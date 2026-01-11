#!/bin/bash
# test addr2line inline unwinding
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
test_dir=$(mktemp -d /tmp/perf-test-inline-addr2line.XXXXXXXXXX)
perf_data="${test_dir}/perf.data"
perf_script_txt="${test_dir}/perf_script.txt"

cleanup() {
    rm -rf "${test_dir}"
    trap - EXIT TERM INT
}

trap_cleanup() {
    echo "Unexpected signal in ${FUNCNAME[1]}"
    cleanup
    exit 1
}
trap trap_cleanup EXIT TERM INT

test_inlinedloop() {
    echo "Inline unwinding verification test"
    # Record data. Currently only dwarf callchains support inlined functions.
    perf record --call-graph dwarf -e task-clock:u -o "${perf_data}" -- perf test -w inlineloop 1

    # Check output with inline (default) and srcline
    perf script -i "${perf_data}" --fields +srcline > "${perf_script_txt}"

    # Expect the leaf and middle functions to occur on lines in the 20s, with
    # the non-inlined parent function on a line in the 30s.
    if grep -q "inlineloop.c:2. (inlined)" "${perf_script_txt}" &&
       grep -q "inlineloop.c:3.$" "${perf_script_txt}"
    then
        echo "Inline unwinding verification test [Success]"
    else
        echo "Inline unwinding verification test [Failed missing inlined functions]"
        err=1
    fi
}

test_inlinedloop

cleanup
exit $err
