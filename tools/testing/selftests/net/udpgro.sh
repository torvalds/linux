#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgro functional tests.

readonly PEER_NS="ns-peer-$(mktemp -u XXXXXX)"

# set global exit status, but never reset nonzero one.
check_err()
{
	if [ $ret -eq 0 ]; then
		ret=$1
	fi
}

cleanup() {
	local -r jobs="$(jobs -p)"
	local -r ns="$(ip netns list|grep $PEER_NS)"

	[ -n "${jobs}" ] && kill -1 ${jobs} 2>/dev/null
	[ -n "$ns" ] && ip netns del $ns 2>/dev/null
}
trap cleanup EXIT

cfg_veth() {
	ip netns add "${PEER_NS}"
	ip -netns "${PEER_NS}" link set lo up
	ip link add type veth
	ip link set dev veth0 up
	ip addr add dev veth0 192.168.1.2/24
	ip addr add dev veth0 2001:db8::2/64 nodad

	ip link set dev veth1 netns "${PEER_NS}"
	ip -netns "${PEER_NS}" addr add dev veth1 192.168.1.1/24
	ip -netns "${PEER_NS}" addr add dev veth1 2001:db8::1/64 nodad
	ip -netns "${PEER_NS}" link set dev veth1 up
	ip -n "${PEER_NS}" link set veth1 xdp object ../bpf/xdp_dummy.o section xdp
}

run_one() {
	# use 'rx' as separator between sender args and receiver args
	local -r all="$@"
	local -r tx_args=${all%rx*}
	local -r rx_args=${all#*rx}

	cfg_veth

	ip netns exec "${PEER_NS}" ./udpgso_bench_rx -C 1000 -R 10 ${rx_args} && \
		echo "ok" || \
		echo "failed" &

	# Hack: let bg programs complete the startup
	sleep 0.2
	./udpgso_bench_tx ${tx_args}
	ret=$?
	wait $(jobs -p)
	return $ret
}

run_test() {
	local -r args=$@

	printf " %-40s" "$1"
	./in_netns.sh $0 __subprocess $2 rx -G -r $3
}

run_one_nat() {
	# use 'rx' as separator between sender args and receiver args
	local addr1 addr2 pid family="" ipt_cmd=ip6tables
	local -r all="$@"
	local -r tx_args=${all%rx*}
	local -r rx_args=${all#*rx}

	if [[ ${tx_args} = *-4* ]]; then
		ipt_cmd=iptables
		family=-4
		addr1=192.168.1.1
		addr2=192.168.1.3/24
	else
		addr1=2001:db8::1
		addr2="2001:db8::3/64 nodad"
	fi

	cfg_veth
	ip -netns "${PEER_NS}" addr add dev veth1 ${addr2}

	# fool the GRO engine changing the destination address ...
	ip netns exec "${PEER_NS}" $ipt_cmd -t nat -I PREROUTING -d ${addr1} -j DNAT --to-destination ${addr2%/*}

	# ... so that GRO will match the UDP_GRO enabled socket, but packets
	# will land on the 'plain' one
	ip netns exec "${PEER_NS}" ./udpgso_bench_rx -G ${family} -b ${addr1} -n 0 &
	pid=$!
	ip netns exec "${PEER_NS}" ./udpgso_bench_rx -C 1000 -R 10 ${family} -b ${addr2%/*} ${rx_args} && \
		echo "ok" || \
		echo "failed"&

	sleep 0.1
	./udpgso_bench_tx ${tx_args}
	ret=$?
	kill -INT $pid
	wait $(jobs -p)
	return $ret
}

run_one_2sock() {
	# use 'rx' as separator between sender args and receiver args
	local -r all="$@"
	local -r tx_args=${all%rx*}
	local -r rx_args=${all#*rx}

	cfg_veth

	ip netns exec "${PEER_NS}" ./udpgso_bench_rx -C 1000 -R 10 ${rx_args} -p 12345 &
	ip netns exec "${PEER_NS}" ./udpgso_bench_rx -C 2000 -R 10 ${rx_args} && \
		echo "ok" || \
		echo "failed" &

	# Hack: let bg programs complete the startup
	sleep 0.2
	./udpgso_bench_tx ${tx_args} -p 12345
	sleep 0.1
	# first UDP GSO socket should be closed at this point
	./udpgso_bench_tx ${tx_args}
	ret=$?
	wait $(jobs -p)
	return $ret
}

run_nat_test() {
	local -r args=$@

	printf " %-40s" "$1"
	./in_netns.sh $0 __subprocess_nat $2 rx -r $3
}

run_2sock_test() {
	local -r args=$@

	printf " %-40s" "$1"
	./in_netns.sh $0 __subprocess_2sock $2 rx -G -r $3
}

run_all() {
	local -r core_args="-l 4"
	local -r ipv4_args="${core_args} -4 -D 192.168.1.1"
	local -r ipv6_args="${core_args} -6 -D 2001:db8::1"
	ret=0

	echo "ipv4"
	run_test "no GRO" "${ipv4_args} -M 10 -s 1400" "-4 -n 10 -l 1400"
	check_err $?

	# explicitly check we are not receiving UDP_SEGMENT cmsg (-S -1)
	# when GRO does not take place
	run_test "no GRO chk cmsg" "${ipv4_args} -M 10 -s 1400" "-4 -n 10 -l 1400 -S -1"
	check_err $?

	# the GSO packets are aggregated because:
	# * veth schedule napi after each xmit
	# * segmentation happens in BH context, veth napi poll is delayed after
	#   the transmission of the last segment
	run_test "GRO" "${ipv4_args} -M 1 -s 14720 -S 0 " "-4 -n 1 -l 14720"
	check_err $?
	run_test "GRO chk cmsg" "${ipv4_args} -M 1 -s 14720 -S 0 " "-4 -n 1 -l 14720 -S 1472"
	check_err $?
	run_test "GRO with custom segment size" "${ipv4_args} -M 1 -s 14720 -S 500 " "-4 -n 1 -l 14720"
	check_err $?
	run_test "GRO with custom segment size cmsg" "${ipv4_args} -M 1 -s 14720 -S 500 " "-4 -n 1 -l 14720 -S 500"
	check_err $?

	run_nat_test "bad GRO lookup" "${ipv4_args} -M 1 -s 14720 -S 0" "-n 10 -l 1472"
	check_err $?
	run_2sock_test "multiple GRO socks" "${ipv4_args} -M 1 -s 14720 -S 0 " "-4 -n 1 -l 14720 -S 1472"
	check_err $?

	echo "ipv6"
	run_test "no GRO" "${ipv6_args} -M 10 -s 1400" "-n 10 -l 1400"
	check_err $?
	run_test "no GRO chk cmsg" "${ipv6_args} -M 10 -s 1400" "-n 10 -l 1400 -S -1"
	check_err $?
	run_test "GRO" "${ipv6_args} -M 1 -s 14520 -S 0" "-n 1 -l 14520"
	check_err $?
	run_test "GRO chk cmsg" "${ipv6_args} -M 1 -s 14520 -S 0" "-n 1 -l 14520 -S 1452"
	check_err $?
	run_test "GRO with custom segment size" "${ipv6_args} -M 1 -s 14520 -S 500" "-n 1 -l 14520"
	check_err $?
	run_test "GRO with custom segment size cmsg" "${ipv6_args} -M 1 -s 14520 -S 500" "-n 1 -l 14520 -S 500"
	check_err $?

	run_nat_test "bad GRO lookup" "${ipv6_args} -M 1 -s 14520 -S 0" "-n 10 -l 1452"
	check_err $?
	run_2sock_test "multiple GRO socks" "${ipv6_args} -M 1 -s 14520 -S 0 " "-n 1 -l 14520 -S 1452"
	check_err $?
	return $ret
}

if [ ! -f ../bpf/xdp_dummy.o ]; then
	echo "Missing xdp_dummy helper. Build bpf selftest first"
	exit -1
fi

if [[ $# -eq 0 ]]; then
	run_all
elif [[ $1 == "__subprocess" ]]; then
	shift
	run_one $@
elif [[ $1 == "__subprocess_nat" ]]; then
	shift
	run_one_nat $@
elif [[ $1 == "__subprocess_2sock" ]]; then
	shift
	run_one_2sock $@
fi

exit $?
