#ifndef _ASM_IA64_PGALLOC_H
#define _ASM_IA64_PGALLOC_H

/*
 * This file contains the functions and defines necessary to allocate
 * page tables.
 *
 * This hopefully works with any (fixed) ia-64 page-size, as defined
 * in <asm/page.h> (currently 8192).
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000, Goutham Rao <goutham.rao@intel.com>
 */

#include <linux/config.h>

#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/threads.h>

#include <asm/mmu_context.h>

/*
 * Very stupidly, we used to get new pgd's and pmd's, init their contents
 * to point to the NULL versions of the next level page table, later on
 * completely re-init them the same way, then free them up.  This wasted
 * a lot of work and caused unnecessary memory traffic.  How broken...
 * We fix this by caching them.
 */
#define pgd_quicklist		(local_cpu_data->pgd_quick)
#define pmd_quicklist		(local_cpu_data->pmd_quick)
#define pgtable_cache_size	(local_cpu_data->pgtable_cache_sz)

static inline pgd_t*
pgd_alloc_one_fast (struct mm_struct *mm)
{
	unsigned long *ret = NULL;

	preempt_disable();

	ret = pgd_quicklist;
	if (likely(ret != NULL)) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	} else
		ret = NULL;

	preempt_enable();

	return (pgd_t *) ret;
}

static inline pgd_t*
pgd_alloc (struct mm_struct *mm)
{
	/* the VM system never calls pgd_alloc_one_fast(), so we do it here. */
	pgd_t *pgd = pgd_alloc_one_fast(mm);

	if (unlikely(pgd == NULL)) {
		pgd = (pgd_t *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
	}
	return pgd;
}

static inline void
pgd_free (pgd_t *pgd)
{
	preempt_disable();
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	++pgtable_cache_size;
	preempt_enable();
}

static inline void
pud_populate (struct mm_struct *mm, pud_t *pud_entry, pmd_t *pmd)
{
	pud_val(*pud_entry) = __pa(pmd);
}

static inline pmd_t*
pmd_alloc_one_fast (struct mm_struct *mm, unsigned long addr)
{
	unsigned long *ret = NULL;

	preempt_disable();

	ret = (unsigned long *)pmd_quicklist;
	if (likely(ret != NULL)) {
		pmd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	}

	preempt_enable();

	return (pmd_t *)ret;
}

static inline pmd_t*
pmd_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	pmd_t *pmd = (pmd_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);

	return pmd;
}

static inline void
pmd_free (pmd_t *pmd)
{
	preempt_disable();
	*(unsigned long *)pmd = (unsigned long) pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	++pgtable_cache_size;
	preempt_enable();
}

#define __pmd_free_tlb(tlb, pmd)	pmd_free(pmd)

static inline void
pmd_populate (struct mm_struct *mm, pmd_t *pmd_entry, struct page *pte)
{
	pmd_val(*pmd_entry) = page_to_phys(pte);
}

static inline void
pmd_populate_kernel (struct mm_struct *mm, pmd_t *pmd_entry, pte_t *pte)
{
	pmd_val(*pmd_entry) = __pa(pte);
}

static inline struct page *
pte_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	struct page *pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);

	return pte;
}

static inline pte_t *
pte_alloc_one_kernel (struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);

	return pte;
}

static inline void
pte_free (struct page *pte)
{
	__free_page(pte);
}

static inline void
pte_free_kernel (pte_t *pte)
{
	free_page((unsigned long) pte);
}

#define __pte_free_tlb(tlb, pte)	tlb_remove_page((tlb), (pte))

extern void check_pgt_cache (void);

#endif /* _ASM_IA64_PGALLOC_H */
