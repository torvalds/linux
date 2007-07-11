/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __OPS_ADDRESS_DOT_H__
#define __OPS_ADDRESS_DOT_H__

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>

extern const struct address_space_operations gfs2_file_aops;
extern int gfs2_get_block(struct inode *inode, sector_t lblock,
			  struct buffer_head *bh_result, int create);
extern int gfs2_releasepage(struct page *page, gfp_t gfp_mask);

#endif /* __OPS_ADDRESS_DOT_H__ */
