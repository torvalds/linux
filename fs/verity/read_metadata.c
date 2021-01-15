// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ioctl to read verity metadata
 *
 * Copyright 2021 Google LLC
 */

#include "fsverity_private.h"

#include <linux/uaccess.h>

/**
 * fsverity_ioctl_read_metadata() - read verity metadata from a file
 * @filp: file to read the metadata from
 * @uarg: user pointer to fsverity_read_metadata_arg
 *
 * Return: length read on success, 0 on EOF, -errno on failure
 */
int fsverity_ioctl_read_metadata(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	const struct fsverity_info *vi;
	struct fsverity_read_metadata_arg arg;
	int length;
	void __user *buf;

	vi = fsverity_get_info(inode);
	if (!vi)
		return -ENODATA; /* not a verity file */
	/*
	 * Note that we don't have to explicitly check that the file is open for
	 * reading, since verity files can only be opened for reading.
	 */

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (arg.__reserved)
		return -EINVAL;

	/* offset + length must not overflow. */
	if (arg.offset + arg.length < arg.offset)
		return -EINVAL;

	/* Ensure that the return value will fit in INT_MAX. */
	length = min_t(u64, arg.length, INT_MAX);

	buf = u64_to_user_ptr(arg.buf_ptr);

	switch (arg.metadata_type) {
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(fsverity_ioctl_read_metadata);
