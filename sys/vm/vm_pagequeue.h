/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)vm_page.h	8.2 (Berkeley) 12/13/93
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD$
 */

#ifndef	_VM_PAGEQUEUE_
#define	_VM_PAGEQUEUE_

#ifdef _KERNEL
struct vm_pagequeue {
	struct mtx	pq_mutex;
	struct pglist	pq_pl;
	int		pq_cnt;
	const char	* const pq_name;
	uint64_t	pq_pdpages;
} __aligned(CACHE_LINE_SIZE);

#ifndef VM_BATCHQUEUE_SIZE
#define	VM_BATCHQUEUE_SIZE	7
#endif

struct vm_batchqueue {
	vm_page_t	bq_pa[VM_BATCHQUEUE_SIZE];
	int		bq_cnt;
} __aligned(CACHE_LINE_SIZE);

#include <vm/uma.h>
#include <sys/pidctrl.h>
struct sysctl_oid;

/*
 * One vm_domain per-numa domain.  Contains pagequeues, free page structures,
 * and accounting.
 *
 * Lock Key:
 * f	vmd_free_mtx
 * p	vmd_pageout_mtx
 * d	vm_domainset_lock
 * a	atomic
 * c	const after boot
 * q	page queue lock
*/
struct vm_domain {
	struct vm_pagequeue vmd_pagequeues[PQ_COUNT];
	struct mtx_padalign vmd_free_mtx;
	struct mtx_padalign vmd_pageout_mtx;
	uma_zone_t vmd_pgcache;		/* (c) page free cache. */
	struct vmem *vmd_kernel_arena;	/* (c) per-domain kva R/W arena. */
	struct vmem *vmd_kernel_rwx_arena; /* (c) per-domain kva R/W/X arena. */
	u_int vmd_domain;		/* (c) Domain number. */
	u_int vmd_page_count;		/* (c) Total page count. */
	long vmd_segs;			/* (c) bitmask of the segments */
	u_int __aligned(CACHE_LINE_SIZE) vmd_free_count; /* (a,f) free page count */
	u_int vmd_pageout_deficit;	/* (a) Estimated number of pages deficit */
	uint8_t vmd_pad[CACHE_LINE_SIZE - (sizeof(u_int) * 2)];

	/* Paging control variables, used within single threaded page daemon. */
	struct pidctrl vmd_pid;		/* Pageout controller. */
	boolean_t vmd_oom;
	int vmd_oom_seq;
	int vmd_last_active_scan;
	struct vm_page vmd_markers[PQ_COUNT]; /* (q) markers for queue scans */
	struct vm_page vmd_inacthead; /* marker for LRU-defeating insertions */
	struct vm_page vmd_clock[2]; /* markers for active queue scan */

	int vmd_pageout_wanted;		/* (a, p) pageout daemon wait channel */
	int vmd_pageout_pages_needed;	/* (d) page daemon waiting for pages? */
	bool vmd_minset;		/* (d) Are we in vm_min_domains? */
	bool vmd_severeset;		/* (d) Are we in vm_severe_domains? */
	enum {
		VM_LAUNDRY_IDLE = 0,
		VM_LAUNDRY_BACKGROUND,
		VM_LAUNDRY_SHORTFALL
	} vmd_laundry_request;

	/* Paging thresholds and targets. */
	u_int vmd_clean_pages_freed;	/* (q) accumulator for laundry thread */
	u_int vmd_background_launder_target; /* (c) */
	u_int vmd_free_reserved;	/* (c) pages reserved for deadlock */
	u_int vmd_free_target;		/* (c) pages desired free */
	u_int vmd_free_min;		/* (c) pages desired free */
	u_int vmd_inactive_target;	/* (c) pages desired inactive */
	u_int vmd_pageout_free_min;	/* (c) min pages reserved for kernel */
	u_int vmd_pageout_wakeup_thresh;/* (c) min pages to wake pagedaemon */
	u_int vmd_interrupt_free_min;	/* (c) reserved pages for int code */
	u_int vmd_free_severe;		/* (c) severe page depletion point */

	/* Name for sysctl etc. */
	struct sysctl_oid *vmd_oid;
	char vmd_name[sizeof(__XSTRING(MAXMEMDOM))];
} __aligned(CACHE_LINE_SIZE);

extern struct vm_domain vm_dom[MAXMEMDOM];

#define	VM_DOMAIN(n)		(&vm_dom[(n)])
#define	VM_DOMAIN_EMPTY(n)	(vm_dom[(n)].vmd_page_count == 0)

#define	vm_pagequeue_assert_locked(pq)	mtx_assert(&(pq)->pq_mutex, MA_OWNED)
#define	vm_pagequeue_lock(pq)		mtx_lock(&(pq)->pq_mutex)
#define	vm_pagequeue_lockptr(pq)	(&(pq)->pq_mutex)
#define	vm_pagequeue_trylock(pq)	mtx_trylock(&(pq)->pq_mutex)
#define	vm_pagequeue_unlock(pq)		mtx_unlock(&(pq)->pq_mutex)

#define	vm_domain_free_assert_locked(n)					\
	    mtx_assert(vm_domain_free_lockptr((n)), MA_OWNED)
#define	vm_domain_free_assert_unlocked(n)				\
	    mtx_assert(vm_domain_free_lockptr((n)), MA_NOTOWNED)
#define	vm_domain_free_lock(d)						\
	    mtx_lock(vm_domain_free_lockptr((d)))
#define	vm_domain_free_lockptr(d)					\
	    (&(d)->vmd_free_mtx)
#define	vm_domain_free_trylock(d)					\
	    mtx_trylock(vm_domain_free_lockptr((d)))
#define	vm_domain_free_unlock(d)					\
	    mtx_unlock(vm_domain_free_lockptr((d)))

#define	vm_domain_pageout_lockptr(d)					\
	    (&(d)->vmd_pageout_mtx)
#define	vm_domain_pageout_assert_locked(n)				\
	    mtx_assert(vm_domain_pageout_lockptr((n)), MA_OWNED)
#define	vm_domain_pageout_assert_unlocked(n)				\
	    mtx_assert(vm_domain_pageout_lockptr((n)), MA_NOTOWNED)
#define	vm_domain_pageout_lock(d)					\
	    mtx_lock(vm_domain_pageout_lockptr((d)))
#define	vm_domain_pageout_unlock(d)					\
	    mtx_unlock(vm_domain_pageout_lockptr((d)))

static __inline void
vm_pagequeue_cnt_add(struct vm_pagequeue *pq, int addend)
{

	vm_pagequeue_assert_locked(pq);
	pq->pq_cnt += addend;
}
#define	vm_pagequeue_cnt_inc(pq)	vm_pagequeue_cnt_add((pq), 1)
#define	vm_pagequeue_cnt_dec(pq)	vm_pagequeue_cnt_add((pq), -1)

static inline void
vm_batchqueue_init(struct vm_batchqueue *bq)
{

	bq->bq_cnt = 0;
}

static inline bool
vm_batchqueue_insert(struct vm_batchqueue *bq, vm_page_t m)
{

	if (bq->bq_cnt < nitems(bq->bq_pa)) {
		bq->bq_pa[bq->bq_cnt++] = m;
		return (true);
	}
	return (false);
}

static inline vm_page_t
vm_batchqueue_pop(struct vm_batchqueue *bq)
{

	if (bq->bq_cnt == 0)
		return (NULL);
	return (bq->bq_pa[--bq->bq_cnt]);
}

void vm_domain_set(struct vm_domain *vmd);
void vm_domain_clear(struct vm_domain *vmd);
int vm_domain_allocate(struct vm_domain *vmd, int req, int npages);

/*
 *      vm_pagequeue_domain:
 *
 *      Return the memory domain the page belongs to.
 */
static inline struct vm_domain *
vm_pagequeue_domain(vm_page_t m)
{

	return (VM_DOMAIN(vm_phys_domain(m)));
}

/*
 * Return the number of pages we need to free-up or cache
 * A positive number indicates that we do not have enough free pages.
 */
static inline int
vm_paging_target(struct vm_domain *vmd)
{

	return (vmd->vmd_free_target - vmd->vmd_free_count);
}

/*
 * Returns TRUE if the pagedaemon needs to be woken up.
 */
static inline int
vm_paging_needed(struct vm_domain *vmd, u_int free_count)
{

	return (free_count < vmd->vmd_pageout_wakeup_thresh);
}

/*
 * Returns TRUE if the domain is below the min paging target.
 */
static inline int
vm_paging_min(struct vm_domain *vmd)
{

        return (vmd->vmd_free_min > vmd->vmd_free_count);
}

/*
 * Returns TRUE if the domain is below the severe paging target.
 */
static inline int
vm_paging_severe(struct vm_domain *vmd)
{

        return (vmd->vmd_free_severe > vmd->vmd_free_count);
}

/*
 * Return the number of pages we need to launder.
 * A positive number indicates that we have a shortfall of clean pages.
 */
static inline int
vm_laundry_target(struct vm_domain *vmd)
{

	return (vm_paging_target(vmd));
}

void pagedaemon_wakeup(int domain);

static inline void
vm_domain_freecnt_inc(struct vm_domain *vmd, int adj)
{
	u_int old, new;

	old = atomic_fetchadd_int(&vmd->vmd_free_count, adj);
	new = old + adj;
	/*
	 * Only update bitsets on transitions.  Notice we short-circuit the
	 * rest of the checks if we're above min already.
	 */
	if (old < vmd->vmd_free_min && (new >= vmd->vmd_free_min ||
	    (old < vmd->vmd_free_severe && new >= vmd->vmd_free_severe) ||
	    (old < vmd->vmd_pageout_free_min &&
	    new >= vmd->vmd_pageout_free_min)))
		vm_domain_clear(vmd);
}

#endif	/* _KERNEL */
#endif				/* !_VM_PAGEQUEUE_ */
