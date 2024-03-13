#!/bin/bash

# check iif/iifname/oifgroup/iiftype match.

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
sfx=$(mktemp -u "XXXXXXXX")
ns0="ns0-$sfx"

if ! nft --version > /dev/null 2>&1; then
	echo "SKIP: Could not run test without nft tool"
	exit $ksft_skip
fi

cleanup()
{
	ip netns del "$ns0"
}

ip netns add "$ns0"
ip -net "$ns0" link set lo up
ip -net "$ns0" addr add 127.0.0.1 dev lo

trap cleanup EXIT

currentyear=$(date +%Y)
lastyear=$((currentyear-1))
ip netns exec "$ns0" nft -f /dev/stdin <<EOF
table inet filter {
	counter iifcount {}
	counter iifnamecount {}
	counter iifgroupcount {}
	counter iiftypecount {}
	counter infproto4count {}
	counter il4protocounter {}
	counter imarkcounter {}
	counter icpu0counter {}
	counter ilastyearcounter {}
	counter icurrentyearcounter {}

	counter oifcount {}
	counter oifnamecount {}
	counter oifgroupcount {}
	counter oiftypecount {}
	counter onfproto4count {}
	counter ol4protocounter {}
	counter oskuidcounter {}
	counter oskgidcounter {}
	counter omarkcounter {}

	chain input {
		type filter hook input priority 0; policy accept;

		meta iif lo counter name "iifcount"
		meta iifname "lo" counter name "iifnamecount"
		meta iifgroup "default" counter name "iifgroupcount"
		meta iiftype "loopback" counter name "iiftypecount"
		meta nfproto ipv4 counter name "infproto4count"
		meta l4proto icmp counter name "il4protocounter"
		meta mark 42 counter name "imarkcounter"
		meta cpu 0 counter name "icpu0counter"
		meta time "$lastyear-01-01" - "$lastyear-12-31" counter name ilastyearcounter
		meta time "$currentyear-01-01" - "$currentyear-12-31" counter name icurrentyearcounter
	}

	chain output {
		type filter hook output priority 0; policy accept;
		meta oif lo counter name "oifcount" counter
		meta oifname "lo" counter name "oifnamecount"
		meta oifgroup "default" counter name "oifgroupcount"
		meta oiftype "loopback" counter name "oiftypecount"
		meta nfproto ipv4 counter name "onfproto4count"
		meta l4proto icmp counter name "ol4protocounter"
		meta skuid 0 counter name "oskuidcounter"
		meta skgid 0 counter name "oskgidcounter"
		meta mark 42 counter name "omarkcounter"
	}
}
EOF

if [ $? -ne 0 ]; then
	echo "SKIP: Could not add test ruleset"
	exit $ksft_skip
fi

ret=0

check_one_counter()
{
	local cname="$1"
	local want="packets $2"
	local verbose="$3"

	if ! ip netns exec "$ns0" nft list counter inet filter $cname | grep -q "$want"; then
		echo "FAIL: $cname, want \"$want\", got"
		ret=1
		ip netns exec "$ns0" nft list counter inet filter $cname
	fi
}

check_lo_counters()
{
	local want="$1"
	local verbose="$2"
	local counter

	for counter in iifcount iifnamecount iifgroupcount iiftypecount infproto4count \
		       oifcount oifnamecount oifgroupcount oiftypecount onfproto4count \
		       il4protocounter icurrentyearcounter ol4protocounter \
	     ; do
		check_one_counter "$counter" "$want" "$verbose"
	done
}

check_lo_counters "0" false
ip netns exec "$ns0" ping -q -c 1 127.0.0.1 -m 42 > /dev/null

check_lo_counters "2" true

check_one_counter oskuidcounter "1" true
check_one_counter oskgidcounter "1" true
check_one_counter imarkcounter "1" true
check_one_counter omarkcounter "1" true
check_one_counter ilastyearcounter "0" true

if [ $ret -eq 0 ];then
	echo "OK: nftables meta iif/oif counters at expected values"
else
	exit $ret
fi

#First CPU execution and counter
taskset -p 01 $$ > /dev/null
ip netns exec "$ns0" nft reset counters > /dev/null
ip netns exec "$ns0" ping -q -c 1 127.0.0.1 > /dev/null
check_one_counter icpu0counter "2" true

if [ $ret -eq 0 ];then
	echo "OK: nftables meta cpu counter at expected values"
fi

exit $ret
