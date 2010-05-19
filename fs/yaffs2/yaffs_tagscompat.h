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

#ifndef __YAFFS_TAGSCOMPAT_H__
#define __YAFFS_TAGSCOMPAT_H__

#include "yaffs_guts.h"
int yaffs_tags_compat_wr(struct yaffs_dev *dev,
			 int nand_chunk,
			 const u8 * data, const struct yaffs_ext_tags *tags);
int yaffs_tags_compat_rd(struct yaffs_dev *dev,
			 int nand_chunk,
			 u8 * data, struct yaffs_ext_tags *tags);
int yaffs_tags_compat_mark_bad(struct yaffs_dev *dev, int block_no);
int yaffs_tags_compat_query_block(struct yaffs_dev *dev,
				  int block_no,
				  enum yaffs_block_state *state,
				  u32 * seq_number);

void yaffs_calc_tags_ecc(struct yaffs_tags *tags);
int yaffs_check_tags_ecc(struct yaffs_tags *tags);
int yaffs_count_bits(u8 byte);

#endif
