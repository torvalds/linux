#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Basc test for cpuset v1 interfaces write/read
#

skip_test() {
	echo "$1"
	echo "Test SKIPPED"
	exit 4 # ksft_skip
}

write_test() {
	dir=$1
	interface=$2
	value=$3
	original=$(cat $dir/$interface)
	echo "testing $interface $value"
	echo $value > $dir/$interface
	new=$(cat $dir/$interface)
	[[ $value -ne $(cat $dir/$interface) ]] && {
		echo "$interface write $value failed: new:$new"
		exit 1
	}
}

[[ $(id -u) -eq 0 ]] || skip_test "Test must be run as root!"

# Find cpuset v1 mount point
CPUSET=$(mount -t cgroup | grep cpuset | head -1 | awk '{print $3}')
[[ -n "$CPUSET" ]] || skip_test "cpuset v1 mount point not found!"

#
# Create a test cpuset, read write test
#
TDIR=test$$
[[ -d $CPUSET/$TDIR ]] || mkdir $CPUSET/$TDIR

ITF_MATRIX=(
	#interface			value		expect 	root_only
	'cpuset.cpus			0-1		0-1	0'
	'cpuset.mem_exclusive		1		1	0'
	'cpuset.mem_exclusive		0		0	0'
	'cpuset.mem_hardwall		1		1	0'
	'cpuset.mem_hardwall		0		0	0'
	'cpuset.memory_migrate		1		1	0'
	'cpuset.memory_migrate		0		0	0'
	'cpuset.memory_spread_page	1		1	0'
	'cpuset.memory_spread_page	0		0	0'
	'cpuset.memory_spread_slab	1		1	0'
	'cpuset.memory_spread_slab	0		0	0'
	'cpuset.mems			0		0	0'
	'cpuset.sched_load_balance	1		1	0'
	'cpuset.sched_load_balance	0		0	0'
	'cpuset.sched_relax_domain_level	2	2	0'
	'cpuset.memory_pressure_enabled	1		1	1'
	'cpuset.memory_pressure_enabled	0		0	1'
)

run_test()
{
	cnt="${ITF_MATRIX[@]}"
	for i in "${ITF_MATRIX[@]}" ; do
		args=($i)
		root_only=${args[3]}
		[[ $root_only -eq 1 ]] && {
			write_test "$CPUSET" "${args[0]}" "${args[1]}" "${args[2]}"
			continue
		}
		write_test "$CPUSET/$TDIR" "${args[0]}" "${args[1]}" "${args[2]}"
	done
}

run_test
rmdir $CPUSET/$TDIR
echo "Test PASSED"
exit 0
