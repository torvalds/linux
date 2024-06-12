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

source lib.sh

if ! nft --version > /dev/null 2>&1;then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

cleanup() {
	cleanup_all_ns
}

trap cleanup EXIT

setup_ns nsclient1 nsclient2 nsrouter1 nsrouter2

ret=0

add_addr()
{
	ns=$1
	dev=$2
	i=$3

	ip -net "$ns" link set "$dev" up
	ip -net "$ns" addr add "192.168.$i.2/24" dev "$dev"
	ip -net "$ns" addr add "dead:$i::2/64" dev "$dev" nodad
}

check_counter()
{
	ns=$1
	name=$2
	expect=$3
	local lret=0

	if ! ip netns exec "$ns" nft list counter inet filter "$name" | grep -q "$expect"; then
		echo "ERROR: counter $name in $ns has unexpected value (expected $expect)" 1>&2
		ip netns exec "$ns" nft list counter inet filter "$name" 1>&2
		lret=1
	fi

	return $lret
}

check_unknown()
{
	expect="packets 0 bytes 0"
	for n in ${nsclient1} ${nsclient2} ${nsrouter1} ${nsrouter2}; do
		if ! check_counter "$n" "unknown" "$expect"; then
			return 1
		fi
	done

	return 0
}

DEV=veth0
ip link add "$DEV" netns "$nsclient1" type veth peer name eth1 netns "$nsrouter1"
ip link add "$DEV" netns "$nsclient2" type veth peer name eth1 netns "$nsrouter2"
ip link add "$DEV" netns "$nsrouter1" type veth peer name eth2 netns "$nsrouter2"

add_addr "$nsclient1" $DEV 1
add_addr "$nsclient2" $DEV 2

ip -net "$nsrouter1" link set eth1 up
ip -net "$nsrouter1" link set $DEV up

ip -net "$nsrouter2" link set eth1 mtu 1280 up
ip -net "$nsrouter2" link set eth2 up

ip -net "$nsclient1" route add default via 192.168.1.1
ip -net "$nsclient1" -6 route add default via dead:1::1

ip -net "$nsclient2" route add default via 192.168.2.1
ip -net "$nsclient2" route add default via dead:2::1
ip -net "$nsclient2" link set veth0 mtu 1280

ip -net "$nsrouter1" addr add 192.168.1.1/24 dev eth1
ip -net "$nsrouter1" addr add 192.168.3.1/24 dev veth0
ip -net "$nsrouter1" addr add dead:1::1/64 dev eth1 nodad
ip -net "$nsrouter1" addr add dead:3::1/64 dev veth0 nodad
ip -net "$nsrouter1" route add default via 192.168.3.10
ip -net "$nsrouter1" -6 route add default via dead:3::10

ip -net "$nsrouter2" addr add 192.168.2.1/24 dev eth1
ip -net "$nsrouter2" addr add 192.168.3.10/24 dev eth2
ip -net "$nsrouter2" addr add dead:2::1/64  dev eth1 nodad
ip -net "$nsrouter2" addr add dead:3::10/64 dev eth2 nodad
ip -net "$nsrouter2" route add default via 192.168.3.1
ip -net "$nsrouter2" route add default via dead:3::1

for i in 4 6; do
	ip netns exec "$nsrouter1" sysctl -q net.ipv$i.conf.all.forwarding=1
	ip netns exec "$nsrouter2" sysctl -q net.ipv$i.conf.all.forwarding=1
done

for netns in "$nsrouter1" "$nsrouter2"; do
ip netns exec "$netns" nft -f - <<EOF
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

ip netns exec "$nsclient1" nft -f - <<EOF
table inet filter {
	counter unknown { }
	counter related { }
	counter redir4 { }
	counter redir6 { }
	chain input {
		type filter hook input priority 0; policy accept;

		icmp type "redirect" ct state "related" counter name "redir4" accept
		icmpv6 type "nd-redirect" ct state "related" counter name "redir6" accept

		meta l4proto { icmp, icmpv6 } ct state established,untracked accept
		meta l4proto { icmp, icmpv6 } ct state "related" counter name "related" accept

		counter name "unknown" drop
	}
}
EOF

ip netns exec "$nsclient2" nft -f - <<EOF
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
ip netns exec "$nsrouter1" nft -f - <<EOF
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

if ! ip netns exec "$nsclient1" ping -c 1 -s 1000 -q -M "do" 192.168.2.2 >/dev/null; then
	echo "ERROR: netns ip routing/connectivity broken" 1>&2
	exit 1
fi
if ! ip netns exec "$nsclient1" ping -c 1 -s 1000 -q dead:2::2 >/dev/null; then
	echo "ERROR: netns ipv6 routing/connectivity broken" 1>&2
	exit 1
fi

if ! check_unknown; then
	ret=1
fi

expect="packets 0 bytes 0"
for netns in "$nsrouter1" "$nsrouter2" "$nsclient1";do
	if ! check_counter "$netns" "related" "$expect"; then
		ret=1
	fi
done

expect="packets 2 bytes 2076"
if ! check_counter "$nsclient2" "new" "$expect"; then
	ret=1
fi

if ip netns exec "$nsclient1" ping -W 0.5 -q -c 1 -s 1300 -M "do" 192.168.2.2 > /dev/null; then
	echo "ERROR: ping should have failed with PMTU too big error" 1>&2
	ret=1
fi

# nsrouter2 should have generated the icmp error, so
# related counter should be 0 (its in forward).
expect="packets 0 bytes 0"
if ! check_counter "$nsrouter2" "related" "$expect"; then
	ret=1
fi

# but nsrouter1 should have seen it, same for nsclient1.
expect="packets 1 bytes 576"
for netns in ${nsrouter1} ${nsclient1};do
	if ! check_counter "$netns" "related" "$expect"; then
		ret=1
	fi
done

if ip netns exec "${nsclient1}" ping6 -W 0.5 -c 1 -s 1300 dead:2::2 > /dev/null; then
	echo "ERROR: ping6 should have failed with PMTU too big error" 1>&2
	ret=1
fi

expect="packets 2 bytes 1856"
for netns in "${nsrouter1}" "${nsclient1}";do
	if ! check_counter "$netns" "related" "$expect"; then
		ret=1
	fi
done

if [ $ret -eq 0 ];then
	echo "PASS: icmp mtu error had RELATED state"
else
	echo "ERROR: icmp error RELATED state test has failed"
fi

# add 'bad' route,  expect icmp REDIRECT to be generated
ip netns exec "${nsclient1}" ip route add 192.168.1.42 via 192.168.1.1
ip netns exec "${nsclient1}" ip route add dead:1::42 via dead:1::1

ip netns exec "$nsclient1" ping -W 1 -q -i 0.5 -c 2 192.168.1.42 > /dev/null

expect="packets 1 bytes 112"
if ! check_counter "$nsclient1" "redir4" "$expect"; then
	ret=1
fi

ip netns exec "$nsclient1" ping -W 1 -c 1 dead:1::42 > /dev/null
expect="packets 1 bytes 192"
if ! check_counter "$nsclient1" "redir6" "$expect"; then
	ret=1
fi

if [ $ret -eq 0 ];then
	echo "PASS: icmp redirects had RELATED state"
else
	echo "ERROR: icmp redirect RELATED state test has failed"
fi

exit $ret
