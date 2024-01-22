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

int target_hid = 0;
bool is_cgroup1 = 0;

struct cgroup *bpf_task_get_cgroup1(struct task_struct *task, int hierarchy_id) __ksym;
void bpf_cgroup_release(struct cgroup *cgrp) __ksym;

static void __on_lookup(struct cgroup *cgrp)
{
	bpf_cgrp_storage_delete(&map_a, cgrp);
	bpf_cgrp_storage_delete(&map_b, cgrp);
}

SEC("fentry/bpf_local_storage_lookup")
int BPF_PROG(on_lookup)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct cgroup *cgrp;

	if (is_cgroup1) {
		cgrp = bpf_task_get_cgroup1(task, target_hid);
		if (!cgrp)
			return 0;

		__on_lookup(cgrp);
		bpf_cgroup_release(cgrp);
		return 0;
	}

	__on_lookup(task->cgroups->dfl_cgrp);
	return 0;
}

static void __on_update(struct cgroup *cgrp)
{
	long *ptr;

	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr += 1;

	ptr = bpf_cgrp_storage_get(&map_b, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr += 1;
}

SEC("fentry/bpf_local_storage_update")
int BPF_PROG(on_update)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct cgroup *cgrp;

	if (is_cgroup1) {
		cgrp = bpf_task_get_cgroup1(task, target_hid);
		if (!cgrp)
			return 0;

		__on_update(cgrp);
		bpf_cgroup_release(cgrp);
		return 0;
	}

	__on_update(task->cgroups->dfl_cgrp);
	return 0;
}

static void __on_enter(struct pt_regs *regs, long id, struct cgroup *cgrp)
{
	long *ptr;

	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr = 200;

	ptr = bpf_cgrp_storage_get(&map_b, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		*ptr = 100;
}

SEC("tp_btf/sys_enter")
int BPF_PROG(on_enter, struct pt_regs *regs, long id)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct cgroup *cgrp;

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
