// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_trace.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_error.h"

#include <linux/iversion.h>

kmem_zone_t	*xfs_ili_zone;		/* inode log item zone */

static inline struct xfs_inode_log_item *INODE_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_inode_log_item, ili_item);
}

/*
 * The logged size of an inode fork is always the current size of the inode
 * fork. This means that when an inode fork is relogged, the size of the logged
 * region is determined by the current state, not the combination of the
 * previously logged state + the current state. This is different relogging
 * behaviour to most other log items which will retain the size of the
 * previously logged changes when smaller regions are relogged.
 *
 * Hence operations that remove data from the inode fork (e.g. shortform
 * dir/attr remove, extent form extent removal, etc), the size of the relogged
 * inode gets -smaller- rather than stays the same size as the previously logged
 * size and this can result in the committing transaction reducing the amount of
 * space being consumed by the CIL.
 */
STATIC void
xfs_inode_item_data_fork_size(
	struct xfs_inode_log_item *iip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_inode	*ip = iip->ili_inode;

	switch (ip->i_df.if_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & XFS_ILOG_DEXT) &&
		    ip->i_df.if_nextents > 0 &&
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

	switch (ip->i_afp->if_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if ((iip->ili_fields & XFS_ILOG_AEXT) &&
		    ip->i_afp->if_nextents > 0 &&
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
		   xfs_log_dinode_size(ip->i_mount);

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

	switch (ip->i_df.if_format) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_fields &=
			~(XFS_ILOG_DDATA | XFS_ILOG_DBROOT | XFS_ILOG_DEV);

		if ((iip->ili_fields & XFS_ILOG_DEXT) &&
		    ip->i_df.if_nextents > 0 &&
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
			 * The underlying memory is guaranteed
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_df.if_bytes, 4);
			ASSERT(ip->i_df.if_u1.if_data != NULL);
			ASSERT(ip->i_disk_size > 0);
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

	switch (ip->i_afp->if_format) {
	case XFS_DINODE_FMT_EXTENTS:
		iip->ili_fields &=
			~(XFS_ILOG_ADATA | XFS_ILOG_ABROOT);

		if ((iip->ili_fields & XFS_ILOG_AEXT) &&
		    ip->i_afp->if_nextents > 0 &&
		    ip->i_afp->if_bytes > 0) {
			struct xfs_bmbt_rec *p;

			ASSERT(xfs_iext_count(ip->i_afp) ==
				ip->i_afp->if_nextents);

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
			 * The underlying memory is guaranteed
			 * to be there by xfs_idata_realloc().
			 */
			data_bytes = roundup(ip->i_afp->if_bytes, 4);
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

/*
 * Convert an incore timestamp to a log timestamp.  Note that the log format
 * specifies host endian format!
 */
static inline xfs_log_timestamp_t
xfs_inode_to_log_dinode_ts(
	struct xfs_inode		*ip,
	const struct timespec64		tv)
{
	struct xfs_log_legacy_timestamp	*lits;
	xfs_log_timestamp_t		its;

	if (xfs_inode_has_bigtime(ip))
		return xfs_inode_encode_bigtime(tv);

	lits = (struct xfs_log_legacy_timestamp *)&its;
	lits->t_sec = tv.tv_sec;
	lits->t_nsec = tv.tv_nsec;

	return its;
}

/*
 * The legacy DMAPI fields are only present in the on-disk and in-log inodes,
 * but not in the in-memory one.  But we are guaranteed to have an inode buffer
 * in memory when logging an inode, so we can just copy it from the on-disk
 * inode to the in-log inode here so that recovery of file system with these
 * fields set to non-zero values doesn't lose them.  For all other cases we zero
 * the fields.
 */
static void
xfs_copy_dm_fields_to_log_dinode(
	struct xfs_inode	*ip,
	struct xfs_log_dinode	*to)
{
	struct xfs_dinode	*dip;

	dip = xfs_buf_offset(ip->i_itemp->ili_item.li_buf,
			     ip->i_imap.im_boffset);

	if (xfs_iflags_test(ip, XFS_IPRESERVE_DM_FIELDS)) {
		to->di_dmevmask = be32_to_cpu(dip->di_dmevmask);
		to->di_dmstate = be16_to_cpu(dip->di_dmstate);
	} else {
		to->di_dmevmask = 0;
		to->di_dmstate = 0;
	}
}

static void
xfs_inode_to_log_dinode(
	struct xfs_inode	*ip,
	struct xfs_log_dinode	*to,
	xfs_lsn_t		lsn)
{
	struct inode		*inode = VFS_I(ip);

	to->di_magic = XFS_DINODE_MAGIC;
	to->di_format = xfs_ifork_format(&ip->i_df);
	to->di_uid = i_uid_read(inode);
	to->di_gid = i_gid_read(inode);
	to->di_projid_lo = ip->i_projid & 0xffff;
	to->di_projid_hi = ip->i_projid >> 16;

	memset(to->di_pad, 0, sizeof(to->di_pad));
	memset(to->di_pad3, 0, sizeof(to->di_pad3));
	to->di_atime = xfs_inode_to_log_dinode_ts(ip, inode->i_atime);
	to->di_mtime = xfs_inode_to_log_dinode_ts(ip, inode->i_mtime);
	to->di_ctime = xfs_inode_to_log_dinode_ts(ip, inode->i_ctime);
	to->di_nlink = inode->i_nlink;
	to->di_gen = inode->i_generation;
	to->di_mode = inode->i_mode;

	to->di_size = ip->i_disk_size;
	to->di_nblocks = ip->i_nblocks;
	to->di_extsize = ip->i_extsize;
	to->di_nextents = xfs_ifork_nextents(&ip->i_df);
	to->di_anextents = xfs_ifork_nextents(ip->i_afp);
	to->di_forkoff = ip->i_forkoff;
	to->di_aformat = xfs_ifork_format(ip->i_afp);
	to->di_flags = ip->i_diflags;

	xfs_copy_dm_fields_to_log_dinode(ip, to);

	/* log a dummy value to ensure log structure is fully initialised */
	to->di_next_unlinked = NULLAGINO;

	if (xfs_sb_version_has_v3inode(&ip->i_mount->m_sb)) {
		to->di_version = 3;
		to->di_changecount = inode_peek_iversion(inode);
		to->di_crtime = xfs_inode_to_log_dinode_ts(ip, ip->i_crtime);
		to->di_flags2 = ip->i_diflags2;
		to->di_cowextsize = ip->i_cowextsize;
		to->di_ino = ip->i_ino;
		to->di_lsn = lsn;
		memset(to->di_pad2, 0, sizeof(to->di_pad2));
		uuid_copy(&to->di_uuid, &ip->i_mount->m_sb.sb_meta_uuid);
		to->di_flushiter = 0;
	} else {
		to->di_version = 2;
		to->di_flushiter = ip->i_flushiter;
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
	xlog_finish_iovec(lv, *vecp, xfs_log_dinode_size(ip->i_mount));
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
	ASSERT(lip->li_buf);

	trace_xfs_inode_pin(ip, _RET_IP_);
	atomic_inc(&ip->i_pincount);
}


/*
 * This is called to unpin the inode associated with the inode log
 * item which was previously pinned with a call to xfs_inode_item_pin().
 *
 * Also wake up anyone in xfs_iunpin_wait() if the count goes to 0.
 *
 * Note that unpin can race with inode cluster buffer freeing marking the buffer
 * stale. In that case, flush completions are run from the buffer unpin call,
 * which may happen before the inode is unpinned. If we lose the race, there
 * will be no buffer attached to the log item, but the inode will be marked
 * XFS_ISTALE.
 */
STATIC void
xfs_inode_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_inode	*ip = INODE_ITEM(lip)->ili_inode;

	trace_xfs_inode_unpin(ip, _RET_IP_);
	ASSERT(lip->li_buf || xfs_iflags_test(ip, XFS_ISTALE));
	ASSERT(atomic_read(&ip->i_pincount) > 0);
	if (atomic_dec_and_test(&ip->i_pincount))
		wake_up_bit(&ip->i_flags, __XFS_IPINNED_BIT);
}

STATIC uint
xfs_inode_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
		__releases(&lip->li_ailp->ail_lock)
		__acquires(&lip->li_ailp->ail_lock)
{
	struct xfs_inode_log_item *iip = INODE_ITEM(lip);
	struct xfs_inode	*ip = iip->ili_inode;
	struct xfs_buf		*bp = lip->li_buf;
	uint			rval = XFS_ITEM_SUCCESS;
	int			error;

	ASSERT(iip->ili_item.li_buf);

	if (xfs_ipincount(ip) > 0 || xfs_buf_ispinned(bp) ||
	    (ip->i_flags & XFS_ISTALE))
		return XFS_ITEM_PINNED;

	if (xfs_iflags_test(ip, XFS_IFLUSHING))
		return XFS_ITEM_FLUSHING;

	if (!xfs_buf_trylock(bp))
		return XFS_ITEM_LOCKED;

	spin_unlock(&lip->li_ailp->ail_lock);

	/*
	 * We need to hold a reference for flushing the cluster buffer as it may
	 * fail the buffer without IO submission. In which case, we better get a
	 * reference for that completion because otherwise we don't get a
	 * reference for IO until we queue the buffer for delwri submission.
	 */
	xfs_buf_hold(bp);
	error = xfs_iflush_cluster(bp);
	if (!error) {
		if (!xfs_buf_delwri_queue(bp, buffer_list))
			rval = XFS_ITEM_FLUSHING;
		xfs_buf_relse(bp);
	} else {
		/*
		 * Release the buffer if we were unable to flush anything. On
		 * any other error, the buffer has already been released.
		 */
		if (error == -EAGAIN)
			xfs_buf_relse(bp);
		rval = XFS_ITEM_LOCKED;
	}

	spin_lock(&lip->li_ailp->ail_lock);
	return rval;
}

/*
 * Unlock the inode associated with the inode log item.
 */
STATIC void
xfs_inode_item_release(
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

STATIC void
xfs_inode_item_committing(
	struct xfs_log_item	*lip,
	xfs_csn_t		seq)
{
	INODE_ITEM(lip)->ili_commit_seq = seq;
	return xfs_inode_item_release(lip);
}

static const struct xfs_item_ops xfs_inode_item_ops = {
	.iop_size	= xfs_inode_item_size,
	.iop_format	= xfs_inode_item_format,
	.iop_pin	= xfs_inode_item_pin,
	.iop_unpin	= xfs_inode_item_unpin,
	.iop_release	= xfs_inode_item_release,
	.iop_committed	= xfs_inode_item_committed,
	.iop_push	= xfs_inode_item_push,
	.iop_committing	= xfs_inode_item_committing,
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
	iip = ip->i_itemp = kmem_cache_zalloc(xfs_ili_zone,
					      GFP_KERNEL | __GFP_NOFAIL);

	iip->ili_inode = ip;
	spin_lock_init(&iip->ili_lock);
	xfs_log_item_init(mp, &iip->ili_item, XFS_LI_INODE,
						&xfs_inode_item_ops);
}

/*
 * Free the inode log item and any memory hanging off of it.
 */
void
xfs_inode_item_destroy(
	struct xfs_inode	*ip)
{
	struct xfs_inode_log_item *iip = ip->i_itemp;

	ASSERT(iip->ili_item.li_buf == NULL);

	ip->i_itemp = NULL;
	kmem_free(iip->ili_item.li_lv_shadow);
	kmem_cache_free(xfs_ili_zone, iip);
}


/*
 * We only want to pull the item from the AIL if it is actually there
 * and its location in the log has not changed since we started the
 * flush.  Thus, we only bother if the inode's lsn has not changed.
 */
static void
xfs_iflush_ail_updates(
	struct xfs_ail		*ailp,
	struct list_head	*list)
{
	struct xfs_log_item	*lip;
	xfs_lsn_t		tail_lsn = 0;

	/* this is an opencoded batch version of xfs_trans_ail_delete */
	spin_lock(&ailp->ail_lock);
	list_for_each_entry(lip, list, li_bio_list) {
		xfs_lsn_t	lsn;

		clear_bit(XFS_LI_FAILED, &lip->li_flags);
		if (INODE_ITEM(lip)->ili_flush_lsn != lip->li_lsn)
			continue;

		lsn = xfs_ail_delete_one(ailp, lip);
		if (!tail_lsn && lsn)
			tail_lsn = lsn;
	}
	xfs_ail_update_finish(ailp, tail_lsn);
}

/*
 * Walk the list of inodes that have completed their IOs. If they are clean
 * remove them from the list and dissociate them from the buffer. Buffers that
 * are still dirty remain linked to the buffer and on the list. Caller must
 * handle them appropriately.
 */
static void
xfs_iflush_finish(
	struct xfs_buf		*bp,
	struct list_head	*list)
{
	struct xfs_log_item	*lip, *n;

	list_for_each_entry_safe(lip, n, list, li_bio_list) {
		struct xfs_inode_log_item *iip = INODE_ITEM(lip);
		bool	drop_buffer = false;

		spin_lock(&iip->ili_lock);

		/*
		 * Remove the reference to the cluster buffer if the inode is
		 * clean in memory and drop the buffer reference once we've
		 * dropped the locks we hold.
		 */
		ASSERT(iip->ili_item.li_buf == bp);
		if (!iip->ili_fields) {
			iip->ili_item.li_buf = NULL;
			list_del_init(&lip->li_bio_list);
			drop_buffer = true;
		}
		iip->ili_last_fields = 0;
		iip->ili_flush_lsn = 0;
		spin_unlock(&iip->ili_lock);
		xfs_iflags_clear(iip->ili_inode, XFS_IFLUSHING);
		if (drop_buffer)
			xfs_buf_rele(bp);
	}
}

/*
 * Inode buffer IO completion routine.  It is responsible for removing inodes
 * attached to the buffer from the AIL if they have not been re-logged and
 * completing the inode flush.
 */
void
xfs_buf_inode_iodone(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip, *n;
	LIST_HEAD(flushed_inodes);
	LIST_HEAD(ail_updates);

	/*
	 * Pull the attached inodes from the buffer one at a time and take the
	 * appropriate action on them.
	 */
	list_for_each_entry_safe(lip, n, &bp->b_li_list, li_bio_list) {
		struct xfs_inode_log_item *iip = INODE_ITEM(lip);

		if (xfs_iflags_test(iip->ili_inode, XFS_ISTALE)) {
			xfs_iflush_abort(iip->ili_inode);
			continue;
		}
		if (!iip->ili_last_fields)
			continue;

		/* Do an unlocked check for needing the AIL lock. */
		if (iip->ili_flush_lsn == lip->li_lsn ||
		    test_bit(XFS_LI_FAILED, &lip->li_flags))
			list_move_tail(&lip->li_bio_list, &ail_updates);
		else
			list_move_tail(&lip->li_bio_list, &flushed_inodes);
	}

	if (!list_empty(&ail_updates)) {
		xfs_iflush_ail_updates(bp->b_mount->m_ail, &ail_updates);
		list_splice_tail(&ail_updates, &flushed_inodes);
	}

	xfs_iflush_finish(bp, &flushed_inodes);
	if (!list_empty(&flushed_inodes))
		list_splice_tail(&flushed_inodes, &bp->b_li_list);
}

void
xfs_buf_inode_io_fail(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip;

	list_for_each_entry(lip, &bp->b_li_list, li_bio_list)
		set_bit(XFS_LI_FAILED, &lip->li_flags);
}

/*
 * This is the inode flushing abort routine.  It is called when
 * the filesystem is shutting down to clean up the inode state.  It is
 * responsible for removing the inode item from the AIL if it has not been
 * re-logged and clearing the inode's flush state.
 */
void
xfs_iflush_abort(
	struct xfs_inode	*ip)
{
	struct xfs_inode_log_item *iip = ip->i_itemp;
	struct xfs_buf		*bp = NULL;

	if (iip) {
		/*
		 * Clear the failed bit before removing the item from the AIL so
		 * xfs_trans_ail_delete() doesn't try to clear and release the
		 * buffer attached to the log item before we are done with it.
		 */
		clear_bit(XFS_LI_FAILED, &iip->ili_item.li_flags);
		xfs_trans_ail_delete(&iip->ili_item, 0);

		/*
		 * Clear the inode logging fields so no more flushes are
		 * attempted.
		 */
		spin_lock(&iip->ili_lock);
		iip->ili_last_fields = 0;
		iip->ili_fields = 0;
		iip->ili_fsync_fields = 0;
		iip->ili_flush_lsn = 0;
		bp = iip->ili_item.li_buf;
		iip->ili_item.li_buf = NULL;
		list_del_init(&iip->ili_item.li_bio_list);
		spin_unlock(&iip->ili_lock);
	}
	xfs_iflags_clear(ip, XFS_IFLUSHING);
	if (bp)
		xfs_buf_rele(bp);
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

	if (buf->i_len != sizeof(*in_f32)) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, NULL);
		return -EFSCORRUPTED;
	}

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
