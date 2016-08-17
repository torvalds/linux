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
 * Copyright (c) 2013 Martin Matuska. All rights reserved.
 * Copyright 2015, Joyent, Inc.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>

#include "zfs_prop.h"

#define	ZPROP_INHERIT_SUFFIX "$inherit"
#define	ZPROP_RECVD_SUFFIX "$recvd"

static int
dodefault(zfs_prop_t prop, int intsz, int numints, void *buf)
{
	/*
	 * The setonce properties are read-only, BUT they still
	 * have a default value that can be used as the initial
	 * value.
	 */
	if (prop == ZPROP_INVAL ||
	    (zfs_prop_readonly(prop) && !zfs_prop_setonce(prop)))
		return (SET_ERROR(ENOENT));

	if (zfs_prop_get_type(prop) == PROP_TYPE_STRING) {
		if (intsz != 1)
			return (SET_ERROR(EOVERFLOW));
		(void) strncpy(buf, zfs_prop_default_string(prop),
		    numints);
	} else {
		if (intsz != 8 || numints < 1)
			return (SET_ERROR(EOVERFLOW));

		*(uint64_t *)buf = zfs_prop_default_numeric(prop);
	}

	return (0);
}

int
dsl_prop_get_dd(dsl_dir_t *dd, const char *propname,
    int intsz, int numints, void *buf, char *setpoint, boolean_t snapshot)
{
	int err = ENOENT;
	dsl_dir_t *target = dd;
	objset_t *mos = dd->dd_pool->dp_meta_objset;
	zfs_prop_t prop;
	boolean_t inheritable;
	boolean_t inheriting = B_FALSE;
	char *inheritstr;
	char *recvdstr;

	ASSERT(dsl_pool_config_held(dd->dd_pool));

	if (setpoint)
		setpoint[0] = '\0';

	prop = zfs_name_to_prop(propname);
	inheritable = (prop == ZPROP_INVAL || zfs_prop_inheritable(prop));
	inheritstr = kmem_asprintf("%s%s", propname, ZPROP_INHERIT_SUFFIX);
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	/*
	 * Note: dd may become NULL, therefore we shouldn't dereference it
	 * after this loop.
	 */
	for (; dd != NULL; dd = dd->dd_parent) {
		if (dd != target || snapshot) {
			if (!inheritable)
				break;
			inheriting = B_TRUE;
		}

		/* Check for a local value. */
		err = zap_lookup(mos, dsl_dir_phys(dd)->dd_props_zapobj,
		    propname, intsz, numints, buf);
		if (err != ENOENT) {
			if (setpoint != NULL && err == 0)
				dsl_dir_name(dd, setpoint);
			break;
		}

		/*
		 * Skip the check for a received value if there is an explicit
		 * inheritance entry.
		 */
		err = zap_contains(mos, dsl_dir_phys(dd)->dd_props_zapobj,
		    inheritstr);
		if (err != 0 && err != ENOENT)
			break;

		if (err == ENOENT) {
			/* Check for a received value. */
			err = zap_lookup(mos, dsl_dir_phys(dd)->dd_props_zapobj,
			    recvdstr, intsz, numints, buf);
			if (err != ENOENT) {
				if (setpoint != NULL && err == 0) {
					if (inheriting) {
						dsl_dir_name(dd, setpoint);
					} else {
						(void) strcpy(setpoint,
						    ZPROP_SOURCE_VAL_RECVD);
					}
				}
				break;
			}
		}

		/*
		 * If we found an explicit inheritance entry, err is zero even
		 * though we haven't yet found the value, so reinitializing err
		 * at the end of the loop (instead of at the beginning) ensures
		 * that err has a valid post-loop value.
		 */
		err = SET_ERROR(ENOENT);
	}

	if (err == ENOENT)
		err = dodefault(prop, intsz, numints, buf);

	strfree(inheritstr);
	strfree(recvdstr);

	return (err);
}

int
dsl_prop_get_ds(dsl_dataset_t *ds, const char *propname,
    int intsz, int numints, void *buf, char *setpoint)
{
	zfs_prop_t prop = zfs_name_to_prop(propname);
	boolean_t inheritable;
	uint64_t zapobj;

	ASSERT(dsl_pool_config_held(ds->ds_dir->dd_pool));
	inheritable = (prop == ZPROP_INVAL || zfs_prop_inheritable(prop));
	zapobj = dsl_dataset_phys(ds)->ds_props_obj;

	if (zapobj != 0) {
		objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
		int err;

		ASSERT(ds->ds_is_snapshot);

		/* Check for a local value. */
		err = zap_lookup(mos, zapobj, propname, intsz, numints, buf);
		if (err != ENOENT) {
			if (setpoint != NULL && err == 0)
				dsl_dataset_name(ds, setpoint);
			return (err);
		}

		/*
		 * Skip the check for a received value if there is an explicit
		 * inheritance entry.
		 */
		if (inheritable) {
			char *inheritstr = kmem_asprintf("%s%s", propname,
			    ZPROP_INHERIT_SUFFIX);
			err = zap_contains(mos, zapobj, inheritstr);
			strfree(inheritstr);
			if (err != 0 && err != ENOENT)
				return (err);
		}

		if (err == ENOENT) {
			/* Check for a received value. */
			char *recvdstr = kmem_asprintf("%s%s", propname,
			    ZPROP_RECVD_SUFFIX);
			err = zap_lookup(mos, zapobj, recvdstr,
			    intsz, numints, buf);
			strfree(recvdstr);
			if (err != ENOENT) {
				if (setpoint != NULL && err == 0)
					(void) strcpy(setpoint,
					    ZPROP_SOURCE_VAL_RECVD);
				return (err);
			}
		}
	}

	return (dsl_prop_get_dd(ds->ds_dir, propname,
	    intsz, numints, buf, setpoint, ds->ds_is_snapshot));
}

static dsl_prop_record_t *
dsl_prop_record_find(dsl_dir_t *dd, const char *propname)
{
	dsl_prop_record_t *pr = NULL;

	ASSERT(MUTEX_HELD(&dd->dd_lock));

	for (pr = list_head(&dd->dd_props);
	    pr != NULL; pr = list_next(&dd->dd_props, pr)) {
		if (strcmp(pr->pr_propname, propname) == 0)
			break;
	}

	return (pr);
}

static dsl_prop_record_t *
dsl_prop_record_create(dsl_dir_t *dd, const char *propname)
{
	dsl_prop_record_t *pr;

	ASSERT(MUTEX_HELD(&dd->dd_lock));

	pr = kmem_alloc(sizeof (dsl_prop_record_t), KM_SLEEP);
	pr->pr_propname = spa_strdup(propname);
	list_create(&pr->pr_cbs, sizeof (dsl_prop_cb_record_t),
	    offsetof(dsl_prop_cb_record_t, cbr_pr_node));
	list_insert_head(&dd->dd_props, pr);

	return (pr);
}

void
dsl_prop_init(dsl_dir_t *dd)
{
	list_create(&dd->dd_props, sizeof (dsl_prop_record_t),
	    offsetof(dsl_prop_record_t, pr_node));
}

void
dsl_prop_fini(dsl_dir_t *dd)
{
	dsl_prop_record_t *pr;

	while ((pr = list_remove_head(&dd->dd_props)) != NULL) {
		list_destroy(&pr->pr_cbs);
		spa_strfree((char *)pr->pr_propname);
		kmem_free(pr, sizeof (dsl_prop_record_t));
	}
	list_destroy(&dd->dd_props);
}

/*
 * Register interest in the named property.  We'll call the callback
 * once to notify it of the current property value, and again each time
 * the property changes, until this callback is unregistered.
 *
 * Return 0 on success, errno if the prop is not an integer value.
 */
int
dsl_prop_register(dsl_dataset_t *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg)
{
	dsl_dir_t *dd = ds->ds_dir;
	uint64_t value;
	dsl_prop_record_t *pr;
	dsl_prop_cb_record_t *cbr;
	int err;
	ASSERTV(dsl_pool_t *dp = dd->dd_pool);

	ASSERT(dsl_pool_config_held(dp));

	err = dsl_prop_get_int_ds(ds, propname, &value);
	if (err != 0)
		return (err);

	cbr = kmem_alloc(sizeof (dsl_prop_cb_record_t), KM_SLEEP);
	cbr->cbr_ds = ds;
	cbr->cbr_func = callback;
	cbr->cbr_arg = cbarg;

	mutex_enter(&dd->dd_lock);
	pr = dsl_prop_record_find(dd, propname);
	if (pr == NULL)
		pr = dsl_prop_record_create(dd, propname);
	cbr->cbr_pr = pr;
	list_insert_head(&pr->pr_cbs, cbr);
	list_insert_head(&ds->ds_prop_cbs, cbr);
	mutex_exit(&dd->dd_lock);

	cbr->cbr_func(cbr->cbr_arg, value);
	return (0);
}

int
dsl_prop_get(const char *dsname, const char *propname,
    int intsz, int numints, void *buf, char *setpoint)
{
	objset_t *os;
	int error;

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0)
		return (error);

	error = dsl_prop_get_ds(dmu_objset_ds(os), propname,
	    intsz, numints, buf, setpoint);

	dmu_objset_rele(os, FTAG);
	return (error);
}

/*
 * Get the current property value.  It may have changed by the time this
 * function returns, so it is NOT safe to follow up with
 * dsl_prop_register() and assume that the value has not changed in
 * between.
 *
 * Return 0 on success, ENOENT if ddname is invalid.
 */
int
dsl_prop_get_integer(const char *ddname, const char *propname,
    uint64_t *valuep, char *setpoint)
{
	return (dsl_prop_get(ddname, propname, 8, 1, valuep, setpoint));
}

int
dsl_prop_get_int_ds(dsl_dataset_t *ds, const char *propname,
    uint64_t *valuep)
{
	return (dsl_prop_get_ds(ds, propname, 8, 1, valuep, NULL));
}

/*
 * Predict the effective value of the given special property if it were set with
 * the given value and source. This is not a general purpose function. It exists
 * only to handle the special requirements of the quota and reservation
 * properties. The fact that these properties are non-inheritable greatly
 * simplifies the prediction logic.
 *
 * Returns 0 on success, a positive error code on failure, or -1 if called with
 * a property not handled by this function.
 */
int
dsl_prop_predict(dsl_dir_t *dd, const char *propname,
    zprop_source_t source, uint64_t value, uint64_t *newvalp)
{
	zfs_prop_t prop = zfs_name_to_prop(propname);
	objset_t *mos;
	uint64_t zapobj;
	uint64_t version;
	char *recvdstr;
	int err = 0;

	switch (prop) {
	case ZFS_PROP_QUOTA:
	case ZFS_PROP_RESERVATION:
	case ZFS_PROP_REFQUOTA:
	case ZFS_PROP_REFRESERVATION:
		break;
	default:
		return (-1);
	}

	mos = dd->dd_pool->dp_meta_objset;
	zapobj = dsl_dir_phys(dd)->dd_props_zapobj;
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	version = spa_version(dd->dd_pool->dp_spa);
	if (version < SPA_VERSION_RECVD_PROPS) {
		if (source & ZPROP_SRC_NONE)
			source = ZPROP_SRC_NONE;
		else if (source & ZPROP_SRC_RECEIVED)
			source = ZPROP_SRC_LOCAL;
	}

	switch ((int)source) {
	case ZPROP_SRC_NONE:
		/* Revert to the received value, if any. */
		err = zap_lookup(mos, zapobj, recvdstr, 8, 1, newvalp);
		if (err == ENOENT)
			*newvalp = 0;
		break;
	case ZPROP_SRC_LOCAL:
		*newvalp = value;
		break;
	case ZPROP_SRC_RECEIVED:
		/*
		 * If there's no local setting, then the new received value will
		 * be the effective value.
		 */
		err = zap_lookup(mos, zapobj, propname, 8, 1, newvalp);
		if (err == ENOENT)
			*newvalp = value;
		break;
	case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		/*
		 * We're clearing the received value, so the local setting (if
		 * it exists) remains the effective value.
		 */
		err = zap_lookup(mos, zapobj, propname, 8, 1, newvalp);
		if (err == ENOENT)
			*newvalp = 0;
		break;
	default:
		panic("unexpected property source: %d", source);
	}

	strfree(recvdstr);

	if (err == ENOENT)
		return (0);

	return (err);
}

/*
 * Unregister this callback.  Return 0 on success, ENOENT if ddname is
 * invalid, or ENOMSG if no matching callback registered.
 *
 * NOTE: This function is no longer used internally but has been preserved
 * to prevent breaking external consumers (Lustre, etc).
 */
int
dsl_prop_unregister(dsl_dataset_t *ds, const char *propname,
    dsl_prop_changed_cb_t *callback, void *cbarg)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_prop_cb_record_t *cbr;

	mutex_enter(&dd->dd_lock);
	for (cbr = list_head(&ds->ds_prop_cbs);
	    cbr; cbr = list_next(&ds->ds_prop_cbs, cbr)) {
		if (cbr->cbr_ds == ds &&
		    cbr->cbr_func == callback &&
		    cbr->cbr_arg == cbarg &&
		    strcmp(cbr->cbr_pr->pr_propname, propname) == 0)
			break;
	}

	if (cbr == NULL) {
		mutex_exit(&dd->dd_lock);
		return (SET_ERROR(ENOMSG));
	}

	list_remove(&ds->ds_prop_cbs, cbr);
	list_remove(&cbr->cbr_pr->pr_cbs, cbr);
	mutex_exit(&dd->dd_lock);
	kmem_free(cbr, sizeof (dsl_prop_cb_record_t));

	return (0);
}

/*
 * Unregister all callbacks that are registered with the
 * given callback argument.
 */
void
dsl_prop_unregister_all(dsl_dataset_t *ds, void *cbarg)
{
	dsl_prop_cb_record_t *cbr, *next_cbr;

	dsl_dir_t *dd = ds->ds_dir;

	mutex_enter(&dd->dd_lock);
	next_cbr = list_head(&ds->ds_prop_cbs);
	while (next_cbr != NULL) {
		cbr = next_cbr;
		next_cbr = list_next(&ds->ds_prop_cbs, cbr);
		if (cbr->cbr_arg == cbarg) {
			list_remove(&ds->ds_prop_cbs, cbr);
			list_remove(&cbr->cbr_pr->pr_cbs, cbr);
			kmem_free(cbr, sizeof (dsl_prop_cb_record_t));
		}
	}
	mutex_exit(&dd->dd_lock);
}

boolean_t
dsl_prop_hascb(dsl_dataset_t *ds)
{
	return (!list_is_empty(&ds->ds_prop_cbs));
}

/* ARGSUSED */
static int
dsl_prop_notify_all_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_prop_record_t *pr;
	dsl_prop_cb_record_t *cbr;

	mutex_enter(&dd->dd_lock);
	for (pr = list_head(&dd->dd_props);
	    pr; pr = list_next(&dd->dd_props, pr)) {
		for (cbr = list_head(&pr->pr_cbs); cbr;
		    cbr = list_next(&pr->pr_cbs, cbr)) {
			uint64_t value;

			/*
			 * Callback entries do not have holds on their
			 * datasets so that datasets with registered
			 * callbacks are still eligible for eviction.
			 * Unlike operations to update properties on a
			 * single dataset, we are performing a recursive
			 * descent of related head datasets.  The caller
			 * of this function only has a dataset hold on
			 * the passed in head dataset, not the snapshots
			 * associated with this dataset.  Without a hold,
			 * the dataset pointer within callback records
			 * for snapshots can be invalidated by eviction
			 * at any time.
			 *
			 * Use dsl_dataset_try_add_ref() to verify
			 * that the dataset for a snapshot has not
			 * begun eviction processing and to prevent
			 * eviction from occurring for the duration of
			 * the callback.  If the hold attempt fails,
			 * this object is already being evicted and the
			 * callback can be safely ignored.
			 */
			if (ds != cbr->cbr_ds &&
			    !dsl_dataset_try_add_ref(dp, cbr->cbr_ds, FTAG))
				continue;

			if (dsl_prop_get_ds(cbr->cbr_ds,
			    cbr->cbr_pr->pr_propname, sizeof (value), 1,
			    &value, NULL) == 0)
				cbr->cbr_func(cbr->cbr_arg, value);

			if (ds != cbr->cbr_ds)
				dsl_dataset_rele(cbr->cbr_ds, FTAG);
		}
	}
	mutex_exit(&dd->dd_lock);

	return (0);
}

/*
 * Update all property values for ddobj & its descendants.  This is used
 * when renaming the dir.
 */
void
dsl_prop_notify_all(dsl_dir_t *dd)
{
	dsl_pool_t *dp = dd->dd_pool;
	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));
	(void) dmu_objset_find_dp(dp, dd->dd_object, dsl_prop_notify_all_cb,
	    NULL, DS_FIND_CHILDREN);
}

static void
dsl_prop_changed_notify(dsl_pool_t *dp, uint64_t ddobj,
    const char *propname, uint64_t value, int first)
{
	dsl_dir_t *dd;
	dsl_prop_record_t *pr;
	dsl_prop_cb_record_t *cbr;
	objset_t *mos = dp->dp_meta_objset;
	zap_cursor_t zc;
	zap_attribute_t *za;
	int err;

	ASSERT(RRW_WRITE_HELD(&dp->dp_config_rwlock));
	err = dsl_dir_hold_obj(dp, ddobj, NULL, FTAG, &dd);
	if (err)
		return;

	if (!first) {
		/*
		 * If the prop is set here, then this change is not
		 * being inherited here or below; stop the recursion.
		 */
		err = zap_contains(mos, dsl_dir_phys(dd)->dd_props_zapobj,
		    propname);
		if (err == 0) {
			dsl_dir_rele(dd, FTAG);
			return;
		}
		ASSERT3U(err, ==, ENOENT);
	}

	mutex_enter(&dd->dd_lock);
	pr = dsl_prop_record_find(dd, propname);
	if (pr != NULL) {
		for (cbr = list_head(&pr->pr_cbs); cbr;
		    cbr = list_next(&pr->pr_cbs, cbr)) {
			uint64_t propobj;

			/*
			 * cbr->cbr_ds may be invalidated due to eviction,
			 * requiring the use of dsl_dataset_try_add_ref().
			 * See comment block in dsl_prop_notify_all_cb()
			 * for details.
			 */
			if (!dsl_dataset_try_add_ref(dp, cbr->cbr_ds, FTAG))
				continue;

			propobj = dsl_dataset_phys(cbr->cbr_ds)->ds_props_obj;

			/*
			 * If the property is not set on this ds, then it is
			 * inherited here; call the callback.
			 */
			if (propobj == 0 ||
			    zap_contains(mos, propobj, propname) != 0)
				cbr->cbr_func(cbr->cbr_arg, value);

			dsl_dataset_rele(cbr->cbr_ds, FTAG);
		}
	}
	mutex_exit(&dd->dd_lock);

	za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
	for (zap_cursor_init(&zc, mos,
	    dsl_dir_phys(dd)->dd_child_dir_zapobj);
	    zap_cursor_retrieve(&zc, za) == 0;
	    zap_cursor_advance(&zc)) {
		dsl_prop_changed_notify(dp, za->za_first_integer,
		    propname, value, FALSE);
	}
	kmem_free(za, sizeof (zap_attribute_t));
	zap_cursor_fini(&zc);
	dsl_dir_rele(dd, FTAG);
}

void
dsl_prop_set_sync_impl(dsl_dataset_t *ds, const char *propname,
    zprop_source_t source, int intsz, int numints, const void *value,
    dmu_tx_t *tx)
{
	objset_t *mos = ds->ds_dir->dd_pool->dp_meta_objset;
	uint64_t zapobj, intval, dummy;
	int isint;
	char valbuf[32];
	const char *valstr = NULL;
	char *inheritstr;
	char *recvdstr;
	char *tbuf = NULL;
	int err;
	uint64_t version = spa_version(ds->ds_dir->dd_pool->dp_spa);

	isint = (dodefault(zfs_name_to_prop(propname), 8, 1, &intval) == 0);

	if (ds->ds_is_snapshot) {
		ASSERT(version >= SPA_VERSION_SNAP_PROPS);
		if (dsl_dataset_phys(ds)->ds_props_obj == 0) {
			dmu_buf_will_dirty(ds->ds_dbuf, tx);
			dsl_dataset_phys(ds)->ds_props_obj =
			    zap_create(mos,
			    DMU_OT_DSL_PROPS, DMU_OT_NONE, 0, tx);
		}
		zapobj = dsl_dataset_phys(ds)->ds_props_obj;
	} else {
		zapobj = dsl_dir_phys(ds->ds_dir)->dd_props_zapobj;
	}

	if (version < SPA_VERSION_RECVD_PROPS) {
		if (source & ZPROP_SRC_NONE)
			source = ZPROP_SRC_NONE;
		else if (source & ZPROP_SRC_RECEIVED)
			source = ZPROP_SRC_LOCAL;
	}

	inheritstr = kmem_asprintf("%s%s", propname, ZPROP_INHERIT_SUFFIX);
	recvdstr = kmem_asprintf("%s%s", propname, ZPROP_RECVD_SUFFIX);

	switch ((int)source) {
	case ZPROP_SRC_NONE:
		/*
		 * revert to received value, if any (inherit -S)
		 * - remove propname
		 * - remove propname$inherit
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		break;
	case ZPROP_SRC_LOCAL:
		/*
		 * remove propname$inherit
		 * set propname -> value
		 */
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		VERIFY0(zap_update(mos, zapobj, propname,
		    intsz, numints, value, tx));
		break;
	case ZPROP_SRC_INHERITED:
		/*
		 * explicitly inherit
		 * - remove propname
		 * - set propname$inherit
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		if (version >= SPA_VERSION_RECVD_PROPS &&
		    dsl_prop_get_int_ds(ds, ZPROP_HAS_RECVD, &dummy) == 0) {
			dummy = 0;
			VERIFY0(zap_update(mos, zapobj, inheritstr,
			    8, 1, &dummy, tx));
		}
		break;
	case ZPROP_SRC_RECEIVED:
		/*
		 * set propname$recvd -> value
		 */
		err = zap_update(mos, zapobj, recvdstr,
		    intsz, numints, value, tx);
		ASSERT(err == 0);
		break;
	case (ZPROP_SRC_NONE | ZPROP_SRC_LOCAL | ZPROP_SRC_RECEIVED):
		/*
		 * clear local and received settings
		 * - remove propname
		 * - remove propname$inherit
		 * - remove propname$recvd
		 */
		err = zap_remove(mos, zapobj, propname, tx);
		ASSERT(err == 0 || err == ENOENT);
		err = zap_remove(mos, zapobj, inheritstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		/* FALLTHRU */
	case (ZPROP_SRC_NONE | ZPROP_SRC_RECEIVED):
		/*
		 * remove propname$recvd
		 */
		err = zap_remove(mos, zapobj, recvdstr, tx);
		ASSERT(err == 0 || err == ENOENT);
		break;
	default:
		cmn_err(CE_PANIC, "unexpected property source: %d", source);
	}

	strfree(inheritstr);
	strfree(recvdstr);

	if (isint) {
		VERIFY0(dsl_prop_get_int_ds(ds, propname, &intval));

		if (ds->ds_is_snapshot) {
			dsl_prop_cb_record_t *cbr;
			/*
			 * It's a snapshot; nothing can inherit this
			 * property, so just look for callbacks on this
			 * ds here.
			 */
			mutex_enter(&ds->ds_dir->dd_lock);
			for (cbr = list_head(&ds->ds_prop_cbs); cbr;
			    cbr = list_next(&ds->ds_prop_cbs, cbr)) {
				if (strcmp(cbr->cbr_pr->pr_propname,
				    propname) == 0)
					cbr->cbr_func(cbr->cbr_arg, intval);
			}
			mutex_exit(&ds->ds_dir->dd_lock);
		} else {
			dsl_prop_changed_notify(ds->ds_dir->dd_pool,
			    ds->ds_dir->dd_object, propname, intval, TRUE);
		}

		(void) snprintf(valbuf, sizeof (valbuf),
		    "%lld", (longlong_t)intval);
		valstr = valbuf;
	} else {
		if (source == ZPROP_SRC_LOCAL) {
			valstr = value;
		} else {
			tbuf = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
			if (dsl_prop_get_ds(ds, propname, 1,
			    ZAP_MAXVALUELEN, tbuf, NULL) == 0)
				valstr = tbuf;
		}
	}

	spa_history_log_internal_ds(ds, (source == ZPROP_SRC_NONE ||
	    source == ZPROP_SRC_INHERITED) ? "inherit" : "set", tx,
	    "%s=%s", propname, (valstr == NULL ? "" : valstr));

	if (tbuf != NULL)
		kmem_free(tbuf, ZAP_MAXVALUELEN);
}

int
dsl_prop_set_int(const char *dsname, const char *propname,
    zprop_source_t source, uint64_t value)
{
	nvlist_t *nvl = fnvlist_alloc();
	int error;

	fnvlist_add_uint64(nvl, propname, value);
	error = dsl_props_set(dsname, source, nvl);
	fnvlist_free(nvl);
	return (error);
}

int
dsl_prop_set_string(const char *dsname, const char *propname,
    zprop_source_t source, const char *value)
{
	nvlist_t *nvl = fnvlist_alloc();
	int error;

	fnvlist_add_string(nvl, propname, value);
	error = dsl_props_set(dsname, source, nvl);
	fnvlist_free(nvl);
	return (error);
}

int
dsl_prop_inherit(const char *dsname, const char *propname,
    zprop_source_t source)
{
	nvlist_t *nvl = fnvlist_alloc();
	int error;

	fnvlist_add_boolean(nvl, propname);
	error = dsl_props_set(dsname, source, nvl);
	fnvlist_free(nvl);
	return (error);
}

typedef struct dsl_props_set_arg {
	const char *dpsa_dsname;
	zprop_source_t dpsa_source;
	nvlist_t *dpsa_props;
} dsl_props_set_arg_t;

static int
dsl_props_set_check(void *arg, dmu_tx_t *tx)
{
	dsl_props_set_arg_t *dpsa = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;
	uint64_t version;
	nvpair_t *elem = NULL;
	int err;

	err = dsl_dataset_hold(dp, dpsa->dpsa_dsname, FTAG, &ds);
	if (err != 0)
		return (err);

	version = spa_version(ds->ds_dir->dd_pool->dp_spa);
	while ((elem = nvlist_next_nvpair(dpsa->dpsa_props, elem)) != NULL) {
		if (strlen(nvpair_name(elem)) >= ZAP_MAXNAMELEN) {
			dsl_dataset_rele(ds, FTAG);
			return (SET_ERROR(ENAMETOOLONG));
		}
		if (nvpair_type(elem) == DATA_TYPE_STRING) {
			char *valstr = fnvpair_value_string(elem);
			if (strlen(valstr) >= (version <
			    SPA_VERSION_STMF_PROP ?
			    ZAP_OLDMAXVALUELEN : ZAP_MAXVALUELEN)) {
				dsl_dataset_rele(ds, FTAG);
				return (E2BIG);
			}
		}
	}

	if (ds->ds_is_snapshot && version < SPA_VERSION_SNAP_PROPS) {
		dsl_dataset_rele(ds, FTAG);
		return (SET_ERROR(ENOTSUP));
	}
	dsl_dataset_rele(ds, FTAG);
	return (0);
}

void
dsl_props_set_sync_impl(dsl_dataset_t *ds, zprop_source_t source,
    nvlist_t *props, dmu_tx_t *tx)
{
	nvpair_t *elem = NULL;

	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		nvpair_t *pair = elem;
		const char *name = nvpair_name(pair);

		if (nvpair_type(pair) == DATA_TYPE_NVLIST) {
			/*
			 * This usually happens when we reuse the nvlist_t data
			 * returned by the counterpart dsl_prop_get_all_impl().
			 * For instance we do this to restore the original
			 * received properties when an error occurs in the
			 * zfs_ioc_recv() codepath.
			 */
			nvlist_t *attrs = fnvpair_value_nvlist(pair);
			pair = fnvlist_lookup_nvpair(attrs, ZPROP_VALUE);
		}

		if (nvpair_type(pair) == DATA_TYPE_STRING) {
			const char *value = fnvpair_value_string(pair);
			dsl_prop_set_sync_impl(ds, name,
			    source, 1, strlen(value) + 1, value, tx);
		} else if (nvpair_type(pair) == DATA_TYPE_UINT64) {
			uint64_t intval = fnvpair_value_uint64(pair);
			dsl_prop_set_sync_impl(ds, name,
			    source, sizeof (intval), 1, &intval, tx);
		} else if (nvpair_type(pair) == DATA_TYPE_BOOLEAN) {
			dsl_prop_set_sync_impl(ds, name,
			    source, 0, 0, NULL, tx);
		} else {
			panic("invalid nvpair type");
		}
	}
}

static void
dsl_props_set_sync(void *arg, dmu_tx_t *tx)
{
	dsl_props_set_arg_t *dpsa = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dataset_t *ds;

	VERIFY0(dsl_dataset_hold(dp, dpsa->dpsa_dsname, FTAG, &ds));
	dsl_props_set_sync_impl(ds, dpsa->dpsa_source, dpsa->dpsa_props, tx);
	dsl_dataset_rele(ds, FTAG);
}

/*
 * All-or-nothing; if any prop can't be set, nothing will be modified.
 */
int
dsl_props_set(const char *dsname, zprop_source_t source, nvlist_t *props)
{
	dsl_props_set_arg_t dpsa;
	int nblks = 0;

	dpsa.dpsa_dsname = dsname;
	dpsa.dpsa_source = source;
	dpsa.dpsa_props = props;

	/*
	 * If the source includes NONE, then we will only be removing entries
	 * from the ZAP object.  In that case don't check for ENOSPC.
	 */
	if ((source & ZPROP_SRC_NONE) == 0)
		nblks = 2 * fnvlist_num_pairs(props);

	return (dsl_sync_task(dsname, dsl_props_set_check, dsl_props_set_sync,
	    &dpsa, nblks, ZFS_SPACE_CHECK_RESERVED));
}

typedef enum dsl_prop_getflags {
	DSL_PROP_GET_INHERITING = 0x1,	/* searching parent of target ds */
	DSL_PROP_GET_SNAPSHOT = 0x2,	/* snapshot dataset */
	DSL_PROP_GET_LOCAL = 0x4,	/* local properties */
	DSL_PROP_GET_RECEIVED = 0x8	/* received properties */
} dsl_prop_getflags_t;

static int
dsl_prop_get_all_impl(objset_t *mos, uint64_t propobj,
    const char *setpoint, dsl_prop_getflags_t flags, nvlist_t *nv)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int err = 0;

	for (zap_cursor_init(&zc, mos, propobj);
	    (err = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		nvlist_t *propval;
		zfs_prop_t prop;
		char buf[ZAP_MAXNAMELEN];
		char *valstr;
		const char *suffix;
		const char *propname;
		const char *source;

		suffix = strchr(za.za_name, '$');

		if (suffix == NULL) {
			/*
			 * Skip local properties if we only want received
			 * properties.
			 */
			if (flags & DSL_PROP_GET_RECEIVED)
				continue;

			propname = za.za_name;
			source = setpoint;
		} else if (strcmp(suffix, ZPROP_INHERIT_SUFFIX) == 0) {
			/* Skip explicitly inherited entries. */
			continue;
		} else if (strcmp(suffix, ZPROP_RECVD_SUFFIX) == 0) {
			if (flags & DSL_PROP_GET_LOCAL)
				continue;

			(void) strncpy(buf, za.za_name, (suffix - za.za_name));
			buf[suffix - za.za_name] = '\0';
			propname = buf;

			if (!(flags & DSL_PROP_GET_RECEIVED)) {
				/* Skip if locally overridden. */
				err = zap_contains(mos, propobj, propname);
				if (err == 0)
					continue;
				if (err != ENOENT)
					break;

				/* Skip if explicitly inherited. */
				valstr = kmem_asprintf("%s%s", propname,
				    ZPROP_INHERIT_SUFFIX);
				err = zap_contains(mos, propobj, valstr);
				strfree(valstr);
				if (err == 0)
					continue;
				if (err != ENOENT)
					break;
			}

			source = ((flags & DSL_PROP_GET_INHERITING) ?
			    setpoint : ZPROP_SOURCE_VAL_RECVD);
		} else {
			/*
			 * For backward compatibility, skip suffixes we don't
			 * recognize.
			 */
			continue;
		}

		prop = zfs_name_to_prop(propname);

		/* Skip non-inheritable properties. */
		if ((flags & DSL_PROP_GET_INHERITING) && prop != ZPROP_INVAL &&
		    !zfs_prop_inheritable(prop))
			continue;

		/* Skip properties not valid for this type. */
		if ((flags & DSL_PROP_GET_SNAPSHOT) && prop != ZPROP_INVAL &&
		    !zfs_prop_valid_for_type(prop, ZFS_TYPE_SNAPSHOT, B_FALSE))
			continue;

		/* Skip properties already defined. */
		if (nvlist_exists(nv, propname))
			continue;

		VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		if (za.za_integer_length == 1) {
			/*
			 * String property
			 */
			char *tmp = kmem_alloc(za.za_num_integers,
			    KM_SLEEP);
			err = zap_lookup(mos, propobj,
			    za.za_name, 1, za.za_num_integers, tmp);
			if (err != 0) {
				kmem_free(tmp, za.za_num_integers);
				break;
			}
			VERIFY(nvlist_add_string(propval, ZPROP_VALUE,
			    tmp) == 0);
			kmem_free(tmp, za.za_num_integers);
		} else {
			/*
			 * Integer property
			 */
			ASSERT(za.za_integer_length == 8);
			(void) nvlist_add_uint64(propval, ZPROP_VALUE,
			    za.za_first_integer);
		}

		VERIFY(nvlist_add_string(propval, ZPROP_SOURCE, source) == 0);
		VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
		nvlist_free(propval);
	}
	zap_cursor_fini(&zc);
	if (err == ENOENT)
		err = 0;
	return (err);
}

/*
 * Iterate over all properties for this dataset and return them in an nvlist.
 */
static int
dsl_prop_get_all_ds(dsl_dataset_t *ds, nvlist_t **nvp,
    dsl_prop_getflags_t flags)
{
	dsl_dir_t *dd = ds->ds_dir;
	dsl_pool_t *dp = dd->dd_pool;
	objset_t *mos = dp->dp_meta_objset;
	int err = 0;
	char setpoint[ZFS_MAX_DATASET_NAME_LEN];

	VERIFY(nvlist_alloc(nvp, NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if (ds->ds_is_snapshot)
		flags |= DSL_PROP_GET_SNAPSHOT;

	ASSERT(dsl_pool_config_held(dp));

	if (dsl_dataset_phys(ds)->ds_props_obj != 0) {
		ASSERT(flags & DSL_PROP_GET_SNAPSHOT);
		dsl_dataset_name(ds, setpoint);
		err = dsl_prop_get_all_impl(mos,
		    dsl_dataset_phys(ds)->ds_props_obj, setpoint, flags, *nvp);
		if (err)
			goto out;
	}

	for (; dd != NULL; dd = dd->dd_parent) {
		if (dd != ds->ds_dir || (flags & DSL_PROP_GET_SNAPSHOT)) {
			if (flags & (DSL_PROP_GET_LOCAL |
			    DSL_PROP_GET_RECEIVED))
				break;
			flags |= DSL_PROP_GET_INHERITING;
		}
		dsl_dir_name(dd, setpoint);
		err = dsl_prop_get_all_impl(mos,
		    dsl_dir_phys(dd)->dd_props_zapobj, setpoint, flags, *nvp);
		if (err)
			break;
	}
out:
	if (err) {
		nvlist_free(*nvp);
		*nvp = NULL;
	}
	return (err);
}

boolean_t
dsl_prop_get_hasrecvd(const char *dsname)
{
	uint64_t dummy;

	return (0 ==
	    dsl_prop_get_integer(dsname, ZPROP_HAS_RECVD, &dummy, NULL));
}

static int
dsl_prop_set_hasrecvd_impl(const char *dsname, zprop_source_t source)
{
	uint64_t version;
	spa_t *spa;
	int error = 0;

	VERIFY0(spa_open(dsname, &spa, FTAG));
	version = spa_version(spa);
	spa_close(spa, FTAG);

	if (version >= SPA_VERSION_RECVD_PROPS)
		error = dsl_prop_set_int(dsname, ZPROP_HAS_RECVD, source, 0);
	return (error);
}

/*
 * Call after successfully receiving properties to ensure that only the first
 * receive on or after SPA_VERSION_RECVD_PROPS blows away local properties.
 */
int
dsl_prop_set_hasrecvd(const char *dsname)
{
	int error = 0;
	if (!dsl_prop_get_hasrecvd(dsname))
		error = dsl_prop_set_hasrecvd_impl(dsname, ZPROP_SRC_LOCAL);
	return (error);
}

void
dsl_prop_unset_hasrecvd(const char *dsname)
{
	VERIFY0(dsl_prop_set_hasrecvd_impl(dsname, ZPROP_SRC_NONE));
}

int
dsl_prop_get_all(objset_t *os, nvlist_t **nvp)
{
	return (dsl_prop_get_all_ds(os->os_dsl_dataset, nvp, 0));
}

int
dsl_prop_get_received(const char *dsname, nvlist_t **nvp)
{
	objset_t *os;
	int error;

	/*
	 * Received properties are not distinguishable from local properties
	 * until the dataset has received properties on or after
	 * SPA_VERSION_RECVD_PROPS.
	 */
	dsl_prop_getflags_t flags = (dsl_prop_get_hasrecvd(dsname) ?
	    DSL_PROP_GET_RECEIVED : DSL_PROP_GET_LOCAL);

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0)
		return (error);
	error = dsl_prop_get_all_ds(os->os_dsl_dataset, nvp, flags);
	dmu_objset_rele(os, FTAG);
	return (error);
}

void
dsl_prop_nvlist_add_uint64(nvlist_t *nv, zfs_prop_t prop, uint64_t value)
{
	nvlist_t *propval;
	const char *propname = zfs_prop_to_name(prop);
	uint64_t default_value;

	if (nvlist_lookup_nvlist(nv, propname, &propval) == 0) {
		VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
		return;
	}

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_uint64(propval, ZPROP_VALUE, value) == 0);
	/* Indicate the default source if we can. */
	if (dodefault(prop, 8, 1, &default_value) == 0 &&
	    value == default_value) {
		VERIFY(nvlist_add_string(propval, ZPROP_SOURCE, "") == 0);
	}
	VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
	nvlist_free(propval);
}

void
dsl_prop_nvlist_add_string(nvlist_t *nv, zfs_prop_t prop, const char *value)
{
	nvlist_t *propval;
	const char *propname = zfs_prop_to_name(prop);

	if (nvlist_lookup_nvlist(nv, propname, &propval) == 0) {
		VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
		return;
	}

	VERIFY(nvlist_alloc(&propval, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	VERIFY(nvlist_add_string(propval, ZPROP_VALUE, value) == 0);
	VERIFY(nvlist_add_nvlist(nv, propname, propval) == 0);
	nvlist_free(propval);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dsl_prop_register);
EXPORT_SYMBOL(dsl_prop_unregister);
EXPORT_SYMBOL(dsl_prop_unregister_all);
EXPORT_SYMBOL(dsl_prop_get);
EXPORT_SYMBOL(dsl_prop_get_integer);
EXPORT_SYMBOL(dsl_prop_get_all);
EXPORT_SYMBOL(dsl_prop_get_received);
EXPORT_SYMBOL(dsl_prop_get_ds);
EXPORT_SYMBOL(dsl_prop_get_int_ds);
EXPORT_SYMBOL(dsl_prop_get_dd);
EXPORT_SYMBOL(dsl_props_set);
EXPORT_SYMBOL(dsl_prop_set_int);
EXPORT_SYMBOL(dsl_prop_set_string);
EXPORT_SYMBOL(dsl_prop_inherit);
EXPORT_SYMBOL(dsl_prop_predict);
EXPORT_SYMBOL(dsl_prop_nvlist_add_uint64);
EXPORT_SYMBOL(dsl_prop_nvlist_add_string);
#endif
