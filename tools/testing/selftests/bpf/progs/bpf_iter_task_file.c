// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

int count = 0;
int tgid = 0;
int last_tgid = 0;
int unique_tgid_count = 0;

SEC("iter/task_file")
int dump_task_file(struct bpf_iter__task_file *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	struct file *file = ctx->file;
	__u32 fd = ctx->fd;

	if (task == (void *)0 || file == (void *)0)
		return 0;

	if (ctx->meta->seq_num == 0) {
		count = 0;
		BPF_SEQ_PRINTF(seq, "    tgid      gid       fd      file\n");
	}

	if (tgid == task->tgid && task->tgid != task->pid)
		count++;

	if (last_tgid != task->tgid) {
		last_tgid = task->tgid;
		unique_tgid_count++;
	}

	BPF_SEQ_PRINTF(seq, "%8d %8d %8d %lx\n", task->tgid, task->pid, fd,
		       (long)file->f_op);
	return 0;
}
