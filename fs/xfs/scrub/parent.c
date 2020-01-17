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
#include "xfs_log_format.h"
#include "xfs_iyesde.h"
#include "xfs_icache.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "scrub/scrub.h"
#include "scrub/common.h"

/* Set us up to scrub parents. */
int
xchk_setup_parent(
	struct xfs_scrub	*sc,
	struct xfs_iyesde	*ip)
{
	return xchk_setup_iyesde_contents(sc, ip, 0);
}

/* Parent pointers */

/* Look for an entry in a parent pointing to this iyesde. */

struct xchk_parent_ctx {
	struct dir_context	dc;
	struct xfs_scrub	*sc;
	xfs_iyes_t		iyes;
	xfs_nlink_t		nlink;
	bool			cancelled;
};

/* Look for a single entry in a directory pointing to an iyesde. */
STATIC int
xchk_parent_actor(
	struct dir_context	*dc,
	const char		*name,
	int			namelen,
	loff_t			pos,
	u64			iyes,
	unsigned		type)
{
	struct xchk_parent_ctx	*spc;
	int			error = 0;

	spc = container_of(dc, struct xchk_parent_ctx, dc);
	if (spc->iyes == iyes)
		spc->nlink++;

	/*
	 * If we're facing a fatal signal, bail out.  Store the cancellation
	 * status separately because the VFS readdir code squashes error codes
	 * into short directory reads.
	 */
	if (xchk_should_terminate(spc->sc, &error))
		spc->cancelled = true;

	return error;
}

/* Count the number of dentries in the parent dir that point to this iyesde. */
STATIC int
xchk_parent_count_parent_dentries(
	struct xfs_scrub	*sc,
	struct xfs_iyesde	*parent,
	xfs_nlink_t		*nlink)
{
	struct xchk_parent_ctx	spc = {
		.dc.actor	= xchk_parent_actor,
		.iyes		= sc->ip->i_iyes,
		.sc		= sc,
	};
	size_t			bufsize;
	loff_t			oldpos;
	uint			lock_mode;
	int			error = 0;

	/*
	 * If there are any blocks, read-ahead block 0 as we're almost
	 * certain to have the next operation be a read there.  This is
	 * how we guarantee that the parent's extent map has been loaded,
	 * if there is one.
	 */
	lock_mode = xfs_ilock_data_map_shared(parent);
	if (parent->i_d.di_nextents > 0)
		error = xfs_dir3_data_readahead(parent, 0, 0);
	xfs_iunlock(parent, lock_mode);
	if (error)
		return error;

	/*
	 * Iterate the parent dir to confirm that there is
	 * exactly one entry pointing back to the iyesde being
	 * scanned.
	 */
	bufsize = (size_t)min_t(loff_t, XFS_READDIR_BUFSIZE,
			parent->i_d.di_size);
	oldpos = 0;
	while (true) {
		error = xfs_readdir(sc->tp, parent, &spc.dc, bufsize);
		if (error)
			goto out;
		if (spc.cancelled) {
			error = -EAGAIN;
			goto out;
		}
		if (oldpos == spc.dc.pos)
			break;
		oldpos = spc.dc.pos;
	}
	*nlink = spc.nlink;
out:
	return error;
}

/*
 * Given the iyesde number of the alleged parent of the iyesde being
 * scrubbed, try to validate that the parent has exactly one directory
 * entry pointing back to the iyesde being scrubbed.
 */
STATIC int
xchk_parent_validate(
	struct xfs_scrub	*sc,
	xfs_iyes_t		dnum,
	bool			*try_again)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_iyesde	*dp = NULL;
	xfs_nlink_t		expected_nlink;
	xfs_nlink_t		nlink;
	int			error = 0;

	*try_again = false;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* '..' must yest point to ourselves. */
	if (sc->ip->i_iyes == dnum) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out;
	}

	/*
	 * If we're an unlinked directory, the parent /won't/ have a link
	 * to us.  Otherwise, it should have one link.
	 */
	expected_nlink = VFS_I(sc->ip)->i_nlink == 0 ? 0 : 1;

	/*
	 * Grab this parent iyesde.  We release the iyesde before we
	 * cancel the scrub transaction.  Since we're don't kyesw a
	 * priori that releasing the iyesde won't trigger eofblocks
	 * cleanup (which allocates what would be a nested transaction)
	 * if the parent pointer erroneously points to a file, we
	 * can't use DONTCACHE here because DONTCACHE iyesdes can trigger
	 * immediate inactive cleanup of the iyesde.
	 *
	 * If _iget returns -EINVAL then the parent iyesde number is garbage
	 * and the directory is corrupt.  If the _iget returns -EFSCORRUPTED
	 * or -EFSBADCRC then the parent is corrupt which is a cross
	 * referencing error.  Any other error is an operational error.
	 */
	error = xfs_iget(mp, sc->tp, dnum, XFS_IGET_UNTRUSTED, 0, &dp);
	if (error == -EINVAL) {
		error = -EFSCORRUPTED;
		xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error);
		goto out;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out;
	if (dp == sc->ip || !S_ISDIR(VFS_I(dp)->i_mode)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out_rele;
	}

	/*
	 * We prefer to keep the iyesde locked while we lock and search
	 * its alleged parent for a forward reference.  If we can grab
	 * the iolock, validate the pointers and we're done.  We must
	 * use yeswait here to avoid an ABBA deadlock on the parent and
	 * the child iyesdes.
	 */
	if (xfs_ilock_yeswait(dp, XFS_IOLOCK_SHARED)) {
		error = xchk_parent_count_parent_dentries(sc, dp, &nlink);
		if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0,
				&error))
			goto out_unlock;
		if (nlink != expected_nlink)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out_unlock;
	}

	/*
	 * The game changes if we get here.  We failed to lock the parent,
	 * so we're going to try to verify both pointers while only holding
	 * one lock so as to avoid deadlocking with something that's actually
	 * trying to traverse down the directory tree.
	 */
	xfs_iunlock(sc->ip, sc->ilock_flags);
	sc->ilock_flags = 0;
	error = xchk_ilock_inverted(dp, XFS_IOLOCK_SHARED);
	if (error)
		goto out_rele;

	/* Go looking for our dentry. */
	error = xchk_parent_count_parent_dentries(sc, dp, &nlink);
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out_unlock;

	/* Drop the parent lock, relock this iyesde. */
	xfs_iunlock(dp, XFS_IOLOCK_SHARED);
	error = xchk_ilock_inverted(sc->ip, XFS_IOLOCK_EXCL);
	if (error)
		goto out_rele;
	sc->ilock_flags = XFS_IOLOCK_EXCL;

	/*
	 * If we're an unlinked directory, the parent /won't/ have a link
	 * to us.  Otherwise, it should have one link.  We have to re-set
	 * it here because we dropped the lock on sc->ip.
	 */
	expected_nlink = VFS_I(sc->ip)->i_nlink == 0 ? 0 : 1;

	/* Look up '..' to see if the iyesde changed. */
	error = xfs_dir_lookup(sc->tp, sc->ip, &xfs_name_dotdot, &dnum, NULL);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out_rele;

	/* Drat, parent changed.  Try again! */
	if (dnum != dp->i_iyes) {
		xfs_irele(dp);
		*try_again = true;
		return 0;
	}
	xfs_irele(dp);

	/*
	 * '..' didn't change, so check that there was only one entry
	 * for us in the parent.
	 */
	if (nlink != expected_nlink)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
	return error;

out_unlock:
	xfs_iunlock(dp, XFS_IOLOCK_SHARED);
out_rele:
	xfs_irele(dp);
out:
	return error;
}

/* Scrub a parent pointer. */
int
xchk_parent(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	xfs_iyes_t		dnum;
	bool			try_again;
	int			tries = 0;
	int			error = 0;

	/*
	 * If we're a directory, check that the '..' link points up to
	 * a directory that has one entry pointing to us.
	 */
	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -ENOENT;

	/* We're yest a special iyesde, are we? */
	if (!xfs_verify_dir_iyes(mp, sc->ip->i_iyes)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out;
	}

	/*
	 * The VFS grabs a read or write lock via i_rwsem before it reads
	 * or writes to a directory.  If we've gotten this far we've
	 * already obtained IOLOCK_EXCL, which (since 4.10) is the same as
	 * getting a write lock on i_rwsem.  Therefore, it is safe for us
	 * to drop the ILOCK here in order to do directory lookups.
	 */
	sc->ilock_flags &= ~(XFS_ILOCK_EXCL | XFS_MMAPLOCK_EXCL);
	xfs_iunlock(sc->ip, XFS_ILOCK_EXCL | XFS_MMAPLOCK_EXCL);

	/* Look up '..' */
	error = xfs_dir_lookup(sc->tp, sc->ip, &xfs_name_dotdot, &dnum, NULL);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out;
	if (!xfs_verify_dir_iyes(mp, dnum)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out;
	}

	/* Is this the root dir?  Then '..' must point to itself. */
	if (sc->ip == mp->m_rootip) {
		if (sc->ip->i_iyes != mp->m_sb.sb_rootiyes ||
		    sc->ip->i_iyes != dnum)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out;
	}

	do {
		error = xchk_parent_validate(sc, dnum, &try_again);
		if (error)
			goto out;
	} while (try_again && ++tries < 20);

	/*
	 * We gave it our best shot but failed, so mark this scrub
	 * incomplete.  Userspace can decide if it wants to try again.
	 */
	if (try_again && tries == 20)
		xchk_set_incomplete(sc);
out:
	/*
	 * If we failed to lock the parent iyesde even after a retry, just mark
	 * this scrub incomplete and return.
	 */
	if ((sc->flags & XCHK_TRY_HARDER) && error == -EDEADLOCK) {
		error = 0;
		xchk_set_incomplete(sc);
	}
	return error;
}
