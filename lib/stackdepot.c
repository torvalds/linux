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
#include <linux/kmsan.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/memblock.h>
#include <linux/kasan-enabled.h>

#define DEPOT_HANDLE_BITS (sizeof(depot_stack_handle_t) * 8)

#define DEPOT_POOL_ORDER 2 /* Pool size order, 4 pages */
#define DEPOT_POOL_SIZE (1LL << (PAGE_SHIFT + DEPOT_POOL_ORDER))
#define DEPOT_STACK_ALIGN 4
#define DEPOT_OFFSET_BITS (DEPOT_POOL_ORDER + PAGE_SHIFT - DEPOT_STACK_ALIGN)
#define DEPOT_POOL_INDEX_BITS (DEPOT_HANDLE_BITS - DEPOT_OFFSET_BITS - \
			       STACK_DEPOT_EXTRA_BITS)
#if IS_ENABLED(CONFIG_KMSAN) && CONFIG_STACKDEPOT_MAX_FRAMES >= 32
/*
 * KMSAN is frequently used in fuzzing scenarios and thus saves a lot of stack
 * traces. As KMSAN does not support evicting stack traces from the stack
 * depot, the stack depot capacity might be reached quickly with large stack
 * records. Adjust the maximum number of stack depot pools for this case.
 */
#define DEPOT_POOLS_CAP (8192 * (CONFIG_STACKDEPOT_MAX_FRAMES / 16))
#else
#define DEPOT_POOLS_CAP 8192
#endif
#define DEPOT_MAX_POOLS \
	(((1LL << (DEPOT_POOL_INDEX_BITS)) < DEPOT_POOLS_CAP) ? \
	 (1LL << (DEPOT_POOL_INDEX_BITS)) : DEPOT_POOLS_CAP)

/* Compact structure that stores a reference to a stack. */
union handle_parts {
	depot_stack_handle_t handle;
	struct {
		u32 pool_index	: DEPOT_POOL_INDEX_BITS;
		u32 offset	: DEPOT_OFFSET_BITS;
		u32 extra	: STACK_DEPOT_EXTRA_BITS;
	};
};

struct stack_record {
	struct list_head list;		/* Links in hash table or freelist */
	u32 hash;			/* Hash in hash table */
	u32 size;			/* Number of stored frames */
	union handle_parts handle;
	refcount_t count;
	unsigned long entries[CONFIG_STACKDEPOT_MAX_FRAMES];	/* Frames */
};

#define DEPOT_STACK_RECORD_SIZE \
	ALIGN(sizeof(struct stack_record), 1 << DEPOT_STACK_ALIGN)

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

/* Hash table of stored stack records. */
static struct list_head *stack_table;
/* Fixed order of the number of table buckets. Used when KASAN is enabled. */
static unsigned int stack_bucket_number_order;
/* Hash mask for indexing the table. */
static unsigned int stack_hash_mask;

/* Array of memory regions that store stack records. */
static void *stack_pools[DEPOT_MAX_POOLS];
/* Newly allocated pool that is not yet added to stack_pools. */
static void *new_pool;
/* Number of pools in stack_pools. */
static int pools_num;
/* Freelist of stack records within stack_pools. */
static LIST_HEAD(free_stacks);
/*
 * Stack depot tries to keep an extra pool allocated even before it runs out
 * of space in the currently used pool. This flag marks whether this extra pool
 * needs to be allocated. It has the value 0 when either an extra pool is not
 * yet allocated or if the limit on the number of pools is reached.
 */
static bool new_pool_required = true;
/* Lock that protects the variables above. */
static DEFINE_RWLOCK(pool_rwlock);

static int __init disable_stack_depot(char *str)
{
	return kstrtobool(str, &stack_depot_disabled);
}
early_param("stack_depot_disable", disable_stack_depot);

void __init stack_depot_request_early_init(void)
{
	/* Too late to request early init now. */
	WARN_ON(__stack_depot_early_init_passed);

	__stack_depot_early_init_requested = true;
}

/* Initialize list_head's within the hash table. */
static void init_stack_table(unsigned long entries)
{
	unsigned long i;

	for (i = 0; i < entries; i++)
		INIT_LIST_HEAD(&stack_table[i]);
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
	 * Print disabled message even if early init has not been requested:
	 * stack_depot_init() will not print one.
	 */
	if (stack_depot_disabled) {
		pr_info("disabled\n");
		return 0;
	}

	/*
	 * If KASAN is enabled, use the maximum order: KASAN is frequently used
	 * in fuzzing scenarios, which leads to a large number of different
	 * stack traces being stored in stack depot.
	 */
	if (kasan_enabled() && !stack_bucket_number_order)
		stack_bucket_number_order = STACK_BUCKET_NUMBER_ORDER_MAX;

	/*
	 * Check if early init has been requested after setting
	 * stack_bucket_number_order: stack_depot_init() uses its value.
	 */
	if (!__stack_depot_early_init_requested)
		return 0;

	/*
	 * If stack_bucket_number_order is not set, leave entries as 0 to rely
	 * on the automatic calculations performed by alloc_large_system_hash().
	 */
	if (stack_bucket_number_order)
		entries = 1UL << stack_bucket_number_order;
	pr_info("allocating hash table via alloc_large_system_hash\n");
	stack_table = alloc_large_system_hash("stackdepot",
						sizeof(struct list_head),
						entries,
						STACK_HASH_TABLE_SCALE,
						HASH_EARLY,
						NULL,
						&stack_hash_mask,
						1UL << STACK_BUCKET_NUMBER_ORDER_MIN,
						1UL << STACK_BUCKET_NUMBER_ORDER_MAX);
	if (!stack_table) {
		pr_err("hash table allocation failed, disabling\n");
		stack_depot_disabled = true;
		return -ENOMEM;
	}
	if (!entries) {
		/*
		 * Obtain the number of entries that was calculated by
		 * alloc_large_system_hash().
		 */
		entries = stack_hash_mask + 1;
	}
	init_stack_table(entries);

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
	stack_table = kvcalloc(entries, sizeof(struct list_head), GFP_KERNEL);
	if (!stack_table) {
		pr_err("hash table allocation failed, disabling\n");
		stack_depot_disabled = true;
		ret = -ENOMEM;
		goto out_unlock;
	}
	stack_hash_mask = entries - 1;
	init_stack_table(entries);

out_unlock:
	mutex_unlock(&stack_depot_init_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(stack_depot_init);

/* Initializes a stack depol pool. */
static void depot_init_pool(void *pool)
{
	int offset;

	lockdep_assert_held_write(&pool_rwlock);

	WARN_ON(!list_empty(&free_stacks));

	/* Initialize handles and link stack records into the freelist. */
	for (offset = 0; offset <= DEPOT_POOL_SIZE - DEPOT_STACK_RECORD_SIZE;
	     offset += DEPOT_STACK_RECORD_SIZE) {
		struct stack_record *stack = pool + offset;

		stack->handle.pool_index = pools_num;
		stack->handle.offset = offset >> DEPOT_STACK_ALIGN;
		stack->handle.extra = 0;

		list_add(&stack->list, &free_stacks);
	}

	/* Save reference to the pool to be used by depot_fetch_stack(). */
	stack_pools[pools_num] = pool;
	pools_num++;
}

/* Keeps the preallocated memory to be used for a new stack depot pool. */
static void depot_keep_new_pool(void **prealloc)
{
	lockdep_assert_held_write(&pool_rwlock);

	/*
	 * If a new pool is already saved or the maximum number of
	 * pools is reached, do not use the preallocated memory.
	 */
	if (!new_pool_required)
		return;

	/*
	 * Use the preallocated memory for the new pool
	 * as long as we do not exceed the maximum number of pools.
	 */
	if (pools_num < DEPOT_MAX_POOLS) {
		new_pool = *prealloc;
		*prealloc = NULL;
	}

	/*
	 * At this point, either a new pool is kept or the maximum
	 * number of pools is reached. In either case, take note that
	 * keeping another pool is not required.
	 */
	new_pool_required = false;
}

/* Updates references to the current and the next stack depot pools. */
static bool depot_update_pools(void **prealloc)
{
	lockdep_assert_held_write(&pool_rwlock);

	/* Check if we still have objects in the freelist. */
	if (!list_empty(&free_stacks))
		goto out_keep_prealloc;

	/* Check if we have a new pool saved and use it. */
	if (new_pool) {
		depot_init_pool(new_pool);
		new_pool = NULL;

		/* Take note that we might need a new new_pool. */
		if (pools_num < DEPOT_MAX_POOLS)
			new_pool_required = true;

		/* Try keeping the preallocated memory for new_pool. */
		goto out_keep_prealloc;
	}

	/* Bail out if we reached the pool limit. */
	if (unlikely(pools_num >= DEPOT_MAX_POOLS)) {
		WARN_ONCE(1, "Stack depot reached limit capacity");
		return false;
	}

	/* Check if we have preallocated memory and use it. */
	if (*prealloc) {
		depot_init_pool(*prealloc);
		*prealloc = NULL;
		return true;
	}

	return false;

out_keep_prealloc:
	/* Keep the preallocated memory for a new pool if required. */
	if (*prealloc)
		depot_keep_new_pool(prealloc);
	return true;
}

/* Allocates a new stack in a stack depot pool. */
static struct stack_record *
depot_alloc_stack(unsigned long *entries, int size, u32 hash, void **prealloc)
{
	struct stack_record *stack;

	lockdep_assert_held_write(&pool_rwlock);

	/* Update current and new pools if required and possible. */
	if (!depot_update_pools(prealloc))
		return NULL;

	/* Check if we have a stack record to save the stack trace. */
	if (list_empty(&free_stacks))
		return NULL;

	/* Get and unlink the first entry from the freelist. */
	stack = list_first_entry(&free_stacks, struct stack_record, list);
	list_del(&stack->list);

	/* Limit number of saved frames to CONFIG_STACKDEPOT_MAX_FRAMES. */
	if (size > CONFIG_STACKDEPOT_MAX_FRAMES)
		size = CONFIG_STACKDEPOT_MAX_FRAMES;

	/* Save the stack trace. */
	stack->hash = hash;
	stack->size = size;
	/* stack->handle is already filled in by depot_init_pool(). */
	refcount_set(&stack->count, 1);
	memcpy(stack->entries, entries, flex_array_size(stack, entries, size));

	/*
	 * Let KMSAN know the stored stack record is initialized. This shall
	 * prevent false positive reports if instrumented code accesses it.
	 */
	kmsan_unpoison_memory(stack, DEPOT_STACK_RECORD_SIZE);

	return stack;
}

static struct stack_record *depot_fetch_stack(depot_stack_handle_t handle)
{
	union handle_parts parts = { .handle = handle };
	void *pool;
	size_t offset = parts.offset << DEPOT_STACK_ALIGN;
	struct stack_record *stack;

	lockdep_assert_held(&pool_rwlock);

	if (parts.pool_index > pools_num) {
		WARN(1, "pool index %d out of bounds (%d) for stack id %08x\n",
		     parts.pool_index, pools_num, handle);
		return NULL;
	}

	pool = stack_pools[parts.pool_index];
	if (!pool)
		return NULL;

	stack = pool + offset;
	return stack;
}

/* Links stack into the freelist. */
static void depot_free_stack(struct stack_record *stack)
{
	lockdep_assert_held_write(&pool_rwlock);

	list_add(&stack->list, &free_stacks);
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
static inline struct stack_record *find_stack(struct list_head *bucket,
					     unsigned long *entries, int size,
					     u32 hash)
{
	struct list_head *pos;
	struct stack_record *found;

	lockdep_assert_held(&pool_rwlock);

	list_for_each(pos, bucket) {
		found = list_entry(pos, struct stack_record, list);
		if (found->hash == hash &&
		    found->size == size &&
		    !stackdepot_memcmp(entries, found->entries, size))
			return found;
	}
	return NULL;
}

depot_stack_handle_t stack_depot_save_flags(unsigned long *entries,
					    unsigned int nr_entries,
					    gfp_t alloc_flags,
					    depot_flags_t depot_flags)
{
	struct list_head *bucket;
	struct stack_record *found = NULL;
	depot_stack_handle_t handle = 0;
	struct page *page = NULL;
	void *prealloc = NULL;
	bool can_alloc = depot_flags & STACK_DEPOT_FLAG_CAN_ALLOC;
	bool need_alloc = false;
	unsigned long flags;
	u32 hash;

	if (WARN_ON(depot_flags & ~STACK_DEPOT_FLAGS_MASK))
		return 0;

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
		return 0;

	hash = hash_stack(entries, nr_entries);
	bucket = &stack_table[hash & stack_hash_mask];

	read_lock_irqsave(&pool_rwlock, flags);
	printk_deferred_enter();

	/* Fast path: look the stack trace up without full locking. */
	found = find_stack(bucket, entries, nr_entries, hash);
	if (found) {
		if (depot_flags & STACK_DEPOT_FLAG_GET)
			refcount_inc(&found->count);
		printk_deferred_exit();
		read_unlock_irqrestore(&pool_rwlock, flags);
		goto exit;
	}

	/* Take note if another stack pool needs to be allocated. */
	if (new_pool_required)
		need_alloc = true;

	printk_deferred_exit();
	read_unlock_irqrestore(&pool_rwlock, flags);

	/*
	 * Allocate memory for a new pool if required now:
	 * we won't be able to do that under the lock.
	 */
	if (unlikely(can_alloc && need_alloc)) {
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

	write_lock_irqsave(&pool_rwlock, flags);
	printk_deferred_enter();

	found = find_stack(bucket, entries, nr_entries, hash);
	if (!found) {
		struct stack_record *new =
			depot_alloc_stack(entries, nr_entries, hash, &prealloc);

		if (new) {
			list_add(&new->list, bucket);
			found = new;
		}
	} else {
		if (depot_flags & STACK_DEPOT_FLAG_GET)
			refcount_inc(&found->count);
		/*
		 * Stack depot already contains this stack trace, but let's
		 * keep the preallocated memory for future.
		 */
		if (prealloc)
			depot_keep_new_pool(&prealloc);
	}

	printk_deferred_exit();
	write_unlock_irqrestore(&pool_rwlock, flags);
exit:
	if (prealloc) {
		/* Stack depot didn't use this memory, free it. */
		free_pages((unsigned long)prealloc, DEPOT_POOL_ORDER);
	}
	if (found)
		handle = found->handle.handle;
	return handle;
}
EXPORT_SYMBOL_GPL(stack_depot_save_flags);

depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries,
				      gfp_t alloc_flags)
{
	return stack_depot_save_flags(entries, nr_entries, alloc_flags,
				      STACK_DEPOT_FLAG_CAN_ALLOC);
}
EXPORT_SYMBOL_GPL(stack_depot_save);

unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries)
{
	struct stack_record *stack;
	unsigned long flags;

	*entries = NULL;
	/*
	 * Let KMSAN know *entries is initialized. This shall prevent false
	 * positive reports if instrumented code accesses it.
	 */
	kmsan_unpoison_memory(entries, sizeof(*entries));

	if (!handle || stack_depot_disabled)
		return 0;

	read_lock_irqsave(&pool_rwlock, flags);
	printk_deferred_enter();

	stack = depot_fetch_stack(handle);

	printk_deferred_exit();
	read_unlock_irqrestore(&pool_rwlock, flags);

	*entries = stack->entries;
	return stack->size;
}
EXPORT_SYMBOL_GPL(stack_depot_fetch);

void stack_depot_put(depot_stack_handle_t handle)
{
	struct stack_record *stack;
	unsigned long flags;

	if (!handle || stack_depot_disabled)
		return;

	write_lock_irqsave(&pool_rwlock, flags);
	printk_deferred_enter();

	stack = depot_fetch_stack(handle);
	if (WARN_ON(!stack))
		goto out;

	if (refcount_dec_and_test(&stack->count)) {
		/* Unlink stack from the hash table. */
		list_del(&stack->list);

		/* Free stack. */
		depot_free_stack(stack);
	}

out:
	printk_deferred_exit();
	write_unlock_irqrestore(&pool_rwlock, flags);
}
EXPORT_SYMBOL_GPL(stack_depot_put);

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
