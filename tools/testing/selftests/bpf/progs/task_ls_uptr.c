// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "uptr_test_common.h"

struct task_struct *bpf_task_from_pid(s32 pid) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
void bpf_cgroup_release(struct cgroup *cgrp) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct value_type);
} datamap SEC(".maps");

pid_t target_pid = 0;
pid_t parent_pid = 0;

SEC("tp_btf/sys_enter")
int on_enter(__u64 *ctx)
{
	struct task_struct *task, *data_task;
	struct value_type *ptr;
	struct user_data *udata;
	struct cgroup *cgrp;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	data_task = bpf_task_from_pid(parent_pid);
	if (!data_task)
		return 0;

	ptr = bpf_task_storage_get(&datamap, data_task, 0, 0);
	bpf_task_release(data_task);
	if (!ptr)
		return 0;

	cgrp = bpf_kptr_xchg(&ptr->cgrp, NULL);
	if (cgrp) {
		int lvl = cgrp->level;

		bpf_cgroup_release(cgrp);
		return lvl;
	}

	udata = ptr->udata;
	if (!udata || udata->result)
		return 0;
	udata->result = MAGIC_VALUE + udata->a + udata->b;

	udata = ptr->nested.udata;
	if (udata && !udata->nested_result)
		udata->nested_result = udata->result;

	return 0;
}

char _license[] SEC("license") = "GPL";
