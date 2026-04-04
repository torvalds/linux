#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# author: Andrea Mayer <andrea.mayer@uniroma2.it>

# This test verifies that the seg6 lwtunnel does not share the dst_cache
# between the input (forwarding) and output (locally generated) paths.
#
# A shared dst_cache allows a forwarded packet to populate the cache and a
# subsequent locally generated packet to silently reuse that entry, bypassing
# its own route lookup. To expose this, the SID is made reachable only for
# forwarded traffic (via an ip rule matching iif) and blackholed for everything
# else. A local ping on ns_router must always hit the blackhole;
# if it succeeds after a forwarded packet has populated the
# cache, the bug is confirmed.
#
# Both forwarded and local packets are pinned to the same CPU with taskset,
# since dst_cache is per-cpu.
#
#
# +--------------------+                        +--------------------+
# |      ns_src        |                        |      ns_dst        |
# |                    |                        |                    |
# |  veth-s0           |                        |           veth-d0  |
# |  fd00::1/64        |                        |        fd01::2/64  |
# +-------+------------+                        +----------+---------+
#         |                                                |
#         |            +--------------------+              |
#         |            |     ns_router      |              |
#         |            |                    |              |
#         +------------+ veth-r0    veth-r1 +--------------+
#                      | fd00::2    fd01::1 |
#                      +--------------------+
#
#
# ns_router: encap (main table)
# +---------+---------------------------------------+
# | dst     | action                                |
# +---------+---------------------------------------+
# | cafe::1 | encap seg6 mode encap segs fc00::100  |
# +---------+---------------------------------------+
#
# ns_router: post-encap SID resolution
# +-------+------------+----------------------------+
# | table | dst        | action                     |
# +-------+------------+----------------------------+
# | 100   | fc00::100  | via fd01::2 dev veth-r1    |
# +-------+------------+----------------------------+
# | main  | fc00::100  | blackhole                  |
# +-------+------------+----------------------------+
#
# ns_router: ip rule
# +------------------+------------------------------+
# | match            | action                       |
# +------------------+------------------------------+
# | iif veth-r0      | lookup 100                   |
# +------------------+------------------------------+
#
# ns_dst: SRv6 decap (main table)
# +--------------+----------------------------------+
# | SID          | action                           |
# +--------------+----------------------------------+
# | fc00::100    | End.DT6 table 255 (local)        |
# +--------------+----------------------------------+

source lib.sh

readonly SID="fc00::100"
readonly DEST="cafe::1"

readonly SRC_MAC="02:00:00:00:00:01"
readonly RTR_R0_MAC="02:00:00:00:00:02"
readonly RTR_R1_MAC="02:00:00:00:00:03"
readonly DST_MAC="02:00:00:00:00:04"

cleanup()
{
	cleanup_ns "${NS_SRC}" "${NS_RTR}" "${NS_DST}"
}

check_prerequisites()
{
	if ! command -v ip &>/dev/null; then
		echo "SKIP: ip tool not found"
		exit "${ksft_skip}"
	fi

	if ! command -v ping &>/dev/null; then
		echo "SKIP: ping not found"
		exit "${ksft_skip}"
	fi

	if ! command -v sysctl &>/dev/null; then
		echo "SKIP: sysctl not found"
		exit "${ksft_skip}"
	fi

	if ! command -v taskset &>/dev/null; then
		echo "SKIP: taskset not found"
		exit "${ksft_skip}"
	fi
}

setup()
{
	setup_ns NS_SRC NS_RTR NS_DST

	ip link add veth-s0 netns "${NS_SRC}" type veth \
		peer name veth-r0 netns "${NS_RTR}"
	ip link add veth-r1 netns "${NS_RTR}" type veth \
		peer name veth-d0 netns "${NS_DST}"

	ip -n "${NS_SRC}" link set veth-s0 address "${SRC_MAC}"
	ip -n "${NS_RTR}" link set veth-r0 address "${RTR_R0_MAC}"
	ip -n "${NS_RTR}" link set veth-r1 address "${RTR_R1_MAC}"
	ip -n "${NS_DST}" link set veth-d0 address "${DST_MAC}"

	# ns_src
	ip -n "${NS_SRC}" link set veth-s0 up
	ip -n "${NS_SRC}" addr add fd00::1/64 dev veth-s0 nodad
	ip -n "${NS_SRC}" -6 route add "${DEST}"/128 via fd00::2

	# ns_router
	ip -n "${NS_RTR}" link set veth-r0 up
	ip -n "${NS_RTR}" addr add fd00::2/64 dev veth-r0 nodad
	ip -n "${NS_RTR}" link set veth-r1 up
	ip -n "${NS_RTR}" addr add fd01::1/64 dev veth-r1 nodad
	ip netns exec "${NS_RTR}" sysctl -qw net.ipv6.conf.all.forwarding=1

	ip -n "${NS_RTR}" -6 route add "${DEST}"/128 \
		encap seg6 mode encap segs "${SID}" dev veth-r0
	ip -n "${NS_RTR}" -6 route add "${SID}"/128 table 100 \
		via fd01::2 dev veth-r1
	ip -n "${NS_RTR}" -6 route add blackhole "${SID}"/128
	ip -n "${NS_RTR}" -6 rule add iif veth-r0 lookup 100

	# ns_dst
	ip -n "${NS_DST}" link set veth-d0 up
	ip -n "${NS_DST}" addr add fd01::2/64 dev veth-d0 nodad
	ip -n "${NS_DST}" addr add "${DEST}"/128 dev lo nodad
	ip -n "${NS_DST}" -6 route add "${SID}"/128 \
		encap seg6local action End.DT6 table 255 dev veth-d0
	ip -n "${NS_DST}" -6 route add fd00::/64 via fd01::1

	# static neighbors
	ip -n "${NS_SRC}" -6 neigh add fd00::2 dev veth-s0 \
		lladdr "${RTR_R0_MAC}" nud permanent
	ip -n "${NS_RTR}" -6 neigh add fd00::1 dev veth-r0 \
		lladdr "${SRC_MAC}" nud permanent
	ip -n "${NS_RTR}" -6 neigh add fd01::2 dev veth-r1 \
		lladdr "${DST_MAC}" nud permanent
	ip -n "${NS_DST}" -6 neigh add fd01::1 dev veth-d0 \
		lladdr "${RTR_R1_MAC}" nud permanent
}

test_cache_isolation()
{
	RET=0

	# local ping with empty cache: must fail (SID is blackholed)
	if ip netns exec "${NS_RTR}" taskset -c 0 \
			ping -c 1 -W 2 "${DEST}" &>/dev/null; then
		echo "SKIP: local ping succeeded, topology broken"
		exit "${ksft_skip}"
	fi

	# forward from ns_src to populate the input cache
	if ! ip netns exec "${NS_SRC}" taskset -c 0 \
			ping -c 1 -W 2 "${DEST}" &>/dev/null; then
		echo "SKIP: forwarded ping failed, topology broken"
		exit "${ksft_skip}"
	fi

	# local ping again: must still fail; if the output path reuses
	# the input cache, it bypasses the blackhole and the ping succeeds
	if ip netns exec "${NS_RTR}" taskset -c 0 \
			ping -c 1 -W 2 "${DEST}" &>/dev/null; then
		echo "FAIL: output path used dst cached by input path"
		RET="${ksft_fail}"
	else
		echo "PASS: output path dst_cache is independent"
	fi

	return "${RET}"
}

if [ "$(id -u)" -ne 0 ]; then
	echo "SKIP: Need root privileges"
	exit "${ksft_skip}"
fi

trap cleanup EXIT

check_prerequisites
setup
test_cache_isolation
exit "${RET}"
