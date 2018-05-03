#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="ping_ipv4 ping_ipv6 multipath_test"
NUM_NETIFS=8
source lib.sh

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

	ip route add 198.51.100.0/24 vrf vrf-r1 \
		nexthop via 169.254.2.22 dev $rp12 \
		nexthop via 169.254.3.23 dev $rp13
	ip route add 2001:db8:2::/64 vrf vrf-r1 \
		nexthop via fe80:2::22 dev $rp12 \
		nexthop via fe80:3::23 dev $rp13
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

	ip route add 192.0.2.0/24 vrf vrf-r2 \
		nexthop via 169.254.2.12 dev $rp22 \
		nexthop via 169.254.3.13 dev $rp23
	ip route add 2001:db8:1::/64 vrf vrf-r2 \
		nexthop via fe80:2::12 dev $rp22 \
		nexthop via fe80:3::13 dev $rp23
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

	ip link set dev $rp23 down
	ip link set dev $rp22 down
	ip link set dev $rp21 down

	vrf_destroy "vrf-r2"
}

multipath_eval()
{
       local desc="$1"
       local weight_rp12=$2
       local weight_rp13=$3
       local packets_rp12=$4
       local packets_rp13=$5
       local weights_ratio packets_ratio diff

       RET=0

       if [[ "$packets_rp12" -eq "0" || "$packets_rp13" -eq "0" ]]; then
              check_err 1 "Packet difference is 0"
              log_test "Multipath"
              log_info "Expected ratio $weights_ratio"
              return
       fi

       if [[ "$weight_rp12" -gt "$weight_rp13" ]]; then
               weights_ratio=$(echo "scale=2; $weight_rp12 / $weight_rp13" \
		       | bc -l)
               packets_ratio=$(echo "scale=2; $packets_rp12 / $packets_rp13" \
		       | bc -l)
       else
               weights_ratio=$(echo "scale=2; $weight_rp13 / $weight_rp12" | \
		       bc -l)
               packets_ratio=$(echo "scale=2; $packets_rp13 / $packets_rp12" | \
		       bc -l)
       fi

       diff=$(echo $weights_ratio - $packets_ratio | bc -l)
       diff=${diff#-}

       test "$(echo "$diff / $weights_ratio > 0.15" | bc -l)" -eq 0
       check_err $? "Too large discrepancy between expected and measured ratios"
       log_test "$desc"
       log_info "Expected ratio $weights_ratio Measured ratio $packets_ratio"
}

multipath4_test()
{
       local desc="$1"
       local weight_rp12=$2
       local weight_rp13=$3
       local t0_rp12 t0_rp13 t1_rp12 t1_rp13
       local packets_rp12 packets_rp13
       local hash_policy

       # Transmit multiple flows from h1 to h2 and make sure they are
       # distributed between both multipath links (rp12 and rp13)
       # according to the configured weights.
       hash_policy=$(sysctl -n net.ipv4.fib_multipath_hash_policy)
       sysctl -q -w net.ipv4.fib_multipath_hash_policy=1
       ip route replace 198.51.100.0/24 vrf vrf-r1 \
               nexthop via 169.254.2.22 dev $rp12 weight $weight_rp12 \
               nexthop via 169.254.3.23 dev $rp13 weight $weight_rp13

       t0_rp12=$(link_stats_tx_packets_get $rp12)
       t0_rp13=$(link_stats_tx_packets_get $rp13)

       ip vrf exec vrf-h1 $MZ -q -p 64 -A 192.0.2.2 -B 198.51.100.2 \
	       -d 1msec -t udp "sp=1024,dp=0-32768"

       t1_rp12=$(link_stats_tx_packets_get $rp12)
       t1_rp13=$(link_stats_tx_packets_get $rp13)

       let "packets_rp12 = $t1_rp12 - $t0_rp12"
       let "packets_rp13 = $t1_rp13 - $t0_rp13"
       multipath_eval "$desc" $weight_rp12 $weight_rp13 $packets_rp12 $packets_rp13

       # Restore settings.
       ip route replace 198.51.100.0/24 vrf vrf-r1 \
               nexthop via 169.254.2.22 dev $rp12 \
               nexthop via 169.254.3.23 dev $rp13
       sysctl -q -w net.ipv4.fib_multipath_hash_policy=$hash_policy
}

multipath6_l4_test()
{
       local desc="$1"
       local weight_rp12=$2
       local weight_rp13=$3
       local t0_rp12 t0_rp13 t1_rp12 t1_rp13
       local packets_rp12 packets_rp13
       local hash_policy

       # Transmit multiple flows from h1 to h2 and make sure they are
       # distributed between both multipath links (rp12 and rp13)
       # according to the configured weights.
       hash_policy=$(sysctl -n net.ipv6.fib_multipath_hash_policy)
       sysctl -q -w net.ipv6.fib_multipath_hash_policy=1

       ip route replace 2001:db8:2::/64 vrf vrf-r1 \
	       nexthop via fe80:2::22 dev $rp12 weight $weight_rp12 \
	       nexthop via fe80:3::23 dev $rp13 weight $weight_rp13

       t0_rp12=$(link_stats_tx_packets_get $rp12)
       t0_rp13=$(link_stats_tx_packets_get $rp13)

       $MZ $h1 -6 -q -p 64 -A 2001:db8:1::2 -B 2001:db8:2::2 \
	       -d 1msec -t udp "sp=1024,dp=0-32768"

       t1_rp12=$(link_stats_tx_packets_get $rp12)
       t1_rp13=$(link_stats_tx_packets_get $rp13)

       let "packets_rp12 = $t1_rp12 - $t0_rp12"
       let "packets_rp13 = $t1_rp13 - $t0_rp13"
       multipath_eval "$desc" $weight_rp12 $weight_rp13 $packets_rp12 $packets_rp13

       ip route replace 2001:db8:2::/64 vrf vrf-r1 \
	       nexthop via fe80:2::22 dev $rp12 \
	       nexthop via fe80:3::23 dev $rp13

       sysctl -q -w net.ipv6.fib_multipath_hash_policy=$hash_policy
}

multipath6_test()
{
       local desc="$1"
       local weight_rp12=$2
       local weight_rp13=$3
       local t0_rp12 t0_rp13 t1_rp12 t1_rp13
       local packets_rp12 packets_rp13

       ip route replace 2001:db8:2::/64 vrf vrf-r1 \
	       nexthop via fe80:2::22 dev $rp12 weight $weight_rp12 \
	       nexthop via fe80:3::23 dev $rp13 weight $weight_rp13

       t0_rp12=$(link_stats_tx_packets_get $rp12)
       t0_rp13=$(link_stats_tx_packets_get $rp13)

       # Generate 16384 echo requests, each with a random flow label.
       for _ in $(seq 1 16384); do
	       ip vrf exec vrf-h1 $PING6 2001:db8:2::2 -F 0 -c 1 -q &> /dev/null
       done

       t1_rp12=$(link_stats_tx_packets_get $rp12)
       t1_rp13=$(link_stats_tx_packets_get $rp13)

       let "packets_rp12 = $t1_rp12 - $t0_rp12"
       let "packets_rp13 = $t1_rp13 - $t0_rp13"
       multipath_eval "$desc" $weight_rp12 $weight_rp13 $packets_rp12 $packets_rp13

       ip route replace 2001:db8:2::/64 vrf vrf-r1 \
	       nexthop via fe80:2::22 dev $rp12 \
	       nexthop via fe80:3::23 dev $rp13
}

multipath_test()
{
	log_info "Running IPv4 multipath tests"
	multipath4_test "ECMP" 1 1
	multipath4_test "Weighted MP 2:1" 2 1
	multipath4_test "Weighted MP 11:45" 11 45

	log_info "Running IPv6 multipath tests"
	multipath6_test "ECMP" 1 1
	multipath6_test "Weighted MP 2:1" 2 1
	multipath6_test "Weighted MP 11:45" 11 45

	log_info "Running IPv6 L4 hash multipath tests"
	multipath6_l4_test "ECMP" 1 1
	multipath6_l4_test "Weighted MP 2:1" 2 1
	multipath6_l4_test "Weighted MP 11:45" 11 45
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

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
