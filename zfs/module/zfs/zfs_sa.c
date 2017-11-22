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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/vnode.h>
#include <sys/sa.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_sa.h>

/*
 * ZPL attribute registration table.
 * Order of attributes doesn't matter
 * a unique value will be assigned for each
 * attribute that is file system specific
 *
 * This is just the set of ZPL attributes that this
 * version of ZFS deals with natively.  The file system
 * could have other attributes stored in files, but they will be
 * ignored.  The SA framework will preserve them, just that
 * this version of ZFS won't change or delete them.
 */

sa_attr_reg_t zfs_attr_table[ZPL_END+1] = {
	{"ZPL_ATIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 0},
	{"ZPL_MTIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 1},
	{"ZPL_CTIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 2},
	{"ZPL_CRTIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 3},
	{"ZPL_GEN", sizeof (uint64_t), SA_UINT64_ARRAY, 4},
	{"ZPL_MODE", sizeof (uint64_t), SA_UINT64_ARRAY, 5},
	{"ZPL_SIZE", sizeof (uint64_t), SA_UINT64_ARRAY, 6},
	{"ZPL_PARENT", sizeof (uint64_t), SA_UINT64_ARRAY, 7},
	{"ZPL_LINKS", sizeof (uint64_t), SA_UINT64_ARRAY, 8},
	{"ZPL_XATTR", sizeof (uint64_t), SA_UINT64_ARRAY, 9},
	{"ZPL_RDEV", sizeof (uint64_t), SA_UINT64_ARRAY, 10},
	{"ZPL_FLAGS", sizeof (uint64_t), SA_UINT64_ARRAY, 11},
	{"ZPL_UID", sizeof (uint64_t), SA_UINT64_ARRAY, 12},
	{"ZPL_GID", sizeof (uint64_t), SA_UINT64_ARRAY, 13},
	{"ZPL_PAD", sizeof (uint64_t) * 4, SA_UINT64_ARRAY, 14},
	{"ZPL_ZNODE_ACL", 88, SA_UINT8_ARRAY, 15},
	{"ZPL_DACL_COUNT", sizeof (uint64_t), SA_UINT64_ARRAY, 0},
	{"ZPL_SYMLINK", 0, SA_UINT8_ARRAY, 0},
	{"ZPL_SCANSTAMP", 32, SA_UINT8_ARRAY, 0},
	{"ZPL_DACL_ACES", 0, SA_ACL, 0},
	{"ZPL_DXATTR", 0, SA_UINT8_ARRAY, 0},
	{NULL, 0, 0, 0}
};

#ifdef _KERNEL
int
zfs_sa_readlink(znode_t *zp, uio_t *uio)
{
	dmu_buf_t *db = sa_get_db(zp->z_sa_hdl);
	size_t bufsz;
	int error;

	bufsz = zp->z_size;
	if (bufsz + ZFS_OLD_ZNODE_PHYS_SIZE <= db->db_size) {
		error = uiomove((caddr_t)db->db_data +
		    ZFS_OLD_ZNODE_PHYS_SIZE,
		    MIN((size_t)bufsz, uio->uio_resid), UIO_READ, uio);
	} else {
		dmu_buf_t *dbp;
		if ((error = dmu_buf_hold(ZTOZSB(zp)->z_os, zp->z_id,
		    0, FTAG, &dbp, DMU_READ_NO_PREFETCH)) == 0) {
			error = uiomove(dbp->db_data,
			    MIN((size_t)bufsz, uio->uio_resid), UIO_READ, uio);
			dmu_buf_rele(dbp, FTAG);
		}
	}
	return (error);
}

void
zfs_sa_symlink(znode_t *zp, char *link, int len, dmu_tx_t *tx)
{
	dmu_buf_t *db = sa_get_db(zp->z_sa_hdl);

	if (ZFS_OLD_ZNODE_PHYS_SIZE + len <= dmu_bonus_max()) {
		VERIFY0(dmu_set_bonus(db, len + ZFS_OLD_ZNODE_PHYS_SIZE, tx));
		if (len) {
			bcopy(link, (caddr_t)db->db_data +
			    ZFS_OLD_ZNODE_PHYS_SIZE, len);
		}
	} else {
		dmu_buf_t *dbp;

		zfs_grow_blocksize(zp, len, tx);
		VERIFY0(dmu_buf_hold(ZTOZSB(zp)->z_os, zp->z_id, 0, FTAG, &dbp,
		    DMU_READ_NO_PREFETCH));

		dmu_buf_will_dirty(dbp, tx);

		ASSERT3U(len, <=, dbp->db_size);
		bcopy(link, dbp->db_data, len);
		dmu_buf_rele(dbp, FTAG);
	}
}

void
zfs_sa_get_scanstamp(znode_t *zp, xvattr_t *xvap)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	xoptattr_t *xoap;

	ASSERT(MUTEX_HELD(&zp->z_lock));
	VERIFY((xoap = xva_getxoptattr(xvap)) != NULL);
	if (zp->z_is_sa) {
		if (sa_lookup(zp->z_sa_hdl, SA_ZPL_SCANSTAMP(zfsvfs),
		    &xoap->xoa_av_scanstamp,
		    sizeof (xoap->xoa_av_scanstamp)) != 0)
			return;
	} else {
		dmu_object_info_t doi;
		dmu_buf_t *db = sa_get_db(zp->z_sa_hdl);
		int len;

		if (!(zp->z_pflags & ZFS_BONUS_SCANSTAMP))
			return;

		sa_object_info(zp->z_sa_hdl, &doi);
		len = sizeof (xoap->xoa_av_scanstamp) +
		    ZFS_OLD_ZNODE_PHYS_SIZE;

		if (len <= doi.doi_bonus_size) {
			(void) memcpy(xoap->xoa_av_scanstamp,
			    (caddr_t)db->db_data + ZFS_OLD_ZNODE_PHYS_SIZE,
			    sizeof (xoap->xoa_av_scanstamp));
		}
	}
	XVA_SET_RTN(xvap, XAT_AV_SCANSTAMP);
}

void
zfs_sa_set_scanstamp(znode_t *zp, xvattr_t *xvap, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	xoptattr_t *xoap;

	ASSERT(MUTEX_HELD(&zp->z_lock));
	VERIFY((xoap = xva_getxoptattr(xvap)) != NULL);
	if (zp->z_is_sa)
		VERIFY(0 == sa_update(zp->z_sa_hdl, SA_ZPL_SCANSTAMP(zfsvfs),
		    &xoap->xoa_av_scanstamp,
		    sizeof (xoap->xoa_av_scanstamp), tx));
	else {
		dmu_object_info_t doi;
		dmu_buf_t *db = sa_get_db(zp->z_sa_hdl);
		int len;

		sa_object_info(zp->z_sa_hdl, &doi);
		len = sizeof (xoap->xoa_av_scanstamp) +
		    ZFS_OLD_ZNODE_PHYS_SIZE;
		if (len > doi.doi_bonus_size)
			VERIFY(dmu_set_bonus(db, len, tx) == 0);
		(void) memcpy((caddr_t)db->db_data + ZFS_OLD_ZNODE_PHYS_SIZE,
		    xoap->xoa_av_scanstamp, sizeof (xoap->xoa_av_scanstamp));

		zp->z_pflags |= ZFS_BONUS_SCANSTAMP;
		VERIFY(0 == sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
		    &zp->z_pflags, sizeof (uint64_t), tx));
	}
}

int
zfs_sa_get_xattr(znode_t *zp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	char *obj;
	int size;
	int error;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));
	ASSERT(!zp->z_xattr_cached);
	ASSERT(zp->z_is_sa);

	error = sa_size(zp->z_sa_hdl, SA_ZPL_DXATTR(zfsvfs), &size);
	if (error) {
		if (error == ENOENT)
			return nvlist_alloc(&zp->z_xattr_cached,
			    NV_UNIQUE_NAME, KM_SLEEP);
		else
			return (error);
	}

	obj = vmem_alloc(size, KM_SLEEP);

	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_DXATTR(zfsvfs), obj, size);
	if (error == 0)
		error = nvlist_unpack(obj, size, &zp->z_xattr_cached, KM_SLEEP);

	vmem_free(obj, size);

	return (error);
}

int
zfs_sa_set_xattr(znode_t *zp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_tx_t *tx;
	char *obj;
	size_t size;
	int error;

	ASSERT(RW_WRITE_HELD(&zp->z_xattr_lock));
	ASSERT(zp->z_xattr_cached);
	ASSERT(zp->z_is_sa);

	error = nvlist_size(zp->z_xattr_cached, &size, NV_ENCODE_XDR);
	if ((error == 0) && (size > SA_ATTR_MAX_LEN))
		error = EFBIG;
	if (error)
		goto out;

	obj = vmem_alloc(size, KM_SLEEP);

	error = nvlist_pack(zp->z_xattr_cached, &obj, &size,
	    NV_ENCODE_XDR, KM_SLEEP);
	if (error)
		goto out_free;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa_create(tx, size);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);

	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		int count = 0;
		sa_bulk_attr_t bulk[2];
		uint64_t ctime[2];

		zfs_tstamp_update_setup(zp, STATE_CHANGED, NULL, ctime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_DXATTR(zfsvfs),
		    NULL, obj, size);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		VERIFY0(sa_bulk_update(zp->z_sa_hdl, bulk, count, tx));

		dmu_tx_commit(tx);
	}
out_free:
	vmem_free(obj, size);
out:
	return (error);
}

/*
 * I'm not convinced we should do any of this upgrade.
 * since the SA code can read both old/new znode formats
 * with probably little to no performance difference.
 *
 * All new files will be created with the new format.
 */

void
zfs_sa_upgrade(sa_handle_t *hdl, dmu_tx_t *tx)
{
	dmu_buf_t *db = sa_get_db(hdl);
	znode_t *zp = sa_get_userdata(hdl);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int count = 0;
	sa_bulk_attr_t *bulk, *sa_attrs;
	zfs_acl_locator_cb_t locate = { 0 };
	uint64_t uid, gid, mode, rdev, xattr, parent, tmp_gen;
	uint64_t crtime[2], mtime[2], ctime[2], atime[2];
	uint64_t links;
	zfs_acl_phys_t znode_acl;
	char scanstamp[AV_SCANSTAMP_SZ];
	boolean_t drop_lock = B_FALSE;

	/*
	 * No upgrade if ACL isn't cached
	 * since we won't know which locks are held
	 * and ready the ACL would require special "locked"
	 * interfaces that would be messy
	 */
	if (zp->z_acl_cached == NULL || S_ISLNK(ZTOI(zp)->i_mode))
		return;

	/*
	 * If the z_lock is held and we aren't the owner
	 * the just return since we don't want to deadlock
	 * trying to update the status of z_is_sa.  This
	 * file can then be upgraded at a later time.
	 *
	 * Otherwise, we know we are doing the
	 * sa_update() that caused us to enter this function.
	 */
	if (mutex_owner(&zp->z_lock) != curthread) {
		if (mutex_tryenter(&zp->z_lock) == 0)
			return;
		else
			drop_lock = B_TRUE;
	}

	/* First do a bulk query of the attributes that aren't cached */
	bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * 20, KM_SLEEP);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL, &atime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL, &parent, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_XATTR(zfsvfs), NULL, &xattr, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_RDEV(zfsvfs), NULL, &rdev, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL, &uid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL, &gid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GEN(zfsvfs), NULL, &tmp_gen, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ZNODE_ACL(zfsvfs), NULL,
	    &znode_acl, 88);

	if (sa_bulk_lookup_locked(hdl, bulk, count) != 0) {
		kmem_free(bulk, sizeof (sa_bulk_attr_t) * 20);
		goto done;
	}

	/*
	 * While the order here doesn't matter its best to try and organize
	 * it is such a way to pick up an already existing layout number
	 */
	count = 0;
	sa_attrs = kmem_zalloc(sizeof (sa_bulk_attr_t) * 20, KM_SLEEP);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_MODE(zfsvfs), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_GEN(zfsvfs),
	    NULL, &tmp_gen, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_UID(zfsvfs), NULL, &uid, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_GID(zfsvfs), NULL, &gid, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_PARENT(zfsvfs),
	    NULL, &parent, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_ATIME(zfsvfs), NULL,
	    &atime, 16);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_MTIME(zfsvfs), NULL,
	    &mtime, 16);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    &ctime, 16);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_CRTIME(zfsvfs), NULL,
	    &crtime, 16);
	links = ZTOI(zp)->i_nlink;
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &links, 8);
	if (S_ISBLK(ZTOI(zp)->i_mode) || S_ISCHR(ZTOI(zp)->i_mode))
		SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_RDEV(zfsvfs), NULL,
		    &rdev, 8);
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_DACL_COUNT(zfsvfs), NULL,
	    &zp->z_acl_cached->z_acl_count, 8);

	if (zp->z_acl_cached->z_version < ZFS_ACL_VERSION_FUID)
		zfs_acl_xform(zp, zp->z_acl_cached, CRED());

	locate.cb_aclp = zp->z_acl_cached;
	SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_DACL_ACES(zfsvfs),
	    zfs_acl_data_locator, &locate, zp->z_acl_cached->z_acl_bytes);

	if (xattr)
		SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_XATTR(zfsvfs),
		    NULL, &xattr, 8);

	/* if scanstamp then add scanstamp */

	if (zp->z_pflags & ZFS_BONUS_SCANSTAMP) {
		bcopy((caddr_t)db->db_data + ZFS_OLD_ZNODE_PHYS_SIZE,
		    scanstamp, AV_SCANSTAMP_SZ);
		SA_ADD_BULK_ATTR(sa_attrs, count, SA_ZPL_SCANSTAMP(zfsvfs),
		    NULL, scanstamp, AV_SCANSTAMP_SZ);
		zp->z_pflags &= ~ZFS_BONUS_SCANSTAMP;
	}

	VERIFY(dmu_set_bonustype(db, DMU_OT_SA, tx) == 0);
	VERIFY(sa_replace_all_by_template_locked(hdl, sa_attrs,
	    count, tx) == 0);
	if (znode_acl.z_acl_extern_obj)
		VERIFY(0 == dmu_object_free(zfsvfs->z_os,
		    znode_acl.z_acl_extern_obj, tx));

	zp->z_is_sa = B_TRUE;
	kmem_free(sa_attrs, sizeof (sa_bulk_attr_t) * 20);
	kmem_free(bulk, sizeof (sa_bulk_attr_t) * 20);
done:
	if (drop_lock)
		mutex_exit(&zp->z_lock);
}

void
zfs_sa_upgrade_txholds(dmu_tx_t *tx, znode_t *zp)
{
	if (!ZTOZSB(zp)->z_use_sa || zp->z_is_sa)
		return;


	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);

	if (zfs_external_acl(zp)) {
		dmu_tx_hold_free(tx, zfs_external_acl(zp), 0,
		    DMU_OBJECT_END);
	}
}

EXPORT_SYMBOL(zfs_attr_table);
EXPORT_SYMBOL(zfs_sa_readlink);
EXPORT_SYMBOL(zfs_sa_symlink);
EXPORT_SYMBOL(zfs_sa_get_scanstamp);
EXPORT_SYMBOL(zfs_sa_set_scanstamp);
EXPORT_SYMBOL(zfs_sa_get_xattr);
EXPORT_SYMBOL(zfs_sa_set_xattr);
EXPORT_SYMBOL(zfs_sa_upgrade);
EXPORT_SYMBOL(zfs_sa_upgrade_txholds);

#endif
