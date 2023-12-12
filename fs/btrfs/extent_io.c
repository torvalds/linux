// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/sched/mm.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/prefetch.h>
#include <linux/fsverity.h>
#include "misc.h"
#include "extent_io.h"
#include "extent-io-tree.h"
#include "extent_map.h"
#include "ctree.h"
#include "btrfs_inode.h"
#include "bio.h"
#include "locking.h"
#include "rcu-string.h"
#include "backref.h"
#include "disk-io.h"
#include "subpage.h"
#include "zoned.h"
#include "block-group.h"
#include "compression.h"
#include "fs.h"
#include "accessors.h"
#include "file-item.h"
#include "file.h"
#include "dev-replace.h"
#include "super.h"
#include "transaction.h"

static struct kmem_cache *extent_buffer_cache;

#ifdef CONFIG_BTRFS_DEBUG
static inline void btrfs_leak_debug_add_eb(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	unsigned long flags;

	spin_lock_irqsave(&fs_info->eb_leak_lock, flags);
	list_add(&eb->leak_list, &fs_info->allocated_ebs);
	spin_unlock_irqrestore(&fs_info->eb_leak_lock, flags);
}

static inline void btrfs_leak_debug_del_eb(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	unsigned long flags;

	spin_lock_irqsave(&fs_info->eb_leak_lock, flags);
	list_del(&eb->leak_list);
	spin_unlock_irqrestore(&fs_info->eb_leak_lock, flags);
}

void btrfs_extent_buffer_leak_debug_check(struct btrfs_fs_info *fs_info)
{
	struct extent_buffer *eb;
	unsigned long flags;

	/*
	 * If we didn't get into open_ctree our allocated_ebs will not be
	 * initialized, so just skip this.
	 */
	if (!fs_info->allocated_ebs.next)
		return;

	WARN_ON(!list_empty(&fs_info->allocated_ebs));
	spin_lock_irqsave(&fs_info->eb_leak_lock, flags);
	while (!list_empty(&fs_info->allocated_ebs)) {
		eb = list_first_entry(&fs_info->allocated_ebs,
				      struct extent_buffer, leak_list);
		pr_err(
	"BTRFS: buffer leak start %llu len %lu refs %d bflags %lu owner %llu\n",
		       eb->start, eb->len, atomic_read(&eb->refs), eb->bflags,
		       btrfs_header_owner(eb));
		list_del(&eb->leak_list);
		kmem_cache_free(extent_buffer_cache, eb);
	}
	spin_unlock_irqrestore(&fs_info->eb_leak_lock, flags);
}
#else
#define btrfs_leak_debug_add_eb(eb)			do {} while (0)
#define btrfs_leak_debug_del_eb(eb)			do {} while (0)
#endif

/*
 * Structure to record info about the bio being assembled, and other info like
 * how many bytes are there before stripe/ordered extent boundary.
 */
struct btrfs_bio_ctrl {
	struct btrfs_bio *bbio;
	enum btrfs_compression_type compress_type;
	u32 len_to_oe_boundary;
	blk_opf_t opf;
	btrfs_bio_end_io_t end_io_func;
	struct writeback_control *wbc;
};

static void submit_one_bio(struct btrfs_bio_ctrl *bio_ctrl)
{
	struct btrfs_bio *bbio = bio_ctrl->bbio;

	if (!bbio)
		return;

	/* Caller should ensure the bio has at least some range added */
	ASSERT(bbio->bio.bi_iter.bi_size);

	if (btrfs_op(&bbio->bio) == BTRFS_MAP_READ &&
	    bio_ctrl->compress_type != BTRFS_COMPRESS_NONE)
		btrfs_submit_compressed_read(bbio);
	else
		btrfs_submit_bio(bbio, 0);

	/* The bbio is owned by the end_io handler now */
	bio_ctrl->bbio = NULL;
}

/*
 * Submit or fail the current bio in the bio_ctrl structure.
 */
static void submit_write_bio(struct btrfs_bio_ctrl *bio_ctrl, int ret)
{
	struct btrfs_bio *bbio = bio_ctrl->bbio;

	if (!bbio)
		return;

	if (ret) {
		ASSERT(ret < 0);
		btrfs_bio_end_io(bbio, errno_to_blk_status(ret));
		/* The bio is owned by the end_io handler now */
		bio_ctrl->bbio = NULL;
	} else {
		submit_one_bio(bio_ctrl);
	}
}

int __init extent_buffer_init_cachep(void)
{
	extent_buffer_cache = kmem_cache_create("btrfs_extent_buffer",
			sizeof(struct extent_buffer), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!extent_buffer_cache)
		return -ENOMEM;

	return 0;
}

void __cold extent_buffer_free_cachep(void)
{
	/*
	 * Make sure all delayed rcu free are flushed before we
	 * destroy caches.
	 */
	rcu_barrier();
	kmem_cache_destroy(extent_buffer_cache);
}

void extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end)
{
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(inode->i_mapping, index);
		BUG_ON(!page); /* Pages should be in the extent_io_tree */
		clear_page_dirty_for_io(page);
		put_page(page);
		index++;
	}
}

static void process_one_page(struct btrfs_fs_info *fs_info,
			     struct page *page, struct page *locked_page,
			     unsigned long page_ops, u64 start, u64 end)
{
	struct folio *folio = page_folio(page);
	u32 len;

	ASSERT(end + 1 - start != 0 && end + 1 - start < U32_MAX);
	len = end + 1 - start;

	if (page_ops & PAGE_SET_ORDERED)
		btrfs_folio_clamp_set_ordered(fs_info, folio, start, len);
	if (page_ops & PAGE_START_WRITEBACK) {
		btrfs_folio_clamp_clear_dirty(fs_info, folio, start, len);
		btrfs_folio_clamp_set_writeback(fs_info, folio, start, len);
	}
	if (page_ops & PAGE_END_WRITEBACK)
		btrfs_folio_clamp_clear_writeback(fs_info, folio, start, len);

	if (page != locked_page && (page_ops & PAGE_UNLOCK))
		btrfs_folio_end_writer_lock(fs_info, folio, start, len);
}

static void __process_pages_contig(struct address_space *mapping,
				   struct page *locked_page, u64 start, u64 end,
				   unsigned long page_ops)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(mapping->host->i_sb);
	pgoff_t start_index = start >> PAGE_SHIFT;
	pgoff_t end_index = end >> PAGE_SHIFT;
	pgoff_t index = start_index;
	struct folio_batch fbatch;
	int i;

	folio_batch_init(&fbatch);
	while (index <= end_index) {
		int found_folios;

		found_folios = filemap_get_folios_contig(mapping, &index,
				end_index, &fbatch);
		for (i = 0; i < found_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			process_one_page(fs_info, &folio->page, locked_page,
					 page_ops, start, end);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

static noinline void __unlock_for_delalloc(struct inode *inode,
					   struct page *locked_page,
					   u64 start, u64 end)
{
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;

	ASSERT(locked_page);
	if (index == locked_page->index && end_index == index)
		return;

	__process_pages_contig(inode->i_mapping, locked_page, start, end,
			       PAGE_UNLOCK);
}

static noinline int lock_delalloc_pages(struct inode *inode,
					struct page *locked_page,
					u64 start,
					u64 end)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t start_index = start >> PAGE_SHIFT;
	pgoff_t end_index = end >> PAGE_SHIFT;
	pgoff_t index = start_index;
	u64 processed_end = start;
	struct folio_batch fbatch;

	if (index == locked_page->index && index == end_index)
		return 0;

	folio_batch_init(&fbatch);
	while (index <= end_index) {
		unsigned int found_folios, i;

		found_folios = filemap_get_folios_contig(mapping, &index,
				end_index, &fbatch);
		if (found_folios == 0)
			goto out;

		for (i = 0; i < found_folios; i++) {
			struct folio *folio = fbatch.folios[i];
			struct page *page = folio_page(folio, 0);
			u32 len = end + 1 - start;

			if (page == locked_page)
				continue;

			if (btrfs_folio_start_writer_lock(fs_info, folio, start,
							  len))
				goto out;

			if (!PageDirty(page) || page->mapping != mapping) {
				btrfs_folio_end_writer_lock(fs_info, folio, start,
							    len);
				goto out;
			}

			processed_end = page_offset(page) + PAGE_SIZE - 1;
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	return 0;
out:
	folio_batch_release(&fbatch);
	if (processed_end > start)
		__unlock_for_delalloc(inode, locked_page, start, processed_end);
	return -EAGAIN;
}

/*
 * Find and lock a contiguous range of bytes in the file marked as delalloc, no
 * more than @max_bytes.
 *
 * @start:	The original start bytenr to search.
 *		Will store the extent range start bytenr.
 * @end:	The original end bytenr of the search range
 *		Will store the extent range end bytenr.
 *
 * Return true if we find a delalloc range which starts inside the original
 * range, and @start/@end will store the delalloc range start/end.
 *
 * Return false if we can't find any delalloc range which starts inside the
 * original range, and @start/@end will be the non-delalloc range start/end.
 */
EXPORT_FOR_TESTS
noinline_for_stack bool find_lock_delalloc_range(struct inode *inode,
				    struct page *locked_page, u64 *start,
				    u64 *end)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
	const u64 orig_start = *start;
	const u64 orig_end = *end;
	/* The sanity tests may not set a valid fs_info. */
	u64 max_bytes = fs_info ? fs_info->max_extent_size : BTRFS_MAX_EXTENT_SIZE;
	u64 delalloc_start;
	u64 delalloc_end;
	bool found;
	struct extent_state *cached_state = NULL;
	int ret;
	int loops = 0;

	/* Caller should pass a valid @end to indicate the search range end */
	ASSERT(orig_end > orig_start);

	/* The range should at least cover part of the page */
	ASSERT(!(orig_start >= page_offset(locked_page) + PAGE_SIZE ||
		 orig_end <= page_offset(locked_page)));
again:
	/* step one, find a bunch of delalloc bytes starting at start */
	delalloc_start = *start;
	delalloc_end = 0;
	found = btrfs_find_delalloc_range(tree, &delalloc_start, &delalloc_end,
					  max_bytes, &cached_state);
	if (!found || delalloc_end <= *start || delalloc_start > orig_end) {
		*start = delalloc_start;

		/* @delalloc_end can be -1, never go beyond @orig_end */
		*end = min(delalloc_end, orig_end);
		free_extent_state(cached_state);
		return false;
	}

	/*
	 * start comes from the offset of locked_page.  We have to lock
	 * pages in order, so we can't process delalloc bytes before
	 * locked_page
	 */
	if (delalloc_start < *start)
		delalloc_start = *start;

	/*
	 * make sure to limit the number of pages we try to lock down
	 */
	if (delalloc_end + 1 - delalloc_start > max_bytes)
		delalloc_end = delalloc_start + max_bytes - 1;

	/* step two, lock all the pages after the page that has start */
	ret = lock_delalloc_pages(inode, locked_page,
				  delalloc_start, delalloc_end);
	ASSERT(!ret || ret == -EAGAIN);
	if (ret == -EAGAIN) {
		/* some of the pages are gone, lets avoid looping by
		 * shortening the size of the delalloc range we're searching
		 */
		free_extent_state(cached_state);
		cached_state = NULL;
		if (!loops) {
			max_bytes = PAGE_SIZE;
			loops = 1;
			goto again;
		} else {
			found = false;
			goto out_failed;
		}
	}

	/* step three, lock the state bits for the whole range */
	lock_extent(tree, delalloc_start, delalloc_end, &cached_state);

	/* then test to make sure it is all still delalloc */
	ret = test_range_bit(tree, delalloc_start, delalloc_end,
			     EXTENT_DELALLOC, cached_state);
	if (!ret) {
		unlock_extent(tree, delalloc_start, delalloc_end,
			      &cached_state);
		__unlock_for_delalloc(inode, locked_page,
			      delalloc_start, delalloc_end);
		cond_resched();
		goto again;
	}
	free_extent_state(cached_state);
	*start = delalloc_start;
	*end = delalloc_end;
out_failed:
	return found;
}

void extent_clear_unlock_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
				  struct page *locked_page,
				  u32 clear_bits, unsigned long page_ops)
{
	clear_extent_bit(&inode->io_tree, start, end, clear_bits, NULL);

	__process_pages_contig(inode->vfs_inode.i_mapping, locked_page,
			       start, end, page_ops);
}

static bool btrfs_verify_page(struct page *page, u64 start)
{
	if (!fsverity_active(page->mapping->host) ||
	    PageUptodate(page) ||
	    start >= i_size_read(page->mapping->host))
		return true;
	return fsverity_verify_page(page);
}

static void end_page_read(struct page *page, bool uptodate, u64 start, u32 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);
	struct folio *folio = page_folio(page);

	ASSERT(page_offset(page) <= start &&
	       start + len <= page_offset(page) + PAGE_SIZE);

	if (uptodate && btrfs_verify_page(page, start))
		btrfs_folio_set_uptodate(fs_info, folio, start, len);
	else
		btrfs_folio_clear_uptodate(fs_info, folio, start, len);

	if (!btrfs_is_subpage(fs_info, page->mapping))
		unlock_page(page);
	else
		btrfs_subpage_end_reader(fs_info, folio, start, len);
}

/*
 * After a write IO is done, we need to:
 *
 * - clear the uptodate bits on error
 * - clear the writeback bits in the extent tree for the range
 * - filio_end_writeback()  if there is no more pending io for the folio
 *
 * Scheduling is not allowed, so the extent state tree is expected
 * to have one and only one object corresponding to this IO.
 */
static void end_bbio_data_write(struct btrfs_bio *bbio)
{
	struct bio *bio = &bbio->bio;
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter fi;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_folio_all(fi, bio) {
		struct folio *folio = fi.folio;
		struct inode *inode = folio->mapping->host;
		struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
		const u32 sectorsize = fs_info->sectorsize;
		u64 start = folio_pos(folio) + fi.offset;
		u32 len = fi.length;

		/* Only order 0 (single page) folios are allowed for data. */
		ASSERT(folio_order(folio) == 0);

		/* Our read/write should always be sector aligned. */
		if (!IS_ALIGNED(fi.offset, sectorsize))
			btrfs_err(fs_info,
		"partial page write in btrfs with offset %zu and length %zu",
				  fi.offset, fi.length);
		else if (!IS_ALIGNED(fi.length, sectorsize))
			btrfs_info(fs_info,
		"incomplete page write with offset %zu and length %zu",
				   fi.offset, fi.length);

		btrfs_finish_ordered_extent(bbio->ordered,
				folio_page(folio, 0), start, len, !error);
		if (error)
			mapping_set_error(folio->mapping, error);
		btrfs_folio_clear_writeback(fs_info, folio, start, len);
	}

	bio_put(bio);
}

/*
 * Record previously processed extent range
 *
 * For endio_readpage_release_extent() to handle a full extent range, reducing
 * the extent io operations.
 */
struct processed_extent {
	struct btrfs_inode *inode;
	/* Start of the range in @inode */
	u64 start;
	/* End of the range in @inode */
	u64 end;
	bool uptodate;
};

/*
 * Try to release processed extent range
 *
 * May not release the extent range right now if the current range is
 * contiguous to processed extent.
 *
 * Will release processed extent when any of @inode, @uptodate, the range is
 * no longer contiguous to the processed range.
 *
 * Passing @inode == NULL will force processed extent to be released.
 */
static void endio_readpage_release_extent(struct processed_extent *processed,
			      struct btrfs_inode *inode, u64 start, u64 end,
			      bool uptodate)
{
	struct extent_state *cached = NULL;
	struct extent_io_tree *tree;

	/* The first extent, initialize @processed */
	if (!processed->inode)
		goto update;

	/*
	 * Contiguous to processed extent, just uptodate the end.
	 *
	 * Several things to notice:
	 *
	 * - bio can be merged as long as on-disk bytenr is contiguous
	 *   This means we can have page belonging to other inodes, thus need to
	 *   check if the inode still matches.
	 * - bvec can contain range beyond current page for multi-page bvec
	 *   Thus we need to do processed->end + 1 >= start check
	 */
	if (processed->inode == inode && processed->uptodate == uptodate &&
	    processed->end + 1 >= start && end >= processed->end) {
		processed->end = end;
		return;
	}

	tree = &processed->inode->io_tree;
	/*
	 * Now we don't have range contiguous to the processed range, release
	 * the processed range now.
	 */
	unlock_extent(tree, processed->start, processed->end, &cached);

update:
	/* Update processed to current range */
	processed->inode = inode;
	processed->start = start;
	processed->end = end;
	processed->uptodate = uptodate;
}

static void begin_page_read(struct btrfs_fs_info *fs_info, struct page *page)
{
	struct folio *folio = page_folio(page);

	ASSERT(folio_test_locked(folio));
	if (!btrfs_is_subpage(fs_info, folio->mapping))
		return;

	ASSERT(folio_test_private(folio));
	btrfs_subpage_start_reader(fs_info, folio, page_offset(page), PAGE_SIZE);
}

/*
 * After a data read IO is done, we need to:
 *
 * - clear the uptodate bits on error
 * - set the uptodate bits if things worked
 * - set the folio up to date if all extents in the tree are uptodate
 * - clear the lock bit in the extent tree
 * - unlock the folio if there are no other extents locked for it
 *
 * Scheduling is not allowed, so the extent state tree is expected
 * to have one and only one object corresponding to this IO.
 */
static void end_bbio_data_read(struct btrfs_bio *bbio)
{
	struct bio *bio = &bbio->bio;
	struct processed_extent processed = { 0 };
	struct folio_iter fi;
	/*
	 * The offset to the beginning of a bio, since one bio can never be
	 * larger than UINT_MAX, u32 here is enough.
	 */
	u32 bio_offset = 0;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_folio_all(fi, &bbio->bio) {
		bool uptodate = !bio->bi_status;
		struct folio *folio = fi.folio;
		struct inode *inode = folio->mapping->host;
		struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
		const u32 sectorsize = fs_info->sectorsize;
		u64 start;
		u64 end;
		u32 len;

		/* For now only order 0 folios are supported for data. */
		ASSERT(folio_order(folio) == 0);
		btrfs_debug(fs_info,
			"%s: bi_sector=%llu, err=%d, mirror=%u",
			__func__, bio->bi_iter.bi_sector, bio->bi_status,
			bbio->mirror_num);

		/*
		 * We always issue full-sector reads, but if some block in a
		 * folio fails to read, blk_update_request() will advance
		 * bv_offset and adjust bv_len to compensate.  Print a warning
		 * for unaligned offsets, and an error if they don't add up to
		 * a full sector.
		 */
		if (!IS_ALIGNED(fi.offset, sectorsize))
			btrfs_err(fs_info,
		"partial page read in btrfs with offset %zu and length %zu",
				  fi.offset, fi.length);
		else if (!IS_ALIGNED(fi.offset + fi.length, sectorsize))
			btrfs_info(fs_info,
		"incomplete page read with offset %zu and length %zu",
				   fi.offset, fi.length);

		start = folio_pos(folio) + fi.offset;
		end = start + fi.length - 1;
		len = fi.length;

		if (likely(uptodate)) {
			loff_t i_size = i_size_read(inode);
			pgoff_t end_index = i_size >> folio_shift(folio);

			/*
			 * Zero out the remaining part if this range straddles
			 * i_size.
			 *
			 * Here we should only zero the range inside the folio,
			 * not touch anything else.
			 *
			 * NOTE: i_size is exclusive while end is inclusive.
			 */
			if (folio_index(folio) == end_index && i_size <= end) {
				u32 zero_start = max(offset_in_folio(folio, i_size),
						     offset_in_folio(folio, start));
				u32 zero_len = offset_in_folio(folio, end) + 1 -
					       zero_start;

				folio_zero_range(folio, zero_start, zero_len);
			}
		}

		/* Update page status and unlock. */
		end_page_read(folio_page(folio, 0), uptodate, start, len);
		endio_readpage_release_extent(&processed, BTRFS_I(inode),
					      start, end, uptodate);

		ASSERT(bio_offset + len > bio_offset);
		bio_offset += len;

	}
	/* Release the last extent */
	endio_readpage_release_extent(&processed, NULL, 0, 0, false);
	bio_put(bio);
}

/*
 * Populate every free slot in a provided array with pages.
 *
 * @nr_pages:   number of pages to allocate
 * @page_array: the array to fill with pages; any existing non-null entries in
 * 		the array will be skipped
 * @extra_gfp:	the extra GFP flags for the allocation.
 *
 * Return: 0        if all pages were able to be allocated;
 *         -ENOMEM  otherwise, the partially allocated pages would be freed and
 *                  the array slots zeroed
 */
int btrfs_alloc_page_array(unsigned int nr_pages, struct page **page_array,
			   gfp_t extra_gfp)
{
	unsigned int allocated;

	for (allocated = 0; allocated < nr_pages;) {
		unsigned int last = allocated;

		allocated = alloc_pages_bulk_array(GFP_NOFS | extra_gfp,
						   nr_pages, page_array);

		if (allocated == nr_pages)
			return 0;

		/*
		 * During this iteration, no page could be allocated, even
		 * though alloc_pages_bulk_array() falls back to alloc_page()
		 * if  it could not bulk-allocate. So we must be out of memory.
		 */
		if (allocated == last) {
			for (int i = 0; i < allocated; i++) {
				__free_page(page_array[i]);
				page_array[i] = NULL;
			}
			return -ENOMEM;
		}

		memalloc_retry_wait(GFP_NOFS);
	}
	return 0;
}

/*
 * Populate needed folios for the extent buffer.
 *
 * For now, the folios populated are always in order 0 (aka, single page).
 */
static int alloc_eb_folio_array(struct extent_buffer *eb, gfp_t extra_gfp)
{
	struct page *page_array[INLINE_EXTENT_BUFFER_PAGES] = { 0 };
	int num_pages = num_extent_pages(eb);
	int ret;

	ret = btrfs_alloc_page_array(num_pages, page_array, extra_gfp);
	if (ret < 0)
		return ret;

	for (int i = 0; i < num_pages; i++)
		eb->folios[i] = page_folio(page_array[i]);
	return 0;
}

static bool btrfs_bio_is_contig(struct btrfs_bio_ctrl *bio_ctrl,
				struct page *page, u64 disk_bytenr,
				unsigned int pg_offset)
{
	struct bio *bio = &bio_ctrl->bbio->bio;
	struct bio_vec *bvec = bio_last_bvec_all(bio);
	const sector_t sector = disk_bytenr >> SECTOR_SHIFT;

	if (bio_ctrl->compress_type != BTRFS_COMPRESS_NONE) {
		/*
		 * For compression, all IO should have its logical bytenr set
		 * to the starting bytenr of the compressed extent.
		 */
		return bio->bi_iter.bi_sector == sector;
	}

	/*
	 * The contig check requires the following conditions to be met:
	 *
	 * 1) The pages are belonging to the same inode
	 *    This is implied by the call chain.
	 *
	 * 2) The range has adjacent logical bytenr
	 *
	 * 3) The range has adjacent file offset
	 *    This is required for the usage of btrfs_bio->file_offset.
	 */
	return bio_end_sector(bio) == sector &&
		page_offset(bvec->bv_page) + bvec->bv_offset + bvec->bv_len ==
		page_offset(page) + pg_offset;
}

static void alloc_new_bio(struct btrfs_inode *inode,
			  struct btrfs_bio_ctrl *bio_ctrl,
			  u64 disk_bytenr, u64 file_offset)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_bio *bbio;

	bbio = btrfs_bio_alloc(BIO_MAX_VECS, bio_ctrl->opf, fs_info,
			       bio_ctrl->end_io_func, NULL);
	bbio->bio.bi_iter.bi_sector = disk_bytenr >> SECTOR_SHIFT;
	bbio->inode = inode;
	bbio->file_offset = file_offset;
	bio_ctrl->bbio = bbio;
	bio_ctrl->len_to_oe_boundary = U32_MAX;

	/* Limit data write bios to the ordered boundary. */
	if (bio_ctrl->wbc) {
		struct btrfs_ordered_extent *ordered;

		ordered = btrfs_lookup_ordered_extent(inode, file_offset);
		if (ordered) {
			bio_ctrl->len_to_oe_boundary = min_t(u32, U32_MAX,
					ordered->file_offset +
					ordered->disk_num_bytes - file_offset);
			bbio->ordered = ordered;
		}

		/*
		 * Pick the last added device to support cgroup writeback.  For
		 * multi-device file systems this means blk-cgroup policies have
		 * to always be set on the last added/replaced device.
		 * This is a bit odd but has been like that for a long time.
		 */
		bio_set_dev(&bbio->bio, fs_info->fs_devices->latest_dev->bdev);
		wbc_init_bio(bio_ctrl->wbc, &bbio->bio);
	}
}

/*
 * @disk_bytenr: logical bytenr where the write will be
 * @page:	page to add to the bio
 * @size:	portion of page that we want to write to
 * @pg_offset:	offset of the new bio or to check whether we are adding
 *              a contiguous page to the previous one
 *
 * The will either add the page into the existing @bio_ctrl->bbio, or allocate a
 * new one in @bio_ctrl->bbio.
 * The mirror number for this IO should already be initizlied in
 * @bio_ctrl->mirror_num.
 */
static void submit_extent_page(struct btrfs_bio_ctrl *bio_ctrl,
			       u64 disk_bytenr, struct page *page,
			       size_t size, unsigned long pg_offset)
{
	struct btrfs_inode *inode = BTRFS_I(page->mapping->host);

	ASSERT(pg_offset + size <= PAGE_SIZE);
	ASSERT(bio_ctrl->end_io_func);

	if (bio_ctrl->bbio &&
	    !btrfs_bio_is_contig(bio_ctrl, page, disk_bytenr, pg_offset))
		submit_one_bio(bio_ctrl);

	do {
		u32 len = size;

		/* Allocate new bio if needed */
		if (!bio_ctrl->bbio) {
			alloc_new_bio(inode, bio_ctrl, disk_bytenr,
				      page_offset(page) + pg_offset);
		}

		/* Cap to the current ordered extent boundary if there is one. */
		if (len > bio_ctrl->len_to_oe_boundary) {
			ASSERT(bio_ctrl->compress_type == BTRFS_COMPRESS_NONE);
			ASSERT(is_data_inode(&inode->vfs_inode));
			len = bio_ctrl->len_to_oe_boundary;
		}

		if (bio_add_page(&bio_ctrl->bbio->bio, page, len, pg_offset) != len) {
			/* bio full: move on to a new one */
			submit_one_bio(bio_ctrl);
			continue;
		}

		if (bio_ctrl->wbc)
			wbc_account_cgroup_owner(bio_ctrl->wbc, page, len);

		size -= len;
		pg_offset += len;
		disk_bytenr += len;

		/*
		 * len_to_oe_boundary defaults to U32_MAX, which isn't page or
		 * sector aligned.  alloc_new_bio() then sets it to the end of
		 * our ordered extent for writes into zoned devices.
		 *
		 * When len_to_oe_boundary is tracking an ordered extent, we
		 * trust the ordered extent code to align things properly, and
		 * the check above to cap our write to the ordered extent
		 * boundary is correct.
		 *
		 * When len_to_oe_boundary is U32_MAX, the cap above would
		 * result in a 4095 byte IO for the last page right before
		 * we hit the bio limit of UINT_MAX.  bio_add_page() has all
		 * the checks required to make sure we don't overflow the bio,
		 * and we should just ignore len_to_oe_boundary completely
		 * unless we're using it to track an ordered extent.
		 *
		 * It's pretty hard to make a bio sized U32_MAX, but it can
		 * happen when the page cache is able to feed us contiguous
		 * pages for large extents.
		 */
		if (bio_ctrl->len_to_oe_boundary != U32_MAX)
			bio_ctrl->len_to_oe_boundary -= len;

		/* Ordered extent boundary: move on to a new bio. */
		if (bio_ctrl->len_to_oe_boundary == 0)
			submit_one_bio(bio_ctrl);
	} while (size);
}

static int attach_extent_buffer_folio(struct extent_buffer *eb,
				      struct folio *folio,
				      struct btrfs_subpage *prealloc)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int ret = 0;

	/*
	 * If the page is mapped to btree inode, we should hold the private
	 * lock to prevent race.
	 * For cloned or dummy extent buffers, their pages are not mapped and
	 * will not race with any other ebs.
	 */
	if (folio->mapping)
		lockdep_assert_held(&folio->mapping->private_lock);

	if (fs_info->nodesize >= PAGE_SIZE) {
		if (!folio_test_private(folio))
			folio_attach_private(folio, eb);
		else
			WARN_ON(folio_get_private(folio) != eb);
		return 0;
	}

	/* Already mapped, just free prealloc */
	if (folio_test_private(folio)) {
		btrfs_free_subpage(prealloc);
		return 0;
	}

	if (prealloc)
		/* Has preallocated memory for subpage */
		folio_attach_private(folio, prealloc);
	else
		/* Do new allocation to attach subpage */
		ret = btrfs_attach_subpage(fs_info, folio, BTRFS_SUBPAGE_METADATA);
	return ret;
}

int set_page_extent_mapped(struct page *page)
{
	struct folio *folio = page_folio(page);
	struct btrfs_fs_info *fs_info;

	ASSERT(page->mapping);

	if (folio_test_private(folio))
		return 0;

	fs_info = btrfs_sb(page->mapping->host->i_sb);

	if (btrfs_is_subpage(fs_info, page->mapping))
		return btrfs_attach_subpage(fs_info, folio, BTRFS_SUBPAGE_DATA);

	folio_attach_private(folio, (void *)EXTENT_FOLIO_PRIVATE);
	return 0;
}

void clear_page_extent_mapped(struct page *page)
{
	struct folio *folio = page_folio(page);
	struct btrfs_fs_info *fs_info;

	ASSERT(page->mapping);

	if (!folio_test_private(folio))
		return;

	fs_info = btrfs_sb(page->mapping->host->i_sb);
	if (btrfs_is_subpage(fs_info, page->mapping))
		return btrfs_detach_subpage(fs_info, folio);

	folio_detach_private(folio);
}

static struct extent_map *
__get_extent_map(struct inode *inode, struct page *page, size_t pg_offset,
		 u64 start, u64 len, struct extent_map **em_cached)
{
	struct extent_map *em;

	if (em_cached && *em_cached) {
		em = *em_cached;
		if (extent_map_in_tree(em) && start >= em->start &&
		    start < extent_map_end(em)) {
			refcount_inc(&em->refs);
			return em;
		}

		free_extent_map(em);
		*em_cached = NULL;
	}

	em = btrfs_get_extent(BTRFS_I(inode), page, pg_offset, start, len);
	if (em_cached && !IS_ERR(em)) {
		BUG_ON(*em_cached);
		refcount_inc(&em->refs);
		*em_cached = em;
	}
	return em;
}
/*
 * basic readpage implementation.  Locked extent state structs are inserted
 * into the tree that are removed when the IO is done (by the end_io
 * handlers)
 * XXX JDM: This needs looking at to ensure proper page locking
 * return 0 on success, otherwise return error
 */
static int btrfs_do_readpage(struct page *page, struct extent_map **em_cached,
		      struct btrfs_bio_ctrl *bio_ctrl, u64 *prev_em_start)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 start = page_offset(page);
	const u64 end = start + PAGE_SIZE - 1;
	u64 cur = start;
	u64 extent_offset;
	u64 last_byte = i_size_read(inode);
	u64 block_start;
	struct extent_map *em;
	int ret = 0;
	size_t pg_offset = 0;
	size_t iosize;
	size_t blocksize = inode->i_sb->s_blocksize;
	struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;

	ret = set_page_extent_mapped(page);
	if (ret < 0) {
		unlock_extent(tree, start, end, NULL);
		unlock_page(page);
		return ret;
	}

	if (page->index == last_byte >> PAGE_SHIFT) {
		size_t zero_offset = offset_in_page(last_byte);

		if (zero_offset) {
			iosize = PAGE_SIZE - zero_offset;
			memzero_page(page, zero_offset, iosize);
		}
	}
	bio_ctrl->end_io_func = end_bbio_data_read;
	begin_page_read(fs_info, page);
	while (cur <= end) {
		enum btrfs_compression_type compress_type = BTRFS_COMPRESS_NONE;
		bool force_bio_submit = false;
		u64 disk_bytenr;

		ASSERT(IS_ALIGNED(cur, fs_info->sectorsize));
		if (cur >= last_byte) {
			iosize = PAGE_SIZE - pg_offset;
			memzero_page(page, pg_offset, iosize);
			unlock_extent(tree, cur, cur + iosize - 1, NULL);
			end_page_read(page, true, cur, iosize);
			break;
		}
		em = __get_extent_map(inode, page, pg_offset, cur,
				      end - cur + 1, em_cached);
		if (IS_ERR(em)) {
			unlock_extent(tree, cur, end, NULL);
			end_page_read(page, false, cur, end + 1 - cur);
			return PTR_ERR(em);
		}
		extent_offset = cur - em->start;
		BUG_ON(extent_map_end(em) <= cur);
		BUG_ON(end < cur);

		compress_type = extent_map_compression(em);

		iosize = min(extent_map_end(em) - cur, end - cur + 1);
		iosize = ALIGN(iosize, blocksize);
		if (compress_type != BTRFS_COMPRESS_NONE)
			disk_bytenr = em->block_start;
		else
			disk_bytenr = em->block_start + extent_offset;
		block_start = em->block_start;
		if (em->flags & EXTENT_FLAG_PREALLOC)
			block_start = EXTENT_MAP_HOLE;

		/*
		 * If we have a file range that points to a compressed extent
		 * and it's followed by a consecutive file range that points
		 * to the same compressed extent (possibly with a different
		 * offset and/or length, so it either points to the whole extent
		 * or only part of it), we must make sure we do not submit a
		 * single bio to populate the pages for the 2 ranges because
		 * this makes the compressed extent read zero out the pages
		 * belonging to the 2nd range. Imagine the following scenario:
		 *
		 *  File layout
		 *  [0 - 8K]                     [8K - 24K]
		 *    |                               |
		 *    |                               |
		 * points to extent X,         points to extent X,
		 * offset 4K, length of 8K     offset 0, length 16K
		 *
		 * [extent X, compressed length = 4K uncompressed length = 16K]
		 *
		 * If the bio to read the compressed extent covers both ranges,
		 * it will decompress extent X into the pages belonging to the
		 * first range and then it will stop, zeroing out the remaining
		 * pages that belong to the other range that points to extent X.
		 * So here we make sure we submit 2 bios, one for the first
		 * range and another one for the third range. Both will target
		 * the same physical extent from disk, but we can't currently
		 * make the compressed bio endio callback populate the pages
		 * for both ranges because each compressed bio is tightly
		 * coupled with a single extent map, and each range can have
		 * an extent map with a different offset value relative to the
		 * uncompressed data of our extent and different lengths. This
		 * is a corner case so we prioritize correctness over
		 * non-optimal behavior (submitting 2 bios for the same extent).
		 */
		if (compress_type != BTRFS_COMPRESS_NONE &&
		    prev_em_start && *prev_em_start != (u64)-1 &&
		    *prev_em_start != em->start)
			force_bio_submit = true;

		if (prev_em_start)
			*prev_em_start = em->start;

		free_extent_map(em);
		em = NULL;

		/* we've found a hole, just zero and go on */
		if (block_start == EXTENT_MAP_HOLE) {
			memzero_page(page, pg_offset, iosize);

			unlock_extent(tree, cur, cur + iosize - 1, NULL);
			end_page_read(page, true, cur, iosize);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}
		/* the get_extent function already copied into the page */
		if (block_start == EXTENT_MAP_INLINE) {
			unlock_extent(tree, cur, cur + iosize - 1, NULL);
			end_page_read(page, true, cur, iosize);
			cur = cur + iosize;
			pg_offset += iosize;
			continue;
		}

		if (bio_ctrl->compress_type != compress_type) {
			submit_one_bio(bio_ctrl);
			bio_ctrl->compress_type = compress_type;
		}

		if (force_bio_submit)
			submit_one_bio(bio_ctrl);
		submit_extent_page(bio_ctrl, disk_bytenr, page, iosize,
				   pg_offset);
		cur = cur + iosize;
		pg_offset += iosize;
	}

	return 0;
}

int btrfs_read_folio(struct file *file, struct folio *folio)
{
	struct page *page = &folio->page;
	struct btrfs_inode *inode = BTRFS_I(page->mapping->host);
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	struct btrfs_bio_ctrl bio_ctrl = { .opf = REQ_OP_READ };
	int ret;

	btrfs_lock_and_flush_ordered_range(inode, start, end, NULL);

	ret = btrfs_do_readpage(page, NULL, &bio_ctrl, NULL);
	/*
	 * If btrfs_do_readpage() failed we will want to submit the assembled
	 * bio to do the cleanup.
	 */
	submit_one_bio(&bio_ctrl);
	return ret;
}

static inline void contiguous_readpages(struct page *pages[], int nr_pages,
					u64 start, u64 end,
					struct extent_map **em_cached,
					struct btrfs_bio_ctrl *bio_ctrl,
					u64 *prev_em_start)
{
	struct btrfs_inode *inode = BTRFS_I(pages[0]->mapping->host);
	int index;

	btrfs_lock_and_flush_ordered_range(inode, start, end, NULL);

	for (index = 0; index < nr_pages; index++) {
		btrfs_do_readpage(pages[index], em_cached, bio_ctrl,
				  prev_em_start);
		put_page(pages[index]);
	}
}

/*
 * helper for __extent_writepage, doing all of the delayed allocation setup.
 *
 * This returns 1 if btrfs_run_delalloc_range function did all the work required
 * to write the page (copy into inline extent).  In this case the IO has
 * been started and the page is already unlocked.
 *
 * This returns 0 if all went well (page still locked)
 * This returns < 0 if there were errors (page still locked)
 */
static noinline_for_stack int writepage_delalloc(struct btrfs_inode *inode,
		struct page *page, struct writeback_control *wbc)
{
	const u64 page_start = page_offset(page);
	const u64 page_end = page_start + PAGE_SIZE - 1;
	u64 delalloc_start = page_start;
	u64 delalloc_end = page_end;
	u64 delalloc_to_write = 0;
	int ret = 0;

	while (delalloc_start < page_end) {
		delalloc_end = page_end;
		if (!find_lock_delalloc_range(&inode->vfs_inode, page,
					      &delalloc_start, &delalloc_end)) {
			delalloc_start = delalloc_end + 1;
			continue;
		}

		ret = btrfs_run_delalloc_range(inode, page, delalloc_start,
					       delalloc_end, wbc);
		if (ret < 0)
			return ret;

		delalloc_start = delalloc_end + 1;
	}

	/*
	 * delalloc_end is already one less than the total length, so
	 * we don't subtract one from PAGE_SIZE
	 */
	delalloc_to_write +=
		DIV_ROUND_UP(delalloc_end + 1 - page_start, PAGE_SIZE);

	/*
	 * If btrfs_run_dealloc_range() already started I/O and unlocked
	 * the pages, we just need to account for them here.
	 */
	if (ret == 1) {
		wbc->nr_to_write -= delalloc_to_write;
		return 1;
	}

	if (wbc->nr_to_write < delalloc_to_write) {
		int thresh = 8192;

		if (delalloc_to_write < thresh * 2)
			thresh = delalloc_to_write;
		wbc->nr_to_write = min_t(u64, delalloc_to_write,
					 thresh);
	}

	return 0;
}

/*
 * Find the first byte we need to write.
 *
 * For subpage, one page can contain several sectors, and
 * __extent_writepage_io() will just grab all extent maps in the page
 * range and try to submit all non-inline/non-compressed extents.
 *
 * This is a big problem for subpage, we shouldn't re-submit already written
 * data at all.
 * This function will lookup subpage dirty bit to find which range we really
 * need to submit.
 *
 * Return the next dirty range in [@start, @end).
 * If no dirty range is found, @start will be page_offset(page) + PAGE_SIZE.
 */
static void find_next_dirty_byte(struct btrfs_fs_info *fs_info,
				 struct page *page, u64 *start, u64 *end)
{
	struct folio *folio = page_folio(page);
	struct btrfs_subpage *subpage = folio_get_private(folio);
	struct btrfs_subpage_info *spi = fs_info->subpage_info;
	u64 orig_start = *start;
	/* Declare as unsigned long so we can use bitmap ops */
	unsigned long flags;
	int range_start_bit;
	int range_end_bit;

	/*
	 * For regular sector size == page size case, since one page only
	 * contains one sector, we return the page offset directly.
	 */
	if (!btrfs_is_subpage(fs_info, page->mapping)) {
		*start = page_offset(page);
		*end = page_offset(page) + PAGE_SIZE;
		return;
	}

	range_start_bit = spi->dirty_offset +
			  (offset_in_page(orig_start) >> fs_info->sectorsize_bits);

	/* We should have the page locked, but just in case */
	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_next_set_region(subpage->bitmaps, &range_start_bit, &range_end_bit,
			       spi->dirty_offset + spi->bitmap_nr_bits);
	spin_unlock_irqrestore(&subpage->lock, flags);

	range_start_bit -= spi->dirty_offset;
	range_end_bit -= spi->dirty_offset;

	*start = page_offset(page) + range_start_bit * fs_info->sectorsize;
	*end = page_offset(page) + range_end_bit * fs_info->sectorsize;
}

/*
 * helper for __extent_writepage.  This calls the writepage start hooks,
 * and does the loop to map the page into extents and bios.
 *
 * We return 1 if the IO is started and the page is unlocked,
 * 0 if all went well (page still locked)
 * < 0 if there were errors (page still locked)
 */
static noinline_for_stack int __extent_writepage_io(struct btrfs_inode *inode,
				 struct page *page,
				 struct btrfs_bio_ctrl *bio_ctrl,
				 loff_t i_size,
				 int *nr_ret)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 cur = page_offset(page);
	u64 end = cur + PAGE_SIZE - 1;
	u64 extent_offset;
	u64 block_start;
	struct extent_map *em;
	int ret = 0;
	int nr = 0;

	ret = btrfs_writepage_cow_fixup(page);
	if (ret) {
		/* Fixup worker will requeue */
		redirty_page_for_writepage(bio_ctrl->wbc, page);
		unlock_page(page);
		return 1;
	}

	bio_ctrl->end_io_func = end_bbio_data_write;
	while (cur <= end) {
		u32 len = end - cur + 1;
		u64 disk_bytenr;
		u64 em_end;
		u64 dirty_range_start = cur;
		u64 dirty_range_end;
		u32 iosize;

		if (cur >= i_size) {
			btrfs_mark_ordered_io_finished(inode, page, cur, len,
						       true);
			/*
			 * This range is beyond i_size, thus we don't need to
			 * bother writing back.
			 * But we still need to clear the dirty subpage bit, or
			 * the next time the page gets dirtied, we will try to
			 * writeback the sectors with subpage dirty bits,
			 * causing writeback without ordered extent.
			 */
			btrfs_folio_clear_dirty(fs_info, page_folio(page), cur, len);
			break;
		}

		find_next_dirty_byte(fs_info, page, &dirty_range_start,
				     &dirty_range_end);
		if (cur < dirty_range_start) {
			cur = dirty_range_start;
			continue;
		}

		em = btrfs_get_extent(inode, NULL, 0, cur, len);
		if (IS_ERR(em)) {
			ret = PTR_ERR_OR_ZERO(em);
			goto out_error;
		}

		extent_offset = cur - em->start;
		em_end = extent_map_end(em);
		ASSERT(cur <= em_end);
		ASSERT(cur < end);
		ASSERT(IS_ALIGNED(em->start, fs_info->sectorsize));
		ASSERT(IS_ALIGNED(em->len, fs_info->sectorsize));

		block_start = em->block_start;
		disk_bytenr = em->block_start + extent_offset;

		ASSERT(!extent_map_is_compressed(em));
		ASSERT(block_start != EXTENT_MAP_HOLE);
		ASSERT(block_start != EXTENT_MAP_INLINE);

		/*
		 * Note that em_end from extent_map_end() and dirty_range_end from
		 * find_next_dirty_byte() are all exclusive
		 */
		iosize = min(min(em_end, end + 1), dirty_range_end) - cur;
		free_extent_map(em);
		em = NULL;

		btrfs_set_range_writeback(inode, cur, cur + iosize - 1);
		if (!PageWriteback(page)) {
			btrfs_err(inode->root->fs_info,
				   "page %lu not writeback, cur %llu end %llu",
			       page->index, cur, end);
		}

		/*
		 * Although the PageDirty bit is cleared before entering this
		 * function, subpage dirty bit is not cleared.
		 * So clear subpage dirty bit here so next time we won't submit
		 * page for range already written to disk.
		 */
		btrfs_folio_clear_dirty(fs_info, page_folio(page), cur, iosize);

		submit_extent_page(bio_ctrl, disk_bytenr, page, iosize,
				   cur - page_offset(page));
		cur += iosize;
		nr++;
	}

	btrfs_folio_assert_not_dirty(fs_info, page_folio(page));
	*nr_ret = nr;
	return 0;

out_error:
	/*
	 * If we finish without problem, we should not only clear page dirty,
	 * but also empty subpage dirty bits
	 */
	*nr_ret = nr;
	return ret;
}

/*
 * the writepage semantics are similar to regular writepage.  extent
 * records are inserted to lock ranges in the tree, and as dirty areas
 * are found, they are marked writeback.  Then the lock bits are removed
 * and the end_io handler clears the writeback ranges
 *
 * Return 0 if everything goes well.
 * Return <0 for error.
 */
static int __extent_writepage(struct page *page, struct btrfs_bio_ctrl *bio_ctrl)
{
	struct folio *folio = page_folio(page);
	struct inode *inode = page->mapping->host;
	const u64 page_start = page_offset(page);
	int ret;
	int nr = 0;
	size_t pg_offset;
	loff_t i_size = i_size_read(inode);
	unsigned long end_index = i_size >> PAGE_SHIFT;

	trace___extent_writepage(page, inode, bio_ctrl->wbc);

	WARN_ON(!PageLocked(page));

	pg_offset = offset_in_page(i_size);
	if (page->index > end_index ||
	   (page->index == end_index && !pg_offset)) {
		folio_invalidate(folio, 0, folio_size(folio));
		folio_unlock(folio);
		return 0;
	}

	if (page->index == end_index)
		memzero_page(page, pg_offset, PAGE_SIZE - pg_offset);

	ret = set_page_extent_mapped(page);
	if (ret < 0)
		goto done;

	ret = writepage_delalloc(BTRFS_I(inode), page, bio_ctrl->wbc);
	if (ret == 1)
		return 0;
	if (ret)
		goto done;

	ret = __extent_writepage_io(BTRFS_I(inode), page, bio_ctrl, i_size, &nr);
	if (ret == 1)
		return 0;

	bio_ctrl->wbc->nr_to_write--;

done:
	if (nr == 0) {
		/* make sure the mapping tag for page dirty gets cleared */
		set_page_writeback(page);
		end_page_writeback(page);
	}
	if (ret) {
		btrfs_mark_ordered_io_finished(BTRFS_I(inode), page, page_start,
					       PAGE_SIZE, !ret);
		mapping_set_error(page->mapping, ret);
	}
	unlock_page(page);
	ASSERT(ret <= 0);
	return ret;
}

void wait_on_extent_buffer_writeback(struct extent_buffer *eb)
{
	wait_on_bit_io(&eb->bflags, EXTENT_BUFFER_WRITEBACK,
		       TASK_UNINTERRUPTIBLE);
}

/*
 * Lock extent buffer status and pages for writeback.
 *
 * Return %false if the extent buffer doesn't need to be submitted (e.g. the
 * extent buffer is not dirty)
 * Return %true is the extent buffer is submitted to bio.
 */
static noinline_for_stack bool lock_extent_buffer_for_io(struct extent_buffer *eb,
			  struct writeback_control *wbc)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	bool ret = false;

	btrfs_tree_lock(eb);
	while (test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags)) {
		btrfs_tree_unlock(eb);
		if (wbc->sync_mode != WB_SYNC_ALL)
			return false;
		wait_on_extent_buffer_writeback(eb);
		btrfs_tree_lock(eb);
	}

	/*
	 * We need to do this to prevent races in people who check if the eb is
	 * under IO since we can end up having no IO bits set for a short period
	 * of time.
	 */
	spin_lock(&eb->refs_lock);
	if (test_and_clear_bit(EXTENT_BUFFER_DIRTY, &eb->bflags)) {
		set_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags);
		spin_unlock(&eb->refs_lock);
		btrfs_set_header_flag(eb, BTRFS_HEADER_FLAG_WRITTEN);
		percpu_counter_add_batch(&fs_info->dirty_metadata_bytes,
					 -eb->len,
					 fs_info->dirty_metadata_batch);
		ret = true;
	} else {
		spin_unlock(&eb->refs_lock);
	}
	btrfs_tree_unlock(eb);
	return ret;
}

static void set_btree_ioerr(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;

	set_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags);

	/*
	 * A read may stumble upon this buffer later, make sure that it gets an
	 * error and knows there was an error.
	 */
	clear_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);

	/*
	 * We need to set the mapping with the io error as well because a write
	 * error will flip the file system readonly, and then syncfs() will
	 * return a 0 because we are readonly if we don't modify the err seq for
	 * the superblock.
	 */
	mapping_set_error(eb->fs_info->btree_inode->i_mapping, -EIO);

	/*
	 * If writeback for a btree extent that doesn't belong to a log tree
	 * failed, increment the counter transaction->eb_write_errors.
	 * We do this because while the transaction is running and before it's
	 * committing (when we call filemap_fdata[write|wait]_range against
	 * the btree inode), we might have
	 * btree_inode->i_mapping->a_ops->writepages() called by the VM - if it
	 * returns an error or an error happens during writeback, when we're
	 * committing the transaction we wouldn't know about it, since the pages
	 * can be no longer dirty nor marked anymore for writeback (if a
	 * subsequent modification to the extent buffer didn't happen before the
	 * transaction commit), which makes filemap_fdata[write|wait]_range not
	 * able to find the pages tagged with SetPageError at transaction
	 * commit time. So if this happens we must abort the transaction,
	 * otherwise we commit a super block with btree roots that point to
	 * btree nodes/leafs whose content on disk is invalid - either garbage
	 * or the content of some node/leaf from a past generation that got
	 * cowed or deleted and is no longer valid.
	 *
	 * Note: setting AS_EIO/AS_ENOSPC in the btree inode's i_mapping would
	 * not be enough - we need to distinguish between log tree extents vs
	 * non-log tree extents, and the next filemap_fdatawait_range() call
	 * will catch and clear such errors in the mapping - and that call might
	 * be from a log sync and not from a transaction commit. Also, checking
	 * for the eb flag EXTENT_BUFFER_WRITE_ERR at transaction commit time is
	 * not done and would not be reliable - the eb might have been released
	 * from memory and reading it back again means that flag would not be
	 * set (since it's a runtime flag, not persisted on disk).
	 *
	 * Using the flags below in the btree inode also makes us achieve the
	 * goal of AS_EIO/AS_ENOSPC when writepages() returns success, started
	 * writeback for all dirty pages and before filemap_fdatawait_range()
	 * is called, the writeback for all dirty pages had already finished
	 * with errors - because we were not using AS_EIO/AS_ENOSPC,
	 * filemap_fdatawait_range() would return success, as it could not know
	 * that writeback errors happened (the pages were no longer tagged for
	 * writeback).
	 */
	switch (eb->log_index) {
	case -1:
		set_bit(BTRFS_FS_BTREE_ERR, &fs_info->flags);
		break;
	case 0:
		set_bit(BTRFS_FS_LOG1_ERR, &fs_info->flags);
		break;
	case 1:
		set_bit(BTRFS_FS_LOG2_ERR, &fs_info->flags);
		break;
	default:
		BUG(); /* unexpected, logic error */
	}
}

/*
 * The endio specific version which won't touch any unsafe spinlock in endio
 * context.
 */
static struct extent_buffer *find_extent_buffer_nolock(
		struct btrfs_fs_info *fs_info, u64 start)
{
	struct extent_buffer *eb;

	rcu_read_lock();
	eb = radix_tree_lookup(&fs_info->buffer_radix,
			       start >> fs_info->sectorsize_bits);
	if (eb && atomic_inc_not_zero(&eb->refs)) {
		rcu_read_unlock();
		return eb;
	}
	rcu_read_unlock();
	return NULL;
}

static void end_bbio_meta_write(struct btrfs_bio *bbio)
{
	struct extent_buffer *eb = bbio->private;
	struct btrfs_fs_info *fs_info = eb->fs_info;
	bool uptodate = !bbio->bio.bi_status;
	struct folio_iter fi;
	u32 bio_offset = 0;

	if (!uptodate)
		set_btree_ioerr(eb);

	bio_for_each_folio_all(fi, &bbio->bio) {
		u64 start = eb->start + bio_offset;
		struct folio *folio = fi.folio;
		u32 len = fi.length;

		btrfs_folio_clear_writeback(fs_info, folio, start, len);
		bio_offset += len;
	}

	clear_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags);
	smp_mb__after_atomic();
	wake_up_bit(&eb->bflags, EXTENT_BUFFER_WRITEBACK);

	bio_put(&bbio->bio);
}

static void prepare_eb_write(struct extent_buffer *eb)
{
	u32 nritems;
	unsigned long start;
	unsigned long end;

	clear_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags);

	/* Set btree blocks beyond nritems with 0 to avoid stale content */
	nritems = btrfs_header_nritems(eb);
	if (btrfs_header_level(eb) > 0) {
		end = btrfs_node_key_ptr_offset(eb, nritems);
		memzero_extent_buffer(eb, end, eb->len - end);
	} else {
		/*
		 * Leaf:
		 * header 0 1 2 .. N ... data_N .. data_2 data_1 data_0
		 */
		start = btrfs_item_nr_offset(eb, nritems);
		end = btrfs_item_nr_offset(eb, 0);
		if (nritems == 0)
			end += BTRFS_LEAF_DATA_SIZE(eb->fs_info);
		else
			end += btrfs_item_offset(eb, nritems - 1);
		memzero_extent_buffer(eb, start, end - start);
	}
}

static noinline_for_stack void write_one_eb(struct extent_buffer *eb,
					    struct writeback_control *wbc)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct btrfs_bio *bbio;

	prepare_eb_write(eb);

	bbio = btrfs_bio_alloc(INLINE_EXTENT_BUFFER_PAGES,
			       REQ_OP_WRITE | REQ_META | wbc_to_write_flags(wbc),
			       eb->fs_info, end_bbio_meta_write, eb);
	bbio->bio.bi_iter.bi_sector = eb->start >> SECTOR_SHIFT;
	bio_set_dev(&bbio->bio, fs_info->fs_devices->latest_dev->bdev);
	wbc_init_bio(wbc, &bbio->bio);
	bbio->inode = BTRFS_I(eb->fs_info->btree_inode);
	bbio->file_offset = eb->start;
	if (fs_info->nodesize < PAGE_SIZE) {
		struct folio *folio = eb->folios[0];
		bool ret;

		folio_lock(folio);
		btrfs_subpage_set_writeback(fs_info, folio, eb->start, eb->len);
		if (btrfs_subpage_clear_and_test_dirty(fs_info, folio, eb->start,
						       eb->len)) {
			folio_clear_dirty_for_io(folio);
			wbc->nr_to_write--;
		}
		ret = bio_add_folio(&bbio->bio, folio, eb->len,
				    eb->start - folio_pos(folio));
		ASSERT(ret);
		wbc_account_cgroup_owner(wbc, folio_page(folio, 0), eb->len);
		folio_unlock(folio);
	} else {
		int num_folios = num_extent_folios(eb);

		for (int i = 0; i < num_folios; i++) {
			struct folio *folio = eb->folios[i];
			bool ret;

			folio_lock(folio);
			folio_clear_dirty_for_io(folio);
			folio_start_writeback(folio);
			ret = bio_add_folio(&bbio->bio, folio, folio_size(folio), 0);
			ASSERT(ret);
			wbc_account_cgroup_owner(wbc, folio_page(folio, 0),
						 folio_size(folio));
			wbc->nr_to_write -= folio_nr_pages(folio);
			folio_unlock(folio);
		}
	}
	btrfs_submit_bio(bbio, 0);
}

/*
 * Submit one subpage btree page.
 *
 * The main difference to submit_eb_page() is:
 * - Page locking
 *   For subpage, we don't rely on page locking at all.
 *
 * - Flush write bio
 *   We only flush bio if we may be unable to fit current extent buffers into
 *   current bio.
 *
 * Return >=0 for the number of submitted extent buffers.
 * Return <0 for fatal error.
 */
static int submit_eb_subpage(struct page *page, struct writeback_control *wbc)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);
	struct folio *folio = page_folio(page);
	int submitted = 0;
	u64 page_start = page_offset(page);
	int bit_start = 0;
	int sectors_per_node = fs_info->nodesize >> fs_info->sectorsize_bits;

	/* Lock and write each dirty extent buffers in the range */
	while (bit_start < fs_info->subpage_info->bitmap_nr_bits) {
		struct btrfs_subpage *subpage = folio_get_private(folio);
		struct extent_buffer *eb;
		unsigned long flags;
		u64 start;

		/*
		 * Take private lock to ensure the subpage won't be detached
		 * in the meantime.
		 */
		spin_lock(&page->mapping->private_lock);
		if (!folio_test_private(folio)) {
			spin_unlock(&page->mapping->private_lock);
			break;
		}
		spin_lock_irqsave(&subpage->lock, flags);
		if (!test_bit(bit_start + fs_info->subpage_info->dirty_offset,
			      subpage->bitmaps)) {
			spin_unlock_irqrestore(&subpage->lock, flags);
			spin_unlock(&page->mapping->private_lock);
			bit_start++;
			continue;
		}

		start = page_start + bit_start * fs_info->sectorsize;
		bit_start += sectors_per_node;

		/*
		 * Here we just want to grab the eb without touching extra
		 * spin locks, so call find_extent_buffer_nolock().
		 */
		eb = find_extent_buffer_nolock(fs_info, start);
		spin_unlock_irqrestore(&subpage->lock, flags);
		spin_unlock(&page->mapping->private_lock);

		/*
		 * The eb has already reached 0 refs thus find_extent_buffer()
		 * doesn't return it. We don't need to write back such eb
		 * anyway.
		 */
		if (!eb)
			continue;

		if (lock_extent_buffer_for_io(eb, wbc)) {
			write_one_eb(eb, wbc);
			submitted++;
		}
		free_extent_buffer(eb);
	}
	return submitted;
}

/*
 * Submit all page(s) of one extent buffer.
 *
 * @page:	the page of one extent buffer
 * @eb_context:	to determine if we need to submit this page, if current page
 *		belongs to this eb, we don't need to submit
 *
 * The caller should pass each page in their bytenr order, and here we use
 * @eb_context to determine if we have submitted pages of one extent buffer.
 *
 * If we have, we just skip until we hit a new page that doesn't belong to
 * current @eb_context.
 *
 * If not, we submit all the page(s) of the extent buffer.
 *
 * Return >0 if we have submitted the extent buffer successfully.
 * Return 0 if we don't need to submit the page, as it's already submitted by
 * previous call.
 * Return <0 for fatal error.
 */
static int submit_eb_page(struct page *page, struct btrfs_eb_write_context *ctx)
{
	struct writeback_control *wbc = ctx->wbc;
	struct address_space *mapping = page->mapping;
	struct folio *folio = page_folio(page);
	struct extent_buffer *eb;
	int ret;

	if (!folio_test_private(folio))
		return 0;

	if (btrfs_sb(page->mapping->host->i_sb)->nodesize < PAGE_SIZE)
		return submit_eb_subpage(page, wbc);

	spin_lock(&mapping->private_lock);
	if (!folio_test_private(folio)) {
		spin_unlock(&mapping->private_lock);
		return 0;
	}

	eb = folio_get_private(folio);

	/*
	 * Shouldn't happen and normally this would be a BUG_ON but no point
	 * crashing the machine for something we can survive anyway.
	 */
	if (WARN_ON(!eb)) {
		spin_unlock(&mapping->private_lock);
		return 0;
	}

	if (eb == ctx->eb) {
		spin_unlock(&mapping->private_lock);
		return 0;
	}
	ret = atomic_inc_not_zero(&eb->refs);
	spin_unlock(&mapping->private_lock);
	if (!ret)
		return 0;

	ctx->eb = eb;

	ret = btrfs_check_meta_write_pointer(eb->fs_info, ctx);
	if (ret) {
		if (ret == -EBUSY)
			ret = 0;
		free_extent_buffer(eb);
		return ret;
	}

	if (!lock_extent_buffer_for_io(eb, wbc)) {
		free_extent_buffer(eb);
		return 0;
	}
	/* Implies write in zoned mode. */
	if (ctx->zoned_bg) {
		/* Mark the last eb in the block group. */
		btrfs_schedule_zone_finish_bg(ctx->zoned_bg, eb);
		ctx->zoned_bg->meta_write_pointer += eb->len;
	}
	write_one_eb(eb, wbc);
	free_extent_buffer(eb);
	return 1;
}

int btree_write_cache_pages(struct address_space *mapping,
				   struct writeback_control *wbc)
{
	struct btrfs_eb_write_context ctx = { .wbc = wbc };
	struct btrfs_fs_info *fs_info = BTRFS_I(mapping->host)->root->fs_info;
	int ret = 0;
	int done = 0;
	int nr_to_write_done = 0;
	struct folio_batch fbatch;
	unsigned int nr_folios;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	int scanned = 0;
	xa_mark_t tag;

	folio_batch_init(&fbatch);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
		/*
		 * Start from the beginning does not need to cycle over the
		 * range, mark it as scanned.
		 */
		scanned = (index == 0);
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		scanned = 1;
	}
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
	btrfs_zoned_meta_io_lock(fs_info);
retry:
	if (wbc->sync_mode == WB_SYNC_ALL)
		tag_pages_for_writeback(mapping, index, end);
	while (!done && !nr_to_write_done && (index <= end) &&
	       (nr_folios = filemap_get_folios_tag(mapping, &index, end,
					    tag, &fbatch))) {
		unsigned i;

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			ret = submit_eb_page(&folio->page, &ctx);
			if (ret == 0)
				continue;
			if (ret < 0) {
				done = 1;
				break;
			}

			/*
			 * the filesystem may choose to bump up nr_to_write.
			 * We have to make sure to honor the new nr_to_write
			 * at any time
			 */
			nr_to_write_done = wbc->nr_to_write <= 0;
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}
	/*
	 * If something went wrong, don't allow any metadata write bio to be
	 * submitted.
	 *
	 * This would prevent use-after-free if we had dirty pages not
	 * cleaned up, which can still happen by fuzzed images.
	 *
	 * - Bad extent tree
	 *   Allowing existing tree block to be allocated for other trees.
	 *
	 * - Log tree operations
	 *   Exiting tree blocks get allocated to log tree, bumps its
	 *   generation, then get cleaned in tree re-balance.
	 *   Such tree block will not be written back, since it's clean,
	 *   thus no WRITTEN flag set.
	 *   And after log writes back, this tree block is not traced by
	 *   any dirty extent_io_tree.
	 *
	 * - Offending tree block gets re-dirtied from its original owner
	 *   Since it has bumped generation, no WRITTEN flag, it can be
	 *   reused without COWing. This tree block will not be traced
	 *   by btrfs_transaction::dirty_pages.
	 *
	 *   Now such dirty tree block will not be cleaned by any dirty
	 *   extent io tree. Thus we don't want to submit such wild eb
	 *   if the fs already has error.
	 *
	 * We can get ret > 0 from submit_extent_page() indicating how many ebs
	 * were submitted. Reset it to 0 to avoid false alerts for the caller.
	 */
	if (ret > 0)
		ret = 0;
	if (!ret && BTRFS_FS_ERROR(fs_info))
		ret = -EROFS;

	if (ctx.zoned_bg)
		btrfs_put_block_group(ctx.zoned_bg);
	btrfs_zoned_meta_io_unlock(fs_info);
	return ret;
}

/*
 * Walk the list of dirty pages of the given address space and write all of them.
 *
 * @mapping:   address space structure to write
 * @wbc:       subtract the number of written pages from *@wbc->nr_to_write
 * @bio_ctrl:  holds context for the write, namely the bio
 *
 * If a page is already under I/O, write_cache_pages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
static int extent_write_cache_pages(struct address_space *mapping,
			     struct btrfs_bio_ctrl *bio_ctrl)
{
	struct writeback_control *wbc = bio_ctrl->wbc;
	struct inode *inode = mapping->host;
	int ret = 0;
	int done = 0;
	int nr_to_write_done = 0;
	struct folio_batch fbatch;
	unsigned int nr_folios;
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index;
	int range_whole = 0;
	int scanned = 0;
	xa_mark_t tag;

	/*
	 * We have to hold onto the inode so that ordered extents can do their
	 * work when the IO finishes.  The alternative to this is failing to add
	 * an ordered extent if the igrab() fails there and that is a huge pain
	 * to deal with, so instead just hold onto the inode throughout the
	 * writepages operation.  If it fails here we are freeing up the inode
	 * anyway and we'd rather not waste our time writing out stuff that is
	 * going to be truncated anyway.
	 */
	if (!igrab(inode))
		return 0;

	folio_batch_init(&fbatch);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
		/*
		 * Start from the beginning does not need to cycle over the
		 * range, mark it as scanned.
		 */
		scanned = (index == 0);
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		scanned = 1;
	}

	/*
	 * We do the tagged writepage as long as the snapshot flush bit is set
	 * and we are the first one who do the filemap_flush() on this inode.
	 *
	 * The nr_to_write == LONG_MAX is needed to make sure other flushers do
	 * not race in and drop the bit.
	 */
	if (range_whole && wbc->nr_to_write == LONG_MAX &&
	    test_and_clear_bit(BTRFS_INODE_SNAPSHOT_FLUSH,
			       &BTRFS_I(inode)->runtime_flags))
		wbc->tagged_writepages = 1;

	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	while (!done && !nr_to_write_done && (index <= end) &&
			(nr_folios = filemap_get_folios_tag(mapping, &index,
							end, tag, &fbatch))) {
		unsigned i;

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			done_index = folio_next_index(folio);
			/*
			 * At this point we hold neither the i_pages lock nor
			 * the page lock: the page may be truncated or
			 * invalidated (changing page->mapping to NULL),
			 * or even swizzled back from swapper_space to
			 * tmpfs file mapping
			 */
			if (!folio_trylock(folio)) {
				submit_write_bio(bio_ctrl, 0);
				folio_lock(folio);
			}

			if (unlikely(folio->mapping != mapping)) {
				folio_unlock(folio);
				continue;
			}

			if (!folio_test_dirty(folio)) {
				/* Someone wrote it for us. */
				folio_unlock(folio);
				continue;
			}

			if (wbc->sync_mode != WB_SYNC_NONE) {
				if (folio_test_writeback(folio))
					submit_write_bio(bio_ctrl, 0);
				folio_wait_writeback(folio);
			}

			if (folio_test_writeback(folio) ||
			    !folio_clear_dirty_for_io(folio)) {
				folio_unlock(folio);
				continue;
			}

			ret = __extent_writepage(&folio->page, bio_ctrl);
			if (ret < 0) {
				done = 1;
				break;
			}

			/*
			 * The filesystem may choose to bump up nr_to_write.
			 * We have to make sure to honor the new nr_to_write
			 * at any time.
			 */
			nr_to_write_done = (wbc->sync_mode == WB_SYNC_NONE &&
					    wbc->nr_to_write <= 0);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;

		/*
		 * If we're looping we could run into a page that is locked by a
		 * writer and that writer could be waiting on writeback for a
		 * page in our current bio, and thus deadlock, so flush the
		 * write bio here.
		 */
		submit_write_bio(bio_ctrl, 0);
		goto retry;
	}

	if (wbc->range_cyclic || (wbc->nr_to_write > 0 && range_whole))
		mapping->writeback_index = done_index;

	btrfs_add_delayed_iput(BTRFS_I(inode));
	return ret;
}

/*
 * Submit the pages in the range to bio for call sites which delalloc range has
 * already been ran (aka, ordered extent inserted) and all pages are still
 * locked.
 */
void extent_write_locked_range(struct inode *inode, struct page *locked_page,
			       u64 start, u64 end, struct writeback_control *wbc,
			       bool pages_dirty)
{
	bool found_error = false;
	int ret = 0;
	struct address_space *mapping = inode->i_mapping;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	const u32 sectorsize = fs_info->sectorsize;
	loff_t i_size = i_size_read(inode);
	u64 cur = start;
	struct btrfs_bio_ctrl bio_ctrl = {
		.wbc = wbc,
		.opf = REQ_OP_WRITE | wbc_to_write_flags(wbc),
	};

	if (wbc->no_cgroup_owner)
		bio_ctrl.opf |= REQ_BTRFS_CGROUP_PUNT;

	ASSERT(IS_ALIGNED(start, sectorsize) && IS_ALIGNED(end + 1, sectorsize));

	while (cur <= end) {
		u64 cur_end = min(round_down(cur, PAGE_SIZE) + PAGE_SIZE - 1, end);
		u32 cur_len = cur_end + 1 - cur;
		struct page *page;
		int nr = 0;

		page = find_get_page(mapping, cur >> PAGE_SHIFT);
		ASSERT(PageLocked(page));
		if (pages_dirty && page != locked_page) {
			ASSERT(PageDirty(page));
			clear_page_dirty_for_io(page);
		}

		ret = __extent_writepage_io(BTRFS_I(inode), page, &bio_ctrl,
					    i_size, &nr);
		if (ret == 1)
			goto next_page;

		/* Make sure the mapping tag for page dirty gets cleared. */
		if (nr == 0) {
			set_page_writeback(page);
			end_page_writeback(page);
		}
		if (ret) {
			btrfs_mark_ordered_io_finished(BTRFS_I(inode), page,
						       cur, cur_len, !ret);
			mapping_set_error(page->mapping, ret);
		}
		btrfs_folio_unlock_writer(fs_info, page_folio(page), cur, cur_len);
		if (ret < 0)
			found_error = true;
next_page:
		put_page(page);
		cur = cur_end + 1;
	}

	submit_write_bio(&bio_ctrl, found_error ? ret : 0);
}

int extent_writepages(struct address_space *mapping,
		      struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	int ret = 0;
	struct btrfs_bio_ctrl bio_ctrl = {
		.wbc = wbc,
		.opf = REQ_OP_WRITE | wbc_to_write_flags(wbc),
	};

	/*
	 * Allow only a single thread to do the reloc work in zoned mode to
	 * protect the write pointer updates.
	 */
	btrfs_zoned_data_reloc_lock(BTRFS_I(inode));
	ret = extent_write_cache_pages(mapping, &bio_ctrl);
	submit_write_bio(&bio_ctrl, ret);
	btrfs_zoned_data_reloc_unlock(BTRFS_I(inode));
	return ret;
}

void extent_readahead(struct readahead_control *rac)
{
	struct btrfs_bio_ctrl bio_ctrl = { .opf = REQ_OP_READ | REQ_RAHEAD };
	struct page *pagepool[16];
	struct extent_map *em_cached = NULL;
	u64 prev_em_start = (u64)-1;
	int nr;

	while ((nr = readahead_page_batch(rac, pagepool))) {
		u64 contig_start = readahead_pos(rac);
		u64 contig_end = contig_start + readahead_batch_length(rac) - 1;

		contiguous_readpages(pagepool, nr, contig_start, contig_end,
				&em_cached, &bio_ctrl, &prev_em_start);
	}

	if (em_cached)
		free_extent_map(em_cached);
	submit_one_bio(&bio_ctrl);
}

/*
 * basic invalidate_folio code, this waits on any locked or writeback
 * ranges corresponding to the folio, and then deletes any extent state
 * records from the tree
 */
int extent_invalidate_folio(struct extent_io_tree *tree,
			  struct folio *folio, size_t offset)
{
	struct extent_state *cached_state = NULL;
	u64 start = folio_pos(folio);
	u64 end = start + folio_size(folio) - 1;
	size_t blocksize = folio->mapping->host->i_sb->s_blocksize;

	/* This function is only called for the btree inode */
	ASSERT(tree->owner == IO_TREE_BTREE_INODE_IO);

	start += ALIGN(offset, blocksize);
	if (start > end)
		return 0;

	lock_extent(tree, start, end, &cached_state);
	folio_wait_writeback(folio);

	/*
	 * Currently for btree io tree, only EXTENT_LOCKED is utilized,
	 * so here we only need to unlock the extent range to free any
	 * existing extent state.
	 */
	unlock_extent(tree, start, end, &cached_state);
	return 0;
}

/*
 * a helper for release_folio, this tests for areas of the page that
 * are locked or under IO and drops the related state bits if it is safe
 * to drop the page.
 */
static int try_release_extent_state(struct extent_io_tree *tree,
				    struct page *page, gfp_t mask)
{
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	int ret = 1;

	if (test_range_bit_exists(tree, start, end, EXTENT_LOCKED)) {
		ret = 0;
	} else {
		u32 clear_bits = ~(EXTENT_LOCKED | EXTENT_NODATASUM |
				   EXTENT_DELALLOC_NEW | EXTENT_CTLBITS |
				   EXTENT_QGROUP_RESERVED);

		/*
		 * At this point we can safely clear everything except the
		 * locked bit, the nodatasum bit and the delalloc new bit.
		 * The delalloc new bit will be cleared by ordered extent
		 * completion.
		 */
		ret = __clear_extent_bit(tree, start, end, clear_bits, NULL, NULL);

		/* if clear_extent_bit failed for enomem reasons,
		 * we can't allow the release to continue.
		 */
		if (ret < 0)
			ret = 0;
		else
			ret = 1;
	}
	return ret;
}

/*
 * a helper for release_folio.  As long as there are no locked extents
 * in the range corresponding to the page, both state records and extent
 * map records are removed
 */
int try_release_extent_mapping(struct page *page, gfp_t mask)
{
	struct extent_map *em;
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	struct btrfs_inode *btrfs_inode = BTRFS_I(page->mapping->host);
	struct extent_io_tree *tree = &btrfs_inode->io_tree;
	struct extent_map_tree *map = &btrfs_inode->extent_tree;

	if (gfpflags_allow_blocking(mask) &&
	    page->mapping->host->i_size > SZ_16M) {
		u64 len;
		while (start <= end) {
			struct btrfs_fs_info *fs_info;
			u64 cur_gen;

			len = end - start + 1;
			write_lock(&map->lock);
			em = lookup_extent_mapping(map, start, len);
			if (!em) {
				write_unlock(&map->lock);
				break;
			}
			if ((em->flags & EXTENT_FLAG_PINNED) ||
			    em->start != start) {
				write_unlock(&map->lock);
				free_extent_map(em);
				break;
			}
			if (test_range_bit_exists(tree, em->start,
						  extent_map_end(em) - 1,
						  EXTENT_LOCKED))
				goto next;
			/*
			 * If it's not in the list of modified extents, used
			 * by a fast fsync, we can remove it. If it's being
			 * logged we can safely remove it since fsync took an
			 * extra reference on the em.
			 */
			if (list_empty(&em->list) ||
			    (em->flags & EXTENT_FLAG_LOGGING))
				goto remove_em;
			/*
			 * If it's in the list of modified extents, remove it
			 * only if its generation is older then the current one,
			 * in which case we don't need it for a fast fsync.
			 * Otherwise don't remove it, we could be racing with an
			 * ongoing fast fsync that could miss the new extent.
			 */
			fs_info = btrfs_inode->root->fs_info;
			spin_lock(&fs_info->trans_lock);
			cur_gen = fs_info->generation;
			spin_unlock(&fs_info->trans_lock);
			if (em->generation >= cur_gen)
				goto next;
remove_em:
			/*
			 * We only remove extent maps that are not in the list of
			 * modified extents or that are in the list but with a
			 * generation lower then the current generation, so there
			 * is no need to set the full fsync flag on the inode (it
			 * hurts the fsync performance for workloads with a data
			 * size that exceeds or is close to the system's memory).
			 */
			remove_extent_mapping(map, em);
			/* once for the rb tree */
			free_extent_map(em);
next:
			start = extent_map_end(em);
			write_unlock(&map->lock);

			/* once for us */
			free_extent_map(em);

			cond_resched(); /* Allow large-extent preemption. */
		}
	}
	return try_release_extent_state(tree, page, mask);
}

/*
 * To cache previous fiemap extent
 *
 * Will be used for merging fiemap extent
 */
struct fiemap_cache {
	u64 offset;
	u64 phys;
	u64 len;
	u32 flags;
	bool cached;
};

/*
 * Helper to submit fiemap extent.
 *
 * Will try to merge current fiemap extent specified by @offset, @phys,
 * @len and @flags with cached one.
 * And only when we fails to merge, cached one will be submitted as
 * fiemap extent.
 *
 * Return value is the same as fiemap_fill_next_extent().
 */
static int emit_fiemap_extent(struct fiemap_extent_info *fieinfo,
				struct fiemap_cache *cache,
				u64 offset, u64 phys, u64 len, u32 flags)
{
	int ret = 0;

	/* Set at the end of extent_fiemap(). */
	ASSERT((flags & FIEMAP_EXTENT_LAST) == 0);

	if (!cache->cached)
		goto assign;

	/*
	 * Sanity check, extent_fiemap() should have ensured that new
	 * fiemap extent won't overlap with cached one.
	 * Not recoverable.
	 *
	 * NOTE: Physical address can overlap, due to compression
	 */
	if (cache->offset + cache->len > offset) {
		WARN_ON(1);
		return -EINVAL;
	}

	/*
	 * Only merges fiemap extents if
	 * 1) Their logical addresses are continuous
	 *
	 * 2) Their physical addresses are continuous
	 *    So truly compressed (physical size smaller than logical size)
	 *    extents won't get merged with each other
	 *
	 * 3) Share same flags
	 */
	if (cache->offset + cache->len  == offset &&
	    cache->phys + cache->len == phys  &&
	    cache->flags == flags) {
		cache->len += len;
		return 0;
	}

	/* Not mergeable, need to submit cached one */
	ret = fiemap_fill_next_extent(fieinfo, cache->offset, cache->phys,
				      cache->len, cache->flags);
	cache->cached = false;
	if (ret)
		return ret;
assign:
	cache->cached = true;
	cache->offset = offset;
	cache->phys = phys;
	cache->len = len;
	cache->flags = flags;

	return 0;
}

/*
 * Emit last fiemap cache
 *
 * The last fiemap cache may still be cached in the following case:
 * 0		      4k		    8k
 * |<- Fiemap range ->|
 * |<------------  First extent ----------->|
 *
 * In this case, the first extent range will be cached but not emitted.
 * So we must emit it before ending extent_fiemap().
 */
static int emit_last_fiemap_cache(struct fiemap_extent_info *fieinfo,
				  struct fiemap_cache *cache)
{
	int ret;

	if (!cache->cached)
		return 0;

	ret = fiemap_fill_next_extent(fieinfo, cache->offset, cache->phys,
				      cache->len, cache->flags);
	cache->cached = false;
	if (ret > 0)
		ret = 0;
	return ret;
}

static int fiemap_next_leaf_item(struct btrfs_inode *inode, struct btrfs_path *path)
{
	struct extent_buffer *clone;
	struct btrfs_key key;
	int slot;
	int ret;

	path->slots[0]++;
	if (path->slots[0] < btrfs_header_nritems(path->nodes[0]))
		return 0;

	ret = btrfs_next_leaf(inode->root, path);
	if (ret != 0)
		return ret;

	/*
	 * Don't bother with cloning if there are no more file extent items for
	 * our inode.
	 */
	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
	if (key.objectid != btrfs_ino(inode) || key.type != BTRFS_EXTENT_DATA_KEY)
		return 1;

	/* See the comment at fiemap_search_slot() about why we clone. */
	clone = btrfs_clone_extent_buffer(path->nodes[0]);
	if (!clone)
		return -ENOMEM;

	slot = path->slots[0];
	btrfs_release_path(path);
	path->nodes[0] = clone;
	path->slots[0] = slot;

	return 0;
}

/*
 * Search for the first file extent item that starts at a given file offset or
 * the one that starts immediately before that offset.
 * Returns: 0 on success, < 0 on error, 1 if not found.
 */
static int fiemap_search_slot(struct btrfs_inode *inode, struct btrfs_path *path,
			      u64 file_offset)
{
	const u64 ino = btrfs_ino(inode);
	struct btrfs_root *root = inode->root;
	struct extent_buffer *clone;
	struct btrfs_key key;
	int slot;
	int ret;

	key.objectid = ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = file_offset;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ret;

	if (ret > 0 && path->slots[0] > 0) {
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0] - 1);
		if (key.objectid == ino && key.type == BTRFS_EXTENT_DATA_KEY)
			path->slots[0]--;
	}

	if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
		ret = btrfs_next_leaf(root, path);
		if (ret != 0)
			return ret;

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid != ino || key.type != BTRFS_EXTENT_DATA_KEY)
			return 1;
	}

	/*
	 * We clone the leaf and use it during fiemap. This is because while
	 * using the leaf we do expensive things like checking if an extent is
	 * shared, which can take a long time. In order to prevent blocking
	 * other tasks for too long, we use a clone of the leaf. We have locked
	 * the file range in the inode's io tree, so we know none of our file
	 * extent items can change. This way we avoid blocking other tasks that
	 * want to insert items for other inodes in the same leaf or b+tree
	 * rebalance operations (triggered for example when someone is trying
	 * to push items into this leaf when trying to insert an item in a
	 * neighbour leaf).
	 * We also need the private clone because holding a read lock on an
	 * extent buffer of the subvolume's b+tree will make lockdep unhappy
	 * when we call fiemap_fill_next_extent(), because that may cause a page
	 * fault when filling the user space buffer with fiemap data.
	 */
	clone = btrfs_clone_extent_buffer(path->nodes[0]);
	if (!clone)
		return -ENOMEM;

	slot = path->slots[0];
	btrfs_release_path(path);
	path->nodes[0] = clone;
	path->slots[0] = slot;

	return 0;
}

/*
 * Process a range which is a hole or a prealloc extent in the inode's subvolume
 * btree. If @disk_bytenr is 0, we are dealing with a hole, otherwise a prealloc
 * extent. The end offset (@end) is inclusive.
 */
static int fiemap_process_hole(struct btrfs_inode *inode,
			       struct fiemap_extent_info *fieinfo,
			       struct fiemap_cache *cache,
			       struct extent_state **delalloc_cached_state,
			       struct btrfs_backref_share_check_ctx *backref_ctx,
			       u64 disk_bytenr, u64 extent_offset,
			       u64 extent_gen,
			       u64 start, u64 end)
{
	const u64 i_size = i_size_read(&inode->vfs_inode);
	u64 cur_offset = start;
	u64 last_delalloc_end = 0;
	u32 prealloc_flags = FIEMAP_EXTENT_UNWRITTEN;
	bool checked_extent_shared = false;
	int ret;

	/*
	 * There can be no delalloc past i_size, so don't waste time looking for
	 * it beyond i_size.
	 */
	while (cur_offset < end && cur_offset < i_size) {
		u64 delalloc_start;
		u64 delalloc_end;
		u64 prealloc_start;
		u64 prealloc_len = 0;
		bool delalloc;

		delalloc = btrfs_find_delalloc_in_range(inode, cur_offset, end,
							delalloc_cached_state,
							&delalloc_start,
							&delalloc_end);
		if (!delalloc)
			break;

		/*
		 * If this is a prealloc extent we have to report every section
		 * of it that has no delalloc.
		 */
		if (disk_bytenr != 0) {
			if (last_delalloc_end == 0) {
				prealloc_start = start;
				prealloc_len = delalloc_start - start;
			} else {
				prealloc_start = last_delalloc_end + 1;
				prealloc_len = delalloc_start - prealloc_start;
			}
		}

		if (prealloc_len > 0) {
			if (!checked_extent_shared && fieinfo->fi_extents_max) {
				ret = btrfs_is_data_extent_shared(inode,
								  disk_bytenr,
								  extent_gen,
								  backref_ctx);
				if (ret < 0)
					return ret;
				else if (ret > 0)
					prealloc_flags |= FIEMAP_EXTENT_SHARED;

				checked_extent_shared = true;
			}
			ret = emit_fiemap_extent(fieinfo, cache, prealloc_start,
						 disk_bytenr + extent_offset,
						 prealloc_len, prealloc_flags);
			if (ret)
				return ret;
			extent_offset += prealloc_len;
		}

		ret = emit_fiemap_extent(fieinfo, cache, delalloc_start, 0,
					 delalloc_end + 1 - delalloc_start,
					 FIEMAP_EXTENT_DELALLOC |
					 FIEMAP_EXTENT_UNKNOWN);
		if (ret)
			return ret;

		last_delalloc_end = delalloc_end;
		cur_offset = delalloc_end + 1;
		extent_offset += cur_offset - delalloc_start;
		cond_resched();
	}

	/*
	 * Either we found no delalloc for the whole prealloc extent or we have
	 * a prealloc extent that spans i_size or starts at or after i_size.
	 */
	if (disk_bytenr != 0 && last_delalloc_end < end) {
		u64 prealloc_start;
		u64 prealloc_len;

		if (last_delalloc_end == 0) {
			prealloc_start = start;
			prealloc_len = end + 1 - start;
		} else {
			prealloc_start = last_delalloc_end + 1;
			prealloc_len = end + 1 - prealloc_start;
		}

		if (!checked_extent_shared && fieinfo->fi_extents_max) {
			ret = btrfs_is_data_extent_shared(inode,
							  disk_bytenr,
							  extent_gen,
							  backref_ctx);
			if (ret < 0)
				return ret;
			else if (ret > 0)
				prealloc_flags |= FIEMAP_EXTENT_SHARED;
		}
		ret = emit_fiemap_extent(fieinfo, cache, prealloc_start,
					 disk_bytenr + extent_offset,
					 prealloc_len, prealloc_flags);
		if (ret)
			return ret;
	}

	return 0;
}

static int fiemap_find_last_extent_offset(struct btrfs_inode *inode,
					  struct btrfs_path *path,
					  u64 *last_extent_end_ret)
{
	const u64 ino = btrfs_ino(inode);
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *ei;
	struct btrfs_key key;
	u64 disk_bytenr;
	int ret;

	/*
	 * Lookup the last file extent. We're not using i_size here because
	 * there might be preallocation past i_size.
	 */
	ret = btrfs_lookup_file_extent(NULL, root, path, ino, (u64)-1, 0);
	/* There can't be a file extent item at offset (u64)-1 */
	ASSERT(ret != 0);
	if (ret < 0)
		return ret;

	/*
	 * For a non-existing key, btrfs_search_slot() always leaves us at a
	 * slot > 0, except if the btree is empty, which is impossible because
	 * at least it has the inode item for this inode and all the items for
	 * the root inode 256.
	 */
	ASSERT(path->slots[0] > 0);
	path->slots[0]--;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.objectid != ino || key.type != BTRFS_EXTENT_DATA_KEY) {
		/* No file extent items in the subvolume tree. */
		*last_extent_end_ret = 0;
		return 0;
	}

	/*
	 * For an inline extent, the disk_bytenr is where inline data starts at,
	 * so first check if we have an inline extent item before checking if we
	 * have an implicit hole (disk_bytenr == 0).
	 */
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);
	if (btrfs_file_extent_type(leaf, ei) == BTRFS_FILE_EXTENT_INLINE) {
		*last_extent_end_ret = btrfs_file_extent_end(path);
		return 0;
	}

	/*
	 * Find the last file extent item that is not a hole (when NO_HOLES is
	 * not enabled). This should take at most 2 iterations in the worst
	 * case: we have one hole file extent item at slot 0 of a leaf and
	 * another hole file extent item as the last item in the previous leaf.
	 * This is because we merge file extent items that represent holes.
	 */
	disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, ei);
	while (disk_bytenr == 0) {
		ret = btrfs_previous_item(root, path, ino, BTRFS_EXTENT_DATA_KEY);
		if (ret < 0) {
			return ret;
		} else if (ret > 0) {
			/* No file extent items that are not holes. */
			*last_extent_end_ret = 0;
			return 0;
		}
		leaf = path->nodes[0];
		ei = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, ei);
	}

	*last_extent_end_ret = btrfs_file_extent_end(path);
	return 0;
}

int extent_fiemap(struct btrfs_inode *inode, struct fiemap_extent_info *fieinfo,
		  u64 start, u64 len)
{
	const u64 ino = btrfs_ino(inode);
	struct extent_state *cached_state = NULL;
	struct extent_state *delalloc_cached_state = NULL;
	struct btrfs_path *path;
	struct fiemap_cache cache = { 0 };
	struct btrfs_backref_share_check_ctx *backref_ctx;
	u64 last_extent_end;
	u64 prev_extent_end;
	u64 lockstart;
	u64 lockend;
	bool stopped = false;
	int ret;

	backref_ctx = btrfs_alloc_backref_share_check_ctx();
	path = btrfs_alloc_path();
	if (!backref_ctx || !path) {
		ret = -ENOMEM;
		goto out;
	}

	lockstart = round_down(start, inode->root->fs_info->sectorsize);
	lockend = round_up(start + len, inode->root->fs_info->sectorsize);
	prev_extent_end = lockstart;

	btrfs_inode_lock(inode, BTRFS_ILOCK_SHARED);
	lock_extent(&inode->io_tree, lockstart, lockend, &cached_state);

	ret = fiemap_find_last_extent_offset(inode, path, &last_extent_end);
	if (ret < 0)
		goto out_unlock;
	btrfs_release_path(path);

	path->reada = READA_FORWARD;
	ret = fiemap_search_slot(inode, path, lockstart);
	if (ret < 0) {
		goto out_unlock;
	} else if (ret > 0) {
		/*
		 * No file extent item found, but we may have delalloc between
		 * the current offset and i_size. So check for that.
		 */
		ret = 0;
		goto check_eof_delalloc;
	}

	while (prev_extent_end < lockend) {
		struct extent_buffer *leaf = path->nodes[0];
		struct btrfs_file_extent_item *ei;
		struct btrfs_key key;
		u64 extent_end;
		u64 extent_len;
		u64 extent_offset = 0;
		u64 extent_gen;
		u64 disk_bytenr = 0;
		u64 flags = 0;
		int extent_type;
		u8 compression;

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != ino || key.type != BTRFS_EXTENT_DATA_KEY)
			break;

		extent_end = btrfs_file_extent_end(path);

		/*
		 * The first iteration can leave us at an extent item that ends
		 * before our range's start. Move to the next item.
		 */
		if (extent_end <= lockstart)
			goto next_item;

		backref_ctx->curr_leaf_bytenr = leaf->start;

		/* We have in implicit hole (NO_HOLES feature enabled). */
		if (prev_extent_end < key.offset) {
			const u64 range_end = min(key.offset, lockend) - 1;

			ret = fiemap_process_hole(inode, fieinfo, &cache,
						  &delalloc_cached_state,
						  backref_ctx, 0, 0, 0,
						  prev_extent_end, range_end);
			if (ret < 0) {
				goto out_unlock;
			} else if (ret > 0) {
				/* fiemap_fill_next_extent() told us to stop. */
				stopped = true;
				break;
			}

			/* We've reached the end of the fiemap range, stop. */
			if (key.offset >= lockend) {
				stopped = true;
				break;
			}
		}

		extent_len = extent_end - key.offset;
		ei = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		compression = btrfs_file_extent_compression(leaf, ei);
		extent_type = btrfs_file_extent_type(leaf, ei);
		extent_gen = btrfs_file_extent_generation(leaf, ei);

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, ei);
			if (compression == BTRFS_COMPRESS_NONE)
				extent_offset = btrfs_file_extent_offset(leaf, ei);
		}

		if (compression != BTRFS_COMPRESS_NONE)
			flags |= FIEMAP_EXTENT_ENCODED;

		if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			flags |= FIEMAP_EXTENT_DATA_INLINE;
			flags |= FIEMAP_EXTENT_NOT_ALIGNED;
			ret = emit_fiemap_extent(fieinfo, &cache, key.offset, 0,
						 extent_len, flags);
		} else if (extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			ret = fiemap_process_hole(inode, fieinfo, &cache,
						  &delalloc_cached_state,
						  backref_ctx,
						  disk_bytenr, extent_offset,
						  extent_gen, key.offset,
						  extent_end - 1);
		} else if (disk_bytenr == 0) {
			/* We have an explicit hole. */
			ret = fiemap_process_hole(inode, fieinfo, &cache,
						  &delalloc_cached_state,
						  backref_ctx, 0, 0, 0,
						  key.offset, extent_end - 1);
		} else {
			/* We have a regular extent. */
			if (fieinfo->fi_extents_max) {
				ret = btrfs_is_data_extent_shared(inode,
								  disk_bytenr,
								  extent_gen,
								  backref_ctx);
				if (ret < 0)
					goto out_unlock;
				else if (ret > 0)
					flags |= FIEMAP_EXTENT_SHARED;
			}

			ret = emit_fiemap_extent(fieinfo, &cache, key.offset,
						 disk_bytenr + extent_offset,
						 extent_len, flags);
		}

		if (ret < 0) {
			goto out_unlock;
		} else if (ret > 0) {
			/* fiemap_fill_next_extent() told us to stop. */
			stopped = true;
			break;
		}

		prev_extent_end = extent_end;
next_item:
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out_unlock;
		}

		ret = fiemap_next_leaf_item(inode, path);
		if (ret < 0) {
			goto out_unlock;
		} else if (ret > 0) {
			/* No more file extent items for this inode. */
			break;
		}
		cond_resched();
	}

check_eof_delalloc:
	/*
	 * Release (and free) the path before emitting any final entries to
	 * fiemap_fill_next_extent() to keep lockdep happy. This is because
	 * once we find no more file extent items exist, we may have a
	 * non-cloned leaf, and fiemap_fill_next_extent() can trigger page
	 * faults when copying data to the user space buffer.
	 */
	btrfs_free_path(path);
	path = NULL;

	if (!stopped && prev_extent_end < lockend) {
		ret = fiemap_process_hole(inode, fieinfo, &cache,
					  &delalloc_cached_state, backref_ctx,
					  0, 0, 0, prev_extent_end, lockend - 1);
		if (ret < 0)
			goto out_unlock;
		prev_extent_end = lockend;
	}

	if (cache.cached && cache.offset + cache.len >= last_extent_end) {
		const u64 i_size = i_size_read(&inode->vfs_inode);

		if (prev_extent_end < i_size) {
			u64 delalloc_start;
			u64 delalloc_end;
			bool delalloc;

			delalloc = btrfs_find_delalloc_in_range(inode,
								prev_extent_end,
								i_size - 1,
								&delalloc_cached_state,
								&delalloc_start,
								&delalloc_end);
			if (!delalloc)
				cache.flags |= FIEMAP_EXTENT_LAST;
		} else {
			cache.flags |= FIEMAP_EXTENT_LAST;
		}
	}

	ret = emit_last_fiemap_cache(fieinfo, &cache);

out_unlock:
	unlock_extent(&inode->io_tree, lockstart, lockend, &cached_state);
	btrfs_inode_unlock(inode, BTRFS_ILOCK_SHARED);
out:
	free_extent_state(delalloc_cached_state);
	btrfs_free_backref_share_ctx(backref_ctx);
	btrfs_free_path(path);
	return ret;
}

static void __free_extent_buffer(struct extent_buffer *eb)
{
	kmem_cache_free(extent_buffer_cache, eb);
}

static int extent_buffer_under_io(const struct extent_buffer *eb)
{
	return (test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags) ||
		test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
}

static bool folio_range_has_eb(struct btrfs_fs_info *fs_info, struct folio *folio)
{
	struct btrfs_subpage *subpage;

	lockdep_assert_held(&folio->mapping->private_lock);

	if (folio_test_private(folio)) {
		subpage = folio_get_private(folio);
		if (atomic_read(&subpage->eb_refs))
			return true;
		/*
		 * Even there is no eb refs here, we may still have
		 * end_page_read() call relying on page::private.
		 */
		if (atomic_read(&subpage->readers))
			return true;
	}
	return false;
}

static void detach_extent_buffer_folio(struct extent_buffer *eb, struct folio *folio)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	const bool mapped = !test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	/*
	 * For mapped eb, we're going to change the folio private, which should
	 * be done under the private_lock.
	 */
	if (mapped)
		spin_lock(&folio->mapping->private_lock);

	if (!folio_test_private(folio)) {
		if (mapped)
			spin_unlock(&folio->mapping->private_lock);
		return;
	}

	if (fs_info->nodesize >= PAGE_SIZE) {
		/*
		 * We do this since we'll remove the pages after we've
		 * removed the eb from the radix tree, so we could race
		 * and have this page now attached to the new eb.  So
		 * only clear folio if it's still connected to
		 * this eb.
		 */
		if (folio_test_private(folio) && folio_get_private(folio) == eb) {
			BUG_ON(test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
			BUG_ON(folio_test_dirty(folio));
			BUG_ON(folio_test_writeback(folio));
			/* We need to make sure we haven't be attached to a new eb. */
			folio_detach_private(folio);
		}
		if (mapped)
			spin_unlock(&folio->mapping->private_lock);
		return;
	}

	/*
	 * For subpage, we can have dummy eb with folio private attached.  In
	 * this case, we can directly detach the private as such folio is only
	 * attached to one dummy eb, no sharing.
	 */
	if (!mapped) {
		btrfs_detach_subpage(fs_info, folio);
		return;
	}

	btrfs_folio_dec_eb_refs(fs_info, folio);

	/*
	 * We can only detach the folio private if there are no other ebs in the
	 * page range and no unfinished IO.
	 */
	if (!folio_range_has_eb(fs_info, folio))
		btrfs_detach_subpage(fs_info, folio);

	spin_unlock(&folio->mapping->private_lock);
}

/* Release all pages attached to the extent buffer */
static void btrfs_release_extent_buffer_pages(struct extent_buffer *eb)
{
	ASSERT(!extent_buffer_under_io(eb));

	for (int i = 0; i < INLINE_EXTENT_BUFFER_PAGES; i++) {
		struct folio *folio = eb->folios[i];

		if (!folio)
			continue;

		detach_extent_buffer_folio(eb, folio);

		/* One for when we allocated the folio. */
		folio_put(folio);
	}
}

/*
 * Helper for releasing the extent buffer.
 */
static inline void btrfs_release_extent_buffer(struct extent_buffer *eb)
{
	btrfs_release_extent_buffer_pages(eb);
	btrfs_leak_debug_del_eb(eb);
	__free_extent_buffer(eb);
}

static struct extent_buffer *
__alloc_extent_buffer(struct btrfs_fs_info *fs_info, u64 start,
		      unsigned long len)
{
	struct extent_buffer *eb = NULL;

	eb = kmem_cache_zalloc(extent_buffer_cache, GFP_NOFS|__GFP_NOFAIL);
	eb->start = start;
	eb->len = len;
	eb->fs_info = fs_info;
	init_rwsem(&eb->lock);

	btrfs_leak_debug_add_eb(eb);

	spin_lock_init(&eb->refs_lock);
	atomic_set(&eb->refs, 1);

	ASSERT(len <= BTRFS_MAX_METADATA_BLOCKSIZE);

	return eb;
}

struct extent_buffer *btrfs_clone_extent_buffer(const struct extent_buffer *src)
{
	struct extent_buffer *new;
	int num_folios = num_extent_folios(src);
	int ret;

	new = __alloc_extent_buffer(src->fs_info, src->start, src->len);
	if (new == NULL)
		return NULL;

	/*
	 * Set UNMAPPED before calling btrfs_release_extent_buffer(), as
	 * btrfs_release_extent_buffer() have different behavior for
	 * UNMAPPED subpage extent buffer.
	 */
	set_bit(EXTENT_BUFFER_UNMAPPED, &new->bflags);

	ret = alloc_eb_folio_array(new, 0);
	if (ret) {
		btrfs_release_extent_buffer(new);
		return NULL;
	}

	for (int i = 0; i < num_folios; i++) {
		struct folio *folio = new->folios[i];
		int ret;

		ret = attach_extent_buffer_folio(new, folio, NULL);
		if (ret < 0) {
			btrfs_release_extent_buffer(new);
			return NULL;
		}
		WARN_ON(folio_test_dirty(folio));
	}
	copy_extent_buffer_full(new, src);
	set_extent_buffer_uptodate(new);

	return new;
}

struct extent_buffer *__alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						  u64 start, unsigned long len)
{
	struct extent_buffer *eb;
	int num_folios = 0;
	int ret;

	eb = __alloc_extent_buffer(fs_info, start, len);
	if (!eb)
		return NULL;

	ret = alloc_eb_folio_array(eb, 0);
	if (ret)
		goto err;

	num_folios = num_extent_folios(eb);
	for (int i = 0; i < num_folios; i++) {
		ret = attach_extent_buffer_folio(eb, eb->folios[i], NULL);
		if (ret < 0)
			goto err;
	}

	set_extent_buffer_uptodate(eb);
	btrfs_set_header_nritems(eb, 0);
	set_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	return eb;
err:
	for (int i = 0; i < num_folios; i++) {
		if (eb->folios[i]) {
			detach_extent_buffer_folio(eb, eb->folios[i]);
			__folio_put(eb->folios[i]);
		}
	}
	__free_extent_buffer(eb);
	return NULL;
}

struct extent_buffer *alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						u64 start)
{
	return __alloc_dummy_extent_buffer(fs_info, start, fs_info->nodesize);
}

static void check_buffer_tree_ref(struct extent_buffer *eb)
{
	int refs;
	/*
	 * The TREE_REF bit is first set when the extent_buffer is added
	 * to the radix tree. It is also reset, if unset, when a new reference
	 * is created by find_extent_buffer.
	 *
	 * It is only cleared in two cases: freeing the last non-tree
	 * reference to the extent_buffer when its STALE bit is set or
	 * calling release_folio when the tree reference is the only reference.
	 *
	 * In both cases, care is taken to ensure that the extent_buffer's
	 * pages are not under io. However, release_folio can be concurrently
	 * called with creating new references, which is prone to race
	 * conditions between the calls to check_buffer_tree_ref in those
	 * codepaths and clearing TREE_REF in try_release_extent_buffer.
	 *
	 * The actual lifetime of the extent_buffer in the radix tree is
	 * adequately protected by the refcount, but the TREE_REF bit and
	 * its corresponding reference are not. To protect against this
	 * class of races, we call check_buffer_tree_ref from the codepaths
	 * which trigger io. Note that once io is initiated, TREE_REF can no
	 * longer be cleared, so that is the moment at which any such race is
	 * best fixed.
	 */
	refs = atomic_read(&eb->refs);
	if (refs >= 2 && test_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		return;

	spin_lock(&eb->refs_lock);
	if (!test_and_set_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		atomic_inc(&eb->refs);
	spin_unlock(&eb->refs_lock);
}

static void mark_extent_buffer_accessed(struct extent_buffer *eb)
{
	int num_folios= num_extent_folios(eb);

	check_buffer_tree_ref(eb);

	for (int i = 0; i < num_folios; i++)
		folio_mark_accessed(eb->folios[i]);
}

struct extent_buffer *find_extent_buffer(struct btrfs_fs_info *fs_info,
					 u64 start)
{
	struct extent_buffer *eb;

	eb = find_extent_buffer_nolock(fs_info, start);
	if (!eb)
		return NULL;
	/*
	 * Lock our eb's refs_lock to avoid races with free_extent_buffer().
	 * When we get our eb it might be flagged with EXTENT_BUFFER_STALE and
	 * another task running free_extent_buffer() might have seen that flag
	 * set, eb->refs == 2, that the buffer isn't under IO (dirty and
	 * writeback flags not set) and it's still in the tree (flag
	 * EXTENT_BUFFER_TREE_REF set), therefore being in the process of
	 * decrementing the extent buffer's reference count twice.  So here we
	 * could race and increment the eb's reference count, clear its stale
	 * flag, mark it as dirty and drop our reference before the other task
	 * finishes executing free_extent_buffer, which would later result in
	 * an attempt to free an extent buffer that is dirty.
	 */
	if (test_bit(EXTENT_BUFFER_STALE, &eb->bflags)) {
		spin_lock(&eb->refs_lock);
		spin_unlock(&eb->refs_lock);
	}
	mark_extent_buffer_accessed(eb);
	return eb;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
struct extent_buffer *alloc_test_extent_buffer(struct btrfs_fs_info *fs_info,
					u64 start)
{
	struct extent_buffer *eb, *exists = NULL;
	int ret;

	eb = find_extent_buffer(fs_info, start);
	if (eb)
		return eb;
	eb = alloc_dummy_extent_buffer(fs_info, start);
	if (!eb)
		return ERR_PTR(-ENOMEM);
	eb->fs_info = fs_info;
again:
	ret = radix_tree_preload(GFP_NOFS);
	if (ret) {
		exists = ERR_PTR(ret);
		goto free_eb;
	}
	spin_lock(&fs_info->buffer_lock);
	ret = radix_tree_insert(&fs_info->buffer_radix,
				start >> fs_info->sectorsize_bits, eb);
	spin_unlock(&fs_info->buffer_lock);
	radix_tree_preload_end();
	if (ret == -EEXIST) {
		exists = find_extent_buffer(fs_info, start);
		if (exists)
			goto free_eb;
		else
			goto again;
	}
	check_buffer_tree_ref(eb);
	set_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags);

	return eb;
free_eb:
	btrfs_release_extent_buffer(eb);
	return exists;
}
#endif

static struct extent_buffer *grab_extent_buffer(
		struct btrfs_fs_info *fs_info, struct page *page)
{
	struct folio *folio = page_folio(page);
	struct extent_buffer *exists;

	/*
	 * For subpage case, we completely rely on radix tree to ensure we
	 * don't try to insert two ebs for the same bytenr.  So here we always
	 * return NULL and just continue.
	 */
	if (fs_info->nodesize < PAGE_SIZE)
		return NULL;

	/* Page not yet attached to an extent buffer */
	if (!folio_test_private(folio))
		return NULL;

	/*
	 * We could have already allocated an eb for this page and attached one
	 * so lets see if we can get a ref on the existing eb, and if we can we
	 * know it's good and we can just return that one, else we know we can
	 * just overwrite folio private.
	 */
	exists = folio_get_private(folio);
	if (atomic_inc_not_zero(&exists->refs))
		return exists;

	WARN_ON(PageDirty(page));
	folio_detach_private(folio);
	return NULL;
}

static int check_eb_alignment(struct btrfs_fs_info *fs_info, u64 start)
{
	if (!IS_ALIGNED(start, fs_info->sectorsize)) {
		btrfs_err(fs_info, "bad tree block start %llu", start);
		return -EINVAL;
	}

	if (fs_info->nodesize < PAGE_SIZE &&
	    offset_in_page(start) + fs_info->nodesize > PAGE_SIZE) {
		btrfs_err(fs_info,
		"tree block crosses page boundary, start %llu nodesize %u",
			  start, fs_info->nodesize);
		return -EINVAL;
	}
	if (fs_info->nodesize >= PAGE_SIZE &&
	    !PAGE_ALIGNED(start)) {
		btrfs_err(fs_info,
		"tree block is not page aligned, start %llu nodesize %u",
			  start, fs_info->nodesize);
		return -EINVAL;
	}
	if (!IS_ALIGNED(start, fs_info->nodesize) &&
	    !test_and_set_bit(BTRFS_FS_UNALIGNED_TREE_BLOCK, &fs_info->flags)) {
		btrfs_warn(fs_info,
"tree block not nodesize aligned, start %llu nodesize %u, can be resolved by a full metadata balance",
			      start, fs_info->nodesize);
	}
	return 0;
}


/*
 * Return 0 if eb->folios[i] is attached to btree inode successfully.
 * Return >0 if there is already another extent buffer for the range,
 * and @found_eb_ret would be updated.
 * Return -EAGAIN if the filemap has an existing folio but with different size
 * than @eb.
 * The caller needs to free the existing folios and retry using the same order.
 */
static int attach_eb_folio_to_filemap(struct extent_buffer *eb, int i,
				      struct extent_buffer **found_eb_ret)
{

	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct address_space *mapping = fs_info->btree_inode->i_mapping;
	const unsigned long index = eb->start >> PAGE_SHIFT;
	struct folio *existing_folio;
	int ret;

	ASSERT(found_eb_ret);

	/* Caller should ensure the folio exists. */
	ASSERT(eb->folios[i]);

retry:
	ret = filemap_add_folio(mapping, eb->folios[i], index + i,
				GFP_NOFS | __GFP_NOFAIL);
	if (!ret)
		return 0;

	existing_folio = filemap_lock_folio(mapping, index + i);
	/* The page cache only exists for a very short time, just retry. */
	if (IS_ERR(existing_folio))
		goto retry;

	/* For now, we should only have single-page folios for btree inode. */
	ASSERT(folio_nr_pages(existing_folio) == 1);

	if (folio_size(existing_folio) != folio_size(eb->folios[0])) {
		folio_unlock(existing_folio);
		folio_put(existing_folio);
		return -EAGAIN;
	}

	if (fs_info->nodesize < PAGE_SIZE) {
		/*
		 * We're going to reuse the existing page, can drop our page
		 * and subpage structure now.
		 */
		__free_page(folio_page(eb->folios[i], 0));
		eb->folios[i] = existing_folio;
	} else {
		struct extent_buffer *existing_eb;

		existing_eb = grab_extent_buffer(fs_info,
						 folio_page(existing_folio, 0));
		if (existing_eb) {
			/* The extent buffer still exists, we can use it directly. */
			*found_eb_ret = existing_eb;
			folio_unlock(existing_folio);
			folio_put(existing_folio);
			return 1;
		}
		/* The extent buffer no longer exists, we can reuse the folio. */
		__free_page(folio_page(eb->folios[i], 0));
		eb->folios[i] = existing_folio;
	}
	return 0;
}

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start, u64 owner_root, int level)
{
	unsigned long len = fs_info->nodesize;
	int num_folios;
	int attached = 0;
	struct extent_buffer *eb;
	struct extent_buffer *existing_eb = NULL;
	struct address_space *mapping = fs_info->btree_inode->i_mapping;
	struct btrfs_subpage *prealloc = NULL;
	u64 lockdep_owner = owner_root;
	bool page_contig = true;
	int uptodate = 1;
	int ret;

	if (check_eb_alignment(fs_info, start))
		return ERR_PTR(-EINVAL);

#if BITS_PER_LONG == 32
	if (start >= MAX_LFS_FILESIZE) {
		btrfs_err_rl(fs_info,
		"extent buffer %llu is beyond 32bit page cache limit", start);
		btrfs_err_32bit_limit(fs_info);
		return ERR_PTR(-EOVERFLOW);
	}
	if (start >= BTRFS_32BIT_EARLY_WARN_THRESHOLD)
		btrfs_warn_32bit_limit(fs_info);
#endif

	eb = find_extent_buffer(fs_info, start);
	if (eb)
		return eb;

	eb = __alloc_extent_buffer(fs_info, start, len);
	if (!eb)
		return ERR_PTR(-ENOMEM);

	/*
	 * The reloc trees are just snapshots, so we need them to appear to be
	 * just like any other fs tree WRT lockdep.
	 */
	if (lockdep_owner == BTRFS_TREE_RELOC_OBJECTID)
		lockdep_owner = BTRFS_FS_TREE_OBJECTID;

	btrfs_set_buffer_lockdep_class(lockdep_owner, eb, level);

	/*
	 * Preallocate folio private for subpage case, so that we won't
	 * allocate memory with private_lock nor page lock hold.
	 *
	 * The memory will be freed by attach_extent_buffer_page() or freed
	 * manually if we exit earlier.
	 */
	if (fs_info->nodesize < PAGE_SIZE) {
		prealloc = btrfs_alloc_subpage(fs_info, BTRFS_SUBPAGE_METADATA);
		if (IS_ERR(prealloc)) {
			ret = PTR_ERR(prealloc);
			goto out;
		}
	}

reallocate:
	/* Allocate all pages first. */
	ret = alloc_eb_folio_array(eb, __GFP_NOFAIL);
	if (ret < 0) {
		btrfs_free_subpage(prealloc);
		goto out;
	}

	num_folios = num_extent_folios(eb);
	/* Attach all pages to the filemap. */
	for (int i = 0; i < num_folios; i++) {
		struct folio *folio;

		ret = attach_eb_folio_to_filemap(eb, i, &existing_eb);
		if (ret > 0) {
			ASSERT(existing_eb);
			goto out;
		}

		/*
		 * TODO: Special handling for a corner case where the order of
		 * folios mismatch between the new eb and filemap.
		 *
		 * This happens when:
		 *
		 * - the new eb is using higher order folio
		 *
		 * - the filemap is still using 0-order folios for the range
		 *   This can happen at the previous eb allocation, and we don't
		 *   have higher order folio for the call.
		 *
		 * - the existing eb has already been freed
		 *
		 * In this case, we have to free the existing folios first, and
		 * re-allocate using the same order.
		 * Thankfully this is not going to happen yet, as we're still
		 * using 0-order folios.
		 */
		if (unlikely(ret == -EAGAIN)) {
			ASSERT(0);
			goto reallocate;
		}
		attached++;

		/*
		 * Only after attach_eb_folio_to_filemap(), eb->folios[] is
		 * reliable, as we may choose to reuse the existing page cache
		 * and free the allocated page.
		 */
		folio = eb->folios[i];
		spin_lock(&mapping->private_lock);
		/* Should not fail, as we have preallocated the memory */
		ret = attach_extent_buffer_folio(eb, folio, prealloc);
		ASSERT(!ret);
		/*
		 * To inform we have extra eb under allocation, so that
		 * detach_extent_buffer_page() won't release the folio private
		 * when the eb hasn't yet been inserted into radix tree.
		 *
		 * The ref will be decreased when the eb released the page, in
		 * detach_extent_buffer_page().
		 * Thus needs no special handling in error path.
		 */
		btrfs_folio_inc_eb_refs(fs_info, folio);
		spin_unlock(&mapping->private_lock);

		WARN_ON(btrfs_folio_test_dirty(fs_info, folio, eb->start, eb->len));

		/*
		 * Check if the current page is physically contiguous with previous eb
		 * page.
		 * At this stage, either we allocated a large folio, thus @i
		 * would only be 0, or we fall back to per-page allocation.
		 */
		if (i && folio_page(eb->folios[i - 1], 0) + 1 != folio_page(folio, 0))
			page_contig = false;

		if (!btrfs_folio_test_uptodate(fs_info, folio, eb->start, eb->len))
			uptodate = 0;

		/*
		 * We can't unlock the pages just yet since the extent buffer
		 * hasn't been properly inserted in the radix tree, this
		 * opens a race with btree_release_folio which can free a page
		 * while we are still filling in all pages for the buffer and
		 * we could crash.
		 */
	}
	if (uptodate)
		set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	/* All pages are physically contiguous, can skip cross page handling. */
	if (page_contig)
		eb->addr = folio_address(eb->folios[0]) + offset_in_page(eb->start);
again:
	ret = radix_tree_preload(GFP_NOFS);
	if (ret)
		goto out;

	spin_lock(&fs_info->buffer_lock);
	ret = radix_tree_insert(&fs_info->buffer_radix,
				start >> fs_info->sectorsize_bits, eb);
	spin_unlock(&fs_info->buffer_lock);
	radix_tree_preload_end();
	if (ret == -EEXIST) {
		ret = 0;
		existing_eb = find_extent_buffer(fs_info, start);
		if (existing_eb)
			goto out;
		else
			goto again;
	}
	/* add one reference for the tree */
	check_buffer_tree_ref(eb);
	set_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags);

	/*
	 * Now it's safe to unlock the pages because any calls to
	 * btree_release_folio will correctly detect that a page belongs to a
	 * live buffer and won't free them prematurely.
	 */
	for (int i = 0; i < num_folios; i++)
		unlock_page(folio_page(eb->folios[i], 0));
	return eb;

out:
	WARN_ON(!atomic_dec_and_test(&eb->refs));

	/*
	 * Any attached folios need to be detached before we unlock them.  This
	 * is because when we're inserting our new folios into the mapping, and
	 * then attaching our eb to that folio.  If we fail to insert our folio
	 * we'll lookup the folio for that index, and grab that EB.  We do not
	 * want that to grab this eb, as we're getting ready to free it.  So we
	 * have to detach it first and then unlock it.
	 *
	 * We have to drop our reference and NULL it out here because in the
	 * subpage case detaching does a btrfs_folio_dec_eb_refs() for our eb.
	 * Below when we call btrfs_release_extent_buffer() we will call
	 * detach_extent_buffer_folio() on our remaining pages in the !subpage
	 * case.  If we left eb->folios[i] populated in the subpage case we'd
	 * double put our reference and be super sad.
	 */
	for (int i = 0; i < attached; i++) {
		ASSERT(eb->folios[i]);
		detach_extent_buffer_folio(eb, eb->folios[i]);
		unlock_page(folio_page(eb->folios[i], 0));
		folio_put(eb->folios[i]);
		eb->folios[i] = NULL;
	}
	/*
	 * Now all pages of that extent buffer is unmapped, set UNMAPPED flag,
	 * so it can be cleaned up without utlizing page->mapping.
	 */
	set_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	btrfs_release_extent_buffer(eb);
	if (ret < 0)
		return ERR_PTR(ret);
	ASSERT(existing_eb);
	return existing_eb;
}

static inline void btrfs_release_extent_buffer_rcu(struct rcu_head *head)
{
	struct extent_buffer *eb =
			container_of(head, struct extent_buffer, rcu_head);

	__free_extent_buffer(eb);
}

static int release_extent_buffer(struct extent_buffer *eb)
	__releases(&eb->refs_lock)
{
	lockdep_assert_held(&eb->refs_lock);

	WARN_ON(atomic_read(&eb->refs) == 0);
	if (atomic_dec_and_test(&eb->refs)) {
		if (test_and_clear_bit(EXTENT_BUFFER_IN_TREE, &eb->bflags)) {
			struct btrfs_fs_info *fs_info = eb->fs_info;

			spin_unlock(&eb->refs_lock);

			spin_lock(&fs_info->buffer_lock);
			radix_tree_delete(&fs_info->buffer_radix,
					  eb->start >> fs_info->sectorsize_bits);
			spin_unlock(&fs_info->buffer_lock);
		} else {
			spin_unlock(&eb->refs_lock);
		}

		btrfs_leak_debug_del_eb(eb);
		/* Should be safe to release our pages at this point */
		btrfs_release_extent_buffer_pages(eb);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
		if (unlikely(test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags))) {
			__free_extent_buffer(eb);
			return 1;
		}
#endif
		call_rcu(&eb->rcu_head, btrfs_release_extent_buffer_rcu);
		return 1;
	}
	spin_unlock(&eb->refs_lock);

	return 0;
}

void free_extent_buffer(struct extent_buffer *eb)
{
	int refs;
	if (!eb)
		return;

	refs = atomic_read(&eb->refs);
	while (1) {
		if ((!test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags) && refs <= 3)
		    || (test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags) &&
			refs == 1))
			break;
		if (atomic_try_cmpxchg(&eb->refs, &refs, refs - 1))
			return;
	}

	spin_lock(&eb->refs_lock);
	if (atomic_read(&eb->refs) == 2 &&
	    test_bit(EXTENT_BUFFER_STALE, &eb->bflags) &&
	    !extent_buffer_under_io(eb) &&
	    test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		atomic_dec(&eb->refs);

	/*
	 * I know this is terrible, but it's temporary until we stop tracking
	 * the uptodate bits and such for the extent buffers.
	 */
	release_extent_buffer(eb);
}

void free_extent_buffer_stale(struct extent_buffer *eb)
{
	if (!eb)
		return;

	spin_lock(&eb->refs_lock);
	set_bit(EXTENT_BUFFER_STALE, &eb->bflags);

	if (atomic_read(&eb->refs) == 2 && !extent_buffer_under_io(eb) &&
	    test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags))
		atomic_dec(&eb->refs);
	release_extent_buffer(eb);
}

static void btree_clear_folio_dirty(struct folio *folio)
{
	ASSERT(folio_test_dirty(folio));
	ASSERT(folio_test_locked(folio));
	folio_clear_dirty_for_io(folio);
	xa_lock_irq(&folio->mapping->i_pages);
	if (!folio_test_dirty(folio))
		__xa_clear_mark(&folio->mapping->i_pages,
				folio_index(folio), PAGECACHE_TAG_DIRTY);
	xa_unlock_irq(&folio->mapping->i_pages);
}

static void clear_subpage_extent_buffer_dirty(const struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct folio *folio = eb->folios[0];
	bool last;

	/* btree_clear_folio_dirty() needs page locked. */
	folio_lock(folio);
	last = btrfs_subpage_clear_and_test_dirty(fs_info, folio, eb->start, eb->len);
	if (last)
		btree_clear_folio_dirty(folio);
	folio_unlock(folio);
	WARN_ON(atomic_read(&eb->refs) == 0);
}

void btrfs_clear_buffer_dirty(struct btrfs_trans_handle *trans,
			      struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int num_folios;

	btrfs_assert_tree_write_locked(eb);

	if (trans && btrfs_header_generation(eb) != trans->transid)
		return;

	/*
	 * Instead of clearing the dirty flag off of the buffer, mark it as
	 * EXTENT_BUFFER_ZONED_ZEROOUT. This allows us to preserve
	 * write-ordering in zoned mode, without the need to later re-dirty
	 * the extent_buffer.
	 *
	 * The actual zeroout of the buffer will happen later in
	 * btree_csum_one_bio.
	 */
	if (btrfs_is_zoned(fs_info)) {
		set_bit(EXTENT_BUFFER_ZONED_ZEROOUT, &eb->bflags);
		return;
	}

	if (!test_and_clear_bit(EXTENT_BUFFER_DIRTY, &eb->bflags))
		return;

	percpu_counter_add_batch(&fs_info->dirty_metadata_bytes, -eb->len,
				 fs_info->dirty_metadata_batch);

	if (eb->fs_info->nodesize < PAGE_SIZE)
		return clear_subpage_extent_buffer_dirty(eb);

	num_folios = num_extent_folios(eb);
	for (int i = 0; i < num_folios; i++) {
		struct folio *folio = eb->folios[i];

		if (!folio_test_dirty(folio))
			continue;
		folio_lock(folio);
		btree_clear_folio_dirty(folio);
		folio_unlock(folio);
	}
	WARN_ON(atomic_read(&eb->refs) == 0);
}

void set_extent_buffer_dirty(struct extent_buffer *eb)
{
	int num_folios;
	bool was_dirty;

	check_buffer_tree_ref(eb);

	was_dirty = test_and_set_bit(EXTENT_BUFFER_DIRTY, &eb->bflags);

	num_folios = num_extent_folios(eb);
	WARN_ON(atomic_read(&eb->refs) == 0);
	WARN_ON(!test_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags));

	if (!was_dirty) {
		bool subpage = eb->fs_info->nodesize < PAGE_SIZE;

		/*
		 * For subpage case, we can have other extent buffers in the
		 * same page, and in clear_subpage_extent_buffer_dirty() we
		 * have to clear page dirty without subpage lock held.
		 * This can cause race where our page gets dirty cleared after
		 * we just set it.
		 *
		 * Thankfully, clear_subpage_extent_buffer_dirty() has locked
		 * its page for other reasons, we can use page lock to prevent
		 * the above race.
		 */
		if (subpage)
			lock_page(folio_page(eb->folios[0], 0));
		for (int i = 0; i < num_folios; i++)
			btrfs_folio_set_dirty(eb->fs_info, eb->folios[i],
					      eb->start, eb->len);
		if (subpage)
			unlock_page(folio_page(eb->folios[0], 0));
		percpu_counter_add_batch(&eb->fs_info->dirty_metadata_bytes,
					 eb->len,
					 eb->fs_info->dirty_metadata_batch);
	}
#ifdef CONFIG_BTRFS_DEBUG
	for (int i = 0; i < num_folios; i++)
		ASSERT(folio_test_dirty(eb->folios[i]));
#endif
}

void clear_extent_buffer_uptodate(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int num_folios = num_extent_folios(eb);

	clear_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	for (int i = 0; i < num_folios; i++) {
		struct folio *folio = eb->folios[i];

		if (!folio)
			continue;

		/*
		 * This is special handling for metadata subpage, as regular
		 * btrfs_is_subpage() can not handle cloned/dummy metadata.
		 */
		if (fs_info->nodesize >= PAGE_SIZE)
			folio_clear_uptodate(folio);
		else
			btrfs_subpage_clear_uptodate(fs_info, folio,
						     eb->start, eb->len);
	}
}

void set_extent_buffer_uptodate(struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	int num_folios = num_extent_folios(eb);

	set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	for (int i = 0; i < num_folios; i++) {
		struct folio *folio = eb->folios[i];

		/*
		 * This is special handling for metadata subpage, as regular
		 * btrfs_is_subpage() can not handle cloned/dummy metadata.
		 */
		if (fs_info->nodesize >= PAGE_SIZE)
			folio_mark_uptodate(folio);
		else
			btrfs_subpage_set_uptodate(fs_info, folio,
						   eb->start, eb->len);
	}
}

static void end_bbio_meta_read(struct btrfs_bio *bbio)
{
	struct extent_buffer *eb = bbio->private;
	struct btrfs_fs_info *fs_info = eb->fs_info;
	bool uptodate = !bbio->bio.bi_status;
	struct folio_iter fi;
	u32 bio_offset = 0;

	eb->read_mirror = bbio->mirror_num;

	if (uptodate &&
	    btrfs_validate_extent_buffer(eb, &bbio->parent_check) < 0)
		uptodate = false;

	if (uptodate) {
		set_extent_buffer_uptodate(eb);
	} else {
		clear_extent_buffer_uptodate(eb);
		set_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
	}

	bio_for_each_folio_all(fi, &bbio->bio) {
		struct folio *folio = fi.folio;
		u64 start = eb->start + bio_offset;
		u32 len = fi.length;

		if (uptodate)
			btrfs_folio_set_uptodate(fs_info, folio, start, len);
		else
			btrfs_folio_clear_uptodate(fs_info, folio, start, len);

		bio_offset += len;
	}

	clear_bit(EXTENT_BUFFER_READING, &eb->bflags);
	smp_mb__after_atomic();
	wake_up_bit(&eb->bflags, EXTENT_BUFFER_READING);
	free_extent_buffer(eb);

	bio_put(&bbio->bio);
}

int read_extent_buffer_pages(struct extent_buffer *eb, int wait, int mirror_num,
			     struct btrfs_tree_parent_check *check)
{
	struct btrfs_bio *bbio;
	bool ret;

	if (test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))
		return 0;

	/*
	 * We could have had EXTENT_BUFFER_UPTODATE cleared by the write
	 * operation, which could potentially still be in flight.  In this case
	 * we simply want to return an error.
	 */
	if (unlikely(test_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags)))
		return -EIO;

	/* Someone else is already reading the buffer, just wait for it. */
	if (test_and_set_bit(EXTENT_BUFFER_READING, &eb->bflags))
		goto done;

	clear_bit(EXTENT_BUFFER_READ_ERR, &eb->bflags);
	eb->read_mirror = 0;
	check_buffer_tree_ref(eb);
	atomic_inc(&eb->refs);

	bbio = btrfs_bio_alloc(INLINE_EXTENT_BUFFER_PAGES,
			       REQ_OP_READ | REQ_META, eb->fs_info,
			       end_bbio_meta_read, eb);
	bbio->bio.bi_iter.bi_sector = eb->start >> SECTOR_SHIFT;
	bbio->inode = BTRFS_I(eb->fs_info->btree_inode);
	bbio->file_offset = eb->start;
	memcpy(&bbio->parent_check, check, sizeof(*check));
	if (eb->fs_info->nodesize < PAGE_SIZE) {
		ret = bio_add_folio(&bbio->bio, eb->folios[0], eb->len,
				    eb->start - folio_pos(eb->folios[0]));
		ASSERT(ret);
	} else {
		int num_folios = num_extent_folios(eb);

		for (int i = 0; i < num_folios; i++) {
			struct folio *folio = eb->folios[i];

			ret = bio_add_folio(&bbio->bio, folio, folio_size(folio), 0);
			ASSERT(ret);
		}
	}
	btrfs_submit_bio(bbio, mirror_num);

done:
	if (wait == WAIT_COMPLETE) {
		wait_on_bit_io(&eb->bflags, EXTENT_BUFFER_READING, TASK_UNINTERRUPTIBLE);
		if (!test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))
			return -EIO;
	}

	return 0;
}

static bool report_eb_range(const struct extent_buffer *eb, unsigned long start,
			    unsigned long len)
{
	btrfs_warn(eb->fs_info,
		"access to eb bytenr %llu len %lu out of range start %lu len %lu",
		eb->start, eb->len, start, len);
	WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG));

	return true;
}

/*
 * Check if the [start, start + len) range is valid before reading/writing
 * the eb.
 * NOTE: @start and @len are offset inside the eb, not logical address.
 *
 * Caller should not touch the dst/src memory if this function returns error.
 */
static inline int check_eb_range(const struct extent_buffer *eb,
				 unsigned long start, unsigned long len)
{
	unsigned long offset;

	/* start, start + len should not go beyond eb->len nor overflow */
	if (unlikely(check_add_overflow(start, len, &offset) || offset > eb->len))
		return report_eb_range(eb, start, len);

	return false;
}

void read_extent_buffer(const struct extent_buffer *eb, void *dstv,
			unsigned long start, unsigned long len)
{
	const int unit_size = folio_size(eb->folios[0]);
	size_t cur;
	size_t offset;
	char *dst = (char *)dstv;
	unsigned long i = get_eb_folio_index(eb, start);

	if (check_eb_range(eb, start, len)) {
		/*
		 * Invalid range hit, reset the memory, so callers won't get
		 * some random garbage for their uninitialized memory.
		 */
		memset(dstv, 0, len);
		return;
	}

	if (eb->addr) {
		memcpy(dstv, eb->addr + start, len);
		return;
	}

	offset = get_eb_offset_in_folio(eb, start);

	while (len > 0) {
		char *kaddr;

		cur = min(len, unit_size - offset);
		kaddr = folio_address(eb->folios[i]);
		memcpy(dst, kaddr + offset, cur);

		dst += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

int read_extent_buffer_to_user_nofault(const struct extent_buffer *eb,
				       void __user *dstv,
				       unsigned long start, unsigned long len)
{
	const int unit_size = folio_size(eb->folios[0]);
	size_t cur;
	size_t offset;
	char __user *dst = (char __user *)dstv;
	unsigned long i = get_eb_folio_index(eb, start);
	int ret = 0;

	WARN_ON(start > eb->len);
	WARN_ON(start + len > eb->start + eb->len);

	if (eb->addr) {
		if (copy_to_user_nofault(dstv, eb->addr + start, len))
			ret = -EFAULT;
		return ret;
	}

	offset = get_eb_offset_in_folio(eb, start);

	while (len > 0) {
		char *kaddr;

		cur = min(len, unit_size - offset);
		kaddr = folio_address(eb->folios[i]);
		if (copy_to_user_nofault(dst, kaddr + offset, cur)) {
			ret = -EFAULT;
			break;
		}

		dst += cur;
		len -= cur;
		offset = 0;
		i++;
	}

	return ret;
}

int memcmp_extent_buffer(const struct extent_buffer *eb, const void *ptrv,
			 unsigned long start, unsigned long len)
{
	const int unit_size = folio_size(eb->folios[0]);
	size_t cur;
	size_t offset;
	char *kaddr;
	char *ptr = (char *)ptrv;
	unsigned long i = get_eb_folio_index(eb, start);
	int ret = 0;

	if (check_eb_range(eb, start, len))
		return -EINVAL;

	if (eb->addr)
		return memcmp(ptrv, eb->addr + start, len);

	offset = get_eb_offset_in_folio(eb, start);

	while (len > 0) {
		cur = min(len, unit_size - offset);
		kaddr = folio_address(eb->folios[i]);
		ret = memcmp(ptr, kaddr + offset, cur);
		if (ret)
			break;

		ptr += cur;
		len -= cur;
		offset = 0;
		i++;
	}
	return ret;
}

/*
 * Check that the extent buffer is uptodate.
 *
 * For regular sector size == PAGE_SIZE case, check if @page is uptodate.
 * For subpage case, check if the range covered by the eb has EXTENT_UPTODATE.
 */
static void assert_eb_folio_uptodate(const struct extent_buffer *eb, int i)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct folio *folio = eb->folios[i];

	ASSERT(folio);

	/*
	 * If we are using the commit root we could potentially clear a page
	 * Uptodate while we're using the extent buffer that we've previously
	 * looked up.  We don't want to complain in this case, as the page was
	 * valid before, we just didn't write it out.  Instead we want to catch
	 * the case where we didn't actually read the block properly, which
	 * would have !PageUptodate and !EXTENT_BUFFER_WRITE_ERR.
	 */
	if (test_bit(EXTENT_BUFFER_WRITE_ERR, &eb->bflags))
		return;

	if (fs_info->nodesize < PAGE_SIZE) {
		struct folio *folio = eb->folios[0];

		ASSERT(i == 0);
		if (WARN_ON(!btrfs_subpage_test_uptodate(fs_info, folio,
							 eb->start, eb->len)))
			btrfs_subpage_dump_bitmap(fs_info, folio, eb->start, eb->len);
	} else {
		WARN_ON(!folio_test_uptodate(folio));
	}
}

static void __write_extent_buffer(const struct extent_buffer *eb,
				  const void *srcv, unsigned long start,
				  unsigned long len, bool use_memmove)
{
	const int unit_size = folio_size(eb->folios[0]);
	size_t cur;
	size_t offset;
	char *kaddr;
	char *src = (char *)srcv;
	unsigned long i = get_eb_folio_index(eb, start);
	/* For unmapped (dummy) ebs, no need to check their uptodate status. */
	const bool check_uptodate = !test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	if (check_eb_range(eb, start, len))
		return;

	if (eb->addr) {
		if (use_memmove)
			memmove(eb->addr + start, srcv, len);
		else
			memcpy(eb->addr + start, srcv, len);
		return;
	}

	offset = get_eb_offset_in_folio(eb, start);

	while (len > 0) {
		if (check_uptodate)
			assert_eb_folio_uptodate(eb, i);

		cur = min(len, unit_size - offset);
		kaddr = folio_address(eb->folios[i]);
		if (use_memmove)
			memmove(kaddr + offset, src, cur);
		else
			memcpy(kaddr + offset, src, cur);

		src += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

void write_extent_buffer(const struct extent_buffer *eb, const void *srcv,
			 unsigned long start, unsigned long len)
{
	return __write_extent_buffer(eb, srcv, start, len, false);
}

static void memset_extent_buffer(const struct extent_buffer *eb, int c,
				 unsigned long start, unsigned long len)
{
	const int unit_size = folio_size(eb->folios[0]);
	unsigned long cur = start;

	if (eb->addr) {
		memset(eb->addr + start, c, len);
		return;
	}

	while (cur < start + len) {
		unsigned long index = get_eb_folio_index(eb, cur);
		unsigned int offset = get_eb_offset_in_folio(eb, cur);
		unsigned int cur_len = min(start + len - cur, unit_size - offset);

		assert_eb_folio_uptodate(eb, index);
		memset(folio_address(eb->folios[index]) + offset, c, cur_len);

		cur += cur_len;
	}
}

void memzero_extent_buffer(const struct extent_buffer *eb, unsigned long start,
			   unsigned long len)
{
	if (check_eb_range(eb, start, len))
		return;
	return memset_extent_buffer(eb, 0, start, len);
}

void copy_extent_buffer_full(const struct extent_buffer *dst,
			     const struct extent_buffer *src)
{
	const int unit_size = folio_size(src->folios[0]);
	unsigned long cur = 0;

	ASSERT(dst->len == src->len);

	while (cur < src->len) {
		unsigned long index = get_eb_folio_index(src, cur);
		unsigned long offset = get_eb_offset_in_folio(src, cur);
		unsigned long cur_len = min(src->len, unit_size - offset);
		void *addr = folio_address(src->folios[index]) + offset;

		write_extent_buffer(dst, addr, cur, cur_len);

		cur += cur_len;
	}
}

void copy_extent_buffer(const struct extent_buffer *dst,
			const struct extent_buffer *src,
			unsigned long dst_offset, unsigned long src_offset,
			unsigned long len)
{
	const int unit_size = folio_size(dst->folios[0]);
	u64 dst_len = dst->len;
	size_t cur;
	size_t offset;
	char *kaddr;
	unsigned long i = get_eb_folio_index(dst, dst_offset);

	if (check_eb_range(dst, dst_offset, len) ||
	    check_eb_range(src, src_offset, len))
		return;

	WARN_ON(src->len != dst_len);

	offset = get_eb_offset_in_folio(dst, dst_offset);

	while (len > 0) {
		assert_eb_folio_uptodate(dst, i);

		cur = min(len, (unsigned long)(unit_size - offset));

		kaddr = folio_address(dst->folios[i]);
		read_extent_buffer(src, kaddr + offset, src_offset, cur);

		src_offset += cur;
		len -= cur;
		offset = 0;
		i++;
	}
}

/*
 * Calculate the folio and offset of the byte containing the given bit number.
 *
 * @eb:           the extent buffer
 * @start:        offset of the bitmap item in the extent buffer
 * @nr:           bit number
 * @folio_index:  return index of the folio in the extent buffer that contains
 *                the given bit number
 * @folio_offset: return offset into the folio given by folio_index
 *
 * This helper hides the ugliness of finding the byte in an extent buffer which
 * contains a given bit.
 */
static inline void eb_bitmap_offset(const struct extent_buffer *eb,
				    unsigned long start, unsigned long nr,
				    unsigned long *folio_index,
				    size_t *folio_offset)
{
	size_t byte_offset = BIT_BYTE(nr);
	size_t offset;

	/*
	 * The byte we want is the offset of the extent buffer + the offset of
	 * the bitmap item in the extent buffer + the offset of the byte in the
	 * bitmap item.
	 */
	offset = start + offset_in_folio(eb->folios[0], eb->start) + byte_offset;

	*folio_index = offset >> folio_shift(eb->folios[0]);
	*folio_offset = offset_in_folio(eb->folios[0], offset);
}

/*
 * Determine whether a bit in a bitmap item is set.
 *
 * @eb:     the extent buffer
 * @start:  offset of the bitmap item in the extent buffer
 * @nr:     bit number to test
 */
int extent_buffer_test_bit(const struct extent_buffer *eb, unsigned long start,
			   unsigned long nr)
{
	unsigned long i;
	size_t offset;
	u8 *kaddr;

	eb_bitmap_offset(eb, start, nr, &i, &offset);
	assert_eb_folio_uptodate(eb, i);
	kaddr = folio_address(eb->folios[i]);
	return 1U & (kaddr[offset] >> (nr & (BITS_PER_BYTE - 1)));
}

static u8 *extent_buffer_get_byte(const struct extent_buffer *eb, unsigned long bytenr)
{
	unsigned long index = get_eb_folio_index(eb, bytenr);

	if (check_eb_range(eb, bytenr, 1))
		return NULL;
	return folio_address(eb->folios[index]) + get_eb_offset_in_folio(eb, bytenr);
}

/*
 * Set an area of a bitmap to 1.
 *
 * @eb:     the extent buffer
 * @start:  offset of the bitmap item in the extent buffer
 * @pos:    bit number of the first bit
 * @len:    number of bits to set
 */
void extent_buffer_bitmap_set(const struct extent_buffer *eb, unsigned long start,
			      unsigned long pos, unsigned long len)
{
	unsigned int first_byte = start + BIT_BYTE(pos);
	unsigned int last_byte = start + BIT_BYTE(pos + len - 1);
	const bool same_byte = (first_byte == last_byte);
	u8 mask = BITMAP_FIRST_BYTE_MASK(pos);
	u8 *kaddr;

	if (same_byte)
		mask &= BITMAP_LAST_BYTE_MASK(pos + len);

	/* Handle the first byte. */
	kaddr = extent_buffer_get_byte(eb, first_byte);
	*kaddr |= mask;
	if (same_byte)
		return;

	/* Handle the byte aligned part. */
	ASSERT(first_byte + 1 <= last_byte);
	memset_extent_buffer(eb, 0xff, first_byte + 1, last_byte - first_byte - 1);

	/* Handle the last byte. */
	kaddr = extent_buffer_get_byte(eb, last_byte);
	*kaddr |= BITMAP_LAST_BYTE_MASK(pos + len);
}


/*
 * Clear an area of a bitmap.
 *
 * @eb:     the extent buffer
 * @start:  offset of the bitmap item in the extent buffer
 * @pos:    bit number of the first bit
 * @len:    number of bits to clear
 */
void extent_buffer_bitmap_clear(const struct extent_buffer *eb,
				unsigned long start, unsigned long pos,
				unsigned long len)
{
	unsigned int first_byte = start + BIT_BYTE(pos);
	unsigned int last_byte = start + BIT_BYTE(pos + len - 1);
	const bool same_byte = (first_byte == last_byte);
	u8 mask = BITMAP_FIRST_BYTE_MASK(pos);
	u8 *kaddr;

	if (same_byte)
		mask &= BITMAP_LAST_BYTE_MASK(pos + len);

	/* Handle the first byte. */
	kaddr = extent_buffer_get_byte(eb, first_byte);
	*kaddr &= ~mask;
	if (same_byte)
		return;

	/* Handle the byte aligned part. */
	ASSERT(first_byte + 1 <= last_byte);
	memset_extent_buffer(eb, 0, first_byte + 1, last_byte - first_byte - 1);

	/* Handle the last byte. */
	kaddr = extent_buffer_get_byte(eb, last_byte);
	*kaddr &= ~BITMAP_LAST_BYTE_MASK(pos + len);
}

static inline bool areas_overlap(unsigned long src, unsigned long dst, unsigned long len)
{
	unsigned long distance = (src > dst) ? src - dst : dst - src;
	return distance < len;
}

void memcpy_extent_buffer(const struct extent_buffer *dst,
			  unsigned long dst_offset, unsigned long src_offset,
			  unsigned long len)
{
	const int unit_size = folio_size(dst->folios[0]);
	unsigned long cur_off = 0;

	if (check_eb_range(dst, dst_offset, len) ||
	    check_eb_range(dst, src_offset, len))
		return;

	if (dst->addr) {
		const bool use_memmove = areas_overlap(src_offset, dst_offset, len);

		if (use_memmove)
			memmove(dst->addr + dst_offset, dst->addr + src_offset, len);
		else
			memcpy(dst->addr + dst_offset, dst->addr + src_offset, len);
		return;
	}

	while (cur_off < len) {
		unsigned long cur_src = cur_off + src_offset;
		unsigned long folio_index = get_eb_folio_index(dst, cur_src);
		unsigned long folio_off = get_eb_offset_in_folio(dst, cur_src);
		unsigned long cur_len = min(src_offset + len - cur_src,
					    unit_size - folio_off);
		void *src_addr = folio_address(dst->folios[folio_index]) + folio_off;
		const bool use_memmove = areas_overlap(src_offset + cur_off,
						       dst_offset + cur_off, cur_len);

		__write_extent_buffer(dst, src_addr, dst_offset + cur_off, cur_len,
				      use_memmove);
		cur_off += cur_len;
	}
}

void memmove_extent_buffer(const struct extent_buffer *dst,
			   unsigned long dst_offset, unsigned long src_offset,
			   unsigned long len)
{
	unsigned long dst_end = dst_offset + len - 1;
	unsigned long src_end = src_offset + len - 1;

	if (check_eb_range(dst, dst_offset, len) ||
	    check_eb_range(dst, src_offset, len))
		return;

	if (dst_offset < src_offset) {
		memcpy_extent_buffer(dst, dst_offset, src_offset, len);
		return;
	}

	if (dst->addr) {
		memmove(dst->addr + dst_offset, dst->addr + src_offset, len);
		return;
	}

	while (len > 0) {
		unsigned long src_i;
		size_t cur;
		size_t dst_off_in_folio;
		size_t src_off_in_folio;
		void *src_addr;
		bool use_memmove;

		src_i = get_eb_folio_index(dst, src_end);

		dst_off_in_folio = get_eb_offset_in_folio(dst, dst_end);
		src_off_in_folio = get_eb_offset_in_folio(dst, src_end);

		cur = min_t(unsigned long, len, src_off_in_folio + 1);
		cur = min(cur, dst_off_in_folio + 1);

		src_addr = folio_address(dst->folios[src_i]) + src_off_in_folio -
					 cur + 1;
		use_memmove = areas_overlap(src_end - cur + 1, dst_end - cur + 1,
					    cur);

		__write_extent_buffer(dst, src_addr, dst_end - cur + 1, cur,
				      use_memmove);

		dst_end -= cur;
		src_end -= cur;
		len -= cur;
	}
}

#define GANG_LOOKUP_SIZE	16
static struct extent_buffer *get_next_extent_buffer(
		struct btrfs_fs_info *fs_info, struct page *page, u64 bytenr)
{
	struct extent_buffer *gang[GANG_LOOKUP_SIZE];
	struct extent_buffer *found = NULL;
	u64 page_start = page_offset(page);
	u64 cur = page_start;

	ASSERT(in_range(bytenr, page_start, PAGE_SIZE));
	lockdep_assert_held(&fs_info->buffer_lock);

	while (cur < page_start + PAGE_SIZE) {
		int ret;
		int i;

		ret = radix_tree_gang_lookup(&fs_info->buffer_radix,
				(void **)gang, cur >> fs_info->sectorsize_bits,
				min_t(unsigned int, GANG_LOOKUP_SIZE,
				      PAGE_SIZE / fs_info->nodesize));
		if (ret == 0)
			goto out;
		for (i = 0; i < ret; i++) {
			/* Already beyond page end */
			if (gang[i]->start >= page_start + PAGE_SIZE)
				goto out;
			/* Found one */
			if (gang[i]->start >= bytenr) {
				found = gang[i];
				goto out;
			}
		}
		cur = gang[ret - 1]->start + gang[ret - 1]->len;
	}
out:
	return found;
}

static int try_release_subpage_extent_buffer(struct page *page)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);
	u64 cur = page_offset(page);
	const u64 end = page_offset(page) + PAGE_SIZE;
	int ret;

	while (cur < end) {
		struct extent_buffer *eb = NULL;

		/*
		 * Unlike try_release_extent_buffer() which uses folio private
		 * to grab buffer, for subpage case we rely on radix tree, thus
		 * we need to ensure radix tree consistency.
		 *
		 * We also want an atomic snapshot of the radix tree, thus go
		 * with spinlock rather than RCU.
		 */
		spin_lock(&fs_info->buffer_lock);
		eb = get_next_extent_buffer(fs_info, page, cur);
		if (!eb) {
			/* No more eb in the page range after or at cur */
			spin_unlock(&fs_info->buffer_lock);
			break;
		}
		cur = eb->start + eb->len;

		/*
		 * The same as try_release_extent_buffer(), to ensure the eb
		 * won't disappear out from under us.
		 */
		spin_lock(&eb->refs_lock);
		if (atomic_read(&eb->refs) != 1 || extent_buffer_under_io(eb)) {
			spin_unlock(&eb->refs_lock);
			spin_unlock(&fs_info->buffer_lock);
			break;
		}
		spin_unlock(&fs_info->buffer_lock);

		/*
		 * If tree ref isn't set then we know the ref on this eb is a
		 * real ref, so just return, this eb will likely be freed soon
		 * anyway.
		 */
		if (!test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags)) {
			spin_unlock(&eb->refs_lock);
			break;
		}

		/*
		 * Here we don't care about the return value, we will always
		 * check the folio private at the end.  And
		 * release_extent_buffer() will release the refs_lock.
		 */
		release_extent_buffer(eb);
	}
	/*
	 * Finally to check if we have cleared folio private, as if we have
	 * released all ebs in the page, the folio private should be cleared now.
	 */
	spin_lock(&page->mapping->private_lock);
	if (!folio_test_private(page_folio(page)))
		ret = 1;
	else
		ret = 0;
	spin_unlock(&page->mapping->private_lock);
	return ret;

}

int try_release_extent_buffer(struct page *page)
{
	struct folio *folio = page_folio(page);
	struct extent_buffer *eb;

	if (btrfs_sb(page->mapping->host->i_sb)->nodesize < PAGE_SIZE)
		return try_release_subpage_extent_buffer(page);

	/*
	 * We need to make sure nobody is changing folio private, as we rely on
	 * folio private as the pointer to extent buffer.
	 */
	spin_lock(&page->mapping->private_lock);
	if (!folio_test_private(folio)) {
		spin_unlock(&page->mapping->private_lock);
		return 1;
	}

	eb = folio_get_private(folio);
	BUG_ON(!eb);

	/*
	 * This is a little awful but should be ok, we need to make sure that
	 * the eb doesn't disappear out from under us while we're looking at
	 * this page.
	 */
	spin_lock(&eb->refs_lock);
	if (atomic_read(&eb->refs) != 1 || extent_buffer_under_io(eb)) {
		spin_unlock(&eb->refs_lock);
		spin_unlock(&page->mapping->private_lock);
		return 0;
	}
	spin_unlock(&page->mapping->private_lock);

	/*
	 * If tree ref isn't set then we know the ref on this eb is a real ref,
	 * so just return, this page will likely be freed soon anyway.
	 */
	if (!test_and_clear_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags)) {
		spin_unlock(&eb->refs_lock);
		return 0;
	}

	return release_extent_buffer(eb);
}

/*
 * Attempt to readahead a child block.
 *
 * @fs_info:	the fs_info
 * @bytenr:	bytenr to read
 * @owner_root: objectid of the root that owns this eb
 * @gen:	generation for the uptodate check, can be 0
 * @level:	level for the eb
 *
 * Attempt to readahead a tree block at @bytenr.  If @gen is 0 then we do a
 * normal uptodate check of the eb, without checking the generation.  If we have
 * to read the block we will not block on anything.
 */
void btrfs_readahead_tree_block(struct btrfs_fs_info *fs_info,
				u64 bytenr, u64 owner_root, u64 gen, int level)
{
	struct btrfs_tree_parent_check check = {
		.has_first_key = 0,
		.level = level,
		.transid = gen
	};
	struct extent_buffer *eb;
	int ret;

	eb = btrfs_find_create_tree_block(fs_info, bytenr, owner_root, level);
	if (IS_ERR(eb))
		return;

	if (btrfs_buffer_uptodate(eb, gen, 1)) {
		free_extent_buffer(eb);
		return;
	}

	ret = read_extent_buffer_pages(eb, WAIT_NONE, 0, &check);
	if (ret < 0)
		free_extent_buffer_stale(eb);
	else
		free_extent_buffer(eb);
}

/*
 * Readahead a node's child block.
 *
 * @node:	parent node we're reading from
 * @slot:	slot in the parent node for the child we want to read
 *
 * A helper for btrfs_readahead_tree_block, we simply read the bytenr pointed at
 * the slot in the node provided.
 */
void btrfs_readahead_node_child(struct extent_buffer *node, int slot)
{
	btrfs_readahead_tree_block(node->fs_info,
				   btrfs_node_blockptr(node, slot),
				   btrfs_header_owner(node),
				   btrfs_node_ptr_generation(node, slot),
				   btrfs_header_level(node) - 1);
}
