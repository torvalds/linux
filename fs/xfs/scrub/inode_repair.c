// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
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
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_quota_defs.h"
#include "xfs_quota.h"
#include "xfs_ag.h"
#include "xfs_rtbitmap.h"
#include "xfs_health.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"

/*
 * Inode Record Repair
 * ===================
 *
 * Roughly speaking, inode problems can be classified based on whether or not
 * they trip the dinode verifiers.  If those trip, then we won't be able to
 * xfs_iget ourselves the inode.
 *
 * Therefore, the xrep_dinode_* functions fix anything that will cause the
 * inode buffer verifier or the dinode verifier.  The xrep_inode_* functions
 * fix things on live incore inodes.  The inode repair functions make decisions
 * with security and usability implications when reviving a file:
 *
 * - Files with zero di_mode or a garbage di_mode are converted to regular file
 *   that only root can read.  This file may not actually contain user data,
 *   if the file was not previously a regular file.  Setuid and setgid bits
 *   are cleared.
 *
 * - Zero-size directories can be truncated to look empty.  It is necessary to
 *   run the bmapbtd and directory repair functions to fully rebuild the
 *   directory.
 *
 * - Zero-size symbolic link targets can be truncated to '?'.  It is necessary
 *   to run the bmapbtd and symlink repair functions to salvage the symlink.
 *
 * - Invalid extent size hints will be removed.
 *
 * - Quotacheck will be scheduled if we repaired an inode that was so badly
 *   damaged that the ondisk inode had to be rebuilt.
 *
 * - Invalid user, group, or project IDs (aka -1U) will be reset to zero.
 *   Setuid and setgid bits are cleared.
 */

/*
 * All the information we need to repair the ondisk inode if we can't iget the
 * incore inode.  We don't allocate this buffer unless we're going to perform
 * a repair to the ondisk inode cluster buffer.
 */
struct xrep_inode {
	/* Inode mapping that we saved from the initial lookup attempt. */
	struct xfs_imap		imap;

	struct xfs_scrub	*sc;

	/* Sick state to set after zapping parts of the inode. */
	unsigned int		ino_sick_mask;
};

/*
 * Setup function for inode repair.  @imap contains the ondisk inode mapping
 * information so that we can correct the ondisk inode cluster buffer if
 * necessary to make iget work.
 */
int
xrep_setup_inode(
	struct xfs_scrub	*sc,
	const struct xfs_imap	*imap)
{
	struct xrep_inode	*ri;

	sc->buf = kzalloc(sizeof(struct xrep_inode), XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -ENOMEM;

	ri = sc->buf;
	memcpy(&ri->imap, imap, sizeof(struct xfs_imap));
	ri->sc = sc;
	return 0;
}

/*
 * Make sure this ondisk inode can pass the inode buffer verifier.  This is
 * not the same as the dinode verifier.
 */
STATIC void
xrep_dinode_buf_core(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp,
	unsigned int		ioffset)
{
	struct xfs_dinode	*dip = xfs_buf_offset(bp, ioffset);
	struct xfs_trans	*tp = sc->tp;
	struct xfs_mount	*mp = sc->mp;
	xfs_agino_t		agino;
	bool			crc_ok = false;
	bool			magic_ok = false;
	bool			unlinked_ok = false;

	agino = be32_to_cpu(dip->di_next_unlinked);

	if (xfs_verify_agino_or_null(bp->b_pag, agino))
		unlinked_ok = true;

	if (dip->di_magic == cpu_to_be16(XFS_DINODE_MAGIC) &&
	    xfs_dinode_good_version(mp, dip->di_version))
		magic_ok = true;

	if (xfs_verify_cksum((char *)dip, mp->m_sb.sb_inodesize,
			XFS_DINODE_CRC_OFF))
		crc_ok = true;

	if (magic_ok && unlinked_ok && crc_ok)
		return;

	if (!magic_ok) {
		dip->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
		dip->di_version = 3;
	}
	if (!unlinked_ok)
		dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
	xfs_dinode_calc_crc(mp, dip);
	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_DINO_BUF);
	xfs_trans_log_buf(tp, bp, ioffset,
				  ioffset + sizeof(struct xfs_dinode) - 1);
}

/* Make sure this inode cluster buffer can pass the inode buffer verifier. */
STATIC void
xrep_dinode_buf(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = sc->mp;
	int			i;
	int			ni;

	ni = XFS_BB_TO_FSB(mp, bp->b_length) * mp->m_sb.sb_inopblock;
	for (i = 0; i < ni; i++)
		xrep_dinode_buf_core(sc, bp, i << mp->m_sb.sb_inodelog);
}

/* Reinitialize things that never change in an inode. */
STATIC void
xrep_dinode_header(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip)
{
	trace_xrep_dinode_header(sc, dip);

	dip->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
	if (!xfs_dinode_good_version(sc->mp, dip->di_version))
		dip->di_version = 3;
	dip->di_ino = cpu_to_be64(sc->sm->sm_ino);
	uuid_copy(&dip->di_uuid, &sc->mp->m_sb.sb_meta_uuid);
	dip->di_gen = cpu_to_be32(sc->sm->sm_gen);
}

/* Turn di_mode into /something/ recognizable. */
STATIC void
xrep_dinode_mode(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip)
{
	uint16_t		mode = be16_to_cpu(dip->di_mode);

	trace_xrep_dinode_mode(sc, dip);

	if (mode == 0 || xfs_mode_to_ftype(mode) != XFS_DIR3_FT_UNKNOWN)
		return;

	/* bad mode, so we set it to a file that only root can read */
	mode = S_IFREG;
	dip->di_mode = cpu_to_be16(mode);
	dip->di_uid = 0;
	dip->di_gid = 0;
}

/* Fix any conflicting flags that the verifiers complain about. */
STATIC void
xrep_dinode_flags(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip)
{
	struct xfs_mount	*mp = sc->mp;
	uint64_t		flags2 = be64_to_cpu(dip->di_flags2);
	uint16_t		flags = be16_to_cpu(dip->di_flags);
	uint16_t		mode = be16_to_cpu(dip->di_mode);

	trace_xrep_dinode_flags(sc, dip);

	/*
	 * For regular files on a reflink filesystem, set the REFLINK flag to
	 * protect shared extents.  A later stage will actually check those
	 * extents and clear the flag if possible.
	 */
	if (xfs_has_reflink(mp) && S_ISREG(mode))
		flags2 |= XFS_DIFLAG2_REFLINK;
	else
		flags2 &= ~(XFS_DIFLAG2_REFLINK | XFS_DIFLAG2_COWEXTSIZE);
	if (flags & XFS_DIFLAG_REALTIME)
		flags2 &= ~XFS_DIFLAG2_REFLINK;
	if (!xfs_has_bigtime(mp))
		flags2 &= ~XFS_DIFLAG2_BIGTIME;
	if (!xfs_has_large_extent_counts(mp))
		flags2 &= ~XFS_DIFLAG2_NREXT64;
	if (flags2 & XFS_DIFLAG2_NREXT64)
		dip->di_nrext64_pad = 0;
	else if (dip->di_version >= 3)
		dip->di_v3_pad = 0;
	dip->di_flags = cpu_to_be16(flags);
	dip->di_flags2 = cpu_to_be64(flags2);
}

/*
 * Blow out symlink; now it points nowhere.  We don't have to worry about
 * incore state because this inode is failing the verifiers.
 */
STATIC void
xrep_dinode_zap_symlink(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip)
{
	struct xfs_scrub	*sc = ri->sc;
	char			*p;

	trace_xrep_dinode_zap_symlink(sc, dip);

	dip->di_format = XFS_DINODE_FMT_LOCAL;
	dip->di_size = cpu_to_be64(1);
	p = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
	*p = '?';
	ri->ino_sick_mask |= XFS_SICK_INO_SYMLINK_ZAPPED;
}

/*
 * Blow out dir, make the parent point to the root.  In the future repair will
 * reconstruct this directory for us.  Note that there's no in-core directory
 * inode because the sf verifier tripped, so we don't have to worry about the
 * dentry cache.
 */
STATIC void
xrep_dinode_zap_dir(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_dir2_sf_hdr	*sfp;
	int			i8count;

	trace_xrep_dinode_zap_dir(sc, dip);

	dip->di_format = XFS_DINODE_FMT_LOCAL;
	i8count = mp->m_sb.sb_rootino > XFS_DIR2_MAX_SHORT_INUM;
	sfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
	sfp->count = 0;
	sfp->i8count = i8count;
	xfs_dir2_sf_put_parent_ino(sfp, mp->m_sb.sb_rootino);
	dip->di_size = cpu_to_be64(xfs_dir2_sf_hdr_size(i8count));
	ri->ino_sick_mask |= XFS_SICK_INO_DIR_ZAPPED;
}

/* Make sure we don't have a garbage file size. */
STATIC void
xrep_dinode_size(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip)
{
	struct xfs_scrub	*sc = ri->sc;
	uint64_t		size = be64_to_cpu(dip->di_size);
	uint16_t		mode = be16_to_cpu(dip->di_mode);

	trace_xrep_dinode_size(sc, dip);

	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		/* di_size can't be nonzero for special files */
		dip->di_size = 0;
		break;
	case S_IFREG:
		/* Regular files can't be larger than 2^63-1 bytes. */
		dip->di_size = cpu_to_be64(size & ~(1ULL << 63));
		break;
	case S_IFLNK:
		/*
		 * Truncate ridiculously oversized symlinks.  If the size is
		 * zero, reset it to point to the current directory.  Both of
		 * these conditions trigger dinode verifier errors, so there
		 * is no in-core state to reset.
		 */
		if (size > XFS_SYMLINK_MAXLEN)
			dip->di_size = cpu_to_be64(XFS_SYMLINK_MAXLEN);
		else if (size == 0)
			xrep_dinode_zap_symlink(ri, dip);
		break;
	case S_IFDIR:
		/*
		 * Directories can't have a size larger than 32G.  If the size
		 * is zero, reset it to an empty directory.  Both of these
		 * conditions trigger dinode verifier errors, so there is no
		 * in-core state to reset.
		 */
		if (size > XFS_DIR2_SPACE_SIZE)
			dip->di_size = cpu_to_be64(XFS_DIR2_SPACE_SIZE);
		else if (size == 0)
			xrep_dinode_zap_dir(ri, dip);
		break;
	}
}

/* Fix extent size hints. */
STATIC void
xrep_dinode_extsize_hints(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip)
{
	struct xfs_mount	*mp = sc->mp;
	uint64_t		flags2 = be64_to_cpu(dip->di_flags2);
	uint16_t		flags = be16_to_cpu(dip->di_flags);
	uint16_t		mode = be16_to_cpu(dip->di_mode);

	xfs_failaddr_t		fa;

	trace_xrep_dinode_extsize_hints(sc, dip);

	fa = xfs_inode_validate_extsize(mp, be32_to_cpu(dip->di_extsize),
			mode, flags);
	if (fa) {
		dip->di_extsize = 0;
		dip->di_flags &= ~cpu_to_be16(XFS_DIFLAG_EXTSIZE |
					      XFS_DIFLAG_EXTSZINHERIT);
	}

	if (dip->di_version < 3)
		return;

	fa = xfs_inode_validate_cowextsize(mp, be32_to_cpu(dip->di_cowextsize),
			mode, flags, flags2);
	if (fa) {
		dip->di_cowextsize = 0;
		dip->di_flags2 &= ~cpu_to_be64(XFS_DIFLAG2_COWEXTSIZE);
	}
}

/* Inode didn't pass dinode verifiers, so fix the raw buffer and retry iget. */
STATIC int
xrep_dinode_core(
	struct xrep_inode	*ri)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_buf		*bp;
	struct xfs_dinode	*dip;
	xfs_ino_t		ino = sc->sm->sm_ino;
	int			error;
	int			iget_error;

	/* Read the inode cluster buffer. */
	error = xfs_trans_read_buf(sc->mp, sc->tp, sc->mp->m_ddev_targp,
			ri->imap.im_blkno, ri->imap.im_len, XBF_UNMAPPED, &bp,
			NULL);
	if (error)
		return error;

	/* Make sure we can pass the inode buffer verifier. */
	xrep_dinode_buf(sc, bp);
	bp->b_ops = &xfs_inode_buf_ops;

	/* Fix everything the verifier will complain about. */
	dip = xfs_buf_offset(bp, ri->imap.im_boffset);
	xrep_dinode_header(sc, dip);
	xrep_dinode_mode(sc, dip);
	xrep_dinode_flags(sc, dip);
	xrep_dinode_size(ri, dip);
	xrep_dinode_extsize_hints(sc, dip);

	/* Write out the inode. */
	trace_xrep_dinode_fixed(sc, dip);
	xfs_dinode_calc_crc(sc->mp, dip);
	xfs_trans_buf_set_type(sc->tp, bp, XFS_BLFT_DINO_BUF);
	xfs_trans_log_buf(sc->tp, bp, ri->imap.im_boffset,
			ri->imap.im_boffset + sc->mp->m_sb.sb_inodesize - 1);

	/*
	 * In theory, we've fixed the ondisk inode record enough that we should
	 * be able to load the inode into the cache.  Try to iget that inode
	 * now while we hold the AGI and the inode cluster buffer and take the
	 * IOLOCK so that we can continue with repairs without anyone else
	 * accessing the inode.  If iget fails, we still need to commit the
	 * changes.
	 */
	iget_error = xchk_iget(sc, ino, &sc->ip);
	if (!iget_error)
		xchk_ilock(sc, XFS_IOLOCK_EXCL);

	/*
	 * Commit the inode cluster buffer updates and drop the AGI buffer that
	 * we've been holding since scrub setup.  From here on out, repairs
	 * deal only with the cached inode.
	 */
	error = xrep_trans_commit(sc);
	if (error)
		return error;

	if (iget_error)
		return iget_error;

	error = xchk_trans_alloc(sc, 0);
	if (error)
		return error;

	error = xrep_ino_dqattach(sc);
	if (error)
		return error;

	xchk_ilock(sc, XFS_ILOCK_EXCL);
	if (ri->ino_sick_mask)
		xfs_inode_mark_sick(sc->ip, ri->ino_sick_mask);
	return 0;
}

/* Fix everything xfs_dinode_verify cares about. */
STATIC int
xrep_dinode_problems(
	struct xrep_inode	*ri)
{
	struct xfs_scrub	*sc = ri->sc;
	int			error;

	error = xrep_dinode_core(ri);
	if (error)
		return error;

	/* We had to fix a totally busted inode, schedule quotacheck. */
	if (XFS_IS_UQUOTA_ON(sc->mp))
		xrep_force_quotacheck(sc, XFS_DQTYPE_USER);
	if (XFS_IS_GQUOTA_ON(sc->mp))
		xrep_force_quotacheck(sc, XFS_DQTYPE_GROUP);
	if (XFS_IS_PQUOTA_ON(sc->mp))
		xrep_force_quotacheck(sc, XFS_DQTYPE_PROJ);

	return 0;
}

/*
 * Fix problems that the verifiers don't care about.  In general these are
 * errors that don't cause problems elsewhere in the kernel that we can easily
 * detect, so we don't check them all that rigorously.
 */

/* Make sure block and extent counts are ok. */
STATIC int
xrep_inode_blockcounts(
	struct xfs_scrub	*sc)
{
	struct xfs_ifork	*ifp;
	xfs_filblks_t		count;
	xfs_filblks_t		acount;
	xfs_extnum_t		nextents;
	int			error;

	trace_xrep_inode_blockcounts(sc);

	/* Set data fork counters from the data fork mappings. */
	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_DATA_FORK,
			&nextents, &count);
	if (error)
		return error;
	if (xfs_is_reflink_inode(sc->ip)) {
		/*
		 * data fork blockcount can exceed physical storage if a user
		 * reflinks the same block over and over again.
		 */
		;
	} else if (XFS_IS_REALTIME_INODE(sc->ip)) {
		if (count >= sc->mp->m_sb.sb_rblocks)
			return -EFSCORRUPTED;
	} else {
		if (count >= sc->mp->m_sb.sb_dblocks)
			return -EFSCORRUPTED;
	}
	error = xrep_ino_ensure_extent_count(sc, XFS_DATA_FORK, nextents);
	if (error)
		return error;
	sc->ip->i_df.if_nextents = nextents;

	/* Set attr fork counters from the attr fork mappings. */
	ifp = xfs_ifork_ptr(sc->ip, XFS_ATTR_FORK);
	if (ifp) {
		error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_ATTR_FORK,
				&nextents, &acount);
		if (error)
			return error;
		if (count >= sc->mp->m_sb.sb_dblocks)
			return -EFSCORRUPTED;
		error = xrep_ino_ensure_extent_count(sc, XFS_ATTR_FORK,
				nextents);
		if (error)
			return error;
		ifp->if_nextents = nextents;
	} else {
		acount = 0;
	}

	sc->ip->i_nblocks = count + acount;
	return 0;
}

/* Check for invalid uid/gid/prid. */
STATIC void
xrep_inode_ids(
	struct xfs_scrub	*sc)
{
	bool			dirty = false;

	trace_xrep_inode_ids(sc);

	if (!uid_valid(VFS_I(sc->ip)->i_uid)) {
		i_uid_write(VFS_I(sc->ip), 0);
		dirty = true;
		if (XFS_IS_UQUOTA_ON(sc->mp))
			xrep_force_quotacheck(sc, XFS_DQTYPE_USER);
	}

	if (!gid_valid(VFS_I(sc->ip)->i_gid)) {
		i_gid_write(VFS_I(sc->ip), 0);
		dirty = true;
		if (XFS_IS_GQUOTA_ON(sc->mp))
			xrep_force_quotacheck(sc, XFS_DQTYPE_GROUP);
	}

	if (sc->ip->i_projid == -1U) {
		sc->ip->i_projid = 0;
		dirty = true;
		if (XFS_IS_PQUOTA_ON(sc->mp))
			xrep_force_quotacheck(sc, XFS_DQTYPE_PROJ);
	}

	/* strip setuid/setgid if we touched any of the ids */
	if (dirty)
		VFS_I(sc->ip)->i_mode &= ~(S_ISUID | S_ISGID);
}

static inline void
xrep_clamp_timestamp(
	struct xfs_inode	*ip,
	struct timespec64	*ts)
{
	ts->tv_nsec = clamp_t(long, ts->tv_nsec, 0, NSEC_PER_SEC);
	*ts = timestamp_truncate(*ts, VFS_I(ip));
}

/* Nanosecond counters can't have more than 1 billion. */
STATIC void
xrep_inode_timestamps(
	struct xfs_inode	*ip)
{
	struct timespec64	tstamp;
	struct inode		*inode = VFS_I(ip);

	tstamp = inode_get_atime(inode);
	xrep_clamp_timestamp(ip, &tstamp);
	inode_set_atime_to_ts(inode, tstamp);

	tstamp = inode_get_mtime(inode);
	xrep_clamp_timestamp(ip, &tstamp);
	inode_set_mtime_to_ts(inode, tstamp);

	tstamp = inode_get_ctime(inode);
	xrep_clamp_timestamp(ip, &tstamp);
	inode_set_ctime_to_ts(inode, tstamp);

	xrep_clamp_timestamp(ip, &ip->i_crtime);
}

/* Fix inode flags that don't make sense together. */
STATIC void
xrep_inode_flags(
	struct xfs_scrub	*sc)
{
	uint16_t		mode;

	trace_xrep_inode_flags(sc);

	mode = VFS_I(sc->ip)->i_mode;

	/* Clear junk flags */
	if (sc->ip->i_diflags & ~XFS_DIFLAG_ANY)
		sc->ip->i_diflags &= ~XFS_DIFLAG_ANY;

	/* NEWRTBM only applies to realtime bitmaps */
	if (sc->ip->i_ino == sc->mp->m_sb.sb_rbmino)
		sc->ip->i_diflags |= XFS_DIFLAG_NEWRTBM;
	else
		sc->ip->i_diflags &= ~XFS_DIFLAG_NEWRTBM;

	/* These only make sense for directories. */
	if (!S_ISDIR(mode))
		sc->ip->i_diflags &= ~(XFS_DIFLAG_RTINHERIT |
					  XFS_DIFLAG_EXTSZINHERIT |
					  XFS_DIFLAG_PROJINHERIT |
					  XFS_DIFLAG_NOSYMLINKS);

	/* These only make sense for files. */
	if (!S_ISREG(mode))
		sc->ip->i_diflags &= ~(XFS_DIFLAG_REALTIME |
					  XFS_DIFLAG_EXTSIZE);

	/* These only make sense for non-rt files. */
	if (sc->ip->i_diflags & XFS_DIFLAG_REALTIME)
		sc->ip->i_diflags &= ~XFS_DIFLAG_FILESTREAM;

	/* Immutable and append only?  Drop the append. */
	if ((sc->ip->i_diflags & XFS_DIFLAG_IMMUTABLE) &&
	    (sc->ip->i_diflags & XFS_DIFLAG_APPEND))
		sc->ip->i_diflags &= ~XFS_DIFLAG_APPEND;

	/* Clear junk flags. */
	if (sc->ip->i_diflags2 & ~XFS_DIFLAG2_ANY)
		sc->ip->i_diflags2 &= ~XFS_DIFLAG2_ANY;

	/* No reflink flag unless we support it and it's a file. */
	if (!xfs_has_reflink(sc->mp) || !S_ISREG(mode))
		sc->ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;

	/* DAX only applies to files and dirs. */
	if (!(S_ISREG(mode) || S_ISDIR(mode)))
		sc->ip->i_diflags2 &= ~XFS_DIFLAG2_DAX;

	/* No reflink files on the realtime device. */
	if (sc->ip->i_diflags & XFS_DIFLAG_REALTIME)
		sc->ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;
}

/*
 * Fix size problems with block/node format directories.  If we fail to find
 * the extent list, just bail out and let the bmapbtd repair functions clean
 * up that mess.
 */
STATIC void
xrep_inode_blockdir_size(
	struct xfs_scrub	*sc)
{
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	got;
	struct xfs_ifork	*ifp;
	xfs_fileoff_t		off;
	int			error;

	trace_xrep_inode_blockdir_size(sc);

	error = xfs_iread_extents(sc->tp, sc->ip, XFS_DATA_FORK);
	if (error)
		return;

	/* Find the last block before 32G; this is the dir size. */
	ifp = xfs_ifork_ptr(sc->ip, XFS_DATA_FORK);
	off = XFS_B_TO_FSB(sc->mp, XFS_DIR2_SPACE_SIZE);
	if (!xfs_iext_lookup_extent_before(sc->ip, ifp, &off, &icur, &got)) {
		/* zero-extents directory? */
		return;
	}

	off = got.br_startoff + got.br_blockcount;
	sc->ip->i_disk_size = min_t(loff_t, XFS_DIR2_SPACE_SIZE,
			XFS_FSB_TO_B(sc->mp, off));
}

/* Fix size problems with short format directories. */
STATIC void
xrep_inode_sfdir_size(
	struct xfs_scrub	*sc)
{
	struct xfs_ifork	*ifp;

	trace_xrep_inode_sfdir_size(sc);

	ifp = xfs_ifork_ptr(sc->ip, XFS_DATA_FORK);
	sc->ip->i_disk_size = ifp->if_bytes;
}

/*
 * Fix any irregularities in a directory inode's size now that we can iterate
 * extent maps and access other regular inode data.
 */
STATIC void
xrep_inode_dir_size(
	struct xfs_scrub	*sc)
{
	trace_xrep_inode_dir_size(sc);

	switch (sc->ip->i_df.if_format) {
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		xrep_inode_blockdir_size(sc);
		break;
	case XFS_DINODE_FMT_LOCAL:
		xrep_inode_sfdir_size(sc);
		break;
	}
}

/* Fix extent size hint problems. */
STATIC void
xrep_inode_extsize(
	struct xfs_scrub	*sc)
{
	/* Fix misaligned extent size hints on a directory. */
	if ((sc->ip->i_diflags & XFS_DIFLAG_RTINHERIT) &&
	    (sc->ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) &&
	    xfs_extlen_to_rtxmod(sc->mp, sc->ip->i_extsize) > 0) {
		sc->ip->i_extsize = 0;
		sc->ip->i_diflags &= ~XFS_DIFLAG_EXTSZINHERIT;
	}
}

/* Fix any irregularities in an inode that the verifiers don't catch. */
STATIC int
xrep_inode_problems(
	struct xfs_scrub	*sc)
{
	int			error;

	error = xrep_inode_blockcounts(sc);
	if (error)
		return error;
	xrep_inode_timestamps(sc->ip);
	xrep_inode_flags(sc);
	xrep_inode_ids(sc);
	/*
	 * We can now do a better job fixing the size of a directory now that
	 * we can scan the data fork extents than we could in xrep_dinode_size.
	 */
	if (S_ISDIR(VFS_I(sc->ip)->i_mode))
		xrep_inode_dir_size(sc);
	xrep_inode_extsize(sc);

	trace_xrep_inode_fixed(sc);
	xfs_trans_log_inode(sc->tp, sc->ip, XFS_ILOG_CORE);
	return xrep_roll_trans(sc);
}

/* Repair an inode's fields. */
int
xrep_inode(
	struct xfs_scrub	*sc)
{
	int			error = 0;

	/*
	 * No inode?  That means we failed the _iget verifiers.  Repair all
	 * the things that the inode verifiers care about, then retry _iget.
	 */
	if (!sc->ip) {
		struct xrep_inode	*ri = sc->buf;

		ASSERT(ri != NULL);

		error = xrep_dinode_problems(ri);
		if (error)
			return error;

		/* By this point we had better have a working incore inode. */
		if (!sc->ip)
			return -EFSCORRUPTED;
	}

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	/* If we found corruption of any kind, try to fix it. */
	if ((sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT) ||
	    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_XCORRUPT)) {
		error = xrep_inode_problems(sc);
		if (error)
			return error;
	}

	/* See if we can clear the reflink flag. */
	if (xfs_is_reflink_inode(sc->ip)) {
		error = xfs_reflink_clear_inode_flag(sc->ip, &sc->tp);
		if (error)
			return error;
	}

	return xrep_defer_finish(sc);
}
