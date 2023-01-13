#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# In-place tunneling

BPF_FILE="test_tc_tunnel.bpf.o"
# must match the port that the bpf program filters on
readonly port=8000

readonly ns_prefix="ns-$$-"
readonly ns1="${ns_prefix}1"
readonly ns2="${ns_prefix}2"

readonly ns1_v4=192.168.1.1
readonly ns2_v4=192.168.1.2
readonly ns1_v6=fd::1
readonly ns2_v6=fd::2

# Must match port used by bpf program
readonly udpport=5555
# MPLSoverUDP
readonly mplsudpport=6635
readonly mplsproto=137

readonly infile="$(mktemp)"
readonly outfile="$(mktemp)"

setup() {
	ip netns add "${ns1}"
	ip netns add "${ns2}"

	ip link add dev veth1 mtu 1500 netns "${ns1}" type veth \
	      peer name veth2 mtu 1500 netns "${ns2}"

	ip netns exec "${ns1}" ethtool -K veth1 tso off

	ip -netns "${ns1}" link set veth1 up
	ip -netns "${ns2}" link set veth2 up

	ip -netns "${ns1}" -4 addr add "${ns1_v4}/24" dev veth1
	ip -netns "${ns2}" -4 addr add "${ns2_v4}/24" dev veth2
	ip -netns "${ns1}" -6 addr add "${ns1_v6}/64" dev veth1 nodad
	ip -netns "${ns2}" -6 addr add "${ns2_v6}/64" dev veth2 nodad

	# clamp route to reserve room for tunnel headers
	ip -netns "${ns1}" -4 route flush table main
	ip -netns "${ns1}" -6 route flush table main
	ip -netns "${ns1}" -4 route add "${ns2_v4}" mtu 1450 dev veth1
	ip -netns "${ns1}" -6 route add "${ns2_v6}" mtu 1430 dev veth1

	sleep 1

	dd if=/dev/urandom of="${infile}" bs="${datalen}" count=1 status=none
}

cleanup() {
	ip netns del "${ns2}"
	ip netns del "${ns1}"

	if [[ -f "${outfile}" ]]; then
		rm "${outfile}"
	fi
	if [[ -f "${infile}" ]]; then
		rm "${infile}"
	fi

	if [[ -n $server_pid ]]; then
		kill $server_pid 2> /dev/null
	fi
}

server_listen() {
	ip netns exec "${ns2}" nc "${netcat_opt}" -l "${port}" > "${outfile}" &
	server_pid=$!
	sleep 0.2
}

client_connect() {
	ip netns exec "${ns1}" timeout 2 nc "${netcat_opt}" -w 1 "${addr2}" "${port}" < "${infile}"
	echo $?
}

verify_data() {
	wait "${server_pid}"
	server_pid=
	# sha1sum returns two fields [sha1] [filepath]
	# convert to bash array and access first elem
	insum=($(sha1sum ${infile}))
	outsum=($(sha1sum ${outfile}))
	if [[ "${insum[0]}" != "${outsum[0]}" ]]; then
		echo "data mismatch"
		exit 1
	fi
}

set -e

# no arguments: automated test, run all
if [[ "$#" -eq "0" ]]; then
	echo "ipip"
	$0 ipv4 ipip none 100

	echo "ipip6"
	$0 ipv4 ipip6 none 100

	echo "ip6ip6"
	$0 ipv6 ip6tnl none 100

	echo "sit"
	$0 ipv6 sit none 100

	echo "ip4 vxlan"
	$0 ipv4 vxlan eth 2000

	echo "ip6 vxlan"
	$0 ipv6 ip6vxlan eth 2000

	for mac in none mpls eth ; do
		echo "ip gre $mac"
		$0 ipv4 gre $mac 100

		echo "ip6 gre $mac"
		$0 ipv6 ip6gre $mac 100

		echo "ip gre $mac gso"
		$0 ipv4 gre $mac 2000

		echo "ip6 gre $mac gso"
		$0 ipv6 ip6gre $mac 2000

		echo "ip udp $mac"
		$0 ipv4 udp $mac 100

		echo "ip6 udp $mac"
		$0 ipv6 ip6udp $mac 100

		echo "ip udp $mac gso"
		$0 ipv4 udp $mac 2000

		echo "ip6 udp $mac gso"
		$0 ipv6 ip6udp $mac 2000
	done

	echo "OK. All tests passed"
	exit 0
fi

if [[ "$#" -ne "4" ]]; then
	echo "Usage: $0"
	echo "   or: $0 <ipv4|ipv6> <tuntype> <none|mpls|eth> <data_len>"
	exit 1
fi

case "$1" in
"ipv4")
	readonly addr1="${ns1_v4}"
	readonly addr2="${ns2_v4}"
	readonly ipproto=4
	readonly netcat_opt=-${ipproto}
	readonly foumod=fou
	readonly foutype=ipip
	readonly fouproto=4
	readonly fouproto_mpls=${mplsproto}
	readonly gretaptype=gretap
	;;
"ipv6")
	readonly addr1="${ns1_v6}"
	readonly addr2="${ns2_v6}"
	readonly ipproto=6
	readonly netcat_opt=-${ipproto}
	readonly foumod=fou6
	readonly foutype=ip6tnl
	readonly fouproto="41 -6"
	readonly fouproto_mpls="${mplsproto} -6"
	readonly gretaptype=ip6gretap
	;;
*)
	echo "unknown arg: $1"
	exit 1
	;;
esac

readonly tuntype=$2
readonly mac=$3
readonly datalen=$4

echo "encap ${addr1} to ${addr2}, type ${tuntype}, mac ${mac} len ${datalen}"

trap cleanup EXIT

setup

# basic communication works
echo "test basic connectivity"
server_listen
client_connect
verify_data

# clientside, insert bpf program to encap all TCP to port ${port}
# client can no longer connect
ip netns exec "${ns1}" tc qdisc add dev veth1 clsact
ip netns exec "${ns1}" tc filter add dev veth1 egress \
	bpf direct-action object-file ${BPF_FILE} \
	section "encap_${tuntype}_${mac}"
echo "test bpf encap without decap (expect failure)"
server_listen
! client_connect

if [[ "$tuntype" =~ "udp" ]]; then
	# Set up fou tunnel.
	ttype="${foutype}"
	targs="encap fou encap-sport auto encap-dport $udpport"
	# fou may be a module; allow this to fail.
	modprobe "${foumod}" ||true
	if [[ "$mac" == "mpls" ]]; then
		dport=${mplsudpport}
		dproto=${fouproto_mpls}
		tmode="mode any ttl 255"
	else
		dport=${udpport}
		dproto=${fouproto}
	fi
	ip netns exec "${ns2}" ip fou add port $dport ipproto ${dproto}
	targs="encap fou encap-sport auto encap-dport $dport"
elif [[ "$tuntype" =~ "gre" && "$mac" == "eth" ]]; then
	ttype=$gretaptype
elif [[ "$tuntype" =~ "vxlan" && "$mac" == "eth" ]]; then
	ttype="vxlan"
	targs="id 1 dstport 8472 udp6zerocsumrx"
elif [[ "$tuntype" == "ipip6" ]]; then
	ttype="ip6tnl"
	targs=""
else
	ttype=$tuntype
	targs=""
fi

# tunnel address family differs from inner for SIT
if [[ "${tuntype}" == "sit" ]]; then
	link_addr1="${ns1_v4}"
	link_addr2="${ns2_v4}"
elif [[ "${tuntype}" == "ipip6" ]]; then
	link_addr1="${ns1_v6}"
	link_addr2="${ns2_v6}"
else
	link_addr1="${addr1}"
	link_addr2="${addr2}"
fi

# serverside, insert decap module
# server is still running
# client can connect again
ip netns exec "${ns2}" ip link add name testtun0 type "${ttype}" \
	${tmode} remote "${link_addr1}" local "${link_addr2}" $targs

expect_tun_fail=0

if [[ "$tuntype" == "ip6udp" && "$mac" == "mpls" ]]; then
	# No support for MPLS IPv6 fou tunnel; expect failure.
	expect_tun_fail=1
elif [[ "$tuntype" =~ "udp" && "$mac" == "eth" ]]; then
	# No support for TEB fou tunnel; expect failure.
	expect_tun_fail=1
elif [[ "$tuntype" =~ (gre|vxlan) && "$mac" == "eth" ]]; then
	# Share ethernet address between tunnel/veth2 so L2 decap works.
	ethaddr=$(ip netns exec "${ns2}" ip link show veth2 | \
		  awk '/ether/ { print $2 }')
	ip netns exec "${ns2}" ip link set testtun0 address $ethaddr
elif [[ "$mac" == "mpls" ]]; then
	modprobe mpls_iptunnel ||true
	modprobe mpls_gso ||true
	ip netns exec "${ns2}" sysctl -qw net.mpls.platform_labels=65536
	ip netns exec "${ns2}" ip -f mpls route add 1000 dev lo
	ip netns exec "${ns2}" ip link set lo up
	ip netns exec "${ns2}" sysctl -qw net.mpls.conf.testtun0.input=1
	ip netns exec "${ns2}" sysctl -qw net.ipv4.conf.lo.rp_filter=0
fi

# Because packets are decapped by the tunnel they arrive on testtun0 from
# the IP stack perspective.  Ensure reverse path filtering is disabled
# otherwise we drop the TCP SYN as arriving on testtun0 instead of the
# expected veth2 (veth2 is where 192.168.1.2 is configured).
ip netns exec "${ns2}" sysctl -qw net.ipv4.conf.all.rp_filter=0
# rp needs to be disabled for both all and testtun0 as the rp value is
# selected as the max of the "all" and device-specific values.
ip netns exec "${ns2}" sysctl -qw net.ipv4.conf.testtun0.rp_filter=0
ip netns exec "${ns2}" ip link set dev testtun0 up
if [[ "$expect_tun_fail" == 1 ]]; then
	# This tunnel mode is not supported, so we expect failure.
	echo "test bpf encap with tunnel device decap (expect failure)"
	! client_connect
else
	echo "test bpf encap with tunnel device decap"
	client_connect
	verify_data
	server_listen
fi

# serverside, use BPF for decap
ip netns exec "${ns2}" ip link del dev testtun0
ip netns exec "${ns2}" tc qdisc add dev veth2 clsact
ip netns exec "${ns2}" tc filter add dev veth2 ingress \
	bpf direct-action object-file ${BPF_FILE} section decap
echo "test bpf encap with bpf decap"
client_connect
verify_data

echo OK
