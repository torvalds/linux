// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

uint32_t tid = 0;
int num_unknown_tid = 0;
int num_known_tid = 0;

SEC("iter/task")
int dump_task(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	static char info[] = "    === END ===";

	if (task == (void *)0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	if (task->pid != tid)
		num_unknown_tid++;
	else
		num_known_tid++;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "    tgid      gid\n");

	BPF_SEQ_PRINTF(seq, "%8d %8d\n", task->tgid, task->pid);
	return 0;
}

int num_expected_failure_copy_from_user_task = 0;
int num_success_copy_from_user_task = 0;

SEC("iter.s/task")
int dump_task_sleepable(struct bpf_iter__task *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;
	static const char info[] = "    === END ===";
	struct pt_regs *regs;
	void *ptr;
	uint32_t user_data = 0;
	int ret;

	if (task == (void *)0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Read an invalid pointer and ensure we get an error */
	ptr = NULL;
	ret = bpf_copy_from_user_task(&user_data, sizeof(uint32_t), ptr, task, 0);
	if (ret) {
		++num_expected_failure_copy_from_user_task;
	} else {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}

	/* Try to read the contents of the task's instruction pointer from the
	 * remote task's address space.
	 */
	regs = (struct pt_regs *)bpf_task_pt_regs(task);
	if (regs == (void *)0) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}
	ptr = (void *)PT_REGS_IP(regs);

	ret = bpf_copy_from_user_task(&user_data, sizeof(uint32_t), ptr, task, 0);
	if (ret) {
		BPF_SEQ_PRINTF(seq, "%s\n", info);
		return 0;
	}
	++num_success_copy_from_user_task;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "    tgid      gid     data\n");

	BPF_SEQ_PRINTF(seq, "%8d %8d %8d\n", task->tgid, task->pid, user_data);
	return 0;
}
