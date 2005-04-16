/*
 *  include/asm-s390/pgalloc.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgalloc.h"
 *    Copyright (C) 1994  Linus Torvalds
 */

#ifndef _S390_PGALLOC_H
#define _S390_PGALLOC_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/gfp.h>
#include <linux/mm.h>

#define check_pgt_cache()	do {} while (0)

extern void diag10(unsigned long addr);

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;
	int i;

#ifndef __s390x__
	pgd = (pgd_t *) __get_free_pages(GFP_KERNEL,1);
        if (pgd != NULL)
		for (i = 0; i < USER_PTRS_PER_PGD; i++)
			pmd_clear(pmd_offset(pgd + i, i*PGDIR_SIZE));
#else /* __s390x__ */
	pgd = (pgd_t *) __get_free_pages(GFP_KERNEL,2);
        if (pgd != NULL)
		for (i = 0; i < PTRS_PER_PGD; i++)
			pgd_clear(pgd + i);
#endif /* __s390x__ */
	return pgd;
}

static inline void pgd_free(pgd_t *pgd)
{
#ifndef __s390x__
        free_pages((unsigned long) pgd, 1);
#else /* __s390x__ */
        free_pages((unsigned long) pgd, 2);
#endif /* __s390x__ */
}

#ifndef __s390x__
/*
 * page middle directory allocation/free routines.
 * We use pmd cache only on s390x, so these are dummy routines. This
 * code never triggers because the pgd will always be present.
 */
#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)                     do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)      BUG()
#else /* __s390x__ */
static inline pmd_t * pmd_alloc_one(struct mm_struct *mm, unsigned long vmaddr)
{
	pmd_t *pmd;
        int i;

	pmd = (pmd_t *) __get_free_pages(GFP_KERNEL, 2);
	if (pmd != NULL) {
		for (i=0; i < PTRS_PER_PMD; i++)
			pmd_clear(pmd+i);
	}
	return pmd;
}

static inline void pmd_free (pmd_t *pmd)
{
	free_pages((unsigned long) pmd, 2);
}

#define __pmd_free_tlb(tlb,pmd)			\
	do {					\
		tlb_flush_mmu(tlb, 0, 0);	\
		pmd_free(pmd);			\
	 } while (0)

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmd)
{
	pgd_val(*pgd) = _PGD_ENTRY | __pa(pmd);
}

#endif /* __s390x__ */

static inline void 
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
#ifndef __s390x__
	pmd_val(pmd[0]) = _PAGE_TABLE + __pa(pte);
	pmd_val(pmd[1]) = _PAGE_TABLE + __pa(pte+256);
	pmd_val(pmd[2]) = _PAGE_TABLE + __pa(pte+512);
	pmd_val(pmd[3]) = _PAGE_TABLE + __pa(pte+768);
#else /* __s390x__ */
	pmd_val(*pmd) = _PMD_ENTRY + __pa(pte);
	pmd_val1(*pmd) = _PMD_ENTRY + __pa(pte+256);
#endif /* __s390x__ */
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *page)
{
	pmd_populate_kernel(mm, pmd, (pte_t *)((page-mem_map) << PAGE_SHIFT));
}

/*
 * page table entry allocation/free routines.
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long vmaddr)
{
	pte_t *pte;
        int i;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (pte != NULL) {
		for (i=0; i < PTRS_PER_PTE; i++) {
			pte_clear(mm, vmaddr, pte+i);
			vmaddr += PAGE_SIZE;
		}
	}
	return pte;
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long vmaddr)
{
	pte_t *pte = pte_alloc_one_kernel(mm, vmaddr);
	if (pte)
		return virt_to_page(pte);
	return 0;
}

static inline void pte_free_kernel(pte_t *pte)
{
        free_page((unsigned long) pte);
}

static inline void pte_free(struct page *pte)
{
        __free_page(pte);
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page(tlb,pte)

/*
 * This establishes kernel virtual mappings (e.g., as a result of a
 * vmalloc call).  Since s390-esame uses a separate kernel page table,
 * there is nothing to do here... :)
 */
#define set_pgdir(addr,entry) do { } while(0)

#endif /* _S390_PGALLOC_H */
