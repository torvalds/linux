/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * Derived from hp300 version by Mike Hibler, this version by William
 * Jolitz uses a recursive map [a pde points to the page directory] to
 * map the page tables using the pagetables themselves. This is done to
 * reduce the impact on kernel virtual memory for lots of sparse address
 * space, and to reduce the cost of memory to each process.
 *
 *	from: hp300: @(#)pmap.h 7.2 (Berkeley) 12/16/90
 *	from: @(#)pmap.h	7.4 (Berkeley) 5/12/91
 *	from: src/sys/i386/include/pmap.h,v 1.65.2.2 2000/11/30 01:54:42 peter
 *	JNPR: pmap.h,v 1.7.2.1 2007/09/10 07:44:12 girish
 *      $FreeBSD$
 */

#ifndef _MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#include <machine/vmparam.h>
#include <machine/pte.h>

#if defined(__mips_n32) || defined(__mips_n64) /* PHYSADDR_64BIT */
#define	NKPT		256	/* mem > 4G, vm_page_startup needs more KPTs */
#else
#define	NKPT		120	/* actual number of kernel page tables */
#endif

#ifndef LOCORE

#include <sys/queue.h>
#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>

/*
 * Pmap stuff
 */
struct pv_entry;
struct pv_chunk;

struct md_page {
	int pv_flags;
	TAILQ_HEAD(, pv_entry) pv_list;
};

#define	PV_TABLE_REF		0x02	/* referenced */
#define	PV_MEMATTR_MASK		0xf0	/* store vm_memattr_t here */
#define	PV_MEMATTR_SHIFT	0x04

#define	ASID_BITS		8
#define	ASIDGEN_BITS		(32 - ASID_BITS)
#define	ASIDGEN_MASK		((1 << ASIDGEN_BITS) - 1)

struct pmap {
	pd_entry_t *pm_segtab;	/* KVA of segment table */
	TAILQ_HEAD(, pv_chunk)	pm_pvchunk;	/* list of mappings in pmap */
	cpuset_t	pm_active;		/* active on cpus */
	struct {
		u_int32_t asid:ASID_BITS;	/* TLB address space tag */
		u_int32_t gen:ASIDGEN_BITS;	/* its generation number */
	}      pm_asid[MAXSMPCPU];
	struct pmap_statistics pm_stats;	/* pmap statistics */
	struct mtx pm_mtx;
};

typedef struct pmap *pmap_t;

#ifdef	_KERNEL

pt_entry_t *pmap_pte(pmap_t, vm_offset_t);
vm_paddr_t pmap_kextract(vm_offset_t va);

#define	vtophys(va)	pmap_kextract(((vm_offset_t) (va)))
#define	pmap_asid(pmap)	(pmap)->pm_asid[PCPU_GET(cpuid)].asid

extern struct pmap	kernel_pmap_store;
#define kernel_pmap	(&kernel_pmap_store)

#define	PMAP_LOCK(pmap)		mtx_lock(&(pmap)->pm_mtx)
#define	PMAP_LOCK_ASSERT(pmap, type)	mtx_assert(&(pmap)->pm_mtx, (type))
#define	PMAP_LOCK_DESTROY(pmap) mtx_destroy(&(pmap)->pm_mtx)
#define	PMAP_LOCK_INIT(pmap)	mtx_init(&(pmap)->pm_mtx, "pmap", \
				    NULL, MTX_DEF)
#define	PMAP_LOCKED(pmap)	mtx_owned(&(pmap)->pm_mtx)
#define	PMAP_MTX(pmap)		(&(pmap)->pm_mtx)
#define	PMAP_TRYLOCK(pmap)	mtx_trylock(&(pmap)->pm_mtx)
#define	PMAP_UNLOCK(pmap)	mtx_unlock(&(pmap)->pm_mtx)

/*
 * For each vm_page_t, there is a list of all currently valid virtual
 * mappings of that page.  An entry is a pv_entry_t, the list is pv_table.
 */
typedef struct pv_entry {
	vm_offset_t pv_va;	/* virtual address for mapping */
	TAILQ_ENTRY(pv_entry) pv_list;
}       *pv_entry_t;

/*
 * pv_entries are allocated in chunks per-process.  This avoids the
 * need to track per-pmap assignments.
 */
#ifdef __mips_n64
#define	_NPCM	3
#define	_NPCPV	168
#else
#define	_NPCM	11
#define	_NPCPV	336
#endif
struct pv_chunk {
	pmap_t			pc_pmap;
	TAILQ_ENTRY(pv_chunk)	pc_list;
	u_long			pc_map[_NPCM];	/* bitmap; 1 = free */
	TAILQ_ENTRY(pv_chunk)	pc_lru;
	struct pv_entry		pc_pventry[_NPCPV];
};

/*
 * physmem_desc[] is a superset of phys_avail[] and describes all the
 * memory present in the system.
 *
 * phys_avail[] is similar but does not include the memory stolen by
 * pmap_steal_memory().
 *
 * Each memory region is described by a pair of elements in the array
 * so we can describe up to (PHYS_AVAIL_ENTRIES / 2) distinct memory
 * regions.
 */
#define	PHYS_AVAIL_ENTRIES	10
extern vm_paddr_t phys_avail[PHYS_AVAIL_ENTRIES + 2];
extern vm_paddr_t physmem_desc[PHYS_AVAIL_ENTRIES + 2];

extern vm_offset_t virtual_avail;
extern vm_offset_t virtual_end;

extern vm_paddr_t dump_avail[PHYS_AVAIL_ENTRIES + 2];

#define	pmap_page_get_memattr(m) (((m)->md.pv_flags & PV_MEMATTR_MASK) >> PV_MEMATTR_SHIFT)
#define	pmap_page_is_mapped(m)	(!TAILQ_EMPTY(&(m)->md.pv_list))
#define	pmap_page_is_write_mapped(m)	(((m)->aflags & PGA_WRITEABLE) != 0)

void pmap_bootstrap(void);
void *pmap_mapdev(vm_paddr_t, vm_size_t);
void *pmap_mapdev_attr(vm_paddr_t, vm_size_t, vm_memattr_t);
void pmap_unmapdev(vm_offset_t, vm_size_t);
vm_offset_t pmap_steal_memory(vm_size_t size);
void pmap_kenter(vm_offset_t va, vm_paddr_t pa);
void pmap_kenter_attr(vm_offset_t va, vm_paddr_t pa, vm_memattr_t attr);
void pmap_kremove(vm_offset_t va);
void *pmap_kenter_temporary(vm_paddr_t pa, int i);
void pmap_kenter_temporary_free(vm_paddr_t pa);
void pmap_flush_pvcache(vm_page_t m);
int pmap_emulate_modified(pmap_t pmap, vm_offset_t va);
void pmap_page_set_memattr(vm_page_t, vm_memattr_t);
int pmap_change_attr(vm_offset_t, vm_size_t, vm_memattr_t);

static inline int
pmap_vmspace_copy(pmap_t dst_pmap __unused, pmap_t src_pmap __unused)
{

	return (0);
}

#endif				/* _KERNEL */

#endif				/* !LOCORE */

#endif				/* !_MACHINE_PMAP_H_ */
