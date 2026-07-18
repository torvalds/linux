#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

WARMUP=${WARMUP:-3}

RUN="sudo ./bench -q -w${WARMUP} -a xdp-lb --machine-readable"

SEP="  +----------------------------------+----------+---------+----------+"
HDR="  | %-32s | %8s | %7s | %8s |\n"
ROW="  | %-32s | %8s | %7s | %8s |\n"

function group_header()
{
	printf "%s\n" "$SEP"
	printf "$HDR" "$1" "p50" "stddev" "p99"
	printf "%s\n" "$SEP"
}

function rval()
{
	echo "$1" | sed -nE "s/.*$2=([^ ]+).*/\1/p"
}

function run_scenario()
{
	local sc="$1"
	shift
	local output rline

	output=$($RUN --scenario "$sc" "$@" 2>&1) || true
	rline=$(echo "$output" | grep '^RESULT ' || true)

	if [ -z "$rline" ]; then
		printf "$ROW" "$sc" "ERR" "-" "-"
		return
	fi

	printf "$ROW" "$sc" \
		"$(rval "$rline" median)" \
		"$(rval "$rline" stddev)" \
		"$(rval "$rline" p99)"
}

header "XDP load-balancer benchmark"

group_header "Single-flow baseline"
for sc in tcp-v4-lru-hit tcp-v4-ch \
	  tcp-v6-lru-hit tcp-v6-ch \
	  udp-v4-lru-hit udp-v6-lru-hit \
	  tcp-v4v6-lru-hit; do
	run_scenario "$sc"
done

group_header "Diverse flows (4K src addrs)"
for sc in tcp-v4-lru-diverse tcp-v4-ch-diverse \
	  tcp-v6-lru-diverse tcp-v6-ch-diverse \
	  udp-v4-lru-diverse; do
	run_scenario "$sc"
done

group_header "TCP flags"
run_scenario tcp-v4-syn
run_scenario tcp-v4-rst-miss

group_header "LRU stress"
run_scenario tcp-v4-lru-miss
run_scenario udp-v4-lru-miss
run_scenario tcp-v4-lru-warmup

group_header "Early exits"
for sc in pass-v4-no-vip pass-v6-no-vip pass-v4-icmp pass-non-ip drop-v4-frag drop-v4-options \
	  drop-v6-frag; do
	run_scenario "$sc"
done
printf "%s\n" "$SEP"
