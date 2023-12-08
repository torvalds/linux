#!/bin/bash
#
# This tests the fib expression.
#
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
ret=0

sfx=$(mktemp -u "XXXXXXXX")
ns1="ns1-$sfx"
ns2="ns2-$sfx"
nsrouter="nsrouter-$sfx"
timeout=4

log_netns=$(sysctl -n net.netfilter.nf_log_all_netns)

cleanup()
{
	ip netns del ${ns1}
	ip netns del ${ns2}
	ip netns del ${nsrouter}

	[ $log_netns -eq 0 ] && sysctl -q net.netfilter.nf_log_all_netns=$log_netns
}

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

ip netns add ${nsrouter}
if [ $? -ne 0 ];then
	echo "SKIP: Could not create net namespace"
	exit $ksft_skip
fi

trap cleanup EXIT

dmesg | grep -q ' nft_rpfilter: '
if [ $? -eq 0 ]; then
	dmesg -c | grep ' nft_rpfilter: '
	echo "WARN: a previous test run has failed" 1>&2
fi

sysctl -q net.netfilter.nf_log_all_netns=1
ip netns add ${ns1}
ip netns add ${ns2}

load_ruleset() {
	local netns=$1

ip netns exec ${netns} nft -f /dev/stdin <<EOF
table inet filter {
	chain prerouting {
		type filter hook prerouting priority 0; policy accept;
	        fib saddr . iif oif missing counter log prefix "$netns nft_rpfilter: " drop
	}
}
EOF
}

load_pbr_ruleset() {
	local netns=$1

ip netns exec ${netns} nft -f /dev/stdin <<EOF
table inet filter {
	chain forward {
		type filter hook forward priority raw;
		fib saddr . iif oif gt 0 accept
		log drop
	}
}
EOF
}

load_ruleset_count() {
	local netns=$1

ip netns exec ${netns} nft -f /dev/stdin <<EOF
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
	dmesg | grep -q ' nft_rpfilter: '
	if [ $? -eq 0 ]; then
		dmesg | grep ' nft_rpfilter: '
		echo "FAIL: rpfilter did drop packets"
		return 1
	fi

	return 0
}

check_fib_counter() {
	local want=$1
	local ns=$2
	local address=$3

	line=$(ip netns exec ${ns} nft list table inet filter | grep 'fib saddr . iif' | grep $address | grep "packets $want" )
	ret=$?

	if [ $ret -ne 0 ];then
		echo "Netns $ns fib counter doesn't match expected packet count of $want for $address" 1>&2
		ip netns exec ${ns} nft list table inet filter
		return 1
	fi

	if [ $want -gt 0 ]; then
		echo "PASS: fib expression did drop packets for $address"
	fi

	return 0
}

load_ruleset ${nsrouter}
load_ruleset ${ns1}
load_ruleset ${ns2}

ip link add veth0 netns ${nsrouter} type veth peer name eth0 netns ${ns1} > /dev/null 2>&1
if [ $? -ne 0 ];then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
fi
ip link add veth1 netns ${nsrouter} type veth peer name eth0 netns ${ns2}

ip -net ${nsrouter} link set lo up
ip -net ${nsrouter} link set veth0 up
ip -net ${nsrouter} addr add 10.0.1.1/24 dev veth0
ip -net ${nsrouter} addr add dead:1::1/64 dev veth0

ip -net ${nsrouter} link set veth1 up
ip -net ${nsrouter} addr add 10.0.2.1/24 dev veth1
ip -net ${nsrouter} addr add dead:2::1/64 dev veth1

ip -net ${ns1} link set lo up
ip -net ${ns1} link set eth0 up

ip -net ${ns2} link set lo up
ip -net ${ns2} link set eth0 up

ip -net ${ns1} addr add 10.0.1.99/24 dev eth0
ip -net ${ns1} addr add dead:1::99/64 dev eth0
ip -net ${ns1} route add default via 10.0.1.1
ip -net ${ns1} route add default via dead:1::1

ip -net ${ns2} addr add 10.0.2.99/24 dev eth0
ip -net ${ns2} addr add dead:2::99/64 dev eth0
ip -net ${ns2} route add default via 10.0.2.1
ip -net ${ns2} route add default via dead:2::1

test_ping() {
  local daddr4=$1
  local daddr6=$2

  ip netns exec ${ns1} ping -c 1 -q $daddr4 > /dev/null
  ret=$?
  if [ $ret -ne 0 ];then
	check_drops
	echo "FAIL: ${ns1} cannot reach $daddr4, ret $ret" 1>&2
	return 1
  fi

  ip netns exec ${ns1} ping -c 3 -q $daddr6 > /dev/null
  ret=$?
  if [ $ret -ne 0 ];then
	check_drops
	echo "FAIL: ${ns1} cannot reach $daddr6, ret $ret" 1>&2
	return 1
  fi

  return 0
}

ip netns exec ${nsrouter} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec ${nsrouter} sysctl net.ipv4.conf.veth0.forwarding=1 > /dev/null
ip netns exec ${nsrouter} sysctl net.ipv4.conf.veth1.forwarding=1 > /dev/null
ip netns exec ${nsrouter} sysctl net.ipv4.conf.all.rp_filter=0 > /dev/null
ip netns exec ${nsrouter} sysctl net.ipv4.conf.veth0.rp_filter=0 > /dev/null

sleep 3

test_ping 10.0.2.1 dead:2::1 || exit 1
check_drops || exit 1

test_ping 10.0.2.99 dead:2::99 || exit 1
check_drops || exit 1

echo "PASS: fib expression did not cause unwanted packet drops"

ip netns exec ${nsrouter} nft flush table inet filter

ip -net ${ns1} route del default
ip -net ${ns1} -6 route del default

ip -net ${ns1} addr del 10.0.1.99/24 dev eth0
ip -net ${ns1} addr del dead:1::99/64 dev eth0

ip -net ${ns1} addr add 10.0.2.99/24 dev eth0
ip -net ${ns1} addr add dead:2::99/64 dev eth0

ip -net ${ns1} route add default via 10.0.2.1
ip -net ${ns1} -6 route add default via dead:2::1

ip -net ${nsrouter} addr add dead:2::1/64 dev veth0

# switch to ruleset that doesn't log, this time
# its expected that this does drop the packets.
load_ruleset_count ${nsrouter}

# ns1 has a default route, but nsrouter does not.
# must not check return value, ping to 1.1.1.1 will
# fail.
check_fib_counter 0 ${nsrouter} 1.1.1.1 || exit 1
check_fib_counter 0 ${nsrouter} 1c3::c01d || exit 1

ip netns exec ${ns1} ping -c 1 -W 1 -q 1.1.1.1 > /dev/null
check_fib_counter 1 ${nsrouter} 1.1.1.1 || exit 1

sleep 2
ip netns exec ${ns1} ping -c 3 -q 1c3::c01d > /dev/null
check_fib_counter 3 ${nsrouter} 1c3::c01d || exit 1

# delete all rules
ip netns exec ${ns1} nft flush ruleset
ip netns exec ${ns2} nft flush ruleset
ip netns exec ${nsrouter} nft flush ruleset

ip -net ${ns1} addr add 10.0.1.99/24 dev eth0
ip -net ${ns1} addr add dead:1::99/64 dev eth0

ip -net ${ns1} addr del 10.0.2.99/24 dev eth0
ip -net ${ns1} addr del dead:2::99/64 dev eth0

ip -net ${nsrouter} addr del dead:2::1/64 dev veth0

# ... pbr ruleset for the router, check iif+oif.
load_pbr_ruleset ${nsrouter}
if [ $? -ne 0 ] ; then
	echo "SKIP: Could not load fib forward ruleset"
	exit $ksft_skip
fi

ip -net ${nsrouter} rule add from all table 128
ip -net ${nsrouter} rule add from all iif veth0 table 129
ip -net ${nsrouter} route add table 128 to 10.0.1.0/24 dev veth0
ip -net ${nsrouter} route add table 129 to 10.0.2.0/24 dev veth1

# drop main ipv4 table
ip -net ${nsrouter} -4 rule delete table main

test_ping 10.0.2.99 dead:2::99
if [ $? -ne 0 ] ; then
	ip -net ${nsrouter} nft list ruleset
	echo "FAIL: fib mismatch in pbr setup"
	exit 1
fi

echo "PASS: fib expression forward check with policy based routing"
exit 0
