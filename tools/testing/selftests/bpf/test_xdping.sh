#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# xdping tests
#   Here we setup and teardown configuration required to run
#   xdping, exercising its options.
#
#   Setup is similar to test_tunnel tests but without the tunnel.
#
# Topology:
# ---------
#     root namespace   |     tc_ns0 namespace
#                      |
#      ----------      |     ----------
#      |  veth1  | --------- |  veth0  |
#      ----------    peer    ----------
#
# Device Configuration
# --------------------
# Root namespace with BPF
# Device names and addresses:
#	veth1 IP: 10.1.1.200
#	xdp added to veth1, xdpings originate from here.
#
# Namespace tc_ns0 with BPF
# Device names and addresses:
#       veth0 IPv4: 10.1.1.100
#	For some tests xdping run in server mode here.
#

readonly TARGET_IP="10.1.1.100"
readonly TARGET_NS="xdp_ns0"

readonly LOCAL_IP="10.1.1.200"

setup()
{
	ip netns add $TARGET_NS
	ip link add veth0 type veth peer name veth1
	ip link set veth0 netns $TARGET_NS
	ip netns exec $TARGET_NS ip addr add ${TARGET_IP}/24 dev veth0
	ip addr add ${LOCAL_IP}/24 dev veth1
	ip netns exec $TARGET_NS ip link set veth0 up
	ip link set veth1 up
}

cleanup()
{
	set +e
	ip netns delete $TARGET_NS 2>/dev/null
	ip link del veth1 2>/dev/null
	if [[ $server_pid -ne 0 ]]; then
		kill -TERM $server_pid
	fi
}

test()
{
	client_args="$1"
	server_args="$2"

	echo "Test client args '$client_args'; server args '$server_args'"

	server_pid=0
	if [[ -n "$server_args" ]]; then
		ip netns exec $TARGET_NS ./xdping $server_args &
		server_pid=$!
		sleep 10
	fi
	./xdping $client_args $TARGET_IP

	if [[ $server_pid -ne 0 ]]; then
		kill -TERM $server_pid
		server_pid=0
	fi

	echo "Test client args '$client_args'; server args '$server_args': PASS"
}

set -e

server_pid=0

trap cleanup EXIT

setup

for server_args in "" "-I veth0 -s -S" ; do
	# client in skb mode
	client_args="-I veth1 -S"
	test "$client_args" "$server_args"

	# client with count of 10 RTT measurements.
	client_args="-I veth1 -S -c 10"
	test "$client_args" "$server_args"
done

# Test drv mode
test "-I veth1 -N" "-I veth0 -s -N"
test "-I veth1 -N -c 10" "-I veth0 -s -N"

echo "OK. All tests passed"
exit 0
