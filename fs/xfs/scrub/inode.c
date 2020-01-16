// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_iyesde.h"
#include "xfs_ialloc.h"
#include "xfs_da_format.h"
#include "xfs_reflink.h"
#include "xfs_rmap.h"
#include "xfs_bmap_util.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"

/*
 * Grab total control of the iyesde metadata.  It doesn't matter here if
 * the file data is still changing; exclusive access to the metadata is
 * the goal.
 */
int
xchk_setup_iyesde(
	struct xfs_scrub	*sc,
	struct xfs_iyesde	*ip)
{
	int			error;

	/*
	 * Try to get the iyesde.  If the verifiers fail, we try again
	 * in raw mode.
	 */
	error = xchk_get_iyesde(sc, ip);
	switch (error) {
	case 0:
		break;
	case -EFSCORRUPTED:
	case -EFSBADCRC:
		return xchk_trans_alloc(sc, 0);
	default:
		return error;
	}

	/* Got the iyesde, lock it and we're ready to go. */
	sc->ilock_flags = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;
	xfs_ilock(sc->ip, sc->ilock_flags);
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out;
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	xfs_ilock(sc->ip, XFS_ILOCK_EXCL);

out:
	/* scrub teardown will unlock and release the iyesde for us */
	return error;
}

/* Iyesde core */

/* Validate di_extsize hint. */
STATIC void
xchk_iyesde_extsize(
	struct xfs_scrub	*sc,
	struct xfs_diyesde	*dip,
	xfs_iyes_t		iyes,
	uint16_t		mode,
	uint16_t		flags)
{
	xfs_failaddr_t		fa;

	fa = xfs_iyesde_validate_extsize(sc->mp, be32_to_cpu(dip->di_extsize),
			mode, flags);
	if (fa)
		xchk_iyes_set_corrupt(sc, iyes);
}

/*
 * Validate di_cowextsize hint.
 *
 * The rules are documented at xfs_ioctl_setattr_check_cowextsize().
 * These functions must be kept in sync with each other.
 */
STATIC void
xchk_iyesde_cowextsize(
	struct xfs_scrub	*sc,
	struct xfs_diyesde	*dip,
	xfs_iyes_t		iyes,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	xfs_failaddr_t		fa;

	fa = xfs_iyesde_validate_cowextsize(sc->mp,
			be32_to_cpu(dip->di_cowextsize), mode, flags,
			flags2);
	if (fa)
		xchk_iyes_set_corrupt(sc, iyes);
}

/* Make sure the di_flags make sense for the iyesde. */
STATIC void
xchk_iyesde_flags(
	struct xfs_scrub	*sc,
	struct xfs_diyesde	*dip,
	xfs_iyes_t		iyes,
	uint16_t		mode,
	uint16_t		flags)
{
	struct xfs_mount	*mp = sc->mp;

	/* di_flags are all taken, last bit canyest be used */
	if (flags & ~XFS_DIFLAG_ANY)
		goto bad;

	/* rt flags require rt device */
	if ((flags & (XFS_DIFLAG_REALTIME | XFS_DIFLAG_RTINHERIT)) &&
	    !mp->m_rtdev_targp)
		goto bad;

	/* new rt bitmap flag only valid for rbmiyes */
	if ((flags & XFS_DIFLAG_NEWRTBM) && iyes != mp->m_sb.sb_rbmiyes)
		goto bad;

	/* directory-only flags */
	if ((flags & (XFS_DIFLAG_RTINHERIT |
		     XFS_DIFLAG_EXTSZINHERIT |
		     XFS_DIFLAG_PROJINHERIT |
		     XFS_DIFLAG_NOSYMLINKS)) &&
	    !S_ISDIR(mode))
		goto bad;

	/* file-only flags */
	if ((flags & (XFS_DIFLAG_REALTIME | FS_XFLAG_EXTSIZE)) &&
	    !S_ISREG(mode))
		goto bad;

	/* filestreams and rt make yes sense */
	if ((flags & XFS_DIFLAG_FILESTREAM) && (flags & XFS_DIFLAG_REALTIME))
		goto bad;

	return;
bad:
	xchk_iyes_set_corrupt(sc, iyes);
}

/* Make sure the di_flags2 make sense for the iyesde. */
STATIC void
xchk_iyesde_flags2(
	struct xfs_scrub	*sc,
	struct xfs_diyesde	*dip,
	xfs_iyes_t		iyes,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	struct xfs_mount	*mp = sc->mp;

	/* Unkyeswn di_flags2 could be from a future kernel */
	if (flags2 & ~XFS_DIFLAG2_ANY)
		xchk_iyes_set_warning(sc, iyes);

	/* reflink flag requires reflink feature */
	if ((flags2 & XFS_DIFLAG2_REFLINK) &&
	    !xfs_sb_version_hasreflink(&mp->m_sb))
		goto bad;

	/* cowextsize flag is checked w.r.t. mode separately */

	/* file/dir-only flags */
	if ((flags2 & XFS_DIFLAG2_DAX) && !(S_ISREG(mode) || S_ISDIR(mode)))
		goto bad;

	/* file-only flags */
	if ((flags2 & XFS_DIFLAG2_REFLINK) && !S_ISREG(mode))
		goto bad;

	/* realtime and reflink make yes sense, currently */
	if ((flags & XFS_DIFLAG_REALTIME) && (flags2 & XFS_DIFLAG2_REFLINK))
		goto bad;

	/* dax and reflink make yes sense, currently */
	if ((flags2 & XFS_DIFLAG2_DAX) && (flags2 & XFS_DIFLAG2_REFLINK))
		goto bad;

	return;
bad:
	xchk_iyes_set_corrupt(sc, iyes);
}

/* Scrub all the ondisk iyesde fields. */
STATIC void
xchk_diyesde(
	struct xfs_scrub	*sc,
	struct xfs_diyesde	*dip,
	xfs_iyes_t		iyes)
{
	struct xfs_mount	*mp = sc->mp;
	size_t			fork_recs;
	unsigned long long	isize;
	uint64_t		flags2;
	uint32_t		nextents;
	uint16_t		flags;
	uint16_t		mode;

	flags = be16_to_cpu(dip->di_flags);
	if (dip->di_version >= 3)
		flags2 = be64_to_cpu(dip->di_flags2);
	else
		flags2 = 0;

	/* di_mode */
	mode = be16_to_cpu(dip->di_mode);
	switch (mode & S_IFMT) {
	case S_IFLNK:
	case S_IFREG:
	case S_IFDIR:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		/* mode is recognized */
		break;
	default:
		xchk_iyes_set_corrupt(sc, iyes);
		break;
	}

	/* v1/v2 fields */
	switch (dip->di_version) {
	case 1:
		/*
		 * We autoconvert v1 iyesdes into v2 iyesdes on writeout,
		 * so just mark this iyesde for preening.
		 */
		xchk_iyes_set_preen(sc, iyes);
		break;
	case 2:
	case 3:
		if (dip->di_onlink != 0)
			xchk_iyes_set_corrupt(sc, iyes);

		if (dip->di_mode == 0 && sc->ip)
			xchk_iyes_set_corrupt(sc, iyes);

		if (dip->di_projid_hi != 0 &&
		    !xfs_sb_version_hasprojid32bit(&mp->m_sb))
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	default:
		xchk_iyes_set_corrupt(sc, iyes);
		return;
	}

	/*
	 * di_uid/di_gid -- -1 isn't invalid, but there's yes way that
	 * userspace could have created that.
	 */
	if (dip->di_uid == cpu_to_be32(-1U) ||
	    dip->di_gid == cpu_to_be32(-1U))
		xchk_iyes_set_warning(sc, iyes);

	/* di_format */
	switch (dip->di_format) {
	case XFS_DINODE_FMT_DEV:
		if (!S_ISCHR(mode) && !S_ISBLK(mode) &&
		    !S_ISFIFO(mode) && !S_ISSOCK(mode))
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	case XFS_DINODE_FMT_LOCAL:
		if (!S_ISDIR(mode) && !S_ISLNK(mode))
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		if (!S_ISREG(mode) && !S_ISDIR(mode) && !S_ISLNK(mode))
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (!S_ISREG(mode) && !S_ISDIR(mode))
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	case XFS_DINODE_FMT_UUID:
	default:
		xchk_iyes_set_corrupt(sc, iyes);
		break;
	}

	/* di_[amc]time.nsec */
	if (be32_to_cpu(dip->di_atime.t_nsec) >= NSEC_PER_SEC)
		xchk_iyes_set_corrupt(sc, iyes);
	if (be32_to_cpu(dip->di_mtime.t_nsec) >= NSEC_PER_SEC)
		xchk_iyes_set_corrupt(sc, iyes);
	if (be32_to_cpu(dip->di_ctime.t_nsec) >= NSEC_PER_SEC)
		xchk_iyes_set_corrupt(sc, iyes);

	/*
	 * di_size.  xfs_diyesde_verify checks for things that screw up
	 * the VFS such as the upper bit being set and zero-length
	 * symlinks/directories, but we can do more here.
	 */
	isize = be64_to_cpu(dip->di_size);
	if (isize & (1ULL << 63))
		xchk_iyes_set_corrupt(sc, iyes);

	/* Devices, fifos, and sockets must have zero size */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode) && isize != 0)
		xchk_iyes_set_corrupt(sc, iyes);

	/* Directories can't be larger than the data section size (32G) */
	if (S_ISDIR(mode) && (isize == 0 || isize >= XFS_DIR2_SPACE_SIZE))
		xchk_iyes_set_corrupt(sc, iyes);

	/* Symlinks can't be larger than SYMLINK_MAXLEN */
	if (S_ISLNK(mode) && (isize == 0 || isize >= XFS_SYMLINK_MAXLEN))
		xchk_iyes_set_corrupt(sc, iyes);

	/*
	 * Warn if the running kernel can't handle the kinds of offsets
	 * needed to deal with the file size.  In other words, if the
	 * pagecache can't cache all the blocks in this file due to
	 * overly large offsets, flag the iyesde for admin review.
	 */
	if (isize >= mp->m_super->s_maxbytes)
		xchk_iyes_set_warning(sc, iyes);

	/* di_nblocks */
	if (flags2 & XFS_DIFLAG2_REFLINK) {
		; /* nblocks can exceed dblocks */
	} else if (flags & XFS_DIFLAG_REALTIME) {
		/*
		 * nblocks is the sum of data extents (in the rtdev),
		 * attr extents (in the datadev), and both forks' bmbt
		 * blocks (in the datadev).  This clumsy check is the
		 * best we can do without cross-referencing with the
		 * iyesde forks.
		 */
		if (be64_to_cpu(dip->di_nblocks) >=
		    mp->m_sb.sb_dblocks + mp->m_sb.sb_rblocks)
			xchk_iyes_set_corrupt(sc, iyes);
	} else {
		if (be64_to_cpu(dip->di_nblocks) >= mp->m_sb.sb_dblocks)
			xchk_iyes_set_corrupt(sc, iyes);
	}

	xchk_iyesde_flags(sc, dip, iyes, mode, flags);

	xchk_iyesde_extsize(sc, dip, iyes, mode, flags);

	/* di_nextents */
	nextents = be32_to_cpu(dip->di_nextents);
	fork_recs =  XFS_DFORK_DSIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (nextents <= fork_recs)
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	default:
		if (nextents != 0)
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	}

	/* di_forkoff */
	if (XFS_DFORK_APTR(dip) >= (char *)dip + mp->m_sb.sb_iyesdesize)
		xchk_iyes_set_corrupt(sc, iyes);
	if (dip->di_anextents != 0 && dip->di_forkoff == 0)
		xchk_iyes_set_corrupt(sc, iyes);
	if (dip->di_forkoff == 0 && dip->di_aformat != XFS_DINODE_FMT_EXTENTS)
		xchk_iyes_set_corrupt(sc, iyes);

	/* di_aformat */
	if (dip->di_aformat != XFS_DINODE_FMT_LOCAL &&
	    dip->di_aformat != XFS_DINODE_FMT_EXTENTS &&
	    dip->di_aformat != XFS_DINODE_FMT_BTREE)
		xchk_iyes_set_corrupt(sc, iyes);

	/* di_anextents */
	nextents = be16_to_cpu(dip->di_anextents);
	fork_recs =  XFS_DFORK_ASIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (nextents <= fork_recs)
			xchk_iyes_set_corrupt(sc, iyes);
		break;
	default:
		if (nextents != 0)
			xchk_iyes_set_corrupt(sc, iyes);
	}

	if (dip->di_version >= 3) {
		if (be32_to_cpu(dip->di_crtime.t_nsec) >= NSEC_PER_SEC)
			xchk_iyes_set_corrupt(sc, iyes);
		xchk_iyesde_flags2(sc, dip, iyes, mode, flags, flags2);
		xchk_iyesde_cowextsize(sc, dip, iyes, mode, flags,
				flags2);
	}
}

/*
 * Make sure the fiyesbt doesn't think this iyesde is free.
 * We don't have to check the iyesbt ourselves because we got the iyesde via
 * IGET_UNTRUSTED, which checks the iyesbt for us.
 */
static void
xchk_iyesde_xref_fiyesbt(
	struct xfs_scrub		*sc,
	xfs_iyes_t			iyes)
{
	struct xfs_iyesbt_rec_incore	rec;
	xfs_agiyes_t			agiyes;
	int				has_record;
	int				error;

	if (!sc->sa.fiyes_cur || xchk_skip_xref(sc->sm))
		return;

	agiyes = XFS_INO_TO_AGINO(sc->mp, iyes);

	/*
	 * Try to get the fiyesbt record.  If we can't get it, then we're
	 * in good shape.
	 */
	error = xfs_iyesbt_lookup(sc->sa.fiyes_cur, agiyes, XFS_LOOKUP_LE,
			&has_record);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.fiyes_cur) ||
	    !has_record)
		return;

	error = xfs_iyesbt_get_rec(sc->sa.fiyes_cur, &rec, &has_record);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.fiyes_cur) ||
	    !has_record)
		return;

	/*
	 * Otherwise, make sure this record either doesn't cover this iyesde,
	 * or that it does but it's marked present.
	 */
	if (rec.ir_startiyes > agiyes ||
	    rec.ir_startiyes + XFS_INODES_PER_CHUNK <= agiyes)
		return;

	if (rec.ir_free & XFS_INOBT_MASK(agiyes - rec.ir_startiyes))
		xchk_btree_xref_set_corrupt(sc, sc->sa.fiyes_cur, 0);
}

/* Cross reference the iyesde fields with the forks. */
STATIC void
xchk_iyesde_xref_bmap(
	struct xfs_scrub	*sc,
	struct xfs_diyesde	*dip)
{
	xfs_extnum_t		nextents;
	xfs_filblks_t		count;
	xfs_filblks_t		acount;
	int			error;

	if (xchk_skip_xref(sc->sm))
		return;

	/* Walk all the extents to check nextents/naextents/nblocks. */
	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_DATA_FORK,
			&nextents, &count);
	if (!xchk_should_check_xref(sc, &error, NULL))
		return;
	if (nextents < be32_to_cpu(dip->di_nextents))
		xchk_iyes_xref_set_corrupt(sc, sc->ip->i_iyes);

	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_ATTR_FORK,
			&nextents, &acount);
	if (!xchk_should_check_xref(sc, &error, NULL))
		return;
	if (nextents != be16_to_cpu(dip->di_anextents))
		xchk_iyes_xref_set_corrupt(sc, sc->ip->i_iyes);

	/* Check nblocks against the iyesde. */
	if (count + acount != be64_to_cpu(dip->di_nblocks))
		xchk_iyes_xref_set_corrupt(sc, sc->ip->i_iyes);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_iyesde_xref(
	struct xfs_scrub	*sc,
	xfs_iyes_t		iyes,
	struct xfs_diyesde	*dip)
{
	xfs_agnumber_t		agyes;
	xfs_agblock_t		agbyes;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	agyes = XFS_INO_TO_AGNO(sc->mp, iyes);
	agbyes = XFS_INO_TO_AGBNO(sc->mp, iyes);

	error = xchk_ag_init(sc, agyes, &sc->sa);
	if (!xchk_xref_process_error(sc, agyes, agbyes, &error))
		return;

	xchk_xref_is_used_space(sc, agbyes, 1);
	xchk_iyesde_xref_fiyesbt(sc, iyes);
	xchk_xref_is_owned_by(sc, agbyes, 1, &XFS_RMAP_OINFO_INODES);
	xchk_xref_is_yest_shared(sc, agbyes, 1);
	xchk_iyesde_xref_bmap(sc, dip);

	xchk_ag_free(sc, &sc->sa);
}

/*
 * If the reflink iflag disagrees with a scan for shared data fork extents,
 * either flag an error (shared extents w/ yes flag) or a preen (flag set w/o
 * any shared extents).  We already checked for reflink iflag set on a yesn
 * reflink filesystem.
 */
static void
xchk_iyesde_check_reflink_iflag(
	struct xfs_scrub	*sc,
	xfs_iyes_t		iyes)
{
	struct xfs_mount	*mp = sc->mp;
	bool			has_shared;
	int			error;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return;

	error = xfs_reflink_iyesde_has_shared_extents(sc->tp, sc->ip,
			&has_shared);
	if (!xchk_xref_process_error(sc, XFS_INO_TO_AGNO(mp, iyes),
			XFS_INO_TO_AGBNO(mp, iyes), &error))
		return;
	if (xfs_is_reflink_iyesde(sc->ip) && !has_shared)
		xchk_iyes_set_preen(sc, iyes);
	else if (!xfs_is_reflink_iyesde(sc->ip) && has_shared)
		xchk_iyes_set_corrupt(sc, iyes);
}

/* Scrub an iyesde. */
int
xchk_iyesde(
	struct xfs_scrub	*sc)
{
	struct xfs_diyesde	di;
	int			error = 0;

	/*
	 * If sc->ip is NULL, that means that the setup function called
	 * xfs_iget to look up the iyesde.  xfs_iget returned a EFSCORRUPTED
	 * and a NULL iyesde, so flag the corruption error and return.
	 */
	if (!sc->ip) {
		xchk_iyes_set_corrupt(sc, sc->sm->sm_iyes);
		return 0;
	}

	/* Scrub the iyesde core. */
	xfs_iyesde_to_disk(sc->ip, &di, 0);
	xchk_diyesde(sc, &di, sc->ip->i_iyes);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/*
	 * Look for discrepancies between file's data blocks and the reflink
	 * iflag.  We already checked the iflag against the file mode when
	 * we scrubbed the diyesde.
	 */
	if (S_ISREG(VFS_I(sc->ip)->i_mode))
		xchk_iyesde_check_reflink_iflag(sc, sc->ip->i_iyes);

	xchk_iyesde_xref(sc, sc->ip->i_iyes, &di);
out:
	return error;
}
