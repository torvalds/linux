#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

htab_mem()
{
	echo -n "per-prod-op: "
	echo -n "$*" | sed -E "s/.* per-prod-op\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+k\/s).*/\1/"
	echo -n -e ", avg mem: "
	echo -n "$*" | sed -E "s/.* memory usage\s+([0-9]+\.[0-9]+ ± [0-9]+\.[0-9]+MiB).*/\1/"
	echo -n ", peak mem: "
	echo "$*" | sed -E "s/.* peak memory usage\s+([0-9]+\.[0-9]+MiB).*/\1/"
}

summarize_htab_mem()
{
	local bench="$1"
	local summary=$(echo $2 | tail -n1)

	printf "%-20s %s\n" "$bench" "$(htab_mem $summary)"
}

htab_mem_bench()
{
	local name

	for name in overwrite batch_add_batch_del add_del_on_diff_cpu
	do
		summarize_htab_mem "$name" "$($RUN_BENCH htab-mem --use-case $name -p8 "$@")"
	done
}

header "preallocated"
htab_mem_bench "--preallocated"

header "normal bpf ma"
htab_mem_bench
