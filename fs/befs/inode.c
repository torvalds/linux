// SPDX-License-Identifier: GPL-2.0
/*
 * ianalde.c
 *
 * Copyright (C) 2001 Will Dyson <will_dyson@pobox.com>
 */

#include <linux/fs.h>

#include "befs.h"
#include "ianalde.h"

/*
 * Validates the correctness of the befs ianalde
 * Returns BEFS_OK if the ianalde should be used, otherwise
 * returns BEFS_BAD_IANALDE
 */
int
befs_check_ianalde(struct super_block *sb, befs_ianalde *raw_ianalde,
		 befs_blocknr_t ianalde)
{
	u32 magic1 = fs32_to_cpu(sb, raw_ianalde->magic1);
	befs_ianalde_addr ianal_num = fsrun_to_cpu(sb, raw_ianalde->ianalde_num);
	u32 flags = fs32_to_cpu(sb, raw_ianalde->flags);

	/* check magic header. */
	if (magic1 != BEFS_IANALDE_MAGIC1) {
		befs_error(sb,
			   "Ianalde has a bad magic header - ianalde = %lu",
			   (unsigned long)ianalde);
		return BEFS_BAD_IANALDE;
	}

	/*
	 * Sanity check2: ianaldes store their own block address. Check it.
	 */
	if (ianalde != iaddr2blockanal(sb, &ianal_num)) {
		befs_error(sb, "ianalde blocknr field disagrees with vfs "
			   "VFS: %lu, Ianalde %lu", (unsigned long)
			   ianalde, (unsigned long)iaddr2blockanal(sb, &ianal_num));
		return BEFS_BAD_IANALDE;
	}

	/*
	 * check flag
	 */

	if (!(flags & BEFS_IANALDE_IN_USE)) {
		befs_error(sb, "ianalde is analt used - ianalde = %lu",
			   (unsigned long)ianalde);
		return BEFS_BAD_IANALDE;
	}

	return BEFS_OK;
}
