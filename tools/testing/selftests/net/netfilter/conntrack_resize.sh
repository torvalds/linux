#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

checktool "conntrack --version" "run test without conntrack"
checktool "nft --version" "run test without nft tool"

init_net_max=0
ct_buckets=0
tmpfile=""
tmpfile_proc=""
tmpfile_uniq=""
ret=0
have_socat=0

socat -h > /dev/null && have_socat=1

insert_count=2000
[ "$KSFT_MACHINE_SLOW" = "yes" ] && insert_count=400

modprobe -q nf_conntrack
if ! sysctl -q net.netfilter.nf_conntrack_max >/dev/null;then
	echo "SKIP: conntrack sysctls not available"
	exit $KSFT_SKIP
fi

init_net_max=$(sysctl -n net.netfilter.nf_conntrack_max) || exit 1
ct_buckets=$(sysctl -n net.netfilter.nf_conntrack_buckets) || exit 1

cleanup() {
	cleanup_all_ns

	rm -f "$tmpfile" "$tmpfile_proc" "$tmpfile_uniq"

	# restore original sysctl setting
	sysctl -q net.netfilter.nf_conntrack_max=$init_net_max
	sysctl -q net.netfilter.nf_conntrack_buckets=$ct_buckets
}
trap cleanup EXIT

check_max_alias()
{
	local expected="$1"
	# old name, expected to alias to the first, i.e. changing one
	# changes the other as well.
	local lv=$(sysctl -n net.nf_conntrack_max)

	if [ $expected -ne "$lv" ];then
		echo "nf_conntrack_max sysctls should have identical values"
		exit 1
	fi
}

insert_ctnetlink() {
	local ns="$1"
	local count="$2"
	local i=0
	local bulk=16

	while [ $i -lt $count ] ;do
		ip netns exec "$ns" bash -c "for i in \$(seq 1 $bulk); do \
			if ! conntrack -I -s \$((\$RANDOM%256)).\$((\$RANDOM%256)).\$((\$RANDOM%256)).\$((\$RANDOM%255+1)) \
					  -d \$((\$RANDOM%256)).\$((\$RANDOM%256)).\$((\$RANDOM%256)).\$((\$RANDOM%255+1)) \
					  --protonum 17 --timeout 3600 --status ASSURED,SEEN_REPLY --sport \$RANDOM --dport 53; then \
					  return;\
			fi & \
		done ; wait" 2>/dev/null

		i=$((i+bulk))
	done
}

check_ctcount() {
	local ns="$1"
	local count="$2"
	local msg="$3"

	local now=$(ip netns exec "$ns" conntrack -C)

	if [ $now -ne "$count" ] ;then
		echo "expected $count entries in $ns, not $now: $msg"
		exit 1
	fi

	echo "PASS: got $count connections: $msg"
}

ctresize() {
	local duration="$1"
	local now=$(date +%s)
	local end=$((now + duration))

	while [ $now -lt $end ]; do
		sysctl -q net.netfilter.nf_conntrack_buckets=$RANDOM
		now=$(date +%s)
	done
}

do_rsleep() {
	local limit="$1"
	local r=$RANDOM

	r=$((r%limit))
	sleep "$r"
}

ct_flush_once() {
	local ns="$1"

	ip netns exec "$ns" conntrack -F 2>/dev/null
}

ctflush() {
	local ns="$1"
	local duration="$2"
	local now=$(date +%s)
	local end=$((now + duration))

	do_rsleep "$duration"

        while [ $now -lt $end ]; do
		ct_flush_once "$ns"
		do_rsleep "$duration"
		now=$(date +%s)
        done
}

ct_pingflood()
{
	local ns="$1"
	local duration="$2"
	local msg="$3"
	local now=$(date +%s)
	local end=$((now + duration))
	local j=0
	local k=0

        while [ $now -lt $end ]; do
		j=$((j%256))
		k=$((k%256))

		ip netns exec "$ns" bash -c \
			"j=$j k=$k; for i in \$(seq 1 254); do ping -q -c 1 127.\$k.\$j.\$i & done; wait" >/dev/null 2>&1

		j=$((j+1))

		if [ $j -eq 256 ];then
			k=$((k+1))
		fi

		now=$(date +%s)
	done

	wait
}

ct_udpflood()
{
	local ns="$1"
	local duration="$2"
	local now=$(date +%s)
	local end=$((now + duration))

	[ $have_socat -ne "1" ] && return

        while [ $now -lt $end ]; do
ip netns exec "$ns" bash<<"EOF"
	for i in $(seq 1 100);do
		dport=$(((RANDOM%65536)+1))

		echo bar | socat -u STDIN UDP:"127.0.0.1:$dport" &
	done > /dev/null 2>&1
	wait
EOF
		now=$(date +%s)
	done
}

ct_udpclash()
{
	local ns="$1"
	local duration="$2"
	local now=$(date +%s)
	local end=$((now + duration))

	[ -x udpclash ] || return

        while [ $now -lt $end ]; do
		ip netns exec "$ns" timeout 30 ./udpclash 127.0.0.1 $((RANDOM%65536)) > /dev/null 2>&1

		now=$(date +%s)
	done
}

# dump to /dev/null.  We don't want dumps to cause infinite loops
# or use-after-free even when conntrack table is altered while dumps
# are in progress.
ct_nulldump()
{
	local ns="$1"

	ip netns exec "$ns" conntrack -L > /dev/null 2>&1 &

	# Don't require /proc support in conntrack
	if [ -r /proc/self/net/nf_conntrack ] ; then
		ip netns exec "$ns" bash -c "wc -l < /proc/self/net/nf_conntrack" > /dev/null &
	fi

	wait
}

ct_nulldump_loop()
{
	local ns="$1"
	local duration="$2"
	local now=$(date +%s)
	local end=$((now + duration))

        while [ $now -lt $end ]; do
		ct_nulldump "$ns"
		sleep $((RANDOM%2))
		now=$(date +%s)
	done
}

change_timeouts()
{
	local ns="$1"
	local r1=$((RANDOM%2))
	local r2=$((RANDOM%2))

	[ "$r1" -eq 1 ] && ip netns exec "$ns" sysctl -q net.netfilter.nf_conntrack_icmp_timeout=$((RANDOM%5))
	[ "$r2" -eq 1 ] && ip netns exec "$ns" sysctl -q net.netfilter.nf_conntrack_udp_timeout=$((RANDOM%5))
}

ct_change_timeouts_loop()
{
	local ns="$1"
	local duration="$2"
	local now=$(date +%s)
	local end=$((now + duration))

        while [ $now -lt $end ]; do
		change_timeouts "$ns"
		sleep $((RANDOM%2))
		now=$(date +%s)
	done

	# restore defaults
	ip netns exec "$ns" sysctl -q net.netfilter.nf_conntrack_icmp_timeout=30
	ip netns exec "$ns" sysctl -q net.netfilter.nf_conntrack_udp_timeout=30
}

check_taint()
{
	local tainted_then="$1"
	local msg="$2"

	local tainted_now=0

	if [ "$tainted_then" -ne 0 ];then
		return
	fi

	read tainted_now < /proc/sys/kernel/tainted

	if [ "$tainted_now" -eq 0 ];then
		echo "PASS: $msg"
	else
		echo "TAINT: $msg"
		dmesg
		exit 1
	fi
}

insert_flood()
{
	local n="$1"
	local timeout="$2"
	local r=0

	r=$((RANDOM%$insert_count))

	ct_pingflood "$n" "$timeout" "floodresize" &
	ct_udpflood "$n" "$timeout" &
	ct_udpclash "$n" "$timeout" &

	insert_ctnetlink "$n" "$r" &
	ctflush "$n" "$timeout" &
	ct_nulldump_loop "$n" "$timeout" &
	ct_change_timeouts_loop "$n" "$timeout" &

	wait
}

test_floodresize_all()
{
	local timeout=20
	local n=""
	local tainted_then=""

	read tainted_then < /proc/sys/kernel/tainted

	for n in "$nsclient1" "$nsclient2";do
		insert_flood "$n" "$timeout" &
	done

	# resize table constantly while flood/insert/dump/flushs
	# are happening in parallel.
	ctresize "$timeout"

	# wait for subshells to complete, everything is limited
	# by $timeout.
	wait

	check_taint "$tainted_then" "resize+flood"
}

check_dump()
{
	local ns="$1"
	local protoname="$2"
	local c=0
	local proto=0
	local proc=0
	local unique=""
	local lret=0

	# NOTE: assumes timeouts are large enough to not have
	# expirations in all following tests.
	l=$(ip netns exec "$ns" conntrack -L 2>/dev/null | sort | tee "$tmpfile" | wc -l)
	c=$(ip netns exec "$ns" conntrack -C)

	if [ "$c" -eq 0 ]; then
		echo "FAIL: conntrack count for $ns is 0"
		lret=1
	fi

	if [ "$c" -ne "$l" ]; then
		echo "FAIL: conntrack count inconsistency for $ns -L: $c != $l"
		lret=1
	fi

	# check the dump we retrieved is free of duplicated entries.
	unique=$(uniq "$tmpfile" | tee "$tmpfile_uniq" | wc -l)
	if [ "$l" -ne "$unique" ]; then
		echo "FAIL: listing contained redundant entries for $ns: $l != $unique"
		diff -u "$tmpfile" "$tmpfile_uniq"
		lret=1
	fi

	# we either inserted icmp or only udp, hence, --proto should return same entry count as without filter.
	proto=$(ip netns exec "$ns" conntrack -L --proto $protoname 2>/dev/null | sort | uniq | tee "$tmpfile_uniq" | wc -l)
	if [ "$l" -ne "$proto" ]; then
		echo "FAIL: dump inconsistency for $ns -L --proto $protoname: $l != $proto"
		diff -u "$tmpfile" "$tmpfile_uniq"
		lret=1
	fi

	if [ -r /proc/self/net/nf_conntrack ] ; then
		proc=$(ip netns exec "$ns" bash -c "sort < /proc/self/net/nf_conntrack | tee \"$tmpfile_proc\" | wc -l")

		if [ "$l" -ne "$proc" ]; then
			echo "FAIL: proc inconsistency for $ns: $l != $proc"
			lret=1
		fi

		proc=$(uniq "$tmpfile_proc" | tee "$tmpfile_uniq" | wc -l)
		if [ "$l" -ne "$proc" ]; then
			echo "FAIL: proc inconsistency after uniq filter for $ns: $l != $proc"
			diff -u "$tmpfile_proc" "$tmpfile_uniq"
			lret=1
		fi
	fi

	if [ $lret -eq 0 ];then
		echo "PASS: dump in netns $ns had same entry count (-C $c, -L $l, -p $proto, /proc $proc)"
	else
		echo "FAIL: dump in netns $ns had different entry count (-C $c, -L $l, -p $proto, /proc $proc)"
		ret=1
	fi
}

test_dump_all()
{
	local timeout=3
	local tainted_then=""

	read tainted_then < /proc/sys/kernel/tainted

	ct_flush_once "$nsclient1"
	ct_flush_once "$nsclient2"

	ip netns exec "$nsclient1" sysctl -q net.netfilter.nf_conntrack_icmp_timeout=3600

	ct_pingflood "$nsclient1" $timeout "dumpall" &
	insert_ctnetlink "$nsclient2" $insert_count

	wait

	check_dump "$nsclient1" "icmp"
	check_dump "$nsclient2" "udp"

	check_taint "$tainted_then" "test parallel conntrack dumps"
}

check_sysctl_immutable()
{
	local ns="$1"
	local name="$2"
	local failhard="$3"
	local o=0
	local n=0

	o=$(ip netns exec "$ns" sysctl -n "$name" 2>/dev/null)
	n=$((o+1))

	# return value isn't reliable, need to read it back
	ip netns exec "$ns" sysctl -q "$name"=$n 2>/dev/null >/dev/null

	n=$(ip netns exec "$ns" sysctl -n "$name" 2>/dev/null)

	[ -z "$n" ] && return 1

	if [ $o -ne $n ]; then
		if [ $failhard -gt 0 ] ;then
			echo "FAIL: net.$name should not be changeable from namespace (now $n)"
			ret=1
		fi
		return 0
	fi

	return 1
}

test_conntrack_max_limit()
{
	sysctl -q net.netfilter.nf_conntrack_max=100
	insert_ctnetlink "$nsclient1" 101

	# check netns is clamped by init_net, i.e., either netns follows
	# init_net value, or a higher pernet limit (compared to init_net) is ignored.
	check_ctcount "$nsclient1" 100 "netns conntrack_max is init_net bound"

	sysctl -q net.netfilter.nf_conntrack_max=$init_net_max
}

test_conntrack_disable()
{
	local timeout=2

	# disable conntrack pickups
	ip netns exec "$nsclient1" nft flush table ip test_ct

	ct_flush_once "$nsclient1"
	ct_flush_once "$nsclient2"

	ct_pingflood "$nsclient1" "$timeout" "conntrack disable"
	ip netns exec "$nsclient2" ping -q -c 1 127.0.0.1 >/dev/null 2>&1

	# Disabled, should not have picked up any connection.
	check_ctcount "$nsclient1" 0 "conntrack disabled"

	# This one is still active, expect 1 connection.
	check_ctcount "$nsclient2" 1 "conntrack enabled"
}

init_net_max=$(sysctl -n net.netfilter.nf_conntrack_max)

check_max_alias $init_net_max

sysctl -q net.netfilter.nf_conntrack_max="262000"
check_max_alias 262000

setup_ns nsclient1 nsclient2

# check this only works from init_net
for n in netfilter.nf_conntrack_buckets netfilter.nf_conntrack_expect_max net.nf_conntrack_max;do
	check_sysctl_immutable "$nsclient1" "net.$n" 1
done

# won't work on older kernels. If it works, check that the netns obeys the limit
if check_sysctl_immutable "$nsclient1" net.netfilter.nf_conntrack_max 0;then
	# subtest: if pernet is changeable, check that reducing it in pernet
	# limits the pernet entries.  Inverse, pernet clamped by a lower init_net
	# setting, is already checked by "test_conntrack_max_limit" test.

	ip netns exec "$nsclient1" sysctl -q net.netfilter.nf_conntrack_max=1
	insert_ctnetlink "$nsclient1" 2
	check_ctcount "$nsclient1" 1 "netns conntrack_max is pernet bound"
	ip netns exec "$nsclient1" sysctl -q net.netfilter.nf_conntrack_max=$init_net_max
fi

for n in "$nsclient1" "$nsclient2";do
# enable conntrack in both namespaces
ip netns exec "$n" nft -f - <<EOF
table ip test_ct {
	chain input {
		type filter hook input priority 0
		ct state new counter
	}
}
EOF
done

tmpfile=$(mktemp)
tmpfile_proc=$(mktemp)
tmpfile_uniq=$(mktemp)
test_conntrack_max_limit
test_dump_all
test_floodresize_all
test_conntrack_disable

exit $ret
