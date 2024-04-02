#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bridge netfilter + conntrack, a combination that doesn't really work,
# with multicast/broadcast packets racing for hash table insertion.

#           eth0    br0     eth0
# setup is: ns1 <->,ns0 <-> ns3
#           ns2 <-'    `'-> ns4

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

sfx=$(mktemp -u "XXXXXXXX")
ns0="ns0-$sfx"
ns1="ns1-$sfx"
ns2="ns2-$sfx"
ns3="ns3-$sfx"
ns4="ns4-$sfx"

ebtables -V > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ebtables"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

for i in $(seq 0 4); do
  eval ip netns add \$ns$i
done

cleanup() {
  for i in $(seq 0 4); do eval ip netns del \$ns$i;done
}

trap cleanup EXIT

do_ping()
{
	fromns="$1"
	dstip="$2"

	ip netns exec $fromns ping -c 1 -q $dstip > /dev/null
	if [ $? -ne 0 ]; then
		echo "ERROR: ping from $fromns to $dstip"
		ip netns exec ${ns0} nft list ruleset
		ret=1
	fi
}

bcast_ping()
{
	fromns="$1"
	dstip="$2"

	for i in $(seq 1 1000); do
		ip netns exec $fromns ping -q -f -b -c 1 -q $dstip > /dev/null 2>&1
		if [ $? -ne 0 ]; then
			echo "ERROR: ping -b from $fromns to $dstip"
			ip netns exec ${ns0} nft list ruleset
			fi
	done
}

ip link add veth1 netns ${ns0} type veth peer name eth0 netns ${ns1}
if [ $? -ne 0 ]; then
	echo "SKIP: Can't create veth device"
	exit $ksft_skip
fi

ip link add veth2 netns ${ns0} type veth peer name eth0 netns $ns2
ip link add veth3 netns ${ns0} type veth peer name eth0 netns $ns3
ip link add veth4 netns ${ns0} type veth peer name eth0 netns $ns4

ip -net ${ns0} link set lo up

for i in $(seq 1 4); do
  ip -net ${ns0} link set veth$i up
done

ip -net ${ns0} link add br0 type bridge stp_state 0 forward_delay 0 nf_call_iptables 1 nf_call_ip6tables 1 nf_call_arptables 1
if [ $? -ne 0 ]; then
	echo "SKIP: Can't create bridge br0"
	exit $ksft_skip
fi

# make veth0,1,2 part of bridge.
for i in $(seq 1 3); do
  ip -net ${ns0} link set veth$i master br0
done

# add a macvlan on top of the bridge.
MACVLAN_ADDR=ba:f3:13:37:42:23
ip -net ${ns0} link add link br0 name macvlan0 type macvlan mode private
ip -net ${ns0} link set macvlan0 address ${MACVLAN_ADDR}
ip -net ${ns0} link set macvlan0 up
ip -net ${ns0} addr add 10.23.0.1/24 dev macvlan0

# add a macvlan on top of veth4.
MACVLAN_ADDR=ba:f3:13:37:42:24
ip -net ${ns0} link add link veth4 name macvlan4 type macvlan mode vepa
ip -net ${ns0} link set macvlan4 address ${MACVLAN_ADDR}
ip -net ${ns0} link set macvlan4 up

# make the macvlan part of the bridge.
# veth4 is not a bridge port, only the macvlan on top of it.
ip -net ${ns0} link set macvlan4 master br0

ip -net ${ns0} link set br0 up
ip -net ${ns0} addr add 10.0.0.1/24 dev br0
ip netns exec ${ns0} sysctl -q net.bridge.bridge-nf-call-iptables=1
ret=$?
if [ $ret -ne 0 ] ; then
	echo "SKIP: bridge netfilter not available"
	ret=$ksft_skip
fi

# for testing, so namespaces will reply to ping -b probes.
ip netns exec ${ns0} sysctl -q net.ipv4.icmp_echo_ignore_broadcasts=0

# enable conntrack in ns0 and drop broadcast packets in forward to
# avoid them from getting confirmed in the postrouting hook before
# the cloned skb is passed up the stack.
ip netns exec ${ns0} nft -f - <<EOF
table ip filter {
	chain input {
		type filter hook input priority 1; policy accept
		iifname br0 counter
		ct state new accept
	}
}

table bridge filter {
	chain forward {
		type filter hook forward priority 0; policy accept
		meta pkttype broadcast ip protocol icmp counter drop
	}
}
EOF

# place 1, 2 & 3 in same subnet, connected via ns0:br0.
# ns4 is placed in same subnet as well, but its not
# part of the bridge: the corresponding veth4 is not
# part of the bridge, only its macvlan interface.
for i in $(seq 1 4); do
  eval ip -net \$ns$i link set lo up
  eval ip -net \$ns$i link set eth0 up
done
for i in $(seq 1 2); do
  eval ip -net \$ns$i addr add 10.0.0.1$i/24 dev eth0
done

ip -net ${ns3} addr add 10.23.0.13/24 dev eth0
ip -net ${ns4} addr add 10.23.0.14/24 dev eth0

# test basic connectivity
do_ping ${ns1} 10.0.0.12
do_ping ${ns3} 10.23.0.1
do_ping ${ns4} 10.23.0.1

if [ $ret -eq 0 ];then
	echo "PASS: netns connectivity: ns1 can reach ns2, ns3 and ns4 can reach ns0"
fi

bcast_ping ${ns1} 10.0.0.255

# This should deliver broadcast to macvlan0, which is on top of ns0:br0.
bcast_ping ${ns3} 10.23.0.255

# same, this time via veth4:macvlan4.
bcast_ping ${ns4} 10.23.0.255

read t < /proc/sys/kernel/tainted

if [ $t -eq 0 ];then
	echo PASS: kernel not tainted
else
	echo ERROR: kernel is tainted
	ret=1
fi

exit $ret
