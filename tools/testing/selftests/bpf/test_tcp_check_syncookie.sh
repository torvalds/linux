#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2018 Facebook
# Copyright (c) 2019 Cloudflare

set -eu
readonly NS1="ns1-$(mktemp -u XXXXXX)"

wait_for_ip()
{
	local _i
	printf "Wait for IP %s to become available " "$1"
	for _i in $(seq ${MAX_PING_TRIES}); do
		printf "."
		if ns1_exec ping -c 1 -W 1 "$1" >/dev/null 2>&1; then
			echo " OK"
			return
		fi
		sleep 1
	done
	echo 1>&2 "ERROR: Timeout waiting for test IP to become available."
	exit 1
}

get_prog_id()
{
	awk '/ id / {sub(/.* id /, "", $0); print($1)}'
}

ns1_exec()
{
	ip netns exec ${NS1} "$@"
}

setup()
{
	ip netns add ${NS1}
	ns1_exec ip link set lo up

	ns1_exec sysctl -w net.ipv4.tcp_syncookies=2
	ns1_exec sysctl -w net.ipv4.tcp_window_scaling=0
	ns1_exec sysctl -w net.ipv4.tcp_timestamps=0
	ns1_exec sysctl -w net.ipv4.tcp_sack=0

	wait_for_ip 127.0.0.1
	wait_for_ip ::1
}

cleanup()
{
	ip netns del ns1 2>/dev/null || :
}

main()
{
	trap cleanup EXIT 2 3 6 15
	setup

	printf "Testing clsact..."
	ns1_exec tc qdisc add dev "${TEST_IF}" clsact
	ns1_exec tc filter add dev "${TEST_IF}" ingress \
		bpf obj "${BPF_PROG_OBJ}" sec "${CLSACT_SECTION}" da

	BPF_PROG_ID=$(ns1_exec tc filter show dev "${TEST_IF}" ingress | \
		      get_prog_id)
	ns1_exec "${PROG}" "${BPF_PROG_ID}"
	ns1_exec tc qdisc del dev "${TEST_IF}" clsact

	printf "Testing XDP..."
	ns1_exec ip link set "${TEST_IF}" xdp \
		object "${BPF_PROG_OBJ}" section "${XDP_SECTION}"
	BPF_PROG_ID=$(ns1_exec ip link show "${TEST_IF}" | get_prog_id)
	ns1_exec "${PROG}" "${BPF_PROG_ID}"
}

DIR=$(dirname $0)
TEST_IF=lo
MAX_PING_TRIES=5
BPF_PROG_OBJ="${DIR}/test_tcp_check_syncookie_kern.bpf.o"
CLSACT_SECTION="tc"
XDP_SECTION="xdp"
BPF_PROG_ID=0
PROG="${DIR}/test_tcp_check_syncookie_user"

main
