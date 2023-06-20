#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright 2022 NXP

# The script is mostly generic, with the exception of the
# ethtool per-TC counter names ("rx_green_prio_${tc}")

WAIT_TIME=1
NUM_NETIFS=4
STABLE_MAC_ADDRS=yes
NETIF_CREATE=no
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

require_command dcb

h1=${NETIFS[p1]}
swp1=${NETIFS[p2]}
swp2=${NETIFS[p3]}
h2=${NETIFS[p4]}

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

h1_create()
{
	simple_if_init $h1 $H1_IPV4/24 $H1_IPV6/64
}

h1_destroy()
{
	simple_if_fini $h1 $H1_IPV4/24 $H1_IPV6/64
}

h2_create()
{
	simple_if_init $h2 $H2_IPV4/24 $H2_IPV6/64
}

h2_destroy()
{
	simple_if_fini $h2 $H2_IPV4/24 $H2_IPV6/64
}

h1_vlan_create()
{
	local vid=$1

	vlan_create $h1 $vid
	simple_if_init $h1.$vid $H1_IPV4/24 $H1_IPV6/64
	ip link set $h1.$vid type vlan \
		egress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7 \
		ingress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7
}

h1_vlan_destroy()
{
	local vid=$1

	simple_if_fini $h1.$vid $H1_IPV4/24 $H1_IPV6/64
	vlan_destroy $h1 $vid
}

h2_vlan_create()
{
	local vid=$1

	vlan_create $h2 $vid
	simple_if_init $h2.$vid $H2_IPV4/24 $H2_IPV6/64
	ip link set $h2.$vid type vlan \
		egress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7 \
		ingress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7
}

h2_vlan_destroy()
{
	local vid=$1

	simple_if_fini $h2.$vid $H2_IPV4/24 $H2_IPV6/64
	vlan_destroy $h2 $vid
}

vlans_prepare()
{
	h1_vlan_create 100
	h2_vlan_create 100

	tc qdisc add dev ${h1}.100 clsact
	tc filter add dev ${h1}.100 egress protocol ipv4 \
		flower ip_proto icmp action skbedit priority 3
	tc filter add dev ${h1}.100 egress protocol ipv6 \
		flower ip_proto icmpv6 action skbedit priority 3
}

vlans_destroy()
{
	tc qdisc del dev ${h1}.100 clsact

	h1_vlan_destroy 100
	h2_vlan_destroy 100
}

switch_create()
{
	ip link set ${swp1} up
	ip link set ${swp2} up

	# Ports should trust VLAN PCP even with vlan_filtering=0
	ip link add br0 type bridge
	ip link set ${swp1} master br0
	ip link set ${swp2} master br0
	ip link set br0 up
}

switch_destroy()
{
	ip link del br0
}

setup_prepare()
{
	vrf_prepare

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy
	switch_destroy

	vrf_cleanup
}

dscp_cs_to_tos()
{
	local dscp_cs=$1

	# https://datatracker.ietf.org/doc/html/rfc2474
	# 4.2.2.1  The Class Selector Codepoints
	echo $((${dscp_cs} << 5))
}

run_test()
{
	local test_name=$1; shift
	local if_name=$1; shift
	local tc=$1; shift
	local tos=$1; shift
	local counter_name="rx_green_prio_${tc}"
	local ipv4_before
	local ipv4_after
	local ipv6_before
	local ipv6_after

	ipv4_before=$(ethtool_stats_get ${swp1} "${counter_name}")
	ping_do ${if_name} $H2_IPV4 "-Q ${tos}"
	ipv4_after=$(ethtool_stats_get ${swp1} "${counter_name}")

	if [ $((${ipv4_after} - ${ipv4_before})) -lt ${PING_COUNT} ]; then
		RET=1
	else
		RET=0
	fi
	log_test "IPv4 ${test_name}"

	ipv6_before=$(ethtool_stats_get ${swp1} "${counter_name}")
	ping_do ${if_name} $H2_IPV6 "-Q ${tos}"
	ipv6_after=$(ethtool_stats_get ${swp1} "${counter_name}")

	if [ $((${ipv6_after} - ${ipv6_before})) -lt ${PING_COUNT} ]; then
		RET=1
	else
		RET=0
	fi
	log_test "IPv6 ${test_name}"
}

port_default_prio_get()
{
	local if_name=$1
	local prio

	prio="$(dcb -j app show dev ${if_name} default-prio | \
		jq '.default_prio[]')"
	if [ -z "${prio}" ]; then
		prio=0
	fi

	echo ${prio}
}

test_port_default()
{
	local orig=$(port_default_prio_get ${swp1})
	local dmac=$(mac_get ${h2})

	dcb app replace dev ${swp1} default-prio 5

	run_test "Port-default QoS classification" ${h1} 5 0

	dcb app replace dev ${swp1} default-prio ${orig}
}

test_vlan_pcp()
{
	vlans_prepare

	run_test "Trusted VLAN PCP QoS classification" ${h1}.100 3 0

	vlans_destroy
}

test_ip_dscp()
{
	local port_default=$(port_default_prio_get ${swp1})
	local tos=$(dscp_cs_to_tos 4)

	dcb app add dev ${swp1} dscp-prio CS4:4
	run_test "Trusted DSCP QoS classification" ${h1} 4 ${tos}
	dcb app del dev ${swp1} dscp-prio CS4:4

	vlans_prepare
	run_test "Untrusted DSCP QoS classification follows VLAN PCP" \
		${h1}.100 3 ${tos}
	vlans_destroy

	run_test "Untrusted DSCP QoS classification follows port default" \
		${h1} ${port_default} ${tos}
}

trap cleanup EXIT

ALL_TESTS="
	test_port_default
	test_vlan_pcp
	test_ip_dscp
"

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
