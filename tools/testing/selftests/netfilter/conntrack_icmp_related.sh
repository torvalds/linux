#!/bin/bash
#
# check that ICMP df-needed/pkttoobig icmp are set are set as related
# state
#
# Setup is:
#
# nsclient1 -> nsrouter1 -> nsrouter2 -> nsclient2
# MTU 1500, except for nsrouter2 <-> nsclient2 link (1280).
# ping nsclient2 from nsclient1, checking that conntrack did set RELATED
# 'fragmentation needed' icmp packet.
#
# In addition, nsrouter1 will perform IP masquerading, i.e. also
# check the icmp errors are propagated to the correct host as per
# nat of "established" icmp-echo "connection".

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

nft --version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

cleanup() {
	for i in 1 2;do ip netns del nsclient$i;done
	for i in 1 2;do ip netns del nsrouter$i;done
}

ipv4() {
    echo -n 192.168.$1.2
}

ipv6 () {
    echo -n dead:$1::2
}

check_counter()
{
	ns=$1
	name=$2
	expect=$3
	local lret=0

	cnt=$(ip netns exec $ns nft list counter inet filter "$name" | grep -q "$expect")
	if [ $? -ne 0 ]; then
		echo "ERROR: counter $name in $ns has unexpected value (expected $expect)" 1>&2
		ip netns exec $ns nft list counter inet filter "$name" 1>&2
		lret=1
	fi

	return $lret
}

check_unknown()
{
	expect="packets 0 bytes 0"
	for n in nsclient1 nsclient2 nsrouter1 nsrouter2; do
		check_counter $n "unknown" "$expect"
		if [ $? -ne 0 ] ;then
			return 1
		fi
	done

	return 0
}

for n in nsclient1 nsclient2 nsrouter1 nsrouter2; do
  ip netns add $n
  ip -net $n link set lo up
done

DEV=veth0
ip link add $DEV netns nsclient1 type veth peer name eth1 netns nsrouter1
DEV=veth0
ip link add $DEV netns nsclient2 type veth peer name eth1 netns nsrouter2

DEV=veth0
ip link add $DEV netns nsrouter1 type veth peer name eth2 netns nsrouter2

DEV=veth0
for i in 1 2; do
    ip -net nsclient$i link set $DEV up
    ip -net nsclient$i addr add $(ipv4 $i)/24 dev $DEV
    ip -net nsclient$i addr add $(ipv6 $i)/64 dev $DEV
done

ip -net nsrouter1 link set eth1 up
ip -net nsrouter1 link set veth0 up

ip -net nsrouter2 link set eth1 up
ip -net nsrouter2 link set eth2 up

ip -net nsclient1 route add default via 192.168.1.1
ip -net nsclient1 -6 route add default via dead:1::1

ip -net nsclient2 route add default via 192.168.2.1
ip -net nsclient2 route add default via dead:2::1

i=3
ip -net nsrouter1 addr add 192.168.1.1/24 dev eth1
ip -net nsrouter1 addr add 192.168.3.1/24 dev veth0
ip -net nsrouter1 addr add dead:1::1/64 dev eth1
ip -net nsrouter1 addr add dead:3::1/64 dev veth0
ip -net nsrouter1 route add default via 192.168.3.10
ip -net nsrouter1 -6 route add default via dead:3::10

ip -net nsrouter2 addr add 192.168.2.1/24 dev eth1
ip -net nsrouter2 addr add 192.168.3.10/24 dev eth2
ip -net nsrouter2 addr add dead:2::1/64 dev eth1
ip -net nsrouter2 addr add dead:3::10/64 dev eth2
ip -net nsrouter2 route add default via 192.168.3.1
ip -net nsrouter2 route add default via dead:3::1

sleep 2
for i in 4 6; do
	ip netns exec nsrouter1 sysctl -q net.ipv$i.conf.all.forwarding=1
	ip netns exec nsrouter2 sysctl -q net.ipv$i.conf.all.forwarding=1
done

for netns in nsrouter1 nsrouter2; do
ip netns exec $netns nft -f - <<EOF
table inet filter {
	counter unknown { }
	counter related { }
	chain forward {
		type filter hook forward priority 0; policy accept;
		meta l4proto icmpv6 icmpv6 type "packet-too-big" ct state "related" counter name "related" accept
		meta l4proto icmp icmp type "destination-unreachable" ct state "related" counter name "related" accept
		meta l4proto { icmp, icmpv6 } ct state new,established accept
		counter name "unknown" drop
	}
}
EOF
done

ip netns exec nsclient1 nft -f - <<EOF
table inet filter {
	counter unknown { }
	counter related { }
	chain input {
		type filter hook input priority 0; policy accept;
		meta l4proto { icmp, icmpv6 } ct state established,untracked accept

		meta l4proto { icmp, icmpv6 } ct state "related" counter name "related" accept
		counter name "unknown" drop
	}
}
EOF

ip netns exec nsclient2 nft -f - <<EOF
table inet filter {
	counter unknown { }
	counter new { }
	counter established { }

	chain input {
		type filter hook input priority 0; policy accept;
		meta l4proto { icmp, icmpv6 } ct state established,untracked accept

		meta l4proto { icmp, icmpv6 } ct state "new" counter name "new" accept
		meta l4proto { icmp, icmpv6 } ct state "established" counter name "established" accept
		counter name "unknown" drop
	}
	chain output {
		type filter hook output priority 0; policy accept;
		meta l4proto { icmp, icmpv6 } ct state established,untracked accept

		meta l4proto { icmp, icmpv6 } ct state "new" counter name "new"
		meta l4proto { icmp, icmpv6 } ct state "established" counter name "established"
		counter name "unknown" drop
	}
}
EOF


# make sure NAT core rewrites adress of icmp error if nat is used according to
# conntrack nat information (icmp error will be directed at nsrouter1 address,
# but it needs to be routed to nsclient1 address).
ip netns exec nsrouter1 nft -f - <<EOF
table ip nat {
	chain postrouting {
		type nat hook postrouting priority 0; policy accept;
		ip protocol icmp oifname "veth0" counter masquerade
	}
}
table ip6 nat {
	chain postrouting {
		type nat hook postrouting priority 0; policy accept;
		ip6 nexthdr icmpv6 oifname "veth0" counter masquerade
	}
}
EOF

ip netns exec nsrouter2 ip link set eth1  mtu 1280
ip netns exec nsclient2 ip link set veth0 mtu 1280
sleep 1

ip netns exec nsclient1 ping -c 1 -s 1000 -q -M do 192.168.2.2 >/dev/null
if [ $? -ne 0 ]; then
	echo "ERROR: netns ip routing/connectivity broken" 1>&2
	cleanup
	exit 1
fi
ip netns exec nsclient1 ping6 -q -c 1 -s 1000 dead:2::2 >/dev/null
if [ $? -ne 0 ]; then
	echo "ERROR: netns ipv6 routing/connectivity broken" 1>&2
	cleanup
	exit 1
fi

check_unknown
if [ $? -ne 0 ]; then
	ret=1
fi

expect="packets 0 bytes 0"
for netns in nsrouter1 nsrouter2 nsclient1;do
	check_counter "$netns" "related" "$expect"
	if [ $? -ne 0 ]; then
		ret=1
	fi
done

expect="packets 2 bytes 2076"
check_counter nsclient2 "new" "$expect"
if [ $? -ne 0 ]; then
	ret=1
fi

ip netns exec nsclient1 ping -q -c 1 -s 1300 -M do 192.168.2.2 > /dev/null
if [ $? -eq 0 ]; then
	echo "ERROR: ping should have failed with PMTU too big error" 1>&2
	ret=1
fi

# nsrouter2 should have generated the icmp error, so
# related counter should be 0 (its in forward).
expect="packets 0 bytes 0"
check_counter "nsrouter2" "related" "$expect"
if [ $? -ne 0 ]; then
	ret=1
fi

# but nsrouter1 should have seen it, same for nsclient1.
expect="packets 1 bytes 576"
for netns in nsrouter1 nsclient1;do
	check_counter "$netns" "related" "$expect"
	if [ $? -ne 0 ]; then
		ret=1
	fi
done

ip netns exec nsclient1 ping6 -c 1 -s 1300 dead:2::2 > /dev/null
if [ $? -eq 0 ]; then
	echo "ERROR: ping6 should have failed with PMTU too big error" 1>&2
	ret=1
fi

expect="packets 2 bytes 1856"
for netns in nsrouter1 nsclient1;do
	check_counter "$netns" "related" "$expect"
	if [ $? -ne 0 ]; then
		ret=1
	fi
done

if [ $ret -eq 0 ];then
	echo "PASS: icmp mtu error had RELATED state"
else
	echo "ERROR: icmp error RELATED state test has failed"
fi

cleanup
exit $ret
