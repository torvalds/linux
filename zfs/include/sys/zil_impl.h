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
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_ZIL_IMPL_H
#define	_SYS_ZIL_IMPL_H

#include <sys/zil.h>
#include <sys/dmu_objset.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Log write buffer.
 */
typedef struct lwb {
	zilog_t		*lwb_zilog;	/* back pointer to log struct */
	blkptr_t	lwb_blk;	/* on disk address of this log blk */
	boolean_t	lwb_fastwrite;	/* is blk marked for fastwrite? */
	int		lwb_nused;	/* # used bytes in buffer */
	int		lwb_sz;		/* size of block and buffer */
	char		*lwb_buf;	/* log write buffer */
	zio_t		*lwb_zio;	/* zio for this buffer */
	dmu_tx_t	*lwb_tx;	/* tx for log block allocation */
	uint64_t	lwb_max_txg;	/* highest txg in this lwb */
	list_node_t	lwb_node;	/* zilog->zl_lwb_list linkage */
} lwb_t;

/*
 * Intent log transaction lists
 */
typedef struct itxs {
	list_t		i_sync_list;	/* list of synchronous itxs */
	avl_tree_t	i_async_tree;	/* tree of foids for async itxs */
} itxs_t;

typedef struct itxg {
	kmutex_t	itxg_lock;	/* lock for this structure */
	uint64_t	itxg_txg;	/* txg for this chain */
	uint64_t	itxg_sod;	/* total size on disk for this txg */
	itxs_t		*itxg_itxs;	/* sync and async itxs */
} itxg_t;

/* for async nodes we build up an AVL tree of lists of async itxs per file */
typedef struct itx_async_node {
	uint64_t	ia_foid;	/* file object id */
	list_t		ia_list;	/* list of async itxs for this foid */
	avl_node_t	ia_node;	/* AVL tree linkage */
} itx_async_node_t;

/*
 * Vdev flushing: during a zil_commit(), we build up an AVL tree of the vdevs
 * we've touched so we know which ones need a write cache flush at the end.
 */
typedef struct zil_vdev_node {
	uint64_t	zv_vdev;	/* vdev to be flushed */
	avl_node_t	zv_node;	/* AVL tree linkage */
} zil_vdev_node_t;

#define	ZIL_PREV_BLKS 16

/*
 * Stable storage intent log management structure.  One per dataset.
 */
struct zilog {
	kmutex_t	zl_lock;	/* protects most zilog_t fields */
	struct dsl_pool	*zl_dmu_pool;	/* DSL pool */
	spa_t		*zl_spa;	/* handle for read/write log */
	const zil_header_t *zl_header;	/* log header buffer */
	objset_t	*zl_os;		/* object set we're logging */
	zil_get_data_t	*zl_get_data;	/* callback to get object content */
	zio_t		*zl_root_zio;	/* log writer root zio */
	uint64_t	zl_lr_seq;	/* on-disk log record sequence number */
	uint64_t	zl_commit_lr_seq; /* last committed on-disk lr seq */
	uint64_t	zl_destroy_txg;	/* txg of last zil_destroy() */
	uint64_t	zl_replayed_seq[TXG_SIZE]; /* last replayed rec seq */
	uint64_t	zl_replaying_seq; /* current replay seq number */
	uint32_t	zl_suspend;	/* log suspend count */
	kcondvar_t	zl_cv_writer;	/* log writer thread completion */
	kcondvar_t	zl_cv_suspend;	/* log suspend completion */
	uint8_t		zl_suspending;	/* log is currently suspending */
	uint8_t		zl_keep_first;	/* keep first log block in destroy */
	uint8_t		zl_replay;	/* replaying records while set */
	uint8_t		zl_stop_sync;	/* for debugging */
	uint8_t		zl_writer;	/* boolean: write setup in progress */
	uint8_t		zl_logbias;	/* latency or throughput */
	uint8_t		zl_sync;	/* synchronous or asynchronous */
	int		zl_parse_error;	/* last zil_parse() error */
	uint64_t	zl_parse_blk_seq; /* highest blk seq on last parse */
	uint64_t	zl_parse_lr_seq; /* highest lr seq on last parse */
	uint64_t	zl_parse_blk_count; /* number of blocks parsed */
	uint64_t	zl_parse_lr_count; /* number of log records parsed */
	uint64_t	zl_next_batch;	/* next batch number */
	uint64_t	zl_com_batch;	/* committed batch number */
	kcondvar_t	zl_cv_batch[2];	/* batch condition variables */
	itxg_t		zl_itxg[TXG_SIZE]; /* intent log txg chains */
	list_t		zl_itx_commit_list; /* itx list to be committed */
	uint64_t	zl_itx_list_sz;	/* total size of records on list */
	uint64_t	zl_cur_used;	/* current commit log size used */
	list_t		zl_lwb_list;	/* in-flight log write list */
	kmutex_t	zl_vdev_lock;	/* protects zl_vdev_tree */
	avl_tree_t	zl_vdev_tree;	/* vdevs to flush in zil_commit() */
	taskq_t		*zl_clean_taskq; /* runs lwb and itx clean tasks */
	avl_tree_t	zl_bp_tree;	/* track bps during log parse */
	clock_t		zl_replay_time;	/* lbolt of when replay started */
	uint64_t	zl_replay_blks;	/* number of log blocks replayed */
	zil_header_t	zl_old_header;	/* debugging aid */
	uint_t		zl_prev_blks[ZIL_PREV_BLKS]; /* size - sector rounded */
	uint_t		zl_prev_rotor;	/* rotor for zl_prev[] */
	txg_node_t	zl_dirty_link;	/* protected by dp_dirty_zilogs list */
};

typedef struct zil_bp_node {
	dva_t		zn_dva;
	avl_node_t	zn_node;
} zil_bp_node_t;

#define	ZIL_MAX_LOG_DATA (SPA_OLD_MAXBLOCKSIZE - sizeof (zil_chain_t) - \
    sizeof (lr_write_t))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIL_IMPL_H */
