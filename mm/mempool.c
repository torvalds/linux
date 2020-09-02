// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/mm/mempool.c
 *
 *  memory buffer pool support. Such pools are mostly used
 *  for guaranteed, deadlock-free memory allocations during
 *  extreme VM load.
 *
 *  started by Ingo Molnar, Copyright (C) 2001
 *  debugging by David Rientjes, Copyright (C) 2015
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/kasan.h>
#include <linux/kmemleak.h>
#include <linux/export.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include "slab.h"

#if defined(CONFIG_DEBUG_SLAB) || defined(CONFIG_SLUB_DEBUG_ON)
static void poison_error(mempool_t *pool, void *element, size_t size,
			 size_t byte)
{
	const int nr = pool->curr_nr;
	const int start = max_t(int, byte - (BITS_PER_LONG / 8), 0);
	const int end = min_t(int, byte + (BITS_PER_LONG / 8), size);
	int i;

	pr_err("BUG: mempool element poison mismatch\n");
	pr_err("Mempool %p size %zu\n", pool, size);
	pr_err(" nr=%d @ %p: %s0x", nr, element, start > 0 ? "... " : "");
	for (i = start; i < end; i++)
		pr_cont("%x ", *(u8 *)(element + i));
	pr_cont("%s\n", end < size ? "..." : "");
	dump_stack();
}

static void __check_element(mempool_t *pool, void *element, size_t size)
{
	u8 *obj = element;
	size_t i;

	for (i = 0; i < size; i++) {
		u8 exp = (i < size - 1) ? POISON_FREE : POISON_END;

		if (obj[i] != exp) {
			poison_error(pool, element, size, i);
			return;
		}
	}
	memset(obj, POISON_INUSE, size);
}

static void check_element(mempool_t *pool, void *element)
{
	/* Mempools backed by slab allocator */
	if (pool->free == mempool_free_slab || pool->free == mempool_kfree)
		__check_element(pool, element, ksize(element));

	/* Mempools backed by page allocator */
	if (pool->free == mempool_free_pages) {
		int order = (int)(long)pool->pool_data;
		void *addr = kmap_atomic((struct page *)element);

		__check_element(pool, addr, 1UL << (PAGE_SHIFT + order));
		kunmap_atomic(addr);
	}
}

static void __poison_element(void *element, size_t size)
{
	u8 *obj = element;

	memset(obj, POISON_FREE, size - 1);
	obj[size - 1] = POISON_END;
}

static void poison_element(mempool_t *pool, void *element)
{
	/* Mempools backed by slab allocator */
	if (pool->alloc == mempool_alloc_slab || pool->alloc == mempool_kmalloc)
		__poison_element(element, ksize(element));

	/* Mempools backed by page allocator */
	if (pool->alloc == mempool_alloc_pages) {
		int order = (int)(long)pool->pool_data;
		void *addr = kmap_atomic((struct page *)element);

		__poison_element(addr, 1UL << (PAGE_SHIFT + order));
		kunmap_atomic(addr);
	}
}
#else /* CONFIG_DEBUG_SLAB || CONFIG_SLUB_DEBUG_ON */
static inline void check_element(mempool_t *pool, void *element)
{
}
static inline void poison_element(mempool_t *pool, void *element)
{
}
#endif /* CONFIG_DEBUG_SLAB || CONFIG_SLUB_DEBUG_ON */

static __always_inline void kasan_poison_element(mempool_t *pool, void *element)
{
	if (pool->alloc == mempool_alloc_slab || pool->alloc == mempool_kmalloc)
		kasan_poison_kfree(element, _RET_IP_);
	if (pool->alloc == mempool_alloc_pages)
		kasan_free_pages(element, (unsigned long)pool->pool_data);
}

static void kasan_unpoison_element(mempool_t *pool, void *element)
{
	if (pool->alloc == mempool_alloc_slab || pool->alloc == mempool_kmalloc)
		kasan_unpoison_slab(element);
	if (pool->alloc == mempool_alloc_pages)
		kasan_alloc_pages(element, (unsigned long)pool->pool_data);
}

static __always_inline void add_element(mempool_t *pool, void *element)
{
	BUG_ON(pool->curr_nr >= pool->min_nr);
	poison_element(pool, element);
	kasan_poison_element(pool, element);
	pool->elements[pool->curr_nr++] = element;
}

static void *remove_element(mempool_t *pool)
{
	void *element = pool->elements[--pool->curr_nr];

	BUG_ON(pool->curr_nr < 0);
	kasan_unpoison_element(pool, element);
	check_element(pool, element);
	return element;
}

/**
 * mempool_exit - exit a mempool initialized with mempool_init()
 * @pool:      pointer to the memory pool which was initialized with
 *             mempool_init().
 *
 * Free all reserved elements in @pool and @pool itself.  This function
 * only sleeps if the free_fn() function sleeps.
 *
 * May be called on a zeroed but uninitialized mempool (i.e. allocated with
 * kzalloc()).
 */
void mempool_exit(mempool_t *pool)
{
	while (pool->curr_nr) {
		void *element = remove_element(pool);
		pool->free(element, pool->pool_data);
	}
	kfree(pool->elements);
	pool->elements = NULL;
}
EXPORT_SYMBOL(mempool_exit);

/**
 * mempool_destroy - deallocate a memory pool
 * @pool:      pointer to the memory pool which was allocated via
 *             mempool_create().
 *
 * Free all reserved elements in @pool and @pool itself.  This function
 * only sleeps if the free_fn() function sleeps.
 */
void mempool_destroy(mempool_t *pool)
{
	if (unlikely(!pool))
		return;

	mempool_exit(pool);
	kfree(pool);
}
EXPORT_SYMBOL(mempool_destroy);

int mempool_init_node(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn,
		      mempool_free_t *free_fn, void *pool_data,
		      gfp_t gfp_mask, int node_id)
{
	spin_lock_init(&pool->lock);
	pool->min_nr	= min_nr;
	pool->pool_data = pool_data;
	pool->alloc	= alloc_fn;
	pool->free	= free_fn;
	init_waitqueue_head(&pool->wait);

	pool->elements = kmalloc_array_node(min_nr, sizeof(void *),
					    gfp_mask, node_id);
	if (!pool->elements)
		return -ENOMEM;

	/*
	 * First pre-allocate the guaranteed number of buffers.
	 */
	while (pool->curr_nr < pool->min_nr) {
		void *element;

		element = pool->alloc(gfp_mask, pool->pool_data);
		if (unlikely(!element)) {
			mempool_exit(pool);
			return -ENOMEM;
		}
		add_element(pool, element);
	}

	return 0;
}
EXPORT_SYMBOL(mempool_init_node);

/**
 * mempool_init - initialize a memory pool
 * @pool:      pointer to the memory pool that should be initialized
 * @min_nr:    the minimum number of elements guaranteed to be
 *             allocated for this pool.
 * @alloc_fn:  user-defined element-allocation function.
 * @free_fn:   user-defined element-freeing function.
 * @pool_data: optional private data available to the user-defined functions.
 *
 * Like mempool_create(), but initializes the pool in (i.e. embedded in another
 * structure).
 *
 * Return: %0 on success, negative error code otherwise.
 */
int mempool_init(mempool_t *pool, int min_nr, mempool_alloc_t *alloc_fn,
		 mempool_free_t *free_fn, void *pool_data)
{
	return mempool_init_node(pool, min_nr, alloc_fn, free_fn,
				 pool_data, GFP_KERNEL, NUMA_NO_NODE);

}
EXPORT_SYMBOL(mempool_init);

/**
 * mempool_create - create a memory pool
 * @min_nr:    the minimum number of elements guaranteed to be
 *             allocated for this pool.
 * @alloc_fn:  user-defined element-allocation function.
 * @free_fn:   user-defined element-freeing function.
 * @pool_data: optional private data available to the user-defined functions.
 *
 * this function creates and allocates a guaranteed size, preallocated
 * memory pool. The pool can be used from the mempool_alloc() and mempool_free()
 * functions. This function might sleep. Both the alloc_fn() and the free_fn()
 * functions might sleep - as long as the mempool_alloc() function is not called
 * from IRQ contexts.
 *
 * Return: pointer to the created memory pool object or %NULL on error.
 */
mempool_t *mempool_create(int min_nr, mempool_alloc_t *alloc_fn,
				mempool_free_t *free_fn, void *pool_data)
{
	return mempool_create_node(min_nr,alloc_fn,free_fn, pool_data,
				   GFP_KERNEL, NUMA_NO_NODE);
}
EXPORT_SYMBOL(mempool_create);

mempool_t *mempool_create_node(int min_nr, mempool_alloc_t *alloc_fn,
			       mempool_free_t *free_fn, void *pool_data,
			       gfp_t gfp_mask, int node_id)
{
	mempool_t *pool;

	pool = kzalloc_node(sizeof(*pool), gfp_mask, node_id);
	if (!pool)
		return NULL;

	if (mempool_init_node(pool, min_nr, alloc_fn, free_fn, pool_data,
			      gfp_mask, node_id)) {
		kfree(pool);
		return NULL;
	}

	return pool;
}
EXPORT_SYMBOL(mempool_create_node);

/**
 * mempool_resize - resize an existing memory pool
 * @pool:       pointer to the memory pool which was allocated via
 *              mempool_create().
 * @new_min_nr: the new minimum number of elements guaranteed to be
 *              allocated for this pool.
 *
 * This function shrinks/grows the pool. In the case of growing,
 * it cannot be guaranteed that the pool will be grown to the new
 * size immediately, but new mempool_free() calls will refill it.
 * This function may sleep.
 *
 * Note, the caller must guarantee that no mempool_destroy is called
 * while this function is running. mempool_alloc() & mempool_free()
 * might be called (eg. from IRQ contexts) while this function executes.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int mempool_resize(mempool_t *pool, int new_min_nr)
{
	void *element;
	void **new_elements;
	unsigned long flags;

	BUG_ON(new_min_nr <= 0);
	might_sleep();

	spin_lock_irqsave(&pool->lock, flags);
	if (new_min_nr <= pool->min_nr) {
		while (new_min_nr < pool->curr_nr) {
			element = remove_element(pool);
			spin_unlock_irqrestore(&pool->lock, flags);
			pool->free(element, pool->pool_data);
			spin_lock_irqsave(&pool->lock, flags);
		}
		pool->min_nr = new_min_nr;
		goto out_unlock;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	/* Grow the pool */
	new_elements = kmalloc_array(new_min_nr, sizeof(*new_elements),
				     GFP_KERNEL);
	if (!new_elements)
		return -ENOMEM;

	spin_lock_irqsave(&pool->lock, flags);
	if (unlikely(new_min_nr <= pool->min_nr)) {
		/* Raced, other resize will do our work */
		spin_unlock_irqrestore(&pool->lock, flags);
		kfree(new_elements);
		goto out;
	}
	memcpy(new_elements, pool->elements,
			pool->curr_nr * sizeof(*new_elements));
	kfree(pool->elements);
	pool->elements = new_elements;
	pool->min_nr = new_min_nr;

	while (pool->curr_nr < pool->min_nr) {
		spin_unlock_irqrestore(&pool->lock, flags);
		element = pool->alloc(GFP_KERNEL, pool->pool_data);
		if (!element)
			goto out;
		spin_lock_irqsave(&pool->lock, flags);
		if (pool->curr_nr < pool->min_nr) {
			add_element(pool, element);
		} else {
			spin_unlock_irqrestore(&pool->lock, flags);
			pool->free(element, pool->pool_data);	/* Raced */
			goto out;
		}
	}
out_unlock:
	spin_unlock_irqrestore(&pool->lock, flags);
out:
	return 0;
}
EXPORT_SYMBOL(mempool_resize);

/**
 * mempool_alloc - allocate an element from a specific memory pool
 * @pool:      pointer to the memory pool which was allocated via
 *             mempool_create().
 * @gfp_mask:  the usual allocation bitmask.
 *
 * this function only sleeps if the alloc_fn() function sleeps or
 * returns NULL. Note that due to preallocation, this function
 * *never* fails when called from process contexts. (it might
 * fail if called from an IRQ context.)
 * Note: using __GFP_ZERO is not supported.
 *
 * Return: pointer to the allocated element or %NULL on error.
 */
void *mempool_alloc(mempool_t *pool, gfp_t gfp_mask)
{
	void *element;
	unsigned long flags;
	wait_queue_entry_t wait;
	gfp_t gfp_temp;

	VM_WARN_ON_ONCE(gfp_mask & __GFP_ZERO);
	might_sleep_if(gfp_mask & __GFP_DIRECT_RECLAIM);

	gfp_mask |= __GFP_NOMEMALLOC;	/* don't allocate emergency reserves */
	gfp_mask |= __GFP_NORETRY;	/* don't loop in __alloc_pages */
	gfp_mask |= __GFP_NOWARN;	/* failures are OK */

	gfp_temp = gfp_mask & ~(__GFP_DIRECT_RECLAIM|__GFP_IO);

repeat_alloc:

	element = pool->alloc(gfp_temp, pool->pool_data);
	if (likely(element != NULL))
		return element;

	spin_lock_irqsave(&pool->lock, flags);
	if (likely(pool->curr_nr)) {
		element = remove_element(pool);
		spin_unlock_irqrestore(&pool->lock, flags);
		/* paired with rmb in mempool_free(), read comment there */
		smp_wmb();
		/*
		 * Update the allocation stack trace as this is more useful
		 * for debugging.
		 */
		kmemleak_update_trace(element);
		return element;
	}

	/*
	 * We use gfp mask w/o direct reclaim or IO for the first round.  If
	 * alloc failed with that and @pool was empty, retry immediately.
	 */
	if (gfp_temp != gfp_mask) {
		spin_unlock_irqrestore(&pool->lock, flags);
		gfp_temp = gfp_mask;
		goto repeat_alloc;
	}

	/* We must not sleep if !__GFP_DIRECT_RECLAIM */
	if (!(gfp_mask & __GFP_DIRECT_RECLAIM)) {
		spin_unlock_irqrestore(&pool->lock, flags);
		return NULL;
	}

	/* Let's wait for someone else to return an element to @pool */
	init_wait(&wait);
	prepare_to_wait(&pool->wait, &wait, TASK_UNINTERRUPTIBLE);

	spin_unlock_irqrestore(&pool->lock, flags);

	/*
	 * FIXME: this should be io_schedule().  The timeout is there as a
	 * workaround for some DM problems in 2.6.18.
	 */
	io_schedule_timeout(5*HZ);

	finish_wait(&pool->wait, &wait);
	goto repeat_alloc;
}
EXPORT_SYMBOL(mempool_alloc);

/**
 * mempool_free - return an element to the pool.
 * @element:   pool element pointer.
 * @pool:      pointer to the memory pool which was allocated via
 *             mempool_create().
 *
 * this function only sleeps if the free_fn() function sleeps.
 */
void mempool_free(void *element, mempool_t *pool)
{
	unsigned long flags;

	if (unlikely(element == NULL))
		return;

	/*
	 * Paired with the wmb in mempool_alloc().  The preceding read is
	 * for @element and the following @pool->curr_nr.  This ensures
	 * that the visible value of @pool->curr_nr is from after the
	 * allocation of @element.  This is necessary for fringe cases
	 * where @element was passed to this task without going through
	 * barriers.
	 *
	 * For example, assume @p is %NULL at the beginning and one task
	 * performs "p = mempool_alloc(...);" while another task is doing
	 * "while (!p) cpu_relax(); mempool_free(p, ...);".  This function
	 * may end up using curr_nr value which is from before allocation
	 * of @p without the following rmb.
	 */
	smp_rmb();

	/*
	 * For correctness, we need a test which is guaranteed to trigger
	 * if curr_nr + #allocated == min_nr.  Testing curr_nr < min_nr
	 * without locking achieves that and refilling as soon as possible
	 * is desirable.
	 *
	 * Because curr_nr visible here is always a value after the
	 * allocation of @element, any task which decremented curr_nr below
	 * min_nr is guaranteed to see curr_nr < min_nr unless curr_nr gets
	 * incremented to min_nr afterwards.  If curr_nr gets incremented
	 * to min_nr after the allocation of @element, the elements
	 * allocated after that are subject to the same guarantee.
	 *
	 * Waiters happen iff curr_nr is 0 and the above guarantee also
	 * ensures that there will be frees which return elements to the
	 * pool waking up the waiters.
	 */
	if (unlikely(READ_ONCE(pool->curr_nr) < pool->min_nr)) {
		spin_lock_irqsave(&pool->lock, flags);
		if (likely(pool->curr_nr < pool->min_nr)) {
			add_element(pool, element);
			spin_unlock_irqrestore(&pool->lock, flags);
			wake_up(&pool->wait);
			return;
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}
	pool->free(element, pool->pool_data);
}
EXPORT_SYMBOL(mempool_free);

/*
 * A commonly used alloc and free fn.
 */
void *mempool_alloc_slab(gfp_t gfp_mask, void *pool_data)
{
	struct kmem_cache *mem = pool_data;
	VM_BUG_ON(mem->ctor);
	return kmem_cache_alloc(mem, gfp_mask);
}
EXPORT_SYMBOL(mempool_alloc_slab);

void mempool_free_slab(void *element, void *pool_data)
{
	struct kmem_cache *mem = pool_data;
	kmem_cache_free(mem, element);
}
EXPORT_SYMBOL(mempool_free_slab);

/*
 * A commonly used alloc and free fn that kmalloc/kfrees the amount of memory
 * specified by pool_data
 */
void *mempool_kmalloc(gfp_t gfp_mask, void *pool_data)
{
	size_t size = (size_t)pool_data;
	return kmalloc(size, gfp_mask);
}
EXPORT_SYMBOL(mempool_kmalloc);

void mempool_kfree(void *element, void *pool_data)
{
	kfree(element);
}
EXPORT_SYMBOL(mempool_kfree);

/*
 * A simple mempool-backed page allocator that allocates pages
 * of the order specified by pool_data.
 */
void *mempool_alloc_pages(gfp_t gfp_mask, void *pool_data)
{
	int order = (int)(long)pool_data;
	return alloc_pages(gfp_mask, order);
}
EXPORT_SYMBOL(mempool_alloc_pages);

void mempool_free_pages(void *element, void *pool_data)
{
	int order = (int)(long)pool_data;
	__free_pages(element, order);
}
EXPORT_SYMBOL(mempool_free_pages);
