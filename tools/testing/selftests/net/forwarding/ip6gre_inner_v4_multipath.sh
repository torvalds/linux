#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test traffic distribution when there are multiple routes between an IPv6
# GRE tunnel. The tunnel carries IPv4 traffic between multiple hosts.
# Multiple routes are in the underlay network. With the default multipath
# policy, SW2 will only look at the outer IP addresses, hence only a single
# route would be used.
#
# +-------------------------+
# | H1                      |
# |               $h1 +     |
# | 192.0.3.{2-62}/24 |     |
# +-------------------|-----+
#                     |
# +-------------------|-------------------------+
# | SW1               |                         |
# |              $ol1 +                         |
# |      192.0.3.1/24                           |
# |                                             |
# |  + g1 (gre)                                 |
# |    loc=2001:db8:40::1                       |
# |    rem=2001:db8:40::2 --.                   |
# |    tos=inherit          |                   |
# |                         v                   |
# |                         + $ul1              |
# |                         | 2001:db8:80::1/64 |
# +-------------------------|-------------------+
#                           |
# +-------------------------|-------------------+
# | SW2                     |                   |
# |                   $ul21 +                   |
# |       2001:db8:80::2/64                     |
# |                   |                         |
# !   ________________|_____                    |
# |  /                      \                   |
# |  |                      |                   |
# |  + $ul22.111 (vlan)     + $ul22.222 (vlan)  |
# |  | 2001:db8:81::1/64    | 2001:db8:82::1/64 |
# |  |                      |                   |
# +--|----------------------|-------------------+
#    |                      |
# +--|----------------------|-------------------+
# |  |                      |                   |
# |  + $ul32.111 (vlan)     + $ul32.222 (vlan)  |
# |  | 2001:db8:81::2/64    | 2001:db8:82::2/64 |
# |  |                      |                   |
# |  \______________________/                   |
# |                   |                         |
# |                   |                         |
# |                   $ul31 +                   |
# |       2001:db8:83::2/64 |               SW3 |
# +-------------------------|-------------------+
#                           |
# +-------------------------|-------------------+
# |                         + $ul4              |
# |                         ^ 2001:db8:83::1/64 |
# |  + g2 (gre)             |                   |
# |    loc=2001:db8:40::2   |                   |
# |    rem=2001:db8:40::1 --'                   |
# |    tos=inherit                              |
# |                                             |
# |               $ol4 +                        |
# |       192.0.4.1/24 |                    SW4 |
# +--------------------|------------------------+
#                      |
# +--------------------|---------+
# |                    |         |
# |                $h2 +         |
# |  192.0.4.{2-62}/24        H2 |
# +------------------------------+

ALL_TESTS="
	ping_ipv4
	multipath_ipv4
"

NUM_NETIFS=10
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.3.2/24
	ip route add vrf v$h1 192.0.4.0/24 via 192.0.3.1
}

h1_destroy()
{
	ip route del vrf v$h1 192.0.4.0/24 via 192.0.3.1
	simple_if_fini $h1 192.0.3.2/24
}

sw1_create()
{
	simple_if_init $ol1 192.0.3.1/24
	__simple_if_init $ul1 v$ol1 2001:db8:80::1/64

	tunnel_create g1 ip6gre 2001:db8:40::1 2001:db8:40::2 tos inherit dev v$ol1
	__simple_if_init g1 v$ol1 2001:db8:40::1/128
	ip -6 route add vrf v$ol1 2001:db8:40::2/128 via 2001:db8:80::2

	ip route add vrf v$ol1 192.0.4.0/24 nexthop dev g1
}

sw1_destroy()
{
	ip route del vrf v$ol1 192.0.4.0/24

	ip -6 route del vrf v$ol1 2001:db8:40::2/128
	__simple_if_fini g1 2001:db8:40::1/128
	tunnel_destroy g1

	__simple_if_fini $ul1 2001:db8:80::1/64
	simple_if_fini $ol1 192.0.3.1/24
}

sw2_create()
{
	simple_if_init $ul21 2001:db8:80::2/64
	__simple_if_init $ul22 v$ul21
	vlan_create $ul22 111 v$ul21 2001:db8:81::1/64
	vlan_create $ul22 222 v$ul21 2001:db8:82::1/64

	ip -6 route add vrf v$ul21 2001:db8:40::1/128 via 2001:db8:80::1
	ip -6 route add vrf v$ul21 2001:db8:40::2/128 \
	   nexthop via 2001:db8:81::2 \
	   nexthop via 2001:db8:82::2
}

sw2_destroy()
{
	ip -6 route del vrf v$ul21 2001:db8:40::2/128
	ip -6 route del vrf v$ul21 2001:db8:40::1/128

	vlan_destroy $ul22 222
	vlan_destroy $ul22 111
	__simple_if_fini $ul22
	simple_if_fini $ul21 2001:db8:80::2/64
}

sw3_create()
{
	simple_if_init $ul31 2001:db8:83::2/64
	__simple_if_init $ul32 v$ul31
	vlan_create $ul32 111 v$ul31 2001:db8:81::2/64
	vlan_create $ul32 222 v$ul31 2001:db8:82::2/64

	ip -6 route add vrf v$ul31 2001:db8:40::2/128 via 2001:db8:83::1
	ip -6 route add vrf v$ul31 2001:db8:40::1/128 \
	   nexthop via 2001:db8:81::1 \
	   nexthop via 2001:db8:82::1

	tc qdisc add dev $ul32 clsact
	tc filter add dev $ul32 ingress pref 111 prot 802.1Q \
	   flower vlan_id 111 action pass
	tc filter add dev $ul32 ingress pref 222 prot 802.1Q \
	   flower vlan_id 222 action pass
}

sw3_destroy()
{
	tc qdisc del dev $ul32 clsact

	ip -6 route del vrf v$ul31 2001:db8:40::1/128
	ip -6 route del vrf v$ul31 2001:db8:40::2/128

	vlan_destroy $ul32 222
	vlan_destroy $ul32 111
	__simple_if_fini $ul32
	simple_if_fini $ul31 2001:Db8:83::2/64
}

sw4_create()
{
	simple_if_init $ol4 192.0.4.1/24
	__simple_if_init $ul4 v$ol4 2001:db8:83::1/64

	tunnel_create g2 ip6gre 2001:db8:40::2 2001:db8:40::1 tos inherit dev v$ol4
	__simple_if_init g2 v$ol4 2001:db8:40::2/128
	ip -6 route add vrf v$ol4 2001:db8:40::1/128 via 2001:db8:83::2

	ip route add vrf v$ol4 192.0.3.0/24 nexthop dev g2
}

sw4_destroy()
{
	ip route del vrf v$ol4 192.0.3.0/24

	ip -6 route del vrf v$ol4 2001:db8:40::1/128
	__simple_if_fini g2 2001:db8:40::2/128
	tunnel_destroy g2

	__simple_if_fini $ul4 2001:db8:83::1/64
	simple_if_fini $ol4 192.0.4.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.4.2/24
	ip route add vrf v$h2 192.0.3.0/24 via 192.0.4.1
}

h2_destroy()
{
	ip route del vrf v$h2 192.0.3.0/24 via 192.0.4.1
	simple_if_fini $h2 192.0.4.2/24
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

multipath4_test()
{
	local what=$1; shift
	local weight1=$1; shift
	local weight2=$1; shift

	sysctl_set net.ipv6.fib_multipath_hash_policy 2
	ip route replace vrf v$ul21 2001:db8:40::2/128 \
	   nexthop via 2001:db8:81::2 weight $weight1 \
	   nexthop via 2001:db8:82::2 weight $weight2

	local t0_111=$(tc_rule_stats_get $ul32 111 ingress)
	local t0_222=$(tc_rule_stats_get $ul32 222 ingress)

	ip vrf exec v$h1 \
	   $MZ $h1 -q -p 64 -A "192.0.3.2-192.0.3.62" -B "192.0.4.2-192.0.4.62" \
	       -d 1msec -c 50 -t udp "sp=1024,dp=1024"
	sleep 1

	local t1_111=$(tc_rule_stats_get $ul32 111 ingress)
	local t1_222=$(tc_rule_stats_get $ul32 222 ingress)

	local d111=$((t1_111 - t0_111))
	local d222=$((t1_222 - t0_222))
	multipath_eval "$what" $weight1 $weight2 $d111 $d222

	ip route replace vrf v$ul21 2001:db8:40::2/128 \
	   nexthop via 2001:db8:81::2 \
	   nexthop via 2001:db8:82::2
	sysctl_restore net.ipv6.fib_multipath_hash_policy
}

ping_ipv4()
{
	ping_test $h1 192.0.4.2
}

multipath_ipv4()
{
	log_info "Running IPv4 over GRE over IPv6 multipath tests"
	multipath4_test "ECMP" 1 1
	multipath4_test "Weighted MP 2:1" 2 1
	multipath4_test "Weighted MP 11:45" 11 45
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
