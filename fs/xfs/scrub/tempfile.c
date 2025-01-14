// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_quota.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_dir2.h"
#include "xfs_exchrange.h"
#include "xfs_exchmaps.h"
#include "xfs_defer.h"
#include "xfs_symlink_remote.h"
#include "xfs_metafile.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"
#include "scrub/trace.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/xfile.h"

/*
 * Create a temporary file for reconstructing metadata, with the intention of
 * atomically exchanging the temporary file's contents with the file that's
 * being repaired.
 */
int
xrep_tempfile_create(
	struct xfs_scrub	*sc,
	uint16_t		mode)
{
	struct xfs_icreate_args	args = {
		.pip		= sc->mp->m_rootip,
		.mode		= mode,
		.flags		= XFS_ICREATE_TMPFILE | XFS_ICREATE_UNLINKABLE,
	};
	struct xfs_mount	*mp = sc->mp;
	struct xfs_trans	*tp = NULL;
	struct xfs_dquot	*udqp;
	struct xfs_dquot	*gdqp;
	struct xfs_dquot	*pdqp;
	struct xfs_trans_res	*tres;
	struct xfs_inode	*dp = mp->m_rootip;
	xfs_ino_t		ino;
	unsigned int		resblks;
	bool			is_dir = S_ISDIR(mode);
	int			error;

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_is_readonly(mp))
		return -EROFS;

	ASSERT(sc->tp == NULL);
	ASSERT(sc->tempip == NULL);

	/*
	 * Make sure that we have allocated dquot(s) on disk.  The temporary
	 * inode should be completely root owned so that we don't fail due to
	 * quota limits.
	 */
	error = xfs_icreate_dqalloc(&args, &udqp, &gdqp, &pdqp);
	if (error)
		return error;

	if (is_dir) {
		resblks = xfs_mkdir_space_res(mp, 0);
		tres = &M_RES(mp)->tr_mkdir;
	} else {
		resblks = XFS_IALLOC_SPACE_RES(mp);
		tres = &M_RES(mp)->tr_create_tmpfile;
	}

	error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp, resblks,
			&tp);
	if (error)
		goto out_release_dquots;

	/* Allocate inode, set up directory. */
	error = xfs_dialloc(&tp, &args, &ino);
	if (error)
		goto out_trans_cancel;
	error = xfs_icreate(tp, ino, &args, &sc->tempip);
	if (error)
		goto out_trans_cancel;

	/* We don't touch file data, so drop the realtime flags. */
	sc->tempip->i_diflags &= ~(XFS_DIFLAG_REALTIME | XFS_DIFLAG_RTINHERIT);
	xfs_trans_log_inode(tp, sc->tempip, XFS_ILOG_CORE);

	/*
	 * Mark our temporary file as private so that LSMs and the ACL code
	 * don't try to add their own metadata or reason about these files.
	 * The file should never be exposed to userspace.
	 */
	VFS_I(sc->tempip)->i_flags |= S_PRIVATE;
	VFS_I(sc->tempip)->i_opflags &= ~IOP_XATTR;

	if (is_dir) {
		error = xfs_dir_init(tp, sc->tempip, dp);
		if (error)
			goto out_trans_cancel;
	} else if (S_ISLNK(VFS_I(sc->tempip)->i_mode)) {
		/*
		 * Initialize the temporary symlink with a meaningless target
		 * that won't trip the verifiers.  Repair must rewrite the
		 * target with meaningful content before swapping with the file
		 * being repaired.  A single-byte target will not write a
		 * remote target block, so the owner is irrelevant.
		 */
		error = xfs_symlink_write_target(tp, sc->tempip,
				sc->tempip->i_ino, ".", 1, 0, 0);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, sc->tempip, udqp, gdqp, pdqp);

	/*
	 * Put our temp file on the unlinked list so it's purged automatically.
	 * All file-based metadata being reconstructed using this file must be
	 * atomically exchanged with the original file because the contents
	 * here will be purged when the inode is dropped or log recovery cleans
	 * out the unlinked list.
	 */
	error = xfs_iunlink(tp, sc->tempip);
	if (error)
		goto out_trans_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_inode;

	trace_xrep_tempfile_create(sc);

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	/* Finish setting up the incore / vfs context. */
	xfs_iunlock(sc->tempip, XFS_ILOCK_EXCL);
	xfs_setup_iops(sc->tempip);
	xfs_finish_inode_setup(sc->tempip);

	sc->temp_ilock_flags = 0;
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the inode and release the inode.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (sc->tempip) {
		xfs_iunlock(sc->tempip, XFS_ILOCK_EXCL);
		xfs_finish_inode_setup(sc->tempip);
		xchk_irele(sc, sc->tempip);
	}
out_release_dquots:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	return error;
}

/*
 * Move sc->tempip from the regular directory tree to the metadata directory
 * tree if sc->ip is part of the metadata directory tree and tempip has an
 * eligible file mode.
 *
 * Temporary files have to be created before we even know which inode we're
 * going to scrub, so we assume that they will be part of the regular directory
 * tree.  If it turns out that we're actually scrubbing a file from the
 * metadata directory tree, we have to subtract the temp file from the root
 * dquots and detach the dquots prior to setting the METADATA iflag.  However,
 * the scrub setup functions grab sc->ip and create sc->tempip before we
 * actually get around to checking if the file mode is the right type for the
 * scrubber.
 */
int
xrep_tempfile_adjust_directory_tree(
	struct xfs_scrub	*sc)
{
	int			error;

	if (!sc->tempip)
		return 0;

	ASSERT(sc->tp == NULL);
	ASSERT(!xfs_is_metadir_inode(sc->tempip));

	if (!sc->ip || !xfs_is_metadir_inode(sc->ip))
		return 0;
	if (!S_ISDIR(VFS_I(sc->tempip)->i_mode) &&
	    !S_ISREG(VFS_I(sc->tempip)->i_mode))
		return 0;

	xfs_ilock(sc->tempip, XFS_IOLOCK_EXCL);
	sc->temp_ilock_flags |= XFS_IOLOCK_EXCL;

	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out_iolock;

	xrep_tempfile_ilock(sc);
	xfs_trans_ijoin(sc->tp, sc->tempip, 0);

	/* Metadir files are not accounted in quota, so drop icount */
	xfs_trans_mod_dquot_byino(sc->tp, sc->tempip, XFS_TRANS_DQ_ICOUNT, -1L);
	xfs_metafile_set_iflag(sc->tp, sc->tempip, XFS_METAFILE_UNKNOWN);

	error = xrep_trans_commit(sc);
	if (error)
		goto out_ilock;

	xfs_iflags_set(sc->tempip, XFS_IRECOVERY);
	xfs_qm_dqdetach(sc->tempip);
out_ilock:
	xrep_tempfile_iunlock(sc);
out_iolock:
	xrep_tempfile_iounlock(sc);
	return error;
}

/*
 * Remove this temporary file from the metadata directory tree so that it can
 * be inactivated the normal way.
 */
STATIC int
xrep_tempfile_remove_metadir(
	struct xfs_scrub	*sc)
{
	int			error;

	if (!sc->tempip || !xfs_is_metadir_inode(sc->tempip))
		return 0;

	ASSERT(sc->tp == NULL);

	xfs_iflags_clear(sc->tempip, XFS_IRECOVERY);

	xfs_ilock(sc->tempip, XFS_IOLOCK_EXCL);
	sc->temp_ilock_flags |= XFS_IOLOCK_EXCL;

	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out_iolock;

	xrep_tempfile_ilock(sc);
	xfs_trans_ijoin(sc->tp, sc->tempip, 0);

	xfs_metafile_clear_iflag(sc->tp, sc->tempip);

	/* Non-metadir files are accounted in quota, so bump bcount/icount */
	error = xfs_qm_dqattach_locked(sc->tempip, false);
	if (error)
		goto out_cancel;

	xfs_trans_mod_dquot_byino(sc->tp, sc->tempip, XFS_TRANS_DQ_ICOUNT, 1L);
	xfs_trans_mod_dquot_byino(sc->tp, sc->tempip, XFS_TRANS_DQ_BCOUNT,
			sc->tempip->i_nblocks);
	error = xrep_trans_commit(sc);
	goto out_ilock;

out_cancel:
	xchk_trans_cancel(sc);
out_ilock:
	xrep_tempfile_iunlock(sc);
out_iolock:
	xrep_tempfile_iounlock(sc);
	return error;
}

/* Take IOLOCK_EXCL on the temporary file, maybe. */
bool
xrep_tempfile_iolock_nowait(
	struct xfs_scrub	*sc)
{
	if (xfs_ilock_nowait(sc->tempip, XFS_IOLOCK_EXCL)) {
		sc->temp_ilock_flags |= XFS_IOLOCK_EXCL;
		return true;
	}

	return false;
}

/*
 * Take the temporary file's IOLOCK while holding a different inode's IOLOCK.
 * In theory nobody else should hold the tempfile's IOLOCK, but we use trylock
 * to avoid deadlocks and lockdep complaints.
 */
int
xrep_tempfile_iolock_polled(
	struct xfs_scrub	*sc)
{
	int			error = 0;

	while (!xrep_tempfile_iolock_nowait(sc)) {
		if (xchk_should_terminate(sc, &error))
			return error;
		delay(1);
	}

	return 0;
}

/* Release IOLOCK_EXCL on the temporary file. */
void
xrep_tempfile_iounlock(
	struct xfs_scrub	*sc)
{
	xfs_iunlock(sc->tempip, XFS_IOLOCK_EXCL);
	sc->temp_ilock_flags &= ~XFS_IOLOCK_EXCL;
}

/* Prepare the temporary file for metadata updates by grabbing ILOCK_EXCL. */
void
xrep_tempfile_ilock(
	struct xfs_scrub	*sc)
{
	sc->temp_ilock_flags |= XFS_ILOCK_EXCL;
	xfs_ilock(sc->tempip, XFS_ILOCK_EXCL);
}

/* Try to grab ILOCK_EXCL on the temporary file. */
bool
xrep_tempfile_ilock_nowait(
	struct xfs_scrub	*sc)
{
	if (xfs_ilock_nowait(sc->tempip, XFS_ILOCK_EXCL)) {
		sc->temp_ilock_flags |= XFS_ILOCK_EXCL;
		return true;
	}

	return false;
}

/* Unlock ILOCK_EXCL on the temporary file after an update. */
void
xrep_tempfile_iunlock(
	struct xfs_scrub	*sc)
{
	xfs_iunlock(sc->tempip, XFS_ILOCK_EXCL);
	sc->temp_ilock_flags &= ~XFS_ILOCK_EXCL;
}

/*
 * Begin the process of making changes to both the file being scrubbed and
 * the temporary file by taking ILOCK_EXCL on both.
 */
void
xrep_tempfile_ilock_both(
	struct xfs_scrub	*sc)
{
	xfs_lock_two_inodes(sc->ip, XFS_ILOCK_EXCL, sc->tempip, XFS_ILOCK_EXCL);
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	sc->temp_ilock_flags |= XFS_ILOCK_EXCL;
}

/* Unlock ILOCK_EXCL on both files. */
void
xrep_tempfile_iunlock_both(
	struct xfs_scrub	*sc)
{
	xrep_tempfile_iunlock(sc);
	xchk_iunlock(sc, XFS_ILOCK_EXCL);
}

/* Release the temporary file. */
void
xrep_tempfile_rele(
	struct xfs_scrub	*sc)
{
	if (!sc->tempip)
		return;

	if (sc->temp_ilock_flags) {
		xfs_iunlock(sc->tempip, sc->temp_ilock_flags);
		sc->temp_ilock_flags = 0;
	}

	xrep_tempfile_remove_metadir(sc);
	xchk_irele(sc, sc->tempip);
	sc->tempip = NULL;
}

/*
 * Make sure that the given range of the data fork of the temporary file is
 * mapped to written blocks.  The caller must ensure that both inodes are
 * joined to the transaction.
 */
int
xrep_tempfile_prealloc(
	struct xfs_scrub	*sc,
	xfs_fileoff_t		off,
	xfs_filblks_t		len)
{
	struct xfs_bmbt_irec	map;
	xfs_fileoff_t		end = off + len;
	int			error;

	ASSERT(sc->tempip != NULL);
	ASSERT(!XFS_NOT_DQATTACHED(sc->mp, sc->tempip));

	for (; off < end; off = map.br_startoff + map.br_blockcount) {
		int		nmaps = 1;

		/*
		 * If we have a real extent mapping this block then we're
		 * in ok shape.
		 */
		error = xfs_bmapi_read(sc->tempip, off, end - off, &map, &nmaps,
				XFS_DATA_FORK);
		if (error)
			return error;
		if (nmaps == 0) {
			ASSERT(nmaps != 0);
			return -EFSCORRUPTED;
		}

		if (xfs_bmap_is_written_extent(&map))
			continue;

		/*
		 * If we find a delalloc reservation then something is very
		 * very wrong.  Bail out.
		 */
		if (map.br_startblock == DELAYSTARTBLOCK)
			return -EFSCORRUPTED;

		/*
		 * Make sure this block has a real zeroed extent allocated to
		 * it.
		 */
		nmaps = 1;
		error = xfs_bmapi_write(sc->tp, sc->tempip, off, end - off,
				XFS_BMAPI_CONVERT | XFS_BMAPI_ZERO, 0, &map,
				&nmaps);
		if (error)
			return error;
		if (nmaps != 1)
			return -EFSCORRUPTED;

		trace_xrep_tempfile_prealloc(sc, XFS_DATA_FORK, &map);

		/* Commit new extent and all deferred work. */
		error = xfs_defer_finish(&sc->tp);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Write data to each block of a file.  The given range of the tempfile's data
 * fork must already be populated with written extents.
 */
int
xrep_tempfile_copyin(
	struct xfs_scrub	*sc,
	xfs_fileoff_t		off,
	xfs_filblks_t		len,
	xrep_tempfile_copyin_fn	prep_fn,
	void			*data)
{
	LIST_HEAD(buffers_list);
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*bp;
	xfs_fileoff_t		flush_mask;
	xfs_fileoff_t		end = off + len;
	loff_t			pos = XFS_FSB_TO_B(mp, off);
	int			error = 0;

	ASSERT(S_ISREG(VFS_I(sc->tempip)->i_mode));

	/* Flush buffers to disk every 512K */
	flush_mask = XFS_B_TO_FSBT(mp, (1U << 19)) - 1;

	for (; off < end; off++, pos += mp->m_sb.sb_blocksize) {
		struct xfs_bmbt_irec	map;
		int			nmaps = 1;

		/* Read block mapping for this file block. */
		error = xfs_bmapi_read(sc->tempip, off, 1, &map, &nmaps, 0);
		if (error)
			goto out_err;
		if (nmaps == 0 || !xfs_bmap_is_written_extent(&map)) {
			error = -EFSCORRUPTED;
			goto out_err;
		}

		/* Get the metadata buffer for this offset in the file. */
		error = xfs_trans_get_buf(sc->tp, mp->m_ddev_targp,
				XFS_FSB_TO_DADDR(mp, map.br_startblock),
				mp->m_bsize, 0, &bp);
		if (error)
			goto out_err;

		trace_xrep_tempfile_copyin(sc, XFS_DATA_FORK, &map);

		/* Read in a block's worth of data from the xfile. */
		error = prep_fn(sc, bp, data);
		if (error) {
			xfs_trans_brelse(sc->tp, bp);
			goto out_err;
		}

		/* Queue buffer, and flush if we have too much dirty data. */
		xfs_buf_delwri_queue_here(bp, &buffers_list);
		xfs_trans_brelse(sc->tp, bp);

		if (!(off & flush_mask)) {
			error = xfs_buf_delwri_submit(&buffers_list);
			if (error)
				goto out_err;
		}
	}

	/*
	 * Write the new blocks to disk.  If the ordered list isn't empty after
	 * that, then something went wrong and we have to fail.  This should
	 * never happen, but we'll check anyway.
	 */
	error = xfs_buf_delwri_submit(&buffers_list);
	if (error)
		goto out_err;

	if (!list_empty(&buffers_list)) {
		ASSERT(list_empty(&buffers_list));
		error = -EIO;
		goto out_err;
	}

	return 0;

out_err:
	xfs_buf_delwri_cancel(&buffers_list);
	return error;
}

/*
 * Set the temporary file's size.  Caller must join the tempfile to the scrub
 * transaction and is responsible for adjusting block mappings as needed.
 */
int
xrep_tempfile_set_isize(
	struct xfs_scrub	*sc,
	unsigned long long	isize)
{
	if (sc->tempip->i_disk_size == isize)
		return 0;

	sc->tempip->i_disk_size = isize;
	i_size_write(VFS_I(sc->tempip), isize);
	return xrep_tempfile_roll_trans(sc);
}

/*
 * Roll a repair transaction involving the temporary file.  Caller must join
 * both the temporary file and the file being scrubbed to the transaction.
 * This function return with both inodes joined to a new scrub transaction,
 * or the usual negative errno.
 */
int
xrep_tempfile_roll_trans(
	struct xfs_scrub	*sc)
{
	int			error;

	xfs_trans_log_inode(sc->tp, sc->tempip, XFS_ILOG_CORE);
	error = xrep_roll_trans(sc);
	if (error)
		return error;

	xfs_trans_ijoin(sc->tp, sc->tempip, 0);
	return 0;
}

/*
 * Fill out the mapping exchange request in preparation for atomically
 * committing the contents of a metadata file that we've rebuilt in the temp
 * file.
 */
STATIC int
xrep_tempexch_prep_request(
	struct xfs_scrub	*sc,
	int			whichfork,
	struct xrep_tempexch	*tx)
{
	struct xfs_exchmaps_req	*req = &tx->req;

	memset(tx, 0, sizeof(struct xrep_tempexch));

	/* COW forks don't exist on disk. */
	if (whichfork == XFS_COW_FORK) {
		ASSERT(0);
		return -EINVAL;
	}

	/* Both files should have the relevant forks. */
	if (!xfs_ifork_ptr(sc->ip, whichfork) ||
	    !xfs_ifork_ptr(sc->tempip, whichfork)) {
		ASSERT(xfs_ifork_ptr(sc->ip, whichfork) != NULL);
		ASSERT(xfs_ifork_ptr(sc->tempip, whichfork) != NULL);
		return -EINVAL;
	}

	/* Exchange all mappings in both forks. */
	req->ip1 = sc->tempip;
	req->ip2 = sc->ip;
	req->startoff1 = 0;
	req->startoff2 = 0;
	switch (whichfork) {
	case XFS_ATTR_FORK:
		req->flags |= XFS_EXCHMAPS_ATTR_FORK;
		break;
	case XFS_DATA_FORK:
		/* Always exchange sizes when exchanging data fork mappings. */
		req->flags |= XFS_EXCHMAPS_SET_SIZES;
		break;
	}
	req->blockcount = XFS_MAX_FILEOFF;

	return 0;
}

/*
 * Fill out the mapping exchange resource estimation structures in preparation
 * for exchanging the contents of a metadata file that we've rebuilt in the
 * temp file.  Caller must hold IOLOCK_EXCL but not ILOCK_EXCL on both files.
 */
STATIC int
xrep_tempexch_estimate(
	struct xfs_scrub	*sc,
	struct xrep_tempexch	*tx)
{
	struct xfs_exchmaps_req	*req = &tx->req;
	struct xfs_ifork	*ifp;
	struct xfs_ifork	*tifp;
	int			whichfork = xfs_exchmaps_reqfork(req);
	int			state = 0;

	/*
	 * The exchmaps code only knows how to exchange file fork space
	 * mappings.  Any fork data in local format must be promoted to a
	 * single block before the exchange can take place.
	 */
	ifp = xfs_ifork_ptr(sc->ip, whichfork);
	if (ifp->if_format == XFS_DINODE_FMT_LOCAL)
		state |= 1;

	tifp = xfs_ifork_ptr(sc->tempip, whichfork);
	if (tifp->if_format == XFS_DINODE_FMT_LOCAL)
		state |= 2;

	switch (state) {
	case 0:
		/* Both files have mapped extents; use the regular estimate. */
		return xfs_exchrange_estimate(req);
	case 1:
		/*
		 * The file being repaired is in local format, but the temp
		 * file has mapped extents.  To perform the exchange, the file
		 * being repaired must have its shorform data converted to an
		 * ondisk block so that the forks will be in extents format.
		 * We need one resblk for the conversion; the number of
		 * exchanges is (worst case) the temporary file's extent count
		 * plus the block we converted.
		 */
		req->ip1_bcount = sc->tempip->i_nblocks;
		req->ip2_bcount = 1;
		req->nr_exchanges = 1 + tifp->if_nextents;
		req->resblks = 1;
		break;
	case 2:
		/*
		 * The temporary file is in local format, but the file being
		 * repaired has mapped extents.  To perform the exchange, the
		 * temp file must have its shortform data converted to an
		 * ondisk block, and the fork changed to extents format.  We
		 * need one resblk for the conversion; the number of exchanges
		 * is (worst case) the extent count of the file being repaired
		 * plus the block we converted.
		 */
		req->ip1_bcount = 1;
		req->ip2_bcount = sc->ip->i_nblocks;
		req->nr_exchanges = 1 + ifp->if_nextents;
		req->resblks = 1;
		break;
	case 3:
		/*
		 * Both forks are in local format.  To perform the exchange,
		 * both files must have their shortform data converted to
		 * fsblocks, and both forks must be converted to extents
		 * format.  We need two resblks for the two conversions, and
		 * the number of exchanges is 1 since there's only one block at
		 * fileoff 0.  Presumably, the caller could not exchange the
		 * two inode fork areas directly.
		 */
		req->ip1_bcount = 1;
		req->ip2_bcount = 1;
		req->nr_exchanges = 1;
		req->resblks = 2;
		break;
	}

	return xfs_exchmaps_estimate_overhead(req);
}

/*
 * Obtain a quota reservation to make sure we don't hit EDQUOT.  We can skip
 * this if quota enforcement is disabled or if both inodes' dquots are the
 * same.  The qretry structure must be initialized to zeroes before the first
 * call to this function.
 */
STATIC int
xrep_tempexch_reserve_quota(
	struct xfs_scrub		*sc,
	const struct xrep_tempexch	*tx)
{
	struct xfs_trans		*tp = sc->tp;
	const struct xfs_exchmaps_req	*req = &tx->req;
	int64_t				ddelta, rdelta;
	int				error;

	/*
	 * Don't bother with a quota reservation if we're not enforcing them
	 * or the two inodes have the same dquots.
	 */
	if (!XFS_IS_QUOTA_ON(tp->t_mountp) || req->ip1 == req->ip2 ||
	    (req->ip1->i_udquot == req->ip2->i_udquot &&
	     req->ip1->i_gdquot == req->ip2->i_gdquot &&
	     req->ip1->i_pdquot == req->ip2->i_pdquot))
		return 0;

	/*
	 * Quota reservation for each file comes from two sources.  First, we
	 * need to account for any net gain in mapped blocks during the
	 * exchange.  Second, we need reservation for the gross gain in mapped
	 * blocks so that we don't trip over any quota block reservation
	 * assertions.  We must reserve the gross gain because the quota code
	 * subtracts from bcount the number of blocks that we unmap; it does
	 * not add that quantity back to the quota block reservation.
	 */
	ddelta = max_t(int64_t, 0, req->ip2_bcount - req->ip1_bcount);
	rdelta = max_t(int64_t, 0, req->ip2_rtbcount - req->ip1_rtbcount);
	error = xfs_trans_reserve_quota_nblks(tp, req->ip1,
			ddelta + req->ip1_bcount, rdelta + req->ip1_rtbcount,
			true);
	if (error)
		return error;

	ddelta = max_t(int64_t, 0, req->ip1_bcount - req->ip2_bcount);
	rdelta = max_t(int64_t, 0, req->ip1_rtbcount - req->ip2_rtbcount);
	return xfs_trans_reserve_quota_nblks(tp, req->ip2,
			ddelta + req->ip2_bcount, rdelta + req->ip2_rtbcount,
			true);
}

/*
 * Prepare an existing transaction for an atomic file contents exchange.
 *
 * This function fills out the mapping exchange request and resource estimation
 * structures in preparation for exchanging the contents of a metadata file
 * that has been rebuilt in the temp file.  Next, it reserves space and quota
 * for the transaction.
 *
 * The caller must hold ILOCK_EXCL of the scrub target file and the temporary
 * file.  The caller must join both inodes to the transaction with no unlock
 * flags, and is responsible for dropping both ILOCKs when appropriate.  Only
 * use this when those ILOCKs cannot be dropped.
 */
int
xrep_tempexch_trans_reserve(
	struct xfs_scrub	*sc,
	int			whichfork,
	struct xrep_tempexch	*tx)
{
	int			error;

	ASSERT(sc->tp != NULL);
	xfs_assert_ilocked(sc->ip, XFS_ILOCK_EXCL);
	xfs_assert_ilocked(sc->tempip, XFS_ILOCK_EXCL);

	error = xrep_tempexch_prep_request(sc, whichfork, tx);
	if (error)
		return error;

	error = xfs_exchmaps_estimate(&tx->req);
	if (error)
		return error;

	error = xfs_trans_reserve_more(sc->tp, tx->req.resblks, 0);
	if (error)
		return error;

	return xrep_tempexch_reserve_quota(sc, tx);
}

/*
 * Create a new transaction for a file contents exchange.
 *
 * This function fills out the mapping excahange request and resource
 * estimation structures in preparation for exchanging the contents of a
 * metadata file that has been rebuilt in the temp file.  Next, it reserves
 * space, takes ILOCK_EXCL of both inodes, joins them to the transaction and
 * reserves quota for the transaction.
 *
 * The caller is responsible for dropping both ILOCKs when appropriate.
 */
int
xrep_tempexch_trans_alloc(
	struct xfs_scrub	*sc,
	int			whichfork,
	struct xrep_tempexch	*tx)
{
	unsigned int		flags = 0;
	int			error;

	ASSERT(sc->tp == NULL);
	ASSERT(xfs_has_exchange_range(sc->mp));

	error = xrep_tempexch_prep_request(sc, whichfork, tx);
	if (error)
		return error;

	error = xrep_tempexch_estimate(sc, tx);
	if (error)
		return error;

	if (xfs_has_lazysbcount(sc->mp))
		flags |= XFS_TRANS_RES_FDBLKS;

	error = xfs_trans_alloc(sc->mp, &M_RES(sc->mp)->tr_itruncate,
			tx->req.resblks, 0, flags, &sc->tp);
	if (error)
		return error;

	sc->temp_ilock_flags |= XFS_ILOCK_EXCL;
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	xfs_exchrange_ilock(sc->tp, sc->ip, sc->tempip);

	return xrep_tempexch_reserve_quota(sc, tx);
}

/*
 * Exchange file mappings (and hence file contents) between the file being
 * repaired and the temporary file.  Returns with both inodes locked and joined
 * to a clean scrub transaction.
 */
int
xrep_tempexch_contents(
	struct xfs_scrub	*sc,
	struct xrep_tempexch	*tx)
{
	int			error;

	ASSERT(xfs_has_exchange_range(sc->mp));

	xfs_exchange_mappings(sc->tp, &tx->req);
	error = xfs_defer_finish(&sc->tp);
	if (error)
		return error;

	/*
	 * If we exchanged the ondisk sizes of two metadata files, we must
	 * exchanged the incore sizes as well.
	 */
	if (tx->req.flags & XFS_EXCHMAPS_SET_SIZES) {
		loff_t	temp;

		temp = i_size_read(VFS_I(sc->ip));
		i_size_write(VFS_I(sc->ip), i_size_read(VFS_I(sc->tempip)));
		i_size_write(VFS_I(sc->tempip), temp);
	}

	return 0;
}

/*
 * Write local format data from one of the temporary file's forks into the same
 * fork of file being repaired, and exchange the file sizes, if appropriate.
 * Caller must ensure that the file being repaired has enough fork space to
 * hold all the bytes.
 */
void
xrep_tempfile_copyout_local(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	struct xfs_ifork	*temp_ifp;
	struct xfs_ifork	*ifp;
	unsigned int		ilog_flags = XFS_ILOG_CORE;

	temp_ifp = xfs_ifork_ptr(sc->tempip, whichfork);
	ifp = xfs_ifork_ptr(sc->ip, whichfork);

	ASSERT(temp_ifp != NULL);
	ASSERT(ifp != NULL);
	ASSERT(temp_ifp->if_format == XFS_DINODE_FMT_LOCAL);
	ASSERT(ifp->if_format == XFS_DINODE_FMT_LOCAL);

	switch (whichfork) {
	case XFS_DATA_FORK:
		ASSERT(sc->tempip->i_disk_size <=
					xfs_inode_data_fork_size(sc->ip));
		break;
	case XFS_ATTR_FORK:
		ASSERT(sc->tempip->i_forkoff >= sc->ip->i_forkoff);
		break;
	default:
		ASSERT(0);
		return;
	}

	/* Recreate @sc->ip's incore fork (ifp) with data from temp_ifp. */
	xfs_idestroy_fork(ifp);
	xfs_init_local_fork(sc->ip, whichfork, temp_ifp->if_data,
			temp_ifp->if_bytes);

	if (whichfork == XFS_DATA_FORK) {
		i_size_write(VFS_I(sc->ip), i_size_read(VFS_I(sc->tempip)));
		sc->ip->i_disk_size = sc->tempip->i_disk_size;
	}

	ilog_flags |= xfs_ilog_fdata(whichfork);
	xfs_trans_log_inode(sc->tp, sc->ip, ilog_flags);
}

/* Decide if a given XFS inode is a temporary file for a repair. */
bool
xrep_is_tempfile(
	const struct xfs_inode	*ip)
{
	const struct inode	*inode = &ip->i_vnode;
	struct xfs_mount	*mp = ip->i_mount;

	/*
	 * Files in the metadata directory tree also have S_PRIVATE set and
	 * IOP_XATTR unset, so we must distinguish them separately.  We (ab)use
	 * the IRECOVERY flag to mark temporary metadir inodes knowing that the
	 * end of log recovery clears IRECOVERY, so the only ones that can
	 * exist during online repair are the ones we create.
	 */
	if (xfs_has_metadir(mp) && (ip->i_diflags2 & XFS_DIFLAG2_METADATA))
		return __xfs_iflags_test(ip, XFS_IRECOVERY);

	if (IS_PRIVATE(inode) && !(inode->i_opflags & IOP_XATTR))
		return true;

	return false;
}
