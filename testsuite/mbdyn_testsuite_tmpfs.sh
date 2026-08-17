#!/bin/bash

# This file is sourced by the test-suite drivers after --prefix-output has
# been resolved.  With MBD_TESTSUITE_TMPFS_ROOT set, retained test output is
# staged on tmpfs and copied to the original persistent output directory when
# the outer driver exits.

function mbdyn_testsuite_tmpfs_cleanup()
{
    local rc=$?
    local source_file

    trap - EXIT HUP INT QUIT TERM
    if test -d "${MBD_TESTSUITE_TMPFS_OUTPUT}"; then
        if ! mkdir -p "${MBD_TESTSUITE_PERSISTENT_OUTPUT}"; then
            printf '%s: cannot create persistent output directory "%s"\n' "${program_name}" "${MBD_TESTSUITE_PERSISTENT_OUTPUT}" >&2
            test "${rc}" -ne 0 || rc=1
        else
            shopt -s dotglob nullglob
            for source_file in "${MBD_TESTSUITE_TMPFS_OUTPUT}"/*; do
                test "${source_file##*/}" = "mbdyn-testsuite-timing.tsv" && continue
                if ! cp -a "${source_file}" "${MBD_TESTSUITE_PERSISTENT_OUTPUT}/"; then
                    printf '%s: failed to preserve "%s"\n' "${program_name}" "${source_file}" >&2
                    test "${rc}" -ne 0 || rc=1
                fi
            done
            shopt -u dotglob nullglob

            # A CI job can invoke the suite more than once.  Retain one TSV
            # header and append the records from every temporary invocation.
            # Rewrite temporary output paths so artifact consumers never see
            # directories that were removed by this trap.
            if test -f "${MBD_TESTSUITE_TMPFS_OUTPUT}/mbdyn-testsuite-timing.tsv"; then
                if test -e "${MBD_TESTSUITE_PERSISTENT_OUTPUT}/mbdyn-testsuite-timing.tsv"; then
                    sed '1d' "${MBD_TESTSUITE_TMPFS_OUTPUT}/mbdyn-testsuite-timing.tsv" | sed "s|${MBD_TESTSUITE_TMPFS_OUTPUT}|${MBD_TESTSUITE_PERSISTENT_OUTPUT}|g" >> "${MBD_TESTSUITE_PERSISTENT_OUTPUT}/mbdyn-testsuite-timing.tsv" || rc=1
                elif ! sed "s|${MBD_TESTSUITE_TMPFS_OUTPUT}|${MBD_TESTSUITE_PERSISTENT_OUTPUT}|g" "${MBD_TESTSUITE_TMPFS_OUTPUT}/mbdyn-testsuite-timing.tsv" > "${MBD_TESTSUITE_PERSISTENT_OUTPUT}/mbdyn-testsuite-timing.tsv"; then
                    printf '%s: failed to preserve timing data\n' "${program_name}" >&2
                    test "${rc}" -ne 0 || rc=1
                fi
            fi
        fi
    fi

    case "${MBD_TESTSUITE_TMPFS_OUTPUT}" in
        "${MBD_TESTSUITE_TMPFS_ROOT}"/mbdyn-testsuite-*)
            rm -rf -- "${MBD_TESTSUITE_TMPFS_OUTPUT}"
            ;;
        *)
            printf '%s: refusing to remove unexpected temporary directory "%s"\n' "${program_name}" "${MBD_TESTSUITE_TMPFS_OUTPUT}" >&2
            test "${rc}" -ne 0 || rc=1
            ;;
    esac
    exit "${rc}"
}

function mbdyn_testsuite_tmpfs_signal()
{
    exit "$1"
}

function mbdyn_testsuite_use_tmpfs()
{
    MBD_TESTSUITE_TMPFS_OUTPUT="$1"

    if test -z "${MBD_TESTSUITE_TMPFS_ROOT}" || test "${MBD_TESTSUITE_TMPFS_ACTIVE:-no}" = "yes"; then
        return 0
    fi
    if ! test -d "${MBD_TESTSUITE_TMPFS_ROOT}"; then
        printf '%s: tmpfs root "%s" does not exist\n' "${program_name}" "${MBD_TESTSUITE_TMPFS_ROOT}" >&2
        return 1
    fi

    MBD_TESTSUITE_TMPFS_ROOT=$(realpath "${MBD_TESTSUITE_TMPFS_ROOT}")
    MBD_TESTSUITE_PERSISTENT_OUTPUT=$(realpath -m "${MBD_TESTSUITE_TMPFS_OUTPUT}")
    mbdyn_tmpfs_project_id="${CI_PROJECT_ID:-local}"
    mbdyn_tmpfs_job_id="${CI_JOB_ID:-$$}"
    if ! MBD_TESTSUITE_TMPFS_OUTPUT=$(mktemp -d -p "${MBD_TESTSUITE_TMPFS_ROOT}" "mbdyn-testsuite-${mbdyn_tmpfs_project_id}-${mbdyn_tmpfs_job_id}.XXXXXXXX"); then
        printf '%s: cannot create a temporary directory in "%s"\n' "${program_name}" "${MBD_TESTSUITE_TMPFS_ROOT}" >&2
        return 1
    fi

    export MBD_TESTSUITE_TMPFS_ACTIVE=yes
    printf 'Using temporary test-suite output directory "%s"\n' "${MBD_TESTSUITE_TMPFS_OUTPUT}"
    trap 'mbdyn_testsuite_tmpfs_signal 129' HUP
    trap 'mbdyn_testsuite_tmpfs_signal 130' INT
    trap 'mbdyn_testsuite_tmpfs_signal 131' QUIT
    trap 'mbdyn_testsuite_tmpfs_signal 143' TERM
    trap mbdyn_testsuite_tmpfs_cleanup EXIT
}
