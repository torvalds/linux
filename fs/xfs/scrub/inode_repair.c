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
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap_util.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_quota_defs.h"
#include "xfs_quota.h"
#include "xfs_ag.h"
#include "xfs_rtbitmap.h"
#include "xfs_attr_leaf.h"
#include "xfs_log_priv.h"
#include "xfs_health.h"
#include "xfs_symlink_remote.h"
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_rtrefcount_btree.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/iscan.h"
#include "scrub/readdir.h"
#include "scrub/tempfile.h"

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
 *
 * - Data and attr forks are reset to extents format with zero extents if the
 *   fork data is inconsistent.  It is necessary to run the bmapbtd or bmapbta
 *   repair functions to recover the space mapping.
 *
 * - ACLs will not be recovered if the attr fork is zapped or the extended
 *   attribute structure itself requires salvaging.
 *
 * - If the attr fork is zapped, the user and group ids are reset to root and
 *   the setuid and setgid bits are removed.
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

	/* Blocks in use on the data device by data extents or bmbt blocks. */
	xfs_rfsblock_t		data_blocks;

	/* Blocks in use on the rt device. */
	xfs_rfsblock_t		rt_blocks;

	/* Blocks in use by the attr fork. */
	xfs_rfsblock_t		attr_blocks;

	/* Number of data device extents for the data fork. */
	xfs_extnum_t		data_extents;

	/*
	 * Number of realtime device extents for the data fork.  If
	 * data_extents and rt_extents indicate that the data fork has extents
	 * on both devices, we'll just back away slowly.
	 */
	xfs_extnum_t		rt_extents;

	/* Number of (data device) extents for the attr fork. */
	xfs_aextnum_t		attr_extents;

	/* Sick state to set after zapping parts of the inode. */
	unsigned int		ino_sick_mask;

	/* Must we remove all access from this file? */
	bool			zap_acls;

	/* Inode scanner to see if we can find the ftype from dirents */
	struct xchk_iscan	ftype_iscan;
	uint8_t			alleged_ftype;
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

/*
 * If this directory entry points to the scrub target inode, then the directory
 * we're scanning is the parent of the scrub target inode.
 */
STATIC int
xrep_dinode_findmode_dirent(
	struct xfs_scrub		*sc,
	struct xfs_inode		*dp,
	xfs_dir2_dataptr_t		dapos,
	const struct xfs_name		*name,
	xfs_ino_t			ino,
	void				*priv)
{
	struct xrep_inode		*ri = priv;
	int				error = 0;

	if (xchk_should_terminate(ri->sc, &error))
		return error;

	if (ino != sc->sm->sm_ino)
		return 0;

	/* Ignore garbage directory entry names. */
	if (name->len == 0 || !xfs_dir2_namecheck(name->name, name->len))
		return -EFSCORRUPTED;

	/* Don't pick up dot or dotdot entries; we only want child dirents. */
	if (xfs_dir2_samename(name, &xfs_name_dotdot) ||
	    xfs_dir2_samename(name, &xfs_name_dot))
		return 0;

	/*
	 * Uhoh, more than one parent for this inode and they don't agree on
	 * the file type?
	 */
	if (ri->alleged_ftype != XFS_DIR3_FT_UNKNOWN &&
	    ri->alleged_ftype != name->type) {
		trace_xrep_dinode_findmode_dirent_inval(ri->sc, dp, name->type,
				ri->alleged_ftype);
		return -EFSCORRUPTED;
	}

	/* We found a potential parent; remember the ftype. */
	trace_xrep_dinode_findmode_dirent(ri->sc, dp, name->type);
	ri->alleged_ftype = name->type;
	return 0;
}

/* Try to lock a directory, or wait a jiffy. */
static inline int
xrep_dinode_ilock_nowait(
	struct xfs_inode	*dp,
	unsigned int		lock_mode)
{
	if (xfs_ilock_nowait(dp, lock_mode))
		return true;

	schedule_timeout_killable(1);
	return false;
}

/*
 * Try to lock a directory to look for ftype hints.  Since we already hold the
 * AGI buffer, we cannot block waiting for the ILOCK because rename can take
 * the ILOCK and then try to lock AGIs.
 */
STATIC int
xrep_dinode_trylock_directory(
	struct xrep_inode	*ri,
	struct xfs_inode	*dp,
	unsigned int		*lock_modep)
{
	unsigned long		deadline = jiffies + msecs_to_jiffies(30000);
	unsigned int		lock_mode;
	int			error = 0;

	do {
		if (xchk_should_terminate(ri->sc, &error))
			return error;

		if (xfs_need_iread_extents(&dp->i_df))
			lock_mode = XFS_ILOCK_EXCL;
		else
			lock_mode = XFS_ILOCK_SHARED;

		if (xrep_dinode_ilock_nowait(dp, lock_mode)) {
			*lock_modep = lock_mode;
			return 0;
		}
	} while (!time_is_before_jiffies(deadline));
	return -EBUSY;
}

/*
 * If this is a directory, walk the dirents looking for any that point to the
 * scrub target inode.
 */
STATIC int
xrep_dinode_findmode_walk_directory(
	struct xrep_inode	*ri,
	struct xfs_inode	*dp)
{
	struct xfs_scrub	*sc = ri->sc;
	unsigned int		lock_mode;
	int			error = 0;

	/* Ignore temporary repair directories. */
	if (xrep_is_tempfile(dp))
		return 0;

	/*
	 * Scan the directory to see if there it contains an entry pointing to
	 * the directory that we are repairing.
	 */
	error = xrep_dinode_trylock_directory(ri, dp, &lock_mode);
	if (error)
		return error;

	/*
	 * If this directory is known to be sick, we cannot scan it reliably
	 * and must abort.
	 */
	if (xfs_inode_has_sickness(dp, XFS_SICK_INO_CORE |
				       XFS_SICK_INO_BMBTD |
				       XFS_SICK_INO_DIR)) {
		error = -EFSCORRUPTED;
		goto out_unlock;
	}

	/*
	 * We cannot complete our parent pointer scan if a directory looks as
	 * though it has been zapped by the inode record repair code.
	 */
	if (xchk_dir_looks_zapped(dp)) {
		error = -EBUSY;
		goto out_unlock;
	}

	error = xchk_dir_walk(sc, dp, xrep_dinode_findmode_dirent, ri);
	if (error)
		goto out_unlock;

out_unlock:
	xfs_iunlock(dp, lock_mode);
	return error;
}

/*
 * Try to find the mode of the inode being repaired by looking for directories
 * that point down to this file.
 */
STATIC int
xrep_dinode_find_mode(
	struct xrep_inode	*ri,
	uint16_t		*mode)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_inode	*dp;
	int			error;

	/* No ftype means we have no other metadata to consult. */
	if (!xfs_has_ftype(sc->mp)) {
		*mode = S_IFREG;
		return 0;
	}

	/*
	 * Scan all directories for parents that might point down to this
	 * inode.  Skip the inode being repaired during the scan since it
	 * cannot be its own parent.  Note that we still hold the AGI locked
	 * so there's a real possibility that _iscan_iter can return EBUSY.
	 */
	xchk_iscan_start(sc, 5000, 100, &ri->ftype_iscan);
	xchk_iscan_set_agi_trylock(&ri->ftype_iscan);
	ri->ftype_iscan.skip_ino = sc->sm->sm_ino;
	ri->alleged_ftype = XFS_DIR3_FT_UNKNOWN;
	while ((error = xchk_iscan_iter(&ri->ftype_iscan, &dp)) == 1) {
		if (S_ISDIR(VFS_I(dp)->i_mode))
			error = xrep_dinode_findmode_walk_directory(ri, dp);
		xchk_iscan_mark_visited(&ri->ftype_iscan, dp);
		xchk_irele(sc, dp);
		if (error < 0)
			break;
		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&ri->ftype_iscan);
	xchk_iscan_teardown(&ri->ftype_iscan);

	if (error == -EBUSY) {
		if (ri->alleged_ftype != XFS_DIR3_FT_UNKNOWN) {
			/*
			 * If we got an EBUSY after finding at least one
			 * dirent, that means the scan found an inode on the
			 * inactivation list and could not open it.  Accept the
			 * alleged ftype and install a new mode below.
			 */
			error = 0;
		} else if (!(sc->flags & XCHK_TRY_HARDER)) {
			/*
			 * Otherwise, retry the operation one time to see if
			 * the reason for the delay is an inode from the same
			 * cluster buffer waiting on the inactivation list.
			 */
			error = -EDEADLOCK;
		}
	}
	if (error)
		return error;

	/*
	 * Convert the discovered ftype into the file mode.  If all else fails,
	 * return S_IFREG.
	 */
	switch (ri->alleged_ftype) {
	case XFS_DIR3_FT_DIR:
		*mode = S_IFDIR;
		break;
	case XFS_DIR3_FT_WHT:
	case XFS_DIR3_FT_CHRDEV:
		*mode = S_IFCHR;
		break;
	case XFS_DIR3_FT_BLKDEV:
		*mode = S_IFBLK;
		break;
	case XFS_DIR3_FT_FIFO:
		*mode = S_IFIFO;
		break;
	case XFS_DIR3_FT_SOCK:
		*mode = S_IFSOCK;
		break;
	case XFS_DIR3_FT_SYMLINK:
		*mode = S_IFLNK;
		break;
	default:
		*mode = S_IFREG;
		break;
	}
	return 0;
}

/* Turn di_mode into /something/ recognizable.  Returns true if we succeed. */
STATIC int
xrep_dinode_mode(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip)
{
	struct xfs_scrub	*sc = ri->sc;
	uint16_t		mode = be16_to_cpu(dip->di_mode);
	int			error;

	trace_xrep_dinode_mode(sc, dip);

	if (mode == 0 || xfs_mode_to_ftype(mode) != XFS_DIR3_FT_UNKNOWN)
		return 0;

	/* Try to fix the mode.  If we cannot, then leave everything alone. */
	error = xrep_dinode_find_mode(ri, &mode);
	switch (error) {
	case -EINTR:
	case -EBUSY:
	case -EDEADLOCK:
		/* temporary failure or fatal signal */
		return error;
	case 0:
		/* found mode */
		break;
	default:
		/* some other error, assume S_IFREG */
		mode = S_IFREG;
		break;
	}

	/* bad mode, so we set it to a file that only root can read */
	dip->di_mode = cpu_to_be16(mode);
	dip->di_uid = 0;
	dip->di_gid = 0;
	ri->zap_acls = true;
	return 0;
}

/* Fix unused link count fields having nonzero values. */
STATIC void
xrep_dinode_nlinks(
	struct xfs_dinode	*dip)
{
	if (dip->di_version < 2) {
		dip->di_nlink = 0;
		return;
	}

	if (xfs_dinode_is_metadir(dip)) {
		if (be16_to_cpu(dip->di_metatype) >= XFS_METAFILE_MAX)
			dip->di_metatype = cpu_to_be16(XFS_METAFILE_UNKNOWN);
	} else {
		dip->di_metatype = 0;
	}
}

/* Fix any conflicting flags that the verifiers complain about. */
STATIC void
xrep_dinode_flags(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	bool			isrt)
{
	struct xfs_mount	*mp = sc->mp;
	uint64_t		flags2 = be64_to_cpu(dip->di_flags2);
	uint16_t		flags = be16_to_cpu(dip->di_flags);
	uint16_t		mode = be16_to_cpu(dip->di_mode);

	trace_xrep_dinode_flags(sc, dip);

	if (isrt)
		flags |= XFS_DIFLAG_REALTIME;
	else
		flags &= ~XFS_DIFLAG_REALTIME;

	/*
	 * For regular files on a reflink filesystem, set the REFLINK flag to
	 * protect shared extents.  A later stage will actually check those
	 * extents and clear the flag if possible.
	 */
	if (xfs_has_reflink(mp) && S_ISREG(mode))
		flags2 |= XFS_DIFLAG2_REFLINK;
	else
		flags2 &= ~(XFS_DIFLAG2_REFLINK | XFS_DIFLAG2_COWEXTSIZE);
	if (!xfs_has_bigtime(mp))
		flags2 &= ~XFS_DIFLAG2_BIGTIME;
	if (!xfs_has_large_extent_counts(mp))
		flags2 &= ~XFS_DIFLAG2_NREXT64;
	if (flags2 & XFS_DIFLAG2_NREXT64)
		dip->di_nrext64_pad = 0;
	else if (dip->di_version >= 3)
		dip->di_v3_pad = 0;

	if (flags2 & XFS_DIFLAG2_METADATA) {
		xfs_failaddr_t	fa;

		fa = xfs_dinode_verify_metadir(sc->mp, dip, mode, flags,
				flags2);
		if (fa)
			flags2 &= ~XFS_DIFLAG2_METADATA;
	}

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

/* Count extents and blocks for an inode given an rmap. */
STATIC int
xrep_dinode_walk_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_inode		*ri = priv;
	int				error = 0;

	if (xchk_should_terminate(ri->sc, &error))
		return error;

	/* We only care about this inode. */
	if (rec->rm_owner != ri->sc->sm->sm_ino)
		return 0;

	if (rec->rm_flags & XFS_RMAP_ATTR_FORK) {
		ri->attr_blocks += rec->rm_blockcount;
		if (!(rec->rm_flags & XFS_RMAP_BMBT_BLOCK))
			ri->attr_extents++;

		return 0;
	}

	ri->data_blocks += rec->rm_blockcount;
	if (!(rec->rm_flags & XFS_RMAP_BMBT_BLOCK))
		ri->data_extents++;

	return 0;
}

/* Count extents and blocks for an inode from all AG rmap data. */
STATIC int
xrep_dinode_count_ag_rmaps(
	struct xrep_inode	*ri,
	struct xfs_perag	*pag)
{
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agf;
	int			error;

	error = xfs_alloc_read_agf(pag, ri->sc->tp, 0, &agf);
	if (error)
		return error;

	cur = xfs_rmapbt_init_cursor(ri->sc->mp, ri->sc->tp, agf, pag);
	error = xfs_rmap_query_all(cur, xrep_dinode_walk_rmap, ri);
	xfs_btree_del_cursor(cur, error);
	xfs_trans_brelse(ri->sc->tp, agf);
	return error;
}

/* Count extents and blocks for an inode given an rt rmap. */
STATIC int
xrep_dinode_walk_rtrmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_inode		*ri = priv;
	int				error = 0;

	if (xchk_should_terminate(ri->sc, &error))
		return error;

	/* We only care about this inode. */
	if (rec->rm_owner != ri->sc->sm->sm_ino)
		return 0;

	if (rec->rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK))
		return -EFSCORRUPTED;

	ri->rt_blocks += rec->rm_blockcount;
	ri->rt_extents++;
	return 0;
}

/* Count extents and blocks for an inode from all realtime rmap data. */
STATIC int
xrep_dinode_count_rtgroup_rmaps(
	struct xrep_inode	*ri,
	struct xfs_rtgroup	*rtg)
{
	struct xfs_scrub	*sc = ri->sc;
	int			error;

	error = xrep_rtgroup_init(sc, rtg, &sc->sr, XFS_RTGLOCK_RMAP);
	if (error)
		return error;

	error = xfs_rmap_query_all(sc->sr.rmap_cur, xrep_dinode_walk_rtrmap,
			ri);
	xchk_rtgroup_btcur_free(&sc->sr);
	xchk_rtgroup_free(sc, &sc->sr);
	return error;
}

/* Count extents and blocks for a given inode from all rmap data. */
STATIC int
xrep_dinode_count_rmaps(
	struct xrep_inode	*ri)
{
	struct xfs_perag	*pag = NULL;
	struct xfs_rtgroup	*rtg = NULL;
	int			error;

	if (!xfs_has_rmapbt(ri->sc->mp))
		return -EOPNOTSUPP;

	while ((rtg = xfs_rtgroup_next(ri->sc->mp, rtg))) {
		error = xrep_dinode_count_rtgroup_rmaps(ri, rtg);
		if (error) {
			xfs_rtgroup_rele(rtg);
			return error;
		}
	}

	while ((pag = xfs_perag_next(ri->sc->mp, pag))) {
		error = xrep_dinode_count_ag_rmaps(ri, pag);
		if (error) {
			xfs_perag_rele(pag);
			return error;
		}
	}

	/* Can't have extents on both the rt and the data device. */
	if (ri->data_extents && ri->rt_extents)
		return -EFSCORRUPTED;

	trace_xrep_dinode_count_rmaps(ri->sc,
			ri->data_blocks, ri->rt_blocks, ri->attr_blocks,
			ri->data_extents, ri->rt_extents, ri->attr_extents);
	return 0;
}

/* Return true if this extents-format ifork looks like garbage. */
STATIC bool
xrep_dinode_bad_extents_fork(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	unsigned int		dfork_size,
	int			whichfork)
{
	struct xfs_bmbt_irec	new;
	struct xfs_bmbt_rec	*dp;
	xfs_extnum_t		nex;
	bool			isrt;
	unsigned int		i;

	nex = xfs_dfork_nextents(dip, whichfork);
	if (nex > dfork_size / sizeof(struct xfs_bmbt_rec))
		return true;

	dp = XFS_DFORK_PTR(dip, whichfork);

	isrt = dip->di_flags & cpu_to_be16(XFS_DIFLAG_REALTIME);
	for (i = 0; i < nex; i++, dp++) {
		xfs_failaddr_t	fa;

		xfs_bmbt_disk_get_all(dp, &new);
		fa = xfs_bmap_validate_extent_raw(sc->mp, isrt, whichfork,
				&new);
		if (fa)
			return true;
	}

	return false;
}

/* Return true if this btree-format ifork looks like garbage. */
STATIC bool
xrep_dinode_bad_bmbt_fork(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	unsigned int		dfork_size,
	int			whichfork)
{
	struct xfs_bmdr_block	*dfp;
	xfs_extnum_t		nex;
	unsigned int		i;
	unsigned int		dmxr;
	unsigned int		nrecs;
	unsigned int		level;

	nex = xfs_dfork_nextents(dip, whichfork);
	if (nex <= dfork_size / sizeof(struct xfs_bmbt_rec))
		return true;

	if (dfork_size < sizeof(struct xfs_bmdr_block))
		return true;

	dfp = XFS_DFORK_PTR(dip, whichfork);
	nrecs = be16_to_cpu(dfp->bb_numrecs);
	level = be16_to_cpu(dfp->bb_level);

	if (nrecs == 0 || xfs_bmdr_space_calc(nrecs) > dfork_size)
		return true;
	if (level == 0 || level >= XFS_BM_MAXLEVELS(sc->mp, whichfork))
		return true;

	dmxr = xfs_bmdr_maxrecs(dfork_size, 0);
	for (i = 1; i <= nrecs; i++) {
		struct xfs_bmbt_key	*fkp;
		xfs_bmbt_ptr_t		*fpp;
		xfs_fileoff_t		fileoff;
		xfs_fsblock_t		fsbno;

		fkp = xfs_bmdr_key_addr(dfp, i);
		fileoff = be64_to_cpu(fkp->br_startoff);
		if (!xfs_verify_fileoff(sc->mp, fileoff))
			return true;

		fpp = xfs_bmdr_ptr_addr(dfp, i, dmxr);
		fsbno = be64_to_cpu(*fpp);
		if (!xfs_verify_fsbno(sc->mp, fsbno))
			return true;
	}

	return false;
}

/* Return true if this rmap-format ifork looks like garbage. */
STATIC bool
xrep_dinode_bad_rtrmapbt_fork(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	unsigned int		dfork_size)
{
	struct xfs_rtrmap_root	*dfp;
	unsigned int		nrecs;
	unsigned int		level;

	if (dfork_size < sizeof(struct xfs_rtrmap_root))
		return true;

	dfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
	nrecs = be16_to_cpu(dfp->bb_numrecs);
	level = be16_to_cpu(dfp->bb_level);

	if (level > sc->mp->m_rtrmap_maxlevels)
		return true;
	if (xfs_rtrmap_droot_space_calc(level, nrecs) > dfork_size)
		return true;
	if (level > 0 && nrecs == 0)
		return true;

	return false;
}

/* Return true if this refcount-format ifork looks like garbage. */
STATIC bool
xrep_dinode_bad_rtrefcountbt_fork(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	unsigned int		dfork_size)
{
	struct xfs_rtrefcount_root *dfp;
	unsigned int		nrecs;
	unsigned int		level;

	if (dfork_size < sizeof(struct xfs_rtrefcount_root))
		return true;

	dfp = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
	nrecs = be16_to_cpu(dfp->bb_numrecs);
	level = be16_to_cpu(dfp->bb_level);

	if (level > sc->mp->m_rtrefc_maxlevels)
		return true;
	if (xfs_rtrefcount_droot_space_calc(level, nrecs) > dfork_size)
		return true;
	if (level > 0 && nrecs == 0)
		return true;

	return false;
}

/* Check a metadata-btree fork. */
STATIC bool
xrep_dinode_bad_metabt_fork(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	unsigned int		dfork_size,
	int			whichfork)
{
	if (whichfork != XFS_DATA_FORK)
		return true;

	switch (be16_to_cpu(dip->di_metatype)) {
	case XFS_METAFILE_RTRMAP:
		return xrep_dinode_bad_rtrmapbt_fork(sc, dip, dfork_size);
	case XFS_METAFILE_RTREFCOUNT:
		return xrep_dinode_bad_rtrefcountbt_fork(sc, dip, dfork_size);
	default:
		return true;
	}

	return false;
}

/*
 * Check the data fork for things that will fail the ifork verifiers or the
 * ifork formatters.
 */
STATIC bool
xrep_dinode_check_dfork(
	struct xfs_scrub	*sc,
	struct xfs_dinode	*dip,
	uint16_t		mode)
{
	void			*dfork_ptr;
	int64_t			data_size;
	unsigned int		fmt;
	unsigned int		dfork_size;

	/*
	 * Verifier functions take signed int64_t, so check for bogus negative
	 * values first.
	 */
	data_size = be64_to_cpu(dip->di_size);
	if (data_size < 0)
		return true;

	fmt = XFS_DFORK_FORMAT(dip, XFS_DATA_FORK);
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		if (fmt != XFS_DINODE_FMT_DEV)
			return true;
		break;
	case S_IFREG:
		switch (fmt) {
		case XFS_DINODE_FMT_LOCAL:
			return true;
		case XFS_DINODE_FMT_EXTENTS:
		case XFS_DINODE_FMT_BTREE:
		case XFS_DINODE_FMT_META_BTREE:
			break;
		default:
			return true;
		}
		break;
	case S_IFLNK:
	case S_IFDIR:
		switch (fmt) {
		case XFS_DINODE_FMT_LOCAL:
		case XFS_DINODE_FMT_EXTENTS:
		case XFS_DINODE_FMT_BTREE:
			break;
		default:
			return true;
		}
		break;
	default:
		return true;
	}

	dfork_size = XFS_DFORK_SIZE(dip, sc->mp, XFS_DATA_FORK);
	dfork_ptr = XFS_DFORK_PTR(dip, XFS_DATA_FORK);

	switch (fmt) {
	case XFS_DINODE_FMT_DEV:
		break;
	case XFS_DINODE_FMT_LOCAL:
		/* dir/symlink structure cannot be larger than the fork */
		if (data_size > dfork_size)
			return true;
		/* directory structure must pass verification. */
		if (S_ISDIR(mode) &&
		    xfs_dir2_sf_verify(sc->mp, dfork_ptr, data_size) != NULL)
			return true;
		/* symlink structure must pass verification. */
		if (S_ISLNK(mode) &&
		    xfs_symlink_shortform_verify(dfork_ptr, data_size) != NULL)
			return true;
		break;
	case XFS_DINODE_FMT_EXTENTS:
		if (xrep_dinode_bad_extents_fork(sc, dip, dfork_size,
				XFS_DATA_FORK))
			return true;
		break;
	case XFS_DINODE_FMT_BTREE:
		if (xrep_dinode_bad_bmbt_fork(sc, dip, dfork_size,
				XFS_DATA_FORK))
			return true;
		break;
	case XFS_DINODE_FMT_META_BTREE:
		if (xrep_dinode_bad_metabt_fork(sc, dip, dfork_size,
				XFS_DATA_FORK))
			return true;
		break;
	default:
		return true;
	}

	return false;
}

static void
xrep_dinode_set_data_nextents(
	struct xfs_dinode	*dip,
	xfs_extnum_t		nextents)
{
	if (xfs_dinode_has_large_extent_counts(dip))
		dip->di_big_nextents = cpu_to_be64(nextents);
	else
		dip->di_nextents = cpu_to_be32(nextents);
}

static void
xrep_dinode_set_attr_nextents(
	struct xfs_dinode	*dip,
	xfs_extnum_t		nextents)
{
	if (xfs_dinode_has_large_extent_counts(dip))
		dip->di_big_anextents = cpu_to_be32(nextents);
	else
		dip->di_anextents = cpu_to_be16(nextents);
}

/* Reset the data fork to something sane. */
STATIC void
xrep_dinode_zap_dfork(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip,
	uint16_t		mode)
{
	struct xfs_scrub	*sc = ri->sc;

	trace_xrep_dinode_zap_dfork(sc, dip);

	ri->ino_sick_mask |= XFS_SICK_INO_BMBTD_ZAPPED;

	xrep_dinode_set_data_nextents(dip, 0);
	ri->data_blocks = 0;
	ri->rt_blocks = 0;

	/* Special files always get reset to DEV */
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		dip->di_format = XFS_DINODE_FMT_DEV;
		dip->di_size = 0;
		return;
	}

	/*
	 * If we have data extents, reset to an empty map and hope the user
	 * will run the bmapbtd checker next.
	 */
	if (ri->data_extents || ri->rt_extents || S_ISREG(mode)) {
		dip->di_format = XFS_DINODE_FMT_EXTENTS;
		return;
	}

	/* Otherwise, reset the local format to the minimum. */
	switch (mode & S_IFMT) {
	case S_IFLNK:
		xrep_dinode_zap_symlink(ri, dip);
		break;
	case S_IFDIR:
		xrep_dinode_zap_dir(ri, dip);
		break;
	}
}

/*
 * Check the attr fork for things that will fail the ifork verifiers or the
 * ifork formatters.
 */
STATIC bool
xrep_dinode_check_afork(
	struct xfs_scrub		*sc,
	struct xfs_dinode		*dip)
{
	struct xfs_attr_sf_hdr		*afork_ptr;
	size_t				attr_size;
	unsigned int			afork_size;

	if (XFS_DFORK_BOFF(dip) == 0)
		return dip->di_aformat != XFS_DINODE_FMT_EXTENTS ||
		       xfs_dfork_attr_extents(dip) != 0;

	afork_size = XFS_DFORK_SIZE(dip, sc->mp, XFS_ATTR_FORK);
	afork_ptr = XFS_DFORK_PTR(dip, XFS_ATTR_FORK);

	switch (XFS_DFORK_FORMAT(dip, XFS_ATTR_FORK)) {
	case XFS_DINODE_FMT_LOCAL:
		/* Fork has to be large enough to extract the xattr size. */
		if (afork_size < sizeof(struct xfs_attr_sf_hdr))
			return true;

		/* xattr structure cannot be larger than the fork */
		attr_size = be16_to_cpu(afork_ptr->totsize);
		if (attr_size > afork_size)
			return true;

		/* xattr structure must pass verification. */
		return xfs_attr_shortform_verify(afork_ptr, attr_size) != NULL;
	case XFS_DINODE_FMT_EXTENTS:
		if (xrep_dinode_bad_extents_fork(sc, dip, afork_size,
					XFS_ATTR_FORK))
			return true;
		break;
	case XFS_DINODE_FMT_BTREE:
		if (xrep_dinode_bad_bmbt_fork(sc, dip, afork_size,
					XFS_ATTR_FORK))
			return true;
		break;
	case XFS_DINODE_FMT_META_BTREE:
		if (xrep_dinode_bad_metabt_fork(sc, dip, afork_size,
					XFS_ATTR_FORK))
			return true;
		break;
	default:
		return true;
	}

	return false;
}

/*
 * Reset the attr fork to empty.  Since the attr fork could have contained
 * ACLs, make the file readable only by root.
 */
STATIC void
xrep_dinode_zap_afork(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip,
	uint16_t		mode)
{
	struct xfs_scrub	*sc = ri->sc;

	trace_xrep_dinode_zap_afork(sc, dip);

	ri->ino_sick_mask |= XFS_SICK_INO_BMBTA_ZAPPED;

	dip->di_aformat = XFS_DINODE_FMT_EXTENTS;
	xrep_dinode_set_attr_nextents(dip, 0);
	ri->attr_blocks = 0;

	/*
	 * If the data fork is in btree format, removing the attr fork entirely
	 * might cause verifier failures if the next level down in the bmbt
	 * could now fit in the data fork area.
	 */
	if (dip->di_format != XFS_DINODE_FMT_BTREE)
		dip->di_forkoff = 0;
	dip->di_mode = cpu_to_be16(mode & ~0777);
	dip->di_uid = 0;
	dip->di_gid = 0;
}

/* Make sure the fork offset is a sensible value. */
STATIC void
xrep_dinode_ensure_forkoff(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip,
	uint16_t		mode)
{
	struct xfs_bmdr_block	*bmdr;
	struct xfs_rtrmap_root	*rmdr;
	struct xfs_rtrefcount_root *rcdr;
	struct xfs_scrub	*sc = ri->sc;
	xfs_extnum_t		attr_extents, data_extents;
	size_t			bmdr_minsz = xfs_bmdr_space_calc(1);
	unsigned int		lit_sz = XFS_LITINO(sc->mp);
	unsigned int		afork_min, dfork_min;

	trace_xrep_dinode_ensure_forkoff(sc, dip);

	/*
	 * Before calling this function, xrep_dinode_core ensured that both
	 * forks actually fit inside their respective literal areas.  If this
	 * was not the case, the fork was reset to FMT_EXTENTS with zero
	 * records.  If the rmapbt scan found attr or data fork blocks, this
	 * will be noted in the dinode_stats, and we must leave enough room
	 * for the bmap repair code to reconstruct the mapping structure.
	 *
	 * First, compute the minimum space required for the attr fork.
	 */
	switch (dip->di_aformat) {
	case XFS_DINODE_FMT_LOCAL:
		/*
		 * If we still have a shortform xattr structure at all, that
		 * means the attr fork area was exactly large enough to fit
		 * the sf structure.
		 */
		afork_min = XFS_DFORK_SIZE(dip, sc->mp, XFS_ATTR_FORK);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		attr_extents = xfs_dfork_attr_extents(dip);
		if (attr_extents) {
			/*
			 * We must maintain sufficient space to hold the entire
			 * extent map array in the data fork.  Note that we
			 * previously zapped the fork if it had no chance of
			 * fitting in the inode.
			 */
			afork_min = sizeof(struct xfs_bmbt_rec) * attr_extents;
		} else if (ri->attr_extents > 0) {
			/*
			 * The attr fork thinks it has zero extents, but we
			 * found some xattr extents.  We need to leave enough
			 * empty space here so that the incore attr fork will
			 * get created (and hence trigger the attr fork bmap
			 * repairer).
			 */
			afork_min = bmdr_minsz;
		} else {
			/* No extents on disk or found in rmapbt. */
			afork_min = 0;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		/* Must have space for btree header and key/pointers. */
		bmdr = XFS_DFORK_PTR(dip, XFS_ATTR_FORK);
		afork_min = xfs_bmap_broot_space(sc->mp, bmdr);
		break;
	default:
		/* We should never see any other formats. */
		afork_min = 0;
		break;
	}

	/* Compute the minimum space required for the data fork. */
	switch (dip->di_format) {
	case XFS_DINODE_FMT_DEV:
		dfork_min = sizeof(__be32);
		break;
	case XFS_DINODE_FMT_UUID:
		dfork_min = sizeof(uuid_t);
		break;
	case XFS_DINODE_FMT_LOCAL:
		/*
		 * If we still have a shortform data fork at all, that means
		 * the data fork area was large enough to fit whatever was in
		 * there.
		 */
		dfork_min = be64_to_cpu(dip->di_size);
		break;
	case XFS_DINODE_FMT_EXTENTS:
		data_extents = xfs_dfork_data_extents(dip);
		if (data_extents) {
			/*
			 * We must maintain sufficient space to hold the entire
			 * extent map array in the data fork.  Note that we
			 * previously zapped the fork if it had no chance of
			 * fitting in the inode.
			 */
			dfork_min = sizeof(struct xfs_bmbt_rec) * data_extents;
		} else if (ri->data_extents > 0 || ri->rt_extents > 0) {
			/*
			 * The data fork thinks it has zero extents, but we
			 * found some data extents.  We need to leave enough
			 * empty space here so that the data fork bmap repair
			 * will recover the mappings.
			 */
			dfork_min = bmdr_minsz;
		} else {
			/* No extents on disk or found in rmapbt. */
			dfork_min = 0;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		/* Must have space for btree header and key/pointers. */
		bmdr = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
		dfork_min = xfs_bmap_broot_space(sc->mp, bmdr);
		break;
	case XFS_DINODE_FMT_META_BTREE:
		switch (be16_to_cpu(dip->di_metatype)) {
		case XFS_METAFILE_RTRMAP:
			rmdr = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
			dfork_min = xfs_rtrmap_broot_space(sc->mp, rmdr);
			break;
		case XFS_METAFILE_RTREFCOUNT:
			rcdr = XFS_DFORK_PTR(dip, XFS_DATA_FORK);
			dfork_min = xfs_rtrefcount_broot_space(sc->mp, rcdr);
			break;
		default:
			dfork_min = 0;
			break;
		}
		break;
	default:
		dfork_min = 0;
		break;
	}

	/*
	 * Round all values up to the nearest 8 bytes, because that is the
	 * precision of di_forkoff.
	 */
	afork_min = roundup(afork_min, 8);
	dfork_min = roundup(dfork_min, 8);
	bmdr_minsz = roundup(bmdr_minsz, 8);

	ASSERT(dfork_min <= lit_sz);
	ASSERT(afork_min <= lit_sz);

	/*
	 * If the data fork was zapped and we don't have enough space for the
	 * recovery fork, move the attr fork up.
	 */
	if (dip->di_format == XFS_DINODE_FMT_EXTENTS &&
	    xfs_dfork_data_extents(dip) == 0 &&
	    (ri->data_extents > 0 || ri->rt_extents > 0) &&
	    bmdr_minsz > XFS_DFORK_DSIZE(dip, sc->mp)) {
		if (bmdr_minsz + afork_min > lit_sz) {
			/*
			 * The attr for and the stub fork we need to recover
			 * the data fork won't both fit.  Zap the attr fork.
			 */
			xrep_dinode_zap_afork(ri, dip, mode);
			afork_min = bmdr_minsz;
		} else {
			void	*before, *after;

			/* Otherwise, just slide the attr fork up. */
			before = XFS_DFORK_APTR(dip);
			dip->di_forkoff = bmdr_minsz >> 3;
			after = XFS_DFORK_APTR(dip);
			memmove(after, before, XFS_DFORK_ASIZE(dip, sc->mp));
		}
	}

	/*
	 * If the attr fork was zapped and we don't have enough space for the
	 * recovery fork, move the attr fork down.
	 */
	if (dip->di_aformat == XFS_DINODE_FMT_EXTENTS &&
	    xfs_dfork_attr_extents(dip) == 0 &&
	    ri->attr_extents > 0 &&
	    bmdr_minsz > XFS_DFORK_ASIZE(dip, sc->mp)) {
		if (dip->di_format == XFS_DINODE_FMT_BTREE) {
			/*
			 * If the data fork is in btree format then we can't
			 * adjust forkoff because that runs the risk of
			 * violating the extents/btree format transition rules.
			 */
		} else if (bmdr_minsz + dfork_min > lit_sz) {
			/*
			 * If we can't move the attr fork, too bad, we lose the
			 * attr fork and leak its blocks.
			 */
			xrep_dinode_zap_afork(ri, dip, mode);
		} else {
			/*
			 * Otherwise, just slide the attr fork down.  The attr
			 * fork is empty, so we don't have any old contents to
			 * move here.
			 */
			dip->di_forkoff = (lit_sz - bmdr_minsz) >> 3;
		}
	}
}

/*
 * Zap the data/attr forks if we spot anything that isn't going to pass the
 * ifork verifiers or the ifork formatters, because we need to get the inode
 * into good enough shape that the higher level repair functions can run.
 */
STATIC void
xrep_dinode_zap_forks(
	struct xrep_inode	*ri,
	struct xfs_dinode	*dip)
{
	struct xfs_scrub	*sc = ri->sc;
	xfs_extnum_t		data_extents;
	xfs_extnum_t		attr_extents;
	xfs_filblks_t		nblocks;
	uint16_t		mode;
	bool			zap_datafork = false;
	bool			zap_attrfork = ri->zap_acls;

	trace_xrep_dinode_zap_forks(sc, dip);

	mode = be16_to_cpu(dip->di_mode);

	data_extents = xfs_dfork_data_extents(dip);
	attr_extents = xfs_dfork_attr_extents(dip);
	nblocks = be64_to_cpu(dip->di_nblocks);

	/* Inode counters don't make sense? */
	if (data_extents > nblocks)
		zap_datafork = true;
	if (attr_extents > nblocks)
		zap_attrfork = true;
	if (data_extents + attr_extents > nblocks)
		zap_datafork = zap_attrfork = true;

	if (!zap_datafork)
		zap_datafork = xrep_dinode_check_dfork(sc, dip, mode);
	if (!zap_attrfork)
		zap_attrfork = xrep_dinode_check_afork(sc, dip);

	/* Zap whatever's bad. */
	if (zap_attrfork)
		xrep_dinode_zap_afork(ri, dip, mode);
	if (zap_datafork)
		xrep_dinode_zap_dfork(ri, dip, mode);
	xrep_dinode_ensure_forkoff(ri, dip, mode);

	/*
	 * Zero di_nblocks if we don't have any extents at all to satisfy the
	 * buffer verifier.
	 */
	data_extents = xfs_dfork_data_extents(dip);
	attr_extents = xfs_dfork_attr_extents(dip);
	if (data_extents + attr_extents == 0)
		dip->di_nblocks = 0;
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

	/* Figure out what this inode had mapped in both forks. */
	error = xrep_dinode_count_rmaps(ri);
	if (error)
		return error;

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
	iget_error = xrep_dinode_mode(ri, dip);
	if (iget_error)
		goto write;
	xrep_dinode_nlinks(dip);
	xrep_dinode_flags(sc, dip, ri->rt_extents > 0);
	xrep_dinode_size(ri, dip);
	xrep_dinode_extsize_hints(sc, dip);
	xrep_dinode_zap_forks(ri, dip);

write:
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
	if (!iget_error)
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
	error = xchk_inode_count_blocks(sc, XFS_DATA_FORK, &nextents, &count);
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
		error = xchk_inode_count_blocks(sc, XFS_ATTR_FORK, &nextents,
				&acount);
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

/* Ensure this file has an attr fork if it needs to hold a parent pointer. */
STATIC int
xrep_inode_pptr(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	struct inode		*inode = VFS_I(ip);

	if (!xfs_has_parent(mp))
		return 0;

	/*
	 * Unlinked inodes that cannot be added to the directory tree will not
	 * have a parent pointer.
	 */
	if (inode->i_nlink == 0 && !(inode->i_state & I_LINKABLE))
		return 0;

	/* Children of the superblock do not have parent pointers. */
	if (xchk_inode_is_sb_rooted(ip))
		return 0;

	/* Inode already has an attr fork; no further work possible here. */
	if (xfs_inode_has_attr_fork(ip))
		return 0;

	return xfs_bmap_add_attrfork(sc->tp, ip,
			sizeof(struct xfs_attr_sf_hdr), true);
}

/* Fix COW extent size hint problems. */
STATIC void
xrep_inode_cowextsize(
	struct xfs_scrub	*sc)
{
	/* Fix misaligned CoW extent size hints on a directory. */
	if ((sc->ip->i_diflags & XFS_DIFLAG_RTINHERIT) &&
	    (sc->ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE) &&
	    sc->ip->i_extsize % sc->mp->m_sb.sb_rextsize > 0) {
		sc->ip->i_cowextsize = 0;
		sc->ip->i_diflags2 &= ~XFS_DIFLAG2_COWEXTSIZE;
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
	error = xrep_inode_pptr(sc);
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
	xrep_inode_cowextsize(sc);

	trace_xrep_inode_fixed(sc);
	xfs_trans_log_inode(sc->tp, sc->ip, XFS_ILOG_CORE);
	return xrep_roll_trans(sc);
}

/*
 * Make sure this inode's unlinked list pointers are consistent with its
 * link count.
 */
STATIC int
xrep_inode_unlinked(
	struct xfs_scrub	*sc)
{
	unsigned int		nlink = VFS_I(sc->ip)->i_nlink;
	int			error;

	/*
	 * If this inode is linked from the directory tree and on the unlinked
	 * list, remove it from the unlinked list.
	 */
	if (nlink > 0 && xfs_inode_on_unlinked_list(sc->ip)) {
		struct xfs_perag	*pag;
		int			error;

		pag = xfs_perag_get(sc->mp,
				XFS_INO_TO_AGNO(sc->mp, sc->ip->i_ino));
		error = xfs_iunlink_remove(sc->tp, pag, sc->ip);
		xfs_perag_put(pag);
		if (error)
			return error;
	}

	/*
	 * If this inode is not linked from the directory tree yet not on the
	 * unlinked list, put it on the unlinked list.
	 */
	if (nlink == 0 && !xfs_inode_on_unlinked_list(sc->ip)) {
		error = xfs_iunlink(sc->tp, sc->ip);
		if (error)
			return error;
	}

	return 0;
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
		if (error == -EBUSY) {
			/*
			 * Directory scan to recover inode mode encountered a
			 * busy inode, so we did not continue repairing things.
			 */
			return 0;
		}
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

	/* Reconnect incore unlinked list */
	error = xrep_inode_unlinked(sc);
	if (error)
		return error;

	return xrep_defer_finish(sc);
}
