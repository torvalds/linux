#!/bin/bash

# This script demonstrates interaction of conntrack and vrf.
# The vrf driver calls the netfilter hooks again, with oif/iif
# pointing at the VRF device.
#
# For ingress, this means first iteration has iifname of lower/real
# device.  In this script, thats veth0.
# Second iteration is iifname set to vrf device, tvrf in this script.
#
# For egress, this is reversed: first iteration has the vrf device,
# second iteration is done with the lower/real/veth0 device.
#
# test_ct_zone_in demonstrates unexpected change of nftables
# behavior # caused by commit 09e856d54bda5f28 "vrf: Reset skb conntrack
# connection on VRF rcv"
#
# It was possible to assign conntrack zone to a packet (or mark it for
# `notracking`) in the prerouting chain before conntrack, based on real iif.
#
# After the change, the zone assignment is lost and the zone is assigned based
# on the VRF master interface (in case such a rule exists).
# assignment is lost. Instead, assignment based on the `iif` matching
# Thus it is impossible to distinguish packets based on the original
# interface.
#
# test_masquerade_vrf and test_masquerade_veth0 demonstrate the problem
# that was supposed to be fixed by the commit mentioned above to make sure
# that any fix to test case 1 won't break masquerade again.

source lib.sh

IP0=172.30.30.1
IP1=172.30.30.2
PFXL=30
ret=0

cleanup()
{
	ip netns pids $ns0 | xargs kill 2>/dev/null
	ip netns pids $ns1 | xargs kill 2>/dev/null

	cleanup_all_ns
}

checktool "nft --version" "run test without nft"
checktool "conntrack --version" "run test without conntrack"
checktool "socat -h" "run test without socat"

trap cleanup EXIT

setup_ns ns0 ns1

ip netns exec "$ns0" sysctl -q -w net.ipv4.conf.default.rp_filter=0
ip netns exec "$ns0" sysctl -q -w net.ipv4.conf.all.rp_filter=0
ip netns exec "$ns0" sysctl -q -w net.ipv4.conf.all.rp_filter=0

if ! ip link add veth0 netns "$ns0" type veth peer name veth0 netns "$ns1" > /dev/null 2>&1; then
	echo "SKIP: Could not add veth device"
	exit $ksft_skip
fi

if ! ip -net "$ns0" li add tvrf type vrf table 9876; then
	echo "SKIP: Could not add vrf device"
	exit $ksft_skip
fi

ip -net "$ns0" li set veth0 master tvrf
ip -net "$ns0" li set tvrf up
ip -net "$ns0" li set veth0 up
ip -net "$ns1" li set veth0 up

ip -net "$ns0" addr add $IP0/$PFXL dev veth0
ip -net "$ns1" addr add $IP1/$PFXL dev veth0

listener_ready()
{
        local ns="$1"

        ss -N "$ns" -l -n -t -o "sport = :55555" | grep -q "55555"
}

ip netns exec "$ns1" socat -u -4 TCP-LISTEN:55555,reuseaddr,fork STDOUT > /dev/null &
busywait $BUSYWAIT_TIMEOUT listener_ready "$ns1"

# test vrf ingress handling.
# The incoming connection should be placed in conntrack zone 1,
# as decided by the first iteration of the ruleset.
test_ct_zone_in()
{
ip netns exec "$ns0" nft -f - <<EOF
table testct {
	chain rawpre {
		type filter hook prerouting priority raw;

		iif { veth0, tvrf } counter meta nftrace set 1
		iif veth0 counter ct zone set 1 counter return
		iif tvrf counter ct zone set 2 counter return
		ip protocol icmp counter
		notrack counter
	}

	chain rawout {
		type filter hook output priority raw;

		oif veth0 counter ct zone set 1 counter return
		oif tvrf counter ct zone set 2 counter return
		notrack counter
	}
}
EOF
	ip netns exec "$ns1" ping -W 1 -c 1 -I veth0 "$IP0" > /dev/null

	# should be in zone 1, not zone 2
	count=$(ip netns exec "$ns0" conntrack -L -s $IP1 -d $IP0 -p icmp --zone 1 2>/dev/null | wc -l)
	if [ "$count" -eq 1 ]; then
		echo "PASS: entry found in conntrack zone 1"
	else
		echo "FAIL: entry not found in conntrack zone 1"
		count=$(ip netns exec "$ns0" conntrack -L -s $IP1 -d $IP0 -p icmp --zone 2 2> /dev/null | wc -l)
		if [ "$count" -eq 1 ]; then
			echo "FAIL: entry found in zone 2 instead"
		else
			echo "FAIL: entry not in zone 1 or 2, dumping table"
			ip netns exec "$ns0" conntrack -L
			ip netns exec "$ns0" nft list ruleset
		fi
	fi
}

# add masq rule that gets evaluated w. outif set to vrf device.
# This tests the first iteration of the packet through conntrack,
# oifname is the vrf device.
test_masquerade_vrf()
{
	local qdisc=$1

	if [ "$qdisc" != "default" ]; then
		tc -net "$ns0" qdisc add dev tvrf root "$qdisc"
	fi

	ip netns exec "$ns0" conntrack -F 2>/dev/null

ip netns exec "$ns0" nft -f - <<EOF
flush ruleset
table ip nat {
	chain rawout {
		type filter hook output priority raw;

		oif tvrf ct state untracked counter
	}
	chain postrouting2 {
		type filter hook postrouting priority mangle;

		oif tvrf ct state untracked counter
	}
	chain postrouting {
		type nat hook postrouting priority 0;
		# NB: masquerade should always be combined with 'oif(name) bla',
		# lack of this is intentional here, we want to exercise double-snat.
		ip saddr 172.30.30.0/30 counter masquerade random
	}
}
EOF
	if ! ip netns exec "$ns0" ip vrf exec tvrf socat -u -4 STDIN TCP:"$IP1":55555 < /dev/null > /dev/null;then
		echo "FAIL: connect failure with masquerade + sport rewrite on vrf device"
		ret=1
		return
	fi

	# must also check that nat table was evaluated on second (lower device) iteration.
	if ip netns exec "$ns0" nft list table ip nat |grep -q 'counter packets 1' &&
	   ip netns exec "$ns0" nft list table ip nat |grep -q 'untracked counter packets [1-9]'; then
		echo "PASS: connect with masquerade + sport rewrite on vrf device ($qdisc qdisc)"
	else
		echo "FAIL: vrf rules have unexpected counter value"
		ret=1
	fi

	if [ "$qdisc" != "default" ]; then
		tc -net "$ns0" qdisc del dev tvrf root
	fi
}

# add masq rule that gets evaluated w. outif set to veth device.
# This tests the 2nd iteration of the packet through conntrack,
# oifname is the lower device (veth0 in this case).
test_masquerade_veth()
{
	ip netns exec "$ns0" conntrack -F 2>/dev/null
ip netns exec "$ns0" nft -f - <<EOF
flush ruleset
table ip nat {
	chain postrouting {
		type nat hook postrouting priority 0;
		meta oif veth0 ip saddr 172.30.30.0/30 counter masquerade random
	}
}
EOF
	if ! ip netns exec "$ns0" ip vrf exec tvrf socat -u -4 STDIN TCP:"$IP1":55555 < /dev/null > /dev/null;then
		echo "FAIL: connect failure with masquerade + sport rewrite on veth device"
		ret=1
		return
	fi

	# must also check that nat table was evaluated on second (lower device) iteration.
	if ip netns exec "$ns0" nft list table ip nat |grep -q 'counter packets 1'; then
		echo "PASS: connect with masquerade + sport rewrite on veth device"
	else
		echo "FAIL: vrf masq rule has unexpected counter value"
		ret=1
	fi
}

test_ct_zone_in
test_masquerade_vrf "default"
test_masquerade_vrf "pfifo"
test_masquerade_veth

exit $ret
