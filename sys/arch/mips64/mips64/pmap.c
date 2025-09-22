/*	$OpenBSD: pmap.c,v 1.126 2025/06/02 18:49:04 claudio Exp $	*/

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/pool.h>
#ifdef SYSVSHM
#include <sys/shm.h>
#endif
#include <sys/atomic.h>

#include <mips64/cache.h>
#include <mips64/mips_cpu.h>
#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/vmparam.h>

#include <uvm/uvm.h>

extern void mem_zero_page(vaddr_t);

struct pool pmap_pmap_pool;
struct pool pmap_pv_pool;
struct pool pmap_pg_pool;

#define pmap_pv_alloc()		(pv_entry_t)pool_get(&pmap_pv_pool, PR_NOWAIT)
#define pmap_pv_free(pv)	pool_put(&pmap_pv_pool, (pv))

#ifndef PMAP_PV_LOWAT
#define PMAP_PV_LOWAT   16
#endif
int	pmap_pv_lowat = PMAP_PV_LOWAT;

/*
 * Maximum number of pages that can be invalidated from remote TLBs at once.
 * This limit prevents the IPI handler from blocking other high-priority
 * interrupts for too long.
 */
#define SHOOTDOWN_MAX	128

uint	 pmap_alloc_tlbpid(struct proc *);
void	 pmap_do_page_cache(vm_page_t, u_int);
void	 pmap_do_remove(pmap_t, vaddr_t, vaddr_t);
int	 pmap_enter_pv(pmap_t, vaddr_t, vm_page_t, pt_entry_t *);
void	 pmap_remove_pv(pmap_t, vaddr_t, paddr_t);
void	 pmap_page_remove(struct vm_page *);
void	 pmap_page_wrprotect(struct vm_page *, vm_prot_t);
void	*pmap_pg_alloc(struct pool *, int, int *);
void	 pmap_pg_free(struct pool *, void *);

struct pool_allocator pmap_pg_allocator = {
	pmap_pg_alloc, pmap_pg_free
};

#define	pmap_invalidate_kernel_page(va) tlb_flush_addr(va)
#define	pmap_update_kernel_page(va, entry) tlb_update((va), (entry))

void	pmap_invalidate_user_page(pmap_t, vaddr_t);
void	pmap_invalidate_icache(pmap_t, vaddr_t, pt_entry_t);
void	pmap_update_user_page(pmap_t, vaddr_t, pt_entry_t);
#ifdef MULTIPROCESSOR
void	pmap_invalidate_icache_action(void *);
void	pmap_shootdown_range(pmap_t, vaddr_t, vaddr_t, int);
void	pmap_shootdown_range_action(void *);
#else
#define	pmap_shootdown_range(pmap, sva, eva, needisync) \
	do { /* nothing */ } while (0)
#endif

#ifdef PMAPDEBUG
struct {
	int kernel;	/* entering kernel mapping */
	int user;	/* entering user mapping */
	int ptpneeded;	/* needed to allocate a PT page */
	int pwchange;	/* no mapping change, just wiring or protection */
	int wchange;	/* no mapping change, just wiring */
	int mchange;	/* was mapped but mapping to different page */
	int managed;	/* a managed page */
	int firstpv;	/* first mapping for this PA */
	int secondpv;	/* second mapping for this PA */
	int ci;		/* cache inhibited */
	int unmanaged;	/* not a managed page */
	int flushes;	/* cache flushes */
	int cachehit;	/* new entry forced valid entry out */
} enter_stats;
struct {
	int calls;
	int removes;
	int flushes;
	int pidflushes;	/* HW pid stolen */
	int pvfirst;
	int pvsearch;
} remove_stats;

#define PDB_FOLLOW	0x0001
#define PDB_INIT	0x0002
#define PDB_ENTER	0x0004
#define PDB_REMOVE	0x0008
#define PDB_CREATE	0x0010
#define PDB_PTPAGE	0x0020
#define PDB_PVENTRY	0x0040
#define PDB_BITS	0x0080
#define PDB_COLLECT	0x0100
#define PDB_PROTECT	0x0200
#define PDB_TLBPID	0x0400
#define PDB_PARANOIA	0x2000
#define PDB_WIRING	0x4000
#define PDB_PVDUMP	0x8000

#define DPRINTF(flag, printdata)	\
	if (pmapdebug & (flag)) 	\
		printf printdata;

#define stat_count(what)	atomic_inc_int(&(what))
int pmapdebug = PDB_ENTER|PDB_FOLLOW;

#else

#define DPRINTF(flag, printdata)
#define stat_count(what)

#endif	/* PMAPDEBUG */

static struct pmap	kernel_pmap_store
	[(PMAP_SIZEOF(MAXCPUS) + sizeof(struct pmap) - 1)
		/ sizeof(struct pmap)];
struct pmap *const kernel_pmap_ptr = kernel_pmap_store;

vaddr_t	virtual_start;  /* VA of first avail page (after kernel bss)*/
vaddr_t	virtual_end;	/* VA of last avail page (end of kernel AS) */

vaddr_t	pmap_prefer_mask;

static struct pmap_asid_info pmap_asid_info[MAXCPUS];

pt_entry_t	*Sysmap;		/* kernel pte table */
u_int		Sysmapsize;		/* number of pte's in Sysmap */
const vaddr_t	Sysmapbase = VM_MIN_KERNEL_ADDRESS;	/* for libkvm */

pt_entry_t	protection_codes[8];
pt_entry_t	pg_ri;
pt_entry_t	pg_xi;

void
pmap_invalidate_user_page(pmap_t pmap, vaddr_t va)
{
	u_long cpuid = cpu_number();
	u_long asid = pmap->pm_asid[cpuid].pma_asid << PG_ASID_SHIFT;

	if (pmap->pm_asid[cpuid].pma_asidgen ==
	    pmap_asid_info[cpuid].pma_asidgen)
		tlb_flush_addr(va | asid);
}

void
pmap_update_user_page(pmap_t pmap, vaddr_t va, pt_entry_t entry)
{
	u_long cpuid = cpu_number();
	u_long asid = pmap->pm_asid[cpuid].pma_asid << PG_ASID_SHIFT;

	if (pmap->pm_asid[cpuid].pma_asidgen ==
	    pmap_asid_info[cpuid].pma_asidgen)
		tlb_update(va | asid, entry);
}

#ifdef MULTIPROCESSOR

#define PMAP_ASSERT_LOCKED(pm)						\
do {									\
	if ((pm) != pmap_kernel())					\
		MUTEX_ASSERT_LOCKED(&(pm)->pm_mtx);			\
} while (0)

static inline void
pmap_lock(pmap_t pmap)
{
	if (pmap != pmap_kernel())
		mtx_enter(&pmap->pm_mtx);
}

static inline void
pmap_unlock(pmap_t pmap)
{
	if (pmap != pmap_kernel())
		mtx_leave(&pmap->pm_mtx);
}

static inline void
pmap_swlock(pmap_t pmap)
{
	if (pmap != pmap_kernel())
		mtx_enter(&pmap->pm_swmtx);
}

static inline void
pmap_swunlock(pmap_t pmap)
{
	if (pmap != pmap_kernel())
		mtx_leave(&pmap->pm_swmtx);
}

static inline pt_entry_t
pmap_pte_cas(pt_entry_t *pte, pt_entry_t o, pt_entry_t n)
{
#ifdef MIPS_PTE64
	return atomic_cas_ulong((unsigned long *)pte, o, n);
#else
	return atomic_cas_uint((unsigned int *)pte, o, n);
#endif
}

struct pmap_invalidate_icache_arg {
	vaddr_t		va;
	pt_entry_t	entry;
};

void
pmap_invalidate_icache(pmap_t pmap, vaddr_t va, pt_entry_t entry)
{
	struct pmap_invalidate_icache_arg ii_args;
	unsigned long cpumask = 0;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	pmap_swlock(pmap);
	CPU_INFO_FOREACH(cii, ci) {
		if (CPU_IS_RUNNING(ci) &&
		    pmap->pm_asid[ci->ci_cpuid].pma_asidgen != 0)
			cpumask |= 1UL << ci->ci_cpuid;
	}
	pmap_swunlock(pmap);
	if (cpumask != 0) {
		ii_args.va = va;
		ii_args.entry = entry;
		smp_rendezvous_cpus(cpumask, pmap_invalidate_icache_action,
		    &ii_args);
	}
}

void
pmap_invalidate_icache_action(void *arg)
{
	struct cpu_info *ci = curcpu();
	struct pmap_invalidate_icache_arg *ii_args = arg;

	Mips_SyncDCachePage(ci, ii_args->va, pfn_to_pad(ii_args->entry));
	Mips_InvalidateICache(ci, ii_args->va, PAGE_SIZE);
}

struct pmap_shootdown_range_arg {
	pmap_t		pmap;
	vaddr_t		sva;
	vaddr_t		eva;
	int		needisync;
};

void
pmap_shootdown_range(pmap_t pmap, vaddr_t sva, vaddr_t eva, int needisync)
{
	struct pmap_shootdown_range_arg sr_arg;
	struct cpu_info *ci, *self = curcpu();
	CPU_INFO_ITERATOR cii;
	vaddr_t va;
	unsigned long cpumask = 0;

	pmap_swlock(pmap);
	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self)
			continue;
		if (!CPU_IS_RUNNING(ci))
			continue;
		if (pmap != pmap_kernel()) {
			if (pmap->pm_asid[ci->ci_cpuid].pma_asidgen !=
			    pmap_asid_info[ci->ci_cpuid].pma_asidgen) {
				continue;
			} else if (ci->ci_curpmap != pmap) {
				pmap->pm_asid[ci->ci_cpuid].pma_asidgen = 0;
				continue;
			}
		}
		cpumask |= 1UL << ci->ci_cpuid;
	}
	pmap_swunlock(pmap);
	if (cpumask != 0) {
		sr_arg.pmap = pmap;
		for (va = sva; va < eva; va += SHOOTDOWN_MAX * PAGE_SIZE) {
			sr_arg.sva = va;
			sr_arg.eva = va + SHOOTDOWN_MAX * PAGE_SIZE;
			if (sr_arg.eva > eva)
				sr_arg.eva = eva;
			sr_arg.needisync = needisync;
			smp_rendezvous_cpus(cpumask,
			    pmap_shootdown_range_action, &sr_arg);
		}
	}
}

void
pmap_shootdown_range_action(void *arg)
{
	struct pmap_shootdown_range_arg *sr_arg = arg;
	vaddr_t va;

	if (sr_arg->pmap == pmap_kernel()) {
		for (va = sr_arg->sva; va < sr_arg->eva; va += PAGE_SIZE)
			pmap_invalidate_kernel_page(va);
	} else {
		for (va = sr_arg->sva; va < sr_arg->eva; va += PAGE_SIZE)
			pmap_invalidate_user_page(sr_arg->pmap, va);
		if (sr_arg->needisync)
			Mips_InvalidateICache(curcpu(), sr_arg->sva,
			    sr_arg->eva - sr_arg->sva);
	}
}

#else /* MULTIPROCESSOR */

#define PMAP_ASSERT_LOCKED(pm)	do { /* nothing */ } while (0)
#define pmap_lock(pm)		do { /* nothing */ } while (0)
#define pmap_unlock(pm)		do { /* nothing */ } while (0)
#define pmap_swlock(pm)		do { /* nothing */ } while (0)
#define pmap_swunlock(pm)	do { /* nothing */ } while (0)

void
pmap_invalidate_icache(pmap_t pmap, vaddr_t va, pt_entry_t entry)
{
	struct cpu_info *ci = curcpu();

	Mips_SyncDCachePage(ci, va, pfn_to_pad(entry));
	Mips_InvalidateICache(ci, va, PAGE_SIZE);
}

#endif /* MULTIPROCESSOR */

/*
 *	Bootstrap the system enough to run with virtual memory.
 */
void
pmap_bootstrap(void)
{
	u_int i;
	pt_entry_t *spte;

	/*
	 * Create a mapping table for kernel virtual memory. This
	 * table is a linear table in contrast to the user process
	 * mapping tables which are built with segment/page tables.
	 * Create 1GB of map (this will only use 1MB of memory).
	 */
	virtual_start = VM_MIN_KERNEL_ADDRESS;
	virtual_end = VM_MAX_KERNEL_ADDRESS;

	Sysmapsize = (VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS) /
	    PAGE_SIZE;
	if (Sysmapsize & 1)
		Sysmapsize++;	/* force even number of pages */

	Sysmap = (pt_entry_t *)
	    uvm_pageboot_alloc(sizeof(pt_entry_t) * Sysmapsize);

	pool_init(&pmap_pmap_pool, PMAP_SIZEOF(ncpusfound), 0, IPL_VM, 0,
	    "pmappl", NULL);
	pool_init(&pmap_pv_pool, sizeof(struct pv_entry), 0, IPL_VM, 0,
	    "pvpl", NULL);
	pool_init(&pmap_pg_pool, PMAP_PGSIZE, PMAP_PGSIZE, IPL_VM, 0,
	    "pmappgpl", &pmap_pg_allocator);

	pmap_kernel()->pm_count = 1;

	/*
	 * The 64 bit Mips architecture stores the AND result
	 * of the Global bits in the pte pair in the on chip
	 * translation lookaside buffer. Thus invalid entries
	 * must have the Global bit set so when Entry LO and
	 * Entry HI G bits are ANDed together they will produce
	 * a global bit to store in the tlb.
	 */
	for (i = Sysmapsize, spte = Sysmap; i != 0; i--, spte++)
		*spte = PG_G;

	for (i = 0; i < MAXCPUS; i++) {
		pmap_asid_info[i].pma_asidgen = 1;
		pmap_asid_info[i].pma_asid = MIN_USER_ASID + 1;
	}

#if defined(CPU_MIPS64R2) && !defined(CPU_LOONGSON2)
	if (cp0_get_pagegrain() & PGRAIN_RIE)
		pg_ri = PG_RI;
	if (cp0_get_pagegrain() & PGRAIN_XIE)
		pg_xi = PG_XI;
#endif

	for (i = 0; i < 8; i++) {
		if ((i & PROT_READ) == 0)
			protection_codes[i] |= pg_ri;
		if ((i & PROT_WRITE) == 0)
			protection_codes[i] |= PG_RO;
		if ((i & PROT_EXEC) == 0)
			protection_codes[i] |= pg_xi;
	}
}

/*
 *  Page steal allocator used during bootup.
 */
vaddr_t
pmap_steal_memory(vsize_t size, vaddr_t *vstartp, vaddr_t *vendp)
{
	int i, j;
	uint npg;
	vaddr_t va;
	paddr_t pa;

#ifdef DIAGNOSTIC
	if (uvm.page_init_done) {
		panic("pmap_steal_memory: too late, vm is running!");
	}
#endif

	size = round_page(size);
	npg = atop(size);
	va = 0;

	for(i = 0; i < vm_nphysseg && va == 0; i++) {
		if (vm_physmem[i].avail_start != vm_physmem[i].start ||
		    vm_physmem[i].avail_start >= vm_physmem[i].avail_end) {
			continue;
		}

		if ((vm_physmem[i].avail_end - vm_physmem[i].avail_start) < npg)
			continue;

		pa = ptoa(vm_physmem[i].avail_start);
		vm_physmem[i].avail_start += npg;
		vm_physmem[i].start += npg;

		if (vm_physmem[i].avail_start == vm_physmem[i].end) {
			if (vm_nphysseg == 1)
				panic("pmap_steal_memory: out of memory!");

			vm_nphysseg--;
			for (j = i; j < vm_nphysseg; j++)
				vm_physmem[j] = vm_physmem[j + 1];
		}
		if (vstartp)
			*vstartp = round_page(virtual_start);
		if (vendp)
			*vendp = virtual_end;

		va = PHYS_TO_XKPHYS(pa, CCA_CACHED);

		bzero((void *)va, size);
		return (va);
	}

	panic("pmap_steal_memory: no memory to steal");
}

/*
 *	Initialize the pmap module.
 *	Called by uvm_init, to initialize any structures that the pmap
 *	system needs to map virtual memory.
 */
void
pmap_init()
{

	DPRINTF(PDB_FOLLOW|PDB_INIT, ("pmap_init()\n"));

#if 0 /* too early */
	pool_setlowat(&pmap_pv_pool, pmap_pv_lowat);
#endif
}

static pv_entry_t pg_to_pvh(struct vm_page *);
static __inline pv_entry_t
pg_to_pvh(struct vm_page *pg)
{
	return &pg->mdpage.pv_ent;
}

/*
 *	Create and return a physical map.
 */
pmap_t
pmap_create()
{
	pmap_t pmap;

	DPRINTF(PDB_FOLLOW|PDB_CREATE, ("pmap_create()\n"));

	pmap = pool_get(&pmap_pmap_pool, PR_WAITOK | PR_ZERO);
	pmap->pm_segtab = pool_get(&pmap_pg_pool, PR_WAITOK | PR_ZERO);
	pmap->pm_count = 1;
	mtx_init(&pmap->pm_mtx, IPL_VM);
	mtx_init(&pmap->pm_swmtx, IPL_SCHED);

	return (pmap);
}

/*
 *	Retire the given physical map from service.
 *	Should only be called if the map contains
 *	no valid mappings.
 */
void
pmap_destroy(pmap_t pmap)
{
	pt_entry_t **pde, *pte;
	int count;
	unsigned int i, j;
#ifdef PARANOIA
	unsigned int k;
#endif

	DPRINTF(PDB_FOLLOW|PDB_CREATE, ("pmap_destroy(%p)\n", pmap));

	count = atomic_dec_int_nv(&pmap->pm_count);
	if (count > 0)
		return;

	if (pmap->pm_segtab) {
		for (i = 0; i < PMAP_SEGTABSIZE; i++) {
			/* get pointer to segment map */
			if ((pde = pmap->pm_segtab->seg_tab[i]) == NULL)
				continue;
			for (j = 0; j < NPDEPG; j++) {
				if ((pte = pde[j]) == NULL)
					continue;
#ifdef PARANOIA
				for (k = 0; k < NPTEPG; k++) {
					if (pte[k] != PG_NV)
						panic("pmap_destroy(%p): "
						    "pgtab %p not empty at "
						    "index %u", pmap, pte, k);
				}
#endif
				pool_put(&pmap_pg_pool, pte);
#ifdef PARANOIA
				pde[j] = NULL;
#endif
			}
			pool_put(&pmap_pg_pool, pde);
#ifdef PARANOIA
			pmap->pm_segtab->seg_tab[i] = NULL;
#endif
		}
		pool_put(&pmap_pg_pool, pmap->pm_segtab);
#ifdef PARANOIA
		pmap->pm_segtab = NULL;
#endif
	}

	pool_put(&pmap_pmap_pool, pmap);
}

/*
 *	Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pmap)
{

	DPRINTF(PDB_FOLLOW, ("pmap_reference(%p)\n", pmap));

	atomic_inc_int(&pmap->pm_count);
}

/*
 *      Make a new pmap (vmspace) active for the given process.
 */
void
pmap_activate(struct proc *p)
{
	pmap_t pmap = p->p_vmspace->vm_map.pmap;
	struct cpu_info *ci = curcpu();
	uint id;

	pmap_swlock(pmap);
	ci->ci_curpmap = pmap;
	p->p_addr->u_pcb.pcb_segtab = pmap->pm_segtab;
	id = pmap_alloc_tlbpid(p);
	if (p == ci->ci_curproc)
		tlb_set_pid(id);
	pmap_swunlock(pmap);
}

/*
 *      Make a previously active pmap (vmspace) inactive.
 */
void
pmap_deactivate(struct proc *p)
{
	struct cpu_info *ci = curcpu();

	ci->ci_curpmap = NULL;
}

/*
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 */
void
pmap_do_remove(pmap_t pmap, vaddr_t sva, vaddr_t eva)
{
	vaddr_t ndsva, nssva, va;
	pt_entry_t ***seg, **pde, *pte, entry;
	paddr_t pa;
	struct cpu_info *ci = curcpu();
	int needisync = 0;

	PMAP_ASSERT_LOCKED(pmap);

	DPRINTF(PDB_FOLLOW|PDB_REMOVE|PDB_PROTECT,
		("pmap_remove(%p, %p, %p)\n", pmap, (void *)sva, (void *)eva));

	stat_count(remove_stats.calls);

	if (pmap == pmap_kernel()) {
		/* remove entries from kernel pmap */
#ifdef DIAGNOSTIC
		if (sva < VM_MIN_KERNEL_ADDRESS ||
		    eva >= VM_MAX_KERNEL_ADDRESS || eva < sva)
			panic("pmap_remove(%p, %p): not in range",
			    (void *)sva, (void *)eva);
#endif
		pte = kvtopte(sva);
		for (va = sva; va < eva; va += PAGE_SIZE, pte++) {
			entry = *pte;
			if (!(entry & PG_V))
				continue;
			if (entry & PG_WIRED)
				atomic_dec_long(&pmap->pm_stats.wired_count);
			atomic_dec_long(&pmap->pm_stats.resident_count);
			pa = pfn_to_pad(entry);
			if ((entry & PG_CACHEMODE) == PG_CACHED)
				Mips_HitSyncDCachePage(ci, va, pa);
			pmap_remove_pv(pmap, va, pa);
			*pte = PG_NV | PG_G;
			/*
			 * Flush the TLB for the given address.
			 */
			pmap_invalidate_kernel_page(va);
			stat_count(remove_stats.flushes);
		}
		pmap_shootdown_range(pmap_kernel(), sva, eva, 0);
		return;
	}

#ifdef DIAGNOSTIC
	if (eva > VM_MAXUSER_ADDRESS)
		panic("pmap_remove: uva not in range");
#endif
	/*
	 * Invalidate every valid mapping within the range.
	 */
	seg = &pmap_segmap(pmap, sva);
	for (va = sva ; va < eva; va = nssva, seg++) {
		nssva = mips_trunc_seg(va) + NBSEG;
		if (*seg == NULL)
			continue;
		pde = *seg + uvtopde(va);
		for ( ; va < eva && va < nssva; va = ndsva, pde++) {
			ndsva = mips_trunc_dir(va) + NBDIR;
			if (*pde == NULL)
				continue;
			pte = *pde + uvtopte(va);
			for ( ; va < eva && va < ndsva;
			    va += PAGE_SIZE, pte++) {
				entry = *pte;
				if (!(entry & PG_V))
					continue;
				if (entry & PG_WIRED)
					atomic_dec_long(
					    &pmap->pm_stats.wired_count);
				atomic_dec_long(&pmap->pm_stats.resident_count);
				pa = pfn_to_pad(entry);
				if ((entry & PG_CACHEMODE) == PG_CACHED)
					Mips_SyncDCachePage(ci, va, pa);
				if (pg_xi != 0 && (entry & pg_xi) == 0)
					needisync = 1;
				pmap_remove_pv(pmap, va, pa);
				*pte = PG_NV;
				/*
				 * Flush the TLB for the given address.
				 */
				pmap_invalidate_user_page(pmap, va);
				if (needisync)
					Mips_InvalidateICache(ci, va,
					    PAGE_SIZE);
				stat_count(remove_stats.flushes);
			}
		}
	}
	pmap_shootdown_range(pmap, sva, eva, needisync);
}

void
pmap_remove(pmap_t pmap, vaddr_t sva, vaddr_t eva)
{
	pmap_lock(pmap);
	pmap_do_remove(pmap, sva, eva);
	pmap_unlock(pmap);
}

/*
 * Makes all mappings to a given page read-only.
 */
void
pmap_page_wrprotect(struct vm_page *pg, vm_prot_t prot)
{
	struct cpu_info *ci = curcpu();
	pt_entry_t *pte, entry, p;
	pv_entry_t pv;
	int needisync = 0;

	p = protection_codes[prot];

	mtx_enter(&pg->mdpage.pv_mtx);
	for (pv = pg_to_pvh(pg); pv != NULL; pv = pv->pv_next) {
		if (pv->pv_pmap == pmap_kernel()) {
#ifdef DIAGNOSTIC
			if (pv->pv_va < VM_MIN_KERNEL_ADDRESS ||
			    pv->pv_va >= VM_MAX_KERNEL_ADDRESS)
				panic("%s(%p)", __func__, (void *)pv->pv_va);
#endif
			pte = kvtopte(pv->pv_va);
			entry = *pte;
			if (!(entry & PG_V))
				continue;
			if ((entry & PG_M) != 0 &&
			    (entry & PG_CACHEMODE) == PG_CACHED)
				Mips_HitSyncDCachePage(ci, pv->pv_va,
				    pfn_to_pad(entry));
			entry = (entry & ~PG_PROTMASK) | p;
			*pte = entry;
			pmap_update_kernel_page(pv->pv_va, entry);
			pmap_shootdown_range(pmap_kernel(), pv->pv_va,
			    pv->pv_va + PAGE_SIZE, 0);
		} else if (pv->pv_pmap != NULL) {
			pte = pmap_pte_lookup(pv->pv_pmap, pv->pv_va);
			if (pte == NULL)
				continue;
			entry = *pte;
			if (!(entry & PG_V))
				continue;
			if ((entry & PG_M) != 0 &&
			    (entry & PG_CACHEMODE) == PG_CACHED)
				Mips_SyncDCachePage(ci, pv->pv_va,
				    pfn_to_pad(entry));
			if (pg_xi != 0 && (entry & pg_xi) == 0)
				needisync = 1;
			entry = (entry & ~PG_PROTMASK) | p;
			*pte = entry;
			pmap_update_user_page(pv->pv_pmap, pv->pv_va, entry);
			if (needisync)
				Mips_InvalidateICache(ci, pv->pv_va, PAGE_SIZE);
			pmap_shootdown_range(pv->pv_pmap, pv->pv_va,
			    pv->pv_va + PAGE_SIZE, needisync);
		}
	}
	mtx_leave(&pg->mdpage.pv_mtx);
}

/*
 * Removes all mappings to a given page.
 */
void
pmap_page_remove(struct vm_page *pg)
{
	pmap_t pmap;
	pv_entry_t pv;
	vaddr_t va;

	mtx_enter(&pg->mdpage.pv_mtx);
	while ((pv = pg_to_pvh(pg))->pv_pmap != NULL) {
		pmap = pv->pv_pmap;
		va = pv->pv_va;

		/*
		 * The PV list lock has to be released for pmap_do_remove().
		 * The lock ordering prevents locking the pmap before the
		 * release, so another CPU might remove or replace the page at
		 * the virtual address in the pmap. Continue with this PV entry
		 * only if the list head is unchanged after reacquiring
		 * the locks.
		 */
		pmap_reference(pmap);
		mtx_leave(&pg->mdpage.pv_mtx);
		pmap_lock(pmap);
		mtx_enter(&pg->mdpage.pv_mtx);
		if (pg_to_pvh(pg)->pv_pmap != pmap ||
		    pg_to_pvh(pg)->pv_va != va) {
			mtx_leave(&pg->mdpage.pv_mtx);
			pmap_unlock(pmap);
			pmap_destroy(pmap);
			mtx_enter(&pg->mdpage.pv_mtx);
			continue;
		}
		mtx_leave(&pg->mdpage.pv_mtx);

		pmap_do_remove(pmap, va, va + PAGE_SIZE);

		pmap_unlock(pmap);
		pmap_destroy(pmap);
		mtx_enter(&pg->mdpage.pv_mtx);
	}
	mtx_leave(&pg->mdpage.pv_mtx);
}

/*
 *	pmap_page_protect:
 *
 *	Lower the permission for all mappings to a given page.
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	if (prot == PROT_NONE) {
		DPRINTF(PDB_REMOVE, ("pmap_page_protect(%p, 0x%x)\n", pg, prot));
	} else {
		DPRINTF(PDB_FOLLOW|PDB_PROTECT,
			("pmap_page_protect(%p, 0x%x)\n", pg, prot));
	}

	switch (prot) {
	case PROT_READ | PROT_WRITE:
	case PROT_MASK:
		break;

	/* copy_on_write */
	case PROT_EXEC:
	case PROT_READ:
	case PROT_READ | PROT_EXEC:
		pmap_page_wrprotect(pg, prot);
		break;

	/* remove_all */
	default:
		pmap_page_remove(pg);
		break;
	}
}

/*
 *	Set the physical protection on the
 *	specified range of this map as requested.
 */
void
pmap_protect(pmap_t pmap, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	vaddr_t ndsva, nssva, va;
	pt_entry_t ***seg, **pde, *pte, entry, p;
	struct cpu_info *ci = curcpu();
	int needisync = 0;

	DPRINTF(PDB_FOLLOW|PDB_PROTECT,
		("pmap_protect(%p, %p, %p, 0x%x)\n",
		    pmap, (void *)sva, (void *)eva, prot));

	if ((prot & PROT_READ) == PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	p = protection_codes[prot];
	if (prot & PROT_WRITE)
		p |= PG_M;

	pmap_lock(pmap);

	if (pmap == pmap_kernel()) {
		/*
		 * Change entries in kernel pmap.
		 * This will trap if the page is writeable (in order to set
		 * the dirty bit) even if the dirty bit is already set. The
		 * optimization isn't worth the effort since this code isn't
		 * executed much. The common case is to make a user page
		 * read-only.
		 */
#ifdef DIAGNOSTIC
		if (sva < VM_MIN_KERNEL_ADDRESS ||
		    eva >= VM_MAX_KERNEL_ADDRESS || eva < sva)
			panic("pmap_protect(%p, %p): not in range",
			    (void *)sva, (void *)eva);
#endif
		pte = kvtopte(sva);
		for (va = sva; va < eva; va += PAGE_SIZE, pte++) {
			entry = *pte;
			if (!(entry & PG_V))
				continue;
			if ((entry & PG_M) != 0 /* && p != PG_M */)
				if ((entry & PG_CACHEMODE) == PG_CACHED)
					Mips_HitSyncDCachePage(ci, va,
					    pfn_to_pad(entry));
			entry = (entry & ~PG_PROTMASK) | p;
			*pte = entry;
			/*
			 * Update the TLB if the given address is in the cache.
			 */
			pmap_update_kernel_page(va, entry);
		}
		pmap_shootdown_range(pmap_kernel(), sva, eva, 0);
		pmap_unlock(pmap);
		return;
	}

#ifdef DIAGNOSTIC
	if (eva > VM_MAXUSER_ADDRESS)
		panic("pmap_protect: uva not in range");
#endif
	/*
	 * Change protection on every valid mapping within the range.
	 */
	seg = &pmap_segmap(pmap, sva);
	for (va = sva ; va < eva; va = nssva, seg++) {
		nssva = mips_trunc_seg(va) + NBSEG;
		if (*seg == NULL)
			continue;
		pde = *seg + uvtopde(va);
		for ( ; va < eva && va < nssva; va = ndsva, pde++) {
			ndsva = mips_trunc_dir(va) + NBDIR;
			if (*pde == NULL)
				continue;
			pte = *pde + uvtopte(va);
			for ( ; va < eva && va < ndsva;
			    va += PAGE_SIZE, pte++) {
				entry = *pte;
				if (!(entry & PG_V))
					continue;
				if ((entry & PG_M) != 0 /* && p != PG_M */ &&
				    (entry & PG_CACHEMODE) == PG_CACHED) {
					if (prot & PROT_EXEC) {
						/* This will also sync D$. */
						pmap_invalidate_icache(pmap,
						    va, entry);
					} else
						Mips_SyncDCachePage(ci, va,
						    pfn_to_pad(entry));
				}
				if (pg_xi != 0 && (entry & pg_xi) == 0)
					needisync = 1;
				entry = (entry & ~PG_PROTMASK) | p;
				*pte = entry;
				pmap_update_user_page(pmap, va, entry);
				if (needisync)
					Mips_InvalidateICache(ci, va,
					    PAGE_SIZE);
			}
		}
	}
	pmap_shootdown_range(pmap, sva, eva, needisync);

	pmap_unlock(pmap);
}

/*
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	NB:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */
int
pmap_enter(pmap_t pmap, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	pt_entry_t **pde, *pte, npte, opte;
	vm_page_t pg;
	struct cpu_info *ci = curcpu();
	u_long cpuid = ci->ci_cpuid;
	int needisync = 0;
	int wired = (flags & PMAP_WIRED) != 0;

	DPRINTF(PDB_FOLLOW|PDB_ENTER,
		("pmap_enter(%p, %p, %p, 0x%x, 0x%x)\n",
		    pmap, (void *)va, (void *)pa, prot, flags));

	pg = PHYS_TO_VM_PAGE(pa);

	pmap_lock(pmap);

	/*
	 * Get the PTE. Allocate page directory and page table pages
	 * if necessary.
	 */
	if (pmap == pmap_kernel()) {
#ifdef DIAGNOSTIC
		if (va < VM_MIN_KERNEL_ADDRESS ||
		    va >= VM_MAX_KERNEL_ADDRESS)
			panic("%s: kva %p", __func__, (void *)va);
#endif
		stat_count(enter_stats.kernel);
		pte = kvtopte(va);
	} else {
#ifdef DIAGNOSTIC
		if (va >= VM_MAXUSER_ADDRESS)
			panic("%s: uva %p", __func__, (void *)va);
#endif
		stat_count(enter_stats.user);
		if ((pde = pmap_segmap(pmap, va)) == NULL) {
			pde = pool_get(&pmap_pg_pool, PR_NOWAIT | PR_ZERO);
			if (pde == NULL) {
				if (flags & PMAP_CANFAIL) {
					pmap_unlock(pmap);
					return ENOMEM;
				}
				panic("%s: out of memory", __func__);
			}
			pmap_segmap(pmap, va) = pde;
		}
		if ((pte = pde[uvtopde(va)]) == NULL) {
			pte = pool_get(&pmap_pg_pool, PR_NOWAIT | PR_ZERO);
			if (pte == NULL) {
				if (flags & PMAP_CANFAIL) {
					pmap_unlock(pmap);
					return ENOMEM;
				}
				panic("%s: out of memory", __func__);
			}
			pde[uvtopde(va)] = pte;
		}
		pte += uvtopte(va);
	}

	if ((*pte & PG_V) && pa != pfn_to_pad(*pte)) {
		pmap_do_remove(pmap, va, va + PAGE_SIZE);
		stat_count(enter_stats.mchange);
	}

	if ((*pte & PG_V) == 0) {
		atomic_inc_long(&pmap->pm_stats.resident_count);
		if (wired)
			atomic_inc_long(&pmap->pm_stats.wired_count);
	} else {
		if ((*pte & PG_WIRED) != 0 && wired == 0)
			atomic_dec_long(&pmap->pm_stats.wired_count);
		else if ((*pte & PG_WIRED) == 0 && wired != 0)
			atomic_inc_long(&pmap->pm_stats.wired_count);
	}

	npte = protection_codes[prot] | PG_V;

	if (pg != NULL) {
		mtx_enter(&pg->mdpage.pv_mtx);

		/* Set page referenced/modified status based on flags */
		if (flags & PROT_WRITE)
			atomic_setbits_int(&pg->pg_flags,
			    PGF_ATTR_MOD | PGF_ATTR_REF);
		else if (flags & PROT_MASK)
			atomic_setbits_int(&pg->pg_flags, PGF_ATTR_REF);

		if (prot & PROT_WRITE) {
			if (pmap == pmap_kernel()) {
				/*
				 * Don't bother to trap on kernel writes,
				 * just record page as dirty.
				 */
				npte |= PG_M;
			} else if (pg->pg_flags & PGF_ATTR_MOD) {
				npte |= PG_M;
			}
		}

		if (flags & PMAP_NOCACHE)
			npte |= PG_UNCACHED;
		else
			npte |= PG_CACHED;

		stat_count(enter_stats.managed);
	} else {
		/*
		 * Assumption: if it is not part of our managed memory
		 * then it must be device memory which may be volatile.
		 */
		stat_count(enter_stats.unmanaged);
		npte |= PG_UNCACHED;
		if (prot & PROT_WRITE)
			npte |= PG_M;
	}

	if (pmap == pmap_kernel()) {
		/*
		 * Enter kernel space mapping.
		 */

		if (pg != NULL) {
			if (pmap_enter_pv(pmap, va, pg, &npte) != 0) {
				if (flags & PMAP_CANFAIL) {
					mtx_leave(&pg->mdpage.pv_mtx);
					pmap_unlock(pmap);
					return ENOMEM;
				}
				panic("pmap_enter: pmap_enter_pv() failed");
			}
		}

		npte |= vad_to_pfn(pa) | PG_G;
		if (wired)
			npte |= PG_WIRED;

		/*
		 * Update the same virtual address entry.
		 */
		opte = *pte;
		*pte = npte;
		pmap_update_kernel_page(va, npte);
		if ((opte & PG_V) != 0)
			pmap_shootdown_range(pmap_kernel(), va, va + PAGE_SIZE,
			    0);
		if (pg != NULL)
			mtx_leave(&pg->mdpage.pv_mtx);
		pmap_unlock(pmap);
		return 0;
	}

	/*
	 * Enter user space mapping.
	 */

	if (pg != NULL) {
		if (pmap_enter_pv(pmap, va, pg, &npte) != 0) {
			if (flags & PMAP_CANFAIL) {
				mtx_leave(&pg->mdpage.pv_mtx);
				pmap_unlock(pmap);
				return ENOMEM;
			}
			panic("pmap_enter: pmap_enter_pv() failed");
		}
	}

	npte |= vad_to_pfn(pa);
	if (wired)
		npte |= PG_WIRED;

	if (pmap->pm_asid[cpuid].pma_asidgen == 
	    pmap_asid_info[cpuid].pma_asidgen) {
		DPRINTF(PDB_ENTER, ("pmap_enter: new pte 0x%08x tlbpid %u\n",
			npte, pmap->pm_asid[cpuid].pma_asid));
	} else {
		DPRINTF(PDB_ENTER, ("pmap_enter: new pte 0x%08x\n", npte));
	}

	opte = *pte;
	*pte = npte;
	pmap_update_user_page(pmap, va, npte);
	if ((opte & PG_V) != 0) {
		if (pg_xi != 0 && (opte & pg_xi) == 0) {
			needisync = 1;
			Mips_InvalidateICache(ci, va, PAGE_SIZE);
		}
		pmap_shootdown_range(pmap, va, va + PAGE_SIZE, needisync);
	}

	/*
	 * If mapping an executable page, invalidate ICache
	 * and make sure there are no pending writes.
	 */
	if (pg != NULL && (prot & PROT_EXEC)) {
		if ((npte & PG_CACHEMODE) == PG_CACHED) {
			/* This will also sync D$. */
			pmap_invalidate_icache(pmap, va, npte);
		} else
			Mips_InvalidateICache(ci, va, PAGE_SIZE);
	}

	if (pg != NULL)
		mtx_leave(&pg->mdpage.pv_mtx);
	pmap_unlock(pmap);

	return 0;
}

void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	pt_entry_t *pte, npte, opte;

	DPRINTF(PDB_FOLLOW|PDB_ENTER,
		("pmap_kenter_pa(%p, %p, 0x%x)\n", (void *)va, (void *)pa, prot));

#ifdef DIAGNOSTIC
	if (va < VM_MIN_KERNEL_ADDRESS ||
	    va >= VM_MAX_KERNEL_ADDRESS)
		panic("pmap_kenter_pa: kva %p", (void *)va);
#endif

	npte = vad_to_pfn(pa) | protection_codes[prot] |
	    PG_V | PG_G | PG_CACHED | PG_WIRED;
	if (prot & PROT_WRITE)
		npte |= PG_M;
	pte = kvtopte(va);
	if ((*pte & PG_V) == 0) {
		atomic_inc_long(&pmap_kernel()->pm_stats.resident_count);
		atomic_inc_long(&pmap_kernel()->pm_stats.wired_count);
	} else {
		if ((*pte & PG_WIRED) == 0)
			atomic_inc_long(&pmap_kernel()->pm_stats.wired_count);
	}
	opte = *pte;
	*pte = npte;
	pmap_update_kernel_page(va, npte);
	if ((opte & PG_V) != 0)
		pmap_shootdown_range(pmap_kernel(), va, va + PAGE_SIZE, 0);
}

/*
 *  Remove a mapping from the kernel map table. When doing this
 *  the cache must be synced for the VA mapped since we mapped
 *  pages behind the back of the VP tracking system. 
 */
void
pmap_kremove(vaddr_t va, vsize_t len)
{
	pt_entry_t *pte, entry;
	vaddr_t eva;
	struct cpu_info *ci = curcpu();

	DPRINTF(PDB_FOLLOW|PDB_REMOVE,
		("pmap_kremove(%p, 0x%lx)\n", (void *)va, len));

	eva = va + len;
#ifdef DIAGNOSTIC
	if (va < VM_MIN_KERNEL_ADDRESS ||
	    eva >= VM_MAX_KERNEL_ADDRESS || eva < va)
		panic("pmap_kremove: va %p len %lx", (void *)va, len);
#endif
	pte = kvtopte(va);
	for (; va < eva; va += PAGE_SIZE, pte++) {
		entry = *pte;
		if (!(entry & PG_V))
			continue;
		if ((entry & PG_CACHEMODE) == PG_CACHED)
			Mips_HitSyncDCachePage(ci, va, pfn_to_pad(entry));
		*pte = PG_NV | PG_G;
		pmap_invalidate_kernel_page(va);
		pmap_shootdown_range(pmap_kernel(), va, va + PAGE_SIZE, 0);
		atomic_dec_long(&pmap_kernel()->pm_stats.wired_count);
		atomic_dec_long(&pmap_kernel()->pm_stats.resident_count);
	}
}

void
pmap_unwire(pmap_t pmap, vaddr_t va)
{
	pt_entry_t *pte;

	pmap_lock(pmap);

	if (pmap == pmap_kernel())
		pte = kvtopte(va);
	else {
		pte = pmap_pte_lookup(pmap, va);
		if (pte == NULL)
			goto out;
	}

	if (*pte & PG_V) {
		if (*pte & PG_WIRED) {
			*pte &= ~PG_WIRED;
			atomic_dec_long(&pmap->pm_stats.wired_count);
		}
	}

out:
	pmap_unlock(pmap);
}

/*
 *	Routine:	pmap_extract
 *	Function:
 *		Extract the physical page address associated
 *		with the given map/virtual_address pair.
 */
int
pmap_extract(pmap_t pmap, vaddr_t va, paddr_t *pap)
{
	paddr_t pa;
	pt_entry_t *pte;
	int found = 1;

	if (pmap == pmap_kernel()) {
		if (IS_XKPHYS(va))
			pa = XKPHYS_TO_PHYS(va);
		else if (va >= (vaddr_t)CKSEG0_BASE &&
		    va < (vaddr_t)CKSEG0_BASE + CKSEG_SIZE)
			pa = CKSEG0_TO_PHYS(va);
		else if (va >= (vaddr_t)CKSEG1_BASE &&
		    va < (vaddr_t)CKSEG1_BASE + CKSEG_SIZE)
			pa = CKSEG1_TO_PHYS(va);
		else {
#ifdef DIAGNOSTIC
			if (va < VM_MIN_KERNEL_ADDRESS ||
			    va >= VM_MAX_KERNEL_ADDRESS)
				panic("pmap_extract(%p, %p)", pmap, (void *)va);
#endif
			pte = kvtopte(va);
			if (*pte & PG_V)
				pa = pfn_to_pad(*pte) | (va & PAGE_MASK);
			else
				found = 0;
		}
	} else {
		pmap_lock(pmap);
		pte = pmap_pte_lookup(pmap, va);
		if (pte != NULL && (*pte & PG_V) != 0)
			pa = pfn_to_pad(*pte) | (va & PAGE_MASK);
		else
			found = 0;
		pmap_unlock(pmap);
	}

	if (found != 0)
		*pap = pa;

	DPRINTF(PDB_FOLLOW, ("pmap_extract(%p, %p)=%p(%d)",
		pmap, (void *)va, (void *)pa, rv));

	return found;
}

/*
 *	pmap_zero_page zeros the specified (machine independent) page.
 */
void
pmap_zero_page(struct vm_page *pg)
{
	paddr_t phys = VM_PAGE_TO_PHYS(pg);
	vaddr_t va;
	pv_entry_t pv;
	struct cpu_info *ci = curcpu();
	int df = 0;

	DPRINTF(PDB_FOLLOW, ("pmap_zero_page(%p)\n", (void *)phys));

	va = (vaddr_t)PHYS_TO_XKPHYS(phys, CCA_CACHED);
	if (pg->pg_flags & PGF_UNCACHED)
		df = 1;
	else if (pg->pg_flags & PGF_CACHED) {
		mtx_enter(&pg->mdpage.pv_mtx);
		pv = pg_to_pvh(pg);
		df = ((pv->pv_va ^ va) & cache_valias_mask) != 0;
		if (df)
			Mips_SyncDCachePage(ci, pv->pv_va, phys);
		mtx_leave(&pg->mdpage.pv_mtx);
	}
	mem_zero_page(va);
	if (df || cache_valias_mask != 0)
		Mips_HitSyncDCachePage(ci, va, phys);
}

/*
 *	pmap_copy_page copies the specified (machine independent) page.
 *
 *	We do the copy phys to phys and need to check if there may be
 *	a virtual coherence problem. If so flush the cache for the
 *	areas before copying, and flush afterwards.
 */
void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t src, dst;
	vaddr_t s, d;
	int sf, df;
	pv_entry_t pv;
	struct cpu_info *ci = curcpu();

	sf = df = 0;
	src = VM_PAGE_TO_PHYS(srcpg);
	dst = VM_PAGE_TO_PHYS(dstpg);
	s = (vaddr_t)PHYS_TO_XKPHYS(src, CCA_CACHED);
	d = (vaddr_t)PHYS_TO_XKPHYS(dst, CCA_CACHED);

	DPRINTF(PDB_FOLLOW,
		("pmap_copy_page(%p, %p)\n", (void *)src, (void *)dst));

	mtx_enter(&srcpg->mdpage.pv_mtx);
	pv = pg_to_pvh(srcpg);
	if (srcpg->pg_flags & PGF_UNCACHED)
		sf = 1;
	else if (srcpg->pg_flags & PGF_CACHED) {
		sf = ((pv->pv_va ^ s) & cache_valias_mask) != 0;
		if (sf)
			Mips_SyncDCachePage(ci, pv->pv_va, src);
	}
	mtx_leave(&srcpg->mdpage.pv_mtx);

	mtx_enter(&dstpg->mdpage.pv_mtx);
	pv = pg_to_pvh(dstpg);
	if (dstpg->pg_flags & PGF_UNCACHED)
		df = 1;
	else if (dstpg->pg_flags & PGF_CACHED) {
		df = ((pv->pv_va ^ s) & cache_valias_mask) != 0;
		if (df)
			Mips_SyncDCachePage(ci, pv->pv_va, dst);
	}
	mtx_leave(&dstpg->mdpage.pv_mtx);

	memcpy((void *)d, (void *)s, PAGE_SIZE);

	if (sf)
		Mips_HitInvalidateDCache(ci, s, PAGE_SIZE);
	if (df || cache_valias_mask != 0)
		Mips_HitSyncDCachePage(ci, d, dst);
}

/*
 *  Clear the modify bits on the specified physical page.
 *  Also sync the cache so it reflects the new clean state of the page.
 */
int
pmap_clear_modify(struct vm_page *pg)
{
	pv_entry_t pv;
	pt_entry_t *pte, entry;
	paddr_t pa;
	struct cpu_info *ci = curcpu();
	int modified = 0;

	DPRINTF(PDB_FOLLOW,
		("pmap_clear_modify(%p)\n", (void *)VM_PAGE_TO_PHYS(pg)));

	mtx_enter(&pg->mdpage.pv_mtx);

	if (pg->pg_flags & PGF_ATTR_MOD) {
		atomic_clearbits_int(&pg->pg_flags, PGF_ATTR_MOD);
		modified = 1;
	}

	pa = VM_PAGE_TO_PHYS(pg);
	for (pv = pg_to_pvh(pg); pv != NULL; pv = pv->pv_next) {
		if (pv->pv_pmap == pmap_kernel()) {
#ifdef DIAGNOSTIC
			if (pv->pv_va < VM_MIN_KERNEL_ADDRESS ||
			    pv->pv_va >= VM_MAX_KERNEL_ADDRESS)
				panic("pmap_clear_modify(%p)",
				    (void *)pv->pv_va);
#endif
			pte = kvtopte(pv->pv_va);
			entry = *pte;
			if ((entry & PG_V) != 0 && (entry & PG_M) != 0) {
				if (pg->pg_flags & PGF_CACHED)
					Mips_HitSyncDCachePage(ci, pv->pv_va,
					    pfn_to_pad(entry));
				modified = 1;
				entry &= ~PG_M;
				*pte = entry;
				pmap_update_kernel_page(pv->pv_va, entry);
				pmap_shootdown_range(pmap_kernel(), pv->pv_va,
				    pv->pv_va + PAGE_SIZE, 0);
			}
		} else if (pv->pv_pmap != NULL) {
			pte = pmap_pte_lookup(pv->pv_pmap, pv->pv_va);
			if (pte == NULL)
				continue;
			entry = *pte;
			if ((entry & PG_V) != 0 && (entry & PG_M) != 0) {
				if (pg->pg_flags & PGF_CACHED)
					Mips_SyncDCachePage(ci, pv->pv_va, pa);
				modified = 1;
				entry &= ~PG_M;
				*pte = entry;
				pmap_update_user_page(pv->pv_pmap, pv->pv_va,
				    entry);
				pmap_shootdown_range(pv->pv_pmap, pv->pv_va,
				    pv->pv_va + PAGE_SIZE, 0);
			}
		}
	}

	mtx_leave(&pg->mdpage.pv_mtx);

	return modified;
}

/*
 *	pmap_clear_reference:
 *
 *	Clear the reference bit on the specified physical page.
 */
int
pmap_clear_reference(struct vm_page *pg)
{
	int referenced;

	DPRINTF(PDB_FOLLOW, ("pmap_clear_reference(%p)\n",
	    (void *)VM_PAGE_TO_PHYS(pg)));

	referenced = (pg->pg_flags & PGF_ATTR_REF) != 0;
	atomic_clearbits_int(&pg->pg_flags, PGF_ATTR_REF);
	return referenced;
}

/*
 *	pmap_is_referenced:
 *
 *	Return whether or not the specified physical page is referenced
 *	by any physical maps.
 */
int
pmap_is_referenced(struct vm_page *pg)
{
	return (pg->pg_flags & PGF_ATTR_REF) != 0;
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page is modified
 *	by any physical maps.
 */
int
pmap_is_modified(struct vm_page *pg)
{
	return (pg->pg_flags & PGF_ATTR_MOD) != 0;
}

/*
 * Miscellaneous support routines not part of the pmap API
 */

/*
 * Sets the modify bit on the page that contains virtual address va.
 * Returns 0 on success. If the page is mapped read-only, the bit is left
 * unchanged and the function returns non-zero.
 */
int
pmap_emulate_modify(pmap_t pmap, vaddr_t va)
{
	pt_entry_t *pte, entry;
	paddr_t pa;
	vm_page_t pg;
#ifdef MULTIPROCESSOR
	/* Keep back TLB shootdowns. */
	register_t sr = disableintr();
	pt_entry_t old_entry;
#endif
	int readonly = 0;

	if (pmap == pmap_kernel()) {
		pte = kvtopte(va);
	} else {
		pte = pmap_pte_lookup(pmap, va);
		if (pte == NULL)
			panic("%s: invalid page dir in pmap %p va %p", __func__,
			    pmap, (void *)va);
	}
	entry = *pte;
	if (!(entry & PG_V) || (entry & PG_M)) {
#ifdef MULTIPROCESSOR
		/* Another CPU might have changed the mapping. */
		if (pmap == pmap_kernel())
			pmap_update_kernel_page(trunc_page(va), entry);
		else
			pmap_update_user_page(pmap, trunc_page(va), entry);
		goto out;
#else
		panic("%s: invalid pte 0x%lx in pmap %p va %p", __func__,
		    (unsigned long)entry, pmap, (void *)va);
#endif
	}
	if (entry & PG_RO) {
		readonly = 1;
		goto out;
	}
#ifdef MULTIPROCESSOR
	old_entry = entry;
	entry |= PG_M;
	if (pmap_pte_cas(pte, old_entry, entry) != old_entry) {
		/* Refault to resolve the conflict. */
		goto out;
	}
#else
	entry |= PG_M;
	*pte = entry;
#endif
	if (pmap == pmap_kernel())
		pmap_update_kernel_page(trunc_page(va), entry);
	else
		pmap_update_user_page(pmap, trunc_page(va), entry);
	pa = pfn_to_pad(entry);
	pg = PHYS_TO_VM_PAGE(pa);
	if (pg == NULL)
		panic("%s: unmanaged page %p in pmap %p va %p", __func__,
		    (void *)pa, pmap, (void *)va);
	atomic_setbits_int(&pg->pg_flags, PGF_ATTR_MOD | PGF_ATTR_REF);
out:
#ifdef MULTIPROCESSOR
	setsr(sr);
#endif
	return readonly;
}

/*
 *  Walk the PV tree for a physical page and change all its
 *  mappings to cached or uncached.
 */
void
pmap_do_page_cache(vm_page_t pg, u_int mode)
{
	pv_entry_t pv;
	pt_entry_t *pte, entry;
	pt_entry_t newmode;

	MUTEX_ASSERT_LOCKED(&pg->mdpage.pv_mtx);

	DPRINTF(PDB_FOLLOW|PDB_ENTER, ("pmap_page_cache(%p)\n", pg));

	newmode = mode & PGF_CACHED ? PG_CACHED : PG_UNCACHED;

	for (pv = pg_to_pvh(pg); pv != NULL; pv = pv->pv_next) {
		if (pv->pv_pmap == pmap_kernel()) {
#ifdef DIAGNOSTIC
			if (pv->pv_va < VM_MIN_KERNEL_ADDRESS ||
			    pv->pv_va >= VM_MAX_KERNEL_ADDRESS)
				panic("pmap_page_cache(%p)", (void *)pv->pv_va);
#endif
			pte = kvtopte(pv->pv_va);
			entry = *pte;
			if (entry & PG_V) {
				entry = (entry & ~PG_CACHEMODE) | newmode;
				*pte = entry;
				pmap_update_kernel_page(pv->pv_va, entry);
				pmap_shootdown_range(pmap_kernel(), pv->pv_va,
				    pv->pv_va + PAGE_SIZE, 0);
			}
		} else if (pv->pv_pmap != NULL) {
			pte = pmap_pte_lookup(pv->pv_pmap, pv->pv_va);
			if (pte == NULL)
				continue;
			entry = *pte;
			if (entry & PG_V) {
				entry = (entry & ~PG_CACHEMODE) | newmode;
				*pte = entry;
				pmap_update_user_page(pv->pv_pmap, pv->pv_va,
				    entry);
				pmap_shootdown_range(pv->pv_pmap, pv->pv_va,
				    pv->pv_va + PAGE_SIZE, 0);
			}
		}
	}
	atomic_clearbits_int(&pg->pg_flags, PGF_CACHED | PGF_UNCACHED);
	atomic_setbits_int(&pg->pg_flags, mode);
}

void
pmap_page_cache(vm_page_t pg, u_int mode)
{
	mtx_enter(&pg->mdpage.pv_mtx);
	pmap_do_page_cache(pg, mode);
	mtx_leave(&pg->mdpage.pv_mtx);
}

/*
 * Allocate a hardware PID and return it.
 * It takes almost as much or more time to search the TLB for a
 * specific PID and flush those entries as it does to flush the entire TLB.
 * Therefore, when we allocate a new PID, we just take the next number. When
 * we run out of numbers, we flush the TLB, increment the generation count
 * and start over. PID zero is reserved for kernel use.
 * This is called only by switch().
 */
uint
pmap_alloc_tlbpid(struct proc *p)
{
	pmap_t pmap;
	uint id;
	struct cpu_info *ci = curcpu();
	u_long cpuid = ci->ci_cpuid;

	pmap = p->p_vmspace->vm_map.pmap;
	if (pmap->pm_asid[cpuid].pma_asidgen != 
	    pmap_asid_info[cpuid].pma_asidgen) {
		id = pmap_asid_info[cpuid].pma_asid;
		if (id >= PG_ASID_COUNT) {
			tlb_asid_wrap(ci);
			/* reserve tlbpid_gen == 0 to always mean invalid */
			if (++pmap_asid_info[cpuid].pma_asidgen == 0)
				pmap_asid_info[cpuid].pma_asidgen = 1;
			id = MIN_USER_ASID;
		}
		pmap_asid_info[cpuid].pma_asid = id + 1;
		pmap->pm_asid[cpuid].pma_asid = id;
		pmap->pm_asid[cpuid].pma_asidgen = 
			pmap_asid_info[cpuid].pma_asidgen;
	} else {
		id = pmap->pm_asid[cpuid].pma_asid;
	}

	if (curproc) {
		DPRINTF(PDB_FOLLOW|PDB_TLBPID, 
			("pmap_alloc_tlbpid: curproc %d '%s' ",
				curproc->p_p->ps_pid, curproc->p_p->ps_comm));
	} else {
		DPRINTF(PDB_FOLLOW|PDB_TLBPID, 
			("pmap_alloc_tlbpid: curproc <none> "));
	}
	DPRINTF(PDB_FOLLOW|PDB_TLBPID, ("segtab %p tlbpid %u pid %d '%s'\n",
			pmap->pm_segtab, id, p->p_p->ps_pid, p->p_p->ps_comm));

	return (id);
}

/*
 * Enter the pmap and virtual address into the physical to virtual map table.
 */
int
pmap_enter_pv(pmap_t pmap, vaddr_t va, vm_page_t pg, pt_entry_t *npte)
{
	pv_entry_t pv, npv;

	MUTEX_ASSERT_LOCKED(&pg->mdpage.pv_mtx);

	pv = pg_to_pvh(pg);
	if (pv->pv_pmap == NULL) {
		/*
		 * No entries yet, use header as the first entry
		 */

		DPRINTF(PDB_PVENTRY,
			("pmap_enter: first pv: pmap %p va %p pa %p\n",
				pmap, (void *)va, (void *)VM_PAGE_TO_PHYS(pg)));

		stat_count(enter_stats.firstpv);

		pv->pv_va = va;
		if (*npte & PG_CACHED)
			atomic_setbits_int(&pg->pg_flags, PGF_CACHED);
		if (*npte & PG_UNCACHED)
			atomic_setbits_int(&pg->pg_flags, PGF_UNCACHED);
		pv->pv_pmap = pmap;
		pv->pv_next = NULL;
	} else {
		/*
		 * There is at least one other VA mapping this page.
		 * We'll place this entry after the header.
		 */

		if ((pg->pg_flags & PGF_CACHED) == 0) {
			/*
			 * If page is not mapped cached it's either because
			 * an uncached mapping was explicitly requested or
			 * we have a VAC situation.
			 * Map this page uncached as well.
			 */
			*npte = (*npte & ~PG_CACHEMODE) | PG_UNCACHED;
		}

		/*
		 * The entry may already be in the list if
		 * we are only changing the protection bits.
		 */
		for (npv = pv; npv; npv = npv->pv_next) {
			if (pmap == npv->pv_pmap && va == npv->pv_va)
				return 0;
		}

		DPRINTF(PDB_PVENTRY,
			("pmap_enter: new pv: pmap %p va %p pg %p\n",
			    pmap, (void *)va, (void *)VM_PAGE_TO_PHYS(pg)));

		npv = pmap_pv_alloc();
		if (npv == NULL)
			return ENOMEM;

		if (cache_valias_mask != 0 && (*npte & PG_CACHED) != 0 &&
		    (pg->pg_flags & PGF_CACHED) != 0) {
			/*
			 * We have a VAC possibility.  Check if virtual
			 * address of current mappings are compatible
			 * with this new mapping. Only need to check first
			 * since all others have been checked compatible
			 * when added. If they are incompatible, update
			 * all mappings to be mapped uncached, flush the
			 * cache and set page to not be mapped cached.
			 */
			if (((pv->pv_va ^ va) & cache_valias_mask) != 0) {
#ifdef PMAPDEBUG
				printf("%s: uncaching page pa %p, va %p/%p\n",
				    __func__, (void *)VM_PAGE_TO_PHYS(pg),
				    (void *)pv->pv_va, (void *)va);
#endif
				pmap_do_page_cache(pg, 0);
				Mips_SyncDCachePage(curcpu(), pv->pv_va,
				    VM_PAGE_TO_PHYS(pg));
				*npte = (*npte & ~PG_CACHEMODE) | PG_UNCACHED;
			}
		}

		npv->pv_va = va;
		npv->pv_pmap = pmap;
		npv->pv_next = pv->pv_next;
		pv->pv_next = npv;

		if (!npv->pv_next)
			stat_count(enter_stats.secondpv);
	}

	return 0;
}

/*
 * Remove a physical to virtual address translation from the PV table.
 */
void
pmap_remove_pv(pmap_t pmap, vaddr_t va, paddr_t pa)
{
	pv_entry_t pv, npv;
	vm_page_t pg;

	DPRINTF(PDB_FOLLOW|PDB_PVENTRY,
		("pmap_remove_pv(%p, %p, %p)\n", pmap, (void *)va, (void *)pa));

	/*
	 * Remove page from the PV table
	 */
	pg = PHYS_TO_VM_PAGE(pa);
	if (pg == NULL)
		return;

	mtx_enter(&pg->mdpage.pv_mtx);

	/*
	 * If we are removing the first entry on the list, copy up
	 * the next entry, if any, and free that pv item since the
	 * first root item can't be freed. Else walk the list.
	 */
	pv = pg_to_pvh(pg);
	if (pmap == pv->pv_pmap && va == pv->pv_va) {
		npv = pv->pv_next;
		if (npv) {
			*pv = *npv;
			pmap_pv_free(npv);
		} else {
			pv->pv_pmap = NULL;
			atomic_clearbits_int(&pg->pg_flags,
			    PG_PMAPMASK & ~PGF_PRESERVE);
		}
		stat_count(remove_stats.pvfirst);
	} else {
		for (npv = pv->pv_next; npv; pv = npv, npv = npv->pv_next) {
			stat_count(remove_stats.pvsearch);
			if (pmap == npv->pv_pmap && va == npv->pv_va)
				break;
		}
		if (npv != NULL) {
			pv->pv_next = npv->pv_next;
			pmap_pv_free(npv);
		} else {
#ifdef DIAGNOSTIC
			panic("pmap_remove_pv(%p, %p, %p) not found",
			    pmap, (void *)va, (void *)pa);
#endif
		}
	}

	if (cache_valias_mask != 0 && pv->pv_pmap != NULL &&
	    (pg->pg_flags & (PGF_CACHED | PGF_UNCACHED)) == 0) {
		/*
		 * If this page had been mapped uncached due to aliasing,
		 * check if it can be mapped cached again after the current
		 * entry's removal.
		 */
		pv = pg_to_pvh(pg);
		va = pv->pv_va;
		for (pv = pv->pv_next; pv != NULL; pv = pv->pv_next) {
			if (((pv->pv_va ^ va) & cache_valias_mask) != 0)
				break;
		}

		if (pv == NULL) {
#ifdef PMAPDEBUG
			printf("%s: caching page pa %p, va %p again\n",
			    __func__, (void *)VM_PAGE_TO_PHYS(pg), (void *)va);
#endif
			pmap_do_page_cache(pg, PGF_CACHED);
		}
	}

	mtx_leave(&pg->mdpage.pv_mtx);
}

/*
 * Allocator for smaller-than-a-page structures pool (pm_segtab, and
 * second level page tables).  Pages backing this poll are mapped in
 * XKPHYS to avoid additional page faults when servicing a TLB miss.
 */

void *
pmap_pg_alloc(struct pool *pp, int flags, int *slowdown)
{
	vm_page_t pg;

	*slowdown = 0;
	for (;;) {
		pg = uvm_pagealloc(NULL, 0, NULL,
		    UVM_PGA_USERESERVE | UVM_PGA_ZERO);
		if (pg != NULL)
			break;

		*slowdown = 1;
		if (flags & PR_WAITOK)
			uvm_wait(__func__);
		else
			break;
	}

	if (pg != NULL)
		return (void *)PHYS_TO_XKPHYS(VM_PAGE_TO_PHYS(pg), CCA_CACHED);
	else
		return NULL;
}

void
pmap_pg_free(struct pool *pp, void *item)
{
	vaddr_t va = (vaddr_t)item;
	paddr_t pa = XKPHYS_TO_PHYS(va);
	vm_page_t pg = PHYS_TO_VM_PAGE(pa);

	if (cache_valias_mask)
		Mips_HitSyncDCachePage(curcpu(), va, pa);
	uvm_pagefree(pg);
}

void
pmap_proc_iflush(struct process *pr, vaddr_t va, vsize_t len)
{
#ifdef MULTIPROCESSOR
	struct pmap *pmap = vm_map_pmap(&pr->ps_vmspace->vm_map);
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci->ci_curpmap == pmap) {
			Mips_InvalidateICache(ci, va, len);
			break;
		}
	}
#else
	Mips_InvalidateICache(curcpu(), va, len);
#endif
}

#ifdef __HAVE_PMAP_DIRECT
vaddr_t
pmap_map_direct(vm_page_t pg)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);
	vaddr_t va;

	va = PHYS_TO_XKPHYS(pa, CCA_CACHED);

	return va;
}

vm_page_t
pmap_unmap_direct(vaddr_t va)
{
	paddr_t pa;
	vm_page_t pg;

	pa = XKPHYS_TO_PHYS(va);
	pg = PHYS_TO_VM_PAGE(pa);
	if (cache_valias_mask)
		Mips_HitSyncDCachePage(curcpu(), va, pa);

	return pg;
}
#endif

void
pmap_update(struct pmap *pmap)
{
	Mips_SyncICache(curcpu());
}

/*
 * Read an instruction from a given virtual memory address.
 * TLB read-inhibition is bypassed.
 */
int
pmap_copyinsn(pmap_t pmap, vaddr_t uva, uint32_t *insn)
{
	pt_entry_t *pte;
	paddr_t pa;
	int found = 0;

	if (uva >= VM_MAXUSER_ADDRESS || pmap == pmap_kernel()) {
		panic("%s(%p, %p): invalid params", __func__,
		    pmap, (void *)uva);
	}

	/*
	 * Read the instruction through the direct map region.
	 *
	 * The pmap lock prevents other threads from changing the mapping
	 * and repurposing the page frame while this thread accesses the
	 * direct map.
	 */
	pmap_lock(pmap);
	pte = pmap_pte_lookup(pmap, uva);
	if (pte != NULL && (*pte & PG_V) != 0 && (*pte & pg_xi) == 0) {
		pa = pfn_to_pad(*pte) | (uva & PAGE_MASK);
		*insn = *(uint32_t *)PHYS_TO_XKPHYS(pa, CCA_CACHED);
		found = 1;
	}
	pmap_unlock(pmap);

	return found;
}
