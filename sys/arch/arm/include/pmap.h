/*	$OpenBSD: pmap.h,v 1.57 2024/11/07 08:12:12 miod Exp $	*/
/*	$NetBSD: pmap.h,v 1.76 2003/09/06 09:10:46 rearnsha Exp $	*/

/*
 * Copyright (c) 2002, 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe & Steve C. Woodford for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
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
 * Copyright (c) 1994,1995 Mark Brinicombe.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe
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
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_ARM_PMAP_H_
#define	_ARM_PMAP_H_

#ifdef _KERNEL

#include <arm/cpuconf.h>
#include <arm/pte.h>
#ifndef _LOCORE
#include <arm/cpufunc.h>
#endif

/*
 * a pmap describes a processes' 4GB virtual address space.  this
 * virtual address space can be broken up into 4096 1MB regions which
 * are described by L1 PTEs in the L1 table.
 *
 * There is a line drawn at KERNEL_BASE.  Everything below that line
 * changes when the VM context is switched.  Everything above that line
 * is the same no matter which VM context is running.  This is achieved
 * by making the L1 PTEs for those slots above KERNEL_BASE reference
 * kernel L2 tables.
 *
 * The basic layout of the virtual address space thus looks like this:
 *
 *	0xffffffff
 *	.
 *	.
 *	.
 *	KERNEL_BASE
 *	--------------------
 *	.
 *	.
 *	.
 *	0x00000000
 */

/*
 * The number of L2 descriptor tables which can be tracked by an l2_dtable.
 * A bucket size of 16 provides for 16MB of contiguous virtual address
 * space per l2_dtable. Most processes will, therefore, require only two or
 * three of these to map their whole working set.
 */
#define	L2_BUCKET_LOG2	4
#define	L2_BUCKET_SIZE	(1 << L2_BUCKET_LOG2)

/*
 * Given the above "L2-descriptors-per-l2_dtable" constant, the number
 * of l2_dtable structures required to track all possible page descriptors
 * mappable by an L1 translation table is given by the following constants:
 */
#define	L2_LOG2		((32 - L1_S_SHIFT) - L2_BUCKET_LOG2)
#define	L2_SIZE		(1 << L2_LOG2)

#ifndef _LOCORE

struct l1_ttable;
struct l2_dtable;

/*
 * Track cache/tlb occupancy using the following structure
 */
union pmap_cache_state {
	struct {
		union {
			u_int8_t csu_cache_b[2];
			u_int16_t csu_cache;
		} cs_cache_u;

		union {
			u_int8_t csu_tlb_b[2];
			u_int16_t csu_tlb;
		} cs_tlb_u;
	} cs_s;
	u_int32_t cs_all;
};
#define	cs_cache_id	cs_s.cs_cache_u.csu_cache_b[0]
#define	cs_cache_d	cs_s.cs_cache_u.csu_cache_b[1]
#define	cs_cache	cs_s.cs_cache_u.csu_cache
#define	cs_tlb_id	cs_s.cs_tlb_u.csu_tlb_b[0]
#define	cs_tlb_d	cs_s.cs_tlb_u.csu_tlb_b[1]
#define	cs_tlb		cs_s.cs_tlb_u.csu_tlb

/*
 * Assigned to cs_all to force cacheops to work for a particular pmap
 */
#define	PMAP_CACHE_STATE_ALL	0xffffffffu

/*
 * The pmap structure itself
 */
struct pmap {
	u_int8_t		pm_domain;
	int			pm_remove_all;
	struct l1_ttable	*pm_l1;
	union pmap_cache_state	pm_cstate;
	u_int			pm_refs;
	struct l2_dtable	*pm_l2[L2_SIZE];
	struct pmap_statistics	pm_stats;
};

typedef struct pmap *pmap_t;

/*
 * MD flags that we use for pmap_enter (in the pa):
 */
#define PMAP_PA_MASK	~((paddr_t)PAGE_MASK) /* to remove the flags */
#define PMAP_NOCACHE	0x1 /* non-cacheable memory. */
#define PMAP_DEVICE	0x2 /* device memory. */

/*
 * Physical / virtual address structure. In a number of places (particularly
 * during bootstrapping) we need to keep track of the physical and virtual
 * addresses of various pages
 */
typedef struct pv_addr {
	SLIST_ENTRY(pv_addr) pv_list;
	paddr_t pv_pa;
	vaddr_t pv_va;
} pv_addr_t;

/*
 * Determine various modes for PTEs (user vs. kernel, cacheable
 * vs. non-cacheable).
 */
#define	PTE_KERNEL	0
#define	PTE_USER	1
#define	PTE_NOCACHE	0
#define	PTE_CACHE	1
#define	PTE_PAGETABLE	2

/*
 * Flags that indicate attributes of pages or mappings of pages.
 *
 * The PVF_MOD and PVF_REF flags are stored in the mdpage for each
 * page.  PVF_WIRED and PVF_WRITE are kept in individual pv_entry's
 * for each page.  They live in the same "namespace" so that we can
 * clear multiple attributes at a time.
 */
#define	PVF_MOD		0x01		/* page is modified */
#define	PVF_REF		0x02		/* page is referenced */
#define	PVF_WIRED	0x04		/* mapping is wired */
#define	PVF_WRITE	0x08		/* mapping is writable */
#define	PVF_EXEC	0x10		/* mapping is executable */

/*
 * Commonly referenced structures
 */
extern struct pmap	kernel_pmap_store;

/*
 * Macros that we need to export
 */
#define pmap_kernel()			(&kernel_pmap_store)
#define	pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)

#define	pmap_is_modified(pg)	\
	(((pg)->mdpage.pvh_attrs & PVF_MOD) != 0)
#define	pmap_is_referenced(pg)	\
	(((pg)->mdpage.pvh_attrs & PVF_REF) != 0)

#define	pmap_deactivate(p)		do { /* nothing */ } while (0)

#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)		do { /* nothing */ } while (0)
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

#define PMAP_CHECK_COPYIN	1

#define PMAP_GROWKERNEL		/* turn on pmap_growkernel interface */

/* Functions we use internally. */
void	pmap_bootstrap(pd_entry_t *, vaddr_t, vaddr_t);

int pmap_get_pde_pte(pmap_t, vaddr_t, pd_entry_t **, pt_entry_t **);
int pmap_get_pde(pmap_t, vaddr_t, pd_entry_t **);
void	pmap_set_pcb_pagedir(pmap_t, struct pcb *);

void	pmap_postinit(void);

void	vector_page_setprot(int);

/* XXX */
void pmap_kenter_cache(vaddr_t va, paddr_t pa, vm_prot_t prot, int cacheable);

/* Bootstrapping routines. */
void	pmap_map_section(vaddr_t, vaddr_t, paddr_t, int, int);
void	pmap_map_entry(vaddr_t, vaddr_t, paddr_t, int, int);
vsize_t	pmap_map_chunk(vaddr_t, vaddr_t, paddr_t, vsize_t, int, int);
void	pmap_link_l2pt(vaddr_t, vaddr_t, pv_addr_t *);

/*
 * The current top of kernel VM
 */
extern vaddr_t	pmap_curmaxkvaddr;

/*
 * Useful macros and constants 
 */

/* Virtual address to page table entry */
static __inline pt_entry_t *
vtopte(vaddr_t va)
{
	pd_entry_t *pdep;
	pt_entry_t *ptep;

	if (pmap_get_pde_pte(pmap_kernel(), va, &pdep, &ptep) == FALSE)
		return (NULL);
	return (ptep);
}

/*
 * Page tables are always mapped write-through.
 * Thus, on some platforms we can run fast and loose and avoid syncing PTEs
 * on every change.
 *
 * Unfortunately, not all CPUs have a write-through cache mode.  So we
 * define PMAP_NEEDS_PTE_SYNC for C code to conditionally do PTE syncs.
 */
extern int pmap_needs_pte_sync;

#define	PMAP_NEEDS_PTE_SYNC	pmap_needs_pte_sync

#define	PTE_SYNC(pte)							\
do {									\
	cpu_drain_writebuf();						\
	if (PMAP_NEEDS_PTE_SYNC) {					\
		paddr_t pa;						\
		cpu_dcache_wb_range((vaddr_t)(pte), sizeof(pt_entry_t));\
		if (cpu_sdcache_enabled()) { 				\
		(void)pmap_extract(pmap_kernel(), (vaddr_t)(pte), &pa);	\
		cpu_sdcache_wb_range((vaddr_t)(pte), (paddr_t)(pa),	\
		    sizeof(pt_entry_t));				\
		};							\
		cpu_drain_writebuf();					\
	}								\
} while (/*CONSTCOND*/0)

#define	PTE_SYNC_RANGE(pte, cnt)					\
do {									\
	cpu_drain_writebuf();						\
	if (PMAP_NEEDS_PTE_SYNC) {					\
		paddr_t pa;						\
		cpu_dcache_wb_range((vaddr_t)(pte),			\
		    (cnt) << 2); /* * sizeof(pt_entry_t) */		\
		if (cpu_sdcache_enabled()) { 				\
		(void)pmap_extract(pmap_kernel(), (vaddr_t)(pte), &pa);\
		cpu_sdcache_wb_range((vaddr_t)(pte), (paddr_t)(pa),	\
		    (cnt) << 2); /* * sizeof(pt_entry_t) */		\
		};							\
		cpu_drain_writebuf();					\
	}								\
} while (/*CONSTCOND*/0)

#define	l1pte_valid(pde)	(((pde) & L1_TYPE_MASK) != L1_TYPE_INV)
#define	l1pte_section_p(pde)	(((pde) & L1_TYPE_MASK) == L1_TYPE_S)
#define	l1pte_page_p(pde)	(((pde) & L1_TYPE_MASK) == L1_TYPE_C)
#define	l1pte_fpage_p(pde)	(((pde) & L1_TYPE_MASK) == L1_TYPE_F)

#define l2pte_index(v)		(((v) & L2_ADDR_BITS) >> L2_S_SHIFT)
#define	l2pte_valid(pte)	(((pte) & L2_TYPE_MASK) != L2_TYPE_INV)
#define	l2pte_pa(pte)		((pte) & L2_S_FRAME)

/* L1 and L2 page table macros */
#define pmap_pde_v(pde)		l1pte_valid(*(pde))
#define pmap_pde_section(pde)	l1pte_section_p(*(pde))
#define pmap_pde_page(pde)	l1pte_page_p(*(pde))
#define pmap_pde_fpage(pde)	l1pte_fpage_p(*(pde))

/************************* ARM MMU configuration *****************************/

void	pmap_pte_init_armv7(void);

#endif /* !_LOCORE */

/*****************************************************************************/

/*
 * Definitions for MMU domains
 */
#define	PMAP_DOMAINS		15	/* 15 'user' domains (0-14) */
#define	PMAP_DOMAIN_KERNEL	15	/* The kernel uses domain #15 */

/*
 * These macros define the various bit masks in the PTE.
 *
 * We use these macros since we use different bits on different processor
 * models.
 */
#define	L1_S_PROT_UR_v7		(L1_S_V7_AP(AP_V7_KRUR))
#define	L1_S_PROT_UW_v7		(L1_S_V7_AP(AP_KRWURW))
#define	L1_S_PROT_KR_v7		(L1_S_V7_AP(AP_V7_KR))
#define	L1_S_PROT_KW_v7		(L1_S_V7_AP(AP_KRW))
#define	L1_S_PROT_MASK_v7	(L1_S_V7_AP(0x07))

#define	L1_S_CACHE_MASK_v7	(L1_S_B|L1_S_C|L1_S_V7_TEX_MASK)

#define	L1_S_COHERENT_v7	(L1_S_C)

#define	L2_L_PROT_UR_v7		(L2_V7_AP(AP_V7_KRUR))
#define	L2_L_PROT_UW_v7		(L2_V7_AP(AP_KRWURW))
#define	L2_L_PROT_KR_v7		(L2_V7_AP(AP_V7_KR))
#define	L2_L_PROT_KW_v7		(L2_V7_AP(AP_KRW))
#define	L2_L_PROT_MASK_v7	(L2_V7_AP(0x07) | L2_V7_L_XN)

#define	L2_L_CACHE_MASK_v7	(L2_B|L2_C|L2_V7_L_TEX_MASK)

#define	L2_L_COHERENT_v7	(L2_C)

#define	L2_S_PROT_UR_v7		(L2_V7_AP(AP_V7_KRUR))
#define	L2_S_PROT_UW_v7		(L2_V7_AP(AP_KRWURW))
#define	L2_S_PROT_KR_v7		(L2_V7_AP(AP_V7_KR))
#define	L2_S_PROT_KW_v7		(L2_V7_AP(AP_KRW))
#define	L2_S_PROT_MASK_v7	(L2_V7_AP(0x07) | L2_V7_S_XN)

#define	L2_S_CACHE_MASK_v7	(L2_B|L2_C|L2_V7_S_TEX_MASK)

#define	L2_S_COHERENT_v7	(L2_C)

#define	L1_S_PROTO_v7		(L1_TYPE_S)

#define	L1_C_PROTO_v7		(L1_TYPE_C)

#define	L2_L_PROTO		(L2_TYPE_L)

#define	L2_S_PROTO_v7		(L2_TYPE_S)

#define	L1_S_PROT_UR		L1_S_PROT_UR_v7
#define	L1_S_PROT_UW		L1_S_PROT_UW_v7
#define	L1_S_PROT_KR		L1_S_PROT_KR_v7
#define	L1_S_PROT_KW		L1_S_PROT_KW_v7
#define	L1_S_PROT_MASK		L1_S_PROT_MASK_v7

#define	L2_L_PROT_UR		L2_L_PROT_UR_v7
#define	L2_L_PROT_UW		L2_L_PROT_UW_v7
#define	L2_L_PROT_KR		L2_L_PROT_KR_v7
#define	L2_L_PROT_KW		L2_L_PROT_KW_v7
#define	L2_L_PROT_MASK		L2_L_PROT_MASK_v7

#define	L2_S_PROT_UR		L2_S_PROT_UR_v7
#define	L2_S_PROT_UW		L2_S_PROT_UW_v7
#define	L2_S_PROT_KR		L2_S_PROT_KR_v7
#define	L2_S_PROT_KW		L2_S_PROT_KW_v7
#define	L2_S_PROT_MASK		L2_S_PROT_MASK_v7

#define	L1_S_CACHE_MASK		L1_S_CACHE_MASK_v7
#define	L2_L_CACHE_MASK		L2_L_CACHE_MASK_v7
#define	L2_S_CACHE_MASK		L2_S_CACHE_MASK_v7

#define	L1_S_COHERENT		L1_S_COHERENT_v7
#define	L2_L_COHERENT		L2_L_COHERENT_v7
#define	L2_S_COHERENT		L2_S_COHERENT_v7

#define	L1_S_PROTO		L1_S_PROTO_v7
#define	L1_C_PROTO		L1_C_PROTO_v7
#define	L2_S_PROTO		L2_S_PROTO_v7

/*
 * These macros return various bits based on kernel/user and protection.
 * Note that the compiler will usually fold these at compile time.
 */
#ifndef _LOCORE
static __inline pt_entry_t
L1_S_PROT(int ku, vm_prot_t pr)
{
	pt_entry_t pte;

	if (ku == PTE_USER)
		pte = (pr & PROT_WRITE) ? L1_S_PROT_UW : L1_S_PROT_UR;
	else
		pte = (pr & PROT_WRITE) ? L1_S_PROT_KW : L1_S_PROT_KR;

	if ((pr & PROT_EXEC) == 0)
		pte |= L1_S_V7_XN;

	return pte;
}
static __inline pt_entry_t
L2_L_PROT(int ku, vm_prot_t pr)
{
	pt_entry_t pte;

	if (ku == PTE_USER)
		pte = (pr & PROT_WRITE) ? L2_L_PROT_UW : L2_L_PROT_UR;
	else
		pte = (pr & PROT_WRITE) ? L2_L_PROT_KW : L2_L_PROT_KR;

	if ((pr & PROT_EXEC) == 0)
		pte |= L2_V7_L_XN;

	return pte;
}
static __inline pt_entry_t
L2_S_PROT(int ku, vm_prot_t pr)
{
	pt_entry_t pte;

	if (ku == PTE_USER)
		pte = (pr & PROT_WRITE) ? L2_S_PROT_UW : L2_S_PROT_UR;
	else
		pte = (pr & PROT_WRITE) ? L2_S_PROT_KW : L2_S_PROT_KR;

	if ((pr & PROT_EXEC) == 0)
		pte |= L2_V7_S_XN;

	return pte;
}

static __inline int
l2pte_is_writeable(pt_entry_t pte, struct pmap *pm)
{
	return (pte & L2_V7_AP(0x4)) == 0;
}
#endif

/*
 * Macros to test if a mapping is mappable with an L1 Section mapping
 * or an L2 Large Page mapping.
 */
#define	L1_S_MAPPABLE_P(va, pa, size)					\
	((((va) | (pa)) & L1_S_OFFSET) == 0 && (size) >= L1_S_SIZE)

#define	L2_L_MAPPABLE_P(va, pa, size)					\
	((((va) | (pa)) & L2_L_OFFSET) == 0 && (size) >= L2_L_SIZE)

#endif /* _KERNEL */

#ifndef _LOCORE
/*
 * pmap-specific data store in the vm_page structure.
 */
struct vm_page_md {
	struct pv_entry *pvh_list;		/* pv_entry list */
	int pvh_attrs;				/* page attributes */
};

#define	VM_MDPAGE_INIT(pg)						\
do {									\
	(pg)->mdpage.pvh_list = NULL;					\
	(pg)->mdpage.pvh_attrs = 0;					\
} while (/*CONSTCOND*/0)
#endif /* _LOCORE */

#endif	/* _ARM_PMAP_H_ */
