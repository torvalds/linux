#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ipv6=true

source ./hsr_common.sh

optstring="h4"
usage() {
	echo "Usage: $0 [OPTION]"
	echo -e "\t-4: IPv4 only: disable IPv6 tests (default: test both IPv4 and IPv6)"
}

while getopts "$optstring" option;do
	case "$option" in
	"h")
		usage "$0"
		exit 0
		;;
	"4")
		ipv6=false
		;;
	"?")
		usage "$0"
		exit 1
		;;
esac
done

setup_prp_interfaces()
{
	echo "INFO: Preparing interfaces for PRP"
# Two PRP nodes, connected by two links (treated as LAN A and LAN B).
#
#       vethA ----- vethA
#     prp1             prp2
#       vethB ----- vethB
#
#     node1           node2

	# Interfaces
	# shellcheck disable=SC2154 # variables assigned by setup_ns
	ip link add vethA netns "$node1" type veth peer name vethA netns "$node2"
	ip link add vethB netns "$node1" type veth peer name vethB netns "$node2"

	# MAC addresses will be copied from LAN A interface
	ip -net "$node1" link set address 00:11:22:00:00:01 dev vethA
	ip -net "$node2" link set address 00:11:22:00:00:02 dev vethA

	# PRP
	ip -net "$node1" link add name prp1 type hsr \
		slave1 vethA slave2 vethB supervision 45 proto 1
	ip -net "$node2" link add name prp2 type hsr \
		slave1 vethA slave2 vethB supervision 45 proto 1

	# IP addresses
	ip -net "$node1" addr add 100.64.0.1/24 dev prp1
	ip -net "$node1" addr add dead:beef:0::1/64 dev prp1 nodad
	ip -net "$node2" addr add 100.64.0.2/24 dev prp2
	ip -net "$node2" addr add dead:beef:0::2/64 dev prp2 nodad

	# All links up
	ip -net "$node1" link set vethA up
	ip -net "$node1" link set vethB up
	ip -net "$node1" link set prp1 up

	ip -net "$node2" link set vethA up
	ip -net "$node2" link set vethB up
	ip -net "$node2" link set prp2 up
}

setup_vlan_interfaces()
{
	# Interfaces
	ip -net "$node1" link add link prp1 name prp1.2 type vlan id 2
	ip -net "$node2" link add link prp2 name prp2.2 type vlan id 2

	# IP addresses
	ip -net "$node1" addr add 100.64.2.1/24 dev prp1.2
	ip -net "$node1" addr add dead:beef:2::1/64 dev prp1.2 nodad

	ip -net "$node2" addr add 100.64.2.2/24 dev prp2.2
	ip -net "$node2" addr add dead:beef:2::2/64 dev prp2.2 nodad

	# All links up
	ip -net "$node1" link set prp1.2 up
	ip -net "$node2" link set prp2.2 up
}

do_ping_tests()
{
	local netid="$1"

	echo "INFO: Initial validation ping"

	do_ping "$node1" "100.64.$netid.2"
	do_ping "$node2" "100.64.$netid.1"
	stop_if_error "Initial validation failed on IPv4"

	do_ping "$node1" "dead:beef:$netid::2"
	do_ping "$node2" "dead:beef:$netid::1"
	stop_if_error "Initial validation failed on IPv6"

	echo "INFO: Longer ping test."

	do_ping_long "$node1" "100.64.$netid.2"
	do_ping_long "$node2" "100.64.$netid.1"
	stop_if_error "Longer ping test failed on IPv4."

	do_ping_long "$node1" "dead:beef:$netid::2"
	do_ping_long "$node2" "dead:beef:$netid::1"
	stop_if_error "Longer ping test failed on IPv6."
}

run_ping_tests()
{
	echo "INFO: Running ping tests"
	do_ping_tests 0
}

run_vlan_ping_tests()
{
	vlan_challenged_prp1=$(ip net exec "$node1" ethtool -k prp1 | \
		grep "vlan-challenged" | awk '{print $2}')
	vlan_challenged_prp2=$(ip net exec "$node2" ethtool -k prp2 | \
		grep "vlan-challenged" | awk '{print $2}')

	if [[ "$vlan_challenged_prp1" = "off" || \
	      "$vlan_challenged_prp2" = "off" ]]; then
		echo "INFO: Running VLAN ping tests"
		setup_vlan_interfaces
		do_ping_tests 2
	else
		echo "INFO: Not Running VLAN tests as the device does not support VLAN"
	fi
}

check_prerequisites
trap cleanup_all_ns EXIT

setup_ns node1 node2
setup_prp_interfaces

run_ping_tests
run_vlan_ping_tests

exit $ret
