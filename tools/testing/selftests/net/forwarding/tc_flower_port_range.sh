#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                             +----------------------+
# | H1 (vrf)              |                             | H2 (vrf)             |
# |    + $h1              |                             |              $h2 +   |
# |    | 192.0.2.1/28     |                             |     192.0.2.2/28 |   |
# |    | 2001:db8:1::1/64 |                             | 2001:db8:1::2/64 |   |
# +----|------------------+                             +------------------|---+
#      |                                                                   |
# +----|-------------------------------------------------------------------|---+
# | SW |                                                                   |   |
# |  +-|-------------------------------------------------------------------|-+ |
# |  | + $swp1                       BR                              $swp2 + | |
# |  +-----------------------------------------------------------------------+ |
# +----------------------------------------------------------------------------+

ALL_TESTS="
	test_port_range_ipv4_udp
	test_port_range_ipv4_tcp
	test_port_range_ipv6_udp
	test_port_range_ipv6_tcp
"

NUM_NETIFS=4
source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28 2001:db8:1::1/64
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28 2001:db8:1::2/64
}

h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/28 2001:db8:1::2/64
}

switch_create()
{
	ip link add name br1 type bridge
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	ip link set dev $swp2 master br1
	ip link set dev $swp2 up
	ip link set dev br1 up

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

	ip link set dev br1 down
	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster
	ip link del dev br1
}

__test_port_range()
{
	local proto=$1; shift
	local ip_proto=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local mode=$1; shift
	local name=$1; shift
	local dmac=$(mac_get $h2)
	local smac=$(mac_get $h1)
	local sport_min=100
	local sport_max=200
	local sport_mid=$((sport_min + (sport_max - sport_min) / 2))
	local dport_min=300
	local dport_max=400
	local dport_mid=$((dport_min + (dport_max - dport_min) / 2))

	RET=0

	tc filter add dev $swp1 ingress protocol $proto handle 101 pref 1 \
		flower src_ip $sip dst_ip $dip ip_proto $ip_proto \
		src_port $sport_min-$sport_max \
		dst_port $dport_min-$dport_max \
		action pass
	tc filter add dev $swp2 egress protocol $proto handle 101 pref 1 \
		flower src_ip $sip dst_ip $dip ip_proto $ip_proto \
		src_port $sport_min-$sport_max \
		dst_port $dport_min-$dport_max \
		action drop

	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$sport_min,dp=$dport_min"
	tc_check_packets "dev $swp1 ingress" 101 1
	check_err $? "Ingress filter not hit with minimum ports"
	tc_check_packets "dev $swp2 egress" 101 1
	check_err $? "Egress filter not hit with minimum ports"

	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$sport_mid,dp=$dport_mid"
	tc_check_packets "dev $swp1 ingress" 101 2
	check_err $? "Ingress filter not hit with middle ports"
	tc_check_packets "dev $swp2 egress" 101 2
	check_err $? "Egress filter not hit with middle ports"

	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$sport_max,dp=$dport_max"
	tc_check_packets "dev $swp1 ingress" 101 3
	check_err $? "Ingress filter not hit with maximum ports"
	tc_check_packets "dev $swp2 egress" 101 3
	check_err $? "Egress filter not hit with maximum ports"

	# Send traffic when both ports are out of range and when only one port
	# is out of range.
	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$((sport_min - 1)),dp=$dport_min"
	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$((sport_max + 1)),dp=$dport_min"
	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$sport_min,dp=$((dport_min - 1))"
	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$sport_min,dp=$((dport_max + 1))"
	$MZ $mode $h1 -c 1 -q -p 100 -a $smac -b $dmac -A $sip -B $dip \
		-t $ip_proto "sp=$((sport_max + 1)),dp=$((dport_max + 1))"
	tc_check_packets "dev $swp1 ingress" 101 3
	check_err $? "Ingress filter was hit when should not"
	tc_check_packets "dev $swp2 egress" 101 3
	check_err $? "Egress filter was hit when should not"

	tc filter del dev $swp2 egress protocol $proto pref 1 handle 101 flower
	tc filter del dev $swp1 ingress protocol $proto pref 1 handle 101 flower

	log_test "Port range matching - $name"
}

test_port_range_ipv4_udp()
{
	local proto=ipv4
	local ip_proto=udp
	local sip=192.0.2.1
	local dip=192.0.2.2
	local mode="-4"
	local name="IPv4 UDP"

	__test_port_range $proto $ip_proto $sip $dip $mode "$name"
}

test_port_range_ipv4_tcp()
{
	local proto=ipv4
	local ip_proto=tcp
	local sip=192.0.2.1
	local dip=192.0.2.2
	local mode="-4"
	local name="IPv4 TCP"

	__test_port_range $proto $ip_proto $sip $dip $mode "$name"
}

test_port_range_ipv6_udp()
{
	local proto=ipv6
	local ip_proto=udp
	local sip=2001:db8:1::1
	local dip=2001:db8:1::2
	local mode="-6"
	local name="IPv6 UDP"

	__test_port_range $proto $ip_proto $sip $dip $mode "$name"
}

test_port_range_ipv6_tcp()
{
	local proto=ipv6
	local ip_proto=tcp
	local sip=2001:db8:1::1
	local dip=2001:db8:1::2
	local mode="-6"
	local name="IPv6 TCP"

	__test_port_range $proto $ip_proto $sip $dip $mode "$name"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
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
	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
