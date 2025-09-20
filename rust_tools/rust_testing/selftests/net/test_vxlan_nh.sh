#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh
TESTS="
	basic_tx_ipv4
	basic_tx_ipv6
	learning
	proxy_ipv4
	proxy_ipv6
"
VERBOSE=0

################################################################################
# Utilities

run_cmd()
{
	local cmd="$1"
	local out
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: $cmd"
		stderr=
	fi

	out=$(eval "$cmd" "$stderr")
	rc=$?
	if [ "$VERBOSE" -eq 1 ] && [ -n "$out" ]; then
		echo "    $out"
	fi

	return $rc
}

################################################################################
# Cleanup

exit_cleanup_all()
{
	cleanup_all_ns
	exit "${EXIT_STATUS}"
}

################################################################################
# Tests

nh_stats_get()
{
	ip -n "$ns1" -s -j nexthop show id 10 | jq ".[][\"group_stats\"][][\"packets\"]"
}

tc_stats_get()
{
	tc_rule_handle_stats_get "dev dummy1 egress" 101 ".packets" "-n $ns1"
}

basic_tx_common()
{
	local af_str=$1; shift
	local proto=$1; shift
	local local_addr=$1; shift
	local plen=$1; shift
	local remote_addr=$1; shift

	RET=0

	# Test basic Tx functionality. Check that stats are incremented on
	# both the FDB nexthop group and the egress device.

	run_cmd "ip -n $ns1 link add name dummy1 up type dummy"
	run_cmd "ip -n $ns1 route add $remote_addr/$plen dev dummy1"
	run_cmd "tc -n $ns1 qdisc add dev dummy1 clsact"
	run_cmd "tc -n $ns1 filter add dev dummy1 egress proto $proto pref 1 handle 101 flower ip_proto udp dst_ip $remote_addr dst_port 4789 action pass"

	run_cmd "ip -n $ns1 address add $local_addr/$plen dev lo"

	run_cmd "ip -n $ns1 nexthop add id 1 via $remote_addr fdb"
	run_cmd "ip -n $ns1 nexthop add id 10 group 1 fdb"

	run_cmd "ip -n $ns1 link add name vx0 up type vxlan id 10010 local $local_addr dstport 4789"
	run_cmd "bridge -n $ns1 fdb add 00:11:22:33:44:55 dev vx0 self static nhid 10"

	run_cmd "ip netns exec $ns1 mausezahn vx0 -a own -b 00:11:22:33:44:55 -c 1 -q"

	busywait "$BUSYWAIT_TIMEOUT" until_counter_is "== 1" nh_stats_get > /dev/null
	check_err $? "FDB nexthop group stats did not increase"

	busywait "$BUSYWAIT_TIMEOUT" until_counter_is "== 1" tc_stats_get > /dev/null
	check_err $? "tc filter stats did not increase"

	log_test "VXLAN FDB nexthop: $af_str basic Tx"
}

basic_tx_ipv4()
{
	basic_tx_common "IPv4" ipv4 192.0.2.1 32 192.0.2.2
}

basic_tx_ipv6()
{
	basic_tx_common "IPv6" ipv6 2001:db8:1::1 128 2001:db8:1::2
}

learning()
{
	RET=0

	# When learning is enabled on the VXLAN device, an incoming packet
	# might try to refresh an FDB entry that points to an FDB nexthop group
	# instead of an ordinary remote destination. Check that the kernel does
	# not crash in this situation.

	run_cmd "ip -n $ns1 address add 192.0.2.1/32 dev lo"
	run_cmd "ip -n $ns1 address add 192.0.2.2/32 dev lo"

	run_cmd "ip -n $ns1 nexthop add id 1 via 192.0.2.3 fdb"
	run_cmd "ip -n $ns1 nexthop add id 10 group 1 fdb"

	run_cmd "ip -n $ns1 link add name vx0 up type vxlan id 10010 local 192.0.2.1 dstport 12345 localbypass"
	run_cmd "ip -n $ns1 link add name vx1 up type vxlan id 10020 local 192.0.2.2 dstport 54321 learning"

	run_cmd "bridge -n $ns1 fdb add 00:11:22:33:44:55 dev vx0 self static dst 192.0.2.2 port 54321 vni 10020"
	run_cmd "bridge -n $ns1 fdb add 00:aa:bb:cc:dd:ee dev vx1 self static nhid 10"

	run_cmd "ip netns exec $ns1 mausezahn vx0 -a 00:aa:bb:cc:dd:ee -b 00:11:22:33:44:55 -c 1 -q"

	log_test "VXLAN FDB nexthop: learning"
}

proxy_common()
{
	local af_str=$1; shift
	local local_addr=$1; shift
	local plen=$1; shift
	local remote_addr=$1; shift
	local neigh_addr=$1; shift
	local ping_cmd=$1; shift

	RET=0

	# When the "proxy" option is enabled on the VXLAN device, the device
	# will suppress ARP requests and IPv6 Neighbor Solicitation messages if
	# it is able to reply on behalf of the remote host. That is, if a
	# matching and valid neighbor entry is configured on the VXLAN device
	# whose MAC address is not behind the "any" remote (0.0.0.0 / ::). The
	# FDB entry for the neighbor's MAC address might point to an FDB
	# nexthop group instead of an ordinary remote destination. Check that
	# the kernel does not crash in this situation.

	run_cmd "ip -n $ns1 address add $local_addr/$plen dev lo"

	run_cmd "ip -n $ns1 nexthop add id 1 via $remote_addr fdb"
	run_cmd "ip -n $ns1 nexthop add id 10 group 1 fdb"

	run_cmd "ip -n $ns1 link add name vx0 up type vxlan id 10010 local $local_addr dstport 4789 proxy"

	run_cmd "ip -n $ns1 neigh add $neigh_addr lladdr 00:11:22:33:44:55 nud perm dev vx0"

	run_cmd "bridge -n $ns1 fdb add 00:11:22:33:44:55 dev vx0 self static nhid 10"

	run_cmd "ip netns exec $ns1 $ping_cmd"

	log_test "VXLAN FDB nexthop: $af_str proxy"
}

proxy_ipv4()
{
	proxy_common "IPv4" 192.0.2.1 32 192.0.2.2 192.0.2.3 \
		"arping -b -c 1 -s 192.0.2.1 -I vx0 192.0.2.3"
}

proxy_ipv6()
{
	proxy_common "IPv6" 2001:db8:1::1 128 2001:db8:1::2 2001:db8:1::3 \
		"ndisc6 -r 1 -s 2001:db8:1::1 -w 1 2001:db8:1::3 vx0"
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
        -v          Verbose mode (show commands and output)
EOF
}

################################################################################
# Main

while getopts ":t:pvh" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=$((VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

require_command mausezahn
require_command arping
require_command ndisc6
require_command jq

if ! ip nexthop help 2>&1 | grep -q "stats"; then
	echo "SKIP: iproute2 ip too old, missing nexthop stats support"
	exit "$ksft_skip"
fi

trap exit_cleanup_all EXIT

for t in $TESTS
do
	setup_ns ns1; $t; cleanup_all_ns;
done
