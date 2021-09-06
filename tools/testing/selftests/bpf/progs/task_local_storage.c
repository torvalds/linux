// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} enter_id SEC(".maps");

#define MAGIC_VALUE 0xabcd1234

pid_t target_pid = 0;
int mismatch_cnt = 0;
int enter_cnt = 0;
int exit_cnt = 0;

SEC("tp_btf/sys_enter")
int BPF_PROG(on_enter, struct pt_regs *regs, long id)
{
	struct task_struct *task;
	long *ptr;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	ptr = bpf_task_storage_get(&enter_id, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		return 0;

	__sync_fetch_and_add(&enter_cnt, 1);
	*ptr = MAGIC_VALUE + enter_cnt;

	return 0;
}

SEC("tp_btf/sys_exit")
int BPF_PROG(on_exit, struct pt_regs *regs, long id)
{
	struct task_struct *task;
	long *ptr;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	ptr = bpf_task_storage_get(&enter_id, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		return 0;

	__sync_fetch_and_add(&exit_cnt, 1);
	if (*ptr != MAGIC_VALUE + exit_cnt)
		__sync_fetch_and_add(&mismatch_cnt, 1);
	return 0;
}
