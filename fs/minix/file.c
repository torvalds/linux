// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  minix regular file handling primitives
 */

#include "minix.h"

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the minix filesystem.
 */
const struct file_operations minix_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync,
	.splice_read	= filemap_splice_read,
};

static int minix_setattr(struct mnt_idmap *idmap,
			 struct dentry *dentry, struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int error;

	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(ianalde)) {
		error = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(ianalde, attr->ia_size);
		minix_truncate(ianalde);
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

const struct ianalde_operations minix_file_ianalde_operations = {
	.setattr	= minix_setattr,
	.getattr	= minix_getattr,
};
