/*      $OpenBSD: pmap.h,v 1.55 2025/06/02 18:49:04 claudio Exp $ */

/*
 * Copyright (c) 1987 Carnegie-Mellon University
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *	from: @(#)pmap.h	8.1 (Berkeley) 6/10/93
 */

#ifndef	_MIPS64_PMAP_H_
#define	_MIPS64_PMAP_H_

#include <sys/mutex.h>

#ifdef	_KERNEL

#include <machine/pte.h>

/*
 * The user address space is currently limited to 1TB (0x0 - 0x10000000000).
 *
 * The user address space is mapped using a two level structure where
 * the virtual addresses bits are split in three groups:
 *   segment:directory:page:offset
 * where:
 * - offset are the in-page offsets (PAGE_SHIFT bits)
 * - page are the third level page table index
 *   (PMAP_PGSHIFT - Log2(pt_entry_t) bits)
 * - directory are the second level page table (directory) index
 *   (PMAP_PGSHIFT - Log2(void *) bits)
 * - segment are the first level page table (segment) index
 *   (PMAP_PGSHIFT - Log2(void *) bits)
 *
 * This scheme allows Segment, directory and page tables have the same size
 * (1 << PMAP_PGSHIFT bytes, regardless of the pt_entry_t size) to be able to
 * share the same allocator.
 *
 * Note: The kernel doesn't use the same data structures as user programs.
 * All the PTE entries are stored in a single array in Sysmap which is
 * dynamically allocated at boot time.
 */

#if defined(MIPS_PTE64) && PAGE_SHIFT == 12
#error "Cannot use MIPS_PTE64 with 4KB pages."
#endif

/*
 * Size of page table structs (page tables, page directories,
 * and segment table) used by this pmap.
 */

#define	PMAP_PGSHIFT		12
#define	PMAP_PGSIZE		(1UL << PMAP_PGSHIFT)

#define	NPDEPG			(PMAP_PGSIZE / sizeof(void *))
#define	NPTEPG			(PMAP_PGSIZE / sizeof(pt_entry_t))

/*
 * Segment sizes
 */

#define	SEGSHIFT		(PAGE_SHIFT+PMAP_PGSHIFT*2-PTE_LOG-3)
#define	DIRSHIFT		(PAGE_SHIFT+PMAP_PGSHIFT-PTE_LOG)
#define	NBSEG			(1UL << SEGSHIFT)
#define	NBDIR			(1UL << DIRSHIFT)
#define	SEGOFSET		(NBSEG - 1)
#define	DIROFSET		(NBDIR - 1)

#define	mips_trunc_seg(x)	((vaddr_t)(x) & ~SEGOFSET)
#define	mips_trunc_dir(x)	((vaddr_t)(x) & ~DIROFSET)
#define	mips_round_seg(x)	(((vaddr_t)(x) + SEGOFSET) & ~SEGOFSET)
#define	mips_round_dir(x)	(((vaddr_t)(x) + DIROFSET) & ~DIROFSET)
#define	pmap_segmap(m, v)	((m)->pm_segtab->seg_tab[((v) >> SEGSHIFT)])

/* number of segments entries */
#define	PMAP_SEGTABSIZE		(PMAP_PGSIZE / sizeof(void *))

struct segtab {
	pt_entry_t	**seg_tab[PMAP_SEGTABSIZE];
};

struct pmap_asid_info {
	u_int			pma_asid;	/* address space tag */
	u_int			pma_asidgen;	/* TLB PID generation number */
};

/*
 * Machine dependent pmap structure.
 */
typedef struct pmap {
	struct mutex		pm_mtx;		/* pmap lock */
	struct mutex		pm_swmtx;	/* pmap switch lock */
	int			pm_count;	/* pmap reference count */
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct segtab		*pm_segtab;	/* pointers to pages of PTEs */
	struct pmap_asid_info	pm_asid[1];	/* ASID information */
} *pmap_t;

/*
 * Compute the sizeof of a pmap structure.  Subtract one because one
 * ASID info structure is already included in the pmap structure itself.
 */
#define	PMAP_SIZEOF(x)							\
	(ALIGN(sizeof(struct pmap) +					\
	       (sizeof(struct pmap_asid_info) * ((x) - 1))))

/* machine-dependent pg_flags */
#define	PGF_UNCACHED	PG_PMAP0	/* Page is explicitly uncached */
#define	PGF_CACHED	PG_PMAP1	/* Page is currently cached */
#define	PGF_ATTR_MOD	PG_PMAP2
#define	PGF_ATTR_REF	PG_PMAP3
#define	PGF_PRESERVE	(PGF_ATTR_MOD | PGF_ATTR_REF)

#define	PMAP_NOCACHE	PMAP_MD0

extern	struct pmap *const kernel_pmap_ptr;

#define	pmap_resident_count(pmap)       ((pmap)->pm_stats.resident_count)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)
#define	pmap_kernel()			(kernel_pmap_ptr)

extern pt_entry_t pg_ri;
#define PMAP_CHECK_COPYIN		(pg_ri == 0)

#define	PMAP_STEAL_MEMORY		/* Enable 'stealing' during boot */

#define	PMAP_PREFER
extern vaddr_t pmap_prefer_mask;
/* pmap prefer alignment */
#define	PMAP_PREFER_ALIGN()						\
	(pmap_prefer_mask ? pmap_prefer_mask + 1 : 0)
/* pmap prefer offset in alignment */
#define	PMAP_PREFER_OFFSET(of)		((of) & pmap_prefer_mask)

void	pmap_bootstrap(void);
int	pmap_copyinsn(pmap_t, vaddr_t, uint32_t *);
int	pmap_emulate_modify(pmap_t, vaddr_t);
void	pmap_page_cache(vm_page_t, u_int);

#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define	pmap_unuse_final(p)		do { /* nothing yet */ } while (0)
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

#define	__HAVE_PMAP_DIRECT
vaddr_t	pmap_map_direct(vm_page_t);
vm_page_t pmap_unmap_direct(vaddr_t);

/*
 * MD flags to pmap_enter:
 */

#define	PMAP_PA_MASK	~((paddr_t)PAGE_MASK)

/* Kernel virtual address to page table entry */
#define	kvtopte(va) \
	(Sysmap + (((vaddr_t)(va) - VM_MIN_KERNEL_ADDRESS) >> PAGE_SHIFT))
/* User virtual address to pte page entry */
#define	uvtopte(va)	(((va) >> PAGE_SHIFT) & (NPTEPG -1))
#define	uvtopde(va)	(((va) >> DIRSHIFT) & (NPDEPG - 1))

static inline pt_entry_t *
pmap_pte_lookup(struct pmap *pmap, vaddr_t va)
{
	pt_entry_t **pde, *pte;

	if ((pde = pmap_segmap(pmap, va)) == NULL)
		return NULL;
	if ((pte = pde[uvtopde(va)]) == NULL)
		return NULL;
	return pte + uvtopte(va);
}

extern	pt_entry_t *Sysmap;		/* kernel pte table */
extern	u_int Sysmapsize;		/* number of pte's in Sysmap */

#endif	/* _KERNEL */

#if !defined(_LOCORE)
typedef struct pv_entry {
	struct pv_entry	*pv_next;	/* next pv_entry */
	struct pmap	*pv_pmap;	/* pmap where mapping lies */
	vaddr_t		pv_va;		/* virtual address for mapping */
} *pv_entry_t;

struct vm_page_md {
	struct mutex	pv_mtx;		/* pv list lock */
	struct pv_entry pv_ent;		/* pv list of this seg */
};

#define	VM_MDPAGE_INIT(pg) \
	do { \
		mtx_init(&(pg)->mdpage.pv_mtx, IPL_VM); \
		(pg)->mdpage.pv_ent.pv_next = NULL; \
		(pg)->mdpage.pv_ent.pv_pmap = NULL; \
		(pg)->mdpage.pv_ent.pv_va = 0; \
	} while (0)

#endif	/* !_LOCORE */

#endif	/* !_MIPS64_PMAP_H_ */
