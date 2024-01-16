#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="standalone bridge"
NUM_NETIFS=2
PING_COUNT=1
REQUIRE_MTOOLS=yes
REQUIRE_MZ=no

source lib.sh

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

BRIDGE_ADDR="00:00:de:ad:be:ee"
MACVLAN_ADDR="00:00:de:ad:be:ef"
UNKNOWN_UC_ADDR1="de:ad:be:ef:ee:03"
UNKNOWN_UC_ADDR2="de:ad:be:ef:ee:04"
UNKNOWN_UC_ADDR3="de:ad:be:ef:ee:05"
JOINED_IPV4_MC_ADDR="225.1.2.3"
UNKNOWN_IPV4_MC_ADDR1="225.1.2.4"
UNKNOWN_IPV4_MC_ADDR2="225.1.2.5"
UNKNOWN_IPV4_MC_ADDR3="225.1.2.6"
JOINED_IPV6_MC_ADDR="ff2e::0102:0304"
UNKNOWN_IPV6_MC_ADDR1="ff2e::0102:0305"
UNKNOWN_IPV6_MC_ADDR2="ff2e::0102:0306"
UNKNOWN_IPV6_MC_ADDR3="ff2e::0102:0307"

JOINED_MACV4_MC_ADDR="01:00:5e:01:02:03"
UNKNOWN_MACV4_MC_ADDR1="01:00:5e:01:02:04"
UNKNOWN_MACV4_MC_ADDR2="01:00:5e:01:02:05"
UNKNOWN_MACV4_MC_ADDR3="01:00:5e:01:02:06"
JOINED_MACV6_MC_ADDR="33:33:01:02:03:04"
UNKNOWN_MACV6_MC_ADDR1="33:33:01:02:03:05"
UNKNOWN_MACV6_MC_ADDR2="33:33:01:02:03:06"
UNKNOWN_MACV6_MC_ADDR3="33:33:01:02:03:07"

NON_IP_MC="01:02:03:04:05:06"
NON_IP_PKT="00:04 48:45:4c:4f"
BC="ff:ff:ff:ff:ff:ff"

# Disable promisc to ensure we don't receive unknown MAC DA packets
export TCPDUMP_EXTRA_FLAGS="-pl"

h1=${NETIFS[p1]}
h2=${NETIFS[p2]}

send_non_ip()
{
	local if_name=$1
	local smac=$2
	local dmac=$3

	$MZ -q $if_name "$dmac $smac $NON_IP_PKT"
}

send_uc_ipv4()
{
	local if_name=$1
	local dmac=$2

	ip neigh add $H2_IPV4 lladdr $dmac dev $if_name
	ping_do $if_name $H2_IPV4
	ip neigh del $H2_IPV4 dev $if_name
}

check_rcv()
{
	local if_name=$1
	local type=$2
	local pattern=$3
	local should_receive=$4
	local should_fail=

	[ $should_receive = true ] && should_fail=0 || should_fail=1
	RET=0

	tcpdump_show $if_name | grep -q "$pattern"

	check_err_fail "$should_fail" "$?" "reception"

	log_test "$if_name: $type"
}

mc_route_prepare()
{
	local if_name=$1
	local vrf_name=$(master_name_get $if_name)

	ip route add 225.100.1.0/24 dev $if_name vrf $vrf_name
	ip -6 route add ff2e::/64 dev $if_name vrf $vrf_name
}

mc_route_destroy()
{
	local if_name=$1
	local vrf_name=$(master_name_get $if_name)

	ip route del 225.100.1.0/24 dev $if_name vrf $vrf_name
	ip -6 route del ff2e::/64 dev $if_name vrf $vrf_name
}

run_test()
{
	local rcv_if_name=$1
	local smac=$(mac_get $h1)
	local rcv_dmac=$(mac_get $rcv_if_name)

	tcpdump_start $rcv_if_name

	mc_route_prepare $h1
	mc_route_prepare $rcv_if_name

	send_uc_ipv4 $h1 $rcv_dmac
	send_uc_ipv4 $h1 $MACVLAN_ADDR
	send_uc_ipv4 $h1 $UNKNOWN_UC_ADDR1

	ip link set dev $rcv_if_name promisc on
	send_uc_ipv4 $h1 $UNKNOWN_UC_ADDR2
	mc_send $h1 $UNKNOWN_IPV4_MC_ADDR2
	mc_send $h1 $UNKNOWN_IPV6_MC_ADDR2
	ip link set dev $rcv_if_name promisc off

	mc_join $rcv_if_name $JOINED_IPV4_MC_ADDR
	mc_send $h1 $JOINED_IPV4_MC_ADDR
	mc_leave

	mc_join $rcv_if_name $JOINED_IPV6_MC_ADDR
	mc_send $h1 $JOINED_IPV6_MC_ADDR
	mc_leave

	mc_send $h1 $UNKNOWN_IPV4_MC_ADDR1
	mc_send $h1 $UNKNOWN_IPV6_MC_ADDR1

	ip link set dev $rcv_if_name allmulticast on
	send_uc_ipv4 $h1 $UNKNOWN_UC_ADDR3
	mc_send $h1 $UNKNOWN_IPV4_MC_ADDR3
	mc_send $h1 $UNKNOWN_IPV6_MC_ADDR3
	ip link set dev $rcv_if_name allmulticast off

	mc_route_destroy $rcv_if_name
	mc_route_destroy $h1

	sleep 1

	tcpdump_stop $rcv_if_name

	check_rcv $rcv_if_name "Unicast IPv4 to primary MAC address" \
		"$smac > $rcv_dmac, ethertype IPv4 (0x0800)" \
		true

	check_rcv $rcv_if_name "Unicast IPv4 to macvlan MAC address" \
		"$smac > $MACVLAN_ADDR, ethertype IPv4 (0x0800)" \
		true

	check_rcv $rcv_if_name "Unicast IPv4 to unknown MAC address" \
		"$smac > $UNKNOWN_UC_ADDR1, ethertype IPv4 (0x0800)" \
		false

	check_rcv $rcv_if_name "Unicast IPv4 to unknown MAC address, promisc" \
		"$smac > $UNKNOWN_UC_ADDR2, ethertype IPv4 (0x0800)" \
		true

	check_rcv $rcv_if_name "Unicast IPv4 to unknown MAC address, allmulti" \
		"$smac > $UNKNOWN_UC_ADDR3, ethertype IPv4 (0x0800)" \
		false

	check_rcv $rcv_if_name "Multicast IPv4 to joined group" \
		"$smac > $JOINED_MACV4_MC_ADDR, ethertype IPv4 (0x0800)" \
		true

	check_rcv $rcv_if_name "Multicast IPv4 to unknown group" \
		"$smac > $UNKNOWN_MACV4_MC_ADDR1, ethertype IPv4 (0x0800)" \
		false

	check_rcv $rcv_if_name "Multicast IPv4 to unknown group, promisc" \
		"$smac > $UNKNOWN_MACV4_MC_ADDR2, ethertype IPv4 (0x0800)" \
		true

	check_rcv $rcv_if_name "Multicast IPv4 to unknown group, allmulti" \
		"$smac > $UNKNOWN_MACV4_MC_ADDR3, ethertype IPv4 (0x0800)" \
		true

	check_rcv $rcv_if_name "Multicast IPv6 to joined group" \
		"$smac > $JOINED_MACV6_MC_ADDR, ethertype IPv6 (0x86dd)" \
		true

	check_rcv $rcv_if_name "Multicast IPv6 to unknown group" \
		"$smac > $UNKNOWN_MACV6_MC_ADDR1, ethertype IPv6 (0x86dd)" \
		false

	check_rcv $rcv_if_name "Multicast IPv6 to unknown group, promisc" \
		"$smac > $UNKNOWN_MACV6_MC_ADDR2, ethertype IPv6 (0x86dd)" \
		true

	check_rcv $rcv_if_name "Multicast IPv6 to unknown group, allmulti" \
		"$smac > $UNKNOWN_MACV6_MC_ADDR3, ethertype IPv6 (0x86dd)" \
		true

	tcpdump_cleanup $rcv_if_name
}

h1_create()
{
	simple_if_init $h1 $H1_IPV4/24 $H1_IPV6/64
}

h1_destroy()
{
	simple_if_fini $h1 $H1_IPV4/24 $H1_IPV6/64
}

h2_create()
{
	simple_if_init $h2 $H2_IPV4/24 $H2_IPV6/64
}

h2_destroy()
{
	simple_if_fini $h2 $H2_IPV4/24 $H2_IPV6/64
}

bridge_create()
{
	ip link add br0 type bridge
	ip link set br0 address $BRIDGE_ADDR
	ip link set br0 up

	ip link set $h2 master br0
	ip link set $h2 up

	simple_if_init br0 $H2_IPV4/24 $H2_IPV6/64
}

bridge_destroy()
{
	simple_if_fini br0 $H2_IPV4/24 $H2_IPV6/64

	ip link del br0
}

standalone()
{
	h1_create
	h2_create

	ip link add link $h2 name macvlan0 type macvlan mode private
	ip link set macvlan0 address $MACVLAN_ADDR
	ip link set macvlan0 up

	run_test $h2

	ip link del macvlan0

	h2_destroy
	h1_destroy
}

bridge()
{
	h1_create
	bridge_create

	ip link add link br0 name macvlan0 type macvlan mode private
	ip link set macvlan0 address $MACVLAN_ADDR
	ip link set macvlan0 up

	run_test br0

	ip link del macvlan0

	bridge_destroy
	h1_destroy
}

cleanup()
{
	pre_cleanup
	vrf_cleanup
}

setup_prepare()
{
	vrf_prepare
	# setup_wait() needs this
	ip link set $h1 up
	ip link set $h2 up
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
