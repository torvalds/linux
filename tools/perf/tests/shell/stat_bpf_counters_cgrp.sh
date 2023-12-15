#!/bin/sh
# perf stat --bpf-counters --for-each-cgroup test
# SPDX-License-Identifier: GPL-2.0

set -e

test_cgroups=
if [ "$1" = "-v" ]; then
	verbose="1"
fi

# skip if --bpf-counters --for-each-cgroup is not supported
check_bpf_counter()
{
	if ! perf stat -a --bpf-counters --for-each-cgroup / true > /dev/null 2>&1; then
		if [ "${verbose}" = "1" ]; then
			echo "Skipping: --bpf-counters --for-each-cgroup not supported"
			perf --no-pager stat -a --bpf-counters --for-each-cgroup / true || true
		fi
		exit 2
	fi
}

# find two cgroups to measure
find_cgroups()
{
	# try usual systemd slices first
	if [ -d /sys/fs/cgroup/system.slice ] && [ -d /sys/fs/cgroup/user.slice ]; then
		test_cgroups="system.slice,user.slice"
		return
	fi

	# try root and self cgroups
	find_cgroups_self_cgrp=$(grep perf_event /proc/self/cgroup | cut -d: -f3)
	if [ -z ${find_cgroups_self_cgrp} ]; then
		# cgroup v2 doesn't specify perf_event
		find_cgroups_self_cgrp=$(grep ^0: /proc/self/cgroup | cut -d: -f3)
	fi

	if [ -z ${find_cgroups_self_cgrp} ]; then
		test_cgroups="/"
	else
		test_cgroups="/,${find_cgroups_self_cgrp}"
	fi
}

# As cgroup events are cpu-wide, we cannot simply compare the result.
# Just check if it runs without failure and has non-zero results.
check_system_wide_counted()
{
	check_system_wide_counted_output=$(perf stat -a --bpf-counters --for-each-cgroup ${test_cgroups} -e cpu-clock -x, sleep 1  2>&1)
	if echo ${check_system_wide_counted_output} | grep -q -F "<not "; then
		echo "Some system-wide events are not counted"
		if [ "${verbose}" = "1" ]; then
			echo ${check_system_wide_counted_output}
		fi
		exit 1
	fi
}

check_cpu_list_counted()
{
	check_cpu_list_counted_output=$(perf stat -C 0,1 --bpf-counters --for-each-cgroup ${test_cgroups} -e cpu-clock -x, taskset -c 1 sleep 1  2>&1)
	if echo ${check_cpu_list_counted_output} | grep -q -F "<not "; then
		echo "Some CPU events are not counted"
		if [ "${verbose}" = "1" ]; then
			echo ${check_cpu_list_counted_output}
		fi
		exit 1
	fi
}

check_bpf_counter
find_cgroups

check_system_wide_counted
check_cpu_list_counted

exit 0
