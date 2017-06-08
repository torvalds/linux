#!/bin/bash

function config_device {
	ip netns add at_ns0
	ip link add veth0 type veth peer name veth0b
	ip link set veth0b up
	ip link set veth0 netns at_ns0
	ip netns exec at_ns0 ip addr add 172.16.1.100/24 dev veth0
	ip netns exec at_ns0 ip addr add 2401:db00::1/64 dev veth0 nodad
	ip netns exec at_ns0 ip link set dev veth0 up
	ip addr add 172.16.1.101/24 dev veth0b
	ip addr add 2401:db00::2/64 dev veth0b nodad
}

function config_cgroup {
	rm -rf /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2
	mount -t cgroup2 none /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2/foo
	echo $$ >> /tmp/cgroupv2/foo/cgroup.procs
}


function attach_bpf {
	test_cgrp2_sock2 /tmp/cgroupv2/foo sock_flags_kern.o $1
	[ $? -ne 0 ] && exit 1
}

function cleanup {
	ip link del veth0b
	ip netns delete at_ns0
	umount /tmp/cgroupv2
	rm -rf /tmp/cgroupv2
}

cleanup 2>/dev/null

set -e
config_device
config_cgroup
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
