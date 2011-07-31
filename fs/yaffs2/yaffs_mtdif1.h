/*
 * YAFFS: Yet another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_MTDIF1_H__
#define __YAFFS_MTDIF1_H__

int nandmtd1_WriteChunkWithTagsToNAND(yaffs_Device *dev, int chunkInNAND,
	const __u8 *data, const yaffs_ExtendedTags *tags);

int nandmtd1_ReadChunkWithTagsFromNAND(yaffs_Device *dev, int chunkInNAND,
	__u8 *data, yaffs_ExtendedTags *tags);

int nandmtd1_MarkNANDBlockBad(struct yaffs_DeviceStruct *dev, int blockNo);

int nandmtd1_QueryNANDBlock(struct yaffs_DeviceStruct *dev, int blockNo,
	yaffs_BlockState *state, __u32 *sequenceNumber);

#endif
