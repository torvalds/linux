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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/dsl_dataset.h>
#include <sys/dmu.h>
#include <sys/refcount.h>
#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/dsl_pool.h>

/*
 * Deadlist concurrency:
 *
 * Deadlists can only be modified from the syncing thread.
 *
 * Except for dsl_deadlist_insert(), it can only be modified with the
 * dp_config_rwlock held with RW_WRITER.
 *
 * The accessors (dsl_deadlist_space() and dsl_deadlist_space_range()) can
 * be called concurrently, from open context, with the dl_config_rwlock held
 * with RW_READER.
 *
 * Therefore, we only need to provide locking between dsl_deadlist_insert() and
 * the accessors, protecting:
 *     dl_phys->dl_used,comp,uncomp
 *     and protecting the dl_tree from being loaded.
 * The locking is provided by dl_lock.  Note that locking on the bpobj_t
 * provides its own locking, and dl_oldfmt is immutable.
 */

static int
dsl_deadlist_compare(const void *arg1, const void *arg2)
{
	const dsl_deadlist_entry_t *dle1 = (const dsl_deadlist_entry_t *)arg1;
	const dsl_deadlist_entry_t *dle2 = (const dsl_deadlist_entry_t *)arg2;

	return (AVL_CMP(dle1->dle_mintxg, dle2->dle_mintxg));
}

static void
dsl_deadlist_load_tree(dsl_deadlist_t *dl)
{
	zap_cursor_t zc;
	zap_attribute_t za;

	ASSERT(MUTEX_HELD(&dl->dl_lock));

	ASSERT(!dl->dl_oldfmt);
	if (dl->dl_havetree)
		return;

	avl_create(&dl->dl_tree, dsl_deadlist_compare,
	    sizeof (dsl_deadlist_entry_t),
	    offsetof(dsl_deadlist_entry_t, dle_node));
	for (zap_cursor_init(&zc, dl->dl_os, dl->dl_object);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		dsl_deadlist_entry_t *dle = kmem_alloc(sizeof (*dle), KM_SLEEP);
		dle->dle_mintxg = zfs_strtonum(za.za_name, NULL);
		VERIFY3U(0, ==, bpobj_open(&dle->dle_bpobj, dl->dl_os,
		    za.za_first_integer));
		avl_add(&dl->dl_tree, dle);
	}
	zap_cursor_fini(&zc);
	dl->dl_havetree = B_TRUE;
}

void
dsl_deadlist_open(dsl_deadlist_t *dl, objset_t *os, uint64_t object)
{
	dmu_object_info_t doi;

	ASSERT(!dsl_deadlist_is_open(dl));

	mutex_init(&dl->dl_lock, NULL, MUTEX_DEFAULT, NULL);
	dl->dl_os = os;
	dl->dl_object = object;
	VERIFY3U(0, ==, dmu_bonus_hold(os, object, dl, &dl->dl_dbuf));
	dmu_object_info_from_db(dl->dl_dbuf, &doi);
	if (doi.doi_type == DMU_OT_BPOBJ) {
		dmu_buf_rele(dl->dl_dbuf, dl);
		dl->dl_dbuf = NULL;
		dl->dl_oldfmt = B_TRUE;
		VERIFY3U(0, ==, bpobj_open(&dl->dl_bpobj, os, object));
		return;
	}

	dl->dl_oldfmt = B_FALSE;
	dl->dl_phys = dl->dl_dbuf->db_data;
	dl->dl_havetree = B_FALSE;
}

boolean_t
dsl_deadlist_is_open(dsl_deadlist_t *dl)
{
	return (dl->dl_os != NULL);
}

void
dsl_deadlist_close(dsl_deadlist_t *dl)
{
	void *cookie = NULL;
	dsl_deadlist_entry_t *dle;

	ASSERT(dsl_deadlist_is_open(dl));

	if (dl->dl_oldfmt) {
		dl->dl_oldfmt = B_FALSE;
		bpobj_close(&dl->dl_bpobj);
		dl->dl_os = NULL;
		dl->dl_object = 0;
		return;
	}

	if (dl->dl_havetree) {
		while ((dle = avl_destroy_nodes(&dl->dl_tree, &cookie))
		    != NULL) {
			bpobj_close(&dle->dle_bpobj);
			kmem_free(dle, sizeof (*dle));
		}
		avl_destroy(&dl->dl_tree);
	}
	dmu_buf_rele(dl->dl_dbuf, dl);
	mutex_destroy(&dl->dl_lock);
	dl->dl_dbuf = NULL;
	dl->dl_phys = NULL;
	dl->dl_os = NULL;
	dl->dl_object = 0;
}

uint64_t
dsl_deadlist_alloc(objset_t *os, dmu_tx_t *tx)
{
	if (spa_version(dmu_objset_spa(os)) < SPA_VERSION_DEADLISTS)
		return (bpobj_alloc(os, SPA_OLD_MAXBLOCKSIZE, tx));
	return (zap_create(os, DMU_OT_DEADLIST, DMU_OT_DEADLIST_HDR,
	    sizeof (dsl_deadlist_phys_t), tx));
}

void
dsl_deadlist_free(objset_t *os, uint64_t dlobj, dmu_tx_t *tx)
{
	dmu_object_info_t doi;
	zap_cursor_t zc;
	zap_attribute_t za;

	VERIFY3U(0, ==, dmu_object_info(os, dlobj, &doi));
	if (doi.doi_type == DMU_OT_BPOBJ) {
		bpobj_free(os, dlobj, tx);
		return;
	}

	for (zap_cursor_init(&zc, os, dlobj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t obj = za.za_first_integer;
		if (obj == dmu_objset_pool(os)->dp_empty_bpobj)
			bpobj_decr_empty(os, tx);
		else
			bpobj_free(os, obj, tx);
	}
	zap_cursor_fini(&zc);
	VERIFY3U(0, ==, dmu_object_free(os, dlobj, tx));
}

static void
dle_enqueue(dsl_deadlist_t *dl, dsl_deadlist_entry_t *dle,
    const blkptr_t *bp, dmu_tx_t *tx)
{
	ASSERT(MUTEX_HELD(&dl->dl_lock));
	if (dle->dle_bpobj.bpo_object ==
	    dmu_objset_pool(dl->dl_os)->dp_empty_bpobj) {
		uint64_t obj = bpobj_alloc(dl->dl_os, SPA_OLD_MAXBLOCKSIZE, tx);
		bpobj_close(&dle->dle_bpobj);
		bpobj_decr_empty(dl->dl_os, tx);
		VERIFY3U(0, ==, bpobj_open(&dle->dle_bpobj, dl->dl_os, obj));
		VERIFY3U(0, ==, zap_update_int_key(dl->dl_os, dl->dl_object,
		    dle->dle_mintxg, obj, tx));
	}
	bpobj_enqueue(&dle->dle_bpobj, bp, tx);
}

static void
dle_enqueue_subobj(dsl_deadlist_t *dl, dsl_deadlist_entry_t *dle,
    uint64_t obj, dmu_tx_t *tx)
{
	ASSERT(MUTEX_HELD(&dl->dl_lock));
	if (dle->dle_bpobj.bpo_object !=
	    dmu_objset_pool(dl->dl_os)->dp_empty_bpobj) {
		bpobj_enqueue_subobj(&dle->dle_bpobj, obj, tx);
	} else {
		bpobj_close(&dle->dle_bpobj);
		bpobj_decr_empty(dl->dl_os, tx);
		VERIFY3U(0, ==, bpobj_open(&dle->dle_bpobj, dl->dl_os, obj));
		VERIFY3U(0, ==, zap_update_int_key(dl->dl_os, dl->dl_object,
		    dle->dle_mintxg, obj, tx));
	}
}

void
dsl_deadlist_insert(dsl_deadlist_t *dl, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	avl_index_t where;

	if (dl->dl_oldfmt) {
		bpobj_enqueue(&dl->dl_bpobj, bp, tx);
		return;
	}

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dl->dl_phys->dl_used +=
	    bp_get_dsize_sync(dmu_objset_spa(dl->dl_os), bp);
	dl->dl_phys->dl_comp += BP_GET_PSIZE(bp);
	dl->dl_phys->dl_uncomp += BP_GET_UCSIZE(bp);

	dle_tofind.dle_mintxg = bp->blk_birth;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	if (dle == NULL)
		dle = avl_nearest(&dl->dl_tree, where, AVL_BEFORE);
	else
		dle = AVL_PREV(&dl->dl_tree, dle);
	dle_enqueue(dl, dle, bp, tx);
	mutex_exit(&dl->dl_lock);
}

/*
 * Insert new key in deadlist, which must be > all current entries.
 * mintxg is not inclusive.
 */
void
dsl_deadlist_add_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx)
{
	uint64_t obj;
	dsl_deadlist_entry_t *dle;

	if (dl->dl_oldfmt)
		return;

	dle = kmem_alloc(sizeof (*dle), KM_SLEEP);
	dle->dle_mintxg = mintxg;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	obj = bpobj_alloc_empty(dl->dl_os, SPA_OLD_MAXBLOCKSIZE, tx);
	VERIFY3U(0, ==, bpobj_open(&dle->dle_bpobj, dl->dl_os, obj));
	avl_add(&dl->dl_tree, dle);

	VERIFY3U(0, ==, zap_add_int_key(dl->dl_os, dl->dl_object,
	    mintxg, obj, tx));
	mutex_exit(&dl->dl_lock);
}

/*
 * Remove this key, merging its entries into the previous key.
 */
void
dsl_deadlist_remove_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle, *dle_prev;

	if (dl->dl_oldfmt)
		return;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	dle_tofind.dle_mintxg = mintxg;
	dle = avl_find(&dl->dl_tree, &dle_tofind, NULL);
	dle_prev = AVL_PREV(&dl->dl_tree, dle);

	dle_enqueue_subobj(dl, dle_prev, dle->dle_bpobj.bpo_object, tx);

	avl_remove(&dl->dl_tree, dle);
	bpobj_close(&dle->dle_bpobj);
	kmem_free(dle, sizeof (*dle));

	VERIFY3U(0, ==, zap_remove_int(dl->dl_os, dl->dl_object, mintxg, tx));
	mutex_exit(&dl->dl_lock);
}

/*
 * Walk ds's snapshots to regenerate generate ZAP & AVL.
 */
static void
dsl_deadlist_regenerate(objset_t *os, uint64_t dlobj,
    uint64_t mrs_obj, dmu_tx_t *tx)
{
	dsl_deadlist_t dl = { 0 };
	dsl_pool_t *dp = dmu_objset_pool(os);

	dsl_deadlist_open(&dl, os, dlobj);
	if (dl.dl_oldfmt) {
		dsl_deadlist_close(&dl);
		return;
	}

	while (mrs_obj != 0) {
		dsl_dataset_t *ds;
		VERIFY3U(0, ==, dsl_dataset_hold_obj(dp, mrs_obj, FTAG, &ds));
		dsl_deadlist_add_key(&dl,
		    dsl_dataset_phys(ds)->ds_prev_snap_txg, tx);
		mrs_obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_deadlist_close(&dl);
}

uint64_t
dsl_deadlist_clone(dsl_deadlist_t *dl, uint64_t maxtxg,
    uint64_t mrs_obj, dmu_tx_t *tx)
{
	dsl_deadlist_entry_t *dle;
	uint64_t newobj;

	newobj = dsl_deadlist_alloc(dl->dl_os, tx);

	if (dl->dl_oldfmt) {
		dsl_deadlist_regenerate(dl->dl_os, newobj, mrs_obj, tx);
		return (newobj);
	}

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);

	for (dle = avl_first(&dl->dl_tree); dle;
	    dle = AVL_NEXT(&dl->dl_tree, dle)) {
		uint64_t obj;

		if (dle->dle_mintxg >= maxtxg)
			break;

		obj = bpobj_alloc_empty(dl->dl_os, SPA_OLD_MAXBLOCKSIZE, tx);
		VERIFY3U(0, ==, zap_add_int_key(dl->dl_os, newobj,
		    dle->dle_mintxg, obj, tx));
	}
	mutex_exit(&dl->dl_lock);
	return (newobj);
}

void
dsl_deadlist_space(dsl_deadlist_t *dl,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	ASSERT(dsl_deadlist_is_open(dl));
	if (dl->dl_oldfmt) {
		VERIFY3U(0, ==, bpobj_space(&dl->dl_bpobj,
		    usedp, compp, uncompp));
		return;
	}

	mutex_enter(&dl->dl_lock);
	*usedp = dl->dl_phys->dl_used;
	*compp = dl->dl_phys->dl_comp;
	*uncompp = dl->dl_phys->dl_uncomp;
	mutex_exit(&dl->dl_lock);
}

/*
 * return space used in the range (mintxg, maxtxg].
 * Includes maxtxg, does not include mintxg.
 * mintxg and maxtxg must both be keys in the deadlist (unless maxtxg is
 * larger than any bp in the deadlist (eg. UINT64_MAX)).
 */
void
dsl_deadlist_space_range(dsl_deadlist_t *dl, uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	dsl_deadlist_entry_t *dle;
	dsl_deadlist_entry_t dle_tofind;
	avl_index_t where;

	if (dl->dl_oldfmt) {
		VERIFY3U(0, ==, bpobj_space_range(&dl->dl_bpobj,
		    mintxg, maxtxg, usedp, compp, uncompp));
		return;
	}

	*usedp = *compp = *uncompp = 0;

	mutex_enter(&dl->dl_lock);
	dsl_deadlist_load_tree(dl);
	dle_tofind.dle_mintxg = mintxg;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	/*
	 * If we don't find this mintxg, there shouldn't be anything
	 * after it either.
	 */
	ASSERT(dle != NULL ||
	    avl_nearest(&dl->dl_tree, where, AVL_AFTER) == NULL);

	for (; dle && dle->dle_mintxg < maxtxg;
	    dle = AVL_NEXT(&dl->dl_tree, dle)) {
		uint64_t used, comp, uncomp;

		VERIFY3U(0, ==, bpobj_space(&dle->dle_bpobj,
		    &used, &comp, &uncomp));

		*usedp += used;
		*compp += comp;
		*uncompp += uncomp;
	}
	mutex_exit(&dl->dl_lock);
}

static void
dsl_deadlist_insert_bpobj(dsl_deadlist_t *dl, uint64_t obj, uint64_t birth,
    dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	avl_index_t where;
	uint64_t used, comp, uncomp;
	bpobj_t bpo;

	ASSERT(MUTEX_HELD(&dl->dl_lock));

	VERIFY3U(0, ==, bpobj_open(&bpo, dl->dl_os, obj));
	VERIFY3U(0, ==, bpobj_space(&bpo, &used, &comp, &uncomp));
	bpobj_close(&bpo);

	dsl_deadlist_load_tree(dl);

	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dl->dl_phys->dl_used += used;
	dl->dl_phys->dl_comp += comp;
	dl->dl_phys->dl_uncomp += uncomp;

	dle_tofind.dle_mintxg = birth;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	if (dle == NULL)
		dle = avl_nearest(&dl->dl_tree, where, AVL_BEFORE);
	dle_enqueue_subobj(dl, dle, obj, tx);
}

static int
dsl_deadlist_insert_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_deadlist_t *dl = arg;
	dsl_deadlist_insert(dl, bp, tx);
	return (0);
}

/*
 * Merge the deadlist pointed to by 'obj' into dl.  obj will be left as
 * an empty deadlist.
 */
void
dsl_deadlist_merge(dsl_deadlist_t *dl, uint64_t obj, dmu_tx_t *tx)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	dmu_buf_t *bonus;
	dsl_deadlist_phys_t *dlp;
	dmu_object_info_t doi;

	VERIFY3U(0, ==, dmu_object_info(dl->dl_os, obj, &doi));
	if (doi.doi_type == DMU_OT_BPOBJ) {
		bpobj_t bpo;
		VERIFY3U(0, ==, bpobj_open(&bpo, dl->dl_os, obj));
		VERIFY3U(0, ==, bpobj_iterate(&bpo,
		    dsl_deadlist_insert_cb, dl, tx));
		bpobj_close(&bpo);
		return;
	}

	mutex_enter(&dl->dl_lock);
	for (zap_cursor_init(&zc, dl->dl_os, obj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t mintxg = zfs_strtonum(za.za_name, NULL);
		dsl_deadlist_insert_bpobj(dl, za.za_first_integer, mintxg, tx);
		VERIFY3U(0, ==, zap_remove_int(dl->dl_os, obj, mintxg, tx));
	}
	zap_cursor_fini(&zc);

	VERIFY3U(0, ==, dmu_bonus_hold(dl->dl_os, obj, FTAG, &bonus));
	dlp = bonus->db_data;
	dmu_buf_will_dirty(bonus, tx);
	bzero(dlp, sizeof (*dlp));
	dmu_buf_rele(bonus, FTAG);
	mutex_exit(&dl->dl_lock);
}

/*
 * Remove entries on dl that are >= mintxg, and put them on the bpobj.
 */
void
dsl_deadlist_move_bpobj(dsl_deadlist_t *dl, bpobj_t *bpo, uint64_t mintxg,
    dmu_tx_t *tx)
{
	dsl_deadlist_entry_t dle_tofind;
	dsl_deadlist_entry_t *dle;
	avl_index_t where;

	ASSERT(!dl->dl_oldfmt);

	mutex_enter(&dl->dl_lock);
	dmu_buf_will_dirty(dl->dl_dbuf, tx);
	dsl_deadlist_load_tree(dl);

	dle_tofind.dle_mintxg = mintxg;
	dle = avl_find(&dl->dl_tree, &dle_tofind, &where);
	if (dle == NULL)
		dle = avl_nearest(&dl->dl_tree, where, AVL_AFTER);
	while (dle) {
		uint64_t used, comp, uncomp;
		dsl_deadlist_entry_t *dle_next;

		bpobj_enqueue_subobj(bpo, dle->dle_bpobj.bpo_object, tx);

		VERIFY3U(0, ==, bpobj_space(&dle->dle_bpobj,
		    &used, &comp, &uncomp));
		ASSERT3U(dl->dl_phys->dl_used, >=, used);
		ASSERT3U(dl->dl_phys->dl_comp, >=, comp);
		ASSERT3U(dl->dl_phys->dl_uncomp, >=, uncomp);
		dl->dl_phys->dl_used -= used;
		dl->dl_phys->dl_comp -= comp;
		dl->dl_phys->dl_uncomp -= uncomp;

		VERIFY3U(0, ==, zap_remove_int(dl->dl_os, dl->dl_object,
		    dle->dle_mintxg, tx));

		dle_next = AVL_NEXT(&dl->dl_tree, dle);
		avl_remove(&dl->dl_tree, dle);
		bpobj_close(&dle->dle_bpobj);
		kmem_free(dle, sizeof (*dle));
		dle = dle_next;
	}
	mutex_exit(&dl->dl_lock);
}
