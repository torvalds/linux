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
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/zfeature.h>
#include <sys/dmu.h>
#include <sys/nvpair.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include "zfeature_common.h"
#include <sys/spa_impl.h>

/*
 * ZFS Feature Flags
 * -----------------
 *
 * ZFS feature flags are used to provide fine-grained versioning to the ZFS
 * on-disk format. Once enabled on a pool feature flags replace the old
 * spa_version() number.
 *
 * Each new on-disk format change will be given a uniquely identifying string
 * guid rather than a version number. This avoids the problem of different
 * organizations creating new on-disk formats with the same version number. To
 * keep feature guids unique they should consist of the reverse dns name of the
 * organization which implemented the feature and a short name for the feature,
 * separated by a colon (e.g. com.delphix:async_destroy).
 *
 * Reference Counts
 * ----------------
 *
 * Within each pool features can be in one of three states: disabled, enabled,
 * or active. These states are differentiated by a reference count stored on
 * disk for each feature:
 *
 *   1) If there is no reference count stored on disk the feature is disabled.
 *   2) If the reference count is 0 a system administrator has enabled the
 *      feature, but the feature has not been used yet, so no on-disk
 *      format changes have been made.
 *   3) If the reference count is greater than 0 the feature is active.
 *      The format changes required by the feature are currently on disk.
 *      Note that if the feature's format changes are reversed the feature
 *      may choose to set its reference count back to 0.
 *
 * Feature flags makes no differentiation between non-zero reference counts
 * for an active feature (e.g. a reference count of 1 means the same thing as a
 * reference count of 27834721), but feature implementations may choose to use
 * the reference count to store meaningful information. For example, a new RAID
 * implementation might set the reference count to the number of vdevs using
 * it. If all those disks are removed from the pool the feature goes back to
 * having a reference count of 0.
 *
 * It is the responsibility of the individual features to maintain a non-zero
 * reference count as long as the feature's format changes are present on disk.
 *
 * Dependencies
 * ------------
 *
 * Each feature may depend on other features. The only effect of this
 * relationship is that when a feature is enabled all of its dependencies are
 * automatically enabled as well. Any future work to support disabling of
 * features would need to ensure that features cannot be disabled if other
 * enabled features depend on them.
 *
 * On-disk Format
 * --------------
 *
 * When feature flags are enabled spa_version() is set to SPA_VERSION_FEATURES
 * (5000). In order for this to work the pool is automatically upgraded to
 * SPA_VERSION_BEFORE_FEATURES (28) first, so all pre-feature flags on disk
 * format changes will be in use.
 *
 * Information about features is stored in 3 ZAP objects in the pool's MOS.
 * These objects are linked to by the following names in the pool directory
 * object:
 *
 * 1) features_for_read: feature guid -> reference count
 *    Features needed to open the pool for reading.
 * 2) features_for_write: feature guid -> reference count
 *    Features needed to open the pool for writing.
 * 3) feature_descriptions: feature guid -> descriptive string
 *    A human readable string.
 *
 * All enabled features appear in either features_for_read or
 * features_for_write, but not both.
 *
 * To open a pool in read-only mode only the features listed in
 * features_for_read need to be supported.
 *
 * To open the pool in read-write mode features in both features_for_read and
 * features_for_write need to be supported.
 *
 * Some features may be required to read the ZAP objects containing feature
 * information. To allow software to check for compatibility with these features
 * before the pool is opened their names must be stored in the label in a
 * new "features_for_read" entry (note that features that are only required
 * to write to a pool never need to be stored in the label since the
 * features_for_write ZAP object can be read before the pool is written to).
 * To save space in the label features must be explicitly marked as needing to
 * be written to the label. Also, reference counts are not stored in the label,
 * instead any feature whose reference count drops to 0 is removed from the
 * label.
 *
 * Adding New Features
 * -------------------
 *
 * Features must be registered in zpool_feature_init() function in
 * zfeature_common.c using the zfeature_register() function. This function
 * has arguments to specify if the feature should be stored in the
 * features_for_read or features_for_write ZAP object and if it needs to be
 * written to the label when active.
 *
 * Once a feature is registered it will appear as a "feature@<feature name>"
 * property which can be set by an administrator. Feature implementors should
 * use the spa_feature_is_enabled() and spa_feature_is_active() functions to
 * query the state of a feature and the spa_feature_incr() and
 * spa_feature_decr() functions to change an enabled feature's reference count.
 * Reference counts may only be updated in the syncing context.
 *
 * Features may not perform enable-time initialization. Instead, any such
 * initialization should occur when the feature is first used. This design
 * enforces that on-disk changes be made only when features are used. Code
 * should only check if a feature is enabled using spa_feature_is_enabled(),
 * not by relying on any feature specific metadata existing. If a feature is
 * enabled, but the feature's metadata is not on disk yet then it should be
 * created as needed.
 *
 * As an example, consider the com.delphix:async_destroy feature. This feature
 * relies on the existence of a bptree in the MOS that store blocks for
 * asynchronous freeing. This bptree is not created when async_destroy is
 * enabled. Instead, when a dataset is destroyed spa_feature_is_enabled() is
 * called to check if async_destroy is enabled. If it is and the bptree object
 * does not exist yet, the bptree object is created as part of the dataset
 * destroy and async_destroy's reference count is incremented to indicate it
 * has made an on-disk format change. Later, after the destroyed dataset's
 * blocks have all been asynchronously freed there is no longer any use for the
 * bptree object, so it is destroyed and async_destroy's reference count is
 * decremented back to 0 to indicate that it has undone its on-disk format
 * changes.
 */

typedef enum {
	FEATURE_ACTION_INCR,
	FEATURE_ACTION_DECR,
} feature_action_t;

/*
 * Checks that the active features in the pool are supported by
 * this software.  Adds each unsupported feature (name -> description) to
 * the supplied nvlist.
 */
boolean_t
spa_features_check(spa_t *spa, boolean_t for_write,
    nvlist_t *unsup_feat, nvlist_t *enabled_feat)
{
	objset_t *os = spa->spa_meta_objset;
	boolean_t supported;
	zap_cursor_t zc;
	zap_attribute_t za;
	uint64_t obj = for_write ?
	    spa->spa_feat_for_write_obj : spa->spa_feat_for_read_obj;

	supported = B_TRUE;
	for (zap_cursor_init(&zc, os, obj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		ASSERT(za.za_integer_length == sizeof (uint64_t) &&
		    za.za_num_integers == 1);

		if (NULL != enabled_feat) {
			fnvlist_add_uint64(enabled_feat, za.za_name,
			    za.za_first_integer);
		}

		if (za.za_first_integer != 0 &&
		    !zfeature_is_supported(za.za_name)) {
			supported = B_FALSE;

			if (NULL != unsup_feat) {
				char *desc = "";
				char buf[MAXPATHLEN];

				if (zap_lookup(os, spa->spa_feat_desc_obj,
				    za.za_name, 1, sizeof (buf), buf) == 0)
					desc = buf;

				VERIFY(nvlist_add_string(unsup_feat, za.za_name,
				    desc) == 0);
			}
		}
	}
	zap_cursor_fini(&zc);

	return (supported);
}

/*
 * Use an in-memory cache of feature refcounts for quick retrieval.
 *
 * Note: well-designed features will not need to use this; they should
 * use spa_feature_is_enabled() and spa_feature_is_active() instead.
 * However, this is non-static for zdb, zhack, and spa_add_feature_stats().
 */
int
feature_get_refcount(spa_t *spa, zfeature_info_t *feature, uint64_t *res)
{
	ASSERT(VALID_FEATURE_FID(feature->fi_feature));
	if (spa->spa_feat_refcount_cache[feature->fi_feature] ==
	    SPA_FEATURE_DISABLED) {
		return (SET_ERROR(ENOTSUP));
	}
	*res = spa->spa_feat_refcount_cache[feature->fi_feature];
	return (0);
}

/*
 * Note: well-designed features will not need to use this; they should
 * use spa_feature_is_enabled() and spa_feature_is_active() instead.
 * However, this is non-static for zdb and zhack.
 */
int
feature_get_refcount_from_disk(spa_t *spa, zfeature_info_t *feature,
    uint64_t *res)
{
	int err;
	uint64_t refcount;
	uint64_t zapobj = (feature->fi_flags & ZFEATURE_FLAG_READONLY_COMPAT) ?
	    spa->spa_feat_for_write_obj : spa->spa_feat_for_read_obj;

	/*
	 * If the pool is currently being created, the feature objects may not
	 * have been allocated yet.  Act as though all features are disabled.
	 */
	if (zapobj == 0)
		return (SET_ERROR(ENOTSUP));

	err = zap_lookup(spa->spa_meta_objset, zapobj,
	    feature->fi_guid, sizeof (uint64_t), 1, &refcount);
	if (err != 0) {
		if (err == ENOENT)
			return (SET_ERROR(ENOTSUP));
		else
			return (err);
	}
	*res = refcount;
	return (0);
}


static int
feature_get_enabled_txg(spa_t *spa, zfeature_info_t *feature, uint64_t *res)
{
	uint64_t enabled_txg_obj = spa->spa_feat_enabled_txg_obj;

	ASSERT(zfeature_depends_on(feature->fi_feature,
	    SPA_FEATURE_ENABLED_TXG));

	if (!spa_feature_is_enabled(spa, feature->fi_feature)) {
		return (SET_ERROR(ENOTSUP));
	}

	ASSERT(enabled_txg_obj != 0);

	VERIFY0(zap_lookup(spa->spa_meta_objset, spa->spa_feat_enabled_txg_obj,
	    feature->fi_guid, sizeof (uint64_t), 1, res));

	return (0);
}

/*
 * This function is non-static for zhack; it should otherwise not be used
 * outside this file.
 */
void
feature_sync(spa_t *spa, zfeature_info_t *feature, uint64_t refcount,
    dmu_tx_t *tx)
{
	ASSERT(VALID_FEATURE_OR_NONE(feature->fi_feature));
	uint64_t zapobj = (feature->fi_flags & ZFEATURE_FLAG_READONLY_COMPAT) ?
	    spa->spa_feat_for_write_obj : spa->spa_feat_for_read_obj;

	VERIFY0(zap_update(spa->spa_meta_objset, zapobj, feature->fi_guid,
	    sizeof (uint64_t), 1, &refcount, tx));

	/*
	 * feature_sync is called directly from zhack, allowing the
	 * creation of arbitrary features whose fi_feature field may
	 * be greater than SPA_FEATURES. When called from zhack, the
	 * zfeature_info_t object's fi_feature field will be set to
	 * SPA_FEATURE_NONE.
	 */
	if (feature->fi_feature != SPA_FEATURE_NONE) {
		uint64_t *refcount_cache =
		    &spa->spa_feat_refcount_cache[feature->fi_feature];
#ifdef atomic_swap_64
		VERIFY3U(*refcount_cache, ==,
		    atomic_swap_64(refcount_cache, refcount));
#else
		*refcount_cache = refcount;
#endif
	}

	if (refcount == 0)
		spa_deactivate_mos_feature(spa, feature->fi_guid);
	else if (feature->fi_flags & ZFEATURE_FLAG_MOS)
		spa_activate_mos_feature(spa, feature->fi_guid, tx);
}

/*
 * This function is non-static for zhack; it should otherwise not be used
 * outside this file.
 */
void
feature_enable_sync(spa_t *spa, zfeature_info_t *feature, dmu_tx_t *tx)
{
	uint64_t initial_refcount =
	    (feature->fi_flags & ZFEATURE_FLAG_ACTIVATE_ON_ENABLE) ? 1 : 0;
	uint64_t zapobj = (feature->fi_flags & ZFEATURE_FLAG_READONLY_COMPAT) ?
	    spa->spa_feat_for_write_obj : spa->spa_feat_for_read_obj;

	ASSERT(0 != zapobj);
	ASSERT(zfeature_is_valid_guid(feature->fi_guid));
	ASSERT3U(spa_version(spa), >=, SPA_VERSION_FEATURES);

	/*
	 * If the feature is already enabled, ignore the request.
	 */
	if (zap_contains(spa->spa_meta_objset, zapobj, feature->fi_guid) == 0)
		return;

	for (int i = 0; feature->fi_depends[i] != SPA_FEATURE_NONE; i++)
		spa_feature_enable(spa, feature->fi_depends[i], tx);

	VERIFY0(zap_update(spa->spa_meta_objset, spa->spa_feat_desc_obj,
	    feature->fi_guid, 1, strlen(feature->fi_desc) + 1,
	    feature->fi_desc, tx));

	feature_sync(spa, feature, initial_refcount, tx);

	if (spa_feature_is_enabled(spa, SPA_FEATURE_ENABLED_TXG)) {
		uint64_t enabling_txg = dmu_tx_get_txg(tx);

		if (spa->spa_feat_enabled_txg_obj == 0ULL) {
			spa->spa_feat_enabled_txg_obj =
			    zap_create_link(spa->spa_meta_objset,
			    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
			    DMU_POOL_FEATURE_ENABLED_TXG, tx);
		}
		spa_feature_incr(spa, SPA_FEATURE_ENABLED_TXG, tx);

		VERIFY0(zap_add(spa->spa_meta_objset,
		    spa->spa_feat_enabled_txg_obj, feature->fi_guid,
		    sizeof (uint64_t), 1, &enabling_txg, tx));
	}
}

static void
feature_do_action(spa_t *spa, spa_feature_t fid, feature_action_t action,
    dmu_tx_t *tx)
{
	uint64_t refcount;
	zfeature_info_t *feature = &spa_feature_table[fid];
	uint64_t zapobj = (feature->fi_flags & ZFEATURE_FLAG_READONLY_COMPAT) ?
	    spa->spa_feat_for_write_obj : spa->spa_feat_for_read_obj;

	ASSERT(VALID_FEATURE_FID(fid));
	ASSERT(0 != zapobj);
	ASSERT(zfeature_is_valid_guid(feature->fi_guid));

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3U(spa_version(spa), >=, SPA_VERSION_FEATURES);

	VERIFY3U(feature_get_refcount(spa, feature, &refcount), !=, ENOTSUP);

	switch (action) {
	case FEATURE_ACTION_INCR:
		VERIFY3U(refcount, !=, UINT64_MAX);
		refcount++;
		break;
	case FEATURE_ACTION_DECR:
		VERIFY3U(refcount, !=, 0);
		refcount--;
		break;
	default:
		ASSERT(0);
		break;
	}

	feature_sync(spa, feature, refcount, tx);
}

void
spa_feature_create_zap_objects(spa_t *spa, dmu_tx_t *tx)
{
	/*
	 * We create feature flags ZAP objects in two instances: during pool
	 * creation and during pool upgrade.
	 */
	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)) || (!spa->spa_sync_on &&
	    tx->tx_txg == TXG_INITIAL));

	spa->spa_feat_for_read_obj = zap_create_link(spa->spa_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FEATURES_FOR_READ, tx);
	spa->spa_feat_for_write_obj = zap_create_link(spa->spa_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FEATURES_FOR_WRITE, tx);
	spa->spa_feat_desc_obj = zap_create_link(spa->spa_meta_objset,
	    DMU_OTN_ZAP_METADATA, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_FEATURE_DESCRIPTIONS, tx);
}

/*
 * Enable any required dependencies, then enable the requested feature.
 */
void
spa_feature_enable(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	ASSERT3U(spa_version(spa), >=, SPA_VERSION_FEATURES);
	ASSERT(VALID_FEATURE_FID(fid));
	feature_enable_sync(spa, &spa_feature_table[fid], tx);
}

void
spa_feature_incr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	feature_do_action(spa, fid, FEATURE_ACTION_INCR, tx);
}

void
spa_feature_decr(spa_t *spa, spa_feature_t fid, dmu_tx_t *tx)
{
	feature_do_action(spa, fid, FEATURE_ACTION_DECR, tx);
}

boolean_t
spa_feature_is_enabled(spa_t *spa, spa_feature_t fid)
{
	int err;
	uint64_t refcount;

	ASSERT(VALID_FEATURE_FID(fid));
	if (spa_version(spa) < SPA_VERSION_FEATURES)
		return (B_FALSE);

	err = feature_get_refcount(spa, &spa_feature_table[fid], &refcount);
	ASSERT(err == 0 || err == ENOTSUP);
	return (err == 0);
}

boolean_t
spa_feature_is_active(spa_t *spa, spa_feature_t fid)
{
	int err;
	uint64_t refcount;

	ASSERT(VALID_FEATURE_FID(fid));
	if (spa_version(spa) < SPA_VERSION_FEATURES)
		return (B_FALSE);

	err = feature_get_refcount(spa, &spa_feature_table[fid], &refcount);
	ASSERT(err == 0 || err == ENOTSUP);
	return (err == 0 && refcount > 0);
}

/*
 * For the feature specified by fid (which must depend on
 * SPA_FEATURE_ENABLED_TXG), return the TXG at which it was enabled in the
 * OUT txg argument.
 *
 * Returns B_TRUE if the feature is enabled, in which case txg will be filled
 * with the transaction group in which the specified feature was enabled.
 * Returns B_FALSE otherwise (i.e. if the feature is not enabled).
 */
boolean_t
spa_feature_enabled_txg(spa_t *spa, spa_feature_t fid, uint64_t *txg)
{
	int err;

	ASSERT(VALID_FEATURE_FID(fid));
	if (spa_version(spa) < SPA_VERSION_FEATURES)
		return (B_FALSE);

	err = feature_get_enabled_txg(spa, &spa_feature_table[fid], txg);
	ASSERT(err == 0 || err == ENOTSUP);

	return (err == 0);
}
