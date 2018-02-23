#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Execute a subprocess in a network namespace

set -e

readonly NETNS="ns-$(mktemp -u XXXXXX)"

setup() {
	ip netns add "${NETNS}"
	ip -netns "${NETNS}" link set lo up
}

cleanup() {
	ip netns del "${NETNS}"
}

trap cleanup EXIT
setup

"$@"
exit "$?"
