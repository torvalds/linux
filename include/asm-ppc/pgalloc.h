#ifdef __KERNEL__
#ifndef _PPC_PGALLOC_H
#define _PPC_PGALLOC_H

#include <linux/config.h>
#include <linux/threads.h>

extern void __bad_pte(pmd_t *pmd);

extern pgd_t *pgd_alloc(struct mm_struct *mm);
extern void pgd_free(pgd_t *pgd);

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })
#define pmd_free(x)                     do { } while (0)
#define __pmd_free_tlb(tlb,x)		do { } while (0)
#define pgd_populate(mm, pmd, pte)      BUG()

#ifndef CONFIG_BOOKE
#define pmd_populate_kernel(mm, pmd, pte)	\
		(pmd_val(*(pmd)) = __pa(pte) | _PMD_PRESENT)
#define pmd_populate(mm, pmd, pte)	\
		(pmd_val(*(pmd)) = (page_to_pfn(pte) << PAGE_SHIFT) | _PMD_PRESENT)
#else
#define pmd_populate_kernel(mm, pmd, pte)	\
		(pmd_val(*(pmd)) = (unsigned long)pte | _PMD_PRESENT)
#define pmd_populate(mm, pmd, pte)	\
		(pmd_val(*(pmd)) = (unsigned long)lowmem_page_address(pte) | _PMD_PRESENT)
#endif

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr);
extern struct page *pte_alloc_one(struct mm_struct *mm, unsigned long addr);
extern void pte_free_kernel(pte_t *pte);
extern void pte_free(struct page *pte);

#define __pte_free_tlb(tlb, pte)	pte_free((pte))

#define check_pgt_cache()	do { } while (0)

#endif /* _PPC_PGALLOC_H */
#endif /* __KERNEL__ */
