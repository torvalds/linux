/*
 *  linux/fs/sysv/file.c
 *
 *  minix/file.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/file.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/file.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  SystemV/Coherent regular file handling primitives
 */

#include "sysv.h"

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the coh filesystem.
 */
struct file_operations sysv_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= generic_file_write,
	.mmap		= generic_file_mmap,
	.fsync		= sysv_sync_file,
	.sendfile	= generic_file_sendfile,
};

struct inode_operations sysv_file_inode_operations = {
	.truncate	= sysv_truncate,
	.getattr	= sysv_getattr,
};

int sysv_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	int err;

	err = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY))
		return err;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return err;
	
	err |= sysv_sync_inode(inode);
	return err ? -EIO : 0;
}
