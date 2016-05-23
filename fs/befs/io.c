/*
 * linux/fs/befs/io.c
 *
 * Copyright (C) 2001 Will Dyson <will_dyson@pobox.com
 *
 * Based on portions of file.c and inode.c 
 * by Makoto Kato (m_kato@ga2.so-net.ne.jp)
 *
 * Many thanks to Dominic Giampaolo, author of Practical File System
 * Design with the Be File System, for such a helpful book.
 *
 */

#include <linux/buffer_head.h>

#include "befs.h"
#include "io.h"

/*
 * Converts befs notion of disk addr to a disk offset and uses
 * linux kernel function sb_bread() to get the buffer containing
 * the offset. -Will Dyson
 *
 */

struct buffer_head *
befs_bread_iaddr(struct super_block *sb, befs_inode_addr iaddr)
{
	struct buffer_head *bh;
	befs_blocknr_t block = 0;
	struct befs_sb_info *befs_sb = BEFS_SB(sb);

	befs_debug(sb, "---> Enter %s "
		   "[%u, %hu, %hu]", __func__, iaddr.allocation_group,
		   iaddr.start, iaddr.len);

	if (iaddr.allocation_group > befs_sb->num_ags) {
		befs_error(sb, "BEFS: Invalid allocation group %u, max is %u",
			   iaddr.allocation_group, befs_sb->num_ags);
		goto error;
	}

	block = iaddr2blockno(sb, &iaddr);

	befs_debug(sb, "%s: offset = %lu", __func__, (unsigned long)block);

	bh = sb_bread(sb, block);

	if (bh == NULL) {
		befs_error(sb, "Failed to read block %lu",
			   (unsigned long)block);
		goto error;
	}

	befs_debug(sb, "<--- %s", __func__);
	return bh;

      error:
	befs_debug(sb, "<--- %s ERROR", __func__);
	return NULL;
}

struct buffer_head *
befs_bread(struct super_block *sb, befs_blocknr_t block)
{
	struct buffer_head *bh = NULL;

	befs_debug(sb, "---> Enter %s %lu", __func__, (unsigned long)block);

	bh = sb_bread(sb, block);

	if (bh == NULL) {
		befs_error(sb, "Failed to read block %lu",
			   (unsigned long)block);
		goto error;
	}

	befs_debug(sb, "<--- %s", __func__);

	return bh;

      error:
	befs_debug(sb, "<--- %s ERROR", __func__);
	return NULL;
}
