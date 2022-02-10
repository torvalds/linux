#!/bin/bash
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

readonly NS1="ns1-$(mktemp -u XXXXXX)"
readonly NS2="ns2-$(mktemp -u XXXXXX)"
ret=0

setup()
{

	local xdpmode=$1

	ip netns add ${NS1}
	ip netns add ${NS2}

	ip link add veth1 index 111 type veth peer name veth11 netns ${NS1}
	ip link add veth2 index 222 type veth peer name veth22 netns ${NS2}

	ip link set veth1 up
	ip link set veth2 up
	ip -n ${NS1} link set dev veth11 up
	ip -n ${NS2} link set dev veth22 up

	ip -n ${NS1} addr add 10.1.1.11/24 dev veth11
	ip -n ${NS2} addr add 10.1.1.22/24 dev veth22
}

cleanup()
{
	ip link del veth1 2> /dev/null
	ip link del veth2 2> /dev/null
	ip netns del ${NS1} 2> /dev/null
	ip netns del ${NS2} 2> /dev/null
}

test_xdp_redirect()
{
	local xdpmode=$1

	setup

	ip link set dev veth1 $xdpmode off &> /dev/null
	if [ $? -ne 0 ];then
		echo "selftests: test_xdp_redirect $xdpmode [SKIP]"
		return 0
	fi

	ip -n ${NS1} link set veth11 $xdpmode obj xdp_dummy.o sec xdp &> /dev/null
	ip -n ${NS2} link set veth22 $xdpmode obj xdp_dummy.o sec xdp &> /dev/null
	ip link set dev veth1 $xdpmode obj test_xdp_redirect.o sec redirect_to_222 &> /dev/null
	ip link set dev veth2 $xdpmode obj test_xdp_redirect.o sec redirect_to_111 &> /dev/null

	if ip netns exec ${NS1} ping -c 1 10.1.1.22 &> /dev/null &&
	   ip netns exec ${NS2} ping -c 1 10.1.1.11 &> /dev/null; then
		echo "selftests: test_xdp_redirect $xdpmode [PASS]";
	else
		ret=1
		echo "selftests: test_xdp_redirect $xdpmode [FAILED]";
	fi

	cleanup
}

set -e
trap cleanup 2 3 6 9

test_xdp_redirect xdpgeneric
test_xdp_redirect xdpdrv

exit $ret
