/*	$OpenBSD: pmap.h,v 1.98 2025/07/16 07:15:42 jsg Exp $	*/
/*	$NetBSD: pmap.h,v 1.44 2000/04/24 17:18:18 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * pmap.h: see pmap.c for the history of this pmap module.
 */

#ifndef	_MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#ifdef _KERNEL
#include <machine/cpufunc.h>
#include <machine/segments.h>
#endif
#include <sys/mutex.h>
#include <uvm/uvm_object.h>
#include <machine/pte.h>

#define	PDSLOT_PTE	((KERNBASE/NBPD)-2) /* 830: for recursive PDP map */
#define	PDSLOT_KERN	(KERNBASE/NBPD) /* 832: start of kernel space */
#define	PDSLOT_APTE	((unsigned)1022) /* 1022: alternative recursive slot */

/*
 * The following define determines how many PTPs should be set up for the
 * kernel by locore.s at boot time.  This should be large enough to
 * get the VM system running.  Once the VM system is running, the
 * pmap module can add more PTPs to the kernel area on demand.
 */

#ifndef	NKPTP
#define	NKPTP		8	/* 16/32MB to start */
#endif
#define	NKPTP_MIN	4	/* smallest value we allow */

/*
 * PG_AVAIL usage: we make use of the ignored bits of the PTE
 */

#define PG_W		PG_AVAIL1	/* "wired" mapping */
#define PG_PVLIST	PG_AVAIL2	/* mapping has entry on pvlist */
#define	PG_X		PG_AVAIL3	/* executable mapping */

#define PTP0_PA             (PAGE_SIZE * 3)

#ifdef _KERNEL
/*
 * pmap data structures: see pmap.c for details of locking.
 */

struct pmap;
typedef struct pmap *pmap_t;

/*
 * We maintain a list of all non-kernel pmaps.
 */

LIST_HEAD(pmap_head, pmap); /* struct pmap_head: head of a pmap list */

/*
 * The pmap structure
 *
 * Note that the pm_obj contains the reference count,
 * page list, and number of PTPs within the pmap.
 */

struct pmap {
	uint64_t pm_pdidx[4];		/* PDIEs for PAE mode */
	uint64_t pm_pdidx_intel[4];	/* PDIEs for PAE mode U-K */

	struct mutex pm_mtx;
	struct mutex pm_apte_mtx;

	/*
	 * pm_pdir		: VA of PD when executing in privileged mode
	 *			  (lock by object lock)
	 * pm_pdirpa		: PA of PD when executing in privileged mode,
	 *			  (read-only after create)
	 * pm_pdir_intel	: VA of PD when executing on Intel CPU in
	 *			  usermode (no kernel mappings)
	 * pm_pdirpa_intel	: PA of PD when executing on Intel CPU in
	 *			  usermode (no kernel mappings)
	 */
	paddr_t pm_pdirpa, pm_pdirpa_intel;
	vaddr_t pm_pdir, pm_pdir_intel;
	int	pm_pdirsize;		/* PD size (4k vs 16k on PAE) */
	struct uvm_object pm_obj;	/* object (lck by object lock) */
	LIST_ENTRY(pmap) pm_list;	/* list (lck by pm_list lock) */
	struct vm_page *pm_ptphint;	/* pointer to a PTP in our pmap */
	struct pmap_statistics pm_stats;  /* pmap stats (lck by object lock) */

	vaddr_t pm_hiexec;		/* highest executable mapping */
	int pm_flags;			/* see below */

	struct segment_descriptor pm_codeseg;	/* cs descriptor for process */
};

/*
 * For each managed physical page we maintain a list of <PMAP,VA>s
 * which it is mapped at.  The list is headed by a pv_head structure.
 * there is one pv_head per managed phys page (allocated at boot time).
 * The pv_head structure points to a list of pv_entry structures (each
 * describes one mapping).
 */

struct pv_entry {			/* locked by its list's pvh_lock */
	struct pv_entry *pv_next;	/* next entry */
	struct pmap *pv_pmap;		/* the pmap */
	vaddr_t pv_va;			/* the virtual address */
	struct vm_page *pv_ptp;		/* the vm_page of the PTP */
};
/*
 * MD flags to pmap_enter:
 */

/* to get just the pa from params to pmap_enter */
#define PMAP_PA_MASK	~((paddr_t)PAGE_MASK)
#define	PMAP_NOCACHE	0x1		/* map uncached */
#define	PMAP_WC		0x2		/* map write combining. */

/*
 * We keep mod/ref flags in struct vm_page->pg_flags.
 */
#define	PG_PMAP_MOD	PG_PMAP0
#define	PG_PMAP_REF	PG_PMAP1
#define	PG_PMAP_WC	PG_PMAP2

/*
 * pv_entrys are dynamically allocated in chunks from a single page.
 * we keep track of how many pv_entrys are in use for each page and
 * we can free pv_entry pages if needed.  There is one lock for the
 * entire allocation system.
 */

struct pv_page_info {
	TAILQ_ENTRY(pv_page) pvpi_list;
	struct pv_entry *pvpi_pvfree;
	int pvpi_nfree;
};

/*
 * number of pv_entries in a pv_page
 */

#define PVE_PER_PVPAGE ((PAGE_SIZE - sizeof(struct pv_page_info)) / \
			sizeof(struct pv_entry))

/*
 * a pv_page: where pv_entrys are allocated from
 */

struct pv_page {
	struct pv_page_info pvinfo;
	struct pv_entry pvents[PVE_PER_PVPAGE];
};

/*
 * pv_entrys are dynamically allocated in chunks from a single page.
 * we keep track of how many pv_entrys are in use for each page and
 * we can free pv_entry pages if needed.  There is one lock for the
 * entire allocation system.
 */

extern char PTD[];
extern struct pmap kernel_pmap_store; /* kernel pmap */
extern int nkptp_max;

#define PMAP_REMOVE_ALL 0
#define PMAP_REMOVE_SKIPWIRED 1

extern struct pool pmap_pv_pool;

/*
 * Macros
 */

#define	pmap_kernel()			(&kernel_pmap_store)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)
#define	pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define	pmap_update(pm)			/* nada */

#define pmap_clear_modify(pg)		pmap_clear_attrs(pg, PG_M)
#define pmap_clear_reference(pg)	pmap_clear_attrs(pg, PG_U)
#define pmap_is_modified(pg)		pmap_test_attrs(pg, PG_M)
#define pmap_is_referenced(pg)		pmap_test_attrs(pg, PG_U)
#define pmap_valid_entry(E) 		((E) & PG_V) /* is PDE or PTE valid? */

#define pmap_proc_iflush(p,va,len)	/* nothing */
#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)		/* nothing */
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

/*
 * Prototypes
 */

vaddr_t pmap_tmpmap_pa_86(paddr_t);
vaddr_t pmap_tmpmap_pa(paddr_t);
void pmap_tmpunmap_pa_86(void);
void pmap_tmpunmap_pa(void);

void pmap_bootstrap(vaddr_t);
void pmap_bootstrap_pae(void);
void pmap_virtual_space(vaddr_t *, vaddr_t *);
void pmap_init(void);
struct pmap *pmap_create(void);
void pmap_destroy(struct pmap *);
void pmap_reference(struct pmap *);
void pmap_remove(struct pmap *, vaddr_t, vaddr_t);
void pmap_activate(struct proc *);
void pmap_deactivate(struct proc *);
void pmap_kenter_pa(vaddr_t, paddr_t, vm_prot_t);
void pmap_kremove(vaddr_t, vsize_t);
void pmap_zero_page(struct vm_page *);
void pmap_copy_page(struct vm_page *, struct vm_page *);
void pmap_enter_pv(struct vm_page *, struct pv_entry *,
    struct pmap *, vaddr_t, struct vm_page *);
int pmap_clear_attrs(struct vm_page *, int);
static void pmap_page_protect(struct vm_page *, vm_prot_t);
void pmap_page_remove(struct vm_page *);
static void pmap_protect(struct pmap *, vaddr_t,
    vaddr_t, vm_prot_t);
void pmap_remove(struct pmap *, vaddr_t, vaddr_t);
int pmap_test_attrs(struct vm_page *, int);
void pmap_write_protect(struct pmap *, vaddr_t,
    vaddr_t, vm_prot_t);
int pmap_exec_fixup(struct vm_map *, struct trapframe *,
    vaddr_t, struct pcb *);
void pmap_exec_account(struct pmap *, vaddr_t, u_int32_t,
    u_int32_t);
struct pv_entry *pmap_remove_pv(struct vm_page *, struct pmap *, vaddr_t);
void pmap_apte_flush(void);
void pmap_switch(struct proc *, struct proc *);
vaddr_t reserve_dumppages(vaddr_t); /* XXX: not a pmap fn */
paddr_t vtophys(vaddr_t va);
paddr_t vtophys_pae(vaddr_t va);

extern u_int32_t (*pmap_pte_set_p)(vaddr_t, paddr_t, u_int32_t);
extern u_int32_t (*pmap_pte_setbits_p)(vaddr_t, u_int32_t, u_int32_t);
extern u_int32_t (*pmap_pte_bits_p)(vaddr_t);
extern paddr_t (*pmap_pte_paddr_p)(vaddr_t);
extern int (*pmap_clear_attrs_p)(struct vm_page *, int);
extern int (*pmap_enter_p)(pmap_t, vaddr_t, paddr_t, vm_prot_t, int);
extern void (*pmap_enter_special_p)(vaddr_t, paddr_t, vm_prot_t, u_int32_t);
extern int (*pmap_extract_p)(pmap_t, vaddr_t, paddr_t *);
extern vaddr_t (*pmap_growkernel_p)(vaddr_t);
extern void (*pmap_page_remove_p)(struct vm_page *);
extern void (*pmap_do_remove_p)(struct pmap *, vaddr_t, vaddr_t, int);
extern int (*pmap_test_attrs_p)(struct vm_page *, int);
extern void (*pmap_unwire_p)(struct pmap *, vaddr_t);
extern void (*pmap_write_protect_p)(struct pmap*, vaddr_t, vaddr_t, vm_prot_t);
extern void (*pmap_pinit_pd_p)(pmap_t);
extern void (*pmap_zero_phys_p)(paddr_t);
extern void (*pmap_copy_page_p)(struct vm_page *, struct vm_page *);

u_int32_t pmap_pte_set_pae(vaddr_t, paddr_t, u_int32_t);
u_int32_t pmap_pte_setbits_pae(vaddr_t, u_int32_t, u_int32_t);
u_int32_t pmap_pte_bits_pae(vaddr_t);
paddr_t pmap_pte_paddr_pae(vaddr_t);
int pmap_clear_attrs_pae(struct vm_page *, int);
int pmap_enter_pae(pmap_t, vaddr_t, paddr_t, vm_prot_t, int);
void pmap_enter_special_pae(vaddr_t, paddr_t, vm_prot_t, u_int32_t);
int pmap_extract_pae(pmap_t, vaddr_t, paddr_t *);
vaddr_t pmap_growkernel_pae(vaddr_t);
void pmap_page_remove_pae(struct vm_page *);
void pmap_do_remove_pae(struct pmap *, vaddr_t, vaddr_t, int);
int pmap_test_attrs_pae(struct vm_page *, int);
void pmap_unwire_pae(struct pmap *, vaddr_t);
void pmap_write_protect_pae(struct pmap *, vaddr_t, vaddr_t, vm_prot_t);
void pmap_pinit_pd_pae(pmap_t);
void pmap_zero_phys_pae(paddr_t);
void pmap_copy_page_pae(struct vm_page *, struct vm_page *);

#define	pmap_pte_set		(*pmap_pte_set_p)
#define	pmap_pte_setbits	(*pmap_pte_setbits_p)
#define	pmap_pte_bits		(*pmap_pte_bits_p)
#define	pmap_pte_paddr		(*pmap_pte_paddr_p)
#define	pmap_clear_attrs	(*pmap_clear_attrs_p)
#define	pmap_page_remove	(*pmap_page_remove_p)
#define	pmap_do_remove		(*pmap_do_remove_p)
#define	pmap_test_attrs		(*pmap_test_attrs_p)
#define	pmap_unwire		(*pmap_unwire_p)
#define	pmap_write_protect	(*pmap_write_protect_p)
#define	pmap_pinit_pd		(*pmap_pinit_pd_p)
#define	pmap_zero_phys		(*pmap_zero_phys_p)
#define	pmap_copy_page		(*pmap_copy_page_p)

u_int32_t pmap_pte_set_86(vaddr_t, paddr_t, u_int32_t);
u_int32_t pmap_pte_setbits_86(vaddr_t, u_int32_t, u_int32_t);
u_int32_t pmap_pte_bits_86(vaddr_t);
paddr_t pmap_pte_paddr_86(vaddr_t);
int pmap_clear_attrs_86(struct vm_page *, int);
int pmap_enter_86(pmap_t, vaddr_t, paddr_t, vm_prot_t, int);
void pmap_enter_special_86(vaddr_t, paddr_t, vm_prot_t, u_int32_t);
int pmap_extract_86(pmap_t, vaddr_t, paddr_t *);
vaddr_t pmap_growkernel_86(vaddr_t);
void pmap_page_remove_86(struct vm_page *);
void pmap_do_remove_86(struct pmap *, vaddr_t, vaddr_t, int);
int pmap_test_attrs_86(struct vm_page *, int);
void pmap_unwire_86(struct pmap *, vaddr_t);
void pmap_write_protect_86(struct pmap *, vaddr_t, vaddr_t, vm_prot_t);
void pmap_pinit_pd_86(pmap_t);
void pmap_zero_phys_86(paddr_t);
void pmap_copy_page_86(struct vm_page *, struct vm_page *);
void pmap_tlb_shootpage(struct pmap *, vaddr_t);
void pmap_tlb_shootrange(struct pmap *, vaddr_t, vaddr_t);
void pmap_tlb_shoottlb(void);
#ifdef MULTIPROCESSOR
void pmap_tlb_droppmap(struct pmap *);
void pmap_tlb_shootwait(void);
#else
#define pmap_tlb_shootwait()
#endif

void pmap_prealloc_lowmem_ptp(void);
void pmap_prealloc_lowmem_ptp_pae(void);
vaddr_t pmap_tmpmap_pa(paddr_t);
void pmap_tmpunmap_pa(void);
vaddr_t pmap_tmpmap_pa_pae(paddr_t);
void pmap_tmpunmap_pa_pae(void);


/* 
 * functions for flushing the cache for vaddrs and pages.
 * these functions are not part of the MI pmap interface and thus
 * should not be used as such.
 */
void pmap_flush_cache(vaddr_t, vsize_t);
void pmap_flush_page(paddr_t);
void pmap_flush_page_pae(paddr_t);

#define PMAP_CHECK_COPYIN	1

#define PMAP_GROWKERNEL		/* turn on pmap_growkernel interface */

/*
 * Inline functions
 */

/*
 * pmap_update_pg: flush one page from the TLB (or flush the whole thing
 *	if hardware doesn't support one-page flushing)
 */

#define pmap_update_pg(va)	invlpg((u_int)(va))

/*
 * pmap_update_2pg: flush two pages from the TLB
 */

#define pmap_update_2pg(va, vb) { invlpg((u_int)(va)); invlpg((u_int)(vb)); }

/*
 * pmap_page_protect: change the protection of all recorded mappings
 *	of a managed page
 *
 * => This function is a front end for pmap_page_remove/pmap_clear_attrs
 * => We only have to worry about making the page more protected.
 *	Unprotecting a page is done on-demand at fault time.
 */

static __inline void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	if ((prot & PROT_WRITE) == 0) {
		if (prot & (PROT_READ | PROT_EXEC)) {
			(void) pmap_clear_attrs(pg, PG_RW);
		} else {
			pmap_page_remove(pg);
		}
	}
}

/*
 * pmap_protect: change the protection of pages in a pmap
 *
 * => This function is a front end for pmap_remove/pmap_write_protect.
 * => We only have to worry about making the page more protected.
 *	Unprotecting a page is done on-demand at fault time.
 */

static __inline void
pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	if ((prot & PROT_WRITE) == 0) {
		if (prot & (PROT_READ | PROT_EXEC)) {
			pmap_write_protect(pmap, sva, eva, prot);
		} else {
			pmap_remove(pmap, sva, eva);
		}
	}
}

/*
 * pmap_growkernel, pmap_enter, and pmap_extract get picked up in various
 * modules from both uvm_pmap.h and pmap.h. Since uvm_pmap.h defines these
 * as functions, inline them here to suppress linker warnings.
 */
static __inline vaddr_t
pmap_growkernel(vaddr_t maxkvaddr)
{
	return (*pmap_growkernel_p)(maxkvaddr);
}

static __inline int
pmap_enter(struct pmap *pmap, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	return (*pmap_enter_p)(pmap, va, pa, prot, flags);
}

static __inline void
pmap_enter_special(vaddr_t va, paddr_t pa, vm_prot_t prot, u_int32_t flags)
{
	(*pmap_enter_special_p)(va, pa, prot, flags);
}

static __inline int
pmap_extract(struct pmap *pmap, vaddr_t va, paddr_t *pa)
{
	return (*pmap_extract_p)(pmap, va, pa);
}

/*
 * p m a p   i n l i n e   h e l p e r   f u n c t i o n s
 */

/*
 * pmap_is_active: is this pmap loaded into the specified processor's %cr3?
 */

static __inline int
pmap_is_active(struct pmap *pmap, struct cpu_info *ci)
{
	return (pmap == pmap_kernel() || ci->ci_curpmap == pmap);
}

static __inline int
pmap_is_curpmap(struct pmap *pmap)
{
	return (pmap_is_active(pmap, curcpu()));
}

#endif /* _KERNEL */

struct pv_entry;
struct vm_page_md {
	struct mutex pv_mtx;
	struct pv_entry *pv_list;
};

#define VM_MDPAGE_INIT(pg) do {			\
	mtx_init(&(pg)->mdpage.pv_mtx, IPL_VM); \
	(pg)->mdpage.pv_list = NULL;	\
} while (0)

#endif	/* _MACHINE_PMAP_H_ */
