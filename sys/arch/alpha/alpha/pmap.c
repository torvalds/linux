/* $OpenBSD: pmap.c,v 1.94 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pmap.c,v 1.154 2000/12/07 22:18:55 thorpej Exp $ */

/*-
 * Copyright (c) 1998, 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center and by Chris G. Demetriou.
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
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	@(#)pmap.c	8.6 (Berkeley) 5/27/94
 */

/*
 * DEC Alpha physical map management code.
 *
 * History:
 *
 *	This pmap started life as a Motorola 68851/68030 pmap,
 *	written by Mike Hibler at the University of Utah.
 *
 *	It was modified for the DEC Alpha by Chris Demetriou
 *	at Carnegie Mellon University.
 *
 *	Support for non-contiguous physical memory was added by
 *	Jason R. Thorpe of the Numerical Aerospace Simulation
 *	Facility, NASA Ames Research Center and Chris Demetriou.
 *
 *	Page table management and a major cleanup were undertaken
 *	by Jason R. Thorpe, with lots of help from Ross Harvey of
 *	Avalon Computer Systems and from Chris Demetriou.
 *
 *	Support for the new UVM pmap interface was written by
 *	Jason R. Thorpe.
 *
 *	Support for ASNs was written by Jason R. Thorpe, again
 *	with help from Chris Demetriou and Ross Harvey.
 *
 *	The locking protocol was written by Jason R. Thorpe,
 *	using Chuck Cranor's i386 pmap for UVM as a model.
 *
 *	TLB shootdown code was written by Jason R. Thorpe.
 *
 * Notes:
 *
 *	All page table access is done via K0SEG.  The one exception
 *	to this is for kernel mappings.  Since all kernel page
 *	tables are pre-allocated, we can use the Virtual Page Table
 *	to access PTEs that map K1SEG addresses.
 *
 *	Kernel page table pages are statically allocated in
 *	pmap_bootstrap(), and are never freed.  In the future,
 *	support for dynamically adding additional kernel page
 *	table pages may be added.  User page table pages are
 *	dynamically allocated and freed.
 *
 * Bugs/misfeatures:
 *
 *	- Some things could be optimized.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/atomic.h>
#ifdef SYSVSHM
#include <sys/shm.h>
#endif

#include <uvm/uvm.h>

#include <machine/atomic.h>
#include <machine/cpu.h>
#if defined(MULTIPROCESSOR)
#include <machine/rpb.h>
#endif

#ifdef DEBUG
#define	PDB_FOLLOW	0x0001
#define	PDB_INIT	0x0002
#define	PDB_ENTER	0x0004
#define	PDB_REMOVE	0x0008
#define	PDB_CREATE	0x0010
#define	PDB_PTPAGE	0x0020
#define	PDB_ASN		0x0040
#define	PDB_BITS	0x0080
#define	PDB_COLLECT	0x0100
#define	PDB_PROTECT	0x0200
#define	PDB_BOOTSTRAP	0x1000
#define	PDB_PARANOIA	0x2000
#define	PDB_WIRING	0x4000
#define	PDB_PVDUMP	0x8000

int debugmap = 0;
int pmapdebug = PDB_PARANOIA|PDB_FOLLOW|PDB_ENTER;
#endif

/*
 * Given a map and a machine independent protection code,
 * convert to an alpha protection code.
 */
#define pte_prot(m, p)	(protection_codes[m == pmap_kernel() ? 0 : 1][p])
int	protection_codes[2][8];

/*
 * kernel_lev1map:
 *
 *	Kernel level 1 page table.  This maps all kernel level 2
 *	page table pages, and is used as a template for all user
 *	pmap level 1 page tables.  When a new user level 1 page
 *	table is allocated, all kernel_lev1map PTEs for kernel
 *	addresses are copied to the new map.
 *
 *	The kernel also has an initial set of kernel level 2 page
 *	table pages.  These map the kernel level 3 page table pages.
 *	As kernel level 3 page table pages are added, more level 2
 *	page table pages may be added to map them.  These pages are
 *	never freed.
 *
 *	Finally, the kernel also has an initial set of kernel level
 *	3 page table pages.  These map pages in K1SEG.  More level
 *	3 page table pages may be added at run-time if additional
 *	K1SEG address space is required.  These pages are never freed.
 *
 * NOTE: When mappings are inserted into the kernel pmap, all
 * level 2 and level 3 page table pages must already be allocated
 * and mapped into the parent page table.
 */
pt_entry_t	*kernel_lev1map;

/*
 * Virtual Page Table.
 */
pt_entry_t	*VPT;

struct pmap	kernel_pmap_store
	[(PMAP_SIZEOF(ALPHA_MAXPROCS) + sizeof(struct pmap) - 1)
		/ sizeof(struct pmap)];

paddr_t    	avail_start;	/* PA of first available physical page */
paddr_t		avail_end;	/* PA of last available physical page */
vaddr_t		pmap_maxkvaddr;	/* VA of last avail page (pmap_growkernel) */

boolean_t	pmap_initialized;	/* Has pmap_init completed? */

u_long		pmap_pages_stolen;	/* instrumentation */

/*
 * This variable contains the number of CPU IDs we need to allocate
 * space for when allocating the pmap structure.  It is used to
 * size a per-CPU array of ASN and ASN Generation number.
 */
u_long		pmap_ncpuids;

#ifndef PMAP_PV_LOWAT
#define	PMAP_PV_LOWAT	16
#endif
int		pmap_pv_lowat = PMAP_PV_LOWAT;

/*
 * List of all pmaps, used to update them when e.g. additional kernel
 * page tables are allocated.  This list is kept LRU-ordered by
 * pmap_activate().
 */
TAILQ_HEAD(, pmap) pmap_all_pmaps;

/*
 * The pools from which pmap structures and sub-structures are allocated.
 */
struct pool pmap_pmap_pool;
struct pool pmap_l1pt_pool;
struct pool pmap_pv_pool;

/*
 * Address Space Numbers.
 *
 * On many implementations of the Alpha architecture, the TLB entries and
 * I-cache blocks are tagged with a unique number within an implementation-
 * specified range.  When a process context becomes active, the ASN is used
 * to match TLB entries; if a TLB entry for a particular VA does not match
 * the current ASN, it is ignored (one could think of the processor as
 * having a collection of <max ASN> separate TLBs).  This allows operating
 * system software to skip the TLB flush that would otherwise be necessary
 * at context switch time.
 *
 * Alpha PTEs have a bit in them (PG_ASM - Address Space Match) that
 * causes TLB entries to match any ASN.  The PALcode also provides
 * a TBI (Translation Buffer Invalidate) operation that flushes all
 * TLB entries that _do not_ have PG_ASM.  We use this bit for kernel
 * mappings, so that invalidation of all user mappings does not invalidate
 * kernel mappings (which are consistent across all processes).
 *
 * pma_asn always indicates to the next ASN to use.  When
 * pma_asn exceeds pmap_max_asn, we start a new ASN generation.
 *
 * When a new ASN generation is created, the per-process (i.e. non-PG_ASM)
 * TLB entries and the I-cache are flushed, the generation number is bumped,
 * and pma_asn is changed to indicate the first non-reserved ASN.
 *
 * We reserve ASN #0 for pmaps that use the global kernel_lev1map.  This
 * prevents the following scenario:
 *
 *	* New ASN generation starts, and process A is given ASN #0.
 *
 *	* A new process B (and thus new pmap) is created.  The ASN,
 *	  for lack of a better value, is initialized to 0.
 *
 *	* Process B runs.  It is now using the TLB entries tagged
 *	  by process A.  *poof*
 *
 * In the scenario above, in addition to the processor using incorrect
 * TLB entries, the PALcode might use incorrect information to service a
 * TLB miss.  (The PALcode uses the recursively mapped Virtual Page Table
 * to locate the PTE for a faulting address, and tagged TLB entries exist
 * for the Virtual Page Table addresses in order to speed up this procedure,
 * as well.)
 *
 * By reserving an ASN for kernel_lev1map users, we are guaranteeing that
 * new pmaps will initially run with no TLB entries for user addresses
 * or VPT mappings that map user page tables.  Since kernel_lev1map only
 * contains mappings for kernel addresses, and since those mappings
 * are always made with PG_ASM, sharing an ASN for kernel_lev1map users is
 * safe (since PG_ASM mappings match any ASN).
 *
 * On processors that do not support ASNs, the PALcode invalidates
 * the TLB and I-cache automatically on swpctx.  We still go
 * through the motions of assigning an ASN (really, just refreshing
 * the ASN generation in this particular case) to keep the logic sane
 * in other parts of the code.
 */
u_int	pmap_max_asn;		/* max ASN supported by the system */
				/* next ASN and current ASN generation */
struct pmap_asn_info pmap_asn_info[ALPHA_MAXPROCS];

/*
 * Locking:
 *
 *	* pm_mtx (per-pmap) - This lock protects all of the members
 *	  of the pmap structure itself.
 *
 *	* pvh_mtx (per-page) - This locks protects the list of mappings
 *	  of a (managed) physical page.
 *
 *	* pmap_all_pmaps_mtx - This lock protects the global list of
 *	  all pmaps.  Note that a pm_slock must never be held while this
 *	  lock is held.
 *
 *	* pmap_growkernel_mtx - This lock protects pmap_growkernel()
 *	  and the pmap_maxkvaddr variable.
 *
 *	  There is a lock ordering constraint for pmap_growkernel_mtx.
 *	  pmap_growkernel() acquires the locks in the following order:
 *
 *		pmap_growkernel_mtx -> pmap_all_pmaps_mtx ->
 *		    pmap->pm_mtx
 *
 *	Address space number management (global ASN counters and per-pmap
 *	ASN state) are not locked; they use arrays of values indexed
 *	per-processor.
 *
 *	All internal functions which operate on a pmap are called
 *	with the pmap already locked by the caller (which will be
 *	an interface function).
 */
struct mutex pmap_all_pmaps_mtx;
struct mutex pmap_growkernel_mtx;

#define PMAP_LOCK(pmap)		mtx_enter(&pmap->pm_mtx)
#define PMAP_UNLOCK(pmap)	mtx_leave(&pmap->pm_mtx)

#if defined(MULTIPROCESSOR)
/*
 * TLB Shootdown:
 *
 * When a mapping is changed in a pmap, the TLB entry corresponding to
 * the virtual address must be invalidated on all processors.  In order
 * to accomplish this on systems with multiple processors, messages are
 * sent from the processor which performs the mapping change to all
 * processors on which the pmap is active.  For other processors, the
 * ASN generation numbers for that processor is invalidated, so that
 * the next time the pmap is activated on that processor, a new ASN
 * will be allocated (which implicitly invalidates all TLB entries).
 *
 * Note, we can use the pool allocator to allocate job entries
 * since pool pages are mapped with K0SEG, not with the TLB.
 */
struct pmap_tlb_shootdown_job {
	TAILQ_ENTRY(pmap_tlb_shootdown_job) pj_list;
	vaddr_t pj_va;			/* virtual address */
	pmap_t pj_pmap;			/* the pmap which maps the address */
	pt_entry_t pj_pte;		/* the PTE bits */
};

/* If we have more pending jobs than this, we just nail the whole TLB. */
#define	PMAP_TLB_SHOOTDOWN_MAXJOBS	6

struct pmap_tlb_shootdown_q {
	TAILQ_HEAD(, pmap_tlb_shootdown_job) pq_head;
	TAILQ_HEAD(, pmap_tlb_shootdown_job) pq_free;
	int pq_pte;			/* aggregate low PTE bits */
	int pq_tbia;			/* pending global flush */
	struct mutex pq_mtx;		/* queue lock */
	struct pmap_tlb_shootdown_job pq_jobs[PMAP_TLB_SHOOTDOWN_MAXJOBS];
} pmap_tlb_shootdown_q[ALPHA_MAXPROCS];

#define	PSJQ_LOCK(pq, s)	mtx_enter(&(pq)->pq_mtx)
#define	PSJQ_UNLOCK(pq, s)	mtx_leave(&(pq)->pq_mtx)

void	pmap_tlb_shootdown_q_drain(struct pmap_tlb_shootdown_q *);
struct pmap_tlb_shootdown_job *pmap_tlb_shootdown_job_get
	    (struct pmap_tlb_shootdown_q *);
void	pmap_tlb_shootdown_job_put(struct pmap_tlb_shootdown_q *,
	    struct pmap_tlb_shootdown_job *);
#endif /* MULTIPROCESSOR */

#define	PAGE_IS_MANAGED(pa)	(vm_physseg_find(atop(pa), NULL) != -1)

/*
 * Internal routines
 */
void	alpha_protection_init(void);
void	pmap_do_remove(pmap_t, vaddr_t, vaddr_t);
boolean_t pmap_remove_mapping(pmap_t, vaddr_t, pt_entry_t *,
	    boolean_t, cpuid_t);
void	pmap_changebit(struct vm_page *, pt_entry_t, pt_entry_t, cpuid_t);

/*
 * PT page management functions.
 */
int	pmap_lev1map_create(pmap_t, cpuid_t);
void	pmap_lev1map_destroy(pmap_t);
int	pmap_ptpage_alloc(pmap_t, pt_entry_t *, int);
void	pmap_ptpage_free(pmap_t, pt_entry_t *);
void	pmap_l3pt_delref(pmap_t, vaddr_t, pt_entry_t *, cpuid_t);
void	pmap_l2pt_delref(pmap_t, pt_entry_t *, pt_entry_t *);
void	pmap_l1pt_delref(pmap_t, pt_entry_t *);

void	*pmap_l1pt_alloc(struct pool *, int, int *);
void	pmap_l1pt_free(struct pool *, void *);

struct pool_allocator pmap_l1pt_allocator = {
	pmap_l1pt_alloc, pmap_l1pt_free, 0,
};

void	pmap_l1pt_ctor(pt_entry_t *);

/*
 * PV table management functions.
 */
int	pmap_pv_enter(pmap_t, struct vm_page *, vaddr_t, pt_entry_t *,
	    boolean_t);
void	pmap_pv_remove(pmap_t, struct vm_page *, vaddr_t, boolean_t);
void	*pmap_pv_page_alloc(struct pool *, int, int *);
void	pmap_pv_page_free(struct pool *, void *);

struct pool_allocator pmap_pv_page_allocator = {
	pmap_pv_page_alloc, pmap_pv_page_free, 0,
};

#ifdef DEBUG
void	pmap_pv_dump(paddr_t);
#endif

#define	pmap_pv_alloc()		pool_get(&pmap_pv_pool, PR_NOWAIT)
#define	pmap_pv_free(pv)	pool_put(&pmap_pv_pool, (pv))

/*
 * ASN management functions.
 */
void	pmap_asn_alloc(pmap_t, cpuid_t);

/*
 * Misc. functions.
 */
boolean_t pmap_physpage_alloc(int, paddr_t *);
void	pmap_physpage_free(paddr_t);
int	pmap_physpage_addref(void *);
int	pmap_physpage_delref(void *);

/* pmap_physpage_alloc() page usage */
#define	PGU_NORMAL		0		/* free or normal use */
#define	PGU_PVENT		1		/* PV entries */
#define	PGU_L1PT		2		/* level 1 page table */
#define	PGU_L2PT		3		/* level 2 page table */
#define	PGU_L3PT		4		/* level 3 page table */

/*
 * PMAP_ISACTIVE{,_TEST}:
 *
 *	Check to see if a pmap is active on the current processor.
 */
#define	PMAP_ISACTIVE_TEST(pm, cpu_id)					\
	(((pm)->pm_cpus & (1UL << (cpu_id))) != 0)

#if defined(DEBUG) && !defined(MULTIPROCESSOR)
#define	PMAP_ISACTIVE(pm, cpu_id)					\
({									\
	/*								\
	 * XXX This test is not MP-safe.				\
	 */								\
	int isactive_ = PMAP_ISACTIVE_TEST(pm, cpu_id);			\
									\
	if (curproc != NULL && curproc->p_vmspace != NULL &&		\
	    (pm) != pmap_kernel() &&					\
	    (isactive_ ^ ((pm) == curproc->p_vmspace->vm_map.pmap)))	\
		panic("PMAP_ISACTIVE, isa: %d pm: %p curpm:%p",		\
		    isactive_, (pm), curproc->p_vmspace->vm_map.pmap);	\
	(isactive_);							\
})
#else
#define	PMAP_ISACTIVE(pm, cpu_id)	PMAP_ISACTIVE_TEST(pm, cpu_id)
#endif /* DEBUG && !MULTIPROCESSOR */

/*
 * PMAP_ACTIVATE_ASN_SANITY:
 *
 *	DEBUG sanity checks for ASNs within PMAP_ACTIVATE.
 */
#ifdef DEBUG
#define	PMAP_ACTIVATE_ASN_SANITY(pmap, cpu_id)				\
do {									\
	struct pmap_asn_info *__pma = &(pmap)->pm_asni[(cpu_id)];	\
	struct pmap_asn_info *__cpma = &pmap_asn_info[(cpu_id)];	\
									\
	if ((pmap)->pm_lev1map == kernel_lev1map) {			\
		/*							\
		 * This pmap implementation also ensures that pmaps	\
		 * referencing kernel_lev1map use a reserved ASN	\
		 * ASN to prevent the PALcode from servicing a TLB	\
		 * miss	with the wrong PTE.				\
		 */							\
		if (__pma->pma_asn != PMAP_ASN_RESERVED) {		\
			printf("kernel_lev1map with non-reserved ASN "	\
			    "(line %d)\n", __LINE__);			\
			panic("PMAP_ACTIVATE_ASN_SANITY");		\
		}							\
	} else {							\
		if (__pma->pma_asngen != __cpma->pma_asngen) {		\
			/*						\
			 * ASN generation number isn't valid!		\
			 */						\
			printf("pmap asngen %lu, current %lu "		\
			    "(line %d)\n",				\
			    __pma->pma_asngen, 				\
			    __cpma->pma_asngen, 			\
			    __LINE__);					\
			panic("PMAP_ACTIVATE_ASN_SANITY");		\
		}							\
		if (__pma->pma_asn == PMAP_ASN_RESERVED) {		\
			/*						\
			 * DANGER WILL ROBINSON!  We're going to	\
			 * pollute the VPT TLB entries!			\
			 */						\
			printf("Using reserved ASN! (line %d)\n",	\
			    __LINE__);					\
			panic("PMAP_ACTIVATE_ASN_SANITY");		\
		}							\
	}								\
} while (0)
#else
#define	PMAP_ACTIVATE_ASN_SANITY(pmap, cpu_id)	/* nothing */
#endif

/*
 * PMAP_ACTIVATE:
 *
 *	This is essentially the guts of pmap_activate(), without
 *	ASN allocation.  This is used by pmap_activate(),
 *	pmap_lev1map_create(), and pmap_lev1map_destroy().
 *
 *	This is called only when it is known that a pmap is "active"
 *	on the current processor; the ASN must already be valid.
 */
#define	PMAP_ACTIVATE(pmap, p, cpu_id)					\
do {									\
	PMAP_ACTIVATE_ASN_SANITY(pmap, cpu_id);				\
									\
	(p)->p_addr->u_pcb.pcb_hw.apcb_ptbr =				\
	    ALPHA_K0SEG_TO_PHYS((vaddr_t)(pmap)->pm_lev1map) >> PGSHIFT; \
	(p)->p_addr->u_pcb.pcb_hw.apcb_asn =				\
	    (pmap)->pm_asni[(cpu_id)].pma_asn;				\
									\
	if ((p) == curproc) {						\
		/*							\
		 * Page table base register has changed; switch to	\
		 * our own context again so that it will take effect.	\
		 */							\
		(void) alpha_pal_swpctx((u_long)p->p_md.md_pcbpaddr);	\
	}								\
} while (0)

/*
 * PMAP_SET_NEEDISYNC:
 *
 *	Mark that a user pmap needs an I-stream synch on its
 *	way back out to userspace.
 */
#define	PMAP_SET_NEEDISYNC(pmap)	(pmap)->pm_needisync = ~0UL

/*
 * PMAP_SYNC_ISTREAM:
 *
 *	Synchronize the I-stream for the specified pmap.  For user
 *	pmaps, this is deferred until a process using the pmap returns
 *	to userspace.
 */
#if defined(MULTIPROCESSOR)
#define	PMAP_SYNC_ISTREAM_KERNEL()					\
do {									\
	alpha_pal_imb();						\
	alpha_broadcast_ipi(ALPHA_IPI_IMB);				\
} while (0)

#define	PMAP_SYNC_ISTREAM_USER(pmap)					\
do {									\
	alpha_multicast_ipi((pmap)->pm_cpus, ALPHA_IPI_AST);		\
	/* for curcpu, do it before userret() */			\
} while (0)
#else
#define	PMAP_SYNC_ISTREAM_KERNEL()	alpha_pal_imb()
#define	PMAP_SYNC_ISTREAM_USER(pmap)	/* done before userret() */
#endif /* MULTIPROCESSOR */

#define	PMAP_SYNC_ISTREAM(pmap)						\
do {									\
	if ((pmap) == pmap_kernel())					\
		PMAP_SYNC_ISTREAM_KERNEL();				\
	else								\
		PMAP_SYNC_ISTREAM_USER(pmap);				\
} while (0)

/*
 * PMAP_INVALIDATE_ASN:
 *
 *	Invalidate the specified pmap's ASN, so as to force allocation
 *	of a new one the next time pmap_asn_alloc() is called.
 *
 *	NOTE: THIS MUST ONLY BE CALLED IF AT LEAST ONE OF THE FOLLOWING
 *	CONDITIONS ARE TRUE:
 *
 *		(1) The pmap references the global kernel_lev1map.
 *
 *		(2) The pmap is not active on the current processor.
 */
#define	PMAP_INVALIDATE_ASN(pmap, cpu_id)				\
do {									\
	(pmap)->pm_asni[(cpu_id)].pma_asn = PMAP_ASN_RESERVED;		\
} while (0)

/*
 * PMAP_INVALIDATE_TLB:
 *
 *	Invalidate the TLB entry for the pmap/va pair.
 */
#define	PMAP_INVALIDATE_TLB(pmap, va, hadasm, isactive, cpu_id)		\
do {									\
	if ((hadasm) || (isactive)) {					\
		/*							\
		 * Simply invalidating the TLB entry and I-cache	\
		 * works in this case.					\
		 */							\
		ALPHA_TBIS((va));					\
	} else if ((pmap)->pm_asni[(cpu_id)].pma_asngen ==		\
		    pmap_asn_info[(cpu_id)].pma_asngen) {		\
		/*							\
		 * We can't directly invalidate the TLB entry		\
		 * in this case, so we have to force allocation		\
		 * of a new ASN the next time this pmap becomes		\
		 * active.						\
		 */							\
		PMAP_INVALIDATE_ASN((pmap), (cpu_id));			\
	}								\
		/*							\
		 * Nothing to do in this case; the next time the	\
		 * pmap becomes active on this processor, a new		\
		 * ASN will be allocated anyway.			\
		 */							\
} while (0)

/*
 * PMAP_KERNEL_PTE:
 *
 *	Get a kernel PTE.
 *
 *	If debugging, do a table walk.  If not debugging, just use
 *	the Virtual Page Table, since all kernel page tables are
 *	pre-allocated and mapped in.
 */
#ifdef DEBUG
#define	PMAP_KERNEL_PTE(va)						\
({									\
	pt_entry_t *l1pte_, *l2pte_;					\
									\
	l1pte_ = pmap_l1pte(pmap_kernel(), va);				\
	if (pmap_pte_v(l1pte_) == 0) {					\
		printf("kernel level 1 PTE not valid, va 0x%lx "	\
		    "(line %d)\n", (va), __LINE__);			\
		panic("PMAP_KERNEL_PTE");				\
	}								\
	l2pte_ = pmap_l2pte(pmap_kernel(), va, l1pte_);			\
	if (pmap_pte_v(l2pte_) == 0) {					\
		printf("kernel level 2 PTE not valid, va 0x%lx "	\
		    "(line %d)\n", (va), __LINE__);			\
		panic("PMAP_KERNEL_PTE");				\
	}								\
	pmap_l3pte(pmap_kernel(), va, l2pte_);				\
})
#else
#define	PMAP_KERNEL_PTE(va)	(&VPT[VPT_INDEX((va))])
#endif

/*
 * PMAP_SET_PTE:
 *
 *	Set a PTE to a specified value.
 */
#define	PMAP_SET_PTE(ptep, val)	*(ptep) = (val)

/*
 * PMAP_STAT_{INCR,DECR}:
 *
 *	Increment or decrement a pmap statistic.
 */
#define	PMAP_STAT_INCR(s, v)	atomic_add_ulong((unsigned long *)(&(s)), (v))
#define	PMAP_STAT_DECR(s, v)	atomic_sub_ulong((unsigned long *)(&(s)), (v))

/*
 * pmap_bootstrap:
 *
 *	Bootstrap the system to run with virtual memory.
 *
 *	Note: no locking is necessary in this function.
 */
void
pmap_bootstrap(paddr_t ptaddr, u_int maxasn, u_long ncpuids)
{
	vsize_t lev2mapsize, lev3mapsize;
	pt_entry_t *lev2map, *lev3map;
	pt_entry_t pte;
	int i;
#ifdef MULTIPROCESSOR
	int j;
#endif

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_BOOTSTRAP))
		printf("pmap_bootstrap(0x%lx, %u)\n", ptaddr, maxasn);
#endif

	/*
	 * Compute the number of pages kmem_map will have.
	 */
	kmeminit_nkmempages();

	/*
	 * Figure out how many PTEs are necessary to map the kernel.
	 */
	lev3mapsize = (VM_PHYS_SIZE + 16 * NCARGS + PAGER_MAP_SIZE) /
	    PAGE_SIZE + (maxthread * UPAGES) + nkmempages;

#ifdef SYSVSHM
	lev3mapsize += shminfo.shmall;
#endif
	lev3mapsize = roundup(lev3mapsize, NPTEPG);

	/*
	 * Allocate a level 1 PTE table for the kernel.
	 * This is always one page long.
	 * IF THIS IS NOT A MULTIPLE OF PAGE_SIZE, ALL WILL GO TO HELL.
	 */
	kernel_lev1map = (pt_entry_t *)
	    pmap_steal_memory(sizeof(pt_entry_t) * NPTEPG, NULL, NULL);

	/*
	 * Allocate a level 2 PTE table for the kernel.
	 * These must map all of the level3 PTEs.
	 * IF THIS IS NOT A MULTIPLE OF PAGE_SIZE, ALL WILL GO TO HELL.
	 */
	lev2mapsize = roundup(howmany(lev3mapsize, NPTEPG), NPTEPG);
	lev2map = (pt_entry_t *)
	    pmap_steal_memory(sizeof(pt_entry_t) * lev2mapsize, NULL, NULL);

	/*
	 * Allocate a level 3 PTE table for the kernel.
	 * Contains lev3mapsize PTEs.
	 */
	lev3map = (pt_entry_t *)
	    pmap_steal_memory(sizeof(pt_entry_t) * lev3mapsize, NULL, NULL);

	/*
	 * Set up level 1 page table
	 */

	/* Map all of the level 2 pte pages */
	for (i = 0; i < howmany(lev2mapsize, NPTEPG); i++) {
		pte = (ALPHA_K0SEG_TO_PHYS(((vaddr_t)lev2map) +
		    (i*PAGE_SIZE)) >> PGSHIFT) << PG_SHIFT;
		pte |= PG_V | PG_ASM | PG_KRE | PG_KWE | PG_WIRED;
		kernel_lev1map[l1pte_index(VM_MIN_KERNEL_ADDRESS +
		    (i*PAGE_SIZE*NPTEPG*NPTEPG))] = pte;
	}

	/* Map the virtual page table */
	pte = (ALPHA_K0SEG_TO_PHYS((vaddr_t)kernel_lev1map) >> PGSHIFT)
	    << PG_SHIFT;
	pte |= PG_V | PG_KRE | PG_KWE; /* NOTE NO ASM */
	kernel_lev1map[l1pte_index(VPTBASE)] = pte;
	VPT = (pt_entry_t *)VPTBASE;

	/*
	 * Set up level 2 page table.
	 */
	/* Map all of the level 3 pte pages */
	for (i = 0; i < howmany(lev3mapsize, NPTEPG); i++) {
		pte = (ALPHA_K0SEG_TO_PHYS(((vaddr_t)lev3map) +
		    (i*PAGE_SIZE)) >> PGSHIFT) << PG_SHIFT;
		pte |= PG_V | PG_ASM | PG_KRE | PG_KWE | PG_WIRED;
		lev2map[l2pte_index(VM_MIN_KERNEL_ADDRESS+
		    (i*PAGE_SIZE*NPTEPG))] = pte;
	}

	/* Initialize the pmap_growkernel_mtx. */
	mtx_init(&pmap_growkernel_mtx, IPL_NONE);

	/*
	 * Set up level three page table (lev3map)
	 */
	/* Nothing to do; it's already zeroed */

	/*
	 * Initialize `FYI' variables.  Note we're relying on
	 * the fact that BSEARCH sorts the vm_physmem[] array
	 * for us.
	 */
	avail_start = ptoa(vm_physmem[0].start);
	avail_end = ptoa(vm_physmem[vm_nphysseg - 1].end);

	pmap_maxkvaddr = VM_MIN_KERNEL_ADDRESS + lev3mapsize * PAGE_SIZE;

#if 0
	printf("avail_start = 0x%lx\n", avail_start);
	printf("avail_end = 0x%lx\n", avail_end);
#endif

	/*
	 * Initialize the pmap pools and list.
	 */
	pmap_ncpuids = ncpuids;
	pool_init(&pmap_pmap_pool, PMAP_SIZEOF(pmap_ncpuids), 0, IPL_NONE, 0,
	    "pmappl", &pool_allocator_single);
	pool_init(&pmap_l1pt_pool, PAGE_SIZE, 0, IPL_VM, 0,
	    "l1ptpl", &pmap_l1pt_allocator);
	pool_init(&pmap_pv_pool, sizeof(struct pv_entry), 0, IPL_VM, 0,
	    "pvpl", &pmap_pv_page_allocator);

	TAILQ_INIT(&pmap_all_pmaps);

	/*
	 * Initialize the ASN logic.
	 */
	pmap_max_asn = maxasn;
	for (i = 0; i < ALPHA_MAXPROCS; i++) {
		pmap_asn_info[i].pma_asn = 1;
		pmap_asn_info[i].pma_asngen = 0;
	}

	/*
	 * Initialize the locks.
	 */
	mtx_init(&pmap_all_pmaps_mtx, IPL_NONE);

	/*
	 * Initialize kernel pmap.  Note that all kernel mappings
	 * have PG_ASM set, so the ASN doesn't really matter for
	 * the kernel pmap.  Also, since the kernel pmap always
	 * references kernel_lev1map, it always has an invalid ASN
	 * generation.
	 */
	memset(pmap_kernel(), 0, sizeof(pmap_kernel()));
	pmap_kernel()->pm_lev1map = kernel_lev1map;
	pmap_kernel()->pm_count = 1;
	for (i = 0; i < ALPHA_MAXPROCS; i++) {
		pmap_kernel()->pm_asni[i].pma_asn = PMAP_ASN_RESERVED;
		pmap_kernel()->pm_asni[i].pma_asngen =
		    pmap_asn_info[i].pma_asngen;
	}
	TAILQ_INSERT_TAIL(&pmap_all_pmaps, pmap_kernel(), pm_list);
	mtx_init(&pmap_kernel()->pm_mtx, IPL_VM);

#if defined(MULTIPROCESSOR)
	/*
	 * Initialize the TLB shootdown queues.
	 */
	for (i = 0; i < ALPHA_MAXPROCS; i++) {
		TAILQ_INIT(&pmap_tlb_shootdown_q[i].pq_head);
		TAILQ_INIT(&pmap_tlb_shootdown_q[i].pq_free);
		for (j = 0; j < PMAP_TLB_SHOOTDOWN_MAXJOBS; j++)
			TAILQ_INSERT_TAIL(&pmap_tlb_shootdown_q[i].pq_free,
			    &pmap_tlb_shootdown_q[i].pq_jobs[j], pj_list);
		mtx_init(&pmap_tlb_shootdown_q[i].pq_mtx, IPL_IPI);
	}
#endif

	/*
	 * Set up proc0's PCB such that the ptbr points to the right place
	 * and has the kernel pmap's (really unused) ASN.
	 */
	proc0.p_addr->u_pcb.pcb_hw.apcb_ptbr =
	    ALPHA_K0SEG_TO_PHYS((vaddr_t)kernel_lev1map) >> PGSHIFT;
	proc0.p_addr->u_pcb.pcb_hw.apcb_asn =
	    pmap_kernel()->pm_asni[cpu_number()].pma_asn;

	/*
	 * Mark the kernel pmap `active' on this processor.
	 */
	atomic_setbits_ulong(&pmap_kernel()->pm_cpus,
	    (1UL << cpu_number()));
}

/*
 * pmap_steal_memory:		[ INTERFACE ]
 *
 *	Bootstrap memory allocator (alternative to vm_bootstrap_steal_memory()).
 *	This function allows for early dynamic memory allocation until the
 *	virtual memory system has been bootstrapped.  After that point, either
 *	kmem_alloc or malloc should be used.  This function works by stealing
 *	pages from the (to be) managed page pool, then implicitly mapping the
 *	pages (by using their k0seg addresses) and zeroing them.
 *
 *	It may be used once the physical memory segments have been pre-loaded
 *	into the vm_physmem[] array.  Early memory allocation MUST use this
 *	interface!  This cannot be used after vm_page_startup(), and will
 *	generate a panic if tried.
 *
 *	Note that this memory will never be freed, and in essence it is wired
 *	down.
 *
 *	Note: no locking is necessary in this function.
 */
vaddr_t
pmap_steal_memory(vsize_t size, vaddr_t *vstartp, vaddr_t *vendp)
{
	int bank, npgs, x;
	vaddr_t va;
	paddr_t pa;

	size = round_page(size);
	npgs = atop(size);

#if 0
	printf("PSM: size 0x%lx (npgs 0x%x)\n", size, npgs);
#endif

	for (bank = 0; bank < vm_nphysseg; bank++) {
		if (uvm.page_init_done == TRUE)
			panic("pmap_steal_memory: called _after_ bootstrap");

#if 0
		printf("     bank %d: avail_start 0x%lx, start 0x%lx, "
		    "avail_end 0x%lx\n", bank, vm_physmem[bank].avail_start,
		    vm_physmem[bank].start, vm_physmem[bank].avail_end);
#endif

		if (vm_physmem[bank].avail_start != vm_physmem[bank].start ||
		    vm_physmem[bank].avail_start >= vm_physmem[bank].avail_end)
			continue;

#if 0
		printf("             avail_end - avail_start = 0x%lx\n",
		    vm_physmem[bank].avail_end - vm_physmem[bank].avail_start);
#endif

		if ((vm_physmem[bank].avail_end - vm_physmem[bank].avail_start)
		    < npgs)
			continue;

		/*
		 * There are enough pages here; steal them!
		 */
		pa = ptoa(vm_physmem[bank].avail_start);
		vm_physmem[bank].avail_start += npgs;
		vm_physmem[bank].start += npgs;

		/*
		 * Have we used up this segment?
		 */
		if (vm_physmem[bank].avail_start == vm_physmem[bank].end) {
			if (vm_nphysseg == 1)
				panic("pmap_steal_memory: out of memory!");

			/* Remove this segment from the list. */
			vm_nphysseg--;
			for (x = bank; x < vm_nphysseg; x++) {
				/* structure copy */
				vm_physmem[x] = vm_physmem[x + 1];
			}
		}

		/*
		 * Fill these in for the caller; we don't modify them,
		 * but the upper layers still want to know.
		 */
		if (vstartp)
			*vstartp = VM_MIN_KERNEL_ADDRESS;
		if (vendp)
			*vendp = VM_MAX_KERNEL_ADDRESS;

		va = ALPHA_PHYS_TO_K0SEG(pa);
		memset((caddr_t)va, 0, size);
		pmap_pages_stolen += npgs;
		return (va);
	}

	/*
	 * If we got here, this was no memory left.
	 */
	panic("pmap_steal_memory: no memory to steal");
}

/*
 * pmap_init:			[ INTERFACE ]
 *
 *	Initialize the pmap module.  Called by uvm_init(), to initialize any
 *	structures that the pmap system needs to map virtual memory.
 *
 *	Note: no locking is necessary in this function.
 */
void
pmap_init(void)
{

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_init()\n");
#endif

	/* initialize protection array */
	alpha_protection_init();

	/*
	 * Set a low water mark on the pv_entry pool, so that we are
	 * more likely to have these around even in extreme memory
	 * starvation.
	 */
	pool_setlowat(&pmap_pv_pool, pmap_pv_lowat);

	/*
	 * Now it is safe to enable pv entry recording.
	 */
	pmap_initialized = TRUE;

#if 0
	for (bank = 0; bank < vm_nphysseg; bank++) {
		printf("bank %d\n", bank);
		printf("\tstart = 0x%x\n", ptoa(vm_physmem[bank].start));
		printf("\tend = 0x%x\n", ptoa(vm_physmem[bank].end));
		printf("\tavail_start = 0x%x\n",
		    ptoa(vm_physmem[bank].avail_start));
		printf("\tavail_end = 0x%x\n",
		    ptoa(vm_physmem[bank].avail_end));
	}
#endif
}

/*
 * pmap_create:			[ INTERFACE ]
 *
 *	Create and return a physical map.
 */
pmap_t
pmap_create(void)
{
	pmap_t pmap;
	int i;

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_CREATE))
		printf("pmap_create()\n");
#endif

	pmap = pool_get(&pmap_pmap_pool, PR_WAITOK|PR_ZERO);

	pmap->pm_count = 1;
	for (i = 0; i < pmap_ncpuids; i++) {
		pmap->pm_asni[i].pma_asn = PMAP_ASN_RESERVED;
		/* XXX Locking? */
		pmap->pm_asni[i].pma_asngen = pmap_asn_info[i].pma_asngen;
	}
	mtx_init(&pmap->pm_mtx, IPL_VM);

	for (;;) {
		mtx_enter(&pmap_growkernel_mtx);
		i = pmap_lev1map_create(pmap, cpu_number());
		mtx_leave(&pmap_growkernel_mtx);
		if (i == 0)
			break;
		uvm_wait(__func__);
	}

	mtx_enter(&pmap_all_pmaps_mtx);
	TAILQ_INSERT_TAIL(&pmap_all_pmaps, pmap, pm_list);
	mtx_leave(&pmap_all_pmaps_mtx);

	return (pmap);
}

/*
 * pmap_destroy:		[ INTERFACE ]
 *
 *	Drop the reference count on the specified pmap, releasing
 *	all resources if the reference count drops to zero.
 */
void
pmap_destroy(pmap_t pmap)
{
	int refs;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_destroy(%p)\n", pmap);
#endif

	refs = atomic_dec_int_nv(&pmap->pm_count);
	if (refs > 0)
		return;

	/*
	 * Remove it from the global list of all pmaps.
	 */
	mtx_enter(&pmap_all_pmaps_mtx);
	TAILQ_REMOVE(&pmap_all_pmaps, pmap, pm_list);
	mtx_leave(&pmap_all_pmaps_mtx);

	mtx_enter(&pmap_growkernel_mtx);
	pmap_lev1map_destroy(pmap);
	mtx_leave(&pmap_growkernel_mtx);

	pool_put(&pmap_pmap_pool, pmap);
}

/*
 * pmap_reference:		[ INTERFACE ]
 *
 *	Add a reference to the specified pmap.
 */
void
pmap_reference(pmap_t pmap)
{

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_reference(%p)\n", pmap);
#endif

	atomic_inc_int(&pmap->pm_count);
}

/*
 * pmap_remove:			[ INTERFACE ]
 *
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 */
void
pmap_remove(pmap_t pmap, vaddr_t sva, vaddr_t eva)
{

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_REMOVE|PDB_PROTECT))
		printf("pmap_remove(%p, %lx, %lx)\n", pmap, sva, eva);
#endif

	pmap_do_remove(pmap, sva, eva);
}

/*
 * pmap_do_remove:
 *
 *	This actually removes the range of addresses from the
 *	specified map.
 */
void
pmap_do_remove(pmap_t pmap, vaddr_t sva, vaddr_t eva)
{
	pt_entry_t *l1pte, *l2pte, *l3pte;
	pt_entry_t *saved_l1pte, *saved_l2pte, *saved_l3pte;
	vaddr_t l1eva, l2eva, vptva;
	boolean_t needisync = FALSE;
	cpuid_t cpu_id = cpu_number();

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_REMOVE|PDB_PROTECT))
		printf("pmap_remove(%p, %lx, %lx)\n", pmap, sva, eva);
#endif

	/*
	 * If this is the kernel pmap, we can use a faster method
	 * for accessing the PTEs (since the PT pages are always
	 * resident).
	 *
	 * Note that this routine should NEVER be called from an
	 * interrupt context; pmap_kremove() is used for that.
	 */
	if (pmap == pmap_kernel()) {
		PMAP_LOCK(pmap);

		while (sva < eva) {
			l3pte = PMAP_KERNEL_PTE(sva);
			if (pmap_pte_v(l3pte)) {
#ifdef DIAGNOSTIC
				if (PAGE_IS_MANAGED(pmap_pte_pa(l3pte)) &&
				    pmap_pte_pv(l3pte) == 0)
					panic("pmap_remove: managed page "
					    "without PG_PVLIST for 0x%lx",
					    sva);
#endif
				needisync |= pmap_remove_mapping(pmap, sva,
				    l3pte, TRUE, cpu_id);
			}
			sva += PAGE_SIZE;
		}

		PMAP_UNLOCK(pmap);

		if (needisync)
			PMAP_SYNC_ISTREAM_KERNEL();
		return;
	}

#ifdef DIAGNOSTIC
	if (sva > VM_MAXUSER_ADDRESS || eva > VM_MAXUSER_ADDRESS)
		panic("pmap_remove: (0x%lx - 0x%lx) user pmap, kernel "
		    "address range", sva, eva);
#endif

	PMAP_LOCK(pmap);

	/*
	 * If we're already referencing the kernel_lev1map, there
	 * is no work for us to do.
	 */
	if (pmap->pm_lev1map == kernel_lev1map)
		goto out;

	saved_l1pte = l1pte = pmap_l1pte(pmap, sva);

	/*
	 * Add a reference to the L1 table to it won't get
	 * removed from under us.
	 */
	pmap_physpage_addref(saved_l1pte);

	for (; sva < eva; sva = l1eva, l1pte++) {
		l1eva = alpha_trunc_l1seg(sva) + ALPHA_L1SEG_SIZE;
		if (pmap_pte_v(l1pte)) {
			saved_l2pte = l2pte = pmap_l2pte(pmap, sva, l1pte);

			/*
			 * Add a reference to the L2 table so it won't
			 * get removed from under us.
			 */
			pmap_physpage_addref(saved_l2pte);

			for (; sva < l1eva && sva < eva; sva = l2eva, l2pte++) {
				l2eva =
				    alpha_trunc_l2seg(sva) + ALPHA_L2SEG_SIZE;
				if (pmap_pte_v(l2pte)) {
					saved_l3pte = l3pte =
					    pmap_l3pte(pmap, sva, l2pte);

					/*
					 * Add a reference to the L3 table so
					 * it won't get removed from under us.
					 */
					pmap_physpage_addref(saved_l3pte);

					/*
					 * Remember this sva; if the L3 table
					 * gets removed, we need to invalidate
					 * the VPT TLB entry for it.
					 */
					vptva = sva;

					for (; sva < l2eva && sva < eva;
					     sva += PAGE_SIZE, l3pte++) {
						if (pmap_pte_v(l3pte)) {
							needisync |=
							    pmap_remove_mapping(
								pmap, sva,
								l3pte, TRUE,
								cpu_id);
						}
					}

					/*
					 * Remove the reference to the L3
					 * table that we added above.  This
					 * may free the L3 table.
					 */
					pmap_l3pt_delref(pmap, vptva,
					    saved_l3pte, cpu_id);
				}
			}

			/*
			 * Remove the reference to the L2 table that we
			 * added above.  This may free the L2 table.
			 */
			pmap_l2pt_delref(pmap, l1pte, saved_l2pte);
		}
	}

	/*
	 * Remove the reference to the L1 table that we added above.
	 * This may free the L1 table.
	 */
	pmap_l1pt_delref(pmap, saved_l1pte);

	if (needisync)
		PMAP_SYNC_ISTREAM_USER(pmap);

 out:
	PMAP_UNLOCK(pmap);
}

/*
 * pmap_page_protect:		[ INTERFACE ]
 *
 *	Lower the permission for all mappings to a given page to
 *	the permissions specified.
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	pmap_t pmap;
	pv_entry_t pv;
	boolean_t needkisync = FALSE;
	cpuid_t cpu_id = cpu_number();
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if ((pmapdebug & (PDB_FOLLOW|PDB_PROTECT)) ||
	    (prot == PROT_NONE && (pmapdebug & PDB_REMOVE)))
		printf("pmap_page_protect(%p, %x)\n", pg, prot);
#endif

	switch (prot) {
	case PROT_READ | PROT_WRITE | PROT_EXEC:
	case PROT_READ | PROT_WRITE:
		return;

	/* copy_on_write */
	case PROT_READ | PROT_EXEC:
	case PROT_READ:
		mtx_enter(&pg->mdpage.pvh_mtx);
		for (pv = pg->mdpage.pvh_list; pv != NULL; pv = pv->pv_next) {
			if (*pv->pv_pte & (PG_KWE | PG_UWE)) {
				*pv->pv_pte &= ~(PG_KWE | PG_UWE);
				PMAP_INVALIDATE_TLB(pv->pv_pmap, pv->pv_va,
				    pmap_pte_asm(pv->pv_pte),
				    PMAP_ISACTIVE(pv->pv_pmap, cpu_id), cpu_id);
				PMAP_TLB_SHOOTDOWN(pv->pv_pmap, pv->pv_va,
				    pmap_pte_asm(pv->pv_pte));
			}
		}
		mtx_leave(&pg->mdpage.pvh_mtx);
		PMAP_TLB_SHOOTNOW();
		return;

	/* remove_all */
	default:
		break;
	}

	mtx_enter(&pg->mdpage.pvh_mtx);
	while ((pv = pg->mdpage.pvh_list) != NULL) {
		pmap_reference(pv->pv_pmap);
		pmap = pv->pv_pmap;
		mtx_leave(&pg->mdpage.pvh_mtx);

		PMAP_LOCK(pmap);

		/*
		 * We dropped the pvlist lock before grabbing the pmap
		 * lock to avoid lock ordering problems.  This means
		 * we have to check the pvlist again since somebody
		 * else might have modified it.  All we care about is
		 * that the pvlist entry matches the pmap we just
		 * locked.  If it doesn't, unlock the pmap and try
		 * again.
		 */
		mtx_enter(&pg->mdpage.pvh_mtx);
		if ((pv = pg->mdpage.pvh_list) == NULL ||
		    pv->pv_pmap != pmap) {
			mtx_leave(&pg->mdpage.pvh_mtx);
			PMAP_UNLOCK(pmap);
			pmap_destroy(pmap);
			mtx_enter(&pg->mdpage.pvh_mtx);
			continue;
		}

#ifdef DEBUG
		if (pmap_pte_v(pmap_l2pte(pv->pv_pmap, pv->pv_va, NULL)) == 0 ||
		    pmap_pte_pa(pv->pv_pte) != VM_PAGE_TO_PHYS(pg))
			panic("pmap_page_protect: bad mapping");
#endif
		if (pmap_remove_mapping(pmap, pv->pv_va, pv->pv_pte,
		    FALSE, cpu_id) == TRUE) {
			if (pmap == pmap_kernel())
				needkisync |= TRUE;
			else
				PMAP_SYNC_ISTREAM_USER(pmap);
		}
		mtx_leave(&pg->mdpage.pvh_mtx);
		PMAP_UNLOCK(pmap);
		pmap_destroy(pmap);
		mtx_enter(&pg->mdpage.pvh_mtx);
	}
	mtx_leave(&pg->mdpage.pvh_mtx);

	if (needkisync)
		PMAP_SYNC_ISTREAM_KERNEL();
}

/*
 * pmap_protect:		[ INTERFACE ]
 *
 *	Set the physical protection on the specified range of this map
 *	as requested.
 */
void
pmap_protect(pmap_t pmap, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	pt_entry_t *l1pte, *l2pte, *l3pte, bits;
	boolean_t isactive;
	boolean_t hadasm;
	vaddr_t l1eva, l2eva;
	cpuid_t cpu_id = cpu_number();
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_PROTECT))
		printf("pmap_protect(%p, %lx, %lx, %x)\n",
		    pmap, sva, eva, prot);
#endif

	if ((prot & PROT_READ) == PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	PMAP_LOCK(pmap);

	bits = pte_prot(pmap, prot);
	isactive = PMAP_ISACTIVE(pmap, cpu_id);

	l1pte = pmap_l1pte(pmap, sva);
	for (; sva < eva; sva = l1eva, l1pte++) {
		l1eva = alpha_trunc_l1seg(sva) + ALPHA_L1SEG_SIZE;
		if (!pmap_pte_v(l1pte))
			continue;

		l2pte = pmap_l2pte(pmap, sva, l1pte);
		for (; sva < l1eva && sva < eva; sva = l2eva, l2pte++) {
			l2eva = alpha_trunc_l2seg(sva) + ALPHA_L2SEG_SIZE;
			if (!pmap_pte_v(l2pte))
				continue;

			l3pte = pmap_l3pte(pmap, sva, l2pte);
			for (; sva < l2eva && sva < eva;
			     sva += PAGE_SIZE, l3pte++) {
				if (!pmap_pte_v(l3pte))
					continue;

				if (pmap_pte_prot_chg(l3pte, bits)) {
					hadasm = (pmap_pte_asm(l3pte) != 0);
					pmap_pte_set_prot(l3pte, bits);
					PMAP_INVALIDATE_TLB(pmap, sva, hadasm,
					   isactive, cpu_id);
					PMAP_TLB_SHOOTDOWN(pmap, sva,
					   hadasm ? PG_ASM : 0);
				}
			}
		}
	}

	PMAP_TLB_SHOOTNOW();

	if (prot & PROT_EXEC)
		PMAP_SYNC_ISTREAM(pmap);

	PMAP_UNLOCK(pmap);
}

/*
 * pmap_enter:			[ INTERFACE ]
 *
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	If specified, the page will be wired down, meaning
 *	that the related pte can not be reclaimed.
 *
 *	Note:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */
int
pmap_enter(pmap_t pmap, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	struct vm_page *pg;
	pt_entry_t *pte, npte, opte;
	paddr_t opa;
	boolean_t tflush = TRUE;
	boolean_t hadasm = FALSE;	/* XXX gcc -Wuninitialized */
	boolean_t needisync = FALSE;
	boolean_t setisync = FALSE;
	boolean_t isactive;
	boolean_t wired;
	cpuid_t cpu_id = cpu_number();
	int error = 0;
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_ENTER))
		printf("pmap_enter(%p, %lx, %lx, %x, %x)\n",
		       pmap, va, pa, prot, flags);
#endif
	pg = PHYS_TO_VM_PAGE(pa);
	isactive = PMAP_ISACTIVE(pmap, cpu_id);
	wired = (flags & PMAP_WIRED) != 0;

	/*
	 * Determine what we need to do about the I-stream.  If
	 * PROT_EXEC is set, we mark a user pmap as needing
	 * an I-sync on the way back out to userspace.  We always
	 * need an immediate I-sync for the kernel pmap.
	 */
	if (prot & PROT_EXEC) {
		if (pmap == pmap_kernel())
			needisync = TRUE;
		else {
			setisync = TRUE;
			needisync = (pmap->pm_cpus != 0);
		}
	}

	PMAP_LOCK(pmap);

	if (pmap == pmap_kernel()) {
#ifdef DIAGNOSTIC
		/*
		 * Sanity check the virtual address.
		 */
		if (va < VM_MIN_KERNEL_ADDRESS)
			panic("pmap_enter: kernel pmap, invalid va 0x%lx", va);
#endif
		pte = PMAP_KERNEL_PTE(va);
	} else {
		pt_entry_t *l1pte, *l2pte;

#ifdef DIAGNOSTIC
		/*
		 * Sanity check the virtual address.
		 */
		if (va >= VM_MAXUSER_ADDRESS)
			panic("pmap_enter: user pmap, invalid va 0x%lx", va);
#endif

		KASSERT(pmap->pm_lev1map != kernel_lev1map);

		/*
		 * Check to see if the level 1 PTE is valid, and
		 * allocate a new level 2 page table page if it's not.
		 * A reference will be added to the level 2 table when
		 * the level 3 table is created.
		 */
		l1pte = pmap_l1pte(pmap, va);
		if (pmap_pte_v(l1pte) == 0) {
			pmap_physpage_addref(l1pte);
			error = pmap_ptpage_alloc(pmap, l1pte, PGU_L2PT);
			if (error) {
				pmap_l1pt_delref(pmap, l1pte);
				if (flags & PMAP_CANFAIL)
					goto out;
				panic("pmap_enter: unable to create L2 PT "
				    "page");
			}
#ifdef DEBUG
			if (pmapdebug & PDB_PTPAGE)
				printf("pmap_enter: new level 2 table at "
				    "0x%lx\n", pmap_pte_pa(l1pte));
#endif
		}

		/*
		 * Check to see if the level 2 PTE is valid, and
		 * allocate a new level 3 page table page if it's not.
		 * A reference will be added to the level 3 table when
		 * the mapping is validated.
		 */
		l2pte = pmap_l2pte(pmap, va, l1pte);
		if (pmap_pte_v(l2pte) == 0) {
			pmap_physpage_addref(l2pte);
			error = pmap_ptpage_alloc(pmap, l2pte, PGU_L3PT);
			if (error) {
				pmap_l2pt_delref(pmap, l1pte, l2pte);
				if (flags & PMAP_CANFAIL)
					goto out;
				panic("pmap_enter: unable to create L3 PT "
				    "page");
			}
#ifdef DEBUG
			if (pmapdebug & PDB_PTPAGE)
				printf("pmap_enter: new level 3 table at "
				    "0x%lx\n", pmap_pte_pa(l2pte));
#endif
		}

		/*
		 * Get the PTE that will map the page.
		 */
		pte = pmap_l3pte(pmap, va, l2pte);
	}

	/* Remember all of the old PTE; used for TBI check later. */
	opte = *pte;

	/*
	 * Check to see if the old mapping is valid.  If not, validate the
	 * new one immediately.
	 */
	if (pmap_pte_v(pte) == 0) {
		/*
		 * No need to invalidate the TLB in this case; an invalid
		 * mapping won't be in the TLB, and a previously valid
		 * mapping would have been flushed when it was invalidated.
		 */
		tflush = FALSE;

		/*
		 * No need to synchronize the I-stream, either, for basically
		 * the same reason.
		 */
		setisync = needisync = FALSE;

		if (pmap != pmap_kernel()) {
			/*
			 * New mappings gain a reference on the level 3
			 * table.
			 */
			pmap_physpage_addref(pte);
		}
		goto validate_enterpv;
	}

	opa = pmap_pte_pa(pte);
	hadasm = (pmap_pte_asm(pte) != 0);

	if (opa == pa) {
		/*
		 * Mapping has not changed; must be a protection or
		 * wiring change.
		 */
		if (pmap_pte_w_chg(pte, wired ? PG_WIRED : 0)) {
#ifdef DEBUG
			if (pmapdebug & PDB_ENTER)
				printf("pmap_enter: wiring change -> %d\n",
				    wired);
#endif
			/*
			 * Adjust the wiring count.
			 */
			if (wired)
				PMAP_STAT_INCR(pmap->pm_stats.wired_count, 1);
			else
				PMAP_STAT_DECR(pmap->pm_stats.wired_count, 1);
		}

		/*
		 * Set the PTE.
		 */
		goto validate;
	}

	/*
	 * The mapping has changed.  We need to invalidate the
	 * old mapping before creating the new one.
	 */
#ifdef DEBUG
	if (pmapdebug & PDB_ENTER)
		printf("pmap_enter: removing old mapping 0x%lx\n", va);
#endif
	if (pmap != pmap_kernel()) {
		/*
		 * Gain an extra reference on the level 3 table.
		 * pmap_remove_mapping() will delete a reference,
		 * and we don't want the table to be erroneously
		 * freed.
		 */
		pmap_physpage_addref(pte);
	}
	needisync |= pmap_remove_mapping(pmap, va, pte, TRUE, cpu_id);

 validate_enterpv:
	/*
	 * Enter the mapping into the pv_table if appropriate.
	 */
	if (pg != NULL) {
		error = pmap_pv_enter(pmap, pg, va, pte, TRUE);
		if (error) {
			pmap_l3pt_delref(pmap, va, pte, cpu_id);
			if (flags & PMAP_CANFAIL)
				goto out;
			panic("pmap_enter: unable to enter mapping in PV "
			    "table");
		}
	}

	/*
	 * Increment counters.
	 */
	PMAP_STAT_INCR(pmap->pm_stats.resident_count, 1);
	if (wired)
		PMAP_STAT_INCR(pmap->pm_stats.wired_count, 1);

 validate:
	/*
	 * Build the new PTE.
	 */
	npte = ((pa >> PGSHIFT) << PG_SHIFT) | pte_prot(pmap, prot) | PG_V;
	if (pg != NULL) {
		int attrs;

#ifdef DIAGNOSTIC
		if ((flags & PROT_MASK) & ~prot)
			panic("pmap_enter: access type exceeds prot");
#endif
		if (flags & PROT_WRITE)
			atomic_setbits_int(&pg->pg_flags,
			    PG_PMAP_REF | PG_PMAP_MOD);
		else if (flags & PROT_MASK)
			atomic_setbits_int(&pg->pg_flags, PG_PMAP_REF);

		/*
		 * Set up referenced/modified emulation for new mapping.
		 */
		attrs = pg->pg_flags;
		if ((attrs & PG_PMAP_REF) == 0)
			npte |= PG_FOR | PG_FOW | PG_FOE;
		else if ((attrs & PG_PMAP_MOD) == 0)
			npte |= PG_FOW;

		/*
		 * Mapping was entered on PV list.
		 */
		npte |= PG_PVLIST;
	}
	if (wired)
		npte |= PG_WIRED;
#ifdef DEBUG
	if (pmapdebug & PDB_ENTER)
		printf("pmap_enter: new pte = 0x%lx\n", npte);
#endif

	/*
	 * If the PALcode portion of the new PTE is the same as the
	 * old PTE, no TBI is necessary.
	 */
	if (PG_PALCODE(opte) == PG_PALCODE(npte))
		tflush = FALSE;

	/*
	 * Set the new PTE.
	 */
	PMAP_SET_PTE(pte, npte);

	/*
	 * Invalidate the TLB entry for this VA and any appropriate
	 * caches.
	 */
	if (tflush) {
		PMAP_INVALIDATE_TLB(pmap, va, hadasm, isactive, cpu_id);
		PMAP_TLB_SHOOTDOWN(pmap, va, hadasm ? PG_ASM : 0);
		PMAP_TLB_SHOOTNOW();
	}
	if (setisync)
		PMAP_SET_NEEDISYNC(pmap);
	if (needisync)
		PMAP_SYNC_ISTREAM(pmap);

out:
	PMAP_UNLOCK(pmap);

	return error;
}

/*
 * pmap_kenter_pa:		[ INTERFACE ]
 *
 *	Enter a va -> pa mapping into the kernel pmap without any
 *	physical->virtual tracking.
 *
 *	Note: no locking is necessary in this function.
 */
void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	pt_entry_t *pte, npte;
	cpuid_t cpu_id = cpu_number();
	boolean_t needisync = FALSE;
	pmap_t pmap = pmap_kernel();
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_ENTER))
		printf("pmap_kenter_pa(%lx, %lx, %x)\n",
		    va, pa, prot);
#endif

#ifdef DIAGNOSTIC
	/*
	 * Sanity check the virtual address.
	 */
	if (va < VM_MIN_KERNEL_ADDRESS)
		panic("pmap_kenter_pa: kernel pmap, invalid va 0x%lx", va);
#endif

	pte = PMAP_KERNEL_PTE(va);

	if (pmap_pte_v(pte) == 0)
		PMAP_STAT_INCR(pmap->pm_stats.resident_count, 1);
	if (pmap_pte_w(pte) == 0)
		PMAP_STAT_DECR(pmap->pm_stats.wired_count, 1);

	if ((prot & PROT_EXEC) != 0 || pmap_pte_exec(pte))
		needisync = TRUE;

	/*
	 * Build the new PTE.
	 */
	npte = ((pa >> PGSHIFT) << PG_SHIFT) | pte_prot(pmap_kernel(), prot) |
	    PG_V | PG_WIRED;

	/*
	 * Set the new PTE.
	 */
	PMAP_SET_PTE(pte, npte);
#if defined(MULTIPROCESSOR)
	alpha_mb();		/* XXX alpha_wmb()? */
#endif

	/*
	 * Invalidate the TLB entry for this VA and any appropriate
	 * caches.
	 */
	PMAP_INVALIDATE_TLB(pmap, va, TRUE, TRUE, cpu_id);
	PMAP_TLB_SHOOTDOWN(pmap, va, PG_ASM);
	PMAP_TLB_SHOOTNOW();

	if (needisync)
		PMAP_SYNC_ISTREAM_KERNEL();
}

/*
 * pmap_kremove:		[ INTERFACE ]
 *
 *	Remove a mapping entered with pmap_kenter_pa() starting at va,
 *	for size bytes (assumed to be page rounded).
 */
void
pmap_kremove(vaddr_t va, vsize_t size)
{
	pt_entry_t *pte;
	boolean_t needisync = FALSE;
	cpuid_t cpu_id = cpu_number();
	pmap_t pmap = pmap_kernel();
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_ENTER))
		printf("pmap_kremove(%lx, %lx)\n",
		    va, size);
#endif

#ifdef DIAGNOSTIC
	if (va < VM_MIN_KERNEL_ADDRESS)
		panic("pmap_kremove: user address");
#endif

	for (; size != 0; size -= PAGE_SIZE, va += PAGE_SIZE) {
		pte = PMAP_KERNEL_PTE(va);
		if (pmap_pte_v(pte)) {
#ifdef DIAGNOSTIC
			if (pmap_pte_pv(pte))
				panic("pmap_kremove: PG_PVLIST mapping for "
				    "0x%lx", va);
#endif
			if (pmap_pte_exec(pte))
				needisync = TRUE;

			/* Zap the mapping. */
			PMAP_SET_PTE(pte, PG_NV);
#if defined(MULTIPROCESSOR)
			alpha_mb();		/* XXX alpha_wmb()? */
#endif
			PMAP_INVALIDATE_TLB(pmap, va, TRUE, TRUE, cpu_id);
			PMAP_TLB_SHOOTDOWN(pmap, va, PG_ASM);

			/* Update stats. */
			PMAP_STAT_DECR(pmap->pm_stats.resident_count, 1);
			PMAP_STAT_DECR(pmap->pm_stats.wired_count, 1);
		}
	}

	PMAP_TLB_SHOOTNOW();

	if (needisync)
		PMAP_SYNC_ISTREAM_KERNEL();
}

/*
 * pmap_unwire:			[ INTERFACE ]
 *
 *	Clear the wired attribute for a map/virtual-address pair.
 *
 *	The mapping must already exist in the pmap.
 */
void
pmap_unwire(pmap_t pmap, vaddr_t va)
{
	pt_entry_t *pte;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_unwire(%p, %lx)\n", pmap, va);
#endif

	PMAP_LOCK(pmap);

	pte = pmap_l3pte(pmap, va, NULL);
#ifdef DIAGNOSTIC
	if (pte == NULL || pmap_pte_v(pte) == 0)
		panic("pmap_unwire");
#endif

	/*
	 * If wiring actually changed (always?) clear the wire bit and
	 * update the wire count.  Note that wiring is not a hardware
	 * characteristic so there is no need to invalidate the TLB.
	 */
	if (pmap_pte_w_chg(pte, 0)) {
		pmap_pte_set_w(pte, FALSE);
		PMAP_STAT_DECR(pmap->pm_stats.wired_count, 1);
	}
#ifdef DIAGNOSTIC
	else {
		printf("pmap_unwire: wiring for pmap %p va 0x%lx "
		    "didn't change!\n", pmap, va);
	}
#endif

	PMAP_UNLOCK(pmap);
}

/*
 * pmap_extract:		[ INTERFACE ]
 *
 *	Extract the physical address associated with the given
 *	pmap/virtual address pair.
 */
boolean_t
pmap_extract(pmap_t pmap, vaddr_t va, paddr_t *pap)
{
	pt_entry_t *l1pte, *l2pte, *l3pte;
	boolean_t rv = FALSE;
	paddr_t pa;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_extract(%p, %lx) -> ", pmap, va);
#endif

	if (pmap == pmap_kernel()) {
		if (va < ALPHA_K0SEG_BASE) {
			/* nothing */
		} else if (va <= ALPHA_K0SEG_END) {
			pa = ALPHA_K0SEG_TO_PHYS(va);
			*pap = pa;
			rv = TRUE;
		} else {
			l3pte = PMAP_KERNEL_PTE(va);
			if (pmap_pte_v(l3pte)) {
				pa = pmap_pte_pa(l3pte) | (va & PGOFSET);
				*pap = pa;
				rv = TRUE;
			}
		}
		goto out_nolock;
	}

	PMAP_LOCK(pmap);

	l1pte = pmap_l1pte(pmap, va);
	if (pmap_pte_v(l1pte) == 0)
		goto out;

	l2pte = pmap_l2pte(pmap, va, l1pte);
	if (pmap_pte_v(l2pte) == 0)
		goto out;

	l3pte = pmap_l3pte(pmap, va, l2pte);
	if (pmap_pte_v(l3pte) == 0)
		goto out;

	pa = pmap_pte_pa(l3pte) | (va & PGOFSET);
	*pap = pa;
	rv = TRUE;
 out:
	PMAP_UNLOCK(pmap);
 out_nolock:
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW) {
		if (rv)
			printf("0x%lx\n", pa);
		else
			printf("failed\n");
	}
#endif
	return (rv);
}

/*
 * pmap_activate:		[ INTERFACE ]
 *
 *	Activate the pmap used by the specified process.  This includes
 *	reloading the MMU context if the current process, and marking
 *	the pmap in use by the processor.
 *
 *	Note: We may use only spin locks here, since we are called
 *	by a critical section in cpu_switch()!
 */
void
pmap_activate(struct proc *p)
{
	struct pmap *pmap = p->p_vmspace->vm_map.pmap;
	cpuid_t cpu_id = cpu_number();

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_activate(%p)\n", p);
#endif

	/* Mark the pmap in use by this processor. */
	atomic_setbits_ulong(&pmap->pm_cpus, (1UL << cpu_id));

	/* Allocate an ASN. */
	pmap_asn_alloc(pmap, cpu_id);

	PMAP_ACTIVATE(pmap, p, cpu_id);
}

/*
 * pmap_deactivate:		[ INTERFACE ]
 *
 *	Mark that the pmap used by the specified process is no longer
 *	in use by the processor.
 *
 *	The comment above pmap_activate() wrt. locking applies here,
 *	as well.  Note that we use only a single `atomic' operation,
 *	so no locking is necessary.
 */
void
pmap_deactivate(struct proc *p)
{
	struct pmap *pmap = p->p_vmspace->vm_map.pmap;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_deactivate(%p)\n", p);
#endif

	/*
	 * Mark the pmap no longer in use by this processor.
	 */
	atomic_clearbits_ulong(&pmap->pm_cpus, (1UL << cpu_number()));
}

/*
 * pmap_zero_page:		[ INTERFACE ]
 *
 *	Zero the specified (machine independent) page by mapping the page
 *	into virtual memory and clear its contents, one machine dependent
 *	page at a time.
 *
 *	Note: no locking is necessary in this function.
 */
void
pmap_zero_page(struct vm_page *pg)
{
	paddr_t phys = VM_PAGE_TO_PHYS(pg);
	u_long *p0, *p1, *pend;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_zero_page(%lx)\n", phys);
#endif

	p0 = (u_long *)ALPHA_PHYS_TO_K0SEG(phys);
	p1 = NULL;
	pend = (u_long *)((u_long)p0 + PAGE_SIZE);

	/*
	 * Unroll the loop a bit, doing 16 quadwords per iteration.
	 * Do only 8 back-to-back stores, and alternate registers.
	 */
	do {
		__asm volatile(
		"# BEGIN loop body\n"
		"	addq	%2, (8 * 8), %1		\n"
		"	stq	$31, (0 * 8)(%0)	\n"
		"	stq	$31, (1 * 8)(%0)	\n"
		"	stq	$31, (2 * 8)(%0)	\n"
		"	stq	$31, (3 * 8)(%0)	\n"
		"	stq	$31, (4 * 8)(%0)	\n"
		"	stq	$31, (5 * 8)(%0)	\n"
		"	stq	$31, (6 * 8)(%0)	\n"
		"	stq	$31, (7 * 8)(%0)	\n"
		"					\n"
		"	addq	%3, (8 * 8), %0		\n"
		"	stq	$31, (0 * 8)(%1)	\n"
		"	stq	$31, (1 * 8)(%1)	\n"
		"	stq	$31, (2 * 8)(%1)	\n"
		"	stq	$31, (3 * 8)(%1)	\n"
		"	stq	$31, (4 * 8)(%1)	\n"
		"	stq	$31, (5 * 8)(%1)	\n"
		"	stq	$31, (6 * 8)(%1)	\n"
		"	stq	$31, (7 * 8)(%1)	\n"
		"	# END loop body"
		: "=r" (p0), "=r" (p1)
		: "0" (p0), "1" (p1)
		: "memory");
	} while (p0 < pend);
}

/*
 * pmap_copy_page:		[ INTERFACE ]
 *
 *	Copy the specified (machine independent) page by mapping the page
 *	into virtual memory and using memcpy to copy the page, one machine
 *	dependent page at a time.
 *
 *	Note: no locking is necessary in this function.
 */
void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t src = VM_PAGE_TO_PHYS(srcpg);
	paddr_t dst = VM_PAGE_TO_PHYS(dstpg);
	caddr_t s, d;

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_copy_page(%lx, %lx)\n", src, dst);
#endif
	s = (caddr_t)ALPHA_PHYS_TO_K0SEG(src);
	d = (caddr_t)ALPHA_PHYS_TO_K0SEG(dst);
	memcpy(d, s, PAGE_SIZE);
}

/*
 * pmap_clear_modify:		[ INTERFACE ]
 *
 *	Clear the modify bits on the specified physical page.
 */
boolean_t
pmap_clear_modify(struct vm_page *pg)
{
	boolean_t rv = FALSE;
	cpuid_t cpu_id = cpu_number();

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_clear_modify(%p)\n", pg);
#endif

	mtx_enter(&pg->mdpage.pvh_mtx);
	if (pg->pg_flags & PG_PMAP_MOD) {
		rv = TRUE;
		pmap_changebit(pg, PG_FOW, ~0, cpu_id);
		atomic_clearbits_int(&pg->pg_flags, PG_PMAP_MOD);
	}
	mtx_leave(&pg->mdpage.pvh_mtx);

	return (rv);
}

/*
 * pmap_clear_reference:	[ INTERFACE ]
 *
 *	Clear the reference bit on the specified physical page.
 */
boolean_t
pmap_clear_reference(struct vm_page *pg)
{
	boolean_t rv = FALSE;
	cpuid_t cpu_id = cpu_number();

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_clear_reference(%p)\n", pg);
#endif

	mtx_enter(&pg->mdpage.pvh_mtx);
	if (pg->pg_flags & PG_PMAP_REF) {
		rv = TRUE;
		pmap_changebit(pg, PG_FOR | PG_FOW | PG_FOE, ~0, cpu_id);
		atomic_clearbits_int(&pg->pg_flags, PG_PMAP_REF);
	}
	mtx_leave(&pg->mdpage.pvh_mtx);

	return (rv);
}

/*
 * pmap_is_referenced:		[ INTERFACE ]
 *
 *	Return whether or not the specified physical page is referenced
 *	by any physical maps.
 */
boolean_t
pmap_is_referenced(struct vm_page *pg)
{
	boolean_t rv;

	rv = ((pg->pg_flags & PG_PMAP_REF) != 0);
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW) {
		printf("pmap_is_referenced(%p) -> %c\n", pg, "FT"[rv]);
	}
#endif
	return (rv);
}

/*
 * pmap_is_modified:		[ INTERFACE ]
 *
 *	Return whether or not the specified physical page is modified
 *	by any physical maps.
 */
boolean_t
pmap_is_modified(struct vm_page *pg)
{
	boolean_t rv;

	rv = ((pg->pg_flags & PG_PMAP_MOD) != 0);
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW) {
		printf("pmap_is_modified(%p) -> %c\n", pg, "FT"[rv]);
	}
#endif
	return (rv);
}

/*
 * Miscellaneous support routines follow
 */

/*
 * alpha_protection_init:
 *
 *	Initialize Alpha protection code array.
 *
 *	Note: no locking is necessary in this function.
 */
void
alpha_protection_init(void)
{
	int prot, *kp, *up;

	kp = protection_codes[0];
	up = protection_codes[1];

	for (prot = 0; prot < 8; prot++) {
		kp[prot] = PG_ASM;
		up[prot] = 0;

		if (prot & PROT_READ) {
			kp[prot] |= PG_KRE;
			up[prot] |= PG_KRE | PG_URE;
		}
		if (prot & PROT_WRITE) {
			kp[prot] |= PG_KWE;
			up[prot] |= PG_KWE | PG_UWE;
		}
		if (prot & PROT_EXEC) {
			kp[prot] |= PG_EXEC | PG_KRE;
			up[prot] |= PG_EXEC | PG_KRE | PG_URE;
		} else {
			kp[prot] |= PG_FOE;
			up[prot] |= PG_FOE;
		}
	}
}

/*
 * pmap_remove_mapping:
 *
 *	Invalidate a single page denoted by pmap/va.
 *
 *	If (pte != NULL), it is the already computed PTE for the page.
 *
 *	Note: locking in this function is complicated by the fact
 *	that we can be called when the PV list is already locked.
 *	(pmap_page_protect()).  In this case, the caller must be
 *	careful to get the next PV entry while we remove this entry
 *	from beneath it.  We assume that the pmap itself is already
 *	locked; dolock applies only to the PV list.
 *
 *	Returns TRUE or FALSE, indicating if an I-stream sync needs
 *	to be initiated (for this CPU or for other CPUs).
 */
boolean_t
pmap_remove_mapping(pmap_t pmap, vaddr_t va, pt_entry_t *pte,
    boolean_t dolock, cpuid_t cpu_id)
{
	paddr_t pa;
	struct vm_page *pg;
	boolean_t onpv;
	boolean_t hadasm;
	boolean_t isactive;
	boolean_t needisync = FALSE;
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_REMOVE|PDB_PROTECT))
		printf("pmap_remove_mapping(%p, %lx, %p, %d, %ld)\n",
		       pmap, va, pte, dolock, cpu_id);
#endif

	/*
	 * PTE not provided, compute it from pmap and va.
	 */
	if (pte == PT_ENTRY_NULL) {
		pte = pmap_l3pte(pmap, va, NULL);
		if (pmap_pte_v(pte) == 0)
			return (FALSE);
	}

	pa = pmap_pte_pa(pte);
	onpv = (pmap_pte_pv(pte) != 0);
	if (onpv) {
		/*
		 * Remove it from the PV table such that nobody will
		 * attempt to modify the PTE behind our back.
		 */
		pg = PHYS_TO_VM_PAGE(pa);
		KASSERT(pg != NULL);
		pmap_pv_remove(pmap, pg, va, dolock);
	}

	hadasm = (pmap_pte_asm(pte) != 0);
	isactive = PMAP_ISACTIVE(pmap, cpu_id);

	/*
	 * Determine what we need to do about the I-stream.  If
	 * PG_EXEC was set, we mark a user pmap as needing an
	 * I-sync on the way out to userspace.  We always need
	 * an immediate I-sync for the kernel pmap.
	 */
	if (pmap_pte_exec(pte)) {
		if (pmap == pmap_kernel())
			needisync = TRUE;
		else {
			PMAP_SET_NEEDISYNC(pmap);
			needisync = (pmap->pm_cpus != 0);
		}
	}

	/*
	 * Update statistics
	 */
	if (pmap_pte_w(pte))
		PMAP_STAT_DECR(pmap->pm_stats.wired_count, 1);
	PMAP_STAT_DECR(pmap->pm_stats.resident_count, 1);

	/*
	 * Invalidate the PTE after saving the reference modify info.
	 */
#ifdef DEBUG
	if (pmapdebug & PDB_REMOVE)
		printf("remove: invalidating pte at %p\n", pte);
#endif
	PMAP_SET_PTE(pte, PG_NV);

	PMAP_INVALIDATE_TLB(pmap, va, hadasm, isactive, cpu_id);
	PMAP_TLB_SHOOTDOWN(pmap, va, hadasm ? PG_ASM : 0);
	PMAP_TLB_SHOOTNOW();

	/*
	 * If we're removing a user mapping, check to see if we
	 * can free page table pages.
	 */
	if (pmap != pmap_kernel()) {
		/*
		 * Delete the reference on the level 3 table.  It will
		 * delete references on the level 2 and 1 tables as
		 * appropriate.
		 */
		pmap_l3pt_delref(pmap, va, pte, cpu_id);
	}

	return (needisync);
}

/*
 * pmap_changebit:
 *
 *	Set or clear the specified PTE bits for all mappings on the
 *	specified page.
 *
 *	Note: we assume that the pvlist is already locked.  There is no
 *	need to lock the pmap itself as amapping cannot be removed while
 *	we are holding the pvlist lock.
 */
void
pmap_changebit(struct vm_page *pg, u_long set, u_long mask, cpuid_t cpu_id)
{
	pv_entry_t pv;
	pt_entry_t *pte, npte;
	vaddr_t va;
	boolean_t hadasm, isactive;
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

#ifdef DEBUG
	if (pmapdebug & PDB_BITS)
		printf("pmap_changebit(0x%lx, 0x%lx, 0x%lx)\n",
		    VM_PAGE_TO_PHYS(pg), set, mask);
#endif

	MUTEX_ASSERT_LOCKED(&pg->mdpage.pvh_mtx);

	/*
	 * Loop over all current mappings setting/clearing as appropriate.
	 */
	for (pv = pg->mdpage.pvh_list; pv != NULL; pv = pv->pv_next) {
		va = pv->pv_va;

		pte = pv->pv_pte;
		npte = (*pte | set) & mask;
		if (*pte != npte) {
			hadasm = (pmap_pte_asm(pte) != 0);
			isactive = PMAP_ISACTIVE(pv->pv_pmap, cpu_id);
			PMAP_SET_PTE(pte, npte);
			PMAP_INVALIDATE_TLB(pv->pv_pmap, va, hadasm, isactive,
			    cpu_id);
			PMAP_TLB_SHOOTDOWN(pv->pv_pmap, va,
			    hadasm ? PG_ASM : 0);
		}
	}

	PMAP_TLB_SHOOTNOW();
}

/*
 * pmap_emulate_reference:
 *
 *	Emulate reference and/or modified bit hits.
 *	Return non-zero if this was an execute fault on a non-exec mapping,
 *	otherwise return 0.
 */
int
pmap_emulate_reference(struct proc *p, vaddr_t v, int user, int type)
{
	struct pmap *pmap;
	pt_entry_t faultoff, *pte;
	struct vm_page *pg;
	paddr_t pa;
	boolean_t didlock = FALSE;
	boolean_t exec = FALSE;
	cpuid_t cpu_id = cpu_number();

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("pmap_emulate_reference: %p, 0x%lx, %d, %d\n",
		    p, v, user, type);
#endif

	/*
	 * Convert process and virtual address to physical address.
	 */
	if (v >= VM_MIN_KERNEL_ADDRESS) {
		if (user)
			panic("pmap_emulate_reference: user ref to kernel");
		/*
		 * No need to lock here; kernel PT pages never go away.
		 */
		pte = PMAP_KERNEL_PTE(v);
	} else {
#ifdef DIAGNOSTIC
		if (p == NULL)
			panic("pmap_emulate_reference: bad proc");
		if (p->p_vmspace == NULL)
			panic("pmap_emulate_reference: bad p_vmspace");
#endif
		pmap = p->p_vmspace->vm_map.pmap;
		PMAP_LOCK(pmap);
		didlock = TRUE;
		pte = pmap_l3pte(pmap, v, NULL);
		/*
		 * We'll unlock below where we're done with the PTE.
		 */
	}
	if (pte == NULL || !pmap_pte_v(pte)) {
		if (didlock)
			PMAP_UNLOCK(pmap);
		return (0);
	}
	exec = pmap_pte_exec(pte);
	if (!exec && type == ALPHA_MMCSR_FOE) {
		if (didlock)
			PMAP_UNLOCK(pmap);
		return (1);
	}
#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW) {
		printf("\tpte = %p, ", pte);
		printf("*pte = 0x%lx\n", *pte);
	}
#endif
#ifdef DEBUG				/* These checks are more expensive */
#ifndef MULTIPROCESSOR
	/*
	 * Quoting the Alpha ARM 14.3.1.4/5/6:
	 * ``The Translation Buffer may reload and cache the old PTE value
	 *   between the time the FOR (resp. FOW, FOE) fault invalidates the
	 *   old value from the Translation Buffer and the time software
	 *   updates the PTE in memory.  Software that depends on the
	 *   processor-provided invalidate must thus be prepared to take
	 *   another FOR (resp. FOW, FOE) fault on a page after clearing the
	 *   page's PTE<FOR(resp. FOW, FOE)> bit. The second fault will
	 *   invalidate the stale PTE from the Translation Buffer, and the
	 *   processor cannot load another stale copy. Thus, in the worst case,
	 *   a multiprocessor system will take an initial FOR (resp. FOW, FOE)
	 *   fault and then an additional FOR (resp. FOW, FOE) fault on each
	 *   processor. In practice, even a single repetition is unlikely.''
	 *
	 * In practice, spurious faults on the other processors happen, at
	 * least on fast 21264 or better processors.
	 */
	if (type == ALPHA_MMCSR_FOW) {
		if (!(*pte & (user ? PG_UWE : PG_UWE | PG_KWE))) {
			panic("pmap_emulate_reference(%d,%d): "
			    "write but unwritable pte 0x%lx",
			    user, type, *pte);
		}
		if (!(*pte & PG_FOW)) {
			panic("pmap_emulate_reference(%d,%d): "
			    "write but not FOW pte 0x%lx",
			    user, type, *pte);
		}
	} else {
		if (!(*pte & (user ? PG_URE : PG_URE | PG_KRE))) {
			panic("pmap_emulate_reference(%d,%d): "
			    "!write but unreadable pte 0x%lx",
			    user, type, *pte);
		}
		if (!(*pte & (PG_FOR | PG_FOE))) {
			panic("pmap_emulate_reference(%d,%d): "
			    "!write but not FOR|FOE pte 0x%lx",
			    user, type, *pte);
		}
	}
#endif /* MULTIPROCESSOR */
	/* Other diagnostics? */
#endif
	pa = pmap_pte_pa(pte);

	/*
	 * We're now done with the PTE.  If it was a user pmap, unlock
	 * it now.
	 */
	if (didlock)
		PMAP_UNLOCK(pmap);

#ifdef DEBUG
	if (pmapdebug & PDB_FOLLOW)
		printf("\tpa = 0x%lx\n", pa);
#endif

	pg = PHYS_TO_VM_PAGE(pa);

#ifdef DIAGNOSTIC
	if (pg == NULL) {
		panic("pmap_emulate_reference(%p, 0x%lx, %d, %d): "
		    "pa 0x%lx (pte %p 0x%08lx) not managed",
		    p, v, user, type, pa, pte, *pte);
	}
#endif

	/*
	 * Twiddle the appropriate bits to reflect the reference
	 * and/or modification..
	 *
	 * The rules:
	 * 	(1) always mark page as used, and
	 *	(2) if it was a write fault, mark page as modified.
	 */

	mtx_enter(&pg->mdpage.pvh_mtx);
	if (type == ALPHA_MMCSR_FOW) {
		atomic_setbits_int(&pg->pg_flags, PG_PMAP_REF | PG_PMAP_MOD);
		faultoff = PG_FOR | PG_FOW;
	} else {
		atomic_setbits_int(&pg->pg_flags, PG_PMAP_REF);
		faultoff = PG_FOR;
		if (exec) {
			faultoff |= PG_FOE;
		}
	}
	pmap_changebit(pg, 0, ~faultoff, cpu_id);
	mtx_leave(&pg->mdpage.pvh_mtx);

	return (0);
}

#ifdef DEBUG
/*
 * pmap_pv_dump:
 *
 *	Dump the physical->virtual data for the specified page.
 */
void
pmap_pv_dump(paddr_t pa)
{
	struct vm_page *pg;
	pv_entry_t pv;

	pg = PHYS_TO_VM_PAGE(pa);

	printf("pa 0x%lx (attrs = 0x%x):\n",
	    pa, pg->pg_flags & (PG_PMAP_REF | PG_PMAP_MOD));
	mtx_enter(&pg->mdpage.pvh_mtx);
	for (pv = pg->mdpage.pvh_list; pv != NULL; pv = pv->pv_next)
		printf("     pmap %p, va 0x%lx\n",
		    pv->pv_pmap, pv->pv_va);
	mtx_leave(&pg->mdpage.pvh_mtx);
	printf("\n");
}
#endif

/*
 * vtophys:
 *
 *	Return the physical address corresponding to the K0SEG or
 *	K1SEG address provided.
 *
 *	Note: no locking is necessary in this function.
 */
paddr_t
vtophys(vaddr_t vaddr)
{
	pt_entry_t *pte;
	paddr_t paddr = 0;

	if (vaddr < ALPHA_K0SEG_BASE)
		printf("vtophys: invalid vaddr 0x%lx", vaddr);
	else if (vaddr <= ALPHA_K0SEG_END)
		paddr = ALPHA_K0SEG_TO_PHYS(vaddr);
	else {
		pte = PMAP_KERNEL_PTE(vaddr);
		if (pmap_pte_v(pte))
			paddr = pmap_pte_pa(pte) | (vaddr & PGOFSET);
	}

#if 0
	printf("vtophys(0x%lx) -> 0x%lx\n", vaddr, paddr);
#endif

	return (paddr);
}

/******************** pv_entry management ********************/

/*
 * pmap_pv_enter:
 *
 *	Add a physical->virtual entry to the pv_table.
 */
int
pmap_pv_enter(pmap_t pmap, struct vm_page *pg, vaddr_t va, pt_entry_t *pte,
    boolean_t dolock)
{
	pv_entry_t newpv;

	/*
	 * Allocate and fill in the new pv_entry.
	 */
	newpv = pmap_pv_alloc();
	if (newpv == NULL)
		return (ENOMEM);
	newpv->pv_va = va;
	newpv->pv_pmap = pmap;
	newpv->pv_pte = pte;

	if (dolock)
		mtx_enter(&pg->mdpage.pvh_mtx);

#ifdef DEBUG
    {
	pv_entry_t pv;
	/*
	 * Make sure the entry doesn't already exist.
	 */
	for (pv = pg->mdpage.pvh_list; pv != NULL; pv = pv->pv_next) {
		if (pmap == pv->pv_pmap && va == pv->pv_va) {
			printf("pmap = %p, va = 0x%lx\n", pmap, va);
			panic("pmap_pv_enter: already in pv table");
		}
	}
    }
#endif

	/*
	 * ...and put it in the list.
	 */
	newpv->pv_next = pg->mdpage.pvh_list;
	pg->mdpage.pvh_list = newpv;

	if (dolock)
		mtx_leave(&pg->mdpage.pvh_mtx);

	return (0);
}

/*
 * pmap_pv_remove:
 *
 *	Remove a physical->virtual entry from the pv_table.
 */
void
pmap_pv_remove(pmap_t pmap, struct vm_page *pg, vaddr_t va, boolean_t dolock)
{
	pv_entry_t pv, *pvp;

	if (dolock)
		mtx_enter(&pg->mdpage.pvh_mtx);

	/*
	 * Find the entry to remove.
	 */
	for (pvp = &pg->mdpage.pvh_list, pv = *pvp;
	    pv != NULL; pvp = &pv->pv_next, pv = *pvp)
		if (pmap == pv->pv_pmap && va == pv->pv_va)
			break;

#ifdef DEBUG
	if (pv == NULL)
		panic("pmap_pv_remove: not in pv table");
#endif

	*pvp = pv->pv_next;

	if (dolock)
		mtx_leave(&pg->mdpage.pvh_mtx);

	pmap_pv_free(pv);
}

/*
 * pmap_pv_page_alloc:
 *
 *	Allocate a page for the pv_entry pool.
 */
void *
pmap_pv_page_alloc(struct pool *pp, int flags, int *slowdown)
{
	paddr_t pg;

	*slowdown = 0;
	if (pmap_physpage_alloc(PGU_PVENT, &pg))
		return ((void *)ALPHA_PHYS_TO_K0SEG(pg));
	return (NULL);
}

/*
 * pmap_pv_page_free:
 *
 *	Free a pv_entry pool page.
 */
void
pmap_pv_page_free(struct pool *pp, void *v)
{

	pmap_physpage_free(ALPHA_K0SEG_TO_PHYS((vaddr_t)v));
}

/******************** misc. functions ********************/

/*
 * pmap_physpage_alloc:
 *
 *	Allocate a single page from the VM system and return the
 *	physical address for that page.
 */
boolean_t
pmap_physpage_alloc(int usage, paddr_t *pap)
{
	struct vm_page *pg;
	paddr_t pa;

	/*
	 * Don't ask for a zeroed page in the L1PT case -- we will
	 * properly initialize it in the constructor.
	 */

	pg = uvm_pagealloc(NULL, 0, NULL, usage == PGU_L1PT ?
	    UVM_PGA_USERESERVE : UVM_PGA_USERESERVE|UVM_PGA_ZERO);
	if (pg != NULL) {
		pa = VM_PAGE_TO_PHYS(pg);

#ifdef DIAGNOSTIC
		if (pg->wire_count != 0) {
			printf("pmap_physpage_alloc: page 0x%lx has "
			    "%d references\n", pa, pg->wire_count);
			panic("pmap_physpage_alloc");
		}
#endif
		*pap = pa;
		return (TRUE);
	}
	return (FALSE);
}

/*
 * pmap_physpage_free:
 *
 *	Free the single page table page at the specified physical address.
 */
void
pmap_physpage_free(paddr_t pa)
{
	struct vm_page *pg;

	if ((pg = PHYS_TO_VM_PAGE(pa)) == NULL)
		panic("pmap_physpage_free: bogus physical page address");

#ifdef DIAGNOSTIC
	if (pg->wire_count != 0)
		panic("pmap_physpage_free: page still has references");
#endif

	uvm_pagefree(pg);
}

/*
 * pmap_physpage_addref:
 *
 *	Add a reference to the specified special use page.
 */
int
pmap_physpage_addref(void *kva)
{
	struct vm_page *pg;
	paddr_t pa;
	int rval;

	pa = ALPHA_K0SEG_TO_PHYS(trunc_page((vaddr_t)kva));
	pg = PHYS_TO_VM_PAGE(pa);

	rval = ++pg->wire_count;

	return (rval);
}

/*
 * pmap_physpage_delref:
 *
 *	Delete a reference to the specified special use page.
 */
int
pmap_physpage_delref(void *kva)
{
	struct vm_page *pg;
	paddr_t pa;
	int rval;

	pa = ALPHA_K0SEG_TO_PHYS(trunc_page((vaddr_t)kva));
	pg = PHYS_TO_VM_PAGE(pa);

#ifdef DIAGNOSTIC
	/*
	 * Make sure we never have a negative reference count.
	 */
	if (pg->wire_count == 0)
		panic("pmap_physpage_delref: reference count already zero");
#endif

	rval = --pg->wire_count;

	return (rval);
}

/******************** page table page management ********************/

/*
 * pmap_growkernel:		[ INTERFACE ]
 *
 *	Grow the kernel address space.  This is a hint from the
 *	upper layer to pre-allocate more kernel PT pages.
 */
vaddr_t
pmap_growkernel(vaddr_t maxkvaddr)
{
	struct pmap *kpm = pmap_kernel(), *pm;
	paddr_t ptaddr;
	pt_entry_t *l1pte, *l2pte, pte;
	vaddr_t va;
	int l1idx;

	mtx_enter(&pmap_growkernel_mtx);

	if (maxkvaddr <= pmap_maxkvaddr)
		goto out;		/* we are OK */

	va = pmap_maxkvaddr;

	while (va < maxkvaddr) {
		/*
		 * If there is no valid L1 PTE (i.e. no L2 PT page),
		 * allocate a new L2 PT page and insert it into the
		 * L1 map.
		 */
		l1pte = pmap_l1pte(kpm, va);
		if (pmap_pte_v(l1pte) == 0) {
			/*
			 * XXX PGU_NORMAL?  It's not a "traditional" PT page.
			 */
			if (uvm.page_init_done == FALSE) {
				/*
				 * We're growing the kernel pmap early (from
				 * uvm_pageboot_alloc()).  This case must
				 * be handled a little differently.
				 */
				ptaddr = ALPHA_K0SEG_TO_PHYS(
				    pmap_steal_memory(PAGE_SIZE, NULL, NULL));
			} else if (pmap_physpage_alloc(PGU_NORMAL,
				   &ptaddr) == FALSE)
				goto die;
			pte = (atop(ptaddr) << PG_SHIFT) |
			    PG_V | PG_ASM | PG_KRE | PG_KWE | PG_WIRED;
			*l1pte = pte;

			l1idx = l1pte_index(va);

			/* Update all the user pmaps. */
			mtx_enter(&pmap_all_pmaps_mtx);
			for (pm = TAILQ_FIRST(&pmap_all_pmaps);
			     pm != NULL; pm = TAILQ_NEXT(pm, pm_list)) {
				/* Skip the kernel pmap. */
				if (pm == pmap_kernel())
					continue;

				PMAP_LOCK(pm);
				KDASSERT(pm->pm_lev1map != kernel_lev1map);
				pm->pm_lev1map[l1idx] = pte;
				PMAP_UNLOCK(pm);
			}
			mtx_leave(&pmap_all_pmaps_mtx);
		}

		/*
		 * Have an L2 PT page now, add the L3 PT page.
		 */
		l2pte = pmap_l2pte(kpm, va, l1pte);
		KASSERT(pmap_pte_v(l2pte) == 0);
		if (uvm.page_init_done == FALSE) {
			/*
			 * See above.
			 */
			ptaddr = ALPHA_K0SEG_TO_PHYS(
			    pmap_steal_memory(PAGE_SIZE, NULL, NULL));
		} else if (pmap_physpage_alloc(PGU_NORMAL, &ptaddr) == FALSE)
			goto die;
		*l2pte = (atop(ptaddr) << PG_SHIFT) |
		    PG_V | PG_ASM | PG_KRE | PG_KWE | PG_WIRED;
		va += ALPHA_L2SEG_SIZE;
	}

#if 0
	/* Invalidate the L1 PT cache. */
	pool_cache_invalidate(&pmap_l1pt_cache);
#endif

	pmap_maxkvaddr = va;

 out:
	mtx_leave(&pmap_growkernel_mtx);

	return (pmap_maxkvaddr);

 die:
	mtx_leave(&pmap_growkernel_mtx);
	panic("pmap_growkernel: out of memory");
}

/*
 * pmap_lev1map_create:
 *
 *	Create a new level 1 page table for the specified pmap.
 *
 *	Note: growkernel must already by held and the pmap either
 *	already locked or unreferenced globally.
 */
int
pmap_lev1map_create(pmap_t pmap, cpuid_t cpu_id)
{
	pt_entry_t *l1pt;

	KASSERT(pmap != pmap_kernel());
	KASSERT(pmap->pm_asni[cpu_id].pma_asn == PMAP_ASN_RESERVED);

	/* Don't sleep -- we're called with locks held. */
	l1pt = pool_get(&pmap_l1pt_pool, PR_NOWAIT);
	if (l1pt == NULL)
		return (ENOMEM);

	pmap_l1pt_ctor(l1pt);
	pmap->pm_lev1map = l1pt;

	return (0);
}

/*
 * pmap_lev1map_destroy:
 *
 *	Destroy the level 1 page table for the specified pmap.
 *
 *	Note: growkernel must already by held and the pmap either
 *	already locked or unreferenced globally.
 */
void
pmap_lev1map_destroy(pmap_t pmap)
{
	pt_entry_t *l1pt = pmap->pm_lev1map;

	KASSERT(pmap != pmap_kernel());

	/*
	 * Go back to referencing the global kernel_lev1map.
	 */
	pmap->pm_lev1map = kernel_lev1map;

	/*
	 * Free the old level 1 page table page.
	 */
	pool_put(&pmap_l1pt_pool, l1pt);
}

/*
 * pmap_l1pt_ctor:
 *
 *	Constructor for L1 PT pages.
 */
void
pmap_l1pt_ctor(pt_entry_t *l1pt)
{
	pt_entry_t pte;
	int i;

	/*
	 * Initialize the new level 1 table by zeroing the
	 * user portion and copying the kernel mappings into
	 * the kernel portion.
	 */
	for (i = 0; i < l1pte_index(VM_MIN_KERNEL_ADDRESS); i++)
		l1pt[i] = 0;

	for (i = l1pte_index(VM_MIN_KERNEL_ADDRESS);
	     i <= l1pte_index(VM_MAX_KERNEL_ADDRESS); i++)
		l1pt[i] = kernel_lev1map[i];

	/*
	 * Now, map the new virtual page table.  NOTE: NO ASM!
	 */
	pte = ((ALPHA_K0SEG_TO_PHYS((vaddr_t) l1pt) >> PGSHIFT) << PG_SHIFT) |
	    PG_V | PG_KRE | PG_KWE;
	l1pt[l1pte_index(VPTBASE)] = pte;
}

/*
 * pmap_l1pt_alloc:
 *
 *	Page allocator for L1 PT pages.
 *
 *	Note: The growkernel lock is held across allocations
 *	from this pool, so we don't need to acquire it
 *	ourselves.
 */
void *
pmap_l1pt_alloc(struct pool *pp, int flags, int *slowdown)
{
	paddr_t ptpa;

	/*
	 * Attempt to allocate a free page.
	 */
	*slowdown = 0;
	if (pmap_physpage_alloc(PGU_L1PT, &ptpa) == FALSE)
		return (NULL);

	return ((void *) ALPHA_PHYS_TO_K0SEG(ptpa));
}

/*
 * pmap_l1pt_free:
 *
 *	Page freer for L1 PT pages.
 */
void
pmap_l1pt_free(struct pool *pp, void *v)
{

	pmap_physpage_free(ALPHA_K0SEG_TO_PHYS((vaddr_t) v));
}

/*
 * pmap_ptpage_alloc:
 *
 *	Allocate a level 2 or level 3 page table page, and
 *	initialize the PTE that references it.
 *
 *	Note: the pmap must already be locked.
 */
int
pmap_ptpage_alloc(pmap_t pmap, pt_entry_t *pte, int usage)
{
	paddr_t ptpa;

	/*
	 * Allocate the page table page.
	 */
	if (pmap_physpage_alloc(usage, &ptpa) == FALSE)
		return (ENOMEM);

	/*
	 * Initialize the referencing PTE.
	 */
	PMAP_SET_PTE(pte, ((ptpa >> PGSHIFT) << PG_SHIFT) |
	    PG_V | PG_KRE | PG_KWE | PG_WIRED |
	    (pmap == pmap_kernel() ? PG_ASM : 0));

	return (0);
}

/*
 * pmap_ptpage_free:
 *
 *	Free the level 2 or level 3 page table page referenced
 *	be the provided PTE.
 *
 *	Note: the pmap must already be locked.
 */
void
pmap_ptpage_free(pmap_t pmap, pt_entry_t *pte)
{
	paddr_t ptpa;

	/*
	 * Extract the physical address of the page from the PTE
	 * and clear the entry.
	 */
	ptpa = pmap_pte_pa(pte);
	PMAP_SET_PTE(pte, PG_NV);

#ifdef DEBUG
	pmap_zero_page(PHYS_TO_VM_PAGE(ptpa));
#endif
	pmap_physpage_free(ptpa);
}

/*
 * pmap_l3pt_delref:
 *
 *	Delete a reference on a level 3 PT page.  If the reference drops
 *	to zero, free it.
 *
 *	Note: the pmap must already be locked.
 */
void
pmap_l3pt_delref(pmap_t pmap, vaddr_t va, pt_entry_t *l3pte, cpuid_t cpu_id)
{
	pt_entry_t *l1pte, *l2pte;
	PMAP_TLB_SHOOTDOWN_CPUSET_DECL

	l1pte = pmap_l1pte(pmap, va);
	l2pte = pmap_l2pte(pmap, va, l1pte);

#ifdef DIAGNOSTIC
	if (pmap == pmap_kernel())
		panic("pmap_l3pt_delref: kernel pmap");
#endif

	if (pmap_physpage_delref(l3pte) == 0) {
		/*
		 * No more mappings; we can free the level 3 table.
		 */
#ifdef DEBUG
		if (pmapdebug & PDB_PTPAGE)
			printf("pmap_l3pt_delref: freeing level 3 table at "
			    "0x%lx\n", pmap_pte_pa(l2pte));
#endif
		pmap_ptpage_free(pmap, l2pte);

		/*
		 * We've freed a level 3 table, so we must
		 * invalidate the TLB entry for that PT page
		 * in the Virtual Page Table VA range, because
		 * otherwise the PALcode will service a TLB
		 * miss using the stale VPT TLB entry it entered
		 * behind our back to shortcut to the VA's PTE.
		 */
		PMAP_INVALIDATE_TLB(pmap,
		    (vaddr_t)(&VPT[VPT_INDEX(va)]), FALSE,
		    PMAP_ISACTIVE(pmap, cpu_id), cpu_id);
		PMAP_TLB_SHOOTDOWN(pmap,
		    (vaddr_t)(&VPT[VPT_INDEX(va)]), 0);
		PMAP_TLB_SHOOTNOW();

		/*
		 * We've freed a level 3 table, so delete the reference
		 * on the level 2 table.
		 */
		pmap_l2pt_delref(pmap, l1pte, l2pte);
	}
}

/*
 * pmap_l2pt_delref:
 *
 *	Delete a reference on a level 2 PT page.  If the reference drops
 *	to zero, free it.
 *
 *	Note: the pmap must already be locked.
 */
void
pmap_l2pt_delref(pmap_t pmap, pt_entry_t *l1pte, pt_entry_t *l2pte)
{
	KASSERT(pmap != pmap_kernel());
	if (pmap_physpage_delref(l2pte) == 0) {
		/*
		 * No more mappings in this segment; we can free the
		 * level 2 table.
		 */
#ifdef DEBUG
		if (pmapdebug & PDB_PTPAGE)
			printf("pmap_l2pt_delref: freeing level 2 table at "
			    "0x%lx\n", pmap_pte_pa(l1pte));
#endif
		pmap_ptpage_free(pmap, l1pte);

		/*
		 * We've freed a level 2 table, so delete the reference
		 * on the level 1 table.
		 */
		pmap_l1pt_delref(pmap, l1pte);
	}
}

/*
 * pmap_l1pt_delref:
 *
 *	Delete a reference on a level 1 PT page.  If the reference drops
 *	to zero, free it.
 *
 *	Note: the pmap must already be locked.
 */
void
pmap_l1pt_delref(pmap_t pmap, pt_entry_t *l1pte)
{
	KASSERT(pmap != pmap_kernel());
	pmap_physpage_delref(l1pte);
}

/******************** Address Space Number management ********************/

/*
 * pmap_asn_alloc:
 *
 *	Allocate and assign an ASN to the specified pmap.
 *
 *	Note: the pmap must already be locked.  This may be called from
 *	an interprocessor interrupt, and in that case, the sender of
 *	the IPI has the pmap lock.
 */
void
pmap_asn_alloc(pmap_t pmap, cpuid_t cpu_id)
{
	struct pmap_asn_info *pma = &pmap->pm_asni[cpu_id];
	struct pmap_asn_info *cpma = &pmap_asn_info[cpu_id];

#ifdef DEBUG
	if (pmapdebug & (PDB_FOLLOW|PDB_ASN))
		printf("pmap_asn_alloc(%p)\n", pmap);
#endif

	/*
	 * If the pmap is still using the global kernel_lev1map, there
	 * is no need to assign an ASN at this time, because only
	 * kernel mappings exist in that map, and all kernel mappings
	 * have PG_ASM set.  If the pmap eventually gets its own
	 * lev1map, an ASN will be allocated at that time.
	 *
	 * Only the kernel pmap will reference kernel_lev1map.  Do the
	 * same old fixups, but note that we no longer need the pmap
	 * to be locked if we're in this mode, since pm_lev1map will
	 * never change.
	 */
	if (pmap->pm_lev1map == kernel_lev1map) {
#ifdef DEBUG
		if (pmapdebug & PDB_ASN)
			printf("pmap_asn_alloc: still references "
			    "kernel_lev1map\n");
#endif
#if defined(MULTIPROCESSOR)
		/*
		 * In a multiprocessor system, it's possible to
		 * get here without having PMAP_ASN_RESERVED in
		 * pmap->pm_asni[cpu_id].pma_asn; see pmap_lev1map_destroy().
		 *
		 * So, what we do here, is simply assign the reserved
		 * ASN for kernel_lev1map users and let things
		 * continue on.  We do, however, let uniprocessor
		 * configurations continue to make its assertion.
		 */
		pma->pma_asn = PMAP_ASN_RESERVED;
#else
		KASSERT(pma->pma_asn == PMAP_ASN_RESERVED);
#endif /* MULTIPROCESSOR */
		return;
	}

	/*
	 * On processors which do not implement ASNs, the swpctx PALcode
	 * operation will automatically invalidate the TLB and I-cache,
	 * so we don't need to do that here.
	 */
	if (pmap_max_asn == 0) {
		/*
		 * Refresh the pmap's generation number, to
		 * simplify logic elsewhere.
		 */
		pma->pma_asngen = cpma->pma_asngen;
#ifdef DEBUG
		if (pmapdebug & PDB_ASN)
			printf("pmap_asn_alloc: no ASNs, using asngen %lu\n",
			    pma->pma_asngen);
#endif
		return;
	}

	/*
	 * Hopefully, we can continue using the one we have...
	 */
	if (pma->pma_asn != PMAP_ASN_RESERVED &&
	    pma->pma_asngen == cpma->pma_asngen) {
		/*
		 * ASN is still in the current generation; keep on using it.
		 */
#ifdef DEBUG
		if (pmapdebug & PDB_ASN)
			printf("pmap_asn_alloc: same generation, keeping %u\n",
			    pma->pma_asn);
#endif
		return;
	}

	/*
	 * Need to assign a new ASN.  Grab the next one, incrementing
	 * the generation number if we have to.
	 */
	if (cpma->pma_asn > pmap_max_asn) {
		/*
		 * Invalidate all non-PG_ASM TLB entries and the
		 * I-cache, and bump the generation number.
		 */
		ALPHA_TBIAP();
		alpha_pal_imb();

		cpma->pma_asn = 1;
		cpma->pma_asngen++;
#ifdef DIAGNOSTIC
		if (cpma->pma_asngen == 0) {
			/*
			 * The generation number has wrapped.  We could
			 * handle this scenario by traversing all of
			 * the pmaps, and invalidating the generation
			 * number on those which are not currently
			 * in use by this processor.
			 *
			 * However... considering that we're using
			 * an unsigned 64-bit integer for generation
			 * numbers, on non-ASN CPUs, we won't wrap
			 * for approx. 585 million years, or 75 billion
			 * years on a 128-ASN CPU (assuming 1000 switch
			 * operations per second).
			 *
			 * So, we don't bother.
			 */
			panic("pmap_asn_alloc: too much uptime");
		}
#endif
#ifdef DEBUG
		if (pmapdebug & PDB_ASN)
			printf("pmap_asn_alloc: generation bumped to %lu\n",
			    cpma->pma_asngen);
#endif
	}

	/*
	 * Assign the new ASN and validate the generation number.
	 */
	pma->pma_asn = cpma->pma_asn++;
	pma->pma_asngen = cpma->pma_asngen;

#ifdef DEBUG
	if (pmapdebug & PDB_ASN)
		printf("pmap_asn_alloc: assigning %u to pmap %p\n",
		    pma->pma_asn, pmap);
#endif

	/*
	 * Have a new ASN, so there's no need to sync the I-stream
	 * on the way back out to userspace.
	 */
	atomic_clearbits_ulong(&pmap->pm_needisync, (1UL << cpu_id));
}

#if defined(MULTIPROCESSOR)
/******************** TLB shootdown code ********************/

/*
 * pmap_tlb_shootdown:
 *
 *	Cause the TLB entry for pmap/va to be shot down.
 *
 *	NOTE: The pmap must be locked here.
 */
void
pmap_tlb_shootdown(pmap_t pmap, vaddr_t va, pt_entry_t pte, u_long *cpumaskp)
{
	struct pmap_tlb_shootdown_q *pq;
	struct pmap_tlb_shootdown_job *pj;
	struct cpu_info *ci, *self = curcpu();
	u_long cpumask;
	CPU_INFO_ITERATOR cii;
#if 0
	int s;
#endif

	cpumask = 0;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci == self)
			continue;

		/*
		 * The pmap must be locked (unless its the kernel
		 * pmap, in which case it is okay for it to be
		 * unlocked), which prevents it from  becoming
		 * active on any additional processors.  This makes
		 * it safe to check for activeness.  If it's not
		 * active on the processor in question, then just
		 * mark it as needing a new ASN the next time it
		 * does, saving the IPI.  We always have to send
		 * the IPI for the kernel pmap.
		 *
		 * Note if it's marked active now, and it becomes
		 * inactive by the time the processor receives
		 * the IPI, that's okay, because it does the right
		 * thing with it later.
		 */
		if (pmap != pmap_kernel() &&
		    PMAP_ISACTIVE(pmap, ci->ci_cpuid) == 0) {
			PMAP_INVALIDATE_ASN(pmap, ci->ci_cpuid);
			continue;
		}

		cpumask |= 1UL << ci->ci_cpuid;

		pq = &pmap_tlb_shootdown_q[ci->ci_cpuid];

		PSJQ_LOCK(pq, s);

		pq->pq_pte |= pte;

		/*
		 * If a global flush is already pending, we
		 * don't really have to do anything else.
		 */
		if (pq->pq_tbia) {
			PSJQ_UNLOCK(pq, s);
			continue;
		}

		pj = pmap_tlb_shootdown_job_get(pq);
		if (pj == NULL) {
			/*
			 * Couldn't allocate a job entry.  Just
			 * tell the processor to kill everything.
			 */
			pq->pq_tbia = 1;
		} else {
			pj->pj_pmap = pmap;
			pj->pj_va = va;
			pj->pj_pte = pte;
			TAILQ_INSERT_TAIL(&pq->pq_head, pj, pj_list);
		}

		PSJQ_UNLOCK(pq, s);
	}

	*cpumaskp |= cpumask;
}

/*
 * pmap_tlb_shootnow:
 *
 *	Process the TLB shootdowns that we have been accumulating
 *	for the specified processor set.
 */
void
pmap_tlb_shootnow(u_long cpumask)
{

	alpha_multicast_ipi(cpumask, ALPHA_IPI_SHOOTDOWN);
}

/*
 * pmap_do_tlb_shootdown:
 *
 *	Process pending TLB shootdown operations for this processor.
 */
void
pmap_do_tlb_shootdown(struct cpu_info *ci, struct trapframe *framep)
{
	u_long cpu_id = ci->ci_cpuid;
	u_long cpu_mask = (1UL << cpu_id);
	struct pmap_tlb_shootdown_q *pq = &pmap_tlb_shootdown_q[cpu_id];
	struct pmap_tlb_shootdown_job *pj;
#if 0
	int s;
#endif

	PSJQ_LOCK(pq, s);

	if (pq->pq_tbia) {
		if (pq->pq_pte & PG_ASM)
			ALPHA_TBIA();
		else
			ALPHA_TBIAP();
		pq->pq_tbia = 0;
		pmap_tlb_shootdown_q_drain(pq);
	} else {
		while ((pj = TAILQ_FIRST(&pq->pq_head)) != NULL) {
			TAILQ_REMOVE(&pq->pq_head, pj, pj_list);
			PMAP_INVALIDATE_TLB(pj->pj_pmap, pj->pj_va,
			    pj->pj_pte & PG_ASM,
			    pj->pj_pmap->pm_cpus & cpu_mask, cpu_id);
			pmap_tlb_shootdown_job_put(pq, pj);
		}
	}
	pq->pq_pte = 0;

	PSJQ_UNLOCK(pq, s);
}

/*
 * pmap_tlb_shootdown_q_drain:
 *
 *	Drain a processor's TLB shootdown queue.  We do not perform
 *	the shootdown operations.  This is merely a convenience
 *	function.
 *
 *	Note: We expect the queue to be locked.
 */
void
pmap_tlb_shootdown_q_drain(struct pmap_tlb_shootdown_q *pq)
{
	struct pmap_tlb_shootdown_job *pj;

	while ((pj = TAILQ_FIRST(&pq->pq_head)) != NULL) {
		TAILQ_REMOVE(&pq->pq_head, pj, pj_list);
		pmap_tlb_shootdown_job_put(pq, pj);
	}
}

/*
 * pmap_tlb_shootdown_job_get:
 *
 *	Get a TLB shootdown job queue entry.  This places a limit on
 *	the number of outstanding jobs a processor may have.
 *
 *	Note: We expect the queue to be locked.
 */
struct pmap_tlb_shootdown_job *
pmap_tlb_shootdown_job_get(struct pmap_tlb_shootdown_q *pq)
{
	struct pmap_tlb_shootdown_job *pj;

	pj = TAILQ_FIRST(&pq->pq_free);
	if (pj != NULL)
		TAILQ_REMOVE(&pq->pq_free, pj, pj_list);
	return (pj);
}

/*
 * pmap_tlb_shootdown_job_put:
 *
 *	Put a TLB shootdown job queue entry onto the free list.
 *
 *	Note: We expect the queue to be locked.
 */
void
pmap_tlb_shootdown_job_put(struct pmap_tlb_shootdown_q *pq,
    struct pmap_tlb_shootdown_job *pj)
{
	TAILQ_INSERT_TAIL(&pq->pq_free, pj, pj_list);
}
#endif /* MULTIPROCESSOR */
