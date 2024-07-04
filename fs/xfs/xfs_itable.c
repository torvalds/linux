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
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_iwalk.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_icache.h"
#include "xfs_health.h"
#include "xfs_trans.h"

/*
 * Bulk Stat
 * =========
 *
 * Use the inode walking functions to fill out struct xfs_bulkstat for every
 * allocated inode, then pass the stat information to some externally provided
 * iteration function.
 */

struct xfs_bstat_chunk {
	bulkstat_one_fmt_pf	formatter;
	struct xfs_ibulk	*breq;
	struct xfs_bulkstat	*buf;
};

/*
 * Fill out the bulkstat info for a single inode and report it somewhere.
 *
 * bc->breq->lastino is effectively the inode cursor as we walk through the
 * filesystem.  Therefore, we update it any time we need to move the cursor
 * forward, regardless of whether or not we're sending any bstat information
 * back to userspace.  If the inode is internal metadata or, has been freed
 * out from under us, we just simply keep going.
 *
 * However, if any other type of error happens we want to stop right where we
 * are so that userspace will call back with exact number of the bad inode and
 * we can send back an error code.
 *
 * Note that if the formatter tells us there's no space left in the buffer we
 * move the cursor forward and abort the walk.
 */
STATIC int
xfs_bulkstat_one_int(
	struct xfs_mount	*mp,
	struct mnt_idmap	*idmap,
	struct xfs_trans	*tp,
	xfs_ino_t		ino,
	struct xfs_bstat_chunk	*bc)
{
	struct user_namespace	*sb_userns = mp->m_super->s_user_ns;
	struct xfs_inode	*ip;		/* incore inode pointer */
	struct inode		*inode;
	struct xfs_bulkstat	*buf = bc->buf;
	xfs_extnum_t		nextents;
	int			error = -EINVAL;
	vfsuid_t		vfsuid;
	vfsgid_t		vfsgid;

	if (xfs_internal_inum(mp, ino))
		goto out_advance;

	error = xfs_iget(mp, tp, ino,
			 (XFS_IGET_DONTCACHE | XFS_IGET_UNTRUSTED),
			 XFS_ILOCK_SHARED, &ip);
	if (error == -ENOENT || error == -EINVAL)
		goto out_advance;
	if (error)
		goto out;

	/* Reload the incore unlinked list to avoid failure in inodegc. */
	if (xfs_inode_unlinked_incomplete(ip)) {
		error = xfs_inode_reload_unlinked_bucket(tp, ip);
		if (error) {
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
			xfs_irele(ip);
			return error;
		}
	}

	ASSERT(ip != NULL);
	ASSERT(ip->i_imap.im_blkno != 0);
	inode = VFS_I(ip);
	vfsuid = i_uid_into_vfsuid(idmap, inode);
	vfsgid = i_gid_into_vfsgid(idmap, inode);

	/* If this is a private inode, don't leak its details to userspace. */
	if (IS_PRIVATE(inode)) {
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		xfs_irele(ip);
		error = -EINVAL;
		goto out_advance;
	}

	/* xfs_iget returns the following without needing
	 * further change.
	 */
	buf->bs_projectid = ip->i_projid;
	buf->bs_ino = ino;
	buf->bs_uid = from_kuid(sb_userns, vfsuid_into_kuid(vfsuid));
	buf->bs_gid = from_kgid(sb_userns, vfsgid_into_kgid(vfsgid));
	buf->bs_size = ip->i_disk_size;

	buf->bs_nlink = inode->i_nlink;
	buf->bs_atime = inode_get_atime_sec(inode);
	buf->bs_atime_nsec = inode_get_atime_nsec(inode);
	buf->bs_mtime = inode_get_mtime_sec(inode);
	buf->bs_mtime_nsec = inode_get_mtime_nsec(inode);
	buf->bs_ctime = inode_get_ctime_sec(inode);
	buf->bs_ctime_nsec = inode_get_ctime_nsec(inode);
	buf->bs_gen = inode->i_generation;
	buf->bs_mode = inode->i_mode;

	buf->bs_xflags = xfs_ip2xflags(ip);
	buf->bs_extsize_blks = ip->i_extsize;

	nextents = xfs_ifork_nextents(&ip->i_df);
	if (!(bc->breq->flags & XFS_IBULK_NREXT64))
		buf->bs_extents = min(nextents, XFS_MAX_EXTCNT_DATA_FORK_SMALL);
	else
		buf->bs_extents64 = nextents;

	xfs_bulkstat_health(ip, buf);
	buf->bs_aextents = xfs_ifork_nextents(&ip->i_af);
	buf->bs_forkoff = xfs_inode_fork_boff(ip);
	buf->bs_version = XFS_BULKSTAT_VERSION_V5;

	if (xfs_has_v3inodes(mp)) {
		buf->bs_btime = ip->i_crtime.tv_sec;
		buf->bs_btime_nsec = ip->i_crtime.tv_nsec;
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			buf->bs_cowextsize_blks = ip->i_cowextsize;
	}

	switch (ip->i_df.if_format) {
	case XFS_DINODE_FMT_DEV:
		buf->bs_rdev = sysv_encode_dev(inode->i_rdev);
		buf->bs_blksize = BLKDEV_IOSIZE;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_LOCAL:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = ip->i_nblocks + ip->i_delayed_blks;
		break;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	xfs_irele(ip);

	error = bc->formatter(bc->breq, buf);
	if (error == -ECANCELED)
		goto out_advance;
	if (error)
		goto out;

out_advance:
	/*
	 * Advance the cursor to the inode that comes after the one we just
	 * looked at.  We want the caller to move along if the bulkstat
	 * information was copied successfully; if we tried to grab the inode
	 * but it's no longer allocated; or if it's internal metadata.
	 */
	bc->breq->startino = ino + 1;
out:
	return error;
}

/* Bulkstat a single inode. */
int
xfs_bulkstat_one(
	struct xfs_ibulk	*breq,
	bulkstat_one_fmt_pf	formatter)
{
	struct xfs_bstat_chunk	bc = {
		.formatter	= formatter,
		.breq		= breq,
	};
	struct xfs_trans	*tp;
	int			error;

	if (breq->idmap != &nop_mnt_idmap) {
		xfs_warn_ratelimited(breq->mp,
			"bulkstat not supported inside of idmapped mounts.");
		return -EINVAL;
	}

	ASSERT(breq->icount == 1);

	bc.buf = kzalloc(sizeof(struct xfs_bulkstat),
			GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!bc.buf)
		return -ENOMEM;

	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the inobt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(breq->mp, &tp);
	if (error)
		goto out;

	error = xfs_bulkstat_one_int(breq->mp, breq->idmap, tp,
			breq->startino, &bc);
	xfs_trans_cancel(tp);
out:
	kfree(bc.buf);

	/*
	 * If we reported one inode to userspace then we abort because we hit
	 * the end of the buffer.  Don't leak that back to userspace.
	 */
	if (error == -ECANCELED)
		error = 0;

	return error;
}

static int
xfs_bulkstat_iwalk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_ino_t		ino,
	void			*data)
{
	struct xfs_bstat_chunk	*bc = data;
	int			error;

	error = xfs_bulkstat_one_int(mp, bc->breq->idmap, tp, ino, data);
	/* bulkstat just skips over missing inodes */
	if (error == -ENOENT || error == -EINVAL)
		return 0;
	return error;
}

/*
 * Check the incoming lastino parameter.
 *
 * We allow any inode value that could map to physical space inside the
 * filesystem because if there are no inodes there, bulkstat moves on to the
 * next chunk.  In other words, the magic agino value of zero takes us to the
 * first chunk in the AG, and an agino value past the end of the AG takes us to
 * the first chunk in the next AG.
 *
 * Therefore we can end early if the requested inode is beyond the end of the
 * filesystem or doesn't map properly.
 */
static inline bool
xfs_bulkstat_already_done(
	struct xfs_mount	*mp,
	xfs_ino_t		startino)
{
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, startino);
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, startino);

	return agno >= mp->m_sb.sb_agcount ||
	       startino != XFS_AGINO_TO_INO(mp, agno, agino);
}

/* Return stat information in bulk (by-inode) for the filesystem. */
int
xfs_bulkstat(
	struct xfs_ibulk	*breq,
	bulkstat_one_fmt_pf	formatter)
{
	struct xfs_bstat_chunk	bc = {
		.formatter	= formatter,
		.breq		= breq,
	};
	struct xfs_trans	*tp;
	unsigned int		iwalk_flags = 0;
	int			error;

	if (breq->idmap != &nop_mnt_idmap) {
		xfs_warn_ratelimited(breq->mp,
			"bulkstat not supported inside of idmapped mounts.");
		return -EINVAL;
	}
	if (xfs_bulkstat_already_done(breq->mp, breq->startino))
		return 0;

	bc.buf = kzalloc(sizeof(struct xfs_bulkstat),
			GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!bc.buf)
		return -ENOMEM;

	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the inobt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(breq->mp, &tp);
	if (error)
		goto out;

	if (breq->flags & XFS_IBULK_SAME_AG)
		iwalk_flags |= XFS_IWALK_SAME_AG;

	error = xfs_iwalk(breq->mp, tp, breq->startino, iwalk_flags,
			xfs_bulkstat_iwalk, breq->icount, &bc);
	xfs_trans_cancel(tp);
out:
	kfree(bc.buf);

	/*
	 * We found some inodes, so clear the error status and return them.
	 * The lastino pointer will point directly at the inode that triggered
	 * any error that occurred, so on the next call the error will be
	 * triggered again and propagated to userspace as there will be no
	 * formatted inodes in the buffer.
	 */
	if (breq->ocount > 0)
		error = 0;

	return error;
}

/* Convert bulkstat (v5) to bstat (v1). */
void
xfs_bulkstat_to_bstat(
	struct xfs_mount		*mp,
	struct xfs_bstat		*bs1,
	const struct xfs_bulkstat	*bstat)
{
	/* memset is needed here because of padding holes in the structure. */
	memset(bs1, 0, sizeof(struct xfs_bstat));
	bs1->bs_ino = bstat->bs_ino;
	bs1->bs_mode = bstat->bs_mode;
	bs1->bs_nlink = bstat->bs_nlink;
	bs1->bs_uid = bstat->bs_uid;
	bs1->bs_gid = bstat->bs_gid;
	bs1->bs_rdev = bstat->bs_rdev;
	bs1->bs_blksize = bstat->bs_blksize;
	bs1->bs_size = bstat->bs_size;
	bs1->bs_atime.tv_sec = bstat->bs_atime;
	bs1->bs_mtime.tv_sec = bstat->bs_mtime;
	bs1->bs_ctime.tv_sec = bstat->bs_ctime;
	bs1->bs_atime.tv_nsec = bstat->bs_atime_nsec;
	bs1->bs_mtime.tv_nsec = bstat->bs_mtime_nsec;
	bs1->bs_ctime.tv_nsec = bstat->bs_ctime_nsec;
	bs1->bs_blocks = bstat->bs_blocks;
	bs1->bs_xflags = bstat->bs_xflags;
	bs1->bs_extsize = XFS_FSB_TO_B(mp, bstat->bs_extsize_blks);
	bs1->bs_extents = bstat->bs_extents;
	bs1->bs_gen = bstat->bs_gen;
	bs1->bs_projid_lo = bstat->bs_projectid & 0xFFFF;
	bs1->bs_forkoff = bstat->bs_forkoff;
	bs1->bs_projid_hi = bstat->bs_projectid >> 16;
	bs1->bs_sick = bstat->bs_sick;
	bs1->bs_checked = bstat->bs_checked;
	bs1->bs_cowextsize = XFS_FSB_TO_B(mp, bstat->bs_cowextsize_blks);
	bs1->bs_dmevmask = 0;
	bs1->bs_dmstate = 0;
	bs1->bs_aextents = bstat->bs_aextents;
}

struct xfs_inumbers_chunk {
	inumbers_fmt_pf		formatter;
	struct xfs_ibulk	*breq;
};

/*
 * INUMBERS
 * ========
 * This is how we export inode btree records to userspace, so that XFS tools
 * can figure out where inodes are allocated.
 */

/*
 * Format the inode group structure and report it somewhere.
 *
 * Similar to xfs_bulkstat_one_int, lastino is the inode cursor as we walk
 * through the filesystem so we move it forward unless there was a runtime
 * error.  If the formatter tells us the buffer is now full we also move the
 * cursor forward and abort the walk.
 */
STATIC int
xfs_inumbers_walk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	const struct xfs_inobt_rec_incore *irec,
	void			*data)
{
	struct xfs_inumbers	inogrp = {
		.xi_startino	= XFS_AGINO_TO_INO(mp, agno, irec->ir_startino),
		.xi_alloccount	= irec->ir_count - irec->ir_freecount,
		.xi_allocmask	= ~irec->ir_free,
		.xi_version	= XFS_INUMBERS_VERSION_V5,
	};
	struct xfs_inumbers_chunk *ic = data;
	int			error;

	error = ic->formatter(ic->breq, &inogrp);
	if (error && error != -ECANCELED)
		return error;

	ic->breq->startino = XFS_AGINO_TO_INO(mp, agno, irec->ir_startino) +
			XFS_INODES_PER_CHUNK;
	return error;
}

/*
 * Return inode number table for the filesystem.
 */
int
xfs_inumbers(
	struct xfs_ibulk	*breq,
	inumbers_fmt_pf		formatter)
{
	struct xfs_inumbers_chunk ic = {
		.formatter	= formatter,
		.breq		= breq,
	};
	struct xfs_trans	*tp;
	int			error = 0;

	if (xfs_bulkstat_already_done(breq->mp, breq->startino))
		return 0;

	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the inobt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(breq->mp, &tp);
	if (error)
		goto out;

	error = xfs_inobt_walk(breq->mp, tp, breq->startino, breq->flags,
			xfs_inumbers_walk, breq->icount, &ic);
	xfs_trans_cancel(tp);
out:

	/*
	 * We found some inode groups, so clear the error status and return
	 * them.  The lastino pointer will point directly at the inode that
	 * triggered any error that occurred, so on the next call the error
	 * will be triggered again and propagated to userspace as there will be
	 * no formatted inode groups in the buffer.
	 */
	if (breq->ocount > 0)
		error = 0;

	return error;
}

/* Convert an inumbers (v5) struct to a inogrp (v1) struct. */
void
xfs_inumbers_to_inogrp(
	struct xfs_inogrp		*ig1,
	const struct xfs_inumbers	*ig)
{
	/* memset is needed here because of padding holes in the structure. */
	memset(ig1, 0, sizeof(struct xfs_inogrp));
	ig1->xi_startino = ig->xi_startino;
	ig1->xi_alloccount = ig->xi_alloccount;
	ig1->xi_allocmask = ig->xi_allocmask;
}
