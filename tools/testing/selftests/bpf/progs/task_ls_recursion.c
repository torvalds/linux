// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#ifndef EBUSY
#define EBUSY 16
#endif

char _license[] SEC("license") = "GPL";
int nr_del_errs = 0;
int test_pid = 0;

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

SEC("fentry/bpf_local_storage_update")
int BPF_PROG(on_update)
{
	struct task_struct *task = bpf_get_current_task_btf();
	long *ptr;

	if (!test_pid || task->pid != test_pid)
		return 0;

	ptr = bpf_task_storage_get(&map_a, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	/* ptr will not be NULL when it is called from
	 * the bpf_task_storage_get(&map_b,...F_CREATE) in
	 * the BPF_PROG(on_enter) below.  It is because
	 * the value can be found in map_a and the kernel
	 * does not need to acquire any spin_lock.
	 */
	if (ptr) {
		int err;

		*ptr += 1;
		err = bpf_task_storage_delete(&map_a, task);
		if (err == -EBUSY)
			nr_del_errs++;
	}

	/* This will still fail because map_b is empty and
	 * this BPF_PROG(on_update) has failed to acquire
	 * the percpu busy lock => meaning potential
	 * deadlock is detected and it will fail to create
	 * new storage.
	 */
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
	if (!test_pid || task->pid != test_pid)
		return 0;

	ptr = bpf_task_storage_get(&map_a, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr && !*ptr)
		*ptr = 200;

	ptr = bpf_task_storage_get(&map_b, task, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr && !*ptr)
		*ptr = 100;
	return 0;
}
