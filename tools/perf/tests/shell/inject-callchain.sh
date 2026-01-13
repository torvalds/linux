#!/bin/bash
# perf inject to convert DWARF callchains to regular ones
# SPDX-License-Identifier: GPL-2.0

if ! perf check feature -q dwarf; then
    echo "SKIP: DWARF support is not available"
    exit 2
fi

TESTDATA=$(mktemp /tmp/perf-test.XXXXXX)

err=0

cleanup()
{
    trap - EXIT TERM INT
    rm -f ${TESTDATA}*
}

trap_cleanup()
{
	cleanup
	exit 1
}

trap trap_cleanup EXIT TERM INT

echo "recording data with DWARF callchain"
perf record -F 999 --call-graph dwarf -o "${TESTDATA}" -- perf test -w noploop

echo "convert DWARF callchain using perf inject"
perf inject -i "${TESTDATA}" --convert-callchain -o "${TESTDATA}.new"

perf report -i "${TESTDATA}" --no-children -q --percent-limit=1 > ${TESTDATA}.out
perf report -i "${TESTDATA}.new" --no-children -q --percent-limit=1 > ${TESTDATA}.new.out

echo "compare the both result excluding inlined functions"
if diff -u "${TESTDATA}.out" "${TESTDATA}.new.out" | grep "^- " | grep -qv "(inlined)"; then
    echo "Found some differences"
    diff -u "${TESTDATA}.out" "${TESTDATA}.new.out"
    err=1
fi

cleanup
exit $err
