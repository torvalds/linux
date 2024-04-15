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
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_dir2.h"
#include "xfs_exchrange.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/tempfile.h"

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
	struct xfs_mount	*mp = sc->mp;
	struct xfs_trans	*tp = NULL;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
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
	error = xfs_qm_vop_dqalloc(dp, GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, 0,
			XFS_QMOPT_QUOTALL, &udqp, &gdqp, &pdqp);
	if (error)
		return error;

	if (is_dir) {
		resblks = XFS_MKDIR_SPACE_RES(mp, 0);
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
	error = xfs_dialloc(&tp, dp->i_ino, mode, &ino);
	if (error)
		goto out_trans_cancel;
	error = xfs_init_new_inode(&nop_mnt_idmap, tp, dp, ino, mode, 0, 0,
			0, false, &sc->tempip);
	if (error)
		goto out_trans_cancel;

	/* Change the ownership of the inode to root. */
	VFS_I(sc->tempip)->i_uid = GLOBAL_ROOT_UID;
	VFS_I(sc->tempip)->i_gid = GLOBAL_ROOT_GID;
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
		xfs_finish_inode_setup(sc->tempip);
		xchk_irele(sc, sc->tempip);
	}
out_release_dquots:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

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

	xchk_irele(sc, sc->tempip);
	sc->tempip = NULL;
}
