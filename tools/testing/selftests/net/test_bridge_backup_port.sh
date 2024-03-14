#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking bridge backup port and backup nexthop ID
# functionality. The topology consists of two bridge (VTEPs) connected using
# VXLAN. The test checks that when the switch port (swp1) is down, traffic is
# redirected to the VXLAN port (vx0). When a backup nexthop ID is configured,
# the test checks that traffic is redirected with the correct nexthop
# information.
#
# +------------------------------------+ +------------------------------------+
# |    + swp1                   + vx0  | |    + swp1                   + vx0  |
# |    |                        |      | |    |                        |      |
# |    |           br0          |      | |    |                        |      |
# |    +------------+-----------+      | |    +------------+-----------+      |
# |                 |                  | |                 |                  |
# |                 |                  | |                 |                  |
# |                 +                  | |                 +                  |
# |                br0                 | |                br0                 |
# |                 +                  | |                 +                  |
# |                 |                  | |                 |                  |
# |                 |                  | |                 |                  |
# |                 +                  | |                 +                  |
# |              br0.10                | |              br0.10                |
# |           192.0.2.65/28            | |            192.0.2.66/28           |
# |                                    | |                                    |
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
	backup_port
	backup_nhid
	backup_nhid_invalid
	backup_nhid_ping
	backup_nhid_torture
"
VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no
PING_TIMEOUT=5

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

bridge_link_check()
{
	local ns=$1; shift
	local dev=$1; shift
	local state=$1; shift

	bridge -n $ns -d -j link show dev $dev | \
		jq -e ".[][\"state\"] == \"$state\"" &> /dev/null
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

	setup_ns sw1 sw2
	for ns in $sw1 $sw2; do
		setup_topo_ns $ns
	done

	ip link add name veth0 type veth peer name veth1
	ip link set dev veth0 netns $sw1 name veth0
	ip link set dev veth1 netns $sw2 name veth0
}

setup_sw_common()
{
	local ns=$1; shift
	local local_addr=$1; shift
	local remote_addr=$1; shift
	local veth_addr=$1; shift
	local gw_addr=$1; shift
	local br_addr=$1; shift

	ip -n $ns address add $local_addr/32 dev lo

	ip -n $ns link set dev veth0 up
	ip -n $ns address add $veth_addr/28 dev veth0
	ip -n $ns route add default via $gw_addr

	ip -n $ns link add name br0 up type bridge vlan_filtering 1 \
		vlan_default_pvid 0 mcast_snooping 0

	ip -n $ns link add link br0 name br0.10 up type vlan id 10
	bridge -n $ns vlan add vid 10 dev br0 self
	ip -n $ns address add $br_addr/28 dev br0.10

	ip -n $ns link add name swp1 up type dummy
	ip -n $ns link set dev swp1 master br0
	bridge -n $ns vlan add vid 10 dev swp1 untagged

	ip -n $ns link add name vx0 up master br0 type vxlan \
		local $local_addr dstport 4789 nolearning external
	bridge -n $ns link set dev vx0 vlan_tunnel on learning off

	bridge -n $ns vlan add vid 10 dev vx0
	bridge -n $ns vlan add vid 10 dev vx0 tunnel_info id 10010
}

setup_sw1()
{
	local ns=$sw1
	local local_addr=192.0.2.33
	local remote_addr=192.0.2.34
	local veth_addr=192.0.2.49
	local gw_addr=192.0.2.50
	local br_addr=192.0.2.65

	setup_sw_common $ns $local_addr $remote_addr $veth_addr $gw_addr \
		$br_addr
}

setup_sw2()
{
	local ns=$sw2
	local local_addr=192.0.2.34
	local remote_addr=192.0.2.33
	local veth_addr=192.0.2.50
	local gw_addr=192.0.2.49
	local br_addr=192.0.2.66

	setup_sw_common $ns $local_addr $remote_addr $veth_addr $gw_addr \
		$br_addr
}

setup()
{
	set -e

	setup_topo
	setup_sw1
	setup_sw2

	sleep 5

	set +e
}

cleanup()
{
	cleanup_ns $sw1 $sw2
}

################################################################################
# Tests

backup_port()
{
	local dmac=00:11:22:33:44:55
	local smac=00:aa:bb:cc:dd:ee

	echo
	echo "Backup port"
	echo "-----------"

	run_cmd "tc -n $sw1 qdisc replace dev swp1 clsact"
	run_cmd "tc -n $sw1 filter replace dev swp1 egress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac action pass"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac action pass"

	run_cmd "bridge -n $sw1 fdb replace $dmac dev swp1 master static vlan 10"

	# Initial state - check that packets are forwarded out of swp1 when it
	# has a carrier and not forwarded out of any port when it does not have
	# a carrier.
	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 1
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 0
	log_test $? 0 "No forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 1
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 0
	log_test $? 0 "No forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier on"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 forwarding
	log_test $? 0 "swp1 carrier on"

	# Configure vx0 as the backup port of swp1 and check that packets are
	# forwarded out of swp1 when it has a carrier and out of vx0 when swp1
	# does not have a carrier.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_port vx0"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_port vx0\""
	log_test $? 0 "vx0 configured as backup port of swp1"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 2
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 0
	log_test $? 0 "No forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 2
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "Forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier on"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 forwarding
	log_test $? 0 "swp1 carrier on"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 3
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "No forwarding out of vx0"

	# Remove vx0 as the backup port of swp1 and check that packets are no
	# longer forwarded out of vx0 when swp1 does not have a carrier.
	run_cmd "bridge -n $sw1 link set dev swp1 nobackup_port"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_port vx0\""
	log_test $? 1 "vx0 not configured as backup port of swp1"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 4
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "No forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 4
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "No forwarding out of vx0"
}

backup_nhid()
{
	local dmac=00:11:22:33:44:55
	local smac=00:aa:bb:cc:dd:ee

	echo
	echo "Backup nexthop ID"
	echo "-----------------"

	run_cmd "tc -n $sw1 qdisc replace dev swp1 clsact"
	run_cmd "tc -n $sw1 filter replace dev swp1 egress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac action pass"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac action pass"

	run_cmd "ip -n $sw1 nexthop replace id 1 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 2 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 10 group 1/2 fdb"

	run_cmd "bridge -n $sw1 fdb replace $dmac dev swp1 master static vlan 10"
	run_cmd "bridge -n $sw1 fdb replace $dmac dev vx0 self static dst 192.0.2.36 src_vni 10010"

	run_cmd "ip -n $sw2 address replace 192.0.2.36/32 dev lo"

	# The first filter matches on packets forwarded using the backup
	# nexthop ID and the second filter matches on packets forwarded using a
	# regular VXLAN FDB entry.
	run_cmd "tc -n $sw2 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw2 filter replace dev vx0 ingress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac enc_key_id 10010 enc_dst_ip 192.0.2.34 action pass"
	run_cmd "tc -n $sw2 filter replace dev vx0 ingress pref 1 handle 102 proto ip flower src_mac $smac dst_mac $dmac enc_key_id 10010 enc_dst_ip 192.0.2.36 action pass"

	# Configure vx0 as the backup port of swp1 and check that packets are
	# forwarded out of swp1 when it has a carrier and out of vx0 when swp1
	# does not have a carrier. When packets are forwarded out of vx0, check
	# that they are forwarded by the VXLAN FDB entry.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_port vx0"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_port vx0\""
	log_test $? 0 "vx0 configured as backup port of swp1"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 1
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 0
	log_test $? 0 "No forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 1
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 0
	log_test $? 0 "No forwarding using backup nexthop ID"
	tc_check_packets $sw2 "dev vx0 ingress" 102 1
	log_test $? 0 "Forwarding using VXLAN FDB entry"

	run_cmd "ip -n $sw1 link set dev swp1 carrier on"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 forwarding
	log_test $? 0 "swp1 carrier on"

	# Configure nexthop ID 10 as the backup nexthop ID of swp1 and check
	# that when packets are forwarded out of vx0, they are forwarded using
	# the backup nexthop ID.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 10"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid 10\""
	log_test $? 0 "nexthop ID 10 configured as backup nexthop ID of swp1"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 2
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "No forwarding out of vx0"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 2
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "Forwarding using backup nexthop ID"
	tc_check_packets $sw2 "dev vx0 ingress" 102 1
	log_test $? 0 "No forwarding using VXLAN FDB entry"

	run_cmd "ip -n $sw1 link set dev swp1 carrier on"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 forwarding
	log_test $? 0 "swp1 carrier on"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 3
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "No forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	tc_check_packets $sw2 "dev vx0 ingress" 102 1
	log_test $? 0 "No forwarding using VXLAN FDB entry"

	# Reset the backup nexthop ID to 0 and check that packets are no longer
	# forwarded using the backup nexthop ID when swp1 does not have a
	# carrier and are instead forwarded by the VXLAN FDB.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 0"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid\""
	log_test $? 1 "No backup nexthop ID configured for swp1"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 4
	log_test $? 0 "Forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "No forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	tc_check_packets $sw2 "dev vx0 ingress" 102 1
	log_test $? 0 "No forwarding using VXLAN FDB entry"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 4
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	tc_check_packets $sw2 "dev vx0 ingress" 102 2
	log_test $? 0 "Forwarding using VXLAN FDB entry"
}

backup_nhid_invalid()
{
	local dmac=00:11:22:33:44:55
	local smac=00:aa:bb:cc:dd:ee
	local tx_drop

	echo
	echo "Backup nexthop ID - invalid IDs"
	echo "-------------------------------"

	# Check that when traffic is redirected with an invalid nexthop ID, it
	# is forwarded out of the VXLAN port, but dropped by the VXLAN driver
	# and does not crash the host.

	run_cmd "tc -n $sw1 qdisc replace dev swp1 clsact"
	run_cmd "tc -n $sw1 filter replace dev swp1 egress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac action pass"

	run_cmd "tc -n $sw1 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac action pass"
	# Drop all other Tx traffic to avoid changes to Tx drop counter.
	run_cmd "tc -n $sw1 filter replace dev vx0 egress pref 2 handle 102 proto all matchall action drop"

	tx_drop=$(ip -n $sw1 -s -j link show dev vx0 | jq '.[]["stats64"]["tx"]["dropped"]')

	run_cmd "ip -n $sw1 nexthop replace id 1 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 2 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 10 group 1/2 fdb"

	run_cmd "bridge -n $sw1 fdb replace $dmac dev swp1 master static vlan 10"

	run_cmd "tc -n $sw2 qdisc replace dev vx0 clsact"
	run_cmd "tc -n $sw2 filter replace dev vx0 ingress pref 1 handle 101 proto ip flower src_mac $smac dst_mac $dmac enc_key_id 10010 enc_dst_ip 192.0.2.34 action pass"

	# First, check that redirection works.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_port vx0"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_port vx0\""
	log_test $? 0 "vx0 configured as backup port of swp1"

	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 10"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid 10\""
	log_test $? 0 "Valid nexthop as backup nexthop"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	log_test $? 0 "swp1 carrier off"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 0
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 1
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "Forwarding using backup nexthop ID"
	run_cmd "ip -n $sw1 -s -j link show dev vx0 | jq -e '.[][\"stats64\"][\"tx\"][\"dropped\"] == $tx_drop'"
	log_test $? 0 "No Tx drop increase"

	# Use a non-existent nexthop ID.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 20"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid 20\""
	log_test $? 0 "Non-existent nexthop as backup nexthop"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 0
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 2
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	run_cmd "ip -n $sw1 -s -j link show dev vx0 | jq -e '.[][\"stats64\"][\"tx\"][\"dropped\"] == $((tx_drop + 1))'"
	log_test $? 0 "Tx drop increased"

	# Use a blckhole nexthop.
	run_cmd "ip -n $sw1 nexthop replace id 30 blackhole"
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 30"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid 30\""
	log_test $? 0 "Blackhole nexthop as backup nexthop"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 0
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 3
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	run_cmd "ip -n $sw1 -s -j link show dev vx0 | jq -e '.[][\"stats64\"][\"tx\"][\"dropped\"] == $((tx_drop + 2))'"
	log_test $? 0 "Tx drop increased"

	# Non-group FDB nexthop.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 1"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid 1\""
	log_test $? 0 "Non-group FDB nexthop as backup nexthop"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 0
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 4
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	run_cmd "ip -n $sw1 -s -j link show dev vx0 | jq -e '.[][\"stats64\"][\"tx\"][\"dropped\"] == $((tx_drop + 3))'"
	log_test $? 0 "Tx drop increased"

	# IPv6 address family nexthop.
	run_cmd "ip -n $sw1 nexthop replace id 100 via 2001:db8:100::1 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 200 via 2001:db8:100::1 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 300 group 100/200 fdb"
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 300"
	run_cmd "bridge -n $sw1 -d link show dev swp1 | grep \"backup_nhid 300\""
	log_test $? 0 "IPv6 address family nexthop as backup nexthop"

	run_cmd "ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 1"
	tc_check_packets $sw1 "dev swp1 egress" 101 0
	log_test $? 0 "No forwarding out of swp1"
	tc_check_packets $sw1 "dev vx0 egress" 101 5
	log_test $? 0 "Forwarding out of vx0"
	tc_check_packets $sw2 "dev vx0 ingress" 101 1
	log_test $? 0 "No forwarding using backup nexthop ID"
	run_cmd "ip -n $sw1 -s -j link show dev vx0 | jq -e '.[][\"stats64\"][\"tx\"][\"dropped\"] == $((tx_drop + 4))'"
	log_test $? 0 "Tx drop increased"
}

backup_nhid_ping()
{
	local sw1_mac
	local sw2_mac

	echo
	echo "Backup nexthop ID - ping"
	echo "------------------------"

	# Test bidirectional traffic when traffic is redirected in both VTEPs.
	sw1_mac=$(ip -n $sw1 -j -p link show br0.10 | jq -r '.[]["address"]')
	sw2_mac=$(ip -n $sw2 -j -p link show br0.10 | jq -r '.[]["address"]')

	run_cmd "bridge -n $sw1 fdb replace $sw2_mac dev swp1 master static vlan 10"
	run_cmd "bridge -n $sw2 fdb replace $sw1_mac dev swp1 master static vlan 10"

	run_cmd "ip -n $sw1 neigh replace 192.0.2.66 lladdr $sw2_mac nud perm dev br0.10"
	run_cmd "ip -n $sw2 neigh replace 192.0.2.65 lladdr $sw1_mac nud perm dev br0.10"

	run_cmd "ip -n $sw1 nexthop replace id 1 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw2 nexthop replace id 1 via 192.0.2.33 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 10 group 1 fdb"
	run_cmd "ip -n $sw2 nexthop replace id 10 group 1 fdb"

	run_cmd "bridge -n $sw1 link set dev swp1 backup_port vx0"
	run_cmd "bridge -n $sw2 link set dev swp1 backup_port vx0"
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 10"
	run_cmd "bridge -n $sw2 link set dev swp1 backup_nhid 10"

	run_cmd "ip -n $sw1 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw1 swp1 disabled
	run_cmd "ip -n $sw2 link set dev swp1 carrier off"
	busywait $BUSYWAIT_TIMEOUT bridge_link_check $sw2 swp1 disabled

	run_cmd "ip netns exec $sw1 ping -i 0.1 -c 10 -w $PING_TIMEOUT 192.0.2.66"
	log_test $? 0 "Ping with backup nexthop ID"

	# Reset the backup nexthop ID to 0 and check that ping fails.
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 0"
	run_cmd "bridge -n $sw2 link set dev swp1 backup_nhid 0"

	run_cmd "ip netns exec $sw1 ping -i 0.1 -c 10 -w $PING_TIMEOUT 192.0.2.66"
	log_test $? 1 "Ping after disabling backup nexthop ID"
}

backup_nhid_add_del_loop()
{
	while true; do
		ip -n $sw1 nexthop del id 10
		ip -n $sw1 nexthop replace id 10 group 1/2 fdb
	done >/dev/null 2>&1
}

backup_nhid_torture()
{
	local dmac=00:11:22:33:44:55
	local smac=00:aa:bb:cc:dd:ee
	local pid1
	local pid2
	local pid3

	echo
	echo "Backup nexthop ID - torture test"
	echo "--------------------------------"

	# Continuously send traffic through the backup nexthop while adding and
	# deleting the group. The test is considered successful if nothing
	# crashed.

	run_cmd "ip -n $sw1 nexthop replace id 1 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 2 via 192.0.2.34 fdb"
	run_cmd "ip -n $sw1 nexthop replace id 10 group 1/2 fdb"

	run_cmd "bridge -n $sw1 fdb replace $dmac dev swp1 master static vlan 10"

	run_cmd "bridge -n $sw1 link set dev swp1 backup_port vx0"
	run_cmd "bridge -n $sw1 link set dev swp1 backup_nhid 10"
	run_cmd "ip -n $sw1 link set dev swp1 carrier off"

	backup_nhid_add_del_loop &
	pid1=$!
	ip netns exec $sw1 mausezahn br0.10 -a $smac -b $dmac -A 198.51.100.1 -B 198.51.100.2 -t ip -p 100 -q -c 0 &
	pid2=$!

	sleep 30
	kill -9 $pid1 $pid2
	wait $pid1 $pid2 2>/dev/null

	log_test 0 0 "Torture test"
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
        -w          Timeout for ping
EOF
}

################################################################################
# Main

trap cleanup EXIT

while getopts ":t:pPvhw:" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		w) PING_TIMEOUT=$OPTARG;;
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

if [ ! -x "$(command -v mausezahn)" ]; then
	echo "SKIP: Could not run test without mausezahn tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v jq)" ]; then
	echo "SKIP: Could not run test without jq tool"
	exit $ksft_skip
fi

bridge link help 2>&1 | grep -q "backup_nhid"
if [ $? -ne 0 ]; then
   echo "SKIP: iproute2 bridge too old, missing backup nexthop ID support"
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
