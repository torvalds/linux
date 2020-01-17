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
	.splice_read	= generic_file_splice_read,
};

static int minix_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int error;

	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(iyesde)) {
		error = iyesde_newsize_ok(iyesde, attr->ia_size);
		if (error)
			return error;

		truncate_setsize(iyesde, attr->ia_size);
		minix_truncate(iyesde);
	}

	setattr_copy(iyesde, attr);
	mark_iyesde_dirty(iyesde);
	return 0;
}

const struct iyesde_operations minix_file_iyesde_operations = {
	.setattr	= minix_setattr,
	.getattr	= minix_getattr,
};
