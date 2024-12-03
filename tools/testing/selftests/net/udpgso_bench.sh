#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgso benchmarks

readonly GREEN='\033[0;92m'
readonly YELLOW='\033[0;33m'
readonly RED='\033[0;31m'
readonly NC='\033[0m' # No Color
readonly TESTPORT=8000

readonly KSFT_PASS=0
readonly KSFT_FAIL=1
readonly KSFT_SKIP=4

num_pass=0
num_err=0
num_skip=0

kselftest_test_exitcode() {
	local -r exitcode=$1

	if [[ ${exitcode} -eq ${KSFT_PASS} ]]; then
		num_pass=$(( $num_pass + 1 ))
	elif [[ ${exitcode} -eq ${KSFT_SKIP} ]]; then
		num_skip=$(( $num_skip + 1 ))
	else
		num_err=$(( $num_err + 1 ))
	fi
}

kselftest_exit() {
	echo -e "$(basename $0): PASS=${num_pass} SKIP=${num_skip} FAIL=${num_err}"

	if [[ $num_err -ne 0 ]]; then
		echo -e "$(basename $0): ${RED}FAIL${NC}"
		exit ${KSFT_FAIL}
	fi

	if [[ $num_skip -ne 0 ]]; then
		echo -e "$(basename $0): ${YELLOW}SKIP${NC}"
		exit ${KSFT_SKIP}
	fi

	echo -e "$(basename $0): ${GREEN}PASS${NC}"
	exit ${KSFT_PASS}
}

wake_children() {
	local -r jobs="$(jobs -p)"

	if [[ "${jobs}" != "" ]]; then
		kill -1 ${jobs} 2>/dev/null
	fi
}
trap wake_children EXIT

run_one() {
	local -r args=$@
	local nr_socks=0
	local i=0
	local -r timeout=10

	./udpgso_bench_rx -p "$TESTPORT" &
	./udpgso_bench_rx -p "$TESTPORT" -t &

	# Wait for the above test program to get ready to receive connections.
	while [ "$i" -lt "$timeout" ]; do
		nr_socks="$(ss -lnHi | grep -c "\*:${TESTPORT}")"
		[ "$nr_socks" -eq 2 ] && break
		i=$((i + 1))
		sleep 1
	done
	if [ "$nr_socks" -ne 2 ]; then
		echo "timed out while waiting for udpgso_bench_rx"
		exit 1
	fi

	./udpgso_bench_tx -p "$TESTPORT" ${args}
}

run_in_netns() {
	local -r args=$@

	./in_netns.sh $0 __subprocess ${args}
	kselftest_test_exitcode $?
}

run_udp() {
	local -r args=$@

	echo "udp"
	run_in_netns ${args}

	echo "udp sendmmsg"
	run_in_netns ${args} -m

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
	run_tcp "${ipv6_args}"
	run_udp "${ipv6_args}"
}

if [[ $# -eq 0 ]]; then
	run_all
	kselftest_exit
elif [[ $1 == "__subprocess" ]]; then
	shift
	run_one $@
else
	run_in_netns $@
fi
