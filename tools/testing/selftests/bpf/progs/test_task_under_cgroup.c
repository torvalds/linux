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
int BPF_PROG(handle__task_newtask, struct task_struct *task, u64 clone_flags)
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

char _license[] SEC("license") = "GPL";
