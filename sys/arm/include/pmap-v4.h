/*-
 * Copyright (c) 1991 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 *      from: hp300: @(#)pmap.h 7.2 (Berkeley) 12/16/90
 *      from: @(#)pmap.h        7.4 (Berkeley) 5/12/91
 * 	from: FreeBSD: src/sys/i386/include/pmap.h,v 1.70 2000/11/30
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PMAP_V4_H_
#define _MACHINE_PMAP_V4_H_

#include <machine/pte-v4.h>

/*
 * Pte related macros
 */
#define PTE_NOCACHE	1
#define PTE_CACHE	2
#define PTE_DEVICE	PTE_NOCACHE
#define PTE_PAGETABLE	3

enum mem_type {
	STRONG_ORD = 0,
	DEVICE_NOSHARE,
	DEVICE_SHARE,
	NRML_NOCACHE,
	NRML_IWT_OWT,
	NRML_IWB_OWB,
	NRML_IWBA_OWBA
};

#ifndef LOCORE

#include <sys/queue.h>
#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>

#define PDESIZE		sizeof(pd_entry_t)	/* for assembly files */
#define PTESIZE		sizeof(pt_entry_t)	/* for assembly files */

#define	pmap_page_get_memattr(m)	((m)->md.pv_memattr)
#define	pmap_page_is_mapped(m)	(!TAILQ_EMPTY(&(m)->md.pv_list))

/*
 * Pmap stuff
 */

/*
 * This structure is used to hold a virtual<->physical address
 * association and is used mostly by bootstrap code
 */
struct pv_addr {
	SLIST_ENTRY(pv_addr) pv_list;
	vm_offset_t	pv_va;
	vm_paddr_t	pv_pa;
};

struct	pv_entry;
struct	pv_chunk;

struct	md_page {
	int pvh_attrs;
	vm_memattr_t	 pv_memattr;
	vm_offset_t pv_kva;		/* first kernel VA mapping */
	TAILQ_HEAD(,pv_entry)	pv_list;
};

struct l1_ttable;
struct l2_dtable;


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

struct	pmap {
	struct mtx		pm_mtx;
	u_int8_t		pm_domain;
	struct l1_ttable	*pm_l1;
	struct l2_dtable	*pm_l2[L2_SIZE];
	cpuset_t		pm_active;	/* active on cpus */
	struct pmap_statistics	pm_stats;	/* pmap statictics */
	TAILQ_HEAD(,pv_entry)	pm_pvlist;	/* list of mappings in pmap */
};

typedef struct pmap *pmap_t;

#ifdef _KERNEL
extern struct pmap	kernel_pmap_store;
#define kernel_pmap	(&kernel_pmap_store)

#define	PMAP_ASSERT_LOCKED(pmap) \
				mtx_assert(&(pmap)->pm_mtx, MA_OWNED)
#define	PMAP_LOCK(pmap)		mtx_lock(&(pmap)->pm_mtx)
#define	PMAP_LOCK_DESTROY(pmap)	mtx_destroy(&(pmap)->pm_mtx)
#define	PMAP_LOCK_INIT(pmap)	mtx_init(&(pmap)->pm_mtx, "pmap", \
				    NULL, MTX_DEF | MTX_DUPOK)
#define	PMAP_OWNED(pmap)	mtx_owned(&(pmap)->pm_mtx)
#define	PMAP_MTX(pmap)		(&(pmap)->pm_mtx)
#define	PMAP_TRYLOCK(pmap)	mtx_trylock(&(pmap)->pm_mtx)
#define	PMAP_UNLOCK(pmap)	mtx_unlock(&(pmap)->pm_mtx)
#endif

/*
 * For each vm_page_t, there is a list of all currently valid virtual
 * mappings of that page.  An entry is a pv_entry_t, the list is pv_list.
 */
typedef struct pv_entry {
	vm_offset_t     pv_va;          /* virtual address for mapping */
	TAILQ_ENTRY(pv_entry)   pv_list;
	int		pv_flags;	/* flags (wired, etc...) */
	pmap_t          pv_pmap;        /* pmap where mapping lies */
	TAILQ_ENTRY(pv_entry)	pv_plist;
} *pv_entry_t;

/*
 * pv_entries are allocated in chunks per-process.  This avoids the
 * need to track per-pmap assignments.
 */
#define	_NPCM	8
#define	_NPCPV	252

struct pv_chunk {
	pmap_t			pc_pmap;
	TAILQ_ENTRY(pv_chunk)	pc_list;
	uint32_t		pc_map[_NPCM];	/* bitmap; 1 = free */
	uint32_t		pc_dummy[3];	/* aligns pv_chunk to 4KB */
	TAILQ_ENTRY(pv_chunk)	pc_lru;
	struct pv_entry		pc_pventry[_NPCPV];
};

#ifdef _KERNEL

boolean_t pmap_get_pde_pte(pmap_t, vm_offset_t, pd_entry_t **, pt_entry_t **);

/*
 * virtual address to page table entry and
 * to physical address. Likewise for alternate address space.
 * Note: these work recursively, thus vtopte of a pte will give
 * the corresponding pde that in turn maps it.
 */

/*
 * The current top of kernel VM.
 */
extern vm_offset_t pmap_curmaxkvaddr;

/* Virtual address to page table entry */
static __inline pt_entry_t *
vtopte(vm_offset_t va)
{
	pd_entry_t *pdep;
	pt_entry_t *ptep;

	if (pmap_get_pde_pte(kernel_pmap, va, &pdep, &ptep) == FALSE)
		return (NULL);
	return (ptep);
}

void	pmap_bootstrap(vm_offset_t firstaddr, struct pv_addr *l1pt);
int	pmap_change_attr(vm_offset_t, vm_size_t, int);
void	pmap_kenter(vm_offset_t va, vm_paddr_t pa);
void	pmap_kenter_nocache(vm_offset_t va, vm_paddr_t pa);
void 	pmap_kenter_user(vm_offset_t va, vm_paddr_t pa);
vm_paddr_t pmap_dump_kextract(vm_offset_t, pt2_entry_t *);
void	pmap_kremove(vm_offset_t);
vm_page_t	pmap_use_pt(pmap_t, vm_offset_t);
void	pmap_debug(int);
void	pmap_map_section(vm_offset_t, vm_offset_t, vm_offset_t, int, int);
void	pmap_link_l2pt(vm_offset_t, vm_offset_t, struct pv_addr *);
vm_size_t	pmap_map_chunk(vm_offset_t, vm_offset_t, vm_offset_t, vm_size_t, int, int);
void
pmap_map_entry(vm_offset_t l1pt, vm_offset_t va, vm_offset_t pa, int prot,
    int cache);
int pmap_fault_fixup(pmap_t, vm_offset_t, vm_prot_t, int);

/*
 * Definitions for MMU domains
 */
#define	PMAP_DOMAINS		15	/* 15 'user' domains (1-15) */
#define	PMAP_DOMAIN_KERNEL	0	/* The kernel uses domain #0 */

/*
 * The new pmap ensures that page-tables are always mapping Write-Thru.
 * Thus, on some platforms we can run fast and loose and avoid syncing PTEs
 * on every change.
 *
 * Unfortunately, not all CPUs have a write-through cache mode.  So we
 * define PMAP_NEEDS_PTE_SYNC for C code to conditionally do PTE syncs,
 * and if there is the chance for PTE syncs to be needed, we define
 * PMAP_INCLUDE_PTE_SYNC so e.g. assembly code can include (and run)
 * the code.
 */
extern int pmap_needs_pte_sync;

/*
 * These macros define the various bit masks in the PTE.
 */

#define	L1_S_CACHE_MASK		(L1_S_B|L1_S_C)
#define	L2_L_CACHE_MASK		(L2_B|L2_C)
#define	L2_S_PROT_U		(L2_AP(AP_U))
#define	L2_S_PROT_W		(L2_AP(AP_W))
#define	L2_S_PROT_MASK		(L2_S_PROT_U|L2_S_PROT_W)
#define	L2_S_CACHE_MASK		(L2_B|L2_C)
#define	L1_S_PROTO		(L1_TYPE_S | L1_S_IMP)
#define	L1_C_PROTO		(L1_TYPE_C | L1_C_IMP2)
#define	L2_L_PROTO		(L2_TYPE_L)
#define	L2_S_PROTO		(L2_TYPE_S)

/*
 * User-visible names for the ones that vary with MMU class.
 */
#define	L2_AP(x)	(L2_AP0(x) | L2_AP1(x) | L2_AP2(x) | L2_AP3(x))

#if defined(CPU_XSCALE_81342)
#define CPU_XSCALE_CORE3
#define PMAP_NEEDS_PTE_SYNC	1
#define PMAP_INCLUDE_PTE_SYNC
#else
#define	PMAP_NEEDS_PTE_SYNC	0
#endif

/*
 * These macros return various bits based on kernel/user and protection.
 * Note that the compiler will usually fold these at compile time.
 */
#define	L1_S_PROT_U		(L1_S_AP(AP_U))
#define	L1_S_PROT_W		(L1_S_AP(AP_W))
#define	L1_S_PROT_MASK		(L1_S_PROT_U|L1_S_PROT_W)
#define	L1_S_WRITABLE(pd)	((pd) & L1_S_PROT_W)

#define	L1_S_PROT(ku, pr)	((((ku) == PTE_USER) ? L1_S_PROT_U : 0) | \
				 (((pr) & VM_PROT_WRITE) ? L1_S_PROT_W : 0))

#define	L2_L_PROT_U		(L2_AP(AP_U))
#define	L2_L_PROT_W		(L2_AP(AP_W))
#define	L2_L_PROT_MASK		(L2_L_PROT_U|L2_L_PROT_W)

#define	L2_L_PROT(ku, pr)	((((ku) == PTE_USER) ? L2_L_PROT_U : 0) | \
				 (((pr) & VM_PROT_WRITE) ? L2_L_PROT_W : 0))

#define	L2_S_PROT(ku, pr)	((((ku) == PTE_USER) ? L2_S_PROT_U : 0) | \
				 (((pr) & VM_PROT_WRITE) ? L2_S_PROT_W : 0))

/*
 * Macros to test if a mapping is mappable with an L1 Section mapping
 * or an L2 Large Page mapping.
 */
#define	L1_S_MAPPABLE_P(va, pa, size)					\
	((((va) | (pa)) & L1_S_OFFSET) == 0 && (size) >= L1_S_SIZE)

#define	L2_L_MAPPABLE_P(va, pa, size)					\
	((((va) | (pa)) & L2_L_OFFSET) == 0 && (size) >= L2_L_SIZE)

/*
 * Provide a fallback in case we were not able to determine it at
 * compile-time.
 */
#ifndef PMAP_NEEDS_PTE_SYNC
#define	PMAP_NEEDS_PTE_SYNC	pmap_needs_pte_sync
#define	PMAP_INCLUDE_PTE_SYNC
#endif

#ifdef ARM_L2_PIPT
#define _sync_l2(pte, size) 	cpu_l2cache_wb_range(vtophys(pte), size)
#else
#define _sync_l2(pte, size) 	cpu_l2cache_wb_range(pte, size)
#endif

#define	PTE_SYNC(pte)							\
do {									\
	if (PMAP_NEEDS_PTE_SYNC) {					\
		cpu_dcache_wb_range((vm_offset_t)(pte), sizeof(pt_entry_t));\
		cpu_drain_writebuf();					\
		_sync_l2((vm_offset_t)(pte), sizeof(pt_entry_t));\
	} else								\
		cpu_drain_writebuf();					\
} while (/*CONSTCOND*/0)

#define	PTE_SYNC_RANGE(pte, cnt)					\
do {									\
	if (PMAP_NEEDS_PTE_SYNC) {					\
		cpu_dcache_wb_range((vm_offset_t)(pte),			\
		    (cnt) << 2); /* * sizeof(pt_entry_t) */		\
		cpu_drain_writebuf();					\
		_sync_l2((vm_offset_t)(pte),		 		\
		    (cnt) << 2); /* * sizeof(pt_entry_t) */		\
	} else								\
		cpu_drain_writebuf();					\
} while (/*CONSTCOND*/0)

void	pmap_pte_init_generic(void);

#define PTE_KERNEL	0
#define PTE_USER	1

/*
 * Flags that indicate attributes of pages or mappings of pages.
 *
 * The PVF_MOD and PVF_REF flags are stored in the mdpage for each
 * page.  PVF_WIRED, PVF_WRITE, and PVF_NC are kept in individual
 * pv_entry's for each page.  They live in the same "namespace" so
 * that we can clear multiple attributes at a time.
 *
 * Note the "non-cacheable" flag generally means the page has
 * multiple mappings in a given address space.
 */
#define	PVF_MOD		0x01		/* page is modified */
#define	PVF_REF		0x02		/* page is referenced */
#define	PVF_WIRED	0x04		/* mapping is wired */
#define	PVF_WRITE	0x08		/* mapping is writable */
#define	PVF_EXEC	0x10		/* mapping is executable */
#define	PVF_NC		0x20		/* mapping is non-cacheable */
#define	PVF_MWC		0x40		/* mapping is used multiple times in userland */
#define	PVF_UNMAN	0x80		/* mapping is unmanaged */

void vector_page_setprot(int);

#define SECTION_CACHE	0x1
#define SECTION_PT	0x2
void	pmap_postinit(void);

#endif	/* _KERNEL */

#endif	/* !LOCORE */

#endif	/* !_MACHINE_PMAP_V4_H_ */
