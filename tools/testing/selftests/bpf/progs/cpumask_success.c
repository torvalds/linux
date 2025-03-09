// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"
#include "cpumask_common.h"

char _license[] SEC("license") = "GPL";

int pid, nr_cpus;

struct kptr_nested {
	struct bpf_cpumask __kptr * mask;
};

struct kptr_nested_pair {
	struct bpf_cpumask __kptr * mask_1;
	struct bpf_cpumask __kptr * mask_2;
};

struct kptr_nested_mid {
	int dummy;
	struct kptr_nested m;
};

struct kptr_nested_deep {
	struct kptr_nested_mid ptrs[2];
	struct kptr_nested_pair ptr_pairs[3];
};

struct kptr_nested_deep_array_1_2 {
	int dummy;
	struct bpf_cpumask __kptr * mask[CPUMASK_KPTR_FIELDS_MAX];
};

struct kptr_nested_deep_array_1_1 {
	int dummy;
	struct kptr_nested_deep_array_1_2 d_2;
};

struct kptr_nested_deep_array_1 {
	long dummy;
	struct kptr_nested_deep_array_1_1 d_1;
};

struct kptr_nested_deep_array_2_2 {
	long dummy[2];
	struct bpf_cpumask __kptr * mask;
};

struct kptr_nested_deep_array_2_1 {
	int dummy;
	struct kptr_nested_deep_array_2_2 d_2[CPUMASK_KPTR_FIELDS_MAX];
};

struct kptr_nested_deep_array_2 {
	long dummy;
	struct kptr_nested_deep_array_2_1 d_1;
};

struct kptr_nested_deep_array_3_2 {
	long dummy[2];
	struct bpf_cpumask __kptr * mask;
};

struct kptr_nested_deep_array_3_1 {
	int dummy;
	struct kptr_nested_deep_array_3_2 d_2;
};

struct kptr_nested_deep_array_3 {
	long dummy;
	struct kptr_nested_deep_array_3_1 d_1[CPUMASK_KPTR_FIELDS_MAX];
};

private(MASK) static struct bpf_cpumask __kptr * global_mask_array[2];
private(MASK) static struct bpf_cpumask __kptr * global_mask_array_l2[2][1];
private(MASK) static struct bpf_cpumask __kptr * global_mask_array_one[1];
private(MASK) static struct kptr_nested global_mask_nested[2];
private(MASK_DEEP) static struct kptr_nested_deep global_mask_nested_deep;
private(MASK_1) static struct kptr_nested_deep_array_1 global_mask_nested_deep_array_1;
private(MASK_2) static struct kptr_nested_deep_array_2 global_mask_nested_deep_array_2;
private(MASK_3) static struct kptr_nested_deep_array_3 global_mask_nested_deep_array_3;

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
	int cpu;

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
int BPF_PROG(test_global_mask_array_one_rcu, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *local, *prev;

	if (!is_test_task())
		return 0;

	/* Kptr arrays with one element are special cased, being treated
	 * just like a single pointer.
	 */

	local = create_cpumask();
	if (!local)
		return 0;

	prev = bpf_kptr_xchg(&global_mask_array_one[0], local);
	if (prev) {
		bpf_cpumask_release(prev);
		err = 3;
		return 0;
	}

	bpf_rcu_read_lock();
	local = global_mask_array_one[0];
	if (!local) {
		err = 4;
		bpf_rcu_read_unlock();
		return 0;
	}

	bpf_rcu_read_unlock();

	return 0;
}

static int _global_mask_array_rcu(struct bpf_cpumask **mask0,
				  struct bpf_cpumask **mask1)
{
	struct bpf_cpumask *local;

	if (!is_test_task())
		return 0;

	/* Check if two kptrs in the array work and independently */

	local = create_cpumask();
	if (!local)
		return 0;

	bpf_rcu_read_lock();

	local = bpf_kptr_xchg(mask0, local);
	if (local) {
		err = 1;
		goto err_exit;
	}

	/* [<mask 0>, *] */
	if (!*mask0) {
		err = 2;
		goto err_exit;
	}

	if (!mask1)
		goto err_exit;

	/* [*, NULL] */
	if (*mask1) {
		err = 3;
		goto err_exit;
	}

	local = create_cpumask();
	if (!local) {
		err = 9;
		goto err_exit;
	}

	local = bpf_kptr_xchg(mask1, local);
	if (local) {
		err = 10;
		goto err_exit;
	}

	/* [<mask 0>, <mask 1>] */
	if (!*mask0 || !*mask1 || *mask0 == *mask1) {
		err = 11;
		goto err_exit;
	}

err_exit:
	if (local)
		bpf_cpumask_release(local);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_global_mask_array_rcu, struct task_struct *task, u64 clone_flags)
{
	return _global_mask_array_rcu(&global_mask_array[0], &global_mask_array[1]);
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_global_mask_array_l2_rcu, struct task_struct *task, u64 clone_flags)
{
	return _global_mask_array_rcu(&global_mask_array_l2[0][0], &global_mask_array_l2[1][0]);
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_global_mask_nested_rcu, struct task_struct *task, u64 clone_flags)
{
	return _global_mask_array_rcu(&global_mask_nested[0].mask, &global_mask_nested[1].mask);
}

/* Ensure that the field->offset has been correctly advanced from one
 * nested struct or array sub-tree to another. In the case of
 * kptr_nested_deep, it comprises two sub-trees: ktpr_1 and kptr_2.  By
 * calling bpf_kptr_xchg() on every single kptr in both nested sub-trees,
 * the verifier should reject the program if the field->offset of any kptr
 * is incorrect.
 *
 * For instance, if we have 10 kptrs in a nested struct and a program that
 * accesses each kptr individually with bpf_kptr_xchg(), the compiler
 * should emit instructions to access 10 different offsets if it works
 * correctly. If the field->offset values of any pair of them are
 * incorrectly the same, the number of unique offsets in btf_record for
 * this nested struct should be less than 10. The verifier should fail to
 * discover some of the offsets emitted by the compiler.
 *
 * Even if the field->offset values of kptrs are not duplicated, the
 * verifier should fail to find a btf_field for the instruction accessing a
 * kptr if the corresponding field->offset is pointing to a random
 * incorrect offset.
 */
SEC("tp_btf/task_newtask")
int BPF_PROG(test_global_mask_nested_deep_rcu, struct task_struct *task, u64 clone_flags)
{
	int r, i;

	r = _global_mask_array_rcu(&global_mask_nested_deep.ptrs[0].m.mask,
				   &global_mask_nested_deep.ptrs[1].m.mask);
	if (r)
		return r;

	for (i = 0; i < 3; i++) {
		r = _global_mask_array_rcu(&global_mask_nested_deep.ptr_pairs[i].mask_1,
					   &global_mask_nested_deep.ptr_pairs[i].mask_2);
		if (r)
			return r;
	}
	return 0;
}

SEC("tp_btf/task_newtask")
int BPF_PROG(test_global_mask_nested_deep_array_rcu, struct task_struct *task, u64 clone_flags)
{
	int i;

	for (i = 0; i < CPUMASK_KPTR_FIELDS_MAX; i++)
		_global_mask_array_rcu(&global_mask_nested_deep_array_1.d_1.d_2.mask[i], NULL);

	for (i = 0; i < CPUMASK_KPTR_FIELDS_MAX; i++)
		_global_mask_array_rcu(&global_mask_nested_deep_array_2.d_1.d_2[i].mask, NULL);

	for (i = 0; i < CPUMASK_KPTR_FIELDS_MAX; i++)
		_global_mask_array_rcu(&global_mask_nested_deep_array_3.d_1[i].d_2.mask, NULL);

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

SEC("tp_btf/task_newtask")
int BPF_PROG(test_populate_reject_small_mask, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *local;
	u8 toofewbits;
	int ret;

	if (!is_test_task())
		return 0;

	local = create_cpumask();
	if (!local)
		return 0;

	/* The kfunc should prevent this operation */
	ret = bpf_cpumask_populate((struct cpumask *)local, &toofewbits, sizeof(toofewbits));
	if (ret != -EACCES)
		err = 2;

	bpf_cpumask_release(local);

	return 0;
}

/* Mask is guaranteed to be large enough for bpf_cpumask_t. */
#define CPUMASK_TEST_MASKLEN (sizeof(cpumask_t))

/* Add an extra word for the test_populate_reject_unaligned test. */
u64 bits[CPUMASK_TEST_MASKLEN / 8 + 1];
extern bool CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS __kconfig __weak;

SEC("tp_btf/task_newtask")
int BPF_PROG(test_populate_reject_unaligned, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask;
	char *src;
	int ret;

	if (!is_test_task())
		return 0;

	/* Skip if unaligned accesses are fine for this arch.  */
	if (CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
		return 0;

	mask = bpf_cpumask_create();
	if (!mask) {
		err = 1;
		return 0;
	}

	/* Misalign the source array by a byte. */
	src = &((char *)bits)[1];

	ret = bpf_cpumask_populate((struct cpumask *)mask, src, CPUMASK_TEST_MASKLEN);
	if (ret != -EINVAL)
		err = 2;

	bpf_cpumask_release(mask);

	return 0;
}


SEC("tp_btf/task_newtask")
int BPF_PROG(test_populate, struct task_struct *task, u64 clone_flags)
{
	struct bpf_cpumask *mask;
	bool bit;
	int ret;
	int i;

	if (!is_test_task())
		return 0;

	/* Set only odd bits. */
	__builtin_memset(bits, 0xaa, CPUMASK_TEST_MASKLEN);

	mask = bpf_cpumask_create();
	if (!mask) {
		err = 1;
		return 0;
	}

	/* Pass the entire bits array, the kfunc will only copy the valid bits. */
	ret = bpf_cpumask_populate((struct cpumask *)mask, bits, CPUMASK_TEST_MASKLEN);
	if (ret) {
		err = 2;
		goto out;
	}

	/*
	 * Test is there to appease the verifier. We cannot directly
	 * access NR_CPUS, the upper bound for nr_cpus, so we infer
	 * it from the size of cpumask_t.
	 */
	if (nr_cpus < 0 || nr_cpus >= CPUMASK_TEST_MASKLEN * 8) {
		err = 3;
		goto out;
	}

	bpf_for(i, 0, nr_cpus) {
		/* Odd-numbered bits should be set, even ones unset. */
		bit = bpf_cpumask_test_cpu(i, (const struct cpumask *)mask);
		if (bit == (i % 2 != 0))
			continue;

		err = 4;
		break;
	}

out:
	bpf_cpumask_release(mask);

	return 0;
}

#undef CPUMASK_TEST_MASKLEN
