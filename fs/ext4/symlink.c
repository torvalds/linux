// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 symlink handling code
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include "ext4.h"
#include "xattr.h"

static const char *ext4_encrypted_get_link(struct dentry *dentry,
					   struct inode *inode,
					   struct delayed_call *done)
{
	struct buffer_head *bh = NULL;
	const void *caddr;
	unsigned int max_size;
	const char *paddr;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	if (ext4_inode_is_fast_symlink(inode)) {
		caddr = EXT4_I(inode)->i_data;
		max_size = sizeof(EXT4_I(inode)->i_data);
	} else {
		bh = ext4_bread(NULL, inode, 0, 0);
		if (IS_ERR(bh))
			return ERR_CAST(bh);
		if (!bh) {
			EXT4_ERROR_INODE(inode, "bad symlink.");
			return ERR_PTR(-EFSCORRUPTED);
		}
		caddr = bh->b_data;
		max_size = inode->i_sb->s_blocksize;
	}

	paddr = fscrypt_get_symlink(inode, caddr, max_size, done);
	brelse(bh);
	return paddr;
}

static int ext4_encrypted_symlink_getattr(struct user_namespace *mnt_userns,
					  const struct path *path,
					  struct kstat *stat, u32 request_mask,
					  unsigned int query_flags)
{
	ext4_getattr(mnt_userns, path, stat, request_mask, query_flags);

	return fscrypt_symlink_getattr(path, stat);
}

static void ext4_free_link(void *bh)
{
	brelse(bh);
}

static const char *ext4_get_link(struct dentry *dentry, struct inode *inode,
				 struct delayed_call *callback)
{
	struct buffer_head *bh;

	if (!dentry) {
		bh = ext4_getblk(NULL, inode, 0, EXT4_GET_BLOCKS_CACHED_NOWAIT);
		if (IS_ERR(bh))
			return ERR_CAST(bh);
		if (!bh || !ext4_buffer_uptodate(bh))
			return ERR_PTR(-ECHILD);
	} else {
		bh = ext4_bread(NULL, inode, 0, 0);
		if (IS_ERR(bh))
			return ERR_CAST(bh);
		if (!bh) {
			EXT4_ERROR_INODE(inode, "bad symlink.");
			return ERR_PTR(-EFSCORRUPTED);
		}
	}

	set_delayed_call(callback, ext4_free_link, bh);
	nd_terminate_link(bh->b_data, inode->i_size,
			  inode->i_sb->s_blocksize - 1);
	return bh->b_data;
}

const struct inode_operations ext4_encrypted_symlink_inode_operations = {
	.get_link	= ext4_encrypted_get_link,
	.setattr	= ext4_setattr,
	.getattr	= ext4_encrypted_symlink_getattr,
	.listxattr	= ext4_listxattr,
};

const struct inode_operations ext4_symlink_inode_operations = {
	.get_link	= ext4_get_link,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
};

const struct inode_operations ext4_fast_symlink_inode_operations = {
	.get_link	= simple_get_link,
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
};
