/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_error.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_ioctl.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_icache.h"
#include "xfs_pnfs.h"

#include <linux/dcache.h>
#include <linux/falloc.h>
#include <linux/pagevec.h>
#include <linux/backing-dev.h>

static const struct vm_operations_struct xfs_file_vm_ops;

/*
 * Locking primitives for read and write IO paths to ensure we consistently use
 * and order the inode->i_mutex, ip->i_lock and ip->i_iolock.
 */
static inline void
xfs_rw_ilock(
	struct xfs_inode	*ip,
	int			type)
{
	if (type & XFS_IOLOCK_EXCL)
		inode_lock(VFS_I(ip));
	xfs_ilock(ip, type);
}

static inline void
xfs_rw_iunlock(
	struct xfs_inode	*ip,
	int			type)
{
	xfs_iunlock(ip, type);
	if (type & XFS_IOLOCK_EXCL)
		inode_unlock(VFS_I(ip));
}

static inline void
xfs_rw_ilock_demote(
	struct xfs_inode	*ip,
	int			type)
{
	xfs_ilock_demote(ip, type);
	if (type & XFS_IOLOCK_EXCL)
		inode_unlock(VFS_I(ip));
}

/*
 * xfs_iozero clears the specified range supplied via the page cache (except in
 * the DAX case). Writes through the page cache will allocate blocks over holes,
 * though the callers usually map the holes first and avoid them. If a block is
 * not completely zeroed, then it will be read from disk before being partially
 * zeroed.
 *
 * In the DAX case, we can just directly write to the underlying pages. This
 * will not allocate blocks, but will avoid holes and unwritten extents and so
 * not do unnecessary work.
 */
int
xfs_iozero(
	struct xfs_inode	*ip,	/* inode			*/
	loff_t			pos,	/* offset in file		*/
	size_t			count)	/* size of data to zero		*/
{
	struct page		*page;
	struct address_space	*mapping;
	int			status = 0;


	mapping = VFS_I(ip)->i_mapping;
	do {
		unsigned offset, bytes;
		void *fsdata;

		offset = (pos & (PAGE_SIZE -1)); /* Within page */
		bytes = PAGE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		if (IS_DAX(VFS_I(ip))) {
			status = dax_zero_page_range(VFS_I(ip), pos, bytes,
						     xfs_get_blocks_direct);
			if (status)
				break;
		} else {
			status = pagecache_write_begin(NULL, mapping, pos, bytes,
						AOP_FLAG_UNINTERRUPTIBLE,
						&page, &fsdata);
			if (status)
				break;

			zero_user(page, offset, bytes);

			status = pagecache_write_end(NULL, mapping, pos, bytes,
						bytes, page, fsdata);
			WARN_ON(status <= 0); /* can't return less than zero! */
			status = 0;
		}
		pos += bytes;
		count -= bytes;
	} while (count);

	return status;
}

int
xfs_update_prealloc_flags(
	struct xfs_inode	*ip,
	enum xfs_prealloc_flags	flags)
{
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(ip->i_mount, &M_RES(ip->i_mount)->tr_writeid,
			0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	if (!(flags & XFS_PREALLOC_INVISIBLE)) {
		VFS_I(ip)->i_mode &= ~S_ISUID;
		if (VFS_I(ip)->i_mode & S_IXGRP)
			VFS_I(ip)->i_mode &= ~S_ISGID;
		xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	}

	if (flags & XFS_PREALLOC_SET)
		ip->i_d.di_flags |= XFS_DIFLAG_PREALLOC;
	if (flags & XFS_PREALLOC_CLEAR)
		ip->i_d.di_flags &= ~XFS_DIFLAG_PREALLOC;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	if (flags & XFS_PREALLOC_SYNC)
		xfs_trans_set_sync(tp);
	return xfs_trans_commit(tp);
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
	struct xfs_mount	*mp = ip->i_mount;
	xfs_lsn_t		lsn = 0;

	trace_xfs_dir_fsync(ip);

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (xfs_ipincount(ip))
		lsn = ip->i_itemp->ili_last_lsn;
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!lsn)
		return 0;
	return _xfs_log_force_lsn(mp, lsn, XFS_LOG_SYNC, NULL);
}

STATIC int
xfs_file_fsync(
	struct file		*file,
	loff_t			start,
	loff_t			end,
	int			datasync)
{
	struct inode		*inode = file->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	int			error = 0;
	int			log_flushed = 0;
	xfs_lsn_t		lsn = 0;

	trace_xfs_file_fsync(ip);

	error = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (error)
		return error;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	xfs_iflags_clear(ip, XFS_ITRUNCATED);

	if (mp->m_flags & XFS_MOUNT_BARRIER) {
		/*
		 * If we have an RT and/or log subvolume we need to make sure
		 * to flush the write cache the device used for file data
		 * first.  This is to ensure newly written file data make
		 * it to disk before logging the new inode size in case of
		 * an extending write.
		 */
		if (XFS_IS_REALTIME_INODE(ip))
			xfs_blkdev_issue_flush(mp->m_rtdev_targp);
		else if (mp->m_logdev_targp != mp->m_ddev_targp)
			xfs_blkdev_issue_flush(mp->m_ddev_targp);
	}

	/*
	 * All metadata updates are logged, which means that we just have to
	 * flush the log up to the latest LSN that touched the inode. If we have
	 * concurrent fsync/fdatasync() calls, we need them to all block on the
	 * log force before we clear the ili_fsync_fields field. This ensures
	 * that we don't get a racing sync operation that does not wait for the
	 * metadata to hit the journal before returning. If we race with
	 * clearing the ili_fsync_fields, then all that will happen is the log
	 * force will do nothing as the lsn will already be on disk. We can't
	 * race with setting ili_fsync_fields because that is done under
	 * XFS_ILOCK_EXCL, and that can't happen because we hold the lock shared
	 * until after the ili_fsync_fields is cleared.
	 */
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (xfs_ipincount(ip)) {
		if (!datasync ||
		    (ip->i_itemp->ili_fsync_fields & ~XFS_ILOG_TIMESTAMP))
			lsn = ip->i_itemp->ili_last_lsn;
	}

	if (lsn) {
		error = _xfs_log_force_lsn(mp, lsn, XFS_LOG_SYNC, &log_flushed);
		ip->i_itemp->ili_fsync_fields = 0;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	/*
	 * If we only have a single device, and the log force about was
	 * a no-op we might have to flush the data device cache here.
	 * This can only happen for fdatasync/O_DSYNC if we were overwriting
	 * an already allocated file and thus do not have any metadata to
	 * commit.
	 */
	if ((mp->m_flags & XFS_MOUNT_BARRIER) &&
	    mp->m_logdev_targp == mp->m_ddev_targp &&
	    !XFS_IS_REALTIME_INODE(ip) &&
	    !log_flushed)
		xfs_blkdev_issue_flush(mp->m_ddev_targp);

	return error;
}

STATIC ssize_t
xfs_file_dio_aio_read(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct address_space	*mapping = iocb->ki_filp->f_mapping;
	struct inode		*inode = mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	size_t			count = iov_iter_count(to);
	struct xfs_buftarg	*target;
	ssize_t			ret = 0;

	trace_xfs_file_direct_read(ip, count, iocb->ki_pos);

	if (XFS_IS_REALTIME_INODE(ip))
		target = ip->i_mount->m_rtdev_targp;
	else
		target = ip->i_mount->m_ddev_targp;

	if (!IS_DAX(inode)) {
		/* DIO must be aligned to device logical sector size */
		if ((iocb->ki_pos | count) & target->bt_logical_sectormask) {
			if (iocb->ki_pos == i_size_read(inode))
				return 0;
			return -EINVAL;
		}
	}

	/*
	 * Locking is a bit tricky here. If we take an exclusive lock for direct
	 * IO, we effectively serialise all new concurrent read IO to this file
	 * and block it behind IO that is currently in progress because IO in
	 * progress holds the IO lock shared. We only need to hold the lock
	 * exclusive to blow away the page cache, so only take lock exclusively
	 * if the page cache needs invalidation. This allows the normal direct
	 * IO case of no page cache pages to proceeed concurrently without
	 * serialisation.
	 */
	xfs_rw_ilock(ip, XFS_IOLOCK_SHARED);
	if (mapping->nrpages) {
		xfs_rw_iunlock(ip, XFS_IOLOCK_SHARED);
		xfs_rw_ilock(ip, XFS_IOLOCK_EXCL);

		/*
		 * The generic dio code only flushes the range of the particular
		 * I/O. Because we take an exclusive lock here, this whole
		 * sequence is considerably more expensive for us. This has a
		 * noticeable performance impact for any file with cached pages,
		 * even when outside of the range of the particular I/O.
		 *
		 * Hence, amortize the cost of the lock against a full file
		 * flush and reduce the chances of repeated iolock cycles going
		 * forward.
		 */
		if (mapping->nrpages) {
			ret = filemap_write_and_wait(mapping);
			if (ret) {
				xfs_rw_iunlock(ip, XFS_IOLOCK_EXCL);
				return ret;
			}

			/*
			 * Invalidate whole pages. This can return an error if
			 * we fail to invalidate a page, but this should never
			 * happen on XFS. Warn if it does fail.
			 */
			ret = invalidate_inode_pages2(mapping);
			WARN_ON_ONCE(ret);
			ret = 0;
		}
		xfs_rw_ilock_demote(ip, XFS_IOLOCK_EXCL);
	}

	ret = generic_file_read_iter(iocb, to);
	xfs_rw_iunlock(ip, XFS_IOLOCK_SHARED);

	return ret;
}

STATIC ssize_t
xfs_file_buffered_aio_read(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct xfs_inode	*ip = XFS_I(file_inode(iocb->ki_filp));
	ssize_t			ret;

	trace_xfs_file_buffered_read(ip, iov_iter_count(to), iocb->ki_pos);

	xfs_rw_ilock(ip, XFS_IOLOCK_SHARED);
	ret = generic_file_read_iter(iocb, to);
	xfs_rw_iunlock(ip, XFS_IOLOCK_SHARED);

	return ret;
}

STATIC ssize_t
xfs_file_read_iter(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct xfs_mount	*mp = XFS_I(file_inode(iocb->ki_filp))->i_mount;
	ssize_t			ret = 0;

	XFS_STATS_INC(mp, xs_read_calls);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	if (iocb->ki_flags & IOCB_DIRECT)
		ret = xfs_file_dio_aio_read(iocb, to);
	else
		ret = xfs_file_buffered_aio_read(iocb, to);

	if (ret > 0)
		XFS_STATS_ADD(mp, xs_read_bytes, ret);
	return ret;
}

STATIC ssize_t
xfs_file_splice_read(
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			count,
	unsigned int		flags)
{
	struct xfs_inode	*ip = XFS_I(infilp->f_mapping->host);
	ssize_t			ret;

	XFS_STATS_INC(ip->i_mount, xs_read_calls);

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	trace_xfs_file_splice_read(ip, count, *ppos);

	/*
	 * DAX inodes cannot ues the page cache for splice, so we have to push
	 * them through the VFS IO path. This means it goes through
	 * ->read_iter, which for us takes the XFS_IOLOCK_SHARED. Hence we
	 * cannot lock the splice operation at this level for DAX inodes.
	 */
	if (IS_DAX(VFS_I(ip))) {
		ret = default_file_splice_read(infilp, ppos, pipe, count,
					       flags);
		goto out;
	}

	xfs_rw_ilock(ip, XFS_IOLOCK_SHARED);
	ret = generic_file_splice_read(infilp, ppos, pipe, count, flags);
	xfs_rw_iunlock(ip, XFS_IOLOCK_SHARED);
out:
	if (ret > 0)
		XFS_STATS_ADD(ip->i_mount, xs_read_bytes, ret);
	return ret;
}

/*
 * This routine is called to handle zeroing any space in the last block of the
 * file that is beyond the EOF.  We do this since the size is being increased
 * without writing anything to that block and we don't want to read the
 * garbage on the disk.
 */
STATIC int				/* error (positive) */
xfs_zero_last_block(
	struct xfs_inode	*ip,
	xfs_fsize_t		offset,
	xfs_fsize_t		isize,
	bool			*did_zeroing)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		last_fsb = XFS_B_TO_FSBT(mp, isize);
	int			zero_offset = XFS_B_FSB_OFFSET(mp, isize);
	int			zero_len;
	int			nimaps = 1;
	int			error = 0;
	struct xfs_bmbt_irec	imap;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	error = xfs_bmapi_read(ip, last_fsb, 1, &imap, &nimaps, 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		return error;

	ASSERT(nimaps > 0);

	/*
	 * If the block underlying isize is just a hole, then there
	 * is nothing to zero.
	 */
	if (imap.br_startblock == HOLESTARTBLOCK)
		return 0;

	zero_len = mp->m_sb.sb_blocksize - zero_offset;
	if (isize + zero_len > offset)
		zero_len = offset - isize;
	*did_zeroing = true;
	return xfs_iozero(ip, isize, zero_len);
}

/*
 * Zero any on disk space between the current EOF and the new, larger EOF.
 *
 * This handles the normal case of zeroing the remainder of the last block in
 * the file and the unusual case of zeroing blocks out beyond the size of the
 * file.  This second case only happens with fixed size extents and when the
 * system crashes before the inode size was updated but after blocks were
 * allocated.
 *
 * Expects the iolock to be held exclusive, and will take the ilock internally.
 */
int					/* error (positive) */
xfs_zero_eof(
	struct xfs_inode	*ip,
	xfs_off_t		offset,		/* starting I/O offset */
	xfs_fsize_t		isize,		/* current inode size */
	bool			*did_zeroing)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		start_zero_fsb;
	xfs_fileoff_t		end_zero_fsb;
	xfs_fileoff_t		zero_count_fsb;
	xfs_fileoff_t		last_fsb;
	xfs_fileoff_t		zero_off;
	xfs_fsize_t		zero_len;
	int			nimaps;
	int			error = 0;
	struct xfs_bmbt_irec	imap;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT(offset > isize);

	trace_xfs_zero_eof(ip, isize, offset - isize);

	/*
	 * First handle zeroing the block on which isize resides.
	 *
	 * We only zero a part of that block so it is handled specially.
	 */
	if (XFS_B_FSB_OFFSET(mp, isize) != 0) {
		error = xfs_zero_last_block(ip, offset, isize, did_zeroing);
		if (error)
			return error;
	}

	/*
	 * Calculate the range between the new size and the old where blocks
	 * needing to be zeroed may exist.
	 *
	 * To get the block where the last byte in the file currently resides,
	 * we need to subtract one from the size and truncate back to a block
	 * boundary.  We subtract 1 in case the size is exactly on a block
	 * boundary.
	 */
	last_fsb = isize ? XFS_B_TO_FSBT(mp, isize - 1) : (xfs_fileoff_t)-1;
	start_zero_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)isize);
	end_zero_fsb = XFS_B_TO_FSBT(mp, offset - 1);
	ASSERT((xfs_sfiloff_t)last_fsb < (xfs_sfiloff_t)start_zero_fsb);
	if (last_fsb == end_zero_fsb) {
		/*
		 * The size was only incremented on its last block.
		 * We took care of that above, so just return.
		 */
		return 0;
	}

	ASSERT(start_zero_fsb <= end_zero_fsb);
	while (start_zero_fsb <= end_zero_fsb) {
		nimaps = 1;
		zero_count_fsb = end_zero_fsb - start_zero_fsb + 1;

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_bmapi_read(ip, start_zero_fsb, zero_count_fsb,
					  &imap, &nimaps, 0);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return error;

		ASSERT(nimaps > 0);

		if (imap.br_state == XFS_EXT_UNWRITTEN ||
		    imap.br_startblock == HOLESTARTBLOCK) {
			start_zero_fsb = imap.br_startoff + imap.br_blockcount;
			ASSERT(start_zero_fsb <= (end_zero_fsb + 1));
			continue;
		}

		/*
		 * There are blocks we need to zero.
		 */
		zero_off = XFS_FSB_TO_B(mp, start_zero_fsb);
		zero_len = XFS_FSB_TO_B(mp, imap.br_blockcount);

		if ((zero_off + zero_len) > offset)
			zero_len = offset - zero_off;

		error = xfs_iozero(ip, zero_off, zero_len);
		if (error)
			return error;

		*did_zeroing = true;
		start_zero_fsb = imap.br_startoff + imap.br_blockcount;
		ASSERT(start_zero_fsb <= (end_zero_fsb + 1));
	}

	return 0;
}

/*
 * Common pre-write limit and setup checks.
 *
 * Called with the iolocked held either shared and exclusive according to
 * @iolock, and returns with it held.  Might upgrade the iolock to exclusive
 * if called for a direct write beyond i_size.
 */
STATIC ssize_t
xfs_file_aio_write_checks(
	struct kiocb		*iocb,
	struct iov_iter		*from,
	int			*iolock)
{
	struct file		*file = iocb->ki_filp;
	struct inode		*inode = file->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	ssize_t			error = 0;
	size_t			count = iov_iter_count(from);
	bool			drained_dio = false;

restart:
	error = generic_write_checks(iocb, from);
	if (error <= 0)
		return error;

	error = xfs_break_layouts(inode, iolock, true);
	if (error)
		return error;

	/* For changing security info in file_remove_privs() we need i_mutex */
	if (*iolock == XFS_IOLOCK_SHARED && !IS_NOSEC(inode)) {
		xfs_rw_iunlock(ip, *iolock);
		*iolock = XFS_IOLOCK_EXCL;
		xfs_rw_ilock(ip, *iolock);
		goto restart;
	}
	/*
	 * If the offset is beyond the size of the file, we need to zero any
	 * blocks that fall between the existing EOF and the start of this
	 * write.  If zeroing is needed and we are currently holding the
	 * iolock shared, we need to update it to exclusive which implies
	 * having to redo all checks before.
	 *
	 * We need to serialise against EOF updates that occur in IO
	 * completions here. We want to make sure that nobody is changing the
	 * size while we do this check until we have placed an IO barrier (i.e.
	 * hold the XFS_IOLOCK_EXCL) that prevents new IO from being dispatched.
	 * The spinlock effectively forms a memory barrier once we have the
	 * XFS_IOLOCK_EXCL so we are guaranteed to see the latest EOF value
	 * and hence be able to correctly determine if we need to run zeroing.
	 */
	spin_lock(&ip->i_flags_lock);
	if (iocb->ki_pos > i_size_read(inode)) {
		bool	zero = false;

		spin_unlock(&ip->i_flags_lock);
		if (!drained_dio) {
			if (*iolock == XFS_IOLOCK_SHARED) {
				xfs_rw_iunlock(ip, *iolock);
				*iolock = XFS_IOLOCK_EXCL;
				xfs_rw_ilock(ip, *iolock);
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
		error = xfs_zero_eof(ip, iocb->ki_pos, i_size_read(inode), &zero);
		if (error)
			return error;
	} else
		spin_unlock(&ip->i_flags_lock);

	/*
	 * Updating the timestamps will grab the ilock again from
	 * xfs_fs_dirty_inode, so we have to call it after dropping the
	 * lock above.  Eventually we should look into a way to avoid
	 * the pointless lock roundtrip.
	 */
	if (likely(!(file->f_mode & FMODE_NOCMTIME))) {
		error = file_update_time(file);
		if (error)
			return error;
	}

	/*
	 * If we're writing the file then make sure to clear the setuid and
	 * setgid bits if the process is not being run by root.  This keeps
	 * people from modifying setuid and setgid binaries.
	 */
	if (!IS_NOSEC(inode))
		return file_remove_privs(file);
	return 0;
}

/*
 * xfs_file_dio_aio_write - handle direct IO writes
 *
 * Lock the inode appropriately to prepare for and issue a direct IO write.
 * By separating it from the buffered write path we remove all the tricky to
 * follow locking changes and looping.
 *
 * If there are cached pages or we're extending the file, we need IOLOCK_EXCL
 * until we're sure the bytes at the new EOF have been zeroed and/or the cached
 * pages are flushed out.
 *
 * In most cases the direct IO writes will be done holding IOLOCK_SHARED
 * allowing them to be done in parallel with reads and other direct IO writes.
 * However, if the IO is not aligned to filesystem blocks, the direct IO layer
 * needs to do sub-block zeroing and that requires serialisation against other
 * direct IOs to the same block. In this case we need to serialise the
 * submission of the unaligned IOs so that we don't get racing block zeroing in
 * the dio layer.  To avoid the problem with aio, we also need to wait for
 * outstanding IOs to complete so that unwritten extent conversion is completed
 * before we try to map the overlapping block. This is currently implemented by
 * hitting it with a big hammer (i.e. inode_dio_wait()).
 *
 * Returns with locks held indicated by @iolock and errors indicated by
 * negative return values.
 */
STATIC ssize_t
xfs_file_dio_aio_write(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct file		*file = iocb->ki_filp;
	struct address_space	*mapping = file->f_mapping;
	struct inode		*inode = mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			ret = 0;
	int			unaligned_io = 0;
	int			iolock;
	size_t			count = iov_iter_count(from);
	loff_t			end;
	struct iov_iter		data;
	struct xfs_buftarg	*target = XFS_IS_REALTIME_INODE(ip) ?
					mp->m_rtdev_targp : mp->m_ddev_targp;

	/* DIO must be aligned to device logical sector size */
	if (!IS_DAX(inode) &&
	    ((iocb->ki_pos | count) & target->bt_logical_sectormask))
		return -EINVAL;

	/* "unaligned" here means not aligned to a filesystem block */
	if ((iocb->ki_pos & mp->m_blockmask) ||
	    ((iocb->ki_pos + count) & mp->m_blockmask))
		unaligned_io = 1;

	/*
	 * We don't need to take an exclusive lock unless there page cache needs
	 * to be invalidated or unaligned IO is being executed. We don't need to
	 * consider the EOF extension case here because
	 * xfs_file_aio_write_checks() will relock the inode as necessary for
	 * EOF zeroing cases and fill out the new inode size as appropriate.
	 */
	if (unaligned_io || mapping->nrpages)
		iolock = XFS_IOLOCK_EXCL;
	else
		iolock = XFS_IOLOCK_SHARED;
	xfs_rw_ilock(ip, iolock);

	/*
	 * Recheck if there are cached pages that need invalidate after we got
	 * the iolock to protect against other threads adding new pages while
	 * we were waiting for the iolock.
	 */
	if (mapping->nrpages && iolock == XFS_IOLOCK_SHARED) {
		xfs_rw_iunlock(ip, iolock);
		iolock = XFS_IOLOCK_EXCL;
		xfs_rw_ilock(ip, iolock);
	}

	ret = xfs_file_aio_write_checks(iocb, from, &iolock);
	if (ret)
		goto out;
	count = iov_iter_count(from);
	end = iocb->ki_pos + count - 1;

	/*
	 * See xfs_file_dio_aio_read() for why we do a full-file flush here.
	 */
	if (mapping->nrpages) {
		ret = filemap_write_and_wait(VFS_I(ip)->i_mapping);
		if (ret)
			goto out;
		/*
		 * Invalidate whole pages. This can return an error if we fail
		 * to invalidate a page, but this should never happen on XFS.
		 * Warn if it does fail.
		 */
		ret = invalidate_inode_pages2(VFS_I(ip)->i_mapping);
		WARN_ON_ONCE(ret);
		ret = 0;
	}

	/*
	 * If we are doing unaligned IO, wait for all other IO to drain,
	 * otherwise demote the lock if we had to flush cached pages
	 */
	if (unaligned_io)
		inode_dio_wait(inode);
	else if (iolock == XFS_IOLOCK_EXCL) {
		xfs_rw_ilock_demote(ip, XFS_IOLOCK_EXCL);
		iolock = XFS_IOLOCK_SHARED;
	}

	trace_xfs_file_direct_write(ip, count, iocb->ki_pos);

	data = *from;
	ret = mapping->a_ops->direct_IO(iocb, &data);

	/* see generic_file_direct_write() for why this is necessary */
	if (mapping->nrpages) {
		invalidate_inode_pages2_range(mapping,
					      iocb->ki_pos >> PAGE_SHIFT,
					      end >> PAGE_SHIFT);
	}

	if (ret > 0) {
		iocb->ki_pos += ret;
		iov_iter_advance(from, ret);
	}
out:
	xfs_rw_iunlock(ip, iolock);

	/*
	 * No fallback to buffered IO on errors for XFS. DAX can result in
	 * partial writes, but direct IO will either complete fully or fail.
	 */
	ASSERT(ret < 0 || ret == count || IS_DAX(VFS_I(ip)));
	return ret;
}

STATIC ssize_t
xfs_file_buffered_aio_write(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct file		*file = iocb->ki_filp;
	struct address_space	*mapping = file->f_mapping;
	struct inode		*inode = mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	ssize_t			ret;
	int			enospc = 0;
	int			iolock = XFS_IOLOCK_EXCL;

	xfs_rw_ilock(ip, iolock);

	ret = xfs_file_aio_write_checks(iocb, from, &iolock);
	if (ret)
		goto out;

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = inode_to_bdi(inode);

write_retry:
	trace_xfs_file_buffered_write(ip, iov_iter_count(from), iocb->ki_pos);
	ret = generic_perform_write(file, from, iocb->ki_pos);
	if (likely(ret >= 0))
		iocb->ki_pos += ret;

	/*
	 * If we hit a space limit, try to free up some lingering preallocated
	 * space before returning an error. In the case of ENOSPC, first try to
	 * write back all dirty inodes to free up some of the excess reserved
	 * metadata space. This reduces the chances that the eofblocks scan
	 * waits on dirty mappings. Since xfs_flush_inodes() is serialized, this
	 * also behaves as a filter to prevent too many eofblocks scans from
	 * running at the same time.
	 */
	if (ret == -EDQUOT && !enospc) {
		enospc = xfs_inode_free_quota_eofblocks(ip);
		if (enospc)
			goto write_retry;
	} else if (ret == -ENOSPC && !enospc) {
		struct xfs_eofblocks eofb = {0};

		enospc = 1;
		xfs_flush_inodes(ip->i_mount);
		eofb.eof_scan_owner = ip->i_ino; /* for locking */
		eofb.eof_flags = XFS_EOF_FLAGS_SYNC;
		xfs_icache_free_eofblocks(ip->i_mount, &eofb);
		goto write_retry;
	}

	current->backing_dev_info = NULL;
out:
	xfs_rw_iunlock(ip, iolock);
	return ret;
}

STATIC ssize_t
xfs_file_write_iter(
	struct kiocb		*iocb,
	struct iov_iter		*from)
{
	struct file		*file = iocb->ki_filp;
	struct address_space	*mapping = file->f_mapping;
	struct inode		*inode = mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	ssize_t			ret;
	size_t			ocount = iov_iter_count(from);

	XFS_STATS_INC(ip->i_mount, xs_write_calls);

	if (ocount == 0)
		return 0;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	if ((iocb->ki_flags & IOCB_DIRECT) || IS_DAX(inode))
		ret = xfs_file_dio_aio_write(iocb, from);
	else
		ret = xfs_file_buffered_aio_write(iocb, from);

	if (ret > 0) {
		XFS_STATS_ADD(ip->i_mount, xs_write_bytes, ret);

		/* Handle various SYNC-type writes */
		ret = generic_write_sync(iocb, ret);
	}
	return ret;
}

#define	XFS_FALLOC_FL_SUPPORTED						\
		(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |		\
		 FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |	\
		 FALLOC_FL_INSERT_RANGE)

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
	enum xfs_prealloc_flags	flags = 0;
	uint			iolock = XFS_IOLOCK_EXCL;
	loff_t			new_size = 0;
	bool			do_file_insert = 0;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;
	if (mode & ~XFS_FALLOC_FL_SUPPORTED)
		return -EOPNOTSUPP;

	xfs_ilock(ip, iolock);
	error = xfs_break_layouts(inode, &iolock, false);
	if (error)
		goto out_unlock;

	xfs_ilock(ip, XFS_MMAPLOCK_EXCL);
	iolock |= XFS_MMAPLOCK_EXCL;

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		error = xfs_free_file_space(ip, offset, len);
		if (error)
			goto out_unlock;
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		unsigned blksize_mask = (1 << inode->i_blkbits) - 1;

		if (offset & blksize_mask || len & blksize_mask) {
			error = -EINVAL;
			goto out_unlock;
		}

		/*
		 * There is no need to overlap collapse range with EOF,
		 * in which case it is effectively a truncate operation
		 */
		if (offset + len >= i_size_read(inode)) {
			error = -EINVAL;
			goto out_unlock;
		}

		new_size = i_size_read(inode) - len;

		error = xfs_collapse_file_space(ip, offset, len);
		if (error)
			goto out_unlock;
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		unsigned blksize_mask = (1 << inode->i_blkbits) - 1;

		new_size = i_size_read(inode) + len;
		if (offset & blksize_mask || len & blksize_mask) {
			error = -EINVAL;
			goto out_unlock;
		}

		/* check the new inode size does not wrap through zero */
		if (new_size > inode->i_sb->s_maxbytes) {
			error = -EFBIG;
			goto out_unlock;
		}

		/* Offset should be less than i_size */
		if (offset >= i_size_read(inode)) {
			error = -EINVAL;
			goto out_unlock;
		}
		do_file_insert = 1;
	} else {
		flags |= XFS_PREALLOC_SET;

		if (!(mode & FALLOC_FL_KEEP_SIZE) &&
		    offset + len > i_size_read(inode)) {
			new_size = offset + len;
			error = inode_newsize_ok(inode, new_size);
			if (error)
				goto out_unlock;
		}

		if (mode & FALLOC_FL_ZERO_RANGE)
			error = xfs_zero_file_space(ip, offset, len);
		else
			error = xfs_alloc_file_space(ip, offset, len,
						     XFS_BMAPI_PREALLOC);
		if (error)
			goto out_unlock;
	}

	if (file->f_flags & O_DSYNC)
		flags |= XFS_PREALLOC_SYNC;

	error = xfs_update_prealloc_flags(ip, flags);
	if (error)
		goto out_unlock;

	/* Change file size if needed */
	if (new_size) {
		struct iattr iattr;

		iattr.ia_valid = ATTR_SIZE;
		iattr.ia_size = new_size;
		error = xfs_setattr_size(ip, &iattr);
		if (error)
			goto out_unlock;
	}

	/*
	 * Perform hole insertion now that the file size has been
	 * updated so that if we crash during the operation we don't
	 * leave shifted extents past EOF and hence losing access to
	 * the data that is contained within them.
	 */
	if (do_file_insert)
		error = xfs_insert_file_space(ip, offset, len);

out_unlock:
	xfs_iunlock(ip, iolock);
	return error;
}


STATIC int
xfs_file_open(
	struct inode	*inode,
	struct file	*file)
{
	if (!(file->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EFBIG;
	if (XFS_FORCED_SHUTDOWN(XFS_M(inode->i_sb)))
		return -EIO;
	return 0;
}

STATIC int
xfs_dir_open(
	struct inode	*inode,
	struct file	*file)
{
	struct xfs_inode *ip = XFS_I(inode);
	int		mode;
	int		error;

	error = xfs_file_open(inode, file);
	if (error)
		return error;

	/*
	 * If there are any blocks, read-ahead block 0 as we're almost
	 * certain to have the next operation be a read there.
	 */
	mode = xfs_ilock_data_map_shared(ip);
	if (ip->i_d.di_nextents > 0)
		xfs_dir3_data_readahead(ip, 0, -1);
	xfs_iunlock(ip, mode);
	return 0;
}

STATIC int
xfs_file_release(
	struct inode	*inode,
	struct file	*filp)
{
	return xfs_release(XFS_I(inode));
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
	bufsize = (size_t)min_t(loff_t, 32768, ip->i_d.di_size);

	return xfs_readdir(ip, ctx, bufsize);
}

/*
 * This type is designed to indicate the type of offset we would like
 * to search from page cache for xfs_seek_hole_data().
 */
enum {
	HOLE_OFF = 0,
	DATA_OFF,
};

/*
 * Lookup the desired type of offset from the given page.
 *
 * On success, return true and the offset argument will point to the
 * start of the region that was found.  Otherwise this function will
 * return false and keep the offset argument unchanged.
 */
STATIC bool
xfs_lookup_buffer_offset(
	struct page		*page,
	loff_t			*offset,
	unsigned int		type)
{
	loff_t			lastoff = page_offset(page);
	bool			found = false;
	struct buffer_head	*bh, *head;

	bh = head = page_buffers(page);
	do {
		/*
		 * Unwritten extents that have data in the page
		 * cache covering them can be identified by the
		 * BH_Unwritten state flag.  Pages with multiple
		 * buffers might have a mix of holes, data and
		 * unwritten extents - any buffer with valid
		 * data in it should have BH_Uptodate flag set
		 * on it.
		 */
		if (buffer_unwritten(bh) ||
		    buffer_uptodate(bh)) {
			if (type == DATA_OFF)
				found = true;
		} else {
			if (type == HOLE_OFF)
				found = true;
		}

		if (found) {
			*offset = lastoff;
			break;
		}
		lastoff += bh->b_size;
	} while ((bh = bh->b_this_page) != head);

	return found;
}

/*
 * This routine is called to find out and return a data or hole offset
 * from the page cache for unwritten extents according to the desired
 * type for xfs_seek_hole_data().
 *
 * The argument offset is used to tell where we start to search from the
 * page cache.  Map is used to figure out the end points of the range to
 * lookup pages.
 *
 * Return true if the desired type of offset was found, and the argument
 * offset is filled with that address.  Otherwise, return false and keep
 * offset unchanged.
 */
STATIC bool
xfs_find_get_desired_pgoff(
	struct inode		*inode,
	struct xfs_bmbt_irec	*map,
	unsigned int		type,
	loff_t			*offset)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct pagevec		pvec;
	pgoff_t			index;
	pgoff_t			end;
	loff_t			endoff;
	loff_t			startoff = *offset;
	loff_t			lastoff = startoff;
	bool			found = false;

	pagevec_init(&pvec, 0);

	index = startoff >> PAGE_SHIFT;
	endoff = XFS_FSB_TO_B(mp, map->br_startoff + map->br_blockcount);
	end = endoff >> PAGE_SHIFT;
	do {
		int		want;
		unsigned	nr_pages;
		unsigned int	i;

		want = min_t(pgoff_t, end - index, PAGEVEC_SIZE);
		nr_pages = pagevec_lookup(&pvec, inode->i_mapping, index,
					  want);
		/*
		 * No page mapped into given range.  If we are searching holes
		 * and if this is the first time we got into the loop, it means
		 * that the given offset is landed in a hole, return it.
		 *
		 * If we have already stepped through some block buffers to find
		 * holes but they all contains data.  In this case, the last
		 * offset is already updated and pointed to the end of the last
		 * mapped page, if it does not reach the endpoint to search,
		 * that means there should be a hole between them.
		 */
		if (nr_pages == 0) {
			/* Data search found nothing */
			if (type == DATA_OFF)
				break;

			ASSERT(type == HOLE_OFF);
			if (lastoff == startoff || lastoff < endoff) {
				found = true;
				*offset = lastoff;
			}
			break;
		}

		/*
		 * At lease we found one page.  If this is the first time we
		 * step into the loop, and if the first page index offset is
		 * greater than the given search offset, a hole was found.
		 */
		if (type == HOLE_OFF && lastoff == startoff &&
		    lastoff < page_offset(pvec.pages[0])) {
			found = true;
			break;
		}

		for (i = 0; i < nr_pages; i++) {
			struct page	*page = pvec.pages[i];
			loff_t		b_offset;

			/*
			 * At this point, the page may be truncated or
			 * invalidated (changing page->mapping to NULL),
			 * or even swizzled back from swapper_space to tmpfs
			 * file mapping. However, page->index will not change
			 * because we have a reference on the page.
			 *
			 * Searching done if the page index is out of range.
			 * If the current offset is not reaches the end of
			 * the specified search range, there should be a hole
			 * between them.
			 */
			if (page->index > end) {
				if (type == HOLE_OFF && lastoff < endoff) {
					*offset = lastoff;
					found = true;
				}
				goto out;
			}

			lock_page(page);
			/*
			 * Page truncated or invalidated(page->mapping == NULL).
			 * We can freely skip it and proceed to check the next
			 * page.
			 */
			if (unlikely(page->mapping != inode->i_mapping)) {
				unlock_page(page);
				continue;
			}

			if (!page_has_buffers(page)) {
				unlock_page(page);
				continue;
			}

			found = xfs_lookup_buffer_offset(page, &b_offset, type);
			if (found) {
				/*
				 * The found offset may be less than the start
				 * point to search if this is the first time to
				 * come here.
				 */
				*offset = max_t(loff_t, startoff, b_offset);
				unlock_page(page);
				goto out;
			}

			/*
			 * We either searching data but nothing was found, or
			 * searching hole but found a data buffer.  In either
			 * case, probably the next page contains the desired
			 * things, update the last offset to it so.
			 */
			lastoff = page_offset(page) + PAGE_SIZE;
			unlock_page(page);
		}

		/*
		 * The number of returned pages less than our desired, search
		 * done.  In this case, nothing was found for searching data,
		 * but we found a hole behind the last offset.
		 */
		if (nr_pages < want) {
			if (type == HOLE_OFF) {
				*offset = lastoff;
				found = true;
			}
			break;
		}

		index = pvec.pages[i - 1]->index + 1;
		pagevec_release(&pvec);
	} while (index <= end);

out:
	pagevec_release(&pvec);
	return found;
}

/*
 * caller must lock inode with xfs_ilock_data_map_shared,
 * can we craft an appropriate ASSERT?
 *
 * end is because the VFS-level lseek interface is defined such that any
 * offset past i_size shall return -ENXIO, but we use this for quota code
 * which does not maintain i_size, and we want to SEEK_DATA past i_size.
 */
loff_t
__xfs_seek_hole_data(
	struct inode		*inode,
	loff_t			start,
	loff_t			end,
	int			whence)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	loff_t			uninitialized_var(offset);
	xfs_fileoff_t		fsbno;
	xfs_filblks_t		lastbno;
	int			error;

	if (start >= end) {
		error = -ENXIO;
		goto out_error;
	}

	/*
	 * Try to read extents from the first block indicated
	 * by fsbno to the end block of the file.
	 */
	fsbno = XFS_B_TO_FSBT(mp, start);
	lastbno = XFS_B_TO_FSB(mp, end);

	for (;;) {
		struct xfs_bmbt_irec	map[2];
		int			nmap = 2;
		unsigned int		i;

		error = xfs_bmapi_read(ip, fsbno, lastbno - fsbno, map, &nmap,
				       XFS_BMAPI_ENTIRE);
		if (error)
			goto out_error;

		/* No extents at given offset, must be beyond EOF */
		if (nmap == 0) {
			error = -ENXIO;
			goto out_error;
		}

		for (i = 0; i < nmap; i++) {
			offset = max_t(loff_t, start,
				       XFS_FSB_TO_B(mp, map[i].br_startoff));

			/* Landed in the hole we wanted? */
			if (whence == SEEK_HOLE &&
			    map[i].br_startblock == HOLESTARTBLOCK)
				goto out;

			/* Landed in the data extent we wanted? */
			if (whence == SEEK_DATA &&
			    (map[i].br_startblock == DELAYSTARTBLOCK ||
			     (map[i].br_state == XFS_EXT_NORM &&
			      !isnullstartblock(map[i].br_startblock))))
				goto out;

			/*
			 * Landed in an unwritten extent, try to search
			 * for hole or data from page cache.
			 */
			if (map[i].br_state == XFS_EXT_UNWRITTEN) {
				if (xfs_find_get_desired_pgoff(inode, &map[i],
				      whence == SEEK_HOLE ? HOLE_OFF : DATA_OFF,
							&offset))
					goto out;
			}
		}

		/*
		 * We only received one extent out of the two requested. This
		 * means we've hit EOF and didn't find what we are looking for.
		 */
		if (nmap == 1) {
			/*
			 * If we were looking for a hole, set offset to
			 * the end of the file (i.e., there is an implicit
			 * hole at the end of any file).
		 	 */
			if (whence == SEEK_HOLE) {
				offset = end;
				break;
			}
			/*
			 * If we were looking for data, it's nowhere to be found
			 */
			ASSERT(whence == SEEK_DATA);
			error = -ENXIO;
			goto out_error;
		}

		ASSERT(i > 1);

		/*
		 * Nothing was found, proceed to the next round of search
		 * if the next reading offset is not at or beyond EOF.
		 */
		fsbno = map[i - 1].br_startoff + map[i - 1].br_blockcount;
		start = XFS_FSB_TO_B(mp, fsbno);
		if (start >= end) {
			if (whence == SEEK_HOLE) {
				offset = end;
				break;
			}
			ASSERT(whence == SEEK_DATA);
			error = -ENXIO;
			goto out_error;
		}
	}

out:
	/*
	 * If at this point we have found the hole we wanted, the returned
	 * offset may be bigger than the file size as it may be aligned to
	 * page boundary for unwritten extents.  We need to deal with this
	 * situation in particular.
	 */
	if (whence == SEEK_HOLE)
		offset = min_t(loff_t, offset, end);

	return offset;

out_error:
	return error;
}

STATIC loff_t
xfs_seek_hole_data(
	struct file		*file,
	loff_t			start,
	int			whence)
{
	struct inode		*inode = file->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	uint			lock;
	loff_t			offset, end;
	int			error = 0;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	lock = xfs_ilock_data_map_shared(ip);

	end = i_size_read(inode);
	offset = __xfs_seek_hole_data(inode, start, end, whence);
	if (offset < 0) {
		error = offset;
		goto out_unlock;
	}

	offset = vfs_setpos(file, offset, inode->i_sb->s_maxbytes);

out_unlock:
	xfs_iunlock(ip, lock);

	if (error)
		return error;
	return offset;
}

STATIC loff_t
xfs_file_llseek(
	struct file	*file,
	loff_t		offset,
	int		whence)
{
	switch (whence) {
	case SEEK_END:
	case SEEK_CUR:
	case SEEK_SET:
		return generic_file_llseek(file, offset, whence);
	case SEEK_HOLE:
	case SEEK_DATA:
		return xfs_seek_hole_data(file, offset, whence);
	default:
		return -EINVAL;
	}
}

/*
 * Locking for serialisation of IO during page faults. This results in a lock
 * ordering of:
 *
 * mmap_sem (MM)
 *   sb_start_pagefault(vfs, freeze)
 *     i_mmaplock (XFS - truncate serialisation)
 *       page_lock (MM)
 *         i_lock (XFS - extent map serialisation)
 */

/*
 * mmap()d file has taken write protection fault and is being made writable. We
 * can set the page state up correctly for a writable page, which means we can
 * do correct delalloc accounting (ENOSPC checking!) and unwritten extent
 * mapping.
 */
STATIC int
xfs_filemap_page_mkwrite(
	struct vm_area_struct	*vma,
	struct vm_fault		*vmf)
{
	struct inode		*inode = file_inode(vma->vm_file);
	int			ret;

	trace_xfs_filemap_page_mkwrite(XFS_I(inode));

	sb_start_pagefault(inode->i_sb);
	file_update_time(vma->vm_file);
	xfs_ilock(XFS_I(inode), XFS_MMAPLOCK_SHARED);

	if (IS_DAX(inode)) {
		ret = __dax_mkwrite(vma, vmf, xfs_get_blocks_dax_fault);
	} else {
		ret = block_page_mkwrite(vma, vmf, xfs_get_blocks);
		ret = block_page_mkwrite_return(ret);
	}

	xfs_iunlock(XFS_I(inode), XFS_MMAPLOCK_SHARED);
	sb_end_pagefault(inode->i_sb);

	return ret;
}

STATIC int
xfs_filemap_fault(
	struct vm_area_struct	*vma,
	struct vm_fault		*vmf)
{
	struct inode		*inode = file_inode(vma->vm_file);
	int			ret;

	trace_xfs_filemap_fault(XFS_I(inode));

	/* DAX can shortcut the normal fault path on write faults! */
	if ((vmf->flags & FAULT_FLAG_WRITE) && IS_DAX(inode))
		return xfs_filemap_page_mkwrite(vma, vmf);

	xfs_ilock(XFS_I(inode), XFS_MMAPLOCK_SHARED);
	if (IS_DAX(inode)) {
		/*
		 * we do not want to trigger unwritten extent conversion on read
		 * faults - that is unnecessary overhead and would also require
		 * changes to xfs_get_blocks_direct() to map unwritten extent
		 * ioend for conversion on read-only mappings.
		 */
		ret = __dax_fault(vma, vmf, xfs_get_blocks_dax_fault);
	} else
		ret = filemap_fault(vma, vmf);
	xfs_iunlock(XFS_I(inode), XFS_MMAPLOCK_SHARED);

	return ret;
}

/*
 * Similar to xfs_filemap_fault(), the DAX fault path can call into here on
 * both read and write faults. Hence we need to handle both cases. There is no
 * ->pmd_mkwrite callout for huge pages, so we have a single function here to
 * handle both cases here. @flags carries the information on the type of fault
 * occuring.
 */
STATIC int
xfs_filemap_pmd_fault(
	struct vm_area_struct	*vma,
	unsigned long		addr,
	pmd_t			*pmd,
	unsigned int		flags)
{
	struct inode		*inode = file_inode(vma->vm_file);
	struct xfs_inode	*ip = XFS_I(inode);
	int			ret;

	if (!IS_DAX(inode))
		return VM_FAULT_FALLBACK;

	trace_xfs_filemap_pmd_fault(ip);

	if (flags & FAULT_FLAG_WRITE) {
		sb_start_pagefault(inode->i_sb);
		file_update_time(vma->vm_file);
	}

	xfs_ilock(XFS_I(inode), XFS_MMAPLOCK_SHARED);
	ret = __dax_pmd_fault(vma, addr, pmd, flags, xfs_get_blocks_dax_fault);
	xfs_iunlock(XFS_I(inode), XFS_MMAPLOCK_SHARED);

	if (flags & FAULT_FLAG_WRITE)
		sb_end_pagefault(inode->i_sb);

	return ret;
}

/*
 * pfn_mkwrite was originally inteneded to ensure we capture time stamp
 * updates on write faults. In reality, it's need to serialise against
 * truncate similar to page_mkwrite. Hence we cycle the XFS_MMAPLOCK_SHARED
 * to ensure we serialise the fault barrier in place.
 */
static int
xfs_filemap_pfn_mkwrite(
	struct vm_area_struct	*vma,
	struct vm_fault		*vmf)
{

	struct inode		*inode = file_inode(vma->vm_file);
	struct xfs_inode	*ip = XFS_I(inode);
	int			ret = VM_FAULT_NOPAGE;
	loff_t			size;

	trace_xfs_filemap_pfn_mkwrite(ip);

	sb_start_pagefault(inode->i_sb);
	file_update_time(vma->vm_file);

	/* check if the faulting page hasn't raced with truncate */
	xfs_ilock(ip, XFS_MMAPLOCK_SHARED);
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (vmf->pgoff >= size)
		ret = VM_FAULT_SIGBUS;
	else if (IS_DAX(inode))
		ret = dax_pfn_mkwrite(vma, vmf);
	xfs_iunlock(ip, XFS_MMAPLOCK_SHARED);
	sb_end_pagefault(inode->i_sb);
	return ret;

}

static const struct vm_operations_struct xfs_file_vm_ops = {
	.fault		= xfs_filemap_fault,
	.pmd_fault	= xfs_filemap_pmd_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= xfs_filemap_page_mkwrite,
	.pfn_mkwrite	= xfs_filemap_pfn_mkwrite,
};

STATIC int
xfs_file_mmap(
	struct file	*filp,
	struct vm_area_struct *vma)
{
	file_accessed(filp);
	vma->vm_ops = &xfs_file_vm_ops;
	if (IS_DAX(file_inode(filp)))
		vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	return 0;
}

const struct file_operations xfs_file_operations = {
	.llseek		= xfs_file_llseek,
	.read_iter	= xfs_file_read_iter,
	.write_iter	= xfs_file_write_iter,
	.splice_read	= xfs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
	.fallocate	= xfs_file_fallocate,
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
