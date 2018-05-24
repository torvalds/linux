#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking IPv4 and IPv6 FIB behavior in response to
# different events.

ret=0

VERBOSE=${VERBOSE:=0}
PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}
IP="ip -netns testns"

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "    TEST: %-60s  [ OK ]\n" "${msg}"
	else
		ret=1
		printf "    TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
		echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

setup()
{
	set -e
	ip netns add testns
	$IP link set dev lo up

	$IP link add dummy0 type dummy
	$IP link set dev dummy0 up
	$IP address add 198.51.100.1/24 dev dummy0
	$IP -6 address add 2001:db8:1::1/64 dev dummy0
	set +e

}

cleanup()
{
	$IP link del dev dummy0 &> /dev/null
	ip netns del testns
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

################################################################################
#

fib_test()
{
	if [ -n "$TEST" ]; then
		eval $TEST
	else
		fib_unreg_test
		fib_down_test
		fib_carrier_test
		fib_nexthop_test
	fi
}

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit 0
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit 0
fi

ip route help 2>&1 | grep -q fibmatch
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing fibmatch"
	exit 0
fi

# start clean
cleanup &> /dev/null

fib_test

exit $ret
