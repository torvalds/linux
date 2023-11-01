// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Chuyi Zhou <zhouchuyi@bytedance.com> */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

pid_t target_pid;
u64 root_cg_id, leaf_cg_id;
u64 first_cg_id, last_cg_id;

int pre_order_cnt, post_order_cnt, tree_high;

struct cgroup *bpf_cgroup_from_id(u64 cgid) __ksym;
void bpf_cgroup_release(struct cgroup *p) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

SEC("fentry.s/" SYS_PREFIX "sys_getpgid")
int iter_css_for_each(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct cgroup_subsys_state *root_css, *leaf_css, *pos;
	struct cgroup *root_cgrp, *leaf_cgrp, *cur_cgrp;

	if (cur_task->pid != target_pid)
		return 0;

	root_cgrp = bpf_cgroup_from_id(root_cg_id);

	if (!root_cgrp)
		return 0;

	leaf_cgrp = bpf_cgroup_from_id(leaf_cg_id);

	if (!leaf_cgrp) {
		bpf_cgroup_release(root_cgrp);
		return 0;
	}
	root_css = &root_cgrp->self;
	leaf_css = &leaf_cgrp->self;
	pre_order_cnt = post_order_cnt = tree_high = 0;
	first_cg_id = last_cg_id = 0;

	bpf_rcu_read_lock();
	bpf_for_each(css, pos, root_css, BPF_CGROUP_ITER_DESCENDANTS_POST) {
		cur_cgrp = pos->cgroup;
		post_order_cnt++;
		last_cg_id = cur_cgrp->kn->id;
	}

	bpf_for_each(css, pos, root_css, BPF_CGROUP_ITER_DESCENDANTS_PRE) {
		cur_cgrp = pos->cgroup;
		pre_order_cnt++;
		if (!first_cg_id)
			first_cg_id = cur_cgrp->kn->id;
	}

	bpf_for_each(css, pos, leaf_css, BPF_CGROUP_ITER_ANCESTORS_UP)
		tree_high++;

	bpf_for_each(css, pos, root_css, BPF_CGROUP_ITER_ANCESTORS_UP)
		tree_high--;
	bpf_rcu_read_unlock();
	bpf_cgroup_release(root_cgrp);
	bpf_cgroup_release(leaf_cgrp);
	return 0;
}
