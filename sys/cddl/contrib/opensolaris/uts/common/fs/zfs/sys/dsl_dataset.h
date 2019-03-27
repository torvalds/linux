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
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#ifndef	_SYS_DSL_DATASET_H
#define	_SYS_DSL_DATASET_H

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/bplist.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_context.h>
#include <sys/dsl_deadlist.h>
#include <sys/refcount.h>
#include <sys/rrwlock.h>
#include <zfeature_common.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_dataset;
struct dsl_dir;
struct dsl_pool;

#define	DS_FLAG_INCONSISTENT	(1ULL<<0)
#define	DS_IS_INCONSISTENT(ds)	\
	(dsl_dataset_phys(ds)->ds_flags & DS_FLAG_INCONSISTENT)

/*
 * Do not allow this dataset to be promoted.
 */
#define	DS_FLAG_NOPROMOTE	(1ULL<<1)

/*
 * DS_FLAG_UNIQUE_ACCURATE is set if ds_unique_bytes has been correctly
 * calculated for head datasets (starting with SPA_VERSION_UNIQUE_ACCURATE,
 * refquota/refreservations).
 */
#define	DS_FLAG_UNIQUE_ACCURATE	(1ULL<<2)

/*
 * DS_FLAG_DEFER_DESTROY is set after 'zfs destroy -d' has been called
 * on a dataset. This allows the dataset to be destroyed using 'zfs release'.
 */
#define	DS_FLAG_DEFER_DESTROY	(1ULL<<3)
#define	DS_IS_DEFER_DESTROY(ds)	\
	(dsl_dataset_phys(ds)->ds_flags & DS_FLAG_DEFER_DESTROY)

/*
 * DS_FIELD_* are strings that are used in the "extensified" dataset zap object.
 * They should be of the format <reverse-dns>:<field>.
 */

/*
 * This field's value is the object ID of a zap object which contains the
 * bookmarks of this dataset.  If it is present, then this dataset is counted
 * in the refcount of the SPA_FEATURES_BOOKMARKS feature.
 */
#define	DS_FIELD_BOOKMARK_NAMES "com.delphix:bookmarks"

/*
 * This field is present (with value=0) if this dataset may contain large
 * dnodes (>512B).  If it is present, then this dataset is counted in the
 * refcount of the SPA_FEATURE_LARGE_DNODE feature.
 */
#define	DS_FIELD_LARGE_DNODE "org.zfsonlinux:large_dnode"

/*
 * These fields are set on datasets that are in the middle of a resumable
 * receive, and allow the sender to resume the send if it is interrupted.
 */
#define	DS_FIELD_RESUME_FROMGUID "com.delphix:resume_fromguid"
#define	DS_FIELD_RESUME_TONAME "com.delphix:resume_toname"
#define	DS_FIELD_RESUME_TOGUID "com.delphix:resume_toguid"
#define	DS_FIELD_RESUME_OBJECT "com.delphix:resume_object"
#define	DS_FIELD_RESUME_OFFSET "com.delphix:resume_offset"
#define	DS_FIELD_RESUME_BYTES "com.delphix:resume_bytes"
#define	DS_FIELD_RESUME_LARGEBLOCK "com.delphix:resume_largeblockok"
#define	DS_FIELD_RESUME_EMBEDOK "com.delphix:resume_embedok"
#define	DS_FIELD_RESUME_COMPRESSOK "com.delphix:resume_compressok"

/*
 * This field is set to the object number of the remap deadlist if one exists.
 */
#define	DS_FIELD_REMAP_DEADLIST	"com.delphix:remap_deadlist"

/*
 * DS_FLAG_CI_DATASET is set if the dataset contains a file system whose
 * name lookups should be performed case-insensitively.
 */
#define	DS_FLAG_CI_DATASET	(1ULL<<16)

#define	DS_CREATE_FLAG_NODIRTY	(1ULL<<24)

typedef struct dsl_dataset_phys {
	uint64_t ds_dir_obj;		/* DMU_OT_DSL_DIR */
	uint64_t ds_prev_snap_obj;	/* DMU_OT_DSL_DATASET */
	uint64_t ds_prev_snap_txg;
	uint64_t ds_next_snap_obj;	/* DMU_OT_DSL_DATASET */
	uint64_t ds_snapnames_zapobj;	/* DMU_OT_DSL_DS_SNAP_MAP 0 for snaps */
	uint64_t ds_num_children;	/* clone/snap children; ==0 for head */
	uint64_t ds_creation_time;	/* seconds since 1970 */
	uint64_t ds_creation_txg;
	uint64_t ds_deadlist_obj;	/* DMU_OT_DEADLIST */
	/*
	 * ds_referenced_bytes, ds_compressed_bytes, and ds_uncompressed_bytes
	 * include all blocks referenced by this dataset, including those
	 * shared with any other datasets.
	 */
	uint64_t ds_referenced_bytes;
	uint64_t ds_compressed_bytes;
	uint64_t ds_uncompressed_bytes;
	uint64_t ds_unique_bytes;	/* only relevant to snapshots */
	/*
	 * The ds_fsid_guid is a 56-bit ID that can change to avoid
	 * collisions.  The ds_guid is a 64-bit ID that will never
	 * change, so there is a small probability that it will collide.
	 */
	uint64_t ds_fsid_guid;
	uint64_t ds_guid;
	uint64_t ds_flags;		/* DS_FLAG_* */
	blkptr_t ds_bp;
	uint64_t ds_next_clones_obj;	/* DMU_OT_DSL_CLONES */
	uint64_t ds_props_obj;		/* DMU_OT_DSL_PROPS for snaps */
	uint64_t ds_userrefs_obj;	/* DMU_OT_USERREFS */
	uint64_t ds_pad[5]; /* pad out to 320 bytes for good measure */
} dsl_dataset_phys_t;

typedef struct dsl_dataset {
	dmu_buf_user_t ds_dbu;
	rrwlock_t ds_bp_rwlock; /* Protects ds_phys->ds_bp */

	/* Immutable: */
	struct dsl_dir *ds_dir;
	dmu_buf_t *ds_dbuf;
	uint64_t ds_object;
	uint64_t ds_fsid_guid;
	boolean_t ds_is_snapshot;

	/* only used in syncing context, only valid for non-snapshots: */
	struct dsl_dataset *ds_prev;
	uint64_t ds_bookmarks;  /* DMU_OTN_ZAP_METADATA */

	/* has internal locking: */
	dsl_deadlist_t ds_deadlist;
	bplist_t ds_pending_deadlist;

	/*
	 * The remap deadlist contains blocks (DVA's, really) that are
	 * referenced by the previous snapshot and point to indirect vdevs,
	 * but in this dataset they have been remapped to point to concrete
	 * (or at least, less-indirect) vdevs.  In other words, the
	 * physical DVA is referenced by the previous snapshot but not by
	 * this dataset.  Logically, the DVA continues to be referenced,
	 * but we are using a different (less indirect) physical DVA.
	 * This deadlist is used to determine when physical DVAs that
	 * point to indirect vdevs are no longer referenced anywhere,
	 * and thus should be marked obsolete.
	 *
	 * This is only used if SPA_FEATURE_OBSOLETE_COUNTS is enabled.
	 */
	dsl_deadlist_t ds_remap_deadlist;
	/* protects creation of the ds_remap_deadlist */
	kmutex_t ds_remap_deadlist_lock;

	/* protected by lock on pool's dp_dirty_datasets list */
	txg_node_t ds_dirty_link;
	list_node_t ds_synced_link;

	/*
	 * ds_phys->ds_<accounting> is also protected by ds_lock.
	 * Protected by ds_lock:
	 */
	kmutex_t ds_lock;
	objset_t *ds_objset;
	uint64_t ds_userrefs;
	void *ds_owner;

	/*
	 * Long holds prevent the ds from being destroyed; they allow the
	 * ds to remain held even after dropping the dp_config_rwlock.
	 * Owning counts as a long hold.  See the comments above
	 * dsl_pool_hold() for details.
	 */
	refcount_t ds_longholds;

	/* no locking; only for making guesses */
	uint64_t ds_trysnap_txg;

	/* for objset_open() */
	kmutex_t ds_opening_lock;

	uint64_t ds_reserved;	/* cached refreservation */
	uint64_t ds_quota;	/* cached refquota */

	kmutex_t ds_sendstream_lock;
	list_t ds_sendstreams;

	/*
	 * When in the middle of a resumable receive, tracks how much
	 * progress we have made.
	 */
	uint64_t ds_resume_object[TXG_SIZE];
	uint64_t ds_resume_offset[TXG_SIZE];
	uint64_t ds_resume_bytes[TXG_SIZE];

	/* Protected by our dsl_dir's dd_lock */
	list_t ds_prop_cbs;

	/*
	 * For ZFEATURE_FLAG_PER_DATASET features, set if this dataset
	 * uses this feature.
	 */
	uint8_t ds_feature_inuse[SPA_FEATURES];

	/*
	 * Set if we need to activate the feature on this dataset this txg
	 * (used only in syncing context).
	 */
	uint8_t ds_feature_activation_needed[SPA_FEATURES];

	/* Protected by ds_lock; keep at end of struct for better locality */
	char ds_snapname[ZFS_MAX_DATASET_NAME_LEN];
} dsl_dataset_t;

inline dsl_dataset_phys_t *
dsl_dataset_phys(dsl_dataset_t *ds)
{
	return (ds->ds_dbuf->db_data);
}

typedef struct dsl_dataset_promote_arg {
	const char *ddpa_clonename;
	dsl_dataset_t *ddpa_clone;
	list_t shared_snaps, origin_snaps, clone_snaps;
	dsl_dataset_t *origin_origin; /* origin of the origin */
	uint64_t used, comp, uncomp, unique, cloneusedsnap, originusedsnap;
	nvlist_t *err_ds;
	cred_t *cr;
} dsl_dataset_promote_arg_t;

typedef struct dsl_dataset_rollback_arg {
	const char *ddra_fsname;
	const char *ddra_tosnap;
	void *ddra_owner;
	nvlist_t *ddra_result;
} dsl_dataset_rollback_arg_t;

typedef struct dsl_dataset_snapshot_arg {
	nvlist_t *ddsa_snaps;
	nvlist_t *ddsa_props;
	nvlist_t *ddsa_errors;
	cred_t *ddsa_cr;
} dsl_dataset_snapshot_arg_t;

/*
 * The max length of a temporary tag prefix is the number of hex digits
 * required to express UINT64_MAX plus one for the hyphen.
 */
#define	MAX_TAG_PREFIX_LEN	17

#define	dsl_dataset_is_snapshot(ds) \
	(dsl_dataset_phys(ds)->ds_num_children != 0)

#define	DS_UNIQUE_IS_ACCURATE(ds)	\
	((dsl_dataset_phys(ds)->ds_flags & DS_FLAG_UNIQUE_ACCURATE) != 0)

int dsl_dataset_hold(struct dsl_pool *dp, const char *name, void *tag,
    dsl_dataset_t **dsp);
boolean_t dsl_dataset_try_add_ref(struct dsl_pool *dp, dsl_dataset_t *ds,
    void *tag);
int dsl_dataset_hold_obj(struct dsl_pool *dp, uint64_t dsobj, void *tag,
    dsl_dataset_t **);
void dsl_dataset_rele(dsl_dataset_t *ds, void *tag);
int dsl_dataset_own(struct dsl_pool *dp, const char *name,
    void *tag, dsl_dataset_t **dsp);
int dsl_dataset_own_obj(struct dsl_pool *dp, uint64_t dsobj,
    void *tag, dsl_dataset_t **dsp);
void dsl_dataset_disown(dsl_dataset_t *ds, void *tag);
void dsl_dataset_name(dsl_dataset_t *ds, char *name);
boolean_t dsl_dataset_tryown(dsl_dataset_t *ds, void *tag);
int dsl_dataset_namelen(dsl_dataset_t *ds);
boolean_t dsl_dataset_has_owner(dsl_dataset_t *ds);
uint64_t dsl_dataset_create_sync(dsl_dir_t *pds, const char *lastname,
    dsl_dataset_t *origin, uint64_t flags, cred_t *, dmu_tx_t *);
uint64_t dsl_dataset_create_sync_dd(dsl_dir_t *dd, dsl_dataset_t *origin,
    uint64_t flags, dmu_tx_t *tx);
void dsl_dataset_snapshot_sync(void *arg, dmu_tx_t *tx);
int dsl_dataset_snapshot_check(void *arg, dmu_tx_t *tx);
int dsl_dataset_snapshot(nvlist_t *snaps, nvlist_t *props, nvlist_t *errors);
void dsl_dataset_promote_sync(void *arg, dmu_tx_t *tx);
int dsl_dataset_promote_check(void *arg, dmu_tx_t *tx);
int dsl_dataset_promote(const char *name, char *conflsnap);
int dsl_dataset_clone_swap(dsl_dataset_t *clone, dsl_dataset_t *origin_head,
    boolean_t force);
int dsl_dataset_rename_snapshot(const char *fsname,
    const char *oldsnapname, const char *newsnapname, boolean_t recursive);
int dsl_dataset_snapshot_tmp(const char *fsname, const char *snapname,
    minor_t cleanup_minor, const char *htag);

blkptr_t *dsl_dataset_get_blkptr(dsl_dataset_t *ds);

spa_t *dsl_dataset_get_spa(dsl_dataset_t *ds);

boolean_t dsl_dataset_modified_since_snap(dsl_dataset_t *ds,
    dsl_dataset_t *snap);

void dsl_dataset_sync(dsl_dataset_t *os, zio_t *zio, dmu_tx_t *tx);
void dsl_dataset_sync_done(dsl_dataset_t *os, dmu_tx_t *tx);

void dsl_dataset_block_born(dsl_dataset_t *ds, const blkptr_t *bp,
    dmu_tx_t *tx);
int dsl_dataset_block_kill(dsl_dataset_t *ds, const blkptr_t *bp,
    dmu_tx_t *tx, boolean_t async);
void dsl_dataset_block_remapped(dsl_dataset_t *ds, uint64_t vdev,
    uint64_t offset, uint64_t size, uint64_t birth, dmu_tx_t *tx);

void dsl_dataset_dirty(dsl_dataset_t *ds, dmu_tx_t *tx);

int get_clones_stat_impl(dsl_dataset_t *ds, nvlist_t *val);
char *get_receive_resume_stats_impl(dsl_dataset_t *ds);
char *get_child_receive_stats(dsl_dataset_t *ds);
uint64_t dsl_get_refratio(dsl_dataset_t *ds);
uint64_t dsl_get_logicalreferenced(dsl_dataset_t *ds);
uint64_t dsl_get_compressratio(dsl_dataset_t *ds);
uint64_t dsl_get_used(dsl_dataset_t *ds);
uint64_t dsl_get_creation(dsl_dataset_t *ds);
uint64_t dsl_get_creationtxg(dsl_dataset_t *ds);
uint64_t dsl_get_refquota(dsl_dataset_t *ds);
uint64_t dsl_get_refreservation(dsl_dataset_t *ds);
uint64_t dsl_get_guid(dsl_dataset_t *ds);
uint64_t dsl_get_unique(dsl_dataset_t *ds);
uint64_t dsl_get_objsetid(dsl_dataset_t *ds);
uint64_t dsl_get_userrefs(dsl_dataset_t *ds);
uint64_t dsl_get_defer_destroy(dsl_dataset_t *ds);
uint64_t dsl_get_referenced(dsl_dataset_t *ds);
uint64_t dsl_get_numclones(dsl_dataset_t *ds);
uint64_t dsl_get_inconsistent(dsl_dataset_t *ds);
uint64_t dsl_get_available(dsl_dataset_t *ds);
int dsl_get_written(dsl_dataset_t *ds, uint64_t *written);
int dsl_get_prev_snap(dsl_dataset_t *ds, char *snap);
int dsl_get_mountpoint(dsl_dataset_t *ds, const char *dsname, char *value,
    char *source);

void get_clones_stat(dsl_dataset_t *ds, nvlist_t *nv);

void dsl_dataset_stats(dsl_dataset_t *os, nvlist_t *nv);

void dsl_dataset_fast_stat(dsl_dataset_t *ds, dmu_objset_stats_t *stat);
void dsl_dataset_space(dsl_dataset_t *ds,
    uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp);
uint64_t dsl_dataset_fsid_guid(dsl_dataset_t *ds);
int dsl_dataset_space_written(dsl_dataset_t *oldsnap, dsl_dataset_t *new,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
int dsl_dataset_space_wouldfree(dsl_dataset_t *firstsnap, dsl_dataset_t *last,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
boolean_t dsl_dataset_is_dirty(dsl_dataset_t *ds);

int dsl_dsobj_to_dsname(char *pname, uint64_t obj, char *buf);

int dsl_dataset_check_quota(dsl_dataset_t *ds, boolean_t check_quota,
    uint64_t asize, uint64_t inflight, uint64_t *used,
    uint64_t *ref_rsrv);
int dsl_dataset_set_refquota(const char *dsname, zprop_source_t source,
    uint64_t quota);
int dsl_dataset_set_refreservation(const char *dsname, zprop_source_t source,
    uint64_t reservation);

boolean_t dsl_dataset_is_before(dsl_dataset_t *later, dsl_dataset_t *earlier,
    uint64_t earlier_txg);
void dsl_dataset_long_hold(dsl_dataset_t *ds, void *tag);
void dsl_dataset_long_rele(dsl_dataset_t *ds, void *tag);
boolean_t dsl_dataset_long_held(dsl_dataset_t *ds);

int dsl_dataset_clone_swap_check_impl(dsl_dataset_t *clone,
    dsl_dataset_t *origin_head, boolean_t force, void *owner, dmu_tx_t *tx);
void dsl_dataset_clone_swap_sync_impl(dsl_dataset_t *clone,
    dsl_dataset_t *origin_head, dmu_tx_t *tx);
int dsl_dataset_snapshot_check_impl(dsl_dataset_t *ds, const char *snapname,
    dmu_tx_t *tx, boolean_t recv, uint64_t cnt, cred_t *cr);
void dsl_dataset_snapshot_sync_impl(dsl_dataset_t *ds, const char *snapname,
    dmu_tx_t *tx);

void dsl_dataset_remove_from_next_clones(dsl_dataset_t *ds, uint64_t obj,
    dmu_tx_t *tx);
void dsl_dataset_recalc_head_uniq(dsl_dataset_t *ds);
int dsl_dataset_get_snapname(dsl_dataset_t *ds);
int dsl_dataset_snap_lookup(dsl_dataset_t *ds, const char *name,
    uint64_t *value);
int dsl_dataset_snap_remove(dsl_dataset_t *ds, const char *name, dmu_tx_t *tx,
    boolean_t adj_cnt);
void dsl_dataset_set_refreservation_sync_impl(dsl_dataset_t *ds,
    zprop_source_t source, uint64_t value, dmu_tx_t *tx);
void dsl_dataset_zapify(dsl_dataset_t *ds, dmu_tx_t *tx);
boolean_t dsl_dataset_is_zapified(dsl_dataset_t *ds);
boolean_t dsl_dataset_has_resume_receive_state(dsl_dataset_t *ds);

int dsl_dataset_rollback_check(void *arg, dmu_tx_t *tx);
void dsl_dataset_rollback_sync(void *arg, dmu_tx_t *tx);
int dsl_dataset_rollback(const char *fsname, const char *tosnap, void *owner,
    nvlist_t *result);

uint64_t dsl_dataset_get_remap_deadlist_object(dsl_dataset_t *ds);
void dsl_dataset_create_remap_deadlist(dsl_dataset_t *ds, dmu_tx_t *tx);
boolean_t dsl_dataset_remap_deadlist_exists(dsl_dataset_t *ds);
void dsl_dataset_destroy_remap_deadlist(dsl_dataset_t *ds, dmu_tx_t *tx);

void dsl_dataset_deactivate_feature(uint64_t dsobj,
    spa_feature_t f, dmu_tx_t *tx);

#ifdef ZFS_DEBUG
#define	dprintf_ds(ds, fmt, ...) do { \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { \
	char *__ds_name = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP); \
	dsl_dataset_name(ds, __ds_name); \
	dprintf("ds=%s " fmt, __ds_name, __VA_ARGS__); \
	kmem_free(__ds_name, ZFS_MAX_DATASET_NAME_LEN); \
	} \
_NOTE(CONSTCOND) } while (0)
#else
#define	dprintf_ds(dd, fmt, ...)
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_DATASET_H */
