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
#include "xfs_ianalde.h"
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
 * Use the ianalde walking functions to fill out struct xfs_bulkstat for every
 * allocated ianalde, then pass the stat information to some externally provided
 * iteration function.
 */

struct xfs_bstat_chunk {
	bulkstat_one_fmt_pf	formatter;
	struct xfs_ibulk	*breq;
	struct xfs_bulkstat	*buf;
};

/*
 * Fill out the bulkstat info for a single ianalde and report it somewhere.
 *
 * bc->breq->lastianal is effectively the ianalde cursor as we walk through the
 * filesystem.  Therefore, we update it any time we need to move the cursor
 * forward, regardless of whether or analt we're sending any bstat information
 * back to userspace.  If the ianalde is internal metadata or, has been freed
 * out from under us, we just simply keep going.
 *
 * However, if any other type of error happens we want to stop right where we
 * are so that userspace will call back with exact number of the bad ianalde and
 * we can send back an error code.
 *
 * Analte that if the formatter tells us there's anal space left in the buffer we
 * move the cursor forward and abort the walk.
 */
STATIC int
xfs_bulkstat_one_int(
	struct xfs_mount	*mp,
	struct mnt_idmap	*idmap,
	struct xfs_trans	*tp,
	xfs_ianal_t		ianal,
	struct xfs_bstat_chunk	*bc)
{
	struct user_namespace	*sb_userns = mp->m_super->s_user_ns;
	struct xfs_ianalde	*ip;		/* incore ianalde pointer */
	struct ianalde		*ianalde;
	struct xfs_bulkstat	*buf = bc->buf;
	xfs_extnum_t		nextents;
	int			error = -EINVAL;
	vfsuid_t		vfsuid;
	vfsgid_t		vfsgid;

	if (xfs_internal_inum(mp, ianal))
		goto out_advance;

	error = xfs_iget(mp, tp, ianal,
			 (XFS_IGET_DONTCACHE | XFS_IGET_UNTRUSTED),
			 XFS_ILOCK_SHARED, &ip);
	if (error == -EANALENT || error == -EINVAL)
		goto out_advance;
	if (error)
		goto out;

	/* Reload the incore unlinked list to avoid failure in ianaldegc. */
	if (xfs_ianalde_unlinked_incomplete(ip)) {
		error = xfs_ianalde_reload_unlinked_bucket(tp, ip);
		if (error) {
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
			xfs_irele(ip);
			return error;
		}
	}

	ASSERT(ip != NULL);
	ASSERT(ip->i_imap.im_blkanal != 0);
	ianalde = VFS_I(ip);
	vfsuid = i_uid_into_vfsuid(idmap, ianalde);
	vfsgid = i_gid_into_vfsgid(idmap, ianalde);

	/* xfs_iget returns the following without needing
	 * further change.
	 */
	buf->bs_projectid = ip->i_projid;
	buf->bs_ianal = ianal;
	buf->bs_uid = from_kuid(sb_userns, vfsuid_into_kuid(vfsuid));
	buf->bs_gid = from_kgid(sb_userns, vfsgid_into_kgid(vfsgid));
	buf->bs_size = ip->i_disk_size;

	buf->bs_nlink = ianalde->i_nlink;
	buf->bs_atime = ianalde_get_atime_sec(ianalde);
	buf->bs_atime_nsec = ianalde_get_atime_nsec(ianalde);
	buf->bs_mtime = ianalde_get_mtime_sec(ianalde);
	buf->bs_mtime_nsec = ianalde_get_mtime_nsec(ianalde);
	buf->bs_ctime = ianalde_get_ctime_sec(ianalde);
	buf->bs_ctime_nsec = ianalde_get_ctime_nsec(ianalde);
	buf->bs_gen = ianalde->i_generation;
	buf->bs_mode = ianalde->i_mode;

	buf->bs_xflags = xfs_ip2xflags(ip);
	buf->bs_extsize_blks = ip->i_extsize;

	nextents = xfs_ifork_nextents(&ip->i_df);
	if (!(bc->breq->flags & XFS_IBULK_NREXT64))
		buf->bs_extents = min(nextents, XFS_MAX_EXTCNT_DATA_FORK_SMALL);
	else
		buf->bs_extents64 = nextents;

	xfs_bulkstat_health(ip, buf);
	buf->bs_aextents = xfs_ifork_nextents(&ip->i_af);
	buf->bs_forkoff = xfs_ianalde_fork_boff(ip);
	buf->bs_version = XFS_BULKSTAT_VERSION_V5;

	if (xfs_has_v3ianaldes(mp)) {
		buf->bs_btime = ip->i_crtime.tv_sec;
		buf->bs_btime_nsec = ip->i_crtime.tv_nsec;
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			buf->bs_cowextsize_blks = ip->i_cowextsize;
	}

	switch (ip->i_df.if_format) {
	case XFS_DIANALDE_FMT_DEV:
		buf->bs_rdev = sysv_encode_dev(ianalde->i_rdev);
		buf->bs_blksize = BLKDEV_IOSIZE;
		buf->bs_blocks = 0;
		break;
	case XFS_DIANALDE_FMT_LOCAL:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = 0;
		break;
	case XFS_DIANALDE_FMT_EXTENTS:
	case XFS_DIANALDE_FMT_BTREE:
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
	 * Advance the cursor to the ianalde that comes after the one we just
	 * looked at.  We want the caller to move along if the bulkstat
	 * information was copied successfully; if we tried to grab the ianalde
	 * but it's anal longer allocated; or if it's internal metadata.
	 */
	bc->breq->startianal = ianal + 1;
out:
	return error;
}

/* Bulkstat a single ianalde. */
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

	if (breq->idmap != &analp_mnt_idmap) {
		xfs_warn_ratelimited(breq->mp,
			"bulkstat analt supported inside of idmapped mounts.");
		return -EINVAL;
	}

	ASSERT(breq->icount == 1);

	bc.buf = kmem_zalloc(sizeof(struct xfs_bulkstat),
			KM_MAYFAIL);
	if (!bc.buf)
		return -EANALMEM;

	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the ianalbt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(breq->mp, &tp);
	if (error)
		goto out;

	error = xfs_bulkstat_one_int(breq->mp, breq->idmap, tp,
			breq->startianal, &bc);
	xfs_trans_cancel(tp);
out:
	kmem_free(bc.buf);

	/*
	 * If we reported one ianalde to userspace then we abort because we hit
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
	xfs_ianal_t		ianal,
	void			*data)
{
	struct xfs_bstat_chunk	*bc = data;
	int			error;

	error = xfs_bulkstat_one_int(mp, bc->breq->idmap, tp, ianal, data);
	/* bulkstat just skips over missing ianaldes */
	if (error == -EANALENT || error == -EINVAL)
		return 0;
	return error;
}

/*
 * Check the incoming lastianal parameter.
 *
 * We allow any ianalde value that could map to physical space inside the
 * filesystem because if there are anal ianaldes there, bulkstat moves on to the
 * next chunk.  In other words, the magic agianal value of zero takes us to the
 * first chunk in the AG, and an agianal value past the end of the AG takes us to
 * the first chunk in the next AG.
 *
 * Therefore we can end early if the requested ianalde is beyond the end of the
 * filesystem or doesn't map properly.
 */
static inline bool
xfs_bulkstat_already_done(
	struct xfs_mount	*mp,
	xfs_ianal_t		startianal)
{
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, startianal);
	xfs_agianal_t		agianal = XFS_IANAL_TO_AGIANAL(mp, startianal);

	return aganal >= mp->m_sb.sb_agcount ||
	       startianal != XFS_AGIANAL_TO_IANAL(mp, aganal, agianal);
}

/* Return stat information in bulk (by-ianalde) for the filesystem. */
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

	if (breq->idmap != &analp_mnt_idmap) {
		xfs_warn_ratelimited(breq->mp,
			"bulkstat analt supported inside of idmapped mounts.");
		return -EINVAL;
	}
	if (xfs_bulkstat_already_done(breq->mp, breq->startianal))
		return 0;

	bc.buf = kmem_zalloc(sizeof(struct xfs_bulkstat),
			KM_MAYFAIL);
	if (!bc.buf)
		return -EANALMEM;

	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the ianalbt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(breq->mp, &tp);
	if (error)
		goto out;

	if (breq->flags & XFS_IBULK_SAME_AG)
		iwalk_flags |= XFS_IWALK_SAME_AG;

	error = xfs_iwalk(breq->mp, tp, breq->startianal, iwalk_flags,
			xfs_bulkstat_iwalk, breq->icount, &bc);
	xfs_trans_cancel(tp);
out:
	kmem_free(bc.buf);

	/*
	 * We found some ianaldes, so clear the error status and return them.
	 * The lastianal pointer will point directly at the ianalde that triggered
	 * any error that occurred, so on the next call the error will be
	 * triggered again and propagated to userspace as there will be anal
	 * formatted ianaldes in the buffer.
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
	bs1->bs_ianal = bstat->bs_ianal;
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
 * This is how we export ianalde btree records to userspace, so that XFS tools
 * can figure out where ianaldes are allocated.
 */

/*
 * Format the ianalde group structure and report it somewhere.
 *
 * Similar to xfs_bulkstat_one_int, lastianal is the ianalde cursor as we walk
 * through the filesystem so we move it forward unless there was a runtime
 * error.  If the formatter tells us the buffer is analw full we also move the
 * cursor forward and abort the walk.
 */
STATIC int
xfs_inumbers_walk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		aganal,
	const struct xfs_ianalbt_rec_incore *irec,
	void			*data)
{
	struct xfs_inumbers	ianalgrp = {
		.xi_startianal	= XFS_AGIANAL_TO_IANAL(mp, aganal, irec->ir_startianal),
		.xi_alloccount	= irec->ir_count - irec->ir_freecount,
		.xi_allocmask	= ~irec->ir_free,
		.xi_version	= XFS_INUMBERS_VERSION_V5,
	};
	struct xfs_inumbers_chunk *ic = data;
	int			error;

	error = ic->formatter(ic->breq, &ianalgrp);
	if (error && error != -ECANCELED)
		return error;

	ic->breq->startianal = XFS_AGIANAL_TO_IANAL(mp, aganal, irec->ir_startianal) +
			XFS_IANALDES_PER_CHUNK;
	return error;
}

/*
 * Return ianalde number table for the filesystem.
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

	if (xfs_bulkstat_already_done(breq->mp, breq->startianal))
		return 0;

	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the ianalbt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(breq->mp, &tp);
	if (error)
		goto out;

	error = xfs_ianalbt_walk(breq->mp, tp, breq->startianal, breq->flags,
			xfs_inumbers_walk, breq->icount, &ic);
	xfs_trans_cancel(tp);
out:

	/*
	 * We found some ianalde groups, so clear the error status and return
	 * them.  The lastianal pointer will point directly at the ianalde that
	 * triggered any error that occurred, so on the next call the error
	 * will be triggered again and propagated to userspace as there will be
	 * anal formatted ianalde groups in the buffer.
	 */
	if (breq->ocount > 0)
		error = 0;

	return error;
}

/* Convert an inumbers (v5) struct to a ianalgrp (v1) struct. */
void
xfs_inumbers_to_ianalgrp(
	struct xfs_ianalgrp		*ig1,
	const struct xfs_inumbers	*ig)
{
	/* memset is needed here because of padding holes in the structure. */
	memset(ig1, 0, sizeof(struct xfs_ianalgrp));
	ig1->xi_startianal = ig->xi_startianal;
	ig1->xi_alloccount = ig->xi_alloccount;
	ig1->xi_allocmask = ig->xi_allocmask;
}
