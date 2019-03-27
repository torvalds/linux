/*
 * Copyright (c) Red Hat Inc.

 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Jerome Glisse <jglisse@redhat.com>
 *          Pauli Nieminen <suokkos@gmail.com>
 */
/*
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * <kib@FreeBSD.org> under sponsorship from the FreeBSD Foundation.
 */

/* simple list based uncached page pool
 * - Pool collects resently freed pages for reuse
 * - Use page->lru to keep a free list
 * - doesn't track currently in use pages
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>
#include <dev/drm2/ttm/ttm_bo_driver.h>
#include <dev/drm2/ttm/ttm_page_alloc.h>
#include <vm/vm_pageout.h>

#define NUM_PAGES_TO_ALLOC		(PAGE_SIZE/sizeof(vm_page_t))
#define SMALL_ALLOCATION		16
#define FREE_ALL_PAGES			(~0U)
/* times are in msecs */
#define PAGE_FREE_INTERVAL		1000

/**
 * struct ttm_page_pool - Pool to reuse recently allocated uc/wc pages.
 *
 * @lock: Protects the shared pool from concurrnet access. Must be used with
 * irqsave/irqrestore variants because pool allocator maybe called from
 * delayed work.
 * @fill_lock: Prevent concurrent calls to fill.
 * @list: Pool of free uc/wc pages for fast reuse.
 * @gfp_flags: Flags to pass for alloc_page.
 * @npages: Number of pages in pool.
 */
struct ttm_page_pool {
	struct mtx		lock;
	bool			fill_lock;
	bool			dma32;
	struct pglist		list;
	int			ttm_page_alloc_flags;
	unsigned		npages;
	char			*name;
	unsigned long		nfrees;
	unsigned long		nrefills;
};

/**
 * Limits for the pool. They are handled without locks because only place where
 * they may change is in sysfs store. They won't have immediate effect anyway
 * so forcing serialization to access them is pointless.
 */

struct ttm_pool_opts {
	unsigned	alloc_size;
	unsigned	max_size;
	unsigned	small;
};

#define NUM_POOLS 4

/**
 * struct ttm_pool_manager - Holds memory pools for fst allocation
 *
 * Manager is read only object for pool code so it doesn't need locking.
 *
 * @free_interval: minimum number of jiffies between freeing pages from pool.
 * @page_alloc_inited: reference counting for pool allocation.
 * @work: Work that is used to shrink the pool. Work is only run when there is
 * some pages to free.
 * @small_allocation: Limit in number of pages what is small allocation.
 *
 * @pools: All pool objects in use.
 **/
struct ttm_pool_manager {
	unsigned int kobj_ref;
	eventhandler_tag lowmem_handler;
	struct ttm_pool_opts	options;

	union {
		struct ttm_page_pool	u_pools[NUM_POOLS];
		struct _utag {
			struct ttm_page_pool	u_wc_pool;
			struct ttm_page_pool	u_uc_pool;
			struct ttm_page_pool	u_wc_pool_dma32;
			struct ttm_page_pool	u_uc_pool_dma32;
		} _ut;
	} _u;
};

#define	pools _u.u_pools
#define	wc_pool _u._ut.u_wc_pool
#define	uc_pool _u._ut.u_uc_pool
#define	wc_pool_dma32 _u._ut.u_wc_pool_dma32
#define	uc_pool_dma32 _u._ut.u_uc_pool_dma32

MALLOC_DEFINE(M_TTM_POOLMGR, "ttm_poolmgr", "TTM Pool Manager");

static void
ttm_vm_page_free(vm_page_t m)
{

	KASSERT(m->object == NULL, ("ttm page %p is owned", m));
	KASSERT(m->wire_count == 1, ("ttm lost wire %p", m));
	KASSERT((m->flags & PG_FICTITIOUS) != 0, ("ttm lost fictitious %p", m));
	KASSERT((m->oflags & VPO_UNMANAGED) == 0, ("ttm got unmanaged %p", m));
	m->flags &= ~PG_FICTITIOUS;
	m->oflags |= VPO_UNMANAGED;
	vm_page_unwire(m, PQ_NONE);
	vm_page_free(m);
}

static vm_memattr_t
ttm_caching_state_to_vm(enum ttm_caching_state cstate)
{

	switch (cstate) {
	case tt_uncached:
		return (VM_MEMATTR_UNCACHEABLE);
	case tt_wc:
		return (VM_MEMATTR_WRITE_COMBINING);
	case tt_cached:
		return (VM_MEMATTR_WRITE_BACK);
	}
	panic("caching state %d\n", cstate);
}

static vm_page_t
ttm_vm_page_alloc_dma32(int req, vm_memattr_t memattr)
{
	vm_page_t p;
	int tries;

	for (tries = 0; ; tries++) {
		p = vm_page_alloc_contig(NULL, 0, req, 1, 0, 0xffffffff,
		    PAGE_SIZE, 0, memattr);
		if (p != NULL || tries > 2)
			return (p);
		if (!vm_page_reclaim_contig(req, 1, 0, 0xffffffff,
		    PAGE_SIZE, 0))
			vm_wait(NULL);
	}
}

static vm_page_t
ttm_vm_page_alloc_any(int req, vm_memattr_t memattr)
{
	vm_page_t p;

	while (1) {
		p = vm_page_alloc(NULL, 0, req);
		if (p != NULL)
			break;
		vm_wait(NULL);
	}
	pmap_page_set_memattr(p, memattr);
	return (p);
}

static vm_page_t
ttm_vm_page_alloc(int flags, enum ttm_caching_state cstate)
{
	vm_page_t p;
	vm_memattr_t memattr;
	int req;

	memattr = ttm_caching_state_to_vm(cstate);
	req = VM_ALLOC_NORMAL | VM_ALLOC_WIRED | VM_ALLOC_NOOBJ;
	if ((flags & TTM_PAGE_FLAG_ZERO_ALLOC) != 0)
		req |= VM_ALLOC_ZERO;

	if ((flags & TTM_PAGE_FLAG_DMA32) != 0)
		p = ttm_vm_page_alloc_dma32(req, memattr);
	else
		p = ttm_vm_page_alloc_any(req, memattr);

	if (p != NULL) {
		p->oflags &= ~VPO_UNMANAGED;
		p->flags |= PG_FICTITIOUS;
	}
	return (p);
}

static void ttm_pool_kobj_release(struct ttm_pool_manager *m)
{

	free(m, M_TTM_POOLMGR);
}

#if 0
/* XXXKIB sysctl */
static ssize_t ttm_pool_store(struct ttm_pool_manager *m,
		struct attribute *attr, const char *buffer, size_t size)
{
	int chars;
	unsigned val;
	chars = sscanf(buffer, "%u", &val);
	if (chars == 0)
		return size;

	/* Convert kb to number of pages */
	val = val / (PAGE_SIZE >> 10);

	if (attr == &ttm_page_pool_max)
		m->options.max_size = val;
	else if (attr == &ttm_page_pool_small)
		m->options.small = val;
	else if (attr == &ttm_page_pool_alloc_size) {
		if (val > NUM_PAGES_TO_ALLOC*8) {
			pr_err("Setting allocation size to %lu is not allowed. Recommended size is %lu\n",
			       NUM_PAGES_TO_ALLOC*(PAGE_SIZE >> 7),
			       NUM_PAGES_TO_ALLOC*(PAGE_SIZE >> 10));
			return size;
		} else if (val > NUM_PAGES_TO_ALLOC) {
			pr_warn("Setting allocation size to larger than %lu is not recommended\n",
				NUM_PAGES_TO_ALLOC*(PAGE_SIZE >> 10));
		}
		m->options.alloc_size = val;
	}

	return size;
}

static ssize_t ttm_pool_show(struct ttm_pool_manager *m,
		struct attribute *attr, char *buffer)
{
	unsigned val = 0;

	if (attr == &ttm_page_pool_max)
		val = m->options.max_size;
	else if (attr == &ttm_page_pool_small)
		val = m->options.small;
	else if (attr == &ttm_page_pool_alloc_size)
		val = m->options.alloc_size;

	val = val * (PAGE_SIZE >> 10);

	return snprintf(buffer, PAGE_SIZE, "%u\n", val);
}
#endif

static struct ttm_pool_manager *_manager;

static int set_pages_array_wb(vm_page_t *pages, int addrinarray)
{
#ifdef TTM_HAS_AGP
	int i;

	for (i = 0; i < addrinarray; i++)
		pmap_page_set_memattr(pages[i], VM_MEMATTR_WRITE_BACK);
#endif
	return 0;
}

static int set_pages_array_wc(vm_page_t *pages, int addrinarray)
{
#ifdef TTM_HAS_AGP
	int i;

	for (i = 0; i < addrinarray; i++)
		pmap_page_set_memattr(pages[i], VM_MEMATTR_WRITE_COMBINING);
#endif
	return 0;
}

static int set_pages_array_uc(vm_page_t *pages, int addrinarray)
{
#ifdef TTM_HAS_AGP
	int i;

	for (i = 0; i < addrinarray; i++)
		pmap_page_set_memattr(pages[i], VM_MEMATTR_UNCACHEABLE);
#endif
	return 0;
}

/**
 * Select the right pool or requested caching state and ttm flags. */
static struct ttm_page_pool *ttm_get_pool(int flags,
		enum ttm_caching_state cstate)
{
	int pool_index;

	if (cstate == tt_cached)
		return NULL;

	if (cstate == tt_wc)
		pool_index = 0x0;
	else
		pool_index = 0x1;

	if (flags & TTM_PAGE_FLAG_DMA32)
		pool_index |= 0x2;

	return &_manager->pools[pool_index];
}

/* set memory back to wb and free the pages. */
static void ttm_pages_put(vm_page_t *pages, unsigned npages)
{
	unsigned i;

	/* Our VM handles vm memattr automatically on the page free. */
	if (set_pages_array_wb(pages, npages))
		printf("[TTM] Failed to set %d pages to wb!\n", npages);
	for (i = 0; i < npages; ++i)
		ttm_vm_page_free(pages[i]);
}

static void ttm_pool_update_free_locked(struct ttm_page_pool *pool,
		unsigned freed_pages)
{
	pool->npages -= freed_pages;
	pool->nfrees += freed_pages;
}

/**
 * Free pages from pool.
 *
 * To prevent hogging the ttm_swap process we only free NUM_PAGES_TO_ALLOC
 * number of pages in one go.
 *
 * @pool: to free the pages from
 * @free_all: If set to true will free all pages in pool
 **/
static int ttm_page_pool_free(struct ttm_page_pool *pool, unsigned nr_free)
{
	vm_page_t p, p1;
	vm_page_t *pages_to_free;
	unsigned freed_pages = 0,
		 npages_to_free = nr_free;
	unsigned i;

	if (NUM_PAGES_TO_ALLOC < nr_free)
		npages_to_free = NUM_PAGES_TO_ALLOC;

	pages_to_free = malloc(npages_to_free * sizeof(vm_page_t),
	    M_TEMP, M_WAITOK | M_ZERO);

restart:
	mtx_lock(&pool->lock);

	TAILQ_FOREACH_REVERSE_SAFE(p, &pool->list, pglist, plinks.q, p1) {
		if (freed_pages >= npages_to_free)
			break;

		pages_to_free[freed_pages++] = p;
		/* We can only remove NUM_PAGES_TO_ALLOC at a time. */
		if (freed_pages >= NUM_PAGES_TO_ALLOC) {
			/* remove range of pages from the pool */
			for (i = 0; i < freed_pages; i++)
				TAILQ_REMOVE(&pool->list, pages_to_free[i], plinks.q);

			ttm_pool_update_free_locked(pool, freed_pages);
			/**
			 * Because changing page caching is costly
			 * we unlock the pool to prevent stalling.
			 */
			mtx_unlock(&pool->lock);

			ttm_pages_put(pages_to_free, freed_pages);
			if (likely(nr_free != FREE_ALL_PAGES))
				nr_free -= freed_pages;

			if (NUM_PAGES_TO_ALLOC >= nr_free)
				npages_to_free = nr_free;
			else
				npages_to_free = NUM_PAGES_TO_ALLOC;

			freed_pages = 0;

			/* free all so restart the processing */
			if (nr_free)
				goto restart;

			/* Not allowed to fall through or break because
			 * following context is inside spinlock while we are
			 * outside here.
			 */
			goto out;

		}
	}

	/* remove range of pages from the pool */
	if (freed_pages) {
		for (i = 0; i < freed_pages; i++)
			TAILQ_REMOVE(&pool->list, pages_to_free[i], plinks.q);

		ttm_pool_update_free_locked(pool, freed_pages);
		nr_free -= freed_pages;
	}

	mtx_unlock(&pool->lock);

	if (freed_pages)
		ttm_pages_put(pages_to_free, freed_pages);
out:
	free(pages_to_free, M_TEMP);
	return nr_free;
}

/* Get good estimation how many pages are free in pools */
static int ttm_pool_get_num_unused_pages(void)
{
	unsigned i;
	int total = 0;
	for (i = 0; i < NUM_POOLS; ++i)
		total += _manager->pools[i].npages;

	return total;
}

/**
 * Callback for mm to request pool to reduce number of page held.
 */
static int ttm_pool_mm_shrink(void *arg)
{
	static unsigned int start_pool = 0;
	unsigned i;
	unsigned pool_offset = atomic_fetchadd_int(&start_pool, 1);
	struct ttm_page_pool *pool;
	int shrink_pages = 100; /* XXXKIB */

	pool_offset = pool_offset % NUM_POOLS;
	/* select start pool in round robin fashion */
	for (i = 0; i < NUM_POOLS; ++i) {
		unsigned nr_free = shrink_pages;
		if (shrink_pages == 0)
			break;
		pool = &_manager->pools[(i + pool_offset)%NUM_POOLS];
		shrink_pages = ttm_page_pool_free(pool, nr_free);
	}
	/* return estimated number of unused pages in pool */
	return ttm_pool_get_num_unused_pages();
}

static void ttm_pool_mm_shrink_init(struct ttm_pool_manager *manager)
{

	manager->lowmem_handler = EVENTHANDLER_REGISTER(vm_lowmem,
	    ttm_pool_mm_shrink, manager, EVENTHANDLER_PRI_ANY);
}

static void ttm_pool_mm_shrink_fini(struct ttm_pool_manager *manager)
{

	EVENTHANDLER_DEREGISTER(vm_lowmem, manager->lowmem_handler);
}

static int ttm_set_pages_caching(vm_page_t *pages,
		enum ttm_caching_state cstate, unsigned cpages)
{
	int r = 0;
	/* Set page caching */
	switch (cstate) {
	case tt_uncached:
		r = set_pages_array_uc(pages, cpages);
		if (r)
			printf("[TTM] Failed to set %d pages to uc!\n", cpages);
		break;
	case tt_wc:
		r = set_pages_array_wc(pages, cpages);
		if (r)
			printf("[TTM] Failed to set %d pages to wc!\n", cpages);
		break;
	default:
		break;
	}
	return r;
}

/**
 * Free pages the pages that failed to change the caching state. If there is
 * any pages that have changed their caching state already put them to the
 * pool.
 */
static void ttm_handle_caching_state_failure(struct pglist *pages,
		int ttm_flags, enum ttm_caching_state cstate,
		vm_page_t *failed_pages, unsigned cpages)
{
	unsigned i;
	/* Failed pages have to be freed */
	for (i = 0; i < cpages; ++i) {
		TAILQ_REMOVE(pages, failed_pages[i], plinks.q);
		ttm_vm_page_free(failed_pages[i]);
	}
}

/**
 * Allocate new pages with correct caching.
 *
 * This function is reentrant if caller updates count depending on number of
 * pages returned in pages array.
 */
static int ttm_alloc_new_pages(struct pglist *pages, int ttm_alloc_flags,
		int ttm_flags, enum ttm_caching_state cstate, unsigned count)
{
	vm_page_t *caching_array;
	vm_page_t p;
	int r = 0;
	unsigned i, cpages;
	unsigned max_cpages = min(count,
			(unsigned)(PAGE_SIZE/sizeof(vm_page_t)));

	/* allocate array for page caching change */
	caching_array = malloc(max_cpages * sizeof(vm_page_t), M_TEMP,
	    M_WAITOK | M_ZERO);

	for (i = 0, cpages = 0; i < count; ++i) {
		p = ttm_vm_page_alloc(ttm_alloc_flags, cstate);
		if (!p) {
			printf("[TTM] Unable to get page %u\n", i);

			/* store already allocated pages in the pool after
			 * setting the caching state */
			if (cpages) {
				r = ttm_set_pages_caching(caching_array,
							  cstate, cpages);
				if (r)
					ttm_handle_caching_state_failure(pages,
						ttm_flags, cstate,
						caching_array, cpages);
			}
			r = -ENOMEM;
			goto out;
		}

#ifdef CONFIG_HIGHMEM /* KIB: nop */
		/* gfp flags of highmem page should never be dma32 so we
		 * we should be fine in such case
		 */
		if (!PageHighMem(p))
#endif
		{
			caching_array[cpages++] = p;
			if (cpages == max_cpages) {

				r = ttm_set_pages_caching(caching_array,
						cstate, cpages);
				if (r) {
					ttm_handle_caching_state_failure(pages,
						ttm_flags, cstate,
						caching_array, cpages);
					goto out;
				}
				cpages = 0;
			}
		}

		TAILQ_INSERT_HEAD(pages, p, plinks.q);
	}

	if (cpages) {
		r = ttm_set_pages_caching(caching_array, cstate, cpages);
		if (r)
			ttm_handle_caching_state_failure(pages,
					ttm_flags, cstate,
					caching_array, cpages);
	}
out:
	free(caching_array, M_TEMP);

	return r;
}

/**
 * Fill the given pool if there aren't enough pages and the requested number of
 * pages is small.
 */
static void ttm_page_pool_fill_locked(struct ttm_page_pool *pool,
    int ttm_flags, enum ttm_caching_state cstate, unsigned count)
{
	vm_page_t p;
	int r;
	unsigned cpages = 0;
	/**
	 * Only allow one pool fill operation at a time.
	 * If pool doesn't have enough pages for the allocation new pages are
	 * allocated from outside of pool.
	 */
	if (pool->fill_lock)
		return;

	pool->fill_lock = true;

	/* If allocation request is small and there are not enough
	 * pages in a pool we fill the pool up first. */
	if (count < _manager->options.small
		&& count > pool->npages) {
		struct pglist new_pages;
		unsigned alloc_size = _manager->options.alloc_size;

		/**
		 * Can't change page caching if in irqsave context. We have to
		 * drop the pool->lock.
		 */
		mtx_unlock(&pool->lock);

		TAILQ_INIT(&new_pages);
		r = ttm_alloc_new_pages(&new_pages, pool->ttm_page_alloc_flags,
		    ttm_flags, cstate, alloc_size);
		mtx_lock(&pool->lock);

		if (!r) {
			TAILQ_CONCAT(&pool->list, &new_pages, plinks.q);
			++pool->nrefills;
			pool->npages += alloc_size;
		} else {
			printf("[TTM] Failed to fill pool (%p)\n", pool);
			/* If we have any pages left put them to the pool. */
			TAILQ_FOREACH(p, &pool->list, plinks.q) {
				++cpages;
			}
			TAILQ_CONCAT(&pool->list, &new_pages, plinks.q);
			pool->npages += cpages;
		}

	}
	pool->fill_lock = false;
}

/**
 * Cut 'count' number of pages from the pool and put them on the return list.
 *
 * @return count of pages still required to fulfill the request.
 */
static unsigned ttm_page_pool_get_pages(struct ttm_page_pool *pool,
					struct pglist *pages,
					int ttm_flags,
					enum ttm_caching_state cstate,
					unsigned count)
{
	vm_page_t p;
	unsigned i;

	mtx_lock(&pool->lock);
	ttm_page_pool_fill_locked(pool, ttm_flags, cstate, count);

	if (count >= pool->npages) {
		/* take all pages from the pool */
		TAILQ_CONCAT(pages, &pool->list, plinks.q);
		count -= pool->npages;
		pool->npages = 0;
		goto out;
	}
	for (i = 0; i < count; i++) {
		p = TAILQ_FIRST(&pool->list);
		TAILQ_REMOVE(&pool->list, p, plinks.q);
		TAILQ_INSERT_TAIL(pages, p, plinks.q);
	}
	pool->npages -= count;
	count = 0;
out:
	mtx_unlock(&pool->lock);
	return count;
}

/* Put all pages in pages list to correct pool to wait for reuse */
static void ttm_put_pages(vm_page_t *pages, unsigned npages, int flags,
			  enum ttm_caching_state cstate)
{
	struct ttm_page_pool *pool = ttm_get_pool(flags, cstate);
	unsigned i;

	if (pool == NULL) {
		/* No pool for this memory type so free the pages */
		for (i = 0; i < npages; i++) {
			if (pages[i]) {
				ttm_vm_page_free(pages[i]);
				pages[i] = NULL;
			}
		}
		return;
	}

	mtx_lock(&pool->lock);
	for (i = 0; i < npages; i++) {
		if (pages[i]) {
			TAILQ_INSERT_TAIL(&pool->list, pages[i], plinks.q);
			pages[i] = NULL;
			pool->npages++;
		}
	}
	/* Check that we don't go over the pool limit */
	npages = 0;
	if (pool->npages > _manager->options.max_size) {
		npages = pool->npages - _manager->options.max_size;
		/* free at least NUM_PAGES_TO_ALLOC number of pages
		 * to reduce calls to set_memory_wb */
		if (npages < NUM_PAGES_TO_ALLOC)
			npages = NUM_PAGES_TO_ALLOC;
	}
	mtx_unlock(&pool->lock);
	if (npages)
		ttm_page_pool_free(pool, npages);
}

/*
 * On success pages list will hold count number of correctly
 * cached pages.
 */
static int ttm_get_pages(vm_page_t *pages, unsigned npages, int flags,
			 enum ttm_caching_state cstate)
{
	struct ttm_page_pool *pool = ttm_get_pool(flags, cstate);
	struct pglist plist;
	vm_page_t p = NULL;
	int gfp_flags;
	unsigned count;
	int r;

	/* No pool for cached pages */
	if (pool == NULL) {
		for (r = 0; r < npages; ++r) {
			p = ttm_vm_page_alloc(flags, cstate);
			if (!p) {
				printf("[TTM] Unable to allocate page\n");
				return -ENOMEM;
			}
			pages[r] = p;
		}
		return 0;
	}

	/* combine zero flag to pool flags */
	gfp_flags = flags | pool->ttm_page_alloc_flags;

	/* First we take pages from the pool */
	TAILQ_INIT(&plist);
	npages = ttm_page_pool_get_pages(pool, &plist, flags, cstate, npages);
	count = 0;
	TAILQ_FOREACH(p, &plist, plinks.q) {
		pages[count++] = p;
	}

	/* clear the pages coming from the pool if requested */
	if (flags & TTM_PAGE_FLAG_ZERO_ALLOC) {
		TAILQ_FOREACH(p, &plist, plinks.q) {
			pmap_zero_page(p);
		}
	}

	/* If pool didn't have enough pages allocate new one. */
	if (npages > 0) {
		/* ttm_alloc_new_pages doesn't reference pool so we can run
		 * multiple requests in parallel.
		 **/
		TAILQ_INIT(&plist);
		r = ttm_alloc_new_pages(&plist, gfp_flags, flags, cstate,
		    npages);
		TAILQ_FOREACH(p, &plist, plinks.q) {
			pages[count++] = p;
		}
		if (r) {
			/* If there is any pages in the list put them back to
			 * the pool. */
			printf("[TTM] Failed to allocate extra pages for large request\n");
			ttm_put_pages(pages, count, flags, cstate);
			return r;
		}
	}

	return 0;
}

static void ttm_page_pool_init_locked(struct ttm_page_pool *pool, int flags,
				      char *name)
{
	mtx_init(&pool->lock, "ttmpool", NULL, MTX_DEF);
	pool->fill_lock = false;
	TAILQ_INIT(&pool->list);
	pool->npages = pool->nfrees = 0;
	pool->ttm_page_alloc_flags = flags;
	pool->name = name;
}

int ttm_page_alloc_init(struct ttm_mem_global *glob, unsigned max_pages)
{

	if (_manager != NULL)
		printf("[TTM] manager != NULL\n");
	printf("[TTM] Initializing pool allocator\n");

	_manager = malloc(sizeof(*_manager), M_TTM_POOLMGR, M_WAITOK | M_ZERO);

	ttm_page_pool_init_locked(&_manager->wc_pool, 0, "wc");
	ttm_page_pool_init_locked(&_manager->uc_pool, 0, "uc");
	ttm_page_pool_init_locked(&_manager->wc_pool_dma32,
	    TTM_PAGE_FLAG_DMA32, "wc dma");
	ttm_page_pool_init_locked(&_manager->uc_pool_dma32,
	    TTM_PAGE_FLAG_DMA32, "uc dma");

	_manager->options.max_size = max_pages;
	_manager->options.small = SMALL_ALLOCATION;
	_manager->options.alloc_size = NUM_PAGES_TO_ALLOC;

	refcount_init(&_manager->kobj_ref, 1);
	ttm_pool_mm_shrink_init(_manager);

	return 0;
}

void ttm_page_alloc_fini(void)
{
	int i;

	printf("[TTM] Finalizing pool allocator\n");
	ttm_pool_mm_shrink_fini(_manager);

	for (i = 0; i < NUM_POOLS; ++i)
		ttm_page_pool_free(&_manager->pools[i], FREE_ALL_PAGES);

	if (refcount_release(&_manager->kobj_ref))
		ttm_pool_kobj_release(_manager);
	_manager = NULL;
}

int ttm_pool_populate(struct ttm_tt *ttm)
{
	struct ttm_mem_global *mem_glob = ttm->glob->mem_glob;
	unsigned i;
	int ret;

	if (ttm->state != tt_unpopulated)
		return 0;

	for (i = 0; i < ttm->num_pages; ++i) {
		ret = ttm_get_pages(&ttm->pages[i], 1,
				    ttm->page_flags,
				    ttm->caching_state);
		if (ret != 0) {
			ttm_pool_unpopulate(ttm);
			return -ENOMEM;
		}

		ret = ttm_mem_global_alloc_page(mem_glob, ttm->pages[i],
						false, false);
		if (unlikely(ret != 0)) {
			ttm_pool_unpopulate(ttm);
			return -ENOMEM;
		}
	}

	if (unlikely(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
		ret = ttm_tt_swapin(ttm);
		if (unlikely(ret != 0)) {
			ttm_pool_unpopulate(ttm);
			return ret;
		}
	}

	ttm->state = tt_unbound;
	return 0;
}

void ttm_pool_unpopulate(struct ttm_tt *ttm)
{
	unsigned i;

	for (i = 0; i < ttm->num_pages; ++i) {
		if (ttm->pages[i]) {
			ttm_mem_global_free_page(ttm->glob->mem_glob,
						 ttm->pages[i]);
			ttm_put_pages(&ttm->pages[i], 1,
				      ttm->page_flags,
				      ttm->caching_state);
		}
	}
	ttm->state = tt_unpopulated;
}

#if 0
/* XXXKIB sysctl */
int ttm_page_alloc_debugfs(struct seq_file *m, void *data)
{
	struct ttm_page_pool *p;
	unsigned i;
	char *h[] = {"pool", "refills", "pages freed", "size"};
	if (!_manager) {
		seq_printf(m, "No pool allocator running.\n");
		return 0;
	}
	seq_printf(m, "%6s %12s %13s %8s\n",
			h[0], h[1], h[2], h[3]);
	for (i = 0; i < NUM_POOLS; ++i) {
		p = &_manager->pools[i];

		seq_printf(m, "%6s %12ld %13ld %8d\n",
				p->name, p->nrefills,
				p->nfrees, p->npages);
	}
	return 0;
}
#endif
