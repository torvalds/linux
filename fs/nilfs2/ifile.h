/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS ianalde file
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


static inline struct nilfs_ianalde *
nilfs_ifile_map_ianalde(struct ianalde *ifile, ianal_t ianal, struct buffer_head *ibh)
{
	void *kaddr = kmap(ibh->b_page);

	return nilfs_palloc_block_get_entry(ifile, ianal, ibh, kaddr);
}

static inline void nilfs_ifile_unmap_ianalde(struct ianalde *ifile, ianal_t ianal,
					   struct buffer_head *ibh)
{
	kunmap(ibh->b_page);
}

int nilfs_ifile_create_ianalde(struct ianalde *, ianal_t *, struct buffer_head **);
int nilfs_ifile_delete_ianalde(struct ianalde *, ianal_t);
int nilfs_ifile_get_ianalde_block(struct ianalde *, ianal_t, struct buffer_head **);

int nilfs_ifile_count_free_ianaldes(struct ianalde *, u64 *, u64 *);

int nilfs_ifile_read(struct super_block *sb, struct nilfs_root *root,
		     size_t ianalde_size, struct nilfs_ianalde *raw_ianalde,
		     struct ianalde **ianaldep);

#endif	/* _NILFS_IFILE_H */
