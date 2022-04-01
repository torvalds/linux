// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct callback_ctx {
	int dummy;
};

#define VM_EXEC		0x00000004
#define DNAME_INLINE_LEN 32

pid_t target_pid = 0;
char d_iname[DNAME_INLINE_LEN] = {0};
__u32 found_vm_exec = 0;
__u64 addr = 0;
int find_zero_ret = -1;
int find_addr_ret = -1;

static long check_vma(struct task_struct *task, struct vm_area_struct *vma,
		      struct callback_ctx *data)
{
	if (vma->vm_file)
		bpf_probe_read_kernel_str(d_iname, DNAME_INLINE_LEN - 1,
					  vma->vm_file->f_path.dentry->d_iname);

	/* check for VM_EXEC */
	if (vma->vm_flags & VM_EXEC)
		found_vm_exec = 1;

	return 0;
}

SEC("raw_tp/sys_enter")
int handle_getpid(void)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct callback_ctx data = {};

	if (task->pid != target_pid)
		return 0;

	find_addr_ret = bpf_find_vma(task, addr, check_vma, &data, 0);

	/* this should return -ENOENT */
	find_zero_ret = bpf_find_vma(task, 0, check_vma, &data, 0);
	return 0;
}

SEC("perf_event")
int handle_pe(void)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct callback_ctx data = {};

	if (task->pid != target_pid)
		return 0;

	find_addr_ret = bpf_find_vma(task, addr, check_vma, &data, 0);

	/* In NMI, this should return -EBUSY, as the previous call is using
	 * the irq_work.
	 */
	find_zero_ret = bpf_find_vma(task, 0, check_vma, &data, 0);
	return 0;
}
