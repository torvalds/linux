/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Peter Wemm.
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
 *	from: hp300: @(#)pmap.h	7.2 (Berkeley) 12/16/90
 *	from: @(#)pmap.h	7.4 (Berkeley) 5/12/91
 * $FreeBSD$
 */

#ifndef _MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

/*
 * Page-directory and page-table entries follow this format, with a few
 * of the fields not present here and there, depending on a lot of things.
 */
				/* ---- Intel Nomenclature ---- */
#define	X86_PG_V	0x001	/* P	Valid			*/
#define	X86_PG_RW	0x002	/* R/W	Read/Write		*/
#define	X86_PG_U	0x004	/* U/S  User/Supervisor		*/
#define	X86_PG_NC_PWT	0x008	/* PWT	Write through		*/
#define	X86_PG_NC_PCD	0x010	/* PCD	Cache disable		*/
#define	X86_PG_A	0x020	/* A	Accessed		*/
#define	X86_PG_M	0x040	/* D	Dirty			*/
#define	X86_PG_PS	0x080	/* PS	Page size (0=4k,1=2M)	*/
#define	X86_PG_PTE_PAT	0x080	/* PAT	PAT index		*/
#define	X86_PG_G	0x100	/* G	Global			*/
#define	X86_PG_AVAIL1	0x200	/*    /	Available for system	*/
#define	X86_PG_AVAIL2	0x400	/*   <	programmers use		*/
#define	X86_PG_AVAIL3	0x800	/*    \				*/
#define	X86_PG_PDE_PAT	0x1000	/* PAT	PAT index		*/
#define	X86_PG_PKU(idx)	((pt_entry_t)idx << 59)
#define	X86_PG_NX	(1ul<<63) /* No-execute */
#define	X86_PG_AVAIL(x)	(1ul << (x))

/* Page level cache control fields used to determine the PAT type */
#define	X86_PG_PDE_CACHE (X86_PG_PDE_PAT | X86_PG_NC_PWT | X86_PG_NC_PCD)
#define	X86_PG_PTE_CACHE (X86_PG_PTE_PAT | X86_PG_NC_PWT | X86_PG_NC_PCD)

/* Protection keys indexes */
#define	PMAP_MAX_PKRU_IDX	0xf
#define	X86_PG_PKU_MASK		X86_PG_PKU(PMAP_MAX_PKRU_IDX)

/*
 * Intel extended page table (EPT) bit definitions.
 */
#define	EPT_PG_READ		0x001	/* R	Read		*/
#define	EPT_PG_WRITE		0x002	/* W	Write		*/
#define	EPT_PG_EXECUTE		0x004	/* X	Execute		*/
#define	EPT_PG_IGNORE_PAT	0x040	/* IPAT	Ignore PAT	*/
#define	EPT_PG_PS		0x080	/* PS	Page size	*/
#define	EPT_PG_A		0x100	/* A	Accessed	*/
#define	EPT_PG_M		0x200	/* D	Dirty		*/
#define	EPT_PG_MEMORY_TYPE(x)	((x) << 3) /* MT Memory Type	*/

/*
 * Define the PG_xx macros in terms of the bits on x86 PTEs.
 */
#define	PG_V		X86_PG_V
#define	PG_RW		X86_PG_RW
#define	PG_U		X86_PG_U
#define	PG_NC_PWT	X86_PG_NC_PWT
#define	PG_NC_PCD	X86_PG_NC_PCD
#define	PG_A		X86_PG_A
#define	PG_M		X86_PG_M
#define	PG_PS		X86_PG_PS
#define	PG_PTE_PAT	X86_PG_PTE_PAT
#define	PG_G		X86_PG_G
#define	PG_AVAIL1	X86_PG_AVAIL1
#define	PG_AVAIL2	X86_PG_AVAIL2
#define	PG_AVAIL3	X86_PG_AVAIL3
#define	PG_PDE_PAT	X86_PG_PDE_PAT
#define	PG_NX		X86_PG_NX
#define	PG_PDE_CACHE	X86_PG_PDE_CACHE
#define	PG_PTE_CACHE	X86_PG_PTE_CACHE

/* Our various interpretations of the above */
#define	PG_W		X86_PG_AVAIL3	/* "Wired" pseudoflag */
#define	PG_MANAGED	X86_PG_AVAIL2
#define	EPT_PG_EMUL_V	X86_PG_AVAIL(52)
#define	EPT_PG_EMUL_RW	X86_PG_AVAIL(53)
#define	PG_PROMOTED	X86_PG_AVAIL(54)	/* PDE only */
#define	PG_FRAME	(0x000ffffffffff000ul)
#define	PG_PS_FRAME	(0x000fffffffe00000ul)

/*
 * Promotion to a 2MB (PDE) page mapping requires that the corresponding 4KB
 * (PTE) page mappings have identical settings for the following fields:
 */
#define	PG_PTE_PROMOTE	(PG_NX | PG_MANAGED | PG_W | PG_G | PG_PTE_CACHE | \
	    PG_M | PG_A | PG_U | PG_RW | PG_V | PG_PKU_MASK)

/*
 * Page Protection Exception bits
 */

#define PGEX_P		0x01	/* Protection violation vs. not present */
#define PGEX_W		0x02	/* during a Write cycle */
#define PGEX_U		0x04	/* access from User mode (UPL) */
#define PGEX_RSV	0x08	/* reserved PTE field is non-zero */
#define PGEX_I		0x10	/* during an instruction fetch */
#define	PGEX_PK		0x20	/* protection key violation */
#define	PGEX_SGX	0x40	/* SGX-related */

/* 
 * undef the PG_xx macros that define bits in the regular x86 PTEs that
 * have a different position in nested PTEs. This is done when compiling
 * code that needs to be aware of the differences between regular x86 and
 * nested PTEs.
 *
 * The appropriate bitmask will be calculated at runtime based on the pmap
 * type.
 */
#ifdef AMD64_NPT_AWARE
#undef PG_AVAIL1		/* X86_PG_AVAIL1 aliases with EPT_PG_M */
#undef PG_G
#undef PG_A
#undef PG_M
#undef PG_PDE_PAT
#undef PG_PDE_CACHE
#undef PG_PTE_PAT
#undef PG_PTE_CACHE
#undef PG_RW
#undef PG_V
#endif

/*
 * Pte related macros.  This is complicated by having to deal with
 * the sign extension of the 48th bit.
 */
#define KVADDR(l4, l3, l2, l1) ( \
	((unsigned long)-1 << 47) | \
	((unsigned long)(l4) << PML4SHIFT) | \
	((unsigned long)(l3) << PDPSHIFT) | \
	((unsigned long)(l2) << PDRSHIFT) | \
	((unsigned long)(l1) << PAGE_SHIFT))

#define UVADDR(l4, l3, l2, l1) ( \
	((unsigned long)(l4) << PML4SHIFT) | \
	((unsigned long)(l3) << PDPSHIFT) | \
	((unsigned long)(l2) << PDRSHIFT) | \
	((unsigned long)(l1) << PAGE_SHIFT))

/*
 * Number of kernel PML4 slots.  Can be anywhere from 1 to 64 or so,
 * but setting it larger than NDMPML4E makes no sense.
 *
 * Each slot provides .5 TB of kernel virtual space.
 */
#define NKPML4E		4

#define	NUPML4E		(NPML4EPG/2)	/* number of userland PML4 pages */
#define	NUPDPE		(NUPML4E*NPDPEPG)/* number of userland PDP pages */
#define	NUPDE		(NUPDPE*NPDEPG)	/* number of userland PD entries */

/*
 * NDMPML4E is the maximum number of PML4 entries that will be
 * used to implement the direct map.  It must be a power of two,
 * and should generally exceed NKPML4E.  The maximum possible
 * value is 64; using 128 will make the direct map intrude into
 * the recursive page table map.
 */
#define	NDMPML4E	8

/*
 * These values control the layout of virtual memory.  The starting address
 * of the direct map, which is controlled by DMPML4I, must be a multiple of
 * its size.  (See the PHYS_TO_DMAP() and DMAP_TO_PHYS() macros.)
 *
 * Note: KPML4I is the index of the (single) level 4 page that maps
 * the KVA that holds KERNBASE, while KPML4BASE is the index of the
 * first level 4 page that maps VM_MIN_KERNEL_ADDRESS.  If NKPML4E
 * is 1, these are the same, otherwise KPML4BASE < KPML4I and extra
 * level 4 PDEs are needed to map from VM_MIN_KERNEL_ADDRESS up to
 * KERNBASE.
 *
 * (KPML4I combines with KPDPI to choose where KERNBASE starts.
 * Or, in other words, KPML4I provides bits 39..47 of KERNBASE,
 * and KPDPI provides bits 30..38.)
 */
#define	PML4PML4I	(NPML4EPG/2)	/* Index of recursive pml4 mapping */

#define	KPML4BASE	(NPML4EPG-NKPML4E) /* KVM at highest addresses */
#define	DMPML4I		rounddown(KPML4BASE-NDMPML4E, NDMPML4E) /* Below KVM */

#define	KPML4I		(NPML4EPG-1)
#define	KPDPI		(NPDPEPG-2)	/* kernbase at -2GB */

/* Large map: index of the first and max last pml4 entry */
#define	LMSPML4I	(PML4PML4I + 1)
#define	LMEPML4I	(DMPML4I - 1)

/*
 * XXX doesn't really belong here I guess...
 */
#define ISA_HOLE_START    0xa0000
#define ISA_HOLE_LENGTH (0x100000-ISA_HOLE_START)

#define	PMAP_PCID_NONE		0xffffffff
#define	PMAP_PCID_KERN		0
#define	PMAP_PCID_OVERMAX	0x1000
#define	PMAP_PCID_OVERMAX_KERN	0x800
#define	PMAP_PCID_USER_PT	0x800

#define	PMAP_NO_CR3		(~0UL)

#ifndef LOCORE

#include <sys/queue.h>
#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_pctrie.h>
#include <sys/_rangeset.h>

#include <vm/_vm_radix.h>

typedef u_int64_t pd_entry_t;
typedef u_int64_t pt_entry_t;
typedef u_int64_t pdp_entry_t;
typedef u_int64_t pml4_entry_t;

/*
 * Address of current address space page table maps and directories.
 */
#ifdef _KERNEL
#define	addr_PTmap	(KVADDR(PML4PML4I, 0, 0, 0))
#define	addr_PDmap	(KVADDR(PML4PML4I, PML4PML4I, 0, 0))
#define	addr_PDPmap	(KVADDR(PML4PML4I, PML4PML4I, PML4PML4I, 0))
#define	addr_PML4map	(KVADDR(PML4PML4I, PML4PML4I, PML4PML4I, PML4PML4I))
#define	addr_PML4pml4e	(addr_PML4map + (PML4PML4I * sizeof(pml4_entry_t)))
#define	PTmap		((pt_entry_t *)(addr_PTmap))
#define	PDmap		((pd_entry_t *)(addr_PDmap))
#define	PDPmap		((pd_entry_t *)(addr_PDPmap))
#define	PML4map		((pd_entry_t *)(addr_PML4map))
#define	PML4pml4e	((pd_entry_t *)(addr_PML4pml4e))

extern int nkpt;		/* Initial number of kernel page tables */
extern u_int64_t KPDPphys;	/* physical address of kernel level 3 */
extern u_int64_t KPML4phys;	/* physical address of kernel level 4 */

/*
 * virtual address to page table entry and
 * to physical address.
 * Note: these work recursively, thus vtopte of a pte will give
 * the corresponding pde that in turn maps it.
 */
pt_entry_t *vtopte(vm_offset_t);
#define	vtophys(va)	pmap_kextract(((vm_offset_t) (va)))

#define	pte_load_store(ptep, pte)	atomic_swap_long(ptep, pte)
#define	pte_load_clear(ptep)		atomic_swap_long(ptep, 0)
#define	pte_store(ptep, pte) do { \
	*(u_long *)(ptep) = (u_long)(pte); \
} while (0)
#define	pte_clear(ptep)			pte_store(ptep, 0)

#define	pde_store(pdep, pde)		pte_store(pdep, pde)

extern pt_entry_t pg_nx;

#endif /* _KERNEL */

/*
 * Pmap stuff
 */
struct	pv_entry;
struct	pv_chunk;

/*
 * Locks
 * (p) PV list lock
 */
struct md_page {
	TAILQ_HEAD(, pv_entry)	pv_list;  /* (p) */
	int			pv_gen;   /* (p) */
	int			pat_mode;
};

enum pmap_type {
	PT_X86,			/* regular x86 page tables */
	PT_EPT,			/* Intel's nested page tables */
	PT_RVI,			/* AMD's nested page tables */
};

struct pmap_pcids {
	uint32_t	pm_pcid;
	uint32_t	pm_gen;
};

/*
 * The kernel virtual address (KVA) of the level 4 page table page is always
 * within the direct map (DMAP) region.
 */
struct pmap {
	struct mtx		pm_mtx;
	pml4_entry_t		*pm_pml4;	/* KVA of level 4 page table */
	pml4_entry_t		*pm_pml4u;	/* KVA of user l4 page table */
	uint64_t		pm_cr3;
	uint64_t		pm_ucr3;
	TAILQ_HEAD(,pv_chunk)	pm_pvchunk;	/* list of mappings in pmap */
	cpuset_t		pm_active;	/* active on cpus */
	enum pmap_type		pm_type;	/* regular or nested tables */
	struct pmap_statistics	pm_stats;	/* pmap statistics */
	struct vm_radix		pm_root;	/* spare page table pages */
	long			pm_eptgen;	/* EPT pmap generation id */
	int			pm_flags;
	struct pmap_pcids	pm_pcids[MAXCPU];
	struct rangeset		pm_pkru;
};

/* flags */
#define	PMAP_NESTED_IPIMASK	0xff
#define	PMAP_PDE_SUPERPAGE	(1 << 8)	/* supports 2MB superpages */
#define	PMAP_EMULATE_AD_BITS	(1 << 9)	/* needs A/D bits emulation */
#define	PMAP_SUPPORTS_EXEC_ONLY	(1 << 10)	/* execute only mappings ok */

typedef struct pmap	*pmap_t;

#ifdef _KERNEL
extern struct pmap	kernel_pmap_store;
#define kernel_pmap	(&kernel_pmap_store)

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

int	pmap_pinit_type(pmap_t pmap, enum pmap_type pm_type, int flags);
int	pmap_emulate_accessed_dirty(pmap_t pmap, vm_offset_t va, int ftype);
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

#ifdef	_KERNEL

extern caddr_t	CADDR1;
extern pt_entry_t *CMAP1;
extern vm_paddr_t phys_avail[];
extern vm_paddr_t dump_avail[];
extern vm_offset_t virtual_avail;
extern vm_offset_t virtual_end;
extern vm_paddr_t dmaplimit;
extern int pmap_pcid_enabled;
extern int invpcid_works;

#define	pmap_page_get_memattr(m)	((vm_memattr_t)(m)->md.pat_mode)
#define	pmap_page_is_write_mapped(m)	(((m)->aflags & PGA_WRITEABLE) != 0)
#define	pmap_unmapbios(va, sz)	pmap_unmapdev((va), (sz))

struct thread;

void	pmap_activate_boot(pmap_t pmap);
void	pmap_activate_sw(struct thread *);
void	pmap_bootstrap(vm_paddr_t *);
int	pmap_cache_bits(pmap_t pmap, int mode, boolean_t is_pde);
int	pmap_change_attr(vm_offset_t, vm_size_t, int);
void	pmap_demote_DMAP(vm_paddr_t base, vm_size_t len, boolean_t invalidate);
void	pmap_flush_cache_range(vm_offset_t, vm_offset_t);
void	pmap_flush_cache_phys_range(vm_paddr_t, vm_paddr_t, vm_memattr_t);
void	pmap_init_pat(void);
void	pmap_kenter(vm_offset_t va, vm_paddr_t pa);
void	*pmap_kenter_temporary(vm_paddr_t pa, int i);
vm_paddr_t pmap_kextract(vm_offset_t);
void	pmap_kremove(vm_offset_t);
int	pmap_large_map(vm_paddr_t, vm_size_t, void **, vm_memattr_t);
void	pmap_large_map_wb(void *sva, vm_size_t len);
void	pmap_large_unmap(void *sva, vm_size_t len);
void	*pmap_mapbios(vm_paddr_t, vm_size_t);
void	*pmap_mapdev(vm_paddr_t, vm_size_t);
void	*pmap_mapdev_attr(vm_paddr_t, vm_size_t, int);
void	*pmap_mapdev_pciecfg(vm_paddr_t pa, vm_size_t size);
boolean_t pmap_page_is_mapped(vm_page_t m);
void	pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma);
void	pmap_pinit_pml4(vm_page_t);
bool	pmap_ps_enabled(pmap_t pmap);
void	pmap_unmapdev(vm_offset_t, vm_size_t);
void	pmap_invalidate_page(pmap_t, vm_offset_t);
void	pmap_invalidate_range(pmap_t, vm_offset_t, vm_offset_t);
void	pmap_invalidate_all(pmap_t);
void	pmap_invalidate_cache(void);
void	pmap_invalidate_cache_pages(vm_page_t *pages, int count);
void	pmap_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva);
void	pmap_force_invalidate_cache_range(vm_offset_t sva, vm_offset_t eva);
void	pmap_get_mapping(pmap_t pmap, vm_offset_t va, uint64_t *ptr, int *num);
boolean_t pmap_map_io_transient(vm_page_t *, vm_offset_t *, int, boolean_t);
void	pmap_unmap_io_transient(vm_page_t *, vm_offset_t *, int, boolean_t);
void	pmap_pti_add_kva(vm_offset_t sva, vm_offset_t eva, bool exec);
void	pmap_pti_remove_kva(vm_offset_t sva, vm_offset_t eva);
void	pmap_pti_pcid_invalidate(uint64_t ucr3, uint64_t kcr3);
void	pmap_pti_pcid_invlpg(uint64_t ucr3, uint64_t kcr3, vm_offset_t va);
void	pmap_pti_pcid_invlrng(uint64_t ucr3, uint64_t kcr3, vm_offset_t sva,
	    vm_offset_t eva);
int	pmap_pkru_clear(pmap_t pmap, vm_offset_t sva, vm_offset_t eva);
int	pmap_pkru_set(pmap_t pmap, vm_offset_t sva, vm_offset_t eva,
	    u_int keyidx, int flags);
int	pmap_vmspace_copy(pmap_t dst_pmap, pmap_t src_pmap);
#endif /* _KERNEL */

/* Return various clipped indexes for a given VA */
static __inline vm_pindex_t
pmap_pte_index(vm_offset_t va)
{

	return ((va >> PAGE_SHIFT) & ((1ul << NPTEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pde_index(vm_offset_t va)
{

	return ((va >> PDRSHIFT) & ((1ul << NPDEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pdpe_index(vm_offset_t va)
{

	return ((va >> PDPSHIFT) & ((1ul << NPDPEPGSHIFT) - 1));
}

static __inline vm_pindex_t
pmap_pml4e_index(vm_offset_t va)
{

	return ((va >> PML4SHIFT) & ((1ul << NPML4EPGSHIFT) - 1));
}

#endif /* !LOCORE */

#endif /* !_MACHINE_PMAP_H_ */
