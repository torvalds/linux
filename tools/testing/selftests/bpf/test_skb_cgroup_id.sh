#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2018 Facebook

set -eu

wait_for_ip()
{
	local _i
	echo -n "Wait for testing link-local IP to become available "
	for _i in $(seq ${MAX_PING_TRIES}); do
		echo -n "."
		if $PING6 -c 1 -W 1 ff02::1%${TEST_IF} >/dev/null 2>&1; then
			echo " OK"
			return
		fi
		sleep 1
	done
	echo 1>&2 "ERROR: Timeout waiting for test IP to become available."
	exit 1
}

setup()
{
	# Create testing interfaces not to interfere with current environment.
	ip link add dev ${TEST_IF} type veth peer name ${TEST_IF_PEER}
	ip link set ${TEST_IF} up
	ip link set ${TEST_IF_PEER} up

	wait_for_ip

	tc qdisc add dev ${TEST_IF} clsact
	mkdir -p /sys/fs/bpf/${BPF_PROG_PIN}
	bpftool prog loadall ${BPF_PROG_OBJ} /sys/fs/bpf/${BPF_PROG_PIN} type tc
	tc filter add dev ${TEST_IF} egress bpf da object-pinned \
		/sys/fs/bpf/${BPF_PROG_PIN}/${BPF_PROG_NAME}

	BPF_PROG_ID=$(tc filter show dev ${TEST_IF} egress | \
			awk '/ id / {sub(/.* id /, "", $0); print($1)}')
}

cleanup()
{
	ip link del ${TEST_IF} 2>/dev/null || :
	ip link del ${TEST_IF_PEER} 2>/dev/null || :
	rm -rf /sys/fs/bpf/${BPF_PROG_PIN}
}

main()
{
	trap cleanup EXIT 2 3 6 15
	setup
	${PROG} ${TEST_IF} ${BPF_PROG_ID}
}

DIR=$(dirname $0)
TEST_IF="test_cgid_1"
TEST_IF_PEER="test_cgid_2"
MAX_PING_TRIES=5
BPF_PROG_PIN="cgroup_ancestor"
BPF_PROG_OBJ="${DIR}/${BPF_PROG_PIN}.bpf.o"
BPF_PROG_NAME="log_cgroup_id"
BPF_PROG_ID=0
PROG="${DIR}/test_skb_cgroup_id_user"
type ping6 >/dev/null 2>&1 && PING6="ping6" || PING6="ping -6"

main
