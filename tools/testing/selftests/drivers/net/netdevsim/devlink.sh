#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="fw_flash_test"
NUM_NETIFS=0
source $lib_dir/lib.sh

BUS_ADDR=10
PORT_COUNT=4
DEV_NAME=netdevsim$BUS_ADDR
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV_NAME/net/
DEBUGFS_DIR=/sys/kernel/debug/netdevsim/$DEV_NAME/
DL_HANDLE=netdevsim/$DEV_NAME

fw_flash_test()
{
	RET=0

	devlink dev flash $DL_HANDLE file dummy
	check_err $? "Failed to flash with status updates on"

	echo "n"> $DEBUGFS_DIR/fw_update_status
	check_err $? "Failed to disable status updates"

	devlink dev flash $DL_HANDLE file dummy
	check_err $? "Failed to flash with status updates off"

	log_test "fw flash test"
}

setup_prepare()
{
	modprobe netdevsim
	echo "$BUS_ADDR $PORT_COUNT" > /sys/bus/netdevsim/new_device
	while [ ! -d $SYSFS_NET_DIR ] ; do :; done
}

cleanup()
{
	pre_cleanup
	echo "$BUS_ADDR" > /sys/bus/netdevsim/del_device
	modprobe -r netdevsim
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS
