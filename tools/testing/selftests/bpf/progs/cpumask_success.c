// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"
#include "cpumask_common.h"

char _license[] SEC("license") = "GPL";

int pid, nr_cpus;

static bool is_test_task(void)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;

	return pid == cur_pid;
}

static bool create_cpumask_set(struct bpf_cpumask **out1,
			       struct bpf_cpumask **out2,
			       struct bpf_cpumask **out3,
			       struct bpf_cpumask **out4)
{
	struct bpf_cpumask *mask1, *mask2, *mask3, *mask4;

	mask1 = create_cpumask();
	if (!mask1)
		return false;

	mask2 = create_cpumask();
	if (!mask2) {
		bpf_cpumask_release(mask1);
		err = 3;
		return false;
	}

	mask3 = create_cpumask();
	if (!mask3) {
		bpf_cpumask_release(mask1);
		bpf_cpumask_release(mask2);
		err = 4;
		return false;
	}

	mask4 = create_cpumask();
	if (!mask4) {
		bpf_cpumask_release(mask1);
		bpf_cpumask_release(mask2);
		bpf_cpumask_release(mask3);
		err = 5;
		return false;
	}

	*out1 = mask1;
	*out2 = mask2;
	*out3 = mask3;
	*out4 = mask4;

	return true;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_alloc_free_cpumask, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	if (!is_test_task())
		return 0;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	bpf_cpumask_release(cpumask);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_set_clear_cpu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	if (!is_test_task())
		return 0;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	bpf_cpumask_set_cpu(0, cpumask);
	if (!bpf_cpumask_test_cpu(0, cast(cpumask))) {
		err = 3;
		goto release_exit;
	}

	bpf_cpumask_clear_cpu(0, cpumask);
	if (bpf_cpumask_test_cpu(0, cast(cpumask))) {
		err = 4;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(cpumask);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_setall_clear_cpu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	if (!is_test_task())
		return 0;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	bpf_cpumask_setall(cpumask);
	if (!bpf_cpumask_full(cast(cpumask))) {
		err = 3;
		goto release_exit;
	}

	bpf_cpumask_clear(cpumask);
	if (!bpf_cpumask_empty(cast(cpumask))) {
		err = 4;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(cpumask);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_first_firstzero_cpu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	if (!is_test_task())
		return 0;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	if (bpf_cpumask_first(cast(cpumask)) < nr_cpus) {
		err = 3;
		goto release_exit;
	}

	if (bpf_cpumask_first_zero(cast(cpumask)) != 0) {
		bpf_printk("first zero: %d", bpf_cpumask_first_zero(cast(cpumask)));
		err = 4;
		goto release_exit;
	}

	bpf_cpumask_set_cpu(0, cpumask);
	if (bpf_cpumask_first(cast(cpumask)) != 0) {
		err = 5;
		goto release_exit;
	}

	if (bpf_cpumask_first_zero(cast(cpumask)) != 1) {
		err = 6;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(cpumask);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_firstand_nocpu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask1, *mask2;
	u32 first;

	if (!is_test_task())
		return 0;

	mask1 = create_cpumask();
	if (!mask1)
		return 0;

	mask2 = create_cpumask();
	if (!mask2)
		goto release_exit;

	bpf_cpumask_set_cpu(0, mask1);
	bpf_cpumask_set_cpu(1, mask2);

	first = bpf_cpumask_first_and(cast(mask1), cast(mask2));
	if (first <= 1)
		err = 3;

release_exit:
	if (mask1)
		bpf_cpumask_release(mask1);
	if (mask2)
		bpf_cpumask_release(mask2);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_test_and_set_clear, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	if (!is_test_task())
		return 0;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	if (bpf_cpumask_test_and_set_cpu(0, cpumask)) {
		err = 3;
		goto release_exit;
	}

	if (!bpf_cpumask_test_and_set_cpu(0, cpumask)) {
		err = 4;
		goto release_exit;
	}

	if (!bpf_cpumask_test_and_clear_cpu(0, cpumask)) {
		err = 5;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(cpumask);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_and_or_xor, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask1, *mask2, *dst1, *dst2;

	if (!is_test_task())
		return 0;

	if (!create_cpumask_set(&mask1, &mask2, &dst1, &dst2))
		return 0;

	bpf_cpumask_set_cpu(0, mask1);
	bpf_cpumask_set_cpu(1, mask2);

	if (bpf_cpumask_and(dst1, cast(mask1), cast(mask2))) {
		err = 6;
		goto release_exit;
	}
	if (!bpf_cpumask_empty(cast(dst1))) {
		err = 7;
		goto release_exit;
	}

	bpf_cpumask_or(dst1, cast(mask1), cast(mask2));
	if (!bpf_cpumask_test_cpu(0, cast(dst1))) {
		err = 8;
		goto release_exit;
	}
	if (!bpf_cpumask_test_cpu(1, cast(dst1))) {
		err = 9;
		goto release_exit;
	}

	bpf_cpumask_xor(dst2, cast(mask1), cast(mask2));
	if (!bpf_cpumask_equal(cast(dst1), cast(dst2))) {
		err = 10;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(mask1);
	bpf_cpumask_release(mask2);
	bpf_cpumask_release(dst1);
	bpf_cpumask_release(dst2);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_intersects_subset, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask1, *mask2, *dst1, *dst2;

	if (!is_test_task())
		return 0;

	if (!create_cpumask_set(&mask1, &mask2, &dst1, &dst2))
		return 0;

	bpf_cpumask_set_cpu(0, mask1);
	bpf_cpumask_set_cpu(1, mask2);
	if (bpf_cpumask_intersects(cast(mask1), cast(mask2))) {
		err = 6;
		goto release_exit;
	}

	bpf_cpumask_or(dst1, cast(mask1), cast(mask2));
	if (!bpf_cpumask_subset(cast(mask1), cast(dst1))) {
		err = 7;
		goto release_exit;
	}

	if (!bpf_cpumask_subset(cast(mask2), cast(dst1))) {
		err = 8;
		goto release_exit;
	}

	if (bpf_cpumask_subset(cast(dst1), cast(mask1))) {
		err = 9;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(mask1);
	bpf_cpumask_release(mask2);
	bpf_cpumask_release(dst1);
	bpf_cpumask_release(dst2);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_copy_any_anyand, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask1, *mask2, *dst1, *dst2;
	u32 cpu;

	if (!is_test_task())
		return 0;

	if (!create_cpumask_set(&mask1, &mask2, &dst1, &dst2))
		return 0;

	bpf_cpumask_set_cpu(0, mask1);
	bpf_cpumask_set_cpu(1, mask2);
	bpf_cpumask_or(dst1, cast(mask1), cast(mask2));

	cpu = bpf_cpumask_any_distribute(cast(mask1));
	if (cpu != 0) {
		err = 6;
		goto release_exit;
	}

	cpu = bpf_cpumask_any_distribute(cast(dst2));
	if (cpu < nr_cpus) {
		err = 7;
		goto release_exit;
	}

	bpf_cpumask_copy(dst2, cast(dst1));
	if (!bpf_cpumask_equal(cast(dst1), cast(dst2))) {
		err = 8;
		goto release_exit;
	}

	cpu = bpf_cpumask_any_distribute(cast(dst2));
	if (cpu > 1) {
		err = 9;
		goto release_exit;
	}

	cpu = bpf_cpumask_any_and_distribute(cast(mask1), cast(mask2));
	if (cpu < nr_cpus) {
		err = 10;
		goto release_exit;
	}

release_exit:
	bpf_cpumask_release(mask1);
	bpf_cpumask_release(mask2);
	bpf_cpumask_release(dst1);
	bpf_cpumask_release(dst2);
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_insert_leave, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	if (cpumask_map_insert(cpumask))
		err = 3;

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_insert_remove_release, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *cpumask;
	struct __cpumask_map_value *v;

	cpumask = create_cpumask();
	if (!cpumask)
		return 0;

	if (cpumask_map_insert(cpumask)) {
		err = 3;
		return 0;
	}

	v = cpumask_map_value_lookup();
	if (!v) {
		err = 4;
		return 0;
	}

	cpumask = bpf_kptr_xchg(&v->cpumask, NULL);
	if (cpumask)
		bpf_cpumask_release(cpumask);
	else
		err = 5;

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_global_mask_rcu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *local, *prev;

	if (!is_test_task())
		return 0;

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

	bpf_cpumask_test_cpu(0, (const struct cpumask *)local);
	bpf_rcu_read_unlock();

	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_cpumask_weight, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *local;

	if (!is_test_task())
		return 0;

	local = create_cpumask();
	if (!local)
		return 0;

	if (bpf_cpumask_weight(cast(local)) != 0) {
		err = 3;
		goto out;
	}

	bpf_cpumask_set_cpu(0, local);
	if (bpf_cpumask_weight(cast(local)) != 1) {
		err = 4;
		goto out;
	}

	/*
	 * Make sure that adding additional CPUs changes the weight. Test to
	 * see whether the CPU was set to account for running on UP machines.
	 */
	bpf_cpumask_set_cpu(1, local);
	if (bpf_cpumask_test_cpu(1, cast(local)) && bpf_cpumask_weight(cast(local)) != 2) {
		err = 5;
		goto out;
	}

	bpf_cpumask_clear(local);
	if (bpf_cpumask_weight(cast(local)) != 0) {
		err = 6;
		goto out;
	}
out:
	bpf_cpumask_release(local);
	return 0;
}

SEC("tp_btf/task_newtask")
__success
int BPF_PROG(test_refcount_null_tracking, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask1, *mask2;

	mask1 = bpf_cpumask_create();
	mask2 = bpf_cpumask_create();

	if (!mask1 || !mask2)
		goto free_masks_return;

	bpf_cpumask_test_cpu(0, (const struct cpumask *)mask1);
	bpf_cpumask_test_cpu(0, (const struct cpumask *)mask2);

free_masks_return:
	if (mask1)
		bpf_cpumask_release(mask1);
	if (mask2)
		bpf_cpumask_release(mask2);
	return 0;
}
