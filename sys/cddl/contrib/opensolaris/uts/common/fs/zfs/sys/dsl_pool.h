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
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc.  All rights reserved.
 */

#ifndef	_SYS_DSL_POOL_H
#define	_SYS_DSL_POOL_H

#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/txg_impl.h>
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/dnode.h>
#include <sys/ddt.h>
#include <sys/arc.h>
#include <sys/bpobj.h>
#include <sys/bptree.h>
#include <sys/rrwlock.h>
#include <sys/dsl_synctask.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct objset;
struct dsl_dir;
struct dsl_dataset;
struct dsl_pool;
struct dmu_tx;
struct dsl_scan;

extern uint64_t zfs_dirty_data_max;
extern uint64_t zfs_dirty_data_max_max;
extern uint64_t zfs_dirty_data_sync;
extern int zfs_dirty_data_max_percent;
extern int zfs_delay_min_dirty_percent;
extern uint64_t zfs_delay_scale;

/* These macros are for indexing into the zfs_all_blkstats_t. */
#define	DMU_OT_DEFERRED	DMU_OT_NONE
#define	DMU_OT_OTHER	DMU_OT_NUMTYPES /* place holder for DMU_OT() types */
#define	DMU_OT_TOTAL	(DMU_OT_NUMTYPES + 1)

typedef struct zfs_blkstat {
	uint64_t	zb_count;
	uint64_t	zb_asize;
	uint64_t	zb_lsize;
	uint64_t	zb_psize;
	uint64_t	zb_gangs;
	uint64_t	zb_ditto_2_of_2_samevdev;
	uint64_t	zb_ditto_2_of_3_samevdev;
	uint64_t	zb_ditto_3_of_3_samevdev;
} zfs_blkstat_t;

typedef struct zfs_all_blkstats {
	zfs_blkstat_t	zab_type[DN_MAX_LEVELS + 1][DMU_OT_TOTAL + 1];
	kmutex_t	zab_lock;
} zfs_all_blkstats_t;


typedef struct dsl_pool {
	/* Immutable */
	spa_t *dp_spa;
	struct objset *dp_meta_objset;
	struct dsl_dir *dp_root_dir;
	struct dsl_dir *dp_mos_dir;
	struct dsl_dir *dp_free_dir;
	struct dsl_dir *dp_leak_dir;
	struct dsl_dataset *dp_origin_snap;
	uint64_t dp_root_dir_obj;
	struct taskq *dp_vnrele_taskq;

	/* No lock needed - sync context only */
	blkptr_t dp_meta_rootbp;
	uint64_t dp_tmp_userrefs_obj;
	bpobj_t dp_free_bpobj;
	uint64_t dp_bptree_obj;
	uint64_t dp_empty_bpobj;
	bpobj_t dp_obsolete_bpobj;

	struct dsl_scan *dp_scan;

	/* Uses dp_lock */
	kmutex_t dp_lock;
	kcondvar_t dp_spaceavail_cv;
	uint64_t dp_dirty_pertxg[TXG_SIZE];
	uint64_t dp_dirty_total;
	uint64_t dp_long_free_dirty_pertxg[TXG_SIZE];
	uint64_t dp_mos_used_delta;
	uint64_t dp_mos_compressed_delta;
	uint64_t dp_mos_uncompressed_delta;

	/*
	 * Time of most recently scheduled (furthest in the future)
	 * wakeup for delayed transactions.
	 */
	hrtime_t dp_last_wakeup;

	/* Has its own locking */
	tx_state_t dp_tx;
	txg_list_t dp_dirty_datasets;
	txg_list_t dp_dirty_zilogs;
	txg_list_t dp_dirty_dirs;
	txg_list_t dp_sync_tasks;
	txg_list_t dp_early_sync_tasks;
	taskq_t *dp_sync_taskq;
	taskq_t *dp_zil_clean_taskq;

	/*
	 * Protects administrative changes (properties, namespace)
	 *
	 * It is only held for write in syncing context.  Therefore
	 * syncing context does not need to ever have it for read, since
	 * nobody else could possibly have it for write.
	 */
	rrwlock_t dp_config_rwlock;

	zfs_all_blkstats_t *dp_blkstats;
} dsl_pool_t;

int dsl_pool_init(spa_t *spa, uint64_t txg, dsl_pool_t **dpp);
int dsl_pool_open(dsl_pool_t *dp);
void dsl_pool_close(dsl_pool_t *dp);
dsl_pool_t *dsl_pool_create(spa_t *spa, nvlist_t *zplprops, uint64_t txg);
void dsl_pool_sync(dsl_pool_t *dp, uint64_t txg);
void dsl_pool_sync_done(dsl_pool_t *dp, uint64_t txg);
int dsl_pool_sync_context(dsl_pool_t *dp);
uint64_t dsl_pool_adjustedsize(dsl_pool_t *dp, zfs_space_check_t slop_policy);
uint64_t dsl_pool_unreserved_space(dsl_pool_t *dp,
    zfs_space_check_t slop_policy);
void dsl_pool_dirty_space(dsl_pool_t *dp, int64_t space, dmu_tx_t *tx);
void dsl_pool_undirty_space(dsl_pool_t *dp, int64_t space, uint64_t txg);
void dsl_free(dsl_pool_t *dp, uint64_t txg, const blkptr_t *bpp);
void dsl_free_sync(zio_t *pio, dsl_pool_t *dp, uint64_t txg,
    const blkptr_t *bpp);
void dsl_pool_create_origin(dsl_pool_t *dp, dmu_tx_t *tx);
void dsl_pool_upgrade_clones(dsl_pool_t *dp, dmu_tx_t *tx);
void dsl_pool_upgrade_dir_clones(dsl_pool_t *dp, dmu_tx_t *tx);
void dsl_pool_mos_diduse_space(dsl_pool_t *dp,
    int64_t used, int64_t comp, int64_t uncomp);
void dsl_pool_ckpoint_diduse_space(dsl_pool_t *dp,
    int64_t used, int64_t comp, int64_t uncomp);
void dsl_pool_config_enter(dsl_pool_t *dp, void *tag);
void dsl_pool_config_enter_prio(dsl_pool_t *dp, void *tag);
void dsl_pool_config_exit(dsl_pool_t *dp, void *tag);
boolean_t dsl_pool_config_held(dsl_pool_t *dp);
boolean_t dsl_pool_config_held_writer(dsl_pool_t *dp);
boolean_t dsl_pool_need_dirty_delay(dsl_pool_t *dp);

taskq_t *dsl_pool_vnrele_taskq(dsl_pool_t *dp);

int dsl_pool_user_hold(dsl_pool_t *dp, uint64_t dsobj,
    const char *tag, uint64_t now, dmu_tx_t *tx);
int dsl_pool_user_release(dsl_pool_t *dp, uint64_t dsobj,
    const char *tag, dmu_tx_t *tx);
void dsl_pool_clean_tmp_userrefs(dsl_pool_t *dp);
int dsl_pool_open_special_dir(dsl_pool_t *dp, const char *name, dsl_dir_t **);
int dsl_pool_hold(const char *name, void *tag, dsl_pool_t **dp);
void dsl_pool_rele(dsl_pool_t *dp, void *tag);

void dsl_pool_create_obsolete_bpobj(dsl_pool_t *dp, dmu_tx_t *tx);
void dsl_pool_destroy_obsolete_bpobj(dsl_pool_t *dp, dmu_tx_t *tx);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_POOL_H */
