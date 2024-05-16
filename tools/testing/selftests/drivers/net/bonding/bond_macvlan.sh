#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test macvlan over balance-alb

lib_dir=$(dirname "$0")
source ${lib_dir}/bond_topo_2d1c.sh

m1_ns="m1-$(mktemp -u XXXXXX)"
m2_ns="m1-$(mktemp -u XXXXXX)"
m1_ip4="192.0.2.11"
m1_ip6="2001:db8::11"
m2_ip4="192.0.2.12"
m2_ip6="2001:db8::12"

cleanup()
{
	ip -n ${m1_ns} link del macv0
	ip netns del ${m1_ns}
	ip -n ${m2_ns} link del macv0
	ip netns del ${m2_ns}

	client_destroy
	server_destroy
	gateway_destroy
}

check_connection()
{
	local ns=${1}
	local target=${2}
	local message=${3:-"macvlan_over_bond"}
	RET=0


	ip netns exec ${ns} ping ${target} -c 4 -i 0.1 &>/dev/null
	check_err $? "ping failed"
	log_test "$mode: $message"
}

macvlan_over_bond()
{
	local param="$1"
	RET=0

	# setup new bond mode
	bond_reset "${param}"

	ip -n ${s_ns} link add link bond0 name macv0 type macvlan mode bridge
	ip -n ${s_ns} link set macv0 netns ${m1_ns}
	ip -n ${m1_ns} link set dev macv0 up
	ip -n ${m1_ns} addr add ${m1_ip4}/24 dev macv0
	ip -n ${m1_ns} addr add ${m1_ip6}/24 dev macv0

	ip -n ${s_ns} link add link bond0 name macv0 type macvlan mode bridge
	ip -n ${s_ns} link set macv0 netns ${m2_ns}
	ip -n ${m2_ns} link set dev macv0 up
	ip -n ${m2_ns} addr add ${m2_ip4}/24 dev macv0
	ip -n ${m2_ns} addr add ${m2_ip6}/24 dev macv0

	sleep 2

	check_connection "${c_ns}" "${s_ip4}" "IPv4: client->server"
	check_connection "${c_ns}" "${s_ip6}" "IPv6: client->server"
	check_connection "${c_ns}" "${m1_ip4}" "IPv4: client->macvlan_1"
	check_connection "${c_ns}" "${m1_ip6}" "IPv6: client->macvlan_1"
	check_connection "${c_ns}" "${m2_ip4}" "IPv4: client->macvlan_2"
	check_connection "${c_ns}" "${m2_ip6}" "IPv6: client->macvlan_2"
	check_connection "${m1_ns}" "${m2_ip4}" "IPv4: macvlan_1->macvlan_2"
	check_connection "${m1_ns}" "${m2_ip6}" "IPv6: macvlan_1->macvlan_2"


	sleep 5

	check_connection "${s_ns}" "${c_ip4}" "IPv4: server->client"
	check_connection "${s_ns}" "${c_ip6}" "IPv6: server->client"
	check_connection "${m1_ns}" "${c_ip4}" "IPv4: macvlan_1->client"
	check_connection "${m1_ns}" "${c_ip6}" "IPv6: macvlan_1->client"
	check_connection "${m2_ns}" "${c_ip4}" "IPv4: macvlan_2->client"
	check_connection "${m2_ns}" "${c_ip6}" "IPv6: macvlan_2->client"
	check_connection "${m2_ns}" "${m1_ip4}" "IPv4: macvlan_2->macvlan_2"
	check_connection "${m2_ns}" "${m1_ip6}" "IPv6: macvlan_2->macvlan_2"

	ip -n ${c_ns} neigh flush dev eth0
}

trap cleanup EXIT

setup_prepare
ip netns add ${m1_ns}
ip netns add ${m2_ns}

modes="active-backup balance-tlb balance-alb"

for mode in $modes; do
	macvlan_over_bond "mode $mode"
done

exit $EXIT_STATUS
