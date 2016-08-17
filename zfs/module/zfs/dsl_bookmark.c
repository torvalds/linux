/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2013, 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/spa.h>
#include <sys/dsl_bookmark.h>
#include <zfs_namecheck.h>

static int
dsl_bookmark_hold_ds(dsl_pool_t *dp, const char *fullname,
    dsl_dataset_t **dsp, void *tag, char **shortnamep)
{
	char buf[MAXNAMELEN];
	char *hashp;

	if (strlen(fullname) >= MAXNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));
	hashp = strchr(fullname, '#');
	if (hashp == NULL)
		return (SET_ERROR(EINVAL));

	*shortnamep = hashp + 1;
	if (zfs_component_namecheck(*shortnamep, NULL, NULL))
		return (SET_ERROR(EINVAL));
	(void) strlcpy(buf, fullname, hashp - fullname + 1);
	return (dsl_dataset_hold(dp, buf, tag, dsp));
}

/*
 * Returns ESRCH if bookmark is not found.
 */
static int
dsl_dataset_bmark_lookup(dsl_dataset_t *ds, const char *shortname,
    zfs_bookmark_phys_t *bmark_phys)
{
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t bmark_zapobj = ds->ds_bookmarks;
	matchtype_t mt;
	int err;

	if (bmark_zapobj == 0)
		return (SET_ERROR(ESRCH));

	if (dsl_dataset_phys(ds)->ds_flags & DS_FLAG_CI_DATASET)
		mt = MT_FIRST;
	else
		mt = MT_EXACT;

	err = zap_lookup_norm(mos, bmark_zapobj, shortname, sizeof (uint64_t),
	    sizeof (*bmark_phys) / sizeof (uint64_t), bmark_phys, mt,
	    NULL, 0, NULL);

	return (err == ENOENT ? ESRCH : err);
}

/*
 * If later_ds is non-NULL, this will return EXDEV if the the specified bookmark
 * does not represents an earlier point in later_ds's timeline.
 *
 * Returns ENOENT if the dataset containing the bookmark does not exist.
 * Returns ESRCH if the dataset exists but the bookmark was not found in it.
 */
int
dsl_bookmark_lookup(dsl_pool_t *dp, const char *fullname,
    dsl_dataset_t *later_ds, zfs_bookmark_phys_t *bmp)
{
	char *shortname;
	dsl_dataset_t *ds;
	int error;

	error = dsl_bookmark_hold_ds(dp, fullname, &ds, FTAG, &shortname);
	if (error != 0)
		return (error);

	error = dsl_dataset_bmark_lookup(ds, shortname, bmp);
	if (error == 0 && later_ds != NULL) {
		if (!dsl_dataset_is_before(later_ds, ds, bmp->zbm_creation_txg))
			error = SET_ERROR(EXDEV);
	}
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

typedef struct dsl_bookmark_create_arg {
	nvlist_t *dbca_bmarks;
	nvlist_t *dbca_errors;
} dsl_bookmark_create_arg_t;

static int
dsl_bookmark_create_check_impl(dsl_dataset_t *snapds, const char *bookmark_name,
    dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *bmark_fs;
	char *shortname;
	int error;
	zfs_bookmark_phys_t bmark_phys;

	if (!snapds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	error = dsl_bookmark_hold_ds(dp, bookmark_name,
	    &bmark_fs, FTAG, &shortname);
	if (error != 0)
		return (error);

	if (!dsl_dataset_is_before(bmark_fs, snapds, 0)) {
		dsl_dataset_rele(bmark_fs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	error = dsl_dataset_bmark_lookup(bmark_fs, shortname,
	    &bmark_phys);
	dsl_dataset_rele(bmark_fs, FTAG);
	if (error == 0)
		return (SET_ERROR(EEXIST));
	if (error == ESRCH)
		return (0);
	return (error);
}

static int
dsl_bookmark_create_check(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_create_arg_t *dbca = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	int rv = 0;
	nvpair_t *pair;

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_BOOKMARKS))
		return (SET_ERROR(ENOTSUP));

	for (pair = nvlist_next_nvpair(dbca->dbca_bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbca->dbca_bmarks, pair)) {
		dsl_dataset_t *snapds;
		int error;

		/* note: validity of nvlist checked by ioctl layer */
		error = dsl_dataset_hold(dp, fnvpair_value_string(pair),
		    FTAG, &snapds);
		if (error == 0) {
			error = dsl_bookmark_create_check_impl(snapds,
			    nvpair_name(pair), tx);
			dsl_dataset_rele(snapds, FTAG);
		}
		if (error != 0) {
			fnvlist_add_int32(dbca->dbca_errors,
			    nvpair_name(pair), error);
			rv = error;
		}
	}

	return (rv);
}

static void
dsl_bookmark_create_sync(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_create_arg_t *dbca = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	nvpair_t *pair;

	ASSERT(spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_BOOKMARKS));

	for (pair = nvlist_next_nvpair(dbca->dbca_bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbca->dbca_bmarks, pair)) {
		dsl_dataset_t *snapds, *bmark_fs;
		zfs_bookmark_phys_t bmark_phys;
		char *shortname;

		VERIFY0(dsl_dataset_hold(dp, fnvpair_value_string(pair),
		    FTAG, &snapds));
		VERIFY0(dsl_bookmark_hold_ds(dp, nvpair_name(pair),
		    &bmark_fs, FTAG, &shortname));
		if (bmark_fs->ds_bookmarks == 0) {
			bmark_fs->ds_bookmarks =
			    zap_create_norm(mos, U8_TEXTPREP_TOUPPER,
			    DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);
			spa_feature_incr(dp->dp_spa, SPA_FEATURE_BOOKMARKS, tx);

			dsl_dataset_zapify(bmark_fs, tx);
			VERIFY0(zap_add(mos, bmark_fs->ds_object,
			    DS_FIELD_BOOKMARK_NAMES,
			    sizeof (bmark_fs->ds_bookmarks), 1,
			    &bmark_fs->ds_bookmarks, tx));
		}

		bmark_phys.zbm_guid = dsl_dataset_phys(snapds)->ds_guid;
		bmark_phys.zbm_creation_txg =
		    dsl_dataset_phys(snapds)->ds_creation_txg;
		bmark_phys.zbm_creation_time =
		    dsl_dataset_phys(snapds)->ds_creation_time;

		VERIFY0(zap_add(mos, bmark_fs->ds_bookmarks,
		    shortname, sizeof (uint64_t),
		    sizeof (zfs_bookmark_phys_t) / sizeof (uint64_t),
		    &bmark_phys, tx));

		spa_history_log_internal_ds(bmark_fs, "bookmark", tx,
		    "name=%s creation_txg=%llu target_snap=%llu",
		    shortname,
		    (longlong_t)bmark_phys.zbm_creation_txg,
		    (longlong_t)snapds->ds_object);

		dsl_dataset_rele(bmark_fs, FTAG);
		dsl_dataset_rele(snapds, FTAG);
	}
}

/*
 * The bookmarks must all be in the same pool.
 */
int
dsl_bookmark_create(nvlist_t *bmarks, nvlist_t *errors)
{
	nvpair_t *pair;
	dsl_bookmark_create_arg_t dbca;

	pair = nvlist_next_nvpair(bmarks, NULL);
	if (pair == NULL)
		return (0);

	dbca.dbca_bmarks = bmarks;
	dbca.dbca_errors = errors;

	return (dsl_sync_task(nvpair_name(pair), dsl_bookmark_create_check,
	    dsl_bookmark_create_sync, &dbca,
	    fnvlist_num_pairs(bmarks), ZFS_SPACE_CHECK_NORMAL));
}

int
dsl_get_bookmarks_impl(dsl_dataset_t *ds, nvlist_t *props, nvlist_t *outnvl)
{
	int err = 0;
	zap_cursor_t zc;
	zap_attribute_t attr;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	uint64_t bmark_zapobj = ds->ds_bookmarks;
	if (bmark_zapobj == 0)
		return (0);

	for (zap_cursor_init(&zc, dp->dp_meta_objset, bmark_zapobj);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		nvlist_t *out_props;
		char *bmark_name = attr.za_name;
		zfs_bookmark_phys_t bmark_phys;

		err = dsl_dataset_bmark_lookup(ds, bmark_name, &bmark_phys);
		ASSERT3U(err, !=, ENOENT);
		if (err != 0)
			break;

		out_props = fnvlist_alloc();
		if (nvlist_exists(props,
		    zfs_prop_to_name(ZFS_PROP_GUID))) {
			dsl_prop_nvlist_add_uint64(out_props,
			    ZFS_PROP_GUID, bmark_phys.zbm_guid);
		}
		if (nvlist_exists(props,
		    zfs_prop_to_name(ZFS_PROP_CREATETXG))) {
			dsl_prop_nvlist_add_uint64(out_props,
			    ZFS_PROP_CREATETXG, bmark_phys.zbm_creation_txg);
		}
		if (nvlist_exists(props,
		    zfs_prop_to_name(ZFS_PROP_CREATION))) {
			dsl_prop_nvlist_add_uint64(out_props,
			    ZFS_PROP_CREATION, bmark_phys.zbm_creation_time);
		}

		fnvlist_add_nvlist(outnvl, bmark_name, out_props);
		fnvlist_free(out_props);
	}
	zap_cursor_fini(&zc);
	return (err);
}

/*
 * Retrieve the bookmarks that exist in the specified dataset, and the
 * requested properties of each bookmark.
 *
 * The "props" nvlist specifies which properties are requested.
 * See lzc_get_bookmarks() for the list of valid properties.
 */
int
dsl_get_bookmarks(const char *dsname, nvlist_t *props, nvlist_t *outnvl)
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

	err = dsl_get_bookmarks_impl(ds, props, outnvl);

	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	return (err);
}

typedef struct dsl_bookmark_destroy_arg {
	nvlist_t *dbda_bmarks;
	nvlist_t *dbda_success;
	nvlist_t *dbda_errors;
} dsl_bookmark_destroy_arg_t;

static int
dsl_dataset_bookmark_remove(dsl_dataset_t *ds, const char *name, dmu_tx_t *tx)
{
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t bmark_zapobj = ds->ds_bookmarks;
	matchtype_t mt;

	if (dsl_dataset_phys(ds)->ds_flags & DS_FLAG_CI_DATASET)
		mt = MT_FIRST;
	else
		mt = MT_EXACT;

	return (zap_remove_norm(mos, bmark_zapobj, name, mt, tx));
}

static int
dsl_bookmark_destroy_check(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_destroy_arg_t *dbda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	int rv = 0;
	nvpair_t *pair;

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_BOOKMARKS))
		return (0);

	for (pair = nvlist_next_nvpair(dbda->dbda_bmarks, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbda->dbda_bmarks, pair)) {
		const char *fullname = nvpair_name(pair);
		dsl_dataset_t *ds;
		zfs_bookmark_phys_t bm;
		int error;
		char *shortname;

		error = dsl_bookmark_hold_ds(dp, fullname, &ds,
		    FTAG, &shortname);
		if (error == ENOENT) {
			/* ignore it; the bookmark is "already destroyed" */
			continue;
		}
		if (error == 0) {
			error = dsl_dataset_bmark_lookup(ds, shortname, &bm);
			dsl_dataset_rele(ds, FTAG);
			if (error == ESRCH) {
				/*
				 * ignore it; the bookmark is
				 * "already destroyed"
				 */
				continue;
			}
		}
		if (error == 0) {
			fnvlist_add_boolean(dbda->dbda_success, fullname);
		} else {
			fnvlist_add_int32(dbda->dbda_errors, fullname, error);
			rv = error;
		}
	}
	return (rv);
}

static void
dsl_bookmark_destroy_sync(void *arg, dmu_tx_t *tx)
{
	dsl_bookmark_destroy_arg_t *dbda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	objset_t *mos = dp->dp_meta_objset;
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(dbda->dbda_success, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(dbda->dbda_success, pair)) {
		dsl_dataset_t *ds;
		char *shortname;
		uint64_t zap_cnt;

		VERIFY0(dsl_bookmark_hold_ds(dp, nvpair_name(pair),
		    &ds, FTAG, &shortname));
		VERIFY0(dsl_dataset_bookmark_remove(ds, shortname, tx));

		/*
		 * If all of this dataset's bookmarks have been destroyed,
		 * free the zap object and decrement the feature's use count.
		 */
		VERIFY0(zap_count(mos, ds->ds_bookmarks,
		    &zap_cnt));
		if (zap_cnt == 0) {
			dmu_buf_will_dirty(ds->ds_dbuf, tx);
			VERIFY0(zap_destroy(mos, ds->ds_bookmarks, tx));
			ds->ds_bookmarks = 0;
			spa_feature_decr(dp->dp_spa, SPA_FEATURE_BOOKMARKS, tx);
			VERIFY0(zap_remove(mos, ds->ds_object,
			    DS_FIELD_BOOKMARK_NAMES, tx));
		}

		spa_history_log_internal_ds(ds, "remove bookmark", tx,
		    "name=%s", shortname);

		dsl_dataset_rele(ds, FTAG);
	}
}

/*
 * The bookmarks must all be in the same pool.
 */
int
dsl_bookmark_destroy(nvlist_t *bmarks, nvlist_t *errors)
{
	int rv;
	dsl_bookmark_destroy_arg_t dbda;
	nvpair_t *pair = nvlist_next_nvpair(bmarks, NULL);
	if (pair == NULL)
		return (0);

	dbda.dbda_bmarks = bmarks;
	dbda.dbda_errors = errors;
	dbda.dbda_success = fnvlist_alloc();

	rv = dsl_sync_task(nvpair_name(pair), dsl_bookmark_destroy_check,
	    dsl_bookmark_destroy_sync, &dbda, fnvlist_num_pairs(bmarks),
	    ZFS_SPACE_CHECK_RESERVED);
	fnvlist_free(dbda.dbda_success);
	return (rv);
}
