// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Bytedance */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"

struct cgroup *bpf_cgroup_from_id(u64 cgid) __ksym;
long bpf_task_under_cgroup(struct task_struct *task, struct cgroup *ancestor) __ksym;
void bpf_cgroup_release(struct cgroup *p) __ksym;
struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;

const volatile int local_pid;
const volatile __u64 cgid;
int remote_pid;

SEC("tp_btf/task_newtask")
int BPF_PROG(tp_btf_run, struct task_struct *task, u64 clone_flags)
{
	struct cgroup *cgrp = NULL;
	struct task_struct *acquired;

	if (local_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	acquired = bpf_task_acquire(task);
	if (!acquired)
		return 0;

	if (local_pid == acquired->tgid)
		goto out;

	cgrp = bpf_cgroup_from_id(cgid);
	if (!cgrp)
		goto out;

	if (bpf_task_under_cgroup(acquired, cgrp))
		remote_pid = acquired->tgid;

out:
	if (cgrp)
		bpf_cgroup_release(cgrp);
	bpf_task_release(acquired);

	return 0;
}

SEC("lsm.s/bpf")
int BPF_PROG(lsm_run, int cmd, union bpf_attr *attr, unsigned int size, bool kernel)
{
	struct cgroup *cgrp = NULL;
	struct task_struct *task;
	int ret = 0;

	task = bpf_get_current_task_btf();
	if (local_pid != task->pid)
		return 0;

	if (cmd != BPF_LINK_CREATE)
		return 0;

	/* 1 is the root cgroup */
	cgrp = bpf_cgroup_from_id(1);
	if (!cgrp)
		goto out;
	if (!bpf_task_under_cgroup(task, cgrp))
		ret = -1;
	bpf_cgroup_release(cgrp);

out:
	return ret;
}

char _license[] SEC("license") = "GPL";
