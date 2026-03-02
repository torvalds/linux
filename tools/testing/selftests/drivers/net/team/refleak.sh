#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2154

lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/lib.sh

trap cleanup_all_ns EXIT

# Test that there is no reference count leak and that dummy1 can be deleted.
# https://lore.kernel.org/netdev/4d69abe1-ca8d-4f0b-bcf8-13899b211e57@I-love.SAKURA.ne.jp/
setup_ns ns1 ns2
ip -n "$ns1" link add name team1 type team
ip -n "$ns1" link add name dummy1 mtu 1499 type dummy
ip -n "$ns1" link set dev dummy1 master team1
ip -n "$ns1" link set dev dummy1 netns "$ns2"
ip -n "$ns2" link del dev dummy1
