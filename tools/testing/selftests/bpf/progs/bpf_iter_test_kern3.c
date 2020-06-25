// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__task bpf_iter__task___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__task
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

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
	struct task_struct *task = ctx->task;
	int tgid;

	tgid = task->tgid;
	bpf_seq_write(seq, &tgid, sizeof(tgid));
	return 0;
}
