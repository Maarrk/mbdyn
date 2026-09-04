#!/usr/bin/env python3
"""Small, local-only smoke test for the Python simple testsuite runner."""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from simple_testsuite import junit_status_ok, octave_environment

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
required = ('MBD_TESTSUITE_INITIAL_VALUE_BEGIN', 'MBD_TESTSUITE_INITIAL_VALUE_END',
            'MBD_TESTSUITE_CONTROL_DATA_BEGIN', 'MBD_TESTSUITE_CONTROL_DATA_END')
if os.environ.get('MBDYN_PARITY_REQUIRE_PATCH_ENV') and not all(os.environ.get(x) for x in required):
    raise SystemExit(7)
trace = os.environ.get('MBDYN_PARITY_TRACE')
if trace:
    with open(trace, 'a') as file:
        file.write(f'{time.monotonic():.9f}\\n')
    time.sleep(.20)
print('End of simulation at time 0 after 3 steps;')
for arg in sys.argv:
    if arg.startswith('--gtest_output=xml:'):
        pathlib.Path(arg.split(':', 1)[1]).write_text('<testsuites tests="1" failures="0" disabled="0" errors="0"></testsuites>')
if any(arg.endswith('fail.mbd') for arg in sys.argv):
    raise SystemExit(1)
"""

def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)

def invoke(args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(ROOT / "simple_testsuite.py"), *args],
                          text=True, capture_output=True, env=env)

def main() -> int:
    with tempfile.TemporaryDirectory(prefix="simple-testsuite-smoke-") as temporary:
        work = pathlib.Path(temporary)
        inputs = work / "inputs"; inputs.mkdir()
        (inputs / "valid.mbd").write_text(INPUT)
        (inputs / "malformed.mbd").write_text("begin: data; begin: initial value;\n")
        mock = work / "mock-mbdyn"; mock.write_text(MOCK); mock.chmod(0o755)
        env = os.environ | {"MBDYN_ARGS_ADD": "", "HOME": str(work)}
        python_env = octave_environment({"mbdyn_exec": str(mock), "mbdyn_args": "", "enable_gtest": "no"}, str(work / "report.xml"))
        require(str(ROOT.parent / "libraries" / "libmbc") in python_env["PYTHONPATH"], "libmbc path missing")

        for name, contents in {
            "valid.xml": '<testsuites tests="2" failures="0" disabled="0" errors="0"></testsuites>\n',
            "failure.xml": '<testsuites tests="2" failures="1" disabled="0" errors="0"></testsuites>\n',
            "reordered.xml": '<testsuites failures="1" tests="2" disabled="0" errors="0"></testsuites>\n',
        }.items():
            path = work / name; path.write_text(contents)
            awk_ok = subprocess.run(["awk", "-f", str(ROOT / "parse_test_suite_status.awk"), str(path)], capture_output=True).returncode == 0
            require(junit_status_ok(str(path)) == awk_ok, f"JUnit parser mismatch: {name}")
        require(not junit_status_ok(str(work / "missing.xml")), "missing JUnit must fail")

        plain = invoke(["plain", "--prefix-output", str(work / "plain-out"), "--prefix-input", str(inputs),
                        "--mbdyn-exec", str(mock), "--tasks", "2", "--keep-output", "all"], env)
        require(plain.returncode == 0 and "1 valid input files were found" in plain.stdout and "passed{Steps=3}" in plain.stdout,
                f"plain smoke failed: {plain.stdout}\n{plain.stderr}")

        reference = work / "reference"; reference.mkdir()
        (reference / "pass.mbd").write_text(INPUT)
        (reference / "fail.mbd").write_text(INPUT + "## @MBDYN_SIMPLE_TESTSUITE_EXIT_STATUS@ = 1\n")
        status = invoke(["plain", "--prefix-output", str(work / "reference-out"), "--prefix-input", str(reference),
                         "--mbdyn-exec", str(mock), "--use-reference-test-status", "yes", "--keep-output", "all"], env)
        require("KNOWN-FAILURES" in status.stdout and "FIXED-FAILURES" in status.stdout,
                "reference-status handling failed")

        matrix = ["--linear-solvers", "umfpack", "--matrix-handlers", "map", "--scale-methods", "rowmaxcolumnmax",
                  "--scale-when", "never", "--autodiff", "autodiff", "--nonlinear-solvers", "newtonraphson",
                  "--method", "impliciteuler", "--output-format", "netcdf-text", "--abort-after", "input",
                  "--skip-initial-joint-assembly", "not-skip",
                  "--initial-assembly-of-deformable-and-force-elements", "exclude"]
        patched = invoke(["patched", "--prefix-output", str(work / "patched-out"), "--prefix-input", str(inputs),
                          "--mbdyn-exec", str(mock), "--keep-output", "all", *matrix],
                         env | {"MBDYN_PARITY_REQUIRE_PATCH_ENV": "yes"})
        require(patched.returncode == 0 and list((work / "patched-out").rglob("mbd_init_val_end.set")),
                f"patched smoke failed: {patched.stdout}\n{patched.stderr}")

        locked = work / "locked"; locked.mkdir()
        (locked / "one.mbd").write_text(INPUT + "socket: inet, port, 24568;\n")
        (locked / "two.mbd").write_text(INPUT + "socket: inet, port, 24568;\n")
        trace = work / "starts"
        locked_run = invoke(["plain", "--prefix-output", str(work / "locked-out"), "--prefix-input", str(locked),
                             "--mbdyn-exec", str(mock), "--enable-gtest", "no", "--tasks", "2"],
                            env | {"MBDYN_PARITY_TRACE": str(trace)})
        starts = sorted(float(value) for value in trace.read_text().splitlines())
        require(locked_run.returncode == 0 and len(starts) == 2 and starts[1] - starts[0] >= .18,
                "TCP port lock did not serialize consumers")
    print("simple_testsuite Python smoke: PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
