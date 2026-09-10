# `simple_testsuite.py` reference

## Purpose and entry points

`simple_testsuite.py` is the common MBDyn input-file test runner. Invoke it as:

```text
simple_testsuite.py plain OPTIONS
simple_testsuite.py patched OPTIONS
simple_testsuite.py validate-patch-config [--patch-config FILE]
```

`plain` runs each accepted input once. It can also make a simple temporary
abort-after-step patch or consume externally supplied patch include files.
`patched` constructs a compatible configuration matrix and runs every accepted
input once for every configuration. `validate-patch-config` checks the INI
schema without running tests.

The runner owns discovery, generator preparation, task production, consumers,
classification, cleanup, timing, and the final report. Workers never append to
shared reports; they return structured results to the parent process.

## End-to-end flow

One invocation follows this sequence:

1. Parse options and validate positive task/thread counts and incompatible
   option combinations.
2. Create the output directory and select timing, resource-lock, and optional
   global-slot paths.
3. Discover valid input files under `--prefix-input`.
4. In patched mode, load/validate the INI, generate compatible configurations,
   and materialize four include files plus a log for each configuration.
5. Execute each discovered generator script once, unless disabled.
6. Stop here for `--prepare-only`, or print a task manifest and stop for
   `--dry-run` (dry-run does not run generators or consumers).
7. Create `--tasks` worker processes and the task/result queues.
8. Feed runnable tasks while collecting completed results. Skipped tasks are
   completed by the producer and never consume a worker slot.
9. Stop and join consumers after the source is exhausted and all submitted
   work has returned.
10. Classify errors and reference outcomes, clean unwanted artifacts, compute
    configuration summaries, print the suite report, append final timing rows,
    and return the masked report bits.

## Input discovery

Discovery is recursive and has two stages.

First, GNU `find` lists regular files below `--prefix-input`. Every repeated
`--regex-filter-include` becomes an additional `-and -regex` condition and
every repeated `--regex-filter-exclude` becomes `-and -not -regex`. These are
GNU find full-path Emacs regular expressions, not Python regular expressions.
Names matching `*_patched_*.mbd` are always excluded to prevent generated
inputs from becoming new tests.

Second, `mbdyn_input_file_format.awk` accepts files with a complete recognized
MBDyn structure. It accepts either:

- data, initial value, control data, nodes, and elements sections; or
- data, inverse dynamics, control data, nodes, and elements sections.

`--exclude-initial-value` and `--exclude-inverse-dynamics` disable the
corresponding form. The recognizer tracks correctly closed sections; a file
that merely contains matching words is not sufficient.

If `MBD_INPUT_FILES_CACHE` names a nonempty file, its lines are used as the
candidate list instead of running `find`. If the variable is set but no
nonempty cache exists, the accepted candidate list is written there after AWK
recognition. Callers must ensure a cache belongs to the same input tree and
filter set.

For every accepted file, the runner also extracts:

- the last `## @MBDYN_SIMPLE_TESTSUITE_EXIT_STATUS@ = VALUE` marker; absent is
  “unknown” (`-1` internally), `0` means expected success, and any other token
  means expected failure;
- the last `## @MBDYN_SIMPLE_TESTSUITE_EXCLUDE@ = 0|1` marker, where `1` skips
  the test and a later `0` re-enables it;
- literal TCP ports from uncommented lines containing both `socket` and a
  `port, NUMBER` clause;
- one companion script, using this precedence:
  `_run.m`, `_gen.m`, `_run.sh`, `_gen.sh`.

### Companion generators and runners

A `_gen.m` or `_gen.sh` companion is run once before consumers and is expected
to prepare shared input data. Octave generators run through `OCTAVE_EXEC`;
shell generators are made executable and invoked directly. A generator failure
does not create a synthetic final result because the later solver task remains
authoritative.

A `_run.m` or `_run.sh` companion replaces the normal MBDyn command in plain
mode. It receives `-f INPUT -o OUTPUT`. Run scripts are deliberately skipped
for patched tasks and abort-after-step tasks, because those transformations
would not control the custom script's own solver invocation.

CI's concurrent jobs use a `plain --prepare-only` invocation, then pass
`--exec-gen no` to all simultaneously started phases. This prevents two phases
from racing while producing the same generated files.

## Plain and temporary-patch modes

Ordinary plain tasks invoke:

```text
MBdyn command + added arguments + -f INPUT + -o OUTPUT + GoogleTest arguments
```

`--abort-after-step N` creates a temporary input beside the source and inserts
`abort after: regular step, N;` immediately before `end: initial value;`. A
copy is also placed in the output directory for failure diagnostics. This mode
is reported as patched timing even though it does not use the matrix.

`--patch-input yes` is a compatibility mode for one externally defined patch.
It inserts the same four includes used by patched mode, taking their paths from
`MBD_TESTSUITE_INITIAL_VALUE_BEGIN`, `MBD_TESTSUITE_INITIAL_VALUE_END`,
`MBD_TESTSUITE_CONTROL_DATA_BEGIN`, and `MBD_TESTSUITE_CONTROL_DATA_END`, or
from corresponding files in the output directory. It cannot be combined with
`--abort-after-step`.

## Patched configuration generation

### INI structure

The default file is `simple_testsuite_patched.ini`; `--patch-config FILE`
selects another one. It contains:

- `[matrix] fields`: ordered field names. Order defines the directory/key
  layout and generation order.
- `[options]`: one unique long CLI option for each field.
- `[arguments]`: one unique valid Python destination name for each field.
- `[defaults]`: a nonempty whitespace-separated value list for each field.
- any number of `[rule NAME]` sections containing compatibility constraints.

Because CLI options are generated from the INI, adding or renaming a matrix
field does not require a second option-name table in Python. The current patch
renderer itself still requires the semantic fields `linear`, `handler`,
`scale`, `when`, `autodiff`, `nonlinear`, `method`, `output`, `abort`, `skip`,
and `assembly`.

### Current fields and defaults

| Field | CLI option | Current default values |
|---|---|---|
| `linear` | `--linear-solvers` | `naive umfpack klu pardiso pardiso_64 y12 qr lapack siconossparse siconosdense` |
| `handler` | `--matrix-handlers` | `map cc dir grad` |
| `scale` | `--scale-methods` | `rowmaxcolumnmax iterative lapack rowmax columnmax rowsum columnsum` |
| `when` | `--scale-when` | `never always once` |
| `autodiff` | `--autodiff` | `autodiff noautodiff` |
| `nonlinear` | `--nonlinear-solvers` | `newtonraphson linesearch linesearch-modified nox nox-newton-krylov nox-direct nox-broyden2 nox-broyden3 nox-broyden1 mcpnewtonminfb mcpnewtonfb bfgs siconosmcpnewtonminfb siconosmcpnewtonfb` |
| `method` | `--method` | `impliciteuler cranknicolson ms2,0.6 ms3,0.6 ms4,0.6 ss2,0.6 ss3,0.6 ss4,0.6 hope,0.6 Bathe,0.6 msstc3,0.6 msstc4,0.6 msstc5,0.6 mssth3,0.6 mssth4,0.6 mssth5,0.6 DIRK33 DIRK43 DIRK54 hybrid,ms,0.6` |
| `output` | `--output-format` | `netcdf-text` |
| `abort` | `--abort-after` | `input assembly derivatives regularstep,2` |
| `skip` | `--skip-initial-joint-assembly` | `not-skip skip` |
| `assembly` | `--initial-assembly-of-deformable-and-force-elements` | `exclude include` |

Every matrix option takes one shell argument containing a whitespace-separated
list. A command-line list replaces that field's INI default; it is not appended.

### Rule language

A rule is active when every `when.FIELD` value matches and no `unless.FIELD`
value matches. Once active:

- `reject = yes` rejects the branch outright;
- `allow.FIELD = VALUES` requires that field to match one listed value;
- `deny.FIELD = VALUES` rejects matching values;
- entries named `allow_any.GROUP.FIELD` form an OR group: at least one field
  condition in each group must match.

Values normally use exact matching. `prefix:TEXT` matches any value beginning
with `TEXT`. Multiple `when` entries are ANDed, multiple restrictions are all
enforced, and separately named `allow_any` groups are ANDed with one another.

### Current compatibility rules

The following is a prose expansion of the current INI. “Requires” means the
listed value is allowed only under the stated condition; values not named by a
rule continue to the other rules.

| Trigger | Constraint |
|---|---|
| linear is `naive`, `lapack`, `qr`, `siconosdense`, or `siconossparse` | handler must be `map` |
| linear is `y12` | handler must be `map`, `cc`, or `dir` |
| linear is `pardiso`, `pardiso_64`, or `spqr` | handler must be `map` or `grad` |
| scale timing is `never` | scale method must be `rowmaxcolumnmax` |
| linear is `belos`, `amesos`, either Siconos solver, either Pardiso solver, `qr`, `spqr`, or `y12` | scale timing must be `never` |
| handler is `grad` | autodiff must be enabled |
| nonlinear is `mcpnewtonminfb` | autodiff must be enabled |
| nonlinear is `mcpnewtonfb` | Siconos dense/sparse linear solvers are denied |
| nonlinear starts with `siconosmcp` | linear must be `siconosdense` |
| nonlinear is `nox`, `nox-direct`, or starts with `nox-broyden` | linear cannot be `naive`, `qr`, `lapack`, or `siconosdense` |
| nonlinear is `bfgs` | linear must be `spqr` or `qr` |
| initial assembly is included | joint assembly cannot be skipped |
| joint assembly is skipped and abort is not `derivatives` | reject |
| joint assembly is skipped with derivative abort | require `umfpack`, `map`, `rowmaxcolumnmax`, `never`, `newtonraphson`, and `impliciteuler` |
| initial assembly is included and abort is not `assembly` | reject |
| abort is `input` | require `umfpack`, `map`, `rowmaxcolumnmax`, `never`, `newtonraphson`, and `impliciteuler` |
| abort is `assembly` | nonlinear must be `newtonraphson`; method must be `impliciteuler` |
| abort is `derivatives` | method must be `impliciteuler` |
| abort starts with `regularstep,` | for each of nonlinear, linear, handler, scale, and scale timing, require the baseline value (`newtonraphson`, `umfpack`, `map`, `rowmaxcolumnmax`, `never`) unless the method starts with `ms2` |

The INI remains authoritative when this table and the code differ.

### Validation

Run:

```text
testsuite/simple_testsuite.py validate-patch-config
testsuite/simple_testsuite.py validate-patch-config --patch-config FILE
```

Validation catches unreadable files, missing required sections, duplicate or
empty fields, unknown section/setting field names (for example `linea` instead
of `linear`), missing/duplicate/malformed CLI options and Python argument
names, empty defaults/rule value lists, invalid booleans, invalid section
types, and rules that reference no fields.

It does not prove that a token is accepted by MBDyn, that the compatibility
rules are complete, or that a resulting matrix is nonempty. Those are semantic
properties requiring a dry run or CI execution.

### Early-pruned generation

Rules are compiled once after loading. Each rule is indexed under the last
field, in matrix order, that it needs to inspect. The recursive generator
selects one field at a time and evaluates only predicates that have just become
fully decidable. An incompatible partial branch is discarded before its later
dimensions are expanded; the implementation does not build the full Cartesian
product and filter it afterward.

For every accepted tuple, the slash-separated value sequence is both its
configuration key and its nested output path. With the current default INI,
there are 6,062 compatible tuples. Configuration files are materialized once,
before workers start:

- `mbd_init_val_begin.set` (currently a comment placeholder);
- `mbd_init_val_end.set` (linear solver, scaling, abort point, nonlinear
  solver, tolerances, method, and disabled internal threads);
- `mbd_control_data_begin.set` (`print: all, to file;`);
- `mbd_control_data_end.set` (autodiff, initial assembly, max iterations,
  joint assembly, and output format);
- `mbdyn-testsuite-patched.log` describing the configuration.

The renderer expands selected nonlinear aliases into detailed MBDyn solver
clauses. These include the NOX variants, standard/heavy/modified line-search
variants, and BFGS; unrecognized values are emitted verbatim. It also adds
linear-solver-specific clauses: `naive` adds `colamd`; `umfpack` limits maximum
iterations to 10; Pardiso variants set a pivot factor and 100 iterations; and
`amesos`/`belos` add tolerance, iteration, KLU preconditioner, and verbosity
settings. Thus matrix keys contain compact names while include files contain
the actual MBDyn syntax.

### Per-task input transformation

The worker transforms each source input in process, preserving the historical
`mbdyn_testsuite_patch.sed` behavior. It removes/replaces selected method,
tolerance, solver, and threading statements and inserts environment-expanded
include directives at the initial-value and control-data boundaries. It writes
a temporary source-side input for MBDyn and a diagnostic copy under the
configuration output directory.

Each patched task also receives a unique temporary directory inside its
configuration directory through `TMPDIR`. This isolates fixed-name temporary
files and permits multiple inputs and configurations to run concurrently.
Temporary inputs and task directories are removed in the worker's `finally`
block; retained diagnostic copies follow the output-retention policy.

## Producer/consumer scheduling

### Workers and queues

`--tasks N` creates exactly N Python worker processes. The task queue has a
capacity of `max(2, 2*N)`, but the parent deliberately maintains at most N
submitted, incomplete runnable tasks. Whenever one result arrives, the parent
submits the next runnable task. Producer-classified skips do not enter the
queue and therefore do not consume the pending limit.

There is no per-configuration worker pool and no “configuration jobs” layer.
`--configuration-jobs` remains accepted only as a deprecated compatibility
option and has no scheduling effect.

In plain mode the source is one task per input. In patched mode, if inputs are
`I[0..n-1]` and configurations are `C[0..m-1]`, round `r` produces:

```text
(I[i], C[(r + i) mod m]) for every input i
```

for every `r` from `0` through `m-1`. This diagonal ordering covers each
input/configuration pair exactly once while spreading adjacent initial tasks
across configurations. The producer does not wait for all permutations of one
file, nor for one configuration, before moving to other files/configurations.

For illustration, if a tree has 13 inputs and 6,062 configurations, the first
32 pending tasks cover all 13 input files and 15 nearby configurations. They do
not cover 32 completely unrelated configurations, but they also do not consist
of 32 permutations of one input. This detail affects task-duration mixing, not
matrix completeness.

Workers report scheduler states as `idle`, `setup`, `resource-wait`, `running`,
or `postprocess`. A normal task performs patch/setup, waits for all applicable
locks, runs one solver or companion script, releases locks, parses diagnostics,
then sends one result. Worker-completion acknowledgements are sent through the
same result queue after all earlier results, preventing the parent from losing
buffered results at shutdown. Unexpected consumer death becomes an unexpected
suite result.

All results remain in parent memory until the run ends. The parent appends each
timing row as its result arrives, but final reference classification, output
cleanup, configuration summaries, report-bit construction, and report printing
are serial parent work after consumers stop. Successful JUnit status checks and
the historical sed transformation are implemented in Python to avoid one
external process per successful task; AWK error classification is started only
for failed results. Solver-output cleanup groups prefixes by directory, and
patched configuration summaries index results once, avoiding former
quadratic-style rescans of all outputs/results.

### Concurrency limits

Within one invocation, simultaneous solver/run-script processes cannot exceed
`--tasks`. When no global-slot environment is configured, that is the only
general solver cap.

CI sometimes starts several invocations concurrently. Setting both
`MBD_TESTSUITE_GLOBAL_SLOT_DIR` and a positive `MBD_TESTSUITE_GLOBAL_SLOTS`
creates a shared cross-process cap. Before running, each worker cycles over
`solver-slot-N.lock` files using nonblocking `flock`; if all are occupied it
waits 2 ms and tries again. The lock is held only for the solver subprocess and
is released before postprocessing. The kernel releases it automatically if a
worker exits or is killed.

For example, the optimized public job starts three 32-consumer runners but
gives them one shared pool of 32 slots, so at most 32 solver/run-script
subprocesses run across all three phases.

## Resource locking

### Literal TCP-port locks

Every discovered literal port produces a lock name
`tcp-port-PORT.lock`. Before acquiring a global solver slot, a worker obtains
exclusive blocking `flock` locks for all ports used by its input. Ports are
stored in sorted order, which gives a consistent lock order when one input uses
several ports. The locks are held for the subprocess and then released.

The default resource-lock directory is `.resource-locks` below that
invocation's output directory. Concurrent CI phases explicitly set the same
`MBD_TESTSUITE_RESOURCE_LOCK_DIR`, extending protection across plain,
restarted, and patched processes. This mechanism works identically for inputs
under `tests` and `modules`; directory location does not matter.

Only literal uncommented `port, NUMBER` syntax on a line also mentioning
`socket` is recognized. Computed ports, variables, helper scripts that open
sockets invisibly, Unix-domain socket paths, or ports formatted differently
are not automatically locked. Such resources need an explicit convention or
an extension to discovery.

Port locks and global slots are independent. A worker first holds all port
locks and then waits for a solver slot. Its TSV `resource_wait_ms` includes
both waits.

## Command construction and environment

Normal inputs run the tokenized `--mbdyn-exec`, then tokenized
`--mbdyn-args-add`, `-f INPUT`, `-o OUTPUT`, and optional GoogleTest arguments.
Commands such as `valgrind mbdyn` are therefore supported. The subprocess
working directory is the source input's parent directory so relative includes
and data files preserve historical behavior.

The runner exports or adjusts:

- `MBDYN_EXEC`, `OCTAVE_EXEC` (default `octave`), and `PYTHON_EXEC` (default
  `python3`);
- `GTEST_MBDYN_ARGS`, `GTEST_OCTAVE_ARGS`, and
  `MBOCT_MBDYN_PKG_MBDYN_SOLVER_COMMAND`;
- `PYTHONPATH`, adding `libraries/libmbc` for custom Python runners;
- `TMPDIR`, using the unique per-task directory for patched tasks or the output
  directory otherwise;
- the four patch-include variables for patched tasks;
- `OMP_NUM_THREADS=1`, `MKL_NUM_THREADS=1`, and `OPENBLAS_NUM_THREADS=1` to
  prevent nested BLAS oversubscription;
- `MBD_NUM_THREADS=--threads`.

`--timeout` accepts `unlimited` or a nonnegative number with optional `s`, `m`,
or `h`. A suffix-less value is seconds. A timeout terminates the subprocess
through Python's timeout handling and records return code 124.

`--print-resources time|all` wraps each subprocess with `TESTSUITE_TIME_CMD`
(default `/usr/bin/time --verbose`), stores its output per task, and prints it
from the parent. In the current implementation `time` and `all` take the same
runner path.

## Result and reference classification

The immediate subprocess outcome is `passed`, `timeout`, a signal-related
status, or `failed`. For successful runs the in-process permissive parser scans
the JUnit/text report using the historical AWK patterns; it records whether the
file is valid for cleanup and extracts `End of simulation ... after N steps`
from stdout for a `passed{Steps=N}` detail. Missing or unrecognized JUnit does
not by itself turn a zero solver return code into failure.

A failed log is then checked by `parse_mbdyn_error_message.awk`. Recognized
optional-module/loadable-module errors become `module`; errors matching
`--suppressed-errors` become `suppressed:DETAIL`; otherwise they remain
`failed`. Worker exceptions and abnormal process termination become
`unexpected` for reporting.

When `--use-reference-test-status yes`, actual success/failure is compared with
the source marker:

- expected failure that still fails: `known-failure-*`;
- expected success that fails: `regression-*`;
- expected failure that passes: `fixed-failure`;
- expected success that passes: `passed`.

`fixed-failure` has its own report bit, but it does not make a patched
configuration summary fail. Configuration failure excludes `passed`,
`skipped`, `known-failure`, and `fixed-failure`.

`--update-reference-test-status` can append or replace source markers for
selected outcomes. This option modifies source inputs and should normally be
used deliberately outside routine CI. Suppressed errors are not written as
reference updates. Enabling updates also adds `feature` to suppressed-error
matching for legacy parity.

### Final report bits

The process builds this report mask and finally clears bits selected by
`--exit-status-mask`/`--exec-status-mask`:

| Category | Bit |
|---|---:|
| no passed tests | `0x001` |
| timeout | `0x002` |
| module | `0x004` |
| suppressed | `0x008` |
| failed | `0x010` |
| regression | `0x020` |
| unexpected/interrupted/terminated/killed | `0x040` |
| fixed failure | `0x080` |
| known failure | `0x100` |
| skipped | `0x200` |

Patched mode always masks the no-passed-tests bit internally. Shell process
exit statuses are limited by the operating system even though the internal
mask retains the higher bits.

## Output layout and cleanup

Plain outputs use `PREFIX_OUTPUT/STEM_mbdyn_output_INDEX` and
`junit_xml_report_STEM_INDEX.xml`. Optimized concurrent CI jobs put the plain
phase below `original/` and restarted phase below `restarted/`. Patched output
uses one nested directory per slash-separated configuration key.

`--keep-output` controls solver logs and generated output families:

| Value | Retention intent |
|---|---|
| `all` | keep every task output |
| `failed` | keep failed and unexpected task outputs |
| `unexpected` | keep only unexpected task outputs |
| `no` | keep no ordinary task outputs |

For efficiency, generated solver files are deleted with one directory scan per
output directory rather than one glob scan per task. Patched configuration
include files and configuration logs are retained for `all`, or for `failed`
when that configuration failed; otherwise they are removed. A failed
configuration also receives `.failed`.

`--keep-output-junit-xml` independently requests JUnit retention:

- `always`: retain all recognized reports;
- `not-passed`: retain recognized reports except passed and timeout outcomes;
- `failed`: retain recognized reports for failed/unexpected outcomes;
- `none`: remove recognized reports unless ordinary output retention kept the
  entire result.

The CI variable `JUNIT_XML_KEEP_ALL_OUTPUT` supplies this option's default.
Artifact rules can only upload files that remain after this cleanup.

## Timing and diagnostics

### Per-suite/task TSV

Timing is enabled by `--timing yes`. Its default is
`MBD_TESTSUITE_TIMING`, or `no` when that environment variable is absent. The
CI job-family YAML sets it to `yes`; users do not need an extra variable in
those jobs. `MBD_TESTSUITE_TIMING_FILE` selects the path, defaulting to
`PREFIX_OUTPUT/mbdyn-testsuite-timing.tsv`.

The columns are:

```text
scope mode target index status patch_ms resource_wait_ms run_ms total_ms configuration
```

`test` rows contain raw worker status and durations. `patch_ms` is input
transformation time, `resource_wait_ms` combines port/global-slot waits,
`run_ms` is subprocess wall time, and `total_ms` includes worker setup and
postprocessing. The final configuration column identifies the exact patch.

Patched mode also emits:

- a `phase ... preparation` row covering discovery, configuration generation
  and materialization, and generator execution;
- one `configuration` row whose interval runs from matrix scheduling start to
  completion of that configuration's final input (a wall interval, not summed
  CPU time);
- a `phase ... configurations` row for the post-preparation matrix interval;
- a final `suite` wall-time row.

Plain mode emits test rows and the final suite row. CI assigns separate timing
files to restarted, original, and patched phases, avoiding concurrent appends
to one file.

### Scheduler TSV

Set `MBD_TESTSUITE_SCHEDULER_TIMING=yes` to sample scheduler state about once a
second. `MBD_TESTSUITE_SCHEDULER_TIMING_FILE` selects the path; otherwise it is
derived by adding `-scheduler` to the normal timing filename stem.

Columns are:

```text
elapsed_ms submitted completed pending idle setup resource_wait running postprocess workers_alive source_exhausted
```

`running` is the closest measure of active solver-equivalent tasks. A low value
with high `resource_wait` points toward port/global-slot contention; high
`setup` or `postprocess` points toward Python/filesystem work; high `idle` while
the source is not exhausted indicates feeding or worker-health trouble; low
activity after `source_exhausted=1` is normally the tail of unequal task
durations.

The public YAML currently enables scheduler timing globally for all public
variants. Private and module YAML enable ordinary timing but not scheduler
sampling unless the variable is added externally.

### Dry-run manifest

`--dry-run` performs discovery and matrix generation but does not materialize
configuration files, run generators, or create consumers. It prints one
`TASK`/`SKIP` row per planned pair and a final manifest summary. Redirect the
potentially large output when inspecting the default public matrix.

## Complete command-line option reference

### Common options

| Option | Default | Meaning |
|---|---|---|
| `--prefix-output DIR` | required | Persistent result/output root. |
| `--prefix-input DIR` | required | Input tree to discover recursively. |
| `--timeout VALUE` | `unlimited` | Per generator/solver timeout; suffix-less values are seconds. |
| `--regex-filter-include REGEX` | none; repeatable | Add a GNU-find full-path inclusion condition. |
| `--regex-filter-exclude REGEX` | none; repeatable | Add a GNU-find full-path exclusion condition. |
| `--exclude-inverse-dynamics INT` | `0` | Nonzero disables inverse-dynamics input recognition. |
| `--exclude-initial-value INT` | `0` | Nonzero disables initial-value input recognition. |
| `--threads N` | `MBD_NUM_THREADS`, else `1` | `MBD_NUM_THREADS` exported to each task; must be positive. |
| `--tasks N` | `MBD_NUM_TASKS`, else physical cores from `lscpu`, else logical CPU count | Number of Python consumers and per-invocation subprocess cap. |
| `--verbose yes|no` | `no` | Print retained per-task logs from the parent. |
| `--keep-output all|failed|unexpected|no` | `unexpected` | Solver-output retention policy. |
| `--keep-output-junit-xml always|not-passed|failed|none` | `JUNIT_XML_KEEP_ALL_OUTPUT`, else `none` | JUnit retention policy. |
| `--patch-input yes|no` | `no` | Apply one externally supplied four-include patch in plain mode. |
| `--abort-after-step N` | unset | Insert a temporary regular-step abort. |
| `--skip-expected-failures yes|no` | `no` | Run only inputs explicitly marked expected status `0`; nonzero or absent markers are skipped. |
| `--update-reference-test-status failed|passed|all|yes|no` | `no` | Modify expected-status markers for selected outcomes (`yes` means all). |
| `--use-reference-test-status yes|no` | `no` | Compare actual failure boolean with the expected marker. |
| `--mbdyn-exec COMMAND` | `MBDYN_EXEC`, else `mbdyn` | Solver command, tokenized with shell-like quoting. |
| `--mbdyn-args-add ARGS` | `MBDYN_ARGS_ADD`, else `-CF` | Extra solver arguments. CI overrides the environment default to `-CGF`. |
| `--exec-gen yes|no` | `yes` | Run discovered generator companions. |
| `--exec-solver yes|no` | `yes` | Run tasks; `no` reports them as skipped after preparation. |
| `--enable-gtest yes|no` | `yes` | Add GoogleTest/JUnit arguments and environment. |
| `--exit-status-mask MASK`, `--exec-status-mask MASK` | `0` | Clear selected final report bits; accepts Python integer syntax such as `0x380`. |
| `--print-resources no|all|time` | `no` | Wrap each task in `TESTSUITE_TIME_CMD` and print resource details. |
| `--timing yes|no` | `MBD_TESTSUITE_TIMING`, else `no` | Write the timing TSV. |
| `--suppressed-errors PATTERN` | empty | AWK error categories/details allowed to become suppressed. |
| `--dry-run` | off | Print the task/skip manifest only. |
| `--prepare-only` | off | Discover and run generators, then stop before workers. |

### Patched-only options

`--patch-config` and all matrix-field options are described above. In addition,
`--configuration-jobs N` is parsed for old callers, prints a deprecation
warning when supplied, and is ignored. `--tasks` is the sole Python consumer
count.

## Environment variable reference

| Variable | Purpose |
|---|---|
| `MBD_NUM_TASKS` | Default `--tasks`. |
| `MBD_NUM_THREADS` | Default `--threads`; also exported to tasks. |
| `MBDYN_EXEC`, `MBDYN_ARGS_ADD` | Default solver command and arguments. |
| `OCTAVE_EXEC`, `PYTHON_EXEC` | Companion-script/interpreter environment. |
| `JUNIT_XML_KEEP_ALL_OUTPUT` | Default JUnit retention. |
| `TESTSUITE_TIME_CMD` | Per-task profiler command. |
| `MBD_INPUT_FILES_CACHE` | Optional discovery candidate/accepted-file cache. |
| `MBD_TESTSUITE_TIMING` | Default timing switch. |
| `MBD_TESTSUITE_TIMING_FILE` | Timing TSV path. |
| `MBD_TESTSUITE_SCHEDULER_TIMING` | Enable scheduler-state TSV. |
| `MBD_TESTSUITE_SCHEDULER_TIMING_FILE` | Scheduler TSV path. |
| `MBD_TESTSUITE_RESOURCE_LOCK_DIR` | Shared literal-port lock directory. |
| `MBD_TESTSUITE_GLOBAL_SLOT_DIR` | Shared global solver-slot directory. |
| `MBD_TESTSUITE_GLOBAL_SLOTS` | Number of cross-invocation solver slots; `0` disables. |
| four `MBD_TESTSUITE_{INITIAL_VALUE,CONTROL_DATA}_{BEGIN,END}` variables | Include paths for patched or external-patch inputs. |

## Safe maintenance checks

Changes to the runner or INI can be checked without running the real suite:

```text
python3 -m py_compile testsuite/simple_testsuite.py
testsuite/simple_testsuite.py validate-patch-config
python3 testsuite/simple_testsuite_parity.py
```

The parity fixture uses synthetic inputs and retained legacy transformations;
it does not replace side-by-side desktop CI validation of real public, private,
module, restart, debug, gcov, Clang, and no-dependency artifacts.
