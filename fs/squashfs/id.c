// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * id.c
 */

/*
 * This file implements code to handle uids and gids.
 *
 * For space efficiency regular files store uid and gid indexes, which are
 * converted to 32-bit uids/gids using an id look up table.  This table is
 * stored compressed into metadata blocks.  A second index table is used to
 * locate these.  This second index table for speed of access (and because it
 * is small) is read at mount time and cached in memory.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"

/*
 * Map uid/gid index into real 32-bit uid/gid using the id look up table
 */
int squashfs_get_id(struct super_block *sb, unsigned int index,
					unsigned int *id)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int block = SQUASHFS_ID_BLOCK(index);
	int offset = SQUASHFS_ID_BLOCK_OFFSET(index);
	u64 start_block;
	__le32 disk_id;
	int err;

	if (index >= msblk->ids)
		return -EINVAL;

	start_block = le64_to_cpu(msblk->id_table[block]);

	err = squashfs_read_metadata(sb, &disk_id, &start_block, &offset,
							sizeof(disk_id));
	if (err < 0)
		return err;

	*id = le32_to_cpu(disk_id);
	return 0;
}


/*
 * Read uncompressed id lookup table indexes from disk into memory
 */
__le64 *squashfs_read_id_index_table(struct super_block *sb,
		u64 id_table_start, u64 next_table, unsigned short no_ids)
{
	unsigned int length = SQUASHFS_ID_BLOCK_BYTES(no_ids);
	unsigned int indexes = SQUASHFS_ID_BLOCKS(no_ids);
	int n;
	__le64 *table;
	u64 start, end;

	TRACE("In read_id_index_table, length %d\n", length);

	/* Sanity check values */

	/* there should always be at least one id */
	if (no_ids == 0)
		return ERR_PTR(-EINVAL);

	/*
	 * The computed size of the index table (length bytes) should exactly
	 * match the table start and end points
	 */
	if (length != (next_table - id_table_start))
		return ERR_PTR(-EINVAL);

	table = squashfs_read_table(sb, id_table_start, length);
	if (IS_ERR(table))
		return table;

	/*
	 * table[0], table[1], ... table[indexes - 1] store the locations
	 * of the compressed id blocks.   Each entry should be less than
	 * the next (i.e. table[0] < table[1]), and the difference between them
	 * should be SQUASHFS_METADATA_SIZE or less.  table[indexes - 1]
	 * should be less than id_table_start, and again the difference
	 * should be SQUASHFS_METADATA_SIZE or less
	 */
	for (n = 0; n < (indexes - 1); n++) {
		start = le64_to_cpu(table[n]);
		end = le64_to_cpu(table[n + 1]);

		if (start >= end || (end - start) > SQUASHFS_METADATA_SIZE) {
			kfree(table);
			return ERR_PTR(-EINVAL);
		}
	}

	start = le64_to_cpu(table[indexes - 1]);
	if (start >= id_table_start || (id_table_start - start) > SQUASHFS_METADATA_SIZE) {
		kfree(table);
		return ERR_PTR(-EINVAL);
	}

	return table;
}
