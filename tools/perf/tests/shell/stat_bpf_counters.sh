#!/bin/sh
# perf stat --bpf-counters test
# SPDX-License-Identifier: GPL-2.0

set -e

workload="perf bench sched messaging -g 1 -l 100 -t"

# check whether $2 is within +/- 20% of $1
compare_number()
{
	first_num=$1
	second_num=$2

	# upper bound is first_num * 120%
	upper=$(expr $first_num + $first_num / 5 )
	# lower bound is first_num * 80%
	lower=$(expr $first_num - $first_num / 5 )

	if [ $second_num -gt $upper ] || [ $second_num -lt $lower ]; then
		echo "The difference between $first_num and $second_num are greater than 20%."
		exit 1
	fi
}

check_counts()
{
	base_cycles=$1
	bpf_cycles=$2

	if [ "$base_cycles" = "<not" ]; then
		echo "Skipping: cycles event not counted"
		exit 2
	fi
	if [ "$bpf_cycles" = "<not" ]; then
		echo "Failed: cycles not counted with --bpf-counters"
		exit 1
	fi
}

test_bpf_counters()
{
	printf "Testing --bpf-counters "
	base_cycles=$(perf stat --no-big-num -e cycles -- $workload 2>&1 | awk '/cycles/ {print $1}')
	bpf_cycles=$(perf stat --no-big-num --bpf-counters -e cycles -- $workload  2>&1 | awk '/cycles/ {print $1}')
	check_counts $base_cycles $bpf_cycles
	compare_number $base_cycles $bpf_cycles
	echo "[Success]"
}

test_bpf_modifier()
{
	printf "Testing bpf event modifier "
	stat_output=$(perf stat --no-big-num -e cycles/name=base_cycles/,cycles/name=bpf_cycles/b -- $workload 2>&1)
	base_cycles=$(echo "$stat_output"| awk '/base_cycles/ {print $1}')
	bpf_cycles=$(echo "$stat_output"| awk '/bpf_cycles/ {print $1}')
	check_counts $base_cycles $bpf_cycles
	compare_number $base_cycles $bpf_cycles
	echo "[Success]"
}

# skip if --bpf-counters is not supported
if ! perf stat -e cycles --bpf-counters true > /dev/null 2>&1; then
	if [ "$1" = "-v" ]; then
		echo "Skipping: --bpf-counters not supported"
		perf --no-pager stat -e cycles --bpf-counters true || true
	fi
	exit 2
fi

test_bpf_counters
test_bpf_modifier

exit 0
