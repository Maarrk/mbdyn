#!/bin/bash

# Remove only test-suite tmpfs directories that belong to this CI project.

set -o pipefail

program_name="$0"
tmpfs_root="${MBD_TESTSUITE_TMPFS_ROOT:-/dev/shm}"
project_id="${CI_PROJECT_ID:-local}"
job_id="${CI_JOB_ID:-}"
stale_age_minutes="${MBD_TESTSUITE_TMPFS_STALE_AGE_MINUTES:-1440}"
cleanup_mode=""

function usage()
{
    printf 'Usage: %s {--stale|--current}\n' "${program_name}" >&2
}

function remove_directory()
{
    local directory="$1"

    case "${directory}" in
        "${tmpfs_root}"/mbdyn-testsuite-"${project_id}"-*)
            printf 'Removing test-suite tmpfs directory "%s"\n' "${directory}"
            rm -rf -- "${directory}"
            ;;
        *)
            printf '%s: refusing to remove unexpected directory "%s"\n' "${program_name}" "${directory}" >&2
            return 1
            ;;
    esac
}

case "$1" in
    --stale|--current)
        cleanup_mode="$1"
        ;;
    *)
        usage
        exit 1
        ;;
esac

if ! test -d "${tmpfs_root}"; then
    printf '%s: tmpfs root "%s" does not exist\n' "${program_name}" "${tmpfs_root}" >&2
    exit 1
fi
tmpfs_root=$(realpath "${tmpfs_root}")

case "${cleanup_mode}" in
    --stale)
        if ! [[ "${stale_age_minutes}" =~ ^[1-9][0-9]*$ ]]; then
            printf '%s: invalid stale age "%s"\n' "${program_name}" "${stale_age_minutes}" >&2
            exit 1
        fi
        while IFS= read -r -d '' directory; do
            remove_directory "${directory}"
        done < <(find "${tmpfs_root}" -maxdepth 1 -mindepth 1 -type d -name "mbdyn-testsuite-${project_id}-*" -mmin "+${stale_age_minutes}" -print0)
        ;;
    --current)
        if test -z "${job_id}"; then
            printf '%s: CI_JOB_ID is required for --current\n' "${program_name}" >&2
            exit 1
        fi
        while IFS= read -r -d '' directory; do
            remove_directory "${directory}"
        done < <(find "${tmpfs_root}" -maxdepth 1 -mindepth 1 -type d -name "mbdyn-testsuite-${project_id}-${job_id}.*" -print0)
        ;;
esac
