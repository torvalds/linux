// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic stack depot for storing stack traces.
 *
 * Some debugging tools need to save stack traces of certain events which can
 * be later presented to the user. For example, KASAN needs to safe alloc and
 * free stacks for each object, but storing two stack traces per object
 * requires too much memory (e.g. SLUB_DEBUG needs 256 bytes per object for
 * that).
 *
 * Instead, stack depot maintains a hashtable of unique stacktraces. Since alloc
 * and free stacks repeat a lot, we save about 100x space.
 * Stacks are never removed from depot, so we store them contiguously one after
 * another in a contiguos memory allocation.
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on code by Dmitry Chernenkov.
 */

#include <linux/gfp.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>
#include <linux/string.h>
#include <linux/types.h>

#define DEPOT_STACK_BITS (sizeof(depot_stack_handle_t) * 8)

#define STACK_ALLOC_NULL_PROTECTION_BITS 1
#define STACK_ALLOC_ORDER 2 /* 'Slab' size order for stack depot, 4 pages */
#define STACK_ALLOC_SIZE (1LL << (PAGE_SHIFT + STACK_ALLOC_ORDER))
#define STACK_ALLOC_ALIGN 4
#define STACK_ALLOC_OFFSET_BITS (STACK_ALLOC_ORDER + PAGE_SHIFT - \
					STACK_ALLOC_ALIGN)
#define STACK_ALLOC_INDEX_BITS (DEPOT_STACK_BITS - \
		STACK_ALLOC_NULL_PROTECTION_BITS - STACK_ALLOC_OFFSET_BITS)
#define STACK_ALLOC_SLABS_CAP 8192
#define STACK_ALLOC_MAX_SLABS \
	(((1LL << (STACK_ALLOC_INDEX_BITS)) < STACK_ALLOC_SLABS_CAP) ? \
	 (1LL << (STACK_ALLOC_INDEX_BITS)) : STACK_ALLOC_SLABS_CAP)

/* The compact structure to store the reference to stacks. */
union handle_parts {
	depot_stack_handle_t handle;
	struct {
		u32 slabindex : STACK_ALLOC_INDEX_BITS;
		u32 offset : STACK_ALLOC_OFFSET_BITS;
		u32 valid : STACK_ALLOC_NULL_PROTECTION_BITS;
	};
};

struct stack_record {
	struct stack_record *next;	/* Link in the hashtable */
	u32 hash;			/* Hash in the hastable */
	u32 size;			/* Number of frames in the stack */
	union handle_parts handle;
	unsigned long entries[1];	/* Variable-sized array of entries. */
};

static void *stack_slabs[STACK_ALLOC_MAX_SLABS];

static int depot_index;
static int next_slab_inited;
static size_t depot_offset;
static DEFINE_SPINLOCK(depot_lock);

static bool init_stack_slab(void **prealloc)
{
	if (!*prealloc)
		return false;
	/*
	 * This smp_load_acquire() pairs with smp_store_release() to
	 * |next_slab_inited| below and in depot_alloc_stack().
	 */
	if (smp_load_acquire(&next_slab_inited))
		return true;
	if (stack_slabs[depot_index] == NULL) {
		stack_slabs[depot_index] = *prealloc;
		*prealloc = NULL;
	} else {
		/* If this is the last depot slab, do not touch the next one. */
		if (depot_index + 1 < STACK_ALLOC_MAX_SLABS) {
			stack_slabs[depot_index + 1] = *prealloc;
			*prealloc = NULL;
		}
		/*
		 * This smp_store_release pairs with smp_load_acquire() from
		 * |next_slab_inited| above and in stack_depot_save().
		 */
		smp_store_release(&next_slab_inited, 1);
	}
	return true;
}

/* Allocation of a new stack in raw storage */
static struct stack_record *depot_alloc_stack(unsigned long *entries, int size,
		u32 hash, void **prealloc, gfp_t alloc_flags)
{
	int required_size = offsetof(struct stack_record, entries) +
		sizeof(unsigned long) * size;
	struct stack_record *stack;

	required_size = ALIGN(required_size, 1 << STACK_ALLOC_ALIGN);

	if (unlikely(depot_offset + required_size > STACK_ALLOC_SIZE)) {
		if (unlikely(depot_index + 1 >= STACK_ALLOC_MAX_SLABS)) {
			WARN_ONCE(1, "Stack depot reached limit capacity");
			return NULL;
		}
		depot_index++;
		depot_offset = 0;
		/*
		 * smp_store_release() here pairs with smp_load_acquire() from
		 * |next_slab_inited| in stack_depot_save() and
		 * init_stack_slab().
		 */
		if (depot_index + 1 < STACK_ALLOC_MAX_SLABS)
			smp_store_release(&next_slab_inited, 0);
	}
	init_stack_slab(prealloc);
	if (stack_slabs[depot_index] == NULL)
		return NULL;

	stack = stack_slabs[depot_index] + depot_offset;

	stack->hash = hash;
	stack->size = size;
	stack->handle.slabindex = depot_index;
	stack->handle.offset = depot_offset >> STACK_ALLOC_ALIGN;
	stack->handle.valid = 1;
	memcpy(stack->entries, entries, size * sizeof(unsigned long));
	depot_offset += required_size;

	return stack;
}

#define STACK_HASH_ORDER 20
#define STACK_HASH_SIZE (1L << STACK_HASH_ORDER)
#define STACK_HASH_MASK (STACK_HASH_SIZE - 1)
#define STACK_HASH_SEED 0x9747b28c

static struct stack_record *stack_table[STACK_HASH_SIZE] = {
	[0 ...	STACK_HASH_SIZE - 1] = NULL
};

/* Calculate hash for a stack */
static inline u32 hash_stack(unsigned long *entries, unsigned int size)
{
	return jhash2((u32 *)entries,
			       size * sizeof(unsigned long) / sizeof(u32),
			       STACK_HASH_SEED);
}

/* Use our own, non-instrumented version of memcmp().
 *
 * We actually don't care about the order, just the equality.
 */
static inline
int stackdepot_memcmp(const unsigned long *u1, const unsigned long *u2,
			unsigned int n)
{
	for ( ; n-- ; u1++, u2++) {
		if (*u1 != *u2)
			return 1;
	}
	return 0;
}

/* Find a stack that is equal to the one stored in entries in the hash */
static inline struct stack_record *find_stack(struct stack_record *bucket,
					     unsigned long *entries, int size,
					     u32 hash)
{
	struct stack_record *found;

	for (found = bucket; found; found = found->next) {
		if (found->hash == hash &&
		    found->size == size &&
		    !stackdepot_memcmp(entries, found->entries, size))
			return found;
	}
	return NULL;
}

/**
 * stack_depot_fetch - Fetch stack entries from a depot
 *
 * @handle:		Stack depot handle which was returned from
 *			stack_depot_save().
 * @entries:		Pointer to store the entries address
 *
 * Return: The number of trace entries for this depot.
 */
unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries)
{
	union handle_parts parts = { .handle = handle };
	void *slab = stack_slabs[parts.slabindex];
	size_t offset = parts.offset << STACK_ALLOC_ALIGN;
	struct stack_record *stack = slab + offset;

	*entries = stack->entries;
	return stack->size;
}
EXPORT_SYMBOL_GPL(stack_depot_fetch);

/**
 * stack_depot_save - Save a stack trace from an array
 *
 * @entries:		Pointer to storage array
 * @nr_entries:		Size of the storage array
 * @alloc_flags:	Allocation gfp flags
 *
 * Return: The handle of the stack struct stored in depot
 */
depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries,
				      gfp_t alloc_flags)
{
	struct stack_record *found = NULL, **bucket;
	depot_stack_handle_t retval = 0;
	struct page *page = NULL;
	void *prealloc = NULL;
	unsigned long flags;
	u32 hash;

	if (unlikely(nr_entries == 0))
		goto fast_exit;

	hash = hash_stack(entries, nr_entries);
	bucket = &stack_table[hash & STACK_HASH_MASK];

	/*
	 * Fast path: look the stack trace up without locking.
	 * The smp_load_acquire() here pairs with smp_store_release() to
	 * |bucket| below.
	 */
	found = find_stack(smp_load_acquire(bucket), entries,
			   nr_entries, hash);
	if (found)
		goto exit;

	/*
	 * Check if the current or the next stack slab need to be initialized.
	 * If so, allocate the memory - we won't be able to do that under the
	 * lock.
	 *
	 * The smp_load_acquire() here pairs with smp_store_release() to
	 * |next_slab_inited| in depot_alloc_stack() and init_stack_slab().
	 */
	if (unlikely(!smp_load_acquire(&next_slab_inited))) {
		/*
		 * Zero out zone modifiers, as we don't have specific zone
		 * requirements. Keep the flags related to allocation in atomic
		 * contexts and I/O.
		 */
		alloc_flags &= ~GFP_ZONEMASK;
		alloc_flags &= (GFP_ATOMIC | GFP_KERNEL);
		alloc_flags |= __GFP_NOWARN;
		page = alloc_pages(alloc_flags, STACK_ALLOC_ORDER);
		if (page)
			prealloc = page_address(page);
	}

	spin_lock_irqsave(&depot_lock, flags);

	found = find_stack(*bucket, entries, nr_entries, hash);
	if (!found) {
		struct stack_record *new =
			depot_alloc_stack(entries, nr_entries,
					  hash, &prealloc, alloc_flags);
		if (new) {
			new->next = *bucket;
			/*
			 * This smp_store_release() pairs with
			 * smp_load_acquire() from |bucket| above.
			 */
			smp_store_release(bucket, new);
			found = new;
		}
	} else if (prealloc) {
		/*
		 * We didn't need to store this stack trace, but let's keep
		 * the preallocated memory for the future.
		 */
		WARN_ON(!init_stack_slab(&prealloc));
	}

	spin_unlock_irqrestore(&depot_lock, flags);
exit:
	if (prealloc) {
		/* Nobody used this memory, ok to free it. */
		free_pages((unsigned long)prealloc, STACK_ALLOC_ORDER);
	}
	if (found)
		retval = found->handle.handle;
fast_exit:
	return retval;
}
EXPORT_SYMBOL_GPL(stack_depot_save);
