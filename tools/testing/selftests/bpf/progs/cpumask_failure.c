// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#include "cpumask_common.h"

char _license[] SEC("license") = "GPL";

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */

SEC("tp_btf/task_newtask")
__failure __msg("Unreleased reference")
int BPF_PROG(test_alloc_no_release, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	cpumask = create_cpumask();
	__sink(cpumask);

	/* cpumask is never released. */
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("NULL pointer passed to trusted arg0")
int BPF_PROG(test_alloc_double_release, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	cpumask = create_cpumask();

	/* cpumask is released twice. */
	bpf_cpumask_release(cpumask);
	bpf_cpumask_release(cpumask);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("must be referenced")
int BPF_PROG(test_acquire_wrong_cpumask, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	/* Can't acquire a non-struct bpf_cpumask. */
	cpumask = bpf_cpumask_acquire((struct bpf_cpumask *)task->cpus_ptr);
	__sink(cpumask);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("bpf_cpumask_set_cpu args#1 expected pointer to STRUCT bpf_cpumask")
int BPF_PROG(test_mutate_cpumask, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	/* Can't set the CPU of a non-struct bpf_cpumask. */
	bpf_cpumask_set_cpu(0, (struct bpf_cpumask *)task->cpus_ptr);
	__sink(cpumask);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Unreleased reference")
int BPF_PROG(test_insert_remove_no_release, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;
	struct __cpumask_map_value *v;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	if (cpumask_map_insert(cpumask))
		return 0;

	v = cpumask_map_value_lookup();
	if (!v)
		return 0;

	cpumask = bpf_kptr_xchg(&v->cpumask, NULL);

	/* cpumask is never released. */
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("NULL pointer passed to trusted arg0")
int BPF_PROG(test_cpumask_null, struct task_struct *task, u64 clone_flags)
{
  /* NULL passed to KF_TRUSTED_ARGS kfunc. */
	bpf_cpumask_empty(NULL);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("R2 must be a rcu pointer")
int BPF_PROG(test_global_mask_out_of_rcu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *local, *prev;

	local = create_cpumask();
	if (!local)
		return 0;

	prev = bpf_kptr_xchg(&global_mask, local);
	if (prev) {
		bpf_cpumask_release(prev);
		err = 3;
		return 0;
	}

	bpf_rcu_read_lock();
	local = global_mask;
	if (!local) {
		err = 4;
		bpf_rcu_read_unlock();
		return 0;
	}

	bpf_rcu_read_unlock();

	/* RCU region is exited before calling KF_RCU kfunc. */

	bpf_cpumask_test_cpu(0, (const struct cpumask *)local);

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("NULL pointer passed to trusted arg1")
int BPF_PROG(test_global_mask_no_null_check, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *local, *prev;

	local = create_cpumask();
	if (!local)
		return 0;

	prev = bpf_kptr_xchg(&global_mask, local);
	if (prev) {
		bpf_cpumask_release(prev);
		err = 3;
		return 0;
	}

	bpf_rcu_read_lock();
	local = global_mask;

	/* No NULL check is performed on global cpumask kptr. */
	bpf_cpumask_test_cpu(0, (const struct cpumask *)local);

	bpf_rcu_read_unlock();

	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("Possibly NULL pointer passed to helper arg2")
int BPF_PROG(test_global_mask_rcu_no_null_check, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *prev, *curr;

	curr = bpf_cpumask_create();
	if (!curr)
		return 0;

	prev = bpf_kptr_xchg(&global_mask, curr);
	if (prev)
		bpf_cpumask_release(prev);

	bpf_rcu_read_lock();
	curr = global_mask;
	/* PTR_TO_BTF_ID | PTR_MAYBE_NULL | MEM_RCU passed to bpf_kptr_xchg() */
	prev = bpf_kptr_xchg(&global_mask, curr);
	bpf_rcu_read_unlock();
	if (prev)
		bpf_cpumask_release(prev);

	return 0;
}
