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
#include "ext2.h"
#include "xip.h"

static inline int
__inode_direct_access(struct inode *inode, sector_t sector, unsigned long *data) {
	BUG_ON(!inode->i_sb->s_bdev->bd_disk->fops->direct_access);
	return inode->i_sb->s_bdev->bd_disk->fops
		->direct_access(inode->i_sb->s_bdev,sector,data);
}

int
ext2_clear_xip_target(struct inode *inode, int block) {
	sector_t sector = block*(PAGE_SIZE/512);
	unsigned long data;
	int rc;

	rc = __inode_direct_access(inode, sector, &data);
	if (rc)
		return rc;
	clear_page((void*)data);
	return 0;
}

void ext2_xip_verify_sb(struct super_block *sb)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);

	if ((sbi->s_mount_opt & EXT2_MOUNT_XIP)) {
		if ((sb->s_bdev == NULL) ||
			sb->s_bdev->bd_disk == NULL ||
			sb->s_bdev->bd_disk->fops == NULL ||
			sb->s_bdev->bd_disk->fops->direct_access == NULL) {
			sbi->s_mount_opt &= (~EXT2_MOUNT_XIP);
			ext2_warning(sb, __FUNCTION__,
				"ignoring xip option - not supported by bdev");
		}
	}
}

struct page*
ext2_get_xip_page(struct address_space *mapping, sector_t blockno,
		   int create)
{
	int rc;
	unsigned long data;
	struct buffer_head tmp;

	tmp.b_state = 0;
	tmp.b_blocknr = 0;
	rc = ext2_get_block(mapping->host, blockno/(PAGE_SIZE/512) , &tmp,
				create);
	if (rc)
		return ERR_PTR(rc);
	if (tmp.b_blocknr == 0) {
		/* SPARSE block */
		BUG_ON(create);
		return ERR_PTR(-ENODATA);
	}

	rc = __inode_direct_access
		(mapping->host,tmp.b_blocknr*(PAGE_SIZE/512) ,&data);
	if (rc)
		return ERR_PTR(rc);

	SetPageUptodate(virt_to_page(data));
	return virt_to_page(data);
}
