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
const struct file_operations sysv_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= simple_fsync,
	.splice_read	= generic_file_splice_read,
};

const struct inode_operations sysv_file_inode_operations = {
	.truncate	= sysv_truncate,
	.getattr	= sysv_getattr,
};
