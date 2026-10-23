#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

RUN_BENCH="./bench -d3 -q"

summarize_libarena()
{
	local bench="$1"
	local summary

	summary=$(printf '%s\n' "$2" | tail -n1)
	summary=${summary#Summary: }
	printf "%-20s %s\n" "$bench" "$summary"
}

header "libarena sequential malloc\n"

for size in 16 64 256 1024 4096; do
subtitle "allocation size: $size"
printf "\t-------------------\n"
	for nallocs in 10 50 100 500 1000 5000 10000; do
		summarize_libarena "malloc:" \
			"$($RUN_BENCH --alloc_size "$size" --nallocs "$nallocs" libarena-malloc)"
		summarize_libarena "calloc:" \
			"$($RUN_BENCH --alloc_size "$size" --nallocs "$nallocs" libarena-calloc)"
	done
done
