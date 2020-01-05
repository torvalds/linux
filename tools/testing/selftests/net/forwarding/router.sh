#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	sip_in_class_e
"

NUM_NETIFS=4
source lib.sh
source tc_common.sh

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

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up

	tc qdisc add dev $rp2 clsact

	ip address add 192.0.2.1/24 dev $rp1
	ip address add 2001:db8:1::1/64 dev $rp1

	ip address add 198.51.100.1/24 dev $rp2
	ip address add 2001:db8:2::1/64 dev $rp2
}

router_destroy()
{
	ip address del 2001:db8:2::1/64 dev $rp2
	ip address del 198.51.100.1/24 dev $rp2

	ip address del 2001:db8:1::1/64 dev $rp1
	ip address del 192.0.2.1/24 dev $rp1

	tc qdisc del dev $rp2 clsact

	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp1mac=$(mac_get $rp1)

	vrf_prepare

	h1_create
	h2_create

	router_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

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

sip_in_class_e()
{
	RET=0

	# Disable rpfilter to prevent packets to be dropped because of it.
	sysctl_set net.ipv4.conf.all.rp_filter 0
	sysctl_set net.ipv4.conf.$rp1.rp_filter 0

	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 \
		flower src_ip 240.0.0.1 ip_proto udp action pass

	$MZ $h1 -t udp "sp=54321,dp=12345" -c 5 -d 1msec \
		-A 240.0.0.1 -b $rp1mac -B 198.51.100.2 -q

	tc_check_packets "dev $rp2 egress" 101 5
	check_err $? "Packets were dropped"

	log_test "Source IP in class E"

	tc filter del dev $rp2 egress protocol ip pref 1 handle 101 flower
	sysctl_restore net.ipv4.conf.$rp1.rp_filter
	sysctl_restore net.ipv4.conf.all.rp_filter
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
