#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Regression tests for the SO_TXTIME interface

set -e

readonly ksft_skip=4
readonly DEV="veth0"
readonly BIN="./so_txtime"

readonly RAND="$(mktemp -u XXXXXX)"
readonly NSPREFIX="ns-${RAND}"
readonly NS1="${NSPREFIX}1"
readonly NS2="${NSPREFIX}2"

readonly SADDR4='192.168.1.1'
readonly DADDR4='192.168.1.2'
readonly SADDR6='fd::1'
readonly DADDR6='fd::2'

cleanup() {
	ip netns del "${NS2}"
	ip netns del "${NS1}"
}

trap cleanup EXIT

# Create virtual ethernet pair between network namespaces
ip netns add "${NS1}"
ip netns add "${NS2}"

ip link add "${DEV}" netns "${NS1}" type veth \
  peer name "${DEV}" netns "${NS2}"

# Bring the devices up
ip -netns "${NS1}" link set "${DEV}" up
ip -netns "${NS2}" link set "${DEV}" up

# Set fixed MAC addresses on the devices
ip -netns "${NS1}" link set dev "${DEV}" address 02:02:02:02:02:02
ip -netns "${NS2}" link set dev "${DEV}" address 06:06:06:06:06:06

# Add fixed IP addresses to the devices
ip -netns "${NS1}" addr add 192.168.1.1/24 dev "${DEV}"
ip -netns "${NS2}" addr add 192.168.1.2/24 dev "${DEV}"
ip -netns "${NS1}" addr add       fd::1/64 dev "${DEV}" nodad
ip -netns "${NS2}" addr add       fd::2/64 dev "${DEV}" nodad

run_test() {
	local readonly IP="$1"
	local readonly CLOCK="$2"
	local readonly TXARGS="$3"
	local readonly RXARGS="$4"

	if [[ "${IP}" == "4" ]]; then
		local readonly SADDR="${SADDR4}"
		local readonly DADDR="${DADDR4}"
	elif [[ "${IP}" == "6" ]]; then
		local readonly SADDR="${SADDR6}"
		local readonly DADDR="${DADDR6}"
	else
		echo "Invalid IP version ${IP}"
		exit 1
	fi

	local readonly START="$(date +%s%N --date="+ 0.1 seconds")"

	ip netns exec "${NS2}" "${BIN}" -"${IP}" -c "${CLOCK}" -t "${START}" -S "${SADDR}" -D "${DADDR}" "${RXARGS}" -r &
	ip netns exec "${NS1}" "${BIN}" -"${IP}" -c "${CLOCK}" -t "${START}" -S "${SADDR}" -D "${DADDR}" "${TXARGS}"
	wait "$!"
}

do_test() {
	run_test $@
	[ $? -ne 0 ] && ret=1
}

do_fail_test() {
	run_test $@
	[ $? -eq 0 ] && ret=1
}

ip netns exec "${NS1}" tc qdisc add dev "${DEV}" root fq
set +e
ret=0
do_test 4 mono a,-1 a,-1
do_test 6 mono a,0 a,0
do_test 6 mono a,10 a,10
do_test 4 mono a,10,b,20 a,10,b,20
do_test 6 mono a,20,b,10 b,20,a,20

if ip netns exec "${NS1}" tc qdisc replace dev "${DEV}" root etf clockid CLOCK_TAI delta 400000; then
	do_fail_test 4 tai a,-1 a,-1
	do_fail_test 6 tai a,0 a,0
	do_test 6 tai a,10 a,10
	do_test 4 tai a,10,b,20 a,10,b,20
	do_test 6 tai a,20,b,10 b,10,a,20
else
	echo "tc ($(tc -V)) does not support qdisc etf. skipping"
	[ $ret -eq 0 ] && ret=$ksft_skip
fi

if [ $ret -eq 0 ]; then
	echo OK. All tests passed
elif [[ $ret -ne $ksft_skip && -n "$KSFT_MACHINE_SLOW" ]]; then
	echo "Ignoring errors due to slow environment" 1>&2
	ret=0
fi
exit $ret
