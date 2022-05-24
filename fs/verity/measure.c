// SPDX-License-Identifier: GPL-2.0
/*
 * Ioctl to get a verity file's digest
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/uaccess.h>

/**
 * fsverity_ioctl_measure() - get a verity file's digest
 * @filp: file to get digest of
 * @_uarg: user pointer to fsverity_digest
 *
 * Retrieve the file digest that the kernel is enforcing for reads from a verity
 * file.  See the "FS_IOC_MEASURE_VERITY" section of
 * Documentation/filesystems/fsverity.rst for the documentation.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_ioctl_measure(struct file *filp, void __user *_uarg)
{
	const struct inode *inode = file_inode(filp);
	struct fsverity_digest __user *uarg = _uarg;
	const struct fsverity_info *vi;
	const struct fsverity_hash_alg *hash_alg;
	struct fsverity_digest arg;

	vi = fsverity_get_info(inode);
	if (!vi)
		return -ENODATA; /* not a verity file */
	hash_alg = vi->tree_params.hash_alg;

	/*
	 * The user specifies the digest_size their buffer has space for; we can
	 * return the digest if it fits in the available space.  We write back
	 * the actual size, which may be shorter than the user-specified size.
	 */

	if (get_user(arg.digest_size, &uarg->digest_size))
		return -EFAULT;
	if (arg.digest_size < hash_alg->digest_size)
		return -EOVERFLOW;

	memset(&arg, 0, sizeof(arg));
	arg.digest_algorithm = hash_alg - fsverity_hash_algs;
	arg.digest_size = hash_alg->digest_size;

	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;

	if (copy_to_user(uarg->digest, vi->file_digest, hash_alg->digest_size))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(fsverity_ioctl_measure);

/**
 * fsverity_get_digest() - get a verity file's digest
 * @inode: inode to get digest of
 * @digest: (out) pointer to the digest
 * @alg: (out) pointer to the hash algorithm enumeration
 *
 * Return the file hash algorithm and digest of an fsverity protected file.
 * Assumption: before calling fsverity_get_digest(), the file must have been
 * opened.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_get_digest(struct inode *inode,
			u8 digest[FS_VERITY_MAX_DIGEST_SIZE],
			enum hash_algo *alg)
{
	const struct fsverity_info *vi;
	const struct fsverity_hash_alg *hash_alg;
	int i;

	vi = fsverity_get_info(inode);
	if (!vi)
		return -ENODATA; /* not a verity file */

	hash_alg = vi->tree_params.hash_alg;
	memset(digest, 0, FS_VERITY_MAX_DIGEST_SIZE);

	/* convert the verity hash algorithm name to a hash_algo_name enum */
	i = match_string(hash_algo_name, HASH_ALGO__LAST, hash_alg->name);
	if (i < 0)
		return -EINVAL;
	*alg = i;

	if (WARN_ON_ONCE(hash_alg->digest_size != hash_digest_size[*alg]))
		return -EINVAL;
	memcpy(digest, vi->file_digest, hash_alg->digest_size);

	pr_debug("file digest %s:%*phN\n", hash_algo_name[*alg],
		 hash_digest_size[*alg], digest);

	return 0;
}
