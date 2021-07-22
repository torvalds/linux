#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test NAT source port clash resolution
#

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

sfx=$(mktemp -u "XXXXXXXX")
ns1="ns1-$sfx"
ns2="ns2-$sfx"

cleanup()
{
	ip netns del $ns1
	ip netns del $ns2
}

iperf3 -v > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without iperf3"
	exit $ksft_skip
fi

iptables --version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without iptables"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip netns add "$ns1"
if [ $? -ne 0 ];then
	echo "SKIP: Could not create net namespace $ns1"
	exit $ksft_skip
fi

trap cleanup EXIT

ip netns add $ns2

# Connect the namespaces using a veth pair
ip link add name veth2 type veth peer name veth1
ip link set netns $ns1 dev veth1
ip link set netns $ns2 dev veth2

ip netns exec $ns1 ip link set up dev lo
ip netns exec $ns1 ip link set up dev veth1
ip netns exec $ns1 ip addr add 192.168.1.1/24 dev veth1

ip netns exec $ns2 ip link set up dev lo
ip netns exec $ns2 ip link set up dev veth2
ip netns exec $ns2 ip addr add 192.168.1.2/24 dev veth2

# Create a server in one namespace
ip netns exec $ns1 iperf3 -s > /dev/null 2>&1 &
iperfs=$!

# Restrict source port to just one so we don't have to exhaust
# all others.
ip netns exec $ns2 sysctl -q net.ipv4.ip_local_port_range="10000 10000"

# add a virtual IP using DNAT
ip netns exec $ns2 iptables -t nat -A OUTPUT -d 10.96.0.1/32 -p tcp --dport 443 -j DNAT --to-destination 192.168.1.1:5201

# ... and route it to the other namespace
ip netns exec $ns2 ip route add 10.96.0.1 via 192.168.1.1

sleep 1

# add a persistent connection from the other namespace
ip netns exec $ns2 nc -q 10 -w 10 192.168.1.1 5201 > /dev/null &

sleep 1

# ip daddr:dport will be rewritten to 192.168.1.1 5201
# NAT must reallocate source port 10000 because
# 192.168.1.2:10000 -> 192.168.1.1:5201 is already in use
echo test | ip netns exec $ns2 nc -w 3 -q 3 10.96.0.1 443 >/dev/null
ret=$?

kill $iperfs

# Check nc can connect to 10.96.0.1:443 (aka 192.168.1.1:5201).
if [ $ret -eq 0 ]; then
	echo "PASS: nc can connect via NAT'd address"
else
	echo "FAIL: nc cannot connect via NAT'd address"
	exit 1
fi

exit 0
