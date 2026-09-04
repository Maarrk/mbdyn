#!/usr/bin/env python3
"""Synthetic parity smoke test for the retained Bash testsuite runners.

It intentionally creates only temporary inputs and mock executables.  It is
safe to run on a developer machine and must never be replaced by a full-suite
test in local pre-commit checks.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from simple_testsuite import InputSpec, PatchSpec, Task, abort_after_file, junit_status_ok, octave_environment, patch_file

ROOT = pathlib.Path(__file__).resolve().parent

INPUT = """begin: data;
end: data;
begin: initial value;
end: initial value;
begin: control data;
    use automatic differentiation;
end: control data;
begin: nodes;
end: nodes;
begin: elements;
end: elements;
"""

MOCK = """#!/usr/bin/env python3
import os, pathlib, sys, time
if os.environ.get('MBDYN_PARITY_REQUIRE_PATCH_ENV'):
    required = ('MBD_TESTSUITE_INITIAL_VALUE_BEGIN', 'MBD_TESTSUITE_INITIAL_VALUE_END', 'MBD_TESTSUITE_CONTROL_DATA_BEGIN', 'MBD_TESTSUITE_CONTROL_DATA_END')
    if not all(os.environ.get(name) for name in required):
        raise SystemExit(7)
trace = os.environ.get('MBDYN_PARITY_TRACE')
if trace:
    with open(trace, 'a') as file:
        file.write(f'{time.monotonic():.9f}\\n')
    time.sleep(float(os.environ.get('MBDYN_PARITY_SLEEP', '.20')))
config_trace = os.environ.get('MBDYN_PARITY_CONFIG_TRACE')
if config_trace:
    output = sys.argv[sys.argv.index('-o') + 1]
    with open(config_trace, 'a') as file:
        file.write(f'{time.monotonic():.9f}\\t{output}\\t{os.environ.get("TMPDIR", "")}\\n')
    time.sleep(float(os.environ.get('MBDYN_PARITY_SLEEP', '.20')))
print('End of simulation at time 0 after 3 steps;')
for arg in sys.argv:
    if arg.startswith('--gtest_output=xml:'):
        pathlib.Path(arg.split(':', 1)[1]).write_text('<testsuites tests="1" failures="0" disabled="0" errors="0"></testsuites>')
if any(arg.endswith('fail.mbd') for arg in sys.argv):
    raise SystemExit(1)
"""

def invoke(program: pathlib.Path, args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(program), *args], text=True, capture_output=True, env=env)

def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mbdyn-testsuite-parity-") as temp:
        work = pathlib.Path(temp)
        inputs = work / "inputs"; inputs.mkdir()
        (inputs / "valid.mbd").write_text(INPUT + "socket: inet, port, 24567;\n")
        # The AWK recognizer must reject a merely keyword-containing file.
        (inputs / "malformed.mbd").write_text("begin: data; begin: initial value;\n")
        mock = work / "mock-mbdyn"; mock.write_text(MOCK); mock.chmod(0o755)
        env = os.environ | {"MBDYN_ARGS_ADD": "", "HOME": str(work)}
        python_env = octave_environment({"mbdyn_exec": str(mock), "mbdyn_args": "", "enable_gtest": "no"}, str(work / "report.xml"))
        require(str(ROOT.parent / "libraries" / "libmbc") in python_env["PYTHONPATH"], "custom Python runners cannot import libmbc")

        # junit_status_ok() is an in-process port of the permissive legacy
        # AWK parser.  Compare direct exit-status semantics, including odd
        # but intentional cases where AWK ignores malformed XML attributes.
        junit_cases = {
            "valid.xml": '<testsuites tests="2" failures="0" disabled="0" errors="0"></testsuites>\n',
            "failure.xml": '<testsuites tests="2" failures="1" disabled="0" errors="0"></testsuites>\n',
            "error.xml": '<testsuites tests="2" failures="0" disabled="0" errors="1"></testsuites>\n',
            "reordered.xml": '<testsuites failures="1" tests="2" disabled="0" errors="0"></testsuites>\n',
            "octave.txt": '  FAIL> 2\nPASSES 3 out of 4 test\n',
            "gtest.txt": 'x <FAILED> x 3 test\n',
            "malformed.xml": '<testsuites this is not XML>\n',
        }
        for name, contents in junit_cases.items():
            candidate = work / name; candidate.write_text(contents)
            awk_status = subprocess.run(["awk", "-f", str(ROOT / "parse_test_suite_status.awk"), str(candidate)], capture_output=True).returncode == 0
            require(junit_status_ok(str(candidate)) == awk_status, f"JUnit AWK parity differs for {name}")
        missing_junit = work / "missing.xml"
        require(not junit_status_ok(str(missing_junit)), "missing JUnit file must fail like AWK")
        base = ["--prefix-input", str(inputs), "--mbdyn-exec", str(mock), "--tasks", "2", "--keep-output", "all"]

        legacy = invoke(ROOT / "simple_testsuite_legacy.sh", ["--prefix-output", str(work / "legacy"), *base], env)
        python = invoke(ROOT / "simple_testsuite.py", ["plain", "--prefix-output", str(work / "python"), *base], env)
        require(legacy.returncode == python.returncode == 0, f"plain return codes: legacy={legacy.returncode}, python={python.returncode}")
        require("1 valid input files were found" in legacy.stdout and "1 valid input files were found" in python.stdout, "input discovery differs")
        require("passed{Steps=3}" in legacy.stdout and "passed{Steps=3}" in python.stdout, "JUnit/step reporting differs")

        reference = work / "reference"; reference.mkdir()
        (reference / "pass.mbd").write_text(INPUT)
        (reference / "fail.mbd").write_text(INPUT + "## @MBDYN_SIMPLE_TESTSUITE_EXIT_STATUS@ = 1\n")
        reference_args = ["--prefix-input", str(reference), "--mbdyn-exec", str(mock), "--use-reference-test-status", "yes", "--keep-output", "all"]
        legacy_reference = invoke(ROOT / "simple_testsuite_legacy.sh", ["--prefix-output", str(work / "legacy-reference"), *reference_args], env)
        python_reference = invoke(ROOT / "simple_testsuite.py", ["plain", "--prefix-output", str(work / "python-reference"), *reference_args], env)
        require(legacy_reference.returncode == python_reference.returncode, f"reference-status exit differs: legacy={legacy_reference.returncode}, python={python_reference.returncode}")
        require("KNOWN-FAILURES" in legacy_reference.stdout and "KNOWN-FAILURES" in python_reference.stdout and "FIXED-FAILURES" in legacy_reference.stdout and "FIXED-FAILURES" in python_reference.stdout, "reference-status classification differs")

        # This crosses each non-trivial compatibility rule while remaining
        # tiny (31 accepted configurations), and catches matrix-pruning drift.
        matrix = ["--linear-solvers", "umfpack pardiso siconosdense", "--matrix-handlers", "map grad", "--scale-methods", "rowmaxcolumnmax", "--scale-when", "never", "--autodiff", "autodiff noautodiff", "--nonlinear-solvers", "newtonraphson nox-newton-krylov mcpnewtonminfb mcpnewtonfb siconosmcpnewtonfb bfgs", "--method", "impliciteuler ms2,0.6", "--output-format", "netcdf-text", "--abort-after", "regularstep,2", "--skip-initial-joint-assembly", "not-skip", "--initial-assembly-of-deformable-and-force-elements", "exclude", "--exec-solver", "no", "--keep-output", "all"]
        legacy_patch = invoke(ROOT / "simple_testsuite_patched_legacy.sh", ["--prefix-output", str(work / "legacy-patched"), "--prefix-input", str(inputs), *matrix], env)
        python_patch = invoke(ROOT / "simple_testsuite.py", ["patched", "--prefix-output", str(work / "python-patched"), "--prefix-input", str(inputs), *matrix], env)
        require(legacy_patch.returncode == python_patch.returncode == 0, "patched return code differs")
        legacy_configs = sorted(path.parent.relative_to(work / "legacy-patched") for path in (work / "legacy-patched").rglob("mbd_init_val_end.set"))
        python_configs = sorted(path.parent.relative_to(work / "python-patched") for path in (work / "python-patched").rglob("mbd_init_val_end.set"))
        require(legacy_configs == python_configs and len(legacy_configs) == 31, "patched configuration matrix differs")
        for tail in legacy_configs:
            for name in ("mbd_init_val_begin.set", "mbd_init_val_end.set", "mbd_control_data_begin.set", "mbd_control_data_end.set"):
                require((work / "legacy-patched" / tail / name).read_text() == (work / "python-patched" / tail / name).read_text(), f"patched include differs: {tail}/{name}")

        execution_matrix = ["--linear-solvers", "umfpack", "--matrix-handlers", "map", "--scale-methods", "rowmaxcolumnmax", "--scale-when", "never", "--autodiff", "autodiff", "--nonlinear-solvers", "newtonraphson", "--method", "impliciteuler", "--output-format", "netcdf-text", "--abort-after", "input", "--skip-initial-joint-assembly", "not-skip", "--initial-assembly-of-deformable-and-force-elements", "exclude", "--keep-output", "all"]
        legacy_execution = invoke(ROOT / "simple_testsuite_patched_legacy.sh", ["--prefix-output", str(work / "legacy-patched-execution"), "--prefix-input", str(inputs), "--mbdyn-exec", str(mock), *execution_matrix], env | {"MBDYN_PARITY_REQUIRE_PATCH_ENV": "yes"})
        python_execution = invoke(ROOT / "simple_testsuite.py", ["patched", "--prefix-output", str(work / "python-patched-execution"), "--prefix-input", str(inputs), "--mbdyn-exec", str(mock), *execution_matrix], env | {"MBDYN_PARITY_REQUIRE_PATCH_ENV": "yes"})
        require(legacy_execution.returncode == python_execution.returncode == 0, "patched solver environment differs")

        # The worker's native patcher must remain byte-identical to the sed
        # program, including removal of existing settings and all insertions.
        one = legacy_configs[0]
        legacy_dir = work / "legacy-patched" / one
        source = inputs / "valid.mbd"
        spec = PatchSpec("fixture", str(legacy_dir), *(str(legacy_dir / name) for name in ("mbd_init_val_begin.set", "mbd_init_val_end.set", "mbd_control_data_begin.set", "mbd_control_data_end.set")))
        task = Task(InputSpec(str(source), 1, -1, False, (), None, None), spec)
        patched, copy = patch_file(task)
        expected_env = env | {"MBD_TESTSUITE_INITIAL_VALUE_BEGIN": spec.init_begin, "MBD_TESTSUITE_INITIAL_VALUE_END": spec.init_end, "MBD_TESTSUITE_CONTROL_DATA_BEGIN": spec.control_begin, "MBD_TESTSUITE_CONTROL_DATA_END": spec.control_end}
        expected = subprocess.run(["sed", "-E", "-f", str(ROOT / "mbdyn_testsuite_patch.sed"), str(source)], text=True, capture_output=True, env=expected_env, check=True).stdout
        require(pathlib.Path(patched).read_text() == expected and pathlib.Path(copy).read_text() == expected, "native Python patcher differs from sed")
        pathlib.Path(patched).unlink(); pathlib.Path(copy).unlink()
        abort_task = Task(InputSpec(str(source), 1, -1, False, (), None, None), None)
        aborted, abort_copy = abort_after_file(abort_task, 25, work)
        abort_expected = subprocess.run(["sed", "-E", r"/^[[:space:]]*\<end\>:[[:space:]]*initial[[:space:]]*value[[:space:]]*;[[:space:]]*$/i abort after: regular step, 25;", str(source)], text=True, capture_output=True, check=True).stdout
        require(pathlib.Path(aborted).read_text() == abort_expected, "native abort-after patcher differs from sed")
        pathlib.Path(aborted).unlink(); pathlib.Path(abort_copy).unlink()

        # Two unrelated inputs declare the same literal TCP port.  They may be
        # assigned to different consumers, but their child simulations must
        # start at least one mock runtime apart.
        locked = work / "locked"; locked.mkdir()
        (locked / "one.mbd").write_text(INPUT + "socket: inet, port, 24568;\n")
        (locked / "two.mbd").write_text(INPUT + "socket: inet, port, 24568;\n")
        trace = work / "port-starts"; locked_env = env | {"MBDYN_PARITY_TRACE": str(trace)}
        lock_run = invoke(ROOT / "simple_testsuite.py", ["plain", "--prefix-output", str(work / "locked-out"), "--prefix-input", str(locked), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--tasks", "2"], locked_env)
        require(lock_run.returncode == 0, "port-lock fixture failed")
        starts = sorted(float(value) for value in trace.read_text().splitlines())
        require(len(starts) == 2 and starts[1] - starts[0] >= .18, "TCP-port resource lock did not serialize consumers")

        unlocked = work / "unlocked"; unlocked.mkdir()
        (unlocked / "one.mbd").write_text(INPUT)
        (unlocked / "two.mbd").write_text(INPUT)
        parallel_trace = work / "parallel-starts"
        parallel_run = invoke(ROOT / "simple_testsuite.py", ["plain", "--prefix-output", str(work / "unlocked-out"), "--prefix-input", str(unlocked), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--tasks", "2"], env | {"MBDYN_PARITY_TRACE": str(parallel_trace)})
        require(parallel_run.returncode == 0, "unlocked concurrency fixture failed")
        parallel_starts = sorted(float(value) for value in parallel_trace.read_text().splitlines())
        require(len(parallel_starts) == 2 and parallel_starts[1] - parallel_starts[0] < .18, "independent tasks did not use both consumers")

        # Patched tasks share read-only include files, but each invocation has
        # its own TMPDIR.  The global producer must therefore allow distinct
        # inputs using the same configuration to occupy separate consumers.
        config_trace = work / "configuration-starts"
        configuration_output = work / "configuration-out"
        configuration_run = invoke(ROOT / "simple_testsuite.py", ["patched", "--prefix-output", str(configuration_output), "--prefix-input", str(unlocked), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--tasks", "4", "--linear-solvers", "umfpack klu", "--matrix-handlers", "map", "--scale-methods", "rowmaxcolumnmax", "--scale-when", "always", "--autodiff", "autodiff", "--nonlinear-solvers", "newtonraphson", "--method", "impliciteuler", "--output-format", "netcdf-text", "--abort-after", "none", "--skip-initial-joint-assembly", "not-skip", "--initial-assembly-of-deformable-and-force-elements", "exclude"], env | {"MBDYN_PARITY_CONFIG_TRACE": str(config_trace)})
        starts_by_configuration: dict[str, list[float]] = {}
        task_tmpdirs: set[str] = set()
        for line in config_trace.read_text().splitlines():
            started, output_name, task_tmpdir = line.split("\t", 2)
            starts_by_configuration.setdefault(str(pathlib.Path(output_name).parent), []).append(float(started))
            task_tmpdirs.add(task_tmpdir)
        require(configuration_run.returncode == 0 and len(starts_by_configuration) == 2 and all(len(starts) == 2 and max(starts) - min(starts) < .18 for starts in starts_by_configuration.values()), "global patched scheduler serialized a configuration")
        require(len(task_tmpdirs) == 4 and not list(configuration_output.rglob(".tmp-*")), "patched tasks did not use and remove isolated TMPDIRs")

        # An idle consumer is entitled to finish before a busy one.  Its
        # clean exit must not abort the still-running task (the failure seen
        # in the first desktop producer/consumer validation).
        one_task = work / "one-task"; one_task.mkdir()
        (one_task / "only.mbd").write_text(INPUT)
        orderly_exit = invoke(ROOT / "simple_testsuite.py", ["plain", "--prefix-output", str(work / "one-task-out"), "--prefix-input", str(one_task), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--tasks", "2"], env | {"MBDYN_PARITY_TRACE": str(work / "one-task-trace"), "MBDYN_PARITY_SLEEP": "1.2"})
        require(orderly_exit.returncode == 0 and "FAILED-UNEXPECTED: 0" in orderly_exit.stdout, "an orderly idle-consumer exit aborted pending work")

        # The producer must rotate configurations across files rather than
        # enumerate a complete matrix for the first file before touching the
        # second.  Dry-run makes this scheduling invariant observable without
        # executing a solver.
        manifest = invoke(ROOT / "simple_testsuite.py", ["patched", "--dry-run", "--prefix-output", str(work / "manifest-out"), "--prefix-input", str(unlocked), "--linear-solvers", "umfpack klu", "--matrix-handlers", "map", "--scale-methods", "rowmaxcolumnmax", "--scale-when", "always", "--autodiff", "autodiff", "--nonlinear-solvers", "newtonraphson", "--method", "impliciteuler", "--output-format", "netcdf-text", "--abort-after", "none", "--skip-initial-joint-assembly", "not-skip", "--initial-assembly-of-deformable-and-force-elements", "exclude"], env)
        task_lines = [line.split("\t") for line in manifest.stdout.splitlines() if line.startswith("TASK\t")]
        require(manifest.returncode == 0 and len(task_lines) == 4 and task_lines[0][3] != task_lines[1][3], f"producer did not round-robin configurations across inputs: rc={manifest.returncode}, lines={task_lines!r}")

        # Custom run and generator scripts are public-test features.  Verify
        # command selection, the exported MBDYN_EXEC value, and the legacy
        # per-input -o name used by generators.
        for label, program in (("legacy-custom", ROOT / "simple_testsuite_legacy.sh"), ("python-custom", ROOT / "simple_testsuite.py")):
            scripted = work / label; scripted.mkdir()
            (scripted / "case.mbd").write_text(INPUT)
            run_script = scripted / "case_run.sh"
            run_script.write_text("#!/bin/sh\ntest \"$MBDYN_EXEC\" = \"$MBDYN_PARITY_EXPECT_EXEC\"\n")
            run_script.chmod(0o755)
            command = (["plain"] if program.name == "simple_testsuite.py" else []) + ["--prefix-output", str(work / f"{label}-out"), "--prefix-input", str(scripted), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--keep-output", "all"]
            completed = invoke(program, command, env | {"MBDYN_PARITY_EXPECT_EXEC": str(mock)})
            require(completed.returncode == 0 and "PASSED" in completed.stdout, f"custom run script differs: {label}")

        # --abort-after-step implies an input patch.  Legacy deliberately
        # skips custom run scripts in that case, since they may ignore the
        # patched input file; the Python worker must make the same choice.
        for label, program in (("legacy-custom-patch", ROOT / "simple_testsuite_legacy.sh"), ("python-custom-patch", ROOT / "simple_testsuite.py")):
            scripted = work / label; scripted.mkdir()
            (scripted / "case.mbd").write_text(INPUT)
            invocation_trace = work / f"{label}-invoked"
            run_script = scripted / "case_run.sh"
            run_script.write_text(f"#!/bin/sh\nprintf invoked > {invocation_trace}\n")
            run_script.chmod(0o755)
            output_dir = work / f"{label}-out"
            command = (["plain"] if program.name == "simple_testsuite.py" else []) + ["--prefix-output", str(output_dir), "--prefix-input", str(scripted), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--abort-after-step", "1", "--exit-status-mask", "0x1", "--timing", "yes"]
            completed = invoke(program, command, env)
            require(completed.returncode == 0 and "SKIPPED" in completed.stdout and not invocation_trace.exists(), f"patched custom run script was not skipped: {label}; rc={completed.returncode}; stdout={completed.stdout!r}; stderr={completed.stderr!r}")
            require("test\tpatched\t" in (output_dir / "mbdyn-testsuite-timing.tsv").read_text(), f"abort-after timing mode differs: {label}")
        for label, program in (("legacy-generator", ROOT / "simple_testsuite_legacy.sh"), ("python-generator", ROOT / "simple_testsuite.py")):
            generated = work / label; generated.mkdir()
            (generated / "case.mbd").write_text(INPUT)
            trace_file = work / f"{label}-generator-output"
            gen_script = generated / "case_gen.sh"
            gen_script.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$4\" > {trace_file}\n")
            gen_script.chmod(0o755)
            command = (["plain"] if program.name == "simple_testsuite.py" else []) + ["--prefix-output", str(work / f"{label}-out"), "--prefix-input", str(generated), "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--keep-output", "all"]
            completed = invoke(program, command, env)
            require(completed.returncode == 0, f"generator stage differs: {label}")
            require(trace_file.read_text().strip().endswith("case_mbdyn_output_1"), f"generator output name differs: {label}")
    print("simple_testsuite Python/legacy synthetic parity: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
