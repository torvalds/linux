// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2010
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * xattr_id.c
 */

/*
 * This file implements code to map the 32-bit xattr id stored in the inode
 * into the on disk location of the xattr data.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "xattr.h"

/*
 * Map xattr id using the xattr id look up table
 */
int squashfs_xattr_lookup(struct super_block *sb, unsigned int index,
		int *count, unsigned int *size, unsigned long long *xattr)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int block = SQUASHFS_XATTR_BLOCK(index);
	int offset = SQUASHFS_XATTR_BLOCK_OFFSET(index);
	u64 start_block;
	struct squashfs_xattr_id id;
	int err;

	if (index >= msblk->xattr_ids)
		return -EINVAL;

	start_block = le64_to_cpu(msblk->xattr_id_table[block]);

	err = squashfs_read_metadata(sb, &id, &start_block, &offset,
							sizeof(id));
	if (err < 0)
		return err;

	*xattr = le64_to_cpu(id.xattr);
	*size = le32_to_cpu(id.size);
	*count = le32_to_cpu(id.count);
	return 0;
}


/*
 * Read uncompressed xattr id lookup table indexes from disk into memory
 */
__le64 *squashfs_read_xattr_id_table(struct super_block *sb, u64 table_start,
		u64 *xattr_table_start, unsigned int *xattr_ids)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	unsigned int len, indexes;
	struct squashfs_xattr_id_table *id_table;
	__le64 *table;
	u64 start, end;
	int n;

	id_table = squashfs_read_table(sb, table_start, sizeof(*id_table));
	if (IS_ERR(id_table))
		return (__le64 *) id_table;

	*xattr_table_start = le64_to_cpu(id_table->xattr_table_start);
	*xattr_ids = le32_to_cpu(id_table->xattr_ids);
	kfree(id_table);

	/* Sanity check values */

	/* there is always at least one xattr id */
	if (*xattr_ids <= 0)
		return ERR_PTR(-EINVAL);

	len = SQUASHFS_XATTR_BLOCK_BYTES(*xattr_ids);
	indexes = SQUASHFS_XATTR_BLOCKS(*xattr_ids);

	/*
	 * The computed size of the index table (len bytes) should exactly
	 * match the table start and end points
	 */
	start = table_start + sizeof(*id_table);
	end = msblk->bytes_used;

	if (len != (end - start))
		return ERR_PTR(-EINVAL);

	table = squashfs_read_table(sb, start, len);
	if (IS_ERR(table))
		return table;

	/* table[0], table[1], ... table[indexes - 1] store the locations
	 * of the compressed xattr id blocks.  Each entry should be less than
	 * the next (i.e. table[0] < table[1]), and the difference between them
	 * should be SQUASHFS_METADATA_SIZE or less.  table[indexes - 1]
	 * should be less than table_start, and again the difference
	 * shouls be SQUASHFS_METADATA_SIZE or less.
	 *
	 * Finally xattr_table_start should be less than table[0].
	 */
	for (n = 0; n < (indexes - 1); n++) {
		start = le64_to_cpu(table[n]);
		end = le64_to_cpu(table[n + 1]);

		if (start >= end || (end - start) >
				(SQUASHFS_METADATA_SIZE + SQUASHFS_BLOCK_OFFSET)) {
			kfree(table);
			return ERR_PTR(-EINVAL);
		}
	}

	start = le64_to_cpu(table[indexes - 1]);
	if (start >= table_start || (table_start - start) >
				(SQUASHFS_METADATA_SIZE + SQUASHFS_BLOCK_OFFSET)) {
		kfree(table);
		return ERR_PTR(-EINVAL);
	}

	if (*xattr_table_start >= le64_to_cpu(table[0])) {
		kfree(table);
		return ERR_PTR(-EINVAL);
	}

	return table;
}
