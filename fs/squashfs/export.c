// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * export.c
 */

/*
 * This file implements code to make Squashfs filesystems exportable (NFS etc.)
 *
 * The export code uses an inode lookup table to map inode numbers passed in
 * filehandles to an inode location on disk.  This table is stored compressed
 * into metadata blocks.  A second index table is used to locate these.  This
 * second index table for speed of access (and because it is small) is read at
 * mount time and cached in memory.
 *
 * The inode lookup table is used only by the export code, inode disk
 * locations are directly encoded in directories, enabling direct access
 * without an intermediate lookup for all operations except the export ops.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/dcache.h>
#include <linux/exportfs.h>
#include <linux/slab.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"

/*
 * Look-up inode number (ino) in table, returning the inode location.
 */
static long long squashfs_inode_lookup(struct super_block *sb, int ino_num)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int blk = SQUASHFS_LOOKUP_BLOCK(ino_num - 1);
	int offset = SQUASHFS_LOOKUP_BLOCK_OFFSET(ino_num - 1);
	u64 start;
	__le64 ino;
	int err;

	TRACE("Entered squashfs_inode_lookup, inode_number = %d\n", ino_num);

	if (ino_num == 0 || (ino_num - 1) >= msblk->inodes)
		return -EINVAL;

	start = le64_to_cpu(msblk->inode_lookup_table[blk]);

	err = squashfs_read_metadata(sb, &ino, &start, &offset, sizeof(ino));
	if (err < 0)
		return err;

	TRACE("squashfs_inode_lookup, inode = 0x%llx\n",
		(u64) le64_to_cpu(ino));

	return le64_to_cpu(ino);
}


static struct dentry *squashfs_export_iget(struct super_block *sb,
	unsigned int ino_num)
{
	long long ino;
	struct dentry *dentry = ERR_PTR(-ENOENT);

	TRACE("Entered squashfs_export_iget\n");

	ino = squashfs_inode_lookup(sb, ino_num);
	if (ino >= 0)
		dentry = d_obtain_alias(squashfs_iget(sb, ino, ino_num));

	return dentry;
}


static struct dentry *squashfs_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if ((fh_type != FILEID_INO32_GEN && fh_type != FILEID_INO32_GEN_PARENT)
			|| fh_len < 2)
		return NULL;

	return squashfs_export_iget(sb, fid->i32.ino);
}


static struct dentry *squashfs_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if (fh_type != FILEID_INO32_GEN_PARENT || fh_len < 4)
		return NULL;

	return squashfs_export_iget(sb, fid->i32.parent_ino);
}


static struct dentry *squashfs_get_parent(struct dentry *child)
{
	struct inode *inode = d_inode(child);
	unsigned int parent_ino = squashfs_i(inode)->parent;

	return squashfs_export_iget(inode->i_sb, parent_ino);
}


/*
 * Read uncompressed inode lookup table indexes off disk into memory
 */
__le64 *squashfs_read_inode_lookup_table(struct super_block *sb,
		u64 lookup_table_start, u64 next_table, unsigned int inodes)
{
	unsigned int length = SQUASHFS_LOOKUP_BLOCK_BYTES(inodes);
	unsigned int indexes = SQUASHFS_LOOKUP_BLOCKS(inodes);
	int n;
	__le64 *table;
	u64 start, end;

	TRACE("In read_inode_lookup_table, length %d\n", length);

	/* Sanity check values */

	/* there should always be at least one inode */
	if (inodes == 0)
		return ERR_PTR(-EINVAL);

	/*
	 * The computed size of the lookup table (length bytes) should exactly
	 * match the table start and end points
	 */
	if (length != (next_table - lookup_table_start))
		return ERR_PTR(-EINVAL);

	table = squashfs_read_table(sb, lookup_table_start, length);
	if (IS_ERR(table))
		return table;

	/*
	 * table0], table[1], ... table[indexes - 1] store the locations
	 * of the compressed inode lookup blocks.  Each entry should be
	 * less than the next (i.e. table[0] < table[1]), and the difference
	 * between them should be SQUASHFS_METADATA_SIZE or less.
	 * table[indexes - 1] should  be less than lookup_table_start, and
	 * again the difference should be SQUASHFS_METADATA_SIZE or less
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
	if (start >= lookup_table_start || (lookup_table_start - start) > SQUASHFS_METADATA_SIZE) {
		kfree(table);
		return ERR_PTR(-EINVAL);
	}

	return table;
}


const struct export_operations squashfs_export_ops = {
	.fh_to_dentry = squashfs_fh_to_dentry,
	.fh_to_parent = squashfs_fh_to_parent,
	.get_parent = squashfs_get_parent
};
