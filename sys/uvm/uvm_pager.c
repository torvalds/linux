/*	$OpenBSD: uvm_pager.c,v 1.94 2025/03/10 14:13:58 mpi Exp $	*/
/*	$NetBSD: uvm_pager.c,v 1.36 2000/11/27 18:26:41 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * from: Id: uvm_pager.c,v 1.1.2.23 1998/02/02 20:38:06 chuck Exp
 */

/*
 * uvm_pager.c: generic functions used to assist the pagers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/buf.h>
#include <sys/atomic.h>

#include <uvm/uvm.h>

const struct uvm_pagerops *uvmpagerops[] = {
	&aobj_pager,
	&uvm_deviceops,
	&uvm_vnodeops,
};

/*
 * the pager map: provides KVA for I/O
 *
 * Each uvm_pseg has room for MAX_PAGERMAP_SEGS pager io space of
 * MAXBSIZE bytes.
 *
 * The number of uvm_pseg instances is dynamic using an array segs.
 * At most UVM_PSEG_COUNT instances can exist.
 *
 * psegs[0/1] always exist (so that the pager can always map in pages).
 * psegs[0/1] element 0 are always reserved for the pagedaemon.
 *
 * Any other pseg is automatically created when no space is available
 * and automatically destroyed when it is no longer in use.
 */
#define MAX_PAGER_SEGS	16
#define PSEG_NUMSEGS	(PAGER_MAP_SIZE / MAX_PAGER_SEGS / MAXBSIZE)
struct uvm_pseg {
	/* Start of virtual space; 0 if not inited. */
	vaddr_t	start;
	/* Bitmap of the segments in use in this pseg. */
	int	use;
};
struct	mutex uvm_pseg_lck;
struct	uvm_pseg psegs[PSEG_NUMSEGS];

#define UVM_PSEG_FULL(pseg)	((pseg)->use == (1 << MAX_PAGER_SEGS) - 1)
#define UVM_PSEG_EMPTY(pseg)	((pseg)->use == 0)
#define UVM_PSEG_INUSE(pseg,id)	(((pseg)->use & (1 << (id))) != 0)

void		uvm_pseg_init(struct uvm_pseg *);
vaddr_t		uvm_pseg_get(int);
void		uvm_pseg_release(vaddr_t);

/*
 * uvm_pager_init: init pagers (at boot time)
 */
void
uvm_pager_init(void)
{
	int lcv;

	/* init pager map */
	uvm_pseg_init(&psegs[0]);
	uvm_pseg_init(&psegs[1]);
	mtx_init(&uvm_pseg_lck, IPL_VM);

	/* init ASYNC I/O queue */
	TAILQ_INIT(&uvm.aio_done);

	/* call pager init functions */
	for (lcv = 0 ; lcv < sizeof(uvmpagerops)/sizeof(struct uvm_pagerops *);
	    lcv++) {
		if (uvmpagerops[lcv]->pgo_init)
			uvmpagerops[lcv]->pgo_init();
	}
}

/*
 * Initialize a uvm_pseg.
 *
 * May fail, in which case seg->start == 0.
 *
 * Caller locks uvm_pseg_lck.
 */
void
uvm_pseg_init(struct uvm_pseg *pseg)
{
	KASSERT(pseg->start == 0);
	KASSERT(pseg->use == 0);
	pseg->start = (vaddr_t)km_alloc(MAX_PAGER_SEGS * MAXBSIZE,
	    &kv_any, &kp_none, &kd_trylock);
}

/*
 * Acquire a pager map segment.
 *
 * Returns a vaddr for paging. 0 on failure.
 *
 * Caller does not lock.
 */
vaddr_t
uvm_pseg_get(int flags)
{
	int i;
	struct uvm_pseg *pseg;

	mtx_enter(&uvm_pseg_lck);

pager_seg_restart:
	/* Find first pseg that has room. */
	for (pseg = &psegs[0]; pseg != &psegs[PSEG_NUMSEGS]; pseg++) {
		if (UVM_PSEG_FULL(pseg))
			continue;

		if (pseg->start == 0) {
			/* Need initialization. */
			uvm_pseg_init(pseg);
			if (pseg->start == 0)
				goto pager_seg_fail;
		}

		/* Keep indexes 0,1 reserved for pagedaemon. */
		if ((pseg == &psegs[0] || pseg == &psegs[1]) &&
		    (curproc != uvm.pagedaemon_proc))
			i = 2;
		else
			i = 0;

		for (; i < MAX_PAGER_SEGS; i++) {
			if (!UVM_PSEG_INUSE(pseg, i)) {
				pseg->use |= 1 << i;
				mtx_leave(&uvm_pseg_lck);
				return pseg->start + i * MAXBSIZE;
			}
		}
	}

pager_seg_fail:
	if ((flags & UVMPAGER_MAPIN_WAITOK) != 0) {
		msleep_nsec(&psegs, &uvm_pseg_lck, PVM, "pagerseg", INFSLP);
		goto pager_seg_restart;
	}

	mtx_leave(&uvm_pseg_lck);
	return 0;
}

/*
 * Release a pager map segment.
 *
 * Caller does not lock.
 *
 * Deallocates pseg if it is no longer in use.
 */
void
uvm_pseg_release(vaddr_t segaddr)
{
	int id;
	struct uvm_pseg *pseg;
	vaddr_t va = 0;

	mtx_enter(&uvm_pseg_lck);
	for (pseg = &psegs[0]; pseg != &psegs[PSEG_NUMSEGS]; pseg++) {
		if (pseg->start <= segaddr &&
		    segaddr < pseg->start + MAX_PAGER_SEGS * MAXBSIZE)
			break;
	}
	KASSERT(pseg != &psegs[PSEG_NUMSEGS]);

	id = (segaddr - pseg->start) / MAXBSIZE;
	KASSERT(id >= 0 && id < MAX_PAGER_SEGS);

	/* test for no remainder */
	KDASSERT(segaddr == pseg->start + id * MAXBSIZE);


	KASSERT(UVM_PSEG_INUSE(pseg, id));

	pseg->use &= ~(1 << id);
	wakeup(&psegs);

	if ((pseg != &psegs[0] && pseg != &psegs[1]) && UVM_PSEG_EMPTY(pseg)) {
		va = pseg->start;
		pseg->start = 0;
	}

	mtx_leave(&uvm_pseg_lck);

	if (va) {
		km_free((void *)va, MAX_PAGER_SEGS * MAXBSIZE,
		    &kv_any, &kp_none);
	}
}

/*
 * uvm_pagermapin: map pages into KVA for I/O that needs mappings
 *
 * We basically just km_valloc a blank map entry to reserve the space in the
 * kernel map and then use pmap_enter() to put the mappings in by hand.
 */
vaddr_t
uvm_pagermapin(struct vm_page **pps, int npages, int flags)
{
	vaddr_t kva, cva;
	vm_prot_t prot;
	vsize_t size;
	struct vm_page *pp;

#if defined(__HAVE_PMAP_DIRECT)
	/*
	 * Use direct mappings for single page, unless there is a risk
	 * of aliasing.
	 */
	if (npages == 1 && PMAP_PREFER_ALIGN() == 0) {
		KASSERT(pps[0]);
		KASSERT(pps[0]->pg_flags & PG_BUSY);
		return pmap_map_direct(pps[0]);
	}
#endif

	prot = PROT_READ;
	if (flags & UVMPAGER_MAPIN_READ)
		prot |= PROT_WRITE;
	size = ptoa(npages);

	KASSERT(size <= MAXBSIZE);

	kva = uvm_pseg_get(flags);
	if (kva == 0)
		return 0;

	for (cva = kva ; size != 0 ; size -= PAGE_SIZE, cva += PAGE_SIZE) {
		pp = *pps++;
		KASSERT(pp);
		KASSERT(pp->pg_flags & PG_BUSY);
		/* Allow pmap_enter to fail. */
		if (pmap_enter(pmap_kernel(), cva, VM_PAGE_TO_PHYS(pp),
		    prot, PMAP_WIRED | PMAP_CANFAIL | prot) != 0) {
			pmap_remove(pmap_kernel(), kva, cva);
			pmap_update(pmap_kernel());
			uvm_pseg_release(kva);
			return 0;
		}
	}
	pmap_update(pmap_kernel());
	return kva;
}

/*
 * uvm_pagermapout: remove KVA mapping
 *
 * We remove our mappings by hand and then remove the mapping.
 */
void
uvm_pagermapout(vaddr_t kva, int npages)
{
#if defined(__HAVE_PMAP_DIRECT)
	/*
	 * Use direct mappings for single page, unless there is a risk
	 * of aliasing.
	 */
	if (npages == 1 && PMAP_PREFER_ALIGN() == 0) {
		pmap_unmap_direct(kva);
		return;
	}
#endif

	pmap_remove(pmap_kernel(), kva, kva + ((vsize_t)npages << PAGE_SHIFT));
	pmap_update(pmap_kernel());
	uvm_pseg_release(kva);

}

/*
 * uvm_mk_pcluster
 *
 * generic "make 'pager put' cluster" function.  a pager can either
 * [1] set pgo_mk_pcluster to NULL (never cluster), [2] set it to this
 * generic function, or [3] set it to a pager specific function.
 *
 * => caller must lock object _and_ pagequeues (since we need to look
 *    at active vs. inactive bits, etc.)
 * => caller must make center page busy and write-protect it
 * => we mark all cluster pages busy for the caller
 * => the caller must unbusy all pages (and check wanted/released
 *    status if it drops the object lock)
 * => flags:
 *      PGO_ALLPAGES:  all pages in object are valid targets
 *      !PGO_ALLPAGES: use "lo" and "hi" to limit range of cluster
 *      PGO_DOACTCLUST: include active pages in cluster.
 *	PGO_FREE: set the PG_RELEASED bits on the cluster so they'll be freed
 *		in async io (caller must clean on error).
 *        NOTE: the caller should clear PG_CLEANCHK bits if PGO_DOACTCLUST.
 *              PG_CLEANCHK is only a hint, but clearing will help reduce
 *		the number of calls we make to the pmap layer.
 */

struct vm_page **
uvm_mk_pcluster(struct uvm_object *uobj, struct vm_page **pps, int *npages,
    struct vm_page *center, int flags, voff_t mlo, voff_t mhi)
{
	struct vm_page **ppsp, *pclust;
	voff_t lo, hi, curoff;
	int center_idx, forward, incr;

	/* 
	 * center page should already be busy and write protected.  XXX:
	 * suppose page is wired?  if we lock, then a process could
	 * fault/block on it.  if we don't lock, a process could write the
	 * pages in the middle of an I/O.  (consider an msync()).  let's
	 * lock it for now (better to delay than corrupt data?).
	 */
	/* get cluster boundaries, check sanity, and apply our limits as well.*/
	uobj->pgops->pgo_cluster(uobj, center->offset, &lo, &hi);
	if ((flags & PGO_ALLPAGES) == 0) {
		if (lo < mlo)
			lo = mlo;
		if (hi > mhi)
			hi = mhi;
	}
	if ((hi - lo) >> PAGE_SHIFT > *npages) { /* pps too small, bail out! */
		pps[0] = center;
		*npages = 1;
		return pps;
	}

	/* now determine the center and attempt to cluster around the edges */
	center_idx = (center->offset - lo) >> PAGE_SHIFT;
	pps[center_idx] = center;	/* plug in the center page */
	ppsp = &pps[center_idx];
	*npages = 1;

	/*
	 * attempt to cluster around the left [backward], and then 
	 * the right side [forward].    
	 *
	 * note that for inactive pages (pages that have been deactivated)
	 * there are no valid mappings and PG_CLEAN should be up to date.
	 * [i.e. there is no need to query the pmap with pmap_is_modified
	 * since there are no mappings].
	 */
	for (forward  = 0 ; forward <= 1 ; forward++) {
		incr = forward ? PAGE_SIZE : -PAGE_SIZE;
		curoff = center->offset + incr;
		for ( ;(forward == 0 && curoff >= lo) ||
		       (forward && curoff < hi);
		      curoff += incr) {

			pclust = uvm_pagelookup(uobj, curoff); /* lookup page */
			if (pclust == NULL) {
				break;			/* no page */
			}
			/* handle active pages */
			/* NOTE: inactive pages don't have pmap mappings */
			if ((pclust->pg_flags & PQ_INACTIVE) == 0) {
				if ((flags & PGO_DOACTCLUST) == 0) {
					/* dont want mapped pages at all */
					break;
				}

				/* make sure "clean" bit is sync'd */
				if ((pclust->pg_flags & PG_CLEANCHK) == 0) {
					if ((pclust->pg_flags & (PG_CLEAN|PG_BUSY))
					   == PG_CLEAN &&
					   pmap_is_modified(pclust))
						atomic_clearbits_int(
						    &pclust->pg_flags,
						    PG_CLEAN);
					/* now checked */
					atomic_setbits_int(&pclust->pg_flags,
					    PG_CLEANCHK);
				}
			}

			/* is page available for cleaning and does it need it */
			if ((pclust->pg_flags & (PG_CLEAN|PG_BUSY)) != 0) {
				break;	/* page is already clean or is busy */
			}

			/* yes!   enroll the page in our array */
			atomic_setbits_int(&pclust->pg_flags, PG_BUSY);
			UVM_PAGE_OWN(pclust, "uvm_mk_pcluster");

			/*
			 * If we want to free after io is done, and we're
			 * async, set the released flag
			 */
			if ((flags & (PGO_FREE|PGO_SYNCIO)) == PGO_FREE)
				atomic_setbits_int(&pclust->pg_flags,
				    PG_RELEASED);

			/* XXX: protect wired page?   see above comment. */
			pmap_page_protect(pclust, PROT_READ);
			if (!forward) {
				ppsp--;			/* back up one page */
				*ppsp = pclust;
			} else {
				/* move forward one page */
				ppsp[*npages] = pclust;
			}
			(*npages)++;
		}
	}
	
	/*
	 * done!  return the cluster array to the caller!!!
	 */
	return ppsp;
}

/*
 * uvm_pager_put: high level pageout routine
 *
 * we want to pageout page "pg" to backing store, clustering if
 * possible.
 *
 * => page queues must be locked by caller
 * => if page is not swap-backed, then "uobj" points to the object
 *	backing it.
 * => if page is swap-backed, then "uobj" should be NULL.
 * => "pg" should be PG_BUSY (by caller), and !PG_CLEAN
 *    for swap-backed memory, "pg" can be NULL if there is no page
 *    of interest [sometimes the case for the pagedaemon]
 * => "ppsp_ptr" should point to an array of npages vm_page pointers
 *	for possible cluster building
 * => flags (first two for non-swap-backed pages)
 *	PGO_ALLPAGES: all pages in uobj are valid targets
 *	PGO_DOACTCLUST: include "PQ_ACTIVE" pages as valid targets
 *	PGO_SYNCIO: do SYNC I/O (no async)
 *	PGO_PDFREECLUST: pagedaemon: drop cluster on successful I/O
 *	PGO_FREE: tell the aio daemon to free pages in the async case.
 * => start/stop: if (uobj && !PGO_ALLPAGES) limit targets to this range
 *		  if (!uobj) start is the (daddr_t) of the starting swapblk
 * => return state:
 *	1. we return the VM_PAGER status code of the pageout
 *	2. we return with the page queues unlocked
 *	3. on errors we always drop the cluster.   thus, if we return
 *		!PEND, !OK, then the caller only has to worry about
 *		un-busying the main page (not the cluster pages).
 *	4. on success, if !PGO_PDFREECLUST, we return the cluster
 *		with all pages busy (caller must un-busy and check
 *		wanted/released flags).
 */
int
uvm_pager_put(struct uvm_object *uobj, struct vm_page *pg,
    struct vm_page ***ppsp_ptr, int *npages, int flags,
    voff_t start, voff_t stop)
{
	int result;
	daddr_t swblk;
	struct vm_page **ppsp = *ppsp_ptr;

	/*
	 * note that uobj is null  if we are doing a swap-backed pageout.
	 * note that uobj is !null if we are doing normal object pageout.
	 * note that the page queues must be locked to cluster.
	 */
	if (uobj) {	/* if !swap-backed */
		/*
		 * attempt to build a cluster for pageout using its
		 * make-put-cluster function (if it has one).
		 */
		if (uobj->pgops->pgo_mk_pcluster) {
			ppsp = uobj->pgops->pgo_mk_pcluster(uobj, ppsp,
			    npages, pg, flags, start, stop);
			*ppsp_ptr = ppsp;  /* update caller's pointer */
		} else {
			ppsp[0] = pg;
			*npages = 1;
		}

		swblk = 0;		/* XXX: keep gcc happy */
	} else {
		/*
		 * for swap-backed pageout, the caller (the pagedaemon) has
		 * already built the cluster for us.   the starting swap
		 * block we are writing to has been passed in as "start."
		 * "pg" could be NULL if there is no page we are especially
		 * interested in (in which case the whole cluster gets dropped
		 * in the event of an error or a sync "done").
		 */
		swblk = start;
		/* ppsp and npages should be ok */
	}

	/* now that we've clustered we can unlock the page queues */
	uvm_unlock_pageq();

	/*
	 * now attempt the I/O.   if we have a failure and we are
	 * clustered, we will drop the cluster and try again.
	 */
	if (uobj) {
		result = uobj->pgops->pgo_put(uobj, ppsp, *npages, flags);
	} else {
		/* XXX daddr_t -> int */
		result = uvm_swap_put(swblk, ppsp, *npages, flags);
	}

	/*
	 * we have attempted the I/O.
	 *
	 * if the I/O was a success then:
	 * 	if !PGO_PDFREECLUST, we return the cluster to the 
	 *		caller (who must un-busy all pages)
	 *	else we un-busy cluster pages for the pagedaemon
	 *
	 * if I/O is pending (async i/o) then we return the pending code.
	 * [in this case the async i/o done function must clean up when
	 *  i/o is done...]
	 */
	if (result == VM_PAGER_PEND || result == VM_PAGER_OK) {
		if (result == VM_PAGER_OK && (flags & PGO_PDFREECLUST)) {
			/* drop cluster */
			if (*npages > 1 || pg == NULL)
				uvm_pager_dropcluster(uobj, pg, ppsp, npages,
				    PGO_PDFREECLUST);
		}
		return (result);
	}

	/*
	 * a pager error occurred (even after dropping the cluster, if there
	 * was one).  give up! the caller only has one page ("pg")
	 * to worry about.
	 */
	if (*npages > 1 || pg == NULL) {
		uvm_pager_dropcluster(uobj, pg, ppsp, npages, PGO_REALLOCSWAP);

		/*
		 * for failed swap-backed pageouts with a "pg",
		 * we need to reset pg's swslot to either:
		 * "swblk" (for transient errors, so we can retry),
		 * or 0 (for hard errors).
		 */
		if (uobj == NULL) {
			if (pg != NULL) {
				if (pg->pg_flags & PQ_ANON) {
					rw_enter(pg->uanon->an_lock, RW_WRITE);
					pg->uanon->an_swslot = 0;
					rw_exit(pg->uanon->an_lock);
				} else {
					rw_enter(pg->uobject->vmobjlock, RW_WRITE);
					uao_set_swslot(pg->uobject,
					    pg->offset >> PAGE_SHIFT, 0);
					rw_exit(pg->uobject->vmobjlock);
				}
			}
			/*
			 * for transient failures, free all the swslots
			 */
			if (result == VM_PAGER_AGAIN) {
				/* XXX daddr_t -> int */
				uvm_swap_free(swblk, *npages);
			} else {
				/*
				 * for hard errors on swap-backed pageouts,
				 * mark the swslots as bad.  note that we do not
				 * free swslots that we mark bad.
				 */
				/* XXX daddr_t -> int */
				uvm_swap_markbad(swblk, *npages);
			}
		}
	}

	/*
	 * a pager error occurred (even after dropping the cluster, if there
	 * was one).    give up!   the caller only has one page ("pg")
	 * to worry about.
	 */
	return result;
}

/*
 * uvm_pager_dropcluster: drop a cluster we have built (because we 
 * got an error, or, if PGO_PDFREECLUST we are un-busying the
 * cluster pages on behalf of the pagedaemon).
 *
 * => uobj, if non-null, is a non-swap-backed object
 * => page queues are not locked
 * => pg is our page of interest (the one we clustered around, can be null)
 * => ppsp/npages is our current cluster
 * => flags: PGO_PDFREECLUST: pageout was a success: un-busy cluster
 *	pages on behalf of the pagedaemon.
 *           PGO_REALLOCSWAP: drop previously allocated swap slots for 
 *		clustered swap-backed pages (except for "pg" if !NULL)
 *		"swblk" is the start of swap alloc (e.g. for ppsp[0])
 *		[only meaningful if swap-backed (uobj == NULL)]
 */

void
uvm_pager_dropcluster(struct uvm_object *uobj, struct vm_page *pg,
    struct vm_page **ppsp, int *npages, int flags)
{
	int lcv;

	KASSERT(uobj == NULL || rw_write_held(uobj->vmobjlock));

	/* drop all pages but "pg" */
	for (lcv = 0 ; lcv < *npages ; lcv++) {
		/* skip "pg" or empty slot */
		if (ppsp[lcv] == pg || ppsp[lcv] == NULL)
			continue;
	
		/*
		 * Note that PQ_ANON bit can't change as long as we are holding
		 * the PG_BUSY bit (so there is no need to lock the page
		 * queues to test it).
		 */
		if (!uobj) {
			if (ppsp[lcv]->pg_flags & PQ_ANON) {
				rw_enter(ppsp[lcv]->uanon->an_lock, RW_WRITE);
				if (flags & PGO_REALLOCSWAP)
					  /* zap swap block */
					  ppsp[lcv]->uanon->an_swslot = 0;
			} else {
				rw_enter(ppsp[lcv]->uobject->vmobjlock,
				    RW_WRITE);
				if (flags & PGO_REALLOCSWAP)
					uao_set_swslot(ppsp[lcv]->uobject,
					    ppsp[lcv]->offset >> PAGE_SHIFT, 0);
			}
		}

		/* did someone want the page while we had it busy-locked? */
		if (ppsp[lcv]->pg_flags & PG_WANTED) {
			wakeup(ppsp[lcv]);
		}

		/* if page was released, release it.  otherwise un-busy it */
		if (ppsp[lcv]->pg_flags & PG_RELEASED &&
		    ppsp[lcv]->pg_flags & PQ_ANON) {
				/* kills anon and frees pg */
				uvm_anon_release(ppsp[lcv]->uanon);
				continue;
		} else {
			/*
			 * if we were planning on async io then we would
			 * have PG_RELEASED set, clear that with the others.
			 */
			atomic_clearbits_int(&ppsp[lcv]->pg_flags,
			    PG_BUSY|PG_WANTED|PG_FAKE|PG_RELEASED);
			UVM_PAGE_OWN(ppsp[lcv], NULL);
		}

		/*
		 * if we are operating on behalf of the pagedaemon and we 
		 * had a successful pageout update the page!
		 */
		if (flags & PGO_PDFREECLUST) {
			pmap_clear_reference(ppsp[lcv]);
			pmap_clear_modify(ppsp[lcv]);
			atomic_setbits_int(&ppsp[lcv]->pg_flags, PG_CLEAN);
		}

		/* if anonymous cluster, unlock object and move on */
		if (!uobj) {
			if (ppsp[lcv]->pg_flags & PQ_ANON)
				rw_exit(ppsp[lcv]->uanon->an_lock);
			else
				rw_exit(ppsp[lcv]->uobject->vmobjlock);
		}
	}
}

/*
 * interrupt-context iodone handler for single-buf i/os
 * or the top-level buf of a nested-buf i/o.
 *
 * => must be at splbio().
 */

void
uvm_aio_biodone(struct buf *bp)
{
	splassert(IPL_BIO);

	/* reset b_iodone for when this is a single-buf i/o. */
	bp->b_iodone = uvm_aio_aiodone;

	mtx_enter(&uvm.aiodoned_lock);
	TAILQ_INSERT_TAIL(&uvm.aio_done, bp, b_freelist);
	wakeup(&uvm.aiodoned);
	mtx_leave(&uvm.aiodoned_lock);
}

void
uvm_aio_aiodone_pages(struct vm_page **pgs, int npages, boolean_t write,
    int error)
{
	struct vm_page *pg;
	struct rwlock *slock;
	boolean_t swap;
	int i, swslot;

	slock = NULL;
	pg = pgs[0];
	swap = (pg->uanon != NULL && pg->uobject == NULL) ||
		(pg->pg_flags & PQ_AOBJ) != 0;

	KASSERT(swap);
	KASSERT(write);

	if (error) {
		if (pg->uobject != NULL) {
			swslot = uao_find_swslot(pg->uobject,
			    pg->offset >> PAGE_SHIFT);
		} else {
			swslot = pg->uanon->an_swslot;
		}
		KASSERT(swslot);
	}

	for (i = 0; i < npages; i++) {
		int anon_disposed = 0;

		pg = pgs[i];
		KASSERT((pg->pg_flags & PG_FAKE) == 0);

		/*
		 * lock each page's object (or anon) individually since
		 * each page may need a different lock.
		 */
		if (pg->uobject != NULL) {
			slock = pg->uobject->vmobjlock;
		} else {
			slock = pg->uanon->an_lock;
		}
		rw_enter(slock, RW_WRITE);
		anon_disposed = (pg->pg_flags & PG_RELEASED) != 0;
		KASSERT(!anon_disposed || pg->uobject != NULL ||
		    pg->uanon->an_ref == 0);

		/*
		 * if this was a successful write,
		 * mark the page PG_CLEAN.
		 */
		if (!error) {
			pmap_clear_reference(pg);
			pmap_clear_modify(pg);
			atomic_setbits_int(&pg->pg_flags, PG_CLEAN);
		}

		/*
		 * unlock everything for this page now.
		 */
		if (pg->uobject == NULL && anon_disposed) {
			uvm_anon_release(pg->uanon);
		} else {
			uvm_page_unbusy(&pg, 1);
			rw_exit(slock);
		}
	}

	if (error) {
		uvm_swap_markbad(swslot, npages);
	}
}

/*
 * uvm_aio_aiodone: do iodone processing for async i/os.
 * this should be called in thread context, not interrupt context.
 */
void
uvm_aio_aiodone(struct buf *bp)
{
	int npages = bp->b_bufsize >> PAGE_SHIFT;
	struct vm_page *pgs[MAXPHYS >> PAGE_SHIFT];
	int i, error;
	boolean_t write;

	KASSERT(npages <= MAXPHYS >> PAGE_SHIFT);
	splassert(IPL_BIO);

	error = (bp->b_flags & B_ERROR) ? (bp->b_error ? bp->b_error : EIO) : 0;
	write = (bp->b_flags & B_READ) == 0;

	for (i = 0; i < npages; i++)
		pgs[i] = uvm_atopg((vaddr_t)bp->b_data +
		    ((vsize_t)i << PAGE_SHIFT));
	uvm_pagermapout((vaddr_t)bp->b_data, npages);
#ifdef UVM_SWAP_ENCRYPT
	/*
	 * XXX - assumes that we only get ASYNC writes. used to be above.
	 */
	if (pgs[0]->pg_flags & PQ_ENCRYPT) {
		uvm_swap_freepages(pgs, npages);
		goto freed;
	}
#endif /* UVM_SWAP_ENCRYPT */

	uvm_aio_aiodone_pages(pgs, npages, write, error);

#ifdef UVM_SWAP_ENCRYPT
freed:
#endif
	pool_put(&bufpool, bp);
}
