// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} map_a SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} map_b SEC(".maps");

#define MAGIC_VALUE 0xabcd1234

pid_t target_pid = 0;
int mismatch_cnt = 0;
int enter_cnt = 0;
int exit_cnt = 0;
int target_hid = 0;
bool is_cgroup1 = 0;

struct cgroup *bpf_task_get_cgroup1(struct task_struct *task, int hierarchy_id) __ksym;
void bpf_cgroup_release(struct cgroup *cgrp) __ksym;

static void __on_enter(struct pt_regs *regs, long id, struct cgroup *cgrp)
{
	long *ptr;
	int err;

	/* populate value 0 */
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		return;

	/* delete value 0 */
	err = bpf_cgrp_storage_delete(&map_a, cgrp);
	if (err)
		return;

	/* value is not available */
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0, 0);
	if (ptr)
		return;

	/* re-populate the value */
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		return;
	__sync_fetch_and_add(&enter_cnt, 1);
	*ptr = MAGIC_VALUE + enter_cnt;
}

SEC("tp_btf/sys_enter")
int BPF_PROG(on_enter, struct pt_regs *regs, long id)
{
	struct task_struct *task;
	struct cgroup *cgrp;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	if (is_cgroup1) {
		cgrp = bpf_task_get_cgroup1(task, target_hid);
		if (!cgrp)
			return 0;

		__on_enter(regs, id, cgrp);
		bpf_cgroup_release(cgrp);
		return 0;
	}

	__on_enter(regs, id, task->cgroups->dfl_cgrp);
	return 0;
}

static void __on_exit(struct pt_regs *regs, long id, struct cgroup *cgrp)
{
	long *ptr;

	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!ptr)
		return;

	__sync_fetch_and_add(&exit_cnt, 1);
	if (*ptr != MAGIC_VALUE + exit_cnt)
		__sync_fetch_and_add(&mismatch_cnt, 1);
}

SEC("tp_btf/sys_exit")
int BPF_PROG(on_exit, struct pt_regs *regs, long id)
{
	struct task_struct *task;
	struct cgroup *cgrp;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	if (is_cgroup1) {
		cgrp = bpf_task_get_cgroup1(task, target_hid);
		if (!cgrp)
			return 0;

		__on_exit(regs, id, cgrp);
		bpf_cgroup_release(cgrp);
		return 0;
	}

	__on_exit(regs, id, task->cgroups->dfl_cgrp);
	return 0;
}
