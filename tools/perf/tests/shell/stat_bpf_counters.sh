#!/bin/bash
# perf stat --bpf-counters test (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

# Get the first allowed CPU
CPU=$(taskset -c -p $$ | awk -F': ' '{print $2}' | awk -F'[,-]' '{print $1}')
if [ -z "$CPU" ]; then
	CPU=0
fi
workload=(taskset -c "$CPU" awk 'BEGIN { for (i=0; i<10000000; i++) sum+=i }')

# check whether $2 is within +/- 15% of $1
compare_number()
{
	first_num=$1
	second_num=$2

	# upper bound is first_num * 115%
	upper=$(expr $first_num + $first_num / 20 \* 3 )
	# lower bound is first_num * 85%
	lower=$(expr $first_num - $first_num / 20 \* 3 )

	if [ $second_num -gt $upper ] || [ $second_num -lt $lower ]; then
		echo "The difference between $first_num and $second_num are greater than 15%."
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
	base_instructions=$(perf stat --no-big-num -e instructions:u -- "${workload[@]}" 2>&1 | \
				awk -v i=0 -v c=0 '/instructions/ { \
					if ($1 != "<not") { i++; c += $1 } \
				} END { if (i > 0) printf "%.0f", c; else print "<not" }')
	bpf_instructions=$(perf stat --no-big-num --bpf-counters -e instructions:u \
				-- "${workload[@]}"  2>&1 | \
				awk -v i=0 -v c=0 '/instructions/ { \
					if ($1 != "<not") { i++; c += $1 } \
				} END { if (i > 0) printf "%.0f", c; else print "<not" }')
	check_counts $base_instructions $bpf_instructions
	compare_number $base_instructions $bpf_instructions
	echo "[Success]"
}

test_bpf_modifier()
{
	printf "Testing bpf event modifier "
	stat_output=$(perf stat --no-big-num \
		-e instructions/name=base_instructions/u,instructions/name=bpf_instructions/bu \
		-- "${workload[@]}" 2>&1)
	base_instructions=$(echo "$stat_output"| \
				awk -v i=0 -v c=0 '/base_instructions/ { \
					if ($1 != "<not") { i++; c += $1 } \
				} END { if (i > 0) printf "%.0f", c; else print "<not" }')
	bpf_instructions=$(echo "$stat_output"| \
				awk -v i=0 -v c=0 '/bpf_instructions/ { \
					if ($1 != "<not") { i++; c += $1 } \
				} END { if (i > 0) printf "%.0f", c; else print "<not" }')
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
