/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Buffer/page management specific to NILFS
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


void __nilfs_clear_folio_dirty(struct folio *);

struct buffer_head *nilfs_grab_buffer(struct inode *, struct address_space *,
				      unsigned long, unsigned long);
void nilfs_forget_buffer(struct buffer_head *);
void nilfs_copy_buffer(struct buffer_head *, struct buffer_head *);
bool nilfs_folio_buffers_clean(struct folio *);
void nilfs_folio_bug(struct folio *);

int nilfs_copy_dirty_pages(struct address_space *, struct address_space *);
void nilfs_copy_back_pages(struct address_space *, struct address_space *);
void nilfs_clear_folio_dirty(struct folio *folio);
void nilfs_clear_dirty_pages(struct address_space *mapping);
unsigned int nilfs_page_count_clean_buffers(struct folio *folio,
		unsigned int from, unsigned int to);
unsigned long nilfs_find_uncommitted_extent(struct inode *inode,
					    sector_t start_blk,
					    sector_t *blkoff);

#define NILFS_FOLIO_BUG(folio, m, a...) \
	do { nilfs_folio_bug(folio); BUG(); } while (0)

#endif /* _NILFS_PAGE_H */
