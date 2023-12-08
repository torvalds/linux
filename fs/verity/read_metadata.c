// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ioctl to read verity metadata
 *
 * Copyright 2021 Google LLC
 */

#include "fsverity_private.h"

#include <linux/backing-dev.h>
#include <linux/highmem.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

static int fsverity_read_merkle_tree(struct inode *inode,
				     const struct fsverity_info *vi,
				     void __user *buf, u64 offset, int length)
{
	const struct fsverity_operations *vops = inode->i_sb->s_vop;
	u64 end_offset;
	unsigned int offs_in_page;
	pgoff_t index, last_index;
	int retval = 0;
	int err = 0;

	end_offset = min(offset + length, vi->tree_params.tree_size);
	if (offset >= end_offset)
		return 0;
	offs_in_page = offset_in_page(offset);
	last_index = (end_offset - 1) >> PAGE_SHIFT;

	/*
	 * Iterate through each Merkle tree page in the requested range and copy
	 * the requested portion to userspace.  Note that the Merkle tree block
	 * size isn't important here, as we are returning a byte stream; i.e.,
	 * we can just work with pages even if the tree block size != PAGE_SIZE.
	 */
	for (index = offset >> PAGE_SHIFT; index <= last_index; index++) {
		unsigned long num_ra_pages =
			min_t(unsigned long, last_index - index + 1,
			      inode->i_sb->s_bdi->io_pages);
		unsigned int bytes_to_copy = min_t(u64, end_offset - offset,
						   PAGE_SIZE - offs_in_page);
		struct page *page;
		const void *virt;

		page = vops->read_merkle_tree_page(inode, index, num_ra_pages);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			fsverity_err(inode,
				     "Error %d reading Merkle tree page %lu",
				     err, index);
			break;
		}

		virt = kmap_local_page(page);
		if (copy_to_user(buf, virt + offs_in_page, bytes_to_copy)) {
			kunmap_local(virt);
			put_page(page);
			err = -EFAULT;
			break;
		}
		kunmap_local(virt);
		put_page(page);

		retval += bytes_to_copy;
		buf += bytes_to_copy;
		offset += bytes_to_copy;

		if (fatal_signal_pending(current))  {
			err = -EINTR;
			break;
		}
		cond_resched();
		offs_in_page = 0;
	}
	return retval ? retval : err;
}

/* Copy the requested portion of the buffer to userspace. */
static int fsverity_read_buffer(void __user *dst, u64 offset, int length,
				const void *src, size_t src_length)
{
	if (offset >= src_length)
		return 0;
	src += offset;
	src_length -= offset;

	length = min_t(size_t, length, src_length);

	if (copy_to_user(dst, src, length))
		return -EFAULT;

	return length;
}

static int fsverity_read_descriptor(struct inode *inode,
				    void __user *buf, u64 offset, int length)
{
	struct fsverity_descriptor *desc;
	size_t desc_size;
	int res;

	res = fsverity_get_descriptor(inode, &desc);
	if (res)
		return res;

	/* don't include the signature */
	desc_size = offsetof(struct fsverity_descriptor, signature);
	desc->sig_size = 0;

	res = fsverity_read_buffer(buf, offset, length, desc, desc_size);

	kfree(desc);
	return res;
}

static int fsverity_read_signature(struct inode *inode,
				   void __user *buf, u64 offset, int length)
{
	struct fsverity_descriptor *desc;
	int res;

	res = fsverity_get_descriptor(inode, &desc);
	if (res)
		return res;

	if (desc->sig_size == 0) {
		res = -ENODATA;
		goto out;
	}

	/*
	 * Include only the signature.  Note that fsverity_get_descriptor()
	 * already verified that sig_size is in-bounds.
	 */
	res = fsverity_read_buffer(buf, offset, length, desc->signature,
				   le32_to_cpu(desc->sig_size));
out:
	kfree(desc);
	return res;
}

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
	case FS_VERITY_METADATA_TYPE_MERKLE_TREE:
		return fsverity_read_merkle_tree(inode, vi, buf, arg.offset,
						 length);
	case FS_VERITY_METADATA_TYPE_DESCRIPTOR:
		return fsverity_read_descriptor(inode, buf, arg.offset, length);
	case FS_VERITY_METADATA_TYPE_SIGNATURE:
		return fsverity_read_signature(inode, buf, arg.offset, length);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(fsverity_ioctl_read_metadata);
