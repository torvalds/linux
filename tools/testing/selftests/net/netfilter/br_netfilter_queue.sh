#!/bin/bash

source lib.sh

checktool "nft --version" "run test without nft tool"

read t < /proc/sys/kernel/tainted
if [ "$t" -ne 0 ];then
	echo SKIP: kernel is tainted
	exit $ksft_skip
fi

cleanup() {
	cleanup_all_ns
}

setup_ns c1 c2 c3 sender

trap cleanup EXIT

nf_queue_wait()
{
	grep -q "^ *$1 " "/proc/self/net/netfilter/nfnetlink_queue"
}

port_add() {
	ns="$1"
	dev="$2"
	a="$3"

	ip link add name "$dev" type veth peer name "$dev" netns "$ns"

	ip -net "$ns" addr add 192.168.1."$a"/24 dev "$dev"
	ip -net "$ns" link set "$dev" up

	ip link set "$dev" master br0
	ip link set "$dev" up
}

[ "${1}" != "run" ] && { unshare -n "${0}" run; exit $?; }

ip link add br0 type bridge
ip addr add 192.168.1.254/24 dev br0

port_add "$c1" "c1" 1
port_add "$c2" "c2" 2
port_add "$c3" "c3" 3
port_add "$sender" "sender" 253

ip link set br0 up

modprobe -q br_netfilter

sysctl net.bridge.bridge-nf-call-iptables=1 || exit 1

ip netns exec "$sender" ping -I sender -c1 192.168.1.1 || exit 1
ip netns exec "$sender" ping -I sender -c1 192.168.1.2 || exit 2
ip netns exec "$sender" ping -I sender -c1 192.168.1.3 || exit 3

nft -f /dev/stdin <<EOF
table ip filter {
	chain forward {
		type filter hook forward priority 0; policy accept;
		ct state new counter
		ip protocol icmp counter queue num 0 bypass
	}
}
EOF
./nf_queue -t 5 > /dev/null &

busywait 5000 nf_queue_wait

for i in $(seq 1 5); do conntrack -F > /dev/null 2> /dev/null; sleep 0.1 ; done &
ip netns exec "$sender" ping -I sender -f -c 50 -b 192.168.1.255

read t < /proc/sys/kernel/tainted
if [ "$t" -eq 0 ];then
	echo PASS: kernel not tainted
else
	echo ERROR: kernel is tainted
	dmesg
	exit 1
fi

exit 0
