#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking the psample module. It makes use of netdevsim
# which periodically generates "sampled" packets.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	psample_enable_test
	psample_group_num_test
	psample_md_test
"
NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV/net/
PSAMPLE_DIR=/sys/kernel/debug/netdevsim/$DEV/psample/
CAPTURE_FILE=$(mktemp)
NUM_NETIFS=0
source $lib_dir/lib.sh

DEVLINK_DEV=
source $lib_dir/devlink_lib.sh
DEVLINK_DEV=netdevsim/${DEV}

# Available at https://github.com/Mellanox/libpsample
require_command psample

psample_capture()
{
	rm -f $CAPTURE_FILE

	timeout 2 ip netns exec testns1 psample &> $CAPTURE_FILE
}

psample_enable_test()
{
	RET=0

	echo 1 > $PSAMPLE_DIR/enable
	check_err $? "Failed to enable sampling when should not"

	echo 1 > $PSAMPLE_DIR/enable 2>/dev/null
	check_fail $? "Sampling enablement succeeded when should fail"

	psample_capture
	if [ $(cat $CAPTURE_FILE | wc -l) -eq 0 ]; then
		check_err 1 "Failed to capture sampled packets"
	fi

	echo 0 > $PSAMPLE_DIR/enable
	check_err $? "Failed to disable sampling when should not"

	echo 0 > $PSAMPLE_DIR/enable 2>/dev/null
	check_fail $? "Sampling disablement succeeded when should fail"

	psample_capture
	if [ $(cat $CAPTURE_FILE | wc -l) -ne 0 ]; then
		check_err 1 "Captured sampled packets when should not"
	fi

	log_test "psample enable / disable"
}

psample_group_num_test()
{
	RET=0

	echo 1234 > $PSAMPLE_DIR/group_num
	echo 1 > $PSAMPLE_DIR/enable

	psample_capture
	grep -q -e "group 1234" $CAPTURE_FILE
	check_err $? "Sampled packets reported with wrong group number"

	# New group number should only be used after disable / enable.
	echo 4321 > $PSAMPLE_DIR/group_num

	psample_capture
	grep -q -e "group 4321" $CAPTURE_FILE
	check_fail $? "Group number changed while sampling is active"

	echo 0 > $PSAMPLE_DIR/enable && echo 1 > $PSAMPLE_DIR/enable

	psample_capture
	grep -q -e "group 4321" $CAPTURE_FILE
	check_err $? "Group number did not change after restarting sampling"

	log_test "psample group number"

	echo 0 > $PSAMPLE_DIR/enable
}

psample_md_test()
{
	RET=0

	echo 1 > $PSAMPLE_DIR/enable

	echo 1234 > $PSAMPLE_DIR/in_ifindex
	echo 4321 > $PSAMPLE_DIR/out_ifindex
	psample_capture

	grep -q -e "in-ifindex 1234" $CAPTURE_FILE
	check_err $? "Sampled packets reported with wrong in-ifindex"

	grep -q -e "out-ifindex 4321" $CAPTURE_FILE
	check_err $? "Sampled packets reported with wrong out-ifindex"

	echo 5 > $PSAMPLE_DIR/out_tc
	psample_capture

	grep -q -e "out-tc 5" $CAPTURE_FILE
	check_err $? "Sampled packets reported with wrong out-tc"

	echo $((2**16 - 1)) > $PSAMPLE_DIR/out_tc
	psample_capture

	grep -q -e "out-tc " $CAPTURE_FILE
	check_fail $? "Sampled packets reported with out-tc when should not"

	echo 1 > $PSAMPLE_DIR/out_tc
	echo 10000 > $PSAMPLE_DIR/out_tc_occ_max
	psample_capture

	grep -q -e "out-tc-occ " $CAPTURE_FILE
	check_err $? "Sampled packets not reported with out-tc-occ when should"

	echo 0 > $PSAMPLE_DIR/out_tc_occ_max
	psample_capture

	grep -q -e "out-tc-occ " $CAPTURE_FILE
	check_fail $? "Sampled packets reported with out-tc-occ when should not"

	echo 10000 > $PSAMPLE_DIR/latency_max
	psample_capture

	grep -q -e "latency " $CAPTURE_FILE
	check_err $? "Sampled packets not reported with latency when should"

	echo 0 > $PSAMPLE_DIR/latency_max
	psample_capture

	grep -q -e "latency " $CAPTURE_FILE
	check_fail $? "Sampled packets reported with latency when should not"

	log_test "psample metadata"

	echo 0 > $PSAMPLE_DIR/enable
}

setup_prepare()
{
	modprobe netdevsim &> /dev/null

	echo "$DEV_ADDR 1" > ${NETDEVSIM_PATH}/new_device
	while [ ! -d $SYSFS_NET_DIR ] ; do :; done

	set -e

	ip netns add testns1
	devlink dev reload $DEVLINK_DEV netns testns1

	set +e
}

cleanup()
{
	pre_cleanup
	rm -f $CAPTURE_FILE
	ip netns del testns1
	echo "$DEV_ADDR" > ${NETDEVSIM_PATH}/del_device
	modprobe -r netdevsim &> /dev/null
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS
