#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgso benchmarks

wake_children() {
	local -r jobs="$(jobs -p)"

	if [[ "${jobs}" != "" ]]; then
		kill -1 ${jobs} 2>/dev/null
	fi
}
trap wake_children EXIT

run_one() {
	local -r args=$@

	./udpgso_bench_rx &
	./udpgso_bench_rx -t &

	./udpgso_bench_tx ${args}
}

run_in_netns() {
	local -r args=$@

	./in_netns.sh $0 __subprocess ${args}
}

run_udp() {
	local -r args=$@

	echo "udp"
	run_in_netns ${args}

	echo "udp gso"
	run_in_netns ${args} -S 0

	echo "udp gso zerocopy"
	run_in_netns ${args} -S 0 -z

	echo "udp gso timestamp"
	run_in_netns ${args} -S 0 -T

	echo "udp gso zerocopy audit"
	run_in_netns ${args} -S 0 -z -a

	echo "udp gso timestamp audit"
	run_in_netns ${args} -S 0 -T -a

	echo "udp gso zerocopy timestamp audit"
	run_in_netns ${args} -S 0 -T -z -a
}

run_tcp() {
	local -r args=$@

	echo "tcp"
	run_in_netns ${args} -t

	echo "tcp zerocopy"
	run_in_netns ${args} -t -z

	# excluding for now because test fails intermittently
	# add -P option to include poll() to reduce possibility of lost messages
	#echo "tcp zerocopy audit"
	#run_in_netns ${args} -t -z -P -a
}

run_all() {
	local -r core_args="-l 3"
	local -r ipv4_args="${core_args} -4 -D 127.0.0.1"
	local -r ipv6_args="${core_args} -6 -D ::1"

	echo "ipv4"
	run_tcp "${ipv4_args}"
	run_udp "${ipv4_args}"

	echo "ipv6"
	run_tcp "${ipv4_args}"
	run_udp "${ipv6_args}"
}

if [[ $# -eq 0 ]]; then
	run_all
elif [[ $1 == "__subprocess" ]]; then
	shift
	run_one $@
else
	run_in_netns $@
fi
