/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__task bpf_iter__task___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__task
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";
int count = 0;

struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
} __attribute__((preserve_access_index));

struct bpf_iter__task {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
} __attribute__((preserve_access_index));

SEC("iter/task")
int dump_task(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	char c;

	if (count < 4) {
		c = START_CHAR + count;
		bpf_seq_write(seq, &c, sizeof(c));
		count++;
	}

	return 0;
}
