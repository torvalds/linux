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
#include "xfs_parent.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_attr.h"
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_rtrefcount_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/readdir.h"
#include "scrub/repair.h"

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

	/* Directory update for repairs */
	struct xfs_dir_update		du;

	/* Path down to this metadata file from the parent directory */
	const char			*path;

	/* Directory parent of the metadata file. */
	struct xfs_inode		*dp;

	/* Locks held on dp */
	unsigned int			dp_ilock_flags;

	/* Transaction block reservations */
	unsigned int			link_resblks;
	unsigned int			unlink_resblks;

	/* Parent pointer updates */
	struct xfs_parent_args		link_ppargs;
	struct xfs_parent_args		unlink_ppargs;

	/* Scratchpads for removing links */
	struct xfs_da_args		pptr_args;
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

/* Set up a metadir path scan.  @path must be dynamically allocated. */
static inline int
xchk_setup_metapath_scan(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	const char		*path,
	struct xfs_inode	*ip)
{
	struct xchk_metapath	*mpath;
	int			error;

	if (!path)
		return -ENOMEM;

	error = xchk_install_live_inode(sc, ip);
	if (error) {
		kfree(path);
		return error;
	}

	mpath = kzalloc(sizeof(struct xchk_metapath), XCHK_GFP_FLAGS);
	if (!mpath) {
		kfree(path);
		return -ENOMEM;
	}

	mpath->sc = sc;
	sc->buf = mpath;
	sc->buf_cleanup = xchk_metapath_cleanup;

	mpath->dp = dp;
	mpath->path = path; /* path is now owned by mpath */

	mpath->xname.name = mpath->path;
	mpath->xname.len = strlen(mpath->path);
	mpath->xname.type = xfs_mode_to_ftype(VFS_I(ip)->i_mode);

	return 0;
}

#ifdef CONFIG_XFS_RT
/* Scan the /rtgroups directory itself. */
static int
xchk_setup_metapath_rtdir(
	struct xfs_scrub	*sc)
{
	if (!sc->mp->m_rtdirip)
		return -ENOENT;

	return xchk_setup_metapath_scan(sc, sc->mp->m_metadirip,
			kasprintf(GFP_KERNEL, "rtgroups"), sc->mp->m_rtdirip);
}

/* Scan a rtgroup inode under the /rtgroups directory. */
static int
xchk_setup_metapath_rtginode(
	struct xfs_scrub	*sc,
	enum xfs_rtg_inodes	type)
{
	struct xfs_rtgroup	*rtg;
	struct xfs_inode	*ip;
	int			error;

	rtg = xfs_rtgroup_get(sc->mp, sc->sm->sm_agno);
	if (!rtg)
		return -ENOENT;

	ip = rtg->rtg_inodes[type];
	if (!ip) {
		error = -ENOENT;
		goto out_put_rtg;
	}

	error = xchk_setup_metapath_scan(sc, sc->mp->m_rtdirip,
			xfs_rtginode_path(rtg_rgno(rtg), type), ip);

out_put_rtg:
	xfs_rtgroup_put(rtg);
	return error;
}
#else
# define xchk_setup_metapath_rtdir(...)		(-ENOENT)
# define xchk_setup_metapath_rtginode(...)	(-ENOENT)
#endif /* CONFIG_XFS_RT */

#ifdef CONFIG_XFS_QUOTA
/* Scan the /quota directory itself. */
static int
xchk_setup_metapath_quotadir(
	struct xfs_scrub	*sc)
{
	struct xfs_quotainfo	*qi = sc->mp->m_quotainfo;

	if (!qi || !qi->qi_dirip)
		return -ENOENT;

	return xchk_setup_metapath_scan(sc, sc->mp->m_metadirip,
			kstrdup("quota", GFP_KERNEL), qi->qi_dirip);
}

/* Scan a quota inode under the /quota directory. */
static int
xchk_setup_metapath_dqinode(
	struct xfs_scrub	*sc,
	xfs_dqtype_t		type)
{
	struct xfs_quotainfo	*qi = sc->mp->m_quotainfo;
	struct xfs_inode	*ip = NULL;

	if (!qi)
		return -ENOENT;

	switch (type) {
	case XFS_DQTYPE_USER:
		ip = qi->qi_uquotaip;
		break;
	case XFS_DQTYPE_GROUP:
		ip = qi->qi_gquotaip;
		break;
	case XFS_DQTYPE_PROJ:
		ip = qi->qi_pquotaip;
		break;
	default:
		ASSERT(0);
		return -EINVAL;
	}
	if (!ip)
		return -ENOENT;

	return xchk_setup_metapath_scan(sc, qi->qi_dirip,
			kstrdup(xfs_dqinode_path(type), GFP_KERNEL), ip);
}
#else
# define xchk_setup_metapath_quotadir(...)	(-ENOENT)
# define xchk_setup_metapath_dqinode(...)	(-ENOENT)
#endif /* CONFIG_XFS_QUOTA */

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
	case XFS_SCRUB_METAPATH_RTDIR:
		return xchk_setup_metapath_rtdir(sc);
	case XFS_SCRUB_METAPATH_RTBITMAP:
		return xchk_setup_metapath_rtginode(sc, XFS_RTGI_BITMAP);
	case XFS_SCRUB_METAPATH_RTSUMMARY:
		return xchk_setup_metapath_rtginode(sc, XFS_RTGI_SUMMARY);
	case XFS_SCRUB_METAPATH_QUOTADIR:
		return xchk_setup_metapath_quotadir(sc);
	case XFS_SCRUB_METAPATH_USRQUOTA:
		return xchk_setup_metapath_dqinode(sc, XFS_DQTYPE_USER);
	case XFS_SCRUB_METAPATH_GRPQUOTA:
		return xchk_setup_metapath_dqinode(sc, XFS_DQTYPE_GROUP);
	case XFS_SCRUB_METAPATH_PRJQUOTA:
		return xchk_setup_metapath_dqinode(sc, XFS_DQTYPE_PROJ);
	case XFS_SCRUB_METAPATH_RTRMAPBT:
		return xchk_setup_metapath_rtginode(sc, XFS_RTGI_RMAP);
	case XFS_SCRUB_METAPATH_RTREFCOUNTBT:
		return xchk_setup_metapath_rtginode(sc, XFS_RTGI_REFCOUNT);
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

#ifdef CONFIG_XFS_ONLINE_REPAIR
/* Create the dirent represented by the final component of the path. */
STATIC int
xrep_metapath_link(
	struct xchk_metapath	*mpath)
{
	struct xfs_scrub	*sc = mpath->sc;

	mpath->du.dp = mpath->dp;
	mpath->du.name = &mpath->xname;
	mpath->du.ip = sc->ip;

	if (xfs_has_parent(sc->mp))
		mpath->du.ppargs = &mpath->link_ppargs;
	else
		mpath->du.ppargs = NULL;

	trace_xrep_metapath_link(sc, mpath->path, mpath->dp, sc->ip->i_ino);

	return xfs_dir_add_child(sc->tp, mpath->link_resblks, &mpath->du);
}

/* Remove the dirent at the final component of the path. */
STATIC int
xrep_metapath_unlink(
	struct xchk_metapath	*mpath,
	xfs_ino_t		ino,
	struct xfs_inode	*ip)
{
	struct xfs_parent_rec	rec;
	struct xfs_scrub	*sc = mpath->sc;
	struct xfs_mount	*mp = sc->mp;
	int			error;

	trace_xrep_metapath_unlink(sc, mpath->path, mpath->dp, ino);

	if (!ip) {
		/* The child inode isn't allocated.  Junk the dirent. */
		xfs_trans_log_inode(sc->tp, mpath->dp, XFS_ILOG_CORE);
		return xfs_dir_removename(sc->tp, mpath->dp, &mpath->xname,
				ino, mpath->unlink_resblks);
	}

	mpath->du.dp = mpath->dp;
	mpath->du.name = &mpath->xname;
	mpath->du.ip = ip;
	mpath->du.ppargs = NULL;

	/* Figure out if we're removing a parent pointer too. */
	if (xfs_has_parent(mp)) {
		xfs_inode_to_parent_rec(&rec, ip);
		error = xfs_parent_lookup(sc->tp, ip, &mpath->xname, &rec,
				&mpath->pptr_args);
		switch (error) {
		case -ENOATTR:
			break;
		case 0:
			mpath->du.ppargs = &mpath->unlink_ppargs;
			break;
		default:
			return error;
		}
	}

	return xfs_dir_remove_child(sc->tp, mpath->unlink_resblks, &mpath->du);
}

/*
 * Try to create a dirent in @mpath->dp with the name @mpath->xname that points
 * to @sc->ip.  Returns:
 *
 * -EEXIST and an @alleged_child if the dirent that points to the wrong inode;
 * 0 if there is now a dirent pointing to @sc->ip; or
 * A negative errno on error.
 */
STATIC int
xrep_metapath_try_link(
	struct xchk_metapath	*mpath,
	xfs_ino_t		*alleged_child)
{
	struct xfs_scrub	*sc = mpath->sc;
	xfs_ino_t		ino;
	int			error;

	/* Allocate transaction, lock inodes, join to transaction. */
	error = xchk_trans_alloc(sc, mpath->link_resblks);
	if (error)
		return error;

	error = xchk_metapath_ilock_both(mpath);
	if (error) {
		xchk_trans_cancel(sc);
		return error;
	}
	xfs_trans_ijoin(sc->tp, mpath->dp, 0);
	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	error = xchk_dir_lookup(sc, mpath->dp, &mpath->xname, &ino);
	trace_xrep_metapath_lookup(sc, mpath->path, mpath->dp, ino);
	if (error == -ENOENT) {
		/*
		 * There is no dirent in the directory.  Create an entry
		 * pointing to @sc->ip.
		 */
		error = xrep_metapath_link(mpath);
		if (error)
			goto out_cancel;

		error = xrep_trans_commit(sc);
		xchk_metapath_iunlock(mpath);
		return error;
	}
	if (error)
		goto out_cancel;

	if (ino == sc->ip->i_ino) {
		/* The dirent already points to @sc->ip; we're done. */
		error = 0;
		goto out_cancel;
	}

	/*
	 * The dirent points elsewhere; pass that back so that the caller
	 * can try to remove the dirent.
	 */
	*alleged_child = ino;
	error = -EEXIST;

out_cancel:
	xchk_trans_cancel(sc);
	xchk_metapath_iunlock(mpath);
	return error;
}

/*
 * Take the ILOCK on the metadata directory parent and a bad child, if one is
 * supplied.  We do not know that the metadata directory is not corrupt, so we
 * lock the parent and try to lock the child.  Returns 0 if successful, or
 * -EINTR to abort the repair.  The lock state of @dp is not recorded in @mpath.
 */
STATIC int
xchk_metapath_ilock_parent_and_child(
	struct xchk_metapath	*mpath,
	struct xfs_inode	*ip)
{
	struct xfs_scrub	*sc = mpath->sc;
	int			error = 0;

	while (true) {
		xfs_ilock(mpath->dp, XFS_ILOCK_EXCL);
		if (!ip || xfs_ilock_nowait(ip, XFS_ILOCK_EXCL))
			return 0;
		xfs_iunlock(mpath->dp, XFS_ILOCK_EXCL);

		if (xchk_should_terminate(sc, &error))
			return error;

		delay(1);
	}

	ASSERT(0);
	return -EINTR;
}

/*
 * Try to remove a dirent in @mpath->dp with the name @mpath->xname that points
 * to @alleged_child.  Returns:
 *
 * 0 if there is no longer a dirent;
 * -EEXIST if the dirent points to @sc->ip;
 * -EAGAIN and an updated @alleged_child if the dirent points elsewhere; or
 * A negative errno for any other error.
 */
STATIC int
xrep_metapath_try_unlink(
	struct xchk_metapath	*mpath,
	xfs_ino_t		*alleged_child)
{
	struct xfs_scrub	*sc = mpath->sc;
	struct xfs_inode	*ip = NULL;
	xfs_ino_t		ino;
	int			error;

	ASSERT(*alleged_child != sc->ip->i_ino);

	trace_xrep_metapath_try_unlink(sc, mpath->path, mpath->dp,
			*alleged_child);

	/*
	 * Allocate transaction, grab the alleged child inode, lock inodes,
	 * join to transaction.
	 */
	error = xchk_trans_alloc(sc, mpath->unlink_resblks);
	if (error)
		return error;

	error = xchk_iget(sc, *alleged_child, &ip);
	if (error == -EINVAL || error == -ENOENT) {
		/* inode number is bogus, junk the dirent */
		error = 0;
	}
	if (error) {
		xchk_trans_cancel(sc);
		return error;
	}

	error = xchk_metapath_ilock_parent_and_child(mpath, ip);
	if (error) {
		xchk_trans_cancel(sc);
		return error;
	}
	xfs_trans_ijoin(sc->tp, mpath->dp, 0);
	if (ip)
		xfs_trans_ijoin(sc->tp, ip, 0);

	error = xchk_dir_lookup(sc, mpath->dp, &mpath->xname, &ino);
	trace_xrep_metapath_lookup(sc, mpath->path, mpath->dp, ino);
	if (error == -ENOENT) {
		/*
		 * There is no dirent in the directory anymore.  We're ready to
		 * try the link operation again.
		 */
		error = 0;
		goto out_cancel;
	}
	if (error)
		goto out_cancel;

	if (ino == sc->ip->i_ino) {
		/* The dirent already points to @sc->ip; we're done. */
		error = -EEXIST;
		goto out_cancel;
	}

	/*
	 * The dirent does not point to the alleged child.  Update the caller
	 * and signal that we want to be called again.
	 */
	if (ino != *alleged_child) {
		*alleged_child = ino;
		error = -EAGAIN;
		goto out_cancel;
	}

	/* Remove the link to the child. */
	error = xrep_metapath_unlink(mpath, ino, ip);
	if (error)
		goto out_cancel;

	error = xrep_trans_commit(sc);
	goto out_unlock;

out_cancel:
	xchk_trans_cancel(sc);
out_unlock:
	xfs_iunlock(mpath->dp, XFS_ILOCK_EXCL);
	if (ip) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xchk_irele(sc, ip);
	}
	return error;
}

/*
 * Make sure the metadata directory path points to the child being examined.
 *
 * Repair needs to be able to create a directory structure, create its own
 * transactions, and take ILOCKs.  This function /must/ be called after all
 * other repairs have completed.
 */
int
xrep_metapath(
	struct xfs_scrub	*sc)
{
	struct xchk_metapath	*mpath = sc->buf;
	struct xfs_mount	*mp = sc->mp;
	int			error = 0;

	/* Just probing, nothing to repair. */
	if (sc->sm->sm_ino == XFS_SCRUB_METAPATH_PROBE)
		return 0;

	/* Parent required to do anything else. */
	if (mpath->dp == NULL)
		return -EFSCORRUPTED;

	/*
	 * Make sure the child file actually has an attr fork to receive a new
	 * parent pointer if the fs has parent pointers.
	 */
	if (xfs_has_parent(mp)) {
		error = xfs_attr_add_fork(sc->ip,
				sizeof(struct xfs_attr_sf_hdr), 1);
		if (error)
			return error;
	}

	/* Compute block reservation required to unlink and link a file. */
	mpath->unlink_resblks = xfs_remove_space_res(mp, MAXNAMELEN);
	mpath->link_resblks = xfs_link_space_res(mp, MAXNAMELEN);

	do {
		xfs_ino_t	alleged_child;

		/* Re-establish the link, or tell us which inode to remove. */
		error = xrep_metapath_try_link(mpath, &alleged_child);
		if (!error)
			return 0;
		if (error != -EEXIST)
			return error;

		/*
		 * Remove an incorrect link to an alleged child, or tell us
		 * which inode to remove.
		 */
		do {
			error = xrep_metapath_try_unlink(mpath, &alleged_child);
		} while (error == -EAGAIN);
		if (error == -EEXIST) {
			/* Link established; we're done. */
			error = 0;
			break;
		}
	} while (!error);

	return error;
}
#endif /* CONFIG_XFS_ONLINE_REPAIR */
