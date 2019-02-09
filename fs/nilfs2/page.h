/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * page.h - buffer/page management specific to NILFS
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Ryusuke Konishi and Seiji Kihara.
 */

#ifndef _NILFS_PAGE_H
#define _NILFS_PAGE_H

#include <linux/buffer_head.h>
#include "nilfs.h"

/*
 * Extended buffer state bits
 */
enum {
	BH_NILFS_Allocated = BH_PrivateStart,
	BH_NILFS_Node,
	BH_NILFS_Volatile,
	BH_NILFS_Checked,
	BH_NILFS_Redirected,
};

BUFFER_FNS(NILFS_Node, nilfs_node)		/* nilfs node buffers */
BUFFER_FNS(NILFS_Volatile, nilfs_volatile)
BUFFER_FNS(NILFS_Checked, nilfs_checked)	/* buffer is verified */
BUFFER_FNS(NILFS_Redirected, nilfs_redirected)	/* redirected to a copy */


int __nilfs_clear_page_dirty(struct page *);

struct buffer_head *nilfs_grab_buffer(struct inode *, struct address_space *,
				      unsigned long, unsigned long);
void nilfs_forget_buffer(struct buffer_head *);
void nilfs_copy_buffer(struct buffer_head *, struct buffer_head *);
int nilfs_page_buffers_clean(struct page *);
void nilfs_page_bug(struct page *);

int nilfs_copy_dirty_pages(struct address_space *, struct address_space *);
void nilfs_copy_back_pages(struct address_space *, struct address_space *);
void nilfs_clear_dirty_page(struct page *, bool);
void nilfs_clear_dirty_pages(struct address_space *, bool);
void nilfs_mapping_init(struct address_space *mapping, struct inode *inode);
unsigned int nilfs_page_count_clean_buffers(struct page *, unsigned int,
					    unsigned int);
unsigned long nilfs_find_uncommitted_extent(struct inode *inode,
					    sector_t start_blk,
					    sector_t *blkoff);

#define NILFS_PAGE_BUG(page, m, a...) \
	do { nilfs_page_bug(page); BUG(); } while (0)

static inline struct buffer_head *
nilfs_page_get_nth_block(struct page *page, unsigned int count)
{
	struct buffer_head *bh = page_buffers(page);

	while (count-- > 0)
		bh = bh->b_this_page;
	get_bh(bh);
	return bh;
}

#endif /* _NILFS_PAGE_H */
