#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking drop monitor functionality.
source lib.sh
ret=0

# all tests in this script. Can be overridden with -t option
TESTS="
	sw_drops
	hw_drops
"

NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
DEVLINK_DEV=netdevsim/${DEV}

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
	fi
}

setup()
{
	modprobe netdevsim &> /dev/null

	set -e
	setup_ns NS1
	$IP link add dummy10 up type dummy

	$NS_EXEC echo "$DEV_ADDR 1" > ${NETDEVSIM_PATH}/new_device
	udevadm settle
	local netdev=$($NS_EXEC ls ${NETDEVSIM_PATH}/devices/${DEV}/net/)
	$IP link set dev $netdev up

	set +e
}

cleanup()
{
	$NS_EXEC echo "$DEV_ADDR" > ${NETDEVSIM_PATH}/del_device
	cleanup_ns ${NS1}
}

sw_drops_test()
{
	echo
	echo "Software drops test"

	setup

	local dir=$(mktemp -d)

	$TC qdisc add dev dummy10 clsact
	$TC filter add dev dummy10 egress pref 1 handle 101 proto ip \
		flower dst_ip 192.0.2.10 action drop

	$NS_EXEC mausezahn dummy10 -a 00:11:22:33:44:55 -b 00:aa:bb:cc:dd:ee \
		-A 192.0.2.1 -B 192.0.2.10 -t udp sp=12345,dp=54321 -c 0 -q \
		-d 100msec &
	timeout 5 dwdump -o sw -w ${dir}/packets.pcap
	(( $(tshark -r ${dir}/packets.pcap \
		-Y 'ip.dst == 192.0.2.10' 2> /dev/null | wc -l) != 0))
	log_test $? 0 "Capturing active software drops"

	rm ${dir}/packets.pcap

	{ kill %% && wait %%; } 2>/dev/null
	timeout 5 dwdump -o sw -w ${dir}/packets.pcap
	(( $(tshark -r ${dir}/packets.pcap \
		-Y 'ip.dst == 192.0.2.10' 2> /dev/null | wc -l) == 0))
	log_test $? 0 "Capturing inactive software drops"

	rm -r $dir

	cleanup
}

hw_drops_test()
{
	echo
	echo "Hardware drops test"

	setup

	local dir=$(mktemp -d)

	$DEVLINK trap set $DEVLINK_DEV trap blackhole_route action trap
	timeout 5 dwdump -o hw -w ${dir}/packets.pcap
	(( $(tshark -r ${dir}/packets.pcap \
		-Y 'net_dm.hw_trap_name== blackhole_route' 2> /dev/null \
		| wc -l) != 0))
	log_test $? 0 "Capturing active hardware drops"

	rm ${dir}/packets.pcap

	$DEVLINK trap set $DEVLINK_DEV trap blackhole_route action drop
	timeout 5 dwdump -o hw -w ${dir}/packets.pcap
	(( $(tshark -r ${dir}/packets.pcap \
		-Y 'net_dm.hw_trap_name== blackhole_route' 2> /dev/null \
		| wc -l) == 0))
	log_test $? 0 "Capturing inactive hardware drops"

	rm -r $dir

	cleanup
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
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v devlink)" ]; then
	echo "SKIP: Could not run test without devlink tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v tshark)" ]; then
	echo "SKIP: Could not run test without tshark tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v dwdump)" ]; then
	echo "SKIP: Could not run test without dwdump tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v udevadm)" ]; then
	echo "SKIP: Could not run test without udevadm tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v timeout)" ]; then
	echo "SKIP: Could not run test without timeout tool"
	exit $ksft_skip
fi

if [ ! -x "$(command -v mausezahn)" ]; then
	echo "SKIP: Could not run test without mausezahn tool"
	exit $ksft_skip
fi

tshark -G fields 2> /dev/null | grep -q net_dm
if [ $? -ne 0 ]; then
	echo "SKIP: tshark too old, missing net_dm dissector"
	exit $ksft_skip
fi

# create netns first so we can get the namespace name
setup_ns NS1
cleanup &> /dev/null
trap cleanup EXIT

IP="ip -netns ${NS1}"
TC="tc -netns ${NS1}"
DEVLINK="devlink -N ${NS1}"
NS_EXEC="ip netns exec ${NS1}"

for t in $TESTS
do
	case $t in
	sw_drops|sw)			sw_drops_test;;
	hw_drops|hw)			hw_drops_test;;

	help) echo "Test names: $TESTS"; exit 0;;
	esac
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
