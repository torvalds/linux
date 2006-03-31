#ifndef _X86_64_PGALLOC_H
#define _X86_64_PGALLOC_H

#include <asm/fixmap.h>
#include <asm/pda.h>
#include <linux/threads.h>
#include <linux/mm.h>

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

static inline pmd_t *get_pmd(void)
{
	return (pmd_t *)get_zeroed_page(GFP_KERNEL);
}

static inline void pmd_free(pmd_t *pmd)
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

static inline void pud_free (pud_t *pud)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	free_page((unsigned long)pud);
}

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);

	spin_lock(&pgd_lock);
	page->index = (pgoff_t)pgd_list;
	if (pgd_list)
		pgd_list->private = (unsigned long)&page->index;
	pgd_list = page;
	page->private = (unsigned long)&pgd_list;
	spin_unlock(&pgd_lock);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *next, **pprev, *page = virt_to_page(pgd);

	spin_lock(&pgd_lock);
	next = (struct page *)page->index;
	pprev = (struct page **)page->private;
	*pprev = next;
	if (next)
		next->private = (unsigned long)pprev;
	spin_unlock(&pgd_lock);
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

static inline void pgd_free(pgd_t *pgd)
{
	BUG_ON((unsigned long)pgd & (PAGE_SIZE-1));
	pgd_list_del(pgd);
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return (pte_t *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	void *p = (void *)get_zeroed_page(GFP_KERNEL|__GFP_REPEAT);
	if (!p)
		return NULL;
	return virt_to_page(p);
}

/* Should really implement gc for free page table pages. This could be
   done with a reference count in struct page. */

static inline void pte_free_kernel(pte_t *pte)
{
	BUG_ON((unsigned long)pte & (PAGE_SIZE-1));
	free_page((unsigned long)pte); 
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
} 

#define __pte_free_tlb(tlb,pte) tlb_remove_page((tlb),(pte))

#define __pmd_free_tlb(tlb,x)   tlb_remove_page((tlb),virt_to_page(x))
#define __pud_free_tlb(tlb,x)   tlb_remove_page((tlb),virt_to_page(x))

#endif /* _X86_64_PGALLOC_H */
