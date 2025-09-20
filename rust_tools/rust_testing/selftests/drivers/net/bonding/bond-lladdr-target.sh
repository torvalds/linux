#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Regression Test:
#   Verify bond interface could up when set IPv6 link local address target.
#
#  +----------------+
#  |      br0       |
#  |       |        |    sw
#  | veth0   veth1  |
#  +---+-------+----+
#      |       |
#  +---+-------+----+
#  | veth0   veth1  |
#  |       |        |    host
#  |     bond0      |
#  +----------------+
#
# We use veths instead of physical interfaces
REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

sw="sw-$(mktemp -u XXXXXX)"
host="ns-$(mktemp -u XXXXXX)"

cleanup()
{
	ip netns del $sw
	ip netns del $host
}

wait_lladdr_dad()
{
	$@ | grep fe80 | grep -qv tentative
}

wait_bond_up()
{
	$@ | grep -q 'state UP'
}

trap cleanup 0 1 2

ip netns add $sw
ip netns add $host

ip -n $host link add veth0 type veth peer name veth0 netns $sw
ip -n $host link add veth1 type veth peer name veth1 netns $sw

ip -n $sw link add br0 type bridge
ip -n $sw link set br0 up
sw_lladdr=$(ip -n $sw addr show br0 | awk '/fe80/{print $2}' | cut -d'/' -f1)
# wait some time to make sure bridge lladdr pass DAD
slowwait 2 wait_lladdr_dad ip -n $sw addr show br0

ip -n $host link add bond0 type bond mode 1 ns_ip6_target ${sw_lladdr} \
	arp_validate 3 arp_interval 1000
# add a lladdr for bond to make sure there is a route to target
ip -n $host addr add fe80::beef/64 dev bond0
ip -n $host link set bond0 up
ip -n $host link set veth0 master bond0
ip -n $host link set veth1 master bond0

ip -n $sw link set veth0 master br0
ip -n $sw link set veth1 master br0
ip -n $sw link set veth0 up
ip -n $sw link set veth1 up

slowwait 5 wait_bond_up ip -n $host link show bond0

rc=0
if ip -n $host link show bond0 | grep -q LOWER_UP; then
	echo "PASS"
else
	echo "FAIL"
	rc=1
fi
exit $rc
