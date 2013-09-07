#ifndef __LINUX_UKSM_H
#define __LINUX_UKSM_H
/*
 * Memory merging support.
 *
 * This code enables dynamic sharing of identical pages found in different
 * memory areas, even if they are not shared by fork().
 */

/* if !CONFIG_UKSM this file should not be compiled at all. */
#ifdef CONFIG_UKSM

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/sched.h>

extern unsigned long zero_pfn __read_mostly;
extern unsigned long uksm_zero_pfn __read_mostly;
extern struct page *empty_uksm_zero_page;

/* must be done before linked to mm */
extern void uksm_vma_add_new(struct vm_area_struct *vma);
extern void uksm_remove_vma(struct vm_area_struct *vma);

#define UKSM_SLOT_NEED_SORT	(1 << 0)
#define UKSM_SLOT_NEED_RERAND 	(1 << 1)
#define UKSM_SLOT_SCANNED     	(1 << 2) /* It's scanned in this round */
#define UKSM_SLOT_FUL_SCANNED 	(1 << 3)
#define UKSM_SLOT_IN_UKSM 	(1 << 4)

struct vma_slot {
	struct sradix_tree_node *snode;
	unsigned long sindex;

	struct list_head slot_list;
	unsigned long fully_scanned_round;
	unsigned long dedup_num;
	unsigned long pages_scanned;
	unsigned long last_scanned;
	unsigned long pages_to_scan;
	struct scan_rung *rung;
	struct page **rmap_list_pool;
	unsigned int *pool_counts;
	unsigned long pool_size;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long ctime_j;
	unsigned long pages;
	unsigned long flags;
	unsigned long pages_cowed; /* pages cowed this round */
	unsigned long pages_merged; /* pages merged this round */
	unsigned long pages_bemerged;

	/* when it has page merged in this eval round */
	struct list_head dedup_list;
};

static inline void uksm_unmap_zero_page(pte_t pte)
{
	if (pte_pfn(pte) == uksm_zero_pfn)
		__dec_zone_page_state(empty_uksm_zero_page, NR_UKSM_ZERO_PAGES);
}

static inline void uksm_map_zero_page(pte_t pte)
{
	if (pte_pfn(pte) == uksm_zero_pfn)
		__inc_zone_page_state(empty_uksm_zero_page, NR_UKSM_ZERO_PAGES);
}

static inline void uksm_cow_page(struct vm_area_struct *vma, struct page *page)
{
	if (vma->uksm_vma_slot && PageKsm(page))
		vma->uksm_vma_slot->pages_cowed++;
}

static inline void uksm_cow_pte(struct vm_area_struct *vma, pte_t pte)
{
	if (vma->uksm_vma_slot && pte_pfn(pte) == uksm_zero_pfn)
		vma->uksm_vma_slot->pages_cowed++;
}

static inline int uksm_flags_can_scan(unsigned long vm_flags)
{
#ifndef VM_SAO
#define VM_SAO 0
#endif
	return !(vm_flags & (VM_PFNMAP | VM_IO  | VM_DONTEXPAND |
			     VM_HUGETLB | VM_NONLINEAR | VM_MIXEDMAP |
			     VM_SHARED  | VM_MAYSHARE | VM_GROWSUP | VM_GROWSDOWN | VM_SAO));
}

static inline void uksm_vm_flags_mod(unsigned long *vm_flags_p)
{
	if (uksm_flags_can_scan(*vm_flags_p))
		*vm_flags_p |= VM_MERGEABLE;
}

/*
 * Just a wrapper for BUG_ON for where ksm_zeropage must not be. TODO: it will
 * be removed when uksm zero page patch is stable enough.
 */
static inline void uksm_bugon_zeropage(pte_t pte)
{
	BUG_ON(pte_pfn(pte) == uksm_zero_pfn);
}
#else
static inline void uksm_vma_add_new(struct vm_area_struct *vma)
{
}

static inline void uksm_remove_vma(struct vm_area_struct *vma)
{
}

static inline void uksm_unmap_zero_page(pte_t pte)
{
}

static inline void uksm_map_zero_page(pte_t pte)
{
}

static inline void uksm_cow_page(struct vm_area_struct *vma, struct page *page)
{
}

static inline void uksm_cow_pte(struct vm_area_struct *vma, pte_t pte)
{
}

static inline int uksm_flags_can_scan(unsigned long vm_flags)
{
	return 0;
}

static inline void uksm_vm_flags_mod(unsigned long *vm_flags_p)
{
}

static inline void uksm_bugon_zeropage(pte_t pte)
{
}
#endif /* !CONFIG_UKSM */
#endif /* __LINUX_UKSM_H */
