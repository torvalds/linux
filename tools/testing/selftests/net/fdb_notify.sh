#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

ALL_TESTS="
	test_dup_bridge
	test_dup_vxlan_self
	test_dup_vxlan_master
	test_dup_macvlan_self
	test_dup_macvlan_master
"

do_test_dup()
{
	local op=$1; shift
	local what=$1; shift
	local tmpf

	RET=0

	tmpf=$(mktemp)
	defer rm "$tmpf"

	defer_scope_push
		bridge monitor fdb &> "$tmpf" &
		defer kill_process $!

		sleep 0.5
		bridge fdb "$op" 00:11:22:33:44:55 vlan 1 "$@"
		sleep 0.5
	defer_scope_pop

	local count=$(grep -c -e 00:11:22:33:44:55 $tmpf)
	((count == 1))
	check_err $? "Got $count notifications, expected 1"

	log_test "$what $op: Duplicate notifications"
}

test_dup_bridge()
{
	adf_ip_link_add br up type bridge vlan_filtering 1
	do_test_dup add "bridge" dev br self
	do_test_dup del "bridge" dev br self
}

test_dup_vxlan_self()
{
	adf_ip_link_add br up type bridge vlan_filtering 1
	adf_ip_link_add vx up type vxlan id 2000 dstport 4789
	adf_ip_link_set_master vx br

	do_test_dup add "vxlan" dev vx self dst 192.0.2.1
	do_test_dup del "vxlan" dev vx self dst 192.0.2.1
}

test_dup_vxlan_master()
{
	adf_ip_link_add br up type bridge vlan_filtering 1
	adf_ip_link_add vx up type vxlan id 2000 dstport 4789
	adf_ip_link_set_master vx br

	do_test_dup add "vxlan master" dev vx master
	do_test_dup del "vxlan master" dev vx master
}

test_dup_macvlan_self()
{
	adf_ip_link_add dd up type dummy
	adf_ip_link_add mv up link dd type macvlan mode passthru

	do_test_dup add "macvlan self" dev mv self
	do_test_dup del "macvlan self" dev mv self
}

test_dup_macvlan_master()
{
	adf_ip_link_add br up type bridge vlan_filtering 1
	adf_ip_link_add dd up type dummy
	adf_ip_link_add mv up link dd type macvlan mode passthru
	adf_ip_link_set_master mv br

	do_test_dup add "macvlan master" dev mv self
	do_test_dup del "macvlan master" dev mv self
}

cleanup()
{
	defer_scopes_cleanup
}

trap cleanup EXIT
tests_run

exit $EXIT_STATUS
