/*	$OpenBSD: pmap.h,v 1.41 2025/09/18 14:54:33 kettenis Exp $	*/
/*	$NetBSD: pmap.h,v 1.16 2001/04/22 23:19:30 thorpej Exp $	*/

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

#ifndef	_MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#ifndef _LOCORE
#ifdef	_KERNEL
#include <sys/queue.h>
#endif
#include <sys/mutex.h>
#endif

#define __HAVE_PMAP_PURGE

/*
 * This scheme uses 2-level page tables.
 *
 * While we're still in 32-bit mode we do the following:
 *
 *   offset:						13 bits
 * 1st level: 1024 64-bit TTEs in an 8K page for	10 bits
 * 2nd level: 512 32-bit pointers in the pmap for 	 9 bits
 *							-------
 * total:						32 bits
 *
 * In 64-bit mode the Spitfire and Blackbird CPUs support only
 * 44-bit virtual addresses.  All addresses between 
 * 0x0000 07ff ffff ffff and 0xffff f800 0000 0000 are in the
 * "VA hole" and trap, so we don't have to track them.  However,
 * we do need to keep them in mind during PT walking.  If they
 * ever change the size of the address "hole" we need to rework
 * all the page table handling.
 *
 *   offset:						13 bits
 * 1st level: 1024 64-bit TTEs in an 8K page for	10 bits
 * 2nd level: 1024 64-bit pointers in an 8K page for 	10 bits
 * 3rd level: 1024 64-bit pointers in the segmap for 	10 bits
 *							-------
 * total:						43 bits
 *
 * Of course, this means for 32-bit spaces we always have a (practically)
 * wasted page for the segmap (only one entry used) and half a page wasted
 * for the page directory.  We still have need of one extra bit 8^(.
 */

#define HOLESHIFT	(43)

#define PTSZ	(PAGE_SIZE/8)
#define PDSZ	(PTSZ)
#define STSZ	(PTSZ)

#define PTSHIFT		(13)
#define	PDSHIFT		(10+PTSHIFT)
#define STSHIFT		(10+PDSHIFT)

#define PTMASK		(PTSZ-1)
#define PDMASK		(PDSZ-1)
#define STMASK		(STSZ-1)

#ifndef _LOCORE

#define va_to_seg(v)	(int)((((paddr_t)(v))>>STSHIFT)&STMASK)
#define va_to_dir(v)	(int)((((paddr_t)(v))>>PDSHIFT)&PDMASK)
#define va_to_pte(v)	(int)((((paddr_t)(v))>>PTSHIFT)&PTMASK)

#ifdef	_KERNEL

/*
 * Support for big page sizes.  This maps the page size to the
 * page bits.
 */
struct page_size_map {
	u_int64_t mask;
	u_int64_t code;
#ifdef DEBUG
	u_int64_t use;
#endif
};
extern const struct page_size_map page_size_map[];

struct pmap {
	struct mutex pm_mtx;
	int pm_ctx;		/* Current context */
	int pm_refs;		/* ref count */
	/* 
	 * This contains 64-bit pointers to pages that contain 
	 * 1024 64-bit pointers to page tables.  All addresses
	 * are physical.  
	 *
	 * !!! Only touch this through pseg_get() and pseg_set() !!!
	 */
	paddr_t pm_physaddr;	/* physical address of pm_segs */
	int64_t *pm_segs;

	struct pmap_statistics pm_stats;
};

/*
 * This comes from the PROM and is used to map prom entries.
 */
struct prom_map {
	u_int64_t	vstart;
	u_int64_t	vsize;
	u_int64_t	tte;
};

#define PMAP_NC		0x001	/* Set the E bit in the page */
#define PMAP_NVC	0x002	/* Don't enable the virtual cache */
#define PMAP_NOCACHE	PMAP_NC
#define PMAP_LITTLE	0x004	/* Map in little endian mode */
/* Large page size hints -- we really should use another param to pmap_enter() */
#define PMAP_8K		0x000
#define PMAP_64K	0x008	/* Use 64K page */
#define PMAP_512K	0x010
#define PMAP_4M		0x018
#define PMAP_SZ_TO_TTE(x)	(((x)&0x018)<<58)
/* If these bits are different in va's to the same PA then there is an aliasing in the d$ */
#define VA_ALIAS_ALIGN	(1<<14)
#define VA_ALIAS_MASK	(VA_ALIAS_ALIGN - 1)

typedef	struct pmap *pmap_t;

extern struct pmap kernel_pmap_;
#define	pmap_kernel()	(&kernel_pmap_)

/* int pmap_change_wiring(pmap_t pm, vaddr_t va, boolean_t wired); */
#define	pmap_resident_count(pm)		((pm)->pm_stats.resident_count)
#define	pmap_update(pm)			/* nothing (yet) */

#define pmap_proc_iflush(p,va,len)	/* nothing */
#define pmap_init_percpu()		do { /* nothing */ } while (0)

void	pmap_bootstrap(u_long, u_long, u_int, u_int);
int	pmap_copyinsn(pmap_t, vaddr_t, uint32_t *);

/* make sure all page mappings are modulo 16K to prevent d$ aliasing */
#define PMAP_PREFER
/* pmap prefer alignment */
#define PMAP_PREFER_ALIGN()	(VA_ALIAS_ALIGN)
/* pmap prefer offset in alignment */
#define PMAP_PREFER_OFFSET(of)	((of) & VA_ALIAS_MASK)

#define PMAP_CHECK_COPYIN	CPU_ISSUN4V

#define PMAP_GROWKERNEL         /* turn on pmap_growkernel interface */

/* SPARC specific? */
int	pmap_dumpsize(void);
int	pmap_dumpmmu(int (*)(dev_t, daddr_t, caddr_t, size_t), daddr_t);
int	pmap_pa_exists(paddr_t);

/* SPARC64 specific */
int	ctx_alloc(struct pmap*);
void	ctx_free(struct pmap*);

#endif	/* _KERNEL */

/*
 * For each struct vm_page, there is a list of all currently valid virtual
 * mappings of that page.
 */
typedef struct pv_entry {
	struct pv_entry	*pv_next;	/* next pv_entry */
	struct pmap	*pv_pmap;	/* pmap where mapping lies */
	vaddr_t		 pv_va;		/* virtual address for mapping */
} *pv_entry_t;
/* PV flags encoded in the low bits of the VA of the first pv_entry */

struct vm_page_md {
	struct mutex pvmtx;
	struct pv_entry pvent;
};

#define VM_MDPAGE_INIT(pg) do {			\
	mtx_init(&(pg)->mdpage.pvmtx, IPL_VM);	\
	(pg)->mdpage.pvent.pv_next = NULL;	\
	(pg)->mdpage.pvent.pv_pmap = NULL;	\
	(pg)->mdpage.pvent.pv_va = 0;		\
} while (0)

#endif	/* _LOCORE */
#endif	/* _MACHINE_PMAP_H_ */
