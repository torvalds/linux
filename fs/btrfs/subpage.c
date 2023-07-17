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

bool btrfs_is_subpage(const struct btrfs_fs_info *fs_info, struct page *page)
{
	if (fs_info->sectorsize >= PAGE_SIZE)
		return false;

	/*
	 * Only data pages (either through DIO or compression) can have no
	 * mapping. And if page->mapping->host is data inode, it's subpage.
	 * As we have ruled our sectorsize >= PAGE_SIZE case already.
	 */
	if (!page->mapping || !page->mapping->host ||
	    is_data_inode(page->mapping->host))
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

	subpage_info->total_nr_bits = cur;
}

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct page *page, enum btrfs_subpage_type type)
{
	struct btrfs_subpage *subpage;

	/*
	 * We have cases like a dummy extent buffer page, which is not mapped
	 * and doesn't need to be locked.
	 */
	if (page->mapping)
		ASSERT(PageLocked(page));

	/* Either not subpage, or the page already has private attached */
	if (!btrfs_is_subpage(fs_info, page) || PagePrivate(page))
		return 0;

	subpage = btrfs_alloc_subpage(fs_info, type);
	if (IS_ERR(subpage))
		return  PTR_ERR(subpage);

	attach_page_private(page, subpage);
	return 0;
}

void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info,
			  struct page *page)
{
	struct btrfs_subpage *subpage;

	/* Either not subpage, or already detached */
	if (!btrfs_is_subpage(fs_info, page) || !PagePrivate(page))
		return;

	subpage = detach_page_private(page);
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
 * detach_extent_buffer_page() won't detach the page private while we're still
 * allocating the extent buffer.
 */
void btrfs_page_inc_eb_refs(const struct btrfs_fs_info *fs_info,
			    struct page *page)
{
	struct btrfs_subpage *subpage;

	if (!btrfs_is_subpage(fs_info, page))
		return;

	ASSERT(PagePrivate(page) && page->mapping);
	lockdep_assert_held(&page->mapping->private_lock);

	subpage = (struct btrfs_subpage *)page->private;
	atomic_inc(&subpage->eb_refs);
}

void btrfs_page_dec_eb_refs(const struct btrfs_fs_info *fs_info,
			    struct page *page)
{
	struct btrfs_subpage *subpage;

	if (!btrfs_is_subpage(fs_info, page))
		return;

	ASSERT(PagePrivate(page) && page->mapping);
	lockdep_assert_held(&page->mapping->private_lock);

	subpage = (struct btrfs_subpage *)page->private;
	ASSERT(atomic_read(&subpage->eb_refs));
	atomic_dec(&subpage->eb_refs);
}

static void btrfs_subpage_assert(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	/* Basic checks */
	ASSERT(PagePrivate(page) && page->private);
	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(len, fs_info->sectorsize));
	/*
	 * The range check only works for mapped page, we can still have
	 * unmapped page like dummy extent buffer pages.
	 */
	if (page->mapping)
		ASSERT(page_offset(page) <= start &&
		       start + len <= page_offset(page) + PAGE_SIZE);
}

void btrfs_subpage_start_reader(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const int nbits = len >> fs_info->sectorsize_bits;

	btrfs_subpage_assert(fs_info, page, start, len);

	atomic_add(nbits, &subpage->readers);
}

void btrfs_subpage_end_reader(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const int nbits = len >> fs_info->sectorsize_bits;
	bool is_data;
	bool last;

	btrfs_subpage_assert(fs_info, page, start, len);
	is_data = is_data_inode(page->mapping->host);
	ASSERT(atomic_read(&subpage->readers) >= nbits);
	last = atomic_sub_and_test(nbits, &subpage->readers);

	/*
	 * For data we need to unlock the page if the last read has finished.
	 *
	 * And please don't replace @last with atomic_sub_and_test() call
	 * inside if () condition.
	 * As we want the atomic_sub_and_test() to be always executed.
	 */
	if (is_data && last)
		unlock_page(page);
}

static void btrfs_subpage_clamp_range(struct page *page, u64 *start, u32 *len)
{
	u64 orig_start = *start;
	u32 orig_len = *len;

	*start = max_t(u64, page_offset(page), orig_start);
	/*
	 * For certain call sites like btrfs_drop_pages(), we may have pages
	 * beyond the target range. In that case, just set @len to 0, subpage
	 * helpers can handle @len == 0 without any problem.
	 */
	if (page_offset(page) >= orig_start + orig_len)
		*len = 0;
	else
		*len = min_t(u64, page_offset(page) + PAGE_SIZE,
			     orig_start + orig_len) - *start;
}

void btrfs_subpage_start_writer(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const int nbits = (len >> fs_info->sectorsize_bits);
	int ret;

	btrfs_subpage_assert(fs_info, page, start, len);

	ASSERT(atomic_read(&subpage->readers) == 0);
	ret = atomic_add_return(nbits, &subpage->writers);
	ASSERT(ret == nbits);
}

bool btrfs_subpage_end_and_test_writer(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const int nbits = (len >> fs_info->sectorsize_bits);

	btrfs_subpage_assert(fs_info, page, start, len);

	/*
	 * We have call sites passing @lock_page into
	 * extent_clear_unlock_delalloc() for compression path.
	 *
	 * This @locked_page is locked by plain lock_page(), thus its
	 * subpage::writers is 0.  Handle them in a special way.
	 */
	if (atomic_read(&subpage->writers) == 0)
		return true;

	ASSERT(atomic_read(&subpage->writers) >= nbits);
	return atomic_sub_and_test(nbits, &subpage->writers);
}

/*
 * Lock a page for delalloc page writeback.
 *
 * Return -EAGAIN if the page is not properly initialized.
 * Return 0 with the page locked, and writer counter updated.
 *
 * Even with 0 returned, the page still need extra check to make sure
 * it's really the correct page, as the caller is using
 * filemap_get_folios_contig(), which can race with page invalidating.
 */
int btrfs_page_start_writer_lock(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page)) {
		lock_page(page);
		return 0;
	}
	lock_page(page);
	if (!PagePrivate(page) || !page->private) {
		unlock_page(page);
		return -EAGAIN;
	}
	btrfs_subpage_clamp_range(page, &start, &len);
	btrfs_subpage_start_writer(fs_info, page, start, len);
	return 0;
}

void btrfs_page_end_writer_lock(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page))
		return unlock_page(page);
	btrfs_subpage_clamp_range(page, &start, &len);
	if (btrfs_subpage_end_and_test_writer(fs_info, page, start, len))
		unlock_page(page);
}

#define subpage_calc_start_bit(fs_info, page, name, start, len)		\
({									\
	unsigned int start_bit;						\
									\
	btrfs_subpage_assert(fs_info, page, start, len);		\
	start_bit = offset_in_page(start) >> fs_info->sectorsize_bits;	\
	start_bit += fs_info->subpage_info->name##_offset;		\
	start_bit;							\
})

#define subpage_test_bitmap_all_set(fs_info, subpage, name)		\
	bitmap_test_range_all_set(subpage->bitmaps,			\
			fs_info->subpage_info->name##_offset,		\
			fs_info->subpage_info->bitmap_nr_bits)

#define subpage_test_bitmap_all_zero(fs_info, subpage, name)		\
	bitmap_test_range_all_zero(subpage->bitmaps,			\
			fs_info->subpage_info->name##_offset,		\
			fs_info->subpage_info->bitmap_nr_bits)

void btrfs_subpage_set_uptodate(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							uptodate, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_set(fs_info, subpage, uptodate))
		SetPageUptodate(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_uptodate(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							uptodate, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	ClearPageUptodate(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_dirty(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							dirty, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	spin_unlock_irqrestore(&subpage->lock, flags);
	set_page_dirty(page);
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
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
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
		struct page *page, u64 start, u32 len)
{
	bool last;

	last = btrfs_subpage_clear_and_test_dirty(fs_info, page, start, len);
	if (last)
		clear_page_dirty_for_io(page);
}

void btrfs_subpage_set_writeback(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							writeback, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	set_page_writeback(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_writeback(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							writeback, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, subpage, writeback)) {
		ASSERT(PageWriteback(page));
		end_page_writeback(page);
	}
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_ordered(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							ordered, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	SetPageOrdered(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_ordered(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							ordered, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_zero(fs_info, subpage, ordered))
		ClearPageOrdered(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_checked(const struct btrfs_fs_info *fs_info,
			       struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							checked, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_set(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	if (subpage_test_bitmap_all_set(fs_info, subpage, checked))
		SetPageChecked(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_checked(const struct btrfs_fs_info *fs_info,
				 struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,
							checked, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	bitmap_clear(subpage->bitmaps, start_bit, len >> fs_info->sectorsize_bits);
	ClearPageChecked(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

/*
 * Unlike set/clear which is dependent on each page status, for test all bits
 * are tested in the same way.
 */
#define IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(name)				\
bool btrfs_subpage_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private; \
	unsigned int start_bit = subpage_calc_start_bit(fs_info, page,	\
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
#define IMPLEMENT_BTRFS_PAGE_OPS(name, set_page_func, clear_page_func,	\
			       test_page_func)				\
void btrfs_page_set_##name(const struct btrfs_fs_info *fs_info,		\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page)) {	\
		set_page_func(page);					\
		return;							\
	}								\
	btrfs_subpage_set_##name(fs_info, page, start, len);		\
}									\
void btrfs_page_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page)) {	\
		clear_page_func(page);					\
		return;							\
	}								\
	btrfs_subpage_clear_##name(fs_info, page, start, len);		\
}									\
bool btrfs_page_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page))	\
		return test_page_func(page);				\
	return btrfs_subpage_test_##name(fs_info, page, start, len);	\
}									\
void btrfs_page_clamp_set_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page)) {	\
		set_page_func(page);					\
		return;							\
	}								\
	btrfs_subpage_clamp_range(page, &start, &len);			\
	btrfs_subpage_set_##name(fs_info, page, start, len);		\
}									\
void btrfs_page_clamp_clear_##name(const struct btrfs_fs_info *fs_info, \
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page)) {	\
		clear_page_func(page);					\
		return;							\
	}								\
	btrfs_subpage_clamp_range(page, &start, &len);			\
	btrfs_subpage_clear_##name(fs_info, page, start, len);		\
}									\
bool btrfs_page_clamp_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || !btrfs_is_subpage(fs_info, page))	\
		return test_page_func(page);				\
	btrfs_subpage_clamp_range(page, &start, &len);			\
	return btrfs_subpage_test_##name(fs_info, page, start, len);	\
}
IMPLEMENT_BTRFS_PAGE_OPS(uptodate, SetPageUptodate, ClearPageUptodate,
			 PageUptodate);
IMPLEMENT_BTRFS_PAGE_OPS(dirty, set_page_dirty, clear_page_dirty_for_io,
			 PageDirty);
IMPLEMENT_BTRFS_PAGE_OPS(writeback, set_page_writeback, end_page_writeback,
			 PageWriteback);
IMPLEMENT_BTRFS_PAGE_OPS(ordered, SetPageOrdered, ClearPageOrdered,
			 PageOrdered);
IMPLEMENT_BTRFS_PAGE_OPS(checked, SetPageChecked, ClearPageChecked, PageChecked);

/*
 * Make sure not only the page dirty bit is cleared, but also subpage dirty bit
 * is cleared.
 */
void btrfs_page_assert_not_dirty(const struct btrfs_fs_info *fs_info,
				 struct page *page)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;

	if (!IS_ENABLED(CONFIG_BTRFS_ASSERT))
		return;

	ASSERT(!PageDirty(page));
	if (!btrfs_is_subpage(fs_info, page))
		return;

	ASSERT(PagePrivate(page) && page->private);
	ASSERT(subpage_test_bitmap_all_zero(fs_info, subpage, dirty));
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
void btrfs_page_unlock_writer(struct btrfs_fs_info *fs_info, struct page *page,
			      u64 start, u32 len)
{
	struct btrfs_subpage *subpage;

	ASSERT(PageLocked(page));
	/* For non-subpage case, we just unlock the page */
	if (!btrfs_is_subpage(fs_info, page))
		return unlock_page(page);

	ASSERT(PagePrivate(page) && page->private);
	subpage = (struct btrfs_subpage *)page->private;

	/*
	 * For subpage case, there are two types of locked page.  With or
	 * without writers number.
	 *
	 * Since we own the page lock, no one else could touch subpage::writers
	 * and we are safe to do several atomic operations without spinlock.
	 */
	if (atomic_read(&subpage->writers) == 0)
		/* No writers, locked by plain lock_page() */
		return unlock_page(page);

	/* Have writers, use proper subpage helper to end it */
	btrfs_page_end_writer_lock(fs_info, page, start, len);
}

#define GET_SUBPAGE_BITMAP(subpage, subpage_info, name, dst)		\
	bitmap_cut(dst, subpage->bitmaps, 0,				\
		   subpage_info->name##_offset, subpage_info->bitmap_nr_bits)

void __cold btrfs_subpage_dump_bitmap(const struct btrfs_fs_info *fs_info,
				      struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage_info *subpage_info = fs_info->subpage_info;
	struct btrfs_subpage *subpage;
	unsigned long uptodate_bitmap;
	unsigned long error_bitmap;
	unsigned long dirty_bitmap;
	unsigned long writeback_bitmap;
	unsigned long ordered_bitmap;
	unsigned long checked_bitmap;
	unsigned long flags;

	ASSERT(PagePrivate(page) && page->private);
	ASSERT(subpage_info);
	subpage = (struct btrfs_subpage *)page->private;

	spin_lock_irqsave(&subpage->lock, flags);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, uptodate, &uptodate_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, dirty, &dirty_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, writeback, &writeback_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, ordered, &ordered_bitmap);
	GET_SUBPAGE_BITMAP(subpage, subpage_info, checked, &checked_bitmap);
	spin_unlock_irqrestore(&subpage->lock, flags);

	dump_page(page, "btrfs subpage dump");
	btrfs_warn(fs_info,
"start=%llu len=%u page=%llu, bitmaps uptodate=%*pbl error=%*pbl dirty=%*pbl writeback=%*pbl ordered=%*pbl checked=%*pbl",
		    start, len, page_offset(page),
		    subpage_info->bitmap_nr_bits, &uptodate_bitmap,
		    subpage_info->bitmap_nr_bits, &error_bitmap,
		    subpage_info->bitmap_nr_bits, &dirty_bitmap,
		    subpage_info->bitmap_nr_bits, &writeback_bitmap,
		    subpage_info->bitmap_nr_bits, &ordered_bitmap,
		    subpage_info->bitmap_nr_bits, &checked_bitmap);
}
