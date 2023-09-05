#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking the [no]localbypass VXLAN device option. The test
# configures two VXLAN devices in the same network namespace and a tc filter on
# the loopback device that drops encapsulated packets. The test sends packets
# from the first VXLAN device and verifies that by default these packets are
# received by the second VXLAN device. The test then enables the nolocalbypass
# option and verifies that packets are no longer received by the second VXLAN
# device.

ret=0
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

TESTS="
	nolocalbypass
"
VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no

################################################################################
# Utilities

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

tc_check_packets()
{
	local ns=$1; shift
	local id=$1; shift
	local handle=$1; shift
	local count=$1; shift
	local pkts

	sleep 0.1
	pkts=$(tc -n $ns -j -s filter show $id \
		| jq ".[] | select(.options.handle == $handle) | \
		.options.actions[0].stats.packets")
	[[ $pkts == $count ]]
}

################################################################################
# Setup

setup()
{
	ip netns add ns1

	ip -n ns1 link set dev lo up
	ip -n ns1 address add 192.0.2.1/32 dev lo
	ip -n ns1 address add 198.51.100.1/32 dev lo

	ip -n ns1 link add name vx0 up type vxlan id 100 local 198.51.100.1 \
		dstport 4789 nolearning
	ip -n ns1 link add name vx1 up type vxlan id 100 dstport 4790
}

cleanup()
{
	ip netns del ns1 &> /dev/null
}

################################################################################
# Tests

nolocalbypass()
{
	local smac=00:01:02:03:04:05
	local dmac=00:0a:0b:0c:0d:0e

	run_cmd "bridge -n ns1 fdb add $dmac dev vx0 self static dst 192.0.2.1 port 4790"

	run_cmd "tc -n ns1 qdisc add dev vx1 clsact"
	run_cmd "tc -n ns1 filter add dev vx1 ingress pref 1 handle 101 proto all flower src_mac $smac dst_mac $dmac action pass"

	run_cmd "tc -n ns1 qdisc add dev lo clsact"
	run_cmd "tc -n ns1 filter add dev lo ingress pref 1 handle 101 proto ip flower ip_proto udp dst_port 4790 action drop"

	run_cmd "ip -n ns1 -d -j link show dev vx0 | jq -e '.[][\"linkinfo\"][\"info_data\"][\"localbypass\"] == true'"
	log_test $? 0 "localbypass enabled"

	run_cmd "ip netns exec ns1 mausezahn vx0 -a $smac -b $dmac -c 1 -p 100 -q"

	tc_check_packets "ns1" "dev vx1 ingress" 101 1
	log_test $? 0 "Packet received by local VXLAN device - localbypass"

	run_cmd "ip -n ns1 link set dev vx0 type vxlan nolocalbypass"

	run_cmd "ip -n ns1 -d -j link show dev vx0 | jq -e '.[][\"linkinfo\"][\"info_data\"][\"localbypass\"] == false'"
	log_test $? 0 "localbypass disabled"

	run_cmd "ip netns exec ns1 mausezahn vx0 -a $smac -b $dmac -c 1 -p 100 -q"

	tc_check_packets "ns1" "dev vx1 ingress" 101 1
	log_test $? 0 "Packet not received by local VXLAN device - nolocalbypass"

	run_cmd "ip -n ns1 link set dev vx0 type vxlan localbypass"

	run_cmd "ip -n ns1 -d -j link show dev vx0 | jq -e '.[][\"linkinfo\"][\"info_data\"][\"localbypass\"] == true'"
	log_test $? 0 "localbypass enabled"

	run_cmd "ip netns exec ns1 mausezahn vx0 -a $smac -b $dmac -c 1 -p 100 -q"

	tc_check_packets "ns1" "dev vx1 ingress" 101 2
	log_test $? 0 "Packet received by local VXLAN device - localbypass"
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
        -P          Pause after each test before cleanup
        -v          Verbose mode (show commands and output)
EOF
}

################################################################################
# Main

trap cleanup EXIT

while getopts ":t:pPvh" opt; do
	case $opt in
		t) TESTS=$OPTARG ;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# Make sure we don't pause twice.
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v bridge)" ]; then
	echo "SKIP: Could not run test without bridge tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v mausezahn)" ]; then
	echo "SKIP: Could not run test without mausezahn tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v jq)" ]; then
	echo "SKIP: Could not run test without jq tool"
	exit $ksft_skip
fi

ip link help vxlan 2>&1 | grep -q "localbypass"
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 ip too old, missing VXLAN nolocalbypass support"
	exit $ksft_skip
fi

cleanup

for t in $TESTS
do
	setup; $t; cleanup;
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
