#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2154
#
# Reproduce the non-Ethernet header_ops confusion scenario with:
#   g0 (gre) -> b0 (bond) -> t0 (team)
#
# Before the fix, direct header_ops inheritance in this stack could call
# callbacks with the wrong net_device context and crash.

lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/lib.sh

trap cleanup_all_ns EXIT

setup_ns ns1

ip -n "$ns1" link add d0 type dummy
ip -n "$ns1" addr add 10.10.10.1/24 dev d0
ip -n "$ns1" link set d0 up

ip -n "$ns1" link add g0 type gre local 10.10.10.1
ip -n "$ns1" link add b0 type bond mode active-backup
ip -n "$ns1" link add t0 type team

ip -n "$ns1" link set g0 master b0
ip -n "$ns1" link set b0 master t0

ip -n "$ns1" link set g0 up
ip -n "$ns1" link set b0 up
ip -n "$ns1" link set t0 up

# IPv6 address assignment triggers MLD join reports that call
# dev_hard_header() on t0, exercising the inherited header_ops path.
ip -n "$ns1" -6 addr add 2001:db8:1::1/64 dev t0 nodad
for i in $(seq 1 20); do
	ip netns exec "$ns1" ping -6 -I t0 ff02::1 -c1 -W1 &>/dev/null || true
done

echo "PASS: non-Ethernet header_ops stacking did not crash"
exit "$EXIT_STATUS"
