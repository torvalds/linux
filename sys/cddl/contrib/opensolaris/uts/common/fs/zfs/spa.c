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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2015, Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2016 Toomas Soome <tsoome@me.com>
 * Copyright 2018 Joyent, Inc.
 * Copyright (c) 2017 Datto Inc.
 * Copyright 2018 OmniOS Community Edition (OmniOSce) Association.
 */

/*
 * SPA: Storage Pool Allocator
 *
 * This file contains all the routines used when modifying on-disk SPA state.
 * This includes opening, importing, destroying, exporting a pool, and syncing a
 * pool.
 */

#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/ddt.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_removal.h>
#include <sys/vdev_indirect_mapping.h>
#include <sys/vdev_indirect_births.h>
#include <sys/vdev_initialize.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/bpobj.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_objset.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/fs/zfs.h>
#include <sys/arc.h>
#include <sys/callb.h>
#include <sys/spa_boot.h>
#include <sys/zfs_ioctl.h>
#include <sys/dsl_scan.h>
#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_userhold.h>
#include <sys/zfeature.h>
#include <sys/zvol.h>
#include <sys/trim_map.h>
#include <sys/abd.h>

#ifdef	_KERNEL
#include <sys/callb.h>
#include <sys/cpupart.h>
#include <sys/zone.h>
#endif	/* _KERNEL */

#include "zfs_prop.h"
#include "zfs_comutil.h"

/* Check hostid on import? */
static int check_hostid = 1;

/*
 * The interval, in seconds, at which failed configuration cache file writes
 * should be retried.
 */
int zfs_ccw_retry_interval = 300;

SYSCTL_DECL(_vfs_zfs);
SYSCTL_INT(_vfs_zfs, OID_AUTO, check_hostid, CTLFLAG_RWTUN, &check_hostid, 0,
    "Check hostid on import?");
TUNABLE_INT("vfs.zfs.ccw_retry_interval", &zfs_ccw_retry_interval);
SYSCTL_INT(_vfs_zfs, OID_AUTO, ccw_retry_interval, CTLFLAG_RW,
    &zfs_ccw_retry_interval, 0,
    "Configuration cache file write, retry after failure, interval (seconds)");

typedef enum zti_modes {
	ZTI_MODE_FIXED,			/* value is # of threads (min 1) */
	ZTI_MODE_BATCH,			/* cpu-intensive; value is ignored */
	ZTI_MODE_NULL,			/* don't create a taskq */
	ZTI_NMODES
} zti_modes_t;

#define	ZTI_P(n, q)	{ ZTI_MODE_FIXED, (n), (q) }
#define	ZTI_BATCH	{ ZTI_MODE_BATCH, 0, 1 }
#define	ZTI_NULL	{ ZTI_MODE_NULL, 0, 0 }

#define	ZTI_N(n)	ZTI_P(n, 1)
#define	ZTI_ONE		ZTI_N(1)

typedef struct zio_taskq_info {
	zti_modes_t zti_mode;
	uint_t zti_value;
	uint_t zti_count;
} zio_taskq_info_t;

static const char *const zio_taskq_types[ZIO_TASKQ_TYPES] = {
	"issue", "issue_high", "intr", "intr_high"
};

/*
 * This table defines the taskq settings for each ZFS I/O type. When
 * initializing a pool, we use this table to create an appropriately sized
 * taskq. Some operations are low volume and therefore have a small, static
 * number of threads assigned to their taskqs using the ZTI_N(#) or ZTI_ONE
 * macros. Other operations process a large amount of data; the ZTI_BATCH
 * macro causes us to create a taskq oriented for throughput. Some operations
 * are so high frequency and short-lived that the taskq itself can become a a
 * point of lock contention. The ZTI_P(#, #) macro indicates that we need an
 * additional degree of parallelism specified by the number of threads per-
 * taskq and the number of taskqs; when dispatching an event in this case, the
 * particular taskq is chosen at random.
 *
 * The different taskq priorities are to handle the different contexts (issue
 * and interrupt) and then to reserve threads for ZIO_PRIORITY_NOW I/Os that
 * need to be handled with minimum delay.
 */
const zio_taskq_info_t zio_taskqs[ZIO_TYPES][ZIO_TASKQ_TYPES] = {
	/* ISSUE	ISSUE_HIGH	INTR		INTR_HIGH */
	{ ZTI_ONE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* NULL */
	{ ZTI_N(8),	ZTI_NULL,	ZTI_P(12, 8),	ZTI_NULL }, /* READ */
	{ ZTI_BATCH,	ZTI_N(5),	ZTI_N(8),	ZTI_N(5) }, /* WRITE */
	{ ZTI_P(12, 8),	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* FREE */
	{ ZTI_ONE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* CLAIM */
	{ ZTI_ONE,	ZTI_NULL,	ZTI_ONE,	ZTI_NULL }, /* IOCTL */
};

static void spa_sync_version(void *arg, dmu_tx_t *tx);
static void spa_sync_props(void *arg, dmu_tx_t *tx);
static boolean_t spa_has_active_shared_spare(spa_t *spa);
static int spa_load_impl(spa_t *spa, spa_import_type_t type, char **ereport);
static void spa_vdev_resilver_done(spa_t *spa);

uint_t		zio_taskq_batch_pct = 75;	/* 1 thread per cpu in pset */
#ifdef PSRSET_BIND
id_t		zio_taskq_psrset_bind = PS_NONE;
#endif
#ifdef SYSDC
boolean_t	zio_taskq_sysdc = B_TRUE;	/* use SDC scheduling class */
uint_t		zio_taskq_basedc = 80;		/* base duty cycle */
#endif

boolean_t	spa_create_process = B_TRUE;	/* no process ==> no sysdc */
extern int	zfs_sync_pass_deferred_free;

/*
 * Report any spa_load_verify errors found, but do not fail spa_load.
 * This is used by zdb to analyze non-idle pools.
 */
boolean_t	spa_load_verify_dryrun = B_FALSE;

/*
 * This (illegal) pool name is used when temporarily importing a spa_t in order
 * to get the vdev stats associated with the imported devices.
 */
#define	TRYIMPORT_NAME	"$import"

/*
 * For debugging purposes: print out vdev tree during pool import.
 */
int	spa_load_print_vdev_tree = B_FALSE;

/*
 * A non-zero value for zfs_max_missing_tvds means that we allow importing
 * pools with missing top-level vdevs. This is strictly intended for advanced
 * pool recovery cases since missing data is almost inevitable. Pools with
 * missing devices can only be imported read-only for safety reasons, and their
 * fail-mode will be automatically set to "continue".
 *
 * With 1 missing vdev we should be able to import the pool and mount all
 * datasets. User data that was not modified after the missing device has been
 * added should be recoverable. This means that snapshots created prior to the
 * addition of that device should be completely intact.
 *
 * With 2 missing vdevs, some datasets may fail to mount since there are
 * dataset statistics that are stored as regular metadata. Some data might be
 * recoverable if those vdevs were added recently.
 *
 * With 3 or more missing vdevs, the pool is severely damaged and MOS entries
 * may be missing entirely. Chances of data recovery are very low. Note that
 * there are also risks of performing an inadvertent rewind as we might be
 * missing all the vdevs with the latest uberblocks.
 */
uint64_t	zfs_max_missing_tvds = 0;

/*
 * The parameters below are similar to zfs_max_missing_tvds but are only
 * intended for a preliminary open of the pool with an untrusted config which
 * might be incomplete or out-dated.
 *
 * We are more tolerant for pools opened from a cachefile since we could have
 * an out-dated cachefile where a device removal was not registered.
 * We could have set the limit arbitrarily high but in the case where devices
 * are really missing we would want to return the proper error codes; we chose
 * SPA_DVAS_PER_BP - 1 so that some copies of the MOS would still be available
 * and we get a chance to retrieve the trusted config.
 */
uint64_t	zfs_max_missing_tvds_cachefile = SPA_DVAS_PER_BP - 1;

/*
 * In the case where config was assembled by scanning device paths (/dev/dsks
 * by default) we are less tolerant since all the existing devices should have
 * been detected and we want spa_load to return the right error codes.
 */
uint64_t	zfs_max_missing_tvds_scan = 0;


SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_load_print_vdev_tree, CTLFLAG_RWTUN,
    &spa_load_print_vdev_tree, 0,
    "print out vdev tree during pool import");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, max_missing_tvds, CTLFLAG_RWTUN,
    &zfs_max_missing_tvds, 0,
    "allow importing pools with missing top-level vdevs");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, max_missing_tvds_cachefile, CTLFLAG_RWTUN,
    &zfs_max_missing_tvds_cachefile, 0,
    "allow importing pools with missing top-level vdevs in cache file");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, max_missing_tvds_scan, CTLFLAG_RWTUN,
    &zfs_max_missing_tvds_scan, 0,
    "allow importing pools with missing top-level vdevs during scan");

/*
 * Debugging aid that pauses spa_sync() towards the end.
 */
boolean_t	zfs_pause_spa_sync = B_FALSE;

/*
 * ==========================================================================
 * SPA properties routines
 * ==========================================================================
 */

/*
 * Add a (source=src, propname=propval) list to an nvlist.
 */
static void
spa_prop_add_list(nvlist_t *nvl, zpool_prop_t prop, char *strval,
    uint64_t intval, zprop_source_t src)
{
	const char *propname = zpool_prop_to_name(prop);
	nvlist_t *propval;

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_SOURCE, src) == 0);

	if (strval != NULL)
		VERIFY(nvlist_add_string(propval, ZPROP_VALUE, strval) == 0);
	else
		VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, intval) == 0);

	VERIFY(nvlist_add_nvlist(nvl, propname, propval) == 0);
	nvlist_free(propval);
}

/*
 * Get property values from the spa configuration.
 */
static void
spa_prop_get_config(spa_t *spa, nvlist_t **nvp)
{
	vdev_t *rvd = spa->spa_root_vdev;
	dsl_pool_t *pool = spa->spa_dsl_pool;
	uint64_t size, alloc, cap, version;
	zprop_source_t src = ZPROP_SRC_NONE;
	spa_config_dirent_t *dp;
	metaslab_class_t *mc = spa_normal_class(spa);

	ASSERT(MUTEX_HELD(&spa->spa_props_lock));

	if (rvd != NULL) {
		alloc = metaslab_class_get_alloc(spa_normal_class(spa));
		size = metaslab_class_get_space(spa_normal_class(spa));
		spa_prop_add_list(*nvp, ZPOOL_PROP_NAME, spa_name(spa), 0, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_SIZE, NULL, size, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_ALLOCATED, NULL, alloc, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_FREE, NULL,
		    size - alloc, src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_CHECKPOINT, NULL,
		    spa->spa_checkpoint_info.sci_dspace, src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_FRAGMENTATION, NULL,
		    metaslab_class_fragmentation(mc), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_EXPANDSZ, NULL,
		    metaslab_class_expandable_space(mc), src);
		spa_prop_add_list(*nvp, ZPOOL_PROP_READONLY, NULL,
		    (spa_mode(spa) == FREAD), src);

		cap = (size == 0) ? 0 : (alloc * 100 / size);
		spa_prop_add_list(*nvp, ZPOOL_PROP_CAPACITY, NULL, cap, src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_DEDUPRATIO, NULL,
		    ddt_get_pool_dedup_ratio(spa), src);

		spa_prop_add_list(*nvp, ZPOOL_PROP_HEALTH, NULL,
		    rvd->vdev_state, src);

		version = spa_version(spa);
		if (version == zpool_prop_default_numeric(ZPOOL_PROP_VERSION))
			src = ZPROP_SRC_DEFAULT;
		else
			src = ZPROP_SRC_LOCAL;
		spa_prop_add_list(*nvp, ZPOOL_PROP_VERSION, NULL, version, src);
	}

	if (pool != NULL) {
		/*
		 * The $FREE directory was introduced in SPA_VERSION_DEADLISTS,
		 * when opening pools before this version freedir will be NULL.
		 */
		if (pool->dp_free_dir != NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_FREEING, NULL,
			    dsl_dir_phys(pool->dp_free_dir)->dd_used_bytes,
			    src);
		} else {
			spa_prop_add_list(*nvp, ZPOOL_PROP_FREEING,
			    NULL, 0, src);
		}

		if (pool->dp_leak_dir != NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_LEAKED, NULL,
			    dsl_dir_phys(pool->dp_leak_dir)->dd_used_bytes,
			    src);
		} else {
			spa_prop_add_list(*nvp, ZPOOL_PROP_LEAKED,
			    NULL, 0, src);
		}
	}

	spa_prop_add_list(*nvp, ZPOOL_PROP_GUID, NULL, spa_guid(spa), src);

	if (spa->spa_comment != NULL) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_COMMENT, spa->spa_comment,
		    0, ZPROP_SRC_LOCAL);
	}

	if (spa->spa_root != NULL)
		spa_prop_add_list(*nvp, ZPOOL_PROP_ALTROOT, spa->spa_root,
		    0, ZPROP_SRC_LOCAL);

	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_BLOCKS)) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXBLOCKSIZE, NULL,
		    MIN(zfs_max_recordsize, SPA_MAXBLOCKSIZE), ZPROP_SRC_NONE);
	} else {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXBLOCKSIZE, NULL,
		    SPA_OLD_MAXBLOCKSIZE, ZPROP_SRC_NONE);
	}

	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_DNODE)) {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXDNODESIZE, NULL,
		    DNODE_MAX_SIZE, ZPROP_SRC_NONE);
	} else {
		spa_prop_add_list(*nvp, ZPOOL_PROP_MAXDNODESIZE, NULL,
		    DNODE_MIN_SIZE, ZPROP_SRC_NONE);
	}

	if ((dp = list_head(&spa->spa_config_list)) != NULL) {
		if (dp->scd_path == NULL) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_CACHEFILE,
			    "none", 0, ZPROP_SRC_LOCAL);
		} else if (strcmp(dp->scd_path, spa_config_path) != 0) {
			spa_prop_add_list(*nvp, ZPOOL_PROP_CACHEFILE,
			    dp->scd_path, 0, ZPROP_SRC_LOCAL);
		}
	}
}

/*
 * Get zpool property values.
 */
int
spa_prop_get(spa_t *spa, nvlist_t **nvp)
{
	objset_t *mos = spa->spa_meta_objset;
	zap_cursor_t zc;
	zap_attribute_t za;
	int err;

	VERIFY(nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	mutex_enter(&spa->spa_props_lock);

	/*
	 * Get properties from the spa config.
	 */
	spa_prop_get_config(spa, nvp);

	/* If no pool property object, no more prop to get. */
	if (mos == NULL || spa->spa_pool_props_object == 0) {
		mutex_exit(&spa->spa_props_lock);
		return (0);
	}

	/*
	 * Get properties from the MOS pool property object.
	 */
	for (zap_cursor_init(&zc, mos, spa->spa_pool_props_object);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t intval = 0;
		char *strval = NULL;
		zprop_source_t src = ZPROP_SRC_DEFAULT;
		zpool_prop_t prop;

		if ((prop = zpool_name_to_prop(za.za_name)) == ZPOOL_PROP_INVAL)
			continue;

		switch (za.za_integer_length) {
		case 8:
			/* integer property */
			if (za.za_first_integer !=
			    zpool_prop_default_numeric(prop))
				src = ZPROP_SRC_LOCAL;

			if (prop == ZPOOL_PROP_BOOTFS) {
				dsl_pool_t *dp;
				dsl_dataset_t *ds = NULL;

				dp = spa_get_dsl(spa);
				dsl_pool_config_enter(dp, FTAG);
				err = dsl_dataset_hold_obj(dp,
				    za.za_first_integer, FTAG, &ds);
				if (err != 0) {
					dsl_pool_config_exit(dp, FTAG);
					break;
				}

				strval = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN,
				    KM_SLEEP);
				dsl_dataset_name(ds, strval);
				dsl_dataset_rele(ds, FTAG);
				dsl_pool_config_exit(dp, FTAG);
			} else {
				strval = NULL;
				intval = za.za_first_integer;
			}

			spa_prop_add_list(*nvp, prop, strval, intval, src);

			if (strval != NULL)
				kmem_free(strval, ZFS_MAX_DATASET_NAME_LEN);

			break;

		case 1:
			/* string property */
			strval = kmem_alloc(za.za_num_integers, KM_SLEEP);
			err = zap_lookup(mos, spa->spa_pool_props_object,
			    za.za_name, 1, za.za_num_integers, strval);
			if (err) {
				kmem_free(strval, za.za_num_integers);
				break;
			}
			spa_prop_add_list(*nvp, prop, strval, 0, src);
			kmem_free(strval, za.za_num_integers);
			break;

		default:
			break;
		}
	}
	zap_cursor_fini(&zc);
	mutex_exit(&spa->spa_props_lock);
out:
	if (err && err != ENOENT) {
		nvlist_free(*nvp);
		*nvp = NULL;
		return (err);
	}

	return (0);
}

/*
 * Validate the given pool properties nvlist and modify the list
 * for the property values to be set.
 */
static int
spa_prop_validate(spa_t *spa, nvlist_t *props)
{
	nvpair_t *elem;
	int error = 0, reset_bootfs = 0;
	uint64_t objnum = 0;
	boolean_t has_feature = B_FALSE;

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		uint64_t intval;
		char *strval, *slash, *check, *fname;
		const char *propname = nvpair_name(elem);
		zpool_prop_t prop = zpool_name_to_prop(propname);

		switch (prop) {
		case ZPOOL_PROP_INVAL:
			if (!zpool_prop_feature(propname)) {
				error = SET_ERROR(EINVAL);
				break;
			}

			/*
			 * Sanitize the input.
			 */
			if (nvpair_type(elem) != DATA_TYPE_UINT64) {
				error = SET_ERROR(EINVAL);
				break;
			}

			if (nvpair_value_uint64(elem, &intval) != 0) {
				error = SET_ERROR(EINVAL);
				break;
			}

			if (intval != 0) {
				error = SET_ERROR(EINVAL);
				break;
			}

			fname = strchr(propname, '@') + 1;
			if (zfeature_lookup_name(fname, NULL) != 0) {
				error = SET_ERROR(EINVAL);
				break;
			}

			has_feature = B_TRUE;
			break;

		case ZPOOL_PROP_VERSION:
			error = nvpair_value_uint64(elem, &intval);
			if (!error &&
			    (intval < spa_version(spa) ||
			    intval > SPA_VERSION_BEFORE_FEATURES ||
			    has_feature))
				error = SET_ERROR(EINVAL);
			break;

		case ZPOOL_PROP_DELEGATION:
		case ZPOOL_PROP_AUTOREPLACE:
		case ZPOOL_PROP_LISTSNAPS:
		case ZPOOL_PROP_AUTOEXPAND:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && intval > 1)
				error = SET_ERROR(EINVAL);
			break;

		case ZPOOL_PROP_BOOTFS:
			/*
			 * If the pool version is less than SPA_VERSION_BOOTFS,
			 * or the pool is still being created (version == 0),
			 * the bootfs property cannot be set.
			 */
			if (spa_version(spa) < SPA_VERSION_BOOTFS) {
				error = SET_ERROR(ENOTSUP);
				break;
			}

			/*
			 * Make sure the vdev config is bootable
			 */
			if (!vdev_is_bootable(spa->spa_root_vdev)) {
				error = SET_ERROR(ENOTSUP);
				break;
			}

			reset_bootfs = 1;

			error = nvpair_value_string(elem, &strval);

			if (!error) {
				objset_t *os;
				uint64_t propval;

				if (strval == NULL || strval[0] == '\0') {
					objnum = zpool_prop_default_numeric(
					    ZPOOL_PROP_BOOTFS);
					break;
				}

				error = dmu_objset_hold(strval, FTAG, &os);
				if (error != 0)
					break;

				/*
				 * Must be ZPL, and its property settings
				 * must be supported.
				 */

				if (dmu_objset_type(os) != DMU_OST_ZFS) {
					error = SET_ERROR(ENOTSUP);
				} else if ((error =
				    dsl_prop_get_int_ds(dmu_objset_ds(os),
				    zfs_prop_to_name(ZFS_PROP_COMPRESSION),
				    &propval)) == 0 &&
				    !BOOTFS_COMPRESS_VALID(propval)) {
					error = SET_ERROR(ENOTSUP);
				} else {
					objnum = dmu_objset_id(os);
				}
				dmu_objset_rele(os, FTAG);
			}
			break;

		case ZPOOL_PROP_FAILUREMODE:
			error = nvpair_value_uint64(elem, &intval);
			if (!error && (intval < ZIO_FAILURE_MODE_WAIT ||
			    intval > ZIO_FAILURE_MODE_PANIC))
				error = SET_ERROR(EINVAL);

			/*
			 * This is a special case which only occurs when
			 * the pool has completely failed. This allows
			 * the user to change the in-core failmode property
			 * without syncing it out to disk (I/Os might
			 * currently be blocked). We do this by returning
			 * EIO to the caller (spa_prop_set) to trick it
			 * into thinking we encountered a property validation
			 * error.
			 */
			if (!error && spa_suspended(spa)) {
				spa->spa_failmode = intval;
				error = SET_ERROR(EIO);
			}
			break;

		case ZPOOL_PROP_CACHEFILE:
			if ((error = nvpair_value_string(elem, &strval)) != 0)
				break;

			if (strval[0] == '\0')
				break;

			if (strcmp(strval, "none") == 0)
				break;

			if (strval[0] != '/') {
				error = SET_ERROR(EINVAL);
				break;
			}

			slash = strrchr(strval, '/');
			ASSERT(slash != NULL);

			if (slash[1] == '\0' || strcmp(slash, "/.") == 0 ||
			    strcmp(slash, "/..") == 0)
				error = SET_ERROR(EINVAL);
			break;

		case ZPOOL_PROP_COMMENT:
			if ((error = nvpair_value_string(elem, &strval)) != 0)
				break;
			for (check = strval; *check != '\0'; check++) {
				/*
				 * The kernel doesn't have an easy isprint()
				 * check.  For this kernel check, we merely
				 * check ASCII apart from DEL.  Fix this if
				 * there is an easy-to-use kernel isprint().
				 */
				if (*check >= 0x7f) {
					error = SET_ERROR(EINVAL);
					break;
				}
			}
			if (strlen(strval) > ZPROP_MAX_COMMENT)
				error = E2BIG;
			break;

		case ZPOOL_PROP_DEDUPDITTO:
			if (spa_version(spa) < SPA_VERSION_DEDUP)
				error = SET_ERROR(ENOTSUP);
			else
				error = nvpair_value_uint64(elem, &intval);
			if (error == 0 &&
			    intval != 0 && intval < ZIO_DEDUPDITTO_MIN)
				error = SET_ERROR(EINVAL);
			break;
		}

		if (error)
			break;
	}

	if (!error && reset_bootfs) {
		error = nvlist_remove(props,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), DATA_TYPE_STRING);

		if (!error) {
			error = nvlist_add_uint64(props,
			    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), objnum);
		}
	}

	return (error);
}

void
spa_configfile_set(spa_t *spa, nvlist_t *nvp, boolean_t need_sync)
{
	char *cachefile;
	spa_config_dirent_t *dp;

	if (nvlist_lookup_string(nvp, zpool_prop_to_name(ZPOOL_PROP_CACHEFILE),
	    &cachefile) != 0)
		return;

	dp = kmem_alloc(sizeof (spa_config_dirent_t),
	    KM_SLEEP);

	if (cachefile[0] == '\0')
		dp->scd_path = spa_strdup(spa_config_path);
	else if (strcmp(cachefile, "none") == 0)
		dp->scd_path = NULL;
	else
		dp->scd_path = spa_strdup(cachefile);

	list_insert_head(&spa->spa_config_list, dp);
	if (need_sync)
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
}

int
spa_prop_set(spa_t *spa, nvlist_t *nvp)
{
	int error;
	nvpair_t *elem = NULL;
	boolean_t need_sync = B_FALSE;

	if ((error = spa_prop_validate(spa, nvp)) != 0)
		return (error);

	while ((elem = nvlist_next_nvpair(nvp, elem)) != NULL) {
		zpool_prop_t prop = zpool_name_to_prop(nvpair_name(elem));

		if (prop == ZPOOL_PROP_CACHEFILE ||
		    prop == ZPOOL_PROP_ALTROOT ||
		    prop == ZPOOL_PROP_READONLY)
			continue;

		if (prop == ZPOOL_PROP_VERSION || prop == ZPOOL_PROP_INVAL) {
			uint64_t ver;

			if (prop == ZPOOL_PROP_VERSION) {
				VERIFY(nvpair_value_uint64(elem, &ver) == 0);
			} else {
				ASSERT(zpool_prop_feature(nvpair_name(elem)));
				ver = SPA_VERSION_FEATURES;
				need_sync = B_TRUE;
			}

			/* Save time if the version is already set. */
			if (ver == spa_version(spa))
				continue;

			/*
			 * In addition to the pool directory object, we might
			 * create the pool properties object, the features for
			 * read object, the features for write object, or the
			 * feature descriptions object.
			 */
			error = dsl_sync_task(spa->spa_name, NULL,
			    spa_sync_version, &ver,
			    6, ZFS_SPACE_CHECK_RESERVED);
			if (error)
				return (error);
			continue;
		}

		need_sync = B_TRUE;
		break;
	}

	if (need_sync) {
		return (dsl_sync_task(spa->spa_name, NULL, spa_sync_props,
		    nvp, 6, ZFS_SPACE_CHECK_RESERVED));
	}

	return (0);
}

/*
 * If the bootfs property value is dsobj, clear it.
 */
void
spa_prop_clear_bootfs(spa_t *spa, uint64_t dsobj, dmu_tx_t *tx)
{
	if (spa->spa_bootfs == dsobj && spa->spa_pool_props_object != 0) {
		VERIFY(zap_remove(spa->spa_meta_objset,
		    spa->spa_pool_props_object,
		    zpool_prop_to_name(ZPOOL_PROP_BOOTFS), tx) == 0);
		spa->spa_bootfs = 0;
	}
}

/*ARGSUSED*/
static int
spa_change_guid_check(void *arg, dmu_tx_t *tx)
{
	uint64_t *newguid = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t vdev_state;

	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		int error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (SET_ERROR(error));
	}

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	vdev_state = rvd->vdev_state;
	spa_config_exit(spa, SCL_STATE, FTAG);

	if (vdev_state != VDEV_STATE_HEALTHY)
		return (SET_ERROR(ENXIO));

	ASSERT3U(spa_guid(spa), !=, *newguid);

	return (0);
}

static void
spa_change_guid_sync(void *arg, dmu_tx_t *tx)
{
	uint64_t *newguid = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	uint64_t oldguid;
	vdev_t *rvd = spa->spa_root_vdev;

	oldguid = spa_guid(spa);

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	rvd->vdev_guid = *newguid;
	rvd->vdev_guid_sum += (*newguid - oldguid);
	vdev_config_dirty(rvd);
	spa_config_exit(spa, SCL_STATE, FTAG);

	spa_history_log_internal(spa, "guid change", tx, "old=%llu new=%llu",
	    oldguid, *newguid);
}

/*
 * Change the GUID for the pool.  This is done so that we can later
 * re-import a pool built from a clone of our own vdevs.  We will modify
 * the root vdev's guid, our own pool guid, and then mark all of our
 * vdevs dirty.  Note that we must make sure that all our vdevs are
 * online when we do this, or else any vdevs that weren't present
 * would be orphaned from our pool.  We are also going to issue a
 * sysevent to update any watchers.
 */
int
spa_change_guid(spa_t *spa)
{
	int error;
	uint64_t guid;

	mutex_enter(&spa->spa_vdev_top_lock);
	mutex_enter(&spa_namespace_lock);
	guid = spa_generate_guid(NULL);

	error = dsl_sync_task(spa->spa_name, spa_change_guid_check,
	    spa_change_guid_sync, &guid, 5, ZFS_SPACE_CHECK_RESERVED);

	if (error == 0) {
		spa_write_cachefile(spa, B_FALSE, B_TRUE);
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_REGUID);
	}

	mutex_exit(&spa_namespace_lock);
	mutex_exit(&spa->spa_vdev_top_lock);

	return (error);
}

/*
 * ==========================================================================
 * SPA state manipulation (open/create/destroy/import/export)
 * ==========================================================================
 */

static int
spa_error_entry_compare(const void *a, const void *b)
{
	const spa_error_entry_t *sa = (const spa_error_entry_t *)a;
	const spa_error_entry_t *sb = (const spa_error_entry_t *)b;
	int ret;

	ret = memcmp(&sa->se_bookmark, &sb->se_bookmark,
	    sizeof (zbookmark_phys_t));

	return (AVL_ISIGN(ret));
}

/*
 * Utility function which retrieves copies of the current logs and
 * re-initializes them in the process.
 */
void
spa_get_errlists(spa_t *spa, avl_tree_t *last, avl_tree_t *scrub)
{
	ASSERT(MUTEX_HELD(&spa->spa_errlist_lock));

	bcopy(&spa->spa_errlist_last, last, sizeof (avl_tree_t));
	bcopy(&spa->spa_errlist_scrub, scrub, sizeof (avl_tree_t));

	avl_create(&spa->spa_errlist_scrub,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_last,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
}

static void
spa_taskqs_init(spa_t *spa, zio_type_t t, zio_taskq_type_t q)
{
	const zio_taskq_info_t *ztip = &zio_taskqs[t][q];
	enum zti_modes mode = ztip->zti_mode;
	uint_t value = ztip->zti_value;
	uint_t count = ztip->zti_count;
	spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];
	char name[32];
	uint_t flags = 0;
	boolean_t batch = B_FALSE;

	if (mode == ZTI_MODE_NULL) {
		tqs->stqs_count = 0;
		tqs->stqs_taskq = NULL;
		return;
	}

	ASSERT3U(count, >, 0);

	tqs->stqs_count = count;
	tqs->stqs_taskq = kmem_alloc(count * sizeof (taskq_t *), KM_SLEEP);

	switch (mode) {
	case ZTI_MODE_FIXED:
		ASSERT3U(value, >=, 1);
		value = MAX(value, 1);
		break;

	case ZTI_MODE_BATCH:
		batch = B_TRUE;
		flags |= TASKQ_THREADS_CPU_PCT;
		value = zio_taskq_batch_pct;
		break;

	default:
		panic("unrecognized mode for %s_%s taskq (%u:%u) in "
		    "spa_activate()",
		    zio_type_name[t], zio_taskq_types[q], mode, value);
		break;
	}

	for (uint_t i = 0; i < count; i++) {
		taskq_t *tq;

		if (count > 1) {
			(void) snprintf(name, sizeof (name), "%s_%s_%u",
			    zio_type_name[t], zio_taskq_types[q], i);
		} else {
			(void) snprintf(name, sizeof (name), "%s_%s",
			    zio_type_name[t], zio_taskq_types[q]);
		}

#ifdef SYSDC
		if (zio_taskq_sysdc && spa->spa_proc != &p0) {
			if (batch)
				flags |= TASKQ_DC_BATCH;

			tq = taskq_create_sysdc(name, value, 50, INT_MAX,
			    spa->spa_proc, zio_taskq_basedc, flags);
		} else {
#endif
			pri_t pri = maxclsyspri;
			/*
			 * The write issue taskq can be extremely CPU
			 * intensive.  Run it at slightly lower priority
			 * than the other taskqs.
			 * FreeBSD notes:
			 * - numerically higher priorities are lower priorities;
			 * - if priorities divided by four (RQ_PPQ) are equal
			 *   then a difference between them is insignificant.
			 */
			if (t == ZIO_TYPE_WRITE && q == ZIO_TASKQ_ISSUE)
#ifdef illumos
				pri--;
#else
				pri += 4;
#endif

			tq = taskq_create_proc(name, value, pri, 50,
			    INT_MAX, spa->spa_proc, flags);
#ifdef SYSDC
		}
#endif

		tqs->stqs_taskq[i] = tq;
	}
}

static void
spa_taskqs_fini(spa_t *spa, zio_type_t t, zio_taskq_type_t q)
{
	spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];

	if (tqs->stqs_taskq == NULL) {
		ASSERT0(tqs->stqs_count);
		return;
	}

	for (uint_t i = 0; i < tqs->stqs_count; i++) {
		ASSERT3P(tqs->stqs_taskq[i], !=, NULL);
		taskq_destroy(tqs->stqs_taskq[i]);
	}

	kmem_free(tqs->stqs_taskq, tqs->stqs_count * sizeof (taskq_t *));
	tqs->stqs_taskq = NULL;
}

/*
 * Dispatch a task to the appropriate taskq for the ZFS I/O type and priority.
 * Note that a type may have multiple discrete taskqs to avoid lock contention
 * on the taskq itself. In that case we choose which taskq at random by using
 * the low bits of gethrtime().
 */
void
spa_taskq_dispatch_ent(spa_t *spa, zio_type_t t, zio_taskq_type_t q,
    task_func_t *func, void *arg, uint_t flags, taskq_ent_t *ent)
{
	spa_taskqs_t *tqs = &spa->spa_zio_taskq[t][q];
	taskq_t *tq;

	ASSERT3P(tqs->stqs_taskq, !=, NULL);
	ASSERT3U(tqs->stqs_count, !=, 0);

	if (tqs->stqs_count == 1) {
		tq = tqs->stqs_taskq[0];
	} else {
#ifdef _KERNEL
		tq = tqs->stqs_taskq[(u_int)(sbinuptime() + curcpu) %
		    tqs->stqs_count];
#else
		tq = tqs->stqs_taskq[gethrtime() % tqs->stqs_count];
#endif
	}

	taskq_dispatch_ent(tq, func, arg, flags, ent);
}

static void
spa_create_zio_taskqs(spa_t *spa)
{
	for (int t = 0; t < ZIO_TYPES; t++) {
		for (int q = 0; q < ZIO_TASKQ_TYPES; q++) {
			spa_taskqs_init(spa, t, q);
		}
	}
}

#ifdef _KERNEL
#ifdef SPA_PROCESS
static void
spa_thread(void *arg)
{
	callb_cpr_t cprinfo;

	spa_t *spa = arg;
	user_t *pu = PTOU(curproc);

	CALLB_CPR_INIT(&cprinfo, &spa->spa_proc_lock, callb_generic_cpr,
	    spa->spa_name);

	ASSERT(curproc != &p0);
	(void) snprintf(pu->u_psargs, sizeof (pu->u_psargs),
	    "zpool-%s", spa->spa_name);
	(void) strlcpy(pu->u_comm, pu->u_psargs, sizeof (pu->u_comm));

#ifdef PSRSET_BIND
	/* bind this thread to the requested psrset */
	if (zio_taskq_psrset_bind != PS_NONE) {
		pool_lock();
		mutex_enter(&cpu_lock);
		mutex_enter(&pidlock);
		mutex_enter(&curproc->p_lock);

		if (cpupart_bind_thread(curthread, zio_taskq_psrset_bind,
		    0, NULL, NULL) == 0)  {
			curthread->t_bind_pset = zio_taskq_psrset_bind;
		} else {
			cmn_err(CE_WARN,
			    "Couldn't bind process for zfs pool \"%s\" to "
			    "pset %d\n", spa->spa_name, zio_taskq_psrset_bind);
		}

		mutex_exit(&curproc->p_lock);
		mutex_exit(&pidlock);
		mutex_exit(&cpu_lock);
		pool_unlock();
	}
#endif

#ifdef SYSDC
	if (zio_taskq_sysdc) {
		sysdc_thread_enter(curthread, 100, 0);
	}
#endif

	spa->spa_proc = curproc;
	spa->spa_did = curthread->t_did;

	spa_create_zio_taskqs(spa);

	mutex_enter(&spa->spa_proc_lock);
	ASSERT(spa->spa_proc_state == SPA_PROC_CREATED);

	spa->spa_proc_state = SPA_PROC_ACTIVE;
	cv_broadcast(&spa->spa_proc_cv);

	CALLB_CPR_SAFE_BEGIN(&cprinfo);
	while (spa->spa_proc_state == SPA_PROC_ACTIVE)
		cv_wait(&spa->spa_proc_cv, &spa->spa_proc_lock);
	CALLB_CPR_SAFE_END(&cprinfo, &spa->spa_proc_lock);

	ASSERT(spa->spa_proc_state == SPA_PROC_DEACTIVATE);
	spa->spa_proc_state = SPA_PROC_GONE;
	spa->spa_proc = &p0;
	cv_broadcast(&spa->spa_proc_cv);
	CALLB_CPR_EXIT(&cprinfo);	/* drops spa_proc_lock */

	mutex_enter(&curproc->p_lock);
	lwp_exit();
}
#endif	/* SPA_PROCESS */
#endif

/*
 * Activate an uninitialized pool.
 */
static void
spa_activate(spa_t *spa, int mode)
{
	ASSERT(spa->spa_state == POOL_STATE_UNINITIALIZED);

	spa->spa_state = POOL_STATE_ACTIVE;
	spa->spa_mode = mode;

	spa->spa_normal_class = metaslab_class_create(spa, zfs_metaslab_ops);
	spa->spa_log_class = metaslab_class_create(spa, zfs_metaslab_ops);

	/* Try to create a covering process */
	mutex_enter(&spa->spa_proc_lock);
	ASSERT(spa->spa_proc_state == SPA_PROC_NONE);
	ASSERT(spa->spa_proc == &p0);
	spa->spa_did = 0;

#ifdef SPA_PROCESS
	/* Only create a process if we're going to be around a while. */
	if (spa_create_process && strcmp(spa->spa_name, TRYIMPORT_NAME) != 0) {
		if (newproc(spa_thread, (caddr_t)spa, syscid, maxclsyspri,
		    NULL, 0) == 0) {
			spa->spa_proc_state = SPA_PROC_CREATED;
			while (spa->spa_proc_state == SPA_PROC_CREATED) {
				cv_wait(&spa->spa_proc_cv,
				    &spa->spa_proc_lock);
			}
			ASSERT(spa->spa_proc_state == SPA_PROC_ACTIVE);
			ASSERT(spa->spa_proc != &p0);
			ASSERT(spa->spa_did != 0);
		} else {
#ifdef _KERNEL
			cmn_err(CE_WARN,
			    "Couldn't create process for zfs pool \"%s\"\n",
			    spa->spa_name);
#endif
		}
	}
#endif	/* SPA_PROCESS */
	mutex_exit(&spa->spa_proc_lock);

	/* If we didn't create a process, we need to create our taskqs. */
	ASSERT(spa->spa_proc == &p0);
	if (spa->spa_proc == &p0) {
		spa_create_zio_taskqs(spa);
	}

	/*
	 * Start TRIM thread.
	 */
	trim_thread_create(spa);

	for (size_t i = 0; i < TXG_SIZE; i++) {
		spa->spa_txg_zio[i] = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL);
	}

	list_create(&spa->spa_config_dirty_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_config_dirty_node));
	list_create(&spa->spa_evicting_os_list, sizeof (objset_t),
	    offsetof(objset_t, os_evicting_node));
	list_create(&spa->spa_state_dirty_list, sizeof (vdev_t),
	    offsetof(vdev_t, vdev_state_dirty_node));

	txg_list_create(&spa->spa_vdev_txg_list, spa,
	    offsetof(struct vdev, vdev_txg_node));

	avl_create(&spa->spa_errlist_scrub,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
	avl_create(&spa->spa_errlist_last,
	    spa_error_entry_compare, sizeof (spa_error_entry_t),
	    offsetof(spa_error_entry_t, se_avl));
}

/*
 * Opposite of spa_activate().
 */
static void
spa_deactivate(spa_t *spa)
{
	ASSERT(spa->spa_sync_on == B_FALSE);
	ASSERT(spa->spa_dsl_pool == NULL);
	ASSERT(spa->spa_root_vdev == NULL);
	ASSERT(spa->spa_async_zio_root == NULL);
	ASSERT(spa->spa_state != POOL_STATE_UNINITIALIZED);

	/*
	 * Stop TRIM thread in case spa_unload() wasn't called directly
	 * before spa_deactivate().
	 */
	trim_thread_destroy(spa);

	spa_evicting_os_wait(spa);

	txg_list_destroy(&spa->spa_vdev_txg_list);

	list_destroy(&spa->spa_config_dirty_list);
	list_destroy(&spa->spa_evicting_os_list);
	list_destroy(&spa->spa_state_dirty_list);

	for (int t = 0; t < ZIO_TYPES; t++) {
		for (int q = 0; q < ZIO_TASKQ_TYPES; q++) {
			spa_taskqs_fini(spa, t, q);
		}
	}

	for (size_t i = 0; i < TXG_SIZE; i++) {
		ASSERT3P(spa->spa_txg_zio[i], !=, NULL);
		VERIFY0(zio_wait(spa->spa_txg_zio[i]));
		spa->spa_txg_zio[i] = NULL;
	}

	metaslab_class_destroy(spa->spa_normal_class);
	spa->spa_normal_class = NULL;

	metaslab_class_destroy(spa->spa_log_class);
	spa->spa_log_class = NULL;

	/*
	 * If this was part of an import or the open otherwise failed, we may
	 * still have errors left in the queues.  Empty them just in case.
	 */
	spa_errlog_drain(spa);

	avl_destroy(&spa->spa_errlist_scrub);
	avl_destroy(&spa->spa_errlist_last);

	spa->spa_state = POOL_STATE_UNINITIALIZED;

	mutex_enter(&spa->spa_proc_lock);
	if (spa->spa_proc_state != SPA_PROC_NONE) {
		ASSERT(spa->spa_proc_state == SPA_PROC_ACTIVE);
		spa->spa_proc_state = SPA_PROC_DEACTIVATE;
		cv_broadcast(&spa->spa_proc_cv);
		while (spa->spa_proc_state == SPA_PROC_DEACTIVATE) {
			ASSERT(spa->spa_proc != &p0);
			cv_wait(&spa->spa_proc_cv, &spa->spa_proc_lock);
		}
		ASSERT(spa->spa_proc_state == SPA_PROC_GONE);
		spa->spa_proc_state = SPA_PROC_NONE;
	}
	ASSERT(spa->spa_proc == &p0);
	mutex_exit(&spa->spa_proc_lock);

#ifdef SPA_PROCESS
	/*
	 * We want to make sure spa_thread() has actually exited the ZFS
	 * module, so that the module can't be unloaded out from underneath
	 * it.
	 */
	if (spa->spa_did != 0) {
		thread_join(spa->spa_did);
		spa->spa_did = 0;
	}
#endif	/* SPA_PROCESS */
}

/*
 * Verify a pool configuration, and construct the vdev tree appropriately.  This
 * will create all the necessary vdevs in the appropriate layout, with each vdev
 * in the CLOSED state.  This will prep the pool before open/creation/import.
 * All vdev validation is done by the vdev_alloc() routine.
 */
static int
spa_config_parse(spa_t *spa, vdev_t **vdp, nvlist_t *nv, vdev_t *parent,
    uint_t id, int atype)
{
	nvlist_t **child;
	uint_t children;
	int error;

	if ((error = vdev_alloc(spa, vdp, nv, parent, id, atype)) != 0)
		return (error);

	if ((*vdp)->vdev_ops->vdev_op_leaf)
		return (0);

	error = nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children);

	if (error == ENOENT)
		return (0);

	if (error) {
		vdev_free(*vdp);
		*vdp = NULL;
		return (SET_ERROR(EINVAL));
	}

	for (int c = 0; c < children; c++) {
		vdev_t *vd;
		if ((error = spa_config_parse(spa, &vd, child[c], *vdp, c,
		    atype)) != 0) {
			vdev_free(*vdp);
			*vdp = NULL;
			return (error);
		}
	}

	ASSERT(*vdp != NULL);

	return (0);
}

/*
 * Opposite of spa_load().
 */
static void
spa_unload(spa_t *spa)
{
	int i;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_load_note(spa, "UNLOADING");

	/*
	 * Stop TRIM thread.
	 */
	trim_thread_destroy(spa);

	/*
	 * Stop async tasks.
	 */
	spa_async_suspend(spa);

	if (spa->spa_root_vdev) {
		vdev_initialize_stop_all(spa->spa_root_vdev,
		    VDEV_INITIALIZE_ACTIVE);
	}

	/*
	 * Stop syncing.
	 */
	if (spa->spa_sync_on) {
		txg_sync_stop(spa->spa_dsl_pool);
		spa->spa_sync_on = B_FALSE;
	}

	/*
	 * Even though vdev_free() also calls vdev_metaslab_fini, we need
	 * to call it earlier, before we wait for async i/o to complete.
	 * This ensures that there is no async metaslab prefetching, by
	 * calling taskq_wait(mg_taskq).
	 */
	if (spa->spa_root_vdev != NULL) {
		spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);
		for (int c = 0; c < spa->spa_root_vdev->vdev_children; c++)
			vdev_metaslab_fini(spa->spa_root_vdev->vdev_child[c]);
		spa_config_exit(spa, SCL_ALL, spa);
	}

	/*
	 * Wait for any outstanding async I/O to complete.
	 */
	if (spa->spa_async_zio_root != NULL) {
		for (int i = 0; i < max_ncpus; i++)
			(void) zio_wait(spa->spa_async_zio_root[i]);
		kmem_free(spa->spa_async_zio_root, max_ncpus * sizeof (void *));
		spa->spa_async_zio_root = NULL;
	}

	if (spa->spa_vdev_removal != NULL) {
		spa_vdev_removal_destroy(spa->spa_vdev_removal);
		spa->spa_vdev_removal = NULL;
	}

	if (spa->spa_condense_zthr != NULL) {
		ASSERT(!zthr_isrunning(spa->spa_condense_zthr));
		zthr_destroy(spa->spa_condense_zthr);
		spa->spa_condense_zthr = NULL;
	}

	if (spa->spa_checkpoint_discard_zthr != NULL) {
		ASSERT(!zthr_isrunning(spa->spa_checkpoint_discard_zthr));
		zthr_destroy(spa->spa_checkpoint_discard_zthr);
		spa->spa_checkpoint_discard_zthr = NULL;
	}

	spa_condense_fini(spa);

	bpobj_close(&spa->spa_deferred_bpobj);

	spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);

	/*
	 * Close all vdevs.
	 */
	if (spa->spa_root_vdev)
		vdev_free(spa->spa_root_vdev);
	ASSERT(spa->spa_root_vdev == NULL);

	/*
	 * Close the dsl pool.
	 */
	if (spa->spa_dsl_pool) {
		dsl_pool_close(spa->spa_dsl_pool);
		spa->spa_dsl_pool = NULL;
		spa->spa_meta_objset = NULL;
	}

	ddt_unload(spa);

	/*
	 * Drop and purge level 2 cache
	 */
	spa_l2cache_drop(spa);

	for (i = 0; i < spa->spa_spares.sav_count; i++)
		vdev_free(spa->spa_spares.sav_vdevs[i]);
	if (spa->spa_spares.sav_vdevs) {
		kmem_free(spa->spa_spares.sav_vdevs,
		    spa->spa_spares.sav_count * sizeof (void *));
		spa->spa_spares.sav_vdevs = NULL;
	}
	if (spa->spa_spares.sav_config) {
		nvlist_free(spa->spa_spares.sav_config);
		spa->spa_spares.sav_config = NULL;
	}
	spa->spa_spares.sav_count = 0;

	for (i = 0; i < spa->spa_l2cache.sav_count; i++) {
		vdev_clear_stats(spa->spa_l2cache.sav_vdevs[i]);
		vdev_free(spa->spa_l2cache.sav_vdevs[i]);
	}
	if (spa->spa_l2cache.sav_vdevs) {
		kmem_free(spa->spa_l2cache.sav_vdevs,
		    spa->spa_l2cache.sav_count * sizeof (void *));
		spa->spa_l2cache.sav_vdevs = NULL;
	}
	if (spa->spa_l2cache.sav_config) {
		nvlist_free(spa->spa_l2cache.sav_config);
		spa->spa_l2cache.sav_config = NULL;
	}
	spa->spa_l2cache.sav_count = 0;

	spa->spa_async_suspended = 0;

	spa->spa_indirect_vdevs_loaded = B_FALSE;

	if (spa->spa_comment != NULL) {
		spa_strfree(spa->spa_comment);
		spa->spa_comment = NULL;
	}

	spa_config_exit(spa, SCL_ALL, spa);
}

/*
 * Load (or re-load) the current list of vdevs describing the active spares for
 * this pool.  When this is called, we have some form of basic information in
 * 'spa_spares.sav_config'.  We parse this into vdevs, try to open them, and
 * then re-generate a more complete list including status information.
 */
void
spa_load_spares(spa_t *spa)
{
	nvlist_t **spares;
	uint_t nspares;
	int i;
	vdev_t *vd, *tvd;

#ifndef _KERNEL
	/*
	 * zdb opens both the current state of the pool and the
	 * checkpointed state (if present), with a different spa_t.
	 *
	 * As spare vdevs are shared among open pools, we skip loading
	 * them when we load the checkpointed state of the pool.
	 */
	if (!spa_writeable(spa))
		return;
#endif

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/*
	 * First, close and free any existing spare vdevs.
	 */
	for (i = 0; i < spa->spa_spares.sav_count; i++) {
		vd = spa->spa_spares.sav_vdevs[i];

		/* Undo the call to spa_activate() below */
		if ((tvd = spa_lookup_by_guid(spa, vd->vdev_guid,
		    B_FALSE)) != NULL && tvd->vdev_isspare)
			spa_spare_remove(tvd);
		vdev_close(vd);
		vdev_free(vd);
	}

	if (spa->spa_spares.sav_vdevs)
		kmem_free(spa->spa_spares.sav_vdevs,
		    spa->spa_spares.sav_count * sizeof (void *));

	if (spa->spa_spares.sav_config == NULL)
		nspares = 0;
	else
		VERIFY(nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0);

	spa->spa_spares.sav_count = (int)nspares;
	spa->spa_spares.sav_vdevs = NULL;

	if (nspares == 0)
		return;

	/*
	 * Construct the array of vdevs, opening them to get status in the
	 * process.   For each spare, there is potentially two different vdev_t
	 * structures associated with it: one in the list of spares (used only
	 * for basic validation purposes) and one in the active vdev
	 * configuration (if it's spared in).  During this phase we open and
	 * validate each vdev on the spare list.  If the vdev also exists in the
	 * active configuration, then we also mark this vdev as an active spare.
	 */
	spa->spa_spares.sav_vdevs = kmem_alloc(nspares * sizeof (void *),
	    KM_SLEEP);
	for (i = 0; i < spa->spa_spares.sav_count; i++) {
		VERIFY(spa_config_parse(spa, &vd, spares[i], NULL, 0,
		    VDEV_ALLOC_SPARE) == 0);
		ASSERT(vd != NULL);

		spa->spa_spares.sav_vdevs[i] = vd;

		if ((tvd = spa_lookup_by_guid(spa, vd->vdev_guid,
		    B_FALSE)) != NULL) {
			if (!tvd->vdev_isspare)
				spa_spare_add(tvd);

			/*
			 * We only mark the spare active if we were successfully
			 * able to load the vdev.  Otherwise, importing a pool
			 * with a bad active spare would result in strange
			 * behavior, because multiple pool would think the spare
			 * is actively in use.
			 *
			 * There is a vulnerability here to an equally bizarre
			 * circumstance, where a dead active spare is later
			 * brought back to life (onlined or otherwise).  Given
			 * the rarity of this scenario, and the extra complexity
			 * it adds, we ignore the possibility.
			 */
			if (!vdev_is_dead(tvd))
				spa_spare_activate(tvd);
		}

		vd->vdev_top = vd;
		vd->vdev_aux = &spa->spa_spares;

		if (vdev_open(vd) != 0)
			continue;

		if (vdev_validate_aux(vd) == 0)
			spa_spare_add(vd);
	}

	/*
	 * Recompute the stashed list of spares, with status information
	 * this time.
	 */
	VERIFY(nvlist_remove(spa->spa_spares.sav_config, ZPOOL_CONFIG_SPARES,
	    DATA_TYPE_NVLIST_ARRAY) == 0);

	spares = kmem_alloc(spa->spa_spares.sav_count * sizeof (void *),
	    KM_SLEEP);
	for (i = 0; i < spa->spa_spares.sav_count; i++)
		spares[i] = vdev_config_generate(spa,
		    spa->spa_spares.sav_vdevs[i], B_TRUE, VDEV_CONFIG_SPARE);
	VERIFY(nvlist_add_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, spares, spa->spa_spares.sav_count) == 0);
	for (i = 0; i < spa->spa_spares.sav_count; i++)
		nvlist_free(spares[i]);
	kmem_free(spares, spa->spa_spares.sav_count * sizeof (void *));
}

/*
 * Load (or re-load) the current list of vdevs describing the active l2cache for
 * this pool.  When this is called, we have some form of basic information in
 * 'spa_l2cache.sav_config'.  We parse this into vdevs, try to open them, and
 * then re-generate a more complete list including status information.
 * Devices which are already active have their details maintained, and are
 * not re-opened.
 */
void
spa_load_l2cache(spa_t *spa)
{
	nvlist_t **l2cache;
	uint_t nl2cache;
	int i, j, oldnvdevs;
	uint64_t guid;
	vdev_t *vd, **oldvdevs, **newvdevs;
	spa_aux_vdev_t *sav = &spa->spa_l2cache;

#ifndef _KERNEL
	/*
	 * zdb opens both the current state of the pool and the
	 * checkpointed state (if present), with a different spa_t.
	 *
	 * As L2 caches are part of the ARC which is shared among open
	 * pools, we skip loading them when we load the checkpointed
	 * state of the pool.
	 */
	if (!spa_writeable(spa))
		return;
#endif

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if (sav->sav_config != NULL) {
		VERIFY(nvlist_lookup_nvlist_array(sav->sav_config,
		    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0);
		newvdevs = kmem_alloc(nl2cache * sizeof (void *), KM_SLEEP);
	} else {
		nl2cache = 0;
		newvdevs = NULL;
	}

	oldvdevs = sav->sav_vdevs;
	oldnvdevs = sav->sav_count;
	sav->sav_vdevs = NULL;
	sav->sav_count = 0;

	/*
	 * Process new nvlist of vdevs.
	 */
	for (i = 0; i < nl2cache; i++) {
		VERIFY(nvlist_lookup_uint64(l2cache[i], ZPOOL_CONFIG_GUID,
		    &guid) == 0);

		newvdevs[i] = NULL;
		for (j = 0; j < oldnvdevs; j++) {
			vd = oldvdevs[j];
			if (vd != NULL && guid == vd->vdev_guid) {
				/*
				 * Retain previous vdev for add/remove ops.
				 */
				newvdevs[i] = vd;
				oldvdevs[j] = NULL;
				break;
			}
		}

		if (newvdevs[i] == NULL) {
			/*
			 * Create new vdev
			 */
			VERIFY(spa_config_parse(spa, &vd, l2cache[i], NULL, 0,
			    VDEV_ALLOC_L2CACHE) == 0);
			ASSERT(vd != NULL);
			newvdevs[i] = vd;

			/*
			 * Commit this vdev as an l2cache device,
			 * even if it fails to open.
			 */
			spa_l2cache_add(vd);

			vd->vdev_top = vd;
			vd->vdev_aux = sav;

			spa_l2cache_activate(vd);

			if (vdev_open(vd) != 0)
				continue;

			(void) vdev_validate_aux(vd);

			if (!vdev_is_dead(vd))
				l2arc_add_vdev(spa, vd);
		}
	}

	/*
	 * Purge vdevs that were dropped
	 */
	for (i = 0; i < oldnvdevs; i++) {
		uint64_t pool;

		vd = oldvdevs[i];
		if (vd != NULL) {
			ASSERT(vd->vdev_isl2cache);

			if (spa_l2cache_exists(vd->vdev_guid, &pool) &&
			    pool != 0ULL && l2arc_vdev_present(vd))
				l2arc_remove_vdev(vd);
			vdev_clear_stats(vd);
			vdev_free(vd);
		}
	}

	if (oldvdevs)
		kmem_free(oldvdevs, oldnvdevs * sizeof (void *));

	if (sav->sav_config == NULL)
		goto out;

	sav->sav_vdevs = newvdevs;
	sav->sav_count = (int)nl2cache;

	/*
	 * Recompute the stashed list of l2cache devices, with status
	 * information this time.
	 */
	VERIFY(nvlist_remove(sav->sav_config, ZPOOL_CONFIG_L2CACHE,
	    DATA_TYPE_NVLIST_ARRAY) == 0);

	l2cache = kmem_alloc(sav->sav_count * sizeof (void *), KM_SLEEP);
	for (i = 0; i < sav->sav_count; i++)
		l2cache[i] = vdev_config_generate(spa,
		    sav->sav_vdevs[i], B_TRUE, VDEV_CONFIG_L2CACHE);
	VERIFY(nvlist_add_nvlist_array(sav->sav_config,
	    ZPOOL_CONFIG_L2CACHE, l2cache, sav->sav_count) == 0);
out:
	for (i = 0; i < sav->sav_count; i++)
		nvlist_free(l2cache[i]);
	if (sav->sav_count)
		kmem_free(l2cache, sav->sav_count * sizeof (void *));
}

static int
load_nvlist(spa_t *spa, uint64_t obj, nvlist_t **value)
{
	dmu_buf_t *db;
	char *packed = NULL;
	size_t nvsize = 0;
	int error;
	*value = NULL;

	error = dmu_bonus_hold(spa->spa_meta_objset, obj, FTAG, &db);
	if (error != 0)
		return (error);

	nvsize = *(uint64_t *)db->db_data;
	dmu_buf_rele(db, FTAG);

	packed = kmem_alloc(nvsize, KM_SLEEP);
	error = dmu_read(spa->spa_meta_objset, obj, 0, nvsize, packed,
	    DMU_READ_PREFETCH);
	if (error == 0)
		error = nvlist_unpack(packed, nvsize, value, 0);
	kmem_free(packed, nvsize);

	return (error);
}

/*
 * Concrete top-level vdevs that are not missing and are not logs. At every
 * spa_sync we write new uberblocks to at least SPA_SYNC_MIN_VDEVS core tvds.
 */
static uint64_t
spa_healthy_core_tvds(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t tvds = 0;

	for (uint64_t i = 0; i < rvd->vdev_children; i++) {
		vdev_t *vd = rvd->vdev_child[i];
		if (vd->vdev_islog)
			continue;
		if (vdev_is_concrete(vd) && !vdev_is_dead(vd))
			tvds++;
	}

	return (tvds);
}

/*
 * Checks to see if the given vdev could not be opened, in which case we post a
 * sysevent to notify the autoreplace code that the device has been removed.
 */
static void
spa_check_removed(vdev_t *vd)
{
	for (uint64_t c = 0; c < vd->vdev_children; c++)
		spa_check_removed(vd->vdev_child[c]);

	if (vd->vdev_ops->vdev_op_leaf && vdev_is_dead(vd) &&
	    vdev_is_concrete(vd)) {
		zfs_post_autoreplace(vd->vdev_spa, vd);
		spa_event_notify(vd->vdev_spa, vd, NULL, ESC_ZFS_VDEV_CHECK);
	}
}

static int
spa_check_for_missing_logs(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * If we're doing a normal import, then build up any additional
	 * diagnostic information about missing log devices.
	 * We'll pass this up to the user for further processing.
	 */
	if (!(spa->spa_import_flags & ZFS_IMPORT_MISSING_LOG)) {
		nvlist_t **child, *nv;
		uint64_t idx = 0;

		child = kmem_alloc(rvd->vdev_children * sizeof (nvlist_t **),
		    KM_SLEEP);
		VERIFY(nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) == 0);

		for (uint64_t c = 0; c < rvd->vdev_children; c++) {
			vdev_t *tvd = rvd->vdev_child[c];

			/*
			 * We consider a device as missing only if it failed
			 * to open (i.e. offline or faulted is not considered
			 * as missing).
			 */
			if (tvd->vdev_islog &&
			    tvd->vdev_state == VDEV_STATE_CANT_OPEN) {
				child[idx++] = vdev_config_generate(spa, tvd,
				    B_FALSE, VDEV_CONFIG_MISSING);
			}
		}

		if (idx > 0) {
			fnvlist_add_nvlist_array(nv,
			    ZPOOL_CONFIG_CHILDREN, child, idx);
			fnvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_MISSING_DEVICES, nv);

			for (uint64_t i = 0; i < idx; i++)
				nvlist_free(child[i]);
		}
		nvlist_free(nv);
		kmem_free(child, rvd->vdev_children * sizeof (char **));

		if (idx > 0) {
			spa_load_failed(spa, "some log devices are missing");
			vdev_dbgmsg_print_tree(rvd, 2);
			return (SET_ERROR(ENXIO));
		}
	} else {
		for (uint64_t c = 0; c < rvd->vdev_children; c++) {
			vdev_t *tvd = rvd->vdev_child[c];

			if (tvd->vdev_islog &&
			    tvd->vdev_state == VDEV_STATE_CANT_OPEN) {
				spa_set_log_state(spa, SPA_LOG_CLEAR);
				spa_load_note(spa, "some log devices are "
				    "missing, ZIL is dropped.");
				vdev_dbgmsg_print_tree(rvd, 2);
				break;
			}
		}
	}

	return (0);
}

/*
 * Check for missing log devices
 */
static boolean_t
spa_check_logs(spa_t *spa)
{
	boolean_t rv = B_FALSE;
	dsl_pool_t *dp = spa_get_dsl(spa);

	switch (spa->spa_log_state) {
	case SPA_LOG_MISSING:
		/* need to recheck in case slog has been restored */
	case SPA_LOG_UNKNOWN:
		rv = (dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
		    zil_check_log_chain, NULL, DS_FIND_CHILDREN) != 0);
		if (rv)
			spa_set_log_state(spa, SPA_LOG_MISSING);
		break;
	}
	return (rv);
}

static boolean_t
spa_passivate_log(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	boolean_t slog_found = B_FALSE;

	ASSERT(spa_config_held(spa, SCL_ALLOC, RW_WRITER));

	if (!spa_has_slogs(spa))
		return (B_FALSE);

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		if (tvd->vdev_islog) {
			metaslab_group_passivate(mg);
			slog_found = B_TRUE;
		}
	}

	return (slog_found);
}

static void
spa_activate_log(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(spa_config_held(spa, SCL_ALLOC, RW_WRITER));

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		if (tvd->vdev_islog)
			metaslab_group_activate(mg);
	}
}

int
spa_reset_logs(spa_t *spa)
{
	int error;

	error = dmu_objset_find(spa_name(spa), zil_reset,
	    NULL, DS_FIND_CHILDREN);
	if (error == 0) {
		/*
		 * We successfully offlined the log device, sync out the
		 * current txg so that the "stubby" block can be removed
		 * by zil_sync().
		 */
		txg_wait_synced(spa->spa_dsl_pool, 0);
	}
	return (error);
}

static void
spa_aux_check_removed(spa_aux_vdev_t *sav)
{
	int i;

	for (i = 0; i < sav->sav_count; i++)
		spa_check_removed(sav->sav_vdevs[i]);
}

void
spa_claim_notify(zio_t *zio)
{
	spa_t *spa = zio->io_spa;

	if (zio->io_error)
		return;

	mutex_enter(&spa->spa_props_lock);	/* any mutex will do */
	if (spa->spa_claim_max_txg < zio->io_bp->blk_birth)
		spa->spa_claim_max_txg = zio->io_bp->blk_birth;
	mutex_exit(&spa->spa_props_lock);
}

typedef struct spa_load_error {
	uint64_t	sle_meta_count;
	uint64_t	sle_data_count;
} spa_load_error_t;

static void
spa_load_verify_done(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	spa_load_error_t *sle = zio->io_private;
	dmu_object_type_t type = BP_GET_TYPE(bp);
	int error = zio->io_error;
	spa_t *spa = zio->io_spa;

	abd_free(zio->io_abd);
	if (error) {
		if ((BP_GET_LEVEL(bp) != 0 || DMU_OT_IS_METADATA(type)) &&
		    type != DMU_OT_INTENT_LOG)
			atomic_inc_64(&sle->sle_meta_count);
		else
			atomic_inc_64(&sle->sle_data_count);
	}

	mutex_enter(&spa->spa_scrub_lock);
	spa->spa_load_verify_ios--;
	cv_broadcast(&spa->spa_scrub_io_cv);
	mutex_exit(&spa->spa_scrub_lock);
}

/*
 * Maximum number of concurrent scrub i/os to create while verifying
 * a pool while importing it.
 */
int spa_load_verify_maxinflight = 10000;
boolean_t spa_load_verify_metadata = B_TRUE;
boolean_t spa_load_verify_data = B_TRUE;

SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_load_verify_maxinflight, CTLFLAG_RWTUN,
    &spa_load_verify_maxinflight, 0,
    "Maximum number of concurrent scrub I/Os to create while verifying a "
    "pool while importing it");

SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_load_verify_metadata, CTLFLAG_RWTUN,
    &spa_load_verify_metadata, 0,
    "Check metadata on import?");
 
SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_load_verify_data, CTLFLAG_RWTUN,
    &spa_load_verify_data, 0,
    "Check user data on import?");
 
/*ARGSUSED*/
static int
spa_load_verify_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	if (bp == NULL || BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp))
		return (0);
	/*
	 * Note: normally this routine will not be called if
	 * spa_load_verify_metadata is not set.  However, it may be useful
	 * to manually set the flag after the traversal has begun.
	 */
	if (!spa_load_verify_metadata)
		return (0);
	if (!BP_IS_METADATA(bp) && !spa_load_verify_data)
		return (0);

	zio_t *rio = arg;
	size_t size = BP_GET_PSIZE(bp);

	mutex_enter(&spa->spa_scrub_lock);
	while (spa->spa_load_verify_ios >= spa_load_verify_maxinflight)
		cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
	spa->spa_load_verify_ios++;
	mutex_exit(&spa->spa_scrub_lock);

	zio_nowait(zio_read(rio, spa, bp, abd_alloc_for_io(size, B_FALSE), size,
	    spa_load_verify_done, rio->io_private, ZIO_PRIORITY_SCRUB,
	    ZIO_FLAG_SPECULATIVE | ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SCRUB | ZIO_FLAG_RAW, zb));
	return (0);
}

/* ARGSUSED */
int
verify_dataset_name_len(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	if (dsl_dataset_namelen(ds) >= ZFS_MAX_DATASET_NAME_LEN)
		return (SET_ERROR(ENAMETOOLONG));

	return (0);
}

static int
spa_load_verify(spa_t *spa)
{
	zio_t *rio;
	spa_load_error_t sle = { 0 };
	zpool_load_policy_t policy;
	boolean_t verify_ok = B_FALSE;
	int error = 0;

	zpool_get_load_policy(spa->spa_config, &policy);

	if (policy.zlp_rewind & ZPOOL_NEVER_REWIND)
		return (0);

	dsl_pool_config_enter(spa->spa_dsl_pool, FTAG);
	error = dmu_objset_find_dp(spa->spa_dsl_pool,
	    spa->spa_dsl_pool->dp_root_dir_obj, verify_dataset_name_len, NULL,
	    DS_FIND_CHILDREN);
	dsl_pool_config_exit(spa->spa_dsl_pool, FTAG);
	if (error != 0)
		return (error);

	rio = zio_root(spa, NULL, &sle,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE);

	if (spa_load_verify_metadata) {
		if (spa->spa_extreme_rewind) {
			spa_load_note(spa, "performing a complete scan of the "
			    "pool since extreme rewind is on. This may take "
			    "a very long time.\n  (spa_load_verify_data=%u, "
			    "spa_load_verify_metadata=%u)",
			    spa_load_verify_data, spa_load_verify_metadata);
		}
		error = traverse_pool(spa, spa->spa_verify_min_txg,
		    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA,
		    spa_load_verify_cb, rio);
	}

	(void) zio_wait(rio);

	spa->spa_load_meta_errors = sle.sle_meta_count;
	spa->spa_load_data_errors = sle.sle_data_count;

	if (sle.sle_meta_count != 0 || sle.sle_data_count != 0) {
		spa_load_note(spa, "spa_load_verify found %llu metadata errors "
		    "and %llu data errors", (u_longlong_t)sle.sle_meta_count,
		    (u_longlong_t)sle.sle_data_count);
	}

	if (spa_load_verify_dryrun ||
	    (!error && sle.sle_meta_count <= policy.zlp_maxmeta &&
	    sle.sle_data_count <= policy.zlp_maxdata)) {
		int64_t loss = 0;

		verify_ok = B_TRUE;
		spa->spa_load_txg = spa->spa_uberblock.ub_txg;
		spa->spa_load_txg_ts = spa->spa_uberblock.ub_timestamp;

		loss = spa->spa_last_ubsync_txg_ts - spa->spa_load_txg_ts;
		VERIFY(nvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_LOAD_TIME, spa->spa_load_txg_ts) == 0);
		VERIFY(nvlist_add_int64(spa->spa_load_info,
		    ZPOOL_CONFIG_REWIND_TIME, loss) == 0);
		VERIFY(nvlist_add_uint64(spa->spa_load_info,
		    ZPOOL_CONFIG_LOAD_DATA_ERRORS, sle.sle_data_count) == 0);
	} else {
		spa->spa_load_max_txg = spa->spa_uberblock.ub_txg;
	}

	if (spa_load_verify_dryrun)
		return (0);

	if (error) {
		if (error != ENXIO && error != EIO)
			error = SET_ERROR(EIO);
		return (error);
	}

	return (verify_ok ? 0 : EIO);
}

/*
 * Find a value in the pool props object.
 */
static void
spa_prop_find(spa_t *spa, zpool_prop_t prop, uint64_t *val)
{
	(void) zap_lookup(spa->spa_meta_objset, spa->spa_pool_props_object,
	    zpool_prop_to_name(prop), sizeof (uint64_t), 1, val);
}

/*
 * Find a value in the pool directory object.
 */
static int
spa_dir_prop(spa_t *spa, const char *name, uint64_t *val, boolean_t log_enoent)
{
	int error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    name, sizeof (uint64_t), 1, val);

	if (error != 0 && (error != ENOENT || log_enoent)) {
		spa_load_failed(spa, "couldn't get '%s' value in MOS directory "
		    "[error=%d]", name, error);
	}

	return (error);
}

static int
spa_vdev_err(vdev_t *vdev, vdev_aux_t aux, int err)
{
	vdev_set_state(vdev, B_TRUE, VDEV_STATE_CANT_OPEN, aux);
	return (SET_ERROR(err));
}

static void
spa_spawn_aux_threads(spa_t *spa)
{
	ASSERT(spa_writeable(spa));

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_start_indirect_condensing_thread(spa);

	ASSERT3P(spa->spa_checkpoint_discard_zthr, ==, NULL);
	spa->spa_checkpoint_discard_zthr =
	    zthr_create(spa_checkpoint_discard_thread_check,
	    spa_checkpoint_discard_thread, spa);
}

/*
 * Fix up config after a partly-completed split.  This is done with the
 * ZPOOL_CONFIG_SPLIT nvlist.  Both the splitting pool and the split-off
 * pool have that entry in their config, but only the splitting one contains
 * a list of all the guids of the vdevs that are being split off.
 *
 * This function determines what to do with that list: either rejoin
 * all the disks to the pool, or complete the splitting process.  To attempt
 * the rejoin, each disk that is offlined is marked online again, and
 * we do a reopen() call.  If the vdev label for every disk that was
 * marked online indicates it was successfully split off (VDEV_AUX_SPLIT_POOL)
 * then we call vdev_split() on each disk, and complete the split.
 *
 * Otherwise we leave the config alone, with all the vdevs in place in
 * the original pool.
 */
static void
spa_try_repair(spa_t *spa, nvlist_t *config)
{
	uint_t extracted;
	uint64_t *glist;
	uint_t i, gcount;
	nvlist_t *nvl;
	vdev_t **vd;
	boolean_t attempt_reopen;

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_SPLIT, &nvl) != 0)
		return;

	/* check that the config is complete */
	if (nvlist_lookup_uint64_array(nvl, ZPOOL_CONFIG_SPLIT_LIST,
	    &glist, &gcount) != 0)
		return;

	vd = kmem_zalloc(gcount * sizeof (vdev_t *), KM_SLEEP);

	/* attempt to online all the vdevs & validate */
	attempt_reopen = B_TRUE;
	for (i = 0; i < gcount; i++) {
		if (glist[i] == 0)	/* vdev is hole */
			continue;

		vd[i] = spa_lookup_by_guid(spa, glist[i], B_FALSE);
		if (vd[i] == NULL) {
			/*
			 * Don't bother attempting to reopen the disks;
			 * just do the split.
			 */
			attempt_reopen = B_FALSE;
		} else {
			/* attempt to re-online it */
			vd[i]->vdev_offline = B_FALSE;
		}
	}

	if (attempt_reopen) {
		vdev_reopen(spa->spa_root_vdev);

		/* check each device to see what state it's in */
		for (extracted = 0, i = 0; i < gcount; i++) {
			if (vd[i] != NULL &&
			    vd[i]->vdev_stat.vs_aux != VDEV_AUX_SPLIT_POOL)
				break;
			++extracted;
		}
	}

	/*
	 * If every disk has been moved to the new pool, or if we never
	 * even attempted to look at them, then we split them off for
	 * good.
	 */
	if (!attempt_reopen || gcount == extracted) {
		for (i = 0; i < gcount; i++)
			if (vd[i] != NULL)
				vdev_split(vd[i]);
		vdev_reopen(spa->spa_root_vdev);
	}

	kmem_free(vd, gcount * sizeof (vdev_t *));
}

static int
spa_load(spa_t *spa, spa_load_state_t state, spa_import_type_t type)
{
	char *ereport = FM_EREPORT_ZFS_POOL;
	int error;

	spa->spa_load_state = state;

	gethrestime(&spa->spa_loaded_ts);
	error = spa_load_impl(spa, type, &ereport);

	/*
	 * Don't count references from objsets that are already closed
	 * and are making their way through the eviction process.
	 */
	spa_evicting_os_wait(spa);
	spa->spa_minref = refcount_count(&spa->spa_refcount);
	if (error) {
		if (error != EEXIST) {
			spa->spa_loaded_ts.tv_sec = 0;
			spa->spa_loaded_ts.tv_nsec = 0;
		}
		if (error != EBADF) {
			zfs_ereport_post(ereport, spa, NULL, NULL, 0, 0);
		}
	}
	spa->spa_load_state = error ? SPA_LOAD_ERROR : SPA_LOAD_NONE;
	spa->spa_ena = 0;

	return (error);
}

/*
 * Count the number of per-vdev ZAPs associated with all of the vdevs in the
 * vdev tree rooted in the given vd, and ensure that each ZAP is present in the
 * spa's per-vdev ZAP list.
 */
static uint64_t
vdev_count_verify_zaps(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	uint64_t total = 0;
	if (vd->vdev_top_zap != 0) {
		total++;
		ASSERT0(zap_lookup_int(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, vd->vdev_top_zap));
	}
	if (vd->vdev_leaf_zap != 0) {
		total++;
		ASSERT0(zap_lookup_int(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, vd->vdev_leaf_zap));
	}

	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		total += vdev_count_verify_zaps(vd->vdev_child[i]);
	}

	return (total);
}

static int
spa_verify_host(spa_t *spa, nvlist_t *mos_config)
{
	uint64_t hostid;
	char *hostname;
	uint64_t myhostid = 0;

	if (!spa_is_root(spa) && nvlist_lookup_uint64(mos_config,
	    ZPOOL_CONFIG_HOSTID, &hostid) == 0) {
		hostname = fnvlist_lookup_string(mos_config,
		    ZPOOL_CONFIG_HOSTNAME);

		myhostid = zone_get_hostid(NULL);

		if (hostid != 0 && myhostid != 0 && hostid != myhostid) {
			cmn_err(CE_WARN, "pool '%s' could not be "
			    "loaded as it was last accessed by "
			    "another system (host: %s hostid: 0x%llx). "
			    "See: http://illumos.org/msg/ZFS-8000-EY",
			    spa_name(spa), hostname, (u_longlong_t)hostid);
			spa_load_failed(spa, "hostid verification failed: pool "
			    "last accessed by host: %s (hostid: 0x%llx)",
			    hostname, (u_longlong_t)hostid);
			return (SET_ERROR(EBADF));
		}
	}

	return (0);
}

static int
spa_ld_parse_config(spa_t *spa, spa_import_type_t type)
{
	int error = 0;
	nvlist_t *nvtree, *nvl, *config = spa->spa_config;
	int parse;
	vdev_t *rvd;
	uint64_t pool_guid;
	char *comment;

	/*
	 * Versioning wasn't explicitly added to the label until later, so if
	 * it's not present treat it as the initial version.
	 */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &spa->spa_ubsync.ub_version) != 0)
		spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pool_guid)) {
		spa_load_failed(spa, "invalid config provided: '%s' missing",
		    ZPOOL_CONFIG_POOL_GUID);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * If we are doing an import, ensure that the pool is not already
	 * imported by checking if its pool guid already exists in the
	 * spa namespace.
	 *
	 * The only case that we allow an already imported pool to be
	 * imported again, is when the pool is checkpointed and we want to
	 * look at its checkpointed state from userland tools like zdb.
	 */
#ifdef _KERNEL
	if ((spa->spa_load_state == SPA_LOAD_IMPORT ||
	    spa->spa_load_state == SPA_LOAD_TRYIMPORT) &&
	    spa_guid_exists(pool_guid, 0)) {
#else
	if ((spa->spa_load_state == SPA_LOAD_IMPORT ||
	    spa->spa_load_state == SPA_LOAD_TRYIMPORT) &&
	    spa_guid_exists(pool_guid, 0) &&
	    !spa_importing_readonly_checkpoint(spa)) {
#endif
		spa_load_failed(spa, "a pool with guid %llu is already open",
		    (u_longlong_t)pool_guid);
		return (SET_ERROR(EEXIST));
	}

	spa->spa_config_guid = pool_guid;

	nvlist_free(spa->spa_load_info);
	spa->spa_load_info = fnvlist_alloc();

	ASSERT(spa->spa_comment == NULL);
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMMENT, &comment) == 0)
		spa->spa_comment = spa_strdup(comment);

	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    &spa->spa_config_txg);

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_SPLIT, &nvl) == 0)
		spa->spa_config_splitting = fnvlist_dup(nvl);

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvtree)) {
		spa_load_failed(spa, "invalid config provided: '%s' missing",
		    ZPOOL_CONFIG_VDEV_TREE);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Create "The Godfather" zio to hold all async IOs
	 */
	spa->spa_async_zio_root = kmem_alloc(max_ncpus * sizeof (void *),
	    KM_SLEEP);
	for (int i = 0; i < max_ncpus; i++) {
		spa->spa_async_zio_root[i] = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
		    ZIO_FLAG_GODFATHER);
	}

	/*
	 * Parse the configuration into a vdev tree.  We explicitly set the
	 * value that will be returned by spa_version() since parsing the
	 * configuration requires knowing the version number.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	parse = (type == SPA_IMPORT_EXISTING ?
	    VDEV_ALLOC_LOAD : VDEV_ALLOC_SPLIT);
	error = spa_config_parse(spa, &rvd, nvtree, NULL, 0, parse);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_load_failed(spa, "unable to parse config [error=%d]",
		    error);
		return (error);
	}

	ASSERT(spa->spa_root_vdev == rvd);
	ASSERT3U(spa->spa_min_ashift, >=, SPA_MINBLOCKSHIFT);
	ASSERT3U(spa->spa_max_ashift, <=, SPA_MAXBLOCKSHIFT);

	if (type != SPA_IMPORT_ASSEMBLE) {
		ASSERT(spa_guid(spa) == pool_guid);
	}

	return (0);
}

/*
 * Recursively open all vdevs in the vdev tree. This function is called twice:
 * first with the untrusted config, then with the trusted config.
 */
static int
spa_ld_open_vdevs(spa_t *spa)
{
	int error = 0;

	/*
	 * spa_missing_tvds_allowed defines how many top-level vdevs can be
	 * missing/unopenable for the root vdev to be still considered openable.
	 */
	if (spa->spa_trust_config) {
		spa->spa_missing_tvds_allowed = zfs_max_missing_tvds;
	} else if (spa->spa_config_source == SPA_CONFIG_SRC_CACHEFILE) {
		spa->spa_missing_tvds_allowed = zfs_max_missing_tvds_cachefile;
	} else if (spa->spa_config_source == SPA_CONFIG_SRC_SCAN) {
		spa->spa_missing_tvds_allowed = zfs_max_missing_tvds_scan;
	} else {
		spa->spa_missing_tvds_allowed = 0;
	}

	spa->spa_missing_tvds_allowed =
	    MAX(zfs_max_missing_tvds, spa->spa_missing_tvds_allowed);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = vdev_open(spa->spa_root_vdev);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (spa->spa_missing_tvds != 0) {
		spa_load_note(spa, "vdev tree has %lld missing top-level "
		    "vdevs.", (u_longlong_t)spa->spa_missing_tvds);
		if (spa->spa_trust_config && (spa->spa_mode & FWRITE)) {
			/*
			 * Although theoretically we could allow users to open
			 * incomplete pools in RW mode, we'd need to add a lot
			 * of extra logic (e.g. adjust pool space to account
			 * for missing vdevs).
			 * This limitation also prevents users from accidentally
			 * opening the pool in RW mode during data recovery and
			 * damaging it further.
			 */
			spa_load_note(spa, "pools with missing top-level "
			    "vdevs can only be opened in read-only mode.");
			error = SET_ERROR(ENXIO);
		} else {
			spa_load_note(spa, "current settings allow for maximum "
			    "%lld missing top-level vdevs at this stage.",
			    (u_longlong_t)spa->spa_missing_tvds_allowed);
		}
	}
	if (error != 0) {
		spa_load_failed(spa, "unable to open vdev tree [error=%d]",
		    error);
	}
	if (spa->spa_missing_tvds != 0 || error != 0)
		vdev_dbgmsg_print_tree(spa->spa_root_vdev, 2);

	return (error);
}

/*
 * We need to validate the vdev labels against the configuration that
 * we have in hand. This function is called twice: first with an untrusted
 * config, then with a trusted config. The validation is more strict when the
 * config is trusted.
 */
static int
spa_ld_validate_vdevs(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = vdev_validate(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_load_failed(spa, "vdev_validate failed [error=%d]", error);
		return (error);
	}

	if (rvd->vdev_state <= VDEV_STATE_CANT_OPEN) {
		spa_load_failed(spa, "cannot open vdev tree after invalidating "
		    "some vdevs");
		vdev_dbgmsg_print_tree(rvd, 2);
		return (SET_ERROR(ENXIO));
	}

	return (0);
}

static void
spa_ld_select_uberblock_done(spa_t *spa, uberblock_t *ub)
{
	spa->spa_state = POOL_STATE_ACTIVE;
	spa->spa_ubsync = spa->spa_uberblock;
	spa->spa_verify_min_txg = spa->spa_extreme_rewind ?
	    TXG_INITIAL - 1 : spa_last_synced_txg(spa) - TXG_DEFER_SIZE - 1;
	spa->spa_first_txg = spa->spa_last_ubsync_txg ?
	    spa->spa_last_ubsync_txg : spa_last_synced_txg(spa) + 1;
	spa->spa_claim_max_txg = spa->spa_first_txg;
	spa->spa_prev_software_version = ub->ub_software_version;
}

static int
spa_ld_select_uberblock(spa_t *spa, spa_import_type_t type)
{
	vdev_t *rvd = spa->spa_root_vdev;
	nvlist_t *label;
	uberblock_t *ub = &spa->spa_uberblock;

	/*
	 * If we are opening the checkpointed state of the pool by
	 * rewinding to it, at this point we will have written the
	 * checkpointed uberblock to the vdev labels, so searching
	 * the labels will find the right uberblock.  However, if
	 * we are opening the checkpointed state read-only, we have
	 * not modified the labels. Therefore, we must ignore the
	 * labels and continue using the spa_uberblock that was set
	 * by spa_ld_checkpoint_rewind.
	 *
	 * Note that it would be fine to ignore the labels when
	 * rewinding (opening writeable) as well. However, if we
	 * crash just after writing the labels, we will end up
	 * searching the labels. Doing so in the common case means
	 * that this code path gets exercised normally, rather than
	 * just in the edge case.
	 */
	if (ub->ub_checkpoint_txg != 0 &&
	    spa_importing_readonly_checkpoint(spa)) {
		spa_ld_select_uberblock_done(spa, ub);
		return (0);
	}

	/*
	 * Find the best uberblock.
	 */
	vdev_uberblock_load(rvd, ub, &label);

	/*
	 * If we weren't able to find a single valid uberblock, return failure.
	 */
	if (ub->ub_txg == 0) {
		nvlist_free(label);
		spa_load_failed(spa, "no valid uberblock found");
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, ENXIO));
	}

	spa_load_note(spa, "using uberblock with txg=%llu",
	    (u_longlong_t)ub->ub_txg);

	/*
	 * If the pool has an unsupported version we can't open it.
	 */
	if (!SPA_VERSION_IS_SUPPORTED(ub->ub_version)) {
		nvlist_free(label);
		spa_load_failed(spa, "version %llu is not supported",
		    (u_longlong_t)ub->ub_version);
		return (spa_vdev_err(rvd, VDEV_AUX_VERSION_NEWER, ENOTSUP));
	}

	if (ub->ub_version >= SPA_VERSION_FEATURES) {
		nvlist_t *features;

		/*
		 * If we weren't able to find what's necessary for reading the
		 * MOS in the label, return failure.
		 */
		if (label == NULL) {
			spa_load_failed(spa, "label config unavailable");
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA,
			    ENXIO));
		}

		if (nvlist_lookup_nvlist(label, ZPOOL_CONFIG_FEATURES_FOR_READ,
		    &features) != 0) {
			nvlist_free(label);
			spa_load_failed(spa, "invalid label: '%s' missing",
			    ZPOOL_CONFIG_FEATURES_FOR_READ);
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA,
			    ENXIO));
		}

		/*
		 * Update our in-core representation with the definitive values
		 * from the label.
		 */
		nvlist_free(spa->spa_label_features);
		VERIFY(nvlist_dup(features, &spa->spa_label_features, 0) == 0);
	}

	nvlist_free(label);

	/*
	 * Look through entries in the label nvlist's features_for_read. If
	 * there is a feature listed there which we don't understand then we
	 * cannot open a pool.
	 */
	if (ub->ub_version >= SPA_VERSION_FEATURES) {
		nvlist_t *unsup_feat;

		VERIFY(nvlist_alloc(&unsup_feat, NV_UNIQUE_NAME, KM_SLEEP) ==
		    0);

		for (nvpair_t *nvp = nvlist_next_nvpair(spa->spa_label_features,
		    NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(spa->spa_label_features, nvp)) {
			if (!zfeature_is_supported(nvpair_name(nvp))) {
				VERIFY(nvlist_add_string(unsup_feat,
				    nvpair_name(nvp), "") == 0);
			}
		}

		if (!nvlist_empty(unsup_feat)) {
			VERIFY(nvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_UNSUP_FEAT, unsup_feat) == 0);
			nvlist_free(unsup_feat);
			spa_load_failed(spa, "some features are unsupported");
			return (spa_vdev_err(rvd, VDEV_AUX_UNSUP_FEAT,
			    ENOTSUP));
		}

		nvlist_free(unsup_feat);
	}

	if (type != SPA_IMPORT_ASSEMBLE && spa->spa_config_splitting) {
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_try_repair(spa, spa->spa_config);
		spa_config_exit(spa, SCL_ALL, FTAG);
		nvlist_free(spa->spa_config_splitting);
		spa->spa_config_splitting = NULL;
	}

	/*
	 * Initialize internal SPA structures.
	 */
	spa_ld_select_uberblock_done(spa, ub);

	return (0);
}

static int
spa_ld_open_rootbp(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	error = dsl_pool_init(spa, spa->spa_first_txg, &spa->spa_dsl_pool);
	if (error != 0) {
		spa_load_failed(spa, "unable to open rootbp in dsl_pool_init "
		    "[error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}
	spa->spa_meta_objset = spa->spa_dsl_pool->dp_meta_objset;

	return (0);
}

static int
spa_ld_trusted_config(spa_t *spa, spa_import_type_t type,
    boolean_t reloading)
{
	vdev_t *mrvd, *rvd = spa->spa_root_vdev;
	nvlist_t *nv, *mos_config, *policy;
	int error = 0, copy_error;
	uint64_t healthy_tvds, healthy_tvds_mos;
	uint64_t mos_config_txg;

	if (spa_dir_prop(spa, DMU_POOL_CONFIG, &spa->spa_config_object, B_TRUE)
	    != 0)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * If we're assembling a pool from a split, the config provided is
	 * already trusted so there is nothing to do.
	 */
	if (type == SPA_IMPORT_ASSEMBLE)
		return (0);

	healthy_tvds = spa_healthy_core_tvds(spa);

	if (load_nvlist(spa, spa->spa_config_object, &mos_config)
	    != 0) {
		spa_load_failed(spa, "unable to retrieve MOS config");
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * If we are doing an open, pool owner wasn't verified yet, thus do
	 * the verification here.
	 */
	if (spa->spa_load_state == SPA_LOAD_OPEN) {
		error = spa_verify_host(spa, mos_config);
		if (error != 0) {
			nvlist_free(mos_config);
			return (error);
		}
	}

	nv = fnvlist_lookup_nvlist(mos_config, ZPOOL_CONFIG_VDEV_TREE);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * Build a new vdev tree from the trusted config
	 */
	VERIFY(spa_config_parse(spa, &mrvd, nv, NULL, 0, VDEV_ALLOC_LOAD) == 0);

	/*
	 * Vdev paths in the MOS may be obsolete. If the untrusted config was
	 * obtained by scanning /dev/dsk, then it will have the right vdev
	 * paths. We update the trusted MOS config with this information.
	 * We first try to copy the paths with vdev_copy_path_strict, which
	 * succeeds only when both configs have exactly the same vdev tree.
	 * If that fails, we fall back to a more flexible method that has a
	 * best effort policy.
	 */
	copy_error = vdev_copy_path_strict(rvd, mrvd);
	if (copy_error != 0 || spa_load_print_vdev_tree) {
		spa_load_note(spa, "provided vdev tree:");
		vdev_dbgmsg_print_tree(rvd, 2);
		spa_load_note(spa, "MOS vdev tree:");
		vdev_dbgmsg_print_tree(mrvd, 2);
	}
	if (copy_error != 0) {
		spa_load_note(spa, "vdev_copy_path_strict failed, falling "
		    "back to vdev_copy_path_relaxed");
		vdev_copy_path_relaxed(rvd, mrvd);
	}

	vdev_close(rvd);
	vdev_free(rvd);
	spa->spa_root_vdev = mrvd;
	rvd = mrvd;
	spa_config_exit(spa, SCL_ALL, FTAG);

	/*
	 * We will use spa_config if we decide to reload the spa or if spa_load
	 * fails and we rewind. We must thus regenerate the config using the
	 * MOS information with the updated paths. ZPOOL_LOAD_POLICY is used to
	 * pass settings on how to load the pool and is not stored in the MOS.
	 * We copy it over to our new, trusted config.
	 */
	mos_config_txg = fnvlist_lookup_uint64(mos_config,
	    ZPOOL_CONFIG_POOL_TXG);
	nvlist_free(mos_config);
	mos_config = spa_config_generate(spa, NULL, mos_config_txg, B_FALSE);
	if (nvlist_lookup_nvlist(spa->spa_config, ZPOOL_LOAD_POLICY,
	    &policy) == 0)
		fnvlist_add_nvlist(mos_config, ZPOOL_LOAD_POLICY, policy);
	spa_config_set(spa, mos_config);
	spa->spa_config_source = SPA_CONFIG_SRC_MOS;

	/*
	 * Now that we got the config from the MOS, we should be more strict
	 * in checking blkptrs and can make assumptions about the consistency
	 * of the vdev tree. spa_trust_config must be set to true before opening
	 * vdevs in order for them to be writeable.
	 */
	spa->spa_trust_config = B_TRUE;

	/*
	 * Open and validate the new vdev tree
	 */
	error = spa_ld_open_vdevs(spa);
	if (error != 0)
		return (error);

	error = spa_ld_validate_vdevs(spa);
	if (error != 0)
		return (error);

	if (copy_error != 0 || spa_load_print_vdev_tree) {
		spa_load_note(spa, "final vdev tree:");
		vdev_dbgmsg_print_tree(rvd, 2);
	}

	if (spa->spa_load_state != SPA_LOAD_TRYIMPORT &&
	    !spa->spa_extreme_rewind && zfs_max_missing_tvds == 0) {
		/*
		 * Sanity check to make sure that we are indeed loading the
		 * latest uberblock. If we missed SPA_SYNC_MIN_VDEVS tvds
		 * in the config provided and they happened to be the only ones
		 * to have the latest uberblock, we could involuntarily perform
		 * an extreme rewind.
		 */
		healthy_tvds_mos = spa_healthy_core_tvds(spa);
		if (healthy_tvds_mos - healthy_tvds >=
		    SPA_SYNC_MIN_VDEVS) {
			spa_load_note(spa, "config provided misses too many "
			    "top-level vdevs compared to MOS (%lld vs %lld). ",
			    (u_longlong_t)healthy_tvds,
			    (u_longlong_t)healthy_tvds_mos);
			spa_load_note(spa, "vdev tree:");
			vdev_dbgmsg_print_tree(rvd, 2);
			if (reloading) {
				spa_load_failed(spa, "config was already "
				    "provided from MOS. Aborting.");
				return (spa_vdev_err(rvd,
				    VDEV_AUX_CORRUPT_DATA, EIO));
			}
			spa_load_note(spa, "spa must be reloaded using MOS "
			    "config");
			return (SET_ERROR(EAGAIN));
		}
	}

	error = spa_check_for_missing_logs(spa);
	if (error != 0)
		return (spa_vdev_err(rvd, VDEV_AUX_BAD_GUID_SUM, ENXIO));

	if (rvd->vdev_guid_sum != spa->spa_uberblock.ub_guid_sum) {
		spa_load_failed(spa, "uberblock guid sum doesn't match MOS "
		    "guid sum (%llu != %llu)",
		    (u_longlong_t)spa->spa_uberblock.ub_guid_sum,
		    (u_longlong_t)rvd->vdev_guid_sum);
		return (spa_vdev_err(rvd, VDEV_AUX_BAD_GUID_SUM,
		    ENXIO));
	}

	return (0);
}

static int
spa_ld_open_indirect_vdev_metadata(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * Everything that we read before spa_remove_init() must be stored
	 * on concreted vdevs.  Therefore we do this as early as possible.
	 */
	error = spa_remove_init(spa);
	if (error != 0) {
		spa_load_failed(spa, "spa_remove_init failed [error=%d]",
		    error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * Retrieve information needed to condense indirect vdev mappings.
	 */
	error = spa_condense_init(spa);
	if (error != 0) {
		spa_load_failed(spa, "spa_condense_init failed [error=%d]",
		    error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, error));
	}

	return (0);
}

static int
spa_ld_check_features(spa_t *spa, boolean_t *missing_feat_writep)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	if (spa_version(spa) >= SPA_VERSION_FEATURES) {
		boolean_t missing_feat_read = B_FALSE;
		nvlist_t *unsup_feat, *enabled_feat;

		if (spa_dir_prop(spa, DMU_POOL_FEATURES_FOR_READ,
		    &spa->spa_feat_for_read_obj, B_TRUE) != 0) {
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		if (spa_dir_prop(spa, DMU_POOL_FEATURES_FOR_WRITE,
		    &spa->spa_feat_for_write_obj, B_TRUE) != 0) {
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		if (spa_dir_prop(spa, DMU_POOL_FEATURE_DESCRIPTIONS,
		    &spa->spa_feat_desc_obj, B_TRUE) != 0) {
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		enabled_feat = fnvlist_alloc();
		unsup_feat = fnvlist_alloc();

		if (!spa_features_check(spa, B_FALSE,
		    unsup_feat, enabled_feat))
			missing_feat_read = B_TRUE;

		if (spa_writeable(spa) ||
		    spa->spa_load_state == SPA_LOAD_TRYIMPORT) {
			if (!spa_features_check(spa, B_TRUE,
			    unsup_feat, enabled_feat)) {
				*missing_feat_writep = B_TRUE;
			}
		}

		fnvlist_add_nvlist(spa->spa_load_info,
		    ZPOOL_CONFIG_ENABLED_FEAT, enabled_feat);

		if (!nvlist_empty(unsup_feat)) {
			fnvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_UNSUP_FEAT, unsup_feat);
		}

		fnvlist_free(enabled_feat);
		fnvlist_free(unsup_feat);

		if (!missing_feat_read) {
			fnvlist_add_boolean(spa->spa_load_info,
			    ZPOOL_CONFIG_CAN_RDONLY);
		}

		/*
		 * If the state is SPA_LOAD_TRYIMPORT, our objective is
		 * twofold: to determine whether the pool is available for
		 * import in read-write mode and (if it is not) whether the
		 * pool is available for import in read-only mode. If the pool
		 * is available for import in read-write mode, it is displayed
		 * as available in userland; if it is not available for import
		 * in read-only mode, it is displayed as unavailable in
		 * userland. If the pool is available for import in read-only
		 * mode but not read-write mode, it is displayed as unavailable
		 * in userland with a special note that the pool is actually
		 * available for open in read-only mode.
		 *
		 * As a result, if the state is SPA_LOAD_TRYIMPORT and we are
		 * missing a feature for write, we must first determine whether
		 * the pool can be opened read-only before returning to
		 * userland in order to know whether to display the
		 * abovementioned note.
		 */
		if (missing_feat_read || (*missing_feat_writep &&
		    spa_writeable(spa))) {
			spa_load_failed(spa, "pool uses unsupported features");
			return (spa_vdev_err(rvd, VDEV_AUX_UNSUP_FEAT,
			    ENOTSUP));
		}

		/*
		 * Load refcounts for ZFS features from disk into an in-memory
		 * cache during SPA initialization.
		 */
		for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
			uint64_t refcount;

			error = feature_get_refcount_from_disk(spa,
			    &spa_feature_table[i], &refcount);
			if (error == 0) {
				spa->spa_feat_refcount_cache[i] = refcount;
			} else if (error == ENOTSUP) {
				spa->spa_feat_refcount_cache[i] =
				    SPA_FEATURE_DISABLED;
			} else {
				spa_load_failed(spa, "error getting refcount "
				    "for feature %s [error=%d]",
				    spa_feature_table[i].fi_guid, error);
				return (spa_vdev_err(rvd,
				    VDEV_AUX_CORRUPT_DATA, EIO));
			}
		}
	}

	if (spa_feature_is_active(spa, SPA_FEATURE_ENABLED_TXG)) {
		if (spa_dir_prop(spa, DMU_POOL_FEATURE_ENABLED_TXG,
		    &spa->spa_feat_enabled_txg_obj, B_TRUE) != 0)
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	return (0);
}

static int
spa_ld_load_special_directories(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	spa->spa_is_initializing = B_TRUE;
	error = dsl_pool_open(spa->spa_dsl_pool);
	spa->spa_is_initializing = B_FALSE;
	if (error != 0) {
		spa_load_failed(spa, "dsl_pool_open failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	return (0);
}

static int
spa_ld_get_props(spa_t *spa)
{
	int error = 0;
	uint64_t obj;
	vdev_t *rvd = spa->spa_root_vdev;

	/* Grab the secret checksum salt from the MOS. */
	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CHECKSUM_SALT, 1,
	    sizeof (spa->spa_cksum_salt.zcs_bytes),
	    spa->spa_cksum_salt.zcs_bytes);
	if (error == ENOENT) {
		/* Generate a new salt for subsequent use */
		(void) random_get_pseudo_bytes(spa->spa_cksum_salt.zcs_bytes,
		    sizeof (spa->spa_cksum_salt.zcs_bytes));
	} else if (error != 0) {
		spa_load_failed(spa, "unable to retrieve checksum salt from "
		    "MOS [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	if (spa_dir_prop(spa, DMU_POOL_SYNC_BPOBJ, &obj, B_TRUE) != 0)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	error = bpobj_open(&spa->spa_deferred_bpobj, spa->spa_meta_objset, obj);
	if (error != 0) {
		spa_load_failed(spa, "error opening deferred-frees bpobj "
		    "[error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	/*
	 * Load the bit that tells us to use the new accounting function
	 * (raid-z deflation).  If we have an older pool, this will not
	 * be present.
	 */
	error = spa_dir_prop(spa, DMU_POOL_DEFLATE, &spa->spa_deflate, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	error = spa_dir_prop(spa, DMU_POOL_CREATION_VERSION,
	    &spa->spa_creation_version, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the persistent error log.  If we have an older pool, this will
	 * not be present.
	 */
	error = spa_dir_prop(spa, DMU_POOL_ERRLOG_LAST, &spa->spa_errlog_last,
	    B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	error = spa_dir_prop(spa, DMU_POOL_ERRLOG_SCRUB,
	    &spa->spa_errlog_scrub, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the history object.  If we have an older pool, this
	 * will not be present.
	 */
	error = spa_dir_prop(spa, DMU_POOL_HISTORY, &spa->spa_history, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	/*
	 * Load the per-vdev ZAP map. If we have an older pool, this will not
	 * be present; in this case, defer its creation to a later time to
	 * avoid dirtying the MOS this early / out of sync context. See
	 * spa_sync_config_object.
	 */

	/* The sentinel is only available in the MOS config. */
	nvlist_t *mos_config;
	if (load_nvlist(spa, spa->spa_config_object, &mos_config) != 0) {
		spa_load_failed(spa, "unable to retrieve MOS config");
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	error = spa_dir_prop(spa, DMU_POOL_VDEV_ZAP_MAP,
	    &spa->spa_all_vdev_zaps, B_FALSE);

	if (error == ENOENT) {
		VERIFY(!nvlist_exists(mos_config,
		    ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS));
		spa->spa_avz_action = AVZ_ACTION_INITIALIZE;
		ASSERT0(vdev_count_verify_zaps(spa->spa_root_vdev));
	} else if (error != 0) {
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	} else if (!nvlist_exists(mos_config, ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS)) {
		/*
		 * An older version of ZFS overwrote the sentinel value, so
		 * we have orphaned per-vdev ZAPs in the MOS. Defer their
		 * destruction to later; see spa_sync_config_object.
		 */
		spa->spa_avz_action = AVZ_ACTION_DESTROY;
		/*
		 * We're assuming that no vdevs have had their ZAPs created
		 * before this. Better be sure of it.
		 */
		ASSERT0(vdev_count_verify_zaps(spa->spa_root_vdev));
	}
	nvlist_free(mos_config);

	spa->spa_delegation = zpool_prop_default_numeric(ZPOOL_PROP_DELEGATION);

	error = spa_dir_prop(spa, DMU_POOL_PROPS, &spa->spa_pool_props_object,
	    B_FALSE);
	if (error && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));

	if (error == 0) {
		uint64_t autoreplace;

		spa_prop_find(spa, ZPOOL_PROP_BOOTFS, &spa->spa_bootfs);
		spa_prop_find(spa, ZPOOL_PROP_AUTOREPLACE, &autoreplace);
		spa_prop_find(spa, ZPOOL_PROP_DELEGATION, &spa->spa_delegation);
		spa_prop_find(spa, ZPOOL_PROP_FAILUREMODE, &spa->spa_failmode);
		spa_prop_find(spa, ZPOOL_PROP_AUTOEXPAND, &spa->spa_autoexpand);
		spa_prop_find(spa, ZPOOL_PROP_DEDUPDITTO,
		    &spa->spa_dedup_ditto);

		spa->spa_autoreplace = (autoreplace != 0);
	}

	/*
	 * If we are importing a pool with missing top-level vdevs,
	 * we enforce that the pool doesn't panic or get suspended on
	 * error since the likelihood of missing data is extremely high.
	 */
	if (spa->spa_missing_tvds > 0 &&
	    spa->spa_failmode != ZIO_FAILURE_MODE_CONTINUE &&
	    spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
		spa_load_note(spa, "forcing failmode to 'continue' "
		    "as some top level vdevs are missing");
		spa->spa_failmode = ZIO_FAILURE_MODE_CONTINUE;
	}

	return (0);
}

static int
spa_ld_open_aux_vdevs(spa_t *spa, spa_import_type_t type)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * If we're assembling the pool from the split-off vdevs of
	 * an existing pool, we don't want to attach the spares & cache
	 * devices.
	 */

	/*
	 * Load any hot spares for this pool.
	 */
	error = spa_dir_prop(spa, DMU_POOL_SPARES, &spa->spa_spares.sav_object,
	    B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	if (error == 0 && type != SPA_IMPORT_ASSEMBLE) {
		ASSERT(spa_version(spa) >= SPA_VERSION_SPARES);
		if (load_nvlist(spa, spa->spa_spares.sav_object,
		    &spa->spa_spares.sav_config) != 0) {
			spa_load_failed(spa, "error loading spares nvlist");
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
	} else if (error == 0) {
		spa->spa_spares.sav_sync = B_TRUE;
	}

	/*
	 * Load any level 2 ARC devices for this pool.
	 */
	error = spa_dir_prop(spa, DMU_POOL_L2CACHE,
	    &spa->spa_l2cache.sav_object, B_FALSE);
	if (error != 0 && error != ENOENT)
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	if (error == 0 && type != SPA_IMPORT_ASSEMBLE) {
		ASSERT(spa_version(spa) >= SPA_VERSION_L2CACHE);
		if (load_nvlist(spa, spa->spa_l2cache.sav_object,
		    &spa->spa_l2cache.sav_config) != 0) {
			spa_load_failed(spa, "error loading l2cache nvlist");
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
		}

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
	} else if (error == 0) {
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	return (0);
}

static int
spa_ld_load_vdev_metadata(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * If the 'autoreplace' property is set, then post a resource notifying
	 * the ZFS DE that it should not issue any faults for unopenable
	 * devices.  We also iterate over the vdevs, and post a sysevent for any
	 * unopenable vdevs so that the normal autoreplace handler can take
	 * over.
	 */
	if (spa->spa_autoreplace && spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
		spa_check_removed(spa->spa_root_vdev);
		/*
		 * For the import case, this is done in spa_import(), because
		 * at this point we're using the spare definitions from
		 * the MOS config, not necessarily from the userland config.
		 */
		if (spa->spa_load_state != SPA_LOAD_IMPORT) {
			spa_aux_check_removed(&spa->spa_spares);
			spa_aux_check_removed(&spa->spa_l2cache);
		}
	}

	/*
	 * Load the vdev metadata such as metaslabs, DTLs, spacemap object, etc.
	 */
	error = vdev_load(rvd);
	if (error != 0) {
		spa_load_failed(spa, "vdev_load failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, error));
	}

	/*
	 * Propagate the leaf DTLs we just loaded all the way up the vdev tree.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_dtl_reassess(rvd, 0, 0, B_FALSE);
	spa_config_exit(spa, SCL_ALL, FTAG);

	return (0);
}

static int
spa_ld_load_dedup_tables(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	error = ddt_load(spa);
	if (error != 0) {
		spa_load_failed(spa, "ddt_load failed [error=%d]", error);
		return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA, EIO));
	}

	return (0);
}

static int
spa_ld_verify_logs(spa_t *spa, spa_import_type_t type, char **ereport)
{
	vdev_t *rvd = spa->spa_root_vdev;

	if (type != SPA_IMPORT_ASSEMBLE && spa_writeable(spa)) {
		boolean_t missing = spa_check_logs(spa);
		if (missing) {
			if (spa->spa_missing_tvds != 0) {
				spa_load_note(spa, "spa_check_logs failed "
				    "so dropping the logs");
			} else {
				*ereport = FM_EREPORT_ZFS_LOG_REPLAY;
				spa_load_failed(spa, "spa_check_logs failed");
				return (spa_vdev_err(rvd, VDEV_AUX_BAD_LOG,
				    ENXIO));
			}
		}
	}

	return (0);
}

static int
spa_ld_verify_pool_data(spa_t *spa)
{
	int error = 0;
	vdev_t *rvd = spa->spa_root_vdev;

	/*
	 * We've successfully opened the pool, verify that we're ready
	 * to start pushing transactions.
	 */
	if (spa->spa_load_state != SPA_LOAD_TRYIMPORT) {
		error = spa_load_verify(spa);
		if (error != 0) {
			spa_load_failed(spa, "spa_load_verify failed "
			    "[error=%d]", error);
			return (spa_vdev_err(rvd, VDEV_AUX_CORRUPT_DATA,
			    error));
		}
	}

	return (0);
}

static void
spa_ld_claim_log_blocks(spa_t *spa)
{
	dmu_tx_t *tx;
	dsl_pool_t *dp = spa_get_dsl(spa);

	/*
	 * Claim log blocks that haven't been committed yet.
	 * This must all happen in a single txg.
	 * Note: spa_claim_max_txg is updated by spa_claim_notify(),
	 * invoked from zil_claim_log_block()'s i/o done callback.
	 * Price of rollback is that we abandon the log.
	 */
	spa->spa_claiming = B_TRUE;

	tx = dmu_tx_create_assigned(dp, spa_first_txg(spa));
	(void) dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
	    zil_claim, tx, DS_FIND_CHILDREN);
	dmu_tx_commit(tx);

	spa->spa_claiming = B_FALSE;

	spa_set_log_state(spa, SPA_LOG_GOOD);
}

static void
spa_ld_check_for_config_update(spa_t *spa, uint64_t config_cache_txg,
    boolean_t update_config_cache)
{
	vdev_t *rvd = spa->spa_root_vdev;
	int need_update = B_FALSE;

	/*
	 * If the config cache is stale, or we have uninitialized
	 * metaslabs (see spa_vdev_add()), then update the config.
	 *
	 * If this is a verbatim import, trust the current
	 * in-core spa_config and update the disk labels.
	 */
	if (update_config_cache || config_cache_txg != spa->spa_config_txg ||
	    spa->spa_load_state == SPA_LOAD_IMPORT ||
	    spa->spa_load_state == SPA_LOAD_RECOVER ||
	    (spa->spa_import_flags & ZFS_IMPORT_VERBATIM))
		need_update = B_TRUE;

	for (int c = 0; c < rvd->vdev_children; c++)
		if (rvd->vdev_child[c]->vdev_ms_array == 0)
			need_update = B_TRUE;

	/*
	 * Update the config cache asychronously in case we're the
	 * root pool, in which case the config cache isn't writable yet.
	 */
	if (need_update)
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
}

static void
spa_ld_prepare_for_reload(spa_t *spa)
{
	int mode = spa->spa_mode;
	int async_suspended = spa->spa_async_suspended;

	spa_unload(spa);
	spa_deactivate(spa);
	spa_activate(spa, mode);

	/*
	 * We save the value of spa_async_suspended as it gets reset to 0 by
	 * spa_unload(). We want to restore it back to the original value before
	 * returning as we might be calling spa_async_resume() later.
	 */
	spa->spa_async_suspended = async_suspended;
}

static int
spa_ld_read_checkpoint_txg(spa_t *spa)
{
	uberblock_t checkpoint;
	int error = 0;

	ASSERT0(spa->spa_checkpoint_txg);
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ZPOOL_CHECKPOINT, sizeof (uint64_t),
	    sizeof (uberblock_t) / sizeof (uint64_t), &checkpoint);

	if (error == ENOENT)
		return (0);

	if (error != 0)
		return (error);

	ASSERT3U(checkpoint.ub_txg, !=, 0);
	ASSERT3U(checkpoint.ub_checkpoint_txg, !=, 0);
	ASSERT3U(checkpoint.ub_timestamp, !=, 0);
	spa->spa_checkpoint_txg = checkpoint.ub_txg;
	spa->spa_checkpoint_info.sci_timestamp = checkpoint.ub_timestamp;

	return (0);
}

static int
spa_ld_mos_init(spa_t *spa, spa_import_type_t type)
{
	int error = 0;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_config_source != SPA_CONFIG_SRC_NONE);

	/*
	 * Never trust the config that is provided unless we are assembling
	 * a pool following a split.
	 * This means don't trust blkptrs and the vdev tree in general. This
	 * also effectively puts the spa in read-only mode since
	 * spa_writeable() checks for spa_trust_config to be true.
	 * We will later load a trusted config from the MOS.
	 */
	if (type != SPA_IMPORT_ASSEMBLE)
		spa->spa_trust_config = B_FALSE;

	/*
	 * Parse the config provided to create a vdev tree.
	 */
	error = spa_ld_parse_config(spa, type);
	if (error != 0)
		return (error);

	/*
	 * Now that we have the vdev tree, try to open each vdev. This involves
	 * opening the underlying physical device, retrieving its geometry and
	 * probing the vdev with a dummy I/O. The state of each vdev will be set
	 * based on the success of those operations. After this we'll be ready
	 * to read from the vdevs.
	 */
	error = spa_ld_open_vdevs(spa);
	if (error != 0)
		return (error);

	/*
	 * Read the label of each vdev and make sure that the GUIDs stored
	 * there match the GUIDs in the config provided.
	 * If we're assembling a new pool that's been split off from an
	 * existing pool, the labels haven't yet been updated so we skip
	 * validation for now.
	 */
	if (type != SPA_IMPORT_ASSEMBLE) {
		error = spa_ld_validate_vdevs(spa);
		if (error != 0)
			return (error);
	}

	/*
	 * Read all vdev labels to find the best uberblock (i.e. latest,
	 * unless spa_load_max_txg is set) and store it in spa_uberblock. We
	 * get the list of features required to read blkptrs in the MOS from
	 * the vdev label with the best uberblock and verify that our version
	 * of zfs supports them all.
	 */
	error = spa_ld_select_uberblock(spa, type);
	if (error != 0)
		return (error);

	/*
	 * Pass that uberblock to the dsl_pool layer which will open the root
	 * blkptr. This blkptr points to the latest version of the MOS and will
	 * allow us to read its contents.
	 */
	error = spa_ld_open_rootbp(spa);
	if (error != 0)
		return (error);

	return (0);
}

static int
spa_ld_checkpoint_rewind(spa_t *spa)
{
	uberblock_t checkpoint;
	int error = 0;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT);

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ZPOOL_CHECKPOINT, sizeof (uint64_t),
	    sizeof (uberblock_t) / sizeof (uint64_t), &checkpoint);

	if (error != 0) {
		spa_load_failed(spa, "unable to retrieve checkpointed "
		    "uberblock from the MOS config [error=%d]", error);

		if (error == ENOENT)
			error = ZFS_ERR_NO_CHECKPOINT;

		return (error);
	}

	ASSERT3U(checkpoint.ub_txg, <, spa->spa_uberblock.ub_txg);
	ASSERT3U(checkpoint.ub_txg, ==, checkpoint.ub_checkpoint_txg);

	/*
	 * We need to update the txg and timestamp of the checkpointed
	 * uberblock to be higher than the latest one. This ensures that
	 * the checkpointed uberblock is selected if we were to close and
	 * reopen the pool right after we've written it in the vdev labels.
	 * (also see block comment in vdev_uberblock_compare)
	 */
	checkpoint.ub_txg = spa->spa_uberblock.ub_txg + 1;
	checkpoint.ub_timestamp = gethrestime_sec();

	/*
	 * Set current uberblock to be the checkpointed uberblock.
	 */
	spa->spa_uberblock = checkpoint;

	/*
	 * If we are doing a normal rewind, then the pool is open for
	 * writing and we sync the "updated" checkpointed uberblock to
	 * disk. Once this is done, we've basically rewound the whole
	 * pool and there is no way back.
	 *
	 * There are cases when we don't want to attempt and sync the
	 * checkpointed uberblock to disk because we are opening a
	 * pool as read-only. Specifically, verifying the checkpointed
	 * state with zdb, and importing the checkpointed state to get
	 * a "preview" of its content.
	 */
	if (spa_writeable(spa)) {
		vdev_t *rvd = spa->spa_root_vdev;

		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		vdev_t *svd[SPA_SYNC_MIN_VDEVS] = { NULL };
		int svdcount = 0;
		int children = rvd->vdev_children;
		int c0 = spa_get_random(children);

		for (int c = 0; c < children; c++) {
			vdev_t *vd = rvd->vdev_child[(c0 + c) % children];

			/* Stop when revisiting the first vdev */
			if (c > 0 && svd[0] == vd)
				break;

			if (vd->vdev_ms_array == 0 || vd->vdev_islog ||
			    !vdev_is_concrete(vd))
				continue;

			svd[svdcount++] = vd;
			if (svdcount == SPA_SYNC_MIN_VDEVS)
				break;
		}
		error = vdev_config_sync(svd, svdcount, spa->spa_first_txg);
		if (error == 0)
			spa->spa_last_synced_guid = rvd->vdev_guid;
		spa_config_exit(spa, SCL_ALL, FTAG);

		if (error != 0) {
			spa_load_failed(spa, "failed to write checkpointed "
			    "uberblock to the vdev labels [error=%d]", error);
			return (error);
		}
	}

	return (0);
}

static int
spa_ld_mos_with_trusted_config(spa_t *spa, spa_import_type_t type,
    boolean_t *update_config_cache)
{
	int error;

	/*
	 * Parse the config for pool, open and validate vdevs,
	 * select an uberblock, and use that uberblock to open
	 * the MOS.
	 */
	error = spa_ld_mos_init(spa, type);
	if (error != 0)
		return (error);

	/*
	 * Retrieve the trusted config stored in the MOS and use it to create
	 * a new, exact version of the vdev tree, then reopen all vdevs.
	 */
	error = spa_ld_trusted_config(spa, type, B_FALSE);
	if (error == EAGAIN) {
		if (update_config_cache != NULL)
			*update_config_cache = B_TRUE;

		/*
		 * Redo the loading process with the trusted config if it is
		 * too different from the untrusted config.
		 */
		spa_ld_prepare_for_reload(spa);
		spa_load_note(spa, "RELOADING");
		error = spa_ld_mos_init(spa, type);
		if (error != 0)
			return (error);

		error = spa_ld_trusted_config(spa, type, B_TRUE);
		if (error != 0)
			return (error);

	} else if (error != 0) {
		return (error);
	}

	return (0);
}

/*
 * Load an existing storage pool, using the config provided. This config
 * describes which vdevs are part of the pool and is later validated against
 * partial configs present in each vdev's label and an entire copy of the
 * config stored in the MOS.
 */
static int
spa_load_impl(spa_t *spa, spa_import_type_t type, char **ereport)
{
	int error = 0;
	boolean_t missing_feat_write = B_FALSE;
	boolean_t checkpoint_rewind =
	    (spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT);
	boolean_t update_config_cache = B_FALSE;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_config_source != SPA_CONFIG_SRC_NONE);

	spa_load_note(spa, "LOADING");

	error = spa_ld_mos_with_trusted_config(spa, type, &update_config_cache);
	if (error != 0)
		return (error);

	/*
	 * If we are rewinding to the checkpoint then we need to repeat
	 * everything we've done so far in this function but this time
	 * selecting the checkpointed uberblock and using that to open
	 * the MOS.
	 */
	if (checkpoint_rewind) {
		/*
		 * If we are rewinding to the checkpoint update config cache
		 * anyway.
		 */
		update_config_cache = B_TRUE;

		/*
		 * Extract the checkpointed uberblock from the current MOS
		 * and use this as the pool's uberblock from now on. If the
		 * pool is imported as writeable we also write the checkpoint
		 * uberblock to the labels, making the rewind permanent.
		 */
		error = spa_ld_checkpoint_rewind(spa);
		if (error != 0)
			return (error);

		/*
		 * Redo the loading process process again with the
		 * checkpointed uberblock.
		 */
		spa_ld_prepare_for_reload(spa);
		spa_load_note(spa, "LOADING checkpointed uberblock");
		error = spa_ld_mos_with_trusted_config(spa, type, NULL);
		if (error != 0)
			return (error);
	}

	/*
	 * Retrieve the checkpoint txg if the pool has a checkpoint.
	 */
	error = spa_ld_read_checkpoint_txg(spa);
	if (error != 0)
		return (error);

	/*
	 * Retrieve the mapping of indirect vdevs. Those vdevs were removed
	 * from the pool and their contents were re-mapped to other vdevs. Note
	 * that everything that we read before this step must have been
	 * rewritten on concrete vdevs after the last device removal was
	 * initiated. Otherwise we could be reading from indirect vdevs before
	 * we have loaded their mappings.
	 */
	error = spa_ld_open_indirect_vdev_metadata(spa);
	if (error != 0)
		return (error);

	/*
	 * Retrieve the full list of active features from the MOS and check if
	 * they are all supported.
	 */
	error = spa_ld_check_features(spa, &missing_feat_write);
	if (error != 0)
		return (error);

	/*
	 * Load several special directories from the MOS needed by the dsl_pool
	 * layer.
	 */
	error = spa_ld_load_special_directories(spa);
	if (error != 0)
		return (error);

	/*
	 * Retrieve pool properties from the MOS.
	 */
	error = spa_ld_get_props(spa);
	if (error != 0)
		return (error);

	/*
	 * Retrieve the list of auxiliary devices - cache devices and spares -
	 * and open them.
	 */
	error = spa_ld_open_aux_vdevs(spa, type);
	if (error != 0)
		return (error);

	/*
	 * Load the metadata for all vdevs. Also check if unopenable devices
	 * should be autoreplaced.
	 */
	error = spa_ld_load_vdev_metadata(spa);
	if (error != 0)
		return (error);

	error = spa_ld_load_dedup_tables(spa);
	if (error != 0)
		return (error);

	/*
	 * Verify the logs now to make sure we don't have any unexpected errors
	 * when we claim log blocks later.
	 */
	error = spa_ld_verify_logs(spa, type, ereport);
	if (error != 0)
		return (error);

	if (missing_feat_write) {
		ASSERT(spa->spa_load_state == SPA_LOAD_TRYIMPORT);

		/*
		 * At this point, we know that we can open the pool in
		 * read-only mode but not read-write mode. We now have enough
		 * information and can return to userland.
		 */
		return (spa_vdev_err(spa->spa_root_vdev, VDEV_AUX_UNSUP_FEAT,
		    ENOTSUP));
	}

	/*
	 * Traverse the last txgs to make sure the pool was left off in a safe
	 * state. When performing an extreme rewind, we verify the whole pool,
	 * which can take a very long time.
	 */
	error = spa_ld_verify_pool_data(spa);
	if (error != 0)
		return (error);

	/*
	 * Calculate the deflated space for the pool. This must be done before
	 * we write anything to the pool because we'd need to update the space
	 * accounting using the deflated sizes.
	 */
	spa_update_dspace(spa);

	/*
	 * We have now retrieved all the information we needed to open the
	 * pool. If we are importing the pool in read-write mode, a few
	 * additional steps must be performed to finish the import.
	 */
	if (spa_writeable(spa) && (spa->spa_load_state == SPA_LOAD_RECOVER ||
	    spa->spa_load_max_txg == UINT64_MAX)) {
		uint64_t config_cache_txg = spa->spa_config_txg;

		ASSERT(spa->spa_load_state != SPA_LOAD_TRYIMPORT);

		/*
		 * In case of a checkpoint rewind, log the original txg
		 * of the checkpointed uberblock.
		 */
		if (checkpoint_rewind) {
			spa_history_log_internal(spa, "checkpoint rewind",
			    NULL, "rewound state to txg=%llu",
			    (u_longlong_t)spa->spa_uberblock.ub_checkpoint_txg);
		}

		/*
		 * Traverse the ZIL and claim all blocks.
		 */
		spa_ld_claim_log_blocks(spa);

		/*
		 * Kick-off the syncing thread.
		 */
		spa->spa_sync_on = B_TRUE;
		txg_sync_start(spa->spa_dsl_pool);

		/*
		 * Wait for all claims to sync.  We sync up to the highest
		 * claimed log block birth time so that claimed log blocks
		 * don't appear to be from the future.  spa_claim_max_txg
		 * will have been set for us by ZIL traversal operations
		 * performed above.
		 */
		txg_wait_synced(spa->spa_dsl_pool, spa->spa_claim_max_txg);

		/*
		 * Check if we need to request an update of the config. On the
		 * next sync, we would update the config stored in vdev labels
		 * and the cachefile (by default /etc/zfs/zpool.cache).
		 */
		spa_ld_check_for_config_update(spa, config_cache_txg,
		    update_config_cache);

		/*
		 * Check all DTLs to see if anything needs resilvering.
		 */
		if (!dsl_scan_resilvering(spa->spa_dsl_pool) &&
		    vdev_resilver_needed(spa->spa_root_vdev, NULL, NULL))
			spa_async_request(spa, SPA_ASYNC_RESILVER);

		/*
		 * Log the fact that we booted up (so that we can detect if
		 * we rebooted in the middle of an operation).
		 */
		spa_history_log_version(spa, "open");

		/*
		 * Delete any inconsistent datasets.
		 */
		(void) dmu_objset_find(spa_name(spa),
		    dsl_destroy_inconsistent, NULL, DS_FIND_CHILDREN);

		/*
		 * Clean up any stale temporary dataset userrefs.
		 */
		dsl_pool_clean_tmp_userrefs(spa->spa_dsl_pool);

		spa_restart_removal(spa);

		spa_spawn_aux_threads(spa);

		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		vdev_initialize_restart(spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	spa_load_note(spa, "LOADED");

	return (0);
}

static int
spa_load_retry(spa_t *spa, spa_load_state_t state)
{
	int mode = spa->spa_mode;

	spa_unload(spa);
	spa_deactivate(spa);

	spa->spa_load_max_txg = spa->spa_uberblock.ub_txg - 1;

	spa_activate(spa, mode);
	spa_async_suspend(spa);

	spa_load_note(spa, "spa_load_retry: rewind, max txg: %llu",
	    (u_longlong_t)spa->spa_load_max_txg);

	return (spa_load(spa, state, SPA_IMPORT_EXISTING));
}

/*
 * If spa_load() fails this function will try loading prior txg's. If
 * 'state' is SPA_LOAD_RECOVER and one of these loads succeeds the pool
 * will be rewound to that txg. If 'state' is not SPA_LOAD_RECOVER this
 * function will not rewind the pool and will return the same error as
 * spa_load().
 */
static int
spa_load_best(spa_t *spa, spa_load_state_t state, uint64_t max_request,
    int rewind_flags)
{
	nvlist_t *loadinfo = NULL;
	nvlist_t *config = NULL;
	int load_error, rewind_error;
	uint64_t safe_rewind_txg;
	uint64_t min_txg;

	if (spa->spa_load_txg && state == SPA_LOAD_RECOVER) {
		spa->spa_load_max_txg = spa->spa_load_txg;
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	} else {
		spa->spa_load_max_txg = max_request;
		if (max_request != UINT64_MAX)
			spa->spa_extreme_rewind = B_TRUE;
	}

	load_error = rewind_error = spa_load(spa, state, SPA_IMPORT_EXISTING);
	if (load_error == 0)
		return (0);
	if (load_error == ZFS_ERR_NO_CHECKPOINT) {
		/*
		 * When attempting checkpoint-rewind on a pool with no
		 * checkpoint, we should not attempt to load uberblocks
		 * from previous txgs when spa_load fails.
		 */
		ASSERT(spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT);
		return (load_error);
	}

	if (spa->spa_root_vdev != NULL)
		config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);

	spa->spa_last_ubsync_txg = spa->spa_uberblock.ub_txg;
	spa->spa_last_ubsync_txg_ts = spa->spa_uberblock.ub_timestamp;

	if (rewind_flags & ZPOOL_NEVER_REWIND) {
		nvlist_free(config);
		return (load_error);
	}

	if (state == SPA_LOAD_RECOVER) {
		/* Price of rolling back is discarding txgs, including log */
		spa_set_log_state(spa, SPA_LOG_CLEAR);
	} else {
		/*
		 * If we aren't rolling back save the load info from our first
		 * import attempt so that we can restore it after attempting
		 * to rewind.
		 */
		loadinfo = spa->spa_load_info;
		spa->spa_load_info = fnvlist_alloc();
	}

	spa->spa_load_max_txg = spa->spa_last_ubsync_txg;
	safe_rewind_txg = spa->spa_last_ubsync_txg - TXG_DEFER_SIZE;
	min_txg = (rewind_flags & ZPOOL_EXTREME_REWIND) ?
	    TXG_INITIAL : safe_rewind_txg;

	/*
	 * Continue as long as we're finding errors, we're still within
	 * the acceptable rewind range, and we're still finding uberblocks
	 */
	while (rewind_error && spa->spa_uberblock.ub_txg >= min_txg &&
	    spa->spa_uberblock.ub_txg <= spa->spa_load_max_txg) {
		if (spa->spa_load_max_txg < safe_rewind_txg)
			spa->spa_extreme_rewind = B_TRUE;
		rewind_error = spa_load_retry(spa, state);
	}

	spa->spa_extreme_rewind = B_FALSE;
	spa->spa_load_max_txg = UINT64_MAX;

	if (config && (rewind_error || state != SPA_LOAD_RECOVER))
		spa_config_set(spa, config);
	else
		nvlist_free(config);

	if (state == SPA_LOAD_RECOVER) {
		ASSERT3P(loadinfo, ==, NULL);
		return (rewind_error);
	} else {
		/* Store the rewind info as part of the initial load info */
		fnvlist_add_nvlist(loadinfo, ZPOOL_CONFIG_REWIND_INFO,
		    spa->spa_load_info);

		/* Restore the initial load info */
		fnvlist_free(spa->spa_load_info);
		spa->spa_load_info = loadinfo;

		return (load_error);
	}
}

/*
 * Pool Open/Import
 *
 * The import case is identical to an open except that the configuration is sent
 * down from userland, instead of grabbed from the configuration cache.  For the
 * case of an open, the pool configuration will exist in the
 * POOL_STATE_UNINITIALIZED state.
 *
 * The stats information (gen/count/ustats) is used to gather vdev statistics at
 * the same time open the pool, without having to keep around the spa_t in some
 * ambiguous state.
 */
static int
spa_open_common(const char *pool, spa_t **spapp, void *tag, nvlist_t *nvpolicy,
    nvlist_t **config)
{
	spa_t *spa;
	spa_load_state_t state = SPA_LOAD_OPEN;
	int error;
	int locked = B_FALSE;
	int firstopen = B_FALSE;

	*spapp = NULL;

	/*
	 * As disgusting as this is, we need to support recursive calls to this
	 * function because dsl_dir_open() is called during spa_load(), and ends
	 * up calling spa_open() again.  The real fix is to figure out how to
	 * avoid dsl_dir_open() calling this in the first place.
	 */
	if (mutex_owner(&spa_namespace_lock) != curthread) {
		mutex_enter(&spa_namespace_lock);
		locked = B_TRUE;
	}

	if ((spa = spa_lookup(pool)) == NULL) {
		if (locked)
			mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ENOENT));
	}

	if (spa->spa_state == POOL_STATE_UNINITIALIZED) {
		zpool_load_policy_t policy;

		firstopen = B_TRUE;

		zpool_get_load_policy(nvpolicy ? nvpolicy : spa->spa_config,
		    &policy);
		if (policy.zlp_rewind & ZPOOL_DO_REWIND)
			state = SPA_LOAD_RECOVER;

		spa_activate(spa, spa_mode_global);

		if (state != SPA_LOAD_RECOVER)
			spa->spa_last_ubsync_txg = spa->spa_load_txg = 0;
		spa->spa_config_source = SPA_CONFIG_SRC_CACHEFILE;

		zfs_dbgmsg("spa_open_common: opening %s", pool);
		error = spa_load_best(spa, state, policy.zlp_txg,
		    policy.zlp_rewind);

		if (error == EBADF) {
			/*
			 * If vdev_validate() returns failure (indicated by
			 * EBADF), it indicates that one of the vdevs indicates
			 * that the pool has been exported or destroyed.  If
			 * this is the case, the config cache is out of sync and
			 * we should remove the pool from the namespace.
			 */
			spa_unload(spa);
			spa_deactivate(spa);
			spa_write_cachefile(spa, B_TRUE, B_TRUE);
			spa_remove(spa);
			if (locked)
				mutex_exit(&spa_namespace_lock);
			return (SET_ERROR(ENOENT));
		}

		if (error) {
			/*
			 * We can't open the pool, but we still have useful
			 * information: the state of each vdev after the
			 * attempted vdev_open().  Return this to the user.
			 */
			if (config != NULL && spa->spa_config) {
				VERIFY(nvlist_dup(spa->spa_config, config,
				    KM_SLEEP) == 0);
				VERIFY(nvlist_add_nvlist(*config,
				    ZPOOL_CONFIG_LOAD_INFO,
				    spa->spa_load_info) == 0);
			}
			spa_unload(spa);
			spa_deactivate(spa);
			spa->spa_last_open_failed = error;
			if (locked)
				mutex_exit(&spa_namespace_lock);
			*spapp = NULL;
			return (error);
		}
	}

	spa_open_ref(spa, tag);

	if (config != NULL)
		*config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);

	/*
	 * If we've recovered the pool, pass back any information we
	 * gathered while doing the load.
	 */
	if (state == SPA_LOAD_RECOVER) {
		VERIFY(nvlist_add_nvlist(*config, ZPOOL_CONFIG_LOAD_INFO,
		    spa->spa_load_info) == 0);
	}

	if (locked) {
		spa->spa_last_open_failed = 0;
		spa->spa_last_ubsync_txg = 0;
		spa->spa_load_txg = 0;
		mutex_exit(&spa_namespace_lock);
#ifdef __FreeBSD__
#ifdef _KERNEL
		if (firstopen)
			zvol_create_minors(spa->spa_name);
#endif
#endif
	}

	*spapp = spa;

	return (0);
}

int
spa_open_rewind(const char *name, spa_t **spapp, void *tag, nvlist_t *policy,
    nvlist_t **config)
{
	return (spa_open_common(name, spapp, tag, policy, config));
}

int
spa_open(const char *name, spa_t **spapp, void *tag)
{
	return (spa_open_common(name, spapp, tag, NULL, NULL));
}

/*
 * Lookup the given spa_t, incrementing the inject count in the process,
 * preventing it from being exported or destroyed.
 */
spa_t *
spa_inject_addref(char *name)
{
	spa_t *spa;

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(name)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (NULL);
	}
	spa->spa_inject_ref++;
	mutex_exit(&spa_namespace_lock);

	return (spa);
}

void
spa_inject_delref(spa_t *spa)
{
	mutex_enter(&spa_namespace_lock);
	spa->spa_inject_ref--;
	mutex_exit(&spa_namespace_lock);
}

/*
 * Add spares device information to the nvlist.
 */
static void
spa_add_spares(spa_t *spa, nvlist_t *config)
{
	nvlist_t **spares;
	uint_t i, nspares;
	nvlist_t *nvroot;
	uint64_t guid;
	vdev_stat_t *vs;
	uint_t vsc;
	uint64_t pool;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	if (spa->spa_spares.sav_count == 0)
		return;

	VERIFY(nvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
	VERIFY(nvlist_lookup_nvlist_array(spa->spa_spares.sav_config,
	    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0);
	if (nspares != 0) {
		VERIFY(nvlist_add_nvlist_array(nvroot,
		    ZPOOL_CONFIG_SPARES, spares, nspares) == 0);
		VERIFY(nvlist_lookup_nvlist_array(nvroot,
		    ZPOOL_CONFIG_SPARES, &spares, &nspares) == 0);

		/*
		 * Go through and find any spares which have since been
		 * repurposed as an active spare.  If this is the case, update
		 * their status appropriately.
		 */
		for (i = 0; i < nspares; i++) {
			VERIFY(nvlist_lookup_uint64(spares[i],
			    ZPOOL_CONFIG_GUID, &guid) == 0);
			if (spa_spare_exists(guid, &pool, NULL) &&
			    pool != 0ULL) {
				VERIFY(nvlist_lookup_uint64_array(
				    spares[i], ZPOOL_CONFIG_VDEV_STATS,
				    (uint64_t **)&vs, &vsc) == 0);
				vs->vs_state = VDEV_STATE_CANT_OPEN;
				vs->vs_aux = VDEV_AUX_SPARED;
			}
		}
	}
}

/*
 * Add l2cache device information to the nvlist, including vdev stats.
 */
static void
spa_add_l2cache(spa_t *spa, nvlist_t *config)
{
	nvlist_t **l2cache;
	uint_t i, j, nl2cache;
	nvlist_t *nvroot;
	uint64_t guid;
	vdev_t *vd;
	vdev_stat_t *vs;
	uint_t vsc;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	if (spa->spa_l2cache.sav_count == 0)
		return;

	VERIFY(nvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
	VERIFY(nvlist_lookup_nvlist_array(spa->spa_l2cache.sav_config,
	    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0);
	if (nl2cache != 0) {
		VERIFY(nvlist_add_nvlist_array(nvroot,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache) == 0);
		VERIFY(nvlist_lookup_nvlist_array(nvroot,
		    ZPOOL_CONFIG_L2CACHE, &l2cache, &nl2cache) == 0);

		/*
		 * Update level 2 cache device stats.
		 */

		for (i = 0; i < nl2cache; i++) {
			VERIFY(nvlist_lookup_uint64(l2cache[i],
			    ZPOOL_CONFIG_GUID, &guid) == 0);

			vd = NULL;
			for (j = 0; j < spa->spa_l2cache.sav_count; j++) {
				if (guid ==
				    spa->spa_l2cache.sav_vdevs[j]->vdev_guid) {
					vd = spa->spa_l2cache.sav_vdevs[j];
					break;
				}
			}
			ASSERT(vd != NULL);

			VERIFY(nvlist_lookup_uint64_array(l2cache[i],
			    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&vs, &vsc)
			    == 0);
			vdev_get_stats(vd, vs);
		}
	}
}

static void
spa_feature_stats_from_disk(spa_t *spa, nvlist_t *features)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	/* We may be unable to read features if pool is suspended. */
	if (spa_suspended(spa))
		return;

	if (spa->spa_feat_for_read_obj != 0) {
		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_feat_for_read_obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			ASSERT(za.za_integer_length == sizeof (uint64_t) &&
			    za.za_num_integers == 1);
			VERIFY0(nvlist_add_uint64(features, za.za_name,
			    za.za_first_integer));
		}
		zap_cursor_fini(&zc);
	}

	if (spa->spa_feat_for_write_obj != 0) {
		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_feat_for_write_obj);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			ASSERT(za.za_integer_length == sizeof (uint64_t) &&
			    za.za_num_integers == 1);
			VERIFY0(nvlist_add_uint64(features, za.za_name,
			    za.za_first_integer));
		}
		zap_cursor_fini(&zc);
	}
}

static void
spa_feature_stats_from_cache(spa_t *spa, nvlist_t *features)
{
	int i;

	for (i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t feature = spa_feature_table[i];
		uint64_t refcount;

		if (feature_get_refcount(spa, &feature, &refcount) != 0)
			continue;

		VERIFY0(nvlist_add_uint64(features, feature.fi_guid, refcount));
	}
}

/*
 * Store a list of pool features and their reference counts in the
 * config.
 *
 * The first time this is called on a spa, allocate a new nvlist, fetch
 * the pool features and reference counts from disk, then save the list
 * in the spa. In subsequent calls on the same spa use the saved nvlist
 * and refresh its values from the cached reference counts.  This
 * ensures we don't block here on I/O on a suspended pool so 'zpool
 * clear' can resume the pool.
 */
static void
spa_add_feature_stats(spa_t *spa, nvlist_t *config)
{
	nvlist_t *features;

	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_READER));

	mutex_enter(&spa->spa_feat_stats_lock);
	features = spa->spa_feat_stats;

	if (features != NULL) {
		spa_feature_stats_from_cache(spa, features);
	} else {
		VERIFY0(nvlist_alloc(&features, NV_UNIQUE_NAME, KM_SLEEP));
		spa->spa_feat_stats = features;
		spa_feature_stats_from_disk(spa, features);
	}

	VERIFY0(nvlist_add_nvlist(config, ZPOOL_CONFIG_FEATURE_STATS,
	    features));

	mutex_exit(&spa->spa_feat_stats_lock);
}

int
spa_get_stats(const char *name, nvlist_t **config,
    char *altroot, size_t buflen)
{
	int error;
	spa_t *spa;

	*config = NULL;
	error = spa_open_common(name, &spa, FTAG, NULL, config);

	if (spa != NULL) {
		/*
		 * This still leaves a window of inconsistency where the spares
		 * or l2cache devices could change and the config would be
		 * self-inconsistent.
		 */
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

		if (*config != NULL) {
			uint64_t loadtimes[2];

			loadtimes[0] = spa->spa_loaded_ts.tv_sec;
			loadtimes[1] = spa->spa_loaded_ts.tv_nsec;
			VERIFY(nvlist_add_uint64_array(*config,
			    ZPOOL_CONFIG_LOADED_TIME, loadtimes, 2) == 0);

			VERIFY(nvlist_add_uint64(*config,
			    ZPOOL_CONFIG_ERRCOUNT,
			    spa_get_errlog_size(spa)) == 0);

			if (spa_suspended(spa))
				VERIFY(nvlist_add_uint64(*config,
				    ZPOOL_CONFIG_SUSPENDED,
				    spa->spa_failmode) == 0);

			spa_add_spares(spa, *config);
			spa_add_l2cache(spa, *config);
			spa_add_feature_stats(spa, *config);
		}
	}

	/*
	 * We want to get the alternate root even for faulted pools, so we cheat
	 * and call spa_lookup() directly.
	 */
	if (altroot) {
		if (spa == NULL) {
			mutex_enter(&spa_namespace_lock);
			spa = spa_lookup(name);
			if (spa)
				spa_altroot(spa, altroot, buflen);
			else
				altroot[0] = '\0';
			spa = NULL;
			mutex_exit(&spa_namespace_lock);
		} else {
			spa_altroot(spa, altroot, buflen);
		}
	}

	if (spa != NULL) {
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		spa_close(spa, FTAG);
	}

	return (error);
}

/*
 * Validate that the auxiliary device array is well formed.  We must have an
 * array of nvlists, each which describes a valid leaf vdev.  If this is an
 * import (mode is VDEV_ALLOC_SPARE), then we allow corrupted spares to be
 * specified, as long as they are well-formed.
 */
static int
spa_validate_aux_devs(spa_t *spa, nvlist_t *nvroot, uint64_t crtxg, int mode,
    spa_aux_vdev_t *sav, const char *config, uint64_t version,
    vdev_labeltype_t label)
{
	nvlist_t **dev;
	uint_t i, ndev;
	vdev_t *vd;
	int error;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	/*
	 * It's acceptable to have no devs specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, config, &dev, &ndev) != 0)
		return (0);

	if (ndev == 0)
		return (SET_ERROR(EINVAL));

	/*
	 * Make sure the pool is formatted with a version that supports this
	 * device type.
	 */
	if (spa_version(spa) < version)
		return (SET_ERROR(ENOTSUP));

	/*
	 * Set the pending device list so we correctly handle device in-use
	 * checking.
	 */
	sav->sav_pending = dev;
	sav->sav_npending = ndev;

	for (i = 0; i < ndev; i++) {
		if ((error = spa_config_parse(spa, &vd, dev[i], NULL, 0,
		    mode)) != 0)
			goto out;

		if (!vd->vdev_ops->vdev_op_leaf) {
			vdev_free(vd);
			error = SET_ERROR(EINVAL);
			goto out;
		}

		/*
		 * The L2ARC currently only supports disk devices in
		 * kernel context.  For user-level testing, we allow it.
		 */
#ifdef _KERNEL
		if ((strcmp(config, ZPOOL_CONFIG_L2CACHE) == 0) &&
		    strcmp(vd->vdev_ops->vdev_op_type, VDEV_TYPE_DISK) != 0) {
			error = SET_ERROR(ENOTBLK);
			vdev_free(vd);
			goto out;
		}
#endif
		vd->vdev_top = vd;

		if ((error = vdev_open(vd)) == 0 &&
		    (error = vdev_label_init(vd, crtxg, label)) == 0) {
			VERIFY(nvlist_add_uint64(dev[i], ZPOOL_CONFIG_GUID,
			    vd->vdev_guid) == 0);
		}

		vdev_free(vd);

		if (error &&
		    (mode != VDEV_ALLOC_SPARE && mode != VDEV_ALLOC_L2CACHE))
			goto out;
		else
			error = 0;
	}

out:
	sav->sav_pending = NULL;
	sav->sav_npending = 0;
	return (error);
}

static int
spa_validate_aux(spa_t *spa, nvlist_t *nvroot, uint64_t crtxg, int mode)
{
	int error;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == SCL_ALL);

	if ((error = spa_validate_aux_devs(spa, nvroot, crtxg, mode,
	    &spa->spa_spares, ZPOOL_CONFIG_SPARES, SPA_VERSION_SPARES,
	    VDEV_LABEL_SPARE)) != 0) {
		return (error);
	}

	return (spa_validate_aux_devs(spa, nvroot, crtxg, mode,
	    &spa->spa_l2cache, ZPOOL_CONFIG_L2CACHE, SPA_VERSION_L2CACHE,
	    VDEV_LABEL_L2CACHE));
}

static void
spa_set_aux_vdevs(spa_aux_vdev_t *sav, nvlist_t **devs, int ndevs,
    const char *config)
{
	int i;

	if (sav->sav_config != NULL) {
		nvlist_t **olddevs;
		uint_t oldndevs;
		nvlist_t **newdevs;

		/*
		 * Generate new dev list by concatentating with the
		 * current dev list.
		 */
		VERIFY(nvlist_lookup_nvlist_array(sav->sav_config, config,
		    &olddevs, &oldndevs) == 0);

		newdevs = kmem_alloc(sizeof (void *) *
		    (ndevs + oldndevs), KM_SLEEP);
		for (i = 0; i < oldndevs; i++)
			VERIFY(nvlist_dup(olddevs[i], &newdevs[i],
			    KM_SLEEP) == 0);
		for (i = 0; i < ndevs; i++)
			VERIFY(nvlist_dup(devs[i], &newdevs[i + oldndevs],
			    KM_SLEEP) == 0);

		VERIFY(nvlist_remove(sav->sav_config, config,
		    DATA_TYPE_NVLIST_ARRAY) == 0);

		VERIFY(nvlist_add_nvlist_array(sav->sav_config,
		    config, newdevs, ndevs + oldndevs) == 0);
		for (i = 0; i < oldndevs + ndevs; i++)
			nvlist_free(newdevs[i]);
		kmem_free(newdevs, (oldndevs + ndevs) * sizeof (void *));
	} else {
		/*
		 * Generate a new dev list.
		 */
		VERIFY(nvlist_alloc(&sav->sav_config, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(sav->sav_config, config,
		    devs, ndevs) == 0);
	}
}

/*
 * Stop and drop level 2 ARC devices
 */
void
spa_l2cache_drop(spa_t *spa)
{
	vdev_t *vd;
	int i;
	spa_aux_vdev_t *sav = &spa->spa_l2cache;

	for (i = 0; i < sav->sav_count; i++) {
		uint64_t pool;

		vd = sav->sav_vdevs[i];
		ASSERT(vd != NULL);

		if (spa_l2cache_exists(vd->vdev_guid, &pool) &&
		    pool != 0ULL && l2arc_vdev_present(vd))
			l2arc_remove_vdev(vd);
	}
}

/*
 * Pool Creation
 */
int
spa_create(const char *pool, nvlist_t *nvroot, nvlist_t *props,
    nvlist_t *zplprops)
{
	spa_t *spa;
	char *altroot = NULL;
	vdev_t *rvd;
	dsl_pool_t *dp;
	dmu_tx_t *tx;
	int error = 0;
	uint64_t txg = TXG_INITIAL;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;
	uint64_t version, obj;
	boolean_t has_features;
	char *poolname;
	nvlist_t *nvl;

	if (nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_TNAME), &poolname) != 0)
		poolname = (char *)pool;

	/*
	 * If this pool already exists, return failure.
	 */
	mutex_enter(&spa_namespace_lock);
	if (spa_lookup(poolname) != NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EEXIST));
	}

	/*
	 * Allocate a new spa_t structure.
	 */
	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, ZPOOL_CONFIG_POOL_NAME, pool);
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);
	spa = spa_add(poolname, nvl, altroot);
	fnvlist_free(nvl);
	spa_activate(spa, spa_mode_global);

	if (props && (error = spa_prop_validate(spa, props))) {
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	/*
	 * Temporary pool names should never be written to disk.
	 */
	if (poolname != pool)
		spa->spa_import_flags |= ZFS_IMPORT_TEMP_NAME;

	has_features = B_FALSE;
	for (nvpair_t *elem = nvlist_next_nvpair(props, NULL);
	    elem != NULL; elem = nvlist_next_nvpair(props, elem)) {
		if (zpool_prop_feature(nvpair_name(elem)))
			has_features = B_TRUE;
	}

	if (has_features || nvlist_lookup_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_VERSION), &version) != 0) {
		version = SPA_VERSION;
	}
	ASSERT(SPA_VERSION_IS_SUPPORTED(version));

	spa->spa_first_txg = txg;
	spa->spa_uberblock.ub_txg = txg - 1;
	spa->spa_uberblock.ub_version = version;
	spa->spa_ubsync = spa->spa_uberblock;
	spa->spa_load_state = SPA_LOAD_CREATE;
	spa->spa_removing_phys.sr_state = DSS_NONE;
	spa->spa_removing_phys.sr_removing_vdev = -1;
	spa->spa_removing_phys.sr_prev_indirect_vdev = -1;
	spa->spa_indirect_vdevs_loaded = B_TRUE;

	/*
	 * Create "The Godfather" zio to hold all async IOs
	 */
	spa->spa_async_zio_root = kmem_alloc(max_ncpus * sizeof (void *),
	    KM_SLEEP);
	for (int i = 0; i < max_ncpus; i++) {
		spa->spa_async_zio_root[i] = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
		    ZIO_FLAG_GODFATHER);
	}

	/*
	 * Create the root vdev.
	 */
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	error = spa_config_parse(spa, &rvd, nvroot, NULL, 0, VDEV_ALLOC_ADD);

	ASSERT(error != 0 || rvd != NULL);
	ASSERT(error != 0 || spa->spa_root_vdev == rvd);

	if (error == 0 && !zfs_allocatable_devs(nvroot))
		error = SET_ERROR(EINVAL);

	if (error == 0 &&
	    (error = vdev_create(rvd, txg, B_FALSE)) == 0 &&
	    (error = spa_validate_aux(spa, nvroot, txg,
	    VDEV_ALLOC_ADD)) == 0) {
		for (int c = 0; c < rvd->vdev_children; c++) {
			vdev_ashift_optimize(rvd->vdev_child[c]);
			vdev_metaslab_set_size(rvd->vdev_child[c]);
			vdev_expand(rvd->vdev_child[c], txg);
		}
	}

	spa_config_exit(spa, SCL_ALL, FTAG);

	if (error != 0) {
		spa_unload(spa);
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	/*
	 * Get the list of spares, if specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		VERIFY(nvlist_alloc(&spa->spa_spares.sav_config, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, spares, nspares) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_spares.sav_sync = B_TRUE;
	}

	/*
	 * Get the list of level 2 cache devices, if specified.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		VERIFY(nvlist_alloc(&spa->spa_l2cache.sav_config,
		    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	spa->spa_is_initializing = B_TRUE;
	spa->spa_dsl_pool = dp = dsl_pool_create(spa, zplprops, txg);
	spa->spa_meta_objset = dp->dp_meta_objset;
	spa->spa_is_initializing = B_FALSE;

	/*
	 * Create DDTs (dedup tables).
	 */
	ddt_create(spa);

	spa_update_dspace(spa);

	tx = dmu_tx_create_assigned(dp, txg);

	/*
	 * Create the pool config object.
	 */
	spa->spa_config_object = dmu_object_alloc(spa->spa_meta_objset,
	    DMU_OT_PACKED_NVLIST, SPA_CONFIG_BLOCKSIZE,
	    DMU_OT_PACKED_NVLIST_SIZE, sizeof (uint64_t), tx);

	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CONFIG,
	    sizeof (uint64_t), 1, &spa->spa_config_object, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add pool config");
	}

	if (spa_version(spa) >= SPA_VERSION_FEATURES)
		spa_feature_create_zap_objects(spa, tx);

	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CREATION_VERSION,
	    sizeof (uint64_t), 1, &version, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add pool version");
	}

	/* Newly created pools with the right version are always deflated. */
	if (version >= SPA_VERSION_RAIDZ_DEFLATE) {
		spa->spa_deflate = TRUE;
		if (zap_add(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
		    sizeof (uint64_t), 1, &spa->spa_deflate, tx) != 0) {
			cmn_err(CE_PANIC, "failed to add deflate");
		}
	}

	/*
	 * Create the deferred-free bpobj.  Turn off compression
	 * because sync-to-convergence takes longer if the blocksize
	 * keeps changing.
	 */
	obj = bpobj_alloc(spa->spa_meta_objset, 1 << 14, tx);
	dmu_object_set_compress(spa->spa_meta_objset, obj,
	    ZIO_COMPRESS_OFF, tx);
	if (zap_add(spa->spa_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_SYNC_BPOBJ,
	    sizeof (uint64_t), 1, &obj, tx) != 0) {
		cmn_err(CE_PANIC, "failed to add bpobj");
	}
	VERIFY3U(0, ==, bpobj_open(&spa->spa_deferred_bpobj,
	    spa->spa_meta_objset, obj));

	/*
	 * Create the pool's history object.
	 */
	if (version >= SPA_VERSION_ZPOOL_HISTORY)
		spa_history_create_obj(spa, tx);

	/*
	 * Generate some random noise for salted checksums to operate on.
	 */
	(void) random_get_pseudo_bytes(spa->spa_cksum_salt.zcs_bytes,
	    sizeof (spa->spa_cksum_salt.zcs_bytes));

	/*
	 * Set pool properties.
	 */
	spa->spa_bootfs = zpool_prop_default_numeric(ZPOOL_PROP_BOOTFS);
	spa->spa_delegation = zpool_prop_default_numeric(ZPOOL_PROP_DELEGATION);
	spa->spa_failmode = zpool_prop_default_numeric(ZPOOL_PROP_FAILUREMODE);
	spa->spa_autoexpand = zpool_prop_default_numeric(ZPOOL_PROP_AUTOEXPAND);

	if (props != NULL) {
		spa_configfile_set(spa, props, B_FALSE);
		spa_sync_props(props, tx);
	}

	dmu_tx_commit(tx);

	spa->spa_sync_on = B_TRUE;
	txg_sync_start(spa->spa_dsl_pool);

	/*
	 * We explicitly wait for the first transaction to complete so that our
	 * bean counters are appropriately updated.
	 */
	txg_wait_synced(spa->spa_dsl_pool, txg);

	spa_spawn_aux_threads(spa);

	spa_write_cachefile(spa, B_FALSE, B_TRUE);
	spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_CREATE);

	spa_history_log_version(spa, "create");

	/*
	 * Don't count references from objsets that are already closed
	 * and are making their way through the eviction process.
	 */
	spa_evicting_os_wait(spa);
	spa->spa_minref = refcount_count(&spa->spa_refcount);
	spa->spa_load_state = SPA_LOAD_NONE;

	mutex_exit(&spa_namespace_lock);

	return (0);
}

#ifdef _KERNEL
#ifdef illumos
/*
 * Get the root pool information from the root disk, then import the root pool
 * during the system boot up time.
 */
extern int vdev_disk_read_rootlabel(char *, char *, nvlist_t **);

static nvlist_t *
spa_generate_rootconf(char *devpath, char *devid, uint64_t *guid)
{
	nvlist_t *config;
	nvlist_t *nvtop, *nvroot;
	uint64_t pgid;

	if (vdev_disk_read_rootlabel(devpath, devid, &config) != 0)
		return (NULL);

	/*
	 * Add this top-level vdev to the child array.
	 */
	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pgid) == 0);
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, guid) == 0);

	/*
	 * Put this pool's top-level vdevs into a root vdev.
	 */
	VERIFY(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, pgid) == 0);
	VERIFY(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &nvtop, 1) == 0);

	/*
	 * Replace the existing vdev_tree with the new root vdev in
	 * this pool's configuration (remove the old, add the new).
	 */
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot) == 0);
	nvlist_free(nvroot);
	return (config);
}

/*
 * Walk the vdev tree and see if we can find a device with "better"
 * configuration. A configuration is "better" if the label on that
 * device has a more recent txg.
 */
static void
spa_alt_rootvdev(vdev_t *vd, vdev_t **avd, uint64_t *txg)
{
	for (int c = 0; c < vd->vdev_children; c++)
		spa_alt_rootvdev(vd->vdev_child[c], avd, txg);

	if (vd->vdev_ops->vdev_op_leaf) {
		nvlist_t *label;
		uint64_t label_txg;

		if (vdev_disk_read_rootlabel(vd->vdev_physpath, vd->vdev_devid,
		    &label) != 0)
			return;

		VERIFY(nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_TXG,
		    &label_txg) == 0);

		/*
		 * Do we have a better boot device?
		 */
		if (label_txg > *txg) {
			*txg = label_txg;
			*avd = vd;
		}
		nvlist_free(label);
	}
}

/*
 * Import a root pool.
 *
 * For x86. devpath_list will consist of devid and/or physpath name of
 * the vdev (e.g. "id1,sd@SSEAGATE..." or "/pci@1f,0/ide@d/disk@0,0:a").
 * The GRUB "findroot" command will return the vdev we should boot.
 *
 * For Sparc, devpath_list consists the physpath name of the booting device
 * no matter the rootpool is a single device pool or a mirrored pool.
 * e.g.
 *	"/pci@1f,0/ide@d/disk@0,0:a"
 */
int
spa_import_rootpool(char *devpath, char *devid)
{
	spa_t *spa;
	vdev_t *rvd, *bvd, *avd = NULL;
	nvlist_t *config, *nvtop;
	uint64_t guid, txg;
	char *pname;
	int error;

	/*
	 * Read the label from the boot device and generate a configuration.
	 */
	config = spa_generate_rootconf(devpath, devid, &guid);
#if defined(_OBP) && defined(_KERNEL)
	if (config == NULL) {
		if (strstr(devpath, "/iscsi/ssd") != NULL) {
			/* iscsi boot */
			get_iscsi_bootpath_phy(devpath);
			config = spa_generate_rootconf(devpath, devid, &guid);
		}
	}
#endif
	if (config == NULL) {
		cmn_err(CE_NOTE, "Cannot read the pool label from '%s'",
		    devpath);
		return (SET_ERROR(EIO));
	}

	VERIFY(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &pname) == 0);
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG, &txg) == 0);

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(pname)) != NULL) {
		/*
		 * Remove the existing root pool from the namespace so that we
		 * can replace it with the correct config we just read in.
		 */
		spa_remove(spa);
	}

	spa = spa_add(pname, config, NULL);
	spa->spa_is_root = B_TRUE;
	spa->spa_import_flags = ZFS_IMPORT_VERBATIM;
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
	    &spa->spa_ubsync.ub_version) != 0)
		spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;

	/*
	 * Build up a vdev tree based on the boot device's label config.
	 */
	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = spa_config_parse(spa, &rvd, nvtop, NULL, 0,
	    VDEV_ALLOC_ROOTPOOL);
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (error) {
		mutex_exit(&spa_namespace_lock);
		nvlist_free(config);
		cmn_err(CE_NOTE, "Can not parse the config for pool '%s'",
		    pname);
		return (error);
	}

	/*
	 * Get the boot vdev.
	 */
	if ((bvd = vdev_lookup_by_guid(rvd, guid)) == NULL) {
		cmn_err(CE_NOTE, "Can not find the boot vdev for guid %llu",
		    (u_longlong_t)guid);
		error = SET_ERROR(ENOENT);
		goto out;
	}

	/*
	 * Determine if there is a better boot device.
	 */
	avd = bvd;
	spa_alt_rootvdev(rvd, &avd, &txg);
	if (avd != bvd) {
		cmn_err(CE_NOTE, "The boot device is 'degraded'. Please "
		    "try booting from '%s'", avd->vdev_path);
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * If the boot device is part of a spare vdev then ensure that
	 * we're booting off the active spare.
	 */
	if (bvd->vdev_parent->vdev_ops == &vdev_spare_ops &&
	    !bvd->vdev_isspare) {
		cmn_err(CE_NOTE, "The boot device is currently spared. Please "
		    "try booting from '%s'",
		    bvd->vdev_parent->
		    vdev_child[bvd->vdev_parent->vdev_children - 1]->vdev_path);
		error = SET_ERROR(EINVAL);
		goto out;
	}

	error = 0;
out:
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_free(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	mutex_exit(&spa_namespace_lock);

	nvlist_free(config);
	return (error);
}

#else	/* !illumos */

extern int vdev_geom_read_pool_label(const char *name, nvlist_t ***configs,
    uint64_t *count);

static nvlist_t *
spa_generate_rootconf(const char *name)
{
	nvlist_t **configs, **tops;
	nvlist_t *config;
	nvlist_t *best_cfg, *nvtop, *nvroot;
	uint64_t *holes;
	uint64_t best_txg;
	uint64_t nchildren;
	uint64_t pgid;
	uint64_t count;
	uint64_t i;
	uint_t   nholes;

	if (vdev_geom_read_pool_label(name, &configs, &count) != 0)
		return (NULL);

	ASSERT3U(count, !=, 0);
	best_txg = 0;
	for (i = 0; i < count; i++) {
		uint64_t txg;

		VERIFY(nvlist_lookup_uint64(configs[i], ZPOOL_CONFIG_POOL_TXG,
		    &txg) == 0);
		if (txg > best_txg) {
			best_txg = txg;
			best_cfg = configs[i];
		}
	}

	nchildren = 1;
	nvlist_lookup_uint64(best_cfg, ZPOOL_CONFIG_VDEV_CHILDREN, &nchildren);
	holes = NULL;
	nvlist_lookup_uint64_array(best_cfg, ZPOOL_CONFIG_HOLE_ARRAY,
	    &holes, &nholes);

	tops = kmem_zalloc(nchildren * sizeof(void *), KM_SLEEP);
	for (i = 0; i < nchildren; i++) {
		if (i >= count)
			break;
		if (configs[i] == NULL)
			continue;
		VERIFY(nvlist_lookup_nvlist(configs[i], ZPOOL_CONFIG_VDEV_TREE,
		    &nvtop) == 0);
		nvlist_dup(nvtop, &tops[i], KM_SLEEP);
	}
	for (i = 0; holes != NULL && i < nholes; i++) {
		if (i >= nchildren)
			continue;
		if (tops[holes[i]] != NULL)
			continue;
		nvlist_alloc(&tops[holes[i]], NV_UNIQUE_NAME, KM_SLEEP);
		VERIFY(nvlist_add_string(tops[holes[i]], ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_HOLE) == 0);
		VERIFY(nvlist_add_uint64(tops[holes[i]], ZPOOL_CONFIG_ID,
		    holes[i]) == 0);
		VERIFY(nvlist_add_uint64(tops[holes[i]], ZPOOL_CONFIG_GUID,
		    0) == 0);
	}
	for (i = 0; i < nchildren; i++) {
		if (tops[i] != NULL)
			continue;
		nvlist_alloc(&tops[i], NV_UNIQUE_NAME, KM_SLEEP);
		VERIFY(nvlist_add_string(tops[i], ZPOOL_CONFIG_TYPE,
		    VDEV_TYPE_MISSING) == 0);
		VERIFY(nvlist_add_uint64(tops[i], ZPOOL_CONFIG_ID,
		    i) == 0);
		VERIFY(nvlist_add_uint64(tops[i], ZPOOL_CONFIG_GUID,
		    0) == 0);
	}

	/*
	 * Create pool config based on the best vdev config.
	 */
	nvlist_dup(best_cfg, &config, KM_SLEEP);

	/*
	 * Put this pool's top-level vdevs into a root vdev.
	 */
	VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &pgid) == 0);
	VERIFY(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL) == 0);
	VERIFY(nvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, pgid) == 0);
	VERIFY(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    tops, nchildren) == 0);

	/*
	 * Replace the existing vdev_tree with the new root vdev in
	 * this pool's configuration (remove the old, add the new).
	 */
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot) == 0);

	/*
	 * Drop vdev config elements that should not be present at pool level.
	 */
	nvlist_remove(config, ZPOOL_CONFIG_GUID, DATA_TYPE_UINT64);
	nvlist_remove(config, ZPOOL_CONFIG_TOP_GUID, DATA_TYPE_UINT64);

	for (i = 0; i < count; i++)
		nvlist_free(configs[i]);
	kmem_free(configs, count * sizeof(void *));
	for (i = 0; i < nchildren; i++)
		nvlist_free(tops[i]);
	kmem_free(tops, nchildren * sizeof(void *));
	nvlist_free(nvroot);
	return (config);
}

int
spa_import_rootpool(const char *name)
{
	spa_t *spa;
	vdev_t *rvd, *bvd, *avd = NULL;
	nvlist_t *config, *nvtop;
	uint64_t txg;
	char *pname;
	int error;

	/*
	 * Read the label from the boot device and generate a configuration.
	 */
	config = spa_generate_rootconf(name);

	mutex_enter(&spa_namespace_lock);
	if (config != NULL) {
		VERIFY(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &pname) == 0 && strcmp(name, pname) == 0);
		VERIFY(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG, &txg)
		    == 0);

		if ((spa = spa_lookup(pname)) != NULL) {
			/*
			 * The pool could already be imported,
			 * e.g., after reboot -r.
			 */
			if (spa->spa_state == POOL_STATE_ACTIVE) {
				mutex_exit(&spa_namespace_lock);
				nvlist_free(config);
				return (0);
			}

			/*
			 * Remove the existing root pool from the namespace so
			 * that we can replace it with the correct config
			 * we just read in.
			 */
			spa_remove(spa);
		}
		spa = spa_add(pname, config, NULL);

		/*
		 * Set spa_ubsync.ub_version as it can be used in vdev_alloc()
		 * via spa_version().
		 */
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
		    &spa->spa_ubsync.ub_version) != 0)
			spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;
	} else if ((spa = spa_lookup(name)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		nvlist_free(config);
		cmn_err(CE_NOTE, "Cannot find the pool label for '%s'",
		    name);
		return (EIO);
	} else {
		VERIFY(nvlist_dup(spa->spa_config, &config, KM_SLEEP) == 0);
	}
	spa->spa_is_root = B_TRUE;
	spa->spa_import_flags = ZFS_IMPORT_VERBATIM;

	/*
	 * Build up a vdev tree based on the boot device's label config.
	 */
	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = spa_config_parse(spa, &rvd, nvtop, NULL, 0,
	    VDEV_ALLOC_ROOTPOOL);
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (error) {
		mutex_exit(&spa_namespace_lock);
		nvlist_free(config);
		cmn_err(CE_NOTE, "Can not parse the config for pool '%s'",
		    pname);
		return (error);
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_free(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	mutex_exit(&spa_namespace_lock);

	nvlist_free(config);
	return (0);
}

#endif	/* illumos */
#endif	/* _KERNEL */

/*
 * Import a non-root pool into the system.
 */
int
spa_import(const char *pool, nvlist_t *config, nvlist_t *props, uint64_t flags)
{
	spa_t *spa;
	char *altroot = NULL;
	spa_load_state_t state = SPA_LOAD_IMPORT;
	zpool_load_policy_t policy;
	uint64_t mode = spa_mode_global;
	uint64_t readonly = B_FALSE;
	int error;
	nvlist_t *nvroot;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	/*
	 * If a pool with this name exists, return failure.
	 */
	mutex_enter(&spa_namespace_lock);
	if (spa_lookup(pool) != NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EEXIST));
	}

	/*
	 * Create and initialize the spa structure.
	 */
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);
	(void) nvlist_lookup_uint64(props,
	    zpool_prop_to_name(ZPOOL_PROP_READONLY), &readonly);
	if (readonly)
		mode = FREAD;
	spa = spa_add(pool, config, altroot);
	spa->spa_import_flags = flags;

	/*
	 * Verbatim import - Take a pool and insert it into the namespace
	 * as if it had been loaded at boot.
	 */
	if (spa->spa_import_flags & ZFS_IMPORT_VERBATIM) {
		if (props != NULL)
			spa_configfile_set(spa, props, B_FALSE);

		spa_write_cachefile(spa, B_FALSE, B_TRUE);
		spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_IMPORT);
		zfs_dbgmsg("spa_import: verbatim import of %s", pool);
		mutex_exit(&spa_namespace_lock);
		return (0);
	}

	spa_activate(spa, mode);

	/*
	 * Don't start async tasks until we know everything is healthy.
	 */
	spa_async_suspend(spa);

	zpool_get_load_policy(config, &policy);
	if (policy.zlp_rewind & ZPOOL_DO_REWIND)
		state = SPA_LOAD_RECOVER;

	spa->spa_config_source = SPA_CONFIG_SRC_TRYIMPORT;

	if (state != SPA_LOAD_RECOVER) {
		spa->spa_last_ubsync_txg = spa->spa_load_txg = 0;
		zfs_dbgmsg("spa_import: importing %s", pool);
	} else {
		zfs_dbgmsg("spa_import: importing %s, max_txg=%lld "
		    "(RECOVERY MODE)", pool, (longlong_t)policy.zlp_txg);
	}
	error = spa_load_best(spa, state, policy.zlp_txg, policy.zlp_rewind);

	/*
	 * Propagate anything learned while loading the pool and pass it
	 * back to caller (i.e. rewind info, missing devices, etc).
	 */
	VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_LOAD_INFO,
	    spa->spa_load_info) == 0);

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	/*
	 * Toss any existing sparelist, as it doesn't have any validity
	 * anymore, and conflicts with spa_has_spare().
	 */
	if (spa->spa_spares.sav_config) {
		nvlist_free(spa->spa_spares.sav_config);
		spa->spa_spares.sav_config = NULL;
		spa_load_spares(spa);
	}
	if (spa->spa_l2cache.sav_config) {
		nvlist_free(spa->spa_l2cache.sav_config);
		spa->spa_l2cache.sav_config = NULL;
		spa_load_l2cache(spa);
	}

	VERIFY(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	if (error == 0)
		error = spa_validate_aux(spa, nvroot, -1ULL,
		    VDEV_ALLOC_SPARE);
	if (error == 0)
		error = spa_validate_aux(spa, nvroot, -1ULL,
		    VDEV_ALLOC_L2CACHE);
	spa_config_exit(spa, SCL_ALL, FTAG);

	if (props != NULL)
		spa_configfile_set(spa, props, B_FALSE);

	if (error != 0 || (props && spa_writeable(spa) &&
	    (error = spa_prop_set(spa, props)))) {
		spa_unload(spa);
		spa_deactivate(spa);
		spa_remove(spa);
		mutex_exit(&spa_namespace_lock);
		return (error);
	}

	spa_async_resume(spa);

	/*
	 * Override any spares and level 2 cache devices as specified by
	 * the user, as these may have correct device names/devids, etc.
	 */
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		if (spa->spa_spares.sav_config)
			VERIFY(nvlist_remove(spa->spa_spares.sav_config,
			    ZPOOL_CONFIG_SPARES, DATA_TYPE_NVLIST_ARRAY) == 0);
		else
			VERIFY(nvlist_alloc(&spa->spa_spares.sav_config,
			    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_spares.sav_config,
		    ZPOOL_CONFIG_SPARES, spares, nspares) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_spares(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_spares.sav_sync = B_TRUE;
	}
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
	    &l2cache, &nl2cache) == 0) {
		if (spa->spa_l2cache.sav_config)
			VERIFY(nvlist_remove(spa->spa_l2cache.sav_config,
			    ZPOOL_CONFIG_L2CACHE, DATA_TYPE_NVLIST_ARRAY) == 0);
		else
			VERIFY(nvlist_alloc(&spa->spa_l2cache.sav_config,
			    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_nvlist_array(spa->spa_l2cache.sav_config,
		    ZPOOL_CONFIG_L2CACHE, l2cache, nl2cache) == 0);
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
		spa_load_l2cache(spa);
		spa_config_exit(spa, SCL_ALL, FTAG);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	/*
	 * Check for any removed devices.
	 */
	if (spa->spa_autoreplace) {
		spa_aux_check_removed(&spa->spa_spares);
		spa_aux_check_removed(&spa->spa_l2cache);
	}

	if (spa_writeable(spa)) {
		/*
		 * Update the config cache to include the newly-imported pool.
		 */
		spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
	}

	/*
	 * It's possible that the pool was expanded while it was exported.
	 * We kick off an async task to handle this for us.
	 */
	spa_async_request(spa, SPA_ASYNC_AUTOEXPAND);

	spa_history_log_version(spa, "import");

	spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_IMPORT);

	mutex_exit(&spa_namespace_lock);

#ifdef __FreeBSD__
#ifdef _KERNEL
	zvol_create_minors(pool);
#endif
#endif
	return (0);
}

nvlist_t *
spa_tryimport(nvlist_t *tryconfig)
{
	nvlist_t *config = NULL;
	char *poolname, *cachefile;
	spa_t *spa;
	uint64_t state;
	int error;
	zpool_load_policy_t policy;

	if (nvlist_lookup_string(tryconfig, ZPOOL_CONFIG_POOL_NAME, &poolname))
		return (NULL);

	if (nvlist_lookup_uint64(tryconfig, ZPOOL_CONFIG_POOL_STATE, &state))
		return (NULL);

	/*
	 * Create and initialize the spa structure.
	 */
	mutex_enter(&spa_namespace_lock);
	spa = spa_add(TRYIMPORT_NAME, tryconfig, NULL);
	spa_activate(spa, FREAD);

	/*
	 * Rewind pool if a max txg was provided.
	 */
	zpool_get_load_policy(spa->spa_config, &policy);
	if (policy.zlp_txg != UINT64_MAX) {
		spa->spa_load_max_txg = policy.zlp_txg;
		spa->spa_extreme_rewind = B_TRUE;
		zfs_dbgmsg("spa_tryimport: importing %s, max_txg=%lld",
		    poolname, (longlong_t)policy.zlp_txg);
	} else {
		zfs_dbgmsg("spa_tryimport: importing %s", poolname);
	}

	if (nvlist_lookup_string(tryconfig, ZPOOL_CONFIG_CACHEFILE, &cachefile)
	    == 0) {
		zfs_dbgmsg("spa_tryimport: using cachefile '%s'", cachefile);
		spa->spa_config_source = SPA_CONFIG_SRC_CACHEFILE;
	} else {
		spa->spa_config_source = SPA_CONFIG_SRC_SCAN;
	}

	error = spa_load(spa, SPA_LOAD_TRYIMPORT, SPA_IMPORT_EXISTING);

	/*
	 * If 'tryconfig' was at least parsable, return the current config.
	 */
	if (spa->spa_root_vdev != NULL) {
		config = spa_config_generate(spa, NULL, -1ULL, B_TRUE);
		VERIFY(nvlist_add_string(config, ZPOOL_CONFIG_POOL_NAME,
		    poolname) == 0);
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    state) == 0);
		VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_TIMESTAMP,
		    spa->spa_uberblock.ub_timestamp) == 0);
		VERIFY(nvlist_add_nvlist(config, ZPOOL_CONFIG_LOAD_INFO,
		    spa->spa_load_info) == 0);

		/*
		 * If the bootfs property exists on this pool then we
		 * copy it out so that external consumers can tell which
		 * pools are bootable.
		 */
		if ((!error || error == EEXIST) && spa->spa_bootfs) {
			char *tmpname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

			/*
			 * We have to play games with the name since the
			 * pool was opened as TRYIMPORT_NAME.
			 */
			if (dsl_dsobj_to_dsname(spa_name(spa),
			    spa->spa_bootfs, tmpname) == 0) {
				char *cp;
				char *dsname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

				cp = strchr(tmpname, '/');
				if (cp == NULL) {
					(void) strlcpy(dsname, tmpname,
					    MAXPATHLEN);
				} else {
					(void) snprintf(dsname, MAXPATHLEN,
					    "%s/%s", poolname, ++cp);
				}
				VERIFY(nvlist_add_string(config,
				    ZPOOL_CONFIG_BOOTFS, dsname) == 0);
				kmem_free(dsname, MAXPATHLEN);
			}
			kmem_free(tmpname, MAXPATHLEN);
		}

		/*
		 * Add the list of hot spares and level 2 cache devices.
		 */
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		spa_add_spares(spa, config);
		spa_add_l2cache(spa, config);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	spa_unload(spa);
	spa_deactivate(spa);
	spa_remove(spa);
	mutex_exit(&spa_namespace_lock);

	return (config);
}

/*
 * Pool export/destroy
 *
 * The act of destroying or exporting a pool is very simple.  We make sure there
 * is no more pending I/O and any references to the pool are gone.  Then, we
 * update the pool state and sync all the labels to disk, removing the
 * configuration from the cache afterwards. If the 'hardforce' flag is set, then
 * we don't sync the labels or remove the configuration cache.
 */
static int
spa_export_common(char *pool, int new_state, nvlist_t **oldconfig,
    boolean_t force, boolean_t hardforce)
{
	spa_t *spa;

	if (oldconfig)
		*oldconfig = NULL;

	if (!(spa_mode_global & FWRITE))
		return (SET_ERROR(EROFS));

	mutex_enter(&spa_namespace_lock);
	if ((spa = spa_lookup(pool)) == NULL) {
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ENOENT));
	}

	/*
	 * Put a hold on the pool, drop the namespace lock, stop async tasks,
	 * reacquire the namespace lock, and see if we can export.
	 */
	spa_open_ref(spa, FTAG);
	mutex_exit(&spa_namespace_lock);
	spa_async_suspend(spa);
	mutex_enter(&spa_namespace_lock);
	spa_close(spa, FTAG);

	/*
	 * The pool will be in core if it's openable,
	 * in which case we can modify its state.
	 */
	if (spa->spa_state != POOL_STATE_UNINITIALIZED && spa->spa_sync_on) {

		/*
		 * Objsets may be open only because they're dirty, so we
		 * have to force it to sync before checking spa_refcnt.
		 */
		txg_wait_synced(spa->spa_dsl_pool, 0);
		spa_evicting_os_wait(spa);

		/*
		 * A pool cannot be exported or destroyed if there are active
		 * references.  If we are resetting a pool, allow references by
		 * fault injection handlers.
		 */
		if (!spa_refcount_zero(spa) ||
		    (spa->spa_inject_ref != 0 &&
		    new_state != POOL_STATE_UNINITIALIZED)) {
			spa_async_resume(spa);
			mutex_exit(&spa_namespace_lock);
			return (SET_ERROR(EBUSY));
		}

		/*
		 * A pool cannot be exported if it has an active shared spare.
		 * This is to prevent other pools stealing the active spare
		 * from an exported pool. At user's own will, such pool can
		 * be forcedly exported.
		 */
		if (!force && new_state == POOL_STATE_EXPORTED &&
		    spa_has_active_shared_spare(spa)) {
			spa_async_resume(spa);
			mutex_exit(&spa_namespace_lock);
			return (SET_ERROR(EXDEV));
		}

		/*
		 * We're about to export or destroy this pool. Make sure
		 * we stop all initializtion activity here before we
		 * set the spa_final_txg. This will ensure that all
		 * dirty data resulting from the initialization is
		 * committed to disk before we unload the pool.
		 */
		if (spa->spa_root_vdev != NULL) {
			vdev_initialize_stop_all(spa->spa_root_vdev,
			    VDEV_INITIALIZE_ACTIVE);
		}

		/*
		 * We want this to be reflected on every label,
		 * so mark them all dirty.  spa_unload() will do the
		 * final sync that pushes these changes out.
		 */
		if (new_state != POOL_STATE_UNINITIALIZED && !hardforce) {
			spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
			spa->spa_state = new_state;
			spa->spa_final_txg = spa_last_synced_txg(spa) +
			    TXG_DEFER_SIZE + 1;
			vdev_config_dirty(spa->spa_root_vdev);
			spa_config_exit(spa, SCL_ALL, FTAG);
		}
	}

	spa_event_notify(spa, NULL, NULL, ESC_ZFS_POOL_DESTROY);

	if (spa->spa_state != POOL_STATE_UNINITIALIZED) {
		spa_unload(spa);
		spa_deactivate(spa);
	}

	if (oldconfig && spa->spa_config)
		VERIFY(nvlist_dup(spa->spa_config, oldconfig, 0) == 0);

	if (new_state != POOL_STATE_UNINITIALIZED) {
		if (!hardforce)
			spa_write_cachefile(spa, B_TRUE, B_TRUE);
		spa_remove(spa);
	}
	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Destroy a storage pool.
 */
int
spa_destroy(char *pool)
{
	return (spa_export_common(pool, POOL_STATE_DESTROYED, NULL,
	    B_FALSE, B_FALSE));
}

/*
 * Export a storage pool.
 */
int
spa_export(char *pool, nvlist_t **oldconfig, boolean_t force,
    boolean_t hardforce)
{
	return (spa_export_common(pool, POOL_STATE_EXPORTED, oldconfig,
	    force, hardforce));
}

/*
 * Similar to spa_export(), this unloads the spa_t without actually removing it
 * from the namespace in any way.
 */
int
spa_reset(char *pool)
{
	return (spa_export_common(pool, POOL_STATE_UNINITIALIZED, NULL,
	    B_FALSE, B_FALSE));
}

/*
 * ==========================================================================
 * Device manipulation
 * ==========================================================================
 */

/*
 * Add a device to a storage pool.
 */
int
spa_vdev_add(spa_t *spa, nvlist_t *nvroot)
{
	uint64_t txg, id;
	int error;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd, *tvd;
	nvlist_t **spares, **l2cache;
	uint_t nspares, nl2cache;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	if ((error = spa_config_parse(spa, &vd, nvroot, NULL, 0,
	    VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, NULL, txg, error));

	spa->spa_pending_vdev = vd;	/* spa_vdev_exit() will clear this */

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES, &spares,
	    &nspares) != 0)
		nspares = 0;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE, &l2cache,
	    &nl2cache) != 0)
		nl2cache = 0;

	if (vd->vdev_children == 0 && nspares == 0 && nl2cache == 0)
		return (spa_vdev_exit(spa, vd, txg, EINVAL));

	if (vd->vdev_children != 0 &&
	    (error = vdev_create(vd, txg, B_FALSE)) != 0)
		return (spa_vdev_exit(spa, vd, txg, error));

	/*
	 * We must validate the spares and l2cache devices after checking the
	 * children.  Otherwise, vdev_inuse() will blindly overwrite the spare.
	 */
	if ((error = spa_validate_aux(spa, nvroot, txg, VDEV_ALLOC_ADD)) != 0)
		return (spa_vdev_exit(spa, vd, txg, error));

	/*
	 * If we are in the middle of a device removal, we can only add
	 * devices which match the existing devices in the pool.
	 * If we are in the middle of a removal, or have some indirect
	 * vdevs, we can not add raidz toplevels.
	 */
	if (spa->spa_vdev_removal != NULL ||
	    spa->spa_removing_phys.sr_prev_indirect_vdev != -1) {
		for (int c = 0; c < vd->vdev_children; c++) {
			tvd = vd->vdev_child[c];
			if (spa->spa_vdev_removal != NULL &&
			    tvd->vdev_ashift != spa->spa_max_ashift) {
				return (spa_vdev_exit(spa, vd, txg, EINVAL));
			}
			/* Fail if top level vdev is raidz */
			if (tvd->vdev_ops == &vdev_raidz_ops) {
				return (spa_vdev_exit(spa, vd, txg, EINVAL));
			}
			/*
			 * Need the top level mirror to be
			 * a mirror of leaf vdevs only
			 */
			if (tvd->vdev_ops == &vdev_mirror_ops) {
				for (uint64_t cid = 0;
				    cid < tvd->vdev_children; cid++) {
					vdev_t *cvd = tvd->vdev_child[cid];
					if (!cvd->vdev_ops->vdev_op_leaf) {
						return (spa_vdev_exit(spa, vd,
						    txg, EINVAL));
					}
				}
			}
		}
	}

	for (int c = 0; c < vd->vdev_children; c++) {

		/*
		 * Set the vdev id to the first hole, if one exists.
		 */
		for (id = 0; id < rvd->vdev_children; id++) {
			if (rvd->vdev_child[id]->vdev_ishole) {
				vdev_free(rvd->vdev_child[id]);
				break;
			}
		}
		tvd = vd->vdev_child[c];
		vdev_remove_child(vd, tvd);
		tvd->vdev_id = id;
		vdev_add_child(rvd, tvd);
		vdev_config_dirty(tvd);
	}

	if (nspares != 0) {
		spa_set_aux_vdevs(&spa->spa_spares, spares, nspares,
		    ZPOOL_CONFIG_SPARES);
		spa_load_spares(spa);
		spa->spa_spares.sav_sync = B_TRUE;
	}

	if (nl2cache != 0) {
		spa_set_aux_vdevs(&spa->spa_l2cache, l2cache, nl2cache,
		    ZPOOL_CONFIG_L2CACHE);
		spa_load_l2cache(spa);
		spa->spa_l2cache.sav_sync = B_TRUE;
	}

	/*
	 * We have to be careful when adding new vdevs to an existing pool.
	 * If other threads start allocating from these vdevs before we
	 * sync the config cache, and we lose power, then upon reboot we may
	 * fail to open the pool because there are DVAs that the config cache
	 * can't translate.  Therefore, we first add the vdevs without
	 * initializing metaslabs; sync the config cache (via spa_vdev_exit());
	 * and then let spa_config_update() initialize the new metaslabs.
	 *
	 * spa_load() checks for added-but-not-initialized vdevs, so that
	 * if we lose power at any point in this sequence, the remaining
	 * steps will be completed the next time we load the pool.
	 */
	(void) spa_vdev_exit(spa, vd, txg, 0);

	mutex_enter(&spa_namespace_lock);
	spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
	spa_event_notify(spa, NULL, NULL, ESC_ZFS_VDEV_ADD);
	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Attach a device to a mirror.  The arguments are the path to any device
 * in the mirror, and the nvroot for the new device.  If the path specifies
 * a device that is not mirrored, we automatically insert the mirror vdev.
 *
 * If 'replacing' is specified, the new device is intended to replace the
 * existing device; in this case the two devices are made into their own
 * mirror using the 'replacing' vdev, which is functionally identical to
 * the mirror vdev (it actually reuses all the same ops) but has a few
 * extra rules: you can't attach to it after it's been created, and upon
 * completion of resilvering, the first disk (the one being replaced)
 * is automatically detached.
 */
int
spa_vdev_attach(spa_t *spa, uint64_t guid, nvlist_t *nvroot, int replacing)
{
	uint64_t txg, dtl_max_txg;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *oldvd, *newvd, *newrootvd, *pvd, *tvd;
	vdev_ops_t *pvops;
	char *oldvdpath, *newvdpath;
	int newvd_isspare;
	int error;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	oldvd = spa_lookup_by_guid(spa, guid, B_FALSE);

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	if (spa->spa_vdev_removal != NULL)
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	if (oldvd == NULL)
		return (spa_vdev_exit(spa, NULL, txg, ENODEV));

	if (!oldvd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	pvd = oldvd->vdev_parent;

	if ((error = spa_config_parse(spa, &newrootvd, nvroot, NULL, 0,
	    VDEV_ALLOC_ATTACH)) != 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	if (newrootvd->vdev_children != 1)
		return (spa_vdev_exit(spa, newrootvd, txg, EINVAL));

	newvd = newrootvd->vdev_child[0];

	if (!newvd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, newrootvd, txg, EINVAL));

	if ((error = vdev_create(newrootvd, txg, replacing)) != 0)
		return (spa_vdev_exit(spa, newrootvd, txg, error));

	/*
	 * Spares can't replace logs
	 */
	if (oldvd->vdev_top->vdev_islog && newvd->vdev_isspare)
		return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

	if (!replacing) {
		/*
		 * For attach, the only allowable parent is a mirror or the root
		 * vdev.
		 */
		if (pvd->vdev_ops != &vdev_mirror_ops &&
		    pvd->vdev_ops != &vdev_root_ops)
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		pvops = &vdev_mirror_ops;
	} else {
		/*
		 * Active hot spares can only be replaced by inactive hot
		 * spares.
		 */
		if (pvd->vdev_ops == &vdev_spare_ops &&
		    oldvd->vdev_isspare &&
		    !spa_has_spare(spa, newvd->vdev_guid))
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));

		/*
		 * If the source is a hot spare, and the parent isn't already a
		 * spare, then we want to create a new hot spare.  Otherwise, we
		 * want to create a replacing vdev.  The user is not allowed to
		 * attach to a spared vdev child unless the 'isspare' state is
		 * the same (spare replaces spare, non-spare replaces
		 * non-spare).
		 */
		if (pvd->vdev_ops == &vdev_replacing_ops &&
		    spa_version(spa) < SPA_VERSION_MULTI_REPLACE) {
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		} else if (pvd->vdev_ops == &vdev_spare_ops &&
		    newvd->vdev_isspare != oldvd->vdev_isspare) {
			return (spa_vdev_exit(spa, newrootvd, txg, ENOTSUP));
		}

		if (newvd->vdev_isspare)
			pvops = &vdev_spare_ops;
		else
			pvops = &vdev_replacing_ops;
	}

	/*
	 * Make sure the new device is big enough.
	 */
	if (newvd->vdev_asize < vdev_get_min_asize(oldvd))
		return (spa_vdev_exit(spa, newrootvd, txg, EOVERFLOW));

	/*
	 * The new device cannot have a higher alignment requirement
	 * than the top-level vdev.
	 */
	if (newvd->vdev_ashift > oldvd->vdev_top->vdev_ashift)
		return (spa_vdev_exit(spa, newrootvd, txg, EDOM));

	/*
	 * If this is an in-place replacement, update oldvd's path and devid
	 * to make it distinguishable from newvd, and unopenable from now on.
	 */
	if (strcmp(oldvd->vdev_path, newvd->vdev_path) == 0) {
		spa_strfree(oldvd->vdev_path);
		oldvd->vdev_path = kmem_alloc(strlen(newvd->vdev_path) + 5,
		    KM_SLEEP);
		(void) sprintf(oldvd->vdev_path, "%s/%s",
		    newvd->vdev_path, "old");
		if (oldvd->vdev_devid != NULL) {
			spa_strfree(oldvd->vdev_devid);
			oldvd->vdev_devid = NULL;
		}
	}

	/* mark the device being resilvered */
	newvd->vdev_resilver_txg = txg;

	/*
	 * If the parent is not a mirror, or if we're replacing, insert the new
	 * mirror/replacing/spare vdev above oldvd.
	 */
	if (pvd->vdev_ops != pvops)
		pvd = vdev_add_parent(oldvd, pvops);

	ASSERT(pvd->vdev_top->vdev_parent == rvd);
	ASSERT(pvd->vdev_ops == pvops);
	ASSERT(oldvd->vdev_parent == pvd);

	/*
	 * Extract the new device from its root and add it to pvd.
	 */
	vdev_remove_child(newrootvd, newvd);
	newvd->vdev_id = pvd->vdev_children;
	newvd->vdev_crtxg = oldvd->vdev_crtxg;
	vdev_add_child(pvd, newvd);

	tvd = newvd->vdev_top;
	ASSERT(pvd->vdev_top == tvd);
	ASSERT(tvd->vdev_parent == rvd);

	vdev_config_dirty(tvd);

	/*
	 * Set newvd's DTL to [TXG_INITIAL, dtl_max_txg) so that we account
	 * for any dmu_sync-ed blocks.  It will propagate upward when
	 * spa_vdev_exit() calls vdev_dtl_reassess().
	 */
	dtl_max_txg = txg + TXG_CONCURRENT_STATES;

	vdev_dtl_dirty(newvd, DTL_MISSING, TXG_INITIAL,
	    dtl_max_txg - TXG_INITIAL);

	if (newvd->vdev_isspare) {
		spa_spare_activate(newvd);
		spa_event_notify(spa, newvd, NULL, ESC_ZFS_VDEV_SPARE);
	}

	oldvdpath = spa_strdup(oldvd->vdev_path);
	newvdpath = spa_strdup(newvd->vdev_path);
	newvd_isspare = newvd->vdev_isspare;

	/*
	 * Mark newvd's DTL dirty in this txg.
	 */
	vdev_dirty(tvd, VDD_DTL, newvd, txg);

	/*
	 * Schedule the resilver to restart in the future. We do this to
	 * ensure that dmu_sync-ed blocks have been stitched into the
	 * respective datasets.
	 */
	dsl_resilver_restart(spa->spa_dsl_pool, dtl_max_txg);

	if (spa->spa_bootfs)
		spa_event_notify(spa, newvd, NULL, ESC_ZFS_BOOTFS_VDEV_ATTACH);

	spa_event_notify(spa, newvd, NULL, ESC_ZFS_VDEV_ATTACH);

	/*
	 * Commit the config
	 */
	(void) spa_vdev_exit(spa, newrootvd, dtl_max_txg, 0);

	spa_history_log_internal(spa, "vdev attach", NULL,
	    "%s vdev=%s %s vdev=%s",
	    replacing && newvd_isspare ? "spare in" :
	    replacing ? "replace" : "attach", newvdpath,
	    replacing ? "for" : "to", oldvdpath);

	spa_strfree(oldvdpath);
	spa_strfree(newvdpath);

	return (0);
}

/*
 * Detach a device from a mirror or replacing vdev.
 *
 * If 'replace_done' is specified, only detach if the parent
 * is a replacing vdev.
 */
int
spa_vdev_detach(spa_t *spa, uint64_t guid, uint64_t pguid, int replace_done)
{
	uint64_t txg;
	int error;
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd, *pvd, *cvd, *tvd;
	boolean_t unspare = B_FALSE;
	uint64_t unspare_guid = 0;
	char *vdpath;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	vd = spa_lookup_by_guid(spa, guid, B_FALSE);

	/*
	 * Besides being called directly from the userland through the
	 * ioctl interface, spa_vdev_detach() can be potentially called
	 * at the end of spa_vdev_resilver_done().
	 *
	 * In the regular case, when we have a checkpoint this shouldn't
	 * happen as we never empty the DTLs of a vdev during the scrub
	 * [see comment in dsl_scan_done()]. Thus spa_vdev_resilvering_done()
	 * should never get here when we have a checkpoint.
	 *
	 * That said, even in a case when we checkpoint the pool exactly
	 * as spa_vdev_resilver_done() calls this function everything
	 * should be fine as the resilver will return right away.
	 */
	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	if (vd == NULL)
		return (spa_vdev_exit(spa, NULL, txg, ENODEV));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	pvd = vd->vdev_parent;

	/*
	 * If the parent/child relationship is not as expected, don't do it.
	 * Consider M(A,R(B,C)) -- that is, a mirror of A with a replacing
	 * vdev that's replacing B with C.  The user's intent in replacing
	 * is to go from M(A,B) to M(A,C).  If the user decides to cancel
	 * the replace by detaching C, the expected behavior is to end up
	 * M(A,B).  But suppose that right after deciding to detach C,
	 * the replacement of B completes.  We would have M(A,C), and then
	 * ask to detach C, which would leave us with just A -- not what
	 * the user wanted.  To prevent this, we make sure that the
	 * parent/child relationship hasn't changed -- in this example,
	 * that C's parent is still the replacing vdev R.
	 */
	if (pvd->vdev_guid != pguid && pguid != 0)
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	/*
	 * Only 'replacing' or 'spare' vdevs can be replaced.
	 */
	if (replace_done && pvd->vdev_ops != &vdev_replacing_ops &&
	    pvd->vdev_ops != &vdev_spare_ops)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	ASSERT(pvd->vdev_ops != &vdev_spare_ops ||
	    spa_version(spa) >= SPA_VERSION_SPARES);

	/*
	 * Only mirror, replacing, and spare vdevs support detach.
	 */
	if (pvd->vdev_ops != &vdev_replacing_ops &&
	    pvd->vdev_ops != &vdev_mirror_ops &&
	    pvd->vdev_ops != &vdev_spare_ops)
		return (spa_vdev_exit(spa, NULL, txg, ENOTSUP));

	/*
	 * If this device has the only valid copy of some data,
	 * we cannot safely detach it.
	 */
	if (vdev_dtl_required(vd))
		return (spa_vdev_exit(spa, NULL, txg, EBUSY));

	ASSERT(pvd->vdev_children >= 2);

	/*
	 * If we are detaching the second disk from a replacing vdev, then
	 * check to see if we changed the original vdev's path to have "/old"
	 * at the end in spa_vdev_attach().  If so, undo that change now.
	 */
	if (pvd->vdev_ops == &vdev_replacing_ops && vd->vdev_id > 0 &&
	    vd->vdev_path != NULL) {
		size_t len = strlen(vd->vdev_path);

		for (int c = 0; c < pvd->vdev_children; c++) {
			cvd = pvd->vdev_child[c];

			if (cvd == vd || cvd->vdev_path == NULL)
				continue;

			if (strncmp(cvd->vdev_path, vd->vdev_path, len) == 0 &&
			    strcmp(cvd->vdev_path + len, "/old") == 0) {
				spa_strfree(cvd->vdev_path);
				cvd->vdev_path = spa_strdup(vd->vdev_path);
				break;
			}
		}
	}

	/*
	 * If we are detaching the original disk from a spare, then it implies
	 * that the spare should become a real disk, and be removed from the
	 * active spare list for the pool.
	 */
	if (pvd->vdev_ops == &vdev_spare_ops &&
	    vd->vdev_id == 0 &&
	    pvd->vdev_child[pvd->vdev_children - 1]->vdev_isspare)
		unspare = B_TRUE;

	/*
	 * Erase the disk labels so the disk can be used for other things.
	 * This must be done after all other error cases are handled,
	 * but before we disembowel vd (so we can still do I/O to it).
	 * But if we can't do it, don't treat the error as fatal --
	 * it may be that the unwritability of the disk is the reason
	 * it's being detached!
	 */
	error = vdev_label_init(vd, 0, VDEV_LABEL_REMOVE);

	/*
	 * Remove vd from its parent and compact the parent's children.
	 */
	vdev_remove_child(pvd, vd);
	vdev_compact_children(pvd);

	/*
	 * Remember one of the remaining children so we can get tvd below.
	 */
	cvd = pvd->vdev_child[pvd->vdev_children - 1];

	/*
	 * If we need to remove the remaining child from the list of hot spares,
	 * do it now, marking the vdev as no longer a spare in the process.
	 * We must do this before vdev_remove_parent(), because that can
	 * change the GUID if it creates a new toplevel GUID.  For a similar
	 * reason, we must remove the spare now, in the same txg as the detach;
	 * otherwise someone could attach a new sibling, change the GUID, and
	 * the subsequent attempt to spa_vdev_remove(unspare_guid) would fail.
	 */
	if (unspare) {
		ASSERT(cvd->vdev_isspare);
		spa_spare_remove(cvd);
		unspare_guid = cvd->vdev_guid;
		(void) spa_vdev_remove(spa, unspare_guid, B_TRUE);
		cvd->vdev_unspare = B_TRUE;
	}

	/*
	 * If the parent mirror/replacing vdev only has one child,
	 * the parent is no longer needed.  Remove it from the tree.
	 */
	if (pvd->vdev_children == 1) {
		if (pvd->vdev_ops == &vdev_spare_ops)
			cvd->vdev_unspare = B_FALSE;
		vdev_remove_parent(cvd);
	}


	/*
	 * We don't set tvd until now because the parent we just removed
	 * may have been the previous top-level vdev.
	 */
	tvd = cvd->vdev_top;
	ASSERT(tvd->vdev_parent == rvd);

	/*
	 * Reevaluate the parent vdev state.
	 */
	vdev_propagate_state(cvd);

	/*
	 * If the 'autoexpand' property is set on the pool then automatically
	 * try to expand the size of the pool. For example if the device we
	 * just detached was smaller than the others, it may be possible to
	 * add metaslabs (i.e. grow the pool). We need to reopen the vdev
	 * first so that we can obtain the updated sizes of the leaf vdevs.
	 */
	if (spa->spa_autoexpand) {
		vdev_reopen(tvd);
		vdev_expand(tvd, txg);
	}

	vdev_config_dirty(tvd);

	/*
	 * Mark vd's DTL as dirty in this txg.  vdev_dtl_sync() will see that
	 * vd->vdev_detached is set and free vd's DTL object in syncing context.
	 * But first make sure we're not on any *other* txg's DTL list, to
	 * prevent vd from being accessed after it's freed.
	 */
	vdpath = spa_strdup(vd->vdev_path);
	for (int t = 0; t < TXG_SIZE; t++)
		(void) txg_list_remove_this(&tvd->vdev_dtl_list, vd, t);
	vd->vdev_detached = B_TRUE;
	vdev_dirty(tvd, VDD_DTL, vd, txg);

	spa_event_notify(spa, vd, NULL, ESC_ZFS_VDEV_REMOVE);

	/* hang on to the spa before we release the lock */
	spa_open_ref(spa, FTAG);

	error = spa_vdev_exit(spa, vd, txg, 0);

	spa_history_log_internal(spa, "detach", NULL,
	    "vdev=%s", vdpath);
	spa_strfree(vdpath);

	/*
	 * If this was the removal of the original device in a hot spare vdev,
	 * then we want to go through and remove the device from the hot spare
	 * list of every other pool.
	 */
	if (unspare) {
		spa_t *altspa = NULL;

		mutex_enter(&spa_namespace_lock);
		while ((altspa = spa_next(altspa)) != NULL) {
			if (altspa->spa_state != POOL_STATE_ACTIVE ||
			    altspa == spa)
				continue;

			spa_open_ref(altspa, FTAG);
			mutex_exit(&spa_namespace_lock);
			(void) spa_vdev_remove(altspa, unspare_guid, B_TRUE);
			mutex_enter(&spa_namespace_lock);
			spa_close(altspa, FTAG);
		}
		mutex_exit(&spa_namespace_lock);

		/* search the rest of the vdevs for spares to remove */
		spa_vdev_resilver_done(spa);
	}

	/* all done with the spa; OK to release */
	mutex_enter(&spa_namespace_lock);
	spa_close(spa, FTAG);
	mutex_exit(&spa_namespace_lock);

	return (error);
}

int
spa_vdev_initialize(spa_t *spa, uint64_t guid, uint64_t cmd_type)
{
	/*
	 * We hold the namespace lock through the whole function
	 * to prevent any changes to the pool while we're starting or
	 * stopping initialization. The config and state locks are held so that
	 * we can properly assess the vdev state before we commit to
	 * the initializing operation.
	 */
	mutex_enter(&spa_namespace_lock);
	spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);

	/* Look up vdev and ensure it's a leaf. */
	vdev_t *vd = spa_lookup_by_guid(spa, guid, B_FALSE);
	if (vd == NULL || vd->vdev_detached) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ENODEV));
	} else if (!vd->vdev_ops->vdev_op_leaf || !vdev_is_concrete(vd)) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EINVAL));
	} else if (!vdev_writeable(vd)) {
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EROFS));
	}
	mutex_enter(&vd->vdev_initialize_lock);
	spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);

	/*
	 * When we activate an initialize action we check to see
	 * if the vdev_initialize_thread is NULL. We do this instead
	 * of using the vdev_initialize_state since there might be
	 * a previous initialization process which has completed but
	 * the thread is not exited.
	 */
	if (cmd_type == POOL_INITIALIZE_DO &&
	    (vd->vdev_initialize_thread != NULL ||
	    vd->vdev_top->vdev_removing)) {
		mutex_exit(&vd->vdev_initialize_lock);
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(EBUSY));
	} else if (cmd_type == POOL_INITIALIZE_CANCEL &&
	    (vd->vdev_initialize_state != VDEV_INITIALIZE_ACTIVE &&
	    vd->vdev_initialize_state != VDEV_INITIALIZE_SUSPENDED)) {
		mutex_exit(&vd->vdev_initialize_lock);
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ESRCH));
	} else if (cmd_type == POOL_INITIALIZE_SUSPEND &&
	    vd->vdev_initialize_state != VDEV_INITIALIZE_ACTIVE) {
		mutex_exit(&vd->vdev_initialize_lock);
		mutex_exit(&spa_namespace_lock);
		return (SET_ERROR(ESRCH));
	}

	switch (cmd_type) {
	case POOL_INITIALIZE_DO:
		vdev_initialize(vd);
		break;
	case POOL_INITIALIZE_CANCEL:
		vdev_initialize_stop(vd, VDEV_INITIALIZE_CANCELED);
		break;
	case POOL_INITIALIZE_SUSPEND:
		vdev_initialize_stop(vd, VDEV_INITIALIZE_SUSPENDED);
		break;
	default:
		panic("invalid cmd_type %llu", (unsigned long long)cmd_type);
	}
	mutex_exit(&vd->vdev_initialize_lock);

	/* Sync out the initializing state */
	txg_wait_synced(spa->spa_dsl_pool, 0);
	mutex_exit(&spa_namespace_lock);

	return (0);
}


/*
 * Split a set of devices from their mirrors, and create a new pool from them.
 */
int
spa_vdev_split_mirror(spa_t *spa, char *newname, nvlist_t *config,
    nvlist_t *props, boolean_t exp)
{
	int error = 0;
	uint64_t txg, *glist;
	spa_t *newspa;
	uint_t c, children, lastlog;
	nvlist_t **child, *nvl, *tmp;
	dmu_tx_t *tx;
	char *altroot = NULL;
	vdev_t *rvd, **vml = NULL;			/* vdev modify list */
	boolean_t activate_slog;

	ASSERT(spa_writeable(spa));

	txg = spa_vdev_enter(spa);

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT)) {
		error = (spa_has_checkpoint(spa)) ?
		    ZFS_ERR_CHECKPOINT_EXISTS : ZFS_ERR_DISCARDING_CHECKPOINT;
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	/* clear the log and flush everything up to now */
	activate_slog = spa_passivate_log(spa);
	(void) spa_vdev_config_exit(spa, NULL, txg, 0, FTAG);
	error = spa_reset_logs(spa);
	txg = spa_vdev_config_enter(spa);

	if (activate_slog)
		spa_activate_log(spa);

	if (error != 0)
		return (spa_vdev_exit(spa, NULL, txg, error));

	/* check new spa name before going any further */
	if (spa_lookup(newname) != NULL)
		return (spa_vdev_exit(spa, NULL, txg, EEXIST));

	/*
	 * scan through all the children to ensure they're all mirrors
	 */
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvl) != 0 ||
	    nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	/* first, check to ensure we've got the right child count */
	rvd = spa->spa_root_vdev;
	lastlog = 0;
	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];

		/* don't count the holes & logs as children */
		if (vd->vdev_islog || !vdev_is_concrete(vd)) {
			if (lastlog == 0)
				lastlog = c;
			continue;
		}

		lastlog = 0;
	}
	if (children != (lastlog != 0 ? lastlog : rvd->vdev_children))
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	/* next, ensure no spare or cache devices are part of the split */
	if (nvlist_lookup_nvlist(nvl, ZPOOL_CONFIG_SPARES, &tmp) == 0 ||
	    nvlist_lookup_nvlist(nvl, ZPOOL_CONFIG_L2CACHE, &tmp) == 0)
		return (spa_vdev_exit(spa, NULL, txg, EINVAL));

	vml = kmem_zalloc(children * sizeof (vdev_t *), KM_SLEEP);
	glist = kmem_zalloc(children * sizeof (uint64_t), KM_SLEEP);

	/* then, loop over each vdev and validate it */
	for (c = 0; c < children; c++) {
		uint64_t is_hole = 0;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &is_hole);

		if (is_hole != 0) {
			if (spa->spa_root_vdev->vdev_child[c]->vdev_ishole ||
			    spa->spa_root_vdev->vdev_child[c]->vdev_islog) {
				continue;
			} else {
				error = SET_ERROR(EINVAL);
				break;
			}
		}

		/* which disk is going to be split? */
		if (nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_GUID,
		    &glist[c]) != 0) {
			error = SET_ERROR(EINVAL);
			break;
		}

		/* look it up in the spa */
		vml[c] = spa_lookup_by_guid(spa, glist[c], B_FALSE);
		if (vml[c] == NULL) {
			error = SET_ERROR(ENODEV);
			break;
		}

		/* make sure there's nothing stopping the split */
		if (vml[c]->vdev_parent->vdev_ops != &vdev_mirror_ops ||
		    vml[c]->vdev_islog ||
		    !vdev_is_concrete(vml[c]) ||
		    vml[c]->vdev_isspare ||
		    vml[c]->vdev_isl2cache ||
		    !vdev_writeable(vml[c]) ||
		    vml[c]->vdev_children != 0 ||
		    vml[c]->vdev_state != VDEV_STATE_HEALTHY ||
		    c != spa->spa_root_vdev->vdev_child[c]->vdev_id) {
			error = SET_ERROR(EINVAL);
			break;
		}

		if (vdev_dtl_required(vml[c])) {
			error = SET_ERROR(EBUSY);
			break;
		}

		/* we need certain info from the top level */
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_METASLAB_ARRAY,
		    vml[c]->vdev_top->vdev_ms_array) == 0);
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_METASLAB_SHIFT,
		    vml[c]->vdev_top->vdev_ms_shift) == 0);
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_ASIZE,
		    vml[c]->vdev_top->vdev_asize) == 0);
		VERIFY(nvlist_add_uint64(child[c], ZPOOL_CONFIG_ASHIFT,
		    vml[c]->vdev_top->vdev_ashift) == 0);

		/* transfer per-vdev ZAPs */
		ASSERT3U(vml[c]->vdev_leaf_zap, !=, 0);
		VERIFY0(nvlist_add_uint64(child[c],
		    ZPOOL_CONFIG_VDEV_LEAF_ZAP, vml[c]->vdev_leaf_zap));

		ASSERT3U(vml[c]->vdev_top->vdev_top_zap, !=, 0);
		VERIFY0(nvlist_add_uint64(child[c],
		    ZPOOL_CONFIG_VDEV_TOP_ZAP,
		    vml[c]->vdev_parent->vdev_top_zap));
	}

	if (error != 0) {
		kmem_free(vml, children * sizeof (vdev_t *));
		kmem_free(glist, children * sizeof (uint64_t));
		return (spa_vdev_exit(spa, NULL, txg, error));
	}

	/* stop writers from using the disks */
	for (c = 0; c < children; c++) {
		if (vml[c] != NULL)
			vml[c]->vdev_offline = B_TRUE;
	}
	vdev_reopen(spa->spa_root_vdev);

	/*
	 * Temporarily record the splitting vdevs in the spa config.  This
	 * will disappear once the config is regenerated.
	 */
	VERIFY(nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64_array(nvl, ZPOOL_CONFIG_SPLIT_LIST,
	    glist, children) == 0);
	kmem_free(glist, children * sizeof (uint64_t));

	mutex_enter(&spa->spa_props_lock);
	VERIFY(nvlist_add_nvlist(spa->spa_config, ZPOOL_CONFIG_SPLIT,
	    nvl) == 0);
	mutex_exit(&spa->spa_props_lock);
	spa->spa_config_splitting = nvl;
	vdev_config_dirty(spa->spa_root_vdev);

	/* configure and create the new pool */
	VERIFY(nvlist_add_string(config, ZPOOL_CONFIG_POOL_NAME, newname) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    exp ? POOL_STATE_EXPORTED : POOL_STATE_ACTIVE) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_VERSION,
	    spa_version(spa)) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_TXG,
	    spa->spa_config_txg) == 0);
	VERIFY(nvlist_add_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    spa_generate_guid(NULL)) == 0);
	VERIFY0(nvlist_add_boolean(config, ZPOOL_CONFIG_HAS_PER_VDEV_ZAPS));
	(void) nvlist_lookup_string(props,
	    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &altroot);

	/* add the new pool to the namespace */
	newspa = spa_add(newname, config, altroot);
	newspa->spa_avz_action = AVZ_ACTION_REBUILD;
	newspa->spa_config_txg = spa->spa_config_txg;
	spa_set_log_state(newspa, SPA_LOG_CLEAR);

	/* release the spa config lock, retaining the namespace lock */
	spa_vdev_config_exit(spa, NULL, txg, 0, FTAG);

	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, FTAG, 1);

	spa_activate(newspa, spa_mode_global);
	spa_async_suspend(newspa);

	for (c = 0; c < children; c++) {
		if (vml[c] != NULL) {
			/*
			 * Temporarily stop the initializing activity. We set
			 * the state to ACTIVE so that we know to resume
			 * the initializing once the split has completed.
			 */
			mutex_enter(&vml[c]->vdev_initialize_lock);
			vdev_initialize_stop(vml[c], VDEV_INITIALIZE_ACTIVE);
			mutex_exit(&vml[c]->vdev_initialize_lock);
		}
	}

#ifndef illumos
	/* mark that we are creating new spa by splitting */
	newspa->spa_splitting_newspa = B_TRUE;
#endif
	newspa->spa_config_source = SPA_CONFIG_SRC_SPLIT;

	/* create the new pool from the disks of the original pool */
	error = spa_load(newspa, SPA_LOAD_IMPORT, SPA_IMPORT_ASSEMBLE);
#ifndef illumos
	newspa->spa_splitting_newspa = B_FALSE;
#endif
	if (error)
		goto out;

	/* if that worked, generate a real config for the new pool */
	if (newspa->spa_root_vdev != NULL) {
		VERIFY(nvlist_alloc(&newspa->spa_config_splitting,
		    NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_uint64(newspa->spa_config_splitting,
		    ZPOOL_CONFIG_SPLIT_GUID, spa_guid(spa)) == 0);
		spa_config_set(newspa, spa_config_generate(newspa, NULL, -1ULL,
		    B_TRUE));
	}

	/* set the props */
	if (props != NULL) {
		spa_configfile_set(newspa, props, B_FALSE);
		error = spa_prop_set(newspa, props);
		if (error)
			goto out;
	}

	/* flush everything */
	txg = spa_vdev_config_enter(newspa);
	vdev_config_dirty(newspa->spa_root_vdev);
	(void) spa_vdev_config_exit(newspa, NULL, txg, 0, FTAG);

	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, FTAG, 2);

	spa_async_resume(newspa);

	/* finally, update the original pool's config */
	txg = spa_vdev_config_enter(spa);
	tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error != 0)
		dmu_tx_abort(tx);
	for (c = 0; c < children; c++) {
		if (vml[c] != NULL) {
			vdev_split(vml[c]);
			if (error == 0)
				spa_history_log_internal(spa, "detach", tx,
				    "vdev=%s", vml[c]->vdev_path);

			vdev_free(vml[c]);
		}
	}
	spa->spa_avz_action = AVZ_ACTION_REBUILD;
	vdev_config_dirty(spa->spa_root_vdev);
	spa->spa_config_splitting = NULL;
	nvlist_free(nvl);
	if (error == 0)
		dmu_tx_commit(tx);
	(void) spa_vdev_exit(spa, NULL, txg, 0);

	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, FTAG, 3);

	/* split is complete; log a history record */
	spa_history_log_internal(newspa, "split", NULL,
	    "from pool %s", spa_name(spa));

	kmem_free(vml, children * sizeof (vdev_t *));

	/* if we're not going to mount the filesystems in userland, export */
	if (exp)
		error = spa_export_common(newname, POOL_STATE_EXPORTED, NULL,
		    B_FALSE, B_FALSE);

	return (error);

out:
	spa_unload(newspa);
	spa_deactivate(newspa);
	spa_remove(newspa);

	txg = spa_vdev_config_enter(spa);

	/* re-online all offlined disks */
	for (c = 0; c < children; c++) {
		if (vml[c] != NULL)
			vml[c]->vdev_offline = B_FALSE;
	}

	/* restart initializing disks as necessary */
	spa_async_request(spa, SPA_ASYNC_INITIALIZE_RESTART);

	vdev_reopen(spa->spa_root_vdev);

	nvlist_free(spa->spa_config_splitting);
	spa->spa_config_splitting = NULL;
	(void) spa_vdev_exit(spa, NULL, txg, error);

	kmem_free(vml, children * sizeof (vdev_t *));
	return (error);
}

/*
 * Find any device that's done replacing, or a vdev marked 'unspare' that's
 * currently spared, so we can detach it.
 */
static vdev_t *
spa_vdev_resilver_done_hunt(vdev_t *vd)
{
	vdev_t *newvd, *oldvd;

	for (int c = 0; c < vd->vdev_children; c++) {
		oldvd = spa_vdev_resilver_done_hunt(vd->vdev_child[c]);
		if (oldvd != NULL)
			return (oldvd);
	}

	/*
	 * Check for a completed replacement.  We always consider the first
	 * vdev in the list to be the oldest vdev, and the last one to be
	 * the newest (see spa_vdev_attach() for how that works).  In
	 * the case where the newest vdev is faulted, we will not automatically
	 * remove it after a resilver completes.  This is OK as it will require
	 * user intervention to determine which disk the admin wishes to keep.
	 */
	if (vd->vdev_ops == &vdev_replacing_ops) {
		ASSERT(vd->vdev_children > 1);

		newvd = vd->vdev_child[vd->vdev_children - 1];
		oldvd = vd->vdev_child[0];

		if (vdev_dtl_empty(newvd, DTL_MISSING) &&
		    vdev_dtl_empty(newvd, DTL_OUTAGE) &&
		    !vdev_dtl_required(oldvd))
			return (oldvd);
	}

	/*
	 * Check for a completed resilver with the 'unspare' flag set.
	 * Also potentially update faulted state.
	 */
	if (vd->vdev_ops == &vdev_spare_ops) {
		vdev_t *first = vd->vdev_child[0];
		vdev_t *last = vd->vdev_child[vd->vdev_children - 1];

		if (last->vdev_unspare) {
			oldvd = first;
			newvd = last;
		} else if (first->vdev_unspare) {
			oldvd = last;
			newvd = first;
		} else {
			oldvd = NULL;
		}

		if (oldvd != NULL &&
		    vdev_dtl_empty(newvd, DTL_MISSING) &&
		    vdev_dtl_empty(newvd, DTL_OUTAGE) &&
		    !vdev_dtl_required(oldvd))
			return (oldvd);

		vdev_propagate_state(vd);

		/*
		 * If there are more than two spares attached to a disk,
		 * and those spares are not required, then we want to
		 * attempt to free them up now so that they can be used
		 * by other pools.  Once we're back down to a single
		 * disk+spare, we stop removing them.
		 */
		if (vd->vdev_children > 2) {
			newvd = vd->vdev_child[1];

			if (newvd->vdev_isspare && last->vdev_isspare &&
			    vdev_dtl_empty(last, DTL_MISSING) &&
			    vdev_dtl_empty(last, DTL_OUTAGE) &&
			    !vdev_dtl_required(newvd))
				return (newvd);
		}
	}

	return (NULL);
}

static void
spa_vdev_resilver_done(spa_t *spa)
{
	vdev_t *vd, *pvd, *ppvd;
	uint64_t guid, sguid, pguid, ppguid;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	while ((vd = spa_vdev_resilver_done_hunt(spa->spa_root_vdev)) != NULL) {
		pvd = vd->vdev_parent;
		ppvd = pvd->vdev_parent;
		guid = vd->vdev_guid;
		pguid = pvd->vdev_guid;
		ppguid = ppvd->vdev_guid;
		sguid = 0;
		/*
		 * If we have just finished replacing a hot spared device, then
		 * we need to detach the parent's first child (the original hot
		 * spare) as well.
		 */
		if (ppvd->vdev_ops == &vdev_spare_ops && pvd->vdev_id == 0 &&
		    ppvd->vdev_children == 2) {
			ASSERT(pvd->vdev_ops == &vdev_replacing_ops);
			sguid = ppvd->vdev_child[1]->vdev_guid;
		}
		ASSERT(vd->vdev_resilver_txg == 0 || !vdev_dtl_required(vd));

		spa_config_exit(spa, SCL_ALL, FTAG);
		if (spa_vdev_detach(spa, guid, pguid, B_TRUE) != 0)
			return;
		if (sguid && spa_vdev_detach(spa, sguid, ppguid, B_TRUE) != 0)
			return;
		spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	}

	spa_config_exit(spa, SCL_ALL, FTAG);
}

/*
 * Update the stored path or FRU for this vdev.
 */
int
spa_vdev_set_common(spa_t *spa, uint64_t guid, const char *value,
    boolean_t ispath)
{
	vdev_t *vd;
	boolean_t sync = B_FALSE;

	ASSERT(spa_writeable(spa));

	spa_vdev_state_enter(spa, SCL_ALL);

	if ((vd = spa_lookup_by_guid(spa, guid, B_TRUE)) == NULL)
		return (spa_vdev_state_exit(spa, NULL, ENOENT));

	if (!vd->vdev_ops->vdev_op_leaf)
		return (spa_vdev_state_exit(spa, NULL, ENOTSUP));

	if (ispath) {
		if (strcmp(value, vd->vdev_path) != 0) {
			spa_strfree(vd->vdev_path);
			vd->vdev_path = spa_strdup(value);
			sync = B_TRUE;
		}
	} else {
		if (vd->vdev_fru == NULL) {
			vd->vdev_fru = spa_strdup(value);
			sync = B_TRUE;
		} else if (strcmp(value, vd->vdev_fru) != 0) {
			spa_strfree(vd->vdev_fru);
			vd->vdev_fru = spa_strdup(value);
			sync = B_TRUE;
		}
	}

	return (spa_vdev_state_exit(spa, sync ? vd : NULL, 0));
}

int
spa_vdev_setpath(spa_t *spa, uint64_t guid, const char *newpath)
{
	return (spa_vdev_set_common(spa, guid, newpath, B_TRUE));
}

int
spa_vdev_setfru(spa_t *spa, uint64_t guid, const char *newfru)
{
	return (spa_vdev_set_common(spa, guid, newfru, B_FALSE));
}

/*
 * ==========================================================================
 * SPA Scanning
 * ==========================================================================
 */
int
spa_scrub_pause_resume(spa_t *spa, pool_scrub_cmd_t cmd)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);

	if (dsl_scan_resilvering(spa->spa_dsl_pool))
		return (SET_ERROR(EBUSY));

	return (dsl_scrub_set_pause_resume(spa->spa_dsl_pool, cmd));
}

int
spa_scan_stop(spa_t *spa)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);
	if (dsl_scan_resilvering(spa->spa_dsl_pool))
		return (SET_ERROR(EBUSY));
	return (dsl_scan_cancel(spa->spa_dsl_pool));
}

int
spa_scan(spa_t *spa, pool_scan_func_t func)
{
	ASSERT(spa_config_held(spa, SCL_ALL, RW_WRITER) == 0);

	if (func >= POOL_SCAN_FUNCS || func == POOL_SCAN_NONE)
		return (SET_ERROR(ENOTSUP));

	/*
	 * If a resilver was requested, but there is no DTL on a
	 * writeable leaf device, we have nothing to do.
	 */
	if (func == POOL_SCAN_RESILVER &&
	    !vdev_resilver_needed(spa->spa_root_vdev, NULL, NULL)) {
		spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);
		return (0);
	}

	return (dsl_scan(spa->spa_dsl_pool, func));
}

/*
 * ==========================================================================
 * SPA async task processing
 * ==========================================================================
 */

static void
spa_async_remove(spa_t *spa, vdev_t *vd)
{
	if (vd->vdev_remove_wanted) {
		vd->vdev_remove_wanted = B_FALSE;
		vd->vdev_delayed_close = B_FALSE;
		vdev_set_state(vd, B_FALSE, VDEV_STATE_REMOVED, VDEV_AUX_NONE);

		/*
		 * We want to clear the stats, but we don't want to do a full
		 * vdev_clear() as that will cause us to throw away
		 * degraded/faulted state as well as attempt to reopen the
		 * device, all of which is a waste.
		 */
		vd->vdev_stat.vs_read_errors = 0;
		vd->vdev_stat.vs_write_errors = 0;
		vd->vdev_stat.vs_checksum_errors = 0;

		vdev_state_dirty(vd->vdev_top);
		/* Tell userspace that the vdev is gone. */
		zfs_post_remove(spa, vd);
	}

	for (int c = 0; c < vd->vdev_children; c++)
		spa_async_remove(spa, vd->vdev_child[c]);
}

static void
spa_async_probe(spa_t *spa, vdev_t *vd)
{
	if (vd->vdev_probe_wanted) {
		vd->vdev_probe_wanted = B_FALSE;
		vdev_reopen(vd);	/* vdev_open() does the actual probe */
	}

	for (int c = 0; c < vd->vdev_children; c++)
		spa_async_probe(spa, vd->vdev_child[c]);
}

static void
spa_async_autoexpand(spa_t *spa, vdev_t *vd)
{
	sysevent_id_t eid;
	nvlist_t *attr;
	char *physpath;

	if (!spa->spa_autoexpand)
		return;

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		spa_async_autoexpand(spa, cvd);
	}

	if (!vd->vdev_ops->vdev_op_leaf || vd->vdev_physpath == NULL)
		return;

	physpath = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
	(void) snprintf(physpath, MAXPATHLEN, "/devices%s", vd->vdev_physpath);

	VERIFY(nvlist_alloc(&attr, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(attr, DEV_PHYS_PATH, physpath) == 0);

	(void) ddi_log_sysevent(zfs_dip, SUNW_VENDOR, EC_DEV_STATUS,
	    ESC_ZFS_VDEV_AUTOEXPAND, attr, &eid, DDI_SLEEP);

	nvlist_free(attr);
	kmem_free(physpath, MAXPATHLEN);
}

static void
spa_async_thread(void *arg)
{
	spa_t *spa = (spa_t *)arg;
	int tasks;

	ASSERT(spa->spa_sync_on);

	mutex_enter(&spa->spa_async_lock);
	tasks = spa->spa_async_tasks;
	spa->spa_async_tasks &= SPA_ASYNC_REMOVE;
	mutex_exit(&spa->spa_async_lock);

	/*
	 * See if the config needs to be updated.
	 */
	if (tasks & SPA_ASYNC_CONFIG_UPDATE) {
		uint64_t old_space, new_space;

		mutex_enter(&spa_namespace_lock);
		old_space = metaslab_class_get_space(spa_normal_class(spa));
		spa_config_update(spa, SPA_CONFIG_UPDATE_POOL);
		new_space = metaslab_class_get_space(spa_normal_class(spa));
		mutex_exit(&spa_namespace_lock);

		/*
		 * If the pool grew as a result of the config update,
		 * then log an internal history event.
		 */
		if (new_space != old_space) {
			spa_history_log_internal(spa, "vdev online", NULL,
			    "pool '%s' size: %llu(+%llu)",
			    spa_name(spa), new_space, new_space - old_space);
		}
	}

	if ((tasks & SPA_ASYNC_AUTOEXPAND) && !spa_suspended(spa)) {
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		spa_async_autoexpand(spa, spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
	}

	/*
	 * See if any devices need to be probed.
	 */
	if (tasks & SPA_ASYNC_PROBE) {
		spa_vdev_state_enter(spa, SCL_NONE);
		spa_async_probe(spa, spa->spa_root_vdev);
		(void) spa_vdev_state_exit(spa, NULL, 0);
	}

	/*
	 * If any devices are done replacing, detach them.
	 */
	if (tasks & SPA_ASYNC_RESILVER_DONE)
		spa_vdev_resilver_done(spa);

	/*
	 * Kick off a resilver.
	 */
	if (tasks & SPA_ASYNC_RESILVER)
		dsl_resilver_restart(spa->spa_dsl_pool, 0);

	if (tasks & SPA_ASYNC_INITIALIZE_RESTART) {
		mutex_enter(&spa_namespace_lock);
		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		vdev_initialize_restart(spa->spa_root_vdev);
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		mutex_exit(&spa_namespace_lock);
	}

	/*
	 * Let the world know that we're done.
	 */
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_thread = NULL;
	cv_broadcast(&spa->spa_async_cv);
	mutex_exit(&spa->spa_async_lock);
	thread_exit();
}

static void
spa_async_thread_vd(void *arg)
{
	spa_t *spa = arg;
	int tasks;

	mutex_enter(&spa->spa_async_lock);
	tasks = spa->spa_async_tasks;
retry:
	spa->spa_async_tasks &= ~SPA_ASYNC_REMOVE;
	mutex_exit(&spa->spa_async_lock);

	/*
	 * See if any devices need to be marked REMOVED.
	 */
	if (tasks & SPA_ASYNC_REMOVE) {
		spa_vdev_state_enter(spa, SCL_NONE);
		spa_async_remove(spa, spa->spa_root_vdev);
		for (int i = 0; i < spa->spa_l2cache.sav_count; i++)
			spa_async_remove(spa, spa->spa_l2cache.sav_vdevs[i]);
		for (int i = 0; i < spa->spa_spares.sav_count; i++)
			spa_async_remove(spa, spa->spa_spares.sav_vdevs[i]);
		(void) spa_vdev_state_exit(spa, NULL, 0);
	}

	/*
	 * Let the world know that we're done.
	 */
	mutex_enter(&spa->spa_async_lock);
	tasks = spa->spa_async_tasks;
	if ((tasks & SPA_ASYNC_REMOVE) != 0)
		goto retry;
	spa->spa_async_thread_vd = NULL;
	cv_broadcast(&spa->spa_async_cv);
	mutex_exit(&spa->spa_async_lock);
	thread_exit();
}

void
spa_async_suspend(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_suspended++;
	while (spa->spa_async_thread != NULL ||
	    spa->spa_async_thread_vd != NULL)
		cv_wait(&spa->spa_async_cv, &spa->spa_async_lock);
	mutex_exit(&spa->spa_async_lock);

	spa_vdev_remove_suspend(spa);

	zthr_t *condense_thread = spa->spa_condense_zthr;
	if (condense_thread != NULL && zthr_isrunning(condense_thread))
		VERIFY0(zthr_cancel(condense_thread));

	zthr_t *discard_thread = spa->spa_checkpoint_discard_zthr;
	if (discard_thread != NULL && zthr_isrunning(discard_thread))
		VERIFY0(zthr_cancel(discard_thread));
}

void
spa_async_resume(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	ASSERT(spa->spa_async_suspended != 0);
	spa->spa_async_suspended--;
	mutex_exit(&spa->spa_async_lock);
	spa_restart_removal(spa);

	zthr_t *condense_thread = spa->spa_condense_zthr;
	if (condense_thread != NULL && !zthr_isrunning(condense_thread))
		zthr_resume(condense_thread);

	zthr_t *discard_thread = spa->spa_checkpoint_discard_zthr;
	if (discard_thread != NULL && !zthr_isrunning(discard_thread))
		zthr_resume(discard_thread);
}

static boolean_t
spa_async_tasks_pending(spa_t *spa)
{
	uint_t non_config_tasks;
	uint_t config_task;
	boolean_t config_task_suspended;

	non_config_tasks = spa->spa_async_tasks & ~(SPA_ASYNC_CONFIG_UPDATE |
	    SPA_ASYNC_REMOVE);
	config_task = spa->spa_async_tasks & SPA_ASYNC_CONFIG_UPDATE;
	if (spa->spa_ccw_fail_time == 0) {
		config_task_suspended = B_FALSE;
	} else {
		config_task_suspended =
		    (gethrtime() - spa->spa_ccw_fail_time) <
		    (zfs_ccw_retry_interval * NANOSEC);
	}

	return (non_config_tasks || (config_task && !config_task_suspended));
}

static void
spa_async_dispatch(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	if (spa_async_tasks_pending(spa) &&
	    !spa->spa_async_suspended &&
	    spa->spa_async_thread == NULL &&
	    rootdir != NULL)
		spa->spa_async_thread = thread_create(NULL, 0,
		    spa_async_thread, spa, 0, &p0, TS_RUN, maxclsyspri);
	mutex_exit(&spa->spa_async_lock);
}

static void
spa_async_dispatch_vd(spa_t *spa)
{
	mutex_enter(&spa->spa_async_lock);
	if ((spa->spa_async_tasks & SPA_ASYNC_REMOVE) != 0 &&
	    !spa->spa_async_suspended &&
	    spa->spa_async_thread_vd == NULL &&
	    rootdir != NULL)
		spa->spa_async_thread_vd = thread_create(NULL, 0,
		    spa_async_thread_vd, spa, 0, &p0, TS_RUN, maxclsyspri);
	mutex_exit(&spa->spa_async_lock);
}

void
spa_async_request(spa_t *spa, int task)
{
	zfs_dbgmsg("spa=%s async request task=%u", spa->spa_name, task);
	mutex_enter(&spa->spa_async_lock);
	spa->spa_async_tasks |= task;
	mutex_exit(&spa->spa_async_lock);
	spa_async_dispatch_vd(spa);
}

/*
 * ==========================================================================
 * SPA syncing routines
 * ==========================================================================
 */

static int
bpobj_enqueue_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	bpobj_t *bpo = arg;
	bpobj_enqueue(bpo, bp, tx);
	return (0);
}

static int
spa_free_sync_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	zio_t *zio = arg;

	zio_nowait(zio_free_sync(zio, zio->io_spa, dmu_tx_get_txg(tx), bp,
	    BP_GET_PSIZE(bp), zio->io_flags));
	return (0);
}

/*
 * Note: this simple function is not inlined to make it easier to dtrace the
 * amount of time spent syncing frees.
 */
static void
spa_sync_frees(spa_t *spa, bplist_t *bpl, dmu_tx_t *tx)
{
	zio_t *zio = zio_root(spa, NULL, NULL, 0);
	bplist_iterate(bpl, spa_free_sync_cb, zio, tx);
	VERIFY(zio_wait(zio) == 0);
}

/*
 * Note: this simple function is not inlined to make it easier to dtrace the
 * amount of time spent syncing deferred frees.
 */
static void
spa_sync_deferred_frees(spa_t *spa, dmu_tx_t *tx)
{
	zio_t *zio = zio_root(spa, NULL, NULL, 0);
	VERIFY3U(bpobj_iterate(&spa->spa_deferred_bpobj,
	    spa_free_sync_cb, zio, tx), ==, 0);
	VERIFY0(zio_wait(zio));
}


static void
spa_sync_nvlist(spa_t *spa, uint64_t obj, nvlist_t *nv, dmu_tx_t *tx)
{
	char *packed = NULL;
	size_t bufsize;
	size_t nvsize = 0;
	dmu_buf_t *db;

	VERIFY(nvlist_size(nv, &nvsize, NV_ENCODE_XDR) == 0);

	/*
	 * Write full (SPA_CONFIG_BLOCKSIZE) blocks of configuration
	 * information.  This avoids the dmu_buf_will_dirty() path and
	 * saves us a pre-read to get data we don't actually care about.
	 */
	bufsize = P2ROUNDUP((uint64_t)nvsize, SPA_CONFIG_BLOCKSIZE);
	packed = kmem_alloc(bufsize, KM_SLEEP);

	VERIFY(nvlist_pack(nv, &packed, &nvsize, NV_ENCODE_XDR,
	    KM_SLEEP) == 0);
	bzero(packed + nvsize, bufsize - nvsize);

	dmu_write(spa->spa_meta_objset, obj, 0, bufsize, packed, tx);

	kmem_free(packed, bufsize);

	VERIFY(0 == dmu_bonus_hold(spa->spa_meta_objset, obj, FTAG, &db));
	dmu_buf_will_dirty(db, tx);
	*(uint64_t *)db->db_data = nvsize;
	dmu_buf_rele(db, FTAG);
}

static void
spa_sync_aux_dev(spa_t *spa, spa_aux_vdev_t *sav, dmu_tx_t *tx,
    const char *config, const char *entry)
{
	nvlist_t *nvroot;
	nvlist_t **list;
	int i;

	if (!sav->sav_sync)
		return;

	/*
	 * Update the MOS nvlist describing the list of available devices.
	 * spa_validate_aux() will have already made sure this nvlist is
	 * valid and the vdevs are labeled appropriately.
	 */
	if (sav->sav_object == 0) {
		sav->sav_object = dmu_object_alloc(spa->spa_meta_objset,
		    DMU_OT_PACKED_NVLIST, 1 << 14, DMU_OT_PACKED_NVLIST_SIZE,
		    sizeof (uint64_t), tx);
		VERIFY(zap_update(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, entry, sizeof (uint64_t), 1,
		    &sav->sav_object, tx) == 0);
	}

	VERIFY(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	if (sav->sav_count == 0) {
		VERIFY(nvlist_add_nvlist_array(nvroot, config, NULL, 0) == 0);
	} else {
		list = kmem_alloc(sav->sav_count * sizeof (void *), KM_SLEEP);
		for (i = 0; i < sav->sav_count; i++)
			list[i] = vdev_config_generate(spa, sav->sav_vdevs[i],
			    B_FALSE, VDEV_CONFIG_L2CACHE);
		VERIFY(nvlist_add_nvlist_array(nvroot, config, list,
		    sav->sav_count) == 0);
		for (i = 0; i < sav->sav_count; i++)
			nvlist_free(list[i]);
		kmem_free(list, sav->sav_count * sizeof (void *));
	}

	spa_sync_nvlist(spa, sav->sav_object, nvroot, tx);
	nvlist_free(nvroot);

	sav->sav_sync = B_FALSE;
}

/*
 * Rebuild spa's all-vdev ZAP from the vdev ZAPs indicated in each vdev_t.
 * The all-vdev ZAP must be empty.
 */
static void
spa_avz_build(vdev_t *vd, uint64_t avz, dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	if (vd->vdev_top_zap != 0) {
		VERIFY0(zap_add_int(spa->spa_meta_objset, avz,
		    vd->vdev_top_zap, tx));
	}
	if (vd->vdev_leaf_zap != 0) {
		VERIFY0(zap_add_int(spa->spa_meta_objset, avz,
		    vd->vdev_leaf_zap, tx));
	}
	for (uint64_t i = 0; i < vd->vdev_children; i++) {
		spa_avz_build(vd->vdev_child[i], avz, tx);
	}
}

static void
spa_sync_config_object(spa_t *spa, dmu_tx_t *tx)
{
	nvlist_t *config;

	/*
	 * If the pool is being imported from a pre-per-vdev-ZAP version of ZFS,
	 * its config may not be dirty but we still need to build per-vdev ZAPs.
	 * Similarly, if the pool is being assembled (e.g. after a split), we
	 * need to rebuild the AVZ although the config may not be dirty.
	 */
	if (list_is_empty(&spa->spa_config_dirty_list) &&
	    spa->spa_avz_action == AVZ_ACTION_NONE)
		return;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	ASSERT(spa->spa_avz_action == AVZ_ACTION_NONE ||
	    spa->spa_avz_action == AVZ_ACTION_INITIALIZE ||
	    spa->spa_all_vdev_zaps != 0);

	if (spa->spa_avz_action == AVZ_ACTION_REBUILD) {
		/* Make and build the new AVZ */
		uint64_t new_avz = zap_create(spa->spa_meta_objset,
		    DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);
		spa_avz_build(spa->spa_root_vdev, new_avz, tx);

		/* Diff old AVZ with new one */
		zap_cursor_t zc;
		zap_attribute_t za;

		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			uint64_t vdzap = za.za_first_integer;
			if (zap_lookup_int(spa->spa_meta_objset, new_avz,
			    vdzap) == ENOENT) {
				/*
				 * ZAP is listed in old AVZ but not in new one;
				 * destroy it
				 */
				VERIFY0(zap_destroy(spa->spa_meta_objset, vdzap,
				    tx));
			}
		}

		zap_cursor_fini(&zc);

		/* Destroy the old AVZ */
		VERIFY0(zap_destroy(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, tx));

		/* Replace the old AVZ in the dir obj with the new one */
		VERIFY0(zap_update(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_VDEV_ZAP_MAP,
		    sizeof (new_avz), 1, &new_avz, tx));

		spa->spa_all_vdev_zaps = new_avz;
	} else if (spa->spa_avz_action == AVZ_ACTION_DESTROY) {
		zap_cursor_t zc;
		zap_attribute_t za;

		/* Walk through the AVZ and destroy all listed ZAPs */
		for (zap_cursor_init(&zc, spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps);
		    zap_cursor_retrieve(&zc, &za) == 0;
		    zap_cursor_advance(&zc)) {
			uint64_t zap = za.za_first_integer;
			VERIFY0(zap_destroy(spa->spa_meta_objset, zap, tx));
		}

		zap_cursor_fini(&zc);

		/* Destroy and unlink the AVZ itself */
		VERIFY0(zap_destroy(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, tx));
		VERIFY0(zap_remove(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_VDEV_ZAP_MAP, tx));
		spa->spa_all_vdev_zaps = 0;
	}

	if (spa->spa_all_vdev_zaps == 0) {
		spa->spa_all_vdev_zaps = zap_create_link(spa->spa_meta_objset,
		    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_VDEV_ZAP_MAP, tx);
	}
	spa->spa_avz_action = AVZ_ACTION_NONE;

	/* Create ZAPs for vdevs that don't have them. */
	vdev_construct_zaps(spa->spa_root_vdev, tx);

	config = spa_config_generate(spa, spa->spa_root_vdev,
	    dmu_tx_get_txg(tx), B_FALSE);

	/*
	 * If we're upgrading the spa version then make sure that
	 * the config object gets updated with the correct version.
	 */
	if (spa->spa_ubsync.ub_version < spa->spa_uberblock.ub_version)
		fnvlist_add_uint64(config, ZPOOL_CONFIG_VERSION,
		    spa->spa_uberblock.ub_version);

	spa_config_exit(spa, SCL_STATE, FTAG);

	nvlist_free(spa->spa_config_syncing);
	spa->spa_config_syncing = config;

	spa_sync_nvlist(spa, spa->spa_config_object, config, tx);
}

static void
spa_sync_version(void *arg, dmu_tx_t *tx)
{
	uint64_t *versionp = arg;
	uint64_t version = *versionp;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	/*
	 * Setting the version is special cased when first creating the pool.
	 */
	ASSERT(tx->tx_txg != TXG_INITIAL);

	ASSERT(SPA_VERSION_IS_SUPPORTED(version));
	ASSERT(version >= spa_version(spa));

	spa->spa_uberblock.ub_version = version;
	vdev_config_dirty(spa->spa_root_vdev);
	spa_history_log_internal(spa, "set", tx, "version=%lld", version);
}

/*
 * Set zpool properties.
 */
static void
spa_sync_props(void *arg, dmu_tx_t *tx)
{
	nvlist_t *nvp = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	objset_t *mos = spa->spa_meta_objset;
	nvpair_t *elem = NULL;

	mutex_enter(&spa->spa_props_lock);

	while ((elem = nvlist_next_nvpair(nvp, elem))) {
		uint64_t intval;
		char *strval, *fname;
		zpool_prop_t prop;
		const char *propname;
		zprop_type_t proptype;
		spa_feature_t fid;

		switch (prop = zpool_name_to_prop(nvpair_name(elem))) {
		case ZPOOL_PROP_INVAL:
			/*
			 * We checked this earlier in spa_prop_validate().
			 */
			ASSERT(zpool_prop_feature(nvpair_name(elem)));

			fname = strchr(nvpair_name(elem), '@') + 1;
			VERIFY0(zfeature_lookup_name(fname, &fid));

			spa_feature_enable(spa, fid, tx);
			spa_history_log_internal(spa, "set", tx,
			    "%s=enabled", nvpair_name(elem));
			break;

		case ZPOOL_PROP_VERSION:
			intval = fnvpair_value_uint64(elem);
			/*
			 * The version is synced seperatly before other
			 * properties and should be correct by now.
			 */
			ASSERT3U(spa_version(spa), >=, intval);
			break;

		case ZPOOL_PROP_ALTROOT:
			/*
			 * 'altroot' is a non-persistent property. It should
			 * have been set temporarily at creation or import time.
			 */
			ASSERT(spa->spa_root != NULL);
			break;

		case ZPOOL_PROP_READONLY:
		case ZPOOL_PROP_CACHEFILE:
			/*
			 * 'readonly' and 'cachefile' are also non-persisitent
			 * properties.
			 */
			break;
		case ZPOOL_PROP_COMMENT:
			strval = fnvpair_value_string(elem);
			if (spa->spa_comment != NULL)
				spa_strfree(spa->spa_comment);
			spa->spa_comment = spa_strdup(strval);
			/*
			 * We need to dirty the configuration on all the vdevs
			 * so that their labels get updated.  It's unnecessary
			 * to do this for pool creation since the vdev's
			 * configuratoin has already been dirtied.
			 */
			if (tx->tx_txg != TXG_INITIAL)
				vdev_config_dirty(spa->spa_root_vdev);
			spa_history_log_internal(spa, "set", tx,
			    "%s=%s", nvpair_name(elem), strval);
			break;
		default:
			/*
			 * Set pool property values in the poolprops mos object.
			 */
			if (spa->spa_pool_props_object == 0) {
				spa->spa_pool_props_object =
				    zap_create_link(mos, DMU_OT_POOL_PROPS,
				    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_PROPS,
				    tx);
			}

			/* normalize the property name */
			propname = zpool_prop_to_name(prop);
			proptype = zpool_prop_get_type(prop);

			if (nvpair_type(elem) == DATA_TYPE_STRING) {
				ASSERT(proptype == PROP_TYPE_STRING);
				strval = fnvpair_value_string(elem);
				VERIFY0(zap_update(mos,
				    spa->spa_pool_props_object, propname,
				    1, strlen(strval) + 1, strval, tx));
				spa_history_log_internal(spa, "set", tx,
				    "%s=%s", nvpair_name(elem), strval);
			} else if (nvpair_type(elem) == DATA_TYPE_UINT64) {
				intval = fnvpair_value_uint64(elem);

				if (proptype == PROP_TYPE_INDEX) {
					const char *unused;
					VERIFY0(zpool_prop_index_to_string(
					    prop, intval, &unused));
				}
				VERIFY0(zap_update(mos,
				    spa->spa_pool_props_object, propname,
				    8, 1, &intval, tx));
				spa_history_log_internal(spa, "set", tx,
				    "%s=%lld", nvpair_name(elem), intval);
			} else {
				ASSERT(0); /* not allowed */
			}

			switch (prop) {
			case ZPOOL_PROP_DELEGATION:
				spa->spa_delegation = intval;
				break;
			case ZPOOL_PROP_BOOTFS:
				spa->spa_bootfs = intval;
				break;
			case ZPOOL_PROP_FAILUREMODE:
				spa->spa_failmode = intval;
				break;
			case ZPOOL_PROP_AUTOEXPAND:
				spa->spa_autoexpand = intval;
				if (tx->tx_txg != TXG_INITIAL)
					spa_async_request(spa,
					    SPA_ASYNC_AUTOEXPAND);
				break;
			case ZPOOL_PROP_DEDUPDITTO:
				spa->spa_dedup_ditto = intval;
				break;
			default:
				break;
			}
		}

	}

	mutex_exit(&spa->spa_props_lock);
}

/*
 * Perform one-time upgrade on-disk changes.  spa_version() does not
 * reflect the new version this txg, so there must be no changes this
 * txg to anything that the upgrade code depends on after it executes.
 * Therefore this must be called after dsl_pool_sync() does the sync
 * tasks.
 */
static void
spa_sync_upgrades(spa_t *spa, dmu_tx_t *tx)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;

	ASSERT(spa->spa_sync_pass == 1);

	rrw_enter(&dp->dp_config_rwlock, RW_WRITER, FTAG);

	if (spa->spa_ubsync.ub_version < SPA_VERSION_ORIGIN &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_ORIGIN) {
		dsl_pool_create_origin(dp, tx);

		/* Keeping the origin open increases spa_minref */
		spa->spa_minref += 3;
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_NEXT_CLONES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_NEXT_CLONES) {
		dsl_pool_upgrade_clones(dp, tx);
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_DIR_CLONES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_DIR_CLONES) {
		dsl_pool_upgrade_dir_clones(dp, tx);

		/* Keeping the freedir open increases spa_minref */
		spa->spa_minref += 3;
	}

	if (spa->spa_ubsync.ub_version < SPA_VERSION_FEATURES &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_FEATURES) {
		spa_feature_create_zap_objects(spa, tx);
	}

	/*
	 * LZ4_COMPRESS feature's behaviour was changed to activate_on_enable
	 * when possibility to use lz4 compression for metadata was added
	 * Old pools that have this feature enabled must be upgraded to have
	 * this feature active
	 */
	if (spa->spa_uberblock.ub_version >= SPA_VERSION_FEATURES) {
		boolean_t lz4_en = spa_feature_is_enabled(spa,
		    SPA_FEATURE_LZ4_COMPRESS);
		boolean_t lz4_ac = spa_feature_is_active(spa,
		    SPA_FEATURE_LZ4_COMPRESS);

		if (lz4_en && !lz4_ac)
			spa_feature_incr(spa, SPA_FEATURE_LZ4_COMPRESS, tx);
	}

	/*
	 * If we haven't written the salt, do so now.  Note that the
	 * feature may not be activated yet, but that's fine since
	 * the presence of this ZAP entry is backwards compatible.
	 */
	if (zap_contains(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CHECKSUM_SALT) == ENOENT) {
		VERIFY0(zap_add(spa->spa_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CHECKSUM_SALT, 1,
		    sizeof (spa->spa_cksum_salt.zcs_bytes),
		    spa->spa_cksum_salt.zcs_bytes, tx));
	}

	rrw_exit(&dp->dp_config_rwlock, FTAG);
}

static void
vdev_indirect_state_sync_verify(vdev_t *vd)
{
	vdev_indirect_mapping_t *vim = vd->vdev_indirect_mapping;
	vdev_indirect_births_t *vib = vd->vdev_indirect_births;

	if (vd->vdev_ops == &vdev_indirect_ops) {
		ASSERT(vim != NULL);
		ASSERT(vib != NULL);
	}

	if (vdev_obsolete_sm_object(vd) != 0) {
		ASSERT(vd->vdev_obsolete_sm != NULL);
		ASSERT(vd->vdev_removing ||
		    vd->vdev_ops == &vdev_indirect_ops);
		ASSERT(vdev_indirect_mapping_num_entries(vim) > 0);
		ASSERT(vdev_indirect_mapping_bytes_mapped(vim) > 0);

		ASSERT3U(vdev_obsolete_sm_object(vd), ==,
		    space_map_object(vd->vdev_obsolete_sm));
		ASSERT3U(vdev_indirect_mapping_bytes_mapped(vim), >=,
		    space_map_allocated(vd->vdev_obsolete_sm));
	}
	ASSERT(vd->vdev_obsolete_segments != NULL);

	/*
	 * Since frees / remaps to an indirect vdev can only
	 * happen in syncing context, the obsolete segments
	 * tree must be empty when we start syncing.
	 */
	ASSERT0(range_tree_space(vd->vdev_obsolete_segments));
}

/*
 * Sync the specified transaction group.  New blocks may be dirtied as
 * part of the process, so we iterate until it converges.
 */
void
spa_sync(spa_t *spa, uint64_t txg)
{
	dsl_pool_t *dp = spa->spa_dsl_pool;
	objset_t *mos = spa->spa_meta_objset;
	bplist_t *free_bpl = &spa->spa_free_bplist[txg & TXG_MASK];
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd;
	dmu_tx_t *tx;
	int error;
	uint32_t max_queue_depth = zfs_vdev_async_write_max_active *
	    zfs_vdev_queue_depth_pct / 100;

	VERIFY(spa_writeable(spa));

	/*
	 * Wait for i/os issued in open context that need to complete
	 * before this txg syncs.
	 */
	(void) zio_wait(spa->spa_txg_zio[txg & TXG_MASK]);
	spa->spa_txg_zio[txg & TXG_MASK] = zio_root(spa, NULL, NULL,
	    ZIO_FLAG_CANFAIL);

	/*
	 * Lock out configuration changes.
	 */
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	spa->spa_syncing_txg = txg;
	spa->spa_sync_pass = 0;

	for (int i = 0; i < spa->spa_alloc_count; i++) {
		mutex_enter(&spa->spa_alloc_locks[i]);
		VERIFY0(avl_numnodes(&spa->spa_alloc_trees[i]));
		mutex_exit(&spa->spa_alloc_locks[i]);
	}

	/*
	 * If there are any pending vdev state changes, convert them
	 * into config changes that go out with this transaction group.
	 */
	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	while (list_head(&spa->spa_state_dirty_list) != NULL) {
		/*
		 * We need the write lock here because, for aux vdevs,
		 * calling vdev_config_dirty() modifies sav_config.
		 * This is ugly and will become unnecessary when we
		 * eliminate the aux vdev wart by integrating all vdevs
		 * into the root vdev tree.
		 */
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_WRITER);
		while ((vd = list_head(&spa->spa_state_dirty_list)) != NULL) {
			vdev_state_clean(vd);
			vdev_config_dirty(vd);
		}
		spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
		spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);
	}
	spa_config_exit(spa, SCL_STATE, FTAG);

	tx = dmu_tx_create_assigned(dp, txg);

	spa->spa_sync_starttime = gethrtime();
#ifdef illumos
	VERIFY(cyclic_reprogram(spa->spa_deadman_cycid,
	    spa->spa_sync_starttime + spa->spa_deadman_synctime));
#else	/* !illumos */
#ifdef _KERNEL
	callout_schedule(&spa->spa_deadman_cycid,
	    hz * spa->spa_deadman_synctime / NANOSEC);
#endif
#endif	/* illumos */

	/*
	 * If we are upgrading to SPA_VERSION_RAIDZ_DEFLATE this txg,
	 * set spa_deflate if we have no raid-z vdevs.
	 */
	if (spa->spa_ubsync.ub_version < SPA_VERSION_RAIDZ_DEFLATE &&
	    spa->spa_uberblock.ub_version >= SPA_VERSION_RAIDZ_DEFLATE) {
		int i;

		for (i = 0; i < rvd->vdev_children; i++) {
			vd = rvd->vdev_child[i];
			if (vd->vdev_deflate_ratio != SPA_MINBLOCKSIZE)
				break;
		}
		if (i == rvd->vdev_children) {
			spa->spa_deflate = TRUE;
			VERIFY(0 == zap_add(spa->spa_meta_objset,
			    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_DEFLATE,
			    sizeof (uint64_t), 1, &spa->spa_deflate, tx));
		}
	}

	/*
	 * Set the top-level vdev's max queue depth. Evaluate each
	 * top-level's async write queue depth in case it changed.
	 * The max queue depth will not change in the middle of syncing
	 * out this txg.
	 */
	uint64_t slots_per_allocator = 0;
	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		if (mg == NULL || mg->mg_class != spa_normal_class(spa) ||
		    !metaslab_group_initialized(mg))
			continue;

		/*
		 * It is safe to do a lock-free check here because only async
		 * allocations look at mg_max_alloc_queue_depth, and async
		 * allocations all happen from spa_sync().
		 */
		for (int i = 0; i < spa->spa_alloc_count; i++)
			ASSERT0(refcount_count(&(mg->mg_alloc_queue_depth[i])));
		mg->mg_max_alloc_queue_depth = max_queue_depth;

		for (int i = 0; i < spa->spa_alloc_count; i++) {
			mg->mg_cur_max_alloc_queue_depth[i] =
			    zfs_vdev_def_queue_depth;
		}
		slots_per_allocator += zfs_vdev_def_queue_depth;
	}
	metaslab_class_t *mc = spa_normal_class(spa);
	for (int i = 0; i < spa->spa_alloc_count; i++) {
		ASSERT0(refcount_count(&mc->mc_alloc_slots[i]));
		mc->mc_alloc_max_slots[i] = slots_per_allocator;
	}
	mc->mc_alloc_throttle_enabled = zio_dva_throttle_enabled;

	for (int c = 0; c < rvd->vdev_children; c++) {
		vdev_t *vd = rvd->vdev_child[c];
		vdev_indirect_state_sync_verify(vd);

		if (vdev_indirect_should_condense(vd)) {
			spa_condense_indirect_start_sync(vd, tx);
			break;
		}
	}

	/*
	 * Iterate to convergence.
	 */
	do {
		int pass = ++spa->spa_sync_pass;

		spa_sync_config_object(spa, tx);
		spa_sync_aux_dev(spa, &spa->spa_spares, tx,
		    ZPOOL_CONFIG_SPARES, DMU_POOL_SPARES);
		spa_sync_aux_dev(spa, &spa->spa_l2cache, tx,
		    ZPOOL_CONFIG_L2CACHE, DMU_POOL_L2CACHE);
		spa_errlog_sync(spa, txg);
		dsl_pool_sync(dp, txg);

		if (pass < zfs_sync_pass_deferred_free) {
			spa_sync_frees(spa, free_bpl, tx);
		} else {
			/*
			 * We can not defer frees in pass 1, because
			 * we sync the deferred frees later in pass 1.
			 */
			ASSERT3U(pass, >, 1);
			bplist_iterate(free_bpl, bpobj_enqueue_cb,
			    &spa->spa_deferred_bpobj, tx);
		}

		ddt_sync(spa, txg);
		dsl_scan_sync(dp, tx);

		if (spa->spa_vdev_removal != NULL)
			svr_sync(spa, tx);

		while ((vd = txg_list_remove(&spa->spa_vdev_txg_list, txg))
		    != NULL)
			vdev_sync(vd, txg);

		if (pass == 1) {
			spa_sync_upgrades(spa, tx);
			ASSERT3U(txg, >=,
			    spa->spa_uberblock.ub_rootbp.blk_birth);
			/*
			 * Note: We need to check if the MOS is dirty
			 * because we could have marked the MOS dirty
			 * without updating the uberblock (e.g. if we
			 * have sync tasks but no dirty user data).  We
			 * need to check the uberblock's rootbp because
			 * it is updated if we have synced out dirty
			 * data (though in this case the MOS will most
			 * likely also be dirty due to second order
			 * effects, we don't want to rely on that here).
			 */
			if (spa->spa_uberblock.ub_rootbp.blk_birth < txg &&
			    !dmu_objset_is_dirty(mos, txg)) {
				/*
				 * Nothing changed on the first pass,
				 * therefore this TXG is a no-op.  Avoid
				 * syncing deferred frees, so that we
				 * can keep this TXG as a no-op.
				 */
				ASSERT(txg_list_empty(&dp->dp_dirty_datasets,
				    txg));
				ASSERT(txg_list_empty(&dp->dp_dirty_dirs, txg));
				ASSERT(txg_list_empty(&dp->dp_sync_tasks, txg));
				ASSERT(txg_list_empty(&dp->dp_early_sync_tasks,
				    txg));
				break;
			}
			spa_sync_deferred_frees(spa, tx);
		}

	} while (dmu_objset_is_dirty(mos, txg));

	if (!list_is_empty(&spa->spa_config_dirty_list)) {
		/*
		 * Make sure that the number of ZAPs for all the vdevs matches
		 * the number of ZAPs in the per-vdev ZAP list. This only gets
		 * called if the config is dirty; otherwise there may be
		 * outstanding AVZ operations that weren't completed in
		 * spa_sync_config_object.
		 */
		uint64_t all_vdev_zap_entry_count;
		ASSERT0(zap_count(spa->spa_meta_objset,
		    spa->spa_all_vdev_zaps, &all_vdev_zap_entry_count));
		ASSERT3U(vdev_count_verify_zaps(spa->spa_root_vdev), ==,
		    all_vdev_zap_entry_count);
	}

	if (spa->spa_vdev_removal != NULL) {
		ASSERT0(spa->spa_vdev_removal->svr_bytes_done[txg & TXG_MASK]);
	}

	/*
	 * Rewrite the vdev configuration (which includes the uberblock)
	 * to commit the transaction group.
	 *
	 * If there are no dirty vdevs, we sync the uberblock to a few
	 * random top-level vdevs that are known to be visible in the
	 * config cache (see spa_vdev_add() for a complete description).
	 * If there *are* dirty vdevs, sync the uberblock to all vdevs.
	 */
	for (;;) {
		/*
		 * We hold SCL_STATE to prevent vdev open/close/etc.
		 * while we're attempting to write the vdev labels.
		 */
		spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

		if (list_is_empty(&spa->spa_config_dirty_list)) {
			vdev_t *svd[SPA_SYNC_MIN_VDEVS] = { NULL };
			int svdcount = 0;
			int children = rvd->vdev_children;
			int c0 = spa_get_random(children);

			for (int c = 0; c < children; c++) {
				vd = rvd->vdev_child[(c0 + c) % children];

				/* Stop when revisiting the first vdev */
				if (c > 0 && svd[0] == vd)
					break;

				if (vd->vdev_ms_array == 0 || vd->vdev_islog ||
				    !vdev_is_concrete(vd))
					continue;

				svd[svdcount++] = vd;
				if (svdcount == SPA_SYNC_MIN_VDEVS)
					break;
			}
			error = vdev_config_sync(svd, svdcount, txg);
		} else {
			error = vdev_config_sync(rvd->vdev_child,
			    rvd->vdev_children, txg);
		}

		if (error == 0)
			spa->spa_last_synced_guid = rvd->vdev_guid;

		spa_config_exit(spa, SCL_STATE, FTAG);

		if (error == 0)
			break;
		zio_suspend(spa, NULL);
		zio_resume_wait(spa);
	}
	dmu_tx_commit(tx);

#ifdef illumos
	VERIFY(cyclic_reprogram(spa->spa_deadman_cycid, CY_INFINITY));
#else	/* !illumos */
#ifdef _KERNEL
	callout_drain(&spa->spa_deadman_cycid);
#endif
#endif	/* illumos */

	/*
	 * Clear the dirty config list.
	 */
	while ((vd = list_head(&spa->spa_config_dirty_list)) != NULL)
		vdev_config_clean(vd);

	/*
	 * Now that the new config has synced transactionally,
	 * let it become visible to the config cache.
	 */
	if (spa->spa_config_syncing != NULL) {
		spa_config_set(spa, spa->spa_config_syncing);
		spa->spa_config_txg = txg;
		spa->spa_config_syncing = NULL;
	}

	dsl_pool_sync_done(dp, txg);

	for (int i = 0; i < spa->spa_alloc_count; i++) {
		mutex_enter(&spa->spa_alloc_locks[i]);
		VERIFY0(avl_numnodes(&spa->spa_alloc_trees[i]));
		mutex_exit(&spa->spa_alloc_locks[i]);
	}

	/*
	 * Update usable space statistics.
	 */
	while ((vd = txg_list_remove(&spa->spa_vdev_txg_list, TXG_CLEAN(txg)))
	    != NULL)
		vdev_sync_done(vd, txg);

	spa_update_dspace(spa);

	/*
	 * It had better be the case that we didn't dirty anything
	 * since vdev_config_sync().
	 */
	ASSERT(txg_list_empty(&dp->dp_dirty_datasets, txg));
	ASSERT(txg_list_empty(&dp->dp_dirty_dirs, txg));
	ASSERT(txg_list_empty(&spa->spa_vdev_txg_list, txg));

	while (zfs_pause_spa_sync)
		delay(1);

	spa->spa_sync_pass = 0;

	/*
	 * Update the last synced uberblock here. We want to do this at
	 * the end of spa_sync() so that consumers of spa_last_synced_txg()
	 * will be guaranteed that all the processing associated with
	 * that txg has been completed.
	 */
	spa->spa_ubsync = spa->spa_uberblock;
	spa_config_exit(spa, SCL_CONFIG, FTAG);

	spa_handle_ignored_writes(spa);

	/*
	 * If any async tasks have been requested, kick them off.
	 */
	spa_async_dispatch(spa);
	spa_async_dispatch_vd(spa);
}

/*
 * Sync all pools.  We don't want to hold the namespace lock across these
 * operations, so we take a reference on the spa_t and drop the lock during the
 * sync.
 */
void
spa_sync_allpools(void)
{
	spa_t *spa = NULL;
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL) {
		if (spa_state(spa) != POOL_STATE_ACTIVE ||
		    !spa_writeable(spa) || spa_suspended(spa))
			continue;
		spa_open_ref(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		txg_wait_synced(spa_get_dsl(spa), 0);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);
	}
	mutex_exit(&spa_namespace_lock);
}

/*
 * ==========================================================================
 * Miscellaneous routines
 * ==========================================================================
 */

/*
 * Remove all pools in the system.
 */
void
spa_evict_all(void)
{
	spa_t *spa;

	/*
	 * Remove all cached state.  All pools should be closed now,
	 * so every spa in the AVL tree should be unreferenced.
	 */
	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(NULL)) != NULL) {
		/*
		 * Stop async tasks.  The async thread may need to detach
		 * a device that's been replaced, which requires grabbing
		 * spa_namespace_lock, so we must drop it here.
		 */
		spa_open_ref(spa, FTAG);
		mutex_exit(&spa_namespace_lock);
		spa_async_suspend(spa);
		mutex_enter(&spa_namespace_lock);
		spa_close(spa, FTAG);

		if (spa->spa_state != POOL_STATE_UNINITIALIZED) {
			spa_unload(spa);
			spa_deactivate(spa);
		}
		spa_remove(spa);
	}
	mutex_exit(&spa_namespace_lock);
}

vdev_t *
spa_lookup_by_guid(spa_t *spa, uint64_t guid, boolean_t aux)
{
	vdev_t *vd;
	int i;

	if ((vd = vdev_lookup_by_guid(spa->spa_root_vdev, guid)) != NULL)
		return (vd);

	if (aux) {
		for (i = 0; i < spa->spa_l2cache.sav_count; i++) {
			vd = spa->spa_l2cache.sav_vdevs[i];
			if (vd->vdev_guid == guid)
				return (vd);
		}

		for (i = 0; i < spa->spa_spares.sav_count; i++) {
			vd = spa->spa_spares.sav_vdevs[i];
			if (vd->vdev_guid == guid)
				return (vd);
		}
	}

	return (NULL);
}

void
spa_upgrade(spa_t *spa, uint64_t version)
{
	ASSERT(spa_writeable(spa));

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	/*
	 * This should only be called for a non-faulted pool, and since a
	 * future version would result in an unopenable pool, this shouldn't be
	 * possible.
	 */
	ASSERT(SPA_VERSION_IS_SUPPORTED(spa->spa_uberblock.ub_version));
	ASSERT3U(version, >=, spa->spa_uberblock.ub_version);

	spa->spa_uberblock.ub_version = version;
	vdev_config_dirty(spa->spa_root_vdev);

	spa_config_exit(spa, SCL_ALL, FTAG);

	txg_wait_synced(spa_get_dsl(spa), 0);
}

boolean_t
spa_has_spare(spa_t *spa, uint64_t guid)
{
	int i;
	uint64_t spareguid;
	spa_aux_vdev_t *sav = &spa->spa_spares;

	for (i = 0; i < sav->sav_count; i++)
		if (sav->sav_vdevs[i]->vdev_guid == guid)
			return (B_TRUE);

	for (i = 0; i < sav->sav_npending; i++) {
		if (nvlist_lookup_uint64(sav->sav_pending[i], ZPOOL_CONFIG_GUID,
		    &spareguid) == 0 && spareguid == guid)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Check if a pool has an active shared spare device.
 * Note: reference count of an active spare is 2, as a spare and as a replace
 */
static boolean_t
spa_has_active_shared_spare(spa_t *spa)
{
	int i, refcnt;
	uint64_t pool;
	spa_aux_vdev_t *sav = &spa->spa_spares;

	for (i = 0; i < sav->sav_count; i++) {
		if (spa_spare_exists(sav->sav_vdevs[i]->vdev_guid, &pool,
		    &refcnt) && pool != 0ULL && pool == spa_guid(spa) &&
		    refcnt > 2)
			return (B_TRUE);
	}

	return (B_FALSE);
}

sysevent_t *
spa_event_create(spa_t *spa, vdev_t *vd, nvlist_t *hist_nvl, const char *name)
{
	sysevent_t		*ev = NULL;
#ifdef _KERNEL
	sysevent_attr_list_t	*attr = NULL;
	sysevent_value_t	value;

	ev = sysevent_alloc(EC_ZFS, (char *)name, SUNW_KERN_PUB "zfs",
	    SE_SLEEP);
	ASSERT(ev != NULL);

	value.value_type = SE_DATA_TYPE_STRING;
	value.value.sv_string = spa_name(spa);
	if (sysevent_add_attr(&attr, ZFS_EV_POOL_NAME, &value, SE_SLEEP) != 0)
		goto done;

	value.value_type = SE_DATA_TYPE_UINT64;
	value.value.sv_uint64 = spa_guid(spa);
	if (sysevent_add_attr(&attr, ZFS_EV_POOL_GUID, &value, SE_SLEEP) != 0)
		goto done;

	if (vd) {
		value.value_type = SE_DATA_TYPE_UINT64;
		value.value.sv_uint64 = vd->vdev_guid;
		if (sysevent_add_attr(&attr, ZFS_EV_VDEV_GUID, &value,
		    SE_SLEEP) != 0)
			goto done;

		if (vd->vdev_path) {
			value.value_type = SE_DATA_TYPE_STRING;
			value.value.sv_string = vd->vdev_path;
			if (sysevent_add_attr(&attr, ZFS_EV_VDEV_PATH,
			    &value, SE_SLEEP) != 0)
				goto done;
		}
	}

	if (hist_nvl != NULL) {
		fnvlist_merge((nvlist_t *)attr, hist_nvl);
	}

	if (sysevent_attach_attributes(ev, attr) != 0)
		goto done;
	attr = NULL;

done:
	if (attr)
		sysevent_free_attr(attr);

#endif
	return (ev);
}

void
spa_event_post(sysevent_t *ev)
{
#ifdef _KERNEL
	sysevent_id_t		eid;

	(void) log_sysevent(ev, SE_SLEEP, &eid);
	sysevent_free(ev);
#endif
}

void
spa_event_discard(sysevent_t *ev)
{
#ifdef _KERNEL
	sysevent_free(ev);
#endif
}

/*
 * Post a sysevent corresponding to the given event.  The 'name' must be one of
 * the event definitions in sys/sysevent/eventdefs.h.  The payload will be
 * filled in from the spa and (optionally) the vdev and history nvl.  This
 * doesn't do anything in the userland libzpool, as we don't want consumers to
 * misinterpret ztest or zdb as real changes.
 */
void
spa_event_notify(spa_t *spa, vdev_t *vd, nvlist_t *hist_nvl, const char *name)
{
	spa_event_post(spa_event_create(spa, vd, hist_nvl, name));
}
