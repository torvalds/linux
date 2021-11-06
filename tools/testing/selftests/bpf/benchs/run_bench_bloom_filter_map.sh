#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

header "Bloom filter map"
for v in 2 4 8 16 40; do
for t in 1 4 8 12 16; do
for h in {1..10}; do
subtitle "value_size: $v bytes, # threads: $t, # hashes: $h"
	for e in 10000 50000 75000 100000 250000 500000 750000 1000000 2500000 5000000; do
		printf "%'d entries -\n" $e
		printf "\t"
		summarize "Lookups, total operations: " \
			"$($RUN_BENCH -p $t --nr_hash_funcs $h --nr_entries $e --value_size $v bloom-lookup)"
		printf "\t"
		summarize "Updates, total operations: " \
			"$($RUN_BENCH -p $t --nr_hash_funcs $h --nr_entries $e --value_size $v bloom-update)"
		printf "\t"
		summarize_percentage "False positive rate: " \
			"$($RUN_BENCH -p $t --nr_hash_funcs $h --nr_entries $e --value_size $v bloom-false-positive)"
	done
	printf "\n"
done
done
done

header "Hashmap without bloom filter vs. hashmap with bloom filter (throughput, 8 threads)"
for v in 2 4 8 16 40; do
for h in {1..10}; do
subtitle "value_size: $v, # hashes: $h"
	for e in 10000 50000 75000 100000 250000 500000 750000 1000000 2500000 5000000; do
		printf "%'d entries -\n" $e
		printf "\t"
		summarize_total "Hashmap without bloom filter: " \
			"$($RUN_BENCH --nr_hash_funcs $h --nr_entries $e --value_size $v -p 8 hashmap-without-bloom)"
		printf "\t"
		summarize_total "Hashmap with bloom filter: " \
			"$($RUN_BENCH --nr_hash_funcs $h --nr_entries $e --value_size $v -p 8 hashmap-with-bloom)"
	done
	printf "\n"
done
done
