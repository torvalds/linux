/*	$OpenBSD: uvm_fault.c,v 1.171 2025/09/11 17:04:35 mpi Exp $	*/
/*	$NetBSD: uvm_fault.c,v 1.51 2000/08/06 00:22:53 thorpej Exp $	*/

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
 * from: Id: uvm_fault.c,v 1.1.2.23 1998/02/06 05:29:05 chs Exp
 */

/*
 * uvm_fault.c: fault handler
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/tracepoint.h>

#include <uvm/uvm.h>

/*
 *
 * a word on page faults:
 *
 * types of page faults we handle:
 *
 * CASE 1: upper layer faults                   CASE 2: lower layer faults
 *
 *    CASE 1A         CASE 1B                  CASE 2A        CASE 2B
 *    read/write1     write>1                  read/write   +-cow_write/zero
 *         |             |                         |        |
 *      +--|--+       +--|--+     +-----+       +  |  +     | +-----+
 * amap |  V  |       |  ---------> new |          |        | |  ^  |
 *      +-----+       +-----+     +-----+       +  |  +     | +--|--+
 *                                                 |        |    |
 *      +-----+       +-----+                   +--|--+     | +--|--+
 * uobj | d/c |       | d/c |                   |  V  |     +----+  |
 *      +-----+       +-----+                   +-----+       +-----+
 *
 * d/c = don't care
 *
 *   case [0]: layerless fault
 *	no amap or uobj is present.   this is an error.
 *
 *   case [1]: upper layer fault [anon active]
 *     1A: [read] or [write with anon->an_ref == 1]
 *		I/O takes place in upper level anon and uobj is not touched.
 *     1B: [write with anon->an_ref > 1]
 *		new anon is alloc'd and data is copied off ["COW"]
 *
 *   case [2]: lower layer fault [uobj]
 *     2A: [read on non-NULL uobj] or [write to non-copy_on_write area]
 *		I/O takes place directly in object.
 *     2B: [write to copy_on_write] or [read on NULL uobj]
 *		data is "promoted" from uobj to a new anon.
 *		if uobj is null, then we zero fill.
 *
 * we follow the standard UVM locking protocol ordering:
 *
 * MAPS => AMAP => UOBJ => ANON => PAGE QUEUES (PQ)
 * we hold a PG_BUSY page if we unlock for I/O
 *
 *
 * the code is structured as follows:
 *
 *     - init the "IN" params in the ufi structure
 *   ReFault: (ERESTART returned to the loop in uvm_fault)
 *     - do lookups [locks maps], check protection, handle needs_copy
 *     - check for case 0 fault (error)
 *     - establish "range" of fault
 *     - if we have an amap lock it and extract the anons
 *     - if sequential advice deactivate pages behind us
 *     - at the same time check pmap for unmapped areas and anon for pages
 *	 that we could map in (and do map it if found)
 *     - check object for resident pages that we could map in
 *     - if (case 2) goto Case2
 *     - >>> handle case 1
 *           - ensure source anon is resident in RAM
 *           - if case 1B alloc new anon and copy from source
 *           - map the correct page in
 *   Case2:
 *     - >>> handle case 2
 *           - ensure source page is resident (if uobj)
 *           - if case 2B alloc new anon and copy from source (could be zero
 *		fill if uobj == NULL)
 *           - map the correct page in
 *     - done!
 *
 * note on paging:
 *   if we have to do I/O we place a PG_BUSY page in the correct object,
 * unlock everything, and do the I/O.   when I/O is done we must reverify
 * the state of the world before assuming that our data structures are
 * valid.   [because mappings could change while the map is unlocked]
 *
 *  alternative 1: unbusy the page in question and restart the page fault
 *    from the top (ReFault).   this is easy but does not take advantage
 *    of the information that we already have from our previous lookup,
 *    although it is possible that the "hints" in the vm_map will help here.
 *
 * alternative 2: the system already keeps track of a "version" number of
 *    a map.   [i.e. every time you write-lock a map (e.g. to change a
 *    mapping) you bump the version number up by one...]   so, we can save
 *    the version number of the map before we release the lock and start I/O.
 *    then when I/O is done we can relock and check the version numbers
 *    to see if anything changed.    this might save us some over 1 because
 *    we don't have to unbusy the page and may be less compares(?).
 *
 * alternative 3: put in backpointers or a way to "hold" part of a map
 *    in place while I/O is in progress.   this could be complex to
 *    implement (especially with structures like amap that can be referenced
 *    by multiple map entries, and figuring out what should wait could be
 *    complex as well...).
 *
 * we use alternative 2.  given that we are multi-threaded now we may want
 * to reconsider the choice.
 */

/*
 * local data structures
 */
struct uvm_advice {
	int nback;
	int nforw;
};

/*
 * page range array: set up in uvmfault_init().
 */
static struct uvm_advice uvmadvice[MADV_MASK + 1];

#define UVM_MAXRANGE 16	/* must be max() of nback+nforw+1 */

/*
 * private prototypes
 */
static inline void uvmfault_anonflush(struct vm_anon **, int);
void	uvmfault_unlockmaps(struct uvm_faultinfo *, boolean_t);
void	uvmfault_update_stats(struct uvm_faultinfo *);

/*
 * inline functions
 */
/*
 * uvmfault_anonflush: try and deactivate pages in specified anons
 *
 * => does not have to deactivate page if it is busy
 */
static inline void
uvmfault_anonflush(struct vm_anon **anons, int n)
{
	int lcv;
	struct vm_page *pg;

	for (lcv = 0; lcv < n; lcv++) {
		if (anons[lcv] == NULL)
			continue;
		KASSERT(rw_lock_held(anons[lcv]->an_lock));
		pg = anons[lcv]->an_page;
		if (pg && (pg->pg_flags & PG_BUSY) == 0) {
			uvm_lock_pageq();
			if (pg->wire_count == 0) {
				uvm_pagedeactivate(pg);
			}
			uvm_unlock_pageq();
		}
	}
}

/*
 * normal functions
 */
/*
 * uvmfault_init: compute proper values for the uvmadvice[] array.
 */
void
uvmfault_init(void)
{
	int npages;

	npages = atop(16384);
	if (npages > 0) {
		KASSERT(npages <= UVM_MAXRANGE / 2);
		uvmadvice[MADV_NORMAL].nforw = npages;
		uvmadvice[MADV_NORMAL].nback = npages - 1;
	}

	npages = atop(32768);
	if (npages > 0) {
		KASSERT(npages <= UVM_MAXRANGE / 2);
		uvmadvice[MADV_SEQUENTIAL].nforw = npages - 1;
		uvmadvice[MADV_SEQUENTIAL].nback = npages;
	}
}

/*
 * uvmfault_anonget: get data in an anon into a non-busy, non-released
 * page in that anon.
 *
 * => Map, amap and thus anon should be locked by caller.
 * => If we fail, we unlock everything and error is returned.
 * => If we are successful, return with everything still locked.
 * => We do not move the page on the queues [gets moved later].  If we
 *    allocate a new page [we_own], it gets put on the queues.  Either way,
 *    the result is that the page is on the queues at return time
 */
int
uvmfault_anonget(struct uvm_faultinfo *ufi, struct vm_amap *amap,
    struct vm_anon *anon)
{
	struct vm_page *pg;
	int lock_type;
	int error;

	KASSERT(rw_lock_held(anon->an_lock));
	KASSERT(anon->an_lock == amap->am_lock);

	/* Increment the counters.*/
	counters_inc(uvmexp_counters, flt_anget);
	if (anon->an_page) {
		curproc->p_ru.ru_minflt++;
	} else {
		curproc->p_ru.ru_majflt++;
	}
	error = 0;

	/*
	 * Loop until we get the anon data, or fail.
	 */
	for (;;) {
		boolean_t we_own, locked;
		/*
		 * Note: 'we_own' will become true if we set PG_BUSY on a page.
		 */
		we_own = FALSE;
		pg = anon->an_page;

		/*
		 * Is page resident?  Make sure it is not busy/released.
		 */
		lock_type = rw_status(anon->an_lock);
		if (pg) {
			KASSERT(pg->pg_flags & PQ_ANON);
			KASSERT(pg->uanon == anon);

			/*
			 * if the page is busy, we drop all the locks and
			 * try again.
			 */
			if ((pg->pg_flags & (PG_BUSY|PG_RELEASED)) == 0)
				return 0;
			counters_inc(uvmexp_counters, flt_pgwait);

			/*
			 * The last unlock must be an atomic unlock and wait
			 * on the owner of page.
			 */
			KASSERT(pg->uobject == NULL);
			uvmfault_unlockall(ufi, NULL, NULL);
			uvm_pagewait(pg, anon->an_lock, "anonget");
		} else {
			/*
			 * No page, therefore allocate one.  A write lock is
			 * required for this.  If the caller didn't supply
			 * one, fail now and have them retry.
			 */
			if (lock_type == RW_READ) {
				return ENOLCK;
			}
			pg = uvm_pagealloc(NULL, 0, anon, 0);
			if (pg == NULL) {
				/* Out of memory.  Wait a little. */
				uvmfault_unlockall(ufi, amap, NULL);
				counters_inc(uvmexp_counters, flt_noram);
				uvm_wait("flt_noram1");
			} else {
				/* PG_BUSY bit is set. */
				we_own = TRUE;
				uvmfault_unlockall(ufi, amap, NULL);

				/*
				 * Pass a PG_BUSY+PG_FAKE+PG_CLEAN page into
				 * the uvm_swap_get() function with all data
				 * structures unlocked.  Note that it is OK
				 * to read an_swslot here, because we hold
				 * PG_BUSY on the page.
				 */
				counters_inc(uvmexp_counters, pageins);
				error = uvm_swap_get(pg, anon->an_swslot,
				    PGO_SYNCIO);

				/*
				 * We clean up after the I/O below in the
				 * 'we_own' case.
				 */
			}
		}

		/*
		 * Re-lock the map and anon.
		 */
		locked = uvmfault_relock(ufi);
		if (locked || we_own) {
			rw_enter(anon->an_lock, lock_type);
		}

		/*
		 * If we own the page (i.e. we set PG_BUSY), then we need
		 * to clean up after the I/O.  There are three cases to
		 * consider:
		 *
		 * 1) Page was released during I/O: free anon and ReFault.
		 * 2) I/O not OK.  Free the page and cause the fault to fail.
		 * 3) I/O OK!  Activate the page and sync with the non-we_own
		 *    case (i.e. drop anon lock if not locked).
		 */
		if (we_own) {
			if (pg->pg_flags & PG_WANTED) {
				wakeup(pg);
			}

			/*
			 * if we were RELEASED during I/O, then our anon is
			 * no longer part of an amap.   we need to free the
			 * anon and try again.
			 */
			if (pg->pg_flags & PG_RELEASED) {
				KASSERT(anon->an_ref == 0);
				/*
				 * Released while we had unlocked amap.
				 */
				if (locked)
					uvmfault_unlockall(ufi, NULL, NULL);
				uvm_anon_release(anon);	/* frees page for us */
				counters_inc(uvmexp_counters, flt_pgrele);
				return ERESTART;	/* refault! */
			}

			if (error != VM_PAGER_OK) {
				KASSERT(error != VM_PAGER_PEND);

				/* remove page from anon */
				anon->an_page = NULL;

				/*
				 * Remove the swap slot from the anon and
				 * mark the anon as having no real slot.
				 * Do not free the swap slot, thus preventing
				 * it from being used again.
				 */
				uvm_swap_markbad(anon->an_swslot, 1);
				anon->an_swslot = SWSLOT_BAD;

				/*
				 * Note: page was never !PG_BUSY, so it
				 * cannot be mapped and thus no need to
				 * pmap_page_protect() it.
				 */
				uvm_pagefree(pg);

				if (locked) {
					uvmfault_unlockall(ufi, NULL, NULL);
				}
				rw_exit(anon->an_lock);
				/*
				 * An error occurred while trying to bring
				 * in the page -- this is the only error we
				 * return right now.
				 */
				return EACCES;	/* XXX */
			}

			/*
			 * We have successfully read the page, activate it.
			 */
			pmap_clear_modify(pg);
			uvm_lock_pageq();
			uvm_pageactivate(pg);
			uvm_unlock_pageq();
			atomic_clearbits_int(&pg->pg_flags,
			    PG_WANTED|PG_BUSY|PG_FAKE);
			UVM_PAGE_OWN(pg, NULL);
		}

		/*
		 * We were not able to re-lock the map - restart the fault.
		 */
		if (!locked) {
			if (we_own) {
				rw_exit(anon->an_lock);
			}
			return ERESTART;
		}

		/*
		 * Verify that no one has touched the amap and moved
		 * the anon on us.
		 */
		if (ufi != NULL && amap_lookup(&ufi->entry->aref,
		    ufi->orig_rvaddr - ufi->entry->start) != anon) {
			uvmfault_unlockall(ufi, amap, NULL);
			return ERESTART;
		}

		/*
		 * Retry..
		 */
		counters_inc(uvmexp_counters, flt_anretry);
		continue;

	}
	/*NOTREACHED*/
}

/*
 * uvmfault_promote: promote data to a new anon.  used for 1B and 2B.
 *
 *	1. allocate an anon and a page.
 *	2. fill its contents.
 *
 * => if we fail (result != 0) we unlock everything.
 * => on success, return a new locked anon via 'nanon'.
 * => it's caller's responsibility to put the promoted nanon->an_page to the
 *    page queue.
 */
int
uvmfault_promote(struct uvm_faultinfo *ufi,
    struct vm_page *uobjpage,
    struct vm_anon **nanon, /* OUT: allocated anon */
    struct vm_page **npg)
{
	struct vm_amap *amap = ufi->entry->aref.ar_amap;
	struct uvm_object *uobj = NULL;
	struct vm_anon *anon;
	struct vm_page *pg = NULL;

	if (uobjpage != PGO_DONTCARE)
		uobj = uobjpage->uobject;

	KASSERT(rw_write_held(amap->am_lock));
	KASSERT(uobj == NULL || rw_lock_held(uobj->vmobjlock));

	anon = uvm_analloc();
	if (anon) {
		anon->an_lock = amap->am_lock;
		pg = uvm_pagealloc(NULL, 0, anon,
		    (uobjpage == PGO_DONTCARE) ? UVM_PGA_ZERO : 0);
	}

	/* check for out of RAM */
	if (anon == NULL || pg == NULL) {
		uvmfault_unlockall(ufi, amap, uobj);
		if (anon == NULL)
			counters_inc(uvmexp_counters, flt_noanon);
		else {
			anon->an_lock = NULL;
			anon->an_ref--;
			uvm_anfree(anon);
			counters_inc(uvmexp_counters, flt_noram);
		}

		if (uvm_swapisfull())
			return ENOMEM;

		/* out of RAM, wait for more */
		if (anon == NULL)
			uvm_anwait();
		else
			uvm_wait("flt_noram3");
		return ERESTART;
	}

	/*
	 * copy the page [pg now dirty]
	 */
	if (uobjpage != PGO_DONTCARE)
		uvm_pagecopy(uobjpage, pg);

	*nanon = anon;
	*npg = pg;
	return 0;
}

/*
 * Update statistics after fault resolution.
 * - maxrss
 */
void
uvmfault_update_stats(struct uvm_faultinfo *ufi)
{
	struct vm_map		*map;
	struct proc		*p;
	vsize_t			 res;

	map = ufi->orig_map;

	/*
	 * If this is a nested pmap (eg, a virtual machine pmap managed
	 * by vmm(4) on amd64/i386), don't do any updating, just return.
	 *
	 * pmap_nested() on other archs is #defined to 0, so this is a
	 * no-op.
	 */
	if (pmap_nested(map->pmap))
		return;

	/* Update the maxrss for the process. */
	if (map->flags & VM_MAP_ISVMSPACE) {
		p = curproc;
		KASSERT(p != NULL && &p->p_vmspace->vm_map == map);

		res = pmap_resident_count(map->pmap);
		/* Convert res from pages to kilobytes. */
		res <<= (PAGE_SHIFT - 10);

		if (p->p_ru.ru_maxrss < res)
			p->p_ru.ru_maxrss = res;
	}
}

/*
 *   F A U L T   -   m a i n   e n t r y   p o i n t
 */

/*
 * uvm_fault: page fault handler
 *
 * => called from MD code to resolve a page fault
 * => VM data structures usually should be unlocked.   however, it is
 *	possible to call here with the main map locked if the caller
 *	gets a write lock, sets it recursive, and then calls us (c.f.
 *	uvm_map_pageable).   this should be avoided because it keeps
 *	the map locked off during I/O.
 * => MUST NEVER BE CALLED IN INTERRUPT CONTEXT
 */
#define MASK(entry)     (UVM_ET_ISCOPYONWRITE(entry) ? \
			 ~PROT_WRITE : PROT_MASK)
struct uvm_faultctx {
	/*
	 * the following members are set up by uvm_fault_check() and
	 * read-only after that.
	 */
	vm_prot_t enter_prot;
	vm_prot_t access_type;
	vaddr_t startva;
	int npages;
	int centeridx;
	boolean_t narrow;
	boolean_t wired;
	paddr_t pa_flags;
	boolean_t promote;
	int upper_lock_type;
	int lower_lock_type;
};

int		uvm_fault_check(
		    struct uvm_faultinfo *, struct uvm_faultctx *,
		    struct vm_anon ***, vm_fault_t);

int		uvm_fault_upper(
		    struct uvm_faultinfo *, struct uvm_faultctx *,
		    struct vm_anon **);
boolean_t	uvm_fault_upper_lookup(
		    struct uvm_faultinfo *, const struct uvm_faultctx *,
		    struct vm_anon **, struct vm_page **);

int		uvm_fault_lower(
		    struct uvm_faultinfo *, struct uvm_faultctx *,
		    struct vm_page **);
int		uvm_fault_lower_io(
		    struct uvm_faultinfo *, struct uvm_faultctx *,
		    struct uvm_object **, struct vm_page **);

int
uvm_fault(vm_map_t orig_map, vaddr_t vaddr, vm_fault_t fault_type,
    vm_prot_t access_type)
{
	struct uvm_faultinfo ufi;
	struct uvm_faultctx flt;
	boolean_t shadowed;
	struct vm_anon *anons_store[UVM_MAXRANGE], **anons;
	struct vm_page *pages[UVM_MAXRANGE];
	int error;

	counters_inc(uvmexp_counters, faults);
	TRACEPOINT(uvm, fault, vaddr, fault_type, access_type, NULL);

	/*
	 * init the IN parameters in the ufi
	 */
	ufi.orig_map = orig_map;
	ufi.orig_rvaddr = trunc_page(vaddr);
	ufi.orig_size = PAGE_SIZE;	/* can't get any smaller than this */
	flt.access_type = access_type;
	flt.narrow = FALSE;		/* assume normal fault for now */
	flt.wired = FALSE;		/* assume non-wired fault for now */
#if notyet
	flt.upper_lock_type = RW_READ;
	flt.lower_lock_type = RW_READ;	/* shared lock for now */
#else
	flt.upper_lock_type = RW_WRITE;
	flt.lower_lock_type = RW_WRITE;	/* exclusive lock for now */
#endif

	error = ERESTART;
	while (error == ERESTART) { /* ReFault: */
		anons = anons_store;

		error = uvm_fault_check(&ufi, &flt, &anons, fault_type);
		if (error != 0)
			continue;

		/* True if there is an anon at the faulting address */
		shadowed = uvm_fault_upper_lookup(&ufi, &flt, anons, pages);
		if (shadowed == TRUE) {
			/* case 1: fault on an anon in our amap */
			error = uvm_fault_upper(&ufi, &flt, anons);
		} else {
			struct uvm_object *uobj = ufi.entry->object.uvm_obj;

			/*
			 * if the desired page is not shadowed by the amap and
			 * we have a backing object, then we check to see if
			 * the backing object would prefer to handle the fault
			 * itself (rather than letting us do it with the usual
			 * pgo_get hook).  the backing object signals this by
			 * providing a pgo_fault routine.
			 */
			if (uobj != NULL && uobj->pgops->pgo_fault != NULL) {
				rw_enter(uobj->vmobjlock, RW_WRITE);
				KERNEL_LOCK();
				error = uobj->pgops->pgo_fault(&ufi,
				    flt.startva, pages, flt.npages,
				    flt.centeridx, fault_type, flt.access_type,
				    PGO_LOCKED);
				KERNEL_UNLOCK();
			} else {
				/* case 2: fault on backing obj or zero fill */
				error = uvm_fault_lower(&ufi, &flt, pages);
			}
		}
	}

	return error;
}

/*
 * uvm_fault_check: check prot, handle needs-copy, etc.
 *
 *	1. lookup entry.
 *	2. check protection.
 *	3. adjust fault condition (mainly for simulated fault).
 *	4. handle needs-copy (lazy amap copy).
 *	5. establish range of interest for neighbor fault (aka pre-fault).
 *	6. look up anons (if amap exists).
 *	7. flush pages (if MADV_SEQUENTIAL)
 *
 * => called with nothing locked.
 * => if we fail (result != 0) we unlock everything.
 * => initialize/adjust many members of flt.
 */
int
uvm_fault_check(struct uvm_faultinfo *ufi, struct uvm_faultctx *flt,
    struct vm_anon ***ranons, vm_fault_t fault_type)
{
	struct vm_amap *amap;
	struct uvm_object *uobj;
	int nback, nforw;
	boolean_t write_locked = FALSE;

	/*
	 * lookup and lock the maps
	 */
lookup:
	if (uvmfault_lookup(ufi, write_locked) == FALSE) {
		return EFAULT;
	}

#ifdef DIAGNOSTIC
	if ((ufi->map->flags & VM_MAP_PAGEABLE) == 0)
		panic("uvm_fault: fault on non-pageable map (%p, 0x%lx)",
		    ufi->map, ufi->orig_rvaddr);
#endif

	/*
	 * check protection
	 */
	if ((ufi->entry->protection & flt->access_type) != flt->access_type) {
		uvmfault_unlockmaps(ufi, write_locked);
		return EACCES;
	}

	/*
	 * "enter_prot" is the protection we want to enter the page in at.
	 * for certain pages (e.g. copy-on-write pages) this protection can
	 * be more strict than ufi->entry->protection.  "wired" means either
	 * the entry is wired or we are fault-wiring the pg.
	 */
	flt->enter_prot = ufi->entry->protection;
	flt->pa_flags = UVM_ET_ISWC(ufi->entry) ? PMAP_WC : 0;
	if (VM_MAPENT_ISWIRED(ufi->entry) || (fault_type == VM_FAULT_WIRE)) {
		flt->wired = TRUE;
		flt->access_type = flt->enter_prot; /* full access for wired */
		/*  don't look for neighborhood * pages on "wire" fault */
		flt->narrow = TRUE;
		/* wiring pages requires a write lock. */
		flt->upper_lock_type = RW_WRITE;
		flt->lower_lock_type = RW_WRITE;
	}

	/* handle "needs_copy" case. */
	if (UVM_ET_ISNEEDSCOPY(ufi->entry)) {
		if ((flt->access_type & PROT_WRITE) ||
		    (ufi->entry->object.uvm_obj == NULL)) {
			/* modifying `ufi->entry' requires write lock */
			if (!write_locked) {
				write_locked = TRUE;
				if (!vm_map_upgrade(ufi->map)) {
					uvmfault_unlockmaps(ufi, FALSE);
					goto lookup;
				}
			}

			amap_copy(ufi->map, ufi->entry, M_NOWAIT,
			    UVM_ET_ISSTACK(ufi->entry) ? FALSE : TRUE,
			    ufi->orig_rvaddr, ufi->orig_rvaddr + 1);

			/* didn't work?  must be out of RAM. */
			if (UVM_ET_ISNEEDSCOPY(ufi->entry)) {
				uvmfault_unlockmaps(ufi, write_locked);
				uvm_wait("fltamapcopy");
				return ERESTART;
			}

			counters_inc(uvmexp_counters, flt_amcopy);
		} else {
			/*
			 * ensure that we pmap_enter page R/O since
			 * needs_copy is still true
			 */
			flt->enter_prot &= ~PROT_WRITE;
		}
	}

	if (write_locked) {
		vm_map_downgrade(ufi->map);
		write_locked = FALSE;
	}

	/*
	 * identify the players
	 */
	amap = ufi->entry->aref.ar_amap;	/* upper layer */
	uobj = ufi->entry->object.uvm_obj;	/* lower layer */

	/*
	 * check for a case 0 fault.  if nothing backing the entry then
	 * error now.
	 */
	if (amap == NULL && uobj == NULL) {
		uvmfault_unlockmaps(ufi, FALSE);
		return EFAULT;
	}

	/*
	 * for a case 2B fault waste no time on adjacent pages because
	 * they are likely already entered.
	 */
	if (uobj != NULL && amap != NULL &&
	    (flt->access_type & PROT_WRITE) != 0) {
		/* wide fault (!narrow) */
		flt->narrow = TRUE;
	}

	/*
	 * establish range of interest based on advice from mapper
	 * and then clip to fit map entry.   note that we only want
	 * to do this the first time through the fault.   if we
	 * ReFault we will disable this by setting "narrow" to true.
	 */
	if (flt->narrow == FALSE) {

		/* wide fault (!narrow) */
		nback = min(uvmadvice[ufi->entry->advice].nback,
		    (ufi->orig_rvaddr - ufi->entry->start) >> PAGE_SHIFT);
		flt->startva = ufi->orig_rvaddr - ((vsize_t)nback << PAGE_SHIFT);
		nforw = min(uvmadvice[ufi->entry->advice].nforw,
		    ((ufi->entry->end - ufi->orig_rvaddr) >> PAGE_SHIFT) - 1);
		/*
		 * note: "-1" because we don't want to count the
		 * faulting page as forw
		 */
		flt->npages = nback + nforw + 1;
		flt->centeridx = nback;

		flt->narrow = TRUE;	/* ensure only once per-fault */
	} else {
		/* narrow fault! */
		nback = nforw = 0;
		flt->startva = ufi->orig_rvaddr;
		flt->npages = 1;
		flt->centeridx = 0;
	}

	/*
	 * if we've got an amap then lock it and extract current anons.
	 */
	if (amap) {
		if ((flt->access_type & PROT_WRITE) != 0) {
			/*
			 * assume we're about to COW.
			 */
			flt->upper_lock_type = RW_WRITE;
		}
		amap_lock(amap, flt->upper_lock_type);
		amap_lookups(&ufi->entry->aref,
		    flt->startva - ufi->entry->start, *ranons, flt->npages);
	} else {
		*ranons = NULL;	/* to be safe */
	}

	if ((flt->access_type & PROT_WRITE) != 0) {
		/*
		 * we are about to dirty the object and that
		 * requires a write lock.
		 */
		flt->lower_lock_type = RW_WRITE;
	}

	/*
	 * for MADV_SEQUENTIAL mappings we want to deactivate the back pages
	 * now and then forget about them (for the rest of the fault).
	 */
	if (ufi->entry->advice == MADV_SEQUENTIAL && nback != 0) {
		/* flush back-page anons? */
		if (amap)
			uvmfault_anonflush(*ranons, nback);

		/*
		 * flush object?  change lock type to RW_WRITE, to avoid
		 * excessive competition between read/write locks if many
		 * threads doing "sequential access".
		 */
		if (uobj) {
			voff_t uoff;

			uoff = (flt->startva - ufi->entry->start) + ufi->entry->offset;
			flt->lower_lock_type = RW_WRITE;
			rw_enter(uobj->vmobjlock, RW_WRITE);
			(void) uobj->pgops->pgo_flush(uobj, uoff, uoff +
			    ((vsize_t)nback << PAGE_SHIFT), PGO_DEACTIVATE);
			rw_exit(uobj->vmobjlock);
		}

		/* now forget about the backpages */
		if (amap)
			*ranons += nback;
		flt->startva += ((vsize_t)nback << PAGE_SHIFT);
		flt->npages -= nback;
		flt->centeridx = 0;
	}

	return 0;
}

/*
 * uvm_fault_upper_upgrade: upgrade upper lock, reader -> writer
 */
static inline int
uvm_fault_upper_upgrade(struct uvm_faultctx *flt, struct vm_amap *amap)
{
	KASSERT(flt->upper_lock_type == rw_status(amap->am_lock));

	/*
	 * fast path.
	 */
	if (flt->upper_lock_type == RW_WRITE) {
		return 0;
	}

	/*
	 * otherwise try for the upgrade.  if we don't get it, unlock
	 * everything, restart the fault and next time around get a writer
	 * lock.
	 */
	flt->upper_lock_type = RW_WRITE;
	if (rw_enter(amap->am_lock, RW_UPGRADE|RW_NOSLEEP)) {
		counters_inc(uvmexp_counters, flt_noup);
		return ERESTART;
	}
	counters_inc(uvmexp_counters, flt_up);
	KASSERT(flt->upper_lock_type == rw_status(amap->am_lock));
	return 0;
}

/*
 * uvm_fault_upper_lookup: look up existing h/w mapping and amap.
 *
 * iterate range of interest:
 *	1. check if h/w mapping exists.  if yes, we don't care
 *	2. check if anon exists.  if not, page is lower.
 *	3. if anon exists, enter h/w mapping for neighbors.
 *
 * => called with amap locked (if exists).
 */
boolean_t
uvm_fault_upper_lookup(struct uvm_faultinfo *ufi,
    const struct uvm_faultctx *flt, struct vm_anon **anons,
    struct vm_page **pages)
{
	struct vm_amap *amap = ufi->entry->aref.ar_amap;
	struct vm_anon *anon;
	struct vm_page *pg;
	boolean_t shadowed;
	vaddr_t currva;
	paddr_t pa;
	int lcv, entered = 0;

	KASSERT(amap == NULL ||
	    rw_status(amap->am_lock) == flt->upper_lock_type);

	/*
	 * map in the backpages and frontpages we found in the amap in hopes
	 * of preventing future faults.    we also init the pages[] array as
	 * we go.
	 */
	currva = flt->startva;
	shadowed = FALSE;
	for (lcv = 0; lcv < flt->npages; lcv++, currva += PAGE_SIZE) {
		/*
		 * unmapped or center page.   check if any anon at this level.
		 */
		if (amap == NULL || anons[lcv] == NULL) {
			pages[lcv] = NULL;
			continue;
		}

		/*
		 * check for present page and map if possible.
		 */
		pages[lcv] = PGO_DONTCARE;
		if (lcv == flt->centeridx) {	/* save center for later! */
			shadowed = TRUE;
			continue;
		}

		anon = anons[lcv];
		pg = anon->an_page;

		KASSERT(anon->an_lock == amap->am_lock);

		/*
		 * ignore busy pages.
		 * don't play with VAs that are already mapped.
		 */
		if (pg && (pg->pg_flags & (PG_RELEASED|PG_BUSY)) == 0 &&
		    !pmap_extract(ufi->orig_map->pmap, currva, &pa)) {
			uvm_lock_pageq();
			uvm_pageactivate(pg);	/* reactivate */
			uvm_unlock_pageq();
			counters_inc(uvmexp_counters, flt_namap);

			/* No fault-ahead when wired. */
			KASSERT(flt->wired == FALSE);

			/*
			 * Since this isn't the page that's actually faulting,
			 * ignore pmap_enter() failures; it's not critical
			 * that we enter these right now.
			 */
			(void) pmap_enter(ufi->orig_map->pmap, currva,
			    VM_PAGE_TO_PHYS(pg) | flt->pa_flags,
			    (anon->an_ref > 1) ?
			    (flt->enter_prot & ~PROT_WRITE) : flt->enter_prot,
			    PMAP_CANFAIL);
			entered++;
		}
	}
	if (entered > 0)
		pmap_update(ufi->orig_map->pmap);

	return shadowed;
}

/*
 * uvm_fault_upper: handle upper fault.
 *
 *	1. acquire anon lock.
 *	2. get anon.  let uvmfault_anonget do the dirty work.
 *	3. if COW, promote data to new anon
 *	4. enter h/w mapping
 */
int
uvm_fault_upper(struct uvm_faultinfo *ufi, struct uvm_faultctx *flt,
   struct vm_anon **anons)
{
	struct vm_amap *amap = ufi->entry->aref.ar_amap;
	struct vm_anon *oanon, *anon = anons[flt->centeridx];
	struct vm_page *pg = NULL;
	int error, ret;

	KASSERT(rw_status(amap->am_lock) == flt->upper_lock_type);
	KASSERT(anon->an_lock == amap->am_lock);

	/*
	 * no matter if we have case 1A or case 1B we are going to need to
	 * have the anon's memory resident.   ensure that now.
	 */
	/*
	 * let uvmfault_anonget do the dirty work.
	 * if it fails (!OK) it will unlock everything for us.
	 * if it succeeds, locks are still valid and locked.
	 * also, if it is OK, then the anon's page is on the queues.
	 */
retry:
	error = uvmfault_anonget(ufi, amap, anon);
	switch (error) {
	case 0:
		break;

	case ERESTART:
		return ERESTART;

	case ENOLCK:
		/* it needs a write lock: retry */
		error = uvm_fault_upper_upgrade(flt, amap);
		if (error != 0) {
			uvmfault_unlockall(ufi, amap, NULL);
			return error;
		}
		KASSERT(rw_write_held(amap->am_lock));
		goto retry;

	default:
		return error;
	}

	KASSERT(rw_status(amap->am_lock) == flt->upper_lock_type);
	KASSERT(anon->an_lock == amap->am_lock);

	/*
	 * if we are case 1B then we will need to allocate a new blank
	 * anon to transfer the data into.   note that we have a lock
	 * on anon, so no one can busy or release the page until we are done.
	 * also note that the ref count can't drop to zero here because
	 * it is > 1 and we are only dropping one ref.
	 *
	 * in the (hopefully very rare) case that we are out of RAM we
	 * will unlock, wait for more RAM, and refault.
	 *
	 * if we are out of anon VM we wait for RAM to become available.
	 */
	if ((flt->access_type & PROT_WRITE) != 0 && anon->an_ref > 1) {
		/* promoting requires a write lock. */
		error = uvm_fault_upper_upgrade(flt, amap);
		if (error != 0) {
			uvmfault_unlockall(ufi, amap, NULL);
			return error;
		}
		KASSERT(rw_write_held(amap->am_lock));

		counters_inc(uvmexp_counters, flt_acow);
		oanon = anon;		/* oanon = old */

		error = uvmfault_promote(ufi, oanon->an_page, &anon, &pg);
		if (error)
			return error;

		/* un-busy! new page */
		atomic_clearbits_int(&pg->pg_flags, PG_BUSY|PG_FAKE);
		UVM_PAGE_OWN(pg, NULL);
		ret = amap_add(&ufi->entry->aref,
		    ufi->orig_rvaddr - ufi->entry->start, anon, 1);
		KASSERT(ret == 0);

		KASSERT(anon->an_lock == oanon->an_lock);

		/* deref: can not drop to zero here by defn! */
		KASSERT(oanon->an_ref > 1);
		oanon->an_ref--;

		/*
		 * note: oanon is still locked, as is the new anon.  we
		 * need to check for this later when we unlock oanon; if
		 * oanon != anon, we'll have to unlock anon, too.
		 */
		KASSERT(anon->an_lock == amap->am_lock);
		KASSERT(oanon->an_lock == amap->am_lock);

#if defined(MULTIPROCESSOR) && !defined(__HAVE_PMAP_MPSAFE_ENTER_COW)
		/*
		 * If there are multiple threads, either uvm or the
		 * pmap has to make sure no threads see the old RO
		 * mapping once any have seen the new RW mapping.
		 * uvm does it by inserting the new mapping RO and
		 * letting it fault again.
		 * This is only a problem on MP systems.
		 */
		if (P_HASSIBLING(curproc)) {
			flt->enter_prot &= ~PROT_WRITE;
			flt->access_type &= ~PROT_WRITE;
		}
#endif
	} else {
		counters_inc(uvmexp_counters, flt_anon);
		oanon = anon;
		pg = anon->an_page;
		if (anon->an_ref > 1)     /* disallow writes to ref > 1 anons */
			flt->enter_prot = flt->enter_prot & ~PROT_WRITE;
	}

	/*
	 * now map the page in .
	 */
	if (pmap_enter(ufi->orig_map->pmap, ufi->orig_rvaddr,
	    VM_PAGE_TO_PHYS(pg) | flt->pa_flags, flt->enter_prot,
	    flt->access_type | PMAP_CANFAIL | (flt->wired ? PMAP_WIRED : 0)) != 0) {
		/*
		 * No need to undo what we did; we can simply think of
		 * this as the pmap throwing away the mapping information.
		 *
		 * We do, however, have to go through the ReFault path,
		 * as the map may change while we're asleep.
		 */
		uvmfault_unlockall(ufi, amap, NULL);
		if (uvm_swapisfull()) {
			/* XXX instrumentation */
			return ENOMEM;
		}
#ifdef __HAVE_PMAP_POPULATE
		pmap_populate(ufi->orig_map->pmap, ufi->orig_rvaddr);
#else
		/* XXX instrumentation */
		uvm_wait("flt_pmfail1");
#endif
		return ERESTART;
	}

	/*
	 * ... update the page queues.
	 */
	uvm_lock_pageq();
	if (flt->wired) {
		uvm_pagewire(pg);
	} else {
		uvm_pageactivate(pg);
	}
	uvm_unlock_pageq();

	if (flt->wired) {
		/*
		 * since the now-wired page cannot be paged out,
		 * release its swap resources for others to use.
		 * since an anon with no swap cannot be PG_CLEAN,
		 * clear its clean flag now.
		 */
		atomic_clearbits_int(&pg->pg_flags, PG_CLEAN);
		uvm_anon_dropswap(anon);
	}

	/*
	 * done case 1!  finish up by unlocking everything and returning success
	 */
	uvmfault_unlockall(ufi, amap, NULL);
	pmap_update(ufi->orig_map->pmap);
	return 0;
}

/*
 * uvm_fault_lower_lookup: look up on-memory uobj pages.
 *
 *	1. get on-memory pages.
 *	2. if failed, give up (get only center page later).
 *	3. if succeeded, enter h/w mapping of neighbor pages.
 */

struct vm_page *
uvm_fault_lower_lookup(
	struct uvm_faultinfo *ufi, const struct uvm_faultctx *flt,
	struct vm_page **pages)
{
	struct uvm_object *uobj = ufi->entry->object.uvm_obj;
	struct vm_page *uobjpage = NULL;
	int lcv, gotpages, entered;
	vaddr_t currva;
	paddr_t pa;

	rw_enter(uobj->vmobjlock, flt->lower_lock_type);

	counters_inc(uvmexp_counters, flt_lget);
	gotpages = flt->npages;
	(void) uobj->pgops->pgo_get(uobj,
	    ufi->entry->offset + (flt->startva - ufi->entry->start),
	    pages, &gotpages, flt->centeridx,
	    flt->access_type & MASK(ufi->entry), ufi->entry->advice,
	    PGO_LOCKED);

	/*
	 * check for pages to map, if we got any
	 */
	if (gotpages == 0) {
		return NULL;
	}

	entered = 0;
	currva = flt->startva;
	for (lcv = 0; lcv < flt->npages; lcv++, currva += PAGE_SIZE) {
		if (pages[lcv] == NULL ||
		    pages[lcv] == PGO_DONTCARE)
			continue;

		KASSERT((pages[lcv]->pg_flags & PG_BUSY) == 0);
		KASSERT((pages[lcv]->pg_flags & PG_RELEASED) == 0);

		/*
		 * if center page is resident and not PG_BUSY, then pgo_get
		 * gave us a handle to it.
		 * remember this page as "uobjpage." (for later use).
		 */
		if (lcv == flt->centeridx) {
			uobjpage = pages[lcv];
			continue;
		}

		if (pmap_extract(ufi->orig_map->pmap, currva, &pa))
			continue;

		/*
		 * calling pgo_get with PGO_LOCKED returns us pages which
		 * are neither busy nor released, so we don't need to check
		 * for this.  we can just directly enter the pages.
		 */
		if (pages[lcv]->wire_count == 0) {
			uvm_lock_pageq();
			uvm_pageactivate(pages[lcv]);
			uvm_unlock_pageq();
		}
		counters_inc(uvmexp_counters, flt_nomap);

		/* No fault-ahead when wired. */
		KASSERT(flt->wired == FALSE);

		/*
		 * Since this page isn't the page that's actually faulting,
		 * ignore pmap_enter() failures; it's not critical that we
		 * enter these right now.
		 * NOTE: page can't be PG_WANTED or PG_RELEASED because we've
		 * held the lock the whole time we've had the handle.
		 */
		(void) pmap_enter(ufi->orig_map->pmap, currva,
		    VM_PAGE_TO_PHYS(pages[lcv]) | flt->pa_flags,
		    flt->enter_prot & MASK(ufi->entry), PMAP_CANFAIL);
		entered++;

	}
	if (entered > 0)
		pmap_update(ufi->orig_map->pmap);

	return uobjpage;
}

/*
 * uvm_fault_lower_upgrade: upgrade lower lock, reader -> writer
 */
static inline int
uvm_fault_lower_upgrade(struct uvm_faultctx *flt, struct uvm_object *uobj)
{
	KASSERT(flt->lower_lock_type == rw_status(uobj->vmobjlock));

	/*
	 * fast path.
	 */
	if (flt->lower_lock_type == RW_WRITE)
		return 0;

	/*
	 * otherwise try for the upgrade.  if we don't get it, unlock
	 * everything, restart the fault and next time around get a writer
	 * lock.
	 */
	flt->lower_lock_type = RW_WRITE;
	if (rw_enter(uobj->vmobjlock, RW_UPGRADE|RW_NOSLEEP)) {
		counters_inc(uvmexp_counters, flt_noup);
		return ERESTART;
	}
	counters_inc(uvmexp_counters, flt_up);
	KASSERT(flt->lower_lock_type == rw_status(uobj->vmobjlock));
	return 0;
}
/*
 * uvm_fault_lower: handle lower fault.
 *
 *	1. check uobj
 *	1.1. if null, ZFOD.
 *	1.2. if not null, look up unmapped neighbor pages.
 *	2. for center page, check if promote.
 *	2.1. ZFOD always needs promotion.
 *	2.2. other uobjs, when entry is marked COW (usually MAP_PRIVATE vnode).
 *	3. if uobj is not ZFOD and page is not found, do i/o.
 *	4. dispatch either direct / promote fault.
 */
int
uvm_fault_lower(struct uvm_faultinfo *ufi, struct uvm_faultctx *flt,
   struct vm_page **pages)
{
	struct vm_amap *amap = ufi->entry->aref.ar_amap;
	struct uvm_object *uobj = ufi->entry->object.uvm_obj;
	int dropswap = 0;
	struct vm_page *uobjpage, *pg = NULL;
	struct vm_anon *anon = NULL;
	int error;

	/*
	 * now, if the desired page is not shadowed by the amap and we have
	 * a backing object that does not have a special fault routine, then
	 * we ask (with pgo_get) the object for resident pages that we care
	 * about and attempt to map them in.  we do not let pgo_get block
	 * (PGO_LOCKED).
	 */
	if (uobj == NULL) {
		/* zero fill; don't care neighbor pages */
		uobjpage = NULL;
	} else {
		uobjpage = uvm_fault_lower_lookup(ufi, flt, pages);
	}

	/*
	 * note that at this point we are done with any front or back pages.
	 * we are now going to focus on the center page (i.e. the one we've
	 * faulted on).  if we have faulted on the bottom (uobj)
	 * layer [i.e. case 2] and the page was both present and available,
	 * then we've got a pointer to it as "uobjpage" and we've already
	 * made it BUSY.
	 */

	KASSERT(amap == NULL ||
	    rw_status(amap->am_lock) == flt->upper_lock_type);
	KASSERT(uobj == NULL ||
	    rw_status(uobj->vmobjlock) == flt->lower_lock_type);

	/*
	 * note that uobjpage can not be PGO_DONTCARE at this point.  we now
	 * set uobjpage to PGO_DONTCARE if we are doing a zero fill.  if we
	 * have a backing object, check and see if we are going to promote
	 * the data up to an anon during the fault.
	 */
	if (uobj == NULL) {
		uobjpage = PGO_DONTCARE;
		flt->promote = TRUE;		/* always need anon here */
	} else {
		KASSERT(uobjpage != PGO_DONTCARE);
		flt->promote = (flt->access_type & PROT_WRITE) &&
		     UVM_ET_ISCOPYONWRITE(ufi->entry);
	}

	/*
	 * if uobjpage is not null then we do not need to do I/O to get the
	 * uobjpage.
	 *
	 * if uobjpage is null, then we need to ask the pager to
	 * get the data for us.   once we have the data, we need to reverify
	 * the state the world.   we are currently not holding any resources.
	 */
	if (uobjpage) {
		/* update rusage counters */
		curproc->p_ru.ru_minflt++;
		if (uobjpage != PGO_DONTCARE) {
			uvm_lock_pageq();
			uvm_pageactivate(uobjpage);
			uvm_unlock_pageq();
		}
	} else {
		error = uvm_fault_lower_io(ufi, flt, &uobj, &uobjpage);
		if (error != 0)
			return error;
	}

	/*
	 * notes:
	 *  - at this point uobjpage can not be NULL
	 *  - at this point uobjpage could be PG_WANTED (handle later)
	 */
	if (flt->promote == FALSE) {
		/*
		 * we are not promoting.   if the mapping is COW ensure that we
		 * don't give more access than we should (e.g. when doing a read
		 * fault on a COPYONWRITE mapping we want to map the COW page in
		 * R/O even though the entry protection could be R/W).
		 *
		 * set "pg" to the page we want to map in (uobjpage, usually)
		 */
		counters_inc(uvmexp_counters, flt_obj);
		if (UVM_ET_ISCOPYONWRITE(ufi->entry))
			flt->enter_prot &= ~PROT_WRITE;
		pg = uobjpage;		/* map in the actual object */

		/* assert(uobjpage != PGO_DONTCARE) */

		/*
		 * we are faulting directly on the page.
		 */
	} else {
		KASSERT(amap != NULL);

		/* promoting requires a write lock. */
		error = uvm_fault_upper_upgrade(flt, amap);
		if (error != 0) {
			uvmfault_unlockall(ufi, amap, uobj);
			return error;
		}
	        KASSERT(rw_write_held(amap->am_lock));
	        KASSERT(uobj == NULL ||
	            rw_status(uobj->vmobjlock) == flt->lower_lock_type);

		/*
		 * if we are going to promote the data to an anon we
		 * allocate a blank anon here and plug it into our amap.
		 */
		error = uvmfault_promote(ufi, uobjpage, &anon, &pg);
		if (error)
			return error;

		/*
		 * fill in the data
		 */
		if (uobjpage != PGO_DONTCARE) {
			counters_inc(uvmexp_counters, flt_prcopy);

			/*
			 * promote to shared amap?  make sure all sharing
			 * procs see it
			 */
			if ((amap_flags(amap) & AMAP_SHARED) != 0) {
				pmap_page_protect(uobjpage, PROT_NONE);
			}
#if defined(MULTIPROCESSOR) && !defined(__HAVE_PMAP_MPSAFE_ENTER_COW)
			/*
			 * Otherwise:
			 * If there are multiple threads, either uvm or the
			 * pmap has to make sure no threads see the old RO
			 * mapping once any have seen the new RW mapping.
			 * uvm does it here by forcing it to PROT_NONE before
			 * inserting the new mapping.
			 */
			else if (P_HASSIBLING(curproc)) {
				pmap_page_protect(uobjpage, PROT_NONE);
			}
#endif
			/* done with copied uobjpage. */
			rw_exit(uobj->vmobjlock);
			uobj = NULL;
		} else {
			counters_inc(uvmexp_counters, flt_przero);
			/*
			 * Page is zero'd and marked dirty by uvm_pagealloc(),
			 * called in uvmfault_promote() above.
			 */
		}

		if (amap_add(&ufi->entry->aref,
		    ufi->orig_rvaddr - ufi->entry->start, anon, 0)) {
			if (pg->pg_flags & PG_WANTED)
				wakeup(pg);

			atomic_clearbits_int(&pg->pg_flags,
			    PG_BUSY|PG_FAKE|PG_WANTED);
			UVM_PAGE_OWN(pg, NULL);
			uvmfault_unlockall(ufi, amap, uobj);
			uvm_anfree(anon);
			counters_inc(uvmexp_counters, flt_noamap);

			if (uvm_swapisfull())
				return (ENOMEM);

			amap_populate(&ufi->entry->aref,
			    ufi->orig_rvaddr - ufi->entry->start);
			return ERESTART;
		}
	}

	/*
	 * anon must be write locked (promotion).  uobj can be either.
	 *
	 * Note: pg is either the uobjpage or the new page in the new anon.
	 */
	KASSERT(amap == NULL ||
	    rw_status(amap->am_lock) == flt->upper_lock_type);
	KASSERT(uobj == NULL ||
	    rw_status(uobj->vmobjlock) == flt->lower_lock_type);
	KASSERT(anon == NULL || anon->an_lock == amap->am_lock);

	/*
	 * all resources are present.   we can now map it in and free our
	 * resources.
	 */
	if (pmap_enter(ufi->orig_map->pmap, ufi->orig_rvaddr,
	    VM_PAGE_TO_PHYS(pg) | flt->pa_flags, flt->enter_prot,
	    flt->access_type | PMAP_CANFAIL | (flt->wired ? PMAP_WIRED : 0)) != 0) {
		/*
		 * No need to undo what we did; we can simply think of
		 * this as the pmap throwing away the mapping information.
		 *
		 * We do, however, have to go through the ReFault path,
		 * as the map may change while we're asleep.
		 */
		if (pg->pg_flags & PG_WANTED)
			wakeup(pg);

		atomic_clearbits_int(&pg->pg_flags, PG_BUSY|PG_FAKE|PG_WANTED);
		UVM_PAGE_OWN(pg, NULL);
		uvmfault_unlockall(ufi, amap, uobj);
		if (uvm_swapisfull()) {
			/* XXX instrumentation */
			return (ENOMEM);
		}
#ifdef __HAVE_PMAP_POPULATE
		pmap_populate(ufi->orig_map->pmap, ufi->orig_rvaddr);
#else
		/* XXX instrumentation */
		uvm_wait("flt_pmfail2");
#endif
		return ERESTART;
	}

	uvm_lock_pageq();
	if (flt->wired) {
		uvm_pagewire(pg);
		if (pg->pg_flags & PQ_AOBJ) {
			/*
			 * since the now-wired page cannot be paged out,
			 * release its swap resources for others to use.
			 * since an aobj page with no swap cannot be clean,
			 * mark it dirty now.
			 *
			 * use pg->uobject here.  if the page is from a
			 * tmpfs vnode, the pages are backed by its UAO and
			 * not the vnode.
			 */
			KASSERT(uobj != NULL);
			KASSERT(uobj->vmobjlock == pg->uobject->vmobjlock);
			atomic_clearbits_int(&pg->pg_flags, PG_CLEAN);
			dropswap = 1;
		}
	} else {
		uvm_pageactivate(pg);
	}
	uvm_unlock_pageq();

	if (dropswap)
		uao_dropswap(uobj, pg->offset >> PAGE_SHIFT);

	if (pg->pg_flags & PG_WANTED)
		wakeup(pg);

	atomic_clearbits_int(&pg->pg_flags, PG_BUSY|PG_FAKE|PG_WANTED);
	UVM_PAGE_OWN(pg, NULL);
	uvmfault_unlockall(ufi, amap, uobj);
	pmap_update(ufi->orig_map->pmap);

	return (0);
}

/*
 * uvm_fault_lower_io: get lower page from backing store.
 *
 *	1. unlock everything, because i/o will block.
 *	2. call pgo_get.
 *	3. if failed, recover.
 *	4. if succeeded, relock everything and verify things.
 */
int
uvm_fault_lower_io(
	struct uvm_faultinfo *ufi, struct uvm_faultctx *flt,
	struct uvm_object **ruobj, struct vm_page **ruobjpage)
{
	struct vm_amap * const amap = ufi->entry->aref.ar_amap;
	struct uvm_object *uobj = *ruobj;
	struct vm_page *pg;
	boolean_t locked;
	int gotpages, advice;
	int error, result;
	voff_t uoff;
	vm_prot_t access_type;

	/* grab everything we need from the entry before we unlock */
	uoff = (ufi->orig_rvaddr - ufi->entry->start) + ufi->entry->offset;
	access_type = flt->access_type & MASK(ufi->entry);
	advice = ufi->entry->advice;

	/* Upgrade to a write lock if needed. */
	error = uvm_fault_lower_upgrade(flt, uobj);
	if (error != 0) {
		uvmfault_unlockall(ufi, amap, uobj);
		return error;
	}
	uvmfault_unlockall(ufi, amap, NULL);

	/* update rusage counters */
	curproc->p_ru.ru_majflt++;

	KASSERT(rw_write_held(uobj->vmobjlock));

	counters_inc(uvmexp_counters, flt_get);
	gotpages = 1;
	pg = NULL;
	result = uobj->pgops->pgo_get(uobj, uoff, &pg, &gotpages,
	    0, access_type, advice, PGO_SYNCIO);

	/*
	 * recover from I/O
	 */
	if (result != VM_PAGER_OK) {
		KASSERT(result != VM_PAGER_PEND);

		if (result == VM_PAGER_AGAIN) {
			tsleep_nsec(&nowake, PVM, "fltagain2", MSEC_TO_NSEC(5));
			return ERESTART;
		}

		if (!UVM_ET_ISNOFAULT(ufi->entry))
			return (EIO);

		pg = PGO_DONTCARE;
		uobj = NULL;
		flt->promote = TRUE;
	}

	/* re-verify the state of the world.  */
	locked = uvmfault_relock(ufi);
	if (locked && amap != NULL)
		amap_lock(amap, flt->upper_lock_type);

	/* might be changed */
	if (pg != PGO_DONTCARE) {
		uobj = pg->uobject;
		rw_enter(uobj->vmobjlock, flt->lower_lock_type);
		KASSERT((pg->pg_flags & PG_BUSY) != 0);
		KASSERT(flt->lower_lock_type == RW_WRITE);
	}

	/*
	 * Re-verify that amap slot is still free. if there is
	 * a problem, we clean up.
	 */
	if (locked && amap && amap_lookup(&ufi->entry->aref,
	      ufi->orig_rvaddr - ufi->entry->start)) {
		if (locked)
			uvmfault_unlockall(ufi, amap, NULL);
		locked = FALSE;
	}

	/* release the page now, still holding object lock */
	if (pg != PGO_DONTCARE) {
		uvm_lock_pageq();
		uvm_pageactivate(pg);
		uvm_unlock_pageq();

		if (pg->pg_flags & PG_WANTED)
			wakeup(pg);
		atomic_clearbits_int(&pg->pg_flags, PG_BUSY|PG_WANTED);
		UVM_PAGE_OWN(pg, NULL);
	}

	if (locked == FALSE) {
		if (pg != PGO_DONTCARE)
			rw_exit(uobj->vmobjlock);
		return ERESTART;
	}

	/*
	 * we have the data in pg.  we are holding object lock (so the page
	 * can't be released on us).
	 */
	*ruobj = uobj;
	*ruobjpage = pg;
	return 0;
}

/*
 * uvm_fault_wire: wire down a range of virtual addresses in a map.
 *
 * => map may be read-locked by caller, but MUST NOT be write-locked.
 * => if map is read-locked, any operations which may cause map to
 *	be write-locked in uvm_fault() must be taken care of by
 *	the caller.  See uvm_map_pageable().
 */
int
uvm_fault_wire(vm_map_t map, vaddr_t start, vaddr_t end, vm_prot_t access_type)
{
	vaddr_t va;
	int rv;

	/*
	 * now fault it in a page at a time.   if the fault fails then we have
	 * to undo what we have done.   note that in uvm_fault PROT_NONE
	 * is replaced with the max protection if fault_type is VM_FAULT_WIRE.
	 */
	for (va = start ; va < end ; va += PAGE_SIZE) {
		rv = uvm_fault(map, va, VM_FAULT_WIRE, access_type);
		if (rv) {
			if (va != start) {
				uvm_fault_unwire(map, start, va);
			}
			return (rv);
		}
	}

	return (0);
}

/*
 * uvm_fault_unwire(): unwire range of virtual space.
 */
void
uvm_fault_unwire(vm_map_t map, vaddr_t start, vaddr_t end)
{

	vm_map_lock_read(map);
	uvm_fault_unwire_locked(map, start, end);
	vm_map_unlock_read(map);
}

/*
 * uvm_fault_unwire_locked(): the guts of uvm_fault_unwire().
 *
 * => map must be at least read-locked.
 */
void
uvm_fault_unwire_locked(vm_map_t map, vaddr_t start, vaddr_t end)
{
	vm_map_entry_t entry, oentry = NULL, next;
	pmap_t pmap = vm_map_pmap(map);
	vaddr_t va;
	paddr_t pa;
	struct vm_page *pg;

	KASSERT((map->flags & VM_MAP_INTRSAFE) == 0);
	vm_map_assert_anylock(map);

	/*
	 * we assume that the area we are unwiring has actually been wired
	 * in the first place.   this means that we should be able to extract
	 * the PAs from the pmap.
	 */

	/*
	 * find the beginning map entry for the region.
	 */
	KASSERT(start >= vm_map_min(map) && end <= vm_map_max(map));
	if (uvm_map_lookup_entry(map, start, &entry) == FALSE)
		panic("uvm_fault_unwire_locked: address not in map");

	for (va = start; va < end ; va += PAGE_SIZE) {
		/*
		 * find the map entry for the current address.
		 */
		KASSERT(va >= entry->start);
		while (va >= entry->end) {
			next = RBT_NEXT(uvm_map_addr, entry);
			KASSERT(next != NULL && next->start <= entry->end);
			entry = next;
		}

		/*
		 * lock it.
		 */
		if (entry != oentry) {
			if (oentry != NULL) {
				uvm_map_unlock_entry(oentry);
			}
			uvm_map_lock_entry(entry);
			oentry = entry;
		}

		if (!pmap_extract(pmap, va, &pa))
			continue;

		/*
		 * if the entry is no longer wired, tell the pmap.
		 */
		if (VM_MAPENT_ISWIRED(entry) == 0)
			pmap_unwire(pmap, va);

		pg = PHYS_TO_VM_PAGE(pa);
		if (pg) {
			uvm_lock_pageq();
			uvm_pageunwire(pg);
			uvm_unlock_pageq();
		}
	}

	if (oentry != NULL) {
		uvm_map_unlock_entry(entry);
	}
}

/*
 * uvmfault_unlockmaps: unlock the maps
 */
void
uvmfault_unlockmaps(struct uvm_faultinfo *ufi, boolean_t write_locked)
{
	/*
	 * ufi can be NULL when this isn't really a fault,
	 * but merely paging in anon data.
	 */
	if (ufi == NULL) {
		return;
	}

	uvmfault_update_stats(ufi);
	if (write_locked) {
		vm_map_unlock(ufi->map);
	} else {
		vm_map_unlock_read(ufi->map);
	}
}

/*
 * uvmfault_unlockall: unlock everything passed in.
 *
 * => maps must be read-locked (not write-locked).
 */
void
uvmfault_unlockall(struct uvm_faultinfo *ufi, struct vm_amap *amap,
    struct uvm_object *uobj)
{
	if (uobj)
		rw_exit(uobj->vmobjlock);
	if (amap != NULL)
		amap_unlock(amap);
	uvmfault_unlockmaps(ufi, FALSE);
}

/*
 * uvmfault_lookup: lookup a virtual address in a map
 *
 * => caller must provide a uvm_faultinfo structure with the IN
 *	params properly filled in
 * => we will lookup the map entry (handling submaps) as we go
 * => if the lookup is a success we will return with the maps locked
 * => if "write_lock" is TRUE, we write_lock the map, otherwise we only
 *	get a read lock.
 * => note that submaps can only appear in the kernel and they are
 *	required to use the same virtual addresses as the map they
 *	are referenced by (thus address translation between the main
 *	map and the submap is unnecessary).
 */

boolean_t
uvmfault_lookup(struct uvm_faultinfo *ufi, boolean_t write_lock)
{
	vm_map_t tmpmap;

	/*
	 * init ufi values for lookup.
	 */
	ufi->map = ufi->orig_map;
	ufi->size = ufi->orig_size;

	/*
	 * keep going down levels until we are done.   note that there can
	 * only be two levels so we won't loop very long.
	 */
	while (1) {
		if (ufi->orig_rvaddr < ufi->map->min_offset ||
		    ufi->orig_rvaddr >= ufi->map->max_offset)
			return FALSE;

		/* lock map */
		if (write_lock) {
			vm_map_lock(ufi->map);
		} else {
			vm_map_lock_read(ufi->map);
		}

		/* lookup */
		if (!uvm_map_lookup_entry(ufi->map, ufi->orig_rvaddr,
		    &ufi->entry)) {
			uvmfault_unlockmaps(ufi, write_lock);
			return FALSE;
		}

		/* reduce size if necessary */
		if (ufi->entry->end - ufi->orig_rvaddr < ufi->size)
			ufi->size = ufi->entry->end - ufi->orig_rvaddr;

		/*
		 * submap?    replace map with the submap and lookup again.
		 * note: VAs in submaps must match VAs in main map.
		 */
		if (UVM_ET_ISSUBMAP(ufi->entry)) {
			tmpmap = ufi->entry->object.sub_map;
			uvmfault_unlockmaps(ufi, write_lock);
			ufi->map = tmpmap;
			continue;
		}

		/*
		 * got it!
		 */
		ufi->mapv = ufi->map->timestamp;
		return TRUE;

	}	/* while loop */

	/*NOTREACHED*/
}

/*
 * uvmfault_relock: attempt to relock the same version of the map
 *
 * => fault data structures should be unlocked before calling.
 * => if a success (TRUE) maps will be locked after call.
 */
boolean_t
uvmfault_relock(struct uvm_faultinfo *ufi)
{
	/*
	 * ufi can be NULL when this isn't really a fault,
	 * but merely paging in anon data.
	 */
	if (ufi == NULL) {
		return TRUE;
	}

	/*
	 * relock map.   fail if version mismatch (in which case nothing
	 * gets locked).
	 */
	vm_map_lock_read(ufi->map);
	if (ufi->mapv != ufi->map->timestamp) {
		vm_map_unlock_read(ufi->map);
		counters_inc(uvmexp_counters, flt_norelck);
		return FALSE;
	}

	counters_inc(uvmexp_counters, flt_relck);
	return TRUE;		/* got it! */
}
