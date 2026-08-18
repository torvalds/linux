// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include "messages.h"
#include "subpage.h"
#include "btrfs_inode.h"

/*
 * Subpage (block size < folio size) support overview:
 *
 * Limitations:
 *
 * - Metadata must be fully aligned to node size
 *   So when nodesize <= page size, the metadata can never cross folio boundaries.
 *
 * - Only support blocks per folio <= min(BTRFS_MAX_FOLIO_SIZE / fs block size,
 *					  BTRFS_MAX_BLOCKS_PER_FOLIO)
 *   This is to ensure we can afford an on-stack bitmap, without the need to allocate
 *   bitmap memory at runtime.
 *
 * Implementation:
 *
 * - Common
 *   Both metadata and data will use a new structure, btrfs_folio_state, to
 *   record the status of each sector inside a page.  This provides the extra
 *   granularity needed.
 *
 * - Metadata
 *   Since we have multiple tree blocks inside one page, we can't rely on page
 *   locking anymore, or we will have greatly reduced concurrency or even
 *   deadlocks (hold one tree lock while trying to lock another tree lock in
 *   the same page).
 *
 *   Thus for metadata locking, subpage support relies on io_tree locking only.
 *   This means a slightly higher tree locking latency.
 */

int btrfs_attach_folio_state(const struct btrfs_fs_info *fs_info,
			     struct folio *folio, enum btrfs_folio_type type)
{
	struct btrfs_folio_state *bfs;

	/* For metadata we don't support large folio yet. */
	if (type == BTRFS_SUBPAGE_METADATA)
		ASSERT(!folio_test_large(folio));

	/*
	 * We have cases like a dummy extent buffer page, which is not mapped
	 * and doesn't need to be locked.
	 */
	if (folio->mapping)
		ASSERT(folio_test_locked(folio));

	/* Either not subpage, or the folio already has private attached. */
	if (folio_test_private(folio))
		return 0;
	if (type == BTRFS_SUBPAGE_METADATA && !btrfs_meta_is_subpage(fs_info))
		return 0;
	if (type == BTRFS_SUBPAGE_DATA && !btrfs_is_subpage(fs_info, folio))
		return 0;

	bfs = btrfs_alloc_folio_state(fs_info, folio_size(folio), type);
	if (IS_ERR(bfs))
		return PTR_ERR(bfs);

	folio_attach_private(folio, bfs);
	return 0;
}

void btrfs_detach_folio_state(const struct btrfs_fs_info *fs_info, struct folio *folio,
			      enum btrfs_folio_type type)
{
	struct btrfs_folio_state *bfs;

	/* Either not subpage, or the folio already has private attached. */
	if (!folio_test_private(folio))
		return;
	if (type == BTRFS_SUBPAGE_METADATA && !btrfs_meta_is_subpage(fs_info))
		return;
	if (type == BTRFS_SUBPAGE_DATA && !btrfs_is_subpage(fs_info, folio))
		return;

	bfs = folio_detach_private(folio);
	ASSERT(bfs);
	btrfs_free_folio_state(bfs);
}

struct btrfs_folio_state *btrfs_alloc_folio_state(const struct btrfs_fs_info *fs_info,
						  size_t fsize, enum btrfs_folio_type type)
{
	struct btrfs_folio_state *ret;
	unsigned int real_size;

	ASSERT(fs_info->sectorsize < fsize);

	real_size = struct_size(ret, bitmaps,
			BITS_TO_LONGS(btrfs_bitmap_nr_max *
				      (fsize >> fs_info->sectorsize_bits)));
	ret = kzalloc(real_size, GFP_NOFS);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&ret->lock);
	if (type == BTRFS_SUBPAGE_METADATA)
		atomic_set(&ret->eb_refs, 0);
	else
		atomic_set(&ret->nr_locked, 0);
	return ret;
}

/*
 * Increase the eb_refs of current subpage.
 *
 * This is important for eb allocation, to prevent race with last eb freeing
 * of the same page.
 * With the eb_refs increased before the eb inserted into radix tree,
 * detach_extent_buffer_page() won't detach the folio private while we're still
 * allocating the extent buffer.
 */
void btrfs_folio_inc_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio)
{
	struct btrfs_folio_state *bfs;

	if (!btrfs_meta_is_subpage(fs_info))
		return;

	ASSERT(folio_test_private(folio) && folio->mapping);
	lockdep_assert_held(&folio->mapping->i_private_lock);

	bfs = folio_get_private(folio);
	atomic_inc(&bfs->eb_refs);
}

void btrfs_folio_dec_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio)
{
	struct btrfs_folio_state *bfs;

	if (!btrfs_meta_is_subpage(fs_info))
		return;

	ASSERT(folio_test_private(folio) && folio->mapping);
	lockdep_assert_held(&folio->mapping->i_private_lock);

	bfs = folio_get_private(folio);
	ASSERT(atomic_read(&bfs->eb_refs));
	atomic_dec(&bfs->eb_refs);
}

static void btrfs_subpage_assert(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	/* Basic checks */
	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(len, fs_info->sectorsize), "start=%llu len=%u", start, len);
	/*
	 * The range check only works for mapped page, we can still have
	 * unmapped page like dummy extent buffer pages.
	 */
	if (folio->mapping)
		ASSERT(folio_pos(folio) <= start &&
		       start + len <= folio_next_pos(folio),
		       "start=%llu len=%u folio_pos=%llu folio_size=%zu",
		       start, len, folio_pos(folio), folio_size(folio));
}

#define subpage_calc_start_bit(fs_info, folio, name, start, len)	\
({									\
	unsigned int __start_bit;					\
	const unsigned int __bpf = btrfs_blocks_per_folio(fs_info, folio); \
									\
	btrfs_subpage_assert(fs_info, folio, start, len);		\
	__start_bit = offset_in_folio(folio, start) >> fs_info->sectorsize_bits; \
	__start_bit += __bpf * btrfs_bitmap_nr_##name;			\
	__start_bit;							\
})

static void btrfs_subpage_clamp_range(struct folio *folio, u64 *start, u32 *len)
{
	u64 orig_start = *start;
	u32 orig_len = *len;

	*start = max_t(u64, folio_pos(folio), orig_start);
	/*
	 * For certain call sites like btrfs_drop_pages(), we may have pages
	 * beyond the target range. In that case, just set @len to 0, subpage
	 * helpers can handle @len == 0 without any problem.
	 */
	if (folio_pos(folio) >= orig_start + orig_len)
		*len = 0;
	else
		*len = min_t(u64, folio_next_pos(folio), orig_start + orig_len) - *start;
}

static bool btrfs_subpage_end_and_test_lock(const struct btrfs_fs_info *fs_info,
					    struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	const int nbits = (len >> fs_info->sectorsize_bits);
	unsigned long flags;
	bool last;

	btrfs_subpage_assert(fs_info, folio, start, len);

	spin_lock_irqsave(&bfs->lock, flags);
	/*
	 * We have call sites passing @lock_page into
	 * extent_clear_unlock_delalloc() for compression path.
	 *
	 * This @locked_page is locked by plain lock_page(), thus its
	 * subpage::locked is 0.  Handle them in a special way.
	 */
	if (atomic_read(&bfs->nr_locked) == 0) {
		spin_unlock_irqrestore(&bfs->lock, flags);
		return true;
	}
	ASSERT(atomic_read(&bfs->nr_locked) >= nbits,
	       "atomic_read(&bfs->nr_locked)=%d nbits=%d",
	       atomic_read(&bfs->nr_locked), nbits);
	last = atomic_sub_and_test(nbits, &bfs->nr_locked);
	spin_unlock_irqrestore(&bfs->lock, flags);
	return last;
}

/*
 * Handle different locked folios:
 *
 * - Non-subpage folio
 *   Just unlock it.
 *
 * - folio locked but without any subpage locked
 *   This happens either before writepage_delalloc() or the delalloc range is
 *   already handled by previous folio.
 *   We can simple unlock it.
 *
 * - folio locked with subpage range locked.
 *   We go through the locked sectors inside the range and clear their locked
 *   bitmap, reduce the writer lock number, and unlock the page if that's
 *   the last locked range.
 */
void btrfs_folio_end_lock(const struct btrfs_fs_info *fs_info,
			  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);

	ASSERT(folio_test_locked(folio));

	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, folio)) {
		folio_unlock(folio);
		return;
	}

	/*
	 * For subpage case, there are two types of locked page.  With or
	 * without locked number.
	 *
	 * Since we own the page lock, no one else could touch subpage::locked
	 * and we are safe to do several atomic operations without spinlock.
	 */
	if (atomic_read(&bfs->nr_locked) == 0) {
		/* No subpage lock, locked by plain lock_page(). */
		folio_unlock(folio);
		return;
	}

	btrfs_subpage_clamp_range(folio, &start, &len);
	if (btrfs_subpage_end_and_test_lock(fs_info, folio, start, len))
		folio_unlock(folio);
}

void btrfs_folio_end_lock_bitmap(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, unsigned long *bitmap)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);
	const unsigned int nbits = bitmap_weight(bitmap, blocks_per_folio);
	unsigned long flags;
	bool last = false;

	if (!btrfs_is_subpage(fs_info, folio)) {
		folio_unlock(folio);
		return;
	}

	if (atomic_read(&bfs->nr_locked) == 0) {
		/* No subpage lock, locked by plain lock_page(). */
		folio_unlock(folio);
		return;
	}

	spin_lock_irqsave(&bfs->lock, flags);
	ASSERT(atomic_read(&bfs->nr_locked) >= nbits,
	       "atomic_read(&bfs->nr_locked)=%d nbits=%d",
	       atomic_read(&bfs->nr_locked), nbits);
	last = atomic_sub_and_test(nbits, &bfs->nr_locked);
	spin_unlock_irqrestore(&bfs->lock, flags);
	if (last)
		folio_unlock(folio);
}

#define subpage_test_bitmap_all_set(fs_info, folio, name)		\
({									\
	struct btrfs_folio_state *__bfs = folio_get_private(folio);	\
	const unsigned int __bpf = btrfs_blocks_per_folio(fs_info, folio); \
									\
	bitmap_test_range_all_set(__bfs->bitmaps,			\
				  __bpf * btrfs_bitmap_nr_##name, __bpf); \
})

#define subpage_test_bitmap_all_zero(fs_info, folio, name)		\
({									\
	struct btrfs_folio_state *__bfs = folio_get_private(folio);	\
	const unsigned int __bpf = btrfs_blocks_per_folio(fs_info, folio); \
									\
	bitmap_test_range_all_zero(__bfs->bitmaps,			\
				   __bpf * btrfs_bitmap_nr_##name, __bpf); \
})

void btrfs_subpage_set_uptodate(const struct btrfs_fs_info *fs_info,
				struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							uptodate, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_set(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_set(fs_info, folio, uptodate))
		folio_mark_uptodate(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_subpage_clear_uptodate(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							uptodate, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_clear(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	folio_clear_uptodate(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

/*
 * folio_mark_dirty() for a folio we are dirtying with a space reservation.
 *
 * Dirtiers without a reservation use btrfs_data_dirty_folio().
 */
static void btrfs_folio_mark_dirty(struct folio *folio)
{
	struct address_space *mapping = folio_mapping(folio);

	if (!mapping || !mapping->host || !is_data_inode(BTRFS_I(mapping->host))) {
		folio_mark_dirty(folio);
		return;
	}
	if (folio_test_reclaim(folio))
		folio_clear_reclaim(folio);
	filemap_dirty_folio(mapping, folio);
}

/*
 * The set helper of the dirty ops, so it only runs for folios without a
 * fixup bitmap: for those the folio flag is the whole fixup state, and this
 * reserving write covers the block, so retire it.  Metadata never has the
 * flag set and only pays the test.
 */
static void btrfs_folio_mark_dirty_reserved(struct folio *folio)
{
	if (folio_test_fixup_pending(folio))
		folio_clear_fixup_pending(folio);
	btrfs_folio_mark_dirty(folio);
}

void btrfs_subpage_set_dirty(const struct btrfs_fs_info *fs_info,
			     struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int dirty_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned int fixup_bit = subpage_calc_start_bit(fs_info, folio,
							fixup, start, len);
	const unsigned int nbits = len >> fs_info->sectorsize_bits;
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_set(bfs->bitmaps, dirty_bit, nbits);
	/* Proper dirtying obviates the need for fixup. */
	bitmap_clear(bfs->bitmaps, fixup_bit, nbits);
	if (folio_test_fixup_pending(folio) &&
	    subpage_test_bitmap_all_zero(fs_info, folio, fixup))
		folio_clear_fixup_pending(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
	btrfs_folio_mark_dirty(folio);
}

static void folio_clear_tags(struct folio *folio)
{
	struct address_space *mapping = folio_mapping(folio);
	XA_STATE(xas, &mapping->i_pages, folio->index);
	unsigned long flags;

	ASSERT(folio_test_locked(folio));
	ASSERT(mapping);
	ASSERT(mapping_use_writeback_tags(mapping));

	xas_lock_irqsave(&xas, flags);
	xas_load(&xas);
	xas_clear_mark(&xas, PAGECACHE_TAG_DIRTY);
	xas_clear_mark(&xas, PAGECACHE_TAG_TOWRITE);
	xas_unlock_irqrestore(&xas, flags);
}

/*
 * Extra clear_and_test function for subpage dirty bitmap.
 *
 * Return true if we're the last bits in the dirty_bitmap and clear the
 * dirty_bitmap.
 * Return false otherwise.
 *
 * NOTE: Callers should manually clear page dirty for true case, as we have
 * extra handling for tree blocks.
 */
bool btrfs_subpage_clear_and_test_dirty(const struct btrfs_fs_info *fs_info,
					struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned long flags;
	bool last = false;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_clear(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, folio, dirty))
		last = true;
	spin_unlock_irqrestore(&bfs->lock, flags);
	return last;
}

void btrfs_subpage_clear_dirty(const struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	bool last;

	last = btrfs_subpage_clear_and_test_dirty(fs_info, folio, start, len);
	if (last)
		folio_clear_dirty_for_io(folio);
}

void btrfs_subpage_set_writeback(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							writeback, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_set(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);

	/*
	 * Don't clear the TOWRITE tag when starting writeback on a still-dirty
	 * folio. Doing so can cause WB_SYNC_ALL writepages() to overlook it,
	 * assume writeback is complete, and exit too early — violating sync
	 * ordering guarantees.
	 *
	 * Instead we manually clear the DIRTY and TOWRITE tags after the folio
	 * is no longer dirty.
	 */
	if (!folio_test_writeback(folio))
		__folio_start_writeback(folio, true);
	if (!folio_test_dirty(folio))
		folio_clear_tags(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_subpage_clear_writeback(const struct btrfs_fs_info *fs_info,
				   struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							writeback, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_clear(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, folio, writeback)) {
		ASSERT(folio_test_writeback(folio));
		folio_end_writeback(folio);
	}
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_subpage_clear_fixup(const struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							fixup, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_clear(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, folio, fixup))
		folio_clear_fixup_pending(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

/*
 * In one pass under bfs->lock, mark every block with a clear dirty bit in the
 * range both dirty and needing fixup.
 *
 * Only called from the dirty_folio callback, which owns the folio-level
 * dirty flag; calling folio_mark_dirty() here would recurse.
 *
 * The folio fixup flag and bits are both set under bfs->lock so that a
 * writeback pass observing the new bits also observes the flag.
 */
static void btrfs_subpage_set_fixup_dirty(const struct btrfs_fs_info *fs_info,
					  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int dirty_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned int fixup_bit = subpage_calc_start_bit(fs_info, folio,
							fixup, start, len);
	const unsigned int nbits = len >> fs_info->sectorsize_bits;
	unsigned long flags;
	bool marked = false;

	spin_lock_irqsave(&bfs->lock, flags);
	for (unsigned int i = 0; i < nbits; i++) {
		if (test_bit(dirty_bit + i, bfs->bitmaps))
			continue;
		set_bit(dirty_bit + i, bfs->bitmaps);
		set_bit(fixup_bit + i, bfs->bitmaps);
		marked = true;
	}
	if (marked)
		folio_set_fixup_pending(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

/*
 * Mark the still-clean blocks of a folio dirty and needing fixup, for
 * btrfs_data_dirty_folio().
 *
 * A subpage block size folio that is not uptodate is left alone: its clean
 * blocks may hold content that was never read in, which must not be marked
 * dirty.
 */
void btrfs_folio_set_fixup_dirty(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	if (!btrfs_is_subpage(fs_info, folio)) {
		if (!folio_test_dirty(folio))
			folio_set_fixup_pending(folio);
		return;
	}
	if (!folio_test_uptodate(folio))
		return;
	btrfs_subpage_set_fixup_dirty(fs_info, folio, start, len);
}

/*
 * Drop the fixup blocks inside the range: clear both their fixup and dirty
 * bits.
 *
 * Fixup blocks carry no space reservation, so their fixup and dirty bits
 * must be dropped together. Clearing only the fixup bit would leave a
 * dirty block without a reservation which is not a valid state.
 *
 * Returns true if the folio has no dirty blocks left.
 */
static bool btrfs_subpage_clear_fixup_dirty(const struct btrfs_fs_info *fs_info,
					    struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int dirty_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned int fixup_bit = subpage_calc_start_bit(fs_info, folio,
							fixup, start, len);
	const unsigned int nbits = len >> fs_info->sectorsize_bits;
	unsigned long flags;
	bool last;

	spin_lock_irqsave(&bfs->lock, flags);
	for (unsigned int i = 0; i < nbits; i++) {
		if (!test_bit(fixup_bit + i, bfs->bitmaps))
			continue;
		clear_bit(fixup_bit + i, bfs->bitmaps);
		clear_bit(dirty_bit + i, bfs->bitmaps);
	}
	if (subpage_test_bitmap_all_zero(fs_info, folio, fixup))
		folio_clear_fixup_pending(folio);
	last = subpage_test_bitmap_all_zero(fs_info, folio, dirty);
	spin_unlock_irqrestore(&bfs->lock, flags);
	return last;
}

/*
 * Drop the fixup blocks inside the range, for callers discarding their data:
 * btrfs_invalidate_folio() and the writepage fixup worker's error path.
 *
 * Callers that have just reserved space for a block want
 * btrfs_folio_clear_fixup() instead - there the block stays dirty and gets
 * written.
 *
 * The range can be byte-granular (an unaligned truncate through
 * btrfs_invalidate_folio()); only blocks fully inside it are dropped, as a
 * partially covered block still holds live data outside the range.  For
 * single-block folios the folio flag is the fixup state, so it is dropped
 * only when the range covers the whole folio.
 */
void btrfs_folio_clear_fixup_dirty(const struct btrfs_fs_info *fs_info,
				   struct folio *folio, u64 start, u32 len)
{
	u64 aligned_start;
	u64 aligned_end;

	/* The folio flag is set whenever any fixup bitmap bit is. */
	if (!folio_test_fixup_pending(folio))
		return;
	if (!btrfs_is_subpage(fs_info, folio)) {
		if (start <= folio_pos(folio) &&
		    start + len >= folio_next_pos(folio)) {
			folio_clear_fixup_pending(folio);
			folio_clear_dirty_for_io(folio);
		}
		return;
	}
	btrfs_subpage_clamp_range(folio, &start, &len);
	aligned_start = round_up(start, fs_info->sectorsize);
	aligned_end = round_down(start + len, fs_info->sectorsize);
	if (aligned_end <= aligned_start)
		return;
	if (btrfs_subpage_clear_fixup_dirty(fs_info, folio, aligned_start,
					    aligned_end - aligned_start))
		folio_clear_dirty_for_io(folio);
}

bool btrfs_folio_test_fixup(const struct btrfs_fs_info *fs_info,
			    struct folio *folio, u64 start, u32 len)
{
	if (!btrfs_is_subpage(fs_info, folio))
		return folio_test_fixup_pending(folio);
	return btrfs_subpage_test_fixup(fs_info, folio, start, len);
}

void btrfs_folio_clear_fixup(const struct btrfs_fs_info *fs_info,
			     struct folio *folio, u64 start, u32 len)
{
	if (!btrfs_is_subpage(fs_info, folio)) {
		folio_clear_fixup_pending(folio);
		return;
	}
	btrfs_subpage_clear_fixup(fs_info, folio, start, len);
}

/*
 * Unlike set/clear which is dependent on each page status, for test all bits
 * are tested in the same way.
 */
#define IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(name)				\
bool btrfs_subpage_test_##name(const struct btrfs_fs_info *fs_info,	\
			       struct folio *folio, u64 start, u32 len)	\
{									\
	struct btrfs_folio_state *bfs = folio_get_private(folio);	\
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,	\
						name, start, len);	\
	unsigned long flags;						\
	bool ret;							\
									\
	spin_lock_irqsave(&bfs->lock, flags);			\
	ret = bitmap_test_range_all_set(bfs->bitmaps, start_bit,	\
				len >> fs_info->sectorsize_bits);	\
	spin_unlock_irqrestore(&bfs->lock, flags);			\
	return ret;							\
}
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(uptodate);
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(dirty);
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(writeback);
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(fixup);

/*
 * Note that, in selftests (extent-io-tests), we can have empty fs_info passed
 * in.  We only test sectorsize == PAGE_SIZE cases so far, thus we can fall
 * back to regular sectorsize branch.
 */
#define IMPLEMENT_BTRFS_PAGE_OPS(name, folio_set_func,			\
				 folio_clear_func, folio_test_func)	\
void btrfs_folio_set_##name(const struct btrfs_fs_info *fs_info,	\
			    struct folio *folio, u64 start, u32 len)	\
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio)) {			\
		folio_set_func(folio);					\
		return;							\
	}								\
	btrfs_subpage_set_##name(fs_info, folio, start, len);		\
}									\
void btrfs_folio_clear_##name(const struct btrfs_fs_info *fs_info,	\
			      struct folio *folio, u64 start, u32 len)	\
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio)) {			\
		folio_clear_func(folio);				\
		return;							\
	}								\
	btrfs_subpage_clear_##name(fs_info, folio, start, len);		\
}									\
bool btrfs_folio_test_##name(const struct btrfs_fs_info *fs_info,	\
			     struct folio *folio, u64 start, u32 len)	\
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio))				\
		return folio_test_func(folio);				\
	return btrfs_subpage_test_##name(fs_info, folio, start, len);	\
}									\
void btrfs_folio_clamp_set_##name(const struct btrfs_fs_info *fs_info,	\
				  struct folio *folio, u64 start, u32 len) \
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio)) {			\
		folio_set_func(folio);					\
		return;							\
	}								\
	btrfs_subpage_clamp_range(folio, &start, &len);			\
	btrfs_subpage_set_##name(fs_info, folio, start, len);		\
}									\
void btrfs_folio_clamp_clear_##name(const struct btrfs_fs_info *fs_info, \
				    struct folio *folio, u64 start, u32 len) \
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio)) {			\
		folio_clear_func(folio);				\
		return;							\
	}								\
	btrfs_subpage_clamp_range(folio, &start, &len);			\
	btrfs_subpage_clear_##name(fs_info, folio, start, len);		\
}									\
bool btrfs_folio_clamp_test_##name(const struct btrfs_fs_info *fs_info,	\
				   struct folio *folio, u64 start, u32 len) \
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio))				\
		return folio_test_func(folio);				\
	btrfs_subpage_clamp_range(folio, &start, &len);			\
	return btrfs_subpage_test_##name(fs_info, folio, start, len);	\
}									\
void btrfs_meta_folio_set_##name(struct folio *folio, const struct extent_buffer *eb) \
{									\
	if (!btrfs_meta_is_subpage(eb->fs_info)) {			\
		folio_set_func(folio);					\
		return;							\
	}								\
	btrfs_subpage_set_##name(eb->fs_info, folio, eb->start, eb->len); \
}									\
void btrfs_meta_folio_clear_##name(struct folio *folio, const struct extent_buffer *eb) \
{									\
	if (!btrfs_meta_is_subpage(eb->fs_info)) {			\
		folio_clear_func(folio);				\
		return;							\
	}								\
	btrfs_subpage_clear_##name(eb->fs_info, folio, eb->start, eb->len); \
}									\
bool btrfs_meta_folio_test_##name(struct folio *folio, const struct extent_buffer *eb) \
{									\
	if (!btrfs_meta_is_subpage(eb->fs_info))			\
		return folio_test_func(folio);				\
	return btrfs_subpage_test_##name(eb->fs_info, folio, eb->start, eb->len); \
}
IMPLEMENT_BTRFS_PAGE_OPS(uptodate, folio_mark_uptodate, folio_clear_uptodate,
			 folio_test_uptodate);
IMPLEMENT_BTRFS_PAGE_OPS(dirty, btrfs_folio_mark_dirty_reserved,
			 folio_clear_dirty_for_io, folio_test_dirty);
IMPLEMENT_BTRFS_PAGE_OPS(writeback, folio_start_writeback, folio_end_writeback,
			 folio_test_writeback);

#define DEFINE_GET_SUBPAGE_BITMAP(name)						\
static inline unsigned long get_bitmap_value_##name(				\
					const struct btrfs_fs_info *fs_info,	\
					struct folio *folio)			\
{										\
	const unsigned int __bpf = btrfs_blocks_per_folio(fs_info, folio);	\
	const struct btrfs_folio_state *__bfs = folio_get_private(folio);	\
	unsigned long value;							\
										\
	ASSERT(__bpf <= BITS_PER_LONG);						\
	value = bitmap_read(__bfs->bitmaps, __bpf * btrfs_bitmap_nr_##name,	\
			     __bpf);						\
	return value;								\
}										\
static inline const unsigned long *get_bitmap_pointer_##name(			\
					const struct btrfs_fs_info *fs_info,	\
					struct folio *folio)			\
{										\
	const unsigned int __bpf = btrfs_blocks_per_folio(fs_info, folio);	\
	struct btrfs_folio_state *__bfs = folio_get_private(folio);		\
	unsigned long *pointer;							\
										\
	ASSERT(__bpf >= BITS_PER_LONG);						\
	ASSERT(IS_ALIGNED(__bpf, BITS_PER_LONG));				\
	pointer = __bfs->bitmaps + (BIT_WORD(__bpf) * btrfs_bitmap_nr_##name);	\
	return pointer;								\
}

DEFINE_GET_SUBPAGE_BITMAP(uptodate);
DEFINE_GET_SUBPAGE_BITMAP(dirty);
DEFINE_GET_SUBPAGE_BITMAP(writeback);

#define SUBPAGE_DUMP_BITMAP(fs_info, folio, name, start, len)			\
{										\
	const unsigned int __bpf = btrfs_blocks_per_folio(fs_info, folio);	\
										\
	if (__bpf <= BITS_PER_LONG) {						\
		unsigned long bitmap = get_bitmap_value_##name(fs_info, folio);	\
										\
		btrfs_warn(fs_info,						\
	"dumping bitmap start=%llu len=%u folio=%llu " #name "_bitmap=%*pbl",	\
		   start, len, folio_pos(folio), __bpf, &bitmap);		\
	} else {								\
		btrfs_warn(fs_info,						\
	"dumping bitmap start=%llu len=%u folio=%llu " #name "_bitmap=%*pbl",	\
		   start, len, folio_pos(folio), __bpf,				\
		   get_bitmap_pointer_##name(fs_info, folio));			\
	}									\
}

/*
 * Make sure not only the page dirty bit is cleared, but also subpage dirty bit
 * is cleared.
 */
void btrfs_folio_assert_not_dirty(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs;
	unsigned int start_bit;
	unsigned int nbits;
	unsigned long flags;

	if (!IS_ENABLED(CONFIG_BTRFS_ASSERT))
		return;

	if (!btrfs_is_subpage(fs_info, folio)) {
		ASSERT(!folio_test_dirty(folio));
		return;
	}

	start_bit = subpage_calc_start_bit(fs_info, folio, dirty, start, len);
	nbits = len >> fs_info->sectorsize_bits;
	bfs = folio_get_private(folio);
	ASSERT(bfs);
	spin_lock_irqsave(&bfs->lock, flags);
	if (unlikely(!bitmap_test_range_all_zero(bfs->bitmaps, start_bit, nbits))) {
		SUBPAGE_DUMP_BITMAP(fs_info, folio, dirty, start, len);
		ASSERT(bitmap_test_range_all_zero(bfs->bitmaps, start_bit, nbits));
	}
	ASSERT(bitmap_test_range_all_zero(bfs->bitmaps, start_bit, nbits));
	spin_unlock_irqrestore(&bfs->lock, flags);
}

/*
 * This is for folio already locked by plain lock_page()/folio_lock(), which
 * doesn't have any subpage awareness.
 *
 * This populates the involved subpage ranges so that subpage helpers can
 * properly unlock them.
 */
void btrfs_folio_set_lock(const struct btrfs_fs_info *fs_info,
			  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs;
	unsigned long flags;
	unsigned int nbits;
	int ret;

	ASSERT(folio_test_locked(folio));
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, folio))
		return;

	bfs = folio_get_private(folio);
	nbits = len >> fs_info->sectorsize_bits;
	spin_lock_irqsave(&bfs->lock, flags);
	ret = atomic_add_return(nbits, &bfs->nr_locked);
	ASSERT(ret <= btrfs_blocks_per_folio(fs_info, folio));
	spin_unlock_irqrestore(&bfs->lock, flags);
}

/*
 * Clear the dirty flag for the folio.
 *
 * If the affected folio is no longer dirty, return true. Otherwise return false.
 */
bool btrfs_meta_folio_clear_and_test_dirty(struct folio *folio, const struct extent_buffer *eb)
{
	bool last;

	if (!btrfs_meta_is_subpage(eb->fs_info)) {
		folio_clear_dirty_for_io(folio);
		return true;
	}

	last = btrfs_subpage_clear_and_test_dirty(eb->fs_info, folio, eb->start, eb->len);
	if (last) {
		folio_clear_dirty_for_io(folio);
		return true;
	}
	return false;
}

void __cold btrfs_subpage_dump_bitmap(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs;
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);
	unsigned long flags;

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(blocks_per_folio > 1);
	bfs = folio_get_private(folio);

	dump_page(folio_page(folio, 0), "btrfs folio state dump");

	if (blocks_per_folio <= BITS_PER_LONG) {
		unsigned long uptodate;
		unsigned long dirty;
		unsigned long writeback;

		spin_lock_irqsave(&bfs->lock, flags);
		uptodate = get_bitmap_value_uptodate(fs_info, folio);
		dirty = get_bitmap_value_dirty(fs_info, folio);
		writeback = get_bitmap_value_writeback(fs_info, folio);

		spin_unlock_irqrestore(&bfs->lock, flags);

		btrfs_warn(fs_info,
"start=%llu len=%u page=%llu, bitmaps uptodate=%*pbl dirty=%*pbl writeback=%*pbl",
			    start, len, folio_pos(folio),
			    blocks_per_folio, &uptodate,
			    blocks_per_folio, &dirty,
			    blocks_per_folio, &writeback);
		return;
	}

	spin_lock_irqsave(&bfs->lock, flags);
	btrfs_warn(fs_info,
"start=%llu len=%u page=%llu, bitmaps uptodate=%*pbl dirty=%*pbl writeback=%*pbl",
		    start, len, folio_pos(folio),
		    blocks_per_folio, get_bitmap_pointer_uptodate(fs_info, folio),
		    blocks_per_folio, get_bitmap_pointer_dirty(fs_info, folio),
		    blocks_per_folio, get_bitmap_pointer_writeback(fs_info, folio));
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_copy_subpage_dirty_bitmap(struct btrfs_fs_info *fs_info,
				     struct folio *folio,
				     unsigned long *dst)
{
	struct btrfs_folio_state *bfs;
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);
	unsigned long flags;
	unsigned long value;

	if (blocks_per_folio == 1) {
		value = 1;
		bitmap_copy(dst, &value, 1);
		return;
	}

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(blocks_per_folio > 1);
	bfs = folio_get_private(folio);

	if (blocks_per_folio <= BITS_PER_LONG) {
		spin_lock_irqsave(&bfs->lock, flags);
		value = bitmap_read(bfs->bitmaps, btrfs_bitmap_nr_dirty * blocks_per_folio,
				    blocks_per_folio);
		spin_unlock_irqrestore(&bfs->lock, flags);
		bitmap_copy(dst, &value, blocks_per_folio);
		return;
	}
	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_copy(dst, get_bitmap_pointer_dirty(fs_info, folio),
		    blocks_per_folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}
