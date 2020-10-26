#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test sets up 3 netns (src <-> fwd <-> dst). There is no direct veth link
# between src and dst. The netns fwd has veth links to each src and dst. The
# client is in src and server in dst. The test installs a TC BPF program to each
# host facing veth in fwd which calls into i) bpf_redirect_neigh() to perform the
# neigh addr population and redirect or ii) bpf_redirect_peer() for namespace
# switch from ingress side; it also installs a checker prog on the egress side
# to drop unexpected traffic.

if [[ $EUID -ne 0 ]]; then
	echo "This script must be run as root"
	echo "FAIL"
	exit 1
fi

# check that needed tools are present
command -v nc >/dev/null 2>&1 || \
	{ echo >&2 "nc is not available"; exit 1; }
command -v dd >/dev/null 2>&1 || \
	{ echo >&2 "dd is not available"; exit 1; }
command -v timeout >/dev/null 2>&1 || \
	{ echo >&2 "timeout is not available"; exit 1; }
command -v ping >/dev/null 2>&1 || \
	{ echo >&2 "ping is not available"; exit 1; }
if command -v ping6 >/dev/null 2>&1; then PING6=ping6; else PING6=ping; fi
command -v perl >/dev/null 2>&1 || \
	{ echo >&2 "perl is not available"; exit 1; }
command -v jq >/dev/null 2>&1 || \
	{ echo >&2 "jq is not available"; exit 1; }
command -v bpftool >/dev/null 2>&1 || \
	{ echo >&2 "bpftool is not available"; exit 1; }

readonly GREEN='\033[0;92m'
readonly RED='\033[0;31m'
readonly NC='\033[0m' # No Color

readonly PING_ARG="-c 3 -w 10 -q"

readonly TIMEOUT=10

readonly NS_SRC="ns-src-$(mktemp -u XXXXXX)"
readonly NS_FWD="ns-fwd-$(mktemp -u XXXXXX)"
readonly NS_DST="ns-dst-$(mktemp -u XXXXXX)"

readonly IP4_SRC="172.16.1.100"
readonly IP4_DST="172.16.2.100"

readonly IP6_SRC="::1:dead:beef:cafe"
readonly IP6_DST="::2:dead:beef:cafe"

readonly IP4_SLL="169.254.0.1"
readonly IP4_DLL="169.254.0.2"
readonly IP4_NET="169.254.0.0"

netns_cleanup()
{
	ip netns del ${NS_SRC}
	ip netns del ${NS_FWD}
	ip netns del ${NS_DST}
}

netns_setup()
{
	ip netns add "${NS_SRC}"
	ip netns add "${NS_FWD}"
	ip netns add "${NS_DST}"

	ip link add veth_src type veth peer name veth_src_fwd
	ip link add veth_dst type veth peer name veth_dst_fwd

	ip link set veth_src netns ${NS_SRC}
	ip link set veth_src_fwd netns ${NS_FWD}

	ip link set veth_dst netns ${NS_DST}
	ip link set veth_dst_fwd netns ${NS_FWD}

	ip -netns ${NS_SRC} addr add ${IP4_SRC}/32 dev veth_src
	ip -netns ${NS_DST} addr add ${IP4_DST}/32 dev veth_dst

	# The fwd netns automatically get a v6 LL address / routes, but also
	# needs v4 one in order to start ARP probing. IP4_NET route is added
	# to the endpoints so that the ARP processing will reply.

	ip -netns ${NS_FWD} addr add ${IP4_SLL}/32 dev veth_src_fwd
	ip -netns ${NS_FWD} addr add ${IP4_DLL}/32 dev veth_dst_fwd

	ip -netns ${NS_SRC} addr add ${IP6_SRC}/128 dev veth_src nodad
	ip -netns ${NS_DST} addr add ${IP6_DST}/128 dev veth_dst nodad

	ip -netns ${NS_SRC} link set dev veth_src up
	ip -netns ${NS_FWD} link set dev veth_src_fwd up

	ip -netns ${NS_DST} link set dev veth_dst up
	ip -netns ${NS_FWD} link set dev veth_dst_fwd up

	ip -netns ${NS_SRC} route add ${IP4_DST}/32 dev veth_src scope global
	ip -netns ${NS_SRC} route add ${IP4_NET}/16 dev veth_src scope global
	ip -netns ${NS_FWD} route add ${IP4_SRC}/32 dev veth_src_fwd scope global

	ip -netns ${NS_SRC} route add ${IP6_DST}/128 dev veth_src scope global
	ip -netns ${NS_FWD} route add ${IP6_SRC}/128 dev veth_src_fwd scope global

	ip -netns ${NS_DST} route add ${IP4_SRC}/32 dev veth_dst scope global
	ip -netns ${NS_DST} route add ${IP4_NET}/16 dev veth_dst scope global
	ip -netns ${NS_FWD} route add ${IP4_DST}/32 dev veth_dst_fwd scope global

	ip -netns ${NS_DST} route add ${IP6_SRC}/128 dev veth_dst scope global
	ip -netns ${NS_FWD} route add ${IP6_DST}/128 dev veth_dst_fwd scope global

	fmac_src=$(ip netns exec ${NS_FWD} cat /sys/class/net/veth_src_fwd/address)
	fmac_dst=$(ip netns exec ${NS_FWD} cat /sys/class/net/veth_dst_fwd/address)

	ip -netns ${NS_SRC} neigh add ${IP4_DST} dev veth_src lladdr $fmac_src
	ip -netns ${NS_DST} neigh add ${IP4_SRC} dev veth_dst lladdr $fmac_dst

	ip -netns ${NS_SRC} neigh add ${IP6_DST} dev veth_src lladdr $fmac_src
	ip -netns ${NS_DST} neigh add ${IP6_SRC} dev veth_dst lladdr $fmac_dst
}

netns_test_connectivity()
{
	set +e

	ip netns exec ${NS_DST} bash -c "nc -4 -l -p 9004 &"
	ip netns exec ${NS_DST} bash -c "nc -6 -l -p 9006 &"

	TEST="TCPv4 connectivity test"
	ip netns exec ${NS_SRC} bash -c "timeout ${TIMEOUT} dd if=/dev/zero bs=1000 count=100 > /dev/tcp/${IP4_DST}/9004"
	if [ $? -ne 0 ]; then
		echo -e "${TEST}: ${RED}FAIL${NC}"
		exit 1
	fi
	echo -e "${TEST}: ${GREEN}PASS${NC}"

	TEST="TCPv6 connectivity test"
	ip netns exec ${NS_SRC} bash -c "timeout ${TIMEOUT} dd if=/dev/zero bs=1000 count=100 > /dev/tcp/${IP6_DST}/9006"
	if [ $? -ne 0 ]; then
		echo -e "${TEST}: ${RED}FAIL${NC}"
		exit 1
	fi
	echo -e "${TEST}: ${GREEN}PASS${NC}"

	TEST="ICMPv4 connectivity test"
	ip netns exec ${NS_SRC} ping  $PING_ARG ${IP4_DST}
	if [ $? -ne 0 ]; then
		echo -e "${TEST}: ${RED}FAIL${NC}"
		exit 1
	fi
	echo -e "${TEST}: ${GREEN}PASS${NC}"

	TEST="ICMPv6 connectivity test"
	ip netns exec ${NS_SRC} $PING6 $PING_ARG ${IP6_DST}
	if [ $? -ne 0 ]; then
		echo -e "${TEST}: ${RED}FAIL${NC}"
		exit 1
	fi
	echo -e "${TEST}: ${GREEN}PASS${NC}"

	set -e
}

hex_mem_str()
{
	perl -e 'print join(" ", unpack("(H2)8", pack("L", @ARGV)))' $1
}

netns_setup_bpf()
{
	local obj=$1
	local use_forwarding=${2:-0}

	ip netns exec ${NS_FWD} tc qdisc add dev veth_src_fwd clsact
	ip netns exec ${NS_FWD} tc filter add dev veth_src_fwd ingress bpf da obj $obj sec src_ingress
	ip netns exec ${NS_FWD} tc filter add dev veth_src_fwd egress  bpf da obj $obj sec chk_egress

	ip netns exec ${NS_FWD} tc qdisc add dev veth_dst_fwd clsact
	ip netns exec ${NS_FWD} tc filter add dev veth_dst_fwd ingress bpf da obj $obj sec dst_ingress
	ip netns exec ${NS_FWD} tc filter add dev veth_dst_fwd egress  bpf da obj $obj sec chk_egress

	if [ "$use_forwarding" -eq "1" ]; then
		# bpf_fib_lookup() checks if forwarding is enabled
		ip netns exec ${NS_FWD} sysctl -w net.ipv4.ip_forward=1
		ip netns exec ${NS_FWD} sysctl -w net.ipv6.conf.veth_dst_fwd.forwarding=1
		ip netns exec ${NS_FWD} sysctl -w net.ipv6.conf.veth_src_fwd.forwarding=1
		return 0
	fi

	veth_src=$(ip netns exec ${NS_FWD} cat /sys/class/net/veth_src_fwd/ifindex)
	veth_dst=$(ip netns exec ${NS_FWD} cat /sys/class/net/veth_dst_fwd/ifindex)

	progs=$(ip netns exec ${NS_FWD} bpftool net --json | jq -r '.[] | .tc | map(.id) | .[]')
	for prog in $progs; do
		map=$(bpftool prog show id $prog --json | jq -r '.map_ids | .? | .[]')
		if [ ! -z "$map" ]; then
			bpftool map update id $map key hex $(hex_mem_str 0) value hex $(hex_mem_str $veth_src)
			bpftool map update id $map key hex $(hex_mem_str 1) value hex $(hex_mem_str $veth_dst)
		fi
	done
}

trap netns_cleanup EXIT
set -e

netns_setup
netns_setup_bpf test_tc_neigh.o
netns_test_connectivity
netns_cleanup
netns_setup
netns_setup_bpf test_tc_neigh_fib.o 1
netns_test_connectivity
netns_cleanup
netns_setup
netns_setup_bpf test_tc_peer.o
netns_test_connectivity
