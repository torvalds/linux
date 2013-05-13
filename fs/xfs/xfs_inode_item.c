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
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_error.h"
#include "xfs_trace.h"


kmem_zone_t	*xfs_ili_zone;		/* inode log item zone */

static inline struct xfs_inode_log_item *INODE_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_inode_log_item, ili_item);
}


/*
 * This returns the number of iovecs needed to log the given inode item.
 *
 * We need one iovec for the inode log format structure, one for the
 * inode core, and possibly one for the inode data/extents/b-tree root
 * and one for the inode attribute data/extents/b-tree root.
 */
STATIC uint
xfs_inode_item_size(
	struct xfs_log_item	*lip)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	uint			nvecs = 2;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & XFS_ILOG_DEXT) &&
		    ip->i_d.di_nextents > 0 &&
		    ip->i_df.if_bytes > 0)
			nvecs++;
		break;

	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & XFS_ILOG_DBROOT) &&
		    ip->i_df.if_broot_bytes > 0)
			nvecs++;
		break;

	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & XFS_ILOG_DDATA) &&
		    ip->i_df.if_bytes > 0)
			nvecs++;
		break;

	case XFS_DINODE_FMT_DEV:
	case XFS_DINODE_FMT_UUID:
		break;

	default:
		ASSERT(0);
		break;
	}

	if (!XFS_IFORK_Q(ip))
		return nvecs;


	/*
	 * Log any necessary attribute data.
	 */
	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & XFS_ILOG_AEXT) &&
		    ip->i_d.di_anextents > 0 &&
		    ip->i_afp->if_bytes > 0)
			nvecs++;
		break;

	case XFS_DINODE_FMT_BTREE:
		if ((iip->ili_fields & XFS_ILOG_ABROOT) &&
		    ip->i_afp->if_broot_bytes > 0)
			nvecs++;
		break;

	case XFS_DINODE_FMT_LOCAL:
		if ((iip->ili_fields & XFS_ILOG_ADATA) &&
		    ip->i_afp->if_bytes > 0)
			nvecs++;
		break;

	default:
		ASSERT(0);
		break;
	}

	return nvecs;
}

/*
 * xfs_inode_item_format_extents - convert in-core extents to on-disk form
 *
 * For either the data or attr fork in extent format, we need to endian convert
 * the in-core extent as we place them into the on-disk inode. In this case, we
 * need to do this conversion before we write the extents into the log. Because
 * we don't have the disk inode to write into here, we allocate a buffer and
 * format the extents into it via xfs_iextents_copy(). We free the buffer in
 * the unlock routine after the copy for the log has been made.
 *
 * In the case of the data fork, the in-core and on-disk fork sizes can be
 * different due to delayed allocation extents. We only log on-disk extents
 * here, so always use the physical fork size to determine the size of the
 * buffer we need to allocate.
 */
STATIC void
xfs_inode_item_format_extents(
	struct xfs_inode	*ip,
	struct xfs_log_iovec	*vecp,
	int			whichfork,
	int			type)
{
	xfs_bmbt_rec_t		*ext_buffer;

	ext_buffer = kmem_alloc(XFS_IFORK_SIZE(ip, whichfork), KM_SLEEP);
	if (whichfork == XFS_DATA_FORK)
		ip->i_itemp->ili_extents_buf = ext_buffer;
	else
		ip->i_itemp->ili_aextents_buf = ext_buffer;

	vecp->i_addr = ext_buffer;
	vecp->i_len = xfs_iextents_copy(ip, ext_buffer, whichfork);
	vecp->i_type = type;
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given inode log item.  It fills the first item with an inode
 * log format structure, the second with the on-disk inode structure,
 * and a possible third and/or fourth with the inode data/extents/b-tree
 * root and inode attributes data/extents/b-tree root.
 */
STATIC void
xfs_inode_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_iovec	*vecp)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	uint			nvecs;
	size_t			data_bytes;
	xfs_mount_t		*mp;

	vecp->i_addr = &iip->ili_format;
	vecp->i_len  = sizeof(xfs_inode_log_format_t);
	vecp->i_type = XLOG_REG_TYPE_IFORMAT;
	vecp++;
	nvecs	     = 1;

	vecp->i_addr = &ip->i_d;
	vecp->i_len  = xfs_icdinode_size(ip->i_d.di_version);
	vecp->i_type = XLOG_REG_TYPE_ICORE;
	vecp++;
	nvecs++;

	/*
	 * If this is really an old format inode, then we need to
	 * log it as such.  This means that we have to copy the link
	 * count from the new field to the old.  We don't have to worry
	 * about the new fields, because nothing trusts them as long as
	 * the old inode version number is there.  If the superblock already
	 * has a new version number, then we don't bother converting back.
	 */
	mp = ip->i_mount;
	ASSERT(ip->i_d.di_version == 1 || xfs_sb_version_hasnlink(&mp->m_sb));
	if (ip->i_d.di_version == 1) {
		if (!xfs_sb_version_hasnlink(&mp->m_sb)) {
			/*
			 * Convert it back.
			 */
			ASSERT(ip->i_d.di_nlink <= XFS_MAXLINK_1);
			ip->i_d.di_onlink = ip->i_d.di_nlink;
		} else {
			/*
			 * The superblock version has already been bumped,
			 * so just make the conversion to the new inode
			 * format permanent.
			 */
			ip->i_d.di_version = 2;
			ip->i_d.di_onlink = 0;
			memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));
		}
	}

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);

		if ((iip->ili_fields & XFS_ILOG_DEXT) &&
		    ip->i_d.di_nextents > 0 &&
		    ip->i_df.if_bytes > 0) {
			ASSERT(ip->i_df.if_u1.if_extents != NULL);
			ASSERT(ip->i_df.if_bytes / sizeof(xfs_bmbt_rec_t) > 0);
			ASSERT(iip->ili_extents_buf == NULL);

#ifdef XFS_NATIVE_HOST
                       if (ip->i_d.di_nextents == ip->i_df.if_bytes /
                                               (uint)sizeof(xfs_bmbt_rec_t)) {
				/*
				 * There are no delayed allocation
				 * extents, so just point to the
				 * real extents array.
				 */
				vecp->i_addr = ip->i_df.if_u1.if_extents;
				vecp->i_len = ip->i_df.if_bytes;
				vecp->i_type = XLOG_REG_TYPE_IEXT;
			} else
#endif
			{
				xfs_inode_item_format_extents(ip, vecp,
					XFS_DATA_FORK, XLOG_REG_TYPE_IEXT);
			}
			ASSERT(vecp->i_len <= ip->i_df.if_bytes);
			iip->ili_format.ilf_dsize = vecp->i_len;
			vecp++;
			nvecs++;
		} else {
			iip->ili_fields &= ~XFS_ILOG_DEXT;
		}
		break;

	case XFS_DINODE_FMT_BTREE:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DEXT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);

		if ((iip->ili_fields & XFS_ILOG_DBROOT) &&
		    ip->i_df.if_broot_bytes > 0) {
			ASSERT(ip->i_df.if_broot != NULL);
			vecp->i_addr = ip->i_df.if_broot;
			vecp->i_len = ip->i_df.if_broot_bytes;
			vecp->i_type = XLOG_REG_TYPE_IBROOT;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_dsize = ip->i_df.if_broot_bytes;
		} else {
			ASSERT(!(iip->ili_fields &
				 XFS_ILOG_DBROOT));
			iip->ili_fields &= ~XFS_ILOG_DBROOT;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		iip->ili_fields &=
			~(XFS_ILOG_DEXT | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEV | XFS_ILOG_UUID);
		if ((iip->ili_fields & XFS_ILOG_DDATA) &&
		    ip->i_df.if_bytes > 0) {
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_d.di_size > 0);

			vecp->i_addr = ip->i_df.if_u1.if_data;
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_df.if_bytes, 4);
			ASSERT((ip->i_df.if_real_bytes == 0) ||
			       (ip->i_df.if_real_bytes == data_bytes));
			vecp->i_len = (int)data_bytes;
			vecp->i_type = XLOG_REG_TYPE_ILOCAL;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_dsize = (unsigned)data_bytes;
		} else {
			iip->ili_fields &= ~XFS_ILOG_DDATA;
		}
		break;

	case XFS_DINODE_FMT_DEV:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEXT | XFS_ILOG_UUID);
		if (iip->ili_fields & XFS_ILOG_DEV) {
			iip->ili_format.ilf_u.ilfu_rdev =
				ip->i_df.if_u2.if_rdev;
		}
		break;

	case XFS_DINODE_FMT_UUID:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT |
			  XFS_ILOG_DEXT | XFS_ILOG_DEV);
		if (iip->ili_fields & XFS_ILOG_UUID) {
			iip->ili_format.ilf_u.ilfu_uuid =
				ip->i_df.if_u2.if_uuid;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

	/*
	 * If there are no attributes associated with the file, then we're done.
	 */
	if (!XFS_IFORK_Q(ip)) {
		iip->ili_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT | XFS_ILOG_AEXT);
		goto out;
	}

	switch (ip->i_d.di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT);

		if ((iip->ili_fields & XFS_ILOG_AEXT) &&
		    ip->i_d.di_anextents > 0 &&
		    ip->i_afp->if_bytes > 0) {
			ASSERT(ip->i_afp->if_bytes / sizeof(xfs_bmbt_rec_t) ==
				ip->i_d.di_anextents);
			ASSERT(ip->i_afp->if_u1.if_extents != NULL);
#ifdef XFS_NATIVE_HOST
			/*
			 * There are not delayed allocation extents
			 * for attributes, so just point at the array.
			 */
			vecp->i_addr = ip->i_afp->if_u1.if_extents;
			vecp->i_len = ip->i_afp->if_bytes;
			vecp->i_type = XLOG_REG_TYPE_IATTR_EXT;
#else
			ASSERT(iip->ili_aextents_buf == NULL);
			xfs_inode_item_format_extents(ip, vecp,
					XFS_ATTR_FORK, XLOG_REG_TYPE_IATTR_EXT);
#endif
			iip->ili_format.ilf_asize = vecp->i_len;
			vecp++;
			nvecs++;
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

			vecp->i_addr = ip->i_afp->if_broot;
			vecp->i_len = ip->i_afp->if_broot_bytes;
			vecp->i_type = XLOG_REG_TYPE_IATTR_BROOT;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_asize = ip->i_afp->if_broot_bytes;
		} else {
			iip->ili_fields &= ~XFS_ILOG_ABROOT;
		}
		break;

	case XFS_DINODE_FMT_LOCAL:
		iip->ili_fields &=
			~(XFS_ILOG_AEXT | XFS_ILOG_ABROOT);

		if ((iip->ili_fields & XFS_ILOG_ADATA) &&
		    ip->i_afp->if_bytes > 0) {
			ASSERT(ip->i_afp->if_u1.if_data != NULL);

			vecp->i_addr = ip->i_afp->if_u1.if_data;
			/*
			 * Round i_bytes up to a word boundary.
			 * The underlying memory is guaranteed to
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_afp->if_bytes, 4);
			ASSERT((ip->i_afp->if_real_bytes == 0) ||
			       (ip->i_afp->if_real_bytes == data_bytes));
			vecp->i_len = (int)data_bytes;
			vecp->i_type = XLOG_REG_TYPE_IATTR_LOCAL;
			vecp++;
			nvecs++;
			iip->ili_format.ilf_asize = (unsigned)data_bytes;
		} else {
			iip->ili_fields &= ~XFS_ILOG_ADATA;
		}
		break;

	default:
		ASSERT(0);
		break;
	}

out:
	/*
	 * Now update the log format that goes out to disk from the in-core
	 * values.  We always write the inode core to make the arithmetic
	 * games in recovery easier, which isn't a big deal as just about any
	 * transaction would dirty it anyway.
	 */
	iip->ili_format.ilf_fields = XFS_ILOG_CORE |
		(iip->ili_fields & ~XFS_ILOG_TIMESTAMP);
	iip->ili_format.ilf_size = nvecs;
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

STATIC uint
xfs_inode_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	struct xfs_buf		*bp = NULL;
	uint			rval = XFS_ITEM_SUCCESS;
	int			error;

	if (xfs_ipincount(ip) > 0)
		return XFS_ITEM_PINNED;

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

	/*
	 * If the inode needed a separate buffer with which to log
	 * its extents, then free it now.
	 */
	if (iip->ili_extents_buf != NULL) {
		ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS);
		ASSERT(ip->i_d.di_nextents > 0);
		ASSERT(iip->ili_fields & XFS_ILOG_DEXT);
		ASSERT(ip->i_df.if_bytes > 0);
		kmem_free(iip->ili_extents_buf);
		iip->ili_extents_buf = NULL;
	}
	if (iip->ili_aextents_buf != NULL) {
		ASSERT(ip->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS);
		ASSERT(ip->i_d.di_anextents > 0);
		ASSERT(iip->ili_fields & XFS_ILOG_AEXT);
		ASSERT(ip->i_afp->if_bytes > 0);
		kmem_free(iip->ili_aextents_buf);
		iip->ili_aextents_buf = NULL;
	}

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
	.iop_committing = xfs_inode_item_committing
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
	iip->ili_format.ilf_type = XFS_LI_INODE;
	iip->ili_format.ilf_ino = ip->i_ino;
	iip->ili_format.ilf_blkno = ip->i_imap.im_blkno;
	iip->ili_format.ilf_len = ip->i_imap.im_len;
	iip->ili_format.ilf_boffset = ip->i_imap.im_boffset;
}

/*
 * Free the inode log item and any memory hanging off of it.
 */
void
xfs_inode_item_destroy(
	xfs_inode_t	*ip)
{
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
		if (lip->li_cb != xfs_iflush_done) {
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
		if (iip->ili_logged && blip->li_lsn == iip->ili_flush_lsn)
			need_ail++;

		blip = next;
	}

	/* make sure we capture the state of the initial inode. */
	iip = INODE_ITEM(lip);
	if (iip->ili_logged && lip->li_lsn == iip->ili_flush_lsn)
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
		struct xfs_log_item *log_items[need_ail];
		int i = 0;
		spin_lock(&ailp->xa_lock);
		for (blip = lip; blip; blip = blip->li_bio_list) {
			iip = INODE_ITEM(blip);
			if (iip->ili_logged &&
			    blip->li_lsn == iip->ili_flush_lsn) {
				log_items[i++] = blip;
			}
			ASSERT(i <= need_ail);
		}
		/* xfs_trans_ail_delete_bulk() drops the AIL lock. */
		xfs_trans_ail_delete_bulk(ailp, log_items, i,
					  SHUTDOWN_CORRUPT_INCORE);
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
		struct xfs_ail	*ailp = iip->ili_item.li_ailp;
		if (iip->ili_item.li_flags & XFS_LI_IN_AIL) {
			spin_lock(&ailp->xa_lock);
			if (iip->ili_item.li_flags & XFS_LI_IN_AIL) {
				/* xfs_trans_ail_delete() drops the AIL lock. */
				xfs_trans_ail_delete(ailp, &iip->ili_item,
						stale ?
						     SHUTDOWN_LOG_IO_ERROR :
						     SHUTDOWN_CORRUPT_INCORE);
			} else
				spin_unlock(&ailp->xa_lock);
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
 * convert an xfs_inode_log_format struct from either 32 or 64 bit versions
 * (which can have different field alignments) to the native version
 */
int
xfs_inode_item_format_convert(
	xfs_log_iovec_t		*buf,
	xfs_inode_log_format_t	*in_f)
{
	if (buf->i_len == sizeof(xfs_inode_log_format_32_t)) {
		xfs_inode_log_format_32_t *in_f32 = buf->i_addr;

		in_f->ilf_type = in_f32->ilf_type;
		in_f->ilf_size = in_f32->ilf_size;
		in_f->ilf_fields = in_f32->ilf_fields;
		in_f->ilf_asize = in_f32->ilf_asize;
		in_f->ilf_dsize = in_f32->ilf_dsize;
		in_f->ilf_ino = in_f32->ilf_ino;
		/* copy biggest field of ilf_u */
		memcpy(in_f->ilf_u.ilfu_uuid.__u_bits,
		       in_f32->ilf_u.ilfu_uuid.__u_bits,
		       sizeof(uuid_t));
		in_f->ilf_blkno = in_f32->ilf_blkno;
		in_f->ilf_len = in_f32->ilf_len;
		in_f->ilf_boffset = in_f32->ilf_boffset;
		return 0;
	} else if (buf->i_len == sizeof(xfs_inode_log_format_64_t)){
		xfs_inode_log_format_64_t *in_f64 = buf->i_addr;

		in_f->ilf_type = in_f64->ilf_type;
		in_f->ilf_size = in_f64->ilf_size;
		in_f->ilf_fields = in_f64->ilf_fields;
		in_f->ilf_asize = in_f64->ilf_asize;
		in_f->ilf_dsize = in_f64->ilf_dsize;
		in_f->ilf_ino = in_f64->ilf_ino;
		/* copy biggest field of ilf_u */
		memcpy(in_f->ilf_u.ilfu_uuid.__u_bits,
		       in_f64->ilf_u.ilfu_uuid.__u_bits,
		       sizeof(uuid_t));
		in_f->ilf_blkno = in_f64->ilf_blkno;
		in_f->ilf_len = in_f64->ilf_len;
		in_f->ilf_boffset = in_f64->ilf_boffset;
		return 0;
	}
	return EFSCORRUPTED;
}
