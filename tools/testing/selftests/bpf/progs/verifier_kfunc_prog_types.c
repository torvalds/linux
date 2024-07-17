// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"
#include "cgrp_kfunc_common.h"
#include "cpumask_common.h"
#include "task_kfunc_common.h"

char _license[] SEC("license") = "GPL";

/***************
 * Task kfuncs *
 ***************/

static void task_kfunc_load_test(void)
{
	struct task_struct *current, *ref_1, *ref_2;

	current = bpf_get_current_task_btf();
	ref_1 = bpf_task_from_pid(current->pid);
	if (!ref_1)
		return;

	ref_2 = bpf_task_acquire(ref_1);
	if (ref_2)
		bpf_task_release(ref_2);
	bpf_task_release(ref_1);
}

SEC("raw_tp")
__failure __msg("calling kernel function")
int BPF_PROG(task_kfunc_raw_tp)
{
	task_kfunc_load_test();
	return 0;
}

SEC("syscall")
__success
int BPF_PROG(task_kfunc_syscall)
{
	task_kfunc_load_test();
	return 0;
}

/*****************
 * cgroup kfuncs *
 *****************/

static void cgrp_kfunc_load_test(void)
{
	struct cgroup *cgrp, *ref;

	cgrp = bpf_cgroup_from_id(0);
	if (!cgrp)
		return;

	ref = bpf_cgroup_acquire(cgrp);
	if (!ref) {
		bpf_cgroup_release(cgrp);
		return;
	}

	bpf_cgroup_release(ref);
	bpf_cgroup_release(cgrp);
}

SEC("raw_tp")
__failure __msg("calling kernel function")
int BPF_PROG(cgrp_kfunc_raw_tp)
{
	cgrp_kfunc_load_test();
	return 0;
}

SEC("syscall")
__success
int BPF_PROG(cgrp_kfunc_syscall)
{
	cgrp_kfunc_load_test();
	return 0;
}

/******************
 * cpumask kfuncs *
 ******************/

static void cpumask_kfunc_load_test(void)
{
	struct bpf_cpumask *alloc, *ref;

	alloc = bpf_cpumask_create();
	if (!alloc)
		return;

	ref = bpf_cpumask_acquire(alloc);
	bpf_cpumask_set_cpu(0, alloc);
	bpf_cpumask_test_cpu(0, (const struct cpumask *)ref);

	bpf_cpumask_release(ref);
	bpf_cpumask_release(alloc);
}

SEC("raw_tp")
__failure __msg("calling kernel function")
int BPF_PROG(cpumask_kfunc_raw_tp)
{
	cpumask_kfunc_load_test();
	return 0;
}

SEC("syscall")
__success
int BPF_PROG(cpumask_kfunc_syscall)
{
	cpumask_kfunc_load_test();
	return 0;
}
