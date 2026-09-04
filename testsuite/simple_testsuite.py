#!/usr/bin/env python3
"""Single producer/consumer implementation for the MBDyn simple testsuite.

The Bash entry points select ``plain`` or ``patched`` mode.  Workers never
write shared reports: the parent owns discovery, scheduling, timing and the
final report.
"""
from __future__ import annotations

import argparse
import fcntl
import multiprocessing as mp
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
import time
import traceback
from collections import Counter
from dataclasses import dataclass
from itertools import product
from queue import Empty

ROOT = pathlib.Path(__file__).resolve().parent

@dataclass(frozen=True)
class InputSpec:
    path: str
    index: int
    expected: int
    excluded: bool
    ports: tuple[int, ...]
    run_script: str | None
    gen_script: str | None

@dataclass(frozen=True)
class PatchSpec:
    key: str
    outdir: str
    init_begin: str
    init_end: str
    control_begin: str
    control_end: str

@dataclass(frozen=True)
class Task:
    input: InputSpec
    patch: PatchSpec | None

@dataclass
class Result:
    task: Task
    status: str
    code: int
    patch_ms: int = 0
    lock_ms: int = 0
    run_ms: int = 0
    total_ms: int = 0
    detail: str = ""
    log: str = ""
    output: str = ""
    junit: str = ""
    patched_copy: str = ""
    time_file: str = ""

@dataclass(frozen=True)
class WorkerDone:
    """In-band acknowledgement that a consumer has flushed all results."""
    pid: int

def ms() -> int:
    return time.monotonic_ns() // 1_000_000

def words(value: str) -> list[str]:
    return value.split()

def default_tasks() -> int:
    """Match the Bash default: physical cores, unless explicitly overridden."""
    configured = os.environ.get("MBD_NUM_TASKS")
    if configured:
        return int(configured)
    try:
        info = subprocess.run(["lscpu"], text=True, capture_output=True, check=True).stdout
        sockets = re.search(r"^Socket\(s\):\s*(\d+)\s*$", info, re.M)
        cores = re.search(r"^Core\(s\) per socket:\s*(\d+)\s*$", info, re.M)
        if sockets and cores:
            return int(sockets.group(1)) * int(cores.group(1))
    except (OSError, subprocess.CalledProcessError):
        pass
    return os.cpu_count() or 1

def timeout_seconds(value: str) -> float | None:
    if value == "unlimited": return None
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)([smh]?)", value)
    if not match: raise ValueError(f"invalid timeout {value!r}")
    # GNU timeout, used by the Bash runner, interprets a suffix-less value in
    # seconds (not minutes).
    return float(match.group(1)) * {"": 1, "s": 1, "m": 60, "h": 3600}[match.group(2)]

def parser(mode: str) -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog=f"simple_testsuite.py {mode}")
    p.add_argument("--prefix-output", required=True)
    p.add_argument("--prefix-input", required=True)
    p.add_argument("--timeout", default="unlimited")
    p.add_argument("--regex-filter-include", action="append", default=[])
    p.add_argument("--regex-filter-exclude", action="append", default=[])
    p.add_argument("--exclude-inverse-dynamics", type=int, default=0)
    p.add_argument("--exclude-initial-value", type=int, default=0)
    p.add_argument("--threads", type=int, default=int(os.environ.get("MBD_NUM_THREADS", "1")))
    p.add_argument("--tasks", type=int, default=default_tasks())
    p.add_argument("--verbose", choices=("yes", "no"), default="no")
    p.add_argument("--keep-output", choices=("all", "failed", "unexpected", "no"), default="unexpected")
    p.add_argument("--keep-output-junit-xml", choices=("always", "not-passed", "failed", "none"),
                   default=os.environ.get("JUNIT_XML_KEEP_ALL_OUTPUT", "none"))
    p.add_argument("--patch-input", choices=("yes", "no"), default="no")
    p.add_argument("--abort-after-step", type=int, default=None)
    p.add_argument("--skip-expected-failures", choices=("yes", "no"), default="no")
    p.add_argument("--update-reference-test-status", choices=("failed", "passed", "all", "yes", "no"), default="no")
    p.add_argument("--use-reference-test-status", choices=("yes", "no"), default="no")
    p.add_argument("--mbdyn-exec", default=os.environ.get("MBDYN_EXEC", "mbdyn"))
    p.add_argument("--mbdyn-args-add", default=os.environ.get("MBDYN_ARGS_ADD", "-CF"))
    p.add_argument("--exec-gen", choices=("yes", "no"), default="yes")
    p.add_argument("--exec-solver", choices=("yes", "no"), default="yes")
    p.add_argument("--enable-gtest", choices=("yes", "no"), default="yes")
    # The old usage text called this --exec-status-mask, while the code used
    # --exit-status-mask.  Keep both spellings accepted by CI callers.
    p.add_argument("--exit-status-mask", "--exec-status-mask", default="0")
    p.add_argument("--print-resources", choices=("no", "all", "time"), default="no")
    p.add_argument("--timing", choices=("yes", "no"), default=os.environ.get("MBD_TESTSUITE_TIMING", "no"))
    p.add_argument("--suppressed-errors", default="")
    p.add_argument("--dry-run", action="store_true", help="print the producer task manifest without generators or consumers")
    if mode == "patched":
        p.add_argument("--linear-solvers", default="naive umfpack klu pardiso pardiso_64 y12 qr lapack siconossparse siconosdense")
        p.add_argument("--matrix-handlers", default="map cc dir grad")
        p.add_argument("--scale-methods", default="rowmaxcolumnmax iterative lapack rowmax columnmax rowsum columnsum")
        p.add_argument("--scale-when", default="never always once")
        p.add_argument("--nonlinear-solvers", default="newtonraphson linesearch linesearch-modified nox nox-newton-krylov nox-direct nox-broyden2 nox-broyden3 nox-broyden1 mcpnewtonminfb mcpnewtonfb bfgs siconosmcpnewtonminfb siconosmcpnewtonfb")
        p.add_argument("--autodiff", default="autodiff noautodiff")
        p.add_argument("--method", default="impliciteuler cranknicolson ms2,0.6 ms3,0.6 ms4,0.6 ss2,0.6 ss3,0.6 ss4,0.6 hope,0.6 Bathe,0.6 msstc3,0.6 msstc4,0.6 msstc5,0.6 mssth3,0.6 mssth4,0.6 mssth5,0.6 DIRK33 DIRK43 DIRK54 hybrid,ms,0.6")
        p.add_argument("--output-format", default="netcdf-text")
        p.add_argument("--abort-after", default="input assembly derivatives regularstep,2")
        p.add_argument("--skip-initial-joint-assembly", default="not-skip skip")
        p.add_argument("--initial-assembly-of-deformable-and-force-elements", default="exclude include")
        p.add_argument("--configuration-jobs", type=int, default=None, help="deprecated; --tasks is the global worker count")
    return p

def discover(args: argparse.Namespace) -> list[InputSpec]:
    """Use the retained AWK recognizer, which is the legacy definition of input.

    In particular it checks correctly nested, complete sections; a simple text
    search accepts malformed inputs that the shell suite deliberately ignores.
    """
    root = pathlib.Path(args.prefix_input).resolve()
    cache = os.environ.get("MBD_INPUT_FILES_CACHE", "")
    candidates: list[str] = []
    if cache and pathlib.Path(cache).is_file() and pathlib.Path(cache).stat().st_size:
        candidates = [line for line in pathlib.Path(cache).read_text().splitlines() if line]
    else:
        # Keep GNU find's full-path Emacs-regex semantics; Python's re.search
        # would subtly change established CI filters.
        command = ["find", str(root), "(", "-type", "f"]
        for pattern in args.regex_filter_include:
            command.extend(["-and", "-regex", pattern])
        for pattern in args.regex_filter_exclude:
            command.extend(["-and", "-not", "-regex", pattern])
        command.extend(["-and", "-not", "-name", "*_patched_*.mbd", ")", "-print0"])
        listed = subprocess.run(command, capture_output=True)
        if listed.returncode:
            raise RuntimeError(f"input discovery failed under {root}")
        candidates = [item.decode(errors="surrogateescape") for item in listed.stdout.split(b"\0") if item]
    if not candidates:
        return []
    recognized = subprocess.run(
        ["awk", "-v", f"exclude_initial_value={args.exclude_initial_value}",
         "-v", f"exclude_inverse_dynamics={args.exclude_inverse_dynamics}",
         "-f", str(ROOT / "mbdyn_input_file_format.awk"), *candidates],
        text=True, capture_output=True,
    )
    if recognized.returncode:
        raise RuntimeError(f"mbdyn_input_file_format.awk failed: {recognized.stderr.strip()}")
    valid = {str(pathlib.Path(line).resolve()) for line in recognized.stdout.splitlines() if line}
    if cache:
        pathlib.Path(cache).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(cache).write_text("".join(f"{path}\n" for path in candidates if str(pathlib.Path(path).resolve()) in valid))
    found: list[InputSpec] = []
    for name in candidates:
        path = pathlib.Path(name)
        if str(path.resolve()) not in valid:
            continue
        try: text = path.read_text(errors="replace")
        except OSError: continue
        marker = re.findall(r"^\s*##\s*@MBDYN_SIMPLE_TESTSUITE_EXIT_STATUS@\s*=\s*([^\s]*)\s*$", text, re.M)
        expected = int(marker[-1] != "0") if marker else -1
        exclusion_markers = re.findall(r"^\s*##\s*@MBDYN_SIMPLE_TESTSUITE_EXCLUDE@\s*=\s*([01])\s*$", text, re.M)
        # mbdyn_testsuite_exclude_test.awk deliberately lets the last marker
        # win, so an appended '= 0' re-enables a test.
        excluded = bool(exclusion_markers and exclusion_markers[-1] == "1")
        ports_found: set[int] = set()
        for raw_line in text.splitlines():
            line = raw_line.split("#", 1)[0]
            if not re.search(r"socket", line, re.I) or not re.search(r"port[ \t]*,", line, re.I):
                continue
            # The legacy AWK `sub(/.*port.../)` is greedy: use the final
            # literal port clause on a line, then accept digits only.
            remainder = re.split(r".*port[ \t]*,[ \t]*", line, flags=re.I)[-1]
            number = re.match(r"([0-9]+)", remainder)
            if number:
                ports_found.add(int(number.group(1)))
        ports = tuple(sorted(ports_found))
        stem = path.with_suffix("")
        scripts = [(stem.with_name(stem.name + suffix), kind) for suffix, kind in
                   (("_run.m", "run"), ("_gen.m", "gen"), ("_run.sh", "run"), ("_gen.sh", "gen"))]
        script, kind = next(((str(script), kind) for script, kind in scripts if script.is_file()), (None, None))
        run = script if kind == "run" else None
        gen = script if kind == "gen" else None
        found.append(InputSpec(str(path.resolve()), len(found)+1, expected, excluded, ports, run, gen))
    return found

def include_files(spec: PatchSpec, values: dict[str, str]) -> None:
    d = pathlib.Path(spec.outdir); d.mkdir(parents=True, exist_ok=True)
    pathlib.Path(spec.init_begin).write_text("    # mbd_init_val_begin.set currently not used!\n")
    pathlib.Path(spec.init_end).write_text(values["init"])
    pathlib.Path(spec.control_begin).write_text("    print: all, to file;\n")
    pathlib.Path(spec.control_end).write_text(values["control"])
    # Retain the configuration log artifact expected from the old patched
    # driver.  The collector appends final configuration status below.
    log = d / "mbdyn-testsuite-patched.log"
    log.write_text("\n".join([str(d), pathlib.Path(spec.init_begin).read_text(),
                               pathlib.Path(spec.init_end).read_text(), pathlib.Path(spec.control_begin).read_text(),
                               pathlib.Path(spec.control_end).read_text()]) + "\n")

def compatible(linear: str, handler: str, scale: str, when: str, nonlinear: str, autodiff: str,
               method: str, abort: str, skip: str, assembly: str) -> bool:
    """Port the compatibility pruning in simple_testsuite_patched_legacy.sh."""
    if linear in {"naive", "lapack", "qr", "siconosdense", "siconossparse"} and handler != "map": return False
    if linear == "y12" and handler not in {"map", "cc", "dir"}: return False
    if linear in {"pardiso", "pardiso_64", "spqr"} and handler not in {"map", "grad"}: return False
    if when == "never" and scale != "rowmaxcolumnmax": return False
    if linear in {"belos", "amesos", "siconosdense", "siconossparse", "pardiso", "pardiso_64", "qr", "spqr", "y12"} and when != "never": return False
    if handler == "grad" and autodiff == "noautodiff": return False
    if nonlinear == "mcpnewtonminfb" and autodiff == "noautodiff": return False
    if nonlinear == "mcpnewtonfb" and linear in {"siconosdense", "siconossparse"}: return False
    if nonlinear.startswith("siconosmcp") and linear != "siconosdense": return False
    if nonlinear in {"nox", "nox-direct"} or nonlinear.startswith("nox-broyden"):
        if linear in {"naive", "qr", "lapack", "siconosdense"}: return False
    if nonlinear == "bfgs" and linear not in {"spqr", "qr"}: return False
    if abort == "derivatives" and skip == "skip":
        return linear == "umfpack" and handler == "map" and scale == "rowmaxcolumnmax" and when == "never" and nonlinear == "newtonraphson" and method == "impliciteuler"
    if abort != "derivatives" and skip == "skip": return False
    if assembly == "include" and not (abort == "assembly" and skip != "skip"): return False
    if abort == "input":
        return linear == "umfpack" and handler == "map" and scale == "rowmaxcolumnmax" and when == "never" and nonlinear == "newtonraphson" and method == "impliciteuler"
    if abort == "assembly": return nonlinear == "newtonraphson" and method == "impliciteuler"
    if abort == "derivatives": return method == "impliciteuler"
    if abort.startswith("regularstep,"):
        return ((nonlinear == "newtonraphson") or method.startswith("ms2")) and ((linear == "umfpack") or method.startswith("ms2")) and ((handler == "map") or method.startswith("ms2")) and ((scale == "rowmaxcolumnmax") or method.startswith("ms2")) and ((when == "never") or method.startswith("ms2"))
    return True

def patches(args: argparse.Namespace, materialize: bool = True) -> list[PatchSpec]:
    result: list[PatchSpec] = []
    dimensions = (words(args.linear_solvers), words(args.matrix_handlers), words(args.scale_methods), words(args.scale_when), words(args.autodiff), words(args.nonlinear_solvers), words(args.method), words(args.output_format), words(args.abort_after), words(args.skip_initial_joint_assembly), words(args.initial_assembly_of_deformable_and_force_elements))
    base = pathlib.Path(args.prefix_output).resolve()
    for vals in product(*dimensions):
        linear, handler, scale, when, ad, nonlinear, method, output, abort, skip, assembly = vals
        if not compatible(linear, handler, scale, when, nonlinear, ad, method, abort, skip, assembly): continue
        key = "/".join(vals); out = base / key
        spec = PatchSpec(key, str(out), str(out/"mbd_init_val_begin.set"), str(out/"mbd_init_val_end.set"), str(out/"mbd_control_data_begin.set"), str(out/"mbd_control_data_end.set"))
        nonlin = {
            "nox": "nox, minimum step, 1e-12, recovery step, 1e-4",
            "nox-newton-krylov": "nox, modified, 10, jacobian operator, newton krylov, use preconditioner as solver, no, minimum step, 1e-12, recovery step, 1e-4",
            "nox-direct": "nox, use preconditioner as solver, yes, minimum step, 1e-12, recovery step, 1e-12",
            "nox-broyden2": "nox, modified, 10, direction, broyden, minimum step, 1e-12, recovery step, 1e-12",
            "nox-broyden3": "nox, modified, 10, solver, trust region based, direction, broyden, minimum step, 1e-12, recovery step, 1e-12",
            "nox-broyden1": "nox, modified, 10, solver, inexact trust region based, direction, broyden, minimum step, 1e-12, recovery step, 1e-12",
            "nox-broyden-tensor": "nox, modified, 10, solver, tensor based, direction, broyden, minimum step, 1e-12, recovery step, 1e-12",
            "linesearch": "linesearch, default solver options, heavy nonlinear, divergence check, no, lambda min, 1, print convergence info, yes, verbose, yes, abort at lambda min, no",
            "linesearch-heavy-nonlinear": "linesearch, default solver options, heavy nonlinear, divergence check, no, lambda min, 1e-12, print convergence info, yes, verbose, yes, abort at lambda min, no",
            "linesearch-modified": "linesearch, modified, 0, default solver options, heavy nonlinear, divergence check, no, lambda min, 1, print convergence info, yes, verbose, yes, abort at lambda min, no",
            "linesearch-modified-heavy-nonlinear": "linesearch, modified, 0, default solver options, heavy nonlinear, divergence check, no, lambda min, 1e-12, print convergence info, yes, verbose, yes, abort at lambda min, no",
            "bfgs": "bfgs, modified, 10, default solver options, heavy nonlinear, divergence check, no, lambda min, 1e-12, print convergence info, yes, verbose, yes, abort at lambda min, no",
        }.get(nonlinear, nonlinear)
        linear_pre, linear_post = {
            "naive": (",colamd", ""),
            "umfpack": ("", ",max iterations, 10"),
            "pardiso": (", pivot factor, 1e-4", ",max iterations, 100"),
            "pardiso_64": (", pivot factor, 1e-4", ",max iterations, 100"),
            "amesos": ("", ", tolerance, 1e-8, max iterations, 100, preconditioner, klu,verbose,3"),
            "belos": ("", ", tolerance, 1e-8, max iterations, 100, preconditioner, klu,verbose,3"),
        }.get(linear, ("", ""))
        init = f"    linear solver: {linear},{handler}{linear_pre},scale,{scale},{when}{linear_post};\n"
        if abort != "none": init += f"    abort after: {abort};\n"
        init += f"    threads: disable;\n    nonlinear solver: {nonlin};\n    output: iterations, cpu time, solver condition number, stat, yes;\n    tolerance: 1e-4;\n    derivatives tolerance: 1e-4;\n    derivatives max iterations: 10;\n    derivatives coefficient: auto;\n    method: {method};\n"
        control = ("    use automatic differentiation;\n" if ad == "autodiff" else "    # automatic differentiation disabled\n")
        control += ("    initial assembly of deformable and force elements;\n    initial stiffness: 1e6, 1e6;\n" if assembly == "include" else "    ## initial assembly of deformable and force elements;\n    ## initial stiffness: 1e6, 1e6;\n")
        control += "    max iterations: 10;\n" + ("    skip initial joint assembly;\n" if skip == "skip" else "    ## skip initial joint assembly;\n")
        control += {"netcdf":"    output results: netcdf, no text;\n", "netcdf-text":"    output results: netcdf, text;\n", "text":"    # output results: text;\n", "none":"    default output: none;\n"}.get(output, "")
        if materialize:
            include_files(spec, {"init": init, "control": control})
        result.append(spec)
    return result

def patch_file(task: Task) -> tuple[str, str]:
    assert task.patch
    src = pathlib.Path(task.input.path); p = task.patch
    text = src.read_text(errors="replace")
    # This is a line-for-line port of mbdyn_testsuite_patch.sed.  It is kept
    # here rather than spawning sed for every patched task, eliminating one
    # process and preserving the producer/consumer design's Python-only hot
    # path.  simple_testsuite_parity.py compares its output to the legacy sed
    # implementation on every run.
    boundary = {
        "initial_begin": re.compile(r"^[ \t]*\bbegin\b:[ \t]*\binitial[ \t]*value\b[ \t]*;[ \t]*$", re.ASCII),
        "initial_end": re.compile(r"^[ \t]*\bend\b:[ \t]*\binitial[ \t]*value\b[ \t]*;[ \t]*$", re.ASCII),
        "control_begin": re.compile(r"^[ \t]*\bbegin\b:[ \t]*control[ \t]*data[ \t]*;[ \t]*$", re.ASCII),
        "control_end": re.compile(r"^[ \t]*\bend\b:[ \t]*control[ \t]*data\b[ \t]*;[ \t]*$", re.ASCII),
    }
    remove = (
        re.compile(r"^[ \t]*\bmethod\b[ \t]*:.*;.*$", re.ASCII),
        re.compile(r"^[ \t]*\btolerance\b[ \t]*:.*;.*$", re.ASCII),
        re.compile(r"^[ \t]*\bderivatives[ \t]*tolerance\b[ \t]*:.*;.*$", re.ASCII),
        re.compile(r"^[ \t]*(?:\blinear|\bnonlinear)[ \t]*solver[ \t]*:[ \tA-Za-z0-9,.+\-]*;[ \t]*$", re.ASCII),
        re.compile(r"^[ \t]*threads[ \t]*:[ \t]*(?:assembly|solver)[ \t]*,[ \tA-Za-z0-9,.+\-]*;[ \t]*$", re.ASCII),
        re.compile(r"^[ \t]*threads[ \t]*:[ \t]*disable[ \t]*;[ \t]*$", re.ASCII),
        # Deliberately preserve the legacy sed expression's final
        # ``[[:space:]]$`` (without ``*``).  Thus a normal line ending in
        # ``;`` is *not* removed; only one trailing whitespace character
        # triggers the substitution.  This seemingly odd behavior is relied
        # on by the existing noautodiff matrix results.
        re.compile(r"^[ \t]*use[ \t]*automatic[ \t]*differentiation[ \t]*;[ \t]$", re.ASCII),
    )
    output: list[str] = []
    for raw in text.splitlines(keepends=True):
        line = raw[:-1] if raw.endswith("\n") else raw
        if boundary["initial_end"].fullmatch(line):
            output.append('include: "${MBD_TESTSUITE_INITIAL_VALUE_END}";\n')
        if boundary["control_end"].fullmatch(line):
            output.append('include: "${MBD_TESTSUITE_CONTROL_DATA_END}";\n')
        output.append("" if any(pattern.fullmatch(line) for pattern in remove) else raw)
        if boundary["initial_begin"].fullmatch(line):
            output.append('include: "${MBD_TESTSUITE_INITIAL_VALUE_BEGIN}";\n')
        if boundary["control_begin"].fullmatch(line):
            output.append('include: "${MBD_TESTSUITE_CONTROL_DATA_BEGIN}";\n')
    transformed = "".join(output)
    fd, name = tempfile.mkstemp(prefix=src.stem + "_", suffix=f"_patched_{task.input.index}.mbd", dir=src.parent)
    with os.fdopen(fd, "w") as f: f.write(transformed)
    copy = pathlib.Path(p.outdir) / f"{src.stem}_mbdyn_input_file_patched_{task.input.index}.mbd"
    copy.write_text(transformed)
    return name, str(copy)

def abort_after_file(task: Task, step: int, output_dir: pathlib.Path) -> tuple[str, str]:
    src = pathlib.Path(task.input.path)
    boundary = re.compile(r"^[ \t]*\bend\b:[ \t]*initial[ \t]*value[ \t]*;[ \t]*$", re.ASCII)
    output: list[str] = []
    for raw in src.read_text(errors="replace").splitlines(keepends=True):
        line = raw[:-1] if raw.endswith("\n") else raw
        if boundary.fullmatch(line):
            output.append(f"abort after: regular step, {step};\n")
        output.append(raw)
    transformed = "".join(output)
    fd, name = tempfile.mkstemp(prefix=src.stem + "_", suffix=f"_abort_{task.input.index}.mbd", dir=src.parent)
    with os.fdopen(fd, "w") as f: f.write(transformed)
    copy = output_dir / f"{src.stem}_mbdyn_input_file_patched_{task.input.index}.mbd"
    copy.write_text(transformed)
    return name, str(copy)

def run_generators(inputs: list[InputSpec], cfg: dict) -> None:
    """Run each generator once before consumers, preserving shared outputs."""
    seen: set[str] = set()
    for inp in inputs:
        if not inp.gen_script or inp.gen_script in seen or cfg["exec_gen"] == "no": continue
        seen.add(inp.gen_script)
        if inp.gen_script.endswith(".sh"):
            pathlib.Path(inp.gen_script).chmod(pathlib.Path(inp.gen_script).stat().st_mode | 0o111)
        base = pathlib.Path(inp.path).stem
        out = pathlib.Path(cfg["output"]) / f"{base}_mbdyn_output_{inp.index}"
        junit = pathlib.Path(cfg["output"]) / f"junit_xml_report_{base}_{inp.index}.xml"
        env = octave_environment(cfg, str(junit)); env["TMPDIR"] = cfg["output"]
        cmd = ([os.environ.get("OCTAVE_EXEC", "octave"), *shlex.split(env["GTEST_OCTAVE_ARGS"]), "-qf"] if inp.gen_script.endswith(".m") else [])
        cmd += [inp.gen_script, "-f", inp.path, "-o", str(out)]
        try:
            with open(str(out) + ".stdout", "w") as log:
                rc = subprocess.run(cmd, cwd=str(pathlib.Path(inp.path).parent), env=env, timeout=cfg["timeout_seconds"], stdout=log, stderr=subprocess.STDOUT).returncode
        except (OSError, subprocess.TimeoutExpired): rc = 1
        # Legacy pre-generation records a transient result which is replaced
        # by the later solver stage.  Therefore do not turn a generator
        # failure into a synthetic final test result here: the solver's real
        # outcome remains authoritative.
        if rc == 0 or cfg["keep"] not in {"all", "failed"}:
            pathlib.Path(str(out) + ".stdout").unlink(missing_ok=True)
        _ = rc

def octave_environment(cfg: dict, junit: str) -> dict:
    """Environment exported by the Bash runner for both .m and .sh scripts."""
    env = os.environ.copy()
    env["MBDYN_EXEC"] = cfg["mbdyn_exec"]
    env.setdefault("OCTAVE_EXEC", "octave")
    env.setdefault("PYTHON_EXEC", "python3")
    gtest = f"-G --gtest_output=xml:{junit}" if cfg["enable_gtest"] == "yes" else ""
    env["GTEST_MBDYN_ARGS"] = gtest
    env["MBOCT_MBDYN_PKG_MBDYN_SOLVER_COMMAND"] = " ".join(
        part for part in (cfg["mbdyn_exec"], cfg["mbdyn_args"], gtest) if part)
    env["GTEST_OCTAVE_ARGS"] = f"--gtest_output=xml:{junit}" if os.environ.get("OCTAVE_EXEC", "octave").startswith("gtest-") else ""
    # libraries/ is a sibling of testsuite/, not a child of it.  This is
    # required by custom Python runners such as gopal2010_static_run.sh.
    env["PYTHONPATH"] = f"{env.get('PYTHONPATH', '')}:{ROOT.parent / 'libraries' / 'libmbc'}"
    return env

def locks(root: str, ports: tuple[int, ...]):
    fds = []; start = ms(); pathlib.Path(root).mkdir(parents=True, exist_ok=True)
    for port in ports:
        fd = os.open(str(pathlib.Path(root) / f"tcp-port-{port}.lock"), os.O_CREAT | os.O_WRONLY, 0o644)
        fcntl.flock(fd, fcntl.LOCK_EX); fds.append(fd)
    return fds, ms()-start

def worker(task_q: mp.Queue, result_q: mp.Queue, cfg: dict) -> None:
    try:
        while True:
            task = task_q.get()
            if task is None: break
            start = ms(); patch_ms = 0; lock_ms = 0; temporary = copied = None; fds: list[int] = []
            try:
                inp = task.input
                if inp.excluded or (cfg["skip_expected"] and inp.expected != 0) or ((task.patch or cfg["abort_after_step"] is not None) and inp.run_script):
                    result_q.put(Result(task, "skipped", -1, total_ms=ms()-start)); continue
                if cfg["exec_solver"] == "no":
                    result_q.put(Result(task, "skipped", -1, total_ms=ms()-start)); continue
                outdir = pathlib.Path(task.patch.outdir if task.patch else cfg["output"]); outdir.mkdir(parents=True, exist_ok=True)
                filename = inp.path
                if task.patch:
                    ps = ms(); filename, copied = patch_file(task); temporary = filename; patch_ms = ms()-ps
                elif cfg["abort_after_step"] is not None:
                    ps = ms(); filename, copied = abort_after_file(task, cfg["abort_after_step"], outdir); temporary = filename; patch_ms = ms()-ps
                base = pathlib.Path(inp.path).stem; suffix = f"_{inp.index}"
                output = str(outdir / f"{base}_mbdyn_output{suffix}"); log = output + ".stdout"
                junit = str(outdir / f"junit_xml_report_{base}{suffix}.xml")
                time_file = str(outdir / f"{base}_mbdyn_output_time{suffix}.log")
                env = octave_environment(cfg, junit); env["TMPDIR"] = str(outdir)
                if task.patch:
                    env.update({"MBD_TESTSUITE_INITIAL_VALUE_BEGIN": task.patch.init_begin,
                                "MBD_TESTSUITE_INITIAL_VALUE_END": task.patch.init_end,
                                "MBD_TESTSUITE_CONTROL_DATA_BEGIN": task.patch.control_begin,
                                "MBD_TESTSUITE_CONTROL_DATA_END": task.patch.control_end})
                env["OMP_NUM_THREADS"] = "1"
                env["MKL_NUM_THREADS"] = "1"
                env["OPENBLAS_NUM_THREADS"] = "1"
                env["MBD_NUM_THREADS"] = str(cfg["threads"])
                if inp.run_script:
                    if inp.run_script.endswith(".sh"):
                        pathlib.Path(inp.run_script).chmod(pathlib.Path(inp.run_script).stat().st_mode | 0o111)
                    command = ([os.environ.get("OCTAVE_EXEC", "octave"), *shlex.split(env["GTEST_OCTAVE_ARGS"]), "-q", "-f"] if inp.run_script.endswith(".m") else [])
                    command += [inp.run_script, "-f", inp.path, "-o", output]
                else:
                    command = shlex.split(cfg["mbdyn_exec"]) + shlex.split(cfg["mbdyn_args"]) + ["-f", filename, "-o", output] + shlex.split(env.get("GTEST_MBDYN_ARGS", ""))
                if cfg["print_resources"] in {"all", "time"}:
                    command = shlex.split(os.environ.get("TESTSUITE_TIME_CMD", "/usr/bin/time --verbose")) + ["--output", time_file] + command
                fds, lock_ms = locks(cfg["lockdir"], inp.ports)
                run_start = ms()
                with open(log, "w") as lf:
                    try:
                        rc = subprocess.run(command, cwd=str(pathlib.Path(inp.path).parent), env=env, stdout=lf, stderr=subprocess.STDOUT, timeout=cfg["timeout_seconds"]).returncode
                    except subprocess.TimeoutExpired: rc = 124
                    lf.flush()
                run_ms = ms()-run_start
                for fd in fds:
                    fcntl.flock(fd, fcntl.LOCK_UN); os.close(fd)
                fds = []
                status = "passed" if rc == 0 else ("timeout" if rc == 124 else { -2: "interrupted", -15: "terminated", -9: "killed", 130: "interrupted", 143: "terminated", 137: "killed" }.get(rc, "failed"))
                if status == "passed":
                    junit_status = subprocess.run(["awk", "-f", str(ROOT / "parse_test_suite_status.awk"), junit],
                                                   text=True, capture_output=True)
                    if junit_status.returncode == 0:
                        steps = re.findall(r"^End of simulation at time [0-9.-]+ after ([0-9]+) steps;$",
                                           pathlib.Path(log).read_text(errors="replace"), re.M)
                        status = "passed" + (f"{{Steps={steps[-1]}}}" if steps else "")
                if copied and status.startswith("passed") and cfg["keep"] not in {"all"}:
                    pathlib.Path(copied).unlink(missing_ok=True)
                detail = pathlib.Path(time_file).read_text(errors="replace") if pathlib.Path(time_file).is_file() else ""
                result_q.put(Result(task, status, rc, patch_ms, lock_ms, run_ms, ms()-start, detail=detail, log=log, output=output, junit=junit, patched_copy=copied or "", time_file=time_file))
            except Exception:
                outdir = pathlib.Path(task.patch.outdir if task.patch else cfg["output"])
                outdir.mkdir(parents=True, exist_ok=True)
                log = outdir / f"{pathlib.Path(task.input.path).stem}_mbdyn_output_{task.input.index}.stdout"
                detail = traceback.format_exc()
                log.write_text(f"Python testsuite worker failure:\n{detail}")
                result_q.put(Result(task, "unexpected", 255, patch_ms, lock_ms, total_ms=ms()-start, detail=detail, log=str(log)))
            finally:
                for fd in fds:
                    try:
                        fcntl.flock(fd, fcntl.LOCK_UN); os.close(fd)
                    except OSError:
                        pass
                if temporary: pathlib.Path(temporary).unlink(missing_ok=True)
    finally:
        # A process with no work may exit while another consumer is still
        # running.  Its exit is normal, not evidence that work was lost.
        # Queue this after every Result so the parent can distinguish orderly
        # completion from an actual worker crash.
        result_q.put(WorkerDone(os.getpid()))

# Per-test bits are the values written by simple_testsuite_run_test().  The
# final process status is intentionally different; it is assembled from the
# report categories below, exactly like the Bash report epilogue.
STATUS_BITS = {"timeout": 0x2, "suppressed": 0x4, "module": 0x8,
               "failed": 0x10, "unexpected": 0x40, "fixed-failure": 0x80,
               "known-failure": 0, "regression": 0x20, "skipped": 0,
               "passed": 0, "interrupted": 0x40, "terminated": 0x40, "killed": 0x40}
REPORT_BITS = {"timeout": 0x2, "module": 0x4, "suppressed": 0x8,
               "failed": 0x10, "regression": 0x20, "unexpected": 0x40,
               "fixed-failure": 0x80, "known-failure": 0x100,
               "skipped": 0x200}

def base_status(status: str) -> str:
    for prefix in ("known-failure-", "regression-"):
        if status.startswith(prefix):
            return base_status(status[len(prefix):])
    if status == "fixed-failure":
        return "passed"
    for name in ("skipped", "passed", "timeout", "suppressed", "module", "failed", "interrupted", "terminated", "killed", "unexpected"):
        if status.startswith(name): return name
    return "unexpected"

def report_category(status: str) -> str:
    if status.startswith("known-failure-"): return "known-failure"
    if status.startswith("regression-"): return "regression"
    if status == "fixed-failure": return "fixed-failure"
    base = base_status(status)
    return "unexpected" if base in {"interrupted", "terminated", "killed"} else base

def classify(result: Result, args: argparse.Namespace) -> tuple[str, int]:
    original_status = result.status
    status = base_status(original_status)
    if status == "failed" and result.log:
        parser = ROOT / "parse_mbdyn_error_message.awk"
        if parser.is_file():
            parsed = subprocess.run(["awk", "-v", f"suppressed_errors={args.suppressed_errors}", "-f", str(parser), result.log], text=True, capture_output=True)
            if parsed.returncode == 0:
                status = f"suppressed:{parsed.stdout.strip()}"
            elif parsed.stdout.strip().startswith(("module", "loadable")):
                status = "module"
        else:
            try: log = pathlib.Path(result.log).read_text(errors="replace").lower()
            except OSError: log = ""
            if "module" in log or "loadable" in log: status = "module"
            elif args.suppressed_errors and any(x and x in log for x in args.suppressed_errors.split("|")):
                status = "suppressed"
    if status in {"passed", "skipped"}:
        # Preserve the useful passed{Steps=N} detail emitted by the worker.
        status = original_status
    category = report_category(status)
    return status, STATUS_BITS.get(category, 0x40)

def update_reference(result: Result, status: str, bit: int, args: argparse.Namespace) -> int:
    """Mirror the legacy update-before-reference-comparison ordering."""
    setting = args.update_reference_test_status
    original_expected = result.task.input.expected
    if setting == "no": return original_expected
    failed = bit != 0
    update = bit != STATUS_BITS["suppressed"] and (setting in {"all", "yes"} or (setting == "failed" and failed) or (setting == "passed" and not failed))
    path = pathlib.Path(result.task.input.path)
    try: text = path.read_text()
    except OSError: return original_expected
    value = str(bit)
    marker = r"^\s*##\s*@MBDYN_SIMPLE_TESTSUITE_EXIT_STATUS@\s*=\s*[0-9]+\s*$"
    line = f"## @MBDYN_SIMPLE_TESTSUITE_EXIT_STATUS@ = {value}"
    if original_expected == -1:
        text += (f"\n{'#'*110}\n## Variables to be updated by simple_testsuite.sh --update-reference-test-status\n"
                 f"## Warning, do not edit!!!\n{line}\n{'#'*110}\n")
        path.write_text(text)
    elif update:
        path.write_text(re.sub(marker, line, text, flags=re.M))
    # The Bash implementation assigns expected_test_status=test_status after
    # any update-reference invocation, even if no textual replacement occurs.
    return int(failed)

def apply_reference(status: str, expected: int, args: argparse.Namespace) -> str:
    if args.use_reference_test_status != "yes":
        return status
    failed = base_status(status) not in {"passed", "skipped"}
    if failed == bool(expected):
        return f"known-failure-{status}" if failed else status
    return f"regression-{status}" if failed else "fixed-failure"

def cleanup(result: Result, status: str, args: argparse.Namespace) -> None:
    keep = args.keep_output == "all" or (args.keep_output == "failed" and base_status(status) in {"failed", "unexpected"}) or (args.keep_output == "unexpected" and base_status(status) == "unexpected")
    if keep: return
    output = pathlib.Path(result.output) if result.output else None
    # A custom script may ignore -o.  Match the legacy runner's log message
    # before selecting the output prefix, but never delete outside its output
    # tree.
    if result.log and pathlib.Path(result.log).is_file():
        found = re.findall(r'^output in file\s*"([^"]+)"', pathlib.Path(result.log).read_text(errors="replace"), re.M)
        if found:
            candidate = pathlib.Path(found[-1])
            if not candidate.is_absolute():
                candidate = pathlib.Path(result.log).parent / candidate
            try:
                candidate.resolve().relative_to(pathlib.Path(args.prefix_output).resolve())
                output = candidate
            except ValueError:
                pass
    if output and pathlib.Path(str(output) + ".log").is_file():
        for generated in output.parent.glob(output.name + "*"):
            if generated.is_file():
                generated.unlink(missing_ok=True)
    for path in (result.log,):
        if path: pathlib.Path(path).unlink(missing_ok=True)
    if result.time_file:
        pathlib.Path(result.time_file).unlink(missing_ok=True)
    if result.patched_copy: pathlib.Path(result.patched_copy).unlink(missing_ok=True)
    raw_base = base_status(status)
    junit_keep = args.keep_output_junit_xml == "always" or (
        args.keep_output_junit_xml == "not-passed" and raw_base not in {"passed", "timeout"}) or (
        args.keep_output_junit_xml == "failed" and raw_base in {"failed", "unexpected"})
    if result.junit and not junit_keep and pathlib.Path(result.junit).is_file():
        parsed = subprocess.run(["awk", "-f", str(ROOT / "parse_test_suite_status.awk"), result.junit], capture_output=True)
        if parsed.returncode == 0:
            pathlib.Path(result.junit).unlink(missing_ok=True)

def run(mode: str, args: argparse.Namespace) -> int:
    if args.tasks < 1 or args.threads < 1: raise SystemExit("--tasks and --threads must be positive")
    if args.patch_input == "yes" and args.abort_after_step is not None:
        raise SystemExit("--patch-input must not be used in combination with --abort-after-step")
    output = pathlib.Path(args.prefix_output).resolve(); output.mkdir(parents=True, exist_ok=True)
    # The Bash runner implements --abort-after-step with a temporary input
    # patch, and labels that pass as patched for timing.  It is nevertheless
    # distinct from --patch-input, which injects configuration include files.
    abort_patch = args.abort_after_step is not None
    timing_mode = "patched" if mode == "patched" or args.patch_input == "yes" or abort_patch else "unpatched"
    args.prefix_input = str(pathlib.Path(args.prefix_input).resolve())
    lockdir = os.environ.get("MBD_TESTSUITE_RESOURCE_LOCK_DIR", str(output / ".resource-locks"))
    timing = pathlib.Path(os.environ.get("MBD_TESTSUITE_TIMING_FILE", str(output / "mbdyn-testsuite-timing.tsv")))
    if args.timing == "yes" and not timing.exists():
        timing.parent.mkdir(parents=True, exist_ok=True)
        timing.write_text("scope\tmode\ttarget\tindex\tstatus\tpatch_ms\tresource_wait_ms\trun_ms\ttotal_ms\n")
    suite_start = ms(); inputs = discover(args); print(f"{len(inputs)} valid input files were found")
    if mode == "patched":
        specs: list[PatchSpec | None] = patches(args, materialize=not args.dry_run)
    elif args.patch_input == "yes":
        # This compatibility mode is used by callers that provide their own
        # include files through the traditional MBD_TESTSUITE_* variables.
        specs = [PatchSpec("plain-patch", str(output),
                           os.environ.get("MBD_TESTSUITE_INITIAL_VALUE_BEGIN", str(output / "mbd_init_val_begin.set")),
                           os.environ.get("MBD_TESTSUITE_INITIAL_VALUE_END", str(output / "mbd_init_val_end.set")),
                           os.environ.get("MBD_TESTSUITE_CONTROL_DATA_BEGIN", str(output / "mbd_control_data_begin.set")),
                           os.environ.get("MBD_TESTSUITE_CONTROL_DATA_END", str(output / "mbd_control_data_end.set")))]
    else:
        specs = [None]
    if args.dry_run:
        count = 0
        for round_index in range(len(specs)):
            for input_index, inp in enumerate(inputs):
                spec = specs[(round_index + input_index) % len(specs)]
                skipped = inp.excluded or (args.skip_expected_failures == "yes" and inp.expected != 0) or ((spec is not None or abort_patch) and inp.run_script is not None) or args.exec_solver == "no"
                fields = ("SKIP" if skipped else "TASK", "patched" if (spec or abort_patch) else "unpatched", str(inp.index), inp.path)
                print("\t".join((*fields, spec.key if spec else "")))
                count += 1
        print(f"MANIFEST\t{count}\tinputs={len(inputs)}\tconfigurations={len(specs)}")
        return 0
    timeout_seconds_value = timeout_seconds(args.timeout)
    if args.update_reference_test_status != "no":
        args.suppressed_errors = "|".join(value for value in (args.suppressed_errors, "feature") if value)
    cfg = {"output":str(output), "lockdir":lockdir, "mbdyn_exec":args.mbdyn_exec, "mbdyn_args":args.mbdyn_args_add, "timeout":args.timeout, "timeout_seconds":timeout_seconds_value, "keep":args.keep_output, "skip_expected":args.skip_expected_failures == "yes", "exec_solver":args.exec_solver, "exec_gen":args.exec_gen, "enable_gtest":args.enable_gtest, "abort_after_step":args.abort_after_step, "threads":args.threads, "print_resources":args.print_resources}
    run_generators(inputs, cfg)
    # The legacy patched driver performed generator preparation before it
    # began configuration work, so its preparation phase includes this time.
    preparation_ms = ms() - suite_start
    task_q: mp.Queue = mp.Queue(maxsize=max(2, 2*args.tasks)); result_q: mp.Queue = mp.Queue()
    workers = [mp.Process(target=worker, args=(task_q, result_q, cfg)) for _ in range(args.tasks)]
    for p in workers: p.start()
    pending = 0; results: list[Result] = []; config_start: dict[str, int] = {}; config_end: dict[str, int] = {}

    def record(result: Result) -> None:
        """Collect a completed task and emit its parent-owned diagnostics."""
        results.append(result)
        if args.verbose == "yes" and result.log:
            print(pathlib.Path(result.log).read_text(errors="replace"), end="")
        if args.print_resources in {"all", "time"} and result.detail:
            print(result.detail, end="" if result.detail.endswith("\n") else "\n")
        if args.timing == "yes":
            task_mode = "patched" if (result.task.patch or abort_patch) else "unpatched"
            with timing.open("a") as f:
                f.write(f"test\t{task_mode}\t{result.task.input.path}\t{result.task.input.index}\t{result.status}\t{result.patch_ms}\t{result.lock_ms}\t{result.run_ms}\t{result.total_ms}\n")

    def producer_skips(task: Task) -> bool:
        inp = task.input
        return (inp.excluded or (args.skip_expected_failures == "yes" and inp.expected != 0)
                or ((task.patch is not None or abort_patch) and inp.run_script is not None)
                or args.exec_solver == "no")

    def raise_if_worker_failed() -> None:
        failed_workers = [p for p in workers if p.exitcode not in (None, 0)]
        if failed_workers:
            diagnostic = InputSpec("<testsuite consumer>", 0, -1, False, (), None, None)
            exits = ", ".join(f"pid={p.pid}, exitcode={p.exitcode}" for p in failed_workers)
            record(Result(Task(diagnostic, None), "unexpected", 255, detail=f"a testsuite consumer died ({exits})"))
            raise RuntimeError("a testsuite consumer died")

    try:
        if mode == "patched":
            # A patched configuration owns a shared output/TMPDIR.  The
            # legacy outer scheduler runs one input at a time within a
            # configuration, while several configurations run concurrently.
            # Preserve that resource boundary without restoring nested
            # parallelism: at most --tasks configurations are active, and
            # each consumer receives one file/configuration task at a time.
            active: dict[str, list[object]] = {}
            next_configuration = 0

            def schedule_next(state: list[object]) -> bool:
                nonlocal pending
                spec, position = state
                assert isinstance(spec, PatchSpec) and isinstance(position, int)
                while position < len(inputs):
                    task = Task(inputs[position], spec); position += 1; state[1] = position
                    if producer_skips(task):
                        record(Result(task, "skipped", -1, total_ms=0))
                        continue
                    task_q.put(task); pending += 1
                    return True
                config_end[spec.key] = ms()
                return False

            def activate_configuration() -> bool:
                nonlocal next_configuration
                if next_configuration >= len(specs): return False
                spec = specs[next_configuration]; next_configuration += 1
                assert spec is not None
                config_start[spec.key] = ms()
                state: list[object] = [spec, 0]
                active[spec.key] = state
                if not schedule_next(state): active.pop(spec.key, None)
                return True

            while len(active) < args.tasks and next_configuration < len(specs):
                activate_configuration()
            while active:
                try: r = result_q.get(timeout=1)
                except Empty:
                    raise_if_worker_failed()
                    continue
                if isinstance(r, WorkerDone):
                    raise_if_worker_failed()
                    continue
                pending -= 1; record(r)
                assert r.task.patch is not None
                state = active[r.task.patch.key]
                if not schedule_next(state): active.pop(r.task.patch.key, None)
                while len(active) < args.tasks and next_configuration < len(specs):
                    activate_configuration()
        else:
            for inp in inputs:
                task = Task(inp, None)
                if producer_skips(task):
                    record(Result(task, "skipped", -1, total_ms=0))
                    continue
                task_q.put(task); pending += 1
            while pending:
                try: r = result_q.get(timeout=1)
                except Empty:
                    raise_if_worker_failed()
                    continue
                if isinstance(r, WorkerDone): continue
                pending -= 1; record(r)
        for _ in workers: task_q.put(None)
        finished_workers: set[int] = set()
        while len(finished_workers) < len(workers):
            try: r = result_q.get(timeout=1)
            except Empty:
                raise_if_worker_failed()
                continue
            if isinstance(r, WorkerDone):
                finished_workers.add(r.pid)
            else:
                # Results should have been drained before sentinels; retain a
                # late one rather than silently dropping it.
                record(r)
    except KeyboardInterrupt:
        diagnostic = InputSpec("<testsuite interrupted>", 0, -1, False, (), None, None)
        results.append(Result(Task(diagnostic, None), "interrupted", 130, detail="interrupted by signal"))
    finally:
        for p in workers: p.join(timeout=5)
        for p in workers:
            if p.is_alive(): p.terminate(); p.join()
    categories: dict[str, list[Result]] = {}
    for r in results:
        raw_status, bit = classify(r, args)
        expected = update_reference(r, raw_status, bit, args)
        status = apply_reference(raw_status, expected, args); r.status = status
        categories.setdefault(report_category(status), []).append(r)
        cleanup(r, status, args)
    counts = Counter(report_category(r.status) for r in results)
    # The old report treats an empty PASSED section as bit 0x1, even if all
    # tests were deliberately skipped.  Keep high report bits too: shells
    # truncate them on exit, as they did for the Bash implementation.
    total_bits = 0 if counts["passed"] else 0x1
    for category, bit in REPORT_BITS.items():
        if counts[category]:
            total_bits |= bit
    if mode == "patched":
        for spec in specs:
            assert spec is not None
            relevant = [result for result in results if result.task.patch == spec]
            configuration_failed = any(report_category(result.status) not in {"passed", "skipped", "known-failure"}
                                       for result in relevant)
            if configuration_failed:
                pathlib.Path(spec.outdir, ".failed").touch()
            keep_configuration = args.keep_output == "all" or (args.keep_output == "failed" and configuration_failed)
            configuration_log = pathlib.Path(spec.outdir, "mbdyn-testsuite-patched.log")
            with configuration_log.open("a") as log:
                log.write(f"TEST \"{spec.outdir}\" {'FAILED' if configuration_failed else 'PASSED'}\n")
            if not keep_configuration:
                for include in (spec.init_begin, spec.init_end, spec.control_begin, spec.control_end):
                    pathlib.Path(include).unlink(missing_ok=True)
                configuration_log.unlink(missing_ok=True)
    if args.timing == "yes" and mode == "patched":
        with timing.open("a") as f:
            f.write(f"phase\tpatched\t{output}\t\tpreparation\t0\t0\t0\t{preparation_ms}\n")
            for spec in specs:
                assert spec is not None
                relevant = [result for result in results if result.task.patch == spec]
                status = "FAILED" if any(report_category(result.status) not in {"passed", "skipped", "known-failure"} for result in relevant) else "PASSED"
                elapsed = config_end.get(spec.key, ms()) - config_start.get(spec.key, suite_start)
                f.write(f"configuration\tpatched\t{spec.outdir}\t\t{status}\t0\t0\t{elapsed}\t{elapsed}\n")
            f.write(f"phase\tpatched\t{output}\t\tconfigurations\t0\t0\t0\t{ms()-suite_start-preparation_ms}\n")
    print("@BEGIN_SIMPLE_TESTSUITE_REPORT@")
    labels = (("passed", "PASSED"), ("timeout", "TIMEOUT"), ("module", "FAILED-MODULE"), ("suppressed", "FAILED-SUPPRESSED"), ("failed", "FAILED"), ("regression", "REGRESSIONS"), ("unexpected", "FAILED-UNEXPECTED"), ("fixed-failure", "FIXED-FAILURES"), ("known-failure", "KNOWN-FAILURES"), ("skipped", "SKIPPED"))
    for key, label in labels:
        entries = categories.get(key, [])
        print(f"{label}: {len(entries)}")
        for r in entries: print(f"  {r.task.input.path}:{r.status}")
    print("@END_SIMPLE_TESTSUITE_REPORT@")
    if args.timing == "yes":
        with timing.open("a") as f: f.write(f"suite\t{timing_mode}\t{output}\t\t\t0\t0\t0\t{ms()-suite_start}\n")
    mask = int(args.exit_status_mask, 0)
    # The legacy patched driver always masks the plain runner's "no passed
    # tests" bit during its preparation/configuration orchestration.
    if mode == "patched":
        mask |= 0x1
    return total_bits & ~mask

def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] not in {"plain", "patched"}:
        print("usage: simple_testsuite.py {plain|patched} [options]", file=sys.stderr); return 2
    mode = sys.argv[1]; args = parser(mode).parse_args(sys.argv[2:])
    if mode == "patched" and args.configuration_jobs is not None:
        print("simple_testsuite.py: --configuration-jobs is deprecated; --tasks is the global consumer limit", file=sys.stderr)
    return run(mode, args)

if __name__ == "__main__":
    raise SystemExit(main())
