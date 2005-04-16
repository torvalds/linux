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
#include <linux/slab.h>

extern kmem_cache_t *pte_cache;

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr){
	return kmem_cache_alloc(pte_cache, GFP_KERNEL);
}

static inline void pte_free_kernel(pte_t *pte){
        if (pte)
                kmem_cache_free(pte_cache, pte);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * If 'mm' is the init tasks mm, then we are doing a vmalloc, and we
 * need to set stuff up correctly for it.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
//FIXME - is this doing the right thing?
        set_pmd(pmdp, (unsigned long)ptep | 1/*FIXME _PMD_PRESENT*/);
}

/*
 * FIXME - We use the old 2.5.5-rmk1 hack for this.
 * This is not truly correct, but should be functional.
 */
#define pte_alloc_one(mm,addr)  ((struct page *)pte_alloc_one_kernel(mm,addr))
#define pte_free(pte)           pte_free_kernel((pte_t *)pte)
#define pmd_populate(mm,pmdp,ptep) pmd_populate_kernel(mm,pmdp,(pte_t *)ptep)

/*
 * Since we have only two-level page tables, these are trivial
 * 
 * trick __pmd_alloc into optimising away. The actual value is irrelevant though as it
 * is thrown away. It just cant be zero. -IM
 */

#define pmd_alloc_one(mm,addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(pmd)			do { } while (0)
#define pgd_populate(mm,pmd,pte)	BUG()

extern pgd_t *get_pgd_slow(struct mm_struct *mm);
extern void free_pgd_slow(pgd_t *pgd);

#define pgd_alloc(mm)			get_pgd_slow(mm)
#define pgd_free(pgd)			free_pgd_slow(pgd)

#define check_pgt_cache()		do { } while (0)

#endif
