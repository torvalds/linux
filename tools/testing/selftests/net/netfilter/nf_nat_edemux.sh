#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test NAT source port clash resolution
#

source lib.sh
ret=0
socatpid=0

cleanup()
{
	[ "$socatpid" -gt 0 ] && kill "$socatpid"

	cleanup_all_ns
}

checktool "socat -h" "run test without socat"
checktool "iptables --version" "run test without iptables"

trap cleanup EXIT

setup_ns ns1 ns2

# Connect the namespaces using a veth pair
ip link add name veth2 type veth peer name veth1
ip link set netns "$ns1" dev veth1
ip link set netns "$ns2" dev veth2

ip netns exec "$ns1" ip link set up dev lo
ip netns exec "$ns1" ip link set up dev veth1
ip netns exec "$ns1" ip addr add 192.168.1.1/24 dev veth1

ip netns exec "$ns2" ip link set up dev lo
ip netns exec "$ns2" ip link set up dev veth2
ip netns exec "$ns2" ip addr add 192.168.1.2/24 dev veth2

# Create a server in one namespace
ip netns exec "$ns1" socat -u TCP-LISTEN:5201,fork OPEN:/dev/null,wronly=1 &
socatpid=$!

# Restrict source port to just one so we don't have to exhaust
# all others.
ip netns exec "$ns2" sysctl -q net.ipv4.ip_local_port_range="10000 10000"

# add a virtual IP using DNAT
ip netns exec "$ns2" iptables -t nat -A OUTPUT -d 10.96.0.1/32 -p tcp --dport 443 -j DNAT --to-destination 192.168.1.1:5201

# ... and route it to the other namespace
ip netns exec "$ns2" ip route add 10.96.0.1 via 192.168.1.1

# add a persistent connection from the other namespace
ip netns exec "$ns2" socat -t 10 - TCP:192.168.1.1:5201 > /dev/null &

sleep 1

# ip daddr:dport will be rewritten to 192.168.1.1 5201
# NAT must reallocate source port 10000 because
# 192.168.1.2:10000 -> 192.168.1.1:5201 is already in use
echo test | ip netns exec "$ns2" socat -t 3 -u STDIN TCP:10.96.0.1:443,connect-timeout=3 >/dev/null
ret=$?

# Check socat can connect to 10.96.0.1:443 (aka 192.168.1.1:5201).
if [ $ret -eq 0 ]; then
	echo "PASS: socat can connect via NAT'd address"
else
	echo "FAIL: socat cannot connect via NAT'd address"
fi

# check sport clashres.
ip netns exec "$ns1" iptables -t nat -A PREROUTING -p tcp --dport 5202 -j REDIRECT --to-ports 5201
ip netns exec "$ns1" iptables -t nat -A PREROUTING -p tcp --dport 5203 -j REDIRECT --to-ports 5201

sleep 5 | ip netns exec "$ns2" socat -t 5 -u STDIN TCP:192.168.1.1:5202,connect-timeout=5 >/dev/null &

# if connect succeeds, client closes instantly due to EOF on stdin.
# if connect hangs, it will time out after 5s.
echo | ip netns exec "$ns2" socat -t 3 -u STDIN TCP:192.168.1.1:5203,connect-timeout=5 >/dev/null &
cpid2=$!

time_then=$(date +%s)
wait $cpid2
rv=$?
time_now=$(date +%s)

# Check how much time has elapsed, expectation is for
# 'cpid2' to connect and then exit (and no connect delay).
delta=$((time_now - time_then))

if [ $delta -lt 2 ] && [ $rv -eq 0 ]; then
	echo "PASS: could connect to service via redirected ports"
else
	echo "FAIL: socat cannot connect to service via redirect ($delta seconds elapsed, returned $rv)"
	ret=1
fi

exit $ret
