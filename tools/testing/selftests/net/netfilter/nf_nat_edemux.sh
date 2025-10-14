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
checktool "conntrack --version" "run test without conntrack"

trap cleanup EXIT

connect_done()
{
	local ns="$1"
	local port="$2"

	ip netns exec "$ns" ss -nt -o state established "dport = :$port" | grep -q "$port"
}

check_ctstate()
{
	local ns="$1"
	local dp="$2"

	if ! ip netns exec "$ns" conntrack --get -s 192.168.1.2 -d 192.168.1.1 -p tcp \
	     --sport 10000 --dport "$dp" --state ESTABLISHED > /dev/null 2>&1;then
		echo "FAIL: Did not find expected state for dport $2"
		ip netns exec "$ns" bash -c 'conntrack -L; conntrack -S; ss -nt'
		ret=1
	fi
}

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
ip netns exec "$ns2" iptables -t nat -A OUTPUT -d 10.96.0.1/32 -p tcp --dport 443 -j DNAT --to-destination 192.168.1.1:5201 || exit 1

# ... and route it to the other namespace
ip netns exec "$ns2" ip route add 10.96.0.1 via 192.168.1.1

# listener should be up by now, wait if it isn't yet.
wait_local_port_listen "$ns1" 5201 tcp

# add a persistent connection from the other namespace
sleep 10 | ip netns exec "$ns2" socat -t 10 - TCP:192.168.1.1:5201 > /dev/null &
cpid0=$!
busywait "$BUSYWAIT_TIMEOUT" connect_done "$ns2" "5201"

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

sleep 5 | ip netns exec "$ns2" socat -T 5 -u STDIN TCP:192.168.1.1:5202,connect-timeout=5 >/dev/null &
cpid1=$!

sleep 5 | ip netns exec "$ns2" socat -T 5 -u STDIN TCP:192.168.1.1:5203,connect-timeout=5 >/dev/null &
cpid2=$!

busywait "$BUSYWAIT_TIMEOUT" connect_done "$ns2" 5202
busywait "$BUSYWAIT_TIMEOUT" connect_done "$ns2" 5203

check_ctstate "$ns1" 5202
check_ctstate "$ns1" 5203

kill $socatpid $cpid0 $cpid1 $cpid2
socatpid=0

if [ $ret -eq 0 ]; then
	echo "PASS: could connect to service via redirected ports"
else
	echo "FAIL: socat cannot connect to service via redirect"
	ret=1
fi

exit $ret
