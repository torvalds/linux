#!/bin/bash -e
# SPDX-License-Identifier: GPL-2.0
#
# This test checks that the network buffer sysctls are present
# in a network namespaces, and that they are readonly.

source lib.sh

cleanup() {
    cleanup_ns $test_ns
}

trap cleanup EXIT

fail() {
	echo "ERROR: $*" >&2
	exit 1
}

setup_ns test_ns

for sc in {r,w}mem_{default,max}; do
	# check that this is writable in a netns
	[ -w "/proc/sys/net/core/$sc" ] ||
		fail "$sc isn't writable in the init netns!"

	# change the value in the host netns
	sysctl -qw "net.core.$sc=300000" ||
		fail "Can't write $sc in init netns!"

	# check that the value is read from the init netns
	[ "$(ip netns exec $test_ns sysctl -n "net.core.$sc")" -eq 300000 ] ||
		fail "Value for $sc mismatch!"

	# check that this isn't writable in a netns
	ip netns exec $test_ns [ -w "/proc/sys/net/core/$sc" ] &&
		fail "$sc is writable in a netns!"
done

echo 'Test passed OK'
