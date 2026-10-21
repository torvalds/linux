// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS iomap support implementation.
 *
 * Written by Viacheslav Dubeyko.
 */

#include <linux/iomap.h>
#include <linux/pagemap.h>
#include "nilfs.h"
#include "mdt.h"
#include "iomap.h"

static int nilfs_iomap_begin(struct inode *inode, loff_t offset,
			     loff_t length, unsigned int flags,
			     struct iomap *iomap, struct iomap *srcmap)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_inode_info *ii = NILFS_I(inode);
	sector_t blkoff = offset >> inode->i_blkbits;
	loff_t iomap_offset = (loff_t)blkoff << inode->i_blkbits;
	unsigned int maxblocks;
	__u64 blknum = 0;
	int ret;

	/* Completely beyond EOF. Treat as hole */
	if (i_size_read(inode) <= offset) {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->offset = offset;
		iomap->length = length;
		return 0;
	}

	/* Clamp length if the requested range goes beyond i_size */
	if (offset + length > i_size_read(inode)) {
		loff_t i_size = i_size_read(inode);
		unsigned int blocksize = i_blocksize(inode);

		length = round_up(i_size, blocksize) - offset;
	}

	maxblocks = min_t(loff_t, length >> inode->i_blkbits, INT_MAX);
	if (maxblocks == 0)
		maxblocks = 1;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	ret = nilfs_bmap_lookup_contig(ii->i_bmap, blkoff, &blknum, maxblocks);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);

	if (ret == -ENOENT) {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->offset = iomap_offset;
		iomap->length = i_blocksize(inode);
		return 0;
	} else if (ret < 0)
		return ret;

	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = iomap_offset;
	iomap->length = (loff_t)ret << inode->i_blkbits;
	iomap->addr = (loff_t)blknum << inode->i_blkbits;
	iomap->type = IOMAP_MAPPED;
	iomap->flags = IOMAP_F_MERGED;

	return 0;
}

const struct iomap_ops nilfs_iomap_ops = {
	.iomap_begin = nilfs_iomap_begin,
};
