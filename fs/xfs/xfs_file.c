// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_ioctl.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_icache.h"
#include "xfs_pnfs.h"
#include "xfs_iomap.h"
#include "xfs_reflink.h"
#include "xfs_file.h"

#include <linux/dax.h>
#include <linux/falloc.h>
#include <linux/backing-dev.h>
#include <linux/mman.h>
#include <linux/fadvise.h>
#include <linux/mount.h>

static const struct vm_operations_struct xfs_file_vm_ops;

/*
 * Decide if the given file range is aligned to the size of the fundamental
 * allocation unit for the file.
 */
bool
xfs_is_falloc_aligned(
	struct xfs_inode	*ip,
	loff_t			pos,
	long long int		len)
{
	unsigned int		alloc_unit = xfs_inode_alloc_unitsize(ip);

	if (!is_power_of_2(alloc_unit))
		return isaligned_64(pos, alloc_unit) &&
		       isaligned_64(len, alloc_unit);

	return !((pos | len) & (alloc_unit - 1));
}

/*
 * Fsync operations on directories are much simpler than on regular files,
 * as there is no file data to flush, and thus also no need for explicit
 * cache flush operations, and there are no non-transaction metadata updates
 * on directories either.
 */
STATIC int
xfs_dir_fsync(
	struct file		*file,
	loff_t			start,
	loff_t			end,
	int			datasync)
{
	struct xfs_inode	*ip = XFS_I(file->f_mapping->host);

	trace_xfs_dir_fsync(ip);
	return xfs_log_force_inode(ip);
}

static xfs_csn_t
xfs_fsync_seq(
	struct xfs_inode	*ip,
	bool			datasync)
{
	if (!xfs_ipincount(ip))
		return 0;
	if (datasync && !(ip->i_itemp->ili_fsync_fields & ~XFS_ILOG_TIMESTAMP))
		return 0;
	return ip->i_itemp->ili_commit_seq;
}

/*
 * All metadata updates are logged, which means that we just have to flush the
 * log up to the latest LSN that touched the inode.
 *
 * If we have concurrent fsync/fdatasync() calls, we need them to all block on
 * the log force before we clear the ili_fsync_fields field. This ensures that
 * we don't get a racing sync operation that does not wait for the metadata to
 * hit the journal before returning.  If we race with clearing ili_fsync_fields,
 * then all that will happen is the log force will do nothing as the lsn will
 * already be on disk.  We can't race with setting ili_fsync_fields because that
 * is done under XFS_ILOCK_EXCL, and that can't happen because we hold the lock
 * shared until after the ili_fsync_fields is cleared.
 */
static  int
xfs_fsync_flush_log(
	struct xfs_inode	*ip,
	bool			datasync,
	int			*log_flushed)
{
	int			error = 0;
	xfs_csn_t		seq;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	seq = xfs_fsync_seq(ip, datasync);
	if (seq) {
		error = xfs_log_force_seq(ip->i_mount, seq, XFS_LOG_SYNC,
					  log_flushed);

		spin_lock(&ip->i_itemp->ili_lock);
		ip->i_itemp->ili_fsync_fields = 0;
		spin_unlock(&ip->i_itemp->ili_lock);
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}

STATIC int
xfs_file_fsync(
	struct file		*file,
	loff_t			start,
	loff_t			end,
	int			datasync)
{
	struct xfs_inode	*ip = XFS_I(file->f_mapping->host);
	struct xfs_mount	*mp = ip->i_mount;
	int			error, err2;
	int			log_flushed = 0;

	trace_xfs_file_fsync(ip);

	error = file_write_and_wait_range(file, start, end);
	if (error)
		return error;

	if (xfs_is_shutdown(mp))
		return -EIO;

	xfs_iflags_clear(ip, XFS_ITRUNCATED);

	/*
	 * If we have an RT and/or log subvolume we need to make sure to flush
	 * the write cache the device used for file data first.  This is to
	 * ensure newly written file data make it to disk before logging the new
	 * inode size in case of an extending write.
	 */
	if (XFS_IS_REALTIME_INODE(ip))
		error = blkdev_issue_flush(mp->m_rtdev_targp->bt_bdev);
	else if (mp->m_logdev_targp != mp->m_ddev_targp)
		error = blkdev_issue_flush(mp->m_ddev_targp->bt_bdev);

	/*
	 * Any inode that has dirty modifications in the log is pinned.  The
	 * racy check here for a pinned inode will not catch modifications
	 * that happen concurrently to the fsync call, but fsync semantics
	 * only require to sync previously completed I/O.
	 */
	if (xfs_ipincount(ip)) {
		err2 = xfs_fsync_flush_log(ip, datasync, &log_flushed);
		if (err2 && !error)
			error = err2;
	}

	/*
	 * If we only have a single device, and the log force about was
	 * a no-op we might have to flush the data device cache here.
	 * This can only happen for fdatasync/O_DSYNC if we were overwriting
	 * an already allocated file and thus do not have any metadata to
	 * commit.
	 */
	if (!log_flushed && !XFS_IS_REALTIME_INODE(ip) &&
	    mp->m_logdev_targp == mp->m_ddev_targp) {
		err2 = blkdev_issue_flush(mp->m_ddev_targp->bt_bdev);
		if (err2 && !error)
			error = err2;
	}

	return error;
}

static int
xfs_ilock_iocb(
	struct kiocb		*iocb,
	unsigned int		lock_mode)
{
	struct xfs_inode	*ip = XFS_I(file_inode(iocb->ki_filp));

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!xfs_ilock_nowait(ip, lock_mode))
			return -EAGAIN;
	} else {
		xfs_ilock(ip, lock_mode);
	}

	return 0;
}

static int
xfs_ilock_iocb_for_write(
	struct kiocb		*iocb,
	unsigned int		*lock_mode)
{
	ssize_t			ret;
	struct xfs_inode	*ip = XFS_I(file_inode(iocb->ki_filp));

	ret = xfs_ilock_iocb(iocb, *lock_mode);
	if (ret)
		return ret;

	/*
	 * If a reflink remap is in progress we always need to take the iolock
	 * exclusively to wait for it to finish.
	 */
	if (*lock_mode == XFS_IOLOCK_SHARED &&
	    xfs_iflags_test(ip, XFS_IREMAPPING)) {
		xfs_iunlock(ip, *lock_mode);
		*lock_mode = XFS_IOLOCK_EXCL;
		return xfs_ilock_iocb(iocb, *lock_mode);
	}

	return 0;
}

STATIC ssize_t
xfs_file_dio_read(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct xfs_inode	*ip = XFS_I(file_inode(iocb->ki_filp));
	ssize_t			ret;

	trace_xfs_file_direct_read(iocb, to);

	if (!iov_iter_count(to))
		return 0; /* skip atime */

	file_accessed(iocb->ki_filp);

	ret = xfs_ilock_iocb(iocb, XFS_IOLOCK_SHARED);
	if (ret)
		return ret;
	ret = iomap_dio_rw(iocb, to, &xfs_read_iomap_ops, NULL, 0, NULL, 0);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);

	return ret;
}

static noinline ssize_t
xfs_file_dax_read(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct xfs_inode	*ip = XFS_I(iocb->ki_filp->f_mapping->host);
	ssize_t			ret = 0;

	trace_xfs_file_dax_read(iocb, to);

	if (!iov_iter_count(to))
		return 0; /* skip atime */

	ret = xfs_ilock_iocb(iocb, XFS_IOLOCK_SHARED);
	if (ret)
		return ret;
	ret = dax_iomap_rw(iocb, to, &xfs_read_iomap_ops);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);

	file_accessed(iocb->ki_filp);
	return ret;
}

STATIC ssize_t
xfs_file_buffered_read(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct xfs_inode	*ip = XFS_I(file_inode(iocb->ki_filp));
	ssize_t			ret;

	trace_xfs_file_buffered_read(iocb, to);

	ret = xfs_ilock_iocb(iocb, XFS_IOLOCK_SHARED);
	if (ret)
		return ret;
	ret = generic_file_read_iter(iocb, to);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);

	return ret;
}

STATIC ssize_t
xfs_file_read_iter(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct inode		*inode = file_inode(iocb->ki_filp);
	struct xfs_mount	*mp = XFS_I(inode)->i_mount;
	ssize_t			ret = 0;

	XFS_STATS_INC(mp, xs_read_calls);

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (IS_DAX(inode))
		ret = xfs_file_dax_read(iocb, to);
	else if (iocb->ki_flags & IOCB_DIRECT)
		ret = xfs_file_dio_read(iocb, to);
	else
		ret = xfs_file_buffered_read(iocb, to);

	if (ret > 0)
		XFS_STATS_ADD(mp, xs_read_bytes, ret);
	return ret;
}

STATIC ssize_t
xfs_file_splice_read(
	struct file		*in,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			len,
	unsigned int		flags)
{
	struct inode		*inode = file_inode(in);
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			ret = 0;

	XFS_STATS_INC(mp, xs_read_calls);

	if (xfs_is_shutdown(mp))
		return -EIO;

	trace_xfs_file_splice_read(ip, *ppos, len);

	xfs_ilock(ip, XFS_IOLOCK_SHARED);
	ret = filemap_splice_read(in, ppos, pipe, len, flags);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	if (ret > 0)
		XFS_STATS_ADD(mp, xs_read_bytes, ret);
	return ret;
}

/*
 * Common pre-write limit and setup checks.
 *
 * Called with the iolocked held either shared and exclusive according to
 * @iolock, and returns with it held.  Might upgrade the iolock to exclusive
 * if called for a direct write beyond i_size.
 */
STATIC ssize_t
xfs_file_write_checks(
	struct kiocb		*iocb,
	struct iov_iter		*from,
	unsigned int		*iolock)
{
	struct file		*file = iocb->ki_filp;
	struct inode		*inode = file->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	ssize_t			error = 0;
	size_t			count = iov_iter_count(from);
	bool			drained_dio = false;
	loff_t			isize;

restart:
	error = generic_write_checks(iocb, from);
	if (error <= 0)
		return error;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		error = break_layout(inode, false);
		if (error == -EWOULDBLOCK)
			error = -EAGAIN;
	} else {
		error = xfs_break_layouts(inode, iolock, BREAK_WRITE);
	}

	if (error)
		return error;

	/*
	 * For changing security info in file_remove_privs() we need i_rwsem
	 * exclusively.
	 */
	if (*iolock == XFS_IOLOCK_SHARED && !IS_NOSEC(inode)) {
		xfs_iunlock(ip, *iolock);
		*iolock = XFS_IOLOCK_EXCL;
		error = xfs_ilock_iocb(iocb, *iolock);
		if (error) {
			*iolock = 0;
			return error;
		}
		goto restart;
	}

	/*
	 * If the offset is beyond the size of the file, we need to zero any
	 * blocks that fall between the existing EOF and the start of this
	 * write.  If zeroing is needed and we are currently holding the iolock
	 * shared, we need to update it to exclusive which implies having to
	 * redo all checks before.
	 *
	 * We need to serialise against EOF updates that occur in IO completions
	 * here. We want to make sure that nobody is changing the size while we
	 * do this check until we have placed an IO barrier (i.e.  hold the
	 * XFS_IOLOCK_EXCL) that prevents new IO from being dispatched.  The
	 * spinlock effectively forms a memory barrier once we have the
	 * XFS_IOLOCK_EXCL so we are guaranteed to see the latest EOF value and
	 * hence be able to correctly determine if we need to run zeroing.
	 *
	 * We can do an unlocked check here safely as IO completion can only
	 * extend EOF. Truncate is locked out at this point, so the EOF can
	 * not move backwards, only forwards. Hence we only need to take the
	 * slow path and spin locks when we are at or beyond the current EOF.
	 */
	if (iocb->ki_pos <= i_size_read(inode))
		goto out;

	spin_lock(&ip->i_flags_lock);
	isize = i_size_read(inode);
	if (iocb->ki_pos > isize) {
		spin_unlock(&ip->i_flags_lock);

		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;

		if (!drained_dio) {
			if (*iolock == XFS_IOLOCK_SHARED) {
				xfs_iunlock(ip, *iolock);
				*iolock = XFS_IOLOCK_EXCL;
				xfs_ilock(ip, *iolock);
				iov_iter_reexpand(from, count);
			}
			/*
			 * We now have an IO submission barrier in place, but
			 * AIO can do EOF updates during IO completion and hence
			 * we now need to wait for all of them to drain. Non-AIO
			 * DIO will have drained before we are given the
			 * XFS_IOLOCK_EXCL, and so for most cases this wait is a
			 * no-op.
			 */
			inode_dio_wait(inode);
			drained_dio = true;
			goto restart;
		}

		trace_xfs_zero_eof(ip, isize, iocb->ki_pos - isize);
		error = xfs_zero_range(ip, isize, iocb->ki_pos - isize, NULL);
		if (error)
			return error;
	} else
		spin_unlock(&ip->i_flags_lock);

out:
	return kiocb_modified(iocb);
}

static int
xfs_dio_write_end_io(
	struct kiocb		*iocb,
	ssize_t			size,
	int			error,
	unsigned		flags)
{
	struct inode		*inode = file_inode(iocb->ki_filp);
	struct xfs_inode	*ip = XFS_I(inode);
	loff_t			offset = iocb->ki_pos;
	unsigned int		nofs_flag;

	trace_xfs_end_io_direct_write(ip, offset, size);

	if (xfs_is_shutdown(ip->i_mount))
		return -EIO;

	if (error)
		return error;
	if (!size)
		return 0;

	/*
	 * Capture amount written on completion as we can't reliably account
	 * for it on submission.
	 */
	XFS_STATS_ADD(ip->i_mount, xs_write_bytes, size);

	/*
	 * We can allocate memory here while doing writeback on behalf of
	 * memory reclaim.  To avoid memory allocation deadlocks set the
	 * task-wide nofs context for the following operations.
	 */
	nofs_flag = memalloc_nofs_save();

	if (flags & IOMAP_DIO_COW) {
		error = xfs_reflink_end_cow(ip, offset, size);
		if (error)
			goto out;
	}

	/*
	 * Unwritten conversion updates the in-core isize after extent
	 * conversion but before updating the on-disk size. Updating isize any
	 * earlier allows a racing dio read to find unwritten extents before
	 * they are converted.
	 */
	if (flags & IOMAP_DIO_UNWRITTEN) {
		error = xfs_iomap_write_unwritten(ip, offset, size, true);
		goto out;
	}

	/*
	 * We need to update the in-core inode size here so that we don't end up
	 * with the on-disk inode size being outside the in-core inode size. We
	 * have no other method of updating EOF for AIO, so always do it here
	 * if necessary.
	 *
	 * We need to lock the test/set EOF update as we can be racing with
	 * other IO completions here to update the EOF. Failing to serialise
	 * here can result in EOF moving backwards and Bad Things Happen when
	 * that occurs.
	 *
	 * As IO completion only ever extends EOF, we can do an unlocked check
	 * here to avoid taking the spinlock. If we land within the current EOF,
	 * then we do not need to do an extending update at all, and we don't
	 * need to take the lock to check this. If we race with an update moving
	 * EOF, then we'll either still be beyond EOF and need to take the lock,
	 * or we'll be within EOF and we don't need to take it at all.
	 */
	if (offset + size <= i_size_read(inode))
		goto out;

	spin_lock(&ip->i_flags_lock);
	if (offset + size > i_size_read(inode)) {
		i_size_write(inode, offset + size);
		spin_unlock(&ip->i_flags_lock);
		error = xfs_setfilesize(ip, offset, size);
	} else {
		spin_unlock(&ip->i_flags_lock);
	}

out:
	memalloc_nofs_restore(nofs_flag);
	return error;
}

static const struct iomap_dio_ops xfs_dio_write_ops = {
	.end_io		= xfs_dio_write_end_io,
};

/*
 * Handle block aligned direct I/O writes
 */
static noinline ssize_t
xfs_file_dio_write_aligned(
	struct xfs_inode	*ip,
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	unsigned int		iolock = XFS_IOLOCK_SHARED;
	ssize_t			ret;

	ret = xfs_ilock_iocb_for_write(iocb, &iolock);
	if (ret)
		return ret;
	ret = xfs_file_write_checks(iocb, from, &iolock);
	if (ret)
		goto out_unlock;

	/*
	 * We don't need to hold the IOLOCK exclusively across the IO, so demote
	 * the iolock back to shared if we had to take the exclusive lock in
	 * xfs_file_write_checks() for other reasons.
	 */
	if (iolock == XFS_IOLOCK_EXCL) {
		xfs_ilock_demote(ip, XFS_IOLOCK_EXCL);
		iolock = XFS_IOLOCK_SHARED;
	}
	trace_xfs_file_direct_write(iocb, from);
	ret = iomap_dio_rw(iocb, from, &xfs_direct_write_iomap_ops,
			   &xfs_dio_write_ops, 0, NULL, 0);
out_unlock:
	if (iolock)
		xfs_iunlock(ip, iolock);
	return ret;
}

/*
 * Handle block unaligned direct I/O writes
 *
 * In most cases direct I/O writes will be done holding IOLOCK_SHARED, allowing
 * them to be done in parallel with reads and other direct I/O writes.  However,
 * if the I/O is not aligned to filesystem blocks, the direct I/O layer may need
 * to do sub-block zeroing and that requires serialisation against other direct
 * I/O to the same block.  In this case we need to serialise the submission of
 * the unaligned I/O so that we don't get racing block zeroing in the dio layer.
 * In the case where sub-block zeroing is not required, we can do concurrent
 * sub-block dios to the same block successfully.
 *
 * Optimistically submit the I/O using the shared lock first, but use the
 * IOMAP_DIO_OVERWRITE_ONLY flag to tell the lower layers to return -EAGAIN
 * if block allocation or partial block zeroing would be required.  In that case
 * we try again with the exclusive lock.
 */
static noinline ssize_t
xfs_file_dio_write_unaligned(
	struct xfs_inode	*ip,
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	size_t			isize = i_size_read(VFS_I(ip));
	size_t			count = iov_iter_count(from);
	unsigned int		iolock = XFS_IOLOCK_SHARED;
	unsigned int		flags = IOMAP_DIO_OVERWRITE_ONLY;
	ssize_t			ret;

	/*
	 * Extending writes need exclusivity because of the sub-block zeroing
	 * that the DIO code always does for partial tail blocks beyond EOF, so
	 * don't even bother trying the fast path in this case.
	 */
	if (iocb->ki_pos > isize || iocb->ki_pos + count >= isize) {
		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;
retry_exclusive:
		iolock = XFS_IOLOCK_EXCL;
		flags = IOMAP_DIO_FORCE_WAIT;
	}

	ret = xfs_ilock_iocb_for_write(iocb, &iolock);
	if (ret)
		return ret;

	/*
	 * We can't properly handle unaligned direct I/O to reflink files yet,
	 * as we can't unshare a partial block.
	 */
	if (xfs_is_cow_inode(ip)) {
		trace_xfs_reflink_bounce_dio_write(iocb, from);
		ret = -ENOTBLK;
		goto out_unlock;
	}

	ret = xfs_file_write_checks(iocb, from, &iolock);
	if (ret)
		goto out_unlock;

	/*
	 * If we are doing exclusive unaligned I/O, this must be the only I/O
	 * in-flight.  Otherwise we risk data corruption due to unwritten extent
	 * conversions from the AIO end_io handler.  Wait for all other I/O to
	 * drain first.
	 */
	if (flags & IOMAP_DIO_FORCE_WAIT)
		inode_dio_wait(VFS_I(ip));

	trace_xfs_file_direct_write(iocb, from);
	ret = iomap_dio_rw(iocb, from, &xfs_direct_write_iomap_ops,
			   &xfs_dio_write_ops, flags, NULL, 0);

	/*
	 * Retry unaligned I/O with exclusive blocking semantics if the DIO
	 * layer rejected it for mapping or locking reasons. If we are doing
	 * nonblocking user I/O, propagate the error.
	 */
	if (ret == -EAGAIN && !(iocb->ki_flags & IOCB_NOWAIT)) {
		ASSERT(flags & IOMAP_DIO_OVERWRITE_ONLY);
		xfs_iunlock(ip, iolock);
		goto retry_exclusive;
	}

out_unlock:
	if (iolock)
		xfs_iunlock(ip, iolock);
	return ret;
}

static ssize_t
xfs_file_dio_write(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct xfs_inode	*ip = XFS_I(file_inode(iocb->ki_filp));
	struct xfs_buftarg      *target = xfs_inode_buftarg(ip);
	size_t			count = iov_iter_count(from);

	/* direct I/O must be aligned to device logical sector size */
	if ((iocb->ki_pos | count) & target->bt_logical_sectormask)
		return -EINVAL;
	if ((iocb->ki_pos | count) & ip->i_mount->m_blockmask)
		return xfs_file_dio_write_unaligned(ip, iocb, from);
	return xfs_file_dio_write_aligned(ip, iocb, from);
}

static noinline ssize_t
xfs_file_dax_write(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct inode		*inode = iocb->ki_filp->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	unsigned int		iolock = XFS_IOLOCK_EXCL;
	ssize_t			ret, error = 0;
	loff_t			pos;

	ret = xfs_ilock_iocb(iocb, iolock);
	if (ret)
		return ret;
	ret = xfs_file_write_checks(iocb, from, &iolock);
	if (ret)
		goto out;

	pos = iocb->ki_pos;

	trace_xfs_file_dax_write(iocb, from);
	ret = dax_iomap_rw(iocb, from, &xfs_dax_write_iomap_ops);
	if (ret > 0 && iocb->ki_pos > i_size_read(inode)) {
		i_size_write(inode, iocb->ki_pos);
		error = xfs_setfilesize(ip, pos, ret);
	}
out:
	if (iolock)
		xfs_iunlock(ip, iolock);
	if (error)
		return error;

	if (ret > 0) {
		XFS_STATS_ADD(ip->i_mount, xs_write_bytes, ret);

		/* Handle various SYNC-type writes */
		ret = generic_write_sync(iocb, ret);
	}
	return ret;
}

STATIC ssize_t
xfs_file_buffered_write(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct inode		*inode = iocb->ki_filp->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	ssize_t			ret;
	bool			cleared_space = false;
	unsigned int		iolock;

write_retry:
	iolock = XFS_IOLOCK_EXCL;
	ret = xfs_ilock_iocb(iocb, iolock);
	if (ret)
		return ret;

	ret = xfs_file_write_checks(iocb, from, &iolock);
	if (ret)
		goto out;

	trace_xfs_file_buffered_write(iocb, from);
	ret = iomap_file_buffered_write(iocb, from,
			&xfs_buffered_write_iomap_ops, NULL);

	/*
	 * If we hit a space limit, try to free up some lingering preallocated
	 * space before returning an error. In the case of ENOSPC, first try to
	 * write back all dirty inodes to free up some of the excess reserved
	 * metadata space. This reduces the chances that the eofblocks scan
	 * waits on dirty mappings. Since xfs_flush_inodes() is serialized, this
	 * also behaves as a filter to prevent too many eofblocks scans from
	 * running at the same time.  Use a synchronous scan to increase the
	 * effectiveness of the scan.
	 */
	if (ret == -EDQUOT && !cleared_space) {
		xfs_iunlock(ip, iolock);
		xfs_blockgc_free_quota(ip, XFS_ICWALK_FLAG_SYNC);
		cleared_space = true;
		goto write_retry;
	} else if (ret == -ENOSPC && !cleared_space) {
		struct xfs_icwalk	icw = {0};

		cleared_space = true;
		xfs_flush_inodes(ip->i_mount);

		xfs_iunlock(ip, iolock);
		icw.icw_flags = XFS_ICWALK_FLAG_SYNC;
		xfs_blockgc_free_space(ip->i_mount, &icw);
		goto write_retry;
	}

out:
	if (iolock)
		xfs_iunlock(ip, iolock);

	if (ret > 0) {
		XFS_STATS_ADD(ip->i_mount, xs_write_bytes, ret);
		/* Handle various SYNC-type writes */
		ret = generic_write_sync(iocb, ret);
	}
	return ret;
}

STATIC ssize_t
xfs_file_write_iter(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct inode		*inode = iocb->ki_filp->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	ssize_t			ret;
	size_t			ocount = iov_iter_count(from);

	XFS_STATS_INC(ip->i_mount, xs_write_calls);

	if (ocount == 0)
		return 0;

	if (xfs_is_shutdown(ip->i_mount))
		return -EIO;

	if (IS_DAX(inode))
		return xfs_file_dax_write(iocb, from);

	if (iocb->ki_flags & IOCB_DIRECT) {
		/*
		 * Allow a directio write to fall back to a buffered
		 * write *only* in the case that we're doing a reflink
		 * CoW.  In all other directio scenarios we do not
		 * allow an operation to fall back to buffered mode.
		 */
		ret = xfs_file_dio_write(iocb, from);
		if (ret != -ENOTBLK)
			return ret;
	}

	return xfs_file_buffered_write(iocb, from);
}

/* Does this file, inode, or mount want synchronous writes? */
static inline bool xfs_file_sync_writes(struct file *filp)
{
	struct xfs_inode	*ip = XFS_I(file_inode(filp));

	if (xfs_has_wsync(ip->i_mount))
		return true;
	if (filp->f_flags & (__O_SYNC | O_DSYNC))
		return true;
	if (IS_SYNC(file_inode(filp)))
		return true;

	return false;
}

static int
xfs_falloc_newsize(
	struct file		*file,
	int			mode,
	loff_t			offset,
	loff_t			len,
	loff_t			*new_size)
{
	struct inode		*inode = file_inode(file);

	if ((mode & FALLOC_FL_KEEP_SIZE) || offset + len <= i_size_read(inode))
		return 0;
	*new_size = offset + len;
	return inode_newsize_ok(inode, *new_size);
}

static int
xfs_falloc_setsize(
	struct file		*file,
	loff_t			new_size)
{
	struct iattr iattr = {
		.ia_valid	= ATTR_SIZE,
		.ia_size	= new_size,
	};

	if (!new_size)
		return 0;
	return xfs_vn_setattr_size(file_mnt_idmap(file), file_dentry(file),
			&iattr);
}

static int
xfs_falloc_collapse_range(
	struct file		*file,
	loff_t			offset,
	loff_t			len)
{
	struct inode		*inode = file_inode(file);
	loff_t			new_size = i_size_read(inode) - len;
	int			error;

	if (!xfs_is_falloc_aligned(XFS_I(inode), offset, len))
		return -EINVAL;

	/*
	 * There is no need to overlap collapse range with EOF, in which case it
	 * is effectively a truncate operation
	 */
	if (offset + len >= i_size_read(inode))
		return -EINVAL;

	error = xfs_collapse_file_space(XFS_I(inode), offset, len);
	if (error)
		return error;
	return xfs_falloc_setsize(file, new_size);
}

static int
xfs_falloc_insert_range(
	struct file		*file,
	loff_t			offset,
	loff_t			len)
{
	struct inode		*inode = file_inode(file);
	loff_t			isize = i_size_read(inode);
	int			error;

	if (!xfs_is_falloc_aligned(XFS_I(inode), offset, len))
		return -EINVAL;

	/*
	 * New inode size must not exceed ->s_maxbytes, accounting for
	 * possible signed overflow.
	 */
	if (inode->i_sb->s_maxbytes - isize < len)
		return -EFBIG;

	/* Offset should be less than i_size */
	if (offset >= isize)
		return -EINVAL;

	error = xfs_falloc_setsize(file, isize + len);
	if (error)
		return error;

	/*
	 * Perform hole insertion now that the file size has been updated so
	 * that if we crash during the operation we don't leave shifted extents
	 * past EOF and hence losing access to the data that is contained within
	 * them.
	 */
	return xfs_insert_file_space(XFS_I(inode), offset, len);
}

/*
 * Punch a hole and prealloc the range.  We use a hole punch rather than
 * unwritten extent conversion for two reasons:
 *
 *   1.) Hole punch handles partial block zeroing for us.
 *   2.) If prealloc returns ENOSPC, the file range is still zero-valued by
 *	 virtue of the hole punch.
 */
static int
xfs_falloc_zero_range(
	struct file		*file,
	int			mode,
	loff_t			offset,
	loff_t			len)
{
	struct inode		*inode = file_inode(file);
	unsigned int		blksize = i_blocksize(inode);
	loff_t			new_size = 0;
	int			error;

	trace_xfs_zero_file_space(XFS_I(inode));

	error = xfs_falloc_newsize(file, mode, offset, len, &new_size);
	if (error)
		return error;

	error = xfs_free_file_space(XFS_I(inode), offset, len);
	if (error)
		return error;

	len = round_up(offset + len, blksize) - round_down(offset, blksize);
	offset = round_down(offset, blksize);
	error = xfs_alloc_file_space(XFS_I(inode), offset, len);
	if (error)
		return error;
	return xfs_falloc_setsize(file, new_size);
}

static int
xfs_falloc_unshare_range(
	struct file		*file,
	int			mode,
	loff_t			offset,
	loff_t			len)
{
	struct inode		*inode = file_inode(file);
	loff_t			new_size = 0;
	int			error;

	error = xfs_falloc_newsize(file, mode, offset, len, &new_size);
	if (error)
		return error;

	error = xfs_reflink_unshare(XFS_I(inode), offset, len);
	if (error)
		return error;

	error = xfs_alloc_file_space(XFS_I(inode), offset, len);
	if (error)
		return error;
	return xfs_falloc_setsize(file, new_size);
}

static int
xfs_falloc_allocate_range(
	struct file		*file,
	int			mode,
	loff_t			offset,
	loff_t			len)
{
	struct inode		*inode = file_inode(file);
	loff_t			new_size = 0;
	int			error;

	/*
	 * If always_cow mode we can't use preallocations and thus should not
	 * create them.
	 */
	if (xfs_is_always_cow_inode(XFS_I(inode)))
		return -EOPNOTSUPP;

	error = xfs_falloc_newsize(file, mode, offset, len, &new_size);
	if (error)
		return error;

	error = xfs_alloc_file_space(XFS_I(inode), offset, len);
	if (error)
		return error;
	return xfs_falloc_setsize(file, new_size);
}

#define	XFS_FALLOC_FL_SUPPORTED						\
		(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |		\
		 FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |	\
		 FALLOC_FL_INSERT_RANGE | FALLOC_FL_UNSHARE_RANGE)

STATIC long
xfs_file_fallocate(
	struct file		*file,
	int			mode,
	loff_t			offset,
	loff_t			len)
{
	struct inode		*inode = file_inode(file);
	struct xfs_inode	*ip = XFS_I(inode);
	long			error;
	uint			iolock = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;
	if (mode & ~XFS_FALLOC_FL_SUPPORTED)
		return -EOPNOTSUPP;

	xfs_ilock(ip, iolock);
	error = xfs_break_layouts(inode, &iolock, BREAK_UNMAP);
	if (error)
		goto out_unlock;

	/*
	 * Must wait for all AIO to complete before we continue as AIO can
	 * change the file size on completion without holding any locks we
	 * currently hold. We must do this first because AIO can update both
	 * the on disk and in memory inode sizes, and the operations that follow
	 * require the in-memory size to be fully up-to-date.
	 */
	inode_dio_wait(inode);

	error = file_modified(file);
	if (error)
		goto out_unlock;

	switch (mode & FALLOC_FL_MODE_MASK) {
	case FALLOC_FL_PUNCH_HOLE:
		error = xfs_free_file_space(ip, offset, len);
		break;
	case FALLOC_FL_COLLAPSE_RANGE:
		error = xfs_falloc_collapse_range(file, offset, len);
		break;
	case FALLOC_FL_INSERT_RANGE:
		error = xfs_falloc_insert_range(file, offset, len);
		break;
	case FALLOC_FL_ZERO_RANGE:
		error = xfs_falloc_zero_range(file, mode, offset, len);
		break;
	case FALLOC_FL_UNSHARE_RANGE:
		error = xfs_falloc_unshare_range(file, mode, offset, len);
		break;
	case FALLOC_FL_ALLOCATE_RANGE:
		error = xfs_falloc_allocate_range(file, mode, offset, len);
		break;
	default:
		error = -EOPNOTSUPP;
		break;
	}

	if (!error && xfs_file_sync_writes(file))
		error = xfs_log_force_inode(ip);

out_unlock:
	xfs_iunlock(ip, iolock);
	return error;
}

STATIC int
xfs_file_fadvise(
	struct file	*file,
	loff_t		start,
	loff_t		end,
	int		advice)
{
	struct xfs_inode *ip = XFS_I(file_inode(file));
	int ret;
	int lockflags = 0;

	/*
	 * Operations creating pages in page cache need protection from hole
	 * punching and similar ops
	 */
	if (advice == POSIX_FADV_WILLNEED) {
		lockflags = XFS_IOLOCK_SHARED;
		xfs_ilock(ip, lockflags);
	}
	ret = generic_fadvise(file, start, end, advice);
	if (lockflags)
		xfs_iunlock(ip, lockflags);
	return ret;
}

STATIC loff_t
xfs_file_remap_range(
	struct file		*file_in,
	loff_t			pos_in,
	struct file		*file_out,
	loff_t			pos_out,
	loff_t			len,
	unsigned int		remap_flags)
{
	struct inode		*inode_in = file_inode(file_in);
	struct xfs_inode	*src = XFS_I(inode_in);
	struct inode		*inode_out = file_inode(file_out);
	struct xfs_inode	*dest = XFS_I(inode_out);
	struct xfs_mount	*mp = src->i_mount;
	loff_t			remapped = 0;
	xfs_extlen_t		cowextsize;
	int			ret;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (!xfs_has_reflink(mp))
		return -EOPNOTSUPP;

	if (xfs_is_shutdown(mp))
		return -EIO;

	/* Prepare and then clone file data. */
	ret = xfs_reflink_remap_prep(file_in, pos_in, file_out, pos_out,
			&len, remap_flags);
	if (ret || len == 0)
		return ret;

	trace_xfs_reflink_remap_range(src, pos_in, len, dest, pos_out);

	ret = xfs_reflink_remap_blocks(src, pos_in, dest, pos_out, len,
			&remapped);
	if (ret)
		goto out_unlock;

	/*
	 * Carry the cowextsize hint from src to dest if we're sharing the
	 * entire source file to the entire destination file, the source file
	 * has a cowextsize hint, and the destination file does not.
	 */
	cowextsize = 0;
	if (pos_in == 0 && len == i_size_read(inode_in) &&
	    (src->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE) &&
	    pos_out == 0 && len >= i_size_read(inode_out) &&
	    !(dest->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE))
		cowextsize = src->i_cowextsize;

	ret = xfs_reflink_update_dest(dest, pos_out + len, cowextsize,
			remap_flags);
	if (ret)
		goto out_unlock;

	if (xfs_file_sync_writes(file_in) || xfs_file_sync_writes(file_out))
		xfs_log_force_inode(dest);
out_unlock:
	xfs_iunlock2_remapping(src, dest);
	if (ret)
		trace_xfs_reflink_remap_range_error(dest, ret, _RET_IP_);
	return remapped > 0 ? remapped : ret;
}

STATIC int
xfs_file_open(
	struct inode	*inode,
	struct file	*file)
{
	if (xfs_is_shutdown(XFS_M(inode->i_sb)))
		return -EIO;
	file->f_mode |= FMODE_NOWAIT | FMODE_CAN_ODIRECT;
	return generic_file_open(inode, file);
}

STATIC int
xfs_dir_open(
	struct inode	*inode,
	struct file	*file)
{
	struct xfs_inode *ip = XFS_I(inode);
	unsigned int	mode;
	int		error;

	if (xfs_is_shutdown(ip->i_mount))
		return -EIO;
	error = generic_file_open(inode, file);
	if (error)
		return error;

	/*
	 * If there are any blocks, read-ahead block 0 as we're almost
	 * certain to have the next operation be a read there.
	 */
	mode = xfs_ilock_data_map_shared(ip);
	if (ip->i_df.if_nextents > 0)
		error = xfs_dir3_data_readahead(ip, 0, 0);
	xfs_iunlock(ip, mode);
	return error;
}

/*
 * Don't bother propagating errors.  We're just doing cleanup, and the caller
 * ignores the return value anyway.
 */
STATIC int
xfs_file_release(
	struct inode		*inode,
	struct file		*file)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;

	/*
	 * If this is a read-only mount or the file system has been shut down,
	 * don't generate I/O.
	 */
	if (xfs_is_readonly(mp) || xfs_is_shutdown(mp))
		return 0;

	/*
	 * If we previously truncated this file and removed old data in the
	 * process, we want to initiate "early" writeout on the last close.
	 * This is an attempt to combat the notorious NULL files problem which
	 * is particularly noticeable from a truncate down, buffered (re-)write
	 * (delalloc), followed by a crash.  What we are effectively doing here
	 * is significantly reducing the time window where we'd otherwise be
	 * exposed to that problem.
	 */
	if (xfs_iflags_test_and_clear(ip, XFS_ITRUNCATED)) {
		xfs_iflags_clear(ip, XFS_EOFBLOCKS_RELEASED);
		if (ip->i_delayed_blks > 0)
			filemap_flush(inode->i_mapping);
	}

	/*
	 * XFS aggressively preallocates post-EOF space to generate contiguous
	 * allocations for writers that append to the end of the file.
	 *
	 * To support workloads that close and reopen the file frequently, these
	 * preallocations usually persist after a close unless it is the first
	 * close for the inode.  This is a tradeoff to generate tightly packed
	 * data layouts for unpacking tarballs or similar archives that write
	 * one file after another without going back to it while keeping the
	 * preallocation for files that have recurring open/write/close cycles.
	 *
	 * This heuristic is skipped for inodes with the append-only flag as
	 * that flag is rather pointless for inodes written only once.
	 *
	 * There is no point in freeing blocks here for open but unlinked files
	 * as they will be taken care of by the inactivation path soon.
	 *
	 * When releasing a read-only context, don't flush data or trim post-EOF
	 * blocks.  This avoids open/read/close workloads from removing EOF
	 * blocks that other writers depend upon to reduce fragmentation.
	 *
	 * If we can't get the iolock just skip truncating the blocks past EOF
	 * because we could deadlock with the mmap_lock otherwise. We'll get
	 * another chance to drop them once the last reference to the inode is
	 * dropped, so we'll never leak blocks permanently.
	 */
	if (inode->i_nlink &&
	    (file->f_mode & FMODE_WRITE) &&
	    !(ip->i_diflags & XFS_DIFLAG_APPEND) &&
	    !xfs_iflags_test(ip, XFS_EOFBLOCKS_RELEASED) &&
	    xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL)) {
		if (xfs_can_free_eofblocks(ip) &&
		    !xfs_iflags_test_and_set(ip, XFS_EOFBLOCKS_RELEASED))
			xfs_free_eofblocks(ip);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	}

	return 0;
}

STATIC int
xfs_file_readdir(
	struct file	*file,
	struct dir_context *ctx)
{
	struct inode	*inode = file_inode(file);
	xfs_inode_t	*ip = XFS_I(inode);
	size_t		bufsize;

	/*
	 * The Linux API doesn't pass down the total size of the buffer
	 * we read into down to the filesystem.  With the filldir concept
	 * it's not needed for correct information, but the XFS dir2 leaf
	 * code wants an estimate of the buffer size to calculate it's
	 * readahead window and size the buffers used for mapping to
	 * physical blocks.
	 *
	 * Try to give it an estimate that's good enough, maybe at some
	 * point we can change the ->readdir prototype to include the
	 * buffer size.  For now we use the current glibc buffer size.
	 */
	bufsize = (size_t)min_t(loff_t, XFS_READDIR_BUFSIZE, ip->i_disk_size);

	return xfs_readdir(NULL, ip, ctx, bufsize);
}

STATIC loff_t
xfs_file_llseek(
	struct file	*file,
	loff_t		offset,
	int		whence)
{
	struct inode		*inode = file->f_mapping->host;

	if (xfs_is_shutdown(XFS_I(inode)->i_mount))
		return -EIO;

	switch (whence) {
	default:
		return generic_file_llseek(file, offset, whence);
	case SEEK_HOLE:
		offset = iomap_seek_hole(inode, offset, &xfs_seek_iomap_ops);
		break;
	case SEEK_DATA:
		offset = iomap_seek_data(inode, offset, &xfs_seek_iomap_ops);
		break;
	}

	if (offset < 0)
		return offset;
	return vfs_setpos(file, offset, inode->i_sb->s_maxbytes);
}

static inline vm_fault_t
xfs_dax_fault_locked(
	struct vm_fault		*vmf,
	unsigned int		order,
	bool			write_fault)
{
	vm_fault_t		ret;
	pfn_t			pfn;

	if (!IS_ENABLED(CONFIG_FS_DAX)) {
		ASSERT(0);
		return VM_FAULT_SIGBUS;
	}
	ret = dax_iomap_fault(vmf, order, &pfn, NULL,
			(write_fault && !vmf->cow_page) ?
				&xfs_dax_write_iomap_ops :
				&xfs_read_iomap_ops);
	if (ret & VM_FAULT_NEEDDSYNC)
		ret = dax_finish_sync_fault(vmf, order, pfn);
	return ret;
}

static vm_fault_t
xfs_dax_read_fault(
	struct vm_fault		*vmf,
	unsigned int		order)
{
	struct xfs_inode	*ip = XFS_I(file_inode(vmf->vma->vm_file));
	vm_fault_t		ret;

	xfs_ilock(ip, XFS_MMAPLOCK_SHARED);
	ret = xfs_dax_fault_locked(vmf, order, false);
	xfs_iunlock(ip, XFS_MMAPLOCK_SHARED);

	return ret;
}

static vm_fault_t
xfs_write_fault(
	struct vm_fault		*vmf,
	unsigned int		order)
{
	struct inode		*inode = file_inode(vmf->vma->vm_file);
	struct xfs_inode	*ip = XFS_I(inode);
	unsigned int		lock_mode = XFS_MMAPLOCK_SHARED;
	vm_fault_t		ret;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vmf->vma->vm_file);

	/*
	 * Normally we only need the shared mmaplock, but if a reflink remap is
	 * in progress we take the exclusive lock to wait for the remap to
	 * finish before taking a write fault.
	 */
	xfs_ilock(ip, XFS_MMAPLOCK_SHARED);
	if (xfs_iflags_test(ip, XFS_IREMAPPING)) {
		xfs_iunlock(ip, XFS_MMAPLOCK_SHARED);
		xfs_ilock(ip, XFS_MMAPLOCK_EXCL);
		lock_mode = XFS_MMAPLOCK_EXCL;
	}

	if (IS_DAX(inode))
		ret = xfs_dax_fault_locked(vmf, order, true);
	else
		ret = iomap_page_mkwrite(vmf, &xfs_page_mkwrite_iomap_ops);
	xfs_iunlock(ip, lock_mode);

	sb_end_pagefault(inode->i_sb);
	return ret;
}

/*
 * Locking for serialisation of IO during page faults. This results in a lock
 * ordering of:
 *
 * mmap_lock (MM)
 *   sb_start_pagefault(vfs, freeze)
 *     invalidate_lock (vfs/XFS_MMAPLOCK - truncate serialisation)
 *       page_lock (MM)
 *         i_lock (XFS - extent map serialisation)
 */
static vm_fault_t
__xfs_filemap_fault(
	struct vm_fault		*vmf,
	unsigned int		order,
	bool			write_fault)
{
	struct inode		*inode = file_inode(vmf->vma->vm_file);

	trace_xfs_filemap_fault(XFS_I(inode), order, write_fault);

	if (write_fault)
		return xfs_write_fault(vmf, order);
	if (IS_DAX(inode))
		return xfs_dax_read_fault(vmf, order);
	return filemap_fault(vmf);
}

static inline bool
xfs_is_write_fault(
	struct vm_fault		*vmf)
{
	return (vmf->flags & FAULT_FLAG_WRITE) &&
	       (vmf->vma->vm_flags & VM_SHARED);
}

static vm_fault_t
xfs_filemap_fault(
	struct vm_fault		*vmf)
{
	/* DAX can shortcut the normal fault path on write faults! */
	return __xfs_filemap_fault(vmf, 0,
			IS_DAX(file_inode(vmf->vma->vm_file)) &&
			xfs_is_write_fault(vmf));
}

static vm_fault_t
xfs_filemap_huge_fault(
	struct vm_fault		*vmf,
	unsigned int		order)
{
	if (!IS_DAX(file_inode(vmf->vma->vm_file)))
		return VM_FAULT_FALLBACK;

	/* DAX can shortcut the normal fault path on write faults! */
	return __xfs_filemap_fault(vmf, order,
			xfs_is_write_fault(vmf));
}

static vm_fault_t
xfs_filemap_page_mkwrite(
	struct vm_fault		*vmf)
{
	return __xfs_filemap_fault(vmf, 0, true);
}

/*
 * pfn_mkwrite was originally intended to ensure we capture time stamp updates
 * on write faults. In reality, it needs to serialise against truncate and
 * prepare memory for writing so handle is as standard write fault.
 */
static vm_fault_t
xfs_filemap_pfn_mkwrite(
	struct vm_fault		*vmf)
{

	return __xfs_filemap_fault(vmf, 0, true);
}

static const struct vm_operations_struct xfs_file_vm_ops = {
	.fault		= xfs_filemap_fault,
	.huge_fault	= xfs_filemap_huge_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= xfs_filemap_page_mkwrite,
	.pfn_mkwrite	= xfs_filemap_pfn_mkwrite,
};

STATIC int
xfs_file_mmap(
	struct file		*file,
	struct vm_area_struct	*vma)
{
	struct inode		*inode = file_inode(file);
	struct xfs_buftarg	*target = xfs_inode_buftarg(XFS_I(inode));

	/*
	 * We don't support synchronous mappings for non-DAX files and
	 * for DAX files if underneath dax_device is not synchronous.
	 */
	if (!daxdev_mapping_supported(vma, target->bt_daxdev))
		return -EOPNOTSUPP;

	file_accessed(file);
	vma->vm_ops = &xfs_file_vm_ops;
	if (IS_DAX(inode))
		vm_flags_set(vma, VM_HUGEPAGE);
	return 0;
}

const struct file_operations xfs_file_operations = {
	.llseek		= xfs_file_llseek,
	.read_iter	= xfs_file_read_iter,
	.write_iter	= xfs_file_write_iter,
	.splice_read	= xfs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.iopoll		= iocb_bio_iopoll,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
	.get_unmapped_area = thp_get_unmapped_area,
	.fallocate	= xfs_file_fallocate,
	.fadvise	= xfs_file_fadvise,
	.remap_file_range = xfs_file_remap_range,
	.fop_flags	= FOP_MMAP_SYNC | FOP_BUFFER_RASYNC |
			  FOP_BUFFER_WASYNC | FOP_DIO_PARALLEL_WRITE,
};

const struct file_operations xfs_dir_file_operations = {
	.open		= xfs_dir_open,
	.read		= generic_read_dir,
	.iterate_shared	= xfs_file_readdir,
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.fsync		= xfs_dir_fsync,
};
