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
#include <linux/mm_inline.h>
#include <linux/string.h>
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
#include "swap.h"

#define __MADV_SET_ANON_VMA_NAME (-1)

/*
 * Maximum number of attempts we make to install guard pages before we give up
 * and return -ERESTARTNOINTR to have userspace try again.
 */
#define MAX_MADVISE_GUARD_RETRIES 3

struct madvise_walk_private {
	struct mmu_gather *tlb;
	bool pageout;
};

enum madvise_lock_mode {
	MADVISE_NO_LOCK,
	MADVISE_MMAP_READ_LOCK,
	MADVISE_MMAP_WRITE_LOCK,
	MADVISE_VMA_READ_LOCK,
};

struct madvise_behavior_range {
	unsigned long start;
	unsigned long end;
};

struct madvise_behavior {
	struct mm_struct *mm;
	int behavior;
	struct mmu_gather *tlb;
	enum madvise_lock_mode lock_mode;
	struct anon_vma_name *anon_name;

	/*
	 * The range over which the behaviour is currently being applied. If
	 * traversing multiple VMAs, this is updated for each.
	 */
	struct madvise_behavior_range range;
	/* The VMA and VMA preceding it (if applicable) currently targeted. */
	struct vm_area_struct *prev;
	struct vm_area_struct *vma;
	bool lock_dropped;
};

#ifdef CONFIG_ANON_VMA_NAME
static int madvise_walk_vmas(struct madvise_behavior *madv_behavior);

struct anon_vma_name *anon_vma_name_alloc(const char *name)
{
	struct anon_vma_name *anon_name;
	size_t count;

	/* Add 1 for NUL terminator at the end of the anon_name->name */
	count = strlen(name) + 1;
	anon_name = kmalloc(struct_size(anon_name, name, count), GFP_KERNEL);
	if (anon_name) {
		kref_init(&anon_name->kref);
		memcpy(anon_name->name, name, count);
	}

	return anon_name;
}

void anon_vma_name_free(struct kref *kref)
{
	struct anon_vma_name *anon_name =
			container_of(kref, struct anon_vma_name, kref);
	kfree(anon_name);
}

struct anon_vma_name *anon_vma_name(struct vm_area_struct *vma)
{
	if (!rwsem_is_locked(&vma->vm_mm->mmap_lock))
		vma_assert_locked(vma);

	return vma->anon_name;
}

/* mmap_lock should be write-locked */
static int replace_anon_vma_name(struct vm_area_struct *vma,
				 struct anon_vma_name *anon_name)
{
	struct anon_vma_name *orig_name = anon_vma_name(vma);

	if (!anon_name) {
		vma->anon_name = NULL;
		anon_vma_name_put(orig_name);
		return 0;
	}

	if (anon_vma_name_eq(orig_name, anon_name))
		return 0;

	vma->anon_name = anon_vma_name_reuse(anon_name);
	anon_vma_name_put(orig_name);

	return 0;
}
#else /* CONFIG_ANON_VMA_NAME */
static int replace_anon_vma_name(struct vm_area_struct *vma,
				 struct anon_vma_name *anon_name)
{
	if (anon_name)
		return -EINVAL;

	return 0;
}
#endif /* CONFIG_ANON_VMA_NAME */
/*
 * Update the vm_flags or anon_name on region of a vma, splitting it or merging
 * it as necessary. Must be called with mmap_lock held for writing.
 */
static int madvise_update_vma(vm_flags_t new_flags,
		struct madvise_behavior *madv_behavior)
{
	struct vm_area_struct *vma = madv_behavior->vma;
	struct madvise_behavior_range *range = &madv_behavior->range;
	struct anon_vma_name *anon_name = madv_behavior->anon_name;
	bool set_new_anon_name = madv_behavior->behavior == __MADV_SET_ANON_VMA_NAME;
	VMA_ITERATOR(vmi, madv_behavior->mm, range->start);

	if (new_flags == vma->vm_flags && (!set_new_anon_name ||
			anon_vma_name_eq(anon_vma_name(vma), anon_name)))
		return 0;

	if (set_new_anon_name)
		vma = vma_modify_name(&vmi, madv_behavior->prev, vma,
			range->start, range->end, anon_name);
	else
		vma = vma_modify_flags(&vmi, madv_behavior->prev, vma,
			range->start, range->end, new_flags);

	if (IS_ERR(vma))
		return PTR_ERR(vma);

	madv_behavior->vma = vma;

	/* vm_flags is protected by the mmap_lock held in write mode. */
	vma_start_write(vma);
	vm_flags_reset(vma, new_flags);
	if (set_new_anon_name)
		return replace_anon_vma_name(vma, anon_name);

	return 0;
}

#ifdef CONFIG_SWAP
static int swapin_walk_pmd_entry(pmd_t *pmd, unsigned long start,
		unsigned long end, struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->private;
	struct swap_iocb *splug = NULL;
	pte_t *ptep = NULL;
	spinlock_t *ptl;
	unsigned long addr;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		pte_t pte;
		swp_entry_t entry;
		struct folio *folio;

		if (!ptep++) {
			ptep = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
			if (!ptep)
				break;
		}

		pte = ptep_get(ptep);
		if (!is_swap_pte(pte))
			continue;
		entry = pte_to_swp_entry(pte);
		if (unlikely(non_swap_entry(entry)))
			continue;

		pte_unmap_unlock(ptep, ptl);
		ptep = NULL;

		folio = read_swap_cache_async(entry, GFP_HIGHUSER_MOVABLE,
					     vma, addr, &splug);
		if (folio)
			folio_put(folio);
	}

	if (ptep)
		pte_unmap_unlock(ptep, ptl);
	swap_read_unplug(splug);
	cond_resched();

	return 0;
}

static const struct mm_walk_ops swapin_walk_ops = {
	.pmd_entry		= swapin_walk_pmd_entry,
	.walk_lock		= PGWALK_RDLOCK,
};

static void shmem_swapin_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end,
		struct address_space *mapping)
{
	XA_STATE(xas, &mapping->i_pages, linear_page_index(vma, start));
	pgoff_t end_index = linear_page_index(vma, end) - 1;
	struct folio *folio;
	struct swap_iocb *splug = NULL;

	rcu_read_lock();
	xas_for_each(&xas, folio, end_index) {
		unsigned long addr;
		swp_entry_t entry;

		if (!xa_is_value(folio))
			continue;
		entry = radix_to_swp_entry(folio);
		/* There might be swapin error entries in shmem mapping. */
		if (non_swap_entry(entry))
			continue;

		addr = vma->vm_start +
			((xas.xa_index - vma->vm_pgoff) << PAGE_SHIFT);
		xas_pause(&xas);
		rcu_read_unlock();

		folio = read_swap_cache_async(entry, mapping_gfp_mask(mapping),
					     vma, addr, &splug);
		if (folio)
			folio_put(folio);

		rcu_read_lock();
	}
	rcu_read_unlock();
	swap_read_unplug(splug);
}
#endif		/* CONFIG_SWAP */

static void mark_mmap_lock_dropped(struct madvise_behavior *madv_behavior)
{
	VM_WARN_ON_ONCE(madv_behavior->lock_mode == MADVISE_VMA_READ_LOCK);
	madv_behavior->lock_dropped = true;
}

/*
 * Schedule all required I/O operations.  Do not wait for completion.
 */
static long madvise_willneed(struct madvise_behavior *madv_behavior)
{
	struct vm_area_struct *vma = madv_behavior->vma;
	struct mm_struct *mm = madv_behavior->mm;
	struct file *file = vma->vm_file;
	unsigned long start = madv_behavior->range.start;
	unsigned long end = madv_behavior->range.end;
	loff_t offset;

#ifdef CONFIG_SWAP
	if (!file) {
		walk_page_range_vma(vma, start, end, &swapin_walk_ops, vma);
		lru_add_drain(); /* Push any new pages onto the LRU now */
		return 0;
	}

	if (shmem_mapping(file->f_mapping)) {
		shmem_swapin_range(vma, start, end, file->f_mapping);
		lru_add_drain(); /* Push any new pages onto the LRU now */
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
	mark_mmap_lock_dropped(madv_behavior);
	get_file(file);
	offset = (loff_t)(start - vma->vm_start)
			+ ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	mmap_read_unlock(mm);
	vfs_fadvise(file, offset, end - start, POSIX_FADV_WILLNEED);
	fput(file);
	mmap_read_lock(mm);
	return 0;
}

static inline bool can_do_file_pageout(struct vm_area_struct *vma)
{
	if (!vma->vm_file)
		return false;
	/*
	 * paging out pagecache only for non-anonymous mappings that correspond
	 * to the files the calling process could (if tried) open for writing;
	 * otherwise we'd be including shared non-exclusive mappings, which
	 * opens a side channel.
	 */
	return inode_owner_or_capable(&nop_mnt_idmap,
				      file_inode(vma->vm_file)) ||
	       file_permission(vma->vm_file, MAY_WRITE) == 0;
}

static inline int madvise_folio_pte_batch(unsigned long addr, unsigned long end,
					  struct folio *folio, pte_t *ptep,
					  pte_t *ptentp)
{
	int max_nr = (end - addr) / PAGE_SIZE;

	return folio_pte_batch_flags(folio, NULL, ptep, ptentp, max_nr,
				     FPB_MERGE_YOUNG_DIRTY);
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
	pte_t *start_pte, *pte, ptent;
	spinlock_t *ptl;
	struct folio *folio = NULL;
	LIST_HEAD(folio_list);
	bool pageout_anon_only_filter;
	unsigned int batch_count = 0;
	int nr;

	if (fatal_signal_pending(current))
		return -EINTR;

	pageout_anon_only_filter = pageout && !vma_is_anonymous(vma) &&
					!can_do_file_pageout(vma);

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

		folio = pmd_folio(orig_pmd);

		/* Do not interfere with other mappings of this folio */
		if (folio_maybe_mapped_shared(folio))
			goto huge_unlock;

		if (pageout_anon_only_filter && !folio_test_anon(folio))
			goto huge_unlock;

		if (next - addr != HPAGE_PMD_SIZE) {
			int err;

			folio_get(folio);
			spin_unlock(ptl);
			folio_lock(folio);
			err = split_folio(folio);
			folio_unlock(folio);
			folio_put(folio);
			if (!err)
				goto regular_folio;
			return 0;
		}

		if (!pageout && pmd_young(orig_pmd)) {
			pmdp_invalidate(vma, addr, pmd);
			orig_pmd = pmd_mkold(orig_pmd);

			set_pmd_at(mm, addr, pmd, orig_pmd);
			tlb_remove_pmd_tlb_entry(tlb, pmd, addr);
		}

		folio_clear_referenced(folio);
		folio_test_clear_young(folio);
		if (folio_test_active(folio))
			folio_set_workingset(folio);
		if (pageout) {
			if (folio_isolate_lru(folio)) {
				if (folio_test_unevictable(folio))
					folio_putback_lru(folio);
				else
					list_add(&folio->lru, &folio_list);
			}
		} else
			folio_deactivate(folio);
huge_unlock:
		spin_unlock(ptl);
		if (pageout)
			reclaim_pages(&folio_list);
		return 0;
	}

regular_folio:
#endif
	tlb_change_page_size(tlb, PAGE_SIZE);
restart:
	start_pte = pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (!start_pte)
		return 0;
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	for (; addr < end; pte += nr, addr += nr * PAGE_SIZE) {
		nr = 1;
		ptent = ptep_get(pte);

		if (++batch_count == SWAP_CLUSTER_MAX) {
			batch_count = 0;
			if (need_resched()) {
				arch_leave_lazy_mmu_mode();
				pte_unmap_unlock(start_pte, ptl);
				cond_resched();
				goto restart;
			}
		}

		if (pte_none(ptent))
			continue;

		if (!pte_present(ptent))
			continue;

		folio = vm_normal_folio(vma, addr, ptent);
		if (!folio || folio_is_zone_device(folio))
			continue;

		/*
		 * If we encounter a large folio, only split it if it is not
		 * fully mapped within the range we are operating on. Otherwise
		 * leave it as is so that it can be swapped out whole. If we
		 * fail to split a folio, leave it in place and advance to the
		 * next pte in the range.
		 */
		if (folio_test_large(folio)) {
			nr = madvise_folio_pte_batch(addr, end, folio, pte, &ptent);
			if (nr < folio_nr_pages(folio)) {
				int err;

				if (folio_maybe_mapped_shared(folio))
					continue;
				if (pageout_anon_only_filter && !folio_test_anon(folio))
					continue;
				if (!folio_trylock(folio))
					continue;
				folio_get(folio);
				arch_leave_lazy_mmu_mode();
				pte_unmap_unlock(start_pte, ptl);
				start_pte = NULL;
				err = split_folio(folio);
				folio_unlock(folio);
				folio_put(folio);
				start_pte = pte =
					pte_offset_map_lock(mm, pmd, addr, &ptl);
				if (!start_pte)
					break;
				flush_tlb_batched_pending(mm);
				arch_enter_lazy_mmu_mode();
				if (!err)
					nr = 0;
				continue;
			}
		}

		/*
		 * Do not interfere with other mappings of this folio and
		 * non-LRU folio. If we have a large folio at this point, we
		 * know it is fully mapped so if its mapcount is the same as its
		 * number of pages, it must be exclusive.
		 */
		if (!folio_test_lru(folio) ||
		    folio_mapcount(folio) != folio_nr_pages(folio))
			continue;

		if (pageout_anon_only_filter && !folio_test_anon(folio))
			continue;

		if (!pageout && pte_young(ptent)) {
			clear_young_dirty_ptes(vma, addr, pte, nr,
					       CYDP_CLEAR_YOUNG);
			tlb_remove_tlb_entries(tlb, pte, nr, addr);
		}

		/*
		 * We are deactivating a folio for accelerating reclaiming.
		 * VM couldn't reclaim the folio unless we clear PG_young.
		 * As a side effect, it makes confuse idle-page tracking
		 * because they will miss recent referenced history.
		 */
		folio_clear_referenced(folio);
		folio_test_clear_young(folio);
		if (folio_test_active(folio))
			folio_set_workingset(folio);
		if (pageout) {
			if (folio_isolate_lru(folio)) {
				if (folio_test_unevictable(folio))
					folio_putback_lru(folio);
				else
					list_add(&folio->lru, &folio_list);
			}
		} else
			folio_deactivate(folio);
	}

	if (start_pte) {
		arch_leave_lazy_mmu_mode();
		pte_unmap_unlock(start_pte, ptl);
	}
	if (pageout)
		reclaim_pages(&folio_list);
	cond_resched();

	return 0;
}

static const struct mm_walk_ops cold_walk_ops = {
	.pmd_entry = madvise_cold_or_pageout_pte_range,
	.walk_lock = PGWALK_RDLOCK,
};

static void madvise_cold_page_range(struct mmu_gather *tlb,
		struct madvise_behavior *madv_behavior)

{
	struct vm_area_struct *vma = madv_behavior->vma;
	struct madvise_behavior_range *range = &madv_behavior->range;
	struct madvise_walk_private walk_private = {
		.pageout = false,
		.tlb = tlb,
	};

	tlb_start_vma(tlb, vma);
	walk_page_range_vma(vma, range->start, range->end, &cold_walk_ops,
			&walk_private);
	tlb_end_vma(tlb, vma);
}

static inline bool can_madv_lru_vma(struct vm_area_struct *vma)
{
	return !(vma->vm_flags & (VM_LOCKED|VM_PFNMAP|VM_HUGETLB));
}

static long madvise_cold(struct madvise_behavior *madv_behavior)
{
	struct vm_area_struct *vma = madv_behavior->vma;
	struct mmu_gather tlb;

	if (!can_madv_lru_vma(vma))
		return -EINVAL;

	lru_add_drain();
	tlb_gather_mmu(&tlb, madv_behavior->mm);
	madvise_cold_page_range(&tlb, madv_behavior);
	tlb_finish_mmu(&tlb);

	return 0;
}

static void madvise_pageout_page_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma,
		struct madvise_behavior_range *range)
{
	struct madvise_walk_private walk_private = {
		.pageout = true,
		.tlb = tlb,
	};

	tlb_start_vma(tlb, vma);
	walk_page_range_vma(vma, range->start, range->end, &cold_walk_ops,
			    &walk_private);
	tlb_end_vma(tlb, vma);
}

static long madvise_pageout(struct madvise_behavior *madv_behavior)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma = madv_behavior->vma;

	if (!can_madv_lru_vma(vma))
		return -EINVAL;

	/*
	 * If the VMA belongs to a private file mapping, there can be private
	 * dirty pages which can be paged out if even this process is neither
	 * owner nor write capable of the file. We allow private file mappings
	 * further to pageout dirty anon pages.
	 */
	if (!vma_is_anonymous(vma) && (!can_do_file_pageout(vma) &&
				(vma->vm_flags & VM_MAYSHARE)))
		return 0;

	lru_add_drain();
	tlb_gather_mmu(&tlb, madv_behavior->mm);
	madvise_pageout_page_range(&tlb, vma, &madv_behavior->range);
	tlb_finish_mmu(&tlb);

	return 0;
}

static int madvise_free_pte_range(pmd_t *pmd, unsigned long addr,
				unsigned long end, struct mm_walk *walk)

{
	const cydp_t cydp_flags = CYDP_CLEAR_YOUNG | CYDP_CLEAR_DIRTY;
	struct mmu_gather *tlb = walk->private;
	struct mm_struct *mm = tlb->mm;
	struct vm_area_struct *vma = walk->vma;
	spinlock_t *ptl;
	pte_t *start_pte, *pte, ptent;
	struct folio *folio;
	int nr_swap = 0;
	unsigned long next;
	int nr, max_nr;

	next = pmd_addr_end(addr, end);
	if (pmd_trans_huge(*pmd))
		if (madvise_free_huge_pmd(tlb, vma, pmd, addr, next))
			return 0;

	tlb_change_page_size(tlb, PAGE_SIZE);
	start_pte = pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	if (!start_pte)
		return 0;
	flush_tlb_batched_pending(mm);
	arch_enter_lazy_mmu_mode();
	for (; addr != end; pte += nr, addr += PAGE_SIZE * nr) {
		nr = 1;
		ptent = ptep_get(pte);

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
			if (!non_swap_entry(entry)) {
				max_nr = (end - addr) / PAGE_SIZE;
				nr = swap_pte_batch(pte, max_nr, ptent);
				nr_swap -= nr;
				free_swap_and_cache_nr(entry, nr);
				clear_not_present_full_ptes(mm, addr, pte, nr, tlb->fullmm);
			} else if (is_hwpoison_entry(entry) ||
				   is_poisoned_swp_entry(entry)) {
				pte_clear_not_present_full(mm, addr, pte, tlb->fullmm);
			}
			continue;
		}

		folio = vm_normal_folio(vma, addr, ptent);
		if (!folio || folio_is_zone_device(folio))
			continue;

		/*
		 * If we encounter a large folio, only split it if it is not
		 * fully mapped within the range we are operating on. Otherwise
		 * leave it as is so that it can be marked as lazyfree. If we
		 * fail to split a folio, leave it in place and advance to the
		 * next pte in the range.
		 */
		if (folio_test_large(folio)) {
			nr = madvise_folio_pte_batch(addr, end, folio, pte, &ptent);
			if (nr < folio_nr_pages(folio)) {
				int err;

				if (folio_maybe_mapped_shared(folio))
					continue;
				if (!folio_trylock(folio))
					continue;
				folio_get(folio);
				arch_leave_lazy_mmu_mode();
				pte_unmap_unlock(start_pte, ptl);
				start_pte = NULL;
				err = split_folio(folio);
				folio_unlock(folio);
				folio_put(folio);
				pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
				start_pte = pte;
				if (!start_pte)
					break;
				flush_tlb_batched_pending(mm);
				arch_enter_lazy_mmu_mode();
				if (!err)
					nr = 0;
				continue;
			}
		}

		if (folio_test_swapcache(folio) || folio_test_dirty(folio)) {
			if (!folio_trylock(folio))
				continue;
			/*
			 * If we have a large folio at this point, we know it is
			 * fully mapped so if its mapcount is the same as its
			 * number of pages, it must be exclusive.
			 */
			if (folio_mapcount(folio) != folio_nr_pages(folio)) {
				folio_unlock(folio);
				continue;
			}

			if (folio_test_swapcache(folio) &&
			    !folio_free_swap(folio)) {
				folio_unlock(folio);
				continue;
			}

			folio_clear_dirty(folio);
			folio_unlock(folio);
		}

		if (pte_young(ptent) || pte_dirty(ptent)) {
			clear_young_dirty_ptes(vma, addr, pte, nr, cydp_flags);
			tlb_remove_tlb_entries(tlb, pte, nr, addr);
		}
		folio_mark_lazyfree(folio);
	}

	if (nr_swap)
		add_mm_counter(mm, MM_SWAPENTS, nr_swap);
	if (start_pte) {
		arch_leave_lazy_mmu_mode();
		pte_unmap_unlock(start_pte, ptl);
	}
	cond_resched();

	return 0;
}

static inline enum page_walk_lock get_walk_lock(enum madvise_lock_mode mode)
{
	switch (mode) {
	case MADVISE_VMA_READ_LOCK:
		return PGWALK_VMA_RDLOCK_VERIFY;
	case MADVISE_MMAP_READ_LOCK:
		return PGWALK_RDLOCK;
	default:
		/* Other modes don't require fixing up the walk_lock */
		WARN_ON_ONCE(1);
		return PGWALK_RDLOCK;
	}
}

static int madvise_free_single_vma(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;
	struct vm_area_struct *vma = madv_behavior->vma;
	unsigned long start_addr = madv_behavior->range.start;
	unsigned long end_addr = madv_behavior->range.end;
	struct mmu_notifier_range range;
	struct mmu_gather *tlb = madv_behavior->tlb;
	struct mm_walk_ops walk_ops = {
		.pmd_entry		= madvise_free_pte_range,
	};

	/* MADV_FREE works for only anon vma at the moment */
	if (!vma_is_anonymous(vma))
		return -EINVAL;

	range.start = max(vma->vm_start, start_addr);
	if (range.start >= vma->vm_end)
		return -EINVAL;
	range.end = min(vma->vm_end, end_addr);
	if (range.end <= vma->vm_start)
		return -EINVAL;
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm,
				range.start, range.end);

	lru_add_drain();
	update_hiwater_rss(mm);

	mmu_notifier_invalidate_range_start(&range);
	tlb_start_vma(tlb, vma);
	walk_ops.walk_lock = get_walk_lock(madv_behavior->lock_mode);
	walk_page_range_vma(vma, range.start, range.end,
			&walk_ops, tlb);
	tlb_end_vma(tlb, vma);
	mmu_notifier_invalidate_range_end(&range);
	return 0;
}

/*
 * Application no longer needs these pages.  If the pages are dirty,
 * it's OK to just throw them away.  The app will be more careful about
 * data it wants to keep.  Be sure to free swap resources too.  The
 * zap_page_range_single call sets things up for shrink_active_list to actually
 * free these pages later if no one else has touched them in the meantime,
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
static long madvise_dontneed_single_vma(struct madvise_behavior *madv_behavior)

{
	struct madvise_behavior_range *range = &madv_behavior->range;
	struct zap_details details = {
		.reclaim_pt = true,
		.even_cows = true,
	};

	zap_page_range_single_batched(
			madv_behavior->tlb, madv_behavior->vma, range->start,
			range->end - range->start, &details);
	return 0;
}

static
bool madvise_dontneed_free_valid_vma(struct madvise_behavior *madv_behavior)
{
	struct vm_area_struct *vma = madv_behavior->vma;
	int behavior = madv_behavior->behavior;
	struct madvise_behavior_range *range = &madv_behavior->range;

	if (!is_vm_hugetlb_page(vma)) {
		unsigned int forbidden = VM_PFNMAP;

		if (behavior != MADV_DONTNEED_LOCKED)
			forbidden |= VM_LOCKED;

		return !(vma->vm_flags & forbidden);
	}

	if (behavior != MADV_DONTNEED && behavior != MADV_DONTNEED_LOCKED)
		return false;
	if (range->start & ~huge_page_mask(hstate_vma(vma)))
		return false;

	/*
	 * Madvise callers expect the length to be rounded up to PAGE_SIZE
	 * boundaries, and may be unaware that this VMA uses huge pages.
	 * Avoid unexpected data loss by rounding down the number of
	 * huge pages freed.
	 */
	range->end = ALIGN_DOWN(range->end, huge_page_size(hstate_vma(vma)));

	return true;
}

static long madvise_dontneed_free(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;
	struct madvise_behavior_range *range = &madv_behavior->range;
	int behavior = madv_behavior->behavior;

	if (!madvise_dontneed_free_valid_vma(madv_behavior))
		return -EINVAL;

	if (range->start == range->end)
		return 0;

	if (!userfaultfd_remove(madv_behavior->vma, range->start, range->end)) {
		struct vm_area_struct *vma;

		mark_mmap_lock_dropped(madv_behavior);
		mmap_read_lock(mm);
		madv_behavior->vma = vma = vma_lookup(mm, range->start);
		if (!vma)
			return -ENOMEM;
		/*
		 * Potential end adjustment for hugetlb vma is OK as
		 * the check below keeps end within vma.
		 */
		if (!madvise_dontneed_free_valid_vma(madv_behavior))
			return -EINVAL;
		if (range->end > vma->vm_end) {
			/*
			 * Don't fail if end > vma->vm_end. If the old
			 * vma was split while the mmap_lock was
			 * released the effect of the concurrent
			 * operation may not cause madvise() to
			 * have an undefined result. There may be an
			 * adjacent next vma that we'll walk
			 * next. userfaultfd_remove() will generate an
			 * UFFD_EVENT_REMOVE repetition on the
			 * end-vma->vm_end range, but the manager can
			 * handle a repetition fine.
			 */
			range->end = vma->vm_end;
		}
		/*
		 * If the memory region between start and end was
		 * originally backed by 4kB pages and then remapped to
		 * be backed by hugepages while mmap_lock was dropped,
		 * the adjustment for hugetlb vma above may have rounded
		 * end down to the start address.
		 */
		if (range->start == range->end)
			return 0;
		VM_WARN_ON(range->start > range->end);
	}

	if (behavior == MADV_DONTNEED || behavior == MADV_DONTNEED_LOCKED)
		return madvise_dontneed_single_vma(madv_behavior);
	else if (behavior == MADV_FREE)
		return madvise_free_single_vma(madv_behavior);
	else
		return -EINVAL;
}

static long madvise_populate(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;
	const bool write = madv_behavior->behavior == MADV_POPULATE_WRITE;
	int locked = 1;
	unsigned long start = madv_behavior->range.start;
	unsigned long end = madv_behavior->range.end;
	long pages;

	while (start < end) {
		/* Populate (prefault) page tables readable/writable. */
		pages = faultin_page_range(mm, start, end, write, &locked);
		if (!locked) {
			mmap_read_lock(mm);
			locked = 1;
		}
		if (pages < 0) {
			switch (pages) {
			case -EINTR:
				return -EINTR;
			case -EINVAL: /* Incompatible mappings / permissions. */
				return -EINVAL;
			case -EHWPOISON:
				return -EHWPOISON;
			case -EFAULT: /* VM_FAULT_SIGBUS or VM_FAULT_SIGSEGV */
				return -EFAULT;
			default:
				pr_warn_once("%s: unhandled return value: %ld\n",
					     __func__, pages);
				fallthrough;
			case -ENOMEM: /* No VMA or out of memory. */
				return -ENOMEM;
			}
		}
		start += pages * PAGE_SIZE;
	}
	return 0;
}

/*
 * Application wants to free up the pages and associated backing store.
 * This is effectively punching a hole into the middle of a file.
 */
static long madvise_remove(struct madvise_behavior *madv_behavior)
{
	loff_t offset;
	int error;
	struct file *f;
	struct mm_struct *mm = madv_behavior->mm;
	struct vm_area_struct *vma = madv_behavior->vma;
	unsigned long start = madv_behavior->range.start;
	unsigned long end = madv_behavior->range.end;

	mark_mmap_lock_dropped(madv_behavior);

	if (vma->vm_flags & VM_LOCKED)
		return -EINVAL;

	f = vma->vm_file;

	if (!f || !f->f_mapping || !f->f_mapping->host) {
			return -EINVAL;
	}

	if (!vma_is_shared_maywrite(vma))
		return -EACCES;

	offset = (loff_t)(start - vma->vm_start)
			+ ((loff_t)vma->vm_pgoff << PAGE_SHIFT);

	/*
	 * Filesystem's fallocate may need to take i_rwsem.  We need to
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

static bool is_valid_guard_vma(struct vm_area_struct *vma, bool allow_locked)
{
	vm_flags_t disallowed = VM_SPECIAL | VM_HUGETLB;

	/*
	 * A user could lock after setting a guard range but that's fine, as
	 * they'd not be able to fault in. The issue arises when we try to zap
	 * existing locked VMAs. We don't want to do that.
	 */
	if (!allow_locked)
		disallowed |= VM_LOCKED;

	return !(vma->vm_flags & disallowed);
}

static bool is_guard_pte_marker(pte_t ptent)
{
	return is_pte_marker(ptent) &&
		is_guard_swp_entry(pte_to_swp_entry(ptent));
}

static int guard_install_pud_entry(pud_t *pud, unsigned long addr,
				   unsigned long next, struct mm_walk *walk)
{
	pud_t pudval = pudp_get(pud);

	/* If huge return >0 so we abort the operation + zap. */
	return pud_trans_huge(pudval);
}

static int guard_install_pmd_entry(pmd_t *pmd, unsigned long addr,
				   unsigned long next, struct mm_walk *walk)
{
	pmd_t pmdval = pmdp_get(pmd);

	/* If huge return >0 so we abort the operation + zap. */
	return pmd_trans_huge(pmdval);
}

static int guard_install_pte_entry(pte_t *pte, unsigned long addr,
				   unsigned long next, struct mm_walk *walk)
{
	pte_t pteval = ptep_get(pte);
	unsigned long *nr_pages = (unsigned long *)walk->private;

	/* If there is already a guard page marker, we have nothing to do. */
	if (is_guard_pte_marker(pteval)) {
		(*nr_pages)++;

		return 0;
	}

	/* If populated return >0 so we abort the operation + zap. */
	return 1;
}

static int guard_install_set_pte(unsigned long addr, unsigned long next,
				 pte_t *ptep, struct mm_walk *walk)
{
	unsigned long *nr_pages = (unsigned long *)walk->private;

	/* Simply install a PTE marker, this causes segfault on access. */
	*ptep = make_pte_marker(PTE_MARKER_GUARD);
	(*nr_pages)++;

	return 0;
}

static const struct mm_walk_ops guard_install_walk_ops = {
	.pud_entry		= guard_install_pud_entry,
	.pmd_entry		= guard_install_pmd_entry,
	.pte_entry		= guard_install_pte_entry,
	.install_pte		= guard_install_set_pte,
	.walk_lock		= PGWALK_RDLOCK,
};

static long madvise_guard_install(struct madvise_behavior *madv_behavior)
{
	struct vm_area_struct *vma = madv_behavior->vma;
	struct madvise_behavior_range *range = &madv_behavior->range;
	long err;
	int i;

	if (!is_valid_guard_vma(vma, /* allow_locked = */false))
		return -EINVAL;

	/*
	 * If we install guard markers, then the range is no longer
	 * empty from a page table perspective and therefore it's
	 * appropriate to have an anon_vma.
	 *
	 * This ensures that on fork, we copy page tables correctly.
	 */
	err = anon_vma_prepare(vma);
	if (err)
		return err;

	/*
	 * Optimistically try to install the guard marker pages first. If any
	 * non-guard pages are encountered, give up and zap the range before
	 * trying again.
	 *
	 * We try a few times before giving up and releasing back to userland to
	 * loop around, releasing locks in the process to avoid contention. This
	 * would only happen if there was a great many racing page faults.
	 *
	 * In most cases we should simply install the guard markers immediately
	 * with no zap or looping.
	 */
	for (i = 0; i < MAX_MADVISE_GUARD_RETRIES; i++) {
		unsigned long nr_pages = 0;

		/* Returns < 0 on error, == 0 if success, > 0 if zap needed. */
		err = walk_page_range_mm(vma->vm_mm, range->start, range->end,
					 &guard_install_walk_ops, &nr_pages);
		if (err < 0)
			return err;

		if (err == 0) {
			unsigned long nr_expected_pages =
				PHYS_PFN(range->end - range->start);

			VM_WARN_ON(nr_pages != nr_expected_pages);
			return 0;
		}

		/*
		 * OK some of the range have non-guard pages mapped, zap
		 * them. This leaves existing guard pages in place.
		 */
		zap_page_range_single(vma, range->start,
				range->end - range->start, NULL);
	}

	/*
	 * We were unable to install the guard pages due to being raced by page
	 * faults. This should not happen ordinarily. We return to userspace and
	 * immediately retry, relieving lock contention.
	 */
	return restart_syscall();
}

static int guard_remove_pud_entry(pud_t *pud, unsigned long addr,
				  unsigned long next, struct mm_walk *walk)
{
	pud_t pudval = pudp_get(pud);

	/* If huge, cannot have guard pages present, so no-op - skip. */
	if (pud_trans_huge(pudval))
		walk->action = ACTION_CONTINUE;

	return 0;
}

static int guard_remove_pmd_entry(pmd_t *pmd, unsigned long addr,
				  unsigned long next, struct mm_walk *walk)
{
	pmd_t pmdval = pmdp_get(pmd);

	/* If huge, cannot have guard pages present, so no-op - skip. */
	if (pmd_trans_huge(pmdval))
		walk->action = ACTION_CONTINUE;

	return 0;
}

static int guard_remove_pte_entry(pte_t *pte, unsigned long addr,
				  unsigned long next, struct mm_walk *walk)
{
	pte_t ptent = ptep_get(pte);

	if (is_guard_pte_marker(ptent)) {
		/* Simply clear the PTE marker. */
		pte_clear_not_present_full(walk->mm, addr, pte, false);
		update_mmu_cache(walk->vma, addr, pte);
	}

	return 0;
}

static const struct mm_walk_ops guard_remove_walk_ops = {
	.pud_entry		= guard_remove_pud_entry,
	.pmd_entry		= guard_remove_pmd_entry,
	.pte_entry		= guard_remove_pte_entry,
	.walk_lock		= PGWALK_RDLOCK,
};

static long madvise_guard_remove(struct madvise_behavior *madv_behavior)
{
	struct vm_area_struct *vma = madv_behavior->vma;
	struct madvise_behavior_range *range = &madv_behavior->range;

	/*
	 * We're ok with removing guards in mlock()'d ranges, as this is a
	 * non-destructive action.
	 */
	if (!is_valid_guard_vma(vma, /* allow_locked = */true))
		return -EINVAL;

	return walk_page_range_vma(vma, range->start, range->end,
			       &guard_remove_walk_ops, NULL);
}

/*
 * Apply an madvise behavior to a region of a vma.  madvise_update_vma
 * will handle splitting a vm area into separate areas, each area with its own
 * behavior.
 */
static int madvise_vma_behavior(struct madvise_behavior *madv_behavior)
{
	int behavior = madv_behavior->behavior;
	struct vm_area_struct *vma = madv_behavior->vma;
	vm_flags_t new_flags = vma->vm_flags;
	struct madvise_behavior_range *range = &madv_behavior->range;
	int error;

	if (unlikely(!can_modify_vma_madv(madv_behavior->vma, behavior)))
		return -EPERM;

	switch (behavior) {
	case MADV_REMOVE:
		return madvise_remove(madv_behavior);
	case MADV_WILLNEED:
		return madvise_willneed(madv_behavior);
	case MADV_COLD:
		return madvise_cold(madv_behavior);
	case MADV_PAGEOUT:
		return madvise_pageout(madv_behavior);
	case MADV_FREE:
	case MADV_DONTNEED:
	case MADV_DONTNEED_LOCKED:
		return madvise_dontneed_free(madv_behavior);
	case MADV_COLLAPSE:
		return madvise_collapse(vma, range->start, range->end,
			&madv_behavior->lock_dropped);
	case MADV_GUARD_INSTALL:
		return madvise_guard_install(madv_behavior);
	case MADV_GUARD_REMOVE:
		return madvise_guard_remove(madv_behavior);

	/* The below behaviours update VMAs via madvise_update_vma(). */

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
		if (new_flags & VM_IO)
			return -EINVAL;
		new_flags &= ~VM_DONTCOPY;
		break;
	case MADV_WIPEONFORK:
		/* MADV_WIPEONFORK is only supported on anonymous memory. */
		if (vma->vm_file || new_flags & VM_SHARED)
			return -EINVAL;
		new_flags |= VM_WIPEONFORK;
		break;
	case MADV_KEEPONFORK:
		if (new_flags & VM_DROPPABLE)
			return -EINVAL;
		new_flags &= ~VM_WIPEONFORK;
		break;
	case MADV_DONTDUMP:
		new_flags |= VM_DONTDUMP;
		break;
	case MADV_DODUMP:
		if ((!is_vm_hugetlb_page(vma) && (new_flags & VM_SPECIAL)) ||
		    (new_flags & VM_DROPPABLE))
			return -EINVAL;
		new_flags &= ~VM_DONTDUMP;
		break;
	case MADV_MERGEABLE:
	case MADV_UNMERGEABLE:
		error = ksm_madvise(vma, range->start, range->end,
				behavior, &new_flags);
		if (error)
			goto out;
		break;
	case MADV_HUGEPAGE:
	case MADV_NOHUGEPAGE:
		error = hugepage_madvise(vma, &new_flags, behavior);
		if (error)
			goto out;
		break;
	case __MADV_SET_ANON_VMA_NAME:
		/* Only anonymous mappings can be named */
		if (vma->vm_file && !vma_is_anon_shmem(vma))
			return -EBADF;
		break;
	}

	/* This is a write operation.*/
	VM_WARN_ON_ONCE(madv_behavior->lock_mode != MADVISE_MMAP_WRITE_LOCK);

	error = madvise_update_vma(new_flags, madv_behavior);
out:
	/*
	 * madvise() returns EAGAIN if kernel resources, such as
	 * slab, are temporarily unavailable.
	 */
	if (error == -ENOMEM)
		error = -EAGAIN;
	return error;
}

#ifdef CONFIG_MEMORY_FAILURE
/*
 * Error injection support for memory error handling.
 */
static int madvise_inject_error(struct madvise_behavior *madv_behavior)
{
	unsigned long size;
	unsigned long start = madv_behavior->range.start;
	unsigned long end = madv_behavior->range.end;

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

		if (madv_behavior->behavior == MADV_SOFT_OFFLINE) {
			pr_info("Soft offlining pfn %#lx at process virtual address %#lx\n",
				 pfn, start);
			ret = soft_offline_page(pfn, MF_COUNT_INCREASED);
		} else {
			pr_info("Injecting memory failure for pfn %#lx at process virtual address %#lx\n",
				 pfn, start);
			ret = memory_failure(pfn, MF_ACTION_REQUIRED | MF_COUNT_INCREASED | MF_SW_SIMULATED);
			if (ret == -EOPNOTSUPP)
				ret = 0;
		}

		if (ret)
			return ret;
	}

	return 0;
}

static bool is_memory_failure(struct madvise_behavior *madv_behavior)
{
	switch (madv_behavior->behavior) {
	case MADV_HWPOISON:
	case MADV_SOFT_OFFLINE:
		return true;
	default:
		return false;
	}
}

#else

static int madvise_inject_error(struct madvise_behavior *madv_behavior)
{
	return 0;
}

static bool is_memory_failure(struct madvise_behavior *madv_behavior)
{
	return false;
}

#endif	/* CONFIG_MEMORY_FAILURE */

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
	case MADV_DONTNEED_LOCKED:
	case MADV_FREE:
	case MADV_COLD:
	case MADV_PAGEOUT:
	case MADV_POPULATE_READ:
	case MADV_POPULATE_WRITE:
#ifdef CONFIG_KSM
	case MADV_MERGEABLE:
	case MADV_UNMERGEABLE:
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	case MADV_HUGEPAGE:
	case MADV_NOHUGEPAGE:
	case MADV_COLLAPSE:
#endif
	case MADV_DONTDUMP:
	case MADV_DODUMP:
	case MADV_WIPEONFORK:
	case MADV_KEEPONFORK:
	case MADV_GUARD_INSTALL:
	case MADV_GUARD_REMOVE:
#ifdef CONFIG_MEMORY_FAILURE
	case MADV_SOFT_OFFLINE:
	case MADV_HWPOISON:
#endif
		return true;

	default:
		return false;
	}
}

/* Can we invoke process_madvise() on a remote mm for the specified behavior? */
static bool process_madvise_remote_valid(int behavior)
{
	switch (behavior) {
	case MADV_COLD:
	case MADV_PAGEOUT:
	case MADV_WILLNEED:
	case MADV_COLLAPSE:
		return true;
	default:
		return false;
	}
}

/*
 * Try to acquire a VMA read lock if possible.
 *
 * We only support this lock over a single VMA, which the input range must
 * span either partially or fully.
 *
 * This function always returns with an appropriate lock held. If a VMA read
 * lock could be acquired, we return true and set madv_behavior state
 * accordingly.
 *
 * If a VMA read lock could not be acquired, we return false and expect caller to
 * fallback to mmap lock behaviour.
 */
static bool try_vma_read_lock(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;
	struct vm_area_struct *vma;

	vma = lock_vma_under_rcu(mm, madv_behavior->range.start);
	if (!vma)
		goto take_mmap_read_lock;
	/*
	 * Must span only a single VMA; uffd and remote processes are
	 * unsupported.
	 */
	if (madv_behavior->range.end > vma->vm_end || current->mm != mm ||
	    userfaultfd_armed(vma)) {
		vma_end_read(vma);
		goto take_mmap_read_lock;
	}
	madv_behavior->vma = vma;
	return true;

take_mmap_read_lock:
	mmap_read_lock(mm);
	madv_behavior->lock_mode = MADVISE_MMAP_READ_LOCK;
	return false;
}

/*
 * Walk the vmas in range [start,end), and call the madvise_vma_behavior
 * function on each one.  The function will get start and end parameters that
 * cover the overlap between the current vma and the original range.  Any
 * unmapped regions in the original range will result in this function returning
 * -ENOMEM while still calling the madvise_vma_behavior function on all of the
 * existing vmas in the range.  Must be called with the mmap_lock held for
 * reading or writing.
 */
static
int madvise_walk_vmas(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;
	struct madvise_behavior_range *range = &madv_behavior->range;
	/* range is updated to span each VMA, so store end of entire range. */
	unsigned long last_end = range->end;
	int unmapped_error = 0;
	int error;
	struct vm_area_struct *prev, *vma;

	/*
	 * If VMA read lock is supported, apply madvise to a single VMA
	 * tentatively, avoiding walking VMAs.
	 */
	if (madv_behavior->lock_mode == MADVISE_VMA_READ_LOCK &&
	    try_vma_read_lock(madv_behavior)) {
		error = madvise_vma_behavior(madv_behavior);
		vma_end_read(madv_behavior->vma);
		return error;
	}

	vma = find_vma_prev(mm, range->start, &prev);
	if (vma && range->start > vma->vm_start)
		prev = vma;

	for (;;) {
		/* Still start < end. */
		if (!vma)
			return -ENOMEM;

		/* Here start < (last_end|vma->vm_end). */
		if (range->start < vma->vm_start) {
			/*
			 * This indicates a gap between VMAs in the input
			 * range. This does not cause the operation to abort,
			 * rather we simply return -ENOMEM to indicate that this
			 * has happened, but carry on.
			 */
			unmapped_error = -ENOMEM;
			range->start = vma->vm_start;
			if (range->start >= last_end)
				break;
		}

		/* Here vma->vm_start <= range->start < (last_end|vma->vm_end) */
		range->end = min(vma->vm_end, last_end);

		/* Here vma->vm_start <= range->start < range->end <= (last_end|vma->vm_end). */
		madv_behavior->prev = prev;
		madv_behavior->vma = vma;
		error = madvise_vma_behavior(madv_behavior);
		if (error)
			return error;
		if (madv_behavior->lock_dropped) {
			/* We dropped the mmap lock, we can't ref the VMA. */
			prev = NULL;
			vma = NULL;
			madv_behavior->lock_dropped = false;
		} else {
			vma = madv_behavior->vma;
			prev = vma;
		}

		if (vma && range->end < vma->vm_end)
			range->end = vma->vm_end;
		if (range->end >= last_end)
			break;

		vma = find_vma(mm, vma ? vma->vm_end : range->end);
		range->start = range->end;
	}

	return unmapped_error;
}

/*
 * Any behaviour which results in changes to the vma->vm_flags needs to
 * take mmap_lock for writing. Others, which simply traverse vmas, need
 * to only take it for reading.
 */
static enum madvise_lock_mode get_lock_mode(struct madvise_behavior *madv_behavior)
{
	if (is_memory_failure(madv_behavior))
		return MADVISE_NO_LOCK;

	switch (madv_behavior->behavior) {
	case MADV_REMOVE:
	case MADV_WILLNEED:
	case MADV_COLD:
	case MADV_PAGEOUT:
	case MADV_POPULATE_READ:
	case MADV_POPULATE_WRITE:
	case MADV_COLLAPSE:
	case MADV_GUARD_INSTALL:
	case MADV_GUARD_REMOVE:
		return MADVISE_MMAP_READ_LOCK;
	case MADV_DONTNEED:
	case MADV_DONTNEED_LOCKED:
	case MADV_FREE:
		return MADVISE_VMA_READ_LOCK;
	default:
		return MADVISE_MMAP_WRITE_LOCK;
	}
}

static int madvise_lock(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;
	enum madvise_lock_mode lock_mode = get_lock_mode(madv_behavior);

	switch (lock_mode) {
	case MADVISE_NO_LOCK:
		break;
	case MADVISE_MMAP_WRITE_LOCK:
		if (mmap_write_lock_killable(mm))
			return -EINTR;
		break;
	case MADVISE_MMAP_READ_LOCK:
		mmap_read_lock(mm);
		break;
	case MADVISE_VMA_READ_LOCK:
		/* We will acquire the lock per-VMA in madvise_walk_vmas(). */
		break;
	}

	madv_behavior->lock_mode = lock_mode;
	return 0;
}

static void madvise_unlock(struct madvise_behavior *madv_behavior)
{
	struct mm_struct *mm = madv_behavior->mm;

	switch (madv_behavior->lock_mode) {
	case  MADVISE_NO_LOCK:
		return;
	case MADVISE_MMAP_WRITE_LOCK:
		mmap_write_unlock(mm);
		break;
	case MADVISE_MMAP_READ_LOCK:
		mmap_read_unlock(mm);
		break;
	case MADVISE_VMA_READ_LOCK:
		/* We will drop the lock per-VMA in madvise_walk_vmas(). */
		break;
	}

	madv_behavior->lock_mode = MADVISE_NO_LOCK;
}

static bool madvise_batch_tlb_flush(int behavior)
{
	switch (behavior) {
	case MADV_DONTNEED:
	case MADV_DONTNEED_LOCKED:
	case MADV_FREE:
		return true;
	default:
		return false;
	}
}

static void madvise_init_tlb(struct madvise_behavior *madv_behavior)
{
	if (madvise_batch_tlb_flush(madv_behavior->behavior))
		tlb_gather_mmu(madv_behavior->tlb, madv_behavior->mm);
}

static void madvise_finish_tlb(struct madvise_behavior *madv_behavior)
{
	if (madvise_batch_tlb_flush(madv_behavior->behavior))
		tlb_finish_mmu(madv_behavior->tlb);
}

static bool is_valid_madvise(unsigned long start, size_t len_in, int behavior)
{
	size_t len;

	if (!madvise_behavior_valid(behavior))
		return false;

	if (!PAGE_ALIGNED(start))
		return false;
	len = PAGE_ALIGN(len_in);

	/* Check to see whether len was rounded up from small -ve to zero */
	if (len_in && !len)
		return false;

	if (start + len < start)
		return false;

	return true;
}

/*
 * madvise_should_skip() - Return if the request is invalid or nothing.
 * @start:	Start address of madvise-requested address range.
 * @len_in:	Length of madvise-requested address range.
 * @behavior:	Requested madvise behavor.
 * @err:	Pointer to store an error code from the check.
 *
 * If the specified behaviour is invalid or nothing would occur, we skip the
 * operation.  This function returns true in the cases, otherwise false.  In
 * the former case we store an error on @err.
 */
static bool madvise_should_skip(unsigned long start, size_t len_in,
		int behavior, int *err)
{
	if (!is_valid_madvise(start, len_in, behavior)) {
		*err = -EINVAL;
		return true;
	}
	if (start + PAGE_ALIGN(len_in) == start) {
		*err = 0;
		return true;
	}
	return false;
}

static bool is_madvise_populate(struct madvise_behavior *madv_behavior)
{
	switch (madv_behavior->behavior) {
	case MADV_POPULATE_READ:
	case MADV_POPULATE_WRITE:
		return true;
	default:
		return false;
	}
}

/*
 * untagged_addr_remote() assumes mmap_lock is already held. On
 * architectures like x86 and RISC-V, tagging is tricky because each
 * mm may have a different tagging mask. However, we might only hold
 * the per-VMA lock (currently only local processes are supported),
 * so untagged_addr is used to avoid the mmap_lock assertion for
 * local processes.
 */
static inline unsigned long get_untagged_addr(struct mm_struct *mm,
		unsigned long start)
{
	return current->mm == mm ? untagged_addr(start) :
				   untagged_addr_remote(mm, start);
}

static int madvise_do_behavior(unsigned long start, size_t len_in,
		struct madvise_behavior *madv_behavior)
{
	struct blk_plug plug;
	int error;
	struct madvise_behavior_range *range = &madv_behavior->range;

	if (is_memory_failure(madv_behavior)) {
		range->start = start;
		range->end = start + len_in;
		return madvise_inject_error(madv_behavior);
	}

	range->start = get_untagged_addr(madv_behavior->mm, start);
	range->end = range->start + PAGE_ALIGN(len_in);

	blk_start_plug(&plug);
	if (is_madvise_populate(madv_behavior))
		error = madvise_populate(madv_behavior);
	else
		error = madvise_walk_vmas(madv_behavior);
	blk_finish_plug(&plug);
	return error;
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
 *  MADV_COLLAPSE - synchronously coalesce pages into new THP.
 *  MADV_DONTDUMP - the application wants to prevent pages in the given range
 *		from being included in its core dump.
 *  MADV_DODUMP - cancel MADV_DONTDUMP: no longer exclude from core dump.
 *  MADV_COLD - the application is not expected to use this memory soon,
 *		deactivate pages in this range so that they can be reclaimed
 *		easily if memory pressure happens.
 *  MADV_PAGEOUT - the application is not expected to use this memory soon,
 *		page out the pages in this range immediately.
 *  MADV_POPULATE_READ - populate (prefault) page tables readable by
 *		triggering read faults if required
 *  MADV_POPULATE_WRITE - populate (prefault) page tables writable by
 *		triggering write faults if required
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
 *  -EPERM  - memory is sealed.
 */
int do_madvise(struct mm_struct *mm, unsigned long start, size_t len_in, int behavior)
{
	int error;
	struct mmu_gather tlb;
	struct madvise_behavior madv_behavior = {
		.mm = mm,
		.behavior = behavior,
		.tlb = &tlb,
	};

	if (madvise_should_skip(start, len_in, behavior, &error))
		return error;
	error = madvise_lock(&madv_behavior);
	if (error)
		return error;
	madvise_init_tlb(&madv_behavior);
	error = madvise_do_behavior(start, len_in, &madv_behavior);
	madvise_finish_tlb(&madv_behavior);
	madvise_unlock(&madv_behavior);

	return error;
}

SYSCALL_DEFINE3(madvise, unsigned long, start, size_t, len_in, int, behavior)
{
	return do_madvise(current->mm, start, len_in, behavior);
}

/* Perform an madvise operation over a vector of addresses and lengths. */
static ssize_t vector_madvise(struct mm_struct *mm, struct iov_iter *iter,
			      int behavior)
{
	ssize_t ret = 0;
	size_t total_len;
	struct mmu_gather tlb;
	struct madvise_behavior madv_behavior = {
		.mm = mm,
		.behavior = behavior,
		.tlb = &tlb,
	};

	total_len = iov_iter_count(iter);

	ret = madvise_lock(&madv_behavior);
	if (ret)
		return ret;
	madvise_init_tlb(&madv_behavior);

	while (iov_iter_count(iter)) {
		unsigned long start = (unsigned long)iter_iov_addr(iter);
		size_t len_in = iter_iov_len(iter);
		int error;

		if (madvise_should_skip(start, len_in, behavior, &error))
			ret = error;
		else
			ret = madvise_do_behavior(start, len_in, &madv_behavior);
		/*
		 * An madvise operation is attempting to restart the syscall,
		 * but we cannot proceed as it would not be correct to repeat
		 * the operation in aggregate, and would be surprising to the
		 * user.
		 *
		 * We drop and reacquire locks so it is safe to just loop and
		 * try again. We check for fatal signals in case we need exit
		 * early anyway.
		 */
		if (ret == -ERESTARTNOINTR) {
			if (fatal_signal_pending(current)) {
				ret = -EINTR;
				break;
			}

			/* Drop and reacquire lock to unwind race. */
			madvise_finish_tlb(&madv_behavior);
			madvise_unlock(&madv_behavior);
			ret = madvise_lock(&madv_behavior);
			if (ret)
				goto out;
			madvise_init_tlb(&madv_behavior);
			continue;
		}
		if (ret < 0)
			break;
		iov_iter_advance(iter, iter_iov_len(iter));
	}
	madvise_finish_tlb(&madv_behavior);
	madvise_unlock(&madv_behavior);

out:
	ret = (total_len - iov_iter_count(iter)) ? : ret;

	return ret;
}

SYSCALL_DEFINE5(process_madvise, int, pidfd, const struct iovec __user *, vec,
		size_t, vlen, int, behavior, unsigned int, flags)
{
	ssize_t ret;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	struct task_struct *task;
	struct mm_struct *mm;
	unsigned int f_flags;

	if (flags != 0) {
		ret = -EINVAL;
		goto out;
	}

	ret = import_iovec(ITER_DEST, vec, vlen, ARRAY_SIZE(iovstack), &iov, &iter);
	if (ret < 0)
		goto out;

	task = pidfd_get_task(pidfd, &f_flags);
	if (IS_ERR(task)) {
		ret = PTR_ERR(task);
		goto free_iov;
	}

	/* Require PTRACE_MODE_READ to avoid leaking ASLR metadata. */
	mm = mm_access(task, PTRACE_MODE_READ_FSCREDS);
	if (IS_ERR(mm)) {
		ret = PTR_ERR(mm);
		goto release_task;
	}

	/*
	 * We need only perform this check if we are attempting to manipulate a
	 * remote process's address space.
	 */
	if (mm != current->mm && !process_madvise_remote_valid(behavior)) {
		ret = -EINVAL;
		goto release_mm;
	}

	/*
	 * Require CAP_SYS_NICE for influencing process performance. Note that
	 * only non-destructive hints are currently supported for remote
	 * processes.
	 */
	if (mm != current->mm && !capable(CAP_SYS_NICE)) {
		ret = -EPERM;
		goto release_mm;
	}

	ret = vector_madvise(mm, &iter, behavior);

release_mm:
	mmput(mm);
release_task:
	put_task_struct(task);
free_iov:
	kfree(iov);
out:
	return ret;
}

#ifdef CONFIG_ANON_VMA_NAME

#define ANON_VMA_NAME_MAX_LEN		80
#define ANON_VMA_NAME_INVALID_CHARS	"\\`$[]"

static inline bool is_valid_name_char(char ch)
{
	/* printable ascii characters, excluding ANON_VMA_NAME_INVALID_CHARS */
	return ch > 0x1f && ch < 0x7f &&
		!strchr(ANON_VMA_NAME_INVALID_CHARS, ch);
}

static int madvise_set_anon_name(struct mm_struct *mm, unsigned long start,
		unsigned long len_in, struct anon_vma_name *anon_name)
{
	unsigned long end;
	unsigned long len;
	int error;
	struct madvise_behavior madv_behavior = {
		.mm = mm,
		.behavior = __MADV_SET_ANON_VMA_NAME,
		.anon_name = anon_name,
	};

	if (start & ~PAGE_MASK)
		return -EINVAL;
	len = (len_in + ~PAGE_MASK) & PAGE_MASK;

	/* Check to see whether len was rounded up from small -ve to zero */
	if (len_in && !len)
		return -EINVAL;

	end = start + len;
	if (end < start)
		return -EINVAL;

	if (end == start)
		return 0;

	madv_behavior.range.start = start;
	madv_behavior.range.end = end;

	error = madvise_lock(&madv_behavior);
	if (error)
		return error;
	error = madvise_walk_vmas(&madv_behavior);
	madvise_unlock(&madv_behavior);

	return error;
}

int set_anon_vma_name(unsigned long addr, unsigned long size,
		      const char __user *uname)
{
	struct anon_vma_name *anon_name = NULL;
	struct mm_struct *mm = current->mm;
	int error;

	if (uname) {
		char *name, *pch;

		name = strndup_user(uname, ANON_VMA_NAME_MAX_LEN);
		if (IS_ERR(name))
			return PTR_ERR(name);

		for (pch = name; *pch != '\0'; pch++) {
			if (!is_valid_name_char(*pch)) {
				kfree(name);
				return -EINVAL;
			}
		}
		/* anon_vma has its own copy */
		anon_name = anon_vma_name_alloc(name);
		kfree(name);
		if (!anon_name)
			return -ENOMEM;
	}

	error = madvise_set_anon_name(mm, addr, size, anon_name);
	anon_vma_name_put(anon_name);

	return error;
}
#endif
