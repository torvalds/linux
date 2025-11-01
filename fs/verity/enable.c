// SPDX-License-Identifier: GPL-2.0
/*
 * Ioctl to enable verity on a file
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

#include <linux/export.h>
#include <linux/mount.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

struct block_buffer {
	u32 filled;
	bool is_root_hash;
	u8 *data;
};

/* Hash a block, writing the result to the next level's pending block buffer. */
static int hash_one_block(const struct merkle_tree_params *params,
			  struct block_buffer *cur)
{
	struct block_buffer *next = cur + 1;

	/*
	 * Safety check to prevent a buffer overflow in case of a filesystem bug
	 * that allows the file size to change despite deny_write_access(), or a
	 * bug in the Merkle tree logic itself
	 */
	if (WARN_ON_ONCE(next->is_root_hash && next->filled != 0))
		return -EINVAL;

	/* Zero-pad the block if it's shorter than the block size. */
	memset(&cur->data[cur->filled], 0, params->block_size - cur->filled);

	fsverity_hash_block(params, cur->data, &next->data[next->filled]);
	next->filled += params->digest_size;
	cur->filled = 0;
	return 0;
}

static int write_merkle_tree_block(struct inode *inode, const u8 *buf,
				   unsigned long index,
				   const struct merkle_tree_params *params)
{
	u64 pos = (u64)index << params->log_blocksize;
	int err;

	err = inode->i_sb->s_vop->write_merkle_tree_block(inode, buf, pos,
							  params->block_size);
	if (err)
		fsverity_err(inode, "Error %d writing Merkle tree block %lu",
			     err, index);
	return err;
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
	const u64 data_size = inode->i_size;
	const int num_levels = params->num_levels;
	struct block_buffer _buffers[1 + FS_VERITY_MAX_LEVELS + 1] = {};
	struct block_buffer *buffers = &_buffers[1];
	unsigned long level_offset[FS_VERITY_MAX_LEVELS];
	int level;
	u64 offset;
	int err;

	if (data_size == 0) {
		/* Empty file is a special case; root hash is all 0's */
		memset(root_hash, 0, params->digest_size);
		return 0;
	}

	/*
	 * Allocate the block buffers.  Buffer "-1" is for data blocks.
	 * Buffers 0 <= level < num_levels are for the actual tree levels.
	 * Buffer 'num_levels' is for the root hash.
	 */
	for (level = -1; level < num_levels; level++) {
		buffers[level].data = kzalloc(params->block_size, GFP_KERNEL);
		if (!buffers[level].data) {
			err = -ENOMEM;
			goto out;
		}
	}
	buffers[num_levels].data = root_hash;
	buffers[num_levels].is_root_hash = true;

	BUILD_BUG_ON(sizeof(level_offset) != sizeof(params->level_start));
	memcpy(level_offset, params->level_start, sizeof(level_offset));

	/* Hash each data block, also hashing the tree blocks as they fill up */
	for (offset = 0; offset < data_size; offset += params->block_size) {
		ssize_t bytes_read;
		loff_t pos = offset;

		buffers[-1].filled = min_t(u64, params->block_size,
					   data_size - offset);
		bytes_read = __kernel_read(filp, buffers[-1].data,
					   buffers[-1].filled, &pos);
		if (bytes_read < 0) {
			err = bytes_read;
			fsverity_err(inode, "Error %d reading file data", err);
			goto out;
		}
		if (bytes_read != buffers[-1].filled) {
			err = -EINVAL;
			fsverity_err(inode, "Short read of file data");
			goto out;
		}
		err = hash_one_block(params, &buffers[-1]);
		if (err)
			goto out;
		for (level = 0; level < num_levels; level++) {
			if (buffers[level].filled + params->digest_size <=
			    params->block_size) {
				/* Next block at @level isn't full yet */
				break;
			}
			/* Next block at @level is full */

			err = hash_one_block(params, &buffers[level]);
			if (err)
				goto out;
			err = write_merkle_tree_block(inode,
						      buffers[level].data,
						      level_offset[level],
						      params);
			if (err)
				goto out;
			level_offset[level]++;
		}
		if (fatal_signal_pending(current)) {
			err = -EINTR;
			goto out;
		}
		cond_resched();
	}
	/* Finish all nonempty pending tree blocks. */
	for (level = 0; level < num_levels; level++) {
		if (buffers[level].filled != 0) {
			err = hash_one_block(params, &buffers[level]);
			if (err)
				goto out;
			err = write_merkle_tree_block(inode,
						      buffers[level].data,
						      level_offset[level],
						      params);
			if (err)
				goto out;
		}
	}
	/* The root hash was filled by the last call to hash_one_block(). */
	if (WARN_ON_ONCE(buffers[num_levels].filled != params->digest_size)) {
		err = -EINVAL;
		goto out;
	}
	err = 0;
out:
	for (level = -1; level < num_levels; level++)
		kfree(buffers[level].data);
	return err;
}

static int enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	const struct fsverity_operations *vops = inode->i_sb->s_vop;
	struct merkle_tree_params params = { };
	struct fsverity_descriptor *desc;
	size_t desc_size = struct_size(desc, signature, arg->sig_size);
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

	/* Get the builtin signature if the user provided one */
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
	BUILD_BUG_ON(sizeof(desc->root_hash) < FS_VERITY_MAX_DIGEST_SIZE);
	err = build_merkle_tree(filp, &params, desc->root_hash);
	if (err) {
		fsverity_err(inode, "Error %d building Merkle tree", err);
		goto rollback;
	}

	/*
	 * Create the fsverity_info.  Don't bother trying to save work by
	 * reusing the merkle_tree_params from above.  Instead, just create the
	 * fsverity_info from the fsverity_descriptor as if it were just loaded
	 * from disk.  This is simpler, and it serves as an extra check that the
	 * metadata we're writing is valid before actually enabling verity.
	 */
	vi = fsverity_create_info(inode, desc);
	if (IS_ERR(vi)) {
		err = PTR_ERR(vi);
		goto rollback;
	}

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
	} else if (WARN_ON_ONCE(!IS_VERITY(inode))) {
		err = -EINVAL;
		fsverity_free_info(vi);
	} else {
		/* Successfully enabled verity */

		/*
		 * Readers can start using the inode's verity info immediately,
		 * so it can't be rolled back once set.  So don't set it until
		 * just after the filesystem has successfully enabled verity.
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

	if (!is_power_of_2(arg.block_size))
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
	/*
	 * __kernel_read() is used while building the Merkle tree.  So, we can't
	 * allow file descriptors that were opened for ioctl access only, using
	 * the special nonstandard access mode 3.  O_RDONLY only, please!
	 */
	if (!(filp->f_mode & FMODE_READ))
		return -EBADF;

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

	/*
	 * We no longer drop the inode's pagecache after enabling verity.  This
	 * used to be done to try to avoid a race condition where pages could be
	 * evicted after being used in the Merkle tree construction, then
	 * re-instantiated by a concurrent read.  Such pages are unverified, and
	 * the backing storage could have filled them with different content, so
	 * they shouldn't be used to fulfill reads once verity is enabled.
	 *
	 * But, dropping the pagecache has a big performance impact, and it
	 * doesn't fully solve the race condition anyway.  So for those reasons,
	 * and also because this race condition isn't very important relatively
	 * speaking (especially for small-ish files, where the chance of a page
	 * being used, evicted, *and* re-instantiated all while enabling verity
	 * is quite small), we no longer drop the inode's pagecache.
	 */

	/*
	 * allow_write_access() is needed to pair with deny_write_access().
	 * Regardless, the filesystem won't allow writing to verity files.
	 */
	allow_write_access(filp);
out_drop_write:
	mnt_drop_write_file(filp);
	return err;
}
EXPORT_SYMBOL_GPL(fsverity_ioctl_enable);
