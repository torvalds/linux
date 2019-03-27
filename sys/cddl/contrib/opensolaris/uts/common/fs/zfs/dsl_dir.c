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
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2014 Joyent, Inc. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_impl.h>
#include <sys/spa.h>
#include <sys/metaslab.h>
#include <sys/zap.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/sunddi.h>
#include <sys/zvol.h>
#ifdef _KERNEL
#include <sys/zfs_vfsops.h>
#endif
#include <sys/zfeature.h>
#include <sys/policy.h>
#include <sys/zfs_znode.h>
#include "zfs_namecheck.h"
#include "zfs_prop.h"

/*
 * Filesystem and Snapshot Limits
 * ------------------------------
 *
 * These limits are used to restrict the number of filesystems and/or snapshots
 * that can be created at a given level in the tree or below. A typical
 * use-case is with a delegated dataset where the administrator wants to ensure
 * that a user within the zone is not creating too many additional filesystems
 * or snapshots, even though they're not exceeding their space quota.
 *
 * The filesystem and snapshot counts are stored as extensible properties. This
 * capability is controlled by a feature flag and must be enabled to be used.
 * Once enabled, the feature is not active until the first limit is set. At
 * that point, future operations to create/destroy filesystems or snapshots
 * will validate and update the counts.
 *
 * Because the count properties will not exist before the feature is active,
 * the counts are updated when a limit is first set on an uninitialized
 * dsl_dir node in the tree (The filesystem/snapshot count on a node includes
 * all of the nested filesystems/snapshots. Thus, a new leaf node has a
 * filesystem count of 0 and a snapshot count of 0. Non-existent filesystem and
 * snapshot count properties on a node indicate uninitialized counts on that
 * node.) When first setting a limit on an uninitialized node, the code starts
 * at the filesystem with the new limit and descends into all sub-filesystems
 * to add the count properties.
 *
 * In practice this is lightweight since a limit is typically set when the
 * filesystem is created and thus has no children. Once valid, changing the
 * limit value won't require a re-traversal since the counts are already valid.
 * When recursively fixing the counts, if a node with a limit is encountered
 * during the descent, the counts are known to be valid and there is no need to
 * descend into that filesystem's children. The counts on filesystems above the
 * one with the new limit will still be uninitialized, unless a limit is
 * eventually set on one of those filesystems. The counts are always recursively
 * updated when a limit is set on a dataset, unless there is already a limit.
 * When a new limit value is set on a filesystem with an existing limit, it is
 * possible for the new limit to be less than the current count at that level
 * since a user who can change the limit is also allowed to exceed the limit.
 *
 * Once the feature is active, then whenever a filesystem or snapshot is
 * created, the code recurses up the tree, validating the new count against the
 * limit at each initialized level. In practice, most levels will not have a
 * limit set. If there is a limit at any initialized level up the tree, the
 * check must pass or the creation will fail. Likewise, when a filesystem or
 * snapshot is destroyed, the counts are recursively adjusted all the way up
 * the initizized nodes in the tree. Renaming a filesystem into different point
 * in the tree will first validate, then update the counts on each branch up to
 * the common ancestor. A receive will also validate the counts and then update
 * them.
 *
 * An exception to the above behavior is that the limit is not enforced if the
 * user has permission to modify the limit. This is primarily so that
 * recursive snapshots in the global zone always work. We want to prevent a
 * denial-of-service in which a lower level delegated dataset could max out its
 * limit and thus block recursive snapshots from being taken in the global zone.
 * Because of this, it is possible for the snapshot count to be over the limit
 * and snapshots taken in the global zone could cause a lower level dataset to
 * hit or exceed its limit. The administrator taking the global zone recursive
 * snapshot should be aware of this side-effect and behave accordingly.
 * For consistency, the filesystem limit is also not enforced if the user can
 * modify the limit.
 *
 * The filesystem and snapshot limits are validated by dsl_fs_ss_limit_check()
 * and updated by dsl_fs_ss_count_adjust(). A new limit value is setup in
 * dsl_dir_activate_fs_ss_limit() and the counts are adjusted, if necessary, by
 * dsl_dir_init_fs_ss_count().
 *
 * There is a special case when we receive a filesystem that already exists. In
 * this case a temporary clone name of %X is created (see dmu_recv_begin). We
 * never update the filesystem counts for temporary clones.
 *
 * Likewise, we do not update the snapshot counts for temporary snapshots,
 * such as those created by zfs diff.
 */

extern inline dsl_dir_phys_t *dsl_dir_phys(dsl_dir_t *dd);

static uint64_t dsl_dir_space_towrite(dsl_dir_t *dd);

typedef struct ddulrt_arg {
	dsl_dir_t	*ddulrta_dd;
	uint64_t	ddlrta_txg;
} ddulrt_arg_t;

static void
dsl_dir_evict_async(void *dbu)
{
	dsl_dir_t *dd = dbu;
	dsl_pool_t *dp = dd->dd_pool;
	int t;

	dd->dd_dbuf = NULL;

	for (t = 0; t < TXG_SIZE; t++) {
		ASSERT(!txg_list_member(&dp->dp_dirty_dirs, dd, t));
		ASSERT(dd->dd_tempreserved[t] == 0);
		ASSERT(dd->dd_space_towrite[t] == 0);
	}

	if (dd->dd_parent)
		dsl_dir_async_rele(dd->dd_parent, dd);

	spa_async_close(dd->dd_pool->dp_spa, dd);

	dsl_prop_fini(dd);
	mutex_destroy(&dd->dd_lock);
	kmem_free(dd, sizeof (dsl_dir_t));
}

int
dsl_dir_hold_obj(dsl_pool_t *dp, uint64_t ddobj,
    const char *tail, void *tag, dsl_dir_t **ddp)
{
	dmu_buf_t *dbuf;
	dsl_dir_t *dd;
	int err;

	ASSERT(dsl_pool_config_held(dp));

	err = dmu_bonus_hold(dp->dp_meta_objset, ddobj, tag, &dbuf);
	if (err != 0)
		return (err);
	dd = dmu_buf_get_user(dbuf);
#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(dbuf, &doi);
		ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_DSL_DIR);
		ASSERT3U(doi.doi_bonus_size, >=, sizeof (dsl_dir_phys_t));
	}
#endif
	if (dd == NULL) {
		dsl_dir_t *winner;

		dd = kmem_zalloc(sizeof (dsl_dir_t), KM_SLEEP);
		dd->dd_object = ddobj;
		dd->dd_dbuf = dbuf;
		dd->dd_pool = dp;
		mutex_init(&dd->dd_lock, NULL, MUTEX_DEFAULT, NULL);
		dsl_prop_init(dd);

		dsl_dir_snap_cmtime_update(dd);

		if (dsl_dir_phys(dd)->dd_parent_obj) {
			err = dsl_dir_hold_obj(dp,
			    dsl_dir_phys(dd)->dd_parent_obj, NULL, dd,
			    &dd->dd_parent);
			if (err != 0)
				goto errout;
			if (tail) {
#ifdef ZFS_DEBUG
				uint64_t foundobj;

				err = zap_lookup(dp->dp_meta_objset,
				    dsl_dir_phys(dd->dd_parent)->
				    dd_child_dir_zapobj, tail,
				    sizeof (foundobj), 1, &foundobj);
				ASSERT(err || foundobj == ddobj);
#endif
				(void) strcpy(dd->dd_myname, tail);
			} else {
				err = zap_value_search(dp->dp_meta_objset,
				    dsl_dir_phys(dd->dd_parent)->
				    dd_child_dir_zapobj,
				    ddobj, 0, dd->dd_myname);
			}
			if (err != 0)
				goto errout;
		} else {
			(void) strcpy(dd->dd_myname, spa_name(dp->dp_spa));
		}

		if (dsl_dir_is_clone(dd)) {
			dmu_buf_t *origin_bonus;
			dsl_dataset_phys_t *origin_phys;

			/*
			 * We can't open the origin dataset, because
			 * that would require opening this dsl_dir.
			 * Just look at its phys directly instead.
			 */
			err = dmu_bonus_hold(dp->dp_meta_objset,
			    dsl_dir_phys(dd)->dd_origin_obj, FTAG,
			    &origin_bonus);
			if (err != 0)
				goto errout;
			origin_phys = origin_bonus->db_data;
			dd->dd_origin_txg =
			    origin_phys->ds_creation_txg;
			dmu_buf_rele(origin_bonus, FTAG);
		}

		dmu_buf_init_user(&dd->dd_dbu, NULL, dsl_dir_evict_async,
		    &dd->dd_dbuf);
		winner = dmu_buf_set_user_ie(dbuf, &dd->dd_dbu);
		if (winner != NULL) {
			if (dd->dd_parent)
				dsl_dir_rele(dd->dd_parent, dd);
			dsl_prop_fini(dd);
			mutex_destroy(&dd->dd_lock);
			kmem_free(dd, sizeof (dsl_dir_t));
			dd = winner;
		} else {
			spa_open_ref(dp->dp_spa, dd);
		}
	}

	/*
	 * The dsl_dir_t has both open-to-close and instantiate-to-evict
	 * holds on the spa.  We need the open-to-close holds because
	 * otherwise the spa_refcnt wouldn't change when we open a
	 * dir which the spa also has open, so we could incorrectly
	 * think it was OK to unload/export/destroy the pool.  We need
	 * the instantiate-to-evict hold because the dsl_dir_t has a
	 * pointer to the dd_pool, which has a pointer to the spa_t.
	 */
	spa_open_ref(dp->dp_spa, tag);
	ASSERT3P(dd->dd_pool, ==, dp);
	ASSERT3U(dd->dd_object, ==, ddobj);
	ASSERT3P(dd->dd_dbuf, ==, dbuf);
	*ddp = dd;
	return (0);

errout:
	if (dd->dd_parent)
		dsl_dir_rele(dd->dd_parent, dd);
	dsl_prop_fini(dd);
	mutex_destroy(&dd->dd_lock);
	kmem_free(dd, sizeof (dsl_dir_t));
	dmu_buf_rele(dbuf, tag);
	return (err);
}

void
dsl_dir_rele(dsl_dir_t *dd, void *tag)
{
	dprintf_dd(dd, "%s\n", "");
	spa_close(dd->dd_pool->dp_spa, tag);
	dmu_buf_rele(dd->dd_dbuf, tag);
}

/*
 * Remove a reference to the given dsl dir that is being asynchronously
 * released.  Async releases occur from a taskq performing eviction of
 * dsl datasets and dirs.  This process is identical to a normal release
 * with the exception of using the async API for releasing the reference on
 * the spa.
 */
void
dsl_dir_async_rele(dsl_dir_t *dd, void *tag)
{
	dprintf_dd(dd, "%s\n", "");
	spa_async_close(dd->dd_pool->dp_spa, tag);
	dmu_buf_rele(dd->dd_dbuf, tag);
}

/* buf must be at least ZFS_MAX_DATASET_NAME_LEN bytes */
void
dsl_dir_name(dsl_dir_t *dd, char *buf)
{
	if (dd->dd_parent) {
		dsl_dir_name(dd->dd_parent, buf);
		VERIFY3U(strlcat(buf, "/", ZFS_MAX_DATASET_NAME_LEN), <,
		    ZFS_MAX_DATASET_NAME_LEN);
	} else {
		buf[0] = '\0';
	}
	if (!MUTEX_HELD(&dd->dd_lock)) {
		/*
		 * recursive mutex so that we can use
		 * dprintf_dd() with dd_lock held
		 */
		mutex_enter(&dd->dd_lock);
		VERIFY3U(strlcat(buf, dd->dd_myname, ZFS_MAX_DATASET_NAME_LEN),
		    <, ZFS_MAX_DATASET_NAME_LEN);
		mutex_exit(&dd->dd_lock);
	} else {
		VERIFY3U(strlcat(buf, dd->dd_myname, ZFS_MAX_DATASET_NAME_LEN),
		    <, ZFS_MAX_DATASET_NAME_LEN);
	}
}

/* Calculate name length, avoiding all the strcat calls of dsl_dir_name */
int
dsl_dir_namelen(dsl_dir_t *dd)
{
	int result = 0;

	if (dd->dd_parent) {
		/* parent's name + 1 for the "/" */
		result = dsl_dir_namelen(dd->dd_parent) + 1;
	}

	if (!MUTEX_HELD(&dd->dd_lock)) {
		/* see dsl_dir_name */
		mutex_enter(&dd->dd_lock);
		result += strlen(dd->dd_myname);
		mutex_exit(&dd->dd_lock);
	} else {
		result += strlen(dd->dd_myname);
	}

	return (result);
}

static int
getcomponent(const char *path, char *component, const char **nextp)
{
	char *p;

	if ((path == NULL) || (path[0] == '\0'))
		return (SET_ERROR(ENOENT));
	/* This would be a good place to reserve some namespace... */
	p = strpbrk(path, "/@");
	if (p && (p[1] == '/' || p[1] == '@')) {
		/* two separators in a row */
		return (SET_ERROR(EINVAL));
	}
	if (p == NULL || p == path) {
		/*
		 * if the first thing is an @ or /, it had better be an
		 * @ and it had better not have any more ats or slashes,
		 * and it had better have something after the @.
		 */
		if (p != NULL &&
		    (p[0] != '@' || strpbrk(path+1, "/@") || p[1] == '\0'))
			return (SET_ERROR(EINVAL));
		if (strlen(path) >= ZFS_MAX_DATASET_NAME_LEN)
			return (SET_ERROR(ENAMETOOLONG));
		(void) strcpy(component, path);
		p = NULL;
	} else if (p[0] == '/') {
		if (p - path >= ZFS_MAX_DATASET_NAME_LEN)
			return (SET_ERROR(ENAMETOOLONG));
		(void) strncpy(component, path, p - path);
		component[p - path] = '\0';
		p++;
	} else if (p[0] == '@') {
		/*
		 * if the next separator is an @, there better not be
		 * any more slashes.
		 */
		if (strchr(path, '/'))
			return (SET_ERROR(EINVAL));
		if (p - path >= ZFS_MAX_DATASET_NAME_LEN)
			return (SET_ERROR(ENAMETOOLONG));
		(void) strncpy(component, path, p - path);
		component[p - path] = '\0';
	} else {
		panic("invalid p=%p", (void *)p);
	}
	*nextp = p;
	return (0);
}

/*
 * Return the dsl_dir_t, and possibly the last component which couldn't
 * be found in *tail.  The name must be in the specified dsl_pool_t.  This
 * thread must hold the dp_config_rwlock for the pool.  Returns NULL if the
 * path is bogus, or if tail==NULL and we couldn't parse the whole name.
 * (*tail)[0] == '@' means that the last component is a snapshot.
 */
int
dsl_dir_hold(dsl_pool_t *dp, const char *name, void *tag,
    dsl_dir_t **ddp, const char **tailp)
{
	char buf[ZFS_MAX_DATASET_NAME_LEN];
	const char *spaname, *next, *nextnext = NULL;
	int err;
	dsl_dir_t *dd;
	uint64_t ddobj;

	err = getcomponent(name, buf, &next);
	if (err != 0)
		return (err);

	/* Make sure the name is in the specified pool. */
	spaname = spa_name(dp->dp_spa);
	if (strcmp(buf, spaname) != 0)
		return (SET_ERROR(EXDEV));

	ASSERT(dsl_pool_config_held(dp));

	err = dsl_dir_hold_obj(dp, dp->dp_root_dir_obj, NULL, tag, &dd);
	if (err != 0) {
		return (err);
	}

	while (next != NULL) {
		dsl_dir_t *child_dd;
		err = getcomponent(next, buf, &nextnext);
		if (err != 0)
			break;
		ASSERT(next[0] != '\0');
		if (next[0] == '@')
			break;
		dprintf("looking up %s in obj%lld\n",
		    buf, dsl_dir_phys(dd)->dd_child_dir_zapobj);

		err = zap_lookup(dp->dp_meta_objset,
		    dsl_dir_phys(dd)->dd_child_dir_zapobj,
		    buf, sizeof (ddobj), 1, &ddobj);
		if (err != 0) {
			if (err == ENOENT)
				err = 0;
			break;
		}

		err = dsl_dir_hold_obj(dp, ddobj, buf, tag, &child_dd);
		if (err != 0)
			break;
		dsl_dir_rele(dd, tag);
		dd = child_dd;
		next = nextnext;
	}

	if (err != 0) {
		dsl_dir_rele(dd, tag);
		return (err);
	}

	/*
	 * It's an error if there's more than one component left, or
	 * tailp==NULL and there's any component left.
	 */
	if (next != NULL &&
	    (tailp == NULL || (nextnext && nextnext[0] != '\0'))) {
		/* bad path name */
		dsl_dir_rele(dd, tag);
		dprintf("next=%p (%s) tail=%p\n", next, next?next:"", tailp);
		err = SET_ERROR(ENOENT);
	}
	if (tailp != NULL)
		*tailp = next;
	*ddp = dd;
	return (err);
}

/*
 * If the counts are already initialized for this filesystem and its
 * descendants then do nothing, otherwise initialize the counts.
 *
 * The counts on this filesystem, and those below, may be uninitialized due to
 * either the use of a pre-existing pool which did not support the
 * filesystem/snapshot limit feature, or one in which the feature had not yet
 * been enabled.
 *
 * Recursively descend the filesystem tree and update the filesystem/snapshot
 * counts on each filesystem below, then update the cumulative count on the
 * current filesystem. If the filesystem already has a count set on it,
 * then we know that its counts, and the counts on the filesystems below it,
 * are already correct, so we don't have to update this filesystem.
 */
static void
dsl_dir_init_fs_ss_count(dsl_dir_t *dd, dmu_tx_t *tx)
{
	uint64_t my_fs_cnt = 0;
	uint64_t my_ss_cnt = 0;
	dsl_pool_t *dp = dd->dd_pool;
	objset_t *os = dp->dp_meta_objset;
	zap_cursor_t *zc;
	zap_attribute_t *za;
	dsl_dataset_t *ds;

	ASSERT(spa_feature_is_active(dp->dp_spa, SPA_FEATURE_FS_SS_LIMIT));
	ASSERT(dsl_pool_config_held(dp));
	ASSERT(dmu_tx_is_syncing(tx));

	dsl_dir_zapify(dd, tx);

	/*
	 * If the filesystem count has already been initialized then we
	 * don't need to recurse down any further.
	 */
	if (zap_contains(os, dd->dd_object, DD_FIELD_FILESYSTEM_COUNT) == 0)
		return;

	zc = kmem_alloc(sizeof (zap_cursor_t), KM_SLEEP);
	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);

	/* Iterate my child dirs */
	for (zap_cursor_init(zc, os, dsl_dir_phys(dd)->dd_child_dir_zapobj);
	    zap_cursor_retrieve(zc, za) == 0; zap_cursor_advance(zc)) {
		dsl_dir_t *chld_dd;
		uint64_t count;

		VERIFY0(dsl_dir_hold_obj(dp, za->za_first_integer, NULL, FTAG,
		    &chld_dd));

		/*
		 * Ignore hidden ($FREE, $MOS & $ORIGIN) objsets and
		 * temporary datasets.
		 */
		if (chld_dd->dd_myname[0] == '$' ||
		    chld_dd->dd_myname[0] == '%') {
			dsl_dir_rele(chld_dd, FTAG);
			continue;
		}

		my_fs_cnt++;	/* count this child */

		dsl_dir_init_fs_ss_count(chld_dd, tx);

		VERIFY0(zap_lookup(os, chld_dd->dd_object,
		    DD_FIELD_FILESYSTEM_COUNT, sizeof (count), 1, &count));
		my_fs_cnt += count;
		VERIFY0(zap_lookup(os, chld_dd->dd_object,
		    DD_FIELD_SNAPSHOT_COUNT, sizeof (count), 1, &count));
		my_ss_cnt += count;

		dsl_dir_rele(chld_dd, FTAG);
	}
	zap_cursor_fini(zc);
	/* Count my snapshots (we counted children's snapshots above) */
	VERIFY0(dsl_dataset_hold_obj(dd->dd_pool,
	    dsl_dir_phys(dd)->dd_head_dataset_obj, FTAG, &ds));

	for (zap_cursor_init(zc, os, dsl_dataset_phys(ds)->ds_snapnames_zapobj);
	    zap_cursor_retrieve(zc, za) == 0;
	    zap_cursor_advance(zc)) {
		/* Don't count temporary snapshots */
		if (za->za_name[0] != '%')
			my_ss_cnt++;
	}
	zap_cursor_fini(zc);

	dsl_dataset_rele(ds, FTAG);

	kmem_free(zc, sizeof (zap_cursor_t));
	kmem_free(za, sizeof (zap_attribute_t));

	/* we're in a sync task, update counts */
	dmu_buf_will_dirty(dd->dd_dbuf, tx);
	VERIFY0(zap_add(os, dd->dd_object, DD_FIELD_FILESYSTEM_COUNT,
	    sizeof (my_fs_cnt), 1, &my_fs_cnt, tx));
	VERIFY0(zap_add(os, dd->dd_object, DD_FIELD_SNAPSHOT_COUNT,
	    sizeof (my_ss_cnt), 1, &my_ss_cnt, tx));
}

static int
dsl_dir_actv_fs_ss_limit_check(void *arg, dmu_tx_t *tx)
{
	char *ddname = (char *)arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	dsl_dir_t *dd;
	int error;

	error = dsl_dataset_hold(dp, ddname, FTAG, &ds);
	if (error != 0)
		return (error);

	if (!spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_FS_SS_LIMIT)) {
		dsl_dataset_rele(ds, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	dd = ds->ds_dir;
	if (spa_feature_is_active(dp->dp_spa, SPA_FEATURE_FS_SS_LIMIT) &&
	    dsl_dir_is_zapified(dd) &&
	    zap_contains(dp->dp_meta_objset, dd->dd_object,
	    DD_FIELD_FILESYSTEM_COUNT) == 0) {
		dsl_dataset_rele(ds, FTAG);
		return (SET_ERROR(EALREADY));
	}

	dsl_dataset_rele(ds, FTAG);
	return (0);
}

static void
dsl_dir_actv_fs_ss_limit_sync(void *arg, dmu_tx_t *tx)
{
	char *ddname = (char *)arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	spa_t *spa;

	VERIFY0(dsl_dataset_hold(dp, ddname, FTAG, &ds));

	spa = dsl_dataset_get_spa(ds);

	if (!spa_feature_is_active(spa, SPA_FEATURE_FS_SS_LIMIT)) {
		/*
		 * Since the feature was not active and we're now setting a
		 * limit, increment the feature-active counter so that the
		 * feature becomes active for the first time.
		 *
		 * We are already in a sync task so we can update the MOS.
		 */
		spa_feature_incr(spa, SPA_FEATURE_FS_SS_LIMIT, tx);
	}

	/*
	 * Since we are now setting a non-UINT64_MAX limit on the filesystem,
	 * we need to ensure the counts are correct. Descend down the tree from
	 * this point and update all of the counts to be accurate.
	 */
	dsl_dir_init_fs_ss_count(ds->ds_dir, tx);

	dsl_dataset_rele(ds, FTAG);
}

/*
 * Make sure the feature is enabled and activate it if necessary.
 * Since we're setting a limit, ensure the on-disk counts are valid.
 * This is only called by the ioctl path when setting a limit value.
 *
 * We do not need to validate the new limit, since users who can change the
 * limit are also allowed to exceed the limit.
 */
int
dsl_dir_activate_fs_ss_limit(const char *ddname)
{
	int error;

	error = dsl_sync_task(ddname, dsl_dir_actv_fs_ss_limit_check,
	    dsl_dir_actv_fs_ss_limit_sync, (void *)ddname, 0,
	    ZFS_SPACE_CHECK_RESERVED);

	if (error == EALREADY)
		error = 0;

	return (error);
}

/*
 * Used to determine if the filesystem_limit or snapshot_limit should be
 * enforced. We allow the limit to be exceeded if the user has permission to
 * write the property value. We pass in the creds that we got in the open
 * context since we will always be the GZ root in syncing context. We also have
 * to handle the case where we are allowed to change the limit on the current
 * dataset, but there may be another limit in the tree above.
 *
 * We can never modify these two properties within a non-global zone. In
 * addition, the other checks are modeled on zfs_secpolicy_write_perms. We
 * can't use that function since we are already holding the dp_config_rwlock.
 * In addition, we already have the dd and dealing with snapshots is simplified
 * in this code.
 */

typedef enum {
	ENFORCE_ALWAYS,
	ENFORCE_NEVER,
	ENFORCE_ABOVE
} enforce_res_t;

static enforce_res_t
dsl_enforce_ds_ss_limits(dsl_dir_t *dd, zfs_prop_t prop, cred_t *cr)
{
	enforce_res_t enforce = ENFORCE_ALWAYS;
	uint64_t obj;
	dsl_dataset_t *ds;
	uint64_t zoned;

	ASSERT(prop == ZFS_PROP_FILESYSTEM_LIMIT ||
	    prop == ZFS_PROP_SNAPSHOT_LIMIT);

#ifdef _KERNEL
#ifdef __FreeBSD__
	if (jailed(cr))
#else
	if (crgetzoneid(cr) != GLOBAL_ZONEID)
#endif
		return (ENFORCE_ALWAYS);

	if (secpolicy_zfs(cr) == 0)
		return (ENFORCE_NEVER);
#endif

	if ((obj = dsl_dir_phys(dd)->dd_head_dataset_obj) == 0)
		return (ENFORCE_ALWAYS);

	ASSERT(dsl_pool_config_held(dd->dd_pool));

	if (dsl_dataset_hold_obj(dd->dd_pool, obj, FTAG, &ds) != 0)
		return (ENFORCE_ALWAYS);

	if (dsl_prop_get_ds(ds, "zoned", 8, 1, &zoned, NULL) || zoned) {
		/* Only root can access zoned fs's from the GZ */
		enforce = ENFORCE_ALWAYS;
	} else {
		if (dsl_deleg_access_impl(ds, zfs_prop_to_name(prop), cr) == 0)
			enforce = ENFORCE_ABOVE;
	}

	dsl_dataset_rele(ds, FTAG);
	return (enforce);
}

static void
dsl_dir_update_last_remap_txg_sync(void *varg, dmu_tx_t *tx)
{
	ddulrt_arg_t *arg = varg;
	uint64_t last_remap_txg;
	dsl_dir_t *dd = arg->ddulrta_dd;
	objset_t *mos = dd->dd_pool->dp_meta_objset;

	dsl_dir_zapify(dd, tx);
	if (zap_lookup(mos, dd->dd_object, DD_FIELD_LAST_REMAP_TXG,
	    sizeof (last_remap_txg), 1, &last_remap_txg) != 0 ||
	    last_remap_txg < arg->ddlrta_txg) {
		VERIFY0(zap_update(mos, dd->dd_object, DD_FIELD_LAST_REMAP_TXG,
		    sizeof (arg->ddlrta_txg), 1, &arg->ddlrta_txg, tx));
	}
}

int
dsl_dir_update_last_remap_txg(dsl_dir_t *dd, uint64_t txg)
{
	ddulrt_arg_t arg;
	arg.ddulrta_dd = dd;
	arg.ddlrta_txg = txg;

	return (dsl_sync_task(spa_name(dd->dd_pool->dp_spa),
	    NULL, dsl_dir_update_last_remap_txg_sync, &arg,
	    1, ZFS_SPACE_CHECK_RESERVED));
}

/*
 * Check if adding additional child filesystem(s) would exceed any filesystem
 * limits or adding additional snapshot(s) would exceed any snapshot limits.
 * The prop argument indicates which limit to check.
 *
 * Note that all filesystem limits up to the root (or the highest
 * initialized) filesystem or the given ancestor must be satisfied.
 */
int
dsl_fs_ss_limit_check(dsl_dir_t *dd, uint64_t delta, zfs_prop_t prop,
    dsl_dir_t *ancestor, cred_t *cr)
{
	objset_t *os = dd->dd_pool->dp_meta_objset;
	uint64_t limit, count;
	char *count_prop;
	enforce_res_t enforce;
	int err = 0;

	ASSERT(dsl_pool_config_held(dd->dd_pool));
	ASSERT(prop == ZFS_PROP_FILESYSTEM_LIMIT ||
	    prop == ZFS_PROP_SNAPSHOT_LIMIT);

	/*
	 * If we're allowed to change the limit, don't enforce the limit
	 * e.g. this can happen if a snapshot is taken by an administrative
	 * user in the global zone (i.e. a recursive snapshot by root).
	 * However, we must handle the case of delegated permissions where we
	 * are allowed to change the limit on the current dataset, but there
	 * is another limit in the tree above.
	 */
	enforce = dsl_enforce_ds_ss_limits(dd, prop, cr);
	if (enforce == ENFORCE_NEVER)
		return (0);

	/*
	 * e.g. if renaming a dataset with no snapshots, count adjustment
	 * is 0.
	 */
	if (delta == 0)
		return (0);

	if (prop == ZFS_PROP_SNAPSHOT_LIMIT) {
		/*
		 * We don't enforce the limit for temporary snapshots. This is
		 * indicated by a NULL cred_t argument.
		 */
		if (cr == NULL)
			return (0);

		count_prop = DD_FIELD_SNAPSHOT_COUNT;
	} else {
		count_prop = DD_FIELD_FILESYSTEM_COUNT;
	}

	/*
	 * If an ancestor has been provided, stop checking the limit once we
	 * hit that dir. We need this during rename so that we don't overcount
	 * the check once we recurse up to the common ancestor.
	 */
	if (ancestor == dd)
		return (0);

	/*
	 * If we hit an uninitialized node while recursing up the tree, we can
	 * stop since we know there is no limit here (or above). The counts are
	 * not valid on this node and we know we won't touch this node's counts.
	 */
	if (!dsl_dir_is_zapified(dd) || zap_lookup(os, dd->dd_object,
	    count_prop, sizeof (count), 1, &count) == ENOENT)
		return (0);

	err = dsl_prop_get_dd(dd, zfs_prop_to_name(prop), 8, 1, &limit, NULL,
	    B_FALSE);
	if (err != 0)
		return (err);

	/* Is there a limit which we've hit? */
	if (enforce == ENFORCE_ALWAYS && (count + delta) > limit)
		return (SET_ERROR(EDQUOT));

	if (dd->dd_parent != NULL)
		err = dsl_fs_ss_limit_check(dd->dd_parent, delta, prop,
		    ancestor, cr);

	return (err);
}

/*
 * Adjust the filesystem or snapshot count for the specified dsl_dir_t and all
 * parents. When a new filesystem/snapshot is created, increment the count on
 * all parents, and when a filesystem/snapshot is destroyed, decrement the
 * count.
 */
void
dsl_fs_ss_count_adjust(dsl_dir_t *dd, int64_t delta, const char *prop,
    dmu_tx_t *tx)
{
	int err;
	objset_t *os = dd->dd_pool->dp_meta_objset;
	uint64_t count;

	ASSERT(dsl_pool_config_held(dd->dd_pool));
	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(strcmp(prop, DD_FIELD_FILESYSTEM_COUNT) == 0 ||
	    strcmp(prop, DD_FIELD_SNAPSHOT_COUNT) == 0);

	/*
	 * When we receive an incremental stream into a filesystem that already
	 * exists, a temporary clone is created.  We don't count this temporary
	 * clone, whose name begins with a '%'. We also ignore hidden ($FREE,
	 * $MOS & $ORIGIN) objsets.
	 */
	if ((dd->dd_myname[0] == '%' || dd->dd_myname[0] == '$') &&
	    strcmp(prop, DD_FIELD_FILESYSTEM_COUNT) == 0)
		return;

	/*
	 * e.g. if renaming a dataset with no snapshots, count adjustment is 0
	 */
	if (delta == 0)
		return;

	/*
	 * If we hit an uninitialized node while recursing up the tree, we can
	 * stop since we know the counts are not valid on this node and we
	 * know we shouldn't touch this node's counts. An uninitialized count
	 * on the node indicates that either the feature has not yet been
	 * activated or there are no limits on this part of the tree.
	 */
	if (!dsl_dir_is_zapified(dd) || (err = zap_lookup(os, dd->dd_object,
	    prop, sizeof (count), 1, &count)) == ENOENT)
		return;
	VERIFY0(err);

	count += delta;
	/* Use a signed verify to make sure we're not neg. */
	VERIFY3S(count, >=, 0);

	VERIFY0(zap_update(os, dd->dd_object, prop, sizeof (count), 1, &count,
	    tx));

	/* Roll up this additional count into our ancestors */
	if (dd->dd_parent != NULL)
		dsl_fs_ss_count_adjust(dd->dd_parent, delta, prop, tx);
}

uint64_t
dsl_dir_create_sync(dsl_pool_t *dp, dsl_dir_t *pds, const char *name,
    dmu_tx_t *tx)
{
	objset_t *mos = dp->dp_meta_objset;
	uint64_t ddobj;
	dsl_dir_phys_t *ddphys;
	dmu_buf_t *dbuf;

	ddobj = dmu_object_alloc(mos, DMU_OT_DSL_DIR, 0,
	    DMU_OT_DSL_DIR, sizeof (dsl_dir_phys_t), tx);
	if (pds) {
		VERIFY0(zap_add(mos, dsl_dir_phys(pds)->dd_child_dir_zapobj,
		    name, sizeof (uint64_t), 1, &ddobj, tx));
	} else {
		/* it's the root dir */
		VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ROOT_DATASET, sizeof (uint64_t), 1, &ddobj, tx));
	}
	VERIFY0(dmu_bonus_hold(mos, ddobj, FTAG, &dbuf));
	dmu_buf_will_dirty(dbuf, tx);
	ddphys = dbuf->db_data;

	ddphys->dd_creation_time = gethrestime_sec();
	if (pds) {
		ddphys->dd_parent_obj = pds->dd_object;

		/* update the filesystem counts */
		dsl_fs_ss_count_adjust(pds, 1, DD_FIELD_FILESYSTEM_COUNT, tx);
	}
	ddphys->dd_props_zapobj = zap_create(mos,
	    DMU_OT_DSL_PROPS, DMU_OT_NONE, 0, tx);
	ddphys->dd_child_dir_zapobj = zap_create(mos,
	    DMU_OT_DSL_DIR_CHILD_MAP, DMU_OT_NONE, 0, tx);
	if (spa_version(dp->dp_spa) >= SPA_VERSION_USED_BREAKDOWN)
		ddphys->dd_flags |= DD_FLAG_USED_BREAKDOWN;
	dmu_buf_rele(dbuf, FTAG);

	return (ddobj);
}

boolean_t
dsl_dir_is_clone(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_origin_obj &&
	    (dd->dd_pool->dp_origin_snap == NULL ||
	    dsl_dir_phys(dd)->dd_origin_obj !=
	    dd->dd_pool->dp_origin_snap->ds_object));
}


uint64_t
dsl_dir_get_used(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_used_bytes);
}

uint64_t
dsl_dir_get_compressed(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_compressed_bytes);
}

uint64_t
dsl_dir_get_quota(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_quota);
}

uint64_t
dsl_dir_get_reservation(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_reserved);
}

uint64_t
dsl_dir_get_compressratio(dsl_dir_t *dd)
{
	/* a fixed point number, 100x the ratio */
	return (dsl_dir_phys(dd)->dd_compressed_bytes == 0 ? 100 :
	    (dsl_dir_phys(dd)->dd_uncompressed_bytes * 100 /
	    dsl_dir_phys(dd)->dd_compressed_bytes));
}

uint64_t
dsl_dir_get_logicalused(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_uncompressed_bytes);
}

uint64_t
dsl_dir_get_usedsnap(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_used_breakdown[DD_USED_SNAP]);
}

uint64_t
dsl_dir_get_usedds(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_used_breakdown[DD_USED_HEAD]);
}

uint64_t
dsl_dir_get_usedrefreserv(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_used_breakdown[DD_USED_REFRSRV]);
}

uint64_t
dsl_dir_get_usedchild(dsl_dir_t *dd)
{
	return (dsl_dir_phys(dd)->dd_used_breakdown[DD_USED_CHILD] +
	    dsl_dir_phys(dd)->dd_used_breakdown[DD_USED_CHILD_RSRV]);
}

void
dsl_dir_get_origin(dsl_dir_t *dd, char *buf)
{
	dsl_dataset_t *ds;
	VERIFY0(dsl_dataset_hold_obj(dd->dd_pool,
	    dsl_dir_phys(dd)->dd_origin_obj, FTAG, &ds));

	dsl_dataset_name(ds, buf);

	dsl_dataset_rele(ds, FTAG);
}

int
dsl_dir_get_filesystem_count(dsl_dir_t *dd, uint64_t *count)
{
	if (dsl_dir_is_zapified(dd)) {
		objset_t *os = dd->dd_pool->dp_meta_objset;
		return (zap_lookup(os, dd->dd_object, DD_FIELD_FILESYSTEM_COUNT,
		    sizeof (*count), 1, count));
	} else {
		return (ENOENT);
	}
}

int
dsl_dir_get_snapshot_count(dsl_dir_t *dd, uint64_t *count)
{
	if (dsl_dir_is_zapified(dd)) {
		objset_t *os = dd->dd_pool->dp_meta_objset;
		return (zap_lookup(os, dd->dd_object, DD_FIELD_SNAPSHOT_COUNT,
		    sizeof (*count), 1, count));
	} else {
		return (ENOENT);
	}
}

int
dsl_dir_get_remaptxg(dsl_dir_t *dd, uint64_t *count)
{
	if (dsl_dir_is_zapified(dd)) {
		objset_t *os = dd->dd_pool->dp_meta_objset;
		return (zap_lookup(os, dd->dd_object, DD_FIELD_LAST_REMAP_TXG,
		    sizeof (*count), 1, count));
	} else {
		return (ENOENT);
	}
}

void
dsl_dir_stats(dsl_dir_t *dd, nvlist_t *nv)
{
	mutex_enter(&dd->dd_lock);
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_QUOTA,
	    dsl_dir_get_quota(dd));
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_RESERVATION,
	    dsl_dir_get_reservation(dd));
	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_LOGICALUSED,
	    dsl_dir_get_logicalused(dd));
	if (dsl_dir_phys(dd)->dd_flags & DD_FLAG_USED_BREAKDOWN) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDSNAP,
		    dsl_dir_get_usedsnap(dd));
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDDS,
		    dsl_dir_get_usedds(dd));
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDREFRESERV,
		    dsl_dir_get_usedrefreserv(dd));
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_USEDCHILD,
		    dsl_dir_get_usedchild(dd));
	}
	mutex_exit(&dd->dd_lock);

	uint64_t count;
	if (dsl_dir_get_filesystem_count(dd, &count) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_FILESYSTEM_COUNT,
		    count);
	}
	if (dsl_dir_get_snapshot_count(dd, &count) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_SNAPSHOT_COUNT,
		    count);
	}
	if (dsl_dir_get_remaptxg(dd, &count) == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_REMAPTXG,
		    count);
	}

	if (dsl_dir_is_clone(dd)) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dsl_dir_get_origin(dd, buf);
		dsl_prop_nvlist_add_string(nv, ZFS_PROP_ORIGIN, buf);
	}

}

void
dsl_dir_dirty(dsl_dir_t *dd, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dd->dd_pool;

	ASSERT(dsl_dir_phys(dd));

	if (txg_list_add(&dp->dp_dirty_dirs, dd, tx->tx_txg)) {
		/* up the hold count until we can be written out */
		dmu_buf_add_ref(dd->dd_dbuf, dd);
	}
}

static int64_t
parent_delta(dsl_dir_t *dd, uint64_t used, int64_t delta)
{
	uint64_t old_accounted = MAX(used, dsl_dir_phys(dd)->dd_reserved);
	uint64_t new_accounted =
	    MAX(used + delta, dsl_dir_phys(dd)->dd_reserved);
	return (new_accounted - old_accounted);
}

void
dsl_dir_sync(dsl_dir_t *dd, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));

	mutex_enter(&dd->dd_lock);
	ASSERT0(dd->dd_tempreserved[tx->tx_txg&TXG_MASK]);
	dprintf_dd(dd, "txg=%llu towrite=%lluK\n", tx->tx_txg,
	    dd->dd_space_towrite[tx->tx_txg&TXG_MASK] / 1024);
	dd->dd_space_towrite[tx->tx_txg&TXG_MASK] = 0;
	mutex_exit(&dd->dd_lock);

	/* release the hold from dsl_dir_dirty */
	dmu_buf_rele(dd->dd_dbuf, dd);
}

static uint64_t
dsl_dir_space_towrite(dsl_dir_t *dd)
{
	uint64_t space = 0;

	ASSERT(MUTEX_HELD(&dd->dd_lock));

	for (int i = 0; i < TXG_SIZE; i++) {
		space += dd->dd_space_towrite[i & TXG_MASK];
		ASSERT3U(dd->dd_space_towrite[i & TXG_MASK], >=, 0);
	}
	return (space);
}

/*
 * How much space would dd have available if ancestor had delta applied
 * to it?  If ondiskonly is set, we're only interested in what's
 * on-disk, not estimated pending changes.
 */
uint64_t
dsl_dir_space_available(dsl_dir_t *dd,
    dsl_dir_t *ancestor, int64_t delta, int ondiskonly)
{
	uint64_t parentspace, myspace, quota, used;

	/*
	 * If there are no restrictions otherwise, assume we have
	 * unlimited space available.
	 */
	quota = UINT64_MAX;
	parentspace = UINT64_MAX;

	if (dd->dd_parent != NULL) {
		parentspace = dsl_dir_space_available(dd->dd_parent,
		    ancestor, delta, ondiskonly);
	}

	mutex_enter(&dd->dd_lock);
	if (dsl_dir_phys(dd)->dd_quota != 0)
		quota = dsl_dir_phys(dd)->dd_quota;
	used = dsl_dir_phys(dd)->dd_used_bytes;
	if (!ondiskonly)
		used += dsl_dir_space_towrite(dd);

	if (dd->dd_parent == NULL) {
		uint64_t poolsize = dsl_pool_adjustedsize(dd->dd_pool,
		    ZFS_SPACE_CHECK_NORMAL);
		quota = MIN(quota, poolsize);
	}

	if (dsl_dir_phys(dd)->dd_reserved > used && parentspace != UINT64_MAX) {
		/*
		 * We have some space reserved, in addition to what our
		 * parent gave us.
		 */
		parentspace += dsl_dir_phys(dd)->dd_reserved - used;
	}

	if (dd == ancestor) {
		ASSERT(delta <= 0);
		ASSERT(used >= -delta);
		used += delta;
		if (parentspace != UINT64_MAX)
			parentspace -= delta;
	}

	if (used > quota) {
		/* over quota */
		myspace = 0;
	} else {
		/*
		 * the lesser of the space provided by our parent and
		 * the space left in our quota
		 */
		myspace = MIN(parentspace, quota - used);
	}

	mutex_exit(&dd->dd_lock);

	return (myspace);
}

struct tempreserve {
	list_node_t tr_node;
	dsl_dir_t *tr_ds;
	uint64_t tr_size;
};

static int
dsl_dir_tempreserve_impl(dsl_dir_t *dd, uint64_t asize, boolean_t netfree,
    boolean_t ignorequota, list_t *tr_list,
    dmu_tx_t *tx, boolean_t first)
{
	uint64_t txg = tx->tx_txg;
	uint64_t quota;
	struct tempreserve *tr;
	int retval = EDQUOT;
	uint64_t ref_rsrv = 0;

	ASSERT3U(txg, !=, 0);
	ASSERT3S(asize, >, 0);

	mutex_enter(&dd->dd_lock);

	/*
	 * Check against the dsl_dir's quota.  We don't add in the delta
	 * when checking for over-quota because they get one free hit.
	 */
	uint64_t est_inflight = dsl_dir_space_towrite(dd);
	for (int i = 0; i < TXG_SIZE; i++)
		est_inflight += dd->dd_tempreserved[i];
	uint64_t used_on_disk = dsl_dir_phys(dd)->dd_used_bytes;

	/*
	 * On the first iteration, fetch the dataset's used-on-disk and
	 * refreservation values. Also, if checkrefquota is set, test if
	 * allocating this space would exceed the dataset's refquota.
	 */
	if (first && tx->tx_objset) {
		int error;
		dsl_dataset_t *ds = tx->tx_objset->os_dsl_dataset;

		error = dsl_dataset_check_quota(ds, !netfree,
		    asize, est_inflight, &used_on_disk, &ref_rsrv);
		if (error != 0) {
			mutex_exit(&dd->dd_lock);
			return (error);
		}
	}

	/*
	 * If this transaction will result in a net free of space,
	 * we want to let it through.
	 */
	if (ignorequota || netfree || dsl_dir_phys(dd)->dd_quota == 0)
		quota = UINT64_MAX;
	else
		quota = dsl_dir_phys(dd)->dd_quota;

	/*
	 * Adjust the quota against the actual pool size at the root
	 * minus any outstanding deferred frees.
	 * To ensure that it's possible to remove files from a full
	 * pool without inducing transient overcommits, we throttle
	 * netfree transactions against a quota that is slightly larger,
	 * but still within the pool's allocation slop.  In cases where
	 * we're very close to full, this will allow a steady trickle of
	 * removes to get through.
	 */
	uint64_t deferred = 0;
	if (dd->dd_parent == NULL) {
		uint64_t avail = dsl_pool_unreserved_space(dd->dd_pool,
		    (netfree) ?
		    ZFS_SPACE_CHECK_RESERVED : ZFS_SPACE_CHECK_NORMAL);

		if (avail < quota) {
			quota = avail;
			retval = ENOSPC;
		}
	}

	/*
	 * If they are requesting more space, and our current estimate
	 * is over quota, they get to try again unless the actual
	 * on-disk is over quota and there are no pending changes (which
	 * may free up space for us).
	 */
	if (used_on_disk + est_inflight >= quota) {
		if (est_inflight > 0 || used_on_disk < quota ||
		    (retval == ENOSPC && used_on_disk < quota + deferred))
			retval = ERESTART;
		dprintf_dd(dd, "failing: used=%lluK inflight = %lluK "
		    "quota=%lluK tr=%lluK err=%d\n",
		    used_on_disk>>10, est_inflight>>10,
		    quota>>10, asize>>10, retval);
		mutex_exit(&dd->dd_lock);
		return (SET_ERROR(retval));
	}

	/* We need to up our estimated delta before dropping dd_lock */
	dd->dd_tempreserved[txg & TXG_MASK] += asize;

	uint64_t parent_rsrv = parent_delta(dd, used_on_disk + est_inflight,
	    asize - ref_rsrv);
	mutex_exit(&dd->dd_lock);

	tr = kmem_zalloc(sizeof (struct tempreserve), KM_SLEEP);
	tr->tr_ds = dd;
	tr->tr_size = asize;
	list_insert_tail(tr_list, tr);

	/* see if it's OK with our parent */
	if (dd->dd_parent != NULL && parent_rsrv != 0) {
		boolean_t ismos = (dsl_dir_phys(dd)->dd_head_dataset_obj == 0);

		return (dsl_dir_tempreserve_impl(dd->dd_parent,
		    parent_rsrv, netfree, ismos, tr_list, tx, B_FALSE));
	} else {
		return (0);
	}
}

/*
 * Reserve space in this dsl_dir, to be used in this tx's txg.
 * After the space has been dirtied (and dsl_dir_willuse_space()
 * has been called), the reservation should be canceled, using
 * dsl_dir_tempreserve_clear().
 */
int
dsl_dir_tempreserve_space(dsl_dir_t *dd, uint64_t lsize, uint64_t asize,
    boolean_t netfree, void **tr_cookiep, dmu_tx_t *tx)
{
	int err;
	list_t *tr_list;

	if (asize == 0) {
		*tr_cookiep = NULL;
		return (0);
	}

	tr_list = kmem_alloc(sizeof (list_t), KM_SLEEP);
	list_create(tr_list, sizeof (struct tempreserve),
	    offsetof(struct tempreserve, tr_node));
	ASSERT3S(asize, >, 0);

	err = arc_tempreserve_space(dd->dd_pool->dp_spa, lsize, tx->tx_txg);
	if (err == 0) {
		struct tempreserve *tr;

		tr = kmem_zalloc(sizeof (struct tempreserve), KM_SLEEP);
		tr->tr_size = lsize;
		list_insert_tail(tr_list, tr);
	} else {
		if (err == EAGAIN) {
			/*
			 * If arc_memory_throttle() detected that pageout
			 * is running and we are low on memory, we delay new
			 * non-pageout transactions to give pageout an
			 * advantage.
			 *
			 * It is unfortunate to be delaying while the caller's
			 * locks are held.
			 */
			txg_delay(dd->dd_pool, tx->tx_txg,
			    MSEC2NSEC(10), MSEC2NSEC(10));
			err = SET_ERROR(ERESTART);
		}
	}

	if (err == 0) {
		err = dsl_dir_tempreserve_impl(dd, asize, netfree,
		    B_FALSE, tr_list, tx, B_TRUE);
	}

	if (err != 0)
		dsl_dir_tempreserve_clear(tr_list, tx);
	else
		*tr_cookiep = tr_list;

	return (err);
}

/*
 * Clear a temporary reservation that we previously made with
 * dsl_dir_tempreserve_space().
 */
void
dsl_dir_tempreserve_clear(void *tr_cookie, dmu_tx_t *tx)
{
	int txgidx = tx->tx_txg & TXG_MASK;
	list_t *tr_list = tr_cookie;
	struct tempreserve *tr;

	ASSERT3U(tx->tx_txg, !=, 0);

	if (tr_cookie == NULL)
		return;

	while ((tr = list_head(tr_list)) != NULL) {
		if (tr->tr_ds) {
			mutex_enter(&tr->tr_ds->dd_lock);
			ASSERT3U(tr->tr_ds->dd_tempreserved[txgidx], >=,
			    tr->tr_size);
			tr->tr_ds->dd_tempreserved[txgidx] -= tr->tr_size;
			mutex_exit(&tr->tr_ds->dd_lock);
		} else {
			arc_tempreserve_clear(tr->tr_size);
		}
		list_remove(tr_list, tr);
		kmem_free(tr, sizeof (struct tempreserve));
	}

	kmem_free(tr_list, sizeof (list_t));
}

/*
 * This should be called from open context when we think we're going to write
 * or free space, for example when dirtying data. Be conservative; it's okay
 * to write less space or free more, but we don't want to write more or free
 * less than the amount specified.
 */
void
dsl_dir_willuse_space(dsl_dir_t *dd, int64_t space, dmu_tx_t *tx)
{
	int64_t parent_space;
	uint64_t est_used;

	mutex_enter(&dd->dd_lock);
	if (space > 0)
		dd->dd_space_towrite[tx->tx_txg & TXG_MASK] += space;

	est_used = dsl_dir_space_towrite(dd) + dsl_dir_phys(dd)->dd_used_bytes;
	parent_space = parent_delta(dd, est_used, space);
	mutex_exit(&dd->dd_lock);

	/* Make sure that we clean up dd_space_to* */
	dsl_dir_dirty(dd, tx);

	/* XXX this is potentially expensive and unnecessary... */
	if (parent_space && dd->dd_parent)
		dsl_dir_willuse_space(dd->dd_parent, parent_space, tx);
}

/* call from syncing context when we actually write/free space for this dd */
void
dsl_dir_diduse_space(dsl_dir_t *dd, dd_used_t type,
    int64_t used, int64_t compressed, int64_t uncompressed, dmu_tx_t *tx)
{
	int64_t accounted_delta;

	/*
	 * dsl_dataset_set_refreservation_sync_impl() calls this with
	 * dd_lock held, so that it can atomically update
	 * ds->ds_reserved and the dsl_dir accounting, so that
	 * dsl_dataset_check_quota() can see dataset and dir accounting
	 * consistently.
	 */
	boolean_t needlock = !MUTEX_HELD(&dd->dd_lock);

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(type < DD_USED_NUM);

	dmu_buf_will_dirty(dd->dd_dbuf, tx);

	if (needlock)
		mutex_enter(&dd->dd_lock);
	accounted_delta =
	    parent_delta(dd, dsl_dir_phys(dd)->dd_used_bytes, used);
	ASSERT(used >= 0 || dsl_dir_phys(dd)->dd_used_bytes >= -used);
	ASSERT(compressed >= 0 ||
	    dsl_dir_phys(dd)->dd_compressed_bytes >= -compressed);
	ASSERT(uncompressed >= 0 ||
	    dsl_dir_phys(dd)->dd_uncompressed_bytes >= -uncompressed);
	dsl_dir_phys(dd)->dd_used_bytes += used;
	dsl_dir_phys(dd)->dd_uncompressed_bytes += uncompressed;
	dsl_dir_phys(dd)->dd_compressed_bytes += compressed;

	if (dsl_dir_phys(dd)->dd_flags & DD_FLAG_USED_BREAKDOWN) {
		ASSERT(used > 0 ||
		    dsl_dir_phys(dd)->dd_used_breakdown[type] >= -used);
		dsl_dir_phys(dd)->dd_used_breakdown[type] += used;
#ifdef DEBUG
		dd_used_t t;
		uint64_t u = 0;
		for (t = 0; t < DD_USED_NUM; t++)
			u += dsl_dir_phys(dd)->dd_used_breakdown[t];
		ASSERT3U(u, ==, dsl_dir_phys(dd)->dd_used_bytes);
#endif
	}
	if (needlock)
		mutex_exit(&dd->dd_lock);

	if (dd->dd_parent != NULL) {
		dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD,
		    accounted_delta, compressed, uncompressed, tx);
		dsl_dir_transfer_space(dd->dd_parent,
		    used - accounted_delta,
		    DD_USED_CHILD_RSRV, DD_USED_CHILD, NULL);
	}
}

void
dsl_dir_transfer_space(dsl_dir_t *dd, int64_t delta,
    dd_used_t oldtype, dd_used_t newtype, dmu_tx_t *tx)
{
	ASSERT(tx == NULL || dmu_tx_is_syncing(tx));
	ASSERT(oldtype < DD_USED_NUM);
	ASSERT(newtype < DD_USED_NUM);

	if (delta == 0 ||
	    !(dsl_dir_phys(dd)->dd_flags & DD_FLAG_USED_BREAKDOWN))
		return;

	if (tx != NULL)
		dmu_buf_will_dirty(dd->dd_dbuf, tx);
	mutex_enter(&dd->dd_lock);
	ASSERT(delta > 0 ?
	    dsl_dir_phys(dd)->dd_used_breakdown[oldtype] >= delta :
	    dsl_dir_phys(dd)->dd_used_breakdown[newtype] >= -delta);
	ASSERT(dsl_dir_phys(dd)->dd_used_bytes >= ABS(delta));
	dsl_dir_phys(dd)->dd_used_breakdown[oldtype] -= delta;
	dsl_dir_phys(dd)->dd_used_breakdown[newtype] += delta;
	mutex_exit(&dd->dd_lock);
}

typedef struct dsl_dir_set_qr_arg {
	const char *ddsqra_name;
	zprop_source_t ddsqra_source;
	uint64_t ddsqra_value;
} dsl_dir_set_qr_arg_t;

static int
dsl_dir_set_quota_check(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	int error;
	uint64_t towrite, newval;

	error = dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds);
	if (error != 0)
		return (error);

	error = dsl_prop_predict(ds->ds_dir, "quota",
	    ddsqra->ddsqra_source, ddsqra->ddsqra_value, &newval);
	if (error != 0) {
		dsl_dataset_rele(ds, FTAG);
		return (error);
	}

	if (newval == 0) {
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	mutex_enter(&ds->ds_dir->dd_lock);
	/*
	 * If we are doing the preliminary check in open context, and
	 * there are pending changes, then don't fail it, since the
	 * pending changes could under-estimate the amount of space to be
	 * freed up.
	 */
	towrite = dsl_dir_space_towrite(ds->ds_dir);
	if ((dmu_tx_is_syncing(tx) || towrite == 0) &&
	    (newval < dsl_dir_phys(ds->ds_dir)->dd_reserved ||
	    newval < dsl_dir_phys(ds->ds_dir)->dd_used_bytes + towrite)) {
		error = SET_ERROR(ENOSPC);
	}
	mutex_exit(&ds->ds_dir->dd_lock);
	dsl_dataset_rele(ds, FTAG);
	return (error);
}

static void
dsl_dir_set_quota_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	uint64_t newval;

	VERIFY0(dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds));

	if (spa_version(dp->dp_spa) >= SPA_VERSION_RECVD_PROPS) {
		dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_QUOTA),
		    ddsqra->ddsqra_source, sizeof (ddsqra->ddsqra_value), 1,
		    &ddsqra->ddsqra_value, tx);

		VERIFY0(dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_QUOTA), &newval));
	} else {
		newval = ddsqra->ddsqra_value;
		spa_history_log_internal_ds(ds, "set", tx, "%s=%lld",
		    zfs_prop_to_name(ZFS_PROP_QUOTA), (longlong_t)newval);
	}

	dmu_buf_will_dirty(ds->ds_dir->dd_dbuf, tx);
	mutex_enter(&ds->ds_dir->dd_lock);
	dsl_dir_phys(ds->ds_dir)->dd_quota = newval;
	mutex_exit(&ds->ds_dir->dd_lock);
	dsl_dataset_rele(ds, FTAG);
}

int
dsl_dir_set_quota(const char *ddname, zprop_source_t source, uint64_t quota)
{
	dsl_dir_set_qr_arg_t ddsqra;

	ddsqra.ddsqra_name = ddname;
	ddsqra.ddsqra_source = source;
	ddsqra.ddsqra_value = quota;

	return (dsl_sync_task(ddname, dsl_dir_set_quota_check,
	    dsl_dir_set_quota_sync, &ddsqra, 0,
	    ZFS_SPACE_CHECK_EXTRA_RESERVED));
}

int
dsl_dir_set_reservation_check(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	dsl_dir_t *dd;
	uint64_t newval, used, avail;
	int error;

	error = dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds);
	if (error != 0)
		return (error);
	dd = ds->ds_dir;

	/*
	 * If we are doing the preliminary check in open context, the
	 * space estimates may be inaccurate.
	 */
	if (!dmu_tx_is_syncing(tx)) {
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	error = dsl_prop_predict(ds->ds_dir,
	    zfs_prop_to_name(ZFS_PROP_RESERVATION),
	    ddsqra->ddsqra_source, ddsqra->ddsqra_value, &newval);
	if (error != 0) {
		dsl_dataset_rele(ds, FTAG);
		return (error);
	}

	mutex_enter(&dd->dd_lock);
	used = dsl_dir_phys(dd)->dd_used_bytes;
	mutex_exit(&dd->dd_lock);

	if (dd->dd_parent) {
		avail = dsl_dir_space_available(dd->dd_parent,
		    NULL, 0, FALSE);
	} else {
		avail = dsl_pool_adjustedsize(dd->dd_pool,
		    ZFS_SPACE_CHECK_NORMAL) - used;
	}

	if (MAX(used, newval) > MAX(used, dsl_dir_phys(dd)->dd_reserved)) {
		uint64_t delta = MAX(used, newval) -
		    MAX(used, dsl_dir_phys(dd)->dd_reserved);

		if (delta > avail ||
		    (dsl_dir_phys(dd)->dd_quota > 0 &&
		    newval > dsl_dir_phys(dd)->dd_quota))
			error = SET_ERROR(ENOSPC);
	}

	dsl_dataset_rele(ds, FTAG);
	return (error);
}

void
dsl_dir_set_reservation_sync_impl(dsl_dir_t *dd, uint64_t value, dmu_tx_t *tx)
{
	uint64_t used;
	int64_t delta;

	dmu_buf_will_dirty(dd->dd_dbuf, tx);

	mutex_enter(&dd->dd_lock);
	used = dsl_dir_phys(dd)->dd_used_bytes;
	delta = MAX(used, value) - MAX(used, dsl_dir_phys(dd)->dd_reserved);
	dsl_dir_phys(dd)->dd_reserved = value;

	if (dd->dd_parent != NULL) {
		/* Roll up this additional usage into our ancestors */
		dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD_RSRV,
		    delta, 0, 0, tx);
	}
	mutex_exit(&dd->dd_lock);
}

static void
dsl_dir_set_reservation_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dir_set_qr_arg_t *ddsqra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	uint64_t newval;

	VERIFY0(dsl_dataset_hold(dp, ddsqra->ddsqra_name, FTAG, &ds));

	if (spa_version(dp->dp_spa) >= SPA_VERSION_RECVD_PROPS) {
		dsl_prop_set_sync_impl(ds,
		    zfs_prop_to_name(ZFS_PROP_RESERVATION),
		    ddsqra->ddsqra_source, sizeof (ddsqra->ddsqra_value), 1,
		    &ddsqra->ddsqra_value, tx);

		VERIFY0(dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_RESERVATION), &newval));
	} else {
		newval = ddsqra->ddsqra_value;
		spa_history_log_internal_ds(ds, "set", tx, "%s=%lld",
		    zfs_prop_to_name(ZFS_PROP_RESERVATION),
		    (longlong_t)newval);
	}

	dsl_dir_set_reservation_sync_impl(ds->ds_dir, newval, tx);
	dsl_dataset_rele(ds, FTAG);
}

int
dsl_dir_set_reservation(const char *ddname, zprop_source_t source,
    uint64_t reservation)
{
	dsl_dir_set_qr_arg_t ddsqra;

	ddsqra.ddsqra_name = ddname;
	ddsqra.ddsqra_source = source;
	ddsqra.ddsqra_value = reservation;

	return (dsl_sync_task(ddname, dsl_dir_set_reservation_check,
	    dsl_dir_set_reservation_sync, &ddsqra, 0,
	    ZFS_SPACE_CHECK_EXTRA_RESERVED));
}

static dsl_dir_t *
closest_common_ancestor(dsl_dir_t *ds1, dsl_dir_t *ds2)
{
	for (; ds1; ds1 = ds1->dd_parent) {
		dsl_dir_t *dd;
		for (dd = ds2; dd; dd = dd->dd_parent) {
			if (ds1 == dd)
				return (dd);
		}
	}
	return (NULL);
}

/*
 * If delta is applied to dd, how much of that delta would be applied to
 * ancestor?  Syncing context only.
 */
static int64_t
would_change(dsl_dir_t *dd, int64_t delta, dsl_dir_t *ancestor)
{
	if (dd == ancestor)
		return (delta);

	mutex_enter(&dd->dd_lock);
	delta = parent_delta(dd, dsl_dir_phys(dd)->dd_used_bytes, delta);
	mutex_exit(&dd->dd_lock);
	return (would_change(dd->dd_parent, delta, ancestor));
}

typedef struct dsl_dir_rename_arg {
	const char *ddra_oldname;
	const char *ddra_newname;
	cred_t *ddra_cred;
} dsl_dir_rename_arg_t;

typedef struct dsl_valid_rename_arg {
	int char_delta;
	int nest_delta;
} dsl_valid_rename_arg_t;

/* ARGSUSED */
static int
dsl_valid_rename(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	dsl_valid_rename_arg_t *dvra = arg;
	char namebuf[ZFS_MAX_DATASET_NAME_LEN];

	dsl_dataset_name(ds, namebuf);

	ASSERT3U(strnlen(namebuf, ZFS_MAX_DATASET_NAME_LEN),
	    <, ZFS_MAX_DATASET_NAME_LEN);
	int namelen = strlen(namebuf) + dvra->char_delta;
	int depth = get_dataset_depth(namebuf) + dvra->nest_delta;

	if (namelen >= ZFS_MAX_DATASET_NAME_LEN)
		return (SET_ERROR(ENAMETOOLONG));
	if (dvra->nest_delta > 0 && depth >= zfs_max_dataset_nesting)
		return (SET_ERROR(ENAMETOOLONG));
	return (0);
}

static int
dsl_dir_rename_check(void *arg, dmu_tx_t *tx)
{
	dsl_dir_rename_arg_t *ddra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd, *newparent;
	dsl_valid_rename_arg_t dvra;
	const char *mynewname;
	int error;

	/* target dir should exist */
	error = dsl_dir_hold(dp, ddra->ddra_oldname, FTAG, &dd, NULL);
	if (error != 0)
		return (error);

	/* new parent should exist */
	error = dsl_dir_hold(dp, ddra->ddra_newname, FTAG,
	    &newparent, &mynewname);
	if (error != 0) {
		dsl_dir_rele(dd, FTAG);
		return (error);
	}

	/* can't rename to different pool */
	if (dd->dd_pool != newparent->dd_pool) {
		dsl_dir_rele(newparent, FTAG);
		dsl_dir_rele(dd, FTAG);
		return (SET_ERROR(EXDEV));
	}

	/* new name should not already exist */
	if (mynewname == NULL) {
		dsl_dir_rele(newparent, FTAG);
		dsl_dir_rele(dd, FTAG);
		return (SET_ERROR(EEXIST));
	}

	ASSERT3U(strnlen(ddra->ddra_newname, ZFS_MAX_DATASET_NAME_LEN),
	    <, ZFS_MAX_DATASET_NAME_LEN);
	ASSERT3U(strnlen(ddra->ddra_oldname, ZFS_MAX_DATASET_NAME_LEN),
	    <, ZFS_MAX_DATASET_NAME_LEN);
	dvra.char_delta = strlen(ddra->ddra_newname)
	    - strlen(ddra->ddra_oldname);
	dvra.nest_delta = get_dataset_depth(ddra->ddra_newname)
	    - get_dataset_depth(ddra->ddra_oldname);

	/* if the name length is growing, validate child name lengths */
	if (dvra.char_delta > 0 || dvra.nest_delta > 0) {
		error = dmu_objset_find_dp(dp, dd->dd_object, dsl_valid_rename,
		    &dvra, DS_FIND_CHILDREN | DS_FIND_SNAPSHOTS);
		if (error != 0) {
			dsl_dir_rele(newparent, FTAG);
			dsl_dir_rele(dd, FTAG);
			return (error);
		}
	}

	if (dmu_tx_is_syncing(tx)) {
		if (spa_feature_is_active(dp->dp_spa,
		    SPA_FEATURE_FS_SS_LIMIT)) {
			/*
			 * Although this is the check function and we don't
			 * normally make on-disk changes in check functions,
			 * we need to do that here.
			 *
			 * Ensure this portion of the tree's counts have been
			 * initialized in case the new parent has limits set.
			 */
			dsl_dir_init_fs_ss_count(dd, tx);
		}
	}

	if (newparent != dd->dd_parent) {
		/* is there enough space? */
		uint64_t myspace =
		    MAX(dsl_dir_phys(dd)->dd_used_bytes,
		    dsl_dir_phys(dd)->dd_reserved);
		objset_t *os = dd->dd_pool->dp_meta_objset;
		uint64_t fs_cnt = 0;
		uint64_t ss_cnt = 0;

		if (dsl_dir_is_zapified(dd)) {
			int err;

			err = zap_lookup(os, dd->dd_object,
			    DD_FIELD_FILESYSTEM_COUNT, sizeof (fs_cnt), 1,
			    &fs_cnt);
			if (err != ENOENT && err != 0) {
				dsl_dir_rele(newparent, FTAG);
				dsl_dir_rele(dd, FTAG);
				return (err);
			}

			/*
			 * have to add 1 for the filesystem itself that we're
			 * moving
			 */
			fs_cnt++;

			err = zap_lookup(os, dd->dd_object,
			    DD_FIELD_SNAPSHOT_COUNT, sizeof (ss_cnt), 1,
			    &ss_cnt);
			if (err != ENOENT && err != 0) {
				dsl_dir_rele(newparent, FTAG);
				dsl_dir_rele(dd, FTAG);
				return (err);
			}
		}

		/* no rename into our descendant */
		if (closest_common_ancestor(dd, newparent) == dd) {
			dsl_dir_rele(newparent, FTAG);
			dsl_dir_rele(dd, FTAG);
			return (SET_ERROR(EINVAL));
		}

		error = dsl_dir_transfer_possible(dd->dd_parent,
		    newparent, fs_cnt, ss_cnt, myspace, ddra->ddra_cred);
		if (error != 0) {
			dsl_dir_rele(newparent, FTAG);
			dsl_dir_rele(dd, FTAG);
			return (error);
		}
	}

	dsl_dir_rele(newparent, FTAG);
	dsl_dir_rele(dd, FTAG);
	return (0);
}

static void
dsl_dir_rename_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dir_rename_arg_t *ddra = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd, *newparent;
	const char *mynewname;
	int error;
	objset_t *mos = dp->dp_meta_objset;

	VERIFY0(dsl_dir_hold(dp, ddra->ddra_oldname, FTAG, &dd, NULL));
	VERIFY0(dsl_dir_hold(dp, ddra->ddra_newname, FTAG, &newparent,
	    &mynewname));

	/* Log this before we change the name. */
	spa_history_log_internal_dd(dd, "rename", tx,
	    "-> %s", ddra->ddra_newname);

	if (newparent != dd->dd_parent) {
		objset_t *os = dd->dd_pool->dp_meta_objset;
		uint64_t fs_cnt = 0;
		uint64_t ss_cnt = 0;

		/*
		 * We already made sure the dd counts were initialized in the
		 * check function.
		 */
		if (spa_feature_is_active(dp->dp_spa,
		    SPA_FEATURE_FS_SS_LIMIT)) {
			VERIFY0(zap_lookup(os, dd->dd_object,
			    DD_FIELD_FILESYSTEM_COUNT, sizeof (fs_cnt), 1,
			    &fs_cnt));
			/* add 1 for the filesystem itself that we're moving */
			fs_cnt++;

			VERIFY0(zap_lookup(os, dd->dd_object,
			    DD_FIELD_SNAPSHOT_COUNT, sizeof (ss_cnt), 1,
			    &ss_cnt));
		}

		dsl_fs_ss_count_adjust(dd->dd_parent, -fs_cnt,
		    DD_FIELD_FILESYSTEM_COUNT, tx);
		dsl_fs_ss_count_adjust(newparent, fs_cnt,
		    DD_FIELD_FILESYSTEM_COUNT, tx);

		dsl_fs_ss_count_adjust(dd->dd_parent, -ss_cnt,
		    DD_FIELD_SNAPSHOT_COUNT, tx);
		dsl_fs_ss_count_adjust(newparent, ss_cnt,
		    DD_FIELD_SNAPSHOT_COUNT, tx);

		dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD,
		    -dsl_dir_phys(dd)->dd_used_bytes,
		    -dsl_dir_phys(dd)->dd_compressed_bytes,
		    -dsl_dir_phys(dd)->dd_uncompressed_bytes, tx);
		dsl_dir_diduse_space(newparent, DD_USED_CHILD,
		    dsl_dir_phys(dd)->dd_used_bytes,
		    dsl_dir_phys(dd)->dd_compressed_bytes,
		    dsl_dir_phys(dd)->dd_uncompressed_bytes, tx);

		if (dsl_dir_phys(dd)->dd_reserved >
		    dsl_dir_phys(dd)->dd_used_bytes) {
			uint64_t unused_rsrv = dsl_dir_phys(dd)->dd_reserved -
			    dsl_dir_phys(dd)->dd_used_bytes;

			dsl_dir_diduse_space(dd->dd_parent, DD_USED_CHILD_RSRV,
			    -unused_rsrv, 0, 0, tx);
			dsl_dir_diduse_space(newparent, DD_USED_CHILD_RSRV,
			    unused_rsrv, 0, 0, tx);
		}
	}

	dmu_buf_will_dirty(dd->dd_dbuf, tx);

	/* remove from old parent zapobj */
	error = zap_remove(mos,
	    dsl_dir_phys(dd->dd_parent)->dd_child_dir_zapobj,
	    dd->dd_myname, tx);
	ASSERT0(error);

	(void) strcpy(dd->dd_myname, mynewname);
	dsl_dir_rele(dd->dd_parent, dd);
	dsl_dir_phys(dd)->dd_parent_obj = newparent->dd_object;
	VERIFY0(dsl_dir_hold_obj(dp,
	    newparent->dd_object, NULL, dd, &dd->dd_parent));

	/* add to new parent zapobj */
	VERIFY0(zap_add(mos, dsl_dir_phys(newparent)->dd_child_dir_zapobj,
	    dd->dd_myname, 8, 1, &dd->dd_object, tx));

#ifdef __FreeBSD__
#ifdef _KERNEL
	zfsvfs_update_fromname(ddra->ddra_oldname, ddra->ddra_newname);
	zvol_rename_minors(ddra->ddra_oldname, ddra->ddra_newname);
#endif
#endif

	dsl_prop_notify_all(dd);

	dsl_dir_rele(newparent, FTAG);
	dsl_dir_rele(dd, FTAG);
}

int
dsl_dir_rename(const char *oldname, const char *newname)
{
	dsl_dir_rename_arg_t ddra;

	ddra.ddra_oldname = oldname;
	ddra.ddra_newname = newname;
	ddra.ddra_cred = CRED();

	return (dsl_sync_task(oldname,
	    dsl_dir_rename_check, dsl_dir_rename_sync, &ddra,
	    3, ZFS_SPACE_CHECK_RESERVED));
}

int
dsl_dir_transfer_possible(dsl_dir_t *sdd, dsl_dir_t *tdd,
    uint64_t fs_cnt, uint64_t ss_cnt, uint64_t space, cred_t *cr)
{
	dsl_dir_t *ancestor;
	int64_t adelta;
	uint64_t avail;
	int err;

	ancestor = closest_common_ancestor(sdd, tdd);
	adelta = would_change(sdd, -space, ancestor);
	avail = dsl_dir_space_available(tdd, ancestor, adelta, FALSE);
	if (avail < space)
		return (SET_ERROR(ENOSPC));

	err = dsl_fs_ss_limit_check(tdd, fs_cnt, ZFS_PROP_FILESYSTEM_LIMIT,
	    ancestor, cr);
	if (err != 0)
		return (err);
	err = dsl_fs_ss_limit_check(tdd, ss_cnt, ZFS_PROP_SNAPSHOT_LIMIT,
	    ancestor, cr);
	if (err != 0)
		return (err);

	return (0);
}

timestruc_t
dsl_dir_snap_cmtime(dsl_dir_t *dd)
{
	timestruc_t t;

	mutex_enter(&dd->dd_lock);
	t = dd->dd_snap_cmtime;
	mutex_exit(&dd->dd_lock);

	return (t);
}

void
dsl_dir_snap_cmtime_update(dsl_dir_t *dd)
{
	timestruc_t t;

	gethrestime(&t);
	mutex_enter(&dd->dd_lock);
	dd->dd_snap_cmtime = t;
	mutex_exit(&dd->dd_lock);
}

void
dsl_dir_zapify(dsl_dir_t *dd, dmu_tx_t *tx)
{
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	dmu_object_zapify(mos, dd->dd_object, DMU_OT_DSL_DIR, tx);
}

boolean_t
dsl_dir_is_zapified(dsl_dir_t *dd)
{
	dmu_object_info_t doi;

	dmu_object_info_from_db(dd->dd_dbuf, &doi);
	return (doi.doi_type == DMU_OTN_ZAP_METADATA);
}
