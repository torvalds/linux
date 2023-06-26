// SPDX-License-Identifier: GPL-2.0
/*
 * Opening fs-verity files
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/mm.h>
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
	const struct fsverity_hash_alg *hash_alg;
	int err;
	u64 blocks;
	u64 blocks_in_level[FS_VERITY_MAX_LEVELS];
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

	/*
	 * fs/verity/ directly assumes that the Merkle tree block size is a
	 * power of 2 less than or equal to PAGE_SIZE.  Another restriction
	 * arises from the interaction between fs/verity/ and the filesystems
	 * themselves: filesystems expect to be able to verify a single
	 * filesystem block of data at a time.  Therefore, the Merkle tree block
	 * size must also be less than or equal to the filesystem block size.
	 *
	 * The above are the only hard limitations, so in theory the Merkle tree
	 * block size could be as small as twice the digest size.  However,
	 * that's not useful, and it would result in some unusually deep and
	 * large Merkle trees.  So we currently require that the Merkle tree
	 * block size be at least 1024 bytes.  That's small enough to test the
	 * sub-page block case on systems with 4K pages, but not too small.
	 */
	if (log_blocksize < 10 || log_blocksize > PAGE_SHIFT ||
	    log_blocksize > inode->i_blkbits) {
		fsverity_warn(inode, "Unsupported log_blocksize: %u",
			      log_blocksize);
		err = -EINVAL;
		goto out_err;
	}
	params->log_blocksize = log_blocksize;
	params->block_size = 1 << log_blocksize;
	params->log_blocks_per_page = PAGE_SHIFT - log_blocksize;
	params->blocks_per_page = 1 << params->log_blocks_per_page;

	if (WARN_ON_ONCE(!is_power_of_2(params->digest_size))) {
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
	params->log_digestsize = ilog2(params->digest_size);
	params->log_arity = log_blocksize - params->log_digestsize;
	params->hashes_per_block = 1 << params->log_arity;

	/*
	 * Compute the number of levels in the Merkle tree and create a map from
	 * level to the starting block of that level.  Level 'num_levels - 1' is
	 * the root and is stored first.  Level 0 is the level directly "above"
	 * the data blocks and is stored last.
	 */

	/* Compute number of levels and the number of blocks in each level */
	blocks = ((u64)inode->i_size + params->block_size - 1) >> log_blocksize;
	while (blocks > 1) {
		if (params->num_levels >= FS_VERITY_MAX_LEVELS) {
			fsverity_err(inode, "Too many levels in Merkle tree");
			err = -EFBIG;
			goto out_err;
		}
		blocks = (blocks + params->hashes_per_block - 1) >>
			 params->log_arity;
		blocks_in_level[params->num_levels++] = blocks;
	}

	/* Compute the starting block of each level */
	offset = 0;
	for (level = (int)params->num_levels - 1; level >= 0; level--) {
		params->level_start[level] = offset;
		offset += blocks_in_level[level];
	}

	/*
	 * With block_size != PAGE_SIZE, an in-memory bitmap will need to be
	 * allocated to track the "verified" status of hash blocks.  Don't allow
	 * this bitmap to get too large.  For now, limit it to 1 MiB, which
	 * limits the file size to about 4.4 TB with SHA-256 and 4K blocks.
	 *
	 * Together with the fact that the data, and thus also the Merkle tree,
	 * cannot have more than ULONG_MAX pages, this implies that hash block
	 * indices can always fit in an 'unsigned long'.  But to be safe, we
	 * explicitly check for that too.  Note, this is only for hash block
	 * indices; data block indices might not fit in an 'unsigned long'.
	 */
	if ((params->block_size != PAGE_SIZE && offset > 1 << 23) ||
	    offset > ULONG_MAX) {
		fsverity_err(inode, "Too many blocks in Merkle tree");
		err = -EFBIG;
		goto out_err;
	}

	params->tree_size = offset << log_blocksize;
	params->tree_pages = PAGE_ALIGN(params->tree_size) >> PAGE_SHIFT;
	return 0;

out_err:
	kfree(params->hashstate);
	memset(params, 0, sizeof(*params));
	return err;
}

/*
 * Compute the file digest by hashing the fsverity_descriptor excluding the
 * builtin signature and with the sig_size field set to 0.
 */
static int compute_file_digest(const struct fsverity_hash_alg *hash_alg,
			       struct fsverity_descriptor *desc,
			       u8 *file_digest)
{
	__le32 sig_size = desc->sig_size;
	int err;

	desc->sig_size = 0;
	err = fsverity_hash_buffer(hash_alg, desc, sizeof(*desc), file_digest);
	desc->sig_size = sig_size;

	return err;
}

/*
 * Create a new fsverity_info from the given fsverity_descriptor (with optional
 * appended builtin signature), and check the signature if present.  The
 * fsverity_descriptor must have already undergone basic validation.
 */
struct fsverity_info *fsverity_create_info(const struct inode *inode,
					   struct fsverity_descriptor *desc)
{
	struct fsverity_info *vi;
	int err;

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
		goto fail;
	}

	memcpy(vi->root_hash, desc->root_hash, vi->tree_params.digest_size);

	err = compute_file_digest(vi->tree_params.hash_alg, desc,
				  vi->file_digest);
	if (err) {
		fsverity_err(inode, "Error %d computing file digest", err);
		goto fail;
	}

	err = fsverity_verify_signature(vi, desc->signature,
					le32_to_cpu(desc->sig_size));
	if (err)
		goto fail;

	if (vi->tree_params.block_size != PAGE_SIZE) {
		/*
		 * When the Merkle tree block size and page size differ, we use
		 * a bitmap to keep track of which hash blocks have been
		 * verified.  This bitmap must contain one bit per hash block,
		 * including alignment to a page boundary at the end.
		 *
		 * Eventually, to support extremely large files in an efficient
		 * way, it might be necessary to make pages of this bitmap
		 * reclaimable.  But for now, simply allocating the whole bitmap
		 * is a simple solution that works well on the files on which
		 * fsverity is realistically used.  E.g., with SHA-256 and 4K
		 * blocks, a 100MB file only needs a 24-byte bitmap, and the
		 * bitmap for any file under 17GB fits in a 4K page.
		 */
		unsigned long num_bits =
			vi->tree_params.tree_pages <<
			vi->tree_params.log_blocks_per_page;

		vi->hash_block_verified = kvcalloc(BITS_TO_LONGS(num_bits),
						   sizeof(unsigned long),
						   GFP_KERNEL);
		if (!vi->hash_block_verified) {
			err = -ENOMEM;
			goto fail;
		}
		spin_lock_init(&vi->hash_page_init_lock);
	}

	return vi;

fail:
	fsverity_free_info(vi);
	return ERR_PTR(err);
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
	kvfree(vi->hash_block_verified);
	kmem_cache_free(fsverity_info_cachep, vi);
}

static bool validate_fsverity_descriptor(struct inode *inode,
					 const struct fsverity_descriptor *desc,
					 size_t desc_size)
{
	if (desc_size < sizeof(*desc)) {
		fsverity_err(inode, "Unrecognized descriptor size: %zu bytes",
			     desc_size);
		return false;
	}

	if (desc->version != 1) {
		fsverity_err(inode, "Unrecognized descriptor version: %u",
			     desc->version);
		return false;
	}

	if (memchr_inv(desc->__reserved, 0, sizeof(desc->__reserved))) {
		fsverity_err(inode, "Reserved bits set in descriptor");
		return false;
	}

	if (desc->salt_size > sizeof(desc->salt)) {
		fsverity_err(inode, "Invalid salt_size: %u", desc->salt_size);
		return false;
	}

	if (le64_to_cpu(desc->data_size) != inode->i_size) {
		fsverity_err(inode,
			     "Wrong data_size: %llu (desc) != %lld (inode)",
			     le64_to_cpu(desc->data_size), inode->i_size);
		return false;
	}

	if (le32_to_cpu(desc->sig_size) > desc_size - sizeof(*desc)) {
		fsverity_err(inode, "Signature overflows verity descriptor");
		return false;
	}

	return true;
}

/*
 * Read the inode's fsverity_descriptor (with optional appended builtin
 * signature) from the filesystem, and do basic validation of it.
 */
int fsverity_get_descriptor(struct inode *inode,
			    struct fsverity_descriptor **desc_ret)
{
	int res;
	struct fsverity_descriptor *desc;

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
		kfree(desc);
		return res;
	}

	if (!validate_fsverity_descriptor(inode, desc, res)) {
		kfree(desc);
		return -EINVAL;
	}

	*desc_ret = desc;
	return 0;
}

/* Ensure the inode has an ->i_verity_info */
static int ensure_verity_info(struct inode *inode)
{
	struct fsverity_info *vi = fsverity_get_info(inode);
	struct fsverity_descriptor *desc;
	int err;

	if (vi)
		return 0;

	err = fsverity_get_descriptor(inode, &desc);
	if (err)
		return err;

	vi = fsverity_create_info(inode, desc);
	if (IS_ERR(vi)) {
		err = PTR_ERR(vi);
		goto out_free_desc;
	}

	fsverity_set_info(inode, vi);
	err = 0;
out_free_desc:
	kfree(desc);
	return err;
}

int __fsverity_file_open(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		return -EPERM;
	return ensure_verity_info(inode);
}
EXPORT_SYMBOL_GPL(__fsverity_file_open);

int __fsverity_prepare_setattr(struct dentry *dentry, struct iattr *attr)
{
	if (attr->ia_valid & ATTR_SIZE)
		return -EPERM;
	return 0;
}
EXPORT_SYMBOL_GPL(__fsverity_prepare_setattr);

void __fsverity_cleanup_inode(struct inode *inode)
{
	fsverity_free_info(inode->i_verity_info);
	inode->i_verity_info = NULL;
}
EXPORT_SYMBOL_GPL(__fsverity_cleanup_inode);

int __init fsverity_init_info_cache(void)
{
	fsverity_info_cachep = KMEM_CACHE_USERCOPY(fsverity_info,
						   SLAB_RECLAIM_ACCOUNT,
						   file_digest);
	if (!fsverity_info_cachep)
		return -ENOMEM;
	return 0;
}

void __init fsverity_exit_info_cache(void)
{
	kmem_cache_destroy(fsverity_info_cachep);
	fsverity_info_cachep = NULL;
}
