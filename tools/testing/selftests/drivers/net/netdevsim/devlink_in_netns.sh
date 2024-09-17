#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="check_devlink_test check_ports_test"
NUM_NETIFS=0
source $lib_dir/lib.sh

BUS_ADDR=10
PORT_COUNT=4
DEV_NAME=netdevsim$BUS_ADDR
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV_NAME/net/
DL_HANDLE=netdevsim/$DEV_NAME
NETNS_NAME=testns1

port_netdev_get()
{
	local port_index=$1

	cmd_jq "devlink -N $NETNS_NAME port show -j" \
	       ".[][\"$DL_HANDLE/$port_index\"].netdev" "-e"
}

check_ports_test()
{
	RET=0

	for i in $(seq 0 $(expr $PORT_COUNT - 1)); do
		netdev_name=$(port_netdev_get $i)
		check_err $? "Failed to get netdev name for port $DL_HANDLE/$i"
		ip -n $NETNS_NAME link show $netdev_name &> /dev/null
		check_err $? "Failed to find netdev $netdev_name"
	done

	log_test "check ports test"
}

check_devlink_test()
{
	RET=0

	devlink -N $NETNS_NAME dev show $DL_HANDLE &> /dev/null
	check_err $? "Failed to show devlink instance"

	log_test "check devlink test"
}

setup_prepare()
{
	modprobe netdevsim
	ip netns add $NETNS_NAME
	ip netns exec $NETNS_NAME \
		echo "$BUS_ADDR $PORT_COUNT" > /sys/bus/netdevsim/new_device
	while [ ! -d $SYSFS_NET_DIR ] ; do :; done
}

cleanup()
{
	pre_cleanup
	echo "$BUS_ADDR" > /sys/bus/netdevsim/del_device
	ip netns del $NETNS_NAME
	modprobe -r netdevsim
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS
