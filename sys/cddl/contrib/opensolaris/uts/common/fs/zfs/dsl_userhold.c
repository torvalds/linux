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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dsl_userhold.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>
#include <sys/zfs_onexit.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>

typedef struct dsl_dataset_user_hold_arg {
	nvlist_t *dduha_holds;
	nvlist_t *dduha_chkholds;
	nvlist_t *dduha_errlist;
	minor_t dduha_minor;
} dsl_dataset_user_hold_arg_t;

/*
 * If you add new checks here, you may need to add additional checks to the
 * "temporary" case in snapshot_check() in dmu_objset.c.
 */
int
dsl_dataset_user_hold_check_one(dsl_dataset_t *ds, const char *htag,
    boolean_t temphold, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	int error = 0;

	ASSERT(dsl_pool_config_held(dp));

	if (strlen(htag) > MAXNAMELEN)
		return (SET_ERROR(E2BIG));
	/* Tempholds have a more restricted length */
	if (temphold && strlen(htag) + MAX_TAG_PREFIX_LEN >= MAXNAMELEN)
		return (SET_ERROR(E2BIG));

	/* tags must be unique (if ds already exists) */
	if (ds != NULL && dsl_dataset_phys(ds)->ds_userrefs_obj != 0) {
		uint64_t value;

		error = zap_lookup(mos, dsl_dataset_phys(ds)->ds_userrefs_obj,
		    htag, 8, 1, &value);
		if (error == 0)
			error = SET_ERROR(EEXIST);
		else if (error == ENOENT)
			error = 0;
	}

	return (error);
}

static int
dsl_dataset_user_hold_check(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_hold_arg_t *dduha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);

	if (spa_version(dp->dp_spa) < SPA_VERSION_USERREFS)
		return (SET_ERROR(ENOTSUP));

	if (!dmu_tx_is_syncing(tx))
		return (0);

	for (nvpair_t *pair = nvlist_next_nvpair(dduha->dduha_holds, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dduha->dduha_holds, pair)) {
		dsl_dataset_t *ds;
		int error = 0;
		char *htag, *name;

		/* must be a snapshot */
		name = nvpair_name(pair);
		if (strchr(name, '@') == NULL)
			error = SET_ERROR(EINVAL);

		if (error == 0)
			error = nvpair_value_string(pair, &htag);

		if (error == 0)
			error = dsl_dataset_hold(dp, name, FTAG, &ds);

		if (error == 0) {
			error = dsl_dataset_user_hold_check_one(ds, htag,
			    dduha->dduha_minor != 0, tx);
			dsl_dataset_rele(ds, FTAG);
		}

		if (error == 0) {
			fnvlist_add_string(dduha->dduha_chkholds, name, htag);
		} else {
			/*
			 * We register ENOENT errors so they can be correctly
			 * reported if needed, such as when all holds fail.
			 */
			fnvlist_add_int32(dduha->dduha_errlist, name, error);
			if (error != ENOENT)
				return (error);
		}
	}

	return (0);
}


static void
dsl_dataset_user_hold_sync_one_impl(nvlist_t *tmpholds, dsl_dataset_t *ds,
    const char *htag, minor_t minor, uint64_t now, dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	uint64_t zapobj;

	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));

	if (dsl_dataset_phys(ds)->ds_userrefs_obj == 0) {
		/*
		 * This is the first user hold for this dataset.  Create
		 * the userrefs zap object.
		 */
		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		zapobj = dsl_dataset_phys(ds)->ds_userrefs_obj =
		    zap_create(mos, DMU_OT_USERREFS, DMU_OT_NONE, 0, tx);
	} else {
		zapobj = dsl_dataset_phys(ds)->ds_userrefs_obj;
	}
	ds->ds_userrefs++;

	VERIFY0(zap_add(mos, zapobj, htag, 8, 1, &now, tx));

	if (minor != 0) {
		char name[MAXNAMELEN];
		nvlist_t *tags;

		VERIFY0(dsl_pool_user_hold(dp, ds->ds_object,
		    htag, now, tx));
		(void) snprintf(name, sizeof (name), "%llx",
		    (u_longlong_t)ds->ds_object);

		if (nvlist_lookup_nvlist(tmpholds, name, &tags) != 0) {
			tags = fnvlist_alloc();
			fnvlist_add_boolean(tags, htag);
			fnvlist_add_nvlist(tmpholds, name, tags);
			fnvlist_free(tags);
		} else {
			fnvlist_add_boolean(tags, htag);
		}
	}

	spa_history_log_internal_ds(ds, "hold", tx,
	    "tag=%s temp=%d refs=%llu",
	    htag, minor != 0, ds->ds_userrefs);
}

typedef struct zfs_hold_cleanup_arg {
	char zhca_spaname[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t zhca_spa_load_guid;
	nvlist_t *zhca_holds;
} zfs_hold_cleanup_arg_t;

static void
dsl_dataset_user_release_onexit(void *arg)
{
	zfs_hold_cleanup_arg_t *ca = arg;
	spa_t *spa;
	int error;

	error = spa_open(ca->zhca_spaname, &spa, FTAG);
	if (error != 0) {
		zfs_dbgmsg("couldn't release holds on pool=%s "
		    "because pool is no longer loaded",
		    ca->zhca_spaname);
		return;
	}
	if (spa_load_guid(spa) != ca->zhca_spa_load_guid) {
		zfs_dbgmsg("couldn't release holds on pool=%s "
		    "because pool is no longer loaded (guid doesn't match)",
		    ca->zhca_spaname);
		spa_close(spa, FTAG);
		return;
	}

	(void) dsl_dataset_user_release_tmp(spa_get_dsl(spa), ca->zhca_holds);
	fnvlist_free(ca->zhca_holds);
	kmem_free(ca, sizeof (zfs_hold_cleanup_arg_t));
	spa_close(spa, FTAG);
}

static void
dsl_onexit_hold_cleanup(spa_t *spa, nvlist_t *holds, minor_t minor)
{
	zfs_hold_cleanup_arg_t *ca;

	if (minor == 0 || nvlist_empty(holds)) {
		fnvlist_free(holds);
		return;
	}

	ASSERT(spa != NULL);
	ca = kmem_alloc(sizeof (*ca), KM_SLEEP);

	(void) strlcpy(ca->zhca_spaname, spa_name(spa),
	    sizeof (ca->zhca_spaname));
	ca->zhca_spa_load_guid = spa_load_guid(spa);
	ca->zhca_holds = holds;
	VERIFY0(zfs_onexit_add_cb(minor,
	    dsl_dataset_user_release_onexit, ca, NULL));
}

void
dsl_dataset_user_hold_sync_one(dsl_dataset_t *ds, const char *htag,
    minor_t minor, uint64_t now, dmu_tx_t *tx)
{
	nvlist_t *tmpholds;

	if (minor != 0)
		tmpholds = fnvlist_alloc();
	else
		tmpholds = NULL;
	dsl_dataset_user_hold_sync_one_impl(tmpholds, ds, htag, minor, now, tx);
	dsl_onexit_hold_cleanup(dsl_dataset_get_spa(ds), tmpholds, minor);
}

static void
dsl_dataset_user_hold_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_hold_arg_t *dduha = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	nvlist_t *tmpholds;
	uint64_t now = gethrestime_sec();

	if (dduha->dduha_minor != 0)
		tmpholds = fnvlist_alloc();
	else
		tmpholds = NULL;
	for (nvpair_t *pair = nvlist_next_nvpair(dduha->dduha_chkholds, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(dduha->dduha_chkholds, pair)) {
		dsl_dataset_t *ds;

		VERIFY0(dsl_dataset_hold(dp, nvpair_name(pair), FTAG, &ds));
		dsl_dataset_user_hold_sync_one_impl(tmpholds, ds,
		    fnvpair_value_string(pair), dduha->dduha_minor, now, tx);
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_onexit_hold_cleanup(dp->dp_spa, tmpholds, dduha->dduha_minor);
}

/*
 * The full semantics of this function are described in the comment above
 * lzc_hold().
 *
 * To summarize:
 * holds is nvl of snapname -> holdname
 * errlist will be filled in with snapname -> error
 *
 * The snaphosts must all be in the same pool.
 *
 * Holds for snapshots that don't exist will be skipped.
 *
 * If none of the snapshots for requested holds exist then ENOENT will be
 * returned.
 *
 * If cleanup_minor is not 0, the holds will be temporary, which will be cleaned
 * up when the process exits.
 *
 * On success all the holds, for snapshots that existed, will be created and 0
 * will be returned.
 *
 * On failure no holds will be created, the errlist will be filled in,
 * and an errno will returned.
 *
 * In all cases the errlist will contain entries for holds where the snapshot
 * didn't exist.
 */
int
dsl_dataset_user_hold(nvlist_t *holds, minor_t cleanup_minor, nvlist_t *errlist)
{
	dsl_dataset_user_hold_arg_t dduha;
	nvpair_t *pair;
	int ret;

	pair = nvlist_next_nvpair(holds, NULL);
	if (pair == NULL)
		return (0);

	dduha.dduha_holds = holds;
	dduha.dduha_chkholds = fnvlist_alloc();
	dduha.dduha_errlist = errlist;
	dduha.dduha_minor = cleanup_minor;

	ret = dsl_sync_task(nvpair_name(pair), dsl_dataset_user_hold_check,
	    dsl_dataset_user_hold_sync, &dduha,
	    fnvlist_num_pairs(holds), ZFS_SPACE_CHECK_RESERVED);
	fnvlist_free(dduha.dduha_chkholds);

	return (ret);
}

typedef int (dsl_holdfunc_t)(dsl_pool_t *dp, const char *name, void *tag,
    dsl_dataset_t **dsp);

typedef struct dsl_dataset_user_release_arg {
	dsl_holdfunc_t *ddura_holdfunc;
	nvlist_t *ddura_holds;
	nvlist_t *ddura_todelete;
	nvlist_t *ddura_errlist;
	nvlist_t *ddura_chkholds;
} dsl_dataset_user_release_arg_t;

/* Place a dataset hold on the snapshot identified by passed dsobj string */
static int
dsl_dataset_hold_obj_string(dsl_pool_t *dp, const char *dsobj, void *tag,
    dsl_dataset_t **dsp)
{
	return (dsl_dataset_hold_obj(dp, zfs_strtonum(dsobj, NULL), tag, dsp));
}

static int
dsl_dataset_user_release_check_one(dsl_dataset_user_release_arg_t *ddura,
    dsl_dataset_t *ds, nvlist_t *holds, const char *snapname)
{
	uint64_t zapobj;
	nvlist_t *holds_found;
	objset_t *mos;
	int numholds;

	if (!ds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	if (nvlist_empty(holds))
		return (0);

	numholds = 0;
	mos = ds->ds_dir->dd_pool->dp_meta_objset;
	zapobj = dsl_dataset_phys(ds)->ds_userrefs_obj;
	holds_found = fnvlist_alloc();

	for (nvpair_t *pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		uint64_t tmp;
		int error;
		const char *holdname = nvpair_name(pair);

		if (zapobj != 0)
			error = zap_lookup(mos, zapobj, holdname, 8, 1, &tmp);
		else
			error = SET_ERROR(ENOENT);

		/*
		 * Non-existent holds are put on the errlist, but don't
		 * cause an overall failure.
		 */
		if (error == ENOENT) {
			if (ddura->ddura_errlist != NULL) {
				char *errtag = kmem_asprintf("%s#%s",
				    snapname, holdname);
				fnvlist_add_int32(ddura->ddura_errlist, errtag,
				    ENOENT);
				strfree(errtag);
			}
			continue;
		}

		if (error != 0) {
			fnvlist_free(holds_found);
			return (error);
		}

		fnvlist_add_boolean(holds_found, holdname);
		numholds++;
	}

	if (DS_IS_DEFER_DESTROY(ds) &&
	    dsl_dataset_phys(ds)->ds_num_children == 1 &&
	    ds->ds_userrefs == numholds) {
		/* we need to destroy the snapshot as well */
		if (dsl_dataset_long_held(ds)) {
			fnvlist_free(holds_found);
			return (SET_ERROR(EBUSY));
		}
		fnvlist_add_boolean(ddura->ddura_todelete, snapname);
	}

	if (numholds != 0) {
		fnvlist_add_nvlist(ddura->ddura_chkholds, snapname,
		    holds_found);
	}
	fnvlist_free(holds_found);

	return (0);
}

static int
dsl_dataset_user_release_check(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_release_arg_t *ddura;
	dsl_holdfunc_t *holdfunc;
	dsl_pool_t *dp;

	if (!dmu_tx_is_syncing(tx))
		return (0);

	dp = dmu_tx_pool(tx);

	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));

	ddura = arg;
	holdfunc = ddura->ddura_holdfunc;

	for (nvpair_t *pair = nvlist_next_nvpair(ddura->ddura_holds, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(ddura->ddura_holds, pair)) {
		int error;
		dsl_dataset_t *ds;
		nvlist_t *holds;
		const char *snapname = nvpair_name(pair);

		error = nvpair_value_nvlist(pair, &holds);
		if (error != 0)
			error = (SET_ERROR(EINVAL));
		else
			error = holdfunc(dp, snapname, FTAG, &ds);
		if (error == 0) {
			error = dsl_dataset_user_release_check_one(ddura, ds,
			    holds, snapname);
			dsl_dataset_rele(ds, FTAG);
		}
		if (error != 0) {
			if (ddura->ddura_errlist != NULL) {
				fnvlist_add_int32(ddura->ddura_errlist,
				    snapname, error);
			}
			/*
			 * Non-existent snapshots are put on the errlist,
			 * but don't cause an overall failure.
			 */
			if (error != ENOENT)
				return (error);
		}
	}

	return (0);
}

static void
dsl_dataset_user_release_sync_one(dsl_dataset_t *ds, nvlist_t *holds,
    dmu_tx_t *tx)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	objset_t *mos = dp->dp_meta_objset;

	for (nvpair_t *pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(holds, pair)) {
		int error;
		const char *holdname = nvpair_name(pair);

		/* Remove temporary hold if one exists. */
		error = dsl_pool_user_release(dp, ds->ds_object, holdname, tx);
		VERIFY(error == 0 || error == ENOENT);

		VERIFY0(zap_remove(mos, dsl_dataset_phys(ds)->ds_userrefs_obj,
		    holdname, tx));
		ds->ds_userrefs--;

		spa_history_log_internal_ds(ds, "release", tx,
		    "tag=%s refs=%lld", holdname, (longlong_t)ds->ds_userrefs);
	}
}

static void
dsl_dataset_user_release_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_user_release_arg_t *ddura = arg;
	dsl_holdfunc_t *holdfunc = ddura->ddura_holdfunc;
	dsl_pool_t *dp = dmu_tx_pool(tx);

	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));

	for (nvpair_t *pair = nvlist_next_nvpair(ddura->ddura_chkholds, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(ddura->ddura_chkholds,
	    pair)) {
		dsl_dataset_t *ds;
		const char *name = nvpair_name(pair);

		VERIFY0(holdfunc(dp, name, FTAG, &ds));

		dsl_dataset_user_release_sync_one(ds,
		    fnvpair_value_nvlist(pair), tx);
		if (nvlist_exists(ddura->ddura_todelete, name)) {
			ASSERT(ds->ds_userrefs == 0 &&
			    dsl_dataset_phys(ds)->ds_num_children == 1 &&
			    DS_IS_DEFER_DESTROY(ds));
			dsl_destroy_snapshot_sync_impl(ds, B_FALSE, tx);
		}
		dsl_dataset_rele(ds, FTAG);
	}
}

/*
 * The full semantics of this function are described in the comment above
 * lzc_release().
 *
 * To summarize:
 * Releases holds specified in the nvl holds.
 *
 * holds is nvl of snapname -> { holdname, ... }
 * errlist will be filled in with snapname -> error
 *
 * If tmpdp is not NULL the names for holds should be the dsobj's of snapshots,
 * otherwise they should be the names of shapshots.
 *
 * As a release may cause snapshots to be destroyed this trys to ensure they
 * aren't mounted.
 *
 * The release of non-existent holds are skipped.
 *
 * At least one hold must have been released for the this function to succeed
 * and return 0.
 */
static int
dsl_dataset_user_release_impl(nvlist_t *holds, nvlist_t *errlist,
    dsl_pool_t *tmpdp)
{
	dsl_dataset_user_release_arg_t ddura;
	nvpair_t *pair;
	char *pool;
	int error;

	pair = nvlist_next_nvpair(holds, NULL);
	if (pair == NULL)
		return (0);

	/*
	 * The release may cause snapshots to be destroyed; make sure they
	 * are not mounted.
	 */
	if (tmpdp != NULL) {
		/* Temporary holds are specified by dsobj string. */
		ddura.ddura_holdfunc = dsl_dataset_hold_obj_string;
		pool = spa_name(tmpdp->dp_spa);
#ifdef _KERNEL
		for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
		    pair = nvlist_next_nvpair(holds, pair)) {
			dsl_dataset_t *ds;

			dsl_pool_config_enter(tmpdp, FTAG);
			error = dsl_dataset_hold_obj_string(tmpdp,
			    nvpair_name(pair), FTAG, &ds);
			if (error == 0) {
				char name[ZFS_MAX_DATASET_NAME_LEN];
				dsl_dataset_name(ds, name);
				dsl_pool_config_exit(tmpdp, FTAG);
				dsl_dataset_rele(ds, FTAG);
				(void) zfs_unmount_snap(name);
			} else {
				dsl_pool_config_exit(tmpdp, FTAG);
			}
		}
#endif
	} else {
		/* Non-temporary holds are specified by name. */
		ddura.ddura_holdfunc = dsl_dataset_hold;
		pool = nvpair_name(pair);
#ifdef _KERNEL
		for (pair = nvlist_next_nvpair(holds, NULL); pair != NULL;
		    pair = nvlist_next_nvpair(holds, pair)) {
			(void) zfs_unmount_snap(nvpair_name(pair));
		}
#endif
	}

	ddura.ddura_holds = holds;
	ddura.ddura_errlist = errlist;
	ddura.ddura_todelete = fnvlist_alloc();
	ddura.ddura_chkholds = fnvlist_alloc();

	error = dsl_sync_task(pool, dsl_dataset_user_release_check,
	    dsl_dataset_user_release_sync, &ddura, 0,
	    ZFS_SPACE_CHECK_EXTRA_RESERVED);
	fnvlist_free(ddura.ddura_todelete);
	fnvlist_free(ddura.ddura_chkholds);

	return (error);
}

/*
 * holds is nvl of snapname -> { holdname, ... }
 * errlist will be filled in with snapname -> error
 */
int
dsl_dataset_user_release(nvlist_t *holds, nvlist_t *errlist)
{
	return (dsl_dataset_user_release_impl(holds, errlist, NULL));
}

/*
 * holds is nvl of snapdsobj -> { holdname, ... }
 */
void
dsl_dataset_user_release_tmp(struct dsl_pool *dp, nvlist_t *holds)
{
	ASSERT(dp != NULL);
	(void) dsl_dataset_user_release_impl(holds, NULL, dp);
}

int
dsl_dataset_get_holds(const char *dsname, nvlist_t *nvl)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	int err;

	err = dsl_pool_hold(dsname, FTAG, &dp);
	if (err != 0)
		return (err);
	err = dsl_dataset_hold(dp, dsname, FTAG, &ds);
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	if (dsl_dataset_phys(ds)->ds_userrefs_obj != 0) {
		zap_attribute_t *za;
		zap_cursor_t zc;

		za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
		for (zap_cursor_init(&zc, ds->ds_dir->dd_pool->dp_meta_objset,
		    dsl_dataset_phys(ds)->ds_userrefs_obj);
		    zap_cursor_retrieve(&zc, za) == 0;
		    zap_cursor_advance(&zc)) {
			fnvlist_add_uint64(nvl, za->za_name,
			    za->za_first_integer);
		}
		zap_cursor_fini(&zc);
		kmem_free(za, sizeof (zap_attribute_t));
	}
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (0);
}
