/* fs/fat/nfs.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/exportfs.h>
#include "fat.h"

/*
 * a FAT file handle with fhtype 3 is
 *  0/  i_ino - for fast, reliable lookup if still in the cache
 *  1/  i_generation - to see if i_ino is still valid
 *          bit 0 == 0 iff directory
 *  2/  i_pos(8-39) - if ino has changed, but still in cache
 *  3/  i_pos(4-7)|i_logstart - to semi-verify inode found at i_pos
 *  4/  i_pos(0-3)|parent->i_logstart - maybe used to hunt for the file on disc
 *
 * Hack for NFSv2: Maximum FAT entry number is 28bits and maximum
 * i_pos is 40bits (blocknr(32) + dir offset(8)), so two 4bits
 * of i_logstart is used to store the directory entry offset.
 */

int
fat_encode_fh(struct inode *inode, __u32 *fh, int *lenp, struct inode *parent)
{
	int len = *lenp;
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	loff_t i_pos;

	if (len < 5) {
		*lenp = 5;
		return 255; /* no room */
	}

	i_pos = fat_i_pos_read(sbi, inode);
	*lenp = 5;
	fh[0] = inode->i_ino;
	fh[1] = inode->i_generation;
	fh[2] = i_pos >> 8;
	fh[3] = ((i_pos & 0xf0) << 24) | MSDOS_I(inode)->i_logstart;
	fh[4] = (i_pos & 0x0f) << 28;
	if (parent)
		fh[4] |= MSDOS_I(parent)->i_logstart;
	return 3;
}

static int fat_is_valid_fh(int fh_len, int fh_type)
{
	return ((fh_len >= 5) && (fh_type == 3));
}

/**
 * Map a NFS file handle to a corresponding dentry.
 * The dentry may or may not be connected to the filesystem root.
 */
struct dentry *fat_fh_to_dentry(struct super_block *sb, struct fid *fid,
				int fh_len, int fh_type)
{
	struct inode *inode = NULL;
	u32 *fh = fid->raw;
	loff_t i_pos;
	unsigned long i_ino;
	__u32 i_generation;
	int i_logstart;

	if (!fat_is_valid_fh(fh_len, fh_type))
		return NULL;

	i_ino = fh[0];
	i_generation = fh[1];
	i_logstart = fh[3] & 0x0fffffff;

	/* Try i_ino lookup first - fastest and most reliable */
	inode = ilookup(sb, i_ino);
	if (inode && (inode->i_generation != i_generation)) {
		iput(inode);
		inode = NULL;
	}
	if (!inode) {
		i_pos = (loff_t)fh[2] << 8;
		i_pos |= ((fh[3] >> 24) & 0xf0) | (fh[4] >> 28);

		/* try 2 - see if i_pos is in F-d-c
		 * require i_logstart to be the same
		 * Will fail if you truncate and then re-write
		 */

		inode = fat_iget(sb, i_pos);
		if (inode && MSDOS_I(inode)->i_logstart != i_logstart) {
			iput(inode);
			inode = NULL;
		}
	}

	/*
	 * For now, do nothing if the inode is not found.
	 *
	 * What we could do is:
	 *
	 *	- follow the file starting at fh[4], and record the ".." entry,
	 *	  and the name of the fh[2] entry.
	 *	- then follow the ".." file finding the next step up.
	 *
	 * This way we build a path to the root of the tree. If this works, we
	 * lookup the path and so get this inode into the cache.  Finally try
	 * the fat_iget lookup again.  If that fails, then we are totally out
	 * of luck.  But all that is for another day
	 */
	return d_obtain_alias(inode);
}

/*
 * Find the parent for a directory that is not currently connected to
 * the filesystem root.
 *
 * On entry, the caller holds child_dir->d_inode->i_mutex.
 */
struct dentry *fat_get_parent(struct dentry *child_dir)
{
	struct super_block *sb = child_dir->d_sb;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	loff_t i_pos;
	struct dentry *parent;
	struct inode *inode;
	int err;

	lock_super(sb);

	err = fat_get_dotdot_entry(child_dir->d_inode, &bh, &de, &i_pos);
	if (err) {
		parent = ERR_PTR(err);
		goto out;
	}
	inode = fat_build_inode(sb, de, i_pos);

	parent = d_obtain_alias(inode);
out:
	brelse(bh);
	unlock_super(sb);

	return parent;
}
