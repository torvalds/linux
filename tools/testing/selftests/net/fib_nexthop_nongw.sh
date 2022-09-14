#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# ns: h1               | ns: h2
#   192.168.0.1/24     |
#            eth0      |
#                      |       192.168.1.1/32
#            veth0 <---|---> veth1
# Validate source address selection for route without gateway

PAUSE_ON_FAIL=no
VERBOSE=0
ret=0

################################################################################
# helpers

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
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi

	[ "$VERBOSE" = "1" ] && echo
}

run_cmd()
{
	local cmd="$*"
	local out
	local rc

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: $cmd"
	fi

	out=$(eval $cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "$out"
	fi

	[ "$VERBOSE" = "1" ] && echo

	return $rc
}

################################################################################
# config
setup()
{
	ip netns add h1
	ip -n h1 link set lo up
	ip netns add h2
	ip -n h2 link set lo up

	# Add a fake eth0 to support an ip address
	ip -n h1 link add name eth0 type dummy
	ip -n h1 link set eth0 up
	ip -n h1 address add 192.168.0.1/24 dev eth0

	# Configure veths (same @mac, arp off)
	ip -n h1 link add name veth0 type veth peer name veth1 netns h2
	ip -n h1 link set veth0 up

	ip -n h2 link set veth1 up

	# Configure @IP in the peer netns
	ip -n h2 address add 192.168.1.1/32 dev veth1
	ip -n h2 route add default dev veth1

	# Add a nexthop without @gw and use it in a route
	ip -n h1 nexthop add id 1 dev veth0
	ip -n h1 route add 192.168.1.1 nhid 1
}

cleanup()
{
	ip netns del h1 2>/dev/null
	ip netns del h2 2>/dev/null
}

trap cleanup EXIT

################################################################################
# main

while getopts :pv o
do
	case $o in
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=1;;
	esac
done

cleanup
setup

run_cmd ip -netns h1 route get 192.168.1.1
log_test $? 0 "nexthop: get route with nexthop without gw"
run_cmd ip netns exec h1 ping -c1 192.168.1.1
log_test $? 0 "nexthop: ping through nexthop without gw"

exit $ret
