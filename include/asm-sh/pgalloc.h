#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <linux/threads.h>
#include <linux/slab.h>
#include <linux/mm.h>

#define pgd_quicklist ((unsigned long *)0)
#define pmd_quicklist ((unsigned long *)0)
#define pte_quicklist ((unsigned long *)0)
#define pgtable_cache_size 0L

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE + page_to_phys(pte)));
}

/*
 * Allocate and free page tables.
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	unsigned int pgd_size = (USER_PTRS_PER_PGD * sizeof(pgd_t));
	pgd_t *pgd = (pgd_t *)kmalloc(pgd_size, GFP_KERNEL);

	if (pgd)
		memset(pgd, 0, pgd_size);

	return pgd;
}

static inline void pgd_free(pgd_t *pgd)
{
	kfree(pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	pte_t *pte;

	pte = (pte_t *) __get_free_page(GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO);

	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	struct page *pte;

   	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);

	return pte;
}

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()
#define check_pgt_cache()		do { } while (0)

#ifdef CONFIG_CPU_SH4
#define PG_mapped			PG_arch_1
#endif

#endif /* __ASM_SH_PGALLOC_H */
