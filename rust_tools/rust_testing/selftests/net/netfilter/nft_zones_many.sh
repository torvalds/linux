#!/bin/bash

# Test insertion speed for packets with identical addresses/ports
# that are all placed in distinct conntrack zones.

source lib.sh

zones=2000
[ "$KSFT_MACHINE_SLOW" = yes ] && zones=500

have_ct_tool=0
ret=0

cleanup()
{
	cleanup_all_ns
}

checktool "nft --version" "run test without nft tool"
checktool "socat -V" "run test without socat tool"

setup_ns ns1

trap cleanup EXIT

if conntrack -V > /dev/null 2>&1; then
	have_ct_tool=1
fi

test_zones() {
	local max_zones=$1

ip netns exec "$ns1" nft -f /dev/stdin<<EOF
flush ruleset
table inet raw {
	map rndzone {
		typeof numgen inc mod $max_zones : ct zone
	}

	chain output {
		type filter hook output priority -64000; policy accept;
		udp dport 12345  ct zone set numgen inc mod 65536 map @rndzone
	}
}
EOF
if [ "$?" -ne 0 ];then
	echo "SKIP: Cannot add nftables rules"
	exit $ksft_skip
fi

	ip netns exec "$ns1" sysctl -q net.netfilter.nf_conntrack_udp_timeout=3600

	(
		echo "add element inet raw rndzone {"
	for i in $(seq 1 "$max_zones");do
		echo -n "$i : $i"
		if [ "$i" -lt "$max_zones" ]; then
			echo ","
		else
			echo "}"
		fi
	done
	) | ip netns exec "$ns1" nft -f /dev/stdin

	local i=0
	local j=0
	local outerstart
	local stop
	outerstart=$(date +%s%3N)
	stop=$outerstart

	while [ "$i" -lt "$max_zones" ]; do
		local start
		start=$(date +%s%3N)
		i=$((i + 1000))
		j=$((j + 1))
		# nft rule in output places each packet in a different zone.
		dd if=/dev/zero bs=8k count=1000 2>/dev/null | ip netns exec "$ns1" socat -u STDIN UDP:127.0.0.1:12345,sourceport=12345
		if [ $? -ne 0 ] ;then
			ret=1
			break
		fi

		stop=$(date +%s%3N)
		local duration=$((stop-start))
		echo "PASS: added 1000 entries in $duration ms (now $i total, loop $j)"
	done

	if [ "$have_ct_tool" -eq 1 ]; then
		local count duration
		count=$(ip netns exec "$ns1" conntrack -C)
		duration=$((stop-outerstart))

		if [ "$count" -ge "$max_zones" ]; then
			echo "PASS: inserted $count entries from packet path in $duration ms total"
		else
			ip netns exec "$ns1" conntrack -S 1>&2
			echo "FAIL: inserted $count entries from packet path in $duration ms total, expected $max_zones entries"
			ret=1
		fi
	fi

	if [ $ret -ne 0 ];then
		echo "FAIL: insert $max_zones entries from packet path" 1>&2
	fi
}

test_conntrack_tool() {
	local max_zones=$1

	ip netns exec "$ns1" conntrack -F >/dev/null 2>/dev/null

	local outerstart start stop i
	outerstart=$(date +%s%3N)
	start=$(date +%s%3N)
	stop="$start"
	i=0
	while [ "$i" -lt "$max_zones" ]; do
		i=$((i + 1))
		ip netns exec "$ns1" conntrack -I -s 1.1.1.1 -d 2.2.2.2 --protonum 6 \
	                 --timeout 3600 --state ESTABLISHED --sport 12345 --dport 1000 --zone $i >/dev/null 2>&1
		if [ $? -ne 0 ];then
			ip netns exec "$ns1" conntrack -I -s 1.1.1.1 -d 2.2.2.2 --protonum 6 \
	                 --timeout 3600 --state ESTABLISHED --sport 12345 --dport 1000 --zone $i > /dev/null
			echo "FAIL: conntrack -I returned an error"
			ret=1
			break
		fi

		if [ $((i%1000)) -eq 0 ];then
			stop=$(date +%s%3N)

			local duration=$((stop-start))
			echo "PASS: added 1000 entries in $duration ms (now $i total)"
			start=$stop
		fi
	done

	local count
	local duration
	count=$(ip netns exec "$ns1" conntrack -C)
	duration=$((stop-outerstart))

	if [ "$count" -eq "$max_zones" ]; then
		echo "PASS: inserted $count entries via ctnetlink in $duration ms"
	else
		ip netns exec "$ns1" conntrack -S 1>&2
		echo "FAIL: inserted $count entries via ctnetlink in $duration ms, expected $max_zones entries ($duration ms)"
		ret=1
	fi
}

test_zones $zones

if [ "$have_ct_tool" -eq 1 ];then
	test_conntrack_tool $zones
else
	echo "SKIP: Could not run ctnetlink insertion test without conntrack tool"
	if [ $ret -eq 0 ];then
		exit $ksft_skip
	fi
fi

exit $ret
