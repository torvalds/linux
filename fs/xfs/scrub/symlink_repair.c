// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
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
#include "xfs_inode_fork.h"
#include "xfs_symlink.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_symlink_remote.h"
#include "xfs_exchmaps.h"
#include "xfs_exchrange.h"
#include "xfs_health.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/reap.h"
#include "scrub/health.h"

/*
 * Symbolic Link Repair
 * ====================
 *
 * We repair symbolic links by reading whatever target data we can find, up to
 * the first NULL byte.  If the recovered target strlen matches i_size, then
 * we rewrite the target.  In all other cases, we replace the target with an
 * overly long string that cannot possibly resolve.  The new target is written
 * into a private hidden temporary file, and then a file contents exchange
 * commits the new symlink target to the file being repaired.
 */

/* Set us up to repair the symlink file. */
int
xrep_setup_symlink(
	struct xfs_scrub	*sc,
	unsigned int		*resblks)
{
	struct xfs_mount	*mp = sc->mp;
	unsigned long long	blocks;
	int			error;

	error = xrep_tempfile_create(sc, S_IFLNK);
	if (error)
		return error;

	/*
	 * If we're doing a repair, we reserve enough blocks to write out a
	 * completely new symlink file, plus twice as many blocks as we would
	 * need if we can only allocate one block per data fork mapping.  This
	 * should cover the preallocation of the temporary file and exchanging
	 * the extent mappings.
	 *
	 * We cannot use xfs_exchmaps_estimate because we have not yet
	 * constructed the replacement symlink and therefore do not know how
	 * many extents it will use.  By the time we do, we will have a dirty
	 * transaction (which we cannot drop because we cannot drop the
	 * symlink ILOCK) and cannot ask for more reservation.
	 */
	blocks = xfs_symlink_blocks(sc->mp, XFS_SYMLINK_MAXLEN);
	blocks += xfs_bmbt_calc_size(mp, blocks) * 2;
	if (blocks > UINT_MAX)
		return -EOPNOTSUPP;

	*resblks += blocks;
	return 0;
}

/*
 * Try to salvage the pathname from remote blocks.  Returns the number of bytes
 * salvaged or a negative errno.
 */
STATIC ssize_t
xrep_symlink_salvage_remote(
	struct xfs_scrub	*sc)
{
	struct xfs_bmbt_irec	mval[XFS_SYMLINK_MAPS];
	struct xfs_inode	*ip = sc->ip;
	struct xfs_buf		*bp;
	char			*target_buf = sc->buf;
	xfs_failaddr_t		fa;
	xfs_filblks_t		fsblocks;
	xfs_daddr_t		d;
	loff_t			len;
	loff_t			offset = 0;
	unsigned int		byte_cnt;
	bool			magic_ok;
	bool			hdr_ok;
	int			n;
	int			nmaps = XFS_SYMLINK_MAPS;
	int			error;

	/* We'll only read until the buffer is full. */
	len = min_t(loff_t, ip->i_disk_size, XFS_SYMLINK_MAXLEN);
	fsblocks = xfs_symlink_blocks(sc->mp, len);
	error = xfs_bmapi_read(ip, 0, fsblocks, mval, &nmaps, 0);
	if (error)
		return error;

	for (n = 0; n < nmaps; n++) {
		struct xfs_dsymlink_hdr	*dsl;

		d = XFS_FSB_TO_DADDR(sc->mp, mval[n].br_startblock);

		/* Read the rmt block.  We'll run the verifiers manually. */
		error = xfs_trans_read_buf(sc->mp, sc->tp, sc->mp->m_ddev_targp,
				d, XFS_FSB_TO_BB(sc->mp, mval[n].br_blockcount),
				0, &bp, NULL);
		if (error)
			return error;
		bp->b_ops = &xfs_symlink_buf_ops;

		/* How many bytes do we expect to get out of this buffer? */
		byte_cnt = XFS_FSB_TO_B(sc->mp, mval[n].br_blockcount);
		byte_cnt = XFS_SYMLINK_BUF_SPACE(sc->mp, byte_cnt);
		byte_cnt = min_t(unsigned int, byte_cnt, len);

		/*
		 * See if the verifiers accept this block.  We're willing to
		 * salvage if the if the offset/byte/ino are ok and either the
		 * verifier passed or the magic is ok.  Anything else and we
		 * stop dead in our tracks.
		 */
		fa = bp->b_ops->verify_struct(bp);
		dsl = bp->b_addr;
		magic_ok = dsl->sl_magic == cpu_to_be32(XFS_SYMLINK_MAGIC);
		hdr_ok = xfs_symlink_hdr_ok(ip->i_ino, offset, byte_cnt, bp);
		if (!hdr_ok || (fa != NULL && !magic_ok))
			break;

		memcpy(target_buf + offset, dsl + 1, byte_cnt);

		len -= byte_cnt;
		offset += byte_cnt;
	}
	return offset;
}

/*
 * Try to salvage an inline symlink's contents.  Returns the number of bytes
 * salvaged or a negative errno.
 */
STATIC ssize_t
xrep_symlink_salvage_inline(
	struct xfs_scrub	*sc)
{
	struct xfs_inode	*ip = sc->ip;
	char			*target_buf = sc->buf;
	char			*old_target;
	struct xfs_ifork	*ifp;
	unsigned int		nr;

	ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	if (!ifp->if_data)
		return 0;

	/*
	 * If inode repair zapped the link target, pretend that we didn't find
	 * any bytes at all so that we can replace the (now totally lost) link
	 * target with a warning message.
	 */
	old_target = ifp->if_data;
	if (xfs_inode_has_sickness(sc->ip, XFS_SICK_INO_SYMLINK_ZAPPED) &&
	    sc->ip->i_disk_size == 1 && old_target[0] == '?')
		return 0;

	nr = min(XFS_SYMLINK_MAXLEN, xfs_inode_data_fork_size(ip));
	strncpy(target_buf, ifp->if_data, nr);
	return nr;
}

#define DUMMY_TARGET \
	"The target of this symbolic link could not be recovered at all and " \
	"has been replaced with this explanatory message.  To avoid " \
	"accidentally pointing to an existing file path, this message is " \
	"longer than the maximum supported file name length.  That is an " \
	"acceptable length for a symlink target on XFS but will produce " \
	"File Name Too Long errors if resolved."

/* Salvage whatever we can of the target. */
STATIC int
xrep_symlink_salvage(
	struct xfs_scrub	*sc)
{
	char			*target_buf = sc->buf;
	ssize_t			buflen = 0;

	BUILD_BUG_ON(sizeof(DUMMY_TARGET) - 1 <= NAME_MAX);

	/*
	 * Salvage the target if there weren't any corruption problems observed
	 * while scanning it.
	 */
	if (!(sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
		if (sc->ip->i_df.if_format == XFS_DINODE_FMT_LOCAL)
			buflen = xrep_symlink_salvage_inline(sc);
		else
			buflen = xrep_symlink_salvage_remote(sc);
		if (buflen < 0)
			return buflen;

		/*
		 * NULL-terminate the buffer because the ondisk target does not
		 * do that for us.  If salvage didn't find the exact amount of
		 * data that we expected to find, don't salvage anything.
		 */
		target_buf[buflen] = 0;
		if (strlen(target_buf) != sc->ip->i_disk_size)
			buflen = 0;
	}

	/*
	 * Change an empty target into a dummy target and clear the symlink
	 * target zapped flag.
	 */
	if (buflen == 0) {
		xchk_mark_healthy_if_clean(sc, XFS_SICK_INO_SYMLINK_ZAPPED);
		sprintf(target_buf, DUMMY_TARGET);
	}

	trace_xrep_symlink_salvage_target(sc->ip, target_buf,
			strlen(target_buf));
	return 0;
}

STATIC void
xrep_symlink_local_to_remote(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	struct xfs_inode	*ip,
	struct xfs_ifork	*ifp,
	void			*priv)
{
	struct xfs_scrub	*sc = priv;
	struct xfs_dsymlink_hdr	*dsl = bp->b_addr;

	xfs_symlink_local_to_remote(tp, bp, ip, ifp, NULL);

	if (!xfs_has_crc(sc->mp))
		return;

	dsl->sl_owner = cpu_to_be64(sc->ip->i_ino);
	xfs_trans_log_buf(tp, bp, 0,
			  sizeof(struct xfs_dsymlink_hdr) + ifp->if_bytes - 1);
}

/*
 * Prepare both links' data forks for an exchange.  Promote the tempfile from
 * local format to extents format, and if the file being repaired has a short
 * format data fork, turn it into an empty extent list.
 */
STATIC int
xrep_symlink_swap_prep(
	struct xfs_scrub	*sc,
	bool			temp_local,
	bool			ip_local)
{
	int			error;

	/*
	 * If the temp link is in shortform format, convert that to a remote
	 * target so that we can use the atomic mapping exchange.
	 */
	if (temp_local) {
		int		logflags = XFS_ILOG_CORE;

		error = xfs_bmap_local_to_extents(sc->tp, sc->tempip, 1,
				&logflags, XFS_DATA_FORK,
				xrep_symlink_local_to_remote,
				sc);
		if (error)
			return error;

		xfs_trans_log_inode(sc->tp, sc->ip, 0);

		error = xfs_defer_finish(&sc->tp);
		if (error)
			return error;
	}

	/*
	 * If the file being repaired had a shortform data fork, convert that
	 * to an empty extent list in preparation for the atomic mapping
	 * exchange.
	 */
	if (ip_local) {
		struct xfs_ifork	*ifp;

		ifp = xfs_ifork_ptr(sc->ip, XFS_DATA_FORK);
		xfs_idestroy_fork(ifp);
		ifp->if_format = XFS_DINODE_FMT_EXTENTS;
		ifp->if_nextents = 0;
		ifp->if_bytes = 0;
		ifp->if_data = NULL;
		ifp->if_height = 0;

		xfs_trans_log_inode(sc->tp, sc->ip,
				XFS_ILOG_CORE | XFS_ILOG_DDATA);
	}

	return 0;
}

/* Exchange the temporary symlink's data fork with the one being repaired. */
STATIC int
xrep_symlink_swap(
	struct xfs_scrub	*sc)
{
	struct xrep_tempexch	*tx = sc->buf;
	bool			ip_local, temp_local;
	int			error;

	ip_local = sc->ip->i_df.if_format == XFS_DINODE_FMT_LOCAL;
	temp_local = sc->tempip->i_df.if_format == XFS_DINODE_FMT_LOCAL;

	/*
	 * If the both links have a local format data fork and the rebuilt
	 * remote data would fit in the repaired file's data fork, copy the
	 * contents from the tempfile and declare ourselves done.
	 */
	if (ip_local && temp_local &&
	    sc->tempip->i_disk_size <= xfs_inode_data_fork_size(sc->ip)) {
		xrep_tempfile_copyout_local(sc, XFS_DATA_FORK);
		return 0;
	}

	/* Otherwise, make sure both data forks are in block-mapping mode. */
	error = xrep_symlink_swap_prep(sc, temp_local, ip_local);
	if (error)
		return error;

	return xrep_tempexch_contents(sc, tx);
}

/*
 * Free all the remote blocks and reset the data fork.  The caller must join
 * the inode to the transaction.  This function returns with the inode joined
 * to a clean scrub transaction.
 */
STATIC int
xrep_symlink_reset_fork(
	struct xfs_scrub	*sc)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->tempip, XFS_DATA_FORK);
	int			error;

	/* Unmap all the remote target buffers. */
	if (xfs_ifork_has_extents(ifp)) {
		error = xrep_reap_ifork(sc, sc->tempip, XFS_DATA_FORK);
		if (error)
			return error;
	}

	trace_xrep_symlink_reset_fork(sc->tempip);

	/* Reset the temp symlink target to dummy content. */
	xfs_idestroy_fork(ifp);
	return xfs_symlink_write_target(sc->tp, sc->tempip, sc->tempip->i_ino,
			"?", 1, 0, 0);
}

/*
 * Reinitialize a link target.  Caller must ensure the inode is joined to
 * the transaction.
 */
STATIC int
xrep_symlink_rebuild(
	struct xfs_scrub	*sc)
{
	struct xrep_tempexch	*tx;
	char			*target_buf = sc->buf;
	xfs_fsblock_t		fs_blocks;
	unsigned int		target_len;
	unsigned int		resblks;
	int			error;

	/* How many blocks do we need? */
	target_len = strlen(target_buf);
	ASSERT(target_len != 0);
	if (target_len == 0 || target_len > XFS_SYMLINK_MAXLEN)
		return -EFSCORRUPTED;

	trace_xrep_symlink_rebuild(sc->ip);

	/*
	 * In preparation to write the new symlink target to the temporary
	 * file, drop the ILOCK of the file being repaired (it shouldn't be
	 * joined) and take the ILOCK of the temporary file.
	 *
	 * The VFS does not take the IOLOCK while reading a symlink (and new
	 * symlinks are hidden with INEW until they've been written) so it's
	 * possible that a readlink() could see the old corrupted contents
	 * while we're doing this.
	 */
	xchk_iunlock(sc, XFS_ILOCK_EXCL);
	xrep_tempfile_ilock(sc);
	xfs_trans_ijoin(sc->tp, sc->tempip, 0);

	/*
	 * Reserve resources to reinitialize the target.  We're allowed to
	 * exceed file quota to repair inconsistent metadata, though this is
	 * unlikely.
	 */
	fs_blocks = xfs_symlink_blocks(sc->mp, target_len);
	resblks = xfs_symlink_space_res(sc->mp, target_len, fs_blocks);
	error = xfs_trans_reserve_quota_nblks(sc->tp, sc->tempip, resblks, 0,
			true);
	if (error)
		return error;

	/* Erase the dummy target set up by the tempfile initialization. */
	xfs_idestroy_fork(&sc->tempip->i_df);
	sc->tempip->i_df.if_bytes = 0;
	sc->tempip->i_df.if_format = XFS_DINODE_FMT_EXTENTS;

	/* Write the salvaged target to the temporary link. */
	error = xfs_symlink_write_target(sc->tp, sc->tempip, sc->ip->i_ino,
			target_buf, target_len, fs_blocks, resblks);
	if (error)
		return error;

	/*
	 * Commit the repair transaction so that we can use the atomic mapping
	 * exchange functions to compute the correct block reservations and
	 * re-lock the inodes.
	 */
	target_buf = NULL;
	error = xrep_trans_commit(sc);
	if (error)
		return error;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	xrep_tempfile_iunlock(sc);

	/*
	 * We're done with the temporary buffer, so we can reuse it for the
	 * tempfile contents exchange information.
	 */
	tx = sc->buf;
	error = xrep_tempexch_trans_alloc(sc, XFS_DATA_FORK, tx);
	if (error)
		return error;

	/*
	 * Exchange the temp link's data fork with the file being repaired.
	 * This recreates the transaction and takes the ILOCKs of the file
	 * being repaired and the temporary file.
	 */
	error = xrep_symlink_swap(sc);
	if (error)
		return error;

	/*
	 * Release the old symlink blocks and reset the data fork of the temp
	 * link to an empty shortform link.  This is the last repair action we
	 * perform on the symlink, so we don't need to clean the transaction.
	 */
	return xrep_symlink_reset_fork(sc);
}

/* Repair a symbolic link. */
int
xrep_symlink(
	struct xfs_scrub	*sc)
{
	int			error;

	/* The rmapbt is required to reap the old data fork. */
	if (!xfs_has_rmapbt(sc->mp))
		return -EOPNOTSUPP;
	/* We require atomic file exchange range to rebuild anything. */
	if (!xfs_has_exchange_range(sc->mp))
		return -EOPNOTSUPP;

	ASSERT(sc->ilock_flags & XFS_ILOCK_EXCL);

	error = xrep_symlink_salvage(sc);
	if (error)
		return error;

	/* Now reset the target. */
	error = xrep_symlink_rebuild(sc);
	if (error)
		return error;

	return xrep_trans_commit(sc);
}
