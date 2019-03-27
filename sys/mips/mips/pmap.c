/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
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
 *	from:	@(#)pmap.c	7.7 (Berkeley)	5/12/91
 *	from: src/sys/i386/i386/pmap.c,v 1.250.2.8 2000/11/21 00:09:14 ps
 *	JNPR: pmap.c,v 1.11.2.1 2007/08/16 11:51:06 girish
 */

/*
 *	Manages physical address maps.
 *
 *	Since the information managed by this module is
 *	also stored by the logical address mapping module,
 *	this module may throw away valid virtual-to-physical
 *	mappings at almost any time.  However, invalidations
 *	of virtual-to-physical mappings must be done as
 *	requested.
 *
 *	In order to cope with hardware architectures which
 *	make virtual-to-physical map invalidates expensive,
 *	this module may delay invalidate or reduced protection
 *	operations until such time as they are actually
 *	necessary.  This module is given full information as
 *	to which processors are currently using which maps,
 *	and to when physical maps must be made correct.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_pmap.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/uma.h>

#include <machine/cache.h>
#include <machine/md_var.h>
#include <machine/tlb.h>

#undef PMAP_DEBUG

#if !defined(DIAGNOSTIC)
#define	PMAP_INLINE __inline
#else
#define	PMAP_INLINE
#endif

#ifdef PV_STATS
#define PV_STAT(x)	do { x ; } while (0)
#else
#define PV_STAT(x)	do { } while (0)
#endif

/*
 * Get PDEs and PTEs for user/kernel address space
 */
#define	pmap_seg_index(v)	(((v) >> SEGSHIFT) & (NPDEPG - 1))
#define	pmap_pde_index(v)	(((v) >> PDRSHIFT) & (NPDEPG - 1))
#define	pmap_pte_index(v)	(((v) >> PAGE_SHIFT) & (NPTEPG - 1))
#define	pmap_pde_pindex(v)	((v) >> PDRSHIFT)

#ifdef __mips_n64
#define	NUPDE			(NPDEPG * NPDEPG)
#define	NUSERPGTBLS		(NUPDE + NPDEPG)
#else
#define	NUPDE			(NPDEPG)
#define	NUSERPGTBLS		(NUPDE)
#endif

#define	is_kernel_pmap(x)	((x) == kernel_pmap)

struct pmap kernel_pmap_store;
pd_entry_t *kernel_segmap;

vm_offset_t virtual_avail;	/* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */

static int nkpt;
unsigned pmap_max_asid;		/* max ASID supported by the system */

#define	PMAP_ASID_RESERVED	0

vm_offset_t kernel_vm_end = VM_MIN_KERNEL_ADDRESS;

static void pmap_asid_alloc(pmap_t pmap);

static struct rwlock_padalign pvh_global_lock;

/*
 * Data for the pv entry allocation mechanism
 */
static TAILQ_HEAD(pch, pv_chunk) pv_chunks = TAILQ_HEAD_INITIALIZER(pv_chunks);
static int pv_entry_count;

static void free_pv_chunk(struct pv_chunk *pc);
static void free_pv_entry(pmap_t pmap, pv_entry_t pv);
static pv_entry_t get_pv_entry(pmap_t pmap, boolean_t try);
static vm_page_t pmap_pv_reclaim(pmap_t locked_pmap);
static void pmap_pvh_free(struct md_page *pvh, pmap_t pmap, vm_offset_t va);
static pv_entry_t pmap_pvh_remove(struct md_page *pvh, pmap_t pmap,
    vm_offset_t va);
static vm_page_t pmap_alloc_direct_page(unsigned int index, int req);
static vm_page_t pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va,
    vm_page_t m, vm_prot_t prot, vm_page_t mpte);
static void pmap_grow_direct_page(int req);
static int pmap_remove_pte(struct pmap *pmap, pt_entry_t *ptq, vm_offset_t va,
    pd_entry_t pde);
static void pmap_remove_page(struct pmap *pmap, vm_offset_t va);
static void pmap_remove_entry(struct pmap *pmap, vm_page_t m, vm_offset_t va);
static boolean_t pmap_try_insert_pv_entry(pmap_t pmap, vm_page_t mpte,
    vm_offset_t va, vm_page_t m);
static void pmap_update_page(pmap_t pmap, vm_offset_t va, pt_entry_t pte);
static void pmap_invalidate_all(pmap_t pmap);
static void pmap_invalidate_page(pmap_t pmap, vm_offset_t va);
static void _pmap_unwire_ptp(pmap_t pmap, vm_offset_t va, vm_page_t m);

static vm_page_t pmap_allocpte(pmap_t pmap, vm_offset_t va, u_int flags);
static vm_page_t _pmap_allocpte(pmap_t pmap, unsigned ptepindex, u_int flags);
static int pmap_unuse_pt(pmap_t, vm_offset_t, pd_entry_t);
static pt_entry_t init_pte_prot(vm_page_t m, vm_prot_t access, vm_prot_t prot);

static void pmap_invalidate_page_action(void *arg);
static void pmap_invalidate_range_action(void *arg);
static void pmap_update_page_action(void *arg);

#ifndef __mips_n64
/*
 * This structure is for high memory (memory above 512Meg in 32 bit) support.
 * The highmem area does not have a KSEG0 mapping, and we need a mechanism to
 * do temporary per-CPU mappings for pmap_zero_page, pmap_copy_page etc.
 *
 * At bootup, we reserve 2 virtual pages per CPU for mapping highmem pages. To
 * access a highmem physical address on a CPU, we map the physical address to
 * the reserved virtual address for the CPU in the kernel pagetable.  This is
 * done with interrupts disabled(although a spinlock and sched_pin would be
 * sufficient).
 */
struct local_sysmaps {
	vm_offset_t	base;
	uint32_t	saved_intr;
	uint16_t	valid1, valid2;
};
static struct local_sysmaps sysmap_lmem[MAXCPU];

static __inline void
pmap_alloc_lmem_map(void)
{
	int i;

	for (i = 0; i < MAXCPU; i++) {
		sysmap_lmem[i].base = virtual_avail;
		virtual_avail += PAGE_SIZE * 2;
		sysmap_lmem[i].valid1 = sysmap_lmem[i].valid2 = 0;
	}
}

static __inline vm_offset_t
pmap_lmem_map1(vm_paddr_t phys)
{
	struct local_sysmaps *sysm;
	pt_entry_t *pte, npte;
	vm_offset_t va;
	uint32_t intr;
	int cpu;

	intr = intr_disable();
	cpu = PCPU_GET(cpuid);
	sysm = &sysmap_lmem[cpu];
	sysm->saved_intr = intr;
	va = sysm->base;
	npte = TLBLO_PA_TO_PFN(phys) | PTE_C_CACHE | PTE_D | PTE_V | PTE_G;
	pte = pmap_pte(kernel_pmap, va);
	*pte = npte;
	sysm->valid1 = 1;
	return (va);
}

static __inline vm_offset_t
pmap_lmem_map2(vm_paddr_t phys1, vm_paddr_t phys2)
{
	struct local_sysmaps *sysm;
	pt_entry_t *pte, npte;
	vm_offset_t va1, va2;
	uint32_t intr;
	int cpu;

	intr = intr_disable();
	cpu = PCPU_GET(cpuid);
	sysm = &sysmap_lmem[cpu];
	sysm->saved_intr = intr;
	va1 = sysm->base;
	va2 = sysm->base + PAGE_SIZE;
	npte = TLBLO_PA_TO_PFN(phys1) | PTE_C_CACHE | PTE_D | PTE_V | PTE_G;
	pte = pmap_pte(kernel_pmap, va1);
	*pte = npte;
	npte = TLBLO_PA_TO_PFN(phys2) | PTE_C_CACHE | PTE_D | PTE_V | PTE_G;
	pte = pmap_pte(kernel_pmap, va2);
	*pte = npte;
	sysm->valid1 = 1;
	sysm->valid2 = 1;
	return (va1);
}

static __inline void
pmap_lmem_unmap(void)
{
	struct local_sysmaps *sysm;
	pt_entry_t *pte;
	int cpu;

	cpu = PCPU_GET(cpuid);
	sysm = &sysmap_lmem[cpu];
	pte = pmap_pte(kernel_pmap, sysm->base);
	*pte = PTE_G;
	tlb_invalidate_address(kernel_pmap, sysm->base);
	sysm->valid1 = 0;
	if (sysm->valid2) {
		pte = pmap_pte(kernel_pmap, sysm->base + PAGE_SIZE);
		*pte = PTE_G;
		tlb_invalidate_address(kernel_pmap, sysm->base + PAGE_SIZE);
		sysm->valid2 = 0;
	}
	intr_restore(sysm->saved_intr);
}
#else  /* __mips_n64 */

static __inline void
pmap_alloc_lmem_map(void)
{
}

static __inline vm_offset_t
pmap_lmem_map1(vm_paddr_t phys)
{

	return (0);
}

static __inline vm_offset_t
pmap_lmem_map2(vm_paddr_t phys1, vm_paddr_t phys2)
{

	return (0);
}

static __inline vm_offset_t
pmap_lmem_unmap(void)
{

	return (0);
}
#endif /* !__mips_n64 */

static __inline int
pmap_pte_cache_bits(vm_paddr_t pa, vm_page_t m)
{
	vm_memattr_t ma;

	ma = pmap_page_get_memattr(m);
	if (ma == VM_MEMATTR_WRITE_BACK && !is_cacheable_mem(pa))
		ma = VM_MEMATTR_UNCACHEABLE;
	return PTE_C(ma);
}
#define PMAP_PTE_SET_CACHE_BITS(pte, ps, m) {	\
	pte &= ~PTE_C_MASK;			\
	pte |= pmap_pte_cache_bits(pa, m);	\
}

/*
 * Page table entry lookup routines.
 */
static __inline pd_entry_t *
pmap_segmap(pmap_t pmap, vm_offset_t va)
{

	return (&pmap->pm_segtab[pmap_seg_index(va)]);
}

#ifdef __mips_n64
static __inline pd_entry_t *
pmap_pdpe_to_pde(pd_entry_t *pdpe, vm_offset_t va)
{
	pd_entry_t *pde;

	pde = (pd_entry_t *)*pdpe;
	return (&pde[pmap_pde_index(va)]);
}

static __inline pd_entry_t *
pmap_pde(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *pdpe;

	pdpe = pmap_segmap(pmap, va);
	if (*pdpe == NULL)
		return (NULL);

	return (pmap_pdpe_to_pde(pdpe, va));
}
#else
static __inline pd_entry_t *
pmap_pdpe_to_pde(pd_entry_t *pdpe, vm_offset_t va)
{

	return (pdpe);
}

static __inline
pd_entry_t *pmap_pde(pmap_t pmap, vm_offset_t va)
{

	return (pmap_segmap(pmap, va));
}
#endif

static __inline pt_entry_t *
pmap_pde_to_pte(pd_entry_t *pde, vm_offset_t va)
{
	pt_entry_t *pte;

	pte = (pt_entry_t *)*pde;
	return (&pte[pmap_pte_index(va)]);
}

pt_entry_t *
pmap_pte(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *pde;

	pde = pmap_pde(pmap, va);
	if (pde == NULL || *pde == NULL)
		return (NULL);

	return (pmap_pde_to_pte(pde, va));
}

vm_offset_t
pmap_steal_memory(vm_size_t size)
{
	vm_paddr_t bank_size, pa;
	vm_offset_t va;

	size = round_page(size);
	bank_size = phys_avail[1] - phys_avail[0];
	while (size > bank_size) {
		int i;

		for (i = 0; phys_avail[i + 2]; i += 2) {
			phys_avail[i] = phys_avail[i + 2];
			phys_avail[i + 1] = phys_avail[i + 3];
		}
		phys_avail[i] = 0;
		phys_avail[i + 1] = 0;
		if (!phys_avail[0])
			panic("pmap_steal_memory: out of memory");
		bank_size = phys_avail[1] - phys_avail[0];
	}

	pa = phys_avail[0];
	phys_avail[0] += size;
	if (MIPS_DIRECT_MAPPABLE(pa) == 0)
		panic("Out of memory below 512Meg?");
	va = MIPS_PHYS_TO_DIRECT(pa);
	bzero((caddr_t)va, size);
	return (va);
}

/*
 * Bootstrap the system enough to run with virtual memory.  This
 * assumes that the phys_avail array has been initialized.
 */
static void
pmap_create_kernel_pagetable(void)
{
	int i, j;
	vm_offset_t ptaddr;
	pt_entry_t *pte;
#ifdef __mips_n64
	pd_entry_t *pde;
	vm_offset_t pdaddr;
	int npt, npde;
#endif

	/*
	 * Allocate segment table for the kernel
	 */
	kernel_segmap = (pd_entry_t *)pmap_steal_memory(PAGE_SIZE);

	/*
	 * Allocate second level page tables for the kernel
	 */
#ifdef __mips_n64
	npde = howmany(NKPT, NPDEPG);
	pdaddr = pmap_steal_memory(PAGE_SIZE * npde);
#endif
	nkpt = NKPT;
	ptaddr = pmap_steal_memory(PAGE_SIZE * nkpt);

	/*
	 * The R[4-7]?00 stores only one copy of the Global bit in the
	 * translation lookaside buffer for each 2 page entry. Thus invalid
	 * entrys must have the Global bit set so when Entry LO and Entry HI
	 * G bits are anded together they will produce a global bit to store
	 * in the tlb.
	 */
	for (i = 0, pte = (pt_entry_t *)ptaddr; i < (nkpt * NPTEPG); i++, pte++)
		*pte = PTE_G;

#ifdef __mips_n64
	for (i = 0,  npt = nkpt; npt > 0; i++) {
		kernel_segmap[i] = (pd_entry_t)(pdaddr + i * PAGE_SIZE);
		pde = (pd_entry_t *)kernel_segmap[i];

		for (j = 0; j < NPDEPG && npt > 0; j++, npt--)
			pde[j] = (pd_entry_t)(ptaddr + (i * NPDEPG + j) * PAGE_SIZE);
	}
#else
	for (i = 0, j = pmap_seg_index(VM_MIN_KERNEL_ADDRESS); i < nkpt; i++, j++)
		kernel_segmap[j] = (pd_entry_t)(ptaddr + (i * PAGE_SIZE));
#endif

	PMAP_LOCK_INIT(kernel_pmap);
	kernel_pmap->pm_segtab = kernel_segmap;
	CPU_FILL(&kernel_pmap->pm_active);
	TAILQ_INIT(&kernel_pmap->pm_pvchunk);
	kernel_pmap->pm_asid[0].asid = PMAP_ASID_RESERVED;
	kernel_pmap->pm_asid[0].gen = 0;
	kernel_vm_end += nkpt * NPTEPG * PAGE_SIZE;
}

void
pmap_bootstrap(void)
{
	int i;
	int need_local_mappings = 0;

	/* Sort. */
again:
	for (i = 0; phys_avail[i + 1] != 0; i += 2) {
		/*
		 * Keep the memory aligned on page boundary.
		 */
		phys_avail[i] = round_page(phys_avail[i]);
		phys_avail[i + 1] = trunc_page(phys_avail[i + 1]);

		if (i < 2)
			continue;
		if (phys_avail[i - 2] > phys_avail[i]) {
			vm_paddr_t ptemp[2];

			ptemp[0] = phys_avail[i + 0];
			ptemp[1] = phys_avail[i + 1];

			phys_avail[i + 0] = phys_avail[i - 2];
			phys_avail[i + 1] = phys_avail[i - 1];

			phys_avail[i - 2] = ptemp[0];
			phys_avail[i - 1] = ptemp[1];
			goto again;
		}
	}

       	/*
	 * In 32 bit, we may have memory which cannot be mapped directly.
	 * This memory will need temporary mapping before it can be
	 * accessed.
	 */
	if (!MIPS_DIRECT_MAPPABLE(phys_avail[i - 1] - 1))
		need_local_mappings = 1;

	/*
	 * Copy the phys_avail[] array before we start stealing memory from it.
	 */
	for (i = 0; phys_avail[i + 1] != 0; i += 2) {
		physmem_desc[i] = phys_avail[i];
		physmem_desc[i + 1] = phys_avail[i + 1];
	}

	Maxmem = atop(phys_avail[i - 1]);

	if (bootverbose) {
		printf("Physical memory chunk(s):\n");
		for (i = 0; phys_avail[i + 1] != 0; i += 2) {
			vm_paddr_t size;

			size = phys_avail[i + 1] - phys_avail[i];
			printf("%#08jx - %#08jx, %ju bytes (%ju pages)\n",
			    (uintmax_t) phys_avail[i],
			    (uintmax_t) phys_avail[i + 1] - 1,
			    (uintmax_t) size, (uintmax_t) size / PAGE_SIZE);
		}
		printf("Maxmem is 0x%0jx\n", ptoa((uintmax_t)Maxmem));
	}
	/*
	 * Steal the message buffer from the beginning of memory.
	 */
	msgbufp = (struct msgbuf *)pmap_steal_memory(msgbufsize);
	msgbufinit(msgbufp, msgbufsize);

	/*
	 * Steal thread0 kstack.
	 */
	kstack0 = pmap_steal_memory(KSTACK_PAGES << PAGE_SHIFT);

	virtual_avail = VM_MIN_KERNEL_ADDRESS;
	virtual_end = VM_MAX_KERNEL_ADDRESS;

#ifdef SMP
	/*
	 * Steal some virtual address space to map the pcpu area.
	 */
	virtual_avail = roundup2(virtual_avail, PAGE_SIZE * 2);
	pcpup = (struct pcpu *)virtual_avail;
	virtual_avail += PAGE_SIZE * 2;

	/*
	 * Initialize the wired TLB entry mapping the pcpu region for
	 * the BSP at 'pcpup'. Up until this point we were operating
	 * with the 'pcpup' for the BSP pointing to a virtual address
	 * in KSEG0 so there was no need for a TLB mapping.
	 */
	mips_pcpu_tlb_init(PCPU_ADDR(0));

	if (bootverbose)
		printf("pcpu is available at virtual address %p.\n", pcpup);
#endif

	if (need_local_mappings)
		pmap_alloc_lmem_map();
	pmap_create_kernel_pagetable();
	pmap_max_asid = VMNUM_PIDS;
	mips_wr_entryhi(0);
	mips_wr_pagemask(0);

 	/*
	 * Initialize the global pv list lock.
	 */
	rw_init(&pvh_global_lock, "pmap pv global");
}

/*
 * Initialize a vm_page's machine-dependent fields.
 */
void
pmap_page_init(vm_page_t m)
{

	TAILQ_INIT(&m->md.pv_list);
	m->md.pv_flags = VM_MEMATTR_DEFAULT << PV_MEMATTR_SHIFT;
}

/*
 *	Initialize the pmap module.
 *	Called by vm_init, to initialize any structures that the pmap
 *	system needs to map virtual memory.
 */
void
pmap_init(void)
{
}

/***************************************************
 * Low level helper routines.....
 ***************************************************/

#ifdef	SMP
static __inline void
pmap_call_on_active_cpus(pmap_t pmap, void (*fn)(void *), void *arg)
{
	int	cpuid, cpu, self;
	cpuset_t active_cpus;

	sched_pin();
	if (is_kernel_pmap(pmap)) {
		smp_rendezvous(NULL, fn, NULL, arg);
		goto out;
	}
	/* Force ASID update on inactive CPUs */
	CPU_FOREACH(cpu) {
		if (!CPU_ISSET(cpu, &pmap->pm_active))
			pmap->pm_asid[cpu].gen = 0;
	}
	cpuid = PCPU_GET(cpuid);
	/*
	 * XXX: barrier/locking for active?
	 *
	 * Take a snapshot of active here, any further changes are ignored.
	 * tlb update/invalidate should be harmless on inactive CPUs
	 */
	active_cpus = pmap->pm_active;
	self = CPU_ISSET(cpuid, &active_cpus);
	CPU_CLR(cpuid, &active_cpus);
	/* Optimize for the case where this cpu is the only active one */
	if (CPU_EMPTY(&active_cpus)) {
		if (self)
			fn(arg);
	} else {
		if (self)
			CPU_SET(cpuid, &active_cpus);
		smp_rendezvous_cpus(active_cpus, NULL, fn, NULL, arg);
	}
out:
	sched_unpin();
}
#else /* !SMP */
static __inline void
pmap_call_on_active_cpus(pmap_t pmap, void (*fn)(void *), void *arg)
{
	int	cpuid;

	if (is_kernel_pmap(pmap)) {
		fn(arg);
		return;
	}
	cpuid = PCPU_GET(cpuid);
	if (!CPU_ISSET(cpuid, &pmap->pm_active))
		pmap->pm_asid[cpuid].gen = 0;
	else
		fn(arg);
}
#endif /* SMP */

static void
pmap_invalidate_all(pmap_t pmap)
{

	pmap_call_on_active_cpus(pmap,
	    (void (*)(void *))tlb_invalidate_all_user, pmap);
}

struct pmap_invalidate_page_arg {
	pmap_t pmap;
	vm_offset_t va;
};

static void
pmap_invalidate_page_action(void *arg)
{
	struct pmap_invalidate_page_arg *p = arg;

	tlb_invalidate_address(p->pmap, p->va);
}

static void
pmap_invalidate_page(pmap_t pmap, vm_offset_t va)
{
	struct pmap_invalidate_page_arg arg;

	arg.pmap = pmap;
	arg.va = va;
	pmap_call_on_active_cpus(pmap, pmap_invalidate_page_action, &arg);
}

struct pmap_invalidate_range_arg {
	pmap_t pmap;
	vm_offset_t sva;
	vm_offset_t eva;
};

static void
pmap_invalidate_range_action(void *arg)
{
	struct pmap_invalidate_range_arg *p = arg;

	tlb_invalidate_range(p->pmap, p->sva, p->eva);
}

static void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	struct pmap_invalidate_range_arg arg;

	arg.pmap = pmap;
	arg.sva = sva;
	arg.eva = eva;
	pmap_call_on_active_cpus(pmap, pmap_invalidate_range_action, &arg);
}

struct pmap_update_page_arg {
	pmap_t pmap;
	vm_offset_t va;
	pt_entry_t pte;
};

static void
pmap_update_page_action(void *arg)
{
	struct pmap_update_page_arg *p = arg;

	tlb_update(p->pmap, p->va, p->pte);
}

static void
pmap_update_page(pmap_t pmap, vm_offset_t va, pt_entry_t pte)
{
	struct pmap_update_page_arg arg;

	arg.pmap = pmap;
	arg.va = va;
	arg.pte = pte;
	pmap_call_on_active_cpus(pmap, pmap_update_page_action, &arg);
}

/*
 *	Routine:	pmap_extract
 *	Function:
 *		Extract the physical page address associated
 *		with the given map/virtual_address pair.
 */
vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	pt_entry_t *pte;
	vm_offset_t retval = 0;

	PMAP_LOCK(pmap);
	pte = pmap_pte(pmap, va);
	if (pte) {
		retval = TLBLO_PTE_TO_PA(*pte) | (va & PAGE_MASK);
	}
	PMAP_UNLOCK(pmap);
	return (retval);
}

/*
 *	Routine:	pmap_extract_and_hold
 *	Function:
 *		Atomically extract and hold the physical page
 *		with the given pmap and virtual address pair
 *		if that mapping permits the given protection.
 */
vm_page_t
pmap_extract_and_hold(pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{
	pt_entry_t pte, *ptep;
	vm_paddr_t pa, pte_pa;
	vm_page_t m;

	m = NULL;
	pa = 0;
	PMAP_LOCK(pmap);
retry:
	ptep = pmap_pte(pmap, va);
	if (ptep != NULL) {
		pte = *ptep;
		if (pte_test(&pte, PTE_V) && (!pte_test(&pte, PTE_RO) ||
		    (prot & VM_PROT_WRITE) == 0)) {
			pte_pa = TLBLO_PTE_TO_PA(pte);
			if (vm_page_pa_tryrelock(pmap, pte_pa, &pa))
				goto retry;
			m = PHYS_TO_VM_PAGE(pte_pa);
			vm_page_hold(m);
		}
	}
	PA_UNLOCK_COND(pa);
	PMAP_UNLOCK(pmap);
	return (m);
}

/***************************************************
 * Low level mapping routines.....
 ***************************************************/

/*
 * add a wired page to the kva
 */
void
pmap_kenter_attr(vm_offset_t va, vm_paddr_t pa, vm_memattr_t ma)
{
	pt_entry_t *pte;
	pt_entry_t opte, npte;

#ifdef PMAP_DEBUG
	printf("pmap_kenter:  va: %p -> pa: %p\n", (void *)va, (void *)pa);
#endif

	pte = pmap_pte(kernel_pmap, va);
	opte = *pte;
	npte = TLBLO_PA_TO_PFN(pa) | PTE_C(ma) | PTE_D | PTE_V | PTE_G;
	*pte = npte;
	if (pte_test(&opte, PTE_V) && opte != npte)
		pmap_update_page(kernel_pmap, va, npte);
}

void
pmap_kenter(vm_offset_t va, vm_paddr_t pa)
{

	KASSERT(is_cacheable_mem(pa),
		("pmap_kenter: memory at 0x%lx is not cacheable", (u_long)pa));

	pmap_kenter_attr(va, pa, VM_MEMATTR_DEFAULT);
}

/*
 * remove a page from the kernel pagetables
 */
 /* PMAP_INLINE */ void
pmap_kremove(vm_offset_t va)
{
	pt_entry_t *pte;

	/*
	 * Write back all caches from the page being destroyed
	 */
	mips_dcache_wbinv_range_index(va, PAGE_SIZE);

	pte = pmap_pte(kernel_pmap, va);
	*pte = PTE_G;
	pmap_invalidate_page(kernel_pmap, va);
}

/*
 *	Used to map a range of physical addresses into kernel
 *	virtual address space.
 *
 *	The value passed in '*virt' is a suggested virtual address for
 *	the mapping. Architectures which can support a direct-mapped
 *	physical to virtual region can return the appropriate address
 *	within that region, leaving '*virt' unchanged. Other
 *	architectures should map the pages starting at '*virt' and
 *	update '*virt' with the first usable address after the mapped
 *	region.
 *
 *	Use XKPHYS for 64 bit, and KSEG0 where possible for 32 bit.
 */
vm_offset_t
pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end, int prot)
{
	vm_offset_t va, sva;

	if (MIPS_DIRECT_MAPPABLE(end - 1))
		return (MIPS_PHYS_TO_DIRECT(start));

	va = sva = *virt;
	while (start < end) {
		pmap_kenter(va, start);
		va += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	*virt = va;
	return (sva);
}

/*
 * Add a list of wired pages to the kva
 * this routine is only used for temporary
 * kernel mappings that do not need to have
 * page modification or references recorded.
 * Note that old mappings are simply written
 * over.  The page *must* be wired.
 */
void
pmap_qenter(vm_offset_t va, vm_page_t *m, int count)
{
	int i;
	vm_offset_t origva = va;

	for (i = 0; i < count; i++) {
		pmap_flush_pvcache(m[i]);
		pmap_kenter(va, VM_PAGE_TO_PHYS(m[i]));
		va += PAGE_SIZE;
	}

	mips_dcache_wbinv_range_index(origva, PAGE_SIZE*count);
}

/*
 * this routine jerks page mappings from the
 * kernel -- it is meant only for temporary mappings.
 */
void
pmap_qremove(vm_offset_t va, int count)
{
	pt_entry_t *pte;
	vm_offset_t origva;

	if (count < 1)
		return;
	mips_dcache_wbinv_range_index(va, PAGE_SIZE * count);
	origva = va;
	do {
		pte = pmap_pte(kernel_pmap, va);
		*pte = PTE_G;
		va += PAGE_SIZE;
	} while (--count > 0);
	pmap_invalidate_range(kernel_pmap, origva, va);
}

/***************************************************
 * Page table page management routines.....
 ***************************************************/

/*
 * Decrements a page table page's wire count, which is used to record the
 * number of valid page table entries within the page.  If the wire count
 * drops to zero, then the page table page is unmapped.  Returns TRUE if the
 * page table page was unmapped and FALSE otherwise.
 */
static PMAP_INLINE boolean_t
pmap_unwire_ptp(pmap_t pmap, vm_offset_t va, vm_page_t m)
{

	--m->wire_count;
	if (m->wire_count == 0) {
		_pmap_unwire_ptp(pmap, va, m);
		return (TRUE);
	} else
		return (FALSE);
}

static void
_pmap_unwire_ptp(pmap_t pmap, vm_offset_t va, vm_page_t m)
{
	pd_entry_t *pde;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	/*
	 * unmap the page table page
	 */
#ifdef __mips_n64
	if (m->pindex < NUPDE)
		pde = pmap_pde(pmap, va);
	else
		pde = pmap_segmap(pmap, va);
#else
	pde = pmap_pde(pmap, va);
#endif
	*pde = 0;
	pmap->pm_stats.resident_count--;

#ifdef __mips_n64
	if (m->pindex < NUPDE) {
		pd_entry_t *pdp;
		vm_page_t pdpg;

		/*
		 * Recursively decrement next level pagetable refcount
		 */
		pdp = (pd_entry_t *)*pmap_segmap(pmap, va);
		pdpg = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS(pdp));
		pmap_unwire_ptp(pmap, va, pdpg);
	}
#endif

	/*
	 * If the page is finally unwired, simply free it.
	 */
	vm_page_free_zero(m);
	vm_wire_sub(1);
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the hold/wire counts.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, pd_entry_t pde)
{
	vm_page_t mpte;

	if (va >= VM_MAXUSER_ADDRESS)
		return (0);
	KASSERT(pde != 0, ("pmap_unuse_pt: pde != 0"));
	mpte = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS(pde));
	return (pmap_unwire_ptp(pmap, va, mpte));
}

void
pmap_pinit0(pmap_t pmap)
{
	int i;

	PMAP_LOCK_INIT(pmap);
	pmap->pm_segtab = kernel_segmap;
	CPU_ZERO(&pmap->pm_active);
	for (i = 0; i < MAXCPU; i++) {
		pmap->pm_asid[i].asid = PMAP_ASID_RESERVED;
		pmap->pm_asid[i].gen = 0;
	}
	PCPU_SET(curpmap, pmap);
	TAILQ_INIT(&pmap->pm_pvchunk);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);
}

static void
pmap_grow_direct_page(int req)
{

#ifdef __mips_n64
	vm_wait(NULL);
#else
	if (!vm_page_reclaim_contig(req, 1, 0, MIPS_KSEG0_LARGEST_PHYS,
	    PAGE_SIZE, 0))
		vm_wait(NULL);
#endif
}

static vm_page_t
pmap_alloc_direct_page(unsigned int index, int req)
{
	vm_page_t m;

	m = vm_page_alloc_freelist(VM_FREELIST_DIRECT, req | VM_ALLOC_WIRED |
	    VM_ALLOC_ZERO);
	if (m == NULL)
		return (NULL);

	if ((m->flags & PG_ZERO) == 0)
		pmap_zero_page(m);

	m->pindex = index;
	return (m);
}

/*
 * Initialize a preallocated and zeroed pmap structure,
 * such as one in a vmspace structure.
 */
int
pmap_pinit(pmap_t pmap)
{
	vm_offset_t ptdva;
	vm_page_t ptdpg;
	int i, req_class;

	/*
	 * allocate the page directory page
	 */
	req_class = VM_ALLOC_NORMAL;
	while ((ptdpg = pmap_alloc_direct_page(NUSERPGTBLS, req_class)) ==
	    NULL)
		pmap_grow_direct_page(req_class);

	ptdva = MIPS_PHYS_TO_DIRECT(VM_PAGE_TO_PHYS(ptdpg));
	pmap->pm_segtab = (pd_entry_t *)ptdva;
	CPU_ZERO(&pmap->pm_active);
	for (i = 0; i < MAXCPU; i++) {
		pmap->pm_asid[i].asid = PMAP_ASID_RESERVED;
		pmap->pm_asid[i].gen = 0;
	}
	TAILQ_INIT(&pmap->pm_pvchunk);
	bzero(&pmap->pm_stats, sizeof pmap->pm_stats);

	return (1);
}

/*
 * this routine is called if the page table page is not
 * mapped correctly.
 */
static vm_page_t
_pmap_allocpte(pmap_t pmap, unsigned ptepindex, u_int flags)
{
	vm_offset_t pageva;
	vm_page_t m;
	int req_class;

	/*
	 * Find or fabricate a new pagetable page
	 */
	req_class = VM_ALLOC_NORMAL;
	if ((m = pmap_alloc_direct_page(ptepindex, req_class)) == NULL) {
		if ((flags & PMAP_ENTER_NOSLEEP) == 0) {
			PMAP_UNLOCK(pmap);
			rw_wunlock(&pvh_global_lock);
			pmap_grow_direct_page(req_class);
			rw_wlock(&pvh_global_lock);
			PMAP_LOCK(pmap);
		}

		/*
		 * Indicate the need to retry.	While waiting, the page
		 * table page may have been allocated.
		 */
		return (NULL);
	}

	/*
	 * Map the pagetable page into the process address space, if it
	 * isn't already there.
	 */
	pageva = MIPS_PHYS_TO_DIRECT(VM_PAGE_TO_PHYS(m));

#ifdef __mips_n64
	if (ptepindex >= NUPDE) {
		pmap->pm_segtab[ptepindex - NUPDE] = (pd_entry_t)pageva;
	} else {
		pd_entry_t *pdep, *pde;
		int segindex = ptepindex >> (SEGSHIFT - PDRSHIFT);
		int pdeindex = ptepindex & (NPDEPG - 1);
		vm_page_t pg;

		pdep = &pmap->pm_segtab[segindex];
		if (*pdep == NULL) {
			/* recurse for allocating page dir */
			if (_pmap_allocpte(pmap, NUPDE + segindex,
			    flags) == NULL) {
				/* alloc failed, release current */
				vm_page_unwire_noq(m);
				vm_page_free_zero(m);
				return (NULL);
			}
		} else {
			pg = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS(*pdep));
			pg->wire_count++;
		}
		/* Next level entry */
		pde = (pd_entry_t *)*pdep;
		pde[pdeindex] = (pd_entry_t)pageva;
	}
#else
	pmap->pm_segtab[ptepindex] = (pd_entry_t)pageva;
#endif
	pmap->pm_stats.resident_count++;
	return (m);
}

static vm_page_t
pmap_allocpte(pmap_t pmap, vm_offset_t va, u_int flags)
{
	unsigned ptepindex;
	pd_entry_t *pde;
	vm_page_t m;

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = pmap_pde_pindex(va);
retry:
	/*
	 * Get the page directory entry
	 */
	pde = pmap_pde(pmap, va);

	/*
	 * If the page table page is mapped, we just increment the hold
	 * count, and activate it.
	 */
	if (pde != NULL && *pde != NULL) {
		m = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS(*pde));
		m->wire_count++;
	} else {
		/*
		 * Here if the pte page isn't mapped, or if it has been
		 * deallocated.
		 */
		m = _pmap_allocpte(pmap, ptepindex, flags);
		if (m == NULL && (flags & PMAP_ENTER_NOSLEEP) == 0)
			goto retry;
	}
	return (m);
}


/***************************************************
 * Pmap allocation/deallocation routines.
 ***************************************************/

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_release(pmap_t pmap)
{
	vm_offset_t ptdva;
	vm_page_t ptdpg;

	KASSERT(pmap->pm_stats.resident_count == 0,
	    ("pmap_release: pmap resident count %ld != 0",
	    pmap->pm_stats.resident_count));

	ptdva = (vm_offset_t)pmap->pm_segtab;
	ptdpg = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS(ptdva));

	vm_page_unwire_noq(ptdpg);
	vm_page_free_zero(ptdpg);
}

/*
 * grow the number of kernel page table entries, if needed
 */
void
pmap_growkernel(vm_offset_t addr)
{
	vm_page_t nkpg;
	pd_entry_t *pde, *pdpe;
	pt_entry_t *pte;
	int i, req_class;

	mtx_assert(&kernel_map->system_mtx, MA_OWNED);
	req_class = VM_ALLOC_INTERRUPT;
	addr = roundup2(addr, NBSEG);
	if (addr - 1 >= vm_map_max(kernel_map))
		addr = vm_map_max(kernel_map);
	while (kernel_vm_end < addr) {
		pdpe = pmap_segmap(kernel_pmap, kernel_vm_end);
#ifdef __mips_n64
		if (*pdpe == 0) {
			/* new intermediate page table entry */
			nkpg = pmap_alloc_direct_page(nkpt, req_class);
			if (nkpg == NULL)
				panic("pmap_growkernel: no memory to grow kernel");
			*pdpe = (pd_entry_t)MIPS_PHYS_TO_DIRECT(VM_PAGE_TO_PHYS(nkpg));
			continue; /* try again */
		}
#endif
		pde = pmap_pdpe_to_pde(pdpe, kernel_vm_end);
		if (*pde != 0) {
			kernel_vm_end = (kernel_vm_end + NBPDR) & ~PDRMASK;
			if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
				kernel_vm_end = vm_map_max(kernel_map);
				break;
			}
			continue;
		}

		/*
		 * This index is bogus, but out of the way
		 */
		nkpg = pmap_alloc_direct_page(nkpt, req_class);
#ifndef __mips_n64
		if (nkpg == NULL && vm_page_reclaim_contig(req_class, 1,
		    0, MIPS_KSEG0_LARGEST_PHYS, PAGE_SIZE, 0))
			nkpg = pmap_alloc_direct_page(nkpt, req_class);
#endif
		if (nkpg == NULL)
			panic("pmap_growkernel: no memory to grow kernel");
		nkpt++;
		*pde = (pd_entry_t)MIPS_PHYS_TO_DIRECT(VM_PAGE_TO_PHYS(nkpg));

		/*
		 * The R[4-7]?00 stores only one copy of the Global bit in
		 * the translation lookaside buffer for each 2 page entry.
		 * Thus invalid entrys must have the Global bit set so when
		 * Entry LO and Entry HI G bits are anded together they will
		 * produce a global bit to store in the tlb.
		 */
		pte = (pt_entry_t *)*pde;
		for (i = 0; i < NPTEPG; i++)
			pte[i] = PTE_G;

		kernel_vm_end = (kernel_vm_end + NBPDR) & ~PDRMASK;
		if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
			kernel_vm_end = vm_map_max(kernel_map);
			break;
		}
	}
}

/***************************************************
 * page management routines.
 ***************************************************/

CTASSERT(sizeof(struct pv_chunk) == PAGE_SIZE);
#ifdef __mips_n64
CTASSERT(_NPCM == 3);
CTASSERT(_NPCPV == 168);
#else
CTASSERT(_NPCM == 11);
CTASSERT(_NPCPV == 336);
#endif

static __inline struct pv_chunk *
pv_to_chunk(pv_entry_t pv)
{

	return ((struct pv_chunk *)((uintptr_t)pv & ~(uintptr_t)PAGE_MASK));
}

#define PV_PMAP(pv) (pv_to_chunk(pv)->pc_pmap)

#ifdef __mips_n64
#define	PC_FREE0_1	0xfffffffffffffffful
#define	PC_FREE2	0x000000fffffffffful
#else
#define	PC_FREE0_9	0xfffffffful	/* Free values for index 0 through 9 */
#define	PC_FREE10	0x0000fffful	/* Free values for index 10 */
#endif

static const u_long pc_freemask[_NPCM] = {
#ifdef __mips_n64
	PC_FREE0_1, PC_FREE0_1, PC_FREE2
#else
	PC_FREE0_9, PC_FREE0_9, PC_FREE0_9,
	PC_FREE0_9, PC_FREE0_9, PC_FREE0_9,
	PC_FREE0_9, PC_FREE0_9, PC_FREE0_9,
	PC_FREE0_9, PC_FREE10
#endif
};

static SYSCTL_NODE(_vm, OID_AUTO, pmap, CTLFLAG_RD, 0, "VM/pmap parameters");

SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_count, CTLFLAG_RD, &pv_entry_count, 0,
    "Current number of pv entries");

#ifdef PV_STATS
static int pc_chunk_count, pc_chunk_allocs, pc_chunk_frees, pc_chunk_tryfail;

SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_count, CTLFLAG_RD, &pc_chunk_count, 0,
    "Current number of pv entry chunks");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_allocs, CTLFLAG_RD, &pc_chunk_allocs, 0,
    "Current number of pv entry chunks allocated");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_frees, CTLFLAG_RD, &pc_chunk_frees, 0,
    "Current number of pv entry chunks frees");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_tryfail, CTLFLAG_RD, &pc_chunk_tryfail, 0,
    "Number of times tried to get a chunk page but failed.");

static long pv_entry_frees, pv_entry_allocs;
static int pv_entry_spare;

SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_frees, CTLFLAG_RD, &pv_entry_frees, 0,
    "Current number of pv entry frees");
SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_allocs, CTLFLAG_RD, &pv_entry_allocs, 0,
    "Current number of pv entry allocs");
SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_spare, CTLFLAG_RD, &pv_entry_spare, 0,
    "Current number of spare pv entries");
#endif

/*
 * We are in a serious low memory condition.  Resort to
 * drastic measures to free some pages so we can allocate
 * another pv entry chunk.
 */
static vm_page_t
pmap_pv_reclaim(pmap_t locked_pmap)
{
	struct pch newtail;
	struct pv_chunk *pc;
	pd_entry_t *pde;
	pmap_t pmap;
	pt_entry_t *pte, oldpte;
	pv_entry_t pv;
	vm_offset_t va;
	vm_page_t m, m_pc;
	u_long inuse;
	int bit, field, freed, idx;

	PMAP_LOCK_ASSERT(locked_pmap, MA_OWNED);
	pmap = NULL;
	m_pc = NULL;
	TAILQ_INIT(&newtail);
	while ((pc = TAILQ_FIRST(&pv_chunks)) != NULL) {
		TAILQ_REMOVE(&pv_chunks, pc, pc_lru);
		if (pmap != pc->pc_pmap) {
			if (pmap != NULL) {
				pmap_invalidate_all(pmap);
				if (pmap != locked_pmap)
					PMAP_UNLOCK(pmap);
			}
			pmap = pc->pc_pmap;
			/* Avoid deadlock and lock recursion. */
			if (pmap > locked_pmap)
				PMAP_LOCK(pmap);
			else if (pmap != locked_pmap && !PMAP_TRYLOCK(pmap)) {
				pmap = NULL;
				TAILQ_INSERT_TAIL(&newtail, pc, pc_lru);
				continue;
			}
		}

		/*
		 * Destroy every non-wired, 4 KB page mapping in the chunk.
		 */
		freed = 0;
		for (field = 0; field < _NPCM; field++) {
			for (inuse = ~pc->pc_map[field] & pc_freemask[field];
			    inuse != 0; inuse &= ~(1UL << bit)) {
				bit = ffsl(inuse) - 1;
				idx = field * sizeof(inuse) * NBBY + bit;
				pv = &pc->pc_pventry[idx];
				va = pv->pv_va;
				pde = pmap_pde(pmap, va);
				KASSERT(pde != NULL && *pde != 0,
				    ("pmap_pv_reclaim: pde"));
				pte = pmap_pde_to_pte(pde, va);
				oldpte = *pte;
				if (pte_test(&oldpte, PTE_W))
					continue;
				if (is_kernel_pmap(pmap))
					*pte = PTE_G;
				else
					*pte = 0;
				m = PHYS_TO_VM_PAGE(TLBLO_PTE_TO_PA(oldpte));
				if (pte_test(&oldpte, PTE_D))
					vm_page_dirty(m);
				if (m->md.pv_flags & PV_TABLE_REF)
					vm_page_aflag_set(m, PGA_REFERENCED);
				m->md.pv_flags &= ~PV_TABLE_REF;
				TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
				if (TAILQ_EMPTY(&m->md.pv_list))
					vm_page_aflag_clear(m, PGA_WRITEABLE);
				pc->pc_map[field] |= 1UL << bit;
				pmap_unuse_pt(pmap, va, *pde);
				freed++;
			}
		}
		if (freed == 0) {
			TAILQ_INSERT_TAIL(&newtail, pc, pc_lru);
			continue;
		}
		/* Every freed mapping is for a 4 KB page. */
		pmap->pm_stats.resident_count -= freed;
		PV_STAT(pv_entry_frees += freed);
		PV_STAT(pv_entry_spare += freed);
		pv_entry_count -= freed;
		TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
		for (field = 0; field < _NPCM; field++)
			if (pc->pc_map[field] != pc_freemask[field]) {
				TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc,
				    pc_list);
				TAILQ_INSERT_TAIL(&newtail, pc, pc_lru);

				/*
				 * One freed pv entry in locked_pmap is
				 * sufficient.
				 */
				if (pmap == locked_pmap)
					goto out;
				break;
			}
		if (field == _NPCM) {
			PV_STAT(pv_entry_spare -= _NPCPV);
			PV_STAT(pc_chunk_count--);
			PV_STAT(pc_chunk_frees++);
			/* Entire chunk is free; return it. */
			m_pc = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS(
			    (vm_offset_t)pc));
			dump_drop_page(m_pc->phys_addr);
			break;
		}
	}
out:
	TAILQ_CONCAT(&pv_chunks, &newtail, pc_lru);
	if (pmap != NULL) {
		pmap_invalidate_all(pmap);
		if (pmap != locked_pmap)
			PMAP_UNLOCK(pmap);
	}
	return (m_pc);
}

/*
 * free the pv_entry back to the free list
 */
static void
free_pv_entry(pmap_t pmap, pv_entry_t pv)
{
	struct pv_chunk *pc;
	int bit, field, idx;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	PV_STAT(pv_entry_frees++);
	PV_STAT(pv_entry_spare++);
	pv_entry_count--;
	pc = pv_to_chunk(pv);
	idx = pv - &pc->pc_pventry[0];
	field = idx / (sizeof(u_long) * NBBY);
	bit = idx % (sizeof(u_long) * NBBY);
	pc->pc_map[field] |= 1ul << bit;
	for (idx = 0; idx < _NPCM; idx++)
		if (pc->pc_map[idx] != pc_freemask[idx]) {
			/*
			 * 98% of the time, pc is already at the head of the
			 * list.  If it isn't already, move it to the head.
			 */
			if (__predict_false(TAILQ_FIRST(&pmap->pm_pvchunk) !=
			    pc)) {
				TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
				TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc,
				    pc_list);
			}
			return;
		}
	TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
	free_pv_chunk(pc);
}

static void
free_pv_chunk(struct pv_chunk *pc)
{
	vm_page_t m;

 	TAILQ_REMOVE(&pv_chunks, pc, pc_lru);
	PV_STAT(pv_entry_spare -= _NPCPV);
	PV_STAT(pc_chunk_count--);
	PV_STAT(pc_chunk_frees++);
	/* entire chunk is free, return it */
	m = PHYS_TO_VM_PAGE(MIPS_DIRECT_TO_PHYS((vm_offset_t)pc));
	dump_drop_page(m->phys_addr);
	vm_page_unwire(m, PQ_NONE);
	vm_page_free(m);
}

/*
 * get a new pv_entry, allocating a block from the system
 * when needed.
 */
static pv_entry_t
get_pv_entry(pmap_t pmap, boolean_t try)
{
	struct pv_chunk *pc;
	pv_entry_t pv;
	vm_page_t m;
	int bit, field, idx;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	PV_STAT(pv_entry_allocs++);
	pv_entry_count++;
retry:
	pc = TAILQ_FIRST(&pmap->pm_pvchunk);
	if (pc != NULL) {
		for (field = 0; field < _NPCM; field++) {
			if (pc->pc_map[field]) {
				bit = ffsl(pc->pc_map[field]) - 1;
				break;
			}
		}
		if (field < _NPCM) {
			idx = field * sizeof(pc->pc_map[field]) * NBBY + bit;
			pv = &pc->pc_pventry[idx];
			pc->pc_map[field] &= ~(1ul << bit);
			/* If this was the last item, move it to tail */
			for (field = 0; field < _NPCM; field++)
				if (pc->pc_map[field] != 0) {
					PV_STAT(pv_entry_spare--);
					return (pv);	/* not full, return */
				}
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			TAILQ_INSERT_TAIL(&pmap->pm_pvchunk, pc, pc_list);
			PV_STAT(pv_entry_spare--);
			return (pv);
		}
	}
	/* No free items, allocate another chunk */
	m = vm_page_alloc_freelist(VM_FREELIST_DIRECT, VM_ALLOC_NORMAL |
	    VM_ALLOC_WIRED);
	if (m == NULL) {
		if (try) {
			pv_entry_count--;
			PV_STAT(pc_chunk_tryfail++);
			return (NULL);
		}
		m = pmap_pv_reclaim(pmap);
		if (m == NULL)
			goto retry;
	}
	PV_STAT(pc_chunk_count++);
	PV_STAT(pc_chunk_allocs++);
	dump_add_page(m->phys_addr);
	pc = (struct pv_chunk *)MIPS_PHYS_TO_DIRECT(VM_PAGE_TO_PHYS(m));
	pc->pc_pmap = pmap;
	pc->pc_map[0] = pc_freemask[0] & ~1ul;	/* preallocated bit 0 */
	for (field = 1; field < _NPCM; field++)
		pc->pc_map[field] = pc_freemask[field];
	TAILQ_INSERT_TAIL(&pv_chunks, pc, pc_lru);
	pv = &pc->pc_pventry[0];
	TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc, pc_list);
	PV_STAT(pv_entry_spare += _NPCPV - 1);
	return (pv);
}

static pv_entry_t
pmap_pvh_remove(struct md_page *pvh, pmap_t pmap, vm_offset_t va)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_list) {
		if (pmap == PV_PMAP(pv) && va == pv->pv_va) {
			TAILQ_REMOVE(&pvh->pv_list, pv, pv_list);
			break;
		}
	}
	return (pv);
}

static void
pmap_pvh_free(struct md_page *pvh, pmap_t pmap, vm_offset_t va)
{
	pv_entry_t pv;

	pv = pmap_pvh_remove(pvh, pmap, va);
	KASSERT(pv != NULL, ("pmap_pvh_free: pv not found, pa %lx va %lx",
	     (u_long)VM_PAGE_TO_PHYS(__containerof(pvh, struct vm_page, md)),
	     (u_long)va));
	free_pv_entry(pmap, pv);
}

static void
pmap_remove_entry(pmap_t pmap, vm_page_t m, vm_offset_t va)
{

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	pmap_pvh_free(&m->md, pmap, va);
	if (TAILQ_EMPTY(&m->md.pv_list))
		vm_page_aflag_clear(m, PGA_WRITEABLE);
}

/*
 * Conditionally create a pv entry.
 */
static boolean_t
pmap_try_insert_pv_entry(pmap_t pmap, vm_page_t mpte, vm_offset_t va,
    vm_page_t m)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if ((pv = get_pv_entry(pmap, TRUE)) != NULL) {
		pv->pv_va = va;
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_list);
		return (TRUE);
	} else
		return (FALSE);
}

/*
 * pmap_remove_pte: do the things to unmap a page in a process
 */
static int
pmap_remove_pte(struct pmap *pmap, pt_entry_t *ptq, vm_offset_t va,
    pd_entry_t pde)
{
	pt_entry_t oldpte;
	vm_page_t m;
	vm_paddr_t pa;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * Write back all cache lines from the page being unmapped.
	 */
	mips_dcache_wbinv_range_index(va, PAGE_SIZE);

	oldpte = *ptq;
	if (is_kernel_pmap(pmap))
		*ptq = PTE_G;
	else
		*ptq = 0;

	if (pte_test(&oldpte, PTE_W))
		pmap->pm_stats.wired_count -= 1;

	pmap->pm_stats.resident_count -= 1;

	if (pte_test(&oldpte, PTE_MANAGED)) {
		pa = TLBLO_PTE_TO_PA(oldpte);
		m = PHYS_TO_VM_PAGE(pa);
		if (pte_test(&oldpte, PTE_D)) {
			KASSERT(!pte_test(&oldpte, PTE_RO),
			    ("%s: modified page not writable: va: %p, pte: %#jx",
			    __func__, (void *)va, (uintmax_t)oldpte));
			vm_page_dirty(m);
		}
		if (m->md.pv_flags & PV_TABLE_REF)
			vm_page_aflag_set(m, PGA_REFERENCED);
		m->md.pv_flags &= ~PV_TABLE_REF;

		pmap_remove_entry(pmap, m, va);
	}
	return (pmap_unuse_pt(pmap, va, pde));
}

/*
 * Remove a single page from a process address space
 */
static void
pmap_remove_page(struct pmap *pmap, vm_offset_t va)
{
	pd_entry_t *pde;
	pt_entry_t *ptq;

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pde = pmap_pde(pmap, va);
	if (pde == NULL || *pde == 0)
		return;
	ptq = pmap_pde_to_pte(pde, va);

	/*
	 * If there is no pte for this address, just skip it!
	 */
	if (!pte_test(ptq, PTE_V))
		return;

	(void)pmap_remove_pte(pmap, ptq, va, *pde);
	pmap_invalidate_page(pmap, va);
}

/*
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 */
void
pmap_remove(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	pd_entry_t *pde, *pdpe;
	pt_entry_t *pte;
	vm_offset_t va, va_next;

	/*
	 * Perform an unsynchronized read.  This is, however, safe.
	 */
	if (pmap->pm_stats.resident_count == 0)
		return;

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);

	/*
	 * special handling of removing one page.  a very common operation
	 * and easy to short circuit some code.
	 */
	if ((sva + PAGE_SIZE) == eva) {
		pmap_remove_page(pmap, sva);
		goto out;
	}
	for (; sva < eva; sva = va_next) {
		pdpe = pmap_segmap(pmap, sva);
#ifdef __mips_n64
		if (*pdpe == 0) {
			va_next = (sva + NBSEG) & ~SEGMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}
#endif
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;

		pde = pmap_pdpe_to_pde(pdpe, sva);
		if (*pde == NULL)
			continue;

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
		if (va_next > eva)
			va_next = eva;

		va = va_next;
		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			if (!pte_test(pte, PTE_V)) {
				if (va != va_next) {
					pmap_invalidate_range(pmap, va, sva);
					va = va_next;
				}
				continue;
			}
			if (va == va_next)
				va = sva;
			if (pmap_remove_pte(pmap, pte, sva, *pde)) {
				sva += PAGE_SIZE;
				break;
			}
		}
		if (va != va_next)
			pmap_invalidate_range(pmap, va, sva);
	}
out:
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 *	Routine:	pmap_remove_all
 *	Function:
 *		Removes this physical page from
 *		all physical maps in which it resides.
 *		Reflects back modify bits to the pager.
 *
 *	Notes:
 *		Original versions of this routine were very
 *		inefficient because they iteratively called
 *		pmap_remove (slow...)
 */

void
pmap_remove_all(vm_page_t m)
{
	pv_entry_t pv;
	pmap_t pmap;
	pd_entry_t *pde;
	pt_entry_t *pte, tpte;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_all: page %p is not managed", m));
	rw_wlock(&pvh_global_lock);

	if (m->md.pv_flags & PV_TABLE_REF)
		vm_page_aflag_set(m, PGA_REFERENCED);

	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);

		/*
		 * If it's last mapping writeback all caches from
		 * the page being destroyed
	 	 */
		if (TAILQ_NEXT(pv, pv_list) == NULL)
			mips_dcache_wbinv_range_index(pv->pv_va, PAGE_SIZE);

		pmap->pm_stats.resident_count--;

		pde = pmap_pde(pmap, pv->pv_va);
		KASSERT(pde != NULL && *pde != 0, ("pmap_remove_all: pde"));
		pte = pmap_pde_to_pte(pde, pv->pv_va);

		tpte = *pte;
		if (is_kernel_pmap(pmap))
			*pte = PTE_G;
		else
			*pte = 0;

		if (pte_test(&tpte, PTE_W))
			pmap->pm_stats.wired_count--;

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if (pte_test(&tpte, PTE_D)) {
			KASSERT(!pte_test(&tpte, PTE_RO),
			    ("%s: modified page not writable: va: %p, pte: %#jx",
			    __func__, (void *)pv->pv_va, (uintmax_t)tpte));
			vm_page_dirty(m);
		}
		pmap_invalidate_page(pmap, pv->pv_va);

		TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
		pmap_unuse_pt(pmap, pv->pv_va, *pde);
		free_pv_entry(pmap, pv);
		PMAP_UNLOCK(pmap);
	}

	vm_page_aflag_clear(m, PGA_WRITEABLE);
	m->md.pv_flags &= ~PV_TABLE_REF;
	rw_wunlock(&pvh_global_lock);
}

/*
 *	Set the physical protection on the
 *	specified range of this map as requested.
 */
void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
	pt_entry_t pbits, *pte;
	pd_entry_t *pde, *pdpe;
	vm_offset_t va, va_next;
	vm_paddr_t pa;
	vm_page_t m;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}
	if (prot & VM_PROT_WRITE)
		return;

	PMAP_LOCK(pmap);
	for (; sva < eva; sva = va_next) {
		pdpe = pmap_segmap(pmap, sva);
#ifdef __mips_n64
		if (*pdpe == 0) {
			va_next = (sva + NBSEG) & ~SEGMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}
#endif
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;

		pde = pmap_pdpe_to_pde(pdpe, sva);
		if (*pde == NULL)
			continue;

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being write protected.
		 */
		if (va_next > eva)
			va_next = eva;

		va = va_next;
		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			pbits = *pte;
			if (!pte_test(&pbits, PTE_V) || pte_test(&pbits,
			    PTE_RO)) {
				if (va != va_next) {
					pmap_invalidate_range(pmap, va, sva);
					va = va_next;
				}
				continue;
			}
			pte_set(&pbits, PTE_RO);
			if (pte_test(&pbits, PTE_D)) {
				pte_clear(&pbits, PTE_D);
				if (pte_test(&pbits, PTE_MANAGED)) {
					pa = TLBLO_PTE_TO_PA(pbits);
					m = PHYS_TO_VM_PAGE(pa);
					vm_page_dirty(m);
				}
				if (va == va_next)
					va = sva;
			} else {
				/*
				 * Unless PTE_D is set, any TLB entries
				 * mapping "sva" don't allow write access, so
				 * they needn't be invalidated.
				 */
				if (va != va_next) {
					pmap_invalidate_range(pmap, va, sva);
					va = va_next;
				}
			}
			*pte = pbits;
		}
		if (va != va_next)
			pmap_invalidate_range(pmap, va, sva);
	}
	PMAP_UNLOCK(pmap);
}

/*
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	If specified, the page will be wired down, meaning
 *	that the related pte can not be reclaimed.
 *
 *	NB:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */
int
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind __unused)
{
	vm_paddr_t pa, opa;
	pt_entry_t *pte;
	pt_entry_t origpte, newpte;
	pv_entry_t pv;
	vm_page_t mpte, om;

	va &= ~PAGE_MASK;
 	KASSERT(va <= VM_MAX_KERNEL_ADDRESS, ("pmap_enter: toobig"));
	KASSERT((m->oflags & VPO_UNMANAGED) != 0 || va < kmi.clean_sva ||
	    va >= kmi.clean_eva,
	    ("pmap_enter: managed mapping within the clean submap"));
	if ((m->oflags & VPO_UNMANAGED) == 0 && !vm_page_xbusied(m))
		VM_OBJECT_ASSERT_LOCKED(m->object);
	pa = VM_PAGE_TO_PHYS(m);
	newpte = TLBLO_PA_TO_PFN(pa) | init_pte_prot(m, flags, prot);
	if ((flags & PMAP_ENTER_WIRED) != 0)
		newpte |= PTE_W;
	if (is_kernel_pmap(pmap))
		newpte |= PTE_G;
	PMAP_PTE_SET_CACHE_BITS(newpte, pa, m);
	if ((m->oflags & VPO_UNMANAGED) == 0)
		newpte |= PTE_MANAGED;

	mpte = NULL;

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);

	/*
	 * In the case that a page table page is not resident, we are
	 * creating it here.
	 */
	if (va < VM_MAXUSER_ADDRESS) {
		mpte = pmap_allocpte(pmap, va, flags);
		if (mpte == NULL) {
			KASSERT((flags & PMAP_ENTER_NOSLEEP) != 0,
			    ("pmap_allocpte failed with sleep allowed"));
			rw_wunlock(&pvh_global_lock);
			PMAP_UNLOCK(pmap);
			return (KERN_RESOURCE_SHORTAGE);
		}
	}
	pte = pmap_pte(pmap, va);

	/*
	 * Page Directory table entry not valid, we need a new PT page
	 */
	if (pte == NULL) {
		panic("pmap_enter: invalid page directory, pdir=%p, va=%p",
		    (void *)pmap->pm_segtab, (void *)va);
	}

	origpte = *pte;
	KASSERT(!pte_test(&origpte, PTE_D | PTE_RO | PTE_V),
	    ("pmap_enter: modified page not writable: va: %p, pte: %#jx",
	    (void *)va, (uintmax_t)origpte));
	opa = TLBLO_PTE_TO_PA(origpte);

	/*
	 * Mapping has not changed, must be protection or wiring change.
	 */
	if (pte_test(&origpte, PTE_V) && opa == pa) {
		/*
		 * Wiring change, just update stats. We don't worry about
		 * wiring PT pages as they remain resident as long as there
		 * are valid mappings in them. Hence, if a user page is
		 * wired, the PT page will be also.
		 */
		if (pte_test(&newpte, PTE_W) && !pte_test(&origpte, PTE_W))
			pmap->pm_stats.wired_count++;
		else if (!pte_test(&newpte, PTE_W) && pte_test(&origpte,
		    PTE_W))
			pmap->pm_stats.wired_count--;

		/*
		 * Remove extra pte reference
		 */
		if (mpte)
			mpte->wire_count--;

		if (pte_test(&origpte, PTE_MANAGED)) {
			m->md.pv_flags |= PV_TABLE_REF;
			if (!pte_test(&newpte, PTE_RO))
				vm_page_aflag_set(m, PGA_WRITEABLE);
		}
		goto validate;
	}

	pv = NULL;

	/*
	 * Mapping has changed, invalidate old range and fall through to
	 * handle validating new mapping.
	 */
	if (opa) {
		if (is_kernel_pmap(pmap))
			*pte = PTE_G;
		else
			*pte = 0;
		if (pte_test(&origpte, PTE_W))
			pmap->pm_stats.wired_count--;
		if (pte_test(&origpte, PTE_MANAGED)) {
			om = PHYS_TO_VM_PAGE(opa);
			if (pte_test(&origpte, PTE_D))
				vm_page_dirty(om);
			if ((om->md.pv_flags & PV_TABLE_REF) != 0) {
				om->md.pv_flags &= ~PV_TABLE_REF;
				vm_page_aflag_set(om, PGA_REFERENCED);
			}
			pv = pmap_pvh_remove(&om->md, pmap, va);
			if (!pte_test(&newpte, PTE_MANAGED))
				free_pv_entry(pmap, pv);
			if ((om->aflags & PGA_WRITEABLE) != 0 &&
			    TAILQ_EMPTY(&om->md.pv_list))
				vm_page_aflag_clear(om, PGA_WRITEABLE);
		}
		pmap_invalidate_page(pmap, va);
		origpte = 0;
		if (mpte != NULL) {
			mpte->wire_count--;
			KASSERT(mpte->wire_count > 0,
			    ("pmap_enter: missing reference to page table page,"
			    " va: %p", (void *)va));
		}
	} else
		pmap->pm_stats.resident_count++;

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if (pte_test(&newpte, PTE_MANAGED)) {
		m->md.pv_flags |= PV_TABLE_REF;
		if (pv == NULL) {
			pv = get_pv_entry(pmap, FALSE);
			pv->pv_va = va;
		}
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_list);
		if (!pte_test(&newpte, PTE_RO))
			vm_page_aflag_set(m, PGA_WRITEABLE);
	}

	/*
	 * Increment counters
	 */
	if (pte_test(&newpte, PTE_W))
		pmap->pm_stats.wired_count++;

validate:

#ifdef PMAP_DEBUG
	printf("pmap_enter:  va: %p -> pa: %p\n", (void *)va, (void *)pa);
#endif

	/*
	 * if the mapping or permission bits are different, we need to
	 * update the pte.
	 */
	if (origpte != newpte) {
		*pte = newpte;
		if (pte_test(&origpte, PTE_V)) {
			KASSERT(opa == pa, ("pmap_enter: invalid update"));
			if (pte_test(&origpte, PTE_D)) {
				if (pte_test(&origpte, PTE_MANAGED))
					vm_page_dirty(m);
			}
			pmap_update_page(pmap, va, newpte);
		}
	}

	/*
	 * Sync I & D caches for executable pages.  Do this only if the
	 * target pmap belongs to the current process.  Otherwise, an
	 * unresolvable TLB miss may occur.
	 */
	if (!is_kernel_pmap(pmap) && (pmap == &curproc->p_vmspace->vm_pmap) &&
	    (prot & VM_PROT_EXECUTE)) {
		mips_icache_sync_range(va, PAGE_SIZE);
		mips_dcache_wbinv_range(va, PAGE_SIZE);
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	return (KERN_SUCCESS);
}

/*
 * this code makes some *MAJOR* assumptions:
 * 1. Current pmap & pmap exists.
 * 2. Not wired.
 * 3. Read access.
 * 4. No page table pages.
 * but is *MUCH* faster than pmap_enter...
 */

void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{

	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	(void)pmap_enter_quick_locked(pmap, va, m, prot, NULL);
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

static vm_page_t
pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, vm_page_t mpte)
{
	pt_entry_t *pte, npte;
	vm_paddr_t pa;

	KASSERT(va < kmi.clean_sva || va >= kmi.clean_eva ||
	    (m->oflags & VPO_UNMANAGED) != 0,
	    ("pmap_enter_quick_locked: managed mapping within the clean submap"));
	rw_assert(&pvh_global_lock, RA_WLOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * In the case that a page table page is not resident, we are
	 * creating it here.
	 */
	if (va < VM_MAXUSER_ADDRESS) {
		pd_entry_t *pde;
		unsigned ptepindex;

		/*
		 * Calculate pagetable page index
		 */
		ptepindex = pmap_pde_pindex(va);
		if (mpte && (mpte->pindex == ptepindex)) {
			mpte->wire_count++;
		} else {
			/*
			 * Get the page directory entry
			 */
			pde = pmap_pde(pmap, va);

			/*
			 * If the page table page is mapped, we just
			 * increment the hold count, and activate it.
			 */
			if (pde && *pde != 0) {
				mpte = PHYS_TO_VM_PAGE(
				    MIPS_DIRECT_TO_PHYS(*pde));
				mpte->wire_count++;
			} else {
				mpte = _pmap_allocpte(pmap, ptepindex,
				    PMAP_ENTER_NOSLEEP);
				if (mpte == NULL)
					return (mpte);
			}
		}
	} else {
		mpte = NULL;
	}

	pte = pmap_pte(pmap, va);
	if (pte_test(pte, PTE_V)) {
		if (mpte != NULL) {
			mpte->wire_count--;
			mpte = NULL;
		}
		return (mpte);
	}

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0 &&
	    !pmap_try_insert_pv_entry(pmap, mpte, va, m)) {
		if (mpte != NULL) {
			pmap_unwire_ptp(pmap, va, mpte);
			mpte = NULL;
		}
		return (mpte);
	}

	/*
	 * Increment counters
	 */
	pmap->pm_stats.resident_count++;

	pa = VM_PAGE_TO_PHYS(m);

	/*
	 * Now validate mapping with RO protection
	 */
	npte = PTE_RO | TLBLO_PA_TO_PFN(pa) | PTE_V;
	if ((m->oflags & VPO_UNMANAGED) == 0)
		npte |= PTE_MANAGED;

	PMAP_PTE_SET_CACHE_BITS(npte, pa, m);

	if (is_kernel_pmap(pmap))
		*pte = npte | PTE_G;
	else {
		*pte = npte;
		/*
		 * Sync I & D caches.  Do this only if the target pmap
		 * belongs to the current process.  Otherwise, an
		 * unresolvable TLB miss may occur. */
		if (pmap == &curproc->p_vmspace->vm_pmap) {
			va &= ~PAGE_MASK;
			mips_icache_sync_range(va, PAGE_SIZE);
			mips_dcache_wbinv_range(va, PAGE_SIZE);
		}
	}
	return (mpte);
}

/*
 * Make a temporary mapping for a physical address.  This is only intended
 * to be used for panic dumps.
 *
 * Use XKPHYS for 64 bit, and KSEG0 where possible for 32 bit.
 */
void *
pmap_kenter_temporary(vm_paddr_t pa, int i)
{
	vm_offset_t va;

	if (i != 0)
		printf("%s: ERROR!!! More than one page of virtual address mapping not supported\n",
		    __func__);

	if (MIPS_DIRECT_MAPPABLE(pa)) {
		va = MIPS_PHYS_TO_DIRECT(pa);
	} else {
#ifndef __mips_n64    /* XXX : to be converted to new style */
		int cpu;
		register_t intr;
		struct local_sysmaps *sysm;
		pt_entry_t *pte, npte;

		/* If this is used other than for dumps, we may need to leave
		 * interrupts disasbled on return. If crash dumps don't work when
		 * we get to this point, we might want to consider this (leaving things
		 * disabled as a starting point ;-)
	 	 */
		intr = intr_disable();
		cpu = PCPU_GET(cpuid);
		sysm = &sysmap_lmem[cpu];
		/* Since this is for the debugger, no locks or any other fun */
		npte = TLBLO_PA_TO_PFN(pa) | PTE_C_CACHE | PTE_D | PTE_V |
		    PTE_G;
		pte = pmap_pte(kernel_pmap, sysm->base);
		*pte = npte;
		sysm->valid1 = 1;
		pmap_update_page(kernel_pmap, sysm->base, npte);
		va = sysm->base;
		intr_restore(intr);
#endif
	}
	return ((void *)va);
}

void
pmap_kenter_temporary_free(vm_paddr_t pa)
{
#ifndef __mips_n64    /* XXX : to be converted to new style */
	int cpu;
	register_t intr;
	struct local_sysmaps *sysm;
#endif

	if (MIPS_DIRECT_MAPPABLE(pa)) {
		/* nothing to do for this case */
		return;
	}
#ifndef __mips_n64    /* XXX : to be converted to new style */
	cpu = PCPU_GET(cpuid);
	sysm = &sysmap_lmem[cpu];
	if (sysm->valid1) {
		pt_entry_t *pte;

		intr = intr_disable();
		pte = pmap_pte(kernel_pmap, sysm->base);
		*pte = PTE_G;
		pmap_invalidate_page(kernel_pmap, sysm->base);
		intr_restore(intr);
		sysm->valid1 = 0;
	}
#endif
}

/*
 * Maps a sequence of resident pages belonging to the same object.
 * The sequence begins with the given page m_start.  This page is
 * mapped at the given virtual address start.  Each subsequent page is
 * mapped at a virtual address that is offset from start by the same
 * amount as the page is offset from m_start within the object.  The
 * last page in the sequence is the page with the largest offset from
 * m_start that can be mapped at a virtual address less than the given
 * virtual address end.  Not every virtual page between start and end
 * is mapped; only those for which a resident page exists with the
 * corresponding offset from m_start are mapped.
 */
void
pmap_enter_object(pmap_t pmap, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	vm_page_t m, mpte;
	vm_pindex_t diff, psize;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	psize = atop(end - start);
	mpte = NULL;
	m = m_start;
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	while (m != NULL && (diff = m->pindex - m_start->pindex) < psize) {
		mpte = pmap_enter_quick_locked(pmap, start + ptoa(diff), m,
		    prot, mpte);
		m = TAILQ_NEXT(m, listq);
	}
	rw_wunlock(&pvh_global_lock);
 	PMAP_UNLOCK(pmap);
}

/*
 * pmap_object_init_pt preloads the ptes for a given object
 * into the specified pmap.  This eliminates the blast of soft
 * faults on process startup and immediately after an mmap.
 */
void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr,
    vm_object_t object, vm_pindex_t pindex, vm_size_t size)
{
	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_DEVICE || object->type == OBJT_SG,
	    ("pmap_object_init_pt: non-device object"));
}

/*
 *	Clear the wired attribute from the mappings for the specified range of
 *	addresses in the given pmap.  Every valid mapping within that range
 *	must have the wired attribute set.  In contrast, invalid mappings
 *	cannot have the wired attribute set, so they are ignored.
 *
 *	The wired attribute of the page table entry is not a hardware feature,
 *	so there is no need to invalidate any TLB entries.
 */
void
pmap_unwire(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	pd_entry_t *pde, *pdpe;
	pt_entry_t *pte;
	vm_offset_t va_next;

	PMAP_LOCK(pmap);
	for (; sva < eva; sva = va_next) {
		pdpe = pmap_segmap(pmap, sva);
#ifdef __mips_n64
		if (*pdpe == NULL) {
			va_next = (sva + NBSEG) & ~SEGMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}
#endif
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;
		pde = pmap_pdpe_to_pde(pdpe, sva);
		if (*pde == NULL)
			continue;
		if (va_next > eva)
			va_next = eva;
		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			if (!pte_test(pte, PTE_V))
				continue;
			if (!pte_test(pte, PTE_W))
				panic("pmap_unwire: pte %#jx is missing PG_W",
				    (uintmax_t)*pte);
			pte_clear(pte, PTE_W);
			pmap->pm_stats.wired_count--;
		}
	}
	PMAP_UNLOCK(pmap);
}

/*
 *	Copy the range specified by src_addr/len
 *	from the source map to the range dst_addr/len
 *	in the destination map.
 *
 *	This routine is only advisory and need not do anything.
 */

void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr,
    vm_size_t len, vm_offset_t src_addr)
{
}

/*
 *	pmap_zero_page zeros the specified hardware page by mapping
 *	the page into KVM and using bzero to clear its contents.
 *
 * 	Use XKPHYS for 64 bit, and KSEG0 where possible for 32 bit.
 */
void
pmap_zero_page(vm_page_t m)
{
	vm_offset_t va;
	vm_paddr_t phys = VM_PAGE_TO_PHYS(m);

	if (MIPS_DIRECT_MAPPABLE(phys)) {
		va = MIPS_PHYS_TO_DIRECT(phys);
		bzero((caddr_t)va, PAGE_SIZE);
		mips_dcache_wbinv_range(va, PAGE_SIZE);
	} else {
		va = pmap_lmem_map1(phys);
		bzero((caddr_t)va, PAGE_SIZE);
		mips_dcache_wbinv_range(va, PAGE_SIZE);
		pmap_lmem_unmap();
	}
}

/*
 *	pmap_zero_page_area zeros the specified hardware page by mapping
 *	the page into KVM and using bzero to clear its contents.
 *
 *	off and size may not cover an area beyond a single hardware page.
 */
void
pmap_zero_page_area(vm_page_t m, int off, int size)
{
	vm_offset_t va;
	vm_paddr_t phys = VM_PAGE_TO_PHYS(m);

	if (MIPS_DIRECT_MAPPABLE(phys)) {
		va = MIPS_PHYS_TO_DIRECT(phys);
		bzero((char *)(caddr_t)va + off, size);
		mips_dcache_wbinv_range(va + off, size);
	} else {
		va = pmap_lmem_map1(phys);
		bzero((char *)va + off, size);
		mips_dcache_wbinv_range(va + off, size);
		pmap_lmem_unmap();
	}
}

/*
 *	pmap_copy_page copies the specified (machine independent)
 *	page by mapping the page into virtual memory and using
 *	bcopy to copy the page, one machine dependent page at a
 *	time.
 *
 * 	Use XKPHYS for 64 bit, and KSEG0 where possible for 32 bit.
 */
void
pmap_copy_page(vm_page_t src, vm_page_t dst)
{
	vm_offset_t va_src, va_dst;
	vm_paddr_t phys_src = VM_PAGE_TO_PHYS(src);
	vm_paddr_t phys_dst = VM_PAGE_TO_PHYS(dst);

	if (MIPS_DIRECT_MAPPABLE(phys_src) && MIPS_DIRECT_MAPPABLE(phys_dst)) {
		/* easy case, all can be accessed via KSEG0 */
		/*
		 * Flush all caches for VA that are mapped to this page
		 * to make sure that data in SDRAM is up to date
		 */
		pmap_flush_pvcache(src);
		mips_dcache_wbinv_range_index(
		    MIPS_PHYS_TO_DIRECT(phys_dst), PAGE_SIZE);
		va_src = MIPS_PHYS_TO_DIRECT(phys_src);
		va_dst = MIPS_PHYS_TO_DIRECT(phys_dst);
		bcopy((caddr_t)va_src, (caddr_t)va_dst, PAGE_SIZE);
		mips_dcache_wbinv_range(va_dst, PAGE_SIZE);
	} else {
		va_src = pmap_lmem_map2(phys_src, phys_dst);
		va_dst = va_src + PAGE_SIZE;
		bcopy((void *)va_src, (void *)va_dst, PAGE_SIZE);
		mips_dcache_wbinv_range(va_dst, PAGE_SIZE);
		pmap_lmem_unmap();
	}
}

int unmapped_buf_allowed;

void
pmap_copy_pages(vm_page_t ma[], vm_offset_t a_offset, vm_page_t mb[],
    vm_offset_t b_offset, int xfersize)
{
	char *a_cp, *b_cp;
	vm_page_t a_m, b_m;
	vm_offset_t a_pg_offset, b_pg_offset;
	vm_paddr_t a_phys, b_phys;
	int cnt;

	while (xfersize > 0) {
		a_pg_offset = a_offset & PAGE_MASK;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		a_m = ma[a_offset >> PAGE_SHIFT];
		a_phys = VM_PAGE_TO_PHYS(a_m);
		b_pg_offset = b_offset & PAGE_MASK;
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		b_m = mb[b_offset >> PAGE_SHIFT];
		b_phys = VM_PAGE_TO_PHYS(b_m);
		if (MIPS_DIRECT_MAPPABLE(a_phys) &&
		    MIPS_DIRECT_MAPPABLE(b_phys)) {
			pmap_flush_pvcache(a_m);
			mips_dcache_wbinv_range_index(
			    MIPS_PHYS_TO_DIRECT(b_phys), PAGE_SIZE);
			a_cp = (char *)MIPS_PHYS_TO_DIRECT(a_phys) +
			    a_pg_offset;
			b_cp = (char *)MIPS_PHYS_TO_DIRECT(b_phys) +
			    b_pg_offset;
			bcopy(a_cp, b_cp, cnt);
			mips_dcache_wbinv_range((vm_offset_t)b_cp, cnt);
		} else {
			a_cp = (char *)pmap_lmem_map2(a_phys, b_phys);
			b_cp = (char *)a_cp + PAGE_SIZE;
			a_cp += a_pg_offset;
			b_cp += b_pg_offset;
			bcopy(a_cp, b_cp, cnt);
			mips_dcache_wbinv_range((vm_offset_t)b_cp, cnt);
			pmap_lmem_unmap();
		}
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
}

vm_offset_t
pmap_quick_enter_page(vm_page_t m)
{
#if defined(__mips_n64)
	return MIPS_PHYS_TO_DIRECT(VM_PAGE_TO_PHYS(m));
#else
	vm_paddr_t pa;
	struct local_sysmaps *sysm;
	pt_entry_t *pte, npte;

	pa = VM_PAGE_TO_PHYS(m);

	if (MIPS_DIRECT_MAPPABLE(pa)) {
		if (pmap_page_get_memattr(m) != VM_MEMATTR_WRITE_BACK)
			return (MIPS_PHYS_TO_DIRECT_UNCACHED(pa));
		else
			return (MIPS_PHYS_TO_DIRECT(pa));
	}
	critical_enter();
	sysm = &sysmap_lmem[PCPU_GET(cpuid)];

	KASSERT(sysm->valid1 == 0, ("pmap_quick_enter_page: PTE busy"));

	pte = pmap_pte(kernel_pmap, sysm->base);
	npte = TLBLO_PA_TO_PFN(pa) | PTE_D | PTE_V | PTE_G;
	PMAP_PTE_SET_CACHE_BITS(npte, pa, m);
	*pte = npte;
	sysm->valid1 = 1;

	return (sysm->base);
#endif
}

void
pmap_quick_remove_page(vm_offset_t addr)
{
	mips_dcache_wbinv_range(addr, PAGE_SIZE);

#if !defined(__mips_n64)
	struct local_sysmaps *sysm;
	pt_entry_t *pte;

	if (addr >= MIPS_KSEG0_START && addr < MIPS_KSEG0_END)
		return;

	sysm = &sysmap_lmem[PCPU_GET(cpuid)];

	KASSERT(sysm->valid1 != 0,
	    ("pmap_quick_remove_page: PTE not in use"));
	KASSERT(sysm->base == addr,
	    ("pmap_quick_remove_page: invalid address"));

	pte = pmap_pte(kernel_pmap, addr);
	*pte = PTE_G;
	tlb_invalidate_address(kernel_pmap, addr);
	sysm->valid1 = 0;
	critical_exit();
#endif
}

/*
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
boolean_t
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{
	pv_entry_t pv;
	int loops = 0;
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_page_exists_quick: page %p is not managed", m));
	rv = FALSE;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		if (PV_PMAP(pv) == pmap) {
			rv = TRUE;
			break;
		}
		loops++;
		if (loops >= 16)
			break;
	}
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/*
 * Remove all pages from specified address space
 * this aids process exit speeds.  Also, this code
 * is special cased for current process only, but
 * can have the more generic (and slightly slower)
 * mode enabled.  This is much faster than pmap_remove
 * in the case of running down an entire address space.
 */
void
pmap_remove_pages(pmap_t pmap)
{
	pd_entry_t *pde;
	pt_entry_t *pte, tpte;
	pv_entry_t pv;
	vm_page_t m;
	struct pv_chunk *pc, *npc;
	u_long inuse, bitmask;
	int allfree, bit, field, idx;

	if (pmap != vmspace_pmap(curthread->td_proc->p_vmspace)) {
		printf("warning: pmap_remove_pages called with non-current pmap\n");
		return;
	}
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	TAILQ_FOREACH_SAFE(pc, &pmap->pm_pvchunk, pc_list, npc) {
		allfree = 1;
		for (field = 0; field < _NPCM; field++) {
			inuse = ~pc->pc_map[field] & pc_freemask[field];
			while (inuse != 0) {
				bit = ffsl(inuse) - 1;
				bitmask = 1UL << bit;
				idx = field * sizeof(inuse) * NBBY + bit;
				pv = &pc->pc_pventry[idx];
				inuse &= ~bitmask;

				pde = pmap_pde(pmap, pv->pv_va);
				KASSERT(pde != NULL && *pde != 0,
				    ("pmap_remove_pages: pde"));
				pte = pmap_pde_to_pte(pde, pv->pv_va);
				if (!pte_test(pte, PTE_V))
					panic("pmap_remove_pages: bad pte");
				tpte = *pte;

/*
 * We cannot remove wired pages from a process' mapping at this time
 */
				if (pte_test(&tpte, PTE_W)) {
					allfree = 0;
					continue;
				}
				*pte = is_kernel_pmap(pmap) ? PTE_G : 0;

				m = PHYS_TO_VM_PAGE(TLBLO_PTE_TO_PA(tpte));
				KASSERT(m != NULL,
				    ("pmap_remove_pages: bad tpte %#jx",
				    (uintmax_t)tpte));

				/*
				 * Update the vm_page_t clean and reference bits.
				 */
				if (pte_test(&tpte, PTE_D))
					vm_page_dirty(m);

				/* Mark free */
				PV_STAT(pv_entry_frees++);
				PV_STAT(pv_entry_spare++);
				pv_entry_count--;
				pc->pc_map[field] |= bitmask;
				pmap->pm_stats.resident_count--;
				TAILQ_REMOVE(&m->md.pv_list, pv, pv_list);
				if (TAILQ_EMPTY(&m->md.pv_list))
					vm_page_aflag_clear(m, PGA_WRITEABLE);
				pmap_unuse_pt(pmap, pv->pv_va, *pde);
			}
		}
		if (allfree) {
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			free_pv_chunk(pc);
		}
	}
	pmap_invalidate_all(pmap);
	PMAP_UNLOCK(pmap);
	rw_wunlock(&pvh_global_lock);
}

/*
 * pmap_testbit tests bits in pte's
 */
static boolean_t
pmap_testbit(vm_page_t m, int bit)
{
	pv_entry_t pv;
	pmap_t pmap;
	pt_entry_t *pte;
	boolean_t rv = FALSE;

	if (m->oflags & VPO_UNMANAGED)
		return (rv);

	rw_assert(&pvh_global_lock, RA_WLOCKED);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte(pmap, pv->pv_va);
		rv = pte_test(pte, bit);
		PMAP_UNLOCK(pmap);
		if (rv)
			break;
	}
	return (rv);
}

/*
 *	pmap_page_wired_mappings:
 *
 *	Return the number of managed mappings to the given physical page
 *	that are wired.
 */
int
pmap_page_wired_mappings(vm_page_t m)
{
	pv_entry_t pv;
	pmap_t pmap;
	pt_entry_t *pte;
	int count;

	count = 0;
	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (count);
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte(pmap, pv->pv_va);
		if (pte_test(pte, PTE_W))
			count++;
		PMAP_UNLOCK(pmap);
	}
	rw_wunlock(&pvh_global_lock);
	return (count);
}

/*
 * Clear the write and modified bits in each of the given page's mappings.
 */
void
pmap_remove_write(vm_page_t m)
{
	pmap_t pmap;
	pt_entry_t pbits, *pte;
	pv_entry_t pv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_write: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * set by another thread while the object is locked.  Thus,
	 * if PGA_WRITEABLE is clear, no page table entries need updating.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte(pmap, pv->pv_va);
		KASSERT(pte != NULL && pte_test(pte, PTE_V),
		    ("page on pv_list has no pte"));
		pbits = *pte;
		if (pte_test(&pbits, PTE_D)) {
			pte_clear(&pbits, PTE_D);
			vm_page_dirty(m);
		}
		pte_set(&pbits, PTE_RO);
		if (pbits != *pte) {
			*pte = pbits;
			pmap_update_page(pmap, pv->pv_va, pbits);
		}
		PMAP_UNLOCK(pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&pvh_global_lock);
}

/*
 *	pmap_ts_referenced:
 *
 *	Return the count of reference bits for a page, clearing all of them.
 */
int
pmap_ts_referenced(vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_ts_referenced: page %p is not managed", m));
	if (m->md.pv_flags & PV_TABLE_REF) {
		rw_wlock(&pvh_global_lock);
		m->md.pv_flags &= ~PV_TABLE_REF;
		rw_wunlock(&pvh_global_lock);
		return (1);
	}
	return (0);
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page was modified
 *	in any physical maps.
 */
boolean_t
pmap_is_modified(vm_page_t m)
{
	boolean_t rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_modified: page %p is not managed", m));

	/*
	 * If the page is not exclusive busied, then PGA_WRITEABLE cannot be
	 * concurrently set while the object is locked.  Thus, if PGA_WRITEABLE
	 * is clear, no PTEs can have PTE_D set.
	 */
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	if (!vm_page_xbusied(m) && (m->aflags & PGA_WRITEABLE) == 0)
		return (FALSE);
	rw_wlock(&pvh_global_lock);
	rv = pmap_testbit(m, PTE_D);
	rw_wunlock(&pvh_global_lock);
	return (rv);
}

/* N/C */

/*
 *	pmap_is_prefaultable:
 *
 *	Return whether or not the specified virtual address is elgible
 *	for prefault.
 */
boolean_t
pmap_is_prefaultable(pmap_t pmap, vm_offset_t addr)
{
	pd_entry_t *pde;
	pt_entry_t *pte;
	boolean_t rv;

	rv = FALSE;
	PMAP_LOCK(pmap);
	pde = pmap_pde(pmap, addr);
	if (pde != NULL && *pde != 0) {
		pte = pmap_pde_to_pte(pde, addr);
		rv = (*pte == 0);
	}
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 *	Apply the given advice to the specified range of addresses within the
 *	given pmap.  Depending on the advice, clear the referenced and/or
 *	modified flags in each mapping and set the mapped page's dirty field.
 */
void
pmap_advise(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, int advice)
{
	pd_entry_t *pde, *pdpe;
	pt_entry_t *pte;
	vm_offset_t va, va_next;
	vm_paddr_t pa;
	vm_page_t m;

	if (advice != MADV_DONTNEED && advice != MADV_FREE)
		return;
	rw_wlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	for (; sva < eva; sva = va_next) {
		pdpe = pmap_segmap(pmap, sva);
#ifdef __mips_n64
		if (*pdpe == 0) {
			va_next = (sva + NBSEG) & ~SEGMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}
#endif
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;

		pde = pmap_pdpe_to_pde(pdpe, sva);
		if (*pde == NULL)
			continue;

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being write protected.
		 */
		if (va_next > eva)
			va_next = eva;

		va = va_next;
		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			if (!pte_test(pte, PTE_MANAGED | PTE_V)) {
				if (va != va_next) {
					pmap_invalidate_range(pmap, va, sva);
					va = va_next;
				}
				continue;
			}
			pa = TLBLO_PTE_TO_PA(*pte);
			m = PHYS_TO_VM_PAGE(pa);
			m->md.pv_flags &= ~PV_TABLE_REF;
			if (pte_test(pte, PTE_D)) {
				if (advice == MADV_DONTNEED) {
					/*
					 * Future calls to pmap_is_modified()
					 * can be avoided by making the page
					 * dirty now.
					 */
					vm_page_dirty(m);
				} else {
					pte_clear(pte, PTE_D);
					if (va == va_next)
						va = sva;
				}
			} else {
				/*
				 * Unless PTE_D is set, any TLB entries
				 * mapping "sva" don't allow write access, so
				 * they needn't be invalidated.
				 */
				if (va != va_next) {
					pmap_invalidate_range(pmap, va, sva);
					va = va_next;
				}
			}
		}
		if (va != va_next)
			pmap_invalidate_range(pmap, va, sva);
	}
	rw_wunlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
{
	pmap_t pmap;
	pt_entry_t *pte;
	pv_entry_t pv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_clear_modify: page %p is not managed", m));
	VM_OBJECT_ASSERT_WLOCKED(m->object);
	KASSERT(!vm_page_xbusied(m),
	    ("pmap_clear_modify: page %p is exclusive busied", m));

	/*
	 * If the page is not PGA_WRITEABLE, then no PTEs can have PTE_D set.
	 * If the object containing the page is locked and the page is not
	 * write busied, then PGA_WRITEABLE cannot be concurrently set.
	 */
	if ((m->aflags & PGA_WRITEABLE) == 0)
		return;
	rw_wlock(&pvh_global_lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_list) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pte = pmap_pte(pmap, pv->pv_va);
		if (pte_test(pte, PTE_D)) {
			pte_clear(pte, PTE_D);
			pmap_update_page(pmap, pv->pv_va, *pte);
		}
		PMAP_UNLOCK(pmap);
	}
	rw_wunlock(&pvh_global_lock);
}

/*
 *	pmap_is_referenced:
 *
 *	Return whether or not the specified physical page was referenced
 *	in any physical maps.
 */
boolean_t
pmap_is_referenced(vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_referenced: page %p is not managed", m));
	return ((m->md.pv_flags & PV_TABLE_REF) != 0);
}

/*
 * Miscellaneous support routines follow
 */

/*
 * Map a set of physical memory pages into the kernel virtual
 * address space. Return a pointer to where it is mapped. This
 * routine is intended to be used for mapping device memory,
 * NOT real memory.
 *
 * Use XKPHYS uncached for 64 bit, and KSEG1 where possible for 32 bit.
 */
void *
pmap_mapdev_attr(vm_paddr_t pa, vm_size_t size, vm_memattr_t ma)
{
        vm_offset_t va, tmpva, offset;

	/*
	 * KSEG1 maps only first 512M of phys address space. For
	 * pa > 0x20000000 we should make proper mapping * using pmap_kenter.
	 */
	if (MIPS_DIRECT_MAPPABLE(pa + size - 1) && ma == VM_MEMATTR_UNCACHEABLE)
		return ((void *)MIPS_PHYS_TO_DIRECT_UNCACHED(pa));
	else {
		offset = pa & PAGE_MASK;
		size = roundup(size + offset, PAGE_SIZE);

		va = kva_alloc(size);
		if (!va)
			panic("pmap_mapdev: Couldn't alloc kernel virtual memory");
		pa = trunc_page(pa);
		for (tmpva = va; size > 0;) {
			pmap_kenter_attr(tmpva, pa, ma);
			size -= PAGE_SIZE;
			tmpva += PAGE_SIZE;
			pa += PAGE_SIZE;
		}
	}

	return ((void *)(va + offset));
}

void *
pmap_mapdev(vm_paddr_t pa, vm_size_t size)
{
	return pmap_mapdev_attr(pa, size, VM_MEMATTR_UNCACHEABLE);
}

void
pmap_unmapdev(vm_offset_t va, vm_size_t size)
{
#ifndef __mips_n64
	vm_offset_t base, offset;

	/* If the address is within KSEG1 then there is nothing to do */
	if (va >= MIPS_KSEG1_START && va <= MIPS_KSEG1_END)
		return;

	base = trunc_page(va);
	offset = va & PAGE_MASK;
	size = roundup(size + offset, PAGE_SIZE);
	kva_free(base, size);
#endif
}

/*
 * perform the pmap work for mincore
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr, vm_paddr_t *locked_pa)
{
	pt_entry_t *ptep, pte;
	vm_paddr_t pa;
	vm_page_t m;
	int val;

	PMAP_LOCK(pmap);
retry:
	ptep = pmap_pte(pmap, addr);
	pte = (ptep != NULL) ? *ptep : 0;
	if (!pte_test(&pte, PTE_V)) {
		val = 0;
		goto out;
	}
	val = MINCORE_INCORE;
	if (pte_test(&pte, PTE_D))
		val |= MINCORE_MODIFIED | MINCORE_MODIFIED_OTHER;
	pa = TLBLO_PTE_TO_PA(pte);
	if (pte_test(&pte, PTE_MANAGED)) {
		/*
		 * This may falsely report the given address as
		 * MINCORE_REFERENCED.  Unfortunately, due to the lack of
		 * per-PTE reference information, it is impossible to
		 * determine if the address is MINCORE_REFERENCED.
		 */
		m = PHYS_TO_VM_PAGE(pa);
		if ((m->aflags & PGA_REFERENCED) != 0)
			val |= MINCORE_REFERENCED | MINCORE_REFERENCED_OTHER;
	}
	if ((val & (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER)) !=
	    (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER) &&
	    pte_test(&pte, PTE_MANAGED)) {
		/* Ensure that "PHYS_TO_VM_PAGE(pa)->object" doesn't change. */
		if (vm_page_pa_tryrelock(pmap, pa, locked_pa))
			goto retry;
	} else
out:
		PA_UNLOCK_COND(*locked_pa);
	PMAP_UNLOCK(pmap);
	return (val);
}

void
pmap_activate(struct thread *td)
{
	pmap_t pmap, oldpmap;
	struct proc *p = td->td_proc;
	u_int cpuid;

	critical_enter();

	pmap = vmspace_pmap(p->p_vmspace);
	oldpmap = PCPU_GET(curpmap);
	cpuid = PCPU_GET(cpuid);

	if (oldpmap)
		CPU_CLR_ATOMIC(cpuid, &oldpmap->pm_active);
	CPU_SET_ATOMIC(cpuid, &pmap->pm_active);
	pmap_asid_alloc(pmap);
	if (td == curthread) {
		PCPU_SET(segbase, pmap->pm_segtab);
		mips_wr_entryhi(pmap->pm_asid[cpuid].asid);
	}

	PCPU_SET(curpmap, pmap);
	critical_exit();
}

static void
pmap_sync_icache_one(void *arg __unused)
{

	mips_icache_sync_all();
	mips_dcache_wbinv_all();
}

void
pmap_sync_icache(pmap_t pm, vm_offset_t va, vm_size_t sz)
{

	smp_rendezvous(NULL, pmap_sync_icache_one, NULL, NULL);
}

/*
 *	Increase the starting virtual address of the given mapping if a
 *	different alignment might result in more superpage mappings.
 */
void
pmap_align_superpage(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{
	vm_offset_t superpage_offset;

	if (size < PDRSIZE)
		return;
	if (object != NULL && (object->flags & OBJ_COLORED) != 0)
		offset += ptoa(object->pg_color);
	superpage_offset = offset & PDRMASK;
	if (size - ((PDRSIZE - superpage_offset) & PDRMASK) < PDRSIZE ||
	    (*addr & PDRMASK) == superpage_offset)
		return;
	if ((*addr & PDRMASK) < superpage_offset)
		*addr = (*addr & ~PDRMASK) + superpage_offset;
	else
		*addr = ((*addr + PDRMASK) & ~PDRMASK) + superpage_offset;
}

#ifdef DDB
DB_SHOW_COMMAND(ptable, ddb_pid_dump)
{
	pmap_t pmap;
	struct thread *td = NULL;
	struct proc *p;
	int i, j, k;
	vm_paddr_t pa;
	vm_offset_t va;

	if (have_addr) {
		td = db_lookup_thread(addr, true);
		if (td == NULL) {
			db_printf("Invalid pid or tid");
			return;
		}
		p = td->td_proc;
		if (p->p_vmspace == NULL) {
			db_printf("No vmspace for process");
			return;
		}
			pmap = vmspace_pmap(p->p_vmspace);
	} else
		pmap = kernel_pmap;

	db_printf("pmap:%p segtab:%p asid:%x generation:%x\n",
	    pmap, pmap->pm_segtab, pmap->pm_asid[0].asid,
	    pmap->pm_asid[0].gen);
	for (i = 0; i < NPDEPG; i++) {
		pd_entry_t *pdpe;
		pt_entry_t *pde;
		pt_entry_t pte;

		pdpe = (pd_entry_t *)pmap->pm_segtab[i];
		if (pdpe == NULL)
			continue;
		db_printf("[%4d] %p\n", i, pdpe);
#ifdef __mips_n64
		for (j = 0; j < NPDEPG; j++) {
			pde = (pt_entry_t *)pdpe[j];
			if (pde == NULL)
				continue;
			db_printf("\t[%4d] %p\n", j, pde);
#else
		{
			j = 0;
			pde =  (pt_entry_t *)pdpe;
#endif
			for (k = 0; k < NPTEPG; k++) {
				pte = pde[k];
				if (pte == 0 || !pte_test(&pte, PTE_V))
					continue;
				pa = TLBLO_PTE_TO_PA(pte);
				va = ((u_long)i << SEGSHIFT) | (j << PDRSHIFT) | (k << PAGE_SHIFT);
				db_printf("\t\t[%04d] va: %p pte: %8jx pa:%jx\n",
				       k, (void *)va, (uintmax_t)pte, (uintmax_t)pa);
			}
		}
	}
}
#endif

/*
 * Allocate TLB address space tag (called ASID or TLBPID) and return it.
 * It takes almost as much or more time to search the TLB for a
 * specific ASID and flush those entries as it does to flush the entire TLB.
 * Therefore, when we allocate a new ASID, we just take the next number. When
 * we run out of numbers, we flush the TLB, increment the generation count
 * and start over. ASID zero is reserved for kernel use.
 */
static void
pmap_asid_alloc(pmap)
	pmap_t pmap;
{
	if (pmap->pm_asid[PCPU_GET(cpuid)].asid != PMAP_ASID_RESERVED &&
	    pmap->pm_asid[PCPU_GET(cpuid)].gen == PCPU_GET(asid_generation));
	else {
		if (PCPU_GET(next_asid) == pmap_max_asid) {
			tlb_invalidate_all_user(NULL);
			PCPU_SET(asid_generation,
			    (PCPU_GET(asid_generation) + 1) & ASIDGEN_MASK);
			if (PCPU_GET(asid_generation) == 0) {
				PCPU_SET(asid_generation, 1);
			}
			PCPU_SET(next_asid, 1);	/* 0 means invalid */
		}
		pmap->pm_asid[PCPU_GET(cpuid)].asid = PCPU_GET(next_asid);
		pmap->pm_asid[PCPU_GET(cpuid)].gen = PCPU_GET(asid_generation);
		PCPU_SET(next_asid, PCPU_GET(next_asid) + 1);
	}
}

static pt_entry_t
init_pte_prot(vm_page_t m, vm_prot_t access, vm_prot_t prot)
{
	pt_entry_t rw;

	if (!(prot & VM_PROT_WRITE))
		rw = PTE_V | PTE_RO;
	else if ((m->oflags & VPO_UNMANAGED) == 0) {
		if ((access & VM_PROT_WRITE) != 0)
			rw = PTE_V | PTE_D;
		else
			rw = PTE_V;
	} else
		/* Needn't emulate a modified bit for unmanaged pages. */
		rw = PTE_V | PTE_D;
	return (rw);
}

/*
 * pmap_emulate_modified : do dirty bit emulation
 *
 * On SMP, update just the local TLB, other CPUs will update their
 * TLBs from PTE lazily, if they get the exception.
 * Returns 0 in case of sucess, 1 if the page is read only and we
 * need to fault.
 */
int
pmap_emulate_modified(pmap_t pmap, vm_offset_t va)
{
	pt_entry_t *pte;

	PMAP_LOCK(pmap);
	pte = pmap_pte(pmap, va);
	if (pte == NULL)
		panic("pmap_emulate_modified: can't find PTE");
#ifdef SMP
	/* It is possible that some other CPU changed m-bit */
	if (!pte_test(pte, PTE_V) || pte_test(pte, PTE_D)) {
		tlb_update(pmap, va, *pte);
		PMAP_UNLOCK(pmap);
		return (0);
	}
#else
	if (!pte_test(pte, PTE_V) || pte_test(pte, PTE_D))
		panic("pmap_emulate_modified: invalid pte");
#endif
	if (pte_test(pte, PTE_RO)) {
		PMAP_UNLOCK(pmap);
		return (1);
	}
	pte_set(pte, PTE_D);
	tlb_update(pmap, va, *pte);
	if (!pte_test(pte, PTE_MANAGED))
		panic("pmap_emulate_modified: unmanaged page");
	PMAP_UNLOCK(pmap);
	return (0);
}

/*
 *	Routine:	pmap_kextract
 *	Function:
 *		Extract the physical page address associated
 *		virtual address.
 */
vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	int mapped;

	/*
	 * First, the direct-mapped regions.
	 */
#if defined(__mips_n64)
	if (va >= MIPS_XKPHYS_START && va < MIPS_XKPHYS_END)
		return (MIPS_XKPHYS_TO_PHYS(va));
#endif
	if (va >= MIPS_KSEG0_START && va < MIPS_KSEG0_END)
		return (MIPS_KSEG0_TO_PHYS(va));

	if (va >= MIPS_KSEG1_START && va < MIPS_KSEG1_END)
		return (MIPS_KSEG1_TO_PHYS(va));

	/*
	 * User virtual addresses.
	 */
	if (va < VM_MAXUSER_ADDRESS) {
		pt_entry_t *ptep;

		if (curproc && curproc->p_vmspace) {
			ptep = pmap_pte(&curproc->p_vmspace->vm_pmap, va);
			if (ptep) {
				return (TLBLO_PTE_TO_PA(*ptep) |
				    (va & PAGE_MASK));
			}
			return (0);
		}
	}

	/*
	 * Should be kernel virtual here, otherwise fail
	 */
	mapped = (va >= MIPS_KSEG2_START || va < MIPS_KSEG2_END);
#if defined(__mips_n64)
	mapped = mapped || (va >= MIPS_XKSEG_START || va < MIPS_XKSEG_END);
#endif
	/*
	 * Kernel virtual.
	 */

	if (mapped) {
		pt_entry_t *ptep;

		/* Is the kernel pmap initialized? */
		if (!CPU_EMPTY(&kernel_pmap->pm_active)) {
			/* It's inside the virtual address range */
			ptep = pmap_pte(kernel_pmap, va);
			if (ptep) {
				return (TLBLO_PTE_TO_PA(*ptep) |
				    (va & PAGE_MASK));
			}
		}
		return (0);
	}

	panic("%s for unknown address space %p.", __func__, (void *)va);
}


void
pmap_flush_pvcache(vm_page_t m)
{
	pv_entry_t pv;

	if (m != NULL) {
		for (pv = TAILQ_FIRST(&m->md.pv_list); pv;
		    pv = TAILQ_NEXT(pv, pv_list)) {
			mips_dcache_wbinv_range_index(pv->pv_va, PAGE_SIZE);
		}
	}
}

void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{

	/*
	 * It appears that this function can only be called before any mappings
	 * for the page are established.  If this ever changes, this code will
	 * need to walk the pv_list and make each of the existing mappings
	 * uncacheable, being careful to sync caches and PTEs (and maybe
	 * invalidate TLB?) for any current mapping it modifies.
	 */
	if (TAILQ_FIRST(&m->md.pv_list) != NULL)
		panic("Can't change memattr on page with existing mappings");

	/* Clean memattr portion of pv_flags */
	m->md.pv_flags &= ~PV_MEMATTR_MASK;
	m->md.pv_flags |= (ma << PV_MEMATTR_SHIFT) & PV_MEMATTR_MASK;
}

static __inline void
pmap_pte_attr(pt_entry_t *pte, vm_memattr_t ma)
{
	u_int npte;

	npte = *(u_int *)pte;
	npte &= ~PTE_C_MASK;
	npte |= PTE_C(ma);
	*pte = npte;
}

int
pmap_change_attr(vm_offset_t sva, vm_size_t size, vm_memattr_t ma)
{
	pd_entry_t *pde, *pdpe;
	pt_entry_t *pte;
	vm_offset_t ova, eva, va, va_next;
	pmap_t pmap;

	ova = sva;
	eva = sva + size;
	if (eva < sva)
		return (EINVAL);

	pmap = kernel_pmap;
	PMAP_LOCK(pmap);

	for (; sva < eva; sva = va_next) {
		pdpe = pmap_segmap(pmap, sva);
#ifdef __mips_n64
		if (*pdpe == 0) {
			va_next = (sva + NBSEG) & ~SEGMASK;
			if (va_next < sva)
				va_next = eva;
			continue;
		}
#endif
		va_next = (sva + NBPDR) & ~PDRMASK;
		if (va_next < sva)
			va_next = eva;

		pde = pmap_pdpe_to_pde(pdpe, sva);
		if (*pde == NULL)
			continue;

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
		if (va_next > eva)
			va_next = eva;

		va = va_next;
		for (pte = pmap_pde_to_pte(pde, sva); sva != va_next; pte++,
		    sva += PAGE_SIZE) {
			if (!pte_test(pte, PTE_V) || pte_cache_bits(pte) == ma) {
				if (va != va_next) {
					pmap_invalidate_range(pmap, va, sva);
					va = va_next;
				}
				continue;
			}
			if (va == va_next)
				va = sva;

			pmap_pte_attr(pte, ma);
		}
		if (va != va_next)
			pmap_invalidate_range(pmap, va, sva);
	}
	PMAP_UNLOCK(pmap);

	/* Flush caches to be in the safe side */
	mips_dcache_wbinv_range(ova, size);
	return 0;
}

boolean_t
pmap_is_valid_memattr(pmap_t pmap __unused, vm_memattr_t mode)
{

	switch (mode) {
	case VM_MEMATTR_UNCACHEABLE:
	case VM_MEMATTR_WRITE_BACK:
#ifdef MIPS_CCA_WC
	case VM_MEMATTR_WRITE_COMBINING:
#endif
		return (TRUE);
	default:
		return (FALSE);
	}
}
