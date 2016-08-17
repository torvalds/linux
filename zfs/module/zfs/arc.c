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
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 by Saso Kiselkov. All rights reserved.
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * DVA-based Adjustable Replacement Cache
 *
 * While much of the theory of operation used here is
 * based on the self-tuning, low overhead replacement cache
 * presented by Megiddo and Modha at FAST 2003, there are some
 * significant differences:
 *
 * 1. The Megiddo and Modha model assumes any page is evictable.
 * Pages in its cache cannot be "locked" into memory.  This makes
 * the eviction algorithm simple: evict the last page in the list.
 * This also make the performance characteristics easy to reason
 * about.  Our cache is not so simple.  At any given moment, some
 * subset of the blocks in the cache are un-evictable because we
 * have handed out a reference to them.  Blocks are only evictable
 * when there are no external references active.  This makes
 * eviction far more problematic:  we choose to evict the evictable
 * blocks that are the "lowest" in the list.
 *
 * There are times when it is not possible to evict the requested
 * space.  In these circumstances we are unable to adjust the cache
 * size.  To prevent the cache growing unbounded at these times we
 * implement a "cache throttle" that slows the flow of new data
 * into the cache until we can make space available.
 *
 * 2. The Megiddo and Modha model assumes a fixed cache size.
 * Pages are evicted when the cache is full and there is a cache
 * miss.  Our model has a variable sized cache.  It grows with
 * high use, but also tries to react to memory pressure from the
 * operating system: decreasing its size when system memory is
 * tight.
 *
 * 3. The Megiddo and Modha model assumes a fixed page size. All
 * elements of the cache are therefore exactly the same size.  So
 * when adjusting the cache size following a cache miss, its simply
 * a matter of choosing a single page to evict.  In our model, we
 * have variable sized cache blocks (rangeing from 512 bytes to
 * 128K bytes).  We therefore choose a set of blocks to evict to make
 * space for a cache miss that approximates as closely as possible
 * the space used by the new block.
 *
 * See also:  "ARC: A Self-Tuning, Low Overhead Replacement Cache"
 * by N. Megiddo & D. Modha, FAST 2003
 */

/*
 * The locking model:
 *
 * A new reference to a cache buffer can be obtained in two
 * ways: 1) via a hash table lookup using the DVA as a key,
 * or 2) via one of the ARC lists.  The arc_read() interface
 * uses method 1, while the internal arc algorithms for
 * adjusting the cache use method 2.  We therefore provide two
 * types of locks: 1) the hash table lock array, and 2) the
 * arc list locks.
 *
 * Buffers do not have their own mutexes, rather they rely on the
 * hash table mutexes for the bulk of their protection (i.e. most
 * fields in the arc_buf_hdr_t are protected by these mutexes).
 *
 * buf_hash_find() returns the appropriate mutex (held) when it
 * locates the requested buffer in the hash table.  It returns
 * NULL for the mutex if the buffer was not in the table.
 *
 * buf_hash_remove() expects the appropriate hash mutex to be
 * already held before it is invoked.
 *
 * Each arc state also has a mutex which is used to protect the
 * buffer list associated with the state.  When attempting to
 * obtain a hash table lock while holding an arc list lock you
 * must use: mutex_tryenter() to avoid deadlock.  Also note that
 * the active state mutex must be held before the ghost state mutex.
 *
 * Arc buffers may have an associated eviction callback function.
 * This function will be invoked prior to removing the buffer (e.g.
 * in arc_do_user_evicts()).  Note however that the data associated
 * with the buffer may be evicted prior to the callback.  The callback
 * must be made with *no locks held* (to prevent deadlock).  Additionally,
 * the users of callbacks must ensure that their private data is
 * protected from simultaneous callbacks from arc_clear_callback()
 * and arc_do_user_evicts().
 *
 * It as also possible to register a callback which is run when the
 * arc_meta_limit is reached and no buffers can be safely evicted.  In
 * this case the arc user should drop a reference on some arc buffers so
 * they can be reclaimed and the arc_meta_limit honored.  For example,
 * when using the ZPL each dentry holds a references on a znode.  These
 * dentries must be pruned before the arc buffer holding the znode can
 * be safely evicted.
 *
 * Note that the majority of the performance stats are manipulated
 * with atomic operations.
 *
 * The L2ARC uses the l2ad_mtx on each vdev for the following:
 *
 *	- L2ARC buflist creation
 *	- L2ARC buflist eviction
 *	- L2ARC write completion, which walks L2ARC buflists
 *	- ARC header destruction, as it removes from L2ARC buflists
 *	- ARC header release, as it removes from L2ARC buflists
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/refcount.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_pool.h>
#include <sys/multilist.h>
#ifdef _KERNEL
#include <sys/vmsystm.h>
#include <vm/anon.h>
#include <sys/fs/swapnode.h>
#include <sys/zpl.h>
#include <linux/mm_compat.h>
#endif
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/dmu_tx.h>
#include <zfs_fletcher.h>
#include <sys/arc_impl.h>
#include <sys/trace_arc.h>

#ifndef _KERNEL
/* set with ZFS_DEBUG=watch, to enable watchpoints on frozen buffers */
boolean_t arc_watch = B_FALSE;
#endif

static kmutex_t		arc_reclaim_lock;
static kcondvar_t	arc_reclaim_thread_cv;
static boolean_t	arc_reclaim_thread_exit;
static kcondvar_t	arc_reclaim_waiters_cv;

static kmutex_t		arc_user_evicts_lock;
static kcondvar_t	arc_user_evicts_cv;
static boolean_t	arc_user_evicts_thread_exit;

/*
 * The number of headers to evict in arc_evict_state_impl() before
 * dropping the sublist lock and evicting from another sublist. A lower
 * value means we're more likely to evict the "correct" header (i.e. the
 * oldest header in the arc state), but comes with higher overhead
 * (i.e. more invocations of arc_evict_state_impl()).
 */
int zfs_arc_evict_batch_limit = 10;

/*
 * The number of sublists used for each of the arc state lists. If this
 * is not set to a suitable value by the user, it will be configured to
 * the number of CPUs on the system in arc_init().
 */
int zfs_arc_num_sublists_per_state = 0;

/* number of seconds before growing cache again */
static int		arc_grow_retry = 5;

/* shift of arc_c for calculating overflow limit in arc_get_data_buf */
int		zfs_arc_overflow_shift = 8;

/* shift of arc_c for calculating both min and max arc_p */
static int		arc_p_min_shift = 4;

/* log2(fraction of arc to reclaim) */
static int		arc_shrink_shift = 7;

/*
 * log2(fraction of ARC which must be free to allow growing).
 * I.e. If there is less than arc_c >> arc_no_grow_shift free memory,
 * when reading a new block into the ARC, we will evict an equal-sized block
 * from the ARC.
 *
 * This must be less than arc_shrink_shift, so that when we shrink the ARC,
 * we will still not allow it to grow.
 */
int			arc_no_grow_shift = 5;


/*
 * minimum lifespan of a prefetch block in clock ticks
 * (initialized in arc_init())
 */
static int		arc_min_prefetch_lifespan;

/*
 * If this percent of memory is free, don't throttle.
 */
int arc_lotsfree_percent = 10;

static int arc_dead;

/*
 * The arc has filled available memory and has now warmed up.
 */
static boolean_t arc_warm;

/*
 * These tunables are for performance analysis.
 */
unsigned long zfs_arc_max = 0;
unsigned long zfs_arc_min = 0;
unsigned long zfs_arc_meta_limit = 0;
unsigned long zfs_arc_meta_min = 0;
int zfs_arc_grow_retry = 0;
int zfs_arc_shrink_shift = 0;
int zfs_arc_p_min_shift = 0;
int zfs_disable_dup_eviction = 0;
int zfs_arc_average_blocksize = 8 * 1024; /* 8KB */

/*
 * These tunables are Linux specific
 */
unsigned long zfs_arc_sys_free = 0;
int zfs_arc_min_prefetch_lifespan = 0;
int zfs_arc_p_aggressive_disable = 1;
int zfs_arc_p_dampener_disable = 1;
int zfs_arc_meta_prune = 10000;
int zfs_arc_meta_strategy = ARC_STRATEGY_META_BALANCED;
int zfs_arc_meta_adjust_restarts = 4096;
int zfs_arc_lotsfree_percent = 10;

/* The 6 states: */
static arc_state_t ARC_anon;
static arc_state_t ARC_mru;
static arc_state_t ARC_mru_ghost;
static arc_state_t ARC_mfu;
static arc_state_t ARC_mfu_ghost;
static arc_state_t ARC_l2c_only;

typedef struct arc_stats {
	kstat_named_t arcstat_hits;
	kstat_named_t arcstat_misses;
	kstat_named_t arcstat_demand_data_hits;
	kstat_named_t arcstat_demand_data_misses;
	kstat_named_t arcstat_demand_metadata_hits;
	kstat_named_t arcstat_demand_metadata_misses;
	kstat_named_t arcstat_prefetch_data_hits;
	kstat_named_t arcstat_prefetch_data_misses;
	kstat_named_t arcstat_prefetch_metadata_hits;
	kstat_named_t arcstat_prefetch_metadata_misses;
	kstat_named_t arcstat_mru_hits;
	kstat_named_t arcstat_mru_ghost_hits;
	kstat_named_t arcstat_mfu_hits;
	kstat_named_t arcstat_mfu_ghost_hits;
	kstat_named_t arcstat_deleted;
	/*
	 * Number of buffers that could not be evicted because the hash lock
	 * was held by another thread.  The lock may not necessarily be held
	 * by something using the same buffer, since hash locks are shared
	 * by multiple buffers.
	 */
	kstat_named_t arcstat_mutex_miss;
	/*
	 * Number of buffers skipped because they have I/O in progress, are
	 * indrect prefetch buffers that have not lived long enough, or are
	 * not from the spa we're trying to evict from.
	 */
	kstat_named_t arcstat_evict_skip;
	/*
	 * Number of times arc_evict_state() was unable to evict enough
	 * buffers to reach its target amount.
	 */
	kstat_named_t arcstat_evict_not_enough;
	kstat_named_t arcstat_evict_l2_cached;
	kstat_named_t arcstat_evict_l2_eligible;
	kstat_named_t arcstat_evict_l2_ineligible;
	kstat_named_t arcstat_evict_l2_skip;
	kstat_named_t arcstat_hash_elements;
	kstat_named_t arcstat_hash_elements_max;
	kstat_named_t arcstat_hash_collisions;
	kstat_named_t arcstat_hash_chains;
	kstat_named_t arcstat_hash_chain_max;
	kstat_named_t arcstat_p;
	kstat_named_t arcstat_c;
	kstat_named_t arcstat_c_min;
	kstat_named_t arcstat_c_max;
	kstat_named_t arcstat_size;
	/*
	 * Number of bytes consumed by internal ARC structures necessary
	 * for tracking purposes; these structures are not actually
	 * backed by ARC buffers. This includes arc_buf_hdr_t structures
	 * (allocated via arc_buf_hdr_t_full and arc_buf_hdr_t_l2only
	 * caches), and arc_buf_t structures (allocated via arc_buf_t
	 * cache).
	 */
	kstat_named_t arcstat_hdr_size;
	/*
	 * Number of bytes consumed by ARC buffers of type equal to
	 * ARC_BUFC_DATA. This is generally consumed by buffers backing
	 * on disk user data (e.g. plain file contents).
	 */
	kstat_named_t arcstat_data_size;
	/*
	 * Number of bytes consumed by ARC buffers of type equal to
	 * ARC_BUFC_METADATA. This is generally consumed by buffers
	 * backing on disk data that is used for internal ZFS
	 * structures (e.g. ZAP, dnode, indirect blocks, etc).
	 */
	kstat_named_t arcstat_metadata_size;
	/*
	 * Number of bytes consumed by various buffers and structures
	 * not actually backed with ARC buffers. This includes bonus
	 * buffers (allocated directly via zio_buf_* functions),
	 * dmu_buf_impl_t structures (allocated via dmu_buf_impl_t
	 * cache), and dnode_t structures (allocated via dnode_t cache).
	 */
	kstat_named_t arcstat_other_size;
	/*
	 * Total number of bytes consumed by ARC buffers residing in the
	 * arc_anon state. This includes *all* buffers in the arc_anon
	 * state; e.g. data, metadata, evictable, and unevictable buffers
	 * are all included in this value.
	 */
	kstat_named_t arcstat_anon_size;
	/*
	 * Number of bytes consumed by ARC buffers that meet the
	 * following criteria: backing buffers of type ARC_BUFC_DATA,
	 * residing in the arc_anon state, and are eligible for eviction
	 * (e.g. have no outstanding holds on the buffer).
	 */
	kstat_named_t arcstat_anon_evictable_data;
	/*
	 * Number of bytes consumed by ARC buffers that meet the
	 * following criteria: backing buffers of type ARC_BUFC_METADATA,
	 * residing in the arc_anon state, and are eligible for eviction
	 * (e.g. have no outstanding holds on the buffer).
	 */
	kstat_named_t arcstat_anon_evictable_metadata;
	/*
	 * Total number of bytes consumed by ARC buffers residing in the
	 * arc_mru state. This includes *all* buffers in the arc_mru
	 * state; e.g. data, metadata, evictable, and unevictable buffers
	 * are all included in this value.
	 */
	kstat_named_t arcstat_mru_size;
	/*
	 * Number of bytes consumed by ARC buffers that meet the
	 * following criteria: backing buffers of type ARC_BUFC_DATA,
	 * residing in the arc_mru state, and are eligible for eviction
	 * (e.g. have no outstanding holds on the buffer).
	 */
	kstat_named_t arcstat_mru_evictable_data;
	/*
	 * Number of bytes consumed by ARC buffers that meet the
	 * following criteria: backing buffers of type ARC_BUFC_METADATA,
	 * residing in the arc_mru state, and are eligible for eviction
	 * (e.g. have no outstanding holds on the buffer).
	 */
	kstat_named_t arcstat_mru_evictable_metadata;
	/*
	 * Total number of bytes that *would have been* consumed by ARC
	 * buffers in the arc_mru_ghost state. The key thing to note
	 * here, is the fact that this size doesn't actually indicate
	 * RAM consumption. The ghost lists only consist of headers and
	 * don't actually have ARC buffers linked off of these headers.
	 * Thus, *if* the headers had associated ARC buffers, these
	 * buffers *would have* consumed this number of bytes.
	 */
	kstat_named_t arcstat_mru_ghost_size;
	/*
	 * Number of bytes that *would have been* consumed by ARC
	 * buffers that are eligible for eviction, of type
	 * ARC_BUFC_DATA, and linked off the arc_mru_ghost state.
	 */
	kstat_named_t arcstat_mru_ghost_evictable_data;
	/*
	 * Number of bytes that *would have been* consumed by ARC
	 * buffers that are eligible for eviction, of type
	 * ARC_BUFC_METADATA, and linked off the arc_mru_ghost state.
	 */
	kstat_named_t arcstat_mru_ghost_evictable_metadata;
	/*
	 * Total number of bytes consumed by ARC buffers residing in the
	 * arc_mfu state. This includes *all* buffers in the arc_mfu
	 * state; e.g. data, metadata, evictable, and unevictable buffers
	 * are all included in this value.
	 */
	kstat_named_t arcstat_mfu_size;
	/*
	 * Number of bytes consumed by ARC buffers that are eligible for
	 * eviction, of type ARC_BUFC_DATA, and reside in the arc_mfu
	 * state.
	 */
	kstat_named_t arcstat_mfu_evictable_data;
	/*
	 * Number of bytes consumed by ARC buffers that are eligible for
	 * eviction, of type ARC_BUFC_METADATA, and reside in the
	 * arc_mfu state.
	 */
	kstat_named_t arcstat_mfu_evictable_metadata;
	/*
	 * Total number of bytes that *would have been* consumed by ARC
	 * buffers in the arc_mfu_ghost state. See the comment above
	 * arcstat_mru_ghost_size for more details.
	 */
	kstat_named_t arcstat_mfu_ghost_size;
	/*
	 * Number of bytes that *would have been* consumed by ARC
	 * buffers that are eligible for eviction, of type
	 * ARC_BUFC_DATA, and linked off the arc_mfu_ghost state.
	 */
	kstat_named_t arcstat_mfu_ghost_evictable_data;
	/*
	 * Number of bytes that *would have been* consumed by ARC
	 * buffers that are eligible for eviction, of type
	 * ARC_BUFC_METADATA, and linked off the arc_mru_ghost state.
	 */
	kstat_named_t arcstat_mfu_ghost_evictable_metadata;
	kstat_named_t arcstat_l2_hits;
	kstat_named_t arcstat_l2_misses;
	kstat_named_t arcstat_l2_feeds;
	kstat_named_t arcstat_l2_rw_clash;
	kstat_named_t arcstat_l2_read_bytes;
	kstat_named_t arcstat_l2_write_bytes;
	kstat_named_t arcstat_l2_writes_sent;
	kstat_named_t arcstat_l2_writes_done;
	kstat_named_t arcstat_l2_writes_error;
	kstat_named_t arcstat_l2_writes_lock_retry;
	kstat_named_t arcstat_l2_evict_lock_retry;
	kstat_named_t arcstat_l2_evict_reading;
	kstat_named_t arcstat_l2_evict_l1cached;
	kstat_named_t arcstat_l2_free_on_write;
	kstat_named_t arcstat_l2_cdata_free_on_write;
	kstat_named_t arcstat_l2_abort_lowmem;
	kstat_named_t arcstat_l2_cksum_bad;
	kstat_named_t arcstat_l2_io_error;
	kstat_named_t arcstat_l2_size;
	kstat_named_t arcstat_l2_asize;
	kstat_named_t arcstat_l2_hdr_size;
	kstat_named_t arcstat_l2_compress_successes;
	kstat_named_t arcstat_l2_compress_zeros;
	kstat_named_t arcstat_l2_compress_failures;
	kstat_named_t arcstat_memory_throttle_count;
	kstat_named_t arcstat_duplicate_buffers;
	kstat_named_t arcstat_duplicate_buffers_size;
	kstat_named_t arcstat_duplicate_reads;
	kstat_named_t arcstat_memory_direct_count;
	kstat_named_t arcstat_memory_indirect_count;
	kstat_named_t arcstat_no_grow;
	kstat_named_t arcstat_tempreserve;
	kstat_named_t arcstat_loaned_bytes;
	kstat_named_t arcstat_prune;
	kstat_named_t arcstat_meta_used;
	kstat_named_t arcstat_meta_limit;
	kstat_named_t arcstat_meta_max;
	kstat_named_t arcstat_meta_min;
	kstat_named_t arcstat_need_free;
	kstat_named_t arcstat_sys_free;
} arc_stats_t;

static arc_stats_t arc_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "demand_data_hits",		KSTAT_DATA_UINT64 },
	{ "demand_data_misses",		KSTAT_DATA_UINT64 },
	{ "demand_metadata_hits",	KSTAT_DATA_UINT64 },
	{ "demand_metadata_misses",	KSTAT_DATA_UINT64 },
	{ "prefetch_data_hits",		KSTAT_DATA_UINT64 },
	{ "prefetch_data_misses",	KSTAT_DATA_UINT64 },
	{ "prefetch_metadata_hits",	KSTAT_DATA_UINT64 },
	{ "prefetch_metadata_misses",	KSTAT_DATA_UINT64 },
	{ "mru_hits",			KSTAT_DATA_UINT64 },
	{ "mru_ghost_hits",		KSTAT_DATA_UINT64 },
	{ "mfu_hits",			KSTAT_DATA_UINT64 },
	{ "mfu_ghost_hits",		KSTAT_DATA_UINT64 },
	{ "deleted",			KSTAT_DATA_UINT64 },
	{ "mutex_miss",			KSTAT_DATA_UINT64 },
	{ "evict_skip",			KSTAT_DATA_UINT64 },
	{ "evict_not_enough",		KSTAT_DATA_UINT64 },
	{ "evict_l2_cached",		KSTAT_DATA_UINT64 },
	{ "evict_l2_eligible",		KSTAT_DATA_UINT64 },
	{ "evict_l2_ineligible",	KSTAT_DATA_UINT64 },
	{ "evict_l2_skip",		KSTAT_DATA_UINT64 },
	{ "hash_elements",		KSTAT_DATA_UINT64 },
	{ "hash_elements_max",		KSTAT_DATA_UINT64 },
	{ "hash_collisions",		KSTAT_DATA_UINT64 },
	{ "hash_chains",		KSTAT_DATA_UINT64 },
	{ "hash_chain_max",		KSTAT_DATA_UINT64 },
	{ "p",				KSTAT_DATA_UINT64 },
	{ "c",				KSTAT_DATA_UINT64 },
	{ "c_min",			KSTAT_DATA_UINT64 },
	{ "c_max",			KSTAT_DATA_UINT64 },
	{ "size",			KSTAT_DATA_UINT64 },
	{ "hdr_size",			KSTAT_DATA_UINT64 },
	{ "data_size",			KSTAT_DATA_UINT64 },
	{ "metadata_size",		KSTAT_DATA_UINT64 },
	{ "other_size",			KSTAT_DATA_UINT64 },
	{ "anon_size",			KSTAT_DATA_UINT64 },
	{ "anon_evictable_data",	KSTAT_DATA_UINT64 },
	{ "anon_evictable_metadata",	KSTAT_DATA_UINT64 },
	{ "mru_size",			KSTAT_DATA_UINT64 },
	{ "mru_evictable_data",		KSTAT_DATA_UINT64 },
	{ "mru_evictable_metadata",	KSTAT_DATA_UINT64 },
	{ "mru_ghost_size",		KSTAT_DATA_UINT64 },
	{ "mru_ghost_evictable_data",	KSTAT_DATA_UINT64 },
	{ "mru_ghost_evictable_metadata", KSTAT_DATA_UINT64 },
	{ "mfu_size",			KSTAT_DATA_UINT64 },
	{ "mfu_evictable_data",		KSTAT_DATA_UINT64 },
	{ "mfu_evictable_metadata",	KSTAT_DATA_UINT64 },
	{ "mfu_ghost_size",		KSTAT_DATA_UINT64 },
	{ "mfu_ghost_evictable_data",	KSTAT_DATA_UINT64 },
	{ "mfu_ghost_evictable_metadata", KSTAT_DATA_UINT64 },
	{ "l2_hits",			KSTAT_DATA_UINT64 },
	{ "l2_misses",			KSTAT_DATA_UINT64 },
	{ "l2_feeds",			KSTAT_DATA_UINT64 },
	{ "l2_rw_clash",		KSTAT_DATA_UINT64 },
	{ "l2_read_bytes",		KSTAT_DATA_UINT64 },
	{ "l2_write_bytes",		KSTAT_DATA_UINT64 },
	{ "l2_writes_sent",		KSTAT_DATA_UINT64 },
	{ "l2_writes_done",		KSTAT_DATA_UINT64 },
	{ "l2_writes_error",		KSTAT_DATA_UINT64 },
	{ "l2_writes_lock_retry",	KSTAT_DATA_UINT64 },
	{ "l2_evict_lock_retry",	KSTAT_DATA_UINT64 },
	{ "l2_evict_reading",		KSTAT_DATA_UINT64 },
	{ "l2_evict_l1cached",		KSTAT_DATA_UINT64 },
	{ "l2_free_on_write",		KSTAT_DATA_UINT64 },
	{ "l2_cdata_free_on_write",	KSTAT_DATA_UINT64 },
	{ "l2_abort_lowmem",		KSTAT_DATA_UINT64 },
	{ "l2_cksum_bad",		KSTAT_DATA_UINT64 },
	{ "l2_io_error",		KSTAT_DATA_UINT64 },
	{ "l2_size",			KSTAT_DATA_UINT64 },
	{ "l2_asize",			KSTAT_DATA_UINT64 },
	{ "l2_hdr_size",		KSTAT_DATA_UINT64 },
	{ "l2_compress_successes",	KSTAT_DATA_UINT64 },
	{ "l2_compress_zeros",		KSTAT_DATA_UINT64 },
	{ "l2_compress_failures",	KSTAT_DATA_UINT64 },
	{ "memory_throttle_count",	KSTAT_DATA_UINT64 },
	{ "duplicate_buffers",		KSTAT_DATA_UINT64 },
	{ "duplicate_buffers_size",	KSTAT_DATA_UINT64 },
	{ "duplicate_reads",		KSTAT_DATA_UINT64 },
	{ "memory_direct_count",	KSTAT_DATA_UINT64 },
	{ "memory_indirect_count",	KSTAT_DATA_UINT64 },
	{ "arc_no_grow",		KSTAT_DATA_UINT64 },
	{ "arc_tempreserve",		KSTAT_DATA_UINT64 },
	{ "arc_loaned_bytes",		KSTAT_DATA_UINT64 },
	{ "arc_prune",			KSTAT_DATA_UINT64 },
	{ "arc_meta_used",		KSTAT_DATA_UINT64 },
	{ "arc_meta_limit",		KSTAT_DATA_UINT64 },
	{ "arc_meta_max",		KSTAT_DATA_UINT64 },
	{ "arc_meta_min",		KSTAT_DATA_UINT64 },
	{ "arc_need_free",		KSTAT_DATA_UINT64 },
	{ "arc_sys_free",		KSTAT_DATA_UINT64 }
};

#define	ARCSTAT(stat)	(arc_stats.stat.value.ui64)

#define	ARCSTAT_INCR(stat, val) \
	atomic_add_64(&arc_stats.stat.value.ui64, (val))

#define	ARCSTAT_BUMP(stat)	ARCSTAT_INCR(stat, 1)
#define	ARCSTAT_BUMPDOWN(stat)	ARCSTAT_INCR(stat, -1)

#define	ARCSTAT_MAX(stat, val) {					\
	uint64_t m;							\
	while ((val) > (m = arc_stats.stat.value.ui64) &&		\
	    (m != atomic_cas_64(&arc_stats.stat.value.ui64, m, (val))))	\
		continue;						\
}

#define	ARCSTAT_MAXSTAT(stat) \
	ARCSTAT_MAX(stat##_max, arc_stats.stat.value.ui64)

/*
 * We define a macro to allow ARC hits/misses to be easily broken down by
 * two separate conditions, giving a total of four different subtypes for
 * each of hits and misses (so eight statistics total).
 */
#define	ARCSTAT_CONDSTAT(cond1, stat1, notstat1, cond2, stat2, notstat2, stat) \
	if (cond1) {							\
		if (cond2) {						\
			ARCSTAT_BUMP(arcstat_##stat1##_##stat2##_##stat); \
		} else {						\
			ARCSTAT_BUMP(arcstat_##stat1##_##notstat2##_##stat); \
		}							\
	} else {							\
		if (cond2) {						\
			ARCSTAT_BUMP(arcstat_##notstat1##_##stat2##_##stat); \
		} else {						\
			ARCSTAT_BUMP(arcstat_##notstat1##_##notstat2##_##stat);\
		}							\
	}

kstat_t			*arc_ksp;
static arc_state_t	*arc_anon;
static arc_state_t	*arc_mru;
static arc_state_t	*arc_mru_ghost;
static arc_state_t	*arc_mfu;
static arc_state_t	*arc_mfu_ghost;
static arc_state_t	*arc_l2c_only;

/*
 * There are several ARC variables that are critical to export as kstats --
 * but we don't want to have to grovel around in the kstat whenever we wish to
 * manipulate them.  For these variables, we therefore define them to be in
 * terms of the statistic variable.  This assures that we are not introducing
 * the possibility of inconsistency by having shadow copies of the variables,
 * while still allowing the code to be readable.
 */
#define	arc_size	ARCSTAT(arcstat_size)	/* actual total arc size */
#define	arc_p		ARCSTAT(arcstat_p)	/* target size of MRU */
#define	arc_c		ARCSTAT(arcstat_c)	/* target size of cache */
#define	arc_c_min	ARCSTAT(arcstat_c_min)	/* min target cache size */
#define	arc_c_max	ARCSTAT(arcstat_c_max)	/* max target cache size */
#define	arc_no_grow	ARCSTAT(arcstat_no_grow)
#define	arc_tempreserve	ARCSTAT(arcstat_tempreserve)
#define	arc_loaned_bytes	ARCSTAT(arcstat_loaned_bytes)
#define	arc_meta_limit	ARCSTAT(arcstat_meta_limit) /* max size for metadata */
#define	arc_meta_min	ARCSTAT(arcstat_meta_min) /* min size for metadata */
#define	arc_meta_used	ARCSTAT(arcstat_meta_used) /* size of metadata */
#define	arc_meta_max	ARCSTAT(arcstat_meta_max) /* max size of metadata */
#define	arc_need_free	ARCSTAT(arcstat_need_free) /* bytes to be freed */
#define	arc_sys_free	ARCSTAT(arcstat_sys_free) /* target system free bytes */

#define	L2ARC_IS_VALID_COMPRESS(_c_) \
	((_c_) == ZIO_COMPRESS_LZ4 || (_c_) == ZIO_COMPRESS_EMPTY)

static list_t arc_prune_list;
static kmutex_t arc_prune_mtx;
static taskq_t *arc_prune_taskq;
static arc_buf_t *arc_eviction_list;
static arc_buf_hdr_t arc_eviction_hdr;

#define	GHOST_STATE(state)	\
	((state) == arc_mru_ghost || (state) == arc_mfu_ghost ||	\
	(state) == arc_l2c_only)

#define	HDR_IN_HASH_TABLE(hdr)	((hdr)->b_flags & ARC_FLAG_IN_HASH_TABLE)
#define	HDR_IO_IN_PROGRESS(hdr)	((hdr)->b_flags & ARC_FLAG_IO_IN_PROGRESS)
#define	HDR_IO_ERROR(hdr)	((hdr)->b_flags & ARC_FLAG_IO_ERROR)
#define	HDR_PREFETCH(hdr)	((hdr)->b_flags & ARC_FLAG_PREFETCH)
#define	HDR_FREED_IN_READ(hdr)	((hdr)->b_flags & ARC_FLAG_FREED_IN_READ)
#define	HDR_BUF_AVAILABLE(hdr)	((hdr)->b_flags & ARC_FLAG_BUF_AVAILABLE)

#define	HDR_L2CACHE(hdr)	((hdr)->b_flags & ARC_FLAG_L2CACHE)
#define	HDR_L2COMPRESS(hdr)	((hdr)->b_flags & ARC_FLAG_L2COMPRESS)
#define	HDR_L2_READING(hdr)	\
	    (((hdr)->b_flags & ARC_FLAG_IO_IN_PROGRESS) &&	\
	    ((hdr)->b_flags & ARC_FLAG_HAS_L2HDR))
#define	HDR_L2_WRITING(hdr)	((hdr)->b_flags & ARC_FLAG_L2_WRITING)
#define	HDR_L2_EVICTED(hdr)	((hdr)->b_flags & ARC_FLAG_L2_EVICTED)
#define	HDR_L2_WRITE_HEAD(hdr)	((hdr)->b_flags & ARC_FLAG_L2_WRITE_HEAD)

#define	HDR_ISTYPE_METADATA(hdr)	\
	    ((hdr)->b_flags & ARC_FLAG_BUFC_METADATA)
#define	HDR_ISTYPE_DATA(hdr)	(!HDR_ISTYPE_METADATA(hdr))

#define	HDR_HAS_L1HDR(hdr)	((hdr)->b_flags & ARC_FLAG_HAS_L1HDR)
#define	HDR_HAS_L2HDR(hdr)	((hdr)->b_flags & ARC_FLAG_HAS_L2HDR)

/*
 * Other sizes
 */

#define	HDR_FULL_SIZE ((int64_t)sizeof (arc_buf_hdr_t))
#define	HDR_L2ONLY_SIZE ((int64_t)offsetof(arc_buf_hdr_t, b_l1hdr))

/*
 * Hash table routines
 */

#define	HT_LOCK_ALIGN	64
#define	HT_LOCK_PAD	(P2NPHASE(sizeof (kmutex_t), (HT_LOCK_ALIGN)))

struct ht_lock {
	kmutex_t	ht_lock;
#ifdef _KERNEL
	unsigned char	pad[HT_LOCK_PAD];
#endif
};

#define	BUF_LOCKS 8192
typedef struct buf_hash_table {
	uint64_t ht_mask;
	arc_buf_hdr_t **ht_table;
	struct ht_lock ht_locks[BUF_LOCKS];
} buf_hash_table_t;

static buf_hash_table_t buf_hash_table;

#define	BUF_HASH_INDEX(spa, dva, birth) \
	(buf_hash(spa, dva, birth) & buf_hash_table.ht_mask)
#define	BUF_HASH_LOCK_NTRY(idx) (buf_hash_table.ht_locks[idx & (BUF_LOCKS-1)])
#define	BUF_HASH_LOCK(idx)	(&(BUF_HASH_LOCK_NTRY(idx).ht_lock))
#define	HDR_LOCK(hdr) \
	(BUF_HASH_LOCK(BUF_HASH_INDEX(hdr->b_spa, &hdr->b_dva, hdr->b_birth)))

uint64_t zfs_crc64_table[256];

/*
 * Level 2 ARC
 */

#define	L2ARC_WRITE_SIZE	(8 * 1024 * 1024)	/* initial write max */
#define	L2ARC_HEADROOM		2			/* num of writes */
/*
 * If we discover during ARC scan any buffers to be compressed, we boost
 * our headroom for the next scanning cycle by this percentage multiple.
 */
#define	L2ARC_HEADROOM_BOOST	200
#define	L2ARC_FEED_SECS		1		/* caching interval secs */
#define	L2ARC_FEED_MIN_MS	200		/* min caching interval ms */

/*
 * Used to distinguish headers that are being process by
 * l2arc_write_buffers(), but have yet to be assigned to a l2arc disk
 * address. This can happen when the header is added to the l2arc's list
 * of buffers to write in the first stage of l2arc_write_buffers(), but
 * has not yet been written out which happens in the second stage of
 * l2arc_write_buffers().
 */
#define	L2ARC_ADDR_UNSET	((uint64_t)(-1))

#define	l2arc_writes_sent	ARCSTAT(arcstat_l2_writes_sent)
#define	l2arc_writes_done	ARCSTAT(arcstat_l2_writes_done)

/* L2ARC Performance Tunables */
unsigned long l2arc_write_max = L2ARC_WRITE_SIZE;	/* def max write size */
unsigned long l2arc_write_boost = L2ARC_WRITE_SIZE;	/* extra warmup write */
unsigned long l2arc_headroom = L2ARC_HEADROOM;		/* # of dev writes */
unsigned long l2arc_headroom_boost = L2ARC_HEADROOM_BOOST;
unsigned long l2arc_feed_secs = L2ARC_FEED_SECS;	/* interval seconds */
unsigned long l2arc_feed_min_ms = L2ARC_FEED_MIN_MS;	/* min interval msecs */
int l2arc_noprefetch = B_TRUE;			/* don't cache prefetch bufs */
int l2arc_nocompress = B_FALSE;			/* don't compress bufs */
int l2arc_feed_again = B_TRUE;			/* turbo warmup */
int l2arc_norw = B_FALSE;			/* no reads during writes */

/*
 * L2ARC Internals
 */
static list_t L2ARC_dev_list;			/* device list */
static list_t *l2arc_dev_list;			/* device list pointer */
static kmutex_t l2arc_dev_mtx;			/* device list mutex */
static l2arc_dev_t *l2arc_dev_last;		/* last device used */
static list_t L2ARC_free_on_write;		/* free after write buf list */
static list_t *l2arc_free_on_write;		/* free after write list ptr */
static kmutex_t l2arc_free_on_write_mtx;	/* mutex for list */
static uint64_t l2arc_ndev;			/* number of devices */

typedef struct l2arc_read_callback {
	arc_buf_t		*l2rcb_buf;		/* read buffer */
	spa_t			*l2rcb_spa;		/* spa */
	blkptr_t		l2rcb_bp;		/* original blkptr */
	zbookmark_phys_t	l2rcb_zb;		/* original bookmark */
	int			l2rcb_flags;		/* original flags */
	enum zio_compress	l2rcb_compress;		/* applied compress */
} l2arc_read_callback_t;

typedef struct l2arc_data_free {
	/* protected by l2arc_free_on_write_mtx */
	void		*l2df_data;
	size_t		l2df_size;
	void		(*l2df_func)(void *, size_t);
	list_node_t	l2df_list_node;
} l2arc_data_free_t;

static kmutex_t l2arc_feed_thr_lock;
static kcondvar_t l2arc_feed_thr_cv;
static uint8_t l2arc_thread_exit;

static void arc_get_data_buf(arc_buf_t *);
static void arc_access(arc_buf_hdr_t *, kmutex_t *);
static boolean_t arc_is_overflowing(void);
static void arc_buf_watch(arc_buf_t *);
static void arc_tuning_update(void);

static arc_buf_contents_t arc_buf_type(arc_buf_hdr_t *);
static uint32_t arc_bufc_to_flags(arc_buf_contents_t);

static boolean_t l2arc_write_eligible(uint64_t, arc_buf_hdr_t *);
static void l2arc_read_done(zio_t *);

static boolean_t l2arc_compress_buf(arc_buf_hdr_t *);
static void l2arc_decompress_zio(zio_t *, arc_buf_hdr_t *, enum zio_compress);
static void l2arc_release_cdata_buf(arc_buf_hdr_t *);

static uint64_t
buf_hash(uint64_t spa, const dva_t *dva, uint64_t birth)
{
	uint8_t *vdva = (uint8_t *)dva;
	uint64_t crc = -1ULL;
	int i;

	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);

	for (i = 0; i < sizeof (dva_t); i++)
		crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ vdva[i]) & 0xFF];

	crc ^= (spa>>8) ^ birth;

	return (crc);
}

#define	BUF_EMPTY(buf)						\
	((buf)->b_dva.dva_word[0] == 0 &&			\
	(buf)->b_dva.dva_word[1] == 0)

#define	BUF_EQUAL(spa, dva, birth, buf)				\
	((buf)->b_dva.dva_word[0] == (dva)->dva_word[0]) &&	\
	((buf)->b_dva.dva_word[1] == (dva)->dva_word[1]) &&	\
	((buf)->b_birth == birth) && ((buf)->b_spa == spa)

static void
buf_discard_identity(arc_buf_hdr_t *hdr)
{
	hdr->b_dva.dva_word[0] = 0;
	hdr->b_dva.dva_word[1] = 0;
	hdr->b_birth = 0;
}

static arc_buf_hdr_t *
buf_hash_find(uint64_t spa, const blkptr_t *bp, kmutex_t **lockp)
{
	const dva_t *dva = BP_IDENTITY(bp);
	uint64_t birth = BP_PHYSICAL_BIRTH(bp);
	uint64_t idx = BUF_HASH_INDEX(spa, dva, birth);
	kmutex_t *hash_lock = BUF_HASH_LOCK(idx);
	arc_buf_hdr_t *hdr;

	mutex_enter(hash_lock);
	for (hdr = buf_hash_table.ht_table[idx]; hdr != NULL;
	    hdr = hdr->b_hash_next) {
		if (BUF_EQUAL(spa, dva, birth, hdr)) {
			*lockp = hash_lock;
			return (hdr);
		}
	}
	mutex_exit(hash_lock);
	*lockp = NULL;
	return (NULL);
}

/*
 * Insert an entry into the hash table.  If there is already an element
 * equal to elem in the hash table, then the already existing element
 * will be returned and the new element will not be inserted.
 * Otherwise returns NULL.
 * If lockp == NULL, the caller is assumed to already hold the hash lock.
 */
static arc_buf_hdr_t *
buf_hash_insert(arc_buf_hdr_t *hdr, kmutex_t **lockp)
{
	uint64_t idx = BUF_HASH_INDEX(hdr->b_spa, &hdr->b_dva, hdr->b_birth);
	kmutex_t *hash_lock = BUF_HASH_LOCK(idx);
	arc_buf_hdr_t *fhdr;
	uint32_t i;

	ASSERT(!DVA_IS_EMPTY(&hdr->b_dva));
	ASSERT(hdr->b_birth != 0);
	ASSERT(!HDR_IN_HASH_TABLE(hdr));

	if (lockp != NULL) {
		*lockp = hash_lock;
		mutex_enter(hash_lock);
	} else {
		ASSERT(MUTEX_HELD(hash_lock));
	}

	for (fhdr = buf_hash_table.ht_table[idx], i = 0; fhdr != NULL;
	    fhdr = fhdr->b_hash_next, i++) {
		if (BUF_EQUAL(hdr->b_spa, &hdr->b_dva, hdr->b_birth, fhdr))
			return (fhdr);
	}

	hdr->b_hash_next = buf_hash_table.ht_table[idx];
	buf_hash_table.ht_table[idx] = hdr;
	hdr->b_flags |= ARC_FLAG_IN_HASH_TABLE;

	/* collect some hash table performance data */
	if (i > 0) {
		ARCSTAT_BUMP(arcstat_hash_collisions);
		if (i == 1)
			ARCSTAT_BUMP(arcstat_hash_chains);

		ARCSTAT_MAX(arcstat_hash_chain_max, i);
	}

	ARCSTAT_BUMP(arcstat_hash_elements);
	ARCSTAT_MAXSTAT(arcstat_hash_elements);

	return (NULL);
}

static void
buf_hash_remove(arc_buf_hdr_t *hdr)
{
	arc_buf_hdr_t *fhdr, **hdrp;
	uint64_t idx = BUF_HASH_INDEX(hdr->b_spa, &hdr->b_dva, hdr->b_birth);

	ASSERT(MUTEX_HELD(BUF_HASH_LOCK(idx)));
	ASSERT(HDR_IN_HASH_TABLE(hdr));

	hdrp = &buf_hash_table.ht_table[idx];
	while ((fhdr = *hdrp) != hdr) {
		ASSERT(fhdr != NULL);
		hdrp = &fhdr->b_hash_next;
	}
	*hdrp = hdr->b_hash_next;
	hdr->b_hash_next = NULL;
	hdr->b_flags &= ~ARC_FLAG_IN_HASH_TABLE;

	/* collect some hash table performance data */
	ARCSTAT_BUMPDOWN(arcstat_hash_elements);

	if (buf_hash_table.ht_table[idx] &&
	    buf_hash_table.ht_table[idx]->b_hash_next == NULL)
		ARCSTAT_BUMPDOWN(arcstat_hash_chains);
}

/*
 * Global data structures and functions for the buf kmem cache.
 */
static kmem_cache_t *hdr_full_cache;
static kmem_cache_t *hdr_l2only_cache;
static kmem_cache_t *buf_cache;

static void
buf_fini(void)
{
	int i;

#if defined(_KERNEL) && defined(HAVE_SPL)
	/*
	 * Large allocations which do not require contiguous pages
	 * should be using vmem_free() in the linux kernel\
	 */
	vmem_free(buf_hash_table.ht_table,
	    (buf_hash_table.ht_mask + 1) * sizeof (void *));
#else
	kmem_free(buf_hash_table.ht_table,
	    (buf_hash_table.ht_mask + 1) * sizeof (void *));
#endif
	for (i = 0; i < BUF_LOCKS; i++)
		mutex_destroy(&buf_hash_table.ht_locks[i].ht_lock);
	kmem_cache_destroy(hdr_full_cache);
	kmem_cache_destroy(hdr_l2only_cache);
	kmem_cache_destroy(buf_cache);
}

/*
 * Constructor callback - called when the cache is empty
 * and a new buf is requested.
 */
/* ARGSUSED */
static int
hdr_full_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_hdr_t *hdr = vbuf;

	bzero(hdr, HDR_FULL_SIZE);
	cv_init(&hdr->b_l1hdr.b_cv, NULL, CV_DEFAULT, NULL);
	refcount_create(&hdr->b_l1hdr.b_refcnt);
	mutex_init(&hdr->b_l1hdr.b_freeze_lock, NULL, MUTEX_DEFAULT, NULL);
	list_link_init(&hdr->b_l1hdr.b_arc_node);
	list_link_init(&hdr->b_l2hdr.b_l2node);
	multilist_link_init(&hdr->b_l1hdr.b_arc_node);
	arc_space_consume(HDR_FULL_SIZE, ARC_SPACE_HDRS);

	return (0);
}

/* ARGSUSED */
static int
hdr_l2only_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_hdr_t *hdr = vbuf;

	bzero(hdr, HDR_L2ONLY_SIZE);
	arc_space_consume(HDR_L2ONLY_SIZE, ARC_SPACE_L2HDRS);

	return (0);
}

/* ARGSUSED */
static int
buf_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_t *buf = vbuf;

	bzero(buf, sizeof (arc_buf_t));
	mutex_init(&buf->b_evict_lock, NULL, MUTEX_DEFAULT, NULL);
	arc_space_consume(sizeof (arc_buf_t), ARC_SPACE_HDRS);

	return (0);
}

/*
 * Destructor callback - called when a cached buf is
 * no longer required.
 */
/* ARGSUSED */
static void
hdr_full_dest(void *vbuf, void *unused)
{
	arc_buf_hdr_t *hdr = vbuf;

	ASSERT(BUF_EMPTY(hdr));
	cv_destroy(&hdr->b_l1hdr.b_cv);
	refcount_destroy(&hdr->b_l1hdr.b_refcnt);
	mutex_destroy(&hdr->b_l1hdr.b_freeze_lock);
	ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
	arc_space_return(HDR_FULL_SIZE, ARC_SPACE_HDRS);
}

/* ARGSUSED */
static void
hdr_l2only_dest(void *vbuf, void *unused)
{
	ASSERTV(arc_buf_hdr_t *hdr = vbuf);

	ASSERT(BUF_EMPTY(hdr));
	arc_space_return(HDR_L2ONLY_SIZE, ARC_SPACE_L2HDRS);
}

/* ARGSUSED */
static void
buf_dest(void *vbuf, void *unused)
{
	arc_buf_t *buf = vbuf;

	mutex_destroy(&buf->b_evict_lock);
	arc_space_return(sizeof (arc_buf_t), ARC_SPACE_HDRS);
}

/*
 * Reclaim callback -- invoked when memory is low.
 */
/* ARGSUSED */
static void
hdr_recl(void *unused)
{
	dprintf("hdr_recl called\n");
	/*
	 * umem calls the reclaim func when we destroy the buf cache,
	 * which is after we do arc_fini().
	 */
	if (!arc_dead)
		cv_signal(&arc_reclaim_thread_cv);
}

static void
buf_init(void)
{
	uint64_t *ct;
	uint64_t hsize = 1ULL << 12;
	int i, j;

	/*
	 * The hash table is big enough to fill all of physical memory
	 * with an average block size of zfs_arc_average_blocksize (default 8K).
	 * By default, the table will take up
	 * totalmem * sizeof(void*) / 8K (1MB per GB with 8-byte pointers).
	 */
	while (hsize * zfs_arc_average_blocksize < physmem * PAGESIZE)
		hsize <<= 1;
retry:
	buf_hash_table.ht_mask = hsize - 1;
#if defined(_KERNEL) && defined(HAVE_SPL)
	/*
	 * Large allocations which do not require contiguous pages
	 * should be using vmem_alloc() in the linux kernel
	 */
	buf_hash_table.ht_table =
	    vmem_zalloc(hsize * sizeof (void*), KM_SLEEP);
#else
	buf_hash_table.ht_table =
	    kmem_zalloc(hsize * sizeof (void*), KM_NOSLEEP);
#endif
	if (buf_hash_table.ht_table == NULL) {
		ASSERT(hsize > (1ULL << 8));
		hsize >>= 1;
		goto retry;
	}

	hdr_full_cache = kmem_cache_create("arc_buf_hdr_t_full", HDR_FULL_SIZE,
	    0, hdr_full_cons, hdr_full_dest, hdr_recl, NULL, NULL, 0);
	hdr_l2only_cache = kmem_cache_create("arc_buf_hdr_t_l2only",
	    HDR_L2ONLY_SIZE, 0, hdr_l2only_cons, hdr_l2only_dest, hdr_recl,
	    NULL, NULL, 0);
	buf_cache = kmem_cache_create("arc_buf_t", sizeof (arc_buf_t),
	    0, buf_cons, buf_dest, NULL, NULL, NULL, 0);

	for (i = 0; i < 256; i++)
		for (ct = zfs_crc64_table + i, *ct = i, j = 8; j > 0; j--)
			*ct = (*ct >> 1) ^ (-(*ct & 1) & ZFS_CRC64_POLY);

	for (i = 0; i < BUF_LOCKS; i++) {
		mutex_init(&buf_hash_table.ht_locks[i].ht_lock,
		    NULL, MUTEX_DEFAULT, NULL);
	}
}

/*
 * Transition between the two allocation states for the arc_buf_hdr struct.
 * The arc_buf_hdr struct can be allocated with (hdr_full_cache) or without
 * (hdr_l2only_cache) the fields necessary for the L1 cache - the smaller
 * version is used when a cache buffer is only in the L2ARC in order to reduce
 * memory usage.
 */
static arc_buf_hdr_t *
arc_hdr_realloc(arc_buf_hdr_t *hdr, kmem_cache_t *old, kmem_cache_t *new)
{
	arc_buf_hdr_t *nhdr;
	l2arc_dev_t *dev;

	ASSERT(HDR_HAS_L2HDR(hdr));
	ASSERT((old == hdr_full_cache && new == hdr_l2only_cache) ||
	    (old == hdr_l2only_cache && new == hdr_full_cache));

	dev = hdr->b_l2hdr.b_dev;
	nhdr = kmem_cache_alloc(new, KM_PUSHPAGE);

	ASSERT(MUTEX_HELD(HDR_LOCK(hdr)));
	buf_hash_remove(hdr);

	bcopy(hdr, nhdr, HDR_L2ONLY_SIZE);

	if (new == hdr_full_cache) {
		nhdr->b_flags |= ARC_FLAG_HAS_L1HDR;
		/*
		 * arc_access and arc_change_state need to be aware that a
		 * header has just come out of L2ARC, so we set its state to
		 * l2c_only even though it's about to change.
		 */
		nhdr->b_l1hdr.b_state = arc_l2c_only;

		/* Verify previous threads set to NULL before freeing */
		ASSERT3P(nhdr->b_l1hdr.b_tmp_cdata, ==, NULL);
	} else {
		ASSERT(hdr->b_l1hdr.b_buf == NULL);
		ASSERT0(hdr->b_l1hdr.b_datacnt);

		/*
		 * If we've reached here, We must have been called from
		 * arc_evict_hdr(), as such we should have already been
		 * removed from any ghost list we were previously on
		 * (which protects us from racing with arc_evict_state),
		 * thus no locking is needed during this check.
		 */
		ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));

		/*
		 * A buffer must not be moved into the arc_l2c_only
		 * state if it's not finished being written out to the
		 * l2arc device. Otherwise, the b_l1hdr.b_tmp_cdata field
		 * might try to be accessed, even though it was removed.
		 */
		VERIFY(!HDR_L2_WRITING(hdr));
		VERIFY3P(hdr->b_l1hdr.b_tmp_cdata, ==, NULL);

		nhdr->b_flags &= ~ARC_FLAG_HAS_L1HDR;
	}
	/*
	 * The header has been reallocated so we need to re-insert it into any
	 * lists it was on.
	 */
	(void) buf_hash_insert(nhdr, NULL);

	ASSERT(list_link_active(&hdr->b_l2hdr.b_l2node));

	mutex_enter(&dev->l2ad_mtx);

	/*
	 * We must place the realloc'ed header back into the list at
	 * the same spot. Otherwise, if it's placed earlier in the list,
	 * l2arc_write_buffers() could find it during the function's
	 * write phase, and try to write it out to the l2arc.
	 */
	list_insert_after(&dev->l2ad_buflist, hdr, nhdr);
	list_remove(&dev->l2ad_buflist, hdr);

	mutex_exit(&dev->l2ad_mtx);

	/*
	 * Since we're using the pointer address as the tag when
	 * incrementing and decrementing the l2ad_alloc refcount, we
	 * must remove the old pointer (that we're about to destroy) and
	 * add the new pointer to the refcount. Otherwise we'd remove
	 * the wrong pointer address when calling arc_hdr_destroy() later.
	 */

	(void) refcount_remove_many(&dev->l2ad_alloc,
	    hdr->b_l2hdr.b_asize, hdr);

	(void) refcount_add_many(&dev->l2ad_alloc,
	    nhdr->b_l2hdr.b_asize, nhdr);

	buf_discard_identity(hdr);
	hdr->b_freeze_cksum = NULL;
	kmem_cache_free(old, hdr);

	return (nhdr);
}


#define	ARC_MINTIME	(hz>>4) /* 62 ms */

static void
arc_cksum_verify(arc_buf_t *buf)
{
	zio_cksum_t zc;

	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		return;

	mutex_enter(&buf->b_hdr->b_l1hdr.b_freeze_lock);
	if (buf->b_hdr->b_freeze_cksum == NULL || HDR_IO_ERROR(buf->b_hdr)) {
		mutex_exit(&buf->b_hdr->b_l1hdr.b_freeze_lock);
		return;
	}
	fletcher_2_native(buf->b_data, buf->b_hdr->b_size, &zc);
	if (!ZIO_CHECKSUM_EQUAL(*buf->b_hdr->b_freeze_cksum, zc))
		panic("buffer modified while frozen!");
	mutex_exit(&buf->b_hdr->b_l1hdr.b_freeze_lock);
}

static int
arc_cksum_equal(arc_buf_t *buf)
{
	zio_cksum_t zc;
	int equal;

	mutex_enter(&buf->b_hdr->b_l1hdr.b_freeze_lock);
	fletcher_2_native(buf->b_data, buf->b_hdr->b_size, &zc);
	equal = ZIO_CHECKSUM_EQUAL(*buf->b_hdr->b_freeze_cksum, zc);
	mutex_exit(&buf->b_hdr->b_l1hdr.b_freeze_lock);

	return (equal);
}

static void
arc_cksum_compute(arc_buf_t *buf, boolean_t force)
{
	if (!force && !(zfs_flags & ZFS_DEBUG_MODIFY))
		return;

	mutex_enter(&buf->b_hdr->b_l1hdr.b_freeze_lock);
	if (buf->b_hdr->b_freeze_cksum != NULL) {
		mutex_exit(&buf->b_hdr->b_l1hdr.b_freeze_lock);
		return;
	}
	buf->b_hdr->b_freeze_cksum = kmem_alloc(sizeof (zio_cksum_t), KM_SLEEP);
	fletcher_2_native(buf->b_data, buf->b_hdr->b_size,
	    buf->b_hdr->b_freeze_cksum);
	mutex_exit(&buf->b_hdr->b_l1hdr.b_freeze_lock);
	arc_buf_watch(buf);
}

#ifndef _KERNEL
void
arc_buf_sigsegv(int sig, siginfo_t *si, void *unused)
{
	panic("Got SIGSEGV at address: 0x%lx\n", (long) si->si_addr);
}
#endif

/* ARGSUSED */
static void
arc_buf_unwatch(arc_buf_t *buf)
{
#ifndef _KERNEL
	if (arc_watch) {
		ASSERT0(mprotect(buf->b_data, buf->b_hdr->b_size,
		    PROT_READ | PROT_WRITE));
	}
#endif
}

/* ARGSUSED */
static void
arc_buf_watch(arc_buf_t *buf)
{
#ifndef _KERNEL
	if (arc_watch)
		ASSERT0(mprotect(buf->b_data, buf->b_hdr->b_size, PROT_READ));
#endif
}

static arc_buf_contents_t
arc_buf_type(arc_buf_hdr_t *hdr)
{
	if (HDR_ISTYPE_METADATA(hdr)) {
		return (ARC_BUFC_METADATA);
	} else {
		return (ARC_BUFC_DATA);
	}
}

static uint32_t
arc_bufc_to_flags(arc_buf_contents_t type)
{
	switch (type) {
	case ARC_BUFC_DATA:
		/* metadata field is 0 if buffer contains normal data */
		return (0);
	case ARC_BUFC_METADATA:
		return (ARC_FLAG_BUFC_METADATA);
	default:
		break;
	}
	panic("undefined ARC buffer type!");
	return ((uint32_t)-1);
}

void
arc_buf_thaw(arc_buf_t *buf)
{
	if (zfs_flags & ZFS_DEBUG_MODIFY) {
		if (buf->b_hdr->b_l1hdr.b_state != arc_anon)
			panic("modifying non-anon buffer!");
		if (HDR_IO_IN_PROGRESS(buf->b_hdr))
			panic("modifying buffer while i/o in progress!");
		arc_cksum_verify(buf);
	}

	mutex_enter(&buf->b_hdr->b_l1hdr.b_freeze_lock);
	if (buf->b_hdr->b_freeze_cksum != NULL) {
		kmem_free(buf->b_hdr->b_freeze_cksum, sizeof (zio_cksum_t));
		buf->b_hdr->b_freeze_cksum = NULL;
	}

	mutex_exit(&buf->b_hdr->b_l1hdr.b_freeze_lock);

	arc_buf_unwatch(buf);
}

void
arc_buf_freeze(arc_buf_t *buf)
{
	kmutex_t *hash_lock;

	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		return;

	hash_lock = HDR_LOCK(buf->b_hdr);
	mutex_enter(hash_lock);

	ASSERT(buf->b_hdr->b_freeze_cksum != NULL ||
	    buf->b_hdr->b_l1hdr.b_state == arc_anon);
	arc_cksum_compute(buf, B_FALSE);
	mutex_exit(hash_lock);

}

static void
add_reference(arc_buf_hdr_t *hdr, kmutex_t *hash_lock, void *tag)
{
	arc_state_t *state;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(MUTEX_HELD(hash_lock));

	state = hdr->b_l1hdr.b_state;

	if ((refcount_add(&hdr->b_l1hdr.b_refcnt, tag) == 1) &&
	    (state != arc_anon)) {
		/* We don't use the L2-only state list. */
		if (state != arc_l2c_only) {
			arc_buf_contents_t type = arc_buf_type(hdr);
			uint64_t delta = hdr->b_size * hdr->b_l1hdr.b_datacnt;
			multilist_t *list = &state->arcs_list[type];
			uint64_t *size = &state->arcs_lsize[type];

			multilist_remove(list, hdr);

			if (GHOST_STATE(state)) {
				ASSERT0(hdr->b_l1hdr.b_datacnt);
				ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);
				delta = hdr->b_size;
			}
			ASSERT(delta > 0);
			ASSERT3U(*size, >=, delta);
			atomic_add_64(size, -delta);
		}
		/* remove the prefetch flag if we get a reference */
		hdr->b_flags &= ~ARC_FLAG_PREFETCH;
	}
}

static int
remove_reference(arc_buf_hdr_t *hdr, kmutex_t *hash_lock, void *tag)
{
	int cnt;
	arc_state_t *state = hdr->b_l1hdr.b_state;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(state == arc_anon || MUTEX_HELD(hash_lock));
	ASSERT(!GHOST_STATE(state));

	/*
	 * arc_l2c_only counts as a ghost state so we don't need to explicitly
	 * check to prevent usage of the arc_l2c_only list.
	 */
	if (((cnt = refcount_remove(&hdr->b_l1hdr.b_refcnt, tag)) == 0) &&
	    (state != arc_anon)) {
		arc_buf_contents_t type = arc_buf_type(hdr);
		multilist_t *list = &state->arcs_list[type];
		uint64_t *size = &state->arcs_lsize[type];

		multilist_insert(list, hdr);

		ASSERT(hdr->b_l1hdr.b_datacnt > 0);
		atomic_add_64(size, hdr->b_size *
		    hdr->b_l1hdr.b_datacnt);
	}
	return (cnt);
}

/*
 * Returns detailed information about a specific arc buffer.  When the
 * state_index argument is set the function will calculate the arc header
 * list position for its arc state.  Since this requires a linear traversal
 * callers are strongly encourage not to do this.  However, it can be helpful
 * for targeted analysis so the functionality is provided.
 */
void
arc_buf_info(arc_buf_t *ab, arc_buf_info_t *abi, int state_index)
{
	arc_buf_hdr_t *hdr = ab->b_hdr;
	l1arc_buf_hdr_t *l1hdr = NULL;
	l2arc_buf_hdr_t *l2hdr = NULL;
	arc_state_t *state = NULL;

	memset(abi, 0, sizeof (arc_buf_info_t));

	if (hdr == NULL)
		return;

	abi->abi_flags = hdr->b_flags;

	if (HDR_HAS_L1HDR(hdr)) {
		l1hdr = &hdr->b_l1hdr;
		state = l1hdr->b_state;
	}
	if (HDR_HAS_L2HDR(hdr))
		l2hdr = &hdr->b_l2hdr;

	if (l1hdr) {
		abi->abi_datacnt = l1hdr->b_datacnt;
		abi->abi_access = l1hdr->b_arc_access;
		abi->abi_mru_hits = l1hdr->b_mru_hits;
		abi->abi_mru_ghost_hits = l1hdr->b_mru_ghost_hits;
		abi->abi_mfu_hits = l1hdr->b_mfu_hits;
		abi->abi_mfu_ghost_hits = l1hdr->b_mfu_ghost_hits;
		abi->abi_holds = refcount_count(&l1hdr->b_refcnt);
	}

	if (l2hdr) {
		abi->abi_l2arc_dattr = l2hdr->b_daddr;
		abi->abi_l2arc_asize = l2hdr->b_asize;
		abi->abi_l2arc_compress = l2hdr->b_compress;
		abi->abi_l2arc_hits = l2hdr->b_hits;
	}

	abi->abi_state_type = state ? state->arcs_state : ARC_STATE_ANON;
	abi->abi_state_contents = arc_buf_type(hdr);
	abi->abi_size = hdr->b_size;
}

/*
 * Move the supplied buffer to the indicated state. The hash lock
 * for the buffer must be held by the caller.
 */
static void
arc_change_state(arc_state_t *new_state, arc_buf_hdr_t *hdr,
    kmutex_t *hash_lock)
{
	arc_state_t *old_state;
	int64_t refcnt;
	uint32_t datacnt;
	uint64_t from_delta, to_delta;
	arc_buf_contents_t buftype = arc_buf_type(hdr);

	/*
	 * We almost always have an L1 hdr here, since we call arc_hdr_realloc()
	 * in arc_read() when bringing a buffer out of the L2ARC.  However, the
	 * L1 hdr doesn't always exist when we change state to arc_anon before
	 * destroying a header, in which case reallocating to add the L1 hdr is
	 * pointless.
	 */
	if (HDR_HAS_L1HDR(hdr)) {
		old_state = hdr->b_l1hdr.b_state;
		refcnt = refcount_count(&hdr->b_l1hdr.b_refcnt);
		datacnt = hdr->b_l1hdr.b_datacnt;
	} else {
		old_state = arc_l2c_only;
		refcnt = 0;
		datacnt = 0;
	}

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT3P(new_state, !=, old_state);
	ASSERT(refcnt == 0 || datacnt > 0);
	ASSERT(!GHOST_STATE(new_state) || datacnt == 0);
	ASSERT(old_state != arc_anon || datacnt <= 1);

	from_delta = to_delta = datacnt * hdr->b_size;

	/*
	 * If this buffer is evictable, transfer it from the
	 * old state list to the new state list.
	 */
	if (refcnt == 0) {
		if (old_state != arc_anon && old_state != arc_l2c_only) {
			uint64_t *size = &old_state->arcs_lsize[buftype];

			ASSERT(HDR_HAS_L1HDR(hdr));
			multilist_remove(&old_state->arcs_list[buftype], hdr);

			/*
			 * If prefetching out of the ghost cache,
			 * we will have a non-zero datacnt.
			 */
			if (GHOST_STATE(old_state) && datacnt == 0) {
				/* ghost elements have a ghost size */
				ASSERT(hdr->b_l1hdr.b_buf == NULL);
				from_delta = hdr->b_size;
			}
			ASSERT3U(*size, >=, from_delta);
			atomic_add_64(size, -from_delta);
		}
		if (new_state != arc_anon && new_state != arc_l2c_only) {
			uint64_t *size = &new_state->arcs_lsize[buftype];

			/*
			 * An L1 header always exists here, since if we're
			 * moving to some L1-cached state (i.e. not l2c_only or
			 * anonymous), we realloc the header to add an L1hdr
			 * beforehand.
			 */
			ASSERT(HDR_HAS_L1HDR(hdr));
			multilist_insert(&new_state->arcs_list[buftype], hdr);

			/* ghost elements have a ghost size */
			if (GHOST_STATE(new_state)) {
				ASSERT0(datacnt);
				ASSERT(hdr->b_l1hdr.b_buf == NULL);
				to_delta = hdr->b_size;
			}
			atomic_add_64(size, to_delta);
		}
	}

	ASSERT(!BUF_EMPTY(hdr));
	if (new_state == arc_anon && HDR_IN_HASH_TABLE(hdr))
		buf_hash_remove(hdr);

	/* adjust state sizes (ignore arc_l2c_only) */

	if (to_delta && new_state != arc_l2c_only) {
		ASSERT(HDR_HAS_L1HDR(hdr));
		if (GHOST_STATE(new_state)) {
			ASSERT0(datacnt);

			/*
			 * We moving a header to a ghost state, we first
			 * remove all arc buffers. Thus, we'll have a
			 * datacnt of zero, and no arc buffer to use for
			 * the reference. As a result, we use the arc
			 * header pointer for the reference.
			 */
			(void) refcount_add_many(&new_state->arcs_size,
			    hdr->b_size, hdr);
		} else {
			arc_buf_t *buf;
			ASSERT3U(datacnt, !=, 0);

			/*
			 * Each individual buffer holds a unique reference,
			 * thus we must remove each of these references one
			 * at a time.
			 */
			for (buf = hdr->b_l1hdr.b_buf; buf != NULL;
			    buf = buf->b_next) {
				(void) refcount_add_many(&new_state->arcs_size,
				    hdr->b_size, buf);
			}
		}
	}

	if (from_delta && old_state != arc_l2c_only) {
		ASSERT(HDR_HAS_L1HDR(hdr));
		if (GHOST_STATE(old_state)) {
			/*
			 * When moving a header off of a ghost state,
			 * there's the possibility for datacnt to be
			 * non-zero. This is because we first add the
			 * arc buffer to the header prior to changing
			 * the header's state. Since we used the header
			 * for the reference when putting the header on
			 * the ghost state, we must balance that and use
			 * the header when removing off the ghost state
			 * (even though datacnt is non zero).
			 */

			IMPLY(datacnt == 0, new_state == arc_anon ||
			    new_state == arc_l2c_only);

			(void) refcount_remove_many(&old_state->arcs_size,
			    hdr->b_size, hdr);
		} else {
			arc_buf_t *buf;
			ASSERT3U(datacnt, !=, 0);

			/*
			 * Each individual buffer holds a unique reference,
			 * thus we must remove each of these references one
			 * at a time.
			 */
			for (buf = hdr->b_l1hdr.b_buf; buf != NULL;
			    buf = buf->b_next) {
				(void) refcount_remove_many(
				    &old_state->arcs_size, hdr->b_size, buf);
			}
		}
	}

	if (HDR_HAS_L1HDR(hdr))
		hdr->b_l1hdr.b_state = new_state;

	/*
	 * L2 headers should never be on the L2 state list since they don't
	 * have L1 headers allocated.
	 */
	ASSERT(multilist_is_empty(&arc_l2c_only->arcs_list[ARC_BUFC_DATA]) &&
	    multilist_is_empty(&arc_l2c_only->arcs_list[ARC_BUFC_METADATA]));
}

void
arc_space_consume(uint64_t space, arc_space_type_t type)
{
	ASSERT(type >= 0 && type < ARC_SPACE_NUMTYPES);

	switch (type) {
	default:
		break;
	case ARC_SPACE_DATA:
		ARCSTAT_INCR(arcstat_data_size, space);
		break;
	case ARC_SPACE_META:
		ARCSTAT_INCR(arcstat_metadata_size, space);
		break;
	case ARC_SPACE_OTHER:
		ARCSTAT_INCR(arcstat_other_size, space);
		break;
	case ARC_SPACE_HDRS:
		ARCSTAT_INCR(arcstat_hdr_size, space);
		break;
	case ARC_SPACE_L2HDRS:
		ARCSTAT_INCR(arcstat_l2_hdr_size, space);
		break;
	}

	if (type != ARC_SPACE_DATA)
		ARCSTAT_INCR(arcstat_meta_used, space);

	atomic_add_64(&arc_size, space);
}

void
arc_space_return(uint64_t space, arc_space_type_t type)
{
	ASSERT(type >= 0 && type < ARC_SPACE_NUMTYPES);

	switch (type) {
	default:
		break;
	case ARC_SPACE_DATA:
		ARCSTAT_INCR(arcstat_data_size, -space);
		break;
	case ARC_SPACE_META:
		ARCSTAT_INCR(arcstat_metadata_size, -space);
		break;
	case ARC_SPACE_OTHER:
		ARCSTAT_INCR(arcstat_other_size, -space);
		break;
	case ARC_SPACE_HDRS:
		ARCSTAT_INCR(arcstat_hdr_size, -space);
		break;
	case ARC_SPACE_L2HDRS:
		ARCSTAT_INCR(arcstat_l2_hdr_size, -space);
		break;
	}

	if (type != ARC_SPACE_DATA) {
		ASSERT(arc_meta_used >= space);
		if (arc_meta_max < arc_meta_used)
			arc_meta_max = arc_meta_used;
		ARCSTAT_INCR(arcstat_meta_used, -space);
	}

	ASSERT(arc_size >= space);
	atomic_add_64(&arc_size, -space);
}

arc_buf_t *
arc_buf_alloc(spa_t *spa, uint64_t size, void *tag, arc_buf_contents_t type)
{
	arc_buf_hdr_t *hdr;
	arc_buf_t *buf;

	VERIFY3U(size, <=, spa_maxblocksize(spa));
	hdr = kmem_cache_alloc(hdr_full_cache, KM_PUSHPAGE);
	ASSERT(BUF_EMPTY(hdr));
	ASSERT3P(hdr->b_freeze_cksum, ==, NULL);
	hdr->b_size = size;
	hdr->b_spa = spa_load_guid(spa);
	hdr->b_l1hdr.b_mru_hits = 0;
	hdr->b_l1hdr.b_mru_ghost_hits = 0;
	hdr->b_l1hdr.b_mfu_hits = 0;
	hdr->b_l1hdr.b_mfu_ghost_hits = 0;
	hdr->b_l1hdr.b_l2_hits = 0;

	buf = kmem_cache_alloc(buf_cache, KM_PUSHPAGE);
	buf->b_hdr = hdr;
	buf->b_data = NULL;
	buf->b_efunc = NULL;
	buf->b_private = NULL;
	buf->b_next = NULL;

	hdr->b_flags = arc_bufc_to_flags(type);
	hdr->b_flags |= ARC_FLAG_HAS_L1HDR;

	hdr->b_l1hdr.b_buf = buf;
	hdr->b_l1hdr.b_state = arc_anon;
	hdr->b_l1hdr.b_arc_access = 0;
	hdr->b_l1hdr.b_datacnt = 1;
	hdr->b_l1hdr.b_tmp_cdata = NULL;

	arc_get_data_buf(buf);
	ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
	(void) refcount_add(&hdr->b_l1hdr.b_refcnt, tag);

	return (buf);
}

static char *arc_onloan_tag = "onloan";

/*
 * Loan out an anonymous arc buffer. Loaned buffers are not counted as in
 * flight data by arc_tempreserve_space() until they are "returned". Loaned
 * buffers must be returned to the arc before they can be used by the DMU or
 * freed.
 */
arc_buf_t *
arc_loan_buf(spa_t *spa, uint64_t size)
{
	arc_buf_t *buf;

	buf = arc_buf_alloc(spa, size, arc_onloan_tag, ARC_BUFC_DATA);

	atomic_add_64(&arc_loaned_bytes, size);
	return (buf);
}

/*
 * Return a loaned arc buffer to the arc.
 */
void
arc_return_buf(arc_buf_t *buf, void *tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(buf->b_data != NULL);
	ASSERT(HDR_HAS_L1HDR(hdr));
	(void) refcount_add(&hdr->b_l1hdr.b_refcnt, tag);
	(void) refcount_remove(&hdr->b_l1hdr.b_refcnt, arc_onloan_tag);

	atomic_add_64(&arc_loaned_bytes, -hdr->b_size);
}

/* Detach an arc_buf from a dbuf (tag) */
void
arc_loan_inuse_buf(arc_buf_t *buf, void *tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(buf->b_data != NULL);
	ASSERT(HDR_HAS_L1HDR(hdr));
	(void) refcount_add(&hdr->b_l1hdr.b_refcnt, arc_onloan_tag);
	(void) refcount_remove(&hdr->b_l1hdr.b_refcnt, tag);
	buf->b_efunc = NULL;
	buf->b_private = NULL;

	atomic_add_64(&arc_loaned_bytes, hdr->b_size);
}

static arc_buf_t *
arc_buf_clone(arc_buf_t *from)
{
	arc_buf_t *buf;
	arc_buf_hdr_t *hdr = from->b_hdr;
	uint64_t size = hdr->b_size;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(hdr->b_l1hdr.b_state != arc_anon);

	buf = kmem_cache_alloc(buf_cache, KM_PUSHPAGE);
	buf->b_hdr = hdr;
	buf->b_data = NULL;
	buf->b_efunc = NULL;
	buf->b_private = NULL;
	buf->b_next = hdr->b_l1hdr.b_buf;
	hdr->b_l1hdr.b_buf = buf;
	arc_get_data_buf(buf);
	bcopy(from->b_data, buf->b_data, size);

	/*
	 * This buffer already exists in the arc so create a duplicate
	 * copy for the caller.  If the buffer is associated with user data
	 * then track the size and number of duplicates.  These stats will be
	 * updated as duplicate buffers are created and destroyed.
	 */
	if (HDR_ISTYPE_DATA(hdr)) {
		ARCSTAT_BUMP(arcstat_duplicate_buffers);
		ARCSTAT_INCR(arcstat_duplicate_buffers_size, size);
	}
	hdr->b_l1hdr.b_datacnt += 1;
	return (buf);
}

void
arc_buf_add_ref(arc_buf_t *buf, void* tag)
{
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;

	/*
	 * Check to see if this buffer is evicted.  Callers
	 * must verify b_data != NULL to know if the add_ref
	 * was successful.
	 */
	mutex_enter(&buf->b_evict_lock);
	if (buf->b_data == NULL) {
		mutex_exit(&buf->b_evict_lock);
		return;
	}
	hash_lock = HDR_LOCK(buf->b_hdr);
	mutex_enter(hash_lock);
	hdr = buf->b_hdr;
	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));
	mutex_exit(&buf->b_evict_lock);

	ASSERT(hdr->b_l1hdr.b_state == arc_mru ||
	    hdr->b_l1hdr.b_state == arc_mfu);

	add_reference(hdr, hash_lock, tag);
	DTRACE_PROBE1(arc__hit, arc_buf_hdr_t *, hdr);
	arc_access(hdr, hash_lock);
	mutex_exit(hash_lock);
	ARCSTAT_BUMP(arcstat_hits);
	ARCSTAT_CONDSTAT(!HDR_PREFETCH(hdr),
	    demand, prefetch, !HDR_ISTYPE_METADATA(hdr),
	    data, metadata, hits);
}

static void
arc_buf_free_on_write(void *data, size_t size,
    void (*free_func)(void *, size_t))
{
	l2arc_data_free_t *df;

	df = kmem_alloc(sizeof (*df), KM_SLEEP);
	df->l2df_data = data;
	df->l2df_size = size;
	df->l2df_func = free_func;
	mutex_enter(&l2arc_free_on_write_mtx);
	list_insert_head(l2arc_free_on_write, df);
	mutex_exit(&l2arc_free_on_write_mtx);
}

/*
 * Free the arc data buffer.  If it is an l2arc write in progress,
 * the buffer is placed on l2arc_free_on_write to be freed later.
 */
static void
arc_buf_data_free(arc_buf_t *buf, void (*free_func)(void *, size_t))
{
	arc_buf_hdr_t *hdr = buf->b_hdr;

	if (HDR_L2_WRITING(hdr)) {
		arc_buf_free_on_write(buf->b_data, hdr->b_size, free_func);
		ARCSTAT_BUMP(arcstat_l2_free_on_write);
	} else {
		free_func(buf->b_data, hdr->b_size);
	}
}

static void
arc_buf_l2_cdata_free(arc_buf_hdr_t *hdr)
{
	ASSERT(HDR_HAS_L2HDR(hdr));
	ASSERT(MUTEX_HELD(&hdr->b_l2hdr.b_dev->l2ad_mtx));

	/*
	 * The b_tmp_cdata field is linked off of the b_l1hdr, so if
	 * that doesn't exist, the header is in the arc_l2c_only state,
	 * and there isn't anything to free (it's already been freed).
	 */
	if (!HDR_HAS_L1HDR(hdr))
		return;

	/*
	 * The header isn't being written to the l2arc device, thus it
	 * shouldn't have a b_tmp_cdata to free.
	 */
	if (!HDR_L2_WRITING(hdr)) {
		ASSERT3P(hdr->b_l1hdr.b_tmp_cdata, ==, NULL);
		return;
	}

	/*
	 * The header does not have compression enabled. This can be due
	 * to the buffer not being compressible, or because we're
	 * freeing the buffer before the second phase of
	 * l2arc_write_buffer() has started (which does the compression
	 * step). In either case, b_tmp_cdata does not point to a
	 * separately compressed buffer, so there's nothing to free (it
	 * points to the same buffer as the arc_buf_t's b_data field).
	 */
	if (hdr->b_l2hdr.b_compress == ZIO_COMPRESS_OFF) {
		hdr->b_l1hdr.b_tmp_cdata = NULL;
		return;
	}

	/*
	 * There's nothing to free since the buffer was all zero's and
	 * compressed to a zero length buffer.
	 */
	if (hdr->b_l2hdr.b_compress == ZIO_COMPRESS_EMPTY) {
		ASSERT3P(hdr->b_l1hdr.b_tmp_cdata, ==, NULL);
		return;
	}

	ASSERT(L2ARC_IS_VALID_COMPRESS(hdr->b_l2hdr.b_compress));

	arc_buf_free_on_write(hdr->b_l1hdr.b_tmp_cdata,
	    hdr->b_size, zio_data_buf_free);

	ARCSTAT_BUMP(arcstat_l2_cdata_free_on_write);
	hdr->b_l1hdr.b_tmp_cdata = NULL;
}

/*
 * Free up buf->b_data and if 'remove' is set, then pull the
 * arc_buf_t off of the the arc_buf_hdr_t's list and free it.
 */
static void
arc_buf_destroy(arc_buf_t *buf, boolean_t remove)
{
	arc_buf_t **bufp;

	/* free up data associated with the buf */
	if (buf->b_data != NULL) {
		arc_state_t *state = buf->b_hdr->b_l1hdr.b_state;
		uint64_t size = buf->b_hdr->b_size;
		arc_buf_contents_t type = arc_buf_type(buf->b_hdr);

		arc_cksum_verify(buf);
		arc_buf_unwatch(buf);

		if (type == ARC_BUFC_METADATA) {
			arc_buf_data_free(buf, zio_buf_free);
			arc_space_return(size, ARC_SPACE_META);
		} else {
			ASSERT(type == ARC_BUFC_DATA);
			arc_buf_data_free(buf, zio_data_buf_free);
			arc_space_return(size, ARC_SPACE_DATA);
		}

		/* protected by hash lock, if in the hash table */
		if (multilist_link_active(&buf->b_hdr->b_l1hdr.b_arc_node)) {
			uint64_t *cnt = &state->arcs_lsize[type];

			ASSERT(refcount_is_zero(
			    &buf->b_hdr->b_l1hdr.b_refcnt));
			ASSERT(state != arc_anon && state != arc_l2c_only);

			ASSERT3U(*cnt, >=, size);
			atomic_add_64(cnt, -size);
		}

		(void) refcount_remove_many(&state->arcs_size, size, buf);
		buf->b_data = NULL;

		/*
		 * If we're destroying a duplicate buffer make sure
		 * that the appropriate statistics are updated.
		 */
		if (buf->b_hdr->b_l1hdr.b_datacnt > 1 &&
		    HDR_ISTYPE_DATA(buf->b_hdr)) {
			ARCSTAT_BUMPDOWN(arcstat_duplicate_buffers);
			ARCSTAT_INCR(arcstat_duplicate_buffers_size, -size);
		}
		ASSERT(buf->b_hdr->b_l1hdr.b_datacnt > 0);
		buf->b_hdr->b_l1hdr.b_datacnt -= 1;
	}

	/* only remove the buf if requested */
	if (!remove)
		return;

	/* remove the buf from the hdr list */
	for (bufp = &buf->b_hdr->b_l1hdr.b_buf; *bufp != buf;
	    bufp = &(*bufp)->b_next)
		continue;
	*bufp = buf->b_next;
	buf->b_next = NULL;

	ASSERT(buf->b_efunc == NULL);

	/* clean up the buf */
	buf->b_hdr = NULL;
	kmem_cache_free(buf_cache, buf);
}

static void
arc_hdr_l2hdr_destroy(arc_buf_hdr_t *hdr)
{
	l2arc_buf_hdr_t *l2hdr = &hdr->b_l2hdr;
	l2arc_dev_t *dev = l2hdr->b_dev;

	ASSERT(MUTEX_HELD(&dev->l2ad_mtx));
	ASSERT(HDR_HAS_L2HDR(hdr));

	list_remove(&dev->l2ad_buflist, hdr);

	/*
	 * We don't want to leak the b_tmp_cdata buffer that was
	 * allocated in l2arc_write_buffers()
	 */
	arc_buf_l2_cdata_free(hdr);

	/*
	 * If the l2hdr's b_daddr is equal to L2ARC_ADDR_UNSET, then
	 * this header is being processed by l2arc_write_buffers() (i.e.
	 * it's in the first stage of l2arc_write_buffers()).
	 * Re-affirming that truth here, just to serve as a reminder. If
	 * b_daddr does not equal L2ARC_ADDR_UNSET, then the header may or
	 * may not have its HDR_L2_WRITING flag set. (the write may have
	 * completed, in which case HDR_L2_WRITING will be false and the
	 * b_daddr field will point to the address of the buffer on disk).
	 */
	IMPLY(l2hdr->b_daddr == L2ARC_ADDR_UNSET, HDR_L2_WRITING(hdr));

	/*
	 * If b_daddr is equal to L2ARC_ADDR_UNSET, we're racing with
	 * l2arc_write_buffers(). Since we've just removed this header
	 * from the l2arc buffer list, this header will never reach the
	 * second stage of l2arc_write_buffers(), which increments the
	 * accounting stats for this header. Thus, we must be careful
	 * not to decrement them for this header either.
	 */
	if (l2hdr->b_daddr != L2ARC_ADDR_UNSET) {
		ARCSTAT_INCR(arcstat_l2_asize, -l2hdr->b_asize);
		ARCSTAT_INCR(arcstat_l2_size, -hdr->b_size);

		vdev_space_update(dev->l2ad_vdev,
		    -l2hdr->b_asize, 0, 0);

		(void) refcount_remove_many(&dev->l2ad_alloc,
		    l2hdr->b_asize, hdr);
	}

	hdr->b_flags &= ~ARC_FLAG_HAS_L2HDR;
}

static void
arc_hdr_destroy(arc_buf_hdr_t *hdr)
{
	if (HDR_HAS_L1HDR(hdr)) {
		ASSERT(hdr->b_l1hdr.b_buf == NULL ||
		    hdr->b_l1hdr.b_datacnt > 0);
		ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
		ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);
	}
	ASSERT(!HDR_IO_IN_PROGRESS(hdr));
	ASSERT(!HDR_IN_HASH_TABLE(hdr));

	if (HDR_HAS_L2HDR(hdr)) {
		l2arc_dev_t *dev = hdr->b_l2hdr.b_dev;
		boolean_t buflist_held = MUTEX_HELD(&dev->l2ad_mtx);

		if (!buflist_held)
			mutex_enter(&dev->l2ad_mtx);

		/*
		 * Even though we checked this conditional above, we
		 * need to check this again now that we have the
		 * l2ad_mtx. This is because we could be racing with
		 * another thread calling l2arc_evict() which might have
		 * destroyed this header's L2 portion as we were waiting
		 * to acquire the l2ad_mtx. If that happens, we don't
		 * want to re-destroy the header's L2 portion.
		 */
		if (HDR_HAS_L2HDR(hdr))
			arc_hdr_l2hdr_destroy(hdr);

		if (!buflist_held)
			mutex_exit(&dev->l2ad_mtx);
	}

	if (!BUF_EMPTY(hdr))
		buf_discard_identity(hdr);

	if (hdr->b_freeze_cksum != NULL) {
		kmem_free(hdr->b_freeze_cksum, sizeof (zio_cksum_t));
		hdr->b_freeze_cksum = NULL;
	}

	if (HDR_HAS_L1HDR(hdr)) {
		while (hdr->b_l1hdr.b_buf) {
			arc_buf_t *buf = hdr->b_l1hdr.b_buf;

			if (buf->b_efunc != NULL) {
				mutex_enter(&arc_user_evicts_lock);
				mutex_enter(&buf->b_evict_lock);
				ASSERT(buf->b_hdr != NULL);
				arc_buf_destroy(hdr->b_l1hdr.b_buf, FALSE);
				hdr->b_l1hdr.b_buf = buf->b_next;
				buf->b_hdr = &arc_eviction_hdr;
				buf->b_next = arc_eviction_list;
				arc_eviction_list = buf;
				mutex_exit(&buf->b_evict_lock);
				cv_signal(&arc_user_evicts_cv);
				mutex_exit(&arc_user_evicts_lock);
			} else {
				arc_buf_destroy(hdr->b_l1hdr.b_buf, TRUE);
			}
		}
	}

	ASSERT3P(hdr->b_hash_next, ==, NULL);
	if (HDR_HAS_L1HDR(hdr)) {
		ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
		ASSERT3P(hdr->b_l1hdr.b_acb, ==, NULL);
		kmem_cache_free(hdr_full_cache, hdr);
	} else {
		kmem_cache_free(hdr_l2only_cache, hdr);
	}
}

void
arc_buf_free(arc_buf_t *buf, void *tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;
	int hashed = hdr->b_l1hdr.b_state != arc_anon;

	ASSERT(buf->b_efunc == NULL);
	ASSERT(buf->b_data != NULL);

	if (hashed) {
		kmutex_t *hash_lock = HDR_LOCK(hdr);

		mutex_enter(hash_lock);
		hdr = buf->b_hdr;
		ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));

		(void) remove_reference(hdr, hash_lock, tag);
		if (hdr->b_l1hdr.b_datacnt > 1) {
			arc_buf_destroy(buf, TRUE);
		} else {
			ASSERT(buf == hdr->b_l1hdr.b_buf);
			ASSERT(buf->b_efunc == NULL);
			hdr->b_flags |= ARC_FLAG_BUF_AVAILABLE;
		}
		mutex_exit(hash_lock);
	} else if (HDR_IO_IN_PROGRESS(hdr)) {
		int destroy_hdr;
		/*
		 * We are in the middle of an async write.  Don't destroy
		 * this buffer unless the write completes before we finish
		 * decrementing the reference count.
		 */
		mutex_enter(&arc_user_evicts_lock);
		(void) remove_reference(hdr, NULL, tag);
		ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
		destroy_hdr = !HDR_IO_IN_PROGRESS(hdr);
		mutex_exit(&arc_user_evicts_lock);
		if (destroy_hdr)
			arc_hdr_destroy(hdr);
	} else {
		if (remove_reference(hdr, NULL, tag) > 0)
			arc_buf_destroy(buf, TRUE);
		else
			arc_hdr_destroy(hdr);
	}
}

boolean_t
arc_buf_remove_ref(arc_buf_t *buf, void* tag)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;
	kmutex_t *hash_lock = HDR_LOCK(hdr);
	boolean_t no_callback = (buf->b_efunc == NULL);

	if (hdr->b_l1hdr.b_state == arc_anon) {
		ASSERT(hdr->b_l1hdr.b_datacnt == 1);
		arc_buf_free(buf, tag);
		return (no_callback);
	}

	mutex_enter(hash_lock);
	hdr = buf->b_hdr;
	ASSERT(hdr->b_l1hdr.b_datacnt > 0);
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));
	ASSERT(hdr->b_l1hdr.b_state != arc_anon);
	ASSERT(buf->b_data != NULL);

	(void) remove_reference(hdr, hash_lock, tag);
	if (hdr->b_l1hdr.b_datacnt > 1) {
		if (no_callback)
			arc_buf_destroy(buf, TRUE);
	} else if (no_callback) {
		ASSERT(hdr->b_l1hdr.b_buf == buf && buf->b_next == NULL);
		ASSERT(buf->b_efunc == NULL);
		hdr->b_flags |= ARC_FLAG_BUF_AVAILABLE;
	}
	ASSERT(no_callback || hdr->b_l1hdr.b_datacnt > 1 ||
	    refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
	mutex_exit(hash_lock);
	return (no_callback);
}

uint64_t
arc_buf_size(arc_buf_t *buf)
{
	return (buf->b_hdr->b_size);
}

/*
 * Called from the DMU to determine if the current buffer should be
 * evicted. In order to ensure proper locking, the eviction must be initiated
 * from the DMU. Return true if the buffer is associated with user data and
 * duplicate buffers still exist.
 */
boolean_t
arc_buf_eviction_needed(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr;
	boolean_t evict_needed = B_FALSE;

	if (zfs_disable_dup_eviction)
		return (B_FALSE);

	mutex_enter(&buf->b_evict_lock);
	hdr = buf->b_hdr;
	if (hdr == NULL) {
		/*
		 * We are in arc_do_user_evicts(); let that function
		 * perform the eviction.
		 */
		ASSERT(buf->b_data == NULL);
		mutex_exit(&buf->b_evict_lock);
		return (B_FALSE);
	} else if (buf->b_data == NULL) {
		/*
		 * We have already been added to the arc eviction list;
		 * recommend eviction.
		 */
		ASSERT3P(hdr, ==, &arc_eviction_hdr);
		mutex_exit(&buf->b_evict_lock);
		return (B_TRUE);
	}

	if (hdr->b_l1hdr.b_datacnt > 1 && HDR_ISTYPE_DATA(hdr))
		evict_needed = B_TRUE;

	mutex_exit(&buf->b_evict_lock);
	return (evict_needed);
}

/*
 * Evict the arc_buf_hdr that is provided as a parameter. The resultant
 * state of the header is dependent on its state prior to entering this
 * function. The following transitions are possible:
 *
 *    - arc_mru -> arc_mru_ghost
 *    - arc_mfu -> arc_mfu_ghost
 *    - arc_mru_ghost -> arc_l2c_only
 *    - arc_mru_ghost -> deleted
 *    - arc_mfu_ghost -> arc_l2c_only
 *    - arc_mfu_ghost -> deleted
 */
static int64_t
arc_evict_hdr(arc_buf_hdr_t *hdr, kmutex_t *hash_lock)
{
	arc_state_t *evicted_state, *state;
	int64_t bytes_evicted = 0;

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT(HDR_HAS_L1HDR(hdr));

	state = hdr->b_l1hdr.b_state;
	if (GHOST_STATE(state)) {
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		ASSERT(hdr->b_l1hdr.b_buf == NULL);

		/*
		 * l2arc_write_buffers() relies on a header's L1 portion
		 * (i.e. its b_tmp_cdata field) during its write phase.
		 * Thus, we cannot push a header onto the arc_l2c_only
		 * state (removing its L1 piece) until the header is
		 * done being written to the l2arc.
		 */
		if (HDR_HAS_L2HDR(hdr) && HDR_L2_WRITING(hdr)) {
			ARCSTAT_BUMP(arcstat_evict_l2_skip);
			return (bytes_evicted);
		}

		ARCSTAT_BUMP(arcstat_deleted);
		bytes_evicted += hdr->b_size;

		DTRACE_PROBE1(arc__delete, arc_buf_hdr_t *, hdr);

		if (HDR_HAS_L2HDR(hdr)) {
			/*
			 * This buffer is cached on the 2nd Level ARC;
			 * don't destroy the header.
			 */
			arc_change_state(arc_l2c_only, hdr, hash_lock);
			/*
			 * dropping from L1+L2 cached to L2-only,
			 * realloc to remove the L1 header.
			 */
			hdr = arc_hdr_realloc(hdr, hdr_full_cache,
			    hdr_l2only_cache);
		} else {
			arc_change_state(arc_anon, hdr, hash_lock);
			arc_hdr_destroy(hdr);
		}
		return (bytes_evicted);
	}

	ASSERT(state == arc_mru || state == arc_mfu);
	evicted_state = (state == arc_mru) ? arc_mru_ghost : arc_mfu_ghost;

	/* prefetch buffers have a minimum lifespan */
	if (HDR_IO_IN_PROGRESS(hdr) ||
	    ((hdr->b_flags & (ARC_FLAG_PREFETCH | ARC_FLAG_INDIRECT)) &&
	    ddi_get_lbolt() - hdr->b_l1hdr.b_arc_access <
	    arc_min_prefetch_lifespan)) {
		ARCSTAT_BUMP(arcstat_evict_skip);
		return (bytes_evicted);
	}

	ASSERT0(refcount_count(&hdr->b_l1hdr.b_refcnt));
	ASSERT3U(hdr->b_l1hdr.b_datacnt, >, 0);
	while (hdr->b_l1hdr.b_buf) {
		arc_buf_t *buf = hdr->b_l1hdr.b_buf;
		if (!mutex_tryenter(&buf->b_evict_lock)) {
			ARCSTAT_BUMP(arcstat_mutex_miss);
			break;
		}
		if (buf->b_data != NULL)
			bytes_evicted += hdr->b_size;
		if (buf->b_efunc != NULL) {
			mutex_enter(&arc_user_evicts_lock);
			arc_buf_destroy(buf, FALSE);
			hdr->b_l1hdr.b_buf = buf->b_next;
			buf->b_hdr = &arc_eviction_hdr;
			buf->b_next = arc_eviction_list;
			arc_eviction_list = buf;
			cv_signal(&arc_user_evicts_cv);
			mutex_exit(&arc_user_evicts_lock);
			mutex_exit(&buf->b_evict_lock);
		} else {
			mutex_exit(&buf->b_evict_lock);
			arc_buf_destroy(buf, TRUE);
		}
	}

	if (HDR_HAS_L2HDR(hdr)) {
		ARCSTAT_INCR(arcstat_evict_l2_cached, hdr->b_size);
	} else {
		if (l2arc_write_eligible(hdr->b_spa, hdr))
			ARCSTAT_INCR(arcstat_evict_l2_eligible, hdr->b_size);
		else
			ARCSTAT_INCR(arcstat_evict_l2_ineligible, hdr->b_size);
	}

	if (hdr->b_l1hdr.b_datacnt == 0) {
		arc_change_state(evicted_state, hdr, hash_lock);
		ASSERT(HDR_IN_HASH_TABLE(hdr));
		hdr->b_flags |= ARC_FLAG_IN_HASH_TABLE;
		hdr->b_flags &= ~ARC_FLAG_BUF_AVAILABLE;
		DTRACE_PROBE1(arc__evict, arc_buf_hdr_t *, hdr);
	}

	return (bytes_evicted);
}

static uint64_t
arc_evict_state_impl(multilist_t *ml, int idx, arc_buf_hdr_t *marker,
    uint64_t spa, int64_t bytes)
{
	multilist_sublist_t *mls;
	uint64_t bytes_evicted = 0;
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;
	int evict_count = 0;

	ASSERT3P(marker, !=, NULL);
	IMPLY(bytes < 0, bytes == ARC_EVICT_ALL);

	mls = multilist_sublist_lock(ml, idx);

	for (hdr = multilist_sublist_prev(mls, marker); hdr != NULL;
	    hdr = multilist_sublist_prev(mls, marker)) {
		if ((bytes != ARC_EVICT_ALL && bytes_evicted >= bytes) ||
		    (evict_count >= zfs_arc_evict_batch_limit))
			break;

		/*
		 * To keep our iteration location, move the marker
		 * forward. Since we're not holding hdr's hash lock, we
		 * must be very careful and not remove 'hdr' from the
		 * sublist. Otherwise, other consumers might mistake the
		 * 'hdr' as not being on a sublist when they call the
		 * multilist_link_active() function (they all rely on
		 * the hash lock protecting concurrent insertions and
		 * removals). multilist_sublist_move_forward() was
		 * specifically implemented to ensure this is the case
		 * (only 'marker' will be removed and re-inserted).
		 */
		multilist_sublist_move_forward(mls, marker);

		/*
		 * The only case where the b_spa field should ever be
		 * zero, is the marker headers inserted by
		 * arc_evict_state(). It's possible for multiple threads
		 * to be calling arc_evict_state() concurrently (e.g.
		 * dsl_pool_close() and zio_inject_fault()), so we must
		 * skip any markers we see from these other threads.
		 */
		if (hdr->b_spa == 0)
			continue;

		/* we're only interested in evicting buffers of a certain spa */
		if (spa != 0 && hdr->b_spa != spa) {
			ARCSTAT_BUMP(arcstat_evict_skip);
			continue;
		}

		hash_lock = HDR_LOCK(hdr);

		/*
		 * We aren't calling this function from any code path
		 * that would already be holding a hash lock, so we're
		 * asserting on this assumption to be defensive in case
		 * this ever changes. Without this check, it would be
		 * possible to incorrectly increment arcstat_mutex_miss
		 * below (e.g. if the code changed such that we called
		 * this function with a hash lock held).
		 */
		ASSERT(!MUTEX_HELD(hash_lock));

		if (mutex_tryenter(hash_lock)) {
			uint64_t evicted = arc_evict_hdr(hdr, hash_lock);
			mutex_exit(hash_lock);

			bytes_evicted += evicted;

			/*
			 * If evicted is zero, arc_evict_hdr() must have
			 * decided to skip this header, don't increment
			 * evict_count in this case.
			 */
			if (evicted != 0)
				evict_count++;

			/*
			 * If arc_size isn't overflowing, signal any
			 * threads that might happen to be waiting.
			 *
			 * For each header evicted, we wake up a single
			 * thread. If we used cv_broadcast, we could
			 * wake up "too many" threads causing arc_size
			 * to significantly overflow arc_c; since
			 * arc_get_data_buf() doesn't check for overflow
			 * when it's woken up (it doesn't because it's
			 * possible for the ARC to be overflowing while
			 * full of un-evictable buffers, and the
			 * function should proceed in this case).
			 *
			 * If threads are left sleeping, due to not
			 * using cv_broadcast, they will be woken up
			 * just before arc_reclaim_thread() sleeps.
			 */
			mutex_enter(&arc_reclaim_lock);
			if (!arc_is_overflowing())
				cv_signal(&arc_reclaim_waiters_cv);
			mutex_exit(&arc_reclaim_lock);
		} else {
			ARCSTAT_BUMP(arcstat_mutex_miss);
		}
	}

	multilist_sublist_unlock(mls);

	return (bytes_evicted);
}

/*
 * Evict buffers from the given arc state, until we've removed the
 * specified number of bytes. Move the removed buffers to the
 * appropriate evict state.
 *
 * This function makes a "best effort". It skips over any buffers
 * it can't get a hash_lock on, and so, may not catch all candidates.
 * It may also return without evicting as much space as requested.
 *
 * If bytes is specified using the special value ARC_EVICT_ALL, this
 * will evict all available (i.e. unlocked and evictable) buffers from
 * the given arc state; which is used by arc_flush().
 */
static uint64_t
arc_evict_state(arc_state_t *state, uint64_t spa, int64_t bytes,
    arc_buf_contents_t type)
{
	uint64_t total_evicted = 0;
	multilist_t *ml = &state->arcs_list[type];
	int num_sublists;
	arc_buf_hdr_t **markers;
	int i;

	IMPLY(bytes < 0, bytes == ARC_EVICT_ALL);

	num_sublists = multilist_get_num_sublists(ml);

	/*
	 * If we've tried to evict from each sublist, made some
	 * progress, but still have not hit the target number of bytes
	 * to evict, we want to keep trying. The markers allow us to
	 * pick up where we left off for each individual sublist, rather
	 * than starting from the tail each time.
	 */
	markers = kmem_zalloc(sizeof (*markers) * num_sublists, KM_SLEEP);
	for (i = 0; i < num_sublists; i++) {
		multilist_sublist_t *mls;

		markers[i] = kmem_cache_alloc(hdr_full_cache, KM_SLEEP);

		/*
		 * A b_spa of 0 is used to indicate that this header is
		 * a marker. This fact is used in arc_adjust_type() and
		 * arc_evict_state_impl().
		 */
		markers[i]->b_spa = 0;

		mls = multilist_sublist_lock(ml, i);
		multilist_sublist_insert_tail(mls, markers[i]);
		multilist_sublist_unlock(mls);
	}

	/*
	 * While we haven't hit our target number of bytes to evict, or
	 * we're evicting all available buffers.
	 */
	while (total_evicted < bytes || bytes == ARC_EVICT_ALL) {
		/*
		 * Start eviction using a randomly selected sublist,
		 * this is to try and evenly balance eviction across all
		 * sublists. Always starting at the same sublist
		 * (e.g. index 0) would cause evictions to favor certain
		 * sublists over others.
		 */
		int sublist_idx = multilist_get_random_index(ml);
		uint64_t scan_evicted = 0;

		for (i = 0; i < num_sublists; i++) {
			uint64_t bytes_remaining;
			uint64_t bytes_evicted;

			if (bytes == ARC_EVICT_ALL)
				bytes_remaining = ARC_EVICT_ALL;
			else if (total_evicted < bytes)
				bytes_remaining = bytes - total_evicted;
			else
				break;

			bytes_evicted = arc_evict_state_impl(ml, sublist_idx,
			    markers[sublist_idx], spa, bytes_remaining);

			scan_evicted += bytes_evicted;
			total_evicted += bytes_evicted;

			/* we've reached the end, wrap to the beginning */
			if (++sublist_idx >= num_sublists)
				sublist_idx = 0;
		}

		/*
		 * If we didn't evict anything during this scan, we have
		 * no reason to believe we'll evict more during another
		 * scan, so break the loop.
		 */
		if (scan_evicted == 0) {
			/* This isn't possible, let's make that obvious */
			ASSERT3S(bytes, !=, 0);

			/*
			 * When bytes is ARC_EVICT_ALL, the only way to
			 * break the loop is when scan_evicted is zero.
			 * In that case, we actually have evicted enough,
			 * so we don't want to increment the kstat.
			 */
			if (bytes != ARC_EVICT_ALL) {
				ASSERT3S(total_evicted, <, bytes);
				ARCSTAT_BUMP(arcstat_evict_not_enough);
			}

			break;
		}
	}

	for (i = 0; i < num_sublists; i++) {
		multilist_sublist_t *mls = multilist_sublist_lock(ml, i);
		multilist_sublist_remove(mls, markers[i]);
		multilist_sublist_unlock(mls);

		kmem_cache_free(hdr_full_cache, markers[i]);
	}
	kmem_free(markers, sizeof (*markers) * num_sublists);

	return (total_evicted);
}

/*
 * Flush all "evictable" data of the given type from the arc state
 * specified. This will not evict any "active" buffers (i.e. referenced).
 *
 * When 'retry' is set to FALSE, the function will make a single pass
 * over the state and evict any buffers that it can. Since it doesn't
 * continually retry the eviction, it might end up leaving some buffers
 * in the ARC due to lock misses.
 *
 * When 'retry' is set to TRUE, the function will continually retry the
 * eviction until *all* evictable buffers have been removed from the
 * state. As a result, if concurrent insertions into the state are
 * allowed (e.g. if the ARC isn't shutting down), this function might
 * wind up in an infinite loop, continually trying to evict buffers.
 */
static uint64_t
arc_flush_state(arc_state_t *state, uint64_t spa, arc_buf_contents_t type,
    boolean_t retry)
{
	uint64_t evicted = 0;

	while (state->arcs_lsize[type] != 0) {
		evicted += arc_evict_state(state, spa, ARC_EVICT_ALL, type);

		if (!retry)
			break;
	}

	return (evicted);
}

/*
 * Helper function for arc_prune_async() it is responsible for safely
 * handling the execution of a registered arc_prune_func_t.
 */
static void
arc_prune_task(void *ptr)
{
	arc_prune_t *ap = (arc_prune_t *)ptr;
	arc_prune_func_t *func = ap->p_pfunc;

	if (func != NULL)
		func(ap->p_adjust, ap->p_private);

	refcount_remove(&ap->p_refcnt, func);
}

/*
 * Notify registered consumers they must drop holds on a portion of the ARC
 * buffered they reference.  This provides a mechanism to ensure the ARC can
 * honor the arc_meta_limit and reclaim otherwise pinned ARC buffers.  This
 * is analogous to dnlc_reduce_cache() but more generic.
 *
 * This operation is performed asynchronously so it may be safely called
 * in the context of the arc_reclaim_thread().  A reference is taken here
 * for each registered arc_prune_t and the arc_prune_task() is responsible
 * for releasing it once the registered arc_prune_func_t has completed.
 */
static void
arc_prune_async(int64_t adjust)
{
	arc_prune_t *ap;

	mutex_enter(&arc_prune_mtx);
	for (ap = list_head(&arc_prune_list); ap != NULL;
	    ap = list_next(&arc_prune_list, ap)) {

		if (refcount_count(&ap->p_refcnt) >= 2)
			continue;

		refcount_add(&ap->p_refcnt, ap->p_pfunc);
		ap->p_adjust = adjust;
		taskq_dispatch(arc_prune_taskq, arc_prune_task, ap, TQ_SLEEP);
		ARCSTAT_BUMP(arcstat_prune);
	}
	mutex_exit(&arc_prune_mtx);
}

/*
 * Evict the specified number of bytes from the state specified,
 * restricting eviction to the spa and type given. This function
 * prevents us from trying to evict more from a state's list than
 * is "evictable", and to skip evicting altogether when passed a
 * negative value for "bytes". In contrast, arc_evict_state() will
 * evict everything it can, when passed a negative value for "bytes".
 */
static uint64_t
arc_adjust_impl(arc_state_t *state, uint64_t spa, int64_t bytes,
    arc_buf_contents_t type)
{
	int64_t delta;

	if (bytes > 0 && state->arcs_lsize[type] > 0) {
		delta = MIN(state->arcs_lsize[type], bytes);
		return (arc_evict_state(state, spa, delta, type));
	}

	return (0);
}

/*
 * The goal of this function is to evict enough meta data buffers from the
 * ARC in order to enforce the arc_meta_limit.  Achieving this is slightly
 * more complicated than it appears because it is common for data buffers
 * to have holds on meta data buffers.  In addition, dnode meta data buffers
 * will be held by the dnodes in the block preventing them from being freed.
 * This means we can't simply traverse the ARC and expect to always find
 * enough unheld meta data buffer to release.
 *
 * Therefore, this function has been updated to make alternating passes
 * over the ARC releasing data buffers and then newly unheld meta data
 * buffers.  This ensures forward progress is maintained and arc_meta_used
 * will decrease.  Normally this is sufficient, but if required the ARC
 * will call the registered prune callbacks causing dentry and inodes to
 * be dropped from the VFS cache.  This will make dnode meta data buffers
 * available for reclaim.
 */
static uint64_t
arc_adjust_meta_balanced(void)
{
	int64_t adjustmnt, delta, prune = 0;
	uint64_t total_evicted = 0;
	arc_buf_contents_t type = ARC_BUFC_DATA;
	int restarts = MAX(zfs_arc_meta_adjust_restarts, 0);

restart:
	/*
	 * This slightly differs than the way we evict from the mru in
	 * arc_adjust because we don't have a "target" value (i.e. no
	 * "meta" arc_p). As a result, I think we can completely
	 * cannibalize the metadata in the MRU before we evict the
	 * metadata from the MFU. I think we probably need to implement a
	 * "metadata arc_p" value to do this properly.
	 */
	adjustmnt = arc_meta_used - arc_meta_limit;

	if (adjustmnt > 0 && arc_mru->arcs_lsize[type] > 0) {
		delta = MIN(arc_mru->arcs_lsize[type], adjustmnt);
		total_evicted += arc_adjust_impl(arc_mru, 0, delta, type);
		adjustmnt -= delta;
	}

	/*
	 * We can't afford to recalculate adjustmnt here. If we do,
	 * new metadata buffers can sneak into the MRU or ANON lists,
	 * thus penalize the MFU metadata. Although the fudge factor is
	 * small, it has been empirically shown to be significant for
	 * certain workloads (e.g. creating many empty directories). As
	 * such, we use the original calculation for adjustmnt, and
	 * simply decrement the amount of data evicted from the MRU.
	 */

	if (adjustmnt > 0 && arc_mfu->arcs_lsize[type] > 0) {
		delta = MIN(arc_mfu->arcs_lsize[type], adjustmnt);
		total_evicted += arc_adjust_impl(arc_mfu, 0, delta, type);
	}

	adjustmnt = arc_meta_used - arc_meta_limit;

	if (adjustmnt > 0 && arc_mru_ghost->arcs_lsize[type] > 0) {
		delta = MIN(adjustmnt,
		    arc_mru_ghost->arcs_lsize[type]);
		total_evicted += arc_adjust_impl(arc_mru_ghost, 0, delta, type);
		adjustmnt -= delta;
	}

	if (adjustmnt > 0 && arc_mfu_ghost->arcs_lsize[type] > 0) {
		delta = MIN(adjustmnt,
		    arc_mfu_ghost->arcs_lsize[type]);
		total_evicted += arc_adjust_impl(arc_mfu_ghost, 0, delta, type);
	}

	/*
	 * If after attempting to make the requested adjustment to the ARC
	 * the meta limit is still being exceeded then request that the
	 * higher layers drop some cached objects which have holds on ARC
	 * meta buffers.  Requests to the upper layers will be made with
	 * increasingly large scan sizes until the ARC is below the limit.
	 */
	if (arc_meta_used > arc_meta_limit) {
		if (type == ARC_BUFC_DATA) {
			type = ARC_BUFC_METADATA;
		} else {
			type = ARC_BUFC_DATA;

			if (zfs_arc_meta_prune) {
				prune += zfs_arc_meta_prune;
				arc_prune_async(prune);
			}
		}

		if (restarts > 0) {
			restarts--;
			goto restart;
		}
	}
	return (total_evicted);
}

/*
 * Evict metadata buffers from the cache, such that arc_meta_used is
 * capped by the arc_meta_limit tunable.
 */
static uint64_t
arc_adjust_meta_only(void)
{
	uint64_t total_evicted = 0;
	int64_t target;

	/*
	 * If we're over the meta limit, we want to evict enough
	 * metadata to get back under the meta limit. We don't want to
	 * evict so much that we drop the MRU below arc_p, though. If
	 * we're over the meta limit more than we're over arc_p, we
	 * evict some from the MRU here, and some from the MFU below.
	 */
	target = MIN((int64_t)(arc_meta_used - arc_meta_limit),
	    (int64_t)(refcount_count(&arc_anon->arcs_size) +
	    refcount_count(&arc_mru->arcs_size) - arc_p));

	total_evicted += arc_adjust_impl(arc_mru, 0, target, ARC_BUFC_METADATA);

	/*
	 * Similar to the above, we want to evict enough bytes to get us
	 * below the meta limit, but not so much as to drop us below the
	 * space alloted to the MFU (which is defined as arc_c - arc_p).
	 */
	target = MIN((int64_t)(arc_meta_used - arc_meta_limit),
	    (int64_t)(refcount_count(&arc_mfu->arcs_size) - (arc_c - arc_p)));

	total_evicted += arc_adjust_impl(arc_mfu, 0, target, ARC_BUFC_METADATA);

	return (total_evicted);
}

static uint64_t
arc_adjust_meta(void)
{
	if (zfs_arc_meta_strategy == ARC_STRATEGY_META_ONLY)
		return (arc_adjust_meta_only());
	else
		return (arc_adjust_meta_balanced());
}

/*
 * Return the type of the oldest buffer in the given arc state
 *
 * This function will select a random sublist of type ARC_BUFC_DATA and
 * a random sublist of type ARC_BUFC_METADATA. The tail of each sublist
 * is compared, and the type which contains the "older" buffer will be
 * returned.
 */
static arc_buf_contents_t
arc_adjust_type(arc_state_t *state)
{
	multilist_t *data_ml = &state->arcs_list[ARC_BUFC_DATA];
	multilist_t *meta_ml = &state->arcs_list[ARC_BUFC_METADATA];
	int data_idx = multilist_get_random_index(data_ml);
	int meta_idx = multilist_get_random_index(meta_ml);
	multilist_sublist_t *data_mls;
	multilist_sublist_t *meta_mls;
	arc_buf_contents_t type;
	arc_buf_hdr_t *data_hdr;
	arc_buf_hdr_t *meta_hdr;

	/*
	 * We keep the sublist lock until we're finished, to prevent
	 * the headers from being destroyed via arc_evict_state().
	 */
	data_mls = multilist_sublist_lock(data_ml, data_idx);
	meta_mls = multilist_sublist_lock(meta_ml, meta_idx);

	/*
	 * These two loops are to ensure we skip any markers that
	 * might be at the tail of the lists due to arc_evict_state().
	 */

	for (data_hdr = multilist_sublist_tail(data_mls); data_hdr != NULL;
	    data_hdr = multilist_sublist_prev(data_mls, data_hdr)) {
		if (data_hdr->b_spa != 0)
			break;
	}

	for (meta_hdr = multilist_sublist_tail(meta_mls); meta_hdr != NULL;
	    meta_hdr = multilist_sublist_prev(meta_mls, meta_hdr)) {
		if (meta_hdr->b_spa != 0)
			break;
	}

	if (data_hdr == NULL && meta_hdr == NULL) {
		type = ARC_BUFC_DATA;
	} else if (data_hdr == NULL) {
		ASSERT3P(meta_hdr, !=, NULL);
		type = ARC_BUFC_METADATA;
	} else if (meta_hdr == NULL) {
		ASSERT3P(data_hdr, !=, NULL);
		type = ARC_BUFC_DATA;
	} else {
		ASSERT3P(data_hdr, !=, NULL);
		ASSERT3P(meta_hdr, !=, NULL);

		/* The headers can't be on the sublist without an L1 header */
		ASSERT(HDR_HAS_L1HDR(data_hdr));
		ASSERT(HDR_HAS_L1HDR(meta_hdr));

		if (data_hdr->b_l1hdr.b_arc_access <
		    meta_hdr->b_l1hdr.b_arc_access) {
			type = ARC_BUFC_DATA;
		} else {
			type = ARC_BUFC_METADATA;
		}
	}

	multilist_sublist_unlock(meta_mls);
	multilist_sublist_unlock(data_mls);

	return (type);
}

/*
 * Evict buffers from the cache, such that arc_size is capped by arc_c.
 */
static uint64_t
arc_adjust(void)
{
	uint64_t total_evicted = 0;
	uint64_t bytes;
	int64_t target;

	/*
	 * If we're over arc_meta_limit, we want to correct that before
	 * potentially evicting data buffers below.
	 */
	total_evicted += arc_adjust_meta();

	/*
	 * Adjust MRU size
	 *
	 * If we're over the target cache size, we want to evict enough
	 * from the list to get back to our target size. We don't want
	 * to evict too much from the MRU, such that it drops below
	 * arc_p. So, if we're over our target cache size more than
	 * the MRU is over arc_p, we'll evict enough to get back to
	 * arc_p here, and then evict more from the MFU below.
	 */
	target = MIN((int64_t)(arc_size - arc_c),
	    (int64_t)(refcount_count(&arc_anon->arcs_size) +
	    refcount_count(&arc_mru->arcs_size) + arc_meta_used - arc_p));

	/*
	 * If we're below arc_meta_min, always prefer to evict data.
	 * Otherwise, try to satisfy the requested number of bytes to
	 * evict from the type which contains older buffers; in an
	 * effort to keep newer buffers in the cache regardless of their
	 * type. If we cannot satisfy the number of bytes from this
	 * type, spill over into the next type.
	 */
	if (arc_adjust_type(arc_mru) == ARC_BUFC_METADATA &&
	    arc_meta_used > arc_meta_min) {
		bytes = arc_adjust_impl(arc_mru, 0, target, ARC_BUFC_METADATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * metadata, we try to get the rest from data.
		 */
		target -= bytes;

		total_evicted +=
		    arc_adjust_impl(arc_mru, 0, target, ARC_BUFC_DATA);
	} else {
		bytes = arc_adjust_impl(arc_mru, 0, target, ARC_BUFC_DATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * data, we try to get the rest from metadata.
		 */
		target -= bytes;

		total_evicted +=
		    arc_adjust_impl(arc_mru, 0, target, ARC_BUFC_METADATA);
	}

	/*
	 * Adjust MFU size
	 *
	 * Now that we've tried to evict enough from the MRU to get its
	 * size back to arc_p, if we're still above the target cache
	 * size, we evict the rest from the MFU.
	 */
	target = arc_size - arc_c;

	if (arc_adjust_type(arc_mfu) == ARC_BUFC_METADATA &&
	    arc_meta_used > arc_meta_min) {
		bytes = arc_adjust_impl(arc_mfu, 0, target, ARC_BUFC_METADATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * metadata, we try to get the rest from data.
		 */
		target -= bytes;

		total_evicted +=
		    arc_adjust_impl(arc_mfu, 0, target, ARC_BUFC_DATA);
	} else {
		bytes = arc_adjust_impl(arc_mfu, 0, target, ARC_BUFC_DATA);
		total_evicted += bytes;

		/*
		 * If we couldn't evict our target number of bytes from
		 * data, we try to get the rest from data.
		 */
		target -= bytes;

		total_evicted +=
		    arc_adjust_impl(arc_mfu, 0, target, ARC_BUFC_METADATA);
	}

	/*
	 * Adjust ghost lists
	 *
	 * In addition to the above, the ARC also defines target values
	 * for the ghost lists. The sum of the mru list and mru ghost
	 * list should never exceed the target size of the cache, and
	 * the sum of the mru list, mfu list, mru ghost list, and mfu
	 * ghost list should never exceed twice the target size of the
	 * cache. The following logic enforces these limits on the ghost
	 * caches, and evicts from them as needed.
	 */
	target = refcount_count(&arc_mru->arcs_size) +
	    refcount_count(&arc_mru_ghost->arcs_size) - arc_c;

	bytes = arc_adjust_impl(arc_mru_ghost, 0, target, ARC_BUFC_DATA);
	total_evicted += bytes;

	target -= bytes;

	total_evicted +=
	    arc_adjust_impl(arc_mru_ghost, 0, target, ARC_BUFC_METADATA);

	/*
	 * We assume the sum of the mru list and mfu list is less than
	 * or equal to arc_c (we enforced this above), which means we
	 * can use the simpler of the two equations below:
	 *
	 *	mru + mfu + mru ghost + mfu ghost <= 2 * arc_c
	 *		    mru ghost + mfu ghost <= arc_c
	 */
	target = refcount_count(&arc_mru_ghost->arcs_size) +
	    refcount_count(&arc_mfu_ghost->arcs_size) - arc_c;

	bytes = arc_adjust_impl(arc_mfu_ghost, 0, target, ARC_BUFC_DATA);
	total_evicted += bytes;

	target -= bytes;

	total_evicted +=
	    arc_adjust_impl(arc_mfu_ghost, 0, target, ARC_BUFC_METADATA);

	return (total_evicted);
}

static void
arc_do_user_evicts(void)
{
	mutex_enter(&arc_user_evicts_lock);
	while (arc_eviction_list != NULL) {
		arc_buf_t *buf = arc_eviction_list;
		arc_eviction_list = buf->b_next;
		mutex_enter(&buf->b_evict_lock);
		buf->b_hdr = NULL;
		mutex_exit(&buf->b_evict_lock);
		mutex_exit(&arc_user_evicts_lock);

		if (buf->b_efunc != NULL)
			VERIFY0(buf->b_efunc(buf->b_private));

		buf->b_efunc = NULL;
		buf->b_private = NULL;
		kmem_cache_free(buf_cache, buf);
		mutex_enter(&arc_user_evicts_lock);
	}
	mutex_exit(&arc_user_evicts_lock);
}

void
arc_flush(spa_t *spa, boolean_t retry)
{
	uint64_t guid = 0;

	/*
	 * If retry is TRUE, a spa must not be specified since we have
	 * no good way to determine if all of a spa's buffers have been
	 * evicted from an arc state.
	 */
	ASSERT(!retry || spa == 0);

	if (spa != NULL)
		guid = spa_load_guid(spa);

	(void) arc_flush_state(arc_mru, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mru, guid, ARC_BUFC_METADATA, retry);

	(void) arc_flush_state(arc_mfu, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mfu, guid, ARC_BUFC_METADATA, retry);

	(void) arc_flush_state(arc_mru_ghost, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mru_ghost, guid, ARC_BUFC_METADATA, retry);

	(void) arc_flush_state(arc_mfu_ghost, guid, ARC_BUFC_DATA, retry);
	(void) arc_flush_state(arc_mfu_ghost, guid, ARC_BUFC_METADATA, retry);

	arc_do_user_evicts();
	ASSERT(spa || arc_eviction_list == NULL);
}

void
arc_shrink(int64_t to_free)
{
	uint64_t c = arc_c;

	if (c > to_free && c - to_free > arc_c_min) {
		arc_c = c - to_free;
		atomic_add_64(&arc_p, -(arc_p >> arc_shrink_shift));
		if (arc_c > arc_size)
			arc_c = MAX(arc_size, arc_c_min);
		if (arc_p > arc_c)
			arc_p = (arc_c >> 1);
		ASSERT(arc_c >= arc_c_min);
		ASSERT((int64_t)arc_p >= 0);
	} else {
		arc_c = arc_c_min;
	}

	if (arc_size > arc_c)
		(void) arc_adjust();
}

typedef enum free_memory_reason_t {
	FMR_UNKNOWN,
	FMR_NEEDFREE,
	FMR_LOTSFREE,
	FMR_SWAPFS_MINFREE,
	FMR_PAGES_PP_MAXIMUM,
	FMR_HEAP_ARENA,
	FMR_ZIO_ARENA,
} free_memory_reason_t;

int64_t last_free_memory;
free_memory_reason_t last_free_reason;

#ifdef _KERNEL
/*
 * Additional reserve of pages for pp_reserve.
 */
int64_t arc_pages_pp_reserve = 64;

/*
 * Additional reserve of pages for swapfs.
 */
int64_t arc_swapfs_reserve = 64;
#endif /* _KERNEL */

/*
 * Return the amount of memory that can be consumed before reclaim will be
 * needed.  Positive if there is sufficient free memory, negative indicates
 * the amount of memory that needs to be freed up.
 */
static int64_t
arc_available_memory(void)
{
	int64_t lowest = INT64_MAX;
	free_memory_reason_t r = FMR_UNKNOWN;
#ifdef _KERNEL
	int64_t n;
#ifdef __linux__
	pgcnt_t needfree = btop(arc_need_free);
	pgcnt_t lotsfree = btop(arc_sys_free);
	pgcnt_t desfree = 0;
#endif

	if (needfree > 0) {
		n = PAGESIZE * (-needfree);
		if (n < lowest) {
			lowest = n;
			r = FMR_NEEDFREE;
		}
	}

	/*
	 * check that we're out of range of the pageout scanner.  It starts to
	 * schedule paging if freemem is less than lotsfree and needfree.
	 * lotsfree is the high-water mark for pageout, and needfree is the
	 * number of needed free pages.  We add extra pages here to make sure
	 * the scanner doesn't start up while we're freeing memory.
	 */
	n = PAGESIZE * (freemem - lotsfree - needfree - desfree);
	if (n < lowest) {
		lowest = n;
		r = FMR_LOTSFREE;
	}

#ifndef __linux__
	/*
	 * check to make sure that swapfs has enough space so that anon
	 * reservations can still succeed. anon_resvmem() checks that the
	 * availrmem is greater than swapfs_minfree, and the number of reserved
	 * swap pages.  We also add a bit of extra here just to prevent
	 * circumstances from getting really dire.
	 */
	n = PAGESIZE * (availrmem - swapfs_minfree - swapfs_reserve -
	    desfree - arc_swapfs_reserve);
	if (n < lowest) {
		lowest = n;
		r = FMR_SWAPFS_MINFREE;
	}


	/*
	 * Check that we have enough availrmem that memory locking (e.g., via
	 * mlock(3C) or memcntl(2)) can still succeed.  (pages_pp_maximum
	 * stores the number of pages that cannot be locked; when availrmem
	 * drops below pages_pp_maximum, page locking mechanisms such as
	 * page_pp_lock() will fail.)
	 */
	n = PAGESIZE * (availrmem - pages_pp_maximum -
	    arc_pages_pp_reserve);
	if (n < lowest) {
		lowest = n;
		r = FMR_PAGES_PP_MAXIMUM;
	}
#endif

#if defined(__i386)
	/*
	 * If we're on an i386 platform, it's possible that we'll exhaust the
	 * kernel heap space before we ever run out of available physical
	 * memory.  Most checks of the size of the heap_area compare against
	 * tune.t_minarmem, which is the minimum available real memory that we
	 * can have in the system.  However, this is generally fixed at 25 pages
	 * which is so low that it's useless.  In this comparison, we seek to
	 * calculate the total heap-size, and reclaim if more than 3/4ths of the
	 * heap is allocated.  (Or, in the calculation, if less than 1/4th is
	 * free)
	 */
	n = vmem_size(heap_arena, VMEM_FREE) -
	    (vmem_size(heap_arena, VMEM_FREE | VMEM_ALLOC) >> 2);
	if (n < lowest) {
		lowest = n;
		r = FMR_HEAP_ARENA;
	}
#endif

	/*
	 * If zio data pages are being allocated out of a separate heap segment,
	 * then enforce that the size of available vmem for this arena remains
	 * above about 1/16th free.
	 *
	 * Note: The 1/16th arena free requirement was put in place
	 * to aggressively evict memory from the arc in order to avoid
	 * memory fragmentation issues.
	 */
	if (zio_arena != NULL) {
		n = vmem_size(zio_arena, VMEM_FREE) -
		    (vmem_size(zio_arena, VMEM_ALLOC) >> 4);
		if (n < lowest) {
			lowest = n;
			r = FMR_ZIO_ARENA;
		}
	}
#else /* _KERNEL */
	/* Every 100 calls, free a small amount */
	if (spa_get_random(100) == 0)
		lowest = -1024;
#endif /* _KERNEL */

	last_free_memory = lowest;
	last_free_reason = r;

	return (lowest);
}

/*
 * Determine if the system is under memory pressure and is asking
 * to reclaim memory. A return value of TRUE indicates that the system
 * is under memory pressure and that the arc should adjust accordingly.
 */
static boolean_t
arc_reclaim_needed(void)
{
	return (arc_available_memory() < 0);
}

static void
arc_kmem_reap_now(void)
{
	size_t			i;
	kmem_cache_t		*prev_cache = NULL;
	kmem_cache_t		*prev_data_cache = NULL;
	extern kmem_cache_t	*zio_buf_cache[];
	extern kmem_cache_t	*zio_data_buf_cache[];
	extern kmem_cache_t	*range_seg_cache;

	if ((arc_meta_used >= arc_meta_limit) && zfs_arc_meta_prune) {
		/*
		 * We are exceeding our meta-data cache limit.
		 * Prune some entries to release holds on meta-data.
		 */
		arc_prune_async(zfs_arc_meta_prune);
	}

	for (i = 0; i < SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT; i++) {
#ifdef _ILP32
		/* reach upper limit of cache size on 32-bit */
		if (zio_buf_cache[i] == NULL)
			break;
#endif
		if (zio_buf_cache[i] != prev_cache) {
			prev_cache = zio_buf_cache[i];
			kmem_cache_reap_now(zio_buf_cache[i]);
		}
		if (zio_data_buf_cache[i] != prev_data_cache) {
			prev_data_cache = zio_data_buf_cache[i];
			kmem_cache_reap_now(zio_data_buf_cache[i]);
		}
	}
	kmem_cache_reap_now(buf_cache);
	kmem_cache_reap_now(hdr_full_cache);
	kmem_cache_reap_now(hdr_l2only_cache);
	kmem_cache_reap_now(range_seg_cache);

	if (zio_arena != NULL) {
		/*
		 * Ask the vmem arena to reclaim unused memory from its
		 * quantum caches.
		 */
		vmem_qcache_reap(zio_arena);
	}
}

/*
 * Threads can block in arc_get_data_buf() waiting for this thread to evict
 * enough data and signal them to proceed. When this happens, the threads in
 * arc_get_data_buf() are sleeping while holding the hash lock for their
 * particular arc header. Thus, we must be careful to never sleep on a
 * hash lock in this thread. This is to prevent the following deadlock:
 *
 *  - Thread A sleeps on CV in arc_get_data_buf() holding hash lock "L",
 *    waiting for the reclaim thread to signal it.
 *
 *  - arc_reclaim_thread() tries to acquire hash lock "L" using mutex_enter,
 *    fails, and goes to sleep forever.
 *
 * This possible deadlock is avoided by always acquiring a hash lock
 * using mutex_tryenter() from arc_reclaim_thread().
 */
static void
arc_reclaim_thread(void)
{
	fstrans_cookie_t	cookie = spl_fstrans_mark();
	clock_t			growtime = 0;
	callb_cpr_t		cpr;

	CALLB_CPR_INIT(&cpr, &arc_reclaim_lock, callb_generic_cpr, FTAG);

	mutex_enter(&arc_reclaim_lock);
	while (!arc_reclaim_thread_exit) {
		int64_t to_free;
		int64_t free_memory = arc_available_memory();
		uint64_t evicted = 0;

		arc_tuning_update();

		mutex_exit(&arc_reclaim_lock);

		if (free_memory < 0) {

			arc_no_grow = B_TRUE;
			arc_warm = B_TRUE;

			/*
			 * Wait at least zfs_grow_retry (default 5) seconds
			 * before considering growing.
			 */
			growtime = ddi_get_lbolt() + (arc_grow_retry * hz);

			arc_kmem_reap_now();

			/*
			 * If we are still low on memory, shrink the ARC
			 * so that we have arc_shrink_min free space.
			 */
			free_memory = arc_available_memory();

			to_free = (arc_c >> arc_shrink_shift) - free_memory;
			if (to_free > 0) {
#ifdef _KERNEL
				to_free = MAX(to_free, arc_need_free);
#endif
				arc_shrink(to_free);
			}
		} else if (free_memory < arc_c >> arc_no_grow_shift) {
			arc_no_grow = B_TRUE;
		} else if (ddi_get_lbolt() >= growtime) {
			arc_no_grow = B_FALSE;
		}

		evicted = arc_adjust();

		mutex_enter(&arc_reclaim_lock);

		/*
		 * If evicted is zero, we couldn't evict anything via
		 * arc_adjust(). This could be due to hash lock
		 * collisions, but more likely due to the majority of
		 * arc buffers being unevictable. Therefore, even if
		 * arc_size is above arc_c, another pass is unlikely to
		 * be helpful and could potentially cause us to enter an
		 * infinite loop.
		 */
		if (arc_size <= arc_c || evicted == 0) {
			/*
			 * We're either no longer overflowing, or we
			 * can't evict anything more, so we should wake
			 * up any threads before we go to sleep and clear
			 * arc_need_free since nothing more can be done.
			 */
			cv_broadcast(&arc_reclaim_waiters_cv);
			arc_need_free = 0;

			/*
			 * Block until signaled, or after one second (we
			 * might need to perform arc_kmem_reap_now()
			 * even if we aren't being signalled)
			 */
			CALLB_CPR_SAFE_BEGIN(&cpr);
			(void) cv_timedwait_sig(&arc_reclaim_thread_cv,
			    &arc_reclaim_lock, ddi_get_lbolt() + hz);
			CALLB_CPR_SAFE_END(&cpr, &arc_reclaim_lock);
		}
	}

	arc_reclaim_thread_exit = FALSE;
	cv_broadcast(&arc_reclaim_thread_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops arc_reclaim_lock */
	spl_fstrans_unmark(cookie);
	thread_exit();
}

static void
arc_user_evicts_thread(void)
{
	fstrans_cookie_t	cookie = spl_fstrans_mark();
	callb_cpr_t cpr;

	CALLB_CPR_INIT(&cpr, &arc_user_evicts_lock, callb_generic_cpr, FTAG);

	mutex_enter(&arc_user_evicts_lock);
	while (!arc_user_evicts_thread_exit) {
		mutex_exit(&arc_user_evicts_lock);

		arc_do_user_evicts();

		/*
		 * This is necessary in order for the mdb ::arc dcmd to
		 * show up to date information. Since the ::arc command
		 * does not call the kstat's update function, without
		 * this call, the command may show stale stats for the
		 * anon, mru, mru_ghost, mfu, and mfu_ghost lists. Even
		 * with this change, the data might be up to 1 second
		 * out of date; but that should suffice. The arc_state_t
		 * structures can be queried directly if more accurate
		 * information is needed.
		 */
		if (arc_ksp != NULL)
			arc_ksp->ks_update(arc_ksp, KSTAT_READ);

		mutex_enter(&arc_user_evicts_lock);

		/*
		 * Block until signaled, or after one second (we need to
		 * call the arc's kstat update function regularly).
		 */
		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait_sig(&arc_user_evicts_cv,
		    &arc_user_evicts_lock, ddi_get_lbolt() + hz);
		CALLB_CPR_SAFE_END(&cpr, &arc_user_evicts_lock);
	}

	arc_user_evicts_thread_exit = FALSE;
	cv_broadcast(&arc_user_evicts_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops arc_user_evicts_lock */
	spl_fstrans_unmark(cookie);
	thread_exit();
}

#ifdef _KERNEL
/*
 * Determine the amount of memory eligible for eviction contained in the
 * ARC. All clean data reported by the ghost lists can always be safely
 * evicted. Due to arc_c_min, the same does not hold for all clean data
 * contained by the regular mru and mfu lists.
 *
 * In the case of the regular mru and mfu lists, we need to report as
 * much clean data as possible, such that evicting that same reported
 * data will not bring arc_size below arc_c_min. Thus, in certain
 * circumstances, the total amount of clean data in the mru and mfu
 * lists might not actually be evictable.
 *
 * The following two distinct cases are accounted for:
 *
 * 1. The sum of the amount of dirty data contained by both the mru and
 *    mfu lists, plus the ARC's other accounting (e.g. the anon list),
 *    is greater than or equal to arc_c_min.
 *    (i.e. amount of dirty data >= arc_c_min)
 *
 *    This is the easy case; all clean data contained by the mru and mfu
 *    lists is evictable. Evicting all clean data can only drop arc_size
 *    to the amount of dirty data, which is greater than arc_c_min.
 *
 * 2. The sum of the amount of dirty data contained by both the mru and
 *    mfu lists, plus the ARC's other accounting (e.g. the anon list),
 *    is less than arc_c_min.
 *    (i.e. arc_c_min > amount of dirty data)
 *
 *    2.1. arc_size is greater than or equal arc_c_min.
 *         (i.e. arc_size >= arc_c_min > amount of dirty data)
 *
 *         In this case, not all clean data from the regular mru and mfu
 *         lists is actually evictable; we must leave enough clean data
 *         to keep arc_size above arc_c_min. Thus, the maximum amount of
 *         evictable data from the two lists combined, is exactly the
 *         difference between arc_size and arc_c_min.
 *
 *    2.2. arc_size is less than arc_c_min
 *         (i.e. arc_c_min > arc_size > amount of dirty data)
 *
 *         In this case, none of the data contained in the mru and mfu
 *         lists is evictable, even if it's clean. Since arc_size is
 *         already below arc_c_min, evicting any more would only
 *         increase this negative difference.
 */
static uint64_t
arc_evictable_memory(void) {
	uint64_t arc_clean =
	    arc_mru->arcs_lsize[ARC_BUFC_DATA] +
	    arc_mru->arcs_lsize[ARC_BUFC_METADATA] +
	    arc_mfu->arcs_lsize[ARC_BUFC_DATA] +
	    arc_mfu->arcs_lsize[ARC_BUFC_METADATA];
	uint64_t ghost_clean =
	    arc_mru_ghost->arcs_lsize[ARC_BUFC_DATA] +
	    arc_mru_ghost->arcs_lsize[ARC_BUFC_METADATA] +
	    arc_mfu_ghost->arcs_lsize[ARC_BUFC_DATA] +
	    arc_mfu_ghost->arcs_lsize[ARC_BUFC_METADATA];
	uint64_t arc_dirty = MAX((int64_t)arc_size - (int64_t)arc_clean, 0);

	if (arc_dirty >= arc_c_min)
		return (ghost_clean + arc_clean);

	return (ghost_clean + MAX((int64_t)arc_size - (int64_t)arc_c_min, 0));
}

/*
 * If sc->nr_to_scan is zero, the caller is requesting a query of the
 * number of objects which can potentially be freed.  If it is nonzero,
 * the request is to free that many objects.
 *
 * Linux kernels >= 3.12 have the count_objects and scan_objects callbacks
 * in struct shrinker and also require the shrinker to return the number
 * of objects freed.
 *
 * Older kernels require the shrinker to return the number of freeable
 * objects following the freeing of nr_to_free.
 */
static spl_shrinker_t
__arc_shrinker_func(struct shrinker *shrink, struct shrink_control *sc)
{
	int64_t pages;

	/* The arc is considered warm once reclaim has occurred */
	if (unlikely(arc_warm == B_FALSE))
		arc_warm = B_TRUE;

	/* Return the potential number of reclaimable pages */
	pages = btop((int64_t)arc_evictable_memory());
	if (sc->nr_to_scan == 0)
		return (pages);

	/* Not allowed to perform filesystem reclaim */
	if (!(sc->gfp_mask & __GFP_FS))
		return (SHRINK_STOP);

	/* Reclaim in progress */
	if (mutex_tryenter(&arc_reclaim_lock) == 0)
		return (SHRINK_STOP);

	mutex_exit(&arc_reclaim_lock);

	/*
	 * Evict the requested number of pages by shrinking arc_c the
	 * requested amount.  If there is nothing left to evict just
	 * reap whatever we can from the various arc slabs.
	 */
	if (pages > 0) {
		arc_shrink(ptob(sc->nr_to_scan));
		arc_kmem_reap_now();
#ifdef HAVE_SPLIT_SHRINKER_CALLBACK
		pages = MAX(pages - btop(arc_evictable_memory()), 0);
#else
		pages = btop(arc_evictable_memory());
#endif
	} else {
		arc_kmem_reap_now();
		pages = SHRINK_STOP;
	}

	/*
	 * We've reaped what we can, wake up threads.
	 */
	cv_broadcast(&arc_reclaim_waiters_cv);

	/*
	 * When direct reclaim is observed it usually indicates a rapid
	 * increase in memory pressure.  This occurs because the kswapd
	 * threads were unable to asynchronously keep enough free memory
	 * available.  In this case set arc_no_grow to briefly pause arc
	 * growth to avoid compounding the memory pressure.
	 */
	if (current_is_kswapd()) {
		ARCSTAT_BUMP(arcstat_memory_indirect_count);
	} else {
		arc_no_grow = B_TRUE;
		arc_need_free = ptob(sc->nr_to_scan);
		ARCSTAT_BUMP(arcstat_memory_direct_count);
	}

	return (pages);
}
SPL_SHRINKER_CALLBACK_WRAPPER(arc_shrinker_func);

SPL_SHRINKER_DECLARE(arc_shrinker, arc_shrinker_func, DEFAULT_SEEKS);
#endif /* _KERNEL */

/*
 * Adapt arc info given the number of bytes we are trying to add and
 * the state that we are comming from.  This function is only called
 * when we are adding new content to the cache.
 */
static void
arc_adapt(int bytes, arc_state_t *state)
{
	int mult;
	uint64_t arc_p_min = (arc_c >> arc_p_min_shift);
	int64_t mrug_size = refcount_count(&arc_mru_ghost->arcs_size);
	int64_t mfug_size = refcount_count(&arc_mfu_ghost->arcs_size);

	if (state == arc_l2c_only)
		return;

	ASSERT(bytes > 0);
	/*
	 * Adapt the target size of the MRU list:
	 *	- if we just hit in the MRU ghost list, then increase
	 *	  the target size of the MRU list.
	 *	- if we just hit in the MFU ghost list, then increase
	 *	  the target size of the MFU list by decreasing the
	 *	  target size of the MRU list.
	 */
	if (state == arc_mru_ghost) {
		mult = (mrug_size >= mfug_size) ? 1 : (mfug_size / mrug_size);
		if (!zfs_arc_p_dampener_disable)
			mult = MIN(mult, 10); /* avoid wild arc_p adjustment */

		arc_p = MIN(arc_c - arc_p_min, arc_p + bytes * mult);
	} else if (state == arc_mfu_ghost) {
		uint64_t delta;

		mult = (mfug_size >= mrug_size) ? 1 : (mrug_size / mfug_size);
		if (!zfs_arc_p_dampener_disable)
			mult = MIN(mult, 10);

		delta = MIN(bytes * mult, arc_p);
		arc_p = MAX(arc_p_min, arc_p - delta);
	}
	ASSERT((int64_t)arc_p >= 0);

	if (arc_reclaim_needed()) {
		cv_signal(&arc_reclaim_thread_cv);
		return;
	}

	if (arc_no_grow)
		return;

	if (arc_c >= arc_c_max)
		return;

	/*
	 * If we're within (2 * maxblocksize) bytes of the target
	 * cache size, increment the target cache size
	 */
	ASSERT3U(arc_c, >=, 2ULL << SPA_MAXBLOCKSHIFT);
	if (arc_size >= arc_c - (2ULL << SPA_MAXBLOCKSHIFT)) {
		atomic_add_64(&arc_c, (int64_t)bytes);
		if (arc_c > arc_c_max)
			arc_c = arc_c_max;
		else if (state == arc_anon)
			atomic_add_64(&arc_p, (int64_t)bytes);
		if (arc_p > arc_c)
			arc_p = arc_c;
	}
	ASSERT((int64_t)arc_p >= 0);
}

/*
 * Check if arc_size has grown past our upper threshold, determined by
 * zfs_arc_overflow_shift.
 */
static boolean_t
arc_is_overflowing(void)
{
	/* Always allow at least one block of overflow */
	uint64_t overflow = MAX(SPA_MAXBLOCKSIZE,
	    arc_c >> zfs_arc_overflow_shift);

	return (arc_size >= arc_c + overflow);
}

/*
 * The buffer, supplied as the first argument, needs a data block. If we
 * are hitting the hard limit for the cache size, we must sleep, waiting
 * for the eviction thread to catch up. If we're past the target size
 * but below the hard limit, we'll only signal the reclaim thread and
 * continue on.
 */
static void
arc_get_data_buf(arc_buf_t *buf)
{
	arc_state_t		*state = buf->b_hdr->b_l1hdr.b_state;
	uint64_t		size = buf->b_hdr->b_size;
	arc_buf_contents_t	type = arc_buf_type(buf->b_hdr);

	arc_adapt(size, state);

	/*
	 * If arc_size is currently overflowing, and has grown past our
	 * upper limit, we must be adding data faster than the evict
	 * thread can evict. Thus, to ensure we don't compound the
	 * problem by adding more data and forcing arc_size to grow even
	 * further past it's target size, we halt and wait for the
	 * eviction thread to catch up.
	 *
	 * It's also possible that the reclaim thread is unable to evict
	 * enough buffers to get arc_size below the overflow limit (e.g.
	 * due to buffers being un-evictable, or hash lock collisions).
	 * In this case, we want to proceed regardless if we're
	 * overflowing; thus we don't use a while loop here.
	 */
	if (arc_is_overflowing()) {
		mutex_enter(&arc_reclaim_lock);

		/*
		 * Now that we've acquired the lock, we may no longer be
		 * over the overflow limit, lets check.
		 *
		 * We're ignoring the case of spurious wake ups. If that
		 * were to happen, it'd let this thread consume an ARC
		 * buffer before it should have (i.e. before we're under
		 * the overflow limit and were signalled by the reclaim
		 * thread). As long as that is a rare occurrence, it
		 * shouldn't cause any harm.
		 */
		if (arc_is_overflowing()) {
			cv_signal(&arc_reclaim_thread_cv);
			cv_wait(&arc_reclaim_waiters_cv, &arc_reclaim_lock);
		}

		mutex_exit(&arc_reclaim_lock);
	}

	if (type == ARC_BUFC_METADATA) {
		buf->b_data = zio_buf_alloc(size);
		arc_space_consume(size, ARC_SPACE_META);
	} else {
		ASSERT(type == ARC_BUFC_DATA);
		buf->b_data = zio_data_buf_alloc(size);
		arc_space_consume(size, ARC_SPACE_DATA);
	}

	/*
	 * Update the state size.  Note that ghost states have a
	 * "ghost size" and so don't need to be updated.
	 */
	if (!GHOST_STATE(buf->b_hdr->b_l1hdr.b_state)) {
		arc_buf_hdr_t *hdr = buf->b_hdr;
		arc_state_t *state = hdr->b_l1hdr.b_state;

		(void) refcount_add_many(&state->arcs_size, size, buf);

		/*
		 * If this is reached via arc_read, the link is
		 * protected by the hash lock. If reached via
		 * arc_buf_alloc, the header should not be accessed by
		 * any other thread. And, if reached via arc_read_done,
		 * the hash lock will protect it if it's found in the
		 * hash table; otherwise no other thread should be
		 * trying to [add|remove]_reference it.
		 */
		if (multilist_link_active(&hdr->b_l1hdr.b_arc_node)) {
			ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
			atomic_add_64(&hdr->b_l1hdr.b_state->arcs_lsize[type],
			    size);
		}
		/*
		 * If we are growing the cache, and we are adding anonymous
		 * data, and we have outgrown arc_p, update arc_p
		 */
		if (arc_size < arc_c && hdr->b_l1hdr.b_state == arc_anon &&
		    (refcount_count(&arc_anon->arcs_size) +
		    refcount_count(&arc_mru->arcs_size) > arc_p))
			arc_p = MIN(arc_c, arc_p + size);
	}
}

/*
 * This routine is called whenever a buffer is accessed.
 * NOTE: the hash lock is dropped in this function.
 */
static void
arc_access(arc_buf_hdr_t *hdr, kmutex_t *hash_lock)
{
	clock_t now;

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT(HDR_HAS_L1HDR(hdr));

	if (hdr->b_l1hdr.b_state == arc_anon) {
		/*
		 * This buffer is not in the cache, and does not
		 * appear in our "ghost" list.  Add the new buffer
		 * to the MRU state.
		 */

		ASSERT0(hdr->b_l1hdr.b_arc_access);
		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		DTRACE_PROBE1(new_state__mru, arc_buf_hdr_t *, hdr);
		arc_change_state(arc_mru, hdr, hash_lock);

	} else if (hdr->b_l1hdr.b_state == arc_mru) {
		now = ddi_get_lbolt();

		/*
		 * If this buffer is here because of a prefetch, then either:
		 * - clear the flag if this is a "referencing" read
		 *   (any subsequent access will bump this into the MFU state).
		 * or
		 * - move the buffer to the head of the list if this is
		 *   another prefetch (to make it less likely to be evicted).
		 */
		if (HDR_PREFETCH(hdr)) {
			if (refcount_count(&hdr->b_l1hdr.b_refcnt) == 0) {
				/* link protected by hash lock */
				ASSERT(multilist_link_active(
				    &hdr->b_l1hdr.b_arc_node));
			} else {
				hdr->b_flags &= ~ARC_FLAG_PREFETCH;
				atomic_inc_32(&hdr->b_l1hdr.b_mru_hits);
				ARCSTAT_BUMP(arcstat_mru_hits);
			}
			hdr->b_l1hdr.b_arc_access = now;
			return;
		}

		/*
		 * This buffer has been "accessed" only once so far,
		 * but it is still in the cache. Move it to the MFU
		 * state.
		 */
		if (ddi_time_after(now, hdr->b_l1hdr.b_arc_access +
		    ARC_MINTIME)) {
			/*
			 * More than 125ms have passed since we
			 * instantiated this buffer.  Move it to the
			 * most frequently used state.
			 */
			hdr->b_l1hdr.b_arc_access = now;
			DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
			arc_change_state(arc_mfu, hdr, hash_lock);
		}
		atomic_inc_32(&hdr->b_l1hdr.b_mru_hits);
		ARCSTAT_BUMP(arcstat_mru_hits);
	} else if (hdr->b_l1hdr.b_state == arc_mru_ghost) {
		arc_state_t	*new_state;
		/*
		 * This buffer has been "accessed" recently, but
		 * was evicted from the cache.  Move it to the
		 * MFU state.
		 */

		if (HDR_PREFETCH(hdr)) {
			new_state = arc_mru;
			if (refcount_count(&hdr->b_l1hdr.b_refcnt) > 0)
				hdr->b_flags &= ~ARC_FLAG_PREFETCH;
			DTRACE_PROBE1(new_state__mru, arc_buf_hdr_t *, hdr);
		} else {
			new_state = arc_mfu;
			DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
		}

		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		arc_change_state(new_state, hdr, hash_lock);

		atomic_inc_32(&hdr->b_l1hdr.b_mru_ghost_hits);
		ARCSTAT_BUMP(arcstat_mru_ghost_hits);
	} else if (hdr->b_l1hdr.b_state == arc_mfu) {
		/*
		 * This buffer has been accessed more than once and is
		 * still in the cache.  Keep it in the MFU state.
		 *
		 * NOTE: an add_reference() that occurred when we did
		 * the arc_read() will have kicked this off the list.
		 * If it was a prefetch, we will explicitly move it to
		 * the head of the list now.
		 */
		if ((HDR_PREFETCH(hdr)) != 0) {
			ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
			/* link protected by hash_lock */
			ASSERT(multilist_link_active(&hdr->b_l1hdr.b_arc_node));
		}
		atomic_inc_32(&hdr->b_l1hdr.b_mfu_hits);
		ARCSTAT_BUMP(arcstat_mfu_hits);
		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
	} else if (hdr->b_l1hdr.b_state == arc_mfu_ghost) {
		arc_state_t	*new_state = arc_mfu;
		/*
		 * This buffer has been accessed more than once but has
		 * been evicted from the cache.  Move it back to the
		 * MFU state.
		 */

		if (HDR_PREFETCH(hdr)) {
			/*
			 * This is a prefetch access...
			 * move this block back to the MRU state.
			 */
			ASSERT0(refcount_count(&hdr->b_l1hdr.b_refcnt));
			new_state = arc_mru;
		}

		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
		arc_change_state(new_state, hdr, hash_lock);

		atomic_inc_32(&hdr->b_l1hdr.b_mfu_ghost_hits);
		ARCSTAT_BUMP(arcstat_mfu_ghost_hits);
	} else if (hdr->b_l1hdr.b_state == arc_l2c_only) {
		/*
		 * This buffer is on the 2nd Level ARC.
		 */

		hdr->b_l1hdr.b_arc_access = ddi_get_lbolt();
		DTRACE_PROBE1(new_state__mfu, arc_buf_hdr_t *, hdr);
		arc_change_state(arc_mfu, hdr, hash_lock);
	} else {
		cmn_err(CE_PANIC, "invalid arc state 0x%p",
		    hdr->b_l1hdr.b_state);
	}
}

/* a generic arc_done_func_t which you can use */
/* ARGSUSED */
void
arc_bcopy_func(zio_t *zio, arc_buf_t *buf, void *arg)
{
	if (zio == NULL || zio->io_error == 0)
		bcopy(buf->b_data, arg, buf->b_hdr->b_size);
	VERIFY(arc_buf_remove_ref(buf, arg));
}

/* a generic arc_done_func_t */
void
arc_getbuf_func(zio_t *zio, arc_buf_t *buf, void *arg)
{
	arc_buf_t **bufp = arg;
	if (zio && zio->io_error) {
		VERIFY(arc_buf_remove_ref(buf, arg));
		*bufp = NULL;
	} else {
		*bufp = buf;
		ASSERT(buf->b_data);
	}
}

static void
arc_read_done(zio_t *zio)
{
	arc_buf_hdr_t	*hdr;
	arc_buf_t	*buf;
	arc_buf_t	*abuf;	/* buffer we're assigning to callback */
	kmutex_t	*hash_lock = NULL;
	arc_callback_t	*callback_list, *acb;
	int		freeable = FALSE;

	buf = zio->io_private;
	hdr = buf->b_hdr;

	/*
	 * The hdr was inserted into hash-table and removed from lists
	 * prior to starting I/O.  We should find this header, since
	 * it's in the hash table, and it should be legit since it's
	 * not possible to evict it during the I/O.  The only possible
	 * reason for it not to be found is if we were freed during the
	 * read.
	 */
	if (HDR_IN_HASH_TABLE(hdr)) {
		arc_buf_hdr_t *found;

		ASSERT3U(hdr->b_birth, ==, BP_PHYSICAL_BIRTH(zio->io_bp));
		ASSERT3U(hdr->b_dva.dva_word[0], ==,
		    BP_IDENTITY(zio->io_bp)->dva_word[0]);
		ASSERT3U(hdr->b_dva.dva_word[1], ==,
		    BP_IDENTITY(zio->io_bp)->dva_word[1]);

		found = buf_hash_find(hdr->b_spa, zio->io_bp,
		    &hash_lock);

		ASSERT((found == NULL && HDR_FREED_IN_READ(hdr) &&
		    hash_lock == NULL) ||
		    (found == hdr &&
		    DVA_EQUAL(&hdr->b_dva, BP_IDENTITY(zio->io_bp))) ||
		    (found == hdr && HDR_L2_READING(hdr)));
	}

	hdr->b_flags &= ~ARC_FLAG_L2_EVICTED;
	if (l2arc_noprefetch && HDR_PREFETCH(hdr))
		hdr->b_flags &= ~ARC_FLAG_L2CACHE;

	/* byteswap if necessary */
	callback_list = hdr->b_l1hdr.b_acb;
	ASSERT(callback_list != NULL);
	if (BP_SHOULD_BYTESWAP(zio->io_bp) && zio->io_error == 0) {
		dmu_object_byteswap_t bswap =
		    DMU_OT_BYTESWAP(BP_GET_TYPE(zio->io_bp));
		if (BP_GET_LEVEL(zio->io_bp) > 0)
		    byteswap_uint64_array(buf->b_data, hdr->b_size);
		else
		    dmu_ot_byteswap[bswap].ob_func(buf->b_data, hdr->b_size);
	}

	arc_cksum_compute(buf, B_FALSE);
	arc_buf_watch(buf);

	if (hash_lock && zio->io_error == 0 &&
	    hdr->b_l1hdr.b_state == arc_anon) {
		/*
		 * Only call arc_access on anonymous buffers.  This is because
		 * if we've issued an I/O for an evicted buffer, we've already
		 * called arc_access (to prevent any simultaneous readers from
		 * getting confused).
		 */
		arc_access(hdr, hash_lock);
	}

	/* create copies of the data buffer for the callers */
	abuf = buf;
	for (acb = callback_list; acb; acb = acb->acb_next) {
		if (acb->acb_done) {
			if (abuf == NULL) {
				ARCSTAT_BUMP(arcstat_duplicate_reads);
				abuf = arc_buf_clone(buf);
			}
			acb->acb_buf = abuf;
			abuf = NULL;
		}
	}
	hdr->b_l1hdr.b_acb = NULL;
	hdr->b_flags &= ~ARC_FLAG_IO_IN_PROGRESS;
	ASSERT(!HDR_BUF_AVAILABLE(hdr));
	if (abuf == buf) {
		ASSERT(buf->b_efunc == NULL);
		ASSERT(hdr->b_l1hdr.b_datacnt == 1);
		hdr->b_flags |= ARC_FLAG_BUF_AVAILABLE;
	}

	ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt) ||
	    callback_list != NULL);

	if (zio->io_error != 0) {
		hdr->b_flags |= ARC_FLAG_IO_ERROR;
		if (hdr->b_l1hdr.b_state != arc_anon)
			arc_change_state(arc_anon, hdr, hash_lock);
		if (HDR_IN_HASH_TABLE(hdr))
			buf_hash_remove(hdr);
		freeable = refcount_is_zero(&hdr->b_l1hdr.b_refcnt);
	}

	/*
	 * Broadcast before we drop the hash_lock to avoid the possibility
	 * that the hdr (and hence the cv) might be freed before we get to
	 * the cv_broadcast().
	 */
	cv_broadcast(&hdr->b_l1hdr.b_cv);

	if (hash_lock != NULL) {
		mutex_exit(hash_lock);
	} else {
		/*
		 * This block was freed while we waited for the read to
		 * complete.  It has been removed from the hash table and
		 * moved to the anonymous state (so that it won't show up
		 * in the cache).
		 */
		ASSERT3P(hdr->b_l1hdr.b_state, ==, arc_anon);
		freeable = refcount_is_zero(&hdr->b_l1hdr.b_refcnt);
	}

	/* execute each callback and free its structure */
	while ((acb = callback_list) != NULL) {
		if (acb->acb_done)
			acb->acb_done(zio, acb->acb_buf, acb->acb_private);

		if (acb->acb_zio_dummy != NULL) {
			acb->acb_zio_dummy->io_error = zio->io_error;
			zio_nowait(acb->acb_zio_dummy);
		}

		callback_list = acb->acb_next;
		kmem_free(acb, sizeof (arc_callback_t));
	}

	if (freeable)
		arc_hdr_destroy(hdr);
}

/*
 * "Read" the block at the specified DVA (in bp) via the
 * cache.  If the block is found in the cache, invoke the provided
 * callback immediately and return.  Note that the `zio' parameter
 * in the callback will be NULL in this case, since no IO was
 * required.  If the block is not in the cache pass the read request
 * on to the spa with a substitute callback function, so that the
 * requested block will be added to the cache.
 *
 * If a read request arrives for a block that has a read in-progress,
 * either wait for the in-progress read to complete (and return the
 * results); or, if this is a read with a "done" func, add a record
 * to the read to invoke the "done" func when the read completes,
 * and return; or just return.
 *
 * arc_read_done() will invoke all the requested "done" functions
 * for readers of this block.
 */
int
arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp, arc_done_func_t *done,
    void *private, zio_priority_t priority, int zio_flags,
    arc_flags_t *arc_flags, const zbookmark_phys_t *zb)
{
	arc_buf_hdr_t *hdr = NULL;
	arc_buf_t *buf = NULL;
	kmutex_t *hash_lock = NULL;
	zio_t *rzio;
	uint64_t guid = spa_load_guid(spa);
	int rc = 0;

	ASSERT(!BP_IS_EMBEDDED(bp) ||
	    BPE_GET_ETYPE(bp) == BP_EMBEDDED_TYPE_DATA);

top:
	if (!BP_IS_EMBEDDED(bp)) {
		/*
		 * Embedded BP's have no DVA and require no I/O to "read".
		 * Create an anonymous arc buf to back it.
		 */
		hdr = buf_hash_find(guid, bp, &hash_lock);
	}

	if (hdr != NULL && HDR_HAS_L1HDR(hdr) && hdr->b_l1hdr.b_datacnt > 0) {

		*arc_flags |= ARC_FLAG_CACHED;

		if (HDR_IO_IN_PROGRESS(hdr)) {

			if (*arc_flags & ARC_FLAG_WAIT) {
				cv_wait(&hdr->b_l1hdr.b_cv, hash_lock);
				mutex_exit(hash_lock);
				goto top;
			}
			ASSERT(*arc_flags & ARC_FLAG_NOWAIT);

			if (done) {
				arc_callback_t	*acb = NULL;

				acb = kmem_zalloc(sizeof (arc_callback_t),
				    KM_SLEEP);
				acb->acb_done = done;
				acb->acb_private = private;
				if (pio != NULL)
					acb->acb_zio_dummy = zio_null(pio,
					    spa, NULL, NULL, NULL, zio_flags);

				ASSERT(acb->acb_done != NULL);
				acb->acb_next = hdr->b_l1hdr.b_acb;
				hdr->b_l1hdr.b_acb = acb;
				add_reference(hdr, hash_lock, private);
				mutex_exit(hash_lock);
				goto out;
			}
			mutex_exit(hash_lock);
			goto out;
		}

		ASSERT(hdr->b_l1hdr.b_state == arc_mru ||
		    hdr->b_l1hdr.b_state == arc_mfu);

		if (done) {
			add_reference(hdr, hash_lock, private);
			/*
			 * If this block is already in use, create a new
			 * copy of the data so that we will be guaranteed
			 * that arc_release() will always succeed.
			 */
			buf = hdr->b_l1hdr.b_buf;
			ASSERT(buf);
			ASSERT(buf->b_data);
			if (HDR_BUF_AVAILABLE(hdr)) {
				ASSERT(buf->b_efunc == NULL);
				hdr->b_flags &= ~ARC_FLAG_BUF_AVAILABLE;
			} else {
				buf = arc_buf_clone(buf);
			}

		} else if (*arc_flags & ARC_FLAG_PREFETCH &&
		    refcount_count(&hdr->b_l1hdr.b_refcnt) == 0) {
			hdr->b_flags |= ARC_FLAG_PREFETCH;
		}
		DTRACE_PROBE1(arc__hit, arc_buf_hdr_t *, hdr);
		arc_access(hdr, hash_lock);
		if (*arc_flags & ARC_FLAG_L2CACHE)
			hdr->b_flags |= ARC_FLAG_L2CACHE;
		if (*arc_flags & ARC_FLAG_L2COMPRESS)
			hdr->b_flags |= ARC_FLAG_L2COMPRESS;
		mutex_exit(hash_lock);
		ARCSTAT_BUMP(arcstat_hits);
		ARCSTAT_CONDSTAT(!HDR_PREFETCH(hdr),
		    demand, prefetch, !HDR_ISTYPE_METADATA(hdr),
		    data, metadata, hits);

		if (done)
			done(NULL, buf, private);
	} else {
		uint64_t size = BP_GET_LSIZE(bp);
		arc_callback_t *acb;
		vdev_t *vd = NULL;
		uint64_t addr = 0;
		boolean_t devw = B_FALSE;
		enum zio_compress b_compress = ZIO_COMPRESS_OFF;
		int32_t b_asize = 0;

		/*
		 * Gracefully handle a damaged logical block size as a
		 * checksum error.
		 */
		if (size > spa_maxblocksize(spa)) {
			ASSERT3P(buf, ==, NULL);
			rc = SET_ERROR(ECKSUM);
			goto out;
		}

		if (hdr == NULL) {
			/* this block is not in the cache */
			arc_buf_hdr_t *exists = NULL;
			arc_buf_contents_t type = BP_GET_BUFC_TYPE(bp);
			buf = arc_buf_alloc(spa, size, private, type);
			hdr = buf->b_hdr;
			if (!BP_IS_EMBEDDED(bp)) {
				hdr->b_dva = *BP_IDENTITY(bp);
				hdr->b_birth = BP_PHYSICAL_BIRTH(bp);
				exists = buf_hash_insert(hdr, &hash_lock);
			}
			if (exists != NULL) {
				/* somebody beat us to the hash insert */
				mutex_exit(hash_lock);
				buf_discard_identity(hdr);
				(void) arc_buf_remove_ref(buf, private);
				goto top; /* restart the IO request */
			}

			/* if this is a prefetch, we don't have a reference */
			if (*arc_flags & ARC_FLAG_PREFETCH) {
				(void) remove_reference(hdr, hash_lock,
				    private);
				hdr->b_flags |= ARC_FLAG_PREFETCH;
			}
			if (*arc_flags & ARC_FLAG_L2CACHE)
				hdr->b_flags |= ARC_FLAG_L2CACHE;
			if (*arc_flags & ARC_FLAG_L2COMPRESS)
				hdr->b_flags |= ARC_FLAG_L2COMPRESS;
			if (BP_GET_LEVEL(bp) > 0)
				hdr->b_flags |= ARC_FLAG_INDIRECT;
		} else {
			/*
			 * This block is in the ghost cache. If it was L2-only
			 * (and thus didn't have an L1 hdr), we realloc the
			 * header to add an L1 hdr.
			 */
			if (!HDR_HAS_L1HDR(hdr)) {
				hdr = arc_hdr_realloc(hdr, hdr_l2only_cache,
				    hdr_full_cache);
			}

			ASSERT(GHOST_STATE(hdr->b_l1hdr.b_state));
			ASSERT(!HDR_IO_IN_PROGRESS(hdr));
			ASSERT(refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
			ASSERT3P(hdr->b_l1hdr.b_buf, ==, NULL);

			/* if this is a prefetch, we don't have a reference */
			if (*arc_flags & ARC_FLAG_PREFETCH)
				hdr->b_flags |= ARC_FLAG_PREFETCH;
			else
				add_reference(hdr, hash_lock, private);
			if (*arc_flags & ARC_FLAG_L2CACHE)
				hdr->b_flags |= ARC_FLAG_L2CACHE;
			if (*arc_flags & ARC_FLAG_L2COMPRESS)
				hdr->b_flags |= ARC_FLAG_L2COMPRESS;
			buf = kmem_cache_alloc(buf_cache, KM_PUSHPAGE);
			buf->b_hdr = hdr;
			buf->b_data = NULL;
			buf->b_efunc = NULL;
			buf->b_private = NULL;
			buf->b_next = NULL;
			hdr->b_l1hdr.b_buf = buf;
			ASSERT0(hdr->b_l1hdr.b_datacnt);
			hdr->b_l1hdr.b_datacnt = 1;
			arc_get_data_buf(buf);
			arc_access(hdr, hash_lock);
		}

		ASSERT(!GHOST_STATE(hdr->b_l1hdr.b_state));

		acb = kmem_zalloc(sizeof (arc_callback_t), KM_SLEEP);
		acb->acb_done = done;
		acb->acb_private = private;

		ASSERT(hdr->b_l1hdr.b_acb == NULL);
		hdr->b_l1hdr.b_acb = acb;
		hdr->b_flags |= ARC_FLAG_IO_IN_PROGRESS;

		if (HDR_HAS_L2HDR(hdr) &&
		    (vd = hdr->b_l2hdr.b_dev->l2ad_vdev) != NULL) {
			devw = hdr->b_l2hdr.b_dev->l2ad_writing;
			addr = hdr->b_l2hdr.b_daddr;
			b_compress = hdr->b_l2hdr.b_compress;
			b_asize = hdr->b_l2hdr.b_asize;
			/*
			 * Lock out device removal.
			 */
			if (vdev_is_dead(vd) ||
			    !spa_config_tryenter(spa, SCL_L2ARC, vd, RW_READER))
				vd = NULL;
		}

		if (hash_lock != NULL)
			mutex_exit(hash_lock);

		/*
		 * At this point, we have a level 1 cache miss.  Try again in
		 * L2ARC if possible.
		 */
		ASSERT3U(hdr->b_size, ==, size);
		DTRACE_PROBE4(arc__miss, arc_buf_hdr_t *, hdr, blkptr_t *, bp,
		    uint64_t, size, zbookmark_phys_t *, zb);
		ARCSTAT_BUMP(arcstat_misses);
		ARCSTAT_CONDSTAT(!HDR_PREFETCH(hdr),
		    demand, prefetch, !HDR_ISTYPE_METADATA(hdr),
		    data, metadata, misses);

		if (vd != NULL && l2arc_ndev != 0 && !(l2arc_norw && devw)) {
			/*
			 * Read from the L2ARC if the following are true:
			 * 1. The L2ARC vdev was previously cached.
			 * 2. This buffer still has L2ARC metadata.
			 * 3. This buffer isn't currently writing to the L2ARC.
			 * 4. The L2ARC entry wasn't evicted, which may
			 *    also have invalidated the vdev.
			 * 5. This isn't prefetch and l2arc_noprefetch is set.
			 */
			if (HDR_HAS_L2HDR(hdr) &&
			    !HDR_L2_WRITING(hdr) && !HDR_L2_EVICTED(hdr) &&
			    !(l2arc_noprefetch && HDR_PREFETCH(hdr))) {
				l2arc_read_callback_t *cb;

				DTRACE_PROBE1(l2arc__hit, arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(arcstat_l2_hits);
				atomic_inc_32(&hdr->b_l2hdr.b_hits);

				cb = kmem_zalloc(sizeof (l2arc_read_callback_t),
				    KM_SLEEP);
				cb->l2rcb_buf = buf;
				cb->l2rcb_spa = spa;
				cb->l2rcb_bp = *bp;
				cb->l2rcb_zb = *zb;
				cb->l2rcb_flags = zio_flags;
				cb->l2rcb_compress = b_compress;

				ASSERT(addr >= VDEV_LABEL_START_SIZE &&
				    addr + size < vd->vdev_psize -
				    VDEV_LABEL_END_SIZE);

				/*
				 * l2arc read.  The SCL_L2ARC lock will be
				 * released by l2arc_read_done().
				 * Issue a null zio if the underlying buffer
				 * was squashed to zero size by compression.
				 */
				if (b_compress == ZIO_COMPRESS_EMPTY) {
					rzio = zio_null(pio, spa, vd,
					    l2arc_read_done, cb,
					    zio_flags | ZIO_FLAG_DONT_CACHE |
					    ZIO_FLAG_CANFAIL |
					    ZIO_FLAG_DONT_PROPAGATE |
					    ZIO_FLAG_DONT_RETRY);
				} else {
					rzio = zio_read_phys(pio, vd, addr,
					    b_asize, buf->b_data,
					    ZIO_CHECKSUM_OFF,
					    l2arc_read_done, cb, priority,
					    zio_flags | ZIO_FLAG_DONT_CACHE |
					    ZIO_FLAG_CANFAIL |
					    ZIO_FLAG_DONT_PROPAGATE |
					    ZIO_FLAG_DONT_RETRY, B_FALSE);
				}
				DTRACE_PROBE2(l2arc__read, vdev_t *, vd,
				    zio_t *, rzio);
				ARCSTAT_INCR(arcstat_l2_read_bytes, b_asize);

				if (*arc_flags & ARC_FLAG_NOWAIT) {
					zio_nowait(rzio);
					goto out;
				}

				ASSERT(*arc_flags & ARC_FLAG_WAIT);
				if (zio_wait(rzio) == 0)
					goto out;

				/* l2arc read error; goto zio_read() */
			} else {
				DTRACE_PROBE1(l2arc__miss,
				    arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(arcstat_l2_misses);
				if (HDR_L2_WRITING(hdr))
					ARCSTAT_BUMP(arcstat_l2_rw_clash);
				spa_config_exit(spa, SCL_L2ARC, vd);
			}
		} else {
			if (vd != NULL)
				spa_config_exit(spa, SCL_L2ARC, vd);
			if (l2arc_ndev != 0) {
				DTRACE_PROBE1(l2arc__miss,
				    arc_buf_hdr_t *, hdr);
				ARCSTAT_BUMP(arcstat_l2_misses);
			}
		}

		rzio = zio_read(pio, spa, bp, buf->b_data, size,
		    arc_read_done, buf, priority, zio_flags, zb);

		if (*arc_flags & ARC_FLAG_WAIT) {
			rc = zio_wait(rzio);
			goto out;
		}

		ASSERT(*arc_flags & ARC_FLAG_NOWAIT);
		zio_nowait(rzio);
	}

out:
	spa_read_history_add(spa, zb, *arc_flags);
	return (rc);
}

arc_prune_t *
arc_add_prune_callback(arc_prune_func_t *func, void *private)
{
	arc_prune_t *p;

	p = kmem_alloc(sizeof (*p), KM_SLEEP);
	p->p_pfunc = func;
	p->p_private = private;
	list_link_init(&p->p_node);
	refcount_create(&p->p_refcnt);

	mutex_enter(&arc_prune_mtx);
	refcount_add(&p->p_refcnt, &arc_prune_list);
	list_insert_head(&arc_prune_list, p);
	mutex_exit(&arc_prune_mtx);

	return (p);
}

void
arc_remove_prune_callback(arc_prune_t *p)
{
	boolean_t wait = B_FALSE;
	mutex_enter(&arc_prune_mtx);
	list_remove(&arc_prune_list, p);
	if (refcount_remove(&p->p_refcnt, &arc_prune_list) > 0)
		wait = B_TRUE;
	mutex_exit(&arc_prune_mtx);

	/* wait for arc_prune_task to finish */
	if (wait)
		taskq_wait_outstanding(arc_prune_taskq, 0);
	ASSERT0(refcount_count(&p->p_refcnt));
	refcount_destroy(&p->p_refcnt);
	kmem_free(p, sizeof (*p));
}

void
arc_set_callback(arc_buf_t *buf, arc_evict_func_t *func, void *private)
{
	ASSERT(buf->b_hdr != NULL);
	ASSERT(buf->b_hdr->b_l1hdr.b_state != arc_anon);
	ASSERT(!refcount_is_zero(&buf->b_hdr->b_l1hdr.b_refcnt) ||
	    func == NULL);
	ASSERT(buf->b_efunc == NULL);
	ASSERT(!HDR_BUF_AVAILABLE(buf->b_hdr));

	buf->b_efunc = func;
	buf->b_private = private;
}

/*
 * Notify the arc that a block was freed, and thus will never be used again.
 */
void
arc_freed(spa_t *spa, const blkptr_t *bp)
{
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;
	uint64_t guid = spa_load_guid(spa);

	ASSERT(!BP_IS_EMBEDDED(bp));

	hdr = buf_hash_find(guid, bp, &hash_lock);
	if (hdr == NULL)
		return;
	if (HDR_BUF_AVAILABLE(hdr)) {
		arc_buf_t *buf = hdr->b_l1hdr.b_buf;
		add_reference(hdr, hash_lock, FTAG);
		hdr->b_flags &= ~ARC_FLAG_BUF_AVAILABLE;
		mutex_exit(hash_lock);

		arc_release(buf, FTAG);
		(void) arc_buf_remove_ref(buf, FTAG);
	} else {
		mutex_exit(hash_lock);
	}

}

/*
 * Clear the user eviction callback set by arc_set_callback(), first calling
 * it if it exists.  Because the presence of a callback keeps an arc_buf cached
 * clearing the callback may result in the arc_buf being destroyed.  However,
 * it will not result in the *last* arc_buf being destroyed, hence the data
 * will remain cached in the ARC. We make a copy of the arc buffer here so
 * that we can process the callback without holding any locks.
 *
 * It's possible that the callback is already in the process of being cleared
 * by another thread.  In this case we can not clear the callback.
 *
 * Returns B_TRUE if the callback was successfully called and cleared.
 */
boolean_t
arc_clear_callback(arc_buf_t *buf)
{
	arc_buf_hdr_t *hdr;
	kmutex_t *hash_lock;
	arc_evict_func_t *efunc = buf->b_efunc;
	void *private = buf->b_private;

	mutex_enter(&buf->b_evict_lock);
	hdr = buf->b_hdr;
	if (hdr == NULL) {
		/*
		 * We are in arc_do_user_evicts().
		 */
		ASSERT(buf->b_data == NULL);
		mutex_exit(&buf->b_evict_lock);
		return (B_FALSE);
	} else if (buf->b_data == NULL) {
		/*
		 * We are on the eviction list; process this buffer now
		 * but let arc_do_user_evicts() do the reaping.
		 */
		buf->b_efunc = NULL;
		mutex_exit(&buf->b_evict_lock);
		VERIFY0(efunc(private));
		return (B_TRUE);
	}
	hash_lock = HDR_LOCK(hdr);
	mutex_enter(hash_lock);
	hdr = buf->b_hdr;
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));

	ASSERT3U(refcount_count(&hdr->b_l1hdr.b_refcnt), <,
	    hdr->b_l1hdr.b_datacnt);
	ASSERT(hdr->b_l1hdr.b_state == arc_mru ||
	    hdr->b_l1hdr.b_state == arc_mfu);

	buf->b_efunc = NULL;
	buf->b_private = NULL;

	if (hdr->b_l1hdr.b_datacnt > 1) {
		mutex_exit(&buf->b_evict_lock);
		arc_buf_destroy(buf, TRUE);
	} else {
		ASSERT(buf == hdr->b_l1hdr.b_buf);
		hdr->b_flags |= ARC_FLAG_BUF_AVAILABLE;
		mutex_exit(&buf->b_evict_lock);
	}

	mutex_exit(hash_lock);
	VERIFY0(efunc(private));
	return (B_TRUE);
}

/*
 * Release this buffer from the cache, making it an anonymous buffer.  This
 * must be done after a read and prior to modifying the buffer contents.
 * If the buffer has more than one reference, we must make
 * a new hdr for the buffer.
 */
void
arc_release(arc_buf_t *buf, void *tag)
{
	kmutex_t *hash_lock;
	arc_state_t *state;
	arc_buf_hdr_t *hdr = buf->b_hdr;

	/*
	 * It would be nice to assert that if its DMU metadata (level >
	 * 0 || it's the dnode file), then it must be syncing context.
	 * But we don't know that information at this level.
	 */

	mutex_enter(&buf->b_evict_lock);

	ASSERT(HDR_HAS_L1HDR(hdr));

	/*
	 * We don't grab the hash lock prior to this check, because if
	 * the buffer's header is in the arc_anon state, it won't be
	 * linked into the hash table.
	 */
	if (hdr->b_l1hdr.b_state == arc_anon) {
		mutex_exit(&buf->b_evict_lock);
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		ASSERT(!HDR_IN_HASH_TABLE(hdr));
		ASSERT(!HDR_HAS_L2HDR(hdr));
		ASSERT(BUF_EMPTY(hdr));

		ASSERT3U(hdr->b_l1hdr.b_datacnt, ==, 1);
		ASSERT3S(refcount_count(&hdr->b_l1hdr.b_refcnt), ==, 1);
		ASSERT(!list_link_active(&hdr->b_l1hdr.b_arc_node));

		ASSERT3P(buf->b_efunc, ==, NULL);
		ASSERT3P(buf->b_private, ==, NULL);

		hdr->b_l1hdr.b_arc_access = 0;
		arc_buf_thaw(buf);

		return;
	}

	hash_lock = HDR_LOCK(hdr);
	mutex_enter(hash_lock);

	/*
	 * This assignment is only valid as long as the hash_lock is
	 * held, we must be careful not to reference state or the
	 * b_state field after dropping the lock.
	 */
	state = hdr->b_l1hdr.b_state;
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));
	ASSERT3P(state, !=, arc_anon);

	/* this buffer is not on any list */
	ASSERT(refcount_count(&hdr->b_l1hdr.b_refcnt) > 0);

	if (HDR_HAS_L2HDR(hdr)) {
		mutex_enter(&hdr->b_l2hdr.b_dev->l2ad_mtx);

		/*
		 * We have to recheck this conditional again now that
		 * we're holding the l2ad_mtx to prevent a race with
		 * another thread which might be concurrently calling
		 * l2arc_evict(). In that case, l2arc_evict() might have
		 * destroyed the header's L2 portion as we were waiting
		 * to acquire the l2ad_mtx.
		 */
		if (HDR_HAS_L2HDR(hdr))
			arc_hdr_l2hdr_destroy(hdr);

		mutex_exit(&hdr->b_l2hdr.b_dev->l2ad_mtx);
	}

	/*
	 * Do we have more than one buf?
	 */
	if (hdr->b_l1hdr.b_datacnt > 1) {
		arc_buf_hdr_t *nhdr;
		arc_buf_t **bufp;
		uint64_t blksz = hdr->b_size;
		uint64_t spa = hdr->b_spa;
		arc_buf_contents_t type = arc_buf_type(hdr);
		uint32_t flags = hdr->b_flags;

		ASSERT(hdr->b_l1hdr.b_buf != buf || buf->b_next != NULL);
		/*
		 * Pull the data off of this hdr and attach it to
		 * a new anonymous hdr.
		 */
		(void) remove_reference(hdr, hash_lock, tag);
		bufp = &hdr->b_l1hdr.b_buf;
		while (*bufp != buf)
			bufp = &(*bufp)->b_next;
		*bufp = buf->b_next;
		buf->b_next = NULL;

		ASSERT3P(state, !=, arc_l2c_only);

		(void) refcount_remove_many(
		    &state->arcs_size, hdr->b_size, buf);

		if (refcount_is_zero(&hdr->b_l1hdr.b_refcnt)) {
			uint64_t *size;

			ASSERT3P(state, !=, arc_l2c_only);
			size = &state->arcs_lsize[type];
			ASSERT3U(*size, >=, hdr->b_size);
			atomic_add_64(size, -hdr->b_size);
		}

		/*
		 * We're releasing a duplicate user data buffer, update
		 * our statistics accordingly.
		 */
		if (HDR_ISTYPE_DATA(hdr)) {
			ARCSTAT_BUMPDOWN(arcstat_duplicate_buffers);
			ARCSTAT_INCR(arcstat_duplicate_buffers_size,
			    -hdr->b_size);
		}
		hdr->b_l1hdr.b_datacnt -= 1;
		arc_cksum_verify(buf);
		arc_buf_unwatch(buf);

		mutex_exit(hash_lock);

		nhdr = kmem_cache_alloc(hdr_full_cache, KM_PUSHPAGE);
		nhdr->b_size = blksz;
		nhdr->b_spa = spa;

		nhdr->b_l1hdr.b_mru_hits = 0;
		nhdr->b_l1hdr.b_mru_ghost_hits = 0;
		nhdr->b_l1hdr.b_mfu_hits = 0;
		nhdr->b_l1hdr.b_mfu_ghost_hits = 0;
		nhdr->b_l1hdr.b_l2_hits = 0;
		nhdr->b_flags = flags & ARC_FLAG_L2_WRITING;
		nhdr->b_flags |= arc_bufc_to_flags(type);
		nhdr->b_flags |= ARC_FLAG_HAS_L1HDR;

		nhdr->b_l1hdr.b_buf = buf;
		nhdr->b_l1hdr.b_datacnt = 1;
		nhdr->b_l1hdr.b_state = arc_anon;
		nhdr->b_l1hdr.b_arc_access = 0;
		nhdr->b_l1hdr.b_tmp_cdata = NULL;
		nhdr->b_freeze_cksum = NULL;

		(void) refcount_add(&nhdr->b_l1hdr.b_refcnt, tag);
		buf->b_hdr = nhdr;
		mutex_exit(&buf->b_evict_lock);
		(void) refcount_add_many(&arc_anon->arcs_size, blksz, buf);
	} else {
		mutex_exit(&buf->b_evict_lock);
		ASSERT(refcount_count(&hdr->b_l1hdr.b_refcnt) == 1);
		/* protected by hash lock, or hdr is on arc_anon */
		ASSERT(!multilist_link_active(&hdr->b_l1hdr.b_arc_node));
		ASSERT(!HDR_IO_IN_PROGRESS(hdr));
		hdr->b_l1hdr.b_mru_hits = 0;
		hdr->b_l1hdr.b_mru_ghost_hits = 0;
		hdr->b_l1hdr.b_mfu_hits = 0;
		hdr->b_l1hdr.b_mfu_ghost_hits = 0;
		hdr->b_l1hdr.b_l2_hits = 0;
		arc_change_state(arc_anon, hdr, hash_lock);
		hdr->b_l1hdr.b_arc_access = 0;
		mutex_exit(hash_lock);

		buf_discard_identity(hdr);
		arc_buf_thaw(buf);
	}
	buf->b_efunc = NULL;
	buf->b_private = NULL;
}

int
arc_released(arc_buf_t *buf)
{
	int released;

	mutex_enter(&buf->b_evict_lock);
	released = (buf->b_data != NULL &&
	    buf->b_hdr->b_l1hdr.b_state == arc_anon);
	mutex_exit(&buf->b_evict_lock);
	return (released);
}

#ifdef ZFS_DEBUG
int
arc_referenced(arc_buf_t *buf)
{
	int referenced;

	mutex_enter(&buf->b_evict_lock);
	referenced = (refcount_count(&buf->b_hdr->b_l1hdr.b_refcnt));
	mutex_exit(&buf->b_evict_lock);
	return (referenced);
}
#endif

static void
arc_write_ready(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;
	arc_buf_t *buf = callback->awcb_buf;
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(!refcount_is_zero(&buf->b_hdr->b_l1hdr.b_refcnt));
	ASSERT(hdr->b_l1hdr.b_datacnt > 0);
	callback->awcb_ready(zio, buf, callback->awcb_private);

	/*
	 * If the IO is already in progress, then this is a re-write
	 * attempt, so we need to thaw and re-compute the cksum.
	 * It is the responsibility of the callback to handle the
	 * accounting for any re-write attempt.
	 */
	if (HDR_IO_IN_PROGRESS(hdr)) {
		mutex_enter(&hdr->b_l1hdr.b_freeze_lock);
		if (hdr->b_freeze_cksum != NULL) {
			kmem_free(hdr->b_freeze_cksum, sizeof (zio_cksum_t));
			hdr->b_freeze_cksum = NULL;
		}
		mutex_exit(&hdr->b_l1hdr.b_freeze_lock);
	}
	arc_cksum_compute(buf, B_FALSE);
	hdr->b_flags |= ARC_FLAG_IO_IN_PROGRESS;
}

/*
 * The SPA calls this callback for each physical write that happens on behalf
 * of a logical write.  See the comment in dbuf_write_physdone() for details.
 */
static void
arc_write_physdone(zio_t *zio)
{
	arc_write_callback_t *cb = zio->io_private;
	if (cb->awcb_physdone != NULL)
		cb->awcb_physdone(zio, cb->awcb_buf, cb->awcb_private);
}

static void
arc_write_done(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;
	arc_buf_t *buf = callback->awcb_buf;
	arc_buf_hdr_t *hdr = buf->b_hdr;

	ASSERT(hdr->b_l1hdr.b_acb == NULL);

	if (zio->io_error == 0) {
		if (BP_IS_HOLE(zio->io_bp) || BP_IS_EMBEDDED(zio->io_bp)) {
			buf_discard_identity(hdr);
		} else {
			hdr->b_dva = *BP_IDENTITY(zio->io_bp);
			hdr->b_birth = BP_PHYSICAL_BIRTH(zio->io_bp);
		}
	} else {
		ASSERT(BUF_EMPTY(hdr));
	}

	/*
	 * If the block to be written was all-zero or compressed enough to be
	 * embedded in the BP, no write was performed so there will be no
	 * dva/birth/checksum.  The buffer must therefore remain anonymous
	 * (and uncached).
	 */
	if (!BUF_EMPTY(hdr)) {
		arc_buf_hdr_t *exists;
		kmutex_t *hash_lock;

		ASSERT(zio->io_error == 0);

		arc_cksum_verify(buf);

		exists = buf_hash_insert(hdr, &hash_lock);
		if (exists != NULL) {
			/*
			 * This can only happen if we overwrite for
			 * sync-to-convergence, because we remove
			 * buffers from the hash table when we arc_free().
			 */
			if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
				if (!BP_EQUAL(&zio->io_bp_orig, zio->io_bp))
					panic("bad overwrite, hdr=%p exists=%p",
					    (void *)hdr, (void *)exists);
				ASSERT(refcount_is_zero(
				    &exists->b_l1hdr.b_refcnt));
				arc_change_state(arc_anon, exists, hash_lock);
				mutex_exit(hash_lock);
				arc_hdr_destroy(exists);
				exists = buf_hash_insert(hdr, &hash_lock);
				ASSERT3P(exists, ==, NULL);
			} else if (zio->io_flags & ZIO_FLAG_NOPWRITE) {
				/* nopwrite */
				ASSERT(zio->io_prop.zp_nopwrite);
				if (!BP_EQUAL(&zio->io_bp_orig, zio->io_bp))
					panic("bad nopwrite, hdr=%p exists=%p",
					    (void *)hdr, (void *)exists);
			} else {
				/* Dedup */
				ASSERT(hdr->b_l1hdr.b_datacnt == 1);
				ASSERT(hdr->b_l1hdr.b_state == arc_anon);
				ASSERT(BP_GET_DEDUP(zio->io_bp));
				ASSERT(BP_GET_LEVEL(zio->io_bp) == 0);
			}
		}
		hdr->b_flags &= ~ARC_FLAG_IO_IN_PROGRESS;
		/* if it's not anon, we are doing a scrub */
		if (exists == NULL && hdr->b_l1hdr.b_state == arc_anon)
			arc_access(hdr, hash_lock);
		mutex_exit(hash_lock);
	} else {
		hdr->b_flags &= ~ARC_FLAG_IO_IN_PROGRESS;
	}

	ASSERT(!refcount_is_zero(&hdr->b_l1hdr.b_refcnt));
	callback->awcb_done(zio, buf, callback->awcb_private);

	kmem_free(callback, sizeof (arc_write_callback_t));
}

zio_t *
arc_write(zio_t *pio, spa_t *spa, uint64_t txg,
    blkptr_t *bp, arc_buf_t *buf, boolean_t l2arc, boolean_t l2arc_compress,
    const zio_prop_t *zp, arc_done_func_t *ready, arc_done_func_t *physdone,
    arc_done_func_t *done, void *private, zio_priority_t priority,
    int zio_flags, const zbookmark_phys_t *zb)
{
	arc_buf_hdr_t *hdr = buf->b_hdr;
	arc_write_callback_t *callback;
	zio_t *zio;

	ASSERT(ready != NULL);
	ASSERT(done != NULL);
	ASSERT(!HDR_IO_ERROR(hdr));
	ASSERT(!HDR_IO_IN_PROGRESS(hdr));
	ASSERT(hdr->b_l1hdr.b_acb == NULL);
	ASSERT(hdr->b_l1hdr.b_datacnt > 0);
	if (l2arc)
		hdr->b_flags |= ARC_FLAG_L2CACHE;
	if (l2arc_compress)
		hdr->b_flags |= ARC_FLAG_L2COMPRESS;
	callback = kmem_zalloc(sizeof (arc_write_callback_t), KM_SLEEP);
	callback->awcb_ready = ready;
	callback->awcb_physdone = physdone;
	callback->awcb_done = done;
	callback->awcb_private = private;
	callback->awcb_buf = buf;

	zio = zio_write(pio, spa, txg, bp, buf->b_data, hdr->b_size, zp,
	    arc_write_ready, arc_write_physdone, arc_write_done, callback,
	    priority, zio_flags, zb);

	return (zio);
}

static int
arc_memory_throttle(uint64_t reserve, uint64_t txg)
{
#ifdef _KERNEL
	uint64_t available_memory = ptob(freemem);
	static uint64_t page_load = 0;
	static uint64_t last_txg = 0;
#ifdef __linux__
	pgcnt_t minfree = btop(arc_sys_free / 4);
#endif

	if (freemem > physmem * arc_lotsfree_percent / 100)
		return (0);

	if (txg > last_txg) {
		last_txg = txg;
		page_load = 0;
	}

	/*
	 * If we are in pageout, we know that memory is already tight,
	 * the arc is already going to be evicting, so we just want to
	 * continue to let page writes occur as quickly as possible.
	 */
	if (current_is_kswapd()) {
		if (page_load > MAX(ptob(minfree), available_memory) / 4) {
			DMU_TX_STAT_BUMP(dmu_tx_memory_reclaim);
			return (SET_ERROR(ERESTART));
		}
		/* Note: reserve is inflated, so we deflate */
		page_load += reserve / 8;
		return (0);
	} else if (page_load > 0 && arc_reclaim_needed()) {
		/* memory is low, delay before restarting */
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		DMU_TX_STAT_BUMP(dmu_tx_memory_reclaim);
		return (SET_ERROR(EAGAIN));
	}
	page_load = 0;
#endif
	return (0);
}

void
arc_tempreserve_clear(uint64_t reserve)
{
	atomic_add_64(&arc_tempreserve, -reserve);
	ASSERT((int64_t)arc_tempreserve >= 0);
}

int
arc_tempreserve_space(uint64_t reserve, uint64_t txg)
{
	int error;
	uint64_t anon_size;

	if (!arc_no_grow &&
	    reserve > arc_c/4 &&
	    reserve * 4 > (2ULL << SPA_MAXBLOCKSHIFT))
		arc_c = MIN(arc_c_max, reserve * 4);

	/*
	 * Throttle when the calculated memory footprint for the TXG
	 * exceeds the target ARC size.
	 */
	if (reserve > arc_c) {
		DMU_TX_STAT_BUMP(dmu_tx_memory_reserve);
		return (SET_ERROR(ERESTART));
	}

	/*
	 * Don't count loaned bufs as in flight dirty data to prevent long
	 * network delays from blocking transactions that are ready to be
	 * assigned to a txg.
	 */
	anon_size = MAX((int64_t)(refcount_count(&arc_anon->arcs_size) -
	    arc_loaned_bytes), 0);

	/*
	 * Writes will, almost always, require additional memory allocations
	 * in order to compress/encrypt/etc the data.  We therefore need to
	 * make sure that there is sufficient available memory for this.
	 */
	error = arc_memory_throttle(reserve, txg);
	if (error != 0)
		return (error);

	/*
	 * Throttle writes when the amount of dirty data in the cache
	 * gets too large.  We try to keep the cache less than half full
	 * of dirty blocks so that our sync times don't grow too large.
	 * Note: if two requests come in concurrently, we might let them
	 * both succeed, when one of them should fail.  Not a huge deal.
	 */

	if (reserve + arc_tempreserve + anon_size > arc_c / 2 &&
	    anon_size > arc_c / 4) {
		dprintf("failing, arc_tempreserve=%lluK anon_meta=%lluK "
		    "anon_data=%lluK tempreserve=%lluK arc_c=%lluK\n",
		    arc_tempreserve>>10,
		    arc_anon->arcs_lsize[ARC_BUFC_METADATA]>>10,
		    arc_anon->arcs_lsize[ARC_BUFC_DATA]>>10,
		    reserve>>10, arc_c>>10);
		DMU_TX_STAT_BUMP(dmu_tx_dirty_throttle);
		return (SET_ERROR(ERESTART));
	}
	atomic_add_64(&arc_tempreserve, reserve);
	return (0);
}

static void
arc_kstat_update_state(arc_state_t *state, kstat_named_t *size,
    kstat_named_t *evict_data, kstat_named_t *evict_metadata)
{
	size->value.ui64 = refcount_count(&state->arcs_size);
	evict_data->value.ui64 = state->arcs_lsize[ARC_BUFC_DATA];
	evict_metadata->value.ui64 = state->arcs_lsize[ARC_BUFC_METADATA];
}

static int
arc_kstat_update(kstat_t *ksp, int rw)
{
	arc_stats_t *as = ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		return (EACCES);
	} else {
		arc_kstat_update_state(arc_anon,
		    &as->arcstat_anon_size,
		    &as->arcstat_anon_evictable_data,
		    &as->arcstat_anon_evictable_metadata);
		arc_kstat_update_state(arc_mru,
		    &as->arcstat_mru_size,
		    &as->arcstat_mru_evictable_data,
		    &as->arcstat_mru_evictable_metadata);
		arc_kstat_update_state(arc_mru_ghost,
		    &as->arcstat_mru_ghost_size,
		    &as->arcstat_mru_ghost_evictable_data,
		    &as->arcstat_mru_ghost_evictable_metadata);
		arc_kstat_update_state(arc_mfu,
		    &as->arcstat_mfu_size,
		    &as->arcstat_mfu_evictable_data,
		    &as->arcstat_mfu_evictable_metadata);
		arc_kstat_update_state(arc_mfu_ghost,
		    &as->arcstat_mfu_ghost_size,
		    &as->arcstat_mfu_ghost_evictable_data,
		    &as->arcstat_mfu_ghost_evictable_metadata);
	}

	return (0);
}

/*
 * This function *must* return indices evenly distributed between all
 * sublists of the multilist. This is needed due to how the ARC eviction
 * code is laid out; arc_evict_state() assumes ARC buffers are evenly
 * distributed between all sublists and uses this assumption when
 * deciding which sublist to evict from and how much to evict from it.
 */
unsigned int
arc_state_multilist_index_func(multilist_t *ml, void *obj)
{
	arc_buf_hdr_t *hdr = obj;

	/*
	 * We rely on b_dva to generate evenly distributed index
	 * numbers using buf_hash below. So, as an added precaution,
	 * let's make sure we never add empty buffers to the arc lists.
	 */
	ASSERT(!BUF_EMPTY(hdr));

	/*
	 * The assumption here, is the hash value for a given
	 * arc_buf_hdr_t will remain constant throughout its lifetime
	 * (i.e. its b_spa, b_dva, and b_birth fields don't change).
	 * Thus, we don't need to store the header's sublist index
	 * on insertion, as this index can be recalculated on removal.
	 *
	 * Also, the low order bits of the hash value are thought to be
	 * distributed evenly. Otherwise, in the case that the multilist
	 * has a power of two number of sublists, each sublists' usage
	 * would not be evenly distributed.
	 */
	return (buf_hash(hdr->b_spa, &hdr->b_dva, hdr->b_birth) %
	    multilist_get_num_sublists(ml));
}

/*
 * Called during module initialization and periodically thereafter to
 * apply reasonable changes to the exposed performance tunings.  Non-zero
 * zfs_* values which differ from the currently set values will be applied.
 */
static void
arc_tuning_update(void)
{
	/* Valid range: 64M - <all physical memory> */
	if ((zfs_arc_max) && (zfs_arc_max != arc_c_max) &&
	    (zfs_arc_max > 64 << 20) && (zfs_arc_max < ptob(physmem)) &&
	    (zfs_arc_max > arc_c_min)) {
		arc_c_max = zfs_arc_max;
		arc_c = arc_c_max;
		arc_p = (arc_c >> 1);
		arc_meta_limit = MIN(arc_meta_limit, (3 * arc_c_max) / 4);
	}

	/* Valid range: 32M - <arc_c_max> */
	if ((zfs_arc_min) && (zfs_arc_min != arc_c_min) &&
	    (zfs_arc_min >= 2ULL << SPA_MAXBLOCKSHIFT) &&
	    (zfs_arc_min <= arc_c_max)) {
		arc_c_min = zfs_arc_min;
		arc_c = MAX(arc_c, arc_c_min);
	}

	/* Valid range: 16M - <arc_c_max> */
	if ((zfs_arc_meta_min) && (zfs_arc_meta_min != arc_meta_min) &&
	    (zfs_arc_meta_min >= 1ULL << SPA_MAXBLOCKSHIFT) &&
	    (zfs_arc_meta_min <= arc_c_max)) {
		arc_meta_min = zfs_arc_meta_min;
		arc_meta_limit = MAX(arc_meta_limit, arc_meta_min);
	}

	/* Valid range: <arc_meta_min> - <arc_c_max> */
	if ((zfs_arc_meta_limit) && (zfs_arc_meta_limit != arc_meta_limit) &&
	    (zfs_arc_meta_limit >= zfs_arc_meta_min) &&
	    (zfs_arc_meta_limit <= arc_c_max))
		arc_meta_limit = zfs_arc_meta_limit;

	/* Valid range: 1 - N */
	if (zfs_arc_grow_retry)
		arc_grow_retry = zfs_arc_grow_retry;

	/* Valid range: 1 - N */
	if (zfs_arc_shrink_shift) {
		arc_shrink_shift = zfs_arc_shrink_shift;
		arc_no_grow_shift = MIN(arc_no_grow_shift, arc_shrink_shift -1);
	}

	/* Valid range: 1 - N */
	if (zfs_arc_p_min_shift)
		arc_p_min_shift = zfs_arc_p_min_shift;

	/* Valid range: 1 - N ticks */
	if (zfs_arc_min_prefetch_lifespan)
		arc_min_prefetch_lifespan = zfs_arc_min_prefetch_lifespan;

	/* Valid range: 0 - 100 */
	if ((zfs_arc_lotsfree_percent >= 0) &&
	    (zfs_arc_lotsfree_percent <= 100))
		arc_lotsfree_percent = zfs_arc_lotsfree_percent;

	/* Valid range: 0 - <all physical memory> */
	if ((zfs_arc_sys_free) && (zfs_arc_sys_free != arc_sys_free))
		arc_sys_free = MIN(MAX(zfs_arc_sys_free, 0), ptob(physmem));

}

void
arc_init(void)
{
	/*
	 * allmem is "all memory that we could possibly use".
	 */
#ifdef _KERNEL
	uint64_t allmem = ptob(physmem);
#else
	uint64_t allmem = (physmem * PAGESIZE) / 2;
#endif

	mutex_init(&arc_reclaim_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&arc_reclaim_thread_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&arc_reclaim_waiters_cv, NULL, CV_DEFAULT, NULL);

	mutex_init(&arc_user_evicts_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&arc_user_evicts_cv, NULL, CV_DEFAULT, NULL);

	/* Convert seconds to clock ticks */
	arc_min_prefetch_lifespan = 1 * hz;

	/* Start out with 1/8 of all memory */
	arc_c = allmem / 8;

#ifdef _KERNEL
	/*
	 * On architectures where the physical memory can be larger
	 * than the addressable space (intel in 32-bit mode), we may
	 * need to limit the cache to 1/8 of VM size.
	 */
	arc_c = MIN(arc_c, vmem_size(heap_arena, VMEM_ALLOC | VMEM_FREE) / 8);

	/*
	 * Register a shrinker to support synchronous (direct) memory
	 * reclaim from the arc.  This is done to prevent kswapd from
	 * swapping out pages when it is preferable to shrink the arc.
	 */
	spl_register_shrinker(&arc_shrinker);

	/* Set to 1/64 of all memory or a minimum of 512K */
	arc_sys_free = MAX(ptob(physmem / 64), (512 * 1024));
	arc_need_free = 0;
#endif

	/* Set min cache to allow safe operation of arc_adapt() */
	arc_c_min = 2ULL << SPA_MAXBLOCKSHIFT;
	/* Set max to 1/2 of all memory */
	arc_c_max = allmem / 2;

	arc_c = arc_c_max;
	arc_p = (arc_c >> 1);

	/* Set min to 1/2 of arc_c_min */
	arc_meta_min = 1ULL << SPA_MAXBLOCKSHIFT;
	/* Initialize maximum observed usage to zero */
	arc_meta_max = 0;
	/* Set limit to 3/4 of arc_c_max with a floor of arc_meta_min */
	arc_meta_limit = MAX((3 * arc_c_max) / 4, arc_meta_min);

	/* Apply user specified tunings */
	arc_tuning_update();

	if (zfs_arc_num_sublists_per_state < 1)
		zfs_arc_num_sublists_per_state = MAX(boot_ncpus, 1);

	/* if kmem_flags are set, lets try to use less memory */
	if (kmem_debugging())
		arc_c = arc_c / 2;
	if (arc_c < arc_c_min)
		arc_c = arc_c_min;

	arc_anon = &ARC_anon;
	arc_mru = &ARC_mru;
	arc_mru_ghost = &ARC_mru_ghost;
	arc_mfu = &ARC_mfu;
	arc_mfu_ghost = &ARC_mfu_ghost;
	arc_l2c_only = &ARC_l2c_only;
	arc_size = 0;

	multilist_create(&arc_mru->arcs_list[ARC_BUFC_METADATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mru->arcs_list[ARC_BUFC_DATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mru_ghost->arcs_list[ARC_BUFC_METADATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mru_ghost->arcs_list[ARC_BUFC_DATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mfu->arcs_list[ARC_BUFC_METADATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mfu->arcs_list[ARC_BUFC_DATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mfu_ghost->arcs_list[ARC_BUFC_METADATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_mfu_ghost->arcs_list[ARC_BUFC_DATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_l2c_only->arcs_list[ARC_BUFC_METADATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);
	multilist_create(&arc_l2c_only->arcs_list[ARC_BUFC_DATA],
	    sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l1hdr.b_arc_node),
	    zfs_arc_num_sublists_per_state, arc_state_multilist_index_func);

	arc_anon->arcs_state = ARC_STATE_ANON;
	arc_mru->arcs_state = ARC_STATE_MRU;
	arc_mru_ghost->arcs_state = ARC_STATE_MRU_GHOST;
	arc_mfu->arcs_state = ARC_STATE_MFU;
	arc_mfu_ghost->arcs_state = ARC_STATE_MFU_GHOST;
	arc_l2c_only->arcs_state = ARC_STATE_L2C_ONLY;

	refcount_create(&arc_anon->arcs_size);
	refcount_create(&arc_mru->arcs_size);
	refcount_create(&arc_mru_ghost->arcs_size);
	refcount_create(&arc_mfu->arcs_size);
	refcount_create(&arc_mfu_ghost->arcs_size);
	refcount_create(&arc_l2c_only->arcs_size);

	buf_init();

	arc_reclaim_thread_exit = FALSE;
	arc_user_evicts_thread_exit = FALSE;
	list_create(&arc_prune_list, sizeof (arc_prune_t),
	    offsetof(arc_prune_t, p_node));
	arc_eviction_list = NULL;
	mutex_init(&arc_prune_mtx, NULL, MUTEX_DEFAULT, NULL);
	bzero(&arc_eviction_hdr, sizeof (arc_buf_hdr_t));

	arc_prune_taskq = taskq_create("arc_prune", max_ncpus, defclsyspri,
	    max_ncpus, INT_MAX, TASKQ_PREPOPULATE | TASKQ_DYNAMIC);

	arc_ksp = kstat_create("zfs", 0, "arcstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (arc_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);

	if (arc_ksp != NULL) {
		arc_ksp->ks_data = &arc_stats;
		arc_ksp->ks_update = arc_kstat_update;
		kstat_install(arc_ksp);
	}

	(void) thread_create(NULL, 0, arc_reclaim_thread, NULL, 0, &p0,
	    TS_RUN, defclsyspri);

	(void) thread_create(NULL, 0, arc_user_evicts_thread, NULL, 0, &p0,
	    TS_RUN, defclsyspri);

	arc_dead = FALSE;
	arc_warm = B_FALSE;

	/*
	 * Calculate maximum amount of dirty data per pool.
	 *
	 * If it has been set by a module parameter, take that.
	 * Otherwise, use a percentage of physical memory defined by
	 * zfs_dirty_data_max_percent (default 10%) with a cap at
	 * zfs_dirty_data_max_max (default 25% of physical memory).
	 */
	if (zfs_dirty_data_max_max == 0)
		zfs_dirty_data_max_max = (uint64_t)physmem * PAGESIZE *
		    zfs_dirty_data_max_max_percent / 100;

	if (zfs_dirty_data_max == 0) {
		zfs_dirty_data_max = (uint64_t)physmem * PAGESIZE *
		    zfs_dirty_data_max_percent / 100;
		zfs_dirty_data_max = MIN(zfs_dirty_data_max,
		    zfs_dirty_data_max_max);
	}
}

void
arc_fini(void)
{
	arc_prune_t *p;

#ifdef _KERNEL
	spl_unregister_shrinker(&arc_shrinker);
#endif /* _KERNEL */

	mutex_enter(&arc_reclaim_lock);
	arc_reclaim_thread_exit = TRUE;
	/*
	 * The reclaim thread will set arc_reclaim_thread_exit back to
	 * FALSE when it is finished exiting; we're waiting for that.
	 */
	while (arc_reclaim_thread_exit) {
		cv_signal(&arc_reclaim_thread_cv);
		cv_wait(&arc_reclaim_thread_cv, &arc_reclaim_lock);
	}
	mutex_exit(&arc_reclaim_lock);

	mutex_enter(&arc_user_evicts_lock);
	arc_user_evicts_thread_exit = TRUE;
	/*
	 * The user evicts thread will set arc_user_evicts_thread_exit
	 * to FALSE when it is finished exiting; we're waiting for that.
	 */
	while (arc_user_evicts_thread_exit) {
		cv_signal(&arc_user_evicts_cv);
		cv_wait(&arc_user_evicts_cv, &arc_user_evicts_lock);
	}
	mutex_exit(&arc_user_evicts_lock);

	/* Use TRUE to ensure *all* buffers are evicted */
	arc_flush(NULL, TRUE);

	arc_dead = TRUE;

	if (arc_ksp != NULL) {
		kstat_delete(arc_ksp);
		arc_ksp = NULL;
	}

	taskq_wait(arc_prune_taskq);
	taskq_destroy(arc_prune_taskq);

	mutex_enter(&arc_prune_mtx);
	while ((p = list_head(&arc_prune_list)) != NULL) {
		list_remove(&arc_prune_list, p);
		refcount_remove(&p->p_refcnt, &arc_prune_list);
		refcount_destroy(&p->p_refcnt);
		kmem_free(p, sizeof (*p));
	}
	mutex_exit(&arc_prune_mtx);

	list_destroy(&arc_prune_list);
	mutex_destroy(&arc_prune_mtx);
	mutex_destroy(&arc_reclaim_lock);
	cv_destroy(&arc_reclaim_thread_cv);
	cv_destroy(&arc_reclaim_waiters_cv);

	mutex_destroy(&arc_user_evicts_lock);
	cv_destroy(&arc_user_evicts_cv);

	refcount_destroy(&arc_anon->arcs_size);
	refcount_destroy(&arc_mru->arcs_size);
	refcount_destroy(&arc_mru_ghost->arcs_size);
	refcount_destroy(&arc_mfu->arcs_size);
	refcount_destroy(&arc_mfu_ghost->arcs_size);
	refcount_destroy(&arc_l2c_only->arcs_size);

	multilist_destroy(&arc_mru->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(&arc_mru_ghost->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(&arc_mfu->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(&arc_mfu_ghost->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(&arc_mru->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(&arc_mru_ghost->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(&arc_mfu->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(&arc_mfu_ghost->arcs_list[ARC_BUFC_DATA]);
	multilist_destroy(&arc_l2c_only->arcs_list[ARC_BUFC_METADATA]);
	multilist_destroy(&arc_l2c_only->arcs_list[ARC_BUFC_DATA]);

	buf_fini();

	ASSERT0(arc_loaned_bytes);
}

/*
 * Level 2 ARC
 *
 * The level 2 ARC (L2ARC) is a cache layer in-between main memory and disk.
 * It uses dedicated storage devices to hold cached data, which are populated
 * using large infrequent writes.  The main role of this cache is to boost
 * the performance of random read workloads.  The intended L2ARC devices
 * include short-stroked disks, solid state disks, and other media with
 * substantially faster read latency than disk.
 *
 *                 +-----------------------+
 *                 |         ARC           |
 *                 +-----------------------+
 *                    |         ^     ^
 *                    |         |     |
 *      l2arc_feed_thread()    arc_read()
 *                    |         |     |
 *                    |  l2arc read   |
 *                    V         |     |
 *               +---------------+    |
 *               |     L2ARC     |    |
 *               +---------------+    |
 *                   |    ^           |
 *          l2arc_write() |           |
 *                   |    |           |
 *                   V    |           |
 *                 +-------+      +-------+
 *                 | vdev  |      | vdev  |
 *                 | cache |      | cache |
 *                 +-------+      +-------+
 *                 +=========+     .-----.
 *                 :  L2ARC  :    |-_____-|
 *                 : devices :    | Disks |
 *                 +=========+    `-_____-'
 *
 * Read requests are satisfied from the following sources, in order:
 *
 *	1) ARC
 *	2) vdev cache of L2ARC devices
 *	3) L2ARC devices
 *	4) vdev cache of disks
 *	5) disks
 *
 * Some L2ARC device types exhibit extremely slow write performance.
 * To accommodate for this there are some significant differences between
 * the L2ARC and traditional cache design:
 *
 * 1. There is no eviction path from the ARC to the L2ARC.  Evictions from
 * the ARC behave as usual, freeing buffers and placing headers on ghost
 * lists.  The ARC does not send buffers to the L2ARC during eviction as
 * this would add inflated write latencies for all ARC memory pressure.
 *
 * 2. The L2ARC attempts to cache data from the ARC before it is evicted.
 * It does this by periodically scanning buffers from the eviction-end of
 * the MFU and MRU ARC lists, copying them to the L2ARC devices if they are
 * not already there. It scans until a headroom of buffers is satisfied,
 * which itself is a buffer for ARC eviction. If a compressible buffer is
 * found during scanning and selected for writing to an L2ARC device, we
 * temporarily boost scanning headroom during the next scan cycle to make
 * sure we adapt to compression effects (which might significantly reduce
 * the data volume we write to L2ARC). The thread that does this is
 * l2arc_feed_thread(), illustrated below; example sizes are included to
 * provide a better sense of ratio than this diagram:
 *
 *	       head -->                        tail
 *	        +---------------------+----------+
 *	ARC_mfu |:::::#:::::::::::::::|o#o###o###|-->.   # already on L2ARC
 *	        +---------------------+----------+   |   o L2ARC eligible
 *	ARC_mru |:#:::::::::::::::::::|#o#ooo####|-->|   : ARC buffer
 *	        +---------------------+----------+   |
 *	             15.9 Gbytes      ^ 32 Mbytes    |
 *	                           headroom          |
 *	                                      l2arc_feed_thread()
 *	                                             |
 *	                 l2arc write hand <--[oooo]--'
 *	                         |           8 Mbyte
 *	                         |          write max
 *	                         V
 *		  +==============================+
 *	L2ARC dev |####|#|###|###|    |####| ... |
 *	          +==============================+
 *	                     32 Gbytes
 *
 * 3. If an ARC buffer is copied to the L2ARC but then hit instead of
 * evicted, then the L2ARC has cached a buffer much sooner than it probably
 * needed to, potentially wasting L2ARC device bandwidth and storage.  It is
 * safe to say that this is an uncommon case, since buffers at the end of
 * the ARC lists have moved there due to inactivity.
 *
 * 4. If the ARC evicts faster than the L2ARC can maintain a headroom,
 * then the L2ARC simply misses copying some buffers.  This serves as a
 * pressure valve to prevent heavy read workloads from both stalling the ARC
 * with waits and clogging the L2ARC with writes.  This also helps prevent
 * the potential for the L2ARC to churn if it attempts to cache content too
 * quickly, such as during backups of the entire pool.
 *
 * 5. After system boot and before the ARC has filled main memory, there are
 * no evictions from the ARC and so the tails of the ARC_mfu and ARC_mru
 * lists can remain mostly static.  Instead of searching from tail of these
 * lists as pictured, the l2arc_feed_thread() will search from the list heads
 * for eligible buffers, greatly increasing its chance of finding them.
 *
 * The L2ARC device write speed is also boosted during this time so that
 * the L2ARC warms up faster.  Since there have been no ARC evictions yet,
 * there are no L2ARC reads, and no fear of degrading read performance
 * through increased writes.
 *
 * 6. Writes to the L2ARC devices are grouped and sent in-sequence, so that
 * the vdev queue can aggregate them into larger and fewer writes.  Each
 * device is written to in a rotor fashion, sweeping writes through
 * available space then repeating.
 *
 * 7. The L2ARC does not store dirty content.  It never needs to flush
 * write buffers back to disk based storage.
 *
 * 8. If an ARC buffer is written (and dirtied) which also exists in the
 * L2ARC, the now stale L2ARC buffer is immediately dropped.
 *
 * The performance of the L2ARC can be tweaked by a number of tunables, which
 * may be necessary for different workloads:
 *
 *	l2arc_write_max		max write bytes per interval
 *	l2arc_write_boost	extra write bytes during device warmup
 *	l2arc_noprefetch	skip caching prefetched buffers
 *	l2arc_nocompress	skip compressing buffers
 *	l2arc_headroom		number of max device writes to precache
 *	l2arc_headroom_boost	when we find compressed buffers during ARC
 *				scanning, we multiply headroom by this
 *				percentage factor for the next scan cycle,
 *				since more compressed buffers are likely to
 *				be present
 *	l2arc_feed_secs		seconds between L2ARC writing
 *
 * Tunables may be removed or added as future performance improvements are
 * integrated, and also may become zpool properties.
 *
 * There are three key functions that control how the L2ARC warms up:
 *
 *	l2arc_write_eligible()	check if a buffer is eligible to cache
 *	l2arc_write_size()	calculate how much to write
 *	l2arc_write_interval()	calculate sleep delay between writes
 *
 * These three functions determine what to write, how much, and how quickly
 * to send writes.
 */

static boolean_t
l2arc_write_eligible(uint64_t spa_guid, arc_buf_hdr_t *hdr)
{
	/*
	 * A buffer is *not* eligible for the L2ARC if it:
	 * 1. belongs to a different spa.
	 * 2. is already cached on the L2ARC.
	 * 3. has an I/O in progress (it may be an incomplete read).
	 * 4. is flagged not eligible (zfs property).
	 */
	if (hdr->b_spa != spa_guid || HDR_HAS_L2HDR(hdr) ||
	    HDR_IO_IN_PROGRESS(hdr) || !HDR_L2CACHE(hdr))
		return (B_FALSE);

	return (B_TRUE);
}

static uint64_t
l2arc_write_size(void)
{
	uint64_t size;

	/*
	 * Make sure our globals have meaningful values in case the user
	 * altered them.
	 */
	size = l2arc_write_max;
	if (size == 0) {
		cmn_err(CE_NOTE, "Bad value for l2arc_write_max, value must "
		    "be greater than zero, resetting it to the default (%d)",
		    L2ARC_WRITE_SIZE);
		size = l2arc_write_max = L2ARC_WRITE_SIZE;
	}

	if (arc_warm == B_FALSE)
		size += l2arc_write_boost;

	return (size);

}

static clock_t
l2arc_write_interval(clock_t began, uint64_t wanted, uint64_t wrote)
{
	clock_t interval, next, now;

	/*
	 * If the ARC lists are busy, increase our write rate; if the
	 * lists are stale, idle back.  This is achieved by checking
	 * how much we previously wrote - if it was more than half of
	 * what we wanted, schedule the next write much sooner.
	 */
	if (l2arc_feed_again && wrote > (wanted / 2))
		interval = (hz * l2arc_feed_min_ms) / 1000;
	else
		interval = hz * l2arc_feed_secs;

	now = ddi_get_lbolt();
	next = MAX(now, MIN(now + interval, began + interval));

	return (next);
}

/*
 * Cycle through L2ARC devices.  This is how L2ARC load balances.
 * If a device is returned, this also returns holding the spa config lock.
 */
static l2arc_dev_t *
l2arc_dev_get_next(void)
{
	l2arc_dev_t *first, *next = NULL;

	/*
	 * Lock out the removal of spas (spa_namespace_lock), then removal
	 * of cache devices (l2arc_dev_mtx).  Once a device has been selected,
	 * both locks will be dropped and a spa config lock held instead.
	 */
	mutex_enter(&spa_namespace_lock);
	mutex_enter(&l2arc_dev_mtx);

	/* if there are no vdevs, there is nothing to do */
	if (l2arc_ndev == 0)
		goto out;

	first = NULL;
	next = l2arc_dev_last;
	do {
		/* loop around the list looking for a non-faulted vdev */
		if (next == NULL) {
			next = list_head(l2arc_dev_list);
		} else {
			next = list_next(l2arc_dev_list, next);
			if (next == NULL)
				next = list_head(l2arc_dev_list);
		}

		/* if we have come back to the start, bail out */
		if (first == NULL)
			first = next;
		else if (next == first)
			break;

	} while (vdev_is_dead(next->l2ad_vdev));

	/* if we were unable to find any usable vdevs, return NULL */
	if (vdev_is_dead(next->l2ad_vdev))
		next = NULL;

	l2arc_dev_last = next;

out:
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Grab the config lock to prevent the 'next' device from being
	 * removed while we are writing to it.
	 */
	if (next != NULL)
		spa_config_enter(next->l2ad_spa, SCL_L2ARC, next, RW_READER);
	mutex_exit(&spa_namespace_lock);

	return (next);
}

/*
 * Free buffers that were tagged for destruction.
 */
static void
l2arc_do_free_on_write(void)
{
	list_t *buflist;
	l2arc_data_free_t *df, *df_prev;

	mutex_enter(&l2arc_free_on_write_mtx);
	buflist = l2arc_free_on_write;

	for (df = list_tail(buflist); df; df = df_prev) {
		df_prev = list_prev(buflist, df);
		ASSERT(df->l2df_data != NULL);
		ASSERT(df->l2df_func != NULL);
		df->l2df_func(df->l2df_data, df->l2df_size);
		list_remove(buflist, df);
		kmem_free(df, sizeof (l2arc_data_free_t));
	}

	mutex_exit(&l2arc_free_on_write_mtx);
}

/*
 * A write to a cache device has completed.  Update all headers to allow
 * reads from these buffers to begin.
 */
static void
l2arc_write_done(zio_t *zio)
{
	l2arc_write_callback_t *cb;
	l2arc_dev_t *dev;
	list_t *buflist;
	arc_buf_hdr_t *head, *hdr, *hdr_prev;
	kmutex_t *hash_lock;
	int64_t bytes_dropped = 0;

	cb = zio->io_private;
	ASSERT(cb != NULL);
	dev = cb->l2wcb_dev;
	ASSERT(dev != NULL);
	head = cb->l2wcb_head;
	ASSERT(head != NULL);
	buflist = &dev->l2ad_buflist;
	ASSERT(buflist != NULL);
	DTRACE_PROBE2(l2arc__iodone, zio_t *, zio,
	    l2arc_write_callback_t *, cb);

	if (zio->io_error != 0)
		ARCSTAT_BUMP(arcstat_l2_writes_error);

	/*
	 * All writes completed, or an error was hit.
	 */
top:
	mutex_enter(&dev->l2ad_mtx);
	for (hdr = list_prev(buflist, head); hdr; hdr = hdr_prev) {
		hdr_prev = list_prev(buflist, hdr);

		hash_lock = HDR_LOCK(hdr);

		/*
		 * We cannot use mutex_enter or else we can deadlock
		 * with l2arc_write_buffers (due to swapping the order
		 * the hash lock and l2ad_mtx are taken).
		 */
		if (!mutex_tryenter(hash_lock)) {
			/*
			 * Missed the hash lock. We must retry so we
			 * don't leave the ARC_FLAG_L2_WRITING bit set.
			 */
			ARCSTAT_BUMP(arcstat_l2_writes_lock_retry);

			/*
			 * We don't want to rescan the headers we've
			 * already marked as having been written out, so
			 * we reinsert the head node so we can pick up
			 * where we left off.
			 */
			list_remove(buflist, head);
			list_insert_after(buflist, hdr, head);

			mutex_exit(&dev->l2ad_mtx);

			/*
			 * We wait for the hash lock to become available
			 * to try and prevent busy waiting, and increase
			 * the chance we'll be able to acquire the lock
			 * the next time around.
			 */
			mutex_enter(hash_lock);
			mutex_exit(hash_lock);
			goto top;
		}

		/*
		 * We could not have been moved into the arc_l2c_only
		 * state while in-flight due to our ARC_FLAG_L2_WRITING
		 * bit being set. Let's just ensure that's being enforced.
		 */
		ASSERT(HDR_HAS_L1HDR(hdr));

		/*
		 * We may have allocated a buffer for L2ARC compression,
		 * we must release it to avoid leaking this data.
		 */
		l2arc_release_cdata_buf(hdr);

		if (zio->io_error != 0) {
			/*
			 * Error - drop L2ARC entry.
			 */
			list_remove(buflist, hdr);
			hdr->b_flags &= ~ARC_FLAG_HAS_L2HDR;

			ARCSTAT_INCR(arcstat_l2_asize, -hdr->b_l2hdr.b_asize);
			ARCSTAT_INCR(arcstat_l2_size, -hdr->b_size);

			bytes_dropped += hdr->b_l2hdr.b_asize;
			(void) refcount_remove_many(&dev->l2ad_alloc,
			    hdr->b_l2hdr.b_asize, hdr);
		}

		/*
		 * Allow ARC to begin reads and ghost list evictions to
		 * this L2ARC entry.
		 */
		hdr->b_flags &= ~ARC_FLAG_L2_WRITING;

		mutex_exit(hash_lock);
	}

	atomic_inc_64(&l2arc_writes_done);
	list_remove(buflist, head);
	ASSERT(!HDR_HAS_L1HDR(head));
	kmem_cache_free(hdr_l2only_cache, head);
	mutex_exit(&dev->l2ad_mtx);

	vdev_space_update(dev->l2ad_vdev, -bytes_dropped, 0, 0);

	l2arc_do_free_on_write();

	kmem_free(cb, sizeof (l2arc_write_callback_t));
}

/*
 * A read to a cache device completed.  Validate buffer contents before
 * handing over to the regular ARC routines.
 */
static void
l2arc_read_done(zio_t *zio)
{
	l2arc_read_callback_t *cb;
	arc_buf_hdr_t *hdr;
	arc_buf_t *buf;
	kmutex_t *hash_lock;
	int equal;

	ASSERT(zio->io_vd != NULL);
	ASSERT(zio->io_flags & ZIO_FLAG_DONT_PROPAGATE);

	spa_config_exit(zio->io_spa, SCL_L2ARC, zio->io_vd);

	cb = zio->io_private;
	ASSERT(cb != NULL);
	buf = cb->l2rcb_buf;
	ASSERT(buf != NULL);

	hash_lock = HDR_LOCK(buf->b_hdr);
	mutex_enter(hash_lock);
	hdr = buf->b_hdr;
	ASSERT3P(hash_lock, ==, HDR_LOCK(hdr));

	/*
	 * If the buffer was compressed, decompress it first.
	 */
	if (cb->l2rcb_compress != ZIO_COMPRESS_OFF)
		l2arc_decompress_zio(zio, hdr, cb->l2rcb_compress);
	ASSERT(zio->io_data != NULL);
	ASSERT3U(zio->io_size, ==, hdr->b_size);
	ASSERT3U(BP_GET_LSIZE(&cb->l2rcb_bp), ==, hdr->b_size);

	/*
	 * Check this survived the L2ARC journey.
	 */
	equal = arc_cksum_equal(buf);
	if (equal && zio->io_error == 0 && !HDR_L2_EVICTED(hdr)) {
		mutex_exit(hash_lock);
		zio->io_private = buf;
		zio->io_bp_copy = cb->l2rcb_bp;	/* XXX fix in L2ARC 2.0	*/
		zio->io_bp = &zio->io_bp_copy;	/* XXX fix in L2ARC 2.0	*/
		arc_read_done(zio);
	} else {
		mutex_exit(hash_lock);
		/*
		 * Buffer didn't survive caching.  Increment stats and
		 * reissue to the original storage device.
		 */
		if (zio->io_error != 0) {
			ARCSTAT_BUMP(arcstat_l2_io_error);
		} else {
			zio->io_error = SET_ERROR(EIO);
		}
		if (!equal)
			ARCSTAT_BUMP(arcstat_l2_cksum_bad);

		/*
		 * If there's no waiter, issue an async i/o to the primary
		 * storage now.  If there *is* a waiter, the caller must
		 * issue the i/o in a context where it's OK to block.
		 */
		if (zio->io_waiter == NULL) {
			zio_t *pio = zio_unique_parent(zio);

			ASSERT(!pio || pio->io_child_type == ZIO_CHILD_LOGICAL);

			zio_nowait(zio_read(pio, cb->l2rcb_spa, &cb->l2rcb_bp,
			    buf->b_data, hdr->b_size, arc_read_done, buf,
			    zio->io_priority, cb->l2rcb_flags, &cb->l2rcb_zb));
		}
	}

	kmem_free(cb, sizeof (l2arc_read_callback_t));
}

/*
 * This is the list priority from which the L2ARC will search for pages to
 * cache.  This is used within loops (0..3) to cycle through lists in the
 * desired order.  This order can have a significant effect on cache
 * performance.
 *
 * Currently the metadata lists are hit first, MFU then MRU, followed by
 * the data lists.  This function returns a locked list, and also returns
 * the lock pointer.
 */
static multilist_sublist_t *
l2arc_sublist_lock(int list_num)
{
	multilist_t *ml = NULL;
	unsigned int idx;

	ASSERT(list_num >= 0 && list_num <= 3);

	switch (list_num) {
	case 0:
		ml = &arc_mfu->arcs_list[ARC_BUFC_METADATA];
		break;
	case 1:
		ml = &arc_mru->arcs_list[ARC_BUFC_METADATA];
		break;
	case 2:
		ml = &arc_mfu->arcs_list[ARC_BUFC_DATA];
		break;
	case 3:
		ml = &arc_mru->arcs_list[ARC_BUFC_DATA];
		break;
	}

	/*
	 * Return a randomly-selected sublist. This is acceptable
	 * because the caller feeds only a little bit of data for each
	 * call (8MB). Subsequent calls will result in different
	 * sublists being selected.
	 */
	idx = multilist_get_random_index(ml);
	return (multilist_sublist_lock(ml, idx));
}

/*
 * Evict buffers from the device write hand to the distance specified in
 * bytes.  This distance may span populated buffers, it may span nothing.
 * This is clearing a region on the L2ARC device ready for writing.
 * If the 'all' boolean is set, every buffer is evicted.
 */
static void
l2arc_evict(l2arc_dev_t *dev, uint64_t distance, boolean_t all)
{
	list_t *buflist;
	arc_buf_hdr_t *hdr, *hdr_prev;
	kmutex_t *hash_lock;
	uint64_t taddr;

	buflist = &dev->l2ad_buflist;

	if (!all && dev->l2ad_first) {
		/*
		 * This is the first sweep through the device.  There is
		 * nothing to evict.
		 */
		return;
	}

	if (dev->l2ad_hand >= (dev->l2ad_end - (2 * distance))) {
		/*
		 * When nearing the end of the device, evict to the end
		 * before the device write hand jumps to the start.
		 */
		taddr = dev->l2ad_end;
	} else {
		taddr = dev->l2ad_hand + distance;
	}
	DTRACE_PROBE4(l2arc__evict, l2arc_dev_t *, dev, list_t *, buflist,
	    uint64_t, taddr, boolean_t, all);

top:
	mutex_enter(&dev->l2ad_mtx);
	for (hdr = list_tail(buflist); hdr; hdr = hdr_prev) {
		hdr_prev = list_prev(buflist, hdr);

		hash_lock = HDR_LOCK(hdr);

		/*
		 * We cannot use mutex_enter or else we can deadlock
		 * with l2arc_write_buffers (due to swapping the order
		 * the hash lock and l2ad_mtx are taken).
		 */
		if (!mutex_tryenter(hash_lock)) {
			/*
			 * Missed the hash lock.  Retry.
			 */
			ARCSTAT_BUMP(arcstat_l2_evict_lock_retry);
			mutex_exit(&dev->l2ad_mtx);
			mutex_enter(hash_lock);
			mutex_exit(hash_lock);
			goto top;
		}

		if (HDR_L2_WRITE_HEAD(hdr)) {
			/*
			 * We hit a write head node.  Leave it for
			 * l2arc_write_done().
			 */
			list_remove(buflist, hdr);
			mutex_exit(hash_lock);
			continue;
		}

		if (!all && HDR_HAS_L2HDR(hdr) &&
		    (hdr->b_l2hdr.b_daddr > taddr ||
		    hdr->b_l2hdr.b_daddr < dev->l2ad_hand)) {
			/*
			 * We've evicted to the target address,
			 * or the end of the device.
			 */
			mutex_exit(hash_lock);
			break;
		}

		ASSERT(HDR_HAS_L2HDR(hdr));
		if (!HDR_HAS_L1HDR(hdr)) {
			ASSERT(!HDR_L2_READING(hdr));
			/*
			 * This doesn't exist in the ARC.  Destroy.
			 * arc_hdr_destroy() will call list_remove()
			 * and decrement arcstat_l2_size.
			 */
			arc_change_state(arc_anon, hdr, hash_lock);
			arc_hdr_destroy(hdr);
		} else {
			ASSERT(hdr->b_l1hdr.b_state != arc_l2c_only);
			ARCSTAT_BUMP(arcstat_l2_evict_l1cached);
			/*
			 * Invalidate issued or about to be issued
			 * reads, since we may be about to write
			 * over this location.
			 */
			if (HDR_L2_READING(hdr)) {
				ARCSTAT_BUMP(arcstat_l2_evict_reading);
				hdr->b_flags |= ARC_FLAG_L2_EVICTED;
			}

			/* Ensure this header has finished being written */
			ASSERT(!HDR_L2_WRITING(hdr));
			ASSERT3P(hdr->b_l1hdr.b_tmp_cdata, ==, NULL);

			arc_hdr_l2hdr_destroy(hdr);
		}
		mutex_exit(hash_lock);
	}
	mutex_exit(&dev->l2ad_mtx);
}

/*
 * Find and write ARC buffers to the L2ARC device.
 *
 * An ARC_FLAG_L2_WRITING flag is set so that the L2ARC buffers are not valid
 * for reading until they have completed writing.
 * The headroom_boost is an in-out parameter used to maintain headroom boost
 * state between calls to this function.
 *
 * Returns the number of bytes actually written (which may be smaller than
 * the delta by which the device hand has changed due to alignment).
 */
static uint64_t
l2arc_write_buffers(spa_t *spa, l2arc_dev_t *dev, uint64_t target_sz,
    boolean_t *headroom_boost)
{
	arc_buf_hdr_t *hdr, *hdr_prev, *head;
	uint64_t write_asize, write_sz, headroom, buf_compress_minsz,
	    stats_size;
	void *buf_data;
	boolean_t full;
	l2arc_write_callback_t *cb;
	zio_t *pio, *wzio;
	uint64_t guid = spa_load_guid(spa);
	int try;
	const boolean_t do_headroom_boost = *headroom_boost;

	ASSERT(dev->l2ad_vdev != NULL);

	/* Lower the flag now, we might want to raise it again later. */
	*headroom_boost = B_FALSE;

	pio = NULL;
	write_sz = write_asize = 0;
	full = B_FALSE;
	head = kmem_cache_alloc(hdr_l2only_cache, KM_PUSHPAGE);
	head->b_flags |= ARC_FLAG_L2_WRITE_HEAD;
	head->b_flags |= ARC_FLAG_HAS_L2HDR;

	/*
	 * We will want to try to compress buffers that are at least 2x the
	 * device sector size.
	 */
	buf_compress_minsz = 2 << dev->l2ad_vdev->vdev_ashift;

	/*
	 * Copy buffers for L2ARC writing.
	 */
	for (try = 0; try <= 3; try++) {
		multilist_sublist_t *mls = l2arc_sublist_lock(try);
		uint64_t passed_sz = 0;

		/*
		 * L2ARC fast warmup.
		 *
		 * Until the ARC is warm and starts to evict, read from the
		 * head of the ARC lists rather than the tail.
		 */
		if (arc_warm == B_FALSE)
			hdr = multilist_sublist_head(mls);
		else
			hdr = multilist_sublist_tail(mls);

		headroom = target_sz * l2arc_headroom;
		if (do_headroom_boost)
			headroom = (headroom * l2arc_headroom_boost) / 100;

		for (; hdr; hdr = hdr_prev) {
			kmutex_t *hash_lock;
			uint64_t buf_sz;
			uint64_t buf_a_sz;

			if (arc_warm == B_FALSE)
				hdr_prev = multilist_sublist_next(mls, hdr);
			else
				hdr_prev = multilist_sublist_prev(mls, hdr);

			hash_lock = HDR_LOCK(hdr);
			if (!mutex_tryenter(hash_lock)) {
				/*
				 * Skip this buffer rather than waiting.
				 */
				continue;
			}

			passed_sz += hdr->b_size;
			if (passed_sz > headroom) {
				/*
				 * Searched too far.
				 */
				mutex_exit(hash_lock);
				break;
			}

			if (!l2arc_write_eligible(guid, hdr)) {
				mutex_exit(hash_lock);
				continue;
			}

			/*
			 * Assume that the buffer is not going to be compressed
			 * and could take more space on disk because of a larger
			 * disk block size.
			 */
			buf_sz = hdr->b_size;
			buf_a_sz = vdev_psize_to_asize(dev->l2ad_vdev, buf_sz);

			if ((write_asize + buf_a_sz) > target_sz) {
				full = B_TRUE;
				mutex_exit(hash_lock);
				break;
			}

			if (pio == NULL) {
				/*
				 * Insert a dummy header on the buflist so
				 * l2arc_write_done() can find where the
				 * write buffers begin without searching.
				 */
				mutex_enter(&dev->l2ad_mtx);
				list_insert_head(&dev->l2ad_buflist, head);
				mutex_exit(&dev->l2ad_mtx);

				cb = kmem_alloc(
				    sizeof (l2arc_write_callback_t), KM_SLEEP);
				cb->l2wcb_dev = dev;
				cb->l2wcb_head = head;
				pio = zio_root(spa, l2arc_write_done, cb,
				    ZIO_FLAG_CANFAIL);
			}

			/*
			 * Create and add a new L2ARC header.
			 */
			hdr->b_l2hdr.b_dev = dev;
			hdr->b_flags |= ARC_FLAG_L2_WRITING;
			/*
			 * Temporarily stash the data buffer in b_tmp_cdata.
			 * The subsequent write step will pick it up from
			 * there. This is because can't access b_l1hdr.b_buf
			 * without holding the hash_lock, which we in turn
			 * can't access without holding the ARC list locks
			 * (which we want to avoid during compression/writing)
			 */
			hdr->b_l2hdr.b_compress = ZIO_COMPRESS_OFF;
			hdr->b_l2hdr.b_asize = hdr->b_size;
			hdr->b_l2hdr.b_hits = 0;
			hdr->b_l1hdr.b_tmp_cdata = hdr->b_l1hdr.b_buf->b_data;

			/*
			 * Explicitly set the b_daddr field to a known
			 * value which means "invalid address". This
			 * enables us to differentiate which stage of
			 * l2arc_write_buffers() the particular header
			 * is in (e.g. this loop, or the one below).
			 * ARC_FLAG_L2_WRITING is not enough to make
			 * this distinction, and we need to know in
			 * order to do proper l2arc vdev accounting in
			 * arc_release() and arc_hdr_destroy().
			 *
			 * Note, we can't use a new flag to distinguish
			 * the two stages because we don't hold the
			 * header's hash_lock below, in the second stage
			 * of this function. Thus, we can't simply
			 * change the b_flags field to denote that the
			 * IO has been sent. We can change the b_daddr
			 * field of the L2 portion, though, since we'll
			 * be holding the l2ad_mtx; which is why we're
			 * using it to denote the header's state change.
			 */
			hdr->b_l2hdr.b_daddr = L2ARC_ADDR_UNSET;
			hdr->b_flags |= ARC_FLAG_HAS_L2HDR;

			mutex_enter(&dev->l2ad_mtx);
			list_insert_head(&dev->l2ad_buflist, hdr);
			mutex_exit(&dev->l2ad_mtx);

			/*
			 * Compute and store the buffer cksum before
			 * writing.  On debug the cksum is verified first.
			 */
			arc_cksum_verify(hdr->b_l1hdr.b_buf);
			arc_cksum_compute(hdr->b_l1hdr.b_buf, B_TRUE);

			mutex_exit(hash_lock);

			write_sz += buf_sz;
			write_asize += buf_a_sz;
		}

		multilist_sublist_unlock(mls);

		if (full == B_TRUE)
			break;
	}

	/* No buffers selected for writing? */
	if (pio == NULL) {
		ASSERT0(write_sz);
		ASSERT(!HDR_HAS_L1HDR(head));
		kmem_cache_free(hdr_l2only_cache, head);
		return (0);
	}

	mutex_enter(&dev->l2ad_mtx);

	/*
	 * Note that elsewhere in this file arcstat_l2_asize
	 * and the used space on l2ad_vdev are updated using b_asize,
	 * which is not necessarily rounded up to the device block size.
	 * Too keep accounting consistent we do the same here as well:
	 * stats_size accumulates the sum of b_asize of the written buffers,
	 * while write_asize accumulates the sum of b_asize rounded up
	 * to the device block size.
	 * The latter sum is used only to validate the corectness of the code.
	 */
	stats_size = 0;
	write_asize = 0;

	/*
	 * Now start writing the buffers. We're starting at the write head
	 * and work backwards, retracing the course of the buffer selector
	 * loop above.
	 */
	for (hdr = list_prev(&dev->l2ad_buflist, head); hdr;
	    hdr = list_prev(&dev->l2ad_buflist, hdr)) {
		uint64_t buf_sz;

		/*
		 * We rely on the L1 portion of the header below, so
		 * it's invalid for this header to have been evicted out
		 * of the ghost cache, prior to being written out. The
		 * ARC_FLAG_L2_WRITING bit ensures this won't happen.
		 */
		ASSERT(HDR_HAS_L1HDR(hdr));

		/*
		 * We shouldn't need to lock the buffer here, since we flagged
		 * it as ARC_FLAG_L2_WRITING in the previous step, but we must
		 * take care to only access its L2 cache parameters. In
		 * particular, hdr->l1hdr.b_buf may be invalid by now due to
		 * ARC eviction.
		 */
		hdr->b_l2hdr.b_daddr = dev->l2ad_hand;

		if ((!l2arc_nocompress && HDR_L2COMPRESS(hdr)) &&
		    hdr->b_l2hdr.b_asize >= buf_compress_minsz) {
			if (l2arc_compress_buf(hdr)) {
				/*
				 * If compression succeeded, enable headroom
				 * boost on the next scan cycle.
				 */
				*headroom_boost = B_TRUE;
			}
		}

		/*
		 * Pick up the buffer data we had previously stashed away
		 * (and now potentially also compressed).
		 */
		buf_data = hdr->b_l1hdr.b_tmp_cdata;
		buf_sz = hdr->b_l2hdr.b_asize;

		/*
		 * We need to do this regardless if buf_sz is zero or
		 * not, otherwise, when this l2hdr is evicted we'll
		 * remove a reference that was never added.
		 */
		(void) refcount_add_many(&dev->l2ad_alloc, buf_sz, hdr);

		/* Compression may have squashed the buffer to zero length. */
		if (buf_sz != 0) {
			uint64_t buf_a_sz;

			wzio = zio_write_phys(pio, dev->l2ad_vdev,
			    dev->l2ad_hand, buf_sz, buf_data, ZIO_CHECKSUM_OFF,
			    NULL, NULL, ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_CANFAIL, B_FALSE);

			DTRACE_PROBE2(l2arc__write, vdev_t *, dev->l2ad_vdev,
			    zio_t *, wzio);
			(void) zio_nowait(wzio);

			stats_size += buf_sz;

			/*
			 * Keep the clock hand suitably device-aligned.
			 */
			buf_a_sz = vdev_psize_to_asize(dev->l2ad_vdev, buf_sz);
			write_asize += buf_a_sz;
			dev->l2ad_hand += buf_a_sz;
		}
	}

	mutex_exit(&dev->l2ad_mtx);

	ASSERT3U(write_asize, <=, target_sz);
	ARCSTAT_BUMP(arcstat_l2_writes_sent);
	ARCSTAT_INCR(arcstat_l2_write_bytes, write_asize);
	ARCSTAT_INCR(arcstat_l2_size, write_sz);
	ARCSTAT_INCR(arcstat_l2_asize, stats_size);
	vdev_space_update(dev->l2ad_vdev, stats_size, 0, 0);

	/*
	 * Bump device hand to the device start if it is approaching the end.
	 * l2arc_evict() will already have evicted ahead for this case.
	 */
	if (dev->l2ad_hand >= (dev->l2ad_end - target_sz)) {
		dev->l2ad_hand = dev->l2ad_start;
		dev->l2ad_first = B_FALSE;
	}

	dev->l2ad_writing = B_TRUE;
	(void) zio_wait(pio);
	dev->l2ad_writing = B_FALSE;

	return (write_asize);
}

/*
 * Compresses an L2ARC buffer.
 * The data to be compressed must be prefilled in l1hdr.b_tmp_cdata and its
 * size in l2hdr->b_asize. This routine tries to compress the data and
 * depending on the compression result there are three possible outcomes:
 * *) The buffer was incompressible. The original l2hdr contents were left
 *    untouched and are ready for writing to an L2 device.
 * *) The buffer was all-zeros, so there is no need to write it to an L2
 *    device. To indicate this situation b_tmp_cdata is NULL'ed, b_asize is
 *    set to zero and b_compress is set to ZIO_COMPRESS_EMPTY.
 * *) Compression succeeded and b_tmp_cdata was replaced with a temporary
 *    data buffer which holds the compressed data to be written, and b_asize
 *    tells us how much data there is. b_compress is set to the appropriate
 *    compression algorithm. Once writing is done, invoke
 *    l2arc_release_cdata_buf on this l2hdr to free this temporary buffer.
 *
 * Returns B_TRUE if compression succeeded, or B_FALSE if it didn't (the
 * buffer was incompressible).
 */
static boolean_t
l2arc_compress_buf(arc_buf_hdr_t *hdr)
{
	void *cdata;
	size_t csize, len, rounded;
	l2arc_buf_hdr_t *l2hdr;

	ASSERT(HDR_HAS_L2HDR(hdr));

	l2hdr = &hdr->b_l2hdr;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT3U(l2hdr->b_compress, ==, ZIO_COMPRESS_OFF);
	ASSERT(hdr->b_l1hdr.b_tmp_cdata != NULL);

	len = l2hdr->b_asize;
	cdata = zio_data_buf_alloc(len);
	ASSERT3P(cdata, !=, NULL);
	csize = zio_compress_data(ZIO_COMPRESS_LZ4, hdr->b_l1hdr.b_tmp_cdata,
	    cdata, l2hdr->b_asize);

	rounded = P2ROUNDUP(csize, (size_t)SPA_MINBLOCKSIZE);
	if (rounded > csize) {
		bzero((char *)cdata + csize, rounded - csize);
		csize = rounded;
	}

	if (csize == 0) {
		/* zero block, indicate that there's nothing to write */
		zio_data_buf_free(cdata, len);
		l2hdr->b_compress = ZIO_COMPRESS_EMPTY;
		l2hdr->b_asize = 0;
		hdr->b_l1hdr.b_tmp_cdata = NULL;
		ARCSTAT_BUMP(arcstat_l2_compress_zeros);
		return (B_TRUE);
	} else if (csize > 0 && csize < len) {
		/*
		 * Compression succeeded, we'll keep the cdata around for
		 * writing and release it afterwards.
		 */
		l2hdr->b_compress = ZIO_COMPRESS_LZ4;
		l2hdr->b_asize = csize;
		hdr->b_l1hdr.b_tmp_cdata = cdata;
		ARCSTAT_BUMP(arcstat_l2_compress_successes);
		return (B_TRUE);
	} else {
		/*
		 * Compression failed, release the compressed buffer.
		 * l2hdr will be left unmodified.
		 */
		zio_data_buf_free(cdata, len);
		ARCSTAT_BUMP(arcstat_l2_compress_failures);
		return (B_FALSE);
	}
}

/*
 * Decompresses a zio read back from an l2arc device. On success, the
 * underlying zio's io_data buffer is overwritten by the uncompressed
 * version. On decompression error (corrupt compressed stream), the
 * zio->io_error value is set to signal an I/O error.
 *
 * Please note that the compressed data stream is not checksummed, so
 * if the underlying device is experiencing data corruption, we may feed
 * corrupt data to the decompressor, so the decompressor needs to be
 * able to handle this situation (LZ4 does).
 */
static void
l2arc_decompress_zio(zio_t *zio, arc_buf_hdr_t *hdr, enum zio_compress c)
{
	uint64_t csize;
	void *cdata;

	ASSERT(L2ARC_IS_VALID_COMPRESS(c));

	if (zio->io_error != 0) {
		/*
		 * An io error has occured, just restore the original io
		 * size in preparation for a main pool read.
		 */
		zio->io_orig_size = zio->io_size = hdr->b_size;
		return;
	}

	if (c == ZIO_COMPRESS_EMPTY) {
		/*
		 * An empty buffer results in a null zio, which means we
		 * need to fill its io_data after we're done restoring the
		 * buffer's contents.
		 */
		ASSERT(hdr->b_l1hdr.b_buf != NULL);
		bzero(hdr->b_l1hdr.b_buf->b_data, hdr->b_size);
		zio->io_data = zio->io_orig_data = hdr->b_l1hdr.b_buf->b_data;
	} else {
		ASSERT(zio->io_data != NULL);
		/*
		 * We copy the compressed data from the start of the arc buffer
		 * (the zio_read will have pulled in only what we need, the
		 * rest is garbage which we will overwrite at decompression)
		 * and then decompress back to the ARC data buffer. This way we
		 * can minimize copying by simply decompressing back over the
		 * original compressed data (rather than decompressing to an
		 * aux buffer and then copying back the uncompressed buffer,
		 * which is likely to be much larger).
		 */
		csize = zio->io_size;
		cdata = zio_data_buf_alloc(csize);
		bcopy(zio->io_data, cdata, csize);
		if (zio_decompress_data(c, cdata, zio->io_data, csize,
		    hdr->b_size) != 0)
			zio->io_error = EIO;
		zio_data_buf_free(cdata, csize);
	}

	/* Restore the expected uncompressed IO size. */
	zio->io_orig_size = zio->io_size = hdr->b_size;
}

/*
 * Releases the temporary b_tmp_cdata buffer in an l2arc header structure.
 * This buffer serves as a temporary holder of compressed data while
 * the buffer entry is being written to an l2arc device. Once that is
 * done, we can dispose of it.
 */
static void
l2arc_release_cdata_buf(arc_buf_hdr_t *hdr)
{
	enum zio_compress comp;

	ASSERT(HDR_HAS_L1HDR(hdr));
	ASSERT(HDR_HAS_L2HDR(hdr));
	comp = hdr->b_l2hdr.b_compress;
	ASSERT(comp == ZIO_COMPRESS_OFF || L2ARC_IS_VALID_COMPRESS(comp));

	if (comp == ZIO_COMPRESS_OFF) {
		/*
		 * In this case, b_tmp_cdata points to the same buffer
		 * as the arc_buf_t's b_data field. We don't want to
		 * free it, since the arc_buf_t will handle that.
		 */
		hdr->b_l1hdr.b_tmp_cdata = NULL;
	} else if (comp == ZIO_COMPRESS_EMPTY) {
		/*
		 * In this case, b_tmp_cdata was compressed to an empty
		 * buffer, thus there's nothing to free and b_tmp_cdata
		 * should have been set to NULL in l2arc_write_buffers().
		 */
		ASSERT3P(hdr->b_l1hdr.b_tmp_cdata, ==, NULL);
	} else {
		/*
		 * If the data was compressed, then we've allocated a
		 * temporary buffer for it, so now we need to release it.
		 */
		ASSERT(hdr->b_l1hdr.b_tmp_cdata != NULL);
		zio_data_buf_free(hdr->b_l1hdr.b_tmp_cdata,
		    hdr->b_size);
		hdr->b_l1hdr.b_tmp_cdata = NULL;
	}

}

/*
 * This thread feeds the L2ARC at regular intervals.  This is the beating
 * heart of the L2ARC.
 */
static void
l2arc_feed_thread(void)
{
	callb_cpr_t cpr;
	l2arc_dev_t *dev;
	spa_t *spa;
	uint64_t size, wrote;
	clock_t begin, next = ddi_get_lbolt();
	boolean_t headroom_boost = B_FALSE;
	fstrans_cookie_t cookie;

	CALLB_CPR_INIT(&cpr, &l2arc_feed_thr_lock, callb_generic_cpr, FTAG);

	mutex_enter(&l2arc_feed_thr_lock);

	cookie = spl_fstrans_mark();
	while (l2arc_thread_exit == 0) {
		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait_sig(&l2arc_feed_thr_cv,
		    &l2arc_feed_thr_lock, next);
		CALLB_CPR_SAFE_END(&cpr, &l2arc_feed_thr_lock);
		next = ddi_get_lbolt() + hz;

		/*
		 * Quick check for L2ARC devices.
		 */
		mutex_enter(&l2arc_dev_mtx);
		if (l2arc_ndev == 0) {
			mutex_exit(&l2arc_dev_mtx);
			continue;
		}
		mutex_exit(&l2arc_dev_mtx);
		begin = ddi_get_lbolt();

		/*
		 * This selects the next l2arc device to write to, and in
		 * doing so the next spa to feed from: dev->l2ad_spa.   This
		 * will return NULL if there are now no l2arc devices or if
		 * they are all faulted.
		 *
		 * If a device is returned, its spa's config lock is also
		 * held to prevent device removal.  l2arc_dev_get_next()
		 * will grab and release l2arc_dev_mtx.
		 */
		if ((dev = l2arc_dev_get_next()) == NULL)
			continue;

		spa = dev->l2ad_spa;
		ASSERT(spa != NULL);

		/*
		 * If the pool is read-only then force the feed thread to
		 * sleep a little longer.
		 */
		if (!spa_writeable(spa)) {
			next = ddi_get_lbolt() + 5 * l2arc_feed_secs * hz;
			spa_config_exit(spa, SCL_L2ARC, dev);
			continue;
		}

		/*
		 * Avoid contributing to memory pressure.
		 */
		if (arc_reclaim_needed()) {
			ARCSTAT_BUMP(arcstat_l2_abort_lowmem);
			spa_config_exit(spa, SCL_L2ARC, dev);
			continue;
		}

		ARCSTAT_BUMP(arcstat_l2_feeds);

		size = l2arc_write_size();

		/*
		 * Evict L2ARC buffers that will be overwritten.
		 */
		l2arc_evict(dev, size, B_FALSE);

		/*
		 * Write ARC buffers.
		 */
		wrote = l2arc_write_buffers(spa, dev, size, &headroom_boost);

		/*
		 * Calculate interval between writes.
		 */
		next = l2arc_write_interval(begin, size, wrote);
		spa_config_exit(spa, SCL_L2ARC, dev);
	}
	spl_fstrans_unmark(cookie);

	l2arc_thread_exit = 0;
	cv_broadcast(&l2arc_feed_thr_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops l2arc_feed_thr_lock */
	thread_exit();
}

boolean_t
l2arc_vdev_present(vdev_t *vd)
{
	l2arc_dev_t *dev;

	mutex_enter(&l2arc_dev_mtx);
	for (dev = list_head(l2arc_dev_list); dev != NULL;
	    dev = list_next(l2arc_dev_list, dev)) {
		if (dev->l2ad_vdev == vd)
			break;
	}
	mutex_exit(&l2arc_dev_mtx);

	return (dev != NULL);
}

/*
 * Add a vdev for use by the L2ARC.  By this point the spa has already
 * validated the vdev and opened it.
 */
void
l2arc_add_vdev(spa_t *spa, vdev_t *vd)
{
	l2arc_dev_t *adddev;

	ASSERT(!l2arc_vdev_present(vd));

	/*
	 * Create a new l2arc device entry.
	 */
	adddev = kmem_zalloc(sizeof (l2arc_dev_t), KM_SLEEP);
	adddev->l2ad_spa = spa;
	adddev->l2ad_vdev = vd;
	adddev->l2ad_start = VDEV_LABEL_START_SIZE;
	adddev->l2ad_end = VDEV_LABEL_START_SIZE + vdev_get_min_asize(vd);
	adddev->l2ad_hand = adddev->l2ad_start;
	adddev->l2ad_first = B_TRUE;
	adddev->l2ad_writing = B_FALSE;
	list_link_init(&adddev->l2ad_node);

	mutex_init(&adddev->l2ad_mtx, NULL, MUTEX_DEFAULT, NULL);
	/*
	 * This is a list of all ARC buffers that are still valid on the
	 * device.
	 */
	list_create(&adddev->l2ad_buflist, sizeof (arc_buf_hdr_t),
	    offsetof(arc_buf_hdr_t, b_l2hdr.b_l2node));

	vdev_space_update(vd, 0, 0, adddev->l2ad_end - adddev->l2ad_hand);
	refcount_create(&adddev->l2ad_alloc);

	/*
	 * Add device to global list
	 */
	mutex_enter(&l2arc_dev_mtx);
	list_insert_head(l2arc_dev_list, adddev);
	atomic_inc_64(&l2arc_ndev);
	mutex_exit(&l2arc_dev_mtx);
}

/*
 * Remove a vdev from the L2ARC.
 */
void
l2arc_remove_vdev(vdev_t *vd)
{
	l2arc_dev_t *dev, *nextdev, *remdev = NULL;

	/*
	 * Find the device by vdev
	 */
	mutex_enter(&l2arc_dev_mtx);
	for (dev = list_head(l2arc_dev_list); dev; dev = nextdev) {
		nextdev = list_next(l2arc_dev_list, dev);
		if (vd == dev->l2ad_vdev) {
			remdev = dev;
			break;
		}
	}
	ASSERT(remdev != NULL);

	/*
	 * Remove device from global list
	 */
	list_remove(l2arc_dev_list, remdev);
	l2arc_dev_last = NULL;		/* may have been invalidated */
	atomic_dec_64(&l2arc_ndev);
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Clear all buflists and ARC references.  L2ARC device flush.
	 */
	l2arc_evict(remdev, 0, B_TRUE);
	list_destroy(&remdev->l2ad_buflist);
	mutex_destroy(&remdev->l2ad_mtx);
	refcount_destroy(&remdev->l2ad_alloc);
	kmem_free(remdev, sizeof (l2arc_dev_t));
}

void
l2arc_init(void)
{
	l2arc_thread_exit = 0;
	l2arc_ndev = 0;
	l2arc_writes_sent = 0;
	l2arc_writes_done = 0;

	mutex_init(&l2arc_feed_thr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&l2arc_feed_thr_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&l2arc_dev_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&l2arc_free_on_write_mtx, NULL, MUTEX_DEFAULT, NULL);

	l2arc_dev_list = &L2ARC_dev_list;
	l2arc_free_on_write = &L2ARC_free_on_write;
	list_create(l2arc_dev_list, sizeof (l2arc_dev_t),
	    offsetof(l2arc_dev_t, l2ad_node));
	list_create(l2arc_free_on_write, sizeof (l2arc_data_free_t),
	    offsetof(l2arc_data_free_t, l2df_list_node));
}

void
l2arc_fini(void)
{
	/*
	 * This is called from dmu_fini(), which is called from spa_fini();
	 * Because of this, we can assume that all l2arc devices have
	 * already been removed when the pools themselves were removed.
	 */

	l2arc_do_free_on_write();

	mutex_destroy(&l2arc_feed_thr_lock);
	cv_destroy(&l2arc_feed_thr_cv);
	mutex_destroy(&l2arc_dev_mtx);
	mutex_destroy(&l2arc_free_on_write_mtx);

	list_destroy(l2arc_dev_list);
	list_destroy(l2arc_free_on_write);
}

void
l2arc_start(void)
{
	if (!(spa_mode_global & FWRITE))
		return;

	(void) thread_create(NULL, 0, l2arc_feed_thread, NULL, 0, &p0,
	    TS_RUN, defclsyspri);
}

void
l2arc_stop(void)
{
	if (!(spa_mode_global & FWRITE))
		return;

	mutex_enter(&l2arc_feed_thr_lock);
	cv_signal(&l2arc_feed_thr_cv);	/* kick thread out of startup */
	l2arc_thread_exit = 1;
	while (l2arc_thread_exit != 0)
		cv_wait(&l2arc_feed_thr_cv, &l2arc_feed_thr_lock);
	mutex_exit(&l2arc_feed_thr_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(arc_buf_size);
EXPORT_SYMBOL(arc_write);
EXPORT_SYMBOL(arc_read);
EXPORT_SYMBOL(arc_buf_remove_ref);
EXPORT_SYMBOL(arc_buf_info);
EXPORT_SYMBOL(arc_getbuf_func);
EXPORT_SYMBOL(arc_add_prune_callback);
EXPORT_SYMBOL(arc_remove_prune_callback);

module_param(zfs_arc_min, ulong, 0644);
MODULE_PARM_DESC(zfs_arc_min, "Min arc size");

module_param(zfs_arc_max, ulong, 0644);
MODULE_PARM_DESC(zfs_arc_max, "Max arc size");

module_param(zfs_arc_meta_limit, ulong, 0644);
MODULE_PARM_DESC(zfs_arc_meta_limit, "Meta limit for arc size");

module_param(zfs_arc_meta_min, ulong, 0644);
MODULE_PARM_DESC(zfs_arc_meta_min, "Min arc metadata");

module_param(zfs_arc_meta_prune, int, 0644);
MODULE_PARM_DESC(zfs_arc_meta_prune, "Meta objects to scan for prune");

module_param(zfs_arc_meta_adjust_restarts, int, 0644);
MODULE_PARM_DESC(zfs_arc_meta_adjust_restarts,
	"Limit number of restarts in arc_adjust_meta");

module_param(zfs_arc_meta_strategy, int, 0644);
MODULE_PARM_DESC(zfs_arc_meta_strategy, "Meta reclaim strategy");

module_param(zfs_arc_grow_retry, int, 0644);
MODULE_PARM_DESC(zfs_arc_grow_retry, "Seconds before growing arc size");

module_param(zfs_arc_p_aggressive_disable, int, 0644);
MODULE_PARM_DESC(zfs_arc_p_aggressive_disable, "disable aggressive arc_p grow");

module_param(zfs_arc_p_dampener_disable, int, 0644);
MODULE_PARM_DESC(zfs_arc_p_dampener_disable, "disable arc_p adapt dampener");

module_param(zfs_arc_shrink_shift, int, 0644);
MODULE_PARM_DESC(zfs_arc_shrink_shift, "log2(fraction of arc to reclaim)");

module_param(zfs_arc_p_min_shift, int, 0644);
MODULE_PARM_DESC(zfs_arc_p_min_shift, "arc_c shift to calc min/max arc_p");

module_param(zfs_disable_dup_eviction, int, 0644);
MODULE_PARM_DESC(zfs_disable_dup_eviction, "disable duplicate buffer eviction");

module_param(zfs_arc_average_blocksize, int, 0444);
MODULE_PARM_DESC(zfs_arc_average_blocksize, "Target average block size");

module_param(zfs_arc_min_prefetch_lifespan, int, 0644);
MODULE_PARM_DESC(zfs_arc_min_prefetch_lifespan, "Min life of prefetch block");

module_param(zfs_arc_num_sublists_per_state, int, 0644);
MODULE_PARM_DESC(zfs_arc_num_sublists_per_state,
	"Number of sublists used in each of the ARC state lists");

module_param(l2arc_write_max, ulong, 0644);
MODULE_PARM_DESC(l2arc_write_max, "Max write bytes per interval");

module_param(l2arc_write_boost, ulong, 0644);
MODULE_PARM_DESC(l2arc_write_boost, "Extra write bytes during device warmup");

module_param(l2arc_headroom, ulong, 0644);
MODULE_PARM_DESC(l2arc_headroom, "Number of max device writes to precache");

module_param(l2arc_headroom_boost, ulong, 0644);
MODULE_PARM_DESC(l2arc_headroom_boost, "Compressed l2arc_headroom multiplier");

module_param(l2arc_feed_secs, ulong, 0644);
MODULE_PARM_DESC(l2arc_feed_secs, "Seconds between L2ARC writing");

module_param(l2arc_feed_min_ms, ulong, 0644);
MODULE_PARM_DESC(l2arc_feed_min_ms, "Min feed interval in milliseconds");

module_param(l2arc_noprefetch, int, 0644);
MODULE_PARM_DESC(l2arc_noprefetch, "Skip caching prefetched buffers");

module_param(l2arc_nocompress, int, 0644);
MODULE_PARM_DESC(l2arc_nocompress, "Skip compressing L2ARC buffers");

module_param(l2arc_feed_again, int, 0644);
MODULE_PARM_DESC(l2arc_feed_again, "Turbo L2ARC warmup");

module_param(l2arc_norw, int, 0644);
MODULE_PARM_DESC(l2arc_norw, "No reads during writes");

module_param(zfs_arc_lotsfree_percent, int, 0644);
MODULE_PARM_DESC(zfs_arc_lotsfree_percent,
	"System free memory I/O throttle in bytes");

module_param(zfs_arc_sys_free, ulong, 0644);
MODULE_PARM_DESC(zfs_arc_sys_free, "System free memory target size in bytes");

#endif
