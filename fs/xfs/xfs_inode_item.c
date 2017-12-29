/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"


kmem_zone_t	*xfs_ili_zone;		/* inode log item zone */

static inline struct xfs_inode_log_item *INODE_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_inode_log_item, ili_item);
}

STATIC void
xfs_inode_item_data_fork_size(
	struct xfs_inode_log_item *iip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_inode	*ip = iip->ili_inode;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & XFS_ILOG_DEXT) &&
		    ip->i_d.di_nextents > 0 &&
		    ip->i_df.if_bytes > 0) {
			/* worst case, doesn't subtract delalloc extents */
			*nbytes += XFS_IFORK_DSIZE(ip);
			*nvecs += 1;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & XFS_ILOG_DBROOT) &&
		    ip->i_df.if_broot_bytes > 0) {
			*nbytes += ip->i_df.if_broot_bytes;
			*nvecs += 1;
		}
		break;
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & XFS_ILOG_DDATA) &&
		    ip->i_df.if_bytes > 0) {
			*nbytes += roundup(ip->i_df.if_bytes, 4);
			*nvecs += 1;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		break;
	default:
		ASSERT(0);
		break;
	}
}

STATIC void
xfs_inode_item_attr_fork_size(
	struct xfs_inode_log_item *iip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_inode	*ip = iip->ili_inode;

	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & XFS_ILOG_AEXT) &&
		    ip->i_d.di_anextents > 0 &&
		    ip->i_afp->if_bytes > 0) {
			/* worst case, doesn't subtract unused space */
			*nbytes += XFS_IFORK_ASIZE(ip);
			*nvecs += 1;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & XFS_ILOG_ABROOT) &&
		    ip->i_afp->if_broot_bytes > 0) {
			*nbytes += ip->i_afp->if_broot_bytes;
			*nvecs += 1;
		}
		break;
	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & XFS_ILOG_ADATA) &&
		    ip->i_afp->if_bytes > 0) {
			*nbytes += roundup(ip->i_afp->if_bytes, 4);
			*nvecs += 1;
		}
		break;
	default:
		ASSERT(0);
		break;
	}
}

/*
 * This returns the number of iovecs needed to log the given inode item.
 *
 * We need one iovec for the inode log format structure, one for the
 * inode core, and possibly one for the inode data/extents/b-tree root
 * and one for the inode attribute data/extents/b-tree root.
 */
STATIC void
xfs_inode_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;

	*nvecs += 2;
	*nbytes += sizeof(struct xfs_inode_log_format) +
		   xfs_log_dinode_size(ip->i_d.di_version);

	xfs_inode_item_data_fork_size(iip, nvecs, nbytes);
	if (XFS_IFORK_Q(ip))
		xfs_inode_item_attr_fork_size(iip, nvecs, nbytes);
}

STATIC void
xfs_inode_item_format_data_fork(
	struct xfs_inode_log_item *iip,
	struct xfs_inode_log_format *ilf,
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp)
{
	struct xfs_inode	*ip = iip->ili_inode;
	size_t			data_bytes;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT | XFS_ILOG_DEV);

		if ((iip->ili_fields & XFS_ILOG_DEXT) &&
		    ip->i_d.di_nextents > 0 &&
		    ip->i_df.if_bytes > 0) {
			struct xfs_bmbt_rec *p;

			ASSERT(xfs_iext_count(&ip->i_df) > 0);

			p = xlog_prepare_iovec(lv, vecp, XLOG_REG_TYPE_IEXT);
			data_bytes = xfs_iextents_copy(ip, p, XFS_DATA_FORK);
			xlog_finish_iovec(lv, *vecp, data_bytes);

			ASSERT(data_bytes <= ip->i_df.if_bytes);

			ilf->ilf_dsize = data_bytes;
			ilf->ilf_size++;
		} else {
			iip->ili_fields &= ~XFS_ILOG_DEXT;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DEXT | XFS_ILOG_DEV);

		if ((iip->ili_fields & XFS_ILOG_DBROOT) &&
		    ip->i_df.if_broot_bytes > 0) {
			ASSERT(ip->i_df.if_broot != NULL);
			xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_IBROOT,
					ip->i_df.if_broot,
					ip->i_df.if_broot_bytes);
			ilf->ilf_dsize = ip->i_df.if_broot_bytes;
			ilf->ilf_size++;
		} else {
			ASSERT(!(iip->ili_fields &
				 XFS_ILOG_DBROOT));
			iip->ili_fields &= ~XFS_ILOG_DBROOT;
		}
		break;
	case XFS_DINODE_FMT_LOCAL:
		iip->ili_fields &=
			~(XFS_ILOG_DEXT | XFS_ILOG_DBROOT | XFS_ILOG_DEV);
		if ((iip->ili_fields & XFS_ILOG_DDATA) &&
		    ip->i_df.if_bytes > 0) {
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_df.if_bytes, 4);
			ASSERT(ip->i_df.if_real_bytes == 0 ||
			       ip->i_df.if_real_bytes >= data_bytes);
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_d.di_size > 0);
			xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_ILOCAL,
					ip->i_df.if_u1.if_data, data_bytes);
			ilf->ilf_dsize = (unsigned)data_bytes;
			ilf->ilf_size++;
		} else {
			iip->ili_fields &= ~XFS_ILOG_DDATA;
		}
		break;
	case XFS_DINODE_FMT_DEV:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT | XFS_ILOG_DEXT);
		if (iip->ili_fields & XFS_ILOG_DEV)
			ilf->ilf_u.ilfu_rdev = sysv_encode_dev(VFS_I(ip)->i_rdev);
		break;
	default:
		ASSERT(0);
		break;
	}
}

STATIC void
xfs_inode_item_format_attr_fork(
	struct xfs_inode_log_item *iip,
	struct xfs_inode_log_format *ilf,
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp)
{
	struct xfs_inode	*ip = iip->ili_inode;
	size_t			data_bytes;

	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT);

		if ((iip->ili_fields & XFS_ILOG_AEXT) &&
		    ip->i_d.di_anextents > 0 &&
		    ip->i_afp->if_bytes > 0) {
			struct xfs_bmbt_rec *p;

			ASSERT(xfs_iext_count(ip->i_afp) ==
				ip->i_d.di_anextents);

			p = xlog_prepare_iovec(lv, vecp, XLOG_REG_TYPE_IATTR_EXT);
			data_bytes = xfs_iextents_copy(ip, p, XFS_ATTR_FORK);
			xlog_finish_iovec(lv, *vecp, data_bytes);

			ilf->ilf_asize = data_bytes;
			ilf->ilf_size++;
		} else {
			iip->ili_fields &= ~XFS_ILOG_AEXT;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		iip->ili_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_AEXT);

		if ((iip->ili_fields & XFS_ILOG_ABROOT) &&
		    ip->i_afp->if_broot_bytes > 0) {
			ASSERT(ip->i_afp->if_broot != NULL);

			xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_IATTR_BROOT,
					ip->i_afp->if_broot,
					ip->i_afp->if_broot_bytes);
			ilf->ilf_asize = ip->i_afp->if_broot_bytes;
			ilf->ilf_size++;
		} else {
			iip->ili_fields &= ~XFS_ILOG_ABROOT;
		}
		break;
	case XFS_DINODE_FMT_LOCAL:
		iip->ili_fields &=
			~(XFS_ILOG_AEXT | XFS_ILOG_ABROOT);

		if ((iip->ili_fields & XFS_ILOG_ADATA) &&
		    ip->i_afp->if_bytes > 0) {
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_afp->if_bytes, 4);
			ASSERT(ip->i_afp->if_real_bytes == 0 ||
			       ip->i_afp->if_real_bytes >= data_bytes);
			ASSERT(ip->i_afp->if_u1.if_data != NULL);
			xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_IATTR_LOCAL,
					ip->i_afp->if_u1.if_data,
					data_bytes);
			ilf->ilf_asize = (unsigned)data_bytes;
			ilf->ilf_size++;
		} else {
			iip->ili_fields &= ~XFS_ILOG_ADATA;
		}
		break;
	default:
		ASSERT(0);
		break;
	}
}

static void
xfs_inode_to_log_dinode(
	struct xfs_inode	*ip,
	struct xfs_log_dinode	*to,
	xfs_lsn_t		lsn)
{
	struct xfs_icdinode	*from = &ip->i_d;
	struct inode		*inode = VFS_I(ip);

	to->di_magic = XFS_DINODE_MAGIC;

	to->di_version = from->di_version;
	to->di_format = from->di_format;
	to->di_uid = from->di_uid;
	to->di_gid = from->di_gid;
	to->di_projid_lo = from->di_projid_lo;
	to->di_projid_hi = from->di_projid_hi;

	memset(to->di_pad, 0, sizeof(to->di_pad));
	memset(to->di_pad3, 0, sizeof(to->di_pad3));
	to->di_atime.t_sec = inode->i_atime.tv_sec;
	to->di_atime.t_nsec = inode->i_atime.tv_nsec;
	to->di_mtime.t_sec = inode->i_mtime.tv_sec;
	to->di_mtime.t_nsec = inode->i_mtime.tv_nsec;
	to->di_ctime.t_sec = inode->i_ctime.tv_sec;
	to->di_ctime.t_nsec = inode->i_ctime.tv_nsec;
	to->di_nlink = inode->i_nlink;
	to->di_gen = inode->i_generation;
	to->di_mode = inode->i_mode;

	to->di_size = from->di_size;
	to->di_nblocks = from->di_nblocks;
	to->di_extsize = from->di_extsize;
	to->di_nextents = from->di_nextents;
	to->di_anextents = from->di_anextents;
	to->di_forkoff = from->di_forkoff;
	to->di_aformat = from->di_aformat;
	to->di_dmevmask = from->di_dmevmask;
	to->di_dmstate = from->di_dmstate;
	to->di_flags = from->di_flags;

	/* log a dummy value to ensure log structure is fully initialised */
	to->di_next_unlinked = NULLAGINO;

	if (from->di_version == 3) {
		to->di_changecount = inode->i_version;
		to->di_crtime.t_sec = from->di_crtime.t_sec;
		to->di_crtime.t_nsec = from->di_crtime.t_nsec;
		to->di_flags2 = from->di_flags2;
		to->di_cowextsize = from->di_cowextsize;
		to->di_ino = ip->i_ino;
		to->di_lsn = lsn;
		memset(to->di_pad2, 0, sizeof(to->di_pad2));
		uuid_copy(&to->di_uuid, &ip->i_mount->m_sb.sb_meta_uuid);
		to->di_flushiter = 0;
	} else {
		to->di_flushiter = from->di_flushiter;
	}
}

/*
 * Format the inode core. Current timestamp data is only in the VFS inode
 * fields, so we need to grab them from there. Hence rather than just copying
 * the XFS inode core structure, format the fields directly into the iovec.
 */
static void
xfs_inode_item_format_core(
	struct xfs_inode	*ip,
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp)
{
	struct xfs_log_dinode	*dic;

	dic = xlog_prepare_iovec(lv, vecp, XLOG_REG_TYPE_ICORE);
	xfs_inode_to_log_dinode(ip, dic, ip->i_itemp->ili_item.li_lsn);
	xlog_finish_iovec(lv, *vecp, xfs_log_dinode_size(ip->i_d.di_version));
}

/*
 * This is called to fill in the vector of log iovecs for the given inode
 * log item.  It fills the first item with an inode log format structure,
 * the second with the on-disk inode structure, and a possible third and/or
 * fourth with the inode data/extents/b-tree root and inode attributes
 * data/extents/b-tree root.
 *
 * Note: Always use the 64 bit inode log format structure so we don't
 * leave an uninitialised hole in the format item on 64 bit systems. Log
 * recovery on 32 bit systems handles this just fine, so there's no reason
 * for not using an initialising the properly padded structure all the time.
 */
STATIC void
xfs_inode_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	struct xfs_log_iovec	*vecp = NULL;
	struct xfs_inode_log_format *ilf;

	ASSERT(ip->i_d.di_version > 1);

	ilf = xlog_prepare_iovec(lv, &vecp, XLOG_REG_TYPE_IFORMAT);
	ilf->ilf_type = XFS_LI_INODE;
	ilf->ilf_ino = ip->i_ino;
	ilf->ilf_blkno = ip->i_imap.im_blkno;
	ilf->ilf_len = ip->i_imap.im_len;
	ilf->ilf_boffset = ip->i_imap.im_boffset;
	ilf->ilf_fields = XFS_ILOG_CORE;
	ilf->ilf_size = 2; /* format + core */

	/*
	 * make sure we don't leak uninitialised data into the log in the case
	 * when we don't log every field in the inode.
	 */
	ilf->ilf_dsize = 0;
	ilf->ilf_asize = 0;
	ilf->ilf_pad = 0;
	memset(&ilf->ilf_u, 0, sizeof(ilf->ilf_u));

	xlog_finish_iovec(lv, vecp, sizeof(*ilf));

	xfs_inode_item_format_core(ip, lv, &vecp);
	xfs_inode_item_format_data_fork(iip, ilf, lv, &vecp);
	if (XFS_IFORK_Q(ip)) {
		xfs_inode_item_format_attr_fork(iip, ilf, lv, &vecp);
	} else {
		iip->ili_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT | XFS_ILOG_AEXT);
	}

	/* update the format with the exact fields we actually logged */
	ilf->ilf_fields |= (iip->ili_fields & ~XFS_ILOG_TIMESTAMP);
}

/*
 * This is called to pin the inode associated with the inode log
 * item in memory so it cannot be written out.
 */
STATIC void
xfs_inode_item_pin(
	struct xfs_log_item	*lip)
{
	struct xfs_inode	*ip = INODE_ITEM(lip)->ili_inode;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	trace_xfs_inode_pin(ip, _RET_IP_);
	atomic_inc(&ip->i_pincount);
}


/*
 * This is called to unpin the inode associated with the inode log
 * item which was previously pinned with a call to xfs_inode_item_pin().
 *
 * Also wake up anyone in xfs_iunpin_wait() if the count goes to 0.
 */
STATIC void
xfs_inode_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_inode	*ip = INODE_ITEM(lip)->ili_inode;

	trace_xfs_inode_unpin(ip, _RET_IP_);
	ASSERT(atomic_read(&ip->i_pincount) > 0);
	if (atomic_dec_and_test(&ip->i_pincount))
		wake_up_bit(&ip->i_flags, __XFS_IPINNED_BIT);
}

/*
 * Callback used to mark a buffer with XFS_LI_FAILED when items in the buffer
 * have been failed during writeback
 *
 * This informs the AIL that the inode is already flush locked on the next push,
 * and acquires a hold on the buffer to ensure that it isn't reclaimed before
 * dirty data makes it to disk.
 */
STATIC void
xfs_inode_item_error(
	struct xfs_log_item	*lip,
	struct xfs_buf		*bp)
{
	ASSERT(xfs_isiflocked(INODE_ITEM(lip)->ili_inode));
	xfs_set_li_failed(lip, bp);
}

STATIC uint
xfs_inode_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
		__releases(&lip->li_ailp->xa_lock)
		__acquires(&lip->li_ailp->xa_lock)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	struct xfs_buf		*bp = lip->li_buf;
	uint			rval = XFS_ITEM_SUCCESS;
	int			error;

	if (xfs_ipincount(ip) > 0)
		return XFS_ITEM_PINNED;

	/*
	 * The buffer containing this item failed to be written back
	 * previously. Resubmit the buffer for IO.
	 */
	if (lip->li_flags & XFS_LI_FAILED) {
		if (!xfs_buf_trylock(bp))
			return XFS_ITEM_LOCKED;

		if (!xfs_buf_resubmit_failed_buffers(bp, lip, buffer_list))
			rval = XFS_ITEM_FLUSHING;

		xfs_buf_unlock(bp);
		return rval;
	}

	if (!xfs_ilock_nowait(ip, XFS_ILOCK_SHARED))
		return XFS_ITEM_LOCKED;

	/*
	 * Re-check the pincount now that we stabilized the value by
	 * taking the ilock.
	 */
	if (xfs_ipincount(ip) > 0) {
		rval = XFS_ITEM_PINNED;
		goto out_unlock;
	}

	/*
	 * Stale inode items should force out the iclog.
	 */
	if (ip->i_flags & XFS_ISTALE) {
		rval = XFS_ITEM_PINNED;
		goto out_unlock;
	}

	/*
	 * Someone else is already flushing the inode.  Nothing we can do
	 * here but wait for the flush to finish and remove the item from
	 * the AIL.
	 */
	if (!xfs_iflock_nowait(ip)) {
		rval = XFS_ITEM_FLUSHING;
		goto out_unlock;
	}

	ASSERT(iip->ili_fields != 0 || XFS_FORCED_SHUTDOWN(ip->i_mount));
	ASSERT(iip->ili_logged == 0 || XFS_FORCED_SHUTDOWN(ip->i_mount));

	spin_unlock(&lip->li_ailp->xa_lock);

	error = xfs_iflush(ip, &bp);
	if (!error) {
		if (!xfs_buf_delwri_queue(bp, buffer_list))
			rval = XFS_ITEM_FLUSHING;
		xfs_buf_relse(bp);
	}

	spin_lock(&lip->li_ailp->xa_lock);
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return rval;
}

/*
 * Unlock the inode associated with the inode log item.
 * Clear the fields of the inode and inode log item that
 * are specific to the current transaction.  If the
 * hold flags is set, do not unlock the inode.
 */
STATIC void
xfs_inode_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	unsigned short		lock_flags;

	ASSERT(ip->i_itemp != NULL);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	lock_flags = iip->ili_lock_flags;
	iip->ili_lock_flags = 0;
	if (lock_flags)
		xfs_iunlock(ip, lock_flags);
}

/*
 * This is called to find out where the oldest active copy of the inode log
 * item in the on disk log resides now that the last log write of it completed
 * at the given lsn.  Since we always re-log all dirty data in an inode, the
 * latest copy in the on disk log is the only one that matters.  Therefore,
 * simply return the given lsn.
 *
 * If the inode has been marked stale because the cluster is being freed, we
 * don't want to (re-)insert this inode into the AIL. There is a race condition
 * where the cluster buffer may be unpinned before the inode is inserted into
 * the AIL during transaction committed processing. If the buffer is unpinned
 * before the inode item has been committed and inserted, then it is possible
 * for the buffer to be written and IO completes before the inode is inserted
 * into the AIL. In that case, we'd be inserting a clean, stale inode into the
 * AIL which will never get removed. It will, however, get reclaimed which
 * triggers an assert in xfs_inode_free() complaining about freein an inode
 * still in the AIL.
 *
 * To avoid this, just unpin the inode directly and return a LSN of -1 so the
 * transaction committed code knows that it does not need to do any further
 * processing on the item.
 */
STATIC xfs_lsn_t
xfs_inode_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;

	if (xfs_iflags_test(ip, XFS_ISTALE)) {
		xfs_inode_item_unpin(lip, 0);
		return -1;
	}
	return lsn;
}

/*
 * XXX rcc - this one really has to do something.  Probably needs
 * to stamp in a new field in the incore inode.
 */
STATIC void
xfs_inode_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	INODE_ITEM(lip)->ili_last_lsn = lsn;
}

/*
 * This is the ops vector shared by all buf log items.
 */
static const struct xfs_item_ops xfs_inode_item_ops = {
	.iop_size	= xfs_inode_item_size,
	.iop_format	= xfs_inode_item_format,
	.iop_pin	= xfs_inode_item_pin,
	.iop_unpin	= xfs_inode_item_unpin,
	.iop_unlock	= xfs_inode_item_unlock,
	.iop_committed	= xfs_inode_item_committed,
	.iop_push	= xfs_inode_item_push,
	.iop_committing = xfs_inode_item_committing,
	.iop_error	= xfs_inode_item_error
};


/*
 * Initialize the inode log item for a newly allocated (in-core) inode.
 */
void
xfs_inode_item_init(
	struct xfs_inode	*ip,
	struct xfs_mount	*mp)
{
	struct xfs_inode_log_item *iip;

	ASSERT(ip->i_itemp == NULL);
	iip = ip->i_itemp = kmem_zone_zalloc(xfs_ili_zone, KM_SLEEP);

	iip->ili_inode = ip;
	xfs_log_item_init(mp, &iip->ili_item, XFS_LI_INODE,
						&xfs_inode_item_ops);
}

/*
 * Free the inode log item and any memory hanging off of it.
 */
void
xfs_inode_item_destroy(
	xfs_inode_t	*ip)
{
	kmem_free(ip->i_itemp->ili_item.li_lv_shadow);
	kmem_zone_free(xfs_ili_zone, ip->i_itemp);
}


/*
 * This is the inode flushing I/O completion routine.  It is called
 * from interrupt level when the buffer containing the inode is
 * flushed to disk.  It is responsible for removing the inode item
 * from the AIL if it has not been re-logged, and unlocking the inode's
 * flush lock.
 *
 * To reduce AIL lock traffic as much as possible, we scan the buffer log item
 * list for other inodes that will run this function. We remove them from the
 * buffer list so we can process all the inode IO completions in one AIL lock
 * traversal.
 */
void
xfs_iflush_done(
	struct xfs_buf		*bp,
	struct xfs_log_item	*lip)
{
	struct xfs_inode_log_item *iip;
	struct xfs_log_item	*blip;
	struct xfs_log_item	*next;
	struct xfs_log_item	*prev;
	struct xfs_ail		*ailp = lip->li_ailp;
	int			need_ail = 0;

	/*
	 * Scan the buffer IO completions for other inodes being completed and
	 * attach them to the current inode log item.
	 */
	blip = bp->b_fspriv;
	prev = NULL;
	while (blip != NULL) {
		if (blip->li_cb != xfs_iflush_done) {
			prev = blip;
			blip = blip->li_bio_list;
			continue;
		}

		/* remove from list */
		next = blip->li_bio_list;
		if (!prev) {
			bp->b_fspriv = next;
		} else {
			prev->li_bio_list = next;
		}

		/* add to current list */
		blip->li_bio_list = lip->li_bio_list;
		lip->li_bio_list = blip;

		/*
		 * while we have the item, do the unlocked check for needing
		 * the AIL lock.
		 */
		iip = INODE_ITEM(blip);
		if ((iip->ili_logged && blip->li_lsn == iip->ili_flush_lsn) ||
		    (blip->li_flags & XFS_LI_FAILED))
			need_ail++;

		blip = next;
	}

	/* make sure we capture the state of the initial inode. */
	iip = INODE_ITEM(lip);
	if ((iip->ili_logged && lip->li_lsn == iip->ili_flush_lsn) ||
	    lip->li_flags & XFS_LI_FAILED)
		need_ail++;

	/*
	 * We only want to pull the item from the AIL if it is
	 * actually there and its location in the log has not
	 * changed since we started the flush.  Thus, we only bother
	 * if the ili_logged flag is set and the inode's lsn has not
	 * changed.  First we check the lsn outside
	 * the lock since it's cheaper, and then we recheck while
	 * holding the lock before removing the inode from the AIL.
	 */
	if (need_ail) {
		bool			mlip_changed = false;

		/* this is an opencoded batch version of xfs_trans_ail_delete */
		spin_lock(&ailp->xa_lock);
		for (blip = lip; blip; blip = blip->li_bio_list) {
			if (INODE_ITEM(blip)->ili_logged &&
			    blip->li_lsn == INODE_ITEM(blip)->ili_flush_lsn)
				mlip_changed |= xfs_ail_delete_one(ailp, blip);
			else {
				xfs_clear_li_failed(blip);
			}
		}

		if (mlip_changed) {
			if (!XFS_FORCED_SHUTDOWN(ailp->xa_mount))
				xlog_assign_tail_lsn_locked(ailp->xa_mount);
			if (list_empty(&ailp->xa_ail))
				wake_up_all(&ailp->xa_empty);
		}
		spin_unlock(&ailp->xa_lock);

		if (mlip_changed)
			xfs_log_space_wake(ailp->xa_mount);
	}

	/*
	 * clean up and unlock the flush lock now we are done. We can clear the
	 * ili_last_fields bits now that we know that the data corresponding to
	 * them is safely on disk.
	 */
	for (blip = lip; blip; blip = next) {
		next = blip->li_bio_list;
		blip->li_bio_list = NULL;

		iip = INODE_ITEM(blip);
		iip->ili_logged = 0;
		iip->ili_last_fields = 0;
		xfs_ifunlock(iip->ili_inode);
	}
}

/*
 * This is the inode flushing abort routine.  It is called from xfs_iflush when
 * the filesystem is shutting down to clean up the inode state.  It is
 * responsible for removing the inode item from the AIL if it has not been
 * re-logged, and unlocking the inode's flush lock.
 */
void
xfs_iflush_abort(
	xfs_inode_t		*ip,
	bool			stale)
{
	xfs_inode_log_item_t	*iip = ip->i_itemp;

	if (iip) {
		if (iip->ili_item.li_flags & XFS_LI_IN_AIL) {
			xfs_trans_ail_remove(&iip->ili_item,
					     stale ? SHUTDOWN_LOG_IO_ERROR :
						     SHUTDOWN_CORRUPT_INCORE);
		}
		iip->ili_logged = 0;
		/*
		 * Clear the ili_last_fields bits now that we know that the
		 * data corresponding to them is safely on disk.
		 */
		iip->ili_last_fields = 0;
		/*
		 * Clear the inode logging fields so no more flushes are
		 * attempted.
		 */
		iip->ili_fields = 0;
		iip->ili_fsync_fields = 0;
	}
	/*
	 * Release the inode's flush lock since we're done with it.
	 */
	xfs_ifunlock(ip);
}

void
xfs_istale_done(
	struct xfs_buf		*bp,
	struct xfs_log_item	*lip)
{
	xfs_iflush_abort(INODE_ITEM(lip)->ili_inode, true);
}

/*
 * convert an xfs_inode_log_format struct from the old 32 bit version
 * (which can have different field alignments) to the native 64 bit version
 */
int
xfs_inode_item_format_convert(
	struct xfs_log_iovec		*buf,
	struct xfs_inode_log_format	*in_f)
{
	struct xfs_inode_log_format_32	*in_f32 = buf->i_addr;

	if (buf->i_len != sizeof(*in_f32))
		return -EFSCORRUPTED;

	in_f->ilf_type = in_f32->ilf_type;
	in_f->ilf_size = in_f32->ilf_size;
	in_f->ilf_fields = in_f32->ilf_fields;
	in_f->ilf_asize = in_f32->ilf_asize;
	in_f->ilf_dsize = in_f32->ilf_dsize;
	in_f->ilf_ino = in_f32->ilf_ino;
	memcpy(&in_f->ilf_u, &in_f32->ilf_u, sizeof(in_f->ilf_u));
	in_f->ilf_blkno = in_f32->ilf_blkno;
	in_f->ilf_len = in_f32->ilf_len;
	in_f->ilf_boffset = in_f32->ilf_boffset;
	return 0;
}
