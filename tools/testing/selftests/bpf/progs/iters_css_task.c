// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Chuyi Zhou <zhouchuyi@bytedance.com> */

#include "vmlinux.h"
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

struct cgroup *bpf_cgroup_acquire(struct cgroup *p) __ksym;
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

static inline u64 cgroup_id(struct cgroup *cgrp)
{
	return cgrp->kn->id;
}

SEC("?iter/cgroup")
int cgroup_id_printer(struct bpf_iter__cgroup *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct cgroup *cgrp = ctx->cgroup;
	struct cgroup_subsys_state *css;
	struct task_struct *task;

	/* epilogue */
	if (cgrp == NULL) {
		BPF_SEQ_PRINTF(seq, "epilogue\n");
		return 0;
	}

	/* prologue */
	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "prologue\n");

	BPF_SEQ_PRINTF(seq, "%8llu\n", cgroup_id(cgrp));

	css = &cgrp->self;
	css_task_cnt = 0;
	bpf_for_each(css_task, task, css, CSS_TASK_ITER_PROCS) {
		if (task->pid == target_pid)
			css_task_cnt++;
	}

	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
int BPF_PROG(iter_css_task_for_each_sleep)
{
	u64 cgrp_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp = bpf_cgroup_from_id(cgrp_id);
	struct cgroup_subsys_state *css;
	struct task_struct *task;

	if (cgrp == NULL)
		return 0;
	css = &cgrp->self;

	bpf_for_each(css_task, task, css, CSS_TASK_ITER_PROCS) {

	}
	bpf_cgroup_release(cgrp);
	return 0;
}
