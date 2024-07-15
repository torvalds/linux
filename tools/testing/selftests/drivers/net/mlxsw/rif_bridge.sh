#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	bridge_rif_add
	bridge_rif_nomaster
	bridge_rif_remaster
	bridge_rif_nomaster_addr
	bridge_rif_nomaster_port
	bridge_rif_remaster_port
"

NUM_NETIFS=2
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	team_create lag1 lacp
	ip link set dev lag1 addrgenmode none
	ip link set dev lag1 address $(mac_get $swp1)

	team_create lag2 lacp
	ip link set dev lag2 addrgenmode none
	ip link set dev lag2 address $(mac_get $swp2)

	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 addrgenmode none
	ip link set dev br1 address $(mac_get lag1)
	ip link set dev br1 up

	ip link set dev lag1 master br1

	ip link set dev $swp1 master lag1
	ip link set dev $swp1 up

	ip link set dev $swp2 master lag2
	ip link set dev $swp2 up
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 nomaster
	ip link set dev $swp2 down

	ip link set dev $swp1 nomaster
	ip link set dev $swp1 down

	ip link del dev lag2
	ip link set dev lag1 nomaster
	ip link del dev lag1

	ip link del dev br1
}

bridge_rif_add()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	__addr_add_del br1 add 192.0.2.2/28
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 + 1))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Add RIF for bridge on address addition"
}

bridge_rif_nomaster()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	ip link set dev lag1 nomaster
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 - 1))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Drop RIF for bridge on LAG deslavement"
}

bridge_rif_remaster()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	ip link set dev lag1 master br1
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 + 1))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Add RIF for bridge on LAG reenslavement"
}

bridge_rif_nomaster_addr()
{
	local rifs_occ_t0=$(devlink_resource_occ_get rifs)

	# Adding an address while the LAG is enslaved shouldn't generate a RIF.
	__addr_add_del lag1 add 192.0.2.65/28
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0))

	((expected_rifs == rifs_occ_t1))
	check_err $? "After adding IP: Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	# Removing the LAG from the bridge should drop RIF for the bridge (as
	# tested in bridge_rif_lag_nomaster), but since the LAG now has an
	# address, it should gain a RIF.
	ip link set dev lag1 nomaster
	sleep 1
	local rifs_occ_t2=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0))

	((expected_rifs == rifs_occ_t2))
	check_err $? "After deslaving: Expected $expected_rifs RIFs, $rifs_occ_t2 are used"

	log_test "Add RIF for LAG on deslavement from bridge"

	__addr_add_del lag1 del 192.0.2.65/28
	ip link set dev lag1 master br1
	sleep 1
}

bridge_rif_nomaster_port()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	ip link set dev $swp1 nomaster
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 - 1))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Drop RIF for bridge on deslavement of port from LAG"
}

bridge_rif_remaster_port()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	ip link set dev $swp1 down
	ip link set dev $swp1 master lag1
	ip link set dev $swp1 up
	setup_wait_dev $swp1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 + 1))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Add RIF for bridge on reenslavement of port to LAG"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
