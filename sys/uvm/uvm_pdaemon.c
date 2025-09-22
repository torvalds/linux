/*	$OpenBSD: uvm_pdaemon.c,v 1.137 2025/06/02 18:49:04 claudio Exp $	*/
/*	$NetBSD: uvm_pdaemon.c,v 1.23 2000/08/20 10:24:14 bjh21 Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1991, 1993, The Regents of the University of California.
 *
 * All rights reserved.
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
 *	@(#)vm_pageout.c        8.5 (Berkeley) 2/14/94
 * from: Id: uvm_pdaemon.c,v 1.1.2.32 1998/02/06 05:26:30 chs Exp
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
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
 */

/*
 * uvm_pdaemon.c: the page daemon
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/atomic.h>

#ifdef HIBERNATE
#include <sys/hibernate.h>
#endif

#include <uvm/uvm.h>

#include "drm.h"

#if NDRM > 0
extern unsigned long drmbackoff(long);
#endif

/*
 * UVMPD_NUMDIRTYREACTS is how many dirty pages the pagedaemon will reactivate
 * in a pass thru the inactive list when swap is full.  the value should be
 * "small"... if it's too large we'll cycle the active pages thru the inactive
 * queue too quickly to for them to be referenced and avoid being freed.
 */

#define UVMPD_NUMDIRTYREACTS 16


/*
 * local prototypes
 */

struct rwlock	*uvmpd_trylockowner(struct vm_page *);
void		uvmpd_scan(struct uvm_pmalloc *, int, int);
int		uvmpd_scan_inactive(struct uvm_pmalloc *, int);
void		uvmpd_scan_active(struct uvm_pmalloc *, int, int);
void		uvmpd_tune(void);
void		uvmpd_drop(struct pglist *);
int		uvmpd_dropswap(struct vm_page *);

/*
 * uvm_wait: wait (sleep) for the page daemon to free some pages
 *
 * => should be called with all locks released
 * => should _not_ be called by the page daemon (to avoid deadlock)
 */

void
uvm_wait(const char *wmsg)
{
	uint64_t timo = INFSLP;

#ifdef DIAGNOSTIC
	if (curproc == &proc0)
		panic("%s: cannot sleep for memory during boot", __func__);
#endif

	/*
	 * check for page daemon going to sleep (waiting for itself)
	 */
	if (curproc == uvm.pagedaemon_proc) {
		printf("uvm_wait emergency bufbackoff\n");
		if (bufbackoff(NULL, 4) >= 4)
			return;
		/*
		 * now we have a problem: the pagedaemon wants to go to
		 * sleep until it frees more memory.   but how can it
		 * free more memory if it is asleep?  that is a deadlock.
		 * we have two options:
		 *  [1] panic now
		 *  [2] put a timeout on the sleep, thus causing the
		 *      pagedaemon to only pause (rather than sleep forever)
		 *
		 * note that option [2] will only help us if we get lucky
		 * and some other process on the system breaks the deadlock
		 * by exiting or freeing memory (thus allowing the pagedaemon
		 * to continue).  for now we panic if DEBUG is defined,
		 * otherwise we hope for the best with option [2] (better
		 * yet, this should never happen in the first place!).
		 */

		printf("pagedaemon: deadlock detected!\n");
		timo = MSEC_TO_NSEC(125);	/* set timeout */
#if defined(DEBUG)
		/* DEBUG: panic so we can debug it */
		panic("pagedaemon deadlock");
#endif
	}

	uvm_lock_fpageq();
	wakeup(&uvm.pagedaemon);		/* wake the daemon! */
	msleep_nsec(&uvmexp.free, &uvm.fpageqlock, PVM | PNORELOCK, wmsg, timo);
}

/*
 * uvmpd_tune: tune paging parameters
 */
void
uvmpd_tune(void)
{
	int val;

	val = uvmexp.npages / 30;

	/* XXX:  what are these values good for? */
	val = max(val, (16*1024) >> PAGE_SHIFT);

	/* Make sure there's always a user page free. */
	if (val < uvmexp.reserve_kernel + 1)
		val = uvmexp.reserve_kernel + 1;
	uvmexp.freemin = val;

	/* Calculate free target. */
	val = (uvmexp.freemin * 4) / 3;
	if (val <= uvmexp.freemin)
		val = uvmexp.freemin + 1;
	uvmexp.freetarg = val;

	uvmexp.wiredmax = uvmexp.npages / 3;
}

/*
 * Indicate to the page daemon that a nowait call failed and it should
 * recover at least some memory in the most restricted region (assumed
 * to be dma_constraint).
 */
struct uvm_pmalloc nowait_pma;

static inline int
uvmpd_pma_done(struct uvm_pmalloc *pma)
{
	if (pma == NULL || (pma->pm_flags & UVM_PMA_FREED))
		return 1;
	return 0;
}

/*
 * uvm_pageout: the main loop for the pagedaemon
 */
void
uvm_pageout(void *arg)
{
	struct uvm_constraint_range constraint;
	struct uvm_pmalloc *pma;
	int shortage, inactive_shortage;

	/* ensure correct priority and set paging parameters... */
	uvm.pagedaemon_proc = curproc;
	(void) spl0();
	uvmpd_tune();

	/*
	 * XXX realistically, this is what our nowait callers probably
	 * care about.
	 */
	nowait_pma.pm_constraint = dma_constraint;
	nowait_pma.pm_size = (16 << PAGE_SHIFT); /* XXX */
	nowait_pma.pm_flags = 0;

	for (;;) {
		long size;

		uvm_lock_fpageq();
		if (TAILQ_EMPTY(&uvm.pmr_control.allocs) || uvmexp.paging > 0) {
			msleep_nsec(&uvm.pagedaemon, &uvm.fpageqlock, PVM,
			    "pgdaemon", INFSLP);
			uvmexp.pdwoke++;
		}

		if ((pma = TAILQ_FIRST(&uvm.pmr_control.allocs)) != NULL) {
			pma->pm_flags |= UVM_PMA_BUSY;
			constraint = pma->pm_constraint;
		} else {
			constraint = no_constraint;
		}
		/* How many pages do we need to free during this round? */
		shortage = uvmexp.freetarg -
		    (uvmexp.free + uvmexp.paging) + BUFPAGES_DEFICIT;
		uvm_unlock_fpageq();

		/*
		 * now lock page queues and recompute inactive count
		 */
		uvm_lock_pageq();
		uvmexp.inactarg = (uvmexp.active + uvmexp.inactive) / 3;
		if (uvmexp.inactarg <= uvmexp.freetarg) {
			uvmexp.inactarg = uvmexp.freetarg + 1;
		}
		inactive_shortage =
			uvmexp.inactarg - uvmexp.inactive - BUFPAGES_INACT;
		uvm_unlock_pageq();

		size = 0;
		if (pma != NULL)
			size += pma->pm_size >> PAGE_SHIFT;
		if (shortage > 0)
			size += shortage;

		if (size == 0) {
			/*
			 * Since the inactive target just got updated
			 * above, both `size' and `inactive_shortage' can
			 * be 0.
			 */
			if (inactive_shortage) {
				uvm_lock_pageq();
				uvmpd_scan_active(NULL, 0, inactive_shortage);
				uvm_unlock_pageq();
			}
			continue;
		}

		/* Reclaim pages from the buffer cache if possible. */
		shortage -= bufbackoff(&constraint, size * 2);
#if NDRM > 0
		shortage -= drmbackoff(size * 2);
#endif
		if (shortage > 0)
			shortage -= uvm_pmr_cache_drain();

		/*
		 * scan if needed
		 */
		uvm_lock_pageq();
		if (!uvmpd_pma_done(pma) ||
		    (shortage > 0) || (inactive_shortage > 0)) {
			uvmpd_scan(pma, shortage, inactive_shortage);
		}

		/*
		 * if there's any free memory to be had,
		 * wake up any waiters.
		 */
		uvm_lock_fpageq();
		if (uvmexp.free > uvmexp.reserve_kernel || uvmexp.paging == 0) {
			wakeup(&uvmexp.free);
		}

		if (pma != NULL) {
			/* 
			 * XXX If UVM_PMA_FREED isn't set, no pages
			 * were freed.  Should we set UVM_PMA_FAIL in
			 * that case?
			 */
			pma->pm_flags &= ~UVM_PMA_BUSY;
			if (pma->pm_flags & UVM_PMA_FREED) {
				pma->pm_flags &= ~UVM_PMA_LINKED;
				TAILQ_REMOVE(&uvm.pmr_control.allocs, pma, pmq);
				wakeup(pma);
			}
		}
		uvm_unlock_fpageq();

		/*
		 * scan done.  unlock page queues (the only lock we are holding)
		 */
		uvm_unlock_pageq();

		sched_pause(yield);
	}
	/*NOTREACHED*/
}


/*
 * uvm_aiodone_daemon:  main loop for the aiodone daemon.
 */
void
uvm_aiodone_daemon(void *arg)
{
	int s, npages;
	struct buf *bp, *nbp;

	uvm.aiodoned_proc = curproc;
	KERNEL_UNLOCK();

	for (;;) {
		/*
		 * Check for done aio structures. If we've got structures to
		 * process, do so. Otherwise sleep while avoiding races.
		 */
		mtx_enter(&uvm.aiodoned_lock);
		while ((bp = TAILQ_FIRST(&uvm.aio_done)) == NULL)
			msleep_nsec(&uvm.aiodoned, &uvm.aiodoned_lock,
			    PVM, "aiodoned", INFSLP);
		/* Take the list for ourselves. */
		TAILQ_INIT(&uvm.aio_done);
		mtx_leave(&uvm.aiodoned_lock);

		/* process each i/o that's done. */
		npages = 0;
		KERNEL_LOCK();
		while (bp != NULL) {
			if (bp->b_flags & B_PDAEMON) {
				npages += bp->b_bufsize >> PAGE_SHIFT;
			}
			nbp = TAILQ_NEXT(bp, b_freelist);
			s = splbio();	/* b_iodone must by called at splbio */
			(*bp->b_iodone)(bp);
			splx(s);
			bp = nbp;

			sched_pause(yield);
		}
		KERNEL_UNLOCK();

		uvm_lock_fpageq();
		atomic_sub_int(&uvmexp.paging, npages);
		wakeup(uvmexp.free <= uvmexp.reserve_kernel ? &uvm.pagedaemon :
		    &uvmexp.free);
		uvm_unlock_fpageq();
	}
}

/*
 * uvmpd_trylockowner: trylock the page's owner.
 *
 * => return the locked rwlock on success.  otherwise, return NULL.
 */
struct rwlock *
uvmpd_trylockowner(struct vm_page *pg)
{

	struct uvm_object *uobj = pg->uobject;
	struct rwlock *slock;

	if (uobj != NULL) {
		slock = uobj->vmobjlock;
	} else {
		struct vm_anon *anon = pg->uanon;

		KASSERT(anon != NULL);
		slock = anon->an_lock;
	}

	if (rw_enter(slock, RW_WRITE|RW_NOSLEEP)) {
		return NULL;
	}

	return slock;
}

/*
 * uvmpd_dropswap: free any swap allocated to this page.
 *
 * => called with owner locked.
 * => return 1 if a page had an associated slot.
 */
int
uvmpd_dropswap(struct vm_page *pg)
{
	struct vm_anon *anon = pg->uanon;
	int slot, result = 0;

	if ((pg->pg_flags & PQ_ANON) && anon->an_swslot) {
		uvm_swap_free(anon->an_swslot, 1);
		anon->an_swslot = 0;
		result = 1;
	} else if (pg->pg_flags & PQ_AOBJ) {
		slot = uao_dropswap(pg->uobject, pg->offset >> PAGE_SHIFT);
		if (slot)
			result = 1;
	}

	return result;
}

/*
 * Return 1 if the page `p' belongs to the memory range described by
 * 'constraint', 0 otherwise.
 */
static inline int
uvmpd_match_constraint(struct vm_page *p,
    struct uvm_constraint_range *constraint)
{
	paddr_t paddr;

	paddr = atop(VM_PAGE_TO_PHYS(p));
	if (paddr >= constraint->ucr_low && paddr < constraint->ucr_high)
		return 1;

	return 0;
}

struct vm_page *
uvmpd_iterator(struct pglist *pglst, struct vm_page *p, struct vm_page *iter)
{
	struct vm_page *nextpg = NULL;

	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	/* p is null to signal final swap i/o. */
	if (p == NULL)
		return NULL;

	do {
		nextpg = TAILQ_NEXT(iter, pageq);
	} while (nextpg && (nextpg->pg_flags & PQ_ITER));

	if (nextpg) {
		TAILQ_REMOVE(pglst, iter, pageq);
		TAILQ_INSERT_AFTER(pglst, nextpg, iter, pageq);
	}

	return nextpg;
}

/*
 * uvmpd_scan_inactive: scan an inactive list for pages to clean or free.
 *
 * => called with page queues locked
 * => we work on meeting our free target by converting inactive pages
 *    into free pages.
 * => we handle the building of swap-backed clusters
 * => we return TRUE if we are exiting because we met our target
 */
int
uvmpd_scan_inactive(struct uvm_pmalloc *pma, int shortage)
{
	struct pglist *pglst = &uvm.page_inactive;
	int result, freed = 0;
	struct vm_page *p, iter = { .pg_flags = PQ_ITER };
	struct uvm_object *uobj;
	struct vm_page *pps[SWCLUSTPAGES], **ppsp;
	int npages;
	struct vm_page *swpps[SWCLUSTPAGES]; 	/* XXX: see below */
	struct rwlock *slock;
	int swnpages, swcpages;				/* XXX: see below */
	int swslot;
	struct vm_anon *anon;
	boolean_t swap_backed;
	vaddr_t start;
	int dirtyreacts;

	/*
	 * swslot is non-zero if we are building a swap cluster.  we want
	 * to stay in the loop while we have a page to scan or we have
	 * a swap-cluster to build.
	 */
	swslot = 0;
	swnpages = swcpages = 0;
	dirtyreacts = 0;
	p = NULL;

	/*
	 * If a thread is waiting for us to release memory from a specific
	 * memory range start with the first page on the list that fits in
	 * it.
	 */
	TAILQ_FOREACH(p, pglst, pageq) {
		if (uvmpd_pma_done(pma) ||
		    uvmpd_match_constraint(p, &pma->pm_constraint))
			break;
	}

	if (p == NULL)
		return 0;

	/* Insert iterator. */
	TAILQ_INSERT_AFTER(pglst, p, &iter, pageq);
	for (; p != NULL || swslot != 0; p = uvmpd_iterator(pglst, p, &iter)) {
		/*
		 * note that p can be NULL iff we have traversed the whole
		 * list and need to do one final swap-backed clustered pageout.
		 */
		uobj = NULL;
		anon = NULL;
		if (p) {
			/*
			 * see if we've met our target
			 */
			if ((uvmpd_pma_done(pma) &&
			    (uvmexp.paging >= (shortage - freed))) ||
			    dirtyreacts == UVMPD_NUMDIRTYREACTS) {
				if (swslot == 0) {
					/* exit now if no swap-i/o pending */
					break;
				}

				/* set p to null to signal final swap i/o */
				p = NULL;
			}
		}
		if (p) {	/* if (we have a new page to consider) */
			/*
			 * we are below target and have a new page to consider.
			 */
			uvmexp.pdscans++;

			/*
			 * If we are not short on memory and only interested
			 * in releasing pages from a given memory range, do not
			 * bother with other pages.
			 */
			if (uvmexp.paging >= (shortage - freed) &&
			    !uvmpd_pma_done(pma) &&
			    !uvmpd_match_constraint(p, &pma->pm_constraint))
				continue;

			anon = p->uanon;
			uobj = p->uobject;

			/*
			 * first we attempt to lock the object that this page
			 * belongs to.  if our attempt fails we skip on to
			 * the next page (no harm done).  it is important to
			 * "try" locking the object as we are locking in the
			 * wrong order (pageq -> object) and we don't want to
			 * deadlock.
			 */
			slock = uvmpd_trylockowner(p);
			if (slock == NULL) {
				continue;
			}

			/*
			 * move referenced pages back to active queue
			 * and skip to next page.
			 */
			if (pmap_is_referenced(p)) {
				uvm_pageactivate(p);
				rw_exit(slock);
				uvmexp.pdreact++;
				continue;
			}

			if (p->pg_flags & PG_BUSY) {
				rw_exit(slock);
				uvmexp.pdbusy++;
				continue;
			}

			/* does the page belong to an object? */
			if (uobj != NULL) {
				uvmexp.pdobscan++;
			} else {
				KASSERT(anon != NULL);
				uvmexp.pdanscan++;
			}

			/*
			 * we now have the page queues locked.
			 * the page is not busy.   if the page is clean we
			 * can free it now and continue.
			 */
			if (p->pg_flags & PG_CLEAN) {
				if (p->pg_flags & PQ_SWAPBACKED) {
					/* this page now lives only in swap */
					atomic_inc_int(&uvmexp.swpgonly);
				}

				/* zap all mappings with pmap_page_protect... */
				pmap_page_protect(p, PROT_NONE);
				/* dequeue first to prevent lock recursion */
				uvm_pagedequeue(p);
				uvm_pagefree(p);
				freed++;

				if (anon) {

					/*
					 * an anonymous page can only be clean
					 * if it has backing store assigned.
					 */

					KASSERT(anon->an_swslot != 0);

					/* remove from object */
					anon->an_page = NULL;
				}
				rw_exit(slock);
				continue;
			}

			/*
			 * this page is dirty, skip it if we'll have met our
			 * free target when all the current pageouts complete.
			 */
			if (uvmpd_pma_done(pma) &&
			    (uvmexp.paging > (shortage - freed))) {
				rw_exit(slock);
				continue;
			}

			/*
			 * this page is dirty, but we can't page it out
			 * since all pages in swap are only in swap.
			 * reactivate it so that we eventually cycle
			 * all pages thru the inactive queue.
			 */
			if ((p->pg_flags & PQ_SWAPBACKED) && uvm_swapisfull()) {
				dirtyreacts++;
				uvm_pageactivate(p);
				rw_exit(slock);
				continue;
			}

			/*
			 * if the page is swap-backed and dirty and swap space
			 * is full, free any swap allocated to the page
			 * so that other pages can be paged out.
			 */
			if ((p->pg_flags & PQ_SWAPBACKED) && uvm_swapisfilled())
				uvmpd_dropswap(p);

			/*
			 * the page we are looking at is dirty.   we must
			 * clean it before it can be freed.  to do this we
			 * first mark the page busy so that no one else will
			 * touch the page.   we write protect all the mappings
			 * of the page so that no one touches it while it is
			 * in I/O.
			 */

			swap_backed = ((p->pg_flags & PQ_SWAPBACKED) != 0);
			atomic_setbits_int(&p->pg_flags, PG_BUSY);
			UVM_PAGE_OWN(p, "scan_inactive");
			pmap_page_protect(p, PROT_READ);
			uvmexp.pgswapout++;

			/*
			 * for swap-backed pages we need to (re)allocate
			 * swap space.
			 */
			if (swap_backed) {
				/* free old swap slot (if any) */
				uvmpd_dropswap(p);

				/* start new cluster (if necessary) */
				if (swslot == 0) {
					swnpages = SWCLUSTPAGES;
					swslot = uvm_swap_alloc(&swnpages,
					    TRUE);
					if (swslot == 0) {
						/* no swap?  give up! */
						atomic_clearbits_int(
						    &p->pg_flags,
						    PG_BUSY);
						UVM_PAGE_OWN(p, NULL);
						rw_exit(slock);
						continue;
					}
					swcpages = 0;	/* cluster is empty */
				}

				/* add block to cluster */
				swpps[swcpages] = p;
				if (anon)
					anon->an_swslot = swslot + swcpages;
				else
					uao_set_swslot(uobj,
					    p->offset >> PAGE_SHIFT,
					    swslot + swcpages);
				swcpages++;
				rw_exit(slock);

				/* cluster not full yet? */
				if (swcpages < swnpages)
					continue;
			}
		} else {
			/* if p == NULL we must be doing a last swap i/o */
			swap_backed = TRUE;
		}

		/*
		 * now consider doing the pageout.
		 *
		 * for swap-backed pages, we do the pageout if we have either
		 * filled the cluster (in which case (swnpages == swcpages) or
		 * run out of pages (p == NULL).
		 *
		 * for object pages, we always do the pageout.
		 */
		if (swap_backed) {
			/* starting I/O now... set up for it */
			npages = swcpages;
			ppsp = swpps;
			/* for swap-backed pages only */
			start = (vaddr_t) swslot;

			/* if this is final pageout we could have a few
			 * extra swap blocks */
			if (swcpages < swnpages) {
				uvm_swap_free(swslot + swcpages,
				    (swnpages - swcpages));
			}
		} else {
			/* normal object pageout */
			ppsp = pps;
			npages = sizeof(pps) / sizeof(struct vm_page *);
			/* not looked at because PGO_ALLPAGES is set */
			start = 0;
		}

		/*
		 * now do the pageout.
		 *
		 * for swap_backed pages we have already built the cluster.
		 * for !swap_backed pages, uvm_pager_put will call the object's
		 * "make put cluster" function to build a cluster on our behalf.
		 *
		 * we pass the PGO_PDFREECLUST flag to uvm_pager_put to instruct
		 * it to free the cluster pages for us on a successful I/O (it
		 * always does this for un-successful I/O requests).  this
		 * allows us to do clustered pageout without having to deal
		 * with cluster pages at this level.
		 *
		 * note locking semantics of uvm_pager_put with PGO_PDFREECLUST:
		 *  IN: locked: page queues
		 * OUT: locked: 
		 *     !locked: pageqs
		 */

		uvmexp.pdpageouts++;
		result = uvm_pager_put(swap_backed ? NULL : uobj, p,
		    &ppsp, &npages, PGO_ALLPAGES|PGO_PDFREECLUST, start, 0);

		/*
		 * if we did i/o to swap, zero swslot to indicate that we are
		 * no longer building a swap-backed cluster.
		 */

		if (swap_backed)
			swslot = 0;		/* done with this cluster */

		/*
		 * first, we check for VM_PAGER_PEND which means that the
		 * async I/O is in progress and the async I/O done routine
		 * will clean up after us.   in this case we move on to the
		 * next page.
		 */
		if (result == VM_PAGER_PEND) {
			atomic_add_int(&uvmexp.paging, npages);
			uvm_lock_pageq();
			uvmexp.pdpending++;
			continue;
		}

		/* clean up "p" if we have one */
		if (p) {
			/*
			 * the I/O request to "p" is done and uvm_pager_put
			 * has freed any cluster pages it may have allocated
			 * during I/O.  all that is left for us to do is
			 * clean up page "p" (which is still PG_BUSY).
			 *
			 * our result could be one of the following:
			 *   VM_PAGER_OK: successful pageout
			 *
			 *   VM_PAGER_AGAIN: tmp resource shortage, we skip
			 *     to next page
			 *   VM_PAGER_{FAIL,ERROR,BAD}: an error.   we
			 *     "reactivate" page to get it out of the way (it
			 *     will eventually drift back into the inactive
			 *     queue for a retry).
			 *   VM_PAGER_UNLOCK: should never see this as it is
			 *     only valid for "get" operations
			 */

			/* relock p's object: page queues not lock yet, so
			 * no need for "try" */

			/* !swap_backed case: already locked... */
			if (swap_backed) {
				rw_enter(slock, RW_WRITE);
			}

#ifdef DIAGNOSTIC
			if (result == VM_PAGER_UNLOCK)
				panic("pagedaemon: pageout returned "
				    "invalid 'unlock' code");
#endif

			/* handle PG_WANTED now */
			if (p->pg_flags & PG_WANTED)
				wakeup(p);

			atomic_clearbits_int(&p->pg_flags, PG_BUSY|PG_WANTED);
			UVM_PAGE_OWN(p, NULL);

			/* released during I/O? Can only happen for anons */
			if (p->pg_flags & PG_RELEASED) {
				KASSERT(anon != NULL);
				/*
				 * remove page so we can get nextpg,
				 * also zero out anon so we don't use
				 * it after the free.
				 */
				anon->an_page = NULL;
				p->uanon = NULL;

				uvm_anfree(anon);	/* kills anon */
				pmap_page_protect(p, PROT_NONE);
				anon = NULL;
				uvm_lock_pageq();
				/* dequeue first to prevent lock recursion */
				uvm_pagedequeue(p);
				/* free released page */
				uvm_pagefree(p);
			} else {	/* page was not released during I/O */
				uvm_lock_pageq();
				if (result != VM_PAGER_OK) {
					/* pageout was a failure... */
					if (result != VM_PAGER_AGAIN)
						uvm_pageactivate(p);
					pmap_clear_reference(p);
				} else {
					/* pageout was a success... */
					pmap_clear_reference(p);
					pmap_clear_modify(p);
					atomic_setbits_int(&p->pg_flags,
					    PG_CLEAN);
				}
			}
			rw_exit(slock);
		} else {
			/*
			 * lock page queues here just so they're always locked
			 * at the end of the loop.
			 */
			uvm_lock_pageq();
		}
	}
	TAILQ_REMOVE(pglst, &iter, pageq);

	return freed;
}

/*
 * uvmpd_scan: scan the page queues and attempt to meet our targets.
 *
 * => called with pageq's locked
 */

void
uvmpd_scan(struct uvm_pmalloc *pma, int shortage, int inactive_shortage)
{
	int swap_shortage, pages_freed;

	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	uvmexp.pdrevs++;		/* counter */

	/*
	 * now we want to work on meeting our targets.   first we work on our
	 * free target by converting inactive pages into free pages.  then
	 * we work on meeting our inactive target by converting active pages
	 * to inactive ones.
	 */
	pages_freed = uvmpd_scan_inactive(pma, shortage);
	uvmexp.pdfreed += pages_freed;
	shortage -= pages_freed;

	/*
	 * we have done the scan to get free pages.   now we work on meeting
	 * our inactive target.
	 *
	 * detect if we're not going to be able to page anything out
	 * until we free some swap resources from active pages.
	 */
	swap_shortage = 0;
	if ((shortage > 0) && uvm_swapisfilled() && !uvm_swapisfull() &&
	    pages_freed == 0) {
		swap_shortage = shortage;
	}

	uvmpd_scan_active(pma, swap_shortage, inactive_shortage);
}

void
uvmpd_scan_active(struct uvm_pmalloc *pma, int swap_shortage,
    int inactive_shortage)
{
	struct vm_page *p, *nextpg;
	struct rwlock *slock;

	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	for (p = TAILQ_FIRST(&uvm.page_active);
	     p != NULL && (inactive_shortage > 0 || swap_shortage > 0);
	     p = nextpg) {
		nextpg = TAILQ_NEXT(p, pageq);
		if (p->pg_flags & PG_BUSY) {
			continue;
		}

		/*
		 * If we couldn't release enough pages from a given memory
		 * range try to deactivate them first...
		 *
		 * ...unless we are low on swap slots, in such case we are
		 * probably OOM and want to release swap resources as quickly
		 * as possible.
		 */
		if (inactive_shortage > 0 && swap_shortage == 0 &&
		    !uvmpd_pma_done(pma) &&
		    !uvmpd_match_constraint(p, &pma->pm_constraint))
			continue;

		/*
		 * lock the page's owner.
		 */
		slock = uvmpd_trylockowner(p);
		if (slock == NULL) {
			continue;
		}

		/*
		 * skip this page if it's busy.
		 */
		if ((p->pg_flags & PG_BUSY) != 0) {
			rw_exit(slock);
			continue;
		}

		/*
		 * if there's a shortage of swap, free any swap allocated
		 * to this page so that other pages can be paged out.
		 */
		if (swap_shortage > 0) {
			if (uvmpd_dropswap(p)) {
				atomic_clearbits_int(&p->pg_flags, PG_CLEAN);
				swap_shortage--;
			}
		}

		/*
		 * deactivate this page if there's a shortage of
		 * inactive pages.
		 */
		if (inactive_shortage > 0) {
			/* no need to check wire_count as pg is "active" */
			uvm_pagedeactivate(p);
			uvmexp.pddeact++;
			inactive_shortage--;
		}

		/*
		 * we're done with this page.
		 */
		rw_exit(slock);
	}
}

#ifdef HIBERNATE

/*
 * uvmpd_drop: drop clean pages from list
 */
void
uvmpd_drop(struct pglist *pglst)
{
	struct vm_page *p, *nextpg;

	for (p = TAILQ_FIRST(pglst); p != NULL; p = nextpg) {
		nextpg = TAILQ_NEXT(p, pageq);

		if (p->pg_flags & PQ_ANON || p->uobject == NULL)
			continue;

		if (p->pg_flags & PG_BUSY)
			continue;

		if (p->pg_flags & PG_CLEAN) {
			struct uvm_object * uobj = p->uobject;

			rw_enter(uobj->vmobjlock, RW_WRITE);
			/*
			 * we now have the page queues locked.
			 * the page is not busy.   if the page is clean we
			 * can free it now and continue.
			 */
			if (p->pg_flags & PG_CLEAN) {
				if (p->pg_flags & PQ_SWAPBACKED) {
					/* this page now lives only in swap */
					atomic_inc_int(&uvmexp.swpgonly);
				}

				/* zap all mappings with pmap_page_protect... */
				pmap_page_protect(p, PROT_NONE);
				uvm_pagefree(p);
			}
			rw_exit(uobj->vmobjlock);
		}
	}
}

void
uvmpd_hibernate(void)
{
	uvmpd_drop(&uvm.page_inactive);
	uvmpd_drop(&uvm.page_active);
}

#endif
