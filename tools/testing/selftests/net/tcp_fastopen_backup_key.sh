#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# rotate TFO keys for ipv4/ipv6 and verify that the client does
# not present an invalid cookie.

set +x
set -e

readonly NETNS="ns-$(mktemp -u XXXXXX)"

setup() {
	ip netns add "${NETNS}"
	ip -netns "${NETNS}" link set lo up
	ip netns exec "${NETNS}" sysctl -w net.ipv4.tcp_fastopen=3 \
		>/dev/null 2>&1
}

cleanup() {
	ip netns del "${NETNS}"
}

trap cleanup EXIT
setup

do_test() {
	# flush routes before each run, otherwise successive runs can
	# initially present an old TFO cookie
	ip netns exec "${NETNS}" ip tcp_metrics flush
	ip netns exec "${NETNS}" ./tcp_fastopen_backup_key "$1"
	val=$(ip netns exec "${NETNS}" nstat -az | \
		grep TcpExtTCPFastOpenPassiveFail | awk '{print $2}')
	if [ $val -ne 0 ]; then
		echo "FAIL: TcpExtTCPFastOpenPassiveFail non-zero"
		return 1
	fi
}

do_test "-4"
do_test "-6"
do_test "-4"
do_test "-6"
do_test "-4s"
do_test "-6s"
do_test "-4s"
do_test "-6s"
do_test "-4r"
do_test "-6r"
do_test "-4r"
do_test "-6r"
do_test "-4sr"
do_test "-6sr"
do_test "-4sr"
do_test "-6sr"
echo "all tests done"
