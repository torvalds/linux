/*-
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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
 * The ARM version of this file was more or less based on the i386 version,
 * which has the following provenance...
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

#ifndef _MACHINE_PMAP_V6_H_
#define _MACHINE_PMAP_V6_H_

#include <sys/queue.h>
#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>

typedef	uint32_t	pt1_entry_t;		/* L1 table entry */
typedef	uint32_t	pt2_entry_t;		/* L2 table entry */
typedef uint32_t	ttb_entry_t;		/* TTB entry */

#ifdef _KERNEL

#if 0
#define PMAP_PTE_NOCACHE // Use uncached page tables
#endif

/*
 *  (1) During pmap bootstrap, physical pages for L2 page tables are
 *      allocated in advance which are used for KVA continuous mapping
 *      starting from KERNBASE. This makes things more simple.
 *  (2) During vm subsystem initialization, only vm subsystem itself can
 *      allocate physical memory safely. As pmap_map() is called during
 *      this initialization, we must be prepared for that and have some
 *      preallocated physical pages for L2 page tables.
 *
 *  Note that some more pages for L2 page tables are preallocated too
 *  for mappings laying above VM_MAX_KERNEL_ADDRESS.
 */
#ifndef NKPT2PG
/*
 *  The optimal way is to define this in board configuration as
 *  definition here must be safe enough. It means really big.
 *
 *  1 GB KVA <=> 256 kernel L2 page table pages
 *
 *  From real platforms:
 *	1 GB physical memory <=> 10 pages is enough
 *	2 GB physical memory <=> 21 pages is enough
 */
#define NKPT2PG		32
#endif
#endif	/* _KERNEL */

/*
 * Pmap stuff
 */
struct	pv_entry;
struct	pv_chunk;

struct	md_page {
	TAILQ_HEAD(,pv_entry)	pv_list;
	uint16_t		pt2_wirecount[4];
	vm_memattr_t		pat_mode;
};

struct	pmap {
	struct mtx		pm_mtx;
	pt1_entry_t		*pm_pt1;	/* KVA of pt1 */
	pt2_entry_t		*pm_pt2tab;	/* KVA of pt2 pages table */
	TAILQ_HEAD(,pv_chunk)	pm_pvchunk;	/* list of mappings in pmap */
	cpuset_t		pm_active;	/* active on cpus */
	struct pmap_statistics	pm_stats;	/* pmap statictics */
	LIST_ENTRY(pmap) 	pm_list;	/* List of all pmaps */
};

typedef struct pmap *pmap_t;

#ifdef _KERNEL
extern struct pmap	        kernel_pmap_store;
#define kernel_pmap	        (&kernel_pmap_store)

#define	PMAP_LOCK(pmap)		mtx_lock(&(pmap)->pm_mtx)
#define	PMAP_LOCK_ASSERT(pmap, type) \
				mtx_assert(&(pmap)->pm_mtx, (type))
#define	PMAP_LOCK_DESTROY(pmap)	mtx_destroy(&(pmap)->pm_mtx)
#define	PMAP_LOCK_INIT(pmap)	mtx_init(&(pmap)->pm_mtx, "pmap", \
				    NULL, MTX_DEF | MTX_DUPOK)
#define	PMAP_LOCKED(pmap)	mtx_owned(&(pmap)->pm_mtx)
#define	PMAP_MTX(pmap)		(&(pmap)->pm_mtx)
#define	PMAP_TRYLOCK(pmap)	mtx_trylock(&(pmap)->pm_mtx)
#define	PMAP_UNLOCK(pmap)	mtx_unlock(&(pmap)->pm_mtx)
#endif

/*
 * For each vm_page_t, there is a list of all currently valid virtual
 * mappings of that page.  An entry is a pv_entry_t, the list is pv_list.
 */
typedef struct pv_entry {
	vm_offset_t	pv_va;		/* virtual address for mapping */
	TAILQ_ENTRY(pv_entry)	pv_next;
} *pv_entry_t;

/*
 * pv_entries are allocated in chunks per-process.  This avoids the
 * need to track per-pmap assignments.
 */
#define	_NPCM	11
#define	_NPCPV	336
struct pv_chunk {
	pmap_t			pc_pmap;
	TAILQ_ENTRY(pv_chunk)	pc_list;
	uint32_t		pc_map[_NPCM];	/* bitmap; 1 = free */
	TAILQ_ENTRY(pv_chunk)	pc_lru;
	struct pv_entry		pc_pventry[_NPCPV];
};

#ifdef _KERNEL
extern ttb_entry_t pmap_kern_ttb; 	/* TTB for kernel pmap */

#define	pmap_page_get_memattr(m)	((m)->md.pat_mode)

/*
 * Only the following functions or macros may be used before pmap_bootstrap()
 * is called: pmap_kenter(), pmap_kextract(), pmap_kremove(), vtophys(), and
 * vtopte2().
 */
void pmap_bootstrap(vm_offset_t);
void pmap_kenter(vm_offset_t, vm_paddr_t);
void pmap_kremove(vm_offset_t);
boolean_t pmap_page_is_mapped(vm_page_t);
bool	pmap_ps_enabled(pmap_t pmap);

void pmap_tlb_flush(pmap_t, vm_offset_t);
void pmap_tlb_flush_range(pmap_t, vm_offset_t, vm_size_t);

vm_paddr_t pmap_dump_kextract(vm_offset_t, pt2_entry_t *);

int pmap_fault(pmap_t, vm_offset_t, uint32_t, int, bool);

void pmap_set_tex(void);

/*
 * Pre-bootstrap epoch functions set.
 */
void pmap_bootstrap_prepare(vm_paddr_t);
vm_paddr_t pmap_preboot_get_pages(u_int);
void pmap_preboot_map_pages(vm_paddr_t, vm_offset_t, u_int);
vm_offset_t pmap_preboot_reserve_pages(u_int);
vm_offset_t pmap_preboot_get_vpages(u_int);
void pmap_preboot_map_attr(vm_paddr_t, vm_offset_t, vm_size_t, vm_prot_t,
    vm_memattr_t);
void pmap_remap_vm_attr(vm_memattr_t old_attr, vm_memattr_t new_attr);

#endif	/* _KERNEL */
#endif	/* !_MACHINE_PMAP_V6_H_ */
