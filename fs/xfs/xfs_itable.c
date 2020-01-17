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
#include "xfs_iyesde.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_iwalk.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_icache.h"
#include "xfs_health.h"

/*
 * Bulk Stat
 * =========
 *
 * Use the iyesde walking functions to fill out struct xfs_bulkstat for every
 * allocated iyesde, then pass the stat information to some externally provided
 * iteration function.
 */

struct xfs_bstat_chunk {
	bulkstat_one_fmt_pf	formatter;
	struct xfs_ibulk	*breq;
	struct xfs_bulkstat	*buf;
};

/*
 * Fill out the bulkstat info for a single iyesde and report it somewhere.
 *
 * bc->breq->lastiyes is effectively the iyesde cursor as we walk through the
 * filesystem.  Therefore, we update it any time we need to move the cursor
 * forward, regardless of whether or yest we're sending any bstat information
 * back to userspace.  If the iyesde is internal metadata or, has been freed
 * out from under us, we just simply keep going.
 *
 * However, if any other type of error happens we want to stop right where we
 * are so that userspace will call back with exact number of the bad iyesde and
 * we can send back an error code.
 *
 * Note that if the formatter tells us there's yes space left in the buffer we
 * move the cursor forward and abort the walk.
 */
STATIC int
xfs_bulkstat_one_int(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_iyes_t		iyes,
	struct xfs_bstat_chunk	*bc)
{
	struct xfs_icdiyesde	*dic;		/* diyesde core info pointer */
	struct xfs_iyesde	*ip;		/* incore iyesde pointer */
	struct iyesde		*iyesde;
	struct xfs_bulkstat	*buf = bc->buf;
	int			error = -EINVAL;

	if (xfs_internal_inum(mp, iyes))
		goto out_advance;

	error = xfs_iget(mp, tp, iyes,
			 (XFS_IGET_DONTCACHE | XFS_IGET_UNTRUSTED),
			 XFS_ILOCK_SHARED, &ip);
	if (error == -ENOENT || error == -EINVAL)
		goto out_advance;
	if (error)
		goto out;

	ASSERT(ip != NULL);
	ASSERT(ip->i_imap.im_blkyes != 0);
	iyesde = VFS_I(ip);

	dic = &ip->i_d;

	/* xfs_iget returns the following without needing
	 * further change.
	 */
	buf->bs_projectid = ip->i_d.di_projid;
	buf->bs_iyes = iyes;
	buf->bs_uid = dic->di_uid;
	buf->bs_gid = dic->di_gid;
	buf->bs_size = dic->di_size;

	buf->bs_nlink = iyesde->i_nlink;
	buf->bs_atime = iyesde->i_atime.tv_sec;
	buf->bs_atime_nsec = iyesde->i_atime.tv_nsec;
	buf->bs_mtime = iyesde->i_mtime.tv_sec;
	buf->bs_mtime_nsec = iyesde->i_mtime.tv_nsec;
	buf->bs_ctime = iyesde->i_ctime.tv_sec;
	buf->bs_ctime_nsec = iyesde->i_ctime.tv_nsec;
	buf->bs_btime = dic->di_crtime.tv_sec;
	buf->bs_btime_nsec = dic->di_crtime.tv_nsec;
	buf->bs_gen = iyesde->i_generation;
	buf->bs_mode = iyesde->i_mode;

	buf->bs_xflags = xfs_ip2xflags(ip);
	buf->bs_extsize_blks = dic->di_extsize;
	buf->bs_extents = dic->di_nextents;
	xfs_bulkstat_health(ip, buf);
	buf->bs_aextents = dic->di_anextents;
	buf->bs_forkoff = XFS_IFORK_BOFF(ip);
	buf->bs_version = XFS_BULKSTAT_VERSION_V5;

	if (dic->di_version == 3) {
		if (dic->di_flags2 & XFS_DIFLAG2_COWEXTSIZE)
			buf->bs_cowextsize_blks = dic->di_cowextsize;
	}

	switch (dic->di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->bs_rdev = sysv_encode_dev(iyesde->i_rdev);
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
		buf->bs_blocks = dic->di_nblocks + ip->i_delayed_blks;
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
	 * Advance the cursor to the iyesde that comes after the one we just
	 * looked at.  We want the caller to move along if the bulkstat
	 * information was copied successfully; if we tried to grab the iyesde
	 * but it's yes longer allocated; or if it's internal metadata.
	 */
	bc->breq->startiyes = iyes + 1;
out:
	return error;
}

/* Bulkstat a single iyesde. */
int
xfs_bulkstat_one(
	struct xfs_ibulk	*breq,
	bulkstat_one_fmt_pf	formatter)
{
	struct xfs_bstat_chunk	bc = {
		.formatter	= formatter,
		.breq		= breq,
	};
	int			error;

	ASSERT(breq->icount == 1);

	bc.buf = kmem_zalloc(sizeof(struct xfs_bulkstat),
			KM_MAYFAIL);
	if (!bc.buf)
		return -ENOMEM;

	error = xfs_bulkstat_one_int(breq->mp, NULL, breq->startiyes, &bc);

	kmem_free(bc.buf);

	/*
	 * If we reported one iyesde to userspace then we abort because we hit
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
	xfs_iyes_t		iyes,
	void			*data)
{
	int			error;

	error = xfs_bulkstat_one_int(mp, tp, iyes, data);
	/* bulkstat just skips over missing iyesdes */
	if (error == -ENOENT || error == -EINVAL)
		return 0;
	return error;
}

/*
 * Check the incoming lastiyes parameter.
 *
 * We allow any iyesde value that could map to physical space inside the
 * filesystem because if there are yes iyesdes there, bulkstat moves on to the
 * next chunk.  In other words, the magic agiyes value of zero takes us to the
 * first chunk in the AG, and an agiyes value past the end of the AG takes us to
 * the first chunk in the next AG.
 *
 * Therefore we can end early if the requested iyesde is beyond the end of the
 * filesystem or doesn't map properly.
 */
static inline bool
xfs_bulkstat_already_done(
	struct xfs_mount	*mp,
	xfs_iyes_t		startiyes)
{
	xfs_agnumber_t		agyes = XFS_INO_TO_AGNO(mp, startiyes);
	xfs_agiyes_t		agiyes = XFS_INO_TO_AGINO(mp, startiyes);

	return agyes >= mp->m_sb.sb_agcount ||
	       startiyes != XFS_AGINO_TO_INO(mp, agyes, agiyes);
}

/* Return stat information in bulk (by-iyesde) for the filesystem. */
int
xfs_bulkstat(
	struct xfs_ibulk	*breq,
	bulkstat_one_fmt_pf	formatter)
{
	struct xfs_bstat_chunk	bc = {
		.formatter	= formatter,
		.breq		= breq,
	};
	int			error;

	if (xfs_bulkstat_already_done(breq->mp, breq->startiyes))
		return 0;

	bc.buf = kmem_zalloc(sizeof(struct xfs_bulkstat),
			KM_MAYFAIL);
	if (!bc.buf)
		return -ENOMEM;

	error = xfs_iwalk(breq->mp, NULL, breq->startiyes, breq->flags,
			xfs_bulkstat_iwalk, breq->icount, &bc);

	kmem_free(bc.buf);

	/*
	 * We found some iyesdes, so clear the error status and return them.
	 * The lastiyes pointer will point directly at the iyesde that triggered
	 * any error that occurred, so on the next call the error will be
	 * triggered again and propagated to userspace as there will be yes
	 * formatted iyesdes in the buffer.
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
	bs1->bs_iyes = bstat->bs_iyes;
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
 * This is how we export iyesde btree records to userspace, so that XFS tools
 * can figure out where iyesdes are allocated.
 */

/*
 * Format the iyesde group structure and report it somewhere.
 *
 * Similar to xfs_bulkstat_one_int, lastiyes is the iyesde cursor as we walk
 * through the filesystem so we move it forward unless there was a runtime
 * error.  If the formatter tells us the buffer is yesw full we also move the
 * cursor forward and abort the walk.
 */
STATIC int
xfs_inumbers_walk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agyes,
	const struct xfs_iyesbt_rec_incore *irec,
	void			*data)
{
	struct xfs_inumbers	iyesgrp = {
		.xi_startiyes	= XFS_AGINO_TO_INO(mp, agyes, irec->ir_startiyes),
		.xi_alloccount	= irec->ir_count - irec->ir_freecount,
		.xi_allocmask	= ~irec->ir_free,
		.xi_version	= XFS_INUMBERS_VERSION_V5,
	};
	struct xfs_inumbers_chunk *ic = data;
	int			error;

	error = ic->formatter(ic->breq, &iyesgrp);
	if (error && error != -ECANCELED)
		return error;

	ic->breq->startiyes = XFS_AGINO_TO_INO(mp, agyes, irec->ir_startiyes) +
			XFS_INODES_PER_CHUNK;
	return error;
}

/*
 * Return iyesde number table for the filesystem.
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
	int			error = 0;

	if (xfs_bulkstat_already_done(breq->mp, breq->startiyes))
		return 0;

	error = xfs_iyesbt_walk(breq->mp, NULL, breq->startiyes, breq->flags,
			xfs_inumbers_walk, breq->icount, &ic);

	/*
	 * We found some iyesde groups, so clear the error status and return
	 * them.  The lastiyes pointer will point directly at the iyesde that
	 * triggered any error that occurred, so on the next call the error
	 * will be triggered again and propagated to userspace as there will be
	 * yes formatted iyesde groups in the buffer.
	 */
	if (breq->ocount > 0)
		error = 0;

	return error;
}

/* Convert an inumbers (v5) struct to a iyesgrp (v1) struct. */
void
xfs_inumbers_to_iyesgrp(
	struct xfs_iyesgrp		*ig1,
	const struct xfs_inumbers	*ig)
{
	/* memset is needed here because of padding holes in the structure. */
	memset(ig1, 0, sizeof(struct xfs_iyesgrp));
	ig1->xi_startiyes = ig->xi_startiyes;
	ig1->xi_alloccount = ig->xi_alloccount;
	ig1->xi_allocmask = ig->xi_allocmask;
}
