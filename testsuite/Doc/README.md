# MBDyn CI test documentation

This directory documents the test-stage jobs and their test runners as they are
currently defined in the repository.

- [CI test jobs](test-jobs.md) is the job-level reference: which source trees
  are tested, which MBDyn build is used, which phases run, and which matrix
  overrides apply.
- [`simple_testsuite.py` reference](simple-testsuite.md) describes discovery,
  generators, patch generation, producer/consumer scheduling, locking,
  execution, status handling, cleanup, timing, environment variables, and all
  command-line options.
- [Octave package tests](octave-package-tests.md) describes the separate Octave
  package scheduler used by `octave-pkg-test-job` and by the coverage module
  job.

The executable source of truth remains:

- the repository-root [`.gitlab-ci.yml`](../../.gitlab-ci.yml) for global CI
  variables and included job files;
- [`mbdyn-tests-public-test-job.yml`](../mbdyn-tests-public-test-job.yml),
  [`mbdyn-tests-private-test-job.yml`](../mbdyn-tests-private-test-job.yml), and
  [`mbdyn-modules-test-job.yml`](../mbdyn-modules-test-job.yml) for MBDyn test
  jobs;
- [`simple_testsuite.py`](../simple_testsuite.py) and
  [`simple_testsuite_patched.ini`](../simple_testsuite_patched.ini) for the
  simple testsuite;
- [`octave-pkg-test-job.yml`](../octave-pkg-test-job.yml),
  [`octave_pkg_testsuite.m`](../octave_pkg_testsuite.m),
  [`octave_pkg_testsuite_exec.m`](../octave_pkg_testsuite_exec.m), and
  [`octave_pkg_testsuite_hook.m`](../octave_pkg_testsuite_hook.m) for package
  testing.

Counts in these documents are derived from the current configuration. They are
not contracts: adding an input file or changing an INI/CI option changes them.
