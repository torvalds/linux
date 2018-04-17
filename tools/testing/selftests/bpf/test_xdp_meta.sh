#!/bin/sh

cleanup()
{
	if [ "$?" = "0" ]; then
		echo "selftests: test_xdp_meta [PASS]";
	else
		echo "selftests: test_xdp_meta [FAILED]";
	fi

	set +e
	ip netns del ns1 2> /dev/null
	ip netns del ns2 2> /dev/null
}

ip link set dev lo xdp off 2>/dev/null > /dev/null
if [ $? -ne 0 ];then
	echo "selftests: [SKIP] Could not run test without the ip xdp support"
	exit 0
fi
set -e

ip netns add ns1
ip netns add ns2

trap cleanup 0 2 3 6 9

ip link add veth1 type veth peer name veth2

ip link set veth1 netns ns1
ip link set veth2 netns ns2

ip netns exec ns1 ip addr add 10.1.1.11/24 dev veth1
ip netns exec ns2 ip addr add 10.1.1.22/24 dev veth2

ip netns exec ns1 tc qdisc add dev veth1 clsact
ip netns exec ns2 tc qdisc add dev veth2 clsact

ip netns exec ns1 tc filter add dev veth1 ingress bpf da obj test_xdp_meta.o sec t
ip netns exec ns2 tc filter add dev veth2 ingress bpf da obj test_xdp_meta.o sec t

ip netns exec ns1 ip link set dev veth1 xdp obj test_xdp_meta.o sec x
ip netns exec ns2 ip link set dev veth2 xdp obj test_xdp_meta.o sec x

ip netns exec ns1 ip link set dev veth1 up
ip netns exec ns2 ip link set dev veth2 up

ip netns exec ns1 ping -c 1 10.1.1.22
ip netns exec ns2 ping -c 1 10.1.1.11

exit 0
