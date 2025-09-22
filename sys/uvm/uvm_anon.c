/*	$OpenBSD: uvm_anon.c,v 1.64 2025/04/27 08:37:47 mpi Exp $	*/
/*	$NetBSD: uvm_anon.c,v 1.10 2000/11/25 06:27:59 chs Exp $	*/

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
 */

/*
 * uvm_anon.c: uvm anon ops
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/kernel.h>
#include <sys/atomic.h>

#include <uvm/uvm.h>
#include <uvm/uvm_swap.h>

struct pool uvm_anon_pool;

void
uvm_anon_init(void)
{
	pool_init(&uvm_anon_pool, sizeof(struct vm_anon), 0, IPL_MPFLOOR,
	    PR_WAITOK, "anonpl", NULL);
	pool_sethiwat(&uvm_anon_pool, uvmexp.free / 16);
}

void
uvm_anon_init_percpu(void)
{
#ifdef MULTIPROCESSOR
	pool_cache_init(&uvm_anon_pool);
#endif
}

/*
 * uvm_analloc: allocate a new anon.
 *
 * => anon will have no lock associated.
 */
struct vm_anon *
uvm_analloc(void)
{
	struct vm_anon *anon;

	anon = pool_get(&uvm_anon_pool, PR_NOWAIT);
	if (anon) {
		anon->an_lock = NULL;
		anon->an_ref = 1;
		anon->an_page = NULL;
		anon->an_swslot = 0;
	}
	return anon;
}

/*
 * uvm_anfree_list: free a single anon structure
 *
 * => anon must be removed from the amap (if anon was in an amap).
 * => amap must be locked, if anon was owned by amap.
 * => we may lock the pageq's.
 */
void
uvm_anfree_list(struct vm_anon *anon, struct pglist *pgl)
{
	struct vm_page *pg = anon->an_page;

	KASSERT(anon->an_lock == NULL || rw_write_held(anon->an_lock));
	KASSERT(anon->an_ref == 0);

	/*
	 * Dispose of the page, if it is resident.
	 */
	if (pg != NULL) {
		KASSERT(anon->an_lock != NULL);

		/*
		 * If the page is busy, mark it as PG_RELEASED, so
		 * that uvm_anon_release(9) would release it later.
		 */
		if ((pg->pg_flags & PG_BUSY) != 0) {
			atomic_setbits_int(&pg->pg_flags, PG_RELEASED);
			rw_obj_hold(anon->an_lock);
			return;
		}
		pmap_page_protect(pg, PROT_NONE);
		if (pgl != NULL) {
			/*
			 * clean page, and put it on pglist
			 * for later freeing.
			 */
			uvm_pageclean(pg);
			TAILQ_INSERT_HEAD(pgl, pg, pageq);
		} else {
			uvm_pagefree(pg);	/* bye bye */
		}
	} else {
		if (anon->an_swslot != 0 && anon->an_swslot != SWSLOT_BAD) {
			/* This page is no longer only in swap. */
			KASSERT(uvmexp.swpgonly > 0);
			atomic_dec_int(&uvmexp.swpgonly);
		}
	}
	anon->an_lock = NULL;

	/*
	 * Free any swap resources, leave a page replacement hint.
	 */
	uvm_anon_dropswap(anon);

	KASSERT(anon->an_page == NULL);
	KASSERT(anon->an_swslot == 0);

	pool_put(&uvm_anon_pool, anon);
}

/*
 * uvm_anwait: wait for memory to become available to allocate an anon.
 */
void
uvm_anwait(void)
{
	struct vm_anon *anon;

	/* XXX: Want something like pool_wait()? */
	anon = pool_get(&uvm_anon_pool, PR_WAITOK);
	pool_put(&uvm_anon_pool, anon);
}

/*
 * uvm_anon_pagein: fetch an anon's page.
 *
 * => anon must be locked, and is unlocked upon return.
 * => returns true if pagein was aborted due to lack of memory.
 */

boolean_t
uvm_anon_pagein(struct vm_amap *amap, struct vm_anon *anon)
{
	struct vm_page *pg;
	int rv;

	KASSERT(rw_write_held(anon->an_lock));
	KASSERT(anon->an_lock == amap->am_lock);

	/*
	 * Get the page of the anon.
	 */
	rv = uvmfault_anonget(NULL, amap, anon);

	switch (rv) {
	case 0:
		/* Success - we have the page. */
		KASSERT(rw_write_held(anon->an_lock));
		break;
	case EACCES:
	case ERESTART:
		/*
		 * Nothing more to do on errors.  ERESTART means that the
		 * anon was freed.
		 */
		return FALSE;
	case ENOLCK:
		/* Should not be possible. */
	default:
#ifdef DIAGNOSTIC
		panic("anon_pagein: uvmfault_anonget -> %d", rv);
#else
		return FALSE;
#endif
	}

	/*
	 * Mark the page as dirty and clear its swslot.
	 */
	pg = anon->an_page;
	if (anon->an_swslot > 0) {
		uvm_swap_free(anon->an_swslot, 1);
	}
	anon->an_swslot = 0;
	atomic_clearbits_int(&pg->pg_flags, PG_CLEAN);

	/*
	 * Deactivate the page (to put it on a page queue).
	 */
	uvm_lock_pageq();
	uvm_pagedeactivate(pg);
	uvm_unlock_pageq();
	rw_exit(anon->an_lock);

	return FALSE;
}

/*
 * uvm_anon_dropswap: release any swap resources from this anon.
 *
 * => anon must be locked or have a reference count of 0.
 */
void
uvm_anon_dropswap(struct vm_anon *anon)
{
	KASSERT(anon->an_ref == 0 || rw_lock_held(anon->an_lock));

	if (anon->an_swslot == 0)
		return;

	uvm_swap_free(anon->an_swslot, 1);
	anon->an_swslot = 0;
}


/*
 * uvm_anon_release: release an anon and its page.
 *
 * => anon should not have any references.
 * => anon must be locked.
 */

void
uvm_anon_release(struct vm_anon *anon)
{
	struct vm_page *pg = anon->an_page;
	struct rwlock *lock;

	KASSERT(rw_write_held(anon->an_lock));
	KASSERT(pg != NULL);
	KASSERT((pg->pg_flags & PG_RELEASED) != 0);
	KASSERT((pg->pg_flags & PG_BUSY) != 0);
	KASSERT(pg->uobject == NULL);
	KASSERT(pg->uanon == anon);
	KASSERT(anon->an_ref == 0);

	pmap_page_protect(pg, PROT_NONE);
	uvm_pagefree(pg);
	KASSERT(anon->an_page == NULL);
	lock = anon->an_lock;
	uvm_anon_dropswap(anon);
	pool_put(&uvm_anon_pool, anon);
	rw_exit(lock);
	/* Note: extra reference is held for PG_RELEASED case. */
	rw_obj_free(lock);
}
