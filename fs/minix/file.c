/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include <linux/buffer_head.h>		/* for fsync_inode_buffers() */
#include "minix.h"

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the minix filesystem.
 */
int minix_sync_file(struct file *, struct dentry *, int);

const struct file_operations minix_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= minix_sync_file,
	.sendfile	= generic_file_sendfile,
};

const struct inode_operations minix_file_inode_operations = {
	.truncate	= minix_truncate,
	.getattr	= minix_getattr,
};

int minix_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	int err;

	err = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY))
		return err;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return err;
	
	err |= minix_sync_inode(inode);
	return err ? -EIO : 0;
}
