#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="standalone vlan_unaware_bridge vlan_aware_bridge test_vlan \
	   vlan_over_vlan_unaware_bridged_port vlan_over_vlan_aware_bridged_port \
	   vlan_over_vlan_unaware_bridge vlan_over_vlan_aware_bridge"
NUM_NETIFS=2
PING_COUNT=1
REQUIRE_MTOOLS=yes

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

PTP_1588_L2_SYNC=" \
01:1b:19:00:00:00 00:00:de:ad:be:ef 88:f7 00 02 \
00 2c 00 00 02 00 00 00 00 00 00 00 00 00 00 00 \
00 00 3e 37 63 ff fe cf 17 0e 00 01 00 00 00 00 \
00 00 00 00 00 00 00 00 00 00"
PTP_1588_L2_FOLLOW_UP=" \
01:1b:19:00:00:00 00:00:de:ad:be:ef 88:f7 08 02 \
00 2c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
00 00 3e 37 63 ff fe cf 17 0e 00 01 00 00 02 00 \
00 00 66 83 c5 f1 17 97 ed f0"
PTP_1588_L2_PDELAY_REQ=" \
01:80:c2:00:00:0e 00:00:de:ad:be:ef 88:f7 02 02 \
00 36 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
00 00 3e 37 63 ff fe cf 17 0e 00 01 00 06 05 7f \
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
00 00 00 00"
PTP_1588_IPV4_SYNC=" \
01:00:5e:00:01:81 00:00:de:ad:be:ef 08:00 45 00 \
00 48 0a 9a 40 00 01 11 cb 88 c0 00 02 01 e0 00 \
01 81 01 3f 01 3f 00 34 a3 c8 00 02 00 2c 00 00 \
02 00 00 00 00 00 00 00 00 00 00 00 00 00 3e 37 \
63 ff fe cf 17 0e 00 01 00 00 00 00 00 00 00 00 \
00 00 00 00 00 00"
PTP_1588_IPV4_FOLLOW_UP="
01:00:5e:00:01:81 00:00:de:ad:be:ef 08:00 45 00 \
00 48 0a 9b 40 00 01 11 cb 87 c0 00 02 01 e0 00 \
01 81 01 40 01 40 00 34 a3 c8 08 02 00 2c 00 00 \
00 00 00 00 00 00 00 00 00 00 00 00 00 00 3e 37 \
63 ff fe cf 17 0e 00 01 00 00 02 00 00 00 66 83 \
c6 0f 1d 9a 61 87"
PTP_1588_IPV4_PDELAY_REQ=" \
01:00:5e:00:00:6b 00:00:de:ad:be:ef 08:00 45 00 \
00 52 35 a9 40 00 01 11 a1 85 c0 00 02 01 e0 00 \
00 6b 01 3f 01 3f 00 3e a2 bc 02 02 00 36 00 00 \
00 00 00 00 00 00 00 00 00 00 00 00 00 00 3e 37 \
63 ff fe cf 17 0e 00 01 00 01 05 7f 00 00 00 00 \
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
PTP_1588_IPV6_SYNC=" \
33:33:00:00:01:81 00:00:de:ad:be:ef 86:dd 60 06 \
7c 2f 00 36 11 01 20 01 0d b8 00 01 00 00 00 00 \
00 00 00 00 00 01 ff 0e 00 00 00 00 00 00 00 00 \
00 00 00 00 01 81 01 3f 01 3f 00 36 2e 92 00 02 \
00 2c 00 00 02 00 00 00 00 00 00 00 00 00 00 00 \
00 00 3e 37 63 ff fe cf 17 0e 00 01 00 00 00 00 \
00 00 00 00 00 00 00 00 00 00 00 00"
PTP_1588_IPV6_FOLLOW_UP=" \
33:33:00:00:01:81 00:00:de:ad:be:ef 86:dd 60 0a \
00 bc 00 36 11 01 20 01 0d b8 00 01 00 00 00 00 \
00 00 00 00 00 01 ff 0e 00 00 00 00 00 00 00 00 \
00 00 00 00 01 81 01 40 01 40 00 36 2e 92 08 02 \
00 2c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
00 00 3e 37 63 ff fe cf 17 0e 00 01 00 00 02 00 \
00 00 66 83 c6 2a 32 09 bd 74 00 00"
PTP_1588_IPV6_PDELAY_REQ=" \
33:33:00:00:00:6b 00:00:de:ad:be:ef 86:dd 60 0c \
5c fd 00 40 11 01 fe 80 00 00 00 00 00 00 3c 37 \
63 ff fe cf 17 0e ff 02 00 00 00 00 00 00 00 00 \
00 00 00 00 00 6b 01 3f 01 3f 00 40 b4 54 02 02 \
00 36 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
00 00 3e 37 63 ff fe cf 17 0e 00 01 00 01 05 7f \
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
00 00 00 00 00 00"

# Disable promisc to ensure we don't receive unknown MAC DA packets
export TCPDUMP_EXTRA_FLAGS="-pl"

h1=${NETIFS[p1]}
h2=${NETIFS[p2]}

send_raw()
{
	local if_name=$1; shift
	local pkt="$1"; shift
	local smac=$(mac_get $if_name)

	pkt="${pkt/00:00:de:ad:be:ef/$smac}"

	$MZ -q $if_name "$pkt"
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
	local skip_ptp=$1; shift
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

	if [ $skip_ptp = false ]; then
		ip maddress add 01:1b:19:00:00:00 dev $rcv_if_name
		send_raw $send_if_name "$PTP_1588_L2_SYNC"
		send_raw $send_if_name "$PTP_1588_L2_FOLLOW_UP"
		ip maddress del 01:1b:19:00:00:00 dev $rcv_if_name

		ip maddress add 01:80:c2:00:00:0e dev $rcv_if_name
		send_raw $send_if_name "$PTP_1588_L2_PDELAY_REQ"
		ip maddress del 01:80:c2:00:00:0e dev $rcv_if_name

		mc_join $rcv_if_name 224.0.1.129
		send_raw $send_if_name "$PTP_1588_IPV4_SYNC"
		send_raw $send_if_name "$PTP_1588_IPV4_FOLLOW_UP"
		mc_leave

		mc_join $rcv_if_name 224.0.0.107
		send_raw $send_if_name "$PTP_1588_IPV4_PDELAY_REQ"
		mc_leave

		mc_join $rcv_if_name ff0e::181
		send_raw $send_if_name "$PTP_1588_IPV6_SYNC"
		send_raw $send_if_name "$PTP_1588_IPV6_FOLLOW_UP"
		mc_leave

		mc_join $rcv_if_name ff02::6b
		send_raw $send_if_name "$PTP_1588_IPV6_PDELAY_REQ"
		mc_leave
	fi

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

	if [ $skip_ptp = false ]; then
		check_rcv $rcv_if_name "1588v2 over L2 transport, Sync" \
			"ethertype PTP (0x88f7).* PTPv2.* msg type : sync msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over L2 transport, Follow-Up" \
			"ethertype PTP (0x88f7).* PTPv2.* msg type : follow up msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over L2 transport, Peer Delay Request" \
			"ethertype PTP (0x88f7).* PTPv2.* msg type : peer delay req msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over IPv4, Sync" \
			"ethertype IPv4 (0x0800).* PTPv2.* msg type : sync msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over IPv4, Follow-Up" \
			"ethertype IPv4 (0x0800).* PTPv2.* msg type : follow up msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over IPv4, Peer Delay Request" \
			"ethertype IPv4 (0x0800).* PTPv2.* msg type : peer delay req msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over IPv6, Sync" \
			"ethertype IPv6 (0x86dd).* PTPv2.* msg type : sync msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over IPv6, Follow-Up" \
			"ethertype IPv6 (0x86dd).* PTPv2.* msg type : follow up msg" \
			true "$test_name"

		check_rcv $rcv_if_name "1588v2 over IPv6, Peer Delay Request" \
			"ethertype IPv6 (0x86dd).* PTPv2.* msg type : peer delay req msg" \
			true "$test_name"
	fi

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
	local skip_ptp=false

	if [ $(has_unicast_flt $h2) = yes ]; then
		no_unicast_flt=false
	fi

	h1_create
	h2_create
	macvlan_create $h2

	run_test $h1 $h2 $skip_ptp $no_unicast_flt "$h2"

	macvlan_destroy
	h2_destroy
	h1_destroy
}

test_bridge()
{
	local no_unicast_flt=true
	local vlan_filtering=$1
	local skip_ptp=true

	h1_create
	bridge_create $vlan_filtering
	simple_if_init br0 $H2_IPV4/24 $H2_IPV6/64
	macvlan_create br0

	run_test $h1 br0 $skip_ptp $no_unicast_flt \
		"vlan_filtering=$vlan_filtering bridge"

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
	local skip_ptp=false

	if [ $(has_unicast_flt $h2) = yes ]; then
		no_unicast_flt=false
	fi

	h1_vlan_create
	h2_vlan_create
	macvlan_create $h2.100

	run_test $h1.100 $h2.100 $skip_ptp $no_unicast_flt "VLAN upper"

	macvlan_destroy
	h2_vlan_destroy
	h1_vlan_destroy
}

vlan_over_bridged_port()
{
	local no_unicast_flt=true
	local vlan_filtering=$1
	local skip_ptp=false

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

	run_test $h1.100 $h2.100 $skip_ptp $no_unicast_flt \
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
	local skip_ptp=true

	h1_vlan_create
	bridge_create $vlan_filtering
	simple_if_init br0
	vlan_create br0 100 vbr0 $H2_IPV4/24 $H2_IPV6/64
	macvlan_create br0.100

	if [ $vlan_filtering = 1 ]; then
		bridge vlan add dev $h2 vid 100 master
		bridge vlan add dev br0 vid 100 self
	fi

	run_test $h1.100 br0.100 $skip_ptp $no_unicast_flt \
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

	ip link set $h2 down
	ip link set $h1 down

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
