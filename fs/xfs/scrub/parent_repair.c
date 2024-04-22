// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir2_priv.h"
#include "xfs_trans_space.h"
#include "xfs_health.h"
#include "xfs_exchmaps.h"
#include "xfs_parent.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/iscan.h"
#include "scrub/findparent.h"
#include "scrub/readdir.h"
#include "scrub/tempfile.h"
#include "scrub/orphanage.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/xfblob.h"

/*
 * Repairing The Directory Parent Pointer
 * ======================================
 *
 * Currently, only directories support parent pointers (in the form of '..'
 * entries), so we simply scan the filesystem and update the '..' entry.
 *
 * Note that because the only parent pointer is the dotdot entry, we won't
 * touch an unhealthy directory, since the directory repair code is perfectly
 * capable of rebuilding a directory with the proper parent inode.
 *
 * See the section on locking issues in dir_repair.c for more information about
 * conflicts with the VFS.  The findparent code wll keep our incore parent
 * inode up to date.
 *
 * If parent pointers are enabled, we instead reconstruct the parent pointer
 * information by visiting every directory entry of every directory in the
 * system and translating the relevant dirents into parent pointers.  In this
 * case, it is advantageous to stash all parent pointers created from dirents
 * from a single parent file before replaying them into the temporary file.  To
 * save memory, the live filesystem scan reuses the findparent object.  Parent
 * pointer repair chooses either directory scanning or findparent, but not
 * both.
 *
 * When salvaging completes, the remaining stashed entries are replayed to the
 * temporary file.  All non-parent pointer extended attributes are copied to
 * the temporary file's extended attributes.  An atomic extent swap is used to
 * commit the new directory blocks to the directory being repaired.  This will
 * disrupt attrmulti cursors.
 */

/* A stashed parent pointer update. */
struct xrep_pptr {
	/* Cookie for retrieval of the pptr name. */
	xfblob_cookie		name_cookie;

	/* Parent pointer record. */
	struct xfs_parent_rec	pptr_rec;

	/* Length of the pptr name. */
	uint8_t			namelen;
};

/*
 * Stash up to 8 pages of recovered parent pointers in pptr_recs and
 * pptr_names before we write them to the temp file.
 */
#define XREP_PARENT_MAX_STASH_BYTES	(PAGE_SIZE * 8)

struct xrep_parent {
	struct xfs_scrub	*sc;

	/* Fixed-size array of xrep_pptr structures. */
	struct xfarray		*pptr_recs;

	/* Blobs containing parent pointer names. */
	struct xfblob		*pptr_names;

	/*
	 * Information used to scan the filesystem to find the inumber of the
	 * dotdot entry for this directory.  On filesystems without parent
	 * pointers, we use the findparent_* functions on this object and
	 * access only the parent_ino field directly.
	 *
	 * When parent pointers are enabled, the directory entry scanner uses
	 * the iscan, hooks, and lock fields of this object directly.
	 * @pscan.lock coordinates access to pptr_recs, pptr_names, pptr, and
	 * pptr_scratch.  This reduces the memory requirements of this
	 * structure.
	 */
	struct xrep_parent_scan_info pscan;

	/* Orphanage reparenting request. */
	struct xrep_adoption	adoption;

	/* Directory entry name, plus the trailing null. */
	struct xfs_name		xname;
	unsigned char		namebuf[MAXNAMELEN];

	/* Scratch buffer for scanning pptr xattrs */
	struct xfs_da_args	pptr_args;
};

/* Tear down all the incore stuff we created. */
static void
xrep_parent_teardown(
	struct xrep_parent	*rp)
{
	xrep_findparent_scan_teardown(&rp->pscan);
	if (rp->pptr_names)
		xfblob_destroy(rp->pptr_names);
	rp->pptr_names = NULL;
	if (rp->pptr_recs)
		xfarray_destroy(rp->pptr_recs);
	rp->pptr_recs = NULL;
}

/* Set up for a parent repair. */
int
xrep_setup_parent(
	struct xfs_scrub	*sc)
{
	struct xrep_parent	*rp;
	int			error;

	xchk_fsgates_enable(sc, XCHK_FSGATES_DIRENTS);

	rp = kvzalloc(sizeof(struct xrep_parent), XCHK_GFP_FLAGS);
	if (!rp)
		return -ENOMEM;
	rp->sc = sc;
	rp->xname.name = rp->namebuf;
	sc->buf = rp;

	error = xrep_tempfile_create(sc, S_IFREG);
	if (error)
		return error;

	return xrep_orphanage_try_create(sc);
}

/*
 * Scan all files in the filesystem for a child dirent that we can turn into
 * the dotdot entry for this directory.
 */
STATIC int
xrep_parent_find_dotdot(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		ino;
	unsigned int		sick, checked;
	int			error;

	/*
	 * Avoid sick directories.  There shouldn't be anyone else clearing the
	 * directory's sick status.
	 */
	xfs_inode_measure_sickness(sc->ip, &sick, &checked);
	if (sick & XFS_SICK_INO_DIR)
		return -EFSCORRUPTED;

	ino = xrep_findparent_self_reference(sc);
	if (ino != NULLFSINO) {
		xrep_findparent_scan_finish_early(&rp->pscan, ino);
		return 0;
	}

	/*
	 * Drop the ILOCK on this directory so that we can scan for the dotdot
	 * entry.  Figure out who is going to be the parent of this directory,
	 * then retake the ILOCK so that we can salvage directory entries.
	 */
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* Does the VFS dcache have an answer for us? */
	ino = xrep_findparent_from_dcache(sc);
	if (ino != NULLFSINO) {
		error = xrep_findparent_confirm(sc, &ino);
		if (!error && ino != NULLFSINO) {
			xrep_findparent_scan_finish_early(&rp->pscan, ino);
			goto out_relock;
		}
	}

	/* Scan the entire filesystem for a parent. */
	error = xrep_findparent_scan(&rp->pscan);
out_relock:
	xchk_ilock(sc, XFS_ILOCK_EXCL);

	return error;
}

/*
 * Add this stashed incore parent pointer to the temporary file.
 * The caller must hold the tempdir's IOLOCK, must not hold any ILOCKs, and
 * must not be in transaction context.
 */
STATIC int
xrep_parent_replay_update(
	struct xrep_parent	*rp,
	const struct xfs_name	*xname,
	struct xrep_pptr	*pptr)
{
	struct xfs_scrub	*sc = rp->sc;

	/* Create parent pointer. */
	trace_xrep_parent_replay_parentadd(sc->tempip, xname, &pptr->pptr_rec);

	return xfs_parent_set(sc->tempip, sc->ip->i_ino, xname,
			&pptr->pptr_rec, &rp->pptr_args);
}

/*
 * Flush stashed parent pointer updates that have been recorded by the scanner.
 * This is done to reduce the memory requirements of the parent pointer
 * rebuild, since files can have a lot of hardlinks and the fs can be busy.
 *
 * Caller must not hold transactions or ILOCKs.  Caller must hold the tempfile
 * IOLOCK.
 */
STATIC int
xrep_parent_replay_updates(
	struct xrep_parent	*rp)
{
	xfarray_idx_t		array_cur;
	int			error;

	mutex_lock(&rp->pscan.lock);
	foreach_xfarray_idx(rp->pptr_recs, array_cur) {
		struct xrep_pptr	pptr;

		error = xfarray_load(rp->pptr_recs, array_cur, &pptr);
		if (error)
			goto out_unlock;

		error = xfblob_loadname(rp->pptr_names, pptr.name_cookie,
				&rp->xname, pptr.namelen);
		if (error)
			goto out_unlock;
		rp->xname.len = pptr.namelen;
		mutex_unlock(&rp->pscan.lock);

		error = xrep_parent_replay_update(rp, &rp->xname, &pptr);
		if (error)
			return error;

		mutex_lock(&rp->pscan.lock);
	}

	/* Empty out both arrays now that we've added the entries. */
	xfarray_truncate(rp->pptr_recs);
	xfblob_truncate(rp->pptr_names);
	mutex_unlock(&rp->pscan.lock);
	return 0;
out_unlock:
	mutex_unlock(&rp->pscan.lock);
	return error;
}

/*
 * Remember that we want to create a parent pointer in the tempfile.  These
 * stashed actions will be replayed later.
 */
STATIC int
xrep_parent_stash_parentadd(
	struct xrep_parent	*rp,
	const struct xfs_name	*name,
	const struct xfs_inode	*dp)
{
	struct xrep_pptr	pptr = {
		.namelen	= name->len,
	};
	int			error;

	trace_xrep_parent_stash_parentadd(rp->sc->tempip, dp, name);

	xfs_inode_to_parent_rec(&pptr.pptr_rec, dp);
	error = xfblob_storename(rp->pptr_names, &pptr.name_cookie, name);
	if (error)
		return error;

	return xfarray_append(rp->pptr_recs, &pptr);
}

/*
 * Examine an entry of a directory.  If this dirent leads us back to the file
 * whose parent pointers we're rebuilding, add a pptr to the temporary
 * directory.
 */
STATIC int
xrep_parent_scan_dirent(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xfs_dir2_dataptr_t	dapos,
	const struct xfs_name	*name,
	xfs_ino_t		ino,
	void			*priv)
{
	struct xrep_parent	*rp = priv;
	int			error;

	/* Dirent doesn't point to this directory. */
	if (ino != rp->sc->ip->i_ino)
		return 0;

	/* No weird looking names. */
	if (name->len == 0 || !xfs_dir2_namecheck(name->name, name->len))
		return -EFSCORRUPTED;

	/* No mismatching ftypes. */
	if (name->type != xfs_mode_to_ftype(VFS_I(sc->ip)->i_mode))
		return -EFSCORRUPTED;

	/* Don't pick up dot or dotdot entries; we only want child dirents. */
	if (xfs_dir2_samename(name, &xfs_name_dotdot) ||
	    xfs_dir2_samename(name, &xfs_name_dot))
		return 0;

	/*
	 * Transform this dirent into a parent pointer and queue it for later
	 * addition to the temporary file.
	 */
	mutex_lock(&rp->pscan.lock);
	error = xrep_parent_stash_parentadd(rp, name, dp);
	mutex_unlock(&rp->pscan.lock);
	return error;
}

/*
 * Decide if we want to look for dirents in this directory.  Skip the file
 * being repaired and any files being used to stage repairs.
 */
static inline bool
xrep_parent_want_scan(
	struct xrep_parent	*rp,
	const struct xfs_inode	*ip)
{
	return ip != rp->sc->ip && !xrep_is_tempfile(ip);
}

/*
 * Take ILOCK on a file that we want to scan.
 *
 * Select ILOCK_EXCL if the file is a directory with an unloaded data bmbt.
 * Otherwise, take ILOCK_SHARED.
 */
static inline unsigned int
xrep_parent_scan_ilock(
	struct xrep_parent	*rp,
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	/* Still need to take the shared ILOCK to advance the iscan cursor. */
	if (!xrep_parent_want_scan(rp, ip))
		goto lock;

	if (S_ISDIR(VFS_I(ip)->i_mode) && xfs_need_iread_extents(&ip->i_df)) {
		lock_mode = XFS_ILOCK_EXCL;
		goto lock;
	}

lock:
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

/*
 * Scan this file for relevant child dirents that point to the file whose
 * parent pointers we're rebuilding.
 */
STATIC int
xrep_parent_scan_file(
	struct xrep_parent	*rp,
	struct xfs_inode	*ip)
{
	unsigned int		lock_mode;
	int			error = 0;

	lock_mode = xrep_parent_scan_ilock(rp, ip);

	if (!xrep_parent_want_scan(rp, ip))
		goto scan_done;

	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		/*
		 * If the directory looks as though it has been zapped by the
		 * inode record repair code, we cannot scan for child dirents.
		 */
		if (xchk_dir_looks_zapped(ip)) {
			error = -EBUSY;
			goto scan_done;
		}

		error = xchk_dir_walk(rp->sc, ip, xrep_parent_scan_dirent, rp);
		if (error)
			goto scan_done;
	}

scan_done:
	xchk_iscan_mark_visited(&rp->pscan.iscan, ip);
	xfs_iunlock(ip, lock_mode);
	return error;
}

/* Decide if we've stashed too much pptr data in memory. */
static inline bool
xrep_parent_want_flush_stashed(
	struct xrep_parent	*rp)
{
	unsigned long long	bytes;

	bytes = xfarray_bytes(rp->pptr_recs) + xfblob_bytes(rp->pptr_names);
	return bytes > XREP_PARENT_MAX_STASH_BYTES;
}

/*
 * Scan all directories in the filesystem to look for dirents that we can turn
 * into parent pointers.
 */
STATIC int
xrep_parent_scan_dirtree(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	struct xfs_inode	*ip;
	int			error;

	/*
	 * Filesystem scans are time consuming.  Drop the file ILOCK and all
	 * other resources for the duration of the scan and hope for the best.
	 * The live update hooks will keep our scan information up to date.
	 */
	xchk_trans_cancel(sc);
	if (sc->ilock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL))
		xchk_iunlock(sc, sc->ilock_flags & (XFS_ILOCK_SHARED |
						    XFS_ILOCK_EXCL));
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	while ((error = xchk_iscan_iter(&rp->pscan.iscan, &ip)) == 1) {
		bool		flush;

		error = xrep_parent_scan_file(rp, ip);
		xchk_irele(sc, ip);
		if (error)
			break;

		/* Flush stashed pptr updates to constrain memory usage. */
		mutex_lock(&rp->pscan.lock);
		flush = xrep_parent_want_flush_stashed(rp);
		mutex_unlock(&rp->pscan.lock);
		if (flush) {
			xchk_trans_cancel(sc);

			error = xrep_tempfile_iolock_polled(sc);
			if (error)
				break;

			error = xrep_parent_replay_updates(rp);
			xrep_tempfile_iounlock(sc);
			if (error)
				break;

			error = xchk_trans_alloc_empty(sc);
			if (error)
				break;
		}

		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&rp->pscan.iscan);
	if (error) {
		/*
		 * If we couldn't grab an inode that was busy with a state
		 * change, change the error code so that we exit to userspace
		 * as quickly as possible.
		 */
		if (error == -EBUSY)
			return -ECANCELED;
		return error;
	}

	/*
	 * Cancel the empty transaction so that we can (later) use the atomic
	 * extent swap helpers to lock files and commit the new directory.
	 */
	xchk_trans_cancel(rp->sc);
	return 0;
}

/* Reset a directory's dotdot entry, if needed. */
STATIC int
xrep_parent_reset_dotdot(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		ino;
	unsigned int		spaceres;
	int			error = 0;

	ASSERT(sc->ilock_flags & XFS_ILOCK_EXCL);

	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &ino);
	if (error || ino == rp->pscan.parent_ino)
		return error;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	trace_xrep_parent_reset_dotdot(sc->ip, rp->pscan.parent_ino);

	/*
	 * Reserve more space just in case we have to expand the dir.  We're
	 * allowed to exceed quota to repair inconsistent metadata.
	 */
	spaceres = xfs_rename_space_res(sc->mp, 0, false, xfs_name_dotdot.len,
			false);
	error = xfs_trans_reserve_more_inode(sc->tp, sc->ip, spaceres, 0,
			true);
	if (error)
		return error;

	error = xfs_dir_replace(sc->tp, sc->ip, &xfs_name_dotdot,
			rp->pscan.parent_ino, spaceres);
	if (error)
		return error;

	/*
	 * Roll transaction to detach the inode from the transaction but retain
	 * ILOCK_EXCL.
	 */
	return xfs_trans_roll(&sc->tp);
}

/*
 * Move the current file to the orphanage.
 *
 * Caller must hold IOLOCK_EXCL on @sc->ip, and no other inode locks.  Upon
 * successful return, the scrub transaction will have enough extra reservation
 * to make the move; it will hold IOLOCK_EXCL and ILOCK_EXCL of @sc->ip and the
 * orphanage; and both inodes will be ijoined.
 */
STATIC int
xrep_parent_move_to_orphanage(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		orig_parent, new_parent;
	int			error;

	/*
	 * We are about to drop the ILOCK on sc->ip to lock the orphanage and
	 * prepare for the adoption.  Therefore, look up the old dotdot entry
	 * for sc->ip so that we can compare it after we re-lock sc->ip.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &orig_parent);
	if (error)
		return error;

	/*
	 * Drop the ILOCK on the scrub target and commit the transaction.
	 * Adoption computes its own resource requirements and gathers the
	 * necessary components.
	 */
	error = xrep_trans_commit(sc);
	if (error)
		return error;
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* If we can take the orphanage's iolock then we're ready to move. */
	if (!xrep_orphanage_ilock_nowait(sc, XFS_IOLOCK_EXCL)) {
		xchk_iunlock(sc, sc->ilock_flags);
		error = xrep_orphanage_iolock_two(sc);
		if (error)
			return error;
	}

	/* Grab transaction and ILOCK the two files. */
	error = xrep_adoption_trans_alloc(sc, &rp->adoption);
	if (error)
		return error;

	error = xrep_adoption_compute_name(&rp->adoption, &rp->xname);
	if (error)
		return error;

	/*
	 * Now that we've reacquired the ILOCK on sc->ip, look up the dotdot
	 * entry again.  If the parent changed or the child was unlinked while
	 * the child directory was unlocked, we don't need to move the child to
	 * the orphanage after all.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &new_parent);
	if (error)
		return error;

	/*
	 * Attach to the orphanage if we still have a linked directory and it
	 * hasn't been moved.
	 */
	if (orig_parent == new_parent && VFS_I(sc->ip)->i_nlink > 0) {
		error = xrep_adoption_move(&rp->adoption);
		if (error)
			return error;
	}

	/*
	 * Launder the scrub transaction so we can drop the orphanage ILOCK
	 * and IOLOCK.  Return holding the scrub target's ILOCK and IOLOCK.
	 */
	error = xrep_adoption_trans_roll(&rp->adoption);
	if (error)
		return error;

	xrep_orphanage_iunlock(sc, XFS_ILOCK_EXCL);
	xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
	return 0;
}

/*
 * Commit the new parent pointer structure (currently only the dotdot entry) to
 * the file that we're repairing.
 */
STATIC int
xrep_parent_rebuild_tree(
	struct xrep_parent	*rp)
{
	if (rp->pscan.parent_ino == NULLFSINO) {
		if (xrep_orphanage_can_adopt(rp->sc))
			return xrep_parent_move_to_orphanage(rp);
		return -EFSCORRUPTED;
	}

	return xrep_parent_reset_dotdot(rp);
}

/* Set up the filesystem scan so we can look for parents. */
STATIC int
xrep_parent_setup_scan(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	char			*descr;
	int			error;

	if (!xfs_has_parent(sc->mp))
		return xrep_findparent_scan_start(sc, &rp->pscan);

	/* Set up some staging memory for logging parent pointer updates. */
	descr = xchk_xfile_ino_descr(sc, "parent pointer entries");
	error = xfarray_create(descr, 0, sizeof(struct xrep_pptr),
			&rp->pptr_recs);
	kfree(descr);
	if (error)
		return error;

	descr = xchk_xfile_ino_descr(sc, "parent pointer names");
	error = xfblob_create(descr, &rp->pptr_names);
	kfree(descr);
	if (error)
		goto out_recs;

	error = xrep_findparent_scan_start(sc, &rp->pscan);
	if (error)
		goto out_names;

	return 0;

out_names:
	xfblob_destroy(rp->pptr_names);
	rp->pptr_names = NULL;
out_recs:
	xfarray_destroy(rp->pptr_recs);
	rp->pptr_recs = NULL;
	return error;
}

int
xrep_parent(
	struct xfs_scrub	*sc)
{
	struct xrep_parent	*rp = sc->buf;
	int			error;

	/*
	 * When the parent pointers feature is enabled, repairs are committed
	 * by atomically committing a new xattr structure and reaping the old
	 * attr fork.  Reaping requires rmap to be enabled.
	 */
	if (xfs_has_parent(sc->mp) && !xfs_has_rmapbt(sc->mp))
		return -EOPNOTSUPP;

	error = xrep_parent_setup_scan(rp);
	if (error)
		return error;

	if (xfs_has_parent(sc->mp))
		error = xrep_parent_scan_dirtree(rp);
	else
		error = xrep_parent_find_dotdot(rp);
	if (error)
		goto out_teardown;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto out_teardown;

	error = xrep_parent_rebuild_tree(rp);
	if (error)
		goto out_teardown;

out_teardown:
	xrep_parent_teardown(rp);
	return error;
}
