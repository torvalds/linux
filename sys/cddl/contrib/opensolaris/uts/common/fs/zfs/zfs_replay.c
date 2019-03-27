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
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/vfs.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_fuid.h>
#include <sys/spa.h>
#include <sys/zil.h>
#include <sys/byteorder.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <sys/atomic.h>
#include <sys/cred.h>
#include <sys/namei.h>

/*
 * Functions to replay ZFS intent log (ZIL) records
 * The functions are called through a function vector (zfs_replay_vector)
 * which is indexed by the transaction type.
 */

static void
zfs_init_vattr(vattr_t *vap, uint64_t mask, uint64_t mode,
    uint64_t uid, uint64_t gid, uint64_t rdev, uint64_t nodeid)
{
	VATTR_NULL(vap);
	vap->va_mask = (uint_t)mask;
	if (mask & AT_TYPE)
		vap->va_type = IFTOVT(mode);
	if (mask & AT_MODE)
		vap->va_mode = mode & MODEMASK;
	if (mask & AT_UID)
		vap->va_uid = (uid_t)(IS_EPHEMERAL(uid)) ? -1 : uid;
	if (mask & AT_GID)
		vap->va_gid = (gid_t)(IS_EPHEMERAL(gid)) ? -1 : gid;
	vap->va_rdev = zfs_cmpldev(rdev);
	vap->va_nodeid = nodeid;
}

/* ARGSUSED */
static int
zfs_replay_error(void *arg1, void *arg2, boolean_t byteswap)
{
	return (SET_ERROR(ENOTSUP));
}

static void
zfs_replay_xvattr(lr_attr_t *lrattr, xvattr_t *xvap)
{
	xoptattr_t *xoap = NULL;
	uint64_t *attrs;
	uint64_t *crtime;
	uint32_t *bitmap;
	void *scanstamp;
	int i;

	xvap->xva_vattr.va_mask |= AT_XVATTR;
	if ((xoap = xva_getxoptattr(xvap)) == NULL) {
		xvap->xva_vattr.va_mask &= ~AT_XVATTR; /* shouldn't happen */
		return;
	}

	ASSERT(lrattr->lr_attr_masksize == xvap->xva_mapsize);

	bitmap = &lrattr->lr_attr_bitmap;
	for (i = 0; i != lrattr->lr_attr_masksize; i++, bitmap++)
		xvap->xva_reqattrmap[i] = *bitmap;

	attrs = (uint64_t *)(lrattr + lrattr->lr_attr_masksize - 1);
	crtime = attrs + 1;
	scanstamp = (caddr_t)(crtime + 2);

	if (XVA_ISSET_REQ(xvap, XAT_HIDDEN))
		xoap->xoa_hidden = ((*attrs & XAT0_HIDDEN) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_SYSTEM))
		xoap->xoa_system = ((*attrs & XAT0_SYSTEM) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE))
		xoap->xoa_archive = ((*attrs & XAT0_ARCHIVE) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_READONLY))
		xoap->xoa_readonly = ((*attrs & XAT0_READONLY) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE))
		xoap->xoa_immutable = ((*attrs & XAT0_IMMUTABLE) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK))
		xoap->xoa_nounlink = ((*attrs & XAT0_NOUNLINK) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY))
		xoap->xoa_appendonly = ((*attrs & XAT0_APPENDONLY) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_NODUMP))
		xoap->xoa_nodump = ((*attrs & XAT0_NODUMP) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_OPAQUE))
		xoap->xoa_opaque = ((*attrs & XAT0_OPAQUE) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED))
		xoap->xoa_av_modified = ((*attrs & XAT0_AV_MODIFIED) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED))
		xoap->xoa_av_quarantined =
		    ((*attrs & XAT0_AV_QUARANTINED) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_CREATETIME))
		ZFS_TIME_DECODE(&xoap->xoa_createtime, crtime);
	if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
		bcopy(scanstamp, xoap->xoa_av_scanstamp, AV_SCANSTAMP_SZ);
	if (XVA_ISSET_REQ(xvap, XAT_REPARSE))
		xoap->xoa_reparse = ((*attrs & XAT0_REPARSE) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_OFFLINE))
		xoap->xoa_offline = ((*attrs & XAT0_OFFLINE) != 0);
	if (XVA_ISSET_REQ(xvap, XAT_SPARSE))
		xoap->xoa_sparse = ((*attrs & XAT0_SPARSE) != 0);
}

static int
zfs_replay_domain_cnt(uint64_t uid, uint64_t gid)
{
	uint64_t uid_idx;
	uint64_t gid_idx;
	int domcnt = 0;

	uid_idx = FUID_INDEX(uid);
	gid_idx = FUID_INDEX(gid);
	if (uid_idx)
		domcnt++;
	if (gid_idx > 0 && gid_idx != uid_idx)
		domcnt++;

	return (domcnt);
}

static void *
zfs_replay_fuid_domain_common(zfs_fuid_info_t *fuid_infop, void *start,
    int domcnt)
{
	int i;

	for (i = 0; i != domcnt; i++) {
		fuid_infop->z_domain_table[i] = start;
		start = (caddr_t)start + strlen(start) + 1;
	}

	return (start);
}

/*
 * Set the uid/gid in the fuid_info structure.
 */
static void
zfs_replay_fuid_ugid(zfs_fuid_info_t *fuid_infop, uint64_t uid, uint64_t gid)
{
	/*
	 * If owner or group are log specific FUIDs then slurp up
	 * domain information and build zfs_fuid_info_t
	 */
	if (IS_EPHEMERAL(uid))
		fuid_infop->z_fuid_owner = uid;

	if (IS_EPHEMERAL(gid))
		fuid_infop->z_fuid_group = gid;
}

/*
 * Load fuid domains into fuid_info_t
 */
static zfs_fuid_info_t *
zfs_replay_fuid_domain(void *buf, void **end, uint64_t uid, uint64_t gid)
{
	int domcnt;

	zfs_fuid_info_t *fuid_infop;

	fuid_infop = zfs_fuid_info_alloc();

	domcnt = zfs_replay_domain_cnt(uid, gid);

	if (domcnt == 0)
		return (fuid_infop);

	fuid_infop->z_domain_table =
	    kmem_zalloc(domcnt * sizeof (char **), KM_SLEEP);

	zfs_replay_fuid_ugid(fuid_infop, uid, gid);

	fuid_infop->z_domain_cnt = domcnt;
	*end = zfs_replay_fuid_domain_common(fuid_infop, buf, domcnt);
	return (fuid_infop);
}

/*
 * load zfs_fuid_t's and fuid_domains into fuid_info_t
 */
static zfs_fuid_info_t *
zfs_replay_fuids(void *start, void **end, int idcnt, int domcnt, uint64_t uid,
    uint64_t gid)
{
	uint64_t *log_fuid = (uint64_t *)start;
	zfs_fuid_info_t *fuid_infop;
	int i;

	fuid_infop = zfs_fuid_info_alloc();
	fuid_infop->z_domain_cnt = domcnt;

	fuid_infop->z_domain_table =
	    kmem_zalloc(domcnt * sizeof (char **), KM_SLEEP);

	for (i = 0; i != idcnt; i++) {
		zfs_fuid_t *zfuid;

		zfuid = kmem_alloc(sizeof (zfs_fuid_t), KM_SLEEP);
		zfuid->z_logfuid = *log_fuid;
		zfuid->z_id = -1;
		zfuid->z_domidx = 0;
		list_insert_tail(&fuid_infop->z_fuids, zfuid);
		log_fuid++;
	}

	zfs_replay_fuid_ugid(fuid_infop, uid, gid);

	*end = zfs_replay_fuid_domain_common(fuid_infop, log_fuid, domcnt);
	return (fuid_infop);
}

static void
zfs_replay_swap_attrs(lr_attr_t *lrattr)
{
	/* swap the lr_attr structure */
	byteswap_uint32_array(lrattr, sizeof (*lrattr));
	/* swap the bitmap */
	byteswap_uint32_array(lrattr + 1, (lrattr->lr_attr_masksize - 1) *
	    sizeof (uint32_t));
	/* swap the attributes, create time + 64 bit word for attributes */
	byteswap_uint64_array((caddr_t)(lrattr + 1) + (sizeof (uint32_t) *
	    (lrattr->lr_attr_masksize - 1)), 3 * sizeof (uint64_t));
}

/*
 * Replay file create with optional ACL, xvattr information as well
 * as option FUID information.
 */
static int
zfs_replay_create_acl(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_acl_create_t *lracl = arg2;
	char *name = NULL;		/* location determined later */
	lr_create_t *lr = (lr_create_t *)lracl;
	znode_t *dzp;
	vnode_t *vp = NULL;
	xvattr_t xva;
	int vflg = 0;
	vsecattr_t vsec = { 0 };
	lr_attr_t *lrattr;
	void *aclstart;
	void *fuidstart;
	size_t xvatlen = 0;
	uint64_t txtype;
	uint64_t objid;
	uint64_t dnodesize;
	int error;

	txtype = (lr->lr_common.lrc_txtype & ~TX_CI);
	if (byteswap) {
		byteswap_uint64_array(lracl, sizeof (*lracl));
		if (txtype == TX_CREATE_ACL_ATTR ||
		    txtype == TX_MKDIR_ACL_ATTR) {
			lrattr = (lr_attr_t *)(caddr_t)(lracl + 1);
			zfs_replay_swap_attrs(lrattr);
			xvatlen = ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
		}

		aclstart = (caddr_t)(lracl + 1) + xvatlen;
		zfs_ace_byteswap(aclstart, lracl->lr_acl_bytes, B_FALSE);
		/* swap fuids */
		if (lracl->lr_fuidcnt) {
			byteswap_uint64_array((caddr_t)aclstart +
			    ZIL_ACE_LENGTH(lracl->lr_acl_bytes),
			    lracl->lr_fuidcnt * sizeof (uint64_t));
		}
	}

	if ((error = zfs_zget(zfsvfs, lr->lr_doid, &dzp)) != 0)
		return (error);

	objid = LR_FOID_GET_OBJ(lr->lr_foid);
	dnodesize = LR_FOID_GET_SLOTS(lr->lr_foid) << DNODE_SHIFT;
	
	xva_init(&xva);
	zfs_init_vattr(&xva.xva_vattr, AT_TYPE | AT_MODE | AT_UID | AT_GID,
	    lr->lr_mode, lr->lr_uid, lr->lr_gid, lr->lr_rdev, objid);

	/*
	 * All forms of zfs create (create, mkdir, mkxattrdir, symlink)
	 * eventually end up in zfs_mknode(), which assigns the object's
	 * creation time, generation number, and dnode size. The generic
	 * zfs_create() has no concept of these attributes, so we smuggle
	 * the values inside the vattr's otherwise unused va_ctime,
	 * va_nblocks, and va_fsid fields.

	 */
	ZFS_TIME_DECODE(&xva.xva_vattr.va_ctime, lr->lr_crtime);
	xva.xva_vattr.va_nblocks = lr->lr_gen;
	xva.xva_vattr.va_fsid = dnodesize;

	error = dmu_object_info(zfsvfs->z_os, lr->lr_foid, NULL);
	if (error != ENOENT)
		goto bail;

	if (lr->lr_common.lrc_txtype & TX_CI)
		vflg |= FIGNORECASE;
	switch (txtype) {
	case TX_CREATE_ACL:
		aclstart = (caddr_t)(lracl + 1);
		fuidstart = (caddr_t)aclstart +
		    ZIL_ACE_LENGTH(lracl->lr_acl_bytes);
		zfsvfs->z_fuid_replay = zfs_replay_fuids(fuidstart,
		    (void *)&name, lracl->lr_fuidcnt, lracl->lr_domcnt,
		    lr->lr_uid, lr->lr_gid);
		/*FALLTHROUGH*/
	case TX_CREATE_ACL_ATTR:
		if (name == NULL) {
			lrattr = (lr_attr_t *)(caddr_t)(lracl + 1);
			xvatlen = ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
			xva.xva_vattr.va_mask |= AT_XVATTR;
			zfs_replay_xvattr(lrattr, &xva);
		}
		vsec.vsa_mask = VSA_ACE | VSA_ACE_ACLFLAGS;
		vsec.vsa_aclentp = (caddr_t)(lracl + 1) + xvatlen;
		vsec.vsa_aclcnt = lracl->lr_aclcnt;
		vsec.vsa_aclentsz = lracl->lr_acl_bytes;
		vsec.vsa_aclflags = lracl->lr_acl_flags;
		if (zfsvfs->z_fuid_replay == NULL) {
			fuidstart = (caddr_t)(lracl + 1) + xvatlen +
			    ZIL_ACE_LENGTH(lracl->lr_acl_bytes);
			zfsvfs->z_fuid_replay =
			    zfs_replay_fuids(fuidstart,
			    (void *)&name, lracl->lr_fuidcnt, lracl->lr_domcnt,
			    lr->lr_uid, lr->lr_gid);
		}

#ifdef TODO
		error = VOP_CREATE(ZTOV(dzp), name, &xva.xva_vattr,
		    0, 0, &vp, kcred, vflg, NULL, &vsec);
#else
		panic("%s:%u: unsupported condition", __func__, __LINE__);
#endif
		break;
	case TX_MKDIR_ACL:
		aclstart = (caddr_t)(lracl + 1);
		fuidstart = (caddr_t)aclstart +
		    ZIL_ACE_LENGTH(lracl->lr_acl_bytes);
		zfsvfs->z_fuid_replay = zfs_replay_fuids(fuidstart,
		    (void *)&name, lracl->lr_fuidcnt, lracl->lr_domcnt,
		    lr->lr_uid, lr->lr_gid);
		/*FALLTHROUGH*/
	case TX_MKDIR_ACL_ATTR:
		if (name == NULL) {
			lrattr = (lr_attr_t *)(caddr_t)(lracl + 1);
			xvatlen = ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
			zfs_replay_xvattr(lrattr, &xva);
		}
		vsec.vsa_mask = VSA_ACE | VSA_ACE_ACLFLAGS;
		vsec.vsa_aclentp = (caddr_t)(lracl + 1) + xvatlen;
		vsec.vsa_aclcnt = lracl->lr_aclcnt;
		vsec.vsa_aclentsz = lracl->lr_acl_bytes;
		vsec.vsa_aclflags = lracl->lr_acl_flags;
		if (zfsvfs->z_fuid_replay == NULL) {
			fuidstart = (caddr_t)(lracl + 1) + xvatlen +
			    ZIL_ACE_LENGTH(lracl->lr_acl_bytes);
			zfsvfs->z_fuid_replay =
			    zfs_replay_fuids(fuidstart,
			    (void *)&name, lracl->lr_fuidcnt, lracl->lr_domcnt,
			    lr->lr_uid, lr->lr_gid);
		}
#ifdef TODO
		error = VOP_MKDIR(ZTOV(dzp), name, &xva.xva_vattr,
		    &vp, kcred, NULL, vflg, &vsec);
#else
		panic("%s:%u: unsupported condition", __func__, __LINE__);
#endif
		break;
	default:
		error = SET_ERROR(ENOTSUP);
	}

bail:
	if (error == 0 && vp != NULL)
		VN_RELE(vp);

	VN_RELE(ZTOV(dzp));

	if (zfsvfs->z_fuid_replay)
		zfs_fuid_info_free(zfsvfs->z_fuid_replay);
	zfsvfs->z_fuid_replay = NULL;

	return (error);
}

static int
zfs_replay_create(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_create_t *lr = arg2;
	char *name = NULL;		/* location determined later */
	char *link;			/* symlink content follows name */
	znode_t *dzp;
	vnode_t *vp = NULL;
	xvattr_t xva;
	int vflg = 0;
	size_t lrsize = sizeof (lr_create_t);
	lr_attr_t *lrattr;
	void *start;
	size_t xvatlen;
	uint64_t txtype;
	struct componentname cn;
	int error;

	txtype = (lr->lr_common.lrc_txtype & ~TX_CI);
	if (byteswap) {
		byteswap_uint64_array(lr, sizeof (*lr));
		if (txtype == TX_CREATE_ATTR || txtype == TX_MKDIR_ATTR)
			zfs_replay_swap_attrs((lr_attr_t *)(lr + 1));
	}


	if ((error = zfs_zget(zfsvfs, lr->lr_doid, &dzp)) != 0)
		return (error);

	uint64_t objid = LR_FOID_GET_OBJ(lr->lr_foid);
	int dnodesize = LR_FOID_GET_SLOTS(lr->lr_foid) << DNODE_SHIFT;

	xva_init(&xva);
	zfs_init_vattr(&xva.xva_vattr, AT_TYPE | AT_MODE | AT_UID | AT_GID,
	    lr->lr_mode, lr->lr_uid, lr->lr_gid, lr->lr_rdev, objid);

	/*
	 * All forms of zfs create (create, mkdir, mkxattrdir, symlink)
	 * eventually end up in zfs_mknode(), which assigns the object's
	 * creation time, generation number, and dnode slot count. The
	 * generic zfs_create() has no concept of these attributes, so
	 * we smuggle the values inside * the vattr's otherwise unused
	 * va_ctime, va_nblocks, and va_nlink fields.
	 */
	ZFS_TIME_DECODE(&xva.xva_vattr.va_ctime, lr->lr_crtime);
	xva.xva_vattr.va_nblocks = lr->lr_gen;
	xva.xva_vattr.va_fsid = dnodesize;

	error = dmu_object_info(zfsvfs->z_os, objid, NULL);
	if (error != ENOENT)
		goto out;

	if (lr->lr_common.lrc_txtype & TX_CI)
		vflg |= FIGNORECASE;

	/*
	 * Symlinks don't have fuid info, and CIFS never creates
	 * symlinks.
	 *
	 * The _ATTR versions will grab the fuid info in their subcases.
	 */
	if ((int)lr->lr_common.lrc_txtype != TX_SYMLINK &&
	    (int)lr->lr_common.lrc_txtype != TX_MKDIR_ATTR &&
	    (int)lr->lr_common.lrc_txtype != TX_CREATE_ATTR) {
		start = (lr + 1);
		zfsvfs->z_fuid_replay =
		    zfs_replay_fuid_domain(start, &start,
		    lr->lr_uid, lr->lr_gid);
	}

	cn.cn_cred = kcred;
	cn.cn_thread = curthread;
	cn.cn_flags = SAVENAME;

	vn_lock(ZTOV(dzp), LK_EXCLUSIVE | LK_RETRY);
	switch (txtype) {
	case TX_CREATE_ATTR:
		lrattr = (lr_attr_t *)(caddr_t)(lr + 1);
		xvatlen = ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
		zfs_replay_xvattr((lr_attr_t *)((caddr_t)lr + lrsize), &xva);
		start = (caddr_t)(lr + 1) + xvatlen;
		zfsvfs->z_fuid_replay =
		    zfs_replay_fuid_domain(start, &start,
		    lr->lr_uid, lr->lr_gid);
		name = (char *)start;

		/*FALLTHROUGH*/
	case TX_CREATE:
		if (name == NULL)
			name = (char *)start;

		cn.cn_nameptr = name;
		error = VOP_CREATE(ZTOV(dzp), &vp, &cn, &xva.xva_vattr /*,vflg*/);
		break;
	case TX_MKDIR_ATTR:
		lrattr = (lr_attr_t *)(caddr_t)(lr + 1);
		xvatlen = ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
		zfs_replay_xvattr((lr_attr_t *)((caddr_t)lr + lrsize), &xva);
		start = (caddr_t)(lr + 1) + xvatlen;
		zfsvfs->z_fuid_replay =
		    zfs_replay_fuid_domain(start, &start,
		    lr->lr_uid, lr->lr_gid);
		name = (char *)start;

		/*FALLTHROUGH*/
	case TX_MKDIR:
		if (name == NULL)
			name = (char *)(lr + 1);

		cn.cn_nameptr = name;
		error = VOP_MKDIR(ZTOV(dzp), &vp, &cn, &xva.xva_vattr /*,vflg*/);
		break;
	case TX_MKXATTR:
		error = zfs_make_xattrdir(dzp, &xva.xva_vattr, &vp, kcred);
		break;
	case TX_SYMLINK:
		name = (char *)(lr + 1);
		link = name + strlen(name) + 1;
		cn.cn_nameptr = name;
		error = VOP_SYMLINK(ZTOV(dzp), &vp, &cn, &xva.xva_vattr, link /*,vflg*/);
		break;
	default:
		error = SET_ERROR(ENOTSUP);
	}
	VOP_UNLOCK(ZTOV(dzp), 0);

out:
	if (error == 0 && vp != NULL)
		VN_URELE(vp);

	VN_RELE(ZTOV(dzp));

	if (zfsvfs->z_fuid_replay)
		zfs_fuid_info_free(zfsvfs->z_fuid_replay);
	zfsvfs->z_fuid_replay = NULL;
	return (error);
}

static int
zfs_replay_remove(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_remove_t *lr = arg2;
	char *name = (char *)(lr + 1);	/* name follows lr_remove_t */
	znode_t *dzp;
	struct componentname cn;
	vnode_t *vp;
	int error;
	int vflg = 0;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	if ((error = zfs_zget(zfsvfs, lr->lr_doid, &dzp)) != 0)
		return (error);

	if (lr->lr_common.lrc_txtype & TX_CI)
		vflg |= FIGNORECASE;
	cn.cn_nameptr = name;
	cn.cn_namelen = strlen(name);
	cn.cn_nameiop = DELETE;
	cn.cn_flags = ISLASTCN | SAVENAME;
	cn.cn_lkflags = LK_EXCLUSIVE | LK_RETRY;
	cn.cn_cred = kcred;
	cn.cn_thread = curthread;
	vn_lock(ZTOV(dzp), LK_EXCLUSIVE | LK_RETRY);
	error = VOP_LOOKUP(ZTOV(dzp), &vp, &cn);
	if (error != 0) {
		VOP_UNLOCK(ZTOV(dzp), 0);
		goto fail;
	}

	switch ((int)lr->lr_common.lrc_txtype) {
	case TX_REMOVE:
		error = VOP_REMOVE(ZTOV(dzp), vp, &cn /*,vflg*/);
		break;
	case TX_RMDIR:
		error = VOP_RMDIR(ZTOV(dzp), vp, &cn /*,vflg*/);
		break;
	default:
		error = SET_ERROR(ENOTSUP);
	}
	vput(vp);
	VOP_UNLOCK(ZTOV(dzp), 0);

fail:
	VN_RELE(ZTOV(dzp));

	return (error);
}

static int
zfs_replay_link(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_link_t *lr = arg2;
	char *name = (char *)(lr + 1);	/* name follows lr_link_t */
	znode_t *dzp, *zp;
	struct componentname cn;
	int error;
	int vflg = 0;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	if ((error = zfs_zget(zfsvfs, lr->lr_doid, &dzp)) != 0)
		return (error);

	if ((error = zfs_zget(zfsvfs, lr->lr_link_obj, &zp)) != 0) {
		VN_RELE(ZTOV(dzp));
		return (error);
	}

	if (lr->lr_common.lrc_txtype & TX_CI)
		vflg |= FIGNORECASE;

	cn.cn_nameptr = name;
	cn.cn_cred = kcred;
	cn.cn_thread = curthread;
	cn.cn_flags = SAVENAME;

	vn_lock(ZTOV(dzp), LK_EXCLUSIVE | LK_RETRY);
	vn_lock(ZTOV(zp), LK_EXCLUSIVE | LK_RETRY);
	error = VOP_LINK(ZTOV(dzp), ZTOV(zp), &cn /*,vflg*/);
	VOP_UNLOCK(ZTOV(zp), 0);
	VOP_UNLOCK(ZTOV(dzp), 0);

	VN_RELE(ZTOV(zp));
	VN_RELE(ZTOV(dzp));

	return (error);
}

static int
zfs_replay_rename(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_rename_t *lr = arg2;
	char *sname = (char *)(lr + 1);	/* sname and tname follow lr_rename_t */
	char *tname = sname + strlen(sname) + 1;
	znode_t *sdzp, *tdzp;
	struct componentname scn, tcn;
	vnode_t *svp, *tvp;
	kthread_t *td = curthread;
	int error;
	int vflg = 0;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	if ((error = zfs_zget(zfsvfs, lr->lr_sdoid, &sdzp)) != 0)
		return (error);

	if ((error = zfs_zget(zfsvfs, lr->lr_tdoid, &tdzp)) != 0) {
		VN_RELE(ZTOV(sdzp));
		return (error);
	}

	if (lr->lr_common.lrc_txtype & TX_CI)
		vflg |= FIGNORECASE;
	svp = tvp = NULL;

	scn.cn_nameptr = sname;
	scn.cn_namelen = strlen(sname);
	scn.cn_nameiop = DELETE;
	scn.cn_flags = ISLASTCN | SAVENAME;
	scn.cn_lkflags = LK_EXCLUSIVE | LK_RETRY;
	scn.cn_cred = kcred;
	scn.cn_thread = td;
	vn_lock(ZTOV(sdzp), LK_EXCLUSIVE | LK_RETRY);
	error = VOP_LOOKUP(ZTOV(sdzp), &svp, &scn);
	VOP_UNLOCK(ZTOV(sdzp), 0);
	if (error != 0)
		goto fail;
	VOP_UNLOCK(svp, 0);

	tcn.cn_nameptr = tname;
	tcn.cn_namelen = strlen(tname);
	tcn.cn_nameiop = RENAME;
	tcn.cn_flags = ISLASTCN | SAVENAME;
	tcn.cn_lkflags = LK_EXCLUSIVE | LK_RETRY;
	tcn.cn_cred = kcred;
	tcn.cn_thread = td;
	vn_lock(ZTOV(tdzp), LK_EXCLUSIVE | LK_RETRY);
	error = VOP_LOOKUP(ZTOV(tdzp), &tvp, &tcn);
	if (error == EJUSTRETURN)
		tvp = NULL;
	else if (error != 0) {
		VOP_UNLOCK(ZTOV(tdzp), 0);
		goto fail;
	}

	error = VOP_RENAME(ZTOV(sdzp), svp, &scn, ZTOV(tdzp), tvp, &tcn /*,vflg*/);
	return (error);
fail:
	if (svp != NULL)
		vrele(svp);
	if (tvp != NULL)
		vrele(tvp);
	VN_RELE(ZTOV(tdzp));
	VN_RELE(ZTOV(sdzp));

	return (error);
}

static int
zfs_replay_write(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_write_t *lr = arg2;
	char *data = (char *)(lr + 1);	/* data follows lr_write_t */
	znode_t	*zp;
	int error;
	ssize_t resid;
	uint64_t eod, offset, length;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	if ((error = zfs_zget(zfsvfs, lr->lr_foid, &zp)) != 0) {
		/*
		 * As we can log writes out of order, it's possible the
		 * file has been removed. In this case just drop the write
		 * and return success.
		 */
		if (error == ENOENT)
			error = 0;
		return (error);
	}

	offset = lr->lr_offset;
	length = lr->lr_length;
	eod = offset + length;	/* end of data for this write */

	/*
	 * This may be a write from a dmu_sync() for a whole block,
	 * and may extend beyond the current end of the file.
	 * We can't just replay what was written for this TX_WRITE as
	 * a future TX_WRITE2 may extend the eof and the data for that
	 * write needs to be there. So we write the whole block and
	 * reduce the eof. This needs to be done within the single dmu
	 * transaction created within vn_rdwr -> zfs_write. So a possible
	 * new end of file is passed through in zfsvfs->z_replay_eof
	 */

	zfsvfs->z_replay_eof = 0; /* 0 means don't change end of file */

	/* If it's a dmu_sync() block, write the whole block */
	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		uint64_t blocksize = BP_GET_LSIZE(&lr->lr_blkptr);
		if (length < blocksize) {
			offset -= offset % blocksize;
			length = blocksize;
		}
		if (zp->z_size < eod)
			zfsvfs->z_replay_eof = eod;
	}

	error = vn_rdwr(UIO_WRITE, ZTOV(zp), data, length, offset,
	    UIO_SYSSPACE, 0, RLIM64_INFINITY, kcred, &resid);

	VN_RELE(ZTOV(zp));
	zfsvfs->z_replay_eof = 0;	/* safety */

	return (error);
}

/*
 * TX_WRITE2 are only generated when dmu_sync() returns EALREADY
 * meaning the pool block is already being synced. So now that we always write
 * out full blocks, all we have to do is expand the eof if
 * the file is grown.
 */
static int
zfs_replay_write2(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_write_t *lr = arg2;
	znode_t	*zp;
	int error;
	uint64_t end;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	if ((error = zfs_zget(zfsvfs, lr->lr_foid, &zp)) != 0)
		return (error);

top:
	end = lr->lr_offset + lr->lr_length;
	if (end > zp->z_size) {
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);

		zp->z_size = end;
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			VN_RELE(ZTOV(zp));
			if (error == ERESTART) {
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				goto top;
			}
			dmu_tx_abort(tx);
			return (error);
		}
		(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
		    (void *)&zp->z_size, sizeof (uint64_t), tx);

		/* Ensure the replayed seq is updated */
		(void) zil_replaying(zfsvfs->z_log, tx);

		dmu_tx_commit(tx);
	}

	VN_RELE(ZTOV(zp));

	return (error);
}

static int
zfs_replay_truncate(void *arg1, void *arg2, boolean_t byteswap)
{
#ifdef illumos
	zfsvfs_t *zfsvfs = arg1;
	lr_truncate_t *lr = arg2;
	znode_t *zp;
	flock64_t fl;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	if ((error = zfs_zget(zfsvfs, lr->lr_foid, &zp)) != 0)
		return (error);

	bzero(&fl, sizeof (fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	fl.l_start = lr->lr_offset;
	fl.l_len = lr->lr_length;

	error = VOP_SPACE(ZTOV(zp), F_FREESP, &fl, FWRITE | FOFFMAX,
	    lr->lr_offset, kcred, NULL);

	VN_RELE(ZTOV(zp));

	return (error);
#else
	ZFS_LOG(0, "Unexpected code path, report to pjd@FreeBSD.org");
	return (EOPNOTSUPP);
#endif
}

static int
zfs_replay_setattr(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_setattr_t *lr = arg2;
	znode_t *zp;
	xvattr_t xva;
	vattr_t *vap = &xva.xva_vattr;
	vnode_t *vp;
	int error;
	void *start;

	xva_init(&xva);
	if (byteswap) {
		byteswap_uint64_array(lr, sizeof (*lr));

		if ((lr->lr_mask & AT_XVATTR) &&
		    zfsvfs->z_version >= ZPL_VERSION_INITIAL)
			zfs_replay_swap_attrs((lr_attr_t *)(lr + 1));
	}

	if ((error = zfs_zget(zfsvfs, lr->lr_foid, &zp)) != 0)
		return (error);

	zfs_init_vattr(vap, lr->lr_mask, lr->lr_mode,
	    lr->lr_uid, lr->lr_gid, 0, lr->lr_foid);

	vap->va_size = lr->lr_size;
	ZFS_TIME_DECODE(&vap->va_atime, lr->lr_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, lr->lr_mtime);

	/*
	 * Fill in xvattr_t portions if necessary.
	 */

	start = (lr_setattr_t *)(lr + 1);
	if (vap->va_mask & AT_XVATTR) {
		zfs_replay_xvattr((lr_attr_t *)start, &xva);
		start = (caddr_t)start +
		    ZIL_XVAT_SIZE(((lr_attr_t *)start)->lr_attr_masksize);
	} else
		xva.xva_vattr.va_mask &= ~AT_XVATTR;

	zfsvfs->z_fuid_replay = zfs_replay_fuid_domain(start, &start,
	    lr->lr_uid, lr->lr_gid);

	vp = ZTOV(zp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_SETATTR(vp, vap, kcred);
	VOP_UNLOCK(vp, 0);

	zfs_fuid_info_free(zfsvfs->z_fuid_replay);
	zfsvfs->z_fuid_replay = NULL;
	VN_RELE(vp);

	return (error);
}

extern int zfs_setsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct);

static int
zfs_replay_acl_v0(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_acl_v0_t *lr = arg2;
	ace_t *ace = (ace_t *)(lr + 1);	/* ace array follows lr_acl_t */
	vsecattr_t vsa;
	vnode_t *vp;
	znode_t *zp;
	int error;

	if (byteswap) {
		byteswap_uint64_array(lr, sizeof (*lr));
		zfs_oldace_byteswap(ace, lr->lr_aclcnt);
	}

	if ((error = zfs_zget(zfsvfs, lr->lr_foid, &zp)) != 0)
		return (error);

	bzero(&vsa, sizeof (vsa));
	vsa.vsa_mask = VSA_ACE | VSA_ACECNT;
	vsa.vsa_aclcnt = lr->lr_aclcnt;
	vsa.vsa_aclentsz = sizeof (ace_t) * vsa.vsa_aclcnt;
	vsa.vsa_aclflags = 0;
	vsa.vsa_aclentp = ace;

	vp = ZTOV(zp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = zfs_setsecattr(vp, &vsa, 0, kcred, NULL);
	VOP_UNLOCK(vp, 0);

	VN_RELE(vp);

	return (error);
}

/*
 * Replaying ACLs is complicated by FUID support.
 * The log record may contain some optional data
 * to be used for replaying FUID's.  These pieces
 * are the actual FUIDs that were created initially.
 * The FUID table index may no longer be valid and
 * during zfs_create() a new index may be assigned.
 * Because of this the log will contain the original
 * doman+rid in order to create a new FUID.
 *
 * The individual ACEs may contain an ephemeral uid/gid which is no
 * longer valid and will need to be replaced with an actual FUID.
 *
 */
static int
zfs_replay_acl(void *arg1, void *arg2, boolean_t byteswap)
{
	zfsvfs_t *zfsvfs = arg1;
	lr_acl_t *lr = arg2;
	ace_t *ace = (ace_t *)(lr + 1);
	vsecattr_t vsa;
	znode_t *zp;
	vnode_t *vp;
	int error;

	if (byteswap) {
		byteswap_uint64_array(lr, sizeof (*lr));
		zfs_ace_byteswap(ace, lr->lr_acl_bytes, B_FALSE);
		if (lr->lr_fuidcnt) {
			byteswap_uint64_array((caddr_t)ace +
			    ZIL_ACE_LENGTH(lr->lr_acl_bytes),
			    lr->lr_fuidcnt * sizeof (uint64_t));
		}
	}

	if ((error = zfs_zget(zfsvfs, lr->lr_foid, &zp)) != 0)
		return (error);

	bzero(&vsa, sizeof (vsa));
	vsa.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS;
	vsa.vsa_aclcnt = lr->lr_aclcnt;
	vsa.vsa_aclentp = ace;
	vsa.vsa_aclentsz = lr->lr_acl_bytes;
	vsa.vsa_aclflags = lr->lr_acl_flags;

	if (lr->lr_fuidcnt) {
		void *fuidstart = (caddr_t)ace +
		    ZIL_ACE_LENGTH(lr->lr_acl_bytes);

		zfsvfs->z_fuid_replay =
		    zfs_replay_fuids(fuidstart, &fuidstart,
		    lr->lr_fuidcnt, lr->lr_domcnt, 0, 0);
	}

	vp = ZTOV(zp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = zfs_setsecattr(vp, &vsa, 0, kcred, NULL);
	VOP_UNLOCK(vp, 0);

	if (zfsvfs->z_fuid_replay)
		zfs_fuid_info_free(zfsvfs->z_fuid_replay);

	zfsvfs->z_fuid_replay = NULL;
	VN_RELE(vp);

	return (error);
}

/*
 * Callback vectors for replaying records
 */
zil_replay_func_t *zfs_replay_vector[TX_MAX_TYPE] = {
	zfs_replay_error,	/* 0 no such transaction type */
	zfs_replay_create,	/* TX_CREATE */
	zfs_replay_create,	/* TX_MKDIR */
	zfs_replay_create,	/* TX_MKXATTR */
	zfs_replay_create,	/* TX_SYMLINK */
	zfs_replay_remove,	/* TX_REMOVE */
	zfs_replay_remove,	/* TX_RMDIR */
	zfs_replay_link,	/* TX_LINK */
	zfs_replay_rename,	/* TX_RENAME */
	zfs_replay_write,	/* TX_WRITE */
	zfs_replay_truncate,	/* TX_TRUNCATE */
	zfs_replay_setattr,	/* TX_SETATTR */
	zfs_replay_acl_v0,	/* TX_ACL_V0 */
	zfs_replay_acl,		/* TX_ACL */
	zfs_replay_create_acl,	/* TX_CREATE_ACL */
	zfs_replay_create,	/* TX_CREATE_ATTR */
	zfs_replay_create_acl,	/* TX_CREATE_ACL_ATTR */
	zfs_replay_create_acl,	/* TX_MKDIR_ACL */
	zfs_replay_create,	/* TX_MKDIR_ATTR */
	zfs_replay_create_acl,	/* TX_MKDIR_ACL_ATTR */
	zfs_replay_write2,	/* TX_WRITE2 */
};
