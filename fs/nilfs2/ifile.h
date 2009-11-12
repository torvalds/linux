/*
 * ifile.h - NILFS inode file
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Amagai Yoshiji <amagai@osrg.net>
 * Revised by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */

#ifndef _NILFS_IFILE_H
#define _NILFS_IFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/nilfs2_fs.h>
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

struct inode *nilfs_ifile_new(struct nilfs_sb_info *sbi, size_t inode_size);

#endif	/* _NILFS_IFILE_H */
