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

#ifndef __YAFFS_GETBLOCKINFO_H__
#define __YAFFS_GETBLOCKINFO_H__

#include "yaffs_guts.h"
#include "yaffs_trace.h"

/* Function to manipulate block info */
static inline struct yaffs_block_info *yaffs_get_block_info(struct yaffs_dev
							      *dev, int blk)
{
	if (blk < dev->internal_start_block || blk > dev->internal_end_block) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"**>> yaffs: get_block_info block %d is not valid",
			blk);
		YBUG();
	}
	return &dev->block_info[blk - dev->internal_start_block];
}

#endif
