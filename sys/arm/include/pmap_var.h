/*-
 * Copyright 2014 Svatopluk Kraus <onwahe@gmail.com>
 * Copyright 2014 Michal Meloun <meloun@miracle.cz>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _MACHINE_PMAP_VAR_H_
#define _MACHINE_PMAP_VAR_H_

#include <machine/cpu-v6.h>
#include <machine/pte-v6.h>
/*
 *  Various PMAP defines, exports, and inline functions
 *  definitions also usable in other MD code.
 */

/*  A number of pages in L1 page table. */
#define NPG_IN_PT1	(NB_IN_PT1 / PAGE_SIZE)

/*  A number of L2 page tables in a page. */
#define NPT2_IN_PG	(PAGE_SIZE / NB_IN_PT2)

/*  A number of L2 page table entries in a page. */
#define NPTE2_IN_PG	(NPT2_IN_PG * NPTE2_IN_PT2)

#ifdef _KERNEL

/*
 *  A L2 page tables page contains NPT2_IN_PG L2 page tables. Masking of
 *  pte1_idx by PT2PG_MASK gives us an index to associated L2 page table
 *  in a page. The PT2PG_SHIFT definition depends on NPT2_IN_PG strictly.
 *  I.e., (1 << PT2PG_SHIFT) == NPT2_IN_PG must be fulfilled.
 */
#define PT2PG_SHIFT	2
#define PT2PG_MASK	((1 << PT2PG_SHIFT) - 1)

/*
 *  A PT2TAB holds all allocated L2 page table pages in a pmap.
 *  Right shifting of virtual address by PT2TAB_SHIFT gives us an index
 *  to L2 page table page in PT2TAB which holds the address mapping.
 */
#define PT2TAB_ENTRIES  (NPTE1_IN_PT1 / NPT2_IN_PG)
#define PT2TAB_SHIFT	(PTE1_SHIFT + PT2PG_SHIFT)

/*
 *  All allocated L2 page table pages in a pmap are mapped into PT2MAP space.
 *  An virtual address right shifting by PT2MAP_SHIFT gives us an index to PTE2
 *  which maps the address.
 */
#define PT2MAP_SIZE	(NPTE1_IN_PT1 * NB_IN_PT2)
#define PT2MAP_SHIFT	PTE2_SHIFT

extern pt1_entry_t *kern_pt1;
extern pt2_entry_t *kern_pt2tab;
extern pt2_entry_t *PT2MAP;

/*
 *  Virtual interface for L1 page table management.
 */

static __inline u_int
pte1_index(vm_offset_t va)
{

	return (va >> PTE1_SHIFT);
}

static __inline pt1_entry_t *
pte1_ptr(pt1_entry_t *pt1, vm_offset_t va)
{

	return (pt1 + pte1_index(va));
}

static __inline vm_offset_t
pte1_trunc(vm_offset_t va)
{

	return (va & PTE1_FRAME);
}

static __inline vm_offset_t
pte1_roundup(vm_offset_t va)
{

	return ((va + PTE1_OFFSET) & PTE1_FRAME);
}

/*
 *  Virtual interface for L1 page table entries management.
 *
 *  XXX: Some of the following functions now with a synchronization barrier
 *  are called in a loop, so it could be useful to have two versions of them.
 *  One with the barrier and one without the barrier. In this case, pure
 *  barrier pte1_sync() should be implemented as well.
 */
static __inline void
pte1_sync(pt1_entry_t *pte1p)
{

	dsb();
#ifndef PMAP_PTE_NOCACHE
	if (!cpuinfo.coherent_walk)
		dcache_wb_pou((vm_offset_t)pte1p, sizeof(*pte1p));
#endif
}

static __inline void
pte1_sync_range(pt1_entry_t *pte1p, vm_size_t size)
{

	dsb();
#ifndef PMAP_PTE_NOCACHE
	if (!cpuinfo.coherent_walk)
		dcache_wb_pou((vm_offset_t)pte1p, size);
#endif
}

static __inline void
pte1_store(pt1_entry_t *pte1p, pt1_entry_t pte1)
{

	dmb();
	*pte1p = pte1;
	pte1_sync(pte1p);
}

static __inline void
pte1_clear(pt1_entry_t *pte1p)
{

	pte1_store(pte1p, 0);
}

static __inline void
pte1_clear_bit(pt1_entry_t *pte1p, uint32_t bit)
{

	*pte1p &= ~bit;
	pte1_sync(pte1p);
}

static __inline boolean_t
pte1_is_link(pt1_entry_t pte1)
{

	return ((pte1 & L1_TYPE_MASK) == L1_TYPE_C);
}

static __inline int
pte1_is_section(pt1_entry_t pte1)
{

	return ((pte1 & L1_TYPE_MASK) == L1_TYPE_S);
}

static __inline boolean_t
pte1_is_dirty(pt1_entry_t pte1)
{

	return ((pte1 & (PTE1_NM | PTE1_RO)) == 0);
}

static __inline boolean_t
pte1_is_global(pt1_entry_t pte1)
{

	return ((pte1 & PTE1_NG) == 0);
}

static __inline boolean_t
pte1_is_valid(pt1_entry_t pte1)
{
	int l1_type;

	l1_type = pte1 & L1_TYPE_MASK;
	return ((l1_type == L1_TYPE_C) || (l1_type == L1_TYPE_S));
}

static __inline boolean_t
pte1_is_wired(pt1_entry_t pte1)
{

	return (pte1 & PTE1_W);
}

static __inline pt1_entry_t
pte1_load(pt1_entry_t *pte1p)
{
	pt1_entry_t pte1;

	pte1 = *pte1p;
	return (pte1);
}

static __inline pt1_entry_t
pte1_load_clear(pt1_entry_t *pte1p)
{
	pt1_entry_t opte1;

	opte1 = *pte1p;
	*pte1p = 0;
	pte1_sync(pte1p);
	return (opte1);
}

static __inline void
pte1_set_bit(pt1_entry_t *pte1p, uint32_t bit)
{

	*pte1p |= bit;
	pte1_sync(pte1p);
}

static __inline vm_paddr_t
pte1_pa(pt1_entry_t pte1)
{

	return ((vm_paddr_t)(pte1 & PTE1_FRAME));
}

static __inline vm_paddr_t
pte1_link_pa(pt1_entry_t pte1)
{

	return ((vm_paddr_t)(pte1 & L1_C_ADDR_MASK));
}

/*
 *  Virtual interface for L2 page table entries management.
 *
 *  XXX: Some of the following functions now with a synchronization barrier
 *  are called in a loop, so it could be useful to have two versions of them.
 *  One with the barrier and one without the barrier.
 */

static __inline void
pte2_sync(pt2_entry_t *pte2p)
{

	dsb();
#ifndef PMAP_PTE_NOCACHE
	if (!cpuinfo.coherent_walk)
		dcache_wb_pou((vm_offset_t)pte2p, sizeof(*pte2p));
#endif
}

static __inline void
pte2_sync_range(pt2_entry_t *pte2p, vm_size_t size)
{

	dsb();
#ifndef PMAP_PTE_NOCACHE
	if (!cpuinfo.coherent_walk)
		dcache_wb_pou((vm_offset_t)pte2p, size);
#endif
}

static __inline void
pte2_store(pt2_entry_t *pte2p, pt2_entry_t pte2)
{

	dmb();
	*pte2p = pte2;
	pte2_sync(pte2p);
}

static __inline void
pte2_clear(pt2_entry_t *pte2p)
{

	pte2_store(pte2p, 0);
}

static __inline void
pte2_clear_bit(pt2_entry_t *pte2p, uint32_t bit)
{

	*pte2p &= ~bit;
	pte2_sync(pte2p);
}

static __inline boolean_t
pte2_is_dirty(pt2_entry_t pte2)
{

	return ((pte2 & (PTE2_NM | PTE2_RO)) == 0);
}

static __inline boolean_t
pte2_is_global(pt2_entry_t pte2)
{

	return ((pte2 & PTE2_NG) == 0);
}

static __inline boolean_t
pte2_is_valid(pt2_entry_t pte2)
{

	return (pte2 & PTE2_V);
}

static __inline boolean_t
pte2_is_wired(pt2_entry_t pte2)
{

	return (pte2 & PTE2_W);
}

static __inline pt2_entry_t
pte2_load(pt2_entry_t *pte2p)
{
	pt2_entry_t pte2;

	pte2 = *pte2p;
	return (pte2);
}

static __inline pt2_entry_t
pte2_load_clear(pt2_entry_t *pte2p)
{
	pt2_entry_t opte2;

	opte2 = *pte2p;
	*pte2p = 0;
	pte2_sync(pte2p);
	return (opte2);
}

static __inline void
pte2_set_bit(pt2_entry_t *pte2p, uint32_t bit)
{

	*pte2p |= bit;
	pte2_sync(pte2p);
}

static __inline void
pte2_set_wired(pt2_entry_t *pte2p, boolean_t wired)
{

	/*
	 * Wired bit is transparent for page table walk,
	 * so pte2_sync() is not needed.
	 */
	if (wired)
		*pte2p |= PTE2_W;
	else
		*pte2p &= ~PTE2_W;
}

static __inline vm_paddr_t
pte2_pa(pt2_entry_t pte2)
{

	return ((vm_paddr_t)(pte2 & PTE2_FRAME));
}

static __inline u_int
pte2_attr(pt2_entry_t pte2)
{

	return ((u_int)(pte2 & PTE2_ATTR_MASK));
}

/*
 *  Virtual interface for L2 page tables mapping management.
 */

static __inline u_int
pt2tab_index(vm_offset_t va)
{

	return (va >> PT2TAB_SHIFT);
}

static __inline pt2_entry_t *
pt2tab_entry(pt2_entry_t *pt2tab, vm_offset_t va)
{

	return (pt2tab + pt2tab_index(va));
}

static __inline void
pt2tab_store(pt2_entry_t *pte2p, pt2_entry_t pte2)
{

	pte2_store(pte2p,pte2);
}

static __inline pt2_entry_t
pt2tab_load(pt2_entry_t *pte2p)
{

	return (pte2_load(pte2p));
}

static __inline pt2_entry_t
pt2tab_load_clear(pt2_entry_t *pte2p)
{

	return (pte2_load_clear(pte2p));
}

static __inline u_int
pt2map_index(vm_offset_t va)
{

	return (va >> PT2MAP_SHIFT);
}

static __inline pt2_entry_t *
pt2map_entry(vm_offset_t va)
{

	return (PT2MAP + pt2map_index(va));
}

/*
 *  Virtual interface for pmap structure & kernel shortcuts.
 */

static __inline pt1_entry_t *
pmap_pte1(pmap_t pmap, vm_offset_t va)
{

	return (pte1_ptr(pmap->pm_pt1, va));
}

static __inline pt1_entry_t *
kern_pte1(vm_offset_t va)
{

	return (pte1_ptr(kern_pt1, va));
}

static __inline pt2_entry_t *
pmap_pt2tab_entry(pmap_t pmap, vm_offset_t va)
{

	return (pt2tab_entry(pmap->pm_pt2tab, va));
}

static __inline pt2_entry_t *
kern_pt2tab_entry(vm_offset_t va)
{

	return (pt2tab_entry(kern_pt2tab, va));
}

static __inline vm_page_t
pmap_pt2_page(pmap_t pmap, vm_offset_t va)
{
	pt2_entry_t pte2;

	pte2 = pte2_load(pmap_pt2tab_entry(pmap, va));
	return (PHYS_TO_VM_PAGE(pte2 & PTE2_FRAME));
}

static __inline vm_page_t
kern_pt2_page(vm_offset_t va)
{
	pt2_entry_t pte2;

	pte2 = pte2_load(kern_pt2tab_entry(va));
	return (PHYS_TO_VM_PAGE(pte2 & PTE2_FRAME));
}

#endif	/* _KERNEL */
#endif	/* !_MACHINE_PMAP_VAR_H_ */
