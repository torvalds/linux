#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# return code to signal skipped test
ksft_skip=4

# search for legacy iptables (it uses the xtables extensions
if iptables-legacy --version >/dev/null 2>&1; then
	iptables='iptables-legacy'
elif iptables --version >/dev/null 2>&1; then
	iptables='iptables'
else
	iptables=''
fi

if ip6tables-legacy --version >/dev/null 2>&1; then
	ip6tables='ip6tables-legacy'
elif ip6tables --version >/dev/null 2>&1; then
	ip6tables='ip6tables'
else
	ip6tables=''
fi

if nft --version >/dev/null 2>&1; then
	nft='nft'
else
	nft=''
fi

if [ -z "$iptables$ip6tables$nft" ]; then
	echo "SKIP: Test needs iptables, ip6tables or nft"
	exit $ksft_skip
fi

sfx=$(mktemp -u "XXXXXXXX")
ns1="ns1-$sfx"
ns2="ns2-$sfx"
trap "ip netns del $ns1; ip netns del $ns2" EXIT

# create two netns, disable rp_filter in ns2 and
# keep IPv6 address when moving into VRF
ip netns add "$ns1"
ip netns add "$ns2"
ip netns exec "$ns2" sysctl -q net.ipv4.conf.all.rp_filter=0
ip netns exec "$ns2" sysctl -q net.ipv4.conf.default.rp_filter=0
ip netns exec "$ns2" sysctl -q net.ipv6.conf.all.keep_addr_on_down=1

# a standard connection between the netns, should not trigger rp filter
ip -net "$ns1" link add v0 type veth peer name v0 netns "$ns2"
ip -net "$ns1" link set v0 up; ip -net "$ns2" link set v0 up
ip -net "$ns1" a a 192.168.23.2/24 dev v0
ip -net "$ns2" a a 192.168.23.1/24 dev v0
ip -net "$ns1" a a fec0:23::2/64 dev v0 nodad
ip -net "$ns2" a a fec0:23::1/64 dev v0 nodad

# rp filter testing: ns1 sends packets via v0 which ns2 would route back via d0
ip -net "$ns2" link add d0 type dummy
ip -net "$ns2" link set d0 up
ip -net "$ns1" a a 192.168.42.2/24 dev v0
ip -net "$ns2" a a 192.168.42.1/24 dev d0
ip -net "$ns1" a a fec0:42::2/64 dev v0 nodad
ip -net "$ns2" a a fec0:42::1/64 dev d0 nodad

# avoid neighbor lookups and enable martian IPv6 pings
ns2_hwaddr=$(ip -net "$ns2" link show dev v0 | \
	     sed -n 's, *link/ether \([^ ]*\) .*,\1,p')
ns1_hwaddr=$(ip -net "$ns1" link show dev v0 | \
	     sed -n 's, *link/ether \([^ ]*\) .*,\1,p')
ip -net "$ns1" neigh add fec0:42::1 lladdr "$ns2_hwaddr" nud permanent dev v0
ip -net "$ns1" neigh add fec0:23::1 lladdr "$ns2_hwaddr" nud permanent dev v0
ip -net "$ns2" neigh add fec0:42::2 lladdr "$ns1_hwaddr" nud permanent dev d0
ip -net "$ns2" neigh add fec0:23::2 lladdr "$ns1_hwaddr" nud permanent dev v0

# firewall matches to test
[ -n "$iptables" ] && {
	common='-t raw -A PREROUTING -s 192.168.0.0/16'
	common+=' -p icmp --icmp-type echo-request'
	if ! ip netns exec "$ns2" "$iptables" $common -m rpfilter;then
		echo "Cannot add rpfilter rule"
		exit $ksft_skip
	fi
	ip netns exec "$ns2" "$iptables" $common -m rpfilter --invert
}
[ -n "$ip6tables" ] && {
	common='-t raw -A PREROUTING -s fec0::/16'
	common+=' -p icmpv6 --icmpv6-type echo-request'
	if ! ip netns exec "$ns2" "$ip6tables" $common -m rpfilter;then
		echo "Cannot add rpfilter rule"
		exit $ksft_skip
	fi
	ip netns exec "$ns2" "$ip6tables" $common -m rpfilter --invert
}
[ -n "$nft" ] && ip netns exec "$ns2" $nft -f - <<EOF
table inet t {
	chain c {
		type filter hook prerouting priority raw;
		ip saddr 192.168.0.0/16 icmp type echo-request \
			fib saddr . iif oif exists counter
		ip6 saddr fec0::/16 icmpv6 type echo-request \
			fib saddr . iif oif exists counter
	}
}
EOF

die() {
	echo "FAIL: $*"
	#ip netns exec "$ns2" "$iptables" -t raw -vS
	#ip netns exec "$ns2" "$ip6tables" -t raw -vS
	#ip netns exec "$ns2" nft list ruleset
	exit 1
}

# check rule counters, return true if rule did not match
ipt_zero_rule() { # (command)
	[ -n "$1" ] || return 0
	ip netns exec "$ns2" "$1" -t raw -vS | grep -q -- "-m rpfilter -c 0 0"
}
ipt_zero_reverse_rule() { # (command)
	[ -n "$1" ] || return 0
	ip netns exec "$ns2" "$1" -t raw -vS | \
		grep -q -- "-m rpfilter --invert -c 0 0"
}
nft_zero_rule() { # (family)
	[ -n "$nft" ] || return 0
	ip netns exec "$ns2" "$nft" list chain inet t c | \
		grep -q "$1 saddr .* counter packets 0 bytes 0"
}

netns_ping() { # (netns, args...)
	local netns="$1"
	shift
	ip netns exec "$netns" ping -q -c 1 -W 1 "$@" >/dev/null
}

clear_counters() {
	[ -n "$iptables" ] && ip netns exec "$ns2" "$iptables" -t raw -Z
	[ -n "$ip6tables" ] && ip netns exec "$ns2" "$ip6tables" -t raw -Z
	if [ -n "$nft" ]; then
		(
			echo "delete table inet t";
			ip netns exec "$ns2" $nft -s list table inet t;
		) | ip netns exec "$ns2" $nft -f -
	fi
}

testrun() {
	clear_counters

	# test 1: martian traffic should fail rpfilter matches
	netns_ping "$ns1" -I v0 192.168.42.1 && \
		die "martian ping 192.168.42.1 succeeded"
	netns_ping "$ns1" -I v0 fec0:42::1 && \
		die "martian ping fec0:42::1 succeeded"

	ipt_zero_rule "$iptables" || die "iptables matched martian"
	ipt_zero_rule "$ip6tables" || die "ip6tables matched martian"
	ipt_zero_reverse_rule "$iptables" && die "iptables not matched martian"
	ipt_zero_reverse_rule "$ip6tables" && die "ip6tables not matched martian"
	nft_zero_rule ip || die "nft IPv4 matched martian"
	nft_zero_rule ip6 || die "nft IPv6 matched martian"

	clear_counters

	# test 2: rpfilter match should pass for regular traffic
	netns_ping "$ns1" 192.168.23.1 || \
		die "regular ping 192.168.23.1 failed"
	netns_ping "$ns1" fec0:23::1 || \
		die "regular ping fec0:23::1 failed"

	ipt_zero_rule "$iptables" && die "iptables match not effective"
	ipt_zero_rule "$ip6tables" && die "ip6tables match not effective"
	ipt_zero_reverse_rule "$iptables" || die "iptables match over-effective"
	ipt_zero_reverse_rule "$ip6tables" || die "ip6tables match over-effective"
	nft_zero_rule ip && die "nft IPv4 match not effective"
	nft_zero_rule ip6 && die "nft IPv6 match not effective"

}

testrun

# repeat test with vrf device in $ns2
ip -net "$ns2" link add vrf0 type vrf table 10
ip -net "$ns2" link set vrf0 up
ip -net "$ns2" link set v0 master vrf0

testrun

echo "PASS: netfilter reverse path match works as intended"
exit 0
