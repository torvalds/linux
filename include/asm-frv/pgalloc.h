/* pgalloc.h: Page allocation routines for FRV
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Derived from:
 *	include/asm-m68knommu/pgalloc.h
 *	include/asm-i386/pgalloc.h
 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <asm/setup.h>
#include <asm/virtconvert.h>

#ifdef CONFIG_MMU

#define pmd_populate_kernel(mm, pmd, pte) __set_pmd(pmd, __pa(pte) | _PAGE_TABLE)
#define pmd_populate(MM, PMD, PAGE)						\
do {										\
	__set_pmd((PMD), page_to_pfn(PAGE) << PAGE_SHIFT | _PAGE_TABLE);	\
} while(0)

/*
 * Allocate and free page tables.
 */

extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(struct mm_struct *mm, pgd_t *);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);

extern struct page *pte_alloc_one(struct mm_struct *, unsigned long);

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	__free_page(pte);
}

#define __pte_free_tlb(tlb,pte)		tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 * (In the PAE case we free the pmds as part of the pgd.)
 */
#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *) 2); })
#define pmd_free(mm, x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)

#endif /* CONFIG_MMU */

#endif /* _ASM_PGALLOC_H */
