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

  if ip netns exec "$ns1" ping -c 1 -W 0.1 -q "$daddr4" > /dev/null; then
	echo "FAIL: ${ns1} could reach $daddr4" 1>&2
	return 1
  fi

  if ip netns exec "$ns1" ping -c 1 -W 0.1 -q "$daddr6" > /dev/null; then
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

	ip -net "$nsrouter" link set dummy0 master tvrf
	ip -net "$nsrouter" link set dummy0 up
	ip -net "$nsrouter" link set tvrf up
}

load_ruleset_vrf()
{
# Due to the many different possible combinations using named counters
# or one-rule-per-expected-result is complex.
#
# Instead, add dynamic sets for the fib modes
# (fib address type, fib output interface lookup .. ),
# and then add the obtained fib results to them.
#
# The test is successful if the sets contain the expected results
# and no unexpected extra entries existed.
ip netns exec "$nsrouter" nft -f - <<EOF
flush ruleset
table inet t {
	set fibif4 {
		typeof meta iif . ip daddr . fib daddr oif
		flags dynamic
		counter
	}

	set fibif4iif {
		typeof meta iif . ip daddr . fib daddr . iif oif
		flags dynamic
		counter
	}

	set fibif6 {
		typeof meta iif . ip6 daddr . fib daddr oif
		flags dynamic
		counter
	}

	set fibif6iif {
		typeof meta iif . ip6 daddr . fib daddr . iif oif
		flags dynamic
		counter
	}

	set fibtype4 {
		typeof meta iif . ip daddr . fib daddr type
		flags dynamic
		counter
	}

	set fibtype4iif {
		typeof meta iif . ip daddr . fib daddr . iif type
		flags dynamic
		counter
	}

	set fibtype6 {
		typeof meta iif . ip6 daddr . fib daddr type
		flags dynamic
		counter
	}

	set fibtype6iif {
		typeof meta iif . ip6 daddr . fib daddr . iif type
		flags dynamic
		counter
	}

	chain fib_test {
		meta nfproto ipv4 jump {
			add @fibif4 { meta iif . ip daddr . fib daddr oif }
			add @fibif4iif { meta iif . ip daddr . fib daddr . iif oif }
			add @fibtype4 { meta iif . ip daddr . fib daddr type }
			add @fibtype4iif { meta iif . ip daddr . fib daddr . iif type }

			add @fibif4 { meta iif . ip saddr . fib saddr oif }
			add @fibif4iif { meta iif . ip saddr . fib saddr . iif oif }
		}

		meta nfproto ipv6 jump {
			add @fibif6    { meta iif . ip6 daddr . fib daddr oif }
			add @fibif6iif { meta iif . ip6 daddr . fib daddr . iif oif }
			add @fibtype6    { meta iif . ip6 daddr . fib daddr type }
			add @fibtype6iif { meta iif . ip6 daddr . fib daddr . iif type }

			add @fibif6 { meta iif . ip6 saddr . fib saddr oif }
			add @fibif6iif { meta iif . ip6 saddr . fib saddr . iif oif }
		}
	}

	chain prerouting {
		type filter hook prerouting priority 0;
		icmp type echo-request counter jump fib_test

		# neighbour discovery to be ignored.
		icmpv6 type echo-request counter jump fib_test
	}
}
EOF

if [ $? -ne 0 ] ;then
	echo "SKIP: Could not load ruleset for fib vrf test"
	[ $ret -eq 0 ] && ret=$ksft_skip
	return 1
fi
}

check_type()
{
	local setname="$1"
	local iifname="$2"
	local addr="$3"
	local type="$4"
	local count="$5"
	local lret=0

	[ -z "$count" ] && count=1

	if ! ip netns exec "$nsrouter" nft get element inet t "$setname" { "$iifname" . "$addr" . "$type" } |grep -q "counter packets $count";then
		echo "FAIL: did not find $iifname . $addr . $type in $setname with $count packets"
		ip netns exec "$nsrouter" nft list set inet t "$setname"
		ret=1
		# do not fail right away, delete entry if it exists so later test that
		# checks for unwanted keys don't get confused by this *expected* key.
		lret=1
	fi

	# delete the entry, this allows to check if anything unexpected appeared
	# at the end of the test run: all dynamic sets should be empty by then.
	if ! ip netns exec "$nsrouter" nft delete element inet t "$setname" { "$iifname" . "$addr" . "$type" } ; then
		echo "FAIL: can't delete $iifname . $addr . $type in $setname"
		ip netns exec "$nsrouter" nft list set inet t "$setname"
		ret=1
		return 1
	fi

	return $lret
}

check_local()
{
	check_type $@ "local" 1
}

check_unicast()
{
	check_type $@ "unicast" 1
}

check_rpf()
{
	check_type $@
}

check_fib_vrf_sets_empty()
{
	local setname=""
	local lret=0

	# A non-empty set means that we have seen unexpected packets OR
	# that a fib lookup provided unexpected results.
	for setname in "fibif4" "fibif4iif" "fibif6" "fibif6iif" \
		       "fibtype4" "fibtype4iif" "fibtype6" "fibtype6iif";do
		if ip netns exec "$nsrouter" nft list set inet t "$setname" | grep -q elements;then
			echo "FAIL: $setname not empty"
	                ip netns exec "$nsrouter" nft list set inet t "$setname"
			ret=1
			lret=1
		fi
	done

	return $lret
}

check_fib_vrf_type()
{
	local msg="$1"

	local addr
	# the incoming interface is always veth0.  As its not linked to a VRF,
	# the 'tvrf' device should NOT show up anywhere.
	local ifname="veth0"
	local lret=0

	# local_veth0, local_veth1
	for addr in "10.0.1.1" "10.0.2.1"; do
		check_local fibtype4  "$ifname" "$addr" || lret=1
		check_type  fibif4    "$ifname" "$addr" "0" || lret=1
	done
	for addr in "dead:1::1" "dead:2::1";do
		check_local fibtype6  "$ifname" "$addr" || lret=1
		check_type  fibif6    "$ifname" "$addr" "0" || lret=1
	done

	# when restricted to the incoming interface, 10.0.1.1 should
	# be 'local', but 10.0.2.1 unicast.
	check_local fibtype4iif   "$ifname" "10.0.1.1" || lret=1
	check_unicast fibtype4iif "$ifname" "10.0.2.1" || lret=1

	# same for the ipv6 addresses.
	check_local fibtype6iif   "$ifname" "dead:1::1" || lret=1
	check_unicast fibtype6iif "$ifname" "dead:2::1" || lret=1

	# None of these addresses should find a valid route when restricting
	# to the incoming interface (we ask for daddr - 10.0.1.1/2.1 are
	# reachable via 'lo'.
	for addr in "10.0.1.1" "10.0.2.1" "10.9.9.1" "10.9.9.2";do
		check_type fibif4iif "$ifname" "$addr" "0" || lret=1
	done

	# expect default route (veth1), dummy0 is part of VRF but iif isn't.
	for addr in "10.9.9.1" "10.9.9.2";do
		check_unicast fibtype4    "$ifname" "$addr" || lret=1
		check_unicast fibtype4iif "$ifname" "$addr" || lret=1
		check_type fibif4 "$ifname" "$addr" "veth1" || lret=1
	done
	for addr in "dead:9::1" "dead:9::2";do
		check_unicast fibtype6    "$ifname" "$addr" || lret=1
		check_unicast fibtype6iif "$ifname" "$addr" || lret=1
		check_type fibif6 "$ifname" "$addr" "veth1" || lret=1
	done

	# same for the IPv6 equivalent addresses.
	for addr in "dead:1::1" "dead:2::1" "dead:9::1" "dead:9::2";do
		check_type  fibif6iif "$ifname" "$addr" "0" || lret=1
	done

	check_unicast fibtype4    "$ifname" "10.0.2.99" || lret=1
	check_unicast fibtype4iif "$ifname" "10.0.2.99" || lret=1
	check_unicast fibtype6    "$ifname" "dead:2::99" || lret=1
	check_unicast fibtype6iif "$ifname" "dead:2::99" || lret=1

	check_type fibif4 "$ifname" "10.0.2.99" "veth1" || lret=1
	check_type fibif4iif "$ifname" "10.0.2.99" 0 || lret=1
	check_type fibif6 "$ifname" "dead:2::99" "veth1" || lret=1
	check_type fibif6iif "$ifname" "dead:2::99" 0 || lret=1

	check_rpf  fibif4    "$ifname" "10.0.1.99" "veth0" 5 || lret=1
	check_rpf  fibif4iif "$ifname" "10.0.1.99" "veth0" 5 || lret=1
	check_rpf  fibif6    "$ifname" "dead:1::99" "veth0" 5 || lret=1
	check_rpf  fibif6iif "$ifname" "dead:1::99" "veth0" 5 || lret=1

	check_fib_vrf_sets_empty || lret=1

	if [ $lret -eq 0 ];then
		echo "PASS: $msg"
	else
		echo "FAIL: $msg"
		ret=1
	fi
}

check_fib_veth_vrf_type()
{
	local msg="$1"

	local addr
	local ifname
	local setname
	local lret=0

	# as veth0 is now part of tvrf interface, packets will be seen
	# twice, once with iif veth0, then with iif tvrf.

	for ifname in "veth0" "tvrf"; do
		for addr in "10.0.1.1" "10.9.9.1"; do
			check_local fibtype4  "$ifname" "$addr" || lret=1
			# addr local, but nft_fib doesn't return routes with RTN_LOCAL.
			check_type  fibif4    "$ifname" "$addr" 0 || lret=1
			check_type  fibif4iif "$ifname" "$addr" 0 || lret=1
		done

		for addr in "dead:1::1" "dead:9::1"; do
			check_local fibtype6 "$ifname" "$addr" || lret=1
			# same, address is local but no route is returned for lo.
			check_type  fibif6    "$ifname" "$addr" 0 || lret=1
			check_type  fibif6iif "$ifname" "$addr" 0 || lret=1
		done

		for t in fibtype4 fibtype4iif; do
			check_unicast "$t" "$ifname" 10.9.9.2 || lret=1
		done
		for t in fibtype6 fibtype6iif; do
			check_unicast "$t" "$ifname" dead:9::2 || lret=1
		done

		check_unicast fibtype4iif "$ifname" "10.9.9.1" || lret=1
		check_unicast fibtype6iif "$ifname" "dead:9::1" || lret=1

		check_unicast fibtype4    "$ifname" "10.0.2.99" || lret=1
		check_unicast fibtype4iif "$ifname" "10.0.2.99" || lret=1

		check_unicast fibtype6    "$ifname" "dead:2::99" || lret=1
		check_unicast fibtype6iif "$ifname" "dead:2::99" || lret=1

		check_type fibif4    "$ifname"  "10.0.2.99" "veth1" || lret=1
		check_type fibif6    "$ifname" "dead:2::99" "veth1" || lret=1
		check_type fibif4    "$ifname"   "10.9.9.2" "dummy0" || lret=1
		check_type fibif6    "$ifname"  "dead:9::2" "dummy0" || lret=1

		# restricted to iif -- MUST NOT provide result, its != $ifname.
		check_type fibif4iif "$ifname"  "10.0.2.99" 0 || lret=1
		check_type fibif6iif "$ifname" "dead:2::99" 0 || lret=1

		check_rpf  fibif4 "$ifname" "10.0.1.99" "veth0" 4 || lret=1
		check_rpf  fibif6 "$ifname" "dead:1::99" "veth0" 4 || lret=1
		check_rpf  fibif4iif "$ifname" "10.0.1.99" "$ifname" 4 || lret=1
		check_rpf  fibif6iif "$ifname" "dead:1::99" "$ifname" 4 || lret=1
	done

	check_local fibtype4iif "veth0" "10.0.1.1" || lret=1
	check_local fibtype6iif "veth0" "dead:1::1" || lret=1

	check_unicast fibtype4iif "tvrf" "10.0.1.1" || lret=1
	check_unicast fibtype6iif "tvrf" "dead:1::1" || lret=1

	# 10.9.9.2 should not provide a result for iif veth, but
	# should when iif is tvrf.
	# This is because its reachable via dummy0 which is part of
	# tvrf.  iif veth0 MUST conceal the dummy0 result (i.e. return oif 0).
	check_type fibif4iif "veth0" "10.9.9.2" 0 || lret=1
	check_type fibif6iif "veth0"  "dead:9::2" 0 || lret=1

	check_type fibif4iif "tvrf" "10.9.9.2" "tvrf" || lret=1
	check_type fibif6iif "tvrf" "dead:9::2" "tvrf" || lret=1

	check_fib_vrf_sets_empty || lret=1

	if [ $lret -eq 0 ];then
		echo "PASS: $msg"
	else
		echo "FAIL: $msg"
		ret=1
	fi
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
	local cntname=""

	if ! test_fib_vrf_dev_add_dummy; then
		[ $ret -eq 0 ] && ret=$ksft_skip
		return
	fi

	ip -net "$nsrouter" addr add "10.9.9.1"/24 dev dummy0
	ip -net "$nsrouter" addr add "dead:9::1"/64 dev dummy0 nodad

	ip -net "$nsrouter" route add default via 10.0.2.99
	ip -net "$nsrouter" route add default via dead:2::99

	load_ruleset_vrf || return

	# no echo reply for these addresses: The dummy interface is part of tvrf,
	# but veth0 (incoming interface) isn't linked to it.
	test_ping_unreachable "10.9.9.1" "dead:9::1" &
	test_ping_unreachable "10.9.9.2" "dead:9::2" &

	# expect replies from these.
	test_ping "10.0.1.1" "dead:1::1"
	test_ping "10.0.2.1" "dead:2::1"
	test_ping "10.0.2.99" "dead:2::99"

	wait

	check_fib_vrf_type "fib expression address types match (iif not in vrf)"

	# second round: this time, make veth0 (rx interface) part of the vrf.
	# 10.9.9.1 / dead:9::1 become reachable from ns1, while ns2
	# becomes unreachable.
	ip -net "$nsrouter" link set veth0 master tvrf
	ip -net "$nsrouter" addr add dead:1::1/64 dev veth0 nodad

	# this reload should not be needed, but in case
	# there is some error (missing or unexpected entry) this will prevent them
	# from leaking into round 2.
	load_ruleset_vrf || return

	test_ping "10.0.1.1" "dead:1::1"
	test_ping "10.9.9.1" "dead:9::1"

	# ns2 should no longer be reachable (veth1 not in vrf)
	test_ping_unreachable "10.0.2.99" "dead:2::99" &

	# vrf via dummy0, but host doesn't exist
	test_ping_unreachable "10.9.9.2" "dead:9::2" &

	wait

	check_fib_veth_vrf_type "fib expression address types match (iif in vrf)"
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
