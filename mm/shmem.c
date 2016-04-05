/*
 * Resizable virtual memory filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *		 2000 Transmeta Corp.
 *		 2000-2001 Christoph Rohland
 *		 2000-2001 SAP AG
 *		 2002 Red Hat Inc.
 * Copyright (C) 2002-2011 Hugh Dickins.
 * Copyright (C) 2011 Google Inc.
 * Copyright (C) 2002-2005 VERITAS Software Corporation.
 * Copyright (C) 2004 Andi Kleen, SuSE Labs
 *
 * Extended attribute support for tmpfs:
 * Copyright (c) 2004, Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 * Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * tiny-shmem:
 * Copyright (c) 2004, 2008 Matt Mackall <mpm@selenic.com>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/uio.h>

static struct vfsmount *shm_mnt;

#ifdef CONFIG_SHMEM
/*
 * This virtual memory filesystem is heavily based on the ramfs. It
 * extends ramfs by the ability to use swap and honor resource limits
 * which makes it a completely usable filesystem.
 */

#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/shmem_fs.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/pagevec.h>
#include <linux/percpu_counter.h>
#include <linux/falloc.h>
#include <linux/splice.h>
#include <linux/security.h>
#include <linux/swapops.h>
#include <linux/mempolicy.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/migrate.h>
#include <linux/highmem.h>
#include <linux/seq_file.h>
#include <linux/magic.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <uapi/linux/memfd.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include "internal.h"

#define BLOCKS_PER_PAGE  (PAGE_CACHE_SIZE/512)
#define VM_ACCT(size)    (PAGE_CACHE_ALIGN(size) >> PAGE_SHIFT)

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

/* Symlink up to this size is kmalloc'ed instead of using a swappable page */
#define SHORT_SYMLINK_LEN 128

/*
 * shmem_fallocate communicates with shmem_fault or shmem_writepage via
 * inode->i_private (with i_mutex making sure that it has only one user at
 * a time): we would prefer not to enlarge the shmem inode just for that.
 */
struct shmem_falloc {
	wait_queue_head_t *waitq; /* faults into hole wait for punch to end */
	pgoff_t start;		/* start of range currently being fallocated */
	pgoff_t next;		/* the next page offset to be fallocated */
	pgoff_t nr_falloced;	/* how many new pages have been fallocated */
	pgoff_t nr_unswapped;	/* how often writepage refused to swap out */
};

/* Flag allocation requirements to shmem_getpage */
enum sgp_type {
	SGP_READ,	/* don't exceed i_size, don't allocate page */
	SGP_CACHE,	/* don't exceed i_size, may allocate page */
	SGP_DIRTY,	/* like SGP_CACHE, but set new page dirty */
	SGP_WRITE,	/* may exceed i_size, may allocate !Uptodate page */
	SGP_FALLOC,	/* like SGP_WRITE, but make existing page Uptodate */
};

#ifdef CONFIG_TMPFS
static unsigned long shmem_default_max_blocks(void)
{
	return totalram_pages / 2;
}

static unsigned long shmem_default_max_inodes(void)
{
	return min(totalram_pages - totalhigh_pages, totalram_pages / 2);
}
#endif

static bool shmem_should_replace_page(struct page *page, gfp_t gfp);
static int shmem_replace_page(struct page **pagep, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index);
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, gfp_t gfp, int *fault_type);

static inline int shmem_getpage(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, int *fault_type)
{
	return shmem_getpage_gfp(inode, index, pagep, sgp,
			mapping_gfp_mask(inode->i_mapping), fault_type);
}

static inline struct shmem_sb_info *SHMEM_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 * shmem_file_setup pre-accounts the whole fixed size of a VM object,
 * for shared memory and for shared anonymous (/dev/zero) mappings
 * (unless MAP_NORESERVE and sysctl_overcommit_memory <= 1),
 * consistent with the pre-accounting of private mappings ...
 */
static inline int shmem_acct_size(unsigned long flags, loff_t size)
{
	return (flags & VM_NORESERVE) ?
		0 : security_vm_enough_memory_mm(current->mm, VM_ACCT(size));
}

static inline void shmem_unacct_size(unsigned long flags, loff_t size)
{
	if (!(flags & VM_NORESERVE))
		vm_unacct_memory(VM_ACCT(size));
}

static inline int shmem_reacct_size(unsigned long flags,
		loff_t oldsize, loff_t newsize)
{
	if (!(flags & VM_NORESERVE)) {
		if (VM_ACCT(newsize) > VM_ACCT(oldsize))
			return security_vm_enough_memory_mm(current->mm,
					VM_ACCT(newsize) - VM_ACCT(oldsize));
		else if (VM_ACCT(newsize) < VM_ACCT(oldsize))
			vm_unacct_memory(VM_ACCT(oldsize) - VM_ACCT(newsize));
	}
	return 0;
}

/*
 * ... whereas tmpfs objects are accounted incrementally as
 * pages are allocated, in order to allow huge sparse files.
 * shmem_getpage reports shmem_acct_block failure as -ENOSPC not -ENOMEM,
 * so that a failure on a sparse tmpfs mapping will give SIGBUS not OOM.
 */
static inline int shmem_acct_block(unsigned long flags)
{
	return (flags & VM_NORESERVE) ?
		security_vm_enough_memory_mm(current->mm, VM_ACCT(PAGE_CACHE_SIZE)) : 0;
}

static inline void shmem_unacct_blocks(unsigned long flags, long pages)
{
	if (flags & VM_NORESERVE)
		vm_unacct_memory(pages * VM_ACCT(PAGE_CACHE_SIZE));
}

static const struct super_operations shmem_ops;
static const struct address_space_operations shmem_aops;
static const struct file_operations shmem_file_operations;
static const struct inode_operations shmem_inode_operations;
static const struct inode_operations shmem_dir_inode_operations;
static const struct inode_operations shmem_special_inode_operations;
static const struct vm_operations_struct shmem_vm_ops;

static LIST_HEAD(shmem_swaplist);
static DEFINE_MUTEX(shmem_swaplist_mutex);

static int shmem_reserve_inode(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	if (sbinfo->max_inodes) {
		spin_lock(&sbinfo->stat_lock);
		if (!sbinfo->free_inodes) {
			spin_unlock(&sbinfo->stat_lock);
			return -ENOSPC;
		}
		sbinfo->free_inodes--;
		spin_unlock(&sbinfo->stat_lock);
	}
	return 0;
}

static void shmem_free_inode(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	if (sbinfo->max_inodes) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_inodes++;
		spin_unlock(&sbinfo->stat_lock);
	}
}

/**
 * shmem_recalc_inode - recalculate the block usage of an inode
 * @inode: inode to recalc
 *
 * We have to calculate the free blocks since the mm can drop
 * undirtied hole pages behind our back.
 *
 * But normally   info->alloced == inode->i_mapping->nrpages + info->swapped
 * So mm freed is info->alloced - (inode->i_mapping->nrpages + info->swapped)
 *
 * It has to be called with the spinlock held.
 */
static void shmem_recalc_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	long freed;

	freed = info->alloced - info->swapped - inode->i_mapping->nrpages;
	if (freed > 0) {
		struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
		if (sbinfo->max_blocks)
			percpu_counter_add(&sbinfo->used_blocks, -freed);
		info->alloced -= freed;
		inode->i_blocks -= freed * BLOCKS_PER_PAGE;
		shmem_unacct_blocks(info->flags, freed);
	}
}

/*
 * Replace item expected in radix tree by a new item, while holding tree lock.
 */
static int shmem_radix_tree_replace(struct address_space *mapping,
			pgoff_t index, void *expected, void *replacement)
{
	void **pslot;
	void *item;

	VM_BUG_ON(!expected);
	VM_BUG_ON(!replacement);
	pslot = radix_tree_lookup_slot(&mapping->page_tree, index);
	if (!pslot)
		return -ENOENT;
	item = radix_tree_deref_slot_protected(pslot, &mapping->tree_lock);
	if (item != expected)
		return -ENOENT;
	radix_tree_replace_slot(pslot, replacement);
	return 0;
}

/*
 * Sometimes, before we decide whether to proceed or to fail, we must check
 * that an entry was not already brought back from swap by a racing thread.
 *
 * Checking page is not enough: by the time a SwapCache page is locked, it
 * might be reused, and again be SwapCache, using the same swap as before.
 */
static bool shmem_confirm_swap(struct address_space *mapping,
			       pgoff_t index, swp_entry_t swap)
{
	void *item;

	rcu_read_lock();
	item = radix_tree_lookup(&mapping->page_tree, index);
	rcu_read_unlock();
	return item == swp_to_radix_entry(swap);
}

/*
 * Like add_to_page_cache_locked, but error if expected item has gone.
 */
static int shmem_add_to_page_cache(struct page *page,
				   struct address_space *mapping,
				   pgoff_t index, void *expected)
{
	int error;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageSwapBacked(page), page);

	page_cache_get(page);
	page->mapping = mapping;
	page->index = index;

	spin_lock_irq(&mapping->tree_lock);
	if (!expected)
		error = radix_tree_insert(&mapping->page_tree, index, page);
	else
		error = shmem_radix_tree_replace(mapping, index, expected,
								 page);
	if (!error) {
		mapping->nrpages++;
		__inc_zone_page_state(page, NR_FILE_PAGES);
		__inc_zone_page_state(page, NR_SHMEM);
		spin_unlock_irq(&mapping->tree_lock);
	} else {
		page->mapping = NULL;
		spin_unlock_irq(&mapping->tree_lock);
		page_cache_release(page);
	}
	return error;
}

/*
 * Like delete_from_page_cache, but substitutes swap for page.
 */
static void shmem_delete_from_page_cache(struct page *page, void *radswap)
{
	struct address_space *mapping = page->mapping;
	int error;

	spin_lock_irq(&mapping->tree_lock);
	error = shmem_radix_tree_replace(mapping, page->index, page, radswap);
	page->mapping = NULL;
	mapping->nrpages--;
	__dec_zone_page_state(page, NR_FILE_PAGES);
	__dec_zone_page_state(page, NR_SHMEM);
	spin_unlock_irq(&mapping->tree_lock);
	page_cache_release(page);
	BUG_ON(error);
}

/*
 * Remove swap entry from radix tree, free the swap and its page cache.
 */
static int shmem_free_swap(struct address_space *mapping,
			   pgoff_t index, void *radswap)
{
	void *old;

	spin_lock_irq(&mapping->tree_lock);
	old = radix_tree_delete_item(&mapping->page_tree, index, radswap);
	spin_unlock_irq(&mapping->tree_lock);
	if (old != radswap)
		return -ENOENT;
	free_swap_and_cache(radix_to_swp_entry(radswap));
	return 0;
}

/*
 * Determine (in bytes) how many of the shmem object's pages mapped by the
 * given offsets are swapped out.
 *
 * This is safe to call without i_mutex or mapping->tree_lock thanks to RCU,
 * as long as the inode doesn't go away and racy results are not a problem.
 */
unsigned long shmem_partial_swap_usage(struct address_space *mapping,
						pgoff_t start, pgoff_t end)
{
	struct radix_tree_iter iter;
	void **slot;
	struct page *page;
	unsigned long swapped = 0;

	rcu_read_lock();

	radix_tree_for_each_slot(slot, &mapping->page_tree, &iter, start) {
		if (iter.index >= end)
			break;

		page = radix_tree_deref_slot(slot);

		if (radix_tree_deref_retry(page)) {
			slot = radix_tree_iter_retry(&iter);
			continue;
		}

		if (radix_tree_exceptional_entry(page))
			swapped++;

		if (need_resched()) {
			cond_resched_rcu();
			slot = radix_tree_iter_next(&iter);
		}
	}

	rcu_read_unlock();

	return swapped << PAGE_SHIFT;
}

/*
 * Determine (in bytes) how many of the shmem object's pages mapped by the
 * given vma is swapped out.
 *
 * This is safe to call without i_mutex or mapping->tree_lock thanks to RCU,
 * as long as the inode doesn't go away and racy results are not a problem.
 */
unsigned long shmem_swap_usage(struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(vma->vm_file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct address_space *mapping = inode->i_mapping;
	unsigned long swapped;

	/* Be careful as we don't hold info->lock */
	swapped = READ_ONCE(info->swapped);

	/*
	 * The easier cases are when the shmem object has nothing in swap, or
	 * the vma maps it whole. Then we can simply use the stats that we
	 * already track.
	 */
	if (!swapped)
		return 0;

	if (!vma->vm_pgoff && vma->vm_end - vma->vm_start >= inode->i_size)
		return swapped << PAGE_SHIFT;

	/* Here comes the more involved part */
	return shmem_partial_swap_usage(mapping,
			linear_page_index(vma, vma->vm_start),
			linear_page_index(vma, vma->vm_end));
}

/*
 * SysV IPC SHM_UNLOCK restore Unevictable pages to their evictable lists.
 */
void shmem_unlock_mapping(struct address_space *mapping)
{
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	pgoff_t index = 0;

	pagevec_init(&pvec, 0);
	/*
	 * Minor point, but we might as well stop if someone else SHM_LOCKs it.
	 */
	while (!mapping_unevictable(mapping)) {
		/*
		 * Avoid pagevec_lookup(): find_get_pages() returns 0 as if it
		 * has finished, if it hits a row of PAGEVEC_SIZE swap entries.
		 */
		pvec.nr = find_get_entries(mapping, index,
					   PAGEVEC_SIZE, pvec.pages, indices);
		if (!pvec.nr)
			break;
		index = indices[pvec.nr - 1] + 1;
		pagevec_remove_exceptionals(&pvec);
		check_move_unevictable_pages(pvec.pages, pvec.nr);
		pagevec_release(&pvec);
		cond_resched();
	}
}

/*
 * Remove range of pages and swap entries from radix tree, and free them.
 * If !unfalloc, truncate or punch hole; if unfalloc, undo failed fallocate.
 */
static void shmem_undo_range(struct inode *inode, loff_t lstart, loff_t lend,
								 bool unfalloc)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t start = (lstart + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	pgoff_t end = (lend + 1) >> PAGE_CACHE_SHIFT;
	unsigned int partial_start = lstart & (PAGE_CACHE_SIZE - 1);
	unsigned int partial_end = (lend + 1) & (PAGE_CACHE_SIZE - 1);
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	long nr_swaps_freed = 0;
	pgoff_t index;
	int i;

	if (lend == -1)
		end = -1;	/* unsigned, so actually very big */

	pagevec_init(&pvec, 0);
	index = start;
	while (index < end) {
		pvec.nr = find_get_entries(mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE),
			pvec.pages, indices);
		if (!pvec.nr)
			break;
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			index = indices[i];
			if (index >= end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				if (unfalloc)
					continue;
				nr_swaps_freed += !shmem_free_swap(mapping,
								index, page);
				continue;
			}

			if (!trylock_page(page))
				continue;
			if (!unfalloc || !PageUptodate(page)) {
				if (page->mapping == mapping) {
					VM_BUG_ON_PAGE(PageWriteback(page), page);
					truncate_inode_page(mapping, page);
				}
			}
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		cond_resched();
		index++;
	}

	if (partial_start) {
		struct page *page = NULL;
		shmem_getpage(inode, start - 1, &page, SGP_READ, NULL);
		if (page) {
			unsigned int top = PAGE_CACHE_SIZE;
			if (start > end) {
				top = partial_end;
				partial_end = 0;
			}
			zero_user_segment(page, partial_start, top);
			set_page_dirty(page);
			unlock_page(page);
			page_cache_release(page);
		}
	}
	if (partial_end) {
		struct page *page = NULL;
		shmem_getpage(inode, end, &page, SGP_READ, NULL);
		if (page) {
			zero_user_segment(page, 0, partial_end);
			set_page_dirty(page);
			unlock_page(page);
			page_cache_release(page);
		}
	}
	if (start >= end)
		return;

	index = start;
	while (index < end) {
		cond_resched();

		pvec.nr = find_get_entries(mapping, index,
				min(end - index, (pgoff_t)PAGEVEC_SIZE),
				pvec.pages, indices);
		if (!pvec.nr) {
			/* If all gone or hole-punch or unfalloc, we're done */
			if (index == start || end != -1)
				break;
			/* But if truncating, restart to make sure all gone */
			index = start;
			continue;
		}
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			index = indices[i];
			if (index >= end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				if (unfalloc)
					continue;
				if (shmem_free_swap(mapping, index, page)) {
					/* Swap was replaced by page: retry */
					index--;
					break;
				}
				nr_swaps_freed++;
				continue;
			}

			lock_page(page);
			if (!unfalloc || !PageUptodate(page)) {
				if (page->mapping == mapping) {
					VM_BUG_ON_PAGE(PageWriteback(page), page);
					truncate_inode_page(mapping, page);
				} else {
					/* Page was replaced by swap: retry */
					unlock_page(page);
					index--;
					break;
				}
			}
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		index++;
	}

	spin_lock(&info->lock);
	info->swapped -= nr_swaps_freed;
	shmem_recalc_inode(inode);
	spin_unlock(&info->lock);
}

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	shmem_undo_range(inode, lstart, lend, false);
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

static int shmem_getattr(struct vfsmount *mnt, struct dentry *dentry,
			 struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (info->alloced - info->swapped != inode->i_mapping->nrpages) {
		spin_lock(&info->lock);
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);
	}
	generic_fillattr(inode, stat);
	return 0;
}

static int shmem_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int error;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		loff_t oldsize = inode->i_size;
		loff_t newsize = attr->ia_size;

		/* protected by i_mutex */
		if ((newsize < oldsize && (info->seals & F_SEAL_SHRINK)) ||
		    (newsize > oldsize && (info->seals & F_SEAL_GROW)))
			return -EPERM;

		if (newsize != oldsize) {
			error = shmem_reacct_size(SHMEM_I(inode)->flags,
					oldsize, newsize);
			if (error)
				return error;
			i_size_write(inode, newsize);
			inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		}
		if (newsize <= oldsize) {
			loff_t holebegin = round_up(newsize, PAGE_SIZE);
			if (oldsize > holebegin)
				unmap_mapping_range(inode->i_mapping,
							holebegin, 0, 1);
			if (info->alloced)
				shmem_truncate_range(inode,
							newsize, (loff_t)-1);
			/* unmap again to remove racily COWed private pages */
			if (oldsize > holebegin)
				unmap_mapping_range(inode->i_mapping,
							holebegin, 0, 1);
		}
	}

	setattr_copy(inode, attr);
	if (attr->ia_valid & ATTR_MODE)
		error = posix_acl_chmod(inode, inode->i_mode);
	return error;
}

static void shmem_evict_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (inode->i_mapping->a_ops == &shmem_aops) {
		shmem_unacct_size(info->flags, inode->i_size);
		inode->i_size = 0;
		shmem_truncate_range(inode, 0, (loff_t)-1);
		if (!list_empty(&info->swaplist)) {
			mutex_lock(&shmem_swaplist_mutex);
			list_del_init(&info->swaplist);
			mutex_unlock(&shmem_swaplist_mutex);
		}
	}

	simple_xattrs_free(&info->xattrs);
	WARN_ON(inode->i_blocks);
	shmem_free_inode(inode->i_sb);
	clear_inode(inode);
}

/*
 * If swap found in inode, free it and move page from swapcache to filecache.
 */
static int shmem_unuse_inode(struct shmem_inode_info *info,
			     swp_entry_t swap, struct page **pagep)
{
	struct address_space *mapping = info->vfs_inode.i_mapping;
	void *radswap;
	pgoff_t index;
	gfp_t gfp;
	int error = 0;

	radswap = swp_to_radix_entry(swap);
	index = radix_tree_locate_item(&mapping->page_tree, radswap);
	if (index == -1)
		return -EAGAIN;	/* tell shmem_unuse we found nothing */

	/*
	 * Move _head_ to start search for next from here.
	 * But be careful: shmem_evict_inode checks list_empty without taking
	 * mutex, and there's an instant in list_move_tail when info->swaplist
	 * would appear empty, if it were the only one on shmem_swaplist.
	 */
	if (shmem_swaplist.next != &info->swaplist)
		list_move_tail(&shmem_swaplist, &info->swaplist);

	gfp = mapping_gfp_mask(mapping);
	if (shmem_should_replace_page(*pagep, gfp)) {
		mutex_unlock(&shmem_swaplist_mutex);
		error = shmem_replace_page(pagep, gfp, info, index);
		mutex_lock(&shmem_swaplist_mutex);
		/*
		 * We needed to drop mutex to make that restrictive page
		 * allocation, but the inode might have been freed while we
		 * dropped it: although a racing shmem_evict_inode() cannot
		 * complete without emptying the radix_tree, our page lock
		 * on this swapcache page is not enough to prevent that -
		 * free_swap_and_cache() of our swap entry will only
		 * trylock_page(), removing swap from radix_tree whatever.
		 *
		 * We must not proceed to shmem_add_to_page_cache() if the
		 * inode has been freed, but of course we cannot rely on
		 * inode or mapping or info to check that.  However, we can
		 * safely check if our swap entry is still in use (and here
		 * it can't have got reused for another page): if it's still
		 * in use, then the inode cannot have been freed yet, and we
		 * can safely proceed (if it's no longer in use, that tells
		 * nothing about the inode, but we don't need to unuse swap).
		 */
		if (!page_swapcount(*pagep))
			error = -ENOENT;
	}

	/*
	 * We rely on shmem_swaplist_mutex, not only to protect the swaplist,
	 * but also to hold up shmem_evict_inode(): so inode cannot be freed
	 * beneath us (pagelock doesn't help until the page is in pagecache).
	 */
	if (!error)
		error = shmem_add_to_page_cache(*pagep, mapping, index,
						radswap);
	if (error != -ENOMEM) {
		/*
		 * Truncation and eviction use free_swap_and_cache(), which
		 * only does trylock page: if we raced, best clean up here.
		 */
		delete_from_swap_cache(*pagep);
		set_page_dirty(*pagep);
		if (!error) {
			spin_lock(&info->lock);
			info->swapped--;
			spin_unlock(&info->lock);
			swap_free(swap);
		}
	}
	return error;
}

/*
 * Search through swapped inodes to find and replace swap by page.
 */
int shmem_unuse(swp_entry_t swap, struct page *page)
{
	struct list_head *this, *next;
	struct shmem_inode_info *info;
	struct mem_cgroup *memcg;
	int error = 0;

	/*
	 * There's a faint possibility that swap page was replaced before
	 * caller locked it: caller will come back later with the right page.
	 */
	if (unlikely(!PageSwapCache(page) || page_private(page) != swap.val))
		goto out;

	/*
	 * Charge page using GFP_KERNEL while we can wait, before taking
	 * the shmem_swaplist_mutex which might hold up shmem_writepage().
	 * Charged back to the user (not to caller) when swap account is used.
	 */
	error = mem_cgroup_try_charge(page, current->mm, GFP_KERNEL, &memcg,
			false);
	if (error)
		goto out;
	/* No radix_tree_preload: swap entry keeps a place for page in tree */
	error = -EAGAIN;

	mutex_lock(&shmem_swaplist_mutex);
	list_for_each_safe(this, next, &shmem_swaplist) {
		info = list_entry(this, struct shmem_inode_info, swaplist);
		if (info->swapped)
			error = shmem_unuse_inode(info, swap, &page);
		else
			list_del_init(&info->swaplist);
		cond_resched();
		if (error != -EAGAIN)
			break;
		/* found nothing in this: move on to search the next */
	}
	mutex_unlock(&shmem_swaplist_mutex);

	if (error) {
		if (error != -ENOMEM)
			error = 0;
		mem_cgroup_cancel_charge(page, memcg, false);
	} else
		mem_cgroup_commit_charge(page, memcg, true, false);
out:
	unlock_page(page);
	page_cache_release(page);
	return error;
}

/*
 * Move the page from the page cache to the swap cache.
 */
static int shmem_writepage(struct page *page, struct writeback_control *wbc)
{
	struct shmem_inode_info *info;
	struct address_space *mapping;
	struct inode *inode;
	swp_entry_t swap;
	pgoff_t index;

	BUG_ON(!PageLocked(page));
	mapping = page->mapping;
	index = page->index;
	inode = mapping->host;
	info = SHMEM_I(inode);
	if (info->flags & VM_LOCKED)
		goto redirty;
	if (!total_swap_pages)
		goto redirty;

	/*
	 * Our capabilities prevent regular writeback or sync from ever calling
	 * shmem_writepage; but a stacking filesystem might use ->writepage of
	 * its underlying filesystem, in which case tmpfs should write out to
	 * swap only in response to memory pressure, and not for the writeback
	 * threads or sync.
	 */
	if (!wbc->for_reclaim) {
		WARN_ON_ONCE(1);	/* Still happens? Tell us about it! */
		goto redirty;
	}

	/*
	 * This is somewhat ridiculous, but without plumbing a SWAP_MAP_FALLOC
	 * value into swapfile.c, the only way we can correctly account for a
	 * fallocated page arriving here is now to initialize it and write it.
	 *
	 * That's okay for a page already fallocated earlier, but if we have
	 * not yet completed the fallocation, then (a) we want to keep track
	 * of this page in case we have to undo it, and (b) it may not be a
	 * good idea to continue anyway, once we're pushing into swap.  So
	 * reactivate the page, and let shmem_fallocate() quit when too many.
	 */
	if (!PageUptodate(page)) {
		if (inode->i_private) {
			struct shmem_falloc *shmem_falloc;
			spin_lock(&inode->i_lock);
			shmem_falloc = inode->i_private;
			if (shmem_falloc &&
			    !shmem_falloc->waitq &&
			    index >= shmem_falloc->start &&
			    index < shmem_falloc->next)
				shmem_falloc->nr_unswapped++;
			else
				shmem_falloc = NULL;
			spin_unlock(&inode->i_lock);
			if (shmem_falloc)
				goto redirty;
		}
		clear_highpage(page);
		flush_dcache_page(page);
		SetPageUptodate(page);
	}

	swap = get_swap_page();
	if (!swap.val)
		goto redirty;

	if (mem_cgroup_try_charge_swap(page, swap))
		goto free_swap;

	/*
	 * Add inode to shmem_unuse()'s list of swapped-out inodes,
	 * if it's not already there.  Do it now before the page is
	 * moved to swap cache, when its pagelock no longer protects
	 * the inode from eviction.  But don't unlock the mutex until
	 * we've incremented swapped, because shmem_unuse_inode() will
	 * prune a !swapped inode from the swaplist under this mutex.
	 */
	mutex_lock(&shmem_swaplist_mutex);
	if (list_empty(&info->swaplist))
		list_add_tail(&info->swaplist, &shmem_swaplist);

	if (add_to_swap_cache(page, swap, GFP_ATOMIC) == 0) {
		spin_lock(&info->lock);
		shmem_recalc_inode(inode);
		info->swapped++;
		spin_unlock(&info->lock);

		swap_shmem_alloc(swap);
		shmem_delete_from_page_cache(page, swp_to_radix_entry(swap));

		mutex_unlock(&shmem_swaplist_mutex);
		BUG_ON(page_mapped(page));
		swap_writepage(page, wbc);
		return 0;
	}

	mutex_unlock(&shmem_swaplist_mutex);
free_swap:
	swapcache_free(swap);
redirty:
	set_page_dirty(page);
	if (wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;	/* Return with page locked */
	unlock_page(page);
	return 0;
}

#ifdef CONFIG_NUMA
#ifdef CONFIG_TMPFS
static void shmem_show_mpol(struct seq_file *seq, struct mempolicy *mpol)
{
	char buffer[64];

	if (!mpol || mpol->mode == MPOL_DEFAULT)
		return;		/* show nothing */

	mpol_to_str(buffer, sizeof(buffer), mpol);

	seq_printf(seq, ",mpol=%s", buffer);
}

static struct mempolicy *shmem_get_sbmpol(struct shmem_sb_info *sbinfo)
{
	struct mempolicy *mpol = NULL;
	if (sbinfo->mpol) {
		spin_lock(&sbinfo->stat_lock);	/* prevent replace/use races */
		mpol = sbinfo->mpol;
		mpol_get(mpol);
		spin_unlock(&sbinfo->stat_lock);
	}
	return mpol;
}
#endif /* CONFIG_TMPFS */

static struct page *shmem_swapin(swp_entry_t swap, gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct page *page;

	/* Create a pseudo vma that just contains the policy */
	pvma.vm_start = 0;
	/* Bias interleave by inode number to distribute better across nodes */
	pvma.vm_pgoff = index + info->vfs_inode.i_ino;
	pvma.vm_ops = NULL;
	pvma.vm_policy = mpol_shared_policy_lookup(&info->policy, index);

	page = swapin_readahead(swap, gfp, &pvma, 0);

	/* Drop reference taken by mpol_shared_policy_lookup() */
	mpol_cond_put(pvma.vm_policy);

	return page;
}

static struct page *shmem_alloc_page(gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct page *page;

	/* Create a pseudo vma that just contains the policy */
	pvma.vm_start = 0;
	/* Bias interleave by inode number to distribute better across nodes */
	pvma.vm_pgoff = index + info->vfs_inode.i_ino;
	pvma.vm_ops = NULL;
	pvma.vm_policy = mpol_shared_policy_lookup(&info->policy, index);

	page = alloc_page_vma(gfp, &pvma, 0);

	/* Drop reference taken by mpol_shared_policy_lookup() */
	mpol_cond_put(pvma.vm_policy);

	return page;
}
#else /* !CONFIG_NUMA */
#ifdef CONFIG_TMPFS
static inline void shmem_show_mpol(struct seq_file *seq, struct mempolicy *mpol)
{
}
#endif /* CONFIG_TMPFS */

static inline struct page *shmem_swapin(swp_entry_t swap, gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	return swapin_readahead(swap, gfp, NULL, 0);
}

static inline struct page *shmem_alloc_page(gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	return alloc_page(gfp);
}
#endif /* CONFIG_NUMA */

#if !defined(CONFIG_NUMA) || !defined(CONFIG_TMPFS)
static inline struct mempolicy *shmem_get_sbmpol(struct shmem_sb_info *sbinfo)
{
	return NULL;
}
#endif

/*
 * When a page is moved from swapcache to shmem filecache (either by the
 * usual swapin of shmem_getpage_gfp(), or by the less common swapoff of
 * shmem_unuse_inode()), it may have been read in earlier from swap, in
 * ignorance of the mapping it belongs to.  If that mapping has special
 * constraints (like the gma500 GEM driver, which requires RAM below 4GB),
 * we may need to copy to a suitable page before moving to filecache.
 *
 * In a future release, this may well be extended to respect cpuset and
 * NUMA mempolicy, and applied also to anonymous pages in do_swap_page();
 * but for now it is a simple matter of zone.
 */
static bool shmem_should_replace_page(struct page *page, gfp_t gfp)
{
	return page_zonenum(page) > gfp_zone(gfp);
}

static int shmem_replace_page(struct page **pagep, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index)
{
	struct page *oldpage, *newpage;
	struct address_space *swap_mapping;
	pgoff_t swap_index;
	int error;

	oldpage = *pagep;
	swap_index = page_private(oldpage);
	swap_mapping = page_mapping(oldpage);

	/*
	 * We have arrived here because our zones are constrained, so don't
	 * limit chance of success by further cpuset and node constraints.
	 */
	gfp &= ~GFP_CONSTRAINT_MASK;
	newpage = shmem_alloc_page(gfp, info, index);
	if (!newpage)
		return -ENOMEM;

	page_cache_get(newpage);
	copy_highpage(newpage, oldpage);
	flush_dcache_page(newpage);

	__SetPageLocked(newpage);
	SetPageUptodate(newpage);
	SetPageSwapBacked(newpage);
	set_page_private(newpage, swap_index);
	SetPageSwapCache(newpage);

	/*
	 * Our caller will very soon move newpage out of swapcache, but it's
	 * a nice clean interface for us to replace oldpage by newpage there.
	 */
	spin_lock_irq(&swap_mapping->tree_lock);
	error = shmem_radix_tree_replace(swap_mapping, swap_index, oldpage,
								   newpage);
	if (!error) {
		__inc_zone_page_state(newpage, NR_FILE_PAGES);
		__dec_zone_page_state(oldpage, NR_FILE_PAGES);
	}
	spin_unlock_irq(&swap_mapping->tree_lock);

	if (unlikely(error)) {
		/*
		 * Is this possible?  I think not, now that our callers check
		 * both PageSwapCache and page_private after getting page lock;
		 * but be defensive.  Reverse old to newpage for clear and free.
		 */
		oldpage = newpage;
	} else {
		mem_cgroup_migrate(oldpage, newpage);
		lru_cache_add_anon(newpage);
		*pagep = newpage;
	}

	ClearPageSwapCache(oldpage);
	set_page_private(oldpage, 0);

	unlock_page(oldpage);
	page_cache_release(oldpage);
	page_cache_release(oldpage);
	return error;
}

/*
 * shmem_getpage_gfp - find page in cache, or get from swap, or allocate
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache
 */
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, gfp_t gfp, int *fault_type)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo;
	struct mem_cgroup *memcg;
	struct page *page;
	swp_entry_t swap;
	int error;
	int once = 0;
	int alloced = 0;

	if (index > (MAX_LFS_FILESIZE >> PAGE_CACHE_SHIFT))
		return -EFBIG;
repeat:
	swap.val = 0;
	page = find_lock_entry(mapping, index);
	if (radix_tree_exceptional_entry(page)) {
		swap = radix_to_swp_entry(page);
		page = NULL;
	}

	if (sgp != SGP_WRITE && sgp != SGP_FALLOC &&
	    ((loff_t)index << PAGE_CACHE_SHIFT) >= i_size_read(inode)) {
		error = -EINVAL;
		goto unlock;
	}

	if (page && sgp == SGP_WRITE)
		mark_page_accessed(page);

	/* fallocated page? */
	if (page && !PageUptodate(page)) {
		if (sgp != SGP_READ)
			goto clear;
		unlock_page(page);
		page_cache_release(page);
		page = NULL;
	}
	if (page || (sgp == SGP_READ && !swap.val)) {
		*pagep = page;
		return 0;
	}

	/*
	 * Fast cache lookup did not find it:
	 * bring it back from swap or allocate.
	 */
	info = SHMEM_I(inode);
	sbinfo = SHMEM_SB(inode->i_sb);

	if (swap.val) {
		/* Look it up and read it in.. */
		page = lookup_swap_cache(swap);
		if (!page) {
			/* here we actually do the io */
			if (fault_type)
				*fault_type |= VM_FAULT_MAJOR;
			page = shmem_swapin(swap, gfp, info, index);
			if (!page) {
				error = -ENOMEM;
				goto failed;
			}
		}

		/* We have to do this with page locked to prevent races */
		lock_page(page);
		if (!PageSwapCache(page) || page_private(page) != swap.val ||
		    !shmem_confirm_swap(mapping, index, swap)) {
			error = -EEXIST;	/* try again */
			goto unlock;
		}
		if (!PageUptodate(page)) {
			error = -EIO;
			goto failed;
		}
		wait_on_page_writeback(page);

		if (shmem_should_replace_page(page, gfp)) {
			error = shmem_replace_page(&page, gfp, info, index);
			if (error)
				goto failed;
		}

		error = mem_cgroup_try_charge(page, current->mm, gfp, &memcg,
				false);
		if (!error) {
			error = shmem_add_to_page_cache(page, mapping, index,
						swp_to_radix_entry(swap));
			/*
			 * We already confirmed swap under page lock, and make
			 * no memory allocation here, so usually no possibility
			 * of error; but free_swap_and_cache() only trylocks a
			 * page, so it is just possible that the entry has been
			 * truncated or holepunched since swap was confirmed.
			 * shmem_undo_range() will have done some of the
			 * unaccounting, now delete_from_swap_cache() will do
			 * the rest.
			 * Reset swap.val? No, leave it so "failed" goes back to
			 * "repeat": reading a hole and writing should succeed.
			 */
			if (error) {
				mem_cgroup_cancel_charge(page, memcg, false);
				delete_from_swap_cache(page);
			}
		}
		if (error)
			goto failed;

		mem_cgroup_commit_charge(page, memcg, true, false);

		spin_lock(&info->lock);
		info->swapped--;
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);

		if (sgp == SGP_WRITE)
			mark_page_accessed(page);

		delete_from_swap_cache(page);
		set_page_dirty(page);
		swap_free(swap);

	} else {
		if (shmem_acct_block(info->flags)) {
			error = -ENOSPC;
			goto failed;
		}
		if (sbinfo->max_blocks) {
			if (percpu_counter_compare(&sbinfo->used_blocks,
						sbinfo->max_blocks) >= 0) {
				error = -ENOSPC;
				goto unacct;
			}
			percpu_counter_inc(&sbinfo->used_blocks);
		}

		page = shmem_alloc_page(gfp, info, index);
		if (!page) {
			error = -ENOMEM;
			goto decused;
		}

		__SetPageSwapBacked(page);
		__SetPageLocked(page);
		if (sgp == SGP_WRITE)
			__SetPageReferenced(page);

		error = mem_cgroup_try_charge(page, current->mm, gfp, &memcg,
				false);
		if (error)
			goto decused;
		error = radix_tree_maybe_preload(gfp & GFP_RECLAIM_MASK);
		if (!error) {
			error = shmem_add_to_page_cache(page, mapping, index,
							NULL);
			radix_tree_preload_end();
		}
		if (error) {
			mem_cgroup_cancel_charge(page, memcg, false);
			goto decused;
		}
		mem_cgroup_commit_charge(page, memcg, false, false);
		lru_cache_add_anon(page);

		spin_lock(&info->lock);
		info->alloced++;
		inode->i_blocks += BLOCKS_PER_PAGE;
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);
		alloced = true;

		/*
		 * Let SGP_FALLOC use the SGP_WRITE optimization on a new page.
		 */
		if (sgp == SGP_FALLOC)
			sgp = SGP_WRITE;
clear:
		/*
		 * Let SGP_WRITE caller clear ends if write does not fill page;
		 * but SGP_FALLOC on a page fallocated earlier must initialize
		 * it now, lest undo on failure cancel our earlier guarantee.
		 */
		if (sgp != SGP_WRITE) {
			clear_highpage(page);
			flush_dcache_page(page);
			SetPageUptodate(page);
		}
		if (sgp == SGP_DIRTY)
			set_page_dirty(page);
	}

	/* Perhaps the file has been truncated since we checked */
	if (sgp != SGP_WRITE && sgp != SGP_FALLOC &&
	    ((loff_t)index << PAGE_CACHE_SHIFT) >= i_size_read(inode)) {
		if (alloced) {
			ClearPageDirty(page);
			delete_from_page_cache(page);
			spin_lock(&info->lock);
			shmem_recalc_inode(inode);
			spin_unlock(&info->lock);
		}
		error = -EINVAL;
		goto unlock;
	}
	*pagep = page;
	return 0;

	/*
	 * Error recovery.
	 */
decused:
	if (sbinfo->max_blocks)
		percpu_counter_add(&sbinfo->used_blocks, -1);
unacct:
	shmem_unacct_blocks(info->flags, 1);
failed:
	if (swap.val && !shmem_confirm_swap(mapping, index, swap))
		error = -EEXIST;
unlock:
	if (page) {
		unlock_page(page);
		page_cache_release(page);
	}
	if (error == -ENOSPC && !once++) {
		info = SHMEM_I(inode);
		spin_lock(&info->lock);
		shmem_recalc_inode(inode);
		spin_unlock(&info->lock);
		goto repeat;
	}
	if (error == -EEXIST)	/* from above or from radix_tree_insert */
		goto repeat;
	return error;
}

static int shmem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vma->vm_file);
	int error;
	int ret = VM_FAULT_LOCKED;

	/*
	 * Trinity finds that probing a hole which tmpfs is punching can
	 * prevent the hole-punch from ever completing: which in turn
	 * locks writers out with its hold on i_mutex.  So refrain from
	 * faulting pages into the hole while it's being punched.  Although
	 * shmem_undo_range() does remove the additions, it may be unable to
	 * keep up, as each new page needs its own unmap_mapping_range() call,
	 * and the i_mmap tree grows ever slower to scan if new vmas are added.
	 *
	 * It does not matter if we sometimes reach this check just before the
	 * hole-punch begins, so that one fault then races with the punch:
	 * we just need to make racing faults a rare case.
	 *
	 * The implementation below would be much simpler if we just used a
	 * standard mutex or completion: but we cannot take i_mutex in fault,
	 * and bloating every shmem inode for this unlikely case would be sad.
	 */
	if (unlikely(inode->i_private)) {
		struct shmem_falloc *shmem_falloc;

		spin_lock(&inode->i_lock);
		shmem_falloc = inode->i_private;
		if (shmem_falloc &&
		    shmem_falloc->waitq &&
		    vmf->pgoff >= shmem_falloc->start &&
		    vmf->pgoff < shmem_falloc->next) {
			wait_queue_head_t *shmem_falloc_waitq;
			DEFINE_WAIT(shmem_fault_wait);

			ret = VM_FAULT_NOPAGE;
			if ((vmf->flags & FAULT_FLAG_ALLOW_RETRY) &&
			   !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
				/* It's polite to up mmap_sem if we can */
				up_read(&vma->vm_mm->mmap_sem);
				ret = VM_FAULT_RETRY;
			}

			shmem_falloc_waitq = shmem_falloc->waitq;
			prepare_to_wait(shmem_falloc_waitq, &shmem_fault_wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock(&inode->i_lock);
			schedule();

			/*
			 * shmem_falloc_waitq points into the shmem_fallocate()
			 * stack of the hole-punching task: shmem_falloc_waitq
			 * is usually invalid by the time we reach here, but
			 * finish_wait() does not dereference it in that case;
			 * though i_lock needed lest racing with wake_up_all().
			 */
			spin_lock(&inode->i_lock);
			finish_wait(shmem_falloc_waitq, &shmem_fault_wait);
			spin_unlock(&inode->i_lock);
			return ret;
		}
		spin_unlock(&inode->i_lock);
	}

	error = shmem_getpage(inode, vmf->pgoff, &vmf->page, SGP_CACHE, &ret);
	if (error)
		return ((error == -ENOMEM) ? VM_FAULT_OOM : VM_FAULT_SIGBUS);

	if (ret & VM_FAULT_MAJOR) {
		count_vm_event(PGMAJFAULT);
		mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
	}
	return ret;
}

#ifdef CONFIG_NUMA
static int shmem_set_policy(struct vm_area_struct *vma, struct mempolicy *mpol)
{
	struct inode *inode = file_inode(vma->vm_file);
	return mpol_set_shared_policy(&SHMEM_I(inode)->policy, vma, mpol);
}

static struct mempolicy *shmem_get_policy(struct vm_area_struct *vma,
					  unsigned long addr)
{
	struct inode *inode = file_inode(vma->vm_file);
	pgoff_t index;

	index = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	return mpol_shared_policy_lookup(&SHMEM_I(inode)->policy, index);
}
#endif

int shmem_lock(struct file *file, int lock, struct user_struct *user)
{
	struct inode *inode = file_inode(file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int retval = -ENOMEM;

	spin_lock(&info->lock);
	if (lock && !(info->flags & VM_LOCKED)) {
		if (!user_shm_lock(inode->i_size, user))
			goto out_nomem;
		info->flags |= VM_LOCKED;
		mapping_set_unevictable(file->f_mapping);
	}
	if (!lock && (info->flags & VM_LOCKED) && user) {
		user_shm_unlock(inode->i_size, user);
		info->flags &= ~VM_LOCKED;
		mapping_clear_unevictable(file->f_mapping);
	}
	retval = 0;

out_nomem:
	spin_unlock(&info->lock);
	return retval;
}

static int shmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &shmem_vm_ops;
	return 0;
}

static struct inode *shmem_get_inode(struct super_block *sb, const struct inode *dir,
				     umode_t mode, dev_t dev, unsigned long flags)
{
	struct inode *inode;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	if (shmem_reserve_inode(sb))
		return NULL;

	inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_generation = get_seconds();
		info = SHMEM_I(inode);
		memset(info, 0, (char *)inode - (char *)info);
		spin_lock_init(&info->lock);
		info->seals = F_SEAL_SEAL;
		info->flags = flags & VM_NORESERVE;
		INIT_LIST_HEAD(&info->swaplist);
		simple_xattrs_init(&info->xattrs);
		cache_no_acl(inode);

		switch (mode & S_IFMT) {
		default:
			inode->i_op = &shmem_special_inode_operations;
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_mapping->a_ops = &shmem_aops;
			inode->i_op = &shmem_inode_operations;
			inode->i_fop = &shmem_file_operations;
			mpol_shared_policy_init(&info->policy,
						 shmem_get_sbmpol(sbinfo));
			break;
		case S_IFDIR:
			inc_nlink(inode);
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * BOGO_DIRENT_SIZE;
			inode->i_op = &shmem_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			break;
		case S_IFLNK:
			/*
			 * Must not load anything in the rbtree,
			 * mpol_free_shared_policy will not be called.
			 */
			mpol_shared_policy_init(&info->policy, NULL);
			break;
		}
	} else
		shmem_free_inode(sb);
	return inode;
}

bool shmem_mapping(struct address_space *mapping)
{
	if (!mapping->host)
		return false;

	return mapping->host->i_sb->s_op == &shmem_ops;
}

#ifdef CONFIG_TMPFS
static const struct inode_operations shmem_symlink_inode_operations;
static const struct inode_operations shmem_short_symlink_operations;

#ifdef CONFIG_TMPFS_XATTR
static int shmem_initxattrs(struct inode *, const struct xattr *, void *);
#else
#define shmem_initxattrs NULL
#endif

static int
shmem_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	/* i_mutex is held by caller */
	if (unlikely(info->seals)) {
		if (info->seals & F_SEAL_WRITE)
			return -EPERM;
		if ((info->seals & F_SEAL_GROW) && pos + len > inode->i_size)
			return -EPERM;
	}

	return shmem_getpage(inode, index, pagep, SGP_WRITE, NULL);
}

static int
shmem_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;

	if (pos + copied > inode->i_size)
		i_size_write(inode, pos + copied);

	if (!PageUptodate(page)) {
		if (copied < PAGE_CACHE_SIZE) {
			unsigned from = pos & (PAGE_CACHE_SIZE - 1);
			zero_user_segments(page, 0, from,
					from + copied, PAGE_CACHE_SIZE);
		}
		SetPageUptodate(page);
	}
	set_page_dirty(page);
	unlock_page(page);
	page_cache_release(page);

	return copied;
}

static ssize_t shmem_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
	unsigned long offset;
	enum sgp_type sgp = SGP_READ;
	int error = 0;
	ssize_t retval = 0;
	loff_t *ppos = &iocb->ki_pos;

	/*
	 * Might this read be for a stacking filesystem?  Then when reading
	 * holes of a sparse file, we actually need to allocate those pages,
	 * and even mark them dirty, so it cannot exceed the max_blocks limit.
	 */
	if (!iter_is_iovec(to))
		sgp = SGP_DIRTY;

	index = *ppos >> PAGE_CACHE_SHIFT;
	offset = *ppos & ~PAGE_CACHE_MASK;

	for (;;) {
		struct page *page = NULL;
		pgoff_t end_index;
		unsigned long nr, ret;
		loff_t i_size = i_size_read(inode);

		end_index = i_size >> PAGE_CACHE_SHIFT;
		if (index > end_index)
			break;
		if (index == end_index) {
			nr = i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset)
				break;
		}

		error = shmem_getpage(inode, index, &page, sgp, NULL);
		if (error) {
			if (error == -EINVAL)
				error = 0;
			break;
		}
		if (page)
			unlock_page(page);

		/*
		 * We must evaluate after, since reads (unlike writes)
		 * are called without i_mutex protection against truncate
		 */
		nr = PAGE_CACHE_SIZE;
		i_size = i_size_read(inode);
		end_index = i_size >> PAGE_CACHE_SHIFT;
		if (index == end_index) {
			nr = i_size & ~PAGE_CACHE_MASK;
			if (nr <= offset) {
				if (page)
					page_cache_release(page);
				break;
			}
		}
		nr -= offset;

		if (page) {
			/*
			 * If users can be writing to this page using arbitrary
			 * virtual addresses, take care about potential aliasing
			 * before reading the page on the kernel side.
			 */
			if (mapping_writably_mapped(mapping))
				flush_dcache_page(page);
			/*
			 * Mark the page accessed if we read the beginning.
			 */
			if (!offset)
				mark_page_accessed(page);
		} else {
			page = ZERO_PAGE(0);
			page_cache_get(page);
		}

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 */
		ret = copy_page_to_iter(page, offset, nr, to);
		retval += ret;
		offset += ret;
		index += offset >> PAGE_CACHE_SHIFT;
		offset &= ~PAGE_CACHE_MASK;

		page_cache_release(page);
		if (!iov_iter_count(to))
			break;
		if (ret < nr) {
			error = -EFAULT;
			break;
		}
		cond_resched();
	}

	*ppos = ((loff_t) index << PAGE_CACHE_SHIFT) + offset;
	file_accessed(file);
	return retval ? retval : error;
}

static ssize_t shmem_file_splice_read(struct file *in, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags)
{
	struct address_space *mapping = in->f_mapping;
	struct inode *inode = mapping->host;
	unsigned int loff, nr_pages, req_pages;
	struct page *pages[PIPE_DEF_BUFFERS];
	struct partial_page partial[PIPE_DEF_BUFFERS];
	struct page *page;
	pgoff_t index, end_index;
	loff_t isize, left;
	int error, page_nr;
	struct splice_pipe_desc spd = {
		.pages = pages,
		.partial = partial,
		.nr_pages_max = PIPE_DEF_BUFFERS,
		.flags = flags,
		.ops = &page_cache_pipe_buf_ops,
		.spd_release = spd_release_page,
	};

	isize = i_size_read(inode);
	if (unlikely(*ppos >= isize))
		return 0;

	left = isize - *ppos;
	if (unlikely(left < len))
		len = left;

	if (splice_grow_spd(pipe, &spd))
		return -ENOMEM;

	index = *ppos >> PAGE_CACHE_SHIFT;
	loff = *ppos & ~PAGE_CACHE_MASK;
	req_pages = (len + loff + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	nr_pages = min(req_pages, spd.nr_pages_max);

	spd.nr_pages = find_get_pages_contig(mapping, index,
						nr_pages, spd.pages);
	index += spd.nr_pages;
	error = 0;

	while (spd.nr_pages < nr_pages) {
		error = shmem_getpage(inode, index, &page, SGP_CACHE, NULL);
		if (error)
			break;
		unlock_page(page);
		spd.pages[spd.nr_pages++] = page;
		index++;
	}

	index = *ppos >> PAGE_CACHE_SHIFT;
	nr_pages = spd.nr_pages;
	spd.nr_pages = 0;

	for (page_nr = 0; page_nr < nr_pages; page_nr++) {
		unsigned int this_len;

		if (!len)
			break;

		this_len = min_t(unsigned long, len, PAGE_CACHE_SIZE - loff);
		page = spd.pages[page_nr];

		if (!PageUptodate(page) || page->mapping != mapping) {
			error = shmem_getpage(inode, index, &page,
							SGP_CACHE, NULL);
			if (error)
				break;
			unlock_page(page);
			page_cache_release(spd.pages[page_nr]);
			spd.pages[page_nr] = page;
		}

		isize = i_size_read(inode);
		end_index = (isize - 1) >> PAGE_CACHE_SHIFT;
		if (unlikely(!isize || index > end_index))
			break;

		if (end_index == index) {
			unsigned int plen;

			plen = ((isize - 1) & ~PAGE_CACHE_MASK) + 1;
			if (plen <= loff)
				break;

			this_len = min(this_len, plen - loff);
			len = this_len;
		}

		spd.partial[page_nr].offset = loff;
		spd.partial[page_nr].len = this_len;
		len -= this_len;
		loff = 0;
		spd.nr_pages++;
		index++;
	}

	while (page_nr < nr_pages)
		page_cache_release(spd.pages[page_nr++]);

	if (spd.nr_pages)
		error = splice_to_pipe(pipe, &spd);

	splice_shrink_spd(&spd);

	if (error > 0) {
		*ppos += error;
		file_accessed(in);
	}
	return error;
}

/*
 * llseek SEEK_DATA or SEEK_HOLE through the radix_tree.
 */
static pgoff_t shmem_seek_hole_data(struct address_space *mapping,
				    pgoff_t index, pgoff_t end, int whence)
{
	struct page *page;
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	bool done = false;
	int i;

	pagevec_init(&pvec, 0);
	pvec.nr = 1;		/* start small: we may be there already */
	while (!done) {
		pvec.nr = find_get_entries(mapping, index,
					pvec.nr, pvec.pages, indices);
		if (!pvec.nr) {
			if (whence == SEEK_DATA)
				index = end;
			break;
		}
		for (i = 0; i < pvec.nr; i++, index++) {
			if (index < indices[i]) {
				if (whence == SEEK_HOLE) {
					done = true;
					break;
				}
				index = indices[i];
			}
			page = pvec.pages[i];
			if (page && !radix_tree_exceptional_entry(page)) {
				if (!PageUptodate(page))
					page = NULL;
			}
			if (index >= end ||
			    (page && whence == SEEK_DATA) ||
			    (!page && whence == SEEK_HOLE)) {
				done = true;
				break;
			}
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		pvec.nr = PAGEVEC_SIZE;
		cond_resched();
	}
	return index;
}

static loff_t shmem_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	pgoff_t start, end;
	loff_t new_offset;

	if (whence != SEEK_DATA && whence != SEEK_HOLE)
		return generic_file_llseek_size(file, offset, whence,
					MAX_LFS_FILESIZE, i_size_read(inode));
	inode_lock(inode);
	/* We're holding i_mutex so we can access i_size directly */

	if (offset < 0)
		offset = -EINVAL;
	else if (offset >= inode->i_size)
		offset = -ENXIO;
	else {
		start = offset >> PAGE_CACHE_SHIFT;
		end = (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
		new_offset = shmem_seek_hole_data(mapping, start, end, whence);
		new_offset <<= PAGE_CACHE_SHIFT;
		if (new_offset > offset) {
			if (new_offset < inode->i_size)
				offset = new_offset;
			else if (whence == SEEK_DATA)
				offset = -ENXIO;
			else
				offset = inode->i_size;
		}
	}

	if (offset >= 0)
		offset = vfs_setpos(file, offset, MAX_LFS_FILESIZE);
	inode_unlock(inode);
	return offset;
}

/*
 * We need a tag: a new tag would expand every radix_tree_node by 8 bytes,
 * so reuse a tag which we firmly believe is never set or cleared on shmem.
 */
#define SHMEM_TAG_PINNED        PAGECACHE_TAG_TOWRITE
#define LAST_SCAN               4       /* about 150ms max */

static void shmem_tag_pins(struct address_space *mapping)
{
	struct radix_tree_iter iter;
	void **slot;
	pgoff_t start;
	struct page *page;

	lru_add_drain();
	start = 0;
	rcu_read_lock();

	radix_tree_for_each_slot(slot, &mapping->page_tree, &iter, start) {
		page = radix_tree_deref_slot(slot);
		if (!page || radix_tree_exception(page)) {
			if (radix_tree_deref_retry(page)) {
				slot = radix_tree_iter_retry(&iter);
				continue;
			}
		} else if (page_count(page) - page_mapcount(page) > 1) {
			spin_lock_irq(&mapping->tree_lock);
			radix_tree_tag_set(&mapping->page_tree, iter.index,
					   SHMEM_TAG_PINNED);
			spin_unlock_irq(&mapping->tree_lock);
		}

		if (need_resched()) {
			cond_resched_rcu();
			slot = radix_tree_iter_next(&iter);
		}
	}
	rcu_read_unlock();
}

/*
 * Setting SEAL_WRITE requires us to verify there's no pending writer. However,
 * via get_user_pages(), drivers might have some pending I/O without any active
 * user-space mappings (eg., direct-IO, AIO). Therefore, we look at all pages
 * and see whether it has an elevated ref-count. If so, we tag them and wait for
 * them to be dropped.
 * The caller must guarantee that no new user will acquire writable references
 * to those pages to avoid races.
 */
static int shmem_wait_for_pins(struct address_space *mapping)
{
	struct radix_tree_iter iter;
	void **slot;
	pgoff_t start;
	struct page *page;
	int error, scan;

	shmem_tag_pins(mapping);

	error = 0;
	for (scan = 0; scan <= LAST_SCAN; scan++) {
		if (!radix_tree_tagged(&mapping->page_tree, SHMEM_TAG_PINNED))
			break;

		if (!scan)
			lru_add_drain_all();
		else if (schedule_timeout_killable((HZ << scan) / 200))
			scan = LAST_SCAN;

		start = 0;
		rcu_read_lock();
		radix_tree_for_each_tagged(slot, &mapping->page_tree, &iter,
					   start, SHMEM_TAG_PINNED) {

			page = radix_tree_deref_slot(slot);
			if (radix_tree_exception(page)) {
				if (radix_tree_deref_retry(page)) {
					slot = radix_tree_iter_retry(&iter);
					continue;
				}

				page = NULL;
			}

			if (page &&
			    page_count(page) - page_mapcount(page) != 1) {
				if (scan < LAST_SCAN)
					goto continue_resched;

				/*
				 * On the last scan, we clean up all those tags
				 * we inserted; but make a note that we still
				 * found pages pinned.
				 */
				error = -EBUSY;
			}

			spin_lock_irq(&mapping->tree_lock);
			radix_tree_tag_clear(&mapping->page_tree,
					     iter.index, SHMEM_TAG_PINNED);
			spin_unlock_irq(&mapping->tree_lock);
continue_resched:
			if (need_resched()) {
				cond_resched_rcu();
				slot = radix_tree_iter_next(&iter);
			}
		}
		rcu_read_unlock();
	}

	return error;
}

#define F_ALL_SEALS (F_SEAL_SEAL | \
		     F_SEAL_SHRINK | \
		     F_SEAL_GROW | \
		     F_SEAL_WRITE)

int shmem_add_seals(struct file *file, unsigned int seals)
{
	struct inode *inode = file_inode(file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int error;

	/*
	 * SEALING
	 * Sealing allows multiple parties to share a shmem-file but restrict
	 * access to a specific subset of file operations. Seals can only be
	 * added, but never removed. This way, mutually untrusted parties can
	 * share common memory regions with a well-defined policy. A malicious
	 * peer can thus never perform unwanted operations on a shared object.
	 *
	 * Seals are only supported on special shmem-files and always affect
	 * the whole underlying inode. Once a seal is set, it may prevent some
	 * kinds of access to the file. Currently, the following seals are
	 * defined:
	 *   SEAL_SEAL: Prevent further seals from being set on this file
	 *   SEAL_SHRINK: Prevent the file from shrinking
	 *   SEAL_GROW: Prevent the file from growing
	 *   SEAL_WRITE: Prevent write access to the file
	 *
	 * As we don't require any trust relationship between two parties, we
	 * must prevent seals from being removed. Therefore, sealing a file
	 * only adds a given set of seals to the file, it never touches
	 * existing seals. Furthermore, the "setting seals"-operation can be
	 * sealed itself, which basically prevents any further seal from being
	 * added.
	 *
	 * Semantics of sealing are only defined on volatile files. Only
	 * anonymous shmem files support sealing. More importantly, seals are
	 * never written to disk. Therefore, there's no plan to support it on
	 * other file types.
	 */

	if (file->f_op != &shmem_file_operations)
		return -EINVAL;
	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;
	if (seals & ~(unsigned int)F_ALL_SEALS)
		return -EINVAL;

	inode_lock(inode);

	if (info->seals & F_SEAL_SEAL) {
		error = -EPERM;
		goto unlock;
	}

	if ((seals & F_SEAL_WRITE) && !(info->seals & F_SEAL_WRITE)) {
		error = mapping_deny_writable(file->f_mapping);
		if (error)
			goto unlock;

		error = shmem_wait_for_pins(file->f_mapping);
		if (error) {
			mapping_allow_writable(file->f_mapping);
			goto unlock;
		}
	}

	info->seals |= seals;
	error = 0;

unlock:
	inode_unlock(inode);
	return error;
}
EXPORT_SYMBOL_GPL(shmem_add_seals);

int shmem_get_seals(struct file *file)
{
	if (file->f_op != &shmem_file_operations)
		return -EINVAL;

	return SHMEM_I(file_inode(file))->seals;
}
EXPORT_SYMBOL_GPL(shmem_get_seals);

long shmem_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long error;

	switch (cmd) {
	case F_ADD_SEALS:
		/* disallow upper 32bit */
		if (arg > UINT_MAX)
			return -EINVAL;

		error = shmem_add_seals(file, arg);
		break;
	case F_GET_SEALS:
		error = shmem_get_seals(file);
		break;
	default:
		error = -EINVAL;
		break;
	}

	return error;
}

static long shmem_fallocate(struct file *file, int mode, loff_t offset,
							 loff_t len)
{
	struct inode *inode = file_inode(file);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_falloc shmem_falloc;
	pgoff_t start, index, end;
	int error;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	inode_lock(inode);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		struct address_space *mapping = file->f_mapping;
		loff_t unmap_start = round_up(offset, PAGE_SIZE);
		loff_t unmap_end = round_down(offset + len, PAGE_SIZE) - 1;
		DECLARE_WAIT_QUEUE_HEAD_ONSTACK(shmem_falloc_waitq);

		/* protected by i_mutex */
		if (info->seals & F_SEAL_WRITE) {
			error = -EPERM;
			goto out;
		}

		shmem_falloc.waitq = &shmem_falloc_waitq;
		shmem_falloc.start = unmap_start >> PAGE_SHIFT;
		shmem_falloc.next = (unmap_end + 1) >> PAGE_SHIFT;
		spin_lock(&inode->i_lock);
		inode->i_private = &shmem_falloc;
		spin_unlock(&inode->i_lock);

		if ((u64)unmap_end > (u64)unmap_start)
			unmap_mapping_range(mapping, unmap_start,
					    1 + unmap_end - unmap_start, 0);
		shmem_truncate_range(inode, offset, offset + len - 1);
		/* No need to unmap again: hole-punching leaves COWed pages */

		spin_lock(&inode->i_lock);
		inode->i_private = NULL;
		wake_up_all(&shmem_falloc_waitq);
		spin_unlock(&inode->i_lock);
		error = 0;
		goto out;
	}

	/* We need to check rlimit even when FALLOC_FL_KEEP_SIZE */
	error = inode_newsize_ok(inode, offset + len);
	if (error)
		goto out;

	if ((info->seals & F_SEAL_GROW) && offset + len > inode->i_size) {
		error = -EPERM;
		goto out;
	}

	start = offset >> PAGE_CACHE_SHIFT;
	end = (offset + len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	/* Try to avoid a swapstorm if len is impossible to satisfy */
	if (sbinfo->max_blocks && end - start > sbinfo->max_blocks) {
		error = -ENOSPC;
		goto out;
	}

	shmem_falloc.waitq = NULL;
	shmem_falloc.start = start;
	shmem_falloc.next  = start;
	shmem_falloc.nr_falloced = 0;
	shmem_falloc.nr_unswapped = 0;
	spin_lock(&inode->i_lock);
	inode->i_private = &shmem_falloc;
	spin_unlock(&inode->i_lock);

	for (index = start; index < end; index++) {
		struct page *page;

		/*
		 * Good, the fallocate(2) manpage permits EINTR: we may have
		 * been interrupted because we are using up too much memory.
		 */
		if (signal_pending(current))
			error = -EINTR;
		else if (shmem_falloc.nr_unswapped > shmem_falloc.nr_falloced)
			error = -ENOMEM;
		else
			error = shmem_getpage(inode, index, &page, SGP_FALLOC,
									NULL);
		if (error) {
			/* Remove the !PageUptodate pages we added */
			shmem_undo_range(inode,
				(loff_t)start << PAGE_CACHE_SHIFT,
				(loff_t)index << PAGE_CACHE_SHIFT, true);
			goto undone;
		}

		/*
		 * Inform shmem_writepage() how far we have reached.
		 * No need for lock or barrier: we have the page lock.
		 */
		shmem_falloc.next++;
		if (!PageUptodate(page))
			shmem_falloc.nr_falloced++;

		/*
		 * If !PageUptodate, leave it that way so that freeable pages
		 * can be recognized if we need to rollback on error later.
		 * But set_page_dirty so that memory pressure will swap rather
		 * than free the pages we are allocating (and SGP_CACHE pages
		 * might still be clean: we now need to mark those dirty too).
		 */
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
		cond_resched();
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
	inode->i_ctime = CURRENT_TIME;
undone:
	spin_lock(&inode->i_lock);
	inode->i_private = NULL;
	spin_unlock(&inode->i_lock);
out:
	inode_unlock(inode);
	return error;
}

static int shmem_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(dentry->d_sb);

	buf->f_type = TMPFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;
	if (sbinfo->max_blocks) {
		buf->f_blocks = sbinfo->max_blocks;
		buf->f_bavail =
		buf->f_bfree  = sbinfo->max_blocks -
				percpu_counter_sum(&sbinfo->used_blocks);
	}
	if (sbinfo->max_inodes) {
		buf->f_files = sbinfo->max_inodes;
		buf->f_ffree = sbinfo->free_inodes;
	}
	/* else leave those fields 0 like simple_statfs */
	return 0;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int
shmem_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = shmem_get_inode(dir->i_sb, dir, mode, dev, VM_NORESERVE);
	if (inode) {
		error = simple_acl_create(dir, inode);
		if (error)
			goto out_iput;
		error = security_inode_init_security(inode, dir,
						     &dentry->d_name,
						     shmem_initxattrs, NULL);
		if (error && error != -EOPNOTSUPP)
			goto out_iput;

		error = 0;
		dir->i_size += BOGO_DIRENT_SIZE;
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
	}
	return error;
out_iput:
	iput(inode);
	return error;
}

static int
shmem_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = shmem_get_inode(dir->i_sb, dir, mode, 0, VM_NORESERVE);
	if (inode) {
		error = security_inode_init_security(inode, dir,
						     NULL,
						     shmem_initxattrs, NULL);
		if (error && error != -EOPNOTSUPP)
			goto out_iput;
		error = simple_acl_create(dir, inode);
		if (error)
			goto out_iput;
		d_tmpfile(dentry, inode);
	}
	return error;
out_iput:
	iput(inode);
	return error;
}

static int shmem_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int error;

	if ((error = shmem_mknod(dir, dentry, mode | S_IFDIR, 0)))
		return error;
	inc_nlink(dir);
	return 0;
}

static int shmem_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return shmem_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	int ret;

	/*
	 * No ordinary (disk based) filesystem counts links as inodes;
	 * but each new link needs a new dentry, pinning lowmem, and
	 * tmpfs dentries cannot be pruned until they are unlinked.
	 */
	ret = shmem_reserve_inode(inode->i_sb);
	if (ret)
		goto out;

	dir->i_size += BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	inc_nlink(inode);
	ihold(inode);	/* New dentry reference */
	dget(dentry);		/* Extra pinning count for the created dentry */
	d_instantiate(dentry, inode);
out:
	return ret;
}

static int shmem_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_nlink > 1 && !S_ISDIR(inode->i_mode))
		shmem_free_inode(inode->i_sb);

	dir->i_size -= BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	drop_nlink(inode);
	dput(dentry);	/* Undo the count from "create" - this does all the work */
	return 0;
}

static int shmem_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	drop_nlink(d_inode(dentry));
	drop_nlink(dir);
	return shmem_unlink(dir, dentry);
}

static int shmem_exchange(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	bool old_is_dir = d_is_dir(old_dentry);
	bool new_is_dir = d_is_dir(new_dentry);

	if (old_dir != new_dir && old_is_dir != new_is_dir) {
		if (old_is_dir) {
			drop_nlink(old_dir);
			inc_nlink(new_dir);
		} else {
			drop_nlink(new_dir);
			inc_nlink(old_dir);
		}
	}
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	d_inode(old_dentry)->i_ctime =
	d_inode(new_dentry)->i_ctime = CURRENT_TIME;

	return 0;
}

static int shmem_whiteout(struct inode *old_dir, struct dentry *old_dentry)
{
	struct dentry *whiteout;
	int error;

	whiteout = d_alloc(old_dentry->d_parent, &old_dentry->d_name);
	if (!whiteout)
		return -ENOMEM;

	error = shmem_mknod(old_dir, whiteout,
			    S_IFCHR | WHITEOUT_MODE, WHITEOUT_DEV);
	dput(whiteout);
	if (error)
		return error;

	/*
	 * Cheat and hash the whiteout while the old dentry is still in
	 * place, instead of playing games with FS_RENAME_DOES_D_MOVE.
	 *
	 * d_lookup() will consistently find one of them at this point,
	 * not sure which one, but that isn't even important.
	 */
	d_rehash(whiteout);
	return 0;
}

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename2(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	int they_are_dirs = S_ISDIR(inode->i_mode);

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return shmem_exchange(old_dir, old_dentry, new_dir, new_dentry);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (flags & RENAME_WHITEOUT) {
		int error;

		error = shmem_whiteout(old_dir, old_dentry);
		if (error)
			return error;
	}

	if (d_really_is_positive(new_dentry)) {
		(void) shmem_unlink(new_dir, new_dentry);
		if (they_are_dirs) {
			drop_nlink(d_inode(new_dentry));
			drop_nlink(old_dir);
		}
	} else if (they_are_dirs) {
		drop_nlink(old_dir);
		inc_nlink(new_dir);
	}

	old_dir->i_size -= BOGO_DIRENT_SIZE;
	new_dir->i_size += BOGO_DIRENT_SIZE;
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	inode->i_ctime = CURRENT_TIME;
	return 0;
}

static int shmem_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int error;
	int len;
	struct inode *inode;
	struct page *page;
	struct shmem_inode_info *info;

	len = strlen(symname) + 1;
	if (len > PAGE_CACHE_SIZE)
		return -ENAMETOOLONG;

	inode = shmem_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0, VM_NORESERVE);
	if (!inode)
		return -ENOSPC;

	error = security_inode_init_security(inode, dir, &dentry->d_name,
					     shmem_initxattrs, NULL);
	if (error) {
		if (error != -EOPNOTSUPP) {
			iput(inode);
			return error;
		}
		error = 0;
	}

	info = SHMEM_I(inode);
	inode->i_size = len-1;
	if (len <= SHORT_SYMLINK_LEN) {
		inode->i_link = kmemdup(symname, len, GFP_KERNEL);
		if (!inode->i_link) {
			iput(inode);
			return -ENOMEM;
		}
		inode->i_op = &shmem_short_symlink_operations;
	} else {
		inode_nohighmem(inode);
		error = shmem_getpage(inode, 0, &page, SGP_WRITE, NULL);
		if (error) {
			iput(inode);
			return error;
		}
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_op = &shmem_symlink_inode_operations;
		memcpy(page_address(page), symname, len);
		SetPageUptodate(page);
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
	}
	dir->i_size += BOGO_DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
}

static void shmem_put_link(void *arg)
{
	mark_page_accessed(arg);
	put_page(arg);
}

static const char *shmem_get_link(struct dentry *dentry,
				  struct inode *inode,
				  struct delayed_call *done)
{
	struct page *page = NULL;
	int error;
	if (!dentry) {
		page = find_get_page(inode->i_mapping, 0);
		if (!page)
			return ERR_PTR(-ECHILD);
		if (!PageUptodate(page)) {
			put_page(page);
			return ERR_PTR(-ECHILD);
		}
	} else {
		error = shmem_getpage(inode, 0, &page, SGP_READ, NULL);
		if (error)
			return ERR_PTR(error);
		unlock_page(page);
	}
	set_delayed_call(done, shmem_put_link, page);
	return page_address(page);
}

#ifdef CONFIG_TMPFS_XATTR
/*
 * Superblocks without xattr inode operations may get some security.* xattr
 * support from the LSM "for free". As soon as we have any other xattrs
 * like ACLs, we also need to implement the security.* handlers at
 * filesystem level, though.
 */

/*
 * Callback for security_inode_init_security() for acquiring xattrs.
 */
static int shmem_initxattrs(struct inode *inode,
			    const struct xattr *xattr_array,
			    void *fs_info)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	const struct xattr *xattr;
	struct simple_xattr *new_xattr;
	size_t len;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		new_xattr = simple_xattr_alloc(xattr->value, xattr->value_len);
		if (!new_xattr)
			return -ENOMEM;

		len = strlen(xattr->name) + 1;
		new_xattr->name = kmalloc(XATTR_SECURITY_PREFIX_LEN + len,
					  GFP_KERNEL);
		if (!new_xattr->name) {
			kfree(new_xattr);
			return -ENOMEM;
		}

		memcpy(new_xattr->name, XATTR_SECURITY_PREFIX,
		       XATTR_SECURITY_PREFIX_LEN);
		memcpy(new_xattr->name + XATTR_SECURITY_PREFIX_LEN,
		       xattr->name, len);

		simple_xattr_list_add(&info->xattrs, new_xattr);
	}

	return 0;
}

static int shmem_xattr_handler_get(const struct xattr_handler *handler,
				   struct dentry *dentry, const char *name,
				   void *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));

	name = xattr_full_name(handler, name);
	return simple_xattr_get(&info->xattrs, name, buffer, size);
}

static int shmem_xattr_handler_set(const struct xattr_handler *handler,
				   struct dentry *dentry, const char *name,
				   const void *value, size_t size, int flags)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));

	name = xattr_full_name(handler, name);
	return simple_xattr_set(&info->xattrs, name, value, size, flags);
}

static const struct xattr_handler shmem_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = shmem_xattr_handler_get,
	.set = shmem_xattr_handler_set,
};

static const struct xattr_handler shmem_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = shmem_xattr_handler_get,
	.set = shmem_xattr_handler_set,
};

static const struct xattr_handler *shmem_xattr_handlers[] = {
#ifdef CONFIG_TMPFS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	&shmem_security_xattr_handler,
	&shmem_trusted_xattr_handler,
	NULL
};

static ssize_t shmem_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	return simple_xattr_list(d_inode(dentry), &info->xattrs, buffer, size);
}
#endif /* CONFIG_TMPFS_XATTR */

static const struct inode_operations shmem_short_symlink_operations = {
	.readlink	= generic_readlink,
	.get_link	= simple_get_link,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

static const struct inode_operations shmem_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.get_link	= shmem_get_link,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

static struct dentry *shmem_get_parent(struct dentry *child)
{
	return ERR_PTR(-ESTALE);
}

static int shmem_match(struct inode *ino, void *vfh)
{
	__u32 *fh = vfh;
	__u64 inum = fh[2];
	inum = (inum << 32) | fh[1];
	return ino->i_ino == inum && fh[0] == ino->i_generation;
}

static struct dentry *shmem_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct inode *inode;
	struct dentry *dentry = NULL;
	u64 inum;

	if (fh_len < 3)
		return NULL;

	inum = fid->raw[2];
	inum = (inum << 32) | fid->raw[1];

	inode = ilookup5(sb, (unsigned long)(inum + fid->raw[0]),
			shmem_match, fid->raw);
	if (inode) {
		dentry = d_find_alias(inode);
		iput(inode);
	}

	return dentry;
}

static int shmem_encode_fh(struct inode *inode, __u32 *fh, int *len,
				struct inode *parent)
{
	if (*len < 3) {
		*len = 3;
		return FILEID_INVALID;
	}

	if (inode_unhashed(inode)) {
		/* Unfortunately insert_inode_hash is not idempotent,
		 * so as we hash inodes here rather than at creation
		 * time, we need a lock to ensure we only try
		 * to do it once
		 */
		static DEFINE_SPINLOCK(lock);
		spin_lock(&lock);
		if (inode_unhashed(inode))
			__insert_inode_hash(inode,
					    inode->i_ino + inode->i_generation);
		spin_unlock(&lock);
	}

	fh[0] = inode->i_generation;
	fh[1] = inode->i_ino;
	fh[2] = ((__u64)inode->i_ino) >> 32;

	*len = 3;
	return 1;
}

static const struct export_operations shmem_export_ops = {
	.get_parent     = shmem_get_parent,
	.encode_fh      = shmem_encode_fh,
	.fh_to_dentry	= shmem_fh_to_dentry,
};

static int shmem_parse_options(char *options, struct shmem_sb_info *sbinfo,
			       bool remount)
{
	char *this_char, *value, *rest;
	struct mempolicy *mpol = NULL;
	uid_t uid;
	gid_t gid;

	while (options != NULL) {
		this_char = options;
		for (;;) {
			/*
			 * NUL-terminate this option: unfortunately,
			 * mount options form a comma-separated list,
			 * but mpol's nodelist may also contain commas.
			 */
			options = strchr(options, ',');
			if (options == NULL)
				break;
			options++;
			if (!isdigit(*options)) {
				options[-1] = '\0';
				break;
			}
		}
		if (!*this_char)
			continue;
		if ((value = strchr(this_char,'=')) != NULL) {
			*value++ = 0;
		} else {
			pr_err("tmpfs: No value for mount option '%s'\n",
			       this_char);
			goto error;
		}

		if (!strcmp(this_char,"size")) {
			unsigned long long size;
			size = memparse(value,&rest);
			if (*rest == '%') {
				size <<= PAGE_SHIFT;
				size *= totalram_pages;
				do_div(size, 100);
				rest++;
			}
			if (*rest)
				goto bad_val;
			sbinfo->max_blocks =
				DIV_ROUND_UP(size, PAGE_CACHE_SIZE);
		} else if (!strcmp(this_char,"nr_blocks")) {
			sbinfo->max_blocks = memparse(value, &rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"nr_inodes")) {
			sbinfo->max_inodes = memparse(value, &rest);
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"mode")) {
			if (remount)
				continue;
			sbinfo->mode = simple_strtoul(value, &rest, 8) & 07777;
			if (*rest)
				goto bad_val;
		} else if (!strcmp(this_char,"uid")) {
			if (remount)
				continue;
			uid = simple_strtoul(value, &rest, 0);
			if (*rest)
				goto bad_val;
			sbinfo->uid = make_kuid(current_user_ns(), uid);
			if (!uid_valid(sbinfo->uid))
				goto bad_val;
		} else if (!strcmp(this_char,"gid")) {
			if (remount)
				continue;
			gid = simple_strtoul(value, &rest, 0);
			if (*rest)
				goto bad_val;
			sbinfo->gid = make_kgid(current_user_ns(), gid);
			if (!gid_valid(sbinfo->gid))
				goto bad_val;
		} else if (!strcmp(this_char,"mpol")) {
			mpol_put(mpol);
			mpol = NULL;
			if (mpol_parse_str(value, &mpol))
				goto bad_val;
		} else {
			pr_err("tmpfs: Bad mount option %s\n", this_char);
			goto error;
		}
	}
	sbinfo->mpol = mpol;
	return 0;

bad_val:
	pr_err("tmpfs: Bad value '%s' for mount option '%s'\n",
	       value, this_char);
error:
	mpol_put(mpol);
	return 1;

}

static int shmem_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	struct shmem_sb_info config = *sbinfo;
	unsigned long inodes;
	int error = -EINVAL;

	config.mpol = NULL;
	if (shmem_parse_options(data, &config, true))
		return error;

	spin_lock(&sbinfo->stat_lock);
	inodes = sbinfo->max_inodes - sbinfo->free_inodes;
	if (percpu_counter_compare(&sbinfo->used_blocks, config.max_blocks) > 0)
		goto out;
	if (config.max_inodes < inodes)
		goto out;
	/*
	 * Those tests disallow limited->unlimited while any are in use;
	 * but we must separately disallow unlimited->limited, because
	 * in that case we have no record of how much is already in use.
	 */
	if (config.max_blocks && !sbinfo->max_blocks)
		goto out;
	if (config.max_inodes && !sbinfo->max_inodes)
		goto out;

	error = 0;
	sbinfo->max_blocks  = config.max_blocks;
	sbinfo->max_inodes  = config.max_inodes;
	sbinfo->free_inodes = config.max_inodes - inodes;

	/*
	 * Preserve previous mempolicy unless mpol remount option was specified.
	 */
	if (config.mpol) {
		mpol_put(sbinfo->mpol);
		sbinfo->mpol = config.mpol;	/* transfers initial ref */
	}
out:
	spin_unlock(&sbinfo->stat_lock);
	return error;
}

static int shmem_show_options(struct seq_file *seq, struct dentry *root)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(root->d_sb);

	if (sbinfo->max_blocks != shmem_default_max_blocks())
		seq_printf(seq, ",size=%luk",
			sbinfo->max_blocks << (PAGE_CACHE_SHIFT - 10));
	if (sbinfo->max_inodes != shmem_default_max_inodes())
		seq_printf(seq, ",nr_inodes=%lu", sbinfo->max_inodes);
	if (sbinfo->mode != (S_IRWXUGO | S_ISVTX))
		seq_printf(seq, ",mode=%03ho", sbinfo->mode);
	if (!uid_eq(sbinfo->uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",uid=%u",
				from_kuid_munged(&init_user_ns, sbinfo->uid));
	if (!gid_eq(sbinfo->gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u",
				from_kgid_munged(&init_user_ns, sbinfo->gid));
	shmem_show_mpol(seq, sbinfo->mpol);
	return 0;
}

#define MFD_NAME_PREFIX "memfd:"
#define MFD_NAME_PREFIX_LEN (sizeof(MFD_NAME_PREFIX) - 1)
#define MFD_NAME_MAX_LEN (NAME_MAX - MFD_NAME_PREFIX_LEN)

#define MFD_ALL_FLAGS (MFD_CLOEXEC | MFD_ALLOW_SEALING)

SYSCALL_DEFINE2(memfd_create,
		const char __user *, uname,
		unsigned int, flags)
{
	struct shmem_inode_info *info;
	struct file *file;
	int fd, error;
	char *name;
	long len;

	if (flags & ~(unsigned int)MFD_ALL_FLAGS)
		return -EINVAL;

	/* length includes terminating zero */
	len = strnlen_user(uname, MFD_NAME_MAX_LEN + 1);
	if (len <= 0)
		return -EFAULT;
	if (len > MFD_NAME_MAX_LEN + 1)
		return -EINVAL;

	name = kmalloc(len + MFD_NAME_PREFIX_LEN, GFP_TEMPORARY);
	if (!name)
		return -ENOMEM;

	strcpy(name, MFD_NAME_PREFIX);
	if (copy_from_user(&name[MFD_NAME_PREFIX_LEN], uname, len)) {
		error = -EFAULT;
		goto err_name;
	}

	/* terminating-zero may have changed after strnlen_user() returned */
	if (name[len + MFD_NAME_PREFIX_LEN - 1]) {
		error = -EFAULT;
		goto err_name;
	}

	fd = get_unused_fd_flags((flags & MFD_CLOEXEC) ? O_CLOEXEC : 0);
	if (fd < 0) {
		error = fd;
		goto err_name;
	}

	file = shmem_file_setup(name, 0, VM_NORESERVE);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		goto err_fd;
	}
	info = SHMEM_I(file_inode(file));
	file->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
	file->f_flags |= O_RDWR | O_LARGEFILE;
	if (flags & MFD_ALLOW_SEALING)
		info->seals &= ~F_SEAL_SEAL;

	fd_install(fd, file);
	kfree(name);
	return fd;

err_fd:
	put_unused_fd(fd);
err_name:
	kfree(name);
	return error;
}

#endif /* CONFIG_TMPFS */

static void shmem_put_super(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	percpu_counter_destroy(&sbinfo->used_blocks);
	mpol_put(sbinfo->mpol);
	kfree(sbinfo);
	sb->s_fs_info = NULL;
}

int shmem_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct shmem_sb_info *sbinfo;
	int err = -ENOMEM;

	/* Round up to L1_CACHE_BYTES to resist false sharing */
	sbinfo = kzalloc(max((int)sizeof(struct shmem_sb_info),
				L1_CACHE_BYTES), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;

	sbinfo->mode = S_IRWXUGO | S_ISVTX;
	sbinfo->uid = current_fsuid();
	sbinfo->gid = current_fsgid();
	sb->s_fs_info = sbinfo;

#ifdef CONFIG_TMPFS
	/*
	 * Per default we only allow half of the physical ram per
	 * tmpfs instance, limiting inodes to one per page of lowmem;
	 * but the internal instance is left unlimited.
	 */
	if (!(sb->s_flags & MS_KERNMOUNT)) {
		sbinfo->max_blocks = shmem_default_max_blocks();
		sbinfo->max_inodes = shmem_default_max_inodes();
		if (shmem_parse_options(data, sbinfo, false)) {
			err = -EINVAL;
			goto failed;
		}
	} else {
		sb->s_flags |= MS_NOUSER;
	}
	sb->s_export_op = &shmem_export_ops;
	sb->s_flags |= MS_NOSEC;
#else
	sb->s_flags |= MS_NOUSER;
#endif

	spin_lock_init(&sbinfo->stat_lock);
	if (percpu_counter_init(&sbinfo->used_blocks, 0, GFP_KERNEL))
		goto failed;
	sbinfo->free_inodes = sbinfo->max_inodes;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = TMPFS_MAGIC;
	sb->s_op = &shmem_ops;
	sb->s_time_gran = 1;
#ifdef CONFIG_TMPFS_XATTR
	sb->s_xattr = shmem_xattr_handlers;
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	sb->s_flags |= MS_POSIXACL;
#endif

	inode = shmem_get_inode(sb, NULL, S_IFDIR | sbinfo->mode, 0, VM_NORESERVE);
	if (!inode)
		goto failed;
	inode->i_uid = sbinfo->uid;
	inode->i_gid = sbinfo->gid;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto failed;
	return 0;

failed:
	shmem_put_super(sb);
	return err;
}

static struct kmem_cache *shmem_inode_cachep;

static struct inode *shmem_alloc_inode(struct super_block *sb)
{
	struct shmem_inode_info *info;
	info = kmem_cache_alloc(shmem_inode_cachep, GFP_KERNEL);
	if (!info)
		return NULL;
	return &info->vfs_inode;
}

static void shmem_destroy_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kfree(inode->i_link);
	kmem_cache_free(shmem_inode_cachep, SHMEM_I(inode));
}

static void shmem_destroy_inode(struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		mpol_free_shared_policy(&SHMEM_I(inode)->policy);
	call_rcu(&inode->i_rcu, shmem_destroy_callback);
}

static void shmem_init_inode(void *foo)
{
	struct shmem_inode_info *info = foo;
	inode_init_once(&info->vfs_inode);
}

static int shmem_init_inodecache(void)
{
	shmem_inode_cachep = kmem_cache_create("shmem_inode_cache",
				sizeof(struct shmem_inode_info),
				0, SLAB_PANIC|SLAB_ACCOUNT, shmem_init_inode);
	return 0;
}

static void shmem_destroy_inodecache(void)
{
	kmem_cache_destroy(shmem_inode_cachep);
}

static const struct address_space_operations shmem_aops = {
	.writepage	= shmem_writepage,
	.set_page_dirty	= __set_page_dirty_no_writeback,
#ifdef CONFIG_TMPFS
	.write_begin	= shmem_write_begin,
	.write_end	= shmem_write_end,
#endif
#ifdef CONFIG_MIGRATION
	.migratepage	= migrate_page,
#endif
	.error_remove_page = generic_error_remove_page,
};

static const struct file_operations shmem_file_operations = {
	.mmap		= shmem_mmap,
#ifdef CONFIG_TMPFS
	.llseek		= shmem_file_llseek,
	.read_iter	= shmem_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.fsync		= noop_fsync,
	.splice_read	= shmem_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= shmem_fallocate,
#endif
};

static const struct inode_operations shmem_inode_operations = {
	.getattr	= shmem_getattr,
	.setattr	= shmem_setattr,
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= generic_removexattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_dir_inode_operations = {
#ifdef CONFIG_TMPFS
	.create		= shmem_create,
	.lookup		= simple_lookup,
	.link		= shmem_link,
	.unlink		= shmem_unlink,
	.symlink	= shmem_symlink,
	.mkdir		= shmem_mkdir,
	.rmdir		= shmem_rmdir,
	.mknod		= shmem_mknod,
	.rename2	= shmem_rename2,
	.tmpfile	= shmem_tmpfile,
#endif
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= generic_removexattr,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_special_inode_operations = {
#ifdef CONFIG_TMPFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= shmem_listxattr,
	.removexattr	= generic_removexattr,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct super_operations shmem_ops = {
	.alloc_inode	= shmem_alloc_inode,
	.destroy_inode	= shmem_destroy_inode,
#ifdef CONFIG_TMPFS
	.statfs		= shmem_statfs,
	.remount_fs	= shmem_remount_fs,
	.show_options	= shmem_show_options,
#endif
	.evict_inode	= shmem_evict_inode,
	.drop_inode	= generic_delete_inode,
	.put_super	= shmem_put_super,
};

static const struct vm_operations_struct shmem_vm_ops = {
	.fault		= shmem_fault,
	.map_pages	= filemap_map_pages,
#ifdef CONFIG_NUMA
	.set_policy     = shmem_set_policy,
	.get_policy     = shmem_get_policy,
#endif
};

static struct dentry *shmem_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, shmem_fill_super);
}

static struct file_system_type shmem_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tmpfs",
	.mount		= shmem_mount,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

int __init shmem_init(void)
{
	int error;

	/* If rootfs called this, don't re-init */
	if (shmem_inode_cachep)
		return 0;

	error = shmem_init_inodecache();
	if (error)
		goto out3;

	error = register_filesystem(&shmem_fs_type);
	if (error) {
		pr_err("Could not register tmpfs\n");
		goto out2;
	}

	shm_mnt = kern_mount(&shmem_fs_type);
	if (IS_ERR(shm_mnt)) {
		error = PTR_ERR(shm_mnt);
		pr_err("Could not kern_mount tmpfs\n");
		goto out1;
	}
	return 0;

out1:
	unregister_filesystem(&shmem_fs_type);
out2:
	shmem_destroy_inodecache();
out3:
	shm_mnt = ERR_PTR(error);
	return error;
}

#else /* !CONFIG_SHMEM */

/*
 * tiny-shmem: simple shmemfs and tmpfs using ramfs code
 *
 * This is intended for small system where the benefits of the full
 * shmem code (swap-backed and resource-limited) are outweighed by
 * their complexity. On systems without swap this code should be
 * effectively equivalent, but much lighter weight.
 */

static struct file_system_type shmem_fs_type = {
	.name		= "tmpfs",
	.mount		= ramfs_mount,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

int __init shmem_init(void)
{
	BUG_ON(register_filesystem(&shmem_fs_type) != 0);

	shm_mnt = kern_mount(&shmem_fs_type);
	BUG_ON(IS_ERR(shm_mnt));

	return 0;
}

int shmem_unuse(swp_entry_t swap, struct page *page)
{
	return 0;
}

int shmem_lock(struct file *file, int lock, struct user_struct *user)
{
	return 0;
}

void shmem_unlock_mapping(struct address_space *mapping)
{
}

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	truncate_inode_pages_range(inode->i_mapping, lstart, lend);
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

#define shmem_vm_ops				generic_file_vm_ops
#define shmem_file_operations			ramfs_file_operations
#define shmem_get_inode(sb, dir, mode, dev, flags)	ramfs_get_inode(sb, dir, mode, dev)
#define shmem_acct_size(flags, size)		0
#define shmem_unacct_size(flags, size)		do {} while (0)

#endif /* CONFIG_SHMEM */

/* common code */

static struct dentry_operations anon_ops = {
	.d_dname = simple_dname
};

static struct file *__shmem_file_setup(const char *name, loff_t size,
				       unsigned long flags, unsigned int i_flags)
{
	struct file *res;
	struct inode *inode;
	struct path path;
	struct super_block *sb;
	struct qstr this;

	if (IS_ERR(shm_mnt))
		return ERR_CAST(shm_mnt);

	if (size < 0 || size > MAX_LFS_FILESIZE)
		return ERR_PTR(-EINVAL);

	if (shmem_acct_size(flags, size))
		return ERR_PTR(-ENOMEM);

	res = ERR_PTR(-ENOMEM);
	this.name = name;
	this.len = strlen(name);
	this.hash = 0; /* will go */
	sb = shm_mnt->mnt_sb;
	path.mnt = mntget(shm_mnt);
	path.dentry = d_alloc_pseudo(sb, &this);
	if (!path.dentry)
		goto put_memory;
	d_set_d_op(path.dentry, &anon_ops);

	res = ERR_PTR(-ENOSPC);
	inode = shmem_get_inode(sb, NULL, S_IFREG | S_IRWXUGO, 0, flags);
	if (!inode)
		goto put_memory;

	inode->i_flags |= i_flags;
	d_instantiate(path.dentry, inode);
	inode->i_size = size;
	clear_nlink(inode);	/* It is unlinked */
	res = ERR_PTR(ramfs_nommu_expand_for_mapping(inode, size));
	if (IS_ERR(res))
		goto put_path;

	res = alloc_file(&path, FMODE_WRITE | FMODE_READ,
		  &shmem_file_operations);
	if (IS_ERR(res))
		goto put_path;

	return res;

put_memory:
	shmem_unacct_size(flags, size);
put_path:
	path_put(&path);
	return res;
}

/**
 * shmem_kernel_file_setup - get an unlinked file living in tmpfs which must be
 * 	kernel internal.  There will be NO LSM permission checks against the
 * 	underlying inode.  So users of this interface must do LSM checks at a
 *	higher layer.  The users are the big_key and shm implementations.  LSM
 *	checks are provided at the key or shm level rather than the inode.
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_kernel_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return __shmem_file_setup(name, size, flags, S_PRIVATE);
}

/**
 * shmem_file_setup - get an unlinked file living in tmpfs
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return __shmem_file_setup(name, size, flags, 0);
}
EXPORT_SYMBOL_GPL(shmem_file_setup);

/**
 * shmem_zero_setup - setup a shared anonymous mapping
 * @vma: the vma to be mmapped is prepared by do_mmap_pgoff
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;

	/*
	 * Cloning a new file under mmap_sem leads to a lock ordering conflict
	 * between XFS directory reading and selinux: since this file is only
	 * accessible to the user through its mapping, use S_PRIVATE flag to
	 * bypass file security, in the same way as shmem_kernel_file_setup().
	 */
	file = __shmem_file_setup("dev/zero", size, vma->vm_flags, S_PRIVATE);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_vm_ops;
	return 0;
}

/**
 * shmem_read_mapping_page_gfp - read into page cache, using specified page allocation flags.
 * @mapping:	the page's address_space
 * @index:	the page index
 * @gfp:	the page allocator flags to use if allocating
 *
 * This behaves as a tmpfs "read_cache_page_gfp(mapping, index, gfp)",
 * with any new page allocations done using the specified allocation flags.
 * But read_cache_page_gfp() uses the ->readpage() method: which does not
 * suit tmpfs, since it may have pages in swapcache, and needs to find those
 * for itself; although drivers/gpu/drm i915 and ttm rely upon this support.
 *
 * i915_gem_object_get_pages_gtt() mixes __GFP_NORETRY | __GFP_NOWARN in
 * with the mapping_gfp_mask(), to avoid OOMing the machine unnecessarily.
 */
struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					 pgoff_t index, gfp_t gfp)
{
#ifdef CONFIG_SHMEM
	struct inode *inode = mapping->host;
	struct page *page;
	int error;

	BUG_ON(mapping->a_ops != &shmem_aops);
	error = shmem_getpage_gfp(inode, index, &page, SGP_CACHE, gfp, NULL);
	if (error)
		page = ERR_PTR(error);
	else
		unlock_page(page);
	return page;
#else
	/*
	 * The tiny !SHMEM case uses ramfs without swap
	 */
	return read_cache_page_gfp(mapping, index, gfp);
#endif
}
EXPORT_SYMBOL_GPL(shmem_read_mapping_page_gfp);
