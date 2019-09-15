#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# ns: me               | ns: peer              | ns: remote
#   2001:db8:91::1     |       2001:db8:91::2  |
#   172.16.1.1         |       172.16.1.2      |
#            veth1 <---|---> veth2             |
#                      |              veth5 <--|--> veth6  172.16.101.1
#            veth3 <---|---> veth4             |           2001:db8:101::1
#   172.16.2.1         |       172.16.2.2      |
#   2001:db8:92::1     |       2001:db8:92::2  |
#
# This test is for checking IPv4 and IPv6 FIB behavior with nexthop
# objects. Device reference counts and network namespace cleanup tested
# by use of network namespace for peer.

ret=0
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# all tests in this script. Can be overridden with -t option
IPV4_TESTS="ipv4_fcnal ipv4_grp_fcnal ipv4_withv6_fcnal ipv4_fcnal_runtime"
IPV6_TESTS="ipv6_fcnal ipv6_grp_fcnal ipv6_fcnal_runtime"

ALL_TESTS="basic ${IPV4_TESTS} ${IPV6_TESTS}"
TESTS="${ALL_TESTS}"
VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no

nsid=100

################################################################################
# utilities

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

get_linklocal()
{
	local dev=$1
	local ns
	local addr

	[ -n "$2" ] && ns="-netns $2"
	addr=$(ip $ns -6 -br addr show dev ${dev} | \
	awk '{
		for (i = 3; i <= NF; ++i) {
			if ($i ~ /^fe80/)
				print $i
		}
	}'
	)
	addr=${addr/\/*}

	[ -z "$addr" ] && return 1

	echo $addr

	return 0
}

create_ns()
{
	local n=${1}

	ip netns del ${n} 2>/dev/null

	set -e
	ip netns add ${n}
	ip netns set ${n} $((nsid++))
	ip -netns ${n} addr add 127.0.0.1/8 dev lo
	ip -netns ${n} link set lo up

	ip netns exec ${n} sysctl -qw net.ipv4.ip_forward=1
	ip netns exec ${n} sysctl -qw net.ipv4.fib_multipath_use_neigh=1
	ip netns exec ${n} sysctl -qw net.ipv4.conf.default.ignore_routes_with_linkdown=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.all.forwarding=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.default.forwarding=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.default.ignore_routes_with_linkdown=1
	ip netns exec ${n} sysctl -qw net.ipv6.conf.all.accept_dad=0
	ip netns exec ${n} sysctl -qw net.ipv6.conf.default.accept_dad=0

	set +e
}

setup()
{
	cleanup

	create_ns me
	create_ns peer
	create_ns remote

	IP="ip -netns me"
	set -e
	$IP li add veth1 type veth peer name veth2
	$IP li set veth1 up
	$IP addr add 172.16.1.1/24 dev veth1
	$IP -6 addr add 2001:db8:91::1/64 dev veth1

	$IP li add veth3 type veth peer name veth4
	$IP li set veth3 up
	$IP addr add 172.16.2.1/24 dev veth3
	$IP -6 addr add 2001:db8:92::1/64 dev veth3

	$IP li set veth2 netns peer up
	ip -netns peer addr add 172.16.1.2/24 dev veth2
	ip -netns peer -6 addr add 2001:db8:91::2/64 dev veth2

	$IP li set veth4 netns peer up
	ip -netns peer addr add 172.16.2.2/24 dev veth4
	ip -netns peer -6 addr add 2001:db8:92::2/64 dev veth4

	ip -netns remote li add veth5 type veth peer name veth6
	ip -netns remote li set veth5 up
	ip -netns remote addr add dev veth5 172.16.101.1/24
	ip -netns remote addr add dev veth5 2001:db8:101::1/64
	ip -netns remote ro add 172.16.0.0/22 via 172.16.101.2
	ip -netns remote -6 ro add 2001:db8:90::/40 via 2001:db8:101::2

	ip -netns remote li set veth6 netns peer up
	ip -netns peer addr add dev veth6 172.16.101.2/24
	ip -netns peer addr add dev veth6 2001:db8:101::2/64
	set +e
}

cleanup()
{
	local ns

	for ns in me peer remote; do
		ip netns del ${ns} 2>/dev/null
	done
}

check_output()
{
	local out="$1"
	local expected="$2"
	local rc=0

	[ "${out}" = "${expected}" ] && return 0

	if [ -z "${out}" ]; then
		if [ "$VERBOSE" = "1" ]; then
			printf "\nNo entry found\n"
			printf "Expected:\n"
			printf "    ${expected}\n"
		fi
		return 1
	fi

	out=$(echo ${out})
	if [ "${out}" != "${expected}" ]; then
		rc=1
		if [ "${VERBOSE}" = "1" ]; then
			printf "    Unexpected entry. Have:\n"
			printf "        ${out}\n"
			printf "    Expected:\n"
			printf "        ${expected}\n\n"
		else
			echo "      WARNING: Unexpected route entry"
		fi
	fi

	return $rc
}

check_nexthop()
{
	local nharg="$1"
	local expected="$2"
	local out

	out=$($IP nexthop ls ${nharg} 2>/dev/null)

	check_output "${out}" "${expected}"
}

check_route()
{
	local pfx="$1"
	local expected="$2"
	local out

	out=$($IP route ls match ${pfx} 2>/dev/null)

	check_output "${out}" "${expected}"
}

check_route6()
{
	local pfx="$1"
	local expected="$2"
	local out

	out=$($IP -6 route ls match ${pfx} 2>/dev/null)

	check_output "${out}" "${expected}"
}

################################################################################
# basic operations (add, delete, replace) on nexthops and nexthop groups
#
# IPv6

ipv6_fcnal()
{
	local rc

	echo
	echo "IPv6"
	echo "----------------------"

	run_cmd "$IP nexthop add id 52 via 2001:db8:91::2 dev veth1"
	rc=$?
	log_test $rc 0 "Create nexthop with id, gw, dev"
	if [ $rc -ne 0 ]; then
		echo "Basic IPv6 create fails; can not continue"
		return 1
	fi

	run_cmd "$IP nexthop get id 52"
	log_test $? 0 "Get nexthop by id"
	check_nexthop "id 52" "id 52 via 2001:db8:91::2 dev veth1 scope link"

	run_cmd "$IP nexthop del id 52"
	log_test $? 0 "Delete nexthop by id"
	check_nexthop "id 52" ""

	#
	# gw, device spec
	#
	# gw validation, no device - fails since dev required
	run_cmd "$IP nexthop add id 52 via 2001:db8:92::3"
	log_test $? 2 "Create nexthop - gw only"

	# gw is not reachable throught given dev
	run_cmd "$IP nexthop add id 53 via 2001:db8:3::3 dev veth1"
	log_test $? 2 "Create nexthop - invalid gw+dev combination"

	# onlink arg overrides gw+dev lookup
	run_cmd "$IP nexthop add id 53 via 2001:db8:3::3 dev veth1 onlink"
	log_test $? 0 "Create nexthop - gw+dev and onlink"

	# admin down should delete nexthops
	set -e
	run_cmd "$IP -6 nexthop add id 55 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 56 via 2001:db8:91::4 dev veth1"
	run_cmd "$IP nexthop add id 57 via 2001:db8:91::5 dev veth1"
	run_cmd "$IP li set dev veth1 down"
	set +e
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops removed on admin down"
}

ipv6_grp_fcnal()
{
	local rc

	echo
	echo "IPv6 groups functional"
	echo "----------------------"

	# basic functionality: create a nexthop group, default weight
	run_cmd "$IP nexthop add id 61 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 61"
	log_test $? 0 "Create nexthop group with single nexthop"

	# get nexthop group
	run_cmd "$IP nexthop get id 101"
	log_test $? 0 "Get nexthop group by id"
	check_nexthop "id 101" "id 101 group 61"

	# delete nexthop group
	run_cmd "$IP nexthop del id 101"
	log_test $? 0 "Delete nexthop group by id"
	check_nexthop "id 101" ""

	$IP nexthop flush >/dev/null 2>&1
	check_nexthop "id 101" ""

	#
	# create group with multiple nexthops - mix of gw and dev only
	#
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::4 dev veth1"
	run_cmd "$IP nexthop add id 65 dev veth1"
	run_cmd "$IP nexthop add id 102 group 62/63/64/65"
	log_test $? 0 "Nexthop group with multiple nexthops"
	check_nexthop "id 102" "id 102 group 62/63/64/65"

	# Delete nexthop in a group and group is updated
	run_cmd "$IP nexthop del id 63"
	check_nexthop "id 102" "id 102 group 62/64/65"
	log_test $? 0 "Nexthop group updated when entry is deleted"

	# create group with multiple weighted nexthops
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 103 group 62/63,2/64,3/65,4"
	log_test $? 0 "Nexthop group with weighted nexthops"
	check_nexthop "id 103" "id 103 group 62/63,2/64,3/65,4"

	# Delete nexthop in a weighted group and group is updated
	run_cmd "$IP nexthop del id 63"
	check_nexthop "id 103" "id 103 group 62/64,3/65,4"
	log_test $? 0 "Weighted nexthop group updated when entry is deleted"

	# admin down - nexthop is removed from group
	run_cmd "$IP li set dev veth1 down"
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops in groups removed on admin down"

	# expect groups to have been deleted as well
	check_nexthop "" ""

	run_cmd "$IP li set dev veth1 up"

	$IP nexthop flush >/dev/null 2>&1

	# group with nexthops using different devices
	set -e
	run_cmd "$IP nexthop add id 62 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP nexthop add id 63 via 2001:db8:91::3 dev veth1"
	run_cmd "$IP nexthop add id 64 via 2001:db8:91::4 dev veth1"
	run_cmd "$IP nexthop add id 65 via 2001:db8:91::5 dev veth1"

	run_cmd "$IP nexthop add id 72 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 73 via 2001:db8:92::3 dev veth3"
	run_cmd "$IP nexthop add id 74 via 2001:db8:92::4 dev veth3"
	run_cmd "$IP nexthop add id 75 via 2001:db8:92::5 dev veth3"
	set +e

	# multiple groups with same nexthop
	run_cmd "$IP nexthop add id 104 group 62"
	run_cmd "$IP nexthop add id 105 group 62"
	check_nexthop "group" "id 104 group 62 id 105 group 62"
	log_test $? 0 "Multiple groups with same nexthop"

	run_cmd "$IP nexthop flush groups"
	[ $? -ne 0 ] && return 1

	# on admin down of veth1, it should be removed from the group
	run_cmd "$IP nexthop add id 105 group 62/63/72/73/64"
	run_cmd "$IP li set veth1 down"
	check_nexthop "id 105" "id 105 group 72/73"
	log_test $? 0 "Nexthops in group removed on admin down - mixed group"

	run_cmd "$IP nexthop add id 106 group 105/74"
	log_test $? 2 "Nexthop group can not have a group as an entry"

	# a group can have a blackhole entry only if it is the only
	# nexthop in the group. Needed for atomic replace with an
	# actual nexthop group
	run_cmd "$IP -6 nexthop add id 31 blackhole"
	run_cmd "$IP nexthop add id 107 group 31"
	log_test $? 0 "Nexthop group with a blackhole entry"

	run_cmd "$IP nexthop add id 108 group 31/24"
	log_test $? 2 "Nexthop group can not have a blackhole and another nexthop"
}

ipv6_fcnal_runtime()
{
	local rc

	echo
	echo "IPv6 functional runtime"
	echo "-----------------------"

	sleep 5

	#
	# IPv6 - the basics
	#
	run_cmd "$IP nexthop add id 81 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 81"
	log_test $? 0 "Route add"

	run_cmd "$IP ro delete 2001:db8:101::1/128 nhid 81"
	log_test $? 0 "Route delete"

	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 81"
	run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
	log_test $? 0 "Ping with nexthop"

	run_cmd "$IP nexthop add id 82 via 2001:db8:92::2 dev veth3"
	run_cmd "$IP nexthop add id 122 group 81/82"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 122"
	run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
	log_test $? 0 "Ping - multipath"

	#
	# IPv6 with blackhole nexthops
	#
	run_cmd "$IP -6 nexthop add id 83 blackhole"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 83"
	run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
	log_test $? 2 "Ping - blackhole"

	run_cmd "$IP nexthop replace id 83 via 2001:db8:91::2 dev veth1"
	run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
	log_test $? 0 "Ping - blackhole replaced with gateway"

	run_cmd "$IP -6 nexthop replace id 83 blackhole"
	run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
	log_test $? 2 "Ping - gateway replaced by blackhole"

	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 122"
	run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
	if [ $? -eq 0 ]; then
		run_cmd "$IP nexthop replace id 122 group 83"
		run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
		log_test $? 2 "Ping - group with blackhole"

		run_cmd "$IP nexthop replace id 122 group 81/82"
		run_cmd "ip netns exec me ping -c1 -w1 2001:db8:101::1"
		log_test $? 0 "Ping - group blackhole replaced with gateways"
	else
		log_test 2 0 "Ping - multipath failed"
	fi

	#
	# device only and gw + dev only mix
	#
	run_cmd "$IP -6 nexthop add id 85 dev veth1"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 85"
	log_test $? 0 "IPv6 route with device only nexthop"
	check_route6 "2001:db8:101::1" "2001:db8:101::1 nhid 85 dev veth1 metric 1024 pref medium"

	run_cmd "$IP nexthop add id 123 group 81/85"
	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 123"
	log_test $? 0 "IPv6 multipath route with nexthop mix - dev only + gw"
	check_route6 "2001:db8:101::1" "2001:db8:101::1 nhid 123 metric 1024 nexthop via 2001:db8:91::2 dev veth1 weight 1 nexthop dev veth1 weight 1 pref medium"

	#
	# IPv6 route with v4 nexthop - not allowed
	#
	run_cmd "$IP ro delete 2001:db8:101::1/128"
	run_cmd "$IP nexthop add id 84 via 172.16.1.1 dev veth1"
	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 84"
	log_test $? 2 "IPv6 route can not have a v4 gateway"

	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 81"
	run_cmd "$IP nexthop replace id 81 via 172.16.1.1 dev veth1"
	log_test $? 2 "Nexthop replace - v6 route, v4 nexthop"

	run_cmd "$IP ro replace 2001:db8:101::1/128 nhid 122"
	run_cmd "$IP nexthop replace id 81 via 172.16.1.1 dev veth1"
	log_test $? 2 "Nexthop replace of group entry - v6 route, v4 nexthop"

	$IP nexthop flush >/dev/null 2>&1

	#
	# weird IPv6 cases
	#
	run_cmd "$IP nexthop add id 86 via 2001:db8:91::2 dev veth1"
	run_cmd "$IP ro add 2001:db8:101::1/128 nhid 81"

	# TO-DO:
	# existing route with old nexthop; append route with new nexthop
	# existing route with old nexthop; replace route with new
	# existing route with new nexthop; replace route with old
	# route with src address and using nexthop - not allowed
}

ipv4_fcnal()
{
	local rc

	echo
	echo "IPv4 functional"
	echo "----------------------"

	#
	# basic IPv4 ops - add, get, delete
	#
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	rc=$?
	log_test $rc 0 "Create nexthop with id, gw, dev"
	if [ $rc -ne 0 ]; then
		echo "Basic IPv4 create fails; can not continue"
		return 1
	fi

	run_cmd "$IP nexthop get id 12"
	log_test $? 0 "Get nexthop by id"
	check_nexthop "id 12" "id 12 via 172.16.1.2 dev veth1 scope link"

	run_cmd "$IP nexthop del id 12"
	log_test $? 0 "Delete nexthop by id"
	check_nexthop "id 52" ""

	#
	# gw, device spec
	#
	# gw validation, no device - fails since dev is required
	run_cmd "$IP nexthop add id 12 via 172.16.2.3"
	log_test $? 2 "Create nexthop - gw only"

	# gw not reachable through given dev
	run_cmd "$IP nexthop add id 13 via 172.16.3.2 dev veth1"
	log_test $? 2 "Create nexthop - invalid gw+dev combination"

	# onlink flag overrides gw+dev lookup
	run_cmd "$IP nexthop add id 13 via 172.16.3.2 dev veth1 onlink"
	log_test $? 0 "Create nexthop - gw+dev and onlink"

	# admin down should delete nexthops
	set -e
	run_cmd "$IP nexthop add id 15 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 16 via 172.16.1.4 dev veth1"
	run_cmd "$IP nexthop add id 17 via 172.16.1.5 dev veth1"
	run_cmd "$IP li set dev veth1 down"
	set +e
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops removed on admin down"
}

ipv4_grp_fcnal()
{
	local rc

	echo
	echo "IPv4 groups functional"
	echo "----------------------"

	# basic functionality: create a nexthop group, default weight
	run_cmd "$IP nexthop add id 11 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 11"
	log_test $? 0 "Create nexthop group with single nexthop"

	# get nexthop group
	run_cmd "$IP nexthop get id 101"
	log_test $? 0 "Get nexthop group by id"
	check_nexthop "id 101" "id 101 group 11"

	# delete nexthop group
	run_cmd "$IP nexthop del id 101"
	log_test $? 0 "Delete nexthop group by id"
	check_nexthop "id 101" ""

	$IP nexthop flush >/dev/null 2>&1

	#
	# create group with multiple nexthops
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 14 via 172.16.1.4 dev veth1"
	run_cmd "$IP nexthop add id 15 via 172.16.1.5 dev veth1"
	run_cmd "$IP nexthop add id 102 group 12/13/14/15"
	log_test $? 0 "Nexthop group with multiple nexthops"
	check_nexthop "id 102" "id 102 group 12/13/14/15"

	# Delete nexthop in a group and group is updated
	run_cmd "$IP nexthop del id 13"
	check_nexthop "id 102" "id 102 group 12/14/15"
	log_test $? 0 "Nexthop group updated when entry is deleted"

	# create group with multiple weighted nexthops
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 103 group 12/13,2/14,3/15,4"
	log_test $? 0 "Nexthop group with weighted nexthops"
	check_nexthop "id 103" "id 103 group 12/13,2/14,3/15,4"

	# Delete nexthop in a weighted group and group is updated
	run_cmd "$IP nexthop del id 13"
	check_nexthop "id 103" "id 103 group 12/14,3/15,4"
	log_test $? 0 "Weighted nexthop group updated when entry is deleted"

	# admin down - nexthop is removed from group
	run_cmd "$IP li set dev veth1 down"
	check_nexthop "dev veth1" ""
	log_test $? 0 "Nexthops in groups removed on admin down"

	# expect groups to have been deleted as well
	check_nexthop "" ""

	run_cmd "$IP li set dev veth1 up"

	$IP nexthop flush >/dev/null 2>&1

	# group with nexthops using different devices
	set -e
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 13 via 172.16.1.3 dev veth1"
	run_cmd "$IP nexthop add id 14 via 172.16.1.4 dev veth1"
	run_cmd "$IP nexthop add id 15 via 172.16.1.5 dev veth1"

	run_cmd "$IP nexthop add id 22 via 172.16.2.2 dev veth3"
	run_cmd "$IP nexthop add id 23 via 172.16.2.3 dev veth3"
	run_cmd "$IP nexthop add id 24 via 172.16.2.4 dev veth3"
	run_cmd "$IP nexthop add id 25 via 172.16.2.5 dev veth3"
	set +e

	# multiple groups with same nexthop
	run_cmd "$IP nexthop add id 104 group 12"
	run_cmd "$IP nexthop add id 105 group 12"
	check_nexthop "group" "id 104 group 12 id 105 group 12"
	log_test $? 0 "Multiple groups with same nexthop"

	run_cmd "$IP nexthop flush groups"
	[ $? -ne 0 ] && return 1

	# on admin down of veth1, it should be removed from the group
	run_cmd "$IP nexthop add id 105 group 12/13/22/23/14"
	run_cmd "$IP li set veth1 down"
	check_nexthop "id 105" "id 105 group 22/23"
	log_test $? 0 "Nexthops in group removed on admin down - mixed group"

	run_cmd "$IP nexthop add id 106 group 105/24"
	log_test $? 2 "Nexthop group can not have a group as an entry"

	# a group can have a blackhole entry only if it is the only
	# nexthop in the group. Needed for atomic replace with an
	# actual nexthop group
	run_cmd "$IP nexthop add id 31 blackhole"
	run_cmd "$IP nexthop add id 107 group 31"
	log_test $? 0 "Nexthop group with a blackhole entry"

	run_cmd "$IP nexthop add id 108 group 31/24"
	log_test $? 2 "Nexthop group can not have a blackhole and another nexthop"
}

ipv4_withv6_fcnal()
{
	local lladdr

	set -e
	lladdr=$(get_linklocal veth2 peer)
	run_cmd "$IP nexthop add id 11 via ${lladdr} dev veth1"
	set +e
	run_cmd "$IP ro add 172.16.101.1/32 nhid 11"
	log_test $? 0 "IPv6 nexthop with IPv4 route"
	check_route "172.16.101.1" "172.16.101.1 nhid 11 via inet6 ${lladdr} dev veth1"

	set -e
	run_cmd "$IP nexthop add id 12 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 11/12"
	set +e
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 101"
	log_test $? 0 "IPv6 nexthop with IPv4 route"

	check_route "172.16.101.1" "172.16.101.1 nhid 101 nexthop via inet6 ${lladdr} dev veth1 weight 1 nexthop via 172.16.1.2 dev veth1 weight 1"

	run_cmd "$IP ro replace 172.16.101.1/32 via inet6 ${lladdr} dev veth1"
	log_test $? 0 "IPv4 route with IPv6 gateway"
	check_route "172.16.101.1" "172.16.101.1 via inet6 ${lladdr} dev veth1"

	run_cmd "$IP ro replace 172.16.101.1/32 via inet6 2001:db8:50::1 dev veth1"
	log_test $? 2 "IPv4 route with invalid IPv6 gateway"
}

ipv4_fcnal_runtime()
{
	local lladdr
	local rc

	echo
	echo "IPv4 functional runtime"
	echo "-----------------------"

	run_cmd "$IP nexthop add id 21 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro add 172.16.101.1/32 nhid 21"
	log_test $? 0 "Route add"
	check_route "172.16.101.1" "172.16.101.1 nhid 21 via 172.16.1.2 dev veth1"

	run_cmd "$IP ro delete 172.16.101.1/32 nhid 21"
	log_test $? 0 "Route delete"

	#
	# scope mismatch
	#
	run_cmd "$IP nexthop add id 22 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro add 172.16.101.1/32 nhid 22 scope host"
	log_test $? 2 "Route add - scope conflict with nexthop"

	run_cmd "$IP nexthop replace id 22 dev veth3"
	run_cmd "$IP ro add 172.16.101.1/32 nhid 22 scope host"
	run_cmd "$IP nexthop replace id 22 via 172.16.2.2 dev veth3"
	log_test $? 2 "Nexthop replace with invalid scope for existing route"

	#
	# add route with nexthop and check traffic
	#
	run_cmd "$IP nexthop replace id 21 via 172.16.1.2 dev veth1"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 21"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 0 "Basic ping"

	run_cmd "$IP nexthop replace id 22 via 172.16.2.2 dev veth3"
	run_cmd "$IP nexthop add id 122 group 21/22"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 122"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 0 "Ping - multipath"

	#
	# IPv4 with blackhole nexthops
	#
	run_cmd "$IP nexthop add id 23 blackhole"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 23"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 2 "Ping - blackhole"

	run_cmd "$IP nexthop replace id 23 via 172.16.1.2 dev veth1"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 0 "Ping - blackhole replaced with gateway"

	run_cmd "$IP nexthop replace id 23 blackhole"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 2 "Ping - gateway replaced by blackhole"

	run_cmd "$IP ro replace 172.16.101.1/32 nhid 122"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	if [ $? -eq 0 ]; then
		run_cmd "$IP nexthop replace id 122 group 23"
		run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
		log_test $? 2 "Ping - group with blackhole"

		run_cmd "$IP nexthop replace id 122 group 21/22"
		run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
		log_test $? 0 "Ping - group blackhole replaced with gateways"
	else
		log_test 2 0 "Ping - multipath failed"
	fi

	#
	# device only and gw + dev only mix
	#
	run_cmd "$IP nexthop add id 85 dev veth1"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 85"
	log_test $? 0 "IPv4 route with device only nexthop"
	check_route "172.16.101.1" "172.16.101.1 nhid 85 dev veth1"

	run_cmd "$IP nexthop add id 123 group 21/85"
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 123"
	log_test $? 0 "IPv4 multipath route with nexthop mix - dev only + gw"
	check_route "172.16.101.1" "172.16.101.1 nhid 123 nexthop via 172.16.1.2 dev veth1 weight 1 nexthop dev veth1 weight 1"

	#
	# IPv4 with IPv6
	#
	set -e
	lladdr=$(get_linklocal veth2 peer)
	run_cmd "$IP nexthop add id 24 via ${lladdr} dev veth1"
	set +e
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 24"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 0 "IPv6 nexthop with IPv4 route"

	$IP neigh sh | grep -q "${lladdr} dev veth1"
	if [ $? -eq 1 ]; then
		echo "    WARNING: Neigh entry missing for ${lladdr}"
		$IP neigh sh | grep 'dev veth1'
	fi

	$IP neigh sh | grep -q "172.16.101.1 dev eth1"
	if [ $? -eq 0 ]; then
		echo "    WARNING: Neigh entry exists for 172.16.101.1"
		$IP neigh sh | grep 'dev veth1'
	fi

	set -e
	run_cmd "$IP nexthop add id 25 via 172.16.1.2 dev veth1"
	run_cmd "$IP nexthop add id 101 group 24/25"
	set +e
	run_cmd "$IP ro replace 172.16.101.1/32 nhid 101"
	log_test $? 0 "IPv4 route with mixed v4-v6 multipath route"

	check_route "172.16.101.1" "172.16.101.1 nhid 101 nexthop via inet6 ${lladdr} dev veth1 weight 1 nexthop via 172.16.1.2 dev veth1 weight 1"

	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 0 "IPv6 nexthop with IPv4 route"

	run_cmd "$IP ro replace 172.16.101.1/32 via inet6 ${lladdr} dev veth1"
	run_cmd "ip netns exec me ping -c1 -w1 172.16.101.1"
	log_test $? 0 "IPv4 route with IPv6 gateway"

	$IP neigh sh | grep -q "${lladdr} dev veth1"
	if [ $? -eq 1 ]; then
		echo "    WARNING: Neigh entry missing for ${lladdr}"
		$IP neigh sh | grep 'dev veth1'
	fi

	$IP neigh sh | grep -q "172.16.101.1 dev eth1"
	if [ $? -eq 0 ]; then
		echo "    WARNING: Neigh entry exists for 172.16.101.1"
		$IP neigh sh | grep 'dev veth1'
	fi

	#
	# MPLS as an example of LWT encap
	#
	run_cmd "$IP nexthop add id 51 encap mpls 101 via 172.16.1.2 dev veth1"
	log_test $? 0 "IPv4 route with MPLS encap"
	check_nexthop "id 51" "id 51 encap mpls 101 via 172.16.1.2 dev veth1 scope link"
	log_test $? 0 "IPv4 route with MPLS encap - check"

	run_cmd "$IP nexthop add id 52 encap mpls 102 via inet6 2001:db8:91::2 dev veth1"
	log_test $? 0 "IPv4 route with MPLS encap and v6 gateway"
	check_nexthop "id 52" "id 52 encap mpls 102 via 2001:db8:91::2 dev veth1 scope link"
	log_test $? 0 "IPv4 route with MPLS encap, v6 gw - check"
}

basic()
{
	echo
	echo "Basic functional tests"
	echo "----------------------"
	run_cmd "$IP nexthop ls"
	log_test $? 0 "List with nothing defined"

	run_cmd "$IP nexthop get id 1"
	log_test $? 2 "Nexthop get on non-existent id"

	# attempt to create nh without a device or gw - fails
	run_cmd "$IP nexthop add id 1"
	log_test $? 2 "Nexthop with no device or gateway"

	# attempt to create nh with down device - fails
	$IP li set veth1 down
	run_cmd "$IP nexthop add id 1 dev veth1"
	log_test $? 2 "Nexthop with down device"

	# create nh with linkdown device - fails
	$IP li set veth1 up
	ip -netns peer li set veth2 down
	run_cmd "$IP nexthop add id 1 dev veth1"
	log_test $? 2 "Nexthop with device that is linkdown"
	ip -netns peer li set veth2 up

	# device only
	run_cmd "$IP nexthop add id 1 dev veth1"
	log_test $? 0 "Nexthop with device only"

	# create nh with duplicate id
	run_cmd "$IP nexthop add id 1 dev veth3"
	log_test $? 2 "Nexthop with duplicate id"

	# blackhole nexthop
	run_cmd "$IP nexthop add id 2 blackhole"
	log_test $? 0 "Blackhole nexthop"

	# blackhole nexthop can not have other specs
	run_cmd "$IP nexthop replace id 2 blackhole dev veth1"
	log_test $? 2 "Blackhole nexthop with other attributes"

	#
	# groups
	#

	run_cmd "$IP nexthop add id 101 group 1"
	log_test $? 0 "Create group"

	run_cmd "$IP nexthop add id 102 group 2"
	log_test $? 0 "Create group with blackhole nexthop"

	# multipath group can not have a blackhole as 1 path
	run_cmd "$IP nexthop add id 103 group 1/2"
	log_test $? 2 "Create multipath group where 1 path is a blackhole"

	# multipath group can not have a member replaced by a blackhole
	run_cmd "$IP nexthop replace id 2 dev veth3"
	run_cmd "$IP nexthop replace id 102 group 1/2"
	run_cmd "$IP nexthop replace id 2 blackhole"
	log_test $? 2 "Multipath group can not have a member replaced by blackhole"

	# attempt to create group with non-existent nexthop
	run_cmd "$IP nexthop add id 103 group 12"
	log_test $? 2 "Create group with non-existent nexthop"

	# attempt to create group with same nexthop
	run_cmd "$IP nexthop add id 103 group 1/1"
	log_test $? 2 "Create group with same nexthop multiple times"

	# replace nexthop with a group - fails
	run_cmd "$IP nexthop replace id 2 group 1"
	log_test $? 2 "Replace nexthop with nexthop group"

	# replace nexthop group with a nexthop - fails
	run_cmd "$IP nexthop replace id 101 dev veth1"
	log_test $? 2 "Replace nexthop group with nexthop"

	# nexthop group with other attributes fail
	run_cmd "$IP nexthop add id 104 group 1 dev veth1"
	log_test $? 2 "Nexthop group and device"

	run_cmd "$IP nexthop add id 104 group 1 blackhole"
	log_test $? 2 "Nexthop group and blackhole"

	$IP nexthop flush >/dev/null 2>&1
}

################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $ALL_TESTS)
        -4          IPv4 tests only
        -6          IPv6 tests only
        -p          Pause on fail
        -P          Pause after each test before cleanup
        -v          verbose mode (show commands and output)

    Runtime test
	-n num	    Number of nexthops to target
	-N    	    Use new style to install routes in DUT

done
EOF
}

################################################################################
# main

while getopts :t:pP46hv o
do
	case $o in
		t) TESTS=$OPTARG;;
		4) TESTS=${IPV4_TESTS};;
		6) TESTS=${IPV6_TESTS};;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# make sure we don't pause twice
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip help 2>&1 | grep -q nexthop
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing nexthop command"
	exit $ksft_skip
fi

out=$(ip nexthop ls 2>&1 | grep -q "Operation not supported")
if [ $? -eq 0 ]; then
	echo "SKIP: kernel lacks nexthop support"
	exit $ksft_skip
fi

for t in $TESTS
do
	case $t in
	none) IP="ip -netns peer"; setup; exit 0;;
	*) setup; $t; cleanup;;
	esac
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
