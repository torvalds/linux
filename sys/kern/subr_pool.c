/*	$OpenBSD: subr_pool.c,v 1.242 2025/08/01 19:00:38 cludwig Exp $	*/
/*	$NetBSD: subr_pool.c,v 1.61 2001/09/26 07:14:56 chs Exp $	*/

/*-
 * Copyright (c) 1997, 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg; by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/task.h>
#include <sys/time.h>
#include <sys/timeout.h>
#include <sys/percpu.h>
#include <sys/tracepoint.h>

#include <uvm/uvm_extern.h>

/*
 * Pool resource management utility.
 *
 * Memory is allocated in pages which are split into pieces according to
 * the pool item size. Each page is kept on one of three lists in the
 * pool structure: `pr_emptypages', `pr_fullpages' and `pr_partpages',
 * for empty, full and partially-full pages respectively. The individual
 * pool items are on a linked list headed by `ph_items' in each page
 * header. The memory for building the page list is either taken from
 * the allocated pages themselves (for small pool items) or taken from
 * an internal pool of page headers (`phpool').
 */

/* List of all pools */
SIMPLEQ_HEAD(,pool) pool_head = SIMPLEQ_HEAD_INITIALIZER(pool_head);

/*
 * Every pool gets a unique serial number assigned to it. If this counter
 * wraps, we're screwed, but we shouldn't create so many pools anyway.
 */
unsigned int pool_serial;
unsigned int pool_count;

/* Lock the previous variables making up the global pool state */
struct rwlock pool_lock = RWLOCK_INITIALIZER("pools");

/* Private pool for page header structures */
struct pool phpool;

struct pool_lock_ops {
	void	(*pl_init)(struct pool *, union pool_lock *,
		    const struct lock_type *);
	void	(*pl_enter)(union pool_lock *);
	int	(*pl_enter_try)(union pool_lock *);
	void	(*pl_leave)(union pool_lock *);
	void	(*pl_assert_locked)(union pool_lock *);
	void	(*pl_assert_unlocked)(union pool_lock *);
	int	(*pl_sleep)(void *, union pool_lock *, int, const char *);
};

static const struct pool_lock_ops pool_lock_ops_mtx;
static const struct pool_lock_ops pool_lock_ops_rw;

#ifdef WITNESS
#define pl_init(pp, pl) do {						\
	static const struct lock_type __lock_type = { .lt_name = #pl };	\
	(pp)->pr_lock_ops->pl_init(pp, pl, &__lock_type);		\
} while (0)
#else /* WITNESS */
#define pl_init(pp, pl)		(pp)->pr_lock_ops->pl_init(pp, pl, NULL)
#endif /* WITNESS */

static inline void
pl_enter(struct pool *pp, union pool_lock *pl)
{
	pp->pr_lock_ops->pl_enter(pl);
}
static inline int
pl_enter_try(struct pool *pp, union pool_lock *pl)
{
	return pp->pr_lock_ops->pl_enter_try(pl);
}
static inline void
pl_leave(struct pool *pp, union pool_lock *pl)
{
	pp->pr_lock_ops->pl_leave(pl);
}
static inline void
pl_assert_locked(struct pool *pp, union pool_lock *pl)
{
	pp->pr_lock_ops->pl_assert_locked(pl);
}
static inline void
pl_assert_unlocked(struct pool *pp, union pool_lock *pl)
{
	pp->pr_lock_ops->pl_assert_unlocked(pl);
}
static inline int
pl_sleep(struct pool *pp, void *ident, union pool_lock *lock, int priority,
    const char *wmesg)
{
	return pp->pr_lock_ops->pl_sleep(ident, lock, priority, wmesg);
}

struct pool_item {
	u_long				pi_magic;
	XSIMPLEQ_ENTRY(pool_item)	pi_list;
};
#define POOL_IMAGIC(ph, pi) ((u_long)(pi) ^ (ph)->ph_magic)

struct pool_page_header {
	/* Page headers */
	TAILQ_ENTRY(pool_page_header)
				ph_entry;	/* pool page list */
	XSIMPLEQ_HEAD(, pool_item)
				ph_items;	/* free items on the page */
	RBT_ENTRY(pool_page_header)
				ph_node;	/* off-page page headers */
	unsigned int		ph_nmissing;	/* # of chunks in use */
	caddr_t			ph_page;	/* this page's address */
	caddr_t			ph_colored;	/* page's colored address */
	unsigned long		ph_magic;
	uint64_t		ph_timestamp;
};
#define POOL_MAGICBIT (1 << 3) /* keep away from perturbed low bits */
#define POOL_PHPOISON(ph) ISSET((ph)->ph_magic, POOL_MAGICBIT)

#ifdef MULTIPROCESSOR
struct pool_cache_item {
	struct pool_cache_item	*ci_next;	/* next item in list */
	unsigned long		 ci_nitems;	/* number of items in list */
	TAILQ_ENTRY(pool_cache_item)
				 ci_nextl;	/* entry in list of lists */
};

/* we store whether the cached item is poisoned in the high bit of nitems */
#define POOL_CACHE_ITEM_NITEMS_MASK	0x7ffffffUL
#define POOL_CACHE_ITEM_NITEMS_POISON	0x8000000UL

#define POOL_CACHE_ITEM_NITEMS(_ci)					\
    ((_ci)->ci_nitems & POOL_CACHE_ITEM_NITEMS_MASK)

#define POOL_CACHE_ITEM_POISONED(_ci)					\
    ISSET((_ci)->ci_nitems, POOL_CACHE_ITEM_NITEMS_POISON)

struct pool_cache {
	struct pool_cache_item	*pc_actv;	/* active list of items */
	unsigned long		 pc_nactv;	/* actv head nitems cache */
	struct pool_cache_item	*pc_prev;	/* previous list of items */

	uint64_t		 pc_gen;	/* generation number */
	uint64_t		 pc_nget;	/* # of successful requests */
	uint64_t		 pc_nfail;	/* # of unsuccessful reqs */
	uint64_t		 pc_nput;	/* # of releases */
	uint64_t		 pc_nlget;	/* # of list requests */
	uint64_t		 pc_nlfail;	/* # of fails getting a list */
	uint64_t		 pc_nlput;	/* # of list releases */

	int			 pc_nout;
};

void	*pool_cache_get(struct pool *);
void	 pool_cache_put(struct pool *, void *);
void	 pool_cache_destroy(struct pool *);
void	 pool_cache_gc(struct pool *);
#endif
void	 pool_cache_pool_info(struct pool *, struct kinfo_pool *);
int	 pool_cache_info(struct pool *, void *, size_t *);
int	 pool_cache_cpus_info(struct pool *, void *, size_t *);

#ifdef POOL_DEBUG
int	pool_debug = 1;
#else
int	pool_debug = 0;
#endif

#define POOL_INPGHDR(pp) ((pp)->pr_phoffset != 0)

struct pool_page_header *
	 pool_p_alloc(struct pool *, int, int *);
void	 pool_p_insert(struct pool *, struct pool_page_header *);
void	 pool_p_remove(struct pool *, struct pool_page_header *);
void	 pool_p_free(struct pool *, struct pool_page_header *);

void	 pool_update_curpage(struct pool *);
void	*pool_do_get(struct pool *, int, int *);
void	 pool_do_put(struct pool *, void *);
int	 pool_chk_page(struct pool *, struct pool_page_header *, int);
int	 pool_chk(struct pool *);
void	 pool_get_done(struct pool *, void *, void *);
void	 pool_runqueue(struct pool *, int);

void	*pool_allocator_alloc(struct pool *, int, int *);
void	 pool_allocator_free(struct pool *, void *);

/*
 * The default pool allocator.
 */
void	*pool_page_alloc(struct pool *, int, int *);
void	pool_page_free(struct pool *, void *);

/*
 * safe for interrupts; this is the default allocator
 */
struct pool_allocator pool_allocator_single = {
	pool_page_alloc,
	pool_page_free,
	POOL_ALLOC_SIZE(PAGE_SIZE, POOL_ALLOC_ALIGNED)
};

void	*pool_multi_alloc(struct pool *, int, int *);
void	pool_multi_free(struct pool *, void *);

struct pool_allocator pool_allocator_multi = {
	pool_multi_alloc,
	pool_multi_free,
	POOL_ALLOC_SIZES(PAGE_SIZE, (1UL << 31), POOL_ALLOC_ALIGNED)
};

void	*pool_multi_alloc_ni(struct pool *, int, int *);
void	pool_multi_free_ni(struct pool *, void *);

struct pool_allocator pool_allocator_multi_ni = {
	pool_multi_alloc_ni,
	pool_multi_free_ni,
	POOL_ALLOC_SIZES(PAGE_SIZE, (1UL << 31), POOL_ALLOC_ALIGNED)
};

#ifdef DDB
void	 pool_print_pagelist(struct pool_pagelist *, int (*)(const char *, ...)
	     __attribute__((__format__(__kprintf__,1,2))));
void	 pool_print1(struct pool *, const char *, int (*)(const char *, ...)
	     __attribute__((__format__(__kprintf__,1,2))));
#endif

/* stale page garbage collectors */
void	pool_gc_sched(void *);
struct timeout pool_gc_tick = TIMEOUT_INITIALIZER(pool_gc_sched, NULL);
void	pool_gc_pages(void *);
struct task pool_gc_task = TASK_INITIALIZER(pool_gc_pages, NULL);

#define POOL_WAIT_FREE	SEC_TO_NSEC(1)
#define POOL_WAIT_GC	SEC_TO_NSEC(8)

RBT_PROTOTYPE(phtree, pool_page_header, ph_node, phtree_compare);

static inline int
phtree_compare(const struct pool_page_header *a,
    const struct pool_page_header *b)
{
	vaddr_t va = (vaddr_t)a->ph_page;
	vaddr_t vb = (vaddr_t)b->ph_page;

	/* the compares in this order are important for the NFIND to work */
	if (vb < va)
		return (-1);
	if (vb > va)
		return (1);

	return (0);
}

RBT_GENERATE(phtree, pool_page_header, ph_node, phtree_compare);

/*
 * Return the pool page header based on page address.
 */
static inline struct pool_page_header *
pr_find_pagehead(struct pool *pp, void *v)
{
	struct pool_page_header *ph, key;

	if (POOL_INPGHDR(pp)) {
		caddr_t page;

		page = (caddr_t)((vaddr_t)v & pp->pr_pgmask);

		return ((struct pool_page_header *)(page + pp->pr_phoffset));
	}

	key.ph_page = v;
	ph = RBT_NFIND(phtree, &pp->pr_phtree, &key);
	if (ph == NULL)
		panic("%s: %s: page header missing", __func__, pp->pr_wchan);

	KASSERT(ph->ph_page <= (caddr_t)v);
	if (ph->ph_page + pp->pr_pgsize <= (caddr_t)v)
		panic("%s: %s: incorrect page", __func__, pp->pr_wchan);

	return (ph);
}

/*
 * Initialize the given pool resource structure.
 *
 * We export this routine to allow other kernel parts to declare
 * static pools that must be initialized before malloc() is available.
 */
void
pool_init(struct pool *pp, size_t size, u_int align, int ipl, int flags,
    const char *wchan, struct pool_allocator *palloc)
{
	int off = 0, space;
	unsigned int pgsize = PAGE_SIZE, items;
	size_t pa_pagesz;
#ifdef DIAGNOSTIC
	struct pool *iter;
#endif

	if (align == 0)
		align = ALIGN(1);

	if (size < sizeof(struct pool_item))
		size = sizeof(struct pool_item);

	size = roundup(size, align);

	while (size * 8 > pgsize)
		pgsize <<= 1;

	if (palloc == NULL) {
		if (pgsize > PAGE_SIZE) {
			palloc = ISSET(flags, PR_WAITOK) ?
			    &pool_allocator_multi_ni : &pool_allocator_multi;
		} else
			palloc = &pool_allocator_single;

		pa_pagesz = palloc->pa_pagesz;
	} else {
		size_t pgsizes;

		pa_pagesz = palloc->pa_pagesz;
		if (pa_pagesz == 0)
			pa_pagesz = POOL_ALLOC_DEFAULT;

		pgsizes = pa_pagesz & ~POOL_ALLOC_ALIGNED;

		/* make sure the allocator can fit at least one item */
		if (size > pgsizes) {
			panic("%s: pool %s item size 0x%zx > "
			    "allocator %p sizes 0x%zx", __func__, wchan,
			    size, palloc, pgsizes);
		}

		/* shrink pgsize until it fits into the range */
		while (!ISSET(pgsizes, pgsize))
			pgsize >>= 1;
	}
	KASSERT(ISSET(pa_pagesz, pgsize));

	items = pgsize / size;

	/*
	 * Decide whether to put the page header off page to avoid
	 * wasting too large a part of the page. Off-page page headers
	 * go into an RB tree, so we can match a returned item with
	 * its header based on the page address.
	 */
	if (ISSET(pa_pagesz, POOL_ALLOC_ALIGNED)) {
		if (pgsize - (size * items) >
		    sizeof(struct pool_page_header)) {
			off = pgsize - sizeof(struct pool_page_header);
		} else if (sizeof(struct pool_page_header) * 2 >= size) {
			off = pgsize - sizeof(struct pool_page_header);
			items = off / size;
		}
	}

	KASSERT(items > 0);

	/*
	 * Initialize the pool structure.
	 */
	memset(pp, 0, sizeof(*pp));
	refcnt_init(&pp->pr_refcnt);
	if (ISSET(flags, PR_RWLOCK)) {
		KASSERT(flags & PR_WAITOK);
		pp->pr_lock_ops = &pool_lock_ops_rw;
	} else
		pp->pr_lock_ops = &pool_lock_ops_mtx;
	TAILQ_INIT(&pp->pr_emptypages);
	TAILQ_INIT(&pp->pr_fullpages);
	TAILQ_INIT(&pp->pr_partpages);
	pp->pr_curpage = NULL;
	pp->pr_npages = 0;
	pp->pr_minitems = 0;
	pp->pr_minpages = 0;
	pp->pr_maxpages = 8;
	pp->pr_size = size;
	pp->pr_pgsize = pgsize;
	pp->pr_pgmask = ~0UL ^ (pgsize - 1);
	pp->pr_phoffset = off;
	pp->pr_itemsperpage = items;
	pp->pr_wchan = wchan;
	pp->pr_alloc = palloc;
	pp->pr_nitems = 0;
	pp->pr_nout = 0;
	pp->pr_hardlimit = UINT_MAX;
	RBT_INIT(phtree, &pp->pr_phtree);

	/*
	 * Use the space between the chunks and the page header
	 * for cache coloring.
	 */
	space = POOL_INPGHDR(pp) ? pp->pr_phoffset : pp->pr_pgsize;
	space -= pp->pr_itemsperpage * pp->pr_size;
	pp->pr_align = align;
	pp->pr_maxcolors = (space / align) + 1;

	pp->pr_nget = 0;
	pp->pr_nfail = 0;
	pp->pr_nput = 0;
	pp->pr_npagealloc = 0;
	pp->pr_npagefree = 0;
	pp->pr_hiwat = 0;
	pp->pr_nidle = 0;

	pp->pr_ipl = ipl;
	pp->pr_flags = flags;

	pl_init(pp, &pp->pr_lock);
	pl_init(pp, &pp->pr_requests_lock);
	TAILQ_INIT(&pp->pr_requests);

	if (phpool.pr_size == 0) {
		pool_init(&phpool, sizeof(struct pool_page_header), 0,
		    IPL_HIGH, 0, "phpool", NULL);

		/* make sure phpool won't "recurse" */
		KASSERT(POOL_INPGHDR(&phpool));
	}

	/* pglistalloc/constraint parameters */
	pp->pr_crange = &kp_dirty;

	/* Insert this into the list of all pools. */
	rw_enter_write(&pool_lock);
#ifdef DIAGNOSTIC
	SIMPLEQ_FOREACH(iter, &pool_head, pr_poollist) {
		if (iter == pp)
			panic("%s: pool %s already on list", __func__, wchan);
	}
#endif

	pp->pr_serial = ++pool_serial;
	if (pool_serial == 0)
		panic("%s: too much uptime", __func__);

	SIMPLEQ_INSERT_HEAD(&pool_head, pp, pr_poollist);
	pool_count++;
	rw_exit_write(&pool_lock);
}

/*
 * Decommission a pool resource.
 */
void
pool_destroy(struct pool *pp)
{
	struct pool_page_header *ph;
	struct pool *prev, *iter;

#ifdef DIAGNOSTIC
	if (pp->pr_nout != 0)
		panic("%s: pool busy: still out: %u", __func__, pp->pr_nout);
#endif

	/* Remove from global pool list */
	rw_enter_write(&pool_lock);
	pool_count--;
	if (pp == SIMPLEQ_FIRST(&pool_head))
		SIMPLEQ_REMOVE_HEAD(&pool_head, pr_poollist);
	else {
		prev = SIMPLEQ_FIRST(&pool_head);
		SIMPLEQ_FOREACH(iter, &pool_head, pr_poollist) {
			if (iter == pp) {
				SIMPLEQ_REMOVE_AFTER(&pool_head, prev,
				    pr_poollist);
				break;
			}
			prev = iter;
		}
	}
	rw_exit_write(&pool_lock);

	/* Wait for concurrent sysctl_dopool() */
	refcnt_finalize(&pp->pr_refcnt, "pooldtor");

#ifdef MULTIPROCESSOR
	if (pp->pr_cache != NULL)
		pool_cache_destroy(pp);
#endif

	/* Remove all pages */
	while ((ph = TAILQ_FIRST(&pp->pr_emptypages)) != NULL) {
		pl_enter(pp, &pp->pr_lock);
		pool_p_remove(pp, ph);
		pl_leave(pp, &pp->pr_lock);
		pool_p_free(pp, ph);
	}
	KASSERT(TAILQ_EMPTY(&pp->pr_fullpages));
	KASSERT(TAILQ_EMPTY(&pp->pr_partpages));
}

void
pool_request_init(struct pool_request *pr,
    void (*handler)(struct pool *, void *, void *), void *cookie)
{
	pr->pr_handler = handler;
	pr->pr_cookie = cookie;
	pr->pr_item = NULL;
}

void
pool_request(struct pool *pp, struct pool_request *pr)
{
	pl_enter(pp, &pp->pr_requests_lock);
	TAILQ_INSERT_TAIL(&pp->pr_requests, pr, pr_entry);
	pool_runqueue(pp, PR_NOWAIT);
	pl_leave(pp, &pp->pr_requests_lock);
}

struct pool_get_memory {
	union pool_lock lock;
	void * volatile v;
};

/*
 * Grab an item from the pool.
 */
void *
pool_get(struct pool *pp, int flags)
{
	void *v = NULL;
	int slowdown = 0;

	if (flags & PR_WAITOK)
		assertwaitok();

	KASSERT(flags & (PR_WAITOK | PR_NOWAIT));
	if (pp->pr_flags & PR_RWLOCK)
		KASSERT(flags & PR_WAITOK);

#ifdef MULTIPROCESSOR
	if (pp->pr_cache != NULL) {
		v = pool_cache_get(pp);
		if (v != NULL)
			goto good;
	}
#endif

	pl_enter(pp, &pp->pr_lock);
	if (pp->pr_nout >= pp->pr_hardlimit) {
		if (ISSET(flags, PR_NOWAIT|PR_LIMITFAIL))
			goto fail;
	} else if ((v = pool_do_get(pp, flags, &slowdown)) == NULL) {
		if (ISSET(flags, PR_NOWAIT))
			goto fail;
	}
	pl_leave(pp, &pp->pr_lock);

	if ((slowdown || pool_debug == 2) && ISSET(flags, PR_WAITOK))
		yield();

	if (v == NULL) {
		struct pool_get_memory mem = { .v = NULL };
		struct pool_request pr;

#ifdef DIAGNOSTIC
		if (ISSET(flags, PR_WAITOK) && curproc == &proc0)
			panic("%s: cannot sleep for memory during boot",
			    __func__);
#endif
		pl_init(pp, &mem.lock);
		pool_request_init(&pr, pool_get_done, &mem);
		pool_request(pp, &pr);

		pl_enter(pp, &mem.lock);
		while (mem.v == NULL)
			pl_sleep(pp, &mem, &mem.lock, PSWP, pp->pr_wchan);
		pl_leave(pp, &mem.lock);

		v = mem.v;
	}

#ifdef MULTIPROCESSOR
good:
#endif
	if (ISSET(flags, PR_ZERO))
		memset(v, 0, pp->pr_size);

	TRACEPOINT(uvm, pool_get, pp, v, flags);

	return (v);

fail:
	pp->pr_nfail++;
	pl_leave(pp, &pp->pr_lock);
	return (NULL);
}

void
pool_get_done(struct pool *pp, void *xmem, void *v)
{
	struct pool_get_memory *mem = xmem;

	pl_enter(pp, &mem->lock);
	mem->v = v;
	pl_leave(pp, &mem->lock);

	wakeup_one(mem);
}

void
pool_runqueue(struct pool *pp, int flags)
{
	struct pool_requests prl = TAILQ_HEAD_INITIALIZER(prl);
	struct pool_request *pr;

	pl_assert_unlocked(pp, &pp->pr_lock);
	pl_assert_locked(pp, &pp->pr_requests_lock);

	if (pp->pr_requesting++)
		return;

	do {
		pp->pr_requesting = 1;

		TAILQ_CONCAT(&prl, &pp->pr_requests, pr_entry);
		if (TAILQ_EMPTY(&prl))
			continue;

		pl_leave(pp, &pp->pr_requests_lock);

		pl_enter(pp, &pp->pr_lock);
		pr = TAILQ_FIRST(&prl);
		while (pr != NULL) {
			int slowdown = 0;

			if (pp->pr_nout >= pp->pr_hardlimit)
				break;

			pr->pr_item = pool_do_get(pp, flags, &slowdown);
			if (pr->pr_item == NULL) /* || slowdown ? */
				break;

			pr = TAILQ_NEXT(pr, pr_entry);
		}
		pl_leave(pp, &pp->pr_lock);

		while ((pr = TAILQ_FIRST(&prl)) != NULL &&
		    pr->pr_item != NULL) {
			TAILQ_REMOVE(&prl, pr, pr_entry);
			(*pr->pr_handler)(pp, pr->pr_cookie, pr->pr_item);
		}

		pl_enter(pp, &pp->pr_requests_lock);
	} while (--pp->pr_requesting);

	TAILQ_CONCAT(&pp->pr_requests, &prl, pr_entry);
}

void *
pool_do_get(struct pool *pp, int flags, int *slowdown)
{
	struct pool_item *pi;
	struct pool_page_header *ph;

	pl_assert_locked(pp, &pp->pr_lock);

	splassert(pp->pr_ipl);

	/*
	 * Account for this item now to avoid races if we need to give up
	 * pr_lock to allocate a page.
	 */
	pp->pr_nout++;

	if (pp->pr_curpage == NULL) {
		pl_leave(pp, &pp->pr_lock);
		ph = pool_p_alloc(pp, flags, slowdown);
		pl_enter(pp, &pp->pr_lock);

		if (ph == NULL) {
			pp->pr_nout--;
			return (NULL);
		}

		pool_p_insert(pp, ph);
	}

	ph = pp->pr_curpage;
	pi = XSIMPLEQ_FIRST(&ph->ph_items);
	if (__predict_false(pi == NULL))
		panic("%s: %s: page empty", __func__, pp->pr_wchan);

	if (__predict_false(pi->pi_magic != POOL_IMAGIC(ph, pi))) {
		panic("%s: %s free list modified: "
		    "page %p; item addr %p; offset 0x%x=0x%lx != 0x%lx",
		    __func__, pp->pr_wchan, ph->ph_page, pi,
		    0, pi->pi_magic, POOL_IMAGIC(ph, pi));
	}

	XSIMPLEQ_REMOVE_HEAD(&ph->ph_items, pi_list);

#ifdef DIAGNOSTIC
	if (pool_debug && POOL_PHPOISON(ph)) {
		size_t pidx;
		uint32_t pval;
		if (poison_check(pi + 1, pp->pr_size - sizeof(*pi),
		    &pidx, &pval)) {
			int *ip = (int *)(pi + 1);
			panic("%s: %s free list modified: "
			    "page %p; item addr %p; offset 0x%zx=0x%x",
			    __func__, pp->pr_wchan, ph->ph_page, pi,
			    (pidx * sizeof(int)) + sizeof(*pi), ip[pidx]);
		}
	}
#endif /* DIAGNOSTIC */

	if (ph->ph_nmissing++ == 0) {
		/*
		 * This page was previously empty.  Move it to the list of
		 * partially-full pages.  This page is already curpage.
		 */
		TAILQ_REMOVE(&pp->pr_emptypages, ph, ph_entry);
		TAILQ_INSERT_TAIL(&pp->pr_partpages, ph, ph_entry);

		pp->pr_nidle--;
	}

	if (ph->ph_nmissing == pp->pr_itemsperpage) {
		/*
		 * This page is now full.  Move it to the full list
		 * and select a new current page.
		 */
		TAILQ_REMOVE(&pp->pr_partpages, ph, ph_entry);
		TAILQ_INSERT_TAIL(&pp->pr_fullpages, ph, ph_entry);
		pool_update_curpage(pp);
	}

	pp->pr_nget++;

	return (pi);
}

/*
 * Return resource to the pool.
 */
void
pool_put(struct pool *pp, void *v)
{
	struct pool_page_header *ph, *freeph = NULL;

#ifdef DIAGNOSTIC
	if (v == NULL)
		panic("%s: NULL item", __func__);
#endif

	TRACEPOINT(uvm, pool_put, pp, v);

#ifdef MULTIPROCESSOR
	if (pp->pr_cache != NULL && TAILQ_EMPTY(&pp->pr_requests)) {
		pool_cache_put(pp, v);
		return;
	}
#endif

	pl_enter(pp, &pp->pr_lock);

	pool_do_put(pp, v);

	pp->pr_nout--;
	pp->pr_nput++;

	/* is it time to free a page? */
	if (pp->pr_nidle > pp->pr_maxpages &&
	    (ph = TAILQ_FIRST(&pp->pr_emptypages)) != NULL &&
	    getnsecuptime() - ph->ph_timestamp > POOL_WAIT_FREE) {
		freeph = ph;
		pool_p_remove(pp, freeph);
	}

	pl_leave(pp, &pp->pr_lock);

	if (freeph != NULL)
		pool_p_free(pp, freeph);

	pool_wakeup(pp);
}

void
pool_wakeup(struct pool *pp)
{
	if (!TAILQ_EMPTY(&pp->pr_requests)) {
		pl_enter(pp, &pp->pr_requests_lock);
		pool_runqueue(pp, PR_NOWAIT);
		pl_leave(pp, &pp->pr_requests_lock);
	}
}

void
pool_do_put(struct pool *pp, void *v)
{
	struct pool_item *pi = v;
	struct pool_page_header *ph;

	splassert(pp->pr_ipl);

	ph = pr_find_pagehead(pp, v);

#ifdef DIAGNOSTIC
	if (pool_debug) {
		struct pool_item *qi;
		XSIMPLEQ_FOREACH(qi, &ph->ph_items, pi_list) {
			if (pi == qi) {
				panic("%s: %s: double pool_put: %p", __func__,
				    pp->pr_wchan, pi);
			}
		}
	}
#endif /* DIAGNOSTIC */

	pi->pi_magic = POOL_IMAGIC(ph, pi);
	XSIMPLEQ_INSERT_HEAD(&ph->ph_items, pi, pi_list);
#ifdef DIAGNOSTIC
	if (POOL_PHPOISON(ph))
		poison_mem(pi + 1, pp->pr_size - sizeof(*pi));
#endif /* DIAGNOSTIC */

	if (ph->ph_nmissing-- == pp->pr_itemsperpage) {
		/*
		 * The page was previously completely full, move it to the
		 * partially-full list.
		 */
		TAILQ_REMOVE(&pp->pr_fullpages, ph, ph_entry);
		TAILQ_INSERT_TAIL(&pp->pr_partpages, ph, ph_entry);
	}

	if (ph->ph_nmissing == 0) {
		/*
		 * The page is now empty, so move it to the empty page list.
		 */
		pp->pr_nidle++;

		ph->ph_timestamp = getnsecuptime();
		TAILQ_REMOVE(&pp->pr_partpages, ph, ph_entry);
		TAILQ_INSERT_TAIL(&pp->pr_emptypages, ph, ph_entry);
		pool_update_curpage(pp);
	}
}

/*
 * Add N items to the pool.
 */
int
pool_prime(struct pool *pp, int n)
{
	struct pool_pagelist pl = TAILQ_HEAD_INITIALIZER(pl);
	struct pool_page_header *ph;
	int newpages;

	newpages = roundup(n, pp->pr_itemsperpage) / pp->pr_itemsperpage;

	while (newpages-- > 0) {
		int slowdown = 0;

		ph = pool_p_alloc(pp, PR_NOWAIT, &slowdown);
		if (ph == NULL) /* or slowdown? */
			break;

		TAILQ_INSERT_TAIL(&pl, ph, ph_entry);
	}

	pl_enter(pp, &pp->pr_lock);
	while ((ph = TAILQ_FIRST(&pl)) != NULL) {
		TAILQ_REMOVE(&pl, ph, ph_entry);
		pool_p_insert(pp, ph);
	}
	pl_leave(pp, &pp->pr_lock);

	return (0);
}

struct pool_page_header *
pool_p_alloc(struct pool *pp, int flags, int *slowdown)
{
	struct pool_page_header *ph;
	struct pool_item *pi;
	caddr_t addr;
	unsigned int order;
	int o;
	int n;

	pl_assert_unlocked(pp, &pp->pr_lock);
	KASSERT(pp->pr_size >= sizeof(*pi));

	addr = pool_allocator_alloc(pp, flags, slowdown);
	if (addr == NULL)
		return (NULL);

	if (POOL_INPGHDR(pp))
		ph = (struct pool_page_header *)(addr + pp->pr_phoffset);
	else {
		ph = pool_get(&phpool, flags);
		if (ph == NULL) {
			pool_allocator_free(pp, addr);
			return (NULL);
		}
	}

	XSIMPLEQ_INIT(&ph->ph_items);
	ph->ph_page = addr;
	addr += pp->pr_align * (pp->pr_npagealloc % pp->pr_maxcolors);
	ph->ph_colored = addr;
	ph->ph_nmissing = 0;
	arc4random_buf(&ph->ph_magic, sizeof(ph->ph_magic));
#ifdef DIAGNOSTIC
	/* use a bit in ph_magic to record if we poison page items */
	if (pool_debug)
		SET(ph->ph_magic, POOL_MAGICBIT);
	else
		CLR(ph->ph_magic, POOL_MAGICBIT);
#endif /* DIAGNOSTIC */

	n = pp->pr_itemsperpage;
	o = 32;
	while (n--) {
		pi = (struct pool_item *)addr;
		pi->pi_magic = POOL_IMAGIC(ph, pi);

		if (o == 32) {
			order = arc4random();
			o = 0;
		}
		if (ISSET(order, 1U << o++))
			XSIMPLEQ_INSERT_TAIL(&ph->ph_items, pi, pi_list);
		else
			XSIMPLEQ_INSERT_HEAD(&ph->ph_items, pi, pi_list);

#ifdef DIAGNOSTIC
		if (POOL_PHPOISON(ph))
			poison_mem(pi + 1, pp->pr_size - sizeof(*pi));
#endif /* DIAGNOSTIC */

		addr += pp->pr_size;
	}

	return (ph);
}

void
pool_p_free(struct pool *pp, struct pool_page_header *ph)
{
	struct pool_item *pi;

	pl_assert_unlocked(pp, &pp->pr_lock);
	KASSERT(ph->ph_nmissing == 0);

	XSIMPLEQ_FOREACH(pi, &ph->ph_items, pi_list) {
		if (__predict_false(pi->pi_magic != POOL_IMAGIC(ph, pi))) {
			panic("%s: %s free list modified: "
			    "page %p; item addr %p; offset 0x%x=0x%lx",
			    __func__, pp->pr_wchan, ph->ph_page, pi,
			    0, pi->pi_magic);
		}

#ifdef DIAGNOSTIC
		if (POOL_PHPOISON(ph)) {
			size_t pidx;
			uint32_t pval;
			if (poison_check(pi + 1, pp->pr_size - sizeof(*pi),
			    &pidx, &pval)) {
				int *ip = (int *)(pi + 1);
				panic("%s: %s free list modified: "
				    "page %p; item addr %p; offset 0x%zx=0x%x",
				    __func__, pp->pr_wchan, ph->ph_page, pi,
				    pidx * sizeof(int), ip[pidx]);
			}
		}
#endif
	}

	pool_allocator_free(pp, ph->ph_page);

	if (!POOL_INPGHDR(pp))
		pool_put(&phpool, ph);
}

void
pool_p_insert(struct pool *pp, struct pool_page_header *ph)
{
	pl_assert_locked(pp, &pp->pr_lock);

	/* If the pool was depleted, point at the new page */
	if (pp->pr_curpage == NULL)
		pp->pr_curpage = ph;

	TAILQ_INSERT_TAIL(&pp->pr_emptypages, ph, ph_entry);
	if (!POOL_INPGHDR(pp))
		RBT_INSERT(phtree, &pp->pr_phtree, ph);

	pp->pr_nitems += pp->pr_itemsperpage;
	pp->pr_nidle++;

	pp->pr_npagealloc++;
	if (++pp->pr_npages > pp->pr_hiwat)
		pp->pr_hiwat = pp->pr_npages;
}

void
pool_p_remove(struct pool *pp, struct pool_page_header *ph)
{
	pl_assert_locked(pp, &pp->pr_lock);

	pp->pr_npagefree++;
	pp->pr_npages--;
	pp->pr_nidle--;
	pp->pr_nitems -= pp->pr_itemsperpage;

	if (!POOL_INPGHDR(pp))
		RBT_REMOVE(phtree, &pp->pr_phtree, ph);
	TAILQ_REMOVE(&pp->pr_emptypages, ph, ph_entry);

	pool_update_curpage(pp);
}

void
pool_update_curpage(struct pool *pp)
{
	pp->pr_curpage = TAILQ_LAST(&pp->pr_partpages, pool_pagelist);
	if (pp->pr_curpage == NULL) {
		pp->pr_curpage = TAILQ_LAST(&pp->pr_emptypages, pool_pagelist);
	}
}

void
pool_setlowat(struct pool *pp, int n)
{
	int prime = 0;

	pl_enter(pp, &pp->pr_lock);
	pp->pr_minitems = n;
	pp->pr_minpages = (n == 0)
		? 0
		: roundup(n, pp->pr_itemsperpage) / pp->pr_itemsperpage;

	if (pp->pr_nitems < n)
		prime = n - pp->pr_nitems;
	pl_leave(pp, &pp->pr_lock);

	if (prime > 0)
		pool_prime(pp, prime);
}

void
pool_sethiwat(struct pool *pp, int n)
{
	pp->pr_maxpages = (n == 0)
		? 0
		: roundup(n, pp->pr_itemsperpage) / pp->pr_itemsperpage;
}

int
pool_sethardlimit(struct pool *pp, u_int n)
{
	int error = 0;

	pl_enter(pp, &pp->pr_lock);

	if (n < pp->pr_nout) {
		error = EINVAL;
		goto done;
	}

	pp->pr_hardlimit = n;
done:
	pl_leave(pp, &pp->pr_lock);

	return (error);
}

void
pool_set_constraints(struct pool *pp, const struct kmem_pa_mode *mode)
{
	pp->pr_crange = mode;
}

/*
 * Release all complete pages that have not been used recently.
 *
 * Returns non-zero if any pages have been reclaimed.
 */
int
pool_reclaim(struct pool *pp)
{
	struct pool_page_header *ph, *phnext;
	struct pool_pagelist pl = TAILQ_HEAD_INITIALIZER(pl);

	pl_enter(pp, &pp->pr_lock);
	for (ph = TAILQ_FIRST(&pp->pr_emptypages); ph != NULL; ph = phnext) {
		phnext = TAILQ_NEXT(ph, ph_entry);

		/* Check our minimum page claim */
		if (pp->pr_npages <= pp->pr_minpages)
			break;

		/*
		 * If freeing this page would put us below
		 * the low water mark, stop now.
		 */
		if ((pp->pr_nitems - pp->pr_itemsperpage) <
		    pp->pr_minitems)
			break;

		pool_p_remove(pp, ph);
		TAILQ_INSERT_TAIL(&pl, ph, ph_entry);
	}
	pl_leave(pp, &pp->pr_lock);

	if (TAILQ_EMPTY(&pl))
		return (0);

	while ((ph = TAILQ_FIRST(&pl)) != NULL) {
		TAILQ_REMOVE(&pl, ph, ph_entry);
		pool_p_free(pp, ph);
	}

	return (1);
}

/*
 * Release all complete pages that have not been used recently
 * from all pools.
 */
void
pool_reclaim_all(void)
{
	struct pool	*pp;

	rw_enter_read(&pool_lock);
	SIMPLEQ_FOREACH(pp, &pool_head, pr_poollist)
		pool_reclaim(pp);
	rw_exit_read(&pool_lock);
}

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_output.h>

/*
 * Diagnostic helpers.
 */
void
pool_printit(struct pool *pp, const char *modif,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	pool_print1(pp, modif, pr);
}

void
pool_print_pagelist(struct pool_pagelist *pl,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	struct pool_page_header *ph;
	struct pool_item *pi;

	TAILQ_FOREACH(ph, pl, ph_entry) {
		(*pr)("\t\tpage %p, color %p, nmissing %d\n",
		    ph->ph_page, ph->ph_colored, ph->ph_nmissing);
		XSIMPLEQ_FOREACH(pi, &ph->ph_items, pi_list) {
			if (pi->pi_magic != POOL_IMAGIC(ph, pi)) {
				(*pr)("\t\t\titem %p, magic 0x%lx\n",
				    pi, pi->pi_magic);
			}
		}
	}
}

void
pool_print1(struct pool *pp, const char *modif,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	struct pool_page_header *ph;
	int print_pagelist = 0;
	char c;

	while ((c = *modif++) != '\0') {
		if (c == 'p')
			print_pagelist = 1;
		modif++;
	}

	(*pr)("POOL %s: size %u maxcolors %u\n", pp->pr_wchan, pp->pr_size,
	    pp->pr_maxcolors);
	(*pr)("\talloc %p\n", pp->pr_alloc);
	(*pr)("\tminitems %u, minpages %u, maxpages %u, npages %u\n",
	    pp->pr_minitems, pp->pr_minpages, pp->pr_maxpages, pp->pr_npages);
	(*pr)("\titemsperpage %u, nitems %u, nout %u, hardlimit %u\n",
	    pp->pr_itemsperpage, pp->pr_nitems, pp->pr_nout, pp->pr_hardlimit);

	(*pr)("\n\tnget %lu, nfail %lu, nput %lu\n",
	    pp->pr_nget, pp->pr_nfail, pp->pr_nput);
	(*pr)("\tnpagealloc %lu, npagefree %lu, hiwat %u, nidle %lu\n",
	    pp->pr_npagealloc, pp->pr_npagefree, pp->pr_hiwat, pp->pr_nidle);

	if (print_pagelist == 0)
		return;

	if ((ph = TAILQ_FIRST(&pp->pr_emptypages)) != NULL)
		(*pr)("\n\tempty page list:\n");
	pool_print_pagelist(&pp->pr_emptypages, pr);
	if ((ph = TAILQ_FIRST(&pp->pr_fullpages)) != NULL)
		(*pr)("\n\tfull page list:\n");
	pool_print_pagelist(&pp->pr_fullpages, pr);
	if ((ph = TAILQ_FIRST(&pp->pr_partpages)) != NULL)
		(*pr)("\n\tpartial-page list:\n");
	pool_print_pagelist(&pp->pr_partpages, pr);

	if (pp->pr_curpage == NULL)
		(*pr)("\tno current page\n");
	else
		(*pr)("\tcurpage %p\n", pp->pr_curpage->ph_page);
}

void
db_show_all_pools(db_expr_t expr, int haddr, db_expr_t count, char *modif)
{
	struct pool *pp;
	char maxp[16];
	int ovflw;
	char mode;

	mode = modif[0];
	if (mode != '\0' && mode != 'a') {
		db_printf("usage: show all pools [/a]\n");
		return;
	}

	if (mode == '\0')
		db_printf("%-10s%4s%9s%5s%9s%6s%6s%6s%6s%6s%6s%5s\n",
		    "Name",
		    "Size",
		    "Requests",
		    "Fail",
		    "Releases",
		    "Pgreq",
		    "Pgrel",
		    "Npage",
		    "Hiwat",
		    "Minpg",
		    "Maxpg",
		    "Idle");
	else
		db_printf("%-12s %18s %18s\n",
		    "Name", "Address", "Allocator");

	SIMPLEQ_FOREACH(pp, &pool_head, pr_poollist) {
		if (mode == 'a') {
			db_printf("%-12s %18p %18p\n", pp->pr_wchan, pp,
			    pp->pr_alloc);
			continue;
		}

		if (!pp->pr_nget)
			continue;

		if (pp->pr_maxpages == UINT_MAX)
			snprintf(maxp, sizeof maxp, "inf");
		else
			snprintf(maxp, sizeof maxp, "%u", pp->pr_maxpages);

#define PRWORD(ovflw, fmt, width, fixed, val) do {	\
	(ovflw) += db_printf((fmt),			\
	    (width) - (fixed) - (ovflw) > 0 ?		\
	    (width) - (fixed) - (ovflw) : 0,		\
	    (val)) - (width);				\
	if ((ovflw) < 0)				\
		(ovflw) = 0;				\
} while (/* CONSTCOND */0)

		ovflw = 0;
		PRWORD(ovflw, "%-*s", 10, 0, pp->pr_wchan);
		PRWORD(ovflw, " %*u", 4, 1, pp->pr_size);
		PRWORD(ovflw, " %*lu", 9, 1, pp->pr_nget);
		PRWORD(ovflw, " %*lu", 5, 1, pp->pr_nfail);
		PRWORD(ovflw, " %*lu", 9, 1, pp->pr_nput);
		PRWORD(ovflw, " %*lu", 6, 1, pp->pr_npagealloc);
		PRWORD(ovflw, " %*lu", 6, 1, pp->pr_npagefree);
		PRWORD(ovflw, " %*d", 6, 1, pp->pr_npages);
		PRWORD(ovflw, " %*d", 6, 1, pp->pr_hiwat);
		PRWORD(ovflw, " %*d", 6, 1, pp->pr_minpages);
		PRWORD(ovflw, " %*s", 6, 1, maxp);
		PRWORD(ovflw, " %*lu\n", 5, 1, pp->pr_nidle);

		pool_chk(pp);
	}
}
#endif /* DDB */

#if defined(POOL_DEBUG) || defined(DDB)
int
pool_chk_page(struct pool *pp, struct pool_page_header *ph, int expected)
{
	struct pool_item *pi;
	caddr_t page;
	int n;
	const char *label = pp->pr_wchan;

	page = (caddr_t)((u_long)ph & pp->pr_pgmask);
	if (page != ph->ph_page && POOL_INPGHDR(pp)) {
		printf("%s: ", label);
		printf("pool(%p:%s): page inconsistency: page %p; "
		    "at page head addr %p (p %p)\n",
		    pp, pp->pr_wchan, ph->ph_page, ph, page);
		return 1;
	}

	for (pi = XSIMPLEQ_FIRST(&ph->ph_items), n = 0;
	     pi != NULL;
	     pi = XSIMPLEQ_NEXT(&ph->ph_items, pi, pi_list), n++) {
		if ((caddr_t)pi < ph->ph_page ||
		    (caddr_t)pi >= ph->ph_page + pp->pr_pgsize) {
			printf("%s: ", label);
			printf("pool(%p:%s): page inconsistency: page %p;"
			    " item ordinal %d; addr %p\n", pp,
			    pp->pr_wchan, ph->ph_page, n, pi);
			return (1);
		}

		if (pi->pi_magic != POOL_IMAGIC(ph, pi)) {
			printf("%s: ", label);
			printf("pool(%p:%s): free list modified: "
			    "page %p; item ordinal %d; addr %p "
			    "(p %p); offset 0x%x=0x%lx\n",
			    pp, pp->pr_wchan, ph->ph_page, n, pi, page,
			    0, pi->pi_magic);
		}

#ifdef DIAGNOSTIC
		if (POOL_PHPOISON(ph)) {
			size_t pidx;
			uint32_t pval;
			if (poison_check(pi + 1, pp->pr_size - sizeof(*pi),
			    &pidx, &pval)) {
				int *ip = (int *)(pi + 1);
				printf("pool(%s): free list modified: "
				    "page %p; item ordinal %d; addr %p "
				    "(p %p); offset 0x%zx=0x%x\n",
				    pp->pr_wchan, ph->ph_page, n, pi,
				    page, pidx * sizeof(int), ip[pidx]);
			}
		}
#endif /* DIAGNOSTIC */
	}
	if (n + ph->ph_nmissing != pp->pr_itemsperpage) {
		printf("pool(%p:%s): page inconsistency: page %p;"
		    " %d on list, %d missing, %d items per page\n", pp,
		    pp->pr_wchan, ph->ph_page, n, ph->ph_nmissing,
		    pp->pr_itemsperpage);
		return 1;
	}
	if (expected >= 0 && n != expected) {
		printf("pool(%p:%s): page inconsistency: page %p;"
		    " %d on list, %d missing, %d expected\n", pp,
		    pp->pr_wchan, ph->ph_page, n, ph->ph_nmissing,
		    expected);
		return 1;
	}
	return 0;
}

int
pool_chk(struct pool *pp)
{
	struct pool_page_header *ph;
	int r = 0;

	TAILQ_FOREACH(ph, &pp->pr_emptypages, ph_entry)
		r += pool_chk_page(pp, ph, pp->pr_itemsperpage);
	TAILQ_FOREACH(ph, &pp->pr_fullpages, ph_entry)
		r += pool_chk_page(pp, ph, 0);
	TAILQ_FOREACH(ph, &pp->pr_partpages, ph_entry)
		r += pool_chk_page(pp, ph, -1);

	return (r);
}
#endif /* defined(POOL_DEBUG) || defined(DDB) */

#ifdef DDB
void
pool_walk(struct pool *pp, int full,
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))),
    void (*func)(void *, int, int (*)(const char *, ...)
	    __attribute__((__format__(__kprintf__,1,2)))))
{
	struct pool_page_header *ph;
	struct pool_item *pi;
	caddr_t cp;
	int n;

	TAILQ_FOREACH(ph, &pp->pr_fullpages, ph_entry) {
		cp = ph->ph_colored;
		n = ph->ph_nmissing;

		while (n--) {
			func(cp, full, pr);
			cp += pp->pr_size;
		}
	}

	TAILQ_FOREACH(ph, &pp->pr_partpages, ph_entry) {
		cp = ph->ph_colored;
		n = ph->ph_nmissing;

		do {
			XSIMPLEQ_FOREACH(pi, &ph->ph_items, pi_list) {
				if (cp == (caddr_t)pi)
					break;
			}
			if (cp != (caddr_t)pi) {
				func(cp, full, pr);
				n--;
			}

			cp += pp->pr_size;
		} while (n > 0);
	}
}
#endif

#ifndef SMALL_KERNEL
/*
 * We have three different sysctls.
 * kern.pool.npools - the number of pools.
 * kern.pool.pool.<pool#> - the pool struct for the pool#.
 * kern.pool.name.<pool#> - the name for pool#.
 */
int
sysctl_dopool(int *name, u_int namelen, char *oldp, size_t *oldlenp)
{
	struct kinfo_pool pi;
	struct pool *pp;
	int rv = EOPNOTSUPP;

	switch (name[0]) {
	case KERN_POOL_NPOOLS:
		if (namelen != 1)
			return (ENOTDIR);
		return (sysctl_rdint(oldp, oldlenp, NULL, pool_count));

	case KERN_POOL_NAME:
	case KERN_POOL_POOL:
	case KERN_POOL_CACHE:
	case KERN_POOL_CACHE_CPUS:
		break;
	default:
		return (EOPNOTSUPP);
	}

	if (namelen != 2)
		return (ENOTDIR);

	rw_enter_read(&pool_lock);
	SIMPLEQ_FOREACH(pp, &pool_head, pr_poollist) {
		if (name[1] == pp->pr_serial) {
			refcnt_take(&pp->pr_refcnt);
			break;
		}
	}
	rw_exit_read(&pool_lock);

	if (pp == NULL)
		return (ENOENT);

	switch (name[0]) {
	case KERN_POOL_NAME:
		rv = sysctl_rdstring(oldp, oldlenp, NULL, pp->pr_wchan);
		break;
	case KERN_POOL_POOL:
		memset(&pi, 0, sizeof(pi));

		pl_enter(pp, &pp->pr_lock);
		pi.pr_size = pp->pr_size;
		pi.pr_pgsize = pp->pr_pgsize;
		pi.pr_itemsperpage = pp->pr_itemsperpage;
		pi.pr_npages = pp->pr_npages;
		pi.pr_minpages = pp->pr_minpages;
		pi.pr_maxpages = pp->pr_maxpages;
		pi.pr_hardlimit = pp->pr_hardlimit;
		pi.pr_nout = pp->pr_nout;
		pi.pr_nitems = pp->pr_nitems;
		pi.pr_nget = pp->pr_nget;
		pi.pr_nput = pp->pr_nput;
		pi.pr_nfail = pp->pr_nfail;
		pi.pr_npagealloc = pp->pr_npagealloc;
		pi.pr_npagefree = pp->pr_npagefree;
		pi.pr_hiwat = pp->pr_hiwat;
		pi.pr_nidle = pp->pr_nidle;
		pl_leave(pp, &pp->pr_lock);

		pool_cache_pool_info(pp, &pi);

		rv = sysctl_rdstruct(oldp, oldlenp, NULL, &pi, sizeof(pi));
		break;

	case KERN_POOL_CACHE:
		rv = pool_cache_info(pp, oldp, oldlenp);
		break;

	case KERN_POOL_CACHE_CPUS:
		rv = pool_cache_cpus_info(pp, oldp, oldlenp);
		break;
	}

	refcnt_rele_wake(&pp->pr_refcnt);

	return (rv);
}
#endif /* SMALL_KERNEL */

void
pool_gc_sched(void *null)
{
	task_add(systqmp, &pool_gc_task);
}

void
pool_gc_pages(void *null)
{
	struct pool *pp;
	struct pool_page_header *ph, *freeph;
	int s;

	rw_enter_read(&pool_lock);
	s = splvm(); /* XXX go to splvm until all pools _setipl properly */
	SIMPLEQ_FOREACH(pp, &pool_head, pr_poollist) {
#ifdef MULTIPROCESSOR
		if (pp->pr_cache != NULL)
			pool_cache_gc(pp);
#endif

		if (pp->pr_nidle <= pp->pr_minpages || /* guess */
		    !pl_enter_try(pp, &pp->pr_lock)) /* try */
			continue;

		/* is it time to free a page? */
		if (pp->pr_nidle > pp->pr_minpages &&
		    (ph = TAILQ_FIRST(&pp->pr_emptypages)) != NULL &&
		    getnsecuptime() - ph->ph_timestamp > POOL_WAIT_GC) {
			freeph = ph;
			pool_p_remove(pp, freeph);
		} else
			freeph = NULL;

		pl_leave(pp, &pp->pr_lock);

		if (freeph != NULL)
			pool_p_free(pp, freeph);
	}
	splx(s);
	rw_exit_read(&pool_lock);

	timeout_add_sec(&pool_gc_tick, 1);
}

/*
 * Pool backend allocators.
 */

void *
pool_allocator_alloc(struct pool *pp, int flags, int *slowdown)
{
	void *v;

	v = (*pp->pr_alloc->pa_alloc)(pp, flags, slowdown);

#ifdef DIAGNOSTIC
	if (v != NULL && POOL_INPGHDR(pp)) {
		vaddr_t addr = (vaddr_t)v;
		if ((addr & pp->pr_pgmask) != addr) {
			panic("%s: %s page address %p isn't aligned to %u",
			    __func__, pp->pr_wchan, v, pp->pr_pgsize);
		}
	}
#endif

	return (v);
}

void
pool_allocator_free(struct pool *pp, void *v)
{
	struct pool_allocator *pa = pp->pr_alloc;

	(*pa->pa_free)(pp, v);
}

void *
pool_page_alloc(struct pool *pp, int flags, int *slowdown)
{
	struct kmem_dyn_mode kd = KMEM_DYN_INITIALIZER;

	kd.kd_waitok = ISSET(flags, PR_WAITOK);
	kd.kd_slowdown = slowdown;

	return (km_alloc(pp->pr_pgsize, &kv_page, pp->pr_crange, &kd));
}

void
pool_page_free(struct pool *pp, void *v)
{
	km_free(v, pp->pr_pgsize, &kv_page, pp->pr_crange);
}

void *
pool_multi_alloc(struct pool *pp, int flags, int *slowdown)
{
	struct kmem_va_mode kv = kv_intrsafe;
	struct kmem_dyn_mode kd = KMEM_DYN_INITIALIZER;
	void *v;
	int s;

	if (POOL_INPGHDR(pp))
		kv.kv_align = pp->pr_pgsize;

	kd.kd_waitok = ISSET(flags, PR_WAITOK);
	kd.kd_slowdown = slowdown;

	s = splvm();
	v = km_alloc(pp->pr_pgsize, &kv, pp->pr_crange, &kd);
	splx(s);

	return (v);
}

void
pool_multi_free(struct pool *pp, void *v)
{
	struct kmem_va_mode kv = kv_intrsafe;
	int s;

	if (POOL_INPGHDR(pp))
		kv.kv_align = pp->pr_pgsize;

	s = splvm();
	km_free(v, pp->pr_pgsize, &kv, pp->pr_crange);
	splx(s);
}

void *
pool_multi_alloc_ni(struct pool *pp, int flags, int *slowdown)
{
	struct kmem_va_mode kv = kv_any;
	struct kmem_dyn_mode kd = KMEM_DYN_INITIALIZER;
	void *v;

	if (POOL_INPGHDR(pp))
		kv.kv_align = pp->pr_pgsize;

	kd.kd_waitok = ISSET(flags, PR_WAITOK);
	kd.kd_slowdown = slowdown;

	KERNEL_LOCK();
	v = km_alloc(pp->pr_pgsize, &kv, pp->pr_crange, &kd);
	KERNEL_UNLOCK();

	return (v);
}

void
pool_multi_free_ni(struct pool *pp, void *v)
{
	struct kmem_va_mode kv = kv_any;

	if (POOL_INPGHDR(pp))
		kv.kv_align = pp->pr_pgsize;

	KERNEL_LOCK();
	km_free(v, pp->pr_pgsize, &kv, pp->pr_crange);
	KERNEL_UNLOCK();
}

#ifdef MULTIPROCESSOR

struct pool pool_caches; /* per cpu cache entries */

void
pool_cache_init(struct pool *pp)
{
	struct cpumem *cm;
	struct pool_cache *pc;
	struct cpumem_iter i;

	if (pool_caches.pr_size == 0) {
		pool_init(&pool_caches, sizeof(struct pool_cache),
		    CACHELINESIZE, IPL_NONE, PR_WAITOK | PR_RWLOCK,
		    "plcache", NULL);
	}

	/* must be able to use the pool items as cache list items */
	KASSERT(pp->pr_size >= sizeof(struct pool_cache_item));

	cm = cpumem_get(&pool_caches);

	pl_init(pp, &pp->pr_cache_lock);
	arc4random_buf(pp->pr_cache_magic, sizeof(pp->pr_cache_magic));
	TAILQ_INIT(&pp->pr_cache_lists);
	pp->pr_cache_nitems = 0;
	pp->pr_cache_timestamp = getnsecuptime();
	pp->pr_cache_items = 8;
	pp->pr_cache_contention = 0;
	pp->pr_cache_ngc = 0;

	CPUMEM_FOREACH(pc, &i, cm) {
		pc->pc_actv = NULL;
		pc->pc_nactv = 0;
		pc->pc_prev = NULL;

		pc->pc_nget = 0;
		pc->pc_nfail = 0;
		pc->pc_nput = 0;
		pc->pc_nlget = 0;
		pc->pc_nlfail = 0;
		pc->pc_nlput = 0;
		pc->pc_nout = 0;
	}

	membar_producer();

	pp->pr_cache = cm;
}

static inline void
pool_cache_item_magic(struct pool *pp, struct pool_cache_item *ci)
{
	unsigned long *entry = (unsigned long *)&ci->ci_nextl;

	entry[0] = pp->pr_cache_magic[0] ^ (u_long)ci;
	entry[1] = pp->pr_cache_magic[1] ^ (u_long)ci->ci_next;
}

static inline void
pool_cache_item_magic_check(struct pool *pp, struct pool_cache_item *ci)
{
	unsigned long *entry;
	unsigned long val;

	entry = (unsigned long *)&ci->ci_nextl;
	val = pp->pr_cache_magic[0] ^ (u_long)ci;
	if (*entry != val)
		goto fail;

	entry++;
	val = pp->pr_cache_magic[1] ^ (u_long)ci->ci_next;
	if (*entry != val)
		goto fail;

	return;

fail:
	panic("%s: %s cpu free list modified: item addr %p+%zu 0x%lx!=0x%lx",
	    __func__, pp->pr_wchan, ci, (caddr_t)entry - (caddr_t)ci,
	    *entry, val);
}

static inline void
pool_list_enter(struct pool *pp)
{
	if (pl_enter_try(pp, &pp->pr_cache_lock) == 0) {
		pl_enter(pp, &pp->pr_cache_lock);
		pp->pr_cache_contention++;
	}
}

static inline void
pool_list_leave(struct pool *pp)
{
	pl_leave(pp, &pp->pr_cache_lock);
}

static inline struct pool_cache_item *
pool_cache_list_alloc(struct pool *pp, struct pool_cache *pc)
{
	struct pool_cache_item *pl;

	pool_list_enter(pp);
	pl = TAILQ_FIRST(&pp->pr_cache_lists);
	if (pl != NULL) {
		TAILQ_REMOVE(&pp->pr_cache_lists, pl, ci_nextl);
		pp->pr_cache_nitems -= POOL_CACHE_ITEM_NITEMS(pl);

		pool_cache_item_magic(pp, pl);

		pc->pc_nlget++;
	} else
		pc->pc_nlfail++;

	/* fold this cpus nout into the global while we have the lock */
	pp->pr_cache_nout += pc->pc_nout;
	pc->pc_nout = 0;
	pool_list_leave(pp);

	return (pl);
}

static inline void
pool_cache_list_free(struct pool *pp, struct pool_cache *pc,
    struct pool_cache_item *ci)
{
	pool_list_enter(pp);
	if (TAILQ_EMPTY(&pp->pr_cache_lists))
		pp->pr_cache_timestamp = getnsecuptime();

	pp->pr_cache_nitems += POOL_CACHE_ITEM_NITEMS(ci);
	TAILQ_INSERT_TAIL(&pp->pr_cache_lists, ci, ci_nextl);

	pc->pc_nlput++;

	/* fold this cpus nout into the global while we have the lock */
	pp->pr_cache_nout += pc->pc_nout;
	pc->pc_nout = 0;
	pool_list_leave(pp);
}

static inline struct pool_cache *
pool_cache_enter(struct pool *pp, int *s)
{
	struct pool_cache *pc;

	pc = cpumem_enter(pp->pr_cache);
	*s = splraise(pp->pr_ipl);
	pc->pc_gen++;

	return (pc);
}

static inline void
pool_cache_leave(struct pool *pp, struct pool_cache *pc, int s)
{
	pc->pc_gen++;
	splx(s);
	cpumem_leave(pp->pr_cache, pc);
}

void *
pool_cache_get(struct pool *pp)
{
	struct pool_cache *pc;
	struct pool_cache_item *ci;
	int s;

	pc = pool_cache_enter(pp, &s);

	if (pc->pc_actv != NULL) {
		ci = pc->pc_actv;
	} else if (pc->pc_prev != NULL) {
		ci = pc->pc_prev;
		pc->pc_prev = NULL;
	} else if ((ci = pool_cache_list_alloc(pp, pc)) == NULL) {
		pc->pc_nfail++;
		goto done;
	}

	pool_cache_item_magic_check(pp, ci);
#ifdef DIAGNOSTIC
	if (pool_debug && POOL_CACHE_ITEM_POISONED(ci)) {
		size_t pidx;
		uint32_t pval;

		if (poison_check(ci + 1, pp->pr_size - sizeof(*ci),
		    &pidx, &pval)) {
			int *ip = (int *)(ci + 1);
			ip += pidx;

			panic("%s: %s cpu free list modified: "
			    "item addr %p+%zu 0x%x!=0x%x",
			    __func__, pp->pr_wchan, ci,
			    (caddr_t)ip - (caddr_t)ci, *ip, pval);
		}
	}
#endif

	pc->pc_actv = ci->ci_next;
	pc->pc_nactv = POOL_CACHE_ITEM_NITEMS(ci) - 1;
	pc->pc_nget++;
	pc->pc_nout++;

done:
	pool_cache_leave(pp, pc, s);

	return (ci);
}

void
pool_cache_put(struct pool *pp, void *v)
{
	struct pool_cache *pc;
	struct pool_cache_item *ci = v;
	unsigned long nitems;
	int s;
#ifdef DIAGNOSTIC
	int poison = pool_debug && pp->pr_size > sizeof(*ci);

	if (poison)
		poison_mem(ci + 1, pp->pr_size - sizeof(*ci));
#endif

	pc = pool_cache_enter(pp, &s);

	nitems = pc->pc_nactv;
	if (nitems >= pp->pr_cache_items) {
		if (pc->pc_prev != NULL)
			pool_cache_list_free(pp, pc, pc->pc_prev);

		pc->pc_prev = pc->pc_actv;

		pc->pc_actv = NULL;
		pc->pc_nactv = 0;
		nitems = 0;
	}

	ci->ci_next = pc->pc_actv;
	ci->ci_nitems = ++nitems;
#ifdef DIAGNOSTIC
	ci->ci_nitems |= poison ? POOL_CACHE_ITEM_NITEMS_POISON : 0;
#endif
	pool_cache_item_magic(pp, ci);

	pc->pc_actv = ci;
	pc->pc_nactv = nitems;

	pc->pc_nput++;
	pc->pc_nout--;

	pool_cache_leave(pp, pc, s);
}

struct pool_cache_item *
pool_cache_list_put(struct pool *pp, struct pool_cache_item *pl)
{
	struct pool_cache_item *rpl, *next;

	if (pl == NULL)
		return (NULL);

	rpl = TAILQ_NEXT(pl, ci_nextl);

	pl_enter(pp, &pp->pr_lock);
	do {
		next = pl->ci_next;
		pool_do_put(pp, pl);
		pl = next;
	} while (pl != NULL);
	pl_leave(pp, &pp->pr_lock);

	return (rpl);
}

void
pool_cache_destroy(struct pool *pp)
{
	struct pool_cache *pc;
	struct pool_cache_item *pl;
	struct cpumem_iter i;
	struct cpumem *cm;

	rw_enter_write(&pool_lock); /* serialise with the gc */
	cm = pp->pr_cache;
	pp->pr_cache = NULL; /* make pool_put avoid the cache */
	rw_exit_write(&pool_lock);

	CPUMEM_FOREACH(pc, &i, cm) {
		pool_cache_list_put(pp, pc->pc_actv);
		pool_cache_list_put(pp, pc->pc_prev);
	}

	cpumem_put(&pool_caches, cm);

	pl = TAILQ_FIRST(&pp->pr_cache_lists);
	while (pl != NULL)
		pl = pool_cache_list_put(pp, pl);
}

void
pool_cache_gc(struct pool *pp)
{
	unsigned int contention, delta;

	if (getnsecuptime() - pp->pr_cache_timestamp > POOL_WAIT_GC &&
	    !TAILQ_EMPTY(&pp->pr_cache_lists) &&
	    pl_enter_try(pp, &pp->pr_cache_lock)) {
		struct pool_cache_item *pl = NULL;

		pl = TAILQ_FIRST(&pp->pr_cache_lists);
		if (pl != NULL) {
			TAILQ_REMOVE(&pp->pr_cache_lists, pl, ci_nextl);
			pp->pr_cache_nitems -= POOL_CACHE_ITEM_NITEMS(pl);
			pp->pr_cache_timestamp = getnsecuptime();

			pp->pr_cache_ngc++;
		}

		pl_leave(pp, &pp->pr_cache_lock);

		pool_cache_list_put(pp, pl);
	}

	/*
	 * if there's a lot of contention on the pr_cache_mtx then consider
	 * growing the length of the list to reduce the need to access the
	 * global pool.
	 */

	contention = pp->pr_cache_contention;
	delta = contention - pp->pr_cache_contention_prev;
	if (delta > 8 /* magic */) {
		if ((ncpusfound * 8 * 2) <= pp->pr_cache_nitems)
			pp->pr_cache_items += 8;
	} else if (delta == 0) {
		if (pp->pr_cache_items > 8)
			pp->pr_cache_items--;
	}
	pp->pr_cache_contention_prev = contention;
}

void
pool_cache_pool_info(struct pool *pp, struct kinfo_pool *pi)
{
	struct pool_cache *pc;
	struct cpumem_iter i;

	if (pp->pr_cache == NULL)
		return;

	/* loop through the caches twice to collect stats */

	/* once without the lock so we can yield while reading nget/nput */
	CPUMEM_FOREACH(pc, &i, pp->pr_cache) {
		uint64_t gen, nget, nput;

		do {
			while ((gen = pc->pc_gen) & 1)
				yield();

			nget = pc->pc_nget;
			nput = pc->pc_nput;
		} while (gen != pc->pc_gen);

		pi->pr_nget += nget;
		pi->pr_nput += nput;
	}

	/* and once with the mtx so we can get consistent nout values */
	pl_enter(pp, &pp->pr_cache_lock);
	CPUMEM_FOREACH(pc, &i, pp->pr_cache)
		pi->pr_nout += pc->pc_nout;

	pi->pr_nout += pp->pr_cache_nout;
	pl_leave(pp, &pp->pr_cache_lock);
}

int
pool_cache_info(struct pool *pp, void *oldp, size_t *oldlenp)
{
	struct kinfo_pool_cache kpc;

	if (pp->pr_cache == NULL)
		return (EOPNOTSUPP);

	memset(&kpc, 0, sizeof(kpc)); /* don't leak padding */

	pl_enter(pp, &pp->pr_cache_lock);
	kpc.pr_ngc = pp->pr_cache_ngc;
	kpc.pr_len = pp->pr_cache_items;
	kpc.pr_nitems = pp->pr_cache_nitems;
	kpc.pr_contention = pp->pr_cache_contention;
	pl_leave(pp, &pp->pr_cache_lock);

	return (sysctl_rdstruct(oldp, oldlenp, NULL, &kpc, sizeof(kpc)));
}

int
pool_cache_cpus_info(struct pool *pp, void *oldp, size_t *oldlenp)
{
	struct pool_cache *pc;
	struct kinfo_pool_cache_cpu *kpcc, *info;
	unsigned int cpu = 0;
	struct cpumem_iter i;
	int error = 0;
	size_t len;

	if (pp->pr_cache == NULL)
		return (EOPNOTSUPP);
	if (*oldlenp % sizeof(*kpcc))
		return (EINVAL);

	kpcc = mallocarray(ncpusfound, sizeof(*kpcc), M_TEMP,
	    M_WAITOK|M_CANFAIL|M_ZERO);
	if (kpcc == NULL)
		return (EIO);

	len = ncpusfound * sizeof(*kpcc);

	CPUMEM_FOREACH(pc, &i, pp->pr_cache) {
		uint64_t gen;

		if (cpu >= ncpusfound) {
			error = EIO;
			goto err;
		}

		info = &kpcc[cpu];
		info->pr_cpu = cpu;

		do {
			while ((gen = pc->pc_gen) & 1)
				yield();

			info->pr_nget = pc->pc_nget;
			info->pr_nfail = pc->pc_nfail;
			info->pr_nput = pc->pc_nput;
			info->pr_nlget = pc->pc_nlget;
			info->pr_nlfail = pc->pc_nlfail;
			info->pr_nlput = pc->pc_nlput;
		} while (gen != pc->pc_gen);

		cpu++;
	}

	error = sysctl_rdstruct(oldp, oldlenp, NULL, kpcc, len);
err:
	free(kpcc, M_TEMP, len);

	return (error);
}
#else /* MULTIPROCESSOR */
void
pool_cache_init(struct pool *pp)
{
	/* nop */
}

void
pool_cache_pool_info(struct pool *pp, struct kinfo_pool *pi)
{
	/* nop */
}

int
pool_cache_info(struct pool *pp, void *oldp, size_t *oldlenp)
{
	return (EOPNOTSUPP);
}

int
pool_cache_cpus_info(struct pool *pp, void *oldp, size_t *oldlenp)
{
	return (EOPNOTSUPP);
}
#endif /* MULTIPROCESSOR */


void
pool_lock_mtx_init(struct pool *pp, union pool_lock *lock,
    const struct lock_type *type)
{
	_mtx_init_flags(&lock->prl_mtx, pp->pr_ipl, pp->pr_wchan, 0, type);
}

void
pool_lock_mtx_enter(union pool_lock *lock)
{
	mtx_enter(&lock->prl_mtx);
}

int
pool_lock_mtx_enter_try(union pool_lock *lock)
{
	return (mtx_enter_try(&lock->prl_mtx));
}

void
pool_lock_mtx_leave(union pool_lock *lock)
{
	mtx_leave(&lock->prl_mtx);
}

void
pool_lock_mtx_assert_locked(union pool_lock *lock)
{
	MUTEX_ASSERT_LOCKED(&lock->prl_mtx);
}

void
pool_lock_mtx_assert_unlocked(union pool_lock *lock)
{
	MUTEX_ASSERT_UNLOCKED(&lock->prl_mtx);
}

int
pool_lock_mtx_sleep(void *ident, union pool_lock *lock, int priority,
    const char *wmesg)
{
	return msleep_nsec(ident, &lock->prl_mtx, priority, wmesg, INFSLP);
}

static const struct pool_lock_ops pool_lock_ops_mtx = {
	pool_lock_mtx_init,
	pool_lock_mtx_enter,
	pool_lock_mtx_enter_try,
	pool_lock_mtx_leave,
	pool_lock_mtx_assert_locked,
	pool_lock_mtx_assert_unlocked,
	pool_lock_mtx_sleep,
};

void
pool_lock_rw_init(struct pool *pp, union pool_lock *lock,
    const struct lock_type *type)
{
	_rw_init_flags(&lock->prl_rwlock, pp->pr_wchan, 0, type, 0);
}

void
pool_lock_rw_enter(union pool_lock *lock)
{
	rw_enter_write(&lock->prl_rwlock);
}

int
pool_lock_rw_enter_try(union pool_lock *lock)
{
	return (rw_enter(&lock->prl_rwlock, RW_WRITE | RW_NOSLEEP) == 0);
}

void
pool_lock_rw_leave(union pool_lock *lock)
{
	rw_exit_write(&lock->prl_rwlock);
}

void
pool_lock_rw_assert_locked(union pool_lock *lock)
{
	rw_assert_wrlock(&lock->prl_rwlock);
}

void
pool_lock_rw_assert_unlocked(union pool_lock *lock)
{
	KASSERT(rw_status(&lock->prl_rwlock) != RW_WRITE);
}

int
pool_lock_rw_sleep(void *ident, union pool_lock *lock, int priority,
    const char *wmesg)
{
	return rwsleep_nsec(ident, &lock->prl_rwlock, priority, wmesg, INFSLP);
}

static const struct pool_lock_ops pool_lock_ops_rw = {
	pool_lock_rw_init,
	pool_lock_rw_enter,
	pool_lock_rw_enter_try,
	pool_lock_rw_leave,
	pool_lock_rw_assert_locked,
	pool_lock_rw_assert_unlocked,
	pool_lock_rw_sleep,
};
