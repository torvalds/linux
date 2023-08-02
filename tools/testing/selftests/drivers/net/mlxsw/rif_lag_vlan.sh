#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	lag_rif_add
	lag_rif_nomaster
	lag_rif_remaster
	lag_rif_nomaster_addr
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

	ip link set dev $swp1 master lag1
	ip link set dev $swp1 up

	ip link set dev $swp2 master lag2
	ip link set dev $swp2 up

	vlan_create lag1 100
	ip link set dev lag1.100 addrgenmode none

	vlan_create lag1 200
	ip link set dev lag1.200 addrgenmode none
}

cleanup()
{
	pre_cleanup

	ip link del dev lag1.200
	ip link del dev lag1.100

	ip link set dev $swp2 nomaster
	ip link set dev $swp2 down

	ip link set dev $swp1 nomaster
	ip link set dev $swp1 down

	ip link del dev lag2
	ip link del dev lag1
}

lag_rif_add()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	__addr_add_del lag1.100 add 192.0.2.2/28
	__addr_add_del lag1.200 add 192.0.2.18/28
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 + 2))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Add RIFs for LAG VLANs on address addition"
}

lag_rif_nomaster()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	ip link set dev $swp1 nomaster
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 - 2))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Drop RIFs for LAG VLANs on port deslavement"
}

lag_rif_remaster()
{
	RET=0

	local rifs_occ_t0=$(devlink_resource_occ_get rifs)
	ip link set dev $swp1 down
	ip link set dev $swp1 master lag1
	ip link set dev $swp1 up
	setup_wait_dev $swp1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 + 2))

	((expected_rifs == rifs_occ_t1))
	check_err $? "Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	log_test "Add RIFs for LAG VLANs on port reenslavement"
}

lag_rif_nomaster_addr()
{
	local rifs_occ_t0=$(devlink_resource_occ_get rifs)

	# Adding an address while the port is LAG'd shouldn't generate a RIF.
	__addr_add_del $swp1 add 192.0.2.65/28
	sleep 1
	local rifs_occ_t1=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0))

	((expected_rifs == rifs_occ_t1))
	check_err $? "After adding IP: Expected $expected_rifs RIFs, $rifs_occ_t1 are used"

	# Removing the port from LAG should drop two RIFs for the LAG VLANs (as
	# tested in lag_rif_nomaster), but since the port now has an address, it
	# should gain a RIF.
	ip link set dev $swp1 nomaster
	sleep 1
	local rifs_occ_t2=$(devlink_resource_occ_get rifs)
	local expected_rifs=$((rifs_occ_t0 - 1))

	((expected_rifs == rifs_occ_t2))
	check_err $? "After deslaving: Expected $expected_rifs RIFs, $rifs_occ_t2 are used"

	__addr_add_del $swp1 del 192.0.2.65/28
	log_test "Add RIF for port on deslavement from LAG"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
