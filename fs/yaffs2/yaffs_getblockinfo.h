/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2007 Aleph One Ltd.
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

/* Function to manipulate block info */
static Y_INLINE yaffs_BlockInfo *yaffs_GetBlockInfo(yaffs_Device * dev, int blk)
{
	if (blk < dev->internalStartBlock || blk > dev->internalEndBlock) {
		T(YAFFS_TRACE_ERROR,
		  (TSTR
		   ("**>> yaffs: getBlockInfo block %d is not valid" TENDSTR),
		   blk));
		YBUG();
	}
	return &dev->blockInfo[blk - dev->internalStartBlock];
}

#endif
