/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS inode file
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Amagai Yoshiji.
 * Revised by Ryusuke Konishi.
 *
 */

#ifndef _NILFS_IFILE_H
#define _NILFS_IFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "mdt.h"
#include "alloc.h"


static inline struct nilfs_inode *
nilfs_ifile_map_inode(struct inode *ifile, ino_t ino, struct buffer_head *ibh)
{
	size_t __offset_in_folio = nilfs_palloc_entry_offset(ifile, ino, ibh);

	return kmap_local_folio(ibh->b_folio, __offset_in_folio);
}

static inline void nilfs_ifile_unmap_inode(struct nilfs_inode *raw_inode)
{
	kunmap_local(raw_inode);
}

int nilfs_ifile_create_inode(struct inode *, ino_t *, struct buffer_head **);
int nilfs_ifile_delete_inode(struct inode *, ino_t);
int nilfs_ifile_get_inode_block(struct inode *, ino_t, struct buffer_head **);

int nilfs_ifile_count_free_inodes(struct inode *, u64 *, u64 *);

int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     __u64 cno, size_t inode_size);

#endif	/* _NILFS_IFILE_H */
