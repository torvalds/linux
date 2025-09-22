/*	$OpenBSD: uvm_page.h,v 1.73 2025/03/10 18:54:38 mpi Exp $	*/
/*	$NetBSD: uvm_page.h,v 1.19 2000/12/28 08:24:55 chs Exp $	*/

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
 *	@(#)vm_page.h   7.3 (Berkeley) 4/21/91
 * from: Id: uvm_page.h,v 1.1.2.6 1998/02/04 02:31:42 chuck Exp
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

#ifndef _UVM_UVM_PAGE_H_
#define _UVM_UVM_PAGE_H_

/*
 * uvm_page.h
 */

/*
 *	Resident memory system definitions.
 */

/*
 *	Management of resident (logical) pages.
 *
 *	A small structure is kept for each resident
 *	page, indexed by page number.  Each structure
 *	contains a list used for manipulating pages, and
 *	a tree structure for in object/offset lookups
 *
 *	In addition, the structure contains the object
 *	and offset to which this page belongs (for pageout),
 *	and sundry status bits.
 *
 * Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	a	atomic operations
 *	Q	uvm.pageqlock
 *	F	uvm.fpageqlock
 *	o	owner lock (uobject->vmobjlock or uanon->an_lock)
 */

TAILQ_HEAD(pglist, vm_page);

struct vm_page {
	TAILQ_ENTRY(vm_page)	pageq;		/* [Q] LRU or free page queue */
	RBT_ENTRY(vm_page)	objt;		/* [o] object tree */

	struct vm_anon		*uanon;		/* [o] anon */
	struct uvm_object	*uobject;	/* [o] object */
	voff_t			offset;		/* [o] offset into object */

	uint32_t		pg_flags;	/* [a] object flags */

	uint32_t		pg_version;	/* version count */
	uint32_t		wire_count;	/* [o] wired down map refs */

	paddr_t			phys_addr;	/* [I] physical address */
	psize_t			fpgsz;		/* [F] free page range size */

	struct vm_page_md	mdpage;		/* pmap-specific data */

#if defined(UVM_PAGE_TRKOWN)
	/* debugging fields to track page ownership */
	pid_t			owner;		/* thread that set PG_BUSY */
	char			*owner_tag;	/* why it was set busy */
#endif
};

/*
 * These are the flags defined for vm_page.
 *
 * Note: PG_FILLED and PG_DIRTY are added for the filesystems.
 */

/*
 * locking rules:
 *   PQ_ ==> lock by page queue lock 
 *   PQ_FREE is locked by free queue lock and is mutex with all other PQs
 *   pg_flags may only be changed using the atomic operations.
 *
 * PG_ZERO is used to indicate that a page has been pre-zero'd.  This flag
 * is only set when the page is on no queues, and is cleared when the page
 * is placed on the free list.
 */

#define	PG_BUSY		0x00000001	/* page is locked */
#define	PG_WANTED	0x00000002	/* someone is waiting for page */
#define	PG_TABLED	0x00000004	/* page is in VP table  */
#define	PG_CLEAN	0x00000008	/* page has not been modified */
#define PG_CLEANCHK	0x00000010	/* clean bit has been checked */
#define PG_RELEASED	0x00000020	/* page released while paging */
#define	PG_FAKE		0x00000040	/* page is not yet initialized */
#define PG_RDONLY	0x00000080	/* page must be mapped read-only */
#define PG_ZERO		0x00000100	/* page is pre-zero'd */
#define PG_DEV		0x00000200	/* page is in device space, lay off */
#define PG_MASK		0x0000ffff

#define PQ_FREE		0x00010000	/* page is on free list */
#define PQ_INACTIVE	0x00020000	/* page is in inactive list */
#define PQ_ACTIVE	0x00040000	/* page is in active list */
#define PQ_ITER		0x00080000	/* page is an iterator marker */
#define PQ_ANON		0x00100000	/* page is part of an anon, rather
					   than an uvm_object */
#define PQ_AOBJ		0x00200000	/* page is part of an anonymous
					   uvm_object */
#define PQ_SWAPBACKED	(PQ_ANON|PQ_AOBJ)
#define	PQ_ENCRYPT	0x00400000	/* page needs {en,de}cryption */
#define PQ_MASK		0x00ff0000

#define PG_PMAP0	0x01000000	/* Used by some pmaps. */
#define PG_PMAP1	0x02000000	/* Used by some pmaps. */
#define PG_PMAP2	0x04000000	/* Used by some pmaps. */
#define PG_PMAP3	0x08000000	/* Used by some pmaps. */
#define PG_PMAP4	0x10000000	/* Used by some pmaps. */
#define PG_PMAP5	0x20000000	/* Used by some pmaps. */
#define PG_PMAPMASK	0x3f000000

/*
 * physical memory layout structure
 *
 * MD vmparam.h must #define:
 *   VM_PHYSSEG_MAX = max number of physical memory segments we support
 *		   (if this is "1" then we revert to a "contig" case)
 *   VM_PHYSSEG_STRAT: memory sort/search options (for VM_PHYSSEG_MAX > 1)
 * 	- VM_PSTRAT_RANDOM:   linear search (random order)
 *	- VM_PSTRAT_BSEARCH:  binary search (sorted by address)
 *	- VM_PSTRAT_BIGFIRST: linear search (sorted by largest segment first)
 *      - others?
 *   XXXCDC: eventually we should purge all left-over global variables...
 */
#define VM_PSTRAT_RANDOM	1
#define VM_PSTRAT_BSEARCH	2
#define VM_PSTRAT_BIGFIRST	3

/*
 * vm_physmemseg: describes one segment of physical memory
 */
struct vm_physseg {
	paddr_t	start;			/* PF# of first page in segment */
	paddr_t	end;			/* (PF# of last page in segment) + 1 */
	paddr_t	avail_start;		/* PF# of first free page in segment */
	paddr_t	avail_end;		/* (PF# of last free page in segment) +1  */
	struct	vm_page *pgs;		/* vm_page structures (from start) */
	struct	vm_page *lastpg;	/* vm_page structure for end */
};

#ifdef _KERNEL

/*
 * physical memory config is stored in vm_physmem.
 */

extern struct vm_physseg vm_physmem[VM_PHYSSEG_MAX];
extern int vm_nphysseg;

/*
 * prototypes: the following prototypes define the interface to pages
 */

void		uvm_page_init(vaddr_t *, vaddr_t *);
#if defined(UVM_PAGE_TRKOWN)
void		uvm_page_own(struct vm_page *, char *);
#endif
#if !defined(PMAP_STEAL_MEMORY)
boolean_t	uvm_page_physget(paddr_t *);
#endif

void		uvm_pageactivate(struct vm_page *);
void		uvm_pagedequeue(struct vm_page *);
vaddr_t		uvm_pageboot_alloc(vsize_t);
void		uvm_pagecopy(struct vm_page *, struct vm_page *);
void		uvm_pagedeactivate(struct vm_page *);
void		uvm_pageclean(struct vm_page *);
void		uvm_pagefree(struct vm_page *);
void		uvm_page_unbusy(struct vm_page **, int);
struct vm_page	*uvm_pagelookup(struct uvm_object *, voff_t);
void		uvm_pageunwire(struct vm_page *);
void		uvm_pagewait(struct vm_page *, struct rwlock *, const char *);
void		uvm_pagewire(struct vm_page *);
void		uvm_pagezero(struct vm_page *);
void		uvm_pagealloc_pg(struct vm_page *, struct uvm_object *,
		    voff_t, struct vm_anon *);

struct uvm_constraint_range; /* XXX move to uvm_extern.h? */
psize_t		uvm_pagecount(struct uvm_constraint_range*);

#if  VM_PHYSSEG_MAX == 1
/*
 * Inline functions for archs where function calls are expensive.
 */
/*
 * vm_physseg_find: find vm_physseg structure that belongs to a PA
 */
static inline int
vm_physseg_find(paddr_t pframe, int *offp)
{
	/* 'contig' case */
	if (pframe >= vm_physmem[0].start && pframe < vm_physmem[0].end) {
		if (offp)
			*offp = pframe - vm_physmem[0].start;
		return 0;
	}
	return -1;
}

/*
 * PHYS_TO_VM_PAGE: find vm_page for a PA.   used by MI code to get vm_pages
 * back from an I/O mapping (ugh!).   used in some MD code as well.
 */
static inline struct vm_page *
PHYS_TO_VM_PAGE(paddr_t pa)
{
	paddr_t pf = atop(pa);
	int	off;
	int	psi;

	psi = vm_physseg_find(pf, &off);

	return ((psi == -1) ? NULL : &vm_physmem[psi].pgs[off]);
}
#else
/* if VM_PHYSSEG_MAX > 1 they're not inline, they're in uvm_page.c. */
struct vm_page	*PHYS_TO_VM_PAGE(paddr_t);
int		vm_physseg_find(paddr_t, int *);
#endif

/*
 * macros
 */

#define uvm_lock_pageq()	mtx_enter(&uvm.pageqlock)
#define uvm_unlock_pageq()	mtx_leave(&uvm.pageqlock)
#define uvm_lock_fpageq()	mtx_enter(&uvm.fpageqlock)
#define uvm_unlock_fpageq()	mtx_leave(&uvm.fpageqlock)

#define	UVM_PAGEZERO_TARGET	(uvmexp.free / 8)

#define VM_PAGE_TO_PHYS(pg)	((pg)->phys_addr)

#define VM_PAGE_IS_FREE(pg)  ((pg)->pg_flags & PQ_FREE)

#define	PADDR_IS_DMA_REACHABLE(paddr)	\
	(dma_constraint.ucr_low <= paddr && dma_constraint.ucr_high > paddr)

#endif /* _KERNEL */

#endif /* _UVM_UVM_PAGE_H_ */
