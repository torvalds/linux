/* $OpenBSD: pmap.h,v 1.49 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pmap.h,v 1.37 2000/11/19 03:16:35 thorpej Exp $ */

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
 * Copyright (c) 1987 Carnegie-Mellon University
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
 *	@(#)pmap.h	8.1 (Berkeley) 6/10/93
 */

#ifndef	_PMAP_MACHINE_
#define	_PMAP_MACHINE_

#include <sys/mutex.h>
#include <machine/pte.h>

#ifdef _KERNEL

#include <sys/queue.h>

/*
 * Machine-dependent virtual memory state.
 *
 * If we ever support processor numbers higher than 63, we'll have to
 * rethink the CPU mask.
 *
 * Note pm_asn and pm_asngen are arrays allocated in pmap_create().
 * Their size is based on the PCS count from the HWRPB, and indexed
 * by processor ID (from `whami').
 *
 * The kernel pmap is a special case; it gets statically-allocated
 * arrays which hold enough for ALPHA_MAXPROCS.
 */
struct pmap_asn_info {
	unsigned int		pma_asn;	/* address space number */
	unsigned long		pma_asngen;	/* ASN generation number */
};

struct pmap {
	TAILQ_ENTRY(pmap)	pm_list;	/* list of all pmaps */
	pt_entry_t		*pm_lev1map;	/* level 1 map */
	int			pm_count;	/* pmap reference count */
	struct mutex		pm_mtx;		/* lock on pmap */
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	unsigned long		pm_cpus;	/* mask of CPUs using pmap */
	unsigned long		pm_needisync;	/* mask of CPUs needing isync */
	struct pmap_asn_info	pm_asni[1];	/* ASN information */
			/*	variable length		*/
};
typedef struct pmap	*pmap_t;

/*
 * Compute the sizeof of a pmap structure.  Subtract one because one
 * ASN info structure is already included in the pmap structure itself.
 */
#define PMAP_SIZEOF(x)							\
	(ALIGN(sizeof(struct pmap) +					\
	       (sizeof(struct pmap_asn_info) * ((x) - 1))))

#define	PMAP_ASN_RESERVED	0	/* reserved for Lev1map users */

extern struct pmap	kernel_pmap_store[];

#define pmap_kernel()	kernel_pmap_store

/*
 * For each vm_page_t, there is a list of all currently valid virtual
 * mappings of that page.  An entry is a pv_entry_t, the list is pv_table.
 */
typedef struct pv_entry {
	struct pv_entry *pv_next;	/* next pv_entry on list */
	struct pmap	*pv_pmap;	/* pmap where mapping lies */
	vaddr_t		pv_va;		/* virtual address for mapping */
	pt_entry_t	*pv_pte;	/* PTE that maps the VA */
} *pv_entry_t;

/* pg_flags extra flags */
#define	PG_PMAP_MOD		PG_PMAP0	/* modified */
#define	PG_PMAP_REF		PG_PMAP1	/* referenced */

#if defined(MULTIPROCESSOR)
void	pmap_tlb_shootdown(pmap_t, vaddr_t, pt_entry_t, u_long *);
void	pmap_tlb_shootnow(u_long);
void	pmap_do_tlb_shootdown(struct cpu_info *, struct trapframe *);
#define	PMAP_TLB_SHOOTDOWN_CPUSET_DECL		u_long shootset = 0;
#define	PMAP_TLB_SHOOTDOWN(pm, va, pte)					\
	pmap_tlb_shootdown((pm), (va), (pte), &shootset)
#define	PMAP_TLB_SHOOTNOW()						\
	pmap_tlb_shootnow(shootset)
#else
#define	PMAP_TLB_SHOOTDOWN_CPUSET_DECL		/* nothing */
#define	PMAP_TLB_SHOOTDOWN(pm, va, pte)		/* nothing */
#define	PMAP_TLB_SHOOTNOW()			/* nothing */
#endif /* MULTIPROCESSOR */

#define	pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)

#define pmap_update(pmap)		/* nothing (yet) */

#define pmap_proc_iflush(p, va, len)	/* nothing */
#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)		/* nothing */
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

extern	pt_entry_t *VPT;		/* Virtual Page Table */

#define PMAP_CHECK_COPYIN	1

#define	PMAP_STEAL_MEMORY		/* enable pmap_steal_memory() */
#define PMAP_GROWKERNEL			/* enable pmap_growkernel() */

/*
 * Alternate mapping hooks for pool pages.  Avoids thrashing the TLB.
 */
#define	pmap_map_direct(pg)	ALPHA_PHYS_TO_K0SEG(VM_PAGE_TO_PHYS(pg))
#define	pmap_unmap_direct(va)	PHYS_TO_VM_PAGE(ALPHA_K0SEG_TO_PHYS((va)))
#define	__HAVE_PMAP_DIRECT

paddr_t vtophys(vaddr_t);

/* Machine-specific functions. */
void	pmap_bootstrap(paddr_t ptaddr, u_int maxasn, u_long ncpuids);
int	pmap_emulate_reference(struct proc *p, vaddr_t v, int user, int type);

#define	pmap_pte_pa(pte)	(PG_PFNUM(*(pte)) << PAGE_SHIFT)
#define	pmap_pte_prot(pte)	(*(pte) & PG_PROT)
#define	pmap_pte_w(pte)		(*(pte) & PG_WIRED)
#define	pmap_pte_v(pte)		(*(pte) & PG_V)
#define	pmap_pte_pv(pte)	(*(pte) & PG_PVLIST)
#define	pmap_pte_asm(pte)	(*(pte) & PG_ASM)
#define	pmap_pte_exec(pte)	(*(pte) & PG_EXEC)

#define	pmap_pte_set_w(pte, v)						\
do {									\
	if (v)								\
		*(pte) |= PG_WIRED;					\
	else								\
		*(pte) &= ~PG_WIRED;					\
} while (0)

#define	pmap_pte_w_chg(pte, nw)	((nw) ^ pmap_pte_w(pte))

#define	pmap_pte_set_prot(pte, np)					\
do {									\
	*(pte) &= ~PG_PROT;						\
	*(pte) |= (np);							\
} while (0)

#define	pmap_pte_prot_chg(pte, np) ((np) ^ pmap_pte_prot(pte))

static __inline pt_entry_t *pmap_l2pte(pmap_t, vaddr_t, pt_entry_t *);
static __inline pt_entry_t *pmap_l3pte(pmap_t, vaddr_t, pt_entry_t *);

#define	pmap_l1pte(pmap, v)						\
	(&(pmap)->pm_lev1map[l1pte_index((vaddr_t)(v))])

static __inline pt_entry_t *
pmap_l2pte(pmap_t pmap, vaddr_t v, pt_entry_t *l1pte)
{
	pt_entry_t *lev2map;

	if (l1pte == NULL) {
		l1pte = pmap_l1pte(pmap, v);
		if (pmap_pte_v(l1pte) == 0)
			return (NULL);
	}

	lev2map = (pt_entry_t *)ALPHA_PHYS_TO_K0SEG(pmap_pte_pa(l1pte));
	return (&lev2map[l2pte_index(v)]);
}

static __inline pt_entry_t *
pmap_l3pte(pmap_t pmap, vaddr_t v, pt_entry_t *l2pte)
{
	pt_entry_t *l1pte, *lev2map, *lev3map;

	if (l2pte == NULL) {
		l1pte = pmap_l1pte(pmap, v);
		if (pmap_pte_v(l1pte) == 0)
			return (NULL);

		lev2map = (pt_entry_t *)ALPHA_PHYS_TO_K0SEG(pmap_pte_pa(l1pte));
		l2pte = &lev2map[l2pte_index(v)];
		if (pmap_pte_v(l2pte) == 0)
			return (NULL);
	}

	lev3map = (pt_entry_t *)ALPHA_PHYS_TO_K0SEG(pmap_pte_pa(l2pte));
	return (&lev3map[l3pte_index(v)]);
}

/*
 * Macro for processing deferred I-stream synchronization.
 *
 * The pmap module may defer syncing the user I-stream until the
 * return to userspace, since the IMB PALcode op can be quite
 * expensive.  Since user instructions won't be executed until the
 * return to userspace, this can be deferred until just before userret().
 */
#define	PMAP_USERRET(pmap)						\
do {									\
	u_long cpu_mask = (1UL << cpu_number());			\
									\
	if ((pmap)->pm_needisync & cpu_mask) {				\
		atomic_clearbits_ulong(&(pmap)->pm_needisync,		\
		    cpu_mask);						\
		alpha_pal_imb();					\
	}								\
} while (0)

#endif /* _KERNEL */

/*
 * pmap-specific data stored in the vm_page structure.
 */
struct vm_page_md {
	struct mutex pvh_mtx;
	struct pv_entry *pvh_list;	/* pv entry list */
};

#define	VM_MDPAGE_INIT(pg)						\
do {									\
	mtx_init(&(pg)->mdpage.pvh_mtx, IPL_VM);			\
	(pg)->mdpage.pvh_list = NULL;					\
} while (0)

#endif /* _PMAP_MACHINE_ */
