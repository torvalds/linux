#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

ALL_TESTS="
	test_binding_on
	test_binding_off
	test_binding_toggle_on
	test_binding_toggle_off
	test_binding_toggle_on_when_upper_down
	test_binding_toggle_off_when_upper_down
	test_binding_toggle_on_when_lower_down
	test_binding_toggle_off_when_lower_down
"

setup_prepare()
{
	local port

	ip_link_add br up type bridge vlan_filtering 1

	for port in d1 d2 d3; do
		ip_link_add $port type veth peer name r$port
		ip_link_set_up $port
		ip_link_set_up r$port
		ip_link_set_master $port br
	done

	bridge_vlan_add vid 11 dev br self
	bridge_vlan_add vid 11 dev d1 master

	bridge_vlan_add vid 12 dev br self
	bridge_vlan_add vid 12 dev d2 master

	bridge_vlan_add vid 13 dev br self
	bridge_vlan_add vid 13 dev d1 master
	bridge_vlan_add vid 13 dev d2 master

	bridge_vlan_add vid 14 dev br self
	bridge_vlan_add vid 14 dev d1 master
	bridge_vlan_add vid 14 dev d2 master
	bridge_vlan_add vid 14 dev d3 master
}

operstate_is()
{
	local dev=$1; shift
	local expect=$1; shift

	local operstate=$(ip -j link show $dev | jq -r .[].operstate)
	if [[ $operstate == UP ]]; then
		operstate=1
	elif [[ $operstate == DOWN || $operstate == LOWERLAYERDOWN ]]; then
		operstate=0
	fi
	echo -n $operstate
	[[ $operstate == $expect ]]
}

check_operstate()
{
	local dev=$1; shift
	local expect=$1; shift
	local operstate

	operstate=$(busywait 1000 \
			operstate_is "$dev" "$expect")
	check_err $? "Got operstate of $operstate, expected $expect"
}

add_one_vlan()
{
	local link=$1; shift
	local id=$1; shift

	ip_link_add $link.$id link $link type vlan id $id "$@"
}

add_vlans()
{
	add_one_vlan br 11 "$@"
	add_one_vlan br 12 "$@"
	add_one_vlan br 13 "$@"
	add_one_vlan br 14 "$@"
}

set_vlans()
{
	ip link set dev br.11 "$@"
	ip link set dev br.12 "$@"
	ip link set dev br.13 "$@"
	ip link set dev br.14 "$@"
}

down_netdevs()
{
	local dev

	for dev in "$@"; do
		ip_link_set_down $dev
	done
}

check_operstates()
{
	local opst_11=$1; shift
	local opst_12=$1; shift
	local opst_13=$1; shift
	local opst_14=$1; shift

	check_operstate br.11 $opst_11
	check_operstate br.12 $opst_12
	check_operstate br.13 $opst_13
	check_operstate br.14 $opst_14
}

do_test_binding()
{
	local inject=$1; shift
	local what=$1; shift
	local opsts_d1=$1; shift
	local opsts_d2=$1; shift
	local opsts_d12=$1; shift
	local opsts_d123=$1; shift

	RET=0

	defer_scope_push
		down_netdevs d1
		$inject
		check_operstates $opsts_d1
	defer_scope_pop

	defer_scope_push
		down_netdevs d2
		$inject
		check_operstates $opsts_d2
	defer_scope_pop

	defer_scope_push
		down_netdevs d1 d2
		$inject
		check_operstates $opsts_d12
	defer_scope_pop

	defer_scope_push
		down_netdevs d1 d2 d3
		$inject
		check_operstates $opsts_d123
	defer_scope_pop

	log_test "Test bridge_binding $what"
}

do_test_binding_on()
{
	local inject=$1; shift
	local what=$1; shift

	do_test_binding "$inject" "$what"	\
			"0 1 1 1"		\
			"1 0 1 1"		\
			"0 0 0 1"		\
			"0 0 0 0"
}

do_test_binding_off()
{
	local inject=$1; shift
	local what=$1; shift

	do_test_binding "$inject" "$what"	\
			"1 1 1 1"		\
			"1 1 1 1"		\
			"1 1 1 1"		\
			"0 0 0 0"
}

test_binding_on()
{
	add_vlans bridge_binding on
	set_vlans up
	do_test_binding_on : "on"
}

test_binding_off()
{
	add_vlans bridge_binding off
	set_vlans up
	do_test_binding_off : "off"
}

test_binding_toggle_on()
{
	add_vlans bridge_binding off
	set_vlans up
	set_vlans type vlan bridge_binding on
	do_test_binding_on : "off->on"
}

test_binding_toggle_off()
{
	add_vlans bridge_binding on
	set_vlans up
	set_vlans type vlan bridge_binding off
	do_test_binding_off : "on->off"
}

dfr_set_binding_on()
{
	set_vlans type vlan bridge_binding on
	defer set_vlans type vlan bridge_binding off
}

dfr_set_binding_off()
{
	set_vlans type vlan bridge_binding off
	defer set_vlans type vlan bridge_binding on
}

test_binding_toggle_on_when_lower_down()
{
	add_vlans bridge_binding off
	set_vlans up
	do_test_binding_on dfr_set_binding_on "off->on when lower down"
}

test_binding_toggle_off_when_lower_down()
{
	add_vlans bridge_binding on
	set_vlans up
	do_test_binding_off dfr_set_binding_off "on->off when lower down"
}

test_binding_toggle_on_when_upper_down()
{
	add_vlans bridge_binding off
	set_vlans type vlan bridge_binding on
	set_vlans up
	do_test_binding_on : "off->on when upper down"
}

test_binding_toggle_off_when_upper_down()
{
	add_vlans bridge_binding on
	set_vlans type vlan bridge_binding off
	set_vlans up
	do_test_binding_off : "on->off when upper down"
}

trap defer_scopes_cleanup EXIT
setup_prepare
tests_run

exit $EXIT_STATUS
