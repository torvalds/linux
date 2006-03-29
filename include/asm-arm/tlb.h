/*
 *  linux/include/asm-arm/tlb.h
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Experimentation shows that on a StrongARM, it appears to be faster
 *  to use the "invalidate whole tlb" rather than "invalidate single
 *  tlb" for this.
 *
 *  This appears true for both the process fork+exit case, as well as
 *  the munmap-large-area case.
 */
#ifndef __ASMARM_TLB_H
#define __ASMARM_TLB_H

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#ifndef CONFIG_MMU

#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#else /* !CONFIG_MMU */

#include <asm/pgalloc.h>

/*
 * TLB handling.  This allows us to remove pages from the page
 * tables, and efficiently handle the TLB issues.
 */
struct mmu_gather {
	struct mm_struct	*mm;
	unsigned int		fullmm;
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
	struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

	tlb->mm = mm;
	tlb->fullmm = full_mm_flush;

	return tlb;
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
	if (tlb->fullmm)
		flush_tlb_mm(tlb->mm);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	put_cpu_var(mmu_gathers);
}

#define tlb_remove_tlb_entry(tlb,ptep,address)	do { } while (0)

/*
 * In the case of tlb vma handling, we can optimise these away in the
 * case where we're doing a full MM flush.  When we're doing a munmap,
 * the vmas are adjusted to only cover the region to be torn down.
 */
static inline void
tlb_start_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm)
		flush_cache_range(vma, vma->vm_start, vma->vm_end);
}

static inline void
tlb_end_vma(struct mmu_gather *tlb, struct vm_area_struct *vma)
{
	if (!tlb->fullmm)
		flush_tlb_range(vma, vma->vm_start, vma->vm_end);
}

#define tlb_remove_page(tlb,page)	free_page_and_swap_cache(page)
#define pte_free_tlb(tlb,ptep)		pte_free(ptep)
#define pmd_free_tlb(tlb,pmdp)		pmd_free(pmdp)

#define tlb_migrate_finish(mm)		do { } while (0)

#endif /* CONFIG_MMU */
#endif
