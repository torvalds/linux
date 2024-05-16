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

TMPFILE=$(mktemp)

cleanup()
{
    rm -f "$TMPFILE"
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

# Run the test - a ping runs in the background, and we capture ICMP responses
# with tcpdump; -c 1 means it should exit on the first ping, but add a timeout
# in case something goes wrong
ip netns exec $NS1 ping -w 3 -i 0.5 $PINGADDR >/dev/null &
ip netns exec $NS1 timeout 10 tcpdump -tpni veth0 -c 1 'icmp and icmp[icmptype] != icmp-echo' > $TMPFILE 2>/dev/null

# Parse response and check for dummy address
# tcpdump output looks like:
# IP 192.0.0.8 > 172.16.0.1: ICMP net 172.16.1.1 unreachable, length 92
RESP_IP=$(awk '{print $2}' < $TMPFILE)
if [[ "$RESP_IP" != "192.0.0.8" ]]; then
    echo "FAIL - got ICMP response from $RESP_IP, should be 192.0.0.8"
    exit 1
else
    echo "OK"
    exit 0
fi
