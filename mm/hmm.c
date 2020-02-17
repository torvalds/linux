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
	struct dev_pagemap	*pgmap;
	unsigned long		last;
	unsigned int		flags;
};

static int hmm_vma_do_fault(struct mm_walk *walk, unsigned long addr,
			    bool write_fault, uint64_t *pfn)
{
	unsigned int flags = FAULT_FLAG_REMOTE;
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	struct vm_area_struct *vma = walk->vma;
	vm_fault_t ret;

	if (!vma)
		goto err;

	if (hmm_vma_walk->flags & HMM_FAULT_ALLOW_RETRY)
		flags |= FAULT_FLAG_ALLOW_RETRY;
	if (write_fault)
		flags |= FAULT_FLAG_WRITE;

	ret = handle_mm_fault(vma, addr, flags);
	if (ret & VM_FAULT_RETRY) {
		/* Note, handle_mm_fault did up_read(&mm->mmap_sem)) */
		return -EAGAIN;
	}
	if (ret & VM_FAULT_ERROR)
		goto err;

	return -EBUSY;

err:
	*pfn = range->values[HMM_PFN_ERROR];
	return -EFAULT;
}

static int hmm_pfns_fill(unsigned long addr, unsigned long end,
		struct hmm_range *range, enum hmm_pfn_value_e value)
{
	uint64_t *pfns = range->pfns;
	unsigned long i;

	i = (addr - range->start) >> PAGE_SHIFT;
	for (; addr < end; addr += PAGE_SIZE, i++)
		pfns[i] = range->values[value];

	return 0;
}

/*
 * hmm_vma_walk_hole_() - handle a range lacking valid pmd or pte(s)
 * @addr: range virtual start address (inclusive)
 * @end: range virtual end address (exclusive)
 * @fault: should we fault or not ?
 * @write_fault: write fault ?
 * @walk: mm_walk structure
 * Return: 0 on success, -EBUSY after page fault, or page fault error
 *
 * This function will be called whenever pmd_none() or pte_none() returns true,
 * or whenever there is no page directory covering the virtual address range.
 */
static int hmm_vma_walk_hole_(unsigned long addr, unsigned long end,
			      bool fault, bool write_fault,
			      struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	uint64_t *pfns = range->pfns;
	unsigned long i;

	hmm_vma_walk->last = addr;
	i = (addr - range->start) >> PAGE_SHIFT;

	if (write_fault && walk->vma && !(walk->vma->vm_flags & VM_WRITE))
		return -EPERM;

	for (; addr < end; addr += PAGE_SIZE, i++) {
		pfns[i] = range->values[HMM_PFN_NONE];
		if (fault || write_fault) {
			int ret;

			ret = hmm_vma_do_fault(walk, addr, write_fault,
					       &pfns[i]);
			if (ret != -EBUSY)
				return ret;
		}
	}

	return (fault || write_fault) ? -EBUSY : 0;
}

static inline void hmm_pte_need_fault(const struct hmm_vma_walk *hmm_vma_walk,
				      uint64_t pfns, uint64_t cpu_flags,
				      bool *fault, bool *write_fault)
{
	struct hmm_range *range = hmm_vma_walk->range;

	if (hmm_vma_walk->flags & HMM_FAULT_SNAPSHOT)
		return;

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
	pfns = (pfns & range->pfn_flags_mask) | range->default_flags;

	/* We aren't ask to do anything ... */
	if (!(pfns & range->flags[HMM_PFN_VALID]))
		return;
	/* If this is device memory then only fault if explicitly requested */
	if ((cpu_flags & range->flags[HMM_PFN_DEVICE_PRIVATE])) {
		/* Do we fault on device memory ? */
		if (pfns & range->flags[HMM_PFN_DEVICE_PRIVATE]) {
			*write_fault = pfns & range->flags[HMM_PFN_WRITE];
			*fault = true;
		}
		return;
	}

	/* If CPU page table is not valid then we need to fault */
	*fault = !(cpu_flags & range->flags[HMM_PFN_VALID]);
	/* Need to write fault ? */
	if ((pfns & range->flags[HMM_PFN_WRITE]) &&
	    !(cpu_flags & range->flags[HMM_PFN_WRITE])) {
		*write_fault = true;
		*fault = true;
	}
}

static void hmm_range_need_fault(const struct hmm_vma_walk *hmm_vma_walk,
				 const uint64_t *pfns, unsigned long npages,
				 uint64_t cpu_flags, bool *fault,
				 bool *write_fault)
{
	unsigned long i;

	if (hmm_vma_walk->flags & HMM_FAULT_SNAPSHOT) {
		*fault = *write_fault = false;
		return;
	}

	*fault = *write_fault = false;
	for (i = 0; i < npages; ++i) {
		hmm_pte_need_fault(hmm_vma_walk, pfns[i], cpu_flags,
				   fault, write_fault);
		if ((*write_fault))
			return;
	}
}

static int hmm_vma_walk_hole(unsigned long addr, unsigned long end,
			     __always_unused int depth, struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	bool fault, write_fault;
	unsigned long i, npages;
	uint64_t *pfns;

	i = (addr - range->start) >> PAGE_SHIFT;
	npages = (end - addr) >> PAGE_SHIFT;
	pfns = &range->pfns[i];
	hmm_range_need_fault(hmm_vma_walk, pfns, npages,
			     0, &fault, &write_fault);
	return hmm_vma_walk_hole_(addr, end, fault, write_fault, walk);
}

static inline uint64_t pmd_to_hmm_pfn_flags(struct hmm_range *range, pmd_t pmd)
{
	if (pmd_protnone(pmd))
		return 0;
	return pmd_write(pmd) ? range->flags[HMM_PFN_VALID] |
				range->flags[HMM_PFN_WRITE] :
				range->flags[HMM_PFN_VALID];
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static int hmm_vma_handle_pmd(struct mm_walk *walk, unsigned long addr,
		unsigned long end, uint64_t *pfns, pmd_t pmd)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	unsigned long pfn, npages, i;
	bool fault, write_fault;
	uint64_t cpu_flags;

	npages = (end - addr) >> PAGE_SHIFT;
	cpu_flags = pmd_to_hmm_pfn_flags(range, pmd);
	hmm_range_need_fault(hmm_vma_walk, pfns, npages, cpu_flags,
			     &fault, &write_fault);

	if (pmd_protnone(pmd) || fault || write_fault)
		return hmm_vma_walk_hole_(addr, end, fault, write_fault, walk);

	pfn = pmd_pfn(pmd) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	for (i = 0; addr < end; addr += PAGE_SIZE, i++, pfn++) {
		if (pmd_devmap(pmd)) {
			hmm_vma_walk->pgmap = get_dev_pagemap(pfn,
					      hmm_vma_walk->pgmap);
			if (unlikely(!hmm_vma_walk->pgmap))
				return -EBUSY;
		}
		pfns[i] = hmm_device_entry_from_pfn(range, pfn) | cpu_flags;
	}
	if (hmm_vma_walk->pgmap) {
		put_dev_pagemap(hmm_vma_walk->pgmap);
		hmm_vma_walk->pgmap = NULL;
	}
	hmm_vma_walk->last = end;
	return 0;
}
#else /* CONFIG_TRANSPARENT_HUGEPAGE */
/* stub to allow the code below to compile */
int hmm_vma_handle_pmd(struct mm_walk *walk, unsigned long addr,
		unsigned long end, uint64_t *pfns, pmd_t pmd);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static inline uint64_t pte_to_hmm_pfn_flags(struct hmm_range *range, pte_t pte)
{
	if (pte_none(pte) || !pte_present(pte) || pte_protnone(pte))
		return 0;
	return pte_write(pte) ? range->flags[HMM_PFN_VALID] |
				range->flags[HMM_PFN_WRITE] :
				range->flags[HMM_PFN_VALID];
}

static int hmm_vma_handle_pte(struct mm_walk *walk, unsigned long addr,
			      unsigned long end, pmd_t *pmdp, pte_t *ptep,
			      uint64_t *pfn)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	bool fault, write_fault;
	uint64_t cpu_flags;
	pte_t pte = *ptep;
	uint64_t orig_pfn = *pfn;

	*pfn = range->values[HMM_PFN_NONE];
	fault = write_fault = false;

	if (pte_none(pte)) {
		hmm_pte_need_fault(hmm_vma_walk, orig_pfn, 0,
				   &fault, &write_fault);
		if (fault || write_fault)
			goto fault;
		return 0;
	}

	if (!pte_present(pte)) {
		swp_entry_t entry = pte_to_swp_entry(pte);

		if (!non_swap_entry(entry)) {
			cpu_flags = pte_to_hmm_pfn_flags(range, pte);
			hmm_pte_need_fault(hmm_vma_walk, orig_pfn, cpu_flags,
					   &fault, &write_fault);
			if (fault || write_fault)
				goto fault;
			return 0;
		}

		/*
		 * This is a special swap entry, ignore migration, use
		 * device and report anything else as error.
		 */
		if (is_device_private_entry(entry)) {
			cpu_flags = range->flags[HMM_PFN_VALID] |
				range->flags[HMM_PFN_DEVICE_PRIVATE];
			cpu_flags |= is_write_device_private_entry(entry) ?
				range->flags[HMM_PFN_WRITE] : 0;
			hmm_pte_need_fault(hmm_vma_walk, orig_pfn, cpu_flags,
					   &fault, &write_fault);
			if (fault || write_fault)
				goto fault;
			*pfn = hmm_device_entry_from_pfn(range,
					    swp_offset(entry));
			*pfn |= cpu_flags;
			return 0;
		}

		if (is_migration_entry(entry)) {
			if (fault || write_fault) {
				pte_unmap(ptep);
				hmm_vma_walk->last = addr;
				migration_entry_wait(walk->mm, pmdp, addr);
				return -EBUSY;
			}
			return 0;
		}

		/* Report error for everything else */
		*pfn = range->values[HMM_PFN_ERROR];
		return -EFAULT;
	} else {
		cpu_flags = pte_to_hmm_pfn_flags(range, pte);
		hmm_pte_need_fault(hmm_vma_walk, orig_pfn, cpu_flags,
				   &fault, &write_fault);
	}

	if (fault || write_fault)
		goto fault;

	if (pte_devmap(pte)) {
		hmm_vma_walk->pgmap = get_dev_pagemap(pte_pfn(pte),
					      hmm_vma_walk->pgmap);
		if (unlikely(!hmm_vma_walk->pgmap))
			return -EBUSY;
	} else if (IS_ENABLED(CONFIG_ARCH_HAS_PTE_SPECIAL) && pte_special(pte)) {
		if (!is_zero_pfn(pte_pfn(pte))) {
			*pfn = range->values[HMM_PFN_SPECIAL];
			return -EFAULT;
		}
		/*
		 * Since each architecture defines a struct page for the zero
		 * page, just fall through and treat it like a normal page.
		 */
	}

	*pfn = hmm_device_entry_from_pfn(range, pte_pfn(pte)) | cpu_flags;
	return 0;

fault:
	if (hmm_vma_walk->pgmap) {
		put_dev_pagemap(hmm_vma_walk->pgmap);
		hmm_vma_walk->pgmap = NULL;
	}
	pte_unmap(ptep);
	/* Fault any virtual address we were asked to fault */
	return hmm_vma_walk_hole_(addr, end, fault, write_fault, walk);
}

static int hmm_vma_walk_pmd(pmd_t *pmdp,
			    unsigned long start,
			    unsigned long end,
			    struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	uint64_t *pfns = range->pfns;
	unsigned long addr = start, i;
	pte_t *ptep;
	pmd_t pmd;

again:
	pmd = READ_ONCE(*pmdp);
	if (pmd_none(pmd))
		return hmm_vma_walk_hole(start, end, -1, walk);

	if (thp_migration_supported() && is_pmd_migration_entry(pmd)) {
		bool fault, write_fault;
		unsigned long npages;
		uint64_t *pfns;

		i = (addr - range->start) >> PAGE_SHIFT;
		npages = (end - addr) >> PAGE_SHIFT;
		pfns = &range->pfns[i];

		hmm_range_need_fault(hmm_vma_walk, pfns, npages,
				     0, &fault, &write_fault);
		if (fault || write_fault) {
			hmm_vma_walk->last = addr;
			pmd_migration_entry_wait(walk->mm, pmdp);
			return -EBUSY;
		}
		return 0;
	} else if (!pmd_present(pmd))
		return hmm_pfns_fill(start, end, range, HMM_PFN_ERROR);

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

		i = (addr - range->start) >> PAGE_SHIFT;
		return hmm_vma_handle_pmd(walk, addr, end, &pfns[i], pmd);
	}

	/*
	 * We have handled all the valid cases above ie either none, migration,
	 * huge or transparent huge. At this point either it is a valid pmd
	 * entry pointing to pte directory or it is a bad pmd that will not
	 * recover.
	 */
	if (pmd_bad(pmd))
		return hmm_pfns_fill(start, end, range, HMM_PFN_ERROR);

	ptep = pte_offset_map(pmdp, addr);
	i = (addr - range->start) >> PAGE_SHIFT;
	for (; addr < end; addr += PAGE_SIZE, ptep++, i++) {
		int r;

		r = hmm_vma_handle_pte(walk, addr, end, pmdp, ptep, &pfns[i]);
		if (r) {
			/* hmm_vma_handle_pte() did unmap pte directory */
			hmm_vma_walk->last = addr;
			return r;
		}
	}
	if (hmm_vma_walk->pgmap) {
		/*
		 * We do put_dev_pagemap() here and not in hmm_vma_handle_pte()
		 * so that we can leverage get_dev_pagemap() optimization which
		 * will not re-take a reference on a pgmap if we already have
		 * one.
		 */
		put_dev_pagemap(hmm_vma_walk->pgmap);
		hmm_vma_walk->pgmap = NULL;
	}
	pte_unmap(ptep - 1);

	hmm_vma_walk->last = addr;
	return 0;
}

#if defined(CONFIG_ARCH_HAS_PTE_DEVMAP) && \
    defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
static inline uint64_t pud_to_hmm_pfn_flags(struct hmm_range *range, pud_t pud)
{
	if (!pud_present(pud))
		return 0;
	return pud_write(pud) ? range->flags[HMM_PFN_VALID] |
				range->flags[HMM_PFN_WRITE] :
				range->flags[HMM_PFN_VALID];
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
		ret = hmm_vma_walk_hole(start, end, -1, walk);
		goto out_unlock;
	}

	if (pud_huge(pud) && pud_devmap(pud)) {
		unsigned long i, npages, pfn;
		uint64_t *pfns, cpu_flags;
		bool fault, write_fault;

		if (!pud_present(pud)) {
			ret = hmm_vma_walk_hole(start, end, -1, walk);
			goto out_unlock;
		}

		i = (addr - range->start) >> PAGE_SHIFT;
		npages = (end - addr) >> PAGE_SHIFT;
		pfns = &range->pfns[i];

		cpu_flags = pud_to_hmm_pfn_flags(range, pud);
		hmm_range_need_fault(hmm_vma_walk, pfns, npages,
				     cpu_flags, &fault, &write_fault);
		if (fault || write_fault) {
			ret = hmm_vma_walk_hole_(addr, end, fault,
						 write_fault, walk);
			goto out_unlock;
		}

		pfn = pud_pfn(pud) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
		for (i = 0; i < npages; ++i, ++pfn) {
			hmm_vma_walk->pgmap = get_dev_pagemap(pfn,
					      hmm_vma_walk->pgmap);
			if (unlikely(!hmm_vma_walk->pgmap)) {
				ret = -EBUSY;
				goto out_unlock;
			}
			pfns[i] = hmm_device_entry_from_pfn(range, pfn) |
				  cpu_flags;
		}
		if (hmm_vma_walk->pgmap) {
			put_dev_pagemap(hmm_vma_walk->pgmap);
			hmm_vma_walk->pgmap = NULL;
		}
		hmm_vma_walk->last = end;
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
	uint64_t orig_pfn, cpu_flags;
	bool fault, write_fault;
	spinlock_t *ptl;
	pte_t entry;
	int ret = 0;

	ptl = huge_pte_lock(hstate_vma(vma), walk->mm, pte);
	entry = huge_ptep_get(pte);

	i = (start - range->start) >> PAGE_SHIFT;
	orig_pfn = range->pfns[i];
	range->pfns[i] = range->values[HMM_PFN_NONE];
	cpu_flags = pte_to_hmm_pfn_flags(range, entry);
	fault = write_fault = false;
	hmm_pte_need_fault(hmm_vma_walk, orig_pfn, cpu_flags,
			   &fault, &write_fault);
	if (fault || write_fault) {
		ret = -ENOENT;
		goto unlock;
	}

	pfn = pte_pfn(entry) + ((start & ~hmask) >> PAGE_SHIFT);
	for (; addr < end; addr += PAGE_SIZE, i++, pfn++)
		range->pfns[i] = hmm_device_entry_from_pfn(range, pfn) |
				 cpu_flags;
	hmm_vma_walk->last = end;

unlock:
	spin_unlock(ptl);

	if (ret == -ENOENT)
		return hmm_vma_walk_hole_(addr, end, fault, write_fault, walk);

	return ret;
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

	/*
	 * Skip vma ranges that don't have struct page backing them or
	 * map I/O devices directly.
	 */
	if (vma->vm_flags & (VM_IO | VM_PFNMAP | VM_MIXEDMAP))
		return -EFAULT;

	/*
	 * If the vma does not allow read access, then assume that it does not
	 * allow write access either. HMM does not support architectures
	 * that allow write without read.
	 */
	if (!(vma->vm_flags & VM_READ)) {
		bool fault, write_fault;

		/*
		 * Check to see if a fault is requested for any page in the
		 * range.
		 */
		hmm_range_need_fault(hmm_vma_walk, range->pfns +
					((start - range->start) >> PAGE_SHIFT),
					(end - start) >> PAGE_SHIFT,
					0, &fault, &write_fault);
		if (fault || write_fault)
			return -EFAULT;

		hmm_pfns_fill(start, end, range, HMM_PFN_NONE);
		hmm_vma_walk->last = end;

		/* Skip this vma and continue processing the next vma. */
		return 1;
	}

	return 0;
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
 * @range:	range being faulted
 * @flags:	HMM_FAULT_* flags
 *
 * Return: the number of valid pages in range->pfns[] (from range start
 * address), which may be zero.  On error one of the following status codes
 * can be returned:
 *
 * -EINVAL:	Invalid arguments or mm or virtual address is in an invalid vma
 *		(e.g., device file vma).
 * -ENOMEM:	Out of memory.
 * -EPERM:	Invalid permission (e.g., asking for write and range is read
 *		only).
 * -EAGAIN:	A page fault needs to be retried and mmap_sem was dropped.
 * -EBUSY:	The range has been invalidated and the caller needs to wait for
 *		the invalidation to finish.
 * -EFAULT:	Invalid (i.e., either no valid vma or it is illegal to access
 *		that range) number of valid pages in range->pfns[] (from
 *              range start address).
 *
 * This is similar to a regular CPU page fault except that it will not trigger
 * any memory migration if the memory being faulted is not accessible by CPUs
 * and caller does not ask for migration.
 *
 * On error, for one virtual address in the range, the function will mark the
 * corresponding HMM pfn entry with an error flag.
 */
long hmm_range_fault(struct hmm_range *range, unsigned int flags)
{
	struct hmm_vma_walk hmm_vma_walk = {
		.range = range,
		.last = range->start,
		.flags = flags,
	};
	struct mm_struct *mm = range->notifier->mm;
	int ret;

	lockdep_assert_held(&mm->mmap_sem);

	do {
		/* If range is no longer valid force retry. */
		if (mmu_interval_check_retry(range->notifier,
					     range->notifier_seq))
			return -EBUSY;
		ret = walk_page_range(mm, hmm_vma_walk.last, range->end,
				      &hmm_walk_ops, &hmm_vma_walk);
	} while (ret == -EBUSY);

	if (ret)
		return ret;
	return (hmm_vma_walk.last - range->start) >> PAGE_SHIFT;
}
EXPORT_SYMBOL(hmm_range_fault);
