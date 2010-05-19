/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_YAFFS2_H__
#define __YAFFS_YAFFS2_H__

#include "yaffs_guts.h"

void yaffs_calc_oldest_dirty_seq(struct yaffs_dev *dev);
void yaffs2_find_oldest_dirty_seq(struct yaffs_dev *dev);
void yaffs2_clear_oldest_dirty_seq(struct yaffs_dev *dev,
				   struct yaffs_block_info *bi);
void yaffs2_update_oldest_dirty_seq(struct yaffs_dev *dev, unsigned block_no,
				    struct yaffs_block_info *bi);
int yaffs_block_ok_for_gc(struct yaffs_dev *dev, struct yaffs_block_info *bi);
u32 yaffs2_find_refresh_block(struct yaffs_dev *dev);
int yaffs2_checkpt_required(struct yaffs_dev *dev);
int yaffs_calc_checkpt_blocks_required(struct yaffs_dev *dev);

void yaffs2_checkpt_invalidate(struct yaffs_dev *dev);
int yaffs2_checkpt_save(struct yaffs_dev *dev);
int yaffs2_checkpt_restore(struct yaffs_dev *dev);

int yaffs2_handle_hole(struct yaffs_obj *obj, loff_t new_size);
int yaffs2_scan_backwards(struct yaffs_dev *dev);

#endif
