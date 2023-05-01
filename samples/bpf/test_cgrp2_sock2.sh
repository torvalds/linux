#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

BPFFS=/sys/fs/bpf
MY_DIR=$(dirname $0)
TEST=$MY_DIR/test_cgrp2_sock2
LINK_PIN=$BPFFS/test_cgrp2_sock2
BPF_PROG=$MY_DIR/sock_flags.bpf.o

function config_device {
	ip netns add at_ns0
	ip link add veth0 type veth peer name veth0b
	ip link set veth0 netns at_ns0
	ip netns exec at_ns0 sysctl -q net.ipv6.conf.veth0.disable_ipv6=0
	ip netns exec at_ns0 ip addr add 172.16.1.100/24 dev veth0
	ip netns exec at_ns0 ip addr add 2401:db00::1/64 dev veth0 nodad
	ip netns exec at_ns0 ip link set dev veth0 up
	sysctl -q net.ipv6.conf.veth0b.disable_ipv6=0
	ip addr add 172.16.1.101/24 dev veth0b
	ip addr add 2401:db00::2/64 dev veth0b nodad
	ip link set veth0b up
}

function config_cgroup {
	rm -rf /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2
	mount -t cgroup2 none /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2/foo
	echo $$ >> /tmp/cgroupv2/foo/cgroup.procs
}

function config_bpffs {
	if mount | grep $BPFFS > /dev/null; then
		echo "bpffs already mounted"
	else
		echo "bpffs not mounted. Mounting..."
		mount -t bpf none $BPFFS
	fi
}

function attach_bpf {
	$TEST /tmp/cgroupv2/foo $BPF_PROG $1
	[ $? -ne 0 ] && exit 1
}

function cleanup {
	rm -rf $LINK_PIN
	ip link del veth0b
	ip netns delete at_ns0
	umount /tmp/cgroupv2
	rm -rf /tmp/cgroupv2
}

cleanup 2>/dev/null

set -e
config_device
config_cgroup
config_bpffs
set +e

#
# Test 1 - fail ping6
#
attach_bpf 0
ping -c1 -w1 172.16.1.100
if [ $? -ne 0 ]; then
	echo "ping failed when it should succeed"
	cleanup
	exit 1
fi

ping6 -c1 -w1 2401:db00::1
if [ $? -eq 0 ]; then
	echo "ping6 succeeded when it should not"
	cleanup
	exit 1
fi

rm -rf $LINK_PIN
sleep 1                 # Wait for link detach

#
# Test 2 - fail ping
#
attach_bpf 1
ping6 -c1 -w1 2401:db00::1
if [ $? -ne 0 ]; then
	echo "ping6 failed when it should succeed"
	cleanup
	exit 1
fi

ping -c1 -w1 172.16.1.100
if [ $? -eq 0 ]; then
	echo "ping succeeded when it should not"
	cleanup
	exit 1
fi

cleanup
echo
echo "*** PASS ***"
