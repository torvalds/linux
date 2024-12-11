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
#include "xfs_quota.h"
#include "xfs_bmap_util.h"
#include "xfs_reflink.h"
#include "xfs_trace.h"
#include "xfs_exchrange.h"
#include "xfs_exchmaps.h"
#include "xfs_sb.h"
#include "xfs_icache.h"
#include "xfs_log.h"
#include "xfs_rtbitmap.h"
#include <linux/fsnotify.h>

/* Lock (and optionally join) two inodes for a file range exchange. */
void
xfs_exchrange_ilock(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	if (ip1 != ip2)
		xfs_lock_two_inodes(ip1, XFS_ILOCK_EXCL,
				    ip2, XFS_ILOCK_EXCL);
	else
		xfs_ilock(ip1, XFS_ILOCK_EXCL);
	if (tp) {
		xfs_trans_ijoin(tp, ip1, 0);
		if (ip2 != ip1)
			xfs_trans_ijoin(tp, ip2, 0);
	}

}

/* Unlock two inodes after a file range exchange operation. */
void
xfs_exchrange_iunlock(
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	if (ip2 != ip1)
		xfs_iunlock(ip2, XFS_ILOCK_EXCL);
	xfs_iunlock(ip1, XFS_ILOCK_EXCL);
}

/*
 * Estimate the resource requirements to exchange file contents between the two
 * files.  The caller is required to hold the IOLOCK and the MMAPLOCK and to
 * have flushed both inodes' pagecache and active direct-ios.
 */
int
xfs_exchrange_estimate(
	struct xfs_exchmaps_req	*req)
{
	int			error;

	xfs_exchrange_ilock(NULL, req->ip1, req->ip2);
	error = xfs_exchmaps_estimate(req);
	xfs_exchrange_iunlock(req->ip1, req->ip2);
	return error;
}

/*
 * Check that file2's metadata agree with the snapshot that we took for the
 * range commit request.
 *
 * This should be called after the filesystem has locked /all/ inode metadata
 * against modification.
 */
STATIC int
xfs_exchrange_check_freshness(
	const struct xfs_exchrange	*fxr,
	struct xfs_inode		*ip2)
{
	struct inode			*inode2 = VFS_I(ip2);
	struct timespec64		ctime = inode_get_ctime(inode2);
	struct timespec64		mtime = inode_get_mtime(inode2);

	trace_xfs_exchrange_freshness(fxr, ip2);

	/* Check that file2 hasn't otherwise been modified. */
	if (fxr->file2_ino != ip2->i_ino ||
	    fxr->file2_gen != inode2->i_generation ||
	    !timespec64_equal(&fxr->file2_ctime, &ctime) ||
	    !timespec64_equal(&fxr->file2_mtime, &mtime))
		return -EBUSY;

	return 0;
}

#define QRETRY_IP1	(0x1)
#define QRETRY_IP2	(0x2)

/*
 * Obtain a quota reservation to make sure we don't hit EDQUOT.  We can skip
 * this if quota enforcement is disabled or if both inodes' dquots are the
 * same.  The qretry structure must be initialized to zeroes before the first
 * call to this function.
 */
STATIC int
xfs_exchrange_reserve_quota(
	struct xfs_trans		*tp,
	const struct xfs_exchmaps_req	*req,
	unsigned int			*qretry)
{
	int64_t				ddelta, rdelta;
	int				ip1_error = 0;
	int				error;

	/*
	 * Don't bother with a quota reservation if we're not enforcing them
	 * or the two inodes have the same dquots.
	 */
	if (!XFS_IS_QUOTA_ON(tp->t_mountp) || req->ip1 == req->ip2 ||
	    (req->ip1->i_udquot == req->ip2->i_udquot &&
	     req->ip1->i_gdquot == req->ip2->i_gdquot &&
	     req->ip1->i_pdquot == req->ip2->i_pdquot))
		return 0;

	*qretry = 0;

	/*
	 * For each file, compute the net gain in the number of regular blocks
	 * that will be mapped into that file and reserve that much quota.  The
	 * quota counts must be able to absorb at least that much space.
	 */
	ddelta = req->ip2_bcount - req->ip1_bcount;
	rdelta = req->ip2_rtbcount - req->ip1_rtbcount;
	if (ddelta > 0 || rdelta > 0) {
		error = xfs_trans_reserve_quota_nblks(tp, req->ip1,
				ddelta > 0 ? ddelta : 0,
				rdelta > 0 ? rdelta : 0,
				false);
		if (error == -EDQUOT || error == -ENOSPC) {
			/*
			 * Save this error and see what happens if we try to
			 * reserve quota for ip2.  Then report both.
			 */
			*qretry |= QRETRY_IP1;
			ip1_error = error;
			error = 0;
		}
		if (error)
			return error;
	}
	if (ddelta < 0 || rdelta < 0) {
		error = xfs_trans_reserve_quota_nblks(tp, req->ip2,
				ddelta < 0 ? -ddelta : 0,
				rdelta < 0 ? -rdelta : 0,
				false);
		if (error == -EDQUOT || error == -ENOSPC)
			*qretry |= QRETRY_IP2;
		if (error)
			return error;
	}
	if (ip1_error)
		return ip1_error;

	/*
	 * For each file, forcibly reserve the gross gain in mapped blocks so
	 * that we don't trip over any quota block reservation assertions.
	 * We must reserve the gross gain because the quota code subtracts from
	 * bcount the number of blocks that we unmap; it does not add that
	 * quantity back to the quota block reservation.
	 */
	error = xfs_trans_reserve_quota_nblks(tp, req->ip1, req->ip1_bcount,
			req->ip1_rtbcount, true);
	if (error)
		return error;

	return xfs_trans_reserve_quota_nblks(tp, req->ip2, req->ip2_bcount,
			req->ip2_rtbcount, true);
}

/* Exchange the mappings (and hence the contents) of two files' forks. */
STATIC int
xfs_exchrange_mappings(
	const struct xfs_exchrange	*fxr,
	struct xfs_inode		*ip1,
	struct xfs_inode		*ip2)
{
	struct xfs_mount		*mp = ip1->i_mount;
	struct xfs_exchmaps_req		req = {
		.ip1			= ip1,
		.ip2			= ip2,
		.startoff1		= XFS_B_TO_FSBT(mp, fxr->file1_offset),
		.startoff2		= XFS_B_TO_FSBT(mp, fxr->file2_offset),
		.blockcount		= XFS_B_TO_FSB(mp, fxr->length),
	};
	struct xfs_trans		*tp;
	unsigned int			qretry;
	bool				retried = false;
	int				error;

	trace_xfs_exchrange_mappings(fxr, ip1, ip2);

	if (fxr->flags & XFS_EXCHANGE_RANGE_TO_EOF)
		req.flags |= XFS_EXCHMAPS_SET_SIZES;
	if (fxr->flags & XFS_EXCHANGE_RANGE_FILE1_WRITTEN)
		req.flags |= XFS_EXCHMAPS_INO1_WRITTEN;

	/*
	 * Round the request length up to the nearest file allocation unit.
	 * The prep function already checked that the request offsets and
	 * length in @fxr are safe to round up.
	 */
	if (xfs_inode_has_bigrtalloc(ip2))
		req.blockcount = xfs_blen_roundup_rtx(mp, req.blockcount);

	error = xfs_exchrange_estimate(&req);
	if (error)
		return error;

retry:
	/* Allocate the transaction, lock the inodes, and join them. */
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, req.resblks, 0,
			XFS_TRANS_RES_FDBLKS, &tp);
	if (error)
		return error;

	xfs_exchrange_ilock(tp, ip1, ip2);

	trace_xfs_exchrange_before(ip2, 2);
	trace_xfs_exchrange_before(ip1, 1);

	error = xfs_exchmaps_check_forks(mp, &req);
	if (error)
		goto out_trans_cancel;

	/*
	 * Reserve ourselves some quota if any of them are in enforcing mode.
	 * In theory we only need enough to satisfy the change in the number
	 * of blocks between the two ranges being remapped.
	 */
	error = xfs_exchrange_reserve_quota(tp, &req, &qretry);
	if ((error == -EDQUOT || error == -ENOSPC) && !retried) {
		xfs_trans_cancel(tp);
		xfs_exchrange_iunlock(ip1, ip2);
		if (qretry & QRETRY_IP1)
			xfs_blockgc_free_quota(ip1, 0);
		if (qretry & QRETRY_IP2)
			xfs_blockgc_free_quota(ip2, 0);
		retried = true;
		goto retry;
	}
	if (error)
		goto out_trans_cancel;

	/* If we got this far on a dry run, all parameters are ok. */
	if (fxr->flags & XFS_EXCHANGE_RANGE_DRY_RUN)
		goto out_trans_cancel;

	/* Update the mtime and ctime of both files. */
	if (fxr->flags & __XFS_EXCHANGE_RANGE_UPD_CMTIME1)
		xfs_trans_ichgtime(tp, ip1, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	if (fxr->flags & __XFS_EXCHANGE_RANGE_UPD_CMTIME2)
		xfs_trans_ichgtime(tp, ip2, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	xfs_exchange_mappings(tp, &req);

	/*
	 * Force the log to persist metadata updates if the caller or the
	 * administrator requires this.  The generic prep function already
	 * flushed the relevant parts of the page cache.
	 */
	if (xfs_has_wsync(mp) || (fxr->flags & XFS_EXCHANGE_RANGE_DSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);

	trace_xfs_exchrange_after(ip2, 2);
	trace_xfs_exchrange_after(ip1, 1);

	if (error)
		goto out_unlock;

	/*
	 * If the caller wanted us to exchange the contents of two complete
	 * files of unequal length, exchange the incore sizes now.  This should
	 * be safe because we flushed both files' page caches, exchanged all
	 * the mappings, and updated the ondisk sizes.
	 */
	if (fxr->flags & XFS_EXCHANGE_RANGE_TO_EOF) {
		loff_t	temp;

		temp = i_size_read(VFS_I(ip2));
		i_size_write(VFS_I(ip2), i_size_read(VFS_I(ip1)));
		i_size_write(VFS_I(ip1), temp);
	}

out_unlock:
	xfs_exchrange_iunlock(ip1, ip2);
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
	goto out_unlock;
}

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

/*
 * Check the alignment of an exchange request when the allocation unit size
 * isn't a power of two.  The generic file-level helpers use (fast)
 * bitmask-based alignment checks, but here we have to use slow long division.
 */
static int
xfs_exchrange_check_rtalign(
	const struct xfs_exchrange	*fxr,
	struct xfs_inode		*ip1,
	struct xfs_inode		*ip2,
	unsigned int			alloc_unit)
{
	uint64_t			length = fxr->length;
	uint64_t			blen;
	loff_t				size1, size2;

	size1 = i_size_read(VFS_I(ip1));
	size2 = i_size_read(VFS_I(ip2));

	/* The start of both ranges must be aligned to a rt extent. */
	if (!isaligned_64(fxr->file1_offset, alloc_unit) ||
	    !isaligned_64(fxr->file2_offset, alloc_unit))
		return -EINVAL;

	if (fxr->flags & XFS_EXCHANGE_RANGE_TO_EOF)
		length = max_t(int64_t, size1 - fxr->file1_offset,
					size2 - fxr->file2_offset);

	/*
	 * If the user wanted us to exchange up to the infile's EOF, round up
	 * to the next rt extent boundary for this check.  Do the same for the
	 * outfile.
	 *
	 * Otherwise, reject the range length if it's not rt extent aligned.
	 * We already confirmed the starting offsets' rt extent block
	 * alignment.
	 */
	if (fxr->file1_offset + length == size1)
		blen = roundup_64(size1, alloc_unit) - fxr->file1_offset;
	else if (fxr->file2_offset + length == size2)
		blen = roundup_64(size2, alloc_unit) - fxr->file2_offset;
	else if (!isaligned_64(length, alloc_unit))
		return -EINVAL;
	else
		blen = length;

	/* Don't allow overlapped exchanges within the same file. */
	if (ip1 == ip2 &&
	    fxr->file2_offset + blen > fxr->file1_offset &&
	    fxr->file1_offset + blen > fxr->file2_offset)
		return -EINVAL;

	/*
	 * Ensure that we don't exchange a partial EOF rt extent into the
	 * middle of another file.
	 */
	if (isaligned_64(length, alloc_unit))
		return 0;

	blen = length;
	if (fxr->file2_offset + length < size2)
		blen = rounddown_64(blen, alloc_unit);

	if (fxr->file1_offset + blen < size1)
		blen = rounddown_64(blen, alloc_unit);

	return blen == length ? 0 : -EINVAL;
}

/* Prepare two files to have their data exchanged. */
STATIC int
xfs_exchrange_prep(
	struct xfs_exchrange	*fxr,
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	struct xfs_mount	*mp = ip2->i_mount;
	unsigned int		alloc_unit = xfs_inode_alloc_unitsize(ip2);
	int			error;

	trace_xfs_exchrange_prep(fxr, ip1, ip2);

	/* Verify both files are either real-time or non-realtime */
	if (XFS_IS_REALTIME_INODE(ip1) != XFS_IS_REALTIME_INODE(ip2))
		return -EINVAL;

	/* Check non-power of two alignment issues, if necessary. */
	if (!is_power_of_2(alloc_unit)) {
		error = xfs_exchrange_check_rtalign(fxr, ip1, ip2, alloc_unit);
		if (error)
			return error;

		/*
		 * Do the generic file-level checks with the regular block
		 * alignment.
		 */
		alloc_unit = mp->m_sb.sb_blocksize;
	}

	error = xfs_exchange_range_prep(fxr, alloc_unit);
	if (error || fxr->length == 0)
		return error;

	if (fxr->flags & __XFS_EXCHANGE_RANGE_CHECK_FRESH2) {
		error = xfs_exchrange_check_freshness(fxr, ip2);
		if (error)
			return error;
	}

	/* Attach dquots to both inodes before changing block maps. */
	error = xfs_qm_dqattach(ip2);
	if (error)
		return error;
	error = xfs_qm_dqattach(ip1);
	if (error)
		return error;

	trace_xfs_exchrange_flush(fxr, ip1, ip2);

	/* Flush the relevant ranges of both files. */
	error = xfs_flush_unmap_range(ip2, fxr->file2_offset, fxr->length);
	if (error)
		return error;
	error = xfs_flush_unmap_range(ip1, fxr->file1_offset, fxr->length);
	if (error)
		return error;

	/*
	 * Cancel CoW fork preallocations for the ranges of both files.  The
	 * prep function should have flushed all the dirty data, so the only
	 * CoW mappings remaining should be speculative.
	 */
	if (xfs_inode_has_cow_data(ip1)) {
		error = xfs_reflink_cancel_cow_range(ip1, fxr->file1_offset,
				fxr->length, true);
		if (error)
			return error;
	}

	if (xfs_inode_has_cow_data(ip2)) {
		error = xfs_reflink_cancel_cow_range(ip2, fxr->file2_offset,
				fxr->length, true);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Exchange contents of files.  This is the binding between the generic
 * file-level concepts and the XFS inode-specific implementation.
 */
STATIC int
xfs_exchrange_contents(
	struct xfs_exchrange	*fxr)
{
	struct inode		*inode1 = file_inode(fxr->file1);
	struct inode		*inode2 = file_inode(fxr->file2);
	struct xfs_inode	*ip1 = XFS_I(inode1);
	struct xfs_inode	*ip2 = XFS_I(inode2);
	struct xfs_mount	*mp = ip1->i_mount;
	int			error;

	if (!xfs_has_exchange_range(mp))
		return -EOPNOTSUPP;

	if (fxr->flags & ~(XFS_EXCHANGE_RANGE_ALL_FLAGS |
			   XFS_EXCHANGE_RANGE_PRIV_FLAGS))
		return -EINVAL;

	if (xfs_is_shutdown(mp))
		return -EIO;

	/* Lock both files against IO */
	error = xfs_ilock2_io_mmap(ip1, ip2);
	if (error)
		goto out_err;

	/* Prepare and then exchange file contents. */
	error = xfs_exchrange_prep(fxr, ip1, ip2);
	if (error)
		goto out_unlock;

	error = xfs_exchrange_mappings(fxr, ip1, ip2);
	if (error)
		goto out_unlock;

	/*
	 * Finish the exchange by removing special file privileges like any
	 * other file write would do.  This may involve turning on support for
	 * logged xattrs if either file has security capabilities.
	 */
	error = xfs_exchange_range_finish(fxr);
	if (error)
		goto out_unlock;

out_unlock:
	xfs_iunlock2_io_mmap(ip1, ip2);
out_err:
	if (error)
		trace_xfs_exchrange_error(ip2, error, _RET_IP_);
	return error;
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

	if (fxr->flags & ~(XFS_EXCHANGE_RANGE_ALL_FLAGS |
			 __XFS_EXCHANGE_RANGE_CHECK_FRESH2))
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
	ret = xfs_exchrange_contents(fxr);
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

	CLASS(fd, file1)(args.file1_fd);
	if (fd_empty(file1))
		return -EBADF;
	fxr.file1 = fd_file(file1);

	return xfs_exchange_range(&fxr);
}

/* Opaque freshness blob for XFS_IOC_COMMIT_RANGE */
struct xfs_commit_range_fresh {
	xfs_fsid_t	fsid;		/* m_fixedfsid */
	__u64		file2_ino;	/* inode number */
	__s64		file2_mtime;	/* modification time */
	__s64		file2_ctime;	/* change time */
	__s32		file2_mtime_nsec; /* mod time, nsec */
	__s32		file2_ctime_nsec; /* change time, nsec */
	__u32		file2_gen;	/* inode generation */
	__u32		magic;		/* zero */
};
#define XCR_FRESH_MAGIC	0x444F524B	/* DORK */

/* Set up a commitrange operation by sampling file2's write-related attrs */
long
xfs_ioc_start_commit(
	struct file			*file,
	struct xfs_commit_range __user	*argp)
{
	struct xfs_commit_range		args = { };
	struct timespec64		ts;
	struct xfs_commit_range_fresh	*kern_f;
	struct xfs_commit_range_fresh	__user *user_f;
	struct inode			*inode2 = file_inode(file);
	struct xfs_inode		*ip2 = XFS_I(inode2);
	const unsigned int		lockflags = XFS_IOLOCK_SHARED |
						    XFS_MMAPLOCK_SHARED |
						    XFS_ILOCK_SHARED;

	BUILD_BUG_ON(sizeof(struct xfs_commit_range_fresh) !=
		     sizeof(args.file2_freshness));

	kern_f = (struct xfs_commit_range_fresh *)&args.file2_freshness;

	memcpy(&kern_f->fsid, ip2->i_mount->m_fixedfsid, sizeof(xfs_fsid_t));

	xfs_ilock(ip2, lockflags);
	ts = inode_get_ctime(inode2);
	kern_f->file2_ctime		= ts.tv_sec;
	kern_f->file2_ctime_nsec	= ts.tv_nsec;
	ts = inode_get_mtime(inode2);
	kern_f->file2_mtime		= ts.tv_sec;
	kern_f->file2_mtime_nsec	= ts.tv_nsec;
	kern_f->file2_ino		= ip2->i_ino;
	kern_f->file2_gen		= inode2->i_generation;
	kern_f->magic			= XCR_FRESH_MAGIC;
	xfs_iunlock(ip2, lockflags);

	user_f = (struct xfs_commit_range_fresh __user *)&argp->file2_freshness;
	if (copy_to_user(user_f, kern_f, sizeof(*kern_f)))
		return -EFAULT;

	return 0;
}

/*
 * Exchange file1 and file2 contents if file2 has not been written since the
 * start commit operation.
 */
long
xfs_ioc_commit_range(
	struct file			*file,
	struct xfs_commit_range __user	*argp)
{
	struct xfs_exchrange		fxr = {
		.file2			= file,
	};
	struct xfs_commit_range		args;
	struct xfs_commit_range_fresh	*kern_f;
	struct xfs_inode		*ip2 = XFS_I(file_inode(file));
	struct xfs_mount		*mp = ip2->i_mount;

	kern_f = (struct xfs_commit_range_fresh *)&args.file2_freshness;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;
	if (args.flags & ~XFS_EXCHANGE_RANGE_ALL_FLAGS)
		return -EINVAL;
	if (kern_f->magic != XCR_FRESH_MAGIC)
		return -EBUSY;
	if (memcmp(&kern_f->fsid, mp->m_fixedfsid, sizeof(xfs_fsid_t)))
		return -EBUSY;

	fxr.file1_offset	= args.file1_offset;
	fxr.file2_offset	= args.file2_offset;
	fxr.length		= args.length;
	fxr.flags		= args.flags | __XFS_EXCHANGE_RANGE_CHECK_FRESH2;
	fxr.file2_ino		= kern_f->file2_ino;
	fxr.file2_gen		= kern_f->file2_gen;
	fxr.file2_mtime.tv_sec	= kern_f->file2_mtime;
	fxr.file2_mtime.tv_nsec	= kern_f->file2_mtime_nsec;
	fxr.file2_ctime.tv_sec	= kern_f->file2_ctime;
	fxr.file2_ctime.tv_nsec	= kern_f->file2_ctime_nsec;

	CLASS(fd, file1)(args.file1_fd);
	if (fd_empty(file1))
		return -EBADF;
	fxr.file1 = fd_file(file1);

	return xfs_exchange_range(&fxr);
}
