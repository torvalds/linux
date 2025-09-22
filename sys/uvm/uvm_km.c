/*	$OpenBSD: uvm_km.c,v 1.157 2025/07/14 08:42:54 jsg Exp $	*/
/*	$NetBSD: uvm_km.c,v 1.42 2001/01/14 02:10:01 thorpej Exp $	*/

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
 *	@(#)vm_kern.c   8.3 (Berkeley) 1/12/94
 * from: Id: uvm_km.c,v 1.1.2.14 1998/02/06 05:19:27 chs Exp
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
 * uvm_km.c: handle kernel memory allocation and management
 */

/*
 * overview of kernel memory management:
 *
 * the kernel virtual address space is mapped by "kernel_map."   kernel_map
 * starts at a machine-dependent address and is VM_KERNEL_SPACE_SIZE bytes
 * large.
 *
 * the kernel_map has several "submaps."   submaps can only appear in 
 * the kernel_map (user processes can't use them).   submaps "take over"
 * the management of a sub-range of the kernel's address space.  submaps
 * are typically allocated at boot time and are never released.   kernel
 * virtual address space that is mapped by a submap is locked by the 
 * submap's lock -- not the kernel_map's lock.
 *
 * thus, the useful feature of submaps is that they allow us to break
 * up the locking and protection of the kernel address space into smaller
 * chunks.
 *
 * The VM system has several standard kernel submaps:
 *   kmem_map: Contains only wired kernel memory for malloc(9).
 *	       Note: All access to this map must be protected by splvm as
 *	       calls to malloc(9) are allowed in interrupt handlers.
 *   exec_map: Memory to hold arguments to system calls are allocated from
 *	       this map.
 *	       XXX: This is primarily used to artificially limit the number
 *	       of concurrent processes doing an exec.
 *   phys_map: Buffers for vmapbuf (physio) are allocated from this map.
 *
 * the kernel allocates its private memory out of special uvm_objects whose
 * reference count is set to UVM_OBJ_KERN (thus indicating that the objects
 * are "special" and never die).   all kernel objects should be thought of
 * as large, fixed-sized, sparsely populated uvm_objects.   each kernel 
 * object is equal to the size of kernel virtual address space (i.e.
 * VM_KERNEL_SPACE_SIZE).
 *
 * most kernel private memory lives in kernel_object.   the only exception
 * to this is for memory that belongs to submaps that must be protected
 * by splvm(). each of these submaps manages their own pages.
 *
 * note that just because a kernel object spans the entire kernel virtual
 * address space doesn't mean that it has to be mapped into the entire space.
 * large chunks of a kernel object's space go unused either because 
 * that area of kernel VM is unmapped, or there is some other type of 
 * object mapped into that range (e.g. a vnode).    for submap's kernel
 * objects, the only part of the object that can ever be populated is the
 * offsets that are managed by the submap.
 *
 * note that the "offset" in a kernel object is always the kernel virtual
 * address minus the vm_map_min(kernel_map).
 * example:
 *   suppose kernel_map starts at 0xf8000000 and the kernel does a
 *   km_alloc(PAGE_SIZE, &kv_any, &kp_none, &kd_waitok)) [allocate 1 wired
 *   down page in the kernel map].    if km_alloc() returns virtual address
 *   0xf8235000, then that means that the page at offset 0x235000 in
 *   kernel_object is mapped at 0xf8235000.
 *
 * kernel objects have one other special property: when the kernel virtual
 * memory mapping them is unmapped, the backing memory in the object is
 * freed right away.   this is done with the uvm_km_pgremove() function.
 * this has to be done because there is no backing store for kernel pages
 * and no need to save them after they are no longer referenced.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <uvm/uvm.h>

/*
 * global data structures
 */

struct vm_map *kernel_map = NULL;

/* Unconstraint range. */
struct uvm_constraint_range	no_constraint = { 0x0, (paddr_t)-1 };

/*
 * local data structures
 */
static struct vm_map		kernel_map_store;

/*
 * uvm_km_init: init kernel maps and objects to reflect reality (i.e.
 * KVM already allocated for text, data, bss, and static data structures).
 *
 * => KVM is defined by [base.. base + VM_KERNEL_SPACE_SIZE].
 *    we assume that [base -> start] has already been allocated and that
 *    "end" is the end of the kernel image span.
 */
void
uvm_km_init(vaddr_t base, vaddr_t start, vaddr_t end)
{
	/* kernel_object: for pageable anonymous kernel memory */
	uao_init();
	uvm.kernel_object = uao_create(VM_KERNEL_SPACE_SIZE, UAO_FLAG_KERNOBJ);

	/*
	 * init the map and reserve already allocated kernel space 
	 * before installing.
	 */

	uvm_map_setup(&kernel_map_store, pmap_kernel(), base, end,
#ifdef KVA_GUARDPAGES
	    VM_MAP_PAGEABLE | VM_MAP_GUARDPAGES
#else
	    VM_MAP_PAGEABLE
#endif
	    );
	if (base != start && uvm_map(&kernel_map_store, &base, start - base,
	    NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
	    MAP_INHERIT_NONE, MADV_RANDOM, UVM_FLAG_FIXED)) != 0)
		panic("uvm_km_init: could not reserve space for kernel");
	
	kernel_map = &kernel_map_store;

#ifndef __HAVE_PMAP_DIRECT
	/* allow km_alloc calls before uvm_km_thread starts */
	mtx_init(&uvm_km_pages.mtx, IPL_VM);
#endif
}

/*
 * uvm_km_suballoc: allocate a submap in the kernel map.   once a submap
 * is allocated all references to that area of VM must go through it.  this
 * allows the locking of VAs in kernel_map to be broken up into regions.
 *
 * => if `fixed' is true, *min specifies where the region described
 *      by the submap must start
 * => if submap is non NULL we use that as the submap, otherwise we
 *	alloc a new map
 */
struct vm_map *
uvm_km_suballoc(struct vm_map *map, vaddr_t *min, vaddr_t *max, vsize_t size,
    int flags, boolean_t fixed, struct vm_map *submap)
{
	int mapflags = UVM_FLAG_NOMERGE | (fixed ? UVM_FLAG_FIXED : 0);

	size = round_page(size);	/* round up to pagesize */

	/* first allocate a blank spot in the parent map */
	if (uvm_map(map, min, size, NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
	    MAP_INHERIT_NONE, MADV_RANDOM, mapflags)) != 0) {
	       panic("uvm_km_suballoc: unable to allocate space in parent map");
	}

	/* set VM bounds (min is filled in by uvm_map) */
	*max = *min + size;

	/* add references to pmap and create or init the submap */
	pmap_reference(vm_map_pmap(map));
	if (submap == NULL) {
		submap = uvm_map_create(vm_map_pmap(map), *min, *max, flags);
		if (submap == NULL)
			panic("uvm_km_suballoc: unable to create submap");
	} else {
		uvm_map_setup(submap, vm_map_pmap(map), *min, *max, flags);
	}

	/*
	 * now let uvm_map_submap plug in it...
	 */
	if (uvm_map_submap(map, *min, *max, submap) != 0)
		panic("uvm_km_suballoc: submap allocation failed");

	return(submap);
}

/*
 * uvm_km_pgremove: remove pages from a kernel uvm_object.
 *
 * => when you unmap a part of anonymous kernel memory you want to toss
 *    the pages right away.    (this gets called from uvm_unmap_...).
 */
void
uvm_km_pgremove(struct uvm_object *uobj, vaddr_t startva, vaddr_t endva)
{
	const voff_t start = startva - vm_map_min(kernel_map);
	const voff_t end = endva - vm_map_min(kernel_map);
	struct vm_page *pp;
	voff_t curoff;
	int slot;
	int swpgonlydelta = 0;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));
	KASSERT(rw_write_held(uobj->vmobjlock));

	pmap_remove(pmap_kernel(), startva, endva);
	for (curoff = start ; curoff < end ; curoff += PAGE_SIZE) {
		pp = uvm_pagelookup(uobj, curoff);
		if (pp && pp->pg_flags & PG_BUSY) {
			uvm_pagewait(pp, uobj->vmobjlock, "km_pgrm");
			rw_enter(uobj->vmobjlock, RW_WRITE);
			curoff -= PAGE_SIZE; /* loop back to us */
			continue;
		}

		/* free the swap slot, then the page */
		slot = uao_dropswap(uobj, curoff >> PAGE_SHIFT);

		if (pp != NULL) {
			uvm_pagefree(pp);
		} else if (slot != 0) {
			swpgonlydelta++;
		}
	}

	if (swpgonlydelta > 0) {
		KASSERT(uvmexp.swpgonly >= swpgonlydelta);
		atomic_add_int(&uvmexp.swpgonly, -swpgonlydelta);
	}
}


/*
 * uvm_km_pgremove_intrsafe: like uvm_km_pgremove(), but for "intrsafe"
 *    objects
 *
 * => when you unmap a part of anonymous kernel memory you want to toss
 *    the pages right away.    (this gets called from uvm_unmap_...).
 * => none of the pages will ever be busy, and none of them will ever
 *    be on the active or inactive queues (because these objects are
 *    never allowed to "page").
 */
void
uvm_km_pgremove_intrsafe(vaddr_t start, vaddr_t end)
{
	struct vm_page *pg;
	vaddr_t va;
	paddr_t pa;

	for (va = start; va < end; va += PAGE_SIZE) {
		if (!pmap_extract(pmap_kernel(), va, &pa))
			continue;
		pg = PHYS_TO_VM_PAGE(pa);
		if (pg == NULL)
			panic("uvm_km_pgremove_intrsafe: no page");
		uvm_pagefree(pg);
	}
	pmap_kremove(start, end - start);
}

/*
 * uvm_km_kmemalloc: lower level kernel memory allocator for malloc()
 *
 * => we map wired memory into the specified map using the obj passed in
 * => NOTE: we can return NULL even if we can wait if there is not enough
 *	free VM space in the map... caller should be prepared to handle
 *	this case.
 * => we return KVA of memory allocated
 * => flags: NOWAIT, VALLOC - just allocate VA, TRYLOCK - fail if we can't
 *	lock the map
 * => low, high, alignment, boundary, nsegs are the corresponding parameters
 *	to uvm_pglistalloc
 * => flags: ZERO - correspond to uvm_pglistalloc flags
 */
vaddr_t
uvm_km_kmemalloc_pla(struct vm_map *map, struct uvm_object *obj, vsize_t size,
    vsize_t valign, int flags, paddr_t low, paddr_t high, paddr_t alignment,
    paddr_t boundary, int nsegs)
{
	vaddr_t kva, loopva;
	voff_t offset;
	struct vm_page *pg;
	struct pglist pgl;
	int pla_flags;

	KASSERT(vm_map_pmap(map) == pmap_kernel());
	/* UVM_KMF_VALLOC => !UVM_KMF_ZERO */
	KASSERT(!(flags & UVM_KMF_VALLOC) ||
	    !(flags & UVM_KMF_ZERO));

	/* setup for call */
	size = round_page(size);
	kva = vm_map_min(map);	/* hint */
	if (nsegs == 0)
		nsegs = atop(size);

	/* allocate some virtual space */
	if (__predict_false(uvm_map(map, &kva, size, obj, UVM_UNKNOWN_OFFSET,
	    valign, UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
	    MAP_INHERIT_NONE, MADV_RANDOM, (flags & UVM_KMF_TRYLOCK))) != 0)) {
		return 0;
	}

	/* if all we wanted was VA, return now */
	if (flags & UVM_KMF_VALLOC) {
		return kva;
	}

	/* recover object offset from virtual address */
	if (obj != NULL)
		offset = kva - vm_map_min(kernel_map);
	else
		offset = 0;

	/*
	 * now allocate and map in the memory... note that we are the only ones
	 * whom should ever get a handle on this area of VM.
	 */
	TAILQ_INIT(&pgl);
	pla_flags = 0;
	KASSERT(uvmexp.swpgonly <= uvmexp.swpages);
	if ((flags & UVM_KMF_NOWAIT) ||
	    ((flags & UVM_KMF_CANFAIL) &&
	    uvmexp.swpages - uvmexp.swpgonly <= atop(size)))
		pla_flags |= UVM_PLA_NOWAIT;
	else
		pla_flags |= UVM_PLA_WAITOK;
	if (flags & UVM_KMF_ZERO)
		pla_flags |= UVM_PLA_ZERO;
	if (uvm_pglistalloc(size, low, high, alignment, boundary, &pgl, nsegs,
	    pla_flags) != 0) {
		/* Failed. */
		uvm_unmap(map, kva, kva + size);
		return (0);
	}

	if (obj != NULL)
		rw_enter(obj->vmobjlock, RW_WRITE);

	loopva = kva;
	while (loopva != kva + size) {
		pg = TAILQ_FIRST(&pgl);
		TAILQ_REMOVE(&pgl, pg, pageq);
		uvm_pagealloc_pg(pg, obj, offset, NULL);
		atomic_clearbits_int(&pg->pg_flags, PG_BUSY);
		UVM_PAGE_OWN(pg, NULL);

		/*
		 * map it in: note that we call pmap_enter with the map and
		 * object unlocked in case we are kmem_map.
		 */
		if (obj == NULL) {
			pmap_kenter_pa(loopva, VM_PAGE_TO_PHYS(pg),
			    PROT_READ | PROT_WRITE);
		} else {
			pmap_enter(map->pmap, loopva, VM_PAGE_TO_PHYS(pg),
			    PROT_READ | PROT_WRITE,
			    PROT_READ | PROT_WRITE | PMAP_WIRED);
		}
		loopva += PAGE_SIZE;
		offset += PAGE_SIZE;
	}
	KASSERT(TAILQ_EMPTY(&pgl));
	pmap_update(pmap_kernel());

	if (obj != NULL)
		rw_exit(obj->vmobjlock);

	return kva;
}

/*
 * uvm_km_free: free an area of kernel memory
 */
void
uvm_km_free(struct vm_map *map, vaddr_t addr, vsize_t size)
{
	uvm_unmap(map, trunc_page(addr), round_page(addr+size));
}

#if defined(__HAVE_PMAP_DIRECT)
/*
 * uvm_km_page allocator, __HAVE_PMAP_DIRECT arch
 * On architectures with machine memory direct mapped into a portion
 * of KVM, we have very little work to do.  Just get a physical page,
 * and find and return its VA.
 */
void
uvm_km_page_init(void)
{
	/* nothing */
}

void
uvm_km_page_lateinit(void)
{
	/* nothing */
}

#else
/*
 * uvm_km_page allocator, non __HAVE_PMAP_DIRECT archs
 * This is a special allocator that uses a reserve of free pages
 * to fulfill requests.  It is fast and interrupt safe, but can only
 * return page sized regions.  Its primary use is as a backend for pool.
 *
 * The memory returned is allocated from the larger kernel_map, sparing
 * pressure on the small interrupt-safe kmem_map.  It is wired, but
 * not zero filled.
 */

struct uvm_km_pages uvm_km_pages;

void uvm_km_createthread(void *);
void uvm_km_thread(void *);
struct uvm_km_free_page *uvm_km_doputpage(struct uvm_km_free_page *);

/*
 * Allocate the initial reserve, and create the thread which will
 * keep the reserve full.  For bootstrapping, we allocate more than
 * the lowat amount, because it may be a while before the thread is
 * running.
 */
void
uvm_km_page_init(void)
{
	int	lowat_min;
	int	i;
	int	len, bulk;
	vaddr_t	addr;

	if (!uvm_km_pages.lowat) {
		/* based on physmem, calculate a good value here */
		uvm_km_pages.lowat = physmem / 256;
		lowat_min = physmem < atop(16 * 1024 * 1024) ? 32 : 128;
		if (uvm_km_pages.lowat < lowat_min)
			uvm_km_pages.lowat = lowat_min;
	}
	if (uvm_km_pages.lowat > UVM_KM_PAGES_LOWAT_MAX)
		uvm_km_pages.lowat = UVM_KM_PAGES_LOWAT_MAX;
	uvm_km_pages.hiwat = 4 * uvm_km_pages.lowat;
	if (uvm_km_pages.hiwat > UVM_KM_PAGES_HIWAT_MAX)
		uvm_km_pages.hiwat = UVM_KM_PAGES_HIWAT_MAX;

	/* Allocate all pages in as few allocations as possible. */
	len = 0;
	bulk = uvm_km_pages.hiwat;
	while (len < uvm_km_pages.hiwat && bulk > 0) {
		bulk = MIN(bulk, uvm_km_pages.hiwat - len);
		addr = vm_map_min(kernel_map);
		if (uvm_map(kernel_map, &addr, (vsize_t)bulk << PAGE_SHIFT,
		    NULL, UVM_UNKNOWN_OFFSET, 0,
		    UVM_MAPFLAG(PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE, MAP_INHERIT_NONE,
		    MADV_RANDOM, UVM_KMF_TRYLOCK)) != 0) {
			bulk /= 2;
			continue;
		}

		for (i = len; i < len + bulk; i++, addr += PAGE_SIZE)
			uvm_km_pages.page[i] = addr;
		len += bulk;
	}

	uvm_km_pages.free = len;
	for (i = len; i < UVM_KM_PAGES_HIWAT_MAX; i++)
		uvm_km_pages.page[i] = 0;

	/* tone down if really high */
	if (uvm_km_pages.lowat > 512)
		uvm_km_pages.lowat = 512;
}

void
uvm_km_page_lateinit(void)
{
	kthread_create_deferred(uvm_km_createthread, NULL);
}

void
uvm_km_createthread(void *arg)
{
	kthread_create(uvm_km_thread, NULL, &uvm_km_pages.km_proc, "kmthread");
}

/*
 * Endless loop.  We grab pages in increments of 16 pages, then
 * quickly swap them into the list.
 */
void
uvm_km_thread(void *arg)
{
	vaddr_t pg[16];
	int i;
	int allocmore = 0;
	int flags;
	struct uvm_km_free_page *fp = NULL;

	KERNEL_UNLOCK();

	for (;;) {
		mtx_enter(&uvm_km_pages.mtx);
		if (uvm_km_pages.free >= uvm_km_pages.lowat &&
		    uvm_km_pages.freelist == NULL) {
			msleep_nsec(&uvm_km_pages.km_proc, &uvm_km_pages.mtx,
			    PVM, "kmalloc", INFSLP);
		}
		allocmore = uvm_km_pages.free < uvm_km_pages.lowat;
		fp = uvm_km_pages.freelist;
		uvm_km_pages.freelist = NULL;
		uvm_km_pages.freelistlen = 0;
		mtx_leave(&uvm_km_pages.mtx);

		if (allocmore) {
			/*
			 * If there was nothing on the freelist, then we
			 * must obtain at least one page to make progress.
			 * So, only use UVM_KMF_TRYLOCK for the first page
			 * if fp != NULL
			 */
			flags = UVM_MAPFLAG(PROT_READ | PROT_WRITE,
			    PROT_READ | PROT_WRITE, MAP_INHERIT_NONE,
			    MADV_RANDOM, fp != NULL ? UVM_KMF_TRYLOCK : 0);
			memset(pg, 0, sizeof(pg));
			for (i = 0; i < nitems(pg); i++) {
				pg[i] = vm_map_min(kernel_map);
				if (uvm_map(kernel_map, &pg[i], PAGE_SIZE,
				    NULL, UVM_UNKNOWN_OFFSET, 0, flags) != 0) {
					pg[i] = 0;
					break;
				}

				/* made progress, so don't sleep for more */
				flags = UVM_MAPFLAG(PROT_READ | PROT_WRITE,
				    PROT_READ | PROT_WRITE, MAP_INHERIT_NONE,
				    MADV_RANDOM, UVM_KMF_TRYLOCK);
			}

			mtx_enter(&uvm_km_pages.mtx);
			for (i = 0; i < nitems(pg); i++) {
				if (uvm_km_pages.free ==
				    nitems(uvm_km_pages.page))
					break;
				else if (pg[i] != 0)
					uvm_km_pages.page[uvm_km_pages.free++]
					    = pg[i];
			}
			wakeup(&uvm_km_pages.free);
			mtx_leave(&uvm_km_pages.mtx);

			/* Cleanup left-over pages (if any). */
			for (; i < nitems(pg); i++) {
				if (pg[i] != 0) {
					uvm_unmap(kernel_map,
					    pg[i], pg[i] + PAGE_SIZE);
				}
			}
		}
		while (fp) {
			fp = uvm_km_doputpage(fp);
		}
	}
}

struct uvm_km_free_page *
uvm_km_doputpage(struct uvm_km_free_page *fp)
{
	vaddr_t va = (vaddr_t)fp;
	struct vm_page *pg;
	int	freeva = 1;
	struct uvm_km_free_page *nextfp = fp->next;

	pg = uvm_atopg(va);

	pmap_kremove(va, PAGE_SIZE);
	pmap_update(kernel_map->pmap);

	mtx_enter(&uvm_km_pages.mtx);
	if (uvm_km_pages.free < uvm_km_pages.hiwat) {
		uvm_km_pages.page[uvm_km_pages.free++] = va;
		freeva = 0;
	}
	mtx_leave(&uvm_km_pages.mtx);

	if (freeva)
		uvm_unmap(kernel_map, va, va + PAGE_SIZE);

	uvm_pagefree(pg);
	return (nextfp);
}
#endif	/* !__HAVE_PMAP_DIRECT */

void *
km_alloc(size_t sz, const struct kmem_va_mode *kv,
    const struct kmem_pa_mode *kp, const struct kmem_dyn_mode *kd)
{
	struct vm_map *map;
	struct vm_page *pg;
	struct pglist pgl;
	int mapflags = 0;
	vm_prot_t prot;
	paddr_t pla_align;
	int pla_flags;
	int pla_maxseg;
	vaddr_t va, sva = 0;

	KASSERT(sz == round_page(sz));

	TAILQ_INIT(&pgl);

	if (kp->kp_nomem || kp->kp_pageable)
		goto alloc_va;

	pla_flags = kd->kd_waitok ? UVM_PLA_WAITOK : UVM_PLA_NOWAIT;
	pla_flags |= UVM_PLA_TRYCONTIG;
	if (kp->kp_zero)
		pla_flags |= UVM_PLA_ZERO;

	pla_align = kp->kp_align;
#ifdef __HAVE_PMAP_DIRECT
	if (pla_align < kv->kv_align)
		pla_align = kv->kv_align;
#endif
	pla_maxseg = kp->kp_maxseg;
	if (pla_maxseg == 0)
		pla_maxseg = sz / PAGE_SIZE;

	if (uvm_pglistalloc(sz, kp->kp_constraint->ucr_low,
	    kp->kp_constraint->ucr_high, pla_align, kp->kp_boundary,
	    &pgl, pla_maxseg, pla_flags)) {	
		return (NULL);
	}

#ifdef __HAVE_PMAP_DIRECT
	/*
	 * Only use direct mappings for single page or single segment
	 * allocations.
	 */
	if (kv->kv_singlepage || kp->kp_maxseg == 1) {
		TAILQ_FOREACH(pg, &pgl, pageq) {
			va = pmap_map_direct(pg);
			if (pg == TAILQ_FIRST(&pgl))
				sva = va;
		}
		return ((void *)sva);
	}
#endif
alloc_va:
	prot = PROT_READ | PROT_WRITE;

	if (kp->kp_pageable) {
		KASSERT(kp->kp_object);
		KASSERT(!kv->kv_singlepage);
	} else {
		KASSERT(kp->kp_object == NULL);
	}

	if (kv->kv_singlepage) {
		KASSERT(sz == PAGE_SIZE);
#ifdef __HAVE_PMAP_DIRECT
		panic("km_alloc: DIRECT single page");
#else
		mtx_enter(&uvm_km_pages.mtx);
		while (uvm_km_pages.free == 0) {
			if (kd->kd_waitok == 0) {
				mtx_leave(&uvm_km_pages.mtx);
				uvm_pglistfree(&pgl);
				return NULL;
			}
			msleep_nsec(&uvm_km_pages.free, &uvm_km_pages.mtx,
			    PVM, "getpage", INFSLP);
		}
		va = uvm_km_pages.page[--uvm_km_pages.free];
		if (uvm_km_pages.free < uvm_km_pages.lowat &&
		    curproc != uvm_km_pages.km_proc) {
			if (kd->kd_slowdown)
				*kd->kd_slowdown = 1;
			wakeup(&uvm_km_pages.km_proc);
		}
		mtx_leave(&uvm_km_pages.mtx);
#endif
	} else {
		struct uvm_object *uobj = NULL;

		if (kd->kd_trylock)
			mapflags |= UVM_KMF_TRYLOCK;

		if (kp->kp_object)
			uobj = *kp->kp_object;
try_map:
		map = *kv->kv_map;
		va = vm_map_min(map);
		if (uvm_map(map, &va, sz, uobj, kd->kd_prefer,
		    kv->kv_align, UVM_MAPFLAG(prot, prot, MAP_INHERIT_NONE,
		    MADV_RANDOM, mapflags))) {
			if (kv->kv_wait && kd->kd_waitok) {
				tsleep_nsec(map, PVM, "km_allocva", INFSLP);
				goto try_map;
			}
			uvm_pglistfree(&pgl);
			return (NULL);
		}
	}
	sva = va;
	TAILQ_FOREACH(pg, &pgl, pageq) {
		if (kp->kp_pageable)
			pmap_enter(pmap_kernel(), va, VM_PAGE_TO_PHYS(pg),
			    prot, prot | PMAP_WIRED);
		else
			pmap_kenter_pa(va, VM_PAGE_TO_PHYS(pg), prot);
		va += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
	return ((void *)sva);
}

void
km_free(void *v, size_t sz, const struct kmem_va_mode *kv,
    const struct kmem_pa_mode *kp)
{
	vaddr_t sva, eva, va;
	struct vm_page *pg;
	struct pglist pgl;

	sva = (vaddr_t)v;
	eva = sva + sz;

	if (kp->kp_nomem)
		goto free_va;

#ifdef __HAVE_PMAP_DIRECT
	if (kv->kv_singlepage || kp->kp_maxseg == 1) {
		TAILQ_INIT(&pgl);
		for (va = sva; va < eva; va += PAGE_SIZE) {
			pg = pmap_unmap_direct(va);
			TAILQ_INSERT_TAIL(&pgl, pg, pageq);
		}
		uvm_pglistfree(&pgl);
		return;
	}
#else
	if (kv->kv_singlepage) {
		struct uvm_km_free_page *fp = v;

		mtx_enter(&uvm_km_pages.mtx);
		fp->next = uvm_km_pages.freelist;
		uvm_km_pages.freelist = fp;
		if (uvm_km_pages.freelistlen++ > 16)
			wakeup(&uvm_km_pages.km_proc);
		mtx_leave(&uvm_km_pages.mtx);
		return;
	}
#endif

	if (kp->kp_pageable) {
		pmap_remove(pmap_kernel(), sva, eva);
		pmap_update(pmap_kernel());
	} else {
		TAILQ_INIT(&pgl);
		for (va = sva; va < eva; va += PAGE_SIZE) {
			paddr_t pa;

			if (!pmap_extract(pmap_kernel(), va, &pa))
				continue;

			pg = PHYS_TO_VM_PAGE(pa);
			if (pg == NULL) {
				panic("km_free: unmanaged page 0x%lx", pa);
			}
			TAILQ_INSERT_TAIL(&pgl, pg, pageq);
		}
		pmap_kremove(sva, sz);
		pmap_update(pmap_kernel());
		uvm_pglistfree(&pgl);
	}
free_va:
	uvm_unmap(*kv->kv_map, sva, eva);
	if (kv->kv_wait)
		wakeup(*kv->kv_map);
}

const struct kmem_va_mode kv_any = {
	.kv_map = &kernel_map,
};

const struct kmem_va_mode kv_intrsafe = {
	.kv_map = &kmem_map,
};

const struct kmem_va_mode kv_page = {
	.kv_singlepage = 1
};

const struct kmem_pa_mode kp_dirty = {
	.kp_constraint = &no_constraint
};

const struct kmem_pa_mode kp_dma = {
	.kp_constraint = &dma_constraint
};

const struct kmem_pa_mode kp_dma_contig = {
	.kp_constraint = &dma_constraint,
	.kp_maxseg = 1
};

const struct kmem_pa_mode kp_dma_zero = {
	.kp_constraint = &dma_constraint,
	.kp_zero = 1
};

const struct kmem_pa_mode kp_zero = {
	.kp_constraint = &no_constraint,
	.kp_zero = 1
};

const struct kmem_pa_mode kp_pageable = {
	.kp_object = &uvm.kernel_object,
	.kp_pageable = 1
/* XXX - kp_nomem, maybe, but we'll need to fix km_free. */
};

const struct kmem_pa_mode kp_none = {
	.kp_nomem = 1
};

const struct kmem_dyn_mode kd_waitok = {
	.kd_waitok = 1,
	.kd_prefer = UVM_UNKNOWN_OFFSET
};

const struct kmem_dyn_mode kd_nowait = {
	.kd_prefer = UVM_UNKNOWN_OFFSET
};

const struct kmem_dyn_mode kd_trylock = {
	.kd_trylock = 1,
	.kd_prefer = UVM_UNKNOWN_OFFSET
};
