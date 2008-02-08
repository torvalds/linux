#ifndef _X86_64_PGALLOC_H
#define _X86_64_PGALLOC_H

#include <asm/pda.h>
#include <linux/threads.h>
#include <linux/mm.h>

#define pmd_populate_kernel(mm, pmd, pte) \
		set_pmd(pmd, __pmd(_PAGE_TABLE | __pa(pte)))
#define pud_populate(mm, pud, pmd) \
		set_pud(pud, __pud(_PAGE_TABLE | __pa(pmd)))
#define pgd_populate(mm, pgd, pud) \
		set_pgd(pgd, __pgd(_PAGE_TABLE | __pa(pud)))

#define pmd_pgtable(pmd) pmd_page(pmd)

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *pte)
{
	set_pmd(pmd, __pmd(_PAGE_TABLE | (page_to_pfn(pte) << PAGE_SHIFT)));
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	free_page((unsigned long)pmd);
}

static inline pmd_t *pmd_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pud_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	free_page((unsigned long)pud);
}

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);
	unsigned long flags;

	spin_lock_irqsave(&pgd_lock, flags);
	list_add(&page->lru, &pgd_list);
	spin_unlock_irqrestore(&pgd_lock, flags);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);
	unsigned long flags;

	spin_lock_irqsave(&pgd_lock, flags);
	list_del(&page->lru);
	spin_unlock_irqrestore(&pgd_lock, flags);
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	unsigned boundary;
	pgd_t *pgd = (pgd_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (!pgd)
		return NULL;
	pgd_list_add(pgd);
	/*
	 * Copy kernel pointers in from init.
	 * Could keep a freelist or slab cache of those because the kernel
	 * part never changes.
	 */
	boundary = pgd_index(__PAGE_OFFSET);
	memset(pgd, 0, boundary * sizeof(pgd_t));
	memcpy(pgd + boundary,
	       init_level4_pgt + boundary,
	       (PTRS_PER_PGD - boundary) * sizeof(pgd_t));
	return pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	BUG_ON((unsigned long)pgd & (PAGE_SIZE-1));
	pgd_list_del(pgd);
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return (pte_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *page;
	void *p;

	p = (void *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
	if (!p)
		return NULL;
	page = virt_to_page(p);
	pgtable_page_ctor(page);
	return page;
}

/* Should really implement gc for free page table pages. This could be
   done with a reference count in struct page. */

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	BUG_ON((unsigned long)pte & (PAGE_SIZE-1));
	free_page((unsigned long)pte); 
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
} 

#define __pte_free_tlb(tlb,pte)				\
do {							\
	pgtable_page_dtor((pte));				\
	tlb_remove_page((tlb), (pte));			\
} while (0)

#define __pmd_free_tlb(tlb,x)   tlb_remove_page((tlb),virt_to_page(x))
#define __pud_free_tlb(tlb,x)   tlb_remove_page((tlb),virt_to_page(x))

#endif /* _X86_64_PGALLOC_H */
