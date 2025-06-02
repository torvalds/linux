#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Author: Justin Iurman <justin.iurman@uliege.be>
#
# WARNING
# -------
# This is just a dummy script that triggers encap cases with possible dst cache
# reference loops in affected lwt users (see list below). Some cases are
# pathological configurations for simplicity, others are valid. Overall, we
# don't want this issue to happen, no matter what. In order to catch any
# reference loops, kmemleak MUST be used. The results alone are always blindly
# successful, don't rely on them. Note that the following tests may crash the
# kernel if the fix to prevent lwtunnel_{input|output|xmit}() reentry loops is
# not present.
#
# Affected lwt users so far (please update accordingly if needed):
#  - ila_lwt (output only)
#  - ioam6_iptunnel (output only)
#  - rpl_iptunnel (both input and output)
#  - seg6_iptunnel (both input and output)

source lib.sh

check_compatibility()
{
	setup_ns tmp_node &>/dev/null
	if [ $? != 0 ]; then
		echo "SKIP: Cannot create netns."
		exit $ksft_skip
	fi

	ip link add name veth0 netns $tmp_node type veth \
		peer name veth1 netns $tmp_node &>/dev/null
	local ret=$?

	ip -netns $tmp_node link set veth0 up &>/dev/null
	ret=$((ret + $?))

	ip -netns $tmp_node link set veth1 up &>/dev/null
	ret=$((ret + $?))

	if [ $ret != 0 ]; then
		echo "SKIP: Cannot configure links."
		cleanup_ns $tmp_node
		exit $ksft_skip
	fi

	lsmod 2>/dev/null | grep -q "ila"
	ila_lsmod=$?
	[ $ila_lsmod != 0 ] && modprobe ila &>/dev/null

	ip -netns $tmp_node route add 2001:db8:1::/64 \
		encap ila 1:2:3:4 csum-mode no-action ident-type luid \
			hook-type output \
		dev veth0 &>/dev/null

	ip -netns $tmp_node route add 2001:db8:2::/64 \
		encap ioam6 trace prealloc type 0x800000 ns 0 size 4 \
		dev veth0 &>/dev/null

	ip -netns $tmp_node route add 2001:db8:3::/64 \
		encap rpl segs 2001:db8:3::1 dev veth0 &>/dev/null

	ip -netns $tmp_node route add 2001:db8:4::/64 \
		encap seg6 mode inline segs 2001:db8:4::1 dev veth0 &>/dev/null

	ip -netns $tmp_node -6 route 2>/dev/null | grep -q "encap ila"
	skip_ila=$?

	ip -netns $tmp_node -6 route 2>/dev/null | grep -q "encap ioam6"
	skip_ioam6=$?

	ip -netns $tmp_node -6 route 2>/dev/null | grep -q "encap rpl"
	skip_rpl=$?

	ip -netns $tmp_node -6 route 2>/dev/null | grep -q "encap seg6"
	skip_seg6=$?

	cleanup_ns $tmp_node
}

setup()
{
	setup_ns alpha beta gamma &>/dev/null

	ip link add name veth-alpha netns $alpha type veth \
		peer name veth-betaL netns $beta &>/dev/null

	ip link add name veth-betaR netns $beta type veth \
		peer name veth-gamma netns $gamma &>/dev/null

	ip -netns $alpha link set veth-alpha name veth0 &>/dev/null
	ip -netns $beta link set veth-betaL name veth0 &>/dev/null
	ip -netns $beta link set veth-betaR name veth1 &>/dev/null
	ip -netns $gamma link set veth-gamma name veth0 &>/dev/null

	ip -netns $alpha addr add 2001:db8:1::2/64 dev veth0 &>/dev/null
	ip -netns $alpha link set veth0 up &>/dev/null
	ip -netns $alpha link set lo up &>/dev/null
	ip -netns $alpha route add 2001:db8:2::/64 \
		via 2001:db8:1::1 dev veth0 &>/dev/null

	ip -netns $beta addr add 2001:db8:1::1/64 dev veth0 &>/dev/null
	ip -netns $beta addr add 2001:db8:2::1/64 dev veth1 &>/dev/null
	ip -netns $beta link set veth0 up &>/dev/null
	ip -netns $beta link set veth1 up &>/dev/null
	ip -netns $beta link set lo up &>/dev/null
	ip -netns $beta route del 2001:db8:2::/64
	ip -netns $beta route add 2001:db8:2::/64 dev veth1
	ip netns exec $beta \
		sysctl -wq net.ipv6.conf.all.forwarding=1 &>/dev/null

	ip -netns $gamma addr add 2001:db8:2::2/64 dev veth0 &>/dev/null
	ip -netns $gamma link set veth0 up &>/dev/null
	ip -netns $gamma link set lo up &>/dev/null
	ip -netns $gamma route add 2001:db8:1::/64 \
		via 2001:db8:2::1 dev veth0 &>/dev/null

	sleep 1

	ip netns exec $alpha ping6 -c 5 -W 1 2001:db8:2::2 &>/dev/null
	if [ $? != 0 ]; then
		echo "SKIP: Setup failed."
		exit $ksft_skip
	fi

	sleep 1
}

cleanup()
{
	cleanup_ns $alpha $beta $gamma
	[ $ila_lsmod != 0 ] && modprobe -r ila &>/dev/null
}

run_ila()
{
	if [ $skip_ila != 0 ]; then
		echo "SKIP: ila (output)"
		return
	fi

	ip -netns $beta route del 2001:db8:2::/64
	ip -netns $beta route add 2001:db8:2:0:0:0:0:2/128 \
		encap ila 2001:db8:2:0 csum-mode no-action ident-type luid \
			hook-type output \
		dev veth1 &>/dev/null
	sleep 1

	echo "TEST: ila (output)"
	ip netns exec $beta ping6 -c 2 -W 1 2001:db8:2::2 &>/dev/null
	sleep 1

	ip -netns $beta route del 2001:db8:2:0:0:0:0:2/128
	ip -netns $beta route add 2001:db8:2::/64 dev veth1
	sleep 1
}

run_ioam6()
{
	if [ $skip_ioam6 != 0 ]; then
		echo "SKIP: ioam6 (output)"
		return
	fi

	ip -netns $beta route change 2001:db8:2::/64 \
		encap ioam6 trace prealloc type 0x800000 ns 1 size 4 \
		dev veth1 &>/dev/null
	sleep 1

	echo "TEST: ioam6 (output)"
	ip netns exec $beta ping6 -c 2 -W 1 2001:db8:2::2 &>/dev/null
	sleep 1
}

run_rpl()
{
	if [ $skip_rpl != 0 ]; then
		echo "SKIP: rpl (input)"
		echo "SKIP: rpl (output)"
		return
	fi

	ip -netns $beta route change 2001:db8:2::/64 \
		encap rpl segs 2001:db8:2::2 \
		dev veth1 &>/dev/null
	sleep 1

	echo "TEST: rpl (input)"
	ip netns exec $alpha ping6 -c 2 -W 1 2001:db8:2::2 &>/dev/null
	sleep 1

	echo "TEST: rpl (output)"
	ip netns exec $beta ping6 -c 2 -W 1 2001:db8:2::2 &>/dev/null
	sleep 1
}

run_seg6()
{
	if [ $skip_seg6 != 0 ]; then
		echo "SKIP: seg6 (input)"
		echo "SKIP: seg6 (output)"
		return
	fi

	ip -netns $beta route change 2001:db8:2::/64 \
		encap seg6 mode inline segs 2001:db8:2::2 \
		dev veth1 &>/dev/null
	sleep 1

	echo "TEST: seg6 (input)"
	ip netns exec $alpha ping6 -c 2 -W 1 2001:db8:2::2 &>/dev/null
	sleep 1

	echo "TEST: seg6 (output)"
	ip netns exec $beta ping6 -c 2 -W 1 2001:db8:2::2 &>/dev/null
	sleep 1
}

run()
{
	run_ila
	run_ioam6
	run_rpl
	run_seg6
}

if [ "$(id -u)" -ne 0 ]; then
	echo "SKIP: Need root privileges."
	exit $ksft_skip
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool."
	exit $ksft_skip
fi

check_compatibility

trap cleanup EXIT

setup
run

exit $ksft_pass
