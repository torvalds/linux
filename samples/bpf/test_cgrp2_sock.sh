#!/bin/bash

function config_device {
	ip netns add at_ns0
	ip link add veth0 type veth peer name veth0b
	ip link set veth0b up
	ip link set veth0 netns at_ns0
	ip netns exec at_ns0 ip addr add 172.16.1.100/24 dev veth0
	ip netns exec at_ns0 ip addr add 2401:db00::1/64 dev veth0 nodad
	ip netns exec at_ns0 ip link set dev veth0 up
	ip link add foo type vrf table 1234
	ip link set foo up
	ip addr add 172.16.1.101/24 dev veth0b
	ip addr add 2401:db00::2/64 dev veth0b nodad
	ip link set veth0b master foo
}

function attach_bpf {
	rm -rf /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2
	mount -t cgroup2 none /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2/foo
	test_cgrp2_sock /tmp/cgroupv2/foo foo
	echo $$ >> /tmp/cgroupv2/foo/cgroup.procs
}

function cleanup {
	set +ex
	ip netns delete at_ns0
	ip link del veth0
	ip link del foo
	umount /tmp/cgroupv2
	rm -rf /tmp/cgroupv2
	set -ex
}

function do_test {
	ping -c1 -w1 172.16.1.100
	ping6 -c1 -w1 2401:db00::1
}

cleanup 2>/dev/null
config_device
attach_bpf
do_test
cleanup
echo "*** PASS ***"
