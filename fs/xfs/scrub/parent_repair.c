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
#include "xfs_attr.h"
#include "xfs_bmap.h"
#include "xfs_ag.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/iscan.h"
#include "scrub/findparent.h"
#include "scrub/readdir.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/orphanage.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/xfblob.h"
#include "scrub/attr_repair.h"
#include "scrub/listxattr.h"

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
 * the temporary file's extended attributes.  An atomic file mapping exchange
 * is used to commit the new xattr blocks to the file being repaired.  This
 * will disrupt attrmulti cursors.
 */

/* Create a parent pointer in the tempfile. */
#define XREP_PPTR_ADD		(1)

/* Remove a parent pointer from the tempfile. */
#define XREP_PPTR_REMOVE	(2)

/* A stashed parent pointer update. */
struct xrep_pptr {
	/* Cookie for retrieval of the pptr name. */
	xfblob_cookie		name_cookie;

	/* Parent pointer record. */
	struct xfs_parent_rec	pptr_rec;

	/* Length of the pptr name. */
	uint8_t			namelen;

	/* XREP_PPTR_{ADD,REMOVE} */
	uint8_t			action;
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

	/* xattr keys */
	struct xfarray		*xattr_records;

	/* xattr values */
	struct xfblob		*xattr_blobs;

	/* Scratch buffers for saving extended attributes */
	unsigned char		*xattr_name;
	void			*xattr_value;
	unsigned int		xattr_value_sz;

	/*
	 * Information used to exchange the attr fork mappings, if the fs
	 * supports parent pointers.
	 */
	struct xrep_tempexch	tx;

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
	 *
	 * The lock also controls access to xattr_records and xattr_blobs(?)
	 */
	struct xrep_parent_scan_info pscan;

	/* Orphanage reparenting request. */
	struct xrep_adoption	adoption;

	/* Directory entry name, plus the trailing null. */
	struct xfs_name		xname;
	unsigned char		namebuf[MAXNAMELEN];

	/* Scratch buffer for scanning pptr xattrs */
	struct xfs_da_args	pptr_args;

	/* Have we seen any live updates of parent pointers recently? */
	bool			saw_pptr_updates;

	/* Number of parents we found after all other repairs */
	unsigned long long	parents;
};

struct xrep_parent_xattr {
	/* Cookie for retrieval of the xattr name. */
	xfblob_cookie		name_cookie;

	/* Cookie for retrieval of the xattr value. */
	xfblob_cookie		value_cookie;

	/* XFS_ATTR_* flags */
	int			flags;

	/* Length of the value and name. */
	uint32_t		valuelen;
	uint16_t		namelen;
};

/*
 * Stash up to 8 pages of attrs in xattr_records/xattr_blobs before we write
 * them to the temp file.
 */
#define XREP_PARENT_XATTR_MAX_STASH_BYTES	(PAGE_SIZE * 8)

/* Tear down all the incore stuff we created. */
static void
xrep_parent_teardown(
	struct xrep_parent	*rp)
{
	xrep_findparent_scan_teardown(&rp->pscan);
	kvfree(rp->xattr_name);
	rp->xattr_name = NULL;
	kvfree(rp->xattr_value);
	rp->xattr_value = NULL;
	if (rp->xattr_blobs)
		xfblob_destroy(rp->xattr_blobs);
	rp->xattr_blobs = NULL;
	if (rp->xattr_records)
		xfarray_destroy(rp->xattr_records);
	rp->xattr_records = NULL;
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

	switch (pptr->action) {
	case XREP_PPTR_ADD:
		/* Create parent pointer. */
		trace_xrep_parent_replay_parentadd(sc->tempip, xname,
				&pptr->pptr_rec);

		return xfs_parent_set(sc->tempip, sc->ip->i_ino, xname,
				&pptr->pptr_rec, &rp->pptr_args);
	case XREP_PPTR_REMOVE:
		/* Remove parent pointer. */
		trace_xrep_parent_replay_parentremove(sc->tempip, xname,
				&pptr->pptr_rec);

		return xfs_parent_unset(sc->tempip, sc->ip->i_ino, xname,
				&pptr->pptr_rec, &rp->pptr_args);
	}

	ASSERT(0);
	return -EIO;
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
		.action		= XREP_PPTR_ADD,
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
 * Remember that we want to remove a parent pointer from the tempfile.  These
 * stashed actions will be replayed later.
 */
STATIC int
xrep_parent_stash_parentremove(
	struct xrep_parent	*rp,
	const struct xfs_name	*name,
	const struct xfs_inode	*dp)
{
	struct xrep_pptr	pptr = {
		.action		= XREP_PPTR_REMOVE,
		.namelen	= name->len,
	};
	int			error;

	trace_xrep_parent_stash_parentremove(rp->sc->tempip, dp, name);

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
	xchk_trans_alloc_empty(sc);

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

			xchk_trans_alloc_empty(sc);
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
	 * Retake sc->ip's ILOCK now that we're done flushing stashed parent
	 * pointers.  We end this function with an empty transaction and the
	 * ILOCK.
	 */
	xchk_ilock(rp->sc, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * Capture dirent updates being made by other threads which are relevant to the
 * file being repaired.
 */
STATIC int
xrep_parent_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_dir_update_params	*p = data;
	struct xrep_parent		*rp;
	struct xfs_scrub		*sc;
	int				error;

	rp = container_of(nb, struct xrep_parent, pscan.dhook.dirent_hook.nb);
	sc = rp->sc;

	/*
	 * This thread updated a dirent that points to the file that we're
	 * repairing, so stash the update for replay against the temporary
	 * file.
	 */
	if (p->ip->i_ino == sc->ip->i_ino &&
	    xchk_iscan_want_live_update(&rp->pscan.iscan, p->dp->i_ino)) {
		mutex_lock(&rp->pscan.lock);
		if (p->delta > 0)
			error = xrep_parent_stash_parentadd(rp, p->name, p->dp);
		else
			error = xrep_parent_stash_parentremove(rp, p->name,
					p->dp);
		if (!error)
			rp->saw_pptr_updates = true;
		mutex_unlock(&rp->pscan.lock);
		if (error)
			goto out_abort;
	}

	return NOTIFY_DONE;
out_abort:
	xchk_iscan_abort(&rp->pscan.iscan);
	return NOTIFY_DONE;
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

/* Pass back the parent inumber if this a parent pointer */
STATIC int
xrep_parent_lookup_pptr(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen,
	void			*priv)
{
	xfs_ino_t		*inop = priv;
	xfs_ino_t		parent_ino;
	int			error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, &parent_ino, NULL);
	if (error)
		return error;

	*inop = parent_ino;
	return -ECANCELED;
}

/*
 * Find the first parent of the scrub target by walking parent pointers for
 * the purpose of deciding if we're going to move it to the orphanage.
 * We don't care if the attr fork is zapped.
 */
STATIC int
xrep_parent_lookup_pptrs(
	struct xfs_scrub	*sc,
	xfs_ino_t		*inop)
{
	int			error;

	*inop = NULLFSINO;

	error = xchk_xattr_walk(sc, sc->ip, xrep_parent_lookup_pptr, NULL,
			inop);
	if (error && error != -ECANCELED)
		return error;
	return 0;
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

	if (S_ISDIR(VFS_I(sc->ip)->i_mode)) {
		/*
		 * We are about to drop the ILOCK on sc->ip to lock the
		 * orphanage and prepare for the adoption.  Therefore, look up
		 * the old dotdot entry for sc->ip so that we can compare it
		 * after we re-lock sc->ip.
		 */
		error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot,
				&orig_parent);
		if (error)
			return error;
	} else {
		/*
		 * We haven't dropped the ILOCK since we committed the new
		 * xattr structure (and hence the new parent pointer records),
		 * which means that the file cannot have been moved in the
		 * directory tree, and there are no parents.
		 */
		orig_parent = NULLFSINO;
	}

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
	 * the orphanage after all.  For a non-directory, we have to scan for
	 * the first parent pointer to see if one has been added.
	 */
	if (S_ISDIR(VFS_I(sc->ip)->i_mode))
		error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot,
				&new_parent);
	else
		error = xrep_parent_lookup_pptrs(sc, &new_parent);
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

/* Ensure that the xattr value buffer is large enough. */
STATIC int
xrep_parent_alloc_xattr_value(
	struct xrep_parent	*rp,
	size_t			bufsize)
{
	void			*new_val;

	if (rp->xattr_value_sz >= bufsize)
		return 0;

	if (rp->xattr_value) {
		kvfree(rp->xattr_value);
		rp->xattr_value = NULL;
		rp->xattr_value_sz = 0;
	}

	new_val = kvmalloc(bufsize, XCHK_GFP_FLAGS);
	if (!new_val)
		return -ENOMEM;

	rp->xattr_value = new_val;
	rp->xattr_value_sz = bufsize;
	return 0;
}

/* Retrieve the (remote) value of a non-pptr xattr. */
STATIC int
xrep_parent_fetch_xattr_remote(
	struct xrep_parent	*rp,
	struct xfs_inode	*ip,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	unsigned int		valuelen)
{
	struct xfs_scrub	*sc = rp->sc;
	struct xfs_da_args	args = {
		.attr_filter	= attr_flags & XFS_ATTR_NSP_ONDISK_MASK,
		.geo		= sc->mp->m_attr_geo,
		.whichfork	= XFS_ATTR_FORK,
		.dp		= ip,
		.name		= name,
		.namelen	= namelen,
		.trans		= sc->tp,
		.valuelen	= valuelen,
		.owner		= ip->i_ino,
	};
	int			error;

	/*
	 * If we need a larger value buffer, try to allocate one.  If that
	 * fails, return with -EDEADLOCK to try harder.
	 */
	error = xrep_parent_alloc_xattr_value(rp, valuelen);
	if (error == -ENOMEM)
		return -EDEADLOCK;
	if (error)
		return error;

	args.value = rp->xattr_value;
	xfs_attr_sethash(&args);
	return xfs_attr_get_ilocked(&args);
}

/* Stash non-pptr attributes for later replay into the temporary file. */
STATIC int
xrep_parent_stash_xattr(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen,
	void			*priv)
{
	struct xrep_parent_xattr key = {
		.valuelen	= valuelen,
		.namelen	= namelen,
		.flags		= attr_flags & XFS_ATTR_NSP_ONDISK_MASK,
	};
	struct xrep_parent	*rp = priv;
	int			error;

	if (attr_flags & (XFS_ATTR_INCOMPLETE | XFS_ATTR_PARENT))
		return 0;

	if (!value) {
		error = xrep_parent_fetch_xattr_remote(rp, ip, attr_flags,
				name, namelen, valuelen);
		if (error)
			return error;

		value = rp->xattr_value;
	}

	trace_xrep_parent_stash_xattr(rp->sc->tempip, key.flags, (void *)name,
			key.namelen, key.valuelen);

	error = xfblob_store(rp->xattr_blobs, &key.name_cookie, name,
			key.namelen);
	if (error)
		return error;

	error = xfblob_store(rp->xattr_blobs, &key.value_cookie, value,
			key.valuelen);
	if (error)
		return error;

	return xfarray_append(rp->xattr_records, &key);
}

/* Insert one xattr key/value. */
STATIC int
xrep_parent_insert_xattr(
	struct xrep_parent		*rp,
	const struct xrep_parent_xattr	*key)
{
	struct xfs_da_args		args = {
		.dp			= rp->sc->tempip,
		.attr_filter		= key->flags,
		.namelen		= key->namelen,
		.valuelen		= key->valuelen,
		.owner			= rp->sc->ip->i_ino,
		.geo			= rp->sc->mp->m_attr_geo,
		.whichfork		= XFS_ATTR_FORK,
		.op_flags		= XFS_DA_OP_OKNOENT,
	};
	int				error;

	ASSERT(!(key->flags & XFS_ATTR_PARENT));

	/*
	 * Grab pointers to the scrub buffer so that we can use them to insert
	 * attrs into the temp file.
	 */
	args.name = rp->xattr_name;
	args.value = rp->xattr_value;

	/*
	 * The attribute name is stored near the end of the in-core buffer,
	 * though we reserve one more byte to ensure null termination.
	 */
	rp->xattr_name[XATTR_NAME_MAX] = 0;

	error = xfblob_load(rp->xattr_blobs, key->name_cookie, rp->xattr_name,
			key->namelen);
	if (error)
		return error;

	error = xfblob_free(rp->xattr_blobs, key->name_cookie);
	if (error)
		return error;

	error = xfblob_load(rp->xattr_blobs, key->value_cookie, args.value,
			key->valuelen);
	if (error)
		return error;

	error = xfblob_free(rp->xattr_blobs, key->value_cookie);
	if (error)
		return error;

	rp->xattr_name[key->namelen] = 0;

	trace_xrep_parent_insert_xattr(rp->sc->tempip, key->flags,
			rp->xattr_name, key->namelen, key->valuelen);

	xfs_attr_sethash(&args);
	return xfs_attr_set(&args, XFS_ATTRUPDATE_UPSERT, false);
}

/*
 * Periodically flush salvaged attributes to the temporary file.  This is done
 * to reduce the memory requirements of the xattr rebuild because files can
 * contain millions of attributes.
 */
STATIC int
xrep_parent_flush_xattrs(
	struct xrep_parent	*rp)
{
	xfarray_idx_t		array_cur;
	int			error;

	/*
	 * Entering this function, the scrub context has a reference to the
	 * inode being repaired, the temporary file, and the empty scrub
	 * transaction that we created for the xattr scan.  We hold ILOCK_EXCL
	 * on the inode being repaired.
	 *
	 * To constrain kernel memory use, we occasionally flush salvaged
	 * xattrs from the xfarray and xfblob structures into the temporary
	 * file in preparation for exchanging the xattr structures at the end.
	 * Updating the temporary file requires a transaction, so we commit the
	 * scrub transaction and drop the ILOCK so that xfs_attr_set can
	 * allocate whatever transaction it wants.
	 *
	 * We still hold IOLOCK_EXCL on the inode being repaired, which
	 * prevents anyone from adding xattrs (or parent pointers) while we're
	 * flushing.
	 */
	xchk_trans_cancel(rp->sc);
	xchk_iunlock(rp->sc, XFS_ILOCK_EXCL);

	/*
	 * Take the IOLOCK of the temporary file while we modify xattrs.  This
	 * isn't strictly required because the temporary file is never revealed
	 * to userspace, but we follow the same locking rules.  We still hold
	 * sc->ip's IOLOCK.
	 */
	error = xrep_tempfile_iolock_polled(rp->sc);
	if (error)
		return error;

	/* Add all the salvaged attrs to the temporary file. */
	foreach_xfarray_idx(rp->xattr_records, array_cur) {
		struct xrep_parent_xattr	key;

		error = xfarray_load(rp->xattr_records, array_cur, &key);
		if (error)
			return error;

		error = xrep_parent_insert_xattr(rp, &key);
		if (error)
			return error;
	}

	/* Empty out both arrays now that we've added the entries. */
	xfarray_truncate(rp->xattr_records);
	xfblob_truncate(rp->xattr_blobs);

	xrep_tempfile_iounlock(rp->sc);

	/* Recreate the empty transaction and relock the inode. */
	xchk_trans_alloc_empty(rp->sc);
	xchk_ilock(rp->sc, XFS_ILOCK_EXCL);
	return 0;
}

/* Decide if we've stashed too much xattr data in memory. */
static inline bool
xrep_parent_want_flush_xattrs(
	struct xrep_parent	*rp)
{
	unsigned long long	bytes;

	bytes = xfarray_bytes(rp->xattr_records) +
		xfblob_bytes(rp->xattr_blobs);
	return bytes > XREP_PARENT_XATTR_MAX_STASH_BYTES;
}

/* Flush staged attributes to the temporary file if we're over the limit. */
STATIC int
xrep_parent_try_flush_xattrs(
	struct xfs_scrub	*sc,
	void			*priv)
{
	struct xrep_parent	*rp = priv;
	int			error;

	if (!xrep_parent_want_flush_xattrs(rp))
		return 0;

	error = xrep_parent_flush_xattrs(rp);
	if (error)
		return error;

	/*
	 * If there were any parent pointer updates to the xattr structure
	 * while we dropped the ILOCK, the xattr structure is now stale.
	 * Signal to the attr copy process that we need to start over, but
	 * this time without opportunistic attr flushing.
	 *
	 * This is unlikely to happen, so we're ok with restarting the copy.
	 */
	mutex_lock(&rp->pscan.lock);
	if (rp->saw_pptr_updates)
		error = -ESTALE;
	mutex_unlock(&rp->pscan.lock);
	return error;
}

/* Copy all the non-pptr extended attributes into the temporary file. */
STATIC int
xrep_parent_copy_xattrs(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	int			error;

	/*
	 * Clear the pptr updates flag.  We hold sc->ip ILOCKed, so there
	 * can't be any parent pointer updates in progress.
	 */
	mutex_lock(&rp->pscan.lock);
	rp->saw_pptr_updates = false;
	mutex_unlock(&rp->pscan.lock);

	/* Copy xattrs, stopping periodically to flush the incore buffers. */
	error = xchk_xattr_walk(sc, sc->ip, xrep_parent_stash_xattr,
			xrep_parent_try_flush_xattrs, rp);
	if (error && error != -ESTALE)
		return error;

	if (error == -ESTALE) {
		/*
		 * The xattr copy collided with a parent pointer update.
		 * Restart the copy, but this time hold the ILOCK all the way
		 * to the end to lock out any directory parent pointer updates.
		 */
		error = xchk_xattr_walk(sc, sc->ip, xrep_parent_stash_xattr,
				NULL, rp);
		if (error)
			return error;
	}

	/* Flush any remaining stashed xattrs to the temporary file. */
	if (xfarray_bytes(rp->xattr_records) == 0)
		return 0;

	return xrep_parent_flush_xattrs(rp);
}

/*
 * Ensure that @sc->ip and @sc->tempip both have attribute forks before we head
 * into the attr fork exchange transaction.  All files on a filesystem with
 * parent pointers must have an attr fork because the parent pointer code does
 * not itself add attribute forks.
 *
 * Note: Unlinkable unlinked files don't need one, but the overhead of having
 * an unnecessary attr fork is not justified by the additional code complexity
 * that would be needed to track that state correctly.
 */
STATIC int
xrep_parent_ensure_attr_fork(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	int			error;

	error = xfs_attr_add_fork(sc->tempip,
			sizeof(struct xfs_attr_sf_hdr), 1);
	if (error)
		return error;
	return xfs_attr_add_fork(sc->ip, sizeof(struct xfs_attr_sf_hdr), 1);
}

/*
 * Finish replaying stashed parent pointer updates, allocate a transaction for
 * exchanging extent mappings, and take the ILOCKs of both files before we
 * commit the new attribute structure.
 */
STATIC int
xrep_parent_finalize_tempfile(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	int			error;

	/*
	 * Repair relies on the ILOCK to quiesce all possible xattr updates.
	 * Replay all queued parent pointer updates into the tempfile before
	 * exchanging the contents, even if that means dropping the ILOCKs and
	 * the transaction.
	 */
	do {
		error = xrep_parent_replay_updates(rp);
		if (error)
			return error;

		error = xrep_parent_ensure_attr_fork(rp);
		if (error)
			return error;

		error = xrep_tempexch_trans_alloc(sc, XFS_ATTR_FORK, &rp->tx);
		if (error)
			return error;

		if (xfarray_length(rp->pptr_recs) == 0)
			break;

		xchk_trans_cancel(sc);
		xrep_tempfile_iunlock_both(sc);
	} while (!xchk_should_terminate(sc, &error));
	return error;
}

/*
 * Replay all the stashed parent pointers into the temporary file, copy all
 * the non-pptr xattrs from the file being repaired into the temporary file,
 * and exchange the attr fork contents atomically.
 */
STATIC int
xrep_parent_rebuild_pptrs(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		parent_ino = NULLFSINO;
	int			error;

	/*
	 * Copy non-ppttr xattrs from the file being repaired into the
	 * temporary file's xattr structure.  We hold sc->ip's IOLOCK, which
	 * prevents setxattr/removexattr calls from occurring, but renames
	 * update the parent pointers without holding IOLOCK.  If we detect
	 * stale attr structures, we restart the scan but only flush at the
	 * end.
	 */
	error = xrep_parent_copy_xattrs(rp);
	if (error)
		return error;

	/*
	 * Cancel the empty transaction that we used to walk and copy attrs,
	 * and drop the ILOCK so that we can take the IOLOCK on the temporary
	 * file.  We still hold sc->ip's IOLOCK.
	 */
	xchk_trans_cancel(sc);
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	error = xrep_tempfile_iolock_polled(sc);
	if (error)
		return error;

	/*
	 * Allocate transaction, lock inodes, and make sure that we've replayed
	 * all the stashed pptr updates to the tempdir.  After this point,
	 * we're ready to exchange the attr fork mappings.
	 */
	error = xrep_parent_finalize_tempfile(rp);
	if (error)
		return error;

	/* Last chance to abort before we start committing pptr fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	if (xchk_iscan_aborted(&rp->pscan.iscan))
		return -ECANCELED;

	/*
	 * Exchange the attr fork contents and junk the old attr fork contents,
	 * which are now in the tempfile.
	 */
	error = xrep_xattr_swap(sc, &rp->tx);
	if (error)
		return error;
	error = xrep_xattr_reset_tempfile_fork(sc);
	if (error)
		return error;

	/*
	 * Roll to get a transaction without any inodes joined to it.  Then we
	 * can drop the tempfile's ILOCK and IOLOCK before doing more work on
	 * the scrub target file.
	 */
	error = xfs_trans_roll(&sc->tp);
	if (error)
		return error;
	xrep_tempfile_iunlock(sc);
	xrep_tempfile_iounlock(sc);

	/*
	 * We've committed the new parent pointers.  Find at least one parent
	 * so that we can decide if we're moving this file to the orphanage.
	 * For this purpose, root directories are their own parents.
	 */
	if (xchk_inode_is_dirtree_root(sc->ip)) {
		xrep_findparent_scan_found(&rp->pscan, sc->ip->i_ino);
	} else {
		error = xrep_parent_lookup_pptrs(sc, &parent_ino);
		if (error)
			return error;
		if (parent_ino != NULLFSINO)
			xrep_findparent_scan_found(&rp->pscan, parent_ino);
	}
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
	struct xfs_scrub	*sc = rp->sc;
	bool			try_adoption;
	int			error;

	if (xfs_has_parent(sc->mp)) {
		error = xrep_parent_rebuild_pptrs(rp);
		if (error)
			return error;
	}

	/*
	 * Any file with no parent could be adopted.  This check happens after
	 * rebuilding the parent pointer structure because we might have cycled
	 * the ILOCK during that process.
	 */
	try_adoption = rp->pscan.parent_ino == NULLFSINO;

	/*
	 * Starting with metadir, we allow checking of parent pointers
	 * of non-directory files that are children of the superblock.
	 * Lack of parent is ok here.
	 */
	if (try_adoption && xfs_has_metadir(sc->mp) &&
	    xchk_inode_is_sb_rooted(sc->ip))
		try_adoption = false;

	if (try_adoption) {
		if (xrep_orphanage_can_adopt(sc))
			return xrep_parent_move_to_orphanage(rp);
		return -EFSCORRUPTED;

	}

	if (S_ISDIR(VFS_I(sc->ip)->i_mode))
		return xrep_parent_reset_dotdot(rp);

	return 0;
}

/* Count the number of parent pointers. */
STATIC int
xrep_parent_count_pptr(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen,
	void			*priv)
{
	struct xrep_parent	*rp = priv;
	int			error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, NULL, NULL);
	if (error)
		return error;

	rp->parents++;
	return 0;
}

/*
 * After all parent pointer rebuilding and adoption activity completes, reset
 * the link count of this nondirectory, having scanned the fs to rebuild all
 * parent pointers.
 */
STATIC int
xrep_parent_set_nondir_nlink(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	struct xfs_inode	*ip = sc->ip;
	struct xfs_perag	*pag;
	bool			joined = false;
	int			error;

	/* Count parent pointers so we can reset the file link count. */
	rp->parents = 0;
	error = xchk_xattr_walk(sc, ip, xrep_parent_count_pptr, NULL, rp);
	if (error)
		return error;

	/*
	 * Starting with metadir, we allow checking of parent pointers of
	 * non-directory files that are children of the superblock.  Pretend
	 * that we found a parent pointer attr.
	 */
	if (xfs_has_metadir(sc->mp) && xchk_inode_is_sb_rooted(sc->ip))
		rp->parents++;

	if (rp->parents > 0 && xfs_inode_on_unlinked_list(ip)) {
		xfs_trans_ijoin(sc->tp, sc->ip, 0);
		joined = true;

		/*
		 * The file is on the unlinked list but we found parents.
		 * Remove the file from the unlinked list.
		 */
		pag = xfs_perag_get(sc->mp, XFS_INO_TO_AGNO(sc->mp, ip->i_ino));
		if (!pag) {
			ASSERT(0);
			return -EFSCORRUPTED;
		}

		error = xfs_iunlink_remove(sc->tp, pag, ip);
		xfs_perag_put(pag);
		if (error)
			return error;
	} else if (rp->parents == 0 && !xfs_inode_on_unlinked_list(ip)) {
		xfs_trans_ijoin(sc->tp, sc->ip, 0);
		joined = true;

		/*
		 * The file is not on the unlinked list but we found no
		 * parents.  Add the file to the unlinked list.
		 */
		error = xfs_iunlink(sc->tp, ip);
		if (error)
			return error;
	}

	/* Set the correct link count. */
	if (VFS_I(ip)->i_nlink != rp->parents) {
		if (!joined) {
			xfs_trans_ijoin(sc->tp, sc->ip, 0);
			joined = true;
		}

		set_nlink(VFS_I(ip), min_t(unsigned long long, rp->parents,
					   XFS_NLINK_PINNED));
	}

	/* Log the inode to keep it moving forward if we dirtied anything. */
	if (joined)
		xfs_trans_log_inode(sc->tp, ip, XFS_ILOG_CORE);
	return 0;
}

/* Set up the filesystem scan so we can look for parents. */
STATIC int
xrep_parent_setup_scan(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	char			*descr;
	struct xfs_da_geometry	*geo = sc->mp->m_attr_geo;
	int			max_len;
	int			error;

	if (!xfs_has_parent(sc->mp))
		return xrep_findparent_scan_start(sc, &rp->pscan);

	/* Buffers for copying non-pptr attrs to the tempfile */
	rp->xattr_name = kvmalloc(XATTR_NAME_MAX + 1, XCHK_GFP_FLAGS);
	if (!rp->xattr_name)
		return -ENOMEM;

	/*
	 * Allocate enough memory to handle loading local attr values from the
	 * xfblob data while flushing stashed attrs to the temporary file.
	 * We only realloc the buffer when salvaging remote attr values, so
	 * TRY_HARDER means we allocate the maximal attr value size.
	 */
	if (sc->flags & XCHK_TRY_HARDER)
		max_len = XATTR_SIZE_MAX;
	else
		max_len = xfs_attr_leaf_entsize_local_max(geo->blksize);
	error = xrep_parent_alloc_xattr_value(rp, max_len);
	if (error)
		goto out_xattr_name;

	/* Set up some staging memory for logging parent pointer updates. */
	descr = xchk_xfile_ino_descr(sc, "parent pointer entries");
	error = xfarray_create(descr, 0, sizeof(struct xrep_pptr),
			&rp->pptr_recs);
	kfree(descr);
	if (error)
		goto out_xattr_value;

	descr = xchk_xfile_ino_descr(sc, "parent pointer names");
	error = xfblob_create(descr, &rp->pptr_names);
	kfree(descr);
	if (error)
		goto out_recs;

	/* Set up some storage for copying attrs before the mapping exchange */
	descr = xchk_xfile_ino_descr(sc,
				"parent pointer retained xattr entries");
	error = xfarray_create(descr, 0, sizeof(struct xrep_parent_xattr),
			&rp->xattr_records);
	kfree(descr);
	if (error)
		goto out_names;

	descr = xchk_xfile_ino_descr(sc,
				"parent pointer retained xattr values");
	error = xfblob_create(descr, &rp->xattr_blobs);
	kfree(descr);
	if (error)
		goto out_attr_keys;

	error = __xrep_findparent_scan_start(sc, &rp->pscan,
			xrep_parent_live_update);
	if (error)
		goto out_attr_values;

	return 0;

out_attr_values:
	xfblob_destroy(rp->xattr_blobs);
	rp->xattr_blobs = NULL;
out_attr_keys:
	xfarray_destroy(rp->xattr_records);
	rp->xattr_records = NULL;
out_names:
	xfblob_destroy(rp->pptr_names);
	rp->pptr_names = NULL;
out_recs:
	xfarray_destroy(rp->pptr_recs);
	rp->pptr_recs = NULL;
out_xattr_value:
	kvfree(rp->xattr_value);
	rp->xattr_value = NULL;
out_xattr_name:
	kvfree(rp->xattr_name);
	rp->xattr_name = NULL;
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
	 * attr fork.  Reaping requires rmap and exchange-range to be enabled.
	 */
	if (xfs_has_parent(sc->mp)) {
		if (!xfs_has_rmapbt(sc->mp))
			return -EOPNOTSUPP;
		if (!xfs_has_exchange_range(sc->mp))
			return -EOPNOTSUPP;
	}

	error = xrep_parent_setup_scan(rp);
	if (error)
		return error;

	if (xfs_has_parent(sc->mp))
		error = xrep_parent_scan_dirtree(rp);
	else
		error = xrep_parent_find_dotdot(rp);
	if (error)
		goto out_teardown;

	/* Last chance to abort before we start committing dotdot fixes. */
	if (xchk_should_terminate(sc, &error))
		goto out_teardown;

	error = xrep_parent_rebuild_tree(rp);
	if (error)
		goto out_teardown;
	if (xfs_has_parent(sc->mp) && !S_ISDIR(VFS_I(sc->ip)->i_mode)) {
		error = xrep_parent_set_nondir_nlink(rp);
		if (error)
			goto out_teardown;
	}

	error = xrep_defer_finish(sc);

out_teardown:
	xrep_parent_teardown(rp);
	return error;
}
