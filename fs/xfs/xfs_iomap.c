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
#include "xfs_ialloc.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_space.h"
#include "xfs_utils.h"
#include "xfs_iomap.h"

#if defined(XFS_RW_TRACE)
void
xfs_iomap_enter_trace(
	int		tag,
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	ssize_t		count)
{
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
		(void *)((unsigned long)((ip->i_new_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(ip->i_new_size & 0xffffffff)),
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
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	ssize_t		count,
	xfs_iomap_t	*iomapp,
	xfs_bmbt_irec_t	*imapp,
	int		flags)
{
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
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	xfs_bmbt_irec_t *imap,
	xfs_iomap_t	*iomapp,
	int		imaps,			/* Number of imap entries */
	int		iomaps,			/* Number of iomap entries */
	int		flags)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		pbm;
	xfs_fsblock_t	start_block;


	for (pbm = 0; imaps && pbm < iomaps; imaps--, iomapp++, imap++, pbm++) {
		iomapp->iomap_offset = XFS_FSB_TO_B(mp, imap->br_startoff);
		iomapp->iomap_delta = offset - iomapp->iomap_offset;
		iomapp->iomap_bsize = XFS_FSB_TO_B(mp, imap->br_blockcount);
		iomapp->iomap_flags = flags;

		if (XFS_IS_REALTIME_INODE(ip)) {
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
			iomapp->iomap_bn = xfs_fsb_to_db(ip, start_block);
			if (ISUNWRITTEN(imap))
				iomapp->iomap_flags |= IOMAP_UNWRITTEN;
		}

		offset += iomapp->iomap_bsize - iomapp->iomap_delta;
	}
	return pbm;	/* Return the number filled */
}

int
xfs_iomap(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	ssize_t		count,
	int		flags,
	xfs_iomap_t	*iomapp,
	int		*niomaps)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	offset_fsb, end_fsb;
	int		error = 0;
	int		lockmode = 0;
	xfs_bmbt_irec_t	imap;
	int		nimaps = 1;
	int		bmapi_flags = 0;
	int		iomap_flags = 0;

	ASSERT((ip->i_d.di_mode & S_IFMT) == S_IFREG);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	switch (flags & (BMAPI_READ | BMAPI_WRITE | BMAPI_ALLOCATE)) {
	case BMAPI_READ:
		xfs_iomap_enter_trace(XFS_IOMAP_READ_ENTER, ip, offset, count);
		lockmode = xfs_ilock_map_shared(ip);
		bmapi_flags = XFS_BMAPI_ENTIRE;
		break;
	case BMAPI_WRITE:
		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_ENTER, ip, offset, count);
		lockmode = XFS_ILOCK_EXCL;
		if (flags & BMAPI_IGNSTATE)
			bmapi_flags |= XFS_BMAPI_IGSTATE|XFS_BMAPI_ENTIRE;
		xfs_ilock(ip, lockmode);
		break;
	case BMAPI_ALLOCATE:
		xfs_iomap_enter_trace(XFS_IOMAP_ALLOC_ENTER, ip, offset, count);
		lockmode = XFS_ILOCK_SHARED;
		bmapi_flags = XFS_BMAPI_ENTIRE;

		/* Attempt non-blocking lock */
		if (flags & BMAPI_TRYLOCK) {
			if (!xfs_ilock_nowait(ip, lockmode))
				return XFS_ERROR(EAGAIN);
		} else {
			xfs_ilock(ip, lockmode);
		}
		break;
	default:
		BUG();
	}

	ASSERT(offset <= mp->m_maxioffset);
	if ((xfs_fsize_t)offset + count > mp->m_maxioffset)
		count = mp->m_maxioffset - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	error = xfs_bmapi(NULL, ip, offset_fsb,
			(xfs_filblks_t)(end_fsb - offset_fsb),
			bmapi_flags,  NULL, 0, &imap,
			&nimaps, NULL, NULL);

	if (error)
		goto out;

	switch (flags & (BMAPI_WRITE|BMAPI_ALLOCATE)) {
	case BMAPI_WRITE:
		/* If we found an extent, return it */
		if (nimaps &&
		    (imap.br_startblock != HOLESTARTBLOCK) &&
		    (imap.br_startblock != DELAYSTARTBLOCK)) {
			xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP, ip,
					offset, count, iomapp, &imap, flags);
			break;
		}

		if (flags & (BMAPI_DIRECT|BMAPI_MMAP)) {
			error = xfs_iomap_write_direct(ip, offset, count, flags,
						       &imap, &nimaps, nimaps);
		} else {
			error = xfs_iomap_write_delay(ip, offset, count, flags,
						      &imap, &nimaps);
		}
		if (!error) {
			xfs_iomap_map_trace(XFS_IOMAP_ALLOC_MAP, ip,
					offset, count, iomapp, &imap, flags);
		}
		iomap_flags = IOMAP_NEW;
		break;
	case BMAPI_ALLOCATE:
		/* If we found an extent, return it */
		xfs_iunlock(ip, lockmode);
		lockmode = 0;

		if (nimaps && !isnullstartblock(imap.br_startblock)) {
			xfs_iomap_map_trace(XFS_IOMAP_WRITE_MAP, ip,
					offset, count, iomapp, &imap, flags);
			break;
		}

		error = xfs_iomap_write_allocate(ip, offset, count,
						 &imap, &nimaps);
		break;
	}

	if (nimaps) {
		*niomaps = xfs_imap_to_bmap(ip, offset, &imap,
					    iomapp, nimaps, *niomaps, iomap_flags);
	} else if (niomaps) {
		*niomaps = 0;
	}

out:
	if (lockmode)
		xfs_iunlock(ip, lockmode);
	return XFS_ERROR(error);
}


STATIC int
xfs_iomap_eof_align_last_fsb(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	xfs_extlen_t	extsize,
	xfs_fileoff_t	*last_fsb)
{
	xfs_fileoff_t	new_last_fsb = 0;
	xfs_extlen_t	align;
	int		eof, error;

	if (XFS_IS_REALTIME_INODE(ip))
		;
	/*
	 * If mounted with the "-o swalloc" option, roundup the allocation
	 * request to a stripe width boundary if the file size is >=
	 * stripe width and we are allocating past the allocation eof.
	 */
	else if (mp->m_swidth && (mp->m_flags & XFS_MOUNT_SWALLOC) &&
	        (ip->i_size >= XFS_FSB_TO_B(mp, mp->m_swidth)))
		new_last_fsb = roundup_64(*last_fsb, mp->m_swidth);
	/*
	 * Roundup the allocation request to a stripe unit (m_dalign) boundary
	 * if the file size is >= stripe unit size, and we are allocating past
	 * the allocation eof.
	 */
	else if (mp->m_dalign && (ip->i_size >= XFS_FSB_TO_B(mp, mp->m_dalign)))
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
		error = xfs_bmap_eof(ip, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error)
			return error;
		if (eof)
			*last_fsb = new_last_fsb;
	}
	return 0;
}

STATIC int
xfs_cmn_err_fsblock_zero(
	xfs_inode_t	*ip,
	xfs_bmbt_irec_t	*imap)
{
	xfs_cmn_err(XFS_PTAG_FSBLOCK_ZERO, CE_ALERT, ip->i_mount,
			"Access to block zero in inode %llu "
			"start_block: %llx start_off: %llx "
			"blkcnt: %llx extent-state: %x\n",
		(unsigned long long)ip->i_ino,
		(unsigned long long)imap->br_startblock,
		(unsigned long long)imap->br_startoff,
		(unsigned long long)imap->br_blockcount,
		imap->br_state);
	return EFSCORRUPTED;
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
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb, resaligned;
	xfs_fsblock_t	firstfsb;
	xfs_extlen_t	extsz, temp;
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
	error = xfs_qm_dqattach_locked(ip, 0);
	if (error)
		return XFS_ERROR(error);

	rt = XFS_IS_REALTIME_INODE(ip);
	extsz = xfs_get_extsz_hint(ip);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	if ((offset + count) > ip->i_size) {
		error = xfs_iomap_eof_align_last_fsb(mp, ip, extsz, &last_fsb);
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

	error = xfs_trans_reserve_quota_nblks(tp, ip, qblocks, 0, quota_flag);
	if (error)
		goto error1;

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);

	bmapi_flag = XFS_BMAPI_WRITE;
	if ((flags & BMAPI_DIRECT) && (offset < ip->i_size || extsz))
		bmapi_flag |= XFS_BMAPI_PREALLOC;

	/*
	 * Issue the xfs_bmapi() call to allocate the blocks
	 */
	xfs_bmap_init(&free_list, &firstfsb);
	nimaps = 1;
	error = xfs_bmapi(tp, ip, offset_fsb, count_fsb, bmapi_flag,
		&firstfsb, 0, &imap, &nimaps, &free_list, NULL);
	if (error)
		goto error0;

	/*
	 * Complete the transaction
	 */
	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto error0;
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error)
		goto error_out;

	/*
	 * Copy any maps to caller's array and return any error.
	 */
	if (nimaps == 0) {
		error = ENOSPC;
		goto error_out;
	}

	if (!(imap.br_startblock || XFS_IS_REALTIME_INODE(ip))) {
		error = xfs_cmn_err_fsblock_zero(ip, &imap);
		goto error_out;
	}

	*ret_imap = imap;
	*nmaps = 1;
	return 0;

error0:	/* Cancel bmap, unlock inode, unreserve quota blocks, cancel trans */
	xfs_bmap_cancel(&free_list);
	xfs_trans_unreserve_quota_nblks(tp, ip, qblocks, 0, quota_flag);

error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	*nmaps = 0;	/* nothing set-up here */

error_out:
	return XFS_ERROR(error);
}

/*
 * If the caller is doing a write at the end of the file, then extend the
 * allocation out to the file system's write iosize.  We clean up any extra
 * space left over when the file is closed in xfs_inactive().
 */
STATIC int
xfs_iomap_eof_want_preallocate(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
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
	if ((offset + count) <= ip->i_size)
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
		error = xfs_bmapi(NULL, ip, start_fsb, count_fsb, 0,
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
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_off_t	aligned_offset;
	xfs_fileoff_t	ioalign;
	xfs_fsblock_t	firstblock;
	xfs_extlen_t	extsz;
	int		nimaps;
	xfs_bmbt_irec_t imap[XFS_WRITE_IMAPS];
	int		prealloc, flushed = 0;
	int		error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	/*
	 * Make sure that the dquots are there. This doesn't hold
	 * the ilock across a disk read.
	 */
	error = xfs_qm_dqattach_locked(ip, 0);
	if (error)
		return XFS_ERROR(error);

	extsz = xfs_get_extsz_hint(ip);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	error = xfs_iomap_eof_want_preallocate(mp, ip, offset, count,
				ioflag, imap, XFS_WRITE_IMAPS, &prealloc);
	if (error)
		return error;

retry:
	if (prealloc) {
		aligned_offset = XFS_WRITEIO_ALIGN(mp, (offset + count - 1));
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
		last_fsb = ioalign + mp->m_writeio_blocks;
	} else {
		last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	}

	if (prealloc || extsz) {
		error = xfs_iomap_eof_align_last_fsb(mp, ip, extsz, &last_fsb);
		if (error)
			return error;
	}

	nimaps = XFS_WRITE_IMAPS;
	firstblock = NULLFSBLOCK;
	error = xfs_bmapi(NULL, ip, offset_fsb,
			  (xfs_filblks_t)(last_fsb - offset_fsb),
			  XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			  XFS_BMAPI_ENTIRE, &firstblock, 1, imap,
			  &nimaps, NULL, NULL);
	if (error && (error != ENOSPC))
		return XFS_ERROR(error);

	/*
	 * If bmapi returned us nothing, and if we didn't get back EDQUOT,
	 * then we must have run out of space - flush all other inodes with
	 * delalloc blocks and retry without EOF preallocation.
	 */
	if (nimaps == 0) {
		xfs_iomap_enter_trace(XFS_IOMAP_WRITE_NOSPACE,
					ip, offset, count);
		if (flushed)
			return XFS_ERROR(ENOSPC);

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_flush_inodes(ip);
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		flushed = 1;
		error = 0;
		prealloc = 0;
		goto retry;
	}

	if (!(imap[0].br_startblock || XFS_IS_REALTIME_INODE(ip)))
		return xfs_cmn_err_fsblock_zero(ip, &imap[0]);

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
 *
 * We no longer bother to look at the incoming map - all we have to
 * guarantee is that whatever we allocate fills the required range.
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
	xfs_fileoff_t	offset_fsb, last_block;
	xfs_fileoff_t	end_fsb, map_start_fsb;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t	free_list;
	xfs_filblks_t	count_fsb;
	xfs_bmbt_irec_t	imap;
	xfs_trans_t	*tp;
	int		nimaps, committed;
	int		error = 0;
	int		nres;

	*retmap = 0;

	/*
	 * Make sure that the dquots are there.
	 */
	error = xfs_qm_dqattach(ip, 0);
	if (error)
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
			tp->t_flags |= XFS_TRANS_RESERVE;
			nres = XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK);
			error = xfs_trans_reserve(tp, nres,
					XFS_WRITE_LOG_RES(mp),
					0, XFS_TRANS_PERM_LOG_RES,
					XFS_WRITE_LOG_COUNT);
			if (error) {
				xfs_trans_cancel(tp, 0);
				return XFS_ERROR(error);
			}
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);

			xfs_bmap_init(&free_list, &first_block);

			/*
			 * it is possible that the extents have changed since
			 * we did the read call as we dropped the ilock for a
			 * while. We have to be careful about truncates or hole
			 * punchs here - we are not allowed to allocate
			 * non-delalloc blocks here.
			 *
			 * The only protection against truncation is the pages
			 * for the range we are being asked to convert are
			 * locked and hence a truncate will block on them
			 * first.
			 *
			 * As a result, if we go beyond the range we really
			 * need and hit an delalloc extent boundary followed by
			 * a hole while we have excess blocks in the map, we
			 * will fill the hole incorrectly and overrun the
			 * transaction reservation.
			 *
			 * Using a single map prevents this as we are forced to
			 * check each map we look for overlap with the desired
			 * range and abort as soon as we find it. Also, given
			 * that we only return a single map, having one beyond
			 * what we can return is probably a bit silly.
			 *
			 * We also need to check that we don't go beyond EOF;
			 * this is a truncate optimisation as a truncate sets
			 * the new file size before block on the pages we
			 * currently have locked under writeback. Because they
			 * are about to be tossed, we don't need to write them
			 * back....
			 */
			nimaps = 1;
			end_fsb = XFS_B_TO_FSB(mp, ip->i_size);
			error = xfs_bmap_last_offset(NULL, ip, &last_block,
							XFS_DATA_FORK);
			if (error)
				goto trans_cancel;

			last_block = XFS_FILEOFF_MAX(last_block, end_fsb);
			if ((map_start_fsb + count_fsb) > last_block) {
				count_fsb = last_block - map_start_fsb;
				if (count_fsb == 0) {
					error = EAGAIN;
					goto trans_cancel;
				}
			}

			/* Go get the actual blocks */
			error = xfs_bmapi(tp, ip, map_start_fsb, count_fsb,
					XFS_BMAPI_WRITE, &first_block, 1,
					&imap, &nimaps, &free_list, NULL);
			if (error)
				goto trans_cancel;

			error = xfs_bmap_finish(&tp, &free_list, &committed);
			if (error)
				goto trans_cancel;

			error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
			if (error)
				goto error0;

			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		/*
		 * See if we were able to allocate an extent that
		 * covers at least part of the callers request
		 */
		if (!(imap.br_startblock || XFS_IS_REALTIME_INODE(ip)))
			return xfs_cmn_err_fsblock_zero(ip, &imap);

		if ((offset_fsb >= imap.br_startoff) &&
		    (offset_fsb < (imap.br_startoff +
				   imap.br_blockcount))) {
			*map = imap;
			*retmap = 1;
			XFS_STATS_INC(xs_xstrat_quick);
			return 0;
		}

		/*
		 * So far we have not mapped the requested part of the
		 * file, just surrounding data, try again.
		 */
		count_fsb -= imap.br_blockcount;
		map_start_fsb = imap.br_startoff + imap.br_blockcount;
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

	xfs_iomap_enter_trace(XFS_IOMAP_UNWRITTEN, ip, offset, count);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	count_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	count_fsb = (xfs_filblks_t)(count_fsb - offset_fsb);

	/*
	 * Reserve enough blocks in this transaction for two complete extent
	 * btree splits.  We may be converting the middle part of an unwritten
	 * extent and in this case we will insert two new extents in the btree
	 * each of which could cause a full split.
	 *
	 * This reservation amount will be used in the first call to
	 * xfs_bmbt_split() to select an AG with enough space to satisfy the
	 * rest of the operation.
	 */
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;

	do {
		/*
		 * set up a transaction to convert the range of extents
		 * from unwritten to real. Do allocations in a loop until
		 * we have covered the range passed in.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_STRAT_WRITE);
		tp->t_flags |= XFS_TRANS_RESERVE;
		error = xfs_trans_reserve(tp, resblks,
				XFS_WRITE_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES,
				XFS_WRITE_LOG_COUNT);
		if (error) {
			xfs_trans_cancel(tp, 0);
			return XFS_ERROR(error);
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * Modify the unwritten extent state of the buffer.
		 */
		xfs_bmap_init(&free_list, &firstfsb);
		nimaps = 1;
		error = xfs_bmapi(tp, ip, offset_fsb, count_fsb,
				  XFS_BMAPI_WRITE|XFS_BMAPI_CONVERT, &firstfsb,
				  1, &imap, &nimaps, &free_list, NULL);
		if (error)
			goto error_on_bmapi_transaction;

		error = xfs_bmap_finish(&(tp), &(free_list), &committed);
		if (error)
			goto error_on_bmapi_transaction;

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return XFS_ERROR(error);

		if (!(imap.br_startblock || XFS_IS_REALTIME_INODE(ip)))
			return xfs_cmn_err_fsblock_zero(ip, &imap);

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
	return XFS_ERROR(error);
}
