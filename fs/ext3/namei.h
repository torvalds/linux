/*  linux/fs/ext3/namei.h
 *
 * Copyright (C) 2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
*/

extern struct dentry *ext3_get_parent(struct dentry *child);

static inline struct buffer_head *ext3_dir_bread(handle_t *handle,
						 struct inode *inode,
						 int block, int create,
						 int *err)
{
	struct buffer_head *bh;

	bh = ext3_bread(handle, inode, block, create, err);

	if (!bh && !(*err)) {
		*err = -EIO;
		ext3_error(inode->i_sb, __func__,
			   "Directory hole detected on inode %lu\n",
			   inode->i_ino);
		return NULL;
	}
	return bh;
}
