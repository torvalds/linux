#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0
#
# Torture nftables' netdevice notifier callbacks and related code by frequent
# renaming of interfaces which netdev-family chains and flowtables hook into.

source lib.sh

checktool "nft --version" "run test without nft tool"
checktool "iperf3 --version" "run test without iperf3 tool"

read kernel_tainted < /proc/sys/kernel/tainted

# how many seconds to torture the kernel?
# default to 80% of max run time but don't exceed 48s
TEST_RUNTIME=$((${kselftest_timeout:-60} * 8 / 10))
[[ $TEST_RUNTIME -gt 48 ]] && TEST_RUNTIME=48

trap "cleanup_all_ns" EXIT

setup_ns nsc nsr nss

ip -net $nsc link add cr0 type veth peer name rc0 netns $nsr
ip -net $nsc addr add 10.0.0.1/24 dev cr0
ip -net $nsc link set cr0 up
ip -net $nsc route add default via 10.0.0.2

ip -net $nss link add sr0 type veth peer name rs0 netns $nsr
ip -net $nss addr add 10.1.0.1/24 dev sr0
ip -net $nss link set sr0 up
ip -net $nss route add default via 10.1.0.2

ip -net $nsr addr add 10.0.0.2/24 dev rc0
ip -net $nsr link set rc0 up
ip -net $nsr addr add 10.1.0.2/24 dev rs0
ip -net $nsr link set rs0 up
ip netns exec $nsr sysctl -q net.ipv4.ip_forward=1
ip netns exec $nsr sysctl -q net.ipv4.conf.all.forwarding=1

{
	echo "table netdev t {"
	for ((i = 0; i < 10; i++)); do
		cat <<-EOF
		chain chain_rc$i {
			type filter hook ingress device rc$i priority 0
			counter
		}
		chain chain_rs$i {
			type filter hook ingress device rs$i priority 0
			counter
		}
		EOF
	done
	echo "}"
	echo "table ip t {"
	for ((i = 0; i < 10; i++)); do
		cat <<-EOF
		flowtable ft_${i} {
			hook ingress priority 0
			devices = { rc$i, rs$i }
		}
		EOF
	done
	echo "chain c {"
	echo "type filter hook forward priority 0"
	for ((i = 0; i < 10; i++)); do
		echo -n "iifname rc$i oifname rs$i "
		echo    "ip protocol tcp counter flow add @ft_${i}"
	done
	echo "counter"
	echo "}"
	echo "}"
} | ip netns exec $nsr nft -f - || {
	echo "SKIP: Could not load nft ruleset"
	exit $ksft_skip
}

for ((o=0, n=1; ; o=n, n++, n %= 10)); do
	ip -net $nsr link set rc$o name rc$n
	ip -net $nsr link set rs$o name rs$n
done &
rename_loop_pid=$!

while true; do ip netns exec $nsr nft list ruleset >/dev/null 2>&1; done &
nft_list_pid=$!

ip netns exec $nsr nft monitor >/dev/null &
nft_monitor_pid=$!

ip netns exec $nss iperf3 --server --daemon -1
summary_expr='s,^\[SUM\] .* \([0-9\.]\+\) Kbits/sec .* receiver,\1,p'
rate=$(ip netns exec $nsc iperf3 \
	--format k -c 10.1.0.1 --time $TEST_RUNTIME \
	--length 56 --parallel 10 -i 0 | sed -n "$summary_expr")

kill $nft_list_pid
kill $nft_monitor_pid
kill $rename_loop_pid
wait

wildcard_prep() {
	ip netns exec $nsr nft -f - <<EOF
table ip t {
	flowtable ft_wild {
		hook ingress priority 0
		devices = { wild* }
	}
}
EOF
}

if ! wildcard_prep; then
	echo "SKIP wildcard tests: not supported by host's nft?"
else
	for ((i = 0; i < 100; i++)); do
		ip -net $nsr link add wild$i type dummy &
	done
	wait
	for ((i = 80; i < 100; i++)); do
		ip -net $nsr link del wild$i &
	done
	for ((i = 0; i < 80; i++)); do
		ip -net $nsr link del wild$i &
	done
	wait
	for ((i = 0; i < 100; i += 10)); do
		(
		for ((j = 0; j < 10; j++)); do
			ip -net $nsr link add wild$((i + j)) type dummy
		done
		for ((j = 0; j < 10; j++)); do
			ip -net $nsr link del wild$((i + j))
		done
		) &
	done
	wait
fi


[[ $kernel_tainted -eq 0 && $(</proc/sys/kernel/tainted) -ne 0 ]] && {
	echo "FAIL: Kernel is tainted!"
	exit $ksft_fail
}

[[ $rate -gt 0 ]] || {
	echo "FAIL: Zero throughput in iperf3"
	exit $ksft_fail
}

[[ -f /sys/kernel/debug/kmemleak && \
   -n $(</sys/kernel/debug/kmemleak) ]] && {
	echo "FAIL: non-empty kmemleak report"
	exit $ksft_fail
}

exit $ksft_pass
