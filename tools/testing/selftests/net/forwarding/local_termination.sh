#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="standalone vlan_unaware_bridge vlan_aware_bridge test_vlan \
	   vlan_over_vlan_unaware_bridged_port vlan_over_vlan_aware_bridged_port \
	   vlan_over_vlan_unaware_bridge vlan_over_vlan_aware_bridge"
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
	local if_name=$1; shift
	local type=$1; shift
	local pattern=$1; shift
	local should_receive=$1; shift
	local test_name="$1"; shift
	local should_fail=

	[ $should_receive = true ] && should_fail=0 || should_fail=1
	RET=0

	tcpdump_show $if_name | grep -q "$pattern"

	check_err_fail "$should_fail" "$?" "reception"

	log_test "$test_name: $type"
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
	local send_if_name=$1; shift
	local rcv_if_name=$1; shift
	local no_unicast_flt=$1; shift
	local test_name="$1"; shift
	local smac=$(mac_get $send_if_name)
	local rcv_dmac=$(mac_get $rcv_if_name)
	local should_receive

	tcpdump_start $rcv_if_name

	mc_route_prepare $send_if_name
	mc_route_prepare $rcv_if_name

	send_uc_ipv4 $send_if_name $rcv_dmac
	send_uc_ipv4 $send_if_name $MACVLAN_ADDR
	send_uc_ipv4 $send_if_name $UNKNOWN_UC_ADDR1

	ip link set dev $rcv_if_name promisc on
	send_uc_ipv4 $send_if_name $UNKNOWN_UC_ADDR2
	mc_send $send_if_name $UNKNOWN_IPV4_MC_ADDR2
	mc_send $send_if_name $UNKNOWN_IPV6_MC_ADDR2
	ip link set dev $rcv_if_name promisc off

	mc_join $rcv_if_name $JOINED_IPV4_MC_ADDR
	mc_send $send_if_name $JOINED_IPV4_MC_ADDR
	mc_leave

	mc_join $rcv_if_name $JOINED_IPV6_MC_ADDR
	mc_send $send_if_name $JOINED_IPV6_MC_ADDR
	mc_leave

	mc_send $send_if_name $UNKNOWN_IPV4_MC_ADDR1
	mc_send $send_if_name $UNKNOWN_IPV6_MC_ADDR1

	ip link set dev $rcv_if_name allmulticast on
	send_uc_ipv4 $send_if_name $UNKNOWN_UC_ADDR3
	mc_send $send_if_name $UNKNOWN_IPV4_MC_ADDR3
	mc_send $send_if_name $UNKNOWN_IPV6_MC_ADDR3
	ip link set dev $rcv_if_name allmulticast off

	mc_route_destroy $rcv_if_name
	mc_route_destroy $send_if_name

	sleep 1

	tcpdump_stop $rcv_if_name

	check_rcv $rcv_if_name "Unicast IPv4 to primary MAC address" \
		"$smac > $rcv_dmac, ethertype IPv4 (0x0800)" \
		true "$test_name"

	check_rcv $rcv_if_name "Unicast IPv4 to macvlan MAC address" \
		"$smac > $MACVLAN_ADDR, ethertype IPv4 (0x0800)" \
		true "$test_name"

	[ $no_unicast_flt = true ] && should_receive=true || should_receive=false
	check_rcv $rcv_if_name "Unicast IPv4 to unknown MAC address" \
		"$smac > $UNKNOWN_UC_ADDR1, ethertype IPv4 (0x0800)" \
		$should_receive "$test_name"

	check_rcv $rcv_if_name "Unicast IPv4 to unknown MAC address, promisc" \
		"$smac > $UNKNOWN_UC_ADDR2, ethertype IPv4 (0x0800)" \
		true "$test_name"

	[ $no_unicast_flt = true ] && should_receive=true || should_receive=false
	check_rcv $rcv_if_name \
		"Unicast IPv4 to unknown MAC address, allmulti" \
		"$smac > $UNKNOWN_UC_ADDR3, ethertype IPv4 (0x0800)" \
		$should_receive "$test_name"

	check_rcv $rcv_if_name "Multicast IPv4 to joined group" \
		"$smac > $JOINED_MACV4_MC_ADDR, ethertype IPv4 (0x0800)" \
		true "$test_name"

	xfail \
		check_rcv $rcv_if_name \
			"Multicast IPv4 to unknown group" \
			"$smac > $UNKNOWN_MACV4_MC_ADDR1, ethertype IPv4 (0x0800)" \
			false "$test_name"

	check_rcv $rcv_if_name "Multicast IPv4 to unknown group, promisc" \
		"$smac > $UNKNOWN_MACV4_MC_ADDR2, ethertype IPv4 (0x0800)" \
		true "$test_name"

	check_rcv $rcv_if_name "Multicast IPv4 to unknown group, allmulti" \
		"$smac > $UNKNOWN_MACV4_MC_ADDR3, ethertype IPv4 (0x0800)" \
		true "$test_name"

	check_rcv $rcv_if_name "Multicast IPv6 to joined group" \
		"$smac > $JOINED_MACV6_MC_ADDR, ethertype IPv6 (0x86dd)" \
		true "$test_name"

	xfail \
		check_rcv $rcv_if_name "Multicast IPv6 to unknown group" \
			"$smac > $UNKNOWN_MACV6_MC_ADDR1, ethertype IPv6 (0x86dd)" \
			false "$test_name"

	check_rcv $rcv_if_name "Multicast IPv6 to unknown group, promisc" \
		"$smac > $UNKNOWN_MACV6_MC_ADDR2, ethertype IPv6 (0x86dd)" \
		true "$test_name"

	check_rcv $rcv_if_name "Multicast IPv6 to unknown group, allmulti" \
		"$smac > $UNKNOWN_MACV6_MC_ADDR3, ethertype IPv6 (0x86dd)" \
		true "$test_name"

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

h1_vlan_create()
{
	simple_if_init $h1
	vlan_create $h1 100 v$h1 $H1_IPV4/24 $H1_IPV6/64
}

h1_vlan_destroy()
{
	vlan_destroy $h1 100
	simple_if_fini $h1
}

h2_vlan_create()
{
	simple_if_init $h2
	vlan_create $h2 100 v$h2 $H2_IPV4/24 $H2_IPV6/64
}

h2_vlan_destroy()
{
	vlan_destroy $h2 100
	simple_if_fini $h2
}

bridge_create()
{
	local vlan_filtering=$1

	ip link add br0 type bridge vlan_filtering $vlan_filtering
	ip link set br0 address $BRIDGE_ADDR
	ip link set br0 up

	ip link set $h2 master br0
	ip link set $h2 up
}

bridge_destroy()
{
	ip link del br0
}

macvlan_create()
{
	local lower=$1

	ip link add link $lower name macvlan0 type macvlan mode private
	ip link set macvlan0 address $MACVLAN_ADDR
	ip link set macvlan0 up
}

macvlan_destroy()
{
	ip link del macvlan0
}

standalone()
{
	local no_unicast_flt=true

	if [ $(has_unicast_flt $h2) = yes ]; then
		no_unicast_flt=false
	fi

	h1_create
	h2_create
	macvlan_create $h2

	run_test $h1 $h2 $no_unicast_flt "$h2"

	macvlan_destroy
	h2_destroy
	h1_destroy
}

test_bridge()
{
	local no_unicast_flt=true
	local vlan_filtering=$1

	h1_create
	bridge_create $vlan_filtering
	simple_if_init br0 $H2_IPV4/24 $H2_IPV6/64
	macvlan_create br0

	run_test $h1 br0 $no_unicast_flt "vlan_filtering=$vlan_filtering bridge"

	macvlan_destroy
	simple_if_fini br0 $H2_IPV4/24 $H2_IPV6/64
	bridge_destroy
	h1_destroy
}

vlan_unaware_bridge()
{
	test_bridge 0
}

vlan_aware_bridge()
{
	test_bridge 1
}

test_vlan()
{
	local no_unicast_flt=true

	if [ $(has_unicast_flt $h2) = yes ]; then
		no_unicast_flt=false
	fi

	h1_vlan_create
	h2_vlan_create
	macvlan_create $h2.100

	run_test $h1.100 $h2.100 $no_unicast_flt "VLAN upper"

	macvlan_destroy
	h2_vlan_destroy
	h1_vlan_destroy
}

vlan_over_bridged_port()
{
	local no_unicast_flt=true
	local vlan_filtering=$1

	# br_manage_promisc() will not force a single vlan_filtering port to
	# promiscuous mode, so we should still expect unicast filtering to take
	# place if the device can do it.
	if [ $(has_unicast_flt $h2) = yes ] && [ $vlan_filtering = 1 ]; then
		no_unicast_flt=false
	fi

	h1_vlan_create
	h2_vlan_create
	bridge_create $vlan_filtering
	macvlan_create $h2.100

	run_test $h1.100 $h2.100 $no_unicast_flt \
		"VLAN over vlan_filtering=$vlan_filtering bridged port"

	macvlan_destroy
	bridge_destroy
	h2_vlan_destroy
	h1_vlan_destroy
}

vlan_over_vlan_unaware_bridged_port()
{
	vlan_over_bridged_port 0
}

vlan_over_vlan_aware_bridged_port()
{
	vlan_over_bridged_port 1
}

vlan_over_bridge()
{
	local no_unicast_flt=true
	local vlan_filtering=$1

	h1_vlan_create
	bridge_create $vlan_filtering
	simple_if_init br0
	vlan_create br0 100 vbr0 $H2_IPV4/24 $H2_IPV6/64
	macvlan_create br0.100

	if [ $vlan_filtering = 1 ]; then
		bridge vlan add dev $h2 vid 100 master
		bridge vlan add dev br0 vid 100 self
	fi

	run_test $h1.100 br0.100 $no_unicast_flt \
		"VLAN over vlan_filtering=$vlan_filtering bridge"

	if [ $vlan_filtering = 1 ]; then
		bridge vlan del dev br0 vid 100 self
		bridge vlan del dev $h2 vid 100 master
	fi

	macvlan_destroy
	vlan_destroy br0 100
	simple_if_fini br0
	bridge_destroy
	h1_vlan_destroy
}

vlan_over_vlan_unaware_bridge()
{
	vlan_over_bridge 0
}

vlan_over_vlan_aware_bridge()
{
	vlan_over_bridge 1
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
