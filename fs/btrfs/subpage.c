// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include "ctree.h"
#include "subpage.h"

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct page *page, enum btrfs_subpage_type type)
{
	struct btrfs_subpage *subpage = NULL;
	int ret;

	/*
	 * We have cases like a dummy extent buffer page, which is not mappped
	 * and doesn't need to be locked.
	 */
	if (page->mapping)
		ASSERT(PageLocked(page));
	/* Either not subpage, or the page already has private attached */
	if (fs_info->sectorsize == PAGE_SIZE || PagePrivate(page))
		return 0;

	ret = btrfs_alloc_subpage(fs_info, &subpage, type);
	if (ret < 0)
		return ret;
	attach_page_private(page, subpage);
	return 0;
}

void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info,
			  struct page *page)
{
	struct btrfs_subpage *subpage;

	/* Either not subpage, or already detached */
	if (fs_info->sectorsize == PAGE_SIZE || !PagePrivate(page))
		return;

	subpage = (struct btrfs_subpage *)detach_page_private(page);
	ASSERT(subpage);
	btrfs_free_subpage(subpage);
}

int btrfs_alloc_subpage(const struct btrfs_fs_info *fs_info,
			struct btrfs_subpage **ret,
			enum btrfs_subpage_type type)
{
	if (fs_info->sectorsize == PAGE_SIZE)
		return 0;

	*ret = kzalloc(sizeof(struct btrfs_subpage), GFP_NOFS);
	if (!*ret)
		return -ENOMEM;
	spin_lock_init(&(*ret)->lock);
	if (type == BTRFS_SUBPAGE_METADATA)
		atomic_set(&(*ret)->eb_refs, 0);
	else
		atomic_set(&(*ret)->readers, 0);
	return 0;
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

	if (fs_info->sectorsize == PAGE_SIZE)
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

	if (fs_info->sectorsize == PAGE_SIZE)
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
	int ret;

	btrfs_subpage_assert(fs_info, page, start, len);

	ret = atomic_add_return(nbits, &subpage->readers);
	ASSERT(ret == nbits);
}

void btrfs_subpage_end_reader(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const int nbits = len >> fs_info->sectorsize_bits;

	btrfs_subpage_assert(fs_info, page, start, len);
	ASSERT(atomic_read(&subpage->readers) >= nbits);
	if (atomic_sub_and_test(nbits, &subpage->readers))
		unlock_page(page);
}

/*
 * Convert the [start, start + len) range into a u16 bitmap
 *
 * For example: if start == page_offset() + 16K, len = 16K, we get 0x00f0.
 */
static u16 btrfs_subpage_calc_bitmap(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	const int bit_start = offset_in_page(start) >> fs_info->sectorsize_bits;
	const int nbits = len >> fs_info->sectorsize_bits;

	btrfs_subpage_assert(fs_info, page, start, len);

	/*
	 * Here nbits can be 16, thus can go beyond u16 range. We make the
	 * first left shift to be calculate in unsigned long (at least u32),
	 * then truncate the result to u16.
	 */
	return (u16)(((1UL << nbits) - 1) << bit_start);
}

void btrfs_subpage_set_uptodate(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const u16 tmp = btrfs_subpage_calc_bitmap(fs_info, page, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	subpage->uptodate_bitmap |= tmp;
	if (subpage->uptodate_bitmap == U16_MAX)
		SetPageUptodate(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_uptodate(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const u16 tmp = btrfs_subpage_calc_bitmap(fs_info, page, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	subpage->uptodate_bitmap &= ~tmp;
	ClearPageUptodate(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_set_error(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const u16 tmp = btrfs_subpage_calc_bitmap(fs_info, page, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	subpage->error_bitmap |= tmp;
	SetPageError(page);
	spin_unlock_irqrestore(&subpage->lock, flags);
}

void btrfs_subpage_clear_error(const struct btrfs_fs_info *fs_info,
		struct page *page, u64 start, u32 len)
{
	struct btrfs_subpage *subpage = (struct btrfs_subpage *)page->private;
	const u16 tmp = btrfs_subpage_calc_bitmap(fs_info, page, start, len);
	unsigned long flags;

	spin_lock_irqsave(&subpage->lock, flags);
	subpage->error_bitmap &= ~tmp;
	if (subpage->error_bitmap == 0)
		ClearPageError(page);
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
	const u16 tmp = btrfs_subpage_calc_bitmap(fs_info, page, start, len); \
	unsigned long flags;						\
	bool ret;							\
									\
	spin_lock_irqsave(&subpage->lock, flags);			\
	ret = ((subpage->name##_bitmap & tmp) == tmp);			\
	spin_unlock_irqrestore(&subpage->lock, flags);			\
	return ret;							\
}
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(uptodate);
IMPLEMENT_BTRFS_SUBPAGE_TEST_OP(error);

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
	if (unlikely(!fs_info) || fs_info->sectorsize == PAGE_SIZE) {	\
		set_page_func(page);					\
		return;							\
	}								\
	btrfs_subpage_set_##name(fs_info, page, start, len);		\
}									\
void btrfs_page_clear_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || fs_info->sectorsize == PAGE_SIZE) {	\
		clear_page_func(page);					\
		return;							\
	}								\
	btrfs_subpage_clear_##name(fs_info, page, start, len);		\
}									\
bool btrfs_page_test_##name(const struct btrfs_fs_info *fs_info,	\
		struct page *page, u64 start, u32 len)			\
{									\
	if (unlikely(!fs_info) || fs_info->sectorsize == PAGE_SIZE)	\
		return test_page_func(page);				\
	return btrfs_subpage_test_##name(fs_info, page, start, len);	\
}
IMPLEMENT_BTRFS_PAGE_OPS(uptodate, SetPageUptodate, ClearPageUptodate,
			 PageUptodate);
IMPLEMENT_BTRFS_PAGE_OPS(error, SetPageError, ClearPageError, PageError);
