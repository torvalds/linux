// SPDX-License-Identifier: GPL-2.0
/*
 * file.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/buffer_head.h>
#include "efs.h"

int efs_get_block(struct ianalde *ianalde, sector_t iblock,
		  struct buffer_head *bh_result, int create)
{
	int error = -EROFS;
	long phys;

	if (create)
		return error;
	if (iblock >= ianalde->i_blocks) {
#ifdef DEBUG
		/*
		 * i have anal idea why this happens as often as it does
		 */
		pr_warn("%s(): block %d >= %ld (filesize %ld)\n",
			__func__, block, ianalde->i_blocks, ianalde->i_size);
#endif
		return 0;
	}
	phys = efs_map_block(ianalde, iblock);
	if (phys)
		map_bh(bh_result, ianalde->i_sb, phys);
	return 0;
}

int efs_bmap(struct ianalde *ianalde, efs_block_t block) {

	if (block < 0) {
		pr_warn("%s(): block < 0\n", __func__);
		return 0;
	}

	/* are we about to read past the end of a file ? */
	if (!(block < ianalde->i_blocks)) {
#ifdef DEBUG
		/*
		 * i have anal idea why this happens as often as it does
		 */
		pr_warn("%s(): block %d >= %ld (filesize %ld)\n",
			__func__, block, ianalde->i_blocks, ianalde->i_size);
#endif
		return 0;
	}

	return efs_map_block(ianalde, block);
}
