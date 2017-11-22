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
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 */

#ifndef _SYS_SPA_IMPL_H
#define	_SYS_SPA_IMPL_H

#include <sys/spa.h>
#include <sys/vdev.h>
#include <sys/metaslab.h>
#include <sys/dmu.h>
#include <sys/dsl_pool.h>
#include <sys/uberblock_impl.h>
#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <sys/refcount.h>
#include <sys/bplist.h>
#include <sys/bpobj.h>
#include <sys/zfeature.h>
#include <zfeature_common.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct spa_error_entry {
	zbookmark_phys_t	se_bookmark;
	char			*se_name;
	avl_node_t		se_avl;
} spa_error_entry_t;

typedef struct spa_history_phys {
	uint64_t sh_pool_create_len;	/* ending offset of zpool create */
	uint64_t sh_phys_max_off;	/* physical EOF */
	uint64_t sh_bof;		/* logical BOF */
	uint64_t sh_eof;		/* logical EOF */
	uint64_t sh_records_lost;	/* num of records overwritten */
} spa_history_phys_t;

struct spa_aux_vdev {
	uint64_t	sav_object;		/* MOS object for device list */
	nvlist_t	*sav_config;		/* cached device config */
	vdev_t		**sav_vdevs;		/* devices */
	int		sav_count;		/* number devices */
	boolean_t	sav_sync;		/* sync the device list */
	nvlist_t	**sav_pending;		/* pending device additions */
	uint_t		sav_npending;		/* # pending devices */
};

typedef struct spa_config_lock {
	kmutex_t	scl_lock;
	kthread_t	*scl_writer;
	int		scl_write_wanted;
	kcondvar_t	scl_cv;
	refcount_t	scl_count;
} spa_config_lock_t;

typedef struct spa_config_dirent {
	list_node_t	scd_link;
	char		*scd_path;
} spa_config_dirent_t;

typedef enum zio_taskq_type {
	ZIO_TASKQ_ISSUE = 0,
	ZIO_TASKQ_ISSUE_HIGH,
	ZIO_TASKQ_INTERRUPT,
	ZIO_TASKQ_INTERRUPT_HIGH,
	ZIO_TASKQ_TYPES
} zio_taskq_type_t;

/*
 * State machine for the zpool-poolname process.  The states transitions
 * are done as follows:
 *
 *	From		   To			Routine
 *	PROC_NONE	-> PROC_CREATED		spa_activate()
 *	PROC_CREATED	-> PROC_ACTIVE		spa_thread()
 *	PROC_ACTIVE	-> PROC_DEACTIVATE	spa_deactivate()
 *	PROC_DEACTIVATE	-> PROC_GONE		spa_thread()
 *	PROC_GONE	-> PROC_NONE		spa_deactivate()
 */
typedef enum spa_proc_state {
	SPA_PROC_NONE,		/* spa_proc = &p0, no process created */
	SPA_PROC_CREATED,	/* spa_activate() has proc, is waiting */
	SPA_PROC_ACTIVE,	/* taskqs created, spa_proc set */
	SPA_PROC_DEACTIVATE,	/* spa_deactivate() requests process exit */
	SPA_PROC_GONE		/* spa_thread() is exiting, spa_proc = &p0 */
} spa_proc_state_t;

typedef struct spa_taskqs {
	uint_t stqs_count;
	taskq_t **stqs_taskq;
} spa_taskqs_t;

typedef enum spa_all_vdev_zap_action {
	AVZ_ACTION_NONE = 0,
	AVZ_ACTION_DESTROY,	/* Destroy all per-vdev ZAPs and the AVZ. */
	AVZ_ACTION_REBUILD,	/* Populate the new AVZ, see spa_avz_rebuild */
	AVZ_ACTION_INITIALIZE
} spa_avz_action_t;

struct spa {
	/*
	 * Fields protected by spa_namespace_lock.
	 */
	char		spa_name[ZFS_MAX_DATASET_NAME_LEN];	/* pool name */
	char		*spa_comment;		/* comment */
	avl_node_t	spa_avl;		/* node in spa_namespace_avl */
	nvlist_t	*spa_config;		/* last synced config */
	nvlist_t	*spa_config_syncing;	/* currently syncing config */
	nvlist_t	*spa_config_splitting;	/* config for splitting */
	nvlist_t	*spa_load_info;		/* info and errors from load */
	uint64_t	spa_config_txg;		/* txg of last config change */
	int		spa_sync_pass;		/* iterate-to-convergence */
	pool_state_t	spa_state;		/* pool state */
	int		spa_inject_ref;		/* injection references */
	uint8_t		spa_sync_on;		/* sync threads are running */
	spa_load_state_t spa_load_state;	/* current load operation */
	uint64_t	spa_import_flags;	/* import specific flags */
	spa_taskqs_t	spa_zio_taskq[ZIO_TYPES][ZIO_TASKQ_TYPES];
	dsl_pool_t	*spa_dsl_pool;
	boolean_t	spa_is_initializing;	/* true while opening pool */
	metaslab_class_t *spa_normal_class;	/* normal data class */
	metaslab_class_t *spa_log_class;	/* intent log data class */
	uint64_t	spa_first_txg;		/* first txg after spa_open() */
	uint64_t	spa_final_txg;		/* txg of export/destroy */
	uint64_t	spa_freeze_txg;		/* freeze pool at this txg */
	uint64_t	spa_load_max_txg;	/* best initial ub_txg */
	uint64_t	spa_claim_max_txg;	/* highest claimed birth txg */
	timespec_t	spa_loaded_ts;		/* 1st successful open time */
	objset_t	*spa_meta_objset;	/* copy of dp->dp_meta_objset */
	kmutex_t	spa_evicting_os_lock;	/* Evicting objset list lock */
	list_t		spa_evicting_os_list;	/* Objsets being evicted. */
	kcondvar_t	spa_evicting_os_cv;	/* Objset Eviction Completion */
	txg_list_t	spa_vdev_txg_list;	/* per-txg dirty vdev list */
	vdev_t		*spa_root_vdev;		/* top-level vdev container */
	int		spa_min_ashift;		/* of vdevs in normal class */
	int		spa_max_ashift;		/* of vdevs in normal class */
	uint64_t	spa_config_guid;	/* config pool guid */
	uint64_t	spa_load_guid;		/* spa_load initialized guid */
	uint64_t	spa_last_synced_guid;	/* last synced guid */
	list_t		spa_config_dirty_list;	/* vdevs with dirty config */
	list_t		spa_state_dirty_list;	/* vdevs with dirty state */
	kmutex_t	spa_alloc_lock;
	avl_tree_t	spa_alloc_tree;
	spa_aux_vdev_t	spa_spares;		/* hot spares */
	spa_aux_vdev_t	spa_l2cache;		/* L2ARC cache devices */
	nvlist_t	*spa_label_features;	/* Features for reading MOS */
	uint64_t	spa_config_object;	/* MOS object for pool config */
	uint64_t	spa_config_generation;	/* config generation number */
	uint64_t	spa_syncing_txg;	/* txg currently syncing */
	bpobj_t		spa_deferred_bpobj;	/* deferred-free bplist */
	bplist_t	spa_free_bplist[TXG_SIZE]; /* bplist of stuff to free */
	zio_cksum_salt_t spa_cksum_salt;	/* secret salt for cksum */
	/* checksum context templates */
	kmutex_t	spa_cksum_tmpls_lock;
	void		*spa_cksum_tmpls[ZIO_CHECKSUM_FUNCTIONS];
	uberblock_t	spa_ubsync;		/* last synced uberblock */
	uberblock_t	spa_uberblock;		/* current uberblock */
	boolean_t	spa_extreme_rewind;	/* rewind past deferred frees */
	uint64_t	spa_last_io;		/* lbolt of last non-scan I/O */
	kmutex_t	spa_scrub_lock;		/* resilver/scrub lock */
	uint64_t	spa_scrub_inflight;	/* in-flight scrub I/Os */
	kcondvar_t	spa_scrub_io_cv;	/* scrub I/O completion */
	uint8_t		spa_scrub_active;	/* active or suspended? */
	uint8_t		spa_scrub_type;		/* type of scrub we're doing */
	uint8_t		spa_scrub_finished;	/* indicator to rotate logs */
	uint8_t		spa_scrub_started;	/* started since last boot */
	uint8_t		spa_scrub_reopen;	/* scrub doing vdev_reopen */
	uint64_t	spa_scan_pass_start;	/* start time per pass/reboot */
	uint64_t	spa_scan_pass_scrub_pause; /* scrub pause time */
	uint64_t	spa_scan_pass_scrub_spent_paused; /* total paused */
	uint64_t	spa_scan_pass_exam;	/* examined bytes per pass */
	kmutex_t	spa_async_lock;		/* protect async state */
	kthread_t	*spa_async_thread;	/* thread doing async task */
	int		spa_async_suspended;	/* async tasks suspended */
	kcondvar_t	spa_async_cv;		/* wait for thread_exit() */
	uint16_t	spa_async_tasks;	/* async task mask */
	char		*spa_root;		/* alternate root directory */
	uint64_t	spa_ena;		/* spa-wide ereport ENA */
	int		spa_last_open_failed;	/* error if last open failed */
	uint64_t	spa_last_ubsync_txg;	/* "best" uberblock txg */
	uint64_t	spa_last_ubsync_txg_ts;	/* timestamp from that ub */
	uint64_t	spa_load_txg;		/* ub txg that loaded */
	uint64_t	spa_load_txg_ts;	/* timestamp from that ub */
	uint64_t	spa_load_meta_errors;	/* verify metadata err count */
	uint64_t	spa_load_data_errors;	/* verify data err count */
	uint64_t	spa_verify_min_txg;	/* start txg of verify scrub */
	kmutex_t	spa_errlog_lock;	/* error log lock */
	uint64_t	spa_errlog_last;	/* last error log object */
	uint64_t	spa_errlog_scrub;	/* scrub error log object */
	kmutex_t	spa_errlist_lock;	/* error list/ereport lock */
	avl_tree_t	spa_errlist_last;	/* last error list */
	avl_tree_t	spa_errlist_scrub;	/* scrub error list */
	uint64_t	spa_deflate;		/* should we deflate? */
	uint64_t	spa_history;		/* history object */
	kmutex_t	spa_history_lock;	/* history lock */
	vdev_t		*spa_pending_vdev;	/* pending vdev additions */
	kmutex_t	spa_props_lock;		/* property lock */
	uint64_t	spa_pool_props_object;	/* object for properties */
	uint64_t	spa_bootfs;		/* default boot filesystem */
	uint64_t	spa_failmode;		/* failure mode for the pool */
	uint64_t	spa_delegation;		/* delegation on/off */
	list_t		spa_config_list;	/* previous cache file(s) */
	/* per-CPU array of root of async I/O: */
	zio_t		**spa_async_zio_root;
	zio_t		*spa_suspend_zio_root;	/* root of all suspended I/O */
	kmutex_t	spa_suspend_lock;	/* protects suspend_zio_root */
	kcondvar_t	spa_suspend_cv;		/* notification of resume */
	uint8_t		spa_suspended;		/* pool is suspended */
	uint8_t		spa_claiming;		/* pool is doing zil_claim() */
	boolean_t	spa_debug;		/* debug enabled? */
	boolean_t	spa_is_root;		/* pool is root */
	int		spa_minref;		/* num refs when first opened */
	int		spa_mode;		/* FREAD | FWRITE */
	spa_log_state_t spa_log_state;		/* log state */
	uint64_t	spa_autoexpand;		/* lun expansion on/off */
	ddt_t		*spa_ddt[ZIO_CHECKSUM_FUNCTIONS]; /* in-core DDTs */
	uint64_t	spa_ddt_stat_object;	/* DDT statistics */
	uint64_t	spa_dedup_dspace;	/* Cache get_dedup_dspace() */
	uint64_t	spa_dedup_ditto;	/* dedup ditto threshold */
	uint64_t	spa_dedup_checksum;	/* default dedup checksum */
	uint64_t	spa_dspace;		/* dspace in normal class */
	kmutex_t	spa_vdev_top_lock;	/* dueling offline/remove */
	kmutex_t	spa_proc_lock;		/* protects spa_proc* */
	kcondvar_t	spa_proc_cv;		/* spa_proc_state transitions */
	spa_proc_state_t spa_proc_state;	/* see definition */
	proc_t		*spa_proc;		/* "zpool-poolname" process */
	uint64_t	spa_did;		/* if procp != p0, did of t1 */
	boolean_t	spa_autoreplace;	/* autoreplace set in open */
	int		spa_vdev_locks;		/* locks grabbed */
	uint64_t	spa_creation_version;	/* version at pool creation */
	uint64_t	spa_prev_software_version; /* See ub_software_version */
	uint64_t	spa_feat_for_write_obj;	/* required to write to pool */
	uint64_t	spa_feat_for_read_obj;	/* required to read from pool */
	uint64_t	spa_feat_desc_obj;	/* Feature descriptions */
	uint64_t	spa_feat_enabled_txg_obj; /* Feature enabled txg */
	kmutex_t	spa_feat_stats_lock;	/* protects spa_feat_stats */
	nvlist_t	*spa_feat_stats;	/* Cache of enabled features */
	/* cache feature refcounts */
	uint64_t	spa_feat_refcount_cache[SPA_FEATURES];
	taskqid_t	spa_deadman_tqid;	/* Task id */
	uint64_t	spa_deadman_calls;	/* number of deadman calls */
	hrtime_t	spa_sync_starttime;	/* starting time of spa_sync */
	uint64_t	spa_deadman_synctime;	/* deadman expiration timer */
	uint64_t	spa_all_vdev_zaps;	/* ZAP of per-vd ZAP obj #s */
	spa_avz_action_t	spa_avz_action;	/* destroy/rebuild AVZ? */
	uint64_t	spa_errata;		/* errata issues detected */
	spa_stats_t	spa_stats;		/* assorted spa statistics */
	hrtime_t	spa_ccw_fail_time;	/* Conf cache write fail time */
	taskq_t		*spa_zvol_taskq;	/* Taskq for minor management */
	uint64_t	spa_multihost;		/* multihost aware (mmp) */
	mmp_thread_t	spa_mmp;		/* multihost mmp thread */

	/*
	 * spa_refcount & spa_config_lock must be the last elements
	 * because refcount_t changes size based on compilation options.
	 * In order for the MDB module to function correctly, the other
	 * fields must remain in the same location.
	 */
	spa_config_lock_t spa_config_lock[SCL_LOCKS]; /* config changes */
	refcount_t	spa_refcount;		/* number of opens */

	taskq_t		*spa_upgrade_taskq;	/* taskq for upgrade jobs */
};

extern char *spa_config_path;

extern void spa_taskq_dispatch_ent(spa_t *spa, zio_type_t t, zio_taskq_type_t q,
    task_func_t *func, void *arg, uint_t flags, taskq_ent_t *ent);
extern void spa_taskq_dispatch_sync(spa_t *, zio_type_t t, zio_taskq_type_t q,
    task_func_t *func, void *arg, uint_t flags);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPA_IMPL_H */
