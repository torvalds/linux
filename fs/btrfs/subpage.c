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
