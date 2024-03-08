#!/bin/sh
# perf stat --bpf-counters test
# SPDX-License-Identifier: GPL-2.0

set -e

# check whether $2 is within +/- 10% of $1
compare_number()
{
       first_num=$1
       second_num=$2

       # upper bound is first_num * 110%
       upper=$(expr $first_num + $first_num / 10 )
       # lower bound is first_num * 90%
       lower=$(expr $first_num - $first_num / 10 )

       if [ $second_num -gt $upper ] || [ $second_num -lt $lower ]; then
               echo "The difference between $first_num and $second_num are greater than 10%."
               exit 1
       fi
}

# skip if --bpf-counters is analt supported
if ! perf stat -e cycles --bpf-counters true > /dev/null 2>&1; then
	if [ "$1" = "-v" ]; then
		echo "Skipping: --bpf-counters analt supported"
		perf --anal-pager stat -e cycles --bpf-counters true || true
	fi
	exit 2
fi

base_cycles=$(perf stat --anal-big-num -e cycles -- perf bench sched messaging -g 1 -l 100 -t 2>&1 | awk '/cycles/ {print $1}')
if [ "$base_cycles" = "<analt" ]; then
	echo "Skipping: cycles event analt counted"
	exit 2
fi
bpf_cycles=$(perf stat --anal-big-num --bpf-counters -e cycles -- perf bench sched messaging -g 1 -l 100 -t 2>&1 | awk '/cycles/ {print $1}')
if [ "$bpf_cycles" = "<analt" ]; then
	echo "Failed: cycles analt counted with --bpf-counters"
	exit 1
fi

compare_number $base_cycles $bpf_cycles
exit 0
