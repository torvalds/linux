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

#ifndef __YAFFS_CHECKPTRW_H__
#define __YAFFS_CHECKPTRW_H__

#include "yaffs_guts.h"

int yaffs2_checkpt_open(struct yaffs_dev *dev, int writing);

int yaffs2_checkpt_wr(struct yaffs_dev *dev, const void *data, int n_bytes);

int yaffs2_checkpt_rd(struct yaffs_dev *dev, void *data, int n_bytes);

int yaffs2_get_checkpt_sum(struct yaffs_dev *dev, u32 * sum);

int yaffs_checkpt_close(struct yaffs_dev *dev);

int yaffs2_checkpt_invalidate_stream(struct yaffs_dev *dev);

#endif
