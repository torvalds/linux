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
 * $FreeBSD$
 */

#ifndef _MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#include <machine/pte.h>

#ifndef LOCORE

#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>

#include <vm/_vm_radix.h>

#ifdef _KERNEL

#define	vtophys(va)	pmap_kextract((vm_offset_t)(va))

#endif

#define	pmap_page_get_memattr(m)	((m)->md.pv_memattr)
#define	pmap_page_is_write_mapped(m)	(((m)->aflags & PGA_WRITEABLE) != 0)
void pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma);

/*
 * Pmap stuff
 */

struct md_page {
	TAILQ_HEAD(,pv_entry)	pv_list;
	int			pv_gen;
	vm_memattr_t		pv_memattr;
};

/*
 * This structure is used to hold a virtual<->physical address
 * association and is used mostly by bootstrap code
 */
struct pv_addr {
	SLIST_ENTRY(pv_addr) pv_list;
	vm_offset_t	pv_va;
	vm_paddr_t	pv_pa;
};


struct pmap {
	struct mtx		pm_mtx;
	struct pmap_statistics	pm_stats;	/* pmap statictics */
	pd_entry_t		*pm_l0;
	TAILQ_HEAD(,pv_chunk)	pm_pvchunk;	/* list of mappings in pmap */
	struct vm_radix		pm_root;	/* spare page table pages */
};
typedef struct pmap *pmap_t;

typedef struct pv_entry {
	vm_offset_t		pv_va;	/* virtual address for mapping */
	TAILQ_ENTRY(pv_entry)	pv_next;
} *pv_entry_t;

/*
 * pv_entries are allocated in chunks per-process.  This avoids the
 * need to track per-pmap assignments.
 */
#define	_NPCM	3
#define	_NPCPV	168
#define	PV_CHUNK_HEADER							\
	pmap_t			pc_pmap;				\
	TAILQ_ENTRY(pv_chunk)	pc_list;				\
	uint64_t		pc_map[_NPCM];	/* bitmap; 1 = free */	\
	TAILQ_ENTRY(pv_chunk)	pc_lru;

struct pv_chunk_header {
	PV_CHUNK_HEADER
};

struct pv_chunk {
	PV_CHUNK_HEADER
	struct pv_entry		pc_pventry[_NPCPV];
};

struct thread;

#ifdef _KERNEL
extern struct pmap	kernel_pmap_store;
#define	kernel_pmap	(&kernel_pmap_store)
#define	pmap_kernel()	kernel_pmap

#define	PMAP_ASSERT_LOCKED(pmap) \
				mtx_assert(&(pmap)->pm_mtx, MA_OWNED)
#define	PMAP_LOCK(pmap)		mtx_lock(&(pmap)->pm_mtx)
#define	PMAP_LOCK_ASSERT(pmap, type) \
				mtx_assert(&(pmap)->pm_mtx, (type))
#define	PMAP_LOCK_DESTROY(pmap)	mtx_destroy(&(pmap)->pm_mtx)
#define	PMAP_LOCK_INIT(pmap)	mtx_init(&(pmap)->pm_mtx, "pmap", \
				    NULL, MTX_DEF | MTX_DUPOK)
#define	PMAP_OWNED(pmap)	mtx_owned(&(pmap)->pm_mtx)
#define	PMAP_MTX(pmap)		(&(pmap)->pm_mtx)
#define	PMAP_TRYLOCK(pmap)	mtx_trylock(&(pmap)->pm_mtx)
#define	PMAP_UNLOCK(pmap)	mtx_unlock(&(pmap)->pm_mtx)

#define	PHYS_AVAIL_SIZE	32
extern vm_paddr_t phys_avail[];
extern vm_paddr_t dump_avail[];
extern vm_offset_t virtual_avail;
extern vm_offset_t virtual_end;

/*
 * Macros to test if a mapping is mappable with an L1 Section mapping
 * or an L2 Large Page mapping.
 */
#define	L1_MAPPABLE_P(va, pa, size)					\
	((((va) | (pa)) & L1_OFFSET) == 0 && (size) >= L1_SIZE)

void	pmap_bootstrap(vm_offset_t, vm_offset_t, vm_paddr_t, vm_size_t);
void	pmap_kenter(vm_offset_t sva, vm_size_t size, vm_paddr_t pa, int mode);
void	pmap_kenter_device(vm_offset_t, vm_size_t, vm_paddr_t);
vm_paddr_t pmap_kextract(vm_offset_t va);
void	pmap_kremove(vm_offset_t);
void	pmap_kremove_device(vm_offset_t, vm_size_t);
void	*pmap_mapdev_attr(vm_offset_t pa, vm_size_t size, vm_memattr_t ma);
bool	pmap_ps_enabled(pmap_t pmap);

void	*pmap_mapdev(vm_offset_t, vm_size_t);
void	*pmap_mapbios(vm_paddr_t, vm_size_t);
void	pmap_unmapdev(vm_offset_t, vm_size_t);
void	pmap_unmapbios(vm_offset_t, vm_size_t);

boolean_t pmap_map_io_transient(vm_page_t *, vm_offset_t *, int, boolean_t);
void	pmap_unmap_io_transient(vm_page_t *, vm_offset_t *, int, boolean_t);

bool	pmap_get_tables(pmap_t, vm_offset_t, pd_entry_t **, pd_entry_t **,
    pd_entry_t **, pt_entry_t **);

int	pmap_fault(pmap_t, uint64_t, uint64_t);

struct pcb *pmap_switch(struct thread *, struct thread *);

#define	pmap_page_is_mapped(m)	(!TAILQ_EMPTY(&(m)->md.pv_list))

static inline int
pmap_vmspace_copy(pmap_t dst_pmap __unused, pmap_t src_pmap __unused)
{

	return (0);
}

#endif	/* _KERNEL */

#endif	/* !LOCORE */

#endif	/* !_MACHINE_PMAP_H_ */
