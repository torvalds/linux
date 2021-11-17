#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Send packets with transmit timestamps over loopback with netem
# Verify that timestamps correspond to netem delay

set -e

setup() {
	# set 1ms delay on lo egress
	tc qdisc add dev lo root netem delay 1ms

	# set 2ms delay on ifb0 egress
	modprobe ifb
	ip link add ifb_netem0 type ifb
	ip link set dev ifb_netem0 up
	tc qdisc add dev ifb_netem0 root netem delay 2ms

	# redirect lo ingress through ifb0 egress
	tc qdisc add dev lo handle ffff: ingress
	tc filter add dev lo parent ffff: \
		u32 match mark 0 0xffff \
		action mirred egress redirect dev ifb_netem0
}

run_test_v4v6() {
	# SND will be delayed 1000us
	# ACK will be delayed 6000us: 1 + 2 ms round-trip
	local -r args="$@ -v 1000 -V 6000"

	./txtimestamp ${args} -4 -L 127.0.0.1
	./txtimestamp ${args} -6 -L ::1
}

run_test_tcpudpraw() {
	local -r args=$@

	run_test_v4v6 ${args}		# tcp
	run_test_v4v6 ${args} -u	# udp
	run_test_v4v6 ${args} -r	# raw
	run_test_v4v6 ${args} -R	# raw (IPPROTO_RAW)
	run_test_v4v6 ${args} -P	# pf_packet
}

run_test_all() {
	setup
	run_test_tcpudpraw		# setsockopt
	run_test_tcpudpraw -C		# cmsg
	run_test_tcpudpraw -n		# timestamp w/o data
	echo "OK. All tests passed"
}

run_test_one() {
	setup
	./txtimestamp $@
}

usage() {
	echo "Usage: $0 [ -r | --run ] <txtimestamp args> | [ -h | --help ]"
	echo "  (no args)  Run all tests"
	echo "  -r|--run  Run an individual test with arguments"
	echo "  -h|--help Help"
}

main() {
	if [[ $# -eq 0 ]]; then
		run_test_all
	else
		if [[ "$1" = "-r" || "$1" == "--run" ]]; then
			shift
			run_test_one $@
		else
			usage
		fi
	fi
}

if [[ -z "$(ip netns identify)" ]]; then
	./in_netns.sh $0 $@
else
	main $@
fi
