// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched/xacct.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

/*
 * Performs necessary checks before doing a clone.
 *
 * Can adjust amount of bytes to clone via @req_count argument.
 * Returns appropriate error code that caller should return or
 * zero in case the clone should be allowed.
 */
int generic_remap_checks(struct file *file_in, loff_t pos_in,
			 struct file *file_out, loff_t pos_out,
			 loff_t *req_count, unsigned int remap_flags)
{
	struct inode *inode_in = file_in->f_mapping->host;
	struct inode *inode_out = file_out->f_mapping->host;
	uint64_t count = *req_count;
	uint64_t bcount;
	loff_t size_in, size_out;
	loff_t bs = inode_out->i_sb->s_blocksize;
	int ret;

	/* The start of both ranges must be aligned to an fs block. */
	if (!IS_ALIGNED(pos_in, bs) || !IS_ALIGNED(pos_out, bs))
		return -EINVAL;

	/* Ensure offsets don't wrap. */
	if (pos_in + count < pos_in || pos_out + count < pos_out)
		return -EINVAL;

	size_in = i_size_read(inode_in);
	size_out = i_size_read(inode_out);

	/* Dedupe requires both ranges to be within EOF. */
	if ((remap_flags & REMAP_FILE_DEDUP) &&
	    (pos_in >= size_in || pos_in + count > size_in ||
	     pos_out >= size_out || pos_out + count > size_out))
		return -EINVAL;

	/* Ensure the infile range is within the infile. */
	if (pos_in >= size_in)
		return -EINVAL;
	count = min(count, size_in - (uint64_t)pos_in);

	ret = generic_write_check_limits(file_out, pos_out, &count);
	if (ret)
		return ret;

	/*
	 * If the user wanted us to link to the infile's EOF, round up to the
	 * next block boundary for this check.
	 *
	 * Otherwise, make sure the count is also block-aligned, having
	 * already confirmed the starting offsets' block alignment.
	 */
	if (pos_in + count == size_in) {
		bcount = ALIGN(size_in, bs) - pos_in;
	} else {
		if (!IS_ALIGNED(count, bs))
			count = ALIGN_DOWN(count, bs);
		bcount = count;
	}

	/* Don't allow overlapped cloning within the same file. */
	if (inode_in == inode_out &&
	    pos_out + bcount > pos_in &&
	    pos_out < pos_in + bcount)
		return -EINVAL;

	/*
	 * We shortened the request but the caller can't deal with that, so
	 * bounce the request back to userspace.
	 */
	if (*req_count != count && !(remap_flags & REMAP_FILE_CAN_SHORTEN))
		return -EINVAL;

	*req_count = count;
	return 0;
}
