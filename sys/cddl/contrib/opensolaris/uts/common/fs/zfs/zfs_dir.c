/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/uio.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/sunddi.h>
#include <sys/random.h>
#include <sys/policy.h>
#include <sys/kcondvar.h>
#include <sys/callb.h>
#include <sys/smp.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/fs/zfs.h>
#include <sys/zap.h>
#include <sys/dmu.h>
#include <sys/atomic.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/sa.h>
#include <sys/zfs_sa.h>
#include <sys/dnlc.h>
#include <sys/extdirent.h>

/*
 * zfs_match_find() is used by zfs_dirent_lookup() to peform zap lookups
 * of names after deciding which is the appropriate lookup interface.
 */
static int
zfs_match_find(zfsvfs_t *zfsvfs, znode_t *dzp, const char *name,
    matchtype_t mt, uint64_t *zoid)
{
	int error;

	if (zfsvfs->z_norm) {

		/*
		 * In the non-mixed case we only expect there would ever
		 * be one match, but we need to use the normalizing lookup.
		 */
		error = zap_lookup_norm(zfsvfs->z_os, dzp->z_id, name, 8, 1,
		    zoid, mt, NULL, 0, NULL);
	} else {
		error = zap_lookup(zfsvfs->z_os, dzp->z_id, name, 8, 1, zoid);
	}
	*zoid = ZFS_DIRENT_OBJ(*zoid);

	return (error);
}

/*
 * Look up a directory entry under a locked vnode.
 * dvp being locked gives us a guarantee that there are no concurrent
 * modification of the directory and, thus, if a node can be found in
 * the directory, then it must not be unlinked.
 *
 * Input arguments:
 *	dzp	- znode for directory
 *	name	- name of entry to lock
 *	flag	- ZNEW: if the entry already exists, fail with EEXIST.
 *		  ZEXISTS: if the entry does not exist, fail with ENOENT.
 *		  ZXATTR: we want dzp's xattr directory
 *
 * Output arguments:
 *	zpp	- pointer to the znode for the entry (NULL if there isn't one)
 *
 * Return value: 0 on success or errno on failure.
 *
 * NOTE: Always checks for, and rejects, '.' and '..'.
 */
int
zfs_dirent_lookup(znode_t *dzp, const char *name, znode_t **zpp, int flag)
{
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	matchtype_t	mt = 0;
	uint64_t	zoid;
	vnode_t		*vp = NULL;
	int		error = 0;

	ASSERT_VOP_LOCKED(ZTOV(dzp), __func__);

	*zpp = NULL;

	/*
	 * Verify that we are not trying to lock '.', '..', or '.zfs'
	 */
	if (name[0] == '.' &&
	    (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')) ||
	    zfs_has_ctldir(dzp) && strcmp(name, ZFS_CTLDIR_NAME) == 0)
		return (SET_ERROR(EEXIST));

	/*
	 * Case sensitivity and normalization preferences are set when
	 * the file system is created.  These are stored in the
	 * zfsvfs->z_case and zfsvfs->z_norm fields.  These choices
	 * affect how we perform zap lookups.
	 *
	 * When matching we may need to normalize & change case according to
	 * FS settings.
	 *
	 * Note that a normalized match is necessary for a case insensitive
	 * filesystem when the lookup request is not exact because normalization
	 * can fold case independent of normalizing code point sequences.
	 *
	 * See the table above zfs_dropname().
	 */
	if (zfsvfs->z_norm != 0) {
		mt = MT_NORMALIZE;

		/*
		 * Determine if the match needs to honor the case specified in
		 * lookup, and if so keep track of that so that during
		 * normalization we don't fold case.
		 */
		if (zfsvfs->z_case == ZFS_CASE_MIXED) {
			mt |= MT_MATCH_CASE;
		}
	}

	/*
	 * Only look in or update the DNLC if we are looking for the
	 * name on a file system that does not require normalization
	 * or case folding.  We can also look there if we happen to be
	 * on a non-normalizing, mixed sensitivity file system IF we
	 * are looking for the exact name.
	 *
	 * NB: we do not need to worry about this flag for ZFS_CASE_SENSITIVE
	 * because in that case MT_EXACT and MT_FIRST should produce exactly
	 * the same result.
	 */

	if (dzp->z_unlinked && !(flag & ZXATTR))
		return (ENOENT);
	if (flag & ZXATTR) {
		error = sa_lookup(dzp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs), &zoid,
		    sizeof (zoid));
		if (error == 0)
			error = (zoid == 0 ? ENOENT : 0);
	} else {
		error = zfs_match_find(zfsvfs, dzp, name, mt, &zoid);
	}
	if (error) {
		if (error != ENOENT || (flag & ZEXISTS)) {
			return (error);
		}
	} else {
		if (flag & ZNEW) {
			return (SET_ERROR(EEXIST));
		}
		error = zfs_zget(zfsvfs, zoid, zpp);
		if (error)
			return (error);
		ASSERT(!(*zpp)->z_unlinked);
	}

	return (0);
}

static int
zfs_dd_lookup(znode_t *dzp, znode_t **zpp)
{
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	znode_t *zp;
	uint64_t parent;
	int error;

	ASSERT_VOP_LOCKED(ZTOV(dzp), __func__);
	ASSERT(RRM_READ_HELD(&zfsvfs->z_teardown_lock));

	if (dzp->z_unlinked)
		return (ENOENT);

	if ((error = sa_lookup(dzp->z_sa_hdl,
	    SA_ZPL_PARENT(zfsvfs), &parent, sizeof (parent))) != 0)
		return (error);

	error = zfs_zget(zfsvfs, parent, &zp);
	if (error == 0)
		*zpp = zp;
	return (error);
}

int
zfs_dirlook(znode_t *dzp, const char *name, znode_t **zpp)
{
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	znode_t *zp;
	int error = 0;

	ASSERT_VOP_LOCKED(ZTOV(dzp), __func__);
	ASSERT(RRM_READ_HELD(&zfsvfs->z_teardown_lock));

	if (dzp->z_unlinked)
		return (SET_ERROR(ENOENT));

	if (name[0] == 0 || (name[0] == '.' && name[1] == 0)) {
		*zpp = dzp;
	} else if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
		error = zfs_dd_lookup(dzp, zpp);
	} else {
		error = zfs_dirent_lookup(dzp, name, &zp, ZEXISTS);
		if (error == 0) {
			dzp->z_zn_prefetch = B_TRUE; /* enable prefetching */
			*zpp = zp;
		}
	}
	return (error);
}

/*
 * unlinked Set (formerly known as the "delete queue") Error Handling
 *
 * When dealing with the unlinked set, we dmu_tx_hold_zap(), but we
 * don't specify the name of the entry that we will be manipulating.  We
 * also fib and say that we won't be adding any new entries to the
 * unlinked set, even though we might (this is to lower the minimum file
 * size that can be deleted in a full filesystem).  So on the small
 * chance that the nlink list is using a fat zap (ie. has more than
 * 2000 entries), we *may* not pre-read a block that's needed.
 * Therefore it is remotely possible for some of the assertions
 * regarding the unlinked set below to fail due to i/o error.  On a
 * nondebug system, this will result in the space being leaked.
 */
void
zfs_unlinked_add(znode_t *zp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ASSERT(zp->z_unlinked);
	ASSERT(zp->z_links == 0);

	VERIFY3U(0, ==,
	    zap_add_int(zfsvfs->z_os, zfsvfs->z_unlinkedobj, zp->z_id, tx));
}

/*
 * Clean up any znodes that had no links when we either crashed or
 * (force) umounted the file system.
 */
void
zfs_unlinked_drain(zfsvfs_t *zfsvfs)
{
	zap_cursor_t	zc;
	zap_attribute_t zap;
	dmu_object_info_t doi;
	znode_t		*zp;
	dmu_tx_t	*tx;
	int		error;

	/*
	 * Interate over the contents of the unlinked set.
	 */
	for (zap_cursor_init(&zc, zfsvfs->z_os, zfsvfs->z_unlinkedobj);
	    zap_cursor_retrieve(&zc, &zap) == 0;
	    zap_cursor_advance(&zc)) {

		/*
		 * See what kind of object we have in list
		 */

		error = dmu_object_info(zfsvfs->z_os,
		    zap.za_first_integer, &doi);
		if (error != 0)
			continue;

		ASSERT((doi.doi_type == DMU_OT_PLAIN_FILE_CONTENTS) ||
		    (doi.doi_type == DMU_OT_DIRECTORY_CONTENTS));
		/*
		 * We need to re-mark these list entries for deletion,
		 * so we pull them back into core and set zp->z_unlinked.
		 */
		error = zfs_zget(zfsvfs, zap.za_first_integer, &zp);

		/*
		 * We may pick up znodes that are already marked for deletion.
		 * This could happen during the purge of an extended attribute
		 * directory.  All we need to do is skip over them, since they
		 * are already in the system marked z_unlinked.
		 */
		if (error != 0)
			continue;

		vn_lock(ZTOV(zp), LK_EXCLUSIVE | LK_RETRY);
#if defined(__FreeBSD__)
		/*
		 * Due to changes in zfs_rmnode we need to make sure the
		 * link count is set to zero here.
		 */
		if (zp->z_links != 0) {
			tx = dmu_tx_create(zfsvfs->z_os);
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
			error = dmu_tx_assign(tx, TXG_WAIT);
			if (error != 0) {
				dmu_tx_abort(tx);
				vput(ZTOV(zp));
				continue;
			}
			zp->z_links = 0;
			VERIFY0(sa_update(zp->z_sa_hdl, SA_ZPL_LINKS(zfsvfs),
			    &zp->z_links, sizeof (zp->z_links), tx));
			dmu_tx_commit(tx);
		}
#endif
		zp->z_unlinked = B_TRUE;
		vput(ZTOV(zp));
	}
	zap_cursor_fini(&zc);
}

/*
 * Delete the entire contents of a directory.  Return a count
 * of the number of entries that could not be deleted. If we encounter
 * an error, return a count of at least one so that the directory stays
 * in the unlinked set.
 *
 * NOTE: this function assumes that the directory is inactive,
 *	so there is no need to lock its entries before deletion.
 *	Also, it assumes the directory contents is *only* regular
 *	files.
 */
static int
zfs_purgedir(znode_t *dzp)
{
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	znode_t		*xzp;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	int skipped = 0;
	int error;

	for (zap_cursor_init(&zc, zfsvfs->z_os, dzp->z_id);
	    (error = zap_cursor_retrieve(&zc, &zap)) == 0;
	    zap_cursor_advance(&zc)) {
		error = zfs_zget(zfsvfs,
		    ZFS_DIRENT_OBJ(zap.za_first_integer), &xzp);
		if (error) {
			skipped += 1;
			continue;
		}

		vn_lock(ZTOV(xzp), LK_EXCLUSIVE | LK_RETRY);
		ASSERT((ZTOV(xzp)->v_type == VREG) ||
		    (ZTOV(xzp)->v_type == VLNK));

		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
		dmu_tx_hold_zap(tx, dzp->z_id, FALSE, zap.za_name);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
		/* Is this really needed ? */
		zfs_sa_upgrade_txholds(tx, xzp);
		dmu_tx_mark_netfree(tx);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			vput(ZTOV(xzp));
			skipped += 1;
			continue;
		}

		error = zfs_link_destroy(dzp, zap.za_name, xzp, tx, 0, NULL);
		if (error)
			skipped += 1;
		dmu_tx_commit(tx);

		vput(ZTOV(xzp));
	}
	zap_cursor_fini(&zc);
	if (error != ENOENT)
		skipped += 1;
	return (skipped);
}

#if defined(__FreeBSD__)
extern taskq_t *zfsvfs_taskq;
#endif

void
zfs_rmnode(znode_t *zp)
{
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os = zfsvfs->z_os;
	dmu_tx_t	*tx;
	uint64_t	acl_obj;
	uint64_t	xattr_obj;
	int		error;

	ASSERT(zp->z_links == 0);
	ASSERT_VOP_ELOCKED(ZTOV(zp), __func__);

	/*
	 * If this is an attribute directory, purge its contents.
	 */
	if (ZTOV(zp) != NULL && ZTOV(zp)->v_type == VDIR &&
	    (zp->z_pflags & ZFS_XATTR)) {
		if (zfs_purgedir(zp) != 0) {
			/*
			 * Not enough space to delete some xattrs.
			 * Leave it in the unlinked set.
			 */
			zfs_znode_dmu_fini(zp);
			zfs_znode_free(zp);
			return;
		}
	} else {
		/*
		 * Free up all the data in the file.  We don't do this for
		 * XATTR directories because we need truncate and remove to be
		 * in the same tx, like in zfs_znode_delete(). Otherwise, if
		 * we crash here we'll end up with an inconsistent truncated
		 * zap object in the delete queue.  Note a truncated file is
		 * harmless since it only contains user data.
		 */
		error = dmu_free_long_range(os, zp->z_id, 0, DMU_OBJECT_END);
		if (error) {
			/*
			 * Not enough space or we were interrupted by unmount.
			 * Leave the file in the unlinked set.
			 */
			zfs_znode_dmu_fini(zp);
			zfs_znode_free(zp);
			return;
		}
	}

	/*
	 * If the file has extended attributes, we're going to unlink
	 * the xattr dir.
	 */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error)
		xattr_obj = 0;

	acl_obj = zfs_external_acl(zp);

	/*
	 * Set up the final transaction.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_free(tx, zp->z_id, 0, DMU_OBJECT_END);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	if (xattr_obj)
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, TRUE, NULL);
	if (acl_obj)
		dmu_tx_hold_free(tx, acl_obj, 0, DMU_OBJECT_END);

	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		/*
		 * Not enough space to delete the file.  Leave it in the
		 * unlinked set, leaking it until the fs is remounted (at
		 * which point we'll call zfs_unlinked_drain() to process it).
		 */
		dmu_tx_abort(tx);
		zfs_znode_dmu_fini(zp);
		zfs_znode_free(zp);
		return;
	}

#if defined(__FreeBSD__)
	/*
	 * FreeBSD's implemention of zfs_zget requires a vnode to back it.
	 * This means that we could end up calling into getnewvnode while
	 * calling zfs_rmnode as a result of a prior call to getnewvnode
	 * trying to clear vnodes out of the cache. If this repeats we can
	 * recurse enough that we overflow our stack. To avoid this, we
	 * avoid calling zfs_zget on the xattr znode and instead simply add
	 * it to the unlinked set and schedule a call to zfs_unlinked_drain.
	 */
	if (xattr_obj) {
		/* Add extended attribute directory to the unlinked set. */
		VERIFY3U(0, ==,
		    zap_add_int(os, zfsvfs->z_unlinkedobj, xattr_obj, tx));
	}
#else
	if (xzp) {
		ASSERT(error == 0);
		xzp->z_unlinked = B_TRUE;	/* mark xzp for deletion */
		xzp->z_links = 0;	/* no more links to it */
		VERIFY(0 == sa_update(xzp->z_sa_hdl, SA_ZPL_LINKS(zfsvfs),
		    &xzp->z_links, sizeof (xzp->z_links), tx));
		zfs_unlinked_add(xzp, tx);
	}
#endif

	/* Remove this znode from the unlinked set */
	VERIFY3U(0, ==,
	    zap_remove_int(zfsvfs->z_os, zfsvfs->z_unlinkedobj, zp->z_id, tx));

	zfs_znode_delete(zp, tx);

	dmu_tx_commit(tx);

#if defined(__FreeBSD__)
	if (xattr_obj) {
		/*
		 * We're using the FreeBSD taskqueue API here instead of
		 * the Solaris taskq API since the FreeBSD API allows for a
		 * task to be enqueued multiple times but executed once.
		 */
		taskqueue_enqueue(zfsvfs_taskq->tq_queue,
		    &zfsvfs->z_unlinked_drain_task);
	}
#endif
}

static uint64_t
zfs_dirent(znode_t *zp, uint64_t mode)
{
	uint64_t de = zp->z_id;

	if (zp->z_zfsvfs->z_version >= ZPL_VERSION_DIRENT_TYPE)
		de |= IFTODT(mode) << 60;
	return (de);
}

/*
 * Link zp into dzp.  Can only fail if zp has been unlinked.
 */
int
zfs_link_create(znode_t *dzp, const char *name, znode_t *zp, dmu_tx_t *tx,
    int flag)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	vnode_t *vp = ZTOV(zp);
	uint64_t value;
	int zp_is_dir = (vp->v_type == VDIR);
	sa_bulk_attr_t bulk[5];
	uint64_t mtime[2], ctime[2];
	int count = 0;
	int error;

	ASSERT_VOP_ELOCKED(ZTOV(dzp), __func__);
	ASSERT_VOP_ELOCKED(ZTOV(zp), __func__);
#ifdef __FreeBSD__
	if (zp_is_dir) {
		if (dzp->z_links >= ZFS_LINK_MAX)
			return (SET_ERROR(EMLINK));
	}
#endif
	if (!(flag & ZRENAMING)) {
		if (zp->z_unlinked) {	/* no new links to unlinked zp */
			ASSERT(!(flag & (ZNEW | ZEXISTS)));
			return (SET_ERROR(ENOENT));
		}
#ifdef __FreeBSD__
		if (zp->z_links >= ZFS_LINK_MAX - zp_is_dir) {
			return (SET_ERROR(EMLINK));
		}
#endif
		zp->z_links++;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
		    &zp->z_links, sizeof (zp->z_links));

	} else {
		ASSERT(zp->z_unlinked == 0);
	}
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL,
	    &dzp->z_id, sizeof (dzp->z_id));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (!(flag & ZNEW)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime,
		    ctime, B_TRUE);
	}
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	dzp->z_size++;
	dzp->z_links += zp_is_dir;
	count = 0;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &dzp->z_size, sizeof (dzp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &dzp->z_links, sizeof (dzp->z_links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
	    mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    ctime, sizeof (ctime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &dzp->z_pflags, sizeof (dzp->z_pflags));
	zfs_tstamp_update_setup(dzp, CONTENT_MODIFIED, mtime, ctime, B_TRUE);
	error = sa_bulk_update(dzp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	value = zfs_dirent(zp, zp->z_mode);
	error = zap_add(zp->z_zfsvfs->z_os, dzp->z_id, name,
	    8, 1, &value, tx);
	VERIFY0(error);

	return (0);
}

/*
 * The match type in the code for this function should conform to:
 *
 * ------------------------------------------------------------------------
 * fs type  | z_norm      | lookup type | match type
 * ---------|-------------|-------------|----------------------------------
 * CS !norm | 0           |           0 | 0 (exact)
 * CS  norm | formX       |           0 | MT_NORMALIZE
 * CI !norm | upper       |   !ZCIEXACT | MT_NORMALIZE
 * CI !norm | upper       |    ZCIEXACT | MT_NORMALIZE | MT_MATCH_CASE
 * CI  norm | upper|formX |   !ZCIEXACT | MT_NORMALIZE
 * CI  norm | upper|formX |    ZCIEXACT | MT_NORMALIZE | MT_MATCH_CASE
 * CM !norm | upper       |    !ZCILOOK | MT_NORMALIZE | MT_MATCH_CASE
 * CM !norm | upper       |     ZCILOOK | MT_NORMALIZE
 * CM  norm | upper|formX |    !ZCILOOK | MT_NORMALIZE | MT_MATCH_CASE
 * CM  norm | upper|formX |     ZCILOOK | MT_NORMALIZE
 *
 * Abbreviations:
 *    CS = Case Sensitive, CI = Case Insensitive, CM = Case Mixed
 *    upper = case folding set by fs type on creation (U8_TEXTPREP_TOUPPER)
 *    formX = unicode normalization form set on fs creation
 */
static int
zfs_dropname(znode_t *dzp, const char *name, znode_t *zp, dmu_tx_t *tx,
    int flag)
{
	int error;

	if (zp->z_zfsvfs->z_norm) {
		matchtype_t mt = MT_NORMALIZE;

		if (zp->z_zfsvfs->z_case == ZFS_CASE_MIXED) {
			mt |= MT_MATCH_CASE;
		}

		error = zap_remove_norm(zp->z_zfsvfs->z_os, dzp->z_id,
		    name, mt, tx);
	} else {
		error = zap_remove(zp->z_zfsvfs->z_os, dzp->z_id, name, tx);
	}

	return (error);
}

/*
 * Unlink zp from dzp, and mark zp for deletion if this was the last link.
 * Can fail if zp is a mount point (EBUSY) or a non-empty directory (EEXIST).
 * If 'unlinkedp' is NULL, we put unlinked znodes on the unlinked list.
 * If it's non-NULL, we use it to indicate whether the znode needs deletion,
 * and it's the caller's job to do it.
 */
int
zfs_link_destroy(znode_t *dzp, const char *name, znode_t *zp, dmu_tx_t *tx,
    int flag, boolean_t *unlinkedp)
{
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	vnode_t *vp = ZTOV(zp);
	int zp_is_dir = (vp->v_type == VDIR);
	boolean_t unlinked = B_FALSE;
	sa_bulk_attr_t bulk[5];
	uint64_t mtime[2], ctime[2];
	int count = 0;
	int error;

	ASSERT_VOP_ELOCKED(ZTOV(dzp), __func__);
	ASSERT_VOP_ELOCKED(ZTOV(zp), __func__);

	if (!(flag & ZRENAMING)) {

		if (zp_is_dir && !zfs_dirempty(zp)) {
#ifdef illumos
			return (SET_ERROR(EEXIST));
#else
			return (SET_ERROR(ENOTEMPTY));
#endif
		}

		/*
		 * If we get here, we are going to try to remove the object.
		 * First try removing the name from the directory; if that
		 * fails, return the error.
		 */
		error = zfs_dropname(dzp, name, zp, tx, flag);
		if (error != 0) {
			return (error);
		}

		if (zp->z_links <= zp_is_dir) {
			zfs_panic_recover("zfs: link count on vnode %p is %u, "
			    "should be at least %u", zp->z_vnode,
			    (int)zp->z_links,
			    zp_is_dir + 1);
			zp->z_links = zp_is_dir + 1;
		}
		if (--zp->z_links == zp_is_dir) {
			zp->z_unlinked = B_TRUE;
			zp->z_links = 0;
			unlinked = B_TRUE;
		} else {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
			    NULL, &ctime, sizeof (ctime));
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
			    NULL, &zp->z_pflags, sizeof (zp->z_pflags));
			zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime, ctime,
			    B_TRUE);
		}
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
		    NULL, &zp->z_links, sizeof (zp->z_links));
		error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		count = 0;
		ASSERT0(error);
	} else {
		ASSERT(zp->z_unlinked == 0);
		error = zfs_dropname(dzp, name, zp, tx, flag);
		if (error != 0)
			return (error);
	}

	dzp->z_size--;		/* one dirent removed */
	dzp->z_links -= zp_is_dir;	/* ".." link from zp */
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
	    NULL, &dzp->z_links, sizeof (dzp->z_links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &dzp->z_size, sizeof (dzp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
	    NULL, ctime, sizeof (ctime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
	    NULL, mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &dzp->z_pflags, sizeof (dzp->z_pflags));
	zfs_tstamp_update_setup(dzp, CONTENT_MODIFIED, mtime, ctime, B_TRUE);
	error = sa_bulk_update(dzp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	if (unlinkedp != NULL)
		*unlinkedp = unlinked;
	else if (unlinked)
		zfs_unlinked_add(zp, tx);

	return (0);
}

/*
 * Indicate whether the directory is empty.
 */
boolean_t
zfs_dirempty(znode_t *dzp)
{
	return (dzp->z_size == 2);
}

int
zfs_make_xattrdir(znode_t *zp, vattr_t *vap, vnode_t **xvpp, cred_t *cr)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	znode_t *xzp;
	dmu_tx_t *tx;
	int error;
	zfs_acl_ids_t acl_ids;
	boolean_t fuid_dirtied;
	uint64_t parent;

	*xvpp = NULL;

	/*
	 * In FreeBSD, access checking for creating an EA is being done
	 * in zfs_setextattr(),
	 */
#ifndef __FreeBSD_kernel__
	if (error = zfs_zaccess(zp, ACE_WRITE_NAMED_ATTRS, 0, B_FALSE, cr))
		return (error);
#endif

	if ((error = zfs_acl_ids_create(zp, IS_XATTR, vap, cr, NULL,
	    &acl_ids)) != 0)
		return (error);
	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		return (SET_ERROR(EDQUOT));
	}

	getnewvnode_reserve(1);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		return (error);
	}
	zfs_mknode(zp, vap, tx, cr, IS_XATTR, &xzp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

#ifdef DEBUG
	error = sa_lookup(xzp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent));
	ASSERT(error == 0 && parent == zp->z_id);
#endif

	VERIFY(0 == sa_update(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs), &xzp->z_id,
	    sizeof (xzp->z_id), tx));

	(void) zfs_log_create(zfsvfs->z_log, tx, TX_MKXATTR, zp,
	    xzp, "", NULL, acl_ids.z_fuidp, vap);

	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	getnewvnode_drop_reserve();

	*xvpp = ZTOV(xzp);

	return (0);
}

/*
 * Return a znode for the extended attribute directory for zp.
 * ** If the directory does not already exist, it is created **
 *
 *	IN:	zp	- znode to obtain attribute directory from
 *		cr	- credentials of caller
 *		flags	- flags from the VOP_LOOKUP call
 *
 *	OUT:	xzpp	- pointer to extended attribute znode
 *
 *	RETURN:	0 on success
 *		error number on failure
 */
int
zfs_get_xattrdir(znode_t *zp, vnode_t **xvpp, cred_t *cr, int flags)
{
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	znode_t		*xzp;
	vattr_t		va;
	int		error;
top:
	error = zfs_dirent_lookup(zp, "", &xzp, ZXATTR);
	if (error)
		return (error);

	if (xzp != NULL) {
		*xvpp = ZTOV(xzp);
		return (0);
	}


	if (!(flags & CREATE_XATTR_DIR)) {
#ifdef illumos
		return (SET_ERROR(ENOENT));
#else
		return (SET_ERROR(ENOATTR));
#endif
	}

	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		return (SET_ERROR(EROFS));
	}

	/*
	 * The ability to 'create' files in an attribute
	 * directory comes from the write_xattr permission on the base file.
	 *
	 * The ability to 'search' an attribute directory requires
	 * read_xattr permission on the base file.
	 *
	 * Once in a directory the ability to read/write attributes
	 * is controlled by the permissions on the attribute file.
	 */
	va.va_mask = AT_TYPE | AT_MODE | AT_UID | AT_GID;
	va.va_type = VDIR;
	va.va_mode = S_IFDIR | S_ISVTX | 0777;
	zfs_fuid_map_ids(zp, cr, &va.va_uid, &va.va_gid);

	error = zfs_make_xattrdir(zp, &va, xvpp, cr);

	if (error == ERESTART) {
		/* NB: we already did dmu_tx_wait() if necessary */
		goto top;
	}
	if (error == 0)
		VOP_UNLOCK(*xvpp, 0);

	return (error);
}

/*
 * Decide whether it is okay to remove within a sticky directory.
 *
 * In sticky directories, write access is not sufficient;
 * you can remove entries from a directory only if:
 *
 *	you own the directory,
 *	you own the entry,
 *	the entry is a plain file and you have write access,
 *	or you are privileged (checked in secpolicy...).
 *
 * The function returns 0 if remove access is granted.
 */
int
zfs_sticky_remove_access(znode_t *zdp, znode_t *zp, cred_t *cr)
{
	uid_t  		uid;
	uid_t		downer;
	uid_t		fowner;
	zfsvfs_t	*zfsvfs = zdp->z_zfsvfs;

	if (zdp->z_zfsvfs->z_replay)
		return (0);

	if ((zdp->z_mode & S_ISVTX) == 0)
		return (0);

	downer = zfs_fuid_map_id(zfsvfs, zdp->z_uid, cr, ZFS_OWNER);
	fowner = zfs_fuid_map_id(zfsvfs, zp->z_uid, cr, ZFS_OWNER);

	if ((uid = crgetuid(cr)) == downer || uid == fowner ||
	    (ZTOV(zp)->v_type == VREG &&
	    zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr) == 0))
		return (0);
	else
		return (secpolicy_vnode_remove(ZTOV(zp), cr));
}
