/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yportenv.h"


#include "yaffs_mtdif.h"

#include "linux/mtd/mtd.h"
#include "linux/types.h"
#include "linux/time.h"
#include "linux/mtd/nand.h"

#include "yaffs_linux.h"

int nandmtd_EraseBlockInNAND(yaffs_Device *dev, int blockNumber)
{
	struct mtd_info *mtd = yaffs_DeviceToContext(dev)->mtd;
	__u32 addr =
	    ((loff_t) blockNumber) * dev->param.totalBytesPerChunk
		* dev->param.nChunksPerBlock;
	struct erase_info ei;
	
	int retval = 0;

	ei.mtd = mtd;
	ei.addr = addr;
	ei.len = dev->param.totalBytesPerChunk * dev->param.nChunksPerBlock;
	ei.time = 1000;
	ei.retries = 2;
	ei.callback = NULL;
	ei.priv = (u_long) dev;

	retval = mtd->erase(mtd, &ei);

	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

int nandmtd_InitialiseNAND(yaffs_Device *dev)
{
	return YAFFS_OK;
}

