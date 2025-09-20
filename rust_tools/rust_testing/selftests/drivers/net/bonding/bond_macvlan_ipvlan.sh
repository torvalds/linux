#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test macvlan/ipvlan over bond

lib_dir=$(dirname "$0")
source ${lib_dir}/bond_topo_2d1c.sh

xvlan1_ns="xvlan1-$(mktemp -u XXXXXX)"
xvlan2_ns="xvlan2-$(mktemp -u XXXXXX)"
xvlan1_ip4="192.0.2.11"
xvlan1_ip6="2001:db8::11"
xvlan2_ip4="192.0.2.12"
xvlan2_ip6="2001:db8::12"

cleanup()
{
	client_destroy
	server_destroy
	gateway_destroy

	ip netns del ${xvlan1_ns}
	ip netns del ${xvlan2_ns}
}

check_connection()
{
	local ns=${1}
	local target=${2}
	local message=${3}
	RET=0

	ip netns exec ${ns} ping ${target} -c 4 -i 0.1 &>/dev/null
	check_err $? "ping failed"
	log_test "${bond_mode}/${xvlan_type}_${xvlan_mode}: ${message}"
}

xvlan_over_bond()
{
	local param="$1"
	local xvlan_type="$2"
	local xvlan_mode="$3"
	RET=0

	# setup new bond mode
	bond_reset "${param}"

	ip -n ${s_ns} link add link bond0 name ${xvlan_type}0 type ${xvlan_type} mode ${xvlan_mode}
	ip -n ${s_ns} link set ${xvlan_type}0 netns ${xvlan1_ns}
	ip -n ${xvlan1_ns} link set dev ${xvlan_type}0 up
	ip -n ${xvlan1_ns} addr add ${xvlan1_ip4}/24 dev ${xvlan_type}0
	ip -n ${xvlan1_ns} addr add ${xvlan1_ip6}/24 dev ${xvlan_type}0

	ip -n ${s_ns} link add link bond0 name ${xvlan_type}0 type ${xvlan_type} mode ${xvlan_mode}
	ip -n ${s_ns} link set ${xvlan_type}0 netns ${xvlan2_ns}
	ip -n ${xvlan2_ns} link set dev ${xvlan_type}0 up
	ip -n ${xvlan2_ns} addr add ${xvlan2_ip4}/24 dev ${xvlan_type}0
	ip -n ${xvlan2_ns} addr add ${xvlan2_ip6}/24 dev ${xvlan_type}0

	sleep 2

	check_connection "${c_ns}" "${s_ip4}" "IPv4: client->server"
	check_connection "${c_ns}" "${s_ip6}" "IPv6: client->server"
	check_connection "${c_ns}" "${xvlan1_ip4}" "IPv4: client->${xvlan_type}_1"
	check_connection "${c_ns}" "${xvlan1_ip6}" "IPv6: client->${xvlan_type}_1"
	check_connection "${c_ns}" "${xvlan2_ip4}" "IPv4: client->${xvlan_type}_2"
	check_connection "${c_ns}" "${xvlan2_ip6}" "IPv6: client->${xvlan_type}_2"
	check_connection "${xvlan1_ns}" "${xvlan2_ip4}" "IPv4: ${xvlan_type}_1->${xvlan_type}_2"
	check_connection "${xvlan1_ns}" "${xvlan2_ip6}" "IPv6: ${xvlan_type}_1->${xvlan_type}_2"

	check_connection "${s_ns}" "${c_ip4}" "IPv4: server->client"
	check_connection "${s_ns}" "${c_ip6}" "IPv6: server->client"
	check_connection "${xvlan1_ns}" "${c_ip4}" "IPv4: ${xvlan_type}_1->client"
	check_connection "${xvlan1_ns}" "${c_ip6}" "IPv6: ${xvlan_type}_1->client"
	check_connection "${xvlan2_ns}" "${c_ip4}" "IPv4: ${xvlan_type}_2->client"
	check_connection "${xvlan2_ns}" "${c_ip6}" "IPv6: ${xvlan_type}_2->client"
	check_connection "${xvlan2_ns}" "${xvlan1_ip4}" "IPv4: ${xvlan_type}_2->${xvlan_type}_1"
	check_connection "${xvlan2_ns}" "${xvlan1_ip6}" "IPv6: ${xvlan_type}_2->${xvlan_type}_1"

	ip -n ${c_ns} neigh flush dev eth0
}

trap cleanup EXIT

setup_prepare
ip netns add ${xvlan1_ns}
ip netns add ${xvlan2_ns}

bond_modes="active-backup balance-tlb balance-alb"

for bond_mode in ${bond_modes}; do
	xvlan_over_bond "mode ${bond_mode}" macvlan bridge
	xvlan_over_bond "mode ${bond_mode}" ipvlan  l2
done

exit $EXIT_STATUS
