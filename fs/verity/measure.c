// SPDX-License-Identifier: GPL-2.0
/*
 * Ioctl to get a verity file's digest
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/bpf.h>
#include <linux/btf.h>
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

#ifdef CONFIG_BPF_SYSCALL

/* bpf kfuncs */
__bpf_kfunc_start_defs();

/**
 * bpf_get_fsverity_digest: read fsverity digest of file
 * @file: file to get digest from
 * @digest_ptr: (out) dynptr for struct fsverity_digest
 *
 * Read fsverity_digest of *file* into *digest_ptr*.
 *
 * Return: 0 on success, a negative value on error.
 */
__bpf_kfunc int bpf_get_fsverity_digest(struct file *file, struct bpf_dynptr_kern *digest_ptr)
{
	const struct inode *inode = file_inode(file);
	u32 dynptr_sz = __bpf_dynptr_size(digest_ptr);
	struct fsverity_digest *arg;
	const struct fsverity_info *vi;
	const struct fsverity_hash_alg *hash_alg;
	int out_digest_sz;

	if (dynptr_sz < sizeof(struct fsverity_digest))
		return -EINVAL;

	arg = __bpf_dynptr_data_rw(digest_ptr, dynptr_sz);
	if (!arg)
		return -EINVAL;

	if (!IS_ALIGNED((uintptr_t)arg, __alignof__(*arg)))
		return -EINVAL;

	vi = fsverity_get_info(inode);
	if (!vi)
		return -ENODATA; /* not a verity file */

	hash_alg = vi->tree_params.hash_alg;

	arg->digest_algorithm = hash_alg - fsverity_hash_algs;
	arg->digest_size = hash_alg->digest_size;

	out_digest_sz = dynptr_sz - sizeof(struct fsverity_digest);

	/* copy digest */
	memcpy(arg->digest, vi->file_digest,  min_t(int, hash_alg->digest_size, out_digest_sz));

	/* fill the extra buffer with zeros */
	if (out_digest_sz > hash_alg->digest_size)
		memset(arg->digest + arg->digest_size, 0, out_digest_sz - hash_alg->digest_size);

	return 0;
}

__bpf_kfunc_end_defs();

BTF_SET8_START(fsverity_set_ids)
BTF_ID_FLAGS(func, bpf_get_fsverity_digest, KF_TRUSTED_ARGS)
BTF_SET8_END(fsverity_set_ids)

static int bpf_get_fsverity_digest_filter(const struct bpf_prog *prog, u32 kfunc_id)
{
	if (!btf_id_set8_contains(&fsverity_set_ids, kfunc_id))
		return 0;

	/* Only allow to attach from LSM hooks, to avoid recursion */
	return prog->type != BPF_PROG_TYPE_LSM ? -EACCES : 0;
}

static const struct btf_kfunc_id_set bpf_fsverity_set = {
	.owner = THIS_MODULE,
	.set = &fsverity_set_ids,
	.filter = bpf_get_fsverity_digest_filter,
};

void __init fsverity_init_bpf(void)
{
	register_btf_kfunc_id_set(BPF_PROG_TYPE_LSM, &bpf_fsverity_set);
}

#endif /* CONFIG_BPF_SYSCALL */
