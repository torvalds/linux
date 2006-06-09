/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_space.h"
#include "xfs_utils.h"
#include "xfs_iomap.h"

#if defined(XFS_RW_TRACE)
void
xfs_iomap_enter_trace(
	int		tag,
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	ssize_t		count)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (!ip->i_rwtrace)
		return;

	ktrace_enter(ip->i_rwtrace,
		(void *)((unsigned long)tag),
		(void *)ip,
		(void *)((unsigned long)((ip->i_d.di_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(ip->i_d.di_size & 0xffffffff)),
		(void *)((unsigned long)((offset >> 32) & 0xffffffff)),
		(void *)((unsigned long)(offset & 0xffffffff)),
		(void *)((unsigned long)count),
		(void *)((unsigned long)((io->io_new_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(io->io_new_size & 0xffffffff)),
		(void *)((unsigned long)current_pid()),
		(void *)NULL,
		(void *)NULL,
		(void *)NULL,
		(void *)NULL,
		(void *)NULL,
		(void *)NULL);
}

void
xfs_iomap_map_trace(
	int		tag,
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	ssize_t		count,
	xfs_iomap_t	*iomapp,
	xfs_bmbt_irec_t	*imapp,
	int		flags)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);

	if (!ip->i_rwtrace)
		return;

	ktrace_enter(ip->i_rwtrace,
		(void *)((unsigned long)tag),
		(void *)ip,
		(void *)((unsigned long)((ip->i_d.di_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(ip->i_d.di_size & 0xffffffff)),
		(void *)((unsigned long)((offset >> 32) & 0xffffffff)),
		(void *)((unsigned long)(offset & 0xffffffff)),
		(void *)((unsigned long)count),
		(void *)((unsigned long)flags),
		(void *)((unsigned long)((iomapp->iomap_offset >> 32) & 0xffffffff)),
		(void *)((unsigned long)(iomapp->iomap_offset & 0xffffffff)),
		(void *)((unsigned long)(iomapp->iomap_delta)),
		(void *)((unsigned long)(iomapp->iomap_bsize)),
		(void *)((unsigned long)(iomapp->iomap_bn)),
		(void *)(__psint_t)(imapp->br_startoff),
		(void *)((unsigned long)(imapp->br_blockcount)),
		(void *)(__psint_t)(imapp->br_startblock));
}
#else
#define xfs_iomap_enter_trace(tag, io, offset, count)
#define xfs_iomap_map_trace(tag, io, offset, count, iomapp, imapp, flags)
#endif

#define XFS_WRITEIO_ALIGN(mp,off)	(((off) >> mp->m_writeio_log) \
						<< mp->m_writeio_log)
#define XFS_STRAT_WRITE_IMAPS	2
#define XFS_WRITE_IMAPS		XFS_BMAP_MAX_NMAP

STATIC int
xfs_imap_to_bmap(
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	xfs_bmbt_irec_t *imap,
	xfs_iomap_t	*iomapp,
	int		imaps,			/* Number of imap entries */
	int		iomaps,			/* Number of iomap entries */
	int		flags)
{
	xfs_mount_t	*mp;
	xfs_fsize_t	nisize;
	int		pbm;
	xfs_fsblock_t	start_block;

	mp = io->io_mount;
	nisize = XFS_SIZE(mp, io);
	if (io->io_new_size > nisize)
		nisize = io->io_new_size;

	for (pbm = 0; imaps && pbm < iomaps; imaps--, iomapp++, imap++, pbm++) {
		iomapp->iomap_offset = XFS_FSB_TO_B(mp, imap->br_startoff);
		iomapp->iomap_delta = offset - iomapp->iomap_offset;
		iomapp->iomap_bsize = XFS_FSB_TO_B(mp, imap->br_blockcount);
		iomapp->iomap_flags = flags;

		if (io->io_flags & XFS_IOCORE_RT) {
			iomapp->iomap_flags |= IOMAP_REALTIME;
			iomapp->iomap_target = mp->m_rtdev_targp;
		} else {
			iomapp->iomap_target = mp->m_ddev_targp;
		}
		start_block = imap->br_startblock;
		if (start_block == HOLESTARTBLOCK) {
			iomapp->iomap_bn = IOMAP_DADDR_NULL;
			iomapp->iomap_flags |= IOMAP_HOLE;
		} else if (start_block == DELAYSTARTBLOCK) {
			iomapp->iomap_bn = IOMAP_DADDR_NULL;
			iomapp->iomap_flags |= IOMAP_DELAY;
		} else {
			iomapp->iomap_bn = XFS_FSB_TO_DB_IO(io, start_block);
			if (ISUNWRITTEN(imap))
				iomapp->iomap_flags |= IOMAP_UNWRITTEN;
		}

		if ((iomapp->iomap_offset + iomapp->iomap_bsize) >= nisize) {
			iomapp->iomap_flags |= IOMAP_EOF;
		}

		offset += iomapp->iomap_bsize - iomapp->iomap_delta;
	}
	return pbm;	/* Return the number filled */
}

int
xfs_iomap(
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	ssize_t		count,
	int		flags,
	xfs_iomap_t	*iomapp,
	int		*niomaps)
{
	xfs_mount_t	*mp = io->io_mount;
	xfs_fileoff_t	offset_fsb, end_fsb;
	int		error = 0;
	int		lockmode = 0;
	xfs_bmbt_irec_t	imap;
	int		nimaps = 1;
	int		bmapi_flags = 0;
	int		iomap_flags = 0;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	switch (flags &
		(BMAPI_READ | BMAPI_WRITE | BMAPI_ALLOCATE |
		 BMAPI_UNWRITTEN | BMAPI_DEVICE)) {
	case BMAPI_READ:
		xfs_iomap_enter_trace(XFS_IOMAP_READ_ENTER, io, offset, count);
		lockmode = XFS_LCK_MAP_SHARED(mp, io);
		bmapi_flags = XFS_BMAPI_ENTIRE;
		break;
	case BMAPI_WRITE:
		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_ENTER, io, offset, count);
		lockmode = XFS_ILOCK_EXCL|XFS_EXTSIZE_WR;
		if (flags & BMAPI_IGNSTATE)
			bmapi_flags |= XFS_BMAPI_IGSTATE|XFS_BMAPI_ENTIRE;
		XFS_ILOCK(mp, io, lockmode);
		break;
	case BMAPI_ALLOCATE:
		xfs_iomap_enter_trace(XFS_IOMAP_ALLOC_ENTER, io, offset, count);
		lockmode = XFS_ILOCK_SHARED|XFS_EXTSIZE_RD;
		bmapi_flags = XFS_BMAPI_ENTIRE;
		/* Attempt non-blocking lock */
		if (flags & BMAPI_TRYLOCK) {
			if (!XFS_ILOCK_NOWAIT(mp, io, lockmode))
				return XFS_ERROR(EAGAIN);
		} else {
			XFS_ILOCK(mp, io, lockmode);
		}
		break;
	case BMAPI_UNWRITTEN:
		goto phase2;
	case BMAPI_DEVICE:
		lockmode = XFS_LCK_MAP_SHARED(mp, io);
		iomapp->iomap_target = io->io_flags & XFS_IOCORE_RT ?
			mp->m_rtdev_targp : mp->m_ddev_targp;
		error = 0;
		*niomaps = 1;
		goto out;
	default:
		BUG();
	}

	ASSERT(offset <= mp->m_maxioffset);
	if ((xfs_fsize_t)offset + count > mp->m_maxioffset)
		count = mp->m_maxioffset - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
			(xfs_filblks_t)(end_fsb - offset_fsb),
			bmapi_flags,  NULL, 0, &imap,
			&nimaps, NULL, NULL);

	if (error)
		goto out;

phase2:
	switch (flags & (BMAPI_WRITE|BMAPI_ALLOCATE|BMAPI_UNWRITTEN)) {
	case BMAPI_WRITE:
		/* If we found an extent, return it */
		if (nimaps &&
		    (imap.br_startblock != HOLESTARTBLOCK) &&
		    (imap.br_startblock != DELAYSTARTBLOCK)) {
			xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP, io,
					offset, count, iomapp, &imap, flags);
			break;
		}

		if (flags & (BMAPI_DIRECT|BMAPI_MMAP)) {
			error = XFS_IOMAP_WRITE_DIRECT(mp, io, offset,
					count, flags, &imap, &nimaps, nimaps);
		} else {
			error = XFS_IOMAP_WRITE_DELAY(mp, io, offset, count,
					flags, &imap, &nimaps);
		}
		if (!error) {
			xfs_iomap_map_trace(XFS_IOMAP_ALLOC_MAP, io,
					offset, count, iomapp, &imap, flags);
		}
		iomap_flags = IOMAP_NEW;
		break;
	case BMAPI_ALLOCATE:
		/* If we found an extent, return it */
		XFS_IUNLOCK(mp, io, lockmode);
		lockmode = 0;

		if (nimaps && !ISNULLSTARTBLOCK(imap.br_startblock)) {
			xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP, io,
					offset, count, iomapp, &imap, flags);
			break;
		}

		error = XFS_IOMAP_WRITE_ALLOCATE(mp, io, offset, count,
						 &imap, &nimaps);
		break;
	case BMAPI_UNWRITTEN:
		lockmode = 0;
		error = XFS_IOMAP_WRITE_UNWRITTEN(mp, io, offset, count);
		nimaps = 0;
		break;
	}

	if (nimaps) {
		*niomaps = xfs_imap_to_bmap(io, offset, &imap,
					    iomapp, nimaps, *niomaps, iomap_flags);
	} else if (niomaps) {
		*niomaps = 0;
	}

out:
	if (lockmode)
		XFS_IUNLOCK(mp, io, lockmode);
	return XFS_ERROR(error);
}

STATIC int
xfs_iomap_eof_align_last_fsb(
	xfs_mount_t	*mp,
	xfs_iocore_t	*io,
	xfs_fsize_t	isize,
	xfs_extlen_t	extsize,
	xfs_fileoff_t	*last_fsb)
{
	xfs_fileoff_t	new_last_fsb = 0;
	xfs_extlen_t	align;
	int		eof, error;

	if (io->io_flags & XFS_IOCORE_RT)
		;
	/*
	 * If mounted with the "-o swalloc" option, roundup the allocation
	 * request to a stripe width boundary if the file size is >=
	 * stripe width and we are allocating past the allocation eof.
	 */
	else if (mp->m_swidth && (mp->m_flags & XFS_MOUNT_SWALLOC) &&
	        (isize >= XFS_FSB_TO_B(mp, mp->m_swidth)))
		new_last_fsb = roundup_64(*last_fsb, mp->m_swidth);
	/*
	 * Roundup the allocation request to a stripe unit (m_dalign) boundary
	 * if the file size is >= stripe unit size, and we are allocating past
	 * the allocation eof.
	 */
	else if (mp->m_dalign && (isize >= XFS_FSB_TO_B(mp, mp->m_dalign)))
		new_last_fsb = roundup_64(*last_fsb, mp->m_dalign);

	/*
	 * Always round up the allocation request to an extent boundary
	 * (when file on a real-time subvolume or has di_extsize hint).
	 */
	if (extsize) {
		if (new_last_fsb)
			align = roundup_64(new_last_fsb, extsize);
		else
			align = extsize;
		new_last_fsb = roundup_64(*last_fsb, align);
	}

	if (new_last_fsb) {
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error)
			return error;
		if (eof)
			*last_fsb = new_last_fsb;
	}
	return 0;
}

STATIC int
xfs_flush_space(
	xfs_inode_t	*ip,
	int		*fsynced,
	int		*ioflags)
{
	switch (*fsynced) {
	case 0:
		if (ip->i_delayed_blks) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			xfs_flush_inode(ip);
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			*fsynced = 1;
		} else {
			*ioflags |= BMAPI_SYNC;
			*fsynced = 2;
		}
		return 0;
	case 1:
		*fsynced = 2;
		*ioflags |= BMAPI_SYNC;
		return 0;
	case 2:
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_flush_device(ip);
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		*fsynced = 3;
		return 0;
	}
	return 1;
}

int
xfs_iomap_write_direct(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		count,
	int		flags,
	xfs_bmbt_irec_t *ret_imap,
	int		*nmaps,
	int		found)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_iocore_t	*io = &ip->i_iocore;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb, resaligned;
	xfs_fsblock_t	firstfsb;
	xfs_extlen_t	extsz, temp;
	xfs_fsize_t	isize;
	int		nimaps;
	int		bmapi_flag;
	int		quota_flag;
	int		rt;
	xfs_trans_t	*tp;
	xfs_bmbt_irec_t imap;
	xfs_bmap_free_t free_list;
	uint		qblocks, resblks, resrtextents;
	int		committed;
	int		error;

	/*
	 * Make sure that the dquots are there. This doesn't hold
	 * the ilock across a disk read.
	 */
	error = XFS_QM_DQATTACH(ip->i_mount, ip, XFS_QMOPT_ILOCKED);
	if (error)
		return XFS_ERROR(error);

	rt = XFS_IS_REALTIME_INODE(ip);
	if (unlikely(rt)) {
		if (!(extsz = ip->i_d.di_extsize))
			extsz = mp->m_sb.sb_rextsize;
	} else {
		extsz = ip->i_d.di_extsize;
	}

	isize = ip->i_d.di_size;
	if (io->io_new_size > isize)
		isize = io->io_new_size;

  	offset_fsb = XFS_B_TO_FSBT(mp, offset);
  	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	if ((offset + count) > isize) {
		error = xfs_iomap_eof_align_last_fsb(mp, io, isize, extsz,
							&last_fsb);
		if (error)
			goto error_out;
	} else {
		if (found && (ret_imap->br_startblock == HOLESTARTBLOCK))
			last_fsb = MIN(last_fsb, (xfs_fileoff_t)
					ret_imap->br_blockcount +
					ret_imap->br_startoff);
	}
	count_fsb = last_fsb - offset_fsb;
	ASSERT(count_fsb > 0);

	resaligned = count_fsb;
	if (unlikely(extsz)) {
		if ((temp = do_mod(offset_fsb, extsz)))
			resaligned += temp;
		if ((temp = do_mod(resaligned, extsz)))
			resaligned += extsz - temp;
	}

	if (unlikely(rt)) {
		resrtextents = qblocks = resaligned;
		resrtextents /= mp->m_sb.sb_rextsize;
  		resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
  		quota_flag = XFS_QMOPT_RES_RTBLKS;
  	} else {
  		resrtextents = 0;
		resblks = qblocks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);
  		quota_flag = XFS_QMOPT_RES_REGBLKS;
  	}

	/*
	 * Allocate and setup the transaction
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
	error = xfs_trans_reserve(tp, resblks,
			XFS_WRITE_LOG_RES(mp), resrtextents,
			XFS_TRANS_PERM_LOG_RES,
			XFS_WRITE_LOG_COUNT);
	/*
	 * Check for running out of space, note: need lock to return
	 */
	if (error)
		xfs_trans_cancel(tp, 0);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (error)
		goto error_out;

	error = XFS_TRANS_RESERVE_QUOTA_NBLKS(mp, tp, ip,
					      qblocks, 0, quota_flag);
	if (error)
		goto error1;

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);

	bmapi_flag = XFS_BMAPI_WRITE;
	if ((flags & BMAPI_DIRECT) && (offset < ip->i_d.di_size || extsz))
		bmapi_flag |= XFS_BMAPI_PREALLOC;

	/*
	 * Issue the xfs_bmapi() call to allocate the blocks
	 */
	XFS_BMAP_INIT(&free_list, &firstfsb);
	nimaps = 1;
	error = XFS_BMAPI(mp, tp, io, offset_fsb, count_fsb, bmapi_flag,
		&firstfsb, 0, &imap, &nimaps, &free_list, NULL);
	if (error)
		goto error0;

	/*
	 * Complete the transaction
	 */
	error = xfs_bmap_finish(&tp, &free_list, firstfsb, &committed);
	if (error)
		goto error0;
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error)
		goto error_out;

	/*
	 * Copy any maps to caller's array and return any error.
	 */
	if (nimaps == 0) {
		error = (ENOSPC);
		goto error_out;
	}

	*ret_imap = imap;
	*nmaps = 1;
	if ( !(io->io_flags & XFS_IOCORE_RT)  && !ret_imap->br_startblock) {
                cmn_err(CE_PANIC,"Access to block zero:  fs <%s> inode: %lld "
                        "start_block : %llx start_off : %llx blkcnt : %llx "
                        "extent-state : %x \n",
                        (ip->i_mount)->m_fsname,
                        (long long)ip->i_ino,
                        (unsigned long long)ret_imap->br_startblock,
			(unsigned long long)ret_imap->br_startoff,
                        (unsigned long long)ret_imap->br_blockcount,
			ret_imap->br_state);
        }
	return 0;

error0:	/* Cancel bmap, unlock inode, unreserve quota blocks, cancel trans */
	xfs_bmap_cancel(&free_list);
	XFS_TRANS_UNRESERVE_QUOTA_NBLKS(mp, tp, ip, qblocks, 0, quota_flag);

error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	*nmaps = 0;	/* nothing set-up here */

error_out:
	return XFS_ERROR(error);
}

/*
 * If the caller is doing a write at the end of the file,
 * then extend the allocation out to the file system's write
 * iosize.  We clean up any extra space left over when the
 * file is closed in xfs_inactive().
 *
 * For sync writes, we are flushing delayed allocate space to
 * try to make additional space available for allocation near
 * the filesystem full boundary - preallocation hurts in that
 * situation, of course.
 */
STATIC int
xfs_iomap_eof_want_preallocate(
	xfs_mount_t	*mp,
	xfs_iocore_t	*io,
	xfs_fsize_t	isize,
	xfs_off_t	offset,
	size_t		count,
	int		ioflag,
	xfs_bmbt_irec_t *imap,
	int		nimaps,
	int		*prealloc)
{
	xfs_fileoff_t   start_fsb;
	xfs_filblks_t   count_fsb;
	xfs_fsblock_t	firstblock;
	int		n, error, imaps;

	*prealloc = 0;
	if ((ioflag & BMAPI_SYNC) || (offset + count) <= isize)
		return 0;

	/*
	 * If there are any real blocks past eof, then don't
	 * do any speculative allocation.
	 */
	start_fsb = XFS_B_TO_FSBT(mp, ((xfs_ufsize_t)(offset + count - 1)));
	count_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAXIOFFSET(mp));
	while (count_fsb > 0) {
		imaps = nimaps;
		firstblock = NULLFSBLOCK;
		error = XFS_BMAPI(mp, NULL, io, start_fsb, count_fsb, 0,
				  &firstblock, 0, imap, &imaps, NULL, NULL);
		if (error)
			return error;
		for (n = 0; n < imaps; n++) {
			if ((imap[n].br_startblock != HOLESTARTBLOCK) &&
			    (imap[n].br_startblock != DELAYSTARTBLOCK))
				return 0;
			start_fsb += imap[n].br_blockcount;
			count_fsb -= imap[n].br_blockcount;
		}
	}
	*prealloc = 1;
	return 0;
}

int
xfs_iomap_write_delay(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		count,
	int		ioflag,
	xfs_bmbt_irec_t *ret_imap,
	int		*nmaps)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_iocore_t	*io = &ip->i_iocore;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_off_t	aligned_offset;
	xfs_fileoff_t	ioalign;
	xfs_fsblock_t	firstblock;
	xfs_extlen_t	extsz;
	xfs_fsize_t	isize;
	int		nimaps;
	xfs_bmbt_irec_t imap[XFS_WRITE_IMAPS];
	int		prealloc, fsynced = 0;
	int		error;

	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE) != 0);

	/*
	 * Make sure that the dquots are there. This doesn't hold
	 * the ilock across a disk read.
	 */
	error = XFS_QM_DQATTACH(mp, ip, XFS_QMOPT_ILOCKED);
	if (error)
		return XFS_ERROR(error);

	if (XFS_IS_REALTIME_INODE(ip)) {
		if (!(extsz = ip->i_d.di_extsize))
			extsz = mp->m_sb.sb_rextsize;
	} else {
		extsz = ip->i_d.di_extsize;
	}

	offset_fsb = XFS_B_TO_FSBT(mp, offset);

retry:
	isize = ip->i_d.di_size;
	if (io->io_new_size > isize)
		isize = io->io_new_size;

	error = xfs_iomap_eof_want_preallocate(mp, io, isize, offset, count,
				ioflag, imap, XFS_WRITE_IMAPS, &prealloc);
	if (error)
		return error;

	if (prealloc) {
		aligned_offset = XFS_WRITEIO_ALIGN(mp, (offset + count - 1));
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
		last_fsb = ioalign + mp->m_writeio_blocks;
	} else {
		last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	}

	if (prealloc || extsz) {
		error = xfs_iomap_eof_align_last_fsb(mp, io, isize, extsz,
							&last_fsb);
		if (error)
			return error;
	}

	nimaps = XFS_WRITE_IMAPS;
	firstblock = NULLFSBLOCK;
	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
			  (xfs_filblks_t)(last_fsb - offset_fsb),
			  XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			  XFS_BMAPI_ENTIRE, &firstblock, 1, imap,
			  &nimaps, NULL, NULL);
	if (error && (error != ENOSPC))
		return XFS_ERROR(error);

	/*
	 * If bmapi returned us nothing, and if we didn't get back EDQUOT,
	 * then we must have run out of space - flush delalloc, and retry..
	 */
	if (nimaps == 0) {
		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_NOSPACE,
					io, offset, count);
		if (xfs_flush_space(ip, &fsynced, &ioflag))
			return XFS_ERROR(ENOSPC);

		error = 0;
		goto retry;
	}

	if (!(io->io_flags & XFS_IOCORE_RT)  && !ret_imap->br_startblock) {
		cmn_err(CE_PANIC,"Access to block zero:  fs <%s> inode: %lld "
                        "start_block : %llx start_off : %llx blkcnt : %llx "
                        "extent-state : %x \n",
                        (ip->i_mount)->m_fsname,
                        (long long)ip->i_ino,
                        (unsigned long long)ret_imap->br_startblock,
			(unsigned long long)ret_imap->br_startoff,
                        (unsigned long long)ret_imap->br_blockcount,
			ret_imap->br_state);
	}

	*ret_imap = imap[0];
	*nmaps = 1;

	return 0;
}

/*
 * Pass in a delayed allocate extent, convert it to real extents;
 * return to the caller the extent we create which maps on top of
 * the originating callers request.
 *
 * Called without a lock on the inode.
 */
int
xfs_iomap_write_allocate(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		count,
	xfs_bmbt_irec_t *map,
	int		*retmap)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_iocore_t    *io = &ip->i_iocore;
	xfs_fileoff_t	offset_fsb, last_block;
	xfs_fileoff_t	end_fsb, map_start_fsb;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t	free_list;
	xfs_filblks_t	count_fsb;
	xfs_bmbt_irec_t	imap[XFS_STRAT_WRITE_IMAPS];
	xfs_trans_t	*tp;
	int		i, nimaps, committed;
	int		error = 0;
	int		nres;

	*retmap = 0;

	/*
	 * Make sure that the dquots are there.
	 */
	if ((error = XFS_QM_DQATTACH(mp, ip, 0)))
		return XFS_ERROR(error);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	count_fsb = map->br_blockcount;
	map_start_fsb = map->br_startoff;

	XFS_STATS_ADD(xs_xstrat_bytes, XFS_FSB_TO_B(mp, count_fsb));

	while (count_fsb != 0) {
		/*
		 * Set up a transaction with which to allocate the
		 * backing store for the file.  Do allocations in a
		 * loop until we get some space in the range we are
		 * interested in.  The other space that might be allocated
		 * is in the delayed allocation extent on which we sit
		 * but before our buffer starts.
		 */

		nimaps = 0;
		while (nimaps == 0) {
			tp = xfs_trans_alloc(mp, XFS_TRANS_STRAT_WRITE);
			nres = XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK);
			error = xfs_trans_reserve(tp, nres,
					XFS_WRITE_LOG_RES(mp),
					0, XFS_TRANS_PERM_LOG_RES,
					XFS_WRITE_LOG_COUNT);
			if (error == ENOSPC) {
				error = xfs_trans_reserve(tp, 0,
						XFS_WRITE_LOG_RES(mp),
						0,
						XFS_TRANS_PERM_LOG_RES,
						XFS_WRITE_LOG_COUNT);
			}
			if (error) {
				xfs_trans_cancel(tp, 0);
				return XFS_ERROR(error);
			}
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);

			XFS_BMAP_INIT(&free_list, &first_block);

			nimaps = XFS_STRAT_WRITE_IMAPS;
			/*
			 * Ensure we don't go beyond eof - it is possible
			 * the extents changed since we did the read call,
			 * we dropped the ilock in the interim.
			 */

			end_fsb = XFS_B_TO_FSB(mp, ip->i_d.di_size);
			xfs_bmap_last_offset(NULL, ip, &last_block,
				XFS_DATA_FORK);
			last_block = XFS_FILEOFF_MAX(last_block, end_fsb);
			if ((map_start_fsb + count_fsb) > last_block) {
				count_fsb = last_block - map_start_fsb;
				if (count_fsb == 0) {
					error = EAGAIN;
					goto trans_cancel;
				}
			}

			/* Go get the actual blocks */
			error = XFS_BMAPI(mp, tp, io, map_start_fsb, count_fsb,
					XFS_BMAPI_WRITE, &first_block, 1,
					imap, &nimaps, &free_list, NULL);
			if (error)
				goto trans_cancel;

			error = xfs_bmap_finish(&tp, &free_list,
					first_block, &committed);
			if (error)
				goto trans_cancel;

			error = xfs_trans_commit(tp,
					XFS_TRANS_RELEASE_LOG_RES, NULL);
			if (error)
				goto error0;

			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		/*
		 * See if we were able to allocate an extent that
		 * covers at least part of the callers request
		 */

		for (i = 0; i < nimaps; i++) {
			if (!(io->io_flags & XFS_IOCORE_RT)  &&
			    !imap[i].br_startblock) {
				cmn_err(CE_PANIC,"Access to block zero:  "
					"fs <%s> inode: %lld "
					"start_block : %llx start_off : %llx "
					"blkcnt : %llx extent-state : %x \n",
					(ip->i_mount)->m_fsname,
					(long long)ip->i_ino,
					(unsigned long long)
						imap[i].br_startblock,
					(unsigned long long)
						imap[i].br_startoff,
					(unsigned long long)
				        	imap[i].br_blockcount,
					imap[i].br_state);
                        }
			if ((offset_fsb >= imap[i].br_startoff) &&
			    (offset_fsb < (imap[i].br_startoff +
					   imap[i].br_blockcount))) {
				*map = imap[i];
				*retmap = 1;
				XFS_STATS_INC(xs_xstrat_quick);
				return 0;
			}
			count_fsb -= imap[i].br_blockcount;
		}

		/* So far we have not mapped the requested part of the
		 * file, just surrounding data, try again.
		 */
		nimaps--;
		map_start_fsb = imap[nimaps].br_startoff +
				imap[nimaps].br_blockcount;
	}

trans_cancel:
	xfs_bmap_cancel(&free_list);
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
error0:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return XFS_ERROR(error);
}

int
xfs_iomap_write_unwritten(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		count)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_iocore_t    *io = &ip->i_iocore;
	xfs_fileoff_t	offset_fsb;
	xfs_filblks_t	count_fsb;
	xfs_filblks_t	numblks_fsb;
	xfs_fsblock_t	firstfsb;
	int		nimaps;
	xfs_trans_t	*tp;
	xfs_bmbt_irec_t imap;
	xfs_bmap_free_t free_list;
	uint		resblks;
	int		committed;
	int		error;

	xfs_iomap_enter_trace(XFS_IOMAP_UNWRITTEN,
				&ip->i_iocore, offset, count);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	count_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	count_fsb = (xfs_filblks_t)(count_fsb - offset_fsb);

	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;

	do {
		/*
		 * set up a transaction to convert the range of extents
		 * from unwritten to real. Do allocations in a loop until
		 * we have covered the range passed in.
		 */

		tp = xfs_trans_alloc(mp, XFS_TRANS_STRAT_WRITE);
		error = xfs_trans_reserve(tp, resblks,
				XFS_WRITE_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES,
				XFS_WRITE_LOG_COUNT);
		if (error) {
			xfs_trans_cancel(tp, 0);
			goto error0;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * Modify the unwritten extent state of the buffer.
		 */
		XFS_BMAP_INIT(&free_list, &firstfsb);
		nimaps = 1;
		error = XFS_BMAPI(mp, tp, io, offset_fsb, count_fsb,
				  XFS_BMAPI_WRITE|XFS_BMAPI_CONVERT, &firstfsb,
				  1, &imap, &nimaps, &free_list, NULL);
		if (error)
			goto error_on_bmapi_transaction;

		error = xfs_bmap_finish(&(tp), &(free_list),
				firstfsb, &committed);
		if (error)
			goto error_on_bmapi_transaction;

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			goto error0;

		if ( !(io->io_flags & XFS_IOCORE_RT)  && !imap.br_startblock) {
			cmn_err(CE_PANIC,"Access to block zero:  fs <%s> "
				"inode: %lld start_block : %llx start_off : "
				"%llx blkcnt : %llx extent-state : %x \n",
				(ip->i_mount)->m_fsname,
				(long long)ip->i_ino,
				(unsigned long long)imap.br_startblock,
				(unsigned long long)imap.br_startoff,
				(unsigned long long)imap.br_blockcount,
				imap.br_state);
        	}

		if ((numblks_fsb = imap.br_blockcount) == 0) {
			/*
			 * The numblks_fsb value should always get
			 * smaller, otherwise the loop is stuck.
			 */
			ASSERT(imap.br_blockcount);
			break;
		}
		offset_fsb += numblks_fsb;
		count_fsb -= numblks_fsb;
	} while (count_fsb > 0);

	return 0;

error_on_bmapi_transaction:
	xfs_bmap_cancel(&free_list);
	xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT));
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
error0:
	return XFS_ERROR(error);
}
