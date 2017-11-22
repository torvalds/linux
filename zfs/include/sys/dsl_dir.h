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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#ifndef	_SYS_DSL_DIR_H
#define	_SYS_DSL_DIR_H

#include <sys/dmu.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/refcount.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_dataset;

/*
 * DD_FIELD_* are strings that are used in the "extensified" dsl_dir zap object.
 * They should be of the format <reverse-dns>:<field>.
 */

#define	DD_FIELD_FILESYSTEM_COUNT	"com.joyent:filesystem_count"
#define	DD_FIELD_SNAPSHOT_COUNT		"com.joyent:snapshot_count"

typedef enum dd_used {
	DD_USED_HEAD,
	DD_USED_SNAP,
	DD_USED_CHILD,
	DD_USED_CHILD_RSRV,
	DD_USED_REFRSRV,
	DD_USED_NUM
} dd_used_t;

#define	DD_FLAG_USED_BREAKDOWN (1<<0)

typedef struct dsl_dir_phys {
	uint64_t dd_creation_time; /* not actually used */
	uint64_t dd_head_dataset_obj;
	uint64_t dd_parent_obj;
	uint64_t dd_origin_obj;
	uint64_t dd_child_dir_zapobj;
	/*
	 * how much space our children are accounting for; for leaf
	 * datasets, == physical space used by fs + snaps
	 */
	uint64_t dd_used_bytes;
	uint64_t dd_compressed_bytes;
	uint64_t dd_uncompressed_bytes;
	/* Administrative quota setting */
	uint64_t dd_quota;
	/* Administrative reservation setting */
	uint64_t dd_reserved;
	uint64_t dd_props_zapobj;
	uint64_t dd_deleg_zapobj; /* dataset delegation permissions */
	uint64_t dd_flags;
	uint64_t dd_used_breakdown[DD_USED_NUM];
	uint64_t dd_clones; /* dsl_dir objects */
	uint64_t dd_pad[13]; /* pad out to 256 bytes for good measure */
} dsl_dir_phys_t;

struct dsl_dir {
	dmu_buf_user_t dd_dbu;

	/* These are immutable; no lock needed: */
	uint64_t dd_object;
	dsl_pool_t *dd_pool;

	/* Stable until user eviction; no lock needed: */
	dmu_buf_t *dd_dbuf;

	/* protected by lock on pool's dp_dirty_dirs list */
	txg_node_t dd_dirty_link;

	/* protected by dp_config_rwlock */
	dsl_dir_t *dd_parent;

	/* Protected by dd_lock */
	kmutex_t dd_lock;
	list_t dd_props; /* list of dsl_prop_record_t's */
	timestruc_t dd_snap_cmtime; /* last time snapshot namespace changed */
	uint64_t dd_origin_txg;

	/* gross estimate of space used by in-flight tx's */
	uint64_t dd_tempreserved[TXG_SIZE];
	/* amount of space we expect to write; == amount of dirty data */
	int64_t dd_space_towrite[TXG_SIZE];

	/* protected by dd_lock; keep at end of struct for better locality */
	char dd_myname[ZFS_MAX_DATASET_NAME_LEN];
};

static inline dsl_dir_phys_t *
dsl_dir_phys(dsl_dir_t *dd)
{
	return (dd->dd_dbuf->db_data);
}

void dsl_dir_rele(dsl_dir_t *dd, void *tag);
void dsl_dir_async_rele(dsl_dir_t *dd, void *tag);
int dsl_dir_hold(dsl_pool_t *dp, const char *name, void *tag,
    dsl_dir_t **, const char **tail);
int dsl_dir_hold_obj(dsl_pool_t *dp, uint64_t ddobj,
    const char *tail, void *tag, dsl_dir_t **);
void dsl_dir_name(dsl_dir_t *dd, char *buf);
int dsl_dir_namelen(dsl_dir_t *dd);
uint64_t dsl_dir_create_sync(dsl_pool_t *dp, dsl_dir_t *pds,
    const char *name, dmu_tx_t *tx);
void dsl_dir_stats(dsl_dir_t *dd, nvlist_t *nv);
uint64_t dsl_dir_space_available(dsl_dir_t *dd,
    dsl_dir_t *ancestor, int64_t delta, int ondiskonly);
void dsl_dir_dirty(dsl_dir_t *dd, dmu_tx_t *tx);
void dsl_dir_sync(dsl_dir_t *dd, dmu_tx_t *tx);
int dsl_dir_tempreserve_space(dsl_dir_t *dd, uint64_t mem,
    uint64_t asize, boolean_t netfree, void **tr_cookiep, dmu_tx_t *tx);
void dsl_dir_tempreserve_clear(void *tr_cookie, dmu_tx_t *tx);
void dsl_dir_willuse_space(dsl_dir_t *dd, int64_t space, dmu_tx_t *tx);
void dsl_dir_diduse_space(dsl_dir_t *dd, dd_used_t type,
    int64_t used, int64_t compressed, int64_t uncompressed, dmu_tx_t *tx);
void dsl_dir_transfer_space(dsl_dir_t *dd, int64_t delta,
    dd_used_t oldtype, dd_used_t newtype, dmu_tx_t *tx);
int dsl_dir_set_quota(const char *ddname, zprop_source_t source,
    uint64_t quota);
int dsl_dir_set_reservation(const char *ddname, zprop_source_t source,
    uint64_t reservation);
int dsl_dir_activate_fs_ss_limit(const char *);
int dsl_fs_ss_limit_check(dsl_dir_t *, uint64_t, zfs_prop_t, dsl_dir_t *,
    cred_t *);
void dsl_fs_ss_count_adjust(dsl_dir_t *, int64_t, const char *, dmu_tx_t *);
int dsl_dir_rename(const char *oldname, const char *newname);
int dsl_dir_transfer_possible(dsl_dir_t *sdd, dsl_dir_t *tdd,
    uint64_t fs_cnt, uint64_t ss_cnt, uint64_t space, cred_t *);
boolean_t dsl_dir_is_clone(dsl_dir_t *dd);
void dsl_dir_new_refreservation(dsl_dir_t *dd, struct dsl_dataset *ds,
    uint64_t reservation, cred_t *cr, dmu_tx_t *tx);
void dsl_dir_snap_cmtime_update(dsl_dir_t *dd);
timestruc_t dsl_dir_snap_cmtime(dsl_dir_t *dd);
void dsl_dir_set_reservation_sync_impl(dsl_dir_t *dd, uint64_t value,
    dmu_tx_t *tx);
void dsl_dir_zapify(dsl_dir_t *dd, dmu_tx_t *tx);
boolean_t dsl_dir_is_zapified(dsl_dir_t *dd);

/* internal reserved dir name */
#define	MOS_DIR_NAME "$MOS"
#define	ORIGIN_DIR_NAME "$ORIGIN"
#define	XLATION_DIR_NAME "$XLATION"
#define	FREE_DIR_NAME "$FREE"
#define	LEAK_DIR_NAME "$LEAK"

#ifdef ZFS_DEBUG
#define	dprintf_dd(dd, fmt, ...) do { \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { \
	char *__ds_name = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP); \
	dsl_dir_name(dd, __ds_name); \
	dprintf("dd=%s " fmt, __ds_name, __VA_ARGS__); \
	kmem_free(__ds_name, ZFS_MAX_DATASET_NAME_LEN); \
	} \
_NOTE(CONSTCOND) } while (0)
#else
#define	dprintf_dd(dd, fmt, ...)
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_DIR_H */
