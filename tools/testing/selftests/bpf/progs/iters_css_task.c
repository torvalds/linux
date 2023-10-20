// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Chuyi Zhou <zhouchuyi@bytedance.com> */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

struct cgroup *bpf_cgroup_from_id(u64 cgid) __ksym;
void bpf_cgroup_release(struct cgroup *p) __ksym;

pid_t target_pid;
int css_task_cnt;
u64 cg_id;

SEC("lsm/file_mprotect")
int BPF_PROG(iter_css_task_for_each, struct vm_area_struct *vma,
	    unsigned long reqprot, unsigned long prot, int ret)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct cgroup_subsys_state *css;
	struct task_struct *task;
	struct cgroup *cgrp;

	if (cur_task->pid != target_pid)
		return ret;

	cgrp = bpf_cgroup_from_id(cg_id);

	if (!cgrp)
		return -EPERM;

	css = &cgrp->self;
	css_task_cnt = 0;

	bpf_for_each(css_task, task, css, CSS_TASK_ITER_PROCS)
		if (task->pid == target_pid)
			css_task_cnt++;

	bpf_cgroup_release(cgrp);

	return -EPERM;
}
