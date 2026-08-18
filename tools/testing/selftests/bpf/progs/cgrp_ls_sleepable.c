// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "err.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} map_a SEC(".maps");

__s32 target_pid;
__u64 cgroup_id;
long update_err;
int target_hid;
bool is_cgroup1;

struct cgroup *bpf_task_get_cgroup1(struct task_struct *task, int hierarchy_id) __ksym;
void bpf_cgroup_release(struct cgroup *cgrp) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

SEC("?iter.s/cgroup")
int cgroup_iter(struct bpf_iter__cgroup *ctx)
{
	struct cgroup *cgrp = ctx->cgroup;
	long *ptr;

	if (cgrp == NULL)
		return 0;

	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		cgroup_id = cgrp->kn->id;
	return 0;
}

static void __no_rcu_lock(struct cgroup *cgrp)
{
	long *ptr;

	/* Note that trace rcu is held in sleepable prog, so we can use
	 * bpf_cgrp_storage_get() in sleepable prog.
	 */
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		cgroup_id = cgrp->kn->id;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int cgrp1_no_rcu_lock(void *ctx)
{
	struct task_struct *task;
	struct cgroup *cgrp;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	/* bpf_task_get_cgroup1 can work in sleepable prog */
	cgrp = bpf_task_get_cgroup1(task, target_hid);
	if (!cgrp)
		return 0;

	__no_rcu_lock(cgrp);
	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int no_rcu_lock(void *ctx)
{
	struct task_struct *task;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	/* task->cgroups is untrusted in sleepable prog outside of RCU CS */
	__no_rcu_lock(task->cgroups->dfl_cgrp);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int yes_rcu_lock(void *ctx)
{
	struct task_struct *task;
	struct cgroup *cgrp;
	long *ptr;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	if (is_cgroup1) {
		bpf_rcu_read_lock();
		cgrp = bpf_task_get_cgroup1(task, target_hid);
		if (!cgrp) {
			bpf_rcu_read_unlock();
			return 0;
		}

		ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
		if (ptr)
			cgroup_id = cgrp->kn->id;
		bpf_cgroup_release(cgrp);
		bpf_rcu_read_unlock();
		return 0;
	}

	bpf_rcu_read_lock();
	cgrp = task->cgroups->dfl_cgrp;
	/* cgrp is trusted under RCU CS */
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		cgroup_id = cgrp->kn->id;
	bpf_rcu_read_unlock();
	return 0;
}

SEC("fexit/bpf_local_storage_update")
int BPF_PROG(fexit_update, void *owner, struct bpf_local_storage_map *smap,
	     void *value, u64 map_flags, bool swap_uptrs,
	     struct bpf_local_storage_data *ret)
{
	struct task_struct *task = bpf_get_current_task_btf();

	if (task->pid != target_pid)
		return 0;

	if (IS_ERR_VALUE(ret))
		update_err = PTR_ERR(ret);

	return 0;
}
