#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

before=$(grep "^pid " /proc/slabinfo | awk '{print $2}')

nr_leaks=$(./debugfs_target_ids_pid_leak 1000)
expected_after_max=$((before + nr_leaks / 2))

after=$(grep "^pid " /proc/slabinfo | awk '{print $2}')

echo > /sys/kernel/debug/damon/target_ids

echo "tried $nr_leaks pid leak"
echo "number of active pid slabs: $before -> $after"
echo "(up to $expected_after_max expected)"
if [ $after -gt $expected_after_max ]
then
	echo "maybe pids are leaking"
	exit 1
else
	exit 0
fi
