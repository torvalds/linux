// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2013 Red Hat Inc.
 *
 * Authors: Jérôme Glisse <jglisse@redhat.com>
 */
/*
 * Refer to include/linux/hmm.h for information about heterogeneous memory
 * management or HMM for short.
 */
#include <linux/pagewalk.h>
#include <linux/hmm.h>
#include <linux/init.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mmzone.h>
#include <linux/pagemap.h>
#include <linux/swapops.h>
#include <linux/hugetlb.h>
#include <linux/memremap.h>
#include <linux/sched/mm.h>
#include <linux/jump_label.h>
#include <linux/dma-mapping.h>
#include <linux/mmu_notifier.h>
#include <linux/memory_hotplug.h>

struct hmm_vma_walk {
	struct hmm_range	*range;
	unsigned long		last;
};

enum {
	HMM_NEED_FAULT = 1 << 0,
	HMM_NEED_WRITE_FAULT = 1 << 1,
	HMM_NEED_ALL_BITS = HMM_NEED_FAULT | HMM_NEED_WRITE_FAULT,
};

static int hmm_pfns_fill(unsigned long addr, unsigned long end,
			 struct hmm_range *range, unsigned long cpu_flags)
{
	unsigned long i = (addr - range->start) >> PAGE_SHIFT;

	for (; addr < end; addr += PAGE_SIZE, i++)
		range->hmm_pfns[i] = cpu_flags;
	return 0;
}

/*
 * hmm_vma_fault() - fault in a range lacking valid pmd or pte(s)
 * @addr: range virtual start address (inclusive)
 * @end: range virtual end address (exclusive)
 * @required_fault: HMM_NEED_* flags
 * @walk: mm_walk structure
 * Return: -EBUSY after page fault, or page fault error
 *
 * This function will be called whenever pmd_none() or pte_none() returns true,
 * or whenever there is no page directory covering the virtual address range.
 */
static int hmm_vma_fault(unsigned long addr, unsigned long end,
			 unsigned int required_fault, struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct vm_area_struct *vma = walk->vma;
	unsigned int fault_flags = FAULT_FLAG_REMOTE;

	WARN_ON_ONCE(!required_fault);
	hmm_vma_walk->last = addr;

	if (required_fault & HMM_NEED_WRITE_FAULT) {
		if (!(vma->vm_flags & VM_WRITE))
			return -EPERM;
		fault_flags |= FAULT_FLAG_WRITE;
	}

	for (; addr < end; addr += PAGE_SIZE)
		if (handle_mm_fault(vma, addr, fault_flags, NULL) &
		    VM_FAULT_ERROR)
			return -EFAULT;
	return -EBUSY;
}

static unsigned int hmm_pte_need_fault(const struct hmm_vma_walk *hmm_vma_walk,
				       unsigned long pfn_req_flags,
				       unsigned long cpu_flags)
{
	struct hmm_range *range = hmm_vma_walk->range;

	/*
	 * So we not only consider the individual per page request we also
	 * consider the default flags requested for the range. The API can
	 * be used 2 ways. The first one where the HMM user coalesces
	 * multiple page faults into one request and sets flags per pfn for
	 * those faults. The second one where the HMM user wants to pre-
	 * fault a range with specific flags. For the latter one it is a
	 * waste to have the user pre-fill the pfn arrays with a default
	 * flags value.
	 */
	pfn_req_flags &= range->pfn_flags_mask;
	pfn_req_flags |= range->default_flags;

	/* We aren't ask to do anything ... */
	if (!(pfn_req_flags & HMM_PFN_REQ_FAULT))
		return 0;

	/* Need to write fault ? */
	if ((pfn_req_flags & HMM_PFN_REQ_WRITE) &&
	    !(cpu_flags & HMM_PFN_WRITE))
		return HMM_NEED_FAULT | HMM_NEED_WRITE_FAULT;

	/* If CPU page table is not valid then we need to fault */
	if (!(cpu_flags & HMM_PFN_VALID))
		return HMM_NEED_FAULT;
	return 0;
}

static unsigned int
hmm_range_need_fault(const struct hmm_vma_walk *hmm_vma_walk,
		     const unsigned long hmm_pfns[], unsigned long npages,
		     unsigned long cpu_flags)
{
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned int required_fault = 0;
	unsigned long i;

	/*
	 * If the default flags do not request to fault pages, and the mask does
	 * not allow for individual pages to be faulted, then
	 * hmm_pte_need_fault() will always return 0.
	 */
	if (!((range->default_flags | range->pfn_flags_mask) &
	      HMM_PFN_REQ_FAULT))
		return 0;

	for (i = 0; i < npages; ++i) {
		required_fault |= hmm_pte_need_fault(hmm_vma_walk, hmm_pfns[i],
						     cpu_flags);
		if (required_fault == HMM_NEED_ALL_BITS)
			return required_fault;
	}
	return required_fault;
}

static int hmm_vma_walk_hole(unsigned long addr, unsigned long end,
			     __always_unused int depth, struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned int required_fault;
	unsigned long i, npages;
	unsigned long *hmm_pfns;

	i = (addr - range->start) >> PAGE_SHIFT;
	npages = (end - addr) >> PAGE_SHIFT;
	hmm_pfns = &range->hmm_pfns[i];
	required_fault =
		hmm_range_need_fault(hmm_vma_walk, hmm_pfns, npages, 0);
	if (!walk->vma) {
		if (required_fault)
			return -EFAULT;
		return hmm_pfns_fill(addr, end, range, HMM_PFN_ERROR);
	}
	if (required_fault)
		return hmm_vma_fault(addr, end, required_fault, walk);
	return hmm_pfns_fill(addr, end, range, 0);
}

static inline unsigned long hmm_pfn_flags_order(unsigned long order)
{
	return order << HMM_PFN_ORDER_SHIFT;
}

static inline unsigned long pmd_to_hmm_pfn_flags(struct hmm_range *range,
						 pmd_t pmd)
{
	if (pmd_protnone(pmd))
		return 0;
	return (pmd_write(pmd) ? (HMM_PFN_VALID | HMM_PFN_WRITE) :
				 HMM_PFN_VALID) |
	       hmm_pfn_flags_order(PMD_SHIFT - PAGE_SHIFT);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static int hmm_vma_handle_pmd(struct mm_walk *walk, unsigned long addr,
			      unsigned long end, unsigned long hmm_pfns[],
			      pmd_t pmd)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned long pfn, npages, i;
	unsigned int required_fault;
	unsigned long cpu_flags;

	npages = (end - addr) >> PAGE_SHIFT;
	cpu_flags = pmd_to_hmm_pfn_flags(range, pmd);
	required_fault =
		hmm_range_need_fault(hmm_vma_walk, hmm_pfns, npages, cpu_flags);
	if (required_fault)
		return hmm_vma_fault(addr, end, required_fault, walk);

	pfn = pmd_pfn(pmd) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	for (i = 0; addr < end; addr += PAGE_SIZE, i++, pfn++)
		hmm_pfns[i] = pfn | cpu_flags;
	return 0;
}
#else /* CONFIG_TRANSPARENT_HUGEPAGE */
/* stub to allow the code below to compile */
int hmm_vma_handle_pmd(struct mm_walk *walk, unsigned long addr,
		unsigned long end, unsigned long hmm_pfns[], pmd_t pmd);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static inline bool hmm_is_device_private_entry(struct hmm_range *range,
		swp_entry_t entry)
{
	return is_device_private_entry(entry) &&
		pfn_swap_entry_to_page(entry)->pgmap->owner ==
		range->dev_private_owner;
}

static inline unsigned long pte_to_hmm_pfn_flags(struct hmm_range *range,
						 pte_t pte)
{
	if (pte_none(pte) || !pte_present(pte) || pte_protnone(pte))
		return 0;
	return pte_write(pte) ? (HMM_PFN_VALID | HMM_PFN_WRITE) : HMM_PFN_VALID;
}

static int hmm_vma_handle_pte(struct mm_walk *walk, unsigned long addr,
			      unsigned long end, pmd_t *pmdp, pte_t *ptep,
			      unsigned long *hmm_pfn)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned int required_fault;
	unsigned long cpu_flags;
	pte_t pte = *ptep;
	uint64_t pfn_req_flags = *hmm_pfn;

	if (pte_none(pte)) {
		required_fault =
			hmm_pte_need_fault(hmm_vma_walk, pfn_req_flags, 0);
		if (required_fault)
			goto fault;
		*hmm_pfn = 0;
		return 0;
	}

	if (!pte_present(pte)) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		/*
		 * Never fault in device private pages, but just report
		 * the PFN even if not present.
		 */
		if (hmm_is_device_private_entry(range, entry)) {
			cpu_flags = HMM_PFN_VALID;
			if (is_writable_device_private_entry(entry))
				cpu_flags |= HMM_PFN_WRITE;
			*hmm_pfn = swp_offset(entry) | cpu_flags;
			return 0;
		}

		required_fault =
			hmm_pte_need_fault(hmm_vma_walk, pfn_req_flags, 0);
		if (!required_fault) {
			*hmm_pfn = 0;
			return 0;
		}

		if (!non_swap_entry(entry))
			goto fault;

		if (is_migration_entry(entry)) {
			pte_unmap(ptep);
			hmm_vma_walk->last = addr;
			migration_entry_wait(walk->mm, pmdp, addr);
			return -EBUSY;
		}

		/* Report error for everything else */
		pte_unmap(ptep);
		return -EFAULT;
	}

	cpu_flags = pte_to_hmm_pfn_flags(range, pte);
	required_fault =
		hmm_pte_need_fault(hmm_vma_walk, pfn_req_flags, cpu_flags);
	if (required_fault)
		goto fault;

	/*
	 * Since each architecture defines a struct page for the zero page, just
	 * fall through and treat it like a normal page.
	 */
	if (pte_special(pte) && !is_zero_pfn(pte_pfn(pte))) {
		if (hmm_pte_need_fault(hmm_vma_walk, pfn_req_flags, 0)) {
			pte_unmap(ptep);
			return -EFAULT;
		}
		*hmm_pfn = HMM_PFN_ERROR;
		return 0;
	}

	*hmm_pfn = pte_pfn(pte) | cpu_flags;
	return 0;

fault:
	pte_unmap(ptep);
	/* Fault any virtual address we were asked to fault */
	return hmm_vma_fault(addr, end, required_fault, walk);
}

static int hmm_vma_walk_pmd(pmd_t *pmdp,
			    unsigned long start,
			    unsigned long end,
			    struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned long *hmm_pfns =
		&range->hmm_pfns[(start - range->start) >> PAGE_SHIFT];
	unsigned long npages = (end - start) >> PAGE_SHIFT;
	unsigned long addr = start;
	pte_t *ptep;
	pmd_t pmd;

again:
	pmd = READ_ONCE(*pmdp);
	if (pmd_none(pmd))
		return hmm_vma_walk_hole(start, end, -1, walk);

	if (thp_migration_supported() && is_pmd_migration_entry(pmd)) {
		if (hmm_range_need_fault(hmm_vma_walk, hmm_pfns, npages, 0)) {
			hmm_vma_walk->last = addr;
			pmd_migration_entry_wait(walk->mm, pmdp);
			return -EBUSY;
		}
		return hmm_pfns_fill(start, end, range, 0);
	}

	if (!pmd_present(pmd)) {
		if (hmm_range_need_fault(hmm_vma_walk, hmm_pfns, npages, 0))
			return -EFAULT;
		return hmm_pfns_fill(start, end, range, HMM_PFN_ERROR);
	}

	if (pmd_devmap(pmd) || pmd_trans_huge(pmd)) {
		/*
		 * No need to take pmd_lock here, even if some other thread
		 * is splitting the huge pmd we will get that event through
		 * mmu_notifier callback.
		 *
		 * So just read pmd value and check again it's a transparent
		 * huge or device mapping one and compute corresponding pfn
		 * values.
		 */
		pmd = pmd_read_atomic(pmdp);
		barrier();
		if (!pmd_devmap(pmd) && !pmd_trans_huge(pmd))
			goto again;

		return hmm_vma_handle_pmd(walk, addr, end, hmm_pfns, pmd);
	}

	/*
	 * We have handled all the valid cases above ie either none, migration,
	 * huge or transparent huge. At this point either it is a valid pmd
	 * entry pointing to pte directory or it is a bad pmd that will not
	 * recover.
	 */
	if (pmd_bad(pmd)) {
		if (hmm_range_need_fault(hmm_vma_walk, hmm_pfns, npages, 0))
			return -EFAULT;
		return hmm_pfns_fill(start, end, range, HMM_PFN_ERROR);
	}

	ptep = pte_offset_map(pmdp, addr);
	for (; addr < end; addr += PAGE_SIZE, ptep++, hmm_pfns++) {
		int r;

		r = hmm_vma_handle_pte(walk, addr, end, pmdp, ptep, hmm_pfns);
		if (r) {
			/* hmm_vma_handle_pte() did pte_unmap() */
			return r;
		}
	}
	pte_unmap(ptep - 1);
	return 0;
}

#if defined(CONFIG_ARCH_HAS_PTE_DEVMAP) && \
    defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
static inline unsigned long pud_to_hmm_pfn_flags(struct hmm_range *range,
						 pud_t pud)
{
	if (!pud_present(pud))
		return 0;
	return (pud_write(pud) ? (HMM_PFN_VALID | HMM_PFN_WRITE) :
				 HMM_PFN_VALID) |
	       hmm_pfn_flags_order(PUD_SHIFT - PAGE_SHIFT);
}

static int hmm_vma_walk_pud(pud_t *pudp, unsigned long start, unsigned long end,
		struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned long addr = start;
	pud_t pud;
	int ret = 0;
	spinlock_t *ptl = pud_trans_huge_lock(pudp, walk->vma);

	if (!ptl)
		return 0;

	/* Normally we don't want to split the huge page */
	walk->action = ACTION_CONTINUE;

	pud = READ_ONCE(*pudp);
	if (pud_none(pud)) {
		spin_unlock(ptl);
		return hmm_vma_walk_hole(start, end, -1, walk);
	}

	if (pud_huge(pud) && pud_devmap(pud)) {
		unsigned long i, npages, pfn;
		unsigned int required_fault;
		unsigned long *hmm_pfns;
		unsigned long cpu_flags;

		if (!pud_present(pud)) {
			spin_unlock(ptl);
			return hmm_vma_walk_hole(start, end, -1, walk);
		}

		i = (addr - range->start) >> PAGE_SHIFT;
		npages = (end - addr) >> PAGE_SHIFT;
		hmm_pfns = &range->hmm_pfns[i];

		cpu_flags = pud_to_hmm_pfn_flags(range, pud);
		required_fault = hmm_range_need_fault(hmm_vma_walk, hmm_pfns,
						      npages, cpu_flags);
		if (required_fault) {
			spin_unlock(ptl);
			return hmm_vma_fault(addr, end, required_fault, walk);
		}

		pfn = pud_pfn(pud) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
		for (i = 0; i < npages; ++i, ++pfn)
			hmm_pfns[i] = pfn | cpu_flags;
		goto out_unlock;
	}

	/* Ask for the PUD to be split */
	walk->action = ACTION_SUBTREE;

out_unlock:
	spin_unlock(ptl);
	return ret;
}
#else
#define hmm_vma_walk_pud	NULL
#endif

#ifdef CONFIG_HUGETLB_PAGE
static int hmm_vma_walk_hugetlb_entry(pte_t *pte, unsigned long hmask,
				      unsigned long start, unsigned long end,
				      struct mm_walk *walk)
{
	unsigned long addr = start, i, pfn;
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	struct vm_area_struct *vma = walk->vma;
	unsigned int required_fault;
	unsigned long pfn_req_flags;
	unsigned long cpu_flags;
	spinlock_t *ptl;
	pte_t entry;

	ptl = huge_pte_lock(hstate_vma(vma), walk->mm, pte);
	entry = huge_ptep_get(pte);

	i = (start - range->start) >> PAGE_SHIFT;
	pfn_req_flags = range->hmm_pfns[i];
	cpu_flags = pte_to_hmm_pfn_flags(range, entry) |
		    hmm_pfn_flags_order(huge_page_order(hstate_vma(vma)));
	required_fault =
		hmm_pte_need_fault(hmm_vma_walk, pfn_req_flags, cpu_flags);
	if (required_fault) {
		spin_unlock(ptl);
		return hmm_vma_fault(addr, end, required_fault, walk);
	}

	pfn = pte_pfn(entry) + ((start & ~hmask) >> PAGE_SHIFT);
	for (; addr < end; addr += PAGE_SIZE, i++, pfn++)
		range->hmm_pfns[i] = pfn | cpu_flags;

	spin_unlock(ptl);
	return 0;
}
#else
#define hmm_vma_walk_hugetlb_entry NULL
#endif /* CONFIG_HUGETLB_PAGE */

static int hmm_vma_walk_test(unsigned long start, unsigned long end,
			     struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	struct vm_area_struct *vma = walk->vma;

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP | VM_MIXEDMAP)) &&
	    vma->vm_flags & VM_READ)
		return 0;

	/*
	 * vma ranges that don't have struct page backing them or map I/O
	 * devices directly cannot be handled by hmm_range_fault().
	 *
	 * If the vma does not allow read access, then assume that it does not
	 * allow write access either. HMM does not support architectures that
	 * allow write without read.
	 *
	 * If a fault is requested for an unsupported range then it is a hard
	 * failure.
	 */
	if (hmm_range_need_fault(hmm_vma_walk,
				 range->hmm_pfns +
					 ((start - range->start) >> PAGE_SHIFT),
				 (end - start) >> PAGE_SHIFT, 0))
		return -EFAULT;

	hmm_pfns_fill(start, end, range, HMM_PFN_ERROR);

	/* Skip this vma and continue processing the next vma. */
	return 1;
}

static const struct mm_walk_ops hmm_walk_ops = {
	.pud_entry	= hmm_vma_walk_pud,
	.pmd_entry	= hmm_vma_walk_pmd,
	.pte_hole	= hmm_vma_walk_hole,
	.hugetlb_entry	= hmm_vma_walk_hugetlb_entry,
	.test_walk	= hmm_vma_walk_test,
};

/**
 * hmm_range_fault - try to fault some address in a virtual address range
 * @range:	argument structure
 *
 * Returns 0 on success or one of the following error codes:
 *
 * -EINVAL:	Invalid arguments or mm or virtual address is in an invalid vma
 *		(e.g., device file vma).
 * -ENOMEM:	Out of memory.
 * -EPERM:	Invalid permission (e.g., asking for write and range is read
 *		only).
 * -EBUSY:	The range has been invalidated and the caller needs to wait for
 *		the invalidation to finish.
 * -EFAULT:     A page was requested to be valid and could not be made valid
 *              ie it has no backing VMA or it is illegal to access
 *
 * This is similar to get_user_pages(), except that it can read the page tables
 * without mutating them (ie causing faults).
 */
int hmm_range_fault(struct hmm_range *range)
{
	struct hmm_vma_walk hmm_vma_walk = {
		.range = range,
		.last = range->start,
	};
	struct mm_struct *mm = range->notifier->mm;
	int ret;

	mmap_assert_locked(mm);

	do {
		/* If range is no longer valid force retry. */
		if (mmu_interval_check_retry(range->notifier,
					     range->notifier_seq))
			return -EBUSY;
		ret = walk_page_range(mm, hmm_vma_walk.last, range->end,
				      &hmm_walk_ops, &hmm_vma_walk);
		/*
		 * When -EBUSY is returned the loop restarts with
		 * hmm_vma_walk.last set to an address that has not been stored
		 * in pfns. All entries < last in the pfn array are set to their
		 * output, and all >= are still at their input values.
		 */
	} while (ret == -EBUSY);
	return ret;
}
EXPORT_SYMBOL(hmm_range_fault);
