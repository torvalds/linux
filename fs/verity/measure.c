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
 * @raw_digest: (out) the raw file digest
 * @alg: (out) the digest's algorithm, as a FS_VERITY_HASH_ALG_* value
 * @halg: (out) the digest's algorithm, as a HASH_ALGO_* value
 *
 * Retrieves the fsverity digest of the given file.  The file must have been
 * opened at least once since the inode was last loaded into the inode cache;
 * otherwise this function will not recognize when fsverity is enabled.
 *
 * The file's fsverity digest consists of @raw_digest in combination with either
 * @alg or @halg.  (The caller can choose which one of @alg or @halg to use.)
 *
 * IMPORTANT: Callers *must* make use of one of the two algorithm IDs, since
 * @raw_digest is meaningless without knowing which algorithm it uses!  fsverity
 * provides no security guarantee for users who ignore the algorithm ID, even if
 * they use the digest size (since algorithms can share the same digest size).
 *
 * Return: The size of the raw digest in bytes, or 0 if the file doesn't have
 *	   fsverity enabled.
 */
int fsverity_get_digest(struct inode *inode,
			u8 raw_digest[FS_VERITY_MAX_DIGEST_SIZE],
			u8 *alg, enum hash_algo *halg)
{
	const struct fsverity_info *vi;
	const struct fsverity_hash_alg *hash_alg;

	vi = fsverity_get_info(inode);
	if (!vi)
		return 0; /* not a verity file */

	hash_alg = vi->tree_params.hash_alg;
	memcpy(raw_digest, vi->file_digest, hash_alg->digest_size);
	if (alg)
		*alg = hash_alg - fsverity_hash_algs;
	if (halg)
		*halg = hash_alg->algo_id;
	return hash_alg->digest_size;
}
EXPORT_SYMBOL_GPL(fsverity_get_digest);
