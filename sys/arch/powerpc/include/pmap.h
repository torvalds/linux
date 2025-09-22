/*	$OpenBSD: pmap.h,v 1.66 2024/06/18 12:37:29 jsg Exp $	*/
/*	$NetBSD: pmap.h,v 1.1 1996/09/30 16:34:29 ws Exp $	*/

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_POWERPC_PMAP_H_
#define	_POWERPC_PMAP_H_

#include <machine/pte.h>

/*
 * Segment registers
 */
#ifndef	_LOCORE
typedef u_int sr_t;
#endif	/* _LOCORE */
#define	SR_TYPE		0x80000000
#define	SR_SUKEY	0x40000000
#define	SR_PRKEY	0x20000000
#define SR_NOEXEC	0x10000000
#define	SR_VSID		0x00ffffff
/*
 * bit 
 *   3  2 2  2    2 1  1 1  1 1            0
 *   1  8 7  4    0 9  6 5  2 1            0
 *  |XXXX|XXXX XXXX|XXXX XXXX|XXXX XXXX XXXX
 *
 *  bits 28 - 31 contain SR
 *  bits 20 - 27 contain L1 for VtoP translation
 *  bits 12 - 19 contain L2 for VtoP translation
 *  bits  0 - 11 contain page offset
 */
#ifndef _LOCORE
/* V->P mapping data */
#define VP_SR_SIZE	16
#define VP_SR_MASK	(VP_SR_SIZE-1)
#define VP_SR_POS 	28
#define VP_IDX1_SIZE	256
#define VP_IDX1_MASK	(VP_IDX1_SIZE-1)
#define VP_IDX1_POS 	20
#define VP_IDX2_SIZE	256
#define VP_IDX2_MASK	(VP_IDX2_SIZE-1)
#define VP_IDX2_POS 	12

/* cache flags */
#define PMAP_CACHE_DEFAULT	0 	/* WB cache managed mem, devices not */
#define PMAP_CACHE_CI		1 	/* cache inhibit */
#define PMAP_CACHE_WT		2 	/* writethru */
#define PMAP_CACHE_WB		3	/* writeback */

#include <sys/mutex.h>
#include <sys/queue.h>

#ifdef	_KERNEL

/*
 * Pmap stuff
 */
struct pmap {
	sr_t pm_sr[16];		/* segments used in this pmap */
	struct pmapvp *pm_vp[VP_SR_SIZE];	/* virtual to physical table */
	u_int32_t pm_exec[16];	/* segments used in this pmap */
	int pm_refs;		/* ref count */
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct mutex		pm_mtx;		/* protect VP table */
};

/*
 * Segment handling stuff
 */
#define	PPC_SEGMENT_LENGTH	0x10000000
#define	PPC_SEGMENT_MASK	0xf0000000

/*
 * Some system constants
 */
#ifndef	NPMAPS
#define	NPMAPS		32768	/* Number of pmaps in system */
#endif

typedef	struct pmap *pmap_t;

extern struct pmap kernel_pmap_;
#define	pmap_kernel()	(&kernel_pmap_)


#define pmap_clear_modify(pg)		pmap_clear_attrs((pg), PG_PMAP_MOD)
#define	pmap_clear_reference(pg)	pmap_clear_attrs((pg), PG_PMAP_REF)
#define	pmap_is_modified(pg)		pmap_test_attrs((pg), PG_PMAP_MOD)
#define	pmap_is_referenced(pg)		pmap_test_attrs((pg), PG_PMAP_REF)

#define	pmap_unwire(pm, va)
#define pmap_update(pmap)	/* nothing (yet) */

#define pmap_resident_count(pmap)       ((pmap)->pm_stats.resident_count) 

/*
 * Alternate mapping methods for pool.
 * Really simple. 0x0->0x80000000 contain 1->1 mappings of the physical
 * memory. - XXX
 */
#define pmap_map_direct(pg)		((vaddr_t)VM_PAGE_TO_PHYS(pg))
#define pmap_unmap_direct(va)		PHYS_TO_VM_PAGE((paddr_t)va)
#define	__HAVE_PMAP_DIRECT

void pmap_bootstrap(u_int kernelstart, u_int kernelend);
void pmap_enable_mmu();

int pmap_clear_attrs(struct vm_page *, unsigned int);
int pmap_test_attrs(struct vm_page *, unsigned int);

void pmap_release(struct pmap *);

#ifdef ALTIVEC
int pmap_copyinsn(pmap_t, vaddr_t, uint32_t *);
#endif

void pmap_real_memory(vaddr_t *start, vsize_t *size);

int pte_spill_v(struct pmap *pm, u_int32_t va, u_int32_t dsisr, int exec_fault);
int reserve_dumppages(caddr_t p);

#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)		/* nothing */
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

#define PMAP_CHECK_COPYIN	(ppc_proc_is_64b == 0)

#define	PMAP_STEAL_MEMORY

#define PG_PMAP_MOD	PG_PMAP0
#define PG_PMAP_REF	PG_PMAP1
#define PG_PMAP_EXE	PG_PMAP2
#define PG_PMAP_UC	PG_PMAP3

/*
 * MD flags that we use for pmap_enter (in the pa):
 */
#define PMAP_PA_MASK	~((paddr_t)PAGE_MASK) /* to remove the flags */
#define PMAP_NOCACHE	0x1		/* map uncached */
#define PMAP_WT		0x2		/* map write-through */

#endif	/* _KERNEL */

struct vm_page_md {
	struct mutex pv_mtx;
	LIST_HEAD(,pte_desc) pv_list;
};

#define VM_MDPAGE_INIT(pg) do {                 \
	mtx_init(&(pg)->mdpage.pv_mtx, IPL_VM); \
	LIST_INIT(&((pg)->mdpage.pv_list)); 	\
} while (0)

#endif	/* _LOCORE */

#endif	/* _POWERPC_PMAP_H_ */
