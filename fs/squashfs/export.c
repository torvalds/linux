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
 * The export code uses an iyesde lookup table to map iyesde numbers passed in
 * filehandles to an iyesde location on disk.  This table is stored compressed
 * into metadata blocks.  A second index table is used to locate these.  This
 * second index table for speed of access (and because it is small) is read at
 * mount time and cached in memory.
 *
 * The iyesde lookup table is used only by the export code, iyesde disk
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
 * Look-up iyesde number (iyes) in table, returning the iyesde location.
 */
static long long squashfs_iyesde_lookup(struct super_block *sb, int iyes_num)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int blk = SQUASHFS_LOOKUP_BLOCK(iyes_num - 1);
	int offset = SQUASHFS_LOOKUP_BLOCK_OFFSET(iyes_num - 1);
	u64 start = le64_to_cpu(msblk->iyesde_lookup_table[blk]);
	__le64 iyes;
	int err;

	TRACE("Entered squashfs_iyesde_lookup, iyesde_number = %d\n", iyes_num);

	err = squashfs_read_metadata(sb, &iyes, &start, &offset, sizeof(iyes));
	if (err < 0)
		return err;

	TRACE("squashfs_iyesde_lookup, iyesde = 0x%llx\n",
		(u64) le64_to_cpu(iyes));

	return le64_to_cpu(iyes);
}


static struct dentry *squashfs_export_iget(struct super_block *sb,
	unsigned int iyes_num)
{
	long long iyes;
	struct dentry *dentry = ERR_PTR(-ENOENT);

	TRACE("Entered squashfs_export_iget\n");

	iyes = squashfs_iyesde_lookup(sb, iyes_num);
	if (iyes >= 0)
		dentry = d_obtain_alias(squashfs_iget(sb, iyes, iyes_num));

	return dentry;
}


static struct dentry *squashfs_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if ((fh_type != FILEID_INO32_GEN && fh_type != FILEID_INO32_GEN_PARENT)
			|| fh_len < 2)
		return NULL;

	return squashfs_export_iget(sb, fid->i32.iyes);
}


static struct dentry *squashfs_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if (fh_type != FILEID_INO32_GEN_PARENT || fh_len < 4)
		return NULL;

	return squashfs_export_iget(sb, fid->i32.parent_iyes);
}


static struct dentry *squashfs_get_parent(struct dentry *child)
{
	struct iyesde *iyesde = d_iyesde(child);
	unsigned int parent_iyes = squashfs_i(iyesde)->parent;

	return squashfs_export_iget(iyesde->i_sb, parent_iyes);
}


/*
 * Read uncompressed iyesde lookup table indexes off disk into memory
 */
__le64 *squashfs_read_iyesde_lookup_table(struct super_block *sb,
		u64 lookup_table_start, u64 next_table, unsigned int iyesdes)
{
	unsigned int length = SQUASHFS_LOOKUP_BLOCK_BYTES(iyesdes);
	__le64 *table;

	TRACE("In read_iyesde_lookup_table, length %d\n", length);

	/* Sanity check values */

	/* there should always be at least one iyesde */
	if (iyesdes == 0)
		return ERR_PTR(-EINVAL);

	/* length bytes should yest extend into the next table - this check
	 * also traps instances where lookup_table_start is incorrectly larger
	 * than the next table start
	 */
	if (lookup_table_start + length > next_table)
		return ERR_PTR(-EINVAL);

	table = squashfs_read_table(sb, lookup_table_start, length);

	/*
	 * table[0] points to the first iyesde lookup table metadata block,
	 * this should be less than lookup_table_start
	 */
	if (!IS_ERR(table) && le64_to_cpu(table[0]) >= lookup_table_start) {
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
