#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

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

trap cleanup EXIT

setup_prepare
setup_wait

ping_test $h1 198.51.100.2
ping6_test $h1 2001:db8:2::2

exit $EXIT_STATUS
