// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include "ctree.h"
#include "subpage.h"

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct page *page, enum btrfs_subpage_type type)
{
	struct btrfs_subpage *subpage;

	/*
	 * We have cases like a dummy extent buffer page, which is not mappped
	 * and doesn't need to be locked.
	 */
	if (page->mapping)
		ASSERT(PageLocked(page));
	/* Either not subpage, or the page already has private attached */
	if (fs_info->sectorsize == PAGE_SIZE || PagePrivate(page))
		return 0;

	subpage = kzalloc(sizeof(struct btrfs_subpage), GFP_NOFS);
	if (!subpage)
		return -ENOMEM;

	spin_lock_init(&subpage->lock);
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
	kfree(subpage);
}
