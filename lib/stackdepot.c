// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack depot - a stack trace storage that avoids duplication.
 *
 * Internally, stack depot maintains a hash table of unique stacktraces. The
 * stack traces themselves are stored contiguously one after another in a set
 * of separate page allocations.
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on the code by Dmitry Chernenkov.
 */

#define pr_fmt(fmt) "stackdepot: " fmt

#include <linux/gfp.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <linux/kasan-enabled.h>

#define DEPOT_HANDLE_BITS (sizeof(depot_stack_handle_t) * 8)

#define DEPOT_VALID_BITS 1
#define DEPOT_POOL_ORDER 2 /* Pool size order, 4 pages */
#define DEPOT_POOL_SIZE (1LL << (PAGE_SHIFT + DEPOT_POOL_ORDER))
#define DEPOT_STACK_ALIGN 4
#define DEPOT_OFFSET_BITS (DEPOT_POOL_ORDER + PAGE_SHIFT - DEPOT_STACK_ALIGN)
#define DEPOT_POOL_INDEX_BITS (DEPOT_HANDLE_BITS - DEPOT_VALID_BITS - \
			       DEPOT_OFFSET_BITS - STACK_DEPOT_EXTRA_BITS)
#define DEPOT_POOLS_CAP 8192
#define DEPOT_MAX_POOLS \
	(((1LL << (DEPOT_POOL_INDEX_BITS)) < DEPOT_POOLS_CAP) ? \
	 (1LL << (DEPOT_POOL_INDEX_BITS)) : DEPOT_POOLS_CAP)

/* Compact structure that stores a reference to a stack. */
union handle_parts {
	depot_stack_handle_t handle;
	struct {
		u32 pool_index	: DEPOT_POOL_INDEX_BITS;
		u32 offset	: DEPOT_OFFSET_BITS;
		u32 valid	: DEPOT_VALID_BITS;
		u32 extra	: STACK_DEPOT_EXTRA_BITS;
	};
};

struct stack_record {
	struct stack_record *next;	/* Link in the hash table */
	u32 hash;			/* Hash in the hash table */
	u32 size;			/* Number of stored frames */
	union handle_parts handle;
	unsigned long entries[];	/* Variable-sized array of frames */
};

static bool stack_depot_disabled;
static bool __stack_depot_early_init_requested __initdata = IS_ENABLED(CONFIG_STACKDEPOT_ALWAYS_INIT);
static bool __stack_depot_early_init_passed __initdata;

/* Use one hash table bucket per 16 KB of memory. */
#define STACK_HASH_TABLE_SCALE 14
/* Limit the number of buckets between 4K and 1M. */
#define STACK_BUCKET_NUMBER_ORDER_MIN 12
#define STACK_BUCKET_NUMBER_ORDER_MAX 20
/* Initial seed for jhash2. */
#define STACK_HASH_SEED 0x9747b28c

/* Hash table of pointers to stored stack traces. */
static struct stack_record **stack_table;
/* Fixed order of the number of table buckets. Used when KASAN is enabled. */
static unsigned int stack_bucket_number_order;
/* Hash mask for indexing the table. */
static unsigned int stack_hash_mask;

/* Array of memory regions that store stack traces. */
static void *stack_pools[DEPOT_MAX_POOLS];
/* Currently used pool in stack_pools. */
static int pool_index;
/* Offset to the unused space in the currently used pool. */
static size_t pool_offset;
/* Lock that protects the variables above. */
static DEFINE_RAW_SPINLOCK(pool_lock);
/*
 * Stack depot tries to keep an extra pool allocated even before it runs out
 * of space in the currently used pool.
 * This flag marks that this next extra pool needs to be allocated and
 * initialized. It has the value 0 when either the next pool is not yet
 * initialized or the limit on the number of pools is reached.
 */
static int next_pool_required = 1;

static int __init disable_stack_depot(char *str)
{
	int ret;

	ret = kstrtobool(str, &stack_depot_disabled);
	if (!ret && stack_depot_disabled) {
		pr_info("disabled\n");
		stack_table = NULL;
	}
	return 0;
}
early_param("stack_depot_disable", disable_stack_depot);

void __init stack_depot_request_early_init(void)
{
	/* Too late to request early init now. */
	WARN_ON(__stack_depot_early_init_passed);

	__stack_depot_early_init_requested = true;
}

/* Allocates a hash table via memblock. Can only be used during early boot. */
int __init stack_depot_early_init(void)
{
	unsigned long entries = 0;

	/* This function must be called only once, from mm_init(). */
	if (WARN_ON(__stack_depot_early_init_passed))
		return 0;
	__stack_depot_early_init_passed = true;

	/*
	 * If KASAN is enabled, use the maximum order: KASAN is frequently used
	 * in fuzzing scenarios, which leads to a large number of different
	 * stack traces being stored in stack depot.
	 */
	if (kasan_enabled() && !stack_bucket_number_order)
		stack_bucket_number_order = STACK_BUCKET_NUMBER_ORDER_MAX;

	if (!__stack_depot_early_init_requested || stack_depot_disabled)
		return 0;

	/*
	 * If stack_bucket_number_order is not set, leave entries as 0 to rely
	 * on the automatic calculations performed by alloc_large_system_hash.
	 */
	if (stack_bucket_number_order)
		entries = 1UL << stack_bucket_number_order;
	pr_info("allocating hash table via alloc_large_system_hash\n");
	stack_table = alloc_large_system_hash("stackdepot",
						sizeof(struct stack_record *),
						entries,
						STACK_HASH_TABLE_SCALE,
						HASH_EARLY | HASH_ZERO,
						NULL,
						&stack_hash_mask,
						1UL << STACK_BUCKET_NUMBER_ORDER_MIN,
						1UL << STACK_BUCKET_NUMBER_ORDER_MAX);
	if (!stack_table) {
		pr_err("hash table allocation failed, disabling\n");
		stack_depot_disabled = true;
		return -ENOMEM;
	}

	return 0;
}

/* Allocates a hash table via kvcalloc. Can be used after boot. */
int stack_depot_init(void)
{
	static DEFINE_MUTEX(stack_depot_init_mutex);
	unsigned long entries;
	int ret = 0;

	mutex_lock(&stack_depot_init_mutex);

	if (stack_depot_disabled || stack_table)
		goto out_unlock;

	/*
	 * Similarly to stack_depot_early_init, use stack_bucket_number_order
	 * if assigned, and rely on automatic scaling otherwise.
	 */
	if (stack_bucket_number_order) {
		entries = 1UL << stack_bucket_number_order;
	} else {
		int scale = STACK_HASH_TABLE_SCALE;

		entries = nr_free_buffer_pages();
		entries = roundup_pow_of_two(entries);

		if (scale > PAGE_SHIFT)
			entries >>= (scale - PAGE_SHIFT);
		else
			entries <<= (PAGE_SHIFT - scale);
	}

	if (entries < 1UL << STACK_BUCKET_NUMBER_ORDER_MIN)
		entries = 1UL << STACK_BUCKET_NUMBER_ORDER_MIN;
	if (entries > 1UL << STACK_BUCKET_NUMBER_ORDER_MAX)
		entries = 1UL << STACK_BUCKET_NUMBER_ORDER_MAX;

	pr_info("allocating hash table of %lu entries via kvcalloc\n", entries);
	stack_table = kvcalloc(entries, sizeof(struct stack_record *), GFP_KERNEL);
	if (!stack_table) {
		pr_err("hash table allocation failed, disabling\n");
		stack_depot_disabled = true;
		ret = -ENOMEM;
		goto out_unlock;
	}
	stack_hash_mask = entries - 1;

out_unlock:
	mutex_unlock(&stack_depot_init_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(stack_depot_init);

/* Uses preallocated memory to initialize a new stack depot pool. */
static void depot_init_pool(void **prealloc)
{
	/*
	 * If the next pool is already initialized or the maximum number of
	 * pools is reached, do not use the preallocated memory.
	 * smp_load_acquire() here pairs with smp_store_release() below and
	 * in depot_alloc_stack().
	 */
	if (!smp_load_acquire(&next_pool_required))
		return;

	/* Check if the current pool is not yet allocated. */
	if (stack_pools[pool_index] == NULL) {
		/* Use the preallocated memory for the current pool. */
		stack_pools[pool_index] = *prealloc;
		*prealloc = NULL;
	} else {
		/*
		 * Otherwise, use the preallocated memory for the next pool
		 * as long as we do not exceed the maximum number of pools.
		 */
		if (pool_index + 1 < DEPOT_MAX_POOLS) {
			stack_pools[pool_index + 1] = *prealloc;
			*prealloc = NULL;
		}
		/*
		 * At this point, either the next pool is initialized or the
		 * maximum number of pools is reached. In either case, take
		 * note that initializing another pool is not required.
		 * This smp_store_release pairs with smp_load_acquire() above
		 * and in stack_depot_save().
		 */
		smp_store_release(&next_pool_required, 0);
	}
}

/* Allocates a new stack in a stack depot pool. */
static struct stack_record *
depot_alloc_stack(unsigned long *entries, int size, u32 hash, void **prealloc)
{
	struct stack_record *stack;
	size_t required_size = struct_size(stack, entries, size);

	required_size = ALIGN(required_size, 1 << DEPOT_STACK_ALIGN);

	/* Check if there is not enough space in the current pool. */
	if (unlikely(pool_offset + required_size > DEPOT_POOL_SIZE)) {
		/* Bail out if we reached the pool limit. */
		if (unlikely(pool_index + 1 >= DEPOT_MAX_POOLS)) {
			WARN_ONCE(1, "Stack depot reached limit capacity");
			return NULL;
		}

		/*
		 * Move on to the next pool.
		 * WRITE_ONCE pairs with potential concurrent read in
		 * stack_depot_fetch().
		 */
		WRITE_ONCE(pool_index, pool_index + 1);
		pool_offset = 0;
		/*
		 * If the maximum number of pools is not reached, take note
		 * that the next pool needs to initialized.
		 * smp_store_release() here pairs with smp_load_acquire() in
		 * stack_depot_save() and depot_init_pool().
		 */
		if (pool_index + 1 < DEPOT_MAX_POOLS)
			smp_store_release(&next_pool_required, 1);
	}

	/* Assign the preallocated memory to a pool if required. */
	if (*prealloc)
		depot_init_pool(prealloc);

	/* Check if we have a pool to save the stack trace. */
	if (stack_pools[pool_index] == NULL)
		return NULL;

	/* Save the stack trace. */
	stack = stack_pools[pool_index] + pool_offset;
	stack->hash = hash;
	stack->size = size;
	stack->handle.pool_index = pool_index;
	stack->handle.offset = pool_offset >> DEPOT_STACK_ALIGN;
	stack->handle.valid = 1;
	stack->handle.extra = 0;
	memcpy(stack->entries, entries, flex_array_size(stack, entries, size));
	pool_offset += required_size;

	return stack;
}

/* Calculates the hash for a stack. */
static inline u32 hash_stack(unsigned long *entries, unsigned int size)
{
	return jhash2((u32 *)entries,
		      array_size(size,  sizeof(*entries)) / sizeof(u32),
		      STACK_HASH_SEED);
}

/*
 * Non-instrumented version of memcmp().
 * Does not check the lexicographical order, only the equality.
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

/* Finds a stack in a bucket of the hash table. */
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

depot_stack_handle_t __stack_depot_save(unsigned long *entries,
					unsigned int nr_entries,
					gfp_t alloc_flags, bool can_alloc)
{
	struct stack_record *found = NULL, **bucket;
	union handle_parts retval = { .handle = 0 };
	struct page *page = NULL;
	void *prealloc = NULL;
	unsigned long flags;
	u32 hash;

	/*
	 * If this stack trace is from an interrupt, including anything before
	 * interrupt entry usually leads to unbounded stack depot growth.
	 *
	 * Since use of filter_irq_stacks() is a requirement to ensure stack
	 * depot can efficiently deduplicate interrupt stacks, always
	 * filter_irq_stacks() to simplify all callers' use of stack depot.
	 */
	nr_entries = filter_irq_stacks(entries, nr_entries);

	if (unlikely(nr_entries == 0) || stack_depot_disabled)
		goto fast_exit;

	hash = hash_stack(entries, nr_entries);
	bucket = &stack_table[hash & stack_hash_mask];

	/*
	 * Fast path: look the stack trace up without locking.
	 * The smp_load_acquire() here pairs with smp_store_release() to
	 * |bucket| below.
	 */
	found = find_stack(smp_load_acquire(bucket), entries, nr_entries, hash);
	if (found)
		goto exit;

	/*
	 * Check if another stack pool needs to be initialized. If so, allocate
	 * the memory now - we won't be able to do that under the lock.
	 *
	 * The smp_load_acquire() here pairs with smp_store_release() to
	 * |next_pool_inited| in depot_alloc_stack() and depot_init_pool().
	 */
	if (unlikely(can_alloc && smp_load_acquire(&next_pool_required))) {
		/*
		 * Zero out zone modifiers, as we don't have specific zone
		 * requirements. Keep the flags related to allocation in atomic
		 * contexts and I/O.
		 */
		alloc_flags &= ~GFP_ZONEMASK;
		alloc_flags &= (GFP_ATOMIC | GFP_KERNEL);
		alloc_flags |= __GFP_NOWARN;
		page = alloc_pages(alloc_flags, DEPOT_POOL_ORDER);
		if (page)
			prealloc = page_address(page);
	}

	raw_spin_lock_irqsave(&pool_lock, flags);

	found = find_stack(*bucket, entries, nr_entries, hash);
	if (!found) {
		struct stack_record *new =
			depot_alloc_stack(entries, nr_entries, hash, &prealloc);

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
		 * Stack depot already contains this stack trace, but let's
		 * keep the preallocated memory for the future.
		 */
		depot_init_pool(&prealloc);
	}

	raw_spin_unlock_irqrestore(&pool_lock, flags);
exit:
	if (prealloc) {
		/* Stack depot didn't use this memory, free it. */
		free_pages((unsigned long)prealloc, DEPOT_POOL_ORDER);
	}
	if (found)
		retval.handle = found->handle.handle;
fast_exit:
	return retval.handle;
}
EXPORT_SYMBOL_GPL(__stack_depot_save);

depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries,
				      gfp_t alloc_flags)
{
	return __stack_depot_save(entries, nr_entries, alloc_flags, true);
}
EXPORT_SYMBOL_GPL(stack_depot_save);

unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries)
{
	union handle_parts parts = { .handle = handle };
	/*
	 * READ_ONCE pairs with potential concurrent write in
	 * depot_alloc_stack.
	 */
	int pool_index_cached = READ_ONCE(pool_index);
	void *pool;
	size_t offset = parts.offset << DEPOT_STACK_ALIGN;
	struct stack_record *stack;

	*entries = NULL;
	if (!handle)
		return 0;

	if (parts.pool_index > pool_index_cached) {
		WARN(1, "pool index %d out of bounds (%d) for stack id %08x\n",
			parts.pool_index, pool_index_cached, handle);
		return 0;
	}
	pool = stack_pools[parts.pool_index];
	if (!pool)
		return 0;
	stack = pool + offset;

	*entries = stack->entries;
	return stack->size;
}
EXPORT_SYMBOL_GPL(stack_depot_fetch);

void stack_depot_print(depot_stack_handle_t stack)
{
	unsigned long *entries;
	unsigned int nr_entries;

	nr_entries = stack_depot_fetch(stack, &entries);
	if (nr_entries > 0)
		stack_trace_print(entries, nr_entries, 0);
}
EXPORT_SYMBOL_GPL(stack_depot_print);

int stack_depot_snprint(depot_stack_handle_t handle, char *buf, size_t size,
		       int spaces)
{
	unsigned long *entries;
	unsigned int nr_entries;

	nr_entries = stack_depot_fetch(handle, &entries);
	return nr_entries ? stack_trace_snprint(buf, size, entries, nr_entries,
						spaces) : 0;
}
EXPORT_SYMBOL_GPL(stack_depot_snprint);

depot_stack_handle_t __must_check stack_depot_set_extra_bits(
			depot_stack_handle_t handle, unsigned int extra_bits)
{
	union handle_parts parts = { .handle = handle };

	/* Don't set extra bits on empty handles. */
	if (!handle)
		return 0;

	parts.extra = extra_bits;
	return parts.handle;
}
EXPORT_SYMBOL(stack_depot_set_extra_bits);

unsigned int stack_depot_get_extra_bits(depot_stack_handle_t handle)
{
	union handle_parts parts = { .handle = handle };

	return parts.extra;
}
EXPORT_SYMBOL(stack_depot_get_extra_bits);
