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
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_inode_buf.h"
#include "xfs_inode_fork.h"
#include "xfs_ialloc.h"
#include "xfs_da_format.h"
#include "xfs_reflink.h"
#include "xfs_rmap.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/*
 * Grab total control of the inode metadata.  It doesn't matter here if
 * the file data is still changing; exclusive access to the metadata is
 * the goal.
 */
int
xchk_setup_inode(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	int			error;

	/*
	 * Try to get the inode.  If the verifiers fail, we try again
	 * in raw mode.
	 */
	error = xchk_get_inode(sc, ip);
	switch (error) {
	case 0:
		break;
	case -EFSCORRUPTED:
	case -EFSBADCRC:
		return xchk_trans_alloc(sc, 0);
	default:
		return error;
	}

	/* Got the inode, lock it and we're ready to go. */
	sc->ilock_flags = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;
	xfs_ilock(sc->ip, sc->ilock_flags);
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out;
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	xfs_ilock(sc->ip, XFS_ILOCK_EXCL);

out:
	/* scrub teardown will unlock and release the inode for us */
	return error;
}

/* Inode core */

/* Validate di_extsize hint. */
STATIC void
xchk_inode_extsize(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	xfs_ino_t		ino,
	uint16_t		mode,
	uint16_t		flags)
{
	xfs_failaddr_t		fa;

	fa = xfs_inode_validate_extsize(sc->mp, be32_to_cpu(dip->di_extsize),
			mode, flags);
	if (fa)
		xchk_ino_set_corrupt(sc, ino);
}

/*
 * Validate di_cowextsize hint.
 *
 * The rules are documented at xfs_ioctl_setattr_check_cowextsize().
 * These functions must be kept in sync with each other.
 */
STATIC void
xchk_inode_cowextsize(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	xfs_ino_t		ino,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	xfs_failaddr_t		fa;

	fa = xfs_inode_validate_cowextsize(sc->mp,
			be32_to_cpu(dip->di_cowextsize), mode, flags,
			flags2);
	if (fa)
		xchk_ino_set_corrupt(sc, ino);
}

/* Make sure the di_flags make sense for the inode. */
STATIC void
xchk_inode_flags(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	xfs_ino_t		ino,
	uint16_t		mode,
	uint16_t		flags)
{
	struct xfs_mount	*mp = sc->mp;

	/* di_flags are all taken, last bit cannot be used */
	if (flags & ~XFS_DIFLAG_ANY)
		goto bad;

	/* rt flags require rt device */
	if ((flags & (XFS_DIFLAG_REALTIME | XFS_DIFLAG_RTINHERIT)) &&
	    !mp->m_rtdev_targp)
		goto bad;

	/* new rt bitmap flag only valid for rbmino */
	if ((flags & XFS_DIFLAG_NEWRTBM) && ino != mp->m_sb.sb_rbmino)
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

	/* filestreams and rt make no sense */
	if ((flags & XFS_DIFLAG_FILESTREAM) && (flags & XFS_DIFLAG_REALTIME))
		goto bad;

	return;
bad:
	xchk_ino_set_corrupt(sc, ino);
}

/* Make sure the di_flags2 make sense for the inode. */
STATIC void
xchk_inode_flags2(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	xfs_ino_t		ino,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	struct xfs_mount	*mp = sc->mp;

	/* Unknown di_flags2 could be from a future kernel */
	if (flags2 & ~XFS_DIFLAG2_ANY)
		xchk_ino_set_warning(sc, ino);

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

	/* realtime and reflink make no sense, currently */
	if ((flags & XFS_DIFLAG_REALTIME) && (flags2 & XFS_DIFLAG2_REFLINK))
		goto bad;

	/* dax and reflink make no sense, currently */
	if ((flags2 & XFS_DIFLAG2_DAX) && (flags2 & XFS_DIFLAG2_REFLINK))
		goto bad;

	return;
bad:
	xchk_ino_set_corrupt(sc, ino);
}

/* Scrub all the ondisk inode fields. */
STATIC void
xchk_dinode(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	xfs_ino_t		ino)
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
		xchk_ino_set_corrupt(sc, ino);
		break;
	}

	/* v1/v2 fields */
	switch (dip->di_version) {
	case 1:
		/*
		 * We autoconvert v1 inodes into v2 inodes on writeout,
		 * so just mark this inode for preening.
		 */
		xchk_ino_set_preen(sc, ino);
		break;
	case 2:
	case 3:
		if (dip->di_onlink != 0)
			xchk_ino_set_corrupt(sc, ino);

		if (dip->di_mode == 0 && sc->ip)
			xchk_ino_set_corrupt(sc, ino);

		if (dip->di_projid_hi != 0 &&
		    !xfs_sb_version_hasprojid32bit(&mp->m_sb))
			xchk_ino_set_corrupt(sc, ino);
		break;
	default:
		xchk_ino_set_corrupt(sc, ino);
		return;
	}

	/*
	 * di_uid/di_gid -- -1 isn't invalid, but there's no way that
	 * userspace could have created that.
	 */
	if (dip->di_uid == cpu_to_be32(-1U) ||
	    dip->di_gid == cpu_to_be32(-1U))
		xchk_ino_set_warning(sc, ino);

	/* di_format */
	switch (dip->di_format) {
	case XFS_DINODE_FMT_DEV:
		if (!S_ISCHR(mode) && !S_ISBLK(mode) &&
		    !S_ISFIFO(mode) && !S_ISSOCK(mode))
			xchk_ino_set_corrupt(sc, ino);
		break;
	case XFS_DINODE_FMT_LOCAL:
		if (!S_ISDIR(mode) && !S_ISLNK(mode))
			xchk_ino_set_corrupt(sc, ino);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		if (!S_ISREG(mode) && !S_ISDIR(mode) && !S_ISLNK(mode))
			xchk_ino_set_corrupt(sc, ino);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (!S_ISREG(mode) && !S_ISDIR(mode))
			xchk_ino_set_corrupt(sc, ino);
		break;
	case XFS_DINODE_FMT_UUID:
	default:
		xchk_ino_set_corrupt(sc, ino);
		break;
	}

	/* di_[amc]time.nsec */
	if (be32_to_cpu(dip->di_atime.t_nsec) >= NSEC_PER_SEC)
		xchk_ino_set_corrupt(sc, ino);
	if (be32_to_cpu(dip->di_mtime.t_nsec) >= NSEC_PER_SEC)
		xchk_ino_set_corrupt(sc, ino);
	if (be32_to_cpu(dip->di_ctime.t_nsec) >= NSEC_PER_SEC)
		xchk_ino_set_corrupt(sc, ino);

	/*
	 * di_size.  xfs_dinode_verify checks for things that screw up
	 * the VFS such as the upper bit being set and zero-length
	 * symlinks/directories, but we can do more here.
	 */
	isize = be64_to_cpu(dip->di_size);
	if (isize & (1ULL << 63))
		xchk_ino_set_corrupt(sc, ino);

	/* Devices, fifos, and sockets must have zero size */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode) && isize != 0)
		xchk_ino_set_corrupt(sc, ino);

	/* Directories can't be larger than the data section size (32G) */
	if (S_ISDIR(mode) && (isize == 0 || isize >= XFS_DIR2_SPACE_SIZE))
		xchk_ino_set_corrupt(sc, ino);

	/* Symlinks can't be larger than SYMLINK_MAXLEN */
	if (S_ISLNK(mode) && (isize == 0 || isize >= XFS_SYMLINK_MAXLEN))
		xchk_ino_set_corrupt(sc, ino);

	/*
	 * Warn if the running kernel can't handle the kinds of offsets
	 * needed to deal with the file size.  In other words, if the
	 * pagecache can't cache all the blocks in this file due to
	 * overly large offsets, flag the inode for admin review.
	 */
	if (isize >= mp->m_super->s_maxbytes)
		xchk_ino_set_warning(sc, ino);

	/* di_nblocks */
	if (flags2 & XFS_DIFLAG2_REFLINK) {
		; /* nblocks can exceed dblocks */
	} else if (flags & XFS_DIFLAG_REALTIME) {
		/*
		 * nblocks is the sum of data extents (in the rtdev),
		 * attr extents (in the datadev), and both forks' bmbt
		 * blocks (in the datadev).  This clumsy check is the
		 * best we can do without cross-referencing with the
		 * inode forks.
		 */
		if (be64_to_cpu(dip->di_nblocks) >=
		    mp->m_sb.sb_dblocks + mp->m_sb.sb_rblocks)
			xchk_ino_set_corrupt(sc, ino);
	} else {
		if (be64_to_cpu(dip->di_nblocks) >= mp->m_sb.sb_dblocks)
			xchk_ino_set_corrupt(sc, ino);
	}

	xchk_inode_flags(sc, dip, ino, mode, flags);

	xchk_inode_extsize(sc, dip, ino, mode, flags);

	/* di_nextents */
	nextents = be32_to_cpu(dip->di_nextents);
	fork_recs =  XFS_DFORK_DSIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xchk_ino_set_corrupt(sc, ino);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (nextents <= fork_recs)
			xchk_ino_set_corrupt(sc, ino);
		break;
	default:
		if (nextents != 0)
			xchk_ino_set_corrupt(sc, ino);
		break;
	}

	/* di_forkoff */
	if (XFS_DFORK_APTR(dip) >= (char *)dip + mp->m_sb.sb_inodesize)
		xchk_ino_set_corrupt(sc, ino);
	if (dip->di_anextents != 0 && dip->di_forkoff == 0)
		xchk_ino_set_corrupt(sc, ino);
	if (dip->di_forkoff == 0 && dip->di_aformat != XFS_DINODE_FMT_EXTENTS)
		xchk_ino_set_corrupt(sc, ino);

	/* di_aformat */
	if (dip->di_aformat != XFS_DINODE_FMT_LOCAL &&
	    dip->di_aformat != XFS_DINODE_FMT_EXTENTS &&
	    dip->di_aformat != XFS_DINODE_FMT_BTREE)
		xchk_ino_set_corrupt(sc, ino);

	/* di_anextents */
	nextents = be16_to_cpu(dip->di_anextents);
	fork_recs =  XFS_DFORK_ASIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xchk_ino_set_corrupt(sc, ino);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (nextents <= fork_recs)
			xchk_ino_set_corrupt(sc, ino);
		break;
	default:
		if (nextents != 0)
			xchk_ino_set_corrupt(sc, ino);
	}

	if (dip->di_version >= 3) {
		if (be32_to_cpu(dip->di_crtime.t_nsec) >= NSEC_PER_SEC)
			xchk_ino_set_corrupt(sc, ino);
		xchk_inode_flags2(sc, dip, ino, mode, flags, flags2);
		xchk_inode_cowextsize(sc, dip, ino, mode, flags,
				flags2);
	}
}

/*
 * Make sure the finobt doesn't think this inode is free.
 * We don't have to check the inobt ourselves because we got the inode via
 * IGET_UNTRUSTED, which checks the inobt for us.
 */
static void
xchk_inode_xref_finobt(
	struct xfs_scrub		*sc,
	xfs_ino_t			ino)
{
	struct xfs_inobt_rec_incore	rec;
	xfs_agino_t			agino;
	int				has_record;
	int				error;

	if (!sc->sa.fino_cur || xchk_skip_xref(sc->sm))
		return;

	agino = XFS_INO_TO_AGINO(sc->mp, ino);

	/*
	 * Try to get the finobt record.  If we can't get it, then we're
	 * in good shape.
	 */
	error = xfs_inobt_lookup(sc->sa.fino_cur, agino, XFS_LOOKUP_LE,
			&has_record);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.fino_cur) ||
	    !has_record)
		return;

	error = xfs_inobt_get_rec(sc->sa.fino_cur, &rec, &has_record);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.fino_cur) ||
	    !has_record)
		return;

	/*
	 * Otherwise, make sure this record either doesn't cover this inode,
	 * or that it does but it's marked present.
	 */
	if (rec.ir_startino > agino ||
	    rec.ir_startino + XFS_INODES_PER_CHUNK <= agino)
		return;

	if (rec.ir_free & XFS_INOBT_MASK(agino - rec.ir_startino))
		xchk_btree_xref_set_corrupt(sc, sc->sa.fino_cur, 0);
}

/* Cross reference the inode fields with the forks. */
STATIC void
xchk_inode_xref_bmap(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip)
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
		xchk_ino_xref_set_corrupt(sc, sc->ip->i_ino);

	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_ATTR_FORK,
			&nextents, &acount);
	if (!xchk_should_check_xref(sc, &error, NULL))
		return;
	if (nextents != be16_to_cpu(dip->di_anextents))
		xchk_ino_xref_set_corrupt(sc, sc->ip->i_ino);

	/* Check nblocks against the inode. */
	if (count + acount != be64_to_cpu(dip->di_nblocks))
		xchk_ino_xref_set_corrupt(sc, sc->ip->i_ino);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_inode_xref(
	struct xfs_scrub	*sc,
	xfs_ino_t		ino,
	struct xfs_dinode	*dip)
{
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	agno = XFS_INO_TO_AGNO(sc->mp, ino);
	agbno = XFS_INO_TO_AGBNO(sc->mp, ino);

	error = xchk_ag_init(sc, agno, &sc->sa);
	if (!xchk_xref_process_error(sc, agno, agbno, &error))
		return;

	xchk_xref_is_used_space(sc, agbno, 1);
	xchk_inode_xref_finobt(sc, ino);
	xchk_xref_is_owned_by(sc, agbno, 1, &XFS_RMAP_OINFO_INODES);
	xchk_xref_is_not_shared(sc, agbno, 1);
	xchk_inode_xref_bmap(sc, dip);

	xchk_ag_free(sc, &sc->sa);
}

/*
 * If the reflink iflag disagrees with a scan for shared data fork extents,
 * either flag an error (shared extents w/ no flag) or a preen (flag set w/o
 * any shared extents).  We already checked for reflink iflag set on a non
 * reflink filesystem.
 */
static void
xchk_inode_check_reflink_iflag(
	struct xfs_scrub	*sc,
	xfs_ino_t		ino)
{
	struct xfs_mount	*mp = sc->mp;
	bool			has_shared;
	int			error;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return;

	error = xfs_reflink_inode_has_shared_extents(sc->tp, sc->ip,
			&has_shared);
	if (!xchk_xref_process_error(sc, XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino), &error))
		return;
	if (xfs_is_reflink_inode(sc->ip) && !has_shared)
		xchk_ino_set_preen(sc, ino);
	else if (!xfs_is_reflink_inode(sc->ip) && has_shared)
		xchk_ino_set_corrupt(sc, ino);
}

/* Scrub an inode. */
int
xchk_inode(
	struct xfs_scrub	*sc)
{
	struct xfs_dinode	di;
	int			error = 0;

	/*
	 * If sc->ip is NULL, that means that the setup function called
	 * xfs_iget to look up the inode.  xfs_iget returned a EFSCORRUPTED
	 * and a NULL inode, so flag the corruption error and return.
	 */
	if (!sc->ip) {
		xchk_ino_set_corrupt(sc, sc->sm->sm_ino);
		return 0;
	}

	/* Scrub the inode core. */
	xfs_inode_to_disk(sc->ip, &di, 0);
	xchk_dinode(sc, &di, sc->ip->i_ino);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/*
	 * Look for discrepancies between file's data blocks and the reflink
	 * iflag.  We already checked the iflag against the file mode when
	 * we scrubbed the dinode.
	 */
	if (S_ISREG(VFS_I(sc->ip)->i_mode))
		xchk_inode_check_reflink_iflag(sc, sc->ip->i_ino);

	xchk_inode_xref(sc, sc->ip->i_ino, &di);
out:
	return error;
}
