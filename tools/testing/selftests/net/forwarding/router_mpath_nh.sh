#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-------------------------+
# |  H1                     |
# |               $h1 +     |
# |      192.0.2.2/24 |     |
# |  2001:db8:1::2/64 |     |
# +-------------------|-----+
#                     |
# +-------------------|----------------------+
# |                   |                   R1 |
# |             $rp11 +                      |
# |      192.0.2.1/24                        |
# |  2001:db8:1::1/64                        |
# |                                          |
# |  + $rp12              + $rp13            |
# |  | 169.254.2.12/24    | 169.254.3.13/24  |
# |  | fe80:2::12/64      | fe80:3::13/64    |
# +--|--------------------|------------------+
#    |                    |
# +--|--------------------|------------------+
# |  + $rp22              + $rp23            |
# |    169.254.2.22/24      169.254.3.23/24  |
# |    fe80:2::22/64        fe80:3::23/64    |
# |                                          |
# |             $rp21 +                      |
# |   198.51.100.1/24 |                      |
# |  2001:db8:2::1/64 |                   R2 |
# +-------------------|----------------------+
#                     |
# +-------------------|-----+
# |                   |     |
# |               $h2 +     |
# |   198.51.100.2/24       |
# |  2001:db8:2::2/64    H2 |
# +-------------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	multipath_test
	multipath16_test
	ping_ipv4_blackhole
	ping_ipv6_blackhole
	nh_stats_test_v4
	nh_stats_test_v6
"
NUM_NETIFS=8
source lib.sh
source router_mpath_nh_lib.sh

h1_create()
{
	vrf_create "vrf-h1"
	ip link set dev $h1 master vrf-h1

	ip link set dev vrf-h1 up
	ip link set dev $h1 up

	ip address add 192.0.2.2/24 dev $h1
	ip address add 2001:db8:1::2/64 dev $h1

	ip route add 198.51.100.0/24 vrf vrf-h1 nexthop via 192.0.2.1
	ip route add 2001:db8:2::/64 vrf vrf-h1 nexthop via 2001:db8:1::1
}

h1_destroy()
{
	ip route del 2001:db8:2::/64 vrf vrf-h1
	ip route del 198.51.100.0/24 vrf vrf-h1

	ip address del 2001:db8:1::2/64 dev $h1
	ip address del 192.0.2.2/24 dev $h1

	ip link set dev $h1 down
	vrf_destroy "vrf-h1"
}

h2_create()
{
	vrf_create "vrf-h2"
	ip link set dev $h2 master vrf-h2

	ip link set dev vrf-h2 up
	ip link set dev $h2 up

	ip address add 198.51.100.2/24 dev $h2
	ip address add 2001:db8:2::2/64 dev $h2

	ip route add 192.0.2.0/24 vrf vrf-h2 nexthop via 198.51.100.1
	ip route add 2001:db8:1::/64 vrf vrf-h2 nexthop via 2001:db8:2::1
}

h2_destroy()
{
	ip route del 2001:db8:1::/64 vrf vrf-h2
	ip route del 192.0.2.0/24 vrf vrf-h2

	ip address del 2001:db8:2::2/64 dev $h2
	ip address del 198.51.100.2/24 dev $h2

	ip link set dev $h2 down
	vrf_destroy "vrf-h2"
}

router1_create()
{
	vrf_create "vrf-r1"
	ip link set dev $rp11 master vrf-r1
	ip link set dev $rp12 master vrf-r1
	ip link set dev $rp13 master vrf-r1

	ip link set dev vrf-r1 up
	ip link set dev $rp11 up
	ip link set dev $rp12 up
	ip link set dev $rp13 up

	ip address add 192.0.2.1/24 dev $rp11
	ip address add 2001:db8:1::1/64 dev $rp11

	ip address add 169.254.2.12/24 dev $rp12
	ip address add fe80:2::12/64 dev $rp12

	ip address add 169.254.3.13/24 dev $rp13
	ip address add fe80:3::13/64 dev $rp13
}

router1_destroy()
{
	ip route del 2001:db8:2::/64 vrf vrf-r1
	ip route del 198.51.100.0/24 vrf vrf-r1

	ip address del fe80:3::13/64 dev $rp13
	ip address del 169.254.3.13/24 dev $rp13

	ip address del fe80:2::12/64 dev $rp12
	ip address del 169.254.2.12/24 dev $rp12

	ip address del 2001:db8:1::1/64 dev $rp11
	ip address del 192.0.2.1/24 dev $rp11

	ip nexthop del id 103
	ip nexthop del id 101
	ip nexthop del id 102
	ip nexthop del id 106
	ip nexthop del id 104
	ip nexthop del id 105

	ip link set dev $rp13 down
	ip link set dev $rp12 down
	ip link set dev $rp11 down

	vrf_destroy "vrf-r1"
}

router2_create()
{
	vrf_create "vrf-r2"
	ip link set dev $rp21 master vrf-r2
	ip link set dev $rp22 master vrf-r2
	ip link set dev $rp23 master vrf-r2

	ip link set dev vrf-r2 up
	ip link set dev $rp21 up
	ip link set dev $rp22 up
	ip link set dev $rp23 up

	ip address add 198.51.100.1/24 dev $rp21
	ip address add 2001:db8:2::1/64 dev $rp21

	ip address add 169.254.2.22/24 dev $rp22
	ip address add fe80:2::22/64 dev $rp22

	ip address add 169.254.3.23/24 dev $rp23
	ip address add fe80:3::23/64 dev $rp23
}

router2_destroy()
{
	ip route del 2001:db8:1::/64 vrf vrf-r2
	ip route del 192.0.2.0/24 vrf vrf-r2

	ip address del fe80:3::23/64 dev $rp23
	ip address del 169.254.3.23/24 dev $rp23

	ip address del fe80:2::22/64 dev $rp22
	ip address del 169.254.2.22/24 dev $rp22

	ip address del 2001:db8:2::1/64 dev $rp21
	ip address del 198.51.100.1/24 dev $rp21

	ip nexthop del id 201
	ip nexthop del id 202
	ip nexthop del id 204
	ip nexthop del id 205

	ip link set dev $rp23 down
	ip link set dev $rp22 down
	ip link set dev $rp21 down

	vrf_destroy "vrf-r2"
}

routing_nh_obj()
{
	ip nexthop add id 101 via 169.254.2.22 dev $rp12
	ip nexthop add id 102 via 169.254.3.23 dev $rp13
	ip nexthop add id 103 group 101/102
	ip route add 198.51.100.0/24 vrf vrf-r1 nhid 103

	ip nexthop add id 104 via fe80:2::22 dev $rp12
	ip nexthop add id 105 via fe80:3::23 dev $rp13
	ip nexthop add id 106 group 104/105
	ip route add 2001:db8:2::/64 vrf vrf-r1 nhid 106

	ip nexthop add id 201 via 169.254.2.12 dev $rp22
	ip nexthop add id 202 via 169.254.3.13 dev $rp23
	ip nexthop add id 203 group 201/202
	ip route add 192.0.2.0/24 vrf vrf-r2 nhid 203

	ip nexthop add id 204 via fe80:2::12 dev $rp22
	ip nexthop add id 205 via fe80:3::13 dev $rp23
	ip nexthop add id 206 group 204/205
	ip route add 2001:db8:1::/64 vrf vrf-r2 nhid 206
}

multipath4_test()
{
	local desc=$1; shift
	local weight_rp12=$1; shift
	local weight_rp13=$1; shift
	local ports=${1-sp=1024,dp=0-32768}; shift

	local t0_rp12 t0_rp13 t1_rp12 t1_rp13
	local packets_rp12 packets_rp13

	# Transmit multiple flows from h1 to h2 and make sure they are
	# distributed between both multipath links (rp12 and rp13)
	# according to the configured weights.
	sysctl_set net.ipv4.fib_multipath_hash_policy 1
	ip nexthop replace id 103 group 101,$weight_rp12/102,$weight_rp13

	t0_rp12=$(link_stats_tx_packets_get $rp12)
	t0_rp13=$(link_stats_tx_packets_get $rp13)

	ip vrf exec vrf-h1 $MZ $h1 -q -p 64 -A 192.0.2.2 -B 198.51.100.2 \
		-d $MZ_DELAY -t udp "$ports"
	sleep 1

	t1_rp12=$(link_stats_tx_packets_get $rp12)
	t1_rp13=$(link_stats_tx_packets_get $rp13)

	let "packets_rp12 = $t1_rp12 - $t0_rp12"
	let "packets_rp13 = $t1_rp13 - $t0_rp13"
	multipath_eval "$desc" $weight_rp12 $weight_rp13 $packets_rp12 $packets_rp13

	# Restore settings.
	ip nexthop replace id 103 group 101/102
	sysctl_restore net.ipv4.fib_multipath_hash_policy
}

multipath6_test()
{
	local desc=$1; shift
	local weight_rp12=$1; shift
	local weight_rp13=$1; shift
	local ports=${1-sp=1024,dp=0-32768}; shift

	local t0_rp12 t0_rp13 t1_rp12 t1_rp13
	local packets_rp12 packets_rp13

	# Transmit multiple flows from h1 to h2 and make sure they are
	# distributed between both multipath links (rp12 and rp13)
	# according to the configured weights.
	sysctl_set net.ipv6.fib_multipath_hash_policy 1

	ip nexthop replace id 106 group 104,$weight_rp12/105,$weight_rp13

	t0_rp12=$(link_stats_tx_packets_get $rp12)
	t0_rp13=$(link_stats_tx_packets_get $rp13)

	$MZ $h1 -6 -q -p 64 -A 2001:db8:1::2 -B 2001:db8:2::2 \
		-d $MZ_DELAY -t udp "$ports"
	sleep 1

	t1_rp12=$(link_stats_tx_packets_get $rp12)
	t1_rp13=$(link_stats_tx_packets_get $rp13)

	let "packets_rp12 = $t1_rp12 - $t0_rp12"
	let "packets_rp13 = $t1_rp13 - $t0_rp13"
	multipath_eval "$desc" $weight_rp12 $weight_rp13 $packets_rp12 $packets_rp13

	ip nexthop replace id 106 group 104/105

	sysctl_restore net.ipv6.fib_multipath_hash_policy
}

multipath_test()
{
	log_info "Running IPv4 multipath tests"
	multipath4_test "ECMP" 1 1
	multipath4_test "Weighted MP 2:1" 2 1
	multipath4_test "Weighted MP 11:45" 11 45

	log_info "Running IPv4 multipath tests with IPv6 link-local nexthops"
	ip nexthop replace id 101 via fe80:2::22 dev $rp12
	ip nexthop replace id 102 via fe80:3::23 dev $rp13

	multipath4_test "ECMP" 1 1
	multipath4_test "Weighted MP 2:1" 2 1
	multipath4_test "Weighted MP 11:45" 11 45

	ip nexthop replace id 102 via 169.254.3.23 dev $rp13
	ip nexthop replace id 101 via 169.254.2.22 dev $rp12

	log_info "Running IPv6 multipath tests"
	multipath6_test "ECMP" 1 1
	multipath6_test "Weighted MP 2:1" 2 1
	multipath6_test "Weighted MP 11:45" 11 45
}

multipath16_test()
{
	check_nhgw16 104 || return

	log_info "Running 16-bit IPv4 multipath tests"
	multipath4_test "65535:65535" 65535 65535
	multipath4_test "128:512" 128 512
	omit_on_slow \
		multipath4_test "255:65535" 255 65535 sp=1024-1026,dp=0-65535

	log_info "Running 16-bit IPv6 multipath tests"
	multipath6_test "65535:65535" 65535 65535
	multipath6_test "128:512" 128 512
	omit_on_slow \
		multipath6_test "255:65535" 255 65535 sp=1024-1026,dp=0-65535
}

ping_ipv4_blackhole()
{
	RET=0

	ip nexthop add id 1001 blackhole
	ip nexthop add id 1002 group 1001

	ip route replace 198.51.100.0/24 vrf vrf-r1 nhid 1001
	ping_do $h1 198.51.100.2
	check_fail $? "ping did not fail when using a blackhole nexthop"

	ip route replace 198.51.100.0/24 vrf vrf-r1 nhid 1002
	ping_do $h1 198.51.100.2
	check_fail $? "ping did not fail when using a blackhole nexthop group"

	ip route replace 198.51.100.0/24 vrf vrf-r1 nhid 103
	ping_do $h1 198.51.100.2
	check_err $? "ping failed with a valid nexthop"

	log_test "IPv4 blackhole ping"

	ip nexthop del id 1002
	ip nexthop del id 1001
}

ping_ipv6_blackhole()
{
	RET=0

	ip -6 nexthop add id 1001 blackhole
	ip nexthop add id 1002 group 1001

	ip route replace 2001:db8:2::/64 vrf vrf-r1 nhid 1001
	ping6_do $h1 2001:db8:2::2
	check_fail $? "ping did not fail when using a blackhole nexthop"

	ip route replace 2001:db8:2::/64 vrf vrf-r1 nhid 1002
	ping6_do $h1 2001:db8:2::2
	check_fail $? "ping did not fail when using a blackhole nexthop group"

	ip route replace 2001:db8:2::/64 vrf vrf-r1 nhid 106
	ping6_do $h1 2001:db8:2::2
	check_err $? "ping failed with a valid nexthop"

	log_test "IPv6 blackhole ping"

	ip nexthop del id 1002
	ip -6 nexthop del id 1001
}

nh_stats_test_v4()
{
	__nh_stats_test_v4 mpath
}

nh_stats_test_v6()
{
	__nh_stats_test_v6 mpath
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp11=${NETIFS[p2]}

	rp12=${NETIFS[p3]}
	rp22=${NETIFS[p4]}

	rp13=${NETIFS[p5]}
	rp23=${NETIFS[p6]}

	rp21=${NETIFS[p7]}
	h2=${NETIFS[p8]}

	vrf_prepare

	h1_create
	h2_create

	router1_create
	router2_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router2_destroy
	router1_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 198.51.100.2
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:2::2
}

ip nexthop ls >/dev/null 2>&1
if [ $? -ne 0 ]; then
	echo "Nexthop objects not supported; skipping tests"
	exit $ksft_skip
fi

trap cleanup EXIT

setup_prepare
setup_wait
routing_nh_obj

tests_run

exit $EXIT_STATUS
