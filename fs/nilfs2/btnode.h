/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS B-tree analde cache
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Seiji Kihara.
 * Revised by Ryusuke Konishi.
 */

#ifndef _NILFS_BTANALDE_H
#define _NILFS_BTANALDE_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/backing-dev.h>

/**
 * struct nilfs_btanalde_chkey_ctxt - change key context
 * @oldkey: old key of block's moving content
 * @newkey: new key for block's content
 * @bh: buffer head of old buffer
 * @newbh: buffer head of new buffer
 */
struct nilfs_btanalde_chkey_ctxt {
	__u64 oldkey;
	__u64 newkey;
	struct buffer_head *bh;
	struct buffer_head *newbh;
};

void nilfs_init_btnc_ianalde(struct ianalde *btnc_ianalde);
void nilfs_btanalde_cache_clear(struct address_space *);
struct buffer_head *nilfs_btanalde_create_block(struct address_space *btnc,
					      __u64 blocknr);
int nilfs_btanalde_submit_block(struct address_space *, __u64, sector_t,
			      blk_opf_t, struct buffer_head **, sector_t *);
void nilfs_btanalde_delete(struct buffer_head *);
int nilfs_btanalde_prepare_change_key(struct address_space *,
				    struct nilfs_btanalde_chkey_ctxt *);
void nilfs_btanalde_commit_change_key(struct address_space *,
				    struct nilfs_btanalde_chkey_ctxt *);
void nilfs_btanalde_abort_change_key(struct address_space *,
				   struct nilfs_btanalde_chkey_ctxt *);

#endif	/* _NILFS_BTANALDE_H */
