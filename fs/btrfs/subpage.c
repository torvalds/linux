// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include "messages.h"
#include "ctree.h"
#include "subpage.h"
#include "btrfs_inode.h"

/*
 * Subpage (sectorsize < PAGE_SIZE) support overview:
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
 *   Both metadata and data will use a new structure, btrfs_subpage, to
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

bool btrfs_is_subpage(const struct btrfs_fs_info *fs_info, struct address_space *mapping)
{
	if (fs_info->sectorsize >= PAGE_SIZE)
		return false;

	/*
	 * Only data pages (either through DIO or compression) can have no
	 * mapping. And if page->mapping->host is data inode, it's subpage.
	 * As we have ruled our sectorsize >= PAGE_SIZE case already.
	 */
	if (!mapping || !mapping->host || is_data_inode(BTRFS_I(mapping->host)))
		return true;

	/*
	 * Now the only remaining case is metadata, which we only go subpage
	 * routine if nodesize < PAGE_SIZE.
	 */
	if (fs_info->nodesize < PAGE_SIZE)
		return true;
	return false;
}

void btrfs_init_subpage_info(struct btrfs_subpage_info *subpage_info, u32 sectorsize)
{
	unsigned int cur = 0;
	unsigned int nr_bits;

	ASSERT(IS_ALIGNED(PAGE_SIZE, sectorsize));

	nr_bits = PAGE_SIZE / sectorsize;
	subpage_info->bitmap_nr_bits = nr_bits;

	subpage_info->uptodate_offset = cur;
	cur += nr_bits;

	subpage_info->dirty_offset = cur;
	cur += nr_bits;

	subpage_info->writeback_offset = cur;
	cur += nr_bits;

	subpage_info->ordered_offset = cur;
	cur += nr_bits;

	subpage_info->checked_offset = cur;
	cur += nr_bits;

	subpage_info->locked_offset = cur;
	cur += nr_bits;

	subpage_info->total_nr_bits = cur;
}

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct folio *folio, enum btrfs_subpage_type type)
{
	struct btrfs_subpage *subpage;

	/*
	 * We have cases like a dummy extent buffer page, which is not mapped
	 * and doesn't need to be locked.
	 */
	if (folio->mapping)
		ASSERT(folio_test_locked(folio));

	/* Either not subpage, or the folio already has private attached. */
	if (!btrfs_is_subpage(fs_info, folio->mapping) || folio_test_private(folio))
		return 0;

	subpage = btrfs_alloc_subpage(fs_info, type);
	if (IS_ERR(subpage))
		return  PTR_ERR(subpage);

	folio_attach_private(folio, subpage);
	return 0;
}

void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info, struct folio *folio)
{
	struct btrfs_subpage *subpage;

	/* Either not subpage, or the folio already has private attached. */
	if (!btrfs_is_subpage(fs_info, folio->mapping) || !folio_test_private(folio))
		return;

	subpage = folio_detach_private(folio);
	ASSERT(subpage);
	btrfs_free_subpage(subpage);
}

struct btrfs_subpage *btrfs_alloc_subpage(const struct btrfs_fs_info *fs_info,
					  enum btrfs_subpage_type type)
{
	struct btrfs_subpage *ret;
	unsigned int real_size;

	ASSERT(fs_info->sectorsize < PAGE_SIZE);

	real_size = struct_size(ret, bitmaps,
			BITS_TO_LONGS(fs_info->subpage_info->total_nr_bits));
	ret = kzalloc(real_size, GFP_NOFS);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&ret->lock);
	if (type == BTRFS_SUBPAGE_METADATA) {
		atomic_set(&ret->eb_refs, 0);
	} else {
		atomic_set(&ret->readers, 0);
		atomic_set(&ret->writers, 0);
	}
	return ret;
}

void btrfs_free_subpage(struct btrfs_subpage *subpage)
{
	kfree(subpage);
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
	struct btrfs_subpage *subpage;

	if (!btrfs_is_subpage(fs_info, folio->mapping))
		return;

	ASSERT(folio_test_private(folio) && folio->mapping);
	lockdep_assert_held(&folio->mapping->i_private_lock);

	subpage = folio_get_private(folio);
	atomic_inc(&subpage->eb_refs);
}

void btrfs_folio_dec_eb_refs(const struct btrfs_fs_info *fs_info, struct folio *folio)
{
	struct btrfs_subpage *subpage;

	if (!btrfs_is_subpage(fs_info, folio->mapping))
		return;

	ASSERT(folio_test_private(folio) && folio->mapping);
	lockdep_assert_held(&folio->mapping->i_private_lock);

	subpage = folio_get_private(folio);
	ASSERT(atomic_read(&subpage->eb_refs));
	atomic_dec(&subpage->eb_refs);
}

static void btrfs_subpage_assert(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	/* For subpage support, the folio must be single page. */
	ASSERT(folio_order(folio) == 0);

	/* Basic checks */
	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(len, fs_info->sectorsize));
	/*
	 * The range check only works for mapped page, we can still have
	 * unmapped page like dummy extent buffer pages.
	 */
	if (folio->mapping)
		ASSERT(folio_pos(folio) <= start &&
		       start + len <= folio_pos(folio) + PAGE_SIZE);
}

#define subpage_calc_start_bit(fs_info, folio, name, start, len)	\
({									\
	unsigned int __start_bit;						\
									\
	btrfs_subpage_assert(fs_info, folio, start, len);		\
	__start_bit = offset_in_page(start) >> fs_info->sectorsize_bits; \
	__start_bit += fs_info->subpage_info->name##_offset;		\
	__start_bit;							\
})

void btrfs_subpage_start_reader(const struct btrfs_fs_info *fs_info,
				struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	const int start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	const int nbits = len >> fs_info->sectorsize_bits;
	unsigned long flags;


	btrfs_subpage_assert(fs_info, folio, start, len);

	spin_lock_irqsave(&subpage->lock, flags);
	/*
	 * Even though it's just for reading the page, no one should have
	 * locked the subpage range.
	 */
	ASSERT(bitmap_test_range_all_zero(subpage->bitmaps, start_bit, nbits));
	bitmap_set(subpage->bitmaps, start_bit, nbits);
	atomic_add(nbits, &subpage->readers);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_end_reader(const struct btrfs_fs_info *fs_info,
			      struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	const int start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	const int nbits = len >> fs_info->sectorsize_bits;
	unsigned long flags;
	bool is_data;
	bool last;

	btrfs_subpage_assert(fs_info, folio, start, len);
	is_data = is_data_inode(BTRFS_I(folio->mapping->host));

	spin_lock_irqsave(&subpage->lock, flags);

	/* The range should have already been locked. */
	ASSERT(bitmap_test_range_all_set(subpage->bitmaps, start_bit, nbits));
	ASSERT(atomic_read(&subpage->readers) >= nbits);

	bitmap_clear(subpage->bitmaps, start_bit, nbits);
	last = atomic_sub_and_test(nbits, &subpage->readers);

	/*
	 * For data we need to unlock the page if the last read has finished.
	 *
	 * And please don't replace @last with atomic_sub_and_test() call
	 * inside if () condition.
	 * As we want the atomic_sub_and_test() to be always executed.
	 */
	if (is_data && last)
		folio_unlock(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

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
		*len = min_t(u64, folio_pos(folio) + PAGE_SIZE,
			     orig_start + orig_len) - *start;
}

static void btrfs_subpage_start_writer(const struct btrfs_fs_info *fs_info,
				       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	const int start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	const int nbits = (len >> fs_info->sectorsize_bits);
	unsigned long flags;
	int ret;

	btrfs_subpage_assert(fs_info, folio, start, len);

	spin_lock_irqsave(&subpage->lock, flags);
	ASSERT(atomic_read(&subpage->readers) == 0);
	ASSERT(bitmap_test_range_all_zero(subpage->bitmaps, start_bit, nbits));
	bitmap_set(subpage->bitmaps, start_bit, nbits);
	ret = atomic_add_return(nbits, &subpage->writers);
	ASSERT(ret == nbits);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

static bool btrfs_subpage_end_and_test_writer(const struct btrfs_fs_info *fs_info,
					      struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	const int start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	const int nbits = (len >> fs_info->sectorsize_bits);
	unsigned long flags;
	bool last;

	btrfs_subpage_assert(fs_info, folio, start, len);

	spin_lock_irqsave(&subpage->lock, flags);
	/*
	 * We have call sites passing @lock_page into
	 * extent_clear_unlock_delalloc() for compression path.
	 *
	 * This @locked_page is locked by plain lock_page(), thus its
	 * subpage::writers is 0.  Handle them in a special way.
	 */
	if (atomic_read(&subpage->writers) == 0) {
		spin_unlock_irqrestore(&subpage->lock, flags);
		return true;
	}

	ASSERT(atomic_read(&subpage->writers) >= nbits);
	/* The target range should have been locked. */
	ASSERT(bitmap_test_range_all_set(subpage->bitmaps, start_bit, nbits));
	bitmap_clear(subpage->bitmaps, start_bit, nbits);
	last = atomic_sub_and_test(nbits, &subpage->writers);
	spin_unlock_irqrestore(&subpage->lock, flags);
	return last;
}

/*
 * Lock a folio for delalloc page writeback.
 *
 * Return -EAGAIN if the page is not properly initialized.
 * Return 0 with the page locked, and writer counter updated.
 *
 * Even with 0 returned, the page still need extra check to make sure
 * it's really the correct page, as the caller is using
 * filemap_get_folios_contig(), which can race with page invalidating.
 */
int btrfs_folio_start_writer_lock(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len)
{
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, folio->mapping)) {
		folio_lock(folio);
		return 0;
	}
	folio_lock(folio);
	if (!folio_test_private(folio) || !folio_get_private(folio)) {
		folio_unlock(folio);
		return -EAGAIN;
	}
	btrfs_subpage_clamp_range(folio, &start, &len);
	btrfs_subpage_start_writer(fs_info, folio, start, len);
	return 0;
}

void btrfs_folio_end_writer_lock(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, folio->mapping)) {
		folio_unlock(folio);
		return;
	}
	btrfs_subpage_clamp_range(folio, &start, &len);
	if (btrfs_subpage_end_and_test_writer(fs_info, folio, start, len))
		folio_unlock(folio);
}

#define subpage_test_bitmap_all_set(fs_info, subpage, name)		\
	bitmap_test_range_all_set(subpage->bitmaps,			\
			fs_info->subpage_info->name##_offset,		\
			fs_info->subpage_info->bitmap_nr_bits)

#define subpage_test_bitmap_all_zero(fs_info, subpage, name)		\
	bitmap_test_range_all_zero(subpage->bitmaps,			\
			fs_info->subpage_info->name##_offset,		\
			fs_info->subpage_info->bitmap_nr_bits)

void btrfs_subpage_set_uptodate(const struct btrfs_fs_info *fs_info,
				struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							uptodate, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_set(fs_info, subpage, uptodate))
		folio_mark_uptodate(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_uptodate(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							uptodate, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	folio_clear_uptodate(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_dirty(const struct btrfs_fs_info *fs_info,
			     struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	spin_unlock_irqrestore(&subpage->lock, flags);
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
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							dirty, start, len);
	unsigned long flags;
	bool last = false;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, subpage, dirty))
		last = true;
	spin_unlock_irqrestore(&subpage->lock, flags);
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
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							writeback, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (!folio_test_writeback(folio))
		folio_start_writeback(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_writeback(const struct btrfs_fs_info *fs_info,
				   struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							writeback, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, subpage, writeback)) {
		ASSERT(folio_test_writeback(folio));
		folio_end_writeback(folio);
	}
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_ordered(const struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							ordered, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	folio_set_ordered(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_ordered(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							ordered, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, subpage, ordered))
		folio_clear_ordered(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_checked(const struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							checked, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_set(fs_info, subpage, checked))
		folio_set_checked(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_checked(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
							checked, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	folio_clear_checked(folio);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

/*
 * Unlike set/clear which is dependent on each page status, for test all bits
 * are tested in the same way.
 */
#define IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(name)				\
bool btrfs_subpage_test_##name(const struct btrfs_fs_info *fs_info,	\
			       struct folio *folio, u64 start, u32 len)	\
{									\
	struct btrfs_subpage *subpage = folio_get_private(folio);	\
	unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,	\
						name, start, len);	\
	unsigned long flags;						\
	bool ret;							\
									\
	spin_lock_irqsave(&subpage->lock, flags);			\
	ret = bitmap_test_range_all_set(subpage->bitmaps, start_bit,	\
				len >> fs_info->sectorsize_bits);	\
	spin_unlock_irqrestore(&subpage->lock, flags);			\
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
	    !btrfs_is_subpage(fs_info, folio->mapping)) {		\
		folio_set_func(folio);					\
		return;							\
	}								\
	btrfs_subpage_set_##name(fs_info, folio, start, len);		\
}									\
void btrfs_folio_clear_##name(const struct btrfs_fs_info *fs_info,	\
			      struct folio *folio, u64 start, u32 len)	\
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio->mapping)) {		\
		folio_clear_func(folio);				\
		return;							\
	}								\
	btrfs_subpage_clear_##name(fs_info, folio, start, len);		\
}									\
bool btrfs_folio_test_##name(const struct btrfs_fs_info *fs_info,	\
			     struct folio *folio, u64 start, u32 len)	\
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio->mapping))			\
		return folio_test_func(folio);				\
	return btrfs_subpage_test_##name(fs_info, folio, start, len);	\
}									\
void btrfs_folio_clamp_set_##name(const struct btrfs_fs_info *fs_info,	\
				  struct folio *folio, u64 start, u32 len) \
{									\
	if (unlikely(!fs_info) ||					\
	    !btrfs_is_subpage(fs_info, folio->mapping)) {		\
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
	    !btrfs_is_subpage(fs_info, folio->mapping)) {		\
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
	    !btrfs_is_subpage(fs_info, folio->mapping))			\
		return folio_test_func(folio);				\
	btrfs_subpage_clamp_range(folio, &start, &len);			\
	return btrfs_subpage_test_##name(fs_info, folio, start, len);	\
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

/*
 * Make sure not only the page dirty bit is cleared, but also subpage dirty bit
 * is cleared.
 */
void btrfs_folio_assert_not_dirty(const struct btrfs_fs_info *fs_info,
				  struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage;
	unsigned int start_bit;
	unsigned int nbits;
	unsigned long flags;

	if (!IS_ENABLED(CONFIG_BTRFS_ASSERT))
		return;

	if (!btrfs_is_subpage(fs_info, folio->mapping)) {
		ASSERT(!folio_test_dirty(folio));
		return;
	}

	start_bit = subpage_calc_start_bit(fs_info, folio, dirty, start, len);
	nbits = len >> fs_info->sectorsize_bits;
	subpage = folio_get_private(folio);
	ASSERT(subpage);
	spin_lock_irqsave(&subpage->lock, flags);
	ASSERT(bitmap_test_range_all_zero(subpage->bitmaps, start_bit, nbits));
	spin_unlock_irqrestore(&subpage->lock, flags);
}

/*
 * Handle different locked pages with different page sizes:
 *
 * - Page locked by plain lock_page()
 *   It should not have any subpage::writers count.
 *   Can be unlocked by unlock_page().
 *   This is the most common locked page for __extent_writepage() called
 *   inside extent_write_cache_pages().
 *   Rarer cases include the @locked_page from extent_write_locked_range().
 *
 * - Page locked by lock_delalloc_pages()
 *   There is only one caller, all pages except @locked_page for
 *   extent_write_locked_range().
 *   In this case, we have to call subpage helper to handle the case.
 */
void btrfs_folio_unlock_writer(struct btrfs_fs_info *fs_info,
			       struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage;

	ASSERT(folio_test_locked(folio));
	/* For non-subpage case, we just unlock the page */
	if (!btrfs_is_subpage(fs_info, folio->mapping)) {
		folio_unlock(folio);
		return;
	}

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	subpage = folio_get_private(folio);

	/*
	 * For subpage case, there are two types of locked page.  With or
	 * without writers number.
	 *
	 * Since we own the page lock, no one else could touch subpage::writers
	 * and we are safe to do several atomic operations without spinlock.
	 */
	if (atomic_read(&subpage->writers) == 0) {
		/* No writers, locked by plain lock_page() */
		folio_unlock(folio);
		return;
	}

	/* Have writers, use proper subpage helper to end it */
	btrfs_folio_end_writer_lock(fs_info, folio, start, len);
}

/*
 * This is for folio already locked by plain lock_page()/folio_lock(), which
 * doesn't have any subpage awareness.
 *
 * This populates the involved subpage ranges so that subpage helpers can
 * properly unlock them.
 */
void btrfs_folio_set_writer_lock(const struct btrfs_fs_info *fs_info,
				 struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage *subpage;
	unsigned long flags;
	unsigned int start_bit;
	unsigned int nbits;
	int ret;

	ASSERT(folio_test_locked(folio));
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, folio->mapping))
		return;

	subpage = folio_get_private(folio);
	start_bit = subpage_calc_start_bit(fs_info, folio, locked, start, len);
	nbits = len >> fs_info->sectorsize_bits;
	spin_lock_irqsave(&subpage->lock, flags);
	/* Target range should not yet be locked. */
	ASSERT(bitmap_test_range_all_zero(subpage->bitmaps, start_bit, nbits));
	bitmap_set(subpage->bitmaps, start_bit, nbits);
	ret = atomic_add_return(nbits, &subpage->writers);
	ASSERT(ret <= fs_info->subpage_info->bitmap_nr_bits);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

/*
 * Find any subpage writer locked range inside @folio, starting at file offset
 * @search_start. The caller should ensure the folio is locked.
 *
 * Return true and update @found_start_ret and @found_len_ret to the first
 * writer locked range.
 * Return false if there is no writer locked range.
 */
bool btrfs_subpage_find_writer_locked(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 search_start,
				      u64 *found_start_ret, u32 *found_len_ret)
{
	struct btrfs_subpage_info *subpage_info = fs_info->subpage_info;
	struct btrfs_subpage *subpage = folio_get_private(folio);
	const unsigned int len = PAGE_SIZE - offset_in_page(search_start);
	const unsigned int start_bit = subpage_calc_start_bit(fs_info, folio,
						locked, search_start, len);
	const unsigned int locked_bitmap_start = subpage_info->locked_offset;
	const unsigned int locked_bitmap_end = locked_bitmap_start +
					       subpage_info->bitmap_nr_bits;
	unsigned long flags;
	int first_zero;
	int first_set;
	bool found = false;

	ASSERT(folio_test_locked(folio));
	spin_lock_irqsave(&subpage->lock, flags);
	first_set = find_next_bit(subpage->bitmaps, locked_bitmap_end, start_bit);
	if (first_set >= locked_bitmap_end)
		goto out;

	found = true;

	*found_start_ret = folio_pos(folio) +
		((first_set - locked_bitmap_start) << fs_info->sectorsize_bits);
	/*
	 * Since @first_set is ensured to be smaller than locked_bitmap_end
	 * here, @found_start_ret should be inside the folio.
	 */
	ASSERT(*found_start_ret < folio_pos(folio) + PAGE_SIZE);

	first_zero = find_next_zero_bit(subpage->bitmaps, locked_bitmap_end, first_set);
	*found_len_ret = (first_zero - first_set) << fs_info->sectorsize_bits;
out:
	spin_unlock_irqrestore(&subpage->lock, flags);
	return found;
}

/*
 * Unlike btrfs_folio_end_writer_lock() which unlocks a specified subpage range,
 * this ends all writer locked ranges of a page.
 *
 * This is for the locked page of __extent_writepage(), as the locked page
 * can contain several locked subpage ranges.
 */
void btrfs_folio_end_all_writers(const struct btrfs_fs_info *fs_info, struct folio *folio)
{
	struct btrfs_subpage *subpage = folio_get_private(folio);
	u64 folio_start = folio_pos(folio);
	u64 cur = folio_start;

	ASSERT(folio_test_locked(folio));
	if (!btrfs_is_subpage(fs_info, folio->mapping)) {
		folio_unlock(folio);
		return;
	}

	/* The page has no new delalloc range locked on it. Just plain unlock. */
	if (atomic_read(&subpage->writers) == 0) {
		folio_unlock(folio);
		return;
	}
	while (cur < folio_start + PAGE_SIZE) {
		u64 found_start;
		u32 found_len;
		bool found;
		bool last;

		found = btrfs_subpage_find_writer_locked(fs_info, folio, cur,
							 &found_start, &found_len);
		if (!found)
			break;
		last = btrfs_subpage_end_and_test_writer(fs_info, folio,
							 found_start, found_len);
		if (last) {
			folio_unlock(folio);
			break;
		}
		cur = found_start + found_len;
	}
}

#define GET_SUBPAGE_BITMAP(subpage, subpage_info, name, dst)		\
	bitmap_cut(dst, subpage->bitmaps, 0,				\
		   subpage_info->name##_offset, subpage_info->bitmap_nr_bits)

void __cold btrfs_subpage_dump_bitmap(const struct btrfs_fs_info *fs_info,
				      struct folio *folio, u64 start, u32 len)
{
	struct btrfs_subpage_info *subpage_info = fs_info->subpage_info;
	struct btrfs_subpage *subpage;
	unsigned long uptodate_bitmap;
	unsigned long dirty_bitmap;
	unsigned long writeback_bitmap;
	unsigned long ordered_bitmap;
	unsigned long checked_bitmap;
	unsigned long flags;

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	ASSERT(subpage_info);
	subpage = folio_get_private(folio);

	spin_lock_irqsave(&subpage->lock, flags);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, uptodate, &uptodate_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, dirty, &dirty_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, writeback, &writeback_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, ordered, &ordered_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, checked, &checked_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, locked, &checked_bitmap);
	spin_unlock_irqrestore(&subpage->lock, flags);

	dump_page(folio_page(folio, 0), "btrfs subpage dump");
	btrfs_warn(fs_info,
"start=%llu len=%u page=%llu, bitmaps uptodate=%*pbl dirty=%*pbl writeback=%*pbl ordered=%*pbl checked=%*pbl",
		    start, len, folio_pos(folio),
		    subpage_info->bitmap_nr_bits, &uptodate_bitmap,
		    subpage_info->bitmap_nr_bits, &dirty_bitmap,
		    subpage_info->bitmap_nr_bits, &writeback_bitmap,
		    subpage_info->bitmap_nr_bits, &ordered_bitmap,
		    subpage_info->bitmap_nr_bits, &checked_bitmap);
}
