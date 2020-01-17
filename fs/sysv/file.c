// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/file.c
 *
 *  minix/file.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/file.c
 *  Copyright (C) 1993  Pascal Haible, Bruyes Haible
 *
 *  sysv/file.c
 *  Copyright (C) 1993  Bruyes Haible
 *
 *  SystemV/Coherent regular file handling primitives
 */

#include "sysv.h"

/*
 * We have mostly NULLs here: the current defaults are OK for
 * the coh filesystem.
 */
const struct file_operations sysv_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync,
	.splice_read	= generic_file_splice_read,
};

static int sysv_setattr(struct dentry *dentry, struct iattr *attr)
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
		sysv_truncate(iyesde);
	}

	setattr_copy(iyesde, attr);
	mark_iyesde_dirty(iyesde);
	return 0;
}

const struct iyesde_operations sysv_file_iyesde_operations = {
	.setattr	= sysv_setattr,
	.getattr	= sysv_getattr,
};
