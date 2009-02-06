/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@lougher.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
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
#include <linux/zlib.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"

/*
 * Look-up fragment using the fragment index table.  Return the on disk
 * location of the fragment and its compressed size
 */
int squashfs_frag_lookup(struct super_block *sb, unsigned int fragment,
				u64 *fragment_block)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int block = SQUASHFS_FRAGMENT_INDEX(fragment);
	int offset = SQUASHFS_FRAGMENT_INDEX_OFFSET(fragment);
	u64 start_block = le64_to_cpu(msblk->fragment_index[block]);
	struct squashfs_fragment_entry fragment_entry;
	int size;

	size = squashfs_read_metadata(sb, &fragment_entry, &start_block,
					&offset, sizeof(fragment_entry));
	if (size < 0)
		return size;

	*fragment_block = le64_to_cpu(fragment_entry.start_block);
	size = le32_to_cpu(fragment_entry.size);

	return size;
}


/*
 * Read the uncompressed fragment lookup table indexes off disk into memory
 */
__le64 *squashfs_read_fragment_index_table(struct super_block *sb,
	u64 fragment_table_start, unsigned int fragments)
{
	unsigned int length = SQUASHFS_FRAGMENT_INDEX_BYTES(fragments);
	__le64 *fragment_index;
	int err;

	/* Allocate fragment lookup table indexes */
	fragment_index = kmalloc(length, GFP_KERNEL);
	if (fragment_index == NULL) {
		ERROR("Failed to allocate fragment index table\n");
		return ERR_PTR(-ENOMEM);
	}

	err = squashfs_read_table(sb, fragment_index, fragment_table_start,
			length);
	if (err < 0) {
		ERROR("unable to read fragment index table\n");
		kfree(fragment_index);
		return ERR_PTR(err);
	}

	return fragment_index;
}
