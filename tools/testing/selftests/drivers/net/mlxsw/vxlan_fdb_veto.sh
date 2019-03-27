#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test vetoing of FDB entries that mlxsw can not offload. This exercises several
# different veto vectors to test various rollback scenarios in the vxlan driver.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	fdb_create_veto_test
	fdb_replace_veto_test
	fdb_append_veto_test
	fdb_changelink_veto_test
"
NUM_NETIFS=2
source $lib_dir/lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	ip link add dev br0 type bridge mcast_snooping 0

	ip link set dev $swp1 up
	ip link set dev $swp1 master br0
	ip link set dev $swp2 up

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link set dev vxlan0 master br0
}

cleanup()
{
	pre_cleanup

	ip link set dev vxlan0 nomaster
	ip link del dev vxlan0

	ip link set dev $swp2 down
	ip link set dev $swp1 nomaster
	ip link set dev $swp1 down

	ip link del dev br0
}

fdb_create_veto_test()
{
	RET=0

	bridge fdb add 01:02:03:04:05:06 dev vxlan0 self static \
	       dst 198.51.100.2 2>/dev/null
	check_fail $? "multicast MAC not rejected"

	bridge fdb add 01:02:03:04:05:06 dev vxlan0 self static \
	       dst 198.51.100.2 2>&1 >/dev/null | grep -q mlxsw_spectrum
	check_err $? "multicast MAC rejected without extack"

	log_test "vxlan FDB veto - create"
}

fdb_replace_veto_test()
{
	RET=0

	bridge fdb add 00:01:02:03:04:05 dev vxlan0 self static \
	       dst 198.51.100.2
	check_err $? "valid FDB rejected"

	bridge fdb replace 00:01:02:03:04:05 dev vxlan0 self static \
	       dst 198.51.100.2 port 1234 2>/dev/null
	check_fail $? "FDB with an explicit port not rejected"

	bridge fdb replace 00:01:02:03:04:05 dev vxlan0 self static \
	       dst 198.51.100.2 port 1234 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "FDB with an explicit port rejected without extack"

	log_test "vxlan FDB veto - replace"
}

fdb_append_veto_test()
{
	RET=0

	bridge fdb add 00:00:00:00:00:00 dev vxlan0 self static \
	       dst 198.51.100.2
	check_err $? "valid FDB rejected"

	bridge fdb append 00:00:00:00:00:00 dev vxlan0 self static \
	       dst 198.51.100.3 port 1234 2>/dev/null
	check_fail $? "FDB with an explicit port not rejected"

	bridge fdb append 00:00:00:00:00:00 dev vxlan0 self static \
	       dst 198.51.100.3 port 1234 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "FDB with an explicit port rejected without extack"

	log_test "vxlan FDB veto - append"
}

fdb_changelink_veto_test()
{
	RET=0

	ip link set dev vxlan0 type vxlan \
	   group 224.0.0.1 dev lo 2>/dev/null
	check_fail $? "FDB with a multicast IP not rejected"

	ip link set dev vxlan0 type vxlan \
	   group 224.0.0.1 dev lo 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "FDB with a multicast IP rejected without extack"

	log_test "vxlan FDB veto - changelink"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
