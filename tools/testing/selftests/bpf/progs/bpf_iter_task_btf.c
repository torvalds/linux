// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Oracle and/or its affiliates. */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include <errno.h>

char _license[] SEC("license") = "GPL";

long tasks = 0;
long seq_err = 0;
bool skip = false;

SEC("iter/task")
int dump_task_struct(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	static struct btf_ptr ptr = { };
	long ret;

#if __has_builtin(__builtin_btf_type_id)
	ptr.type_id = bpf_core_type_id_kernel(struct task_struct);
	ptr.ptr = task;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "Raw BTF task\n");

	ret = bpf_seq_printf_btf(seq, &ptr, sizeof(ptr), 0);
	switch (ret) {
	case 0:
		tasks++;
		break;
	case -ERANGE:
		/* NULL task or task->fs, don't count it as an error. */
		break;
	case -E2BIG:
		return 1;
	default:
		seq_err = ret;
		break;
	}
#else
	skip = true;
#endif

	return 0;
}
