/*
 *  linux/include/asm-arm/pgalloc.h
 *
 *  Copyright (C) 2000-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGALLOC_H
#define _ASMARM_PGALLOC_H

#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

/*
 * Since we have only two-level page tables, these are trivial
 */
#define pmd_alloc_one(mm,addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(pmd)			do { } while (0)
#define pgd_populate(mm,pmd,pte)	BUG()

extern pgd_t *get_pgd_slow(struct mm_struct *mm);
extern void free_pgd_slow(pgd_t *pgd);

#define pgd_alloc(mm)			get_pgd_slow(mm)
#define pgd_free(pgd)			free_pgd_slow(pgd)

#define check_pgt_cache()		do { } while (0)

/*
 * Allocate one PTE table.
 *
 * This actually allocates two hardware PTE tables, but we wrap this up
 * into one table thus:
 *
 *  +------------+
 *  |  h/w pt 0  |
 *  +------------+
 *  |  h/w pt 1  |
 *  +------------+
 *  | Linux pt 0 |
 *  +------------+
 *  | Linux pt 1 |
 *  +------------+
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	if (pte) {
		clean_dcache_area(pte, sizeof(pte_t) * PTRS_PER_PTE);
		pte += PTRS_PER_PTE;
	}

	return pte;
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *pte;

	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);
	if (pte) {
		void *page = page_address(pte);
		clean_dcache_area(page, sizeof(pte_t) * PTRS_PER_PTE);
	}

	return pte;
}

/*
 * Free one PTE table.
 */
static inline void pte_free_kernel(pte_t *pte)
{
	if (pte) {
		pte -= PTRS_PER_PTE;
		free_page((unsigned long)pte);
	}
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}

static inline void __pmd_populate(pmd_t *pmdp, unsigned long pmdval)
{
	pmdp[0] = __pmd(pmdval);
	pmdp[1] = __pmd(pmdval + 256 * sizeof(pte_t));
	flush_pmd_entry(pmdp);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * Ensure that we always set both PMD entries.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	unsigned long pte_ptr = (unsigned long)ptep;

	/*
	 * The pmd must be loaded with the physical
	 * address of the PTE table
	 */
	pte_ptr -= PTRS_PER_PTE * sizeof(void *);
	__pmd_populate(pmdp, __pa(pte_ptr) | _PAGE_KERNEL_TABLE);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmdp, struct page *ptep)
{
	__pmd_populate(pmdp, page_to_pfn(ptep) << PAGE_SHIFT | _PAGE_USER_TABLE);
}

#endif
