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
#include "extent_io.h"
#include "extent-io-tree.h"
#include "extent_map.h"
#include "ctree.h"
#include "btrfs_inode.h"
#include "bio.h"
#include "locking.h"
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
	"BTRFS: buffer leak start %llu len %u refs %d bflags %lu owner %llu\n",
		       eb->start, eb->len, atomic_read(&eb->refs), eb->bflags,
		       btrfs_header_owner(eb));
		list_del(&eb->leak_list);
		WARN_ON_ONCE(1);
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

	/*
	 * The sectors of the page which are going to be submitted by
	 * extent_writepage_io().
	 * This is to avoid touching ranges covered by compression/inline.
	 */
	unsigned long submit_bitmap;
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
		btrfs_submit_bbio(bbio, 0);

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
						sizeof(struct extent_buffer), 0, 0,
						NULL);
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

static void process_one_folio(struct btrfs_fs_info *fs_info,
			      struct folio *folio, const struct folio *locked_folio,
			      unsigned long page_ops, u64 start, u64 end)
{
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

	if (folio != locked_folio && (page_ops & PAGE_UNLOCK))
		btrfs_folio_end_lock(fs_info, folio, start, len);
}

static void __process_folios_contig(struct address_space *mapping,
				    const struct folio *locked_folio, u64 start,
				    u64 end, unsigned long page_ops)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(mapping->host);
	pgoff_t index = start >> PAGE_SHIFT;
	pgoff_t end_index = end >> PAGE_SHIFT;
	struct folio_batch fbatch;
	int i;

	folio_batch_init(&fbatch);
	while (index <= end_index) {
		int found_folios;

		found_folios = filemap_get_folios_contig(mapping, &index,
				end_index, &fbatch);
		for (i = 0; i < found_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			process_one_folio(fs_info, folio, locked_folio,
					  page_ops, start, end);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

static noinline void unlock_delalloc_folio(const struct inode *inode,
					   const struct folio *locked_folio,
					   u64 start, u64 end)
{
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;

	ASSERT(locked_folio);
	if (index == locked_folio->index && end_index == index)
		return;

	__process_folios_contig(inode->i_mapping, locked_folio, start, end,
				PAGE_UNLOCK);
}

static noinline int lock_delalloc_folios(struct inode *inode,
					 const struct folio *locked_folio,
					 u64 start, u64 end)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index = start >> PAGE_SHIFT;
	pgoff_t end_index = end >> PAGE_SHIFT;
	u64 processed_end = start;
	struct folio_batch fbatch;

	if (index == locked_folio->index && index == end_index)
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
			u64 range_start;
			u32 range_len;

			if (folio == locked_folio)
				continue;

			folio_lock(folio);
			if (!folio_test_dirty(folio) || folio->mapping != mapping) {
				folio_unlock(folio);
				goto out;
			}
			range_start = max_t(u64, folio_pos(folio), start);
			range_len = min_t(u64, folio_pos(folio) + folio_size(folio),
					  end + 1) - range_start;
			btrfs_folio_set_lock(fs_info, folio, range_start, range_len);

			processed_end = range_start + range_len - 1;
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	return 0;
out:
	folio_batch_release(&fbatch);
	if (processed_end > start)
		unlock_delalloc_folio(inode, locked_folio, start, processed_end);
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
						 struct folio *locked_folio,
						 u64 *start, u64 *end)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
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

	/* The range should at least cover part of the folio */
	ASSERT(!(orig_start >= folio_pos(locked_folio) + folio_size(locked_folio) ||
		 orig_end <= folio_pos(locked_folio)));
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
	 * start comes from the offset of locked_folio.  We have to lock
	 * folios in order, so we can't process delalloc bytes before
	 * locked_folio
	 */
	if (delalloc_start < *start)
		delalloc_start = *start;

	/*
	 * make sure to limit the number of folios we try to lock down
	 */
	if (delalloc_end + 1 - delalloc_start > max_bytes)
		delalloc_end = delalloc_start + max_bytes - 1;

	/* step two, lock all the folioss after the folios that has start */
	ret = lock_delalloc_folios(inode, locked_folio, delalloc_start,
				   delalloc_end);
	ASSERT(!ret || ret == -EAGAIN);
	if (ret == -EAGAIN) {
		/* some of the folios are gone, lets avoid looping by
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

	unlock_extent(tree, delalloc_start, delalloc_end, &cached_state);
	if (!ret) {
		unlock_delalloc_folio(inode, locked_folio, delalloc_start,
				      delalloc_end);
		cond_resched();
		goto again;
	}
	*start = delalloc_start;
	*end = delalloc_end;
out_failed:
	return found;
}

void extent_clear_unlock_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
				  const struct folio *locked_folio,
				  struct extent_state **cached,
				  u32 clear_bits, unsigned long page_ops)
{
	clear_extent_bit(&inode->io_tree, start, end, clear_bits, cached);

	__process_folios_contig(inode->vfs_inode.i_mapping, locked_folio, start,
				end, page_ops);
}

static bool btrfs_verify_folio(struct folio *folio, u64 start, u32 len)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);

	if (!fsverity_active(folio->mapping->host) ||
	    btrfs_folio_test_uptodate(fs_info, folio, start, len) ||
	    start >= i_size_read(folio->mapping->host))
		return true;
	return fsverity_verify_folio(folio);
}

static void end_folio_read(struct folio *folio, bool uptodate, u64 start, u32 len)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);

	ASSERT(folio_pos(folio) <= start &&
	       start + len <= folio_pos(folio) + folio_size(folio));

	if (uptodate && btrfs_verify_folio(folio, start, len))
		btrfs_folio_set_uptodate(fs_info, folio, start, len);
	else
		btrfs_folio_clear_uptodate(fs_info, folio, start, len);

	if (!btrfs_is_subpage(fs_info, folio))
		folio_unlock(folio);
	else
		btrfs_folio_end_lock(fs_info, folio, start, len);
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
	struct btrfs_fs_info *fs_info = bbio->fs_info;
	struct bio *bio = &bbio->bio;
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter fi;
	const u32 sectorsize = fs_info->sectorsize;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_folio_all(fi, bio) {
		struct folio *folio = fi.folio;
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

		btrfs_finish_ordered_extent(bbio->ordered, folio, start, len,
					    !error);
		if (error)
			mapping_set_error(folio->mapping, error);
		btrfs_folio_clear_writeback(fs_info, folio, start, len);
	}

	bio_put(bio);
}

static void begin_folio_read(struct btrfs_fs_info *fs_info, struct folio *folio)
{
	ASSERT(folio_test_locked(folio));
	if (!btrfs_is_subpage(fs_info, folio))
		return;

	ASSERT(folio_test_private(folio));
	btrfs_folio_set_lock(fs_info, folio, folio_pos(folio), folio_size(folio));
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
	struct btrfs_fs_info *fs_info = bbio->fs_info;
	struct bio *bio = &bbio->bio;
	struct folio_iter fi;
	const u32 sectorsize = fs_info->sectorsize;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_folio_all(fi, &bbio->bio) {
		bool uptodate = !bio->bi_status;
		struct folio *folio = fi.folio;
		struct inode *inode = folio->mapping->host;
		u64 start;
		u64 end;
		u32 len;

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

			/*
			 * Zero out the remaining part if this range straddles
			 * i_size.
			 *
			 * Here we should only zero the range inside the folio,
			 * not touch anything else.
			 *
			 * NOTE: i_size is exclusive while end is inclusive and
			 * folio_contains() takes PAGE_SIZE units.
			 */
			if (folio_contains(folio, i_size >> PAGE_SHIFT) &&
			    i_size <= end) {
				u32 zero_start = max(offset_in_folio(folio, i_size),
						     offset_in_folio(folio, start));
				u32 zero_len = offset_in_folio(folio, end) + 1 -
					       zero_start;

				folio_zero_range(folio, zero_start, zero_len);
			}
		}

		/* Update page status and unlock. */
		end_folio_read(folio, uptodate, start, len);
	}
	bio_put(bio);
}

/*
 * Populate every free slot in a provided array with folios using GFP_NOFS.
 *
 * @nr_folios:   number of folios to allocate
 * @folio_array: the array to fill with folios; any existing non-NULL entries in
 *		 the array will be skipped
 *
 * Return: 0        if all folios were able to be allocated;
 *         -ENOMEM  otherwise, the partially allocated folios would be freed and
 *                  the array slots zeroed
 */
int btrfs_alloc_folio_array(unsigned int nr_folios, struct folio **folio_array)
{
	for (int i = 0; i < nr_folios; i++) {
		if (folio_array[i])
			continue;
		folio_array[i] = folio_alloc(GFP_NOFS, 0);
		if (!folio_array[i])
			goto error;
	}
	return 0;
error:
	for (int i = 0; i < nr_folios; i++) {
		if (folio_array[i])
			folio_put(folio_array[i]);
	}
	return -ENOMEM;
}

/*
 * Populate every free slot in a provided array with pages, using GFP_NOFS.
 *
 * @nr_pages:   number of pages to allocate
 * @page_array: the array to fill with pages; any existing non-null entries in
 *		the array will be skipped
 * @nofail:	whether using __GFP_NOFAIL flag
 *
 * Return: 0        if all pages were able to be allocated;
 *         -ENOMEM  otherwise, the partially allocated pages would be freed and
 *                  the array slots zeroed
 */
int btrfs_alloc_page_array(unsigned int nr_pages, struct page **page_array,
			   bool nofail)
{
	const gfp_t gfp = nofail ? (GFP_NOFS | __GFP_NOFAIL) : GFP_NOFS;
	unsigned int allocated;

	for (allocated = 0; allocated < nr_pages;) {
		unsigned int last = allocated;

		allocated = alloc_pages_bulk(gfp, nr_pages, page_array);
		if (unlikely(allocated == last)) {
			/* No progress, fail and do cleanup. */
			for (int i = 0; i < allocated; i++) {
				__free_page(page_array[i]);
				page_array[i] = NULL;
			}
			return -ENOMEM;
		}
	}
	return 0;
}

/*
 * Populate needed folios for the extent buffer.
 *
 * For now, the folios populated are always in order 0 (aka, single page).
 */
static int alloc_eb_folio_array(struct extent_buffer *eb, bool nofail)
{
	struct page *page_array[INLINE_EXTENT_BUFFER_PAGES] = { 0 };
	int num_pages = num_extent_pages(eb);
	int ret;

	ret = btrfs_alloc_page_array(num_pages, page_array, nofail);
	if (ret < 0)
		return ret;

	for (int i = 0; i < num_pages; i++)
		eb->folios[i] = page_folio(page_array[i]);
	eb->folio_size = PAGE_SIZE;
	eb->folio_shift = PAGE_SHIFT;
	return 0;
}

static bool btrfs_bio_is_contig(struct btrfs_bio_ctrl *bio_ctrl,
				struct folio *folio, u64 disk_bytenr,
				unsigned int pg_offset)
{
	struct bio *bio = &bio_ctrl->bbio->bio;
	struct bio_vec *bvec = bio_last_bvec_all(bio);
	const sector_t sector = disk_bytenr >> SECTOR_SHIFT;
	struct folio *bv_folio = page_folio(bvec->bv_page);

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
	 * 1) The folios are belonging to the same inode
	 *    This is implied by the call chain.
	 *
	 * 2) The range has adjacent logical bytenr
	 *
	 * 3) The range has adjacent file offset
	 *    This is required for the usage of btrfs_bio->file_offset.
	 */
	return bio_end_sector(bio) == sector &&
		folio_pos(bv_folio) + bvec->bv_offset + bvec->bv_len ==
		folio_pos(folio) + pg_offset;
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
	bbio->bio.bi_write_hint = inode->vfs_inode.i_write_hint;
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
static void submit_extent_folio(struct btrfs_bio_ctrl *bio_ctrl,
			       u64 disk_bytenr, struct folio *folio,
			       size_t size, unsigned long pg_offset)
{
	struct btrfs_inode *inode = folio_to_inode(folio);

	ASSERT(pg_offset + size <= folio_size(folio));
	ASSERT(bio_ctrl->end_io_func);

	if (bio_ctrl->bbio &&
	    !btrfs_bio_is_contig(bio_ctrl, folio, disk_bytenr, pg_offset))
		submit_one_bio(bio_ctrl);

	do {
		u32 len = size;

		/* Allocate new bio if needed */
		if (!bio_ctrl->bbio) {
			alloc_new_bio(inode, bio_ctrl, disk_bytenr,
				      folio_pos(folio) + pg_offset);
		}

		/* Cap to the current ordered extent boundary if there is one. */
		if (len > bio_ctrl->len_to_oe_boundary) {
			ASSERT(bio_ctrl->compress_type == BTRFS_COMPRESS_NONE);
			ASSERT(is_data_inode(inode));
			len = bio_ctrl->len_to_oe_boundary;
		}

		if (!bio_add_folio(&bio_ctrl->bbio->bio, folio, len, pg_offset)) {
			/* bio full: move on to a new one */
			submit_one_bio(bio_ctrl);
			continue;
		}

		if (bio_ctrl->wbc)
			wbc_account_cgroup_owner(bio_ctrl->wbc, folio,
						 len);

		size -= len;
		pg_offset += len;
		disk_bytenr += len;

		/*
		 * len_to_oe_boundary defaults to U32_MAX, which isn't folio or
		 * sector aligned.  alloc_new_bio() then sets it to the end of
		 * our ordered extent for writes into zoned devices.
		 *
		 * When len_to_oe_boundary is tracking an ordered extent, we
		 * trust the ordered extent code to align things properly, and
		 * the check above to cap our write to the ordered extent
		 * boundary is correct.
		 *
		 * When len_to_oe_boundary is U32_MAX, the cap above would
		 * result in a 4095 byte IO for the last folio right before
		 * we hit the bio limit of UINT_MAX.  bio_add_folio() has all
		 * the checks required to make sure we don't overflow the bio,
		 * and we should just ignore len_to_oe_boundary completely
		 * unless we're using it to track an ordered extent.
		 *
		 * It's pretty hard to make a bio sized U32_MAX, but it can
		 * happen when the page cache is able to feed us contiguous
		 * folios for large extents.
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
		lockdep_assert_held(&folio->mapping->i_private_lock);

	if (!btrfs_meta_is_subpage(fs_info)) {
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

int set_folio_extent_mapped(struct folio *folio)
{
	struct btrfs_fs_info *fs_info;

	ASSERT(folio->mapping);

	if (folio_test_private(folio))
		return 0;

	fs_info = folio_to_fs_info(folio);

	if (btrfs_is_subpage(fs_info, folio))
		return btrfs_attach_subpage(fs_info, folio, BTRFS_SUBPAGE_DATA);

	folio_attach_private(folio, (void *)EXTENT_FOLIO_PRIVATE);
	return 0;
}

void clear_folio_extent_mapped(struct folio *folio)
{
	struct btrfs_fs_info *fs_info;

	ASSERT(folio->mapping);

	if (!folio_test_private(folio))
		return;

	fs_info = folio_to_fs_info(folio);
	if (btrfs_is_subpage(fs_info, folio))
		return btrfs_detach_subpage(fs_info, folio, BTRFS_SUBPAGE_DATA);

	folio_detach_private(folio);
}

static struct extent_map *get_extent_map(struct btrfs_inode *inode,
					 struct folio *folio, u64 start,
					 u64 len, struct extent_map **em_cached)
{
	struct extent_map *em;

	ASSERT(em_cached);

	if (*em_cached) {
		em = *em_cached;
		if (extent_map_in_tree(em) && start >= em->start &&
		    start < extent_map_end(em)) {
			refcount_inc(&em->refs);
			return em;
		}

		free_extent_map(em);
		*em_cached = NULL;
	}

	em = btrfs_get_extent(inode, folio, start, len);
	if (!IS_ERR(em)) {
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
static int btrfs_do_readpage(struct folio *folio, struct extent_map **em_cached,
		      struct btrfs_bio_ctrl *bio_ctrl, u64 *prev_em_start)
{
	struct inode *inode = folio->mapping->host;
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	u64 start = folio_pos(folio);
	const u64 end = start + folio_size(folio) - 1;
	u64 extent_offset;
	u64 last_byte = i_size_read(inode);
	struct extent_map *em;
	int ret = 0;
	const size_t blocksize = fs_info->sectorsize;

	ret = set_folio_extent_mapped(folio);
	if (ret < 0) {
		folio_unlock(folio);
		return ret;
	}

	if (folio_contains(folio, last_byte >> PAGE_SHIFT)) {
		size_t zero_offset = offset_in_folio(folio, last_byte);

		if (zero_offset)
			folio_zero_range(folio, zero_offset,
					 folio_size(folio) - zero_offset);
	}
	bio_ctrl->end_io_func = end_bbio_data_read;
	begin_folio_read(fs_info, folio);
	for (u64 cur = start; cur <= end; cur += blocksize) {
		enum btrfs_compression_type compress_type = BTRFS_COMPRESS_NONE;
		unsigned long pg_offset = offset_in_folio(folio, cur);
		bool force_bio_submit = false;
		u64 disk_bytenr;
		u64 block_start;

		ASSERT(IS_ALIGNED(cur, fs_info->sectorsize));
		if (cur >= last_byte) {
			folio_zero_range(folio, pg_offset, end - cur + 1);
			end_folio_read(folio, true, cur, end - cur + 1);
			break;
		}
		if (btrfs_folio_test_uptodate(fs_info, folio, cur, blocksize)) {
			end_folio_read(folio, true, cur, blocksize);
			continue;
		}
		em = get_extent_map(BTRFS_I(inode), folio, cur, end - cur + 1, em_cached);
		if (IS_ERR(em)) {
			end_folio_read(folio, false, cur, end + 1 - cur);
			return PTR_ERR(em);
		}
		extent_offset = cur - em->start;
		BUG_ON(extent_map_end(em) <= cur);
		BUG_ON(end < cur);

		compress_type = extent_map_compression(em);

		if (compress_type != BTRFS_COMPRESS_NONE)
			disk_bytenr = em->disk_bytenr;
		else
			disk_bytenr = extent_map_block_start(em) + extent_offset;

		if (em->flags & EXTENT_FLAG_PREALLOC)
			block_start = EXTENT_MAP_HOLE;
		else
			block_start = extent_map_block_start(em);

		/*
		 * If we have a file range that points to a compressed extent
		 * and it's followed by a consecutive file range that points
		 * to the same compressed extent (possibly with a different
		 * offset and/or length, so it either points to the whole extent
		 * or only part of it), we must make sure we do not submit a
		 * single bio to populate the folios for the 2 ranges because
		 * this makes the compressed extent read zero out the folios
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
		 * it will decompress extent X into the folios belonging to the
		 * first range and then it will stop, zeroing out the remaining
		 * folios that belong to the other range that points to extent X.
		 * So here we make sure we submit 2 bios, one for the first
		 * range and another one for the third range. Both will target
		 * the same physical extent from disk, but we can't currently
		 * make the compressed bio endio callback populate the folios
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
			folio_zero_range(folio, pg_offset, blocksize);
			end_folio_read(folio, true, cur, blocksize);
			continue;
		}
		/* the get_extent function already copied into the folio */
		if (block_start == EXTENT_MAP_INLINE) {
			end_folio_read(folio, true, cur, blocksize);
			continue;
		}

		if (bio_ctrl->compress_type != compress_type) {
			submit_one_bio(bio_ctrl);
			bio_ctrl->compress_type = compress_type;
		}

		if (force_bio_submit)
			submit_one_bio(bio_ctrl);
		submit_extent_folio(bio_ctrl, disk_bytenr, folio, blocksize,
				    pg_offset);
	}
	return 0;
}

/*
 * Check if we can skip waiting the @ordered extent covering the block at @fileoff.
 *
 * @fileoff:	Both input and output.
 *		Input as the file offset where the check should start at.
 *		Output as where the next check should start at,
 *		if the function returns true.
 *
 * Return true if we can skip to @fileoff. The caller needs to check the new
 * @fileoff value to make sure it covers the full range, before skipping the
 * full OE.
 *
 * Return false if we must wait for the ordered extent.
 */
static bool can_skip_one_ordered_range(struct btrfs_inode *inode,
				       struct btrfs_ordered_extent *ordered,
				       u64 *fileoff)
{
	const struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct folio *folio;
	const u32 blocksize = fs_info->sectorsize;
	u64 cur = *fileoff;
	bool ret;

	folio = filemap_get_folio(inode->vfs_inode.i_mapping, cur >> PAGE_SHIFT);

	/*
	 * We should have locked the folio(s) for range [start, end], thus
	 * there must be a folio and it must be locked.
	 */
	ASSERT(!IS_ERR(folio));
	ASSERT(folio_test_locked(folio));

	/*
	 * There are several cases for the folio and OE combination:
	 *
	 * 1) Folio has no private flag
	 *    The OE has all its IO done but not yet finished, and folio got
	 *    invalidated.
	 *
	 * Have we have to wait for the OE to finish, as it may contain the
	 * to-be-inserted data checksum.
	 * Without the data checksum inserted into the csum tree, read will
	 * just fail with missing csum.
	 */
	if (!folio_test_private(folio)) {
		ret = false;
		goto out;
	}

	/*
	 * 2) The first block is DIRTY.
	 *
	 * This means the OE is created by some other folios whose file pos is
	 * before this one. And since we are holding the folio lock, the writeback
	 * of this folio cannot start.
	 *
	 * We must skip the whole OE, because it will never start until we
	 * finished our folio read and unlocked the folio.
	 */
	if (btrfs_folio_test_dirty(fs_info, folio, cur, blocksize)) {
		u64 range_len = min(folio_pos(folio) + folio_size(folio),
				    ordered->file_offset + ordered->num_bytes) - cur;

		ret = true;
		/*
		 * At least inside the folio, all the remaining blocks should
		 * also be dirty.
		 */
		ASSERT(btrfs_folio_test_dirty(fs_info, folio, cur, range_len));
		*fileoff = ordered->file_offset + ordered->num_bytes;
		goto out;
	}

	/*
	 * 3) The first block is uptodate.
	 *
	 * At least the first block can be skipped, but we are still not fully
	 * sure. E.g. if the OE has some other folios in the range that cannot
	 * be skipped.
	 * So we return true and update @next_ret to the OE/folio boundary.
	 */
	if (btrfs_folio_test_uptodate(fs_info, folio, cur, blocksize)) {
		u64 range_len = min(folio_pos(folio) + folio_size(folio),
				    ordered->file_offset + ordered->num_bytes) - cur;

		/*
		 * The whole range to the OE end or folio boundary should also
		 * be uptodate.
		 */
		ASSERT(btrfs_folio_test_uptodate(fs_info, folio, cur, range_len));
		ret = true;
		*fileoff = cur + range_len;
		goto out;
	}

	/*
	 * 4) The first block is not uptodate.
	 *
	 * This means the folio is invalidated after the writeback was finished,
	 * but by some other operations (e.g. block aligned buffered write) the
	 * folio is inserted into filemap.
	 * Very much the same as case 1).
	 */
	ret = false;
out:
	folio_put(folio);
	return ret;
}

static bool can_skip_ordered_extent(struct btrfs_inode *inode,
				    struct btrfs_ordered_extent *ordered,
				    u64 start, u64 end)
{
	const u64 range_end = min(end, ordered->file_offset + ordered->num_bytes - 1);
	u64 cur = max(start, ordered->file_offset);

	while (cur < range_end) {
		bool can_skip;

		can_skip = can_skip_one_ordered_range(inode, ordered, &cur);
		if (!can_skip)
			return false;
	}
	return true;
}

/*
 * Locking helper to make sure we get a stable view of extent maps for the
 * involved range.
 *
 * This is for folio read paths (read and readahead), thus the involved range
 * should have all the folios locked.
 */
static void lock_extents_for_read(struct btrfs_inode *inode, u64 start, u64 end,
				  struct extent_state **cached_state)
{
	u64 cur_pos;

	/* Caller must provide a valid @cached_state. */
	ASSERT(cached_state);

	/* The range must at least be page aligned, as all read paths are folio based. */
	ASSERT(IS_ALIGNED(start, PAGE_SIZE));
	ASSERT(IS_ALIGNED(end + 1, PAGE_SIZE));

again:
	lock_extent(&inode->io_tree, start, end, cached_state);
	cur_pos = start;
	while (cur_pos < end) {
		struct btrfs_ordered_extent *ordered;

		ordered = btrfs_lookup_ordered_range(inode, cur_pos,
						     end - cur_pos + 1);
		/*
		 * No ordered extents in the range, and we hold the extent lock,
		 * no one can modify the extent maps in the range, we're safe to return.
		 */
		if (!ordered)
			break;

		/* Check if we can skip waiting for the whole OE. */
		if (can_skip_ordered_extent(inode, ordered, start, end)) {
			cur_pos = min(ordered->file_offset + ordered->num_bytes,
				      end + 1);
			btrfs_put_ordered_extent(ordered);
			continue;
		}

		/* Now wait for the OE to finish. */
		unlock_extent(&inode->io_tree, start, end, cached_state);
		btrfs_start_ordered_extent_nowriteback(ordered, start, end + 1 - start);
		btrfs_put_ordered_extent(ordered);
		/* We have unlocked the whole range, restart from the beginning. */
		goto again;
	}
}

int btrfs_read_folio(struct file *file, struct folio *folio)
{
	struct btrfs_inode *inode = folio_to_inode(folio);
	const u64 start = folio_pos(folio);
	const u64 end = start + folio_size(folio) - 1;
	struct extent_state *cached_state = NULL;
	struct btrfs_bio_ctrl bio_ctrl = { .opf = REQ_OP_READ };
	struct extent_map *em_cached = NULL;
	int ret;

	lock_extents_for_read(inode, start, end, &cached_state);
	ret = btrfs_do_readpage(folio, &em_cached, &bio_ctrl, NULL);
	unlock_extent(&inode->io_tree, start, end, &cached_state);

	free_extent_map(em_cached);

	/*
	 * If btrfs_do_readpage() failed we will want to submit the assembled
	 * bio to do the cleanup.
	 */
	submit_one_bio(&bio_ctrl);
	return ret;
}

static void set_delalloc_bitmap(struct folio *folio, unsigned long *delalloc_bitmap,
				u64 start, u32 len)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);
	const u64 folio_start = folio_pos(folio);
	unsigned int start_bit;
	unsigned int nbits;

	ASSERT(start >= folio_start && start + len <= folio_start + folio_size(folio));
	start_bit = (start - folio_start) >> fs_info->sectorsize_bits;
	nbits = len >> fs_info->sectorsize_bits;
	ASSERT(bitmap_test_range_all_zero(delalloc_bitmap, start_bit, nbits));
	bitmap_set(delalloc_bitmap, start_bit, nbits);
}

static bool find_next_delalloc_bitmap(struct folio *folio,
				      unsigned long *delalloc_bitmap, u64 start,
				      u64 *found_start, u32 *found_len)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);
	const u64 folio_start = folio_pos(folio);
	const unsigned int bitmap_size = btrfs_blocks_per_folio(fs_info, folio);
	unsigned int start_bit;
	unsigned int first_zero;
	unsigned int first_set;

	ASSERT(start >= folio_start && start < folio_start + folio_size(folio));

	start_bit = (start - folio_start) >> fs_info->sectorsize_bits;
	first_set = find_next_bit(delalloc_bitmap, bitmap_size, start_bit);
	if (first_set >= bitmap_size)
		return false;

	*found_start = folio_start + (first_set << fs_info->sectorsize_bits);
	first_zero = find_next_zero_bit(delalloc_bitmap, bitmap_size, first_set);
	*found_len = (first_zero - first_set) << fs_info->sectorsize_bits;
	return true;
}

/*
 * Do all of the delayed allocation setup.
 *
 * Return >0 if all the dirty blocks are submitted async (compression) or inlined.
 * The @folio should no longer be touched (treat it as already unlocked).
 *
 * Return 0 if there is still dirty block that needs to be submitted through
 * extent_writepage_io().
 * bio_ctrl->submit_bitmap will indicate which blocks of the folio should be
 * submitted, and @folio is still kept locked.
 *
 * Return <0 if there is any error hit.
 * Any allocated ordered extent range covering this folio will be marked
 * finished (IOERR), and @folio is still kept locked.
 */
static noinline_for_stack int writepage_delalloc(struct btrfs_inode *inode,
						 struct folio *folio,
						 struct btrfs_bio_ctrl *bio_ctrl)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(&inode->vfs_inode);
	struct writeback_control *wbc = bio_ctrl->wbc;
	const bool is_subpage = btrfs_is_subpage(fs_info, folio);
	const u64 page_start = folio_pos(folio);
	const u64 page_end = page_start + folio_size(folio) - 1;
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);
	unsigned long delalloc_bitmap = 0;
	/*
	 * Save the last found delalloc end. As the delalloc end can go beyond
	 * page boundary, thus we cannot rely on subpage bitmap to locate the
	 * last delalloc end.
	 */
	u64 last_delalloc_end = 0;
	/*
	 * The range end (exclusive) of the last successfully finished delalloc
	 * range.
	 * Any range covered by ordered extent must either be manually marked
	 * finished (error handling), or has IO submitted (and finish the
	 * ordered extent normally).
	 *
	 * This records the end of ordered extent cleanup if we hit an error.
	 */
	u64 last_finished_delalloc_end = page_start;
	u64 delalloc_start = page_start;
	u64 delalloc_end = page_end;
	u64 delalloc_to_write = 0;
	int ret = 0;
	int bit;

	/* Save the dirty bitmap as our submission bitmap will be a subset of it. */
	if (btrfs_is_subpage(fs_info, folio)) {
		ASSERT(blocks_per_folio > 1);
		btrfs_get_subpage_dirty_bitmap(fs_info, folio, &bio_ctrl->submit_bitmap);
	} else {
		bio_ctrl->submit_bitmap = 1;
	}

	for_each_set_bit(bit, &bio_ctrl->submit_bitmap, blocks_per_folio) {
		u64 start = page_start + (bit << fs_info->sectorsize_bits);

		btrfs_folio_set_lock(fs_info, folio, start, fs_info->sectorsize);
	}

	/* Lock all (subpage) delalloc ranges inside the folio first. */
	while (delalloc_start < page_end) {
		delalloc_end = page_end;
		if (!find_lock_delalloc_range(&inode->vfs_inode, folio,
					      &delalloc_start, &delalloc_end)) {
			delalloc_start = delalloc_end + 1;
			continue;
		}
		set_delalloc_bitmap(folio, &delalloc_bitmap, delalloc_start,
				    min(delalloc_end, page_end) + 1 - delalloc_start);
		last_delalloc_end = delalloc_end;
		delalloc_start = delalloc_end + 1;
	}
	delalloc_start = page_start;

	if (!last_delalloc_end)
		goto out;

	/* Run the delalloc ranges for the above locked ranges. */
	while (delalloc_start < page_end) {
		u64 found_start;
		u32 found_len;
		bool found;

		if (!is_subpage) {
			/*
			 * For non-subpage case, the found delalloc range must
			 * cover this folio and there must be only one locked
			 * delalloc range.
			 */
			found_start = page_start;
			found_len = last_delalloc_end + 1 - found_start;
			found = true;
		} else {
			found = find_next_delalloc_bitmap(folio, &delalloc_bitmap,
					delalloc_start, &found_start, &found_len);
		}
		if (!found)
			break;
		/*
		 * The subpage range covers the last sector, the delalloc range may
		 * end beyond the folio boundary, use the saved delalloc_end
		 * instead.
		 */
		if (found_start + found_len >= page_end)
			found_len = last_delalloc_end + 1 - found_start;

		if (ret >= 0) {
			/*
			 * Some delalloc range may be created by previous folios.
			 * Thus we still need to clean up this range during error
			 * handling.
			 */
			last_finished_delalloc_end = found_start;
			/* No errors hit so far, run the current delalloc range. */
			ret = btrfs_run_delalloc_range(inode, folio,
						       found_start,
						       found_start + found_len - 1,
						       wbc);
			if (ret >= 0)
				last_finished_delalloc_end = found_start + found_len;
			if (unlikely(ret < 0))
				btrfs_err_rl(fs_info,
"failed to run delalloc range, root=%lld ino=%llu folio=%llu submit_bitmap=%*pbl start=%llu len=%u: %d",
					     btrfs_root_id(inode->root),
					     btrfs_ino(inode),
					     folio_pos(folio),
					     blocks_per_folio,
					     &bio_ctrl->submit_bitmap,
					     found_start, found_len, ret);
		} else {
			/*
			 * We've hit an error during previous delalloc range,
			 * have to cleanup the remaining locked ranges.
			 */
			unlock_extent(&inode->io_tree, found_start,
				      found_start + found_len - 1, NULL);
			unlock_delalloc_folio(&inode->vfs_inode, folio,
					      found_start,
					      found_start + found_len - 1);
		}

		/*
		 * We have some ranges that's going to be submitted asynchronously
		 * (compression or inline).  These range have their own control
		 * on when to unlock the pages.  We should not touch them
		 * anymore, so clear the range from the submission bitmap.
		 */
		if (ret > 0) {
			unsigned int start_bit = (found_start - page_start) >>
						 fs_info->sectorsize_bits;
			unsigned int end_bit = (min(page_end + 1, found_start + found_len) -
						page_start) >> fs_info->sectorsize_bits;
			bitmap_clear(&bio_ctrl->submit_bitmap, start_bit, end_bit - start_bit);
		}
		/*
		 * Above btrfs_run_delalloc_range() may have unlocked the folio,
		 * thus for the last range, we cannot touch the folio anymore.
		 */
		if (found_start + found_len >= last_delalloc_end + 1)
			break;

		delalloc_start = found_start + found_len;
	}
	/*
	 * It's possible we had some ordered extents created before we hit
	 * an error, cleanup non-async successfully created delalloc ranges.
	 */
	if (unlikely(ret < 0)) {
		unsigned int bitmap_size = min(
				(last_finished_delalloc_end - page_start) >>
				fs_info->sectorsize_bits,
				blocks_per_folio);

		for_each_set_bit(bit, &bio_ctrl->submit_bitmap, bitmap_size)
			btrfs_mark_ordered_io_finished(inode, folio,
				page_start + (bit << fs_info->sectorsize_bits),
				fs_info->sectorsize, false);
		return ret;
	}
out:
	if (last_delalloc_end)
		delalloc_end = last_delalloc_end;
	else
		delalloc_end = page_end;
	/*
	 * delalloc_end is already one less than the total length, so
	 * we don't subtract one from PAGE_SIZE.
	 */
	delalloc_to_write +=
		DIV_ROUND_UP(delalloc_end + 1 - page_start, PAGE_SIZE);

	/*
	 * If all ranges are submitted asynchronously, we just need to account
	 * for them here.
	 */
	if (bitmap_empty(&bio_ctrl->submit_bitmap, blocks_per_folio)) {
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
 * Return 0 if we have submitted or queued the sector for submission.
 * Return <0 for critical errors.
 *
 * Caller should make sure filepos < i_size and handle filepos >= i_size case.
 */
static int submit_one_sector(struct btrfs_inode *inode,
			     struct folio *folio,
			     u64 filepos, struct btrfs_bio_ctrl *bio_ctrl,
			     loff_t i_size)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_map *em;
	u64 block_start;
	u64 disk_bytenr;
	u64 extent_offset;
	u64 em_end;
	const u32 sectorsize = fs_info->sectorsize;

	ASSERT(IS_ALIGNED(filepos, sectorsize));

	/* @filepos >= i_size case should be handled by the caller. */
	ASSERT(filepos < i_size);

	em = btrfs_get_extent(inode, NULL, filepos, sectorsize);
	if (IS_ERR(em))
		return PTR_ERR(em);

	extent_offset = filepos - em->start;
	em_end = extent_map_end(em);
	ASSERT(filepos <= em_end);
	ASSERT(IS_ALIGNED(em->start, sectorsize));
	ASSERT(IS_ALIGNED(em->len, sectorsize));

	block_start = extent_map_block_start(em);
	disk_bytenr = extent_map_block_start(em) + extent_offset;

	ASSERT(!extent_map_is_compressed(em));
	ASSERT(block_start != EXTENT_MAP_HOLE);
	ASSERT(block_start != EXTENT_MAP_INLINE);

	free_extent_map(em);
	em = NULL;

	/*
	 * Although the PageDirty bit is cleared before entering this
	 * function, subpage dirty bit is not cleared.
	 * So clear subpage dirty bit here so next time we won't submit
	 * a folio for a range already written to disk.
	 */
	btrfs_folio_clear_dirty(fs_info, folio, filepos, sectorsize);
	btrfs_folio_set_writeback(fs_info, folio, filepos, sectorsize);
	/*
	 * Above call should set the whole folio with writeback flag, even
	 * just for a single subpage sector.
	 * As long as the folio is properly locked and the range is correct,
	 * we should always get the folio with writeback flag.
	 */
	ASSERT(folio_test_writeback(folio));

	submit_extent_folio(bio_ctrl, disk_bytenr, folio,
			    sectorsize, filepos - folio_pos(folio));
	return 0;
}

/*
 * Helper for extent_writepage().  This calls the writepage start hooks,
 * and does the loop to map the page into extents and bios.
 *
 * We return 1 if the IO is started and the page is unlocked,
 * 0 if all went well (page still locked)
 * < 0 if there were errors (page still locked)
 */
static noinline_for_stack int extent_writepage_io(struct btrfs_inode *inode,
						  struct folio *folio,
						  u64 start, u32 len,
						  struct btrfs_bio_ctrl *bio_ctrl,
						  loff_t i_size)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	unsigned long range_bitmap = 0;
	bool submitted_io = false;
	bool error = false;
	const u64 folio_start = folio_pos(folio);
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);
	u64 cur;
	int bit;
	int ret = 0;

	ASSERT(start >= folio_start &&
	       start + len <= folio_start + folio_size(folio));

	ret = btrfs_writepage_cow_fixup(folio);
	if (ret == -EAGAIN) {
		/* Fixup worker will requeue */
		folio_redirty_for_writepage(bio_ctrl->wbc, folio);
		folio_unlock(folio);
		return 1;
	}
	if (ret < 0)
		return ret;

	for (cur = start; cur < start + len; cur += fs_info->sectorsize)
		set_bit((cur - folio_start) >> fs_info->sectorsize_bits, &range_bitmap);
	bitmap_and(&bio_ctrl->submit_bitmap, &bio_ctrl->submit_bitmap, &range_bitmap,
		   blocks_per_folio);

	bio_ctrl->end_io_func = end_bbio_data_write;

	for_each_set_bit(bit, &bio_ctrl->submit_bitmap, blocks_per_folio) {
		cur = folio_pos(folio) + (bit << fs_info->sectorsize_bits);

		if (cur >= i_size) {
			btrfs_mark_ordered_io_finished(inode, folio, cur,
						       start + len - cur, true);
			/*
			 * This range is beyond i_size, thus we don't need to
			 * bother writing back.
			 * But we still need to clear the dirty subpage bit, or
			 * the next time the folio gets dirtied, we will try to
			 * writeback the sectors with subpage dirty bits,
			 * causing writeback without ordered extent.
			 */
			btrfs_folio_clear_dirty(fs_info, folio, cur,
						start + len - cur);
			break;
		}
		ret = submit_one_sector(inode, folio, cur, bio_ctrl, i_size);
		if (unlikely(ret < 0)) {
			/*
			 * bio_ctrl may contain a bio crossing several folios.
			 * Submit it immediately so that the bio has a chance
			 * to finish normally, other than marked as error.
			 */
			submit_one_bio(bio_ctrl);
			/*
			 * Failed to grab the extent map which should be very rare.
			 * Since there is no bio submitted to finish the ordered
			 * extent, we have to manually finish this sector.
			 */
			btrfs_mark_ordered_io_finished(inode, folio, cur,
						       fs_info->sectorsize, false);
			error = true;
			continue;
		}
		submitted_io = true;
	}

	/*
	 * If we didn't submitted any sector (>= i_size), folio dirty get
	 * cleared but PAGECACHE_TAG_DIRTY is not cleared (only cleared
	 * by folio_start_writeback() if the folio is not dirty).
	 *
	 * Here we set writeback and clear for the range. If the full folio
	 * is no longer dirty then we clear the PAGECACHE_TAG_DIRTY tag.
	 *
	 * If we hit any error, the corresponding sector will still be dirty
	 * thus no need to clear PAGECACHE_TAG_DIRTY.
	 */
	if (!submitted_io && !error) {
		btrfs_folio_set_writeback(fs_info, folio, start, len);
		btrfs_folio_clear_writeback(fs_info, folio, start, len);
	}
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
static int extent_writepage(struct folio *folio, struct btrfs_bio_ctrl *bio_ctrl)
{
	struct btrfs_inode *inode = BTRFS_I(folio->mapping->host);
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	int ret;
	size_t pg_offset;
	loff_t i_size = i_size_read(&inode->vfs_inode);
	unsigned long end_index = i_size >> PAGE_SHIFT;
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);

	trace_extent_writepage(folio, &inode->vfs_inode, bio_ctrl->wbc);

	WARN_ON(!folio_test_locked(folio));

	pg_offset = offset_in_folio(folio, i_size);
	if (folio->index > end_index ||
	   (folio->index == end_index && !pg_offset)) {
		folio_invalidate(folio, 0, folio_size(folio));
		folio_unlock(folio);
		return 0;
	}

	if (folio->index == end_index)
		folio_zero_range(folio, pg_offset, folio_size(folio) - pg_offset);

	/*
	 * Default to unlock the whole folio.
	 * The proper bitmap can only be initialized until writepage_delalloc().
	 */
	bio_ctrl->submit_bitmap = (unsigned long)-1;

	/*
	 * If the page is dirty but without private set, it's marked dirty
	 * without informing the fs.
	 * Nowadays that is a bug, since the introduction of
	 * pin_user_pages*().
	 *
	 * So here we check if the page has private set to rule out such
	 * case.
	 * But we also have a long history of relying on the COW fixup,
	 * so here we only enable this check for experimental builds until
	 * we're sure it's safe.
	 */
	if (IS_ENABLED(CONFIG_BTRFS_EXPERIMENTAL) &&
	    unlikely(!folio_test_private(folio))) {
		WARN_ON(IS_ENABLED(CONFIG_BTRFS_DEBUG));
		btrfs_err_rl(fs_info,
	"root %lld ino %llu folio %llu is marked dirty without notifying the fs",
			     inode->root->root_key.objectid,
			     btrfs_ino(inode), folio_pos(folio));
		ret = -EUCLEAN;
		goto done;
	}

	ret = set_folio_extent_mapped(folio);
	if (ret < 0)
		goto done;

	ret = writepage_delalloc(inode, folio, bio_ctrl);
	if (ret == 1)
		return 0;
	if (ret)
		goto done;

	ret = extent_writepage_io(inode, folio, folio_pos(folio),
				  folio_size(folio), bio_ctrl, i_size);
	if (ret == 1)
		return 0;
	if (ret < 0)
		btrfs_err_rl(fs_info,
"failed to submit blocks, root=%lld inode=%llu folio=%llu submit_bitmap=%*pbl: %d",
			     btrfs_root_id(inode->root), btrfs_ino(inode),
			     folio_pos(folio), blocks_per_folio,
			     &bio_ctrl->submit_bitmap, ret);

	bio_ctrl->wbc->nr_to_write--;

done:
	if (ret < 0)
		mapping_set_error(folio->mapping, ret);
	/*
	 * Only unlock ranges that are submitted. As there can be some async
	 * submitted ranges inside the folio.
	 */
	btrfs_folio_end_lock_bitmap(fs_info, folio, bio_ctrl->submit_bitmap);
	ASSERT(ret <= 0);
	return ret;
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
	 * able to find the pages which contain errors at transaction
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
		const struct btrfs_fs_info *fs_info, u64 start)
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
	struct folio_iter fi;

	if (bbio->bio.bi_status != BLK_STS_OK)
		set_btree_ioerr(eb);

	bio_for_each_folio_all(fi, &bbio->bio) {
		btrfs_meta_folio_clear_writeback(fi.folio, eb);
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
	for (int i = 0; i < num_extent_folios(eb); i++) {
		struct folio *folio = eb->folios[i];
		u64 range_start = max_t(u64, eb->start, folio_pos(folio));
		u32 range_len = min_t(u64, folio_pos(folio) + folio_size(folio),
				      eb->start + eb->len) - range_start;

		folio_lock(folio);
		btrfs_meta_folio_clear_dirty(folio, eb);
		btrfs_meta_folio_set_writeback(folio, eb);
		if (!folio_test_dirty(folio))
			wbc->nr_to_write -= folio_nr_pages(folio);
		bio_add_folio_nofail(&bbio->bio, folio, range_len,
				     offset_in_folio(folio, range_start));
		wbc_account_cgroup_owner(wbc, folio, range_len);
		folio_unlock(folio);
	}
	btrfs_submit_bbio(bbio, 0);
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
static int submit_eb_subpage(struct folio *folio, struct writeback_control *wbc)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);
	int submitted = 0;
	u64 folio_start = folio_pos(folio);
	int bit_start = 0;
	int sectors_per_node = fs_info->nodesize >> fs_info->sectorsize_bits;
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);

	/* Lock and write each dirty extent buffers in the range */
	while (bit_start < blocks_per_folio) {
		struct btrfs_subpage *subpage = folio_get_private(folio);
		struct extent_buffer *eb;
		unsigned long flags;
		u64 start;

		/*
		 * Take private lock to ensure the subpage won't be detached
		 * in the meantime.
		 */
		spin_lock(&folio->mapping->i_private_lock);
		if (!folio_test_private(folio)) {
			spin_unlock(&folio->mapping->i_private_lock);
			break;
		}
		spin_lock_irqsave(&subpage->lock, flags);
		if (!test_bit(bit_start + btrfs_bitmap_nr_dirty * blocks_per_folio,
			      subpage->bitmaps)) {
			spin_unlock_irqrestore(&subpage->lock, flags);
			spin_unlock(&folio->mapping->i_private_lock);
			bit_start++;
			continue;
		}

		start = folio_start + bit_start * fs_info->sectorsize;
		bit_start += sectors_per_node;

		/*
		 * Here we just want to grab the eb without touching extra
		 * spin locks, so call find_extent_buffer_nolock().
		 */
		eb = find_extent_buffer_nolock(fs_info, start);
		spin_unlock_irqrestore(&subpage->lock, flags);
		spin_unlock(&folio->mapping->i_private_lock);

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
static int submit_eb_page(struct folio *folio, struct btrfs_eb_write_context *ctx)
{
	struct writeback_control *wbc = ctx->wbc;
	struct address_space *mapping = folio->mapping;
	struct extent_buffer *eb;
	int ret;

	if (!folio_test_private(folio))
		return 0;

	if (btrfs_meta_is_subpage(folio_to_fs_info(folio)))
		return submit_eb_subpage(folio, wbc);

	spin_lock(&mapping->i_private_lock);
	if (!folio_test_private(folio)) {
		spin_unlock(&mapping->i_private_lock);
		return 0;
	}

	eb = folio_get_private(folio);

	/*
	 * Shouldn't happen and normally this would be a BUG_ON but no point
	 * crashing the machine for something we can survive anyway.
	 */
	if (WARN_ON(!eb)) {
		spin_unlock(&mapping->i_private_lock);
		return 0;
	}

	if (eb == ctx->eb) {
		spin_unlock(&mapping->i_private_lock);
		return 0;
	}
	ret = atomic_inc_not_zero(&eb->refs);
	spin_unlock(&mapping->i_private_lock);
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
	struct btrfs_fs_info *fs_info = inode_to_fs_info(mapping->host);
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

			ret = submit_eb_page(folio, &ctx);
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
	 * We can get ret > 0 from submit_extent_folio() indicating how many ebs
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
			 * the folio lock: the folio may be truncated or
			 * invalidated (changing folio->mapping to NULL).
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

			/*
			 * For subpage case, compression can lead to mixed
			 * writeback and dirty flags, e.g:
			 * 0     32K    64K    96K    128K
			 * |     |//////||/////|   |//|
			 *
			 * In above case, [32K, 96K) is asynchronously submitted
			 * for compression, and [124K, 128K) needs to be written back.
			 *
			 * If we didn't wait wrtiteback for page 64K, [128K, 128K)
			 * won't be submitted as the page still has writeback flag
			 * and will be skipped in the next check.
			 *
			 * This mixed writeback and dirty case is only possible for
			 * subpage case.
			 *
			 * TODO: Remove this check after migrating compression to
			 * regular submission.
			 */
			if (wbc->sync_mode != WB_SYNC_NONE ||
			    btrfs_is_subpage(inode_to_fs_info(inode), folio)) {
				if (folio_test_writeback(folio))
					submit_write_bio(bio_ctrl, 0);
				folio_wait_writeback(folio);
			}

			if (folio_test_writeback(folio) ||
			    !folio_clear_dirty_for_io(folio)) {
				folio_unlock(folio);
				continue;
			}

			ret = extent_writepage(folio, bio_ctrl);
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
void extent_write_locked_range(struct inode *inode, const struct folio *locked_folio,
			       u64 start, u64 end, struct writeback_control *wbc,
			       bool pages_dirty)
{
	bool found_error = false;
	int ret = 0;
	struct address_space *mapping = inode->i_mapping;
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
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
		u64 cur_end;
		u32 cur_len;
		struct folio *folio;

		folio = filemap_get_folio(mapping, cur >> PAGE_SHIFT);

		/*
		 * This shouldn't happen, the pages are pinned and locked, this
		 * code is just in case, but shouldn't actually be run.
		 */
		if (IS_ERR(folio)) {
			cur_end = min(round_down(cur, PAGE_SIZE) + PAGE_SIZE - 1, end);
			cur_len = cur_end + 1 - cur;
			btrfs_mark_ordered_io_finished(BTRFS_I(inode), NULL,
						       cur, cur_len, false);
			mapping_set_error(mapping, PTR_ERR(folio));
			cur = cur_end;
			continue;
		}

		cur_end = min_t(u64, folio_pos(folio) + folio_size(folio) - 1, end);
		cur_len = cur_end + 1 - cur;

		ASSERT(folio_test_locked(folio));
		if (pages_dirty && folio != locked_folio)
			ASSERT(folio_test_dirty(folio));

		/*
		 * Set the submission bitmap to submit all sectors.
		 * extent_writepage_io() will do the truncation correctly.
		 */
		bio_ctrl.submit_bitmap = (unsigned long)-1;
		ret = extent_writepage_io(BTRFS_I(inode), folio, cur, cur_len,
					  &bio_ctrl, i_size);
		if (ret == 1)
			goto next_page;

		if (ret)
			mapping_set_error(mapping, ret);
		btrfs_folio_end_lock(fs_info, folio, cur, cur_len);
		if (ret < 0)
			found_error = true;
next_page:
		folio_put(folio);
		cur = cur_end + 1;
	}

	submit_write_bio(&bio_ctrl, found_error ? ret : 0);
}

int btrfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
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

void btrfs_readahead(struct readahead_control *rac)
{
	struct btrfs_bio_ctrl bio_ctrl = { .opf = REQ_OP_READ | REQ_RAHEAD };
	struct folio *folio;
	struct btrfs_inode *inode = BTRFS_I(rac->mapping->host);
	const u64 start = readahead_pos(rac);
	const u64 end = start + readahead_length(rac) - 1;
	struct extent_state *cached_state = NULL;
	struct extent_map *em_cached = NULL;
	u64 prev_em_start = (u64)-1;

	lock_extents_for_read(inode, start, end, &cached_state);

	while ((folio = readahead_folio(rac)) != NULL)
		btrfs_do_readpage(folio, &em_cached, &bio_ctrl, &prev_em_start);

	unlock_extent(&inode->io_tree, start, end, &cached_state);

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
	size_t blocksize = folio_to_fs_info(folio)->sectorsize;

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
static bool try_release_extent_state(struct extent_io_tree *tree,
				     struct folio *folio)
{
	u64 start = folio_pos(folio);
	u64 end = start + folio_size(folio) - 1;
	bool ret;

	if (test_range_bit_exists(tree, start, end, EXTENT_LOCKED)) {
		ret = false;
	} else {
		u32 clear_bits = ~(EXTENT_LOCKED | EXTENT_NODATASUM |
				   EXTENT_DELALLOC_NEW | EXTENT_CTLBITS |
				   EXTENT_QGROUP_RESERVED);
		int ret2;

		/*
		 * At this point we can safely clear everything except the
		 * locked bit, the nodatasum bit and the delalloc new bit.
		 * The delalloc new bit will be cleared by ordered extent
		 * completion.
		 */
		ret2 = __clear_extent_bit(tree, start, end, clear_bits, NULL, NULL);

		/* if clear_extent_bit failed for enomem reasons,
		 * we can't allow the release to continue.
		 */
		if (ret2 < 0)
			ret = false;
		else
			ret = true;
	}
	return ret;
}

/*
 * a helper for release_folio.  As long as there are no locked extents
 * in the range corresponding to the page, both state records and extent
 * map records are removed
 */
bool try_release_extent_mapping(struct folio *folio, gfp_t mask)
{
	u64 start = folio_pos(folio);
	u64 end = start + folio_size(folio) - 1;
	struct btrfs_inode *inode = folio_to_inode(folio);
	struct extent_io_tree *io_tree = &inode->io_tree;

	while (start <= end) {
		const u64 cur_gen = btrfs_get_fs_generation(inode->root->fs_info);
		const u64 len = end - start + 1;
		struct extent_map_tree *extent_tree = &inode->extent_tree;
		struct extent_map *em;

		write_lock(&extent_tree->lock);
		em = lookup_extent_mapping(extent_tree, start, len);
		if (!em) {
			write_unlock(&extent_tree->lock);
			break;
		}
		if ((em->flags & EXTENT_FLAG_PINNED) || em->start != start) {
			write_unlock(&extent_tree->lock);
			free_extent_map(em);
			break;
		}
		if (test_range_bit_exists(io_tree, em->start,
					  extent_map_end(em) - 1, EXTENT_LOCKED))
			goto next;
		/*
		 * If it's not in the list of modified extents, used by a fast
		 * fsync, we can remove it. If it's being logged we can safely
		 * remove it since fsync took an extra reference on the em.
		 */
		if (list_empty(&em->list) || (em->flags & EXTENT_FLAG_LOGGING))
			goto remove_em;
		/*
		 * If it's in the list of modified extents, remove it only if
		 * its generation is older then the current one, in which case
		 * we don't need it for a fast fsync. Otherwise don't remove it,
		 * we could be racing with an ongoing fast fsync that could miss
		 * the new extent.
		 */
		if (em->generation >= cur_gen)
			goto next;
remove_em:
		/*
		 * We only remove extent maps that are not in the list of
		 * modified extents or that are in the list but with a
		 * generation lower then the current generation, so there is no
		 * need to set the full fsync flag on the inode (it hurts the
		 * fsync performance for workloads with a data size that exceeds
		 * or is close to the system's memory).
		 */
		remove_extent_mapping(inode, em);
		/* Once for the inode's extent map tree. */
		free_extent_map(em);
next:
		start = extent_map_end(em);
		write_unlock(&extent_tree->lock);

		/* Once for us, for the lookup_extent_mapping() reference. */
		free_extent_map(em);

		if (need_resched()) {
			/*
			 * If we need to resched but we can't block just exit
			 * and leave any remaining extent maps.
			 */
			if (!gfpflags_allow_blocking(mask))
				break;

			cond_resched();
		}
	}
	return try_release_extent_state(io_tree, folio);
}

static int extent_buffer_under_io(const struct extent_buffer *eb)
{
	return (test_bit(EXTENT_BUFFER_WRITEBACK, &eb->bflags) ||
		test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags));
}

static bool folio_range_has_eb(struct folio *folio)
{
	struct btrfs_subpage *subpage;

	lockdep_assert_held(&folio->mapping->i_private_lock);

	if (folio_test_private(folio)) {
		subpage = folio_get_private(folio);
		if (atomic_read(&subpage->eb_refs))
			return true;
	}
	return false;
}

static void detach_extent_buffer_folio(const struct extent_buffer *eb, struct folio *folio)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;
	const bool mapped = !test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	/*
	 * For mapped eb, we're going to change the folio private, which should
	 * be done under the i_private_lock.
	 */
	if (mapped)
		spin_lock(&folio->mapping->i_private_lock);

	if (!folio_test_private(folio)) {
		if (mapped)
			spin_unlock(&folio->mapping->i_private_lock);
		return;
	}

	if (!btrfs_meta_is_subpage(fs_info)) {
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
			spin_unlock(&folio->mapping->i_private_lock);
		return;
	}

	/*
	 * For subpage, we can have dummy eb with folio private attached.  In
	 * this case, we can directly detach the private as such folio is only
	 * attached to one dummy eb, no sharing.
	 */
	if (!mapped) {
		btrfs_detach_subpage(fs_info, folio, BTRFS_SUBPAGE_METADATA);
		return;
	}

	btrfs_folio_dec_eb_refs(fs_info, folio);

	/*
	 * We can only detach the folio private if there are no other ebs in the
	 * page range and no unfinished IO.
	 */
	if (!folio_range_has_eb(folio))
		btrfs_detach_subpage(fs_info, folio, BTRFS_SUBPAGE_METADATA);

	spin_unlock(&folio->mapping->i_private_lock);
}

/* Release all folios attached to the extent buffer */
static void btrfs_release_extent_buffer_folios(const struct extent_buffer *eb)
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
	btrfs_release_extent_buffer_folios(eb);
	btrfs_leak_debug_del_eb(eb);
	kmem_cache_free(extent_buffer_cache, eb);
}

static struct extent_buffer *__alloc_extent_buffer(struct btrfs_fs_info *fs_info,
						   u64 start)
{
	struct extent_buffer *eb = NULL;

	eb = kmem_cache_zalloc(extent_buffer_cache, GFP_NOFS|__GFP_NOFAIL);
	eb->start = start;
	eb->len = fs_info->nodesize;
	eb->fs_info = fs_info;
	init_rwsem(&eb->lock);

	btrfs_leak_debug_add_eb(eb);

	spin_lock_init(&eb->refs_lock);
	atomic_set(&eb->refs, 1);

	ASSERT(eb->len <= BTRFS_MAX_METADATA_BLOCKSIZE);

	return eb;
}

struct extent_buffer *btrfs_clone_extent_buffer(const struct extent_buffer *src)
{
	struct extent_buffer *new;
	int ret;

	new = __alloc_extent_buffer(src->fs_info, src->start);
	if (new == NULL)
		return NULL;

	/*
	 * Set UNMAPPED before calling btrfs_release_extent_buffer(), as
	 * btrfs_release_extent_buffer() have different behavior for
	 * UNMAPPED subpage extent buffer.
	 */
	set_bit(EXTENT_BUFFER_UNMAPPED, &new->bflags);

	ret = alloc_eb_folio_array(new, false);
	if (ret) {
		btrfs_release_extent_buffer(new);
		return NULL;
	}

	for (int i = 0; i < num_extent_folios(src); i++) {
		struct folio *folio = new->folios[i];

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

struct extent_buffer *alloc_dummy_extent_buffer(struct btrfs_fs_info *fs_info,
						u64 start)
{
	struct extent_buffer *eb;
	int ret;

	eb = __alloc_extent_buffer(fs_info, start);
	if (!eb)
		return NULL;

	ret = alloc_eb_folio_array(eb, false);
	if (ret)
		goto out;

	for (int i = 0; i < num_extent_folios(eb); i++) {
		ret = attach_extent_buffer_folio(eb, eb->folios[i], NULL);
		if (ret < 0)
			goto out_detach;
	}

	set_extent_buffer_uptodate(eb);
	btrfs_set_header_nritems(eb, 0);
	set_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags);

	return eb;

out_detach:
	for (int i = 0; i < num_extent_folios(eb); i++) {
		if (eb->folios[i]) {
			detach_extent_buffer_folio(eb, eb->folios[i]);
			folio_put(eb->folios[i]);
		}
	}
out:
	kmem_cache_free(extent_buffer_cache, eb);
	return NULL;
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
	check_buffer_tree_ref(eb);

	for (int i = 0; i < num_extent_folios(eb); i++)
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

struct extent_buffer *alloc_test_extent_buffer(struct btrfs_fs_info *fs_info,
					u64 start)
{
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
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
#else
	/* Stub to avoid linker error when compiled with optimizations turned off. */
	return NULL;
#endif
}

static struct extent_buffer *grab_extent_buffer(struct btrfs_fs_info *fs_info,
						struct folio *folio)
{
	struct extent_buffer *exists;

	lockdep_assert_held(&folio->mapping->i_private_lock);

	/*
	 * For subpage case, we completely rely on radix tree to ensure we
	 * don't try to insert two ebs for the same bytenr.  So here we always
	 * return NULL and just continue.
	 */
	if (btrfs_meta_is_subpage(fs_info))
		return NULL;

	/* Page not yet attached to an extent buffer */
	if (!folio_test_private(folio))
		return NULL;

	/*
	 * We could have already allocated an eb for this folio and attached one
	 * so lets see if we can get a ref on the existing eb, and if we can we
	 * know it's good and we can just return that one, else we know we can
	 * just overwrite folio private.
	 */
	exists = folio_get_private(folio);
	if (atomic_inc_not_zero(&exists->refs))
		return exists;

	WARN_ON(folio_test_dirty(folio));
	folio_detach_private(folio);
	return NULL;
}

/*
 * Validate alignment constraints of eb at logical address @start.
 */
static bool check_eb_alignment(struct btrfs_fs_info *fs_info, u64 start)
{
	if (!IS_ALIGNED(start, fs_info->sectorsize)) {
		btrfs_err(fs_info, "bad tree block start %llu", start);
		return true;
	}

	if (fs_info->nodesize < PAGE_SIZE &&
	    offset_in_page(start) + fs_info->nodesize > PAGE_SIZE) {
		btrfs_err(fs_info,
		"tree block crosses page boundary, start %llu nodesize %u",
			  start, fs_info->nodesize);
		return true;
	}
	if (fs_info->nodesize >= PAGE_SIZE &&
	    !PAGE_ALIGNED(start)) {
		btrfs_err(fs_info,
		"tree block is not page aligned, start %llu nodesize %u",
			  start, fs_info->nodesize);
		return true;
	}
	if (!IS_ALIGNED(start, fs_info->nodesize) &&
	    !test_and_set_bit(BTRFS_FS_UNALIGNED_TREE_BLOCK, &fs_info->flags)) {
		btrfs_warn(fs_info,
"tree block not nodesize aligned, start %llu nodesize %u, can be resolved by a full metadata balance",
			      start, fs_info->nodesize);
	}
	return false;
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
				      struct btrfs_subpage *prealloc,
				      struct extent_buffer **found_eb_ret)
{

	struct btrfs_fs_info *fs_info = eb->fs_info;
	struct address_space *mapping = fs_info->btree_inode->i_mapping;
	const unsigned long index = eb->start >> PAGE_SHIFT;
	struct folio *existing_folio = NULL;
	int ret;

	ASSERT(found_eb_ret);

	/* Caller should ensure the folio exists. */
	ASSERT(eb->folios[i]);

retry:
	ret = filemap_add_folio(mapping, eb->folios[i], index + i,
				GFP_NOFS | __GFP_NOFAIL);
	if (!ret)
		goto finish;

	existing_folio = filemap_lock_folio(mapping, index + i);
	/* The page cache only exists for a very short time, just retry. */
	if (IS_ERR(existing_folio)) {
		existing_folio = NULL;
		goto retry;
	}

	/* For now, we should only have single-page folios for btree inode. */
	ASSERT(folio_nr_pages(existing_folio) == 1);

	if (folio_size(existing_folio) != eb->folio_size) {
		folio_unlock(existing_folio);
		folio_put(existing_folio);
		return -EAGAIN;
	}

finish:
	spin_lock(&mapping->i_private_lock);
	if (existing_folio && btrfs_meta_is_subpage(fs_info)) {
		/* We're going to reuse the existing page, can drop our folio now. */
		__free_page(folio_page(eb->folios[i], 0));
		eb->folios[i] = existing_folio;
	} else if (existing_folio) {
		struct extent_buffer *existing_eb;

		existing_eb = grab_extent_buffer(fs_info, existing_folio);
		if (existing_eb) {
			/* The extent buffer still exists, we can use it directly. */
			*found_eb_ret = existing_eb;
			spin_unlock(&mapping->i_private_lock);
			folio_unlock(existing_folio);
			folio_put(existing_folio);
			return 1;
		}
		/* The extent buffer no longer exists, we can reuse the folio. */
		__free_page(folio_page(eb->folios[i], 0));
		eb->folios[i] = existing_folio;
	}
	eb->folio_size = folio_size(eb->folios[i]);
	eb->folio_shift = folio_shift(eb->folios[i]);
	/* Should not fail, as we have preallocated the memory. */
	ret = attach_extent_buffer_folio(eb, eb->folios[i], prealloc);
	ASSERT(!ret);
	/*
	 * To inform we have an extra eb under allocation, so that
	 * detach_extent_buffer_page() won't release the folio private when the
	 * eb hasn't been inserted into radix tree yet.
	 *
	 * The ref will be decreased when the eb releases the page, in
	 * detach_extent_buffer_page().  Thus needs no special handling in the
	 * error path.
	 */
	btrfs_folio_inc_eb_refs(fs_info, eb->folios[i]);
	spin_unlock(&mapping->i_private_lock);
	return 0;
}

struct extent_buffer *alloc_extent_buffer(struct btrfs_fs_info *fs_info,
					  u64 start, u64 owner_root, int level)
{
	int attached = 0;
	struct extent_buffer *eb;
	struct extent_buffer *existing_eb = NULL;
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

	eb = __alloc_extent_buffer(fs_info, start);
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
	 * allocate memory with i_private_lock nor page lock hold.
	 *
	 * The memory will be freed by attach_extent_buffer_page() or freed
	 * manually if we exit earlier.
	 */
	if (btrfs_meta_is_subpage(fs_info)) {
		prealloc = btrfs_alloc_subpage(fs_info, PAGE_SIZE, BTRFS_SUBPAGE_METADATA);
		if (IS_ERR(prealloc)) {
			ret = PTR_ERR(prealloc);
			goto out;
		}
	}

reallocate:
	/* Allocate all pages first. */
	ret = alloc_eb_folio_array(eb, true);
	if (ret < 0) {
		btrfs_free_subpage(prealloc);
		goto out;
	}

	/* Attach all pages to the filemap. */
	for (int i = 0; i < num_extent_folios(eb); i++) {
		struct folio *folio;

		ret = attach_eb_folio_to_filemap(eb, i, prealloc, &existing_eb);
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
		WARN_ON(btrfs_meta_folio_test_dirty(folio, eb));

		/*
		 * Check if the current page is physically contiguous with previous eb
		 * page.
		 * At this stage, either we allocated a large folio, thus @i
		 * would only be 0, or we fall back to per-page allocation.
		 */
		if (i && folio_page(eb->folios[i - 1], 0) + 1 != folio_page(folio, 0))
			page_contig = false;

		if (!btrfs_meta_folio_test_uptodate(folio, eb))
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
	for (int i = 0; i < num_extent_folios(eb); i++)
		folio_unlock(eb->folios[i]);
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
		folio_unlock(eb->folios[i]);
		folio_put(eb->folios[i]);
		eb->folios[i] = NULL;
	}
	/*
	 * Now all pages of that extent buffer is unmapped, set UNMAPPED flag,
	 * so it can be cleaned up without utilizing folio->mapping.
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

	kmem_cache_free(extent_buffer_cache, eb);
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
		/* Should be safe to release folios at this point. */
		btrfs_release_extent_buffer_folios(eb);
#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
		if (unlikely(test_bit(EXTENT_BUFFER_UNMAPPED, &eb->bflags))) {
			kmem_cache_free(extent_buffer_cache, eb);
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

static void btree_clear_folio_dirty_tag(struct folio *folio)
{
	ASSERT(!folio_test_dirty(folio));
	ASSERT(folio_test_locked(folio));
	xa_lock_irq(&folio->mapping->i_pages);
	if (!folio_test_dirty(folio))
		__xa_clear_mark(&folio->mapping->i_pages,
				folio_index(folio), PAGECACHE_TAG_DIRTY);
	xa_unlock_irq(&folio->mapping->i_pages);
}

void btrfs_clear_buffer_dirty(struct btrfs_trans_handle *trans,
			      struct extent_buffer *eb)
{
	struct btrfs_fs_info *fs_info = eb->fs_info;

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
	if (btrfs_is_zoned(fs_info) && test_bit(EXTENT_BUFFER_DIRTY, &eb->bflags)) {
		set_bit(EXTENT_BUFFER_ZONED_ZEROOUT, &eb->bflags);
		return;
	}

	if (!test_and_clear_bit(EXTENT_BUFFER_DIRTY, &eb->bflags))
		return;

	percpu_counter_add_batch(&fs_info->dirty_metadata_bytes, -eb->len,
				 fs_info->dirty_metadata_batch);

	for (int i = 0; i < num_extent_folios(eb); i++) {
		struct folio *folio = eb->folios[i];
		bool last;

		if (!folio_test_dirty(folio))
			continue;
		folio_lock(folio);
		last = btrfs_meta_folio_clear_and_test_dirty(folio, eb);
		if (last)
			btree_clear_folio_dirty_tag(folio);
		folio_unlock(folio);
	}
	WARN_ON(atomic_read(&eb->refs) == 0);
}

void set_extent_buffer_dirty(struct extent_buffer *eb)
{
	bool was_dirty;

	check_buffer_tree_ref(eb);

	was_dirty = test_and_set_bit(EXTENT_BUFFER_DIRTY, &eb->bflags);

	WARN_ON(atomic_read(&eb->refs) == 0);
	WARN_ON(!test_bit(EXTENT_BUFFER_TREE_REF, &eb->bflags));
	WARN_ON(test_bit(EXTENT_BUFFER_ZONED_ZEROOUT, &eb->bflags));

	if (!was_dirty) {
		bool subpage = btrfs_meta_is_subpage(eb->fs_info);

		/*
		 * For subpage case, we can have other extent buffers in the
		 * same page, and in clear_extent_buffer_dirty() we
		 * have to clear page dirty without subpage lock held.
		 * This can cause race where our page gets dirty cleared after
		 * we just set it.
		 *
		 * Thankfully, clear_extent_buffer_dirty() has locked
		 * its page for other reasons, we can use page lock to prevent
		 * the above race.
		 */
		if (subpage)
			folio_lock(eb->folios[0]);
		for (int i = 0; i < num_extent_folios(eb); i++)
			btrfs_meta_folio_set_dirty(eb->folios[i], eb);
		if (subpage)
			folio_unlock(eb->folios[0]);
		percpu_counter_add_batch(&eb->fs_info->dirty_metadata_bytes,
					 eb->len,
					 eb->fs_info->dirty_metadata_batch);
	}
#ifdef CONFIG_BTRFS_DEBUG
	for (int i = 0; i < num_extent_folios(eb); i++)
		ASSERT(folio_test_dirty(eb->folios[i]));
#endif
}

void clear_extent_buffer_uptodate(struct extent_buffer *eb)
{

	clear_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	for (int i = 0; i < num_extent_folios(eb); i++) {
		struct folio *folio = eb->folios[i];

		if (!folio)
			continue;

		btrfs_meta_folio_clear_uptodate(folio, eb);
	}
}

void set_extent_buffer_uptodate(struct extent_buffer *eb)
{

	set_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags);
	for (int i = 0; i < num_extent_folios(eb); i++)
		btrfs_meta_folio_set_uptodate(eb->folios[i], eb);
}

static void clear_extent_buffer_reading(struct extent_buffer *eb)
{
	clear_bit(EXTENT_BUFFER_READING, &eb->bflags);
	smp_mb__after_atomic();
	wake_up_bit(&eb->bflags, EXTENT_BUFFER_READING);
}

static void end_bbio_meta_read(struct btrfs_bio *bbio)
{
	struct extent_buffer *eb = bbio->private;
	bool uptodate = !bbio->bio.bi_status;

	/*
	 * If the extent buffer is marked UPTODATE before the read operation
	 * completes, other calls to read_extent_buffer_pages() will return
	 * early without waiting for the read to finish, causing data races.
	 */
	WARN_ON(test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags));

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

	clear_extent_buffer_reading(eb);
	free_extent_buffer(eb);

	bio_put(&bbio->bio);
}

int read_extent_buffer_pages_nowait(struct extent_buffer *eb, int mirror_num,
				    const struct btrfs_tree_parent_check *check)
{
	struct btrfs_bio *bbio;

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
		return 0;

	/*
	 * Between the initial test_bit(EXTENT_BUFFER_UPTODATE) and the above
	 * test_and_set_bit(EXTENT_BUFFER_READING), someone else could have
	 * started and finished reading the same eb.  In this case, UPTODATE
	 * will now be set, and we shouldn't read it in again.
	 */
	if (unlikely(test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))) {
		clear_extent_buffer_reading(eb);
		return 0;
	}

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
	for (int i = 0; i < num_extent_folios(eb); i++) {
		struct folio *folio = eb->folios[i];
		u64 range_start = max_t(u64, eb->start, folio_pos(folio));
		u32 range_len = min_t(u64, folio_pos(folio) + folio_size(folio),
				      eb->start + eb->len) - range_start;

		bio_add_folio_nofail(&bbio->bio, folio, range_len,
				     offset_in_folio(folio, range_start));
	}
	btrfs_submit_bbio(bbio, mirror_num);
	return 0;
}

int read_extent_buffer_pages(struct extent_buffer *eb, int mirror_num,
			     const struct btrfs_tree_parent_check *check)
{
	int ret;

	ret = read_extent_buffer_pages_nowait(eb, mirror_num, check);
	if (ret < 0)
		return ret;

	wait_on_bit_io(&eb->bflags, EXTENT_BUFFER_READING, TASK_UNINTERRUPTIBLE);
	if (!test_bit(EXTENT_BUFFER_UPTODATE, &eb->bflags))
		return -EIO;
	return 0;
}

static bool report_eb_range(const struct extent_buffer *eb, unsigned long start,
			    unsigned long len)
{
	btrfs_warn(eb->fs_info,
		"access to eb bytenr %llu len %u out of range start %lu len %lu",
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
	const int unit_size = eb->folio_size;
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
	const int unit_size = eb->folio_size;
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
	const int unit_size = eb->folio_size;
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

	if (btrfs_meta_is_subpage(fs_info)) {
		folio = eb->folios[0];
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
	const int unit_size = eb->folio_size;
	size_t cur;
	size_t offset;
	char *kaddr;
	const char *src = (const char *)srcv;
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
	const int unit_size = eb->folio_size;
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
	const int unit_size = src->folio_size;
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
	const int unit_size = dst->folio_size;
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
	offset = start + offset_in_eb_folio(eb, eb->start) + byte_offset;

	*folio_index = offset >> eb->folio_shift;
	*folio_offset = offset_in_eb_folio(eb, offset);
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
	const int unit_size = dst->folio_size;
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
		const struct btrfs_fs_info *fs_info, struct folio *folio, u64 bytenr)
{
	struct extent_buffer *gang[GANG_LOOKUP_SIZE];
	struct extent_buffer *found = NULL;
	u64 folio_start = folio_pos(folio);
	u64 cur = folio_start;

	ASSERT(in_range(bytenr, folio_start, PAGE_SIZE));
	lockdep_assert_held(&fs_info->buffer_lock);

	while (cur < folio_start + PAGE_SIZE) {
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
			if (gang[i]->start >= folio_start + PAGE_SIZE)
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

static int try_release_subpage_extent_buffer(struct folio *folio)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);
	u64 cur = folio_pos(folio);
	const u64 end = cur + PAGE_SIZE;
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
		eb = get_next_extent_buffer(fs_info, folio, cur);
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
	spin_lock(&folio->mapping->i_private_lock);
	if (!folio_test_private(folio))
		ret = 1;
	else
		ret = 0;
	spin_unlock(&folio->mapping->i_private_lock);
	return ret;

}

int try_release_extent_buffer(struct folio *folio)
{
	struct extent_buffer *eb;

	if (btrfs_meta_is_subpage(folio_to_fs_info(folio)))
		return try_release_subpage_extent_buffer(folio);

	/*
	 * We need to make sure nobody is changing folio private, as we rely on
	 * folio private as the pointer to extent buffer.
	 */
	spin_lock(&folio->mapping->i_private_lock);
	if (!folio_test_private(folio)) {
		spin_unlock(&folio->mapping->i_private_lock);
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
		spin_unlock(&folio->mapping->i_private_lock);
		return 0;
	}
	spin_unlock(&folio->mapping->i_private_lock);

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

	ret = read_extent_buffer_pages_nowait(eb, 0, &check);
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
