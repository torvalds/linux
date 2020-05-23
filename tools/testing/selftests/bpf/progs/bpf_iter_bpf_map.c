// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__bpf_map bpf_iter__bpf_map___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__bpf_map
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
} __attribute__((preserve_access_index));

struct bpf_iter__bpf_map {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
} __attribute__((preserve_access_index));

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
		       map->memory.user->locked_vm.counter);
	return 0;
}
