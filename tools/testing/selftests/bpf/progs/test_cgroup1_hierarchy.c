// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

__u32 target_ancestor_level;
__u64 target_ancestor_cgid;
int target_pid, target_hid;

struct cgroup *bpf_task_get_cgroup1(struct task_struct *task, int hierarchy_id) __ksym;
struct cgroup *bpf_cgroup_ancestor(struct cgroup *cgrp, int level) __ksym;
void bpf_cgroup_release(struct cgroup *cgrp) __ksym;

static int bpf_link_create_verify(int cmd)
{
	struct cgroup *cgrp, *ancestor;
	struct task_struct *task;
	int ret = 0;

	if (cmd != BPF_LINK_CREATE)
		return 0;

	task = bpf_get_current_task_btf();

	/* Then it can run in parallel with others */
	if (task->pid != target_pid)
		return 0;

	cgrp = bpf_task_get_cgroup1(task, target_hid);
	if (!cgrp)
		return 0;

	/* Refuse it if its cgid or its ancestor's cgid is the target cgid */
	if (cgrp->kn->id == target_ancestor_cgid)
		ret = -1;

	ancestor = bpf_cgroup_ancestor(cgrp, target_ancestor_level);
	if (!ancestor)
		goto out;

	if (ancestor->kn->id == target_ancestor_cgid)
		ret = -1;
	bpf_cgroup_release(ancestor);

out:
	bpf_cgroup_release(cgrp);
	return ret;
}

SEC("lsm/bpf")
int BPF_PROG(lsm_run, int cmd, union bpf_attr *attr, unsigned int size, bool kernel)
{
	return bpf_link_create_verify(cmd);
}

SEC("lsm.s/bpf")
int BPF_PROG(lsm_s_run, int cmd, union bpf_attr *attr, unsigned int size, bool kernel)
{
	return bpf_link_create_verify(cmd);
}

SEC("fentry")
int BPF_PROG(fentry_run)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
