#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
source lib.sh

timeout=4m
ret=0
tmp=$(mktemp)
cleanup() {
	cleanup_all_ns
	rm -f "$tmp"
}

trap cleanup EXIT

maxpolicies=100000
[ "$KSFT_MACHINE_SLOW" = "yes" ] && maxpolicies=10000

do_dummies4() {
	local dir="$1"
	local max="$2"

	local policies
	local pfx
	pfx=30
	policies=0

	ip netns exec "$ns" ip xfrm policy flush

	for i in $(seq 1 100);do
		local s
		local d
		for j in $(seq 1 255);do
			s=$((i+0))
			d=$((i+100))

			for a in $(seq 1 8 255); do
				policies=$((policies+1))
				[ "$policies" -gt "$max" ] && return
				echo xfrm policy add src 10.$s.$j.0/30 dst 10.$d.$j.$a/$pfx dir $dir action block
			done
			for a in $(seq 1 8 255); do
				policies=$((policies+1))
				[ "$policies" -gt "$max" ] && return
				echo xfrm policy add src 10.$s.$j.$a/30 dst 10.$d.$j.0/$pfx dir $dir action block
			done
		done
	done
}

setup_ns ns

do_bench()
{
	local max="$1"

	start=$(date +%s%3N)
	do_dummies4 "out" "$max" > "$tmp"
	if ! timeout "$timeout" ip netns exec "$ns" ip -batch "$tmp";then
		echo "WARNING: policy insertion cancelled after $timeout"
		ret=1
	fi
	stop=$(date +%s%3N)

	result=$((stop-start))

	policies=$(wc -l < "$tmp")
	printf "Inserted %-06s policies in $result ms\n" $policies

	have=$(ip netns exec "$ns" ip xfrm policy show | grep "action block" | wc -l)
	if [ "$have" -ne "$policies" ]; then
		echo "WARNING: mismatch, have $have policies, expected $policies"
		ret=1
	fi
}

p=100
while [ $p -le "$maxpolicies" ]; do
	do_bench "$p"
	p="${p}0"
done

exit $ret
