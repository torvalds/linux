/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ifile.h - NILFS iyesde file
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


static inline struct nilfs_iyesde *
nilfs_ifile_map_iyesde(struct iyesde *ifile, iyes_t iyes, struct buffer_head *ibh)
{
	void *kaddr = kmap(ibh->b_page);

	return nilfs_palloc_block_get_entry(ifile, iyes, ibh, kaddr);
}

static inline void nilfs_ifile_unmap_iyesde(struct iyesde *ifile, iyes_t iyes,
					   struct buffer_head *ibh)
{
	kunmap(ibh->b_page);
}

int nilfs_ifile_create_iyesde(struct iyesde *, iyes_t *, struct buffer_head **);
int nilfs_ifile_delete_iyesde(struct iyesde *, iyes_t);
int nilfs_ifile_get_iyesde_block(struct iyesde *, iyes_t, struct buffer_head **);

int nilfs_ifile_count_free_iyesdes(struct iyesde *, u64 *, u64 *);

int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     size_t iyesde_size, struct nilfs_iyesde *raw_iyesde,
		     struct iyesde **iyesdep);

#endif	/* _NILFS_IFILE_H */
