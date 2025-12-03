// SPDX-License-Identifier: GPL-2.0
/*
 *  memory buffer pool support. Such pools are mostly used
 *  for guaranteed, deadlock-free memory allocations during
 *  extreme VM load.
 *
 *  started by Ingo Molnar, Copyright (C) 2001
 *  debugging by David Rientjes, Copyright (C) 2015
 */
#include <linux/fault-inject.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/kasan.h>
#include <linux/kmemleak.h>
#include <linux/export.h>
#include <linux/mempool.h>
#include <linux/writeback.h>
#include "slab.h"

static DECLARE_FAULT_ATTR(fail_mempool_alloc);
static DECLARE_FAULT_ATTR(fail_mempool_alloc_bulk);

static int __init mempool_faul_inject_init(void)
{
	int error;

	error = PTR_ERR_OR_ZERO(fault_create_debugfs_attr("fail_mempool_alloc",
			NULL, &fail_mempool_alloc));
	if (error)
		return error;

	/* booting will fail on error return here, don't bother to cleanup */
	return PTR_ERR_OR_ZERO(
		fault_create_debugfs_attr("fail_mempool_alloc_bulk", NULL,
		&fail_mempool_alloc_bulk));
}
late_initcall(mempool_faul_inject_init);

#ifdef CONFIG_SLUB_DEBUG_ON
static void poison_error(struct mempool *pool, void *element, size_t size,
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

static void __check_element(struct mempool *pool, void *element, size_t size)
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

static void check_element(struct mempool *pool, void *element)
{
	/* Skip checking: KASAN might save its metadata in the element. */
	if (kasan_enabled())
		return;

	/* Mempools backed by slab allocator */
	if (pool->free == mempool_kfree) {
		__check_element(pool, element, (size_t)pool->pool_data);
	} else if (pool->free == mempool_free_slab) {
		__check_element(pool, element, kmem_cache_size(pool->pool_data));
	} else if (pool->free == mempool_free_pages) {
		/* Mempools backed by page allocator */
		int order = (int)(long)pool->pool_data;

#ifdef CONFIG_HIGHMEM
		for (int i = 0; i < (1 << order); i++) {
			struct page *page = (struct page *)element;
			void *addr = kmap_local_page(page + i);

			__check_element(pool, addr, PAGE_SIZE);
			kunmap_local(addr);
		}
#else
		void *addr = page_address((struct page *)element);

		__check_element(pool, addr, PAGE_SIZE << order);
#endif
	}
}

static void __poison_element(void *element, size_t size)
{
	u8 *obj = element;

	memset(obj, POISON_FREE, size - 1);
	obj[size - 1] = POISON_END;
}

static void poison_element(struct mempool *pool, void *element)
{
	/* Skip poisoning: KASAN might save its metadata in the element. */
	if (kasan_enabled())
		return;

	/* Mempools backed by slab allocator */
	if (pool->alloc == mempool_kmalloc) {
		__poison_element(element, (size_t)pool->pool_data);
	} else if (pool->alloc == mempool_alloc_slab) {
		__poison_element(element, kmem_cache_size(pool->pool_data));
	} else if (pool->alloc == mempool_alloc_pages) {
		/* Mempools backed by page allocator */
		int order = (int)(long)pool->pool_data;

#ifdef CONFIG_HIGHMEM
		for (int i = 0; i < (1 << order); i++) {
			struct page *page = (struct page *)element;
			void *addr = kmap_local_page(page + i);

			__poison_element(addr, PAGE_SIZE);
			kunmap_local(addr);
		}
#else
		void *addr = page_address((struct page *)element);

		__poison_element(addr, PAGE_SIZE << order);
#endif
	}
}
#else /* CONFIG_SLUB_DEBUG_ON */
static inline void check_element(struct mempool *pool, void *element)
{
}
static inline void poison_element(struct mempool *pool, void *element)
{
}
#endif /* CONFIG_SLUB_DEBUG_ON */

static __always_inline bool kasan_poison_element(struct mempool *pool,
		void *element)
{
	if (pool->alloc == mempool_alloc_slab || pool->alloc == mempool_kmalloc)
		return kasan_mempool_poison_object(element);
	else if (pool->alloc == mempool_alloc_pages)
		return kasan_mempool_poison_pages(element,
						(unsigned long)pool->pool_data);
	return true;
}

static void kasan_unpoison_element(struct mempool *pool, void *element)
{
	if (pool->alloc == mempool_kmalloc)
		kasan_mempool_unpoison_object(element, (size_t)pool->pool_data);
	else if (pool->alloc == mempool_alloc_slab)
		kasan_mempool_unpoison_object(element,
					      kmem_cache_size(pool->pool_data));
	else if (pool->alloc == mempool_alloc_pages)
		kasan_mempool_unpoison_pages(element,
					     (unsigned long)pool->pool_data);
}

static __always_inline void add_element(struct mempool *pool, void *element)
{
	BUG_ON(pool->min_nr != 0 && pool->curr_nr >= pool->min_nr);
	poison_element(pool, element);
	if (kasan_poison_element(pool, element))
		pool->elements[pool->curr_nr++] = element;
}

static void *remove_element(struct mempool *pool)
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
void mempool_exit(struct mempool *pool)
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
void mempool_destroy(struct mempool *pool)
{
	if (unlikely(!pool))
		return;

	mempool_exit(pool);
	kfree(pool);
}
EXPORT_SYMBOL(mempool_destroy);

int mempool_init_node(struct mempool *pool, int min_nr,
		mempool_alloc_t *alloc_fn, mempool_free_t *free_fn,
		void *pool_data, gfp_t gfp_mask, int node_id)
{
	spin_lock_init(&pool->lock);
	pool->min_nr	= min_nr;
	pool->pool_data = pool_data;
	pool->alloc	= alloc_fn;
	pool->free	= free_fn;
	init_waitqueue_head(&pool->wait);
	/*
	 * max() used here to ensure storage for at least 1 element to support
	 * zero minimum pool
	 */
	pool->elements = kmalloc_array_node(max(1, min_nr), sizeof(void *),
					    gfp_mask, node_id);
	if (!pool->elements)
		return -ENOMEM;

	/*
	 * First pre-allocate the guaranteed number of buffers,
	 * also pre-allocate 1 element for zero minimum pool.
	 */
	while (pool->curr_nr < max(1, pool->min_nr)) {
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
int mempool_init_noprof(struct mempool *pool, int min_nr,
		mempool_alloc_t *alloc_fn, mempool_free_t *free_fn,
		void *pool_data)
{
	return mempool_init_node(pool, min_nr, alloc_fn, free_fn,
				 pool_data, GFP_KERNEL, NUMA_NO_NODE);

}
EXPORT_SYMBOL(mempool_init_noprof);

/**
 * mempool_create_node - create a memory pool
 * @min_nr:    the minimum number of elements guaranteed to be
 *             allocated for this pool.
 * @alloc_fn:  user-defined element-allocation function.
 * @free_fn:   user-defined element-freeing function.
 * @pool_data: optional private data available to the user-defined functions.
 * @gfp_mask:  memory allocation flags
 * @node_id:   numa node to allocate on
 *
 * this function creates and allocates a guaranteed size, preallocated
 * memory pool. The pool can be used from the mempool_alloc() and mempool_free()
 * functions. This function might sleep. Both the alloc_fn() and the free_fn()
 * functions might sleep - as long as the mempool_alloc() function is not called
 * from IRQ contexts.
 *
 * Return: pointer to the created memory pool object or %NULL on error.
 */
struct mempool *mempool_create_node_noprof(int min_nr,
		mempool_alloc_t *alloc_fn, mempool_free_t *free_fn,
		void *pool_data, gfp_t gfp_mask, int node_id)
{
	struct mempool *pool;

	pool = kmalloc_node_noprof(sizeof(*pool), gfp_mask | __GFP_ZERO, node_id);
	if (!pool)
		return NULL;

	if (mempool_init_node(pool, min_nr, alloc_fn, free_fn, pool_data,
			      gfp_mask, node_id)) {
		kfree(pool);
		return NULL;
	}

	return pool;
}
EXPORT_SYMBOL(mempool_create_node_noprof);

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
int mempool_resize(struct mempool *pool, int new_min_nr)
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

static unsigned int mempool_alloc_from_pool(struct mempool *pool, void **elems,
		unsigned int count, unsigned int allocated,
		gfp_t gfp_mask)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&pool->lock, flags);
	if (unlikely(pool->curr_nr < count - allocated))
		goto fail;
	for (i = 0; i < count; i++) {
		if (!elems[i]) {
			elems[i] = remove_element(pool);
			allocated++;
		}
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	/* Paired with rmb in mempool_free(), read comment there. */
	smp_wmb();

	/*
	 * Update the allocation stack trace as this is more useful for
	 * debugging.
	 */
	for (i = 0; i < count; i++)
		kmemleak_update_trace(elems[i]);
	return allocated;

fail:
	if (gfp_mask & __GFP_DIRECT_RECLAIM) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&pool->wait, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_irqrestore(&pool->lock, flags);

		/*
		 * Wait for someone else to return an element to @pool, but wake
		 * up occasionally as memory pressure might have reduced even
		 * and the normal allocation in alloc_fn could succeed even if
		 * no element was returned.
		 */
		io_schedule_timeout(5 * HZ);
		finish_wait(&pool->wait, &wait);
	} else {
		/* We must not sleep if __GFP_DIRECT_RECLAIM is not set. */
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	return allocated;
}

/*
 * Adjust the gfp flags for mempool allocations, as we never want to dip into
 * the global emergency reserves or retry in the page allocator.
 *
 * The first pass also doesn't want to go reclaim, but the next passes do, so
 * return a separate subset for that first iteration.
 */
static inline gfp_t mempool_adjust_gfp(gfp_t *gfp_mask)
{
	*gfp_mask |= __GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;
	return *gfp_mask & ~(__GFP_DIRECT_RECLAIM | __GFP_IO);
}

/**
 * mempool_alloc_bulk - allocate multiple elements from a memory pool
 * @pool:	pointer to the memory pool
 * @elems:	partially or fully populated elements array
 * @count:	number of entries in @elem that need to be allocated
 * @allocated:	number of entries in @elem already allocated
 *
 * Allocate elements for each slot in @elem that is non-%NULL. This is done by
 * first calling into the alloc_fn supplied at pool initialization time, and
 * dipping into the reserved pool when alloc_fn fails to allocate an element.
 *
 * On return all @count elements in @elems will be populated.
 *
 * Return: Always 0.  If it wasn't for %$#^$ alloc tags, it would return void.
 */
int mempool_alloc_bulk_noprof(struct mempool *pool, void **elems,
		unsigned int count, unsigned int allocated)
{
	gfp_t gfp_mask = GFP_KERNEL;
	gfp_t gfp_temp = mempool_adjust_gfp(&gfp_mask);
	unsigned int i = 0;

	VM_WARN_ON_ONCE(count > pool->min_nr);
	might_alloc(gfp_mask);

	/*
	 * If an error is injected, fail all elements in a bulk allocation so
	 * that we stress the multiple elements missing path.
	 */
	if (should_fail_ex(&fail_mempool_alloc_bulk, 1, FAULT_NOWARN)) {
		pr_info("forcing mempool usage for %pS\n",
				(void *)_RET_IP_);
		goto use_pool;
	}

repeat_alloc:
	/*
	 * Try to allocate the elements using the allocation callback first as
	 * that might succeed even when the caller's bulk allocation did not.
	 */
	for (i = 0; i < count; i++) {
		if (elems[i])
			continue;
		elems[i] = pool->alloc(gfp_temp, pool->pool_data);
		if (unlikely(!elems[i]))
			goto use_pool;
		allocated++;
	}

	return 0;

use_pool:
	allocated = mempool_alloc_from_pool(pool, elems, count, allocated,
			gfp_temp);
	gfp_temp = gfp_mask;
	goto repeat_alloc;
}
EXPORT_SYMBOL_GPL(mempool_alloc_bulk_noprof);

/**
 * mempool_alloc - allocate an element from a memory pool
 * @pool:	pointer to the memory pool
 * @gfp_mask:	GFP_* flags.  %__GFP_ZERO is not supported.
 *
 * Allocate an element from @pool.  This is done by first calling into the
 * alloc_fn supplied at pool initialization time, and dipping into the reserved
 * pool when alloc_fn fails to allocate an element.
 *
 * This function only sleeps if the alloc_fn callback sleeps, or when waiting
 * for elements to become available in the pool.
 *
 * Return: pointer to the allocated element or %NULL when failing to allocate
 * an element.  Allocation failure can only happen when @gfp_mask does not
 * include %__GFP_DIRECT_RECLAIM.
 */
void *mempool_alloc_noprof(struct mempool *pool, gfp_t gfp_mask)
{
	gfp_t gfp_temp = mempool_adjust_gfp(&gfp_mask);
	void *element;

	VM_WARN_ON_ONCE(gfp_mask & __GFP_ZERO);
	might_alloc(gfp_mask);

repeat_alloc:
	if (should_fail_ex(&fail_mempool_alloc, 1, FAULT_NOWARN)) {
		pr_info("forcing mempool usage for %pS\n",
				(void *)_RET_IP_);
		element = NULL;
	} else {
		element = pool->alloc(gfp_temp, pool->pool_data);
	}

	if (unlikely(!element)) {
		/*
		 * Try to allocate an element from the pool.
		 *
		 * The first pass won't have __GFP_DIRECT_RECLAIM and won't
		 * sleep in mempool_alloc_from_pool.  Retry the allocation
		 * with all flags set in that case.
		 */
		if (!mempool_alloc_from_pool(pool, &element, 1, 0, gfp_temp)) {
			if (gfp_temp != gfp_mask) {
				gfp_temp = gfp_mask;
				goto repeat_alloc;
			}
			if (gfp_mask & __GFP_DIRECT_RECLAIM) {
				goto repeat_alloc;
			}
		}
	}

	return element;
}
EXPORT_SYMBOL(mempool_alloc_noprof);

/**
 * mempool_alloc_preallocated - allocate an element from preallocated elements
 *                              belonging to a memory pool
 * @pool:	pointer to the memory pool
 *
 * This function is similar to mempool_alloc(), but it only attempts allocating
 * an element from the preallocated elements. It only takes a single spinlock_t
 * and immediately returns if no preallocated elements are available.
 *
 * Return: pointer to the allocated element or %NULL if no elements are
 * available.
 */
void *mempool_alloc_preallocated(struct mempool *pool)
{
	void *element = NULL;

	mempool_alloc_from_pool(pool, &element, 1, 0, GFP_NOWAIT);
	return element;
}
EXPORT_SYMBOL(mempool_alloc_preallocated);

/**
 * mempool_free_bulk - return elements to a mempool
 * @pool:	pointer to the memory pool
 * @elems:	elements to return
 * @count:	number of elements to return
 *
 * Returns a number of elements from the start of @elem to @pool if @pool needs
 * replenishing and sets their slots in @elem to NULL.  Other elements are left
 * in @elem.
 *
 * Return: number of elements transferred to @pool.  Elements are always
 * transferred from the beginning of @elem, so the return value can be used as
 * an offset into @elem for the freeing the remaining elements in the caller.
 */
unsigned int mempool_free_bulk(struct mempool *pool, void **elems,
		unsigned int count)
{
	unsigned long flags;
	unsigned int freed = 0;
	bool added = false;

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
	 *
	 * For zero-minimum pools, curr_nr < min_nr (0 < 0) never succeeds,
	 * so waiters sleeping on pool->wait would never be woken by the
	 * wake-up path of previous test. This explicit check ensures the
	 * allocation of element when both min_nr and curr_nr are 0, and
	 * any active waiters are properly awakened.
	 */
	if (unlikely(READ_ONCE(pool->curr_nr) < pool->min_nr)) {
		spin_lock_irqsave(&pool->lock, flags);
		while (pool->curr_nr < pool->min_nr && freed < count) {
			add_element(pool, elems[freed++]);
			added = true;
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	} else if (unlikely(pool->min_nr == 0 &&
		     READ_ONCE(pool->curr_nr) == 0)) {
		/* Handle the min_nr = 0 edge case: */
		spin_lock_irqsave(&pool->lock, flags);
		if (likely(pool->curr_nr == 0)) {
			add_element(pool, elems[freed++]);
			added = true;
		}
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	if (unlikely(added) && wq_has_sleeper(&pool->wait))
		wake_up(&pool->wait);

	return freed;
}
EXPORT_SYMBOL_GPL(mempool_free_bulk);

/**
 * mempool_free - return an element to the pool.
 * @element:	element to return
 * @pool:	pointer to the memory pool
 *
 * Returns @element to @pool if it needs replenishing, else frees it using
 * the free_fn callback in @pool.
 *
 * This function only sleeps if the free_fn callback sleeps.
 */
void mempool_free(void *element, struct mempool *pool)
{
	if (likely(element) && !mempool_free_bulk(pool, &element, 1))
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
	return kmem_cache_alloc_noprof(mem, gfp_mask);
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
	return kmalloc_noprof(size, gfp_mask);
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
	return alloc_pages_noprof(gfp_mask, order);
}
EXPORT_SYMBOL(mempool_alloc_pages);

void mempool_free_pages(void *element, void *pool_data)
{
	int order = (int)(long)pool_data;
	__free_pages(element, order);
}
EXPORT_SYMBOL(mempool_free_pages);
