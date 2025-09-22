/*	$OpenBSD: uvm_object.c,v 1.27 2025/03/10 14:13:58 mpi Exp $	*/

/*
 * Copyright (c) 2006, 2010, 2019 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Mindaugas Rasiukevicius.
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

/*
 * uvm_object.c: operate with memory objects
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/atomic.h>
#include <sys/rwlock.h>

#include <uvm/uvm.h>

/* Dummy object used by some pmaps for sanity checks. */
const struct uvm_pagerops pmap_pager = {
	/* nothing */
};

/* Dummy object used by the buffer cache for sanity checks. */
const struct uvm_pagerops bufcache_pager = {
	/* nothing */
};

/* Page count to fetch per single step. */
#define	FETCH_PAGECOUNT	16

/*
 * uvm_obj_init: initialize UVM memory object.
 */
void
uvm_obj_init(struct uvm_object *uobj, const struct uvm_pagerops *pgops, int refs)
{
	int alock;

	alock = ((pgops != NULL) && (pgops != &pmap_pager) &&
	    (pgops != &bufcache_pager) && (refs != UVM_OBJ_KERN));

	if (alock) {
		/* Allocate and assign a lock. */
		rw_obj_alloc(&uobj->vmobjlock, "uobjlk");
	} else {
		/* The lock will need to be set via uvm_obj_setlock(). */
		uobj->vmobjlock = NULL;
	}
	uobj->pgops = pgops;
	RBT_INIT(uvm_objtree, &uobj->memt);
	uobj->uo_npages = 0;
	uobj->uo_refs = refs;
}

/*
 * uvm_obj_destroy: destroy UVM memory object.
 */
void
uvm_obj_destroy(struct uvm_object *uo)
{
	KASSERT(RBT_EMPTY(uvm_objtree, &uo->memt));

	rw_obj_free(uo->vmobjlock);
}

/*
 * uvm_obj_setlock: assign a vmobjlock to the UVM object.
 *
 * => Caller is responsible to ensure that UVM objects is not use.
 * => Only dynamic lock may be previously set.  We drop the reference then.
 */
void
uvm_obj_setlock(struct uvm_object *uo, struct rwlock *lockptr)
{
	struct rwlock *olockptr = uo->vmobjlock;

	if (olockptr) {
		/* Drop the reference on the old lock. */
		rw_obj_free(olockptr);
	}
	if (lockptr == NULL) {
		/* If new lock is not passed - allocate default one. */
		rw_obj_alloc(&lockptr, "uobjlk");
	}
	uo->vmobjlock = lockptr;
}

#ifndef SMALL_KERNEL
/*
 * uvm_obj_wire: wire the pages of entire UVM object.
 *
 * => NOTE: this function should only be used for types of objects
 *  where PG_RELEASED flag is never set (aobj objects)
 * => caller must pass page-aligned start and end values
 * => if the caller passes in a pageq pointer, we'll return a list of
 *  wired pages.
 */

int
uvm_obj_wire(struct uvm_object *uobj, voff_t start, voff_t end,
    struct pglist *pageq)
{
	int i, npages, left, error;
	struct vm_page *pgs[FETCH_PAGECOUNT];
	voff_t offset = start;

	left = (end - start) >> PAGE_SHIFT;

	rw_enter(uobj->vmobjlock, RW_WRITE | RW_DUPOK);
	while (left) {

		npages = MIN(FETCH_PAGECOUNT, left);

		/* Get the pages */
		memset(pgs, 0, sizeof(pgs));
		error = (*uobj->pgops->pgo_get)(uobj, offset, pgs, &npages, 0,
			PROT_READ | PROT_WRITE, MADV_SEQUENTIAL,
			PGO_ALLPAGES | PGO_SYNCIO);

		if (error)
			goto error;

		rw_enter(uobj->vmobjlock, RW_WRITE | RW_DUPOK);
		for (i = 0; i < npages; i++) {

			KASSERT(pgs[i] != NULL);
			KASSERT(!(pgs[i]->pg_flags & PG_RELEASED));

			if (pgs[i]->pg_flags & PQ_AOBJ) {
				atomic_clearbits_int(&pgs[i]->pg_flags,
				    PG_CLEAN);
				uao_dropswap(uobj, i);
			}
		}

		/* Wire the pages */
		uvm_lock_pageq();
		for (i = 0; i < npages; i++) {
			uvm_pagewire(pgs[i]);
			if (pageq != NULL)
				TAILQ_INSERT_TAIL(pageq, pgs[i], pageq);
		}
		uvm_unlock_pageq();

		/* Unbusy the pages */
		uvm_page_unbusy(pgs, npages);

		left -= npages;
		offset += (voff_t)npages << PAGE_SHIFT;
	}
	rw_exit(uobj->vmobjlock);

	return 0;

error:
	/* Unwire the pages which have been wired */
	uvm_obj_unwire(uobj, start, offset);

	return error;
}

/*
 * uvm_obj_unwire: unwire the pages of entire UVM object.
 *
 * => caller must pass page-aligned start and end values
 */
void
uvm_obj_unwire(struct uvm_object *uobj, voff_t start, voff_t end)
{
	struct vm_page *pg;
	off_t offset;

	rw_enter(uobj->vmobjlock, RW_WRITE | RW_DUPOK);
	uvm_lock_pageq();
	for (offset = start; offset < end; offset += PAGE_SIZE) {
		pg = uvm_pagelookup(uobj, offset);

		KASSERT(pg != NULL);
		KASSERT(!(pg->pg_flags & PG_RELEASED));

		uvm_pageunwire(pg);
	}
	uvm_unlock_pageq();
	rw_exit(uobj->vmobjlock);
}
#endif /* !SMALL_KERNEL */

/*
 * uvm_obj_free: free all pages in a uvm object, used by the buffer
 * cache to free all pages attached to a buffer.
 */
void
uvm_obj_free(struct uvm_object *uobj)
{
	struct vm_page *pg;
	struct pglist pgl;

	KASSERT(UVM_OBJ_IS_BUFCACHE(uobj));
	KERNEL_ASSERT_LOCKED();

	TAILQ_INIT(&pgl);
 	/*
	 * Extract from rb tree in offset order. The phys addresses
	 * usually increase in that order, which is better for
	 * uvm_pglistfree().
 	 */
	RBT_FOREACH(pg, uvm_objtree, &uobj->memt) {
		/*
		 * clear PG_TABLED and `uobject' so we don't do work to
		 * remove this pg from the uobj we are throwing away.
		 */
		atomic_clearbits_int(&pg->pg_flags, PG_TABLED);
		pg->uobject = NULL;
		uvm_pageclean(pg);
		TAILQ_INSERT_TAIL(&pgl, pg, pageq);
 	}
	uvm_pglistfree(&pgl);
}

