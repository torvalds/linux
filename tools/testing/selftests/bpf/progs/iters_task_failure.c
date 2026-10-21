// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Chuyi Zhou <zhouchuyi@bytedance.com> */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

struct cgroup *bpf_cgroup_from_id(u64 cgid) __ksym;
void bpf_cgroup_release(struct cgroup *p) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("kernel func bpf_iter_task_new requires RCU critical section protection")
int BPF_PROG(iter_tasks_without_lock)
{
	struct task_struct *pos;

	bpf_for_each(task, pos, NULL, BPF_TASK_ITER_ALL_PROCS) {

	}
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("kernel func bpf_iter_css_new requires RCU critical section protection")
int BPF_PROG(iter_css_without_lock)
{
	u64 cg_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp = bpf_cgroup_from_id(cg_id);
	struct cgroup_subsys_state *root_css, *pos;

	if (!cgrp)
		return 0;
	root_css = &cgrp->self;

	bpf_for_each(css, pos, root_css, BPF_CGROUP_ITER_DESCENDANTS_POST) {

	}
	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("expected an RCU CS when using bpf_iter_task_next")
int BPF_PROG(iter_tasks_lock_and_unlock)
{
	struct task_struct *pos;

	bpf_rcu_read_lock();
	bpf_for_each(task, pos, NULL, BPF_TASK_ITER_ALL_PROCS) {
		bpf_rcu_read_unlock();

		bpf_rcu_read_lock();
	}
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("expected an RCU CS when using bpf_iter_task_next")
__flag(BPF_F_TEST_STATE_FREQ)
int BPF_PROG(iter_tasks_rcu_state_pruning)
{
	struct bpf_iter_task it;

	asm volatile (
		"call %[bpf_rcu_read_lock];"
		"r1 = %[it];"
		"r2 = 0;"
		"r3 = 0;" /* BPF_TASK_ITER_ALL_PROCS */
		"call %[bpf_iter_task_new];"

		"call %[bpf_get_prandom_u32];"
		"if w0 == 0 goto unprotected_%=;"

		/* Keep the outer RCU lock active on the straight-line path. */
		"call %[bpf_rcu_read_lock];"
		"call %[bpf_rcu_read_unlock];"
		"goto merge_%=;"

	"unprotected_%=:"
		/* Create an unprotected gap on the taken path. */
		"call %[bpf_rcu_read_unlock];"
		"call %[bpf_rcu_read_lock];"

	"merge_%=: r1 = %[it];"
		"call %[bpf_iter_task_next];"
		"r1 = %[it];"
		"call %[bpf_iter_task_destroy];"
		"call %[bpf_rcu_read_unlock];"
		:
		: __imm_ptr(it),
		  __imm(bpf_get_prandom_u32),
		  __imm(bpf_iter_task_new),
		  __imm(bpf_iter_task_next),
		  __imm(bpf_iter_task_destroy),
		  __imm(bpf_rcu_read_lock),
		  __imm(bpf_rcu_read_unlock)
		: __clobber_common
	);

	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("expected an RCU CS when using bpf_iter_css_next")
int BPF_PROG(iter_css_lock_and_unlock)
{
	u64 cg_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp = bpf_cgroup_from_id(cg_id);
	struct cgroup_subsys_state *root_css, *pos;

	if (!cgrp)
		return 0;
	root_css = &cgrp->self;

	bpf_rcu_read_lock();
	bpf_for_each(css, pos, root_css, BPF_CGROUP_ITER_DESCENDANTS_POST) {
		bpf_rcu_read_unlock();

		bpf_rcu_read_lock();
	}
	bpf_rcu_read_unlock();
	bpf_cgroup_release(cgrp);
	return 0;
}

SEC("?fentry/" SYS_PREFIX "sys_getpgid")
__failure __msg("css_task_iter is only allowed in bpf_lsm, bpf_iter and sleepable progs")
int BPF_PROG(iter_css_task_for_each)
{
	u64 cg_id = bpf_get_current_cgroup_id();
	struct cgroup *cgrp = bpf_cgroup_from_id(cg_id);
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
