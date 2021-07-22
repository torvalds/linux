#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test a "one-armed router" [1] scenario. Packets forwarded between H1 and H2
# should be forwarded by the ASIC, but also trapped so that ICMP redirect
# packets could be potentially generated.
#
# 1. https://en.wikipedia.org/wiki/One-armed_router
#
# +---------------------------------+
# | H1 (vrf)                        |
# |    + $h1                        |
# |    | 192.0.2.1/24               |
# |    | 2001:db8:1::1/64           |
# |    |                            |
# |    |  default via 192.0.2.2     |
# |    |  default via 2001:db8:1::2 |
# +----|----------------------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# | +--|--------------------------------------------------------------------+ |
# | |  + $swp1                   BR0 (802.1d)                               | |
# | |                                                                       | |
# | |                            192.0.2.2/24                               | |
# | |                          2001:db8:1::2/64                             | |
# | |                           198.51.100.2/24                             | |
# | |                          2001:db8:2::2/64                             | |
# | |                                                                       | |
# | |  + $swp2                                                              | |
# | +--|--------------------------------------------------------------------+ |
# |    |                                                                      |
# +----|----------------------------------------------------------------------+
#      |
# +----|----------------------------+
# |    |  default via 198.51.100.2  |
# |    |  default via 2001:db8:2::2 |
# |    |                            |
# |    | 2001:db8:2::1/64           |
# |    | 198.51.100.1/24            |
# |    + $h2                        |
# | H2 (vrf)                        |
# +---------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="ping_ipv4 ping_ipv6 fwd_mark_ipv4 fwd_mark_ipv6"
NUM_NETIFS=4
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64

	ip -4 route add default vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add default vrf v$h1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del default vrf v$h1 nexthop via 2001:db8:1::2
	ip -4 route del default vrf v$h1 nexthop via 192.0.2.2

	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 198.51.100.1/24 2001:db8:2::1/64

	ip -4 route add default vrf v$h2 nexthop via 198.51.100.2
	ip -6 route add default vrf v$h2 nexthop via 2001:db8:2::2
}

h2_destroy()
{
	ip -6 route del default vrf v$h2 nexthop via 2001:db8:2::2
	ip -4 route del default vrf v$h2 nexthop via 198.51.100.2

	simple_if_fini $h2 198.51.100.1/24 2001:db8:2::1/64
}

switch_create()
{
	ip link add name br0 type bridge mcast_snooping 0
	ip link set dev br0 up

	ip link set dev $swp1 master br0
	ip link set dev $swp1 up
	ip link set dev $swp2 master br0
	ip link set dev $swp2 up

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact

	__addr_add_del br0 add 192.0.2.2/24 2001:db8:1::2/64
	__addr_add_del br0 add 198.51.100.2/24 2001:db8:2::2/64
}

switch_destroy()
{
	__addr_add_del br0 del 198.51.100.2/24 2001:db8:2::2/64
	__addr_add_del br0 del 192.0.2.2/24 2001:db8:1::2/64

	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link set dev br0 down
	ip link del dev br0
}

ping_ipv4()
{
	ping_test $h1 198.51.100.1 ": h1->h2"
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:2::1 ": h1->h2"
}

fwd_mark_ipv4()
{
	# Transmit packets from H1 to H2 and make sure they are trapped at
	# swp1 due to loopback error, but only forwarded by the ASIC through
	# swp2

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		skip_hw dst_ip 198.51.100.1 ip_proto udp dst_port 52768 \
		action pass

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 flower \
		skip_hw dst_ip 198.51.100.1 ip_proto udp dst_port 52768 \
		action pass

	tc filter add dev $swp2 egress protocol ip pref 2 handle 102 flower \
		skip_sw dst_ip 198.51.100.1 ip_proto udp dst_port 52768 \
		action pass

	ip vrf exec v$h1 $MZ $h1 -c 10 -d 100msec -p 64 -A 192.0.2.1 \
		-B 198.51.100.1 -t udp dp=52768,sp=42768 -q

	RET=0

	tc_check_packets "dev $swp1 ingress" 101 10
	check_err $?

	log_test "fwd mark: trapping IPv4 packets due to LBERROR"

	RET=0

	tc_check_packets "dev $swp2 egress" 101 0
	check_err $?

	log_test "fwd mark: forwarding IPv4 packets in software"

	RET=0

	tc_check_packets "dev $swp2 egress" 102 10
	check_err $?

	log_test "fwd mark: forwarding IPv4 packets in hardware"

	tc filter del dev $swp2 egress protocol ip pref 2 handle 102 flower
	tc filter del dev $swp2 egress protocol ip pref 1 handle 101 flower
	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower
}

fwd_mark_ipv6()
{
	tc filter add dev $swp1 ingress protocol ipv6 pref 1 handle 101 flower \
		skip_hw dst_ip 2001:db8:2::1 ip_proto udp dst_port 52768 \
		action pass

	tc filter add dev $swp2 egress protocol ipv6 pref 1 handle 101 flower \
		skip_hw dst_ip 2001:db8:2::1 ip_proto udp dst_port 52768 \
		action pass

	tc filter add dev $swp2 egress protocol ipv6 pref 2 handle 102 flower \
		skip_sw dst_ip 2001:db8:2::1 ip_proto udp dst_port 52768 \
		action pass

	ip vrf exec v$h1 $MZ $h1 -6 -c 10 -d 100msec -p 64 -A 2001:db8:1::1 \
		-B 2001:db8:2::1 -t udp dp=52768,sp=42768 -q

	RET=0

	tc_check_packets "dev $swp1 ingress" 101 10
	check_err $?

	log_test "fwd mark: trapping IPv6 packets due to LBERROR"

	RET=0

	tc_check_packets "dev $swp2 egress" 101 0
	check_err $?

	log_test "fwd mark: forwarding IPv6 packets in software"

	RET=0

	tc_check_packets "dev $swp2 egress" 102 10
	check_err $?

	log_test "fwd mark: forwarding IPv6 packets in hardware"

	tc filter del dev $swp2 egress protocol ipv6 pref 2 handle 102 flower
	tc filter del dev $swp2 egress protocol ipv6 pref 1 handle 101 flower
	tc filter del dev $swp1 ingress protocol ipv6 pref 1 handle 101 flower
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	sysctl_set net.ipv4.conf.all.accept_redirects 0
	sysctl_set net.ipv6.conf.all.accept_redirects 0

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy

	sysctl_restore net.ipv6.conf.all.accept_redirects
	sysctl_restore net.ipv4.conf.all.accept_redirects

	forwarding_restore
	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
