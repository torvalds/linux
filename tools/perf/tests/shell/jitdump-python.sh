#!/bin/bash
# python profiling with jitdump
# SPDX-License-Identifier: GPL-2.0

SHELLDIR=$(dirname $0)
# shellcheck source=lib/setup_python.sh
. "${SHELLDIR}"/lib/setup_python.sh

OUTPUT=$(${PYTHON} -Xperf_jit -c 'import os, sys; print(os.getpid(), sys.is_stack_trampoline_active())' 2> /dev/null)
PID=${OUTPUT% *}
HAS_PERF_JIT=${OUTPUT#* }

rm -f /tmp/jit-${PID}.dump 2> /dev/null
if [ "${HAS_PERF_JIT}" != "True" ]; then
    echo "SKIP: python JIT dump is not available"
    exit 2
fi

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXXX)

cleanup() {
    echo "Cleaning up files..."
    rm -f ${PERF_DATA} ${PERF_DATA}.jit /tmp/jit-${PID}.dump /tmp/jitted-${PID}-*.so 2> /dev/null

    trap - EXIT TERM INT
}

trap_cleanup() {
    echo "Unexpected termination"
    cleanup
    exit 1
}

trap trap_cleanup EXIT TERM INT

echo "Run python with -Xperf_jit"
cat <<EOF | perf record -k 1 -g --call-graph dwarf -o "${PERF_DATA}" \
		 -- ${PYTHON} -Xperf_jit
def foo(n):
    result = 0
    for _ in range(n):
        result += 1
    return result

def bar(n):
    foo(n)

def baz(n):
    bar(n)

if __name__ == "__main__":
    baz(1000000)
EOF

# extract PID of the target process from the data
_PID=$(perf report -i "${PERF_DATA}" -F pid -q -g none | cut -d: -f1 -s)
PID=$(echo -n $_PID)  # remove newlines

echo "Generate JIT-ed DSOs using perf inject"
DEBUGINFOD_URLS='' perf inject -i "${PERF_DATA}" -j -o "${PERF_DATA}.jit"

echo "Add JIT-ed DSOs to the build-ID cache"
for F in /tmp/jitted-${PID}-*.so; do
  perf buildid-cache -a "${F}"
done

echo "Check the symbol containing the function/module name"
NUM=$(perf report -i "${PERF_DATA}.jit" -s sym | grep -cE 'py::(foo|bar|baz):<stdin>')

echo "Found ${NUM} matching lines"

echo "Remove JIT-ed DSOs from the build-ID cache"
for F in /tmp/jitted-${PID}-*.so; do
  perf buildid-cache -r "${F}"
done

cleanup

if [ "${NUM}" -eq 0 ]; then
    exit 1
fi
