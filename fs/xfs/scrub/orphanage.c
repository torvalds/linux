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
#include "xfs_trans_space.h"
#include "xfs_dir2.h"
#include "xfs_icache.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"
#include "scrub/trace.h"
#include "scrub/orphanage.h"
#include "scrub/readdir.h"

#include <linux/namei.h>

/*
 * The Orphanage
 * =============
 *
 * If the directory tree is damaged, children of that directory become
 * inaccessible via that file path.  If a child has no other parents, the file
 * is said to be orphaned.  xfs_repair fixes this situation by creating a
 * orphanage directory (specifically, /lost+found) and creating a directory
 * entry pointing to the orphaned file.
 *
 * Online repair follows this tactic by creating a root-owned /lost+found
 * directory if one does not exist.  If an orphan is found, it will move that
 * files into orphanage.
 */

/* Make the orphanage owned by root. */
STATIC int
xrep_chown_orphanage(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp)
{
	struct xfs_trans	*tp;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_dquot	*udqp = NULL, *gdqp = NULL, *pdqp = NULL;
	struct xfs_dquot	*oldu = NULL, *oldg = NULL, *oldp = NULL;
	struct inode		*inode = VFS_I(dp);
	int			error;

	error = xfs_qm_vop_dqalloc(dp, GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, 0,
			XFS_QMOPT_QUOTALL, &udqp, &gdqp, &pdqp);
	if (error)
		return error;

	error = xfs_trans_alloc_ichange(dp, udqp, gdqp, pdqp, true, &tp);
	if (error)
		goto out_dqrele;

	/*
	 * Always clear setuid/setgid/sticky on the orphanage since we don't
	 * normally want that functionality on this directory and xfs_repair
	 * doesn't create it this way either.  Leave the other access bits
	 * unchanged.
	 */
	inode->i_mode &= ~(S_ISUID | S_ISGID | S_ISVTX);

	/*
	 * Change the ownerships and register quota modifications
	 * in the transaction.
	 */
	if (!uid_eq(inode->i_uid, GLOBAL_ROOT_UID)) {
		if (XFS_IS_UQUOTA_ON(mp))
			oldu = xfs_qm_vop_chown(tp, dp, &dp->i_udquot, udqp);
		inode->i_uid = GLOBAL_ROOT_UID;
	}
	if (!gid_eq(inode->i_gid, GLOBAL_ROOT_GID)) {
		if (XFS_IS_GQUOTA_ON(mp))
			oldg = xfs_qm_vop_chown(tp, dp, &dp->i_gdquot, gdqp);
		inode->i_gid = GLOBAL_ROOT_GID;
	}
	if (dp->i_projid != 0) {
		if (XFS_IS_PQUOTA_ON(mp))
			oldp = xfs_qm_vop_chown(tp, dp, &dp->i_pdquot, pdqp);
		dp->i_projid = 0;
	}

	dp->i_diflags &= ~(XFS_DIFLAG_REALTIME | XFS_DIFLAG_RTINHERIT);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	XFS_STATS_INC(mp, xs_ig_attrchg);

	if (xfs_has_wsync(mp))
		xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp);

	xfs_qm_dqrele(oldu);
	xfs_qm_dqrele(oldg);
	xfs_qm_dqrele(oldp);

out_dqrele:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);
	return error;
}

#define ORPHANAGE	"lost+found"

/* Create the orphanage directory, and set sc->orphanage to it. */
int
xrep_orphanage_create(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct dentry		*root_dentry, *orphanage_dentry;
	struct inode		*root_inode = VFS_I(sc->mp->m_rootip);
	struct inode		*orphanage_inode;
	int			error;

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_is_readonly(mp)) {
		sc->orphanage = NULL;
		return 0;
	}

	ASSERT(sc->tp == NULL);
	ASSERT(sc->orphanage == NULL);

	/* Find the dentry for the root directory... */
	root_dentry = d_find_alias(root_inode);
	if (!root_dentry) {
		error = -EFSCORRUPTED;
		goto out;
	}

	/* ...which is a directory, right? */
	if (!d_is_dir(root_dentry)) {
		error = -EFSCORRUPTED;
		goto out_dput_root;
	}

	/* Try to find the orphanage directory. */
	inode_lock_nested(root_inode, I_MUTEX_PARENT);
	orphanage_dentry = lookup_one_len(ORPHANAGE, root_dentry,
			strlen(ORPHANAGE));
	if (IS_ERR(orphanage_dentry)) {
		error = PTR_ERR(orphanage_dentry);
		goto out_unlock_root;
	}

	/*
	 * Nothing found?  Call mkdir to create the orphanage.  Create the
	 * directory without other-user access because we're live and someone
	 * could have been relying partly on minimal access to a parent
	 * directory to control access to a file we put in here.
	 */
	if (d_really_is_negative(orphanage_dentry)) {
		error = vfs_mkdir(&nop_mnt_idmap, root_inode, orphanage_dentry,
				0750);
		if (error)
			goto out_dput_orphanage;
	}

	/* Not a directory? Bail out. */
	if (!d_is_dir(orphanage_dentry)) {
		error = -ENOTDIR;
		goto out_dput_orphanage;
	}

	/*
	 * Grab a reference to the orphanage.  This /should/ succeed since
	 * we hold the root directory locked and therefore nobody can delete
	 * the orphanage.
	 */
	orphanage_inode = igrab(d_inode(orphanage_dentry));
	if (!orphanage_inode) {
		error = -ENOENT;
		goto out_dput_orphanage;
	}

	/* Make sure the orphanage is owned by root. */
	error = xrep_chown_orphanage(sc, XFS_I(orphanage_inode));
	if (error)
		goto out_dput_orphanage;

	/* Stash the reference for later and bail out. */
	sc->orphanage = XFS_I(orphanage_inode);
	sc->orphanage_ilock_flags = 0;

out_dput_orphanage:
	dput(orphanage_dentry);
out_unlock_root:
	inode_unlock(VFS_I(sc->mp->m_rootip));
out_dput_root:
	dput(root_dentry);
out:
	return error;
}

void
xrep_orphanage_ilock(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	sc->orphanage_ilock_flags |= ilock_flags;
	xfs_ilock(sc->orphanage, ilock_flags);
}

bool
xrep_orphanage_ilock_nowait(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	if (xfs_ilock_nowait(sc->orphanage, ilock_flags)) {
		sc->orphanage_ilock_flags |= ilock_flags;
		return true;
	}

	return false;
}

void
xrep_orphanage_iunlock(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	xfs_iunlock(sc->orphanage, ilock_flags);
	sc->orphanage_ilock_flags &= ~ilock_flags;
}

/* Grab the IOLOCK of the orphanage and sc->ip. */
int
xrep_orphanage_iolock_two(
	struct xfs_scrub	*sc)
{
	int			error = 0;

	while (true) {
		if (xchk_should_terminate(sc, &error))
			return error;

		/*
		 * Normal XFS takes the IOLOCK before grabbing a transaction.
		 * Scrub holds a transaction, which means that we can't block
		 * on either IOLOCK.
		 */
		if (xrep_orphanage_ilock_nowait(sc, XFS_IOLOCK_EXCL)) {
			if (xchk_ilock_nowait(sc, XFS_IOLOCK_EXCL))
				break;
			xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
		}
		delay(1);
	}

	return 0;
}

/* Release the orphanage. */
void
xrep_orphanage_rele(
	struct xfs_scrub	*sc)
{
	if (!sc->orphanage)
		return;

	if (sc->orphanage_ilock_flags)
		xfs_iunlock(sc->orphanage, sc->orphanage_ilock_flags);

	xchk_irele(sc, sc->orphanage);
	sc->orphanage = NULL;
}

/* Adoption moves a file into /lost+found */

/* Can the orphanage adopt @sc->ip? */
bool
xrep_orphanage_can_adopt(
	struct xfs_scrub	*sc)
{
	ASSERT(sc->ip != NULL);

	if (!sc->orphanage)
		return false;
	if (sc->ip == sc->orphanage)
		return false;
	if (xfs_internal_inum(sc->mp, sc->ip->i_ino))
		return false;
	return true;
}

/*
 * Create a new transaction to send a child to the orphanage.
 *
 * Allocate a new transaction with sufficient disk space to handle the
 * adoption, take ILOCK_EXCL of the orphanage and sc->ip, joins them to the
 * transaction, and reserve quota to reparent the latter.  Caller must hold the
 * IOLOCK of the orphanage and sc->ip.
 */
int
xrep_adoption_trans_alloc(
	struct xfs_scrub	*sc,
	struct xrep_adoption	*adopt)
{
	struct xfs_mount	*mp = sc->mp;
	unsigned int		child_blkres = 0;
	int			error;

	ASSERT(sc->tp == NULL);
	ASSERT(sc->ip != NULL);
	ASSERT(sc->orphanage != NULL);
	ASSERT(sc->ilock_flags & XFS_IOLOCK_EXCL);
	ASSERT(sc->orphanage_ilock_flags & XFS_IOLOCK_EXCL);
	ASSERT(!(sc->ilock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)));
	ASSERT(!(sc->orphanage_ilock_flags &
				(XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)));

	/* Compute the worst case space reservation that we need. */
	adopt->sc = sc;
	adopt->orphanage_blkres = XFS_LINK_SPACE_RES(mp, MAXNAMELEN);
	if (S_ISDIR(VFS_I(sc->ip)->i_mode))
		child_blkres = XFS_RENAME_SPACE_RES(mp, xfs_name_dotdot.len);
	adopt->child_blkres = child_blkres;

	/*
	 * Allocate a transaction to link the child into the parent, along with
	 * enough disk space to handle expansion of both the orphanage and the
	 * dotdot entry of a child directory.
	 */
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_link,
			adopt->orphanage_blkres + adopt->child_blkres, 0, 0,
			&sc->tp);
	if (error)
		return error;

	xfs_lock_two_inodes(sc->orphanage, XFS_ILOCK_EXCL,
			    sc->ip, XFS_ILOCK_EXCL);
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	sc->orphanage_ilock_flags |= XFS_ILOCK_EXCL;

	xfs_trans_ijoin(sc->tp, sc->orphanage, 0);
	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	/*
	 * Reserve enough quota in the orphan directory to add the new name.
	 * Normally the orphanage should have user/group/project ids of zero
	 * and hence is not subject to quota enforcement, but we're allowed to
	 * exceed quota to reattach disconnected parts of the directory tree.
	 */
	error = xfs_trans_reserve_quota_nblks(sc->tp, sc->orphanage,
			adopt->orphanage_blkres, 0, true);
	if (error)
		goto out_cancel;

	/*
	 * Reserve enough quota in the child directory to change dotdot.
	 * Here we're also allowed to exceed file quota to repair inconsistent
	 * metadata.
	 */
	if (adopt->child_blkres) {
		error = xfs_trans_reserve_quota_nblks(sc->tp, sc->ip,
				adopt->child_blkres, 0, true);
		if (error)
			goto out_cancel;
	}

	return 0;
out_cancel:
	xchk_trans_cancel(sc);
	xrep_orphanage_iunlock(sc, XFS_ILOCK_EXCL);
	xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
	return error;
}

/*
 * Compute the xfs_name for the directory entry that we're adding to the
 * orphanage.  Caller must hold ILOCKs of sc->ip and the orphanage and must not
 * reuse namebuf until the adoption completes or is dissolved.
 */
int
xrep_adoption_compute_name(
	struct xrep_adoption	*adopt,
	struct xfs_name		*xname)
{
	struct xfs_scrub	*sc = adopt->sc;
	char			*namebuf = (void *)xname->name;
	xfs_ino_t		ino;
	unsigned int		incr = 0;
	int			error = 0;

	adopt->xname = xname;
	xname->len = snprintf(namebuf, MAXNAMELEN, "%llu", sc->ip->i_ino);
	xname->type = xfs_mode_to_ftype(VFS_I(sc->ip)->i_mode);

	/* Make sure the filename is unique in the lost+found. */
	error = xchk_dir_lookup(sc, sc->orphanage, xname, &ino);
	while (error == 0 && incr < 10000) {
		xname->len = snprintf(namebuf, MAXNAMELEN, "%llu.%u",
				sc->ip->i_ino, ++incr);
		error = xchk_dir_lookup(sc, sc->orphanage, xname, &ino);
	}
	if (error == 0) {
		/* We already have 10,000 entries in the orphanage? */
		return -EFSCORRUPTED;
	}

	if (error != -ENOENT)
		return error;
	return 0;
}

/*
 * Make sure the dcache does not have a positive dentry for the name we've
 * chosen.  The caller should have checked with the ondisk directory, so any
 * discrepancy is a sign that something is seriously wrong.
 */
static int
xrep_adoption_check_dcache(
	struct xrep_adoption	*adopt)
{
	struct qstr		qname = QSTR_INIT(adopt->xname->name,
						  adopt->xname->len);
	struct dentry		*d_orphanage, *d_child;
	int			error = 0;

	d_orphanage = d_find_alias(VFS_I(adopt->sc->orphanage));
	if (!d_orphanage)
		return 0;

	d_child = d_hash_and_lookup(d_orphanage, &qname);
	if (d_child) {
		trace_xrep_adoption_check_child(adopt->sc->mp, d_child);

		if (d_is_positive(d_child)) {
			ASSERT(d_is_negative(d_child));
			error = -EFSCORRUPTED;
		}

		dput(d_child);
	}

	dput(d_orphanage);
	if (error)
		return error;

	/*
	 * Do we need to update d_parent of the dentry for the file being
	 * repaired?  There shouldn't be a hashed dentry with a parent since
	 * the file had nonzero nlink but wasn't connected to any parent dir.
	 */
	d_child = d_find_alias(VFS_I(adopt->sc->ip));
	if (!d_child)
		return 0;

	trace_xrep_adoption_check_alias(adopt->sc->mp, d_child);

	if (d_child->d_parent && !d_unhashed(d_child)) {
		ASSERT(d_child->d_parent == NULL || d_unhashed(d_child));
		error = -EFSCORRUPTED;
	}

	dput(d_child);
	return error;
}

/*
 * Remove all negative dentries from the dcache.  There should not be any
 * positive entries, since we've maintained our lock on the orphanage
 * directory.
 */
static void
xrep_adoption_zap_dcache(
	struct xrep_adoption	*adopt)
{
	struct qstr		qname = QSTR_INIT(adopt->xname->name,
						  adopt->xname->len);
	struct dentry		*d_orphanage, *d_child;

	d_orphanage = d_find_alias(VFS_I(adopt->sc->orphanage));
	if (!d_orphanage)
		return;

	d_child = d_hash_and_lookup(d_orphanage, &qname);
	while (d_child != NULL) {
		trace_xrep_adoption_invalidate_child(adopt->sc->mp, d_child);

		ASSERT(d_is_negative(d_child));
		d_invalidate(d_child);
		dput(d_child);
		d_child = d_lookup(d_orphanage, &qname);
	}

	dput(d_orphanage);
}

/*
 * Move the current file to the orphanage under the computed name.
 *
 * Returns with a dirty transaction so that the caller can handle any other
 * work, such as fixing up unlinked lists or resetting link counts.
 */
int
xrep_adoption_move(
	struct xrep_adoption	*adopt)
{
	struct xfs_scrub	*sc = adopt->sc;
	bool			isdir = S_ISDIR(VFS_I(sc->ip)->i_mode);
	int			error;

	trace_xrep_adoption_reparent(sc->orphanage, adopt->xname,
			sc->ip->i_ino);

	error = xrep_adoption_check_dcache(adopt);
	if (error)
		return error;

	/* Create the new name in the orphanage. */
	error = xfs_dir_createname(sc->tp, sc->orphanage, adopt->xname,
			sc->ip->i_ino, adopt->orphanage_blkres);
	if (error)
		return error;

	/*
	 * Bump the link count of the orphanage if we just added a
	 * subdirectory, and update its timestamps.
	 */
	xfs_trans_ichgtime(sc->tp, sc->orphanage,
			XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	if (isdir)
		xfs_bumplink(sc->tp, sc->orphanage);
	xfs_trans_log_inode(sc->tp, sc->orphanage, XFS_ILOG_CORE);

	/* Replace the dotdot entry if the child is a subdirectory. */
	if (isdir) {
		error = xfs_dir_replace(sc->tp, sc->ip, &xfs_name_dotdot,
				sc->orphanage->i_ino, adopt->child_blkres);
		if (error)
			return error;
	}

	/*
	 * Notify dirent hooks that we moved the file to /lost+found, and
	 * finish all the deferred work so that we know the adoption is fully
	 * recorded in the log.
	 */
	xfs_dir_update_hook(sc->orphanage, sc->ip, 1, adopt->xname);

	/* Remove negative dentries from the lost+found's dcache */
	xrep_adoption_zap_dcache(adopt);
	return 0;
}

/*
 * Roll to a clean scrub transaction so that we can release the orphanage,
 * even if xrep_adoption_move was not called.
 *
 * Commits all the work and deferred ops attached to an adoption request and
 * rolls to a clean scrub transaction.  On success, returns 0 with the scrub
 * context holding a clean transaction with no inodes joined.  On failure,
 * returns negative errno with no scrub transaction.  All inode locks are
 * still held after this function returns.
 */
int
xrep_adoption_trans_roll(
	struct xrep_adoption	*adopt)
{
	struct xfs_scrub	*sc = adopt->sc;
	int			error;

	trace_xrep_adoption_trans_roll(sc->orphanage, sc->ip,
			!!(sc->tp->t_flags & XFS_TRANS_DIRTY));

	/* Finish all the deferred ops to commit all repairs. */
	error = xrep_defer_finish(sc);
	if (error)
		return error;

	/* Roll the transaction once more to detach the inodes. */
	return xfs_trans_roll(&sc->tp);
}
