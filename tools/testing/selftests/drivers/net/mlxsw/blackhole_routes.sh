#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that blackhole routes are marked as offloaded and that packets hitting
# them are dropped by the ASIC and not by the kernel.
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
# |    + $rp1                                                                 |
# |        192.0.2.2/24                                                       |
# |        2001:db8:1::2/64                                                   |
# |                                                                           |
# |        2001:db8:2::2/64                                                   |
# |        198.51.100.2/24                                                    |
# |    + $rp2                                                                 |
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

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	blackhole_ipv4
	blackhole_ipv6
"
NUM_NETIFS=4
: ${TIMEOUT:=20000} # ms
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

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up

	tc qdisc add dev $rp1 clsact

	__addr_add_del $rp1 add 192.0.2.2/24 2001:db8:1::2/64
	__addr_add_del $rp2 add 198.51.100.2/24 2001:db8:2::2/64
}

router_destroy()
{
	__addr_add_del $rp2 del 198.51.100.2/24 2001:db8:2::2/64
	__addr_add_del $rp1 del 192.0.2.2/24 2001:db8:1::2/64

	tc qdisc del dev $rp1 clsact

	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

ping_ipv4()
{
	ping_test $h1 198.51.100.1 ": h1->h2"
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:2::1 ": h1->h2"
}

blackhole_ipv4()
{
	# Transmit packets from H1 to H2 and make sure they are dropped by the
	# ASIC and not by the kernel
	RET=0

	ip -4 route add blackhole 198.51.100.0/30
	tc filter add dev $rp1 ingress protocol ip pref 1 handle 101 flower \
		skip_hw dst_ip 198.51.100.1 src_ip 192.0.2.1 ip_proto icmp \
		action pass

	busywait "$TIMEOUT" wait_for_offload ip -4 route show 198.51.100.0/30
	check_err $? "route not marked as offloaded when should"

	ping_do $h1 198.51.100.1
	check_fail $? "ping passed when should not"

	tc_check_packets "dev $rp1 ingress" 101 0
	check_err $? "packets trapped and not dropped by ASIC"

	log_test "IPv4 blackhole route"

	tc filter del dev $rp1 ingress protocol ip pref 1 handle 101 flower
	ip -4 route del blackhole 198.51.100.0/30
}

blackhole_ipv6()
{
	RET=0

	ip -6 route add blackhole 2001:db8:2::/120
	tc filter add dev $rp1 ingress protocol ipv6 pref 1 handle 101 flower \
		skip_hw dst_ip 2001:db8:2::1 src_ip 2001:db8:1::1 \
		ip_proto icmpv6 action pass

	busywait "$TIMEOUT" wait_for_offload ip -6 route show 2001:db8:2::/120
	check_err $? "route not marked as offloaded when should"

	ping6_do $h1 2001:db8:2::1
	check_fail $? "ping passed when should not"

	tc_check_packets "dev $rp1 ingress" 101 0
	check_err $? "packets trapped and not dropped by ASIC"

	log_test "IPv6 blackhole route"

	tc filter del dev $rp1 ingress protocol ipv6 pref 1 handle 101 flower
	ip -6 route del blackhole 2001:db8:2::/120
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
	router_create
}

cleanup()
{
	pre_cleanup

	router_destroy
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
