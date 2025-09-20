// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

#define MAX_STACK_TRACE_DEPTH   64
unsigned long entries[MAX_STACK_TRACE_DEPTH] = {};
#define SIZE_OF_ULONG (sizeof(unsigned long))

SEC("iter/task")
int dump_task_stack(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	long i, retlen;

	if (task == (void *)0)
		return 0;

	retlen = bpf_get_task_stack(task, entries,
				    MAX_STACK_TRACE_DEPTH * SIZE_OF_ULONG, 0);
	if (retlen < 0)
		return 0;

	BPF_SEQ_PRINTF(seq, "pid: %8u num_entries: %8u\n", task->pid,
		       retlen / SIZE_OF_ULONG);
	for (i = 0; i < MAX_STACK_TRACE_DEPTH; i++) {
		if (retlen > i * SIZE_OF_ULONG)
			BPF_SEQ_PRINTF(seq, "[<0>] %pB\n", (void *)entries[i]);
	}
	BPF_SEQ_PRINTF(seq, "\n");

	return 0;
}

int num_user_stacks = 0;

SEC("iter/task")
int get_task_user_stacks(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	uint64_t buf_sz = 0;
	int64_t res;

	if (task == (void *)0)
		return 0;

	res = bpf_get_task_stack(task, entries,
			MAX_STACK_TRACE_DEPTH * SIZE_OF_ULONG, BPF_F_USER_STACK);
	if (res <= 0)
		return 0;

	/* Only one task, the current one, should succeed */
	++num_user_stacks;

	buf_sz += res;

	/* If the verifier doesn't refine bpf_get_task_stack res, and instead
	 * assumes res is entirely unknown, this program will fail to load as
	 * the verifier will believe that max buf_sz value allows reading
	 * past the end of entries in bpf_seq_write call
	 */
	bpf_seq_write(seq, &entries, buf_sz);
	return 0;
}
