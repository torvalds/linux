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

#ifndef __YAFFS_NAND_H__
#define __YAFFS_NAND_H__
#include "yaffs_guts.h"

int yaffs_rd_chunk_tags_nand(struct yaffs_dev *dev, int nand_chunk,
			     u8 * buffer, struct yaffs_ext_tags *tags);

int yaffs_wr_chunk_tags_nand(struct yaffs_dev *dev,
			     int nand_chunk,
			     const u8 * buffer, struct yaffs_ext_tags *tags);

int yaffs_mark_bad(struct yaffs_dev *dev, int block_no);

int yaffs_query_init_block_state(struct yaffs_dev *dev,
				 int block_no,
				 enum yaffs_block_state *state,
				 unsigned *seq_number);

int yaffs_erase_block(struct yaffs_dev *dev, int flash_block);

int yaffs_init_nand(struct yaffs_dev *dev);

#endif
