#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="standalone two_bridges one_bridge_two_pvids"
NUM_NETIFS=4

source lib.sh

h1=${NETIFS[p1]}
h2=${NETIFS[p3]}
swp1=${NETIFS[p2]}
swp2=${NETIFS[p4]}

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

IPV4_ALLNODES="224.0.0.1"
IPV6_ALLNODES="ff02::1"
MACV4_ALLNODES="01:00:5e:00:00:01"
MACV6_ALLNODES="33:33:00:00:00:01"
NON_IP_MC="01:02:03:04:05:06"
NON_IP_PKT="00:04 48:45:4c:4f"
BC="ff:ff:ff:ff:ff:ff"

# The full 4K VLAN space is too much to check, so strategically pick some
# values which should provide reasonable coverage
vids=(0 1 2 5 10 20 50 100 200 500 1000 1000 2000 4000 4094)

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

send_mc_ipv4()
{
	local if_name=$1

	ping_do $if_name $IPV4_ALLNODES "-I $if_name"
}

send_uc_ipv6()
{
	local if_name=$1
	local dmac=$2

	ip -6 neigh add $H2_IPV6 lladdr $dmac dev $if_name
	ping6_do $if_name $H2_IPV6
	ip -6 neigh del $H2_IPV6 dev $if_name
}

send_mc_ipv6()
{
	local if_name=$1

	ping6_do $if_name $IPV6_ALLNODES%$if_name
}

check_rcv()
{
	local if_name=$1
	local type=$2
	local pattern=$3
	local should_fail=1

	RET=0

	tcpdump_show $if_name | grep -q "$pattern"

	check_err_fail "$should_fail" "$?" "reception"

	log_test "$type"
}

run_test()
{
	local test_name="$1"
	local smac=$(mac_get $h1)
	local dmac=$(mac_get $h2)
	local h1_ipv6_lladdr=$(ipv6_lladdr_get $h1)
	local vid=

	echo "$test_name: Sending packets"

	tcpdump_start $h2

	send_non_ip $h1 $smac $dmac
	send_non_ip $h1 $smac $NON_IP_MC
	send_non_ip $h1 $smac $BC
	send_uc_ipv4 $h1 $dmac
	send_mc_ipv4 $h1
	send_uc_ipv6 $h1 $dmac
	send_mc_ipv6 $h1

	for vid in "${vids[@]}"; do
		vlan_create $h1 $vid
		simple_if_init $h1.$vid $H1_IPV4/24 $H1_IPV6/64

		send_non_ip $h1.$vid $smac $dmac
		send_non_ip $h1.$vid $smac $NON_IP_MC
		send_non_ip $h1.$vid $smac $BC
		send_uc_ipv4 $h1.$vid $dmac
		send_mc_ipv4 $h1.$vid
		send_uc_ipv6 $h1.$vid $dmac
		send_mc_ipv6 $h1.$vid

		simple_if_fini $h1.$vid $H1_IPV4/24 $H1_IPV6/64
		vlan_destroy $h1 $vid
	done

	sleep 1

	echo "$test_name: Checking which packets were received"

	tcpdump_stop $h2

	check_rcv $h2 "$test_name: Unicast non-IP untagged" \
		"$smac > $dmac, 802.3, length 4:"

	check_rcv $h2 "$test_name: Multicast non-IP untagged" \
		"$smac > $NON_IP_MC, 802.3, length 4:"

	check_rcv $h2 "$test_name: Broadcast non-IP untagged" \
		"$smac > $BC, 802.3, length 4:"

	check_rcv $h2 "$test_name: Unicast IPv4 untagged" \
		"$smac > $dmac, ethertype IPv4 (0x0800)"

	check_rcv $h2 "$test_name: Multicast IPv4 untagged" \
		"$smac > $MACV4_ALLNODES, ethertype IPv4 (0x0800).*: $H1_IPV4 > $IPV4_ALLNODES"

	check_rcv $h2 "$test_name: Unicast IPv6 untagged" \
		"$smac > $dmac, ethertype IPv6 (0x86dd).*8: $H1_IPV6 > $H2_IPV6"

	check_rcv $h2 "$test_name: Multicast IPv6 untagged" \
		"$smac > $MACV6_ALLNODES, ethertype IPv6 (0x86dd).*: $h1_ipv6_lladdr > $IPV6_ALLNODES"

	for vid in "${vids[@]}"; do
		check_rcv $h2 "$test_name: Unicast non-IP VID $vid" \
			"$smac > $dmac, ethertype 802.1Q (0x8100).*vlan $vid,.*length 4"

		check_rcv $h2 "$test_name: Multicast non-IP VID $vid" \
			"$smac > $NON_IP_MC, ethertype 802.1Q (0x8100).*vlan $vid,.*length 4"

		check_rcv $h2 "$test_name: Broadcast non-IP VID $vid" \
			"$smac > $BC, ethertype 802.1Q (0x8100).*vlan $vid,.*length 4"

		check_rcv $h2 "$test_name: Unicast IPv4 VID $vid" \
			"$smac > $dmac, ethertype 802.1Q (0x8100).*vlan $vid,.*ethertype IPv4 (0x0800), $H1_IPV4 > $H2_IPV4"

		check_rcv $h2 "$test_name: Multicast IPv4 VID $vid" \
			"$smac > $MACV4_ALLNODES, ethertype 802.1Q (0x8100).*vlan $vid,.*ethertype IPv4 (0x0800), $H1_IPV4 > $IPV4_ALLNODES"

		check_rcv $h2 "$test_name: Unicast IPv6 VID $vid" \
			"$smac > $dmac, ethertype 802.1Q (0x8100).*vlan $vid,.*ethertype IPv6 (0x86dd), $H1_IPV6 > $H2_IPV6"

		check_rcv $h2 "$test_name: Multicast IPv6 VID $vid" \
			"$smac > $MACV6_ALLNODES, ethertype 802.1Q (0x8100).*vlan $vid,.*ethertype IPv6 (0x86dd), $h1_ipv6_lladdr > $IPV6_ALLNODES"
	done

	tcpdump_cleanup $h2
}

standalone()
{
	run_test "Standalone switch ports"
}

two_bridges()
{
	ip link add br0 type bridge && ip link set br0 up
	ip link add br1 type bridge && ip link set br1 up
	ip link set $swp1 master br0
	ip link set $swp2 master br1

	run_test "Switch ports in different bridges"

	ip link del br1
	ip link del br0
}

one_bridge_two_pvids()
{
	ip link add br0 type bridge vlan_filtering 1 vlan_default_pvid 0
	ip link set br0 up
	ip link set $swp1 master br0
	ip link set $swp2 master br0

	bridge vlan add dev $swp1 vid 1 pvid untagged
	bridge vlan add dev $swp2 vid 2 pvid untagged

	run_test "Switch ports in VLAN-aware bridge with different PVIDs"

	ip link del br0
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

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 down
	ip link set dev $swp1 down

	h2_destroy
	h1_destroy

	vrf_cleanup
}

setup_prepare()
{
	vrf_prepare

	h1_create
	h2_create
	# we call simple_if_init from the test itself, but setup_wait expects
	# that we call it from here, and waits until the interfaces are up
	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
