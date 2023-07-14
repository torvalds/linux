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
	test_l2_miss_unicast
	test_l2_miss_multicast
	test_l2_miss_ll_multicast
	test_l2_miss_broadcast
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
	ip link add name br1 up type bridge
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact

	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster
	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster
	ip link del dev br1
}

test_l2_miss_unicast()
{
	local dmac=00:01:02:03:04:05
	local dip=192.0.2.2
	local sip=192.0.2.1

	RET=0

	# Unknown unicast.
	tc filter add dev $swp2 egress protocol ipv4 handle 101 pref 1 \
	   flower indev $swp1 l2_miss 1 dst_mac $dmac src_ip $sip \
	   dst_ip $dip action pass
	# Known unicast.
	tc filter add dev $swp2 egress protocol ipv4 handle 102 pref 1 \
	   flower indev $swp1 l2_miss 0 dst_mac $dmac src_ip $sip \
	   dst_ip $dip action pass

	# Before adding FDB entry.
	$MZ $h1 -a own -b $dmac -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 1
	check_err $? "Unknown unicast filter was not hit before adding FDB entry"

	tc_check_packets "dev $swp2 egress" 102 0
	check_err $? "Known unicast filter was hit before adding FDB entry"

	# Adding FDB entry.
	bridge fdb replace $dmac dev $swp2 master static

	$MZ $h1 -a own -b $dmac -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 1
	check_err $? "Unknown unicast filter was hit after adding FDB entry"

	tc_check_packets "dev $swp2 egress" 102 1
	check_err $? "Known unicast filter was not hit after adding FDB entry"

	# Deleting FDB entry.
	bridge fdb del $dmac dev $swp2 master static

	$MZ $h1 -a own -b $dmac -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 2
	check_err $? "Unknown unicast filter was not hit after deleting FDB entry"

	tc_check_packets "dev $swp2 egress" 102 1
	check_err $? "Known unicast filter was hit after deleting FDB entry"

	tc filter del dev $swp2 egress protocol ipv4 pref 1 handle 102 flower
	tc filter del dev $swp2 egress protocol ipv4 pref 1 handle 101 flower

	log_test "L2 miss - Unicast"
}

test_l2_miss_multicast_common()
{
	local proto=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local mode=$1; shift
	local name=$1; shift

	RET=0

	# Unregistered multicast.
	tc filter add dev $swp2 egress protocol $proto handle 101 pref 1 \
	   flower indev $swp1 l2_miss 1 src_ip $sip dst_ip $dip \
	   action pass
	# Registered multicast.
	tc filter add dev $swp2 egress protocol $proto handle 102 pref 1 \
	   flower indev $swp1 l2_miss 0 src_ip $sip dst_ip $dip \
	   action pass

	# Before adding MDB entry.
	$MZ $mode $h1 -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 1
	check_err $? "Unregistered multicast filter was not hit before adding MDB entry"

	tc_check_packets "dev $swp2 egress" 102 0
	check_err $? "Registered multicast filter was hit before adding MDB entry"

	# Adding MDB entry.
	bridge mdb replace dev br1 port $swp2 grp $dip permanent

	$MZ $mode $h1 -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 1
	check_err $? "Unregistered multicast filter was hit after adding MDB entry"

	tc_check_packets "dev $swp2 egress" 102 1
	check_err $? "Registered multicast filter was not hit after adding MDB entry"

	# Deleting MDB entry.
	bridge mdb del dev br1 port $swp2 grp $dip

	$MZ $mode $h1 -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 2
	check_err $? "Unregistered multicast filter was not hit after deleting MDB entry"

	tc_check_packets "dev $swp2 egress" 102 1
	check_err $? "Registered multicast filter was hit after deleting MDB entry"

	tc filter del dev $swp2 egress protocol $proto pref 1 handle 102 flower
	tc filter del dev $swp2 egress protocol $proto pref 1 handle 101 flower

	log_test "L2 miss - Multicast ($name)"
}

test_l2_miss_multicast_ipv4()
{
	local proto="ipv4"
	local sip=192.0.2.1
	local dip=239.1.1.1
	local mode="-4"
	local name="IPv4"

	test_l2_miss_multicast_common $proto $sip $dip $mode $name
}

test_l2_miss_multicast_ipv6()
{
	local proto="ipv6"
	local sip=2001:db8:1::1
	local dip=ff0e::1
	local mode="-6"
	local name="IPv6"

	test_l2_miss_multicast_common $proto $sip $dip $mode $name
}

test_l2_miss_multicast()
{
	# Configure $swp2 as a multicast router port so that it will forward
	# both registered and unregistered multicast traffic.
	bridge link set dev $swp2 mcast_router 2

	# Forwarding according to MDB entries only takes place when the bridge
	# detects that there is a valid querier in the network. Set the bridge
	# as the querier and assign it a valid IPv6 link-local address to be
	# used as the source address for MLD queries.
	ip link set dev br1 type bridge mcast_querier 1
	ip -6 address add fe80::1/64 nodad dev br1
	# Wait the default Query Response Interval (10 seconds) for the bridge
	# to determine that there are no other queriers in the network.
	sleep 10

	test_l2_miss_multicast_ipv4
	test_l2_miss_multicast_ipv6

	ip -6 address del fe80::1/64 dev br1
	ip link set dev br1 type bridge mcast_querier 0
	bridge link set dev $swp2 mcast_router 1
}

test_l2_miss_multicast_common2()
{
	local name=$1; shift
	local dmac=$1; shift
	local dip=224.0.0.1
	local sip=192.0.2.1

}

test_l2_miss_ll_multicast_common()
{
	local proto=$1; shift
	local dmac=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local mode=$1; shift
	local name=$1; shift

	RET=0

	tc filter add dev $swp2 egress protocol $proto handle 101 pref 1 \
	   flower indev $swp1 l2_miss 1 dst_mac $dmac src_ip $sip \
	   dst_ip $dip action pass

	$MZ $mode $h1 -a own -b $dmac -t ip -A $sip -B $dip -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 1
	check_err $? "Filter was not hit"

	tc filter del dev $swp2 egress protocol $proto pref 1 handle 101 flower

	log_test "L2 miss - Link-local multicast ($name)"
}

test_l2_miss_ll_multicast_ipv4()
{
	local proto=ipv4
	local dmac=01:00:5e:00:00:01
	local sip=192.0.2.1
	local dip=224.0.0.1
	local mode="-4"
	local name="IPv4"

	test_l2_miss_ll_multicast_common $proto $dmac $sip $dip $mode $name
}

test_l2_miss_ll_multicast_ipv6()
{
	local proto=ipv6
	local dmac=33:33:00:00:00:01
	local sip=2001:db8:1::1
	local dip=ff02::1
	local mode="-6"
	local name="IPv6"

	test_l2_miss_ll_multicast_common $proto $dmac $sip $dip $mode $name
}

test_l2_miss_ll_multicast()
{
	test_l2_miss_ll_multicast_ipv4
	test_l2_miss_ll_multicast_ipv6
}

test_l2_miss_broadcast()
{
	local dmac=ff:ff:ff:ff:ff:ff
	local smac=00:01:02:03:04:05

	RET=0

	tc filter add dev $swp2 egress protocol all handle 101 pref 1 \
	   flower l2_miss 1 dst_mac $dmac src_mac $smac \
	   action pass
	tc filter add dev $swp2 egress protocol all handle 102 pref 1 \
	   flower l2_miss 0 dst_mac $dmac src_mac $smac \
	   action pass

	$MZ $h1 -a $smac -b $dmac -c 1 -p 100 -q

	tc_check_packets "dev $swp2 egress" 101 0
	check_err $? "L2 miss filter was hit when should not"

	tc_check_packets "dev $swp2 egress" 102 1
	check_err $? "L2 no miss filter was not hit when should"

	tc filter del dev $swp2 egress protocol all pref 1 handle 102 flower
	tc filter del dev $swp2 egress protocol all pref 1 handle 101 flower

	log_test "L2 miss - Broadcast"
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
