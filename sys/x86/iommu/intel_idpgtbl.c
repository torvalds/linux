/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/memdesc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/rman.h>
#include <sys/sf_buf.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/uio.h>
#include <sys/vmem.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_map.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <x86/include/busdma_impl.h>
#include <x86/iommu/intel_reg.h>
#include <x86/iommu/busdma_dmar.h>
#include <x86/iommu/intel_dmar.h>

static int domain_unmap_buf_locked(struct dmar_domain *domain,
    dmar_gaddr_t base, dmar_gaddr_t size, int flags);

/*
 * The cache of the identity mapping page tables for the DMARs.  Using
 * the cache saves significant amount of memory for page tables by
 * reusing the page tables, since usually DMARs are identical and have
 * the same capabilities.  Still, cache records the information needed
 * to match DMAR capabilities and page table format, to correctly
 * handle different DMARs.
 */

struct idpgtbl {
	dmar_gaddr_t maxaddr;	/* Page table covers the guest address
				   range [0..maxaddr) */
	int pglvl;		/* Total page table levels ignoring
				   superpages */
	int leaf;		/* The last materialized page table
				   level, it is non-zero if superpages
				   are supported */
	vm_object_t pgtbl_obj;	/* The page table pages */
	LIST_ENTRY(idpgtbl) link;
};

static struct sx idpgtbl_lock;
SX_SYSINIT(idpgtbl, &idpgtbl_lock, "idpgtbl");
static LIST_HEAD(, idpgtbl) idpgtbls = LIST_HEAD_INITIALIZER(idpgtbls);
static MALLOC_DEFINE(M_DMAR_IDPGTBL, "dmar_idpgtbl",
    "Intel DMAR Identity mappings cache elements");

/*
 * Build the next level of the page tables for the identity mapping.
 * - lvl is the level to build;
 * - idx is the index of the page table page in the pgtbl_obj, which is
 *   being allocated filled now;
 * - addr is the starting address in the bus address space which is
 *   mapped by the page table page.
 */
static void
domain_idmap_nextlvl(struct idpgtbl *tbl, int lvl, vm_pindex_t idx,
    dmar_gaddr_t addr)
{
	vm_page_t m1;
	dmar_pte_t *pte;
	struct sf_buf *sf;
	dmar_gaddr_t f, pg_sz;
	vm_pindex_t base;
	int i;

	VM_OBJECT_ASSERT_LOCKED(tbl->pgtbl_obj);
	if (addr >= tbl->maxaddr)
		return;
	(void)dmar_pgalloc(tbl->pgtbl_obj, idx, DMAR_PGF_OBJL | DMAR_PGF_WAITOK |
	    DMAR_PGF_ZERO);
	base = idx * DMAR_NPTEPG + 1; /* Index of the first child page of idx */
	pg_sz = pglvl_page_size(tbl->pglvl, lvl);
	if (lvl != tbl->leaf) {
		for (i = 0, f = addr; i < DMAR_NPTEPG; i++, f += pg_sz)
			domain_idmap_nextlvl(tbl, lvl + 1, base + i, f);
	}
	VM_OBJECT_WUNLOCK(tbl->pgtbl_obj);
	pte = dmar_map_pgtbl(tbl->pgtbl_obj, idx, DMAR_PGF_WAITOK, &sf);
	if (lvl == tbl->leaf) {
		for (i = 0, f = addr; i < DMAR_NPTEPG; i++, f += pg_sz) {
			if (f >= tbl->maxaddr)
				break;
			pte[i].pte = (DMAR_PTE_ADDR_MASK & f) |
			    DMAR_PTE_R | DMAR_PTE_W;
		}
	} else {
		for (i = 0, f = addr; i < DMAR_NPTEPG; i++, f += pg_sz) {
			if (f >= tbl->maxaddr)
				break;
			m1 = dmar_pgalloc(tbl->pgtbl_obj, base + i,
			    DMAR_PGF_NOALLOC);
			KASSERT(m1 != NULL, ("lost page table page"));
			pte[i].pte = (DMAR_PTE_ADDR_MASK &
			    VM_PAGE_TO_PHYS(m1)) | DMAR_PTE_R | DMAR_PTE_W;
		}
	}
	/* domain_get_idmap_pgtbl flushes CPU cache if needed. */
	dmar_unmap_pgtbl(sf);
	VM_OBJECT_WLOCK(tbl->pgtbl_obj);
}

/*
 * Find a ready and compatible identity-mapping page table in the
 * cache. If not found, populate the identity-mapping page table for
 * the context, up to the maxaddr. The maxaddr byte is allowed to be
 * not mapped, which is aligned with the definition of Maxmem as the
 * highest usable physical address + 1.  If superpages are used, the
 * maxaddr is typically mapped.
 */
vm_object_t
domain_get_idmap_pgtbl(struct dmar_domain *domain, dmar_gaddr_t maxaddr)
{
	struct dmar_unit *unit;
	struct idpgtbl *tbl;
	vm_object_t res;
	vm_page_t m;
	int leaf, i;

	leaf = 0; /* silence gcc */

	/*
	 * First, determine where to stop the paging structures.
	 */
	for (i = 0; i < domain->pglvl; i++) {
		if (i == domain->pglvl - 1 || domain_is_sp_lvl(domain, i)) {
			leaf = i;
			break;
		}
	}

	/*
	 * Search the cache for a compatible page table.  Qualified
	 * page table must map up to maxaddr, its level must be
	 * supported by the DMAR and leaf should be equal to the
	 * calculated value.  The later restriction could be lifted
	 * but I believe it is currently impossible to have any
	 * deviations for existing hardware.
	 */
	sx_slock(&idpgtbl_lock);
	LIST_FOREACH(tbl, &idpgtbls, link) {
		if (tbl->maxaddr >= maxaddr &&
		    dmar_pglvl_supported(domain->dmar, tbl->pglvl) &&
		    tbl->leaf == leaf) {
			res = tbl->pgtbl_obj;
			vm_object_reference(res);
			sx_sunlock(&idpgtbl_lock);
			domain->pglvl = tbl->pglvl; /* XXXKIB ? */
			goto end;
		}
	}

	/*
	 * Not found in cache, relock the cache into exclusive mode to
	 * be able to add element, and recheck cache again after the
	 * relock.
	 */
	sx_sunlock(&idpgtbl_lock);
	sx_xlock(&idpgtbl_lock);
	LIST_FOREACH(tbl, &idpgtbls, link) {
		if (tbl->maxaddr >= maxaddr &&
		    dmar_pglvl_supported(domain->dmar, tbl->pglvl) &&
		    tbl->leaf == leaf) {
			res = tbl->pgtbl_obj;
			vm_object_reference(res);
			sx_xunlock(&idpgtbl_lock);
			domain->pglvl = tbl->pglvl; /* XXXKIB ? */
			return (res);
		}
	}

	/*
	 * Still not found, create new page table.
	 */
	tbl = malloc(sizeof(*tbl), M_DMAR_IDPGTBL, M_WAITOK);
	tbl->pglvl = domain->pglvl;
	tbl->leaf = leaf;
	tbl->maxaddr = maxaddr;
	tbl->pgtbl_obj = vm_pager_allocate(OBJT_PHYS, NULL,
	    IDX_TO_OFF(pglvl_max_pages(tbl->pglvl)), 0, 0, NULL);
	VM_OBJECT_WLOCK(tbl->pgtbl_obj);
	domain_idmap_nextlvl(tbl, 0, 0, 0);
	VM_OBJECT_WUNLOCK(tbl->pgtbl_obj);
	LIST_INSERT_HEAD(&idpgtbls, tbl, link);
	res = tbl->pgtbl_obj;
	vm_object_reference(res);
	sx_xunlock(&idpgtbl_lock);

end:
	/*
	 * Table was found or created.
	 *
	 * If DMAR does not snoop paging structures accesses, flush
	 * CPU cache to memory.  Note that dmar_unmap_pgtbl() coherent
	 * argument was possibly invalid at the time of the identity
	 * page table creation, since DMAR which was passed at the
	 * time of creation could be coherent, while current DMAR is
	 * not.
	 *
	 * If DMAR cannot look into the chipset write buffer, flush it
	 * as well.
	 */
	unit = domain->dmar;
	if (!DMAR_IS_COHERENT(unit)) {
		VM_OBJECT_WLOCK(res);
		for (m = vm_page_lookup(res, 0); m != NULL;
		     m = vm_page_next(m))
			pmap_invalidate_cache_pages(&m, 1);
		VM_OBJECT_WUNLOCK(res);
	}
	if ((unit->hw_cap & DMAR_CAP_RWBF) != 0) {
		DMAR_LOCK(unit);
		dmar_flush_write_bufs(unit);
		DMAR_UNLOCK(unit);
	}
	
	return (res);
}

/*
 * Return a reference to the identity mapping page table to the cache.
 */
void
put_idmap_pgtbl(vm_object_t obj)
{
	struct idpgtbl *tbl, *tbl1;
	vm_object_t rmobj;

	sx_slock(&idpgtbl_lock);
	KASSERT(obj->ref_count >= 2, ("lost cache reference"));
	vm_object_deallocate(obj);

	/*
	 * Cache always owns one last reference on the page table object.
	 * If there is an additional reference, object must stay.
	 */
	if (obj->ref_count > 1) {
		sx_sunlock(&idpgtbl_lock);
		return;
	}

	/*
	 * Cache reference is the last, remove cache element and free
	 * page table object, returning the page table pages to the
	 * system.
	 */
	sx_sunlock(&idpgtbl_lock);
	sx_xlock(&idpgtbl_lock);
	LIST_FOREACH_SAFE(tbl, &idpgtbls, link, tbl1) {
		rmobj = tbl->pgtbl_obj;
		if (rmobj->ref_count == 1) {
			LIST_REMOVE(tbl, link);
			atomic_subtract_int(&dmar_tbl_pagecnt,
			    rmobj->resident_page_count);
			vm_object_deallocate(rmobj);
			free(tbl, M_DMAR_IDPGTBL);
		}
	}
	sx_xunlock(&idpgtbl_lock);
}

/*
 * The core routines to map and unmap host pages at the given guest
 * address.  Support superpages.
 */

/*
 * Index of the pte for the guest address base in the page table at
 * the level lvl.
 */
static int
domain_pgtbl_pte_off(struct dmar_domain *domain, dmar_gaddr_t base, int lvl)
{

	base >>= DMAR_PAGE_SHIFT + (domain->pglvl - lvl - 1) *
	    DMAR_NPTEPGSHIFT;
	return (base & DMAR_PTEMASK);
}

/*
 * Returns the page index of the page table page in the page table
 * object, which maps the given address base at the page table level
 * lvl.
 */
static vm_pindex_t
domain_pgtbl_get_pindex(struct dmar_domain *domain, dmar_gaddr_t base, int lvl)
{
	vm_pindex_t idx, pidx;
	int i;

	KASSERT(lvl >= 0 && lvl < domain->pglvl,
	    ("wrong lvl %p %d", domain, lvl));

	for (pidx = idx = 0, i = 0; i < lvl; i++, pidx = idx) {
		idx = domain_pgtbl_pte_off(domain, base, i) +
		    pidx * DMAR_NPTEPG + 1;
	}
	return (idx);
}

static dmar_pte_t *
domain_pgtbl_map_pte(struct dmar_domain *domain, dmar_gaddr_t base, int lvl,
    int flags, vm_pindex_t *idxp, struct sf_buf **sf)
{
	vm_page_t m;
	struct sf_buf *sfp;
	dmar_pte_t *pte, *ptep;
	vm_pindex_t idx, idx1;

	DMAR_DOMAIN_ASSERT_PGLOCKED(domain);
	KASSERT((flags & DMAR_PGF_OBJL) != 0, ("lost PGF_OBJL"));

	idx = domain_pgtbl_get_pindex(domain, base, lvl);
	if (*sf != NULL && idx == *idxp) {
		pte = (dmar_pte_t *)sf_buf_kva(*sf);
	} else {
		if (*sf != NULL)
			dmar_unmap_pgtbl(*sf);
		*idxp = idx;
retry:
		pte = dmar_map_pgtbl(domain->pgtbl_obj, idx, flags, sf);
		if (pte == NULL) {
			KASSERT(lvl > 0,
			    ("lost root page table page %p", domain));
			/*
			 * Page table page does not exist, allocate
			 * it and create a pte in the preceeding page level
			 * to reference the allocated page table page.
			 */
			m = dmar_pgalloc(domain->pgtbl_obj, idx, flags |
			    DMAR_PGF_ZERO);
			if (m == NULL)
				return (NULL);

			/*
			 * Prevent potential free while pgtbl_obj is
			 * unlocked in the recursive call to
			 * domain_pgtbl_map_pte(), if other thread did
			 * pte write and clean while the lock is
			 * dropped.
			 */
			m->wire_count++;

			sfp = NULL;
			ptep = domain_pgtbl_map_pte(domain, base, lvl - 1,
			    flags, &idx1, &sfp);
			if (ptep == NULL) {
				KASSERT(m->pindex != 0,
				    ("loosing root page %p", domain));
				m->wire_count--;
				dmar_pgfree(domain->pgtbl_obj, m->pindex,
				    flags);
				return (NULL);
			}
			dmar_pte_store(&ptep->pte, DMAR_PTE_R | DMAR_PTE_W |
			    VM_PAGE_TO_PHYS(m));
			dmar_flush_pte_to_ram(domain->dmar, ptep);
			sf_buf_page(sfp)->wire_count += 1;
			m->wire_count--;
			dmar_unmap_pgtbl(sfp);
			/* Only executed once. */
			goto retry;
		}
	}
	pte += domain_pgtbl_pte_off(domain, base, lvl);
	return (pte);
}

static int
domain_map_buf_locked(struct dmar_domain *domain, dmar_gaddr_t base,
    dmar_gaddr_t size, vm_page_t *ma, uint64_t pflags, int flags)
{
	dmar_pte_t *pte;
	struct sf_buf *sf;
	dmar_gaddr_t pg_sz, base1, size1;
	vm_pindex_t pi, c, idx, run_sz;
	int lvl;
	bool superpage;

	DMAR_DOMAIN_ASSERT_PGLOCKED(domain);

	base1 = base;
	size1 = size;
	flags |= DMAR_PGF_OBJL;
	TD_PREP_PINNED_ASSERT;

	for (sf = NULL, pi = 0; size > 0; base += pg_sz, size -= pg_sz,
	    pi += run_sz) {
		for (lvl = 0, c = 0, superpage = false;; lvl++) {
			pg_sz = domain_page_size(domain, lvl);
			run_sz = pg_sz >> DMAR_PAGE_SHIFT;
			if (lvl == domain->pglvl - 1)
				break;
			/*
			 * Check if the current base suitable for the
			 * superpage mapping.  First, verify the level.
			 */
			if (!domain_is_sp_lvl(domain, lvl))
				continue;
			/*
			 * Next, look at the size of the mapping and
			 * alignment of both guest and host addresses.
			 */
			if (size < pg_sz || (base & (pg_sz - 1)) != 0 ||
			    (VM_PAGE_TO_PHYS(ma[pi]) & (pg_sz - 1)) != 0)
				continue;
			/* All passed, check host pages contiguouty. */
			if (c == 0) {
				for (c = 1; c < run_sz; c++) {
					if (VM_PAGE_TO_PHYS(ma[pi + c]) !=
					    VM_PAGE_TO_PHYS(ma[pi + c - 1]) +
					    PAGE_SIZE)
						break;
				}
			}
			if (c >= run_sz) {
				superpage = true;
				break;
			}
		}
		KASSERT(size >= pg_sz,
		    ("mapping loop overflow %p %jx %jx %jx", domain,
		    (uintmax_t)base, (uintmax_t)size, (uintmax_t)pg_sz));
		KASSERT(pg_sz > 0, ("pg_sz 0 lvl %d", lvl));
		pte = domain_pgtbl_map_pte(domain, base, lvl, flags, &idx, &sf);
		if (pte == NULL) {
			KASSERT((flags & DMAR_PGF_WAITOK) == 0,
			    ("failed waitable pte alloc %p", domain));
			if (sf != NULL)
				dmar_unmap_pgtbl(sf);
			domain_unmap_buf_locked(domain, base1, base - base1,
			    flags);
			TD_PINNED_ASSERT;
			return (ENOMEM);
		}
		dmar_pte_store(&pte->pte, VM_PAGE_TO_PHYS(ma[pi]) | pflags |
		    (superpage ? DMAR_PTE_SP : 0));
		dmar_flush_pte_to_ram(domain->dmar, pte);
		sf_buf_page(sf)->wire_count += 1;
	}
	if (sf != NULL)
		dmar_unmap_pgtbl(sf);
	TD_PINNED_ASSERT;
	return (0);
}

int
domain_map_buf(struct dmar_domain *domain, dmar_gaddr_t base, dmar_gaddr_t size,
    vm_page_t *ma, uint64_t pflags, int flags)
{
	struct dmar_unit *unit;
	int error;

	unit = domain->dmar;

	KASSERT((domain->flags & DMAR_DOMAIN_IDMAP) == 0,
	    ("modifying idmap pagetable domain %p", domain));
	KASSERT((base & DMAR_PAGE_MASK) == 0,
	    ("non-aligned base %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT((size & DMAR_PAGE_MASK) == 0,
	    ("non-aligned size %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT(size > 0, ("zero size %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT(base < (1ULL << domain->agaw),
	    ("base too high %p %jx %jx agaw %d", domain, (uintmax_t)base,
	    (uintmax_t)size, domain->agaw));
	KASSERT(base + size < (1ULL << domain->agaw),
	    ("end too high %p %jx %jx agaw %d", domain, (uintmax_t)base,
	    (uintmax_t)size, domain->agaw));
	KASSERT(base + size > base,
	    ("size overflow %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT((pflags & (DMAR_PTE_R | DMAR_PTE_W)) != 0,
	    ("neither read nor write %jx", (uintmax_t)pflags));
	KASSERT((pflags & ~(DMAR_PTE_R | DMAR_PTE_W | DMAR_PTE_SNP |
	    DMAR_PTE_TM)) == 0,
	    ("invalid pte flags %jx", (uintmax_t)pflags));
	KASSERT((pflags & DMAR_PTE_SNP) == 0 ||
	    (unit->hw_ecap & DMAR_ECAP_SC) != 0,
	    ("PTE_SNP for dmar without snoop control %p %jx",
	    domain, (uintmax_t)pflags));
	KASSERT((pflags & DMAR_PTE_TM) == 0 ||
	    (unit->hw_ecap & DMAR_ECAP_DI) != 0,
	    ("PTE_TM for dmar without DIOTLB %p %jx",
	    domain, (uintmax_t)pflags));
	KASSERT((flags & ~DMAR_PGF_WAITOK) == 0, ("invalid flags %x", flags));

	DMAR_DOMAIN_PGLOCK(domain);
	error = domain_map_buf_locked(domain, base, size, ma, pflags, flags);
	DMAR_DOMAIN_PGUNLOCK(domain);
	if (error != 0)
		return (error);

	if ((unit->hw_cap & DMAR_CAP_CM) != 0)
		domain_flush_iotlb_sync(domain, base, size);
	else if ((unit->hw_cap & DMAR_CAP_RWBF) != 0) {
		/* See 11.1 Write Buffer Flushing. */
		DMAR_LOCK(unit);
		dmar_flush_write_bufs(unit);
		DMAR_UNLOCK(unit);
	}
	return (0);
}

static void domain_unmap_clear_pte(struct dmar_domain *domain,
    dmar_gaddr_t base, int lvl, int flags, dmar_pte_t *pte,
    struct sf_buf **sf, bool free_fs);

static void
domain_free_pgtbl_pde(struct dmar_domain *domain, dmar_gaddr_t base,
    int lvl, int flags)
{
	struct sf_buf *sf;
	dmar_pte_t *pde;
	vm_pindex_t idx;

	sf = NULL;
	pde = domain_pgtbl_map_pte(domain, base, lvl, flags, &idx, &sf);
	domain_unmap_clear_pte(domain, base, lvl, flags, pde, &sf, true);
}

static void
domain_unmap_clear_pte(struct dmar_domain *domain, dmar_gaddr_t base, int lvl,
    int flags, dmar_pte_t *pte, struct sf_buf **sf, bool free_sf)
{
	vm_page_t m;

	dmar_pte_clear(&pte->pte);
	dmar_flush_pte_to_ram(domain->dmar, pte);
	m = sf_buf_page(*sf);
	if (free_sf) {
		dmar_unmap_pgtbl(*sf);
		*sf = NULL;
	}
	m->wire_count--;
	if (m->wire_count != 0)
		return;
	KASSERT(lvl != 0,
	    ("lost reference (lvl) on root pg domain %p base %jx lvl %d",
	    domain, (uintmax_t)base, lvl));
	KASSERT(m->pindex != 0,
	    ("lost reference (idx) on root pg domain %p base %jx lvl %d",
	    domain, (uintmax_t)base, lvl));
	dmar_pgfree(domain->pgtbl_obj, m->pindex, flags);
	domain_free_pgtbl_pde(domain, base, lvl - 1, flags);
}

/*
 * Assumes that the unmap is never partial.
 */
static int
domain_unmap_buf_locked(struct dmar_domain *domain, dmar_gaddr_t base,
    dmar_gaddr_t size, int flags)
{
	dmar_pte_t *pte;
	struct sf_buf *sf;
	vm_pindex_t idx;
	dmar_gaddr_t pg_sz;
	int lvl;

	DMAR_DOMAIN_ASSERT_PGLOCKED(domain);
	if (size == 0)
		return (0);

	KASSERT((domain->flags & DMAR_DOMAIN_IDMAP) == 0,
	    ("modifying idmap pagetable domain %p", domain));
	KASSERT((base & DMAR_PAGE_MASK) == 0,
	    ("non-aligned base %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT((size & DMAR_PAGE_MASK) == 0,
	    ("non-aligned size %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT(base < (1ULL << domain->agaw),
	    ("base too high %p %jx %jx agaw %d", domain, (uintmax_t)base,
	    (uintmax_t)size, domain->agaw));
	KASSERT(base + size < (1ULL << domain->agaw),
	    ("end too high %p %jx %jx agaw %d", domain, (uintmax_t)base,
	    (uintmax_t)size, domain->agaw));
	KASSERT(base + size > base,
	    ("size overflow %p %jx %jx", domain, (uintmax_t)base,
	    (uintmax_t)size));
	KASSERT((flags & ~DMAR_PGF_WAITOK) == 0, ("invalid flags %x", flags));

	pg_sz = 0; /* silence gcc */
	flags |= DMAR_PGF_OBJL;
	TD_PREP_PINNED_ASSERT;

	for (sf = NULL; size > 0; base += pg_sz, size -= pg_sz) {
		for (lvl = 0; lvl < domain->pglvl; lvl++) {
			if (lvl != domain->pglvl - 1 &&
			    !domain_is_sp_lvl(domain, lvl))
				continue;
			pg_sz = domain_page_size(domain, lvl);
			if (pg_sz > size)
				continue;
			pte = domain_pgtbl_map_pte(domain, base, lvl, flags,
			    &idx, &sf);
			KASSERT(pte != NULL,
			    ("sleeping or page missed %p %jx %d 0x%x",
			    domain, (uintmax_t)base, lvl, flags));
			if ((pte->pte & DMAR_PTE_SP) != 0 ||
			    lvl == domain->pglvl - 1) {
				domain_unmap_clear_pte(domain, base, lvl,
				    flags, pte, &sf, false);
				break;
			}
		}
		KASSERT(size >= pg_sz,
		    ("unmapping loop overflow %p %jx %jx %jx", domain,
		    (uintmax_t)base, (uintmax_t)size, (uintmax_t)pg_sz));
	}
	if (sf != NULL)
		dmar_unmap_pgtbl(sf);
	/*
	 * See 11.1 Write Buffer Flushing for an explanation why RWBF
	 * can be ignored there.
	 */

	TD_PINNED_ASSERT;
	return (0);
}

int
domain_unmap_buf(struct dmar_domain *domain, dmar_gaddr_t base,
    dmar_gaddr_t size, int flags)
{
	int error;

	DMAR_DOMAIN_PGLOCK(domain);
	error = domain_unmap_buf_locked(domain, base, size, flags);
	DMAR_DOMAIN_PGUNLOCK(domain);
	return (error);
}

int
domain_alloc_pgtbl(struct dmar_domain *domain)
{
	vm_page_t m;

	KASSERT(domain->pgtbl_obj == NULL,
	    ("already initialized %p", domain));

	domain->pgtbl_obj = vm_pager_allocate(OBJT_PHYS, NULL,
	    IDX_TO_OFF(pglvl_max_pages(domain->pglvl)), 0, 0, NULL);
	DMAR_DOMAIN_PGLOCK(domain);
	m = dmar_pgalloc(domain->pgtbl_obj, 0, DMAR_PGF_WAITOK |
	    DMAR_PGF_ZERO | DMAR_PGF_OBJL);
	/* No implicit free of the top level page table page. */
	m->wire_count = 1;
	DMAR_DOMAIN_PGUNLOCK(domain);
	DMAR_LOCK(domain->dmar);
	domain->flags |= DMAR_DOMAIN_PGTBL_INITED;
	DMAR_UNLOCK(domain->dmar);
	return (0);
}

void
domain_free_pgtbl(struct dmar_domain *domain)
{
	vm_object_t obj;
	vm_page_t m;

	obj = domain->pgtbl_obj;
	if (obj == NULL) {
		KASSERT((domain->dmar->hw_ecap & DMAR_ECAP_PT) != 0 &&
		    (domain->flags & DMAR_DOMAIN_IDMAP) != 0,
		    ("lost pagetable object domain %p", domain));
		return;
	}
	DMAR_DOMAIN_ASSERT_PGLOCKED(domain);
	domain->pgtbl_obj = NULL;

	if ((domain->flags & DMAR_DOMAIN_IDMAP) != 0) {
		put_idmap_pgtbl(obj);
		domain->flags &= ~DMAR_DOMAIN_IDMAP;
		return;
	}

	/* Obliterate wire_counts */
	VM_OBJECT_ASSERT_WLOCKED(obj);
	for (m = vm_page_lookup(obj, 0); m != NULL; m = vm_page_next(m))
		m->wire_count = 0;
	VM_OBJECT_WUNLOCK(obj);
	vm_object_deallocate(obj);
}

static inline uint64_t
domain_wait_iotlb_flush(struct dmar_unit *unit, uint64_t wt, int iro)
{
	uint64_t iotlbr;

	dmar_write8(unit, iro + DMAR_IOTLB_REG_OFF, DMAR_IOTLB_IVT |
	    DMAR_IOTLB_DR | DMAR_IOTLB_DW | wt);
	for (;;) {
		iotlbr = dmar_read8(unit, iro + DMAR_IOTLB_REG_OFF);
		if ((iotlbr & DMAR_IOTLB_IVT) == 0)
			break;
		cpu_spinwait();
	}
	return (iotlbr);
}

void
domain_flush_iotlb_sync(struct dmar_domain *domain, dmar_gaddr_t base,
    dmar_gaddr_t size)
{
	struct dmar_unit *unit;
	dmar_gaddr_t isize;
	uint64_t iotlbr;
	int am, iro;

	unit = domain->dmar;
	KASSERT(!unit->qi_enabled, ("dmar%d: sync iotlb flush call",
	    unit->unit));
	iro = DMAR_ECAP_IRO(unit->hw_ecap) * 16;
	DMAR_LOCK(unit);
	if ((unit->hw_cap & DMAR_CAP_PSI) == 0 || size > 2 * 1024 * 1024) {
		iotlbr = domain_wait_iotlb_flush(unit, DMAR_IOTLB_IIRG_DOM |
		    DMAR_IOTLB_DID(domain->domain), iro);
		KASSERT((iotlbr & DMAR_IOTLB_IAIG_MASK) !=
		    DMAR_IOTLB_IAIG_INVLD,
		    ("dmar%d: invalidation failed %jx", unit->unit,
		    (uintmax_t)iotlbr));
	} else {
		for (; size > 0; base += isize, size -= isize) {
			am = calc_am(unit, base, size, &isize);
			dmar_write8(unit, iro, base | am);
			iotlbr = domain_wait_iotlb_flush(unit,
			    DMAR_IOTLB_IIRG_PAGE |
			    DMAR_IOTLB_DID(domain->domain), iro);
			KASSERT((iotlbr & DMAR_IOTLB_IAIG_MASK) !=
			    DMAR_IOTLB_IAIG_INVLD,
			    ("dmar%d: PSI invalidation failed "
			    "iotlbr 0x%jx base 0x%jx size 0x%jx am %d",
			    unit->unit, (uintmax_t)iotlbr,
			    (uintmax_t)base, (uintmax_t)size, am));
			/*
			 * Any non-page granularity covers whole guest
			 * address space for the domain.
			 */
			if ((iotlbr & DMAR_IOTLB_IAIG_MASK) !=
			    DMAR_IOTLB_IAIG_PAGE)
				break;
		}
	}
	DMAR_UNLOCK(unit);
}
