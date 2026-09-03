#!/bin/bash -f

if test "${MBD_TESTSUITE_USE_LEGACY:-no}" = "yes"; then
    exec "$(dirname "$(realpath "$0")")/simple_testsuite_patched_legacy.sh" "$@"
fi
exec "$(dirname "$(realpath "$0")")/simple_testsuite.py" patched "$@"
