#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

BPF_FILE="xdp_dummy.o"
readonly STATS="$(mktemp -p /tmp ns-XXXXXX)"
readonly BASE=`basename $STATS`
readonly SRC=2
readonly DST=1
readonly DST_NAT=100
readonly NS_SRC=$BASE$SRC
readonly NS_DST=$BASE$DST

# "baremetal" network used for raw UDP traffic
readonly BM_NET_V4=192.168.1.
readonly BM_NET_V6=2001:db8::

readonly CPUS=`nproc`
ret=0

cleanup() {
	local ns
	local jobs
	readonly jobs="$(jobs -p)"
	[ -n "${jobs}" ] && kill -1 ${jobs} 2>/dev/null
	rm -f $STATS

	for ns in $NS_SRC $NS_DST; do
		ip netns del $ns 2>/dev/null
	done
}

trap cleanup EXIT

create_ns() {
	local ns

	for ns in $NS_SRC $NS_DST; do
		ip netns add $ns
		ip -n $ns link set dev lo up
	done

	ip link add name veth$SRC type veth peer name veth$DST

	for ns in $SRC $DST; do
		ip link set dev veth$ns netns $BASE$ns up
		ip -n $BASE$ns addr add dev veth$ns $BM_NET_V4$ns/24
		ip -n $BASE$ns addr add dev veth$ns $BM_NET_V6$ns/64 nodad
	done
	echo "#kernel" > $BASE
	chmod go-rw $BASE
}

__chk_flag() {
	local msg="$1"
	local target=$2
	local expected=$3
	local flagname=$4

	local flag=`ip netns exec $BASE$target ethtool -k veth$target |\
		    grep $flagname | awk '{print $2}'`

	printf "%-60s" "$msg"
	if [ "$flag" = "$expected" ]; then
		echo " ok "
	else
		echo " fail - expected $expected found $flag"
		ret=1
	fi
}

chk_gro_flag() {
	__chk_flag "$1" $2 $3 generic-receive-offload
}

chk_tso_flag() {
	__chk_flag "$1" $2 $3 tcp-segmentation-offload
}

chk_channels() {
	local msg="$1"
	local target=$2
	local rx=$3
	local tx=$4

	local dev=veth$target

	local cur_rx=`ip netns exec $BASE$target ethtool -l $dev |\
		grep RX: | tail -n 1 | awk '{print $2}' `
		local cur_tx=`ip netns exec $BASE$target ethtool -l $dev |\
		grep TX: | tail -n 1 | awk '{print $2}'`
	local cur_combined=`ip netns exec $BASE$target ethtool -l $dev |\
		grep Combined: | tail -n 1 | awk '{print $2}'`

	printf "%-60s" "$msg"
	if [ "$cur_rx" = "$rx" -a "$cur_tx" = "$tx" -a "$cur_combined" = "n/a" ]; then
		echo " ok "
	else
		echo " fail rx:$rx:$cur_rx tx:$tx:$cur_tx combined:n/a:$cur_combined"
	fi
}

chk_gro() {
	local msg="$1"
	local expected=$2

	ip netns exec $BASE$SRC ping -qc 1 $BM_NET_V4$DST >/dev/null
	NSTAT_HISTORY=$STATS ip netns exec $NS_DST nstat -n

	printf "%-60s" "$msg"
	ip netns exec $BASE$DST ./udpgso_bench_rx -C 1000 -R 10 &
	local spid=$!
	sleep 0.1

	ip netns exec $NS_SRC ./udpgso_bench_tx -4 -s 13000 -S 1300 -M 1 -D $BM_NET_V4$DST
	local retc=$?
	wait $spid
	local rets=$?
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " fail client exit code $retc, server $rets"
		ret=1
		return
	fi

	local pkts=`NSTAT_HISTORY=$STATS ip netns exec $NS_DST nstat IpInReceives | \
		    awk '{print $2}' | tail -n 1`
	if [ "$pkts" = "$expected" ]; then
		echo " ok "
	else
		echo " fail - got $pkts packets, expected $expected "
		ret=1
	fi
}

__change_channels()
{
	local cur_cpu
	local end=$1
	local cur
	local i

	while true; do
		printf -v cur '%(%s)T'
		[ $cur -le $end ] || break

		for i in `seq 1 $CPUS`; do
			ip netns exec $NS_SRC ethtool -L veth$SRC rx $i tx $i
			ip netns exec $NS_DST ethtool -L veth$DST rx $i tx $i
		done

		for i in `seq 1 $((CPUS - 1))`; do
			cur_cpu=$((CPUS - $i))
			ip netns exec $NS_SRC ethtool -L veth$SRC rx $cur_cpu tx $cur_cpu
			ip netns exec $NS_DST ethtool -L veth$DST rx $cur_cpu tx $cur_cpu
		done
	done
}

__send_data() {
	local end=$1

	while true; do
		printf -v cur '%(%s)T'
		[ $cur -le $end ] || break

		ip netns exec $NS_SRC ./udpgso_bench_tx -4 -s 1000 -M 300 -D $BM_NET_V4$DST
	done
}

do_stress() {
	local end
	printf -v end '%(%s)T'
	end=$((end + $STRESS))

	ip netns exec $NS_SRC ethtool -L veth$SRC rx 3 tx 3
	ip netns exec $NS_DST ethtool -L veth$DST rx 3 tx 3

	ip netns exec $NS_DST ./udpgso_bench_rx &
	local rx_pid=$!

	echo "Running stress test for $STRESS seconds..."
	__change_channels $end &
	local ch_pid=$!
	__send_data $end &
	local data_pid_1=$!
	__send_data $end &
	local data_pid_2=$!
	__send_data $end &
	local data_pid_3=$!
	__send_data $end &
	local data_pid_4=$!

	wait $ch_pid $data_pid_1 $data_pid_2 $data_pid_3 $data_pid_4
	kill -9 $rx_pid
	echo "done"

	# restore previous setting
	ip netns exec $NS_SRC ethtool -L veth$SRC rx 2 tx 2
	ip netns exec $NS_DST ethtool -L veth$DST rx 2 tx 1
}

usage() {
	echo "Usage: $0 [-h] [-s <seconds>]"
	echo -e "\t-h: show this help"
	echo -e "\t-s: run optional stress tests for the given amount of seconds"
}

STRESS=0
while getopts "hs:" option; do
	case "$option" in
	"h")
		usage $0
		exit 0
		;;
	"s")
		STRESS=$OPTARG
		;;
	esac
done

if [ ! -f ${BPF_FILE} ]; then
	echo "Missing ${BPF_FILE}. Run 'make' first"
	exit 1
fi

[ $CPUS -lt 2 ] && echo "Only one CPU available, some tests will be skipped"
[ $STRESS -gt 0 -a $CPUS -lt 3 ] && echo " stress test will be skipped, too"

create_ns
chk_gro_flag "default - gro flag" $SRC off
chk_gro_flag "        - peer gro flag" $DST off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on
chk_gro "        - aggregation" 1
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
chk_gro "        - aggregation with TSO off" 10
cleanup

create_ns
ip netns exec $NS_DST ethtool -K veth$DST gro on
chk_gro_flag "with gro on - gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
chk_gro "        - aggregation with TSO off" 1
cleanup

create_ns
ip -n $NS_DST link set dev veth$DST up
ip -n $NS_DST link set dev veth$DST xdp object ${BPF_FILE} section xdp
chk_gro_flag "gro vs xdp while down - gro flag off" $DST off
ip -n $NS_DST link set dev veth$DST down
chk_gro_flag "                      - after down" $DST off
ip -n $NS_DST link set dev veth$DST xdp off
chk_gro_flag "                      - after xdp off" $DST off
ip -n $NS_DST link set dev veth$DST up
chk_gro_flag "                      - after up" $DST off
ip -n $NS_SRC link set dev veth$SRC xdp object ${BPF_FILE} section xdp
chk_gro_flag "                      - after peer xdp" $DST off
cleanup

create_ns
ip -n $NS_DST link set dev veth$DST up
ip -n $NS_DST link set dev veth$DST xdp object ${BPF_FILE} section xdp
ip netns exec $NS_DST ethtool -K veth$DST generic-receive-offload on
chk_gro_flag "gro vs xdp while down - gro flag on" $DST on
ip -n $NS_DST link set dev veth$DST down
chk_gro_flag "                      - after down" $DST on
ip -n $NS_DST link set dev veth$DST xdp off
chk_gro_flag "                      - after xdp off" $DST on
ip -n $NS_DST link set dev veth$DST up
chk_gro_flag "                      - after up" $DST on
ip -n $NS_SRC link set dev veth$SRC xdp object ${BPF_FILE} section xdp
chk_gro_flag "                      - after peer xdp" $DST on
cleanup

create_ns
chk_channels "default channels" $DST 1 1

ip -n $NS_DST link set dev veth$DST down
ip netns exec $NS_DST ethtool -K veth$DST gro on
chk_gro_flag "with gro enabled on link down - gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on
ip -n $NS_DST link set dev veth$DST up
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
chk_gro "        - aggregation with TSO off" 1
cleanup

create_ns

CUR_TX=1
CUR_RX=1
if [ $CPUS -gt 1 ]; then
	ip netns exec $NS_DST ethtool -L veth$DST tx 2
	chk_channels "setting tx channels" $DST 1 2
	CUR_TX=2
fi

if [ $CPUS -gt 2 ]; then
	ip netns exec $NS_DST ethtool -L veth$DST rx 3 tx 3
	chk_channels "setting both rx and tx channels" $DST 3 3
	CUR_RX=3
	CUR_TX=3
fi

ip netns exec $NS_DST ethtool -L veth$DST combined 2 2>/dev/null
chk_channels "bad setting: combined channels" $DST $CUR_RX $CUR_TX

ip netns exec $NS_DST ethtool -L veth$DST tx $((CPUS + 1)) 2>/dev/null
chk_channels "setting invalid channels nr" $DST $CUR_RX $CUR_TX

if [ $CPUS -gt 1 ]; then
	# this also tests queues nr reduction
	ip netns exec $NS_DST ethtool -L veth$DST rx 1 tx 2 2>/dev/null
	ip netns exec $NS_SRC ethtool -L veth$SRC rx 1 tx 2 2>/dev/null
	printf "%-60s" "bad setting: XDP with RX nr less than TX"
	ip -n $NS_DST link set dev veth$DST xdp object ${BPF_FILE} \
		section xdp 2>/dev/null &&\
		echo "fail - set operation successful ?!?" || echo " ok "

	# the following tests will run with multiple channels active
	ip netns exec $NS_SRC ethtool -L veth$SRC rx 2
	ip netns exec $NS_DST ethtool -L veth$DST rx 2
	ip -n $NS_DST link set dev veth$DST xdp object ${BPF_FILE} \
		section xdp 2>/dev/null
	printf "%-60s" "bad setting: reducing RX nr below peer TX with XDP set"
	ip netns exec $NS_DST ethtool -L veth$DST rx 1 2>/dev/null &&\
		echo "fail - set operation successful ?!?" || echo " ok "
	CUR_RX=2
	CUR_TX=2
fi

if [ $CPUS -gt 2 ]; then
	printf "%-60s" "bad setting: increasing peer TX nr above RX with XDP set"
	ip netns exec $NS_SRC ethtool -L veth$SRC tx 3 2>/dev/null &&\
		echo "fail - set operation successful ?!?" || echo " ok "
	chk_channels "setting invalid channels nr" $DST 2 2
fi

ip -n $NS_DST link set dev veth$DST xdp object ${BPF_FILE} section xdp 2>/dev/null
chk_gro_flag "with xdp attached - gro flag" $DST off
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC off
chk_tso_flag "        - peer tso flag" $DST on
ip netns exec $NS_DST ethtool -K veth$DST rx-udp-gro-forwarding on
chk_gro "        - no aggregation" 10
ip netns exec $NS_DST ethtool -K veth$DST generic-receive-offload on
chk_gro_flag "        - gro flag with GRO on" $DST on
chk_gro "        - aggregation" 1


ip -n $NS_DST link set dev veth$DST down
ip -n $NS_SRC link set dev veth$SRC down
chk_gro_flag "        - after dev off, flag" $DST on
chk_gro_flag "        - peer flag" $SRC off

ip netns exec $NS_DST ethtool -K veth$DST gro on
ip -n $NS_DST link set dev veth$DST xdp off
chk_gro_flag "        - after gro on xdp off, gro flag" $DST on
chk_gro_flag "        - peer gro flag" $SRC off
chk_tso_flag "        - tso flag" $SRC on
chk_tso_flag "        - peer tso flag" $DST on

if [ $CPUS -gt 1 ]; then
	ip netns exec $NS_DST ethtool -L veth$DST tx 1
	chk_channels "decreasing tx channels with device down" $DST 2 1
fi

ip -n $NS_DST link set dev veth$DST up
ip -n $NS_SRC link set dev veth$SRC up
chk_gro "        - aggregation" 1

if [ $CPUS -gt 1 ]; then
	[ $STRESS -gt 0 -a $CPUS -gt 2 ] && do_stress

	ip -n $NS_DST link set dev veth$DST down
	ip -n $NS_SRC link set dev veth$SRC down
	ip netns exec $NS_DST ethtool -L veth$DST tx 2
	chk_channels "increasing tx channels with device down" $DST 2 2
	ip -n $NS_DST link set dev veth$DST up
	ip -n $NS_SRC link set dev veth$SRC up
fi

ip netns exec $NS_DST ethtool -K veth$DST gro off
ip netns exec $NS_SRC ethtool -K veth$SRC tx-udp-segmentation off
chk_gro "aggregation again with default and TSO off" 10

exit $ret
