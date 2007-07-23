#ifndef _X86_64_PGALLOC_H
#define _X86_64_PGALLOC_H

#include <asm/pda.h>
#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/quicklist.h>

#define QUICK_PGD 0	/* We preserve special mappings over free */
#define QUICK_PT 1	/* Other page table pages that are zero on free */

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE | __pa(pte)))
#define pud_populate(mm, pud, pmd) \
		set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)))
#define pgd_populate(mm, pgd, pud) \
		set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(pud)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE | (page_to_pfn(pte) << PAGE_SHIFT)));
}

static inline void pmd_free(pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	quicklist_free(QUICK_PT, NULL, pmd);
}

static inline pmd_t *pmd_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *)quicklist_alloc(QUICK_PT, GFP_KERNEL|__GFP_REPEAT, NULL);
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pud_t *)quicklist_alloc(QUICK_PT, GFP_KERNEL|__GFP_REPEAT, NULL);
}

static inline void pud_free (pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	quicklist_free(QUICK_PT, NULL, pud);
}

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	spin_lock(&pgd_lock);
	list_add(&page->lru, &pgd_list);
	spin_unlock(&pgd_lock);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	spin_lock(&pgd_lock);
	list_del(&page->lru);
	spin_unlock(&pgd_lock);
}

static inline void pgd_ctor(void *x)
{
	unsigned boundary;
	pgd_t *pgd = x;
	struct page *page = virt_to_page(pgd);

	/*
	 * Copy kernel pointers in from init.
	 */
	boundary = pgd_index(__PAGE_OFFSET);
	memcpy(pgd + boundary,
		init_level4_pgt + boundary,
		(PTRS_PER_PGD - boundary) * sizeof(pgd_t));

	spin_lock(&pgd_lock);
	list_add(&page->lru, &pgd_list);
	spin_unlock(&pgd_lock);
}

static inline void pgd_dtor(void *x)
{
	pgd_t *pgd = x;
	struct page *page = virt_to_page(pgd);

        spin_lock(&pgd_lock);
	list_del(&page->lru);
	spin_unlock(&pgd_lock);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = (pgd_t *)quicklist_alloc(QUICK_PGD,
		GFP_KERNEL|__GFP_REPEAT, pgd_ctor);
	return pgd;
}

static inline void pgd_free(pgd_t *pgd)
{
	BUG_ON((unsigned long)pgd & (PAGE_SIZE-1));
	quicklist_free(QUICK_PGD, pgd_dtor, pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return (pte_t *)quicklist_alloc(QUICK_PT, GFP_KERNEL|__GFP_REPEAT, NULL);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	void *p = (void *)quicklist_alloc(QUICK_PT, GFP_KERNEL|__GFP_REPEAT, NULL);

	if (!p)
		return NULL;
	return virt_to_page(p);
}

/* Should really implement gc for free page table pages. This could be
   done with a reference count in struct page. */

static inline void pte_free_kernel(pte_t *pte)
{
	BUG_ON((unsigned long)pte & (PAGE_SIZE-1));
	quicklist_free(QUICK_PT, NULL, pte);
}

static inline void pte_free(struct page *pte)
{
	quicklist_free_page(QUICK_PT, NULL, pte);
}

#define __pte_free_tlb(tlb,pte) quicklist_free_page(QUICK_PT, NULL,(pte))

#define __pmd_free_tlb(tlb,x)   quicklist_free(QUICK_PT, NULL, (x))
#define __pud_free_tlb(tlb,x)   quicklist_free(QUICK_PT, NULL, (x))

static inline void check_pgt_cache(void)
{
	quicklist_trim(QUICK_PGD, pgd_dtor, 25, 16);
	quicklist_trim(QUICK_PT, NULL, 25, 16);
}
#endif /* _X86_64_PGALLOC_H */
