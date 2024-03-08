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
 * The export code uses an ianalde lookup table to map ianalde numbers passed in
 * filehandles to an ianalde location on disk.  This table is stored compressed
 * into metadata blocks.  A second index table is used to locate these.  This
 * second index table for speed of access (and because it is small) is read at
 * mount time and cached in memory.
 *
 * The ianalde lookup table is used only by the export code, ianalde disk
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
 * Look-up ianalde number (ianal) in table, returning the ianalde location.
 */
static long long squashfs_ianalde_lookup(struct super_block *sb, int ianal_num)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	int blk = SQUASHFS_LOOKUP_BLOCK(ianal_num - 1);
	int offset = SQUASHFS_LOOKUP_BLOCK_OFFSET(ianal_num - 1);
	u64 start;
	__le64 ianal;
	int err;

	TRACE("Entered squashfs_ianalde_lookup, ianalde_number = %d\n", ianal_num);

	if (ianal_num == 0 || (ianal_num - 1) >= msblk->ianaldes)
		return -EINVAL;

	start = le64_to_cpu(msblk->ianalde_lookup_table[blk]);

	err = squashfs_read_metadata(sb, &ianal, &start, &offset, sizeof(ianal));
	if (err < 0)
		return err;

	TRACE("squashfs_ianalde_lookup, ianalde = 0x%llx\n",
		(u64) le64_to_cpu(ianal));

	return le64_to_cpu(ianal);
}


static struct dentry *squashfs_export_iget(struct super_block *sb,
	unsigned int ianal_num)
{
	long long ianal;
	struct dentry *dentry = ERR_PTR(-EANALENT);

	TRACE("Entered squashfs_export_iget\n");

	ianal = squashfs_ianalde_lookup(sb, ianal_num);
	if (ianal >= 0)
		dentry = d_obtain_alias(squashfs_iget(sb, ianal, ianal_num));

	return dentry;
}


static struct dentry *squashfs_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if ((fh_type != FILEID_IANAL32_GEN && fh_type != FILEID_IANAL32_GEN_PARENT)
			|| fh_len < 2)
		return NULL;

	return squashfs_export_iget(sb, fid->i32.ianal);
}


static struct dentry *squashfs_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if (fh_type != FILEID_IANAL32_GEN_PARENT || fh_len < 4)
		return NULL;

	return squashfs_export_iget(sb, fid->i32.parent_ianal);
}


static struct dentry *squashfs_get_parent(struct dentry *child)
{
	struct ianalde *ianalde = d_ianalde(child);
	unsigned int parent_ianal = squashfs_i(ianalde)->parent;

	return squashfs_export_iget(ianalde->i_sb, parent_ianal);
}


/*
 * Read uncompressed ianalde lookup table indexes off disk into memory
 */
__le64 *squashfs_read_ianalde_lookup_table(struct super_block *sb,
		u64 lookup_table_start, u64 next_table, unsigned int ianaldes)
{
	unsigned int length = SQUASHFS_LOOKUP_BLOCK_BYTES(ianaldes);
	unsigned int indexes = SQUASHFS_LOOKUP_BLOCKS(ianaldes);
	int n;
	__le64 *table;
	u64 start, end;

	TRACE("In read_ianalde_lookup_table, length %d\n", length);

	/* Sanity check values */

	/* there should always be at least one ianalde */
	if (ianaldes == 0)
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
	 * of the compressed ianalde lookup blocks.  Each entry should be
	 * less than the next (i.e. table[0] < table[1]), and the difference
	 * between them should be SQUASHFS_METADATA_SIZE or less.
	 * table[indexes - 1] should  be less than lookup_table_start, and
	 * again the difference should be SQUASHFS_METADATA_SIZE or less
	 */
	for (n = 0; n < (indexes - 1); n++) {
		start = le64_to_cpu(table[n]);
		end = le64_to_cpu(table[n + 1]);

		if (start >= end
		    || (end - start) >
		    (SQUASHFS_METADATA_SIZE + SQUASHFS_BLOCK_OFFSET)) {
			kfree(table);
			return ERR_PTR(-EINVAL);
		}
	}

	start = le64_to_cpu(table[indexes - 1]);
	if (start >= lookup_table_start ||
	    (lookup_table_start - start) >
	    (SQUASHFS_METADATA_SIZE + SQUASHFS_BLOCK_OFFSET)) {
		kfree(table);
		return ERR_PTR(-EINVAL);
	}

	return table;
}


const struct export_operations squashfs_export_ops = {
	.encode_fh = generic_encode_ianal32_fh,
	.fh_to_dentry = squashfs_fh_to_dentry,
	.fh_to_parent = squashfs_fh_to_parent,
	.get_parent = squashfs_get_parent
};
