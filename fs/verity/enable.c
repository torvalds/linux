// SPDX-License-Identifier: GPL-2.0
/*
 * Ioctl to enable verity on a file
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <crypto/hash.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

/*
 * Read a file data page for Merkle tree construction.  Do aggressive readahead,
 * since we're sequentially reading the entire file.
 */
static struct page *read_file_data_page(struct file *filp, pgoff_t index,
					struct file_ra_state *ra,
					unsigned long remaining_pages)
{
	struct page *page;

	page = find_get_page_flags(filp->f_mapping, index, FGP_ACCESSED);
	if (!page || !PageUptodate(page)) {
		if (page)
			put_page(page);
		else
			page_cache_sync_readahead(filp->f_mapping, ra, filp,
						  index, remaining_pages);
		page = read_mapping_page(filp->f_mapping, index, NULL);
		if (IS_ERR(page))
			return page;
	}
	if (PageReadahead(page))
		page_cache_async_readahead(filp->f_mapping, ra, filp, page,
					   index, remaining_pages);
	return page;
}

static int build_merkle_tree_level(struct file *filp, unsigned int level,
				   u64 num_blocks_to_hash,
				   const struct merkle_tree_params *params,
				   u8 *pending_hashes,
				   struct ahash_request *req)
{
	struct inode *inode = file_inode(filp);
	const struct fsverity_operations *vops = inode->i_sb->s_vop;
	struct file_ra_state ra = { 0 };
	unsigned int pending_size = 0;
	u64 dst_block_num;
	u64 i;
	int err;

	if (WARN_ON(params->block_size != PAGE_SIZE)) /* checked earlier too */
		return -EINVAL;

	if (level < params->num_levels) {
		dst_block_num = params->level_start[level];
	} else {
		if (WARN_ON(num_blocks_to_hash != 1))
			return -EINVAL;
		dst_block_num = 0; /* unused */
	}

	file_ra_state_init(&ra, filp->f_mapping);

	for (i = 0; i < num_blocks_to_hash; i++) {
		struct page *src_page;

		if ((pgoff_t)i % 10000 == 0 || i + 1 == num_blocks_to_hash)
			pr_debug("Hashing block %llu of %llu for level %u\n",
				 i + 1, num_blocks_to_hash, level);

		if (level == 0) {
			/* Leaf: hashing a data block */
			src_page = read_file_data_page(filp, i, &ra,
						       num_blocks_to_hash - i);
			if (IS_ERR(src_page)) {
				err = PTR_ERR(src_page);
				fsverity_err(inode,
					     "Error %d reading data page %llu",
					     err, i);
				return err;
			}
		} else {
			unsigned long num_ra_pages =
				min_t(unsigned long, num_blocks_to_hash - i,
				      inode->i_sb->s_bdi->io_pages);

			/* Non-leaf: hashing hash block from level below */
			src_page = vops->read_merkle_tree_page(inode,
					params->level_start[level - 1] + i,
					num_ra_pages);
			if (IS_ERR(src_page)) {
				err = PTR_ERR(src_page);
				fsverity_err(inode,
					     "Error %d reading Merkle tree page %llu",
					     err, params->level_start[level - 1] + i);
				return err;
			}
		}

		err = fsverity_hash_page(params, inode, req, src_page,
					 &pending_hashes[pending_size]);
		put_page(src_page);
		if (err)
			return err;
		pending_size += params->digest_size;

		if (level == params->num_levels) /* Root hash? */
			return 0;

		if (pending_size + params->digest_size > params->block_size ||
		    i + 1 == num_blocks_to_hash) {
			/* Flush the pending hash block */
			memset(&pending_hashes[pending_size], 0,
			       params->block_size - pending_size);
			err = vops->write_merkle_tree_block(inode,
					pending_hashes,
					dst_block_num,
					params->log_blocksize);
			if (err) {
				fsverity_err(inode,
					     "Error %d writing Merkle tree block %llu",
					     err, dst_block_num);
				return err;
			}
			dst_block_num++;
			pending_size = 0;
		}

		if (fatal_signal_pending(current))
			return -EINTR;
		cond_resched();
	}
	return 0;
}

/*
 * Build the Merkle tree for the given file using the given parameters, and
 * return the root hash in @root_hash.
 *
 * The tree is written to a filesystem-specific location as determined by the
 * ->write_merkle_tree_block() method.  However, the blocks that comprise the
 * tree are the same for all filesystems.
 */
static int build_merkle_tree(struct file *filp,
			     const struct merkle_tree_params *params,
			     u8 *root_hash)
{
	struct inode *inode = file_inode(filp);
	u8 *pending_hashes;
	struct ahash_request *req;
	u64 blocks;
	unsigned int level;
	int err = -ENOMEM;

	if (inode->i_size == 0) {
		/* Empty file is a special case; root hash is all 0's */
		memset(root_hash, 0, params->digest_size);
		return 0;
	}

	/* This allocation never fails, since it's mempool-backed. */
	req = fsverity_alloc_hash_request(params->hash_alg, GFP_KERNEL);

	pending_hashes = kmalloc(params->block_size, GFP_KERNEL);
	if (!pending_hashes)
		goto out;

	/*
	 * Build each level of the Merkle tree, starting at the leaf level
	 * (level 0) and ascending to the root node (level 'num_levels - 1').
	 * Then at the end (level 'num_levels'), calculate the root hash.
	 */
	blocks = ((u64)inode->i_size + params->block_size - 1) >>
		 params->log_blocksize;
	for (level = 0; level <= params->num_levels; level++) {
		err = build_merkle_tree_level(filp, level, blocks, params,
					      pending_hashes, req);
		if (err)
			goto out;
		blocks = (blocks + params->hashes_per_block - 1) >>
			 params->log_arity;
	}
	memcpy(root_hash, pending_hashes, params->digest_size);
	err = 0;
out:
	kfree(pending_hashes);
	fsverity_free_hash_request(params->hash_alg, req);
	return err;
}

static int enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	const struct fsverity_operations *vops = inode->i_sb->s_vop;
	struct merkle_tree_params params = { };
	struct fsverity_descriptor *desc;
	size_t desc_size = sizeof(*desc) + arg->sig_size;
	struct fsverity_info *vi;
	int err;

	/* Start initializing the fsverity_descriptor */
	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	desc->version = 1;
	desc->hash_algorithm = arg->hash_algorithm;
	desc->log_blocksize = ilog2(arg->block_size);

	/* Get the salt if the user provided one */
	if (arg->salt_size &&
	    copy_from_user(desc->salt, u64_to_user_ptr(arg->salt_ptr),
			   arg->salt_size)) {
		err = -EFAULT;
		goto out;
	}
	desc->salt_size = arg->salt_size;

	/* Get the signature if the user provided one */
	if (arg->sig_size &&
	    copy_from_user(desc->signature, u64_to_user_ptr(arg->sig_ptr),
			   arg->sig_size)) {
		err = -EFAULT;
		goto out;
	}
	desc->sig_size = cpu_to_le32(arg->sig_size);

	desc->data_size = cpu_to_le64(inode->i_size);

	/* Prepare the Merkle tree parameters */
	err = fsverity_init_merkle_tree_params(&params, inode,
					       arg->hash_algorithm,
					       desc->log_blocksize,
					       desc->salt, desc->salt_size);
	if (err)
		goto out;

	/*
	 * Start enabling verity on this file, serialized by the inode lock.
	 * Fail if verity is already enabled or is already being enabled.
	 */
	inode_lock(inode);
	if (IS_VERITY(inode))
		err = -EEXIST;
	else
		err = vops->begin_enable_verity(filp);
	inode_unlock(inode);
	if (err)
		goto out;

	/*
	 * Build the Merkle tree.  Don't hold the inode lock during this, since
	 * on huge files this may take a very long time and we don't want to
	 * force unrelated syscalls like chown() to block forever.  We don't
	 * need the inode lock here because deny_write_access() already prevents
	 * the file from being written to or truncated, and we still serialize
	 * ->begin_enable_verity() and ->end_enable_verity() using the inode
	 * lock and only allow one process to be here at a time on a given file.
	 */
	pr_debug("Building Merkle tree...\n");
	BUILD_BUG_ON(sizeof(desc->root_hash) < FS_VERITY_MAX_DIGEST_SIZE);
	err = build_merkle_tree(filp, &params, desc->root_hash);
	if (err) {
		fsverity_err(inode, "Error %d building Merkle tree", err);
		goto rollback;
	}
	pr_debug("Done building Merkle tree.  Root hash is %s:%*phN\n",
		 params.hash_alg->name, params.digest_size, desc->root_hash);

	/*
	 * Create the fsverity_info.  Don't bother trying to save work by
	 * reusing the merkle_tree_params from above.  Instead, just create the
	 * fsverity_info from the fsverity_descriptor as if it were just loaded
	 * from disk.  This is simpler, and it serves as an extra check that the
	 * metadata we're writing is valid before actually enabling verity.
	 */
	vi = fsverity_create_info(inode, desc, desc_size);
	if (IS_ERR(vi)) {
		err = PTR_ERR(vi);
		goto rollback;
	}

	if (arg->sig_size)
		pr_debug("Storing a %u-byte PKCS#7 signature alongside the file\n",
			 arg->sig_size);

	/*
	 * Tell the filesystem to finish enabling verity on the file.
	 * Serialized with ->begin_enable_verity() by the inode lock.
	 */
	inode_lock(inode);
	err = vops->end_enable_verity(filp, desc, desc_size, params.tree_size);
	inode_unlock(inode);
	if (err) {
		fsverity_err(inode, "%ps() failed with err %d",
			     vops->end_enable_verity, err);
		fsverity_free_info(vi);
	} else if (WARN_ON(!IS_VERITY(inode))) {
		err = -EINVAL;
		fsverity_free_info(vi);
	} else {
		/* Successfully enabled verity */

		/*
		 * Readers can start using ->i_verity_info immediately, so it
		 * can't be rolled back once set.  So don't set it until just
		 * after the filesystem has successfully enabled verity.
		 */
		fsverity_set_info(inode, vi);
	}
out:
	kfree(params.hashstate);
	kfree(desc);
	return err;

rollback:
	inode_lock(inode);
	(void)vops->end_enable_verity(filp, NULL, 0, params.tree_size);
	inode_unlock(inode);
	goto out;
}

/**
 * fsverity_ioctl_enable() - enable verity on a file
 * @filp: file to enable verity on
 * @uarg: user pointer to fsverity_enable_arg
 *
 * Enable fs-verity on a file.  See the "FS_IOC_ENABLE_VERITY" section of
 * Documentation/filesystems/fsverity.rst for the documentation.
 *
 * Return: 0 on success, -errno on failure
 */
int fsverity_ioctl_enable(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_enable_arg arg;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (arg.version != 1)
		return -EINVAL;

	if (arg.__reserved1 ||
	    memchr_inv(arg.__reserved2, 0, sizeof(arg.__reserved2)))
		return -EINVAL;

	if (arg.block_size != PAGE_SIZE)
		return -EINVAL;

	if (arg.salt_size > sizeof_field(struct fsverity_descriptor, salt))
		return -EMSGSIZE;

	if (arg.sig_size > FS_VERITY_MAX_SIGNATURE_SIZE)
		return -EMSGSIZE;

	/*
	 * Require a regular file with write access.  But the actual fd must
	 * still be readonly so that we can lock out all writers.  This is
	 * needed to guarantee that no writable fds exist to the file once it
	 * has verity enabled, and to stabilize the data being hashed.
	 */

	err = file_permission(filp, MAY_WRITE);
	if (err)
		return err;

	if (IS_APPEND(inode))
		return -EPERM;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	err = mnt_want_write_file(filp);
	if (err) /* -EROFS */
		return err;

	err = deny_write_access(filp);
	if (err) /* -ETXTBSY */
		goto out_drop_write;

	err = enable_verity(filp, &arg);
	if (err)
		goto out_allow_write_access;

	/*
	 * Some pages of the file may have been evicted from pagecache after
	 * being used in the Merkle tree construction, then read into pagecache
	 * again by another process reading from the file concurrently.  Since
	 * these pages didn't undergo verification against the file digest which
	 * fs-verity now claims to be enforcing, we have to wipe the pagecache
	 * to ensure that all future reads are verified.
	 */
	filemap_write_and_wait(inode->i_mapping);
	invalidate_inode_pages2(inode->i_mapping);

	/*
	 * allow_write_access() is needed to pair with deny_write_access().
	 * Regardless, the filesystem won't allow writing to verity files.
	 */
out_allow_write_access:
	allow_write_access(filp);
out_drop_write:
	mnt_drop_write_file(filp);
	return err;
}
EXPORT_SYMBOL_GPL(fsverity_ioctl_enable);
