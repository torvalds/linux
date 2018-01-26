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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/cmn_err.h>
#include "fs/fs_subr.h"
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zil.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/varargs.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/mkdev.h>
#include <sys/modctl.h>
#include <sys/refstr.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/bootconf.h>
#include <sys/sunddi.h>
#include <sys/dnlc.h>
#include <sys/dmu_objset.h>
#include <sys/spa_boot.h>
#include <sys/zpl.h>
#include "zfs_comutil.h"

enum {
	TOKEN_RO,
	TOKEN_RW,
	TOKEN_SETUID,
	TOKEN_NOSETUID,
	TOKEN_EXEC,
	TOKEN_NOEXEC,
	TOKEN_DEVICES,
	TOKEN_NODEVICES,
	TOKEN_DIRXATTR,
	TOKEN_SAXATTR,
	TOKEN_XATTR,
	TOKEN_NOXATTR,
	TOKEN_ATIME,
	TOKEN_NOATIME,
	TOKEN_RELATIME,
	TOKEN_NORELATIME,
	TOKEN_NBMAND,
	TOKEN_NONBMAND,
	TOKEN_MNTPOINT,
	TOKEN_LAST,
};

static const match_table_t zpl_tokens = {
	{ TOKEN_RO,		MNTOPT_RO },
	{ TOKEN_RW,		MNTOPT_RW },
	{ TOKEN_SETUID,		MNTOPT_SETUID },
	{ TOKEN_NOSETUID,	MNTOPT_NOSETUID },
	{ TOKEN_EXEC,		MNTOPT_EXEC },
	{ TOKEN_NOEXEC,		MNTOPT_NOEXEC },
	{ TOKEN_DEVICES,	MNTOPT_DEVICES },
	{ TOKEN_NODEVICES,	MNTOPT_NODEVICES },
	{ TOKEN_DIRXATTR,	MNTOPT_DIRXATTR },
	{ TOKEN_SAXATTR,	MNTOPT_SAXATTR },
	{ TOKEN_XATTR,		MNTOPT_XATTR },
	{ TOKEN_NOXATTR,	MNTOPT_NOXATTR },
	{ TOKEN_ATIME,		MNTOPT_ATIME },
	{ TOKEN_NOATIME,	MNTOPT_NOATIME },
	{ TOKEN_RELATIME,	MNTOPT_RELATIME },
	{ TOKEN_NORELATIME,	MNTOPT_NORELATIME },
	{ TOKEN_NBMAND,		MNTOPT_NBMAND },
	{ TOKEN_NONBMAND,	MNTOPT_NONBMAND },
	{ TOKEN_MNTPOINT,	MNTOPT_MNTPOINT "=%s" },
	{ TOKEN_LAST,		NULL },
};

static void
zfsvfs_vfs_free(vfs_t *vfsp)
{
	if (vfsp != NULL) {
		if (vfsp->vfs_mntpoint != NULL)
			strfree(vfsp->vfs_mntpoint);

		kmem_free(vfsp, sizeof (vfs_t));
	}
}

static int
zfsvfs_parse_option(char *option, int token, substring_t *args, vfs_t *vfsp)
{
	switch (token) {
	case TOKEN_RO:
		vfsp->vfs_readonly = B_TRUE;
		vfsp->vfs_do_readonly = B_TRUE;
		break;
	case TOKEN_RW:
		vfsp->vfs_readonly = B_FALSE;
		vfsp->vfs_do_readonly = B_TRUE;
		break;
	case TOKEN_SETUID:
		vfsp->vfs_setuid = B_TRUE;
		vfsp->vfs_do_setuid = B_TRUE;
		break;
	case TOKEN_NOSETUID:
		vfsp->vfs_setuid = B_FALSE;
		vfsp->vfs_do_setuid = B_TRUE;
		break;
	case TOKEN_EXEC:
		vfsp->vfs_exec = B_TRUE;
		vfsp->vfs_do_exec = B_TRUE;
		break;
	case TOKEN_NOEXEC:
		vfsp->vfs_exec = B_FALSE;
		vfsp->vfs_do_exec = B_TRUE;
		break;
	case TOKEN_DEVICES:
		vfsp->vfs_devices = B_TRUE;
		vfsp->vfs_do_devices = B_TRUE;
		break;
	case TOKEN_NODEVICES:
		vfsp->vfs_devices = B_FALSE;
		vfsp->vfs_do_devices = B_TRUE;
		break;
	case TOKEN_DIRXATTR:
		vfsp->vfs_xattr = ZFS_XATTR_DIR;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_SAXATTR:
		vfsp->vfs_xattr = ZFS_XATTR_SA;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_XATTR:
		vfsp->vfs_xattr = ZFS_XATTR_DIR;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_NOXATTR:
		vfsp->vfs_xattr = ZFS_XATTR_OFF;
		vfsp->vfs_do_xattr = B_TRUE;
		break;
	case TOKEN_ATIME:
		vfsp->vfs_atime = B_TRUE;
		vfsp->vfs_do_atime = B_TRUE;
		break;
	case TOKEN_NOATIME:
		vfsp->vfs_atime = B_FALSE;
		vfsp->vfs_do_atime = B_TRUE;
		break;
	case TOKEN_RELATIME:
		vfsp->vfs_relatime = B_TRUE;
		vfsp->vfs_do_relatime = B_TRUE;
		break;
	case TOKEN_NORELATIME:
		vfsp->vfs_relatime = B_FALSE;
		vfsp->vfs_do_relatime = B_TRUE;
		break;
	case TOKEN_NBMAND:
		vfsp->vfs_nbmand = B_TRUE;
		vfsp->vfs_do_nbmand = B_TRUE;
		break;
	case TOKEN_NONBMAND:
		vfsp->vfs_nbmand = B_FALSE;
		vfsp->vfs_do_nbmand = B_TRUE;
		break;
	case TOKEN_MNTPOINT:
		vfsp->vfs_mntpoint = match_strdup(&args[0]);
		if (vfsp->vfs_mntpoint == NULL)
			return (SET_ERROR(ENOMEM));

		break;
	default:
		break;
	}

	return (0);
}

/*
 * Parse the raw mntopts and return a vfs_t describing the options.
 */
static int
zfsvfs_parse_options(char *mntopts, vfs_t **vfsp)
{
	vfs_t *tmp_vfsp;
	int error;

	tmp_vfsp = kmem_zalloc(sizeof (vfs_t), KM_SLEEP);

	if (mntopts != NULL) {
		substring_t args[MAX_OPT_ARGS];
		char *tmp_mntopts, *p, *t;
		int token;

		tmp_mntopts = t = strdup(mntopts);
		if (tmp_mntopts == NULL)
			return (SET_ERROR(ENOMEM));

		while ((p = strsep(&t, ",")) != NULL) {
			if (!*p)
				continue;

			args[0].to = args[0].from = NULL;
			token = match_token(p, zpl_tokens, args);
			error = zfsvfs_parse_option(p, token, args, tmp_vfsp);
			if (error) {
				strfree(tmp_mntopts);
				zfsvfs_vfs_free(tmp_vfsp);
				return (error);
			}
		}

		strfree(tmp_mntopts);
	}

	*vfsp = tmp_vfsp;

	return (0);
}

boolean_t
zfs_is_readonly(zfsvfs_t *zfsvfs)
{
	return (!!(zfsvfs->z_sb->s_flags & MS_RDONLY));
}

/*ARGSUSED*/
int
zfs_sync(struct super_block *sb, int wait, cred_t *cr)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;

	/*
	 * Data integrity is job one.  We don't want a compromised kernel
	 * writing to the storage pool, so we never sync during panic.
	 */
	if (unlikely(oops_in_progress))
		return (0);

	/*
	 * Semantically, the only requirement is that the sync be initiated.
	 * The DMU syncs out txgs frequently, so there's nothing to do.
	 */
	if (!wait)
		return (0);

	if (zfsvfs != NULL) {
		/*
		 * Sync a specific filesystem.
		 */
		dsl_pool_t *dp;

		ZFS_ENTER(zfsvfs);
		dp = dmu_objset_pool(zfsvfs->z_os);

		/*
		 * If the system is shutting down, then skip any
		 * filesystems which may exist on a suspended pool.
		 */
		if (spa_suspended(dp->dp_spa)) {
			ZFS_EXIT(zfsvfs);
			return (0);
		}

		if (zfsvfs->z_log != NULL)
			zil_commit(zfsvfs->z_log, 0);

		ZFS_EXIT(zfsvfs);
	} else {
		/*
		 * Sync all ZFS filesystems.  This is what happens when you
		 * run sync(1M).  Unlike other filesystems, ZFS honors the
		 * request by waiting for all pools to commit all dirty data.
		 */
		spa_sync_allpools();
	}

	return (0);
}

static void
atime_changed_cb(void *arg, uint64_t newval)
{
	((zfsvfs_t *)arg)->z_atime = newval;
}

static void
relatime_changed_cb(void *arg, uint64_t newval)
{
	((zfsvfs_t *)arg)->z_relatime = newval;
}

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == ZFS_XATTR_OFF) {
		zfsvfs->z_flags &= ~ZSB_XATTR;
	} else {
		zfsvfs->z_flags |= ZSB_XATTR;

		if (newval == ZFS_XATTR_SA)
			zfsvfs->z_xattr_sa = B_TRUE;
		else
			zfsvfs->z_xattr_sa = B_FALSE;
	}
}

static void
acltype_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	switch (newval) {
	case ZFS_ACLTYPE_OFF:
		zfsvfs->z_acl_type = ZFS_ACLTYPE_OFF;
		zfsvfs->z_sb->s_flags &= ~MS_POSIXACL;
		break;
	case ZFS_ACLTYPE_POSIXACL:
#ifdef CONFIG_FS_POSIX_ACL
		zfsvfs->z_acl_type = ZFS_ACLTYPE_POSIXACL;
		zfsvfs->z_sb->s_flags |= MS_POSIXACL;
#else
		zfsvfs->z_acl_type = ZFS_ACLTYPE_OFF;
		zfsvfs->z_sb->s_flags &= ~MS_POSIXACL;
#endif /* CONFIG_FS_POSIX_ACL */
		break;
	default:
		break;
	}
}

static void
blksz_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	ASSERT3U(newval, <=, spa_maxblocksize(dmu_objset_spa(zfsvfs->z_os)));
	ASSERT3U(newval, >=, SPA_MINBLOCKSIZE);
	ASSERT(ISP2(newval));

	zfsvfs->z_max_blksz = newval;
}

static void
readonly_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	struct super_block *sb = zfsvfs->z_sb;

	if (sb == NULL)
		return;

	if (newval)
		sb->s_flags |= MS_RDONLY;
	else
		sb->s_flags &= ~MS_RDONLY;
}

static void
devices_changed_cb(void *arg, uint64_t newval)
{
}

static void
setuid_changed_cb(void *arg, uint64_t newval)
{
}

static void
exec_changed_cb(void *arg, uint64_t newval)
{
}

static void
nbmand_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	struct super_block *sb = zfsvfs->z_sb;

	if (sb == NULL)
		return;

	if (newval == TRUE)
		sb->s_flags |= MS_MANDLOCK;
	else
		sb->s_flags &= ~MS_MANDLOCK;
}

static void
snapdir_changed_cb(void *arg, uint64_t newval)
{
	((zfsvfs_t *)arg)->z_show_ctldir = newval;
}

static void
vscan_changed_cb(void *arg, uint64_t newval)
{
	((zfsvfs_t *)arg)->z_vscan = newval;
}

static void
acl_inherit_changed_cb(void *arg, uint64_t newval)
{
	((zfsvfs_t *)arg)->z_acl_inherit = newval;
}

static int
zfs_register_callbacks(vfs_t *vfsp)
{
	struct dsl_dataset *ds = NULL;
	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	int error = 0;

	ASSERT(vfsp);
	zfsvfs = vfsp->vfs_data;
	ASSERT(zfsvfs);
	os = zfsvfs->z_os;

	/*
	 * The act of registering our callbacks will destroy any mount
	 * options we may have.  In order to enable temporary overrides
	 * of mount options, we stash away the current values and
	 * restore them after we register the callbacks.
	 */
	if (zfs_is_readonly(zfsvfs) || !spa_writeable(dmu_objset_spa(os))) {
		vfsp->vfs_do_readonly = B_TRUE;
		vfsp->vfs_readonly = B_TRUE;
	}

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	ds = dmu_objset_ds(os);
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	error = dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ATIME), atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RELATIME), relatime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_XATTR), xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RECORDSIZE), blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_READONLY), readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_DEVICES), devices_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SETUID), setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_EXEC), exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SNAPDIR), snapdir_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLTYPE), acltype_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLINHERIT), acl_inherit_changed_cb,
	    zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_VSCAN), vscan_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_NBMAND), nbmand_changed_cb, zfsvfs);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
	if (error)
		goto unregister;

	/*
	 * Invoke our callbacks to restore temporary mount options.
	 */
	if (vfsp->vfs_do_readonly)
		readonly_changed_cb(zfsvfs, vfsp->vfs_readonly);
	if (vfsp->vfs_do_setuid)
		setuid_changed_cb(zfsvfs, vfsp->vfs_setuid);
	if (vfsp->vfs_do_exec)
		exec_changed_cb(zfsvfs, vfsp->vfs_exec);
	if (vfsp->vfs_do_devices)
		devices_changed_cb(zfsvfs, vfsp->vfs_devices);
	if (vfsp->vfs_do_xattr)
		xattr_changed_cb(zfsvfs, vfsp->vfs_xattr);
	if (vfsp->vfs_do_atime)
		atime_changed_cb(zfsvfs, vfsp->vfs_atime);
	if (vfsp->vfs_do_relatime)
		relatime_changed_cb(zfsvfs, vfsp->vfs_relatime);
	if (vfsp->vfs_do_nbmand)
		nbmand_changed_cb(zfsvfs, vfsp->vfs_nbmand);

	return (0);

unregister:
	dsl_prop_unregister_all(ds, zfsvfs);
	return (error);
}

static int
zfs_space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (SET_ERROR(ENOENT));

	/*
	 * If we have a NULL data pointer
	 * then assume the id's aren't changing and
	 * return EEXIST to the dmu to let it know to
	 * use the same ids
	 */
	if (data == NULL)
		return (SET_ERROR(EEXIST));

	if (bonustype == DMU_OT_ZNODE) {
		znode_phys_t *znp = data;
		*userp = znp->zp_uid;
		*groupp = znp->zp_gid;
	} else {
		int hdrsize;
		sa_hdr_phys_t *sap = data;
		sa_hdr_phys_t sa = *sap;
		boolean_t swap = B_FALSE;

		ASSERT(bonustype == DMU_OT_SA);

		if (sa.sa_magic == 0) {
			/*
			 * This should only happen for newly created
			 * files that haven't had the znode data filled
			 * in yet.
			 */
			*userp = 0;
			*groupp = 0;
			return (0);
		}
		if (sa.sa_magic == BSWAP_32(SA_MAGIC)) {
			sa.sa_magic = SA_MAGIC;
			sa.sa_layout_info = BSWAP_16(sa.sa_layout_info);
			swap = B_TRUE;
		} else {
			VERIFY3U(sa.sa_magic, ==, SA_MAGIC);
		}

		hdrsize = sa_hdrsize(&sa);
		VERIFY3U(hdrsize, >=, sizeof (sa_hdr_phys_t));
		*userp = *((uint64_t *)((uintptr_t)data + hdrsize +
		    SA_UID_OFFSET));
		*groupp = *((uint64_t *)((uintptr_t)data + hdrsize +
		    SA_GID_OFFSET));
		if (swap) {
			*userp = BSWAP_64(*userp);
			*groupp = BSWAP_64(*groupp);
		}
	}
	return (0);
}

static void
fuidstr_to_sid(zfsvfs_t *zfsvfs, const char *fuidstr,
    char *domainbuf, int buflen, uid_t *ridp)
{
	uint64_t fuid;
	const char *domain;

	fuid = zfs_strtonum(fuidstr, NULL);

	domain = zfs_fuid_find_by_idx(zfsvfs, FUID_INDEX(fuid));
	if (domain)
		(void) strlcpy(domainbuf, domain, buflen);
	else
		domainbuf[0] = '\0';
	*ridp = FUID_RID(fuid);
}

static uint64_t
zfs_userquota_prop_to_obj(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type)
{
	switch (type) {
	case ZFS_PROP_USERUSED:
	case ZFS_PROP_USEROBJUSED:
		return (DMU_USERUSED_OBJECT);
	case ZFS_PROP_GROUPUSED:
	case ZFS_PROP_GROUPOBJUSED:
		return (DMU_GROUPUSED_OBJECT);
	case ZFS_PROP_USERQUOTA:
		return (zfsvfs->z_userquota_obj);
	case ZFS_PROP_GROUPQUOTA:
		return (zfsvfs->z_groupquota_obj);
	case ZFS_PROP_USEROBJQUOTA:
		return (zfsvfs->z_userobjquota_obj);
	case ZFS_PROP_GROUPOBJQUOTA:
		return (zfsvfs->z_groupobjquota_obj);
	default:
		return (ZFS_NO_OBJECT);
	}
}

int
zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep)
{
	int error;
	zap_cursor_t zc;
	zap_attribute_t za;
	zfs_useracct_t *buf = vbuf;
	uint64_t obj;
	int offset = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	if ((type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_USEROBJQUOTA || type == ZFS_PROP_GROUPOBJQUOTA) &&
	    !dmu_objset_userobjspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == ZFS_NO_OBJECT) {
		*bufsizep = 0;
		return (0);
	}

	if (type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED)
		offset = DMU_OBJACCT_PREFIX_LEN;

	for (zap_cursor_init_serialized(&zc, zfsvfs->z_os, obj, *cookiep);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((uintptr_t)buf - (uintptr_t)vbuf + sizeof (zfs_useracct_t) >
		    *bufsizep)
			break;

		/*
		 * skip object quota (with zap name prefix DMU_OBJACCT_PREFIX)
		 * when dealing with block quota and vice versa.
		 */
		if ((offset > 0) != (strncmp(za.za_name, DMU_OBJACCT_PREFIX,
		    DMU_OBJACCT_PREFIX_LEN) == 0))
			continue;

		fuidstr_to_sid(zfsvfs, za.za_name + offset,
		    buf->zu_domain, sizeof (buf->zu_domain), &buf->zu_rid);

		buf->zu_space = za.za_first_integer;
		buf++;
	}
	if (error == ENOENT)
		error = 0;

	ASSERT3U((uintptr_t)buf - (uintptr_t)vbuf, <=, *bufsizep);
	*bufsizep = (uintptr_t)buf - (uintptr_t)vbuf;
	*cookiep = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);
	return (error);
}

/*
 * buf must be big enough (eg, 32 bytes)
 */
static int
id_to_fuidstr(zfsvfs_t *zfsvfs, const char *domain, uid_t rid,
    char *buf, boolean_t addok)
{
	uint64_t fuid;
	int domainid = 0;

	if (domain && domain[0]) {
		domainid = zfs_fuid_find_by_domain(zfsvfs, domain, NULL, addok);
		if (domainid == -1)
			return (SET_ERROR(ENOENT));
	}
	fuid = FUID_ENCODE(domainid, rid);
	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	return (0);
}

int
zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valp)
{
	char buf[20 + DMU_OBJACCT_PREFIX_LEN];
	int offset = 0;
	int err;
	uint64_t obj;

	*valp = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	if ((type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED ||
	    type == ZFS_PROP_USEROBJQUOTA || type == ZFS_PROP_GROUPOBJQUOTA) &&
	    !dmu_objset_userobjspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == ZFS_NO_OBJECT)
		return (0);

	if (type == ZFS_PROP_USEROBJUSED || type == ZFS_PROP_GROUPOBJUSED) {
		strlcpy(buf, DMU_OBJACCT_PREFIX, DMU_OBJACCT_PREFIX_LEN + 1);
		offset = DMU_OBJACCT_PREFIX_LEN;
	}

	err = id_to_fuidstr(zfsvfs, domain, rid, buf + offset, B_FALSE);
	if (err)
		return (err);

	err = zap_lookup(zfsvfs->z_os, obj, buf, 8, 1, valp);
	if (err == ENOENT)
		err = 0;
	return (err);
}

int
zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota)
{
	char buf[32];
	int err;
	dmu_tx_t *tx;
	uint64_t *objp;
	boolean_t fuid_dirtied;

	if (zfsvfs->z_version < ZPL_VERSION_USERSPACE)
		return (SET_ERROR(ENOTSUP));

	switch (type) {
	case ZFS_PROP_USERQUOTA:
		objp = &zfsvfs->z_userquota_obj;
		break;
	case ZFS_PROP_GROUPQUOTA:
		objp = &zfsvfs->z_groupquota_obj;
		break;
	case ZFS_PROP_USEROBJQUOTA:
		objp = &zfsvfs->z_userobjquota_obj;
		break;
	case ZFS_PROP_GROUPOBJQUOTA:
		objp = &zfsvfs->z_groupobjquota_obj;
		break;
	default:
		return (SET_ERROR(EINVAL));
	}

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_TRUE);
	if (err)
		return (err);
	fuid_dirtied = zfsvfs->z_fuid_dirty;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, *objp ? *objp : DMU_NEW_OBJECT, B_TRUE, NULL);
	if (*objp == 0) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    zfs_userquota_prop_prefixes[type]);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	mutex_enter(&zfsvfs->z_lock);
	if (*objp == 0) {
		*objp = zap_create(zfsvfs->z_os, DMU_OT_USERGROUP_QUOTA,
		    DMU_OT_NONE, 0, tx);
		VERIFY(0 == zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    zfs_userquota_prop_prefixes[type], 8, 1, objp, tx));
	}
	mutex_exit(&zfsvfs->z_lock);

	if (quota == 0) {
		err = zap_remove(zfsvfs->z_os, *objp, buf, tx);
		if (err == ENOENT)
			err = 0;
	} else {
		err = zap_update(zfsvfs->z_os, *objp, buf, 8, 1, &quota, tx);
	}
	ASSERT(err == 0);
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	dmu_tx_commit(tx);
	return (err);
}

boolean_t
zfs_fuid_overobjquota(zfsvfs_t *zfsvfs, boolean_t isgroup, uint64_t fuid)
{
	char buf[20 + DMU_OBJACCT_PREFIX_LEN];
	uint64_t used, quota, usedobj, quotaobj;
	int err;

	if (!dmu_objset_userobjspace_present(zfsvfs->z_os)) {
		if (dmu_objset_userobjspace_upgradable(zfsvfs->z_os))
			dmu_objset_userobjspace_upgrade(zfsvfs->z_os);
		return (B_FALSE);
	}

	usedobj = isgroup ? DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
	quotaobj = isgroup ? zfsvfs->z_groupobjquota_obj :
	    zfsvfs->z_userobjquota_obj;
	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

	(void) sprintf(buf, DMU_OBJACCT_PREFIX "%llx", (longlong_t)fuid);
	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
}

boolean_t
zfs_fuid_overquota(zfsvfs_t *zfsvfs, boolean_t isgroup, uint64_t fuid)
{
	char buf[20];
	uint64_t used, quota, usedobj, quotaobj;
	int err;

	usedobj = isgroup ? DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
}

boolean_t
zfs_owner_overquota(zfsvfs_t *zfsvfs, znode_t *zp, boolean_t isgroup)
{
	uint64_t fuid;
	uint64_t quotaobj;
	struct inode *ip = ZTOI(zp);

	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	fuid = isgroup ? KGID_TO_SGID(ip->i_gid) : KUID_TO_SUID(ip->i_uid);

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	return (zfs_fuid_overquota(zfsvfs, isgroup, fuid));
}

/*
 * Associate this zfsvfs with the given objset, which must be owned.
 * This will cache a bunch of on-disk state from the objset in the
 * zfsvfs.
 */
static int
zfsvfs_init(zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;
	uint64_t val;

	zfsvfs->z_max_blksz = SPA_OLD_MAXBLOCKSIZE;
	zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;
	zfsvfs->z_os = os;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error != 0)
		return (error);
	if (zfsvfs->z_version >
	    zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
		(void) printk("Can't mount a version %lld file system "
		    "on a version %lld pool\n. Pool must be upgraded to mount "
		    "this file system.", (u_longlong_t)zfsvfs->z_version,
		    (u_longlong_t)spa_version(dmu_objset_spa(os)));
		return (SET_ERROR(ENOTSUP));
	}
	error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_norm = (int)val;

	error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_utf8 = (val != 0);

	error = zfs_get_zplprop(os, ZFS_PROP_CASE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_case = (uint_t)val;

	if ((error = zfs_get_zplprop(os, ZFS_PROP_ACLTYPE, &val)) != 0)
		return (error);
	zfsvfs->z_acl_type = (uint_t)val;

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);

	uint64_t sa_obj = 0;
	if (zfsvfs->z_use_sa) {
		/* should either have both of these objects or none */
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);
		if (error != 0)
			return (error);

		error = zfs_get_zplprop(os, ZFS_PROP_XATTR, &val);
		if ((error == 0) && (val == ZFS_XATTR_SA))
			zfsvfs->z_xattr_sa = B_TRUE;
	}

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error != 0)
		return (error);

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error != 0)
		return (error);
	ASSERT(zfsvfs->z_root != 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USEROBJQUOTA],
	    8, 1, &zfsvfs->z_userobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPOBJQUOTA],
	    8, 1, &zfsvfs->z_groupobjquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupobjquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES, 8, 1,
	    &zfsvfs->z_fuid_obj);
	if (error == ENOENT)
		zfsvfs->z_fuid_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SHARES_DIR, 8, 1,
	    &zfsvfs->z_shares_dir);
	if (error == ENOENT)
		zfsvfs->z_shares_dir = 0;
	else if (error != 0)
		return (error);

	return (0);
}

int
zfsvfs_create(const char *osname, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	/*
	 * We claim to always be readonly so we can open snapshots;
	 * other ZPL code will prevent us from writing to snapshots.
	 */
	error = dmu_objset_own(osname, DMU_OST_ZFS, B_TRUE, zfsvfs, &os);
	if (error) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	zfsvfs->z_vfs = NULL;
	zfsvfs->z_sb = NULL;
	zfsvfs->z_parent = zfsvfs;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));
	rrm_init(&zfsvfs->z_teardown_lock, B_FALSE);
	rw_init(&zfsvfs->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);

	int size = MIN(1 << (highbit64(zfs_object_mutex_size) - 1),
	    ZFS_OBJ_MTX_MAX);
	zfsvfs->z_hold_size = size;
	zfsvfs->z_hold_trees = vmem_zalloc(sizeof (avl_tree_t) * size,
	    KM_SLEEP);
	zfsvfs->z_hold_locks = vmem_zalloc(sizeof (kmutex_t) * size, KM_SLEEP);
	for (int i = 0; i != size; i++) {
		avl_create(&zfsvfs->z_hold_trees[i], zfs_znode_hold_compare,
		    sizeof (znode_hold_t), offsetof(znode_hold_t, zh_node));
		mutex_init(&zfsvfs->z_hold_locks[i], NULL, MUTEX_DEFAULT, NULL);
	}

	error = zfsvfs_init(zfsvfs, os);
	if (error != 0) {
		dmu_objset_disown(os, zfsvfs);
		*zfvp = NULL;
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	*zfvp = zfsvfs;
	return (0);
}

static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;

	error = zfs_register_callbacks(zfsvfs->z_vfs);
	if (error)
		return (error);

	zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {
		boolean_t readonly;

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */
		readonly = zfs_is_readonly(zfsvfs);
		if (readonly != 0)
			readonly_changed_cb(zfsvfs, B_FALSE);
		else
			zfs_unlinked_drain(zfsvfs);

		/*
		 * Parse and replay the intent log.
		 *
		 * Because of ziltest, this must be done after
		 * zfs_unlinked_drain().  (Further note: ziltest
		 * doesn't use readonly mounts, where
		 * zfs_unlinked_drain() isn't called.)  This is because
		 * ziltest causes spa_sync() to think it's committed,
		 * but actually it is not, so the intent log contains
		 * many txg's worth of changes.
		 *
		 * In particular, if object N is in the unlinked set in
		 * the last txg to actually sync, then it could be
		 * actually freed in a later txg and then reallocated
		 * in a yet later txg.  This would write a "create
		 * object N" record to the intent log.  Normally, this
		 * would be fine because the spa_sync() would have
		 * written out the fact that object N is free, before
		 * we could write the "create object N" intent log
		 * record.
		 *
		 * But when we are in ziltest mode, we advance the "open
		 * txg" without actually spa_sync()-ing the changes to
		 * disk.  So we would see that object N is still
		 * allocated and in the unlinked set, and there is an
		 * intent log record saying to allocate it.
		 */
		if (spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			if (zil_replay_disable) {
				zil_destroy(zfsvfs->z_log, B_FALSE);
			} else {
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
			}
		}

		/* restore readonly bit */
		if (readonly != 0)
			readonly_changed_cb(zfsvfs, B_TRUE);
	}

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	return (0);
}

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i, size = zfsvfs->z_hold_size;

	zfs_fuid_destroy(zfsvfs);

	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);
	list_destroy(&zfsvfs->z_all_znodes);
	rrm_destroy(&zfsvfs->z_teardown_lock);
	rw_destroy(&zfsvfs->z_teardown_inactive_lock);
	rw_destroy(&zfsvfs->z_fuid_lock);
	for (i = 0; i != size; i++) {
		avl_destroy(&zfsvfs->z_hold_trees[i]);
		mutex_destroy(&zfsvfs->z_hold_locks[i]);
	}
	vmem_free(zfsvfs->z_hold_trees, sizeof (avl_tree_t) * size);
	vmem_free(zfsvfs->z_hold_locks, sizeof (kmutex_t) * size);
	zfsvfs_vfs_free(zfsvfs->z_vfs);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;

	if (!dmu_objset_is_snapshot(os))
		dsl_prop_unregister_all(dmu_objset_ds(os), zfsvfs);
}

#ifdef HAVE_MLSLABEL
/*
 * Check that the hex label string is appropriate for the dataset being
 * mounted into the global_zone proper.
 *
 * Return an error if the hex label string is not default or
 * admin_low/admin_high.  For admin_low labels, the corresponding
 * dataset must be readonly.
 */
int
zfs_check_global_label(const char *dsname, const char *hexsl)
{
	if (strcasecmp(hexsl, ZFS_MLSLABEL_DEFAULT) == 0)
		return (0);
	if (strcasecmp(hexsl, ADMIN_HIGH) == 0)
		return (0);
	if (strcasecmp(hexsl, ADMIN_LOW) == 0) {
		/* must be readonly */
		uint64_t rdonly;

		if (dsl_prop_get_integer(dsname,
		    zfs_prop_to_name(ZFS_PROP_READONLY), &rdonly, NULL))
			return (SET_ERROR(EACCES));
		return (rdonly ? 0 : EACCES);
	}
	return (SET_ERROR(EACCES));
}
#endif /* HAVE_MLSLABEL */

int
zfs_statvfs(struct dentry *dentry, struct kstatfs *statp)
{
	zfsvfs_t *zfsvfs = dentry->d_sb->s_fs_info;
	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	uint64_t fsid;
	uint32_t bshift;

	ZFS_ENTER(zfsvfs);

	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	fsid = dmu_objset_fsid_guid(zfsvfs->z_os);
	/*
	 * The underlying storage pool actually uses multiple block
	 * size.  Under Solaris frsize (fragment size) is reported as
	 * the smallest block size we support, and bsize (block size)
	 * as the filesystem's maximum block size.  Unfortunately,
	 * under Linux the fragment size and block size are often used
	 * interchangeably.  Thus we are forced to report both of them
	 * as the filesystem's maximum block size.
	 */
	statp->f_frsize = zfsvfs->z_max_blksz;
	statp->f_bsize = zfsvfs->z_max_blksz;
	bshift = fls(statp->f_bsize) - 1;

	/*
	 * The following report "total" blocks of various kinds in
	 * the file system, but reported in terms of f_bsize - the
	 * "preferred" size.
	 */

	statp->f_blocks = (refdbytes + availbytes) >> bshift;
	statp->f_bfree = availbytes >> bshift;
	statp->f_bavail = statp->f_bfree; /* no root reservation */

	/*
	 * statvfs() should really be called statufs(), because it assumes
	 * static metadata.  ZFS doesn't preallocate files, so the best
	 * we can do is report the max that could possibly fit in f_files,
	 * and that minus the number actually used in f_ffree.
	 * For f_ffree, report the smaller of the number of object available
	 * and the number of blocks (each object will take at least a block).
	 */
	statp->f_ffree = MIN(availobjs, availbytes >> DNODE_SHIFT);
	statp->f_files = statp->f_ffree + usedobjs;
	statp->f_fsid.val[0] = (uint32_t)fsid;
	statp->f_fsid.val[1] = (uint32_t)(fsid >> 32);
	statp->f_type = ZFS_SUPER_MAGIC;
	statp->f_namelen = MAXNAMELEN - 1;

	/*
	 * We have all of 40 characters to stuff a string here.
	 * Is there anything useful we could/should provide?
	 */
	bzero(statp->f_spare, sizeof (statp->f_spare));

	ZFS_EXIT(zfsvfs);
	return (0);
}

int
zfs_root(zfsvfs_t *zfsvfs, struct inode **ipp)
{
	znode_t *rootzp;
	int error;

	ZFS_ENTER(zfsvfs);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (error == 0)
		*ipp = ZTOI(rootzp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

#ifdef HAVE_D_PRUNE_ALIASES
/*
 * Linux kernels older than 3.1 do not support a per-filesystem shrinker.
 * To accommodate this we must improvise and manually walk the list of znodes
 * attempting to prune dentries in order to be able to drop the inodes.
 *
 * To avoid scanning the same znodes multiple times they are always rotated
 * to the end of the z_all_znodes list.  New znodes are inserted at the
 * end of the list so we're always scanning the oldest znodes first.
 */
static int
zfs_prune_aliases(zfsvfs_t *zfsvfs, unsigned long nr_to_scan)
{
	znode_t **zp_array, *zp;
	int max_array = MIN(nr_to_scan, PAGE_SIZE * 8 / sizeof (znode_t *));
	int objects = 0;
	int i = 0, j = 0;

	zp_array = kmem_zalloc(max_array * sizeof (znode_t *), KM_SLEEP);

	mutex_enter(&zfsvfs->z_znodes_lock);
	while ((zp = list_head(&zfsvfs->z_all_znodes)) != NULL) {

		if ((i++ > nr_to_scan) || (j >= max_array))
			break;

		ASSERT(list_link_active(&zp->z_link_node));
		list_remove(&zfsvfs->z_all_znodes, zp);
		list_insert_tail(&zfsvfs->z_all_znodes, zp);

		/* Skip active znodes and .zfs entries */
		if (MUTEX_HELD(&zp->z_lock) || zp->z_is_ctldir)
			continue;

		if (igrab(ZTOI(zp)) == NULL)
			continue;

		zp_array[j] = zp;
		j++;
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	for (i = 0; i < j; i++) {
		zp = zp_array[i];

		ASSERT3P(zp, !=, NULL);
		d_prune_aliases(ZTOI(zp));

		if (atomic_read(&ZTOI(zp)->i_count) == 1)
			objects++;

		iput(ZTOI(zp));
	}

	kmem_free(zp_array, max_array * sizeof (znode_t *));

	return (objects);
}
#endif /* HAVE_D_PRUNE_ALIASES */

/*
 * The ARC has requested that the filesystem drop entries from the dentry
 * and inode caches.  This can occur when the ARC needs to free meta data
 * blocks but can't because they are all pinned by entries in these caches.
 */
int
zfs_prune(struct super_block *sb, unsigned long nr_to_scan, int *objects)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;
	int error = 0;
#if defined(HAVE_SHRINK) || defined(HAVE_SPLIT_SHRINKER_CALLBACK)
	struct shrinker *shrinker = &sb->s_shrink;
	struct shrink_control sc = {
		.nr_to_scan = nr_to_scan,
		.gfp_mask = GFP_KERNEL,
	};
#endif

	ZFS_ENTER(zfsvfs);

#if defined(HAVE_SPLIT_SHRINKER_CALLBACK) && \
	defined(SHRINK_CONTROL_HAS_NID) && \
	defined(SHRINKER_NUMA_AWARE)
	if (sb->s_shrink.flags & SHRINKER_NUMA_AWARE) {
		*objects = 0;
		for_each_online_node(sc.nid) {
			*objects += (*shrinker->scan_objects)(shrinker, &sc);
		}
	} else {
			*objects = (*shrinker->scan_objects)(shrinker, &sc);
	}

#elif defined(HAVE_SPLIT_SHRINKER_CALLBACK)
	*objects = (*shrinker->scan_objects)(shrinker, &sc);
#elif defined(HAVE_SHRINK)
	*objects = (*shrinker->shrink)(shrinker, &sc);
#elif defined(HAVE_D_PRUNE_ALIASES)
#define	D_PRUNE_ALIASES_IS_DEFAULT
	*objects = zfs_prune_aliases(zfsvfs, nr_to_scan);
#else
#error "No available dentry and inode cache pruning mechanism."
#endif

#if defined(HAVE_D_PRUNE_ALIASES) && !defined(D_PRUNE_ALIASES_IS_DEFAULT)
#undef	D_PRUNE_ALIASES_IS_DEFAULT
	/*
	 * Fall back to zfs_prune_aliases if the kernel's per-superblock
	 * shrinker couldn't free anything, possibly due to the inodes being
	 * allocated in a different memcg.
	 */
	if (*objects == 0)
		*objects = zfs_prune_aliases(zfsvfs, nr_to_scan);
#endif

	ZFS_EXIT(zfsvfs);

	dprintf_ds(zfsvfs->z_os->os_dsl_dataset,
	    "pruning, nr_to_scan=%lu objects=%d error=%d\n",
	    nr_to_scan, *objects, error);

	return (error);
}

/*
 * Teardown the zfsvfs_t.
 *
 * Note, if 'unmounting' is FALSE, we return with the 'z_teardown_lock'
 * and 'z_teardown_inactive_lock' held.
 */
static int
zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting)
{
	znode_t	*zp;

	/*
	 * If someone has not already unmounted this file system,
	 * drain the iput_taskq to ensure all active references to the
	 * zfsvfs_t have been handled only then can it be safely destroyed.
	 */
	if (zfsvfs->z_os) {
		/*
		 * If we're unmounting we have to wait for the list to
		 * drain completely.
		 *
		 * If we're not unmounting there's no guarantee the list
		 * will drain completely, but iputs run from the taskq
		 * may add the parents of dir-based xattrs to the taskq
		 * so we want to wait for these.
		 *
		 * We can safely read z_nr_znodes without locking because the
		 * VFS has already blocked operations which add to the
		 * z_all_znodes list and thus increment z_nr_znodes.
		 */
		int round = 0;
		while (zfsvfs->z_nr_znodes > 0) {
			taskq_wait_outstanding(dsl_pool_iput_taskq(
			    dmu_objset_pool(zfsvfs->z_os)), 0);
			if (++round > 1 && !unmounting)
				break;
		}
	}

	rrm_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);

	if (!unmounting) {
		/*
		 * We purge the parent filesystem's super block as the
		 * parent filesystem and all of its snapshots have their
		 * inode's super block set to the parent's filesystem's
		 * super block.  Note,  'z_parent' is self referential
		 * for non-snapshots.
		 */
		shrink_dcache_sb(zfsvfs->z_parent->z_sb);
	}

	/*
	 * Close the zil. NB: Can't close the zil while zfs_inactive
	 * threads are blocked as zil_close can call zfs_inactive.
	 */
	if (zfsvfs->z_log) {
		zil_close(zfsvfs->z_log);
		zfsvfs->z_log = NULL;
	}

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_WRITER);

	/*
	 * If we are not unmounting (ie: online recv) and someone already
	 * unmounted this file system while we were doing the switcheroo,
	 * or a reopen of z_os failed then just bail out now.
	 */
	if (!unmounting && (zfsvfs->z_unmounted || zfsvfs->z_os == NULL)) {
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
		return (SET_ERROR(EIO));
	}

	/*
	 * At this point there are no VFS ops active, and any new VFS ops
	 * will fail with EIO since we have z_teardown_lock for writer (only
	 * relevant for forced unmount).
	 *
	 * Release all holds on dbufs.
	 */
	if (!unmounting) {
		mutex_enter(&zfsvfs->z_znodes_lock);
		for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
		    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
			if (zp->z_sa_hdl)
				zfs_znode_dmu_fini(zp);
		}
		mutex_exit(&zfsvfs->z_znodes_lock);
	}

	/*
	 * If we are unmounting, set the unmounted flag and let new VFS ops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other VFS ops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
	}

	/*
	 * z_os will be NULL if there was an error in attempting to reopen
	 * zfsvfs, so just return as the properties had already been
	 *
	 * unregistered and cached data had been evicted before.
	 */
	if (zfsvfs->z_os == NULL)
		return (0);

	/*
	 * Unregister properties.
	 */
	zfs_unregister_callbacks(zfsvfs);

	/*
	 * Evict cached data
	 */
	if (dsl_dataset_is_dirty(dmu_objset_ds(zfsvfs->z_os)) &&
	    !zfs_is_readonly(zfsvfs))
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	dmu_objset_evict_dbufs(zfsvfs->z_os);

	return (0);
}

#if !defined(HAVE_2ARGS_BDI_SETUP_AND_REGISTER) && \
	!defined(HAVE_3ARGS_BDI_SETUP_AND_REGISTER)
atomic_long_t zfs_bdi_seq = ATOMIC_LONG_INIT(0);
#endif

int
zfs_domount(struct super_block *sb, zfs_mnt_t *zm, int silent)
{
	const char *osname = zm->mnt_osname;
	struct inode *root_inode;
	uint64_t recordsize;
	int error = 0;
	zfsvfs_t *zfsvfs;

	ASSERT(zm);
	ASSERT(osname);

	error = zfsvfs_create(osname, &zfsvfs);
	if (error)
		return (error);

	error = zfsvfs_parse_options(zm->mnt_data, &zfsvfs->z_vfs);
	if (error)
		goto out;

	if ((error = dsl_prop_get_integer(osname, "recordsize",
	    &recordsize, NULL)))
		goto out;

	zfsvfs->z_vfs->vfs_data = zfsvfs;
	zfsvfs->z_sb = sb;
	sb->s_fs_info = zfsvfs;
	sb->s_magic = ZFS_SUPER_MAGIC;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_blocksize = recordsize;
	sb->s_blocksize_bits = ilog2(recordsize);

	error = -zpl_bdi_setup(sb, "zfs");
	if (error)
		goto out;

	sb->s_bdi->ra_pages = 0;

	/* Set callback operations for the file system. */
	sb->s_op = &zpl_super_operations;
	sb->s_xattr = zpl_xattr_handlers;
	sb->s_export_op = &zpl_export_operations;
#ifdef HAVE_S_D_OP
	sb->s_d_op = &zpl_dentry_operations;
#endif /* HAVE_S_D_OP */

	/* Set features for file system. */
	zfs_set_fuid_feature(zfsvfs);

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		uint64_t pval;

		atime_changed_cb(zfsvfs, B_FALSE);
		readonly_changed_cb(zfsvfs, B_TRUE);
		if ((error = dsl_prop_get_integer(osname,
		    "xattr", &pval, NULL)))
			goto out;
		xattr_changed_cb(zfsvfs, pval);
		if ((error = dsl_prop_get_integer(osname,
		    "acltype", &pval, NULL)))
			goto out;
		acltype_changed_cb(zfsvfs, pval);
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;
		zfsvfs->z_snap_defer_time = jiffies;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);
	} else {
		if ((error = zfsvfs_setup(zfsvfs, B_TRUE)))
			goto out;
	}

	/* Allocate a root inode for the filesystem. */
	error = zfs_root(zfsvfs, &root_inode);
	if (error) {
		(void) zfs_umount(sb);
		goto out;
	}

	/* Allocate a root dentry for the filesystem */
	sb->s_root = d_make_root(root_inode);
	if (sb->s_root == NULL) {
		(void) zfs_umount(sb);
		error = SET_ERROR(ENOMEM);
		goto out;
	}

	if (!zfsvfs->z_issnap)
		zfsctl_create(zfsvfs);

	zfsvfs->z_arc_prune = arc_add_prune_callback(zpl_prune_sb, sb);
out:
	if (error) {
		dmu_objset_disown(zfsvfs->z_os, zfsvfs);
		zfsvfs_free(zfsvfs);
		/*
		 * make sure we don't have dangling sb->s_fs_info which
		 * zfs_preumount will use.
		 */
		sb->s_fs_info = NULL;
	}

	return (error);
}

/*
 * Called when an unmount is requested and certain sanity checks have
 * already passed.  At this point no dentries or inodes have been reclaimed
 * from their respective caches.  We drop the extra reference on the .zfs
 * control directory to allow everything to be reclaimed.  All snapshots
 * must already have been unmounted to reach this point.
 */
void
zfs_preumount(struct super_block *sb)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;

	/* zfsvfs is NULL when zfs_domount fails during mount */
	if (zfsvfs) {
		zfsctl_destroy(sb->s_fs_info);
		/*
		 * Wait for iput_async before entering evict_inodes in
		 * generic_shutdown_super. The reason we must finish before
		 * evict_inodes is when lazytime is on, or when zfs_purgedir
		 * calls zfs_zget, iput would bump i_count from 0 to 1. This
		 * would race with the i_count check in evict_inodes. This means
		 * it could destroy the inode while we are still using it.
		 *
		 * We wait for two passes. xattr directories in the first pass
		 * may add xattr entries in zfs_purgedir, so in the second pass
		 * we wait for them. We don't use taskq_wait here because it is
		 * a pool wide taskq. Other mounted filesystems can constantly
		 * do iput_async and there's no guarantee when taskq will be
		 * empty.
		 */
		taskq_wait_outstanding(dsl_pool_iput_taskq(
		    dmu_objset_pool(zfsvfs->z_os)), 0);
		taskq_wait_outstanding(dsl_pool_iput_taskq(
		    dmu_objset_pool(zfsvfs->z_os)), 0);
	}
}

/*
 * Called once all other unmount released tear down has occurred.
 * It is our responsibility to release any remaining infrastructure.
 */
/*ARGSUSED*/
int
zfs_umount(struct super_block *sb)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;
	objset_t *os;

	arc_remove_prune_callback(zfsvfs->z_arc_prune);
	VERIFY(zfsvfs_teardown(zfsvfs, B_TRUE) == 0);
	os = zfsvfs->z_os;
	zpl_bdi_destroy(sb);

	/*
	 * z_os will be NULL if there was an error in
	 * attempting to reopen zfsvfs.
	 */
	if (os != NULL) {
		/*
		 * Unset the objset user_ptr.
		 */
		mutex_enter(&os->os_user_ptr_lock);
		dmu_objset_set_user(os, NULL);
		mutex_exit(&os->os_user_ptr_lock);

		/*
		 * Finally release the objset
		 */
		dmu_objset_disown(os, zfsvfs);
	}

	zfsvfs_free(zfsvfs);
	return (0);
}

int
zfs_remount(struct super_block *sb, int *flags, zfs_mnt_t *zm)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;
	vfs_t *vfsp;
	boolean_t issnap = dmu_objset_is_snapshot(zfsvfs->z_os);
	int error;

	if ((issnap || !spa_writeable(dmu_objset_spa(zfsvfs->z_os))) &&
	    !(*flags & MS_RDONLY)) {
		*flags |= MS_RDONLY;
		return (EROFS);
	}

	error = zfsvfs_parse_options(zm->mnt_data, &vfsp);
	if (error)
		return (error);

	zfs_unregister_callbacks(zfsvfs);
	zfsvfs_vfs_free(zfsvfs->z_vfs);

	vfsp->vfs_data = zfsvfs;
	zfsvfs->z_vfs = vfsp;
	if (!issnap)
		(void) zfs_register_callbacks(vfsp);

	return (error);
}

int
zfs_vget(struct super_block *sb, struct inode **ipp, fid_t *fidp)
{
	zfsvfs_t	*zfsvfs = sb->s_fs_info;
	znode_t		*zp;
	uint64_t	object = 0;
	uint64_t	fid_gen = 0;
	uint64_t	gen_mask;
	uint64_t	zp_gen;
	int		i, err;

	*ipp = NULL;

	if (fidp->fid_len == SHORT_FID_LEN || fidp->fid_len == LONG_FID_LEN) {
		zfid_short_t	*zfid = (zfid_short_t *)fidp;

		for (i = 0; i < sizeof (zfid->zf_object); i++)
			object |= ((uint64_t)zfid->zf_object[i]) << (8 * i);

		for (i = 0; i < sizeof (zfid->zf_gen); i++)
			fid_gen |= ((uint64_t)zfid->zf_gen[i]) << (8 * i);
	} else {
		return (SET_ERROR(EINVAL));
	}

	/* LONG_FID_LEN means snapdirs */
	if (fidp->fid_len == LONG_FID_LEN) {
		zfid_long_t	*zlfid = (zfid_long_t *)fidp;
		uint64_t	objsetid = 0;
		uint64_t	setgen = 0;

		for (i = 0; i < sizeof (zlfid->zf_setid); i++)
			objsetid |= ((uint64_t)zlfid->zf_setid[i]) << (8 * i);

		for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
			setgen |= ((uint64_t)zlfid->zf_setgen[i]) << (8 * i);

		if (objsetid != ZFSCTL_INO_SNAPDIRS - object) {
			dprintf("snapdir fid: objsetid (%llu) != "
			    "ZFSCTL_INO_SNAPDIRS (%llu) - object (%llu)\n",
			    objsetid, ZFSCTL_INO_SNAPDIRS, object);

			return (SET_ERROR(EINVAL));
		}

		if (fid_gen > 1 || setgen != 0) {
			dprintf("snapdir fid: fid_gen (%llu) and setgen "
			    "(%llu)\n", fid_gen, setgen);
			return (SET_ERROR(EINVAL));
		}

		return (zfsctl_snapdir_vget(sb, objsetid, fid_gen, ipp));
	}

	ZFS_ENTER(zfsvfs);
	/* A zero fid_gen means we are in the .zfs control directories */
	if (fid_gen == 0 &&
	    (object == ZFSCTL_INO_ROOT || object == ZFSCTL_INO_SNAPDIR)) {
		*ipp = zfsvfs->z_ctldir;
		ASSERT(*ipp != NULL);
		if (object == ZFSCTL_INO_SNAPDIR) {
			VERIFY(zfsctl_root_lookup(*ipp, "snapshot", ipp,
			    0, kcred, NULL, NULL) == 0);
		} else {
			igrab(*ipp);
		}
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	gen_mask = -1ULL >> (64 - 8 * i);

	dprintf("getting %llu [%llu mask %llx]\n", object, fid_gen, gen_mask);
	if ((err = zfs_zget(zfsvfs, object, &zp))) {
		ZFS_EXIT(zfsvfs);
		return (err);
	}

	/* Don't export xattr stuff */
	if (zp->z_pflags & ZFS_XATTR) {
		iput(ZTOI(zp));
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(ENOENT));
	}

	(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs), &zp_gen,
	    sizeof (uint64_t));
	zp_gen = zp_gen & gen_mask;
	if (zp_gen == 0)
		zp_gen = 1;
	if ((fid_gen == 0) && (zfsvfs->z_root == object))
		fid_gen = zp_gen;
	if (zp->z_unlinked || zp_gen != fid_gen) {
		dprintf("znode gen (%llu) != fid gen (%llu)\n", zp_gen,
		    fid_gen);
		iput(ZTOI(zp));
		ZFS_EXIT(zfsvfs);
		return (SET_ERROR(ENOENT));
	}

	*ipp = ZTOI(zp);
	if (*ipp)
		zfs_inode_update(ITOZ(*ipp));

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Block out VFS ops and close zfsvfs_t
 *
 * Note, if successful, then we return with the 'z_teardown_lock' and
 * 'z_teardown_inactive_lock' write held.  We leave ownership of the underlying
 * dataset and objset intact so that they can be atomically handed off during
 * a subsequent rollback or recv operation and the resume thereafter.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	int error;

	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0)
		return (error);

	return (0);
}

/*
 * Rebuild SA and release VOPs.  Note that ownership of the underlying dataset
 * is an invariant across any of the operations that can be performed while the
 * filesystem was suspended.  Whether it succeeded or failed, the preconditions
 * are the same: the relevant objset and associated dataset are owned by
 * zfsvfs, held, and long held on entry.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	int err, err2;
	znode_t *zp;

	ASSERT(RRM_WRITE_HELD(&zfsvfs->z_teardown_lock));
	ASSERT(RW_WRITE_HELD(&zfsvfs->z_teardown_inactive_lock));

	/*
	 * We already own this, so just update the objset_t, as the one we
	 * had before may have been evicted.
	 */
	objset_t *os;
	VERIFY3P(ds->ds_owner, ==, zfsvfs);
	VERIFY(dsl_dataset_long_held(ds));
	VERIFY0(dmu_objset_from_ds(ds, &os));

	err = zfsvfs_init(zfsvfs, os);
	if (err != 0)
		goto bail;

	VERIFY(zfsvfs_setup(zfsvfs, B_FALSE) == 0);

	zfs_set_fuid_feature(zfsvfs);
	zfsvfs->z_rollback_time = jiffies;

	/*
	 * Attempt to re-establish all the active inodes with their
	 * dbufs.  If a zfs_rezget() fails, then we unhash the inode
	 * and mark it stale.  This prevents a collision if a new
	 * inode/object is created which must use the same inode
	 * number.  The stale inode will be be released when the
	 * VFS prunes the dentry holding the remaining references
	 * on the stale inode.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp;
	    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
		err2 = zfs_rezget(zp);
		if (err2) {
			remove_inode_hash(ZTOI(zp));
			zp->z_is_stale = B_TRUE;
		}
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

bail:
	/* release the VFS ops */
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
	rrm_exit(&zfsvfs->z_teardown_lock, FTAG);

	if (err) {
		/*
		 * Since we couldn't setup the sa framework, try to force
		 * unmount this file system.
		 */
		if (zfsvfs->z_os)
			(void) zfs_umount(zfsvfs->z_sb);
	}
	return (err);
}

int
zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;

	if (newvers < ZPL_VERSION_INITIAL || newvers > ZPL_VERSION)
		return (SET_ERROR(EINVAL));

	if (newvers < zfsvfs->z_version)
		return (SET_ERROR(EINVAL));

	if (zfs_spa_version_map(newvers) >
	    spa_version(dmu_objset_spa(zfsvfs->z_os)))
		return (SET_ERROR(ENOTSUP));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, ZPL_VERSION_STR);
	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    ZFS_SA_ATTRS);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	}
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
	    8, 1, &newvers, tx);

	if (error) {
		dmu_tx_commit(tx);
		return (error);
	}

	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		uint64_t sa_obj;

		ASSERT3U(spa_version(dmu_objset_spa(zfsvfs->z_os)), >=,
		    SPA_VERSION_SA);
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);

		error = zap_add(os, MASTER_NODE_OBJ,
		    ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		ASSERT0(error);

		VERIFY(0 == sa_set_sa_object(os, sa_obj));
		sa_register_update_callback(os, zfs_sa_upgrade);
	}

	spa_history_log_internal_ds(dmu_objset_ds(os), "upgrade", tx,
	    "from %llu to %llu", zfsvfs->z_version, newvers);

	dmu_tx_commit(tx);

	zfsvfs->z_version = newvers;

	zfs_set_fuid_feature(zfsvfs);

	return (0);
}

/*
 * Read a property stored within the master node.
 */
int
zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value)
{
	const char *pname;
	int error = SET_ERROR(ENOENT);

	/*
	 * Look up the file system's value for the property.  For the
	 * version property, we look up a slightly different string.
	 */
	if (prop == ZFS_PROP_VERSION)
		pname = ZPL_VERSION_STR;
	else
		pname = zfs_prop_to_name(prop);

	if (os != NULL) {
		ASSERT3U(os->os_phys->os_type, ==, DMU_OST_ZFS);
		error = zap_lookup(os, MASTER_NODE_OBJ, pname, 8, 1, value);
	}

	if (error == ENOENT) {
		/* No value set, use the default value */
		switch (prop) {
		case ZFS_PROP_VERSION:
			*value = ZPL_VERSION;
			break;
		case ZFS_PROP_NORMALIZE:
		case ZFS_PROP_UTF8ONLY:
			*value = 0;
			break;
		case ZFS_PROP_CASE:
			*value = ZFS_CASE_SENSITIVE;
			break;
		case ZFS_PROP_ACLTYPE:
			*value = ZFS_ACLTYPE_OFF;
			break;
		default:
			return (error);
		}
		error = 0;
	}
	return (error);
}

/*
 * Return true if the coresponding vfs's unmounted flag is set.
 * Otherwise return false.
 * If this function returns true we know VFS unmount has been initiated.
 */
boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{
	zfsvfs_t *zfvp;
	boolean_t unmounted = B_FALSE;

	ASSERT(dmu_objset_type(os) == DMU_OST_ZFS);

	mutex_enter(&os->os_user_ptr_lock);
	zfvp = dmu_objset_get_user(os);
	if (zfvp != NULL && zfvp->z_unmounted)
		unmounted = B_TRUE;
	mutex_exit(&os->os_user_ptr_lock);

	return (unmounted);
}

void
zfs_init(void)
{
	zfsctl_init();
	zfs_znode_init();
	dmu_objset_register_type(DMU_OST_ZFS, zfs_space_delta_cb);
	register_filesystem(&zpl_fs_type);
}

void
zfs_fini(void)
{
	/*
	 * we don't use outstanding because zpl_posix_acl_free might add more.
	 */
	taskq_wait(system_delay_taskq);
	taskq_wait(system_taskq);
	unregister_filesystem(&zpl_fs_type);
	zfs_znode_fini();
	zfsctl_fini();
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zfs_suspend_fs);
EXPORT_SYMBOL(zfs_resume_fs);
EXPORT_SYMBOL(zfs_userspace_one);
EXPORT_SYMBOL(zfs_userspace_many);
EXPORT_SYMBOL(zfs_set_userquota);
EXPORT_SYMBOL(zfs_owner_overquota);
EXPORT_SYMBOL(zfs_fuid_overquota);
EXPORT_SYMBOL(zfs_fuid_overobjquota);
EXPORT_SYMBOL(zfs_set_version);
EXPORT_SYMBOL(zfsvfs_create);
EXPORT_SYMBOL(zfsvfs_free);
EXPORT_SYMBOL(zfs_is_readonly);
EXPORT_SYMBOL(zfs_domount);
EXPORT_SYMBOL(zfs_preumount);
EXPORT_SYMBOL(zfs_umount);
EXPORT_SYMBOL(zfs_remount);
EXPORT_SYMBOL(zfs_statvfs);
EXPORT_SYMBOL(zfs_vget);
EXPORT_SYMBOL(zfs_prune);
#endif
