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
#include <linux/ext2_fs_sb.h>
#include <linux/ext2_fs.h>
#include <linux/blkdev.h>
#include "ext2.h"
#include "xip.h"

static inline int
__inode_direct_access(struct inode *inode, sector_t block,
		      void **kaddr, unsigned long *pfn)
{
	struct block_device *bdev = inode->i_sb->s_bdev;
	const struct block_device_operations *ops = bdev->bd_disk->fops;
	sector_t sector;

	sector = block * (PAGE_SIZE / 512); /* ext2 block to bdev sector */

	BUG_ON(!ops->direct_access);
	return ops->direct_access(bdev, sector, kaddr, pfn);
}

static inline int
__ext2_get_block(struct inode *inode, pgoff_t pgoff, int create,
		   sector_t *result)
{
	struct buffer_head tmp;
	int rc;

	memset(&tmp, 0, sizeof(struct buffer_head));
	rc = ext2_get_block(inode, pgoff, &tmp, create);
	*result = tmp.b_blocknr;

	/* did we get a sparse block (hole in the file)? */
	if (!tmp.b_blocknr && !rc) {
		BUG_ON(create);
		rc = -ENODATA;
	}

	return rc;
}

int
ext2_clear_xip_target(struct inode *inode, sector_t block)
{
	void *kaddr;
	unsigned long pfn;
	int rc;

	rc = __inode_direct_access(inode, block, &kaddr, &pfn);
	if (!rc)
		clear_page(kaddr);
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
	int rc;
	sector_t block;

	/* first, retrieve the sector number */
	rc = __ext2_get_block(mapping->host, pgoff, create, &block);
	if (rc)
		return rc;

	/* retrieve address of the target data */
	rc = __inode_direct_access(mapping->host, block, kmem, pfn);
	return rc;
}
