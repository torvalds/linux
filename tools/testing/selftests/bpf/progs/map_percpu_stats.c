// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

__u32 target_id;

__s64 bpf_map_sum_elem_count(const struct bpf_map *map) __ksym;

SEC("iter/bpf_map")
int dump_bpf_map(struct bpf_iter__bpf_map *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct bpf_map *map = ctx->map;

	if (map && map->id == target_id)
		BPF_SEQ_PRINTF(seq, "%lld", bpf_map_sum_elem_count(map));

	return 0;
}

char _license[] SEC("license") = "GPL";
