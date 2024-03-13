#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# Conntrack needs to reassemble fragments in order to have complete
# packets for rule matching.  Reassembly can lead to packet loss.

# Consider the following setup:
#            +--------+       +---------+       +--------+
#            |Router A|-------|Wanrouter|-------|Router B|
#            |        |.IPIP..|         |..IPIP.|        |
#            +--------+       +---------+       +--------+
#           /                  mtu 1400                   \
#          /                                               \
#+--------+                                                 +--------+
#|Client A|                                                 |Client B|
#|        |                                                 |        |
#+--------+                                                 +--------+

# Router A and Router B use IPIP tunnel interfaces to tunnel traffic
# between Client A and Client B over WAN. Wanrouter has MTU 1400 set
# on its interfaces.

rnd=$(mktemp -u XXXXXXXX)
rx=$(mktemp)

r_a="ns-ra-$rnd"
r_b="ns-rb-$rnd"
r_w="ns-rw-$rnd"
c_a="ns-ca-$rnd"
c_b="ns-cb-$rnd"

checktool (){
	if ! $1 > /dev/null 2>&1; then
		echo "SKIP: Could not $2"
		exit $ksft_skip
	fi
}

checktool "iptables --version" "run test without iptables"
checktool "ip -Version" "run test without ip tool"
checktool "which socat" "run test without socat"
checktool "ip netns add ${r_a}" "create net namespace"

for n in ${r_b} ${r_w} ${c_a} ${c_b};do
	ip netns add ${n}
done

cleanup() {
	for n in ${r_a} ${r_b} ${r_w} ${c_a} ${c_b};do
		ip netns del ${n}
	done
	rm -f ${rx}
}

trap cleanup EXIT

test_path() {
	msg="$1"

	ip netns exec ${c_b} socat -t 3 - udp4-listen:5000,reuseaddr > ${rx} < /dev/null &

	sleep 1
	for i in 1 2 3; do
		head -c1400 /dev/zero | tr "\000" "a" | \
			ip netns exec ${c_a} socat -t 1 -u STDIN UDP:192.168.20.2:5000
	done

	wait

	bytes=$(wc -c < ${rx})

	if [ $bytes -eq 1400 ];then
		echo "OK: PMTU $msg connection tracking"
	else
		echo "FAIL: PMTU $msg connection tracking: got $bytes, expected 1400"
		exit 1
	fi
}

# Detailed setup for Router A
# ---------------------------
# Interfaces:
# eth0: 10.2.2.1/24
# eth1: 192.168.10.1/24
# ipip0: No IP address, local 10.2.2.1 remote 10.4.4.1
# Routes:
# 192.168.20.0/24 dev ipip0    (192.168.20.0/24 is subnet of Client B)
# 10.4.4.1 via 10.2.2.254      (Router B via Wanrouter)
# No iptables rules at all.

ip link add veth0 netns ${r_a} type veth peer name veth0 netns ${r_w}
ip link add veth1 netns ${r_a} type veth peer name veth0 netns ${c_a}

l_addr="10.2.2.1"
r_addr="10.4.4.1"
ip netns exec ${r_a} ip link add ipip0 type ipip local ${l_addr} remote ${r_addr} mode ipip || exit $ksft_skip

for dev in lo veth0 veth1 ipip0; do
    ip -net ${r_a} link set $dev up
done

ip -net ${r_a} addr add 10.2.2.1/24 dev veth0
ip -net ${r_a} addr add 192.168.10.1/24 dev veth1

ip -net ${r_a} route add 192.168.20.0/24 dev ipip0
ip -net ${r_a} route add 10.4.4.0/24 via 10.2.2.254

ip netns exec ${r_a} sysctl -q net.ipv4.conf.all.forwarding=1 > /dev/null

# Detailed setup for Router B
# ---------------------------
# Interfaces:
# eth0: 10.4.4.1/24
# eth1: 192.168.20.1/24
# ipip0: No IP address, local 10.4.4.1 remote 10.2.2.1
# Routes:
# 192.168.10.0/24 dev ipip0    (192.168.10.0/24 is subnet of Client A)
# 10.2.2.1 via 10.4.4.254      (Router A via Wanrouter)
# No iptables rules at all.

ip link add veth0 netns ${r_b} type veth peer name veth1 netns ${r_w}
ip link add veth1 netns ${r_b} type veth peer name veth0 netns ${c_b}

l_addr="10.4.4.1"
r_addr="10.2.2.1"

ip netns exec ${r_b} ip link add ipip0 type ipip local ${l_addr} remote ${r_addr} mode ipip || exit $ksft_skip

for dev in lo veth0 veth1 ipip0; do
	ip -net ${r_b} link set $dev up
done

ip -net ${r_b} addr add 10.4.4.1/24 dev veth0
ip -net ${r_b} addr add 192.168.20.1/24 dev veth1

ip -net ${r_b} route add 192.168.10.0/24 dev ipip0
ip -net ${r_b} route add 10.2.2.0/24 via 10.4.4.254
ip netns exec ${r_b} sysctl -q net.ipv4.conf.all.forwarding=1 > /dev/null

# Client A
ip -net ${c_a} addr add 192.168.10.2/24 dev veth0
ip -net ${c_a} link set dev lo up
ip -net ${c_a} link set dev veth0 up
ip -net ${c_a} route add default via 192.168.10.1

# Client A
ip -net ${c_b} addr add 192.168.20.2/24 dev veth0
ip -net ${c_b} link set dev veth0 up
ip -net ${c_b} link set dev lo up
ip -net ${c_b} route add default via 192.168.20.1

# Wan
ip -net ${r_w} addr add 10.2.2.254/24 dev veth0
ip -net ${r_w} addr add 10.4.4.254/24 dev veth1

ip -net ${r_w} link set dev lo up
ip -net ${r_w} link set dev veth0 up mtu 1400
ip -net ${r_w} link set dev veth1 up mtu 1400

ip -net ${r_a} link set dev veth0 mtu 1400
ip -net ${r_b} link set dev veth0 mtu 1400

ip netns exec ${r_w} sysctl -q net.ipv4.conf.all.forwarding=1 > /dev/null

# Path MTU discovery
# ------------------
# Running tracepath from Client A to Client B shows PMTU discovery is working
# as expected:
#
# clienta:~# tracepath 192.168.20.2
# 1?: [LOCALHOST]                      pmtu 1500
# 1:  192.168.10.1                                          0.867ms
# 1:  192.168.10.1                                          0.302ms
# 2:  192.168.10.1                                          0.312ms pmtu 1480
# 2:  no reply
# 3:  192.168.10.1                                          0.510ms pmtu 1380
# 3:  192.168.20.2                                          2.320ms reached
# Resume: pmtu 1380 hops 3 back 3

# ip netns exec ${c_a} traceroute --mtu 192.168.20.2

# Router A has learned PMTU (1400) to Router B from Wanrouter.
# Client A has learned PMTU (1400 - IPIP overhead = 1380) to Client B
# from Router A.

#Send large UDP packet
#---------------------
#Now we send a 1400 bytes UDP packet from Client A to Client B:

# clienta:~# head -c1400 /dev/zero | tr "\000" "a" | socat -u STDIN UDP:192.168.20.2:5000
test_path "without"

# The IPv4 stack on Client A already knows the PMTU to Client B, so the
# UDP packet is sent as two fragments (1380 + 20). Router A forwards the
# fragments between eth1 and ipip0. The fragments fit into the tunnel and
# reach their destination.

#When sending the large UDP packet again, Router A now reassembles the
#fragments before routing the packet over ipip0. The resulting IPIP
#packet is too big (1400) for the tunnel PMTU (1380) to Router B, it is
#dropped on Router A before sending.

ip netns exec ${r_a} iptables -A FORWARD -m conntrack --ctstate NEW
test_path "with"
