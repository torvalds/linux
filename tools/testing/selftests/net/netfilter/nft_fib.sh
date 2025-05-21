#!/bin/bash
#
# This tests the fib expression.
#
# Kselftest framework requirement - SKIP code is 4.
#
#  10.0.1.99     10.0.1.1           10.0.2.1         10.0.2.99
# dead:1::99    dead:1::1          dead:2::1        dead:2::99
# ns1 <-------> [ veth0 ] nsrouter [veth1] <-------> ns2

source lib.sh

ret=0

timeout=4

log_netns=$(sysctl -n net.netfilter.nf_log_all_netns)

cleanup()
{
	cleanup_all_ns

	[ "$log_netns" -eq 0 ] && sysctl -q net.netfilter.nf_log_all_netns=$log_netns
}

checktool "nft --version" "run test without nft"

setup_ns nsrouter ns1 ns2

trap cleanup EXIT

if dmesg | grep -q ' nft_rpfilter: ';then
	dmesg -c | grep ' nft_rpfilter: '
	echo "WARN: a previous test run has failed" 1>&2
fi

sysctl -q net.netfilter.nf_log_all_netns=1

load_ruleset() {
	local netns=$1

ip netns exec "$netns" nft -f /dev/stdin <<EOF
table inet filter {
	chain prerouting {
		type filter hook prerouting priority 0; policy accept;
	        fib saddr . iif oif missing counter log prefix "$netns nft_rpfilter: " drop
	}
}
EOF
}

load_input_ruleset() {
	local netns=$1

ip netns exec "$netns" nft -f /dev/stdin <<EOF
table inet filter {
	chain input {
		type filter hook input priority 0; policy accept;
	        fib saddr . iif oif missing counter log prefix "$netns nft_rpfilter: " drop
	}
}
EOF
}

load_pbr_ruleset() {
	local netns=$1

ip netns exec "$netns" nft -f /dev/stdin <<EOF
table inet filter {
	chain forward {
		type filter hook forward priority raw;
		fib saddr . iif oif gt 0 accept
		log drop
	}
}
EOF
}

load_type_ruleset() {
	local netns=$1

	for family in ip ip6;do
ip netns exec "$netns" nft -f /dev/stdin <<EOF
table $family filter {
	chain type_match_in {
		fib daddr type local counter comment "daddr configured on other iface"
		fib daddr . iif type local counter comment "daddr configured on iif"
		fib daddr type unicast counter comment "daddr not local"
		fib daddr . iif type unicast counter comment "daddr not configured on iif"
	}

	chain type_match_out {
		fib daddr type unicast counter
		fib daddr . oif type unicast counter
		fib daddr type local counter
		fib daddr . oif type local counter
	}

	chain prerouting {
		type filter hook prerouting priority 0;
		icmp type echo-request counter jump type_match_in
		icmpv6 type echo-request counter jump type_match_in
	}

	chain input {
		type filter hook input priority 0;
		icmp type echo-request counter jump type_match_in
		icmpv6 type echo-request counter jump type_match_in
	}

	chain forward {
		type filter hook forward priority 0;
		icmp type echo-request counter jump type_match_in
		icmpv6 type echo-request counter jump type_match_in
	}

	chain output {
		type filter hook output priority 0;
		icmp type echo-request counter jump type_match_out
		icmpv6 type echo-request counter jump type_match_out
	}

	chain postrouting {
		type filter hook postrouting priority 0;
		icmp type echo-request counter jump type_match_out
		icmpv6 type echo-request counter jump type_match_out
	}
}
EOF
done
}

reload_type_ruleset() {
	ip netns exec "$1" nft flush table ip filter
	ip netns exec "$1" nft flush table ip6 filter
	load_type_ruleset "$1"
}

check_fib_type_counter_family() {
	local family="$1"
	local want="$2"
	local ns="$3"
	local chain="$4"
	local what="$5"
	local errmsg="$6"

	if ! ip netns exec "$ns" nft list chain "$family" filter "$chain" | grep "$what" | grep -q "packets $want";then
		echo "Netns $ns $family fib type counter doesn't match expected packet count of $want for $what $errmsg" 1>&2
		ip netns exec "$ns" nft list chain "$family" filter "$chain"
		ret=1
		return 1
	fi

	return 0
}

check_fib_type_counter() {
	check_fib_type_counter_family "ip" "$@" || return 1
	check_fib_type_counter_family "ip6" "$@" || return 1
}

load_ruleset_count() {
	local netns=$1

ip netns exec "$netns" nft -f /dev/stdin <<EOF
table inet filter {
	chain prerouting {
		type filter hook prerouting priority 0; policy accept;
		ip daddr 1.1.1.1 fib saddr . iif oif missing counter drop
		ip6 daddr 1c3::c01d fib saddr . iif oif missing counter drop
	}
}
EOF
}

check_drops() {
	if dmesg | grep -q ' nft_rpfilter: ';then
		dmesg | grep ' nft_rpfilter: '
		echo "FAIL: rpfilter did drop packets"
		ret=1
		return 1
	fi

	return 0
}

check_fib_counter() {
	local want=$1
	local ns=$2
	local address=$3

	if ! ip netns exec "$ns" nft list table inet filter | grep 'fib saddr . iif' | grep "$address" | grep -q "packets $want";then
		echo "Netns $ns fib counter doesn't match expected packet count of $want for $address" 1>&2
		ip netns exec "$ns" nft list table inet filter
		return 1
	fi

	if [ "$want" -gt 0 ]; then
		echo "PASS: fib expression did drop packets for $address"
	fi

	return 0
}

load_ruleset "$nsrouter"
load_ruleset "$ns1"
load_ruleset "$ns2"

if ! ip link add veth0 netns "$nsrouter" type veth peer name eth0 netns "$ns1" > /dev/null 2>&1; then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi
ip link add veth1 netns "$nsrouter" type veth peer name eth0 netns "$ns2"

ip -net "$nsrouter" link set veth0 up
ip -net "$nsrouter" addr add 10.0.1.1/24 dev veth0
ip -net "$nsrouter" addr add dead:1::1/64 dev veth0 nodad

ip -net "$nsrouter" link set veth1 up
ip -net "$nsrouter" addr add 10.0.2.1/24 dev veth1
ip -net "$nsrouter" addr add dead:2::1/64 dev veth1 nodad

ip -net "$ns1" link set eth0 up
ip -net "$ns2" link set eth0 up

ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns1" addr add dead:1::99/64 dev eth0 nodad
ip -net "$ns1" route add default via 10.0.1.1
ip -net "$ns1" route add default via dead:1::1

ip -net "$ns2" addr add 10.0.2.99/24 dev eth0
ip -net "$ns2" addr add dead:2::99/64 dev eth0 nodad
ip -net "$ns2" route add default via 10.0.2.1
ip -net "$ns2" route add default via dead:2::1

test_ping() {
  local daddr4=$1
  local daddr6=$2

  if ! ip netns exec "$ns1" ping -c 1 -q "$daddr4" > /dev/null; then
	check_drops
	echo "FAIL: ${ns1} cannot reach $daddr4, ret $ret" 1>&2
	return 1
  fi

  if ! ip netns exec "$ns1" ping -c 1 -q "$daddr6" > /dev/null; then
	check_drops
	echo "FAIL: ${ns1} cannot reach $daddr6, ret $ret" 1>&2
	return 1
  fi

  return 0
}

test_ping_unreachable() {
  local daddr4=$1
  local daddr6=$2

  if ip netns exec "$ns1" ping -c 1 -w 1 -q "$daddr4" > /dev/null; then
	echo "FAIL: ${ns1} could reach $daddr4" 1>&2
	return 1
  fi

  if ip netns exec "$ns1" ping -c 1 -w 1 -q "$daddr6" > /dev/null; then
	echo "FAIL: ${ns1} could reach $daddr6" 1>&2
	return 1
  fi

  return 0
}

test_fib_type() {
	local notice="$1"
	local errmsg="addr-on-if"
	local lret=0

	if ! load_type_ruleset "$nsrouter";then
		echo "SKIP: Could not load fib type ruleset"
		[ $ret -eq 0 ] && ret=$ksft_skip
		return
	fi

	# makes router receive packet for addresses configured on incoming
	# interface.
	test_ping 10.0.1.1 dead:1::1 || return 1

	# expectation: triggers all 'local' in prerouting/input.
	check_fib_type_counter 2 "$nsrouter" "type_match_in" "fib daddr type local" "$errmsg" || lret=1
	check_fib_type_counter 2 "$nsrouter" "type_match_in" "fib daddr . iif type local" "$errmsg" || lret=1

	reload_type_ruleset "$nsrouter"
	# makes router receive packet for address configured on a different (but local)
	# interface.
	test_ping 10.0.2.1 dead:2::1 || return 1

	# expectation: triggers 'unicast' in prerouting/input for daddr . iif and local for 'daddr'.
	errmsg="addr-on-host"
	check_fib_type_counter 2 "$nsrouter" "type_match_in" "fib daddr type local" "$errmsg" || lret=1
	check_fib_type_counter 2 "$nsrouter" "type_match_in" "fib daddr . iif type unicast" "$errmsg" || lret=1

	reload_type_ruleset "$nsrouter"
	test_ping 10.0.2.99 dead:2::99 || return 1
	errmsg="addr-on-otherhost"
	check_fib_type_counter 2 "$nsrouter" "type_match_in" "fib daddr type unicast" "$errmsg" || lret=1
	check_fib_type_counter 2 "$nsrouter" "type_match_in" "fib daddr . iif type unicast" "$errmsg" || lret=1

	if [ $lret -eq 0 ];then
		echo "PASS: fib expression address types match ($notice)"
	else
		echo "FAIL: fib expression address types match ($notice)"
		ret=1
	fi
}

test_fib_vrf_dev_add_dummy()
{
	if ! ip -net "$nsrouter" link add dummy0 type dummy ;then
		echo "SKIP: VRF tests: dummy device type not supported"
		return 1
	fi

	if ! ip -net "$nsrouter" link add tvrf type vrf table 9876;then
		echo "SKIP: VRF tests: vrf device type not supported"
		return 1
	fi

	ip -net "$nsrouter" link set veth0 master tvrf
	ip -net "$nsrouter" link set dummy0 master tvrf
	ip -net "$nsrouter" link set dummy0 up
	ip -net "$nsrouter" link set tvrf up
}

# Extends nsrouter config by adding dummy0+vrf.
#
#  10.0.1.99     10.0.1.1           10.0.2.1         10.0.2.99
# dead:1::99    dead:1::1          dead:2::1        dead:2::99
# ns1 <-------> [ veth0 ] nsrouter [veth1] <-------> ns2
#                         [dummy0]
#                         10.9.9.1
#                        dead:9::1
#                          [tvrf]
test_fib_vrf()
{
	local dummynet="10.9.9"
	local dummynet6="dead:9"
	local cntname=""

	if ! test_fib_vrf_dev_add_dummy; then
		[ $ret -eq 0 ] && ret=$ksft_skip
		return
	fi

	ip -net "$nsrouter" addr add "$dummynet.1"/24 dev dummy0
	ip -net "$nsrouter" addr add "${dummynet6}::1"/64 dev dummy0 nodad


ip netns exec "$nsrouter" nft -f - <<EOF
flush ruleset
table inet t {
	counter fibcount4 { }
	counter fibcount6 { }

	chain prerouting {
		type filter hook prerouting priority 0;
		meta iifname veth0 ip daddr ${dummynet}.2 fib daddr oif dummy0 counter name fibcount4
		meta iifname veth0 ip6 daddr ${dummynet6}::2 fib daddr oif dummy0 counter name fibcount6
	}
}
EOF
	# no echo reply for these addresses: The dummy interface is part of tvrf,
	test_ping_unreachable "$dummynet.2" "${dummynet6}::2" &

	wait

	for cntname in fibcount4 fibcount6;do
		if ip netns exec "$nsrouter" nft list counter inet t "$cntname" | grep -q "packets 1"; then
			echo "PASS: vrf fib lookup did return expected output interface for $cntname"
		else
			ip netns exec "$nsrouter" nft list counter inet t "$cntname"
			echo "FAIL: vrf fib lookup did not return expected output interface for $cntname"
			ret=1
		fi
	done
}

ip netns exec "$nsrouter" sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
ip netns exec "$nsrouter" sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null

test_ping 10.0.2.1 dead:2::1 || exit 1
check_drops

test_ping 10.0.2.99 dead:2::99 || exit 1
check_drops

[ $ret -eq 0 ] && echo "PASS: fib expression did not cause unwanted packet drops"

load_input_ruleset "$ns1"

test_ping 127.0.0.1 ::1
check_drops

test_ping 10.0.1.99 dead:1::99
check_drops

[ $ret -eq 0 ] && echo "PASS: fib expression did not discard loopback packets"

load_input_ruleset "$ns1"

test_ping 127.0.0.1 ::1 || exit 1
check_drops || exit 1

test_ping 10.0.1.99 dead:1::99 || exit 1
check_drops || exit 1

echo "PASS: fib expression did not discard loopback packets"

ip netns exec "$nsrouter" nft flush table inet filter

ip -net "$ns1" route del default
ip -net "$ns1" -6 route del default

ip -net "$ns1" addr del 10.0.1.99/24 dev eth0
ip -net "$ns1" addr del dead:1::99/64 dev eth0

ip -net "$ns1" addr add 10.0.2.99/24 dev eth0
ip -net "$ns1" addr add dead:2::99/64 dev eth0 nodad

ip -net "$ns1" route add default via 10.0.2.1
ip -net "$ns1" -6 route add default via dead:2::1

ip -net "$nsrouter" addr add dead:2::1/64 dev veth0 nodad

# switch to ruleset that doesn't log, this time
# its expected that this does drop the packets.
load_ruleset_count "$nsrouter"

# ns1 has a default route, but nsrouter does not.
# must not check return value, ping to 1.1.1.1 will
# fail.
check_fib_counter 0 "$nsrouter" 1.1.1.1 || exit 1
check_fib_counter 0 "$nsrouter" 1c3::c01d || exit 1

ip netns exec "$ns1" ping -W 0.5 -c 1 -q 1.1.1.1 > /dev/null
check_fib_counter 1 "$nsrouter" 1.1.1.1 || exit 1

ip netns exec "$ns1" ping -W 0.5 -i 0.1 -c 3 -q 1c3::c01d > /dev/null
check_fib_counter 3 "$nsrouter" 1c3::c01d || exit 1

# delete all rules
ip netns exec "$ns1" nft flush ruleset
ip netns exec "$ns2" nft flush ruleset
ip netns exec "$nsrouter" nft flush ruleset

ip -net "$ns1" addr add 10.0.1.99/24 dev eth0
ip -net "$ns1" addr add dead:1::99/64 dev eth0 nodad

ip -net "$ns1" addr del 10.0.2.99/24 dev eth0
ip -net "$ns1" addr del dead:2::99/64 dev eth0

ip -net "$nsrouter" addr del dead:2::1/64 dev veth0

# ... pbr ruleset for the router, check iif+oif.
if ! load_pbr_ruleset "$nsrouter";then
	echo "SKIP: Could not load fib forward ruleset"
	[ "$ret" -eq 0 ] && ret=$ksft_skip
fi

ip -net "$nsrouter" rule add from all table 128
ip -net "$nsrouter" rule add from all iif veth0 table 129
ip -net "$nsrouter" route add table 128 to 10.0.1.0/24 dev veth0
ip -net "$nsrouter" route add table 129 to 10.0.2.0/24 dev veth1

# drop main ipv4 table
ip -net "$nsrouter" -4 rule delete table main

if test_ping 10.0.2.99 dead:2::99;then
	echo "PASS: fib expression forward check with policy based routing"
else
	echo "FAIL: fib expression forward check with policy based routing"
	ret=1
fi

test_fib_type "policy routing"
ip netns exec "$nsrouter" nft delete table ip filter
ip netns exec "$nsrouter" nft delete table ip6 filter

# Un-do policy routing changes
ip -net "$nsrouter" rule del from all table 128
ip -net "$nsrouter" rule del from all iif veth0 table 129

ip -net "$nsrouter" route del table 128 to 10.0.1.0/24 dev veth0
ip -net "$nsrouter" route del table 129 to 10.0.2.0/24 dev veth1

ip -net "$ns1" -4 route del default
ip -net "$ns1" -6 route del default

ip -net "$ns1" -4 route add default via 10.0.1.1
ip -net "$ns1" -6 route add default via dead:1::1

ip -net "$nsrouter" -4 rule add from all table main priority 32766

test_fib_type "default table"
ip netns exec "$nsrouter" nft delete table ip filter
ip netns exec "$nsrouter" nft delete table ip6 filter

test_fib_vrf

exit $ret
