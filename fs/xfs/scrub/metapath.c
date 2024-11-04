// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023-2024 Oracle.  All Rights Reserved.
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
#include "xfs_metafile.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_dir2.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/readdir.h"

/*
 * Metadata Directory Tree Paths
 * =============================
 *
 * A filesystem with metadir enabled expects to find metadata structures
 * attached to files that are accessible by walking a path down the metadata
 * directory tree.  Given the metadir path and the incore inode storing the
 * metadata, this scrubber ensures that the ondisk metadir path points to the
 * ondisk inode represented by the incore inode.
 */

struct xchk_metapath {
	struct xfs_scrub		*sc;

	/* Name for lookup */
	struct xfs_name			xname;

	/* Path for this metadata file and the parent directory */
	const char			*path;
	const char			*parent_path;

	/* Directory parent of the metadata file. */
	struct xfs_inode		*dp;

	/* Locks held on dp */
	unsigned int			dp_ilock_flags;
};

/* Release resources tracked in the buffer. */
static inline void
xchk_metapath_cleanup(
	void			*buf)
{
	struct xchk_metapath	*mpath = buf;

	if (mpath->dp_ilock_flags)
		xfs_iunlock(mpath->dp, mpath->dp_ilock_flags);
	kfree(mpath->path);
}

int
xchk_setup_metapath(
	struct xfs_scrub	*sc)
{
	if (!xfs_has_metadir(sc->mp))
		return -ENOENT;
	if (sc->sm->sm_gen)
		return -EINVAL;

	switch (sc->sm->sm_ino) {
	case XFS_SCRUB_METAPATH_PROBE:
		/* Just probing, nothing else to do. */
		if (sc->sm->sm_agno)
			return -EINVAL;
		return 0;
	default:
		return -ENOENT;
	}
}

/*
 * Take the ILOCK on the metadata directory parent and child.  We do not know
 * that the metadata directory is not corrupt, so we lock the parent and try
 * to lock the child.  Returns 0 if successful, or -EINTR to abort the scrub.
 */
STATIC int
xchk_metapath_ilock_both(
	struct xchk_metapath	*mpath)
{
	struct xfs_scrub	*sc = mpath->sc;
	int			error = 0;

	while (true) {
		xfs_ilock(mpath->dp, XFS_ILOCK_EXCL);
		if (xchk_ilock_nowait(sc, XFS_ILOCK_EXCL)) {
			mpath->dp_ilock_flags |= XFS_ILOCK_EXCL;
			return 0;
		}
		xfs_iunlock(mpath->dp, XFS_ILOCK_EXCL);

		if (xchk_should_terminate(sc, &error))
			return error;

		delay(1);
	}

	ASSERT(0);
	return -EINTR;
}

/* Unlock parent and child inodes. */
static inline void
xchk_metapath_iunlock(
	struct xchk_metapath	*mpath)
{
	struct xfs_scrub	*sc = mpath->sc;

	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	mpath->dp_ilock_flags &= ~XFS_ILOCK_EXCL;
	xfs_iunlock(mpath->dp, XFS_ILOCK_EXCL);
}

int
xchk_metapath(
	struct xfs_scrub	*sc)
{
	struct xchk_metapath	*mpath = sc->buf;
	xfs_ino_t		ino = NULLFSINO;
	int			error;

	/* Just probing, nothing else to do. */
	if (sc->sm->sm_ino == XFS_SCRUB_METAPATH_PROBE)
		return 0;

	/* Parent required to do anything else. */
	if (mpath->dp == NULL) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		return 0;
	}

	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	error = xchk_metapath_ilock_both(mpath);
	if (error)
		goto out_cancel;

	/* Make sure the parent dir has a dirent pointing to this file. */
	error = xchk_dir_lookup(sc, mpath->dp, &mpath->xname, &ino);
	trace_xchk_metapath_lookup(sc, mpath->path, mpath->dp, ino);
	if (error == -ENOENT) {
		/* No directory entry at all */
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		error = 0;
		goto out_ilock;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out_ilock;
	if (ino != sc->ip->i_ino) {
		/* Pointing to wrong inode */
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
	}

out_ilock:
	xchk_metapath_iunlock(mpath);
out_cancel:
	xchk_trans_cancel(sc);
	return error;
}
