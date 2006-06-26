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
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_inode_item.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_iomap.h"

#include <linux/capability.h>
#include <linux/writeback.h>


#if defined(XFS_RW_TRACE)
void
xfs_rw_enter_trace(
	int			tag,
	xfs_iocore_t		*io,
	void			*data,
	size_t			segs,
	loff_t			offset,
	int			ioflags)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (ip->i_rwtrace == NULL)
		return;
	ktrace_enter(ip->i_rwtrace,
		(void *)(unsigned long)tag,
		(void *)ip,
		(void *)((unsigned long)((ip->i_d.di_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(ip->i_d.di_size & 0xffffffff)),
		(void *)data,
		(void *)((unsigned long)segs),
		(void *)((unsigned long)((offset >> 32) & 0xffffffff)),
		(void *)((unsigned long)(offset & 0xffffffff)),
		(void *)((unsigned long)ioflags),
		(void *)((unsigned long)((io->io_new_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(io->io_new_size & 0xffffffff)),
		(void *)((unsigned long)current_pid()),
		(void *)NULL,
		(void *)NULL,
		(void *)NULL,
		(void *)NULL);
}

void
xfs_inval_cached_trace(
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	xfs_off_t	len,
	xfs_off_t	first,
	xfs_off_t	last)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (ip->i_rwtrace == NULL)
		return;
	ktrace_enter(ip->i_rwtrace,
		(void *)(__psint_t)XFS_INVAL_CACHED,
		(void *)ip,
		(void *)((unsigned long)((offset >> 32) & 0xffffffff)),
		(void *)((unsigned long)(offset & 0xffffffff)),
		(void *)((unsigned long)((len >> 32) & 0xffffffff)),
		(void *)((unsigned long)(len & 0xffffffff)),
		(void *)((unsigned long)((first >> 32) & 0xffffffff)),
		(void *)((unsigned long)(first & 0xffffffff)),
		(void *)((unsigned long)((last >> 32) & 0xffffffff)),
		(void *)((unsigned long)(last & 0xffffffff)),
		(void *)((unsigned long)current_pid()),
		(void *)NULL,
		(void *)NULL,
		(void *)NULL,
		(void *)NULL,
		(void *)NULL);
}
#endif

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
	struct inode		*ip,	/* inode			*/
	loff_t			pos,	/* offset in file		*/
	size_t			count,	/* size of data to zero		*/
	loff_t			end_size)	/* max file size to set */
{
	unsigned		bytes;
	struct page		*page;
	struct address_space	*mapping;
	char			*kaddr;
	int			status;

	mapping = ip->i_mapping;
	do {
		unsigned long index, offset;

		offset = (pos & (PAGE_CACHE_SIZE -1)); /* Within page */
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = PAGE_CACHE_SIZE - offset;
		if (bytes > count)
			bytes = count;

		status = -ENOMEM;
		page = grab_cache_page(mapping, index);
		if (!page)
			break;

		kaddr = kmap(page);
		status = mapping->a_ops->prepare_write(NULL, page, offset,
							offset + bytes);
		if (status) {
			goto unlock;
		}

		memset((void *) (kaddr + offset), 0, bytes);
		flush_dcache_page(page);
		status = mapping->a_ops->commit_write(NULL, page, offset,
							offset + bytes);
		if (!status) {
			pos += bytes;
			count -= bytes;
			if (pos > i_size_read(ip))
				i_size_write(ip, pos < end_size ? pos : end_size);
		}

unlock:
		kunmap(page);
		unlock_page(page);
		page_cache_release(page);
		if (status)
			break;
	} while (count);

	return (-status);
}

ssize_t			/* bytes read, or (-)  error */
xfs_read(
	bhv_desc_t		*bdp,
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned int		segs,
	loff_t			*offset,
	int			ioflags,
	cred_t			*credp)
{
	struct file		*file = iocb->ki_filp;
	struct inode		*inode = file->f_mapping->host;
	size_t			size = 0;
	ssize_t			ret;
	xfs_fsize_t		n;
	xfs_inode_t		*ip;
	xfs_mount_t		*mp;
	bhv_vnode_t		*vp;
	unsigned long		seg;

	ip = XFS_BHVTOI(bdp);
	vp = BHV_TO_VNODE(bdp);
	mp = ip->i_mount;

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
			(ip->i_d.di_flags & XFS_DIFLAG_REALTIME) ?
				mp->m_rtdev_targp : mp->m_ddev_targp;
		if ((*offset & target->bt_smask) ||
		    (size & target->bt_smask)) {
			if (*offset == ip->i_d.di_size) {
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

	if (DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_EVENT_READ) &&
	    !(ioflags & IO_INVIS)) {
		bhv_vrwlock_t locktype = VRWLOCK_READ;
		int dmflags = FILP_DELAY_FLAG(file) | DM_SEM_FLAG_RD(ioflags);

		ret = -XFS_SEND_DATA(mp, DM_EVENT_READ,
					BHV_TO_VNODE(bdp), *offset, size,
					dmflags, &locktype);
		if (ret) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			goto unlock_mutex;
		}
	}

	if (unlikely((ioflags & IO_ISDIRECT) && VN_CACHED(vp)))
		bhv_vop_flushinval_pages(vp, ctooff(offtoct(*offset)),
						-1, FI_REMAPF_LOCKED);

	xfs_rw_enter_trace(XFS_READ_ENTER, &ip->i_iocore,
				(void *)iovp, segs, *offset, ioflags);
	ret = __generic_file_aio_read(iocb, iovp, segs, offset);
	if (ret == -EIOCBQUEUED && !(ioflags & IO_ISAIO))
		ret = wait_on_sync_kiocb(iocb);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);

unlock_mutex:
	if (unlikely(ioflags & IO_ISDIRECT))
		mutex_unlock(&inode->i_mutex);
	return ret;
}

ssize_t
xfs_sendfile(
	bhv_desc_t		*bdp,
	struct file		*filp,
	loff_t			*offset,
	int			ioflags,
	size_t			count,
	read_actor_t		actor,
	void			*target,
	cred_t			*credp)
{
	xfs_inode_t		*ip = XFS_BHVTOI(bdp);
	xfs_mount_t		*mp = ip->i_mount;
	ssize_t			ret;

	XFS_STATS_INC(xs_read_calls);
	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_SHARED);

	if (DM_EVENT_ENABLED(BHV_TO_VNODE(bdp)->v_vfsp, ip, DM_EVENT_READ) &&
	    (!(ioflags & IO_INVIS))) {
		bhv_vrwlock_t locktype = VRWLOCK_READ;
		int error;

		error = XFS_SEND_DATA(mp, DM_EVENT_READ, BHV_TO_VNODE(bdp),
				      *offset, count,
				      FILP_DELAY_FLAG(filp), &locktype);
		if (error) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			return -error;
		}
	}
	xfs_rw_enter_trace(XFS_SENDFILE_ENTER, &ip->i_iocore,
		   (void *)(unsigned long)target, count, *offset, ioflags);
	ret = generic_file_sendfile(filp, offset, count, actor, target);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return ret;
}

ssize_t
xfs_splice_read(
	bhv_desc_t		*bdp,
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			count,
	int			flags,
	int			ioflags,
	cred_t			*credp)
{
	xfs_inode_t		*ip = XFS_BHVTOI(bdp);
	xfs_mount_t		*mp = ip->i_mount;
	ssize_t			ret;

	XFS_STATS_INC(xs_read_calls);
	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_SHARED);

	if (DM_EVENT_ENABLED(BHV_TO_VNODE(bdp)->v_vfsp, ip, DM_EVENT_READ) &&
	    (!(ioflags & IO_INVIS))) {
		bhv_vrwlock_t locktype = VRWLOCK_READ;
		int error;

		error = XFS_SEND_DATA(mp, DM_EVENT_READ, BHV_TO_VNODE(bdp),
					*ppos, count,
					FILP_DELAY_FLAG(infilp), &locktype);
		if (error) {
			xfs_iunlock(ip, XFS_IOLOCK_SHARED);
			return -error;
		}
	}
	xfs_rw_enter_trace(XFS_SPLICE_READ_ENTER, &ip->i_iocore,
			   pipe, count, *ppos, ioflags);
	ret = generic_file_splice_read(infilp, ppos, pipe, count, flags);
	if (ret > 0)
		XFS_STATS_ADD(xs_read_bytes, ret);

	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return ret;
}

ssize_t
xfs_splice_write(
	bhv_desc_t		*bdp,
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			count,
	int			flags,
	int			ioflags,
	cred_t			*credp)
{
	xfs_inode_t		*ip = XFS_BHVTOI(bdp);
	xfs_mount_t		*mp = ip->i_mount;
	ssize_t			ret;

	XFS_STATS_INC(xs_write_calls);
	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return -EIO;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	if (DM_EVENT_ENABLED(BHV_TO_VNODE(bdp)->v_vfsp, ip, DM_EVENT_WRITE) &&
	    (!(ioflags & IO_INVIS))) {
		bhv_vrwlock_t locktype = VRWLOCK_WRITE;
		int error;

		error = XFS_SEND_DATA(mp, DM_EVENT_WRITE, BHV_TO_VNODE(bdp),
					*ppos, count,
					FILP_DELAY_FLAG(outfilp), &locktype);
		if (error) {
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return -error;
		}
	}
	xfs_rw_enter_trace(XFS_SPLICE_WRITE_ENTER, &ip->i_iocore,
			   pipe, count, *ppos, ioflags);
	ret = generic_file_splice_write(pipe, outfilp, ppos, count, flags);
	if (ret > 0)
		XFS_STATS_ADD(xs_write_bytes, ret);

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
	struct inode	*ip,
	xfs_iocore_t	*io,
	xfs_fsize_t	isize,
	xfs_fsize_t	end_size)
{
	xfs_fileoff_t	last_fsb;
	xfs_mount_t	*mp = io->io_mount;
	int		nimaps;
	int		zero_offset;
	int		zero_len;
	int		error = 0;
	xfs_bmbt_irec_t	imap;
	loff_t		loff;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);

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
	error = XFS_BMAPI(mp, NULL, io, last_fsb, 1, 0, NULL, 0, &imap,
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
	XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL| XFS_EXTSIZE_RD);

	loff = XFS_FSB_TO_B(mp, last_fsb);
	zero_len = mp->m_sb.sb_blocksize - zero_offset;
	error = xfs_iozero(ip, loff + zero_offset, zero_len, end_size);

	XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
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
	bhv_vnode_t	*vp,
	xfs_iocore_t	*io,
	xfs_off_t	offset,		/* starting I/O offset */
	xfs_fsize_t	isize,		/* current inode size */
	xfs_fsize_t	end_size)	/* terminal inode size */
{
	struct inode	*ip = vn_to_inode(vp);
	xfs_fileoff_t	start_zero_fsb;
	xfs_fileoff_t	end_zero_fsb;
	xfs_fileoff_t	zero_count_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_mount_t	*mp = io->io_mount;
	int		nimaps;
	int		error = 0;
	xfs_bmbt_irec_t	imap;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE));
	ASSERT(ismrlocked(io->io_iolock, MR_UPDATE));
	ASSERT(offset > isize);

	/*
	 * First handle zeroing the block on which isize resides.
	 * We only zero a part of that block so it is handled specially.
	 */
	error = xfs_zero_last_block(ip, io, isize, end_size);
	if (error) {
		ASSERT(ismrlocked(io->io_lock, MR_UPDATE));
		ASSERT(ismrlocked(io->io_iolock, MR_UPDATE));
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
		error = XFS_BMAPI(mp, NULL, io, start_zero_fsb, zero_count_fsb,
				  0, NULL, 0, &imap, &nimaps, NULL, NULL);
		if (error) {
			ASSERT(ismrlocked(io->io_lock, MR_UPDATE));
			ASSERT(ismrlocked(io->io_iolock, MR_UPDATE));
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
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);

		error = xfs_iozero(ip,
				   XFS_FSB_TO_B(mp, start_zero_fsb),
				   XFS_FSB_TO_B(mp, imap.br_blockcount),
				   end_size);
		if (error) {
			goto out_lock;
		}

		start_zero_fsb = imap.br_startoff + imap.br_blockcount;
		ASSERT(start_zero_fsb <= (end_zero_fsb + 1));

		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	}

	return 0;

out_lock:

	XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	ASSERT(error >= 0);
	return error;
}

ssize_t				/* bytes written, or (-) error */
xfs_write(
	bhv_desc_t		*bdp,
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned int		nsegs,
	loff_t			*offset,
	int			ioflags,
	cred_t			*credp)
{
	struct file		*file = iocb->ki_filp;
	struct address_space	*mapping = file->f_mapping;
	struct inode		*inode = mapping->host;
	unsigned long		segs = nsegs;
	xfs_inode_t		*xip;
	xfs_mount_t		*mp;
	ssize_t			ret = 0, error = 0;
	xfs_fsize_t		isize, new_size;
	xfs_iocore_t		*io;
	bhv_vnode_t		*vp;
	unsigned long		seg;
	int			iolock;
	int			eventsent = 0;
	bhv_vrwlock_t		locktype;
	size_t			ocount = 0, count;
	loff_t			pos;
	int			need_i_mutex = 1, need_flush = 0;

	XFS_STATS_INC(xs_write_calls);

	vp = BHV_TO_VNODE(bdp);
	xip = XFS_BHVTOI(bdp);

	for (seg = 0; seg < segs; seg++) {
		const struct iovec *iv = &iovp[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		ocount += iv->iov_len;
		if (unlikely((ssize_t)(ocount|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_READ, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		segs = seg;
		ocount -= iv->iov_len;  /* This segment is no good */
		break;
	}

	count = ocount;
	pos = *offset;

	if (count == 0)
		return 0;

	io = &xip->i_iocore;
	mp = io->io_mount;

	vfs_wait_for_freeze(vp->v_vfsp, SB_FREEZE_WRITE);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	if (ioflags & IO_ISDIRECT) {
		xfs_buftarg_t	*target =
			(xip->i_d.di_flags & XFS_DIFLAG_REALTIME) ?
				mp->m_rtdev_targp : mp->m_ddev_targp;

		if ((pos & target->bt_smask) || (count & target->bt_smask))
			return XFS_ERROR(-EINVAL);

		if (!VN_CACHED(vp) && pos < i_size_read(inode))
			need_i_mutex = 0;

		if (VN_CACHED(vp))
			need_flush = 1;
	}

relock:
	if (need_i_mutex) {
		iolock = XFS_IOLOCK_EXCL;
		locktype = VRWLOCK_WRITE;

		mutex_lock(&inode->i_mutex);
	} else {
		iolock = XFS_IOLOCK_SHARED;
		locktype = VRWLOCK_WRITE_DIRECT;
	}

	xfs_ilock(xip, XFS_ILOCK_EXCL|iolock);

	isize = i_size_read(inode);

	if (file->f_flags & O_APPEND)
		*offset = isize;

start:
	error = -generic_write_checks(file, &pos, &count,
					S_ISBLK(inode->i_mode));
	if (error) {
		xfs_iunlock(xip, XFS_ILOCK_EXCL|iolock);
		goto out_unlock_mutex;
	}

	new_size = pos + count;
	if (new_size > isize)
		io->io_new_size = new_size;

	if ((DM_EVENT_ENABLED(vp->v_vfsp, xip, DM_EVENT_WRITE) &&
	    !(ioflags & IO_INVIS) && !eventsent)) {
		loff_t		savedsize = pos;
		int		dmflags = FILP_DELAY_FLAG(file);

		if (need_i_mutex)
			dmflags |= DM_FLAGS_IMUX;

		xfs_iunlock(xip, XFS_ILOCK_EXCL);
		error = XFS_SEND_DATA(xip->i_mount, DM_EVENT_WRITE, vp,
				      pos, count,
				      dmflags, &locktype);
		if (error) {
			xfs_iunlock(xip, iolock);
			goto out_unlock_mutex;
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
		if ((file->f_flags & O_APPEND) && savedsize != isize) {
			pos = isize = xip->i_d.di_size;
			goto start;
		}
	}

	if (likely(!(ioflags & IO_INVIS))) {
		file_update_time(file);
		xfs_ichgtime_fast(xip, inode,
				  XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	}

	/*
	 * If the offset is beyond the size of the file, we have a couple
	 * of things to do. First, if there is already space allocated
	 * we need to either create holes or zero the disk or ...
	 *
	 * If there is a page where the previous size lands, we need
	 * to zero it out up to the new size.
	 */

	if (pos > isize) {
		error = xfs_zero_eof(BHV_TO_VNODE(bdp), io, pos,
					isize, pos + count);
		if (error) {
			xfs_iunlock(xip, XFS_ILOCK_EXCL|iolock);
			goto out_unlock_mutex;
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
			error = -remove_suid(file->f_dentry);
		if (unlikely(error)) {
			xfs_iunlock(xip, iolock);
			goto out_unlock_mutex;
		}
	}

retry:
	/* We can write back this queue in page reclaim */
	current->backing_dev_info = mapping->backing_dev_info;

	if ((ioflags & IO_ISDIRECT)) {
		if (need_flush) {
			xfs_inval_cached_trace(io, pos, -1,
					ctooff(offtoct(pos)), -1);
			bhv_vop_flushinval_pages(vp, ctooff(offtoct(pos)),
					-1, FI_REMAPF_LOCKED);
		}

		if (need_i_mutex) {
			/* demote the lock now the cached pages are gone */
			XFS_ILOCK_DEMOTE(mp, io, XFS_IOLOCK_EXCL);
			mutex_unlock(&inode->i_mutex);

			iolock = XFS_IOLOCK_SHARED;
			locktype = VRWLOCK_WRITE_DIRECT;
			need_i_mutex = 0;
		}

 		xfs_rw_enter_trace(XFS_DIOWR_ENTER, io, (void *)iovp, segs,
				*offset, ioflags);
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

			need_i_mutex = 1;
			ioflags &= ~IO_ISDIRECT;
			xfs_iunlock(xip, iolock);
			goto relock;
		}
	} else {
		xfs_rw_enter_trace(XFS_WRITE_ENTER, io, (void *)iovp, segs,
				*offset, ioflags);
		ret = generic_file_buffered_write(iocb, iovp, segs,
				pos, offset, count, ret);
	}

	current->backing_dev_info = NULL;

	if (ret == -EIOCBQUEUED && !(ioflags & IO_ISAIO))
		ret = wait_on_sync_kiocb(iocb);

	if ((ret == -ENOSPC) &&
	    DM_EVENT_ENABLED(vp->v_vfsp, xip, DM_EVENT_NOSPACE) &&
	    !(ioflags & IO_INVIS)) {

		xfs_rwunlock(bdp, locktype);
		if (need_i_mutex)
			mutex_unlock(&inode->i_mutex);
		error = XFS_SEND_NAMESP(xip->i_mount, DM_EVENT_NOSPACE, vp,
				DM_RIGHT_NULL, vp, DM_RIGHT_NULL, NULL, NULL,
				0, 0, 0); /* Delay flag intentionally  unused */
		if (error)
			goto out_nounlocks;
		if (need_i_mutex)
			mutex_lock(&inode->i_mutex);
		xfs_rwlock(bdp, locktype);
		pos = xip->i_d.di_size;
		ret = 0;
		goto retry;
	}

	isize = i_size_read(inode);
	if (unlikely(ret < 0 && ret != -EFAULT && *offset > isize))
		*offset = isize;

	if (*offset > xip->i_d.di_size) {
		xfs_ilock(xip, XFS_ILOCK_EXCL);
		if (*offset > xip->i_d.di_size) {
			xip->i_d.di_size = *offset;
			i_size_write(inode, *offset);
			xip->i_update_core = 1;
			xip->i_update_size = 1;
		}
		xfs_iunlock(xip, XFS_ILOCK_EXCL);
	}

	error = -ret;
	if (ret <= 0)
		goto out_unlock_internal;

	XFS_STATS_ADD(xs_write_bytes, ret);

	/* Handle various SYNC-type writes */
	if ((file->f_flags & O_SYNC) || IS_SYNC(inode)) {
		error = xfs_write_sync_logforce(mp, xip);
		if (error)
			goto out_unlock_internal;

		xfs_rwunlock(bdp, locktype);
		if (need_i_mutex)
			mutex_unlock(&inode->i_mutex);

		error = sync_page_range(inode, mapping, pos, ret);
		if (!error)
			error = ret;
		return error;
	}

 out_unlock_internal:
	xfs_rwunlock(bdp, locktype);
 out_unlock_mutex:
	if (need_i_mutex)
		mutex_unlock(&inode->i_mutex);
 out_nounlocks:
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
	xfs_mount_t	*mp;

	mp = XFS_BUF_FSPRIVATE3(bp, xfs_mount_t *);
	if (!XFS_FORCED_SHUTDOWN(mp)) {
		xfs_buf_iorequest(bp);
		return 0;
	} else {
		xfs_buftrace("XFS__BDSTRAT IOERROR", bp);
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
}


int
xfs_bmap(bhv_desc_t	*bdp,
	xfs_off_t	offset,
	ssize_t		count,
	int		flags,
	xfs_iomap_t	*iomapp,
	int		*niomaps)
{
	xfs_inode_t	*ip = XFS_BHVTOI(bdp);
	xfs_iocore_t	*io = &ip->i_iocore;

	ASSERT((ip->i_d.di_mode & S_IFMT) == S_IFREG);
	ASSERT(((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) ==
	       ((ip->i_iocore.io_flags & XFS_IOCORE_RT) != 0));

	return xfs_iomap(io, offset, count, flags, iomapp, niomaps);
}

/*
 * Wrapper around bdstrat so that we can stop data
 * from going to disk in case we are shutting down the filesystem.
 * Typically user data goes thru this path; one of the exceptions
 * is the superblock.
 */
int
xfsbdstrat(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	ASSERT(mp);
	if (!XFS_FORCED_SHUTDOWN(mp)) {
		/* Grio redirection would go here
		 * if (XFS_BUF_IS_GRIO(bp)) {
		 */

		xfs_buf_iorequest(bp);
		return 0;
	}

	xfs_buftrace("XFSBDSTRAT IOERROR", bp);
	return (xfs_bioerror_relse(bp));
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
