#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+ +-----------------------+ +-----------------------+
# | H1 (vrf)              | | H2 (vrf)              | | H3 (vrf)              |
# |    + $h1              | |    + $h2              | |    + $h3              |
# |    | 192.0.2.1/28     | |    | 192.0.2.2/28     | |    | 192.0.2.18/28    |
# |    | 2001:db8:1::1/64 | |    | 2001:db8:1::2/64 | |    | 2001:db8:2::2/64 |
# |    |                  | |    |                  | |    |                  |
# +----|------------------+ +----|------------------+ +----|------------------+
#      |                         |                         |
# +----|-------------------------|-------------------------|------------------+
# | +--|-------------------------|------------------+      |                  |
# | |  + $swp1                   + $swp2            |      + $swp3            |
# | |                                               |        192.0.2.17/28    |
# | |  BR1 (802.1q)                                 |        2001:db8:2::1/64 |
# | |  192.0.2.3/28                                 |                         |
# | |  2001:db8:1::3/64                             |                         |
# | +-----------------------------------------------+                      SW |
# +---------------------------------------------------------------------------+
#
#shellcheck disable=SC2317 # SC doesn't see our uses of functions.
#shellcheck disable=SC2034 # ... and global variables

ALL_TESTS="
	test_d_no_sharing
	test_d_sharing
	test_q_no_sharing
	test_q_sharing
	test_addr_set
"

NUM_NETIFS=6
source lib.sh

pMAC=00:11:22:33:44:55
bMAC=00:11:22:33:44:66
mMAC=00:11:22:33:44:77
xMAC=00:11:22:33:44:88

host_create()
{
	local h=$1; shift
	local ipv4=$1; shift
	local ipv6=$1; shift

	adf_simple_if_init "$h" "$ipv4" "$ipv6"
	adf_ip_route_add vrf "v$h" 192.0.2.16/28 nexthop via 192.0.2.3
	adf_ip_route_add vrf "v$h" 2001:db8:2::/64 nexthop via 2001:db8:1::3
}

h3_create()
{
	adf_simple_if_init "$h3" 192.0.2.18/28 2001:db8:2::2/64
	adf_ip_route_add vrf "v$h3" 192.0.2.0/28 nexthop via 192.0.2.17
	adf_ip_route_add vrf "v$h3" 2001:db8:1::/64 nexthop via 2001:db8:2::1

	tc qdisc add dev "$h3" clsact
	defer tc qdisc del dev "$h3" clsact

	tc filter add dev "$h3" ingress proto ip pref 104 \
	   flower skip_hw ip_proto udp dst_port 4096 \
	   action pass
	defer tc filter del dev "$h3" ingress proto ip pref 104

	tc qdisc add dev "$h2" clsact
	defer tc qdisc del dev "$h2" clsact

	tc filter add dev "$h2" ingress proto ip pref 104 \
	   flower skip_hw ip_proto udp dst_port 4096 \
	   action pass
	defer tc filter del dev "$h2" ingress proto ip pref 104
}

switch_create()
{
	adf_ip_link_set_up "$swp1"

	adf_ip_link_set_up "$swp2"

	adf_ip_addr_add "$swp3" 192.0.2.17/28
	adf_ip_addr_add "$swp3" 2001:db8:2::1/64
	adf_ip_link_set_up "$swp3"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	adf_vrf_prepare
	adf_forwarding_enable

	host_create "$h1" 192.0.2.1/28 2001:db8:1::1/64
	host_create "$h2" 192.0.2.2/28 2001:db8:1::2/64
	h3_create

	switch_create
}

adf_bridge_configure()
{
	local dev

	adf_ip_addr_add br 192.0.2.3/28
	adf_ip_addr_add br 2001:db8:1::3/64

	adf_bridge_vlan_add dev br vid 1 pvid untagged self
	adf_bridge_vlan_add dev br vid 2 self
	adf_bridge_vlan_add dev br vid 3 self

	for dev in "$swp1" "$swp2"; do
		adf_ip_link_set_master "$dev" br
		adf_bridge_vlan_add dev "$dev" vid 1 pvid untagged
		adf_bridge_vlan_add dev "$dev" vid 2
		adf_bridge_vlan_add dev "$dev" vid 3
	done
}

adf_bridge_create()
{
	local mac

	adf_ip_link_add br up type bridge vlan_default_pvid 0 "$@"
	mac=$(mac_get br)
	adf_bridge_configure
	adf_ip_link_set_addr br "$mac"
}

check_fdb_local_vlan_0_support()
{
	if adf_ip_link_add XXbr up type bridge vlan_filtering 1 \
			fdb_local_vlan_0 1 &>/dev/null; then
		return 0
	fi

	log_test_skip "FDB sharing" \
		      "iproute 2 or the kernel do not support fdb_local_vlan_0"
}

check_mac_presence()
{
	local should_fail=$1; shift
	local dev=$1; shift
	local vlan=$1; shift
	local mac

	mac=$(mac_get "$dev")

	if ((vlan == 0)); then
		vlan=null
	fi

	bridge -j fdb show dev "$dev" |
	    jq -e --arg mac "$mac" --argjson vlan "$vlan" \
	       '.[] | select(.mac == $mac) | select(.vlan == $vlan)' > /dev/null
	check_err_fail "$should_fail" $? "FDB dev $dev vid $vlan addr $mac exists"
}

do_sharing_test()
{
	local should_fail=$1; shift
	local what=$1; shift
	local dev

	RET=0

	for dev in "$swp1" "$swp2" br; do
		check_mac_presence 0 "$dev" 0
		check_mac_presence "$should_fail" "$dev" 1
		check_mac_presence "$should_fail" "$dev" 2
		check_mac_presence "$should_fail" "$dev" 3
	done

	log_test "$what"
}

do_end_to_end_test()
{
	local mac=$1; shift
	local what=$1; shift
	local probe_dev=${1-$h3}; shift
	local expect=${1-10}; shift

	local t0
	local t1
	local dd

	RET=0

	# In mausezahn, use $dev MAC as the destination MAC. In the MAC sharing
	# context, that will cause an FDB miss on VLAN 1 and prompt a second
	# lookup in VLAN 0.

	t0=$(tc_rule_stats_get "$probe_dev" 104 ingress)

	$MZ "$h1" -c 10 -p 64 -a own -b "$mac" \
		  -A 192.0.2.1 -B 192.0.2.18 -t udp "dp=4096,sp=2048" -q
	sleep 1

	t1=$(tc_rule_stats_get "$probe_dev" 104 ingress)
	dd=$((t1 - t0))

	((dd == expect))
	check_err $? "Expected $expect packets on $probe_dev got $dd"

	log_test "$what"
}

do_tests()
{
	local should_fail=$1; shift
	local what=$1; shift
	local swp1_mac
	local br_mac

	swp1_mac=$(mac_get "$swp1")
	br_mac=$(mac_get br)

	do_sharing_test "$should_fail" "$what"
	do_end_to_end_test "$swp1_mac" "$what: end to end, $swp1 MAC"
	do_end_to_end_test "$br_mac" "$what: end to end, br MAC"
}

bridge_standard()
{
	local vlan_filtering=$1; shift

	if ((vlan_filtering)); then
		echo 802.1q
	else
		echo 802.1d
	fi
}

nonexistent_fdb_test()
{
	local vlan_filtering=$1; shift
	local standard

	standard=$(bridge_standard "$vlan_filtering")

	# We expect flooding, so $h2 should get the traffic.
	do_end_to_end_test "$xMAC" "$standard: Nonexistent FDB" "$h2"
}

misleading_fdb_test()
{
	local vlan_filtering=$1; shift
	local standard

	standard=$(bridge_standard "$vlan_filtering")

	defer_scope_push
		# Add an FDB entry on VLAN 0. The lookup on VLAN-aware bridge
		# shouldn't pick this up even with fdb_local_vlan_0 enabled, so
		# the traffic should be flooded. This all holds on
		# vlan_filtering bridge, on non-vlan_filtering one the FDB entry
		# is expected to be found as usual, no flooding takes place.
		#
		# Adding only on VLAN 0 is a bit tricky, because bridge is
		# trying to be nice and interprets the request as if the FDB
		# should be added on each VLAN.

		bridge fdb add "$mMAC" dev "$swp1" master
		bridge fdb del "$mMAC" dev "$swp1" vlan 1 master
		bridge fdb del "$mMAC" dev "$swp1" vlan 2 master
		bridge fdb del "$mMAC" dev "$swp1" vlan 3 master

		local expect=$((vlan_filtering ? 10 : 0))
		do_end_to_end_test "$mMAC" \
				   "$standard: Lookup of non-local MAC on VLAN 0" \
				   "$h2" "$expect"
	defer_scope_pop
}

change_mac()
{
	local dev=$1; shift
	local mac=$1; shift
	local cur_mac

	cur_mac=$(mac_get "$dev")

	log_info "Change $dev MAC $cur_mac -> $mac"
	adf_ip_link_set_addr "$dev" "$mac"
	defer log_info "Change $dev MAC back"
}

do_test_no_sharing()
{
	local vlan_filtering=$1; shift
	local standard

	standard=$(bridge_standard "$vlan_filtering")

	adf_bridge_create vlan_filtering "$vlan_filtering"
	setup_wait

	do_tests 0 "$standard, no FDB sharing"

	change_mac "$swp1" "$pMAC"
	change_mac br "$bMAC"

	do_tests 0 "$standard, no FDB sharing after MAC change"

	in_defer_scope check_fdb_local_vlan_0_support || return

	log_info "Set fdb_local_vlan_0=1"
	ip link set dev br type bridge fdb_local_vlan_0 1

	do_tests 1 "$standard, fdb sharing after toggle"
}

do_test_sharing()
{
	local vlan_filtering=$1; shift
	local standard

	standard=$(bridge_standard "$vlan_filtering")

	in_defer_scope check_fdb_local_vlan_0_support || return

	adf_bridge_create vlan_filtering "$vlan_filtering" fdb_local_vlan_0 1
	setup_wait

	do_tests 1 "$standard, FDB sharing"

	nonexistent_fdb_test "$vlan_filtering"
	misleading_fdb_test "$vlan_filtering"

	change_mac "$swp1" "$pMAC"
	change_mac br "$bMAC"

	do_tests 1 "$standard, FDB sharing after MAC change"

	log_info "Set fdb_local_vlan_0=0"
	ip link set dev br type bridge fdb_local_vlan_0 0

	do_tests 0 "$standard, No FDB sharing after toggle"
}

test_d_no_sharing()
{
	do_test_no_sharing 0
}

test_d_sharing()
{
	do_test_sharing 0
}

test_q_no_sharing()
{
	do_test_no_sharing 1
}

test_q_sharing()
{
	do_test_sharing 1
}

adf_addr_set_bridge_create()
{
	adf_ip_link_add br up type bridge vlan_filtering 0
	adf_ip_link_set_addr br "$(mac_get br)"
	adf_bridge_configure
}

test_addr_set()
{
	adf_addr_set_bridge_create
	setup_wait

	do_end_to_end_test "$(mac_get br)" "NET_ADDR_SET: end to end, br MAC"
}

trap cleanup EXIT

setup_prepare
tests_run
