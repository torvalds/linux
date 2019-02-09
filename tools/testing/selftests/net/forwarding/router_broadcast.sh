#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="ping_ipv4"
NUM_NETIFS=6
source lib.sh

h1_create()
{
	vrf_create "vrf-h1"
	ip link set dev $h1 master vrf-h1

	ip link set dev vrf-h1 up
	ip link set dev $h1 up

	ip address add 192.0.2.2/24 dev $h1

	ip route add 198.51.100.0/24 vrf vrf-h1 nexthop via 192.0.2.1
	ip route add 198.51.200.0/24 vrf vrf-h1 nexthop via 192.0.2.1
}

h1_destroy()
{
	ip route del 198.51.200.0/24 vrf vrf-h1
	ip route del 198.51.100.0/24 vrf vrf-h1

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

	ip route add 192.0.2.0/24 vrf vrf-h2 nexthop via 198.51.100.1
	ip route add 198.51.200.0/24 vrf vrf-h2 nexthop via 198.51.100.1
}

h2_destroy()
{
	ip route del 198.51.200.0/24 vrf vrf-h2
	ip route del 192.0.2.0/24 vrf vrf-h2

	ip address del 198.51.100.2/24 dev $h2

	ip link set dev $h2 down
	vrf_destroy "vrf-h2"
}

h3_create()
{
	vrf_create "vrf-h3"
	ip link set dev $h3 master vrf-h3

	ip link set dev vrf-h3 up
	ip link set dev $h3 up

	ip address add 198.51.200.2/24 dev $h3

	ip route add 192.0.2.0/24 vrf vrf-h3 nexthop via 198.51.200.1
	ip route add 198.51.100.0/24 vrf vrf-h3 nexthop via 198.51.200.1
}

h3_destroy()
{
	ip route del 198.51.100.0/24 vrf vrf-h3
	ip route del 192.0.2.0/24 vrf vrf-h3

	ip address del 198.51.200.2/24 dev $h3

	ip link set dev $h3 down
	vrf_destroy "vrf-h3"
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up
	ip link set dev $rp3 up

	ip address add 192.0.2.1/24 dev $rp1

	ip address add 198.51.100.1/24 dev $rp2
	ip address add 198.51.200.1/24 dev $rp3
}

router_destroy()
{
	ip address del 198.51.200.1/24 dev $rp3
	ip address del 198.51.100.1/24 dev $rp2

	ip address del 192.0.2.1/24 dev $rp1

	ip link set dev $rp3 down
	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare

	h1_create
	h2_create
	h3_create

	router_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

	h3_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

bc_forwarding_disable()
{
	sysctl_set net.ipv4.conf.all.bc_forwarding 0
	sysctl_set net.ipv4.conf.$rp1.bc_forwarding 0
}

bc_forwarding_enable()
{
	sysctl_set net.ipv4.conf.all.bc_forwarding 1
	sysctl_set net.ipv4.conf.$rp1.bc_forwarding 1
}

bc_forwarding_restore()
{
	sysctl_restore net.ipv4.conf.$rp1.bc_forwarding
	sysctl_restore net.ipv4.conf.all.bc_forwarding
}

ping_test_from()
{
	local oif=$1
	local dip=$2
	local from=$3
	local fail=${4:-0}

	RET=0

	log_info "ping $dip, expected reply from $from"
	ip vrf exec $(master_name_get $oif) \
	$PING -I $oif $dip -c 10 -i 0.1 -w 2 -b 2>&1 | grep $from &> /dev/null
	check_err_fail $fail $?
}

ping_ipv4()
{
	sysctl_set net.ipv4.icmp_echo_ignore_broadcasts 0

	bc_forwarding_disable
	log_info "bc_forwarding disabled on r1 =>"
	ping_test_from $h1 198.51.100.255 192.0.2.1
	log_test "h1 -> net2: reply from r1 (not forwarding)"
	ping_test_from $h1 198.51.200.255 192.0.2.1
	log_test "h1 -> net3: reply from r1 (not forwarding)"
	ping_test_from $h1 192.0.2.255 192.0.2.1
	log_test "h1 -> net1: reply from r1 (not dropping)"
	ping_test_from $h1 255.255.255.255 192.0.2.1
	log_test "h1 -> 255.255.255.255: reply from r1 (not forwarding)"

	ping_test_from $h2 192.0.2.255 198.51.100.1
	log_test "h2 -> net1: reply from r1 (not forwarding)"
	ping_test_from $h2 198.51.200.255 198.51.100.1
	log_test "h2 -> net3: reply from r1 (not forwarding)"
	ping_test_from $h2 198.51.100.255 198.51.100.1
	log_test "h2 -> net2: reply from r1 (not dropping)"
	ping_test_from $h2 255.255.255.255 198.51.100.1
	log_test "h2 -> 255.255.255.255: reply from r1 (not forwarding)"
	bc_forwarding_restore

	bc_forwarding_enable
	log_info "bc_forwarding enabled on r1 =>"
	ping_test_from $h1 198.51.100.255 198.51.100.2
	log_test "h1 -> net2: reply from h2 (forwarding)"
	ping_test_from $h1 198.51.200.255 198.51.200.2
	log_test "h1 -> net3: reply from h3 (forwarding)"
	ping_test_from $h1 192.0.2.255 192.0.2.1 1
	log_test "h1 -> net1: no reply (dropping)"
	ping_test_from $h1 255.255.255.255 192.0.2.1
	log_test "h1 -> 255.255.255.255: reply from r1 (not forwarding)"

	ping_test_from $h2 192.0.2.255 192.0.2.2
	log_test "h2 -> net1: reply from h1 (forwarding)"
	ping_test_from $h2 198.51.200.255 198.51.200.2
	log_test "h2 -> net3: reply from h3 (forwarding)"
	ping_test_from $h2 198.51.100.255 198.51.100.1 1
	log_test "h2 -> net2: no reply (dropping)"
	ping_test_from $h2 255.255.255.255 198.51.100.1
	log_test "h2 -> 255.255.255.255: reply from r1 (not forwarding)"
	bc_forwarding_restore

	sysctl_restore net.ipv4.icmp_echo_ignore_broadcasts
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
