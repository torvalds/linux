/*
 *  linux/fs/hfs/mdb.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains functions for reading/writing the MDB.
 */

#include <linux/cdrom.h>
#include <linux/genhd.h>
#include <linux/nls.h>
#include <linux/slab.h>

#include "hfs_fs.h"
#include "btree.h"

/*================ File-local data types ================*/

/*
 * The HFS Master Directory Block (MDB).
 *
 * Also known as the Volume Information Block (VIB), this structure is
 * the HFS equivalent of a superblock.
 *
 * Reference: _Inside Macintosh: Files_ pages 2-59 through 2-62
 *
 * modified for HFS Extended
 */

static int hfs_get_last_session(struct super_block *sb,
				sector_t *start, sector_t *size)
{
	struct cdrom_multisession ms_info;
	struct cdrom_tocentry te;
	int res;

	/* default values */
	*start = 0;
	*size = sb->s_bdev->bd_inode->i_size >> 9;

	if (HFS_SB(sb)->session >= 0) {
		te.cdte_track = HFS_SB(sb)->session;
		te.cdte_format = CDROM_LBA;
		res = ioctl_by_bdev(sb->s_bdev, CDROMREADTOCENTRY, (unsigned long)&te);
		if (!res && (te.cdte_ctrl & CDROM_DATA_TRACK) == 4) {
			*start = (sector_t)te.cdte_addr.lba << 2;
			return 0;
		}
		printk(KERN_ERR "hfs: invalid session number or type of track\n");
		return -EINVAL;
	}
	ms_info.addr_format = CDROM_LBA;
	res = ioctl_by_bdev(sb->s_bdev, CDROMMULTISESSION, (unsigned long)&ms_info);
	if (!res && ms_info.xa_flag)
		*start = (sector_t)ms_info.addr.lba << 2;
	return 0;
}

/*
 * hfs_mdb_get()
 *
 * Build the in-core MDB for a filesystem, including
 * the B-trees and the volume bitmap.
 */
int hfs_mdb_get(struct super_block *sb)
{
	struct buffer_head *bh;
	struct hfs_mdb *mdb, *mdb2;
	unsigned int block;
	char *ptr;
	int off2, len, size, sect;
	sector_t part_start, part_size;
	loff_t off;
	__be16 attrib;

	/* set the device driver to 512-byte blocks */
	size = sb_min_blocksize(sb, HFS_SECTOR_SIZE);
	if (!size)
		return -EINVAL;

	if (hfs_get_last_session(sb, &part_start, &part_size))
		return -EINVAL;
	while (1) {
		/* See if this is an HFS filesystem */
		bh = sb_bread512(sb, part_start + HFS_MDB_BLK, mdb);
		if (!bh)
			goto out;

		if (mdb->drSigWord == cpu_to_be16(HFS_SUPER_MAGIC))
			break;
		brelse(bh);

		/* check for a partition block
		 * (should do this only for cdrom/loop though)
		 */
		if (hfs_part_find(sb, &part_start, &part_size))
			goto out;
	}

	HFS_SB(sb)->alloc_blksz = size = be32_to_cpu(mdb->drAlBlkSiz);
	if (!size || (size & (HFS_SECTOR_SIZE - 1))) {
		printk(KERN_ERR "hfs: bad allocation block size %d\n", size);
		goto out_bh;
	}

	size = min(HFS_SB(sb)->alloc_blksz, (u32)PAGE_SIZE);
	/* size must be a multiple of 512 */
	while (size & (size - 1))
		size -= HFS_SECTOR_SIZE;
	sect = be16_to_cpu(mdb->drAlBlSt) + part_start;
	/* align block size to first sector */
	while (sect & ((size - 1) >> HFS_SECTOR_SIZE_BITS))
		size >>= 1;
	/* align block size to weird alloc size */
	while (HFS_SB(sb)->alloc_blksz & (size - 1))
		size >>= 1;
	brelse(bh);
	if (!sb_set_blocksize(sb, size)) {
		printk(KERN_ERR "hfs: unable to set blocksize to %u\n", size);
		goto out;
	}

	bh = sb_bread512(sb, part_start + HFS_MDB_BLK, mdb);
	if (!bh)
		goto out;
	if (mdb->drSigWord != cpu_to_be16(HFS_SUPER_MAGIC))
		goto out_bh;

	HFS_SB(sb)->mdb_bh = bh;
	HFS_SB(sb)->mdb = mdb;

	/* These parameters are read from the MDB, and never written */
	HFS_SB(sb)->part_start = part_start;
	HFS_SB(sb)->fs_ablocks = be16_to_cpu(mdb->drNmAlBlks);
	HFS_SB(sb)->fs_div = HFS_SB(sb)->alloc_blksz >> sb->s_blocksize_bits;
	HFS_SB(sb)->clumpablks = be32_to_cpu(mdb->drClpSiz) /
				 HFS_SB(sb)->alloc_blksz;
	if (!HFS_SB(sb)->clumpablks)
		HFS_SB(sb)->clumpablks = 1;
	HFS_SB(sb)->fs_start = (be16_to_cpu(mdb->drAlBlSt) + part_start) >>
			       (sb->s_blocksize_bits - HFS_SECTOR_SIZE_BITS);

	/* These parameters are read from and written to the MDB */
	HFS_SB(sb)->free_ablocks = be16_to_cpu(mdb->drFreeBks);
	HFS_SB(sb)->next_id = be32_to_cpu(mdb->drNxtCNID);
	HFS_SB(sb)->root_files = be16_to_cpu(mdb->drNmFls);
	HFS_SB(sb)->root_dirs = be16_to_cpu(mdb->drNmRtDirs);
	HFS_SB(sb)->file_count = be32_to_cpu(mdb->drFilCnt);
	HFS_SB(sb)->folder_count = be32_to_cpu(mdb->drDirCnt);

	/* TRY to get the alternate (backup) MDB. */
	sect = part_start + part_size - 2;
	bh = sb_bread512(sb, sect, mdb2);
	if (bh) {
		if (mdb2->drSigWord == cpu_to_be16(HFS_SUPER_MAGIC)) {
			HFS_SB(sb)->alt_mdb_bh = bh;
			HFS_SB(sb)->alt_mdb = mdb2;
		} else
			brelse(bh);
	}

	if (!HFS_SB(sb)->alt_mdb) {
		printk(KERN_WARNING "hfs: unable to locate alternate MDB\n");
		printk(KERN_WARNING "hfs: continuing without an alternate MDB\n");
	}

	HFS_SB(sb)->bitmap = (__be32 *)__get_free_pages(GFP_KERNEL, PAGE_SIZE < 8192 ? 1 : 0);
	if (!HFS_SB(sb)->bitmap)
		goto out;

	/* read in the bitmap */
	block = be16_to_cpu(mdb->drVBMSt) + part_start;
	off = (loff_t)block << HFS_SECTOR_SIZE_BITS;
	size = (HFS_SB(sb)->fs_ablocks + 8) / 8;
	ptr = (u8 *)HFS_SB(sb)->bitmap;
	while (size) {
		bh = sb_bread(sb, off >> sb->s_blocksize_bits);
		if (!bh) {
			printk(KERN_ERR "hfs: unable to read volume bitmap\n");
			goto out;
		}
		off2 = off & (sb->s_blocksize - 1);
		len = min((int)sb->s_blocksize - off2, size);
		memcpy(ptr, bh->b_data + off2, len);
		brelse(bh);
		ptr += len;
		off += len;
		size -= len;
	}

	HFS_SB(sb)->ext_tree = hfs_btree_open(sb, HFS_EXT_CNID, hfs_ext_keycmp);
	if (!HFS_SB(sb)->ext_tree) {
		printk(KERN_ERR "hfs: unable to open extent tree\n");
		goto out;
	}
	HFS_SB(sb)->cat_tree = hfs_btree_open(sb, HFS_CAT_CNID, hfs_cat_keycmp);
	if (!HFS_SB(sb)->cat_tree) {
		printk(KERN_ERR "hfs: unable to open catalog tree\n");
		goto out;
	}

	attrib = mdb->drAtrb;
	if (!(attrib & cpu_to_be16(HFS_SB_ATTRIB_UNMNT))) {
		printk(KERN_WARNING "hfs: filesystem was not cleanly unmounted, "
			 "running fsck.hfs is recommended.  mounting read-only.\n");
		sb->s_flags |= MS_RDONLY;
	}
	if ((attrib & cpu_to_be16(HFS_SB_ATTRIB_SLOCK))) {
		printk(KERN_WARNING "hfs: filesystem is marked locked, mounting read-only.\n");
		sb->s_flags |= MS_RDONLY;
	}
	if (!(sb->s_flags & MS_RDONLY)) {
		/* Mark the volume uncleanly unmounted in case we crash */
		attrib &= cpu_to_be16(~HFS_SB_ATTRIB_UNMNT);
		attrib |= cpu_to_be16(HFS_SB_ATTRIB_INCNSTNT);
		mdb->drAtrb = attrib;
		be32_add_cpu(&mdb->drWrCnt, 1);
		mdb->drLsMod = hfs_mtime();

		mark_buffer_dirty(HFS_SB(sb)->mdb_bh);
		hfs_buffer_sync(HFS_SB(sb)->mdb_bh);
	}

	return 0;

out_bh:
	brelse(bh);
out:
	hfs_mdb_put(sb);
	return -EIO;
}

/*
 * hfs_mdb_commit()
 *
 * Description:
 *   This updates the MDB on disk (look also at hfs_write_super()).
 *   It does not check, if the superblock has been modified, or
 *   if the filesystem has been mounted read-only. It is mainly
 *   called by hfs_write_super() and hfs_btree_extend().
 * Input Variable(s):
 *   struct hfs_mdb *mdb: Pointer to the hfs MDB
 *   int backup;
 * Output Variable(s):
 *   NONE
 * Returns:
 *   void
 * Preconditions:
 *   'mdb' points to a "valid" (struct hfs_mdb).
 * Postconditions:
 *   The HFS MDB and on disk will be updated, by copying the possibly
 *   modified fields from the in memory MDB (in native byte order) to
 *   the disk block buffer.
 *   If 'backup' is non-zero then the alternate MDB is also written
 *   and the function doesn't return until it is actually on disk.
 */
void hfs_mdb_commit(struct super_block *sb)
{
	struct hfs_mdb *mdb = HFS_SB(sb)->mdb;

	if (test_and_clear_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags)) {
		/* These parameters may have been modified, so write them back */
		mdb->drLsMod = hfs_mtime();
		mdb->drFreeBks = cpu_to_be16(HFS_SB(sb)->free_ablocks);
		mdb->drNxtCNID = cpu_to_be32(HFS_SB(sb)->next_id);
		mdb->drNmFls = cpu_to_be16(HFS_SB(sb)->root_files);
		mdb->drNmRtDirs = cpu_to_be16(HFS_SB(sb)->root_dirs);
		mdb->drFilCnt = cpu_to_be32(HFS_SB(sb)->file_count);
		mdb->drDirCnt = cpu_to_be32(HFS_SB(sb)->folder_count);

		/* write MDB to disk */
		mark_buffer_dirty(HFS_SB(sb)->mdb_bh);
	}

	/* write the backup MDB, not returning until it is written.
	 * we only do this when either the catalog or extents overflow
	 * files grow. */
	if (test_and_clear_bit(HFS_FLG_ALT_MDB_DIRTY, &HFS_SB(sb)->flags) &&
	    HFS_SB(sb)->alt_mdb) {
		hfs_inode_write_fork(HFS_SB(sb)->ext_tree->inode, mdb->drXTExtRec,
				     &mdb->drXTFlSize, NULL);
		hfs_inode_write_fork(HFS_SB(sb)->cat_tree->inode, mdb->drCTExtRec,
				     &mdb->drCTFlSize, NULL);
		memcpy(HFS_SB(sb)->alt_mdb, HFS_SB(sb)->mdb, HFS_SECTOR_SIZE);
		HFS_SB(sb)->alt_mdb->drAtrb |= cpu_to_be16(HFS_SB_ATTRIB_UNMNT);
		HFS_SB(sb)->alt_mdb->drAtrb &= cpu_to_be16(~HFS_SB_ATTRIB_INCNSTNT);
		mark_buffer_dirty(HFS_SB(sb)->alt_mdb_bh);
		hfs_buffer_sync(HFS_SB(sb)->alt_mdb_bh);
	}

	if (test_and_clear_bit(HFS_FLG_BITMAP_DIRTY, &HFS_SB(sb)->flags)) {
		struct buffer_head *bh;
		sector_t block;
		char *ptr;
		int off, size, len;

		block = be16_to_cpu(HFS_SB(sb)->mdb->drVBMSt) + HFS_SB(sb)->part_start;
		off = (block << HFS_SECTOR_SIZE_BITS) & (sb->s_blocksize - 1);
		block >>= sb->s_blocksize_bits - HFS_SECTOR_SIZE_BITS;
		size = (HFS_SB(sb)->fs_ablocks + 7) / 8;
		ptr = (u8 *)HFS_SB(sb)->bitmap;
		while (size) {
			bh = sb_bread(sb, block);
			if (!bh) {
				printk(KERN_ERR "hfs: unable to read volume bitmap\n");
				break;
			}
			len = min((int)sb->s_blocksize - off, size);
			memcpy(bh->b_data + off, ptr, len);
			mark_buffer_dirty(bh);
			brelse(bh);
			block++;
			off = 0;
			ptr += len;
			size -= len;
		}
	}
}

void hfs_mdb_close(struct super_block *sb)
{
	/* update volume attributes */
	if (sb->s_flags & MS_RDONLY)
		return;
	HFS_SB(sb)->mdb->drAtrb |= cpu_to_be16(HFS_SB_ATTRIB_UNMNT);
	HFS_SB(sb)->mdb->drAtrb &= cpu_to_be16(~HFS_SB_ATTRIB_INCNSTNT);
	mark_buffer_dirty(HFS_SB(sb)->mdb_bh);
}

/*
 * hfs_mdb_put()
 *
 * Release the resources associated with the in-core MDB.  */
void hfs_mdb_put(struct super_block *sb)
{
	if (!HFS_SB(sb))
		return;
	/* free the B-trees */
	hfs_btree_close(HFS_SB(sb)->ext_tree);
	hfs_btree_close(HFS_SB(sb)->cat_tree);

	/* free the buffers holding the primary and alternate MDBs */
	brelse(HFS_SB(sb)->mdb_bh);
	brelse(HFS_SB(sb)->alt_mdb_bh);

	unload_nls(HFS_SB(sb)->nls_io);
	unload_nls(HFS_SB(sb)->nls_disk);

	free_pages((unsigned long)HFS_SB(sb)->bitmap, PAGE_SIZE < 8192 ? 1 : 0);
	kfree(HFS_SB(sb));
	sb->s_fs_info = NULL;
}
