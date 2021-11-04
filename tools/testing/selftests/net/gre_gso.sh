#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking GRE GSO.

ret=0
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# all tests in this script. Can be overridden with -t option
TESTS="gre_gso"

VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no
IP="ip -netns ns1"
NS_EXEC="ip netns exec ns1"
TMPFILE=`mktemp`
PID=

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

	ip link add veth0 type veth peer name veth1
	ip link set veth0 up
	ip link set veth1 netns ns1
	$IP link set veth1 name veth0
	$IP link set veth0 up

	dd if=/dev/urandom of=$TMPFILE bs=1024 count=2048 &>/dev/null
	set +e
}

cleanup()
{
	rm -rf $TMPFILE
	[ -n "$PID" ] && kill $PID
	ip link del dev gre1 &> /dev/null
	ip link del dev veth0 &> /dev/null
	ip netns del ns1
}

get_linklocal()
{
	local dev=$1
	local ns=$2
	local addr

	[ -n "$ns" ] && ns="-netns $ns"

	addr=$(ip -6 -br $ns addr show dev ${dev} | \
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

gre_create_tun()
{
	local a1=$1
	local a2=$2
	local mode

	[[ $a1 =~ ^[0-9.]*$ ]] && mode=gre || mode=ip6gre

	ip tunnel add gre1 mode $mode local $a1 remote $a2 dev veth0
	ip link set gre1 up
	$IP tunnel add gre1 mode $mode local $a2 remote $a1 dev veth0
	$IP link set gre1 up
}

gre_gst_test_checks()
{
	local name=$1
	local addr=$2
	local proto=$3

	$NS_EXEC nc $proto -kl $port >/dev/null &
	PID=$!
	while ! $NS_EXEC ss -ltn | grep -q $port; do ((i++)); sleep 0.01; done

	cat $TMPFILE | timeout 1 nc $proto -N $addr $port
	log_test $? 0 "$name - copy file w/ TSO"

	ethtool -K veth0 tso off

	cat $TMPFILE | timeout 1 nc $proto -N $addr $port
	log_test $? 0 "$name - copy file w/ GSO"

	ethtool -K veth0 tso on

	kill $PID
	PID=
}

gre6_gso_test()
{
	local port=7777

	setup

	a1=$(get_linklocal veth0)
	a2=$(get_linklocal veth0 ns1)

	gre_create_tun $a1 $a2

	ip  addr add 172.16.2.1/24 dev gre1
	$IP addr add 172.16.2.2/24 dev gre1

	ip  -6 addr add 2001:db8:1::1/64 dev gre1 nodad
	$IP -6 addr add 2001:db8:1::2/64 dev gre1 nodad

	sleep 2

	gre_gst_test_checks GREv6/v4 172.16.2.2
	gre_gst_test_checks GREv6/v6 2001:db8:1::2 -6

	cleanup
}

gre_gso_test()
{
	gre6_gso_test
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

if [ ! -x "$(command -v nc)" ]; then
	echo "SKIP: Could not run test without nc tool"
	exit $ksft_skip
fi

# start clean
cleanup &> /dev/null

for t in $TESTS
do
	case $t in
	gre_gso)		gre_gso_test;;

	help) echo "Test names: $TESTS"; exit 0;;
	esac
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
