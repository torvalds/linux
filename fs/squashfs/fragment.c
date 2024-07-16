// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * fragment.c
 */

/*
 * This file implements code to handle compressed fragments (tail-end packed
 * datablocks).
 *
 * Regular files contain a fragment index which is mapped to a fragment
 * location on disk and compressed size using a fragment lookup table.
 * Like everything in Squashfs this fragment lookup table is itself stored
 * compressed into metadata blocks.  A second index table is used to locate
 * these.  This second index table for speed of access (and because it
 * is small) is read at mount time and cached in memory.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/slab.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"

/*
 * Look-up fragment using the fragment index table.  Return the on disk
 * location of the fragment and its compressed size
 */
int squashfs_frag_lookup(struct super_block *sb, unsigned int fragment,
				u64 *fragment_block)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int block, offset, size;
	struct squashfs_fragment_entry fragment_entry;
	u64 start_block;

	if (fragment >= msblk->fragments)
		return -EIO;
	block = SQUASHFS_FRAGMENT_INDEX(fragment);
	offset = SQUASHFS_FRAGMENT_INDEX_OFFSET(fragment);

	start_block = le64_to_cpu(msblk->fragment_index[block]);

	size = squashfs_read_metadata(sb, &fragment_entry, &start_block,
					&offset, sizeof(fragment_entry));
	if (size < 0)
		return size;

	*fragment_block = le64_to_cpu(fragment_entry.start_block);
	return squashfs_block_size(fragment_entry.size);
}


/*
 * Read the uncompressed fragment lookup table indexes off disk into memory
 */
__le64 *squashfs_read_fragment_index_table(struct super_block *sb,
	u64 fragment_table_start, u64 next_table, unsigned int fragments)
{
	unsigned int length = SQUASHFS_FRAGMENT_INDEX_BYTES(fragments);
	__le64 *table;

	/*
	 * Sanity check, length bytes should not extend into the next table -
	 * this check also traps instances where fragment_table_start is
	 * incorrectly larger than the next table start
	 */
	if (fragment_table_start + length > next_table)
		return ERR_PTR(-EINVAL);

	table = squashfs_read_table(sb, fragment_table_start, length);

	/*
	 * table[0] points to the first fragment table metadata block, this
	 * should be less than fragment_table_start
	 */
	if (!IS_ERR(table) && le64_to_cpu(table[0]) >= fragment_table_start) {
		kfree(table);
		return ERR_PTR(-EINVAL);
	}

	return table;
}
