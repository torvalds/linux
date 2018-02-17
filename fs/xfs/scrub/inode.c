/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
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
xfs_scrub_setup_inode(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	struct xfs_mount		*mp = sc->mp;
	int				error;

	/*
	 * Try to get the inode.  If the verifiers fail, we try again
	 * in raw mode.
	 */
	error = xfs_scrub_get_inode(sc, ip);
	switch (error) {
	case 0:
		break;
	case -EFSCORRUPTED:
	case -EFSBADCRC:
		return xfs_scrub_trans_alloc(sc->sm, mp, &sc->tp);
	default:
		return error;
	}

	/* Got the inode, lock it and we're ready to go. */
	sc->ilock_flags = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;
	xfs_ilock(sc->ip, sc->ilock_flags);
	error = xfs_scrub_trans_alloc(sc->sm, mp, &sc->tp);
	if (error)
		goto out;
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	xfs_ilock(sc->ip, XFS_ILOCK_EXCL);

out:
	/* scrub teardown will unlock and release the inode for us */
	return error;
}

/* Inode core */

/*
 * Validate di_extsize hint.
 *
 * The rules are documented at xfs_ioctl_setattr_check_extsize().
 * These functions must be kept in sync with each other.
 */
STATIC void
xfs_scrub_inode_extsize(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp,
	struct xfs_dinode		*dip,
	xfs_ino_t			ino,
	uint16_t			mode,
	uint16_t			flags)
{
	struct xfs_mount		*mp = sc->mp;
	bool				rt_flag;
	bool				hint_flag;
	bool				inherit_flag;
	uint32_t			extsize;
	uint32_t			extsize_bytes;
	uint32_t			blocksize_bytes;

	rt_flag = (flags & XFS_DIFLAG_REALTIME);
	hint_flag = (flags & XFS_DIFLAG_EXTSIZE);
	inherit_flag = (flags & XFS_DIFLAG_EXTSZINHERIT);
	extsize = be32_to_cpu(dip->di_extsize);
	extsize_bytes = XFS_FSB_TO_B(sc->mp, extsize);

	if (rt_flag)
		blocksize_bytes = mp->m_sb.sb_rextsize << mp->m_sb.sb_blocklog;
	else
		blocksize_bytes = mp->m_sb.sb_blocksize;

	if ((hint_flag || inherit_flag) && !(S_ISDIR(mode) || S_ISREG(mode)))
		goto bad;

	if (hint_flag && !S_ISREG(mode))
		goto bad;

	if (inherit_flag && !S_ISDIR(mode))
		goto bad;

	if ((hint_flag || inherit_flag) && extsize == 0)
		goto bad;

	if (!(hint_flag || inherit_flag) && extsize != 0)
		goto bad;

	if (extsize_bytes % blocksize_bytes)
		goto bad;

	if (extsize > MAXEXTLEN)
		goto bad;

	if (!rt_flag && extsize > mp->m_sb.sb_agblocks / 2)
		goto bad;

	return;
bad:
	xfs_scrub_ino_set_corrupt(sc, ino, bp);
}

/*
 * Validate di_cowextsize hint.
 *
 * The rules are documented at xfs_ioctl_setattr_check_cowextsize().
 * These functions must be kept in sync with each other.
 */
STATIC void
xfs_scrub_inode_cowextsize(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp,
	struct xfs_dinode		*dip,
	xfs_ino_t			ino,
	uint16_t			mode,
	uint16_t			flags,
	uint64_t			flags2)
{
	struct xfs_mount		*mp = sc->mp;
	bool				rt_flag;
	bool				hint_flag;
	uint32_t			extsize;
	uint32_t			extsize_bytes;

	rt_flag = (flags & XFS_DIFLAG_REALTIME);
	hint_flag = (flags2 & XFS_DIFLAG2_COWEXTSIZE);
	extsize = be32_to_cpu(dip->di_cowextsize);
	extsize_bytes = XFS_FSB_TO_B(sc->mp, extsize);

	if (hint_flag && !xfs_sb_version_hasreflink(&mp->m_sb))
		goto bad;

	if (hint_flag && !(S_ISDIR(mode) || S_ISREG(mode)))
		goto bad;

	if (hint_flag && extsize == 0)
		goto bad;

	if (!hint_flag && extsize != 0)
		goto bad;

	if (hint_flag && rt_flag)
		goto bad;

	if (extsize_bytes % mp->m_sb.sb_blocksize)
		goto bad;

	if (extsize > MAXEXTLEN)
		goto bad;

	if (extsize > mp->m_sb.sb_agblocks / 2)
		goto bad;

	return;
bad:
	xfs_scrub_ino_set_corrupt(sc, ino, bp);
}

/* Make sure the di_flags make sense for the inode. */
STATIC void
xfs_scrub_inode_flags(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp,
	struct xfs_dinode		*dip,
	xfs_ino_t			ino,
	uint16_t			mode,
	uint16_t			flags)
{
	struct xfs_mount		*mp = sc->mp;

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
	xfs_scrub_ino_set_corrupt(sc, ino, bp);
}

/* Make sure the di_flags2 make sense for the inode. */
STATIC void
xfs_scrub_inode_flags2(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp,
	struct xfs_dinode		*dip,
	xfs_ino_t			ino,
	uint16_t			mode,
	uint16_t			flags,
	uint64_t			flags2)
{
	struct xfs_mount		*mp = sc->mp;

	if (flags2 & ~XFS_DIFLAG2_ANY)
		goto bad;

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
	xfs_scrub_ino_set_corrupt(sc, ino, bp);
}

/* Scrub all the ondisk inode fields. */
STATIC void
xfs_scrub_dinode(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp,
	struct xfs_dinode		*dip,
	xfs_ino_t			ino)
{
	struct xfs_mount		*mp = sc->mp;
	size_t				fork_recs;
	unsigned long long		isize;
	uint64_t			flags2;
	uint32_t			nextents;
	uint16_t			flags;
	uint16_t			mode;

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
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	}

	/* v1/v2 fields */
	switch (dip->di_version) {
	case 1:
		/*
		 * We autoconvert v1 inodes into v2 inodes on writeout,
		 * so just mark this inode for preening.
		 */
		xfs_scrub_ino_set_preen(sc, ino, bp);
		break;
	case 2:
	case 3:
		if (dip->di_onlink != 0)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);

		if (dip->di_mode == 0 && sc->ip)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);

		if (dip->di_projid_hi != 0 &&
		    !xfs_sb_version_hasprojid32bit(&mp->m_sb))
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	default:
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
		return;
	}

	/*
	 * di_uid/di_gid -- -1 isn't invalid, but there's no way that
	 * userspace could have created that.
	 */
	if (dip->di_uid == cpu_to_be32(-1U) ||
	    dip->di_gid == cpu_to_be32(-1U))
		xfs_scrub_ino_set_warning(sc, ino, bp);

	/* di_format */
	switch (dip->di_format) {
	case XFS_DINODE_FMT_DEV:
		if (!S_ISCHR(mode) && !S_ISBLK(mode) &&
		    !S_ISFIFO(mode) && !S_ISSOCK(mode))
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	case XFS_DINODE_FMT_LOCAL:
		if (!S_ISDIR(mode) && !S_ISLNK(mode))
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		if (!S_ISREG(mode) && !S_ISDIR(mode) && !S_ISLNK(mode))
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (!S_ISREG(mode) && !S_ISDIR(mode))
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	case XFS_DINODE_FMT_UUID:
	default:
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	}

	/* di_[amc]time.nsec */
	if (be32_to_cpu(dip->di_atime.t_nsec) >= NSEC_PER_SEC)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
	if (be32_to_cpu(dip->di_mtime.t_nsec) >= NSEC_PER_SEC)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
	if (be32_to_cpu(dip->di_ctime.t_nsec) >= NSEC_PER_SEC)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/*
	 * di_size.  xfs_dinode_verify checks for things that screw up
	 * the VFS such as the upper bit being set and zero-length
	 * symlinks/directories, but we can do more here.
	 */
	isize = be64_to_cpu(dip->di_size);
	if (isize & (1ULL << 63))
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/* Devices, fifos, and sockets must have zero size */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode) && isize != 0)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/* Directories can't be larger than the data section size (32G) */
	if (S_ISDIR(mode) && (isize == 0 || isize >= XFS_DIR2_SPACE_SIZE))
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/* Symlinks can't be larger than SYMLINK_MAXLEN */
	if (S_ISLNK(mode) && (isize == 0 || isize >= XFS_SYMLINK_MAXLEN))
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/*
	 * Warn if the running kernel can't handle the kinds of offsets
	 * needed to deal with the file size.  In other words, if the
	 * pagecache can't cache all the blocks in this file due to
	 * overly large offsets, flag the inode for admin review.
	 */
	if (isize >= mp->m_super->s_maxbytes)
		xfs_scrub_ino_set_warning(sc, ino, bp);

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
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
	} else {
		if (be64_to_cpu(dip->di_nblocks) >= mp->m_sb.sb_dblocks)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
	}

	xfs_scrub_inode_flags(sc, bp, dip, ino, mode, flags);

	xfs_scrub_inode_extsize(sc, bp, dip, ino, mode, flags);

	/* di_nextents */
	nextents = be32_to_cpu(dip->di_nextents);
	fork_recs =  XFS_DFORK_DSIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (nextents <= fork_recs)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	default:
		if (nextents != 0)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	}

	/* di_forkoff */
	if (XFS_DFORK_APTR(dip) >= (char *)dip + mp->m_sb.sb_inodesize)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
	if (dip->di_anextents != 0 && dip->di_forkoff == 0)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
	if (dip->di_forkoff == 0 && dip->di_aformat != XFS_DINODE_FMT_EXTENTS)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/* di_aformat */
	if (dip->di_aformat != XFS_DINODE_FMT_LOCAL &&
	    dip->di_aformat != XFS_DINODE_FMT_EXTENTS &&
	    dip->di_aformat != XFS_DINODE_FMT_BTREE)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);

	/* di_anextents */
	nextents = be16_to_cpu(dip->di_anextents);
	fork_recs =  XFS_DFORK_ASIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_aformat) {
	case XFS_DINODE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	case XFS_DINODE_FMT_BTREE:
		if (nextents <= fork_recs)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		break;
	default:
		if (nextents != 0)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
	}

	if (dip->di_version >= 3) {
		if (be32_to_cpu(dip->di_crtime.t_nsec) >= NSEC_PER_SEC)
			xfs_scrub_ino_set_corrupt(sc, ino, bp);
		xfs_scrub_inode_flags2(sc, bp, dip, ino, mode, flags, flags2);
		xfs_scrub_inode_cowextsize(sc, bp, dip, ino, mode, flags,
				flags2);
	}
}

/* Map and read a raw inode. */
STATIC int
xfs_scrub_inode_map_raw(
	struct xfs_scrub_context	*sc,
	xfs_ino_t			ino,
	struct xfs_buf			**bpp,
	struct xfs_dinode		**dipp)
{
	struct xfs_imap			imap;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_buf			*bp = NULL;
	struct xfs_dinode		*dip;
	int				error;

	error = xfs_imap(mp, sc->tp, ino, &imap, XFS_IGET_UNTRUSTED);
	if (error == -EINVAL) {
		/*
		 * Inode could have gotten deleted out from under us;
		 * just forget about it.
		 */
		error = -ENOENT;
		goto out;
	}
	if (!xfs_scrub_process_error(sc, XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino), &error))
		goto out;

	error = xfs_trans_read_buf(mp, sc->tp, mp->m_ddev_targp,
			imap.im_blkno, imap.im_len, XBF_UNMAPPED, &bp,
			NULL);
	if (!xfs_scrub_process_error(sc, XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino), &error))
		goto out;

	/*
	 * Is this really an inode?  We disabled verifiers in the above
	 * xfs_trans_read_buf call because the inode buffer verifier
	 * fails on /any/ inode record in the inode cluster with a bad
	 * magic or version number, not just the one that we're
	 * checking.  Therefore, grab the buffer unconditionally, attach
	 * the inode verifiers by hand, and run the inode verifier only
	 * on the one inode we want.
	 */
	bp->b_ops = &xfs_inode_buf_ops;
	dip = xfs_buf_offset(bp, imap.im_boffset);
	if (xfs_dinode_verify(mp, ino, dip) != NULL ||
	    !xfs_dinode_good_version(mp, dip->di_version)) {
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
		goto out_buf;
	}

	/* ...and is it the one we asked for? */
	if (be32_to_cpu(dip->di_gen) != sc->sm->sm_gen) {
		error = -ENOENT;
		goto out_buf;
	}

	*dipp = dip;
	*bpp = bp;
out:
	return error;
out_buf:
	xfs_trans_brelse(sc->tp, bp);
	return error;
}

/*
 * Make sure the finobt doesn't think this inode is free.
 * We don't have to check the inobt ourselves because we got the inode via
 * IGET_UNTRUSTED, which checks the inobt for us.
 */
static void
xfs_scrub_inode_xref_finobt(
	struct xfs_scrub_context	*sc,
	xfs_ino_t			ino)
{
	struct xfs_inobt_rec_incore	rec;
	xfs_agino_t			agino;
	int				has_record;
	int				error;

	if (!sc->sa.fino_cur)
		return;

	agino = XFS_INO_TO_AGINO(sc->mp, ino);

	/*
	 * Try to get the finobt record.  If we can't get it, then we're
	 * in good shape.
	 */
	error = xfs_inobt_lookup(sc->sa.fino_cur, agino, XFS_LOOKUP_LE,
			&has_record);
	if (!xfs_scrub_should_check_xref(sc, &error, &sc->sa.fino_cur) ||
	    !has_record)
		return;

	error = xfs_inobt_get_rec(sc->sa.fino_cur, &rec, &has_record);
	if (!xfs_scrub_should_check_xref(sc, &error, &sc->sa.fino_cur) ||
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
		xfs_scrub_btree_xref_set_corrupt(sc, sc->sa.fino_cur, 0);
}

/* Cross reference the inode fields with the forks. */
STATIC void
xfs_scrub_inode_xref_bmap(
	struct xfs_scrub_context	*sc,
	struct xfs_dinode		*dip)
{
	xfs_extnum_t			nextents;
	xfs_filblks_t			count;
	xfs_filblks_t			acount;
	int				error;

	/* Walk all the extents to check nextents/naextents/nblocks. */
	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_DATA_FORK,
			&nextents, &count);
	if (!xfs_scrub_should_check_xref(sc, &error, NULL))
		return;
	if (nextents < be32_to_cpu(dip->di_nextents))
		xfs_scrub_ino_xref_set_corrupt(sc, sc->ip->i_ino, NULL);

	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_ATTR_FORK,
			&nextents, &acount);
	if (!xfs_scrub_should_check_xref(sc, &error, NULL))
		return;
	if (nextents != be16_to_cpu(dip->di_anextents))
		xfs_scrub_ino_xref_set_corrupt(sc, sc->ip->i_ino, NULL);

	/* Check nblocks against the inode. */
	if (count + acount != be64_to_cpu(dip->di_nblocks))
		xfs_scrub_ino_xref_set_corrupt(sc, sc->ip->i_ino, NULL);
}

/* Cross-reference with the other btrees. */
STATIC void
xfs_scrub_inode_xref(
	struct xfs_scrub_context	*sc,
	xfs_ino_t			ino,
	struct xfs_dinode		*dip)
{
	struct xfs_owner_info		oinfo;
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;
	int				error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	agno = XFS_INO_TO_AGNO(sc->mp, ino);
	agbno = XFS_INO_TO_AGBNO(sc->mp, ino);

	error = xfs_scrub_ag_init(sc, agno, &sc->sa);
	if (!xfs_scrub_xref_process_error(sc, agno, agbno, &error))
		return;

	xfs_scrub_xref_is_used_space(sc, agbno, 1);
	xfs_scrub_inode_xref_finobt(sc, ino);
	xfs_rmap_ag_owner(&oinfo, XFS_RMAP_OWN_INODES);
	xfs_scrub_xref_is_owned_by(sc, agbno, 1, &oinfo);
	xfs_scrub_xref_is_not_shared(sc, agbno, 1);
	xfs_scrub_inode_xref_bmap(sc, dip);

	xfs_scrub_ag_free(sc, &sc->sa);
}

/*
 * If the reflink iflag disagrees with a scan for shared data fork extents,
 * either flag an error (shared extents w/ no flag) or a preen (flag set w/o
 * any shared extents).  We already checked for reflink iflag set on a non
 * reflink filesystem.
 */
static void
xfs_scrub_inode_check_reflink_iflag(
	struct xfs_scrub_context	*sc,
	xfs_ino_t			ino,
	struct xfs_buf			*bp)
{
	struct xfs_mount		*mp = sc->mp;
	bool				has_shared;
	int				error;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return;

	error = xfs_reflink_inode_has_shared_extents(sc->tp, sc->ip,
			&has_shared);
	if (!xfs_scrub_xref_process_error(sc, XFS_INO_TO_AGNO(mp, ino),
			XFS_INO_TO_AGBNO(mp, ino), &error))
		return;
	if (xfs_is_reflink_inode(sc->ip) && !has_shared)
		xfs_scrub_ino_set_preen(sc, ino, bp);
	else if (!xfs_is_reflink_inode(sc->ip) && has_shared)
		xfs_scrub_ino_set_corrupt(sc, ino, bp);
}

/* Scrub an inode. */
int
xfs_scrub_inode(
	struct xfs_scrub_context	*sc)
{
	struct xfs_dinode		di;
	struct xfs_buf			*bp = NULL;
	struct xfs_dinode		*dip;
	xfs_ino_t			ino;
	int				error = 0;

	/* Did we get the in-core inode, or are we doing this manually? */
	if (sc->ip) {
		ino = sc->ip->i_ino;
		xfs_inode_to_disk(sc->ip, &di, 0);
		dip = &di;
	} else {
		/* Map & read inode. */
		ino = sc->sm->sm_ino;
		error = xfs_scrub_inode_map_raw(sc, ino, &bp, &dip);
		if (error || !bp)
			goto out;
	}

	xfs_scrub_dinode(sc, bp, dip, ino);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Now let's do the things that require a live inode. */
	if (!sc->ip)
		goto out;

	/*
	 * Look for discrepancies between file's data blocks and the reflink
	 * iflag.  We already checked the iflag against the file mode when
	 * we scrubbed the dinode.
	 */
	if (S_ISREG(VFS_I(sc->ip)->i_mode))
		xfs_scrub_inode_check_reflink_iflag(sc, ino, bp);

	xfs_scrub_inode_xref(sc, ino, dip);
out:
	if (bp)
		xfs_trans_brelse(sc->tp, bp);
	return error;
}
