#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for checking ICMP response with dummy address instead of 0.0.0.0.
# Sets up two namespaces like:
# +----------------------+                          +--------------------+
# | ns1                  |    v4-via-v6 routes:     | ns2                |
# |                      |                  '       |                    |
# |             +--------+   -> 172.16.1.0/24 ->    +--------+           |
# |             | veth0  +--------------------------+  veth0 |           |
# |             +--------+   <- 172.16.0.0/24 <-    +--------+           |
# |           172.16.0.1 |                          | 2001:db8:1::2/64   |
# |     2001:db8:1::2/64 |                          |                    |
# +----------------------+                          +--------------------+
#
# And then tries to ping 172.16.1.1 from ns1. This results in a "net
# unreachable" message being sent from ns2, but there is no IPv4 address set in
# that address space, so the kernel should substitute the dummy address
# 192.0.0.8 defined in RFC7600.

source lib.sh

H1_IP=172.16.0.1/32
H1_IP6=2001:db8:1::1
RT1=172.16.1.0/24
PINGADDR=172.16.1.1
RT2=172.16.0.0/24
H2_IP6=2001:db8:1::2

cleanup()
{
    cleanup_ns $NS1 $NS2
}

trap cleanup EXIT

# Namespaces
setup_ns NS1 NS2

# Connectivity
ip -netns $NS1 link add veth0 type veth peer name veth0 netns $NS2
ip -netns $NS1 link set dev veth0 up
ip -netns $NS2 link set dev veth0 up
ip -netns $NS1 addr add $H1_IP dev veth0
ip -netns $NS1 addr add $H1_IP6/64 dev veth0 nodad
ip -netns $NS2 addr add $H2_IP6/64 dev veth0 nodad
ip -netns $NS1 route add $RT1 via inet6 $H2_IP6
ip -netns $NS2 route add $RT2 via inet6 $H1_IP6

# Make sure ns2 will respond with ICMP unreachable
ip netns exec $NS2 sysctl -qw net.ipv4.icmp_ratelimit=0 net.ipv4.ip_forward=1
# Install tc flower filter to count ICMP unreachable from 192.0.0.8
ip netns exec $NS1 tc qdisc add dev veth0 clsact
ip netns exec $NS1 tc filter add dev veth0 ingress \
	protocol ip pref 1 handle 1 flower \
	src_ip 192.0.0.8 ip_proto icmp type 3 code 0 \
	action pass

# Send ping to trigger ICMP unreachable from ns2
ip netns exec $NS1 ping -c 1 $PINGADDR >/dev/null 2>&1

# Verify ICMP unreachable was received from dummy address 192.0.0.8 per RFC7600
COUNT=$(ip netns exec $NS1 tc -j -s filter show dev veth0 ingress \
	| jq '.[1].options.actions[].stats.packets')

if [[ "$COUNT" -eq 1 ]]; then
    echo "OK"
    exit 0
else
    echo "FAIL - expected 1 ICMP unreachable from 192.0.0.8, got $COUNT"
    exit 1
fi
