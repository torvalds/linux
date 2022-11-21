#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

for t in 1 4 8 12 16; do
for i in 10 100 500 1000 5000 10000 50000 100000 500000 1000000; do
subtitle "nr_loops: $i, nr_threads: $t"
	summarize_ops "bpf_loop: " \
	    "$($RUN_BENCH -p $t --nr_loops $i bpf-loop)"
	printf "\n"
done
done
