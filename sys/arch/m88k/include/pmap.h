/*	$OpenBSD: pmap.h,v 1.32 2025/06/02 18:49:04 claudio Exp $	*/
/*
 * Mach Operating System
 * Copyright (c) 1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */
#ifndef _M88K_PMAP_H_
#define _M88K_PMAP_H_

#ifdef	_KERNEL

#include <machine/mmu.h>

/*
 * PMAP structure
 */

struct pmap {
	sdt_entry_t		*pm_stab;	/* virtual pointer to sdt */
	apr_t			 pm_apr;
	int			 pm_count;	/* reference count */
	struct pmap_statistics	pm_stats;	/* pmap statistics */
};

/* 	The PV (Physical to virtual) List.
 *
 * For each vm_page_t, pmap keeps a list of all currently valid virtual
 * mappings of that page. An entry is a pv_entry_t; the list is the
 * pv_head_table. This is used by things like pmap_remove, when we must
 * find and remove all mappings for a particular physical page.
 */
/* XXX - struct pv_entry moved to vmparam.h because of include ordering issues */

typedef struct pmap *pmap_t;
typedef struct pv_entry *pv_entry_t;

extern	pmap_t		kernel_pmap;
extern	struct pmap	kernel_pmap_store;
extern	caddr_t		vmmap;
extern	apr_t		kernel_apr, userland_apr;

#define	pmap_kernel()			(&kernel_pmap_store)
#define pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)

#define pmap_update(pmap)		do { /* nothing */ } while (0)

#define	pmap_clear_modify(pg)		pmap_unsetbit(pg, PG_M)
#define	pmap_clear_reference(pg)	pmap_unsetbit(pg, PG_U)

void	pmap_bootstrap(paddr_t, paddr_t);
void	pmap_bootstrap_cpu(cpuid_t);
void	pmap_cache_ctrl(vaddr_t, vaddr_t, u_int);
void	pmap_page_uncache(paddr_t);
int	pmap_set_modify(pmap_t, vaddr_t);
void	pmap_unmap_firmware(void);
boolean_t pmap_unsetbit(struct vm_page *, int);

#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)		/* nothing */
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)

int	pmap_translation_info(pmap_t, vaddr_t, paddr_t *, uint32_t *);
/*
 * pmap_translation_info() return values
 */
#define	PTI_INVALID	0
#define	PTI_PTE		1
#define	PTI_BATC	2

#define	pmap_map_direct(pg)		((vaddr_t)VM_PAGE_TO_PHYS(pg))
vm_page_t pmap_unmap_direct(vaddr_t);

#define	PMAP_CHECK_COPYIN	1

#define	__HAVE_PMAP_DIRECT
#define	PMAP_STEAL_MEMORY

#endif	/* _KERNEL */

#ifndef _LOCORE
struct pv_entry {
	struct pv_entry	*pv_next;	/* next pv_entry */
	struct pmap	*pv_pmap;	/* pmap where mapping lies */
	vaddr_t		pv_va;		/* virtual address for mapping */
};

struct vm_page_md {
	struct pv_entry pv_ent;
	int		pv_flags;
};

#define	VM_MDPAGE_INIT(pg)						\
do {									\
	(pg)->mdpage.pv_ent.pv_next = NULL;				\
	(pg)->mdpage.pv_ent.pv_pmap = NULL;				\
	(pg)->mdpage.pv_ent.pv_va = 0;					\
	(pg)->mdpage.pv_flags = 0;					\
} while (0)

#endif /* _LOCORE */

#endif /* _M88K_PMAP_H_ */
