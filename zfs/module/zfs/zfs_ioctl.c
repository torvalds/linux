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
 * Portions Copyright 2011 Martin Matuska
 * Portions Copyright 2012 Pawel Jakub Dawidek <pawel@dawidek.net>
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014, Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

/*
 * ZFS ioctls.
 *
 * This file handles the ioctls to /dev/zfs, used for configuring ZFS storage
 * pools and filesystems, e.g. with /sbin/zfs and /sbin/zpool.
 *
 * There are two ways that we handle ioctls: the legacy way where almost
 * all of the logic is in the ioctl callback, and the new way where most
 * of the marshalling is handled in the common entry point, zfsdev_ioctl().
 *
 * Non-legacy ioctls should be registered by calling
 * zfs_ioctl_register() from zfs_ioctl_init().  The ioctl is invoked
 * from userland by lzc_ioctl().
 *
 * The registration arguments are as follows:
 *
 * const char *name
 *   The name of the ioctl.  This is used for history logging.  If the
 *   ioctl returns successfully (the callback returns 0), and allow_log
 *   is true, then a history log entry will be recorded with the input &
 *   output nvlists.  The log entry can be printed with "zpool history -i".
 *
 * zfs_ioc_t ioc
 *   The ioctl request number, which userland will pass to ioctl(2).
 *   The ioctl numbers can change from release to release, because
 *   the caller (libzfs) must be matched to the kernel.
 *
 * zfs_secpolicy_func_t *secpolicy
 *   This function will be called before the zfs_ioc_func_t, to
 *   determine if this operation is permitted.  It should return EPERM
 *   on failure, and 0 on success.  Checks include determining if the
 *   dataset is visible in this zone, and if the user has either all
 *   zfs privileges in the zone (SYS_MOUNT), or has been granted permission
 *   to do this operation on this dataset with "zfs allow".
 *
 * zfs_ioc_namecheck_t namecheck
 *   This specifies what to expect in the zfs_cmd_t:zc_name -- a pool
 *   name, a dataset name, or nothing.  If the name is not well-formed,
 *   the ioctl will fail and the callback will not be called.
 *   Therefore, the callback can assume that the name is well-formed
 *   (e.g. is null-terminated, doesn't have more than one '@' character,
 *   doesn't have invalid characters).
 *
 * zfs_ioc_poolcheck_t pool_check
 *   This specifies requirements on the pool state.  If the pool does
 *   not meet them (is suspended or is readonly), the ioctl will fail
 *   and the callback will not be called.  If any checks are specified
 *   (i.e. it is not POOL_CHECK_NONE), namecheck must not be NO_NAME.
 *   Multiple checks can be or-ed together (e.g. POOL_CHECK_SUSPENDED |
 *   POOL_CHECK_READONLY).
 *
 * boolean_t smush_outnvlist
 *   If smush_outnvlist is true, then the output is presumed to be a
 *   list of errors, and it will be "smushed" down to fit into the
 *   caller's buffer, by removing some entries and replacing them with a
 *   single "N_MORE_ERRORS" entry indicating how many were removed.  See
 *   nvlist_smush() for details.  If smush_outnvlist is false, and the
 *   outnvlist does not fit into the userland-provided buffer, then the
 *   ioctl will fail with ENOMEM.
 *
 * zfs_ioc_func_t *func
 *   The callback function that will perform the operation.
 *
 *   The callback should return 0 on success, or an error number on
 *   failure.  If the function fails, the userland ioctl will return -1,
 *   and errno will be set to the callback's return value.  The callback
 *   will be called with the following arguments:
 *
 *   const char *name
 *     The name of the pool or dataset to operate on, from
 *     zfs_cmd_t:zc_name.  The 'namecheck' argument specifies the
 *     expected type (pool, dataset, or none).
 *
 *   nvlist_t *innvl
 *     The input nvlist, deserialized from zfs_cmd_t:zc_nvlist_src.  Or
 *     NULL if no input nvlist was provided.  Changes to this nvlist are
 *     ignored.  If the input nvlist could not be deserialized, the
 *     ioctl will fail and the callback will not be called.
 *
 *   nvlist_t *outnvl
 *     The output nvlist, initially empty.  The callback can fill it in,
 *     and it will be returned to userland by serializing it into
 *     zfs_cmd_t:zc_nvlist_dst.  If it is non-empty, and serialization
 *     fails (e.g. because the caller didn't supply a large enough
 *     buffer), then the overall ioctl will fail.  See the
 *     'smush_nvlist' argument above for additional behaviors.
 *
 *     There are two typical uses of the output nvlist:
 *       - To return state, e.g. property values.  In this case,
 *         smush_outnvlist should be false.  If the buffer was not large
 *         enough, the caller will reallocate a larger buffer and try
 *         the ioctl again.
 *
 *       - To return multiple errors from an ioctl which makes on-disk
 *         changes.  In this case, smush_outnvlist should be true.
 *         Ioctls which make on-disk modifications should generally not
 *         use the outnvl if they succeed, because the caller can not
 *         distinguish between the operation failing, and
 *         deserialization failing.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/priv_impl.h>
#include <sys/dmu.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunldi.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/nvpair.h>
#include <sys/pathname.h>
#include <sys/mount.h>
#include <sys/sdt.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/dsl_scan.h>
#include <sharefs/share.h>
#include <sys/fm/util.h>

#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_userhold.h>
#include <sys/zfeature.h>

#include <linux/miscdevice.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_deleg.h"
#include "zfs_comutil.h"

kmutex_t zfsdev_state_lock;
zfsdev_state_t *zfsdev_state_list;

extern void zfs_init(void);
extern void zfs_fini(void);

uint_t zfs_fsyncer_key;
extern uint_t rrw_tsd_key;
static uint_t zfs_allow_log_key;

typedef int zfs_ioc_legacy_func_t(zfs_cmd_t *);
typedef int zfs_ioc_func_t(const char *, nvlist_t *, nvlist_t *);
typedef int zfs_secpolicy_func_t(zfs_cmd_t *, nvlist_t *, cred_t *);

typedef enum {
	NO_NAME,
	POOL_NAME,
	DATASET_NAME
} zfs_ioc_namecheck_t;

typedef enum {
	POOL_CHECK_NONE		= 1 << 0,
	POOL_CHECK_SUSPENDED	= 1 << 1,
	POOL_CHECK_READONLY	= 1 << 2,
} zfs_ioc_poolcheck_t;

typedef struct zfs_ioc_vec {
	zfs_ioc_legacy_func_t	*zvec_legacy_func;
	zfs_ioc_func_t		*zvec_func;
	zfs_secpolicy_func_t	*zvec_secpolicy;
	zfs_ioc_namecheck_t	zvec_namecheck;
	boolean_t		zvec_allow_log;
	zfs_ioc_poolcheck_t	zvec_pool_check;
	boolean_t		zvec_smush_outnvlist;
	const char		*zvec_name;
} zfs_ioc_vec_t;

/* This array is indexed by zfs_userquota_prop_t */
static const char *userquota_perms[] = {
	ZFS_DELEG_PERM_USERUSED,
	ZFS_DELEG_PERM_USERQUOTA,
	ZFS_DELEG_PERM_GROUPUSED,
	ZFS_DELEG_PERM_GROUPQUOTA,
};

static int zfs_ioc_userspace_upgrade(zfs_cmd_t *zc);
static int zfs_check_settable(const char *name, nvpair_t *property,
    cred_t *cr);
static int zfs_check_clearable(char *dataset, nvlist_t *props,
    nvlist_t **errors);
static int zfs_fill_zplprops_root(uint64_t, nvlist_t *, nvlist_t *,
    boolean_t *);
int zfs_set_prop_nvlist(const char *, zprop_source_t, nvlist_t *, nvlist_t *);
static int get_nvlist(uint64_t nvl, uint64_t size, int iflag, nvlist_t **nvp);

static void
history_str_free(char *buf)
{
	kmem_free(buf, HIS_MAX_RECORD_LEN);
}

static char *
history_str_get(zfs_cmd_t *zc)
{
	char *buf;

	if (zc->zc_history == 0)
		return (NULL);

	buf = kmem_alloc(HIS_MAX_RECORD_LEN, KM_SLEEP);
	if (copyinstr((void *)(uintptr_t)zc->zc_history,
	    buf, HIS_MAX_RECORD_LEN, NULL) != 0) {
		history_str_free(buf);
		return (NULL);
	}

	buf[HIS_MAX_RECORD_LEN -1] = '\0';

	return (buf);
}

/*
 * Check to see if the named dataset is currently defined as bootable
 */
static boolean_t
zfs_is_bootfs(const char *name)
{
	objset_t *os;

	if (dmu_objset_hold(name, FTAG, &os) == 0) {
		boolean_t ret;
		ret = (dmu_objset_id(os) == spa_bootfs(dmu_objset_spa(os)));
		dmu_objset_rele(os, FTAG);
		return (ret);
	}
	return (B_FALSE);
}

/*
 * Return non-zero if the spa version is less than requested version.
 */
static int
zfs_earlier_version(const char *name, int version)
{
	spa_t *spa;

	if (spa_open(name, &spa, FTAG) == 0) {
		if (spa_version(spa) < version) {
			spa_close(spa, FTAG);
			return (1);
		}
		spa_close(spa, FTAG);
	}
	return (0);
}

/*
 * Return TRUE if the ZPL version is less than requested version.
 */
static boolean_t
zpl_earlier_version(const char *name, int version)
{
	objset_t *os;
	boolean_t rc = B_TRUE;

	if (dmu_objset_hold(name, FTAG, &os) == 0) {
		uint64_t zplversion;

		if (dmu_objset_type(os) != DMU_OST_ZFS) {
			dmu_objset_rele(os, FTAG);
			return (B_TRUE);
		}
		/* XXX reading from non-owned objset */
		if (zfs_get_zplprop(os, ZFS_PROP_VERSION, &zplversion) == 0)
			rc = zplversion < version;
		dmu_objset_rele(os, FTAG);
	}
	return (rc);
}

static void
zfs_log_history(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *buf;

	if ((buf = history_str_get(zc)) == NULL)
		return;

	if (spa_open(zc->zc_name, &spa, FTAG) == 0) {
		if (spa_version(spa) >= SPA_VERSION_ZPOOL_HISTORY)
			(void) spa_history_log(spa, buf);
		spa_close(spa, FTAG);
	}
	history_str_free(buf);
}

/*
 * Policy for top-level read operations (list pools).  Requires no privileges,
 * and can be used in the local zone, as there is no associated dataset.
 */
/* ARGSUSED */
static int
zfs_secpolicy_none(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (0);
}

/*
 * Policy for dataset read operations (list children, get statistics).  Requires
 * no privileges, but must be visible in the local zone.
 */
/* ARGSUSED */
static int
zfs_secpolicy_read(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	if (INGLOBALZONE(curproc) ||
	    zone_dataset_visible(zc->zc_name, NULL))
		return (0);

	return (SET_ERROR(ENOENT));
}

static int
zfs_dozonecheck_impl(const char *dataset, uint64_t zoned, cred_t *cr)
{
	int writable = 1;

	/*
	 * The dataset must be visible by this zone -- check this first
	 * so they don't see EPERM on something they shouldn't know about.
	 */
	if (!INGLOBALZONE(curproc) &&
	    !zone_dataset_visible(dataset, &writable))
		return (SET_ERROR(ENOENT));

	if (INGLOBALZONE(curproc)) {
		/*
		 * If the fs is zoned, only root can access it from the
		 * global zone.
		 */
		if (secpolicy_zfs(cr) && zoned)
			return (SET_ERROR(EPERM));
	} else {
		/*
		 * If we are in a local zone, the 'zoned' property must be set.
		 */
		if (!zoned)
			return (SET_ERROR(EPERM));

		/* must be writable by this zone */
		if (!writable)
			return (SET_ERROR(EPERM));
	}
	return (0);
}

static int
zfs_dozonecheck(const char *dataset, cred_t *cr)
{
	uint64_t zoned;

	if (dsl_prop_get_integer(dataset, "zoned", &zoned, NULL))
		return (SET_ERROR(ENOENT));

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

static int
zfs_dozonecheck_ds(const char *dataset, dsl_dataset_t *ds, cred_t *cr)
{
	uint64_t zoned;

	if (dsl_prop_get_int_ds(ds, "zoned", &zoned))
		return (SET_ERROR(ENOENT));

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

static int
zfs_secpolicy_write_perms_ds(const char *name, dsl_dataset_t *ds,
    const char *perm, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck_ds(name, ds, cr);
	if (error == 0) {
		error = secpolicy_zfs(cr);
		if (error != 0)
			error = dsl_deleg_access_impl(ds, perm, cr);
	}
	return (error);
}

static int
zfs_secpolicy_write_perms(const char *name, const char *perm, cred_t *cr)
{
	int error;
	dsl_dataset_t *ds;
	dsl_pool_t *dp;

	error = dsl_pool_hold(name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, name, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = zfs_secpolicy_write_perms_ds(name, ds, perm, cr);

	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (error);
}

/*
 * Policy for setting the security label property.
 *
 * Returns 0 for success, non-zero for access and other errors.
 */
static int
zfs_set_slabel_policy(const char *name, char *strval, cred_t *cr)
{
#ifdef HAVE_MLSLABEL
	char		ds_hexsl[MAXNAMELEN];
	bslabel_t	ds_sl, new_sl;
	boolean_t	new_default = FALSE;
	uint64_t	zoned;
	int		needed_priv = -1;
	int		error;

	/* First get the existing dataset label. */
	error = dsl_prop_get(name, zfs_prop_to_name(ZFS_PROP_MLSLABEL),
	    1, sizeof (ds_hexsl), &ds_hexsl, NULL);
	if (error != 0)
		return (SET_ERROR(EPERM));

	if (strcasecmp(strval, ZFS_MLSLABEL_DEFAULT) == 0)
		new_default = TRUE;

	/* The label must be translatable */
	if (!new_default && (hexstr_to_label(strval, &new_sl) != 0))
		return (SET_ERROR(EINVAL));

	/*
	 * In a non-global zone, disallow attempts to set a label that
	 * doesn't match that of the zone; otherwise no other checks
	 * are needed.
	 */
	if (!INGLOBALZONE(curproc)) {
		if (new_default || !blequal(&new_sl, CR_SL(CRED())))
			return (SET_ERROR(EPERM));
		return (0);
	}

	/*
	 * For global-zone datasets (i.e., those whose zoned property is
	 * "off", verify that the specified new label is valid for the
	 * global zone.
	 */
	if (dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL))
		return (SET_ERROR(EPERM));
	if (!zoned) {
		if (zfs_check_global_label(name, strval) != 0)
			return (SET_ERROR(EPERM));
	}

	/*
	 * If the existing dataset label is nondefault, check if the
	 * dataset is mounted (label cannot be changed while mounted).
	 * Get the zfs_sb_t; if there isn't one, then the dataset isn't
	 * mounted (or isn't a dataset, doesn't exist, ...).
	 */
	if (strcasecmp(ds_hexsl, ZFS_MLSLABEL_DEFAULT) != 0) {
		objset_t *os;
		static char *setsl_tag = "setsl_tag";

		/*
		 * Try to own the dataset; abort if there is any error,
		 * (e.g., already mounted, in use, or other error).
		 */
		error = dmu_objset_own(name, DMU_OST_ZFS, B_TRUE,
		    setsl_tag, &os);
		if (error != 0)
			return (SET_ERROR(EPERM));

		dmu_objset_disown(os, setsl_tag);

		if (new_default) {
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
			goto out_check;
		}

		if (hexstr_to_label(strval, &new_sl) != 0)
			return (SET_ERROR(EPERM));

		if (blstrictdom(&ds_sl, &new_sl))
			needed_priv = PRIV_FILE_DOWNGRADE_SL;
		else if (blstrictdom(&new_sl, &ds_sl))
			needed_priv = PRIV_FILE_UPGRADE_SL;
	} else {
		/* dataset currently has a default label */
		if (!new_default)
			needed_priv = PRIV_FILE_UPGRADE_SL;
	}

out_check:
	if (needed_priv != -1)
		return (PRIV_POLICY(cr, needed_priv, B_FALSE, EPERM, NULL));
	return (0);
#else
	return (ENOTSUP);
#endif /* HAVE_MLSLABEL */
}

static int
zfs_secpolicy_setprop(const char *dsname, zfs_prop_t prop, nvpair_t *propval,
    cred_t *cr)
{
	char *strval;

	/*
	 * Check permissions for special properties.
	 */
	switch (prop) {
	default:
		break;
	case ZFS_PROP_ZONED:
		/*
		 * Disallow setting of 'zoned' from within a local zone.
		 */
		if (!INGLOBALZONE(curproc))
			return (SET_ERROR(EPERM));
		break;

	case ZFS_PROP_QUOTA:
	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
		if (!INGLOBALZONE(curproc)) {
			uint64_t zoned;
			char setpoint[MAXNAMELEN];
			/*
			 * Unprivileged users are allowed to modify the
			 * limit on things *under* (ie. contained by)
			 * the thing they own.
			 */
			if (dsl_prop_get_integer(dsname, "zoned", &zoned,
			    setpoint))
				return (SET_ERROR(EPERM));
			if (!zoned || strlen(dsname) <= strlen(setpoint))
				return (SET_ERROR(EPERM));
		}
		break;

	case ZFS_PROP_MLSLABEL:
		if (!is_system_labeled())
			return (SET_ERROR(EPERM));

		if (nvpair_value_string(propval, &strval) == 0) {
			int err;

			err = zfs_set_slabel_policy(dsname, strval, CRED());
			if (err != 0)
				return (err);
		}
		break;
	}

	return (zfs_secpolicy_write_perms(dsname, zfs_prop_to_name(prop), cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_set_fsacl(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int error;

	error = zfs_dozonecheck(zc->zc_name, cr);
	if (error != 0)
		return (error);

	/*
	 * permission to set permissions will be evaluated later in
	 * dsl_deleg_can_allow()
	 */
	return (0);
}

/* ARGSUSED */
static int
zfs_secpolicy_rollback(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_ROLLBACK, cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_send(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	char *cp;
	int error;

	/*
	 * Generate the current snapshot name from the given objsetid, then
	 * use that name for the secpolicy/zone checks.
	 */
	cp = strchr(zc->zc_name, '@');
	if (cp == NULL)
		return (SET_ERROR(EINVAL));
	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	dsl_dataset_name(ds, zc->zc_name);

	error = zfs_secpolicy_write_perms_ds(zc->zc_name, ds,
	    ZFS_DELEG_PERM_SEND, cr);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);

	return (error);
}

/* ARGSUSED */
static int
zfs_secpolicy_send_new(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_SEND, cr));
}

#ifdef HAVE_SMB_SHARE
/* ARGSUSED */
static int
zfs_secpolicy_deleg_share(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	vnode_t *vp;
	int error;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (vp->v_vfsp->vfs_fstype != zfsfstype ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (SET_ERROR(EPERM));
	}

	VN_RELE(vp);
	return (dsl_deleg_access(zc->zc_name,
	    ZFS_DELEG_PERM_SHARE, cr));
}
#endif /* HAVE_SMB_SHARE */

int
zfs_secpolicy_share(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
#ifdef HAVE_SMB_SHARE
	if (!INGLOBALZONE(curproc))
		return (SET_ERROR(EPERM));

	if (secpolicy_nfs(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, innvl, cr));
	}
#else
	return (SET_ERROR(ENOTSUP));
#endif /* HAVE_SMB_SHARE */
}

int
zfs_secpolicy_smb_acl(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
#ifdef HAVE_SMB_SHARE
	if (!INGLOBALZONE(curproc))
		return (SET_ERROR(EPERM));

	if (secpolicy_smb(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, innvl, cr));
	}
#else
	return (SET_ERROR(ENOTSUP));
#endif /* HAVE_SMB_SHARE */
}

static int
zfs_get_parent(const char *datasetname, char *parent, int parentsize)
{
	char *cp;

	/*
	 * Remove the @bla or /bla from the end of the name to get the parent.
	 */
	(void) strncpy(parent, datasetname, parentsize);
	cp = strrchr(parent, '@');
	if (cp != NULL) {
		cp[0] = '\0';
	} else {
		cp = strrchr(parent, '/');
		if (cp == NULL)
			return (SET_ERROR(ENOENT));
		cp[0] = '\0';
	}

	return (0);
}

int
zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
	int error;

	if ((error = zfs_secpolicy_write_perms(name,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(name, ZFS_DELEG_PERM_DESTROY, cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_destroy(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_destroy_perms(zc->zc_name, cr));
}

/*
 * Destroying snapshots with delegated permissions requires
 * descendant mount and destroy permissions.
 */
/* ARGSUSED */
static int
zfs_secpolicy_destroy_snaps(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	nvlist_t *snaps;
	nvpair_t *pair, *nextpair;
	int error = 0;

	if (nvlist_lookup_nvlist(innvl, "snaps", &snaps) != 0)
		return (SET_ERROR(EINVAL));
	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nextpair) {
		nextpair = nvlist_next_nvpair(snaps, pair);
		error = zfs_secpolicy_destroy_perms(nvpair_name(pair), cr);
		if (error == ENOENT) {
			/*
			 * Ignore any snapshots that don't exist (we consider
			 * them "already destroyed").  Remove the name from the
			 * nvl here in case the snapshot is created between
			 * now and when we try to destroy it (in which case
			 * we don't want to destroy it since we haven't
			 * checked for permission).
			 */
			fnvlist_remove_nvpair(snaps, pair);
			error = 0;
		}
		if (error != 0)
			break;
	}

	return (error);
}

int
zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	int	error;

	if ((error = zfs_secpolicy_write_perms(from,
	    ZFS_DELEG_PERM_RENAME, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(from,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	if ((error = zfs_get_parent(to, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (error);
}

/* ARGSUSED */
static int
zfs_secpolicy_rename(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_rename_perms(zc->zc_name, zc->zc_value, cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_promote(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	dsl_pool_t *dp;
	dsl_dataset_t *clone;
	int error;

	error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_PROMOTE, cr);
	if (error != 0)
		return (error);

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &clone);

	if (error == 0) {
		char parentname[MAXNAMELEN];
		dsl_dataset_t *origin = NULL;
		dsl_dir_t *dd;
		dd = clone->ds_dir;

		error = dsl_dataset_hold_obj(dd->dd_pool,
		    dsl_dir_phys(dd)->dd_origin_obj, FTAG, &origin);
		if (error != 0) {
			dsl_dataset_rele(clone, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		error = zfs_secpolicy_write_perms_ds(zc->zc_name, clone,
		    ZFS_DELEG_PERM_MOUNT, cr);

		dsl_dataset_name(origin, parentname);
		if (error == 0) {
			error = zfs_secpolicy_write_perms_ds(parentname, origin,
			    ZFS_DELEG_PERM_PROMOTE, cr);
		}
		dsl_dataset_rele(clone, FTAG);
		dsl_dataset_rele(origin, FTAG);
	}
	dsl_pool_rele(dp, FTAG);
	return (error);
}

/* ARGSUSED */
static int
zfs_secpolicy_recv(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int error;

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_RECEIVE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_MOUNT, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_CREATE, cr));
}

int
zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(name,
	    ZFS_DELEG_PERM_SNAPSHOT, cr));
}

/*
 * Check for permission to create each snapshot in the nvlist.
 */
/* ARGSUSED */
static int
zfs_secpolicy_snapshot(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	nvlist_t *snaps;
	int error = 0;
	nvpair_t *pair;

	if (nvlist_lookup_nvlist(innvl, "snaps", &snaps) != 0)
		return (SET_ERROR(EINVAL));
	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		char *name = nvpair_name(pair);
		char *atp = strchr(name, '@');

		if (atp == NULL) {
			error = SET_ERROR(EINVAL);
			break;
		}
		*atp = '\0';
		error = zfs_secpolicy_snapshot_perms(name, cr);
		*atp = '@';
		if (error != 0)
			break;
	}
	return (error);
}

/*
 * Check for permission to create each snapshot in the nvlist.
 */
/* ARGSUSED */
static int
zfs_secpolicy_bookmark(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int error = 0;
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		char *name = nvpair_name(pair);
		char *hashp = strchr(name, '#');

		if (hashp == NULL) {
			error = SET_ERROR(EINVAL);
			break;
		}
		*hashp = '\0';
		error = zfs_secpolicy_write_perms(name,
		    ZFS_DELEG_PERM_BOOKMARK, cr);
		*hashp = '#';
		if (error != 0)
			break;
	}
	return (error);
}

/* ARGSUSED */
static int
zfs_secpolicy_destroy_bookmarks(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	nvpair_t *pair, *nextpair;
	int error = 0;

	for (pair = nvlist_next_nvpair(innvl, NULL); pair != NULL;
	    pair = nextpair) {
		char *name = nvpair_name(pair);
		char *hashp = strchr(name, '#');
		nextpair = nvlist_next_nvpair(innvl, pair);

		if (hashp == NULL) {
			error = SET_ERROR(EINVAL);
			break;
		}

		*hashp = '\0';
		error = zfs_secpolicy_write_perms(name,
		    ZFS_DELEG_PERM_DESTROY, cr);
		*hashp = '#';
		if (error == ENOENT) {
			/*
			 * Ignore any filesystems that don't exist (we consider
			 * their bookmarks "already destroyed").  Remove
			 * the name from the nvl here in case the filesystem
			 * is created between now and when we try to destroy
			 * the bookmark (in which case we don't want to
			 * destroy it since we haven't checked for permission).
			 */
			fnvlist_remove_nvpair(innvl, pair);
			error = 0;
		}
		if (error != 0)
			break;
	}

	return (error);
}

/* ARGSUSED */
static int
zfs_secpolicy_log_history(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	/*
	 * Even root must have a proper TSD so that we know what pool
	 * to log to.
	 */
	if (tsd_get(zfs_allow_log_key) == NULL)
		return (SET_ERROR(EPERM));
	return (0);
}

static int
zfs_secpolicy_create_clone(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	char	parentname[MAXNAMELEN];
	int	error;
	char	*origin;

	if ((error = zfs_get_parent(zc->zc_name, parentname,
	    sizeof (parentname))) != 0)
		return (error);

	if (nvlist_lookup_string(innvl, "origin", &origin) == 0 &&
	    (error = zfs_secpolicy_write_perms(origin,
	    ZFS_DELEG_PERM_CLONE, cr)) != 0)
		return (error);

	if ((error = zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_CREATE, cr)) != 0)
		return (error);

	return (zfs_secpolicy_write_perms(parentname,
	    ZFS_DELEG_PERM_MOUNT, cr));
}

/*
 * Policy for pool operations - create/destroy pools, add vdevs, etc.  Requires
 * SYS_CONFIG privilege, which is not available in a local zone.
 */
/* ARGSUSED */
static int
zfs_secpolicy_config(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	if (secpolicy_sys_config(cr, B_FALSE) != 0)
		return (SET_ERROR(EPERM));

	return (0);
}

/*
 * Policy for object to name lookups.
 */
/* ARGSUSED */
static int
zfs_secpolicy_diff(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int error;

	if ((error = secpolicy_sys_config(cr, B_FALSE)) == 0)
		return (0);

	error = zfs_secpolicy_write_perms(zc->zc_name, ZFS_DELEG_PERM_DIFF, cr);
	return (error);
}

/*
 * Policy for fault injection.  Requires all privileges.
 */
/* ARGSUSED */
static int
zfs_secpolicy_inject(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (secpolicy_zinject(cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_inherit_prop(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	zfs_prop_t prop = zfs_name_to_prop(zc->zc_value);

	if (prop == ZPROP_INVAL) {
		if (!zfs_prop_user(zc->zc_value))
			return (SET_ERROR(EINVAL));
		return (zfs_secpolicy_write_perms(zc->zc_name,
		    ZFS_DELEG_PERM_USERPROP, cr));
	} else {
		return (zfs_secpolicy_setprop(zc->zc_name, prop,
		    NULL, cr));
	}
}

static int
zfs_secpolicy_userspace_one(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, innvl, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (SET_ERROR(EINVAL));

	if (zc->zc_value[0] == 0) {
		/*
		 * They are asking about a posix uid/gid.  If it's
		 * themself, allow it.
		 */
		if (zc->zc_objset_type == ZFS_PROP_USERUSED ||
		    zc->zc_objset_type == ZFS_PROP_USERQUOTA) {
			if (zc->zc_guid == crgetuid(cr))
				return (0);
		} else {
			if (groupmember(zc->zc_guid, cr))
				return (0);
		}
	}

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

static int
zfs_secpolicy_userspace_many(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	int err = zfs_secpolicy_read(zc, innvl, cr);
	if (err)
		return (err);

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (SET_ERROR(EINVAL));

	return (zfs_secpolicy_write_perms(zc->zc_name,
	    userquota_perms[zc->zc_objset_type], cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_userspace_upgrade(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_setprop(zc->zc_name, ZFS_PROP_VERSION,
	    NULL, cr));
}

/* ARGSUSED */
static int
zfs_secpolicy_hold(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	nvpair_t *pair;
	nvlist_t *holds;
	int error;

	error = nvlist_lookup_nvlist(innvl, "holds", &holds);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		char fsname[MAXNAMELEN];
		error = dmu_fsname(nvpair_name(pair), fsname);
		if (error != 0)
			return (error);
		error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_HOLD, cr);
		if (error != 0)
			return (error);
	}
	return (0);
}

/* ARGSUSED */
static int
zfs_secpolicy_release(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	nvpair_t *pair;
	int error;

	for (pair = nvlist_next_nvpair(innvl, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(innvl, pair)) {
		char fsname[MAXNAMELEN];
		error = dmu_fsname(nvpair_name(pair), fsname);
		if (error != 0)
			return (error);
		error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_RELEASE, cr);
		if (error != 0)
			return (error);
	}
	return (0);
}

/*
 * Policy for allowing temporary snapshots to be taken or released
 */
static int
zfs_secpolicy_tmp_snapshot(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	/*
	 * A temporary snapshot is the same as a snapshot,
	 * hold, destroy and release all rolled into one.
	 * Delegated diff alone is sufficient that we allow this.
	 */
	int error;

	if ((error = zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_DIFF, cr)) == 0)
		return (0);

	error = zfs_secpolicy_snapshot_perms(zc->zc_name, cr);
	if (error == 0)
		error = zfs_secpolicy_hold(zc, innvl, cr);
	if (error == 0)
		error = zfs_secpolicy_release(zc, innvl, cr);
	if (error == 0)
		error = zfs_secpolicy_destroy(zc, innvl, cr);
	return (error);
}

/*
 * Returns the nvlist as specified by the user in the zfs_cmd_t.
 */
static int
get_nvlist(uint64_t nvl, uint64_t size, int iflag, nvlist_t **nvp)
{
	char *packed;
	int error;
	nvlist_t *list = NULL;

	/*
	 * Read in and unpack the user-supplied nvlist.
	 */
	if (size == 0)
		return (SET_ERROR(EINVAL));

	packed = vmem_alloc(size, KM_SLEEP);

	if ((error = ddi_copyin((void *)(uintptr_t)nvl, packed, size,
	    iflag)) != 0) {
		vmem_free(packed, size);
		return (SET_ERROR(EFAULT));
	}

	if ((error = nvlist_unpack(packed, size, &list, 0)) != 0) {
		vmem_free(packed, size);
		return (error);
	}

	vmem_free(packed, size);

	*nvp = list;
	return (0);
}

/*
 * Reduce the size of this nvlist until it can be serialized in 'max' bytes.
 * Entries will be removed from the end of the nvlist, and one int32 entry
 * named "N_MORE_ERRORS" will be added indicating how many entries were
 * removed.
 */
static int
nvlist_smush(nvlist_t *errors, size_t max)
{
	size_t size;

	size = fnvlist_size(errors);

	if (size > max) {
		nvpair_t *more_errors;
		int n = 0;

		if (max < 1024)
			return (SET_ERROR(ENOMEM));

		fnvlist_add_int32(errors, ZPROP_N_MORE_ERRORS, 0);
		more_errors = nvlist_prev_nvpair(errors, NULL);

		do {
			nvpair_t *pair = nvlist_prev_nvpair(errors,
			    more_errors);
			fnvlist_remove_nvpair(errors, pair);
			n++;
			size = fnvlist_size(errors);
		} while (size > max);

		fnvlist_remove_nvpair(errors, more_errors);
		fnvlist_add_int32(errors, ZPROP_N_MORE_ERRORS, n);
		ASSERT3U(fnvlist_size(errors), <=, max);
	}

	return (0);
}

static int
put_nvlist(zfs_cmd_t *zc, nvlist_t *nvl)
{
	char *packed = NULL;
	int error = 0;
	size_t size;

	size = fnvlist_size(nvl);

	if (size > zc->zc_nvlist_dst_size) {
		error = SET_ERROR(ENOMEM);
	} else {
		packed = fnvlist_pack(nvl, &size);
		if (ddi_copyout(packed, (void *)(uintptr_t)zc->zc_nvlist_dst,
		    size, zc->zc_iflags) != 0)
			error = SET_ERROR(EFAULT);
		fnvlist_pack_free(packed, size);
	}

	zc->zc_nvlist_dst_size = size;
	zc->zc_nvlist_dst_filled = B_TRUE;
	return (error);
}

static int
get_zfs_sb(const char *dsname, zfs_sb_t **zsbp)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (SET_ERROR(EINVAL));
	}

	mutex_enter(&os->os_user_ptr_lock);
	*zsbp = dmu_objset_get_user(os);
	/* bump s_active only when non-zero to prevent umount race */
	if (*zsbp == NULL || (*zsbp)->z_sb == NULL ||
	    !atomic_inc_not_zero(&((*zsbp)->z_sb->s_active))) {
		error = SET_ERROR(ESRCH);
	}
	mutex_exit(&os->os_user_ptr_lock);
	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * Find a zfs_sb_t for a mounted filesystem, or create our own, in which
 * case its z_sb will be NULL, and it will be opened as the owner.
 * If 'writer' is set, the z_teardown_lock will be held for RW_WRITER,
 * which prevents all inode ops from running.
 */
static int
zfs_sb_hold(const char *name, void *tag, zfs_sb_t **zsbp, boolean_t writer)
{
	int error = 0;

	if (get_zfs_sb(name, zsbp) != 0)
		error = zfs_sb_create(name, NULL, zsbp);
	if (error == 0) {
		rrm_enter(&(*zsbp)->z_teardown_lock, (writer) ? RW_WRITER :
		    RW_READER, tag);
		if ((*zsbp)->z_unmounted) {
			/*
			 * XXX we could probably try again, since the unmounting
			 * thread should be just about to disassociate the
			 * objset from the zsb.
			 */
			rrm_exit(&(*zsbp)->z_teardown_lock, tag);
			return (SET_ERROR(EBUSY));
		}
	}
	return (error);
}

static void
zfs_sb_rele(zfs_sb_t *zsb, void *tag)
{
	rrm_exit(&zsb->z_teardown_lock, tag);

	if (zsb->z_sb) {
		deactivate_super(zsb->z_sb);
	} else {
		dmu_objset_disown(zsb->z_os, zsb);
		zfs_sb_free(zsb);
	}
}

static int
zfs_ioc_pool_create(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *config, *props = NULL;
	nvlist_t *rootprops = NULL;
	nvlist_t *zplprops = NULL;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)))
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if (props) {
		nvlist_t *nvl = NULL;
		uint64_t version = SPA_VERSION;

		(void) nvlist_lookup_uint64(props,
		    zpool_prop_to_name(ZPOOL_PROP_VERSION), &version);
		if (!SPA_VERSION_IS_SUPPORTED(version)) {
			error = SET_ERROR(EINVAL);
			goto pool_props_bad;
		}
		(void) nvlist_lookup_nvlist(props, ZPOOL_ROOTFS_PROPS, &nvl);
		if (nvl) {
			error = nvlist_dup(nvl, &rootprops, KM_SLEEP);
			if (error != 0) {
				nvlist_free(config);
				nvlist_free(props);
				return (error);
			}
			(void) nvlist_remove_all(props, ZPOOL_ROOTFS_PROPS);
		}
		VERIFY(nvlist_alloc(&zplprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		error = zfs_fill_zplprops_root(version, rootprops,
		    zplprops, NULL);
		if (error != 0)
			goto pool_props_bad;
	}

	error = spa_create(zc->zc_name, config, props, zplprops);

	/*
	 * Set the remaining root properties
	 */
	if (!error && (error = zfs_set_prop_nvlist(zc->zc_name,
	    ZPROP_SRC_LOCAL, rootprops, NULL)) != 0)
		(void) spa_destroy(zc->zc_name);

pool_props_bad:
	nvlist_free(rootprops);
	nvlist_free(zplprops);
	nvlist_free(config);
	nvlist_free(props);

	return (error);
}

static int
zfs_ioc_pool_destroy(zfs_cmd_t *zc)
{
	int error;
	zfs_log_history(zc);
	error = spa_destroy(zc->zc_name);

	return (error);
}

static int
zfs_ioc_pool_import(zfs_cmd_t *zc)
{
	nvlist_t *config, *props = NULL;
	uint64_t guid;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) != 0)
		return (error);

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		nvlist_free(config);
		return (error);
	}

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &guid) != 0 ||
	    guid != zc->zc_guid)
		error = SET_ERROR(EINVAL);
	else
		error = spa_import(zc->zc_name, config, props, zc->zc_cookie);

	if (zc->zc_nvlist_dst != 0) {
		int err;

		if ((err = put_nvlist(zc, config)) != 0)
			error = err;
	}

	nvlist_free(config);

	if (props)
		nvlist_free(props);

	return (error);
}

static int
zfs_ioc_pool_export(zfs_cmd_t *zc)
{
	int error;
	boolean_t force = (boolean_t)zc->zc_cookie;
	boolean_t hardforce = (boolean_t)zc->zc_guid;

	zfs_log_history(zc);
	error = spa_export(zc->zc_name, NULL, force, hardforce);

	return (error);
}

static int
zfs_ioc_pool_configs(zfs_cmd_t *zc)
{
	nvlist_t *configs;
	int error;

	if ((configs = spa_all_configs(&zc->zc_cookie)) == NULL)
		return (SET_ERROR(EEXIST));

	error = put_nvlist(zc, configs);

	nvlist_free(configs);

	return (error);
}

/*
 * inputs:
 * zc_name		name of the pool
 *
 * outputs:
 * zc_cookie		real errno
 * zc_nvlist_dst	config nvlist
 * zc_nvlist_dst_size	size of config nvlist
 */
static int
zfs_ioc_pool_stats(zfs_cmd_t *zc)
{
	nvlist_t *config;
	int error;
	int ret = 0;

	error = spa_get_stats(zc->zc_name, &config, zc->zc_value,
	    sizeof (zc->zc_value));

	if (config != NULL) {
		ret = put_nvlist(zc, config);
		nvlist_free(config);

		/*
		 * The config may be present even if 'error' is non-zero.
		 * In this case we return success, and preserve the real errno
		 * in 'zc_cookie'.
		 */
		zc->zc_cookie = error;
	} else {
		ret = error;
	}

	return (ret);
}

/*
 * Try to import the given pool, returning pool stats as appropriate so that
 * user land knows which devices are available and overall pool health.
 */
static int
zfs_ioc_pool_tryimport(zfs_cmd_t *zc)
{
	nvlist_t *tryconfig, *config;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &tryconfig)) != 0)
		return (error);

	config = spa_tryimport(tryconfig);

	nvlist_free(tryconfig);

	if (config == NULL)
		return (SET_ERROR(EINVAL));

	error = put_nvlist(zc, config);
	nvlist_free(config);

	return (error);
}

/*
 * inputs:
 * zc_name              name of the pool
 * zc_cookie            scan func (pool_scan_func_t)
 */
static int
zfs_ioc_pool_scan(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_cookie == POOL_SCAN_NONE)
		error = spa_scan_stop(spa);
	else
		error = spa_scan(spa, zc->zc_cookie);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_freeze(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error == 0) {
		spa_freeze(spa);
		spa_close(spa, FTAG);
	}
	return (error);
}

static int
zfs_ioc_pool_upgrade(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_cookie < spa_version(spa) ||
	    !SPA_VERSION_IS_SUPPORTED(zc->zc_cookie)) {
		spa_close(spa, FTAG);
		return (SET_ERROR(EINVAL));
	}

	spa_upgrade(spa, zc->zc_cookie);
	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_get_history(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *hist_buf;
	uint64_t size;
	int error;

	if ((size = zc->zc_history_len) == 0)
		return (SET_ERROR(EINVAL));

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY) {
		spa_close(spa, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	hist_buf = vmem_alloc(size, KM_SLEEP);
	if ((error = spa_history_get(spa, &zc->zc_history_offset,
	    &zc->zc_history_len, hist_buf)) == 0) {
		error = ddi_copyout(hist_buf,
		    (void *)(uintptr_t)zc->zc_history,
		    zc->zc_history_len, zc->zc_iflags);
	}

	spa_close(spa, FTAG);
	vmem_free(hist_buf, size);
	return (error);
}

static int
zfs_ioc_pool_reguid(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error == 0) {
		error = spa_change_guid(spa);
		spa_close(spa, FTAG);
	}
	return (error);
}

static int
zfs_ioc_dsobj_to_dsname(zfs_cmd_t *zc)
{
	return (dsl_dsobj_to_dsname(zc->zc_name, zc->zc_obj, zc->zc_value));
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_obj		object to find
 *
 * outputs:
 * zc_value		name of object
 */
static int
zfs_ioc_obj_to_path(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	/* XXX reading from objset not owned */
	if ((error = dmu_objset_hold(zc->zc_name, FTAG, &os)) != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (SET_ERROR(EINVAL));
	}
	error = zfs_obj_to_path(os, zc->zc_obj, zc->zc_value,
	    sizeof (zc->zc_value));
	dmu_objset_rele(os, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_obj		object to find
 *
 * outputs:
 * zc_stat		stats on object
 * zc_value		path to object
 */
static int
zfs_ioc_obj_to_stats(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	/* XXX reading from objset not owned */
	if ((error = dmu_objset_hold(zc->zc_name, FTAG, &os)) != 0)
		return (error);
	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (SET_ERROR(EINVAL));
	}
	error = zfs_obj_to_stats(os, zc->zc_obj, &zc->zc_stat, zc->zc_value,
	    sizeof (zc->zc_value));
	dmu_objset_rele(os, FTAG);

	return (error);
}

static int
zfs_ioc_vdev_add(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *config;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config);
	if (error == 0) {
		error = spa_vdev_add(spa, config);
		nvlist_free(config);
	}
	spa_close(spa, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of the pool
 * zc_nvlist_conf	nvlist of devices to remove
 * zc_cookie		to stop the remove?
 */
static int
zfs_ioc_vdev_remove(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);
	error = spa_vdev_remove(spa, zc->zc_guid, B_FALSE);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_set_state(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	vdev_state_t newstate = VDEV_STATE_UNKNOWN;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);
	switch (zc->zc_cookie) {
	case VDEV_STATE_ONLINE:
		error = vdev_online(spa, zc->zc_guid, zc->zc_obj, &newstate);
		break;

	case VDEV_STATE_OFFLINE:
		error = vdev_offline(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_FAULTED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_fault(spa, zc->zc_guid, zc->zc_obj);
		break;

	case VDEV_STATE_DEGRADED:
		if (zc->zc_obj != VDEV_AUX_ERR_EXCEEDED &&
		    zc->zc_obj != VDEV_AUX_EXTERNAL)
			zc->zc_obj = VDEV_AUX_ERR_EXCEEDED;

		error = vdev_degrade(spa, zc->zc_guid, zc->zc_obj);
		break;

	default:
		error = SET_ERROR(EINVAL);
	}
	zc->zc_cookie = newstate;
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_attach(zfs_cmd_t *zc)
{
	spa_t *spa;
	int replacing = zc->zc_cookie;
	nvlist_t *config;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) == 0) {
		error = spa_vdev_attach(spa, zc->zc_guid, config, replacing);
		nvlist_free(config);
	}

	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_detach(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_vdev_detach(spa, zc->zc_guid, 0, B_FALSE);

	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_split(zfs_cmd_t *zc)
{
	spa_t *spa;
	nvlist_t *config, *props = NULL;
	int error;
	boolean_t exp = !!(zc->zc_cookie & ZPOOL_EXPORT_AFTER_SPLIT);

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if ((error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config))) {
		spa_close(spa, FTAG);
		return (error);
	}

	if (zc->zc_nvlist_src_size != 0 && (error =
	    get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))) {
		spa_close(spa, FTAG);
		nvlist_free(config);
		return (error);
	}

	error = spa_vdev_split_mirror(spa, zc->zc_string, config, props, exp);

	spa_close(spa, FTAG);

	nvlist_free(config);
	nvlist_free(props);

	return (error);
}

static int
zfs_ioc_vdev_setpath(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *path = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setpath(spa, guid, path);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_vdev_setfru(zfs_cmd_t *zc)
{
	spa_t *spa;
	char *fru = zc->zc_value;
	uint64_t guid = zc->zc_guid;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = spa_vdev_setfru(spa, guid, fru);
	spa_close(spa, FTAG);
	return (error);
}

static int
zfs_ioc_objset_stats_impl(zfs_cmd_t *zc, objset_t *os)
{
	int error = 0;
	nvlist_t *nv;

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_all(os, &nv)) == 0) {
		dmu_objset_stats(os, nv);
		/*
		 * NB: zvol_get_stats() will read the objset contents,
		 * which we aren't supposed to do with a
		 * DS_MODE_USER hold, because it could be
		 * inconsistent.  So this is a bit of a workaround...
		 * XXX reading with out owning
		 */
		if (!zc->zc_objset_stats.dds_inconsistent &&
		    dmu_objset_type(os) == DMU_OST_ZVOL) {
			error = zvol_get_stats(os, nv);
			if (error == EIO)
				return (error);
			VERIFY0(error);
		}
		if (error == 0)
			error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_objset_stats(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error == 0) {
		error = zfs_ioc_objset_stats_impl(zc, os);
		dmu_objset_rele(os, FTAG);
	}

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_nvlist_dst	received property nvlist
 * zc_nvlist_dst_size	size of received property nvlist
 *
 * Gets received properties (distinct from local properties on or after
 * SPA_VERSION_RECVD_PROPS) for callers who want to differentiate received from
 * local property values.
 */
static int
zfs_ioc_objset_recvd_props(zfs_cmd_t *zc)
{
	int error = 0;
	nvlist_t *nv;

	/*
	 * Without this check, we would return local property values if the
	 * caller has not already received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	if (!dsl_prop_get_hasrecvd(zc->zc_name))
		return (SET_ERROR(ENOTSUP));

	if (zc->zc_nvlist_dst != 0 &&
	    (error = dsl_prop_get_received(zc->zc_name, &nv)) == 0) {
		error = put_nvlist(zc, nv);
		nvlist_free(nv);
	}

	return (error);
}

static int
nvl_add_zplprop(objset_t *os, nvlist_t *props, zfs_prop_t prop)
{
	uint64_t value;
	int error;

	/*
	 * zfs_get_zplprop() will either find a value or give us
	 * the default value (if there is one).
	 */
	if ((error = zfs_get_zplprop(os, prop, &value)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(props, zfs_prop_to_name(prop), value) == 0);
	return (0);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_dst_size	size of buffer for zpl property nvlist
 *
 * outputs:
 * zc_nvlist_dst	zpl property nvlist
 * zc_nvlist_dst_size	size of zpl property nvlist
 */
static int
zfs_ioc_objset_zplprops(zfs_cmd_t *zc)
{
	objset_t *os;
	int err;

	/* XXX reading without owning */
	if ((err = dmu_objset_hold(zc->zc_name, FTAG, &os)))
		return (err);

	dmu_objset_fast_stat(os, &zc->zc_objset_stats);

	/*
	 * NB: nvl_add_zplprop() will read the objset contents,
	 * which we aren't supposed to do with a DS_MODE_USER
	 * hold, because it could be inconsistent.
	 */
	if (zc->zc_nvlist_dst != 0 &&
	    !zc->zc_objset_stats.dds_inconsistent &&
	    dmu_objset_type(os) == DMU_OST_ZFS) {
		nvlist_t *nv;

		VERIFY(nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		if ((err = nvl_add_zplprop(os, nv, ZFS_PROP_VERSION)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_NORMALIZE)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_UTF8ONLY)) == 0 &&
		    (err = nvl_add_zplprop(os, nv, ZFS_PROP_CASE)) == 0)
			err = put_nvlist(zc, nv);
		nvlist_free(nv);
	} else {
		err = SET_ERROR(ENOENT);
	}
	dmu_objset_rele(os, FTAG);
	return (err);
}

boolean_t
dataset_name_hidden(const char *name)
{
	/*
	 * Skip over datasets that are not visible in this zone,
	 * internal datasets (which have a $ in their name), and
	 * temporary datasets (which have a % in their name).
	 */
	if (strchr(name, '$') != NULL)
		return (B_TRUE);
	if (strchr(name, '%') != NULL)
		return (B_TRUE);
	if (!INGLOBALZONE(curproc) && !zone_dataset_visible(name, NULL))
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_name		name of next filesystem
 * zc_cookie		zap cursor
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_dataset_list_next(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;
	char *p;
	size_t orig_len = strlen(zc->zc_name);

top:
	if ((error = dmu_objset_hold(zc->zc_name, FTAG, &os))) {
		if (error == ENOENT)
			error = SET_ERROR(ESRCH);
		return (error);
	}

	p = strrchr(zc->zc_name, '/');
	if (p == NULL || p[1] != '\0')
		(void) strlcat(zc->zc_name, "/", sizeof (zc->zc_name));
	p = zc->zc_name + strlen(zc->zc_name);

	do {
		error = dmu_dir_list_next(os,
		    sizeof (zc->zc_name) - (p - zc->zc_name), p,
		    NULL, &zc->zc_cookie);
		if (error == ENOENT)
			error = SET_ERROR(ESRCH);
	} while (error == 0 && dataset_name_hidden(zc->zc_name));
	dmu_objset_rele(os, FTAG);

	/*
	 * If it's an internal dataset (ie. with a '$' in its name),
	 * don't try to get stats for it, otherwise we'll return ENOENT.
	 */
	if (error == 0 && strchr(zc->zc_name, '$') == NULL) {
		error = zfs_ioc_objset_stats(zc); /* fill in the stats */
		if (error == ENOENT) {
			/* We lost a race with destroy, get the next one. */
			zc->zc_name[orig_len] = '\0';
			goto top;
		}
	}
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_nvlist_dst_size	size of buffer for property nvlist
 *
 * outputs:
 * zc_name		name of next snapshot
 * zc_objset_stats	stats
 * zc_nvlist_dst	property nvlist
 * zc_nvlist_dst_size	size of property nvlist
 */
static int
zfs_ioc_snapshot_list_next(zfs_cmd_t *zc)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error != 0) {
		return (error == ENOENT ? ESRCH : error);
	}

	/*
	 * A dataset name of maximum length cannot have any snapshots,
	 * so exit immediately.
	 */
	if (strlcat(zc->zc_name, "@", sizeof (zc->zc_name)) >= MAXNAMELEN) {
		dmu_objset_rele(os, FTAG);
		return (SET_ERROR(ESRCH));
	}

	error = dmu_snapshot_list_next(os,
	    sizeof (zc->zc_name) - strlen(zc->zc_name),
	    zc->zc_name + strlen(zc->zc_name), &zc->zc_obj, &zc->zc_cookie,
	    NULL);

	if (error == 0 && !zc->zc_simple) {
		dsl_dataset_t *ds;
		dsl_pool_t *dp = os->os_dsl_dataset->ds_dir->dd_pool;

		error = dsl_dataset_hold_obj(dp, zc->zc_obj, FTAG, &ds);
		if (error == 0) {
			objset_t *ossnap;

			error = dmu_objset_from_ds(ds, &ossnap);
			if (error == 0)
				error = zfs_ioc_objset_stats_impl(zc, ossnap);
			dsl_dataset_rele(ds, FTAG);
		}
	} else if (error == ENOENT) {
		error = SET_ERROR(ESRCH);
	}

	dmu_objset_rele(os, FTAG);
	/* if we failed, undo the @ that we tacked on to zc_name */
	if (error != 0)
		*strchr(zc->zc_name, '@') = '\0';
	return (error);
}

static int
zfs_prop_set_userquota(const char *dsname, nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	uint64_t *valary;
	unsigned int vallen;
	const char *domain;
	char *dash;
	zfs_userquota_prop_t type;
	uint64_t rid;
	uint64_t quota;
	zfs_sb_t *zsb;
	int err;

	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &pair) != 0)
			return (SET_ERROR(EINVAL));
	}

	/*
	 * A correctly constructed propname is encoded as
	 * userquota@<rid>-<domain>.
	 */
	if ((dash = strchr(propname, '-')) == NULL ||
	    nvpair_value_uint64_array(pair, &valary, &vallen) != 0 ||
	    vallen != 3)
		return (SET_ERROR(EINVAL));

	domain = dash + 1;
	type = valary[0];
	rid = valary[1];
	quota = valary[2];

	err = zfs_sb_hold(dsname, FTAG, &zsb, B_FALSE);
	if (err == 0) {
		err = zfs_set_userquota(zsb, type, domain, rid, quota);
		zfs_sb_rele(zsb, FTAG);
	}

	return (err);
}

/*
 * If the named property is one that has a special function to set its value,
 * return 0 on success and a positive error code on failure; otherwise if it is
 * not one of the special properties handled by this function, return -1.
 *
 * XXX: It would be better for callers of the property interface if we handled
 * these special cases in dsl_prop.c (in the dsl layer).
 */
static int
zfs_prop_set_special(const char *dsname, zprop_source_t source,
    nvpair_t *pair)
{
	const char *propname = nvpair_name(pair);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval;
	int err = -1;

	if (prop == ZPROP_INVAL) {
		if (zfs_prop_userquota(propname))
			return (zfs_prop_set_userquota(dsname, pair));
		return (-1);
	}

	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &pair) == 0);
	}

	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING)
		return (-1);

	VERIFY(0 == nvpair_value_uint64(pair, &intval));

	switch (prop) {
	case ZFS_PROP_QUOTA:
		err = dsl_dir_set_quota(dsname, source, intval);
		break;
	case ZFS_PROP_REFQUOTA:
		err = dsl_dataset_set_refquota(dsname, source, intval);
		break;
	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
		if (intval == UINT64_MAX) {
			/* clearing the limit, just do it */
			err = 0;
		} else {
			err = dsl_dir_activate_fs_ss_limit(dsname);
		}
		/*
		 * Set err to -1 to force the zfs_set_prop_nvlist code down the
		 * default path to set the value in the nvlist.
		 */
		if (err == 0)
			err = -1;
		break;
	case ZFS_PROP_RESERVATION:
		err = dsl_dir_set_reservation(dsname, source, intval);
		break;
	case ZFS_PROP_REFRESERVATION:
		err = dsl_dataset_set_refreservation(dsname, source, intval);
		break;
	case ZFS_PROP_VOLSIZE:
		err = zvol_set_volsize(dsname, intval);
		break;
	case ZFS_PROP_SNAPDEV:
		err = zvol_set_snapdev(dsname, source, intval);
		break;
	case ZFS_PROP_VERSION:
	{
		zfs_sb_t *zsb;

		if ((err = zfs_sb_hold(dsname, FTAG, &zsb, B_TRUE)) != 0)
			break;

		err = zfs_set_version(zsb, intval);
		zfs_sb_rele(zsb, FTAG);

		if (err == 0 && intval >= ZPL_VERSION_USERSPACE) {
			zfs_cmd_t *zc;

			zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);
			(void) strcpy(zc->zc_name, dsname);
			(void) zfs_ioc_userspace_upgrade(zc);
			kmem_free(zc, sizeof (zfs_cmd_t));
		}
		break;
	}
	default:
		err = -1;
	}

	return (err);
}

/*
 * This function is best effort. If it fails to set any of the given properties,
 * it continues to set as many as it can and returns the last error
 * encountered. If the caller provides a non-NULL errlist, it will be filled in
 * with the list of names of all the properties that failed along with the
 * corresponding error numbers.
 *
 * If every property is set successfully, zero is returned and errlist is not
 * modified.
 */
int
zfs_set_prop_nvlist(const char *dsname, zprop_source_t source, nvlist_t *nvl,
    nvlist_t *errlist)
{
	nvpair_t *pair;
	nvpair_t *propval;
	int rv = 0;
	uint64_t intval;
	char *strval;

	nvlist_t *genericnvl = fnvlist_alloc();
	nvlist_t *retrynvl = fnvlist_alloc();
retry:
	pair = NULL;
	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);
		zfs_prop_t prop = zfs_name_to_prop(propname);
		int err = 0;

		/* decode the property value */
		propval = pair;
		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			nvlist_t *attrs;
			attrs = fnvpair_value_nvlist(pair);
			if (nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
			    &propval) != 0)
				err = SET_ERROR(EINVAL);
		}

		/* Validate value type */
		if (err == 0 && prop == ZPROP_INVAL) {
			if (zfs_prop_user(propname)) {
				if (nvpair_type(propval) != DATA_TYPE_STRING)
					err = SET_ERROR(EINVAL);
			} else if (zfs_prop_userquota(propname)) {
				if (nvpair_type(propval) !=
				    DATA_TYPE_UINT64_ARRAY)
					err = SET_ERROR(EINVAL);
			} else {
				err = SET_ERROR(EINVAL);
			}
		} else if (err == 0) {
			if (nvpair_type(propval) == DATA_TYPE_STRING) {
				if (zfs_prop_get_type(prop) != PROP_TYPE_STRING)
					err = SET_ERROR(EINVAL);
			} else if (nvpair_type(propval) == DATA_TYPE_UINT64) {
				const char *unused;

				intval = fnvpair_value_uint64(propval);

				switch (zfs_prop_get_type(prop)) {
				case PROP_TYPE_NUMBER:
					break;
				case PROP_TYPE_STRING:
					err = SET_ERROR(EINVAL);
					break;
				case PROP_TYPE_INDEX:
					if (zfs_prop_index_to_string(prop,
					    intval, &unused) != 0)
						err = SET_ERROR(EINVAL);
					break;
				default:
					cmn_err(CE_PANIC,
					    "unknown property type");
				}
			} else {
				err = SET_ERROR(EINVAL);
			}
		}

		/* Validate permissions */
		if (err == 0)
			err = zfs_check_settable(dsname, pair, CRED());

		if (err == 0) {
			err = zfs_prop_set_special(dsname, source, pair);
			if (err == -1) {
				/*
				 * For better performance we build up a list of
				 * properties to set in a single transaction.
				 */
				err = nvlist_add_nvpair(genericnvl, pair);
			} else if (err != 0 && nvl != retrynvl) {
				/*
				 * This may be a spurious error caused by
				 * receiving quota and reservation out of order.
				 * Try again in a second pass.
				 */
				err = nvlist_add_nvpair(retrynvl, pair);
			}
		}

		if (err != 0) {
			if (errlist != NULL)
				fnvlist_add_int32(errlist, propname, err);
			rv = err;
		}
	}

	if (nvl != retrynvl && !nvlist_empty(retrynvl)) {
		nvl = retrynvl;
		goto retry;
	}

	if (!nvlist_empty(genericnvl) &&
	    dsl_props_set(dsname, source, genericnvl) != 0) {
		/*
		 * If this fails, we still want to set as many properties as we
		 * can, so try setting them individually.
		 */
		pair = NULL;
		while ((pair = nvlist_next_nvpair(genericnvl, pair)) != NULL) {
			const char *propname = nvpair_name(pair);
			int err = 0;

			propval = pair;
			if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
				nvlist_t *attrs;
				attrs = fnvpair_value_nvlist(pair);
				propval = fnvlist_lookup_nvpair(attrs,
				    ZPROP_VALUE);
			}

			if (nvpair_type(propval) == DATA_TYPE_STRING) {
				strval = fnvpair_value_string(propval);
				err = dsl_prop_set_string(dsname, propname,
				    source, strval);
			} else {
				intval = fnvpair_value_uint64(propval);
				err = dsl_prop_set_int(dsname, propname, source,
				    intval);
			}

			if (err != 0) {
				if (errlist != NULL) {
					fnvlist_add_int32(errlist, propname,
					    err);
				}
				rv = err;
			}
		}
	}
	nvlist_free(genericnvl);
	nvlist_free(retrynvl);

	return (rv);
}

/*
 * Check that all the properties are valid user properties.
 */
static int
zfs_check_userprops(const char *fsname, nvlist_t *nvl)
{
	nvpair_t *pair = NULL;
	int error = 0;

	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL) {
		const char *propname = nvpair_name(pair);

		if (!zfs_prop_user(propname) ||
		    nvpair_type(pair) != DATA_TYPE_STRING)
			return (SET_ERROR(EINVAL));

		if ((error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_USERPROP, CRED())))
			return (error);

		if (strlen(propname) >= ZAP_MAXNAMELEN)
			return (SET_ERROR(ENAMETOOLONG));

		if (strlen(fnvpair_value_string(pair)) >= ZAP_MAXVALUELEN)
			return (SET_ERROR(E2BIG));
	}
	return (0);
}

static void
props_skip(nvlist_t *props, nvlist_t *skipped, nvlist_t **newprops)
{
	nvpair_t *pair;

	VERIFY(nvlist_alloc(newprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	pair = NULL;
	while ((pair = nvlist_next_nvpair(props, pair)) != NULL) {
		if (nvlist_exists(skipped, nvpair_name(pair)))
			continue;

		VERIFY(nvlist_add_nvpair(*newprops, pair) == 0);
	}
}

static int
clear_received_props(const char *dsname, nvlist_t *props,
    nvlist_t *skipped)
{
	int err = 0;
	nvlist_t *cleared_props = NULL;
	props_skip(props, skipped, &cleared_props);
	if (!nvlist_empty(cleared_props)) {
		/*
		 * Acts on local properties until the dataset has received
		 * properties at least once on or after SPA_VERSION_RECVD_PROPS.
		 */
		zprop_source_t flags = (ZPROP_SRC_NONE |
		    (dsl_prop_get_hasrecvd(dsname) ? ZPROP_SRC_RECEIVED : 0));
		err = zfs_set_prop_nvlist(dsname, flags, cleared_props, NULL);
	}
	nvlist_free(cleared_props);
	return (err);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		name of property to set
 * zc_nvlist_src{_size}	nvlist of properties to apply
 * zc_cookie		received properties flag
 *
 * outputs:
 * zc_nvlist_dst{_size} error for each unapplied received property
 */
static int
zfs_ioc_set_prop(zfs_cmd_t *zc)
{
	nvlist_t *nvl;
	boolean_t received = zc->zc_cookie;
	zprop_source_t source = (received ? ZPROP_SRC_RECEIVED :
	    ZPROP_SRC_LOCAL);
	nvlist_t *errors;
	int error;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &nvl)) != 0)
		return (error);

	if (received) {
		nvlist_t *origprops;

		if (dsl_prop_get_received(zc->zc_name, &origprops) == 0) {
			(void) clear_received_props(zc->zc_name,
			    origprops, nvl);
			nvlist_free(origprops);
		}

		error = dsl_prop_set_hasrecvd(zc->zc_name);
	}

	errors = fnvlist_alloc();
	if (error == 0)
		error = zfs_set_prop_nvlist(zc->zc_name, source, nvl, errors);

	if (zc->zc_nvlist_dst != 0 && errors != NULL) {
		(void) put_nvlist(zc, errors);
	}

	nvlist_free(errors);
	nvlist_free(nvl);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		name of property to inherit
 * zc_cookie		revert to received value if TRUE
 *
 * outputs:		none
 */
static int
zfs_ioc_inherit_prop(zfs_cmd_t *zc)
{
	const char *propname = zc->zc_value;
	zfs_prop_t prop = zfs_name_to_prop(propname);
	boolean_t received = zc->zc_cookie;
	zprop_source_t source = (received
	    ? ZPROP_SRC_NONE		/* revert to received value, if any */
	    : ZPROP_SRC_INHERITED);	/* explicitly inherit */

	if (received) {
		nvlist_t *dummy;
		nvpair_t *pair;
		zprop_type_t type;
		int err;

		/*
		 * zfs_prop_set_special() expects properties in the form of an
		 * nvpair with type info.
		 */
		if (prop == ZPROP_INVAL) {
			if (!zfs_prop_user(propname))
				return (SET_ERROR(EINVAL));

			type = PROP_TYPE_STRING;
		} else if (prop == ZFS_PROP_VOLSIZE ||
		    prop == ZFS_PROP_VERSION) {
			return (SET_ERROR(EINVAL));
		} else {
			type = zfs_prop_get_type(prop);
		}

		VERIFY(nvlist_alloc(&dummy, NV_UNIQUE_NAME, KM_SLEEP) == 0);

		switch (type) {
		case PROP_TYPE_STRING:
			VERIFY(0 == nvlist_add_string(dummy, propname, ""));
			break;
		case PROP_TYPE_NUMBER:
		case PROP_TYPE_INDEX:
			VERIFY(0 == nvlist_add_uint64(dummy, propname, 0));
			break;
		default:
			nvlist_free(dummy);
			return (SET_ERROR(EINVAL));
		}

		pair = nvlist_next_nvpair(dummy, NULL);
		err = zfs_prop_set_special(zc->zc_name, source, pair);
		nvlist_free(dummy);
		if (err != -1)
			return (err); /* special property already handled */
	} else {
		/*
		 * Only check this in the non-received case. We want to allow
		 * 'inherit -S' to revert non-inheritable properties like quota
		 * and reservation to the received or default values even though
		 * they are not considered inheritable.
		 */
		if (prop != ZPROP_INVAL && !zfs_prop_inheritable(prop))
			return (SET_ERROR(EINVAL));
	}

	/* property name has been validated by zfs_secpolicy_inherit_prop() */
	return (dsl_prop_inherit(zc->zc_name, zc->zc_value, source));
}

static int
zfs_ioc_pool_set_props(zfs_cmd_t *zc)
{
	nvlist_t *props;
	spa_t *spa;
	int error;
	nvpair_t *pair;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props)))
		return (error);

	/*
	 * If the only property is the configfile, then just do a spa_lookup()
	 * to handle the faulted case.
	 */
	pair = nvlist_next_nvpair(props, NULL);
	if (pair != NULL && strcmp(nvpair_name(pair),
	    zpool_prop_to_name(ZPOOL_PROP_CACHEFILE)) == 0 &&
	    nvlist_next_nvpair(props, pair) == NULL) {
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(zc->zc_name)) != NULL) {
			spa_configfile_set(spa, props, B_FALSE);
			spa_config_sync(spa, B_FALSE, B_TRUE);
		}
		mutex_exit(&spa_namespace_lock);
		if (spa != NULL) {
			nvlist_free(props);
			return (0);
		}
	}

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0) {
		nvlist_free(props);
		return (error);
	}

	error = spa_prop_set(spa, props);

	nvlist_free(props);
	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_get_props(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	nvlist_t *nvp = NULL;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0) {
		/*
		 * If the pool is faulted, there may be properties we can still
		 * get (such as altroot and cachefile), so attempt to get them
		 * anyway.
		 */
		mutex_enter(&spa_namespace_lock);
		if ((spa = spa_lookup(zc->zc_name)) != NULL)
			error = spa_prop_get(spa, &nvp);
		mutex_exit(&spa_namespace_lock);
	} else {
		error = spa_prop_get(spa, &nvp);
		spa_close(spa, FTAG);
	}

	if (error == 0 && zc->zc_nvlist_dst != 0)
		error = put_nvlist(zc, nvp);
	else
		error = SET_ERROR(EFAULT);

	nvlist_free(nvp);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_nvlist_src{_size}	nvlist of delegated permissions
 * zc_perm_action	allow/unallow flag
 *
 * outputs:		none
 */
static int
zfs_ioc_set_fsacl(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *fsaclnv = NULL;

	if ((error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &fsaclnv)) != 0)
		return (error);

	/*
	 * Verify nvlist is constructed correctly
	 */
	if ((error = zfs_deleg_verify_nvlist(fsaclnv)) != 0) {
		nvlist_free(fsaclnv);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If we don't have PRIV_SYS_MOUNT, then validate
	 * that user is allowed to hand out each permission in
	 * the nvlist(s)
	 */

	error = secpolicy_zfs(CRED());
	if (error != 0) {
		if (zc->zc_perm_action == B_FALSE) {
			error = dsl_deleg_can_allow(zc->zc_name,
			    fsaclnv, CRED());
		} else {
			error = dsl_deleg_can_unallow(zc->zc_name,
			    fsaclnv, CRED());
		}
	}

	if (error == 0)
		error = dsl_deleg_set(zc->zc_name, fsaclnv, zc->zc_perm_action);

	nvlist_free(fsaclnv);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * zc_nvlist_src{_size}	nvlist of delegated permissions
 */
static int
zfs_ioc_get_fsacl(zfs_cmd_t *zc)
{
	nvlist_t *nvp;
	int error;

	if ((error = dsl_deleg_get(zc->zc_name, &nvp)) == 0) {
		error = put_nvlist(zc, nvp);
		nvlist_free(nvp);
	}

	return (error);
}

/* ARGSUSED */
static void
zfs_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;

	zfs_create_fs(os, cr, zct->zct_zplprops, tx);
}

#define	ZFS_PROP_UNDEFINED	((uint64_t)-1)

/*
 * inputs:
 * os			parent objset pointer (NULL if root fs)
 * fuids_ok		fuids allowed in this version of the spa?
 * sa_ok		SAs allowed in this version of the spa?
 * createprops		list of properties requested by creator
 *
 * outputs:
 * zplprops	values for the zplprops we attach to the master node object
 * is_ci	true if requested file system will be purely case-insensitive
 *
 * Determine the settings for utf8only, normalization and
 * casesensitivity.  Specific values may have been requested by the
 * creator and/or we can inherit values from the parent dataset.  If
 * the file system is of too early a vintage, a creator can not
 * request settings for these properties, even if the requested
 * setting is the default value.  We don't actually want to create dsl
 * properties for these, so remove them from the source nvlist after
 * processing.
 */
static int
zfs_fill_zplprops_impl(objset_t *os, uint64_t zplver,
    boolean_t fuids_ok, boolean_t sa_ok, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	uint64_t sense = ZFS_PROP_UNDEFINED;
	uint64_t norm = ZFS_PROP_UNDEFINED;
	uint64_t u8 = ZFS_PROP_UNDEFINED;
	int error;

	ASSERT(zplprops != NULL);

	/*
	 * Pull out creator prop choices, if any.
	 */
	if (createprops) {
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_VERSION), &zplver);
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_NORMALIZE), &norm);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_NORMALIZE));
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), &u8);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_UTF8ONLY));
		(void) nvlist_lookup_uint64(createprops,
		    zfs_prop_to_name(ZFS_PROP_CASE), &sense);
		(void) nvlist_remove_all(createprops,
		    zfs_prop_to_name(ZFS_PROP_CASE));
	}

	/*
	 * If the zpl version requested is whacky or the file system
	 * or pool is version is too "young" to support normalization
	 * and the creator tried to set a value for one of the props,
	 * error out.
	 */
	if ((zplver < ZPL_VERSION_INITIAL || zplver > ZPL_VERSION) ||
	    (zplver >= ZPL_VERSION_FUID && !fuids_ok) ||
	    (zplver >= ZPL_VERSION_SA && !sa_ok) ||
	    (zplver < ZPL_VERSION_NORMALIZATION &&
	    (norm != ZFS_PROP_UNDEFINED || u8 != ZFS_PROP_UNDEFINED ||
	    sense != ZFS_PROP_UNDEFINED)))
		return (SET_ERROR(ENOTSUP));

	/*
	 * Put the version in the zplprops
	 */
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_VERSION), zplver) == 0);

	if (norm == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &norm)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_NORMALIZE), norm) == 0);

	/*
	 * If we're normalizing, names must always be valid UTF-8 strings.
	 */
	if (norm)
		u8 = 1;
	if (u8 == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &u8)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), u8) == 0);

	if (sense == ZFS_PROP_UNDEFINED &&
	    (error = zfs_get_zplprop(os, ZFS_PROP_CASE, &sense)) != 0)
		return (error);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_CASE), sense) == 0);

	if (is_ci)
		*is_ci = (sense == ZFS_CASE_INSENSITIVE);

	return (0);
}

static int
zfs_fill_zplprops(const char *dataset, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	boolean_t fuids_ok, sa_ok;
	uint64_t zplver = ZPL_VERSION;
	objset_t *os = NULL;
	char parentname[MAXNAMELEN];
	char *cp;
	spa_t *spa;
	uint64_t spa_vers;
	int error;

	(void) strlcpy(parentname, dataset, sizeof (parentname));
	cp = strrchr(parentname, '/');
	ASSERT(cp != NULL);
	cp[0] = '\0';

	if ((error = spa_open(dataset, &spa, FTAG)) != 0)
		return (error);

	spa_vers = spa_version(spa);
	spa_close(spa, FTAG);

	zplver = zfs_zpl_version_map(spa_vers);
	fuids_ok = (zplver >= ZPL_VERSION_FUID);
	sa_ok = (zplver >= ZPL_VERSION_SA);

	/*
	 * Open parent object set so we can inherit zplprop values.
	 */
	if ((error = dmu_objset_hold(parentname, FTAG, &os)) != 0)
		return (error);

	error = zfs_fill_zplprops_impl(os, zplver, fuids_ok, sa_ok, createprops,
	    zplprops, is_ci);
	dmu_objset_rele(os, FTAG);
	return (error);
}

static int
zfs_fill_zplprops_root(uint64_t spa_vers, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci)
{
	boolean_t fuids_ok;
	boolean_t sa_ok;
	uint64_t zplver = ZPL_VERSION;
	int error;

	zplver = zfs_zpl_version_map(spa_vers);
	fuids_ok = (zplver >= ZPL_VERSION_FUID);
	sa_ok = (zplver >= ZPL_VERSION_SA);

	error = zfs_fill_zplprops_impl(NULL, zplver, fuids_ok, sa_ok,
	    createprops, zplprops, is_ci);
	return (error);
}

/*
 * innvl: {
 *     "type" -> dmu_objset_type_t (int32)
 *     (optional) "props" -> { prop -> value }
 * }
 *
 * outnvl: propname -> error code (int32)
 */
static int
zfs_ioc_create(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error = 0;
	zfs_creat_t zct = { 0 };
	nvlist_t *nvprops = NULL;
	void (*cbfunc)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx);
	int32_t type32;
	dmu_objset_type_t type;
	boolean_t is_insensitive = B_FALSE;

	if (nvlist_lookup_int32(innvl, "type", &type32) != 0)
		return (SET_ERROR(EINVAL));
	type = type32;
	(void) nvlist_lookup_nvlist(innvl, "props", &nvprops);

	switch (type) {
	case DMU_OST_ZFS:
		cbfunc = zfs_create_cb;
		break;

	case DMU_OST_ZVOL:
		cbfunc = zvol_create_cb;
		break;

	default:
		cbfunc = NULL;
		break;
	}
	if (strchr(fsname, '@') ||
	    strchr(fsname, '%'))
		return (SET_ERROR(EINVAL));

	zct.zct_props = nvprops;

	if (cbfunc == NULL)
		return (SET_ERROR(EINVAL));

	if (type == DMU_OST_ZVOL) {
		uint64_t volsize, volblocksize;

		if (nvprops == NULL)
			return (SET_ERROR(EINVAL));
		if (nvlist_lookup_uint64(nvprops,
		    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) != 0)
			return (SET_ERROR(EINVAL));

		if ((error = nvlist_lookup_uint64(nvprops,
		    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
		    &volblocksize)) != 0 && error != ENOENT)
			return (SET_ERROR(EINVAL));

		if (error != 0)
			volblocksize = zfs_prop_default_numeric(
			    ZFS_PROP_VOLBLOCKSIZE);

		if ((error = zvol_check_volblocksize(fsname,
		    volblocksize)) != 0 ||
		    (error = zvol_check_volsize(volsize,
		    volblocksize)) != 0)
			return (error);
	} else if (type == DMU_OST_ZFS) {
		int error;

		/*
		 * We have to have normalization and
		 * case-folding flags correct when we do the
		 * file system creation, so go figure them out
		 * now.
		 */
		VERIFY(nvlist_alloc(&zct.zct_zplprops,
		    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		error = zfs_fill_zplprops(fsname, nvprops,
		    zct.zct_zplprops, &is_insensitive);
		if (error != 0) {
			nvlist_free(zct.zct_zplprops);
			return (error);
		}
	}

	error = dmu_objset_create(fsname, type,
	    is_insensitive ? DS_FLAG_CI_DATASET : 0, cbfunc, &zct);
	nvlist_free(zct.zct_zplprops);

	/*
	 * It would be nice to do this atomically.
	 */
	if (error == 0) {
		error = zfs_set_prop_nvlist(fsname, ZPROP_SRC_LOCAL,
		    nvprops, outnvl);
		if (error != 0)
			(void) dsl_destroy_head(fsname);
	}
	return (error);
}

/*
 * innvl: {
 *     "origin" -> name of origin snapshot
 *     (optional) "props" -> { prop -> value }
 * }
 *
 * outputs:
 * outnvl: propname -> error code (int32)
 */
static int
zfs_ioc_clone(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error = 0;
	nvlist_t *nvprops = NULL;
	char *origin_name;

	if (nvlist_lookup_string(innvl, "origin", &origin_name) != 0)
		return (SET_ERROR(EINVAL));
	(void) nvlist_lookup_nvlist(innvl, "props", &nvprops);

	if (strchr(fsname, '@') ||
	    strchr(fsname, '%'))
		return (SET_ERROR(EINVAL));

	if (dataset_namecheck(origin_name, NULL, NULL) != 0)
		return (SET_ERROR(EINVAL));
	error = dmu_objset_clone(fsname, origin_name);
	if (error != 0)
		return (error);

	/*
	 * It would be nice to do this atomically.
	 */
	if (error == 0) {
		error = zfs_set_prop_nvlist(fsname, ZPROP_SRC_LOCAL,
		    nvprops, outnvl);
		if (error != 0)
			(void) dsl_destroy_head(fsname);
	}
	return (error);
}

/*
 * innvl: {
 *     "snaps" -> { snapshot1, snapshot2 }
 *     (optional) "props" -> { prop -> value (string) }
 * }
 *
 * outnvl: snapshot -> error code (int32)
 */
static int
zfs_ioc_snapshot(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	nvlist_t *snaps;
	nvlist_t *props = NULL;
	int error, poollen;
	nvpair_t *pair, *pair2;

	(void) nvlist_lookup_nvlist(innvl, "props", &props);
	if ((error = zfs_check_userprops(poolname, props)) != 0)
		return (error);

	if (!nvlist_empty(props) &&
	    zfs_earlier_version(poolname, SPA_VERSION_SNAP_PROPS))
		return (SET_ERROR(ENOTSUP));

	if (nvlist_lookup_nvlist(innvl, "snaps", &snaps) != 0)
		return (SET_ERROR(EINVAL));
	poollen = strlen(poolname);
	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		const char *name = nvpair_name(pair);
		const char *cp = strchr(name, '@');

		/*
		 * The snap name must contain an @, and the part after it must
		 * contain only valid characters.
		 */
		if (cp == NULL ||
		    zfs_component_namecheck(cp + 1, NULL, NULL) != 0)
			return (SET_ERROR(EINVAL));

		/*
		 * The snap must be in the specified pool.
		 */
		if (strncmp(name, poolname, poollen) != 0 ||
		    (name[poollen] != '/' && name[poollen] != '@'))
			return (SET_ERROR(EXDEV));

		/* This must be the only snap of this fs. */
		for (pair2 = nvlist_next_nvpair(snaps, pair);
		    pair2 != NULL; pair2 = nvlist_next_nvpair(snaps, pair2)) {
			if (strncmp(name, nvpair_name(pair2), cp - name + 1)
			    == 0) {
				return (SET_ERROR(EXDEV));
			}
		}
	}

	error = dsl_dataset_snapshot(snaps, props, outnvl);

	return (error);
}

/*
 * innvl: "message" -> string
 */
/* ARGSUSED */
static int
zfs_ioc_log_history(const char *unused, nvlist_t *innvl, nvlist_t *outnvl)
{
	char *message;
	spa_t *spa;
	int error;
	char *poolname;

	/*
	 * The poolname in the ioctl is not set, we get it from the TSD,
	 * which was set at the end of the last successful ioctl that allows
	 * logging.  The secpolicy func already checked that it is set.
	 * Only one log ioctl is allowed after each successful ioctl, so
	 * we clear the TSD here.
	 */
	poolname = tsd_get(zfs_allow_log_key);
	(void) tsd_set(zfs_allow_log_key, NULL);
	error = spa_open(poolname, &spa, FTAG);
	strfree(poolname);
	if (error != 0)
		return (error);

	if (nvlist_lookup_string(innvl, "message", &message) != 0)  {
		spa_close(spa, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY) {
		spa_close(spa, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	error = spa_history_log(spa, message);
	spa_close(spa, FTAG);
	return (error);
}

/*
 * The dp_config_rwlock must not be held when calling this, because the
 * unmount may need to write out data.
 *
 * This function is best-effort.  Callers must deal gracefully if it
 * remains mounted (or is remounted after this call).
 *
 * Returns 0 if the argument is not a snapshot, or it is not currently a
 * filesystem, or we were able to unmount it.  Returns error code otherwise.
 */
int
zfs_unmount_snap(const char *snapname)
{
	int err;

	if (strchr(snapname, '@') == NULL)
		return (0);

	err = zfsctl_snapshot_unmount((char *)snapname, MNT_FORCE);
	if (err != 0 && err != ENOENT)
		return (SET_ERROR(err));

	return (0);
}

/* ARGSUSED */
static int
zfs_unmount_snap_cb(const char *snapname, void *arg)
{
	return (zfs_unmount_snap(snapname));
}

/*
 * When a clone is destroyed, its origin may also need to be destroyed,
 * in which case it must be unmounted.  This routine will do that unmount
 * if necessary.
 */
void
zfs_destroy_unmount_origin(const char *fsname)
{
	int error;
	objset_t *os;
	dsl_dataset_t *ds;

	error = dmu_objset_hold(fsname, FTAG, &os);
	if (error != 0)
		return;
	ds = dmu_objset_ds(os);
	if (dsl_dir_is_clone(ds->ds_dir) && DS_IS_DEFER_DESTROY(ds->ds_prev)) {
		char originname[MAXNAMELEN];
		dsl_dataset_name(ds->ds_prev, originname);
		dmu_objset_rele(os, FTAG);
		(void) zfs_unmount_snap(originname);
	} else {
		dmu_objset_rele(os, FTAG);
	}
}

/*
 * innvl: {
 *     "snaps" -> { snapshot1, snapshot2 }
 *     (optional boolean) "defer"
 * }
 *
 * outnvl: snapshot -> error code (int32)
 */
/* ARGSUSED */
static int
zfs_ioc_destroy_snaps(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	nvlist_t *snaps;
	nvpair_t *pair;
	boolean_t defer;

	if (nvlist_lookup_nvlist(innvl, "snaps", &snaps) != 0)
		return (SET_ERROR(EINVAL));
	defer = nvlist_exists(innvl, "defer");

	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		(void) zfs_unmount_snap(nvpair_name(pair));
	}

	return (dsl_destroy_snapshots_nvl(snaps, defer, outnvl));
}

/*
 * Create bookmarks.  Bookmark names are of the form <fs>#<bmark>.
 * All bookmarks must be in the same pool.
 *
 * innvl: {
 *     bookmark1 -> snapshot1, bookmark2 -> snapshot2
 * }
 *
 * outnvl: bookmark -> error code (int32)
 *
 */
/* ARGSUSED */
static int
zfs_ioc_bookmark(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	nvpair_t *pair, *pair2;

	for (pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		char *snap_name;

		/*
		 * Verify the snapshot argument.
		 */
		if (nvpair_value_string(pair, &snap_name) != 0)
			return (SET_ERROR(EINVAL));


		/* Verify that the keys (bookmarks) are unique */
		for (pair2 = nvlist_next_nvpair(innvl, pair);
		    pair2 != NULL; pair2 = nvlist_next_nvpair(innvl, pair2)) {
			if (strcmp(nvpair_name(pair), nvpair_name(pair2)) == 0)
				return (SET_ERROR(EINVAL));
		}
	}

	return (dsl_bookmark_create(innvl, outnvl));
}

/*
 * innvl: {
 *     property 1, property 2, ...
 * }
 *
 * outnvl: {
 *     bookmark name 1 -> { property 1, property 2, ... },
 *     bookmark name 2 -> { property 1, property 2, ... }
 * }
 *
 */
static int
zfs_ioc_get_bookmarks(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	return (dsl_get_bookmarks(fsname, innvl, outnvl));
}

/*
 * innvl: {
 *     bookmark name 1, bookmark name 2
 * }
 *
 * outnvl: bookmark -> error code (int32)
 *
 */
static int
zfs_ioc_destroy_bookmarks(const char *poolname, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	int error, poollen;
	nvpair_t *pair;

	poollen = strlen(poolname);
	for (pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		const char *name = nvpair_name(pair);
		const char *cp = strchr(name, '#');

		/*
		 * The bookmark name must contain an #, and the part after it
		 * must contain only valid characters.
		 */
		if (cp == NULL ||
		    zfs_component_namecheck(cp + 1, NULL, NULL) != 0)
			return (SET_ERROR(EINVAL));

		/*
		 * The bookmark must be in the specified pool.
		 */
		if (strncmp(name, poolname, poollen) != 0 ||
		    (name[poollen] != '/' && name[poollen] != '#'))
			return (SET_ERROR(EXDEV));
	}

	error = dsl_bookmark_destroy(innvl, outnvl);
	return (error);
}

/*
 * inputs:
 * zc_name		name of dataset to destroy
 * zc_objset_type	type of objset
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:		none
 */
static int
zfs_ioc_destroy(zfs_cmd_t *zc)
{
	int err;

	if (zc->zc_objset_type == DMU_OST_ZFS) {
		err = zfs_unmount_snap(zc->zc_name);
		if (err != 0)
			return (err);
	}

	if (strchr(zc->zc_name, '@'))
		err = dsl_destroy_snapshot(zc->zc_name, zc->zc_defer_destroy);
	else
		err = dsl_destroy_head(zc->zc_name);

	return (err);
}

/*
 * fsname is name of dataset to rollback (to most recent snapshot)
 *
 * innvl is not used.
 *
 * outnvl: "target" -> name of most recent snapshot
 * }
 */
/* ARGSUSED */
static int
zfs_ioc_rollback(const char *fsname, nvlist_t *args, nvlist_t *outnvl)
{
	zfs_sb_t *zsb;
	int error;

	if (get_zfs_sb(fsname, &zsb) == 0) {
		error = zfs_suspend_fs(zsb);
		if (error == 0) {
			int resume_err;

			error = dsl_dataset_rollback(fsname, zsb, outnvl);
			resume_err = zfs_resume_fs(zsb, fsname);
			error = error ? error : resume_err;
		}
		deactivate_super(zsb->z_sb);
	} else {
		error = dsl_dataset_rollback(fsname, NULL, outnvl);
	}
	return (error);
}

static int
recursive_unmount(const char *fsname, void *arg)
{
	const char *snapname = arg;
	char *fullname;
	int error;

	fullname = kmem_asprintf("%s@%s", fsname, snapname);
	error = zfs_unmount_snap(fullname);
	strfree(fullname);

	return (error);
}

/*
 * inputs:
 * zc_name	old name of dataset
 * zc_value	new name of dataset
 * zc_cookie	recursive flag (only valid for snapshots)
 *
 * outputs:	none
 */
static int
zfs_ioc_rename(zfs_cmd_t *zc)
{
	boolean_t recursive = zc->zc_cookie & 1;
	char *at;

	zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '%'))
		return (SET_ERROR(EINVAL));

	at = strchr(zc->zc_name, '@');
	if (at != NULL) {
		/* snaps must be in same fs */
		int error;

		if (strncmp(zc->zc_name, zc->zc_value, at - zc->zc_name + 1))
			return (SET_ERROR(EXDEV));
		*at = '\0';
		if (zc->zc_objset_type == DMU_OST_ZFS) {
			error = dmu_objset_find(zc->zc_name,
			    recursive_unmount, at + 1,
			    recursive ? DS_FIND_CHILDREN : 0);
			if (error != 0) {
				*at = '@';
				return (error);
			}
		}
		error = dsl_dataset_rename_snapshot(zc->zc_name,
		    at + 1, strchr(zc->zc_value, '@') + 1, recursive);
		*at = '@';

		return (error);
	} else {
		return (dsl_dir_rename(zc->zc_name, zc->zc_value));
	}
}

static int
zfs_check_settable(const char *dsname, nvpair_t *pair, cred_t *cr)
{
	const char *propname = nvpair_name(pair);
	boolean_t issnap = (strchr(dsname, '@') != NULL);
	zfs_prop_t prop = zfs_name_to_prop(propname);
	uint64_t intval;
	int err;

	if (prop == ZPROP_INVAL) {
		if (zfs_prop_user(propname)) {
			if ((err = zfs_secpolicy_write_perms(dsname,
			    ZFS_DELEG_PERM_USERPROP, cr)))
				return (err);
			return (0);
		}

		if (!issnap && zfs_prop_userquota(propname)) {
			const char *perm = NULL;
			const char *uq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA];
			const char *gq_prefix =
			    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA];

			if (strncmp(propname, uq_prefix,
			    strlen(uq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_USERQUOTA;
			} else if (strncmp(propname, gq_prefix,
			    strlen(gq_prefix)) == 0) {
				perm = ZFS_DELEG_PERM_GROUPQUOTA;
			} else {
				/* USERUSED and GROUPUSED are read-only */
				return (SET_ERROR(EINVAL));
			}

			if ((err = zfs_secpolicy_write_perms(dsname, perm, cr)))
				return (err);
			return (0);
		}

		return (SET_ERROR(EINVAL));
	}

	if (issnap)
		return (SET_ERROR(EINVAL));

	if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
		/*
		 * dsl_prop_get_all_impl() returns properties in this
		 * format.
		 */
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(pair, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &pair) == 0);
	}

	/*
	 * Check that this value is valid for this pool version
	 */
	switch (prop) {
	case ZFS_PROP_COMPRESSION:
		/*
		 * If the user specified gzip compression, make sure
		 * the SPA supports it. We ignore any errors here since
		 * we'll catch them later.
		 */
		if (nvpair_value_uint64(pair, &intval) == 0) {
			if (intval >= ZIO_COMPRESS_GZIP_1 &&
			    intval <= ZIO_COMPRESS_GZIP_9 &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_GZIP_COMPRESSION)) {
				return (SET_ERROR(ENOTSUP));
			}

			if (intval == ZIO_COMPRESS_ZLE &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_ZLE_COMPRESSION))
				return (SET_ERROR(ENOTSUP));

			if (intval == ZIO_COMPRESS_LZ4) {
				spa_t *spa;

				if ((err = spa_open(dsname, &spa, FTAG)) != 0)
					return (err);

				if (!spa_feature_is_enabled(spa,
				    SPA_FEATURE_LZ4_COMPRESS)) {
					spa_close(spa, FTAG);
					return (SET_ERROR(ENOTSUP));
				}
				spa_close(spa, FTAG);
			}

			/*
			 * If this is a bootable dataset then
			 * verify that the compression algorithm
			 * is supported for booting. We must return
			 * something other than ENOTSUP since it
			 * implies a downrev pool version.
			 */
			if (zfs_is_bootfs(dsname) &&
			    !BOOTFS_COMPRESS_VALID(intval)) {
				return (SET_ERROR(ERANGE));
			}
		}
		break;

	case ZFS_PROP_COPIES:
		if (zfs_earlier_version(dsname, SPA_VERSION_DITTO_BLOCKS))
			return (SET_ERROR(ENOTSUP));
		break;

	case ZFS_PROP_DEDUP:
		if (zfs_earlier_version(dsname, SPA_VERSION_DEDUP))
			return (SET_ERROR(ENOTSUP));
		break;

	case ZFS_PROP_VOLBLOCKSIZE:
	case ZFS_PROP_RECORDSIZE:
		/* Record sizes above 128k need the feature to be enabled */
		if (nvpair_value_uint64(pair, &intval) == 0 &&
		    intval > SPA_OLD_MAXBLOCKSIZE) {
			spa_t *spa;

			/*
			 * If this is a bootable dataset then
			 * the we don't allow large (>128K) blocks,
			 * because GRUB doesn't support them.
			 */
			if (zfs_is_bootfs(dsname) &&
			    intval > SPA_OLD_MAXBLOCKSIZE) {
				return (SET_ERROR(EDOM));
			}

			/*
			 * We don't allow setting the property above 1MB,
			 * unless the tunable has been changed.
			 */
			if (intval > zfs_max_recordsize ||
			    intval > SPA_MAXBLOCKSIZE)
				return (SET_ERROR(EDOM));

			if ((err = spa_open(dsname, &spa, FTAG)) != 0)
				return (err);

			if (!spa_feature_is_enabled(spa,
			    SPA_FEATURE_LARGE_BLOCKS)) {
				spa_close(spa, FTAG);
				return (SET_ERROR(ENOTSUP));
			}
			spa_close(spa, FTAG);
		}
		break;

	case ZFS_PROP_SHARESMB:
		if (zpl_earlier_version(dsname, ZPL_VERSION_FUID))
			return (SET_ERROR(ENOTSUP));
		break;

	case ZFS_PROP_ACLINHERIT:
		if (nvpair_type(pair) == DATA_TYPE_UINT64 &&
		    nvpair_value_uint64(pair, &intval) == 0) {
			if (intval == ZFS_ACL_PASSTHROUGH_X &&
			    zfs_earlier_version(dsname,
			    SPA_VERSION_PASSTHROUGH_X))
				return (SET_ERROR(ENOTSUP));
		}
		break;
	default:
		break;
	}

	return (zfs_secpolicy_setprop(dsname, prop, pair, CRED()));
}

/*
 * Removes properties from the given props list that fail permission checks
 * needed to clear them and to restore them in case of a receive error. For each
 * property, make sure we have both set and inherit permissions.
 *
 * Returns the first error encountered if any permission checks fail. If the
 * caller provides a non-NULL errlist, it also gives the complete list of names
 * of all the properties that failed a permission check along with the
 * corresponding error numbers. The caller is responsible for freeing the
 * returned errlist.
 *
 * If every property checks out successfully, zero is returned and the list
 * pointed at by errlist is NULL.
 */
static int
zfs_check_clearable(char *dataset, nvlist_t *props, nvlist_t **errlist)
{
	zfs_cmd_t *zc;
	nvpair_t *pair, *next_pair;
	nvlist_t *errors;
	int err, rv = 0;

	if (props == NULL)
		return (0);

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	zc = kmem_alloc(sizeof (zfs_cmd_t), KM_SLEEP);
	(void) strcpy(zc->zc_name, dataset);
	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		next_pair = nvlist_next_nvpair(props, pair);

		(void) strcpy(zc->zc_value, nvpair_name(pair));
		if ((err = zfs_check_settable(dataset, pair, CRED())) != 0 ||
		    (err = zfs_secpolicy_inherit_prop(zc, NULL, CRED())) != 0) {
			VERIFY(nvlist_remove_nvpair(props, pair) == 0);
			VERIFY(nvlist_add_int32(errors,
			    zc->zc_value, err) == 0);
		}
		pair = next_pair;
	}
	kmem_free(zc, sizeof (zfs_cmd_t));

	if ((pair = nvlist_next_nvpair(errors, NULL)) == NULL) {
		nvlist_free(errors);
		errors = NULL;
	} else {
		VERIFY(nvpair_value_int32(pair, &rv) == 0);
	}

	if (errlist == NULL)
		nvlist_free(errors);
	else
		*errlist = errors;

	return (rv);
}

static boolean_t
propval_equals(nvpair_t *p1, nvpair_t *p2)
{
	if (nvpair_type(p1) == DATA_TYPE_NVLIST) {
		/* dsl_prop_get_all_impl() format */
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(p1, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &p1) == 0);
	}

	if (nvpair_type(p2) == DATA_TYPE_NVLIST) {
		nvlist_t *attrs;
		VERIFY(nvpair_value_nvlist(p2, &attrs) == 0);
		VERIFY(nvlist_lookup_nvpair(attrs, ZPROP_VALUE,
		    &p2) == 0);
	}

	if (nvpair_type(p1) != nvpair_type(p2))
		return (B_FALSE);

	if (nvpair_type(p1) == DATA_TYPE_STRING) {
		char *valstr1, *valstr2;

		VERIFY(nvpair_value_string(p1, (char **)&valstr1) == 0);
		VERIFY(nvpair_value_string(p2, (char **)&valstr2) == 0);
		return (strcmp(valstr1, valstr2) == 0);
	} else {
		uint64_t intval1, intval2;

		VERIFY(nvpair_value_uint64(p1, &intval1) == 0);
		VERIFY(nvpair_value_uint64(p2, &intval2) == 0);
		return (intval1 == intval2);
	}
}

/*
 * Remove properties from props if they are not going to change (as determined
 * by comparison with origprops). Remove them from origprops as well, since we
 * do not need to clear or restore properties that won't change.
 */
static void
props_reduce(nvlist_t *props, nvlist_t *origprops)
{
	nvpair_t *pair, *next_pair;

	if (origprops == NULL)
		return; /* all props need to be received */

	pair = nvlist_next_nvpair(props, NULL);
	while (pair != NULL) {
		const char *propname = nvpair_name(pair);
		nvpair_t *match;

		next_pair = nvlist_next_nvpair(props, pair);

		if ((nvlist_lookup_nvpair(origprops, propname,
		    &match) != 0) || !propval_equals(pair, match))
			goto next; /* need to set received value */

		/* don't clear the existing received value */
		(void) nvlist_remove_nvpair(origprops, match);
		/* don't bother receiving the property */
		(void) nvlist_remove_nvpair(props, pair);
next:
		pair = next_pair;
	}
}

#ifdef	DEBUG
static boolean_t zfs_ioc_recv_inject_err;
#endif

/*
 * inputs:
 * zc_name		name of containing filesystem
 * zc_nvlist_src{_size}	nvlist of properties to apply
 * zc_value		name of snapshot to create
 * zc_string		name of clone origin (if DRR_FLAG_CLONE)
 * zc_cookie		file descriptor to recv from
 * zc_begin_record	the BEGIN record of the stream (not byteswapped)
 * zc_guid		force flag
 * zc_cleanup_fd	cleanup-on-exit file descriptor
 * zc_action_handle	handle for this guid/ds mapping (or zero on first call)
 *
 * outputs:
 * zc_cookie		number of bytes read
 * zc_nvlist_dst{_size} error for each unapplied received property
 * zc_obj		zprop_errflags_t
 * zc_action_handle	handle for this guid/ds mapping
 */
static int
zfs_ioc_recv(zfs_cmd_t *zc)
{
	file_t *fp;
	dmu_recv_cookie_t drc;
	boolean_t force = (boolean_t)zc->zc_guid;
	int fd;
	int error = 0;
	int props_error = 0;
	nvlist_t *errors;
	offset_t off;
	nvlist_t *props = NULL; /* sent properties */
	nvlist_t *origprops = NULL; /* existing properties */
	char *origin = NULL;
	char *tosnap;
	char tofs[ZFS_MAXNAMELEN];
	boolean_t first_recvd_props = B_FALSE;

	if (dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_value, '@') == NULL ||
	    strchr(zc->zc_value, '%'))
		return (SET_ERROR(EINVAL));

	(void) strcpy(tofs, zc->zc_value);
	tosnap = strchr(tofs, '@');
	*tosnap++ = '\0';

	if (zc->zc_nvlist_src != 0 &&
	    (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props)) != 0)
		return (error);

	fd = zc->zc_cookie;
	fp = getf(fd);
	if (fp == NULL) {
		nvlist_free(props);
		return (SET_ERROR(EBADF));
	}

	VERIFY(nvlist_alloc(&errors, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if (zc->zc_string[0])
		origin = zc->zc_string;

	error = dmu_recv_begin(tofs, tosnap,
	    &zc->zc_begin_record, force, origin, &drc);
	if (error != 0)
		goto out;

	/*
	 * Set properties before we receive the stream so that they are applied
	 * to the new data. Note that we must call dmu_recv_stream() if
	 * dmu_recv_begin() succeeds.
	 */
	if (props != NULL && !drc.drc_newfs) {
		if (spa_version(dsl_dataset_get_spa(drc.drc_ds)) >=
		    SPA_VERSION_RECVD_PROPS &&
		    !dsl_prop_get_hasrecvd(tofs))
			first_recvd_props = B_TRUE;

		/*
		 * If new received properties are supplied, they are to
		 * completely replace the existing received properties, so stash
		 * away the existing ones.
		 */
		if (dsl_prop_get_received(tofs, &origprops) == 0) {
			nvlist_t *errlist = NULL;
			/*
			 * Don't bother writing a property if its value won't
			 * change (and avoid the unnecessary security checks).
			 *
			 * The first receive after SPA_VERSION_RECVD_PROPS is a
			 * special case where we blow away all local properties
			 * regardless.
			 */
			if (!first_recvd_props)
				props_reduce(props, origprops);
			if (zfs_check_clearable(tofs, origprops, &errlist) != 0)
				(void) nvlist_merge(errors, errlist, 0);
			nvlist_free(errlist);

			if (clear_received_props(tofs, origprops,
			    first_recvd_props ? NULL : props) != 0)
				zc->zc_obj |= ZPROP_ERR_NOCLEAR;
		} else {
			zc->zc_obj |= ZPROP_ERR_NOCLEAR;
		}
	}

	if (props != NULL) {
		props_error = dsl_prop_set_hasrecvd(tofs);

		if (props_error == 0) {
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
			    props, errors);
		}
	}

	if (zc->zc_nvlist_dst_size != 0 &&
	    (nvlist_smush(errors, zc->zc_nvlist_dst_size) != 0 ||
	    put_nvlist(zc, errors) != 0)) {
		/*
		 * Caller made zc->zc_nvlist_dst less than the minimum expected
		 * size or supplied an invalid address.
		 */
		props_error = SET_ERROR(EINVAL);
	}

	off = fp->f_offset;
	error = dmu_recv_stream(&drc, fp->f_vnode, &off, zc->zc_cleanup_fd,
	    &zc->zc_action_handle);

	if (error == 0) {
		zfs_sb_t *zsb = NULL;

		if (get_zfs_sb(tofs, &zsb) == 0) {
			/* online recv */
			int end_err;

			error = zfs_suspend_fs(zsb);
			/*
			 * If the suspend fails, then the recv_end will
			 * likely also fail, and clean up after itself.
			 */
			end_err = dmu_recv_end(&drc, zsb);
			if (error == 0)
				error = zfs_resume_fs(zsb, tofs);
			error = error ? error : end_err;
			deactivate_super(zsb->z_sb);
		} else {
			error = dmu_recv_end(&drc, NULL);
		}
	}

	zc->zc_cookie = off - fp->f_offset;
	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;

#ifdef	DEBUG
	if (zfs_ioc_recv_inject_err) {
		zfs_ioc_recv_inject_err = B_FALSE;
		error = 1;
	}
#endif

	/*
	 * On error, restore the original props.
	 */
	if (error != 0 && props != NULL && !drc.drc_newfs) {
		if (clear_received_props(tofs, props, NULL) != 0) {
			/*
			 * We failed to clear the received properties.
			 * Since we may have left a $recvd value on the
			 * system, we can't clear the $hasrecvd flag.
			 */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		} else if (first_recvd_props) {
			dsl_prop_unset_hasrecvd(tofs);
		}

		if (origprops == NULL && !drc.drc_newfs) {
			/* We failed to stash the original properties. */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		}

		/*
		 * dsl_props_set() will not convert RECEIVED to LOCAL on or
		 * after SPA_VERSION_RECVD_PROPS, so we need to specify LOCAL
		 * explictly if we're restoring local properties cleared in the
		 * first new-style receive.
		 */
		if (origprops != NULL &&
		    zfs_set_prop_nvlist(tofs, (first_recvd_props ?
		    ZPROP_SRC_LOCAL : ZPROP_SRC_RECEIVED),
		    origprops, NULL) != 0) {
			/*
			 * We stashed the original properties but failed to
			 * restore them.
			 */
			zc->zc_obj |= ZPROP_ERR_NORESTORE;
		}
	}
out:
	nvlist_free(props);
	nvlist_free(origprops);
	nvlist_free(errors);
	releasef(fd);

	if (error == 0)
		error = props_error;

	return (error);
}

/*
 * inputs:
 * zc_name	name of snapshot to send
 * zc_cookie	file descriptor to send stream to
 * zc_obj	fromorigin flag (mutually exclusive with zc_fromobj)
 * zc_sendobj	objsetid of snapshot to send
 * zc_fromobj	objsetid of incremental fromsnap (may be zero)
 * zc_guid	if set, estimate size of stream only.  zc_cookie is ignored.
 *		output size in zc_objset_type.
 * zc_flags	lzc_send_flags
 *
 * outputs:
 * zc_objset_type	estimated size, if zc_guid is set
 */
static int
zfs_ioc_send(zfs_cmd_t *zc)
{
	int error;
	offset_t off;
	boolean_t estimate = (zc->zc_guid != 0);
	boolean_t embedok = (zc->zc_flags & 0x1);
	boolean_t large_block_ok = (zc->zc_flags & 0x2);

	if (zc->zc_obj != 0) {
		dsl_pool_t *dp;
		dsl_dataset_t *tosnap;

		error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
		if (error != 0)
			return (error);

		error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &tosnap);
		if (error != 0) {
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		if (dsl_dir_is_clone(tosnap->ds_dir))
			zc->zc_fromobj =
			    dsl_dir_phys(tosnap->ds_dir)->dd_origin_obj;
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
	}

	if (estimate) {
		dsl_pool_t *dp;
		dsl_dataset_t *tosnap;
		dsl_dataset_t *fromsnap = NULL;

		error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
		if (error != 0)
			return (error);

		error = dsl_dataset_hold_obj(dp, zc->zc_sendobj, FTAG, &tosnap);
		if (error != 0) {
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		if (zc->zc_fromobj != 0) {
			error = dsl_dataset_hold_obj(dp, zc->zc_fromobj,
			    FTAG, &fromsnap);
			if (error != 0) {
				dsl_dataset_rele(tosnap, FTAG);
				dsl_pool_rele(dp, FTAG);
				return (error);
			}
		}

		error = dmu_send_estimate(tosnap, fromsnap,
		    &zc->zc_objset_type);

		if (fromsnap != NULL)
			dsl_dataset_rele(fromsnap, FTAG);
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
	} else {
		file_t *fp = getf(zc->zc_cookie);
		if (fp == NULL)
			return (SET_ERROR(EBADF));

		off = fp->f_offset;
		error = dmu_send_obj(zc->zc_name, zc->zc_sendobj,
		    zc->zc_fromobj, embedok, large_block_ok,
		    zc->zc_cookie, fp->f_vnode, &off);

		if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
			fp->f_offset = off;
		releasef(zc->zc_cookie);
	}
	return (error);
}

/*
 * inputs:
 * zc_name	name of snapshot on which to report progress
 * zc_cookie	file descriptor of send stream
 *
 * outputs:
 * zc_cookie	number of bytes written in send stream thus far
 */
static int
zfs_ioc_send_progress(zfs_cmd_t *zc)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	dmu_sendarg_t *dsp = NULL;
	int error;

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	mutex_enter(&ds->ds_sendstream_lock);

	/*
	 * Iterate over all the send streams currently active on this dataset.
	 * If there's one which matches the specified file descriptor _and_ the
	 * stream was started by the current process, return the progress of
	 * that stream.
	 */

	for (dsp = list_head(&ds->ds_sendstreams); dsp != NULL;
	    dsp = list_next(&ds->ds_sendstreams, dsp)) {
		if (dsp->dsa_outfd == zc->zc_cookie &&
		    dsp->dsa_proc->group_leader == curproc->group_leader)
			break;
	}

	if (dsp != NULL)
		zc->zc_cookie = *(dsp->dsa_off);
	else
		error = SET_ERROR(ENOENT);

	mutex_exit(&ds->ds_sendstream_lock);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (error);
}

static int
zfs_ioc_inject_fault(zfs_cmd_t *zc)
{
	int id, error;

	error = zio_inject_fault(zc->zc_name, (int)zc->zc_guid, &id,
	    &zc->zc_inject_record);

	if (error == 0)
		zc->zc_guid = (uint64_t)id;

	return (error);
}

static int
zfs_ioc_clear_fault(zfs_cmd_t *zc)
{
	return (zio_clear_fault((int)zc->zc_guid));
}

static int
zfs_ioc_inject_list_next(zfs_cmd_t *zc)
{
	int id = (int)zc->zc_guid;
	int error;

	error = zio_inject_list_next(&id, zc->zc_name, sizeof (zc->zc_name),
	    &zc->zc_inject_record);

	zc->zc_guid = id;

	return (error);
}

static int
zfs_ioc_error_log(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;
	size_t count = (size_t)zc->zc_nvlist_dst_size;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	error = spa_get_errlog(spa, (void *)(uintptr_t)zc->zc_nvlist_dst,
	    &count);
	if (error == 0)
		zc->zc_nvlist_dst_size = count;
	else
		zc->zc_nvlist_dst_size = spa_get_errlog_size(spa);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_clear(zfs_cmd_t *zc)
{
	spa_t *spa;
	vdev_t *vd;
	int error;

	/*
	 * On zpool clear we also fix up missing slogs
	 */
	mutex_enter(&spa_namespace_lock);
	spa = spa_lookup(zc->zc_name);
	if (spa == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EIO));
	}
	if (spa_get_log_state(spa) == SPA_LOG_MISSING) {
		/* we need to let spa_open/spa_load clear the chains */
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	}
	spa->spa_last_open_failed = 0;
	mutex_exit(&spa_namespace_lock);

	if (zc->zc_cookie & ZPOOL_NO_REWIND) {
		error = spa_open(zc->zc_name, &spa, FTAG);
	} else {
		nvlist_t *policy;
		nvlist_t *config = NULL;

		if (zc->zc_nvlist_src == 0)
			return (SET_ERROR(EINVAL));

		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &policy)) == 0) {
			error = spa_open_rewind(zc->zc_name, &spa, FTAG,
			    policy, &config);
			if (config != NULL) {
				int err;

				if ((err = put_nvlist(zc, config)) != 0)
					error = err;
				nvlist_free(config);
			}
			nvlist_free(policy);
		}
	}

	if (error != 0)
		return (error);

	spa_vdev_state_enter(spa, SCL_NONE);

	if (zc->zc_guid == 0) {
		vd = NULL;
	} else {
		vd = spa_lookup_by_guid(spa, zc->zc_guid, B_TRUE);
		if (vd == NULL) {
			(void) spa_vdev_state_exit(spa, NULL, ENODEV);
			spa_close(spa, FTAG);
			return (SET_ERROR(ENODEV));
		}
	}

	vdev_clear(spa, vd);

	(void) spa_vdev_state_exit(spa, NULL, 0);

	/*
	 * Resume any suspended I/Os.
	 */
	if (zio_resume(spa) != 0)
		error = SET_ERROR(EIO);

	spa_close(spa, FTAG);

	return (error);
}

static int
zfs_ioc_pool_reopen(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	spa_vdev_state_enter(spa, SCL_NONE);

	/*
	 * If a resilver is already in progress then set the
	 * spa_scrub_reopen flag to B_TRUE so that we don't restart
	 * the scan as a side effect of the reopen. Otherwise, let
	 * vdev_open() decided if a resilver is required.
	 */
	spa->spa_scrub_reopen = dsl_scan_resilvering(spa->spa_dsl_pool);
	vdev_reopen(spa->spa_root_vdev);
	spa->spa_scrub_reopen = B_FALSE;

	(void) spa_vdev_state_exit(spa, NULL, 0);
	spa_close(spa, FTAG);
	return (0);
}
/*
 * inputs:
 * zc_name	name of filesystem
 * zc_value	name of origin snapshot
 *
 * outputs:
 * zc_string	name of conflicting snapshot, if there is one
 */
static int
zfs_ioc_promote(zfs_cmd_t *zc)
{
	char *cp;

	/*
	 * We don't need to unmount *all* the origin fs's snapshots, but
	 * it's easier.
	 */
	cp = strchr(zc->zc_value, '@');
	if (cp)
		*cp = '\0';
	(void) dmu_objset_find(zc->zc_value,
	    zfs_unmount_snap_cb, NULL, DS_FIND_SNAPSHOTS);
	return (dsl_dataset_promote(zc->zc_name, zc->zc_string));
}

/*
 * Retrieve a single {user|group}{used|quota}@... property.
 *
 * inputs:
 * zc_name	name of filesystem
 * zc_objset_type zfs_userquota_prop_t
 * zc_value	domain name (eg. "S-1-234-567-89")
 * zc_guid	RID/UID/GID
 *
 * outputs:
 * zc_cookie	property value
 */
static int
zfs_ioc_userspace_one(zfs_cmd_t *zc)
{
	zfs_sb_t *zsb;
	int error;

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (SET_ERROR(EINVAL));

	error = zfs_sb_hold(zc->zc_name, FTAG, &zsb, B_FALSE);
	if (error != 0)
		return (error);

	error = zfs_userspace_one(zsb,
	    zc->zc_objset_type, zc->zc_value, zc->zc_guid, &zc->zc_cookie);
	zfs_sb_rele(zsb, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_cookie		zap cursor
 * zc_objset_type	zfs_userquota_prop_t
 * zc_nvlist_dst[_size] buffer to fill (not really an nvlist)
 *
 * outputs:
 * zc_nvlist_dst[_size]	data buffer (array of zfs_useracct_t)
 * zc_cookie	zap cursor
 */
static int
zfs_ioc_userspace_many(zfs_cmd_t *zc)
{
	zfs_sb_t *zsb;
	int bufsize = zc->zc_nvlist_dst_size;
	int error;
	void *buf;

	if (bufsize <= 0)
		return (SET_ERROR(ENOMEM));

	error = zfs_sb_hold(zc->zc_name, FTAG, &zsb, B_FALSE);
	if (error != 0)
		return (error);

	buf = vmem_alloc(bufsize, KM_SLEEP);

	error = zfs_userspace_many(zsb, zc->zc_objset_type, &zc->zc_cookie,
	    buf, &zc->zc_nvlist_dst_size);

	if (error == 0) {
		error = xcopyout(buf,
		    (void *)(uintptr_t)zc->zc_nvlist_dst,
		    zc->zc_nvlist_dst_size);
	}
	vmem_free(buf, bufsize);
	zfs_sb_rele(zsb, FTAG);

	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 *
 * outputs:
 * none
 */
static int
zfs_ioc_userspace_upgrade(zfs_cmd_t *zc)
{
	objset_t *os;
	int error = 0;
	zfs_sb_t *zsb;

	if (get_zfs_sb(zc->zc_name, &zsb) == 0) {
		if (!dmu_objset_userused_enabled(zsb->z_os)) {
			/*
			 * If userused is not enabled, it may be because the
			 * objset needs to be closed & reopened (to grow the
			 * objset_phys_t).  Suspend/resume the fs will do that.
			 */
			error = zfs_suspend_fs(zsb);
			if (error == 0) {
				dmu_objset_refresh_ownership(zsb->z_os,
				    zsb);
				error = zfs_resume_fs(zsb, zc->zc_name);
			}
		}
		if (error == 0)
			error = dmu_objset_userspace_upgrade(zsb->z_os);
		deactivate_super(zsb->z_sb);
	} else {
		/* XXX kind of reading contents without owning */
		error = dmu_objset_hold(zc->zc_name, FTAG, &os);
		if (error != 0)
			return (error);

		error = dmu_objset_userspace_upgrade(os);
		dmu_objset_rele(os, FTAG);
	}

	return (error);
}

static int
zfs_ioc_share(zfs_cmd_t *zc)
{
	return (SET_ERROR(ENOSYS));
}

ace_t full_access[] = {
	{(uid_t)-1, ACE_ALL_PERMS, ACE_EVERYONE, 0}
};

/*
 * inputs:
 * zc_name		name of containing filesystem
 * zc_obj		object # beyond which we want next in-use object #
 *
 * outputs:
 * zc_obj		next in-use object #
 */
static int
zfs_ioc_next_obj(zfs_cmd_t *zc)
{
	objset_t *os = NULL;
	int error;

	error = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (error != 0)
		return (error);

	error = dmu_object_next(os, &zc->zc_obj, B_FALSE, 0);

	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * inputs:
 * zc_name		name of filesystem
 * zc_value		prefix name for snapshot
 * zc_cleanup_fd	cleanup-on-exit file descriptor for calling process
 *
 * outputs:
 * zc_value		short name of new snapshot
 */
static int
zfs_ioc_tmp_snapshot(zfs_cmd_t *zc)
{
	char *snap_name;
	char *hold_name;
	int error;
	minor_t minor;

	error = zfs_onexit_fd_hold(zc->zc_cleanup_fd, &minor);
	if (error != 0)
		return (error);

	snap_name = kmem_asprintf("%s-%016llx", zc->zc_value,
	    (u_longlong_t)ddi_get_lbolt64());
	hold_name = kmem_asprintf("%%%s", zc->zc_value);

	error = dsl_dataset_snapshot_tmp(zc->zc_name, snap_name, minor,
	    hold_name);
	if (error == 0)
		(void) strcpy(zc->zc_value, snap_name);
	strfree(snap_name);
	strfree(hold_name);
	zfs_onexit_fd_rele(zc->zc_cleanup_fd);
	return (error);
}

/*
 * inputs:
 * zc_name		name of "to" snapshot
 * zc_value		name of "from" snapshot
 * zc_cookie		file descriptor to write diff data on
 *
 * outputs:
 * dmu_diff_record_t's to the file descriptor
 */
static int
zfs_ioc_diff(zfs_cmd_t *zc)
{
	file_t *fp;
	offset_t off;
	int error;

	fp = getf(zc->zc_cookie);
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	off = fp->f_offset;

	error = dmu_diff(zc->zc_name, zc->zc_value, fp->f_vnode, &off);

	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;
	releasef(zc->zc_cookie);

	return (error);
}

/*
 * Remove all ACL files in shares dir
 */
#ifdef HAVE_SMB_SHARE
static int
zfs_smb_acl_purge(znode_t *dzp)
{
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	zfs_sb_t *zsb = ZTOZSB(dzp);
	int error;

	for (zap_cursor_init(&zc, zsb->z_os, dzp->z_id);
	    (error = zap_cursor_retrieve(&zc, &zap)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((error = VOP_REMOVE(ZTOV(dzp), zap.za_name, kcred,
		    NULL, 0)) != 0)
			break;
	}
	zap_cursor_fini(&zc);
	return (error);
}
#endif /* HAVE_SMB_SHARE */

static int
zfs_ioc_smb_acl(zfs_cmd_t *zc)
{
#ifdef HAVE_SMB_SHARE
	vnode_t *vp;
	znode_t *dzp;
	vnode_t *resourcevp = NULL;
	znode_t *sharedir;
	zfs_sb_t *zsb;
	nvlist_t *nvlist;
	char *src, *target;
	vattr_t vattr;
	vsecattr_t vsec;
	int error = 0;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (vp->v_vfsp->vfs_fstype != zfsfstype ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (SET_ERROR(EINVAL));
	}

	dzp = VTOZ(vp);
	zsb = ZTOZSB(dzp);
	ZFS_ENTER(zsb);

	/*
	 * Create share dir if its missing.
	 */
	mutex_enter(&zsb->z_lock);
	if (zsb->z_shares_dir == 0) {
		dmu_tx_t *tx;

		tx = dmu_tx_create(zsb->z_os);
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, TRUE,
		    ZFS_SHARES_DIR);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error != 0) {
			dmu_tx_abort(tx);
		} else {
			error = zfs_create_share_dir(zsb, tx);
			dmu_tx_commit(tx);
		}
		if (error != 0) {
			mutex_exit(&zsb->z_lock);
			VN_RELE(vp);
			ZFS_EXIT(zsb);
			return (error);
		}
	}
	mutex_exit(&zsb->z_lock);

	ASSERT(zsb->z_shares_dir);
	if ((error = zfs_zget(zsb, zsb->z_shares_dir, &sharedir)) != 0) {
		VN_RELE(vp);
		ZFS_EXIT(zsb);
		return (error);
	}

	switch (zc->zc_cookie) {
	case ZFS_SMB_ACL_ADD:
		vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
		vattr.va_mode = S_IFREG|0777;
		vattr.va_uid = 0;
		vattr.va_gid = 0;

		vsec.vsa_mask = VSA_ACE;
		vsec.vsa_aclentp = &full_access;
		vsec.vsa_aclentsz = sizeof (full_access);
		vsec.vsa_aclcnt = 1;

		error = VOP_CREATE(ZTOV(sharedir), zc->zc_string,
		    &vattr, EXCL, 0, &resourcevp, kcred, 0, NULL, &vsec);
		if (resourcevp)
			VN_RELE(resourcevp);
		break;

	case ZFS_SMB_ACL_REMOVE:
		error = VOP_REMOVE(ZTOV(sharedir), zc->zc_string, kcred,
		    NULL, 0);
		break;

	case ZFS_SMB_ACL_RENAME:
		if ((error = get_nvlist(zc->zc_nvlist_src,
		    zc->zc_nvlist_src_size, zc->zc_iflags, &nvlist)) != 0) {
			VN_RELE(vp);
			ZFS_EXIT(zsb);
			return (error);
		}
		if (nvlist_lookup_string(nvlist, ZFS_SMB_ACL_SRC, &src) ||
		    nvlist_lookup_string(nvlist, ZFS_SMB_ACL_TARGET,
		    &target)) {
			VN_RELE(vp);
			VN_RELE(ZTOV(sharedir));
			ZFS_EXIT(zsb);
			nvlist_free(nvlist);
			return (error);
		}
		error = VOP_RENAME(ZTOV(sharedir), src, ZTOV(sharedir), target,
		    kcred, NULL, 0);
		nvlist_free(nvlist);
		break;

	case ZFS_SMB_ACL_PURGE:
		error = zfs_smb_acl_purge(sharedir);
		break;

	default:
		error = SET_ERROR(EINVAL);
		break;
	}

	VN_RELE(vp);
	VN_RELE(ZTOV(sharedir));

	ZFS_EXIT(zsb);

	return (error);
#else
	return (SET_ERROR(ENOTSUP));
#endif /* HAVE_SMB_SHARE */
}

/*
 * innvl: {
 *     "holds" -> { snapname -> holdname (string), ... }
 *     (optional) "cleanup_fd" -> fd (int32)
 * }
 *
 * outnvl: {
 *     snapname -> error value (int32)
 *     ...
 * }
 */
/* ARGSUSED */
static int
zfs_ioc_hold(const char *pool, nvlist_t *args, nvlist_t *errlist)
{
	nvlist_t *holds;
	int cleanup_fd = -1;
	int error;
	minor_t minor = 0;

	error = nvlist_lookup_nvlist(args, "holds", &holds);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	if (nvlist_lookup_int32(args, "cleanup_fd", &cleanup_fd) == 0) {
		error = zfs_onexit_fd_hold(cleanup_fd, &minor);
		if (error != 0)
			return (error);
	}

	error = dsl_dataset_user_hold(holds, minor, errlist);
	if (minor != 0)
		zfs_onexit_fd_rele(cleanup_fd);
	return (error);
}

/*
 * innvl is not used.
 *
 * outnvl: {
 *    holdname -> time added (uint64 seconds since epoch)
 *    ...
 * }
 */
/* ARGSUSED */
static int
zfs_ioc_get_holds(const char *snapname, nvlist_t *args, nvlist_t *outnvl)
{
	return (dsl_dataset_get_holds(snapname, outnvl));
}

/*
 * innvl: {
 *     snapname -> { holdname, ... }
 *     ...
 * }
 *
 * outnvl: {
 *     snapname -> error value (int32)
 *     ...
 * }
 */
/* ARGSUSED */
static int
zfs_ioc_release(const char *pool, nvlist_t *holds, nvlist_t *errlist)
{
	return (dsl_dataset_user_release(holds, errlist));
}

/*
 * inputs:
 * zc_guid		flags (ZEVENT_NONBLOCK)
 * zc_cleanup_fd	zevent file descriptor
 *
 * outputs:
 * zc_nvlist_dst	next nvlist event
 * zc_cookie		dropped events since last get
 */
static int
zfs_ioc_events_next(zfs_cmd_t *zc)
{
	zfs_zevent_t *ze;
	nvlist_t *event = NULL;
	minor_t minor;
	uint64_t dropped = 0;
	int error;

	error = zfs_zevent_fd_hold(zc->zc_cleanup_fd, &minor, &ze);
	if (error != 0)
		return (error);

	do {
		error = zfs_zevent_next(ze, &event,
			&zc->zc_nvlist_dst_size, &dropped);
		if (event != NULL) {
			zc->zc_cookie = dropped;
			error = put_nvlist(zc, event);
			nvlist_free(event);
		}

		if (zc->zc_guid & ZEVENT_NONBLOCK)
			break;

		if ((error == 0) || (error != ENOENT))
			break;

		error = zfs_zevent_wait(ze);
		if (error != 0)
			break;
	} while (1);

	zfs_zevent_fd_rele(zc->zc_cleanup_fd);

	return (error);
}

/*
 * outputs:
 * zc_cookie		cleared events count
 */
static int
zfs_ioc_events_clear(zfs_cmd_t *zc)
{
	int count;

	zfs_zevent_drain_all(&count);
	zc->zc_cookie = count;

	return (0);
}

/*
 * inputs:
 * zc_guid		eid | ZEVENT_SEEK_START | ZEVENT_SEEK_END
 * zc_cleanup		zevent file descriptor
 */
static int
zfs_ioc_events_seek(zfs_cmd_t *zc)
{
	zfs_zevent_t *ze;
	minor_t minor;
	int error;

	error = zfs_zevent_fd_hold(zc->zc_cleanup_fd, &minor, &ze);
	if (error != 0)
		return (error);

	error = zfs_zevent_seek(ze, zc->zc_guid);
	zfs_zevent_fd_rele(zc->zc_cleanup_fd);

	return (error);
}

/*
 * inputs:
 * zc_name		name of new filesystem or snapshot
 * zc_value		full name of old snapshot
 *
 * outputs:
 * zc_cookie		space in bytes
 * zc_objset_type	compressed space in bytes
 * zc_perm_action	uncompressed space in bytes
 */
static int
zfs_ioc_space_written(zfs_cmd_t *zc)
{
	int error;
	dsl_pool_t *dp;
	dsl_dataset_t *new, *old;

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);
	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &new);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}
	error = dsl_dataset_hold(dp, zc->zc_value, FTAG, &old);
	if (error != 0) {
		dsl_dataset_rele(new, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = dsl_dataset_space_written(old, new, &zc->zc_cookie,
	    &zc->zc_objset_type, &zc->zc_perm_action);
	dsl_dataset_rele(old, FTAG);
	dsl_dataset_rele(new, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (error);
}

/*
 * innvl: {
 *     "firstsnap" -> snapshot name
 * }
 *
 * outnvl: {
 *     "used" -> space in bytes
 *     "compressed" -> compressed space in bytes
 *     "uncompressed" -> uncompressed space in bytes
 * }
 */
static int
zfs_ioc_space_snaps(const char *lastsnap, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error;
	dsl_pool_t *dp;
	dsl_dataset_t *new, *old;
	char *firstsnap;
	uint64_t used, comp, uncomp;

	if (nvlist_lookup_string(innvl, "firstsnap", &firstsnap) != 0)
		return (SET_ERROR(EINVAL));

	error = dsl_pool_hold(lastsnap, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, lastsnap, FTAG, &new);
	if (error == 0 && !new->ds_is_snapshot) {
		dsl_dataset_rele(new, FTAG);
		error = SET_ERROR(EINVAL);
	}
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}
	error = dsl_dataset_hold(dp, firstsnap, FTAG, &old);
	if (error == 0 && !old->ds_is_snapshot) {
		dsl_dataset_rele(old, FTAG);
		error = SET_ERROR(EINVAL);
	}
	if (error != 0) {
		dsl_dataset_rele(new, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = dsl_dataset_space_wouldfree(old, new, &used, &comp, &uncomp);
	dsl_dataset_rele(old, FTAG);
	dsl_dataset_rele(new, FTAG);
	dsl_pool_rele(dp, FTAG);
	fnvlist_add_uint64(outnvl, "used", used);
	fnvlist_add_uint64(outnvl, "compressed", comp);
	fnvlist_add_uint64(outnvl, "uncompressed", uncomp);
	return (error);
}

/*
 * innvl: {
 *     "fd" -> file descriptor to write stream to (int32)
 *     (optional) "fromsnap" -> full snap name to send an incremental from
 *     (optional) "largeblockok" -> (value ignored)
 *         indicates that blocks > 128KB are permitted
 *     (optional) "embedok" -> (value ignored)
 *         presence indicates DRR_WRITE_EMBEDDED records are permitted
 * }
 *
 * outnvl is unused
 */
/* ARGSUSED */
static int
zfs_ioc_send_new(const char *snapname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error;
	offset_t off;
	char *fromname = NULL;
	int fd;
	file_t *fp;
	boolean_t largeblockok;
	boolean_t embedok;

	error = nvlist_lookup_int32(innvl, "fd", &fd);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	(void) nvlist_lookup_string(innvl, "fromsnap", &fromname);

	largeblockok = nvlist_exists(innvl, "largeblockok");
	embedok = nvlist_exists(innvl, "embedok");

	if ((fp = getf(fd)) == NULL)
		return (SET_ERROR(EBADF));

	off = fp->f_offset;
	error = dmu_send(snapname, fromname, embedok, largeblockok,
	    fd, fp->f_vnode, &off);

	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;

	releasef(fd);
	return (error);
}

/*
 * Determine approximately how large a zfs send stream will be -- the number
 * of bytes that will be written to the fd supplied to zfs_ioc_send_new().
 *
 * innvl: {
 *     (optional) "from" -> full snap or bookmark name to send an incremental
 *                          from
 * }
 *
 * outnvl: {
 *     "space" -> bytes of space (uint64)
 * }
 */
static int
zfs_ioc_send_space(const char *snapname, nvlist_t *innvl, nvlist_t *outnvl)
{
	dsl_pool_t *dp;
	dsl_dataset_t *tosnap;
	int error;
	char *fromname;
	uint64_t space;

	error = dsl_pool_hold(snapname, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, snapname, FTAG, &tosnap);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = nvlist_lookup_string(innvl, "from", &fromname);
	if (error == 0) {
		if (strchr(fromname, '@') != NULL) {
			/*
			 * If from is a snapshot, hold it and use the more
			 * efficient dmu_send_estimate to estimate send space
			 * size using deadlists.
			 */
			dsl_dataset_t *fromsnap;
			error = dsl_dataset_hold(dp, fromname, FTAG, &fromsnap);
			if (error != 0)
				goto out;
			error = dmu_send_estimate(tosnap, fromsnap, &space);
			dsl_dataset_rele(fromsnap, FTAG);
		} else if (strchr(fromname, '#') != NULL) {
			/*
			 * If from is a bookmark, fetch the creation TXG of the
			 * snapshot it was created from and use that to find
			 * blocks that were born after it.
			 */
			zfs_bookmark_phys_t frombm;

			error = dsl_bookmark_lookup(dp, fromname, tosnap,
			    &frombm);
			if (error != 0)
				goto out;
			error = dmu_send_estimate_from_txg(tosnap,
			    frombm.zbm_creation_txg, &space);
		} else {
			/*
			 * from is not properly formatted as a snapshot or
			 * bookmark
			 */
			error = SET_ERROR(EINVAL);
			goto out;
		}
	} else {
		// If estimating the size of a full send, use dmu_send_estimate
		error = dmu_send_estimate(tosnap, NULL, &space);
	}

	fnvlist_add_uint64(outnvl, "space", space);

out:
	dsl_dataset_rele(tosnap, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (error);
}

static zfs_ioc_vec_t zfs_ioc_vec[ZFS_IOC_LAST - ZFS_IOC_FIRST];

static void
zfs_ioctl_register_legacy(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy, zfs_ioc_namecheck_t namecheck,
    boolean_t log_history, zfs_ioc_poolcheck_t pool_check)
{
	zfs_ioc_vec_t *vec = &zfs_ioc_vec[ioc - ZFS_IOC_FIRST];

	ASSERT3U(ioc, >=, ZFS_IOC_FIRST);
	ASSERT3U(ioc, <, ZFS_IOC_LAST);
	ASSERT3P(vec->zvec_legacy_func, ==, NULL);
	ASSERT3P(vec->zvec_func, ==, NULL);

	vec->zvec_legacy_func = func;
	vec->zvec_secpolicy = secpolicy;
	vec->zvec_namecheck = namecheck;
	vec->zvec_allow_log = log_history;
	vec->zvec_pool_check = pool_check;
}

/*
 * See the block comment at the beginning of this file for details on
 * each argument to this function.
 */
static void
zfs_ioctl_register(const char *name, zfs_ioc_t ioc, zfs_ioc_func_t *func,
    zfs_secpolicy_func_t *secpolicy, zfs_ioc_namecheck_t namecheck,
    zfs_ioc_poolcheck_t pool_check, boolean_t smush_outnvlist,
    boolean_t allow_log)
{
	zfs_ioc_vec_t *vec = &zfs_ioc_vec[ioc - ZFS_IOC_FIRST];

	ASSERT3U(ioc, >=, ZFS_IOC_FIRST);
	ASSERT3U(ioc, <, ZFS_IOC_LAST);
	ASSERT3P(vec->zvec_legacy_func, ==, NULL);
	ASSERT3P(vec->zvec_func, ==, NULL);

	/* if we are logging, the name must be valid */
	ASSERT(!allow_log || namecheck != NO_NAME);

	vec->zvec_name = name;
	vec->zvec_func = func;
	vec->zvec_secpolicy = secpolicy;
	vec->zvec_namecheck = namecheck;
	vec->zvec_pool_check = pool_check;
	vec->zvec_smush_outnvlist = smush_outnvlist;
	vec->zvec_allow_log = allow_log;
}

static void
zfs_ioctl_register_pool(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy, boolean_t log_history,
    zfs_ioc_poolcheck_t pool_check)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    POOL_NAME, log_history, pool_check);
}

static void
zfs_ioctl_register_dataset_nolog(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy, zfs_ioc_poolcheck_t pool_check)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    DATASET_NAME, B_FALSE, pool_check);
}

static void
zfs_ioctl_register_pool_modify(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func)
{
	zfs_ioctl_register_legacy(ioc, func, zfs_secpolicy_config,
	    POOL_NAME, B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);
}

static void
zfs_ioctl_register_pool_meta(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
    zfs_secpolicy_func_t *secpolicy)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    NO_NAME, B_FALSE, POOL_CHECK_NONE);
}

static void
zfs_ioctl_register_dataset_read_secpolicy(zfs_ioc_t ioc,
    zfs_ioc_legacy_func_t *func, zfs_secpolicy_func_t *secpolicy)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    DATASET_NAME, B_FALSE, POOL_CHECK_SUSPENDED);
}

static void
zfs_ioctl_register_dataset_read(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func)
{
	zfs_ioctl_register_dataset_read_secpolicy(ioc, func,
	    zfs_secpolicy_read);
}

static void
zfs_ioctl_register_dataset_modify(zfs_ioc_t ioc, zfs_ioc_legacy_func_t *func,
	zfs_secpolicy_func_t *secpolicy)
{
	zfs_ioctl_register_legacy(ioc, func, secpolicy,
	    DATASET_NAME, B_TRUE, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);
}

static void
zfs_ioctl_init(void)
{
	zfs_ioctl_register("snapshot", ZFS_IOC_SNAPSHOT,
	    zfs_ioc_snapshot, zfs_secpolicy_snapshot, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("log_history", ZFS_IOC_LOG_HISTORY,
	    zfs_ioc_log_history, zfs_secpolicy_log_history, NO_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_FALSE);

	zfs_ioctl_register("space_snaps", ZFS_IOC_SPACE_SNAPS,
	    zfs_ioc_space_snaps, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE);

	zfs_ioctl_register("send", ZFS_IOC_SEND_NEW,
	    zfs_ioc_send_new, zfs_secpolicy_send_new, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE);

	zfs_ioctl_register("send_space", ZFS_IOC_SEND_SPACE,
	    zfs_ioc_send_space, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE);

	zfs_ioctl_register("create", ZFS_IOC_CREATE,
	    zfs_ioc_create, zfs_secpolicy_create_clone, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("clone", ZFS_IOC_CLONE,
	    zfs_ioc_clone, zfs_secpolicy_create_clone, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("destroy_snaps", ZFS_IOC_DESTROY_SNAPS,
	    zfs_ioc_destroy_snaps, zfs_secpolicy_destroy_snaps, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("hold", ZFS_IOC_HOLD,
	    zfs_ioc_hold, zfs_secpolicy_hold, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);
	zfs_ioctl_register("release", ZFS_IOC_RELEASE,
	    zfs_ioc_release, zfs_secpolicy_release, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("get_holds", ZFS_IOC_GET_HOLDS,
	    zfs_ioc_get_holds, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE);

	zfs_ioctl_register("rollback", ZFS_IOC_ROLLBACK,
	    zfs_ioc_rollback, zfs_secpolicy_rollback, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_TRUE);

	zfs_ioctl_register("bookmark", ZFS_IOC_BOOKMARK,
	    zfs_ioc_bookmark, zfs_secpolicy_bookmark, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("get_bookmarks", ZFS_IOC_GET_BOOKMARKS,
	    zfs_ioc_get_bookmarks, zfs_secpolicy_read, DATASET_NAME,
	    POOL_CHECK_SUSPENDED, B_FALSE, B_FALSE);

	zfs_ioctl_register("destroy_bookmarks", ZFS_IOC_DESTROY_BOOKMARKS,
	    zfs_ioc_destroy_bookmarks, zfs_secpolicy_destroy_bookmarks,
	    POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	/* IOCTLS that use the legacy function signature */

	zfs_ioctl_register_legacy(ZFS_IOC_POOL_FREEZE, zfs_ioc_pool_freeze,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_READONLY);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_CREATE, zfs_ioc_pool_create,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_SCAN,
	    zfs_ioc_pool_scan);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_UPGRADE,
	    zfs_ioc_pool_upgrade);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_ADD,
	    zfs_ioc_vdev_add);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_REMOVE,
	    zfs_ioc_vdev_remove);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SET_STATE,
	    zfs_ioc_vdev_set_state);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_ATTACH,
	    zfs_ioc_vdev_attach);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_DETACH,
	    zfs_ioc_vdev_detach);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SETPATH,
	    zfs_ioc_vdev_setpath);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SETFRU,
	    zfs_ioc_vdev_setfru);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_SET_PROPS,
	    zfs_ioc_pool_set_props);
	zfs_ioctl_register_pool_modify(ZFS_IOC_VDEV_SPLIT,
	    zfs_ioc_vdev_split);
	zfs_ioctl_register_pool_modify(ZFS_IOC_POOL_REGUID,
	    zfs_ioc_pool_reguid);

	zfs_ioctl_register_pool_meta(ZFS_IOC_POOL_CONFIGS,
	    zfs_ioc_pool_configs, zfs_secpolicy_none);
	zfs_ioctl_register_pool_meta(ZFS_IOC_POOL_TRYIMPORT,
	    zfs_ioc_pool_tryimport, zfs_secpolicy_config);
	zfs_ioctl_register_pool_meta(ZFS_IOC_INJECT_FAULT,
	    zfs_ioc_inject_fault, zfs_secpolicy_inject);
	zfs_ioctl_register_pool_meta(ZFS_IOC_CLEAR_FAULT,
	    zfs_ioc_clear_fault, zfs_secpolicy_inject);
	zfs_ioctl_register_pool_meta(ZFS_IOC_INJECT_LIST_NEXT,
	    zfs_ioc_inject_list_next, zfs_secpolicy_inject);

	/*
	 * pool destroy, and export don't log the history as part of
	 * zfsdev_ioctl, but rather zfs_ioc_pool_export
	 * does the logging of those commands.
	 */
	zfs_ioctl_register_pool(ZFS_IOC_POOL_DESTROY, zfs_ioc_pool_destroy,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_EXPORT, zfs_ioc_pool_export,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_STATS, zfs_ioc_pool_stats,
	    zfs_secpolicy_read, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_GET_PROPS, zfs_ioc_pool_get_props,
	    zfs_secpolicy_read, B_FALSE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_ERROR_LOG, zfs_ioc_error_log,
	    zfs_secpolicy_inject, B_FALSE, POOL_CHECK_SUSPENDED);
	zfs_ioctl_register_pool(ZFS_IOC_DSOBJ_TO_DSNAME,
	    zfs_ioc_dsobj_to_dsname,
	    zfs_secpolicy_diff, B_FALSE, POOL_CHECK_SUSPENDED);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_GET_HISTORY,
	    zfs_ioc_pool_get_history,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_IMPORT, zfs_ioc_pool_import,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_CLEAR, zfs_ioc_clear,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_REOPEN, zfs_ioc_pool_reopen,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_SUSPENDED);

	zfs_ioctl_register_dataset_read(ZFS_IOC_SPACE_WRITTEN,
	    zfs_ioc_space_written);
	zfs_ioctl_register_dataset_read(ZFS_IOC_OBJSET_RECVD_PROPS,
	    zfs_ioc_objset_recvd_props);
	zfs_ioctl_register_dataset_read(ZFS_IOC_NEXT_OBJ,
	    zfs_ioc_next_obj);
	zfs_ioctl_register_dataset_read(ZFS_IOC_GET_FSACL,
	    zfs_ioc_get_fsacl);
	zfs_ioctl_register_dataset_read(ZFS_IOC_OBJSET_STATS,
	    zfs_ioc_objset_stats);
	zfs_ioctl_register_dataset_read(ZFS_IOC_OBJSET_ZPLPROPS,
	    zfs_ioc_objset_zplprops);
	zfs_ioctl_register_dataset_read(ZFS_IOC_DATASET_LIST_NEXT,
	    zfs_ioc_dataset_list_next);
	zfs_ioctl_register_dataset_read(ZFS_IOC_SNAPSHOT_LIST_NEXT,
	    zfs_ioc_snapshot_list_next);
	zfs_ioctl_register_dataset_read(ZFS_IOC_SEND_PROGRESS,
	    zfs_ioc_send_progress);

	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_DIFF,
	    zfs_ioc_diff, zfs_secpolicy_diff);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_OBJ_TO_STATS,
	    zfs_ioc_obj_to_stats, zfs_secpolicy_diff);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_OBJ_TO_PATH,
	    zfs_ioc_obj_to_path, zfs_secpolicy_diff);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_USERSPACE_ONE,
	    zfs_ioc_userspace_one, zfs_secpolicy_userspace_one);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_USERSPACE_MANY,
	    zfs_ioc_userspace_many, zfs_secpolicy_userspace_many);
	zfs_ioctl_register_dataset_read_secpolicy(ZFS_IOC_SEND,
	    zfs_ioc_send, zfs_secpolicy_send);

	zfs_ioctl_register_dataset_modify(ZFS_IOC_SET_PROP, zfs_ioc_set_prop,
	    zfs_secpolicy_none);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_DESTROY, zfs_ioc_destroy,
	    zfs_secpolicy_destroy);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_RENAME, zfs_ioc_rename,
	    zfs_secpolicy_rename);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_RECV, zfs_ioc_recv,
	    zfs_secpolicy_recv);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_PROMOTE, zfs_ioc_promote,
	    zfs_secpolicy_promote);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_INHERIT_PROP,
	    zfs_ioc_inherit_prop, zfs_secpolicy_inherit_prop);
	zfs_ioctl_register_dataset_modify(ZFS_IOC_SET_FSACL, zfs_ioc_set_fsacl,
	    zfs_secpolicy_set_fsacl);

	zfs_ioctl_register_dataset_nolog(ZFS_IOC_SHARE, zfs_ioc_share,
	    zfs_secpolicy_share, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_SMB_ACL, zfs_ioc_smb_acl,
	    zfs_secpolicy_smb_acl, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_USERSPACE_UPGRADE,
	    zfs_ioc_userspace_upgrade, zfs_secpolicy_userspace_upgrade,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_TMP_SNAPSHOT,
	    zfs_ioc_tmp_snapshot, zfs_secpolicy_tmp_snapshot,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY);

	/*
	 * ZoL functions
	 */
	zfs_ioctl_register_legacy(ZFS_IOC_EVENTS_NEXT, zfs_ioc_events_next,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_legacy(ZFS_IOC_EVENTS_CLEAR, zfs_ioc_events_clear,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_legacy(ZFS_IOC_EVENTS_SEEK, zfs_ioc_events_seek,
	    zfs_secpolicy_config, NO_NAME, B_FALSE, POOL_CHECK_NONE);
}

int
pool_status_check(const char *name, zfs_ioc_namecheck_t type,
    zfs_ioc_poolcheck_t check)
{
	spa_t *spa;
	int error;

	ASSERT(type == POOL_NAME || type == DATASET_NAME);

	if (check & POOL_CHECK_NONE)
		return (0);

	error = spa_open(name, &spa, FTAG);
	if (error == 0) {
		if ((check & POOL_CHECK_SUSPENDED) && spa_suspended(spa))
			error = SET_ERROR(EAGAIN);
		else if ((check & POOL_CHECK_READONLY) && !spa_writeable(spa))
			error = SET_ERROR(EROFS);
		spa_close(spa, FTAG);
	}
	return (error);
}

static void *
zfsdev_get_state_impl(minor_t minor, enum zfsdev_state_type which)
{
	zfsdev_state_t *zs;

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {
		if (zs->zs_minor == minor) {
			smp_rmb();
			switch (which) {
			case ZST_ONEXIT:
				return (zs->zs_onexit);
			case ZST_ZEVENT:
				return (zs->zs_zevent);
			case ZST_ALL:
				return (zs);
			}
		}
	}

	return (NULL);
}

void *
zfsdev_get_state(minor_t minor, enum zfsdev_state_type which)
{
	void *ptr;

	ptr = zfsdev_get_state_impl(minor, which);

	return (ptr);
}

int
zfsdev_getminor(struct file *filp, minor_t *minorp)
{
	zfsdev_state_t *zs, *fpd;

	ASSERT(filp != NULL);
	ASSERT(!MUTEX_HELD(&zfsdev_state_lock));

	fpd = filp->private_data;
	if (fpd == NULL)
		return (EBADF);

	mutex_enter(&zfsdev_state_lock);

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {

		if (zs->zs_minor == -1)
			continue;

		if (fpd == zs) {
			*minorp = fpd->zs_minor;
			mutex_exit(&zfsdev_state_lock);
			return (0);
		}
	}

	mutex_exit(&zfsdev_state_lock);

	return (EBADF);
}

/*
 * Find a free minor number.  The zfsdev_state_list is expected to
 * be short since it is only a list of currently open file handles.
 */
minor_t
zfsdev_minor_alloc(void)
{
	static minor_t last_minor = 0;
	minor_t m;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	for (m = last_minor + 1; m != last_minor; m++) {
		if (m > ZFSDEV_MAX_MINOR)
			m = 1;
		if (zfsdev_get_state_impl(m, ZST_ALL) == NULL) {
			last_minor = m;
			return (m);
		}
	}

	return (0);
}

static int
zfsdev_state_init(struct file *filp)
{
	zfsdev_state_t *zs, *zsprev = NULL;
	minor_t minor;
	boolean_t newzs = B_FALSE;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	minor = zfsdev_minor_alloc();
	if (minor == 0)
		return (SET_ERROR(ENXIO));

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {
		if (zs->zs_minor == -1)
			break;
		zsprev = zs;
	}

	if (!zs) {
		zs = kmem_zalloc(sizeof (zfsdev_state_t), KM_SLEEP);
		newzs = B_TRUE;
	}

	zs->zs_file = filp;
	filp->private_data = zs;

	zfs_onexit_init((zfs_onexit_t **)&zs->zs_onexit);
	zfs_zevent_init((zfs_zevent_t **)&zs->zs_zevent);


	/*
	 * In order to provide for lock-free concurrent read access
	 * to the minor list in zfsdev_get_state_impl(), new entries
	 * must be completely written before linking them into the
	 * list whereas existing entries are already linked; the last
	 * operation must be updating zs_minor (from -1 to the new
	 * value).
	 */
	if (newzs) {
		zs->zs_minor = minor;
		smp_wmb();
		zsprev->zs_next = zs;
	} else {
		smp_wmb();
		zs->zs_minor = minor;
	}

	return (0);
}

static int
zfsdev_state_destroy(struct file *filp)
{
	zfsdev_state_t *zs;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));
	ASSERT(filp->private_data != NULL);

	zs = filp->private_data;
	zs->zs_minor = -1;
	zfs_onexit_destroy(zs->zs_onexit);
	zfs_zevent_destroy(zs->zs_zevent);

	return (0);
}

static int
zfsdev_open(struct inode *ino, struct file *filp)
{
	int error;

	mutex_enter(&zfsdev_state_lock);
	error = zfsdev_state_init(filp);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static int
zfsdev_release(struct inode *ino, struct file *filp)
{
	int error;

	mutex_enter(&zfsdev_state_lock);
	error = zfsdev_state_destroy(filp);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static long
zfsdev_ioctl(struct file *filp, unsigned cmd, unsigned long arg)
{
	zfs_cmd_t *zc;
	uint_t vecnum;
	int error, rc, flag = 0;
	const zfs_ioc_vec_t *vec;
	char *saved_poolname = NULL;
	nvlist_t *innvl = NULL;
	fstrans_cookie_t cookie;

	vecnum = cmd - ZFS_IOC_FIRST;
	if (vecnum >= sizeof (zfs_ioc_vec) / sizeof (zfs_ioc_vec[0]))
		return (-SET_ERROR(EINVAL));
	vec = &zfs_ioc_vec[vecnum];

	/*
	 * The registered ioctl list may be sparse, verify that either
	 * a normal or legacy handler are registered.
	 */
	if (vec->zvec_func == NULL && vec->zvec_legacy_func == NULL)
		return (-SET_ERROR(EINVAL));

	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	error = ddi_copyin((void *)arg, zc, sizeof (zfs_cmd_t), flag);
	if (error != 0) {
		error = SET_ERROR(EFAULT);
		goto out;
	}

	zc->zc_iflags = flag & FKIOCTL;
	if (zc->zc_nvlist_src_size != 0) {
		error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
		    zc->zc_iflags, &innvl);
		if (error != 0)
			goto out;
	}

	/*
	 * Ensure that all pool/dataset names are valid before we pass down to
	 * the lower layers.
	 */
	zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
	switch (vec->zvec_namecheck) {
	case POOL_NAME:
		if (pool_namecheck(zc->zc_name, NULL, NULL) != 0)
			error = SET_ERROR(EINVAL);
		else
			error = pool_status_check(zc->zc_name,
			    vec->zvec_namecheck, vec->zvec_pool_check);
		break;

	case DATASET_NAME:
		if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0)
			error = SET_ERROR(EINVAL);
		else
			error = pool_status_check(zc->zc_name,
			    vec->zvec_namecheck, vec->zvec_pool_check);
		break;

	case NO_NAME:
		break;
	}


	if (error == 0 && !(flag & FKIOCTL)) {
		cookie = spl_fstrans_mark();
		error = vec->zvec_secpolicy(zc, innvl, CRED());
		spl_fstrans_unmark(cookie);
	}

	if (error != 0)
		goto out;

	/* legacy ioctls can modify zc_name */
	saved_poolname = strdup(zc->zc_name);
	if (saved_poolname == NULL) {
		error = SET_ERROR(ENOMEM);
		goto out;
	} else {
		saved_poolname[strcspn(saved_poolname, "/@#")] = '\0';
	}

	if (vec->zvec_func != NULL) {
		nvlist_t *outnvl;
		int puterror = 0;
		spa_t *spa;
		nvlist_t *lognv = NULL;

		ASSERT(vec->zvec_legacy_func == NULL);

		/*
		 * Add the innvl to the lognv before calling the func,
		 * in case the func changes the innvl.
		 */
		if (vec->zvec_allow_log) {
			lognv = fnvlist_alloc();
			fnvlist_add_string(lognv, ZPOOL_HIST_IOCTL,
			    vec->zvec_name);
			if (!nvlist_empty(innvl)) {
				fnvlist_add_nvlist(lognv, ZPOOL_HIST_INPUT_NVL,
				    innvl);
			}
		}

		outnvl = fnvlist_alloc();
		cookie = spl_fstrans_mark();
		error = vec->zvec_func(zc->zc_name, innvl, outnvl);
		spl_fstrans_unmark(cookie);

		if (error == 0 && vec->zvec_allow_log &&
		    spa_open(zc->zc_name, &spa, FTAG) == 0) {
			if (!nvlist_empty(outnvl)) {
				fnvlist_add_nvlist(lognv, ZPOOL_HIST_OUTPUT_NVL,
				    outnvl);
			}
			(void) spa_history_log_nvl(spa, lognv);
			spa_close(spa, FTAG);
		}
		fnvlist_free(lognv);

		if (!nvlist_empty(outnvl) || zc->zc_nvlist_dst_size != 0) {
			int smusherror = 0;
			if (vec->zvec_smush_outnvlist) {
				smusherror = nvlist_smush(outnvl,
				    zc->zc_nvlist_dst_size);
			}
			if (smusherror == 0)
				puterror = put_nvlist(zc, outnvl);
		}

		if (puterror != 0)
			error = puterror;

		nvlist_free(outnvl);
	} else {
		cookie = spl_fstrans_mark();
		error = vec->zvec_legacy_func(zc);
		spl_fstrans_unmark(cookie);
	}

out:
	nvlist_free(innvl);
	rc = ddi_copyout(zc, (void *)arg, sizeof (zfs_cmd_t), flag);
	if (error == 0 && rc != 0)
		error = SET_ERROR(EFAULT);
	if (error == 0 && vec->zvec_allow_log) {
		char *s = tsd_get(zfs_allow_log_key);
		if (s != NULL)
			strfree(s);
		(void) tsd_set(zfs_allow_log_key, saved_poolname);
	} else {
		if (saved_poolname != NULL)
			strfree(saved_poolname);
	}

	kmem_free(zc, sizeof (zfs_cmd_t));
	return (-error);
}

#ifdef CONFIG_COMPAT
static long
zfsdev_compat_ioctl(struct file *filp, unsigned cmd, unsigned long arg)
{
	return (zfsdev_ioctl(filp, cmd, arg));
}
#else
#define	zfsdev_compat_ioctl	NULL
#endif

static const struct file_operations zfsdev_fops = {
	.open		= zfsdev_open,
	.release	= zfsdev_release,
	.unlocked_ioctl	= zfsdev_ioctl,
	.compat_ioctl	= zfsdev_compat_ioctl,
	.owner		= THIS_MODULE,
};

static struct miscdevice zfs_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= ZFS_DRIVER,
	.fops		= &zfsdev_fops,
};

static int
zfs_attach(void)
{
	int error;

	mutex_init(&zfsdev_state_lock, NULL, MUTEX_DEFAULT, NULL);
	zfsdev_state_list = kmem_zalloc(sizeof (zfsdev_state_t), KM_SLEEP);
	zfsdev_state_list->zs_minor = -1;

	error = misc_register(&zfs_misc);
	if (error != 0) {
		printk(KERN_INFO "ZFS: misc_register() failed %d\n", error);
		return (error);
	}

	return (0);
}

static void
zfs_detach(void)
{
	zfsdev_state_t *zs, *zsprev = NULL;

	misc_deregister(&zfs_misc);
	mutex_destroy(&zfsdev_state_lock);

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {
		if (zsprev)
			kmem_free(zsprev, sizeof (zfsdev_state_t));
		zsprev = zs;
	}
	if (zsprev)
		kmem_free(zsprev, sizeof (zfsdev_state_t));
}

static void
zfs_allow_log_destroy(void *arg)
{
	char *poolname = arg;
	strfree(poolname);
}

#ifdef DEBUG
#define	ZFS_DEBUG_STR	" (DEBUG mode)"
#else
#define	ZFS_DEBUG_STR	""
#endif

static int __init
_init(void)
{
	int error;

	error = -vn_set_pwd("/");
	if (error) {
		printk(KERN_NOTICE
		    "ZFS: Warning unable to set pwd to '/': %d\n", error);
		return (error);
	}

	if ((error = -zvol_init()) != 0)
		return (error);

	spa_init(FREAD | FWRITE);
	zfs_init();

	zfs_ioctl_init();

	if ((error = zfs_attach()) != 0)
		goto out;

	tsd_create(&zfs_fsyncer_key, NULL);
	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
	tsd_create(&zfs_allow_log_key, zfs_allow_log_destroy);

	printk(KERN_NOTICE "ZFS: Loaded module v%s-%s%s, "
	    "ZFS pool version %s, ZFS filesystem version %s\n",
	    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR,
	    SPA_VERSION_STRING, ZPL_VERSION_STRING);
#ifndef CONFIG_FS_POSIX_ACL
	printk(KERN_NOTICE "ZFS: Posix ACLs disabled by kernel\n");
#endif /* CONFIG_FS_POSIX_ACL */

	return (0);

out:
	zfs_fini();
	spa_fini();
	(void) zvol_fini();
	printk(KERN_NOTICE "ZFS: Failed to Load ZFS Filesystem v%s-%s%s"
	    ", rc = %d\n", ZFS_META_VERSION, ZFS_META_RELEASE,
	    ZFS_DEBUG_STR, error);

	return (error);
}

static void __exit
_fini(void)
{
	zfs_detach();
	zfs_fini();
	spa_fini();
	zvol_fini();

	tsd_destroy(&zfs_fsyncer_key);
	tsd_destroy(&rrw_tsd_key);
	tsd_destroy(&zfs_allow_log_key);

	printk(KERN_NOTICE "ZFS: Unloaded module v%s-%s%s\n",
	    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR);
}

#ifdef HAVE_SPL
module_init(_init);
module_exit(_fini);

MODULE_DESCRIPTION("ZFS");
MODULE_AUTHOR(ZFS_META_AUTHOR);
MODULE_LICENSE(ZFS_META_LICENSE);
MODULE_VERSION(ZFS_META_VERSION "-" ZFS_META_RELEASE);
#endif /* HAVE_SPL */
