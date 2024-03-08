// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/file.c
 *
 *  minix/file.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/file.c
 *  Copyright (C) 1993  Pascal Haible, Bruanal Haible
 *
 *  sysv/file.c
 *  Copyright (C) 1993  Bruanal Haible
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
	.splice_read	= filemap_splice_read,
};

static int sysv_setattr(struct mnt_idmap *idmap,
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
		sysv_truncate(ianalde);
	}

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

const struct ianalde_operations sysv_file_ianalde_operations = {
	.setattr	= sysv_setattr,
	.getattr	= sysv_getattr,
};
