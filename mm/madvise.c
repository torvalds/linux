// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/mm/madvise.c
 *
 * Copyright (C) 1999  Linus Torvalds
 * Copyright (C) 2002  Christoph Hellwig
 */

#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/mempolicy.h>
#include <linux/page-isolation.h>
#include <linux/page_idle.h>
#include <linux/userfaultfd_k.h>
#include <linux/hugetlb.h>
#include <linux/falloc.h>
#include <linux/fadvise.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/uio.h>
#include <linux/ksm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/pagewalk.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/shmem_fs.h>
#include <linux/mmu_notifier.h>

#include <asm/tlb.h>

#include "internal.h"

struct madvise_walk_private {
	struct mmu_gather *tlb;
	bool pageout;
};

/*
 * Any behaviour which results in changes to the vma->vm_flags needs to
 * take mmap_lock for writing. Others, which simply traverse vmas, need
 * to only take it for reading.
 */
static int madvise_need_mmap_write(int behavior)
{
	switch (behavior) {
	case MADV_REMOVE:
	case MADV_WILLNEED:
	case MADV_DONTNEED:
	case MADV_COLD:
	case MADV_PAGEOUT:
	case MADV_FREE:
		return 0;
	default:
		/* be safe, default to 1. list exceptions explicitly */
		return 1;
	}
}

/*
 * We can potentially split a vm area into separate
 * areas, each area with its own behavior.
 */
static long madvise_behavior(struct vm_area_struct *vma,
		     struct vm_area_struct **prev,
		     unsigned long start, unsigned long end, int behavior)
{
	struct mm_struct *mm = vma->vm_mm;
	int error = 0;
	pgoff_t pgoff;
	unsigned long new_flags = vma->vm_flags;

	switch (behavior) {
	case MADV_NORMAL:
		new_flags = new_flags & ~VM_RAND_READ & ~VM_SEQ_READ;
		break;
	case MADV_SEQUENTIAL:
		new_flags = (new_flags & ~VM_RAND_READ) | VM_SEQ_READ;
		break;
	case MADV_RANDOM:
		new_flags = (new_flags & ~VM_SEQ_READ) | VM_RAND_READ;
		break;
	case MADV_DONTFORK:
		new_flags |= VM_DONTCOPY;
		break;
	case MADV_DOFORK:
		if (vma->vm_flags & VM_IO) {
			error = -EINVAL;
			goto out;
		}
		new_flags &= ~VM_DONTCOPY;
		break;
	case MADV_WIPEONFORK:
		/* MADV_WIPEONFORK is only supported on anonymous memory. */
		if (vma->vm_file || vma->vm_flags & VM_SHARED) {
			error = -EINVAL;
			goto out;
		}
		new_flags |= VM_WIPEONFORK;
		break;
	case MADV_KEEPONFORK:
		new_flags &= ~VM_WIPEONFORK;
		break;
	case MADV_DONTDUMP:
		new_flags |= VM_DONTDUMP;
		break;
	case MADV_DODUMP:
		if (!is_vm_hugetlb_page(vma) && new_flags & VM_SPECIAL) {
			error = -EINVAL;
			goto out;
		}
		new_flags &= ~VM_DONTDUMP;
		break;
	case MADV_MERGEABLE:
	case MADV_UNMERGEABLE:
		error = ksm_madvise(vma, start, end, behavior, &new_flags);
		if (error)
			goto out_convert_errno;
		break;
	case MADV_HUGEPAGE:
	case MADV_NOHUGEPAGE:
		error = hugepage_madvise(vma, &new_flags, behavior);
		if (error)
			goto out_convert_errno;
		break;
	}

	if (new_flags == vma->vm_flags) {
		*prev = vma;
		goto out;
	}

	pgoff = vma->vm_pgoff + ((start - vma->vm_start) >> PAGE_SHIFT);
	*prev = vma_merge(mm, *prev, start, end, new_flags, vma->anon_vma,
			  vma->vm_file, pgoff, vma_policy(vma),
			  vma->vm_userfaultfd_ctx);
	if (*prev) {
		vma = *prev;
		goto success;
	}

	*prev = vma;

	if (start != vma->vm_start) {
		if (unlikely(mm->map_count >= sysctl_max_map_count)) {
			error = -ENOMEM;
			goto out;
		}
		error = __split_vma(mm, vma, start, 1);
		if (error)
			goto out_convert_errno;
	}

	if (end != vma->vm_end) {
		if (unlikely(mm->map_count >= sysctl_max_map_count)) {
			error = -ENOMEM;
			goto out;
		}
		error = __split_vma(mm, vma, end, 0);
		if (error)
			goto out_convert_errno;
	}

success:
	/*
	 * vm_flags is protected by the mmap_lock held in write mode.
	 */
	vma->vm_flags = new_flags;

out_convert_errno:
	/*
	 * madvise() returns EAGAIN if kernel resources, such as
	 * slab, are temporarily unavailable.
	 */
	if (error == -ENOMEM)
		error = -EAGAIN;
out:
	return error;
}

#ifdef CONFIG_SWAP
static int swapin_walk_pmd_entry(pmd_t *pmd, unsigned long start,
	unsigned long end, struct mm_walk *walk)
{
	pte_t *orig_pte;
	struct vm_area_struct *vma = walk->private;
	unsigned long index;

	if (pmd_none_or_trans_huge_or_clear_bad(pmd))
		return 0;

	for (index = start; index != end; index += PAGE_SIZE) {
		pte_t pte;
		swp_entry_t entry;
		struct page *page;
		spinlock_t *ptl;

		orig_pte = pte_offset_map_lock(vma->vm_mm, pmd, start, &ptl);
		pte = *(orig_pte + ((index - start) / PAGE_SIZE));
		pte_unmap_unlock(orig_pte, ptl);

		if (pte_present(pte) || pte_none(pte))
			continue;
		entry = pte_to_swp_entry(pte);
		if (unlikely(non_swap_entry(entry)))
			continue;

		page = read_swap_cache_async(entry, GFP_HIGHUSER_MOVABLE,
							vma, index, false);
		if (page)
			put_page(page);
	}

	return 0;
}

static const struct mm_walk_ops swapin_walk_ops = {
	.pmd_entry		= swapin_walk_pmd_entry,
};

static void force_shm_swapin_readahead(struct vm_area_struct *vma,
		unsigned long start, unsigned long end,
		struct address_space *mapping)
{
	XA_STATE(xas, &mapping->i_pages, linear_page_index(vma, start));
	pgoff_t end_index = end / PAGE_SIZE;
	struct page *page;

	rcu_read_lock();
	xas_for_each(&xas, page, end_index) {
		swp_entry_t swap;

		if (!xa_is_value(page))
			continue;
		xas_pause(&xas);
		rcu_read_unlock();

		swap = radix_to_swp_entry(page);
		page = read_swap_cache_async(swap, GFP_HIGHUSER_MOVABLE,
							NULL, 0, false);
		if (page)
			put_page(page);

		rcu_read_lock();
	}
	rcu_read_unlock();

	lru_add_drain();	/* Push any new pages onto the LRU now */
}
#endif		/* CONFIG_SWAP */

/*
 * Schedule all required I/O operations.  Do not wait for completion.
 */
static long madvise_willneed(struct vm_area_struct *vma,
			     struct vm_area_struct **prev,
			     unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	loff_t offset;

	*prev = vma;
#ifdef CONFIG_SWAP
	if (!file) {
		walk_page_range(vma->vm_mm, start, end, &swapin_walk_ops, vma);
		lru_add_drain(); /* Push any new pages onto the LRU now */
		return 0;
	}

	if (shmem_mapping(file->f_mapping)) {
		force_shm_swapin_readahead(vma, start, end,
					file->f_mapping);
		return 0;
	}
#else
	if (!file)
		return -EBADF;
#endif

	if (IS_DAX(file_inode(file))) {
		/* no bad return value, but ignore advice */
		return 0;
	}

	/*
	 * Filesystem's fadvise may need to take various locks.  We need to
	 * explicitly grab a reference because the vma (and hence the
	 * vma's reference to the file) can go away as soon as we drop
	 * mmap_lock.
	 */
	*prev = NULL;	/* tell sys_madvise we drop mmap_lock */
	get_file(file);
	offset = (loff_t)(start - vma->vm_start)
			+ ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	mmap_read_unlock(mm);
	vfs_fadvise(file, offset, end - start, POSIX_FADV_WILLNEED);
	fput(file);
	mmap_read_lock(mm);
	return 0;
}

static int madvise_cold_or_pageout_pte_range(pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct mm_walk *walk)
{
	struct madvise_walk_private *private = walk->private;
	struct mmu_gather *tlb = private->tlb;
	bool pageout = private->pageout;
	struct mm_struct *mm = tlb->mm;
	struct vm_area_struct *vma = walk->vma;
	pte_t *orig_pte, *pte, ptent;
	spinlock_t *ptl;
	struct page *page = NULL;
	LIST_HEAD(page_list);

	if (fatal_signal_pending(current))
		return -EINTR;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (pmd_trans_huge(*pmd)) {
		pmd_t orig_pmd;
		unsigned long next = pmd_addr_end(addr, end);

		tlb_change_page_size(tlb, HPAGE_PMD_SIZE);
		ptl = pmd_trans_huge_lock(pmd, vma);
		if (!ptl)
			return 0;

		orig_pmd = *pmd;
		if (is_huge_zero_pmd(orig_pmd))
			goto huge_unlock;

		if (unlikely(!pmd_present(orig_pmd))) {
			VM_BUG_ON(thp_migration_supported() &&
					!is_pmd_migration_entry(orig_pmd));
			goto huge_unlock;
		}

		page = pmd_page(orig_pmd);

		/* Do not interfere with other mappings of this page */
		if (page_mapcount(page) != 1)
			goto huge_unlock;

		if (next - addr != HPAGE_PMD_SIZE) {
			int err;

			get_page(page);
			spin_unlock(ptl);
			lock_page(page);
			err = split_huge_page(page);
			unlock_page(page);
			put_page(page);
			if (!err)
				goto regular_page;
			return 0;
		}

		if (pmd_young(orig_pmd)) {
			pmdp_invalidate(vma, addr, pmd);
			orig_pmd = pmd_mkold(orig_pmd);

			set_pmd_at(mm, addr, pmd, orig_pmd);
			tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
		}

		ClearPageReferenced(page);
		test_and_clear_page_young(page);
		if (pageout) {
			if (!isolate_lru_page(page)) {
				if (PageUnevictable(page))
					putback_lru_page(page);
				else
					list_add(&page->lru, &page_list);
			}
		} else
			deactivate_page(page);
huge_unlock:
		spin_unlock(ptl);
		if (pageout)
			reclaim_pages(&page_list);
		return 0;
	}

regular_page:
	if (pmd_trans_unstable(pmd))
		return 0;
#endif
	tlb_change_page_size(tlb, PAGE_SIZE);
	orig_pte = pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	for (; addr < end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;

		if (pte_none(ptent))
			continue;

		if (!pte_present(ptent))
			continue;

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/*
		 * Creating a THP page is expensive so split it only if we
		 * are sure it's worth. Split it if we are only owner.
		 */
		if (PageTransCompound(page)) {
			if (page_mapcount(page) != 1)
				break;
			get_page(page);
			if (!trylock_page(page)) {
				put_page(page);
				break;
			}
			pte_unmap_unlock(orig_pte, ptl);
			if (split_huge_page(page)) {
				unlock_page(page);
				put_page(page);
				pte_offset_map_lock(mm, pmd, addr, &ptl);
				break;
			}
			unlock_page(page);
			put_page(page);
			pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
			pte--;
			addr -= PAGE_SIZE;
			continue;
		}

		/* Do not interfere with other mappings of this page */
		if (page_mapcount(page) != 1)
			continue;

		VM_BUG_ON_PAGE(PageTransCompound(page), page);

		if (pte_young(ptent)) {
			ptent = ptep_get_and_clear_full(mm, addr, pte,
							tlb->fullmm);
			ptent = pte_mkold(ptent);
			set_pte_at(mm, addr, pte, ptent);
			tlb_remove_tlb_entry(tlb, pte, addr);
		}

		/*
		 * We are deactivating a page for accelerating reclaiming.
		 * VM couldn't reclaim the page unless we clear PG_young.
		 * As a side effect, it makes confuse idle-page tracking
		 * because they will miss recent referenced history.
		 */
		ClearPageReferenced(page);
		test_and_clear_page_young(page);
		if (pageout) {
			if (!isolate_lru_page(page)) {
				if (PageUnevictable(page))
					putback_lru_page(page);
				else
					list_add(&page->lru, &page_list);
			}
		} else
			deactivate_page(page);
	}

	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(orig_pte, ptl);
	if (pageout)
		reclaim_pages(&page_list);
	cond_resched();

	return 0;
}

static const struct mm_walk_ops cold_walk_ops = {
	.pmd_entry = madvise_cold_or_pageout_pte_range,
};

static void madvise_cold_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end)
{
	struct madvise_walk_private walk_private = {
		.pageout = false,
		.tlb = tlb,
	};

	tlb_start_vma(tlb, vma);
	walk_page_range(vma->vm_mm, addr, end, &cold_walk_ops, &walk_private);
	tlb_end_vma(tlb, vma);
}

static long madvise_cold(struct vm_area_struct *vma,
			struct vm_area_struct **prev,
			unsigned long start_addr, unsigned long end_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;

	*prev = vma;
	if (!can_madv_lru_vma(vma))
		return -EINVAL;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, start_addr, end_addr);
	madvise_cold_page_range(&tlb, vma, start_addr, end_addr);
	tlb_finish_mmu(&tlb, start_addr, end_addr);

	return 0;
}

static void madvise_pageout_page_range(struct mmu_gather *tlb,
			     struct vm_area_struct *vma,
			     unsigned long addr, unsigned long end)
{
	struct madvise_walk_private walk_private = {
		.pageout = true,
		.tlb = tlb,
	};

	tlb_start_vma(tlb, vma);
	walk_page_range(vma->vm_mm, addr, end, &cold_walk_ops, &walk_private);
	tlb_end_vma(tlb, vma);
}

static inline bool can_do_pageout(struct vm_area_struct *vma)
{
	if (vma_is_anonymous(vma))
		return true;
	if (!vma->vm_file)
		return false;
	/*
	 * paging out pagecache only for non-anonymous mappings that correspond
	 * to the files the calling process could (if tried) open for writing;
	 * otherwise we'd be including shared non-exclusive mappings, which
	 * opens a side channel.
	 */
	return inode_owner_or_capable(file_inode(vma->vm_file)) ||
		inode_permission(file_inode(vma->vm_file), MAY_WRITE) == 0;
}

static long madvise_pageout(struct vm_area_struct *vma,
			struct vm_area_struct **prev,
			unsigned long start_addr, unsigned long end_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_gather tlb;

	*prev = vma;
	if (!can_madv_lru_vma(vma))
		return -EINVAL;

	if (!can_do_pageout(vma))
		return 0;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, start_addr, end_addr);
	madvise_pageout_page_range(&tlb, vma, start_addr, end_addr);
	tlb_finish_mmu(&tlb, start_addr, end_addr);

	return 0;
}

static int madvise_free_pte_range(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)

{
	struct mmu_gather *tlb = walk->private;
	struct mm_struct *mm = tlb->mm;
	struct vm_area_struct *vma = walk->vma;
	spinlock_t *ptl;
	pte_t *orig_pte, *pte, ptent;
	struct page *page;
	int nr_swap = 0;
	unsigned long next;

	next = pmd_addr_end(addr, end);
	if (pmd_trans_huge(*pmd))
		if (madvise_free_huge_pmd(tlb, vma, pmd, addr, next))
			goto next;

	if (pmd_trans_unstable(pmd))
		return 0;

	tlb_change_page_size(tlb, PAGE_SIZE);
	orig_pte = pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	for (; addr != end; pte++, addr += PAGE_SIZE) {
		ptent = *pte;

		if (pte_none(ptent))
			continue;
		/*
		 * If the pte has swp_entry, just clear page table to
		 * prevent swap-in which is more expensive rather than
		 * (page allocation + zeroing).
		 */
		if (!pte_present(ptent)) {
			swp_entry_t entry;

			entry = pte_to_swp_entry(ptent);
			if (non_swap_entry(entry))
				continue;
			nr_swap--;
			free_swap_and_cache(entry);
			pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
			continue;
		}

		page = vm_normal_page(vma, addr, ptent);
		if (!page)
			continue;

		/*
		 * If pmd isn't transhuge but the page is THP and
		 * is owned by only this process, split it and
		 * deactivate all pages.
		 */
		if (PageTransCompound(page)) {
			if (page_mapcount(page) != 1)
				goto out;
			get_page(page);
			if (!trylock_page(page)) {
				put_page(page);
				goto out;
			}
			pte_unmap_unlock(orig_pte, ptl);
			if (split_huge_page(page)) {
				unlock_page(page);
				put_page(page);
				pte_offset_map_lock(mm, pmd, addr, &ptl);
				goto out;
			}
			unlock_page(page);
			put_page(page);
			pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
			pte--;
			addr -= PAGE_SIZE;
			continue;
		}

		VM_BUG_ON_PAGE(PageTransCompound(page), page);

		if (PageSwapCache(page) || PageDirty(page)) {
			if (!trylock_page(page))
				continue;
			/*
			 * If page is shared with others, we couldn't clear
			 * PG_dirty of the page.
			 */
			if (page_mapcount(page) != 1) {
				unlock_page(page);
				continue;
			}

			if (PageSwapCache(page) && !try_to_free_swap(page)) {
				unlock_page(page);
				continue;
			}

			ClearPageDirty(page);
			unlock_page(page);
		}

		if (pte_young(ptent) || pte_dirty(ptent)) {
			/*
			 * Some of architecture(ex, PPC) don't update TLB
			 * with set_pte_at and tlb_remove_tlb_entry so for
			 * the portability, remap the pte with old|clean
			 * after pte clearing.
			 */
			ptent = ptep_get_and_clear_full(mm, addr, pte,
							tlb->fullmm);

			ptent = pte_mkold(ptent);
			ptent = pte_mkclean(ptent);
			set_pte_at(mm, addr, pte, ptent);
			tlb_remove_tlb_entry(tlb, pte, addr);
		}
		mark_page_lazyfree(page);
	}
out:
	if (nr_swap) {
		if (current->mm == mm)
			sync_mm_rss(mm);

		add_mm_counter(mm, MM_SWAPENTS, nr_swap);
	}
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(orig_pte, ptl);
	cond_resched();
next:
	return 0;
}

static const struct mm_walk_ops madvise_free_walk_ops = {
	.pmd_entry		= madvise_free_pte_range,
};

static int madvise_free_single_vma(struct vm_area_struct *vma,
			unsigned long start_addr, unsigned long end_addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct mmu_notifier_range range;
	struct mmu_gather tlb;

	/* MADV_FREE works for only anon vma at the moment */
	if (!vma_is_anonymous(vma))
		return -EINVAL;

	range.start = max(vma->vm_start, start_addr);
	if (range.start >= vma->vm_end)
		return -EINVAL;
	range.end = min(vma->vm_end, end_addr);
	if (range.end <= vma->vm_start)
		return -EINVAL;
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, mm,
				range.start, range.end);

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, range.start, range.end);
	update_hiwater_rss(mm);

	mmu_notifier_invalidate_range_start(&range);
	tlb_start_vma(&tlb, vma);
	walk_page_range(vma->vm_mm, range.start, range.end,
			&madvise_free_walk_ops, &tlb);
	tlb_end_vma(&tlb, vma);
	mmu_notifier_invalidate_range_end(&range);
	tlb_finish_mmu(&tlb, range.start, range.end);

	return 0;
}

/*
 * Application no longer needs these pages.  If the pages are dirty,
 * it's OK to just throw them away.  The app will be more careful about
 * data it wants to keep.  Be sure to free swap resources too.  The
 * zap_page_range call sets things up for shrink_active_list to actually free
 * these pages later if no one else has touched them in the meantime,
 * although we could add these pages to a global reuse list for
 * shrink_active_list to pick up before reclaiming other pages.
 *
 * NB: This interface discards data rather than pushes it out to swap,
 * as some implementations do.  This has performance implications for
 * applications like large transactional databases which want to discard
 * pages in anonymous maps after committing to backing store the data
 * that was kept in them.  There is no reason to write this data out to
 * the swap area if the application is discarding it.
 *
 * An interface that causes the system to free clean pages and flush
 * dirty pages is already available as msync(MS_INVALIDATE).
 */
static long madvise_dontneed_single_vma(struct vm_area_struct *vma,
					unsigned long start, unsigned long end)
{
	zap_page_range(vma, start, end - start);
	return 0;
}

static long madvise_dontneed_free(struct vm_area_struct *vma,
				  struct vm_area_struct **prev,
				  unsigned long start, unsigned long end,
				  int behavior)
{
	struct mm_struct *mm = vma->vm_mm;

	*prev = vma;
	if (!can_madv_lru_vma(vma))
		return -EINVAL;

	if (!userfaultfd_remove(vma, start, end)) {
		*prev = NULL; /* mmap_lock has been dropped, prev is stale */

		mmap_read_lock(mm);
		vma = find_vma(mm, start);
		if (!vma)
			return -ENOMEM;
		if (start < vma->vm_start) {
			/*
			 * This "vma" under revalidation is the one
			 * with the lowest vma->vm_start where start
			 * is also < vma->vm_end. If start <
			 * vma->vm_start it means an hole materialized
			 * in the user address space within the
			 * virtual range passed to MADV_DONTNEED
			 * or MADV_FREE.
			 */
			return -ENOMEM;
		}
		if (!can_madv_lru_vma(vma))
			return -EINVAL;
		if (end > vma->vm_end) {
			/*
			 * Don't fail if end > vma->vm_end. If the old
			 * vma was splitted while the mmap_lock was
			 * released the effect of the concurrent
			 * operation may not cause madvise() to
			 * have an undefined result. There may be an
			 * adjacent next vma that we'll walk
			 * next. userfaultfd_remove() will generate an
			 * UFFD_EVENT_REMOVE repetition on the
			 * end-vma->vm_end range, but the manager can
			 * handle a repetition fine.
			 */
			end = vma->vm_end;
		}
		VM_WARN_ON(start >= end);
	}

	if (behavior == MADV_DONTNEED)
		return madvise_dontneed_single_vma(vma, start, end);
	else if (behavior == MADV_FREE)
		return madvise_free_single_vma(vma, start, end);
	else
		return -EINVAL;
}

/*
 * Application wants to free up the pages and associated backing store.
 * This is effectively punching a hole into the middle of a file.
 */
static long madvise_remove(struct vm_area_struct *vma,
				struct vm_area_struct **prev,
				unsigned long start, unsigned long end)
{
	loff_t offset;
	int error;
	struct file *f;
	struct mm_struct *mm = vma->vm_mm;

	*prev = NULL;	/* tell sys_madvise we drop mmap_lock */

	if (vma->vm_flags & VM_LOCKED)
		return -EINVAL;

	f = vma->vm_file;

	if (!f || !f->f_mapping || !f->f_mapping->host) {
			return -EINVAL;
	}

	if ((vma->vm_flags & (VM_SHARED|VM_WRITE)) != (VM_SHARED|VM_WRITE))
		return -EACCES;

	offset = (loff_t)(start - vma->vm_start)
			+ ((loff_t)vma->vm_pgoff << PAGE_SHIFT);

	/*
	 * Filesystem's fallocate may need to take i_mutex.  We need to
	 * explicitly grab a reference because the vma (and hence the
	 * vma's reference to the file) can go away as soon as we drop
	 * mmap_lock.
	 */
	get_file(f);
	if (userfaultfd_remove(vma, start, end)) {
		/* mmap_lock was not released by userfaultfd_remove() */
		mmap_read_unlock(mm);
	}
	error = vfs_fallocate(f,
				FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				offset, end - start);
	fput(f);
	mmap_read_lock(mm);
	return error;
}

#ifdef CONFIG_MEMORY_FAILURE
/*
 * Error injection support for memory error handling.
 */
static int madvise_inject_error(int behavior,
		unsigned long start, unsigned long end)
{
	struct zone *zone;
	unsigned long size;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;


	for (; start < end; start += size) {
		unsigned long pfn;
		struct page *page;
		int ret;

		ret = get_user_pages_fast(start, 1, 0, &page);
		if (ret != 1)
			return ret;
		pfn = page_to_pfn(page);

		/*
		 * When soft offlining hugepages, after migrating the page
		 * we dissolve it, therefore in the second loop "page" will
		 * no longer be a compound page.
		 */
		size = page_size(compound_head(page));

		if (behavior == MADV_SOFT_OFFLINE) {
			pr_info("Soft offlining pfn %#lx at process virtual address %#lx\n",
				 pfn, start);
			ret = soft_offline_page(pfn, MF_COUNT_INCREASED);
		} else {
			pr_info("Injecting memory failure for pfn %#lx at process virtual address %#lx\n",
				 pfn, start);
			/*
			 * Drop the page reference taken by get_user_pages_fast(). In
			 * the absence of MF_COUNT_INCREASED the memory_failure()
			 * routine is responsible for pinning the page to prevent it
			 * from being released back to the page allocator.
			 */
			put_page(page);
			ret = memory_failure(pfn, 0);
		}

		if (ret)
			return ret;
	}

	/* Ensure that all poisoned pages are removed from per-cpu lists */
	for_each_populated_zone(zone)
		drain_all_pages(zone);

	return 0;
}
#endif

static long
madvise_vma(struct vm_area_struct *vma, struct vm_area_struct **prev,
		unsigned long start, unsigned long end, int behavior)
{
	switch (behavior) {
	case MADV_REMOVE:
		return madvise_remove(vma, prev, start, end);
	case MADV_WILLNEED:
		return madvise_willneed(vma, prev, start, end);
	case MADV_COLD:
		return madvise_cold(vma, prev, start, end);
	case MADV_PAGEOUT:
		return madvise_pageout(vma, prev, start, end);
	case MADV_FREE:
	case MADV_DONTNEED:
		return madvise_dontneed_free(vma, prev, start, end, behavior);
	default:
		return madvise_behavior(vma, prev, start, end, behavior);
	}
}

static bool
madvise_behavior_valid(int behavior)
{
	switch (behavior) {
	case MADV_DOFORK:
	case MADV_DONTFORK:
	case MADV_NORMAL:
	case MADV_SEQUENTIAL:
	case MADV_RANDOM:
	case MADV_REMOVE:
	case MADV_WILLNEED:
	case MADV_DONTNEED:
	case MADV_FREE:
	case MADV_COLD:
	case MADV_PAGEOUT:
#ifdef CONFIG_KSM
	case MADV_MERGEABLE:
	case MADV_UNMERGEABLE:
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	case MADV_HUGEPAGE:
	case MADV_NOHUGEPAGE:
#endif
	case MADV_DONTDUMP:
	case MADV_DODUMP:
	case MADV_WIPEONFORK:
	case MADV_KEEPONFORK:
#ifdef CONFIG_MEMORY_FAILURE
	case MADV_SOFT_OFFLINE:
	case MADV_HWPOISON:
#endif
		return true;

	default:
		return false;
	}
}

static bool
process_madvise_behavior_valid(int behavior)
{
	switch (behavior) {
	case MADV_COLD:
	case MADV_PAGEOUT:
		return true;
	default:
		return false;
	}
}

/*
 * The madvise(2) system call.
 *
 * Applications can use madvise() to advise the kernel how it should
 * handle paging I/O in this VM area.  The idea is to help the kernel
 * use appropriate read-ahead and caching techniques.  The information
 * provided is advisory only, and can be safely disregarded by the
 * kernel without affecting the correct operation of the application.
 *
 * behavior values:
 *  MADV_NORMAL - the default behavior is to read clusters.  This
 *		results in some read-ahead and read-behind.
 *  MADV_RANDOM - the system should read the minimum amount of data
 *		on any access, since it is unlikely that the appli-
 *		cation will need more than what it asks for.
 *  MADV_SEQUENTIAL - pages in the given range will probably be accessed
 *		once, so they can be aggressively read ahead, and
 *		can be freed soon after they are accessed.
 *  MADV_WILLNEED - the application is notifying the system to read
 *		some pages ahead.
 *  MADV_DONTNEED - the application is finished with the given range,
 *		so the kernel can free resources associated with it.
 *  MADV_FREE - the application marks pages in the given range as lazy free,
 *		where actual purges are postponed until memory pressure happens.
 *  MADV_REMOVE - the application wants to free up the given range of
 *		pages and associated backing store.
 *  MADV_DONTFORK - omit this area from child's address space when forking:
 *		typically, to avoid COWing pages pinned by get_user_pages().
 *  MADV_DOFORK - cancel MADV_DONTFORK: no longer omit this area when forking.
 *  MADV_WIPEONFORK - present the child process with zero-filled memory in this
 *              range after a fork.
 *  MADV_KEEPONFORK - undo the effect of MADV_WIPEONFORK
 *  MADV_HWPOISON - trigger memory error handler as if the given memory range
 *		were corrupted by unrecoverable hardware memory failure.
 *  MADV_SOFT_OFFLINE - try to soft-offline the given range of memory.
 *  MADV_MERGEABLE - the application recommends that KSM try to merge pages in
 *		this area with pages of identical content from other such areas.
 *  MADV_UNMERGEABLE- cancel MADV_MERGEABLE: no longer merge pages with others.
 *  MADV_HUGEPAGE - the application wants to back the given range by transparent
 *		huge pages in the future. Existing pages might be coalesced and
 *		new pages might be allocated as THP.
 *  MADV_NOHUGEPAGE - mark the given range as not worth being backed by
 *		transparent huge pages so the existing pages will not be
 *		coalesced into THP and new pages will not be allocated as THP.
 *  MADV_DONTDUMP - the application wants to prevent pages in the given range
 *		from being included in its core dump.
 *  MADV_DODUMP - cancel MADV_DONTDUMP: no longer exclude from core dump.
 *  MADV_COLD - the application is not expected to use this memory soon,
 *		deactivate pages in this range so that they can be reclaimed
 *		easily if memory pressure hanppens.
 *  MADV_PAGEOUT - the application is not expected to use this memory soon,
 *		page out the pages in this range immediately.
 *
 * return values:
 *  zero    - success
 *  -EINVAL - start + len < 0, start is not page-aligned,
 *		"behavior" is not a valid value, or application
 *		is attempting to release locked or shared pages,
 *		or the specified address range includes file, Huge TLB,
 *		MAP_SHARED or VMPFNMAP range.
 *  -ENOMEM - addresses in the specified range are not currently
 *		mapped, or are outside the AS of the process.
 *  -EIO    - an I/O error occurred while paging in data.
 *  -EBADF  - map exists, but area maps something that isn't a file.
 *  -EAGAIN - a kernel resource was temporarily unavailable.
 */
int do_madvise(struct mm_struct *mm, unsigned long start, size_t len_in, int behavior)
{
	unsigned long end, tmp;
	struct vm_area_struct *vma, *prev;
	int unmapped_error = 0;
	int error = -EINVAL;
	int write;
	size_t len;
	struct blk_plug plug;

	start = untagged_addr(start);

	if (!madvise_behavior_valid(behavior))
		return error;

	if (!PAGE_ALIGNED(start))
		return error;
	len = PAGE_ALIGN(len_in);

	/* Check to see whether len was rounded up from small -ve to zero */
	if (len_in && !len)
		return error;

	end = start + len;
	if (end < start)
		return error;

	error = 0;
	if (end == start)
		return error;

#ifdef CONFIG_MEMORY_FAILURE
	if (behavior == MADV_HWPOISON || behavior == MADV_SOFT_OFFLINE)
		return madvise_inject_error(behavior, start, start + len_in);
#endif

	write = madvise_need_mmap_write(behavior);
	if (write) {
		if (mmap_write_lock_killable(mm))
			return -EINTR;
	} else {
		mmap_read_lock(mm);
	}

	/*
	 * If the interval [start,end) covers some unmapped address
	 * ranges, just ignore them, but return -ENOMEM at the end.
	 * - different from the way of handling in mlock etc.
	 */
	vma = find_vma_prev(mm, start, &prev);
	if (vma && start > vma->vm_start)
		prev = vma;

	blk_start_plug(&plug);
	for (;;) {
		/* Still start < end. */
		error = -ENOMEM;
		if (!vma)
			goto out;

		/* Here start < (end|vma->vm_end). */
		if (start < vma->vm_start) {
			unmapped_error = -ENOMEM;
			start = vma->vm_start;
			if (start >= end)
				goto out;
		}

		/* Here vma->vm_start <= start < (end|vma->vm_end) */
		tmp = vma->vm_end;
		if (end < tmp)
			tmp = end;

		/* Here vma->vm_start <= start < tmp <= (end|vma->vm_end). */
		error = madvise_vma(vma, &prev, start, tmp, behavior);
		if (error)
			goto out;
		start = tmp;
		if (prev && start < prev->vm_end)
			start = prev->vm_end;
		error = unmapped_error;
		if (start >= end)
			goto out;
		if (prev)
			vma = prev->vm_next;
		else	/* madvise_remove dropped mmap_lock */
			vma = find_vma(mm, start);
	}
out:
	blk_finish_plug(&plug);
	if (write)
		mmap_write_unlock(mm);
	else
		mmap_read_unlock(mm);

	return error;
}

SYSCALL_DEFINE3(madvise, unsigned long, start, size_t, len_in, int, behavior)
{
	return do_madvise(current->mm, start, len_in, behavior);
}

SYSCALL_DEFINE5(process_madvise, int, pidfd, const struct iovec __user *, vec,
		size_t, vlen, int, behavior, unsigned int, flags)
{
	ssize_t ret;
	struct iovec iovstack[UIO_FASTIOV], iovec;
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	struct pid *pid;
	struct task_struct *task;
	struct mm_struct *mm;
	size_t total_len;
	unsigned int f_flags;

	if (flags != 0) {
		ret = -EINVAL;
		goto out;
	}

	ret = import_iovec(READ, vec, vlen, ARRAY_SIZE(iovstack), &iov, &iter);
	if (ret < 0)
		goto out;

	pid = pidfd_get_pid(pidfd, &f_flags);
	if (IS_ERR(pid)) {
		ret = PTR_ERR(pid);
		goto free_iov;
	}

	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task) {
		ret = -ESRCH;
		goto put_pid;
	}

	if (task->mm != current->mm &&
			!process_madvise_behavior_valid(behavior)) {
		ret = -EINVAL;
		goto release_task;
	}

	mm = mm_access(task, PTRACE_MODE_ATTACH_FSCREDS);
	if (IS_ERR_OR_NULL(mm)) {
		ret = IS_ERR(mm) ? PTR_ERR(mm) : -ESRCH;
		goto release_task;
	}

	total_len = iov_iter_count(&iter);

	while (iov_iter_count(&iter)) {
		iovec = iov_iter_iovec(&iter);
		ret = do_madvise(mm, (unsigned long)iovec.iov_base,
					iovec.iov_len, behavior);
		if (ret < 0)
			break;
		iov_iter_advance(&iter, iovec.iov_len);
	}

	if (ret == 0)
		ret = total_len - iov_iter_count(&iter);

	mmput(mm);
	return ret;

release_task:
	put_task_struct(task);
put_pid:
	put_pid(pid);
free_iov:
	kfree(iov);
out:
	return ret;
}
