#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test traffic distribution when there are multiple routes between an IPv4
# GRE tunnel. The tunnel carries IPv6 traffic between multiple hosts.
# Multiple routes are in the underlay network. With the default multipath
# policy, SW2 will only look at the outer IP addresses, hence only a single
# route would be used.
#
# +-------------------------+
# | H1                      |
# |               $h1 +     |
# |  2001:db8:1::2/64 |     |
# +-------------------|-----+
#                     |
# +-------------------|------------------------+
# | SW1               |                        |
# |              $ol1 +                        |
# |  2001:db8:1::1/64                          |
# |                                            |
# |  + g1 (gre)                                |
# |    loc=192.0.2.65                          |
# |    rem=192.0.2.66 --.                      |
# |    tos=inherit      |                      |
# |                     v                      |
# |                     + $ul1                 |
# |                     | 192.0.2.129/28       |
# +---------------------|----------------------+
#                       |
# +---------------------|----------------------+
# | SW2                 |                      |
# |               $ul21 +                      |
# |      192.0.2.130/28                        |
# |                   |                        |
# !   ________________|_____                   |
# |  /                      \                  |
# |  |                      |                  |
# |  + $ul22.111 (vlan)     + $ul22.222 (vlan) |
# |  | 192.0.2.145/28       | 192.0.2.161/28   |
# |  |                      |                  |
# +--|----------------------|------------------+
#    |                      |
# +--|----------------------|------------------+
# |  |                      |                  |
# |  + $ul32.111 (vlan)     + $ul32.222 (vlan) |
# |  | 192.0.2.146/28       | 192.0.2.162/28   |
# |  |                      |                  |
# |  \______________________/                  |
# |                   |                        |
# |                   |                        |
# |               $ul31 +                      |
# |      192.0.2.177/28 |                  SW3 |
# +---------------------|----------------------+
#                       |
# +---------------------|----------------------+
# |                     + $ul4                 |
# |                     ^ 192.0.2.178/28       |
# |                     |                      |
# |  + g2 (gre)         |                      |
# |    loc=192.0.2.66   |                      |
# |    rem=192.0.2.65 --'                      |
# |    tos=inherit                             |
# |                                            |
# |               $ol4 +                       |
# |   2001:db8:2::1/64 |                   SW4 |
# +--------------------|-----------------------+
#                      |
# +--------------------|---------+
# |                    |         |
# |                $h2 +         |
# |   2001:db8:2::2/64        H2 |
# +------------------------------+

ALL_TESTS="
	ping_ipv6
	multipath_ipv6
"

NUM_NETIFS=10
source lib.sh

h1_create()
{
	simple_if_init $h1 2001:db8:1::2/64
	ip -6 route add vrf v$h1 2001:db8:2::/64 via 2001:db8:1::1
}

h1_destroy()
{
	ip -6 route del vrf v$h1 2001:db8:2::/64 via 2001:db8:1::1
	simple_if_fini $h1 2001:db8:1::2/64
}

sw1_create()
{
	simple_if_init $ol1 2001:db8:1::1/64
	__simple_if_init $ul1 v$ol1 192.0.2.129/28

	tunnel_create g1 gre 192.0.2.65 192.0.2.66 tos inherit dev v$ol1
	__simple_if_init g1 v$ol1 192.0.2.65/32
	ip route add vrf v$ol1 192.0.2.66/32 via 192.0.2.130

	ip -6 route add vrf v$ol1 2001:db8:2::/64 dev g1
}

sw1_destroy()
{
	ip -6 route del vrf v$ol1 2001:db8:2::/64

	ip route del vrf v$ol1 192.0.2.66/32
	__simple_if_fini g1 192.0.2.65/32
	tunnel_destroy g1

	__simple_if_fini $ul1 192.0.2.129/28
	simple_if_fini $ol1 2001:db8:1::1/64
}

sw2_create()
{
	simple_if_init $ul21 192.0.2.130/28
	__simple_if_init $ul22 v$ul21
	vlan_create $ul22 111 v$ul21 192.0.2.145/28
	vlan_create $ul22 222 v$ul21 192.0.2.161/28

	ip route add vrf v$ul21 192.0.2.65/32 via 192.0.2.129
	ip route add vrf v$ul21 192.0.2.66/32 \
	   nexthop via 192.0.2.146 \
	   nexthop via 192.0.2.162
}

sw2_destroy()
{
	ip route del vrf v$ul21 192.0.2.66/32
	ip route del vrf v$ul21 192.0.2.65/32

	vlan_destroy $ul22 222
	vlan_destroy $ul22 111
	__simple_if_fini $ul22
	simple_if_fini $ul21 192.0.2.130/28
}

sw3_create()
{
	simple_if_init $ul31 192.0.2.177/28
	__simple_if_init $ul32 v$ul31
	vlan_create $ul32 111 v$ul31 192.0.2.146/28
	vlan_create $ul32 222 v$ul31 192.0.2.162/28

	ip route add vrf v$ul31 192.0.2.66/32 via 192.0.2.178
	ip route add vrf v$ul31 192.0.2.65/32 \
	   nexthop via 192.0.2.145 \
	   nexthop via 192.0.2.161

	tc qdisc add dev $ul32 clsact
	tc filter add dev $ul32 ingress pref 111 prot 802.1Q \
	   flower vlan_id 111 action pass
	tc filter add dev $ul32 ingress pref 222 prot 802.1Q \
	   flower vlan_id 222 action pass
}

sw3_destroy()
{
	tc qdisc del dev $ul32 clsact

	ip route del vrf v$ul31 192.0.2.65/32
	ip route del vrf v$ul31 192.0.2.66/32

	vlan_destroy $ul32 222
	vlan_destroy $ul32 111
	__simple_if_fini $ul32
	simple_if_fini $ul31 192.0.2.177/28
}

sw4_create()
{
	simple_if_init $ol4 2001:db8:2::1/64
	__simple_if_init $ul4 v$ol4 192.0.2.178/28

	tunnel_create g2 gre 192.0.2.66 192.0.2.65 tos inherit dev v$ol4
	__simple_if_init g2 v$ol4 192.0.2.66/32
	ip route add vrf v$ol4 192.0.2.65/32 via 192.0.2.177

	ip -6 route add vrf v$ol4 2001:db8:1::/64 dev g2
}

sw4_destroy()
{
	ip -6 route del vrf v$ol4 2001:db8:1::/64

	ip route del vrf v$ol4 192.0.2.65/32
	__simple_if_fini g2 192.0.2.66/32
	tunnel_destroy g2

	__simple_if_fini $ul4 192.0.2.178/28
	simple_if_fini $ol4 2001:db8:2::1/64
}

h2_create()
{
	simple_if_init $h2 2001:db8:2::2/64
	ip -6 route add vrf v$h2 2001:db8:1::/64 via 2001:db8:2::1
}

h2_destroy()
{
	ip -6 route del vrf v$h2 2001:db8:1::/64 via 2001:db8:2::1
	simple_if_fini $h2 2001:db8:2::2/64
}

setup_prepare()
{
	h1=${NETIFS[p1]}

	ol1=${NETIFS[p2]}
	ul1=${NETIFS[p3]}

	ul21=${NETIFS[p4]}
	ul22=${NETIFS[p5]}

	ul32=${NETIFS[p6]}
	ul31=${NETIFS[p7]}

	ul4=${NETIFS[p8]}
	ol4=${NETIFS[p9]}

	h2=${NETIFS[p10]}

	vrf_prepare
	h1_create
	sw1_create
	sw2_create
	sw3_create
	sw4_create
	h2_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	h2_destroy
	sw4_destroy
	sw3_destroy
	sw2_destroy
	sw1_destroy
	h1_destroy
	vrf_cleanup
}

multipath6_test()
{
	local what=$1; shift
	local weight1=$1; shift
	local weight2=$1; shift

	sysctl_set net.ipv4.fib_multipath_hash_policy 2
	ip route replace vrf v$ul21 192.0.2.66/32 \
	   nexthop via 192.0.2.146 weight $weight1 \
	   nexthop via 192.0.2.162 weight $weight2

	local t0_111=$(tc_rule_stats_get $ul32 111 ingress)
	local t0_222=$(tc_rule_stats_get $ul32 222 ingress)

	ip vrf exec v$h1 \
	   $MZ $h1 -6 -q -p 64 -A "2001:db8:1::2-2001:db8:1::3e" \
	       -B "2001:db8:2::2-2001:db8:2::3e" \
	       -d $MZ_DELAY -c 50 -t udp "sp=1024,dp=1024"
	sleep 1

	local t1_111=$(tc_rule_stats_get $ul32 111 ingress)
	local t1_222=$(tc_rule_stats_get $ul32 222 ingress)

	local d111=$((t1_111 - t0_111))
	local d222=$((t1_222 - t0_222))
	multipath_eval "$what" $weight1 $weight2 $d111 $d222

	ip route replace vrf v$ul21 192.0.2.66/32 \
	   nexthop via 192.0.2.146 \
	   nexthop via 192.0.2.162
	sysctl_restore net.ipv4.fib_multipath_hash_policy
}

ping_ipv6()
{
	ping_test $h1 2001:db8:2::2
}

multipath_ipv6()
{
	log_info "Running IPv6 over GRE over IPv4 multipath tests"
	multipath6_test "ECMP" 1 1
	multipath6_test "Weighted MP 2:1" 2 1
	multipath6_test "Weighted MP 11:45" 11 45
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
