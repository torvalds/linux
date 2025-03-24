// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

__u32 unique_tgid_cnt = 0;
uintptr_t address = 0;
uintptr_t offset = 0;
__u32 last_tgid = 0;
__u32 pid = 0;
__u32 page_shift = 0;

SEC("iter/task_vma")
int get_vma_offset(struct bpf_iter__task_vma *ctx)
{
	struct vm_area_struct *vma = ctx->vma;
	struct seq_file *seq = ctx->meta->seq;
	struct task_struct *task = ctx->task;

	if (task == NULL || vma == NULL)
		return 0;

	if (last_tgid != task->tgid)
		unique_tgid_cnt++;
	last_tgid = task->tgid;

	if (task->tgid != pid)
		return 0;

	if (vma->vm_start <= address && vma->vm_end > address) {
		offset = address - vma->vm_start + (vma->vm_pgoff << page_shift);
		BPF_SEQ_PRINTF(seq, "OK\n");
	}
	return 0;
}
