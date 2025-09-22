/*	$OpenBSD: pmap.h,v 1.56 2023/12/11 22:12:53 kettenis Exp $	*/

/*
 * Copyright (c) 2002-2004 Michael Shalayeff
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_PMAP_H_
#define _MACHINE_PMAP_H_

#include <uvm/uvm_object.h>
#include <sys/mutex.h>

#ifdef	_KERNEL
#include <sys/systm.h>
#include <machine/pte.h>

struct pmap {
	struct mutex pm_mtx;
	struct uvm_object pm_obj;
	struct vm_page	*pm_ptphint;
	struct vm_page	*pm_pdir_pg;	/* vm_page for pdir */
	volatile u_int32_t *pm_pdir;	/* page dir (read-only after create) */
	pa_space_t	pm_space;	/* space id (read-only after create) */
	u_int		pm_pid;		/* prot id (read-only after create) */

	struct pmap_statistics	pm_stats;
};
typedef struct pmap *pmap_t;

#define HPPA_MAX_PID    0xfffa
#define	HPPA_SID_MAX	0x7ffd
#define HPPA_SID_KERNEL 0
#define HPPA_PID_KERNEL 2

#define KERNEL_ACCESS_ID 1
#define KERNEL_TEXT_PROT (TLB_AR_KRX | (KERNEL_ACCESS_ID << 1))
#define KERNEL_DATA_PROT (TLB_AR_KRW | (KERNEL_ACCESS_ID << 1))

struct pv_entry {			/* locked by its list's pvh_lock */
	struct pv_entry	*pv_next;
	struct pmap	*pv_pmap;	/* the pmap */
	vaddr_t		pv_va;		/* the virtual address */
	struct vm_page	*pv_ptp;	/* the vm_page of the PTP */
};

/* also match the hardware tlb walker definition */
struct vp_entry {
	u_int	vp_tag;
	u_int	vp_tlbprot;
	u_int	vp_tlbpage;
	u_int	vp_ptr;
};

extern void gateway_page(void);
extern struct pmap kernel_pmap_store;

#if defined(HP7100LC_CPU) || defined(HP7300LC_CPU)
extern int pmap_hptsize;
extern struct pdc_hwtlb pdc_hwtlb;
#endif

/*
 * pool quickmaps
 */
#define	pmap_map_direct(pg)	((vaddr_t)VM_PAGE_TO_PHYS(pg))
struct vm_page *pmap_unmap_direct(vaddr_t);
#define	__HAVE_PMAP_DIRECT

/*
 * according to the parisc manual aliased va's should be
 * different by high 12 bits only.
 */
#define	PMAP_PREFER
/* pmap prefer alignment */
#define PMAP_PREFER_ALIGN()	(HPPA_PGALIAS)
/* pmap prefer offset within alignment */
#define PMAP_PREFER_OFFSET(of)	((of) & HPPA_PGAOFF)

#define	pmap_sid2pid(s)			(((s) + 1) << 1)
#define pmap_kernel()			(&kernel_pmap_store)
#define	pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define	pmap_update(pm)			(void)(pm)

#define	PG_PMAP_MOD		PG_PMAP0	/* modified */
#define	PG_PMAP_REF		PG_PMAP1	/* referenced */

#define pmap_clear_modify(pg)	pmap_changebit(pg, 0, PTE_PROT(TLB_DIRTY))
#define pmap_clear_reference(pg) pmap_changebit(pg, PTE_PROT(TLB_REFTRAP), 0)
#define pmap_is_modified(pg)	pmap_testbit(pg, PG_PMAP_MOD)
#define pmap_is_referenced(pg)	pmap_testbit(pg, PG_PMAP_REF)

#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)		/* nothing */
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

void pmap_bootstrap(vaddr_t);
boolean_t pmap_changebit(struct vm_page *, pt_entry_t, pt_entry_t);
boolean_t pmap_testbit(struct vm_page *, int);
void pmap_page_write_protect(struct vm_page *);
void pmap_write_protect(struct pmap *, vaddr_t, vaddr_t, vm_prot_t);
void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva);
void pmap_page_remove(struct vm_page *pg);

static __inline int
pmap_prot(struct pmap *pmap, int prot)
{
	extern u_int hppa_prot[];
	return (hppa_prot[prot] | (pmap == pmap_kernel()? 0 : TLB_USER));
}

static __inline void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	if (prot == PROT_READ) {
		pmap_page_write_protect(pg);
	} else {
		KASSERT(prot == PROT_NONE);
		pmap_page_remove(pg);
	}
}

static __inline void
pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	if (prot != PROT_NONE)
		pmap_write_protect(pmap, sva, eva, prot);
	else
		pmap_remove(pmap, sva, eva);
}

#endif /* _KERNEL */

#if !defined(_LOCORE)
struct pv_entry;
struct vm_page_md {
	struct mutex pvh_mtx;
	struct pv_entry	*pvh_list;	/* head of list (locked by pvh_mtx) */
};

#define	VM_MDPAGE_INIT(pg) do {				\
	mtx_init(&(pg)->mdpage.pvh_mtx, IPL_VM);	\
	(pg)->mdpage.pvh_list = NULL;			\
} while (0)
#endif

#endif /* _MACHINE_PMAP_H_ */
