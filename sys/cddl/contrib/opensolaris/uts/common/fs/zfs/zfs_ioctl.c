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
 * Copyright (c) 2011-2012 Pawel Jakub Dawidek. All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 * Copyright 2014 Xin Li <delphij@FreeBSD.org>. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014, 2016 Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2017 Datto Inc.
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
#ifdef __FreeBSD__
#include "opt_kstack_pages.h"
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
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
#include <sys/dmu.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/zone.h>
#include <sys/nvpair.h>
#include <sys/mount.h>
#include <sys/taskqueue.h>
#include <sys/sdt.h>
#include <sys/varargs.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/dsl_scan.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_userhold.h>
#include <sys/zfeature.h>
#include <sys/zcp.h>
#include <sys/zio_checksum.h>
#include <sys/vdev_removal.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_initialize.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "zfs_deleg.h"
#include "zfs_comutil.h"
#include "zfs_ioctl_compat.h"

#include "lua.h"
#include "lauxlib.h"

static struct cdev *zfsdev;

extern void zfs_init(void);
extern void zfs_fini(void);

uint_t zfs_fsyncer_key;
extern uint_t rrw_tsd_key;
static uint_t zfs_allow_log_key;
extern uint_t zfs_geom_probe_vdev_key;

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
 
static void zfsdev_close(void *data);

static int zfs_prop_activate_feature(spa_t *spa, spa_feature_t feature);

/* _NOTE(PRINTFLIKE(4)) - this is printf-like, but lint is too whiney */
void
__dprintf(const char *file, const char *func, int line, const char *fmt, ...)
{
	const char *newfile;
	char buf[512];
	va_list adx;

	/*
	 * Get rid of annoying "../common/" prefix to filename.
	 */
	newfile = strrchr(file, '/');
	if (newfile != NULL) {
		newfile = newfile + 1; /* Get rid of leading / */
	} else {
		newfile = file;
	}

	va_start(adx, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, adx);
	va_end(adx);

	/*
	 * To get this data, use the zfs-dprintf probe as so:
	 * dtrace -q -n 'zfs-dprintf \
	 *	/stringof(arg0) == "dbuf.c"/ \
	 *	{printf("%s: %s", stringof(arg1), stringof(arg3))}'
	 * arg0 = file name
	 * arg1 = function name
	 * arg2 = line number
	 * arg3 = message
	 */
	DTRACE_PROBE4(zfs__dprintf,
	    char *, newfile, char *, func, int, line, char *, buf);
}

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
	if (INGLOBALZONE(curthread) ||
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
	if (!INGLOBALZONE(curthread) &&
	    !zone_dataset_visible(dataset, &writable))
		return (SET_ERROR(ENOENT));

	if (INGLOBALZONE(curthread)) {
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

	if (dsl_prop_get_integer(dataset, "jailed", &zoned, NULL))
		return (SET_ERROR(ENOENT));

	return (zfs_dozonecheck_impl(dataset, zoned, cr));
}

static int
zfs_dozonecheck_ds(const char *dataset, dsl_dataset_t *ds, cred_t *cr)
{
	uint64_t zoned;

	if (dsl_prop_get_int_ds(ds, "jailed", &zoned))
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

	/*
	 * First do a quick check for root in the global zone, which
	 * is allowed to do all write_perms.  This ensures that zfs_ioc_*
	 * will get to handle nonexistent datasets.
	 */
	if (INGLOBALZONE(curthread) && secpolicy_zfs(cr) == 0)
		return (0);

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

#ifdef SECLABEL
/*
 * Policy for setting the security label property.
 *
 * Returns 0 for success, non-zero for access and other errors.
 */
static int
zfs_set_slabel_policy(const char *name, char *strval, cred_t *cr)
{
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
	 * Get the zfsvfs; if there isn't one, then the dataset isn't
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
}
#endif	/* SECLABEL */

static int
zfs_secpolicy_setprop(const char *dsname, zfs_prop_t prop, nvpair_t *propval,
    cred_t *cr)
{
	char *strval;

	/*
	 * Check permissions for special properties.
	 */
	switch (prop) {
	case ZFS_PROP_ZONED:
		/*
		 * Disallow setting of 'zoned' from within a local zone.
		 */
		if (!INGLOBALZONE(curthread))
			return (SET_ERROR(EPERM));
		break;

	case ZFS_PROP_QUOTA:
	case ZFS_PROP_FILESYSTEM_LIMIT:
	case ZFS_PROP_SNAPSHOT_LIMIT:
		if (!INGLOBALZONE(curthread)) {
			uint64_t zoned;
			char setpoint[ZFS_MAX_DATASET_NAME_LEN];
			/*
			 * Unprivileged users are allowed to modify the
			 * limit on things *under* (ie. contained by)
			 * the thing they own.
			 */
			if (dsl_prop_get_integer(dsname, "jailed", &zoned,
			    setpoint))
				return (SET_ERROR(EPERM));
			if (!zoned || strlen(dsname) <= strlen(setpoint))
				return (SET_ERROR(EPERM));
		}
		break;

	case ZFS_PROP_MLSLABEL:
#ifdef SECLABEL
		if (!is_system_labeled())
			return (SET_ERROR(EPERM));

		if (nvpair_value_string(propval, &strval) == 0) {
			int err;

			err = zfs_set_slabel_policy(dsname, strval, CRED());
			if (err != 0)
				return (err);
		}
#else
		return (EOPNOTSUPP);
#endif
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

	if (strcmp(vp->v_vfsp->mnt_stat.f_fstypename, "zfs") != 0 ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (SET_ERROR(EPERM));
	}

	VN_RELE(vp);
	return (dsl_deleg_access(zc->zc_name,
	    ZFS_DELEG_PERM_SHARE, cr));
}

int
zfs_secpolicy_share(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	if (!INGLOBALZONE(curthread))
		return (SET_ERROR(EPERM));

	if (secpolicy_nfs(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, innvl, cr));
	}
}

int
zfs_secpolicy_smb_acl(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	if (!INGLOBALZONE(curthread))
		return (SET_ERROR(EPERM));

	if (secpolicy_smb(cr) == 0) {
		return (0);
	} else {
		return (zfs_secpolicy_deleg_share(zc, innvl, cr));
	}
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
	char	parentname[ZFS_MAX_DATASET_NAME_LEN];
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
	char *at = NULL;
	int error;

	if ((zc->zc_cookie & 1) != 0) {
		/*
		 * This is recursive rename, so the starting snapshot might
		 * not exist. Check file system or volume permission instead.
		 */
		at = strchr(zc->zc_name, '@');
		if (at == NULL)
			return (EINVAL);
		*at = '\0';
	}

	error = zfs_secpolicy_rename_perms(zc->zc_name, zc->zc_value, cr);

	if (at != NULL)
		*at = '@';

	return (error);
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
		char parentname[ZFS_MAX_DATASET_NAME_LEN];
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
	int error;
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

	for (nvpair_t *pair = nvlist_next_nvpair(innvl, NULL);
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
zfs_secpolicy_remap(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (zfs_secpolicy_write_perms(zc->zc_name,
	    ZFS_DELEG_PERM_REMAP, cr));
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
	char	parentname[ZFS_MAX_DATASET_NAME_LEN];
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
		char fsname[ZFS_MAX_DATASET_NAME_LEN];
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
		char fsname[ZFS_MAX_DATASET_NAME_LEN];
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

	packed = kmem_alloc(size, KM_SLEEP);

	if ((error = ddi_copyin((void *)(uintptr_t)nvl, packed, size,
	    iflag)) != 0) {
		kmem_free(packed, size);
		return (SET_ERROR(EFAULT));
	}

	if ((error = nvlist_unpack(packed, size, &list, 0)) != 0) {
		kmem_free(packed, size);
		return (error);
	}

	kmem_free(packed, size);

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
		/*
		 * Solaris returns ENOMEM here, because even if an error is
		 * returned from an ioctl(2), new zc_nvlist_dst_size will be
		 * passed to the userland. This is not the case for FreeBSD.
		 * We need to return 0, so the kernel will copy the
		 * zc_nvlist_dst_size back and the userland can discover that a
		 * bigger buffer is needed.
		 */
		error = 0;
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

int
getzfsvfs_impl(objset_t *os, vfs_t **vfsp)
{
	zfsvfs_t *zfvp;
	int error = 0;

	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		return (SET_ERROR(EINVAL));
	}

	mutex_enter(&os->os_user_ptr_lock);
	zfvp = dmu_objset_get_user(os);
	if (zfvp) {
		*vfsp = zfvp->z_vfs;
		vfs_ref(zfvp->z_vfs);
	} else {
		error = SET_ERROR(ESRCH);
	}
	mutex_exit(&os->os_user_ptr_lock);
	return (error);
}

int
getzfsvfs(const char *dsname, zfsvfs_t **zfvp)
{
	objset_t *os;
	vfs_t *vfsp;
	int error;

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0)
		return (error);
	error = getzfsvfs_impl(os, &vfsp);
	dmu_objset_rele(os, FTAG);
	if (error != 0)
		return (error);

	error = vfs_busy(vfsp, 0);
	vfs_rel(vfsp);
	if (error != 0) {
		*zfvp = NULL;
		error = SET_ERROR(ESRCH);
	} else {
		*zfvp = vfsp->vfs_data;
	}
	return (error);
}

/*
 * Find a zfsvfs_t for a mounted filesystem, or create our own, in which
 * case its z_vfs will be NULL, and it will be opened as the owner.
 * If 'writer' is set, the z_teardown_lock will be held for RW_WRITER,
 * which prevents all vnode ops from running.
 */
static int
zfsvfs_hold(const char *name, void *tag, zfsvfs_t **zfvp, boolean_t writer)
{
	int error = 0;

	if (getzfsvfs(name, zfvp) != 0)
		error = zfsvfs_create(name, zfvp);
	if (error == 0) {
		rrm_enter(&(*zfvp)->z_teardown_lock, (writer) ? RW_WRITER :
		    RW_READER, tag);
#ifdef illumos
		if ((*zfvp)->z_unmounted) {
			/*
			 * XXX we could probably try again, since the unmounting
			 * thread should be just about to disassociate the
			 * objset from the zfsvfs.
			 */
			rrm_exit(&(*zfvp)->z_teardown_lock, tag);
			return (SET_ERROR(EBUSY));
		}
#else
		/*
		 * vfs_busy() ensures that the filesystem is not and
		 * can not be unmounted.
		 */
		ASSERT(!(*zfvp)->z_unmounted);
#endif
	}
	return (error);
}

static void
zfsvfs_rele(zfsvfs_t *zfsvfs, void *tag)
{
	rrm_exit(&zfsvfs->z_teardown_lock, tag);

	if (zfsvfs->z_vfs) {
#ifdef illumos
		VFS_RELE(zfsvfs->z_vfs);
#else
		vfs_unbusy(zfsvfs->z_vfs);
#endif
	} else {
		dmu_objset_disown(zfsvfs->z_os, zfsvfs);
		zfsvfs_free(zfsvfs);
	}
}

static int
zfs_ioc_pool_create(zfs_cmd_t *zc)
{
	int error;
	nvlist_t *config, *props = NULL;
	nvlist_t *rootprops = NULL;
	nvlist_t *zplprops = NULL;
	char *spa_name = zc->zc_name;

	if (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config))
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
		char *tname;

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

		if (nvlist_lookup_string(props,
		    zpool_prop_to_name(ZPOOL_PROP_TNAME), &tname) == 0)
			spa_name = tname;
	}

	error = spa_create(zc->zc_name, config, props, zplprops);

	/*
	 * Set the remaining root properties
	 */
	if (!error && (error = zfs_set_prop_nvlist(spa_name,
	    ZPROP_SRC_LOCAL, rootprops, NULL)) != 0)
		(void) spa_destroy(spa_name);

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
	if (error == 0)
		zvol_remove_minors(zc->zc_name);
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
	if (error == 0)
		zvol_remove_minors(zc->zc_name);
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
 * zc_flags             scrub pause/resume flag (pool_scrub_cmd_t)
 */
static int
zfs_ioc_pool_scan(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	if ((error = spa_open(zc->zc_name, &spa, FTAG)) != 0)
		return (error);

	if (zc->zc_flags >= POOL_SCRUB_FLAGS_END)
		return (SET_ERROR(EINVAL));

	if (zc->zc_flags == POOL_SCRUB_PAUSE)
		error = spa_scrub_pause_resume(spa, POOL_SCRUB_PAUSE);
	else if (zc->zc_cookie == POOL_SCAN_NONE)
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

	hist_buf = kmem_alloc(size, KM_SLEEP);
	if ((error = spa_history_get(spa, &zc->zc_history_offset,
	    &zc->zc_history_len, hist_buf)) == 0) {
		error = ddi_copyout(hist_buf,
		    (void *)(uintptr_t)zc->zc_history,
		    zc->zc_history_len, zc->zc_iflags);
	}

	spa_close(spa, FTAG);
	kmem_free(hist_buf, size);
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
	nvlist_t *config, **l2cache, **spares;
	uint_t nl2cache = 0, nspares = 0;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);

	error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config);
	(void) nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache);

	(void) nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares);

#ifdef illumos
	/*
	 * A root pool with concatenated devices is not supported.
	 * Thus, can not add a device to a root pool.
	 *
	 * Intent log device can not be added to a rootpool because
	 * during mountroot, zil is replayed, a seperated log device
	 * can not be accessed during the mountroot time.
	 *
	 * l2cache and spare devices are ok to be added to a rootpool.
	 */
	if (spa_bootfs(spa) != 0 && nl2cache == 0 && nspares == 0) {
		nvlist_free(config);
		spa_close(spa, FTAG);
		return (SET_ERROR(EDOM));
	}
#endif /* illumos */

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
 * zc_guid		guid of vdev to remove
 * zc_cookie		cancel removal
 */
static int
zfs_ioc_vdev_remove(zfs_cmd_t *zc)
{
	spa_t *spa;
	int error;

	error = spa_open(zc->zc_name, &spa, FTAG);
	if (error != 0)
		return (error);
	if (zc->zc_cookie != 0) {
		error = spa_vdev_remove_cancel(spa);
	} else {
		error = spa_vdev_remove(spa, zc->zc_guid, B_FALSE);
	}
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

	if (error = get_nvlist(zc->zc_nvlist_conf, zc->zc_nvlist_conf_size,
	    zc->zc_iflags, &config)) {
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

	if (error == ENOMEM)
		error = 0;
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
	if (err = dmu_objset_hold(zc->zc_name, FTAG, &os))
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
	if (!INGLOBALZONE(curthread) && !zone_dataset_visible(name, NULL))
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
	if (error = dmu_objset_hold(zc->zc_name, FTAG, &os)) {
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
 * zc_simple		when set, only name is requested
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
	if (strlcat(zc->zc_name, "@", sizeof (zc->zc_name)) >=
	    ZFS_MAX_DATASET_NAME_LEN) {
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
	zfsvfs_t *zfsvfs;
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

	err = zfsvfs_hold(dsname, FTAG, &zfsvfs, B_FALSE);
	if (err == 0) {
		err = zfs_set_userquota(zfsvfs, type, domain, rid, quota);
		zfsvfs_rele(zfsvfs, FTAG);
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
	case ZFS_PROP_VERSION:
	{
		zfsvfs_t *zfsvfs;

		if ((err = zfsvfs_hold(dsname, FTAG, &zfsvfs, B_TRUE)) != 0)
			break;

		err = zfs_set_version(zfsvfs, intval);
		zfsvfs_rele(zfsvfs, FTAG);

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

		if (error = zfs_secpolicy_write_perms(fsname,
		    ZFS_DELEG_PERM_USERPROP, CRED()))
			return (error);

		if (strlen(propname) >= ZAP_MAXNAMELEN)
			return (SET_ERROR(ENAMETOOLONG));

		if (strlen(fnvpair_value_string(pair)) >= ZAP_MAXVALUELEN)
			return (E2BIG);
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

	if (error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
	    zc->zc_iflags, &props))
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
			spa_write_cachefile(spa, B_FALSE, B_TRUE);
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

	ASSERT(zplprops != NULL);

	if (os != NULL && os->os_phys->os_type != DMU_OST_ZFS)
		return (SET_ERROR(EINVAL));

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

	if (norm == ZFS_PROP_UNDEFINED)
		VERIFY(zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &norm) == 0);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_NORMALIZE), norm) == 0);

	/*
	 * If we're normalizing, names must always be valid UTF-8 strings.
	 */
	if (norm)
		u8 = 1;
	if (u8 == ZFS_PROP_UNDEFINED)
		VERIFY(zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &u8) == 0);
	VERIFY(nvlist_add_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_UTF8ONLY), u8) == 0);

	if (sense == ZFS_PROP_UNDEFINED)
		VERIFY(zfs_get_zplprop(os, ZFS_PROP_CASE, &sense) == 0);
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
	char parentname[ZFS_MAX_DATASET_NAME_LEN];
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

		if ((error = zvol_check_volblocksize(
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
#ifdef __FreeBSD__
	if (error == 0 && type == DMU_OST_ZVOL)
		zvol_create_minors(fsname);
#endif
	return (error);
}

/*
 * innvl: {
 *     "origin" -> name of origin snapshot
 *     (optional) "props" -> { prop -> value }
 * }
 *
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
#ifdef __FreeBSD__
	if (error == 0)
		zvol_create_minors(fsname);
#endif
	return (error);
}

/* ARGSUSED */
static int
zfs_ioc_remap(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	if (strchr(fsname, '@') ||
	    strchr(fsname, '%'))
		return (SET_ERROR(EINVAL));

	return (dmu_objset_remap_indirects(fsname));
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
	nvpair_t *pair;

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
		for (nvpair_t *pair2 = nvlist_next_nvpair(snaps, pair);
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

#ifdef __FreeBSD__
static int
zfs_ioc_nextboot(const char *unused, nvlist_t *innvl, nvlist_t *outnvl)
{
	char name[MAXNAMELEN];
	spa_t *spa;
	vdev_t *vd;
	char *command;
	uint64_t pool_guid;
	uint64_t vdev_guid;
	int error;

	if (nvlist_lookup_uint64(innvl,
	    ZPOOL_CONFIG_POOL_GUID, &pool_guid) != 0)
		return (EINVAL);
	if (nvlist_lookup_uint64(innvl,
	    ZPOOL_CONFIG_GUID, &vdev_guid) != 0)
		return (EINVAL);
	if (nvlist_lookup_string(innvl,
	    "command", &command) != 0)
		return (EINVAL);

	mutex_enter(&spa_namespace_lock);
	spa = spa_by_guid(pool_guid, vdev_guid);
	if (spa != NULL)
		strcpy(name, spa_name(spa));
	mutex_exit(&spa_namespace_lock);
	if (spa == NULL)
		return (ENOENT);

	if ((error = spa_open(name, &spa, FTAG)) != 0)
		return (error);
	spa_vdev_state_enter(spa, SCL_ALL);
	vd = spa_lookup_by_guid(spa, vdev_guid, B_TRUE);
	if (vd == NULL) {
		(void) spa_vdev_state_exit(spa, NULL, ENXIO);
		spa_close(spa, FTAG);
		return (ENODEV);
	}
	error = vdev_label_write_pad2(vd, command, strlen(command));
	(void) spa_vdev_state_exit(spa, NULL, 0);
	txg_wait_synced(spa->spa_dsl_pool, 0);
	spa_close(spa, FTAG);
	return (error);
}
#endif

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
void
zfs_unmount_snap(const char *snapname)
{
	vfs_t *vfsp = NULL;
	zfsvfs_t *zfsvfs = NULL;

	if (strchr(snapname, '@') == NULL)
		return;

	int err = getzfsvfs(snapname, &zfsvfs);
	if (err != 0) {
		ASSERT3P(zfsvfs, ==, NULL);
		return;
	}
	vfsp = zfsvfs->z_vfs;

	ASSERT(!dsl_pool_config_held(dmu_objset_pool(zfsvfs->z_os)));

#ifdef illumos
	err = vn_vfswlock(vfsp->vfs_vnodecovered);
	VFS_RELE(vfsp);
	if (err != 0)
		return;
#endif

	/*
	 * Always force the unmount for snapshots.
	 */
#ifdef illumos
	(void) dounmount(vfsp, MS_FORCE, kcred);
#else
	vfs_ref(vfsp);
	vfs_unbusy(vfsp);
	(void) dounmount(vfsp, MS_FORCE, curthread);
#endif
}

/* ARGSUSED */
static int
zfs_unmount_snap_cb(const char *snapname, void *arg)
{
	zfs_unmount_snap(snapname);
	return (0);
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
		char originname[ZFS_MAX_DATASET_NAME_LEN];
		dsl_dataset_name(ds->ds_prev, originname);
		dmu_objset_rele(os, FTAG);
		zfs_unmount_snap(originname);
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
 *
 */
/* ARGSUSED */
static int
zfs_ioc_destroy_snaps(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	int error, poollen;
	nvlist_t *snaps;
	nvpair_t *pair;
	boolean_t defer;

	if (nvlist_lookup_nvlist(innvl, "snaps", &snaps) != 0)
		return (SET_ERROR(EINVAL));
	defer = nvlist_exists(innvl, "defer");

	poollen = strlen(poolname);
	for (pair = nvlist_next_nvpair(snaps, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(snaps, pair)) {
		const char *name = nvpair_name(pair);

		/*
		 * The snap must be in the specified pool to prevent the
		 * invalid removal of zvol minors below.
		 */
		if (strncmp(name, poolname, poollen) != 0 ||
		    (name[poollen] != '/' && name[poollen] != '@'))
			return (SET_ERROR(EXDEV));

		zfs_unmount_snap(nvpair_name(pair));
#if defined(__FreeBSD__)
		zvol_remove_minors(name);
#endif
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
	for (nvpair_t *pair = nvlist_next_nvpair(innvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(innvl, pair)) {
		char *snap_name;

		/*
		 * Verify the snapshot argument.
		 */
		if (nvpair_value_string(pair, &snap_name) != 0)
			return (SET_ERROR(EINVAL));


		/* Verify that the keys (bookmarks) are unique */
		for (nvpair_t *pair2 = nvlist_next_nvpair(innvl, pair);
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

	poollen = strlen(poolname);
	for (nvpair_t *pair = nvlist_next_nvpair(innvl, NULL);
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

static int
zfs_ioc_channel_program(const char *poolname, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	char *program;
	uint64_t instrlimit, memlimit;
	boolean_t sync_flag;
	nvpair_t *nvarg = NULL;

	if (0 != nvlist_lookup_string(innvl, ZCP_ARG_PROGRAM, &program)) {
		return (EINVAL);
	}
	if (0 != nvlist_lookup_boolean_value(innvl, ZCP_ARG_SYNC, &sync_flag)) {
		sync_flag = B_TRUE;
	}
	if (0 != nvlist_lookup_uint64(innvl, ZCP_ARG_INSTRLIMIT, &instrlimit)) {
		instrlimit = ZCP_DEFAULT_INSTRLIMIT;
	}
	if (0 != nvlist_lookup_uint64(innvl, ZCP_ARG_MEMLIMIT, &memlimit)) {
		memlimit = ZCP_DEFAULT_MEMLIMIT;
	}
	if (0 != nvlist_lookup_nvpair(innvl, ZCP_ARG_ARGLIST, &nvarg)) {
		return (EINVAL);
	}

	if (instrlimit == 0 || instrlimit > zfs_lua_max_instrlimit)
		return (EINVAL);
	if (memlimit == 0 || memlimit > zfs_lua_max_memlimit)
		return (EINVAL);

	return (zcp_eval(poolname, program, sync_flag, instrlimit, memlimit,
	    nvarg, outnvl));
}

/*
 * innvl: unused
 * outnvl: empty
 */
/* ARGSUSED */
static int
zfs_ioc_pool_checkpoint(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	return (spa_checkpoint(poolname));
}

/*
 * innvl: unused
 * outnvl: empty
 */
/* ARGSUSED */
static int
zfs_ioc_pool_discard_checkpoint(const char *poolname, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	return (spa_checkpoint_discard(poolname));
}

/*
 * inputs:
 * zc_name		name of dataset to destroy
 * zc_defer_destroy	mark for deferred destroy
 *
 * outputs:		none
 */
static int
zfs_ioc_destroy(zfs_cmd_t *zc)
{
	objset_t *os;
	dmu_objset_type_t ost;
	int err;

	err = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (err != 0)
		return (err);
	ost = dmu_objset_type(os);
	dmu_objset_rele(os, FTAG);

	if (ost == DMU_OST_ZFS)
		zfs_unmount_snap(zc->zc_name);

	if (strchr(zc->zc_name, '@'))
		err = dsl_destroy_snapshot(zc->zc_name, zc->zc_defer_destroy);
	else
		err = dsl_destroy_head(zc->zc_name);
	if (ost == DMU_OST_ZVOL && err == 0)
#ifdef __FreeBSD__
		zvol_remove_minors(zc->zc_name);
#else
		(void) zvol_remove_minor(zc->zc_name);
#endif
	return (err);
}

/*
 * innvl: {
 *     vdevs: {
 *         guid 1, guid 2, ...
 *     },
 *     func: POOL_INITIALIZE_{CANCEL|DO|SUSPEND}
 * }
 *
 * outnvl: {
 *     [func: EINVAL (if provided command type didn't make sense)],
 *     [vdevs: {
 *         guid1: errno, (see function body for possible errnos)
 *         ...
 *     }]
 * }
 *
 */
static int
zfs_ioc_pool_initialize(const char *poolname, nvlist_t *innvl, nvlist_t *outnvl)
{
	spa_t *spa;
	int error;

	error = spa_open(poolname, &spa, FTAG);
	if (error != 0)
		return (error);

	uint64_t cmd_type;
	if (nvlist_lookup_uint64(innvl, ZPOOL_INITIALIZE_COMMAND,
	    &cmd_type) != 0) {
		spa_close(spa, FTAG);
		return (SET_ERROR(EINVAL));
	}
	if (!(cmd_type == POOL_INITIALIZE_CANCEL ||
	    cmd_type == POOL_INITIALIZE_DO ||
	    cmd_type == POOL_INITIALIZE_SUSPEND)) {
		spa_close(spa, FTAG);
		return (SET_ERROR(EINVAL));
	}

	nvlist_t *vdev_guids;
	if (nvlist_lookup_nvlist(innvl, ZPOOL_INITIALIZE_VDEVS,
	    &vdev_guids) != 0) {
		spa_close(spa, FTAG);
		return (SET_ERROR(EINVAL));
	}

	nvlist_t *vdev_errlist = fnvlist_alloc();
	int total_errors = 0;

	for (nvpair_t *pair = nvlist_next_nvpair(vdev_guids, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(vdev_guids, pair)) {
		uint64_t vdev_guid = fnvpair_value_uint64(pair);

		error = spa_vdev_initialize(spa, vdev_guid, cmd_type);
		if (error != 0) {
			char guid_as_str[MAXNAMELEN];

			(void) snprintf(guid_as_str, sizeof (guid_as_str),
			    "%llu", (unsigned long long)vdev_guid);
			fnvlist_add_int64(vdev_errlist, guid_as_str, error);
			total_errors++;
		}
	}
	if (fnvlist_size(vdev_errlist) > 0) {
		fnvlist_add_nvlist(outnvl, ZPOOL_INITIALIZE_VDEVS,
		    vdev_errlist);
	}
	fnvlist_free(vdev_errlist);

	spa_close(spa, FTAG);
	return (total_errors > 0 ? EINVAL : 0);
}

/*
 * fsname is name of dataset to rollback (to most recent snapshot)
 *
 * innvl may contain name of expected target snapshot
 *
 * outnvl: "target" -> name of most recent snapshot
 * }
 */
/* ARGSUSED */
static int
zfs_ioc_rollback(const char *fsname, nvlist_t *innvl, nvlist_t *outnvl)
{
	zfsvfs_t *zfsvfs;
	char *target = NULL;
	int error;

	(void) nvlist_lookup_string(innvl, "target", &target);
	if (target != NULL) {
		const char *cp = strchr(target, '@');

		/*
		 * The snap name must contain an @, and the part after it must
		 * contain only valid characters.
		 */
		if (cp == NULL ||
		    zfs_component_namecheck(cp + 1, NULL, NULL) != 0)
			return (SET_ERROR(EINVAL));
	}

	if (getzfsvfs(fsname, &zfsvfs) == 0) {
		dsl_dataset_t *ds;

		ds = dmu_objset_ds(zfsvfs->z_os);
		error = zfs_suspend_fs(zfsvfs);
		if (error == 0) {
			int resume_err;

			error = dsl_dataset_rollback(fsname, target, zfsvfs,
			    outnvl);
			resume_err = zfs_resume_fs(zfsvfs, ds);
			error = error ? error : resume_err;
		}
#ifdef illumos
		VFS_RELE(zfsvfs->z_vfs);
#else
		vfs_unbusy(zfsvfs->z_vfs);
#endif
	} else {
		error = dsl_dataset_rollback(fsname, target, NULL, outnvl);
	}
	return (error);
}

static int
recursive_unmount(const char *fsname, void *arg)
{
	const char *snapname = arg;
	char fullname[ZFS_MAX_DATASET_NAME_LEN];

	(void) snprintf(fullname, sizeof (fullname), "%s@%s", fsname, snapname);
	zfs_unmount_snap(fullname);

	return (0);
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
	objset_t *os;
	dmu_objset_type_t ost;
	boolean_t recursive = zc->zc_cookie & 1;
	char *at;
	boolean_t allow_mounted = B_TRUE;
	int err;

#ifdef __FreeBSD__
	allow_mounted = (zc->zc_cookie & 2) != 0;
#endif

	/* "zfs rename" from and to ...%recv datasets should both fail */
	zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
	zc->zc_value[sizeof (zc->zc_value) - 1] = '\0';
	if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0 ||
	    dataset_namecheck(zc->zc_value, NULL, NULL) != 0 ||
	    strchr(zc->zc_name, '%') || strchr(zc->zc_value, '%'))
		return (SET_ERROR(EINVAL));

	err = dmu_objset_hold(zc->zc_name, FTAG, &os);
	if (err != 0)
		return (err);
	ost = dmu_objset_type(os);
	dmu_objset_rele(os, FTAG);

	at = strchr(zc->zc_name, '@');
	if (at != NULL) {
		/* snaps must be in same fs */
		int error;

		if (strncmp(zc->zc_name, zc->zc_value, at - zc->zc_name + 1))
			return (SET_ERROR(EXDEV));
		*at = '\0';
		if (ost == DMU_OST_ZFS && !allow_mounted) {
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
#ifdef illumos
		if (ost == DMU_OST_ZVOL)
			(void) zvol_remove_minor(zc->zc_name);
#endif
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
			if (err = zfs_secpolicy_write_perms(dsname,
			    ZFS_DELEG_PERM_USERPROP, cr))
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

			if (err = zfs_secpolicy_write_perms(dsname, perm, cr))
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

	case ZFS_PROP_RECORDSIZE:
		/* Record sizes above 128k need the feature to be enabled */
		if (nvpair_value_uint64(pair, &intval) == 0 &&
		    intval > SPA_OLD_MAXBLOCKSIZE) {
			spa_t *spa;

			/*
			 * We don't allow setting the property above 1MB,
			 * unless the tunable has been changed.
			 */
			if (intval > zfs_max_recordsize ||
			    intval > SPA_MAXBLOCKSIZE)
				return (SET_ERROR(ERANGE));

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

	case ZFS_PROP_DNODESIZE:
		/* Dnode sizes above 512 need the feature to be enabled */
		if (nvpair_value_uint64(pair, &intval) == 0 &&
		    intval != ZFS_DNSIZE_LEGACY) {
			spa_t *spa;

			if ((err = spa_open(dsname, &spa, FTAG)) != 0)
				return (err);

			if (!spa_feature_is_enabled(spa,
			    SPA_FEATURE_LARGE_DNODE)) {
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

	case ZFS_PROP_CHECKSUM:
	case ZFS_PROP_DEDUP:
	{
		spa_feature_t feature;
		spa_t *spa;

		/* dedup feature version checks */
		if (prop == ZFS_PROP_DEDUP &&
		    zfs_earlier_version(dsname, SPA_VERSION_DEDUP))
			return (SET_ERROR(ENOTSUP));

		if (nvpair_value_uint64(pair, &intval) != 0)
			return (SET_ERROR(EINVAL));

		/* check prop value is enabled in features */
		feature = zio_checksum_to_feature(intval & ZIO_CHECKSUM_MASK);
		if (feature == SPA_FEATURE_NONE)
			break;

		if ((err = spa_open(dsname, &spa, FTAG)) != 0)
			return (err);
		/*
		 * Salted checksums are not supported on root pools.
		 */
		if (spa_bootfs(spa) != 0 &&
		    intval < ZIO_CHECKSUM_FUNCTIONS &&
		    (zio_checksum_table[intval].ci_flags &
		    ZCHECKSUM_FLAG_SALTED)) {
			spa_close(spa, FTAG);
			return (SET_ERROR(ERANGE));
		}
		if (!spa_feature_is_enabled(spa, feature)) {
			spa_close(spa, FTAG);
			return (SET_ERROR(ENOTSUP));
		}
		spa_close(spa, FTAG);
		break;
	}
	}

	return (zfs_secpolicy_setprop(dsname, prop, pair, CRED()));
}

/*
 * Checks for a race condition to make sure we don't increment a feature flag
 * multiple times.
 */
static int
zfs_prop_activate_feature_check(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	spa_feature_t *featurep = arg;

	if (!spa_feature_is_active(spa, *featurep))
		return (0);
	else
		return (SET_ERROR(EBUSY));
}

/*
 * The callback invoked on feature activation in the sync task caused by
 * zfs_prop_activate_feature.
 */
static void
zfs_prop_activate_feature_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	spa_feature_t *featurep = arg;

	spa_feature_incr(spa, *featurep, tx);
}

/*
 * Activates a feature on a pool in response to a property setting. This
 * creates a new sync task which modifies the pool to reflect the feature
 * as being active.
 */
static int
zfs_prop_activate_feature(spa_t *spa, spa_feature_t feature)
{
	int err;

	/* EBUSY here indicates that the feature is already active */
	err = dsl_sync_task(spa_name(spa),
	    zfs_prop_activate_feature_check, zfs_prop_activate_feature_sync,
	    &feature, 2, ZFS_SPACE_CHECK_RESERVED);

	if (err != 0 && err != EBUSY)
		return (err);
	else
		return (0);
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

/*
 * Extract properties that cannot be set PRIOR to the receipt of a dataset.
 * For example, refquota cannot be set until after the receipt of a dataset,
 * because in replication streams, an older/earlier snapshot may exceed the
 * refquota.  We want to receive the older/earlier snapshot, but setting
 * refquota pre-receipt will set the dsl's ACTUAL quota, which will prevent
 * the older/earlier snapshot from being received (with EDQUOT).
 *
 * The ZFS test "zfs_receive_011_pos" demonstrates such a scenario.
 *
 * libzfs will need to be judicious handling errors encountered by props
 * extracted by this function.
 */
static nvlist_t *
extract_delay_props(nvlist_t *props)
{
	nvlist_t *delayprops;
	nvpair_t *nvp, *tmp;
	static const zfs_prop_t delayable[] = { ZFS_PROP_REFQUOTA, 0 };
	int i;

	VERIFY(nvlist_alloc(&delayprops, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	for (nvp = nvlist_next_nvpair(props, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(props, nvp)) {
		/*
		 * strcmp() is safe because zfs_prop_to_name() always returns
		 * a bounded string.
		 */
		for (i = 0; delayable[i] != 0; i++) {
			if (strcmp(zfs_prop_to_name(delayable[i]),
			    nvpair_name(nvp)) == 0) {
				break;
			}
		}
		if (delayable[i] != 0) {
			tmp = nvlist_prev_nvpair(props, nvp);
			VERIFY(nvlist_add_nvpair(delayprops, nvp) == 0);
			VERIFY(nvlist_remove_nvpair(props, nvp) == 0);
			nvp = tmp;
		}
	}

	if (nvlist_empty(delayprops)) {
		nvlist_free(delayprops);
		delayprops = NULL;
	}
	return (delayprops);
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
 * zc_resumable		if data is incomplete assume sender will resume
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
	nvlist_t *delayprops = NULL; /* sent properties applied post-receive */
	char *origin = NULL;
	char *tosnap;
	char tofs[ZFS_MAX_DATASET_NAME_LEN];
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
#ifdef illumos
	fp = getf(fd);
#else
	fget_read(curthread, fd, &cap_pread_rights, &fp);
#endif
	if (fp == NULL) {
		nvlist_free(props);
		return (SET_ERROR(EBADF));
	}

	errors = fnvlist_alloc();

	if (zc->zc_string[0])
		origin = zc->zc_string;

	error = dmu_recv_begin(tofs, tosnap,
	    &zc->zc_begin_record, force, zc->zc_resumable, origin, &drc);
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
			delayprops = extract_delay_props(props);
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
			    props, errors);
		}
	}

	off = fp->f_offset;
	error = dmu_recv_stream(&drc, fp, &off, zc->zc_cleanup_fd,
	    &zc->zc_action_handle);

	if (error == 0) {
		zfsvfs_t *zfsvfs = NULL;

		if (getzfsvfs(tofs, &zfsvfs) == 0) {
			/* online recv */
			dsl_dataset_t *ds;
			int end_err;

			ds = dmu_objset_ds(zfsvfs->z_os);
			error = zfs_suspend_fs(zfsvfs);
			/*
			 * If the suspend fails, then the recv_end will
			 * likely also fail, and clean up after itself.
			 */
			end_err = dmu_recv_end(&drc, zfsvfs);
			if (error == 0)
				error = zfs_resume_fs(zfsvfs, ds);
			error = error ? error : end_err;
#ifdef illumos
			VFS_RELE(zfsvfs->z_vfs);
#else
			vfs_unbusy(zfsvfs->z_vfs);
#endif
		} else {
			error = dmu_recv_end(&drc, NULL);
		}

		/* Set delayed properties now, after we're done receiving. */
		if (delayprops != NULL && error == 0) {
			(void) zfs_set_prop_nvlist(tofs, ZPROP_SRC_RECEIVED,
			    delayprops, errors);
		}
	}

	if (delayprops != NULL) {
		/*
		 * Merge delayed props back in with initial props, in case
		 * we're DEBUG and zfs_ioc_recv_inject_err is set (which means
		 * we have to make sure clear_received_props() includes
		 * the delayed properties).
		 *
		 * Since zfs_ioc_recv_inject_err is only in DEBUG kernels,
		 * using ASSERT() will be just like a VERIFY.
		 */
		ASSERT(nvlist_merge(props, delayprops, 0) == 0);
		nvlist_free(delayprops);
	}

	/*
	 * Now that all props, initial and delayed, are set, report the prop
	 * errors to the caller.
	 */
	if (zc->zc_nvlist_dst_size != 0 &&
	    (nvlist_smush(errors, zc->zc_nvlist_dst_size) != 0 ||
	    put_nvlist(zc, errors) != 0)) {
		/*
		 * Caller made zc->zc_nvlist_dst less than the minimum expected
		 * size or supplied an invalid address.
		 */
		props_error = SET_ERROR(EINVAL);
	}

	zc->zc_cookie = off - fp->f_offset;
	if (off >= 0 && off <= MAXOFFSET_T)
		fp->f_offset = off;

#ifdef	DEBUG
	if (zfs_ioc_recv_inject_err) {
		zfs_ioc_recv_inject_err = B_FALSE;
		error = 1;
	}
#endif

#ifdef __FreeBSD__
	if (error == 0)
		zvol_create_minors(tofs);
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
	boolean_t compressok = (zc->zc_flags & 0x4);

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

		error = dmu_send_estimate(tosnap, fromsnap, compressok,
		    &zc->zc_objset_type);

		if (fromsnap != NULL)
			dsl_dataset_rele(fromsnap, FTAG);
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
	} else {
		file_t *fp;

#ifdef illumos
		fp = getf(zc->zc_cookie);
#else
		fget_write(curthread, zc->zc_cookie, &cap_write_rights, &fp);
#endif
		if (fp == NULL)
			return (SET_ERROR(EBADF));

		off = fp->f_offset;
		error = dmu_send_obj(zc->zc_name, zc->zc_sendobj,
		    zc->zc_fromobj, embedok, large_block_ok, compressok,
#ifdef illumos
		    zc->zc_cookie, fp->f_vnode, &off);
#else
		    zc->zc_cookie, fp, &off);
#endif

		if (off >= 0 && off <= MAXOFFSET_T)
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
		    dsp->dsa_proc == curproc)
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
 *
 * outputs:
 * zc_string	name of conflicting snapshot, if there is one
 */
static int
zfs_ioc_promote(zfs_cmd_t *zc)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds, *ods;
	char origin[ZFS_MAX_DATASET_NAME_LEN];
	char *cp;
	int error;

	zc->zc_name[sizeof (zc->zc_name) - 1] = '\0';
	if (dataset_namecheck(zc->zc_name, NULL, NULL) != 0 ||
	    strchr(zc->zc_name, '%'))
		return (SET_ERROR(EINVAL));

	error = dsl_pool_hold(zc->zc_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, zc->zc_name, FTAG, &ds);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	if (!dsl_dir_is_clone(ds->ds_dir)) {
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (SET_ERROR(EINVAL));
	}

	error = dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(ds->ds_dir)->dd_origin_obj, FTAG, &ods);
	if (error != 0) {
		dsl_dataset_rele(ds, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	dsl_dataset_name(ods, origin);
	dsl_dataset_rele(ods, FTAG);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);

	/*
	 * We don't need to unmount *all* the origin fs's snapshots, but
	 * it's easier.
	 */
	cp = strchr(origin, '@');
	if (cp)
		*cp = '\0';
	(void) dmu_objset_find(origin,
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
	zfsvfs_t *zfsvfs;
	int error;

	if (zc->zc_objset_type >= ZFS_NUM_USERQUOTA_PROPS)
		return (SET_ERROR(EINVAL));

	error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error != 0)
		return (error);

	error = zfs_userspace_one(zfsvfs,
	    zc->zc_objset_type, zc->zc_value, zc->zc_guid, &zc->zc_cookie);
	zfsvfs_rele(zfsvfs, FTAG);

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
	zfsvfs_t *zfsvfs;
	int bufsize = zc->zc_nvlist_dst_size;

	if (bufsize <= 0)
		return (SET_ERROR(ENOMEM));

	int error = zfsvfs_hold(zc->zc_name, FTAG, &zfsvfs, B_FALSE);
	if (error != 0)
		return (error);

	void *buf = kmem_alloc(bufsize, KM_SLEEP);

	error = zfs_userspace_many(zfsvfs, zc->zc_objset_type, &zc->zc_cookie,
	    buf, &zc->zc_nvlist_dst_size);

	if (error == 0) {
		error = ddi_copyout(buf,
		    (void *)(uintptr_t)zc->zc_nvlist_dst,
		    zc->zc_nvlist_dst_size, zc->zc_iflags);
	}
	kmem_free(buf, bufsize);
	zfsvfs_rele(zfsvfs, FTAG);

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
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(zc->zc_name, &zfsvfs) == 0) {
		if (!dmu_objset_userused_enabled(zfsvfs->z_os)) {
			/*
			 * If userused is not enabled, it may be because the
			 * objset needs to be closed & reopened (to grow the
			 * objset_phys_t).  Suspend/resume the fs will do that.
			 */
			dsl_dataset_t *ds, *newds;

			ds = dmu_objset_ds(zfsvfs->z_os);
			error = zfs_suspend_fs(zfsvfs);
			if (error == 0) {
				dmu_objset_refresh_ownership(ds, &newds,
				    zfsvfs);
				error = zfs_resume_fs(zfsvfs, newds);
			}
		}
		if (error == 0)
			error = dmu_objset_userspace_upgrade(zfsvfs->z_os);
#ifdef illumos
		VFS_RELE(zfsvfs->z_vfs);
#else
		vfs_unbusy(zfsvfs->z_vfs);
#endif
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

#ifdef illumos
/*
 * We don't want to have a hard dependency
 * against some special symbols in sharefs
 * nfs, and smbsrv.  Determine them if needed when
 * the first file system is shared.
 * Neither sharefs, nfs or smbsrv are unloadable modules.
 */
int (*znfsexport_fs)(void *arg);
int (*zshare_fs)(enum sharefs_sys_op, share_t *, uint32_t);
int (*zsmbexport_fs)(void *arg, boolean_t add_share);

int zfs_nfsshare_inited;
int zfs_smbshare_inited;

ddi_modhandle_t nfs_mod;
ddi_modhandle_t sharefs_mod;
ddi_modhandle_t smbsrv_mod;
#endif	/* illumos */
kmutex_t zfs_share_lock;

#ifdef illumos
static int
zfs_init_sharefs()
{
	int error;

	ASSERT(MUTEX_HELD(&zfs_share_lock));
	/* Both NFS and SMB shares also require sharetab support. */
	if (sharefs_mod == NULL && ((sharefs_mod =
	    ddi_modopen("fs/sharefs",
	    KRTLD_MODE_FIRST, &error)) == NULL)) {
		return (SET_ERROR(ENOSYS));
	}
	if (zshare_fs == NULL && ((zshare_fs =
	    (int (*)(enum sharefs_sys_op, share_t *, uint32_t))
	    ddi_modsym(sharefs_mod, "sharefs_impl", &error)) == NULL)) {
		return (SET_ERROR(ENOSYS));
	}
	return (0);
}
#endif	/* illumos */

static int
zfs_ioc_share(zfs_cmd_t *zc)
{
#ifdef illumos
	int error;
	int opcode;

	switch (zc->zc_share.z_sharetype) {
	case ZFS_SHARE_NFS:
	case ZFS_UNSHARE_NFS:
		if (zfs_nfsshare_inited == 0) {
			mutex_enter(&zfs_share_lock);
			if (nfs_mod == NULL && ((nfs_mod = ddi_modopen("fs/nfs",
			    KRTLD_MODE_FIRST, &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (SET_ERROR(ENOSYS));
			}
			if (znfsexport_fs == NULL &&
			    ((znfsexport_fs = (int (*)(void *))
			    ddi_modsym(nfs_mod,
			    "nfs_export", &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (SET_ERROR(ENOSYS));
			}
			error = zfs_init_sharefs();
			if (error != 0) {
				mutex_exit(&zfs_share_lock);
				return (SET_ERROR(ENOSYS));
			}
			zfs_nfsshare_inited = 1;
			mutex_exit(&zfs_share_lock);
		}
		break;
	case ZFS_SHARE_SMB:
	case ZFS_UNSHARE_SMB:
		if (zfs_smbshare_inited == 0) {
			mutex_enter(&zfs_share_lock);
			if (smbsrv_mod == NULL && ((smbsrv_mod =
			    ddi_modopen("drv/smbsrv",
			    KRTLD_MODE_FIRST, &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (SET_ERROR(ENOSYS));
			}
			if (zsmbexport_fs == NULL && ((zsmbexport_fs =
			    (int (*)(void *, boolean_t))ddi_modsym(smbsrv_mod,
			    "smb_server_share", &error)) == NULL)) {
				mutex_exit(&zfs_share_lock);
				return (SET_ERROR(ENOSYS));
			}
			error = zfs_init_sharefs();
			if (error != 0) {
				mutex_exit(&zfs_share_lock);
				return (SET_ERROR(ENOSYS));
			}
			zfs_smbshare_inited = 1;
			mutex_exit(&zfs_share_lock);
		}
		break;
	default:
		return (SET_ERROR(EINVAL));
	}

	switch (zc->zc_share.z_sharetype) {
	case ZFS_SHARE_NFS:
	case ZFS_UNSHARE_NFS:
		if (error =
		    znfsexport_fs((void *)
		    (uintptr_t)zc->zc_share.z_exportdata))
			return (error);
		break;
	case ZFS_SHARE_SMB:
	case ZFS_UNSHARE_SMB:
		if (error = zsmbexport_fs((void *)
		    (uintptr_t)zc->zc_share.z_exportdata,
		    zc->zc_share.z_sharetype == ZFS_SHARE_SMB ?
		    B_TRUE: B_FALSE)) {
			return (error);
		}
		break;
	}

	opcode = (zc->zc_share.z_sharetype == ZFS_SHARE_NFS ||
	    zc->zc_share.z_sharetype == ZFS_SHARE_SMB) ?
	    SHAREFS_ADD : SHAREFS_REMOVE;

	/*
	 * Add or remove share from sharetab
	 */
	error = zshare_fs(opcode,
	    (void *)(uintptr_t)zc->zc_share.z_sharedata,
	    zc->zc_share.z_sharemax);

	return (error);

#else	/* !illumos */
	return (ENOSYS);
#endif	/* illumos */
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

	error = dmu_object_next(os, &zc->zc_obj, B_FALSE,
	    dsl_dataset_phys(os->os_dsl_dataset)->ds_prev_snap_txg);

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

#ifdef illumos
	fp = getf(zc->zc_cookie);
#else
	fget_write(curthread, zc->zc_cookie, &cap_write_rights, &fp);
#endif
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	off = fp->f_offset;

#ifdef illumos
	error = dmu_diff(zc->zc_name, zc->zc_value, fp->f_vnode, &off);
#else
	error = dmu_diff(zc->zc_name, zc->zc_value, fp, &off);
#endif

	if (off >= 0 && off <= MAXOFFSET_T)
		fp->f_offset = off;
	releasef(zc->zc_cookie);

	return (error);
}

#ifdef illumos
/*
 * Remove all ACL files in shares dir
 */
static int
zfs_smb_acl_purge(znode_t *dzp)
{
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	int error;

	for (zap_cursor_init(&zc, zfsvfs->z_os, dzp->z_id);
	    (error = zap_cursor_retrieve(&zc, &zap)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((error = VOP_REMOVE(ZTOV(dzp), zap.za_name, kcred,
		    NULL, 0)) != 0)
			break;
	}
	zap_cursor_fini(&zc);
	return (error);
}
#endif	/* illumos */

static int
zfs_ioc_smb_acl(zfs_cmd_t *zc)
{
#ifdef illumos
	vnode_t *vp;
	znode_t *dzp;
	vnode_t *resourcevp = NULL;
	znode_t *sharedir;
	zfsvfs_t *zfsvfs;
	nvlist_t *nvlist;
	char *src, *target;
	vattr_t vattr;
	vsecattr_t vsec;
	int error = 0;

	if ((error = lookupname(zc->zc_value, UIO_SYSSPACE,
	    NO_FOLLOW, NULL, &vp)) != 0)
		return (error);

	/* Now make sure mntpnt and dataset are ZFS */

	if (strcmp(vp->v_vfsp->mnt_stat.f_fstypename, "zfs") != 0 ||
	    (strcmp((char *)refstr_value(vp->v_vfsp->vfs_resource),
	    zc->zc_name) != 0)) {
		VN_RELE(vp);
		return (SET_ERROR(EINVAL));
	}

	dzp = VTOZ(vp);
	zfsvfs = dzp->z_zfsvfs;
	ZFS_ENTER(zfsvfs);

	/*
	 * Create share dir if its missing.
	 */
	mutex_enter(&zfsvfs->z_lock);
	if (zfsvfs->z_shares_dir == 0) {
		dmu_tx_t *tx;

		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, TRUE,
		    ZFS_SHARES_DIR);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error != 0) {
			dmu_tx_abort(tx);
		} else {
			error = zfs_create_share_dir(zfsvfs, tx);
			dmu_tx_commit(tx);
		}
		if (error != 0) {
			mutex_exit(&zfsvfs->z_lock);
			VN_RELE(vp);
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}
	mutex_exit(&zfsvfs->z_lock);

	ASSERT(zfsvfs->z_shares_dir);
	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &sharedir)) != 0) {
		VN_RELE(vp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	switch (zc->zc_cookie) {
	case ZFS_SMB_ACL_ADD:
		vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
		vattr.va_type = VREG;
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
			VN_RELE(ZTOV(sharedir));
			ZFS_EXIT(zfsvfs);
			return (error);
		}
		if (nvlist_lookup_string(nvlist, ZFS_SMB_ACL_SRC, &src) ||
		    nvlist_lookup_string(nvlist, ZFS_SMB_ACL_TARGET,
		    &target)) {
			VN_RELE(vp);
			VN_RELE(ZTOV(sharedir));
			ZFS_EXIT(zfsvfs);
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

	ZFS_EXIT(zfsvfs);

	return (error);
#else	/* !illumos */
	return (EOPNOTSUPP);
#endif	/* illumos */
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
	nvpair_t *pair;
	nvlist_t *holds;
	int cleanup_fd = -1;
	int error;
	minor_t minor = 0;

	error = nvlist_lookup_nvlist(args, "holds", &holds);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	/* make sure the user didn't pass us any invalid (empty) tags */
	for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		char *htag;

		error = nvpair_value_string(pair, &htag);
		if (error != 0)
			return (SET_ERROR(error));

		if (strlen(htag) == 0)
			return (SET_ERROR(EINVAL));
	}

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

static int
zfs_ioc_jail(zfs_cmd_t *zc)
{

	return (zone_dataset_attach(curthread->td_ucred, zc->zc_name,
	    (int)zc->zc_jailid));
}

static int
zfs_ioc_unjail(zfs_cmd_t *zc)
{

	return (zone_dataset_detach(curthread->td_ucred, zc->zc_name,
	    (int)zc->zc_jailid));
}

/*
 * innvl: {
 *     "fd" -> file descriptor to write stream to (int32)
 *     (optional) "fromsnap" -> full snap name to send an incremental from
 *     (optional) "largeblockok" -> (value ignored)
 *         indicates that blocks > 128KB are permitted
 *     (optional) "embedok" -> (value ignored)
 *         presence indicates DRR_WRITE_EMBEDDED records are permitted
 *     (optional) "compressok" -> (value ignored)
 *         presence indicates compressed DRR_WRITE records are permitted
 *     (optional) "resume_object" and "resume_offset" -> (uint64)
 *         if present, resume send stream from specified object and offset.
 * }
 *
 * outnvl is unused
 */
/* ARGSUSED */
static int
zfs_ioc_send_new(const char *snapname, nvlist_t *innvl, nvlist_t *outnvl)
{
	file_t *fp;
	int error;
	offset_t off;
	char *fromname = NULL;
	int fd;
	boolean_t largeblockok;
	boolean_t embedok;
	boolean_t compressok;
	uint64_t resumeobj = 0;
	uint64_t resumeoff = 0;

	error = nvlist_lookup_int32(innvl, "fd", &fd);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	(void) nvlist_lookup_string(innvl, "fromsnap", &fromname);

	largeblockok = nvlist_exists(innvl, "largeblockok");
	embedok = nvlist_exists(innvl, "embedok");
	compressok = nvlist_exists(innvl, "compressok");

	(void) nvlist_lookup_uint64(innvl, "resume_object", &resumeobj);
	(void) nvlist_lookup_uint64(innvl, "resume_offset", &resumeoff);

#ifdef illumos
	file_t *fp = getf(fd);
#else
	fget_write(curthread, fd, &cap_write_rights, &fp);
#endif
	if (fp == NULL)
		return (SET_ERROR(EBADF));

	off = fp->f_offset;
	error = dmu_send(snapname, fromname, embedok, largeblockok, compressok,
#ifdef illumos
	    fd, resumeobj, resumeoff, fp->f_vnode, &off);
#else
	    fd, resumeobj, resumeoff, fp, &off);
#endif

#ifdef illumos
	if (VOP_SEEK(fp->f_vnode, fp->f_offset, &off, NULL) == 0)
		fp->f_offset = off;
#else
	fp->f_offset = off;
#endif

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
 *     (optional) "largeblockok" -> (value ignored)
 *         indicates that blocks > 128KB are permitted
 *     (optional) "embedok" -> (value ignored)
 *         presence indicates DRR_WRITE_EMBEDDED records are permitted
 *     (optional) "compressok" -> (value ignored)
 *         presence indicates compressed DRR_WRITE records are permitted
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
	boolean_t compressok;
	uint64_t space;

	error = dsl_pool_hold(snapname, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, snapname, FTAG, &tosnap);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	compressok = nvlist_exists(innvl, "compressok");

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
			error = dmu_send_estimate(tosnap, fromsnap, compressok,
			    &space);
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
			    frombm.zbm_creation_txg, compressok, &space);
		} else {
			/*
			 * from is not properly formatted as a snapshot or
			 * bookmark
			 */
			error = SET_ERROR(EINVAL);
			goto out;
		}
	} else {
		/*
		 * If estimating the size of a full send, use dmu_send_estimate.
		 */
		error = dmu_send_estimate(tosnap, NULL, compressok, &space);
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

	zfs_ioctl_register("remap", ZFS_IOC_REMAP,
	    zfs_ioc_remap, zfs_secpolicy_remap, DATASET_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_FALSE, B_TRUE);

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

	zfs_ioctl_register("channel_program", ZFS_IOC_CHANNEL_PROGRAM,
	    zfs_ioc_channel_program, zfs_secpolicy_config,
	    POOL_NAME, POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE,
	    B_TRUE);

	zfs_ioctl_register("zpool_checkpoint", ZFS_IOC_POOL_CHECKPOINT,
	    zfs_ioc_pool_checkpoint, zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("zpool_discard_checkpoint",
	    ZFS_IOC_POOL_DISCARD_CHECKPOINT, zfs_ioc_pool_discard_checkpoint,
	    zfs_secpolicy_config, POOL_NAME,
	    POOL_CHECK_SUSPENDED | POOL_CHECK_READONLY, B_TRUE, B_TRUE);

	zfs_ioctl_register("initialize", ZFS_IOC_POOL_INITIALIZE,
	    zfs_ioc_pool_initialize, zfs_secpolicy_config, POOL_NAME,
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
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_EXPORT, zfs_ioc_pool_export,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_STATS, zfs_ioc_pool_stats,
	    zfs_secpolicy_read, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_GET_PROPS, zfs_ioc_pool_get_props,
	    zfs_secpolicy_read, B_FALSE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_ERROR_LOG, zfs_ioc_error_log,
	    zfs_secpolicy_inject, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool(ZFS_IOC_DSOBJ_TO_DSNAME,
	    zfs_ioc_dsobj_to_dsname,
	    zfs_secpolicy_diff, B_FALSE, POOL_CHECK_NONE);
	zfs_ioctl_register_pool(ZFS_IOC_POOL_GET_HISTORY,
	    zfs_ioc_pool_get_history,
	    zfs_secpolicy_config, B_FALSE, POOL_CHECK_SUSPENDED);

	zfs_ioctl_register_pool(ZFS_IOC_POOL_IMPORT, zfs_ioc_pool_import,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_NONE);

	zfs_ioctl_register_pool(ZFS_IOC_CLEAR, zfs_ioc_clear,
	    zfs_secpolicy_config, B_TRUE, POOL_CHECK_READONLY);
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

#ifdef __FreeBSD__
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_JAIL, zfs_ioc_jail,
	    zfs_secpolicy_config, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_UNJAIL, zfs_ioc_unjail,
	    zfs_secpolicy_config, POOL_CHECK_NONE);
	zfs_ioctl_register("fbsd_nextboot", ZFS_IOC_NEXTBOOT,
	    zfs_ioc_nextboot, zfs_secpolicy_config, NO_NAME,
	    POOL_CHECK_NONE, B_FALSE, B_FALSE);
#endif
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

/*
 * Find a free minor number.
 */
minor_t
zfsdev_minor_alloc(void)
{
	static minor_t last_minor;
	minor_t m;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	for (m = last_minor + 1; m != last_minor; m++) {
		if (m > ZFSDEV_MAX_MINOR)
			m = 1;
		if (ddi_get_soft_state(zfsdev_state, m) == NULL) {
			last_minor = m;
			return (m);
		}
	}

	return (0);
}

static int
zfs_ctldev_init(struct cdev *devp)
{
	minor_t minor;
	zfs_soft_state_t *zs;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	minor = zfsdev_minor_alloc();
	if (minor == 0)
		return (SET_ERROR(ENXIO));

	if (ddi_soft_state_zalloc(zfsdev_state, minor) != DDI_SUCCESS)
		return (SET_ERROR(EAGAIN));

	devfs_set_cdevpriv((void *)(uintptr_t)minor, zfsdev_close);

	zs = ddi_get_soft_state(zfsdev_state, minor);
	zs->zss_type = ZSST_CTLDEV;
	zfs_onexit_init((zfs_onexit_t **)&zs->zss_data);

	return (0);
}

static void
zfs_ctldev_destroy(zfs_onexit_t *zo, minor_t minor)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	zfs_onexit_destroy(zo);
	ddi_soft_state_free(zfsdev_state, minor);
}

void *
zfsdev_get_soft_state(minor_t minor, enum zfs_soft_state_type which)
{
	zfs_soft_state_t *zp;

	zp = ddi_get_soft_state(zfsdev_state, minor);
	if (zp == NULL || zp->zss_type != which)
		return (NULL);

	return (zp->zss_data);
}

static int
zfsdev_open(struct cdev *devp, int flag, int mode, struct thread *td)
{
	int error = 0;

#ifdef illumos
	if (getminor(*devp) != 0)
		return (zvol_open(devp, flag, otyp, cr));
#endif

	/* This is the control device. Allocate a new minor if requested. */
	if (flag & FEXCL) {
		mutex_enter(&spa_namespace_lock);
		error = zfs_ctldev_init(devp);
		mutex_exit(&spa_namespace_lock);
	}

	return (error);
}

static void
zfsdev_close(void *data)
{
	zfs_onexit_t *zo;
	minor_t minor = (minor_t)(uintptr_t)data;

	if (minor == 0)
		return;

	mutex_enter(&spa_namespace_lock);
	zo = zfsdev_get_soft_state(minor, ZSST_CTLDEV);
	if (zo == NULL) {
		mutex_exit(&spa_namespace_lock);
		return;
	}
	zfs_ctldev_destroy(zo, minor);
	mutex_exit(&spa_namespace_lock);
}

static int
zfsdev_ioctl(struct cdev *dev, u_long zcmd, caddr_t arg, int flag,
    struct thread *td)
{
	zfs_cmd_t *zc;
	uint_t vecnum;
	int error, rc, len;
#ifdef illumos
	minor_t minor = getminor(dev);
#else
	zfs_iocparm_t *zc_iocparm;
	int cflag, cmd, oldvecnum;
	boolean_t newioc, compat;
	void *compat_zc = NULL;
	cred_t *cr = td->td_ucred;
#endif
	const zfs_ioc_vec_t *vec;
	char *saved_poolname = NULL;
	nvlist_t *innvl = NULL;

	cflag = ZFS_CMD_COMPAT_NONE;
	compat = B_FALSE;
	newioc = B_TRUE;	/* "new" style (zfs_iocparm_t) ioctl */

	len = IOCPARM_LEN(zcmd);
	vecnum = cmd = zcmd & 0xff;

	/*
	 * Check if we are talking to supported older binaries
	 * and translate zfs_cmd if necessary
	 */
	if (len != sizeof(zfs_iocparm_t)) {
		newioc = B_FALSE;
		compat = B_TRUE;

		vecnum = cmd;

		switch (len) {
		case sizeof(zfs_cmd_zcmd_t):
			cflag = ZFS_CMD_COMPAT_LZC;
			break;
		case sizeof(zfs_cmd_deadman_t):
			cflag = ZFS_CMD_COMPAT_DEADMAN;
			break;
		case sizeof(zfs_cmd_v28_t):
			cflag = ZFS_CMD_COMPAT_V28;
			break;
		case sizeof(zfs_cmd_v15_t):
			if (cmd >= sizeof(zfs_ioctl_v15_to_v28) /
			    sizeof(zfs_ioctl_v15_to_v28[0]))
				return (EINVAL);

			cflag = ZFS_CMD_COMPAT_V15;
			vecnum = zfs_ioctl_v15_to_v28[cmd];

			/*
			 * Return without further handling
			 * if the command is blacklisted.
			 */
			if (vecnum == ZFS_IOC_COMPAT_PASS)
				return (0);
			else if (vecnum == ZFS_IOC_COMPAT_FAIL)
				return (ENOTSUP);
			break;
		default:
			return (EINVAL);
		}
	}

#ifdef illumos
	vecnum = cmd - ZFS_IOC_FIRST;
	ASSERT3U(getmajor(dev), ==, ddi_driver_major(zfs_dip));
#endif

	if (vecnum >= sizeof (zfs_ioc_vec) / sizeof (zfs_ioc_vec[0]))
		return (SET_ERROR(EINVAL));
	vec = &zfs_ioc_vec[vecnum];

	zc = kmem_zalloc(sizeof(zfs_cmd_t), KM_SLEEP);

#ifdef illumos
	error = ddi_copyin((void *)arg, zc, sizeof (zfs_cmd_t), flag);
	if (error != 0) {
		error = SET_ERROR(EFAULT);
		goto out;
	}
#else	/* !illumos */
	bzero(zc, sizeof(zfs_cmd_t));

	if (newioc) {
		zc_iocparm = (void *)arg;

		switch (zc_iocparm->zfs_ioctl_version) {
		case ZFS_IOCVER_CURRENT:
			if (zc_iocparm->zfs_cmd_size != sizeof(zfs_cmd_t)) {
				error = SET_ERROR(EINVAL);
				goto out;
			}
			break;
		case ZFS_IOCVER_INLANES:
			if (zc_iocparm->zfs_cmd_size != sizeof(zfs_cmd_inlanes_t)) {
				error = SET_ERROR(EFAULT);
				goto out;
			}
			compat = B_TRUE;
			cflag = ZFS_CMD_COMPAT_INLANES;
			break;
		case ZFS_IOCVER_RESUME:
			if (zc_iocparm->zfs_cmd_size != sizeof(zfs_cmd_resume_t)) {
				error = SET_ERROR(EFAULT);
				goto out;
			}
			compat = B_TRUE;
			cflag = ZFS_CMD_COMPAT_RESUME;
			break;
		case ZFS_IOCVER_EDBP:
			if (zc_iocparm->zfs_cmd_size != sizeof(zfs_cmd_edbp_t)) {
				error = SET_ERROR(EFAULT);
				goto out;
			}
			compat = B_TRUE;
			cflag = ZFS_CMD_COMPAT_EDBP;
			break;
		case ZFS_IOCVER_ZCMD:
			if (zc_iocparm->zfs_cmd_size > sizeof(zfs_cmd_t) ||
			    zc_iocparm->zfs_cmd_size < sizeof(zfs_cmd_zcmd_t)) {
				error = SET_ERROR(EFAULT);
				goto out;
			}
			compat = B_TRUE;
			cflag = ZFS_CMD_COMPAT_ZCMD;
			break;
		default:
			error = SET_ERROR(EINVAL);
			goto out;
			/* NOTREACHED */
		}

		if (compat) {
			ASSERT(sizeof(zfs_cmd_t) >= zc_iocparm->zfs_cmd_size);
			compat_zc = kmem_zalloc(sizeof(zfs_cmd_t), KM_SLEEP);
			bzero(compat_zc, sizeof(zfs_cmd_t));

			error = ddi_copyin((void *)(uintptr_t)zc_iocparm->zfs_cmd,
			    compat_zc, zc_iocparm->zfs_cmd_size, flag);
			if (error != 0) {
				error = SET_ERROR(EFAULT);
				goto out;
			}
		} else {
			error = ddi_copyin((void *)(uintptr_t)zc_iocparm->zfs_cmd,
			    zc, zc_iocparm->zfs_cmd_size, flag);
			if (error != 0) {
				error = SET_ERROR(EFAULT);
				goto out;
			}
		}
	}

	if (compat) {
		if (newioc) {
			ASSERT(compat_zc != NULL);
			zfs_cmd_compat_get(zc, compat_zc, cflag);
		} else {
			ASSERT(compat_zc == NULL);
			zfs_cmd_compat_get(zc, arg, cflag);
		}
		oldvecnum = vecnum;
		error = zfs_ioctl_compat_pre(zc, &vecnum, cflag);
		if (error != 0)
			goto out;
		if (oldvecnum != vecnum)
			vec = &zfs_ioc_vec[vecnum];
	}
#endif	/* !illumos */

	zc->zc_iflags = flag & FKIOCTL;
	if (zc->zc_nvlist_src_size != 0) {
		error = get_nvlist(zc->zc_nvlist_src, zc->zc_nvlist_src_size,
		    zc->zc_iflags, &innvl);
		if (error != 0)
			goto out;
	}

	/* rewrite innvl for backwards compatibility */
	if (compat)
		innvl = zfs_ioctl_compat_innvl(zc, innvl, vecnum, cflag);

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

	if (error == 0)
		error = vec->zvec_secpolicy(zc, innvl, cr);

	if (error != 0)
		goto out;

	/* legacy ioctls can modify zc_name */
	len = strcspn(zc->zc_name, "/@#") + 1;
	saved_poolname = kmem_alloc(len, KM_SLEEP);
	(void) strlcpy(saved_poolname, zc->zc_name, len);

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
		error = vec->zvec_func(zc->zc_name, innvl, outnvl);

		/*
		 * Some commands can partially execute, modfiy state, and still
		 * return an error.  In these cases, attempt to record what
		 * was modified.
		 */
		if ((error == 0 ||
		    (cmd == ZFS_IOC_CHANNEL_PROGRAM && error != EINVAL)) &&
		    vec->zvec_allow_log &&
		    spa_open(zc->zc_name, &spa, FTAG) == 0) {
			if (!nvlist_empty(outnvl)) {
				fnvlist_add_nvlist(lognv, ZPOOL_HIST_OUTPUT_NVL,
				    outnvl);
			}
			if (error != 0) {
				fnvlist_add_int64(lognv, ZPOOL_HIST_ERRNO,
				    error);
			}
			(void) spa_history_log_nvl(spa, lognv);
			spa_close(spa, FTAG);
		}
		fnvlist_free(lognv);

		/* rewrite outnvl for backwards compatibility */
		if (compat)
			outnvl = zfs_ioctl_compat_outnvl(zc, outnvl, vecnum,
			    cflag);

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
		error = vec->zvec_legacy_func(zc);
	}

out:
	nvlist_free(innvl);

#ifdef illumos
	rc = ddi_copyout(zc, (void *)arg, sizeof (zfs_cmd_t), flag);
	if (error == 0 && rc != 0)
		error = SET_ERROR(EFAULT);
#else
	if (compat) {
		zfs_ioctl_compat_post(zc, cmd, cflag);
		if (newioc) {
			ASSERT(compat_zc != NULL);
			ASSERT(sizeof(zfs_cmd_t) >= zc_iocparm->zfs_cmd_size);

			zfs_cmd_compat_put(zc, compat_zc, vecnum, cflag);
			rc = ddi_copyout(compat_zc,
			    (void *)(uintptr_t)zc_iocparm->zfs_cmd,
			    zc_iocparm->zfs_cmd_size, flag);
			if (error == 0 && rc != 0)
				error = SET_ERROR(EFAULT);
			kmem_free(compat_zc, sizeof (zfs_cmd_t));
		} else {
			zfs_cmd_compat_put(zc, arg, vecnum, cflag);
		}
	} else {
		ASSERT(newioc);

		rc = ddi_copyout(zc, (void *)(uintptr_t)zc_iocparm->zfs_cmd,
		    sizeof (zfs_cmd_t), flag);
		if (error == 0 && rc != 0)
			error = SET_ERROR(EFAULT);
	}
#endif
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
	return (error);
}

#ifdef illumos
static int
zfs_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(dip, "zfs", S_IFCHR, 0,
	    DDI_PSEUDO, 0) == DDI_FAILURE)
		return (DDI_FAILURE);

	zfs_dip = dip;

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

static int
zfs_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (spa_busy() || zfs_busy() || zvol_busy())
		return (DDI_FAILURE);

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	zfs_dip = NULL;

	ddi_prop_remove_all(dip);
	ddi_remove_minor_node(dip, NULL);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
zfs_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = zfs_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}
#endif	/* illumos */

/*
 * OK, so this is a little weird.
 *
 * /dev/zfs is the control node, i.e. minor 0.
 * /dev/zvol/[r]dsk/pool/dataset are the zvols, minor > 0.
 *
 * /dev/zfs has basically nothing to do except serve up ioctls,
 * so most of the standard driver entry points are in zvol.c.
 */
#ifdef illumos
static struct cb_ops zfs_cb_ops = {
	zfsdev_open,	/* open */
	zfsdev_close,	/* close */
	zvol_strategy,	/* strategy */
	nodev,		/* print */
	zvol_dump,	/* dump */
	zvol_read,	/* read */
	zvol_write,	/* write */
	zfsdev_ioctl,	/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* prop_op */
	NULL,		/* streamtab */
	D_NEW | D_MP | D_64BIT,		/* Driver compatibility flag */
	CB_REV,		/* version */
	nodev,		/* async read */
	nodev,		/* async write */
};

static struct dev_ops zfs_dev_ops = {
	DEVO_REV,	/* version */
	0,		/* refcnt */
	zfs_info,	/* info */
	nulldev,	/* identify */
	nulldev,	/* probe */
	zfs_attach,	/* attach */
	zfs_detach,	/* detach */
	nodev,		/* reset */
	&zfs_cb_ops,	/* driver operations */
	NULL,		/* no bus operations */
	NULL,		/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv zfs_modldrv = {
	&mod_driverops,
	"ZFS storage pool",
	&zfs_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&zfs_modlfs,
	(void *)&zfs_modldrv,
	NULL
};
#endif	/* illumos */

static struct cdevsw zfs_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	zfsdev_open,
	.d_ioctl =	zfsdev_ioctl,
	.d_name =	ZFS_DEV_NAME
};

static void
zfs_allow_log_destroy(void *arg)
{
	char *poolname = arg;
	strfree(poolname);
}

static void
zfsdev_init(void)
{
	zfsdev = make_dev(&zfs_cdevsw, 0x0, UID_ROOT, GID_OPERATOR, 0666,
	    ZFS_DEV_NAME);
}

static void
zfsdev_fini(void)
{
	if (zfsdev != NULL)
		destroy_dev(zfsdev);
}

static struct root_hold_token *zfs_root_token;
struct proc *zfsproc;

#ifdef illumos
int
_init(void)
{
	int error;

	spa_init(FREAD | FWRITE);
	zfs_init();
	zvol_init();
	zfs_ioctl_init();

	if ((error = mod_install(&modlinkage)) != 0) {
		zvol_fini();
		zfs_fini();
		spa_fini();
		return (error);
	}

	tsd_create(&zfs_fsyncer_key, NULL);
	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
	tsd_create(&zfs_allow_log_key, zfs_allow_log_destroy);

	error = ldi_ident_from_mod(&modlinkage, &zfs_li);
	ASSERT(error == 0);
	mutex_init(&zfs_share_lock, NULL, MUTEX_DEFAULT, NULL);

	return (0);
}

int
_fini(void)
{
	int error;

	if (spa_busy() || zfs_busy() || zvol_busy() || zio_injection_enabled)
		return (SET_ERROR(EBUSY));

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	zvol_fini();
	zfs_fini();
	spa_fini();
	if (zfs_nfsshare_inited)
		(void) ddi_modclose(nfs_mod);
	if (zfs_smbshare_inited)
		(void) ddi_modclose(smbsrv_mod);
	if (zfs_nfsshare_inited || zfs_smbshare_inited)
		(void) ddi_modclose(sharefs_mod);

	tsd_destroy(&zfs_fsyncer_key);
	ldi_ident_release(zfs_li);
	zfs_li = NULL;
	mutex_destroy(&zfs_share_lock);

	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
#endif	/* illumos */

static int zfs__init(void);
static int zfs__fini(void);
static void zfs_shutdown(void *, int);

static eventhandler_tag zfs_shutdown_event_tag;

#ifdef __FreeBSD__
#define ZFS_MIN_KSTACK_PAGES 4
#endif

int
zfs__init(void)
{

#ifdef __FreeBSD__
#if KSTACK_PAGES < ZFS_MIN_KSTACK_PAGES
	printf("ZFS NOTICE: KSTACK_PAGES is %d which could result in stack "
	    "overflow panic!\nPlease consider adding "
	    "'options KSTACK_PAGES=%d' to your kernel config\n", KSTACK_PAGES,
	    ZFS_MIN_KSTACK_PAGES);
#endif
#endif
	zfs_root_token = root_mount_hold("ZFS");

	mutex_init(&zfs_share_lock, NULL, MUTEX_DEFAULT, NULL);

	spa_init(FREAD | FWRITE);
	zfs_init();
	zvol_init();
	zfs_ioctl_init();

	tsd_create(&zfs_fsyncer_key, NULL);
	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
	tsd_create(&zfs_allow_log_key, zfs_allow_log_destroy);
	tsd_create(&zfs_geom_probe_vdev_key, NULL);

	printf("ZFS storage pool version: features support (" SPA_VERSION_STRING ")\n");
	root_mount_rel(zfs_root_token);

	zfsdev_init();

	return (0);
}

int
zfs__fini(void)
{
	if (spa_busy() || zfs_busy() || zvol_busy() ||
	    zio_injection_enabled) {
		return (EBUSY);
	}

	zfsdev_fini();
	zvol_fini();
	zfs_fini();
	spa_fini();

	tsd_destroy(&zfs_fsyncer_key);
	tsd_destroy(&rrw_tsd_key);
	tsd_destroy(&zfs_allow_log_key);

	mutex_destroy(&zfs_share_lock);

	return (0);
}

static void
zfs_shutdown(void *arg __unused, int howto __unused)
{

	/*
	 * ZFS fini routines can not properly work in a panic-ed system.
	 */
	if (panicstr == NULL)
		(void)zfs__fini();
}


static int
zfs_modevent(module_t mod, int type, void *unused __unused)
{
	int err;

	switch (type) {
	case MOD_LOAD:
		err = zfs__init();
		if (err == 0)
			zfs_shutdown_event_tag = EVENTHANDLER_REGISTER(
			    shutdown_post_sync, zfs_shutdown, NULL,
			    SHUTDOWN_PRI_FIRST);
		return (err);
	case MOD_UNLOAD:
		err = zfs__fini();
		if (err == 0 && zfs_shutdown_event_tag != NULL)
			EVENTHANDLER_DEREGISTER(shutdown_post_sync,
			    zfs_shutdown_event_tag);
		return (err);
	case MOD_SHUTDOWN:
		return (0);
	default:
		break;
	}
	return (EOPNOTSUPP);
}

static moduledata_t zfs_mod = {
	"zfsctrl",
	zfs_modevent,
	0
};
DECLARE_MODULE(zfsctrl, zfs_mod, SI_SUB_VFS, SI_ORDER_ANY);
MODULE_VERSION(zfsctrl, 1);
MODULE_DEPEND(zfsctrl, opensolaris, 1, 1, 1);
MODULE_DEPEND(zfsctrl, krpc, 1, 1, 1);
MODULE_DEPEND(zfsctrl, acl_nfs4, 1, 1, 1);
