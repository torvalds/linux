/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/cred.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include "overlayfs.h"

static struct file *ovl_open_realfile(const struct file *file)
{
	struct inode *inode = file_inode(file);
	struct inode *upperinode = ovl_inode_upper(inode);
	struct inode *realinode = upperinode ?: ovl_inode_lower(inode);
	struct file *realfile;
	const struct cred *old_cred;

	old_cred = ovl_override_creds(inode->i_sb);
	realfile = open_with_fake_path(&file->f_path, file->f_flags | O_NOATIME,
				       realinode, current_cred());
	revert_creds(old_cred);

	pr_debug("open(%p[%pD2/%c], 0%o) -> (%p, 0%o)\n",
		 file, file, upperinode ? 'u' : 'l', file->f_flags,
		 realfile, IS_ERR(realfile) ? 0 : realfile->f_flags);

	return realfile;
}

static int ovl_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	struct file *realfile;
	int err;

	err = ovl_open_maybe_copy_up(dentry, file->f_flags);
	if (err)
		return err;

	/* No longer need these flags, so don't pass them on to underlying fs */
	file->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	realfile = ovl_open_realfile(file);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	file->private_data = realfile;

	return 0;
}

static int ovl_release(struct inode *inode, struct file *file)
{
	fput(file->private_data);

	return 0;
}

static loff_t ovl_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *realinode = ovl_inode_real(file_inode(file));

	return generic_file_llseek_size(file, offset, whence,
					realinode->i_sb->s_maxbytes,
					i_size_read(realinode));
}

const struct file_operations ovl_file_operations = {
	.open		= ovl_open,
	.release	= ovl_release,
	.llseek		= ovl_llseek,
};
