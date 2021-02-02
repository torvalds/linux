// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("iter/bpf_map")
int dump_bpf_map(struct bpf_iter__bpf_map *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	__u64 seq_num = ctx->meta->seq_num;
	struct bpf_map *map = ctx->map;

	if (map == (void *)0) {
		BPF_SEQ_PRINTF(seq, "      %%%%%% END %%%%%%\n");
		return 0;
	}

	if (seq_num == 0)
		BPF_SEQ_PRINTF(seq, "      id   refcnt  usercnt  locked_vm\n");

	BPF_SEQ_PRINTF(seq, "%8u %8ld %8ld %10lu\n", map->id, map->refcnt.counter,
		       map->usercnt.counter,
		       0LLU);
	return 0;
}
