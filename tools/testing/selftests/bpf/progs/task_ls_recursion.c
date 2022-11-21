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
} map_a SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} map_b SEC(".maps");

SEC("fentry/bpf_local_storage_lookup")
int BPF_PROG(on_lookup)
{
	struct task_struct *task = bpf_get_current_task_btf();

	bpf_task_storage_delete(&map_a, task);
	bpf_task_storage_delete(&map_b, task);
	return 0;
}

SEC("fentry/bpf_local_storage_update")
int BPF_PROG(on_update)
{
	struct task_struct *task = bpf_get_current_task_btf();
	long *ptr;

	ptr = bpf_task_storage_get(&map_a, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr += 1;

	ptr = bpf_task_storage_get(&map_b, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr += 1;

	return 0;
}

SEC("tp_btf/sys_enter")
int BPF_PROG(on_enter, struct pt_regs *regs, long id)
{
	struct task_struct *task;
	long *ptr;

	task = bpf_get_current_task_btf();
	ptr = bpf_task_storage_get(&map_a, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr = 200;

	ptr = bpf_task_storage_get(&map_b, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr = 100;
	return 0;
}
