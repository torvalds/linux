// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Meta, Inc */
#include <linux/bpf.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/cpumask.h>

/**
 * struct bpf_cpumask - refcounted BPF cpumask wrapper structure
 * @cpumask:	The actual cpumask embedded in the struct.
 * @usage:	Object reference counter. When the refcount goes to 0, the
 *		memory is released back to the BPF allocator, which provides
 *		RCU safety.
 *
 * Note that we explicitly embed a cpumask_t rather than a cpumask_var_t.  This
 * is done to avoid confusing the verifier due to the typedef of cpumask_var_t
 * changing depending on whether CONFIG_CPUMASK_OFFSTACK is defined or not. See
 * the details in <linux/cpumask.h>. The consequence is that this structure is
 * likely a bit larger than it needs to be when CONFIG_CPUMASK_OFFSTACK is
 * defined due to embedding the whole NR_CPUS-size bitmap, but the extra memory
 * overhead is minimal. For the more typical case of CONFIG_CPUMASK_OFFSTACK
 * not being defined, the structure is the same size regardless.
 */
struct bpf_cpumask {
	cpumask_t cpumask;
	refcount_t usage;
};

static struct bpf_mem_alloc bpf_cpumask_ma;

static bool cpu_valid(u32 cpu)
{
	return cpu < nr_cpu_ids;
}

__bpf_kfunc_start_defs();

/**
 * bpf_cpumask_create() - Create a mutable BPF cpumask.
 *
 * Allocates a cpumask that can be queried, mutated, acquired, and released by
 * a BPF program. The cpumask returned by this function must either be embedded
 * in a map as a kptr, or freed with bpf_cpumask_release().
 *
 * bpf_cpumask_create() allocates memory using the BPF memory allocator, and
 * will not block. It may return NULL if no memory is available.
 */
__bpf_kfunc struct bpf_cpumask *bpf_cpumask_create(void)
{
	struct bpf_cpumask *cpumask;

	/* cpumask must be the first element so struct bpf_cpumask be cast to struct cpumask. */
	BUILD_BUG_ON(offsetof(struct bpf_cpumask, cpumask) != 0);

	cpumask = bpf_mem_cache_alloc(&bpf_cpumask_ma);
	if (!cpumask)
		return NULL;

	memset(cpumask, 0, sizeof(*cpumask));
	refcount_set(&cpumask->usage, 1);

	return cpumask;
}

/**
 * bpf_cpumask_acquire() - Acquire a reference to a BPF cpumask.
 * @cpumask: The BPF cpumask being acquired. The cpumask must be a trusted
 *	     pointer.
 *
 * Acquires a reference to a BPF cpumask. The cpumask returned by this function
 * must either be embedded in a map as a kptr, or freed with
 * bpf_cpumask_release().
 */
__bpf_kfunc struct bpf_cpumask *bpf_cpumask_acquire(struct bpf_cpumask *cpumask)
{
	refcount_inc(&cpumask->usage);
	return cpumask;
}

/**
 * bpf_cpumask_release() - Release a previously acquired BPF cpumask.
 * @cpumask: The cpumask being released.
 *
 * Releases a previously acquired reference to a BPF cpumask. When the final
 * reference of the BPF cpumask has been released, it is subsequently freed in
 * an RCU callback in the BPF memory allocator.
 */
__bpf_kfunc void bpf_cpumask_release(struct bpf_cpumask *cpumask)
{
	if (!refcount_dec_and_test(&cpumask->usage))
		return;

	migrate_disable();
	bpf_mem_cache_free_rcu(&bpf_cpumask_ma, cpumask);
	migrate_enable();
}

/**
 * bpf_cpumask_first() - Get the index of the first nonzero bit in the cpumask.
 * @cpumask: The cpumask being queried.
 *
 * Find the index of the first nonzero bit of the cpumask. A struct bpf_cpumask
 * pointer may be safely passed to this function.
 */
__bpf_kfunc u32 bpf_cpumask_first(const struct cpumask *cpumask)
{
	return cpumask_first(cpumask);
}

/**
 * bpf_cpumask_first_zero() - Get the index of the first unset bit in the
 *			      cpumask.
 * @cpumask: The cpumask being queried.
 *
 * Find the index of the first unset bit of the cpumask. A struct bpf_cpumask
 * pointer may be safely passed to this function.
 */
__bpf_kfunc u32 bpf_cpumask_first_zero(const struct cpumask *cpumask)
{
	return cpumask_first_zero(cpumask);
}

/**
 * bpf_cpumask_first_and() - Return the index of the first nonzero bit from the
 *			     AND of two cpumasks.
 * @src1: The first cpumask.
 * @src2: The second cpumask.
 *
 * Find the index of the first nonzero bit of the AND of two cpumasks.
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc u32 bpf_cpumask_first_and(const struct cpumask *src1,
				      const struct cpumask *src2)
{
	return cpumask_first_and(src1, src2);
}

/**
 * bpf_cpumask_set_cpu() - Set a bit for a CPU in a BPF cpumask.
 * @cpu: The CPU to be set in the cpumask.
 * @cpumask: The BPF cpumask in which a bit is being set.
 */
__bpf_kfunc void bpf_cpumask_set_cpu(u32 cpu, struct bpf_cpumask *cpumask)
{
	if (!cpu_valid(cpu))
		return;

	cpumask_set_cpu(cpu, (struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_clear_cpu() - Clear a bit for a CPU in a BPF cpumask.
 * @cpu: The CPU to be cleared from the cpumask.
 * @cpumask: The BPF cpumask in which a bit is being cleared.
 */
__bpf_kfunc void bpf_cpumask_clear_cpu(u32 cpu, struct bpf_cpumask *cpumask)
{
	if (!cpu_valid(cpu))
		return;

	cpumask_clear_cpu(cpu, (struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_test_cpu() - Test whether a CPU is set in a cpumask.
 * @cpu: The CPU being queried for.
 * @cpumask: The cpumask being queried for containing a CPU.
 *
 * Return:
 * * true  - @cpu is set in the cpumask
 * * false - @cpu was not set in the cpumask, or @cpu is an invalid cpu.
 */
__bpf_kfunc bool bpf_cpumask_test_cpu(u32 cpu, const struct cpumask *cpumask)
{
	if (!cpu_valid(cpu))
		return false;

	return cpumask_test_cpu(cpu, (struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_test_and_set_cpu() - Atomically test and set a CPU in a BPF cpumask.
 * @cpu: The CPU being set and queried for.
 * @cpumask: The BPF cpumask being set and queried for containing a CPU.
 *
 * Return:
 * * true  - @cpu is set in the cpumask
 * * false - @cpu was not set in the cpumask, or @cpu is invalid.
 */
__bpf_kfunc bool bpf_cpumask_test_and_set_cpu(u32 cpu, struct bpf_cpumask *cpumask)
{
	if (!cpu_valid(cpu))
		return false;

	return cpumask_test_and_set_cpu(cpu, (struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_test_and_clear_cpu() - Atomically test and clear a CPU in a BPF
 *				      cpumask.
 * @cpu: The CPU being cleared and queried for.
 * @cpumask: The BPF cpumask being cleared and queried for containing a CPU.
 *
 * Return:
 * * true  - @cpu is set in the cpumask
 * * false - @cpu was not set in the cpumask, or @cpu is invalid.
 */
__bpf_kfunc bool bpf_cpumask_test_and_clear_cpu(u32 cpu, struct bpf_cpumask *cpumask)
{
	if (!cpu_valid(cpu))
		return false;

	return cpumask_test_and_clear_cpu(cpu, (struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_setall() - Set all of the bits in a BPF cpumask.
 * @cpumask: The BPF cpumask having all of its bits set.
 */
__bpf_kfunc void bpf_cpumask_setall(struct bpf_cpumask *cpumask)
{
	cpumask_setall((struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_clear() - Clear all of the bits in a BPF cpumask.
 * @cpumask: The BPF cpumask being cleared.
 */
__bpf_kfunc void bpf_cpumask_clear(struct bpf_cpumask *cpumask)
{
	cpumask_clear((struct cpumask *)cpumask);
}

/**
 * bpf_cpumask_and() - AND two cpumasks and store the result.
 * @dst: The BPF cpumask where the result is being stored.
 * @src1: The first input.
 * @src2: The second input.
 *
 * Return:
 * * true  - @dst has at least one bit set following the operation
 * * false - @dst is empty following the operation
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc bool bpf_cpumask_and(struct bpf_cpumask *dst,
				 const struct cpumask *src1,
				 const struct cpumask *src2)
{
	return cpumask_and((struct cpumask *)dst, src1, src2);
}

/**
 * bpf_cpumask_or() - OR two cpumasks and store the result.
 * @dst: The BPF cpumask where the result is being stored.
 * @src1: The first input.
 * @src2: The second input.
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc void bpf_cpumask_or(struct bpf_cpumask *dst,
				const struct cpumask *src1,
				const struct cpumask *src2)
{
	cpumask_or((struct cpumask *)dst, src1, src2);
}

/**
 * bpf_cpumask_xor() - XOR two cpumasks and store the result.
 * @dst: The BPF cpumask where the result is being stored.
 * @src1: The first input.
 * @src2: The second input.
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc void bpf_cpumask_xor(struct bpf_cpumask *dst,
				 const struct cpumask *src1,
				 const struct cpumask *src2)
{
	cpumask_xor((struct cpumask *)dst, src1, src2);
}

/**
 * bpf_cpumask_equal() - Check two cpumasks for equality.
 * @src1: The first input.
 * @src2: The second input.
 *
 * Return:
 * * true   - @src1 and @src2 have the same bits set.
 * * false  - @src1 and @src2 differ in at least one bit.
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc bool bpf_cpumask_equal(const struct cpumask *src1, const struct cpumask *src2)
{
	return cpumask_equal(src1, src2);
}

/**
 * bpf_cpumask_intersects() - Check two cpumasks for overlap.
 * @src1: The first input.
 * @src2: The second input.
 *
 * Return:
 * * true   - @src1 and @src2 have at least one of the same bits set.
 * * false  - @src1 and @src2 don't have any of the same bits set.
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc bool bpf_cpumask_intersects(const struct cpumask *src1, const struct cpumask *src2)
{
	return cpumask_intersects(src1, src2);
}

/**
 * bpf_cpumask_subset() - Check if a cpumask is a subset of another.
 * @src1: The first cpumask being checked as a subset.
 * @src2: The second cpumask being checked as a superset.
 *
 * Return:
 * * true   - All of the bits of @src1 are set in @src2.
 * * false  - At least one bit in @src1 is not set in @src2.
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc bool bpf_cpumask_subset(const struct cpumask *src1, const struct cpumask *src2)
{
	return cpumask_subset(src1, src2);
}

/**
 * bpf_cpumask_empty() - Check if a cpumask is empty.
 * @cpumask: The cpumask being checked.
 *
 * Return:
 * * true   - None of the bits in @cpumask are set.
 * * false  - At least one bit in @cpumask is set.
 *
 * A struct bpf_cpumask pointer may be safely passed to @cpumask.
 */
__bpf_kfunc bool bpf_cpumask_empty(const struct cpumask *cpumask)
{
	return cpumask_empty(cpumask);
}

/**
 * bpf_cpumask_full() - Check if a cpumask has all bits set.
 * @cpumask: The cpumask being checked.
 *
 * Return:
 * * true   - All of the bits in @cpumask are set.
 * * false  - At least one bit in @cpumask is cleared.
 *
 * A struct bpf_cpumask pointer may be safely passed to @cpumask.
 */
__bpf_kfunc bool bpf_cpumask_full(const struct cpumask *cpumask)
{
	return cpumask_full(cpumask);
}

/**
 * bpf_cpumask_copy() - Copy the contents of a cpumask into a BPF cpumask.
 * @dst: The BPF cpumask being copied into.
 * @src: The cpumask being copied.
 *
 * A struct bpf_cpumask pointer may be safely passed to @src.
 */
__bpf_kfunc void bpf_cpumask_copy(struct bpf_cpumask *dst, const struct cpumask *src)
{
	cpumask_copy((struct cpumask *)dst, src);
}

/**
 * bpf_cpumask_any_distribute() - Return a random set CPU from a cpumask.
 * @cpumask: The cpumask being queried.
 *
 * Return:
 * * A random set bit within [0, num_cpus) if at least one bit is set.
 * * >= num_cpus if no bit is set.
 *
 * A struct bpf_cpumask pointer may be safely passed to @src.
 */
__bpf_kfunc u32 bpf_cpumask_any_distribute(const struct cpumask *cpumask)
{
	return cpumask_any_distribute(cpumask);
}

/**
 * bpf_cpumask_any_and_distribute() - Return a random set CPU from the AND of
 *				      two cpumasks.
 * @src1: The first cpumask.
 * @src2: The second cpumask.
 *
 * Return:
 * * A random set bit within [0, num_cpus) from the AND of two cpumasks, if at
 *   least one bit is set.
 * * >= num_cpus if no bit is set.
 *
 * struct bpf_cpumask pointers may be safely passed to @src1 and @src2.
 */
__bpf_kfunc u32 bpf_cpumask_any_and_distribute(const struct cpumask *src1,
					       const struct cpumask *src2)
{
	return cpumask_any_and_distribute(src1, src2);
}

__bpf_kfunc_end_defs();

BTF_SET8_START(cpumask_kfunc_btf_ids)
BTF_ID_FLAGS(func, bpf_cpumask_create, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_cpumask_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_cpumask_acquire, KF_ACQUIRE | KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_cpumask_first, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_first_zero, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_first_and, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_set_cpu, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_clear_cpu, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_test_cpu, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_test_and_set_cpu, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_test_and_clear_cpu, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_setall, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_clear, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_and, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_or, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_xor, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_equal, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_intersects, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_subset, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_empty, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_full, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_copy, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_any_distribute, KF_RCU)
BTF_ID_FLAGS(func, bpf_cpumask_any_and_distribute, KF_RCU)
BTF_SET8_END(cpumask_kfunc_btf_ids)

static const struct btf_kfunc_id_set cpumask_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &cpumask_kfunc_btf_ids,
};

BTF_ID_LIST(cpumask_dtor_ids)
BTF_ID(struct, bpf_cpumask)
BTF_ID(func, bpf_cpumask_release)

static int __init cpumask_kfunc_init(void)
{
	int ret;
	const struct btf_id_dtor_kfunc cpumask_dtors[] = {
		{
			.btf_id	      = cpumask_dtor_ids[0],
			.kfunc_btf_id = cpumask_dtor_ids[1]
		},
	};

	ret = bpf_mem_alloc_init(&bpf_cpumask_ma, sizeof(struct bpf_cpumask), false);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &cpumask_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &cpumask_kfunc_set);
	return  ret ?: register_btf_id_dtor_kfuncs(cpumask_dtors,
						   ARRAY_SIZE(cpumask_dtors),
						   THIS_MODULE);
}

late_initcall(cpumask_kfunc_init);
