#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# cause kernel oops in bond_rr_gen_slave_id
DEBUG=${DEBUG:-0}

set -e
test ${DEBUG} -ne 0 && set -x

finish()
{
	ip netns delete server || true
	ip netns delete client || true
}

trap finish EXIT

client_ip4=192.168.1.198
server_ip4=192.168.1.254

# setup kernel so it reboots after causing the panic
echo 180 >/proc/sys/kernel/panic

# build namespaces
ip netns add "server"
ip netns add "client"
ip -n client link add eth0 type veth peer name eth0 netns server
ip netns exec server ip link set dev eth0 up
ip netns exec server ip addr add ${server_ip4}/24 dev eth0

ip netns exec client ip link add dev bond0 down type bond mode 1 \
	miimon 100 all_slaves_active 1
ip netns exec client ip link set dev eth0 master bond0
ip netns exec client ip link set dev bond0 up
ip netns exec client ip addr add ${client_ip4}/24 dev bond0
ip netns exec client ping -c 5 $server_ip4 >/dev/null

ip netns exec client ip link set dev eth0 nomaster
ip netns exec client ip link set dev bond0 down
ip netns exec client ip link set dev bond0 type bond mode 0 \
	arp_interval 1000 arp_ip_target "+${server_ip4}"
ip netns exec client ip link set dev eth0 master bond0
ip netns exec client ip link set dev bond0 up
ip netns exec client ping -c 5 $server_ip4 >/dev/null

exit 0
