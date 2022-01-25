#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

for s in 1 8 64 512 2048 4095; do
	for b in no-helper helper; do
		summarize ${b}-${s} "$($RUN_BENCH --cmp-str-len=$s strncmp-${b})"
	done
done
