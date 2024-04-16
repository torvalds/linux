#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run tests when all ephemeral ports are exhausted.
#
# Author: Kuniyuki Iwashima <kuniyu@amazon.co.jp>

set +x
set -e

readonly NETNS="ns-$(mktemp -u XXXXXX)"

setup() {
	ip netns add "${NETNS}"
	ip -netns "${NETNS}" link set lo up
	ip netns exec "${NETNS}" \
		sysctl -w net.ipv4.ip_local_port_range="32768 32768" \
		> /dev/null 2>&1
	ip netns exec "${NETNS}" \
		sysctl -w net.ipv4.ip_autobind_reuse=1 > /dev/null 2>&1
}

cleanup() {
	ip netns del "${NETNS}"
}

trap cleanup EXIT
setup

do_test() {
	ip netns exec "${NETNS}" ./reuseaddr_ports_exhausted
}

do_test
echo "tests done"
