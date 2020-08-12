// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

/*
 * fs-verity integration into incfs
 *
 * Since incfs has its own merkle tree implementation, most of fs-verity code
 * is not needed. The key part that is needed is the signature check, since
 * that is based on the private /proc/sys/fs/verity/require_signatures value
 * and a private keyring. Thus the first change is to modify verity code to
 * export a version of fsverity_verify_signature.
 *
 * fs-verity integration then consists of the following modifications:
 *
 * 1. Add the (optional) verity signature to the incfs file format
 * 2. Add a pointer to the digest of the fs-verity descriptor struct to the
 *    data_file struct that incfs attaches to each file inode.
 * 3. Add the following ioclts:
 *  - FS_IOC_ENABLE_VERITY
 *  - FS_IOC_GETFLAGS
 *  - FS_IOC_MEASURE_VERITY
 * 4. When FS_IOC_ENABLE_VERITY is called on a non-verity file, the
 *    fs-verity descriptor struct is populated and digested. If it passes the
 *    signature check or the signature is NULL and
 *    fs.verity.require_signatures=0, then the S_VERITY flag is set and the
 *    xattr incfs.verity is set. If the signature is non-NULL, an
 *    INCFS_MD_VERITY_SIGNATURE is added to the backing file containing the
 *    signature.
 * 5. When a file with an incfs.verity xattr's inode is initialized, the
 *    inode’s S_VERITY flag is set.
 * 6. When a file with the S_VERITY flag set on its inode is opened, the
 *    data_file is checked for its verity digest. If the file doesn’t have a
 *    digest, the file’s digest is calculated as above, checked, and set, or the
 *    open is denied if it is not valid.
 * 7. FS_IOC_GETFLAGS simply returns the value of the S_VERITY flag
 * 8. FS_IOC_MEASURE_VERITY simply returns the cached digest
 * 9. The final complication is that if FS_IOC_ENABLE_VERITY is called on a file
 *    which doesn’t have a merkle tree, the merkle tree is calculated before the
 *    rest of the process is completed.
 */

#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <linux/fsverity.h>
#include <linux/mount.h>

#include "verity.h"

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"
#include "vfs.h"

#define FS_VERITY_MAX_SIGNATURE_SIZE	16128

static int incfs_get_root_hash(struct file *filp, u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);

	if (!df)
		return -EINVAL;

	memcpy(root_hash, df->df_hash_tree->root_hash,
	       df->df_hash_tree->alg->digest_size);

	return 0;
}

static int incfs_end_enable_verity(struct file *filp, u8 *sig, size_t sig_size)
{
	struct inode *inode = file_inode(filp);
	struct mem_range signature = {
		.data = sig,
		.len = sig_size,
	};
	struct data_file *df = get_incfs_data_file(filp);
	struct backing_file_context *bfc;
	int error;
	struct incfs_df_verity_signature *vs;
	loff_t offset;

	if (!df || !df->df_backing_file_context)
		return -EFSCORRUPTED;

	vs = kzalloc(sizeof(*vs), GFP_NOFS);
	if (!vs)
		return -ENOMEM;

	bfc = df->df_backing_file_context;
	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		goto out;

	error = incfs_write_verity_signature_to_backing_file(bfc, signature,
							     &offset);
	mutex_unlock(&bfc->bc_mutex);
	if (error)
		goto out;

	/*
	 * Set verity xattr so we can set S_VERITY without opening backing file
	 */
	error = vfs_setxattr(bfc->bc_file->f_path.dentry,
			     INCFS_XATTR_VERITY_NAME, NULL, 0, XATTR_CREATE);
	if (error) {
		pr_warn("incfs: error setting verity xattr: %d\n", error);
		goto out;
	}

	*vs = (struct incfs_df_verity_signature) {
		.size = signature.len,
		.offset = offset,
	};

	df->df_verity_signature = vs;
	vs = NULL;
	inode_set_flags(inode, S_VERITY, S_VERITY);

out:
	kfree(vs);
	return error;
}

static int incfs_compute_file_digest(struct incfs_hash_alg *alg,
				struct fsverity_descriptor *desc,
				u8 *digest)
{
	SHASH_DESC_ON_STACK(d, alg->shash);

	d->tfm = alg->shash;
	return crypto_shash_digest(d, (u8 *)desc, sizeof(*desc), digest);
}

static enum incfs_hash_tree_algorithm incfs_convert_fsverity_hash_alg(
								int hash_alg)
{
	switch (hash_alg) {
	case FS_VERITY_HASH_ALG_SHA256:
		return INCFS_HASH_TREE_SHA256;
	default:
		return -EINVAL;
	}
}

static struct mem_range incfs_get_verity_digest(struct inode *inode)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;
	struct mem_range verity_file_digest;

	if (!node) {
		pr_warn("Invalid inode\n");
		return range(NULL, 0);
	}

	df = node->n_file;

	/*
	 * Pairs with the cmpxchg_release() in incfs_set_verity_digest().
	 * I.e., another task may publish ->df_verity_file_digest concurrently,
	 * executing a RELEASE barrier.  We need to use smp_load_acquire() here
	 * to safely ACQUIRE the memory the other task published.
	 */
	verity_file_digest.data = smp_load_acquire(
					&df->df_verity_file_digest.data);
	verity_file_digest.len = df->df_verity_file_digest.len;
	return verity_file_digest;
}

static void incfs_set_verity_digest(struct inode *inode,
				     struct mem_range verity_file_digest)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;

	if (!node) {
		pr_warn("Invalid inode\n");
		kfree(verity_file_digest.data);
		return;
	}

	df = node->n_file;
	df->df_verity_file_digest.len = verity_file_digest.len;

	/*
	 * Multiple tasks may race to set ->df_verity_file_digest.data, so use
	 * cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * incfs_get_verity_digest().  I.e., here we publish
	 * ->df_verity_file_digest.data, with a RELEASE barrier so that other
	 * tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&df->df_verity_file_digest.data, NULL,
			    verity_file_digest.data) != NULL)
		/* Lost the race, so free the file_digest we allocated. */
		kfree(verity_file_digest.data);
}

/*
 * Calculate the digest of the fsverity_descriptor. The signature (if present)
 * is also checked.
 */
static struct mem_range incfs_calc_verity_digest_from_desc(
					const struct inode *inode,
					struct fsverity_descriptor *desc,
					u8 *signature, size_t sig_size)
{
	enum incfs_hash_tree_algorithm incfs_hash_alg;
	struct mem_range verity_file_digest;
	int err;
	struct incfs_hash_alg *hash_alg;

	incfs_hash_alg = incfs_convert_fsverity_hash_alg(desc->hash_algorithm);
	if (incfs_hash_alg < 0)
		return range(ERR_PTR(incfs_hash_alg), 0);

	hash_alg = incfs_get_hash_alg(incfs_hash_alg);
	if (IS_ERR(hash_alg))
		return range((u8 *)hash_alg, 0);

	verity_file_digest = range(kzalloc(hash_alg->digest_size, GFP_KERNEL),
				   hash_alg->digest_size);
	if (!verity_file_digest.data)
		return range(ERR_PTR(-ENOMEM), 0);

	err = incfs_compute_file_digest(hash_alg, desc,
					verity_file_digest.data);
	if (err) {
		pr_err("Error %d computing file digest", err);
		goto out;
	}
	pr_debug("Computed file digest: %s:%*phN\n",
		 hash_alg->name, (int) verity_file_digest.len,
		 verity_file_digest.data);

	err = __fsverity_verify_signature(inode, signature, sig_size,
					  verity_file_digest.data,
					  desc->hash_algorithm);
out:
	if (err) {
		kfree(verity_file_digest.data);
		verity_file_digest = range(ERR_PTR(err), 0);
	}
	return verity_file_digest;
}

static struct mem_range incfs_calc_verity_digest(
					struct inode *inode, struct file *filp,
					u8 *signature, size_t signature_size,
					int hash_algorithm)
{
	struct fsverity_descriptor *desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	int err;
	struct mem_range verity_file_digest;

	if (!desc)
		return range(ERR_PTR(-ENOMEM), 0);

	*desc = (struct fsverity_descriptor) {
		.version = 1,
		.hash_algorithm = hash_algorithm,
		.log_blocksize = ilog2(INCFS_DATA_FILE_BLOCK_SIZE),
		.data_size = cpu_to_le64(inode->i_size),
	};

	err = incfs_get_root_hash(filp, desc->root_hash);
	if (err)
		goto out;

	verity_file_digest = incfs_calc_verity_digest_from_desc(inode, desc,
						signature, signature_size);

out:
	kfree(desc);
	if (err)
		return range(ERR_PTR(err), 0);
	return verity_file_digest;
}

static int incfs_enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	struct data_file *df = get_incfs_data_file(filp);
	u8 *signature = NULL;
	struct mem_range verity_file_digest = range(NULL, 0);
	int err;

	if (!df)
		return -EFSCORRUPTED;

	err = mutex_lock_interruptible(&df->df_enable_verity);
	if (err)
		return err;

	if (IS_VERITY(inode)) {
		err = -EEXIST;
		goto out;
	}

	/* Get the signature if the user provided one */
	if (arg->sig_size) {
		signature = memdup_user(u64_to_user_ptr(arg->sig_ptr),
					arg->sig_size);
		if (IS_ERR(signature)) {
			err = PTR_ERR(signature);
			signature = NULL;
			goto out;
		}
	}

	verity_file_digest = incfs_calc_verity_digest(inode, filp, signature,
					arg->sig_size, arg->hash_algorithm);
	if (IS_ERR(verity_file_digest.data)) {
		err = PTR_ERR(verity_file_digest.data);
		verity_file_digest.data = NULL;
		goto out;
	}

	err = incfs_end_enable_verity(filp, signature, arg->sig_size);
	if (err)
		goto out;

	/* Successfully enabled verity */
	incfs_set_verity_digest(inode, verity_file_digest);
	verity_file_digest.data = NULL;
out:
	mutex_unlock(&df->df_enable_verity);
	kfree(signature);
	kfree(verity_file_digest.data);
	if (err)
		pr_err("%s failed with err %d\n", __func__, err);
	return err;
}

int incfs_ioctl_enable_verity(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_enable_arg arg;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (arg.version != 1)
		return -EINVAL;

	if (arg.__reserved1 ||
	    memchr_inv(arg.__reserved2, 0, sizeof(arg.__reserved2)))
		return -EINVAL;

	if (arg.hash_algorithm != FS_VERITY_HASH_ALG_SHA256)
		return -EINVAL;

	if (arg.block_size != PAGE_SIZE)
		return -EINVAL;

	if (arg.salt_size)
		return -EINVAL;

	if (arg.sig_size > FS_VERITY_MAX_SIGNATURE_SIZE)
		return -EMSGSIZE;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	return incfs_enable_verity(filp, &arg);
}

static u8 *incfs_get_verity_signature(struct file *filp, size_t *sig_size)
{
	struct data_file *df = get_incfs_data_file(filp);
	struct incfs_df_verity_signature *vs;
	u8 *signature;
	int res;

	if (!df || !df->df_backing_file_context)
		return ERR_PTR(-EFSCORRUPTED);

	vs = df->df_verity_signature;
	if (!vs) {
		*sig_size = 0;
		return NULL;
	}

	signature = kzalloc(vs->size, GFP_KERNEL);
	if (!signature)
		return ERR_PTR(-ENOMEM);

	res = incfs_kread(df->df_backing_file_context,
			  signature, vs->size, vs->offset);

	if (res < 0)
		goto err_out;

	if (res != vs->size) {
		res = -EINVAL;
		goto err_out;
	}

	*sig_size = vs->size;
	return signature;

err_out:
	kfree(signature);
	return ERR_PTR(res);
}

/* Ensure data_file->df_verity_file_digest is populated */
static int ensure_verity_info(struct inode *inode, struct file *filp)
{
	struct mem_range verity_file_digest;
	u8 *signature = NULL;
	size_t sig_size;
	int err = 0;

	/* See if this file's verity file digest is already cached */
	verity_file_digest = incfs_get_verity_digest(inode);
	if (verity_file_digest.data)
		return 0;

	signature = incfs_get_verity_signature(filp, &sig_size);
	if (IS_ERR(signature))
		return PTR_ERR(signature);

	verity_file_digest = incfs_calc_verity_digest(inode, filp, signature,
						     sig_size,
						     FS_VERITY_HASH_ALG_SHA256);
	if (IS_ERR(verity_file_digest.data)) {
		err = PTR_ERR(verity_file_digest.data);
		goto out;
	}

	incfs_set_verity_digest(inode, verity_file_digest);

out:
	kfree(signature);
	return err;
}

/**
 * incfs_fsverity_file_open() - prepare to open a file that may be
 * verity-enabled
 * @inode: the inode being opened
 * @filp: the struct file being set up
 *
 * When opening a verity file, set up data_file->df_verity_file_digest if not
 * already done. Note that incfs does not allow opening for writing, so there is
 * no need for that check.
 *
 * Return: 0 on success, -errno on failure
 */
int incfs_fsverity_file_open(struct inode *inode, struct file *filp)
{
	if (IS_VERITY(inode))
		return ensure_verity_info(inode, filp);

	return 0;
}
