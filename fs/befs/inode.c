// SPDX-License-Identifier: GPL-2.0
/*
 * iyesde.c
 *
 * Copyright (C) 2001 Will Dyson <will_dyson@pobox.com>
 */

#include <linux/fs.h>

#include "befs.h"
#include "iyesde.h"

/*
 * Validates the correctness of the befs iyesde
 * Returns BEFS_OK if the iyesde should be used, otherwise
 * returns BEFS_BAD_INODE
 */
int
befs_check_iyesde(struct super_block *sb, befs_iyesde *raw_iyesde,
		 befs_blocknr_t iyesde)
{
	u32 magic1 = fs32_to_cpu(sb, raw_iyesde->magic1);
	befs_iyesde_addr iyes_num = fsrun_to_cpu(sb, raw_iyesde->iyesde_num);
	u32 flags = fs32_to_cpu(sb, raw_iyesde->flags);

	/* check magic header. */
	if (magic1 != BEFS_INODE_MAGIC1) {
		befs_error(sb,
			   "Iyesde has a bad magic header - iyesde = %lu",
			   (unsigned long)iyesde);
		return BEFS_BAD_INODE;
	}

	/*
	 * Sanity check2: iyesdes store their own block address. Check it.
	 */
	if (iyesde != iaddr2blockyes(sb, &iyes_num)) {
		befs_error(sb, "iyesde blocknr field disagrees with vfs "
			   "VFS: %lu, Iyesde %lu", (unsigned long)
			   iyesde, (unsigned long)iaddr2blockyes(sb, &iyes_num));
		return BEFS_BAD_INODE;
	}

	/*
	 * check flag
	 */

	if (!(flags & BEFS_INODE_IN_USE)) {
		befs_error(sb, "iyesde is yest used - iyesde = %lu",
			   (unsigned long)iyesde);
		return BEFS_BAD_INODE;
	}

	return BEFS_OK;
}
