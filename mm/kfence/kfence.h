/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel Electric-Fence (KFENCE). For more info please see
 * Documentation/dev-tools/kfence.rst.
 *
 * Copyright (C) 2020, Google LLC.
 */

#ifndef MM_KFENCE_KFENCE_H
#define MM_KFENCE_KFENCE_H

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "../slab.h" /* for struct kmem_cache */

/*
 * Get the canary byte pattern for @addr. Use a pattern that varies based on the
 * lower 3 bits of the address, to detect memory corruptions with higher
 * probability, where similar constants are used.
 */
#define KFENCE_CANARY_PATTERN(addr) ((u8)0xaa ^ (u8)((unsigned long)(addr) & 0x7))

/* Maximum stack depth for reports. */
#define KFENCE_STACK_DEPTH 64

/* KFENCE object states. */
enum kfence_object_state {
	KFENCE_OBJECT_UNUSED,		/* Object is unused. */
	KFENCE_OBJECT_ALLOCATED,	/* Object is currently allocated. */
	KFENCE_OBJECT_FREED,		/* Object was allocated, and then freed. */
};

/* Alloc/free tracking information. */
struct kfence_track {
	pid_t pid;
	int cpu;
	u64 ts_nsec;
	int num_stack_entries;
	unsigned long stack_entries[KFENCE_STACK_DEPTH];
};

/* KFENCE metadata per guarded allocation. */
struct kfence_metadata {
	struct list_head list;		/* Freelist node; access under kfence_freelist_lock. */
	struct rcu_head rcu_head;	/* For delayed freeing. */

	/*
	 * Lock protecting below data; to ensure consistency of the below data,
	 * since the following may execute concurrently: __kfence_alloc(),
	 * __kfence_free(), kfence_handle_page_fault(). However, note that we
	 * cannot grab the same metadata off the freelist twice, and multiple
	 * __kfence_alloc() cannot run concurrently on the same metadata.
	 */
	raw_spinlock_t lock;

	/* The current state of the object; see above. */
	enum kfence_object_state state;

	/*
	 * Allocated object address; cannot be calculated from size, because of
	 * alignment requirements.
	 *
	 * Invariant: ALIGN_DOWN(addr, PAGE_SIZE) is constant.
	 */
	unsigned long addr;

	/*
	 * The size of the original allocation.
	 */
	size_t size;

	/*
	 * The kmem_cache cache of the last allocation; NULL if never allocated
	 * or the cache has already been destroyed.
	 */
	struct kmem_cache *cache;

	/*
	 * In case of an invalid access, the page that was unprotected; we
	 * optimistically only store one address.
	 */
	unsigned long unprotected_page;

	/* Allocation and free stack information. */
	struct kfence_track alloc_track;
	struct kfence_track free_track;
	/* For updating alloc_covered on frees. */
	u32 alloc_stack_hash;
#ifdef CONFIG_MEMCG
	struct obj_cgroup *objcg;
#endif
};

extern struct kfence_metadata kfence_metadata[CONFIG_KFENCE_NUM_OBJECTS];

static inline struct kfence_metadata *addr_to_metadata(unsigned long addr)
{
	long index;

	/* The checks do not affect performance; only called from slow-paths. */

	if (!is_kfence_address((void *)addr))
		return NULL;

	/*
	 * May be an invalid index if called with an address at the edge of
	 * __kfence_pool, in which case we would report an "invalid access"
	 * error.
	 */
	index = (addr - (unsigned long)__kfence_pool) / (PAGE_SIZE * 2) - 1;
	if (index < 0 || index >= CONFIG_KFENCE_NUM_OBJECTS)
		return NULL;

	return &kfence_metadata[index];
}

/* KFENCE error types for report generation. */
enum kfence_error_type {
	KFENCE_ERROR_OOB,		/* Detected a out-of-bounds access. */
	KFENCE_ERROR_UAF,		/* Detected a use-after-free access. */
	KFENCE_ERROR_CORRUPTION,	/* Detected a memory corruption on free. */
	KFENCE_ERROR_INVALID,		/* Invalid access of unknown type. */
	KFENCE_ERROR_INVALID_FREE,	/* Invalid free. */
};

void kfence_report_error(unsigned long address, bool is_write, struct pt_regs *regs,
			 const struct kfence_metadata *meta, enum kfence_error_type type);

void kfence_print_object(struct seq_file *seq, const struct kfence_metadata *meta);

#endif /* MM_KFENCE_KFENCE_H */
