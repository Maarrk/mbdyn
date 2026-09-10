# CI test jobs

## Scope and terminology

The GitLab pipeline includes the test definitions from the files in
`testsuite/`. This document covers the jobs that execute MBDyn input files or
Octave package tests, plus the coverage report job that consumes their data.
It does not describe build-only, checkout, cleanup, deployment, or individual
compiled-program test jobs.

The names “public” and “private” describe different input trees, not access
control implemented by `simple_testsuite.py`:

- public tests use `tests/benchmarks` and, in selected jobs, `tests/restart`;
- private tests use the broader `tests` tree;
- module tests use `modules` and add the installed module directory to the
  dynamic-library search path.

Consequently, the private tree contains the public subdirectories unless input
layout or discovery filters exclude them. Public and private jobs also use
different patched matrices and different plain-test behavior, so they are not
duplicate invocations.

“Original” below means `simple_testsuite.py plain`. In private and module jobs
the original input is temporarily modified with an `abort after: regular step`
statement; the source file itself is not changed. “Patched” means
`simple_testsuite.py patched`, which runs every discovered input against every
compatible selected patch configuration.

## Common CI defaults

The root `.gitlab-ci.yml` currently supplies these relevant defaults:

| Setting | Default | Effect |
|---|---:|---|
| `MBDYN_EXEC` | `mbdyn` | Solver command; CI also offers Valgrind variants. |
| `MBDYN_ARGS_ADD` | `-CGF` | Arguments added to every normal solver command. |
| `MBD_SIMPLE_TESTSUITE_FLAGS_OUTPUT` | `failed` | Retain outputs for failed tests. |
| `MBD_SIMPLE_TESTSUITE_FLAGS_VERBOSE` | `no` | Do not copy each solver log into the CI trace. |
| `MBD_SIMPLE_TESTSUITE_FLAGS` | derived from the preceding two | Common arguments passed to the Python runner. |
| `JUNIT_XML_KEEP_ALL_OUTPUT` | `none` | JUnit retention policy used by the runner. |
| `TESTSUITE_TIME_CMD` | `/usr/bin/time --verbose` | Profiles complete phases and optionally individual solvers. |
| `MBD_TEST_COVERAGE_ENABLED` | `no` | Enables gcov jobs and the coverage report when set to `yes`. |
| `MBD_DEBUG_ENABLED` | `no` | Enables assertion/debug-build jobs when set to `yes`. |

All public, private, and module job families set `MBD_TESTSUITE_TIMING=yes` and
use 32 consumers. Their timeout is currently `unlimited`, reference-status
comparison is enabled, and their exit mask is `0x380`. That mask ignores the
final report bits for fixed failures (`0x080`), known failures (`0x100`), and
skipped tests (`0x200`); other report categories can still fail the phase.

Every listed simple-testsuite job runs discovery dynamically. A phrase such as
“all inputs under `tests/benchmarks`” therefore means all files accepted by the
format recognizer and command-line filters, not every regular file. See
[Input discovery](simple-testsuite.md#input-discovery) for the exact rule.

## Job summary

| Job | Build | Input tree(s) | Phases | Matrix configurations | Enabled by default |
|---|---|---|---|---:|---|
| `mbdyn-tests-public-test-job` | optimized/default | `tests/restart`, `tests/benchmarks` | restarted + original + patched, concurrent | 6,062 current defaults | yes |
| `mbdyn-tests-public-test-job-gcov` | gcov | `tests/benchmarks` | patched only | 2,632 with `scale-when=always` | no; coverage switch |
| `mbdyn-tests-public-test-job-debug` | assertion/debug | `tests/benchmarks` | original then patched | 6,062 current defaults | no; debug switch |
| `mbdyn-tests-public-test-job-nodeps` | no optional dependencies | `tests/benchmarks` | original only, no GoogleTest integration | none | yes |
| `mbdyn-tests-public-test-job-clang` | Clang | `tests/restart`, `tests/benchmarks` | restarted + original, concurrent | none | yes |
| `mbdyn-tests-private-test-job` | optimized/default | `tests` | abort-after-25 original + patched, concurrent | 20 | yes |
| `mbdyn-tests-private-test-job-gcov` | gcov | `tests` | abort-after-25 original then patched | 20 | no; coverage switch |
| `mbdyn-tests-private-test-job-debug` | assertion/debug | `tests` | abort-after-25 original then patched | 20 | no; debug switch |
| `mbdyn-modules-test-job` | optimized/default | `modules` | abort-after-100 original + patched, concurrent | 24 | yes |
| `mbdyn-modules-test-job-gcov` | gcov | `modules`, then Octave packages | abort-after-100 original; package tests | none | no; coverage switch |
| `mbdyn-modules-test-job-debug` | assertion/debug | `modules` | abort-after-100 original only | none | no; debug switch |
| `octave-pkg-test-job` | optimized/default | enabled installed Octave packages | one task per top-level `.m`/`.tst` file | not applicable | yes |
| `mbdyn-testsuite-report-job-coverage` | report only | accumulated gcov data | HTML and split Cobertura reports | not applicable | no; coverage switch |

The configuration counts were computed from the current
`simple_testsuite_patched.ini` and current job overrides. The number of solver
runs in a patched phase is approximately:

```text
accepted inputs × compatible configurations
```

Tasks rejected by exclusion markers, expected-status skipping, run-script
rules, or `--exec-solver no` are still represented in reporting but do not
start MBDyn. In particular, `--skip-expected-failures yes` requires an explicit
expected-status marker equal to zero; an absent marker is skipped too.

## Public jobs

### `mbdyn-tests-public-test-job`

This is the full optimized public job.

Before the main phases it runs `sp_gradient_test` once and runs generators once
for each of the restart and benchmark trees. It then launches three independent
Python runner processes in the background:

1. restarted tests: one plain task per accepted file under `tests/restart`;
2. original tests: one plain task per accepted file under `tests/benchmarks`;
3. patched tests: the full compatible default INI matrix for every accepted
   file under `tests/benchmarks`.

Each process is configured with 32 consumers, but all three share 32 global
solver-slot lock files. Thus the aggregate limit is 32 simultaneous solver/run
script subprocesses, not 96. They also share one port-resource lock directory,
so a restarted, original, or patched input using the same literal TCP port
cannot overlap another such input.

The producer/consumer scheduler exists inside each Python process. The shell
only starts the three phases, waits for all of them, concatenates phase logs in
the stable restarted/original/patched order, and bitwise-ORs their exit codes.

Patched options not mentioned on the command line come from the INI defaults.
At present that produces 6,062 compatible configurations. The matrix is
described in [Patched configuration generation](simple-testsuite.md#patched-configuration-generation).

### `mbdyn-tests-public-test-job-gcov`

This conditional coverage job uses the gcov MBDyn and Octave builds. It runs
`sp_gradient_test`, followed only by patched benchmark tests. It overrides
`scale-when` to `always`; all other patch dimensions use their INI defaults.
The current rule set leaves 2,632 configurations.

Unlike the optimized public job, generator execution is not split into a
separate prepare phase and there is only one runner invocation, so no global
cross-phase slot directory is needed. Its build directory, including generated
coverage data, is retained for downstream coverage jobs.

### `mbdyn-tests-public-test-job-debug`

This conditional job uses the assertion/debug build and appends
`--debug none:stop` to `MBDYN_ARGS_ADD`. It runs `sp_gradient_test`, then the
plain benchmark phase and full default patched benchmark matrix sequentially.
It does not run the restart tree.

### `mbdyn-tests-public-test-job-nodeps`

This job validates the MBDyn build made without optional dependencies. It runs
only plain `tests/benchmarks` inputs, passes `--enable-gtest no`, and does not
run `sp_gradient_test`, restart tests, or a patched matrix. Its deliberately
small dependency list contains only `mbdyn-build-job-nodeps`.

### `mbdyn-tests-public-test-job-clang`

This job validates the Clang build. It runs `sp_gradient_test`, prepares
generators for both public input trees, and launches restart and original
benchmark phases concurrently. It has no patched phase. The two Python
processes share 32 global solver slots and one resource-lock directory, so the
combined cap is 32.

## Private jobs

The private family discovers accepted inputs recursively under `tests`. Its
plain phase uses `--abort-after-step 25`: for ordinary MBDyn inputs the runner
creates a temporary copy that inserts `abort after: regular step, 25;` before
the end of the initial-value section. This is a shortened structural/execution
check, not an unmodified full simulation.

The private patched matrix overrides these fields:

| Field | Values |
|---|---|
| linear solver | `umfpack` |
| matrix handler | `map cc grad` |
| scale method | `rowmaxcolumnmax` |
| scale timing | `always` |
| nonlinear solver | `newtonraphson linesearch nox` |
| integration method | `impliciteuler` |
| initial assembly of deformable/force elements | `exclude` |

Autodiff, output, abort, skip-joint-assembly, and any other non-overridden
fields retain their INI defaults. Compatibility rules reduce the selected
cross-product to 20 configurations. Patched tests use
`--skip-expected-failures yes`, so only inputs explicitly marked with expected
status zero are run, and classify matching solver errors as suppressed via
`--suppressed-errors solver`.

### `mbdyn-tests-private-test-job`

The optimized job runs `sp_gradient_test`, prepares generators once, then runs
the abort-after-25 and 20-configuration patched phases concurrently. Both
runner processes request 32 consumers but share one pool of 32 global solver
slots and one port-lock directory. Phase statuses are bitwise-ORed.

### `mbdyn-tests-private-test-job-gcov`

The conditional gcov job runs `sp_gradient_test`, abort-after-25 tests, and the
same patched matrix sequentially. It depends on public gcov results so coverage
counter files accumulate along the job chain, and retains the gcov build tree
for the next coverage consumer.

### `mbdyn-tests-private-test-job-debug`

The conditional debug job uses the assertion/debug build, appends
`--debug none:stop`, and runs `sp_gradient_test`, abort-after-25 tests, and the
same patched matrix sequentially.

## Module jobs

The module family discovers inputs recursively under `modules`. It prepends the
installed MBDyn `libexec` directory to `LD_LIBRARY_PATH`, because those tests
load the modules produced by the build. Some module tests also require Gmsh and
the installed Octave packages.

The optimized module patched matrix overrides:

| Field | Values |
|---|---|
| linear solver | `umfpack` |
| nonlinear solver | `newtonraphson linesearch linesearch-modified mcpnewtonminfb mcpnewtonfb bfgs` |
| scale method | `rowmaxcolumnmax` |
| scale timing | `always` |
| autodiff | `autodiff` |
| abort point | `input assembly derivatives` |
| initial assembly of deformable/force elements | `exclude` |

The remaining fields use INI defaults. The current compatibility rules produce
24 configurations. Expected failures are skipped and solver errors may be
suppressed. Configuration summaries regard `fixed-failure` as successful: a
test unexpectedly starting to pass does not mark the entire configuration
failed.

### `mbdyn-modules-test-job`

The optimized job lists installed modules in its log, runs `sp_gradient_test`,
prepares generators once, and then concurrently runs:

- a plain phase with `--abort-after-step 100`;
- the 24-configuration patched phase above.

The two processes share 32 solver slots and a resource-lock directory, so their
aggregate solver concurrency is 32.

### `mbdyn-modules-test-job-gcov`

The conditional gcov job runs `sp_gradient_test` and only the plain
abort-after-100 module phase. It then runs the Octave package tests
sequentially after the module runner has completed. There is no patched module
matrix in this job. It receives prior private gcov artifacts and retains the
updated gcov build directory for the coverage report job.

### `mbdyn-modules-test-job-debug`

The conditional debug job appends `--debug none:stop`, runs
`sp_gradient_test`, and runs only the plain abort-after-100 module phase.

## Octave and coverage-report jobs

`octave-pkg-test-job` tests the installed package files selected by
`OCT_PKG_LIST`. Its parallelism model is different from `simple_testsuite.py`;
see [Octave package tests](octave-package-tests.md).

`mbdyn-testsuite-report-job-coverage` does not run simulations. When coverage
is enabled, it consumes the gcov build tree after the public, private, and
module coverage chain, runs `gcovr` with 32 report workers, emits a detailed
HTML report and Cobertura XML, and splits the latter by package to remain under
GitLab artifact-size limits.

## Phase concurrency at a glance

The distinction between “consumer count” and “aggregate solver cap” matters:

| Job | Concurrent shell phases | Consumers per phase | Shared global slots | Maximum solver/run-script subprocesses |
|---|---:|---:|---:|---:|
| optimized public | 3 | 32 | 32 | 32 total |
| optimized private | 2 | 32 | 32 | 32 total |
| optimized modules | 2 | 32 | 32 | 32 total |
| public Clang | 2 | 32 | 32 | 32 total |
| gcov/debug/nodeps simple-suite jobs | 1 at a time | 32 | disabled | 32 per active runner |
| standalone Octave package job | 1 | 32 Octave children | not used | 32 top-level test files; nested MBDyn behavior is package-controlled |

Port locks can lower observed concurrency when queued tests use the same port.
Short setup/postprocessing intervals, unequal task duration, skipped tasks, and
the natural tail of a phase can also make the observed number of `mbdyn`
processes lower than the configured cap.

## Logs, timing, JUnit, and artifacts

All primary public/private/module jobs always upload their consolidated log,
split `mbdyn-testsuite-timing-*.tsv` files, retained `.stdout` files, and JUnit
XML. JUnit paths are rewritten relative to `CI_PROJECT_DIR` before upload. The
gcov variants additionally upload the instrumented build directory. Artifact
retention is currently 24 hours for public and coverage-report jobs and two
days for private, module, and Octave package jobs.

The optimized concurrent jobs first write separate phase logs and status files
under an internal `.phases` directory, then concatenate logs and combine exit
codes. The plain-phase log is piped through `simple_testsuite_output_filter.awk`
to retain report blocks without flooding GitLab's job-log limit. Patched mode
prints aggregate counts rather than every successful test entry for the same
reason.

Timing file semantics, including scheduler-state samples, are documented in
[Timing and diagnostics](simple-testsuite.md#timing-and-diagnostics).
