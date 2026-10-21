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

PERF_DATA_DIR=$(mktemp -d /tmp/__perf_test.perf.data.dir.XXXXXX)
PERF_DATA="${PERF_DATA_DIR}/perf.data"

cleanup() {
    echo "Cleaning up files..."
    rm -rf ${PERF_DATA_DIR} 2> /dev/null
    for p in ${ALL_PIDS}; do
        rm -f /tmp/jit-${p}.dump /tmp/jitted-${p}-*.so 2> /dev/null
    done

    trap - EXIT TERM INT
}

trap_cleanup() {
    echo "Unexpected termination"
    cleanup
    exit 1
}

trap trap_cleanup EXIT TERM INT

ALL_PIDS=""
NUM=0
for iterations in 1000000 10000000 50000000 100000000; do
    echo "Running with $iterations iterations..."
    rm -f "${PERF_DATA}.pid"
    cat <<EOF | perf record -k 1 -g --call-graph dwarf -o "${PERF_DATA}" -- ${PYTHON} -Xperf_jit
import os
with open("${PERF_DATA}.pid", "w") as f:
    f.write(str(os.getpid()))

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
    baz($iterations)
EOF

    if [ -f "${PERF_DATA}.pid" ]; then
        REAL_PID=$(cat "${PERF_DATA}.pid")
        ALL_PIDS="${ALL_PIDS} ${REAL_PID}"
    fi

    # extract PID of the target process from the data
    PID=$(perf report -i "${PERF_DATA}" --stdio -F pid -q -g none | \
          cut -d: -f1 -s | sort -u | head -n 1 | tr -d ' ')
    if [ -z "${PID}" ]; then
        echo "Failed to get PID, retrying..."
        continue
    fi
    ALL_PIDS="${ALL_PIDS} ${PID}"

    echo "Generate JIT-ed DSOs using perf inject"
    DEBUGINFOD_URLS='' perf inject -i "${PERF_DATA}" -j -o "${PERF_DATA}.jit"

    echo "Add JIT-ed DSOs to the build-ID cache"
    for F in /tmp/jitted-${PID}-*.so; do
        perf buildid-cache -a "${F}"
    done

    echo "Check the symbol containing the function/module name"
    NUM=$(perf report -i "${PERF_DATA}.jit" -s sym --stdio | grep -cE 'py::(foo|bar|baz):<stdin>')

    echo "Remove JIT-ed DSOs from the build-ID cache"
    for F in /tmp/jitted-${PID}-*.so; do
        perf buildid-cache -r "${F}"
    done
    rm -f /tmp/jitted-${PID}-*.so /tmp/jit-${PID}.dump 2>/dev/null

    if [ "${NUM}" -gt 0 ]; then
        echo "Success: found ${NUM} matching lines"
        break
    fi
    echo "No matching lines found, retrying with more iterations..."
done

cleanup

if [ "${NUM}" -eq 0 ]; then
    exit 1
fi
