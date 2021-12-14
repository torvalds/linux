#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking IPv4 and IPv6 FIB behavior in response to
# different events.

ret=0
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# all tests in this script. Can be overridden with -t option
TESTS="unregister down carrier nexthop suppress ipv6_rt ipv4_rt ipv6_addr_metric ipv4_addr_metric ipv6_route_metrics ipv4_route_metrics ipv4_route_v6_gw rp_filter ipv4_del_addr"

VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no
IP="ip -netns ns1"
NS_EXEC="ip netns exec ns1"

which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "    TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "    TEST: %-60s  [FAIL]\n" "${msg}"
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
}

setup()
{
	set -e
	ip netns add ns1
	ip netns set ns1 auto
	$IP link set dev lo up
	ip netns exec ns1 sysctl -qw net.ipv4.ip_forward=1
	ip netns exec ns1 sysctl -qw net.ipv6.conf.all.forwarding=1

	$IP link add dummy0 type dummy
	$IP link set dev dummy0 up
	$IP address add 198.51.100.1/24 dev dummy0
	$IP -6 address add 2001:db8:1::1/64 dev dummy0
	set +e

}

cleanup()
{
	$IP link del dev dummy0 &> /dev/null
	ip netns del ns1
	ip netns del ns2 &> /dev/null
}

get_linklocal()
{
	local dev=$1
	local addr

	addr=$($IP -6 -br addr show dev ${dev} | \
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

fib_unreg_unicast_test()
{
	echo
	echo "Single path route test"

	setup

	echo "    Start point"
	$IP route get fibmatch 198.51.100.2 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	set -e
	$IP link del dev dummy0
	set +e

	echo "    Nexthop device deleted"
	$IP route get fibmatch 198.51.100.2 &> /dev/null
	log_test $? 2 "IPv4 fibmatch - no route"
	$IP -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	log_test $? 2 "IPv6 fibmatch - no route"

	cleanup
}

fib_unreg_multipath_test()
{

	echo
	echo "Multipath route test"

	setup

	set -e
	$IP link add dummy1 type dummy
	$IP link set dev dummy1 up
	$IP address add 192.0.2.1/24 dev dummy1
	$IP -6 address add 2001:db8:2::1/64 dev dummy1

	$IP route add 203.0.113.0/24 \
		nexthop via 198.51.100.2 dev dummy0 \
		nexthop via 192.0.2.2 dev dummy1
	$IP -6 route add 2001:db8:3::/64 \
		nexthop via 2001:db8:1::2 dev dummy0 \
		nexthop via 2001:db8:2::2 dev dummy1
	set +e

	echo "    Start point"
	$IP route get fibmatch 203.0.113.1 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	set -e
	$IP link del dev dummy0
	set +e

	echo "    One nexthop device deleted"
	$IP route get fibmatch 203.0.113.1 &> /dev/null
	log_test $? 2 "IPv4 - multipath route removed on delete"

	$IP -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	# In IPv6 we do not flush the entire multipath route.
	log_test $? 0 "IPv6 - multipath down to single path"

	set -e
	$IP link del dev dummy1
	set +e

	echo "    Second nexthop device deleted"
	$IP -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	log_test $? 2 "IPv6 - no route"

	cleanup
}

fib_unreg_test()
{
	fib_unreg_unicast_test
	fib_unreg_multipath_test
}

fib_down_unicast_test()
{
	echo
	echo "Single path, admin down"

	setup

	echo "    Start point"
	$IP route get fibmatch 198.51.100.2 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	set -e
	$IP link set dev dummy0 down
	set +e

	echo "    Route deleted on down"
	$IP route get fibmatch 198.51.100.2 &> /dev/null
	log_test $? 2 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	log_test $? 2 "IPv6 fibmatch"

	cleanup
}

fib_down_multipath_test_do()
{
	local down_dev=$1
	local up_dev=$2

	$IP route get fibmatch 203.0.113.1 \
		oif $down_dev &> /dev/null
	log_test $? 2 "IPv4 fibmatch on down device"
	$IP -6 route get fibmatch 2001:db8:3::1 \
		oif $down_dev &> /dev/null
	log_test $? 2 "IPv6 fibmatch on down device"

	$IP route get fibmatch 203.0.113.1 \
		oif $up_dev &> /dev/null
	log_test $? 0 "IPv4 fibmatch on up device"
	$IP -6 route get fibmatch 2001:db8:3::1 \
		oif $up_dev &> /dev/null
	log_test $? 0 "IPv6 fibmatch on up device"

	$IP route get fibmatch 203.0.113.1 | \
		grep $down_dev | grep -q "dead linkdown"
	log_test $? 0 "IPv4 flags on down device"
	$IP -6 route get fibmatch 2001:db8:3::1 | \
		grep $down_dev | grep -q "dead linkdown"
	log_test $? 0 "IPv6 flags on down device"

	$IP route get fibmatch 203.0.113.1 | \
		grep $up_dev | grep -q "dead linkdown"
	log_test $? 1 "IPv4 flags on up device"
	$IP -6 route get fibmatch 2001:db8:3::1 | \
		grep $up_dev | grep -q "dead linkdown"
	log_test $? 1 "IPv6 flags on up device"
}

fib_down_multipath_test()
{
	echo
	echo "Admin down multipath"

	setup

	set -e
	$IP link add dummy1 type dummy
	$IP link set dev dummy1 up

	$IP address add 192.0.2.1/24 dev dummy1
	$IP -6 address add 2001:db8:2::1/64 dev dummy1

	$IP route add 203.0.113.0/24 \
		nexthop via 198.51.100.2 dev dummy0 \
		nexthop via 192.0.2.2 dev dummy1
	$IP -6 route add 2001:db8:3::/64 \
		nexthop via 2001:db8:1::2 dev dummy0 \
		nexthop via 2001:db8:2::2 dev dummy1
	set +e

	echo "    Verify start point"
	$IP route get fibmatch 203.0.113.1 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"

	$IP -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	set -e
	$IP link set dev dummy0 down
	set +e

	echo "    One device down, one up"
	fib_down_multipath_test_do "dummy0" "dummy1"

	set -e
	$IP link set dev dummy0 up
	$IP link set dev dummy1 down
	set +e

	echo "    Other device down and up"
	fib_down_multipath_test_do "dummy1" "dummy0"

	set -e
	$IP link set dev dummy0 down
	set +e

	echo "    Both devices down"
	$IP route get fibmatch 203.0.113.1 &> /dev/null
	log_test $? 2 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	log_test $? 2 "IPv6 fibmatch"

	$IP link del dev dummy1
	cleanup
}

fib_down_test()
{
	fib_down_unicast_test
	fib_down_multipath_test
}

# Local routes should not be affected when carrier changes.
fib_carrier_local_test()
{
	echo
	echo "Local carrier tests - single path"

	setup

	set -e
	$IP link set dev dummy0 carrier on
	set +e

	echo "    Start point"
	$IP route get fibmatch 198.51.100.1 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::1 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	$IP route get fibmatch 198.51.100.1 | \
		grep -q "linkdown"
	log_test $? 1 "IPv4 - no linkdown flag"
	$IP -6 route get fibmatch 2001:db8:1::1 | \
		grep -q "linkdown"
	log_test $? 1 "IPv6 - no linkdown flag"

	set -e
	$IP link set dev dummy0 carrier off
	sleep 1
	set +e

	echo "    Carrier off on nexthop"
	$IP route get fibmatch 198.51.100.1 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::1 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	$IP route get fibmatch 198.51.100.1 | \
		grep -q "linkdown"
	log_test $? 1 "IPv4 - linkdown flag set"
	$IP -6 route get fibmatch 2001:db8:1::1 | \
		grep -q "linkdown"
	log_test $? 1 "IPv6 - linkdown flag set"

	set -e
	$IP address add 192.0.2.1/24 dev dummy0
	$IP -6 address add 2001:db8:2::1/64 dev dummy0
	set +e

	echo "    Route to local address with carrier down"
	$IP route get fibmatch 192.0.2.1 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:2::1 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	$IP route get fibmatch 192.0.2.1 | \
		grep -q "linkdown"
	log_test $? 1 "IPv4 linkdown flag set"
	$IP -6 route get fibmatch 2001:db8:2::1 | \
		grep -q "linkdown"
	log_test $? 1 "IPv6 linkdown flag set"

	cleanup
}

fib_carrier_unicast_test()
{
	ret=0

	echo
	echo "Single path route carrier test"

	setup

	set -e
	$IP link set dev dummy0 carrier on
	set +e

	echo "    Start point"
	$IP route get fibmatch 198.51.100.2 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	$IP route get fibmatch 198.51.100.2 | \
		grep -q "linkdown"
	log_test $? 1 "IPv4 no linkdown flag"
	$IP -6 route get fibmatch 2001:db8:1::2 | \
		grep -q "linkdown"
	log_test $? 1 "IPv6 no linkdown flag"

	set -e
	$IP link set dev dummy0 carrier off
	sleep 1
	set +e

	echo "    Carrier down"
	$IP route get fibmatch 198.51.100.2 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	$IP route get fibmatch 198.51.100.2 | \
		grep -q "linkdown"
	log_test $? 0 "IPv4 linkdown flag set"
	$IP -6 route get fibmatch 2001:db8:1::2 | \
		grep -q "linkdown"
	log_test $? 0 "IPv6 linkdown flag set"

	set -e
	$IP address add 192.0.2.1/24 dev dummy0
	$IP -6 address add 2001:db8:2::1/64 dev dummy0
	set +e

	echo "    Second address added with carrier down"
	$IP route get fibmatch 192.0.2.2 &> /dev/null
	log_test $? 0 "IPv4 fibmatch"
	$IP -6 route get fibmatch 2001:db8:2::2 &> /dev/null
	log_test $? 0 "IPv6 fibmatch"

	$IP route get fibmatch 192.0.2.2 | \
		grep -q "linkdown"
	log_test $? 0 "IPv4 linkdown flag set"
	$IP -6 route get fibmatch 2001:db8:2::2 | \
		grep -q "linkdown"
	log_test $? 0 "IPv6 linkdown flag set"

	cleanup
}

fib_carrier_test()
{
	fib_carrier_local_test
	fib_carrier_unicast_test
}

fib_rp_filter_test()
{
	echo
	echo "IPv4 rp_filter tests"

	setup

	set -e
	ip netns add ns2
	ip netns set ns2 auto

	ip -netns ns2 link set dev lo up

	$IP link add name veth1 type veth peer name veth2
	$IP link set dev veth2 netns ns2
	$IP address add 192.0.2.1/24 dev veth1
	ip -netns ns2 address add 192.0.2.1/24 dev veth2
	$IP link set dev veth1 up
	ip -netns ns2 link set dev veth2 up

	$IP link set dev lo address 52:54:00:6a:c7:5e
	$IP link set dev veth1 address 52:54:00:6a:c7:5e
	ip -netns ns2 link set dev lo address 52:54:00:6a:c7:5e
	ip -netns ns2 link set dev veth2 address 52:54:00:6a:c7:5e

	# 1. (ns2) redirect lo's egress to veth2's egress
	ip netns exec ns2 tc qdisc add dev lo parent root handle 1: fq_codel
	ip netns exec ns2 tc filter add dev lo parent 1: protocol arp basic \
		action mirred egress redirect dev veth2
	ip netns exec ns2 tc filter add dev lo parent 1: protocol ip basic \
		action mirred egress redirect dev veth2

	# 2. (ns1) redirect veth1's ingress to lo's ingress
	$NS_EXEC tc qdisc add dev veth1 ingress
	$NS_EXEC tc filter add dev veth1 ingress protocol arp basic \
		action mirred ingress redirect dev lo
	$NS_EXEC tc filter add dev veth1 ingress protocol ip basic \
		action mirred ingress redirect dev lo

	# 3. (ns1) redirect lo's egress to veth1's egress
	$NS_EXEC tc qdisc add dev lo parent root handle 1: fq_codel
	$NS_EXEC tc filter add dev lo parent 1: protocol arp basic \
		action mirred egress redirect dev veth1
	$NS_EXEC tc filter add dev lo parent 1: protocol ip basic \
		action mirred egress redirect dev veth1

	# 4. (ns2) redirect veth2's ingress to lo's ingress
	ip netns exec ns2 tc qdisc add dev veth2 ingress
	ip netns exec ns2 tc filter add dev veth2 ingress protocol arp basic \
		action mirred ingress redirect dev lo
	ip netns exec ns2 tc filter add dev veth2 ingress protocol ip basic \
		action mirred ingress redirect dev lo

	$NS_EXEC sysctl -qw net.ipv4.conf.all.rp_filter=1
	$NS_EXEC sysctl -qw net.ipv4.conf.all.accept_local=1
	$NS_EXEC sysctl -qw net.ipv4.conf.all.route_localnet=1
	ip netns exec ns2 sysctl -qw net.ipv4.conf.all.rp_filter=1
	ip netns exec ns2 sysctl -qw net.ipv4.conf.all.accept_local=1
	ip netns exec ns2 sysctl -qw net.ipv4.conf.all.route_localnet=1
	set +e

	run_cmd "ip netns exec ns2 ping -w1 -c1 192.0.2.1"
	log_test $? 0 "rp_filter passes local packets"

	run_cmd "ip netns exec ns2 ping -w1 -c1 127.0.0.1"
	log_test $? 0 "rp_filter passes loopback packets"

	cleanup
}

################################################################################
# Tests on nexthop spec

# run 'ip route add' with given spec
add_rt()
{
	local desc="$1"
	local erc=$2
	local vrf=$3
	local pfx=$4
	local gw=$5
	local dev=$6
	local cmd out rc

	[ "$vrf" = "-" ] && vrf="default"
	[ -n "$gw" ] && gw="via $gw"
	[ -n "$dev" ] && dev="dev $dev"

	cmd="$IP route add vrf $vrf $pfx $gw $dev"
	if [ "$VERBOSE" = "1" ]; then
		printf "\n    COMMAND: $cmd\n"
	fi

	out=$(eval $cmd 2>&1)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi
	log_test $rc $erc "$desc"
}

fib4_nexthop()
{
	echo
	echo "IPv4 nexthop tests"

	echo "<<< write me >>>"
}

fib6_nexthop()
{
	local lldummy=$(get_linklocal dummy0)
	local llv1=$(get_linklocal dummy0)

	if [ -z "$lldummy" ]; then
		echo "Failed to get linklocal address for dummy0"
		return 1
	fi
	if [ -z "$llv1" ]; then
		echo "Failed to get linklocal address for veth1"
		return 1
	fi

	echo
	echo "IPv6 nexthop tests"

	add_rt "Directly connected nexthop, unicast address" 0 \
		- 2001:db8:101::/64 2001:db8:1::2
	add_rt "Directly connected nexthop, unicast address with device" 0 \
		- 2001:db8:102::/64 2001:db8:1::2 "dummy0"
	add_rt "Gateway is linklocal address" 0 \
		- 2001:db8:103::1/64 $llv1 "veth0"

	# fails because LL address requires a device
	add_rt "Gateway is linklocal address, no device" 2 \
		- 2001:db8:104::1/64 $llv1

	# local address can not be a gateway
	add_rt "Gateway can not be local unicast address" 2 \
		- 2001:db8:105::/64 2001:db8:1::1
	add_rt "Gateway can not be local unicast address, with device" 2 \
		- 2001:db8:106::/64 2001:db8:1::1 "dummy0"
	add_rt "Gateway can not be a local linklocal address" 2 \
		- 2001:db8:107::1/64 $lldummy "dummy0"

	# VRF tests
	add_rt "Gateway can be local address in a VRF" 0 \
		- 2001:db8:108::/64 2001:db8:51::2
	add_rt "Gateway can be local address in a VRF, with device" 0 \
		- 2001:db8:109::/64 2001:db8:51::2 "veth0"
	add_rt "Gateway can be local linklocal address in a VRF" 0 \
		- 2001:db8:110::1/64 $llv1 "veth0"

	add_rt "Redirect to VRF lookup" 0 \
		- 2001:db8:111::/64 "" "red"

	add_rt "VRF route, gateway can be local address in default VRF" 0 \
		red 2001:db8:112::/64 2001:db8:51::1

	# local address in same VRF fails
	add_rt "VRF route, gateway can not be a local address" 2 \
		red 2001:db8:113::1/64 2001:db8:2::1
	add_rt "VRF route, gateway can not be a local addr with device" 2 \
		red 2001:db8:114::1/64 2001:db8:2::1 "dummy1"
}

# Default VRF:
#   dummy0 - 198.51.100.1/24 2001:db8:1::1/64
#   veth0  - 192.0.2.1/24    2001:db8:51::1/64
#
# VRF red:
#   dummy1 - 192.168.2.1/24 2001:db8:2::1/64
#   veth1  - 192.0.2.2/24   2001:db8:51::2/64
#
#  [ dummy0   veth0 ]--[ veth1   dummy1 ]

fib_nexthop_test()
{
	setup

	set -e

	$IP -4 rule add pref 32765 table local
	$IP -4 rule del pref 0
	$IP -6 rule add pref 32765 table local
	$IP -6 rule del pref 0

	$IP link add red type vrf table 1
	$IP link set red up
	$IP -4 route add vrf red unreachable default metric 4278198272
	$IP -6 route add vrf red unreachable default metric 4278198272

	$IP link add veth0 type veth peer name veth1
	$IP link set dev veth0 up
	$IP address add 192.0.2.1/24 dev veth0
	$IP -6 address add 2001:db8:51::1/64 dev veth0

	$IP link set dev veth1 vrf red up
	$IP address add 192.0.2.2/24 dev veth1
	$IP -6 address add 2001:db8:51::2/64 dev veth1

	$IP link add dummy1 type dummy
	$IP link set dev dummy1 vrf red up
	$IP address add 192.168.2.1/24 dev dummy1
	$IP -6 address add 2001:db8:2::1/64 dev dummy1
	set +e

	sleep 1
	fib4_nexthop
	fib6_nexthop

	(
	$IP link del dev dummy1
	$IP link del veth0
	$IP link del red
	) 2>/dev/null
	cleanup
}

fib_suppress_test()
{
	echo
	echo "FIB rule with suppress_prefixlength"
	setup

	$IP link add dummy1 type dummy
	$IP link set dummy1 up
	$IP -6 route add default dev dummy1
	$IP -6 rule add table main suppress_prefixlength 0
	ping -f -c 1000 -W 1 1234::1 >/dev/null 2>&1
	$IP -6 rule del table main suppress_prefixlength 0
	$IP link del dummy1

	# If we got here without crashing, we're good.
	log_test 0 0 "FIB rule suppress test"

	cleanup
}

################################################################################
# Tests on route add and replace

run_cmd()
{
	local cmd="$1"
	local out
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		printf "    COMMAND: $cmd\n"
		stderr=
	fi

	out=$(eval $cmd $stderr)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi

	[ "$VERBOSE" = "1" ] && echo

	return $rc
}

check_expected()
{
	local out="$1"
	local expected="$2"
	local rc=0

	[ "${out}" = "${expected}" ] && return 0

	if [ -z "${out}" ]; then
		if [ "$VERBOSE" = "1" ]; then
			printf "\nNo route entry found\n"
			printf "Expected:\n"
			printf "    ${expected}\n"
		fi
		return 1
	fi

	# tricky way to convert output to 1-line without ip's
	# messy '\'; this drops all extra white space
	out=$(echo ${out})
	if [ "${out}" != "${expected}" ]; then
		rc=1
		if [ "${VERBOSE}" = "1" ]; then
			printf "    Unexpected route entry. Have:\n"
			printf "        ${out}\n"
			printf "    Expected:\n"
			printf "        ${expected}\n\n"
		fi
	fi

	return $rc
}

# add route for a prefix, flushing any existing routes first
# expected to be the first step of a test
add_route6()
{
	local pfx="$1"
	local nh="$2"
	local out

	if [ "$VERBOSE" = "1" ]; then
		echo
		echo "    ##################################################"
		echo
	fi

	run_cmd "$IP -6 ro flush ${pfx}"
	[ $? -ne 0 ] && exit 1

	out=$($IP -6 ro ls match ${pfx})
	if [ -n "$out" ]; then
		echo "Failed to flush routes for prefix used for tests."
		exit 1
	fi

	run_cmd "$IP -6 ro add ${pfx} ${nh}"
	if [ $? -ne 0 ]; then
		echo "Failed to add initial route for test."
		exit 1
	fi
}

# add initial route - used in replace route tests
add_initial_route6()
{
	add_route6 "2001:db8:104::/64" "$1"
}

check_route6()
{
	local pfx
	local expected="$1"
	local out
	local rc=0

	set -- $expected
	pfx=$1

	out=$($IP -6 ro ls match ${pfx} | sed -e 's/ pref medium//')
	check_expected "${out}" "${expected}"
}

route_cleanup()
{
	$IP li del red 2>/dev/null
	$IP li del dummy1 2>/dev/null
	$IP li del veth1 2>/dev/null
	$IP li del veth3 2>/dev/null

	cleanup &> /dev/null
}

route_setup()
{
	route_cleanup
	setup

	[ "${VERBOSE}" = "1" ] && set -x
	set -e

	ip netns add ns2
	ip netns set ns2 auto
	ip -netns ns2 link set dev lo up
	ip netns exec ns2 sysctl -qw net.ipv4.ip_forward=1
	ip netns exec ns2 sysctl -qw net.ipv6.conf.all.forwarding=1

	$IP li add veth1 type veth peer name veth2
	$IP li add veth3 type veth peer name veth4

	$IP li set veth1 up
	$IP li set veth3 up
	$IP li set veth2 netns ns2 up
	$IP li set veth4 netns ns2 up
	ip -netns ns2 li add dummy1 type dummy
	ip -netns ns2 li set dummy1 up

	$IP -6 addr add 2001:db8:101::1/64 dev veth1 nodad
	$IP -6 addr add 2001:db8:103::1/64 dev veth3 nodad
	$IP addr add 172.16.101.1/24 dev veth1
	$IP addr add 172.16.103.1/24 dev veth3

	ip -netns ns2 -6 addr add 2001:db8:101::2/64 dev veth2 nodad
	ip -netns ns2 -6 addr add 2001:db8:103::2/64 dev veth4 nodad
	ip -netns ns2 -6 addr add 2001:db8:104::1/64 dev dummy1 nodad

	ip -netns ns2 addr add 172.16.101.2/24 dev veth2
	ip -netns ns2 addr add 172.16.103.2/24 dev veth4
	ip -netns ns2 addr add 172.16.104.1/24 dev dummy1

	set +e
}

# assumption is that basic add of a single path route works
# otherwise just adding an address on an interface is broken
ipv6_rt_add()
{
	local rc

	echo
	echo "IPv6 route add / append tests"

	# route add same prefix - fails with EEXISTS b/c ip adds NLM_F_EXCL
	add_route6 "2001:db8:104::/64" "via 2001:db8:101::2"
	run_cmd "$IP -6 ro add 2001:db8:104::/64 via 2001:db8:103::2"
	log_test $? 2 "Attempt to add duplicate route - gw"

	# route add same prefix - fails with EEXISTS b/c ip adds NLM_F_EXCL
	add_route6 "2001:db8:104::/64" "via 2001:db8:101::2"
	run_cmd "$IP -6 ro add 2001:db8:104::/64 dev veth3"
	log_test $? 2 "Attempt to add duplicate route - dev only"

	# route add same prefix - fails with EEXISTS b/c ip adds NLM_F_EXCL
	add_route6 "2001:db8:104::/64" "via 2001:db8:101::2"
	run_cmd "$IP -6 ro add unreachable 2001:db8:104::/64"
	log_test $? 2 "Attempt to add duplicate route - reject route"

	# route append with same prefix adds a new route
	# - iproute2 sets NLM_F_CREATE | NLM_F_APPEND
	add_route6 "2001:db8:104::/64" "via 2001:db8:101::2"
	run_cmd "$IP -6 ro append 2001:db8:104::/64 via 2001:db8:103::2"
	check_route6 "2001:db8:104::/64 metric 1024 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
	log_test $? 0 "Append nexthop to existing route - gw"

	# insert mpath directly
	add_route6 "2001:db8:104::/64" "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	check_route6  "2001:db8:104::/64 metric 1024 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
	log_test $? 0 "Add multipath route"

	add_route6 "2001:db8:104::/64" "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro add 2001:db8:104::/64 nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	log_test $? 2 "Attempt to add duplicate multipath route"

	# insert of a second route without append but different metric
	add_route6 "2001:db8:104::/64" "via 2001:db8:101::2"
	run_cmd "$IP -6 ro add 2001:db8:104::/64 via 2001:db8:103::2 metric 512"
	rc=$?
	if [ $rc -eq 0 ]; then
		run_cmd "$IP -6 ro add 2001:db8:104::/64 via 2001:db8:103::3 metric 256"
		rc=$?
	fi
	log_test $rc 0 "Route add with different metrics"

	run_cmd "$IP -6 ro del 2001:db8:104::/64 metric 512"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6 "2001:db8:104::/64 via 2001:db8:103::3 dev veth3 metric 256 2001:db8:104::/64 via 2001:db8:101::2 dev veth1 metric 1024"
		rc=$?
	fi
	log_test $rc 0 "Route delete with metric"
}

ipv6_rt_replace_single()
{
	# single path with single path
	#
	add_initial_route6 "via 2001:db8:101::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 via 2001:db8:103::2"
	check_route6 "2001:db8:104::/64 via 2001:db8:103::2 dev veth3 metric 1024"
	log_test $? 0 "Single path with single path"

	# single path with multipath
	#
	add_initial_route6 "nexthop via 2001:db8:101::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 nexthop via 2001:db8:101::3 nexthop via 2001:db8:103::2"
	check_route6 "2001:db8:104::/64 metric 1024 nexthop via 2001:db8:101::3 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
	log_test $? 0 "Single path with multipath"

	# single path with single path using MULTIPATH attribute
	#
	add_initial_route6 "via 2001:db8:101::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 nexthop via 2001:db8:103::2"
	check_route6 "2001:db8:104::/64 via 2001:db8:103::2 dev veth3 metric 1024"
	log_test $? 0 "Single path with single path via multipath attribute"

	# route replace fails - invalid nexthop
	add_initial_route6 "via 2001:db8:101::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 via 2001:db8:104::2"
	if [ $? -eq 0 ]; then
		# previous command is expected to fail so if it returns 0
		# that means the test failed.
		log_test 0 1 "Invalid nexthop"
	else
		check_route6 "2001:db8:104::/64 via 2001:db8:101::2 dev veth1 metric 1024"
		log_test $? 0 "Invalid nexthop"
	fi

	# replace non-existent route
	# - note use of change versus replace since ip adds NLM_F_CREATE
	#   for replace
	add_initial_route6 "via 2001:db8:101::2"
	run_cmd "$IP -6 ro change 2001:db8:105::/64 via 2001:db8:101::2"
	log_test $? 2 "Single path - replace of non-existent route"
}

ipv6_rt_replace_mpath()
{
	# multipath with multipath
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 nexthop via 2001:db8:101::3 nexthop via 2001:db8:103::3"
	check_route6  "2001:db8:104::/64 metric 1024 nexthop via 2001:db8:101::3 dev veth1 weight 1 nexthop via 2001:db8:103::3 dev veth3 weight 1"
	log_test $? 0 "Multipath with multipath"

	# multipath with single
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 via 2001:db8:101::3"
	check_route6  "2001:db8:104::/64 via 2001:db8:101::3 dev veth1 metric 1024"
	log_test $? 0 "Multipath with single path"

	# multipath with single
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 nexthop via 2001:db8:101::3"
	check_route6 "2001:db8:104::/64 via 2001:db8:101::3 dev veth1 metric 1024"
	log_test $? 0 "Multipath with single path via multipath attribute"

	# multipath with dev-only
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 dev veth1"
	check_route6 "2001:db8:104::/64 dev veth1 metric 1024"
	log_test $? 0 "Multipath with dev-only"

	# route replace fails - invalid nexthop 1
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 nexthop via 2001:db8:111::3 nexthop via 2001:db8:103::3"
	check_route6  "2001:db8:104::/64 metric 1024 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
	log_test $? 0 "Multipath - invalid first nexthop"

	# route replace fails - invalid nexthop 2
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro replace 2001:db8:104::/64 nexthop via 2001:db8:101::3 nexthop via 2001:db8:113::3"
	check_route6  "2001:db8:104::/64 metric 1024 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
	log_test $? 0 "Multipath - invalid second nexthop"

	# multipath non-existent route
	add_initial_route6 "nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	run_cmd "$IP -6 ro change 2001:db8:105::/64 nexthop via 2001:db8:101::3 nexthop via 2001:db8:103::3"
	log_test $? 2 "Multipath - replace of non-existent route"
}

ipv6_rt_replace()
{
	echo
	echo "IPv6 route replace tests"

	ipv6_rt_replace_single
	ipv6_rt_replace_mpath
}

ipv6_route_test()
{
	route_setup

	ipv6_rt_add
	ipv6_rt_replace

	route_cleanup
}

ip_addr_metric_check()
{
	ip addr help 2>&1 | grep -q metric
	if [ $? -ne 0 ]; then
		echo "iproute2 command does not support metric for addresses. Skipping test"
		return 1
	fi

	return 0
}

ipv6_addr_metric_test()
{
	local rc

	echo
	echo "IPv6 prefix route tests"

	ip_addr_metric_check || return 1

	setup

	set -e
	$IP li add dummy1 type dummy
	$IP li add dummy2 type dummy
	$IP li set dummy1 up
	$IP li set dummy2 up

	# default entry is metric 256
	run_cmd "$IP -6 addr add dev dummy1 2001:db8:104::1/64"
	run_cmd "$IP -6 addr add dev dummy2 2001:db8:104::2/64"
	set +e

	check_route6 "2001:db8:104::/64 dev dummy1 proto kernel metric 256 2001:db8:104::/64 dev dummy2 proto kernel metric 256"
	log_test $? 0 "Default metric"

	set -e
	run_cmd "$IP -6 addr flush dev dummy1"
	run_cmd "$IP -6 addr add dev dummy1 2001:db8:104::1/64 metric 257"
	set +e

	check_route6 "2001:db8:104::/64 dev dummy2 proto kernel metric 256 2001:db8:104::/64 dev dummy1 proto kernel metric 257"
	log_test $? 0 "User specified metric on first device"

	set -e
	run_cmd "$IP -6 addr flush dev dummy2"
	run_cmd "$IP -6 addr add dev dummy2 2001:db8:104::2/64 metric 258"
	set +e

	check_route6 "2001:db8:104::/64 dev dummy1 proto kernel metric 257 2001:db8:104::/64 dev dummy2 proto kernel metric 258"
	log_test $? 0 "User specified metric on second device"

	run_cmd "$IP -6 addr del dev dummy1 2001:db8:104::1/64 metric 257"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6 "2001:db8:104::/64 dev dummy2 proto kernel metric 258"
		rc=$?
	fi
	log_test $rc 0 "Delete of address on first device"

	run_cmd "$IP -6 addr change dev dummy2 2001:db8:104::2/64 metric 259"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6 "2001:db8:104::/64 dev dummy2 proto kernel metric 259"
		rc=$?
	fi
	log_test $rc 0 "Modify metric of address"

	# verify prefix route removed on down
	run_cmd "ip netns exec ns1 sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1"
	run_cmd "$IP li set dev dummy2 down"
	rc=$?
	if [ $rc -eq 0 ]; then
		out=$($IP -6 ro ls match 2001:db8:104::/64)
		check_expected "${out}" ""
		rc=$?
	fi
	log_test $rc 0 "Prefix route removed on link down"

	# verify prefix route re-inserted with assigned metric
	run_cmd "$IP li set dev dummy2 up"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6 "2001:db8:104::/64 dev dummy2 proto kernel metric 259"
		rc=$?
	fi
	log_test $rc 0 "Prefix route with metric on link up"

	# verify peer metric added correctly
	set -e
	run_cmd "$IP -6 addr flush dev dummy2"
	run_cmd "$IP -6 addr add dev dummy2 2001:db8:104::1 peer 2001:db8:104::2 metric 260"
	set +e

	check_route6 "2001:db8:104::1 dev dummy2 proto kernel metric 260"
	log_test $? 0 "Set metric with peer route on local side"
	check_route6 "2001:db8:104::2 dev dummy2 proto kernel metric 260"
	log_test $? 0 "Set metric with peer route on peer side"

	set -e
	run_cmd "$IP -6 addr change dev dummy2 2001:db8:104::1 peer 2001:db8:104::3 metric 261"
	set +e

	check_route6 "2001:db8:104::1 dev dummy2 proto kernel metric 261"
	log_test $? 0 "Modify metric and peer address on local side"
	check_route6 "2001:db8:104::3 dev dummy2 proto kernel metric 261"
	log_test $? 0 "Modify metric and peer address on peer side"

	$IP li del dummy1
	$IP li del dummy2
	cleanup
}

ipv6_route_metrics_test()
{
	local rc

	echo
	echo "IPv6 routes with metrics"

	route_setup

	#
	# single path with metrics
	#
	run_cmd "$IP -6 ro add 2001:db8:111::/64 via 2001:db8:101::2 mtu 1400"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6  "2001:db8:111::/64 via 2001:db8:101::2 dev veth1 metric 1024 mtu 1400"
		rc=$?
	fi
	log_test $rc 0 "Single path route with mtu metric"


	#
	# multipath via separate routes with metrics
	#
	run_cmd "$IP -6 ro add 2001:db8:112::/64 via 2001:db8:101::2 mtu 1400"
	run_cmd "$IP -6 ro append 2001:db8:112::/64 via 2001:db8:103::2"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6 "2001:db8:112::/64 metric 1024 mtu 1400 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
		rc=$?
	fi
	log_test $rc 0 "Multipath route via 2 single routes with mtu metric on first"

	# second route is coalesced to first to make a multipath route.
	# MTU of the second path is hidden from display!
	run_cmd "$IP -6 ro add 2001:db8:113::/64 via 2001:db8:101::2"
	run_cmd "$IP -6 ro append 2001:db8:113::/64 via 2001:db8:103::2 mtu 1400"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6 "2001:db8:113::/64 metric 1024 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
		rc=$?
	fi
	log_test $rc 0 "Multipath route via 2 single routes with mtu metric on 2nd"

	run_cmd "$IP -6 ro del 2001:db8:113::/64 via 2001:db8:101::2"
	if [ $? -eq 0 ]; then
		check_route6 "2001:db8:113::/64 via 2001:db8:103::2 dev veth3 metric 1024 mtu 1400"
		log_test $? 0 "    MTU of second leg"
	fi

	#
	# multipath with metrics
	#
	run_cmd "$IP -6 ro add 2001:db8:115::/64 mtu 1400 nexthop via 2001:db8:101::2 nexthop via 2001:db8:103::2"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route6  "2001:db8:115::/64 metric 1024 mtu 1400 nexthop via 2001:db8:101::2 dev veth1 weight 1 nexthop via 2001:db8:103::2 dev veth3 weight 1"
		rc=$?
	fi
	log_test $rc 0 "Multipath route with mtu metric"

	$IP -6 ro add 2001:db8:104::/64 via 2001:db8:101::2 mtu 1300
	run_cmd "ip netns exec ns1 ${ping6} -w1 -c1 -s 1500 2001:db8:104::1"
	log_test $? 0 "Using route with mtu metric"

	run_cmd "$IP -6 ro add 2001:db8:114::/64 via  2001:db8:101::2  congctl lock foo"
	log_test $? 2 "Invalid metric (fails metric_convert)"

	route_cleanup
}

# add route for a prefix, flushing any existing routes first
# expected to be the first step of a test
add_route()
{
	local pfx="$1"
	local nh="$2"
	local out

	if [ "$VERBOSE" = "1" ]; then
		echo
		echo "    ##################################################"
		echo
	fi

	run_cmd "$IP ro flush ${pfx}"
	[ $? -ne 0 ] && exit 1

	out=$($IP ro ls match ${pfx})
	if [ -n "$out" ]; then
		echo "Failed to flush routes for prefix used for tests."
		exit 1
	fi

	run_cmd "$IP ro add ${pfx} ${nh}"
	if [ $? -ne 0 ]; then
		echo "Failed to add initial route for test."
		exit 1
	fi
}

# add initial route - used in replace route tests
add_initial_route()
{
	add_route "172.16.104.0/24" "$1"
}

check_route()
{
	local pfx
	local expected="$1"
	local out

	set -- $expected
	pfx=$1
	[ "${pfx}" = "unreachable" ] && pfx=$2

	out=$($IP ro ls match ${pfx})
	check_expected "${out}" "${expected}"
}

# assumption is that basic add of a single path route works
# otherwise just adding an address on an interface is broken
ipv4_rt_add()
{
	local rc

	echo
	echo "IPv4 route add / append tests"

	# route add same prefix - fails with EEXISTS b/c ip adds NLM_F_EXCL
	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro add 172.16.104.0/24 via 172.16.103.2"
	log_test $? 2 "Attempt to add duplicate route - gw"

	# route add same prefix - fails with EEXISTS b/c ip adds NLM_F_EXCL
	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro add 172.16.104.0/24 dev veth3"
	log_test $? 2 "Attempt to add duplicate route - dev only"

	# route add same prefix - fails with EEXISTS b/c ip adds NLM_F_EXCL
	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro add unreachable 172.16.104.0/24"
	log_test $? 2 "Attempt to add duplicate route - reject route"

	# iproute2 prepend only sets NLM_F_CREATE
	# - adds a new route; does NOT convert existing route to ECMP
	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro prepend 172.16.104.0/24 via 172.16.103.2"
	check_route "172.16.104.0/24 via 172.16.103.2 dev veth3 172.16.104.0/24 via 172.16.101.2 dev veth1"
	log_test $? 0 "Add new nexthop for existing prefix"

	# route append with same prefix adds a new route
	# - iproute2 sets NLM_F_CREATE | NLM_F_APPEND
	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro append 172.16.104.0/24 via 172.16.103.2"
	check_route "172.16.104.0/24 via 172.16.101.2 dev veth1 172.16.104.0/24 via 172.16.103.2 dev veth3"
	log_test $? 0 "Append nexthop to existing route - gw"

	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro append 172.16.104.0/24 dev veth3"
	check_route "172.16.104.0/24 via 172.16.101.2 dev veth1 172.16.104.0/24 dev veth3 scope link"
	log_test $? 0 "Append nexthop to existing route - dev only"

	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro append unreachable 172.16.104.0/24"
	check_route "172.16.104.0/24 via 172.16.101.2 dev veth1 unreachable 172.16.104.0/24"
	log_test $? 0 "Append nexthop to existing route - reject route"

	run_cmd "$IP ro flush 172.16.104.0/24"
	run_cmd "$IP ro add unreachable 172.16.104.0/24"
	run_cmd "$IP ro append 172.16.104.0/24 via 172.16.103.2"
	check_route "unreachable 172.16.104.0/24 172.16.104.0/24 via 172.16.103.2 dev veth3"
	log_test $? 0 "Append nexthop to existing reject route - gw"

	run_cmd "$IP ro flush 172.16.104.0/24"
	run_cmd "$IP ro add unreachable 172.16.104.0/24"
	run_cmd "$IP ro append 172.16.104.0/24 dev veth3"
	check_route "unreachable 172.16.104.0/24 172.16.104.0/24 dev veth3 scope link"
	log_test $? 0 "Append nexthop to existing reject route - dev only"

	# insert mpath directly
	add_route "172.16.104.0/24" "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	check_route  "172.16.104.0/24 nexthop via 172.16.101.2 dev veth1 weight 1 nexthop via 172.16.103.2 dev veth3 weight 1"
	log_test $? 0 "add multipath route"

	add_route "172.16.104.0/24" "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro add 172.16.104.0/24 nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	log_test $? 2 "Attempt to add duplicate multipath route"

	# insert of a second route without append but different metric
	add_route "172.16.104.0/24" "via 172.16.101.2"
	run_cmd "$IP ro add 172.16.104.0/24 via 172.16.103.2 metric 512"
	rc=$?
	if [ $rc -eq 0 ]; then
		run_cmd "$IP ro add 172.16.104.0/24 via 172.16.103.3 metric 256"
		rc=$?
	fi
	log_test $rc 0 "Route add with different metrics"

	run_cmd "$IP ro del 172.16.104.0/24 metric 512"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 via 172.16.101.2 dev veth1 172.16.104.0/24 via 172.16.103.3 dev veth3 metric 256"
		rc=$?
	fi
	log_test $rc 0 "Route delete with metric"
}

ipv4_rt_replace_single()
{
	# single path with single path
	#
	add_initial_route "via 172.16.101.2"
	run_cmd "$IP ro replace 172.16.104.0/24 via 172.16.103.2"
	check_route "172.16.104.0/24 via 172.16.103.2 dev veth3"
	log_test $? 0 "Single path with single path"

	# single path with multipath
	#
	add_initial_route "nexthop via 172.16.101.2"
	run_cmd "$IP ro replace 172.16.104.0/24 nexthop via 172.16.101.3 nexthop via 172.16.103.2"
	check_route "172.16.104.0/24 nexthop via 172.16.101.3 dev veth1 weight 1 nexthop via 172.16.103.2 dev veth3 weight 1"
	log_test $? 0 "Single path with multipath"

	# single path with reject
	#
	add_initial_route "nexthop via 172.16.101.2"
	run_cmd "$IP ro replace unreachable 172.16.104.0/24"
	check_route "unreachable 172.16.104.0/24"
	log_test $? 0 "Single path with reject route"

	# single path with single path using MULTIPATH attribute
	#
	add_initial_route "via 172.16.101.2"
	run_cmd "$IP ro replace 172.16.104.0/24 nexthop via 172.16.103.2"
	check_route "172.16.104.0/24 via 172.16.103.2 dev veth3"
	log_test $? 0 "Single path with single path via multipath attribute"

	# route replace fails - invalid nexthop
	add_initial_route "via 172.16.101.2"
	run_cmd "$IP ro replace 172.16.104.0/24 via 2001:db8:104::2"
	if [ $? -eq 0 ]; then
		# previous command is expected to fail so if it returns 0
		# that means the test failed.
		log_test 0 1 "Invalid nexthop"
	else
		check_route "172.16.104.0/24 via 172.16.101.2 dev veth1"
		log_test $? 0 "Invalid nexthop"
	fi

	# replace non-existent route
	# - note use of change versus replace since ip adds NLM_F_CREATE
	#   for replace
	add_initial_route "via 172.16.101.2"
	run_cmd "$IP ro change 172.16.105.0/24 via 172.16.101.2"
	log_test $? 2 "Single path - replace of non-existent route"
}

ipv4_rt_replace_mpath()
{
	# multipath with multipath
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro replace 172.16.104.0/24 nexthop via 172.16.101.3 nexthop via 172.16.103.3"
	check_route  "172.16.104.0/24 nexthop via 172.16.101.3 dev veth1 weight 1 nexthop via 172.16.103.3 dev veth3 weight 1"
	log_test $? 0 "Multipath with multipath"

	# multipath with single
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro replace 172.16.104.0/24 via 172.16.101.3"
	check_route  "172.16.104.0/24 via 172.16.101.3 dev veth1"
	log_test $? 0 "Multipath with single path"

	# multipath with single
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro replace 172.16.104.0/24 nexthop via 172.16.101.3"
	check_route "172.16.104.0/24 via 172.16.101.3 dev veth1"
	log_test $? 0 "Multipath with single path via multipath attribute"

	# multipath with reject
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro replace unreachable 172.16.104.0/24"
	check_route "unreachable 172.16.104.0/24"
	log_test $? 0 "Multipath with reject route"

	# route replace fails - invalid nexthop 1
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro replace 172.16.104.0/24 nexthop via 172.16.111.3 nexthop via 172.16.103.3"
	check_route  "172.16.104.0/24 nexthop via 172.16.101.2 dev veth1 weight 1 nexthop via 172.16.103.2 dev veth3 weight 1"
	log_test $? 0 "Multipath - invalid first nexthop"

	# route replace fails - invalid nexthop 2
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro replace 172.16.104.0/24 nexthop via 172.16.101.3 nexthop via 172.16.113.3"
	check_route  "172.16.104.0/24 nexthop via 172.16.101.2 dev veth1 weight 1 nexthop via 172.16.103.2 dev veth3 weight 1"
	log_test $? 0 "Multipath - invalid second nexthop"

	# multipath non-existent route
	add_initial_route "nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	run_cmd "$IP ro change 172.16.105.0/24 nexthop via 172.16.101.3 nexthop via 172.16.103.3"
	log_test $? 2 "Multipath - replace of non-existent route"
}

ipv4_rt_replace()
{
	echo
	echo "IPv4 route replace tests"

	ipv4_rt_replace_single
	ipv4_rt_replace_mpath
}

# checks that cached input route on VRF port is deleted
# when VRF is deleted
ipv4_local_rt_cache()
{
	run_cmd "ip addr add 10.0.0.1/32 dev lo"
	run_cmd "ip netns add test-ns"
	run_cmd "ip link add veth-outside type veth peer name veth-inside"
	run_cmd "ip link add vrf-100 type vrf table 1100"
	run_cmd "ip link set veth-outside master vrf-100"
	run_cmd "ip link set veth-inside netns test-ns"
	run_cmd "ip link set veth-outside up"
	run_cmd "ip link set vrf-100 up"
	run_cmd "ip route add 10.1.1.1/32 dev veth-outside table 1100"
	run_cmd "ip netns exec test-ns ip link set veth-inside up"
	run_cmd "ip netns exec test-ns ip addr add 10.1.1.1/32 dev veth-inside"
	run_cmd "ip netns exec test-ns ip route add 10.0.0.1/32 dev veth-inside"
	run_cmd "ip netns exec test-ns ip route add default via 10.0.0.1"
	run_cmd "ip netns exec test-ns ping 10.0.0.1 -c 1 -i 1"
	run_cmd "ip link delete vrf-100"

	# if we do not hang test is a success
	log_test $? 0 "Cached route removed from VRF port device"
}

ipv4_route_test()
{
	route_setup

	ipv4_rt_add
	ipv4_rt_replace
	ipv4_local_rt_cache

	route_cleanup
}

ipv4_addr_metric_test()
{
	local rc

	echo
	echo "IPv4 prefix route tests"

	ip_addr_metric_check || return 1

	setup

	set -e
	$IP li add dummy1 type dummy
	$IP li add dummy2 type dummy
	$IP li set dummy1 up
	$IP li set dummy2 up

	# default entry is metric 256
	run_cmd "$IP addr add dev dummy1 172.16.104.1/24"
	run_cmd "$IP addr add dev dummy2 172.16.104.2/24"
	set +e

	check_route "172.16.104.0/24 dev dummy1 proto kernel scope link src 172.16.104.1 172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.2"
	log_test $? 0 "Default metric"

	set -e
	run_cmd "$IP addr flush dev dummy1"
	run_cmd "$IP addr add dev dummy1 172.16.104.1/24 metric 257"
	set +e

	check_route "172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.2 172.16.104.0/24 dev dummy1 proto kernel scope link src 172.16.104.1 metric 257"
	log_test $? 0 "User specified metric on first device"

	set -e
	run_cmd "$IP addr flush dev dummy2"
	run_cmd "$IP addr add dev dummy2 172.16.104.2/24 metric 258"
	set +e

	check_route "172.16.104.0/24 dev dummy1 proto kernel scope link src 172.16.104.1 metric 257 172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.2 metric 258"
	log_test $? 0 "User specified metric on second device"

	run_cmd "$IP addr del dev dummy1 172.16.104.1/24 metric 257"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.2 metric 258"
		rc=$?
	fi
	log_test $rc 0 "Delete of address on first device"

	run_cmd "$IP addr change dev dummy2 172.16.104.2/24 metric 259"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.2 metric 259"
		rc=$?
	fi
	log_test $rc 0 "Modify metric of address"

	# verify prefix route removed on down
	run_cmd "$IP li set dev dummy2 down"
	rc=$?
	if [ $rc -eq 0 ]; then
		out=$($IP ro ls match 172.16.104.0/24)
		check_expected "${out}" ""
		rc=$?
	fi
	log_test $rc 0 "Prefix route removed on link down"

	# verify prefix route re-inserted with assigned metric
	run_cmd "$IP li set dev dummy2 up"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.2 metric 259"
		rc=$?
	fi
	log_test $rc 0 "Prefix route with metric on link up"

	# explicitly check for metric changes on edge scenarios
	run_cmd "$IP addr flush dev dummy2"
	run_cmd "$IP addr add dev dummy2 172.16.104.0/24 metric 259"
	run_cmd "$IP addr change dev dummy2 172.16.104.0/24 metric 260"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 dev dummy2 proto kernel scope link src 172.16.104.0 metric 260"
		rc=$?
	fi
	log_test $rc 0 "Modify metric of .0/24 address"

	run_cmd "$IP addr flush dev dummy2"
	run_cmd "$IP addr add dev dummy2 172.16.104.1/32 peer 172.16.104.2 metric 260"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.2 dev dummy2 proto kernel scope link src 172.16.104.1 metric 260"
		rc=$?
	fi
	log_test $rc 0 "Set metric of address with peer route"

	run_cmd "$IP addr change dev dummy2 172.16.104.1/32 peer 172.16.104.3 metric 261"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.3 dev dummy2 proto kernel scope link src 172.16.104.1 metric 261"
		rc=$?
	fi
	log_test $rc 0 "Modify metric and peer address for peer route"

	$IP li del dummy1
	$IP li del dummy2
	cleanup
}

ipv4_route_metrics_test()
{
	local rc

	echo
	echo "IPv4 route add / append tests"

	route_setup

	run_cmd "$IP ro add 172.16.111.0/24 via 172.16.101.2 mtu 1400"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.111.0/24 via 172.16.101.2 dev veth1 mtu 1400"
		rc=$?
	fi
	log_test $rc 0 "Single path route with mtu metric"


	run_cmd "$IP ro add 172.16.112.0/24 mtu 1400 nexthop via 172.16.101.2 nexthop via 172.16.103.2"
	rc=$?
	if [ $rc -eq 0 ]; then
		check_route "172.16.112.0/24 mtu 1400 nexthop via 172.16.101.2 dev veth1 weight 1 nexthop via 172.16.103.2 dev veth3 weight 1"
		rc=$?
	fi
	log_test $rc 0 "Multipath route with mtu metric"

	$IP ro add 172.16.104.0/24 via 172.16.101.2 mtu 1300
	run_cmd "ip netns exec ns1 ping -w1 -c1 -s 1500 172.16.104.1"
	log_test $? 0 "Using route with mtu metric"

	run_cmd "$IP ro add 172.16.111.0/24 via 172.16.101.2 congctl lock foo"
	log_test $? 2 "Invalid metric (fails metric_convert)"

	route_cleanup
}

ipv4_del_addr_test()
{
	echo
	echo "IPv4 delete address route tests"

	setup

	set -e
	$IP li add dummy1 type dummy
	$IP li set dummy1 up
	$IP li add dummy2 type dummy
	$IP li set dummy2 up
	$IP li add red type vrf table 1111
	$IP li set red up
	$IP ro add vrf red unreachable default
	$IP li set dummy2 vrf red

	$IP addr add dev dummy1 172.16.104.1/24
	$IP addr add dev dummy1 172.16.104.11/24
	$IP addr add dev dummy2 172.16.104.1/24
	$IP addr add dev dummy2 172.16.104.11/24
	$IP route add 172.16.105.0/24 via 172.16.104.2 src 172.16.104.11
	$IP route add vrf red 172.16.105.0/24 via 172.16.104.2 src 172.16.104.11
	set +e

	# removing address from device in vrf should only remove route from vrf table
	$IP addr del dev dummy2 172.16.104.11/24
	$IP ro ls vrf red | grep -q 172.16.105.0/24
	log_test $? 1 "Route removed from VRF when source address deleted"

	$IP ro ls | grep -q 172.16.105.0/24
	log_test $? 0 "Route in default VRF not removed"

	$IP addr add dev dummy2 172.16.104.11/24
	$IP route add vrf red 172.16.105.0/24 via 172.16.104.2 src 172.16.104.11

	$IP addr del dev dummy1 172.16.104.11/24
	$IP ro ls | grep -q 172.16.105.0/24
	log_test $? 1 "Route removed in default VRF when source address deleted"

	$IP ro ls vrf red | grep -q 172.16.105.0/24
	log_test $? 0 "Route in VRF is not removed by address delete"

	$IP li del dummy1
	$IP li del dummy2
	cleanup
}


ipv4_route_v6_gw_test()
{
	local rc

	echo
	echo "IPv4 route with IPv6 gateway tests"

	route_setup
	sleep 2

	#
	# single path route
	#
	run_cmd "$IP ro add 172.16.104.0/24 via inet6 2001:db8:101::2"
	rc=$?
	log_test $rc 0 "Single path route with IPv6 gateway"
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 via inet6 2001:db8:101::2 dev veth1"
	fi

	run_cmd "ip netns exec ns1 ping -w1 -c1 172.16.104.1"
	log_test $rc 0 "Single path route with IPv6 gateway - ping"

	run_cmd "$IP ro del 172.16.104.0/24 via inet6 2001:db8:101::2"
	rc=$?
	log_test $rc 0 "Single path route delete"
	if [ $rc -eq 0 ]; then
		check_route "172.16.112.0/24"
	fi

	#
	# multipath - v6 then v4
	#
	run_cmd "$IP ro add 172.16.104.0/24 nexthop via inet6 2001:db8:101::2 dev veth1 nexthop via 172.16.103.2 dev veth3"
	rc=$?
	log_test $rc 0 "Multipath route add - v6 nexthop then v4"
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 nexthop via inet6 2001:db8:101::2 dev veth1 weight 1 nexthop via 172.16.103.2 dev veth3 weight 1"
	fi

	run_cmd "$IP ro del 172.16.104.0/24 nexthop via 172.16.103.2 dev veth3 nexthop via inet6 2001:db8:101::2 dev veth1"
	log_test $? 2 "    Multipath route delete - nexthops in wrong order"

	run_cmd "$IP ro del 172.16.104.0/24 nexthop via inet6 2001:db8:101::2 dev veth1 nexthop via 172.16.103.2 dev veth3"
	log_test $? 0 "    Multipath route delete exact match"

	#
	# multipath - v4 then v6
	#
	run_cmd "$IP ro add 172.16.104.0/24 nexthop via 172.16.103.2 dev veth3 nexthop via inet6 2001:db8:101::2 dev veth1"
	rc=$?
	log_test $rc 0 "Multipath route add - v4 nexthop then v6"
	if [ $rc -eq 0 ]; then
		check_route "172.16.104.0/24 nexthop via 172.16.103.2 dev veth3 weight 1 nexthop via inet6 2001:db8:101::2 dev veth1 weight 1"
	fi

	run_cmd "$IP ro del 172.16.104.0/24 nexthop via inet6 2001:db8:101::2 dev veth1 nexthop via 172.16.103.2 dev veth3"
	log_test $? 2 "    Multipath route delete - nexthops in wrong order"

	run_cmd "$IP ro del 172.16.104.0/24 nexthop via 172.16.103.2 dev veth3 nexthop via inet6 2001:db8:101::2 dev veth1"
	log_test $? 0 "    Multipath route delete exact match"

	route_cleanup
}

################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $TESTS)
        -p          Pause on fail
        -P          Pause after each test before cleanup
        -v          verbose mode (show commands and output)
EOF
}

################################################################################
# main

while getopts :t:pPhv o
do
	case $o in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

PEER_CMD="ip netns exec ${PEER_NS}"

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

ip route help 2>&1 | grep -q fibmatch
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing fibmatch"
	exit $ksft_skip
fi

# start clean
cleanup &> /dev/null

for t in $TESTS
do
	case $t in
	fib_unreg_test|unregister)	fib_unreg_test;;
	fib_down_test|down)		fib_down_test;;
	fib_carrier_test|carrier)	fib_carrier_test;;
	fib_rp_filter_test|rp_filter)	fib_rp_filter_test;;
	fib_nexthop_test|nexthop)	fib_nexthop_test;;
	fib_suppress_test|suppress)	fib_suppress_test;;
	ipv6_route_test|ipv6_rt)	ipv6_route_test;;
	ipv4_route_test|ipv4_rt)	ipv4_route_test;;
	ipv6_addr_metric)		ipv6_addr_metric_test;;
	ipv4_addr_metric)		ipv4_addr_metric_test;;
	ipv4_del_addr)			ipv4_del_addr_test;;
	ipv6_route_metrics)		ipv6_route_metrics_test;;
	ipv4_route_metrics)		ipv4_route_metrics_test;;
	ipv4_route_v6_gw)		ipv4_route_v6_gw_test;;

	help) echo "Test names: $TESTS"; exit 0;;
	esac
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
