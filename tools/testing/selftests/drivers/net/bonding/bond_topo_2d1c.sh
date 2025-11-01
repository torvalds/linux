#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Topology for Bond mode 1,5,6 testing
#
#  +-------------------------+
#  |          bond0          |  Server
#  |            +            |  192.0.2.1/24
#  |      eth0  |  eth1      |  2001:db8::1/24
#  |        +---+---+        |
#  |        |       |        |
#  +-------------------------+
#           |       |
#  +-------------------------+
#  |        |       |        |
#  |    +---+-------+---+    |  Gateway
#  |    |      br0      |    |  192.0.2.254/24
#  |    +-------+-------+    |  2001:db8::254/24
#  |            |            |
#  +-------------------------+
#               |
#  +-------------------------+
#  |            |            |  Client
#  |            +            |  192.0.2.10/24
#  |          eth0           |  2001:db8::10/24
#  +-------------------------+

REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

s_ns="s-$(mktemp -u XXXXXX)"
c_ns="c-$(mktemp -u XXXXXX)"
g_ns="g-$(mktemp -u XXXXXX)"
s_ip4="192.0.2.1"
c_ip4="192.0.2.10"
g_ip4="192.0.2.254"
s_ip6="2001:db8::1"
c_ip6="2001:db8::10"
g_ip6="2001:db8::254"
mac[0]="00:0a:0b:0c:0d:01"
mac[1]="00:0a:0b:0c:0d:02"

gateway_create()
{
	ip netns add ${g_ns}
	ip -n ${g_ns} link add br0 type bridge
	ip -n ${g_ns} link set br0 up
	ip -n ${g_ns} addr add ${g_ip4}/24 dev br0
	ip -n ${g_ns} addr add ${g_ip6}/24 dev br0
}

gateway_destroy()
{
	ip -n ${g_ns} link del br0
	ip netns del ${g_ns}
}

server_create()
{
	ip netns add ${s_ns}
	ip -n ${s_ns} link add bond0 type bond mode active-backup miimon 100

	for i in $(seq 0 1); do
		ip -n ${s_ns} link add eth${i} type veth peer name s${i} netns ${g_ns}
		ip -n "${s_ns}" link set "eth${i}" addr "${mac[$i]}"

		ip -n ${g_ns} link set s${i} up
		ip -n ${g_ns} link set s${i} master br0
		ip -n ${s_ns} link set eth${i} master bond0

		tc -n ${g_ns} qdisc add dev s${i} clsact
	done

	ip -n ${s_ns} link set bond0 up
	ip -n ${s_ns} addr add ${s_ip4}/24 dev bond0
	ip -n ${s_ns} addr add ${s_ip6}/24 dev bond0
}

# Reset bond with new mode and options
bond_reset()
{
	# Count the eth link number in real-time as this function
	# maybe called from other topologies.
	local link_num=$(ip -n ${s_ns} -br link show | grep -c "^eth")
	local param="$1"
	link_num=$((link_num -1))

	ip -n ${s_ns} link set bond0 down
	ip -n ${s_ns} link del bond0

	ip -n ${s_ns} link add bond0 type bond $param
	for i in $(seq 0 ${link_num}); do
		ip -n ${s_ns} link set eth$i master bond0
	done

	ip -n ${s_ns} link set bond0 up
	ip -n ${s_ns} addr add ${s_ip4}/24 dev bond0
	ip -n ${s_ns} addr add ${s_ip6}/24 dev bond0
	# Wait for IPv6 address ready as it needs DAD
	slowwait 2 ip netns exec ${s_ns} ping6 ${c_ip6} -c 1 -W 0.1 &> /dev/null
}

server_destroy()
{
	# Count the eth link number in real-time as this function
	# maybe called from other topologies.
	local link_num=$(ip -n ${s_ns} -br link show | grep -c "^eth")
	link_num=$((link_num -1))
	for i in $(seq 0 ${link_num}); do
		ip -n ${s_ns} link del eth${i}
	done
	ip netns del ${s_ns}
}

client_create()
{
	ip netns add ${c_ns}
	ip -n ${c_ns} link add eth0 type veth peer name c0 netns ${g_ns}

	ip -n ${g_ns} link set c0 up
	ip -n ${g_ns} link set c0 master br0

	ip -n ${c_ns} link set eth0 up
	ip -n ${c_ns} addr add ${c_ip4}/24 dev eth0
	ip -n ${c_ns} addr add ${c_ip6}/24 dev eth0
}

client_destroy()
{
	ip -n ${c_ns} link del eth0
	ip netns del ${c_ns}
}

setup_prepare()
{
	gateway_create
	server_create
	client_create
}

cleanup()
{
	pre_cleanup

	client_destroy
	server_destroy
	gateway_destroy
}

bond_check_connection()
{
	local msg=${1:-"check connection"}

	slowwait 2 ip netns exec ${s_ns} ping ${c_ip4} -c 1 -W 0.1 &> /dev/null
	ip netns exec ${s_ns} ping ${c_ip4} -c5 -i 0.1 &>/dev/null
	check_err $? "${msg}: ping failed"
	ip netns exec ${s_ns} ping6 ${c_ip6} -c5 -i 0.1 &>/dev/null
	check_err $? "${msg}: ping6 failed"
}
