/*	$OpenBSD: pool.h,v 1.81 2025/05/21 09:33:49 mvs Exp $	*/
/*	$NetBSD: pool.h,v 1.27 2001/06/06 22:00:17 rafal Exp $	*/

/*-
 * Copyright (c) 1997, 1998, 1999, 2000 The NetBSD Foundation, Inc.
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

#ifndef _SYS_POOL_H_
#define _SYS_POOL_H_

/*
 * sysctls.
 * kern.pool.npools
 * kern.pool.name.<number>
 * kern.pool.pool.<number>
 */
#define KERN_POOL_NPOOLS	1
#define KERN_POOL_NAME		2
#define KERN_POOL_POOL		3
#define KERN_POOL_CACHE		4	/* global pool cache info */
#define KERN_POOL_CACHE_CPUS	5	/* all cpus cache info */

struct kinfo_pool {
	unsigned int	pr_size;	/* size of a pool item */
	unsigned int	pr_pgsize;	/* size of a "page" */
	unsigned int	pr_itemsperpage; /* number of items per "page" */
	unsigned int	pr_minpages;	/* same in page units */
	unsigned int	pr_maxpages;	/* maximum # of idle pages to keep */
	unsigned int	pr_hardlimit;	/* hard limit to number of allocated
					   items */

	unsigned int	pr_npages;	/* # of pages allocated */
	unsigned int	pr_nout;	/* # items currently allocated */
	unsigned int	pr_nitems;	/* # items in the pool */

	unsigned long	pr_nget;	/* # of successful requests */
	unsigned long	pr_nput;	/* # of releases */
	unsigned long	pr_nfail;	/* # of unsuccessful requests */
	unsigned long	pr_npagealloc;	/* # of pages allocated */
	unsigned long	pr_npagefree;	/* # of pages released */
	unsigned int	pr_hiwat;	/* max # of pages in pool */
	unsigned long	pr_nidle;	/* # of idle pages */
};

struct kinfo_pool_cache {
	uint64_t	pr_ngc;		/* # of times a list has been gc'ed */
	unsigned int	pr_len;		/* current target for list len */
	unsigned int	pr_nitems;	/* # of idle items in the depot */
	unsigned int	pr_contention;	/* # of times mtx was busy */
};

/*
 * KERN_POOL_CACHE_CPUS provides an array, not a single struct. ie, it
 * provides struct kinfo_pool_cache_cpu kppc[ncpusfound].
 */
struct kinfo_pool_cache_cpu {
	unsigned int	pr_cpu;		/* which cpu this cache is on */

	/* counters for times items were handled by the cache */
	uint64_t	pr_nget;	/* # of requests */
	uint64_t	pr_nfail;	/* # of unsuccessful requests */
	uint64_t	pr_nput;	/* # of releases */

	/* counters for times the cache interacted with the pool */
	uint64_t	pr_nlget;	/* # of list requests */
	uint64_t	pr_nlfail;	/* # of unsuccessful list requests */
	uint64_t	pr_nlput;	/* # of list releases */
};

#if defined(_KERNEL) || defined(_LIBKVM)

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>

struct pool;
struct pool_request;
struct pool_lock_ops;
TAILQ_HEAD(pool_requests, pool_request);

struct pool_allocator {
	void		*(*pa_alloc)(struct pool *, int, int *);
	void		 (*pa_free)(struct pool *, void *);
	size_t		   pa_pagesz;
};

/*
 * The pa_pagesz member encodes the sizes of pages that can be
 * provided by the allocator, and whether the allocations can be
 * aligned to their size.
 *
 * Page sizes can only be powers of two. Each available page size is
 * represented by its value set as a bit. e.g., to indicate that an
 * allocator can provide 16k and 32k pages you initialise pa_pagesz
 * to (32768 | 16384).
 *
 * If the allocator can provide aligned pages the low bit in pa_pagesz
 * is set. The POOL_ALLOC_ALIGNED macro is provided as a convenience.
 *
 * If pa_pagesz is unset (i.e. 0), POOL_ALLOC_DEFAULT will be used
 * instead.
 */

#define POOL_ALLOC_ALIGNED		1UL
#define POOL_ALLOC_SIZE(_sz, _a)	((_sz) | (_a))
#define POOL_ALLOC_SIZES(_min, _max, _a) \
	((_max) | \
	(((_max) - 1) & ~((_min) - 1)) | (_a))

#define POOL_ALLOC_DEFAULT \
	POOL_ALLOC_SIZE(PAGE_SIZE, POOL_ALLOC_ALIGNED)

TAILQ_HEAD(pool_pagelist, pool_page_header);

struct pool_cache_item;
TAILQ_HEAD(pool_cache_lists, pool_cache_item);
struct cpumem;

union pool_lock {
	struct mutex	prl_mtx;
	struct rwlock	prl_rwlock;
};

struct pool {
	struct refcnt	pr_refcnt;
	union pool_lock	pr_lock;
	const struct pool_lock_ops *
			pr_lock_ops;
	SIMPLEQ_ENTRY(pool)
			pr_poollist;
	struct pool_pagelist
			pr_emptypages;	/* Empty pages */
	struct pool_pagelist
			pr_fullpages;	/* Full pages */
	struct pool_pagelist
			pr_partpages;	/* Partially-allocated pages */
	struct pool_page_header	*
			pr_curpage;
	unsigned int	pr_size;	/* Size of item */
	unsigned int	pr_minitems;	/* minimum # of items to keep */
	unsigned int	pr_minpages;	/* same in page units */
	unsigned int	pr_maxpages;	/* maximum # of idle pages to keep */
	unsigned int	pr_npages;	/* # of pages allocated */
	unsigned int	pr_itemsperpage;/* # items that fit in a page */
	unsigned int	pr_slack;	/* unused space in a page */
	unsigned int	pr_nitems;	/* number of available items in pool */
	unsigned int	pr_nout;	/* # items currently allocated */
	unsigned int	pr_hardlimit;	/* hard limit to number of allocated
					   items */
	unsigned int	pr_serial;	/* unique serial number of the pool */
	unsigned int	pr_pgsize;	/* Size of a "page" */
	vaddr_t		pr_pgmask;	/* Mask with an item to get a page */
	struct pool_allocator *
			pr_alloc;	/* backend allocator */
	const char *	pr_wchan;	/* tsleep(9) identifier */
#define PR_WAITOK	0x0001 /* M_WAITOK */
#define PR_NOWAIT	0x0002 /* M_NOWAIT */
#define PR_LIMITFAIL	0x0004 /* M_CANFAIL */
#define PR_ZERO		0x0008 /* M_ZERO */
#define PR_RWLOCK	0x0010
#define PR_WANTED	0x0100

	int		pr_flags;
	int		pr_ipl;

	RBT_HEAD(phtree, pool_page_header)
			pr_phtree;

	struct cpumem *	pr_cache;
	unsigned long	pr_cache_magic[2];
	union pool_lock	pr_cache_lock;
	struct pool_cache_lists
			pr_cache_lists;	/* list of idle item lists */
	u_int		pr_cache_nitems; /* # of idle items */
	u_int		pr_cache_items;	/* target list length */
	u_int		pr_cache_contention;
	u_int		pr_cache_contention_prev;
	uint64_t	pr_cache_timestamp;	/* time idle list was empty */
	uint64_t	pr_cache_ngc;	/* # of times the gc released a list */
	int		pr_cache_nout;

	u_int		pr_align;
	u_int		pr_maxcolors;	/* Cache coloring */
	int		pr_phoffset;	/* Offset in page of page header */

	/*
	 * pool item requests queue
	 */
	union pool_lock	pr_requests_lock;
	struct pool_requests
			pr_requests;
	unsigned int	pr_requesting;

	/*
	 * Instrumentation
	 */
	unsigned long	pr_nget;	/* # of successful requests */
	unsigned long	pr_nfail;	/* # of unsuccessful requests */
	unsigned long	pr_nput;	/* # of releases */
	unsigned long	pr_npagealloc;	/* # of pages allocated */
	unsigned long	pr_npagefree;	/* # of pages released */
	unsigned int	pr_hiwat;	/* max # of pages in pool */
	unsigned long	pr_nidle;	/* # of idle pages */

	/* Physical memory configuration. */
	const struct kmem_pa_mode *
			pr_crange;
};

#endif /* _KERNEL || _LIBKVM */

#ifdef _KERNEL

extern struct pool_allocator pool_allocator_single;
extern struct pool_allocator pool_allocator_multi;

struct pool_request {
	TAILQ_ENTRY(pool_request) pr_entry;
	void (*pr_handler)(struct pool *, void *, void *);
	void *pr_cookie;
	void *pr_item;
};

void		pool_init(struct pool *, size_t, u_int, int, int,
		    const char *, struct pool_allocator *);
void		pool_cache_init(struct pool *);
void		pool_destroy(struct pool *);
void		pool_setlowat(struct pool *, int);
void		pool_sethiwat(struct pool *, int);
int		pool_sethardlimit(struct pool *, u_int);
void		pool_set_constraints(struct pool *,
		    const struct kmem_pa_mode *mode);

void		*pool_get(struct pool *, int) __malloc;
void		pool_request_init(struct pool_request *,
		    void (*)(struct pool *, void *, void *), void *);
void		pool_request(struct pool *, struct pool_request *);
void		pool_put(struct pool *, void *);
void		pool_wakeup(struct pool *);
int		pool_reclaim(struct pool *);
void		pool_reclaim_all(void);
int		pool_prime(struct pool *, int);

#ifdef DDB
/*
 * Debugging and diagnostic aides.
 */
void		pool_printit(struct pool *, const char *,
		    int (*)(const char *, ...));
void		pool_walk(struct pool *, int, int (*)(const char *, ...),
		    void (*)(void *, int, int (*)(const char *, ...)));
#endif

/* the allocator for dma-able memory is a thin layer on top of pool  */
void		 dma_alloc_init(void);
void		*dma_alloc(size_t size, int flags);
void		 dma_free(void *m, size_t size);
#endif /* _KERNEL */

#endif /* _SYS_POOL_H_ */
