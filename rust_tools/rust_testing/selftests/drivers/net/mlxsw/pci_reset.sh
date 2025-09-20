#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that PCI reset works correctly by verifying that only the expected reset
# methods are supported and that after issuing the reset the ifindex of the
# port changes.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	pci_reset_test
"
NUM_NETIFS=1
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

pci_reset_test()
{
	RET=0

	local bus=$(echo $DEVLINK_DEV | cut -d '/' -f 1)
	local bdf=$(echo $DEVLINK_DEV | cut -d '/' -f 2)

	if [ $bus != "pci" ]; then
		check_err 1 "devlink device is not a PCI device"
		log_test "pci reset"
		return
	fi

	if [ ! -f /sys/bus/pci/devices/$bdf/reset_method ]; then
		check_err 1 "reset is not supported"
		log_test "pci reset"
		return
	fi

	[[ $(cat /sys/bus/pci/devices/$bdf/reset_method) == "bus" ]]
	check_err $? "only \"bus\" reset method should be supported"

	local ifindex_pre=$(ip -j link show dev $swp1 | jq '.[]["ifindex"]')

	echo 1 > /sys/bus/pci/devices/$bdf/reset
	check_err $? "reset failed"

	# Wait for udev to rename newly created netdev.
	udevadm settle

	local ifindex_post=$(ip -j link show dev $swp1 | jq '.[]["ifindex"]')

	[[ $ifindex_pre != $ifindex_post ]]
	check_err $? "reset not performed"

	log_test "pci reset"
}

swp1=${NETIFS[p1]}
tests_run

exit $EXIT_STATUS
