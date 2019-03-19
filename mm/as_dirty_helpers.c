// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/hugetlb.h>
#include <linux/bitops.h>
#include <linux/mmu_notifier.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

/**
 * struct apply_as - Closure structure for apply_as_range
 * @base: struct pfn_range_apply we derive from
 * @start: Address of first modified pte
 * @end: Address of last modified pte + 1
 * @total: Total number of modified ptes
 * @vma: Pointer to the struct vm_area_struct we're currently operating on
 */
struct apply_as {
	struct pfn_range_apply base;
	unsigned long start;
	unsigned long end;
	unsigned long total;
	struct vm_area_struct *vma;
};

/**
 * apply_pt_wrprotect - Leaf pte callback to write-protect a pte
 * @pte: Pointer to the pte
 * @token: Page table token, see apply_to_pfn_range()
 * @addr: The virtual page address
 * @closure: Pointer to a struct pfn_range_apply embedded in a
 * struct apply_as
 *
 * The function write-protects a pte and records the range in
 * virtual address space of touched ptes for efficient range TLB flushes.
 *
 * Return: Always zero.
 */
static int apply_pt_wrprotect(pte_t *pte, pgtable_t token,
			      unsigned long addr,
			      struct pfn_range_apply *closure)
{
	struct apply_as *aas = container_of(closure, typeof(*aas), base);
	pte_t ptent = *pte;

	if (pte_write(ptent)) {
		pte_t old_pte = ptep_modify_prot_start(aas->vma, addr, pte);

		ptent = pte_wrprotect(old_pte);
		ptep_modify_prot_commit(aas->vma, addr, pte, old_pte, ptent);
		aas->total++;
		aas->start = min(aas->start, addr);
		aas->end = max(aas->end, addr + PAGE_SIZE);
	}

	return 0;
}

/**
 * struct apply_as_clean - Closure structure for apply_as_clean
 * @base: struct apply_as we derive from
 * @bitmap_pgoff: Address_space Page offset of the first bit in @bitmap
 * @bitmap: Bitmap with one bit for each page offset in the address_space range
 * covered.
 * @start: Address_space page offset of first modified pte relative
 * to @bitmap_pgoff
 * @end: Address_space page offset of last modified pte relative
 * to @bitmap_pgoff
 */
struct apply_as_clean {
	struct apply_as base;
	pgoff_t bitmap_pgoff;
	unsigned long *bitmap;
	pgoff_t start;
	pgoff_t end;
};

/**
 * apply_pt_clean - Leaf pte callback to clean a pte
 * @pte: Pointer to the pte
 * @token: Page table token, see apply_to_pfn_range()
 * @addr: The virtual page address
 * @closure: Pointer to a struct pfn_range_apply embedded in a
 * struct apply_as_clean
 *
 * The function cleans a pte and records the range in
 * virtual address space of touched ptes for efficient TLB flushes.
 * It also records dirty ptes in a bitmap representing page offsets
 * in the address_space, as well as the first and last of the bits
 * touched.
 *
 * Return: Always zero.
 */
static int apply_pt_clean(pte_t *pte, pgtable_t token,
			  unsigned long addr,
			  struct pfn_range_apply *closure)
{
	struct apply_as *aas = container_of(closure, typeof(*aas), base);
	struct apply_as_clean *clean = container_of(aas, typeof(*clean), base);
	pte_t ptent = *pte;

	if (pte_dirty(ptent)) {
		pgoff_t pgoff = ((addr - aas->vma->vm_start) >> PAGE_SHIFT) +
			aas->vma->vm_pgoff - clean->bitmap_pgoff;
		pte_t old_pte = ptep_modify_prot_start(aas->vma, addr, pte);

		ptent = pte_mkclean(old_pte);
		ptep_modify_prot_commit(aas->vma, addr, pte, old_pte, ptent);

		aas->total++;
		aas->start = min(aas->start, addr);
		aas->end = max(aas->end, addr + PAGE_SIZE);

		__set_bit(pgoff, clean->bitmap);
		clean->start = min(clean->start, pgoff);
		clean->end = max(clean->end, pgoff + 1);
	}

	return 0;
}

/**
 * apply_as_range - Apply a pte callback to all PTEs pointing into a range
 * of an address_space.
 * @mapping: Pointer to the struct address_space
 * @aas: Closure structure
 * @first_index: First page offset in the address_space
 * @nr: Number of incremental page offsets to cover
 *
 * Return: Number of ptes touched. Note that this number might be larger
 * than @nr if there are overlapping vmas
 */
static unsigned long apply_as_range(struct address_space *mapping,
				    struct apply_as *aas,
				    pgoff_t first_index, pgoff_t nr)
{
	struct vm_area_struct *vma;
	pgoff_t vba, vea, cba, cea;
	unsigned long start_addr, end_addr;
	struct mmu_notifier_range range;

	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, first_index,
				  first_index + nr - 1) {
		unsigned long vm_flags = READ_ONCE(vma->vm_flags);

		/*
		 * We can only do advisory flag tests below, since we can't
		 * require the vm's mmap_sem to be held to protect the flags.
		 * Therefore, callers that strictly depend on specific mmap
		 * flags to remain constant throughout the operation must
		 * either ensure those flags are immutable for all relevant
		 * vmas or can't use this function. Fixing this properly would
		 * require the vma::vm_flags to be protected by a separate
		 * lock taken after the i_mmap_lock
		 */

		/* Skip non-applicable VMAs */
		if ((vm_flags & (VM_SHARED | VM_WRITE)) !=
		    (VM_SHARED | VM_WRITE))
			continue;

		/* Warn on and skip VMAs whose flags indicate illegal usage */
		if (WARN_ON((vm_flags & (VM_HUGETLB | VM_IO)) != VM_IO))
			continue;

		/* Clip to the vma */
		vba = vma->vm_pgoff;
		vea = vba + vma_pages(vma);
		cba = first_index;
		cba = max(cba, vba);
		cea = first_index + nr;
		cea = min(cea, vea);

		/* Translate to virtual address */
		start_addr = ((cba - vba) << PAGE_SHIFT) + vma->vm_start;
		end_addr = ((cea - vba) << PAGE_SHIFT) + vma->vm_start;
		if (start_addr >= end_addr)
			continue;

		aas->base.mm = vma->vm_mm;
		aas->vma = vma;
		aas->start = end_addr;
		aas->end = start_addr;

		mmu_notifier_range_init(&range, MMU_NOTIFY_PROTECTION_PAGE, 0,
					vma, vma->vm_mm, start_addr, end_addr);
		mmu_notifier_invalidate_range_start(&range);

		/* Needed when we only change protection? */
		flush_cache_range(vma, start_addr, end_addr);

		/*
		 * We're not using tlb_gather_mmu() since typically
		 * only a small subrange of PTEs are affected.
		 */
		inc_tlb_flush_pending(vma->vm_mm);

		/* Should not error since aas->base.alloc == 0 */
		WARN_ON(apply_to_pfn_range(&aas->base, start_addr,
					   end_addr - start_addr));
		if (aas->end > aas->start)
			flush_tlb_range(vma, aas->start, aas->end);

		mmu_notifier_invalidate_range_end(&range);
		dec_tlb_flush_pending(vma->vm_mm);
	}
	i_mmap_unlock_read(mapping);

	return aas->total;
}

/**
 * apply_as_wrprotect - Write-protect all ptes in an address_space range
 * @mapping: The address_space we want to write protect
 * @first_index: The first page offset in the range
 * @nr: Number of incremental page offsets to cover
 *
 * WARNING: This function should only be used for address spaces whose
 * vmas are marked VM_IO and that do not contain huge pages.
 * To avoid interference with COW'd pages, vmas not marked VM_SHARED are
 * simply skipped.
 *
 * Return: The number of ptes actually write-protected. Note that
 * already write-protected ptes are not counted.
 */
unsigned long apply_as_wrprotect(struct address_space *mapping,
				 pgoff_t first_index, pgoff_t nr)
{
	struct apply_as aas = {
		.base = {
			.alloc = 0,
			.ptefn = apply_pt_wrprotect,
		},
		.total = 0,
	};

	return apply_as_range(mapping, &aas, first_index, nr);
}
EXPORT_SYMBOL_GPL(apply_as_wrprotect);

/**
 * apply_as_clean - Clean all ptes in an address_space range
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
 * reported in the bitmap.
 *
 * If a caller needs to make sure all dirty ptes are picked up and none
 * additional are added, it first needs to write-protect the address-space
 * range and make sure new writers are blocked in page_mkwrite() or
 * pfn_mkwrite(). And then after a TLB flush following the write-protection
 * pick up all dirty bits.
 *
 * WARNING: This function should only be used for address spaces whose
 * vmas are marked VM_IO and that do not contain huge pages.
 * To avoid interference with COW'd pages, vmas not marked VM_SHARED are
 * simply skipped.
 *
 * Return: The number of dirty ptes actually cleaned.
 */
unsigned long apply_as_clean(struct address_space *mapping,
			     pgoff_t first_index, pgoff_t nr,
			     pgoff_t bitmap_pgoff,
			     unsigned long *bitmap,
			     pgoff_t *start,
			     pgoff_t *end)
{
	bool none_set = (*start >= *end);
	struct apply_as_clean clean = {
		.base = {
			.base = {
				.alloc = 0,
				.ptefn = apply_pt_clean,
			},
			.total = 0,
		},
		.bitmap_pgoff = bitmap_pgoff,
		.bitmap = bitmap,
		.start = none_set ? nr : *start,
		.end = none_set ? 0 : *end,
	};
	unsigned long ret = apply_as_range(mapping, &clean.base, first_index,
					   nr);

	*start = clean.start;
	*end = clean.end;
	return ret;
}
EXPORT_SYMBOL_GPL(apply_as_clean);
