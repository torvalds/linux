#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking IPv4 and IPv6 FIB rules API

ret=0

PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}
IP="ip -netns testns"

RTABLE=100
GW_IP4=192.51.100.2
SRC_IP=192.51.100.3
GW_IP6=2001:db8:1::2
SRC_IP6=2001:db8:1::3

DEV_ADDR=192.51.100.1
DEV=dummy0

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		nsuccess=$((nsuccess+1))
		printf "\n    TEST: %-50s  [ OK ]\n" "${msg}"
	else
		ret=1
		nfail=$((nfail+1))
		printf "\n    TEST: %-50s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi
}

log_section()
{
	echo
	echo "######################################################################"
	echo "TEST SECTION: $*"
	echo "######################################################################"
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

fib_check_iproute_support()
{
	ip rule help 2>&1 | grep -q $1
	if [ $? -ne 0 ]; then
		echo "SKIP: iproute2 iprule too old, missing $1 match"
		return 1
	fi

	ip route get help 2>&1 | grep -q $2
	if [ $? -ne 0 ]; then
		echo "SKIP: iproute2 get route too old, missing $2 match"
		return 1
	fi

	return 0
}

fib_rule6_del()
{
	$IP -6 rule del $1
	log_test $? 0 "rule6 del $1"
}

fib_rule6_del_by_pref()
{
	pref=$($IP -6 rule show | grep "$1 lookup $TABLE" | cut -d ":" -f 1)
	$IP -6 rule del pref $pref
}

fib_rule6_test_match_n_redirect()
{
	local match="$1"
	local getmatch="$2"

	$IP -6 rule add $match table $RTABLE
	$IP -6 route get $GW_IP6 $getmatch | grep -q "table $RTABLE"
	log_test $? 0 "rule6 check: $1"

	fib_rule6_del_by_pref "$match"
	log_test $? 0 "rule6 del by pref: $match"
}

fib_rule6_test()
{
	# setup the fib rule redirect route
	$IP -6 route add table $RTABLE default via $GW_IP6 dev $DEV onlink

	match="oif $DEV"
	fib_rule6_test_match_n_redirect "$match" "$match" "oif redirect to table"

	match="from $SRC_IP6 iif $DEV"
	fib_rule6_test_match_n_redirect "$match" "$match" "iif redirect to table"

	match="tos 0x10"
	fib_rule6_test_match_n_redirect "$match" "$match" "tos redirect to table"

	match="fwmark 0x64"
	getmatch="mark 0x64"
	fib_rule6_test_match_n_redirect "$match" "$getmatch" "fwmark redirect to table"

	fib_check_iproute_support "uidrange" "uid"
	if [ $? -eq 0 ]; then
		match="uidrange 100-100"
		getmatch="uid 100"
		fib_rule6_test_match_n_redirect "$match" "$getmatch" "uid redirect to table"
	fi

	fib_check_iproute_support "sport" "sport"
	if [ $? -eq 0 ]; then
		match="sport 666 dport 777"
		fib_rule6_test_match_n_redirect "$match" "$match" "sport and dport redirect to table"
	fi

	fib_check_iproute_support "ipproto" "ipproto"
	if [ $? -eq 0 ]; then
		match="ipproto tcp"
		fib_rule6_test_match_n_redirect "$match" "$match" "ipproto match"
	fi

	fib_check_iproute_support "ipproto" "ipproto"
	if [ $? -eq 0 ]; then
		match="ipproto icmp"
		fib_rule6_test_match_n_redirect "$match" "$match" "ipproto icmp match"
	fi
}

fib_rule4_del()
{
	$IP rule del $1
	log_test $? 0 "del $1"
}

fib_rule4_del_by_pref()
{
	pref=$($IP rule show | grep "$1 lookup $TABLE" | cut -d ":" -f 1)
	$IP rule del pref $pref
}

fib_rule4_test_match_n_redirect()
{
	local match="$1"
	local getmatch="$2"

	$IP rule add $match table $RTABLE
	$IP route get $GW_IP4 $getmatch | grep -q "table $RTABLE"
	log_test $? 0 "rule4 check: $1"

	fib_rule4_del_by_pref "$match"
	log_test $? 0 "rule4 del by pref: $match"
}

fib_rule4_test()
{
	# setup the fib rule redirect route
	$IP route add table $RTABLE default via $GW_IP4 dev $DEV onlink

	match="oif $DEV"
	fib_rule4_test_match_n_redirect "$match" "$match" "oif redirect to table"

	match="from $SRC_IP iif $DEV"
	fib_rule4_test_match_n_redirect "$match" "$match" "iif redirect to table"

	match="tos 0x10"
	fib_rule4_test_match_n_redirect "$match" "$match" "tos redirect to table"

	match="fwmark 0x64"
	getmatch="mark 0x64"
	fib_rule4_test_match_n_redirect "$match" "$getmatch" "fwmark redirect to table"

	fib_check_iproute_support "uidrange" "uid"
	if [ $? -eq 0 ]; then
		match="uidrange 100-100"
		getmatch="uid 100"
		fib_rule4_test_match_n_redirect "$match" "$getmatch" "uid redirect to table"
	fi

	fib_check_iproute_support "sport" "sport"
	if [ $? -eq 0 ]; then
		match="sport 666 dport 777"
		fib_rule4_test_match_n_redirect "$match" "$match" "sport and dport redirect to table"
	fi

	fib_check_iproute_support "ipproto" "ipproto"
	if [ $? -eq 0 ]; then
		match="ipproto tcp"
		fib_rule4_test_match_n_redirect "$match" "$match" "ipproto tcp match"
	fi

	fib_check_iproute_support "ipproto" "ipproto"
	if [ $? -eq 0 ]; then
		match="ipproto icmp"
		fib_rule4_test_match_n_redirect "$match" "$match" "ipproto icmp match"
	fi
}

run_fibrule_tests()
{
	log_section "IPv4 fib rule"
	fib_rule4_test
	log_section "IPv6 fib rule"
	fib_rule6_test
}

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit 0
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit 0
fi

# start clean
cleanup &> /dev/null
setup
run_fibrule_tests
cleanup

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
