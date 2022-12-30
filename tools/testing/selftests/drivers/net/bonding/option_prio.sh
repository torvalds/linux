#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bonding option prio
#

ALL_TESTS="
	prio_arp_ip_target_test
	prio_miimon_test
"

REQUIRE_MZ=no
REQUIRE_JQ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/net_forwarding_lib.sh

destroy()
{
	ip link del bond0 &>/dev/null
	ip link del br0 &>/dev/null
	ip link del veth0 &>/dev/null
	ip link del veth1 &>/dev/null
	ip link del veth2 &>/dev/null
	ip netns del ns1 &>/dev/null
	ip link del veth3 &>/dev/null
}

cleanup()
{
	pre_cleanup

	destroy
}

skip()
{
        local skip=1
	ip link add name bond0 type bond mode 1 miimon 100 &>/dev/null
	ip link add name veth0 type veth peer name veth0_p
	ip link set veth0 master bond0

	# check if iproute support prio option
	ip link set dev veth0 type bond_slave prio 10
	[[ $? -ne 0 ]] && skip=0

	# check if bonding support prio option
	ip -d link show veth0 | grep -q "prio 10"
	[[ $? -ne 0 ]] && skip=0

	ip link del bond0 &>/dev/null
	ip link del veth0

	return $skip
}

active_slave=""
check_active_slave()
{
	local target_active_slave=$1
	active_slave="$(cat /sys/class/net/bond0/bonding/active_slave)"
	test "$active_slave" = "$target_active_slave"
	check_err $? "Current active slave is $active_slave but not $target_active_slave"
}


# Test bonding prio option with mode=$mode monitor=$monitor
# and primary_reselect=$primary_reselect
prio_test()
{
	RET=0

	local monitor=$1
	local mode=$2
	local primary_reselect=$3

	local bond_ip4="192.169.1.2"
	local peer_ip4="192.169.1.1"
	local bond_ip6="2009:0a:0b::02"
	local peer_ip6="2009:0a:0b::01"


	# create veths
	ip link add name veth0 type veth peer name veth0_p
	ip link add name veth1 type veth peer name veth1_p
	ip link add name veth2 type veth peer name veth2_p

	# create bond
	if [[ "$monitor" == "miimon" ]];then
		ip link add name bond0 type bond mode $mode miimon 100 primary veth1 primary_reselect $primary_reselect
	elif [[ "$monitor" == "arp_ip_target" ]];then
		ip link add name bond0 type bond mode $mode arp_interval 1000 arp_ip_target $peer_ip4 primary veth1 primary_reselect $primary_reselect
	elif [[ "$monitor" == "ns_ip6_target" ]];then
		ip link add name bond0 type bond mode $mode arp_interval 1000 ns_ip6_target $peer_ip6 primary veth1 primary_reselect $primary_reselect
	fi
	ip link set bond0 up
	ip link set veth0 master bond0
	ip link set veth1 master bond0
	ip link set veth2 master bond0
	# check bonding member prio value
	ip link set dev veth0 type bond_slave prio 0
	ip link set dev veth1 type bond_slave prio 10
	ip link set dev veth2 type bond_slave prio 11
	ip -d link show veth0 | grep -q 'prio 0'
	check_err $? "veth0 prio is not 0"
	ip -d link show veth1 | grep -q 'prio 10'
	check_err $? "veth0 prio is not 10"
	ip -d link show veth2 | grep -q 'prio 11'
	check_err $? "veth0 prio is not 11"

	ip link set veth0 up
	ip link set veth1 up
	ip link set veth2 up
	ip link set veth0_p up
	ip link set veth1_p up
	ip link set veth2_p up

	# prepare ping target
	ip link add name br0 type bridge
	ip link set br0 up
	ip link set veth0_p master br0
	ip link set veth1_p master br0
	ip link set veth2_p master br0
	ip link add name veth3 type veth peer name veth3_p
	ip netns add ns1
	ip link set veth3_p master br0 up
	ip link set veth3 netns ns1 up
	ip netns exec ns1 ip addr add $peer_ip4/24 dev veth3
	ip netns exec ns1 ip addr add $peer_ip6/64 dev veth3
	ip addr add $bond_ip4/24 dev bond0
	ip addr add $bond_ip6/64 dev bond0
	sleep 5

	ping $peer_ip4 -c5 -I bond0 &>/dev/null
	check_err $? "ping failed 1."
	ping6 $peer_ip6 -c5 -I bond0 &>/dev/null
	check_err $? "ping6 failed 1."

	# active salve should be the primary slave
	check_active_slave veth1

	# active slave should be the higher prio slave
	ip link set $active_slave down
	ping $peer_ip4 -c5 -I bond0 &>/dev/null
	check_err $? "ping failed 2."
	check_active_slave veth2

	# when only 1 slave is up
	ip link set $active_slave down
	ping $peer_ip4 -c5 -I bond0 &>/dev/null
	check_err $? "ping failed 3."
	check_active_slave veth0

	# when a higher prio slave change to up
	ip link set veth2 up
	ping $peer_ip4 -c5 -I bond0 &>/dev/null
	check_err $? "ping failed 4."
	case $primary_reselect in
		"0")
			check_active_slave "veth2"
			;;
		"1")
			check_active_slave "veth0"
			;;
		"2")
			check_active_slave "veth0"
			;;
	esac
	local pre_active_slave=$active_slave

	# when the primary slave change to up
	ip link set veth1 up
	ping $peer_ip4 -c5 -I bond0 &>/dev/null
	check_err $? "ping failed 5."
	case $primary_reselect in
		"0")
			check_active_slave "veth1"
			;;
		"1")
			check_active_slave "$pre_active_slave"
			;;
		"2")
			check_active_slave "$pre_active_slave"
			ip link set $active_slave down
			ping $peer_ip4 -c5 -I bond0 &>/dev/null
			check_err $? "ping failed 6."
			check_active_slave "veth1"
			;;
	esac

	# Test changing bond salve prio
	if [[ "$primary_reselect" == "0" ]];then
		ip link set dev veth0 type bond_slave prio 1000000
		ip link set dev veth1 type bond_slave prio 0
		ip link set dev veth2 type bond_slave prio -50
		ip -d link show veth0 | grep -q 'prio 1000000'
		check_err $? "veth0 prio is not 1000000"
		ip -d link show veth1 | grep -q 'prio 0'
		check_err $? "veth1 prio is not 0"
		ip -d link show veth2 | grep -q 'prio -50'
		check_err $? "veth3 prio is not -50"
		check_active_slave "veth1"

		ip link set $active_slave down
		ping $peer_ip4 -c5 -I bond0 &>/dev/null
		check_err $? "ping failed 7."
		check_active_slave "veth0"
	fi

	cleanup

	log_test "prio_test" "Test bonding option 'prio' with mode=$mode monitor=$monitor and primary_reselect=$primary_reselect"
}

prio_miimon_test()
{
	local mode
	local primary_reselect

	for mode in 1 5 6; do
		for primary_reselect in 0 1 2; do
			prio_test "miimon" $mode $primary_reselect
		done
	done
}

prio_arp_ip_target_test()
{
	local primary_reselect

	for primary_reselect in 0 1 2; do
		prio_test "arp_ip_target" 1 $primary_reselect
	done
}

if skip;then
	log_test_skip "option_prio.sh" "Current iproute doesn't support 'prio'."
	exit 0
fi

trap cleanup EXIT

tests_run

exit "$EXIT_STATUS"
