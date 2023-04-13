// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, long);
} map_a SEC(".maps");

__u32 target_pid;
__u64 cgroup_id;

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

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int no_rcu_lock(void *ctx)
{
	struct task_struct *task;
	struct cgroup *cgrp;
	long *ptr;

	task = bpf_get_current_task_btf();
	if (task->pid != target_pid)
		return 0;

	/* task->cgroups is untrusted in sleepable prog outside of RCU CS */
	cgrp = task->cgroups->dfl_cgrp;
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0,
				   BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		cgroup_id = cgrp->kn->id;
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

	bpf_rcu_read_lock();
	cgrp = task->cgroups->dfl_cgrp;
	/* cgrp is trusted under RCU CS */
	ptr = bpf_cgrp_storage_get(&map_a, cgrp, 0, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (ptr)
		cgroup_id = cgrp->kn->id;
	bpf_rcu_read_unlock();
	return 0;
}
