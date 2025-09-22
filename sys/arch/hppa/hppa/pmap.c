/*	$OpenBSD: pmap.c,v 1.181 2023/01/24 16:51:05 kettenis Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * References:
 * 1. PA7100LC ERS, Hewlett-Packard, March 30 1999, Public version 1.0
 * 2. PA7300LC ERS, Hewlett-Packard, March 18 1996, Version 1.0
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/pool.h>
#include <sys/extent.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/iomod.h>

#ifdef PMAPDEBUG
#define	DPRINTF(l,s)	do {		\
	if ((pmapdebug & (l)) == (l))	\
		printf s;		\
} while(0)
#define	PDB_FOLLOW	0x00000001
#define	PDB_INIT	0x00000002
#define	PDB_ENTER	0x00000004
#define	PDB_REMOVE	0x00000008
#define	PDB_CREATE	0x00000010
#define	PDB_PTPAGE	0x00000020
#define	PDB_CACHE	0x00000040
#define	PDB_BITS	0x00000080
#define	PDB_COLLECT	0x00000100
#define	PDB_PROTECT	0x00000200
#define	PDB_EXTRACT	0x00000400
#define	PDB_VP		0x00000800
#define	PDB_PV		0x00001000
#define	PDB_PARANOIA	0x00002000
#define	PDB_WIRING	0x00004000
#define	PDB_PMAP	0x00008000
#define	PDB_STEAL	0x00010000
#define	PDB_PHYS	0x00020000
#define	PDB_POOL	0x00040000
int pmapdebug = 0
/*	| PDB_INIT */
/*	| PDB_FOLLOW */
/*	| PDB_VP */
/*	| PDB_PV */
/*	| PDB_ENTER */
/*	| PDB_REMOVE */
/*	| PDB_STEAL */
/*	| PDB_PROTECT */
/*	| PDB_PHYS */
	;
#else
#define	DPRINTF(l,s)	/* */
#endif

paddr_t physical_steal, physical_end;

int		pmap_hptsize = 16 * PAGE_SIZE;	/* patchable */
vaddr_t		pmap_hpt;

struct pmap	kernel_pmap_store;
int		hppa_sid_max = HPPA_SID_MAX;
struct pool	pmap_pmap_pool;
struct pool	pmap_pv_pool;
int		pmap_pvlowat = 252;
int 		pmap_initialized;

u_int	hppa_prot[8];

#define	pmap_sid(pmap, va) \
	(((va & 0xc0000000) != 0xc0000000)? pmap->pmap_space : HPPA_SID_KERNEL)

static inline int
pmap_pvh_attrs(pt_entry_t pte)
{
	int attrs = 0;
	if (pte & PTE_PROT(TLB_DIRTY))
		attrs |= PG_PMAP_MOD;
	if ((pte & PTE_PROT(TLB_REFTRAP)) == 0)
		attrs |= PG_PMAP_REF;
	return attrs;
}

struct vm_page	*pmap_pagealloc(struct uvm_object *obj, voff_t off);
void		 pmap_pte_flush(struct pmap *pmap, vaddr_t va, pt_entry_t pte);
#ifdef DDB
void		 pmap_dump_table(pa_space_t space, vaddr_t sva);
void		 pmap_dump_pv(paddr_t pa);
#endif
int		 pmap_check_alias(struct vm_page *pg, vaddr_t va,
		    pt_entry_t pte);

#define	IS_IOPAGE(pa)	((pa) >= HPPA_IOBEGIN)

static inline void
pmap_lock(struct pmap *pmap)
{
	if (pmap != pmap_kernel())
		mtx_enter(&pmap->pm_mtx);
}

static inline void
pmap_unlock(struct pmap *pmap)
{
	if (pmap != pmap_kernel())
		mtx_leave(&pmap->pm_mtx);
}

struct vm_page *
pmap_pagealloc(struct uvm_object *obj, voff_t off)
{
	struct vm_page *pg;

	if ((pg = uvm_pagealloc(obj, off, NULL,
	    UVM_PGA_USERESERVE | UVM_PGA_ZERO)) == NULL)
		printf("pmap_pagealloc fail\n");

	return (pg);
}

#ifdef USE_HPT
/*
 * This hash function is the one used by the hardware TLB walker on the 7100LC.
 */
static __inline struct vp_entry *
pmap_hash(struct pmap *pmap, vaddr_t va)
{
	return (struct vp_entry *)(pmap_hpt +
	    (((va >> 8) ^ (pmap->pm_space << 9)) & (pmap_hptsize - 1)));
}

static __inline u_int32_t
pmap_vtag(struct pmap *pmap, vaddr_t va)
{
	return (0x80000000 | (pmap->pm_space & 0xffff) |
	    ((va >> 1) & 0x7fff0000));
}
#endif

static __inline void
pmap_sdir_set(pa_space_t space, volatile u_int32_t *pd)
{
	volatile u_int32_t *vtop;

	mfctl(CR_VTOP, vtop);
#ifdef PMAPDEBUG
	if (!vtop)
		panic("pmap_sdir_set: zero vtop");
#endif
	vtop[space] = (u_int32_t)pd;
}

static __inline u_int32_t *
pmap_sdir_get(pa_space_t space)
{
	u_int32_t *vtop;

	mfctl(CR_VTOP, vtop);
	return ((u_int32_t *)vtop[space]);
}

static __inline volatile pt_entry_t *
pmap_pde_get(volatile u_int32_t *pd, vaddr_t va)
{
	return ((pt_entry_t *)pd[va >> 22]);
}

static __inline void
pmap_pde_set(struct pmap *pm, vaddr_t va, paddr_t ptp)
{
#ifdef PMAPDEBUG
	if (ptp & PGOFSET)
		panic("pmap_pde_set, unaligned ptp 0x%lx", ptp);
#endif
	DPRINTF(PDB_FOLLOW|PDB_VP,
	    ("pmap_pde_set(%p, 0x%lx, 0x%lx)\n", pm, va, ptp));

	pm->pm_pdir[va >> 22] = ptp;
}

static __inline pt_entry_t *
pmap_pde_alloc(struct pmap *pm, vaddr_t va, struct vm_page **pdep)
{
	struct vm_page *pg;
	volatile pt_entry_t *pde;
	paddr_t pa;

	DPRINTF(PDB_FOLLOW|PDB_VP,
	    ("pmap_pde_alloc(%p, 0x%lx, %p)\n", pm, va, pdep));

	pmap_unlock(pm);
	pg = pmap_pagealloc(&pm->pm_obj, va);
	pmap_lock(pm);
	if (pg == NULL)
		return (NULL);
	pde = pmap_pde_get(pm->pm_pdir, va);
	if (pde) {
		pmap_unlock(pm);
		uvm_pagefree(pg);
		pmap_lock(pm);
		return (pt_entry_t *)pde;
	}

	pa = VM_PAGE_TO_PHYS(pg);

	DPRINTF(PDB_FOLLOW|PDB_VP, ("pmap_pde_alloc: pde %lx\n", pa));

	atomic_clearbits_int(&pg->pg_flags, PG_BUSY);
	pg->wire_count = 1;		/* no mappings yet */
	pmap_pde_set(pm, va, pa);
	pm->pm_stats.resident_count++;	/* count PTP as resident */
	pm->pm_ptphint = pg;
	if (pdep)
		*pdep = pg;
	return ((pt_entry_t *)pa);
}

static __inline struct vm_page *
pmap_pde_ptp(struct pmap *pm, volatile pt_entry_t *pde)
{
	paddr_t pa = (paddr_t)pde;

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pde_ptp(%p, %p)\n", pm, pde));

	if (pm->pm_ptphint && VM_PAGE_TO_PHYS(pm->pm_ptphint) == pa)
		return (pm->pm_ptphint);

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pde_ptp: lookup 0x%lx\n", pa));

	return (PHYS_TO_VM_PAGE(pa));
}

static __inline void
pmap_pde_release(struct pmap *pmap, vaddr_t va, struct vm_page *ptp)
{
	paddr_t pa;

	DPRINTF(PDB_FOLLOW|PDB_PV,
	    ("pmap_pde_release(%p, 0x%lx, %p)\n", pmap, va, ptp));

	if (pmap != pmap_kernel() && --ptp->wire_count <= 1) {
		DPRINTF(PDB_FOLLOW|PDB_PV,
		    ("pmap_pde_release: disposing ptp %p\n", ptp));
		
		pmap_pde_set(pmap, va, 0);
		pmap->pm_stats.resident_count--;
		if (pmap->pm_ptphint == ptp) {
			pmap->pm_ptphint = RBT_ROOT(uvm_objtree,
			    &pmap->pm_obj.memt);
		}
		ptp->wire_count = 0;
#ifdef DIAGNOSTIC
		if (ptp->pg_flags & PG_BUSY)
			panic("pmap_pde_release: busy page table page");
#endif
		pa = VM_PAGE_TO_PHYS(ptp);
		pdcache(HPPA_SID_KERNEL, pa, PAGE_SIZE);
		pdtlb(HPPA_SID_KERNEL, pa);
		uvm_pagefree(ptp);
	}
}

static __inline pt_entry_t
pmap_pte_get(volatile pt_entry_t *pde, vaddr_t va)
{
	return (pde[(va >> 12) & 0x3ff]);
}

static __inline void
pmap_pte_set(volatile pt_entry_t *pde, vaddr_t va, pt_entry_t pte)
{
	DPRINTF(PDB_FOLLOW|PDB_VP, ("pmap_pte_set(%p, 0x%lx, 0x%x)\n",
	    pde, va, pte));

#ifdef PMAPDEBUG
	if (!pde)
		panic("pmap_pte_set: zero pde");

	if ((paddr_t)pde & PGOFSET)
		panic("pmap_pte_set, unaligned pde %p", pde);
#endif

	pde[(va >> 12) & 0x3ff] = pte;
}

void
pmap_pte_flush(struct pmap *pmap, vaddr_t va, pt_entry_t pte)
{
	fdcache(pmap->pm_space, va, PAGE_SIZE);
	if (pte & PTE_PROT(TLB_EXECUTE)) {
		ficache(pmap->pm_space, va, PAGE_SIZE);
		pdtlb(pmap->pm_space, va);
		pitlb(pmap->pm_space, va);
	} else
		pdtlb(pmap->pm_space, va);
#ifdef USE_HPT
	if (pmap_hpt) {
		struct vp_entry *hpt;
		hpt = pmap_hash(pmap, va);
		if (hpt->vp_tag == pmap_vtag(pmap, va))
			hpt->vp_tag = 0xffff;
	}
#endif
}

static __inline pt_entry_t
pmap_vp_find(struct pmap *pm, vaddr_t va)
{
	volatile pt_entry_t *pde;

	if (!(pde = pmap_pde_get(pm->pm_pdir, va)))
		return (0);

	return (pmap_pte_get(pde, va));
}

#ifdef DDB
void
pmap_dump_table(pa_space_t space, vaddr_t sva)
{
	pa_space_t sp;

	for (sp = 0; sp <= hppa_sid_max; sp++) {
		volatile pt_entry_t *pde;
		pt_entry_t pte;
		vaddr_t va, pdemask;
		u_int32_t *pd;

		if (((int)space >= 0 && sp != space) ||
		    !(pd = pmap_sdir_get(sp)))
			continue;

		for (pdemask = 1, va = sva ? sva : 0;
		    va < 0xfffff000; va += PAGE_SIZE) {
			if (pdemask != (va & PDE_MASK)) {
				pdemask = va & PDE_MASK;
				if (!(pde = pmap_pde_get(pd, va))) {
					va = pdemask + (~PDE_MASK + 1);
					va -= PAGE_SIZE;
					continue;
				}
				printf("%x:%8p:\n", sp, pde);
			}

			if (!(pte = pmap_pte_get(pde, va)))
				continue;

			printf("0x%08lx-0x%08x:%b\n", va, pte & ~PAGE_MASK,
			    TLB_PROT(pte & PAGE_MASK), TLB_BITS);
		}
	}
}

void
pmap_dump_pv(paddr_t pa)
{
	struct vm_page *pg;
	struct pv_entry *pve;

	pg = PHYS_TO_VM_PAGE(pa);
	if (pg != NULL) {
		for (pve = pg->mdpage.pvh_list; pve; pve = pve->pv_next)
			printf("%x:%lx\n", pve->pv_pmap->pm_space, pve->pv_va);
	}
}
#endif

int
pmap_check_alias(struct vm_page *pg, vaddr_t va, pt_entry_t pte)
{
	struct pv_entry *pve;
	int ret = 0;

	/* check for non-equ aliased mappings */
	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pve = pg->mdpage.pvh_list; pve; pve = pve->pv_next) {
		pte |= pmap_vp_find(pve->pv_pmap, pve->pv_va);
		if ((va & HPPA_PGAOFF) != (pve->pv_va & HPPA_PGAOFF) &&
		    (pte & PTE_PROT(TLB_GATEWAY)) == 0 &&
		    (pte & PTE_PROT(TLB_WRITE))) {
#ifdef PMAPDEBUG
			printf("pmap_check_alias: "
			    "aliased writable mapping 0x%x:0x%lx\n",
			    pve->pv_pmap->pm_space, pve->pv_va);
#endif
			ret++;
		}
	}
	mtx_leave(&pg->mdpage.pvh_mtx);

	return (ret);
}

static __inline struct pv_entry *
pmap_pv_alloc(void)
{
	struct pv_entry *pv;

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pv_alloc()\n"));

	pv = pool_get(&pmap_pv_pool, PR_NOWAIT);

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pv_alloc: %p\n", pv));

	return (pv);
}

static __inline void
pmap_pv_free(struct pv_entry *pv)
{
	if (pv->pv_ptp)
		pmap_pde_release(pv->pv_pmap, pv->pv_va, pv->pv_ptp);

	pool_put(&pmap_pv_pool, pv);
}

static __inline void
pmap_pv_enter(struct vm_page *pg, struct pv_entry *pve, struct pmap *pm,
    vaddr_t va, struct vm_page *pdep)
{
	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_pv_enter(%p, %p, %p, 0x%lx, %p)\n",
	    pg, pve, pm, va, pdep));
	pve->pv_pmap = pm;
	pve->pv_va = va;
	pve->pv_ptp = pdep;
	mtx_enter(&pg->mdpage.pvh_mtx);
	pve->pv_next = pg->mdpage.pvh_list;
	pg->mdpage.pvh_list = pve;
	mtx_leave(&pg->mdpage.pvh_mtx);
}

static __inline struct pv_entry *
pmap_pv_remove(struct vm_page *pg, struct pmap *pmap, vaddr_t va)
{
	struct pv_entry **pve, *pv;

	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pv = *(pve = &pg->mdpage.pvh_list);
	    pv; pv = *(pve = &(*pve)->pv_next))
		if (pv->pv_pmap == pmap && pv->pv_va == va) {
			*pve = pv->pv_next;
			break;
		}
	mtx_leave(&pg->mdpage.pvh_mtx);
	return (pv);
}

void
pmap_bootstrap(vaddr_t vstart)
{
	extern int resvphysmem, etext, __rodata_end, __data_start;
	extern u_int *ie_mem;
	extern paddr_t hppa_vtop;
	vaddr_t va, addr = round_page(vstart), eaddr;
	vsize_t size;
	struct pmap *kpm;
	int npdes, nkpdes;

	DPRINTF(PDB_FOLLOW|PDB_INIT, ("pmap_bootstrap(0x%lx)\n", vstart));

	uvm_setpagesize();

	hppa_prot[PROT_NONE]  = TLB_AR_NA;
	hppa_prot[PROT_READ]  = TLB_AR_R;
	hppa_prot[PROT_WRITE] = TLB_AR_RW;
	hppa_prot[PROT_READ | PROT_WRITE] = TLB_AR_RW;
	hppa_prot[PROT_EXEC]  = TLB_AR_X;
	hppa_prot[PROT_READ | PROT_EXEC] = TLB_AR_RX;
	hppa_prot[PROT_WRITE | PROT_EXEC] = TLB_AR_RWX;
	hppa_prot[PROT_READ | PROT_WRITE | PROT_EXEC] = TLB_AR_RWX;

	/*
	 * Initialize kernel pmap
	 */
	kpm = &kernel_pmap_store;
	bzero(kpm, sizeof(*kpm));
	uvm_obj_init(&kpm->pm_obj, &pmap_pager, 1);
	kpm->pm_space = HPPA_SID_KERNEL;
	kpm->pm_pid = HPPA_PID_KERNEL;
	kpm->pm_pdir_pg = NULL;
	kpm->pm_pdir = (u_int32_t *)addr;
	bzero((void *)addr, PAGE_SIZE);
	fdcache(HPPA_SID_KERNEL, addr, PAGE_SIZE);
	addr += PAGE_SIZE;

	/*
	 * Allocate various tables and structures.
	 */

	mtctl(addr, CR_VTOP);
	hppa_vtop = addr;
	size = round_page((hppa_sid_max + 1) * 4);
	bzero((void *)addr, size);
	fdcache(HPPA_SID_KERNEL, addr, size);
	DPRINTF(PDB_INIT, ("vtop: 0x%lx @ 0x%lx\n", size, addr));
	addr += size;
	pmap_sdir_set(HPPA_SID_KERNEL, kpm->pm_pdir);

	ie_mem = (u_int *)addr;
	addr += 0x8000;

#ifdef USE_HPT
	if (pmap_hptsize) {
		struct vp_entry *hptp;
		int i, error;

		/* must be aligned to the size XXX */
		if (addr & (pmap_hptsize - 1))
			addr += pmap_hptsize;
		addr &= ~(pmap_hptsize - 1);

		bzero((void *)addr, pmap_hptsize);
		for (hptp = (struct vp_entry *)addr, i = pmap_hptsize / 16; i--;)
			hptp[i].vp_tag = 0xffff;
		pmap_hpt = addr;
		addr += pmap_hptsize;

		DPRINTF(PDB_INIT, ("hpt_table: 0x%x @ %p\n",
		    pmap_hptsize, addr));

		if ((error = (cpu_hpt_init)(pmap_hpt, pmap_hptsize)) < 0) {
			printf("WARNING: HPT init error %d -- DISABLED\n",
			    error);
			pmap_hpt = 0;
		} else
			DPRINTF(PDB_INIT,
			    ("HPT: installed for %d entries @ 0x%x\n",
			    pmap_hptsize / sizeof(struct vp_entry), addr));
	}
#endif

	/* XXX PCXS needs this inserted into an IBTLB */
	/*	and can block-map the whole phys w/ another */

	/*
	 * We use separate mappings for the first 4MB of kernel text
	 * and whetever is left to avoid the mapping to cover kernel
	 * data.
	 */
	for (va = 0; va < (vaddr_t)&etext; va += size) {
		size = (vaddr_t)&etext - va;
		if (size > 4 * 1024 * 1024)
			size = 4 * 1024 * 1024;

		if (btlb_insert(HPPA_SID_KERNEL, va, va, &size,
		    pmap_sid2pid(HPPA_SID_KERNEL) |
		    pmap_prot(pmap_kernel(), PROT_READ | PROT_EXEC)) < 0) {
			printf("WARNING: cannot block map kernel text\n");
			break;
		}
	}

	if (&__rodata_end < &__data_start) {
		physical_steal = (vaddr_t)&__rodata_end;
		physical_end = (vaddr_t)&__data_start;
		DPRINTF(PDB_INIT, ("physpool: 0x%lx @ 0x%lx\n",
		    physical_end - physical_steal, physical_steal));
	}

	/* kernel virtual is the last gig of the moohicans */
	nkpdes = physmem >> 14;	/* at least 16/gig for kmem */
	if (nkpdes < 4)
		nkpdes = 4;		/* ... but no less than four */
	nkpdes += HPPA_IOLEN / PDE_SIZE; /* ... and io space too */
	npdes = nkpdes + (physmem + atop(PDE_SIZE) - 1) / atop(PDE_SIZE);

	/* map the pdes */
	for (va = 0; npdes--; va += PDE_SIZE, addr += PAGE_SIZE) {

		/* last nkpdes are for the kernel virtual */
		if (npdes == nkpdes - 1)
			va = SYSCALLGATE;
		if (npdes == HPPA_IOLEN / PDE_SIZE - 1)
			va = HPPA_IOBEGIN;
		/* now map the pde for the physmem */
		bzero((void *)addr, PAGE_SIZE);
		fdcache(HPPA_SID_KERNEL, addr, PAGE_SIZE);
		DPRINTF(PDB_INIT|PDB_VP,
		    ("pde premap 0x%lx 0x%lx\n", va, addr));
		pmap_pde_set(kpm, va, addr);
		kpm->pm_stats.resident_count++; /* count PTP as resident */
	}

	resvphysmem = atop(addr);
	eaddr = physmem - atop(round_page(MSGBUFSIZE));
	DPRINTF(PDB_INIT, ("physmem: 0x%x - 0x%lx\n", resvphysmem, eaddr));
	uvm_page_physload(0, eaddr, resvphysmem, eaddr, 0);

	/* TODO optimize/inline the kenter */
	for (va = 0; va < ptoa(physmem); va += PAGE_SIZE) {
		extern struct user *proc0paddr;
		vm_prot_t prot = PROT_READ | PROT_WRITE;

		if (va < (vaddr_t)&etext)
			prot = PROT_READ | PROT_EXEC;
		else if (va < (vaddr_t)&__rodata_end)
			prot = PROT_READ;
		else if (va == (vaddr_t)proc0paddr + USPACE)
			prot = PROT_NONE;

		pmap_kenter_pa(va, va, prot);
	}

	DPRINTF(PDB_INIT, ("bootstrap: mapped %p - 0x%lx\n", &etext, va));
}

void
pmap_init(void)
{
	DPRINTF(PDB_FOLLOW|PDB_INIT, ("pmap_init()\n"));

	pool_init(&pmap_pmap_pool, sizeof(struct pmap), 0, IPL_NONE, 0,
	    "pmappl", NULL);
	pool_init(&pmap_pv_pool, sizeof(struct pv_entry), 0, IPL_VM, 0,
	    "pmappv", NULL);
	pool_setlowat(&pmap_pv_pool, pmap_pvlowat);
	pool_sethiwat(&pmap_pv_pool, pmap_pvlowat * 32);

	pmap_initialized = 1;

	/*
	 * map SysCall gateways page once for everybody
	 * NB: we'll have to remap the phys memory
	 *     if we have any at SYSCALLGATE address (;
	 */
	{
		volatile pt_entry_t *pde;

		if (!(pde = pmap_pde_get(pmap_kernel()->pm_pdir, SYSCALLGATE)) &&
		    !(pde = pmap_pde_alloc(pmap_kernel(), SYSCALLGATE, NULL)))
			panic("pmap_init: cannot allocate pde");

		pmap_pte_set(pde, SYSCALLGATE, (paddr_t)&gateway_page |
		    PTE_PROT(TLB_GATE_PROT));
	}

	DPRINTF(PDB_FOLLOW|PDB_INIT, ("pmap_init(): done\n"));
}

void
pmap_virtual_space(vaddr_t *startp, vaddr_t *endp)
{
	*startp = SYSCALLGATE + PAGE_SIZE;
	*endp = VM_MAX_KERNEL_ADDRESS;
}

struct pmap *
pmap_create(void)
{
	struct pmap *pmap;
	pa_space_t space;

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_create()\n"));

	pmap = pool_get(&pmap_pmap_pool, PR_WAITOK);

	mtx_init(&pmap->pm_mtx, IPL_VM);

	uvm_obj_init(&pmap->pm_obj, &pmap_pager, 1);

	for (space = 1 + arc4random_uniform(hppa_sid_max);
	    pmap_sdir_get(space); space = (space + 1) % hppa_sid_max);

	if ((pmap->pm_pdir_pg = pmap_pagealloc(NULL, 0)) == NULL)
		panic("pmap_create: no pages");
	pmap->pm_ptphint = NULL;
	pmap->pm_pdir = (u_int32_t *)VM_PAGE_TO_PHYS(pmap->pm_pdir_pg);
	pmap_sdir_set(space, pmap->pm_pdir);

	pmap->pm_space = space;
	pmap->pm_pid = (space + 1) << 1;

	pmap->pm_stats.resident_count = 1;
	pmap->pm_stats.wired_count = 0;

	return (pmap);
}

void
pmap_destroy(struct pmap *pmap)
{
	paddr_t pa;
	int refs;

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_destroy(%p)\n", pmap));

	refs = atomic_dec_int_nv(&pmap->pm_obj.uo_refs);
	if (refs > 0)
		return;

	KASSERT(RBT_EMPTY(uvm_objtree, &pmap->pm_obj.memt));

	pmap_sdir_set(pmap->pm_space, 0);

	pa = VM_PAGE_TO_PHYS(pmap->pm_pdir_pg);
	pdcache(HPPA_SID_KERNEL, pa, PAGE_SIZE);
	pdtlb(HPPA_SID_KERNEL, pa);
	uvm_pagefree(pmap->pm_pdir_pg);

	pmap->pm_pdir_pg = NULL;
	pool_put(&pmap_pmap_pool, pmap);
}

/*
 * Add a reference to the specified pmap.
 */
void
pmap_reference(struct pmap *pmap)
{
	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_reference(%p)\n", pmap));

	atomic_inc_int(&pmap->pm_obj.uo_refs);
}

int
pmap_enter(struct pmap *pmap, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	volatile pt_entry_t *pde;
	pt_entry_t pte;
	struct vm_page *pg, *ptp = NULL;
	struct pv_entry *pve = NULL;
	boolean_t wired = (flags & PMAP_WIRED) != 0;

	DPRINTF(PDB_FOLLOW|PDB_ENTER,
	    ("pmap_enter(%p, 0x%lx, 0x%lx, 0x%x, 0x%x)\n",
	    pmap, va, pa, prot, flags));
	pmap_lock(pmap);

	if (!(pde = pmap_pde_get(pmap->pm_pdir, va)) &&
	    !(pde = pmap_pde_alloc(pmap, va, &ptp))) {
		if (flags & PMAP_CANFAIL) {
			pmap_unlock(pmap);
			return (ENOMEM);
		}
		panic("pmap_enter: cannot allocate pde");
	}

	if (!ptp)
		ptp = pmap_pde_ptp(pmap, pde);

	if ((pte = pmap_pte_get(pde, va))) {
		DPRINTF(PDB_ENTER,
		    ("pmap_enter: remapping 0x%x -> 0x%lx\n", pte, pa));

		pmap_pte_flush(pmap, va, pte);
		if (wired && !(pte & PTE_PROT(TLB_WIRED)))
			pmap->pm_stats.wired_count++;
		else if (!wired && (pte & PTE_PROT(TLB_WIRED)))
			pmap->pm_stats.wired_count--;

		if (PTE_PAGE(pte) == pa) {
			DPRINTF(PDB_FOLLOW|PDB_ENTER,
			    ("pmap_enter: same page\n"));
			goto enter;
		}

		pg = PHYS_TO_VM_PAGE(PTE_PAGE(pte));
		if (pg != NULL) {
			pve = pmap_pv_remove(pg, pmap, va);
			atomic_setbits_int(&pg->pg_flags, pmap_pvh_attrs(pte));
		}
	} else {
		DPRINTF(PDB_ENTER,
		    ("pmap_enter: new mapping 0x%lx -> 0x%lx\n", va, pa));
		pte = PTE_PROT(TLB_REFTRAP);
		pmap->pm_stats.resident_count++;
		if (wired)
			pmap->pm_stats.wired_count++;
		if (ptp)
			ptp->wire_count++;
	}

	if (pmap_initialized && (pg = PHYS_TO_VM_PAGE(PTE_PAGE(pa)))) {
		if (!pve && !(pve = pmap_pv_alloc())) {
			if (flags & PMAP_CANFAIL) {
				pmap_unlock(pmap);
				return (ENOMEM);
			}
			panic("pmap_enter: no pv entries available");
		}
		pte |= PTE_PROT(pmap_prot(pmap, prot));
		if (pmap_check_alias(pg, va, pte))
			pmap_page_remove(pg);
		pmap_pv_enter(pg, pve, pmap, va, ptp);
	} else if (pve)
		pmap_pv_free(pve);

enter:
	/* preserve old ref & mod */
	pte = pa | PTE_PROT(pmap_prot(pmap, prot)) |
	    (pte & PTE_PROT(TLB_UNCACHABLE|TLB_DIRTY|TLB_REFTRAP));
	if (IS_IOPAGE(pa))
		pte |= PTE_PROT(TLB_UNCACHABLE);
	if (wired)
		pte |= PTE_PROT(TLB_WIRED);
	pmap_pte_set(pde, va, pte);

	DPRINTF(PDB_FOLLOW|PDB_ENTER, ("pmap_enter: leaving\n"));
	pmap_unlock(pmap);

	return (0);
}

void
pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva)
{
	struct pv_entry *pve;
	volatile pt_entry_t *pde;
	pt_entry_t pte;
	struct vm_page *pg, *ptp;
	vaddr_t pdemask;
	int batch;

	DPRINTF(PDB_FOLLOW|PDB_REMOVE,
	    ("pmap_remove(%p, 0x%lx, 0x%lx)\n", pmap, sva, eva));
	pmap_lock(pmap);

	for (batch = 0; sva < eva; sva += PAGE_SIZE) {
		pdemask = sva & PDE_MASK;
		if (!(pde = pmap_pde_get(pmap->pm_pdir, sva))) {
			sva = pdemask + (~PDE_MASK + 1) - PAGE_SIZE;
			continue;
		}
		if (pdemask == sva) {
			if (sva + (~PDE_MASK + 1) <= eva)
				batch = 1;
			else
				batch = 0;
		}

		if ((pte = pmap_pte_get(pde, sva))) {

			/* TODO measure here the speed tradeoff
			 * for flushing whole 4M vs per-page
			 * in case of non-complete pde fill
			 */
			pmap_pte_flush(pmap, sva, pte);
			if (pte & PTE_PROT(TLB_WIRED))
				pmap->pm_stats.wired_count--;
			pmap->pm_stats.resident_count--;

			/* iff properly accounted pde will be dropped anyway */
			if (!batch)
				pmap_pte_set(pde, sva, 0);

			if (pmap_initialized &&
			    (pg = PHYS_TO_VM_PAGE(PTE_PAGE(pte)))) {
				atomic_setbits_int(&pg->pg_flags,
				    pmap_pvh_attrs(pte));
				if ((pve = pmap_pv_remove(pg, pmap, sva)))
					pmap_pv_free(pve);
			} else {
				if (IS_IOPAGE(PTE_PAGE(pte))) {
					ptp = pmap_pde_ptp(pmap, pde);
					if (ptp != NULL)
						pmap_pde_release(pmap, sva, ptp);
				}
			}
		}
	}

	DPRINTF(PDB_FOLLOW|PDB_REMOVE, ("pmap_remove: leaving\n"));
	pmap_unlock(pmap);
}

void
pmap_page_write_protect(struct vm_page *pg)
{
	struct pv_entry *pve;
	int attrs;

	DPRINTF(PDB_FOLLOW|PDB_BITS, ("pmap_page_write_protect(%p)\n", pg));

	attrs = 0;
	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pve = pg->mdpage.pvh_list; pve; pve = pve->pv_next) {
		struct pmap *pmap = pve->pv_pmap;
		vaddr_t va = pve->pv_va;
		volatile pt_entry_t *pde;
		pt_entry_t opte, pte;

		if ((pde = pmap_pde_get(pmap->pm_pdir, va))) {
			opte = pte = pmap_pte_get(pde, va);
			if (pte & TLB_GATEWAY)
				continue;
			pte &= ~TLB_WRITE;
			attrs |= pmap_pvh_attrs(pte);

			if (opte != pte) {
				pmap_pte_flush(pmap, va, opte);
				pmap_pte_set(pde, va, pte);
			}
		}
	}
	mtx_leave(&pg->mdpage.pvh_mtx);
	if (attrs != (PG_PMAP_REF | PG_PMAP_MOD))
		atomic_clearbits_int(&pg->pg_flags,
		    attrs ^(PG_PMAP_REF | PG_PMAP_MOD));
	if (attrs != 0)
		atomic_setbits_int(&pg->pg_flags, attrs);
}

void
pmap_write_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	struct vm_page *pg;
	volatile pt_entry_t *pde;
	pt_entry_t pte;
	u_int tlbprot, pdemask;

	DPRINTF(PDB_FOLLOW|PDB_PMAP,
	    ("pmap_write_protect(%p, %lx, %lx, %x)\n", pmap, sva, eva, prot));
	pmap_lock(pmap);

	sva = trunc_page(sva);
	tlbprot = PTE_PROT(pmap_prot(pmap, prot));

	for (pdemask = 1; sva < eva; sva += PAGE_SIZE) {
		if (pdemask != (sva & PDE_MASK)) {
			pdemask = sva & PDE_MASK;
			if (!(pde = pmap_pde_get(pmap->pm_pdir, sva))) {
				sva = pdemask + (~PDE_MASK + 1) - PAGE_SIZE;
				continue;
			}
		}
		if ((pte = pmap_pte_get(pde, sva))) {

			DPRINTF(PDB_PMAP,
			    ("pmap_write_protect: va=0x%lx pte=0x%x\n",
			    sva,  pte));
			/*
			 * Determine if mapping is changing.
			 * If not, nothing to do.
			 */
			if ((pte & PTE_PROT(TLB_AR_MASK)) == tlbprot)
				continue;

			pg = PHYS_TO_VM_PAGE(PTE_PAGE(pte));
			if (pg != NULL) {
				atomic_setbits_int(&pg->pg_flags,
				    pmap_pvh_attrs(pte));
			}

			pmap_pte_flush(pmap, sva, pte);
			pte &= ~PTE_PROT(TLB_AR_MASK);
			pte |= tlbprot;
			pmap_pte_set(pde, sva, pte);
		}
	}

	pmap_unlock(pmap);
}

void
pmap_page_remove(struct vm_page *pg)
{
	struct pv_entry *pve;

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_page_remove(%p)\n", pg));

	if (pg->mdpage.pvh_list == NULL)
		return;

	mtx_enter(&pg->mdpage.pvh_mtx);
	while ((pve = pg->mdpage.pvh_list)) {
		struct pmap *pmap = pve->pv_pmap;
		vaddr_t va = pve->pv_va;
		volatile pt_entry_t *pde;
		pt_entry_t pte;
		u_int attrs;

		pg->mdpage.pvh_list = pve->pv_next;
		pmap_reference(pmap);
		mtx_leave(&pg->mdpage.pvh_mtx);

		pmap_lock(pmap);
		pde = pmap_pde_get(pmap->pm_pdir, va);
		pte = pmap_pte_get(pde, va);
		attrs = pmap_pvh_attrs(pte);

		pmap_pte_flush(pmap, va, pte);
		if (pte & PTE_PROT(TLB_WIRED))
			pmap->pm_stats.wired_count--;
		pmap->pm_stats.resident_count--;

		pmap_pte_set(pde, va, 0);
		pmap_unlock(pmap);

		pmap_destroy(pmap);
		pmap_pv_free(pve);
		atomic_setbits_int(&pg->pg_flags, attrs);
		mtx_enter(&pg->mdpage.pvh_mtx);
	}
	mtx_leave(&pg->mdpage.pvh_mtx);

	DPRINTF(PDB_FOLLOW|PDB_PV, ("pmap_page_remove: leaving\n"));
}

void
pmap_unwire(struct pmap *pmap, vaddr_t	va)
{
	volatile pt_entry_t *pde;
	pt_entry_t pte = 0;

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_unwire(%p, 0x%lx)\n", pmap, va));
	pmap_lock(pmap);

	if ((pde = pmap_pde_get(pmap->pm_pdir, va))) {
		pte = pmap_pte_get(pde, va);

		if (pte & PTE_PROT(TLB_WIRED)) {
			pte &= ~PTE_PROT(TLB_WIRED);
			pmap->pm_stats.wired_count--;
			pmap_pte_set(pde, va, pte);
		}
	}

	DPRINTF(PDB_FOLLOW|PDB_PMAP, ("pmap_unwire: leaving\n"));
	pmap_unlock(pmap);

#ifdef DIAGNOSTIC
	if (!pte)
		panic("pmap_unwire: invalid va 0x%lx", va);
#endif
}

boolean_t
pmap_changebit(struct vm_page *pg, u_int set, u_int clear)
{
	struct pv_entry *pve;
	pt_entry_t res;
	int attrs;

	DPRINTF(PDB_FOLLOW|PDB_BITS,
	    ("pmap_changebit(%p, %x, %x)\n", pg, set, clear));

	res = 0;
	attrs = 0;
	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pve = pg->mdpage.pvh_list; pve; pve = pve->pv_next) {
		struct pmap *pmap = pve->pv_pmap;
		vaddr_t va = pve->pv_va;
		volatile pt_entry_t *pde;
		pt_entry_t opte, pte;

		if ((pde = pmap_pde_get(pmap->pm_pdir, va))) {
			opte = pte = pmap_pte_get(pde, va);
#ifdef PMAPDEBUG
			if (!pte) {
				printf("pmap_changebit: zero pte for 0x%lx\n",
				    va);
				continue;
			}
#endif
			pte &= ~clear;
			pte |= set;
			attrs |= pmap_pvh_attrs(pte);
			res |= pmap_pvh_attrs(opte);

			if (opte != pte) {
				pmap_pte_flush(pmap, va, opte);
				pmap_pte_set(pde, va, pte);
			}
		}
	}
	mtx_leave(&pg->mdpage.pvh_mtx);
	if (attrs != (PG_PMAP_REF | PG_PMAP_MOD))
		atomic_clearbits_int(&pg->pg_flags,
		    attrs ^(PG_PMAP_REF | PG_PMAP_MOD));
	if (attrs != 0)
		atomic_setbits_int(&pg->pg_flags, attrs);

	return ((res & (clear | set)) != 0);
}

boolean_t
pmap_testbit(struct vm_page *pg, int bit)
{
	struct pv_entry *pve;
	pt_entry_t pte;
	boolean_t ret;

	DPRINTF(PDB_FOLLOW|PDB_BITS, ("pmap_testbit(%p, %x)\n", pg, bit));

	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pve = pg->mdpage.pvh_list; !(pg->pg_flags & bit) && pve;
	    pve = pve->pv_next) {
		pte = pmap_vp_find(pve->pv_pmap, pve->pv_va);
		atomic_setbits_int(&pg->pg_flags, pmap_pvh_attrs(pte));
	}
	mtx_leave(&pg->mdpage.pvh_mtx);
	ret = ((pg->pg_flags & bit) != 0);

	return ret;
}

boolean_t
pmap_extract(struct pmap *pmap, vaddr_t va, paddr_t *pap)
{
	pt_entry_t pte;

	DPRINTF(PDB_FOLLOW|PDB_EXTRACT, ("pmap_extract(%p, %lx)\n", pmap, va));

	pmap_lock(pmap);
	pte = pmap_vp_find(pmap, va);
	pmap_unlock(pmap);

	if (pte) {
		if (pap)
			*pap = (pte & ~PGOFSET) | (va & PGOFSET);
		return (TRUE);
	}

	return (FALSE);
}

void
pmap_activate(struct proc *p)
{
	struct pmap *pmap = p->p_vmspace->vm_map.pmap;
	struct pcb *pcb = &p->p_addr->u_pcb;

	pcb->pcb_space = pmap->pm_space;
}

void
pmap_deactivate(struct proc *p)
{

}

static __inline void
pmap_flush_page(struct vm_page *pg, int purge)
{
	struct pv_entry *pve;

	/* purge cache for all possible mappings for the pa */
	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pve = pg->mdpage.pvh_list; pve; pve = pve->pv_next) {
		if (purge)
			pdcache(pve->pv_pmap->pm_space, pve->pv_va, PAGE_SIZE);
		else
			fdcache(pve->pv_pmap->pm_space, pve->pv_va, PAGE_SIZE);
		ficache(pve->pv_pmap->pm_space, pve->pv_va, PAGE_SIZE);
		pdtlb(pve->pv_pmap->pm_space, pve->pv_va);
		pitlb(pve->pv_pmap->pm_space, pve->pv_va);
	}
	mtx_leave(&pg->mdpage.pvh_mtx);
}

void
pmap_zero_page(struct vm_page *pg)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);

	DPRINTF(PDB_FOLLOW|PDB_PHYS, ("pmap_zero_page(%lx)\n", pa));

	pmap_flush_page(pg, 1);
	bzero((void *)pa, PAGE_SIZE);
	fdcache(HPPA_SID_KERNEL, pa, PAGE_SIZE);
	pdtlb(HPPA_SID_KERNEL, pa);
}

void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t spa = VM_PAGE_TO_PHYS(srcpg);
	paddr_t dpa = VM_PAGE_TO_PHYS(dstpg);
	DPRINTF(PDB_FOLLOW|PDB_PHYS, ("pmap_copy_page(%lx, %lx)\n", spa, dpa));

	pmap_flush_page(srcpg, 0);
	pmap_flush_page(dstpg, 1);
	bcopy((void *)spa, (void *)dpa, PAGE_SIZE);
	pdcache(HPPA_SID_KERNEL, spa, PAGE_SIZE);
	fdcache(HPPA_SID_KERNEL, dpa, PAGE_SIZE);
	pdtlb(HPPA_SID_KERNEL, spa);
	pdtlb(HPPA_SID_KERNEL, dpa);
}

void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	volatile pt_entry_t *pde;
	pt_entry_t pte, opte;

	DPRINTF(PDB_FOLLOW|PDB_ENTER,
	    ("pmap_kenter_pa(%lx, %lx, %x)\n", va, pa, prot));

	if (!(pde = pmap_pde_get(pmap_kernel()->pm_pdir, va)) &&
	    !(pde = pmap_pde_alloc(pmap_kernel(), va, NULL)))
		panic("pmap_kenter_pa: cannot allocate pde for va=0x%lx", va);
	opte = pmap_pte_get(pde, va);
	pte = pa | PTE_PROT(TLB_WIRED | TLB_REFTRAP |
	    pmap_prot(pmap_kernel(), prot));
	if (IS_IOPAGE(pa))
		pte |= PTE_PROT(TLB_UNCACHABLE);
	if (opte)
		pmap_pte_flush(pmap_kernel(), va, opte);
	pmap_pte_set(pde, va, pte);
	pmap_kernel()->pm_stats.wired_count++;
	pmap_kernel()->pm_stats.resident_count++;

#ifdef PMAPDEBUG
	{
		struct vm_page *pg;

		if (pmap_initialized && (pg = PHYS_TO_VM_PAGE(PTE_PAGE(pte)))) {
			if (pmap_check_alias(pg, va, pte))
				db_enter();
		}
	}
#endif
	DPRINTF(PDB_FOLLOW|PDB_ENTER, ("pmap_kenter_pa: leaving\n"));
}

void
pmap_kremove(vaddr_t va, vsize_t size)
{
	struct pv_entry *pve;
	vaddr_t eva, pdemask;
	volatile pt_entry_t *pde;
	pt_entry_t pte;
	struct vm_page *pg;

	DPRINTF(PDB_FOLLOW|PDB_REMOVE,
	    ("pmap_kremove(%lx, %lx)\n", va, size));
#ifdef PMAPDEBUG
	if (va < ptoa(physmem)) {
		printf("pmap_kremove(%lx, %lx): unmapping physmem\n", va, size);
		return;
	}
#endif

	for (pdemask = 1, eva = va + size; va < eva; va += PAGE_SIZE) {
		if (pdemask != (va & PDE_MASK)) {
			pdemask = va & PDE_MASK;
			if (!(pde = pmap_pde_get(pmap_kernel()->pm_pdir, va))) {
				va = pdemask + (~PDE_MASK + 1) - PAGE_SIZE;
				continue;
			}
		}
		if (!(pte = pmap_pte_get(pde, va))) {
#ifdef DEBUG
			printf("pmap_kremove: unmapping unmapped 0x%x\n", va);
#endif
			continue;
		}

		pmap_pte_flush(pmap_kernel(), va, pte);
		pmap_pte_set(pde, va, 0);
		if (pmap_initialized && (pg = PHYS_TO_VM_PAGE(PTE_PAGE(pte)))) {
			atomic_setbits_int(&pg->pg_flags, pmap_pvh_attrs(pte));
			/* just in case we have enter/kenter mismatch */
			if ((pve = pmap_pv_remove(pg, pmap_kernel(), va)))
				pmap_pv_free(pve);
		}
	}

	DPRINTF(PDB_FOLLOW|PDB_REMOVE, ("pmap_kremove: leaving\n"));
}

void
pmap_proc_iflush(struct process *pr, vaddr_t va, vsize_t len)
{
	pmap_t pmap = vm_map_pmap(&pr->ps_vmspace->vm_map);

	fdcache(pmap->pm_space, va, len);
	sync_caches();
	ficache(pmap->pm_space, va, len);
	sync_caches();
}

struct vm_page *
pmap_unmap_direct(vaddr_t va)
{
	fdcache(HPPA_SID_KERNEL, va, PAGE_SIZE);
	pdtlb(HPPA_SID_KERNEL, va);
	return (PHYS_TO_VM_PAGE(va));
}
