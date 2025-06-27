#!/bin/bash
# perf trace summary (exclusive)
# SPDX-License-Identifier: GPL-2.0

# Check that perf trace works with various summary mode

# shellcheck source=lib/probe.sh
. "$(dirname $0)"/lib/probe.sh

skip_if_no_perf_trace || exit 2
[ "$(id -u)" = 0 ] || exit 2

OUTPUT=$(mktemp /tmp/perf_trace_test.XXXXX)

test_perf_trace() {
    args=$1
    workload="true"
    search="^\s*(open|read|close).*[0-9]+%$"

    echo "testing: perf trace ${args} -- ${workload}"
    perf trace ${args} -- ${workload} >${OUTPUT} 2>&1
    if [ $? -ne 0 ]; then
        echo "Error: perf trace ${args} failed unexpectedly"
        cat ${OUTPUT}
        rm -f ${OUTPUT}
        exit 1
    fi

    count=$(grep -E -c -m 3 "${search}" ${OUTPUT})
    if [ "${count}" != "3" ]; then
	echo "Error: cannot find enough pattern ${search} in the output"
	cat ${OUTPUT}
	rm -f ${OUTPUT}
	exit 1
    fi
}

# summary only for a process
test_perf_trace "-s"

# normal output with summary at the end
test_perf_trace "-S"

# summary only with an explicit summary mode
test_perf_trace "-s --summary-mode=thread"

# summary with normal output - total summary mode
test_perf_trace "-S --summary-mode=total"

# summary only for system wide - per-thread summary
test_perf_trace "-as --summary-mode=thread --no-bpf-summary"

# summary only for system wide - total summary mode
test_perf_trace "-as --summary-mode=total --no-bpf-summary"

if ! perf check feature -q bpf; then
    echo "Skip --bpf-summary tests as perf built without libbpf"
    rm -f ${OUTPUT}
    exit 2
fi

# summary only for system wide - per-thread summary with BPF
test_perf_trace "-as --summary-mode=thread --bpf-summary"

# summary only for system wide - total summary mode with BPF
test_perf_trace "-as --summary-mode=total --bpf-summary"

# summary with normal output for system wide - total summary mode with BPF
test_perf_trace "-aS --summary-mode=total --bpf-summary"

# summary only for system wide - cgroup summary mode with BPF
test_perf_trace "-as --summary-mode=cgroup --bpf-summary"

# summary with normal output for system wide - cgroup summary mode with BPF
test_perf_trace "-aS --summary-mode=cgroup --bpf-summary"

rm -f ${OUTPUT}
