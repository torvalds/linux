#ifndef __ASM_SH64_PGALLOC_H
#define __ASM_SH64_PGALLOC_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/pgalloc.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 * Copyright (C) 2003, 2004  Richard Curnow
 *
 */

#include <linux/mm.h>
#include <linux/quicklist.h>
#include <asm/page.h>

static inline void pgd_init(unsigned long page)
{
	unsigned long *pgd = (unsigned long *)page;
	extern pte_t empty_bad_pte_table[PTRS_PER_PTE];
	int i;

	for (i = 0; i < USER_PTRS_PER_PGD; i++)
		pgd[i] = (unsigned long)empty_bad_pte_table;
}

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline pgd_t *get_pgd_slow(void)
{
	unsigned int pgd_size = (USER_PTRS_PER_PGD * sizeof(pgd_t));
	pgd_t *ret = kmalloc(pgd_size, GFP_KERNEL);
	return ret;
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pgd_free(pgd_t *pgd)
{
	quicklist_free(0, NULL, pgd);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	void *pg = quicklist_alloc(0, GFP_KERNEL, NULL);
	return pg ? virt_to_page(pg) : NULL;
}

static inline void pte_free_kernel(pte_t *pte)
{
	quicklist_free(0, NULL, pte);
}

static inline void pte_free(struct page *pte)
{
	quicklist_free_page(0, NULL, pte);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					   unsigned long address)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#if defined(CONFIG_SH64_PGTABLE_2_LEVEL)

#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)			do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()
#define __pte_free_tlb(tlb,pte)		tlb_remove_page((tlb),(pte))
#define __pmd_free_tlb(tlb,pmd)		do { } while (0)

#elif defined(CONFIG_SH64_PGTABLE_3_LEVEL)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pmd_free(pmd_t *pmd)
{
	quicklist_free(0, NULL, pmd);
}

#define pgd_populate(mm, pgd, pmd)	pgd_set(pgd, pmd)
#define __pmd_free_tlb(tlb,pmd)		pmd_free(pmd)

#else
#error "No defined page table size"
#endif

#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) (pte)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) page_address (pte)));
}

static inline void check_pgt_cache(void)
{
	quicklist_trim(0, NULL, 25, 16);
}

#endif /* __ASM_SH64_PGALLOC_H */
