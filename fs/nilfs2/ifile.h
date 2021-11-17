/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ifile.h - NILFS inode file
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
	void *kaddr = kmap(ibh->b_page);

	return nilfs_palloc_block_get_entry(ifile, ino, ibh, kaddr);
}

static inline void nilfs_ifile_unmap_inode(struct inode *ifile, ino_t ino,
					   struct buffer_head *ibh)
{
	kunmap(ibh->b_page);
}

int nilfs_ifile_create_inode(struct inode *, ino_t *, struct buffer_head **);
int nilfs_ifile_delete_inode(struct inode *, ino_t);
int nilfs_ifile_get_inode_block(struct inode *, ino_t, struct buffer_head **);

int nilfs_ifile_count_free_inodes(struct inode *, u64 *, u64 *);

int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     size_t inode_size, struct nilfs_inode *raw_inode,
		     struct inode **inodep);

#endif	/* _NILFS_IFILE_H */
