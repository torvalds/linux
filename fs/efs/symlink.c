// SPDX-License-Identifier: GPL-2.0
/*
 * symlink.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include "efs.h"

static int efs_symlink_read_folio(struct file *file, struct folio *folio)
{
	char *link = folio_address(folio);
	struct buffer_head *bh;
	struct inode *inode = folio->mapping->host;
	efs_block_t size = inode->i_size;
	int err;
  
	err = -ENAMETOOLONG;
	if (size > 2 * EFS_BLOCKSIZE)
		goto fail;
  
	/* read first 512 bytes of link target */
	err = -EIO;
	bh = sb_bread(inode->i_sb, efs_bmap(inode, 0));
	if (!bh)
		goto fail;
	memcpy(link, bh->b_data, (size > EFS_BLOCKSIZE) ? EFS_BLOCKSIZE : size);
	brelse(bh);
	if (size > EFS_BLOCKSIZE) {
		bh = sb_bread(inode->i_sb, efs_bmap(inode, 1));
		if (!bh)
			goto fail;
		memcpy(link + EFS_BLOCKSIZE, bh->b_data, size - EFS_BLOCKSIZE);
		brelse(bh);
	}
	link[size] = '\0';
	err = 0;
fail:
	folio_end_read(folio, err == 0);
	return err;
}

const struct address_space_operations efs_symlink_aops = {
	.read_folio	= efs_symlink_read_folio
};
