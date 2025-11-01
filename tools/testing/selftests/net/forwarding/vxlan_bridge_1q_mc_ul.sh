#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------------------------+
# | + $h1.10             + $h1.20           |
# | | 192.0.2.1/28       | 2001:db8:1::1/64 |
# | \________   ________/                   |
# |          \ /                            |
# |           + $h1                H1 (vrf) |
# +-----------|-----------------------------+
#             |
# +-----------|----------------------------------------------------------------+
# | +---------|--------------------------------------+       SWITCH (main vrf) |
# | |         + $swp1                   BR1 (802.1q) |                         |
# | |            vid 10 20                           |                         |
# | |                                                |                         |
# | |  + vx10 (vxlan)         + vx20 (vxlan)         |      + lo10 (dummy)     |
# | |    local 192.0.2.100      local 2001:db8:4::1  |        192.0.2.100/28   |
# | |    group 233.252.0.1      group ff0e::1:2:3    |        2001:db8:4::1/64 |
# | |    id 1000                id 2000              |                         |
# | |    vid 10 pvid untagged   vid 20 pvid untagged |                         |
# | +------------------------------------------------+                         |
# |                                                                            |
# |   + $swp2                                                        $swp3 +   |
# |   | 192.0.2.33/28                                        192.0.2.65/28 |   |
# |   | 2001:db8:2::1/64                                  2001:db8:3::1/64 |   |
# |   |                                                                    |   |
# +---|--------------------------------------------------------------------|---+
#     |                                                                    |
# +---|--------------------------------+  +--------------------------------|---+
# |   |                      H2 (vrf)  |  | H3 (vrf)                       |   |
# | +-|----------------------------+   |  |  +-----------------------------|-+ |
# | | + $h2           BR2 (802.1d) |   |  |  | BR3 (802.1d)            $h3 + | |
# | |                              |   |  |  |                               | |
# | | + v1$h2 (veth)               |   |  |  |                v1$h3 (veth) + | |
# | +-|----------------------------+   |  |  +-----------------------------|-+ |
# |   |                                |  |                                |   |
# +---|--------------------------------+  +--------------------------------|---+
#     |                                                                    |
# +---|--------------------------------+  +--------------------------------|---+
# |   + v2$h2 (veth)       NS2 (netns) |  | NS3 (netns)       v2$h3 (veth) +   |
# |     192.0.2.34/28                  |  |                  192.0.2.66/28     |
# |     2001:db8:2::2/64               |  |               2001:db8:3::2/64     |
# |                                    |  |                                    |
# | +--------------------------------+ |  | +--------------------------------+ |
# | |                  BR1 (802.1q)  | |  | |                   BR1 (802.1q) | |
# | |  + vx10 (vxlan)                | |  | |  + vx10 (vxlan)                | |
# | |    local 192.0.2.34            | |  | |    local 192.0.2.50            | |
# | |    group 233.252.0.1 dev v2$h2 | |  | |    group 233.252.0.1 dev v2$h3 | |
# | |    id 1000 dstport $VXPORT     | |  | |    id 1000 dstport $VXPORT     | |
# | |    vid 10 pvid untagged        | |  | |    vid 10 pvid untagged        | |
# | |                                | |  | |                                | |
# | |  + vx20 (vxlan)                | |  | |  + vx20 (vxlan)                | |
# | |    local 2001:db8:2::2         | |  | |    local 2001:db8:3::2         | |
# | |    group ff0e::1:2:3 dev v2$h2 | |  | |    group ff0e::1:2:3 dev v2$h3 | |
# | |    id 2000 dstport $VXPORT     | |  | |    id 2000 dstport $VXPORT     | |
# | |    vid 20 pvid untagged        | |  | |    vid 20 pvid untagged        | |
# | |                                | |  | |                                | |
# | |  + w1 (veth)                   | |  | |  + w1 (veth)                   | |
# | |  | vid 10 20                   | |  | |  | vid 10 20                   | |
# | +--|-----------------------------+ |  | +--|-----------------------------+ |
# |    |                               |  |    |                               |
# | +--|-----------------------------+ |  | +--|-----------------------------+ |
# | |  + w2 (veth)        VW2 (vrf)  | |  | |  + w2 (veth)        VW2 (vrf)  | |
# | |  |\                            | |  | |  |\                            | |
# | |  | + w2.10                     | |  | |  | + w2.10                     | |
# | |  |   192.0.2.3/28              | |  | |  |   192.0.2.4/28              | |
# | |  |                             | |  | |  |                             | |
# | |  + w2.20                       | |  | |  + w2.20                       | |
# | |    2001:db8:1::3/64            | |  | |    2001:db8:1::4/64            | |
# | +--------------------------------+ |  | +--------------------------------+ |
# +------------------------------------+  +------------------------------------+
#
#shellcheck disable=SC2317 # SC doesn't see our uses of functions.

: "${VXPORT:=4789}"
export VXPORT

: "${GROUP4:=233.252.0.1}"
export GROUP4

: "${GROUP6:=ff0e::1:2:3}"
export GROUP6

: "${IPMR:=lo10}"

ALL_TESTS="
	ipv4_nomcroute
	ipv4_mcroute
	ipv4_mcroute_changelink
	ipv4_mcroute_starg
	ipv4_mcroute_noroute
	ipv4_mcroute_fdb
	ipv4_mcroute_fdb_oif0
	ipv4_mcroute_fdb_oif0_sep

	ipv6_nomcroute
	ipv6_mcroute
	ipv6_mcroute_changelink
	ipv6_mcroute_starg
	ipv6_mcroute_noroute
	ipv6_mcroute_fdb
	ipv6_mcroute_fdb_oif0

	ipv4_nomcroute_rx
	ipv4_mcroute_rx
	ipv4_mcroute_starg_rx
	ipv4_mcroute_fdb_oif0_sep_rx
	ipv4_mcroute_fdb_sep_rx

	ipv6_nomcroute_rx
	ipv6_mcroute_rx
	ipv6_mcroute_starg_rx
	ipv6_mcroute_fdb_sep_rx
"

NUM_NETIFS=6
source lib.sh

h1_create()
{
	adf_simple_if_init "$h1"

	adf_ip_link_add "$h1.10" master "v$h1" link "$h1" type vlan id 10
	adf_ip_link_set_up "$h1.10"
	adf_ip_addr_add "$h1.10" 192.0.2.1/28

	adf_ip_link_add "$h1.20" master "v$h1" link "$h1" type vlan id 20
	adf_ip_link_set_up "$h1.20"
	adf_ip_addr_add "$h1.20" 2001:db8:1::1/64
}

install_capture()
{
	local dev=$1; shift

	tc qdisc add dev "$dev" clsact
	defer tc qdisc del dev "$dev" clsact

	tc filter add dev "$dev" ingress proto ip pref 104 \
	   flower skip_hw ip_proto udp dst_port "$VXPORT" \
	   action pass
	defer tc filter del dev "$dev" ingress proto ip pref 104

	tc filter add dev "$dev" ingress proto ipv6 pref 106 \
	   flower skip_hw ip_proto udp dst_port "$VXPORT" \
	   action pass
	defer tc filter del dev "$dev" ingress proto ipv6 pref 106
}

h2_create()
{
	# $h2
	adf_ip_link_set_up "$h2"

	# H2
	vrf_create "v$h2"
	defer vrf_destroy "v$h2"

	adf_ip_link_set_up "v$h2"

	# br2
	adf_ip_link_add br2 type bridge vlan_filtering 0 mcast_snooping 0
	adf_ip_link_set_master br2 "v$h2"
	adf_ip_link_set_up br2

	# $h2
	adf_ip_link_set_master "$h2" br2
	install_capture "$h2"

	# v1$h2
	adf_ip_link_set_up "v1$h2"
	adf_ip_link_set_master "v1$h2" br2
}

h3_create()
{
	# $h3
	adf_ip_link_set_up "$h3"

	# H3
	vrf_create "v$h3"
	defer vrf_destroy "v$h3"

	adf_ip_link_set_up "v$h3"

	# br3
	adf_ip_link_add br3 type bridge vlan_filtering 0 mcast_snooping 0
	adf_ip_link_set_master br3 "v$h3"
	adf_ip_link_set_up br3

	# $h3
	adf_ip_link_set_master "$h3" br3
	install_capture "$h3"

	# v1$h3
	adf_ip_link_set_up "v1$h3"
	adf_ip_link_set_master "v1$h3" br3
}

switch_create()
{
	local swp1_mac

	# br1
	swp1_mac=$(mac_get "$swp1")
	adf_ip_link_add br1 type bridge vlan_filtering 1 \
			    vlan_default_pvid 0 mcast_snooping 0
	adf_ip_link_set_addr br1 "$swp1_mac"
	adf_ip_link_set_up br1

	# A dummy to force the IPv6 OIF=0 test to install a suitable MC route on
	# $IPMR to be deterministic. Also used for the IPv6 RX!=TX ping test.
	adf_ip_link_add "X$IPMR" up type dummy

	# IPMR
	adf_ip_link_add "$IPMR" up type dummy
	adf_ip_addr_add "$IPMR" 192.0.2.100/28
	adf_ip_addr_add "$IPMR" 2001:db8:4::1/64

	# $swp1
	adf_ip_link_set_up "$swp1"
	adf_ip_link_set_master "$swp1" br1
	adf_bridge_vlan_add vid 10 dev "$swp1"
	adf_bridge_vlan_add vid 20 dev "$swp1"

	# $swp2
	adf_ip_link_set_up "$swp2"
	adf_ip_addr_add "$swp2" 192.0.2.33/28
	adf_ip_addr_add "$swp2" 2001:db8:2::1/64

	# $swp3
	adf_ip_link_set_up "$swp3"
	adf_ip_addr_add "$swp3" 192.0.2.65/28
	adf_ip_addr_add "$swp3" 2001:db8:3::1/64
}

vx_create()
{
	local name=$1; shift
	local vid=$1; shift

	adf_ip_link_add "$name" up type vxlan dstport "$VXPORT" \
		nolearning noudpcsum tos inherit ttl 16 \
		"$@"
	adf_ip_link_set_master "$name" br1
	adf_bridge_vlan_add vid "$vid" dev "$name" pvid untagged
}
export -f vx_create

vx_wait()
{
	# Wait for all the ARP, IGMP etc. noise to settle down so that the
	# tunnel is clear for measurements.
	sleep 10
}

vx10_create()
{
	vx_create vx10 10 id 1000 "$@"
}
export -f vx10_create

vx20_create()
{
	vx_create vx20 20 id 2000 "$@"
}
export -f vx20_create

vx10_create_wait()
{
	vx10_create "$@"
	vx_wait
}

vx20_create_wait()
{
	vx20_create "$@"
	vx_wait
}

ns_init_common()
{
	local ns=$1; shift
	local if_in=$1; shift
	local ipv4_in=$1; shift
	local ipv6_in=$1; shift
	local ipv4_host=$1; shift
	local ipv6_host=$1; shift

	# v2$h2 / v2$h3
	adf_ip_link_set_up "$if_in"
	adf_ip_addr_add "$if_in" "$ipv4_in"
	adf_ip_addr_add "$if_in" "$ipv6_in"

	# br1
	adf_ip_link_add br1 type bridge vlan_filtering 1 \
		    vlan_default_pvid 0 mcast_snooping 0
	adf_ip_link_set_up br1

	# vx10, vx20
	vx10_create local "${ipv4_in%/*}" group "$GROUP4" dev "$if_in"
	vx20_create local "${ipv6_in%/*}" group "$GROUP6" dev "$if_in"

	# w1
	adf_ip_link_add w1 type veth peer name w2
	adf_ip_link_set_master w1 br1
	adf_ip_link_set_up w1
	adf_bridge_vlan_add vid 10 dev w1
	adf_bridge_vlan_add vid 20 dev w1

	# w2
	adf_simple_if_init w2

	# w2.10
	adf_ip_link_add w2.10 master vw2 link w2 type vlan id 10
	adf_ip_link_set_up w2.10
	adf_ip_addr_add w2.10 "$ipv4_host"

	# w2.20
	adf_ip_link_add w2.20 master vw2 link w2 type vlan id 20
	adf_ip_link_set_up w2.20
	adf_ip_addr_add w2.20 "$ipv6_host"
}
export -f ns_init_common

ns2_create()
{
	# NS2
	ip netns add ns2
	defer ip netns del ns2

	# v2$h2
	ip link set dev "v2$h2" netns ns2
	defer ip -n ns2 link set dev "v2$h2" netns 1

	in_ns ns2 \
	      ns_init_common ns2 "v2$h2" \
			     192.0.2.34/28 2001:db8:2::2/64 \
			     192.0.2.3/28  2001:db8:1::3/64
}

ns3_create()
{
	# NS3
	ip netns add ns3
	defer ip netns del ns3

	# v2$h3
	ip link set dev "v2$h3" netns ns3
	defer ip -n ns3 link set dev "v2$h3" netns 1

	ip -n ns3 link set dev "v2$h3" up

	in_ns ns3 \
	      ns_init_common ns3 "v2$h3" \
			     192.0.2.66/28 2001:db8:3::2/64 \
			     192.0.2.4/28  2001:db8:1::4/64
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

	adf_ip_link_add "v1$h2" type veth peer name "v2$h2"
	adf_ip_link_add "v1$h3" type veth peer name "v2$h3"

	h1_create
	h2_create
	h3_create
	switch_create
	ns2_create
	ns3_create
}

adf_install_broken_sg()
{
	adf_mcd_start "$IPMR" || exit "$EXIT_STATUS"

	mc_cli add "$swp2" 192.0.2.100 "$GROUP4" "$swp1" "$swp3"
	defer mc_cli remove "$swp2" 192.0.2.100 "$GROUP4" "$swp1" "$swp3"

	mc_cli add "$swp2" 2001:db8:4::1 "$GROUP6" "$swp1" "$swp3"
	defer mc_cli remove "$swp2" 2001:db8:4::1 "$GROUP6" "$swp1" "$swp3"
}

adf_install_rx()
{
	mc_cli add "$swp2" 0.0.0.0 "$GROUP4" "$IPMR"
	defer mc_cli remove "$swp2" 0.0.0.0 "$GROUP4" lo10

	mc_cli add "$swp3" 0.0.0.0 "$GROUP4" "$IPMR"
	defer mc_cli remove "$swp3" 0.0.0.0 "$GROUP4" lo10

	mc_cli add "$swp2" :: "$GROUP6" "$IPMR"
	defer mc_cli remove "$swp2" :: "$GROUP6" lo10

	mc_cli add "$swp3" :: "$GROUP6" "$IPMR"
	defer mc_cli remove "$swp3" :: "$GROUP6" lo10
}

adf_install_sg()
{
	adf_mcd_start "$IPMR" || exit "$EXIT_STATUS"

	mc_cli add "$IPMR" 192.0.2.100 "$GROUP4" "$swp2" "$swp3"
	defer mc_cli remove "$IPMR" 192.0.2.33 "$GROUP4" "$swp2" "$swp3"

	mc_cli add "$IPMR" 2001:db8:4::1 "$GROUP6" "$swp2" "$swp3"
	defer mc_cli remove "$IPMR" 2001:db8:4::1 "$GROUP6" "$swp2" "$swp3"

	adf_install_rx
}

adf_install_sg_sep()
{
	adf_mcd_start lo || exit "$EXIT_STATUS"

	mc_cli add lo 192.0.2.120 "$GROUP4" "$swp2" "$swp3"
	defer mc_cli remove lo 192.0.2.120 "$GROUP4" "$swp2" "$swp3"

	mc_cli add lo 2001:db8:5::1 "$GROUP6" "$swp2" "$swp3"
	defer mc_cli remove lo 2001:db8:5::1 "$GROUP6" "$swp2" "$swp3"
}

adf_install_sg_sep_rx()
{
	local lo=$1; shift

	adf_mcd_start "$IPMR" "$lo" || exit "$EXIT_STATUS"

	mc_cli add "$lo" 192.0.2.120 "$GROUP4" "$swp2" "$swp3"
	defer mc_cli remove "$lo" 192.0.2.120 "$GROUP4" "$swp2" "$swp3"

	mc_cli add "$lo" 2001:db8:5::1 "$GROUP6" "$swp2" "$swp3"
	defer mc_cli remove "$lo" 2001:db8:5::1 "$GROUP6" "$swp2" "$swp3"

	adf_install_rx
}

adf_install_starg()
{
	adf_mcd_start "$IPMR" || exit "$EXIT_STATUS"

	mc_cli add "$IPMR" 0.0.0.0 "$GROUP4" "$swp2" "$swp3"
	defer mc_cli remove "$IPMR" 0.0.0.0 "$GROUP4" "$swp2" "$swp3"

	mc_cli add "$IPMR" :: "$GROUP6" "$swp2" "$swp3"
	defer mc_cli remove "$IPMR" :: "$GROUP6" "$swp2" "$swp3"

	adf_install_rx
}

do_packets_v4()
{
	local mac

	mac=$(mac_get "$h2")
	"$MZ" "$h1" -Q 10 -c 10 -d 100msec -p 64 -a own -b "$mac" \
	    -A 192.0.2.1 -B 192.0.2.2 -t udp sp=1234,dp=2345 -q
}

do_packets_v6()
{
	local mac

	mac=$(mac_get "$h2")
	"$MZ" -6 "$h1" -Q 20 -c 10 -d 100msec -p 64 -a own -b "$mac" \
	    -A 2001:db8:1::1 -B 2001:db8:1::2 -t udp sp=1234,dp=2345 -q
}

do_test()
{
	local ipv=$1; shift
	local expect_h2=$1; shift
	local expect_h3=$1; shift
	local what=$1; shift

	local pref=$((100 + ipv))
	local t0_h2
	local t0_h3
	local t1_h2
	local t1_h3
	local d_h2
	local d_h3

	RET=0

	t0_h2=$(tc_rule_stats_get "$h2" "$pref" ingress)
	t0_h3=$(tc_rule_stats_get "$h3" "$pref" ingress)

	"do_packets_v$ipv"
	sleep 1

	t1_h2=$(tc_rule_stats_get "$h2" "$pref" ingress)
	t1_h3=$(tc_rule_stats_get "$h3" "$pref" ingress)

	d_h2=$((t1_h2 - t0_h2))
	d_h3=$((t1_h3 - t0_h3))

	((d_h2 == expect_h2))
	check_err $? "Expected $expect_h2 packets on H2, got $d_h2"

	((d_h3 == expect_h3))
	check_err $? "Expected $expect_h3 packets on H3, got $d_h3"

	log_test "VXLAN MC flood $what"
}

ipv4_do_test_rx()
{
	local h3_should_fail=$1; shift
	local what=$1; shift

	RET=0

	ping_do "$h1.10" 192.0.2.3
	check_err $? "H2 should respond"

	ping_do "$h1.10" 192.0.2.4
	check_err_fail "$h3_should_fail" $? "H3 responds"

	log_test "VXLAN MC flood $what"
}

ipv6_do_test_rx()
{
	local h3_should_fail=$1; shift
	local what=$1; shift

	RET=0

	ping6_do "$h1.20" 2001:db8:1::3
	check_err $? "H2 should respond"

	ping6_do "$h1.20" 2001:db8:1::4
	check_err_fail "$h3_should_fail" $? "H3 responds"

	log_test "VXLAN MC flood $what"
}

ipv4_nomcroute()
{
	# Install a misleading (S,G) rule to attempt to trick the system into
	# pushing the packets elsewhere.
	adf_install_broken_sg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$swp2"
	do_test 4 10 0 "IPv4 nomcroute"
}

ipv6_nomcroute()
{
	# Like for IPv4, install a misleading (S,G).
	adf_install_broken_sg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$swp2"
	do_test 6 10 0 "IPv6 nomcroute"
}

ipv4_nomcroute_rx()
{
	vx10_create local 192.0.2.100 group "$GROUP4" dev "$swp2"
	ipv4_do_test_rx 1 "IPv4 nomcroute ping"
}

ipv6_nomcroute_rx()
{
	vx20_create local 2001:db8:4::1 group "$GROUP6" dev "$swp2"
	ipv6_do_test_rx 1 "IPv6 nomcroute ping"
}

ipv4_mcroute()
{
	adf_install_sg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR" mcroute
	do_test 4 10 10 "IPv4 mcroute"
}

ipv6_mcroute()
{
	adf_install_sg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	do_test 6 10 10 "IPv6 mcroute"
}

ipv4_mcroute_rx()
{
	adf_install_sg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR" mcroute
	ipv4_do_test_rx 0 "IPv4 mcroute ping"
}

ipv6_mcroute_rx()
{
	adf_install_sg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	ipv6_do_test_rx 0 "IPv6 mcroute ping"
}

ipv4_mcroute_changelink()
{
	adf_install_sg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR"
	ip link set dev vx10 type vxlan mcroute
	sleep 1
	do_test 4 10 10 "IPv4 mcroute changelink"
}

ipv6_mcroute_changelink()
{
	adf_install_sg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	ip link set dev vx20 type vxlan mcroute
	sleep 1
	do_test 6 10 10 "IPv6 mcroute changelink"
}

ipv4_mcroute_starg()
{
	adf_install_starg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR" mcroute
	do_test 4 10 10 "IPv4 mcroute (*,G)"
}

ipv6_mcroute_starg()
{
	adf_install_starg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	do_test 6 10 10 "IPv6 mcroute (*,G)"
}

ipv4_mcroute_starg_rx()
{
	adf_install_starg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR" mcroute
	ipv4_do_test_rx 0 "IPv4 mcroute (*,G) ping"
}

ipv6_mcroute_starg_rx()
{
	adf_install_starg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	ipv6_do_test_rx 0 "IPv6 mcroute (*,G) ping"
}

ipv4_mcroute_noroute()
{
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR" mcroute
	do_test 4 0 0 "IPv4 mcroute, no route"
}

ipv6_mcroute_noroute()
{
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	do_test 6 0 0 "IPv6 mcroute, no route"
}

ipv4_mcroute_fdb()
{
	adf_install_sg
	vx10_create_wait local 192.0.2.100 dev "$IPMR" mcroute
	bridge fdb add dev vx10 \
		00:00:00:00:00:00 self static dst "$GROUP4" via "$IPMR"
	do_test 4 10 10 "IPv4 mcroute FDB"
}

ipv6_mcroute_fdb()
{
	adf_install_sg
	vx20_create_wait local 2001:db8:4::1 dev "$IPMR" mcroute
	bridge -6 fdb add dev vx20 \
		00:00:00:00:00:00 self static dst "$GROUP6" via "$IPMR"
	do_test 6 10 10 "IPv6 mcroute FDB"
}

# Use FDB to configure VXLAN in a way where oif=0 for purposes of FIB lookup.
ipv4_mcroute_fdb_oif0()
{
	adf_install_sg
	vx10_create_wait local 192.0.2.100 group "$GROUP4" dev "$IPMR" mcroute
	bridge fdb del dev vx10 00:00:00:00:00:00
	bridge fdb add dev vx10 00:00:00:00:00:00 self static dst "$GROUP4"
	do_test 4 10 10 "IPv4 mcroute oif=0"
}

ipv6_mcroute_fdb_oif0()
{
	# The IPv6 tunnel lookup does not fall back to selection by source
	# address. Instead it just does a FIB match, and that would find one of
	# the several ff00::/8 multicast routes -- each device has one. In order
	# to reliably force the $IPMR device, add a /128 route for the
	# destination group address.
	ip -6 route add table local multicast "$GROUP6/128" dev "$IPMR"
	defer ip -6 route del table local multicast "$GROUP6/128" dev "$IPMR"

	adf_install_sg
	vx20_create_wait local 2001:db8:4::1 group "$GROUP6" dev "$IPMR" mcroute
	bridge -6 fdb del dev vx20 00:00:00:00:00:00
	bridge -6 fdb add dev vx20 00:00:00:00:00:00 self static dst "$GROUP6"
	do_test 6 10 10 "IPv6 mcroute oif=0"
}

# In oif=0 test as above, have FIB lookup resolve to loopback instead of IPMR.
# This doesn't work with IPv6 -- a MC route on lo would be marked as RTF_REJECT.
ipv4_mcroute_fdb_oif0_sep()
{
	adf_install_sg_sep

	adf_ip_addr_add lo 192.0.2.120/28
	vx10_create_wait local 192.0.2.120 group "$GROUP4" dev "$IPMR" mcroute
	bridge fdb del dev vx10 00:00:00:00:00:00
	bridge fdb add dev vx10 00:00:00:00:00:00 self static dst "$GROUP4"
	do_test 4 10 10 "IPv4 mcroute TX!=RX oif=0"
}

ipv4_mcroute_fdb_oif0_sep_rx()
{
	adf_install_sg_sep_rx lo

	adf_ip_addr_add lo 192.0.2.120/28
	vx10_create_wait local 192.0.2.120 group "$GROUP4" dev "$IPMR" mcroute
	bridge fdb del dev vx10 00:00:00:00:00:00
	bridge fdb add dev vx10 00:00:00:00:00:00 self static dst "$GROUP4"
	ipv4_do_test_rx 0 "IPv4 mcroute TX!=RX oif=0 ping"
}

ipv4_mcroute_fdb_sep_rx()
{
	adf_install_sg_sep_rx lo

	adf_ip_addr_add lo 192.0.2.120/28
	vx10_create_wait local 192.0.2.120 group "$GROUP4" dev "$IPMR" mcroute
	bridge fdb del dev vx10 00:00:00:00:00:00
	bridge fdb add \
	       dev vx10 00:00:00:00:00:00 self static dst "$GROUP4" via lo
	ipv4_do_test_rx 0 "IPv4 mcroute TX!=RX ping"
}

ipv6_mcroute_fdb_sep_rx()
{
	adf_install_sg_sep_rx "X$IPMR"

	adf_ip_addr_add "X$IPMR" 2001:db8:5::1/64
	vx20_create_wait local 2001:db8:5::1 group "$GROUP6" dev "$IPMR" mcroute
	bridge -6 fdb del dev vx20 00:00:00:00:00:00
	bridge -6 fdb add dev vx20 00:00:00:00:00:00 \
			  self static dst "$GROUP6" via "X$IPMR"
	ipv6_do_test_rx 0 "IPv6 mcroute TX!=RX ping"
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit "$EXIT_STATUS"
