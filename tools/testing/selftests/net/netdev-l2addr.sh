#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh
set -o pipefail

NSIM_ADDR=2025
TEST_ADDR="d0:be:d0:be:d0:00"

RET_CODE=0

cleanup() {
    cleanup_netdevsim "$NSIM_ADDR"
    cleanup_ns "$NS"
}

trap cleanup EXIT

fail() {
    echo "ERROR: ${1:-unexpected return code} (ret: $_)" >&2
    RET_CODE=1
}

get_addr()
{
    local type="$1"
    local dev="$2"
    local ns="$3"

    ip -j -n "$ns" link show dev "$dev" | jq -er ".[0].$type"
}

setup_ns NS

nsim=$(create_netdevsim $NSIM_ADDR "$NS")

get_addr address "$nsim" "$NS" >/dev/null || fail "Couldn't get ether addr"
get_addr broadcast "$nsim" "$NS" >/dev/null || fail "Couldn't get brd addr"
get_addr permaddr "$nsim" "$NS" >/dev/null && fail "Found perm_addr without setting it"

ip -n "$NS" link set dev "$nsim" address "$TEST_ADDR"
ip -n "$NS" link set dev "$nsim" brd "$TEST_ADDR"

[[ "$(get_addr address "$nsim" "$NS")" == "$TEST_ADDR" ]] || fail "Couldn't set ether addr"
[[ "$(get_addr broadcast "$nsim" "$NS")" == "$TEST_ADDR" ]] || fail "Couldn't set brd addr"

if create_netdevsim_port "$NSIM_ADDR" "$NS" 2 "FF:FF:FF:FF:FF:FF" 2>/dev/null; then
    fail "Created netdevsim with broadcast permaddr"
fi

nsim_port=$(create_netdevsim_port "$NSIM_ADDR" "$NS" 2 "$TEST_ADDR")

get_addr address "$nsim_port" "$NS" >/dev/null || fail "Couldn't get ether addr"
get_addr broadcast "$nsim_port" "$NS" >/dev/null || fail "Couldn't get brd addr"
[[ "$(get_addr permaddr "$nsim_port" "$NS")" == "$TEST_ADDR" ]] || fail "Couldn't get permaddr"

cleanup_netdevsim "$NSIM_ADDR" "$NS"

exit $RET_CODE
