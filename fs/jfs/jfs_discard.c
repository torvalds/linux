// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) Tino Reichardt, 2012
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/blkdev.h>

#include "jfs_incore.h"
#include "jfs_superblock.h"
#include "jfs_discard.h"
#include "jfs_dmap.h"
#include "jfs_debug.h"


/*
 * NAME:	jfs_issue_discard()
 *
 * FUNCTION:	TRIM the specified block range on device, if supported
 *
 * PARAMETERS:
 *	ip	- pointer to in-core inode
 *	blkno	- starting block number to be trimmed (0..N)
 *	nblocks	- number of blocks to be trimmed
 *
 * RETURN VALUES:
 *	none
 *
 * serialization: IREAD_LOCK(ipbmap) held on entry/exit;
 */
void jfs_issue_discard(struct inode *ip, u64 blkno, u64 nblocks)
{
	struct super_block *sb = ip->i_sb;
	int r = 0;

	r = sb_issue_discard(sb, blkno, nblocks, GFP_NOFS, 0);
	if (unlikely(r != 0)) {
		jfs_err("JFS: sb_issue_discard(%p, %llu, %llu, GFP_NOFS, 0) = %d => failed!",
			sb, (unsigned long long)blkno,
			(unsigned long long)nblocks, r);
	}

	jfs_info("JFS: sb_issue_discard(%p, %llu, %llu, GFP_NOFS, 0) = %d",
		sb, (unsigned long long)blkno,
		(unsigned long long)nblocks, r);

	return;
}

/*
 * NAME:	jfs_ioc_trim()
 *
 * FUNCTION:	attempt to discard (TRIM) all free blocks from the
 *              filesystem.
 *
 * PARAMETERS:
 *	ip	- pointer to in-core inode;
 *	range	- the range, given by user space
 *
 * RETURN VALUES:
 *	0	- success
 *	-EIO	- i/o error
 */
int jfs_ioc_trim(struct inode *ip, struct fstrim_range *range)
{
	struct inode *ipbmap = JFS_SBI(ip->i_sb)->ipbmap;
	struct bmap *bmp;
	struct super_block *sb = ipbmap->i_sb;
	int agno, agno_end;
	u64 start, end, minlen;
	u64 trimmed = 0;

	/**
	 * convert byte values to block size of filesystem:
	 * start:	First Byte to trim
	 * len:		number of Bytes to trim from start
	 * minlen:	minimum extent length in Bytes
	 */
	start = range->start >> sb->s_blocksize_bits;
	end = start + (range->len >> sb->s_blocksize_bits) - 1;
	minlen = range->minlen >> sb->s_blocksize_bits;
	if (minlen == 0)
		minlen = 1;

	down_read(&sb->s_umount);
	bmp = JFS_SBI(ip->i_sb)->bmap;

	if (minlen > bmp->db_agsize ||
	    start >= bmp->db_mapsize ||
	    range->len < sb->s_blocksize) {
		up_read(&sb->s_umount);
		return -EINVAL;
	}

	if (end >= bmp->db_mapsize)
		end = bmp->db_mapsize - 1;

	/**
	 * we trim all ag's within the range
	 */
	agno = BLKTOAG(start, JFS_SBI(ip->i_sb));
	agno_end = BLKTOAG(end, JFS_SBI(ip->i_sb));
	while (agno <= agno_end) {
		trimmed += dbDiscardAG(ip, agno, minlen);
		agno++;
	}

	up_read(&sb->s_umount);
	range->len = trimmed << sb->s_blocksize_bits;

	return 0;
}
