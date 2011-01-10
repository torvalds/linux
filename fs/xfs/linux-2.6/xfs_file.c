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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_trans.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_vnodeops.h"
#include "xfs_da_btree.h"
#include "xfs_ioctl.h"
#include "xfs_trace.h"

#include <linux/dcache.h>

static const struct vm_operations_struct xfs_file_vm_ops;

/*
 *	xfs_iozero
 *
 *	xfs_iozero clears the specified range of buffer supplied,
 *	and marks all the affected blocks as valid and modified.  If
 *	an affected block is not allocated, it will be allocated.  If
 *	an affected block is not completely overwritten, and is not
 *	valid before the operation, it will be read from disk before
 *	being partially zeroed.
 */
STATIC int
xfs_iozero(
	struct xfs_inode	*ip,	/* inode			*/
	loff_t			pos,	/* offset in file		*/
	size_t			count)	/* size of data to zero		*/
{
	struct page		*page;
	struct address_space	*mapping;
	int			status;

	mapping = VFS_I(ip)->i_mapping;
	do {
		unsigned offset, bytes;
		void *fsdata;

		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		status = pagecache_write_begin(NULL, mapping, pos, bytes,
					AOP_FLAG_UNINTERRUPTIBLE,
					&page, &fsdata);
		if (status)
			break;

		zero_user(page, offset, bytes);

		status = pagecache_write_end(NULL, mapping, pos, bytes, bytes,
					page, fsdata);
		WARN_ON(status <= 0); /* can't return less than zero! */
		pos += bytes;
		count -= bytes;
		status = 0;
	} while (count);

	return (-status);
}

STATIC int
xfs_file_fsync(
	struct file		*file,
	int			datasync)
{
	struct inode		*inode = file->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_trans	*tp;
	int			error = 0;
	int			log_flushed = 0;

	trace_xfs_file_fsync(ip);

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -XFS_ERROR(EIO);

	xfs_iflags_clear(ip, XFS_ITRUNCATED);

	xfs_ioend_wait(ip);

	/*
	 * We always need to make sure that the required inode state is safe on
	 * disk.  The inode might be clean but we still might need to force the
	 * log because of committed transactions that haven't hit the disk yet.
	 * Likewise, there could be unflushed non-transactional changes to the
	 * inode core that have to go to disk and this requires us to issue
	 * a synchronous transaction to capture these changes correctly.
	 *
	 * This code relies on the assumption that if the i_update_core field
	 * of the inode is clear and the inode is unpinned then it is clean
	 * and no action is required.
	 */
	xfs_ilock(ip, XFS_ILOCK_SHARED);

	/*
	 * First check if the VFS inode is marked dirty.  All the dirtying
	 * of non-transactional updates no goes through mark_inode_dirty*,
	 * which allows us to distinguish beteeen pure timestamp updates
	 * and i_size updates which need to be caught for fdatasync.
	 * After that also theck for the dirty state in the XFS inode, which
	 * might gets cleared when the inode gets written out via the AIL
	 * or xfs_iflush_cluster.
	 */
	if (((inode->i_state & I_DIRTY_DATASYNC) ||
	    ((inode->i_state & I_DIRTY_SYNC) && !datasync)) &&
	    ip->i_update_core) {
		/*
		 * Kick off a transaction to log the inode core to get the
		 * updates.  The sync transaction will also force the log.
		 */
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		tp = xfs_trans_alloc(ip->i_mount, XFS_TRANS_FSYNC_TS);
		error = xfs_trans_reserve(tp, 0,
				XFS_FSYNC_TS_LOG_RES(ip->i_mount), 0, 0, 0);
		if (error) {
			xfs_trans_cancel(tp, 0);
			return -error;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		/*
		 * Note - it's possible that we might have pushed ourselves out
		 * of the way during trans_reserve which would flush the inode.
		 * But there's no guarantee that the inode buffer has actually
		 * gone out yet (it's delwri).	Plus the buffer could be pinned
		 * anyway if it's part of an inode in another recent
		 * transaction.	 So we play it safe and fire off the
		 * transaction anyway.
		 */
		xfs_trans_ijoin(tp, ip);
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		xfs_trans_set_sync(tp);
		error = _xfs_trans_commit(tp, 0, &log_flushed);

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	} else {
		/*
		 * Timestamps/size haven't changed since last inode flush or
		 * inode transaction commit.  That means either nothing got
		 * written or a transaction committed which caught the updates.
		 * If the latter happened and the transaction hasn't hit the
		 * disk yet, the inode will be still be pinned.  If it is,
		 * force the log.
		 */
		if (xfs_ipincount(ip)) {
			error = _xfs_log_force_lsn(ip->i_mount,
					ip->i_itemp->ili_last_lsn,
					XFS_LOG_SYNC, &log_flushed);
		}
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
	}

	if (ip->i_mount->m_flags & XFS_MOUNT_BARRIER) {
		/*
		 * If the log write didn't issue an ordered tag we need
		 * to flush the disk cache for the data device now.
		 */
		if (!log_flushed)
			xfs_blkdev_issue_flush(ip->i_mount->m_ddev_targp);

		/*
		 * If this inode is on the RT dev we need to flush that
		 * cache as well.
		 */
		if (XFS_IS_REALTIME_INODE(ip))
			xfs_blkdev_issue_flush(ip->i_mount->m_rtdev_targp);
	}

	return -error;
}

STATIC ssize_t
xfs_file_aio_read(
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned long		nr_segs,
	loff_t			pos)
{
	struct file		*file = iocb->ki_filp;
	struct inode		*inode = file->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	size_t			size = 0;
	ssize_t			ret = 0;
	int			ioflags = 0;
	xfs_fsize_t		n;
	unsigned long		seg;

	XFS_STATS_INC(xs_read_calls);

	BUG_ON(iocb->ki_pos != pos);

	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;
	if (file->f_mode & FMODE_NOCMTIME)
		ioflags |= IO_INVIS;

	/* START copy & waste from filemap.c */
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iovp[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		size += iv->iov_len;
		if (unlikely((ssize_t)(size|iv->iov_len) < 0))
			return XFS_ERROR(-EINVAL);
	}
	/* END copy & waste from filemap.c */

	if (unlikely(ioflags & IO_ISDIRECT)) {
		xfs_buftarg_t	*target =
			XFS_IS_REALTIME_INODE(ip) ?
				mp->m_rtdev_targp : mp->m_ddev_targp;
		if ((iocb->ki_pos & target->bt_smask) ||
		    (size & target->bt_smask)) {
			if (iocb->ki_pos == ip->i_size)
				return 0;
			return -XFS_ERROR(EINVAL);
		}
	}

	n = XFS_MAXIOFFSET(mp) - iocb->ki_pos;
	if (n <= 0 || size == 0)
		return 0;

	if (n < size)
		size = n;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	if (unlikely(ioflags & IO_ISDIRECT))
		mutex_lock(&inode->i_mutex);
	xfs_ilock(ip, XFS_IOLOCK_SHARED);

	if (unlikely(ioflags & IO_ISDIRECT)) {
		if (inode->i_mapping->nrpages) {
			ret = -xfs_flushinval_pages(ip,
					(iocb->ki_pos & PAGE_CACHE_MASK),
					-1, FI_REMAPF_LOCKED);
		}
		mutex_unlock(&inode->i_mutex);
		if (ret) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			return ret;
		}
	}

	trace_xfs_file_read(ip, size, iocb->ki_pos, ioflags);

	ret = generic_file_aio_read(iocb, iovp, nr_segs, iocb->ki_pos);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
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
	int			ioflags = 0;
	ssize_t			ret;

	XFS_STATS_INC(xs_read_calls);

	if (infilp->f_mode & FMODE_NOCMTIME)
		ioflags |= IO_INVIS;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_SHARED);

	trace_xfs_file_splice_read(ip, count, *ppos, ioflags);

	ret = generic_file_splice_read(infilp, ppos, pipe, count, flags);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return ret;
}

STATIC void
xfs_aio_write_isize_update(
	struct inode	*inode,
	loff_t		*ppos,
	ssize_t		bytes_written)
{
	struct xfs_inode	*ip = XFS_I(inode);
	xfs_fsize_t		isize = i_size_read(inode);

	if (bytes_written > 0)
		XFS_STATS_ADD(xs_write_bytes, bytes_written);

	if (unlikely(bytes_written < 0 && bytes_written != -EFAULT &&
					*ppos > isize))
		*ppos = isize;

	if (*ppos > ip->i_size) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		if (*ppos > ip->i_size)
			ip->i_size = *ppos;
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}
}

STATIC ssize_t
xfs_file_splice_write(
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			count,
	unsigned int		flags)
{
	struct inode		*inode = outfilp->f_mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	xfs_fsize_t		new_size;
	int			ioflags = 0;
	ssize_t			ret;

	XFS_STATS_INC(xs_write_calls);

	if (outfilp->f_mode & FMODE_NOCMTIME)
		ioflags |= IO_INVIS;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	new_size = *ppos + count;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (new_size > ip->i_size)
		ip->i_new_size = new_size;
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	trace_xfs_file_splice_write(ip, count, *ppos, ioflags);

	ret = generic_file_splice_write(pipe, outfilp, ppos, count, flags);

	xfs_aio_write_isize_update(inode, ppos, ret);

	if (ip->i_new_size) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		ip->i_new_size = 0;
		if (ip->i_d.di_size > ip->i_size)
			ip->i_d.di_size = ip->i_size;
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return ret;
}

/*
 * This routine is called to handle zeroing any space in the last
 * block of the file that is beyond the EOF.  We do this since the
 * size is being increased without writing anything to that block
 * and we don't want anyone to read the garbage on the disk.
 */
STATIC int				/* error (positive) */
xfs_zero_last_block(
	xfs_inode_t	*ip,
	xfs_fsize_t	offset,
	xfs_fsize_t	isize)
{
	xfs_fileoff_t	last_fsb;
	xfs_mount_t	*mp = ip->i_mount;
	int		nimaps;
	int		zero_offset;
	int		zero_len;
	int		error = 0;
	xfs_bmbt_irec_t	imap;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	zero_offset = XFS_B_FSB_OFFSET(mp, isize);
	if (zero_offset == 0) {
		/*
		 * There are no extra bytes in the last block on disk to
		 * zero, so return.
		 */
		return 0;
	}

	last_fsb = XFS_B_TO_FSBT(mp, isize);
	nimaps = 1;
	error = xfs_bmapi(NULL, ip, last_fsb, 1, 0, NULL, 0, &imap,
			  &nimaps, NULL);
	if (error) {
		return error;
	}
	ASSERT(nimaps > 0);
	/*
	 * If the block underlying isize is just a hole, then there
	 * is nothing to zero.
	 */
	if (imap.br_startblock == HOLESTARTBLOCK) {
		return 0;
	}
	/*
	 * Zero the part of the last block beyond the EOF, and write it
	 * out sync.  We need to drop the ilock while we do this so we
	 * don't deadlock when the buffer cache calls back to us.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	zero_len = mp->m_sb.sb_blocksize - zero_offset;
	if (isize + zero_len > offset)
		zero_len = offset - isize;
	error = xfs_iozero(ip, isize, zero_len);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	ASSERT(error >= 0);
	return error;
}

/*
 * Zero any on disk space between the current EOF and the new,
 * larger EOF.  This handles the normal case of zeroing the remainder
 * of the last block in the file and the unusual case of zeroing blocks
 * out beyond the size of the file.  This second case only happens
 * with fixed size extents and when the system crashes before the inode
 * size was updated but after blocks were allocated.  If fill is set,
 * then any holes in the range are filled and zeroed.  If not, the holes
 * are left alone as holes.
 */

int					/* error (positive) */
xfs_zero_eof(
	xfs_inode_t	*ip,
	xfs_off_t	offset,		/* starting I/O offset */
	xfs_fsize_t	isize)		/* current inode size */
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	start_zero_fsb;
	xfs_fileoff_t	end_zero_fsb;
	xfs_fileoff_t	zero_count_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	zero_off;
	xfs_fsize_t	zero_len;
	int		nimaps;
	int		error = 0;
	xfs_bmbt_irec_t	imap;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_IOLOCK_EXCL));
	ASSERT(offset > isize);

	/*
	 * First handle zeroing the block on which isize resides.
	 * We only zero a part of that block so it is handled specially.
	 */
	error = xfs_zero_last_block(ip, offset, isize);
	if (error) {
		ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_IOLOCK_EXCL));
		return error;
	}

	/*
	 * Calculate the range between the new size and the old
	 * where blocks needing to be zeroed may exist.  To get the
	 * block where the last byte in the file currently resides,
	 * we need to subtract one from the size and truncate back
	 * to a block boundary.  We subtract 1 in case the size is
	 * exactly on a block boundary.
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
		error = xfs_bmapi(NULL, ip, start_zero_fsb, zero_count_fsb,
				  0, NULL, 0, &imap, &nimaps, NULL);
		if (error) {
			ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_IOLOCK_EXCL));
			return error;
		}
		ASSERT(nimaps > 0);

		if (imap.br_state == XFS_EXT_UNWRITTEN ||
		    imap.br_startblock == HOLESTARTBLOCK) {
			/*
			 * This loop handles initializing pages that were
			 * partially initialized by the code below this
			 * loop. It basically zeroes the part of the page
			 * that sits on a hole and sets the page as P_HOLE
			 * and calls remapf if it is a mapped file.
			 */
			start_zero_fsb = imap.br_startoff + imap.br_blockcount;
			ASSERT(start_zero_fsb <= (end_zero_fsb + 1));
			continue;
		}

		/*
		 * There are blocks we need to zero.
		 * Drop the inode lock while we're doing the I/O.
		 * We'll still have the iolock to protect us.
		 */
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		zero_off = XFS_FSB_TO_B(mp, start_zero_fsb);
		zero_len = XFS_FSB_TO_B(mp, imap.br_blockcount);

		if ((zero_off + zero_len) > offset)
			zero_len = offset - zero_off;

		error = xfs_iozero(ip, zero_off, zero_len);
		if (error) {
			goto out_lock;
		}

		start_zero_fsb = imap.br_startoff + imap.br_blockcount;
		ASSERT(start_zero_fsb <= (end_zero_fsb + 1));

		xfs_ilock(ip, XFS_ILOCK_EXCL);
	}

	return 0;

out_lock:
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	ASSERT(error >= 0);
	return error;
}

STATIC ssize_t
xfs_file_aio_write(
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned long		nr_segs,
	loff_t			pos)
{
	struct file		*file = iocb->ki_filp;
	struct address_space	*mapping = file->f_mapping;
	struct inode		*inode = mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			ret = 0;
	int			ioflags = 0;
	xfs_fsize_t		new_size;
	int			iolock;
	size_t			ocount = 0, count;
	int			need_i_mutex;

	XFS_STATS_INC(xs_write_calls);

	BUG_ON(iocb->ki_pos != pos);

	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;
	if (file->f_mode & FMODE_NOCMTIME)
		ioflags |= IO_INVIS;

	ret = generic_segment_checks(iovp, &nr_segs, &ocount, VERIFY_READ);
	if (ret)
		return ret;

	count = ocount;
	if (count == 0)
		return 0;

	xfs_wait_for_freeze(mp, SB_FREEZE_WRITE);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

relock:
	if (ioflags & IO_ISDIRECT) {
		iolock = XFS_IOLOCK_SHARED;
		need_i_mutex = 0;
	} else {
		iolock = XFS_IOLOCK_EXCL;
		need_i_mutex = 1;
		mutex_lock(&inode->i_mutex);
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL|iolock);

start:
	ret = generic_write_checks(file, &pos, &count,
					S_ISBLK(inode->i_mode));
	if (ret) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL|iolock);
		goto out_unlock_mutex;
	}

	if (ioflags & IO_ISDIRECT) {
		xfs_buftarg_t	*target =
			XFS_IS_REALTIME_INODE(ip) ?
				mp->m_rtdev_targp : mp->m_ddev_targp;

		if ((pos & target->bt_smask) || (count & target->bt_smask)) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL|iolock);
			return XFS_ERROR(-EINVAL);
		}

		if (!need_i_mutex && (mapping->nrpages || pos > ip->i_size)) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL|iolock);
			iolock = XFS_IOLOCK_EXCL;
			need_i_mutex = 1;
			mutex_lock(&inode->i_mutex);
			xfs_ilock(ip, XFS_ILOCK_EXCL|iolock);
			goto start;
		}
	}

	new_size = pos + count;
	if (new_size > ip->i_size)
		ip->i_new_size = new_size;

	if (likely(!(ioflags & IO_INVIS)))
		file_update_time(file);

	/*
	 * If the offset is beyond the size of the file, we have a couple
	 * of things to do. First, if there is already space allocated
	 * we need to either create holes or zero the disk or ...
	 *
	 * If there is a page where the previous size lands, we need
	 * to zero it out up to the new size.
	 */

	if (pos > ip->i_size) {
		ret = -xfs_zero_eof(ip, pos, ip->i_size);
		if (ret) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			goto out_unlock_internal;
		}
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/*
	 * If we're writing the file then make sure to clear the
	 * setuid and setgid bits if the process is not being run
	 * by root.  This keeps people from modifying setuid and
	 * setgid binaries.
	 */
	ret = file_remove_suid(file);
	if (unlikely(ret))
		goto out_unlock_internal;

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = mapping->backing_dev_info;

	if ((ioflags & IO_ISDIRECT)) {
		if (mapping->nrpages) {
			WARN_ON(need_i_mutex == 0);
			ret = -xfs_flushinval_pages(ip,
					(pos & PAGE_CACHE_MASK),
					-1, FI_REMAPF_LOCKED);
			if (ret)
				goto out_unlock_internal;
		}

		if (need_i_mutex) {
			/* demote the lock now the cached pages are gone */
			xfs_ilock_demote(ip, XFS_IOLOCK_EXCL);
			mutex_unlock(&inode->i_mutex);

			iolock = XFS_IOLOCK_SHARED;
			need_i_mutex = 0;
		}

		trace_xfs_file_direct_write(ip, count, iocb->ki_pos, ioflags);
		ret = generic_file_direct_write(iocb, iovp,
				&nr_segs, pos, &iocb->ki_pos, count, ocount);

		/*
		 * direct-io write to a hole: fall through to buffered I/O
		 * for completing the rest of the request.
		 */
		if (ret >= 0 && ret != count) {
			XFS_STATS_ADD(xs_write_bytes, ret);

			pos += ret;
			count -= ret;

			ioflags &= ~IO_ISDIRECT;
			xfs_iunlock(ip, iolock);
			goto relock;
		}
	} else {
		int enospc = 0;

write_retry:
		trace_xfs_file_buffered_write(ip, count, iocb->ki_pos, ioflags);
		ret = generic_file_buffered_write(iocb, iovp, nr_segs,
				pos, &iocb->ki_pos, count, ret);
		/*
		 * if we just got an ENOSPC, flush the inode now we
		 * aren't holding any page locks and retry *once*
		 */
		if (ret == -ENOSPC && !enospc) {
			ret = xfs_flush_pages(ip, 0, -1, 0, FI_NONE);
			if (ret)
				goto out_unlock_internal;
			enospc = 1;
			goto write_retry;
		}
	}

	current->backing_dev_info = NULL;

	xfs_aio_write_isize_update(inode, &iocb->ki_pos, ret);

	if (ret <= 0)
		goto out_unlock_internal;

	/* Handle various SYNC-type writes */
	if ((file->f_flags & O_DSYNC) || IS_SYNC(inode)) {
		loff_t end = pos + ret - 1;
		int error, error2;

		xfs_iunlock(ip, iolock);
		if (need_i_mutex)
			mutex_unlock(&inode->i_mutex);

		error = filemap_write_and_wait_range(mapping, pos, end);
		if (need_i_mutex)
			mutex_lock(&inode->i_mutex);
		xfs_ilock(ip, iolock);

		error2 = -xfs_file_fsync(file,
					 (file->f_flags & __O_SYNC) ? 0 : 1);
		if (error)
			ret = error;
		else if (error2)
			ret = error2;
	}

 out_unlock_internal:
	if (ip->i_new_size) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		ip->i_new_size = 0;
		/*
		 * If this was a direct or synchronous I/O that failed (such
		 * as ENOSPC) then part of the I/O may have been written to
		 * disk before the error occured.  In this case the on-disk
		 * file size may have been adjusted beyond the in-memory file
		 * size and now needs to be truncated back.
		 */
		if (ip->i_d.di_size > ip->i_size)
			ip->i_d.di_size = ip->i_size;
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}
	xfs_iunlock(ip, iolock);
 out_unlock_mutex:
	if (need_i_mutex)
		mutex_unlock(&inode->i_mutex);
	return ret;
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
	mode = xfs_ilock_map_shared(ip);
	if (ip->i_d.di_nextents > 0)
		xfs_da_reada_buf(NULL, ip, 0, XFS_DATA_FORK);
	xfs_iunlock(ip, mode);
	return 0;
}

STATIC int
xfs_file_release(
	struct inode	*inode,
	struct file	*filp)
{
	return -xfs_release(XFS_I(inode));
}

STATIC int
xfs_file_readdir(
	struct file	*filp,
	void		*dirent,
	filldir_t	filldir)
{
	struct inode	*inode = filp->f_path.dentry->d_inode;
	xfs_inode_t	*ip = XFS_I(inode);
	int		error;
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

	error = xfs_readdir(ip, dirent, bufsize,
				(xfs_off_t *)&filp->f_pos, filldir);
	if (error)
		return -error;
	return 0;
}

STATIC int
xfs_file_mmap(
	struct file	*filp,
	struct vm_area_struct *vma)
{
	vma->vm_ops = &xfs_file_vm_ops;
	vma->vm_flags |= VM_CAN_NONLINEAR;

	file_accessed(filp);
	return 0;
}

/*
 * mmap()d file has taken write protection fault and is being made
 * writable. We can set the page state up correctly for a writable
 * page, which means we can do correct delalloc accounting (ENOSPC
 * checking!) and unwritten extent mapping.
 */
STATIC int
xfs_vm_page_mkwrite(
	struct vm_area_struct	*vma,
	struct vm_fault		*vmf)
{
	return block_page_mkwrite(vma, vmf, xfs_get_blocks);
}

const struct file_operations xfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= xfs_file_aio_read,
	.aio_write	= xfs_file_aio_write,
	.splice_read	= xfs_file_splice_read,
	.splice_write	= xfs_file_splice_write,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
};

const struct file_operations xfs_dir_file_operations = {
	.open		= xfs_dir_open,
	.read		= generic_read_dir,
	.readdir	= xfs_file_readdir,
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.fsync		= xfs_file_fsync,
};

static const struct vm_operations_struct xfs_file_vm_ops = {
	.fault		= filemap_fault,
	.page_mkwrite	= xfs_vm_page_mkwrite,
};
