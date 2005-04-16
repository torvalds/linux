#ifndef _I386_PGALLOC_H
#define _I386_PGALLOC_H

#include <linux/config.h>
#include <asm/fixmap.h>
#include <linux/threads.h>
#include <linux/mm.h>		/* for struct page */

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(pte)))

#define pmd_populate(mm, pmd, pte) 				\
	set_pmd(pmd, __pmd(_PAGE_TABLE +			\
		((unsigned long long)page_to_pfn(pte) <<	\
			(unsigned long long) PAGE_SHIFT)))
/*
 * Allocate and free page tables.
 */
extern pgd_t *pgd_alloc(struct mm_struct *);
extern void pgd_free(pgd_t *pgd);

extern pte_t *pte_alloc_one_kernel(struct mm_struct *, unsigned long);
extern struct page *pte_alloc_one(struct mm_struct *, unsigned long);

static inline void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}


#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

#ifdef CONFIG_X86_PAE
/*
 * In the PAE case we free the pmds as part of the pgd.
 */
#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define pud_populate(mm, pmd, pte)	BUG()
#endif

#define check_pgt_cache()	do { } while (0)

#endif /* _I386_PGALLOC_H */
