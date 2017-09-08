/*
 * Copyright 2013 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: JÃ©rÃ´me Glisse <jglisse@redhat.com>
 */
/*
 * Refer to include/linux/hmm.h for information about heterogeneous memory
 * management or HMM for short.
 */
#include <linux/mm.h>
#include <linux/hmm.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/swapops.h>
#include <linux/hugetlb.h>
#include <linux/mmu_notifier.h>


#ifdef CONFIG_HMM
static const struct mmu_notifier_ops hmm_mmu_notifier_ops;

/*
 * struct hmm - HMM per mm struct
 *
 * @mm: mm struct this HMM struct is bound to
 * @lock: lock protecting ranges list
 * @sequence: we track updates to the CPU page table with a sequence number
 * @ranges: list of range being snapshotted
 * @mirrors: list of mirrors for this mm
 * @mmu_notifier: mmu notifier to track updates to CPU page table
 * @mirrors_sem: read/write semaphore protecting the mirrors list
 */
struct hmm {
	struct mm_struct	*mm;
	spinlock_t		lock;
	atomic_t		sequence;
	struct list_head	ranges;
	struct list_head	mirrors;
	struct mmu_notifier	mmu_notifier;
	struct rw_semaphore	mirrors_sem;
};

/*
 * hmm_register - register HMM against an mm (HMM internal)
 *
 * @mm: mm struct to attach to
 *
 * This is not intended to be used directly by device drivers. It allocates an
 * HMM struct if mm does not have one, and initializes it.
 */
static struct hmm *hmm_register(struct mm_struct *mm)
{
	struct hmm *hmm = READ_ONCE(mm->hmm);
	bool cleanup = false;

	/*
	 * The hmm struct can only be freed once the mm_struct goes away,
	 * hence we should always have pre-allocated an new hmm struct
	 * above.
	 */
	if (hmm)
		return hmm;

	hmm = kmalloc(sizeof(*hmm), GFP_KERNEL);
	if (!hmm)
		return NULL;
	INIT_LIST_HEAD(&hmm->mirrors);
	init_rwsem(&hmm->mirrors_sem);
	atomic_set(&hmm->sequence, 0);
	hmm->mmu_notifier.ops = NULL;
	INIT_LIST_HEAD(&hmm->ranges);
	spin_lock_init(&hmm->lock);
	hmm->mm = mm;

	/*
	 * We should only get here if hold the mmap_sem in write mode ie on
	 * registration of first mirror through hmm_mirror_register()
	 */
	hmm->mmu_notifier.ops = &hmm_mmu_notifier_ops;
	if (__mmu_notifier_register(&hmm->mmu_notifier, mm)) {
		kfree(hmm);
		return NULL;
	}

	spin_lock(&mm->page_table_lock);
	if (!mm->hmm)
		mm->hmm = hmm;
	else
		cleanup = true;
	spin_unlock(&mm->page_table_lock);

	if (cleanup) {
		mmu_notifier_unregister(&hmm->mmu_notifier, mm);
		kfree(hmm);
	}

	return mm->hmm;
}

void hmm_mm_destroy(struct mm_struct *mm)
{
	kfree(mm->hmm);
}
#endif /* CONFIG_HMM */

#if IS_ENABLED(CONFIG_HMM_MIRROR)
static void hmm_invalidate_range(struct hmm *hmm,
				 enum hmm_update_type action,
				 unsigned long start,
				 unsigned long end)
{
	struct hmm_mirror *mirror;
	struct hmm_range *range;

	spin_lock(&hmm->lock);
	list_for_each_entry(range, &hmm->ranges, list) {
		unsigned long addr, idx, npages;

		if (end < range->start || start >= range->end)
			continue;

		range->valid = false;
		addr = max(start, range->start);
		idx = (addr - range->start) >> PAGE_SHIFT;
		npages = (min(range->end, end) - addr) >> PAGE_SHIFT;
		memset(&range->pfns[idx], 0, sizeof(*range->pfns) * npages);
	}
	spin_unlock(&hmm->lock);

	down_read(&hmm->mirrors_sem);
	list_for_each_entry(mirror, &hmm->mirrors, list)
		mirror->ops->sync_cpu_device_pagetables(mirror, action,
							start, end);
	up_read(&hmm->mirrors_sem);
}

static void hmm_invalidate_range_start(struct mmu_notifier *mn,
				       struct mm_struct *mm,
				       unsigned long start,
				       unsigned long end)
{
	struct hmm *hmm = mm->hmm;

	VM_BUG_ON(!hmm);

	atomic_inc(&hmm->sequence);
}

static void hmm_invalidate_range_end(struct mmu_notifier *mn,
				     struct mm_struct *mm,
				     unsigned long start,
				     unsigned long end)
{
	struct hmm *hmm = mm->hmm;

	VM_BUG_ON(!hmm);

	hmm_invalidate_range(mm->hmm, HMM_UPDATE_INVALIDATE, start, end);
}

static const struct mmu_notifier_ops hmm_mmu_notifier_ops = {
	.invalidate_range_start	= hmm_invalidate_range_start,
	.invalidate_range_end	= hmm_invalidate_range_end,
};

/*
 * hmm_mirror_register() - register a mirror against an mm
 *
 * @mirror: new mirror struct to register
 * @mm: mm to register against
 *
 * To start mirroring a process address space, the device driver must register
 * an HMM mirror struct.
 *
 * THE mm->mmap_sem MUST BE HELD IN WRITE MODE !
 */
int hmm_mirror_register(struct hmm_mirror *mirror, struct mm_struct *mm)
{
	/* Sanity check */
	if (!mm || !mirror || !mirror->ops)
		return -EINVAL;

	mirror->hmm = hmm_register(mm);
	if (!mirror->hmm)
		return -ENOMEM;

	down_write(&mirror->hmm->mirrors_sem);
	list_add(&mirror->list, &mirror->hmm->mirrors);
	up_write(&mirror->hmm->mirrors_sem);

	return 0;
}
EXPORT_SYMBOL(hmm_mirror_register);

/*
 * hmm_mirror_unregister() - unregister a mirror
 *
 * @mirror: new mirror struct to register
 *
 * Stop mirroring a process address space, and cleanup.
 */
void hmm_mirror_unregister(struct hmm_mirror *mirror)
{
	struct hmm *hmm = mirror->hmm;

	down_write(&hmm->mirrors_sem);
	list_del(&mirror->list);
	up_write(&hmm->mirrors_sem);
}
EXPORT_SYMBOL(hmm_mirror_unregister);

struct hmm_vma_walk {
	struct hmm_range	*range;
	unsigned long		last;
	bool			fault;
	bool			block;
	bool			write;
};

static int hmm_vma_do_fault(struct mm_walk *walk,
			    unsigned long addr,
			    hmm_pfn_t *pfn)
{
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_REMOTE;
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct vm_area_struct *vma = walk->vma;
	int r;

	flags |= hmm_vma_walk->block ? 0 : FAULT_FLAG_ALLOW_RETRY;
	flags |= hmm_vma_walk->write ? FAULT_FLAG_WRITE : 0;
	r = handle_mm_fault(vma, addr, flags);
	if (r & VM_FAULT_RETRY)
		return -EBUSY;
	if (r & VM_FAULT_ERROR) {
		*pfn = HMM_PFN_ERROR;
		return -EFAULT;
	}

	return -EAGAIN;
}

static void hmm_pfns_special(hmm_pfn_t *pfns,
			     unsigned long addr,
			     unsigned long end)
{
	for (; addr < end; addr += PAGE_SIZE, pfns++)
		*pfns = HMM_PFN_SPECIAL;
}

static int hmm_pfns_bad(unsigned long addr,
			unsigned long end,
			struct mm_walk *walk)
{
	struct hmm_range *range = walk->private;
	hmm_pfn_t *pfns = range->pfns;
	unsigned long i;

	i = (addr - range->start) >> PAGE_SHIFT;
	for (; addr < end; addr += PAGE_SIZE, i++)
		pfns[i] = HMM_PFN_ERROR;

	return 0;
}

static void hmm_pfns_clear(hmm_pfn_t *pfns,
			   unsigned long addr,
			   unsigned long end)
{
	for (; addr < end; addr += PAGE_SIZE, pfns++)
		*pfns = 0;
}

static int hmm_vma_walk_hole(unsigned long addr,
			     unsigned long end,
			     struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	hmm_pfn_t *pfns = range->pfns;
	unsigned long i;

	hmm_vma_walk->last = addr;
	i = (addr - range->start) >> PAGE_SHIFT;
	for (; addr < end; addr += PAGE_SIZE, i++) {
		pfns[i] = HMM_PFN_EMPTY;
		if (hmm_vma_walk->fault) {
			int ret;

			ret = hmm_vma_do_fault(walk, addr, &pfns[i]);
			if (ret != -EAGAIN)
				return ret;
		}
	}

	return hmm_vma_walk->fault ? -EAGAIN : 0;
}

static int hmm_vma_walk_clear(unsigned long addr,
			      unsigned long end,
			      struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	hmm_pfn_t *pfns = range->pfns;
	unsigned long i;

	hmm_vma_walk->last = addr;
	i = (addr - range->start) >> PAGE_SHIFT;
	for (; addr < end; addr += PAGE_SIZE, i++) {
		pfns[i] = 0;
		if (hmm_vma_walk->fault) {
			int ret;

			ret = hmm_vma_do_fault(walk, addr, &pfns[i]);
			if (ret != -EAGAIN)
				return ret;
		}
	}

	return hmm_vma_walk->fault ? -EAGAIN : 0;
}

static int hmm_vma_walk_pmd(pmd_t *pmdp,
			    unsigned long start,
			    unsigned long end,
			    struct mm_walk *walk)
{
	struct hmm_vma_walk *hmm_vma_walk = walk->private;
	struct hmm_range *range = hmm_vma_walk->range;
	struct vm_area_struct *vma = walk->vma;
	hmm_pfn_t *pfns = range->pfns;
	unsigned long addr = start, i;
	bool write_fault;
	hmm_pfn_t flag;
	pte_t *ptep;

	i = (addr - range->start) >> PAGE_SHIFT;
	flag = vma->vm_flags & VM_READ ? HMM_PFN_READ : 0;
	write_fault = hmm_vma_walk->fault & hmm_vma_walk->write;

again:
	if (pmd_none(*pmdp))
		return hmm_vma_walk_hole(start, end, walk);

	if (pmd_huge(*pmdp) && vma->vm_flags & VM_HUGETLB)
		return hmm_pfns_bad(start, end, walk);

	if (pmd_devmap(*pmdp) || pmd_trans_huge(*pmdp)) {
		unsigned long pfn;
		pmd_t pmd;

		/*
		 * No need to take pmd_lock here, even if some other threads
		 * is splitting the huge pmd we will get that event through
		 * mmu_notifier callback.
		 *
		 * So just read pmd value and check again its a transparent
		 * huge or device mapping one and compute corresponding pfn
		 * values.
		 */
		pmd = pmd_read_atomic(pmdp);
		barrier();
		if (!pmd_devmap(pmd) && !pmd_trans_huge(pmd))
			goto again;
		if (pmd_protnone(pmd))
			return hmm_vma_walk_clear(start, end, walk);

		if (write_fault && !pmd_write(pmd))
			return hmm_vma_walk_clear(start, end, walk);

		pfn = pmd_pfn(pmd) + pte_index(addr);
		flag |= pmd_write(pmd) ? HMM_PFN_WRITE : 0;
		for (; addr < end; addr += PAGE_SIZE, i++, pfn++)
			pfns[i] = hmm_pfn_t_from_pfn(pfn) | flag;
		return 0;
	}

	if (pmd_bad(*pmdp))
		return hmm_pfns_bad(start, end, walk);

	ptep = pte_offset_map(pmdp, addr);
	for (; addr < end; addr += PAGE_SIZE, ptep++, i++) {
		pte_t pte = *ptep;

		pfns[i] = 0;

		if (pte_none(pte)) {
			pfns[i] = HMM_PFN_EMPTY;
			if (hmm_vma_walk->fault)
				goto fault;
			continue;
		}

		if (!pte_present(pte)) {
			swp_entry_t entry;

			if (!non_swap_entry(entry)) {
				if (hmm_vma_walk->fault)
					goto fault;
				continue;
			}

			entry = pte_to_swp_entry(pte);

			/*
			 * This is a special swap entry, ignore migration, use
			 * device and report anything else as error.
			 */
			if (is_migration_entry(entry)) {
				if (hmm_vma_walk->fault) {
					pte_unmap(ptep);
					hmm_vma_walk->last = addr;
					migration_entry_wait(vma->vm_mm,
							     pmdp, addr);
					return -EAGAIN;
				}
				continue;
			} else {
				/* Report error for everything else */
				pfns[i] = HMM_PFN_ERROR;
			}
			continue;
		}

		if (write_fault && !pte_write(pte))
			goto fault;

		pfns[i] = hmm_pfn_t_from_pfn(pte_pfn(pte)) | flag;
		pfns[i] |= pte_write(pte) ? HMM_PFN_WRITE : 0;
		continue;

fault:
		pte_unmap(ptep);
		/* Fault all pages in range */
		return hmm_vma_walk_clear(start, end, walk);
	}
	pte_unmap(ptep - 1);

	return 0;
}

/*
 * hmm_vma_get_pfns() - snapshot CPU page table for a range of virtual addresses
 * @vma: virtual memory area containing the virtual address range
 * @range: used to track snapshot validity
 * @start: range virtual start address (inclusive)
 * @end: range virtual end address (exclusive)
 * @entries: array of hmm_pfn_t: provided by the caller, filled in by function
 * Returns: -EINVAL if invalid argument, -ENOMEM out of memory, 0 success
 *
 * This snapshots the CPU page table for a range of virtual addresses. Snapshot
 * validity is tracked by range struct. See hmm_vma_range_done() for further
 * information.
 *
 * The range struct is initialized here. It tracks the CPU page table, but only
 * if the function returns success (0), in which case the caller must then call
 * hmm_vma_range_done() to stop CPU page table update tracking on this range.
 *
 * NOT CALLING hmm_vma_range_done() IF FUNCTION RETURNS 0 WILL LEAD TO SERIOUS
 * MEMORY CORRUPTION ! YOU HAVE BEEN WARNED !
 */
int hmm_vma_get_pfns(struct vm_area_struct *vma,
		     struct hmm_range *range,
		     unsigned long start,
		     unsigned long end,
		     hmm_pfn_t *pfns)
{
	struct hmm_vma_walk hmm_vma_walk;
	struct mm_walk mm_walk;
	struct hmm *hmm;

	/* FIXME support hugetlb fs */
	if (is_vm_hugetlb_page(vma) || (vma->vm_flags & VM_SPECIAL)) {
		hmm_pfns_special(pfns, start, end);
		return -EINVAL;
	}

	/* Sanity check, this really should not happen ! */
	if (start < vma->vm_start || start >= vma->vm_end)
		return -EINVAL;
	if (end < vma->vm_start || end > vma->vm_end)
		return -EINVAL;

	hmm = hmm_register(vma->vm_mm);
	if (!hmm)
		return -ENOMEM;
	/* Caller must have registered a mirror, via hmm_mirror_register() ! */
	if (!hmm->mmu_notifier.ops)
		return -EINVAL;

	/* Initialize range to track CPU page table update */
	range->start = start;
	range->pfns = pfns;
	range->end = end;
	spin_lock(&hmm->lock);
	range->valid = true;
	list_add_rcu(&range->list, &hmm->ranges);
	spin_unlock(&hmm->lock);

	hmm_vma_walk.fault = false;
	hmm_vma_walk.range = range;
	mm_walk.private = &hmm_vma_walk;

	mm_walk.vma = vma;
	mm_walk.mm = vma->vm_mm;
	mm_walk.pte_entry = NULL;
	mm_walk.test_walk = NULL;
	mm_walk.hugetlb_entry = NULL;
	mm_walk.pmd_entry = hmm_vma_walk_pmd;
	mm_walk.pte_hole = hmm_vma_walk_hole;

	walk_page_range(start, end, &mm_walk);
	return 0;
}
EXPORT_SYMBOL(hmm_vma_get_pfns);

/*
 * hmm_vma_range_done() - stop tracking change to CPU page table over a range
 * @vma: virtual memory area containing the virtual address range
 * @range: range being tracked
 * Returns: false if range data has been invalidated, true otherwise
 *
 * Range struct is used to track updates to the CPU page table after a call to
 * either hmm_vma_get_pfns() or hmm_vma_fault(). Once the device driver is done
 * using the data,  or wants to lock updates to the data it got from those
 * functions, it must call the hmm_vma_range_done() function, which will then
 * stop tracking CPU page table updates.
 *
 * Note that device driver must still implement general CPU page table update
 * tracking either by using hmm_mirror (see hmm_mirror_register()) or by using
 * the mmu_notifier API directly.
 *
 * CPU page table update tracking done through hmm_range is only temporary and
 * to be used while trying to duplicate CPU page table contents for a range of
 * virtual addresses.
 *
 * There are two ways to use this :
 * again:
 *   hmm_vma_get_pfns(vma, range, start, end, pfns); or hmm_vma_fault(...);
 *   trans = device_build_page_table_update_transaction(pfns);
 *   device_page_table_lock();
 *   if (!hmm_vma_range_done(vma, range)) {
 *     device_page_table_unlock();
 *     goto again;
 *   }
 *   device_commit_transaction(trans);
 *   device_page_table_unlock();
 *
 * Or:
 *   hmm_vma_get_pfns(vma, range, start, end, pfns); or hmm_vma_fault(...);
 *   device_page_table_lock();
 *   hmm_vma_range_done(vma, range);
 *   device_update_page_table(pfns);
 *   device_page_table_unlock();
 */
bool hmm_vma_range_done(struct vm_area_struct *vma, struct hmm_range *range)
{
	unsigned long npages = (range->end - range->start) >> PAGE_SHIFT;
	struct hmm *hmm;

	if (range->end <= range->start) {
		BUG();
		return false;
	}

	hmm = hmm_register(vma->vm_mm);
	if (!hmm) {
		memset(range->pfns, 0, sizeof(*range->pfns) * npages);
		return false;
	}

	spin_lock(&hmm->lock);
	list_del_rcu(&range->list);
	spin_unlock(&hmm->lock);

	return range->valid;
}
EXPORT_SYMBOL(hmm_vma_range_done);

/*
 * hmm_vma_fault() - try to fault some address in a virtual address range
 * @vma: virtual memory area containing the virtual address range
 * @range: use to track pfns array content validity
 * @start: fault range virtual start address (inclusive)
 * @end: fault range virtual end address (exclusive)
 * @pfns: array of hmm_pfn_t, only entry with fault flag set will be faulted
 * @write: is it a write fault
 * @block: allow blocking on fault (if true it sleeps and do not drop mmap_sem)
 * Returns: 0 success, error otherwise (-EAGAIN means mmap_sem have been drop)
 *
 * This is similar to a regular CPU page fault except that it will not trigger
 * any memory migration if the memory being faulted is not accessible by CPUs.
 *
 * On error, for one virtual address in the range, the function will set the
 * hmm_pfn_t error flag for the corresponding pfn entry.
 *
 * Expected use pattern:
 * retry:
 *   down_read(&mm->mmap_sem);
 *   // Find vma and address device wants to fault, initialize hmm_pfn_t
 *   // array accordingly
 *   ret = hmm_vma_fault(vma, start, end, pfns, allow_retry);
 *   switch (ret) {
 *   case -EAGAIN:
 *     hmm_vma_range_done(vma, range);
 *     // You might want to rate limit or yield to play nicely, you may
 *     // also commit any valid pfn in the array assuming that you are
 *     // getting true from hmm_vma_range_monitor_end()
 *     goto retry;
 *   case 0:
 *     break;
 *   default:
 *     // Handle error !
 *     up_read(&mm->mmap_sem)
 *     return;
 *   }
 *   // Take device driver lock that serialize device page table update
 *   driver_lock_device_page_table_update();
 *   hmm_vma_range_done(vma, range);
 *   // Commit pfns we got from hmm_vma_fault()
 *   driver_unlock_device_page_table_update();
 *   up_read(&mm->mmap_sem)
 *
 * YOU MUST CALL hmm_vma_range_done() AFTER THIS FUNCTION RETURN SUCCESS (0)
 * BEFORE FREEING THE range struct OR YOU WILL HAVE SERIOUS MEMORY CORRUPTION !
 *
 * YOU HAVE BEEN WARNED !
 */
int hmm_vma_fault(struct vm_area_struct *vma,
		  struct hmm_range *range,
		  unsigned long start,
		  unsigned long end,
		  hmm_pfn_t *pfns,
		  bool write,
		  bool block)
{
	struct hmm_vma_walk hmm_vma_walk;
	struct mm_walk mm_walk;
	struct hmm *hmm;
	int ret;

	/* Sanity check, this really should not happen ! */
	if (start < vma->vm_start || start >= vma->vm_end)
		return -EINVAL;
	if (end < vma->vm_start || end > vma->vm_end)
		return -EINVAL;

	hmm = hmm_register(vma->vm_mm);
	if (!hmm) {
		hmm_pfns_clear(pfns, start, end);
		return -ENOMEM;
	}
	/* Caller must have registered a mirror using hmm_mirror_register() */
	if (!hmm->mmu_notifier.ops)
		return -EINVAL;

	/* Initialize range to track CPU page table update */
	range->start = start;
	range->pfns = pfns;
	range->end = end;
	spin_lock(&hmm->lock);
	range->valid = true;
	list_add_rcu(&range->list, &hmm->ranges);
	spin_unlock(&hmm->lock);

	/* FIXME support hugetlb fs */
	if (is_vm_hugetlb_page(vma) || (vma->vm_flags & VM_SPECIAL)) {
		hmm_pfns_special(pfns, start, end);
		return 0;
	}

	hmm_vma_walk.fault = true;
	hmm_vma_walk.write = write;
	hmm_vma_walk.block = block;
	hmm_vma_walk.range = range;
	mm_walk.private = &hmm_vma_walk;
	hmm_vma_walk.last = range->start;

	mm_walk.vma = vma;
	mm_walk.mm = vma->vm_mm;
	mm_walk.pte_entry = NULL;
	mm_walk.test_walk = NULL;
	mm_walk.hugetlb_entry = NULL;
	mm_walk.pmd_entry = hmm_vma_walk_pmd;
	mm_walk.pte_hole = hmm_vma_walk_hole;

	do {
		ret = walk_page_range(start, end, &mm_walk);
		start = hmm_vma_walk.last;
	} while (ret == -EAGAIN);

	if (ret) {
		unsigned long i;

		i = (hmm_vma_walk.last - range->start) >> PAGE_SHIFT;
		hmm_pfns_clear(&pfns[i], hmm_vma_walk.last, end);
		hmm_vma_range_done(vma, range);
	}
	return ret;
}
EXPORT_SYMBOL(hmm_vma_fault);
#endif /* IS_ENABLED(CONFIG_HMM_MIRROR) */
