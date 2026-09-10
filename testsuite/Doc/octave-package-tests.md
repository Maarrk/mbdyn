# Octave package test scheduler

## Where it is used

The standalone `octave-pkg-test-job` invokes `octave_pkg_testsuite.m`. The same
script is also invoked after the MBDyn module phase by
`mbdyn-modules-test-job-gcov`, so package tests contribute to accumulated gcov
data.

This runner is independent of `simple_testsuite.py`. It uses the
`mboct-octave-pkg` function `run_parallel`, Octave child processes, and one
scheduling unit per top-level package `.m` or `.tst` file.

## Package selection

The root CI variable `OCT_PKG_LIST` is a whitespace-separated list of records:

```text
PACKAGE:REBUILD:BRANCH:TEST:TIMEOUT
```

The package test script reads the fourth field and selects only records whose
`TEST` value is `yes`. The rebuild, branch, and timeout fields are principally
consumed by package build/orchestration logic; this script does not apply the
per-record timeout itself.

Current defaults select:

- `mboct-octave-pkg`;
- `mboct-numerical-pkg`;
- `mboct-mbdyn-pkg`.

Current defaults do not select `nurbs`, `netcdf`, or `mboct-fem-pkg`. Pipeline
variables can change every selection.

Two related CI variables currently do not alter this runner:
`OCT_PKG_TEST_MODE` is declared in the root YAML but is not passed to or parsed
by `octave_pkg_testsuite.m`, and `OCT_PKG_FUNCTION_FILTER` is declared by the
job YAML but is not consumed. Likewise, each package record carries a timeout,
but this script does not parse or enforce it. These settings should not be used
to predict current scheduling behavior without a corresponding code change.

For each selected package the script executes `pkg load`, locates the installed
package directory, and lists only `.m` and `.tst` files directly in that
directory. It does not recursively traverse package subdirectories. Every
listed filename becomes one parameter for `run_parallel`.

## Scheduling model

`--tasks N` sets `opts.number_of_processors`; CI currently passes 32.
`opts.reuse_subprocess=false`, so each scheduling unit starts a fresh Octave
child instead of reusing a long-lived interpreter. `run_parallel` maintains up
to N children and polls completion every 100 ms. As a child finishes, another
top-level file can be started until the list is exhausted.

The maximum is therefore 32 concurrently scheduled Octave files, not
necessarily 32 `mbdyn` processes. A single `.m`/`.tst` file may:

- run only Octave assertions and never start MBDyn;
- start one MBDyn simulation at a time;
- start several MBDyn simulations in code controlled by that test/package.

The scheduler cannot interleave individual test blocks or nested simulations
from within one file. Near the end, or while long MBDyn-heavy files coexist
with Octave-only files, observed `mbdyn` concurrency can be much lower than 32
even while the Octave scheduler is behaving correctly. Improving that requires
finer-grained package test discovery or explicit scheduling inside the package,
not a change to `simple_testsuite.py`.

Unlike optimized public/private/module jobs, this runner does not use
`MBD_TESTSUITE_GLOBAL_SLOT_DIR`, `MBD_TESTSUITE_GLOBAL_SLOTS`, or automatic
literal-port locks. Package tests that launch MBDyn are responsible for their
own shared-resource safety.

## Per-file execution

Before every spawn, `octave_pkg_testsuite_hook.m` creates a unique
zero-padded output directory such as `OUTPUT/003`, sets `TMPDIR` to it, and
passes package/file names to `gtest-octave-cli` as suite/test labels.

`octave_pkg_testsuite_exec.m` then:

1. identifies the package and filename for the task;
2. constructs
   `MBOCT_MBDYN_PKG_MBDYN_SOLVER_COMMAND` from the selected MBDyn executable,
   added arguments, and a per-directory JUnit XML pattern;
3. enables Octave profiling;
4. calls `test(FILENAME, "normal")`;
5. records passed/maximum assertion counts, elapsed time, and profile data;
6. returns the structured status to the parent.

The JUnit pattern contains `%03d` because one top-level package test may launch
multiple MBDyn instances. Standard output is redirected to per-task
`fntests.out`; the test harness also produces per-index JUnit XML and generated
test scripts as applicable.

The YAML sets `OMP_NUM_THREADS=1`, `MKL_NUM_THREADS=1`, and
`OPENBLAS_NUM_THREADS=1` to avoid nested numerical-library parallelism. The
script exports `MBD_NUM_THREADS` from `--threads`, whose default is 1.

## Result aggregation

For each completed file, the parent computes failures as `NMAX - N`, adds
counts to package and global totals, and prints a line with the file's elapsed
time. Any exception is reported through the GoogleTest integration and
re-raised. The script exits 1 if the aggregate regression/failure count is
positive, otherwise 0.

There is a current reporting caveat in `octave_pkg_testsuite.m`: the statement
that adds `status.test.N` to the per-package passed count appears twice, while
the global passed count is incremented once. Therefore the printed per-package
passed totals can be doubled even though the global totals and exit decision
use their separate counters. This is current implementation behavior, not part
of the intended scheduling model.

The standalone job uploads:

- its main log and status file when present;
- `junit_xml_report_octave_*.xml` files;
- `fntests.out` and `fntests.log`;
- generated `octave_pkg_testsuite_test_*.m` and `.tst` files.

JUnit paths are made relative to `CI_PROJECT_DIR` before GitLab ingests them.

## Options

| Option | Meaning |
|---|---|
| `--octave-pkg-list LIST`, `-p LIST` | Package records; selects entries with fourth field `yes`. Required. |
| `--octave-pkg-test-dir DIR` | Absolute/relative output root, converted to an absolute path. Required. |
| `--octave-exec COMMAND` | Child Octave executable used by `run_parallel`. |
| `--octave-args-append ARGS` | Extra arguments passed to child Octave processes. |
| `--tasks N`, `-t N` | Maximum concurrent top-level package files; initial default is 1, CI passes 32. |
| `--threads N` | `MBD_NUM_THREADS` for nested MBDyn work; default 1. |
| `--mbdyn-exec COMMAND` | Solver executable in the package command; default `mbdyn`. |
| `--mbdyn-args-add ARGS` | Solver arguments; script default `-CGF`. |
| `--verbose` | Enable verbose `run_parallel` behavior. |
| `--help`, `-h` | Print the script's short built-in help and return. |

## Diagnosing low MBDyn occupancy

The most useful distinction is between Octave child occupancy and nested MBDyn
occupancy. During a run, inspect both process types. If close to 32 Octave
children exist but only a few MBDyn processes do, the active package files are
mostly in Octave setup/assertions or contain serialized nested simulations. If
both counts are low before the source is exhausted, investigate `run_parallel`
polling, child failures, or package-file discovery. If both decline only at the
end, that is the normal unequal-duration tail.

Per-file elapsed lines in the main log identify long scheduling units. Profile
data collected in each child can then distinguish Octave work from time spent
waiting on external MBDyn processes. There is currently no scheduler-state TSV
equivalent to `simple_testsuite.py`; adding one would require instrumentation in
`run_parallel` or its hooks.
