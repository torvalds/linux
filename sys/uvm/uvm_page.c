/*	$OpenBSD: uvm_page.c,v 1.183 2025/04/27 08:37:47 mpi Exp $	*/
/*	$NetBSD: uvm_page.c,v 1.44 2000/11/27 08:40:04 chs Exp $	*/

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
 *	@(#)vm_page.c   8.3 (Berkeley) 3/21/94
 * from: Id: uvm_page.c,v 1.1.2.18 1998/02/06 05:24:42 chs Exp
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
 * uvm_page.c: page ops.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sched.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/smr.h>

#include <uvm/uvm.h>

/*
 * for object trees
 */
RBT_GENERATE(uvm_objtree, vm_page, objt, uvm_pagecmp);

int
uvm_pagecmp(const struct vm_page *a, const struct vm_page *b)
{
	return a->offset < b->offset ? -1 : a->offset > b->offset;
}

/*
 * global vars... XXXCDC: move to uvm. structure.
 */
/*
 * physical memory config is stored in vm_physmem.
 */
struct vm_physseg vm_physmem[VM_PHYSSEG_MAX];	/* XXXCDC: uvm.physmem */
int vm_nphysseg = 0;				/* XXXCDC: uvm.nphysseg */

/*
 * Some supported CPUs in a given architecture don't support all
 * of the things necessary to do idle page zero'ing efficiently.
 * We therefore provide a way to disable it from machdep code here.
 */

/*
 * local variables
 */
/*
 * these variables record the values returned by vm_page_bootstrap,
 * for debugging purposes.  The implementation of uvm_pageboot_alloc
 * and pmap_startup here also uses them internally.
 */
static vaddr_t      virtual_space_start;
static vaddr_t      virtual_space_end;

/*
 * local prototypes
 */
static void uvm_pageinsert(struct vm_page *);
static void uvm_pageremove(struct vm_page *);
int uvm_page_owner_locked_p(struct vm_page *, boolean_t);

/*
 * inline functions
 */
/*
 * uvm_pageinsert: insert a page in the object
 *
 * => caller must lock object
 * => call should have already set pg's object and offset pointers
 *    and bumped the version counter
 */
static inline void
uvm_pageinsert(struct vm_page *pg)
{
	struct vm_page	*dupe;

	KASSERT(UVM_OBJ_IS_DUMMY(pg->uobject) ||
	    rw_write_held(pg->uobject->vmobjlock));
	KASSERT((pg->pg_flags & PG_TABLED) == 0);

	dupe = RBT_INSERT(uvm_objtree, &pg->uobject->memt, pg);
	/* not allowed to insert over another page */
	KASSERT(dupe == NULL);
	atomic_setbits_int(&pg->pg_flags, PG_TABLED);
	pg->uobject->uo_npages++;
}

/*
 * uvm_page_remove: remove page from object
 *
 * => caller must lock object
 */
static inline void
uvm_pageremove(struct vm_page *pg)
{
	KASSERT(UVM_OBJ_IS_DUMMY(pg->uobject) ||
	    rw_write_held(pg->uobject->vmobjlock));
	KASSERT(pg->pg_flags & PG_TABLED);

	RBT_REMOVE(uvm_objtree, &pg->uobject->memt, pg);

	atomic_clearbits_int(&pg->pg_flags, PG_TABLED);
	pg->uobject->uo_npages--;
	pg->uobject = NULL;
	pg->pg_version++;
}

/*
 * uvm_page_init: init the page system.   called from uvm_init().
 *
 * => we return the range of kernel virtual memory in kvm_startp/kvm_endp
 */
void
uvm_page_init(vaddr_t *kvm_startp, vaddr_t *kvm_endp)
{
	vsize_t freepages, pagecount, n;
	vm_page_t pagearray, curpg;
	int lcv, i;
	paddr_t paddr, pgno;
	struct vm_physseg *seg;

	/*
	 * init the page queues and page queue locks
	 */

	TAILQ_INIT(&uvm.page_active);
	TAILQ_INIT(&uvm.page_inactive);
	mtx_init(&uvm.pageqlock, IPL_VM);
	mtx_init(&uvm.fpageqlock, IPL_VM);
	uvm_pmr_init();

	/*
	 * allocate vm_page structures.
	 */

	/*
	 * sanity check:
	 * before calling this function the MD code is expected to register
	 * some free RAM with the uvm_page_physload() function.   our job
	 * now is to allocate vm_page structures for this memory.
	 */

	if (vm_nphysseg == 0)
		panic("uvm_page_bootstrap: no memory pre-allocated");

	/*
	 * first calculate the number of free pages...
	 *
	 * note that we use start/end rather than avail_start/avail_end.
	 * this allows us to allocate extra vm_page structures in case we
	 * want to return some memory to the pool after booting.
	 */

	freepages = 0;
	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg ; lcv++, seg++)
		freepages += (seg->end - seg->start);

	/*
	 * we now know we have (PAGE_SIZE * freepages) bytes of memory we can
	 * use.   for each page of memory we use we need a vm_page structure.
	 * thus, the total number of pages we can use is the total size of
	 * the memory divided by the PAGE_SIZE plus the size of the vm_page
	 * structure.   we add one to freepages as a fudge factor to avoid
	 * truncation errors (since we can only allocate in terms of whole
	 * pages).
	 */

	pagecount = (((paddr_t)freepages + 1) << PAGE_SHIFT) /
	    (PAGE_SIZE + sizeof(struct vm_page));
	pagearray = (vm_page_t)uvm_pageboot_alloc(pagecount *
	    sizeof(struct vm_page));
	memset(pagearray, 0, pagecount * sizeof(struct vm_page));

	/* init the vm_page structures and put them in the correct place. */
	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg ; lcv++, seg++) {
		n = seg->end - seg->start;
		if (n > pagecount) {
			panic("uvm_page_init: lost %ld page(s) in init",
			    (long)(n - pagecount));
			    /* XXXCDC: shouldn't happen? */
			/* n = pagecount; */
		}

		/* set up page array pointers */
		seg->pgs = pagearray;
		pagearray += n;
		pagecount -= n;
		seg->lastpg = seg->pgs + (n - 1);

		/* init and free vm_pages (we've already zeroed them) */
		pgno = seg->start;
		paddr = ptoa(pgno);
		for (i = 0, curpg = seg->pgs; i < n;
		    i++, curpg++, pgno++, paddr += PAGE_SIZE) {
			curpg->phys_addr = paddr;
			VM_MDPAGE_INIT(curpg);
			curpg->uobject = NULL;
			curpg->uanon = NULL;
			if (pgno >= seg->avail_start &&
			    pgno < seg->avail_end) {
				uvmexp.npages++;
			}
		}

		/* Add pages to free pool. */
		uvm_pmr_freepages(&seg->pgs[seg->avail_start - seg->start],
		    seg->avail_end - seg->avail_start);
	}

	/*
	 * pass up the values of virtual_space_start and
	 * virtual_space_end (obtained by uvm_pageboot_alloc) to the upper
	 * layers of the VM.
	 */

	*kvm_startp = round_page(virtual_space_start);
	*kvm_endp = trunc_page(virtual_space_end);

	/* init locks for kernel threads */
	mtx_init(&uvm.aiodoned_lock, IPL_BIO);

	/*
	 * init reserve thresholds.
	 *
	 * XXX As long as some disk drivers cannot write any physical
	 * XXX page, we need DMA reachable reserves for the pagedaemon.
	 * XXX We cannot enforce such requirement but it should be ok
	 * XXX in most of the cases because the pmemrange tries hard to
	 * XXX allocate them last.
	 */
	uvmexp.reserve_pagedaemon = 4;
	uvmexp.reserve_kernel = uvmexp.reserve_pagedaemon + 4;

	uvm.page_init_done = TRUE;
}

/*
 * uvm_setpagesize: set the page size
 *
 * => sets page_shift and page_mask from uvmexp.pagesize.
 */
void
uvm_setpagesize(void)
{
	if (uvmexp.pagesize == 0)
		uvmexp.pagesize = DEFAULT_PAGE_SIZE;
	uvmexp.pagemask = uvmexp.pagesize - 1;
	if ((uvmexp.pagemask & uvmexp.pagesize) != 0)
		panic("uvm_setpagesize: page size not a power of two");
	for (uvmexp.pageshift = 0; ; uvmexp.pageshift++)
		if ((1 << uvmexp.pageshift) == uvmexp.pagesize)
			break;
}

/*
 * uvm_pageboot_alloc: steal memory from physmem for bootstrapping
 */
vaddr_t
uvm_pageboot_alloc(vsize_t size)
{
#if defined(PMAP_STEAL_MEMORY)
	vaddr_t addr;

	/*
	 * defer bootstrap allocation to MD code (it may want to allocate
	 * from a direct-mapped segment).  pmap_steal_memory should round
	 * off virtual_space_start/virtual_space_end.
	 */

	addr = pmap_steal_memory(size, &virtual_space_start,
	    &virtual_space_end);

	return addr;

#else /* !PMAP_STEAL_MEMORY */

	static boolean_t initialized = FALSE;
	vaddr_t addr, vaddr;
	paddr_t paddr;

	/* round to page size */
	size = round_page(size);

	/* on first call to this function, initialize ourselves. */
	if (initialized == FALSE) {
		pmap_virtual_space(&virtual_space_start, &virtual_space_end);

		/* round it the way we like it */
		virtual_space_start = round_page(virtual_space_start);
		virtual_space_end = trunc_page(virtual_space_end);

		initialized = TRUE;
	}

	/* allocate virtual memory for this request */
	if (virtual_space_start == virtual_space_end ||
	    (virtual_space_end - virtual_space_start) < size)
		panic("uvm_pageboot_alloc: out of virtual space");

	addr = virtual_space_start;

#ifdef PMAP_GROWKERNEL
	/*
	 * If the kernel pmap can't map the requested space,
	 * then allocate more resources for it.
	 */
	if (uvm_maxkaddr < (addr + size)) {
		uvm_maxkaddr = pmap_growkernel(addr + size);
		if (uvm_maxkaddr < (addr + size))
			panic("uvm_pageboot_alloc: pmap_growkernel() failed");
	}
#endif

	virtual_space_start += size;

	/* allocate and mapin physical pages to back new virtual pages */
	for (vaddr = round_page(addr) ; vaddr < addr + size ;
	    vaddr += PAGE_SIZE) {
		if (!uvm_page_physget(&paddr))
			panic("uvm_pageboot_alloc: out of memory");

		/*
		 * Note this memory is no longer managed, so using
		 * pmap_kenter is safe.
		 */
		pmap_kenter_pa(vaddr, paddr, PROT_READ | PROT_WRITE);
	}
	pmap_update(pmap_kernel());
	return addr;
#endif	/* PMAP_STEAL_MEMORY */
}

#if !defined(PMAP_STEAL_MEMORY)
/*
 * uvm_page_physget: "steal" one page from the vm_physmem structure.
 *
 * => attempt to allocate it off the end of a segment in which the "avail"
 *    values match the start/end values.   if we can't do that, then we
 *    will advance both values (making them equal, and removing some
 *    vm_page structures from the non-avail area).
 * => return false if out of memory.
 */

boolean_t
uvm_page_physget(paddr_t *paddrp)
{
	int lcv;
	struct vm_physseg *seg;

	/* pass 1: try allocating from a matching end */
#if (VM_PHYSSEG_STRAT == VM_PSTRAT_BIGFIRST) || \
	(VM_PHYSSEG_STRAT == VM_PSTRAT_BSEARCH)
	for (lcv = vm_nphysseg - 1, seg = vm_physmem + lcv; lcv >= 0;
	    lcv--, seg--)
#else
	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg ; lcv++, seg++)
#endif
	{
		if (uvm.page_init_done == TRUE)
			panic("uvm_page_physget: called _after_ bootstrap");

		/* try from front */
		if (seg->avail_start == seg->start &&
		    seg->avail_start < seg->avail_end) {
			*paddrp = ptoa(seg->avail_start);
			seg->avail_start++;
			seg->start++;
			/* nothing left?   nuke it */
			if (seg->avail_start == seg->end) {
				if (vm_nphysseg == 1)
				    panic("uvm_page_physget: out of memory!");
				vm_nphysseg--;
				for (; lcv < vm_nphysseg; lcv++, seg++)
					/* structure copy */
					seg[0] = seg[1];
			}
			return TRUE;
		}

		/* try from rear */
		if (seg->avail_end == seg->end &&
		    seg->avail_start < seg->avail_end) {
			*paddrp = ptoa(seg->avail_end - 1);
			seg->avail_end--;
			seg->end--;
			/* nothing left?   nuke it */
			if (seg->avail_end == seg->start) {
				if (vm_nphysseg == 1)
				    panic("uvm_page_physget: out of memory!");
				vm_nphysseg--;
				for (; lcv < vm_nphysseg ; lcv++, seg++)
					/* structure copy */
					seg[0] = seg[1];
			}
			return TRUE;
		}
	}

	/* pass2: forget about matching ends, just allocate something */
#if (VM_PHYSSEG_STRAT == VM_PSTRAT_BIGFIRST) || \
	(VM_PHYSSEG_STRAT == VM_PSTRAT_BSEARCH)
	for (lcv = vm_nphysseg - 1, seg = vm_physmem + lcv; lcv >= 0;
	    lcv--, seg--)
#else
	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg ; lcv++, seg++)
#endif
	{

		/* any room in this bank? */
		if (seg->avail_start >= seg->avail_end)
			continue;  /* nope */

		*paddrp = ptoa(seg->avail_start);
		seg->avail_start++;
		/* truncate! */
		seg->start = seg->avail_start;

		/* nothing left?   nuke it */
		if (seg->avail_start == seg->end) {
			if (vm_nphysseg == 1)
				panic("uvm_page_physget: out of memory!");
			vm_nphysseg--;
			for (; lcv < vm_nphysseg ; lcv++, seg++)
				/* structure copy */
				seg[0] = seg[1];
		}
		return TRUE;
	}

	return FALSE;        /* whoops! */
}

#endif /* PMAP_STEAL_MEMORY */

/*
 * uvm_page_physload: load physical memory into VM system
 *
 * => all args are PFs
 * => all pages in start/end get vm_page structures
 * => areas marked by avail_start/avail_end get added to the free page pool
 * => we are limited to VM_PHYSSEG_MAX physical memory segments
 */

void
uvm_page_physload(paddr_t start, paddr_t end, paddr_t avail_start,
    paddr_t avail_end, int flags)
{
	int preload, lcv;
	psize_t npages;
	struct vm_page *pgs;
	struct vm_physseg *ps, *seg;

#ifdef DIAGNOSTIC
	if (uvmexp.pagesize == 0)
		panic("uvm_page_physload: page size not set!");

	if (start >= end)
		panic("uvm_page_physload: start >= end");
#endif

	/* do we have room? */
	if (vm_nphysseg == VM_PHYSSEG_MAX) {
		printf("uvm_page_physload: unable to load physical memory "
		    "segment\n");
		printf("\t%d segments allocated, ignoring 0x%llx -> 0x%llx\n",
		    VM_PHYSSEG_MAX, (long long)start, (long long)end);
		printf("\tincrease VM_PHYSSEG_MAX\n");
		return;
	}

	/*
	 * check to see if this is a "preload" (i.e. uvm_mem_init hasn't been
	 * called yet, so malloc is not available).
	 */
	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg; lcv++, seg++) {
		if (seg->pgs)
			break;
	}
	preload = (lcv == vm_nphysseg);

	/* if VM is already running, attempt to malloc() vm_page structures */
	if (!preload) {
		/*
		 * XXXCDC: need some sort of lockout for this case
		 * right now it is only used by devices so it should be alright.
		 */
 		paddr_t paddr;

 		npages = end - start;  /* # of pages */

		pgs = km_alloc(round_page(npages * sizeof(*pgs)),
		    &kv_any, &kp_zero, &kd_waitok);
		if (pgs == NULL) {
			printf("uvm_page_physload: can not malloc vm_page "
			    "structs for segment\n");
			printf("\tignoring 0x%lx -> 0x%lx\n", start, end);
			return;
		}
		/* init phys_addr and free pages, XXX uvmexp.npages */
		for (lcv = 0, paddr = ptoa(start); lcv < npages;
		    lcv++, paddr += PAGE_SIZE) {
			pgs[lcv].phys_addr = paddr;
			VM_MDPAGE_INIT(&pgs[lcv]);
			pgs[lcv].uobject = NULL;
			pgs[lcv].uanon = NULL;
			if (atop(paddr) >= avail_start &&
			    atop(paddr) < avail_end) {
				if (flags & PHYSLOAD_DEVICE) {
					atomic_setbits_int(&pgs[lcv].pg_flags,
					    PG_DEV);
					pgs[lcv].wire_count = 1;
				} else {
#if defined(VM_PHYSSEG_NOADD)
		panic("uvm_page_physload: tried to add RAM after vm_mem_init");
#endif
				}
			}
		}

		/* Add pages to free pool. */
		if ((flags & PHYSLOAD_DEVICE) == 0) {
			uvm_pmr_freepages(&pgs[avail_start - start],
			    avail_end - avail_start);
		}

		/* XXXCDC: need hook to tell pmap to rebuild pv_list, etc... */
	} else {
		/* gcc complains if these don't get init'd */
		pgs = NULL;
		npages = 0;

	}

	/* now insert us in the proper place in vm_physmem[] */
#if (VM_PHYSSEG_STRAT == VM_PSTRAT_RANDOM)
	/* random: put it at the end (easy!) */
	ps = &vm_physmem[vm_nphysseg];
#elif (VM_PHYSSEG_STRAT == VM_PSTRAT_BSEARCH)
	{
		int x;
		/* sort by address for binary search */
		for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg; lcv++, seg++)
			if (start < seg->start)
				break;
		ps = seg;
		/* move back other entries, if necessary ... */
		for (x = vm_nphysseg, seg = vm_physmem + x - 1; x > lcv;
		    x--, seg--)
			/* structure copy */
			seg[1] = seg[0];
	}
#elif (VM_PHYSSEG_STRAT == VM_PSTRAT_BIGFIRST)
	{
		int x;
		/* sort by largest segment first */
		for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg; lcv++, seg++)
			if ((end - start) >
			    (seg->end - seg->start))
				break;
		ps = &vm_physmem[lcv];
		/* move back other entries, if necessary ... */
		for (x = vm_nphysseg, seg = vm_physmem + x - 1; x > lcv;
		    x--, seg--)
			/* structure copy */
			seg[1] = seg[0];
	}
#else
	panic("uvm_page_physload: unknown physseg strategy selected!");
#endif

	ps->start = start;
	ps->end = end;
	ps->avail_start = avail_start;
	ps->avail_end = avail_end;
	if (preload) {
		ps->pgs = NULL;
	} else {
		ps->pgs = pgs;
		ps->lastpg = pgs + npages - 1;
	}
	vm_nphysseg++;

	return;
}

#ifdef DDB /* XXXCDC: TMP TMP TMP DEBUG DEBUG DEBUG */

void uvm_page_physdump(void); /* SHUT UP GCC */

/* call from DDB */
void
uvm_page_physdump(void)
{
	int lcv;
	struct vm_physseg *seg;

	printf("uvm_page_physdump: physical memory config [segs=%d of %d]:\n",
	    vm_nphysseg, VM_PHYSSEG_MAX);
	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg ; lcv++, seg++)
		printf("0x%llx->0x%llx [0x%llx->0x%llx]\n",
		    (long long)seg->start,
		    (long long)seg->end,
		    (long long)seg->avail_start,
		    (long long)seg->avail_end);
	printf("STRATEGY = ");
	switch (VM_PHYSSEG_STRAT) {
	case VM_PSTRAT_RANDOM: printf("RANDOM\n"); break;
	case VM_PSTRAT_BSEARCH: printf("BSEARCH\n"); break;
	case VM_PSTRAT_BIGFIRST: printf("BIGFIRST\n"); break;
	default: printf("<<UNKNOWN>>!!!!\n");
	}
}
#endif

void
uvm_shutdown(void)
{
#ifdef UVM_SWAP_ENCRYPT
	uvm_swap_finicrypt_all();
#endif
	smr_flush();
}

/*
 * Perform insert of a given page in the specified anon of obj.
 * This is basically, uvm_pagealloc, but with the page already given.
 */
void
uvm_pagealloc_pg(struct vm_page *pg, struct uvm_object *obj, voff_t off,
    struct vm_anon *anon)
{
	int	flags;

	KASSERT(obj == NULL || anon == NULL);
	KASSERT(anon == NULL || off == 0);
	KASSERT(off == trunc_page(off));
	KASSERT(obj == NULL || UVM_OBJ_IS_DUMMY(obj) ||
	    rw_write_held(obj->vmobjlock));
	KASSERT(anon == NULL || anon->an_lock == NULL ||
	    rw_write_held(anon->an_lock));

	flags = PG_BUSY | PG_FAKE;
	pg->offset = off;
	pg->uobject = obj;
	pg->uanon = anon;
	KASSERT(uvm_page_owner_locked_p(pg, TRUE));
	if (anon) {
		anon->an_page = pg;
		flags |= PQ_ANON;
	} else if (obj)
		uvm_pageinsert(pg);
	atomic_setbits_int(&pg->pg_flags, flags);
#if defined(UVM_PAGE_TRKOWN)
	pg->owner_tag = NULL;
#endif
	UVM_PAGE_OWN(pg, "new alloc");
}

/*
 * uvm_pglistalloc: allocate a list of pages
 *
 * => allocated pages are placed at the tail of rlist.  rlist is
 *    assumed to be properly initialized by caller.
 * => returns 0 on success or errno on failure
 * => doesn't take into account clean non-busy pages on inactive list
 *	that could be used(?)
 * => params:
 *	size		the size of the allocation, rounded to page size.
 *	low		the low address of the allowed allocation range.
 *	high		the high address of the allowed allocation range.
 *	alignment	memory must be aligned to this power-of-two boundary.
 *	boundary	no segment in the allocation may cross this 
 *			power-of-two boundary (relative to zero).
 * => flags:
 *	UVM_PLA_NOWAIT	fail if allocation fails
 *	UVM_PLA_WAITOK	wait for memory to become avail
 *	UVM_PLA_ZERO	return zeroed memory
 */
int
uvm_pglistalloc(psize_t size, paddr_t low, paddr_t high, paddr_t alignment,
    paddr_t boundary, struct pglist *rlist, int nsegs, int flags)
{
	KASSERT((alignment & (alignment - 1)) == 0);
	KASSERT((boundary & (boundary - 1)) == 0);
	KASSERT(!(flags & UVM_PLA_WAITOK) ^ !(flags & UVM_PLA_NOWAIT));

	if (size == 0)
		return EINVAL;
	size = atop(round_page(size));

	/*
	 * XXX uvm_pglistalloc is currently only used for kernel
	 * objects. Unlike the checks in uvm_pagealloc, below, here
	 * we are always allowed to use the kernel reserve.
	 */
	flags |= UVM_PLA_USERESERVE;

	if ((high & PAGE_MASK) != PAGE_MASK) {
		printf("uvm_pglistalloc: Upper boundary 0x%lx "
		    "not on pagemask.\n", (unsigned long)high);
	}

	/*
	 * Our allocations are always page granularity, so our alignment
	 * must be, too.
	 */
	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;

	low = atop(roundup(low, alignment));
	/*
	 * high + 1 may result in overflow, in which case high becomes 0x0,
	 * which is the 'don't care' value.
	 * The only requirement in that case is that low is also 0x0, or the
	 * low<high assert will fail.
	 */
	high = atop(high + 1);
	alignment = atop(alignment);
	if (boundary < PAGE_SIZE && boundary != 0)
		boundary = PAGE_SIZE;
	boundary = atop(boundary);

	return uvm_pmr_getpages(size, low, high, alignment, boundary, nsegs,
	    flags, rlist);
}

/*
 * uvm_pglistfree: free a list of pages
 *
 * => pages should already be unmapped
 */
void
uvm_pglistfree(struct pglist *list)
{
	uvm_pmr_freepageq(list);
}

/*
 * interface used by the buffer cache to allocate a buffer at a time.
 * The pages are allocated wired in DMA accessible memory
 */
int
uvm_pagealloc_multi(struct uvm_object *obj, voff_t off, vsize_t size,
    int flags)
{
	struct pglist    plist;
	struct vm_page  *pg;
	int              i, r;

	KASSERT(UVM_OBJ_IS_BUFCACHE(obj));
	KERNEL_ASSERT_LOCKED();

	TAILQ_INIT(&plist);
	r = uvm_pglistalloc(size, dma_constraint.ucr_low,
	    dma_constraint.ucr_high, 0, 0, &plist, atop(round_page(size)),
	    flags);
	if (r == 0) {
		i = 0;
		while ((pg = TAILQ_FIRST(&plist)) != NULL) {
			pg->wire_count = 1;
			atomic_setbits_int(&pg->pg_flags, PG_CLEAN | PG_FAKE);
			KASSERT((pg->pg_flags & PG_DEV) == 0);
			TAILQ_REMOVE(&plist, pg, pageq);
			uvm_pagealloc_pg(pg, obj, off + ptoa(i++), NULL);
		}
	}
	return r;
}

/*
 * interface used by the buffer cache to reallocate a buffer at a time.
 * The pages are reallocated wired outside the DMA accessible region.
 *
 */
int
uvm_pagerealloc_multi(struct uvm_object *obj, voff_t off, vsize_t size,
    int flags, struct uvm_constraint_range *where)
{
	struct pglist    plist;
	struct vm_page  *pg, *tpg;
	int              i, r;
	voff_t		offset;

	KASSERT(UVM_OBJ_IS_BUFCACHE(obj));
	KERNEL_ASSERT_LOCKED();

	TAILQ_INIT(&plist);
	if (size == 0)
		panic("size 0 uvm_pagerealloc");
	r = uvm_pglistalloc(size, where->ucr_low, where->ucr_high, 0,
	    0, &plist, atop(round_page(size)), flags);
	if (r == 0) {
		i = 0;
		while((pg = TAILQ_FIRST(&plist)) != NULL) {
			offset = off + ptoa(i++);
			tpg = uvm_pagelookup(obj, offset);
			KASSERT(tpg != NULL);
			pg->wire_count = 1;
			atomic_setbits_int(&pg->pg_flags, PG_CLEAN | PG_FAKE);
			KASSERT((pg->pg_flags & PG_DEV) == 0);
			TAILQ_REMOVE(&plist, pg, pageq);
			uvm_pagecopy(tpg, pg);
			KASSERT(tpg->wire_count == 1);
			tpg->wire_count = 0;
			uvm_pagefree(tpg);
			uvm_pagealloc_pg(pg, obj, offset, NULL);
		}
	}
	return r;
}

/*
 * uvm_pagealloc: allocate vm_page from a particular free list.
 *
 * => return null if no pages free
 * => wake up pagedaemon if number of free pages drops below low water mark
 * => only one of obj or anon can be non-null
 * => caller must activate/deactivate page if it is not wired.
 */
struct vm_page *
uvm_pagealloc(struct uvm_object *obj, voff_t off, struct vm_anon *anon,
    int flags)
{
	struct vm_page *pg = NULL;
	int pmr_flags;

	KASSERT(obj == NULL || anon == NULL);
	KASSERT(anon == NULL || off == 0);
	KASSERT(off == trunc_page(off));
	KASSERT(obj == NULL || UVM_OBJ_IS_DUMMY(obj) ||
	    rw_write_held(obj->vmobjlock));
	KASSERT(anon == NULL || anon->an_lock == NULL ||
	    rw_write_held(anon->an_lock));

	pmr_flags = UVM_PLA_NOWAIT;

	/*
	 * We're allowed to use the kernel reserve if the page is
	 * being allocated to a kernel object.
	 */
	if ((flags & UVM_PGA_USERESERVE) ||
	    (obj != NULL && UVM_OBJ_IS_KERN_OBJECT(obj)))
	    	pmr_flags |= UVM_PLA_USERESERVE;

	if (flags & UVM_PGA_ZERO)
		pmr_flags |= UVM_PLA_ZERO;

	pg = uvm_pmr_cache_get(pmr_flags);
	if (pg == NULL)
		return NULL;
	uvm_pagealloc_pg(pg, obj, off, anon);
	KASSERT((pg->pg_flags & PG_DEV) == 0);
	if (flags & UVM_PGA_ZERO)
		atomic_clearbits_int(&pg->pg_flags, PG_CLEAN);
	else
		atomic_setbits_int(&pg->pg_flags, PG_CLEAN);

	return pg;
}

/*
 * uvm_pagerealloc: reallocate a page from one object to another
 */

void
uvm_pagerealloc(struct vm_page *pg, struct uvm_object *newobj, voff_t newoff)
{

	/* remove it from the old object */
	if (pg->uobject) {
		uvm_pageremove(pg);
	}

	/* put it in the new object */
	if (newobj) {
		pg->uobject = newobj;
		pg->offset = newoff;
		pg->pg_version++;
		uvm_pageinsert(pg);
	}
}

/*
 * uvm_pageclean: clean page
 *
 * => erase page's identity (i.e. remove from object)
 * => assumes all valid mappings of pg are gone
 */
void
uvm_pageclean(struct vm_page *pg)
{
	u_int flags_to_clear = 0;

#ifdef DEBUG
	if (pg->uobject == (void *)0xdeadbeef &&
	    pg->uanon == (void *)0xdeadbeef) {
		panic("uvm_pagefree: freeing free page %p", pg);
	}
#endif

	KASSERT((pg->pg_flags & PG_DEV) == 0);
	KASSERT(pg->uobject == NULL || UVM_OBJ_IS_DUMMY(pg->uobject) ||
	    rw_write_held(pg->uobject->vmobjlock));
	KASSERT(pg->uobject != NULL || pg->uanon == NULL ||
	    rw_write_held(pg->uanon->an_lock));

	/*
	 * if the page was an object page (and thus "TABLED"), remove it
	 * from the object.
	 */
	if (pg->pg_flags & PG_TABLED)
		uvm_pageremove(pg);

	/*
	 * now remove the page from the queues
	 */
	if (pg->pg_flags & (PQ_ACTIVE|PQ_INACTIVE)) {
		uvm_lock_pageq();
		uvm_pagedequeue(pg);
		uvm_unlock_pageq();
	}

	/*
	 * if the page was wired, unwire it now.
	 */
	if (pg->wire_count) {
		pg->wire_count = 0;
		atomic_dec_int(&uvmexp.wired);
	}
	if (pg->uanon) {
		pg->uanon->an_page = NULL;
		pg->uanon = NULL;
	}

	/* Clean page state bits. */
	flags_to_clear |= PQ_ANON|PQ_AOBJ|PQ_ENCRYPT|PG_ZERO|PG_FAKE|PG_BUSY|
	    PG_RELEASED|PG_CLEAN|PG_CLEANCHK;
	atomic_clearbits_int(&pg->pg_flags, flags_to_clear);

#ifdef DEBUG
	pg->uobject = (void *)0xdeadbeef;
	pg->offset = 0xdeadbeef;
	pg->uanon = (void *)0xdeadbeef;
#endif
}

/*
 * uvm_pagefree: free page
 *
 * => erase page's identity (i.e. remove from object)
 * => put page on free list
 * => caller must lock page queues if `pg' is managed
 * => assumes all valid mappings of pg are gone
 */
void
uvm_pagefree(struct vm_page *pg)
{
	uvm_pageclean(pg);
	uvm_pmr_cache_put(pg);
}

/*
 * uvm_page_unbusy: unbusy an array of pages.
 *
 * => pages must either all belong to the same object, or all belong to anons.
 * => if pages are object-owned, object must be locked.
 * => if pages are anon-owned, anons must have 0 refcount.
 * => caller must make sure that anon-owned pages are not PG_RELEASED.
 */
void
uvm_page_unbusy(struct vm_page **pgs, int npgs)
{
	struct vm_page *pg;
	int i;

	for (i = 0; i < npgs; i++) {
		pg = pgs[i];

		if (pg == NULL || pg == PGO_DONTCARE) {
			continue;
		}

		KASSERT(uvm_page_owner_locked_p(pg, TRUE));
		KASSERT(pg->pg_flags & PG_BUSY);

		if (pg->pg_flags & PG_WANTED) {
			wakeup(pg);
		}
		if (pg->pg_flags & PG_RELEASED) {
			KASSERT(pg->uobject != NULL ||
			    (pg->uanon != NULL && pg->uanon->an_ref > 0));
			atomic_clearbits_int(&pg->pg_flags, PG_RELEASED);
			pmap_page_protect(pg, PROT_NONE);
			uvm_pagefree(pg);
		} else {
			KASSERT((pg->pg_flags & PG_FAKE) == 0);
			atomic_clearbits_int(&pg->pg_flags, PG_WANTED|PG_BUSY);
			UVM_PAGE_OWN(pg, NULL);
		}
	}
}

/*
 * uvm_pagewait: wait for a busy page
 *
 * => page must be known PG_BUSY
 * => object must be locked
 * => object will be unlocked on return
 */
void
uvm_pagewait(struct vm_page *pg, struct rwlock *lock, const char *wmesg)
{
	KASSERT(rw_lock_held(lock));
	KASSERT((pg->pg_flags & PG_BUSY) != 0);
	KASSERT(uvm_page_owner_locked_p(pg, FALSE));

	atomic_setbits_int(&pg->pg_flags, PG_WANTED);
	rwsleep_nsec(pg, lock, PVM | PNORELOCK, wmesg, INFSLP);
}

#if defined(UVM_PAGE_TRKOWN)
/*
 * uvm_page_own: set or release page ownership
 *
 * => this is a debugging function that keeps track of who sets PG_BUSY
 *	and where they do it.   it can be used to track down problems
 *	such a thread setting "PG_BUSY" and never releasing it.
 * => if "tag" is NULL then we are releasing page ownership
 */
void
uvm_page_own(struct vm_page *pg, char *tag)
{
	/* gain ownership? */
	if (tag) {
		if (pg->owner_tag) {
			printf("uvm_page_own: page %p already owned "
			    "by thread %d [%s]\n", pg,
			     pg->owner, pg->owner_tag);
			panic("uvm_page_own");
		}
		pg->owner = (curproc) ? curproc->p_tid :  (pid_t) -1;
		pg->owner_tag = tag;
		return;
	}

	/* drop ownership */
	if (pg->owner_tag == NULL) {
		printf("uvm_page_own: dropping ownership of an non-owned "
		    "page (%p)\n", pg);
		panic("uvm_page_own");
	}
	pg->owner_tag = NULL;
	return;
}
#endif

/*
 * when VM_PHYSSEG_MAX is 1, we can simplify these functions
 */

#if VM_PHYSSEG_MAX > 1
/*
 * vm_physseg_find: find vm_physseg structure that belongs to a PA
 */
int
vm_physseg_find(paddr_t pframe, int *offp)
{
	struct vm_physseg *seg;

#if (VM_PHYSSEG_STRAT == VM_PSTRAT_BSEARCH)
	/* binary search for it */
	int	start, len, try;

	/*
	 * if try is too large (thus target is less than try) we reduce
	 * the length to trunc(len/2) [i.e. everything smaller than "try"]
	 *
	 * if the try is too small (thus target is greater than try) then
	 * we set the new start to be (try + 1).   this means we need to
	 * reduce the length to (round(len/2) - 1).
	 *
	 * note "adjust" below which takes advantage of the fact that
	 *  (round(len/2) - 1) == trunc((len - 1) / 2)
	 * for any value of len we may have
	 */

	for (start = 0, len = vm_nphysseg ; len != 0 ; len = len / 2) {
		try = start + (len / 2);	/* try in the middle */
		seg = vm_physmem + try;

		/* start past our try? */
		if (pframe >= seg->start) {
			/* was try correct? */
			if (pframe < seg->end) {
				if (offp)
					*offp = pframe - seg->start;
				return try;            /* got it */
			}
			start = try + 1;	/* next time, start here */
			len--;			/* "adjust" */
		} else {
			/*
			 * pframe before try, just reduce length of
			 * region, done in "for" loop
			 */
		}
	}
	return -1;

#else
	/* linear search for it */
	int	lcv;

	for (lcv = 0, seg = vm_physmem; lcv < vm_nphysseg ; lcv++, seg++) {
		if (pframe >= seg->start && pframe < seg->end) {
			if (offp)
				*offp = pframe - seg->start;
			return lcv;		   /* got it */
		}
	}
	return -1;

#endif
}

/*
 * PHYS_TO_VM_PAGE: find vm_page for a PA.   used by MI code to get vm_pages
 * back from an I/O mapping (ugh!).   used in some MD code as well.
 */
struct vm_page *
PHYS_TO_VM_PAGE(paddr_t pa)
{
	paddr_t pf = atop(pa);
	int	off;
	int	psi;

	psi = vm_physseg_find(pf, &off);

	return (psi == -1) ? NULL : &vm_physmem[psi].pgs[off];
}
#endif /* VM_PHYSSEG_MAX > 1 */

/*
 * uvm_pagelookup: look up a page
 */
struct vm_page *
uvm_pagelookup(struct uvm_object *obj, voff_t off)
{
	/* XXX if stack is too much, handroll */
	struct vm_page p, *pg;

	p.offset = off;
	pg = RBT_FIND(uvm_objtree, &obj->memt, &p);

	KASSERT(pg == NULL || obj->uo_npages != 0);
	KASSERT(pg == NULL || (pg->pg_flags & PG_RELEASED) == 0 ||
	    (pg->pg_flags & PG_BUSY) != 0);
	return (pg);
}

/*
 * uvm_pagewire: wire the page, thus removing it from the daemon's grasp
 *
 * => caller must lock page queues
 */
void
uvm_pagewire(struct vm_page *pg)
{
	KASSERT(uvm_page_owner_locked_p(pg, TRUE));
	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	if (pg->wire_count == 0) {
		uvm_pagedequeue(pg);
		atomic_inc_int(&uvmexp.wired);
	}
	pg->wire_count++;
}

/*
 * uvm_pageunwire: unwire the page.
 *
 * => activate if wire count goes to zero.
 * => caller must lock page queues
 */
void
uvm_pageunwire(struct vm_page *pg)
{
	KASSERT(uvm_page_owner_locked_p(pg, TRUE));
	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	pg->wire_count--;
	if (pg->wire_count == 0) {
		uvm_pageactivate(pg);
		atomic_dec_int(&uvmexp.wired);
	}
}

/*
 * uvm_pagedeactivate: deactivate page.
 *
 * => caller must lock page queues
 * => caller must check to make sure page is not wired
 * => object that page belongs to must be locked (so we can adjust pg->flags)
 */
void
uvm_pagedeactivate(struct vm_page *pg)
{
	KASSERT(uvm_page_owner_locked_p(pg, FALSE));
	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	pmap_page_protect(pg, PROT_NONE);

	if (pg->pg_flags & PQ_ACTIVE) {
		TAILQ_REMOVE(&uvm.page_active, pg, pageq);
		atomic_clearbits_int(&pg->pg_flags, PQ_ACTIVE);
		uvmexp.active--;
	}
	if ((pg->pg_flags & PQ_INACTIVE) == 0) {
		KASSERT(pg->wire_count == 0);
		TAILQ_INSERT_TAIL(&uvm.page_inactive, pg, pageq);
		atomic_setbits_int(&pg->pg_flags, PQ_INACTIVE);
		uvmexp.inactive++;
		pmap_clear_reference(pg);
		/*
		 * update the "clean" bit.  this isn't 100%
		 * accurate, and doesn't have to be.  we'll
		 * re-sync it after we zap all mappings when
		 * scanning the inactive list.
		 */
		if ((pg->pg_flags & PG_CLEAN) != 0 &&
		    pmap_is_modified(pg))
			atomic_clearbits_int(&pg->pg_flags, PG_CLEAN);
	}
}

/*
 * uvm_pageactivate: activate page
 *
 * => caller must lock page queues
 */
void
uvm_pageactivate(struct vm_page *pg)
{
	KASSERT(uvm_page_owner_locked_p(pg, FALSE));
	MUTEX_ASSERT_LOCKED(&uvm.pageqlock);

	uvm_pagedequeue(pg);
	if (pg->wire_count == 0) {
		TAILQ_INSERT_TAIL(&uvm.page_active, pg, pageq);
		atomic_setbits_int(&pg->pg_flags, PQ_ACTIVE);
		uvmexp.active++;

	}
}

/*
 * uvm_pagedequeue: remove a page from any paging queue
 */
void
uvm_pagedequeue(struct vm_page *pg)
{
	if (pg->pg_flags & PQ_ACTIVE) {
		TAILQ_REMOVE(&uvm.page_active, pg, pageq);
		atomic_clearbits_int(&pg->pg_flags, PQ_ACTIVE);
		uvmexp.active--;
	}
	if (pg->pg_flags & PQ_INACTIVE) {
		TAILQ_REMOVE(&uvm.page_inactive, pg, pageq);
		atomic_clearbits_int(&pg->pg_flags, PQ_INACTIVE);
		uvmexp.inactive--;
	}
}
/*
 * uvm_pagezero: zero fill a page
 */
void
uvm_pagezero(struct vm_page *pg)
{
	atomic_clearbits_int(&pg->pg_flags, PG_CLEAN);
	pmap_zero_page(pg);
}

/*
 * uvm_pagecopy: copy a page
 */
void
uvm_pagecopy(struct vm_page *src, struct vm_page *dst)
{
	atomic_clearbits_int(&dst->pg_flags, PG_CLEAN);
	pmap_copy_page(src, dst);
}

/*
 * uvm_page_owner_locked_p: return true if object associated with page is
 * locked.  this is a weak check for runtime assertions only.
 */
int
uvm_page_owner_locked_p(struct vm_page *pg, boolean_t exclusive)
{
	if (pg->uobject != NULL) {
		if (UVM_OBJ_IS_DUMMY(pg->uobject))
			return 1;
		return exclusive
		    ? rw_write_held(pg->uobject->vmobjlock)
		    : rw_lock_held(pg->uobject->vmobjlock);
	}
	if (pg->uanon != NULL) {
		return exclusive
		    ? rw_write_held(pg->uanon->an_lock)
		    : rw_lock_held(pg->uanon->an_lock);
	}
	return 1;
}

/*
 * uvm_pagecount: count the number of physical pages in the address range.
 */
psize_t
uvm_pagecount(struct uvm_constraint_range* constraint)
{
	int lcv;
	psize_t sz;
	paddr_t low, high;
	paddr_t ps_low, ps_high;

	/* Algorithm uses page numbers. */
	low = atop(constraint->ucr_low);
	high = atop(constraint->ucr_high);

	sz = 0;
	for (lcv = 0; lcv < vm_nphysseg; lcv++) {
		ps_low = MAX(low, vm_physmem[lcv].avail_start);
		ps_high = MIN(high, vm_physmem[lcv].avail_end);
		if (ps_low < ps_high)
			sz += ps_high - ps_low;
	}
	return sz;
}
