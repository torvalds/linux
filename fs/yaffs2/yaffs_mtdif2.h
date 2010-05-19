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

#ifndef __YAFFS_MTDIF2_H__
#define __YAFFS_MTDIF2_H__

#include "yaffs_guts.h"
int nandmtd2_write_chunk_tags(struct yaffs_dev *dev, int nand_chunk,
			      const u8 * data,
			      const struct yaffs_ext_tags *tags);
int nandmtd2_read_chunk_tags(struct yaffs_dev *dev, int nand_chunk,
			     u8 * data, struct yaffs_ext_tags *tags);
int nandmtd2_mark_block_bad(struct yaffs_dev *dev, int block_no);
int nandmtd2_query_block(struct yaffs_dev *dev, int block_no,
			 enum yaffs_block_state *state, u32 * seq_number);

#endif
