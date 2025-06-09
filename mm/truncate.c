// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/truncate.c - code for taking down pages from address_spaces
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 10Sep2002	Andrew Morton
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/backing-dev.h>
#include <linux/dax.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/export.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/pagevec.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/shmem_fs.h>
#include <linux/rmap.h>
#include "internal.h"

static void clear_shadow_entries(struct address_space *mapping,
				 unsigned long start, unsigned long max)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct folio *folio;

	/* Handled by shmem itself, or for DAX we do nothing. */
	if (shmem_mapping(mapping) || dax_mapping(mapping))
		return;

	xas_set_update(&xas, workingset_update_node);

	spin_lock(&mapping->host->i_lock);
	xas_lock_irq(&xas);

	/* Clear all shadow entries from start to max */
	xas_for_each(&xas, folio, max) {
		if (xa_is_value(folio))
			xas_store(&xas, NULL);
	}

	xas_unlock_irq(&xas);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);
}

/*
 * Unconditionally remove exceptional entries. Usually called from truncate
 * path. Note that the folio_batch may be altered by this function by removing
 * exceptional entries similar to what folio_batch_remove_exceptionals() does.
 * Please note that indices[] has entries in ascending order as guaranteed by
 * either find_get_entries() or find_lock_entries().
 */
static void truncate_folio_batch_exceptionals(struct address_space *mapping,
				struct folio_batch *fbatch, pgoff_t *indices)
{
	XA_STATE(xas, &mapping->i_pages, indices[0]);
	int nr = folio_batch_count(fbatch);
	struct folio *folio;
	int i, j;

	/* Handled by shmem itself */
	if (shmem_mapping(mapping))
		return;

	for (j = 0; j < nr; j++)
		if (xa_is_value(fbatch->folios[j]))
			break;

	if (j == nr)
		return;

	if (dax_mapping(mapping)) {
		for (i = j; i < nr; i++) {
			if (xa_is_value(fbatch->folios[i])) {
				/*
				 * File systems should already have called
				 * dax_break_layout_entry() to remove all DAX
				 * entries while holding a lock to prevent
				 * establishing new entries. Therefore we
				 * shouldn't find any here.
				 */
				WARN_ON_ONCE(1);

				/*
				 * Delete the mapping so truncate_pagecache()
				 * doesn't loop forever.
				 */
				dax_delete_mapping_entry(mapping, indices[i]);
			}
		}
		goto out;
	}

	xas_set(&xas, indices[j]);
	xas_set_update(&xas, workingset_update_node);

	spin_lock(&mapping->host->i_lock);
	xas_lock_irq(&xas);

	xas_for_each(&xas, folio, indices[nr-1]) {
		if (xa_is_value(folio))
			xas_store(&xas, NULL);
	}

	xas_unlock_irq(&xas);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);
out:
	folio_batch_remove_exceptionals(fbatch);
}

/**
 * folio_invalidate - Invalidate part or all of a folio.
 * @folio: The folio which is affected.
 * @offset: start of the range to invalidate
 * @length: length of the range to invalidate
 *
 * folio_invalidate() is called when all or part of the folio has become
 * invalidated by a truncate operation.
 *
 * folio_invalidate() does not have to release all buffers, but it must
 * ensure that no dirty buffer is left outside @offset and that no I/O
 * is underway against any of the blocks which are outside the truncation
 * point.  Because the caller is about to free (and possibly reuse) those
 * blocks on-disk.
 */
void folio_invalidate(struct folio *folio, size_t offset, size_t length)
{
	const struct address_space_operations *aops = folio->mapping->a_ops;

	if (aops->invalidate_folio)
		aops->invalidate_folio(folio, offset, length);
}
EXPORT_SYMBOL_GPL(folio_invalidate);

/*
 * If truncate cannot remove the fs-private metadata from the page, the page
 * becomes orphaned.  It will be left on the LRU and may even be mapped into
 * user pagetables if we're racing with filemap_fault().
 *
 * We need to bail out if page->mapping is no longer equal to the original
 * mapping.  This happens a) when the VM reclaimed the page while we waited on
 * its lock, b) when a concurrent invalidate_mapping_pages got there first and
 * c) when tmpfs swizzles a page between a tmpfs inode and swapper_space.
 */
static void truncate_cleanup_folio(struct folio *folio)
{
	if (folio_mapped(folio))
		unmap_mapping_folio(folio);

	if (folio_needs_release(folio))
		folio_invalidate(folio, 0, folio_size(folio));

	/*
	 * Some filesystems seem to re-dirty the page even after
	 * the VM has canceled the dirty bit (eg ext3 journaling).
	 * Hence dirty accounting check is placed after invalidation.
	 */
	folio_cancel_dirty(folio);
}

int truncate_inode_folio(struct address_space *mapping, struct folio *folio)
{
	if (folio->mapping != mapping)
		return -EIO;

	truncate_cleanup_folio(folio);
	filemap_remove_folio(folio);
	return 0;
}

/*
 * Handle partial folios.  The folio may be entirely within the
 * range if a split has raced with us.  If not, we zero the part of the
 * folio that's within the [start, end] range, and then split the folio if
 * it's large.  split_page_range() will discard pages which now lie beyond
 * i_size, and we rely on the caller to discard pages which lie within a
 * newly created hole.
 *
 * Returns false if splitting failed so the caller can avoid
 * discarding the entire folio which is stubbornly unsplit.
 */
bool truncate_inode_partial_folio(struct folio *folio, loff_t start, loff_t end)
{
	loff_t pos = folio_pos(folio);
	unsigned int offset, length;
	struct page *split_at, *split_at2;

	if (pos < start)
		offset = start - pos;
	else
		offset = 0;
	length = folio_size(folio);
	if (pos + length <= (u64)end)
		length = length - offset;
	else
		length = end + 1 - pos - offset;

	folio_wait_writeback(folio);
	if (length == folio_size(folio)) {
		truncate_inode_folio(folio->mapping, folio);
		return true;
	}

	/*
	 * We may be zeroing pages we're about to discard, but it avoids
	 * doing a complex calculation here, and then doing the zeroing
	 * anyway if the page split fails.
	 */
	if (!mapping_inaccessible(folio->mapping))
		folio_zero_range(folio, offset, length);

	if (folio_needs_release(folio))
		folio_invalidate(folio, offset, length);
	if (!folio_test_large(folio))
		return true;

	split_at = folio_page(folio, PAGE_ALIGN_DOWN(offset) / PAGE_SIZE);
	split_at2 = folio_page(folio,
			PAGE_ALIGN_DOWN(offset + length) / PAGE_SIZE);

	if (!try_folio_split(folio, split_at, NULL)) {
		/*
		 * try to split at offset + length to make sure folios within
		 * the range can be dropped, especially to avoid memory waste
		 * for shmem truncate
		 */
		struct folio *folio2 = page_folio(split_at2);

		if (!folio_try_get(folio2))
			goto no_split;

		if (!folio_test_large(folio2))
			goto out;

		if (!folio_trylock(folio2))
			goto out;

		/*
		 * make sure folio2 is large and does not change its mapping.
		 * Its split result does not matter here.
		 */
		if (folio_test_large(folio2) &&
		    folio2->mapping == folio->mapping)
			try_folio_split(folio2, split_at2, NULL);

		folio_unlock(folio2);
out:
		folio_put(folio2);
no_split:
		return true;
	}
	if (folio_test_dirty(folio))
		return false;
	truncate_inode_folio(folio->mapping, folio);
	return true;
}

/*
 * Used to get rid of pages on hardware memory corruption.
 */
int generic_error_remove_folio(struct address_space *mapping,
		struct folio *folio)
{
	if (!mapping)
		return -EINVAL;
	/*
	 * Only punch for normal data pages for now.
	 * Handling other types like directories would need more auditing.
	 */
	if (!S_ISREG(mapping->host->i_mode))
		return -EIO;
	return truncate_inode_folio(mapping, folio);
}
EXPORT_SYMBOL(generic_error_remove_folio);

/**
 * mapping_evict_folio() - Remove an unused folio from the page-cache.
 * @mapping: The mapping this folio belongs to.
 * @folio: The folio to remove.
 *
 * Safely remove one folio from the page cache.
 * It only drops clean, unused folios.
 *
 * Context: Folio must be locked.
 * Return: The number of pages successfully removed.
 */
long mapping_evict_folio(struct address_space *mapping, struct folio *folio)
{
	/* The page may have been truncated before it was locked */
	if (!mapping)
		return 0;
	if (folio_test_dirty(folio) || folio_test_writeback(folio))
		return 0;
	/* The refcount will be elevated if any page in the folio is mapped */
	if (folio_ref_count(folio) >
			folio_nr_pages(folio) + folio_has_private(folio) + 1)
		return 0;
	if (!filemap_release_folio(folio, 0))
		return 0;

	return remove_mapping(mapping, folio);
}

/**
 * truncate_inode_pages_range - truncate range of pages specified by start & end byte offsets
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 * @lend: offset to which to truncate (inclusive)
 *
 * Truncate the page cache, removing the pages that are between
 * specified offsets (and zeroing out partial pages
 * if lstart or lend + 1 is not page aligned).
 *
 * Truncate takes two passes - the first pass is nonblocking.  It will not
 * block on page locks and it will not block on writeback.  The second pass
 * will wait.  This is to prevent as much IO as possible in the affected region.
 * The first pass will remove most pages, so the search cost of the second pass
 * is low.
 *
 * We pass down the cache-hot hint to the page freeing code.  Even if the
 * mapping is large, it is probably the case that the final pages are the most
 * recently touched, and freeing happens in ascending file offset order.
 *
 * Note that since ->invalidate_folio() accepts range to invalidate
 * truncate_inode_pages_range is able to handle cases where lend + 1 is not
 * page aligned properly.
 */
void truncate_inode_pages_range(struct address_space *mapping,
				loff_t lstart, loff_t lend)
{
	pgoff_t		start;		/* inclusive */
	pgoff_t		end;		/* exclusive */
	struct folio_batch fbatch;
	pgoff_t		indices[PAGEVEC_SIZE];
	pgoff_t		index;
	int		i;
	struct folio	*folio;
	bool		same_folio;

	if (mapping_empty(mapping))
		return;

	/*
	 * 'start' and 'end' always covers the range of pages to be fully
	 * truncated. Partial pages are covered with 'partial_start' at the
	 * start of the range and 'partial_end' at the end of the range.
	 * Note that 'end' is exclusive while 'lend' is inclusive.
	 */
	start = (lstart + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (lend == -1)
		/*
		 * lend == -1 indicates end-of-file so we have to set 'end'
		 * to the highest possible pgoff_t and since the type is
		 * unsigned we're using -1.
		 */
		end = -1;
	else
		end = (lend + 1) >> PAGE_SHIFT;

	folio_batch_init(&fbatch);
	index = start;
	while (index < end && find_lock_entries(mapping, &index, end - 1,
			&fbatch, indices)) {
		truncate_folio_batch_exceptionals(mapping, &fbatch, indices);
		for (i = 0; i < folio_batch_count(&fbatch); i++)
			truncate_cleanup_folio(fbatch.folios[i]);
		delete_from_page_cache_batch(mapping, &fbatch);
		for (i = 0; i < folio_batch_count(&fbatch); i++)
			folio_unlock(fbatch.folios[i]);
		folio_batch_release(&fbatch);
		cond_resched();
	}

	same_folio = (lstart >> PAGE_SHIFT) == (lend >> PAGE_SHIFT);
	folio = __filemap_get_folio(mapping, lstart >> PAGE_SHIFT, FGP_LOCK, 0);
	if (!IS_ERR(folio)) {
		same_folio = lend < folio_pos(folio) + folio_size(folio);
		if (!truncate_inode_partial_folio(folio, lstart, lend)) {
			start = folio_next_index(folio);
			if (same_folio)
				end = folio->index;
		}
		folio_unlock(folio);
		folio_put(folio);
		folio = NULL;
	}

	if (!same_folio) {
		folio = __filemap_get_folio(mapping, lend >> PAGE_SHIFT,
						FGP_LOCK, 0);
		if (!IS_ERR(folio)) {
			if (!truncate_inode_partial_folio(folio, lstart, lend))
				end = folio->index;
			folio_unlock(folio);
			folio_put(folio);
		}
	}

	index = start;
	while (index < end) {
		cond_resched();
		if (!find_get_entries(mapping, &index, end - 1, &fbatch,
				indices)) {
			/* If all gone from start onwards, we're done */
			if (index == start)
				break;
			/* Otherwise restart to make sure all gone */
			index = start;
			continue;
		}

		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			struct folio *folio = fbatch.folios[i];

			/* We rely upon deletion not changing page->index */

			if (xa_is_value(folio))
				continue;

			folio_lock(folio);
			VM_BUG_ON_FOLIO(!folio_contains(folio, indices[i]), folio);
			folio_wait_writeback(folio);
			truncate_inode_folio(mapping, folio);
			folio_unlock(folio);
		}
		truncate_folio_batch_exceptionals(mapping, &fbatch, indices);
		folio_batch_release(&fbatch);
	}
}
EXPORT_SYMBOL(truncate_inode_pages_range);

/**
 * truncate_inode_pages - truncate *all* the pages from an offset
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 *
 * Called under (and serialised by) inode->i_rwsem and
 * mapping->invalidate_lock.
 *
 * Note: When this function returns, there can be a page in the process of
 * deletion (inside __filemap_remove_folio()) in the specified range.  Thus
 * mapping->nrpages can be non-zero when this function returns even after
 * truncation of the whole mapping.
 */
void truncate_inode_pages(struct address_space *mapping, loff_t lstart)
{
	truncate_inode_pages_range(mapping, lstart, (loff_t)-1);
}
EXPORT_SYMBOL(truncate_inode_pages);

/**
 * truncate_inode_pages_final - truncate *all* pages before inode dies
 * @mapping: mapping to truncate
 *
 * Called under (and serialized by) inode->i_rwsem.
 *
 * Filesystems have to use this in the .evict_inode path to inform the
 * VM that this is the final truncate and the inode is going away.
 */
void truncate_inode_pages_final(struct address_space *mapping)
{
	/*
	 * Page reclaim can not participate in regular inode lifetime
	 * management (can't call iput()) and thus can race with the
	 * inode teardown.  Tell it when the address space is exiting,
	 * so that it does not install eviction information after the
	 * final truncate has begun.
	 */
	mapping_set_exiting(mapping);

	if (!mapping_empty(mapping)) {
		/*
		 * As truncation uses a lockless tree lookup, cycle
		 * the tree lock to make sure any ongoing tree
		 * modification that does not see AS_EXITING is
		 * completed before starting the final truncate.
		 */
		xa_lock_irq(&mapping->i_pages);
		xa_unlock_irq(&mapping->i_pages);
	}

	truncate_inode_pages(mapping, 0);
}
EXPORT_SYMBOL(truncate_inode_pages_final);

/**
 * mapping_try_invalidate - Invalidate all the evictable folios of one inode
 * @mapping: the address_space which holds the folios to invalidate
 * @start: the offset 'from' which to invalidate
 * @end: the offset 'to' which to invalidate (inclusive)
 * @nr_failed: How many folio invalidations failed
 *
 * This function is similar to invalidate_mapping_pages(), except that it
 * returns the number of folios which could not be evicted in @nr_failed.
 */
unsigned long mapping_try_invalidate(struct address_space *mapping,
		pgoff_t start, pgoff_t end, unsigned long *nr_failed)
{
	pgoff_t indices[PAGEVEC_SIZE];
	struct folio_batch fbatch;
	pgoff_t index = start;
	unsigned long ret;
	unsigned long count = 0;
	int i;

	folio_batch_init(&fbatch);
	while (find_lock_entries(mapping, &index, end, &fbatch, indices)) {
		bool xa_has_values = false;
		int nr = folio_batch_count(&fbatch);

		for (i = 0; i < nr; i++) {
			struct folio *folio = fbatch.folios[i];

			/* We rely upon deletion not changing folio->index */

			if (xa_is_value(folio)) {
				xa_has_values = true;
				count++;
				continue;
			}

			ret = mapping_evict_folio(mapping, folio);
			folio_unlock(folio);
			/*
			 * Invalidation is a hint that the folio is no longer
			 * of interest and try to speed up its reclaim.
			 */
			if (!ret) {
				deactivate_file_folio(folio);
				/* Likely in the lru cache of a remote CPU */
				if (nr_failed)
					(*nr_failed)++;
			}
			count += ret;
		}

		if (xa_has_values)
			clear_shadow_entries(mapping, indices[0], indices[nr-1]);

		folio_batch_remove_exceptionals(&fbatch);
		folio_batch_release(&fbatch);
		cond_resched();
	}
	return count;
}

/**
 * invalidate_mapping_pages - Invalidate all clean, unlocked cache of one inode
 * @mapping: the address_space which holds the cache to invalidate
 * @start: the offset 'from' which to invalidate
 * @end: the offset 'to' which to invalidate (inclusive)
 *
 * This function removes pages that are clean, unmapped and unlocked,
 * as well as shadow entries. It will not block on IO activity.
 *
 * If you want to remove all the pages of one inode, regardless of
 * their use and writeback state, use truncate_inode_pages().
 *
 * Return: The number of indices that had their contents invalidated
 */
unsigned long invalidate_mapping_pages(struct address_space *mapping,
		pgoff_t start, pgoff_t end)
{
	return mapping_try_invalidate(mapping, start, end, NULL);
}
EXPORT_SYMBOL(invalidate_mapping_pages);

static int folio_launder(struct address_space *mapping, struct folio *folio)
{
	if (!folio_test_dirty(folio))
		return 0;
	if (folio->mapping != mapping || mapping->a_ops->launder_folio == NULL)
		return 0;
	return mapping->a_ops->launder_folio(folio);
}

/*
 * This is like mapping_evict_folio(), except it ignores the folio's
 * refcount.  We do this because invalidate_inode_pages2() needs stronger
 * invalidation guarantees, and cannot afford to leave folios behind because
 * shrink_folio_list() has a temp ref on them, or because they're transiently
 * sitting in the folio_add_lru() caches.
 */
int folio_unmap_invalidate(struct address_space *mapping, struct folio *folio,
			   gfp_t gfp)
{
	int ret;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);

	if (folio_mapped(folio))
		unmap_mapping_folio(folio);
	BUG_ON(folio_mapped(folio));

	ret = folio_launder(mapping, folio);
	if (ret)
		return ret;
	if (folio->mapping != mapping)
		return -EBUSY;
	if (!filemap_release_folio(folio, gfp))
		return -EBUSY;

	spin_lock(&mapping->host->i_lock);
	xa_lock_irq(&mapping->i_pages);
	if (folio_test_dirty(folio))
		goto failed;

	BUG_ON(folio_has_private(folio));
	__filemap_remove_folio(folio, NULL);
	xa_unlock_irq(&mapping->i_pages);
	if (mapping_shrinkable(mapping))
		inode_add_lru(mapping->host);
	spin_unlock(&mapping->host->i_lock);

	filemap_free_folio(mapping, folio);
	return 1;
failed:
	xa_unlock_irq(&mapping->i_pages);
	spin_unlock(&mapping->host->i_lock);
	return -EBUSY;
}

/**
 * invalidate_inode_pages2_range - remove range of pages from an address_space
 * @mapping: the address_space
 * @start: the page offset 'from' which to invalidate
 * @end: the page offset 'to' which to invalidate (inclusive)
 *
 * Any pages which are found to be mapped into pagetables are unmapped prior to
 * invalidation.
 *
 * Return: -EBUSY if any pages could not be invalidated.
 */
int invalidate_inode_pages2_range(struct address_space *mapping,
				  pgoff_t start, pgoff_t end)
{
	pgoff_t indices[PAGEVEC_SIZE];
	struct folio_batch fbatch;
	pgoff_t index;
	int i;
	int ret = 0;
	int ret2 = 0;
	int did_range_unmap = 0;

	if (mapping_empty(mapping))
		return 0;

	folio_batch_init(&fbatch);
	index = start;
	while (find_get_entries(mapping, &index, end, &fbatch, indices)) {
		bool xa_has_values = false;
		int nr = folio_batch_count(&fbatch);

		for (i = 0; i < nr; i++) {
			struct folio *folio = fbatch.folios[i];

			/* We rely upon deletion not changing folio->index */

			if (xa_is_value(folio)) {
				xa_has_values = true;
				if (dax_mapping(mapping) &&
				    !dax_invalidate_mapping_entry_sync(mapping, indices[i]))
					ret = -EBUSY;
				continue;
			}

			if (!did_range_unmap && folio_mapped(folio)) {
				/*
				 * If folio is mapped, before taking its lock,
				 * zap the rest of the file in one hit.
				 */
				unmap_mapping_pages(mapping, indices[i],
						(1 + end - indices[i]), false);
				did_range_unmap = 1;
			}

			folio_lock(folio);
			if (unlikely(folio->mapping != mapping)) {
				folio_unlock(folio);
				continue;
			}
			VM_BUG_ON_FOLIO(!folio_contains(folio, indices[i]), folio);
			folio_wait_writeback(folio);
			ret2 = folio_unmap_invalidate(mapping, folio, GFP_KERNEL);
			if (ret2 < 0)
				ret = ret2;
			folio_unlock(folio);
		}

		if (xa_has_values)
			clear_shadow_entries(mapping, indices[0], indices[nr-1]);

		folio_batch_remove_exceptionals(&fbatch);
		folio_batch_release(&fbatch);
		cond_resched();
	}
	/*
	 * For DAX we invalidate page tables after invalidating page cache.  We
	 * could invalidate page tables while invalidating each entry however
	 * that would be expensive. And doing range unmapping before doesn't
	 * work as we have no cheap way to find whether page cache entry didn't
	 * get remapped later.
	 */
	if (dax_mapping(mapping)) {
		unmap_mapping_pages(mapping, start, end - start + 1, false);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(invalidate_inode_pages2_range);

/**
 * invalidate_inode_pages2 - remove all pages from an address_space
 * @mapping: the address_space
 *
 * Any pages which are found to be mapped into pagetables are unmapped prior to
 * invalidation.
 *
 * Return: -EBUSY if any pages could not be invalidated.
 */
int invalidate_inode_pages2(struct address_space *mapping)
{
	return invalidate_inode_pages2_range(mapping, 0, -1);
}
EXPORT_SYMBOL_GPL(invalidate_inode_pages2);

/**
 * truncate_pagecache - unmap and remove pagecache that has been truncated
 * @inode: inode
 * @newsize: new file size
 *
 * inode's new i_size must already be written before truncate_pagecache
 * is called.
 *
 * This function should typically be called before the filesystem
 * releases resources associated with the freed range (eg. deallocates
 * blocks). This way, pagecache will always stay logically coherent
 * with on-disk format, and the filesystem would not have to deal with
 * situations such as writepage being called for a page that has already
 * had its underlying blocks deallocated.
 */
void truncate_pagecache(struct inode *inode, loff_t newsize)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t holebegin = round_up(newsize, PAGE_SIZE);

	/*
	 * unmap_mapping_range is called twice, first simply for
	 * efficiency so that truncate_inode_pages does fewer
	 * single-page unmaps.  However after this first call, and
	 * before truncate_inode_pages finishes, it is possible for
	 * private pages to be COWed, which remain after
	 * truncate_inode_pages finishes, hence the second
	 * unmap_mapping_range call must be made for correctness.
	 */
	unmap_mapping_range(mapping, holebegin, 0, 1);
	truncate_inode_pages(mapping, newsize);
	unmap_mapping_range(mapping, holebegin, 0, 1);
}
EXPORT_SYMBOL(truncate_pagecache);

/**
 * truncate_setsize - update inode and pagecache for a new file size
 * @inode: inode
 * @newsize: new file size
 *
 * truncate_setsize updates i_size and performs pagecache truncation (if
 * necessary) to @newsize. It will be typically be called from the filesystem's
 * setattr function when ATTR_SIZE is passed in.
 *
 * Must be called with a lock serializing truncates and writes (generally
 * i_rwsem but e.g. xfs uses a different lock) and before all filesystem
 * specific block truncation has been performed.
 */
void truncate_setsize(struct inode *inode, loff_t newsize)
{
	loff_t oldsize = inode->i_size;

	i_size_write(inode, newsize);
	if (newsize > oldsize)
		pagecache_isize_extended(inode, oldsize, newsize);
	truncate_pagecache(inode, newsize);
}
EXPORT_SYMBOL(truncate_setsize);

/**
 * pagecache_isize_extended - update pagecache after extension of i_size
 * @inode:	inode for which i_size was extended
 * @from:	original inode size
 * @to:		new inode size
 *
 * Handle extension of inode size either caused by extending truncate or
 * by write starting after current i_size.  We mark the page straddling
 * current i_size RO so that page_mkwrite() is called on the first
 * write access to the page.  The filesystem will update its per-block
 * information before user writes to the page via mmap after the i_size
 * has been changed.
 *
 * The function must be called after i_size is updated so that page fault
 * coming after we unlock the folio will already see the new i_size.
 * The function must be called while we still hold i_rwsem - this not only
 * makes sure i_size is stable but also that userspace cannot observe new
 * i_size value before we are prepared to store mmap writes at new inode size.
 */
void pagecache_isize_extended(struct inode *inode, loff_t from, loff_t to)
{
	int bsize = i_blocksize(inode);
	loff_t rounded_from;
	struct folio *folio;

	WARN_ON(to > inode->i_size);

	if (from >= to || bsize >= PAGE_SIZE)
		return;
	/* Page straddling @from will not have any hole block created? */
	rounded_from = round_up(from, bsize);
	if (to <= rounded_from || !(rounded_from & (PAGE_SIZE - 1)))
		return;

	folio = filemap_lock_folio(inode->i_mapping, from / PAGE_SIZE);
	/* Folio not cached? Nothing to do */
	if (IS_ERR(folio))
		return;
	/*
	 * See folio_clear_dirty_for_io() for details why folio_mark_dirty()
	 * is needed.
	 */
	if (folio_mkclean(folio))
		folio_mark_dirty(folio);

	/*
	 * The post-eof range of the folio must be zeroed before it is exposed
	 * to the file. Writeback normally does this, but since i_size has been
	 * increased we handle it here.
	 */
	if (folio_test_dirty(folio)) {
		unsigned int offset, end;

		offset = from - folio_pos(folio);
		end = min_t(unsigned int, to - folio_pos(folio),
			    folio_size(folio));
		folio_zero_segment(folio, offset, end);
	}

	folio_unlock(folio);
	folio_put(folio);
}
EXPORT_SYMBOL(pagecache_isize_extended);

/**
 * truncate_pagecache_range - unmap and remove pagecache that is hole-punched
 * @inode: inode
 * @lstart: offset of beginning of hole
 * @lend: offset of last byte of hole
 *
 * This function should typically be called before the filesystem
 * releases resources associated with the freed range (eg. deallocates
 * blocks). This way, pagecache will always stay logically coherent
 * with on-disk format, and the filesystem would not have to deal with
 * situations such as writepage being called for a page that has already
 * had its underlying blocks deallocated.
 */
void truncate_pagecache_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t unmap_start = round_up(lstart, PAGE_SIZE);
	loff_t unmap_end = round_down(1 + lend, PAGE_SIZE) - 1;
	/*
	 * This rounding is currently just for example: unmap_mapping_range
	 * expands its hole outwards, whereas we want it to contract the hole
	 * inwards.  However, existing callers of truncate_pagecache_range are
	 * doing their own page rounding first.  Note that unmap_mapping_range
	 * allows holelen 0 for all, and we allow lend -1 for end of file.
	 */

	/*
	 * Unlike in truncate_pagecache, unmap_mapping_range is called only
	 * once (before truncating pagecache), and without "even_cows" flag:
	 * hole-punching should not remove private COWed pages from the hole.
	 */
	if ((u64)unmap_end > (u64)unmap_start)
		unmap_mapping_range(mapping, unmap_start,
				    1 + unmap_end - unmap_start, 0);
	truncate_inode_pages_range(mapping, lstart, lend);
}
EXPORT_SYMBOL(truncate_pagecache_range);
