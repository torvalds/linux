#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Test the special cpuset v1 hotplug case where a cpuset become empty of
# CPUs will force migration of tasks out to an ancestor.
#

skip_test() {
	echo "$1"
	echo "Test SKIPPED"
	exit 4 # ksft_skip
}

[[ $(id -u) -eq 0 ]] || skip_test "Test must be run as root!"

# Find cpuset v1 mount point
CPUSET=$(mount -t cgroup | grep cpuset | head -1 | awk -e '{print $3}')
[[ -n "$CPUSET" ]] || skip_test "cpuset v1 mount point not found!"

#
# Create a test cpuset, put a CPU and a task there and offline that CPU
#
TDIR=test$$
[[ -d $CPUSET/$TDIR ]] || mkdir $CPUSET/$TDIR
echo 1 > $CPUSET/$TDIR/cpuset.cpus
echo 0 > $CPUSET/$TDIR/cpuset.mems
sleep 10&
TASK=$!
echo $TASK > $CPUSET/$TDIR/tasks
NEWCS=$(cat /proc/$TASK/cpuset)
[[ $NEWCS != "/$TDIR" ]] && {
	echo "Unexpected cpuset $NEWCS, test FAILED!"
	exit 1
}

echo 0 > /sys/devices/system/cpu/cpu1/online
sleep 0.5
echo 1 > /sys/devices/system/cpu/cpu1/online
NEWCS=$(cat /proc/$TASK/cpuset)
rmdir $CPUSET/$TDIR
[[ $NEWCS != "/" ]] && {
	echo "cpuset $NEWCS, test FAILED!"
	exit 1
}
echo "Test PASSED"
exit 0
