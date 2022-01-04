#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test traffic distribution when there are multiple paths between an IPv4 GRE
# tunnel. The tunnel carries IPv4 and IPv6 traffic between multiple hosts.
# Multiple routes are in the underlay network. With the default multipath
# policy, SW2 will only look at the outer IP addresses, hence only a single
# route would be used.
#
# +--------------------------------+
# | H1                             |
# |                     $h1 +      |
# |   198.51.100.{2-253}/24 |      |
# |   2001:db8:1::{2-fd}/64 |      |
# +-------------------------|------+
#                           |
# +-------------------------|------------------+
# | SW1                     |                  |
# |                    $ol1 +                  |
# |         198.51.100.1/24                    |
# |        2001:db8:1::1/64                    |
# |                                            |
# |   + g1 (gre)                               |
# |     loc=192.0.2.1                          |
# |     rem=192.0.2.2 --.                      |
# |     tos=inherit     |                      |
# |                     v                      |
# |                     + $ul1                 |
# |                     | 192.0.2.17/28        |
# +---------------------|----------------------+
#                       |
# +---------------------|----------------------+
# | SW2                 |                      |
# |               $ul21 +                      |
# |       192.0.2.18/28 |                      |
# |                     |                      |
# !   __________________+___                   |
# |  /                      \                  |
# |  |                      |                  |
# |  + $ul22.111 (vlan)     + $ul22.222 (vlan) |
# |  | 192.0.2.33/28        | 192.0.2.49/28    |
# |  |                      |                  |
# +--|----------------------|------------------+
#    |                      |
# +--|----------------------|------------------+
# |  |                      |                  |
# |  + $ul32.111 (vlan)     + $ul32.222 (vlan) |
# |  | 192.0.2.34/28        | 192.0.2.50/28    |
# |  |                      |                  |
# |  \__________________+___/                  |
# |                     |                      |
# |                     |                      |
# |               $ul31 +                      |
# |       192.0.2.65/28 |                  SW3 |
# +---------------------|----------------------+
#                       |
# +---------------------|----------------------+
# |                     + $ul4                 |
# |                     ^ 192.0.2.66/28        |
# |                     |                      |
# |   + g2 (gre)        |                      |
# |     loc=192.0.2.2   |                      |
# |     rem=192.0.2.1 --'                      |
# |     tos=inherit                            |
# |                                            |
# |                    $ol4 +                  |
# |          203.0.113.1/24 |                  |
# |        2001:db8:2::1/64 |              SW4 |
# +-------------------------|------------------+
#                           |
# +-------------------------|------+
# |                         |      |
# |                     $h2 +      |
# |    203.0.113.{2-253}/24        |
# |   2001:db8:2::{2-fd}/64     H2 |
# +--------------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	custom_hash
"

NUM_NETIFS=10
source lib.sh

h1_create()
{
	simple_if_init $h1 198.51.100.2/24 2001:db8:1::2/64
	ip route add vrf v$h1 default via 198.51.100.1 dev $h1
	ip -6 route add vrf v$h1 default via 2001:db8:1::1 dev $h1
}

h1_destroy()
{
	ip -6 route del vrf v$h1 default
	ip route del vrf v$h1 default
	simple_if_fini $h1 198.51.100.2/24 2001:db8:1::2/64
}

sw1_create()
{
	simple_if_init $ol1 198.51.100.1/24 2001:db8:1::1/64
	__simple_if_init $ul1 v$ol1 192.0.2.17/28

	tunnel_create g1 gre 192.0.2.1 192.0.2.2 tos inherit dev v$ol1
	__simple_if_init g1 v$ol1 192.0.2.1/32
	ip route add vrf v$ol1 192.0.2.2/32 via 192.0.2.18

	ip route add vrf v$ol1 203.0.113.0/24 dev g1
	ip -6 route add vrf v$ol1 2001:db8:2::/64 dev g1
}

sw1_destroy()
{
	ip -6 route del vrf v$ol1 2001:db8:2::/64
	ip route del vrf v$ol1 203.0.113.0/24

	ip route del vrf v$ol1 192.0.2.2/32
	__simple_if_fini g1 192.0.2.1/32
	tunnel_destroy g1

	__simple_if_fini $ul1 192.0.2.17/28
	simple_if_fini $ol1 198.51.100.1/24 2001:db8:1::1/64
}

sw2_create()
{
	simple_if_init $ul21 192.0.2.18/28
	__simple_if_init $ul22 v$ul21
	vlan_create $ul22 111 v$ul21 192.0.2.33/28
	vlan_create $ul22 222 v$ul21 192.0.2.49/28

	ip route add vrf v$ul21 192.0.2.1/32 via 192.0.2.17
	ip route add vrf v$ul21 192.0.2.2/32 \
	   nexthop via 192.0.2.34 \
	   nexthop via 192.0.2.50
}

sw2_destroy()
{
	ip route del vrf v$ul21 192.0.2.2/32
	ip route del vrf v$ul21 192.0.2.1/32

	vlan_destroy $ul22 222
	vlan_destroy $ul22 111
	__simple_if_fini $ul22
	simple_if_fini $ul21 192.0.2.18/28
}

sw3_create()
{
	simple_if_init $ul31 192.0.2.65/28
	__simple_if_init $ul32 v$ul31
	vlan_create $ul32 111 v$ul31 192.0.2.34/28
	vlan_create $ul32 222 v$ul31 192.0.2.50/28

	ip route add vrf v$ul31 192.0.2.2/32 via 192.0.2.66
	ip route add vrf v$ul31 192.0.2.1/32 \
	   nexthop via 192.0.2.33 \
	   nexthop via 192.0.2.49

	tc qdisc add dev $ul32 clsact
	tc filter add dev $ul32 ingress pref 111 prot 802.1Q \
	   flower vlan_id 111 action pass
	tc filter add dev $ul32 ingress pref 222 prot 802.1Q \
	   flower vlan_id 222 action pass
}

sw3_destroy()
{
	tc qdisc del dev $ul32 clsact

	ip route del vrf v$ul31 192.0.2.1/32
	ip route del vrf v$ul31 192.0.2.2/32

	vlan_destroy $ul32 222
	vlan_destroy $ul32 111
	__simple_if_fini $ul32
	simple_if_fini $ul31 192.0.2.65/28
}

sw4_create()
{
	simple_if_init $ol4 203.0.113.1/24 2001:db8:2::1/64
	__simple_if_init $ul4 v$ol4 192.0.2.66/28

	tunnel_create g2 gre 192.0.2.2 192.0.2.1 tos inherit dev v$ol4
	__simple_if_init g2 v$ol4 192.0.2.2/32
	ip route add vrf v$ol4 192.0.2.1/32 via 192.0.2.65

	ip route add vrf v$ol4 198.51.100.0/24 dev g2
	ip -6 route add vrf v$ol4 2001:db8:1::/64 dev g2
}

sw4_destroy()
{
	ip -6 route del vrf v$ol4 2001:db8:1::/64
	ip route del vrf v$ol4 198.51.100.0/24

	ip route del vrf v$ol4 192.0.2.1/32
	__simple_if_fini g2 192.0.2.2/32
	tunnel_destroy g2

	__simple_if_fini $ul4 192.0.2.66/28
	simple_if_fini $ol4 203.0.113.1/24 2001:db8:2::1/64
}

h2_create()
{
	simple_if_init $h2 203.0.113.2/24 2001:db8:2::2/64
	ip route add vrf v$h2 default via 203.0.113.1 dev $h2
	ip -6 route add vrf v$h2 default via 2001:db8:2::1 dev $h2
}

h2_destroy()
{
	ip -6 route del vrf v$h2 default
	ip route del vrf v$h2 default
	simple_if_fini $h2 203.0.113.2/24 2001:db8:2::2/64
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

ping_ipv4()
{
	ping_test $h1 203.0.113.2
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:2::2
}

send_src_ipv4()
{
	$MZ $h1 -q -p 64 -A "198.51.100.2-198.51.100.253" -B 203.0.113.2 \
		-d 1msec -c 50 -t udp "sp=20000,dp=30000"
}

send_dst_ipv4()
{
	$MZ $h1 -q -p 64 -A 198.51.100.2 -B "203.0.113.2-203.0.113.253" \
		-d 1msec -c 50 -t udp "sp=20000,dp=30000"
}

send_src_udp4()
{
	$MZ $h1 -q -p 64 -A 198.51.100.2 -B 203.0.113.2 \
		-d 1msec -t udp "sp=0-32768,dp=30000"
}

send_dst_udp4()
{
	$MZ $h1 -q -p 64 -A 198.51.100.2 -B 203.0.113.2 \
		-d 1msec -t udp "sp=20000,dp=0-32768"
}

send_src_ipv6()
{
	$MZ -6 $h1 -q -p 64 -A "2001:db8:1::2-2001:db8:1::fd" -B 2001:db8:2::2 \
		-d 1msec -c 50 -t udp "sp=20000,dp=30000"
}

send_dst_ipv6()
{
	$MZ -6 $h1 -q -p 64 -A 2001:db8:1::2 -B "2001:db8:2::2-2001:db8:2::fd" \
		-d 1msec -c 50 -t udp "sp=20000,dp=30000"
}

send_flowlabel()
{
	# Generate 16384 echo requests, each with a random flow label.
	for _ in $(seq 1 16384); do
		ip vrf exec v$h1 \
			$PING6 2001:db8:2::2 -F 0 -c 1 -q >/dev/null 2>&1
	done
}

send_src_udp6()
{
	$MZ -6 $h1 -q -p 64 -A 2001:db8:1::2 -B 2001:db8:2::2 \
		-d 1msec -t udp "sp=0-32768,dp=30000"
}

send_dst_udp6()
{
	$MZ -6 $h1 -q -p 64 -A 2001:db8:1::2 -B 2001:db8:2::2 \
		-d 1msec -t udp "sp=20000,dp=0-32768"
}

custom_hash_test()
{
	local field="$1"; shift
	local balanced="$1"; shift
	local send_flows="$@"

	RET=0

	local t0_111=$(tc_rule_stats_get $ul32 111 ingress)
	local t0_222=$(tc_rule_stats_get $ul32 222 ingress)

	$send_flows

	local t1_111=$(tc_rule_stats_get $ul32 111 ingress)
	local t1_222=$(tc_rule_stats_get $ul32 222 ingress)

	local d111=$((t1_111 - t0_111))
	local d222=$((t1_222 - t0_222))

	local diff=$((d222 - d111))
	local sum=$((d111 + d222))

	local pct=$(echo "$diff / $sum * 100" | bc -l)
	local is_balanced=$(echo "-20 <= $pct && $pct <= 20" | bc)

	[[ ( $is_balanced -eq 1 && $balanced == "balanced" ) ||
	   ( $is_balanced -eq 0 && $balanced == "unbalanced" ) ]]
	check_err $? "Expected traffic to be $balanced, but it is not"

	log_test "Multipath hash field: $field ($balanced)"
	log_info "Packets sent on path1 / path2: $d111 / $d222"
}

custom_hash_v4()
{
	log_info "Running IPv4 overlay custom multipath hash tests"

	# Prevent the neighbour table from overflowing, as different neighbour
	# entries will be created on $ol4 when using different destination IPs.
	sysctl_set net.ipv4.neigh.default.gc_thresh1 1024
	sysctl_set net.ipv4.neigh.default.gc_thresh2 1024
	sysctl_set net.ipv4.neigh.default.gc_thresh3 1024

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0040
	custom_hash_test "Inner source IP" "balanced" send_src_ipv4
	custom_hash_test "Inner source IP" "unbalanced" send_dst_ipv4

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0080
	custom_hash_test "Inner destination IP" "balanced" send_dst_ipv4
	custom_hash_test "Inner destination IP" "unbalanced" send_src_ipv4

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0400
	custom_hash_test "Inner source port" "balanced" send_src_udp4
	custom_hash_test "Inner source port" "unbalanced" send_dst_udp4

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0800
	custom_hash_test "Inner destination port" "balanced" send_dst_udp4
	custom_hash_test "Inner destination port" "unbalanced" send_src_udp4

	sysctl_restore net.ipv4.neigh.default.gc_thresh3
	sysctl_restore net.ipv4.neigh.default.gc_thresh2
	sysctl_restore net.ipv4.neigh.default.gc_thresh1
}

custom_hash_v6()
{
	log_info "Running IPv6 overlay custom multipath hash tests"

	# Prevent the neighbour table from overflowing, as different neighbour
	# entries will be created on $ol4 when using different destination IPs.
	sysctl_set net.ipv6.neigh.default.gc_thresh1 1024
	sysctl_set net.ipv6.neigh.default.gc_thresh2 1024
	sysctl_set net.ipv6.neigh.default.gc_thresh3 1024

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0040
	custom_hash_test "Inner source IP" "balanced" send_src_ipv6
	custom_hash_test "Inner source IP" "unbalanced" send_dst_ipv6

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0080
	custom_hash_test "Inner destination IP" "balanced" send_dst_ipv6
	custom_hash_test "Inner destination IP" "unbalanced" send_src_ipv6

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0200
	custom_hash_test "Inner flowlabel" "balanced" send_flowlabel
	custom_hash_test "Inner flowlabel" "unbalanced" send_src_ipv6

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0400
	custom_hash_test "Inner source port" "balanced" send_src_udp6
	custom_hash_test "Inner source port" "unbalanced" send_dst_udp6

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0800
	custom_hash_test "Inner destination port" "balanced" send_dst_udp6
	custom_hash_test "Inner destination port" "unbalanced" send_src_udp6

	sysctl_restore net.ipv6.neigh.default.gc_thresh3
	sysctl_restore net.ipv6.neigh.default.gc_thresh2
	sysctl_restore net.ipv6.neigh.default.gc_thresh1
}

custom_hash()
{
	# Test that when the hash policy is set to custom, traffic is
	# distributed only according to the fields set in the
	# fib_multipath_hash_fields sysctl.
	#
	# Each time set a different field and make sure traffic is only
	# distributed when the field is changed in the packet stream.

	sysctl_set net.ipv4.fib_multipath_hash_policy 3

	custom_hash_v4
	custom_hash_v6

	sysctl_restore net.ipv4.fib_multipath_hash_policy
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
