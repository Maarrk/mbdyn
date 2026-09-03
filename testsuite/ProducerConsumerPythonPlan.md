# Python producer/consumer test-suite plan

## Decision

This change is feasible and is a good fit for the present bottleneck.

Replace GNU Parallel in `simple_testsuite.sh` and the configuration-level
background-job scheduler in `simple_testsuite_patched.sh` with one Python
orchestrator.  It creates exactly `MBD_NUM_TASKS` long-lived consumer
processes.  A producer discovers inputs, evaluates all conditions that can
make a test skipped, generates compatible patched configurations, and sends
work items to the consumers through a bounded queue.

For patched runs, a work item means:

    (input file, input index, configuration identifier, PatchSpec,
     output directory, execution options)

The consumer applies `PatchSpec` to that one input, runs the test, cleans it
up, and returns a result.  The producer does *not* wait for every
configuration of one input to finish before discovering and enqueueing the
next input.  A bounded queue keeps consumers fed without allowing an
unbounded number of pending configuration/test pairs.

This makes `MBD_NUM_TASKS` unambiguous: it is the number of consumers and
the maximum number of concurrently executing test commands (subject to
resource locks and generator dependencies).  `--configuration-jobs` becomes
obsolete and should be accepted with a deprecation warning for one release,
then removed.

The refactor is substantial but simplifies the scheduling model materially:
there is one queue, one result collector, one concurrency limit, and no
nested calls to GNU Parallel or `wait -n`.


## Current behaviour that must be retained

The new implementation must preserve the externally visible command-line
interfaces of both `simple_testsuite.sh` and `simple_testsuite_patched.sh`.
Initially, keep those executable names as small Bash launchers which run the
Python implementation.  This avoids CI YAML changes and preserves direct
user invocations.

The following behaviour is part of compatibility, not optional cleanup:

* input selection below `--prefix-input`, including regex filters, exclusion
  of `*_patched_*.mbd`, initial-value/inverse-dynamics filtering and the
  structural test currently performed by `mbdyn_input_file_format.awk`;
* expected-status markers and `--use-reference-test-status` /
  `--update-reference-test-status` behaviour;
* `@MBDYN_SIMPLE_TESTSUITE_EXCLUDE@` handling;
* `--skip-expected-failures`;
* all timeout, output-retention, JUnit XML, gtest, verbose-output, MBDyn
  executable/argument, suppressed-error, status-mask and timing options;
* `*_gen.m`, `*_gen.sh`, `*_run.m`, and `*_run.sh` discovery and semantics;
* TCP port resource locking, including acquisition of multiple port locks in
  sorted order to avoid deadlock;
* all existing patched-matrix options and compatibility exclusions;
* result files, test reports, exit status bits, output names and artifacts.

The existing sources to use as the behavioural specification are:

* `simple_testsuite.sh`, especially `simple_testsuite_run_test()`;
* `simple_testsuite_patched.sh`, especially the nested matrix loops and the
  generated `.set` file content;
* `mbdyn_testsuite_patch.sed`;
* `mbdyn_input_file_format.awk` and `mbdyn_testsuite_exclude_test.awk`.


## Proposed design

### 1. Python package and compatibility entry points

Add a small, standard-library-only package, for example:

    testsuite_runner/
      __init__.py
      cli.py
      discovery.py
      matrix.py
      patching.py
      worker.py
      reporting.py
      resources.py

Use Python 3 only; do not add a third-party dependency.  Keep
`simple_testsuite.sh` and `simple_testsuite_patched.sh` as compatibility
front ends, or alternatively make them invoke one `simple_testsuite.py`
entry point with a `plain` or `patched` mode.

Use `argparse` to retain every current option and its spelling.  Parse the
shared options once; patched mode adds the patched-matrix options.

### 2. Immutable data models

Define serialisable dataclasses:

* `SuiteOptions`: shared CLI/environment settings;
* `InputSpec`: absolute input path, stable input index, expected status,
  exclusion state, custom script information, literal socket ports and
  generator dependency information;
* `PatchSpec`: the selected solver/matrix/scaling/nonlinear/method/autodiff/
  abort/output/assembly choices plus its stable configuration id and output
  directory;
* `Task`: either an unpatched `InputSpec` or an `(InputSpec, PatchSpec)`
  pair;
* `TestResult`: status, exit-status bit, output paths and all timing fields.

All paths in these objects must be absolute.  Give configurations and inputs
stable indices so output names and reports remain deterministic regardless of
the order in which consumers finish.

### 3. Discovery and skip decisions in the producer

The producer discovers input files once.  Port the two small awk programs
into Python, or call them once during the transition and cache their output.
The preferred final state is Python parsing, with regression tests comparing
its input list and metadata to the awk implementation.

For every input, calculate before enqueueing any patched task:

* validity as an MBDyn input;
* expected status;
* explicit exclusion marker;
* custom `*_gen` / `*_run` scripts;
* literal TCP ports;
* whether the selected mode allows the test to be patched.

The producer must emit a completed `skipped` result directly, rather than a
consumer task, when the current Bash implementation would skip the test.
This includes explicit exclusions, expected failures when
`--skip-expected-failures yes` is active, and patched runs that use a custom
`*_run` script and are therefore not patchable.  Consequently skipped tests
are neither patched nor sent to a consumer.

This requirement needs one qualification: a custom `*_gen` script is not a
skip.  It has to be handled in the generator stage below because it may
produce inputs used by several later tests.

### 4. Generator stage and custom run scripts

The current runner deliberately executes all `*_gen.m` / `*_gen.sh` scripts
before solver tasks because multiple models can share their generated output.
Preserve this as an explicit, bounded pre-execution stage:

1. discover generator dependencies;
2. run each unique generator once, with the current command, environment,
   timeout and output semantics;
3. record generator failures as dependent test failures/skips as the current
   behaviour dictates;
4. only then start normal consumer work.

Do not parallelise generators in the first implementation.  It is a small
and correctness-sensitive stage.  It can later be parallelised only after
dependencies are explicitly represented and tested.

Consumers must retain the existing `*_run.m` and `*_run.sh` command forms
for unpatched tests.  Such tests are excluded from patched work exactly as
they are today.

### 5. Patch matrix producer

Port the defaults and all compatibility `continue` conditions from
`simple_testsuite_patched.sh` into a pure Python generator yielding
`PatchSpec` objects.  Do not reproduce the current matrix as an unfiltered
Cartesian product: the compatibility rules are required to retain its test
set.

For each `PatchSpec`, create its configuration output directory and four
include files once, containing exactly the current settings:

* `mbd_init_val_begin.set`;
* `mbd_init_val_end.set`;
* `mbd_control_data_begin.set`;
* `mbd_control_data_end.set`.

The content must be regression-tested byte-for-byte against the current
Bash output for representative configurations.

The producer then traverses inputs and configurations without a per-input
barrier.  It should enqueue tasks in a fair order (for example, round-robin
over configurations while walking inputs) so a slow configuration cannot
delay all work belonging to later inputs.  Use a queue capacity of roughly
`2 * MBD_NUM_TASKS` initially; make it configurable only if measurement
justifies that.

### 6. Consumers

Start exactly `MBD_NUM_TASKS` `multiprocessing.Process` consumers.  A
consumer repeatedly receives one `Task`, then:

1. builds the output, log and JUnit file names from the stable identifiers;
2. for a patched task, creates a unique temporary input in the input file's
   directory and applies the patch there;
3. runs MBDyn or the permitted custom command using `subprocess`, preserving
   current environment variables and timeout handling;
4. parses status and diagnostic output;
5. performs the current retention/cleanup rules;
6. returns one `TestResult` to the result queue.

Patch in Python rather than launching `sed` once per task.  The patcher
must implement the four section-boundary insertions and the existing removal
rules.  Retain a temporary compatibility test that compares Python-patched
files with `sed -E -f mbdyn_testsuite_patch.sed` output.

Use `tempfile.mkstemp(dir=input_directory)` and `finally` cleanup for temporary
patched inputs.  Keep the output-directory copy on failure exactly as the
current suite does.

### 7. Generic resource locks

Retain generic TCP-port resource locks in Python with `fcntl.flock` on lock
files under `MBD_TESTSUITE_RESOURCE_LOCK_DIR`.  The producer supplies the
already parsed, sorted port list in `InputSpec`; consumers acquire locks in
ascending numeric order and release them in `finally` blocks.

The locks must cover all work sources, including module inputs, because the
worker implementation is shared.  The lock directory must be shared by all
consumers and all patched configurations of the suite.

### 8. Result collector, reports and timing

Only the producer/result collector writes shared files.  Consumers return
structured results; they do not append concurrently to the timing TSV or
mutate shared summary variables.

The collector must preserve:

* the existing `mbdyn-testsuite-timing.tsv` columns and test/suite records;
* per-configuration timing records (mark a configuration complete when its
  outstanding task count reaches zero, not when it is enqueued);
* JUnit report discovery and relative-path conversion;
* report categories, status bit masks and final exit status;
* expected-status updates, performed only in the single collector process.

This removes race-prone concurrent `>>` writes while retaining CI artifact
format compatibility.

### 9. Shutdown and failures

Use sentinels for normal worker shutdown.  On SIGINT/SIGTERM, stop producing,
terminate and join consumers, then write all completed results and return an
interrupted/terminated status compatible with the current suite.

Workers must report Python exceptions as `unexpected` results with a
traceback in their per-task log.  The producer must detect a dead consumer,
stop the suite and return failure instead of waiting forever.


## Migration sequence

1. Add behavioural regression fixtures: input discovery, expected-status and
   exclusion decisions, custom-script selection, patched text output, patch
   matrix identifiers, status parsing and TCP lock ordering.
2. Implement Python discovery and matrix generation, but add a dry-run mode
   that prints the exact input/configuration/task manifest without executing
   tests.  Compare it with a manifest emitted from the Bash scripts.
3. Implement one Python consumer and run it sequentially (`MBD_NUM_TASKS=1`)
   for plain tests.  Compare statuses, retained files, JUnit XML and timing
   schema with the Bash implementation.
4. Add patched tasks and validate generated include files and patched input
   text against the Bash/sed output.
5. Add TCP locks, custom scripts and generator pre-execution.  Run targeted
   tests, including socket users and module tests.
6. Add the bounded `MBD_NUM_TASKS` consumer pool.  Verify that at most that
   many MBDyn child processes execute concurrently and that no work item is
   lost on worker failure.
7. Run public, private and module CI jobs side-by-side with the old runner;
   compare test counts, result categories, JUnit reports, retained artifacts
   and timing TSV records.
8. Switch the Bash entry points to Python by default, retaining an explicit
   temporary legacy opt-out for one release.  Remove GNU Parallel and the
   configuration-level `wait -n` scheduler after CI parity is established.


## Acceptance criteria

* `MBD_NUM_TASKS=N` starts exactly N consumers and never has more than N
  simultaneously executing test commands.
* A patched input/configuration task can begin as soon as a consumer is free;
  the producer does not wait for the preceding input's complete patch matrix.
* Inputs skipped by the current logic are recorded as skipped without being
  patched or sent to a consumer.
* Public, private and module jobs retain every currently supported option and
  match the old runner's test/status/artifact results.
* The timing TSV remains consumable by existing CI tooling and distinguishes
  queue wait, patch, resource-lock wait, command run and total time.
* Socket tests remain safe when the same input is scheduled under multiple
  configurations.
* No GNU Parallel dependency remains in the execution path.

## Desktop CI parity gate

Before removing the retained legacy scripts, run the synthetic preflight from
this directory:

    python3 simple_testsuite_parity.py

It exercises input discovery, status/reference handling, the native patcher
against the legacy sed program, configuration pruning/include files, custom
run and generator scripts, resource locks, two-consumer concurrency and
producer round-robin order.  It creates only a temporary fixture.

Then run the public, private and module CI jobs on the desktop with the
Python default.  Compare them with a repeat using:

    MBD_TESTSUITE_USE_LEGACY=yes

For each paired run compare selected input count, report-category counts,
JUnit XML files, retained artifacts and timing TSV schema/records.  In the
patched jobs also compare configuration directories and `.failed` markers.
Only remove the legacy escape hatch after those comparisons agree.
