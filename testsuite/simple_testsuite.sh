#!/bin/bash -f
exec "$(dirname "$(realpath "$0")")/simple_testsuite.py" plain "$@"
