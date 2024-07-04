#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking IPv4 and IPv6 FIB rules API

source lib.sh
ret=0
PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}

RTABLE=100
RTABLE_PEER=101
RTABLE_VRF=102
GW_IP4=192.51.100.2
SRC_IP=192.51.100.3
GW_IP6=2001:db8:1::2
SRC_IP6=2001:db8:1::3

DEV_ADDR=192.51.100.1
DEV_ADDR6=2001:db8:1::1
DEV=dummy0
TESTS="
	fib_rule6
	fib_rule4
	fib_rule6_connect
	fib_rule4_connect
	fib_rule6_vrf
	fib_rule4_vrf
"

SELFTEST_PATH=""

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	$IP rule show | grep -q l3mdev
	if [ $? -eq 0 ]; then
		msg="$msg (VRF)"
	fi

	if [ ${rc} -eq ${expected} ]; then
		nsuccess=$((nsuccess+1))
		printf "\n    TEST: %-60s  [ OK ]\n" "${msg}"
	else
		ret=1
		nfail=$((nfail+1))
		printf "\n    TEST: %-60s  [FAIL]\n" "${msg}"
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

check_nettest()
{
	if which nettest > /dev/null 2>&1; then
		return 0
	fi

	# Add the selftest directory to PATH if not already done
	if [ "${SELFTEST_PATH}" = "" ]; then
		SELFTEST_PATH="$(dirname $0)"
		PATH="${PATH}:${SELFTEST_PATH}"

		# Now retry with the new path
		if which nettest > /dev/null 2>&1; then
			return 0
		fi

		if [ "${ret}" -eq 0 ]; then
			ret="${ksft_skip}"
		fi
		echo "nettest not found (try 'make -C ${SELFTEST_PATH} nettest')"
	fi

	return 1
}

setup()
{
	set -e
	setup_ns testns
	IP="ip -netns $testns"

	$IP link add dummy0 type dummy
	$IP link set dev dummy0 up
	$IP address add $DEV_ADDR/24 dev dummy0
	$IP -6 address add $DEV_ADDR6/64 dev dummy0

	set +e
}

cleanup()
{
	$IP link del dev dummy0 &> /dev/null
	cleanup_ns $testns
}

setup_peer()
{
	set -e

	setup_ns peerns
	IP_PEER="ip -netns $peerns"
	$IP_PEER link set dev lo up

	ip link add name veth0 netns $testns type veth \
		peer name veth1 netns $peerns
	$IP link set dev veth0 up
	$IP_PEER link set dev veth1 up

	$IP address add 192.0.2.10 peer 192.0.2.11/32 dev veth0
	$IP_PEER address add 192.0.2.11 peer 192.0.2.10/32 dev veth1

	$IP address add 2001:db8::10 peer 2001:db8::11/128 dev veth0 nodad
	$IP_PEER address add 2001:db8::11 peer 2001:db8::10/128 dev veth1 nodad

	$IP_PEER address add 198.51.100.11/32 dev lo
	$IP route add table $RTABLE_PEER 198.51.100.11/32 via 192.0.2.11

	$IP_PEER address add 2001:db8::1:11/128 dev lo
	$IP route add table $RTABLE_PEER 2001:db8::1:11/128 via 2001:db8::11

	set +e
}

cleanup_peer()
{
	$IP link del dev veth0
	ip netns del $peerns
}

setup_vrf()
{
	$IP link add name vrf0 up type vrf table $RTABLE_VRF
	$IP link set dev $DEV master vrf0
}

cleanup_vrf()
{
	$IP link del dev vrf0
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
	pref=$($IP -6 rule show $1 table $RTABLE | cut -d ":" -f 1)
	$IP -6 rule del pref $pref
}

fib_rule6_test_match_n_redirect()
{
	local match="$1"
	local getmatch="$2"
	local description="$3"

	$IP -6 rule add $match table $RTABLE
	$IP -6 route get $GW_IP6 $getmatch | grep -q "table $RTABLE"
	log_test $? 0 "rule6 check: $description"

	fib_rule6_del_by_pref "$match"
	log_test $? 0 "rule6 del by pref: $description"
}

fib_rule6_test_reject()
{
	local match="$1"
	local rc

	$IP -6 rule add $match table $RTABLE 2>/dev/null
	rc=$?
	log_test $rc 2 "rule6 check: $match"

	if [ $rc -eq 0 ]; then
		$IP -6 rule del $match table $RTABLE
	fi
}

fib_rule6_test()
{
	local getmatch
	local match
	local cnt

	# setup the fib rule redirect route
	$IP -6 route add table $RTABLE default via $GW_IP6 dev $DEV onlink

	match="oif $DEV"
	fib_rule6_test_match_n_redirect "$match" "$match" "oif redirect to table"

	match="from $SRC_IP6 iif $DEV"
	fib_rule6_test_match_n_redirect "$match" "$match" "iif redirect to table"

	# Reject dsfield (tos) options which have ECN bits set
	for cnt in $(seq 1 3); do
		match="dsfield $cnt"
		fib_rule6_test_reject "$match"
	done

	# Don't take ECN bits into account when matching on dsfield
	match="tos 0x10"
	for cnt in "0x10" "0x11" "0x12" "0x13"; do
		# Using option 'tos' instead of 'dsfield' as old iproute2
		# versions don't support 'dsfield' in ip rule show.
		getmatch="tos $cnt"
		fib_rule6_test_match_n_redirect "$match" "$getmatch" \
						"$getmatch redirect to table"
	done

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
		match="ipproto ipv6-icmp"
		fib_rule6_test_match_n_redirect "$match" "$match" "ipproto ipv6-icmp match"
	fi
}

fib_rule6_vrf_test()
{
	setup_vrf
	fib_rule6_test
	cleanup_vrf
}

# Verify that the IPV6_TCLASS option of UDPv6 and TCPv6 sockets is properly
# taken into account when connecting the socket and when sending packets.
fib_rule6_connect_test()
{
	local dsfield

	if ! check_nettest; then
		echo "SKIP: Could not run test without nettest tool"
		return
	fi

	setup_peer
	$IP -6 rule add dsfield 0x04 table $RTABLE_PEER

	# Combine the base DS Field value (0x04) with all possible ECN values
	# (Not-ECT: 0, ECT(1): 1, ECT(0): 2, CE: 3).
	# The ECN bits shouldn't influence the result of the test.
	for dsfield in 0x04 0x05 0x06 0x07; do
		nettest -q -6 -B -t 5 -N $testns -O $peerns -U -D \
			-Q "${dsfield}" -l 2001:db8::1:11 -r 2001:db8::1:11
		log_test $? 0 "rule6 dsfield udp connect (dsfield ${dsfield})"

		nettest -q -6 -B -t 5 -N $testns -O $peerns -Q "${dsfield}" \
			-l 2001:db8::1:11 -r 2001:db8::1:11
		log_test $? 0 "rule6 dsfield tcp connect (dsfield ${dsfield})"
	done

	$IP -6 rule del dsfield 0x04 table $RTABLE_PEER
	cleanup_peer
}

fib_rule4_del()
{
	$IP rule del $1
	log_test $? 0 "del $1"
}

fib_rule4_del_by_pref()
{
	pref=$($IP rule show $1 table $RTABLE | cut -d ":" -f 1)
	$IP rule del pref $pref
}

fib_rule4_test_match_n_redirect()
{
	local match="$1"
	local getmatch="$2"
	local description="$3"

	$IP rule add $match table $RTABLE
	$IP route get $GW_IP4 $getmatch | grep -q "table $RTABLE"
	log_test $? 0 "rule4 check: $description"

	fib_rule4_del_by_pref "$match"
	log_test $? 0 "rule4 del by pref: $description"
}

fib_rule4_test_reject()
{
	local match="$1"
	local rc

	$IP rule add $match table $RTABLE 2>/dev/null
	rc=$?
	log_test $rc 2 "rule4 check: $match"

	if [ $rc -eq 0 ]; then
		$IP rule del $match table $RTABLE
	fi
}

fib_rule4_test()
{
	local getmatch
	local match
	local cnt

	# setup the fib rule redirect route
	$IP route add table $RTABLE default via $GW_IP4 dev $DEV onlink

	match="oif $DEV"
	fib_rule4_test_match_n_redirect "$match" "$match" "oif redirect to table"

	# need enable forwarding and disable rp_filter temporarily as all the
	# addresses are in the same subnet and egress device == ingress device.
	ip netns exec $testns sysctl -qw net.ipv4.ip_forward=1
	ip netns exec $testns sysctl -qw net.ipv4.conf.$DEV.rp_filter=0
	match="from $SRC_IP iif $DEV"
	fib_rule4_test_match_n_redirect "$match" "$match" "iif redirect to table"
	ip netns exec $testns sysctl -qw net.ipv4.ip_forward=0

	# Reject dsfield (tos) options which have ECN bits set
	for cnt in $(seq 1 3); do
		match="dsfield $cnt"
		fib_rule4_test_reject "$match"
	done

	# Don't take ECN bits into account when matching on dsfield
	match="tos 0x10"
	for cnt in "0x10" "0x11" "0x12" "0x13"; do
		# Using option 'tos' instead of 'dsfield' as old iproute2
		# versions don't support 'dsfield' in ip rule show.
		getmatch="tos $cnt"
		fib_rule4_test_match_n_redirect "$match" "$getmatch" \
						"$getmatch redirect to table"
	done

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

fib_rule4_vrf_test()
{
	setup_vrf
	fib_rule4_test
	cleanup_vrf
}

# Verify that the IP_TOS option of UDPv4 and TCPv4 sockets is properly taken
# into account when connecting the socket and when sending packets.
fib_rule4_connect_test()
{
	local dsfield

	if ! check_nettest; then
		echo "SKIP: Could not run test without nettest tool"
		return
	fi

	setup_peer
	$IP -4 rule add dsfield 0x04 table $RTABLE_PEER

	# Combine the base DS Field value (0x04) with all possible ECN values
	# (Not-ECT: 0, ECT(1): 1, ECT(0): 2, CE: 3).
	# The ECN bits shouldn't influence the result of the test.
	for dsfield in 0x04 0x05 0x06 0x07; do
		nettest -q -B -t 5 -N $testns -O $peerns -D -U -Q "${dsfield}" \
			-l 198.51.100.11 -r 198.51.100.11
		log_test $? 0 "rule4 dsfield udp connect (dsfield ${dsfield})"

		nettest -q -B -t 5 -N $testns -O $peerns -Q "${dsfield}" \
			-l 198.51.100.11 -r 198.51.100.11
		log_test $? 0 "rule4 dsfield tcp connect (dsfield ${dsfield})"
	done

	$IP -4 rule del dsfield 0x04 table $RTABLE_PEER
	cleanup_peer
}

run_fibrule_tests()
{
	log_section "IPv4 fib rule"
	fib_rule4_test
	log_section "IPv6 fib rule"
	fib_rule6_test
}
################################################################################
# usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $TESTS)
EOF
}

################################################################################
# main

while getopts ":t:h" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

# start clean
cleanup &> /dev/null
setup
for t in $TESTS
do
	case $t in
	fib_rule6_test|fib_rule6)		fib_rule6_test;;
	fib_rule4_test|fib_rule4)		fib_rule4_test;;
	fib_rule6_connect_test|fib_rule6_connect)	fib_rule6_connect_test;;
	fib_rule4_connect_test|fib_rule4_connect)	fib_rule4_connect_test;;
	fib_rule6_vrf_test|fib_rule6_vrf)	fib_rule6_vrf_test;;
	fib_rule4_vrf_test|fib_rule4_vrf)	fib_rule4_vrf_test;;

	help) echo "Test names: $TESTS"; exit 0;;

	esac
done
cleanup

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
