/*	$OpenBSD: pmap.h,v 1.14 2025/02/03 17:59:40 jca Exp $	*/

/*
 * Copyright (c) 2019-2020 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2008,2009,2014 Dale Rahn <drahn@dalerahn.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef	_MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#ifndef _LOCORE
#include <sys/mutex.h>
#include <sys/queue.h>
#include <machine/pte.h>
#endif


/* V->P mapping data */
// XXX Only targeting compatibility with SV39
#define VP_IDX0_CNT	512
#define VP_IDX0_MASK	(VP_IDX0_CNT-1)
#define VP_IDX0_POS	39
#define VP_IDX1_CNT	512
#define VP_IDX1_MASK	(VP_IDX1_CNT-1)
#define VP_IDX1_POS	30
#define VP_IDX2_CNT	512
#define VP_IDX2_MASK	(VP_IDX2_CNT-1)
#define VP_IDX2_POS	21
#define VP_IDX3_CNT	512
#define VP_IDX3_MASK	(VP_IDX3_CNT-1)
#define VP_IDX3_POS	12

/* cache flags */
// XXX These are duplicated from arm64 and may need some reworking
#define PMAP_CACHE_CI		(PMAP_MD0)		/* cache inhibit */
#define PMAP_CACHE_WB		(PMAP_MD1)		/* writeback */
#define PMAP_CACHE_DEV		(PMAP_MD2)		/* device mapping */
#define PMAP_CACHE_BITS		(PMAP_MD0|PMAP_MD1|PMAP_MD2)

#define PTED_VA_MANAGED_M	(PMAP_MD3)
#define PTED_VA_WIRED_M		(PMAP_MD3 << 1)

#if defined(_KERNEL) && !defined(_LOCORE)
/*
 * Pmap stuff
 */

typedef struct pmap *pmap_t;

struct pmap {
	struct mutex pm_mtx;
	union {
		// XXX Sv48 not yet supported
		// XXX Consider inverting Lx
		struct pmapvp1 *l1;	/* virtual to physical table 3 lvl */
	} pm_vp;
	uint64_t pm_satp;
	int pm_privileged;
	int pm_refs;				/* ref count */
	struct pmap_statistics  pm_stats;	/* pmap statistics */
};

#define PMAP_PA_MASK	~((paddr_t)PAGE_MASK) /* to remove the flags */
#define PMAP_NOCACHE	0x1 /* non-cacheable memory */
#define PMAP_DEVICE	0x2 /* device memory */

#define PG_PMAP_MOD		PG_PMAP0
#define PG_PMAP_REF		PG_PMAP1
#define PG_PMAP_EXE		PG_PMAP2

// [NCPUS]
extern paddr_t zero_page;
extern paddr_t copy_src_page;
extern paddr_t copy_dst_page;

void pagezero(vaddr_t);

extern struct pmap kernel_pmap_;
#define pmap_kernel()			(&kernel_pmap_)
#define	pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)

vaddr_t pmap_bootstrap(long kvo, paddr_t lpt1,
		vaddr_t kernelstart, vaddr_t kernelend,
		paddr_t memstart, paddr_t memend);
void pmap_kenter_cache(vaddr_t va, paddr_t pa, vm_prot_t prot, int cacheable);
void pmap_page_ro(pmap_t pm, vaddr_t va, vm_prot_t prot);

paddr_t pmap_steal_avail(size_t size, int align, void **kva);
void pmap_avail_fixup(void);
void pmap_physload_avail(void);

#define PMAP_GROWKERNEL

#define	PHYS_TO_DMAP(pa)	((pa) - dmap_phys_base + DMAP_MIN_ADDRESS)

struct pv_entry;

/* investigate */
#define pmap_unuse_final(p)		do { /* nothing */ } while (0)
int	pmap_fault_fixup(pmap_t, vaddr_t, vm_prot_t);
void	pmap_postinit(void);
void	pmap_init_percpu(void);

#define __HAVE_PMAP_POPULATE

#endif /* _KERNEL && !_LOCORE */

#ifndef _LOCORE
struct vm_page_md {
	struct mutex pv_mtx;
	LIST_HEAD(,pte_desc) pv_list;
};

#define VM_MDPAGE_INIT(pg) do {			\
	mtx_init(&(pg)->mdpage.pv_mtx, IPL_VM);	\
	LIST_INIT(&((pg)->mdpage.pv_list));	\
} while (0)
#endif	/* _LOCORE */

#endif	/* _MACHINE_PMAP_H_ */
