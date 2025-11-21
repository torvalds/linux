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
 * - Only support 64K page size for now
 *   This is to make metadata handling easier, as 64K page would ensure
 *   all nodesize would fit inside one page, thus we don't need to handle
 *   cases where a tree block crosses several pages.
 *
 * - Only metadata read-write for now
 *   The data read-write part is in development.
 *
 * - Metadata can't cross 64K page boundary
 *   btrfs-progs and kernel have done that for a while, thus only ancient
 *   filesystems could have such problem.  For such case, do a graceful
 *   rejection.
 *
 * Special behavior:
 *
 * - Metadata
 *   Metadata read is fully supported.
 *   Meaning when reading one tree block will only trigger the read for the
 *   needed range, other unrelated range in the same page will not be touched.
 *
 *   Metadata write support is partial.
 *   The writeback is still for the full page, but we will only submit
 *   the dirty extent buffers in the page.
 *
 *   This means, if we have a metadata page like this:
 *
 *   Page offset
 *   0         16K         32K         48K        64K
 *   |/////////|           |///////////|
 *        \- Tree block A        \- Tree block B
 *
 *   Even if we just want to writeback tree block A, we will also writeback
 *   tree block B if it's also dirty.
 *
 *   This may cause extra metadata writeback which results more COW.
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
	       IS_ALIGNED(len, fs_info->sectorsize));
	/*
	 * The range check only works for mapped page, we can still have
	 * unmapped page like dummy extent buffer pages.
	 */
	if (folio->mapping)
		ASSERT(folio_pos(folio) <= start && start + len <= folio_end(folio),
		       "start=%llu len=%u folio_pos=%llu folio_size=%zu",
		       start, len, folio_pos(folio), folio_size(folio));
}

#define subpage_calc_start_bit(fs_info, folio, name, start, len)	\
({									\
	unsigned int __start_bit;					\
	const unsigned int blocks_per_folio =				\
			   btrfs_blocks_per_folio(fs_info, folio);	\
									\
	btrfs_subpage_assert(fs_info, folio, start, len);		\
	__start_bit = offset_in_folio(folio, start) >> fs_info->sectorsize_bits; \
	__start_bit += blocks_per_folio * btrfs_bitmap_nr_##name;	\
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
		*len = min_t(u64, folio_end(folio), orig_start + orig_len) - *start;
}

static bool btrfs_subpage_end_and_test_lock(const struct btrfs_fs_info *fs_info,
					    struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	const int start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	const int nbits = (len >> fs_info->sectorsize_bits);
	unsigned long flags;
	unsigned int cleared = 0;
	int bit = start_bit;
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

	for_each_set_bit_from(bit, bfs->bitmaps, start_bit + nbits) {
		clear_bit(bit, bfs->bitmaps);
		cleared++;
	}
	ASSERT(atomic_read(&bfs->nr_locked) >= cleared);
	last = atomic_sub_and_test(cleared, &bfs->nr_locked);
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
				 struct folio *folio, unsigned long bitmap)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	const unsigned int blocks_per_folio = btrfs_blocks_per_folio(fs_info, folio);
	const int start_bit = blocks_per_folio * btrfs_bitmap_nr_locked;
	unsigned long flags;
	bool last = false;
	int cleared = 0;
	int bit;

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
	for_each_set_bit(bit, &bitmap, blocks_per_folio) {
		if (test_and_clear_bit(bit + start_bit, bfs->bitmaps))
			cleared++;
	}
	ASSERT(atomic_read(&bfs->nr_locked) >= cleared);
	last = atomic_sub_and_test(cleared, &bfs->nr_locked);
	spin_unlock_irqrestore(&bfs->lock, flags);
	if (last)
		folio_unlock(folio);
}

#define subpage_test_bitmap_all_set(fs_info, folio, name)		\
({									\
	struct btrfs_folio_state *bfs = folio_get_private(folio);	\
	const unsigned int blocks_per_folio =				\
				btrfs_blocks_per_folio(fs_info, folio); \
									\
	bitmap_test_range_all_set(bfs->bitmaps,				\
			blocks_per_folio * btrfs_bitmap_nr_##name,	\
			blocks_per_folio);				\
})

#define subpage_test_bitmap_all_zero(fs_info, folio, name)		\
({									\
	struct btrfs_folio_state *bfs = folio_get_private(folio);	\
	const unsigned int blocks_per_folio =				\
				btrfs_blocks_per_folio(fs_info, folio); \
									\
	bitmap_test_range_all_zero(bfs->bitmaps,			\
			blocks_per_folio * btrfs_bitmap_nr_##name,	\
			blocks_per_folio);				\
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

void btrfs_subpage_set_dirty(const struct btrfs_fs_info *fs_info,
			     struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_set(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	spin_unlock_irqrestore(&bfs->lock, flags);
	folio_mark_dirty(folio);
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
	 */
	if (!folio_test_writeback(folio))
		__folio_start_writeback(folio, true);
	if (!folio_test_dirty(folio)) {
		struct address_space *mapping = folio_mapping(folio);
		XA_STATE(xas, &mapping->i_pages, folio->index);
		unsigned long flags;

		xas_lock_irqsave(&xas, flags);
		xas_load(&xas);
		xas_clear_mark(&xas, PAGECACHE_TAG_TOWRITE);
		xas_unlock_irqrestore(&xas, flags);
	}
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

void btrfs_subpage_set_ordered(const struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							ordered, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_set(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	folio_set_ordered(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_subpage_clear_ordered(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							ordered, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_clear(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, folio, ordered))
		folio_clear_ordered(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_subpage_set_checked(const struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							checked, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_set(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_set(fs_info, folio, checked))
		folio_set_checked(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
}

void btrfs_subpage_clear_checked(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	struct btrfs_folio_state *bfs = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							checked, start, len);
	unsigned long flags;

	spin_lock_irqsave(&bfs->lock, flags);
	bitmap_clear(bfs->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	folio_clear_checked(folio);
	spin_unlock_irqrestore(&bfs->lock, flags);
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
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(ordered);
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(checked);

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
IMPLEMENT_BTRFS_PAGE_OPS(dirty, folio_mark_dirty, folio_clear_dirty_for_io,
			 folio_test_dirty);
IMPLEMENT_BTRFS_PAGE_OPS(writeback, folio_start_writeback, folio_end_writeback,
			 folio_test_writeback);
IMPLEMENT_BTRFS_PAGE_OPS(ordered, folio_set_ordered, folio_clear_ordered,
			 folio_test_ordered);
IMPLEMENT_BTRFS_PAGE_OPS(checked, folio_set_checked, folio_clear_checked,
			 folio_test_checked);

#define GET_SUBPAGE_BITMAP(fs_info, folio, name, dst)			\
{									\
	const unsigned int blocks_per_folio =				\
				btrfs_blocks_per_folio(fs_info, folio);	\
	const struct btrfs_folio_state *bfs = folio_get_private(folio);	\
									\
	ASSERT(blocks_per_folio <= BITS_PER_LONG);			\
	*dst = bitmap_read(bfs->bitmaps,				\
			   blocks_per_folio * btrfs_bitmap_nr_##name,	\
			   blocks_per_folio);				\
}

#define SUBPAGE_DUMP_BITMAP(fs_info, folio, name, start, len)		\
{									\
	unsigned long bitmap;						\
	const unsigned int blocks_per_folio =				\
				btrfs_blocks_per_folio(fs_info, folio);	\
									\
	GET_SUBPAGE_BITMAP(fs_info, folio, name, &bitmap);		\
	btrfs_warn(fs_info,						\
	"dumping bitmap start=%llu len=%u folio=%llu " #name "_bitmap=%*pbl", \
		   start, len, folio_pos(folio),			\
		   blocks_per_folio, &bitmap);				\
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
	unsigned int start_bit;
	unsigned int nbits;
	int ret;

	ASSERT(folio_test_locked(folio));
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, folio))
		return;

	bfs = folio_get_private(folio);
	start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	nbits = len >> fs_info->sectorsize_bits;
	spin_lock_irqsave(&bfs->lock, flags);
	/* Target range should not yet be locked. */
	if (unlikely(!bitmap_test_range_all_zero(bfs->bitmaps, start_bit, nbits))) {
		SUBPAGE_DUMP_BITMAP(fs_info, folio, locked, start, len);
		ASSERT(bitmap_test_range_all_zero(bfs->bitmaps, start_bit, nbits));
	}
	bitmap_set(bfs->bitmaps, start_bit, nbits);
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
	unsigned long uptodate_bitmap;
	unsigned long dirty_bitmap;
	unsigned long writeback_bitmap;
	unsigned long ordered_bitmap;
	unsigned long checked_bitmap;
	unsigned long locked_bitmap;
	unsigned long flags;

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(blocks_per_folio > 1);
	bfs = folio_get_private(folio);

	spin_lock_irqsave(&bfs->lock, flags);
	GET_SUBPAGE_BITMAP(fs_info, folio, uptodate, &uptodate_bitmap);
	GET_SUBPAGE_BITMAP(fs_info, folio, dirty, &dirty_bitmap);
	GET_SUBPAGE_BITMAP(fs_info, folio, writeback, &writeback_bitmap);
	GET_SUBPAGE_BITMAP(fs_info, folio, ordered, &ordered_bitmap);
	GET_SUBPAGE_BITMAP(fs_info, folio, checked, &checked_bitmap);
	GET_SUBPAGE_BITMAP(fs_info, folio, locked, &locked_bitmap);
	spin_unlock_irqrestore(&bfs->lock, flags);

	dump_page(folio_page(folio, 0), "btrfs folio state dump");
	btrfs_warn(fs_info,
"start=%llu len=%u page=%llu, bitmaps uptodate=%*pbl dirty=%*pbl locked=%*pbl writeback=%*pbl ordered=%*pbl checked=%*pbl",
		    start, len, folio_pos(folio),
		    blocks_per_folio, &uptodate_bitmap,
		    blocks_per_folio, &dirty_bitmap,
		    blocks_per_folio, &locked_bitmap,
		    blocks_per_folio, &writeback_bitmap,
		    blocks_per_folio, &ordered_bitmap,
		    blocks_per_folio, &checked_bitmap);
}

void btrfs_get_subpage_dirty_bitmap(struct btrfs_fs_info *fs_info,
				    struct folio *folio,
				    unsigned long *ret_bitmap)
{
	struct btrfs_folio_state *bfs;
	unsigned long flags;

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(btrfs_blocks_per_folio(fs_info, folio) > 1);
	bfs = folio_get_private(folio);

	spin_lock_irqsave(&bfs->lock, flags);
	GET_SUBPAGE_BITMAP(fs_info, folio, dirty, ret_bitmap);
	spin_unlock_irqrestore(&bfs->lock, flags);
}
