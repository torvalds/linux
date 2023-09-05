// SPDX-License-Identifier: GPL-2.0
#include <linux/pagewalk.h>
#include <linux/hugetlb.h>
#include <linux/bitops.h>
#include <linux/mmu_notifier.h>
#include <linux/mm_inline.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

/**
 * struct wp_walk - Private struct for pagetable walk callbacks
 * @range: Range for mmu notifiers
 * @tlbflush_start: Address of first modified pte
 * @tlbflush_end: Address of last modified pte + 1
 * @total: Total number of modified ptes
 */
struct wp_walk {
	struct mmu_notifier_range range;
	unsigned long tlbflush_start;
	unsigned long tlbflush_end;
	unsigned long total;
};

/**
 * wp_pte - Write-protect a pte
 * @pte: Pointer to the pte
 * @addr: The start of protecting virtual address
 * @end: The end of protecting virtual address
 * @walk: pagetable walk callback argument
 *
 * The function write-protects a pte and records the range in
 * virtual address space of touched ptes for efficient range TLB flushes.
 */
static int wp_pte(pte_t *pte, unsigned long addr, unsigned long end,
		  struct mm_walk *walk)
{
	struct wp_walk *wpwalk = walk->private;
	pte_t ptent = ptep_get(pte);

	if (pte_write(ptent)) {
		pte_t old_pte = ptep_modify_prot_start(walk->vma, addr, pte);

		ptent = pte_wrprotect(old_pte);
		ptep_modify_prot_commit(walk->vma, addr, pte, old_pte, ptent);
		wpwalk->total++;
		wpwalk->tlbflush_start = min(wpwalk->tlbflush_start, addr);
		wpwalk->tlbflush_end = max(wpwalk->tlbflush_end,
					   addr + PAGE_SIZE);
	}

	return 0;
}

/**
 * struct clean_walk - Private struct for the clean_record_pte function.
 * @base: struct wp_walk we derive from
 * @bitmap_pgoff: Address_space Page offset of the first bit in @bitmap
 * @bitmap: Bitmap with one bit for each page offset in the address_space range
 * covered.
 * @start: Address_space page offset of first modified pte relative
 * to @bitmap_pgoff
 * @end: Address_space page offset of last modified pte relative
 * to @bitmap_pgoff
 */
struct clean_walk {
	struct wp_walk base;
	pgoff_t bitmap_pgoff;
	unsigned long *bitmap;
	pgoff_t start;
	pgoff_t end;
};

#define to_clean_walk(_wpwalk) container_of(_wpwalk, struct clean_walk, base)

/**
 * clean_record_pte - Clean a pte and record its address space offset in a
 * bitmap
 * @pte: Pointer to the pte
 * @addr: The start of virtual address to be clean
 * @end: The end of virtual address to be clean
 * @walk: pagetable walk callback argument
 *
 * The function cleans a pte and records the range in
 * virtual address space of touched ptes for efficient TLB flushes.
 * It also records dirty ptes in a bitmap representing page offsets
 * in the address_space, as well as the first and last of the bits
 * touched.
 */
static int clean_record_pte(pte_t *pte, unsigned long addr,
			    unsigned long end, struct mm_walk *walk)
{
	struct wp_walk *wpwalk = walk->private;
	struct clean_walk *cwalk = to_clean_walk(wpwalk);
	pte_t ptent = ptep_get(pte);

	if (pte_dirty(ptent)) {
		pgoff_t pgoff = ((addr - walk->vma->vm_start) >> PAGE_SHIFT) +
			walk->vma->vm_pgoff - cwalk->bitmap_pgoff;
		pte_t old_pte = ptep_modify_prot_start(walk->vma, addr, pte);

		ptent = pte_mkclean(old_pte);
		ptep_modify_prot_commit(walk->vma, addr, pte, old_pte, ptent);

		wpwalk->total++;
		wpwalk->tlbflush_start = min(wpwalk->tlbflush_start, addr);
		wpwalk->tlbflush_end = max(wpwalk->tlbflush_end,
					   addr + PAGE_SIZE);

		__set_bit(pgoff, cwalk->bitmap);
		cwalk->start = min(cwalk->start, pgoff);
		cwalk->end = max(cwalk->end, pgoff + 1);
	}

	return 0;
}

/*
 * wp_clean_pmd_entry - The pagewalk pmd callback.
 *
 * Dirty-tracking should take place on the PTE level, so
 * WARN() if encountering a dirty huge pmd.
 * Furthermore, never split huge pmds, since that currently
 * causes dirty info loss. The pagefault handler should do
 * that if needed.
 */
static int wp_clean_pmd_entry(pmd_t *pmd, unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
	pmd_t pmdval = pmdp_get_lockless(pmd);

	/* Do not split a huge pmd, present or migrated */
	if (pmd_trans_huge(pmdval) || pmd_devmap(pmdval)) {
		WARN_ON(pmd_write(pmdval) || pmd_dirty(pmdval));
		walk->action = ACTION_CONTINUE;
	}
	return 0;
}

/*
 * wp_clean_pud_entry - The pagewalk pud callback.
 *
 * Dirty-tracking should take place on the PTE level, so
 * WARN() if encountering a dirty huge puds.
 * Furthermore, never split huge puds, since that currently
 * causes dirty info loss. The pagefault handler should do
 * that if needed.
 */
static int wp_clean_pud_entry(pud_t *pud, unsigned long addr, unsigned long end,
			      struct mm_walk *walk)
{
#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
	pud_t pudval = READ_ONCE(*pud);

	/* Do not split a huge pud */
	if (pud_trans_huge(pudval) || pud_devmap(pudval)) {
		WARN_ON(pud_write(pudval) || pud_dirty(pudval));
		walk->action = ACTION_CONTINUE;
	}
#endif
	return 0;
}

/*
 * wp_clean_pre_vma - The pagewalk pre_vma callback.
 *
 * The pre_vma callback performs the cache flush, stages the tlb flush
 * and calls the necessary mmu notifiers.
 */
static int wp_clean_pre_vma(unsigned long start, unsigned long end,
			    struct mm_walk *walk)
{
	struct wp_walk *wpwalk = walk->private;

	wpwalk->tlbflush_start = end;
	wpwalk->tlbflush_end = start;

	mmu_notifier_range_init(&wpwalk->range, MMU_NOTIFY_PROTECTION_PAGE, 0,
				walk->mm, start, end);
	mmu_notifier_invalidate_range_start(&wpwalk->range);
	flush_cache_range(walk->vma, start, end);

	/*
	 * We're not using tlb_gather_mmu() since typically
	 * only a small subrange of PTEs are affected, whereas
	 * tlb_gather_mmu() records the full range.
	 */
	inc_tlb_flush_pending(walk->mm);

	return 0;
}

/*
 * wp_clean_post_vma - The pagewalk post_vma callback.
 *
 * The post_vma callback performs the tlb flush and calls necessary mmu
 * notifiers.
 */
static void wp_clean_post_vma(struct mm_walk *walk)
{
	struct wp_walk *wpwalk = walk->private;

	if (mm_tlb_flush_nested(walk->mm))
		flush_tlb_range(walk->vma, wpwalk->range.start,
				wpwalk->range.end);
	else if (wpwalk->tlbflush_end > wpwalk->tlbflush_start)
		flush_tlb_range(walk->vma, wpwalk->tlbflush_start,
				wpwalk->tlbflush_end);

	mmu_notifier_invalidate_range_end(&wpwalk->range);
	dec_tlb_flush_pending(walk->mm);
}

/*
 * wp_clean_test_walk - The pagewalk test_walk callback.
 *
 * Won't perform dirty-tracking on COW, read-only or HUGETLB vmas.
 */
static int wp_clean_test_walk(unsigned long start, unsigned long end,
			      struct mm_walk *walk)
{
	unsigned long vm_flags = READ_ONCE(walk->vma->vm_flags);

	/* Skip non-applicable VMAs */
	if ((vm_flags & (VM_SHARED | VM_MAYWRITE | VM_HUGETLB)) !=
	    (VM_SHARED | VM_MAYWRITE))
		return 1;

	return 0;
}

static const struct mm_walk_ops clean_walk_ops = {
	.pte_entry = clean_record_pte,
	.pmd_entry = wp_clean_pmd_entry,
	.pud_entry = wp_clean_pud_entry,
	.test_walk = wp_clean_test_walk,
	.pre_vma = wp_clean_pre_vma,
	.post_vma = wp_clean_post_vma
};

static const struct mm_walk_ops wp_walk_ops = {
	.pte_entry = wp_pte,
	.pmd_entry = wp_clean_pmd_entry,
	.pud_entry = wp_clean_pud_entry,
	.test_walk = wp_clean_test_walk,
	.pre_vma = wp_clean_pre_vma,
	.post_vma = wp_clean_post_vma
};

/**
 * wp_shared_mapping_range - Write-protect all ptes in an address space range
 * @mapping: The address_space we want to write protect
 * @first_index: The first page offset in the range
 * @nr: Number of incremental page offsets to cover
 *
 * Note: This function currently skips transhuge page-table entries, since
 * it's intended for dirty-tracking on the PTE level. It will warn on
 * encountering transhuge write-enabled entries, though, and can easily be
 * extended to handle them as well.
 *
 * Return: The number of ptes actually write-protected. Note that
 * already write-protected ptes are not counted.
 */
unsigned long wp_shared_mapping_range(struct address_space *mapping,
				      pgoff_t first_index, pgoff_t nr)
{
	struct wp_walk wpwalk = { .total = 0 };

	i_mmap_lock_read(mapping);
	WARN_ON(walk_page_mapping(mapping, first_index, nr, &wp_walk_ops,
				  &wpwalk));
	i_mmap_unlock_read(mapping);

	return wpwalk.total;
}
EXPORT_SYMBOL_GPL(wp_shared_mapping_range);

/**
 * clean_record_shared_mapping_range - Clean and record all ptes in an
 * address space range
 * @mapping: The address_space we want to clean
 * @first_index: The first page offset in the range
 * @nr: Number of incremental page offsets to cover
 * @bitmap_pgoff: The page offset of the first bit in @bitmap
 * @bitmap: Pointer to a bitmap of at least @nr bits. The bitmap needs to
 * cover the whole range @first_index..@first_index + @nr.
 * @start: Pointer to number of the first set bit in @bitmap.
 * is modified as new bits are set by the function.
 * @end: Pointer to the number of the last set bit in @bitmap.
 * none set. The value is modified as new bits are set by the function.
 *
 * Note: When this function returns there is no guarantee that a CPU has
 * not already dirtied new ptes. However it will not clean any ptes not
 * reported in the bitmap. The guarantees are as follows:
 * a) All ptes dirty when the function starts executing will end up recorded
 *    in the bitmap.
 * b) All ptes dirtied after that will either remain dirty, be recorded in the
 *    bitmap or both.
 *
 * If a caller needs to make sure all dirty ptes are picked up and none
 * additional are added, it first needs to write-protect the address-space
 * range and make sure new writers are blocked in page_mkwrite() or
 * pfn_mkwrite(). And then after a TLB flush following the write-protection
 * pick up all dirty bits.
 *
 * This function currently skips transhuge page-table entries, since
 * it's intended for dirty-tracking on the PTE level. It will warn on
 * encountering transhuge dirty entries, though, and can easily be extended
 * to handle them as well.
 *
 * Return: The number of dirty ptes actually cleaned.
 */
unsigned long clean_record_shared_mapping_range(struct address_space *mapping,
						pgoff_t first_index, pgoff_t nr,
						pgoff_t bitmap_pgoff,
						unsigned long *bitmap,
						pgoff_t *start,
						pgoff_t *end)
{
	bool none_set = (*start >= *end);
	struct clean_walk cwalk = {
		.base = { .total = 0 },
		.bitmap_pgoff = bitmap_pgoff,
		.bitmap = bitmap,
		.start = none_set ? nr : *start,
		.end = none_set ? 0 : *end,
	};

	i_mmap_lock_read(mapping);
	WARN_ON(walk_page_mapping(mapping, first_index, nr, &clean_walk_ops,
				  &cwalk.base));
	i_mmap_unlock_read(mapping);

	*start = cwalk.start;
	*end = cwalk.end;

	return cwalk.base.total;
}
EXPORT_SYMBOL_GPL(clean_record_shared_mapping_range);
