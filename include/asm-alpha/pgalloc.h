#ifndef _ALPHA_PGALLOC_H
#define _ALPHA_PGALLOC_H

#include <linux/mm.h>
#include <linux/mmzone.h>

/*      
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	pmd_set(pmd, (pte_t *)(page_to_pa(pte) + PAGE_OFFSET));
}

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_set(pmd, pte);
}

static inline void
pgd_populate(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmd)
{
	pgd_set(pgd, pmd);
}

extern pgd_t *pgd_alloc(struct mm_struct *mm);

static inline void
pgd_free(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static inline pmd_t *
pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pmd_t *ret = (pmd_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	return ret;
}

static inline void
pmd_free(pmd_t *pmd)
{
	free_page((unsigned long)pmd);
}

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr);

static inline void
pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = pte_alloc_one_kernel(mm, addr);
	if (pte)
		return virt_to_page(pte);
	return NULL;
}

static inline void
pte_free(struct page *page)
{
	__free_page(page);
}

#define check_pgt_cache()	do { } while (0)

#endif /* _ALPHA_PGALLOC_H */
