#!/bin/sh
# Create 2 namespaces with two veth peers, and
# forward packets in-between using generic XDP
#
# NS1(veth11)     NS2(veth22)
#     |               |
#     |               |
#   (veth1, ------ (veth2,
#   id:111)         id:222)
#     | xdp forwarding |
#     ------------------

cleanup()
{
	if [ "$?" = "0" ]; then
		echo "selftests: test_xdp_redirect [PASS]";
	else
		echo "selftests: test_xdp_redirect [FAILED]";
	fi

	set +e
	ip netns del ns1 2> /dev/null
	ip netns del ns2 2> /dev/null
}

ip link set dev lo xdpgeneric off 2>/dev/null > /dev/null
if [ $? -ne 0 ];then
	echo "selftests: [SKIP] Could not run test without the ip xdpgeneric support"
	exit 0
fi
set -e

ip netns add ns1
ip netns add ns2

trap cleanup 0 2 3 6 9

ip link add veth1 index 111 type veth peer name veth11
ip link add veth2 index 222 type veth peer name veth22

ip link set veth11 netns ns1
ip link set veth22 netns ns2

ip link set veth1 up
ip link set veth2 up

ip netns exec ns1 ip addr add 10.1.1.11/24 dev veth11
ip netns exec ns2 ip addr add 10.1.1.22/24 dev veth22

ip netns exec ns1 ip link set dev veth11 up
ip netns exec ns2 ip link set dev veth22 up

ip link set dev veth1 xdpgeneric obj test_xdp_redirect.o sec redirect_to_222
ip link set dev veth2 xdpgeneric obj test_xdp_redirect.o sec redirect_to_111

ip netns exec ns1 ping -c 1 10.1.1.22
ip netns exec ns2 ping -c 1 10.1.1.11

exit 0
