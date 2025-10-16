#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test traffic distribution between two paths when using custom hash policy.
#
# +--------------------------------+
# | H1                             |
# |                     $h1 +      |
# |   198.51.100.{2-253}/24 |      |
# |   2001:db8:1::{2-fd}/64 |      |
# +-------------------------|------+
#                           |
# +-------------------------|-------------------------+
# | SW1                     |                         |
# |                    $rp1 +                         |
# |         198.51.100.1/24                           |
# |        2001:db8:1::1/64                           |
# |                                                   |
# |                                                   |
# |            $rp11 +             + $rp12            |
# |     192.0.2.1/28 |             | 192.0.2.17/28    |
# | 2001:db8:2::1/64 |             | 2001:db8:3::1/64 |
# +------------------|-------------|------------------+
#                    |             |
# +------------------|-------------|------------------+
# | SW2              |             |                  |
# |                  |             |                  |
# |            $rp21 +             + $rp22            |
# |     192.0.2.2/28                 192.0.2.18/28    |
# | 2001:db8:2::2/64                 2001:db8:3::2/64 |
# |                                                   |
# |                                                   |
# |                    $rp2 +                         |
# |          203.0.113.1/24 |                         |
# |        2001:db8:4::1/64 |                         |
# +-------------------------|-------------------------+
#                           |
# +-------------------------|------+
# | H2                      |      |
# |                     $h2 +      |
# |    203.0.113.{2-253}/24        |
# |   2001:db8:4::{2-fd}/64        |
# +--------------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	custom_hash
"

NUM_NETIFS=8
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
	simple_if_init $rp1 198.51.100.1/24 2001:db8:1::1/64
	__simple_if_init $rp11 v$rp1 192.0.2.1/28 2001:db8:2::1/64
	__simple_if_init $rp12 v$rp1 192.0.2.17/28 2001:db8:3::1/64

	ip route add vrf v$rp1 203.0.113.0/24 \
		nexthop via 192.0.2.2 dev $rp11 \
		nexthop via 192.0.2.18 dev $rp12

	ip -6 route add vrf v$rp1 2001:db8:4::/64 \
		nexthop via 2001:db8:2::2 dev $rp11 \
		nexthop via 2001:db8:3::2 dev $rp12
}

sw1_destroy()
{
	ip -6 route del vrf v$rp1 2001:db8:4::/64

	ip route del vrf v$rp1 203.0.113.0/24

	__simple_if_fini $rp12 192.0.2.17/28 2001:db8:3::1/64
	__simple_if_fini $rp11 192.0.2.1/28 2001:db8:2::1/64
	simple_if_fini $rp1 198.51.100.1/24 2001:db8:1::1/64
}

sw2_create()
{
	simple_if_init $rp2 203.0.113.1/24 2001:db8:4::1/64
	__simple_if_init $rp21 v$rp2 192.0.2.2/28 2001:db8:2::2/64
	__simple_if_init $rp22 v$rp2 192.0.2.18/28 2001:db8:3::2/64

	ip route add vrf v$rp2 198.51.100.0/24 \
		nexthop via 192.0.2.1 dev $rp21 \
		nexthop via 192.0.2.17 dev $rp22

	ip -6 route add vrf v$rp2 2001:db8:1::/64 \
		nexthop via 2001:db8:2::1 dev $rp21 \
		nexthop via 2001:db8:3::1 dev $rp22
}

sw2_destroy()
{
	ip -6 route del vrf v$rp2 2001:db8:1::/64

	ip route del vrf v$rp2 198.51.100.0/24

	__simple_if_fini $rp22 192.0.2.18/28 2001:db8:3::2/64
	__simple_if_fini $rp21 192.0.2.2/28 2001:db8:2::2/64
	simple_if_fini $rp2 203.0.113.1/24 2001:db8:4::1/64
}

h2_create()
{
	simple_if_init $h2 203.0.113.2/24 2001:db8:4::2/64
	ip route add vrf v$h2 default via 203.0.113.1 dev $h2
	ip -6 route add vrf v$h2 default via 2001:db8:4::1 dev $h2
}

h2_destroy()
{
	ip -6 route del vrf v$h2 default
	ip route del vrf v$h2 default
	simple_if_fini $h2 203.0.113.2/24 2001:db8:4::2/64
}

setup_prepare()
{
	h1=${NETIFS[p1]}

	rp1=${NETIFS[p2]}

	rp11=${NETIFS[p3]}
	rp21=${NETIFS[p4]}

	rp12=${NETIFS[p5]}
	rp22=${NETIFS[p6]}

	rp2=${NETIFS[p7]}

	h2=${NETIFS[p8]}

	vrf_prepare
	h1_create
	sw1_create
	sw2_create
	h2_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	h2_destroy
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
	ping6_test $h1 2001:db8:4::2
}

send_src_ipv4()
{
	ip vrf exec v$h1 $MZ $h1 -q -p 64 \
		-A "198.51.100.2-198.51.100.253" -B 203.0.113.2 \
		-d $MZ_DELAY -c 50 -t udp "sp=20000,dp=30000"
}

send_dst_ipv4()
{
	ip vrf exec v$h1 $MZ $h1 -q -p 64 \
		-A 198.51.100.2 -B "203.0.113.2-203.0.113.253" \
		-d $MZ_DELAY -c 50 -t udp "sp=20000,dp=30000"
}

send_src_udp4()
{
	ip vrf exec v$h1 $MZ $h1 -q -p 64 \
		-A 198.51.100.2 -B 203.0.113.2 \
		-d $MZ_DELAY -t udp "sp=0-32768,dp=30000"
}

send_dst_udp4()
{
	ip vrf exec v$h1 $MZ $h1 -q -p 64 \
		-A 198.51.100.2 -B 203.0.113.2 \
		-d $MZ_DELAY -t udp "sp=20000,dp=0-32768"
}

send_src_ipv6()
{
	ip vrf exec v$h1 $MZ -6 $h1 -q -p 64 \
		-A "2001:db8:1::2-2001:db8:1::fd" -B 2001:db8:4::2 \
		-d $MZ_DELAY -c 50 -t udp "sp=20000,dp=30000"
}

send_dst_ipv6()
{
	ip vrf exec v$h1 $MZ -6 $h1 -q -p 64 \
		-A 2001:db8:1::2 -B "2001:db8:4::2-2001:db8:4::fd" \
		-d $MZ_DELAY -c 50 -t udp "sp=20000,dp=30000"
}

send_flowlabel()
{
	# Generate 16384 echo requests, each with a random flow label.
	ip vrf exec v$h1 sh -c \
		"for _ in {1..16384}; do \
			$PING6 -F 0 -c 1 -q 2001:db8:4::2 >/dev/null 2>&1; \
		done"
}

send_src_udp6()
{
	ip vrf exec v$h1 $MZ -6 $h1 -q -p 64 \
		-A 2001:db8:1::2 -B 2001:db8:4::2 \
		-d $MZ_DELAY -t udp "sp=0-32768,dp=30000"
}

send_dst_udp6()
{
	ip vrf exec v$h1 $MZ -6 $h1 -q -p 64 \
		-A 2001:db8:1::2 -B 2001:db8:4::2 \
		-d $MZ_DELAY -t udp "sp=20000,dp=0-32768"
}

custom_hash_test()
{
	local field="$1"; shift
	local balanced="$1"; shift
	local send_flows="$@"

	RET=0

	local t0_rp11=$(link_stats_tx_packets_get $rp11)
	local t0_rp12=$(link_stats_tx_packets_get $rp12)

	$send_flows

	local t1_rp11=$(link_stats_tx_packets_get $rp11)
	local t1_rp12=$(link_stats_tx_packets_get $rp12)

	local d_rp11=$((t1_rp11 - t0_rp11))
	local d_rp12=$((t1_rp12 - t0_rp12))

	local diff=$((d_rp12 - d_rp11))
	local sum=$((d_rp11 + d_rp12))

	local pct=$(echo "$diff / $sum * 100" | bc -l)
	local is_balanced=$(echo "-20 <= $pct && $pct <= 20" | bc)

	[[ ( $is_balanced -eq 1 && $balanced == "balanced" ) ||
	   ( $is_balanced -eq 0 && $balanced == "unbalanced" ) ]]
	check_err $? "Expected traffic to be $balanced, but it is not"

	log_test "Multipath hash field: $field ($balanced)"
	log_info "Packets sent on path1 / path2: $d_rp11 / $d_rp12"
}

custom_hash_v4()
{
	log_info "Running IPv4 custom multipath hash tests"

	sysctl_set net.ipv4.fib_multipath_hash_policy 3

	# Prevent the neighbour table from overflowing, as different neighbour
	# entries will be created on $ol4 when using different destination IPs.
	sysctl_set net.ipv4.neigh.default.gc_thresh1 1024
	sysctl_set net.ipv4.neigh.default.gc_thresh2 1024
	sysctl_set net.ipv4.neigh.default.gc_thresh3 1024

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0001
	custom_hash_test "Source IP" "balanced" send_src_ipv4
	custom_hash_test "Source IP" "unbalanced" send_dst_ipv4

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0002
	custom_hash_test "Destination IP" "balanced" send_dst_ipv4
	custom_hash_test "Destination IP" "unbalanced" send_src_ipv4

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0010
	custom_hash_test "Source port" "balanced" send_src_udp4
	custom_hash_test "Source port" "unbalanced" send_dst_udp4

	sysctl_set net.ipv4.fib_multipath_hash_fields 0x0020
	custom_hash_test "Destination port" "balanced" send_dst_udp4
	custom_hash_test "Destination port" "unbalanced" send_src_udp4

	sysctl_restore net.ipv4.neigh.default.gc_thresh3
	sysctl_restore net.ipv4.neigh.default.gc_thresh2
	sysctl_restore net.ipv4.neigh.default.gc_thresh1

	sysctl_restore net.ipv4.fib_multipath_hash_policy
}

custom_hash_v6()
{
	log_info "Running IPv6 custom multipath hash tests"

	sysctl_set net.ipv6.fib_multipath_hash_policy 3

	# Prevent the neighbour table from overflowing, as different neighbour
	# entries will be created on $ol4 when using different destination IPs.
	sysctl_set net.ipv6.neigh.default.gc_thresh1 1024
	sysctl_set net.ipv6.neigh.default.gc_thresh2 1024
	sysctl_set net.ipv6.neigh.default.gc_thresh3 1024

	sysctl_set net.ipv6.fib_multipath_hash_fields 0x0001
	custom_hash_test "Source IP" "balanced" send_src_ipv6
	custom_hash_test "Source IP" "unbalanced" send_dst_ipv6

	sysctl_set net.ipv6.fib_multipath_hash_fields 0x0002
	custom_hash_test "Destination IP" "balanced" send_dst_ipv6
	custom_hash_test "Destination IP" "unbalanced" send_src_ipv6

	sysctl_set net.ipv6.fib_multipath_hash_fields 0x0008
	custom_hash_test "Flowlabel" "balanced" send_flowlabel
	custom_hash_test "Flowlabel" "unbalanced" send_src_ipv6

	sysctl_set net.ipv6.fib_multipath_hash_fields 0x0010
	custom_hash_test "Source port" "balanced" send_src_udp6
	custom_hash_test "Source port" "unbalanced" send_dst_udp6

	sysctl_set net.ipv6.fib_multipath_hash_fields 0x0020
	custom_hash_test "Destination port" "balanced" send_dst_udp6
	custom_hash_test "Destination port" "unbalanced" send_src_udp6

	sysctl_restore net.ipv6.neigh.default.gc_thresh3
	sysctl_restore net.ipv6.neigh.default.gc_thresh2
	sysctl_restore net.ipv6.neigh.default.gc_thresh1

	sysctl_restore net.ipv6.fib_multipath_hash_policy
}

custom_hash()
{
	# Test that when the hash policy is set to custom, traffic is
	# distributed only according to the fields set in the
	# fib_multipath_hash_fields sysctl.
	#
	# Each time set a different field and make sure traffic is only
	# distributed when the field is changed in the packet stream.
	custom_hash_v4
	custom_hash_v6
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
