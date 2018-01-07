#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking IPv4 and IPv6 FIB behavior in response to
# different events.

ret=0

check_err()
{
	if [ $ret -eq 0 ]; then
		ret=$1
	fi
}

check_fail()
{
	if [ $1 -eq 0 ]; then
		ret=1
	fi
}

netns_create()
{
	local testns=$1

	ip netns add $testns
	ip netns exec $testns ip link set dev lo up
}

fib_unreg_unicast_test()
{
	ret=0

	netns_create "testns"

	ip netns exec testns ip link add dummy0 type dummy
	ip netns exec testns ip link set dev dummy0 up

	ip netns exec testns ip address add 198.51.100.1/24 dev dummy0
	ip netns exec testns ip -6 address add 2001:db8:1::1/64 dev dummy0

	ip netns exec testns ip route get fibmatch 198.51.100.2 &> /dev/null
	check_err $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	check_err $?

	ip netns exec testns ip link del dev dummy0
	check_err $?

	ip netns exec testns ip route get fibmatch 198.51.100.2 &> /dev/null
	check_fail $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	check_fail $?

	ip netns del testns

	if [ $ret -ne 0 ]; then
		echo "FAIL: unicast route test"
		return 1
	fi
	echo "PASS: unicast route test"
}

fib_unreg_multipath_test()
{
	ret=0

	netns_create "testns"

	ip netns exec testns ip link add dummy0 type dummy
	ip netns exec testns ip link set dev dummy0 up

	ip netns exec testns ip link add dummy1 type dummy
	ip netns exec testns ip link set dev dummy1 up

	ip netns exec testns ip address add 198.51.100.1/24 dev dummy0
	ip netns exec testns ip -6 address add 2001:db8:1::1/64 dev dummy0

	ip netns exec testns ip address add 192.0.2.1/24 dev dummy1
	ip netns exec testns ip -6 address add 2001:db8:2::1/64 dev dummy1

	ip netns exec testns ip route add 203.0.113.0/24 \
		nexthop via 198.51.100.2 dev dummy0 \
		nexthop via 192.0.2.2 dev dummy1
	ip netns exec testns ip -6 route add 2001:db8:3::/64 \
		nexthop via 2001:db8:1::2 dev dummy0 \
		nexthop via 2001:db8:2::2 dev dummy1

	ip netns exec testns ip route get fibmatch 203.0.113.1 &> /dev/null
	check_err $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	check_err $?

	ip netns exec testns ip link del dev dummy0
	check_err $?

	ip netns exec testns ip route get fibmatch 203.0.113.1 &> /dev/null
	check_fail $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	# In IPv6 we do not flush the entire multipath route.
	check_err $?

	ip netns exec testns ip link del dev dummy1

	ip netns del testns

	if [ $ret -ne 0 ]; then
		echo "FAIL: multipath route test"
		return 1
	fi
	echo "PASS: multipath route test"
}

fib_unreg_test()
{
	echo "Running netdev unregister tests"

	fib_unreg_unicast_test
	fib_unreg_multipath_test
}

fib_down_unicast_test()
{
	ret=0

	netns_create "testns"

	ip netns exec testns ip link add dummy0 type dummy
	ip netns exec testns ip link set dev dummy0 up

	ip netns exec testns ip address add 198.51.100.1/24 dev dummy0
	ip netns exec testns ip -6 address add 2001:db8:1::1/64 dev dummy0

	ip netns exec testns ip route get fibmatch 198.51.100.2 &> /dev/null
	check_err $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	check_err $?

	ip netns exec testns ip link set dev dummy0 down
	check_err $?

	ip netns exec testns ip route get fibmatch 198.51.100.2 &> /dev/null
	check_fail $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:1::2 &> /dev/null
	check_fail $?

	ip netns exec testns ip link del dev dummy0

	ip netns del testns

	if [ $ret -ne 0 ]; then
		echo "FAIL: unicast route test"
		return 1
	fi
	echo "PASS: unicast route test"
}

fib_down_multipath_test_do()
{
	local down_dev=$1
	local up_dev=$2

	ip netns exec testns ip route get fibmatch 203.0.113.1 \
		oif $down_dev &> /dev/null
	check_fail $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 \
		oif $down_dev &> /dev/null
	check_fail $?

	ip netns exec testns ip route get fibmatch 203.0.113.1 \
		oif $up_dev &> /dev/null
	check_err $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 \
		oif $up_dev &> /dev/null
	check_err $?

	ip netns exec testns ip route get fibmatch 203.0.113.1 | \
		grep $down_dev | grep -q "dead linkdown"
	check_err $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 | \
		grep $down_dev | grep -q "dead linkdown"
	check_err $?

	ip netns exec testns ip route get fibmatch 203.0.113.1 | \
		grep $up_dev | grep -q "dead linkdown"
	check_fail $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 | \
		grep $up_dev | grep -q "dead linkdown"
	check_fail $?
}

fib_down_multipath_test()
{
	ret=0

	netns_create "testns"

	ip netns exec testns ip link add dummy0 type dummy
	ip netns exec testns ip link set dev dummy0 up

	ip netns exec testns ip link add dummy1 type dummy
	ip netns exec testns ip link set dev dummy1 up

	ip netns exec testns ip address add 198.51.100.1/24 dev dummy0
	ip netns exec testns ip -6 address add 2001:db8:1::1/64 dev dummy0

	ip netns exec testns ip address add 192.0.2.1/24 dev dummy1
	ip netns exec testns ip -6 address add 2001:db8:2::1/64 dev dummy1

	ip netns exec testns ip route add 203.0.113.0/24 \
		nexthop via 198.51.100.2 dev dummy0 \
		nexthop via 192.0.2.2 dev dummy1
	ip netns exec testns ip -6 route add 2001:db8:3::/64 \
		nexthop via 2001:db8:1::2 dev dummy0 \
		nexthop via 2001:db8:2::2 dev dummy1

	ip netns exec testns ip route get fibmatch 203.0.113.1 &> /dev/null
	check_err $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	check_err $?

	ip netns exec testns ip link set dev dummy0 down
	check_err $?

	fib_down_multipath_test_do "dummy0" "dummy1"

	ip netns exec testns ip link set dev dummy0 up
	check_err $?
	ip netns exec testns ip link set dev dummy1 down
	check_err $?

	fib_down_multipath_test_do "dummy1" "dummy0"

	ip netns exec testns ip link set dev dummy0 down
	check_err $?

	ip netns exec testns ip route get fibmatch 203.0.113.1 &> /dev/null
	check_fail $?
	ip netns exec testns ip -6 route get fibmatch 2001:db8:3::1 &> /dev/null
	check_fail $?

	ip netns exec testns ip link del dev dummy1
	ip netns exec testns ip link del dev dummy0

	ip netns del testns

	if [ $ret -ne 0 ]; then
		echo "FAIL: multipath route test"
		return 1
	fi
	echo "PASS: multipath route test"
}

fib_down_test()
{
	echo "Running netdev down tests"

	fib_down_unicast_test
	fib_down_multipath_test
}

fib_test()
{
	fib_unreg_test
	fib_down_test
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

fib_test

exit $ret
