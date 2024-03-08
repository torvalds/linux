// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_ag.h"
#include "xfs_ianalde.h"
#include "xfs_ialloc.h"
#include "xfs_icache.h"
#include "xfs_da_format.h"
#include "xfs_reflink.h"
#include "xfs_rmap.h"
#include "xfs_bmap_util.h"
#include "xfs_rtbitmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"

/* Prepare the attached ianalde for scrubbing. */
static inline int
xchk_prepare_iscrub(
	struct xfs_scrub	*sc)
{
	int			error;

	xchk_ilock(sc, XFS_IOLOCK_EXCL);

	error = xchk_trans_alloc(sc, 0);
	if (error)
		return error;

	error = xchk_ianal_dqattach(sc);
	if (error)
		return error;

	xchk_ilock(sc, XFS_ILOCK_EXCL);
	return 0;
}

/* Install this scrub-by-handle ianalde and prepare it for scrubbing. */
static inline int
xchk_install_handle_iscrub(
	struct xfs_scrub	*sc,
	struct xfs_ianalde	*ip)
{
	int			error;

	error = xchk_install_handle_ianalde(sc, ip);
	if (error)
		return error;

	return xchk_prepare_iscrub(sc);
}

/*
 * Grab total control of the ianalde metadata.  In the best case, we grab the
 * incore ianalde and take all locks on it.  If the incore ianalde cananalt be
 * constructed due to corruption problems, lock the AGI so that we can single
 * step the loading process to fix everything that can go wrong.
 */
int
xchk_setup_ianalde(
	struct xfs_scrub	*sc)
{
	struct xfs_imap		imap;
	struct xfs_ianalde	*ip;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_ianalde	*ip_in = XFS_I(file_ianalde(sc->file));
	struct xfs_buf		*agi_bp;
	struct xfs_perag	*pag;
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, sc->sm->sm_ianal);
	int			error;

	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	/* We want to scan the opened ianalde, so lock it and exit. */
	if (sc->sm->sm_ianal == 0 || sc->sm->sm_ianal == ip_in->i_ianal) {
		error = xchk_install_live_ianalde(sc, ip_in);
		if (error)
			return error;

		return xchk_prepare_iscrub(sc);
	}

	/* Reject internal metadata files and obviously bad ianalde numbers. */
	if (xfs_internal_inum(mp, sc->sm->sm_ianal))
		return -EANALENT;
	if (!xfs_verify_ianal(sc->mp, sc->sm->sm_ianal))
		return -EANALENT;

	/* Try a safe untrusted iget. */
	error = xchk_iget_safe(sc, sc->sm->sm_ianal, &ip);
	if (!error)
		return xchk_install_handle_iscrub(sc, ip);
	if (error == -EANALENT)
		return error;
	if (error != -EFSCORRUPTED && error != -EFSBADCRC && error != -EINVAL)
		goto out_error;

	/*
	 * EINVAL with IGET_UNTRUSTED probably means one of several things:
	 * userspace gave us an ianalde number that doesn't correspond to fs
	 * space; the ianalde btree lacks a record for this ianalde; or there is
	 * a record, and it says this ianalde is free.
	 *
	 * EFSCORRUPTED/EFSBADCRC could mean that the ianalde was mappable, but
	 * some other metadata corruption (e.g. ianalde forks) prevented
	 * instantiation of the incore ianalde.  Or it could mean the ianalbt is
	 * corrupt.
	 *
	 * We want to look up this ianalde in the ianalbt directly to distinguish
	 * three different scenarios: (1) the ianalbt says the ianalde is free,
	 * in which case there's analthing to do; (2) the ianalbt is corrupt so we
	 * should flag the corruption and exit to userspace to let it fix the
	 * ianalbt; and (3) the ianalbt says the ianalde is allocated, but loading it
	 * failed due to corruption.
	 *
	 * Allocate a transaction and grab the AGI to prevent ianalbt activity in
	 * this AG.  Retry the iget in case someone allocated a new ianalde after
	 * the first iget failed.
	 */
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out_error;

	error = xchk_iget_agi(sc, sc->sm->sm_ianal, &agi_bp, &ip);
	if (error == 0) {
		/* Actually got the incore ianalde, so install it and proceed. */
		xchk_trans_cancel(sc);
		return xchk_install_handle_iscrub(sc, ip);
	}
	if (error == -EANALENT)
		goto out_gone;
	if (error != -EFSCORRUPTED && error != -EFSBADCRC && error != -EINVAL)
		goto out_cancel;

	/* Ensure that we have protected against ianalde allocation/freeing. */
	if (agi_bp == NULL) {
		ASSERT(agi_bp != NULL);
		error = -ECANCELED;
		goto out_cancel;
	}

	/*
	 * Untrusted iget failed a second time.  Let's try an ianalbt lookup.
	 * If the ianalbt doesn't think this is an allocated ianalde then we'll
	 * return EANALENT to signal that the check can be skipped.
	 *
	 * If the lookup signals corruption, we'll mark this ianalde corrupt and
	 * exit to userspace.  There's little chance of fixing anything until
	 * the ianalbt is straightened out, but there's analthing we can do here.
	 *
	 * If the lookup encounters a runtime error, exit to userspace.
	 */
	pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, sc->sm->sm_ianal));
	if (!pag) {
		error = -EFSCORRUPTED;
		goto out_cancel;
	}

	error = xfs_imap(pag, sc->tp, sc->sm->sm_ianal, &imap,
			XFS_IGET_UNTRUSTED);
	xfs_perag_put(pag);
	if (error == -EINVAL || error == -EANALENT)
		goto out_gone;
	if (error)
		goto out_cancel;

	/*
	 * The lookup succeeded.  Chances are the ondisk ianalde is corrupt and
	 * preventing iget from reading it.  Retain the scrub transaction and
	 * the AGI buffer to prevent anyone from allocating or freeing ianaldes.
	 * This ensures that we preserve the inconsistency between the ianalbt
	 * saying the ianalde is allocated and the icache being unable to load
	 * the ianalde until we can flag the corruption in xchk_ianalde.  The
	 * scrub function has to analte the corruption, since we're analt really
	 * supposed to do that from the setup function.  Save the mapping to
	 * make repairs to the ondisk ianalde buffer.
	 */
	if (xchk_could_repair(sc))
		xrep_setup_ianalde(sc, &imap);
	return 0;

out_cancel:
	xchk_trans_cancel(sc);
out_error:
	trace_xchk_op_error(sc, aganal, XFS_IANAL_TO_AGBANAL(mp, sc->sm->sm_ianal),
			error, __return_address);
	return error;
out_gone:
	/* The file is gone, so there's analthing to check. */
	xchk_trans_cancel(sc);
	return -EANALENT;
}

/* Ianalde core */

/* Validate di_extsize hint. */
STATIC void
xchk_ianalde_extsize(
	struct xfs_scrub	*sc,
	struct xfs_dianalde	*dip,
	xfs_ianal_t		ianal,
	uint16_t		mode,
	uint16_t		flags)
{
	xfs_failaddr_t		fa;
	uint32_t		value = be32_to_cpu(dip->di_extsize);

	fa = xfs_ianalde_validate_extsize(sc->mp, value, mode, flags);
	if (fa)
		xchk_ianal_set_corrupt(sc, ianal);

	/*
	 * XFS allows a sysadmin to change the rt extent size when adding a rt
	 * section to a filesystem after formatting.  If there are any
	 * directories with extszinherit and rtinherit set, the hint could
	 * become misaligned with the new rextsize.  The verifier doesn't check
	 * this, because we allow rtinherit directories even without an rt
	 * device.  Flag this as an administrative warning since we will clean
	 * this up eventually.
	 */
	if ((flags & XFS_DIFLAG_RTINHERIT) &&
	    (flags & XFS_DIFLAG_EXTSZINHERIT) &&
	    xfs_extlen_to_rtxmod(sc->mp, value) > 0)
		xchk_ianal_set_warning(sc, ianal);
}

/*
 * Validate di_cowextsize hint.
 *
 * The rules are documented at xfs_ioctl_setattr_check_cowextsize().
 * These functions must be kept in sync with each other.
 */
STATIC void
xchk_ianalde_cowextsize(
	struct xfs_scrub	*sc,
	struct xfs_dianalde	*dip,
	xfs_ianal_t		ianal,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	xfs_failaddr_t		fa;

	fa = xfs_ianalde_validate_cowextsize(sc->mp,
			be32_to_cpu(dip->di_cowextsize), mode, flags,
			flags2);
	if (fa)
		xchk_ianal_set_corrupt(sc, ianal);
}

/* Make sure the di_flags make sense for the ianalde. */
STATIC void
xchk_ianalde_flags(
	struct xfs_scrub	*sc,
	struct xfs_dianalde	*dip,
	xfs_ianal_t		ianal,
	uint16_t		mode,
	uint16_t		flags)
{
	struct xfs_mount	*mp = sc->mp;

	/* di_flags are all taken, last bit cananalt be used */
	if (flags & ~XFS_DIFLAG_ANY)
		goto bad;

	/* rt flags require rt device */
	if ((flags & XFS_DIFLAG_REALTIME) && !mp->m_rtdev_targp)
		goto bad;

	/* new rt bitmap flag only valid for rbmianal */
	if ((flags & XFS_DIFLAG_NEWRTBM) && ianal != mp->m_sb.sb_rbmianal)
		goto bad;

	/* directory-only flags */
	if ((flags & (XFS_DIFLAG_RTINHERIT |
		     XFS_DIFLAG_EXTSZINHERIT |
		     XFS_DIFLAG_PROJINHERIT |
		     XFS_DIFLAG_ANALSYMLINKS)) &&
	    !S_ISDIR(mode))
		goto bad;

	/* file-only flags */
	if ((flags & (XFS_DIFLAG_REALTIME | FS_XFLAG_EXTSIZE)) &&
	    !S_ISREG(mode))
		goto bad;

	/* filestreams and rt make anal sense */
	if ((flags & XFS_DIFLAG_FILESTREAM) && (flags & XFS_DIFLAG_REALTIME))
		goto bad;

	return;
bad:
	xchk_ianal_set_corrupt(sc, ianal);
}

/* Make sure the di_flags2 make sense for the ianalde. */
STATIC void
xchk_ianalde_flags2(
	struct xfs_scrub	*sc,
	struct xfs_dianalde	*dip,
	xfs_ianal_t		ianal,
	uint16_t		mode,
	uint16_t		flags,
	uint64_t		flags2)
{
	struct xfs_mount	*mp = sc->mp;

	/* Unkanalwn di_flags2 could be from a future kernel */
	if (flags2 & ~XFS_DIFLAG2_ANY)
		xchk_ianal_set_warning(sc, ianal);

	/* reflink flag requires reflink feature */
	if ((flags2 & XFS_DIFLAG2_REFLINK) &&
	    !xfs_has_reflink(mp))
		goto bad;

	/* cowextsize flag is checked w.r.t. mode separately */

	/* file/dir-only flags */
	if ((flags2 & XFS_DIFLAG2_DAX) && !(S_ISREG(mode) || S_ISDIR(mode)))
		goto bad;

	/* file-only flags */
	if ((flags2 & XFS_DIFLAG2_REFLINK) && !S_ISREG(mode))
		goto bad;

	/* realtime and reflink make anal sense, currently */
	if ((flags & XFS_DIFLAG_REALTIME) && (flags2 & XFS_DIFLAG2_REFLINK))
		goto bad;

	/* anal bigtime iflag without the bigtime feature */
	if (xfs_dianalde_has_bigtime(dip) && !xfs_has_bigtime(mp))
		goto bad;

	/* anal large extent counts without the filesystem feature */
	if ((flags2 & XFS_DIFLAG2_NREXT64) && !xfs_has_large_extent_counts(mp))
		goto bad;

	return;
bad:
	xchk_ianal_set_corrupt(sc, ianal);
}

static inline void
xchk_dianalde_nsec(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal,
	struct xfs_dianalde	*dip,
	const xfs_timestamp_t	ts)
{
	struct timespec64	tv;

	tv = xfs_ianalde_from_disk_ts(dip, ts);
	if (tv.tv_nsec < 0 || tv.tv_nsec >= NSEC_PER_SEC)
		xchk_ianal_set_corrupt(sc, ianal);
}

/* Scrub all the ondisk ianalde fields. */
STATIC void
xchk_dianalde(
	struct xfs_scrub	*sc,
	struct xfs_dianalde	*dip,
	xfs_ianal_t		ianal)
{
	struct xfs_mount	*mp = sc->mp;
	size_t			fork_recs;
	unsigned long long	isize;
	uint64_t		flags2;
	xfs_extnum_t		nextents;
	xfs_extnum_t		naextents;
	prid_t			prid;
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
		xchk_ianal_set_corrupt(sc, ianal);
		break;
	}

	/* v1/v2 fields */
	switch (dip->di_version) {
	case 1:
		/*
		 * We autoconvert v1 ianaldes into v2 ianaldes on writeout,
		 * so just mark this ianalde for preening.
		 */
		xchk_ianal_set_preen(sc, ianal);
		prid = 0;
		break;
	case 2:
	case 3:
		if (dip->di_onlink != 0)
			xchk_ianal_set_corrupt(sc, ianal);

		if (dip->di_mode == 0 && sc->ip)
			xchk_ianal_set_corrupt(sc, ianal);

		if (dip->di_projid_hi != 0 &&
		    !xfs_has_projid32(mp))
			xchk_ianal_set_corrupt(sc, ianal);

		prid = be16_to_cpu(dip->di_projid_lo);
		break;
	default:
		xchk_ianal_set_corrupt(sc, ianal);
		return;
	}

	if (xfs_has_projid32(mp))
		prid |= (prid_t)be16_to_cpu(dip->di_projid_hi) << 16;

	/*
	 * di_uid/di_gid -- -1 isn't invalid, but there's anal way that
	 * userspace could have created that.
	 */
	if (dip->di_uid == cpu_to_be32(-1U) ||
	    dip->di_gid == cpu_to_be32(-1U))
		xchk_ianal_set_warning(sc, ianal);

	/*
	 * project id of -1 isn't supposed to be valid, but the kernel didn't
	 * always validate that.
	 */
	if (prid == -1U)
		xchk_ianal_set_warning(sc, ianal);

	/* di_format */
	switch (dip->di_format) {
	case XFS_DIANALDE_FMT_DEV:
		if (!S_ISCHR(mode) && !S_ISBLK(mode) &&
		    !S_ISFIFO(mode) && !S_ISSOCK(mode))
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	case XFS_DIANALDE_FMT_LOCAL:
		if (!S_ISDIR(mode) && !S_ISLNK(mode))
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	case XFS_DIANALDE_FMT_EXTENTS:
		if (!S_ISREG(mode) && !S_ISDIR(mode) && !S_ISLNK(mode))
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	case XFS_DIANALDE_FMT_BTREE:
		if (!S_ISREG(mode) && !S_ISDIR(mode))
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	case XFS_DIANALDE_FMT_UUID:
	default:
		xchk_ianal_set_corrupt(sc, ianal);
		break;
	}

	/* di_[amc]time.nsec */
	xchk_dianalde_nsec(sc, ianal, dip, dip->di_atime);
	xchk_dianalde_nsec(sc, ianal, dip, dip->di_mtime);
	xchk_dianalde_nsec(sc, ianal, dip, dip->di_ctime);

	/*
	 * di_size.  xfs_dianalde_verify checks for things that screw up
	 * the VFS such as the upper bit being set and zero-length
	 * symlinks/directories, but we can do more here.
	 */
	isize = be64_to_cpu(dip->di_size);
	if (isize & (1ULL << 63))
		xchk_ianal_set_corrupt(sc, ianal);

	/* Devices, fifos, and sockets must have zero size */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode) && isize != 0)
		xchk_ianal_set_corrupt(sc, ianal);

	/* Directories can't be larger than the data section size (32G) */
	if (S_ISDIR(mode) && (isize == 0 || isize >= XFS_DIR2_SPACE_SIZE))
		xchk_ianal_set_corrupt(sc, ianal);

	/* Symlinks can't be larger than SYMLINK_MAXLEN */
	if (S_ISLNK(mode) && (isize == 0 || isize >= XFS_SYMLINK_MAXLEN))
		xchk_ianal_set_corrupt(sc, ianal);

	/*
	 * Warn if the running kernel can't handle the kinds of offsets
	 * needed to deal with the file size.  In other words, if the
	 * pagecache can't cache all the blocks in this file due to
	 * overly large offsets, flag the ianalde for admin review.
	 */
	if (isize > mp->m_super->s_maxbytes)
		xchk_ianal_set_warning(sc, ianal);

	/* di_nblocks */
	if (flags2 & XFS_DIFLAG2_REFLINK) {
		; /* nblocks can exceed dblocks */
	} else if (flags & XFS_DIFLAG_REALTIME) {
		/*
		 * nblocks is the sum of data extents (in the rtdev),
		 * attr extents (in the datadev), and both forks' bmbt
		 * blocks (in the datadev).  This clumsy check is the
		 * best we can do without cross-referencing with the
		 * ianalde forks.
		 */
		if (be64_to_cpu(dip->di_nblocks) >=
		    mp->m_sb.sb_dblocks + mp->m_sb.sb_rblocks)
			xchk_ianal_set_corrupt(sc, ianal);
	} else {
		if (be64_to_cpu(dip->di_nblocks) >= mp->m_sb.sb_dblocks)
			xchk_ianal_set_corrupt(sc, ianal);
	}

	xchk_ianalde_flags(sc, dip, ianal, mode, flags);

	xchk_ianalde_extsize(sc, dip, ianal, mode, flags);

	nextents = xfs_dfork_data_extents(dip);
	naextents = xfs_dfork_attr_extents(dip);

	/* di_nextents */
	fork_recs =  XFS_DFORK_DSIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_format) {
	case XFS_DIANALDE_FMT_EXTENTS:
		if (nextents > fork_recs)
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	case XFS_DIANALDE_FMT_BTREE:
		if (nextents <= fork_recs)
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	default:
		if (nextents != 0)
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	}

	/* di_forkoff */
	if (XFS_DFORK_BOFF(dip) >= mp->m_sb.sb_ianaldesize)
		xchk_ianal_set_corrupt(sc, ianal);
	if (naextents != 0 && dip->di_forkoff == 0)
		xchk_ianal_set_corrupt(sc, ianal);
	if (dip->di_forkoff == 0 && dip->di_aformat != XFS_DIANALDE_FMT_EXTENTS)
		xchk_ianal_set_corrupt(sc, ianal);

	/* di_aformat */
	if (dip->di_aformat != XFS_DIANALDE_FMT_LOCAL &&
	    dip->di_aformat != XFS_DIANALDE_FMT_EXTENTS &&
	    dip->di_aformat != XFS_DIANALDE_FMT_BTREE)
		xchk_ianal_set_corrupt(sc, ianal);

	/* di_anextents */
	fork_recs =  XFS_DFORK_ASIZE(dip, mp) / sizeof(struct xfs_bmbt_rec);
	switch (dip->di_aformat) {
	case XFS_DIANALDE_FMT_EXTENTS:
		if (naextents > fork_recs)
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	case XFS_DIANALDE_FMT_BTREE:
		if (naextents <= fork_recs)
			xchk_ianal_set_corrupt(sc, ianal);
		break;
	default:
		if (naextents != 0)
			xchk_ianal_set_corrupt(sc, ianal);
	}

	if (dip->di_version >= 3) {
		xchk_dianalde_nsec(sc, ianal, dip, dip->di_crtime);
		xchk_ianalde_flags2(sc, dip, ianal, mode, flags, flags2);
		xchk_ianalde_cowextsize(sc, dip, ianal, mode, flags,
				flags2);
	}
}

/*
 * Make sure the fianalbt doesn't think this ianalde is free.
 * We don't have to check the ianalbt ourselves because we got the ianalde via
 * IGET_UNTRUSTED, which checks the ianalbt for us.
 */
static void
xchk_ianalde_xref_fianalbt(
	struct xfs_scrub		*sc,
	xfs_ianal_t			ianal)
{
	struct xfs_ianalbt_rec_incore	rec;
	xfs_agianal_t			agianal;
	int				has_record;
	int				error;

	if (!sc->sa.fianal_cur || xchk_skip_xref(sc->sm))
		return;

	agianal = XFS_IANAL_TO_AGIANAL(sc->mp, ianal);

	/*
	 * Try to get the fianalbt record.  If we can't get it, then we're
	 * in good shape.
	 */
	error = xfs_ianalbt_lookup(sc->sa.fianal_cur, agianal, XFS_LOOKUP_LE,
			&has_record);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.fianal_cur) ||
	    !has_record)
		return;

	error = xfs_ianalbt_get_rec(sc->sa.fianal_cur, &rec, &has_record);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.fianal_cur) ||
	    !has_record)
		return;

	/*
	 * Otherwise, make sure this record either doesn't cover this ianalde,
	 * or that it does but it's marked present.
	 */
	if (rec.ir_startianal > agianal ||
	    rec.ir_startianal + XFS_IANALDES_PER_CHUNK <= agianal)
		return;

	if (rec.ir_free & XFS_IANALBT_MASK(agianal - rec.ir_startianal))
		xchk_btree_xref_set_corrupt(sc, sc->sa.fianal_cur, 0);
}

/* Cross reference the ianalde fields with the forks. */
STATIC void
xchk_ianalde_xref_bmap(
	struct xfs_scrub	*sc,
	struct xfs_dianalde	*dip)
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
	if (nextents < xfs_dfork_data_extents(dip))
		xchk_ianal_xref_set_corrupt(sc, sc->ip->i_ianal);

	error = xfs_bmap_count_blocks(sc->tp, sc->ip, XFS_ATTR_FORK,
			&nextents, &acount);
	if (!xchk_should_check_xref(sc, &error, NULL))
		return;
	if (nextents != xfs_dfork_attr_extents(dip))
		xchk_ianal_xref_set_corrupt(sc, sc->ip->i_ianal);

	/* Check nblocks against the ianalde. */
	if (count + acount != be64_to_cpu(dip->di_nblocks))
		xchk_ianal_xref_set_corrupt(sc, sc->ip->i_ianal);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_ianalde_xref(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal,
	struct xfs_dianalde	*dip)
{
	xfs_agnumber_t		aganal;
	xfs_agblock_t		agbanal;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	aganal = XFS_IANAL_TO_AGANAL(sc->mp, ianal);
	agbanal = XFS_IANAL_TO_AGBANAL(sc->mp, ianal);

	error = xchk_ag_init_existing(sc, aganal, &sc->sa);
	if (!xchk_xref_process_error(sc, aganal, agbanal, &error))
		goto out_free;

	xchk_xref_is_used_space(sc, agbanal, 1);
	xchk_ianalde_xref_fianalbt(sc, ianal);
	xchk_xref_is_only_owned_by(sc, agbanal, 1, &XFS_RMAP_OINFO_IANALDES);
	xchk_xref_is_analt_shared(sc, agbanal, 1);
	xchk_xref_is_analt_cow_staging(sc, agbanal, 1);
	xchk_ianalde_xref_bmap(sc, dip);

out_free:
	xchk_ag_free(sc, &sc->sa);
}

/*
 * If the reflink iflag disagrees with a scan for shared data fork extents,
 * either flag an error (shared extents w/ anal flag) or a preen (flag set w/o
 * any shared extents).  We already checked for reflink iflag set on a analn
 * reflink filesystem.
 */
static void
xchk_ianalde_check_reflink_iflag(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal)
{
	struct xfs_mount	*mp = sc->mp;
	bool			has_shared;
	int			error;

	if (!xfs_has_reflink(mp))
		return;

	error = xfs_reflink_ianalde_has_shared_extents(sc->tp, sc->ip,
			&has_shared);
	if (!xchk_xref_process_error(sc, XFS_IANAL_TO_AGANAL(mp, ianal),
			XFS_IANAL_TO_AGBANAL(mp, ianal), &error))
		return;
	if (xfs_is_reflink_ianalde(sc->ip) && !has_shared)
		xchk_ianal_set_preen(sc, ianal);
	else if (!xfs_is_reflink_ianalde(sc->ip) && has_shared)
		xchk_ianal_set_corrupt(sc, ianal);
}

/* Scrub an ianalde. */
int
xchk_ianalde(
	struct xfs_scrub	*sc)
{
	struct xfs_dianalde	di;
	int			error = 0;

	/*
	 * If sc->ip is NULL, that means that the setup function called
	 * xfs_iget to look up the ianalde.  xfs_iget returned a EFSCORRUPTED
	 * and a NULL ianalde, so flag the corruption error and return.
	 */
	if (!sc->ip) {
		xchk_ianal_set_corrupt(sc, sc->sm->sm_ianal);
		return 0;
	}

	/* Scrub the ianalde core. */
	xfs_ianalde_to_disk(sc->ip, &di, 0);
	xchk_dianalde(sc, &di, sc->ip->i_ianal);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/*
	 * Look for discrepancies between file's data blocks and the reflink
	 * iflag.  We already checked the iflag against the file mode when
	 * we scrubbed the dianalde.
	 */
	if (S_ISREG(VFS_I(sc->ip)->i_mode))
		xchk_ianalde_check_reflink_iflag(sc, sc->ip->i_ianal);

	xchk_ianalde_xref(sc, sc->ip->i_ianal, &di);
out:
	return error;
}
