// SPDX-License-Identifier: GPL-2.0
/*
 * fs/verity/open.c: opening fs-verity files
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/slab.h>

static struct kmem_cache *fsverity_info_cachep;

/**
 * fsverity_init_merkle_tree_params() - initialize Merkle tree parameters
 * @params: the parameters struct to initialize
 * @inode: the inode for which the Merkle tree is being built
 * @hash_algorithm: number of hash algorithm to use
 * @log_blocksize: log base 2 of block size to use
 * @salt: pointer to salt (optional)
 * @salt_size: size of salt, possibly 0
 *
 * Validate the hash algorithm and block size, then compute the tree topology
 * (num levels, num blocks in each level, etc.) and initialize @params.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_init_merkle_tree_params(struct merkle_tree_params *params,
				     const struct inode *inode,
				     unsigned int hash_algorithm,
				     unsigned int log_blocksize,
				     const u8 *salt, size_t salt_size)
{
	struct fsverity_hash_alg *hash_alg;
	int err;
	u64 blocks;
	u64 offset;
	int level;

	memset(params, 0, sizeof(*params));

	hash_alg = fsverity_get_hash_alg(inode, hash_algorithm);
	if (IS_ERR(hash_alg))
		return PTR_ERR(hash_alg);
	params->hash_alg = hash_alg;
	params->digest_size = hash_alg->digest_size;

	params->hashstate = fsverity_prepare_hash_state(hash_alg, salt,
							salt_size);
	if (IS_ERR(params->hashstate)) {
		err = PTR_ERR(params->hashstate);
		params->hashstate = NULL;
		fsverity_err(inode, "Error %d preparing hash state", err);
		goto out_err;
	}

	if (log_blocksize != PAGE_SHIFT) {
		fsverity_warn(inode, "Unsupported log_blocksize: %u",
			      log_blocksize);
		err = -EINVAL;
		goto out_err;
	}
	params->log_blocksize = log_blocksize;
	params->block_size = 1 << log_blocksize;

	if (WARN_ON(!is_power_of_2(params->digest_size))) {
		err = -EINVAL;
		goto out_err;
	}
	if (params->block_size < 2 * params->digest_size) {
		fsverity_warn(inode,
			      "Merkle tree block size (%u) too small for hash algorithm \"%s\"",
			      params->block_size, hash_alg->name);
		err = -EINVAL;
		goto out_err;
	}
	params->log_arity = params->log_blocksize - ilog2(params->digest_size);
	params->hashes_per_block = 1 << params->log_arity;

	pr_debug("Merkle tree uses %s with %u-byte blocks (%u hashes/block), salt=%*phN\n",
		 hash_alg->name, params->block_size, params->hashes_per_block,
		 (int)salt_size, salt);

	/*
	 * Compute the number of levels in the Merkle tree and create a map from
	 * level to the starting block of that level.  Level 'num_levels - 1' is
	 * the root and is stored first.  Level 0 is the level directly "above"
	 * the data blocks and is stored last.
	 */

	/* Compute number of levels and the number of blocks in each level */
	blocks = (inode->i_size + params->block_size - 1) >> log_blocksize;
	pr_debug("Data is %lld bytes (%llu blocks)\n", inode->i_size, blocks);
	while (blocks > 1) {
		if (params->num_levels >= FS_VERITY_MAX_LEVELS) {
			fsverity_err(inode, "Too many levels in Merkle tree");
			err = -EINVAL;
			goto out_err;
		}
		blocks = (blocks + params->hashes_per_block - 1) >>
			 params->log_arity;
		/* temporarily using level_start[] to store blocks in level */
		params->level_start[params->num_levels++] = blocks;
	}
	params->level0_blocks = params->level_start[0];

	/* Compute the starting block of each level */
	offset = 0;
	for (level = (int)params->num_levels - 1; level >= 0; level--) {
		blocks = params->level_start[level];
		params->level_start[level] = offset;
		pr_debug("Level %d is %llu blocks starting at index %llu\n",
			 level, blocks, offset);
		offset += blocks;
	}

	params->tree_size = offset << log_blocksize;
	return 0;

out_err:
	kfree(params->hashstate);
	memset(params, 0, sizeof(*params));
	return err;
}

/*
 * Compute the file measurement by hashing the fsverity_descriptor excluding the
 * signature and with the sig_size field set to 0.
 */
static int compute_file_measurement(struct fsverity_hash_alg *hash_alg,
				    struct fsverity_descriptor *desc,
				    u8 *measurement)
{
	__le32 sig_size = desc->sig_size;
	int err;

	desc->sig_size = 0;
	err = fsverity_hash_buffer(hash_alg, desc, sizeof(*desc), measurement);
	desc->sig_size = sig_size;

	return err;
}

/*
 * Validate the given fsverity_descriptor and create a new fsverity_info from
 * it.  The signature (if present) is also checked.
 */
struct fsverity_info *fsverity_create_info(const struct inode *inode,
					   void *_desc, size_t desc_size)
{
	struct fsverity_descriptor *desc = _desc;
	struct fsverity_info *vi;
	int err;

	if (desc_size < sizeof(*desc)) {
		fsverity_err(inode, "Unrecognized descriptor size: %zu bytes",
			     desc_size);
		return ERR_PTR(-EINVAL);
	}

	if (desc->version != 1) {
		fsverity_err(inode, "Unrecognized descriptor version: %u",
			     desc->version);
		return ERR_PTR(-EINVAL);
	}

	if (memchr_inv(desc->__reserved, 0, sizeof(desc->__reserved))) {
		fsverity_err(inode, "Reserved bits set in descriptor");
		return ERR_PTR(-EINVAL);
	}

	if (desc->salt_size > sizeof(desc->salt)) {
		fsverity_err(inode, "Invalid salt_size: %u", desc->salt_size);
		return ERR_PTR(-EINVAL);
	}

	if (le64_to_cpu(desc->data_size) != inode->i_size) {
		fsverity_err(inode,
			     "Wrong data_size: %llu (desc) != %lld (inode)",
			     le64_to_cpu(desc->data_size), inode->i_size);
		return ERR_PTR(-EINVAL);
	}

	vi = kmem_cache_zalloc(fsverity_info_cachep, GFP_KERNEL);
	if (!vi)
		return ERR_PTR(-ENOMEM);
	vi->inode = inode;

	err = fsverity_init_merkle_tree_params(&vi->tree_params, inode,
					       desc->hash_algorithm,
					       desc->log_blocksize,
					       desc->salt, desc->salt_size);
	if (err) {
		fsverity_err(inode,
			     "Error %d initializing Merkle tree parameters",
			     err);
		goto out;
	}

	memcpy(vi->root_hash, desc->root_hash, vi->tree_params.digest_size);

	err = compute_file_measurement(vi->tree_params.hash_alg, desc,
				       vi->measurement);
	if (err) {
		fsverity_err(inode, "Error %d computing file measurement", err);
		goto out;
	}
	pr_debug("Computed file measurement: %s:%*phN\n",
		 vi->tree_params.hash_alg->name,
		 vi->tree_params.digest_size, vi->measurement);

	err = fsverity_verify_signature(vi, desc, desc_size);
out:
	if (err) {
		fsverity_free_info(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

void fsverity_set_info(struct inode *inode, struct fsverity_info *vi)
{
	/*
	 * Multiple tasks may race to set ->i_verity_info, so use
	 * cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * fsverity_get_info().  I.e., here we publish ->i_verity_info with a
	 * RELEASE barrier so that other tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&inode->i_verity_info, NULL, vi) != NULL) {
		/* Lost the race, so free the fsverity_info we allocated. */
		fsverity_free_info(vi);
		/*
		 * Afterwards, the caller may access ->i_verity_info directly,
		 * so make sure to ACQUIRE the winning fsverity_info.
		 */
		(void)fsverity_get_info(inode);
	}
}

void fsverity_free_info(struct fsverity_info *vi)
{
	if (!vi)
		return;
	kfree(vi->tree_params.hashstate);
	kmem_cache_free(fsverity_info_cachep, vi);
}

/* Ensure the inode has an ->i_verity_info */
static int ensure_verity_info(struct inode *inode)
{
	struct fsverity_info *vi = fsverity_get_info(inode);
	struct fsverity_descriptor *desc;
	int res;

	if (vi)
		return 0;

	res = inode->i_sb->s_vop->get_verity_descriptor(inode, NULL, 0);
	if (res < 0) {
		fsverity_err(inode,
			     "Error %d getting verity descriptor size", res);
		return res;
	}
	if (res > FS_VERITY_MAX_DESCRIPTOR_SIZE) {
		fsverity_err(inode, "Verity descriptor is too large (%d bytes)",
			     res);
		return -EMSGSIZE;
	}
	desc = kmalloc(res, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	res = inode->i_sb->s_vop->get_verity_descriptor(inode, desc, res);
	if (res < 0) {
		fsverity_err(inode, "Error %d reading verity descriptor", res);
		goto out_free_desc;
	}

	vi = fsverity_create_info(inode, desc, res);
	if (IS_ERR(vi)) {
		res = PTR_ERR(vi);
		goto out_free_desc;
	}

	fsverity_set_info(inode, vi);
	res = 0;
out_free_desc:
	kfree(desc);
	return res;
}

/**
 * fsverity_file_open() - prepare to open a verity file
 * @inode: the inode being opened
 * @filp: the struct file being set up
 *
 * When opening a verity file, deny the open if it is for writing.  Otherwise,
 * set up the inode's ->i_verity_info if not already done.
 *
 * When combined with fscrypt, this must be called after fscrypt_file_open().
 * Otherwise, we won't have the key set up to decrypt the verity metadata.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_file_open(struct inode *inode, struct file *filp)
{
	if (!IS_VERITY(inode))
		return 0;

	if (filp->f_mode & FMODE_WRITE) {
		pr_debug("Denying opening verity file (ino %lu) for write\n",
			 inode->i_ino);
		return -EPERM;
	}

	return ensure_verity_info(inode);
}
EXPORT_SYMBOL_GPL(fsverity_file_open);

/**
 * fsverity_prepare_setattr() - prepare to change a verity inode's attributes
 * @dentry: dentry through which the inode is being changed
 * @attr: attributes to change
 *
 * Verity files are immutable, so deny truncates.  This isn't covered by the
 * open-time check because sys_truncate() takes a path, not a file descriptor.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_prepare_setattr(struct dentry *dentry, struct iattr *attr)
{
	if (IS_VERITY(d_inode(dentry)) && (attr->ia_valid & ATTR_SIZE)) {
		pr_debug("Denying truncate of verity file (ino %lu)\n",
			 d_inode(dentry)->i_ino);
		return -EPERM;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(fsverity_prepare_setattr);

/**
 * fsverity_cleanup_inode() - free the inode's verity info, if present
 * @inode: an inode being evicted
 *
 * Filesystems must call this on inode eviction to free ->i_verity_info.
 */
void fsverity_cleanup_inode(struct inode *inode)
{
	fsverity_free_info(inode->i_verity_info);
	inode->i_verity_info = NULL;
}
EXPORT_SYMBOL_GPL(fsverity_cleanup_inode);

int __init fsverity_init_info_cache(void)
{
	fsverity_info_cachep = KMEM_CACHE_USERCOPY(fsverity_info,
						   SLAB_RECLAIM_ACCOUNT,
						   measurement);
	if (!fsverity_info_cachep)
		return -ENOMEM;
	return 0;
}

void __init fsverity_exit_info_cache(void)
{
	kmem_cache_destroy(fsverity_info_cachep);
	fsverity_info_cachep = NULL;
}
