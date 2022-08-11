#!/bin/bash
#
# This test is for stress-testing the nf_tables config plane path vs.
# packet path processing: Make sure we never release rules that are
# still visible to other cpus.
#
# set -e

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

testns=testns-$(mktemp -u "XXXXXXXX")

tables="foo bar baz quux"
global_ret=0
eret=0
lret=0

check_result()
{
	local r=$1
	local OK="PASS"

	if [ $r -ne 0 ] ;then
		OK="FAIL"
		global_ret=$r
	fi

	echo "$OK: nft $2 test returned $r"

	eret=0
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

tmp=$(mktemp)

for table in $tables; do
	echo add table inet "$table" >> "$tmp"
	echo flush table inet "$table" >> "$tmp"

	echo "add chain inet $table INPUT { type filter hook input priority 0; }" >> "$tmp"
	echo "add chain inet $table OUTPUT { type filter hook output priority 0; }" >> "$tmp"
	for c in $(seq 1 400); do
		chain=$(printf "chain%03u" "$c")
		echo "add chain inet $table $chain" >> "$tmp"
	done

	for c in $(seq 1 400); do
		chain=$(printf "chain%03u" "$c")
		for BASE in INPUT OUTPUT; do
			echo "add rule inet $table $BASE counter jump $chain" >> "$tmp"
		done
		echo "add rule inet $table $chain counter return" >> "$tmp"
	done
done

ip netns add "$testns"
ip -netns "$testns" link set lo up

lscpu | grep ^CPU\(s\): | ( read cpu cpunum ;
cpunum=$((cpunum-1))
for i in $(seq 0 $cpunum);do
	mask=$(printf 0x%x $((1<<$i)))
        ip netns exec "$testns" taskset $mask ping -4 127.0.0.1 -fq > /dev/null &
        ip netns exec "$testns" taskset $mask ping -6 ::1 -fq > /dev/null &
done)

sleep 1

ip netns exec "$testns" nft -f "$tmp"
for i in $(seq 1 10) ; do ip netns exec "$testns" nft -f "$tmp" & done

for table in $tables;do
	randsleep=$((RANDOM%2))
	sleep $randsleep
	ip netns exec "$testns" nft delete table inet $table
	lret=$?
	if [ $lret -ne 0 ]; then
		eret=$lret
	fi
done

check_result $eret "add/delete"

for i in $(seq 1 10) ; do
	(echo "flush ruleset"; cat "$tmp") | ip netns exec "$testns" nft -f /dev/stdin

	lret=$?
	if [ $lret -ne 0 ]; then
		eret=$lret
	fi
done

check_result $eret "reload"

for i in $(seq 1 10) ; do
	(echo "flush ruleset"; cat "$tmp"
	 echo "insert rule inet foo INPUT meta nftrace set 1"
	 echo "insert rule inet foo OUTPUT meta nftrace set 1"
	 ) | ip netns exec "$testns" nft -f /dev/stdin
	lret=$?
	if [ $lret -ne 0 ]; then
		eret=$lret
	fi

	(echo "flush ruleset"; cat "$tmp"
	 ) | ip netns exec "$testns" nft -f /dev/stdin

	lret=$?
	if [ $lret -ne 0 ]; then
		eret=$lret
	fi
done

check_result $eret "add/delete with nftrace enabled"

echo "insert rule inet foo INPUT meta nftrace set 1" >> $tmp
echo "insert rule inet foo OUTPUT meta nftrace set 1" >> $tmp

for i in $(seq 1 10) ; do
	(echo "flush ruleset"; cat "$tmp") | ip netns exec "$testns" nft -f /dev/stdin

	lret=$?
	if [ $lret -ne 0 ]; then
		eret=1
	fi
done

check_result $lret "add/delete with nftrace enabled"

pkill -9 ping

wait

rm -f "$tmp"
ip netns del "$testns"

exit $global_ret
