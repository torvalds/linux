/*	$OpenBSD: pmap7.c,v 1.68 2024/11/07 08:12:12 miod Exp $	*/
/*	$NetBSD: pmap.c,v 1.147 2004/01/18 13:03:50 scw Exp $	*/

/*
 * Copyright 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Steve C. Woodford for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2002-2003 Wasabi Systems, Inc.
 * Copyright (c) 2001 Richard Earnshaw
 * Copyright (c) 2001-2002 Christopher Gilbert
 * All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * RiscBSD kernel project
 *
 * pmap.c
 *
 * Machine dependant vm stuff
 *
 * Created      : 20/09/94
 */

/*
 * Performance improvements, UVM changes, overhauls and part-rewrites
 * were contributed by Neil A. Carson <neil@causality.com>.
 */

/*
 * Overhauled again to speedup the pmap, use MMU Domains so that L1 tables
 * can be shared, and re-work the KVM layout, by Steve Woodford of Wasabi
 * Systems, Inc.
 *
 * There are still a few things outstanding at this time:
 *
 *   - There are some unresolved issues for MP systems:
 *
 *     o The L1 metadata needs a lock, or more specifically, some places
 *       need to acquire an exclusive lock when modifying L1 translation
 *       table entries.
 *
 *     o When one cpu modifies an L1 entry, and that L1 table is also
 *       being used by another cpu, then the latter will need to be told
 *       that a tlb invalidation may be necessary. (But only if the old
 *       domain number in the L1 entry being over-written is currently
 *       the active domain on that cpu). I guess there are lots more tlb
 *       shootdown issues too...
 *
 *     o If the vector_page is at 0x00000000 instead of 0xffff0000, then
 *       MP systems will lose big-time because of the MMU domain hack.
 *       The only way this can be solved (apart from moving the vector
 *       page to 0xffff0000) is to reserve the first 1MB of user address
 *       space for kernel use only. This would require re-linking all
 *       applications so that the text section starts above this 1MB
 *       boundary.
 *
 *     o Tracking which VM space is resident in the cache/tlb has not yet
 *       been implemented for MP systems.
 *
 *     o Finally, there is a pathological condition where two cpus running
 *       two separate processes (not procs) which happen to share an L1
 *       can get into a fight over one or more L1 entries. This will result
 *       in a significant slow-down if both processes are in tight loops.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/user.h>
#include <sys/pool.h>
 
#include <uvm/uvm.h>

#include <machine/pmap.h>
#include <machine/pcb.h>
#include <machine/param.h>
#include <arm/cpufunc.h>

/*
 * XXX We want to use proper TEX settings eventually.
 */

#define	PTE_L1_S_CACHE_MODE	(L1_S_B | L1_S_C)
#define	PTE_L1_S_CACHE_MODE_PT	(L1_S_B | L1_S_C)

/* write-allocate should be tested */
#define	PTE_L2_L_CACHE_MODE	(L2_B | L2_C)
#define	PTE_L2_S_CACHE_MODE	(L2_B | L2_C)

#define	PTE_L2_L_CACHE_MODE_PT	(L2_B | L2_C)
#define	PTE_L2_S_CACHE_MODE_PT	(L2_B | L2_C)

//#define PMAP_DEBUG
#ifdef PMAP_DEBUG

/*
 * for switching to potentially finer grained debugging
 */
#define	PDB_FOLLOW	0x0001
#define	PDB_INIT	0x0002
#define	PDB_ENTER	0x0004
#define	PDB_REMOVE	0x0008
#define	PDB_CREATE	0x0010
#define	PDB_PTPAGE	0x0020
#define	PDB_GROWKERN	0x0040
#define	PDB_BITS	0x0080
#define	PDB_COLLECT	0x0100
#define	PDB_PROTECT	0x0200
#define	PDB_MAP_L1	0x0400
#define	PDB_BOOTSTRAP	0x1000
#define	PDB_PARANOIA	0x2000
#define	PDB_WIRING	0x4000
#define	PDB_PVDUMP	0x8000
#define	PDB_KENTER	0x20000
#define	PDB_KREMOVE	0x40000

#define pmapdebug (cold ? 0 : 0xffffffff)
#define	NPDEBUG(_lev_,_stat_) \
	if (pmapdebug & (_lev_)) \
        	((_stat_))
    
#else	/* PMAP_DEBUG */
#define NPDEBUG(_lev_,_stat_) /* Nothing */
#endif	/* PMAP_DEBUG */

/*
 * pmap_kernel() points here
 */
struct pmap     kernel_pmap_store;

/*
 * Pool and cache that pmap structures are allocated from.
 * We use a cache to avoid clearing the pm_l2[] array (1KB)
 * in pmap_create().
 */
struct pool pmap_pmap_pool;

/*
 * Pool of PV structures
 */
struct pool pmap_pv_pool;
void *pmap_pv_page_alloc(struct pool *, int, int *);
void pmap_pv_page_free(struct pool *, void *);
struct pool_allocator pmap_pv_allocator = {
	pmap_pv_page_alloc, pmap_pv_page_free
};

/*
 * Pool and cache of l2_dtable structures.
 * We use a cache to avoid clearing the structures when they're
 * allocated. (196 bytes)
 */
struct pool pmap_l2dtable_pool;
vaddr_t pmap_kernel_l2dtable_kva;

/*
 * Pool and cache of L2 page descriptors.
 * We use a cache to avoid clearing the descriptor table
 * when they're allocated. (1KB)
 */
struct pool pmap_l2ptp_pool;
vaddr_t pmap_kernel_l2ptp_kva;
paddr_t pmap_kernel_l2ptp_phys;

/*
 * pmap copy/zero page, wb page, and mem(5) hook point
 */
pt_entry_t *csrc_pte, *cdst_pte;
vaddr_t csrcp, cdstp;
char *memhook;
extern caddr_t msgbufaddr;

/*
 * Flag to indicate if pmap_init() has done its thing
 */
int pmap_initialized;

/*
 * Metadata for L1 translation tables.
 */
struct l1_ttable {
	/* Entry on the L1 Table list */
	TAILQ_ENTRY(l1_ttable) l1_link;

	/* Physical address of this L1 page table */
	paddr_t l1_physaddr;

	/* KVA of this L1 page table */
	pd_entry_t *l1_kva;
};

/*
 * Convert a virtual address into its L1 table index. That is, the
 * index used to locate the L2 descriptor table pointer in an L1 table.
 * This is basically used to index l1->l1_kva[].
 *
 * Each L2 descriptor table represents 1MB of VA space.
 */
#define	L1_IDX(va)		(((vaddr_t)(va)) >> L1_S_SHIFT)

/*
 * Set if the PXN bit is supported.
 */
pd_entry_t l1_c_pxn;

/*
 * A list of all L1 tables
 */
TAILQ_HEAD(, l1_ttable) l1_list;

/*
 * The l2_dtable tracks L2_BUCKET_SIZE worth of L1 slots.
 *
 * This is normally 16MB worth L2 page descriptors for any given pmap.
 * Reference counts are maintained for L2 descriptors so they can be
 * freed when empty.
 */
struct l2_dtable {
	/* The number of L2 page descriptors allocated to this l2_dtable */
	u_int l2_occupancy;

	/* List of L2 page descriptors */
	struct l2_bucket {
		pt_entry_t *l2b_kva;	/* KVA of L2 Descriptor Table */
		paddr_t l2b_phys;	/* Physical address of same */
		u_short l2b_l1idx;	/* This L2 table's L1 index */
		u_short l2b_occupancy;	/* How many active descriptors */
	} l2_bucket[L2_BUCKET_SIZE];
};

/*
 * Given an L1 table index, calculate the corresponding l2_dtable index
 * and bucket index within the l2_dtable.
 */
#define	L2_IDX(l1idx)		(((l1idx) >> L2_BUCKET_LOG2) & \
				 (L2_SIZE - 1))
#define	L2_BUCKET(l1idx)	((l1idx) & (L2_BUCKET_SIZE - 1))

/*
 * Given a virtual address, this macro returns the
 * virtual address required to drop into the next L2 bucket.
 */
#define	L2_NEXT_BUCKET(va)	(((va) & L1_S_FRAME) + L1_S_SIZE)

/*
 * L2 allocation.
 */
#define	pmap_alloc_l2_dtable()		\
	    pool_get(&pmap_l2dtable_pool, PR_NOWAIT|PR_ZERO)
#define	pmap_free_l2_dtable(l2)		\
	    pool_put(&pmap_l2dtable_pool, (l2))

/*
 * We try to map the page tables write-through, if possible.  However, not
 * all CPUs have a write-through cache mode, so on those we have to sync
 * the cache when we frob page tables.
 *
 * We try to evaluate this at compile time, if possible.  However, it's
 * not always possible to do that, hence this run-time var.
 */
int	pmap_needs_pte_sync;

/*
 * Real definition of pv_entry.
 */
struct pv_entry {
	struct pv_entry *pv_next;       /* next pv_entry */
	pmap_t		pv_pmap;        /* pmap where mapping lies */
	vaddr_t		pv_va;          /* virtual address for mapping */
	u_int		pv_flags;       /* flags */
};

/*
 * Macro to determine if a mapping might be resident in the
 * instruction cache and/or TLB
 */
#define	PV_BEEN_EXECD(f)  (((f) & PVF_EXEC) != 0)

/*
 * Local prototypes
 */
void		pmap_alloc_specials(vaddr_t *, int, vaddr_t *,
		    pt_entry_t **);
static int	pmap_is_current(pmap_t);
void		pmap_enter_pv(struct vm_page *, struct pv_entry *,
		    pmap_t, vaddr_t, u_int);
static struct pv_entry *pmap_find_pv(struct vm_page *, pmap_t, vaddr_t);
struct pv_entry *pmap_remove_pv(struct vm_page *, pmap_t, vaddr_t);
u_int		pmap_modify_pv(struct vm_page *, pmap_t, vaddr_t,
		    u_int, u_int);

void		pmap_alloc_l1(pmap_t);
void		pmap_free_l1(pmap_t);

struct l2_bucket *pmap_get_l2_bucket(pmap_t, vaddr_t);
struct l2_bucket *pmap_alloc_l2_bucket(pmap_t, vaddr_t);
void		pmap_free_l2_bucket(pmap_t, struct l2_bucket *, u_int);

void		pmap_clearbit(struct vm_page *, u_int);
void		pmap_clean_page(struct vm_page *);
void		pmap_page_remove(struct vm_page *);

void		pmap_init_l1(struct l1_ttable *, pd_entry_t *);
vaddr_t		kernel_pt_lookup(paddr_t);


/*
 * External function prototypes
 */
extern void bzero_page(vaddr_t);
extern void bcopy_page(vaddr_t, vaddr_t);

/*
 * Misc variables
 */
vaddr_t virtual_avail;
vaddr_t virtual_end;
vaddr_t pmap_curmaxkvaddr;

extern pv_addr_t systempage;

static __inline int
pmap_is_current(pmap_t pm)
{
	if (pm == pmap_kernel() ||
	    (curproc && curproc->p_vmspace->vm_map.pmap == pm))
		return 1;

	return 0;
}

/*
 * A bunch of routines to conditionally flush the caches/TLB depending
 * on whether the specified pmap actually needs to be flushed at any
 * given time.
 */
static __inline void
pmap_tlb_flushID_SE(pmap_t pm, vaddr_t va)
{
	if (pmap_is_current(pm))
		cpu_tlb_flushID_SE(va);
}

static __inline void
pmap_tlb_flushID(pmap_t pm)
{
	if (pmap_is_current(pm))
		cpu_tlb_flushID();
}

/*
 * Returns a pointer to the L2 bucket associated with the specified pmap
 * and VA, or NULL if no L2 bucket exists for the address.
 */
struct l2_bucket *
pmap_get_l2_bucket(pmap_t pm, vaddr_t va)
{
	struct l2_dtable *l2;
	struct l2_bucket *l2b;
	u_short l1idx;

	l1idx = L1_IDX(va);

	if ((l2 = pm->pm_l2[L2_IDX(l1idx)]) == NULL ||
	    (l2b = &l2->l2_bucket[L2_BUCKET(l1idx)])->l2b_kva == NULL)
		return (NULL);

	return (l2b);
}

/*
 * main pv_entry manipulation functions:
 *   pmap_enter_pv: enter a mapping onto a vm_page list
 *   pmap_remove_pv: remove a mapping from a vm_page list
 *
 * NOTE: pmap_enter_pv expects to lock the pvh itself
 *       pmap_remove_pv expects te caller to lock the pvh before calling
 */

/*
 * pmap_enter_pv: enter a mapping onto a vm_page lst
 *
 * => caller should have pmap locked
 * => we will gain the lock on the vm_page and allocate the new pv_entry
 * => caller should adjust ptp's wire_count before calling
 * => caller should not adjust pmap's wire_count
 */
void
pmap_enter_pv(struct vm_page *pg, struct pv_entry *pve, pmap_t pm,
    vaddr_t va, u_int flags)
{

	NPDEBUG(PDB_PVDUMP,
	    printf("pmap_enter_pv: pm %p, pg %p, flags 0x%x\n", pm, pg, flags));

	pve->pv_pmap = pm;
	pve->pv_va = va;
	pve->pv_flags = flags;

	pve->pv_next = pg->mdpage.pvh_list;	/* add to ... */
	pg->mdpage.pvh_list = pve;		/* ... locked list */
	pg->mdpage.pvh_attrs |= flags & (PVF_REF | PVF_MOD);

	if (pve->pv_flags & PVF_WIRED)
		++pm->pm_stats.wired_count;
}

/*
 *
 * pmap_find_pv: Find a pv entry
 *
 * => caller should hold lock on vm_page
 */
static __inline struct pv_entry *
pmap_find_pv(struct vm_page *pg, pmap_t pm, vaddr_t va)
{
	struct pv_entry *pv;

	for (pv = pg->mdpage.pvh_list; pv; pv = pv->pv_next) {
		if (pm == pv->pv_pmap && va == pv->pv_va)
			break;
	}

	return (pv);
}

/*
 * pmap_remove_pv: try to remove a mapping from a pv_list
 *
 * => pmap should be locked
 * => caller should hold lock on vm_page [so that attrs can be adjusted]
 * => caller should adjust ptp's wire_count and free PTP if needed
 * => caller should NOT adjust pmap's wire_count
 * => we return the removed pve
 */
struct pv_entry *
pmap_remove_pv(struct vm_page *pg, pmap_t pm, vaddr_t va)
{
	struct pv_entry *pve, **prevptr;

	NPDEBUG(PDB_PVDUMP,
	    printf("pmap_remove_pv: pm %p, pg %p, va 0x%08lx\n", pm, pg, va));

	prevptr = &pg->mdpage.pvh_list;		/* previous pv_entry pointer */
	pve = *prevptr;

	while (pve) {
		if (pve->pv_pmap == pm && pve->pv_va == va) {	/* match? */
			NPDEBUG(PDB_PVDUMP,
			    printf("pmap_remove_pv: pm %p, pg %p, flags 0x%x\n", pm, pg, pve->pv_flags));
			*prevptr = pve->pv_next;		/* remove it! */
			if (pve->pv_flags & PVF_WIRED)
			    --pm->pm_stats.wired_count;
			break;
		}
		prevptr = &pve->pv_next;		/* previous pointer */
		pve = pve->pv_next;			/* advance */
	}

	return(pve);				/* return removed pve */
}

/*
 *
 * pmap_modify_pv: Update pv flags
 *
 * => caller should hold lock on vm_page [so that attrs can be adjusted]
 * => caller should NOT adjust pmap's wire_count
 * => we return the old flags
 * 
 * Modify a physical-virtual mapping in the pv table
 */
u_int
pmap_modify_pv(struct vm_page *pg, pmap_t pm, vaddr_t va,
    u_int clr_mask, u_int set_mask)
{
	struct pv_entry *npv;
	u_int flags, oflags;

	if ((npv = pmap_find_pv(pg, pm, va)) == NULL)
		return (0);

	NPDEBUG(PDB_PVDUMP,
	    printf("pmap_modify_pv: pm %p, pg %p, clr 0x%x, set 0x%x, flags 0x%x\n", pm, pg, clr_mask, set_mask, npv->pv_flags));

	/*
	 * There is at least one VA mapping this page.
	 */

	if (clr_mask & (PVF_REF | PVF_MOD))
		pg->mdpage.pvh_attrs |= set_mask & (PVF_REF | PVF_MOD);

	oflags = npv->pv_flags;
	npv->pv_flags = flags = (oflags & ~clr_mask) | set_mask;

	if ((flags ^ oflags) & PVF_WIRED) {
		if (flags & PVF_WIRED)
			++pm->pm_stats.wired_count;
		else
			--pm->pm_stats.wired_count;
	}

	return (oflags);
}

uint nl1;
/*
 * Allocate an L1 translation table for the specified pmap.
 * This is called at pmap creation time.
 */
void
pmap_alloc_l1(pmap_t pm)
{
	struct l1_ttable *l1;
	struct pglist plist;
	struct vm_page *m;
	pd_entry_t *pl1pt;
	vaddr_t va, eva;
	int error;

#ifdef PMAP_DEBUG
printf("%s: %d\n", __func__, ++nl1);
#endif
	/* XXX use a pool? or move to inside struct pmap? */
	l1 = malloc(sizeof(*l1), M_VMPMAP, M_WAITOK);

	/* Allocate a L1 page table */
	for (;;) {
		va = (vaddr_t)km_alloc(L1_TABLE_SIZE, &kv_any, &kp_none,
		    &kd_nowait);
		if (va != 0)
			break;
		uvm_wait("alloc_l1_va");
	}

	for (;;) {
		TAILQ_INIT(&plist);
		error = uvm_pglistalloc(L1_TABLE_SIZE, 0, (paddr_t)-1,
		    L1_TABLE_SIZE, 0, &plist, 1, UVM_PLA_WAITOK);
		if (error == 0)
			break;
		uvm_wait("alloc_l1_pg");
	}

	pl1pt = (pd_entry_t *)va;
	m = TAILQ_FIRST(&plist);
	for (eva = va + L1_TABLE_SIZE; va < eva; va += PAGE_SIZE) {
		paddr_t pa = VM_PAGE_TO_PHYS(m);

		pmap_kenter_pa(va, pa, PROT_READ | PROT_WRITE);
		m = TAILQ_NEXT(m, pageq);
	}

	pmap_init_l1(l1, pl1pt);

	pm->pm_l1 = l1;
}

/*
 * Free an L1 translation table.
 * This is called at pmap destruction time.
 */
void
pmap_free_l1(pmap_t pm)
{
	struct l1_ttable *l1 = pm->pm_l1;
	struct pglist mlist;
	struct vm_page *pg;
	struct l2_bucket *l2b;
	pt_entry_t *ptep;
	vaddr_t va;
	uint npg;

	pm->pm_l1 = NULL;
	TAILQ_REMOVE(&l1_list, l1, l1_link);

	/* free backing pages */
	TAILQ_INIT(&mlist);
	va = (vaddr_t)l1->l1_kva;
	for (npg = atop(L1_TABLE_SIZE); npg != 0; npg--) {
		l2b = pmap_get_l2_bucket(pmap_kernel(), va);
		ptep = &l2b->l2b_kva[l2pte_index(va)];
		pg = PHYS_TO_VM_PAGE(l2pte_pa(*ptep));
		TAILQ_INSERT_TAIL(&mlist, pg, pageq);
		va += PAGE_SIZE;
	}
	pmap_kremove((vaddr_t)l1->l1_kva, L1_TABLE_SIZE);
	uvm_pglistfree(&mlist);

	/* free backing va */
	km_free(l1->l1_kva, L1_TABLE_SIZE, &kv_any, &kp_none);

	free(l1, M_VMPMAP, 0);
}

/*
 * void pmap_free_l2_ptp(pt_entry_t *)
 *
 * Free an L2 descriptor table.
 */
static __inline void
pmap_free_l2_ptp(pt_entry_t *l2)
{
	pool_put(&pmap_l2ptp_pool, (void *)l2);
}

/*
 * Returns a pointer to the L2 bucket associated with the specified pmap
 * and VA.
 *
 * If no L2 bucket exists, perform the necessary allocations to put an L2
 * bucket/page table in place.
 *
 * Note that if a new L2 bucket/page was allocated, the caller *must*
 * increment the bucket occupancy counter appropriately *before* 
 * releasing the pmap's lock to ensure no other thread or cpu deallocates
 * the bucket/page in the meantime.
 */
struct l2_bucket *
pmap_alloc_l2_bucket(pmap_t pm, vaddr_t va)
{
	struct l2_dtable *l2;
	struct l2_bucket *l2b;
	u_short l1idx;

	l1idx = L1_IDX(va);

	if ((l2 = pm->pm_l2[L2_IDX(l1idx)]) == NULL) {
		/*
		 * No mapping at this address, as there is
		 * no entry in the L1 table.
		 * Need to allocate a new l2_dtable.
		 */
		if ((l2 = pmap_alloc_l2_dtable()) == NULL)
			return (NULL);

		/*
		 * Link it into the parent pmap
		 */
		pm->pm_l2[L2_IDX(l1idx)] = l2;
	}

	l2b = &l2->l2_bucket[L2_BUCKET(l1idx)];

	/*
	 * Fetch pointer to the L2 page table associated with the address.
	 */
	if (l2b->l2b_kva == NULL) {
		pt_entry_t *ptep;

		/*
		 * No L2 page table has been allocated. Chances are, this
		 * is because we just allocated the l2_dtable, above.
		 */
		ptep = pool_get(&pmap_l2ptp_pool, PR_NOWAIT|PR_ZERO);
		if (ptep == NULL) {
			/*
			 * Oops, no more L2 page tables available at this
			 * time. We may need to deallocate the l2_dtable
			 * if we allocated a new one above.
			 */
			if (l2->l2_occupancy == 0) {
				pm->pm_l2[L2_IDX(l1idx)] = NULL;
				pmap_free_l2_dtable(l2);
			}
			return (NULL);
		}
		PTE_SYNC_RANGE(ptep, L2_TABLE_SIZE_REAL / sizeof(pt_entry_t));
		pmap_extract(pmap_kernel(), (vaddr_t)ptep, &l2b->l2b_phys);

		l2->l2_occupancy++;
		l2b->l2b_kva = ptep;
		l2b->l2b_l1idx = l1idx;
	}

	return (l2b);
}

/*
 * One or more mappings in the specified L2 descriptor table have just been
 * invalidated.
 *
 * Garbage collect the metadata and descriptor table itself if necessary.
 *
 * The pmap lock must be acquired when this is called (not necessary
 * for the kernel pmap).
 */
void
pmap_free_l2_bucket(pmap_t pm, struct l2_bucket *l2b, u_int count)
{
	struct l2_dtable *l2;
	pd_entry_t *pl1pd;
	pt_entry_t *ptep;
	u_short l1idx;

	KDASSERT(count <= l2b->l2b_occupancy);

	/*
	 * Update the bucket's reference count according to how many
	 * PTEs the caller has just invalidated.
	 */
	l2b->l2b_occupancy -= count;

	/*
	 * Note:
	 *
	 * Level 2 page tables allocated to the kernel pmap are never freed
	 * as that would require checking all Level 1 page tables and
	 * removing any references to the Level 2 page table. See also the
	 * comment elsewhere about never freeing bootstrap L2 descriptors.
	 *
	 * We make do with just invalidating the mapping in the L2 table.
	 *
	 * This isn't really a big deal in practice and, in fact, leads
	 * to a performance win over time as we don't need to continually
	 * alloc/free.
	 */
	if (l2b->l2b_occupancy > 0 || pm == pmap_kernel())
		return;

	/*
	 * There are no more valid mappings in this level 2 page table.
	 * Go ahead and NULL-out the pointer in the bucket, then
	 * free the page table.
	 */
	l1idx = l2b->l2b_l1idx;
	ptep = l2b->l2b_kva;
	l2b->l2b_kva = NULL;

	pl1pd = &pm->pm_l1->l1_kva[l1idx];

	/*
	 * Invalidate the L1 slot.
	 */
	*pl1pd = L1_TYPE_INV;
	PTE_SYNC(pl1pd);
	pmap_tlb_flushID_SE(pm, l1idx << L1_S_SHIFT);

	/*
	 * Release the L2 descriptor table back to the pool cache.
	 */
	pmap_free_l2_ptp(ptep);

	/*
	 * Update the reference count in the associated l2_dtable
	 */
	l2 = pm->pm_l2[L2_IDX(l1idx)];
	if (--l2->l2_occupancy > 0)
		return;

	/*
	 * There are no more valid mappings in any of the Level 1
	 * slots managed by this l2_dtable. Go ahead and NULL-out
	 * the pointer in the parent pmap and free the l2_dtable.
	 */
	pm->pm_l2[L2_IDX(l1idx)] = NULL;
	pmap_free_l2_dtable(l2);
}

/*
 * Modify pte bits for all ptes corresponding to the given physical address.
 * We use `maskbits' rather than `clearbits' because we're always passing
 * constants and the latter would require an extra inversion at run-time.
 */
void
pmap_clearbit(struct vm_page *pg, u_int maskbits)
{
	struct l2_bucket *l2b;
	struct pv_entry *pv;
	pt_entry_t *ptep, npte, opte;
	pmap_t pm;
	vaddr_t va;
	u_int oflags;

	NPDEBUG(PDB_BITS,
	    printf("pmap_clearbit: pg %p (0x%08lx) mask 0x%x\n",
	    pg, pg->phys_addr, maskbits));

	/*
	 * Clear saved attributes (modify, reference)
	 */
	pg->mdpage.pvh_attrs &= ~(maskbits & (PVF_MOD | PVF_REF));

	if (pg->mdpage.pvh_list == NULL)
		return;

	/*
	 * Loop over all current mappings setting/clearing as appropriate
	 */
	for (pv = pg->mdpage.pvh_list; pv; pv = pv->pv_next) {
		va = pv->pv_va;
		pm = pv->pv_pmap;
		oflags = pv->pv_flags;
		pv->pv_flags &= ~maskbits;

		l2b = pmap_get_l2_bucket(pm, va);
		KDASSERT(l2b != NULL);

		ptep = &l2b->l2b_kva[l2pte_index(va)];
		npte = opte = *ptep;
		NPDEBUG(PDB_BITS,
		    printf(
		    "pmap_clearbit: pv %p, pm %p, va 0x%08lx, flag 0x%x\n",
		    pv, pv->pv_pmap, pv->pv_va, oflags));

		if (maskbits & (PVF_WRITE|PVF_MOD)) {
			/* Disable write access. */
			npte |= L2_V7_AP(0x4);
		}

		if (maskbits & PVF_REF) {
			/*
			 * Clear the Access Flag such that we will
			 * take a page fault the next time the mapping
			 * is referenced.
			 */
			npte &= ~L2_V7_AF;
		}

		if (npte != opte) {
			*ptep = npte;
			PTE_SYNC(ptep);
			/* Flush the TLB entry if a current pmap. */
			if (opte & L2_V7_AF)
				pmap_tlb_flushID_SE(pm, pv->pv_va);
		}

		NPDEBUG(PDB_BITS,
		    printf("pmap_clearbit: pm %p va 0x%lx opte 0x%08x npte 0x%08x\n",
		    pm, va, opte, npte));
	}
}

/*
 * pmap_clean_page()
 *
 * Invalidate all I$ aliases for a single page.
 */
void
pmap_clean_page(struct vm_page *pg)
{
	pmap_t pm;
	struct pv_entry *pv;

	if (curproc)
		pm = curproc->p_vmspace->vm_map.pmap;
	else
		pm = pmap_kernel();

	for (pv = pg->mdpage.pvh_list; pv; pv = pv->pv_next) {
		/* inline !pmap_is_current(pv->pv_pmap) */
		if (pv->pv_pmap != pmap_kernel() && pv->pv_pmap != pm)
			continue;

		if (PV_BEEN_EXECD(pv->pv_flags))
			cpu_icache_sync_range(pv->pv_va, PAGE_SIZE);
	}
}

/*
 * Routine:	pmap_page_remove
 * Function:
 *		Removes this physical page from
 *		all physical maps in which it resides.
 *		Reflects back modify bits to the pager.
 */
void
pmap_page_remove(struct vm_page *pg)
{
	struct l2_bucket *l2b;
	struct pv_entry *pv, *npv;
	pmap_t pm, curpm;
	pt_entry_t *ptep, opte;
	int flush;

	NPDEBUG(PDB_FOLLOW,
	    printf("pmap_page_remove: pg %p (0x%08lx)\n", pg, pg->phys_addr));

	pv = pg->mdpage.pvh_list;
	if (pv == NULL)
		return;

	flush = 0;
	if (curproc)
		curpm = curproc->p_vmspace->vm_map.pmap;
	else
		curpm = pmap_kernel();

	while (pv) {
		pm = pv->pv_pmap;

		l2b = pmap_get_l2_bucket(pm, pv->pv_va);
		KDASSERT(l2b != NULL);

		ptep = &l2b->l2b_kva[l2pte_index(pv->pv_va)];
		opte = *ptep;
		if (opte != L2_TYPE_INV) {
			/* inline pmap_is_current(pm) */
			if ((opte & L2_V7_AF) &&
			    (pm == curpm || pm == pmap_kernel())) {
				if (PV_BEEN_EXECD(pv->pv_flags))
					cpu_icache_sync_range(pv->pv_va, PAGE_SIZE);
				flush = 1;
			}

			/*
			 * Update statistics
			 */
			--pm->pm_stats.resident_count;

			/* Wired bit */
			if (pv->pv_flags & PVF_WIRED)
				--pm->pm_stats.wired_count;

			/*
			 * Invalidate the PTEs.
			 */
			*ptep = L2_TYPE_INV;
			PTE_SYNC(ptep);
			if (flush)
				cpu_tlb_flushID_SE(pv->pv_va);

			pmap_free_l2_bucket(pm, l2b, 1);
		}

		npv = pv->pv_next;
		pool_put(&pmap_pv_pool, pv);
		pv = npv;
	}
	pg->mdpage.pvh_list = NULL;
}

/*
 * pmap_t pmap_create(void)
 *  
 *      Create a new pmap structure from scratch.
 */
pmap_t
pmap_create(void)
{
	pmap_t pm;

	pm = pool_get(&pmap_pmap_pool, PR_WAITOK|PR_ZERO);

	pm->pm_refs = 1;
	pm->pm_stats.wired_count = 0;
	pmap_alloc_l1(pm);

	return (pm);
}

/*
 * void pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot,
 *     int flags)
 *  
 *      Insert the given physical page (p) at
 *      the specified virtual address (v) in the
 *      target physical map with the protection requested.
 *
 *      NB:  This is the only routine which MAY NOT lazy-evaluate
 *      or lose information.  That is, this routine must actually
 *      insert this page into the given map NOW.
 */
int
pmap_enter(pmap_t pm, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	struct l2_bucket *l2b;
	struct vm_page *pg, *opg;
	struct pv_entry *pve;
	pt_entry_t *ptep, npte, opte;
	u_int nflags;
	u_int oflags;
	int mapped = 1;

	NPDEBUG(PDB_ENTER, printf("pmap_enter: pm %p va 0x%lx pa 0x%lx prot %x flag %x\n", pm, va, pa, prot, flags));

	KDASSERT((flags & PMAP_WIRED) == 0 || (flags & PROT_MASK) != 0);
	KDASSERT(((va | pa) & PGOFSET) == 0);

	/*
	 * Get a pointer to the page.  Later on in this function, we
	 * test for a managed page by checking pg != NULL.
	 */
	pg = pmap_initialized ? PHYS_TO_VM_PAGE(pa) : NULL;

	nflags = 0;
	if (prot & PROT_WRITE)
		nflags |= PVF_WRITE;
	if (prot & PROT_EXEC)
		nflags |= PVF_EXEC;
	if (flags & PMAP_WIRED)
		nflags |= PVF_WIRED;

	/*
	 * Fetch the L2 bucket which maps this page, allocating one if
	 * necessary for user pmaps.
	 */
	if (pm == pmap_kernel())
		l2b = pmap_get_l2_bucket(pm, va);
	else
		l2b = pmap_alloc_l2_bucket(pm, va);
	if (l2b == NULL) {
		if (flags & PMAP_CANFAIL)
			return (ENOMEM);

		panic("pmap_enter: failed to allocate L2 bucket");
	}
	ptep = &l2b->l2b_kva[l2pte_index(va)];
	opte = *ptep;
	npte = L2_S_PROTO | pa;

	if (opte != L2_TYPE_INV) {
		/*
		 * There is already a mapping at this address.
		 * If the physical address is different, lookup the
		 * vm_page.
		 */
		if (l2pte_pa(opte) != pa)
			opg = PHYS_TO_VM_PAGE(l2pte_pa(opte));
		else
			opg = pg;
	} else
		opg = NULL;

	if (pg) {
		/*
		 * This has to be a managed mapping.
		 */
		if ((flags & PROT_MASK) ||
		    (pg->mdpage.pvh_attrs & PVF_REF)) {
			/*
			 * - The access type indicates that we don't need
			 *   to do referenced emulation.
			 * OR
			 * - The physical page has already been referenced
			 *   so no need to re-do referenced emulation here.
			 */
			nflags |= PVF_REF;
			npte |= L2_V7_AF;

			if ((flags & PROT_WRITE) ||
			    (pg->mdpage.pvh_attrs & PVF_MOD)) {
				/*
				 * This is a writable mapping, and the
				 * page's mod state indicates it has
				 * already been modified. Make it
				 * writable from the outset.
				 */
				nflags |= PVF_MOD;
			} else {
				prot &= ~PROT_WRITE;
			}
		} else {
			/*
			 * Need to do page referenced emulation.
			 */
			prot &= ~PROT_WRITE;
			mapped = 0;
		}

		npte |= PTE_L2_S_CACHE_MODE;

		if (pg == opg) {
			/*
			 * We're changing the attrs of an existing mapping.
			 */
			oflags = pmap_modify_pv(pg, pm, va,
			    PVF_WRITE | PVF_EXEC | PVF_WIRED |
			    PVF_MOD | PVF_REF, nflags);
		} else {
			/*
			 * New mapping, or changing the backing page
			 * of an existing mapping.
			 */
			if (opg) {
				/*
				 * Replacing an existing mapping with a new one.
				 * It is part of our managed memory so we
				 * must remove it from the PV list
				 */
				pve = pmap_remove_pv(opg, pm, va);
			} else
			if ((pve = pool_get(&pmap_pv_pool, PR_NOWAIT)) == NULL){
				if ((flags & PMAP_CANFAIL) == 0)
					panic("pmap_enter: no pv entries");

				if (pm != pmap_kernel())
					pmap_free_l2_bucket(pm, l2b, 0);

				NPDEBUG(PDB_ENTER,
				    printf("pmap_enter: ENOMEM\n"));
				return (ENOMEM);
			}

			pmap_enter_pv(pg, pve, pm, va, nflags);
		}
	} else {
		/*
		 * We're mapping an unmanaged page.
		 * These are always readable, and possibly writable, from
		 * the get go as we don't need to track ref/mod status.
		 */
		npte |= L2_V7_AF;

		if (opg) {
			/*
			 * Looks like there's an existing 'managed' mapping
			 * at this address.
			 */
			pve = pmap_remove_pv(opg, pm, va);
			pool_put(&pmap_pv_pool, pve);
		}
	}

	/*
	 * Make sure userland mappings get the right permissions
	 */
	npte |= L2_S_PROT(pm == pmap_kernel() ?  PTE_KERNEL : PTE_USER, prot);

	/*
	 * Keep the stats up to date
	 */
	if (opte == L2_TYPE_INV) {
		l2b->l2b_occupancy++;
		pm->pm_stats.resident_count++;
	} 

	NPDEBUG(PDB_ENTER,
	    printf("pmap_enter: opte 0x%08x npte 0x%08x\n", opte, npte));

	/*
	 * If this is just a wiring change, the two PTEs will be
	 * identical, so there's no need to update the page table.
	 */
	if (npte != opte) {
		*ptep = npte;
		/*
		 * We only need to frob the cache/tlb if this pmap
		 * is current
		 */
		PTE_SYNC(ptep);
		if (npte & L2_V7_AF) {
			/*
			 * This mapping is likely to be accessed as
			 * soon as we return to userland. Fix up the
			 * L1 entry to avoid taking another page fault.
			 */
			pd_entry_t *pl1pd, l1pd;

			pl1pd = &pm->pm_l1->l1_kva[L1_IDX(va)];
			l1pd = L1_C_PROTO | l2b->l2b_phys | l1_c_pxn;
			if (*pl1pd != l1pd) {
				*pl1pd = l1pd;
				PTE_SYNC(pl1pd);
			}
		}

		if (opte & L2_V7_AF)
			pmap_tlb_flushID_SE(pm, va);
	}

	/*
	 * Make sure executable pages do not have stale data in I$,
	 * which is VIPT.
	 */
	if (mapped && (prot & PROT_EXEC) != 0 && pmap_is_current(pm))
		cpu_icache_sync_range(va, PAGE_SIZE);

	return (0);
}

/*
 * pmap_remove()
 *
 * pmap_remove is responsible for nuking a number of mappings for a range
 * of virtual address space in the current pmap.
 */

void
pmap_remove(pmap_t pm, vaddr_t sva, vaddr_t eva)
{
	struct l2_bucket *l2b;
	vaddr_t next_bucket;
	pt_entry_t *ptep;
	u_int mappings;

	NPDEBUG(PDB_REMOVE, printf("pmap_remove: pmap=%p sva=%08lx eva=%08lx\n",
	    pm, sva, eva));

	while (sva < eva) {
		/*
		 * Do one L2 bucket's worth at a time.
		 */
		next_bucket = L2_NEXT_BUCKET(sva);
		if (next_bucket > eva)
			next_bucket = eva;

		l2b = pmap_get_l2_bucket(pm, sva);
		if (l2b == NULL) {
			sva = next_bucket;
			continue;
		}

		ptep = &l2b->l2b_kva[l2pte_index(sva)];
		mappings = 0;

		while (sva < next_bucket) {
			struct vm_page *pg;
			pt_entry_t pte;
			paddr_t pa;

			pte = *ptep;

			if (pte == L2_TYPE_INV) {
				/*
				 * Nothing here, move along
				 */
				sva += PAGE_SIZE;
				ptep++;
				continue;
			}

			pm->pm_stats.resident_count--;
			pa = l2pte_pa(pte);

			/*
			 * Update flags. In a number of circumstances,
			 * we could cluster a lot of these and do a
			 * number of sequential pages in one go.
			 */
			pg = PHYS_TO_VM_PAGE(pa);
			if (pg != NULL) {
				struct pv_entry *pve;
				pve = pmap_remove_pv(pg, pm, sva);
				if (pve != NULL)
					pool_put(&pmap_pv_pool, pve);
			}

			/*
			 * If the cache is physically indexed, we need
			 * to flush any changes to the page before it
			 * gets invalidated.	
			 */
			if (pg != NULL)
				pmap_clean_page(pg);

			*ptep = L2_TYPE_INV;
			PTE_SYNC(ptep);
			if (pte & L2_V7_AF)
				pmap_tlb_flushID_SE(pm, sva);

			sva += PAGE_SIZE;
			ptep++;
			mappings++;
		}

		/*
		 * Deal with any left overs
		 */
		if (!pmap_is_current(pm))
			cpu_idcache_wbinv_all();

		pmap_free_l2_bucket(pm, l2b, mappings);
	}
}

/*
 * pmap_kenter_pa: enter an unmanaged, wired kernel mapping
 *
 * We assume there is already sufficient KVM space available
 * to do this, as we can't allocate L2 descriptor tables/metadata
 * from here.
 */
void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	struct l2_bucket *l2b;
	pt_entry_t *ptep, opte, npte;
	pt_entry_t cache_mode = PTE_L2_S_CACHE_MODE;

	NPDEBUG(PDB_KENTER,
	    printf("pmap_kenter_pa: va 0x%08lx, pa 0x%08lx, prot 0x%x\n",
	    va, pa, prot));

	l2b = pmap_get_l2_bucket(pmap_kernel(), va);
	KDASSERT(l2b != NULL);

	ptep = &l2b->l2b_kva[l2pte_index(va)];
	opte = *ptep;

	if (opte == L2_TYPE_INV)
		l2b->l2b_occupancy++;

	if (pa & PMAP_DEVICE)
		cache_mode = L2_B | L2_V7_S_XN;
	else if (pa & PMAP_NOCACHE)
		cache_mode = L2_V7_S_TEX(1);

	npte = L2_S_PROTO | (pa & PMAP_PA_MASK) | L2_V7_AF |
	    L2_S_PROT(PTE_KERNEL, prot) | cache_mode;
	*ptep = npte;
	PTE_SYNC(ptep);
	if (opte & L2_V7_AF)
		cpu_tlb_flushD_SE(va);

	if (pa & PMAP_NOCACHE) {
		cpu_dcache_wbinv_range(va, PAGE_SIZE);
		cpu_sdcache_wbinv_range(va, (pa & PMAP_PA_MASK), PAGE_SIZE);
	}
}

void
pmap_kenter_cache(vaddr_t va, paddr_t pa, vm_prot_t prot, int cacheable)
{
	if (cacheable == 0)
		pa |= PMAP_NOCACHE;
	pmap_kenter_pa(va, pa, prot);
}

void
pmap_kremove(vaddr_t va, vsize_t len)
{
	struct l2_bucket *l2b;
	pt_entry_t *ptep, *sptep, opte;
	vaddr_t next_bucket, eva;
	u_int mappings;

	NPDEBUG(PDB_KREMOVE, printf("pmap_kremove: va 0x%08lx, len 0x%08lx\n",
	    va, len));

	eva = va + len;

	while (va < eva) {
		next_bucket = L2_NEXT_BUCKET(va);
		if (next_bucket > eva)
			next_bucket = eva;

		l2b = pmap_get_l2_bucket(pmap_kernel(), va);
		KDASSERT(l2b != NULL);

		sptep = ptep = &l2b->l2b_kva[l2pte_index(va)];
		mappings = 0;

		while (va < next_bucket) {
			opte = *ptep;
			if (opte != L2_TYPE_INV) {
				*ptep = L2_TYPE_INV;
				PTE_SYNC(ptep);
				mappings++;
			}
			if (opte & L2_V7_AF)
				cpu_tlb_flushD_SE(va);
			va += PAGE_SIZE;
			ptep++;
		}
		KDASSERT(mappings <= l2b->l2b_occupancy);
		l2b->l2b_occupancy -= mappings;
	}
}

int
pmap_extract(pmap_t pm, vaddr_t va, paddr_t *pap)
{
	struct l2_dtable *l2;
	pd_entry_t *pl1pd, l1pd;
	pt_entry_t *ptep, pte;
	paddr_t pa;
	u_int l1idx;


	l1idx = L1_IDX(va);
	pl1pd = &pm->pm_l1->l1_kva[l1idx];
	l1pd = *pl1pd;

	if (l1pte_section_p(l1pd)) {
		/*
		 * These should only happen for pmap_kernel()
		 */
		KDASSERT(pm == pmap_kernel());
		pa = (l1pd & L1_S_FRAME) | (va & L1_S_OFFSET);
	} else {
		/*
		 * Note that we can't rely on the validity of the L1
		 * descriptor as an indication that a mapping exists.
		 * We have to look it up in the L2 dtable.
		 */
		l2 = pm->pm_l2[L2_IDX(l1idx)];

		if (l2 == NULL ||
		    (ptep = l2->l2_bucket[L2_BUCKET(l1idx)].l2b_kva) == NULL) {
			return 0;
		}

		ptep = &ptep[l2pte_index(va)];
		pte = *ptep;

		if (pte == L2_TYPE_INV)
			return 0;

		switch (pte & L2_TYPE_MASK) {
		case L2_TYPE_L:
			pa = (pte & L2_L_FRAME) | (va & L2_L_OFFSET);
			break;
		/*
		 * Can't check for L2_TYPE_S on V7 because of the XN
		 * bit being part of L2_TYPE_MASK for S mappings.
		 */
		default:
			pa = (pte & L2_S_FRAME) | (va & L2_S_OFFSET);
			break;
		}
	}

	if (pap != NULL)
		*pap = pa;

	return 1;
}

void
pmap_protect(pmap_t pm, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	struct l2_bucket *l2b;
	pt_entry_t *ptep, opte, npte;
	vaddr_t next_bucket;
	int flush;

	NPDEBUG(PDB_PROTECT,
	    printf("pmap_protect: pm %p sva 0x%lx eva 0x%lx prot 0x%x",
	    pm, sva, eva, prot));

	if ((prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC))
		return;

	if (prot == PROT_NONE) {
		pmap_remove(pm, sva, eva);
		return;
	}
		
	/* XXX is that threshold of 4 the best choice for v7? */
	if (pmap_is_current(pm))
		flush = ((eva - sva) > (PAGE_SIZE * 4)) ? -1 : 0;
	else
		flush = -1;

	while (sva < eva) {
		next_bucket = L2_NEXT_BUCKET(sva);
		if (next_bucket > eva)
			next_bucket = eva;

		l2b = pmap_get_l2_bucket(pm, sva);
		if (l2b == NULL) {
			sva = next_bucket;
			continue;
		}

		ptep = &l2b->l2b_kva[l2pte_index(sva)];

		while (sva < next_bucket) {
			npte = opte = *ptep;
			if (opte != L2_TYPE_INV) {
				struct vm_page *pg;

				if ((prot & PROT_WRITE) == 0)
					npte |= L2_V7_AP(0x4);
				if ((prot & PROT_EXEC) == 0)
					npte |= L2_V7_S_XN;
				*ptep = npte;
				PTE_SYNC(ptep);

				pg = PHYS_TO_VM_PAGE(l2pte_pa(opte));
				if (pg != NULL && (prot & PROT_WRITE) == 0)
					pmap_modify_pv(pg, pm, sva,
					    PVF_WRITE, 0);

				if (flush >= 0) {
					flush++;
					if (opte & L2_V7_AF)
						cpu_tlb_flushID_SE(sva);
				}
			}

			sva += PAGE_SIZE;
			ptep++;
		}
	}

	if (flush < 0)
		pmap_tlb_flushID(pm);

	NPDEBUG(PDB_PROTECT, printf("\n"));
}

void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{

	NPDEBUG(PDB_PROTECT,
	    printf("pmap_page_protect: pg %p (0x%08lx), prot 0x%x\n",
	    pg, pg->phys_addr, prot));

	switch(prot) {
	case PROT_READ | PROT_WRITE | PROT_EXEC:
	case PROT_READ | PROT_WRITE:
		return;

	case PROT_READ:
	case PROT_READ | PROT_EXEC:
		pmap_clearbit(pg, PVF_WRITE);
		break;

	default:
		pmap_page_remove(pg);
		break;
	}
}

/*
 * pmap_clear_modify:
 *
 *	Clear the "modified" attribute for a page.
 */
int
pmap_clear_modify(struct vm_page *pg)
{
	int rv;

	if (pg->mdpage.pvh_attrs & PVF_MOD) {
		rv = 1;
		pmap_clearbit(pg, PVF_MOD);
	} else
		rv = 0;

	return (rv);
}

/*
 * pmap_clear_reference:
 *
 *	Clear the "referenced" attribute for a page.
 */
int
pmap_clear_reference(struct vm_page *pg)
{
	int rv;

	if (pg->mdpage.pvh_attrs & PVF_REF) {
		rv = 1;
		pmap_clearbit(pg, PVF_REF);
	} else
		rv = 0;

	return (rv);
}

/*
 * pmap_is_modified:
 *
 *	Test if a page has the "modified" attribute.
 */
/* See <arm/pmap.h> */

/*
 * pmap_is_referenced:
 *
 *	Test if a page has the "referenced" attribute.
 */
/* See <arm/pmap.h> */

/*
 * dab_access() handles the following data aborts:
 *
 *  FAULT_ACCESS_2 - Access flag fault -- Level 2
 *
 * Set the Access Flag and mark the page as referenced.
 */
int
dab_access(trapframe_t *tf, u_int fsr, u_int far, struct proc *p)
{
	struct pmap *pm = p->p_vmspace->vm_map.pmap;
	vaddr_t va = trunc_page(far);
	struct l2_dtable *l2;
	struct l2_bucket *l2b;
	pt_entry_t *ptep, pte;
	struct pv_entry *pv;
	struct vm_page *pg;
	paddr_t pa;
	u_int l1idx;

	if (!TRAP_USERMODE(tf) && far >= VM_MIN_KERNEL_ADDRESS)
		pm = pmap_kernel();

	l1idx = L1_IDX(va);

	/*
	 * If there is no l2_dtable for this address, then the process
	 * has no business accessing it.
	 */
	l2 = pm->pm_l2[L2_IDX(l1idx)];
	KASSERT(l2 != NULL);

	/*
	 * Likewise if there is no L2 descriptor table
	 */
	l2b = &l2->l2_bucket[L2_BUCKET(l1idx)];
	KASSERT(l2b->l2b_kva != NULL);

	/*
	 * Check the PTE itself.
	 */
	ptep = &l2b->l2b_kva[l2pte_index(va)];
	pte = *ptep;
	KASSERT(pte != L2_TYPE_INV);

	pa = l2pte_pa(pte);

	/*
	 * Perform page referenced emulation.
	 */
	KASSERT((pte & L2_V7_AF) == 0);

	/* Extract the physical address of the page */
	pg = PHYS_TO_VM_PAGE(pa);
	KASSERT(pg != NULL);

	/* Get the current flags for this page. */
	pv = pmap_find_pv(pg, pm, va);
	KASSERT(pv != NULL);

	pg->mdpage.pvh_attrs |= PVF_REF;
	pv->pv_flags |= PVF_REF;
	pte |= L2_V7_AF;

	*ptep = pte;
	PTE_SYNC(ptep);
	return 0;
}

/*
 * Routine:	pmap_proc_iflush
 *
 * Function:
 *	Synchronize caches corresponding to [addr, addr+len) in p.
 *
 */
void
pmap_proc_iflush(struct process *pr, vaddr_t va, vsize_t len)
{
	/* We only need to do anything if it is the current process. */
	if (pr == curproc->p_p)
		cpu_icache_sync_range(va, len);
}

/*
 * Routine:	pmap_unwire
 * Function:	Clear the wired attribute for a map/virtual-address pair.
 *
 * In/out conditions:
 *		The mapping must already exist in the pmap.
 */
void
pmap_unwire(pmap_t pm, vaddr_t va)
{
	struct l2_bucket *l2b;
	pt_entry_t *ptep, pte;
	struct vm_page *pg;
	paddr_t pa;

	NPDEBUG(PDB_WIRING, printf("pmap_unwire: pm %p, va 0x%08lx\n", pm, va));

	l2b = pmap_get_l2_bucket(pm, va);
	KDASSERT(l2b != NULL);

	ptep = &l2b->l2b_kva[l2pte_index(va)];
	pte = *ptep;

	/* Extract the physical address of the page */
	pa = l2pte_pa(pte);

	if ((pg = PHYS_TO_VM_PAGE(pa)) != NULL) {
		/* Update the wired bit in the pv entry for this page. */
		(void) pmap_modify_pv(pg, pm, va, PVF_WIRED, 0);
	}
}

void
pmap_activate(struct proc *p)
{
	pmap_t pm;
	struct pcb *pcb;

	pm = p->p_vmspace->vm_map.pmap;
	pcb = &p->p_addr->u_pcb;

	pmap_set_pcb_pagedir(pm, pcb);

	if (p == curproc) {
		u_int cur_ttb;

		__asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(cur_ttb));

		cur_ttb &= ~(L1_TABLE_SIZE - 1);

		if (cur_ttb == (u_int)pcb->pcb_pagedir) {
			/*
			 * No need to switch address spaces.
			 */
			return;
		}

		__asm volatile("cpsid if");
		cpu_setttb(pcb->pcb_pagedir);
		__asm volatile("cpsie if");
	}
}

void
pmap_update(pmap_t pm)
{
	/*
	 * make sure TLB/cache operations have completed.
	 */
}

/*
 * Retire the given physical map from service.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_destroy(pmap_t pm)
{
	u_int count;

	/*
	 * Drop reference count
	 */
	count = --pm->pm_refs;
	if (count > 0)
		return;

	/*
	 * reference count is zero, free pmap resources and then free pmap.
	 */

	pmap_free_l1(pm);

	/* return the pmap to the pool */
	pool_put(&pmap_pmap_pool, pm);
}


/*
 * void pmap_reference(pmap_t pm)
 *
 * Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pm)
{
	if (pm == NULL)
		return;

	pm->pm_refs++;
}

/*
 * pmap_zero_page()
 * 
 * Zero a given physical page by mapping it at a page hook point.
 * In doing the zero page op, the page we zero is mapped cacheable, as with
 * StrongARM accesses to non-cached pages are non-burst making writing
 * _any_ bulk data very slow.
 */
void
pmap_zero_page(struct vm_page *pg)
{
	paddr_t phys = VM_PAGE_TO_PHYS(pg);
#ifdef DEBUG
	if (pg->mdpage.pvh_list != NULL)
		panic("pmap_zero_page: page has mappings");
#endif

	/*
	 * Hook in the page, zero it, and purge the cache for that
	 * zeroed page. Invalidate the TLB as needed.
	 */
	*cdst_pte = L2_S_PROTO | phys | L2_V7_AF |
	    L2_S_PROT(PTE_KERNEL, PROT_WRITE) | PTE_L2_S_CACHE_MODE;
	PTE_SYNC(cdst_pte);
	cpu_tlb_flushD_SE(cdstp);
	bzero_page(cdstp);
}

/*
 * pmap_copy_page()
 *
 * Copy one physical page into another, by mapping the pages into
 * hook points. The same comment regarding cachability as in
 * pmap_zero_page also applies here.
 */
void
pmap_copy_page(struct vm_page *src_pg, struct vm_page *dst_pg)
{
	paddr_t src = VM_PAGE_TO_PHYS(src_pg);
	paddr_t dst = VM_PAGE_TO_PHYS(dst_pg);
#ifdef DEBUG
	if (dst_pg->mdpage.pvh_list != NULL)
		panic("pmap_copy_page: dst page has mappings");
#endif

	/*
	 * Map the pages into the page hook points, copy them, and purge
	 * the cache for the appropriate page. Invalidate the TLB
	 * as required.
	 */
	*csrc_pte = L2_S_PROTO | src | L2_V7_AF |
	    L2_S_PROT(PTE_KERNEL, PROT_READ) | PTE_L2_S_CACHE_MODE;
	PTE_SYNC(csrc_pte);
	*cdst_pte = L2_S_PROTO | dst | L2_V7_AF |
	    L2_S_PROT(PTE_KERNEL, PROT_WRITE) | PTE_L2_S_CACHE_MODE;
	PTE_SYNC(cdst_pte);
	cpu_tlb_flushD_SE(csrcp);
	cpu_tlb_flushD_SE(cdstp);
	bcopy_page(csrcp, cdstp);
}

/*
 * void pmap_virtual_space(vaddr_t *start, vaddr_t *end)
 *
 * Return the start and end addresses of the kernel's virtual space.
 * These values are setup in pmap_bootstrap and are updated as pages
 * are allocated.
 */
void
pmap_virtual_space(vaddr_t *start, vaddr_t *end)
{
	*start = virtual_avail;
	*end = virtual_end;
}

/*
 * Helper function for pmap_grow_l2_bucket()
 */
static __inline int
pmap_grow_map(vaddr_t va, pt_entry_t cache_mode, paddr_t *pap)
{
	struct l2_bucket *l2b;
	pt_entry_t *ptep;
	paddr_t pa;

	KASSERT((va & PAGE_MASK) == 0);

	if (uvm.page_init_done == 0) {
		if (uvm_page_physget(&pa) == 0)
			return (1);
	} else {
		struct vm_page *pg;
		pg = uvm_pagealloc(NULL, 0, NULL, UVM_PGA_USERESERVE);
		if (pg == NULL)
			return (1);
		pa = VM_PAGE_TO_PHYS(pg);
	}

	if (pap)
		*pap = pa;

	l2b = pmap_get_l2_bucket(pmap_kernel(), va);
	KDASSERT(l2b != NULL);

	ptep = &l2b->l2b_kva[l2pte_index(va)];
	*ptep = L2_S_PROTO | pa | L2_V7_AF | cache_mode |
	    L2_S_PROT(PTE_KERNEL, PROT_READ | PROT_WRITE);
	PTE_SYNC(ptep);
	cpu_tlb_flushD_SE(va);

	memset((void *)va, 0, PAGE_SIZE);
	return (0);
}

/*
 * This is the same as pmap_alloc_l2_bucket(), except that it is only
 * used by pmap_growkernel().
 */
static __inline struct l2_bucket *
pmap_grow_l2_bucket(pmap_t pm, vaddr_t va)
{
	struct l2_dtable *l2;
	struct l2_bucket *l2b;
	u_short l1idx;
	vaddr_t nva;

	l1idx = L1_IDX(va);

	if ((l2 = pm->pm_l2[L2_IDX(l1idx)]) == NULL) {
		/*
		 * No mapping at this address, as there is
		 * no entry in the L1 table.
		 * Need to allocate a new l2_dtable.
		 */
		nva = pmap_kernel_l2dtable_kva;
		if ((nva & PGOFSET) == 0) {
			/*
			 * Need to allocate a backing page
			 */
			if (pmap_grow_map(nva, PTE_L2_S_CACHE_MODE, NULL))
				return (NULL);
		}

		l2 = (struct l2_dtable *)nva;
		nva += sizeof(struct l2_dtable);

		if ((nva & PGOFSET) < (pmap_kernel_l2dtable_kva & PGOFSET)) {
			/*
			 * The new l2_dtable straddles a page boundary.
			 * Map in another page to cover it.
			 */
			if (pmap_grow_map(trunc_page(nva),
			    PTE_L2_S_CACHE_MODE, NULL))
				return (NULL);
		}

		pmap_kernel_l2dtable_kva = nva;

		/*
		 * Link it into the parent pmap
		 */
		pm->pm_l2[L2_IDX(l1idx)] = l2;
	}

	l2b = &l2->l2_bucket[L2_BUCKET(l1idx)];

	/*
	 * Fetch pointer to the L2 page table associated with the address.
	 */
	if (l2b->l2b_kva == NULL) {
		pt_entry_t *ptep;

		/*
		 * No L2 page table has been allocated. Chances are, this
		 * is because we just allocated the l2_dtable, above.
		 */
		nva = pmap_kernel_l2ptp_kva;
		ptep = (pt_entry_t *)nva;
		if ((nva & PGOFSET) == 0) {
			/*
			 * Need to allocate a backing page
			 */
			if (pmap_grow_map(nva, PTE_L2_S_CACHE_MODE_PT,
			    &pmap_kernel_l2ptp_phys))
				return (NULL);
			PTE_SYNC_RANGE(ptep, PAGE_SIZE / sizeof(pt_entry_t));
		}

		l2->l2_occupancy++;
		l2b->l2b_kva = ptep;
		l2b->l2b_l1idx = l1idx;
		l2b->l2b_phys = pmap_kernel_l2ptp_phys;

		pmap_kernel_l2ptp_kva += L2_TABLE_SIZE_REAL;
		pmap_kernel_l2ptp_phys += L2_TABLE_SIZE_REAL;
	}

	return (l2b);
}

vaddr_t
pmap_growkernel(vaddr_t maxkvaddr)
{
	pmap_t kpm = pmap_kernel();
	struct l1_ttable *l1;
	struct l2_bucket *l2b;
	pd_entry_t *pl1pd;
	int s;

	if (maxkvaddr <= pmap_curmaxkvaddr)
		goto out;		/* we are OK */

	NPDEBUG(PDB_GROWKERN,
	    printf("pmap_growkernel: growing kernel from 0x%lx to 0x%lx\n",
	    pmap_curmaxkvaddr, maxkvaddr));

	KDASSERT(maxkvaddr <= virtual_end);

	/*
	 * whoops!   we need to add kernel PTPs
	 */

	s = splhigh();	/* to be safe */

	/* Map 1MB at a time */
	for (; pmap_curmaxkvaddr < maxkvaddr; pmap_curmaxkvaddr += L1_S_SIZE) {

		l2b = pmap_grow_l2_bucket(kpm, pmap_curmaxkvaddr);
		KDASSERT(l2b != NULL);

		/* Distribute new L1 entry to all other L1s */
		TAILQ_FOREACH(l1, &l1_list, l1_link) {
			pl1pd = &l1->l1_kva[L1_IDX(pmap_curmaxkvaddr)];
			*pl1pd = L1_C_PROTO | l2b->l2b_phys;
			PTE_SYNC(pl1pd);
		}
	}

	/*
	 * flush out the cache, expensive but growkernel will happen so
	 * rarely
	 */
	cpu_dcache_wbinv_all();
	cpu_sdcache_wbinv_all();
	cpu_tlb_flushD();

	splx(s);

out:
	return (pmap_curmaxkvaddr);
}

/************************ Utility routines ****************************/

/*
 * vector_page_setprot:
 *
 *	Manipulate the protection of the vector page.
 */
void
vector_page_setprot(int prot)
{
	struct l2_bucket *l2b;
	pt_entry_t *ptep;

	l2b = pmap_get_l2_bucket(pmap_kernel(), vector_page);
	KDASSERT(l2b != NULL);

	ptep = &l2b->l2b_kva[l2pte_index(vector_page)];

	*ptep = (*ptep & ~L2_S_PROT_MASK) | L2_S_PROT(PTE_KERNEL, prot);
	PTE_SYNC(ptep);
	cpu_tlb_flushD_SE(vector_page);
}

/*
 * This is used to stuff certain critical values into the PCB where they
 * can be accessed quickly from cpu_switch() et al.
 */
void
pmap_set_pcb_pagedir(pmap_t pm, struct pcb *pcb)
{
	KDASSERT(pm->pm_l1);
	pcb->pcb_pagedir = pm->pm_l1->l1_physaddr;
}

/*
 * Fetch pointers to the PDE/PTE for the given pmap/VA pair.
 * Returns 1 if the mapping exists, else 0.
 *
 * NOTE: This function is only used by a couple of arm-specific modules.
 * It is not safe to take any pmap locks here, since we could be right
 * in the middle of debugging the pmap anyway...
 *
 * It is possible for this routine to return 0 even though a valid
 * mapping does exist. This is because we don't lock, so the metadata
 * state may be inconsistent.
 *
 * NOTE: We can return a NULL *ptp in the case where the L1 pde is
 * a "section" mapping.
 */
int
pmap_get_pde_pte(pmap_t pm, vaddr_t va, pd_entry_t **pdp, pt_entry_t **ptp)
{
	struct l2_dtable *l2;
	pd_entry_t *pl1pd, l1pd;
	pt_entry_t *ptep;
	u_short l1idx;

	if (pm->pm_l1 == NULL)
		return 0;

	l1idx = L1_IDX(va);
	*pdp = pl1pd = &pm->pm_l1->l1_kva[l1idx];
	l1pd = *pl1pd;

	if (l1pte_section_p(l1pd)) {
		*ptp = NULL;
		return 1;
	}

	l2 = pm->pm_l2[L2_IDX(l1idx)];
	if (l2 == NULL ||
	    (ptep = l2->l2_bucket[L2_BUCKET(l1idx)].l2b_kva) == NULL) {
		return 0;
	}

	*ptp = &ptep[l2pte_index(va)];
	return 1;
}

/************************ Bootstrapping routines ****************************/

void
pmap_init_l1(struct l1_ttable *l1, pd_entry_t *l1pt)
{
	l1->l1_kva = l1pt;

	/*
	 * Copy the kernel's L1 entries to each new L1.
	 */
	if (pmap_initialized)
		memcpy(l1pt, pmap_kernel()->pm_l1->l1_kva, L1_TABLE_SIZE);

	if (pmap_extract(pmap_kernel(), (vaddr_t)l1pt, &l1->l1_physaddr) == 0)
		panic("pmap_init_l1: can't get PA of L1 at %p", l1pt);

	TAILQ_INSERT_TAIL(&l1_list, l1, l1_link);
}

/*
 * pmap_bootstrap() is called from the board-specific initarm() routine
 * once the kernel L1/L2 descriptors tables have been set up.
 *
 * This is a somewhat convoluted process since pmap bootstrap is, effectively,
 * spread over a number of disparate files/functions.
 *
 * We are passed the following parameters
 *  - kernel_l1pt
 *    This is a pointer to the base of the kernel's L1 translation table.
 *  - vstart
 *    1MB-aligned start of managed kernel virtual memory.
 *  - vend
 *    1MB-aligned end of managed kernel virtual memory.
 *
 * We use the first parameter to build the metadata (struct l1_ttable and
 * struct l2_dtable) necessary to track kernel mappings.
 */
#define	PMAP_STATIC_L2_SIZE 16
void
pmap_bootstrap(pd_entry_t *kernel_l1pt, vaddr_t vstart, vaddr_t vend)
{
	static struct l1_ttable static_l1;
	static struct l2_dtable static_l2[PMAP_STATIC_L2_SIZE];
	struct l1_ttable *l1 = &static_l1;
	struct l2_dtable *l2;
	struct l2_bucket *l2b;
	pmap_t pm = pmap_kernel();
	pd_entry_t pde;
	pt_entry_t *ptep;
	paddr_t pa;
	vsize_t size;
	int l1idx, l2idx, l2next = 0;

	/*
	 * Initialise the kernel pmap object
	 */
	pm->pm_l1 = l1;
	pm->pm_refs = 1;

	/*
	 * Scan the L1 translation table created by initarm() and create
	 * the required metadata for all valid mappings found in it.
	 */
	for (l1idx = 0; l1idx < (L1_TABLE_SIZE / sizeof(pd_entry_t)); l1idx++) {
		pde = kernel_l1pt[l1idx];

		/*
		 * We're only interested in Coarse mappings.
		 * pmap_extract() can deal with section mappings without
		 * recourse to checking L2 metadata.
		 */
		if ((pde & L1_TYPE_MASK) != L1_TYPE_C)
			continue;

		/*
		 * Lookup the KVA of this L2 descriptor table
		 */
		pa = (paddr_t)(pde & L1_C_ADDR_MASK);
		ptep = (pt_entry_t *)kernel_pt_lookup(pa);
		if (ptep == NULL) {
			panic("pmap_bootstrap: No L2 for va 0x%x, pa 0x%lx",
			    (u_int)l1idx << L1_S_SHIFT, pa);
		}

		/*
		 * Fetch the associated L2 metadata structure.
		 * Allocate a new one if necessary.
		 */
		if ((l2 = pm->pm_l2[L2_IDX(l1idx)]) == NULL) {
			if (l2next == PMAP_STATIC_L2_SIZE)
				panic("pmap_bootstrap: out of static L2s");
			pm->pm_l2[L2_IDX(l1idx)] = l2 = &static_l2[l2next++];
		}

		/*
		 * One more L1 slot tracked...
		 */
		l2->l2_occupancy++;

		/*
		 * Fill in the details of the L2 descriptor in the
		 * appropriate bucket.
		 */
		l2b = &l2->l2_bucket[L2_BUCKET(l1idx)];
		l2b->l2b_kva = ptep;
		l2b->l2b_phys = pa;
		l2b->l2b_l1idx = l1idx;

		/*
		 * Establish an initial occupancy count for this descriptor
		 */
		for (l2idx = 0;
		    l2idx < (L2_TABLE_SIZE_REAL / sizeof(pt_entry_t));
		    l2idx++) {
			if (ptep[l2idx] != L2_TYPE_INV)
				l2b->l2b_occupancy++;
		}
	}

	cpu_idcache_wbinv_all();
	cpu_sdcache_wbinv_all();
	cpu_tlb_flushID();

	/*
	 * now we allocate the "special" VAs which are used for tmp mappings
	 * by the pmap (and other modules).  we allocate the VAs by advancing
	 * virtual_avail (note that there are no pages mapped at these VAs).
	 *
	 * Managed KVM space start from wherever initarm() tells us.
	 */
	virtual_avail = vstart;
	virtual_end = vend;

	pmap_alloc_specials(&virtual_avail, 1, &csrcp, &csrc_pte);
	pmap_alloc_specials(&virtual_avail, 1, &cdstp, &cdst_pte);
	pmap_alloc_specials(&virtual_avail, 1, (void *)&memhook, NULL);
	pmap_alloc_specials(&virtual_avail, round_page(MSGBUFSIZE) / PAGE_SIZE,
	    (void *)&msgbufaddr, NULL);

	/*
	 * Allocate a range of kernel virtual address space to be used
	 * for L2 descriptor tables and metadata allocation in
	 * pmap_growkernel().
	 */
	size = ((virtual_end - pmap_curmaxkvaddr) + L1_S_OFFSET) / L1_S_SIZE;
	pmap_alloc_specials(&virtual_avail,
	    round_page(size * L2_TABLE_SIZE_REAL) / PAGE_SIZE,
	    &pmap_kernel_l2ptp_kva, NULL);

	size = (size + (L2_BUCKET_SIZE - 1)) / L2_BUCKET_SIZE;
	pmap_alloc_specials(&virtual_avail,
	    round_page(size * sizeof(struct l2_dtable)) / PAGE_SIZE,
	    &pmap_kernel_l2dtable_kva, NULL);

	/*
	 * We can now initialise the first L1's metadata.
	 */
	TAILQ_INIT(&l1_list);
	pmap_init_l1(l1, kernel_l1pt);

	/*
	 * Initialize the pmap pool.
	 */
	pool_init(&pmap_pmap_pool, sizeof(struct pmap), 0, IPL_NONE, 0,
	    "pmappl", &pool_allocator_single);
	pool_init(&pmap_pv_pool, sizeof(struct pv_entry), 0, IPL_VM, 0,
	    "pvepl", &pmap_pv_allocator);
	pool_init(&pmap_l2dtable_pool, sizeof(struct l2_dtable), 0, IPL_VM, 0,
	    "l2dtblpl", NULL);
	pool_init(&pmap_l2ptp_pool, L2_TABLE_SIZE_REAL, L2_TABLE_SIZE_REAL,
	    IPL_VM, 0, "l2ptppl", &pool_allocator_single);

	cpu_dcache_wbinv_all();
	cpu_sdcache_wbinv_all();
}

void
pmap_alloc_specials(vaddr_t *availp, int pages, vaddr_t *vap, pt_entry_t **ptep)
{
	vaddr_t va = *availp;
	struct l2_bucket *l2b;

	if (ptep) {
		l2b = pmap_get_l2_bucket(pmap_kernel(), va);
		if (l2b == NULL)
			panic("pmap_alloc_specials: no l2b for 0x%lx", va);

		if (ptep)
			*ptep = &l2b->l2b_kva[l2pte_index(va)];
	}

	*vap = va;
	*availp = va + (PAGE_SIZE * pages);
}

void
pmap_init(void)
{
	pool_setlowat(&pmap_pv_pool, (PAGE_SIZE / sizeof(struct pv_entry)) * 2);

	pmap_initialized = 1;
}

void *
pmap_pv_page_alloc(struct pool *pp, int flags, int *slowdown)
{
	struct kmem_dyn_mode kd = KMEM_DYN_INITIALIZER;

	kd.kd_waitok = ISSET(flags, PR_WAITOK);
	kd.kd_slowdown = slowdown;

	return (km_alloc(pp->pr_pgsize,
	    pmap_initialized ? &kv_page : &kv_any, pp->pr_crange, &kd));
}

void
pmap_pv_page_free(struct pool *pp, void *v)
{
	km_free(v, pp->pr_pgsize, &kv_page, pp->pr_crange);
}

/*
 * pmap_postinit()
 *
 * This routine is called after the vm and kmem subsystems have been
 * initialised. This allows the pmap code to perform any initialisation
 * that can only be done once the memory allocation is in place.
 */
void
pmap_postinit(void)
{
	pool_setlowat(&pmap_l2ptp_pool,
	    (PAGE_SIZE / L2_TABLE_SIZE_REAL) * 4);
	pool_setlowat(&pmap_l2dtable_pool,
	    (PAGE_SIZE / sizeof(struct l2_dtable)) * 2);
}

/*
 * Note that the following routines are used by board-specific initialisation
 * code to configure the initial kernel page tables.
 *
 * If ARM32_NEW_VM_LAYOUT is *not* defined, they operate on the assumption that
 * L2 page-table pages are 4KB in size and use 4 L1 slots. This mimics the
 * behaviour of the old pmap, and provides an easy migration path for
 * initial bring-up of the new pmap on existing ports. Fortunately,
 * pmap_bootstrap() compensates for this hackery. This is only a stop-gap and
 * will be deprecated.
 *
 * If ARM32_NEW_VM_LAYOUT *is* defined, these functions deal with 1KB L2 page
 * tables.
 */

/*
 * This list exists for the benefit of pmap_map_chunk().  It keeps track
 * of the kernel L2 tables during bootstrap, so that pmap_map_chunk() can
 * find them as necessary.
 *
 * Note that the data on this list MUST remain valid after initarm() returns,
 * as pmap_bootstrap() uses it to construct L2 table metadata.
 */
SLIST_HEAD(, pv_addr) kernel_pt_list = SLIST_HEAD_INITIALIZER(kernel_pt_list);

vaddr_t
kernel_pt_lookup(paddr_t pa)
{
	pv_addr_t *pv;

	SLIST_FOREACH(pv, &kernel_pt_list, pv_list) {
#ifndef ARM32_NEW_VM_LAYOUT
		if (pv->pv_pa == (pa & ~PGOFSET))
			return (pv->pv_va | (pa & PGOFSET));
#else
		if (pv->pv_pa == pa)
			return (pv->pv_va);
#endif
	}
	return (0);
}

/*
 * pmap_map_section:
 *
 *	Create a single section mapping.
 */
void
pmap_map_section(vaddr_t l1pt, vaddr_t va, paddr_t pa, int prot, int cache)
{
	pd_entry_t *pde = (pd_entry_t *) l1pt;
	pd_entry_t fl;

	switch (cache) {
	case PTE_NOCACHE:
	default:
		fl = 0;
		break;

	case PTE_CACHE:
		fl = PTE_L1_S_CACHE_MODE;
		break;

	case PTE_PAGETABLE:
		fl = PTE_L1_S_CACHE_MODE_PT;
		break;
	}

	pde[va >> L1_S_SHIFT] = L1_S_PROTO | pa | L1_S_V7_AF |
	    L1_S_PROT(PTE_KERNEL, prot) | fl;
	PTE_SYNC(&pde[va >> L1_S_SHIFT]);
}

/*
 * pmap_map_entry:
 *
 *	Create a single page mapping.
 */
void
pmap_map_entry(vaddr_t l1pt, vaddr_t va, paddr_t pa, int prot, int cache)
{
	pd_entry_t *pde = (pd_entry_t *) l1pt;
	pt_entry_t fl;
	pt_entry_t *pte;

	switch (cache) {
	case PTE_NOCACHE:
	default:
		fl = 0;
		break;

	case PTE_CACHE:
		fl = PTE_L2_S_CACHE_MODE;
		break;

	case PTE_PAGETABLE:
		fl = PTE_L2_S_CACHE_MODE_PT;
		break;
	}

	if ((pde[va >> L1_S_SHIFT] & L1_TYPE_MASK) != L1_TYPE_C)
		panic("pmap_map_entry: no L2 table for VA 0x%08lx", va);

#ifndef ARM32_NEW_VM_LAYOUT
	pte = (pt_entry_t *)
	    kernel_pt_lookup(pde[va >> L1_S_SHIFT] & L2_S_FRAME);
#else
	pte = (pt_entry_t *) kernel_pt_lookup(pde[L1_IDX(va)] & L1_C_ADDR_MASK);
#endif
	if (pte == NULL)
		panic("pmap_map_entry: can't find L2 table for VA 0x%08lx", va);

#ifndef ARM32_NEW_VM_LAYOUT
	pte[(va >> PGSHIFT) & 0x3ff] = L2_S_PROTO | pa | L2_V7_AF |
	    L2_S_PROT(PTE_KERNEL, prot) | fl;
	PTE_SYNC(&pte[(va >> PGSHIFT) & 0x3ff]);
#else
	pte[l2pte_index(va)] = L2_S_PROTO | pa | L2_V7_AF |
	    L2_S_PROT(PTE_KERNEL, prot) | fl;
	PTE_SYNC(&pte[l2pte_index(va)]);
#endif
}

/*
 * pmap_link_l2pt:
 *
 *	Link the L2 page table specified by "l2pv" into the L1
 *	page table at the slot for "va".
 */
void
pmap_link_l2pt(vaddr_t l1pt, vaddr_t va, pv_addr_t *l2pv)
{
	pd_entry_t *pde = (pd_entry_t *) l1pt;
	u_int slot = va >> L1_S_SHIFT;

	pde[slot + 0] = L1_C_PROTO | (l2pv->pv_pa + 0x000);
#ifdef ARM32_NEW_VM_LAYOUT
	PTE_SYNC(&pde[slot]);
#else
	pde[slot + 1] = L1_C_PROTO | (l2pv->pv_pa + 0x400);
	pde[slot + 2] = L1_C_PROTO | (l2pv->pv_pa + 0x800);
	pde[slot + 3] = L1_C_PROTO | (l2pv->pv_pa + 0xc00);
	PTE_SYNC_RANGE(&pde[slot + 0], 4);
#endif

	SLIST_INSERT_HEAD(&kernel_pt_list, l2pv, pv_list);
}

/*
 * pmap_map_chunk:
 *
 *	Map a chunk of memory using the most efficient mappings
 *	possible (section, large page, small page) into the
 *	provided L1 and L2 tables at the specified virtual address.
 */
vsize_t
pmap_map_chunk(vaddr_t l1pt, vaddr_t va, paddr_t pa, vsize_t size,
    int prot, int cache)
{
	pd_entry_t *pde = (pd_entry_t *) l1pt;
	pt_entry_t *pte, f1, f2s, f2l;
	vsize_t resid;  
	int i;

	resid = (size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

	if (l1pt == 0)
		panic("pmap_map_chunk: no L1 table provided");

#ifdef VERBOSE_INIT_ARM     
	printf("pmap_map_chunk: pa=0x%lx va=0x%lx size=0x%lx resid=0x%lx "
	    "prot=0x%x cache=%d\n", pa, va, size, resid, prot, cache);
#endif

	switch (cache) {
	case PTE_NOCACHE:
	default:
		f1 = 0;
		f2l = 0;
		f2s = 0;
		break;

	case PTE_CACHE:
		f1 = PTE_L1_S_CACHE_MODE;
		f2l = PTE_L2_L_CACHE_MODE;
		f2s = PTE_L2_S_CACHE_MODE;
		break;

	case PTE_PAGETABLE:
		f1 = PTE_L1_S_CACHE_MODE_PT;
		f2l = PTE_L2_L_CACHE_MODE_PT;
		f2s = PTE_L2_S_CACHE_MODE_PT;
		break;
	}

	size = resid;

	while (resid > 0) {
		/* See if we can use a section mapping. */
		if (L1_S_MAPPABLE_P(va, pa, resid)) {
#ifdef VERBOSE_INIT_ARM
			printf("S");
#endif
			pde[va >> L1_S_SHIFT] = L1_S_PROTO | pa |
			    L1_S_V7_AF | L1_S_PROT(PTE_KERNEL, prot) | f1;
			PTE_SYNC(&pde[va >> L1_S_SHIFT]);
			va += L1_S_SIZE;
			pa += L1_S_SIZE;
			resid -= L1_S_SIZE;
			continue;
		}

		/*
		 * Ok, we're going to use an L2 table.  Make sure
		 * one is actually in the corresponding L1 slot
		 * for the current VA.
		 */
		if ((pde[va >> L1_S_SHIFT] & L1_TYPE_MASK) != L1_TYPE_C)
			panic("pmap_map_chunk: no L2 table for VA 0x%08lx", va);

#ifndef ARM32_NEW_VM_LAYOUT
		pte = (pt_entry_t *)
		    kernel_pt_lookup(pde[va >> L1_S_SHIFT] & L2_S_FRAME);
#else
		pte = (pt_entry_t *) kernel_pt_lookup(
		    pde[L1_IDX(va)] & L1_C_ADDR_MASK);
#endif
		if (pte == NULL)
			panic("pmap_map_chunk: can't find L2 table for VA"
			    "0x%08lx", va);

		/* See if we can use a L2 large page mapping. */
		if (L2_L_MAPPABLE_P(va, pa, resid)) {
#ifdef VERBOSE_INIT_ARM
			printf("L");
#endif
			for (i = 0; i < 16; i++) {
#ifndef ARM32_NEW_VM_LAYOUT
				pte[((va >> PGSHIFT) & 0x3f0) + i] =
				    L2_L_PROTO | pa | L2_V7_AF |
				    L2_L_PROT(PTE_KERNEL, prot) | f2l;
				PTE_SYNC(&pte[((va >> PGSHIFT) & 0x3f0) + i]);
#else
				pte[l2pte_index(va) + i] =
				    L2_L_PROTO | pa | L2_V7_AF |
				    L2_L_PROT(PTE_KERNEL, prot) | f2l;
				PTE_SYNC(&pte[l2pte_index(va) + i]);
#endif
			}
			va += L2_L_SIZE;
			pa += L2_L_SIZE;
			resid -= L2_L_SIZE;
			continue;
		}

		/* Use a small page mapping. */
#ifdef VERBOSE_INIT_ARM
		printf("P");
#endif
#ifndef ARM32_NEW_VM_LAYOUT
		pte[(va >> PGSHIFT) & 0x3ff] = L2_S_PROTO | pa | L2_V7_AF |
		    L2_S_PROT(PTE_KERNEL, prot) | f2s;
		PTE_SYNC(&pte[(va >> PGSHIFT) & 0x3ff]);
#else
		pte[l2pte_index(va)] = L2_S_PROTO | pa | L2_V7_AF |
		    L2_S_PROT(PTE_KERNEL, prot) | f2s;
		PTE_SYNC(&pte[l2pte_index(va)]);
#endif
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
		resid -= PAGE_SIZE;
	}
#ifdef VERBOSE_INIT_ARM
	printf("\n");
#endif
	return (size);
}

/********************** PTE initialization routines **************************/

/*
 * This routine is called to set up cache modes, etc.
 */

void
pmap_pte_init_armv7(void)
{
	uint32_t id_mmfr0, id_mmfr3;

	pmap_needs_pte_sync = 1;

	/* Check if the PXN bit is supported. */
	__asm volatile("mrc p15, 0, %0, c0, c1, 4" : "=r"(id_mmfr0));
	if ((id_mmfr0 & ID_MMFR0_VMSA_MASK) >= VMSA_V7_PXN)
		l1_c_pxn = L1_C_V7_PXN;

	/* Check for coherent walk. */
	__asm volatile("mrc p15, 0, %0, c0, c1, 7" : "=r"(id_mmfr3));
	if ((id_mmfr3 & 0x00f00000) == 0x00100000)
		pmap_needs_pte_sync = 0;
}
