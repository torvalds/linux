#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking IPv4 and IPv6 FIB behavior in response to
# different events.

ret=0

PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}
IP="ip -netns testns"

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "        %-60s  [ OK ]\n" "${msg}"
	else
		ret=1
		printf "        %-60s  [FAIL]\n" "${msg}"
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

fib_test()
{
	fib_unreg_test
	fib_down_test
	fib_carrier_test
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
