#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that bond_header_parse() does not infinitely recurse with stacked bonds.
#
# When a non-Ethernet device (e.g. GRE) is enslaved to a bond that is itself
# enslaved to another bond (bond1 -> bond0 -> gre), receiving a packet via
# AF_PACKET SOCK_DGRAM triggers dev_parse_header() -> bond_header_parse().
# Since parse() used skb->dev (always the topmost bond) instead of a passed-in
# dev pointer, it would recurse back into itself indefinitely.

# shellcheck disable=SC2034
ALL_TESTS="
	bond_test_stacked_header_parse
"
REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

# shellcheck disable=SC2329
bond_test_stacked_header_parse()
{
	local devdummy="test-dummy0"
	local devgre="test-gre0"
	local devbond0="test-bond0"
	local devbond1="test-bond1"

	# shellcheck disable=SC2034
	RET=0

	# Setup: dummy -> gre -> bond0 -> bond1
	ip link add name "$devdummy" type dummy
	ip addr add 10.0.0.1/24 dev "$devdummy"
	ip link set "$devdummy" up

	ip link add name "$devgre" type gre local 10.0.0.1

	ip link add name "$devbond0" type bond mode active-backup
	ip link add name "$devbond1" type bond mode active-backup

	ip link set "$devgre" master "$devbond0"
	ip link set "$devbond0" master "$devbond1"

	ip link set "$devgre" up
	ip link set "$devbond0" up
	ip link set "$devbond1" up

	# tcpdump on a non-Ethernet bond uses AF_PACKET SOCK_DGRAM (cooked
	# capture), which triggers dev_parse_header() -> bond_header_parse()
	# on receive. With the bug, this recurses infinitely.
	timeout 5 tcpdump -c 1 -i "$devbond1" >/dev/null 2>&1 &
	local tcpdump_pid=$!
	sleep 1

	# Send a GRE packet to 10.0.0.1 so it arrives via gre -> bond0 -> bond1
	python3 -c "from scapy.all import *; send(IP(src='10.0.0.2', dst='10.0.0.1')/GRE()/IP()/UDP(), verbose=0)"
	check_err $? "failed to send GRE packet (scapy installed?)"

	wait "$tcpdump_pid" 2>/dev/null

	ip link del "$devbond1" 2>/dev/null
	ip link del "$devbond0" 2>/dev/null
	ip link del "$devgre" 2>/dev/null
	ip link del "$devdummy" 2>/dev/null

	log_test "Stacked bond header_parse does not recurse"
}

tests_run

exit "$EXIT_STATUS"
