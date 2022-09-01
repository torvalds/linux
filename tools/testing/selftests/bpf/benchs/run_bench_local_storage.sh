#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./benchs/run_common.sh

set -eufo pipefail

header "Hashmap Control"
for i in 10 1000 10000 100000 4194304; do
subtitle "num keys: $i"
	summarize_local_storage "hashmap (control) sequential    get: "\
		"$(./bench --nr_maps 1 --hashmap_nr_keys_used=$i local-storage-cache-hashmap-control)"
	printf "\n"
done

header "Local Storage"
for i in 1 10 16 17 24 32 100 1000; do
subtitle "num_maps: $i"
	summarize_local_storage "local_storage cache sequential  get: "\
		"$(./bench --nr_maps $i local-storage-cache-seq-get)"
	summarize_local_storage "local_storage cache interleaved get: "\
		"$(./bench --nr_maps $i local-storage-cache-int-get)"
	printf "\n"
done
