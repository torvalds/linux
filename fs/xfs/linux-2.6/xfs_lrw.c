/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_attr.h"
#include "xfs_inode_item.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_iomap.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"

#include <linux/capability.h>
#include <linux/writeback.h>


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

ssize_t			/* bytes read, or (-)  error */
xfs_read(
	xfs_inode_t		*ip,
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned int		segs,
	loff_t			*offset,
	int			ioflags)
{
	struct file		*file = iocb->ki_filp;
	struct inode		*inode = file->f_mapping->host;
	xfs_mount_t		*mp = ip->i_mount;
	size_t			size = 0;
	ssize_t			ret = 0;
	xfs_fsize_t		n;
	unsigned long		seg;


	XFS_STATS_INC(xs_read_calls);

	/* START copy & waste from filemap.c */
	for (seg = 0; seg < segs; seg++) {
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
		if ((*offset & target->bt_smask) ||
		    (size & target->bt_smask)) {
			if (*offset == ip->i_size) {
				return (0);
			}
			return -XFS_ERROR(EINVAL);
		}
	}

	n = XFS_MAXIOFFSET(mp) - *offset;
	if ((n <= 0) || (size == 0))
		return 0;

	if (n < size)
		size = n;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	if (unlikely(ioflags & IO_ISDIRECT))
		mutex_lock(&inode->i_mutex);
	xfs_ilock(ip, XFS_IOLOCK_SHARED);

	if (DM_EVENT_ENABLED(ip, DM_EVENT_READ) && !(ioflags & IO_INVIS)) {
		int dmflags = FILP_DELAY_FLAG(file) | DM_SEM_FLAG_RD(ioflags);
		int iolock = XFS_IOLOCK_SHARED;

		ret = -XFS_SEND_DATA(mp, DM_EVENT_READ, ip, *offset, size,
					dmflags, &iolock);
		if (ret) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			if (unlikely(ioflags & IO_ISDIRECT))
				mutex_unlock(&inode->i_mutex);
			return ret;
		}
	}

	if (unlikely(ioflags & IO_ISDIRECT)) {
		if (inode->i_mapping->nrpages)
			ret = -xfs_flushinval_pages(ip, (*offset & PAGE_CACHE_MASK),
						    -1, FI_REMAPF_LOCKED);
		mutex_unlock(&inode->i_mutex);
		if (ret) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			return ret;
		}
	}

	trace_xfs_file_read(ip, size, *offset, ioflags);

	iocb->ki_pos = *offset;
	ret = generic_file_aio_read(iocb, iovp, segs, *offset);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return ret;
}

ssize_t
xfs_splice_read(
	xfs_inode_t		*ip,
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			count,
	int			flags,
	int			ioflags)
{
	xfs_mount_t		*mp = ip->i_mount;
	ssize_t			ret;

	XFS_STATS_INC(xs_read_calls);
	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_SHARED);

	if (DM_EVENT_ENABLED(ip, DM_EVENT_READ) && !(ioflags & IO_INVIS)) {
		int iolock = XFS_IOLOCK_SHARED;
		int error;

		error = XFS_SEND_DATA(mp, DM_EVENT_READ, ip, *ppos, count,
					FILP_DELAY_FLAG(infilp), &iolock);
		if (error) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			return -error;
		}
	}

	trace_xfs_file_splice_read(ip, count, *ppos, ioflags);

	ret = generic_file_splice_read(infilp, ppos, pipe, count, flags);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return ret;
}

ssize_t
xfs_splice_write(
	xfs_inode_t		*ip,
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			count,
	int			flags,
	int			ioflags)
{
	xfs_mount_t		*mp = ip->i_mount;
	ssize_t			ret;
	struct inode		*inode = outfilp->f_mapping->host;
	xfs_fsize_t		isize, new_size;

	XFS_STATS_INC(xs_write_calls);
	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	if (DM_EVENT_ENABLED(ip, DM_EVENT_WRITE) && !(ioflags & IO_INVIS)) {
		int iolock = XFS_IOLOCK_EXCL;
		int error;

		error = XFS_SEND_DATA(mp, DM_EVENT_WRITE, ip, *ppos, count,
					FILP_DELAY_FLAG(outfilp), &iolock);
		if (error) {
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return -error;
		}
	}

	new_size = *ppos + count;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (new_size > ip->i_size)
		ip->i_new_size = new_size;
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	trace_xfs_file_splice_write(ip, count, *ppos, ioflags);

	ret = generic_file_splice_write(pipe, outfilp, ppos, count, flags);
	if (ret > 0)
		XFS_STATS_ADD(xs_write_bytes, ret);

	isize = i_size_read(inode);
	if (unlikely(ret < 0 && ret != -EFAULT && *ppos > isize))
		*ppos = isize;

	if (*ppos > ip->i_size) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		if (*ppos > ip->i_size)
			ip->i_size = *ppos;
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

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
			  &nimaps, NULL, NULL);
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
				  0, NULL, 0, &imap, &nimaps, NULL, NULL);
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

ssize_t				/* bytes written, or (-) error */
xfs_write(
	struct xfs_inode	*xip,
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned int		nsegs,
	loff_t			*offset,
	int			ioflags)
{
	struct file		*file = iocb->ki_filp;
	struct address_space	*mapping = file->f_mapping;
	struct inode		*inode = mapping->host;
	unsigned long		segs = nsegs;
	xfs_mount_t		*mp;
	ssize_t			ret = 0, error = 0;
	xfs_fsize_t		isize, new_size;
	int			iolock;
	int			eventsent = 0;
	size_t			ocount = 0, count;
	loff_t			pos;
	int			need_i_mutex;

	XFS_STATS_INC(xs_write_calls);

	error = generic_segment_checks(iovp, &segs, &ocount, VERIFY_READ);
	if (error)
		return error;

	count = ocount;
	pos = *offset;

	if (count == 0)
		return 0;

	mp = xip->i_mount;

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

	xfs_ilock(xip, XFS_ILOCK_EXCL|iolock);

start:
	error = -generic_write_checks(file, &pos, &count,
					S_ISBLK(inode->i_mode));
	if (error) {
		xfs_iunlock(xip, XFS_ILOCK_EXCL|iolock);
		goto out_unlock_mutex;
	}

	if ((DM_EVENT_ENABLED(xip, DM_EVENT_WRITE) &&
	    !(ioflags & IO_INVIS) && !eventsent)) {
		int		dmflags = FILP_DELAY_FLAG(file);

		if (need_i_mutex)
			dmflags |= DM_FLAGS_IMUX;

		xfs_iunlock(xip, XFS_ILOCK_EXCL);
		error = XFS_SEND_DATA(xip->i_mount, DM_EVENT_WRITE, xip,
				      pos, count, dmflags, &iolock);
		if (error) {
			goto out_unlock_internal;
		}
		xfs_ilock(xip, XFS_ILOCK_EXCL);
		eventsent = 1;

		/*
		 * The iolock was dropped and reacquired in XFS_SEND_DATA
		 * so we have to recheck the size when appending.
		 * We will only "goto start;" once, since having sent the
		 * event prevents another call to XFS_SEND_DATA, which is
		 * what allows the size to change in the first place.
		 */
		if ((file->f_flags & O_APPEND) && pos != xip->i_size)
			goto start;
	}

	if (ioflags & IO_ISDIRECT) {
		xfs_buftarg_t	*target =
			XFS_IS_REALTIME_INODE(xip) ?
				mp->m_rtdev_targp : mp->m_ddev_targp;

		if ((pos & target->bt_smask) || (count & target->bt_smask)) {
			xfs_iunlock(xip, XFS_ILOCK_EXCL|iolock);
			return XFS_ERROR(-EINVAL);
		}

		if (!need_i_mutex && (mapping->nrpages || pos > xip->i_size)) {
			xfs_iunlock(xip, XFS_ILOCK_EXCL|iolock);
			iolock = XFS_IOLOCK_EXCL;
			need_i_mutex = 1;
			mutex_lock(&inode->i_mutex);
			xfs_ilock(xip, XFS_ILOCK_EXCL|iolock);
			goto start;
		}
	}

	new_size = pos + count;
	if (new_size > xip->i_size)
		xip->i_new_size = new_size;

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

	if (pos > xip->i_size) {
		error = xfs_zero_eof(xip, pos, xip->i_size);
		if (error) {
			xfs_iunlock(xip, XFS_ILOCK_EXCL);
			goto out_unlock_internal;
		}
	}
	xfs_iunlock(xip, XFS_ILOCK_EXCL);

	/*
	 * If we're writing the file then make sure to clear the
	 * setuid and setgid bits if the process is not being run
	 * by root.  This keeps people from modifying setuid and
	 * setgid binaries.
	 */

	if (((xip->i_d.di_mode & S_ISUID) ||
	    ((xip->i_d.di_mode & (S_ISGID | S_IXGRP)) ==
		(S_ISGID | S_IXGRP))) &&
	     !capable(CAP_FSETID)) {
		error = xfs_write_clear_setuid(xip);
		if (likely(!error))
			error = -file_remove_suid(file);
		if (unlikely(error)) {
			goto out_unlock_internal;
		}
	}

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = mapping->backing_dev_info;

	if ((ioflags & IO_ISDIRECT)) {
		if (mapping->nrpages) {
			WARN_ON(need_i_mutex == 0);
			error = xfs_flushinval_pages(xip,
					(pos & PAGE_CACHE_MASK),
					-1, FI_REMAPF_LOCKED);
			if (error)
				goto out_unlock_internal;
		}

		if (need_i_mutex) {
			/* demote the lock now the cached pages are gone */
			xfs_ilock_demote(xip, XFS_IOLOCK_EXCL);
			mutex_unlock(&inode->i_mutex);

			iolock = XFS_IOLOCK_SHARED;
			need_i_mutex = 0;
		}

		trace_xfs_file_direct_write(xip, count, *offset, ioflags);
		ret = generic_file_direct_write(iocb, iovp,
				&segs, pos, offset, count, ocount);

		/*
		 * direct-io write to a hole: fall through to buffered I/O
		 * for completing the rest of the request.
		 */
		if (ret >= 0 && ret != count) {
			XFS_STATS_ADD(xs_write_bytes, ret);

			pos += ret;
			count -= ret;

			ioflags &= ~IO_ISDIRECT;
			xfs_iunlock(xip, iolock);
			goto relock;
		}
	} else {
		int enospc = 0;
		ssize_t ret2 = 0;

write_retry:
		trace_xfs_file_buffered_write(xip, count, *offset, ioflags);
		ret2 = generic_file_buffered_write(iocb, iovp, segs,
				pos, offset, count, ret);
		/*
		 * if we just got an ENOSPC, flush the inode now we
		 * aren't holding any page locks and retry *once*
		 */
		if (ret2 == -ENOSPC && !enospc) {
			error = xfs_flush_pages(xip, 0, -1, 0, FI_NONE);
			if (error)
				goto out_unlock_internal;
			enospc = 1;
			goto write_retry;
		}
		ret = ret2;
	}

	current->backing_dev_info = NULL;

	isize = i_size_read(inode);
	if (unlikely(ret < 0 && ret != -EFAULT && *offset > isize))
		*offset = isize;

	if (*offset > xip->i_size) {
		xfs_ilock(xip, XFS_ILOCK_EXCL);
		if (*offset > xip->i_size)
			xip->i_size = *offset;
		xfs_iunlock(xip, XFS_ILOCK_EXCL);
	}

	if (ret == -ENOSPC &&
	    DM_EVENT_ENABLED(xip, DM_EVENT_NOSPACE) && !(ioflags & IO_INVIS)) {
		xfs_iunlock(xip, iolock);
		if (need_i_mutex)
			mutex_unlock(&inode->i_mutex);
		error = XFS_SEND_NAMESP(xip->i_mount, DM_EVENT_NOSPACE, xip,
				DM_RIGHT_NULL, xip, DM_RIGHT_NULL, NULL, NULL,
				0, 0, 0); /* Delay flag intentionally  unused */
		if (need_i_mutex)
			mutex_lock(&inode->i_mutex);
		xfs_ilock(xip, iolock);
		if (error)
			goto out_unlock_internal;
		goto start;
	}

	error = -ret;
	if (ret <= 0)
		goto out_unlock_internal;

	XFS_STATS_ADD(xs_write_bytes, ret);

	/* Handle various SYNC-type writes */
	if ((file->f_flags & O_DSYNC) || IS_SYNC(inode)) {
		loff_t end = pos + ret - 1;
		int error2;

		xfs_iunlock(xip, iolock);
		if (need_i_mutex)
			mutex_unlock(&inode->i_mutex);

		error2 = filemap_write_and_wait_range(mapping, pos, end);
		if (!error)
			error = error2;
		if (need_i_mutex)
			mutex_lock(&inode->i_mutex);
		xfs_ilock(xip, iolock);

		error2 = xfs_fsync(xip);
		if (!error)
			error = error2;
	}

 out_unlock_internal:
	if (xip->i_new_size) {
		xfs_ilock(xip, XFS_ILOCK_EXCL);
		xip->i_new_size = 0;
		/*
		 * If this was a direct or synchronous I/O that failed (such
		 * as ENOSPC) then part of the I/O may have been written to
		 * disk before the error occured.  In this case the on-disk
		 * file size may have been adjusted beyond the in-memory file
		 * size and now needs to be truncated back.
		 */
		if (xip->i_d.di_size > xip->i_size)
			xip->i_d.di_size = xip->i_size;
		xfs_iunlock(xip, XFS_ILOCK_EXCL);
	}
	xfs_iunlock(xip, iolock);
 out_unlock_mutex:
	if (need_i_mutex)
		mutex_unlock(&inode->i_mutex);
	return -error;
}

/*
 * All xfs metadata buffers except log state machine buffers
 * get this attached as their b_bdstrat callback function.
 * This is so that we can catch a buffer
 * after prematurely unpinning it to forcibly shutdown the filesystem.
 */
int
xfs_bdstrat_cb(struct xfs_buf *bp)
{
	if (XFS_FORCED_SHUTDOWN(bp->b_mount)) {
		trace_xfs_bdstrat_shut(bp, _RET_IP_);
		/*
		 * Metadata write that didn't get logged but
		 * written delayed anyway. These aren't associated
		 * with a transaction, and can be ignored.
		 */
		if (XFS_BUF_IODONE_FUNC(bp) == NULL &&
		    (XFS_BUF_ISREAD(bp)) == 0)
			return (xfs_bioerror_relse(bp));
		else
			return (xfs_bioerror(bp));
	}

	xfs_buf_iorequest(bp);
	return 0;
}

/*
 * Wrapper around bdstrat so that we can stop data from going to disk in case
 * we are shutting down the filesystem.  Typically user data goes thru this
 * path; one of the exceptions is the superblock.
 */
void
xfsbdstrat(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	ASSERT(mp);
	if (!XFS_FORCED_SHUTDOWN(mp)) {
		xfs_buf_iorequest(bp);
		return;
	}

	trace_xfs_bdstrat_shut(bp, _RET_IP_);
	xfs_bioerror_relse(bp);
}

/*
 * If the underlying (data/log/rt) device is readonly, there are some
 * operations that cannot proceed.
 */
int
xfs_dev_is_read_only(
	xfs_mount_t		*mp,
	char			*message)
{
	if (xfs_readonly_buftarg(mp->m_ddev_targp) ||
	    xfs_readonly_buftarg(mp->m_logdev_targp) ||
	    (mp->m_rtdev_targp && xfs_readonly_buftarg(mp->m_rtdev_targp))) {
		cmn_err(CE_NOTE,
			"XFS: %s required on read-only device.", message);
		cmn_err(CE_NOTE,
			"XFS: write access unavailable, cannot proceed.");
		return EROFS;
	}
	return 0;
}
