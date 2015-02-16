/*
 *  linux/fs/ext2/xip.c
 *
 * Copyright (C) 2005 IBM Corporation
 * Author: Carsten Otte (cotte@de.ibm.com)
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include "ext2.h"
#include "xip.h"

static inline long __inode_direct_access(struct inode *inode, sector_t block,
				void **kaddr, unsigned long *pfn, long size)
{
	struct block_device *bdev = inode->i_sb->s_bdev;
	sector_t sector = block * (PAGE_SIZE / 512);
	return bdev_direct_access(bdev, sector, kaddr, pfn, size);
}

static inline int
__ext2_get_block(struct inode *inode, pgoff_t pgoff, int create,
		   sector_t *result)
{
	struct buffer_head tmp;
	int rc;

	memset(&tmp, 0, sizeof(struct buffer_head));
	tmp.b_size = 1 << inode->i_blkbits;
	rc = ext2_get_block(inode, pgoff, &tmp, create);
	*result = tmp.b_blocknr;

	/* did we get a sparse block (hole in the file)? */
	if (!tmp.b_blocknr && !rc) {
		BUG_ON(create);
		rc = -ENODATA;
	}

	return rc;
}

void ext2_xip_verify_sb(struct super_block *sb)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	if ((sbi->s_mount_opt & EXT2_MOUNT_XIP) &&
	    !sb->s_bdev->bd_disk->fops->direct_access) {
		sbi->s_mount_opt &= (~EXT2_MOUNT_XIP);
		ext2_msg(sb, KERN_WARNING,
			     "warning: ignoring xip option - "
			     "not supported by bdev");
	}
}

int ext2_get_xip_mem(struct address_space *mapping, pgoff_t pgoff, int create,
				void **kmem, unsigned long *pfn)
{
	long rc;
	sector_t block;

	/* first, retrieve the sector number */
	rc = __ext2_get_block(mapping->host, pgoff, create, &block);
	if (rc)
		return rc;

	/* retrieve address of the target data */
	rc = __inode_direct_access(mapping->host, block, kmem, pfn, PAGE_SIZE);
	return (rc < 0) ? rc : 0;
}
