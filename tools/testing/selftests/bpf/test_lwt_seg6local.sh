#!/bin/bash
# Connects 6 network namespaces through veths.
# Each NS may have different IPv6 global scope addresses :
#   NS1 ---- NS2 ---- NS3 ---- NS4 ---- NS5 ---- NS6
# fb00::1           fd00::1  fd00::2  fd00::3  fb00::6
#                   fc42::1           fd00::4
#
# All IPv6 packets going to fb00::/16 through NS2 will be encapsulated in a
# IPv6 header with a Segment Routing Header, with segments :
# 	fd00::1 -> fd00::2 -> fd00::3 -> fd00::4
#
# 3 fd00::/16 IPv6 addresses are binded to seg6local End.BPF actions :
# - fd00::1 : add a TLV, change the flags and apply a End.X action to fc42::1
# - fd00::2 : remove the TLV, change the flags, add a tag
# - fd00::3 : apply an End.T action to fd00::4, through routing table 117
#
# fd00::4 is a simple Segment Routing node decapsulating the inner IPv6 packet.
# Each End.BPF action will validate the operations applied on the SRH by the
# previous BPF program in the chain, otherwise the packet is dropped.
#
# An UDP datagram is sent from fb00::1 to fb00::6. The test succeeds if this
# datagram can be read on NS6 when binding to fb00::6.

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
readonly NS1="ns1-$(mktemp -u XXXXXX)"
readonly NS2="ns2-$(mktemp -u XXXXXX)"
readonly NS3="ns3-$(mktemp -u XXXXXX)"
readonly NS4="ns4-$(mktemp -u XXXXXX)"
readonly NS5="ns5-$(mktemp -u XXXXXX)"
readonly NS6="ns6-$(mktemp -u XXXXXX)"

msg="skip all tests:"
if [ $UID != 0 ]; then
	echo $msg please run this as root >&2
	exit $ksft_skip
fi

TMP_FILE="/tmp/selftest_lwt_seg6local.txt"

cleanup()
{
	if [ "$?" = "0" ]; then
		echo "selftests: test_lwt_seg6local [PASS]";
	else
		echo "selftests: test_lwt_seg6local [FAILED]";
	fi

	set +e
	ip netns del ${NS1} 2> /dev/null
	ip netns del ${NS2} 2> /dev/null
	ip netns del ${NS3} 2> /dev/null
	ip netns del ${NS4} 2> /dev/null
	ip netns del ${NS5} 2> /dev/null
	ip netns del ${NS6} 2> /dev/null
	rm -f $TMP_FILE
}

set -e

ip netns add ${NS1}
ip netns add ${NS2}
ip netns add ${NS3}
ip netns add ${NS4}
ip netns add ${NS5}
ip netns add ${NS6}

trap cleanup 0 2 3 6 9

ip link add veth1 type veth peer name veth2
ip link add veth3 type veth peer name veth4
ip link add veth5 type veth peer name veth6
ip link add veth7 type veth peer name veth8
ip link add veth9 type veth peer name veth10

ip link set veth1 netns ${NS1}
ip link set veth2 netns ${NS2}
ip link set veth3 netns ${NS2}
ip link set veth4 netns ${NS3}
ip link set veth5 netns ${NS3}
ip link set veth6 netns ${NS4}
ip link set veth7 netns ${NS4}
ip link set veth8 netns ${NS5}
ip link set veth9 netns ${NS5}
ip link set veth10 netns ${NS6}

ip netns exec ${NS1} ip link set dev veth1 up
ip netns exec ${NS2} ip link set dev veth2 up
ip netns exec ${NS2} ip link set dev veth3 up
ip netns exec ${NS3} ip link set dev veth4 up
ip netns exec ${NS3} ip link set dev veth5 up
ip netns exec ${NS4} ip link set dev veth6 up
ip netns exec ${NS4} ip link set dev veth7 up
ip netns exec ${NS5} ip link set dev veth8 up
ip netns exec ${NS5} ip link set dev veth9 up
ip netns exec ${NS6} ip link set dev veth10 up
ip netns exec ${NS6} ip link set dev lo up

# All link scope addresses and routes required between veths
ip netns exec ${NS1} ip -6 addr add fb00::12/16 dev veth1 scope link
ip netns exec ${NS1} ip -6 route add fb00::21 dev veth1 scope link
ip netns exec ${NS2} ip -6 addr add fb00::21/16 dev veth2 scope link
ip netns exec ${NS2} ip -6 addr add fb00::34/16 dev veth3 scope link
ip netns exec ${NS2} ip -6 route add fb00::43 dev veth3 scope link
ip netns exec ${NS3} ip -6 route add fb00::65 dev veth5 scope link
ip netns exec ${NS3} ip -6 addr add fb00::43/16 dev veth4 scope link
ip netns exec ${NS3} ip -6 addr add fb00::56/16 dev veth5 scope link
ip netns exec ${NS4} ip -6 addr add fb00::65/16 dev veth6 scope link
ip netns exec ${NS4} ip -6 addr add fb00::78/16 dev veth7 scope link
ip netns exec ${NS4} ip -6 route add fb00::87 dev veth7 scope link
ip netns exec ${NS5} ip -6 addr add fb00::87/16 dev veth8 scope link
ip netns exec ${NS5} ip -6 addr add fb00::910/16 dev veth9 scope link
ip netns exec ${NS5} ip -6 route add fb00::109 dev veth9 scope link
ip netns exec ${NS5} ip -6 route add fb00::109 table 117 dev veth9 scope link
ip netns exec ${NS6} ip -6 addr add fb00::109/16 dev veth10 scope link

ip netns exec ${NS1} ip -6 addr add fb00::1/16 dev lo
ip netns exec ${NS1} ip -6 route add fb00::6 dev veth1 via fb00::21

ip netns exec ${NS2} ip -6 route add fb00::6 encap bpf in obj test_lwt_seg6local.o sec encap_srh dev veth2
ip netns exec ${NS2} ip -6 route add fd00::1 dev veth3 via fb00::43 scope link

ip netns exec ${NS3} ip -6 route add fc42::1 dev veth5 via fb00::65
ip netns exec ${NS3} ip -6 route add fd00::1 encap seg6local action End.BPF endpoint obj test_lwt_seg6local.o sec add_egr_x dev veth4

ip netns exec ${NS4} ip -6 route add fd00::2 encap seg6local action End.BPF endpoint obj test_lwt_seg6local.o sec pop_egr dev veth6
ip netns exec ${NS4} ip -6 addr add fc42::1 dev lo
ip netns exec ${NS4} ip -6 route add fd00::3 dev veth7 via fb00::87

ip netns exec ${NS5} ip -6 route add fd00::4 table 117 dev veth9 via fb00::109
ip netns exec ${NS5} ip -6 route add fd00::3 encap seg6local action End.BPF endpoint obj test_lwt_seg6local.o sec inspect_t dev veth8

ip netns exec ${NS6} ip -6 addr add fb00::6/16 dev lo
ip netns exec ${NS6} ip -6 addr add fd00::4/16 dev lo

ip netns exec ${NS1} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec ${NS2} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec ${NS3} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec ${NS4} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec ${NS5} sysctl net.ipv6.conf.all.forwarding=1 > /dev/null

ip netns exec ${NS6} sysctl net.ipv6.conf.all.seg6_enabled=1 > /dev/null
ip netns exec ${NS6} sysctl net.ipv6.conf.lo.seg6_enabled=1 > /dev/null
ip netns exec ${NS6} sysctl net.ipv6.conf.veth10.seg6_enabled=1 > /dev/null

ip netns exec ${NS6} nc -l -6 -u -d 7330 > $TMP_FILE &
ip netns exec ${NS1} bash -c "echo 'foobar' | nc -w0 -6 -u -p 2121 -s fb00::1 fb00::6 7330"
sleep 5 # wait enough time to ensure the UDP datagram arrived to the last segment
kill -TERM $!

if [[ $(< $TMP_FILE) != "foobar" ]]; then
	exit 1
fi

exit 0
