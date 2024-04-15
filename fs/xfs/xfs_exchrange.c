// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_exchrange.h"
#include <linux/fsnotify.h>

/*
 * Generic code for exchanging ranges of two files via XFS_IOC_EXCHANGE_RANGE.
 * This part deals with struct file objects and byte ranges and does not deal
 * with XFS-specific data structures such as xfs_inodes and block ranges.  This
 * separation may some day facilitate porting to another filesystem.
 *
 * The goal is to exchange fxr.length bytes starting at fxr.file1_offset in
 * file1 with the same number of bytes starting at fxr.file2_offset in file2.
 * Implementations must call xfs_exchange_range_prep to prepare the two
 * files prior to taking locks; and they must update the inode change and mod
 * times of both files as part of the metadata update.  The timestamp update
 * and freshness checks must be done atomically as part of the data exchange
 * operation to ensure correctness of the freshness check.
 * xfs_exchange_range_finish must be called after the operation completes
 * successfully but before locks are dropped.
 */

/* Verify that we have security clearance to perform this operation. */
static int
xfs_exchange_range_verify_area(
	struct xfs_exchrange	*fxr)
{
	int			ret;

	ret = remap_verify_area(fxr->file1, fxr->file1_offset, fxr->length,
			true);
	if (ret)
		return ret;

	return remap_verify_area(fxr->file2, fxr->file2_offset, fxr->length,
			true);
}

/*
 * Performs necessary checks before doing a range exchange, having stabilized
 * mutable inode attributes via i_rwsem.
 */
static inline int
xfs_exchange_range_checks(
	struct xfs_exchrange	*fxr,
	unsigned int		alloc_unit)
{
	struct inode		*inode1 = file_inode(fxr->file1);
	struct inode		*inode2 = file_inode(fxr->file2);
	uint64_t		allocmask = alloc_unit - 1;
	int64_t			test_len;
	uint64_t		blen;
	loff_t			size1, size2, tmp;
	int			error;

	/* Don't touch certain kinds of inodes */
	if (IS_IMMUTABLE(inode1) || IS_IMMUTABLE(inode2))
		return -EPERM;
	if (IS_SWAPFILE(inode1) || IS_SWAPFILE(inode2))
		return -ETXTBSY;

	size1 = i_size_read(inode1);
	size2 = i_size_read(inode2);

	/* Ranges cannot start after EOF. */
	if (fxr->file1_offset > size1 || fxr->file2_offset > size2)
		return -EINVAL;

	/*
	 * If the caller said to exchange to EOF, we set the length of the
	 * request large enough to cover everything to the end of both files.
	 */
	if (fxr->flags & XFS_EXCHANGE_RANGE_TO_EOF) {
		fxr->length = max_t(int64_t, size1 - fxr->file1_offset,
					     size2 - fxr->file2_offset);

		error = xfs_exchange_range_verify_area(fxr);
		if (error)
			return error;
	}

	/*
	 * The start of both ranges must be aligned to the file allocation
	 * unit.
	 */
	if (!IS_ALIGNED(fxr->file1_offset, alloc_unit) ||
	    !IS_ALIGNED(fxr->file2_offset, alloc_unit))
		return -EINVAL;

	/* Ensure offsets don't wrap. */
	if (check_add_overflow(fxr->file1_offset, fxr->length, &tmp) ||
	    check_add_overflow(fxr->file2_offset, fxr->length, &tmp))
		return -EINVAL;

	/*
	 * We require both ranges to end within EOF, unless we're exchanging
	 * to EOF.
	 */
	if (!(fxr->flags & XFS_EXCHANGE_RANGE_TO_EOF) &&
	    (fxr->file1_offset + fxr->length > size1 ||
	     fxr->file2_offset + fxr->length > size2))
		return -EINVAL;

	/*
	 * Make sure we don't hit any file size limits.  If we hit any size
	 * limits such that test_length was adjusted, we abort the whole
	 * operation.
	 */
	test_len = fxr->length;
	error = generic_write_check_limits(fxr->file2, fxr->file2_offset,
			&test_len);
	if (error)
		return error;
	error = generic_write_check_limits(fxr->file1, fxr->file1_offset,
			&test_len);
	if (error)
		return error;
	if (test_len != fxr->length)
		return -EINVAL;

	/*
	 * If the user wanted us to exchange up to the infile's EOF, round up
	 * to the next allocation unit boundary for this check.  Do the same
	 * for the outfile.
	 *
	 * Otherwise, reject the range length if it's not aligned to an
	 * allocation unit.
	 */
	if (fxr->file1_offset + fxr->length == size1)
		blen = ALIGN(size1, alloc_unit) - fxr->file1_offset;
	else if (fxr->file2_offset + fxr->length == size2)
		blen = ALIGN(size2, alloc_unit) - fxr->file2_offset;
	else if (!IS_ALIGNED(fxr->length, alloc_unit))
		return -EINVAL;
	else
		blen = fxr->length;

	/* Don't allow overlapped exchanges within the same file. */
	if (inode1 == inode2 &&
	    fxr->file2_offset + blen > fxr->file1_offset &&
	    fxr->file1_offset + blen > fxr->file2_offset)
		return -EINVAL;

	/*
	 * Ensure that we don't exchange a partial EOF block into the middle of
	 * another file.
	 */
	if ((fxr->length & allocmask) == 0)
		return 0;

	blen = fxr->length;
	if (fxr->file2_offset + blen < size2)
		blen &= ~allocmask;

	if (fxr->file1_offset + blen < size1)
		blen &= ~allocmask;

	return blen == fxr->length ? 0 : -EINVAL;
}

/*
 * Check that the two inodes are eligible for range exchanges, the ranges make
 * sense, and then flush all dirty data.  Caller must ensure that the inodes
 * have been locked against any other modifications.
 */
static inline int
xfs_exchange_range_prep(
	struct xfs_exchrange	*fxr,
	unsigned int		alloc_unit)
{
	struct inode		*inode1 = file_inode(fxr->file1);
	struct inode		*inode2 = file_inode(fxr->file2);
	bool			same_inode = (inode1 == inode2);
	int			error;

	/* Check that we don't violate system file offset limits. */
	error = xfs_exchange_range_checks(fxr, alloc_unit);
	if (error || fxr->length == 0)
		return error;

	/* Wait for the completion of any pending IOs on both files */
	inode_dio_wait(inode1);
	if (!same_inode)
		inode_dio_wait(inode2);

	error = filemap_write_and_wait_range(inode1->i_mapping,
			fxr->file1_offset,
			fxr->file1_offset + fxr->length - 1);
	if (error)
		return error;

	error = filemap_write_and_wait_range(inode2->i_mapping,
			fxr->file2_offset,
			fxr->file2_offset + fxr->length - 1);
	if (error)
		return error;

	/*
	 * If the files or inodes involved require synchronous writes, amend
	 * the request to force the filesystem to flush all data and metadata
	 * to disk after the operation completes.
	 */
	if (((fxr->file1->f_flags | fxr->file2->f_flags) & O_SYNC) ||
	    IS_SYNC(inode1) || IS_SYNC(inode2))
		fxr->flags |= XFS_EXCHANGE_RANGE_DSYNC;

	return 0;
}

/*
 * Finish a range exchange operation, if it was successful.  Caller must ensure
 * that the inodes are still locked against any other modifications.
 */
static inline int
xfs_exchange_range_finish(
	struct xfs_exchrange	*fxr)
{
	int			error;

	error = file_remove_privs(fxr->file1);
	if (error)
		return error;
	if (file_inode(fxr->file1) == file_inode(fxr->file2))
		return 0;

	return file_remove_privs(fxr->file2);
}

/* Exchange parts of two files. */
static int
xfs_exchange_range(
	struct xfs_exchrange	*fxr)
{
	struct inode		*inode1 = file_inode(fxr->file1);
	struct inode		*inode2 = file_inode(fxr->file2);
	int			ret;

	BUILD_BUG_ON(XFS_EXCHANGE_RANGE_ALL_FLAGS &
		     XFS_EXCHANGE_RANGE_PRIV_FLAGS);

	/* Both files must be on the same mount/filesystem. */
	if (fxr->file1->f_path.mnt != fxr->file2->f_path.mnt)
		return -EXDEV;

	if (fxr->flags & ~XFS_EXCHANGE_RANGE_ALL_FLAGS)
		return -EINVAL;

	/* Userspace requests only honored for regular files. */
	if (S_ISDIR(inode1->i_mode) || S_ISDIR(inode2->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode1->i_mode) || !S_ISREG(inode2->i_mode))
		return -EINVAL;

	/* Both files must be opened for read and write. */
	if (!(fxr->file1->f_mode & FMODE_READ) ||
	    !(fxr->file1->f_mode & FMODE_WRITE) ||
	    !(fxr->file2->f_mode & FMODE_READ) ||
	    !(fxr->file2->f_mode & FMODE_WRITE))
		return -EBADF;

	/* Neither file can be opened append-only. */
	if ((fxr->file1->f_flags & O_APPEND) ||
	    (fxr->file2->f_flags & O_APPEND))
		return -EBADF;

	/*
	 * If we're not exchanging to EOF, we can check the areas before
	 * stabilizing both files' i_size.
	 */
	if (!(fxr->flags & XFS_EXCHANGE_RANGE_TO_EOF)) {
		ret = xfs_exchange_range_verify_area(fxr);
		if (ret)
			return ret;
	}

	/* Update cmtime if the fd/inode don't forbid it. */
	if (!(fxr->file1->f_mode & FMODE_NOCMTIME) && !IS_NOCMTIME(inode1))
		fxr->flags |= __XFS_EXCHANGE_RANGE_UPD_CMTIME1;
	if (!(fxr->file2->f_mode & FMODE_NOCMTIME) && !IS_NOCMTIME(inode2))
		fxr->flags |= __XFS_EXCHANGE_RANGE_UPD_CMTIME2;

	file_start_write(fxr->file2);
	ret = -EOPNOTSUPP; /* XXX call out to lower level code */
	file_end_write(fxr->file2);
	if (ret)
		return ret;

	fsnotify_modify(fxr->file1);
	if (fxr->file2 != fxr->file1)
		fsnotify_modify(fxr->file2);
	return 0;
}

/* Collect exchange-range arguments from userspace. */
long
xfs_ioc_exchange_range(
	struct file			*file,
	struct xfs_exchange_range __user *argp)
{
	struct xfs_exchrange		fxr = {
		.file2			= file,
	};
	struct xfs_exchange_range	args;
	struct fd			file1;
	int				error;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;
	if (memchr_inv(&args.pad, 0, sizeof(args.pad)))
		return -EINVAL;
	if (args.flags & ~XFS_EXCHANGE_RANGE_ALL_FLAGS)
		return -EINVAL;

	fxr.file1_offset	= args.file1_offset;
	fxr.file2_offset	= args.file2_offset;
	fxr.length		= args.length;
	fxr.flags		= args.flags;

	file1 = fdget(args.file1_fd);
	if (!file1.file)
		return -EBADF;
	fxr.file1 = file1.file;

	error = xfs_exchange_range(&fxr);
	fdput(file1);
	return error;
}
