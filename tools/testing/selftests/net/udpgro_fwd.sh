#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source net_helper.sh

BPF_FILE="lib/xdp_dummy.bpf.o"
readonly BASE="ns-$(mktemp -u XXXXXX)"
readonly SRC=2
readonly DST=1
readonly DST_NAT=100
readonly NS_SRC=$BASE$SRC
readonly NS_DST=$BASE$DST

# "baremetal" network used for raw UDP traffic
readonly BM_NET_V4=192.168.1.
readonly BM_NET_V6=2001:db8::

# "overlay" network used for UDP over UDP tunnel traffic
readonly OL_NET_V4=172.16.1.
readonly OL_NET_V6=2001:db8:1::
readonly NPROCS=`nproc`

cleanup() {
	local ns
	local -r jobs="$(jobs -p)"
	[ -n "${jobs}" ] && kill -1 ${jobs} 2>/dev/null

	for ns in $NS_SRC $NS_DST; do
		ip netns del $ns 2>/dev/null
	done
}

trap cleanup EXIT

create_ns() {
	local net
	local ns

	for ns in $NS_SRC $NS_DST; do
		ip netns add $ns
		ip -n $ns link set dev lo up

		# disable route solicitations to decrease 'noise' traffic
		ip netns exec $ns sysctl -qw net.ipv6.conf.default.router_solicitations=0
		ip netns exec $ns sysctl -qw net.ipv6.conf.all.router_solicitations=0
	done

	ip link add name veth$SRC type veth peer name veth$DST

	for ns in $SRC $DST; do
		ip link set dev veth$ns netns $BASE$ns
		ip -n $BASE$ns link set dev veth$ns up
		ip -n $BASE$ns addr add dev veth$ns $BM_NET_V4$ns/24
		ip -n $BASE$ns addr add dev veth$ns $BM_NET_V6$ns/64 nodad
	done
	ip -n $NS_DST link set veth$DST xdp object ${BPF_FILE} section xdp 2>/dev/null
}

create_vxlan_endpoint() {
	local -r netns=$1
	local -r bm_dev=$2
	local -r bm_rem_addr=$3
	local -r vxlan_dev=$4
	local -r vxlan_id=$5
	local -r vxlan_port=4789

	ip -n $netns link set dev $bm_dev up
	ip -n $netns link add dev $vxlan_dev type vxlan id $vxlan_id \
				dstport $vxlan_port remote $bm_rem_addr
	ip -n $netns link set dev $vxlan_dev up
}

create_vxlan_pair() {
	local ns

	create_ns

	for ns in $SRC $DST; do
		# note that 3 - $SRC == $DST and 3 - $DST == $SRC
		create_vxlan_endpoint $BASE$ns veth$ns $BM_NET_V4$((3 - $ns)) vxlan$ns 4
		ip -n $BASE$ns addr add dev vxlan$ns $OL_NET_V4$ns/24
	done
	for ns in $SRC $DST; do
		create_vxlan_endpoint $BASE$ns veth$ns $BM_NET_V6$((3 - $ns)) vxlan6$ns 6
		ip -n $BASE$ns addr add dev vxlan6$ns $OL_NET_V6$ns/24 nodad
	done

	# preload neighbur cache, do avoid some noisy traffic
	local addr_dst=$(ip -j -n $BASE$DST link show dev vxlan6$DST  |jq -r '.[]["address"]')
	local addr_src=$(ip -j -n $BASE$SRC link show dev vxlan6$SRC  |jq -r '.[]["address"]')
	ip -n $BASE$DST neigh add dev vxlan6$DST lladdr $addr_src $OL_NET_V6$SRC
	ip -n $BASE$SRC neigh add dev vxlan6$SRC lladdr $addr_dst $OL_NET_V6$DST
}

is_ipv6() {
	if [[ $1 =~ .*:.* ]]; then
		return 0
	fi
	return 1
}

run_test() {
	local -r msg=$1
	local -r dst=$2
	local -r pkts=$3
	local -r vxpkts=$4
	local bind=$5
	local rx_args=""
	local rx_family="-4"
	local family=-4
	local filter=IpInReceives
	local ipt=iptables

	printf "%-40s" "$msg"

	if is_ipv6 $dst; then
		# rx program does not support '-6' and implies ipv6 usage by default
		rx_family=""
		family=-6
		filter=Ip6InReceives
		ipt=ip6tables
	fi

	rx_args="$rx_family"
	[ -n "$bind" ] && rx_args="$rx_args -b $bind"

	# send a single GSO packet, segmented in 10 UDP frames.
	# Always expect 10 UDP frames on RX side as rx socket does
	# not enable GRO
	ip netns exec $NS_DST $ipt -A INPUT -p udp --dport 4789
	ip netns exec $NS_DST $ipt -A INPUT -p udp --dport 8000
	ip netns exec $NS_DST ./udpgso_bench_rx -C 2000 -R 100 -n 10 -l 1300 $rx_args &
	local spid=$!
	wait_local_port_listen "$NS_DST" 8000 udp
	ip netns exec $NS_SRC ./udpgso_bench_tx $family -M 1 -s 13000 -S 1300 -D $dst
	local retc=$?
	wait $spid
	local rets=$?
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " fail client exit code $retc, server $rets"
		ret=1
		return
	fi

	local rcv=`ip netns exec $NS_DST $ipt"-save" -c | grep 'dport 8000' | \
							  sed -e 's/\[//' -e 's/:.*//'`
	if [ $rcv != $pkts ]; then
		echo " fail - received $rcv packets, expected $pkts"
		ret=1
		return
	fi

	local vxrcv=`ip netns exec $NS_DST $ipt"-save" -c | grep 'dport 4789' | \
							    sed -e 's/\[//' -e 's/:.*//'`

	# upper net can generate a little noise, allow some tolerance
	if [ $vxrcv -lt $vxpkts -o $vxrcv -gt $((vxpkts + 3)) ]; then
		echo " fail - received $vxrcv vxlan packets, expected $vxpkts"
		ret=1
		return
	fi
	echo " ok"
}

run_bench() {
	local -r msg=$1
	local -r dst=$2
	local family=-4

	printf "%-40s" "$msg"
	if [ $NPROCS -lt 2 ]; then
		echo " skip - needed 2 CPUs found $NPROCS"
		return
	fi

	is_ipv6 $dst && family=-6

	# bind the sender and the receiver to different CPUs to try
	# get reproducible results
	ip netns exec $NS_DST bash -c "echo 2 > /sys/class/net/veth$DST/queues/rx-0/rps_cpus"
	ip netns exec $NS_DST taskset 0x2 ./udpgso_bench_rx -C 2000 -R 100  &
	local spid=$!
	wait_local_port_listen "$NS_DST" 8000 udp
	ip netns exec $NS_SRC taskset 0x1 ./udpgso_bench_tx $family -l 3 -S 1300 -D $dst
	local retc=$?
	wait $spid
	local rets=$?
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " fail client exit code $retc, server $rets"
		ret=1
		return
	fi
}

for family in 4 6; do
	BM_NET=$BM_NET_V4
	OL_NET=$OL_NET_V4
	IPT=iptables
	SUFFIX=24
	VXDEV=vxlan
	PING=ping

	if [ $family = 6 ]; then
		BM_NET=$BM_NET_V6
		OL_NET=$OL_NET_V6
		SUFFIX="64 nodad"
		VXDEV=vxlan6
		IPT=ip6tables
		# Use ping6 on systems where ping doesn't handle IPv6
		ping -w 1 -c 1 ::1 > /dev/null 2>&1 || PING="ping6"
	fi

	echo "IPv$family"

	create_ns
	run_test "No GRO" $BM_NET$DST 10 0
	cleanup

	create_ns
	ip netns exec $NS_DST ethtool -K veth$DST generic-receive-offload on
	ip netns exec $NS_DST ethtool -K veth$DST rx-gro-list on
	run_test "GRO frag list" $BM_NET$DST 1 0
	cleanup

	# UDP GRO fwd skips aggregation when find an udp socket with the GRO option
	# if there is an UDP tunnel in the running system, such lookup happen
	# take place.
	# use NAT to circumvent GRO FWD check
	create_ns
	ip -n $NS_DST addr add dev veth$DST $BM_NET$DST_NAT/$SUFFIX
	ip netns exec $NS_DST ethtool -K veth$DST generic-receive-offload on
	ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
	ip netns exec $NS_DST $IPT -t nat -I PREROUTING -d $BM_NET$DST_NAT \
					-j DNAT --to-destination $BM_NET$DST
	run_test "GRO fwd" $BM_NET$DST_NAT 1 0 $BM_NET$DST
	cleanup

	create_ns
	run_bench "UDP fwd perf" $BM_NET$DST
	ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
	run_bench "UDP GRO fwd perf" $BM_NET$DST
	cleanup

	create_vxlan_pair
	ip netns exec $NS_DST ethtool -K veth$DST generic-receive-offload on
	ip netns exec $NS_DST ethtool -K veth$DST rx-gro-list on
	run_test "GRO frag list over UDP tunnel" $OL_NET$DST 10 10
	cleanup

	# use NAT to circumvent GRO FWD check
	create_vxlan_pair
	ip -n $NS_DST addr add dev $VXDEV$DST $OL_NET$DST_NAT/$SUFFIX
	ip netns exec $NS_DST ethtool -K veth$DST generic-receive-offload on
	ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
	ip netns exec $NS_DST $IPT -t nat -I PREROUTING -d $OL_NET$DST_NAT \
					-j DNAT --to-destination $OL_NET$DST

	# load arp cache before running the test to reduce the amount of
	# stray traffic on top of the UDP tunnel
	ip netns exec $NS_SRC $PING -q -c 1 $OL_NET$DST_NAT >/dev/null
	run_test "GRO fwd over UDP tunnel" $OL_NET$DST_NAT 10 10 $OL_NET$DST
	cleanup
done

exit $ret
