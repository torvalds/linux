#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking bridge neighbor suppression functionality. The
# topology consists of two bridges (VTEPs) connected using VXLAN. A single
# host is connected to each bridge over multiple VLANs. The test checks that
# ARP/NS messages from the first host are suppressed on the VXLAN port when
# should.
#
# +-----------------------+              +------------------------+
# | h1                    |              | h2                     |
# |                       |              |                        |
# | + eth0.10             |              | + eth0.10              |
# | | 192.0.2.1/28        |              | | 192.0.2.2/28         |
# | | 2001:db8:1::1/64    |              | | 2001:db8:1::2/64     |
# | |                     |              | |                      |
# | |  + eth0.20          |              | |  + eth0.20           |
# | \  | 192.0.2.17/28    |              | \  | 192.0.2.18/28     |
# |  \ | 2001:db8:2::1/64 |              |  \ | 2001:db8:2::2/64  |
# |   \|                  |              |   \|                   |
# |    + eth0             |              |    + eth0              |
# +----|------------------+              +----|-------------------+
#      |                                      |
#      |                                      |
# +----|-------------------------------+ +----|-------------------------------+
# |    + swp1                   + vx0  | |    + swp1                   + vx0  |
# |    |                        |      | |    |                        |      |
# |    |           br0          |      | |    |                        |      |
# |    +------------+-----------+      | |    +------------+-----------+      |
# |                 |                  | |                 |                  |
# |                 |                  | |                 |                  |
# |             +---+---+              | |             +---+---+              |
# |             |       |              | |             |       |              |
# |             |       |              | |             |       |              |
# |             +       +              | |             +       +              |
# |          br0.10  br0.20            | |          br0.10  br0.20            |
# |                                    | |                                    |
# |                 192.0.2.33         | |                 192.0.2.34         |
# |                 + lo               | |                 + lo               |
# |                                    | |                                    |
# |                                    | |                                    |
# |                   192.0.2.49/28    | |    192.0.2.50/28                   |
# |                           veth0 +-------+ veth0                           |
# |                                    | |                                    |
# | sw1                                | | sw2                                |
# +------------------------------------+ +------------------------------------+

source lib.sh
ret=0

# All tests in this script. Can be overridden with -t option.
TESTS="
	neigh_suppress_arp
	neigh_suppress_ns
	neigh_vlan_suppress_arp
	neigh_vlan_suppress_ns
"
VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no

################################################################################
# Utilities

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "$VERBOSE" = "1" ]; then
			echo "    rc=$rc, expected $expected"
		fi

		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
		echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi

	if [ "${PAUSE}" = "yes" ]; then
		echo
		echo "hit enter to continue, 'q' to quit"
		read a
		[ "$a" = "q" ] && exit 1
	fi

	[ "$VERBOSE" = "1" ] && echo
}

run_cmd()
{
	local cmd="$1"
	local out
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		printf "COMMAND: $cmd\n"
		stderr=
	fi

	out=$(eval $cmd $stderr)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi

	return $rc
}

tc_check_packets()
{
	local ns=$1; shift
	local id=$1; shift
	local handle=$1; shift
	local count=$1; shift
	local pkts

	sleep 0.1
	pkts=$(tc -n $ns -j -s filter show $id \
		| jq ".[] | select(.options.handle == $handle) | \
		.options.actions[0].stats.packets")
	[[ $pkts == $count ]]
}

################################################################################
# Setup

setup_topo_ns()
{
	local ns=$1; shift

	ip netns exec $ns sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1
	ip netns exec $ns sysctl -qw net.ipv6.conf.default.ignore_routes_with_linkdown=1
	ip netns exec $ns sysctl -qw net.ipv6.conf.all.accept_dad=0
	ip netns exec $ns sysctl -qw net.ipv6.conf.default.accept_dad=0
}

setup_topo()
{
	local ns

	setup_ns h1 h2 sw1 sw2
	for ns in $h1 $h2 $sw1 $sw2; do
		setup_topo_ns $ns
	done

	ip link add name veth0 type veth peer name veth1
	ip link set dev veth0 netns $h1 name eth0
	ip link set dev veth1 netns $sw1 name swp1

	ip link add name veth0 type veth peer name veth1
	ip link set dev veth0 netns $sw1 name veth0
	ip link set dev veth1 netns $sw2 name veth0

	ip link add name veth0 type veth peer name veth1
	ip link set dev veth0 netns $h2 name eth0
	ip link set dev veth1 netns $sw2 name swp1
}

setup_host_common()
{
	local ns=$1; shift
	local v4addr1=$1; shift
	local v4addr2=$1; shift
	local v6addr1=$1; shift
	local v6addr2=$1; shift

	ip -n $ns link set dev eth0 up
	ip -n $ns link add link eth0 name eth0.10 up type vlan id 10
	ip -n $ns link add link eth0 name eth0.20 up type vlan id 20

	ip -n $ns address add $v4addr1 dev eth0.10
	ip -n $ns address add $v4addr2 dev eth0.20
	ip -n $ns address add $v6addr1 dev eth0.10
	ip -n $ns address add $v6addr2 dev eth0.20
}

setup_h1()
{
	local ns=$h1
	local v4addr1=192.0.2.1/28
	local v4addr2=192.0.2.17/28
	local v6addr1=2001:db8:1::1/64
	local v6addr2=2001:db8:2::1/64

	setup_host_common $ns $v4addr1 $v4addr2 $v6addr1 $v6addr2
}

setup_h2()
{
	local ns=$h2
	local v4addr1=192.0.2.2/28
	local v4addr2=192.0.2.18/28
	local v6addr1=2001:db8:1::2/64
	local v6addr2=2001:db8:2::2/64

	setup_host_common $ns $v4addr1 $v4addr2 $v6addr1 $v6addr2
}

setup_sw_common()
{
	local ns=$1; shift
	local local_addr=$1; shift
	local remote_addr=$1; shift
	local veth_addr=$1; shift
	local gw_addr=$1; shift

	ip -n $ns address add $local_addr/32 dev lo

	ip -n $ns link set dev veth0 up
	ip -n $ns address add $veth_addr/28 dev veth0
	ip -n $ns route add default via $gw_addr

	ip -n $ns link add name br0 up type bridge vlan_filtering 1 \
		vlan_default_pvid 0 mcast_snooping 0

	ip -n $ns link add link br0 name br0.10 up type vlan id 10
	bridge -n $ns vlan add vid 10 dev br0 self

	ip -n $ns link add link br0 name br0.20 up type vlan id 20
	bridge -n $ns vlan add vid 20 dev br0 self

	ip -n $ns link set dev swp1 up master br0
	bridge -n $ns vlan add vid 10 dev swp1
	bridge -n $ns vlan add vid 20 dev swp1

	ip -n $ns link add name vx0 up master br0 type vxlan \
		local $local_addr dstport 4789 nolearning external
	bridge -n $ns fdb add 00:00:00:00:00:00 dev vx0 self static \
		dst $remote_addr src_vni 10010
	bridge -n $ns fdb add 00:00:00:00:00:00 dev vx0 self static \
		dst $remote_addr src_vni 10020
	bridge -n $ns link set dev vx0 vlan_tunnel on learning off

	bridge -n $ns vlan add vid 10 dev vx0
	bridge -n $ns vlan add vid 10 dev vx0 tunnel_info id 10010

	bridge -n $ns vlan add vid 20 dev vx0
	bridge -n $ns vlan add vid 20 dev vx0 tunnel_info id 10020
}

setup_sw1()
{
	local ns=$sw1
	local local_addr=192.0.2.33
	local remote_addr=192.0.2.34
	local veth_addr=192.0.2.49
	local gw_addr=192.0.2.50

	setup_sw_common $ns $local_addr $remote_addr $veth_addr $gw_addr
}

setup_sw2()
{
	local ns=$sw2
	local local_addr=192.0.2.34
	local remote_addr=192.0.2.33
	local veth_addr=192.0.2.50
	local gw_addr=192.0.2.49

	setup_sw_common $ns $local_addr $remote_addr $veth_addr $gw_addr
}

setup()
{
	set -e

	setup_topo
	setup_h1
	setup_h2
	setup_sw1
	setup_sw2

	sleep 5

	set +e
}

cleanup()
{
	cleanup_ns $h1 $h2 $sw1 $sw2
}

################################################################################
# Tests

neigh_suppress_arp_common()
{
	local vid=$1; shift
	local sip=$1; shift
	local tip=$1; shift
	local h2_mac

	echo
	echo "Per-port ARP suppression - VLAN $vid"
	echo "----------------------------------"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto 0x0806 flower indev swp1 arp_tip $tip arp_sip $sip arp_op request action pass"

	# Initial state - check that ARP requests are not suppressed and that
	# ARP replies are received.
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 0 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "ARP suppression"

	# Enable neighbor suppression and check that nothing changes compared
	# to the initial state.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 0 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "ARP suppression"

	# Install an FDB entry for the remote host and check that nothing
	# changes compared to the initial state.
	h2_mac=$(ip -n $h2 -j -p link show eth0.$vid | jq -r '.[]["address"]')
	run_cmd "bridge -n $sw1 fdb replace $h2_mac dev vx0 master static vlan $vid"
	log_test $? 0 "FDB entry installation"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 0 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "ARP suppression"

	# Install a neighbor on the matching SVI interface and check that ARP
	# requests are suppressed.
	run_cmd "ip -n $sw1 neigh replace $tip lladdr $h2_mac nud permanent dev br0.$vid"
	log_test $? 0 "Neighbor entry installation"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 0 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "ARP suppression"

	# Take the second host down and check that ARP requests are suppressed
	# and that ARP replies are received.
	run_cmd "ip -n $h2 link set dev eth0.$vid down"
	log_test $? 0 "H2 down"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 0 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "ARP suppression"

	run_cmd "ip -n $h2 link set dev eth0.$vid up"
	log_test $? 0 "H2 up"

	# Disable neighbor suppression and check that ARP requests are no
	# longer suppressed.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress off"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 0 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 4
	log_test $? 0 "ARP suppression"

	# Take the second host down and check that ARP requests are not
	# suppressed and that ARP replies are not received.
	run_cmd "ip -n $h2 link set dev eth0.$vid down"
	log_test $? 0 "H2 down"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip -I eth0.$vid $tip"
	log_test $? 1 "arping"
	tc_check_packets $sw1 "dev vx0 egress" 101 5
	log_test $? 0 "ARP suppression"
}

neigh_suppress_arp()
{
	local vid=10
	local sip=192.0.2.1
	local tip=192.0.2.2

	neigh_suppress_arp_common $vid $sip $tip

	vid=20
	sip=192.0.2.17
	tip=192.0.2.18
	neigh_suppress_arp_common $vid $sip $tip
}

neigh_suppress_ns_common()
{
	local vid=$1; shift
	local saddr=$1; shift
	local daddr=$1; shift
	local maddr=$1; shift
	local h2_mac

	echo
	echo "Per-port NS suppression - VLAN $vid"
	echo "---------------------------------"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto ipv6 flower indev swp1 ip_proto icmpv6 dst_ip $maddr src_ip $saddr type 135 code 0 action pass"

	# Initial state - check that NS messages are not suppressed and that ND
	# messages are received.
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 0 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "NS suppression"

	# Enable neighbor suppression and check that nothing changes compared
	# to the initial state.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 0 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "NS suppression"

	# Install an FDB entry for the remote host and check that nothing
	# changes compared to the initial state.
	h2_mac=$(ip -n $h2 -j -p link show eth0.$vid | jq -r '.[]["address"]')
	run_cmd "bridge -n $sw1 fdb replace $h2_mac dev vx0 master static vlan $vid"
	log_test $? 0 "FDB entry installation"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 0 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "NS suppression"

	# Install a neighbor on the matching SVI interface and check that NS
	# messages are suppressed.
	run_cmd "ip -n $sw1 neigh replace $daddr lladdr $h2_mac nud permanent dev br0.$vid"
	log_test $? 0 "Neighbor entry installation"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 0 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "NS suppression"

	# Take the second host down and check that NS messages are suppressed
	# and that ND messages are received.
	run_cmd "ip -n $h2 link set dev eth0.$vid down"
	log_test $? 0 "H2 down"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 0 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "NS suppression"

	run_cmd "ip -n $h2 link set dev eth0.$vid up"
	log_test $? 0 "H2 up"

	# Disable neighbor suppression and check that NS messages are no longer
	# suppressed.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress off"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 0 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 4
	log_test $? 0 "NS suppression"

	# Take the second host down and check that NS messages are not
	# suppressed and that ND messages are not received.
	run_cmd "ip -n $h2 link set dev eth0.$vid down"
	log_test $? 0 "H2 down"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr -w 5000 $daddr eth0.$vid"
	log_test $? 2 "ndisc6"
	tc_check_packets $sw1 "dev vx0 egress" 101 5
	log_test $? 0 "NS suppression"
}

neigh_suppress_ns()
{
	local vid=10
	local saddr=2001:db8:1::1
	local daddr=2001:db8:1::2
	local maddr=ff02::1:ff00:2

	neigh_suppress_ns_common $vid $saddr $daddr $maddr

	vid=20
	saddr=2001:db8:2::1
	daddr=2001:db8:2::2
	maddr=ff02::1:ff00:2

	neigh_suppress_ns_common $vid $saddr $daddr $maddr
}

neigh_vlan_suppress_arp()
{
	local vid1=10
	local vid2=20
	local sip1=192.0.2.1
	local sip2=192.0.2.17
	local tip1=192.0.2.2
	local tip2=192.0.2.18
	local h2_mac1
	local h2_mac2

	echo
	echo "Per-{Port, VLAN} ARP suppression"
	echo "--------------------------------"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto 0x0806 flower indev swp1 arp_tip $tip1 arp_sip $sip1 arp_op request action pass"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 102 proto 0x0806 flower indev swp1 arp_tip $tip2 arp_sip $sip2 arp_op request action pass"

	h2_mac1=$(ip -n $h2 -j -p link show eth0.$vid1 | jq -r '.[]["address"]')
	h2_mac2=$(ip -n $h2 -j -p link show eth0.$vid2 | jq -r '.[]["address"]')
	run_cmd "bridge -n $sw1 fdb replace $h2_mac1 dev vx0 master static vlan $vid1"
	run_cmd "bridge -n $sw1 fdb replace $h2_mac2 dev vx0 master static vlan $vid2"
	run_cmd "ip -n $sw1 neigh replace $tip1 lladdr $h2_mac1 nud permanent dev br0.$vid1"
	run_cmd "ip -n $sw1 neigh replace $tip2 lladdr $h2_mac2 nud permanent dev br0.$vid2"

	# Enable per-{Port, VLAN} neighbor suppression and check that ARP
	# requests are not suppressed and that ARP replies are received.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_vlan_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_vlan_suppress on\""
	log_test $? 0 "\"neigh_vlan_suppress\" is on"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip1 -I eth0.$vid1 $tip1"
	log_test $? 0 "arping (VLAN $vid1)"
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip2 -I eth0.$vid2 $tip2"
	log_test $? 0 "arping (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "ARP suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 1
	log_test $? 0 "ARP suppression (VLAN $vid2)"

	# Enable neighbor suppression on VLAN 10 and check that only on this
	# VLAN ARP requests are suppressed.
	run_cmd "bridge -n $sw1 vlan set vid $vid1 dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d vlan show dev vx0 vid $vid1 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on (VLAN $vid1)"
	run_cmd "bridge -n $sw1 -d vlan show dev vx0 vid $vid2 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off (VLAN $vid2)"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip1 -I eth0.$vid1 $tip1"
	log_test $? 0 "arping (VLAN $vid1)"
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip2 -I eth0.$vid2 $tip2"
	log_test $? 0 "arping (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "ARP suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 2
	log_test $? 0 "ARP suppression (VLAN $vid2)"

	# Enable neighbor suppression on the port and check that it has no
	# effect compared to previous state.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip1 -I eth0.$vid1 $tip1"
	log_test $? 0 "arping (VLAN $vid1)"
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip2 -I eth0.$vid2 $tip2"
	log_test $? 0 "arping (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "ARP suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 3
	log_test $? 0 "ARP suppression (VLAN $vid2)"

	# Disable neighbor suppression on the port and check that it has no
	# effect compared to previous state.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress off"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip1 -I eth0.$vid1 $tip1"
	log_test $? 0 "arping (VLAN $vid1)"
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip2 -I eth0.$vid2 $tip2"
	log_test $? 0 "arping (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "ARP suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 4
	log_test $? 0 "ARP suppression (VLAN $vid2)"

	# Disable neighbor suppression on VLAN 10 and check that ARP requests
	# are no longer suppressed on this VLAN.
	run_cmd "bridge -n $sw1 vlan set vid $vid1 dev vx0 neigh_suppress off"
	run_cmd "bridge -n $sw1 -d vlan show dev vx0 vid $vid1 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off (VLAN $vid1)"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip1 -I eth0.$vid1 $tip1"
	log_test $? 0 "arping (VLAN $vid1)"
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip2 -I eth0.$vid2 $tip2"
	log_test $? 0 "arping (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "ARP suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 5
	log_test $? 0 "ARP suppression (VLAN $vid2)"

	# Disable per-{Port, VLAN} neighbor suppression, enable neighbor
	# suppression on the port and check that on both VLANs ARP requests are
	# suppressed.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_vlan_suppress off"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_vlan_suppress off\""
	log_test $? 0 "\"neigh_vlan_suppress\" is off"

	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on"

	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip1 -I eth0.$vid1 $tip1"
	log_test $? 0 "arping (VLAN $vid1)"
	run_cmd "ip netns exec $h1 arping -q -b -c 1 -w 5 -s $sip2 -I eth0.$vid2 $tip2"
	log_test $? 0 "arping (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "ARP suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 5
	log_test $? 0 "ARP suppression (VLAN $vid2)"
}

neigh_vlan_suppress_ns()
{
	local vid1=10
	local vid2=20
	local saddr1=2001:db8:1::1
	local saddr2=2001:db8:2::1
	local daddr1=2001:db8:1::2
	local daddr2=2001:db8:2::2
	local maddr=ff02::1:ff00:2
	local h2_mac1
	local h2_mac2

	echo
	echo "Per-{Port, VLAN} NS suppression"
	echo "-------------------------------"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto ipv6 flower indev swp1 ip_proto icmpv6 dst_ip $maddr src_ip $saddr1 type 135 code 0 action pass"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 102 proto ipv6 flower indev swp1 ip_proto icmpv6 dst_ip $maddr src_ip $saddr2 type 135 code 0 action pass"

	h2_mac1=$(ip -n $h2 -j -p link show eth0.$vid1 | jq -r '.[]["address"]')
	h2_mac2=$(ip -n $h2 -j -p link show eth0.$vid2 | jq -r '.[]["address"]')
	run_cmd "bridge -n $sw1 fdb replace $h2_mac1 dev vx0 master static vlan $vid1"
	run_cmd "bridge -n $sw1 fdb replace $h2_mac2 dev vx0 master static vlan $vid2"
	run_cmd "ip -n $sw1 neigh replace $daddr1 lladdr $h2_mac1 nud permanent dev br0.$vid1"
	run_cmd "ip -n $sw1 neigh replace $daddr2 lladdr $h2_mac2 nud permanent dev br0.$vid2"

	# Enable per-{Port, VLAN} neighbor suppression and check that NS
	# messages are not suppressed and that ND messages are received.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_vlan_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_vlan_suppress on\""
	log_test $? 0 "\"neigh_vlan_suppress\" is on"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr1 -w 5000 $daddr1 eth0.$vid1"
	log_test $? 0 "ndisc6 (VLAN $vid1)"
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr2 -w 5000 $daddr2 eth0.$vid2"
	log_test $? 0 "ndisc6 (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "NS suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 1
	log_test $? 0 "NS suppression (VLAN $vid2)"

	# Enable neighbor suppression on VLAN 10 and check that only on this
	# VLAN NS messages are suppressed.
	run_cmd "bridge -n $sw1 vlan set vid $vid1 dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d vlan show dev vx0 vid $vid1 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on (VLAN $vid1)"
	run_cmd "bridge -n $sw1 -d vlan show dev vx0 vid $vid2 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off (VLAN $vid2)"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr1 -w 5000 $daddr1 eth0.$vid1"
	log_test $? 0 "ndisc6 (VLAN $vid1)"
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr2 -w 5000 $daddr2 eth0.$vid2"
	log_test $? 0 "ndisc6 (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "NS suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 2
	log_test $? 0 "NS suppression (VLAN $vid2)"

	# Enable neighbor suppression on the port and check that it has no
	# effect compared to previous state.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr1 -w 5000 $daddr1 eth0.$vid1"
	log_test $? 0 "ndisc6 (VLAN $vid1)"
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr2 -w 5000 $daddr2 eth0.$vid2"
	log_test $? 0 "ndisc6 (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "NS suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 3
	log_test $? 0 "NS suppression (VLAN $vid2)"

	# Disable neighbor suppression on the port and check that it has no
	# effect compared to previous state.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress off"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr1 -w 5000 $daddr1 eth0.$vid1"
	log_test $? 0 "ndisc6 (VLAN $vid1)"
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr2 -w 5000 $daddr2 eth0.$vid2"
	log_test $? 0 "ndisc6 (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "NS suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 4
	log_test $? 0 "NS suppression (VLAN $vid2)"

	# Disable neighbor suppression on VLAN 10 and check that NS messages
	# are no longer suppressed on this VLAN.
	run_cmd "bridge -n $sw1 vlan set vid $vid1 dev vx0 neigh_suppress off"
	run_cmd "bridge -n $sw1 -d vlan show dev vx0 vid $vid1 | grep \"neigh_suppress off\""
	log_test $? 0 "\"neigh_suppress\" is off (VLAN $vid1)"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr1 -w 5000 $daddr1 eth0.$vid1"
	log_test $? 0 "ndisc6 (VLAN $vid1)"
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr2 -w 5000 $daddr2 eth0.$vid2"
	log_test $? 0 "ndisc6 (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "NS suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 5
	log_test $? 0 "NS suppression (VLAN $vid2)"

	# Disable per-{Port, VLAN} neighbor suppression, enable neighbor
	# suppression on the port and check that on both VLANs NS messages are
	# suppressed.
	run_cmd "bridge -n $sw1 link set dev vx0 neigh_vlan_suppress off"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_vlan_suppress off\""
	log_test $? 0 "\"neigh_vlan_suppress\" is off"

	run_cmd "bridge -n $sw1 link set dev vx0 neigh_suppress on"
	run_cmd "bridge -n $sw1 -d link show dev vx0 | grep \"neigh_suppress on\""
	log_test $? 0 "\"neigh_suppress\" is on"

	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr1 -w 5000 $daddr1 eth0.$vid1"
	log_test $? 0 "ndisc6 (VLAN $vid1)"
	run_cmd "ip netns exec $h1 ndisc6 -q -r 1 -s $saddr2 -w 5000 $daddr2 eth0.$vid2"
	log_test $? 0 "ndisc6 (VLAN $vid2)"

	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "NS suppression (VLAN $vid1)"
	tc_check_packets $sw1 "dev vx0 egress" 102 5
	log_test $? 0 "NS suppression (VLAN $vid2)"
}

################################################################################
# Usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $TESTS)
        -p          Pause on fail
        -P          Pause after each test before cleanup
        -v          Verbose mode (show commands and output)
EOF
}

################################################################################
# Main

trap cleanup EXIT

while getopts ":t:pPvh" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# Make sure we don't pause twice.
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v bridge)" ]; then
	echo "SKIP: Could not run test without bridge tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v tc)" ]; then
	echo "SKIP: Could not run test without tc tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v arping)" ]; then
	echo "SKIP: Could not run test without arping tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v ndisc6)" ]; then
	echo "SKIP: Could not run test without ndisc6 tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v jq)" ]; then
	echo "SKIP: Could not run test without jq tool"
	exit $ksft_skip
fi

bridge link help 2>&1 | grep -q "neigh_vlan_suppress"
if [ $? -ne 0 ]; then
   echo "SKIP: iproute2 bridge too old, missing per-VLAN neighbor suppression support"
   exit $ksft_skip
fi

# Start clean.
cleanup

for t in $TESTS
do
	setup; $t; cleanup;
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
