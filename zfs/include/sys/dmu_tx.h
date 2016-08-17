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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DMU_TX_H
#define	_SYS_DMU_TX_H

#include <sys/inttypes.h>
#include <sys/dmu.h>
#include <sys/txg.h>
#include <sys/refcount.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dmu_buf_impl;
struct dmu_tx_hold;
struct dnode_link;
struct dsl_pool;
struct dnode;
struct dsl_dir;

struct dmu_tx {
	/*
	 * No synchronization is needed because a tx can only be handled
	 * by one thread.
	 */
	list_t tx_holds; /* list of dmu_tx_hold_t */
	objset_t *tx_objset;
	struct dsl_dir *tx_dir;
	struct dsl_pool *tx_pool;
	uint64_t tx_txg;
	uint64_t tx_lastsnap_txg;
	uint64_t tx_lasttried_txg;
	txg_handle_t tx_txgh;
	void *tx_tempreserve_cookie;
	struct dmu_tx_hold *tx_needassign_txh;

	/* list of dmu_tx_callback_t on this dmu_tx */
	list_t tx_callbacks;

	/* placeholder for syncing context, doesn't need specific holds */
	boolean_t tx_anyobj;

	/* has this transaction already been delayed? */
	boolean_t tx_waited;

	/* time this transaction was created */
	hrtime_t tx_start;

	/* need to wait for sufficient dirty space */
	boolean_t tx_wait_dirty;

	int tx_err;
#ifdef DEBUG_DMU_TX
	uint64_t tx_space_towrite;
	uint64_t tx_space_tofree;
	uint64_t tx_space_tooverwrite;
	uint64_t tx_space_tounref;
	refcount_t tx_space_written;
	refcount_t tx_space_freed;
#endif
};

enum dmu_tx_hold_type {
	THT_NEWOBJECT,
	THT_WRITE,
	THT_BONUS,
	THT_FREE,
	THT_ZAP,
	THT_SPACE,
	THT_SPILL,
	THT_NUMTYPES
};

typedef struct dmu_tx_hold {
	dmu_tx_t *txh_tx;
	list_node_t txh_node;
	struct dnode *txh_dnode;
	uint64_t txh_space_towrite;
	uint64_t txh_space_tofree;
	uint64_t txh_space_tooverwrite;
	uint64_t txh_space_tounref;
	uint64_t txh_memory_tohold;
	uint64_t txh_fudge;
#ifdef DEBUG_DMU_TX
	enum dmu_tx_hold_type txh_type;
	uint64_t txh_arg1;
	uint64_t txh_arg2;
#endif
} dmu_tx_hold_t;

typedef struct dmu_tx_callback {
	list_node_t		dcb_node;    /* linked to tx_callbacks list */
	dmu_tx_callback_func_t	*dcb_func;   /* caller function pointer */
	void			*dcb_data;   /* caller private data */
} dmu_tx_callback_t;

/*
 * Used for dmu tx kstat.
 */
typedef struct dmu_tx_stats {
	kstat_named_t dmu_tx_assigned;
	kstat_named_t dmu_tx_delay;
	kstat_named_t dmu_tx_error;
	kstat_named_t dmu_tx_suspended;
	kstat_named_t dmu_tx_group;
	kstat_named_t dmu_tx_memory_reserve;
	kstat_named_t dmu_tx_memory_reclaim;
	kstat_named_t dmu_tx_dirty_throttle;
	kstat_named_t dmu_tx_dirty_delay;
	kstat_named_t dmu_tx_dirty_over_max;
	kstat_named_t dmu_tx_quota;
} dmu_tx_stats_t;

extern dmu_tx_stats_t dmu_tx_stats;

#define	DMU_TX_STAT_INCR(stat, val) \
    atomic_add_64(&dmu_tx_stats.stat.value.ui64, (val));
#define	DMU_TX_STAT_BUMP(stat) \
    DMU_TX_STAT_INCR(stat, 1);

/*
 * These routines are defined in dmu.h, and are called by the user.
 */
dmu_tx_t *dmu_tx_create(objset_t *dd);
int dmu_tx_assign(dmu_tx_t *tx, txg_how_t txg_how);
void dmu_tx_commit(dmu_tx_t *tx);
void dmu_tx_abort(dmu_tx_t *tx);
uint64_t dmu_tx_get_txg(dmu_tx_t *tx);
struct dsl_pool *dmu_tx_pool(dmu_tx_t *tx);
void dmu_tx_wait(dmu_tx_t *tx);

void dmu_tx_callback_register(dmu_tx_t *tx, dmu_tx_callback_func_t *dcb_func,
    void *dcb_data);
void dmu_tx_do_callbacks(list_t *cb_list, int error);

/*
 * These routines are defined in dmu_spa.h, and are called by the SPA.
 */
extern dmu_tx_t *dmu_tx_create_assigned(struct dsl_pool *dp, uint64_t txg);

/*
 * These routines are only called by the DMU.
 */
dmu_tx_t *dmu_tx_create_dd(dsl_dir_t *dd);
int dmu_tx_is_syncing(dmu_tx_t *tx);
int dmu_tx_private_ok(dmu_tx_t *tx);
void dmu_tx_add_new_object(dmu_tx_t *tx, objset_t *os, uint64_t object);
void dmu_tx_willuse_space(dmu_tx_t *tx, int64_t delta);
void dmu_tx_dirty_buf(dmu_tx_t *tx, struct dmu_buf_impl *db);
int dmu_tx_holds(dmu_tx_t *tx, uint64_t object);
void dmu_tx_hold_space(dmu_tx_t *tx, uint64_t space);

#ifdef DEBUG_DMU_TX
#define	DMU_TX_DIRTY_BUF(tx, db)	dmu_tx_dirty_buf(tx, db)
#else
#define	DMU_TX_DIRTY_BUF(tx, db)
#endif

void dmu_tx_init(void);
void dmu_tx_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DMU_TX_H */
