#!/bin/sh
# perf stat --bpf-counters test (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

workload="perf test -w sqrtloop"

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
	base_instructions=$1
	bpf_instructions=$2

	if [ "$base_instructions" = "<not" ]; then
		echo "Skipping: instructions event not counted"
		exit 2
	fi
	if [ "$bpf_instructions" = "<not" ]; then
		echo "Failed: instructions not counted with --bpf-counters"
		exit 1
	fi
}

test_bpf_counters()
{
	printf "Testing --bpf-counters "
	base_instructions=$(perf stat --no-big-num -e instructions -- $workload 2>&1 | awk '/instructions/ {print $1}')
	bpf_instructions=$(perf stat --no-big-num --bpf-counters -e instructions -- $workload  2>&1 | awk '/instructions/ {print $1}')
	check_counts $base_instructions $bpf_instructions
	compare_number $base_instructions $bpf_instructions
	echo "[Success]"
}

test_bpf_modifier()
{
	printf "Testing bpf event modifier "
	stat_output=$(perf stat --no-big-num -e instructions/name=base_instructions/,instructions/name=bpf_instructions/b -- $workload 2>&1)
	base_instructions=$(echo "$stat_output"| awk '/base_instructions/ {print $1}')
	bpf_instructions=$(echo "$stat_output"| awk '/bpf_instructions/ {print $1}')
	check_counts $base_instructions $bpf_instructions
	compare_number $base_instructions $bpf_instructions
	echo "[Success]"
}

# skip if --bpf-counters is not supported
if ! perf stat -e instructions --bpf-counters true > /dev/null 2>&1; then
	if [ "$1" = "-v" ]; then
		echo "Skipping: --bpf-counters not supported"
		perf --no-pager stat -e instructions --bpf-counters true || true
	fi
	exit 2
fi

test_bpf_counters
test_bpf_modifier

exit 0
