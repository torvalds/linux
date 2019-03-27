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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_send.h>
#include <sys/dmu_impl.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_tx.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_zfetch.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/zfeature.h>
#include <sys/blkptr.h>
#include <sys/range_tree.h>
#include <sys/callb.h>
#include <sys/abd.h>
#include <sys/vdev.h>
#include <sys/cityhash.h>
#include <sys/spa_impl.h>

kstat_t *dbuf_ksp;

typedef struct dbuf_stats {
	/*
	 * Various statistics about the size of the dbuf cache.
	 */
	kstat_named_t cache_count;
	kstat_named_t cache_size_bytes;
	kstat_named_t cache_size_bytes_max;
	/*
	 * Statistics regarding the bounds on the dbuf cache size.
	 */
	kstat_named_t cache_target_bytes;
	kstat_named_t cache_lowater_bytes;
	kstat_named_t cache_hiwater_bytes;
	/*
	 * Total number of dbuf cache evictions that have occurred.
	 */
	kstat_named_t cache_total_evicts;
	/*
	 * The distribution of dbuf levels in the dbuf cache and
	 * the total size of all dbufs at each level.
	 */
	kstat_named_t cache_levels[DN_MAX_LEVELS];
	kstat_named_t cache_levels_bytes[DN_MAX_LEVELS];
	/*
	 * Statistics about the dbuf hash table.
	 */
	kstat_named_t hash_hits;
	kstat_named_t hash_misses;
	kstat_named_t hash_collisions;
	kstat_named_t hash_elements;
	kstat_named_t hash_elements_max;
	/*
	 * Number of sublists containing more than one dbuf in the dbuf
	 * hash table. Keep track of the longest hash chain.
	 */
	kstat_named_t hash_chains;
	kstat_named_t hash_chain_max;
	/*
	 * Number of times a dbuf_create() discovers that a dbuf was
	 * already created and in the dbuf hash table.
	 */
	kstat_named_t hash_insert_race;
	/*
	 * Statistics about the size of the metadata dbuf cache.
	 */
	kstat_named_t metadata_cache_count;
	kstat_named_t metadata_cache_size_bytes;
	kstat_named_t metadata_cache_size_bytes_max;
	/*
	 * For diagnostic purposes, this is incremented whenever we can't add
	 * something to the metadata cache because it's full, and instead put
	 * the data in the regular dbuf cache.
	 */
	kstat_named_t metadata_cache_overflow;
} dbuf_stats_t;

dbuf_stats_t dbuf_stats = {
	{ "cache_count",			KSTAT_DATA_UINT64 },
	{ "cache_size_bytes",			KSTAT_DATA_UINT64 },
	{ "cache_size_bytes_max",		KSTAT_DATA_UINT64 },
	{ "cache_target_bytes",			KSTAT_DATA_UINT64 },
	{ "cache_lowater_bytes",		KSTAT_DATA_UINT64 },
	{ "cache_hiwater_bytes",		KSTAT_DATA_UINT64 },
	{ "cache_total_evicts",			KSTAT_DATA_UINT64 },
	{ { "cache_levels_N",			KSTAT_DATA_UINT64 } },
	{ { "cache_levels_bytes_N",		KSTAT_DATA_UINT64 } },
	{ "hash_hits",				KSTAT_DATA_UINT64 },
	{ "hash_misses",			KSTAT_DATA_UINT64 },
	{ "hash_collisions",			KSTAT_DATA_UINT64 },
	{ "hash_elements",			KSTAT_DATA_UINT64 },
	{ "hash_elements_max",			KSTAT_DATA_UINT64 },
	{ "hash_chains",			KSTAT_DATA_UINT64 },
	{ "hash_chain_max",			KSTAT_DATA_UINT64 },
	{ "hash_insert_race",			KSTAT_DATA_UINT64 },
	{ "metadata_cache_count",		KSTAT_DATA_UINT64 },
	{ "metadata_cache_size_bytes",		KSTAT_DATA_UINT64 },
	{ "metadata_cache_size_bytes_max",	KSTAT_DATA_UINT64 },
	{ "metadata_cache_overflow",		KSTAT_DATA_UINT64 }
};

#define	DBUF_STAT_INCR(stat, val)	\
	atomic_add_64(&dbuf_stats.stat.value.ui64, (val));
#define	DBUF_STAT_DECR(stat, val)	\
	DBUF_STAT_INCR(stat, -(val));
#define	DBUF_STAT_BUMP(stat)		\
	DBUF_STAT_INCR(stat, 1);
#define	DBUF_STAT_BUMPDOWN(stat)	\
	DBUF_STAT_INCR(stat, -1);
#define	DBUF_STAT_MAX(stat, v) {					\
	uint64_t _m;							\
	while ((v) > (_m = dbuf_stats.stat.value.ui64) &&		\
	    (_m != atomic_cas_64(&dbuf_stats.stat.value.ui64, _m, (v))))\
		continue;						\
}

struct dbuf_hold_impl_data {
	/* Function arguments */
	dnode_t *dh_dn;
	uint8_t dh_level;
	uint64_t dh_blkid;
	boolean_t dh_fail_sparse;
	boolean_t dh_fail_uncached;
	void *dh_tag;
	dmu_buf_impl_t **dh_dbp;
	/* Local variables */
	dmu_buf_impl_t *dh_db;
	dmu_buf_impl_t *dh_parent;
	blkptr_t *dh_bp;
	int dh_err;
	dbuf_dirty_record_t *dh_dr;
	int dh_depth;
};

static void __dbuf_hold_impl_init(struct dbuf_hold_impl_data *dh,
    dnode_t *dn, uint8_t level, uint64_t blkid, boolean_t fail_sparse,
	boolean_t fail_uncached,
	void *tag, dmu_buf_impl_t **dbp, int depth);
static int __dbuf_hold_impl(struct dbuf_hold_impl_data *dh);

static boolean_t dbuf_undirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
static void dbuf_write(dbuf_dirty_record_t *dr, arc_buf_t *data, dmu_tx_t *tx);

#ifndef __lint
extern inline void dmu_buf_init_user(dmu_buf_user_t *dbu,
    dmu_buf_evict_func_t *evict_func_sync,
    dmu_buf_evict_func_t *evict_func_async,
    dmu_buf_t **clear_on_evict_dbufp);
#endif /* ! __lint */

/*
 * Global data structures and functions for the dbuf cache.
 */
static kmem_cache_t *dbuf_kmem_cache;
static taskq_t *dbu_evict_taskq;

static kthread_t *dbuf_cache_evict_thread;
static kmutex_t dbuf_evict_lock;
static kcondvar_t dbuf_evict_cv;
static boolean_t dbuf_evict_thread_exit;

/*
 * There are two dbuf caches; each dbuf can only be in one of them at a time.
 *
 * 1. Cache of metadata dbufs, to help make read-heavy administrative commands
 *    from /sbin/zfs run faster. The "metadata cache" specifically stores dbufs
 *    that represent the metadata that describes filesystems/snapshots/
 *    bookmarks/properties/etc. We only evict from this cache when we export a
 *    pool, to short-circuit as much I/O as possible for all administrative
 *    commands that need the metadata. There is no eviction policy for this
 *    cache, because we try to only include types in it which would occupy a
 *    very small amount of space per object but create a large impact on the
 *    performance of these commands. Instead, after it reaches a maximum size
 *    (which should only happen on very small memory systems with a very large
 *    number of filesystem objects), we stop taking new dbufs into the
 *    metadata cache, instead putting them in the normal dbuf cache.
 *
 * 2. LRU cache of dbufs. The dbuf cache maintains a list of dbufs that
 *    are not currently held but have been recently released. These dbufs
 *    are not eligible for arc eviction until they are aged out of the cache.
 *    Dbufs that are aged out of the cache will be immediately destroyed and
 *    become eligible for arc eviction.
 *
 * Dbufs are added to these caches once the last hold is released. If a dbuf is
 * later accessed and still exists in the dbuf cache, then it will be removed
 * from the cache and later re-added to the head of the cache.
 *
 * If a given dbuf meets the requirements for the metadata cache, it will go
 * there, otherwise it will be considered for the generic LRU dbuf cache. The
 * caches and the refcounts tracking their sizes are stored in an array indexed
 * by those caches' matching enum values (from dbuf_cached_state_t).
 */
typedef struct dbuf_cache {
	multilist_t *cache;
	refcount_t size;
} dbuf_cache_t;
dbuf_cache_t dbuf_caches[DB_CACHE_MAX];

/* Size limits for the caches */
uint64_t dbuf_cache_max_bytes = 0;
uint64_t dbuf_metadata_cache_max_bytes = 0;
/* Set the default sizes of the caches to log2 fraction of arc size */
int dbuf_cache_shift = 5;
int dbuf_metadata_cache_shift = 6;

/*
 * For diagnostic purposes, this is incremented whenever we can't add
 * something to the metadata cache because it's full, and instead put
 * the data in the regular dbuf cache.
 */
uint64_t dbuf_metadata_cache_overflow;

/*
 * The LRU dbuf cache uses a three-stage eviction policy:
 *	- A low water marker designates when the dbuf eviction thread
 *	should stop evicting from the dbuf cache.
 *	- When we reach the maximum size (aka mid water mark), we
 *	signal the eviction thread to run.
 *	- The high water mark indicates when the eviction thread
 *	is unable to keep up with the incoming load and eviction must
 *	happen in the context of the calling thread.
 *
 * The dbuf cache:
 *                                                 (max size)
 *                                      low water   mid water   hi water
 * +----------------------------------------+----------+----------+
 * |                                        |          |          |
 * |                                        |          |          |
 * |                                        |          |          |
 * |                                        |          |          |
 * +----------------------------------------+----------+----------+
 *                                        stop        signal     evict
 *                                      evicting     eviction   directly
 *                                                    thread
 *
 * The high and low water marks indicate the operating range for the eviction
 * thread. The low water mark is, by default, 90% of the total size of the
 * cache and the high water mark is at 110% (both of these percentages can be
 * changed by setting dbuf_cache_lowater_pct and dbuf_cache_hiwater_pct,
 * respectively). The eviction thread will try to ensure that the cache remains
 * within this range by waking up every second and checking if the cache is
 * above the low water mark. The thread can also be woken up by callers adding
 * elements into the cache if the cache is larger than the mid water (i.e max
 * cache size). Once the eviction thread is woken up and eviction is required,
 * it will continue evicting buffers until it's able to reduce the cache size
 * to the low water mark. If the cache size continues to grow and hits the high
 * water mark, then callers adding elments to the cache will begin to evict
 * directly from the cache until the cache is no longer above the high water
 * mark.
 */

/*
 * The percentage above and below the maximum cache size.
 */
uint_t dbuf_cache_hiwater_pct = 10;
uint_t dbuf_cache_lowater_pct = 10;

SYSCTL_DECL(_vfs_zfs);
SYSCTL_QUAD(_vfs_zfs, OID_AUTO, dbuf_cache_max_bytes, CTLFLAG_RWTUN,
    &dbuf_cache_max_bytes, 0, "dbuf cache size in bytes");
SYSCTL_QUAD(_vfs_zfs, OID_AUTO, dbuf_metadata_cache_max_bytes, CTLFLAG_RWTUN,
    &dbuf_metadata_cache_max_bytes, 0, "dbuf metadata cache size in bytes");
SYSCTL_INT(_vfs_zfs, OID_AUTO, dbuf_cache_shift, CTLFLAG_RDTUN,
    &dbuf_cache_shift, 0, "dbuf cache size as log2 fraction of ARC");
SYSCTL_INT(_vfs_zfs, OID_AUTO, dbuf_metadata_cache_shift, CTLFLAG_RDTUN,
    &dbuf_metadata_cache_shift, 0,
    "dbuf metadata cache size as log2 fraction of ARC");
SYSCTL_QUAD(_vfs_zfs, OID_AUTO, dbuf_metadata_cache_overflow, CTLFLAG_RD,
    &dbuf_metadata_cache_overflow, 0, "dbuf metadata cache overflow");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, dbuf_cache_hiwater_pct, CTLFLAG_RWTUN,
    &dbuf_cache_hiwater_pct, 0, "max percents above the dbuf cache size");
SYSCTL_UINT(_vfs_zfs, OID_AUTO, dbuf_cache_lowater_pct, CTLFLAG_RWTUN,
    &dbuf_cache_lowater_pct, 0, "max percents below the dbuf cache size");

/* ARGSUSED */
static int
dbuf_cons(void *vdb, void *unused, int kmflag)
{
	dmu_buf_impl_t *db = vdb;
	bzero(db, sizeof (dmu_buf_impl_t));

	mutex_init(&db->db_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&db->db_changed, NULL, CV_DEFAULT, NULL);
	multilist_link_init(&db->db_cache_link);
	refcount_create(&db->db_holds);

	return (0);
}

/* ARGSUSED */
static void
dbuf_dest(void *vdb, void *unused)
{
	dmu_buf_impl_t *db = vdb;
	mutex_destroy(&db->db_mtx);
	cv_destroy(&db->db_changed);
	ASSERT(!multilist_link_active(&db->db_cache_link));
	refcount_destroy(&db->db_holds);
}

/*
 * dbuf hash table routines
 */
static dbuf_hash_table_t dbuf_hash_table;

static uint64_t dbuf_hash_count;

/*
 * We use Cityhash for this. It's fast, and has good hash properties without
 * requiring any large static buffers.
 */
static uint64_t
dbuf_hash(void *os, uint64_t obj, uint8_t lvl, uint64_t blkid)
{
	return (cityhash4((uintptr_t)os, obj, (uint64_t)lvl, blkid));
}

#define	DBUF_EQUAL(dbuf, os, obj, level, blkid)		\
	((dbuf)->db.db_object == (obj) &&		\
	(dbuf)->db_objset == (os) &&			\
	(dbuf)->db_level == (level) &&			\
	(dbuf)->db_blkid == (blkid))

dmu_buf_impl_t *
dbuf_find(objset_t *os, uint64_t obj, uint8_t level, uint64_t blkid)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	uint64_t hv = dbuf_hash(os, obj, level, blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *db;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (db = h->hash_table[idx]; db != NULL; db = db->db_hash_next) {
		if (DBUF_EQUAL(db, os, obj, level, blkid)) {
			mutex_enter(&db->db_mtx);
			if (db->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (db);
			}
			mutex_exit(&db->db_mtx);
		}
	}
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	return (NULL);
}

static dmu_buf_impl_t *
dbuf_find_bonus(objset_t *os, uint64_t object)
{
	dnode_t *dn;
	dmu_buf_impl_t *db = NULL;

	if (dnode_hold(os, object, FTAG, &dn) == 0) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		if (dn->dn_bonus != NULL) {
			db = dn->dn_bonus;
			mutex_enter(&db->db_mtx);
		}
		rw_exit(&dn->dn_struct_rwlock);
		dnode_rele(dn, FTAG);
	}
	return (db);
}

/*
 * Insert an entry into the hash table.  If there is already an element
 * equal to elem in the hash table, then the already existing element
 * will be returned and the new element will not be inserted.
 * Otherwise returns NULL.
 */
static dmu_buf_impl_t *
dbuf_hash_insert(dmu_buf_impl_t *db)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	objset_t *os = db->db_objset;
	uint64_t obj = db->db.db_object;
	int level = db->db_level;
	uint64_t blkid, hv, idx;
	dmu_buf_impl_t *dbf;
	uint32_t i;

	blkid = db->db_blkid;
	hv = dbuf_hash(os, obj, level, blkid);
	idx = hv & h->hash_table_mask;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (dbf = h->hash_table[idx], i = 0; dbf != NULL;
	    dbf = dbf->db_hash_next, i++) {
		if (DBUF_EQUAL(dbf, os, obj, level, blkid)) {
			mutex_enter(&dbf->db_mtx);
			if (dbf->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (dbf);
			}
			mutex_exit(&dbf->db_mtx);
		}
	}

	if (i > 0) {
		DBUF_STAT_BUMP(hash_collisions);
		if (i == 1)
			DBUF_STAT_BUMP(hash_chains);

		DBUF_STAT_MAX(hash_chain_max, i);
	}

	mutex_enter(&db->db_mtx);
	db->db_hash_next = h->hash_table[idx];
	h->hash_table[idx] = db;
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_inc_64(&dbuf_hash_count);
	DBUF_STAT_MAX(hash_elements_max, dbuf_hash_count);

	return (NULL);
}

/*
 * Remove an entry from the hash table.  It must be in the EVICTING state.
 */
static void
dbuf_hash_remove(dmu_buf_impl_t *db)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	uint64_t hv, idx;
	dmu_buf_impl_t *dbf, **dbp;

	hv = dbuf_hash(db->db_objset, db->db.db_object,
	    db->db_level, db->db_blkid);
	idx = hv & h->hash_table_mask;

	/*
	 * We mustn't hold db_mtx to maintain lock ordering:
	 * DBUF_HASH_MUTEX > db_mtx.
	 */
	ASSERT(refcount_is_zero(&db->db_holds));
	ASSERT(db->db_state == DB_EVICTING);
	ASSERT(!MUTEX_HELD(&db->db_mtx));

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	dbp = &h->hash_table[idx];
	while ((dbf = *dbp) != db) {
		dbp = &dbf->db_hash_next;
		ASSERT(dbf != NULL);
	}
	*dbp = db->db_hash_next;
	db->db_hash_next = NULL;
	if (h->hash_table[idx] &&
	    h->hash_table[idx]->db_hash_next == NULL)
		DBUF_STAT_BUMPDOWN(hash_chains);
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_dec_64(&dbuf_hash_count);
}

typedef enum {
	DBVU_EVICTING,
	DBVU_NOT_EVICTING
} dbvu_verify_type_t;

static void
dbuf_verify_user(dmu_buf_impl_t *db, dbvu_verify_type_t verify_type)
{
#ifdef ZFS_DEBUG
	int64_t holds;

	if (db->db_user == NULL)
		return;

	/* Only data blocks support the attachment of user data. */
	ASSERT(db->db_level == 0);

	/* Clients must resolve a dbuf before attaching user data. */
	ASSERT(db->db.db_data != NULL);
	ASSERT3U(db->db_state, ==, DB_CACHED);

	holds = refcount_count(&db->db_holds);
	if (verify_type == DBVU_EVICTING) {
		/*
		 * Immediate eviction occurs when holds == dirtycnt.
		 * For normal eviction buffers, holds is zero on
		 * eviction, except when dbuf_fix_old_data() calls
		 * dbuf_clear_data().  However, the hold count can grow
		 * during eviction even though db_mtx is held (see
		 * dmu_bonus_hold() for an example), so we can only
		 * test the generic invariant that holds >= dirtycnt.
		 */
		ASSERT3U(holds, >=, db->db_dirtycnt);
	} else {
		if (db->db_user_immediate_evict == TRUE)
			ASSERT3U(holds, >=, db->db_dirtycnt);
		else
			ASSERT3U(holds, >, 0);
	}
#endif
}

static void
dbuf_evict_user(dmu_buf_impl_t *db)
{
	dmu_buf_user_t *dbu = db->db_user;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (dbu == NULL)
		return;

	dbuf_verify_user(db, DBVU_EVICTING);
	db->db_user = NULL;

#ifdef ZFS_DEBUG
	if (dbu->dbu_clear_on_evict_dbufp != NULL)
		*dbu->dbu_clear_on_evict_dbufp = NULL;
#endif

	/*
	 * There are two eviction callbacks - one that we call synchronously
	 * and one that we invoke via a taskq.  The async one is useful for
	 * avoiding lock order reversals and limiting stack depth.
	 *
	 * Note that if we have a sync callback but no async callback,
	 * it's likely that the sync callback will free the structure
	 * containing the dbu.  In that case we need to take care to not
	 * dereference dbu after calling the sync evict func.
	 */
	boolean_t has_async = (dbu->dbu_evict_func_async != NULL);

	if (dbu->dbu_evict_func_sync != NULL)
		dbu->dbu_evict_func_sync(dbu);

	if (has_async) {
		taskq_dispatch_ent(dbu_evict_taskq, dbu->dbu_evict_func_async,
		    dbu, 0, &dbu->dbu_tqent);
	}
}

boolean_t
dbuf_is_metadata(dmu_buf_impl_t *db)
{
	if (db->db_level > 0) {
		return (B_TRUE);
	} else {
		boolean_t is_metadata;

		DB_DNODE_ENTER(db);
		is_metadata = DMU_OT_IS_METADATA(DB_DNODE(db)->dn_type);
		DB_DNODE_EXIT(db);

		return (is_metadata);
	}
}

/*
 * This returns whether this dbuf should be stored in the metadata cache, which
 * is based on whether it's from one of the dnode types that store data related
 * to traversing dataset hierarchies.
 */
static boolean_t
dbuf_include_in_metadata_cache(dmu_buf_impl_t *db)
{
	DB_DNODE_ENTER(db);
	dmu_object_type_t type = DB_DNODE(db)->dn_type;
	DB_DNODE_EXIT(db);

	/* Check if this dbuf is one of the types we care about */
	if (DMU_OT_IS_METADATA_CACHED(type)) {
		/* If we hit this, then we set something up wrong in dmu_ot */
		ASSERT(DMU_OT_IS_METADATA(type));

		/*
		 * Sanity check for small-memory systems: don't allocate too
		 * much memory for this purpose.
		 */
		if (refcount_count(&dbuf_caches[DB_DBUF_METADATA_CACHE].size) >
		    dbuf_metadata_cache_max_bytes) {
			dbuf_metadata_cache_overflow++;
			DTRACE_PROBE1(dbuf__metadata__cache__overflow,
			    dmu_buf_impl_t *, db);
			return (B_FALSE);
		}

		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * This function *must* return indices evenly distributed between all
 * sublists of the multilist. This is needed due to how the dbuf eviction
 * code is laid out; dbuf_evict_thread() assumes dbufs are evenly
 * distributed between all sublists and uses this assumption when
 * deciding which sublist to evict from and how much to evict from it.
 */
unsigned int
dbuf_cache_multilist_index_func(multilist_t *ml, void *obj)
{
	dmu_buf_impl_t *db = obj;

	/*
	 * The assumption here, is the hash value for a given
	 * dmu_buf_impl_t will remain constant throughout it's lifetime
	 * (i.e. it's objset, object, level and blkid fields don't change).
	 * Thus, we don't need to store the dbuf's sublist index
	 * on insertion, as this index can be recalculated on removal.
	 *
	 * Also, the low order bits of the hash value are thought to be
	 * distributed evenly. Otherwise, in the case that the multilist
	 * has a power of two number of sublists, each sublists' usage
	 * would not be evenly distributed.
	 */
	return (dbuf_hash(db->db_objset, db->db.db_object,
	    db->db_level, db->db_blkid) %
	    multilist_get_num_sublists(ml));
}

static inline unsigned long
dbuf_cache_target_bytes(void)
{
	return MIN(dbuf_cache_max_bytes,
	    arc_max_bytes() >> dbuf_cache_shift);
}

static inline uint64_t
dbuf_cache_hiwater_bytes(void)
{
	uint64_t dbuf_cache_target = dbuf_cache_target_bytes();
	return (dbuf_cache_target +
	    (dbuf_cache_target * dbuf_cache_hiwater_pct) / 100);
}

static inline uint64_t
dbuf_cache_lowater_bytes(void)
{
	uint64_t dbuf_cache_target = dbuf_cache_target_bytes();
	return (dbuf_cache_target -
	    (dbuf_cache_target * dbuf_cache_lowater_pct) / 100);
}

static inline boolean_t
dbuf_cache_above_hiwater(void)
{
	return (refcount_count(&dbuf_caches[DB_DBUF_CACHE].size) >
	    dbuf_cache_hiwater_bytes());
}

static inline boolean_t
dbuf_cache_above_lowater(void)
{
	return (refcount_count(&dbuf_caches[DB_DBUF_CACHE].size) >
	    dbuf_cache_lowater_bytes());
}

/*
 * Evict the oldest eligible dbuf from the dbuf cache.
 */
static void
dbuf_evict_one(void)
{
	int idx = multilist_get_random_index(dbuf_caches[DB_DBUF_CACHE].cache);
	multilist_sublist_t *mls = multilist_sublist_lock(
	    dbuf_caches[DB_DBUF_CACHE].cache, idx);

	ASSERT(!MUTEX_HELD(&dbuf_evict_lock));

	dmu_buf_impl_t *db = multilist_sublist_tail(mls);
	while (db != NULL && mutex_tryenter(&db->db_mtx) == 0) {
		db = multilist_sublist_prev(mls, db);
	}

	DTRACE_PROBE2(dbuf__evict__one, dmu_buf_impl_t *, db,
	    multilist_sublist_t *, mls);

	if (db != NULL) {
		multilist_sublist_remove(mls, db);
		multilist_sublist_unlock(mls);
		(void) refcount_remove_many(&dbuf_caches[DB_DBUF_CACHE].size,
		    db->db.db_size, db);
		DBUF_STAT_BUMPDOWN(cache_levels[db->db_level]);
		DBUF_STAT_BUMPDOWN(cache_count);
		DBUF_STAT_DECR(cache_levels_bytes[db->db_level],
		    db->db.db_size);
		ASSERT3U(db->db_caching_status, ==, DB_DBUF_CACHE);
		db->db_caching_status = DB_NO_CACHE;
		dbuf_destroy(db);
		DBUF_STAT_MAX(cache_size_bytes_max,
		    refcount_count(&dbuf_caches[DB_DBUF_CACHE].size));
		DBUF_STAT_BUMP(cache_total_evicts);
	} else {
		multilist_sublist_unlock(mls);
	}
}

/*
 * The dbuf evict thread is responsible for aging out dbufs from the
 * cache. Once the cache has reached it's maximum size, dbufs are removed
 * and destroyed. The eviction thread will continue running until the size
 * of the dbuf cache is at or below the maximum size. Once the dbuf is aged
 * out of the cache it is destroyed and becomes eligible for arc eviction.
 */
/* ARGSUSED */
static void
dbuf_evict_thread(void *unused __unused)
{
	callb_cpr_t cpr;

	CALLB_CPR_INIT(&cpr, &dbuf_evict_lock, callb_generic_cpr, FTAG);

	mutex_enter(&dbuf_evict_lock);
	while (!dbuf_evict_thread_exit) {
		while (!dbuf_cache_above_lowater() && !dbuf_evict_thread_exit) {
			CALLB_CPR_SAFE_BEGIN(&cpr);
			(void) cv_timedwait_hires(&dbuf_evict_cv,
			    &dbuf_evict_lock, SEC2NSEC(1), MSEC2NSEC(1), 0);
			CALLB_CPR_SAFE_END(&cpr, &dbuf_evict_lock);
		}
		mutex_exit(&dbuf_evict_lock);

		/*
		 * Keep evicting as long as we're above the low water mark
		 * for the cache. We do this without holding the locks to
		 * minimize lock contention.
		 */
		while (dbuf_cache_above_lowater() && !dbuf_evict_thread_exit) {
			dbuf_evict_one();
		}

		mutex_enter(&dbuf_evict_lock);
	}

	dbuf_evict_thread_exit = B_FALSE;
	cv_broadcast(&dbuf_evict_cv);
	CALLB_CPR_EXIT(&cpr);	/* drops dbuf_evict_lock */
	thread_exit();
}

/*
 * Wake up the dbuf eviction thread if the dbuf cache is at its max size.
 * If the dbuf cache is at its high water mark, then evict a dbuf from the
 * dbuf cache using the callers context.
 */
static void
dbuf_evict_notify(void)
{
	/*
	 * We check if we should evict without holding the dbuf_evict_lock,
	 * because it's OK to occasionally make the wrong decision here,
	 * and grabbing the lock results in massive lock contention.
	 */
	if (refcount_count(&dbuf_caches[DB_DBUF_CACHE].size) >
	    dbuf_cache_max_bytes) {
		if (dbuf_cache_above_hiwater())
			dbuf_evict_one();
		cv_signal(&dbuf_evict_cv);
	}
}

static int
dbuf_kstat_update(kstat_t *ksp, int rw)
{
	dbuf_stats_t *ds = ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		return (SET_ERROR(EACCES));
	} else {
		ds->metadata_cache_size_bytes.value.ui64 =
		    refcount_count(&dbuf_caches[DB_DBUF_METADATA_CACHE].size);
		ds->cache_size_bytes.value.ui64 =
		    refcount_count(&dbuf_caches[DB_DBUF_CACHE].size);
		ds->cache_target_bytes.value.ui64 = dbuf_cache_target_bytes();
		ds->cache_hiwater_bytes.value.ui64 = dbuf_cache_hiwater_bytes();
		ds->cache_lowater_bytes.value.ui64 = dbuf_cache_lowater_bytes();
		ds->hash_elements.value.ui64 = dbuf_hash_count;
	}

	return (0);
}

void
dbuf_init(void)
{
	uint64_t hsize = 1ULL << 16;
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	/*
	 * The hash table is big enough to fill all of physical memory
	 * with an average 4K block size.  The table will take up
	 * totalmem*sizeof(void*)/4K (i.e. 2MB/GB with 8-byte pointers).
	 */
	while (hsize * 4096 < (uint64_t)physmem * PAGESIZE)
		hsize <<= 1;

retry:
	h->hash_table_mask = hsize - 1;
	h->hash_table = kmem_zalloc(hsize * sizeof (void *), KM_NOSLEEP);
	if (h->hash_table == NULL) {
		/* XXX - we should really return an error instead of assert */
		ASSERT(hsize > (1ULL << 10));
		hsize >>= 1;
		goto retry;
	}

	dbuf_kmem_cache = kmem_cache_create("dmu_buf_impl_t",
	    sizeof (dmu_buf_impl_t),
	    0, dbuf_cons, dbuf_dest, NULL, NULL, NULL, 0);

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_init(&h->hash_mutexes[i], NULL, MUTEX_DEFAULT, NULL);

	dbuf_stats_init(h);
	/*
	 * Setup the parameters for the dbuf caches. We set the sizes of the
	 * dbuf cache and the metadata cache to 1/32nd and 1/16th (default)
	 * of the size of the ARC, respectively. If the values are set in
	 * /etc/system and they're not greater than the size of the ARC, then
	 * we honor that value.
	 */
	if (dbuf_cache_max_bytes == 0 ||
	    dbuf_cache_max_bytes >= arc_max_bytes())  {
		dbuf_cache_max_bytes = arc_max_bytes() >> dbuf_cache_shift;
	}
	if (dbuf_metadata_cache_max_bytes == 0 ||
	    dbuf_metadata_cache_max_bytes >= arc_max_bytes()) {
		dbuf_metadata_cache_max_bytes =
		    arc_max_bytes() >> dbuf_metadata_cache_shift;
	}

	/*
	 * All entries are queued via taskq_dispatch_ent(), so min/maxalloc
	 * configuration is not required.
	 */
	dbu_evict_taskq = taskq_create("dbu_evict", 1, minclsyspri, 0, 0, 0);

	for (dbuf_cached_state_t dcs = 0; dcs < DB_CACHE_MAX; dcs++) {
		dbuf_caches[dcs].cache =
		    multilist_create(sizeof (dmu_buf_impl_t),
		    offsetof(dmu_buf_impl_t, db_cache_link),
		    dbuf_cache_multilist_index_func);
		refcount_create(&dbuf_caches[dcs].size);
	}

	dbuf_evict_thread_exit = B_FALSE;
	mutex_init(&dbuf_evict_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dbuf_evict_cv, NULL, CV_DEFAULT, NULL);
	dbuf_cache_evict_thread = thread_create(NULL, 0, dbuf_evict_thread,
	    NULL, 0, &p0, TS_RUN, minclsyspri);

#ifdef __linux__
	/*
	 * XXX FreeBSD's SPL lacks KSTAT_TYPE_NAMED support - TODO 
	 */
	dbuf_ksp = kstat_create("zfs", 0, "dbufstats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (dbuf_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (dbuf_ksp != NULL) {
		dbuf_ksp->ks_data = &dbuf_stats;
		dbuf_ksp->ks_update = dbuf_kstat_update;
		kstat_install(dbuf_ksp);

		for (i = 0; i < DN_MAX_LEVELS; i++) {
			snprintf(dbuf_stats.cache_levels[i].name,
			    KSTAT_STRLEN, "cache_level_%d", i);
			dbuf_stats.cache_levels[i].data_type =
			    KSTAT_DATA_UINT64;
			snprintf(dbuf_stats.cache_levels_bytes[i].name,
			    KSTAT_STRLEN, "cache_level_%d_bytes", i);
			dbuf_stats.cache_levels_bytes[i].data_type =
			    KSTAT_DATA_UINT64;
		}
	}
#endif	
}

void
dbuf_fini(void)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	dbuf_stats_destroy();

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_destroy(&h->hash_mutexes[i]);
	kmem_free(h->hash_table, (h->hash_table_mask + 1) * sizeof (void *));
	kmem_cache_destroy(dbuf_kmem_cache);
	taskq_destroy(dbu_evict_taskq);

	mutex_enter(&dbuf_evict_lock);
	dbuf_evict_thread_exit = B_TRUE;
	while (dbuf_evict_thread_exit) {
		cv_signal(&dbuf_evict_cv);
		cv_wait(&dbuf_evict_cv, &dbuf_evict_lock);
	}
	mutex_exit(&dbuf_evict_lock);

	mutex_destroy(&dbuf_evict_lock);
	cv_destroy(&dbuf_evict_cv);

	for (dbuf_cached_state_t dcs = 0; dcs < DB_CACHE_MAX; dcs++) {
		refcount_destroy(&dbuf_caches[dcs].size);
		multilist_destroy(dbuf_caches[dcs].cache);
	}

	if (dbuf_ksp != NULL) {
		kstat_delete(dbuf_ksp);
		dbuf_ksp = NULL;
	}
}

/*
 * Other stuff.
 */

#ifdef ZFS_DEBUG
static void
dbuf_verify(dmu_buf_impl_t *db)
{
	dnode_t *dn;
	dbuf_dirty_record_t *dr;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (!(zfs_flags & ZFS_DEBUG_DBUF_VERIFY))
		return;

	ASSERT(db->db_objset != NULL);
	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	if (dn == NULL) {
		ASSERT(db->db_parent == NULL);
		ASSERT(db->db_blkptr == NULL);
	} else {
		ASSERT3U(db->db.db_object, ==, dn->dn_object);
		ASSERT3P(db->db_objset, ==, dn->dn_objset);
		ASSERT3U(db->db_level, <, dn->dn_nlevels);
		ASSERT(db->db_blkid == DMU_BONUS_BLKID ||
		    db->db_blkid == DMU_SPILL_BLKID ||
		    !avl_is_empty(&dn->dn_dbufs));
	}
	if (db->db_blkid == DMU_BONUS_BLKID) {
		ASSERT(dn != NULL);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		ASSERT3U(db->db.db_offset, ==, DMU_BONUS_BLKID);
	} else if (db->db_blkid == DMU_SPILL_BLKID) {
		ASSERT(dn != NULL);
		ASSERT0(db->db.db_offset);
	} else {
		ASSERT3U(db->db.db_offset, ==, db->db_blkid * db->db.db_size);
	}

	for (dr = db->db_data_pending; dr != NULL; dr = dr->dr_next)
		ASSERT(dr->dr_dbuf == db);

	for (dr = db->db_last_dirty; dr != NULL; dr = dr->dr_next)
		ASSERT(dr->dr_dbuf == db);

	/*
	 * We can't assert that db_size matches dn_datablksz because it
	 * can be momentarily different when another thread is doing
	 * dnode_set_blksz().
	 */
	if (db->db_level == 0 && db->db.db_object == DMU_META_DNODE_OBJECT) {
		dr = db->db_data_pending;
		/*
		 * It should only be modified in syncing context, so
		 * make sure we only have one copy of the data.
		 */
		ASSERT(dr == NULL || dr->dt.dl.dr_data == db->db_buf);
	}

	/* verify db->db_blkptr */
	if (db->db_blkptr) {
		if (db->db_parent == dn->dn_dbuf) {
			/* db is pointed to by the dnode */
			/* ASSERT3U(db->db_blkid, <, dn->dn_nblkptr); */
			if (DMU_OBJECT_IS_SPECIAL(db->db.db_object))
				ASSERT(db->db_parent == NULL);
			else
				ASSERT(db->db_parent != NULL);
			if (db->db_blkid != DMU_SPILL_BLKID)
				ASSERT3P(db->db_blkptr, ==,
				    &dn->dn_phys->dn_blkptr[db->db_blkid]);
		} else {
			/* db is pointed to by an indirect block */
			int epb = db->db_parent->db.db_size >> SPA_BLKPTRSHIFT;
			ASSERT3U(db->db_parent->db_level, ==, db->db_level+1);
			ASSERT3U(db->db_parent->db.db_object, ==,
			    db->db.db_object);
			/*
			 * dnode_grow_indblksz() can make this fail if we don't
			 * have the struct_rwlock.  XXX indblksz no longer
			 * grows.  safe to do this now?
			 */
			if (RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
				ASSERT3P(db->db_blkptr, ==,
				    ((blkptr_t *)db->db_parent->db.db_data +
				    db->db_blkid % epb));
			}
		}
	}
	if ((db->db_blkptr == NULL || BP_IS_HOLE(db->db_blkptr)) &&
	    (db->db_buf == NULL || db->db_buf->b_data) &&
	    db->db.db_data && db->db_blkid != DMU_BONUS_BLKID &&
	    db->db_state != DB_FILL && !dn->dn_free_txg) {
		/*
		 * If the blkptr isn't set but they have nonzero data,
		 * it had better be dirty, otherwise we'll lose that
		 * data when we evict this buffer.
		 *
		 * There is an exception to this rule for indirect blocks; in
		 * this case, if the indirect block is a hole, we fill in a few
		 * fields on each of the child blocks (importantly, birth time)
		 * to prevent hole birth times from being lost when you
		 * partially fill in a hole.
		 */
		if (db->db_dirtycnt == 0) {
			if (db->db_level == 0) {
				uint64_t *buf = db->db.db_data;
				int i;

				for (i = 0; i < db->db.db_size >> 3; i++) {
					ASSERT(buf[i] == 0);
				}
			} else {
				blkptr_t *bps = db->db.db_data;
				ASSERT3U(1 << DB_DNODE(db)->dn_indblkshift, ==,
				    db->db.db_size);
				/*
				 * We want to verify that all the blkptrs in the
				 * indirect block are holes, but we may have
				 * automatically set up a few fields for them.
				 * We iterate through each blkptr and verify
				 * they only have those fields set.
				 */
				for (int i = 0;
				    i < db->db.db_size / sizeof (blkptr_t);
				    i++) {
					blkptr_t *bp = &bps[i];
					ASSERT(ZIO_CHECKSUM_IS_ZERO(
					    &bp->blk_cksum));
					ASSERT(
					    DVA_IS_EMPTY(&bp->blk_dva[0]) &&
					    DVA_IS_EMPTY(&bp->blk_dva[1]) &&
					    DVA_IS_EMPTY(&bp->blk_dva[2]));
					ASSERT0(bp->blk_fill);
					ASSERT0(bp->blk_pad[0]);
					ASSERT0(bp->blk_pad[1]);
					ASSERT(!BP_IS_EMBEDDED(bp));
					ASSERT(BP_IS_HOLE(bp));
					ASSERT0(bp->blk_phys_birth);
				}
			}
		}
	}
	DB_DNODE_EXIT(db);
}
#endif

static void
dbuf_clear_data(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	dbuf_evict_user(db);
	ASSERT3P(db->db_buf, ==, NULL);
	db->db.db_data = NULL;
	if (db->db_state != DB_NOFILL)
		db->db_state = DB_UNCACHED;
}

static void
dbuf_set_data(dmu_buf_impl_t *db, arc_buf_t *buf)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(buf != NULL);

	db->db_buf = buf;
	ASSERT(buf->b_data != NULL);
	db->db.db_data = buf->b_data;
}

/*
 * Loan out an arc_buf for read.  Return the loaned arc_buf.
 */
arc_buf_t *
dbuf_loan_arcbuf(dmu_buf_impl_t *db)
{
	arc_buf_t *abuf;

	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	mutex_enter(&db->db_mtx);
	if (arc_released(db->db_buf) || refcount_count(&db->db_holds) > 1) {
		int blksz = db->db.db_size;
		spa_t *spa = db->db_objset->os_spa;

		mutex_exit(&db->db_mtx);
		abuf = arc_loan_buf(spa, B_FALSE, blksz);
		bcopy(db->db.db_data, abuf->b_data, blksz);
	} else {
		abuf = db->db_buf;
		arc_loan_inuse_buf(abuf, db);
		db->db_buf = NULL;
		dbuf_clear_data(db);
		mutex_exit(&db->db_mtx);
	}
	return (abuf);
}

/*
 * Calculate which level n block references the data at the level 0 offset
 * provided.
 */
uint64_t
dbuf_whichblock(dnode_t *dn, int64_t level, uint64_t offset)
{
	if (dn->dn_datablkshift != 0 && dn->dn_indblkshift != 0) {
		/*
		 * The level n blkid is equal to the level 0 blkid divided by
		 * the number of level 0s in a level n block.
		 *
		 * The level 0 blkid is offset >> datablkshift =
		 * offset / 2^datablkshift.
		 *
		 * The number of level 0s in a level n is the number of block
		 * pointers in an indirect block, raised to the power of level.
		 * This is 2^(indblkshift - SPA_BLKPTRSHIFT)^level =
		 * 2^(level*(indblkshift - SPA_BLKPTRSHIFT)).
		 *
		 * Thus, the level n blkid is: offset /
		 * ((2^datablkshift)*(2^(level*(indblkshift - SPA_BLKPTRSHIFT)))
		 * = offset / 2^(datablkshift + level *
		 *   (indblkshift - SPA_BLKPTRSHIFT))
		 * = offset >> (datablkshift + level *
		 *   (indblkshift - SPA_BLKPTRSHIFT))
		 */
		return (offset >> (dn->dn_datablkshift + level *
		    (dn->dn_indblkshift - SPA_BLKPTRSHIFT)));
	} else {
		ASSERT3U(offset, <, dn->dn_datablksz);
		return (0);
	}
}

static void
dbuf_read_done(zio_t *zio, const zbookmark_phys_t *zb, const blkptr_t *bp,
    arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;

	mutex_enter(&db->db_mtx);
	ASSERT3U(db->db_state, ==, DB_READ);
	/*
	 * All reads are synchronous, so we must have a hold on the dbuf
	 */
	ASSERT(refcount_count(&db->db_holds) > 0);
	ASSERT(db->db_buf == NULL);
	ASSERT(db->db.db_data == NULL);
	if (buf == NULL) {
		/* i/o error */
		ASSERT(zio == NULL || zio->io_error != 0);
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);
		ASSERT3P(db->db_buf, ==, NULL);
		db->db_state = DB_UNCACHED;
	} else if (db->db_level == 0 && db->db_freed_in_flight) {
		/* freed in flight */
		ASSERT(zio == NULL || zio->io_error == 0);
		if (buf == NULL) {
			buf = arc_alloc_buf(db->db_objset->os_spa,
			     db, DBUF_GET_BUFC_TYPE(db), db->db.db_size);
		}
		arc_release(buf, db);
		bzero(buf->b_data, db->db.db_size);
		arc_buf_freeze(buf);
		db->db_freed_in_flight = FALSE;
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	} else {
		/* success */
		ASSERT(zio == NULL || zio->io_error == 0);
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	}
	cv_broadcast(&db->db_changed);
	dbuf_rele_and_unlock(db, NULL, B_FALSE);
}

static void
dbuf_read_impl(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags)
{
	dnode_t *dn;
	zbookmark_phys_t zb;
	arc_flags_t aflags = ARC_FLAG_NOWAIT;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	ASSERT(!refcount_is_zero(&db->db_holds));
	/* We need the struct_rwlock to prevent db_blkptr from changing. */
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_state == DB_UNCACHED);
	ASSERT(db->db_buf == NULL);

	if (db->db_blkid == DMU_BONUS_BLKID) {
		/*
		 * The bonus length stored in the dnode may be less than
		 * the maximum available space in the bonus buffer.
		 */
		int bonuslen = MIN(dn->dn_bonuslen, dn->dn_phys->dn_bonuslen);
		int max_bonuslen = DN_SLOTS_TO_BONUSLEN(dn->dn_num_slots);

		ASSERT3U(bonuslen, <=, db->db.db_size);
		db->db.db_data = zio_buf_alloc(max_bonuslen);
		arc_space_consume(max_bonuslen, ARC_SPACE_BONUS);
		if (bonuslen < max_bonuslen)
			bzero(db->db.db_data, max_bonuslen);
		if (bonuslen)
			bcopy(DN_BONUS(dn->dn_phys), db->db.db_data, bonuslen);
		DB_DNODE_EXIT(db);
		db->db_state = DB_CACHED;
		mutex_exit(&db->db_mtx);
		return;
	}

	/*
	 * Recheck BP_IS_HOLE() after dnode_block_freed() in case dnode_sync()
	 * processes the delete record and clears the bp while we are waiting
	 * for the dn_mtx (resulting in a "no" from block_freed).
	 */
	if (db->db_blkptr == NULL || BP_IS_HOLE(db->db_blkptr) ||
	    (db->db_level == 0 && (dnode_block_freed(dn, db->db_blkid) ||
	    BP_IS_HOLE(db->db_blkptr)))) {
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);

		dbuf_set_data(db, arc_alloc_buf(db->db_objset->os_spa, db, type,
		    db->db.db_size));
		bzero(db->db.db_data, db->db.db_size);

		if (db->db_blkptr != NULL && db->db_level > 0 &&
		    BP_IS_HOLE(db->db_blkptr) &&
		    db->db_blkptr->blk_birth != 0) {
			blkptr_t *bps = db->db.db_data;
			for (int i = 0; i < ((1 <<
			    DB_DNODE(db)->dn_indblkshift) / sizeof (blkptr_t));
			    i++) {
				blkptr_t *bp = &bps[i];
				ASSERT3U(BP_GET_LSIZE(db->db_blkptr), ==,
				    1 << dn->dn_indblkshift);
				BP_SET_LSIZE(bp,
				    BP_GET_LEVEL(db->db_blkptr) == 1 ?
				    dn->dn_datablksz :
				    BP_GET_LSIZE(db->db_blkptr));
				BP_SET_TYPE(bp, BP_GET_TYPE(db->db_blkptr));
				BP_SET_LEVEL(bp,
				    BP_GET_LEVEL(db->db_blkptr) - 1);
				BP_SET_BIRTH(bp, db->db_blkptr->blk_birth, 0);
			}
		}
		DB_DNODE_EXIT(db);
		db->db_state = DB_CACHED;
		mutex_exit(&db->db_mtx);
		return;
	}

	DB_DNODE_EXIT(db);

	db->db_state = DB_READ;
	mutex_exit(&db->db_mtx);

	if (DBUF_IS_L2CACHEABLE(db))
		aflags |= ARC_FLAG_L2CACHE;

	SET_BOOKMARK(&zb, db->db_objset->os_dsl_dataset ?
	    db->db_objset->os_dsl_dataset->ds_object : DMU_META_OBJSET,
	    db->db.db_object, db->db_level, db->db_blkid);

	dbuf_add_ref(db, NULL);

	(void) arc_read(zio, db->db_objset->os_spa, db->db_blkptr,
	    dbuf_read_done, db, ZIO_PRIORITY_SYNC_READ,
	    (flags & DB_RF_CANFAIL) ? ZIO_FLAG_CANFAIL : ZIO_FLAG_MUSTSUCCEED,
	    &aflags, &zb);
}

/*
 * This is our just-in-time copy function.  It makes a copy of buffers that
 * have been modified in a previous transaction group before we access them in
 * the current active group.
 *
 * This function is used in three places: when we are dirtying a buffer for the
 * first time in a txg, when we are freeing a range in a dnode that includes
 * this buffer, and when we are accessing a buffer which was received compressed
 * and later referenced in a WRITE_BYREF record.
 *
 * Note that when we are called from dbuf_free_range() we do not put a hold on
 * the buffer, we just traverse the active dbuf list for the dnode.
 */
static void
dbuf_fix_old_data(dmu_buf_impl_t *db, uint64_t txg)
{
	dbuf_dirty_record_t *dr = db->db_last_dirty;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db.db_data != NULL);
	ASSERT(db->db_level == 0);
	ASSERT(db->db.db_object != DMU_META_DNODE_OBJECT);

	if (dr == NULL ||
	    (dr->dt.dl.dr_data !=
	    ((db->db_blkid  == DMU_BONUS_BLKID) ? db->db.db_data : db->db_buf)))
		return;

	/*
	 * If the last dirty record for this dbuf has not yet synced
	 * and its referencing the dbuf data, either:
	 *	reset the reference to point to a new copy,
	 * or (if there a no active holders)
	 *	just null out the current db_data pointer.
	 */
	ASSERT(dr->dr_txg >= txg - 2);
	if (db->db_blkid == DMU_BONUS_BLKID) {
		/* Note that the data bufs here are zio_bufs */
		dnode_t *dn = DB_DNODE(db);
		int bonuslen = DN_SLOTS_TO_BONUSLEN(dn->dn_num_slots);
		dr->dt.dl.dr_data = zio_buf_alloc(bonuslen);
		arc_space_consume(bonuslen, ARC_SPACE_BONUS);
		bcopy(db->db.db_data, dr->dt.dl.dr_data, bonuslen);
	} else if (refcount_count(&db->db_holds) > db->db_dirtycnt) {
		int size = arc_buf_size(db->db_buf);
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
		spa_t *spa = db->db_objset->os_spa;
		enum zio_compress compress_type =
		    arc_get_compression(db->db_buf);

		if (compress_type == ZIO_COMPRESS_OFF) {
			dr->dt.dl.dr_data = arc_alloc_buf(spa, db, type, size);
		} else {
			ASSERT3U(type, ==, ARC_BUFC_DATA);
			dr->dt.dl.dr_data = arc_alloc_compressed_buf(spa, db,
			    size, arc_buf_lsize(db->db_buf), compress_type);
		}
		bcopy(db->db.db_data, dr->dt.dl.dr_data->b_data, size);
	} else {
		db->db_buf = NULL;
		dbuf_clear_data(db);
	}
}

int
dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags)
{
	int err = 0;
	boolean_t prefetch;
	dnode_t *dn;

	/*
	 * We don't have to hold the mutex to check db_state because it
	 * can't be freed while we have a hold on the buffer.
	 */
	ASSERT(!refcount_is_zero(&db->db_holds));

	if (db->db_state == DB_NOFILL)
		return (SET_ERROR(EIO));

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	if ((flags & DB_RF_HAVESTRUCT) == 0)
		rw_enter(&dn->dn_struct_rwlock, RW_READER);

	prefetch = db->db_level == 0 && db->db_blkid != DMU_BONUS_BLKID &&
	    (flags & DB_RF_NOPREFETCH) == 0 && dn != NULL &&
	    DBUF_IS_CACHEABLE(db);

	mutex_enter(&db->db_mtx);
	if (db->db_state == DB_CACHED) {
		/*
		 * If the arc buf is compressed, we need to decompress it to
		 * read the data. This could happen during the "zfs receive" of
		 * a stream which is compressed and deduplicated.
		 */
		if (db->db_buf != NULL &&
		    arc_get_compression(db->db_buf) != ZIO_COMPRESS_OFF) {
			dbuf_fix_old_data(db,
			    spa_syncing_txg(dmu_objset_spa(db->db_objset)));
			err = arc_decompress(db->db_buf);
			dbuf_set_data(db, db->db_buf);
		}
		mutex_exit(&db->db_mtx);
		if (prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db_blkid, 1, B_TRUE);
		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&dn->dn_struct_rwlock);
		DB_DNODE_EXIT(db);
		DBUF_STAT_BUMP(hash_hits);
	} else if (db->db_state == DB_UNCACHED) {
		spa_t *spa = dn->dn_objset->os_spa;
		boolean_t need_wait = B_FALSE;

		if (zio == NULL &&
		    db->db_blkptr != NULL && !BP_IS_HOLE(db->db_blkptr)) {
			zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
			need_wait = B_TRUE;
		}
		dbuf_read_impl(db, zio, flags);

		/* dbuf_read_impl has dropped db_mtx for us */

		if (prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db_blkid, 1, B_TRUE);

		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&dn->dn_struct_rwlock);
		DB_DNODE_EXIT(db);
		DBUF_STAT_BUMP(hash_misses);

		if (need_wait)
			err = zio_wait(zio);
	} else {
		/*
		 * Another reader came in while the dbuf was in flight
		 * between UNCACHED and CACHED.  Either a writer will finish
		 * writing the buffer (sending the dbuf to CACHED) or the
		 * first reader's request will reach the read_done callback
		 * and send the dbuf to CACHED.  Otherwise, a failure
		 * occurred and the dbuf went to UNCACHED.
		 */
		mutex_exit(&db->db_mtx);
		if (prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db_blkid, 1, B_TRUE);
		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&dn->dn_struct_rwlock);
		DB_DNODE_EXIT(db);
		DBUF_STAT_BUMP(hash_misses);

		/* Skip the wait per the caller's request. */
		mutex_enter(&db->db_mtx);
		if ((flags & DB_RF_NEVERWAIT) == 0) {
			while (db->db_state == DB_READ ||
			    db->db_state == DB_FILL) {
				ASSERT(db->db_state == DB_READ ||
				    (flags & DB_RF_HAVESTRUCT) == 0);
				DTRACE_PROBE2(blocked__read, dmu_buf_impl_t *,
				    db, zio_t *, zio);
				cv_wait(&db->db_changed, &db->db_mtx);
			}
			if (db->db_state == DB_UNCACHED)
				err = SET_ERROR(EIO);
		}
		mutex_exit(&db->db_mtx);
	}

	return (err);
}

static void
dbuf_noread(dmu_buf_impl_t *db)
{
	ASSERT(!refcount_is_zero(&db->db_holds));
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	mutex_enter(&db->db_mtx);
	while (db->db_state == DB_READ || db->db_state == DB_FILL)
		cv_wait(&db->db_changed, &db->db_mtx);
	if (db->db_state == DB_UNCACHED) {
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
		spa_t *spa = db->db_objset->os_spa;

		ASSERT(db->db_buf == NULL);
		ASSERT(db->db.db_data == NULL);
		dbuf_set_data(db, arc_alloc_buf(spa, db, type, db->db.db_size));
		db->db_state = DB_FILL;
	} else if (db->db_state == DB_NOFILL) {
		dbuf_clear_data(db);
	} else {
		ASSERT3U(db->db_state, ==, DB_CACHED);
	}
	mutex_exit(&db->db_mtx);
}

void
dbuf_unoverride(dbuf_dirty_record_t *dr)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	blkptr_t *bp = &dr->dt.dl.dr_overridden_by;
	uint64_t txg = dr->dr_txg;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	/*
	 * This assert is valid because dmu_sync() expects to be called by
	 * a zilog's get_data while holding a range lock.  This call only
	 * comes from dbuf_dirty() callers who must also hold a range lock.
	 */
	ASSERT(dr->dt.dl.dr_override_state != DR_IN_DMU_SYNC);
	ASSERT(db->db_level == 0);

	if (db->db_blkid == DMU_BONUS_BLKID ||
	    dr->dt.dl.dr_override_state == DR_NOT_OVERRIDDEN)
		return;

	ASSERT(db->db_data_pending != dr);

	/* free this block */
	if (!BP_IS_HOLE(bp) && !dr->dt.dl.dr_nopwrite)
		zio_free(db->db_objset->os_spa, txg, bp);

	dr->dt.dl.dr_override_state = DR_NOT_OVERRIDDEN;
	dr->dt.dl.dr_nopwrite = B_FALSE;

	/*
	 * Release the already-written buffer, so we leave it in
	 * a consistent dirty state.  Note that all callers are
	 * modifying the buffer, so they will immediately do
	 * another (redundant) arc_release().  Therefore, leave
	 * the buf thawed to save the effort of freezing &
	 * immediately re-thawing it.
	 */
	arc_release(dr->dt.dl.dr_data, db);
}

/*
 * Evict (if its unreferenced) or clear (if its referenced) any level-0
 * data blocks in the free range, so that any future readers will find
 * empty blocks.
 */
void
dbuf_free_range(dnode_t *dn, uint64_t start_blkid, uint64_t end_blkid,
    dmu_tx_t *tx)
{
	dmu_buf_impl_t db_search;
	dmu_buf_impl_t *db, *db_next;
	uint64_t txg = tx->tx_txg;
	avl_index_t where;

	if (end_blkid > dn->dn_maxblkid &&
	    !(start_blkid == DMU_SPILL_BLKID || end_blkid == DMU_SPILL_BLKID))
		end_blkid = dn->dn_maxblkid;
	dprintf_dnode(dn, "start=%llu end=%llu\n", start_blkid, end_blkid);

	db_search.db_level = 0;
	db_search.db_blkid = start_blkid;
	db_search.db_state = DB_SEARCH;

	mutex_enter(&dn->dn_dbufs_mtx);
	db = avl_find(&dn->dn_dbufs, &db_search, &where);
	ASSERT3P(db, ==, NULL);

	db = avl_nearest(&dn->dn_dbufs, where, AVL_AFTER);

	for (; db != NULL; db = db_next) {
		db_next = AVL_NEXT(&dn->dn_dbufs, db);
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);

		if (db->db_level != 0 || db->db_blkid > end_blkid) {
			break;
		}
		ASSERT3U(db->db_blkid, >=, start_blkid);

		/* found a level 0 buffer in the range */
		mutex_enter(&db->db_mtx);
		if (dbuf_undirty(db, tx)) {
			/* mutex has been dropped and dbuf destroyed */
			continue;
		}

		if (db->db_state == DB_UNCACHED ||
		    db->db_state == DB_NOFILL ||
		    db->db_state == DB_EVICTING) {
			ASSERT(db->db.db_data == NULL);
			mutex_exit(&db->db_mtx);
			continue;
		}
		if (db->db_state == DB_READ || db->db_state == DB_FILL) {
			/* will be handled in dbuf_read_done or dbuf_rele */
			db->db_freed_in_flight = TRUE;
			mutex_exit(&db->db_mtx);
			continue;
		}
		if (refcount_count(&db->db_holds) == 0) {
			ASSERT(db->db_buf);
			dbuf_destroy(db);
			continue;
		}
		/* The dbuf is referenced */

		if (db->db_last_dirty != NULL) {
			dbuf_dirty_record_t *dr = db->db_last_dirty;

			if (dr->dr_txg == txg) {
				/*
				 * This buffer is "in-use", re-adjust the file
				 * size to reflect that this buffer may
				 * contain new data when we sync.
				 */
				if (db->db_blkid != DMU_SPILL_BLKID &&
				    db->db_blkid > dn->dn_maxblkid)
					dn->dn_maxblkid = db->db_blkid;
				dbuf_unoverride(dr);
			} else {
				/*
				 * This dbuf is not dirty in the open context.
				 * Either uncache it (if its not referenced in
				 * the open context) or reset its contents to
				 * empty.
				 */
				dbuf_fix_old_data(db, txg);
			}
		}
		/* clear the contents if its cached */
		if (db->db_state == DB_CACHED) {
			ASSERT(db->db.db_data != NULL);
			arc_release(db->db_buf, db);
			bzero(db->db.db_data, db->db.db_size);
			arc_buf_freeze(db->db_buf);
		}

		mutex_exit(&db->db_mtx);
	}
	mutex_exit(&dn->dn_dbufs_mtx);
}

void
dbuf_new_size(dmu_buf_impl_t *db, int size, dmu_tx_t *tx)
{
	arc_buf_t *buf, *obuf;
	int osize = db->db.db_size;
	arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
	dnode_t *dn;

	ASSERT(db->db_blkid != DMU_BONUS_BLKID);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	/* XXX does *this* func really need the lock? */
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));

	/*
	 * This call to dmu_buf_will_dirty() with the dn_struct_rwlock held
	 * is OK, because there can be no other references to the db
	 * when we are changing its size, so no concurrent DB_FILL can
	 * be happening.
	 */
	/*
	 * XXX we should be doing a dbuf_read, checking the return
	 * value and returning that up to our callers
	 */
	dmu_buf_will_dirty(&db->db, tx);

	/* create the data buffer for the new block */
	buf = arc_alloc_buf(dn->dn_objset->os_spa, db, type, size);

	/* copy old block data to the new block */
	obuf = db->db_buf;
	bcopy(obuf->b_data, buf->b_data, MIN(osize, size));
	/* zero the remainder */
	if (size > osize)
		bzero((uint8_t *)buf->b_data + osize, size - osize);

	mutex_enter(&db->db_mtx);
	dbuf_set_data(db, buf);
	arc_buf_destroy(obuf, db);
	db->db.db_size = size;

	if (db->db_level == 0) {
		ASSERT3U(db->db_last_dirty->dr_txg, ==, tx->tx_txg);
		db->db_last_dirty->dt.dl.dr_data = buf;
	}
	mutex_exit(&db->db_mtx);

	dmu_objset_willuse_space(dn->dn_objset, size - osize, tx);
	DB_DNODE_EXIT(db);
}

void
dbuf_release_bp(dmu_buf_impl_t *db)
{
	objset_t *os = db->db_objset;

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(os)));
	ASSERT(arc_released(os->os_phys_buf) ||
	    list_link_active(&os->os_dsl_dataset->ds_synced_link));
	ASSERT(db->db_parent == NULL || arc_released(db->db_parent->db_buf));

	(void) arc_release(db->db_buf, db);
}

/*
 * We already have a dirty record for this TXG, and we are being
 * dirtied again.
 */
static void
dbuf_redirty(dbuf_dirty_record_t *dr)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (db->db_level == 0 && db->db_blkid != DMU_BONUS_BLKID) {
		/*
		 * If this buffer has already been written out,
		 * we now need to reset its state.
		 */
		dbuf_unoverride(dr);
		if (db->db.db_object != DMU_META_DNODE_OBJECT &&
		    db->db_state != DB_NOFILL) {
			/* Already released on initial dirty, so just thaw. */
			ASSERT(arc_released(db->db_buf));
			arc_buf_thaw(db->db_buf);
		}
	}
}

dbuf_dirty_record_t *
dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	dnode_t *dn;
	objset_t *os;
	dbuf_dirty_record_t **drp, *dr;
	int drop_struct_lock = FALSE;
	int txgoff = tx->tx_txg & TXG_MASK;

	ASSERT(tx->tx_txg != 0);
	ASSERT(!refcount_is_zero(&db->db_holds));
	DMU_TX_DIRTY_BUF(tx, db);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	/*
	 * Shouldn't dirty a regular buffer in syncing context.  Private
	 * objects may be dirtied in syncing context, but only if they
	 * were already pre-dirtied in open context.
	 */
#ifdef DEBUG
	if (dn->dn_objset->os_dsl_dataset != NULL) {
		rrw_enter(&dn->dn_objset->os_dsl_dataset->ds_bp_rwlock,
		    RW_READER, FTAG);
	}
	ASSERT(!dmu_tx_is_syncing(tx) ||
	    BP_IS_HOLE(dn->dn_objset->os_rootbp) ||
	    DMU_OBJECT_IS_SPECIAL(dn->dn_object) ||
	    dn->dn_objset->os_dsl_dataset == NULL);
	if (dn->dn_objset->os_dsl_dataset != NULL)
		rrw_exit(&dn->dn_objset->os_dsl_dataset->ds_bp_rwlock, FTAG);
#endif
	/*
	 * We make this assert for private objects as well, but after we
	 * check if we're already dirty.  They are allowed to re-dirty
	 * in syncing context.
	 */
	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    dn->dn_dirtyctx == DN_UNDIRTIED || dn->dn_dirtyctx ==
	    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN));

	mutex_enter(&db->db_mtx);
	/*
	 * XXX make this true for indirects too?  The problem is that
	 * transactions created with dmu_tx_create_assigned() from
	 * syncing context don't bother holding ahead.
	 */
	ASSERT(db->db_level != 0 ||
	    db->db_state == DB_CACHED || db->db_state == DB_FILL ||
	    db->db_state == DB_NOFILL);

	mutex_enter(&dn->dn_mtx);
	/*
	 * Don't set dirtyctx to SYNC if we're just modifying this as we
	 * initialize the objset.
	 */
	if (dn->dn_dirtyctx == DN_UNDIRTIED) {
		if (dn->dn_objset->os_dsl_dataset != NULL) {
			rrw_enter(&dn->dn_objset->os_dsl_dataset->ds_bp_rwlock,
			    RW_READER, FTAG);
		}
		if (!BP_IS_HOLE(dn->dn_objset->os_rootbp)) {
			dn->dn_dirtyctx = (dmu_tx_is_syncing(tx) ?
			    DN_DIRTY_SYNC : DN_DIRTY_OPEN);
			ASSERT(dn->dn_dirtyctx_firstset == NULL);
			dn->dn_dirtyctx_firstset = kmem_alloc(1, KM_SLEEP);
		}
		if (dn->dn_objset->os_dsl_dataset != NULL) {
			rrw_exit(&dn->dn_objset->os_dsl_dataset->ds_bp_rwlock,
			    FTAG);
		}
	}
	mutex_exit(&dn->dn_mtx);

	if (db->db_blkid == DMU_SPILL_BLKID)
		dn->dn_have_spill = B_TRUE;

	/*
	 * If this buffer is already dirty, we're done.
	 */
	drp = &db->db_last_dirty;
	ASSERT(*drp == NULL || (*drp)->dr_txg <= tx->tx_txg ||
	    db->db.db_object == DMU_META_DNODE_OBJECT);
	while ((dr = *drp) != NULL && dr->dr_txg > tx->tx_txg)
		drp = &dr->dr_next;
	if (dr && dr->dr_txg == tx->tx_txg) {
		DB_DNODE_EXIT(db);

		dbuf_redirty(dr);
		mutex_exit(&db->db_mtx);
		return (dr);
	}

	/*
	 * Only valid if not already dirty.
	 */
	ASSERT(dn->dn_object == 0 ||
	    dn->dn_dirtyctx == DN_UNDIRTIED || dn->dn_dirtyctx ==
	    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN));

	ASSERT3U(dn->dn_nlevels, >, db->db_level);

	/*
	 * We should only be dirtying in syncing context if it's the
	 * mos or we're initializing the os or it's a special object.
	 * However, we are allowed to dirty in syncing context provided
	 * we already dirtied it in open context.  Hence we must make
	 * this assertion only if we're not already dirty.
	 */
	os = dn->dn_objset;
	VERIFY3U(tx->tx_txg, <=, spa_final_dirty_txg(os->os_spa));
#ifdef DEBUG
	if (dn->dn_objset->os_dsl_dataset != NULL)
		rrw_enter(&os->os_dsl_dataset->ds_bp_rwlock, RW_READER, FTAG);
	ASSERT(!dmu_tx_is_syncing(tx) || DMU_OBJECT_IS_SPECIAL(dn->dn_object) ||
	    os->os_dsl_dataset == NULL || BP_IS_HOLE(os->os_rootbp));
	if (dn->dn_objset->os_dsl_dataset != NULL)
		rrw_exit(&os->os_dsl_dataset->ds_bp_rwlock, FTAG);
#endif
	ASSERT(db->db.db_size != 0);

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	if (db->db_blkid != DMU_BONUS_BLKID) {
		dmu_objset_willuse_space(os, db->db.db_size, tx);
	}

	/*
	 * If this buffer is dirty in an old transaction group we need
	 * to make a copy of it so that the changes we make in this
	 * transaction group won't leak out when we sync the older txg.
	 */
	dr = kmem_zalloc(sizeof (dbuf_dirty_record_t), KM_SLEEP);
	list_link_init(&dr->dr_dirty_node);
	if (db->db_level == 0) {
		void *data_old = db->db_buf;

		if (db->db_state != DB_NOFILL) {
			if (db->db_blkid == DMU_BONUS_BLKID) {
				dbuf_fix_old_data(db, tx->tx_txg);
				data_old = db->db.db_data;
			} else if (db->db.db_object != DMU_META_DNODE_OBJECT) {
				/*
				 * Release the data buffer from the cache so
				 * that we can modify it without impacting
				 * possible other users of this cached data
				 * block.  Note that indirect blocks and
				 * private objects are not released until the
				 * syncing state (since they are only modified
				 * then).
				 */
				arc_release(db->db_buf, db);
				dbuf_fix_old_data(db, tx->tx_txg);
				data_old = db->db_buf;
			}
			ASSERT(data_old != NULL);
		}
		dr->dt.dl.dr_data = data_old;
	} else {
		mutex_init(&dr->dt.di.dr_mtx, NULL, MUTEX_DEFAULT, NULL);
		list_create(&dr->dt.di.dr_children,
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
	}
	if (db->db_blkid != DMU_BONUS_BLKID && os->os_dsl_dataset != NULL)
		dr->dr_accounted = db->db.db_size;
	dr->dr_dbuf = db;
	dr->dr_txg = tx->tx_txg;
	dr->dr_next = *drp;
	*drp = dr;

	/*
	 * We could have been freed_in_flight between the dbuf_noread
	 * and dbuf_dirty.  We win, as though the dbuf_noread() had
	 * happened after the free.
	 */
	if (db->db_level == 0 && db->db_blkid != DMU_BONUS_BLKID &&
	    db->db_blkid != DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		if (dn->dn_free_ranges[txgoff] != NULL) {
			range_tree_clear(dn->dn_free_ranges[txgoff],
			    db->db_blkid, 1);
		}
		mutex_exit(&dn->dn_mtx);
		db->db_freed_in_flight = FALSE;
	}

	/*
	 * This buffer is now part of this txg
	 */
	dbuf_add_ref(db, (void *)(uintptr_t)tx->tx_txg);
	db->db_dirtycnt += 1;
	ASSERT3U(db->db_dirtycnt, <=, 3);

	mutex_exit(&db->db_mtx);

	if (db->db_blkid == DMU_BONUS_BLKID ||
	    db->db_blkid == DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		ASSERT(!list_link_active(&dr->dr_dirty_node));
		list_insert_tail(&dn->dn_dirty_records[txgoff], dr);
		mutex_exit(&dn->dn_mtx);
		dnode_setdirty(dn, tx);
		DB_DNODE_EXIT(db);
		return (dr);
	}

	/*
	 * The dn_struct_rwlock prevents db_blkptr from changing
	 * due to a write from syncing context completing
	 * while we are running, so we want to acquire it before
	 * looking at db_blkptr.
	 */
	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}

	/*
	 * We need to hold the dn_struct_rwlock to make this assertion,
	 * because it protects dn_phys / dn_next_nlevels from changing.
	 */
	ASSERT((dn->dn_phys->dn_nlevels == 0 && db->db_level == 0) ||
	    dn->dn_phys->dn_nlevels > db->db_level ||
	    dn->dn_next_nlevels[txgoff] > db->db_level ||
	    dn->dn_next_nlevels[(tx->tx_txg-1) & TXG_MASK] > db->db_level ||
	    dn->dn_next_nlevels[(tx->tx_txg-2) & TXG_MASK] > db->db_level);

	/*
	 * If we are overwriting a dedup BP, then unless it is snapshotted,
	 * when we get to syncing context we will need to decrement its
	 * refcount in the DDT.  Prefetch the relevant DDT block so that
	 * syncing context won't have to wait for the i/o.
	 */
	ddt_prefetch(os->os_spa, db->db_blkptr);

	if (db->db_level == 0) {
		dnode_new_blkid(dn, db->db_blkid, tx, drop_struct_lock);
		ASSERT(dn->dn_maxblkid >= db->db_blkid);
	}

	if (db->db_level+1 < dn->dn_nlevels) {
		dmu_buf_impl_t *parent = db->db_parent;
		dbuf_dirty_record_t *di;
		int parent_held = FALSE;

		if (db->db_parent == NULL || db->db_parent == dn->dn_dbuf) {
			int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

			parent = dbuf_hold_level(dn, db->db_level+1,
			    db->db_blkid >> epbs, FTAG);
			ASSERT(parent != NULL);
			parent_held = TRUE;
		}
		if (drop_struct_lock)
			rw_exit(&dn->dn_struct_rwlock);
		ASSERT3U(db->db_level+1, ==, parent->db_level);
		di = dbuf_dirty(parent, tx);
		if (parent_held)
			dbuf_rele(parent, FTAG);

		mutex_enter(&db->db_mtx);
		/*
		 * Since we've dropped the mutex, it's possible that
		 * dbuf_undirty() might have changed this out from under us.
		 */
		if (db->db_last_dirty == dr ||
		    dn->dn_object == DMU_META_DNODE_OBJECT) {
			mutex_enter(&di->dt.di.dr_mtx);
			ASSERT3U(di->dr_txg, ==, tx->tx_txg);
			ASSERT(!list_link_active(&dr->dr_dirty_node));
			list_insert_tail(&di->dt.di.dr_children, dr);
			mutex_exit(&di->dt.di.dr_mtx);
			dr->dr_parent = di;
		}
		mutex_exit(&db->db_mtx);
	} else {
		ASSERT(db->db_level+1 == dn->dn_nlevels);
		ASSERT(db->db_blkid < dn->dn_nblkptr);
		ASSERT(db->db_parent == NULL || db->db_parent == dn->dn_dbuf);
		mutex_enter(&dn->dn_mtx);
		ASSERT(!list_link_active(&dr->dr_dirty_node));
		list_insert_tail(&dn->dn_dirty_records[txgoff], dr);
		mutex_exit(&dn->dn_mtx);
		if (drop_struct_lock)
			rw_exit(&dn->dn_struct_rwlock);
	}

	dnode_setdirty(dn, tx);
	DB_DNODE_EXIT(db);
	return (dr);
}

/*
 * Undirty a buffer in the transaction group referenced by the given
 * transaction.  Return whether this evicted the dbuf.
 */
static boolean_t
dbuf_undirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	dnode_t *dn;
	uint64_t txg = tx->tx_txg;
	dbuf_dirty_record_t *dr, **drp;

	ASSERT(txg != 0);

	/*
	 * Due to our use of dn_nlevels below, this can only be called
	 * in open context, unless we are operating on the MOS.
	 * From syncing context, dn_nlevels may be different from the
	 * dn_nlevels used when dbuf was dirtied.
	 */
	ASSERT(db->db_objset ==
	    dmu_objset_pool(db->db_objset)->dp_meta_objset ||
	    txg != spa_syncing_txg(dmu_objset_spa(db->db_objset)));
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	ASSERT0(db->db_level);
	ASSERT(MUTEX_HELD(&db->db_mtx));

	/*
	 * If this buffer is not dirty, we're done.
	 */
	for (drp = &db->db_last_dirty; (dr = *drp) != NULL; drp = &dr->dr_next)
		if (dr->dr_txg <= txg)
			break;
	if (dr == NULL || dr->dr_txg < txg)
		return (B_FALSE);
	ASSERT(dr->dr_txg == txg);
	ASSERT(dr->dr_dbuf == db);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	ASSERT(db->db.db_size != 0);

	dsl_pool_undirty_space(dmu_objset_pool(dn->dn_objset),
	    dr->dr_accounted, txg);

	*drp = dr->dr_next;

	/*
	 * Note that there are three places in dbuf_dirty()
	 * where this dirty record may be put on a list.
	 * Make sure to do a list_remove corresponding to
	 * every one of those list_insert calls.
	 */
	if (dr->dr_parent) {
		mutex_enter(&dr->dr_parent->dt.di.dr_mtx);
		list_remove(&dr->dr_parent->dt.di.dr_children, dr);
		mutex_exit(&dr->dr_parent->dt.di.dr_mtx);
	} else if (db->db_blkid == DMU_SPILL_BLKID ||
	    db->db_level + 1 == dn->dn_nlevels) {
		ASSERT(db->db_blkptr == NULL || db->db_parent == dn->dn_dbuf);
		mutex_enter(&dn->dn_mtx);
		list_remove(&dn->dn_dirty_records[txg & TXG_MASK], dr);
		mutex_exit(&dn->dn_mtx);
	}
	DB_DNODE_EXIT(db);

	if (db->db_state != DB_NOFILL) {
		dbuf_unoverride(dr);

		ASSERT(db->db_buf != NULL);
		ASSERT(dr->dt.dl.dr_data != NULL);
		if (dr->dt.dl.dr_data != db->db_buf)
			arc_buf_destroy(dr->dt.dl.dr_data, db);
	}

	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	ASSERT(db->db_dirtycnt > 0);
	db->db_dirtycnt -= 1;

	if (refcount_remove(&db->db_holds, (void *)(uintptr_t)txg) == 0) {
		ASSERT(db->db_state == DB_NOFILL || arc_released(db->db_buf));
		dbuf_destroy(db);
		return (B_TRUE);
	}

	return (B_FALSE);
}

void
dmu_buf_will_dirty(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	int rf = DB_RF_MUST_SUCCEED | DB_RF_NOPREFETCH;

	ASSERT(tx->tx_txg != 0);
	ASSERT(!refcount_is_zero(&db->db_holds));

	/*
	 * Quick check for dirtyness.  For already dirty blocks, this
	 * reduces runtime of this function by >90%, and overall performance
	 * by 50% for some workloads (e.g. file deletion with indirect blocks
	 * cached).
	 */
	mutex_enter(&db->db_mtx);
	dbuf_dirty_record_t *dr;
	for (dr = db->db_last_dirty;
	    dr != NULL && dr->dr_txg >= tx->tx_txg; dr = dr->dr_next) {
		/*
		 * It's possible that it is already dirty but not cached,
		 * because there are some calls to dbuf_dirty() that don't
		 * go through dmu_buf_will_dirty().
		 */
		if (dr->dr_txg == tx->tx_txg && db->db_state == DB_CACHED) {
			/* This dbuf is already dirty and cached. */
			dbuf_redirty(dr);
			mutex_exit(&db->db_mtx);
			return;
		}
	}
	mutex_exit(&db->db_mtx);

	DB_DNODE_ENTER(db);
	if (RW_WRITE_HELD(&DB_DNODE(db)->dn_struct_rwlock))
		rf |= DB_RF_HAVESTRUCT;
	DB_DNODE_EXIT(db);
	(void) dbuf_read(db, NULL, rf);
	(void) dbuf_dirty(db, tx);
}

void
dmu_buf_will_not_fill(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	db->db_state = DB_NOFILL;

	dmu_buf_will_fill(db_fake, tx);
}

void
dmu_buf_will_fill(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	ASSERT(tx->tx_txg != 0);
	ASSERT(db->db_level == 0);
	ASSERT(!refcount_is_zero(&db->db_holds));

	ASSERT(db->db.db_object != DMU_META_DNODE_OBJECT ||
	    dmu_tx_private_ok(tx));

	dbuf_noread(db);
	(void) dbuf_dirty(db, tx);
}

#pragma weak dmu_buf_fill_done = dbuf_fill_done
/* ARGSUSED */
void
dbuf_fill_done(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	mutex_enter(&db->db_mtx);
	DBUF_VERIFY(db);

	if (db->db_state == DB_FILL) {
		if (db->db_level == 0 && db->db_freed_in_flight) {
			ASSERT(db->db_blkid != DMU_BONUS_BLKID);
			/* we were freed while filling */
			/* XXX dbuf_undirty? */
			bzero(db->db.db_data, db->db.db_size);
			db->db_freed_in_flight = FALSE;
		}
		db->db_state = DB_CACHED;
		cv_broadcast(&db->db_changed);
	}
	mutex_exit(&db->db_mtx);
}

void
dmu_buf_write_embedded(dmu_buf_t *dbuf, void *data,
    bp_embedded_type_t etype, enum zio_compress comp,
    int uncompressed_size, int compressed_size, int byteorder,
    dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbuf;
	struct dirty_leaf *dl;
	dmu_object_type_t type;

	if (etype == BP_EMBEDDED_TYPE_DATA) {
		ASSERT(spa_feature_is_active(dmu_objset_spa(db->db_objset),
		    SPA_FEATURE_EMBEDDED_DATA));
	}

	DB_DNODE_ENTER(db);
	type = DB_DNODE(db)->dn_type;
	DB_DNODE_EXIT(db);

	ASSERT0(db->db_level);
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);

	dmu_buf_will_not_fill(dbuf, tx);

	ASSERT3U(db->db_last_dirty->dr_txg, ==, tx->tx_txg);
	dl = &db->db_last_dirty->dt.dl;
	encode_embedded_bp_compressed(&dl->dr_overridden_by,
	    data, comp, uncompressed_size, compressed_size);
	BPE_SET_ETYPE(&dl->dr_overridden_by, etype);
	BP_SET_TYPE(&dl->dr_overridden_by, type);
	BP_SET_LEVEL(&dl->dr_overridden_by, 0);
	BP_SET_BYTEORDER(&dl->dr_overridden_by, byteorder);

	dl->dr_override_state = DR_OVERRIDDEN;
	dl->dr_overridden_by.blk_birth = db->db_last_dirty->dr_txg;
}

/*
 * Directly assign a provided arc buf to a given dbuf if it's not referenced
 * by anybody except our caller. Otherwise copy arcbuf's contents to dbuf.
 */
void
dbuf_assign_arcbuf(dmu_buf_impl_t *db, arc_buf_t *buf, dmu_tx_t *tx)
{
	ASSERT(!refcount_is_zero(&db->db_holds));
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	ASSERT(db->db_level == 0);
	ASSERT3U(dbuf_is_metadata(db), ==, arc_is_metadata(buf));
	ASSERT(buf != NULL);
	ASSERT(arc_buf_lsize(buf) == db->db.db_size);
	ASSERT(tx->tx_txg != 0);

	arc_return_buf(buf, db);
	ASSERT(arc_released(buf));

	mutex_enter(&db->db_mtx);

	while (db->db_state == DB_READ || db->db_state == DB_FILL)
		cv_wait(&db->db_changed, &db->db_mtx);

	ASSERT(db->db_state == DB_CACHED || db->db_state == DB_UNCACHED);

	if (db->db_state == DB_CACHED &&
	    refcount_count(&db->db_holds) - 1 > db->db_dirtycnt) {
		mutex_exit(&db->db_mtx);
		(void) dbuf_dirty(db, tx);
		bcopy(buf->b_data, db->db.db_data, db->db.db_size);
		arc_buf_destroy(buf, db);
		xuio_stat_wbuf_copied();
		return;
	}

	xuio_stat_wbuf_nocopy();
	if (db->db_state == DB_CACHED) {
		dbuf_dirty_record_t *dr = db->db_last_dirty;

		ASSERT(db->db_buf != NULL);
		if (dr != NULL && dr->dr_txg == tx->tx_txg) {
			ASSERT(dr->dt.dl.dr_data == db->db_buf);
			if (!arc_released(db->db_buf)) {
				ASSERT(dr->dt.dl.dr_override_state ==
				    DR_OVERRIDDEN);
				arc_release(db->db_buf, db);
			}
			dr->dt.dl.dr_data = buf;
			arc_buf_destroy(db->db_buf, db);
		} else if (dr == NULL || dr->dt.dl.dr_data != db->db_buf) {
			arc_release(db->db_buf, db);
			arc_buf_destroy(db->db_buf, db);
		}
		db->db_buf = NULL;
	}
	ASSERT(db->db_buf == NULL);
	dbuf_set_data(db, buf);
	db->db_state = DB_FILL;
	mutex_exit(&db->db_mtx);
	(void) dbuf_dirty(db, tx);
	dmu_buf_fill_done(&db->db, tx);
}

void
dbuf_destroy(dmu_buf_impl_t *db)
{
	dnode_t *dn;
	dmu_buf_impl_t *parent = db->db_parent;
	dmu_buf_impl_t *dndb;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(refcount_is_zero(&db->db_holds));

	if (db->db_buf != NULL) {
		arc_buf_destroy(db->db_buf, db);
		db->db_buf = NULL;
	}

	if (db->db_blkid == DMU_BONUS_BLKID) {
		int slots = DB_DNODE(db)->dn_num_slots;
		int bonuslen = DN_SLOTS_TO_BONUSLEN(slots);
		if (db->db.db_data != NULL) {
			zio_buf_free(db->db.db_data, bonuslen);
			arc_space_return(bonuslen, ARC_SPACE_BONUS);
			db->db_state = DB_UNCACHED;
		}
	}

	dbuf_clear_data(db);

	if (multilist_link_active(&db->db_cache_link)) {
		ASSERT(db->db_caching_status == DB_DBUF_CACHE ||
		    db->db_caching_status == DB_DBUF_METADATA_CACHE);

		multilist_remove(dbuf_caches[db->db_caching_status].cache, db);
		(void) refcount_remove_many(
		    &dbuf_caches[db->db_caching_status].size,
		    db->db.db_size, db);

		if (db->db_caching_status == DB_DBUF_METADATA_CACHE) {
			DBUF_STAT_BUMPDOWN(metadata_cache_count);
		} else {
			DBUF_STAT_BUMPDOWN(cache_levels[db->db_level]);
			DBUF_STAT_BUMPDOWN(cache_count);
			DBUF_STAT_DECR(cache_levels_bytes[db->db_level],
			    db->db.db_size);
		}
		db->db_caching_status = DB_NO_CACHE;
	}

	ASSERT(db->db_state == DB_UNCACHED || db->db_state == DB_NOFILL);
	ASSERT(db->db_data_pending == NULL);

	db->db_state = DB_EVICTING;
	db->db_blkptr = NULL;

	/*
	 * Now that db_state is DB_EVICTING, nobody else can find this via
	 * the hash table.  We can now drop db_mtx, which allows us to
	 * acquire the dn_dbufs_mtx.
	 */
	mutex_exit(&db->db_mtx);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	dndb = dn->dn_dbuf;
	if (db->db_blkid != DMU_BONUS_BLKID) {
		boolean_t needlock = !MUTEX_HELD(&dn->dn_dbufs_mtx);
		if (needlock)
			mutex_enter(&dn->dn_dbufs_mtx);
		avl_remove(&dn->dn_dbufs, db);
		atomic_dec_32(&dn->dn_dbufs_count);
		membar_producer();
		DB_DNODE_EXIT(db);
		if (needlock)
			mutex_exit(&dn->dn_dbufs_mtx);
		/*
		 * Decrementing the dbuf count means that the hold corresponding
		 * to the removed dbuf is no longer discounted in dnode_move(),
		 * so the dnode cannot be moved until after we release the hold.
		 * The membar_producer() ensures visibility of the decremented
		 * value in dnode_move(), since DB_DNODE_EXIT doesn't actually
		 * release any lock.
		 */
		mutex_enter(&dn->dn_mtx);
		dnode_rele_and_unlock(dn, db, B_TRUE);
		db->db_dnode_handle = NULL;

		dbuf_hash_remove(db);
	} else {
		DB_DNODE_EXIT(db);
	}

	ASSERT(refcount_is_zero(&db->db_holds));

	db->db_parent = NULL;

	ASSERT(db->db_buf == NULL);
	ASSERT(db->db.db_data == NULL);
	ASSERT(db->db_hash_next == NULL);
	ASSERT(db->db_blkptr == NULL);
	ASSERT(db->db_data_pending == NULL);
	ASSERT3U(db->db_caching_status, ==, DB_NO_CACHE);
	ASSERT(!multilist_link_active(&db->db_cache_link));

	kmem_cache_free(dbuf_kmem_cache, db);
	arc_space_return(sizeof (dmu_buf_impl_t), ARC_SPACE_DBUF);

	/*
	 * If this dbuf is referenced from an indirect dbuf,
	 * decrement the ref count on the indirect dbuf.
	 */
	if (parent && parent != dndb) {
		mutex_enter(&parent->db_mtx);
		dbuf_rele_and_unlock(parent, db, B_TRUE);
	}
}

/*
 * Note: While bpp will always be updated if the function returns success,
 * parentp will not be updated if the dnode does not have dn_dbuf filled in;
 * this happens when the dnode is the meta-dnode, or a userused or groupused
 * object.
 */
__attribute__((always_inline))
static inline int
dbuf_findbp(dnode_t *dn, int level, uint64_t blkid, int fail_sparse,
    dmu_buf_impl_t **parentp, blkptr_t **bpp, struct dbuf_hold_impl_data *dh)
{
	*parentp = NULL;
	*bpp = NULL;

	ASSERT(blkid != DMU_BONUS_BLKID);

	if (blkid == DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		if (dn->dn_have_spill &&
		    (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR))
			*bpp = DN_SPILL_BLKPTR(dn->dn_phys);
		else
			*bpp = NULL;
		dbuf_add_ref(dn->dn_dbuf, NULL);
		*parentp = dn->dn_dbuf;
		mutex_exit(&dn->dn_mtx);
		return (0);
	}

	int nlevels =
	    (dn->dn_phys->dn_nlevels == 0) ? 1 : dn->dn_phys->dn_nlevels;
	int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

	ASSERT3U(level * epbs, <, 64);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	/*
	 * This assertion shouldn't trip as long as the max indirect block size
	 * is less than 1M.  The reason for this is that up to that point,
	 * the number of levels required to address an entire object with blocks
	 * of size SPA_MINBLOCKSIZE satisfies nlevels * epbs + 1 <= 64.  In
	 * other words, if N * epbs + 1 > 64, then if (N-1) * epbs + 1 > 55
	 * (i.e. we can address the entire object), objects will all use at most
	 * N-1 levels and the assertion won't overflow.  However, once epbs is
	 * 13, 4 * 13 + 1 = 53, but 5 * 13 + 1 = 66.  Then, 4 levels will not be
	 * enough to address an entire object, so objects will have 5 levels,
	 * but then this assertion will overflow.
	 *
	 * All this is to say that if we ever increase DN_MAX_INDBLKSHIFT, we
	 * need to redo this logic to handle overflows.
	 */
	ASSERT(level >= nlevels ||
	    ((nlevels - level - 1) * epbs) +
	    highbit64(dn->dn_phys->dn_nblkptr) <= 64);
	if (level >= nlevels ||
	    blkid >= ((uint64_t)dn->dn_phys->dn_nblkptr <<
	    ((nlevels - level - 1) * epbs)) ||
	    (fail_sparse &&
	    blkid > (dn->dn_phys->dn_maxblkid >> (level * epbs)))) {
		/* the buffer has no parent yet */
		return (SET_ERROR(ENOENT));
	} else if (level < nlevels-1) {
		/* this block is referenced from an indirect block */
		int err;
		if (dh == NULL) {
			err = dbuf_hold_impl(dn, level+1,
			    blkid >> epbs, fail_sparse, FALSE, NULL, parentp);
		} else {
			__dbuf_hold_impl_init(dh + 1, dn, dh->dh_level + 1,
			    blkid >> epbs, fail_sparse, FALSE, NULL,
			    parentp, dh->dh_depth + 1);
			err = __dbuf_hold_impl(dh + 1);
		}
		if (err)
			return (err);
		err = dbuf_read(*parentp, NULL,
		    (DB_RF_HAVESTRUCT | DB_RF_NOPREFETCH | DB_RF_CANFAIL));
		if (err) {
			dbuf_rele(*parentp, NULL);
			*parentp = NULL;
			return (err);
		}
		*bpp = ((blkptr_t *)(*parentp)->db.db_data) +
		    (blkid & ((1ULL << epbs) - 1));
		if (blkid > (dn->dn_phys->dn_maxblkid >> (level * epbs)))
			ASSERT(BP_IS_HOLE(*bpp));
		return (0);
	} else {
		/* the block is referenced from the dnode */
		ASSERT3U(level, ==, nlevels-1);
		ASSERT(dn->dn_phys->dn_nblkptr == 0 ||
		    blkid < dn->dn_phys->dn_nblkptr);
		if (dn->dn_dbuf) {
			dbuf_add_ref(dn->dn_dbuf, NULL);
			*parentp = dn->dn_dbuf;
		}
		*bpp = &dn->dn_phys->dn_blkptr[blkid];
		return (0);
	}
}

static dmu_buf_impl_t *
dbuf_create(dnode_t *dn, uint8_t level, uint64_t blkid,
    dmu_buf_impl_t *parent, blkptr_t *blkptr)
{
	objset_t *os = dn->dn_objset;
	dmu_buf_impl_t *db, *odb;

	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT(dn->dn_type != DMU_OT_NONE);

	db = kmem_cache_alloc(dbuf_kmem_cache, KM_SLEEP);

	db->db_objset = os;
	db->db.db_object = dn->dn_object;
	db->db_level = level;
	db->db_blkid = blkid;
	db->db_last_dirty = NULL;
	db->db_dirtycnt = 0;
	db->db_dnode_handle = dn->dn_handle;
	db->db_parent = parent;
	db->db_blkptr = blkptr;

	db->db_user = NULL;
	db->db_user_immediate_evict = FALSE;
	db->db_freed_in_flight = FALSE;
	db->db_pending_evict = FALSE;

	if (blkid == DMU_BONUS_BLKID) {
		ASSERT3P(parent, ==, dn->dn_dbuf);
		db->db.db_size = DN_SLOTS_TO_BONUSLEN(dn->dn_num_slots) -
		    (dn->dn_nblkptr-1) * sizeof (blkptr_t);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		db->db.db_offset = DMU_BONUS_BLKID;
		db->db_state = DB_UNCACHED;
		db->db_caching_status = DB_NO_CACHE;
		/* the bonus dbuf is not placed in the hash table */
		arc_space_consume(sizeof (dmu_buf_impl_t), ARC_SPACE_DBUF);
		return (db);
	} else if (blkid == DMU_SPILL_BLKID) {
		db->db.db_size = (blkptr != NULL) ?
		    BP_GET_LSIZE(blkptr) : SPA_MINBLOCKSIZE;
		db->db.db_offset = 0;
	} else {
		int blocksize =
		    db->db_level ? 1 << dn->dn_indblkshift : dn->dn_datablksz;
		db->db.db_size = blocksize;
		db->db.db_offset = db->db_blkid * blocksize;
	}

	/*
	 * Hold the dn_dbufs_mtx while we get the new dbuf
	 * in the hash table *and* added to the dbufs list.
	 * This prevents a possible deadlock with someone
	 * trying to look up this dbuf before its added to the
	 * dn_dbufs list.
	 */
	mutex_enter(&dn->dn_dbufs_mtx);
	db->db_state = DB_EVICTING;
	if ((odb = dbuf_hash_insert(db)) != NULL) {
		/* someone else inserted it first */
		kmem_cache_free(dbuf_kmem_cache, db);
		mutex_exit(&dn->dn_dbufs_mtx);
		DBUF_STAT_BUMP(hash_insert_race);
		return (odb);
	}
	avl_add(&dn->dn_dbufs, db);

	db->db_state = DB_UNCACHED;
	db->db_caching_status = DB_NO_CACHE;
	mutex_exit(&dn->dn_dbufs_mtx);
	arc_space_consume(sizeof (dmu_buf_impl_t), ARC_SPACE_DBUF);

	if (parent && parent != dn->dn_dbuf)
		dbuf_add_ref(parent, db);

	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    refcount_count(&dn->dn_holds) > 0);
	(void) refcount_add(&dn->dn_holds, db);
	atomic_inc_32(&dn->dn_dbufs_count);

	dprintf_dbuf(db, "db=%p\n", db);

	return (db);
}

typedef struct dbuf_prefetch_arg {
	spa_t *dpa_spa;	/* The spa to issue the prefetch in. */
	zbookmark_phys_t dpa_zb; /* The target block to prefetch. */
	int dpa_epbs; /* Entries (blkptr_t's) Per Block Shift. */
	int dpa_curlevel; /* The current level that we're reading */
	dnode_t *dpa_dnode; /* The dnode associated with the prefetch */
	zio_priority_t dpa_prio; /* The priority I/Os should be issued at. */
	zio_t *dpa_zio; /* The parent zio_t for all prefetches. */
	arc_flags_t dpa_aflags; /* Flags to pass to the final prefetch. */
} dbuf_prefetch_arg_t;

/*
 * Actually issue the prefetch read for the block given.
 */
static void
dbuf_issue_final_prefetch(dbuf_prefetch_arg_t *dpa, blkptr_t *bp)
{
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp))
		return;

	arc_flags_t aflags =
	    dpa->dpa_aflags | ARC_FLAG_NOWAIT | ARC_FLAG_PREFETCH;

	ASSERT3U(dpa->dpa_curlevel, ==, BP_GET_LEVEL(bp));
	ASSERT3U(dpa->dpa_curlevel, ==, dpa->dpa_zb.zb_level);
	ASSERT(dpa->dpa_zio != NULL);
	(void) arc_read(dpa->dpa_zio, dpa->dpa_spa, bp, NULL, NULL,
	    dpa->dpa_prio, ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
	    &aflags, &dpa->dpa_zb);
}

/*
 * Called when an indirect block above our prefetch target is read in.  This
 * will either read in the next indirect block down the tree or issue the actual
 * prefetch if the next block down is our target.
 */
static void
dbuf_prefetch_indirect_done(zio_t *zio, const zbookmark_phys_t *zb,
    const blkptr_t *iobp, arc_buf_t *abuf, void *private)
{
	dbuf_prefetch_arg_t *dpa = private;

	ASSERT3S(dpa->dpa_zb.zb_level, <, dpa->dpa_curlevel);
	ASSERT3S(dpa->dpa_curlevel, >, 0);

	if (abuf == NULL) {
		ASSERT(zio == NULL || zio->io_error != 0);
		kmem_free(dpa, sizeof (*dpa));
		return;
	}
	ASSERT(zio == NULL || zio->io_error == 0);

	/*
	 * The dpa_dnode is only valid if we are called with a NULL
	 * zio. This indicates that the arc_read() returned without
	 * first calling zio_read() to issue a physical read. Once
	 * a physical read is made the dpa_dnode must be invalidated
	 * as the locks guarding it may have been dropped. If the
	 * dpa_dnode is still valid, then we want to add it to the dbuf
	 * cache. To do so, we must hold the dbuf associated with the block
	 * we just prefetched, read its contents so that we associate it
	 * with an arc_buf_t, and then release it.
	 */
	if (zio != NULL) {
		ASSERT3S(BP_GET_LEVEL(zio->io_bp), ==, dpa->dpa_curlevel);
		if (zio->io_flags & ZIO_FLAG_RAW) {
			ASSERT3U(BP_GET_PSIZE(zio->io_bp), ==, zio->io_size);
		} else {
			ASSERT3U(BP_GET_LSIZE(zio->io_bp), ==, zio->io_size);
		}
		ASSERT3P(zio->io_spa, ==, dpa->dpa_spa);

		dpa->dpa_dnode = NULL;
	} else if (dpa->dpa_dnode != NULL) {
		uint64_t curblkid = dpa->dpa_zb.zb_blkid >>
		    (dpa->dpa_epbs * (dpa->dpa_curlevel -
		    dpa->dpa_zb.zb_level));
		dmu_buf_impl_t *db = dbuf_hold_level(dpa->dpa_dnode,
		    dpa->dpa_curlevel, curblkid, FTAG);
		(void) dbuf_read(db, NULL,
		    DB_RF_MUST_SUCCEED | DB_RF_NOPREFETCH | DB_RF_HAVESTRUCT);
		dbuf_rele(db, FTAG);
	}

	if (abuf == NULL) {
		kmem_free(dpa, sizeof(*dpa));
		return;
	}
	
	dpa->dpa_curlevel--;

	uint64_t nextblkid = dpa->dpa_zb.zb_blkid >>
	    (dpa->dpa_epbs * (dpa->dpa_curlevel - dpa->dpa_zb.zb_level));
	blkptr_t *bp = ((blkptr_t *)abuf->b_data) +
	    P2PHASE(nextblkid, 1ULL << dpa->dpa_epbs);
	if (BP_IS_HOLE(bp)) {
		kmem_free(dpa, sizeof (*dpa));
	} else if (dpa->dpa_curlevel == dpa->dpa_zb.zb_level) {
		ASSERT3U(nextblkid, ==, dpa->dpa_zb.zb_blkid);
		dbuf_issue_final_prefetch(dpa, bp);
		kmem_free(dpa, sizeof (*dpa));
	} else {
		arc_flags_t iter_aflags = ARC_FLAG_NOWAIT;
		zbookmark_phys_t zb;

		/* flag if L2ARC eligible, l2arc_noprefetch then decides */
		if (dpa->dpa_aflags & ARC_FLAG_L2CACHE)
			iter_aflags |= ARC_FLAG_L2CACHE;

		ASSERT3U(dpa->dpa_curlevel, ==, BP_GET_LEVEL(bp));

		SET_BOOKMARK(&zb, dpa->dpa_zb.zb_objset,
		    dpa->dpa_zb.zb_object, dpa->dpa_curlevel, nextblkid);

		(void) arc_read(dpa->dpa_zio, dpa->dpa_spa,
		    bp, dbuf_prefetch_indirect_done, dpa, dpa->dpa_prio,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
		    &iter_aflags, &zb);
	}

	arc_buf_destroy(abuf, private);
}

/*
 * Issue prefetch reads for the given block on the given level.  If the indirect
 * blocks above that block are not in memory, we will read them in
 * asynchronously.  As a result, this call never blocks waiting for a read to
 * complete.
 */
void
dbuf_prefetch(dnode_t *dn, int64_t level, uint64_t blkid, zio_priority_t prio,
    arc_flags_t aflags)
{
	blkptr_t bp;
	int epbs, nlevels, curlevel;
	uint64_t curblkid;

	ASSERT(blkid != DMU_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));

	if (blkid > dn->dn_maxblkid)
		return;

	if (dnode_block_freed(dn, blkid))
		return;

	/*
	 * This dnode hasn't been written to disk yet, so there's nothing to
	 * prefetch.
	 */
	nlevels = dn->dn_phys->dn_nlevels;
	if (level >= nlevels || dn->dn_phys->dn_nblkptr == 0)
		return;

	epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
	if (dn->dn_phys->dn_maxblkid < blkid << (epbs * level))
		return;

	dmu_buf_impl_t *db = dbuf_find(dn->dn_objset, dn->dn_object,
	    level, blkid);
	if (db != NULL) {
		mutex_exit(&db->db_mtx);
		/*
		 * This dbuf already exists.  It is either CACHED, or
		 * (we assume) about to be read or filled.
		 */
		return;
	}

	/*
	 * Find the closest ancestor (indirect block) of the target block
	 * that is present in the cache.  In this indirect block, we will
	 * find the bp that is at curlevel, curblkid.
	 */
	curlevel = level;
	curblkid = blkid;
	while (curlevel < nlevels - 1) {
		int parent_level = curlevel + 1;
		uint64_t parent_blkid = curblkid >> epbs;
		dmu_buf_impl_t *db;

		if (dbuf_hold_impl(dn, parent_level, parent_blkid,
		    FALSE, TRUE, FTAG, &db) == 0) {
			blkptr_t *bpp = db->db_buf->b_data;
			bp = bpp[P2PHASE(curblkid, 1 << epbs)];
			dbuf_rele(db, FTAG);
			break;
		}

		curlevel = parent_level;
		curblkid = parent_blkid;
	}

	if (curlevel == nlevels - 1) {
		/* No cached indirect blocks found. */
		ASSERT3U(curblkid, <, dn->dn_phys->dn_nblkptr);
		bp = dn->dn_phys->dn_blkptr[curblkid];
	}
	if (BP_IS_HOLE(&bp))
		return;

	ASSERT3U(curlevel, ==, BP_GET_LEVEL(&bp));

	zio_t *pio = zio_root(dmu_objset_spa(dn->dn_objset), NULL, NULL,
	    ZIO_FLAG_CANFAIL);

	dbuf_prefetch_arg_t *dpa = kmem_zalloc(sizeof (*dpa), KM_SLEEP);
	dsl_dataset_t *ds = dn->dn_objset->os_dsl_dataset;
	SET_BOOKMARK(&dpa->dpa_zb, ds != NULL ? ds->ds_object : DMU_META_OBJSET,
	    dn->dn_object, level, blkid);
	dpa->dpa_curlevel = curlevel;
	dpa->dpa_prio = prio;
	dpa->dpa_aflags = aflags;
	dpa->dpa_spa = dn->dn_objset->os_spa;
	dpa->dpa_dnode = dn;
	dpa->dpa_epbs = epbs;
	dpa->dpa_zio = pio;

	/* flag if L2ARC eligible, l2arc_noprefetch then decides */
	if (DNODE_LEVEL_IS_L2CACHEABLE(dn, level))
		dpa->dpa_aflags |= ARC_FLAG_L2CACHE;

	/*
	 * If we have the indirect just above us, no need to do the asynchronous
	 * prefetch chain; we'll just run the last step ourselves.  If we're at
	 * a higher level, though, we want to issue the prefetches for all the
	 * indirect blocks asynchronously, so we can go on with whatever we were
	 * doing.
	 */
	if (curlevel == level) {
		ASSERT3U(curblkid, ==, blkid);
		dbuf_issue_final_prefetch(dpa, &bp);
		kmem_free(dpa, sizeof (*dpa));
	} else {
		arc_flags_t iter_aflags = ARC_FLAG_NOWAIT;
		zbookmark_phys_t zb;

		/* flag if L2ARC eligible, l2arc_noprefetch then decides */
		if (DNODE_LEVEL_IS_L2CACHEABLE(dn, level))
			iter_aflags |= ARC_FLAG_L2CACHE;

		SET_BOOKMARK(&zb, ds != NULL ? ds->ds_object : DMU_META_OBJSET,
		    dn->dn_object, curlevel, curblkid);
		(void) arc_read(dpa->dpa_zio, dpa->dpa_spa,
		    &bp, dbuf_prefetch_indirect_done, dpa, prio,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
		    &iter_aflags, &zb);
	}
	/*
	 * We use pio here instead of dpa_zio since it's possible that
	 * dpa may have already been freed.
	 */
	zio_nowait(pio);
}

#define	DBUF_HOLD_IMPL_MAX_DEPTH	20

/*
 * Helper function for __dbuf_hold_impl() to copy a buffer. Handles
 * the case of encrypted, compressed and uncompressed buffers by
 * allocating the new buffer, respectively, with arc_alloc_raw_buf(),
 * arc_alloc_compressed_buf() or arc_alloc_buf().*
 *
 * NOTE: Declared noinline to avoid stack bloat in __dbuf_hold_impl().
 */
noinline static void
dbuf_hold_copy(struct dbuf_hold_impl_data *dh)
{
	dnode_t *dn = dh->dh_dn;
	dmu_buf_impl_t *db = dh->dh_db;
	dbuf_dirty_record_t *dr = dh->dh_dr;
	arc_buf_t *data = dr->dt.dl.dr_data;

	enum zio_compress compress_type = arc_get_compression(data);

	if (compress_type != ZIO_COMPRESS_OFF) {
		dbuf_set_data(db, arc_alloc_compressed_buf(
		    dn->dn_objset->os_spa, db, arc_buf_size(data),
		    arc_buf_lsize(data), compress_type));
	} else {
		dbuf_set_data(db, arc_alloc_buf(dn->dn_objset->os_spa, db,
		    DBUF_GET_BUFC_TYPE(db), db->db.db_size));
	}

	bcopy(data->b_data, db->db.db_data, arc_buf_size(data));
}

/*
 * Returns with db_holds incremented, and db_mtx not held.
 * Note: dn_struct_rwlock must be held.
 */
static int
__dbuf_hold_impl(struct dbuf_hold_impl_data *dh)
{
	ASSERT3S(dh->dh_depth, <, DBUF_HOLD_IMPL_MAX_DEPTH);
	dh->dh_parent = NULL;

	ASSERT(dh->dh_blkid != DMU_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dh->dh_dn->dn_struct_rwlock));
	ASSERT3U(dh->dh_dn->dn_nlevels, >, dh->dh_level);

	*(dh->dh_dbp) = NULL;

	/* dbuf_find() returns with db_mtx held */
	dh->dh_db = dbuf_find(dh->dh_dn->dn_objset, dh->dh_dn->dn_object,
	    dh->dh_level, dh->dh_blkid);

	if (dh->dh_db == NULL) {
		dh->dh_bp = NULL;

		if (dh->dh_fail_uncached)
			return (SET_ERROR(ENOENT));

		ASSERT3P(dh->dh_parent, ==, NULL);
		dh->dh_err = dbuf_findbp(dh->dh_dn, dh->dh_level, dh->dh_blkid,
		    dh->dh_fail_sparse, &dh->dh_parent, &dh->dh_bp, dh);
		if (dh->dh_fail_sparse) {
			if (dh->dh_err == 0 &&
			    dh->dh_bp && BP_IS_HOLE(dh->dh_bp))
				dh->dh_err = SET_ERROR(ENOENT);
			if (dh->dh_err) {
				if (dh->dh_parent)
					dbuf_rele(dh->dh_parent, NULL);
				return (dh->dh_err);
			}
		}
		if (dh->dh_err && dh->dh_err != ENOENT)
			return (dh->dh_err);
		dh->dh_db = dbuf_create(dh->dh_dn, dh->dh_level, dh->dh_blkid,
		    dh->dh_parent, dh->dh_bp);
	}

	if (dh->dh_fail_uncached && dh->dh_db->db_state != DB_CACHED) {
		mutex_exit(&dh->dh_db->db_mtx);
		return (SET_ERROR(ENOENT));
	}

	if (dh->dh_db->db_buf != NULL) {
		arc_buf_access(dh->dh_db->db_buf);
		ASSERT3P(dh->dh_db->db.db_data, ==, dh->dh_db->db_buf->b_data);
	}

	ASSERT(dh->dh_db->db_buf == NULL || arc_referenced(dh->dh_db->db_buf));

	/*
	 * If this buffer is currently syncing out, and we are are
	 * still referencing it from db_data, we need to make a copy
	 * of it in case we decide we want to dirty it again in this txg.
	 */
	if (dh->dh_db->db_level == 0 &&
	    dh->dh_db->db_blkid != DMU_BONUS_BLKID &&
	    dh->dh_dn->dn_object != DMU_META_DNODE_OBJECT &&
	    dh->dh_db->db_state == DB_CACHED && dh->dh_db->db_data_pending) {
		dh->dh_dr = dh->dh_db->db_data_pending;
		if (dh->dh_dr->dt.dl.dr_data == dh->dh_db->db_buf)
			dbuf_hold_copy(dh);
	}

	if (multilist_link_active(&dh->dh_db->db_cache_link)) {
		ASSERT(refcount_is_zero(&dh->dh_db->db_holds));
		ASSERT(dh->dh_db->db_caching_status == DB_DBUF_CACHE ||
		    dh->dh_db->db_caching_status == DB_DBUF_METADATA_CACHE);

		multilist_remove(
		    dbuf_caches[dh->dh_db->db_caching_status].cache,
		    dh->dh_db);
		(void) refcount_remove_many(
		    &dbuf_caches[dh->dh_db->db_caching_status].size,
		    dh->dh_db->db.db_size, dh->dh_db);

		if (dh->dh_db->db_caching_status == DB_DBUF_METADATA_CACHE) {
			DBUF_STAT_BUMPDOWN(metadata_cache_count);
		} else {
			DBUF_STAT_BUMPDOWN(cache_levels[dh->dh_db->db_level]);
			DBUF_STAT_BUMPDOWN(cache_count);
			DBUF_STAT_DECR(cache_levels_bytes[dh->dh_db->db_level],
			    dh->dh_db->db.db_size);
		}
		dh->dh_db->db_caching_status = DB_NO_CACHE;
	}
	(void) refcount_add(&dh->dh_db->db_holds, dh->dh_tag);
	DBUF_VERIFY(dh->dh_db);
	mutex_exit(&dh->dh_db->db_mtx);

	/* NOTE: we can't rele the parent until after we drop the db_mtx */
	if (dh->dh_parent)
		dbuf_rele(dh->dh_parent, NULL);

	ASSERT3P(DB_DNODE(dh->dh_db), ==, dh->dh_dn);
	ASSERT3U(dh->dh_db->db_blkid, ==, dh->dh_blkid);
	ASSERT3U(dh->dh_db->db_level, ==, dh->dh_level);
	*(dh->dh_dbp) = dh->dh_db;

	return (0);
}

/*
 * The following code preserves the recursive function dbuf_hold_impl()
 * but moves the local variables AND function arguments to the heap to
 * minimize the stack frame size.  Enough space is initially allocated
 * on the stack for 20 levels of recursion.
 */
int
dbuf_hold_impl(dnode_t *dn, uint8_t level, uint64_t blkid,
    boolean_t fail_sparse, boolean_t fail_uncached,
    void *tag, dmu_buf_impl_t **dbp)
{
	struct dbuf_hold_impl_data *dh;
	int error;

	dh = kmem_alloc(sizeof (struct dbuf_hold_impl_data) *
	    DBUF_HOLD_IMPL_MAX_DEPTH, KM_SLEEP);
	__dbuf_hold_impl_init(dh, dn, level, blkid, fail_sparse,
	    fail_uncached, tag, dbp, 0);

	error = __dbuf_hold_impl(dh);

	kmem_free(dh, sizeof (struct dbuf_hold_impl_data) *
	    DBUF_HOLD_IMPL_MAX_DEPTH);

	return (error);
}

static void
__dbuf_hold_impl_init(struct dbuf_hold_impl_data *dh,
    dnode_t *dn, uint8_t level, uint64_t blkid,
    boolean_t fail_sparse, boolean_t fail_uncached,
    void *tag, dmu_buf_impl_t **dbp, int depth)
{
	dh->dh_dn = dn;
	dh->dh_level = level;
	dh->dh_blkid = blkid;

	dh->dh_fail_sparse = fail_sparse;
	dh->dh_fail_uncached = fail_uncached;

	dh->dh_tag = tag;
	dh->dh_dbp = dbp;

	dh->dh_db = NULL;
	dh->dh_parent = NULL;
	dh->dh_bp = NULL;
	dh->dh_err = 0;
	dh->dh_dr = NULL;

	dh->dh_depth = depth;
}

dmu_buf_impl_t *
dbuf_hold(dnode_t *dn, uint64_t blkid, void *tag)
{
	return (dbuf_hold_level(dn, 0, blkid, tag));
}

dmu_buf_impl_t *
dbuf_hold_level(dnode_t *dn, int level, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, level, blkid, FALSE, FALSE, tag, &db);
	return (err ? NULL : db);
}

void
dbuf_create_bonus(dnode_t *dn)
{
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));

	ASSERT(dn->dn_bonus == NULL);
	dn->dn_bonus = dbuf_create(dn, 0, DMU_BONUS_BLKID, dn->dn_dbuf, NULL);
}

int
dbuf_spill_set_blksz(dmu_buf_t *db_fake, uint64_t blksz, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;

	if (db->db_blkid != DMU_SPILL_BLKID)
		return (SET_ERROR(ENOTSUP));
	if (blksz == 0)
		blksz = SPA_MINBLOCKSIZE;
	ASSERT3U(blksz, <=, spa_maxblocksize(dmu_objset_spa(db->db_objset)));
	blksz = P2ROUNDUP(blksz, SPA_MINBLOCKSIZE);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dbuf_new_size(db, blksz, tx);
	rw_exit(&dn->dn_struct_rwlock);
	DB_DNODE_EXIT(db);

	return (0);
}

void
dbuf_rm_spill(dnode_t *dn, dmu_tx_t *tx)
{
	dbuf_free_range(dn, DMU_SPILL_BLKID, DMU_SPILL_BLKID, tx);
}

#pragma weak dmu_buf_add_ref = dbuf_add_ref
void
dbuf_add_ref(dmu_buf_impl_t *db, void *tag)
{
	int64_t holds = refcount_add(&db->db_holds, tag);
	ASSERT3S(holds, >, 1);
}

#pragma weak dmu_buf_try_add_ref = dbuf_try_add_ref
boolean_t
dbuf_try_add_ref(dmu_buf_t *db_fake, objset_t *os, uint64_t obj, uint64_t blkid,
    void *tag)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dmu_buf_impl_t *found_db;
	boolean_t result = B_FALSE;

	if (db->db_blkid == DMU_BONUS_BLKID)
		found_db = dbuf_find_bonus(os, obj);
	else
		found_db = dbuf_find(os, obj, 0, blkid);

	if (found_db != NULL) {
		if (db == found_db && dbuf_refcount(db) > db->db_dirtycnt) {
			(void) refcount_add(&db->db_holds, tag);
			result = B_TRUE;
		}
		mutex_exit(&db->db_mtx);
	}
	return (result);
}

/*
 * If you call dbuf_rele() you had better not be referencing the dnode handle
 * unless you have some other direct or indirect hold on the dnode. (An indirect
 * hold is a hold on one of the dnode's dbufs, including the bonus buffer.)
 * Without that, the dbuf_rele() could lead to a dnode_rele() followed by the
 * dnode's parent dbuf evicting its dnode handles.
 */
void
dbuf_rele(dmu_buf_impl_t *db, void *tag)
{
	mutex_enter(&db->db_mtx);
	dbuf_rele_and_unlock(db, tag, B_FALSE);
}

void
dmu_buf_rele(dmu_buf_t *db, void *tag)
{
	dbuf_rele((dmu_buf_impl_t *)db, tag);
}

/*
 * dbuf_rele() for an already-locked dbuf.  This is necessary to allow
 * db_dirtycnt and db_holds to be updated atomically.  The 'evicting'
 * argument should be set if we are already in the dbuf-evicting code
 * path, in which case we don't want to recursively evict.  This allows us to
 * avoid deeply nested stacks that would have a call flow similar to this:
 *
 * dbuf_rele()-->dbuf_rele_and_unlock()-->dbuf_evict_notify()
 *	^						|
 *	|						|
 *	+-----dbuf_destroy()<--dbuf_evict_one()<--------+
 *
 */
void
dbuf_rele_and_unlock(dmu_buf_impl_t *db, void *tag, boolean_t evicting)
{
	int64_t holds;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	DBUF_VERIFY(db);

	/*
	 * Remove the reference to the dbuf before removing its hold on the
	 * dnode so we can guarantee in dnode_move() that a referenced bonus
	 * buffer has a corresponding dnode hold.
	 */
	holds = refcount_remove(&db->db_holds, tag);
	ASSERT(holds >= 0);

	/*
	 * We can't freeze indirects if there is a possibility that they
	 * may be modified in the current syncing context.
	 */
	if (db->db_buf != NULL &&
	    holds == (db->db_level == 0 ? db->db_dirtycnt : 0)) {
		arc_buf_freeze(db->db_buf);
	}

	if (holds == db->db_dirtycnt &&
	    db->db_level == 0 && db->db_user_immediate_evict)
		dbuf_evict_user(db);

	if (holds == 0) {
		if (db->db_blkid == DMU_BONUS_BLKID) {
			dnode_t *dn;
			boolean_t evict_dbuf = db->db_pending_evict;

			/*
			 * If the dnode moves here, we cannot cross this
			 * barrier until the move completes.
			 */
			DB_DNODE_ENTER(db);

			dn = DB_DNODE(db);
			atomic_dec_32(&dn->dn_dbufs_count);

			/*
			 * Decrementing the dbuf count means that the bonus
			 * buffer's dnode hold is no longer discounted in
			 * dnode_move(). The dnode cannot move until after
			 * the dnode_rele() below.
			 */
			DB_DNODE_EXIT(db);

			/*
			 * Do not reference db after its lock is dropped.
			 * Another thread may evict it.
			 */
			mutex_exit(&db->db_mtx);

			if (evict_dbuf)
				dnode_evict_bonus(dn);

			dnode_rele(dn, db);
		} else if (db->db_buf == NULL) {
			/*
			 * This is a special case: we never associated this
			 * dbuf with any data allocated from the ARC.
			 */
			ASSERT(db->db_state == DB_UNCACHED ||
			    db->db_state == DB_NOFILL);
			dbuf_destroy(db);
		} else if (arc_released(db->db_buf)) {
			/*
			 * This dbuf has anonymous data associated with it.
			 */
			dbuf_destroy(db);
		} else {
			boolean_t do_arc_evict = B_FALSE;
			blkptr_t bp;
			spa_t *spa = dmu_objset_spa(db->db_objset);

			if (!DBUF_IS_CACHEABLE(db) &&
			    db->db_blkptr != NULL &&
			    !BP_IS_HOLE(db->db_blkptr) &&
			    !BP_IS_EMBEDDED(db->db_blkptr)) {
				do_arc_evict = B_TRUE;
				bp = *db->db_blkptr;
			}

			if (!DBUF_IS_CACHEABLE(db) ||
			    db->db_pending_evict) {
				dbuf_destroy(db);
			} else if (!multilist_link_active(&db->db_cache_link)) {
				ASSERT3U(db->db_caching_status, ==,
				    DB_NO_CACHE);

				dbuf_cached_state_t dcs =
				    dbuf_include_in_metadata_cache(db) ?
				    DB_DBUF_METADATA_CACHE : DB_DBUF_CACHE;
				db->db_caching_status = dcs;

				multilist_insert(dbuf_caches[dcs].cache, db);
				(void) refcount_add_many(&dbuf_caches[dcs].size,
				    db->db.db_size, db);

				if (dcs == DB_DBUF_METADATA_CACHE) {
					DBUF_STAT_BUMP(metadata_cache_count);
					DBUF_STAT_MAX(
					    metadata_cache_size_bytes_max,
					    refcount_count(
					    &dbuf_caches[dcs].size));
				} else {
					DBUF_STAT_BUMP(
					    cache_levels[db->db_level]);
					DBUF_STAT_BUMP(cache_count);
					DBUF_STAT_INCR(
					    cache_levels_bytes[db->db_level],
					    db->db.db_size);
					DBUF_STAT_MAX(cache_size_bytes_max,
					    refcount_count(
					    &dbuf_caches[dcs].size));
				}
				mutex_exit(&db->db_mtx);

				if (db->db_caching_status == DB_DBUF_CACHE &&
				    !evicting) {
					dbuf_evict_notify();
				}
			}

			if (do_arc_evict)
				arc_freed(spa, &bp);
		}
	} else {
		mutex_exit(&db->db_mtx);
	}

}

#pragma weak dmu_buf_refcount = dbuf_refcount
uint64_t
dbuf_refcount(dmu_buf_impl_t *db)
{
	return (refcount_count(&db->db_holds));
}

void *
dmu_buf_replace_user(dmu_buf_t *db_fake, dmu_buf_user_t *old_user,
    dmu_buf_user_t *new_user)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	mutex_enter(&db->db_mtx);
	dbuf_verify_user(db, DBVU_NOT_EVICTING);
	if (db->db_user == old_user)
		db->db_user = new_user;
	else
		old_user = db->db_user;
	dbuf_verify_user(db, DBVU_NOT_EVICTING);
	mutex_exit(&db->db_mtx);

	return (old_user);
}

void *
dmu_buf_set_user(dmu_buf_t *db_fake, dmu_buf_user_t *user)
{
	return (dmu_buf_replace_user(db_fake, NULL, user));
}

void *
dmu_buf_set_user_ie(dmu_buf_t *db_fake, dmu_buf_user_t *user)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	db->db_user_immediate_evict = TRUE;
	return (dmu_buf_set_user(db_fake, user));
}

void *
dmu_buf_remove_user(dmu_buf_t *db_fake, dmu_buf_user_t *user)
{
	return (dmu_buf_replace_user(db_fake, user, NULL));
}

void *
dmu_buf_get_user(dmu_buf_t *db_fake)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	dbuf_verify_user(db, DBVU_NOT_EVICTING);
	return (db->db_user);
}

void
dmu_buf_user_evict_wait()
{
	taskq_wait(dbu_evict_taskq);
}

blkptr_t *
dmu_buf_get_blkptr(dmu_buf_t *db)
{
	dmu_buf_impl_t *dbi = (dmu_buf_impl_t *)db;
	return (dbi->db_blkptr);
}

objset_t *
dmu_buf_get_objset(dmu_buf_t *db)
{
	dmu_buf_impl_t *dbi = (dmu_buf_impl_t *)db;
	return (dbi->db_objset);
}

dnode_t *
dmu_buf_dnode_enter(dmu_buf_t *db)
{
	dmu_buf_impl_t *dbi = (dmu_buf_impl_t *)db;
	DB_DNODE_ENTER(dbi);
	return (DB_DNODE(dbi));
}

void
dmu_buf_dnode_exit(dmu_buf_t *db)
{
	dmu_buf_impl_t *dbi = (dmu_buf_impl_t *)db;
	DB_DNODE_EXIT(dbi);
}

static void
dbuf_check_blkptr(dnode_t *dn, dmu_buf_impl_t *db)
{
	/* ASSERT(dmu_tx_is_syncing(tx) */
	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (db->db_blkptr != NULL)
		return;

	if (db->db_blkid == DMU_SPILL_BLKID) {
		db->db_blkptr = DN_SPILL_BLKPTR(dn->dn_phys);
		BP_ZERO(db->db_blkptr);
		return;
	}
	if (db->db_level == dn->dn_phys->dn_nlevels-1) {
		/*
		 * This buffer was allocated at a time when there was
		 * no available blkptrs from the dnode, or it was
		 * inappropriate to hook it in (i.e., nlevels mis-match).
		 */
		ASSERT(db->db_blkid < dn->dn_phys->dn_nblkptr);
		ASSERT(db->db_parent == NULL);
		db->db_parent = dn->dn_dbuf;
		db->db_blkptr = &dn->dn_phys->dn_blkptr[db->db_blkid];
		DBUF_VERIFY(db);
	} else {
		dmu_buf_impl_t *parent = db->db_parent;
		int epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;

		ASSERT(dn->dn_phys->dn_nlevels > 1);
		if (parent == NULL) {
			mutex_exit(&db->db_mtx);
			rw_enter(&dn->dn_struct_rwlock, RW_READER);
			parent = dbuf_hold_level(dn, db->db_level + 1,
			    db->db_blkid >> epbs, db);
			rw_exit(&dn->dn_struct_rwlock);
			mutex_enter(&db->db_mtx);
			db->db_parent = parent;
		}
		db->db_blkptr = (blkptr_t *)parent->db.db_data +
		    (db->db_blkid & ((1ULL << epbs) - 1));
		DBUF_VERIFY(db);
	}
}

/*
 * dbuf_sync_indirect() is called recursively from dbuf_sync_list() so it
 * is critical the we not allow the compiler to inline this function in to
 * dbuf_sync_list() thereby drastically bloating the stack usage.
 */
noinline static void
dbuf_sync_indirect(dbuf_dirty_record_t *dr, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn;
	zio_t *zio;

	ASSERT(dmu_tx_is_syncing(tx));

	dprintf_dbuf_bp(db, db->db_blkptr, "blkptr=%p", db->db_blkptr);

	mutex_enter(&db->db_mtx);

	ASSERT(db->db_level > 0);
	DBUF_VERIFY(db);

	/* Read the block if it hasn't been read yet. */
	if (db->db_buf == NULL) {
		mutex_exit(&db->db_mtx);
		(void) dbuf_read(db, NULL, DB_RF_MUST_SUCCEED);
		mutex_enter(&db->db_mtx);
	}
	ASSERT3U(db->db_state, ==, DB_CACHED);
	ASSERT(db->db_buf != NULL);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	/* Indirect block size must match what the dnode thinks it is. */
	ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
	dbuf_check_blkptr(dn, db);
	DB_DNODE_EXIT(db);

	/* Provide the pending dirty record to child dbufs */
	db->db_data_pending = dr;

	mutex_exit(&db->db_mtx);

	dbuf_write(dr, db->db_buf, tx);

	zio = dr->dr_zio;
	mutex_enter(&dr->dt.di.dr_mtx);
	dbuf_sync_list(&dr->dt.di.dr_children, db->db_level - 1, tx);
	ASSERT(list_head(&dr->dt.di.dr_children) == NULL);
	mutex_exit(&dr->dt.di.dr_mtx);
	zio_nowait(zio);
}

/*
 * dbuf_sync_leaf() is called recursively from dbuf_sync_list() so it is
 * critical the we not allow the compiler to inline this function in to
 * dbuf_sync_list() thereby drastically bloating the stack usage.
 */
noinline static void
dbuf_sync_leaf(dbuf_dirty_record_t *dr, dmu_tx_t *tx)
{
	arc_buf_t **datap = &dr->dt.dl.dr_data;
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn;
	objset_t *os;
	uint64_t txg = tx->tx_txg;

	ASSERT(dmu_tx_is_syncing(tx));

	dprintf_dbuf_bp(db, db->db_blkptr, "blkptr=%p", db->db_blkptr);

	mutex_enter(&db->db_mtx);
	/*
	 * To be synced, we must be dirtied.  But we
	 * might have been freed after the dirty.
	 */
	if (db->db_state == DB_UNCACHED) {
		/* This buffer has been freed since it was dirtied */
		ASSERT(db->db.db_data == NULL);
	} else if (db->db_state == DB_FILL) {
		/* This buffer was freed and is now being re-filled */
		ASSERT(db->db.db_data != dr->dt.dl.dr_data);
	} else {
		ASSERT(db->db_state == DB_CACHED || db->db_state == DB_NOFILL);
	}
	DBUF_VERIFY(db);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	if (db->db_blkid == DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		if (!(dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR)) {
			/*
			 * In the previous transaction group, the bonus buffer
			 * was entirely used to store the attributes for the
			 * dnode which overrode the dn_spill field.  However,
			 * when adding more attributes to the file a spill
			 * block was required to hold the extra attributes.
			 *
			 * Make sure to clear the garbage left in the dn_spill
			 * field from the previous attributes in the bonus
			 * buffer.  Otherwise, after writing out the spill
			 * block to the new allocated dva, it will free
			 * the old block pointed to by the invalid dn_spill.
			 */
			db->db_blkptr = NULL;
		}
		dn->dn_phys->dn_flags |= DNODE_FLAG_SPILL_BLKPTR;
		mutex_exit(&dn->dn_mtx);
	}

	/*
	 * If this is a bonus buffer, simply copy the bonus data into the
	 * dnode.  It will be written out when the dnode is synced (and it
	 * will be synced, since it must have been dirty for dbuf_sync to
	 * be called).
	 */
	if (db->db_blkid == DMU_BONUS_BLKID) {
		dbuf_dirty_record_t **drp;

		ASSERT(*datap != NULL);
		ASSERT0(db->db_level);
		ASSERT3U(DN_MAX_BONUS_LEN(dn->dn_phys), <=,
		    DN_SLOTS_TO_BONUSLEN(dn->dn_phys->dn_extra_slots + 1));
		bcopy(*datap, DN_BONUS(dn->dn_phys),
		    DN_MAX_BONUS_LEN(dn->dn_phys));
		DB_DNODE_EXIT(db);

		if (*datap != db->db.db_data) {
			int slots = DB_DNODE(db)->dn_num_slots;
			int bonuslen = DN_SLOTS_TO_BONUSLEN(slots);
			zio_buf_free(*datap, bonuslen);
			arc_space_return(bonuslen, ARC_SPACE_BONUS);
		}
		db->db_data_pending = NULL;
		drp = &db->db_last_dirty;
		while (*drp != dr)
			drp = &(*drp)->dr_next;
		ASSERT(dr->dr_next == NULL);
		ASSERT(dr->dr_dbuf == db);
		*drp = dr->dr_next;
		if (dr->dr_dbuf->db_level != 0) {
			mutex_destroy(&dr->dt.di.dr_mtx);
			list_destroy(&dr->dt.di.dr_children);
		}
		kmem_free(dr, sizeof (dbuf_dirty_record_t));
		ASSERT(db->db_dirtycnt > 0);
		db->db_dirtycnt -= 1;
		dbuf_rele_and_unlock(db, (void *)(uintptr_t)txg, B_FALSE);
		return;
	}

	os = dn->dn_objset;

	/*
	 * This function may have dropped the db_mtx lock allowing a dmu_sync
	 * operation to sneak in. As a result, we need to ensure that we
	 * don't check the dr_override_state until we have returned from
	 * dbuf_check_blkptr.
	 */
	dbuf_check_blkptr(dn, db);

	/*
	 * If this buffer is in the middle of an immediate write,
	 * wait for the synchronous IO to complete.
	 */
	while (dr->dt.dl.dr_override_state == DR_IN_DMU_SYNC) {
		ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT);
		cv_wait(&db->db_changed, &db->db_mtx);
		ASSERT(dr->dt.dl.dr_override_state != DR_NOT_OVERRIDDEN);
	}

	if (db->db_state != DB_NOFILL &&
	    dn->dn_object != DMU_META_DNODE_OBJECT &&
	    refcount_count(&db->db_holds) > 1 &&
	    dr->dt.dl.dr_override_state != DR_OVERRIDDEN &&
	    *datap == db->db_buf) {
		/*
		 * If this buffer is currently "in use" (i.e., there
		 * are active holds and db_data still references it),
		 * then make a copy before we start the write so that
		 * any modifications from the open txg will not leak
		 * into this write.
		 *
		 * NOTE: this copy does not need to be made for
		 * objects only modified in the syncing context (e.g.
		 * DNONE_DNODE blocks).
		 */
		int psize = arc_buf_size(*datap);
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
		enum zio_compress compress_type = arc_get_compression(*datap);

		if (compress_type == ZIO_COMPRESS_OFF) {
			*datap = arc_alloc_buf(os->os_spa, db, type, psize);
		} else {
			ASSERT3U(type, ==, ARC_BUFC_DATA);
			int lsize = arc_buf_lsize(*datap);
			*datap = arc_alloc_compressed_buf(os->os_spa, db,
			    psize, lsize, compress_type);
		}
		bcopy(db->db.db_data, (*datap)->b_data, psize);
	}
	db->db_data_pending = dr;

	mutex_exit(&db->db_mtx);

	dbuf_write(dr, *datap, tx);

	ASSERT(!list_link_active(&dr->dr_dirty_node));
	if (dn->dn_object == DMU_META_DNODE_OBJECT) {
		list_insert_tail(&dn->dn_dirty_records[txg&TXG_MASK], dr);
		DB_DNODE_EXIT(db);
	} else {
		/*
		 * Although zio_nowait() does not "wait for an IO", it does
		 * initiate the IO. If this is an empty write it seems plausible
		 * that the IO could actually be completed before the nowait
		 * returns. We need to DB_DNODE_EXIT() first in case
		 * zio_nowait() invalidates the dbuf.
		 */
		DB_DNODE_EXIT(db);
		zio_nowait(dr->dr_zio);
	}
}

void
dbuf_sync_list(list_t *list, int level, dmu_tx_t *tx)
{
	dbuf_dirty_record_t *dr;

	while (dr = list_head(list)) {
		if (dr->dr_zio != NULL) {
			/*
			 * If we find an already initialized zio then we
			 * are processing the meta-dnode, and we have finished.
			 * The dbufs for all dnodes are put back on the list
			 * during processing, so that we can zio_wait()
			 * these IOs after initiating all child IOs.
			 */
			ASSERT3U(dr->dr_dbuf->db.db_object, ==,
			    DMU_META_DNODE_OBJECT);
			break;
		}
		if (dr->dr_dbuf->db_blkid != DMU_BONUS_BLKID &&
		    dr->dr_dbuf->db_blkid != DMU_SPILL_BLKID) {
			VERIFY3U(dr->dr_dbuf->db_level, ==, level);
		}
		list_remove(list, dr);
		if (dr->dr_dbuf->db_level > 0)
			dbuf_sync_indirect(dr, tx);
		else
			dbuf_sync_leaf(dr, tx);
	}
}

/* ARGSUSED */
static void
dbuf_write_ready(zio_t *zio, arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;
	dnode_t *dn;
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	spa_t *spa = zio->io_spa;
	int64_t delta;
	uint64_t fill = 0;
	int i;

	ASSERT3P(db->db_blkptr, !=, NULL);
	ASSERT3P(&db->db_data_pending->dr_bp_copy, ==, bp);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	delta = bp_get_dsize_sync(spa, bp) - bp_get_dsize_sync(spa, bp_orig);
	dnode_diduse_space(dn, delta - zio->io_prev_space_delta);
	zio->io_prev_space_delta = delta;

	if (bp->blk_birth != 0) {
		ASSERT((db->db_blkid != DMU_SPILL_BLKID &&
		    BP_GET_TYPE(bp) == dn->dn_type) ||
		    (db->db_blkid == DMU_SPILL_BLKID &&
		    BP_GET_TYPE(bp) == dn->dn_bonustype) ||
		    BP_IS_EMBEDDED(bp));
		ASSERT(BP_GET_LEVEL(bp) == db->db_level);
	}

	mutex_enter(&db->db_mtx);

#ifdef ZFS_DEBUG
	if (db->db_blkid == DMU_SPILL_BLKID) {
		ASSERT(dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR);
		ASSERT(!(BP_IS_HOLE(bp)) &&
		    db->db_blkptr == DN_SPILL_BLKPTR(dn->dn_phys));
	}
#endif

	if (db->db_level == 0) {
		mutex_enter(&dn->dn_mtx);
		if (db->db_blkid > dn->dn_phys->dn_maxblkid &&
		    db->db_blkid != DMU_SPILL_BLKID)
			dn->dn_phys->dn_maxblkid = db->db_blkid;
		mutex_exit(&dn->dn_mtx);

		if (dn->dn_type == DMU_OT_DNODE) {
			i = 0;
			while (i < db->db.db_size) {
				dnode_phys_t *dnp = db->db.db_data + i;

				i += DNODE_MIN_SIZE;
				if (dnp->dn_type != DMU_OT_NONE) {
					fill++;
					i += dnp->dn_extra_slots *
					    DNODE_MIN_SIZE;
				}
			}
		} else {
			if (BP_IS_HOLE(bp)) {
				fill = 0;
			} else {
				fill = 1;
			}
		}
	} else {
		blkptr_t *ibp = db->db.db_data;
		ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
		for (i = db->db.db_size >> SPA_BLKPTRSHIFT; i > 0; i--, ibp++) {
			if (BP_IS_HOLE(ibp))
				continue;
			fill += BP_GET_FILL(ibp);
		}
	}
	DB_DNODE_EXIT(db);

	if (!BP_IS_EMBEDDED(bp))
		bp->blk_fill = fill;

	mutex_exit(&db->db_mtx);

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	*db->db_blkptr = *bp;
	rw_exit(&dn->dn_struct_rwlock);
}

/* ARGSUSED */
/*
 * This function gets called just prior to running through the compression
 * stage of the zio pipeline. If we're an indirect block comprised of only
 * holes, then we want this indirect to be compressed away to a hole. In
 * order to do that we must zero out any information about the holes that
 * this indirect points to prior to before we try to compress it.
 */
static void
dbuf_write_children_ready(zio_t *zio, arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;
	dnode_t *dn;
	blkptr_t *bp;
	unsigned int epbs, i;

	ASSERT3U(db->db_level, >, 0);
	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
	ASSERT3U(epbs, <, 31);

	/* Determine if all our children are holes */
	for (i = 0, bp = db->db.db_data; i < 1 << epbs; i++, bp++) {
		if (!BP_IS_HOLE(bp))
			break;
	}

	/*
	 * If all the children are holes, then zero them all out so that
	 * we may get compressed away.
	 */
	if (i == 1 << epbs) {
		/*
		 * We only found holes. Grab the rwlock to prevent
		 * anybody from reading the blocks we're about to
		 * zero out.
		 */
		rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
		bzero(db->db.db_data, db->db.db_size);
		rw_exit(&dn->dn_struct_rwlock);
	}
	DB_DNODE_EXIT(db);
}

/*
 * The SPA will call this callback several times for each zio - once
 * for every physical child i/o (zio->io_phys_children times).  This
 * allows the DMU to monitor the progress of each logical i/o.  For example,
 * there may be 2 copies of an indirect block, or many fragments of a RAID-Z
 * block.  There may be a long delay before all copies/fragments are completed,
 * so this callback allows us to retire dirty space gradually, as the physical
 * i/os complete.
 */
/* ARGSUSED */
static void
dbuf_write_physdone(zio_t *zio, arc_buf_t *buf, void *arg)
{
	dmu_buf_impl_t *db = arg;
	objset_t *os = db->db_objset;
	dsl_pool_t *dp = dmu_objset_pool(os);
	dbuf_dirty_record_t *dr;
	int delta = 0;

	dr = db->db_data_pending;
	ASSERT3U(dr->dr_txg, ==, zio->io_txg);

	/*
	 * The callback will be called io_phys_children times.  Retire one
	 * portion of our dirty space each time we are called.  Any rounding
	 * error will be cleaned up by dsl_pool_sync()'s call to
	 * dsl_pool_undirty_space().
	 */
	delta = dr->dr_accounted / zio->io_phys_children;
	dsl_pool_undirty_space(dp, delta, zio->io_txg);
}

/* ARGSUSED */
static void
dbuf_write_done(zio_t *zio, arc_buf_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	blkptr_t *bp = db->db_blkptr;
	objset_t *os = db->db_objset;
	dmu_tx_t *tx = os->os_synctx;
	dbuf_dirty_record_t **drp, *dr;

	ASSERT0(zio->io_error);
	ASSERT(db->db_blkptr == bp);

	/*
	 * For nopwrites and rewrites we ensure that the bp matches our
	 * original and bypass all the accounting.
	 */
	if (zio->io_flags & (ZIO_FLAG_IO_REWRITE | ZIO_FLAG_NOPWRITE)) {
		ASSERT(BP_EQUAL(bp, bp_orig));
	} else {
		dsl_dataset_t *ds = os->os_dsl_dataset;
		(void) dsl_dataset_block_kill(ds, bp_orig, tx, B_TRUE);
		dsl_dataset_block_born(ds, bp, tx);
	}

	mutex_enter(&db->db_mtx);

	DBUF_VERIFY(db);

	drp = &db->db_last_dirty;
	while ((dr = *drp) != db->db_data_pending)
		drp = &dr->dr_next;
	ASSERT(!list_link_active(&dr->dr_dirty_node));
	ASSERT(dr->dr_dbuf == db);
	ASSERT(dr->dr_next == NULL);
	*drp = dr->dr_next;

#ifdef ZFS_DEBUG
	if (db->db_blkid == DMU_SPILL_BLKID) {
		dnode_t *dn;

		DB_DNODE_ENTER(db);
		dn = DB_DNODE(db);
		ASSERT(dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR);
		ASSERT(!(BP_IS_HOLE(db->db_blkptr)) &&
		    db->db_blkptr == DN_SPILL_BLKPTR(dn->dn_phys));
		DB_DNODE_EXIT(db);
	}
#endif

	if (db->db_level == 0) {
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);
		ASSERT(dr->dt.dl.dr_override_state == DR_NOT_OVERRIDDEN);
		if (db->db_state != DB_NOFILL) {
			if (dr->dt.dl.dr_data != db->db_buf)
				arc_buf_destroy(dr->dt.dl.dr_data, db);
		}
	} else {
		dnode_t *dn;

		DB_DNODE_ENTER(db);
		dn = DB_DNODE(db);
		ASSERT(list_head(&dr->dt.di.dr_children) == NULL);
		ASSERT3U(db->db.db_size, ==, 1 << dn->dn_phys->dn_indblkshift);
		if (!BP_IS_HOLE(db->db_blkptr)) {
			int epbs =
			    dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
			ASSERT3U(db->db_blkid, <=,
			    dn->dn_phys->dn_maxblkid >> (db->db_level * epbs));
			ASSERT3U(BP_GET_LSIZE(db->db_blkptr), ==,
			    db->db.db_size);
		}
		DB_DNODE_EXIT(db);
		mutex_destroy(&dr->dt.di.dr_mtx);
		list_destroy(&dr->dt.di.dr_children);
	}
	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	cv_broadcast(&db->db_changed);
	ASSERT(db->db_dirtycnt > 0);
	db->db_dirtycnt -= 1;
	db->db_data_pending = NULL;
	dbuf_rele_and_unlock(db, (void *)(uintptr_t)tx->tx_txg, B_FALSE);
}

static void
dbuf_write_nofill_ready(zio_t *zio)
{
	dbuf_write_ready(zio, NULL, zio->io_private);
}

static void
dbuf_write_nofill_done(zio_t *zio)
{
	dbuf_write_done(zio, NULL, zio->io_private);
}

static void
dbuf_write_override_ready(zio_t *zio)
{
	dbuf_dirty_record_t *dr = zio->io_private;
	dmu_buf_impl_t *db = dr->dr_dbuf;

	dbuf_write_ready(zio, NULL, db);
}

static void
dbuf_write_override_done(zio_t *zio)
{
	dbuf_dirty_record_t *dr = zio->io_private;
	dmu_buf_impl_t *db = dr->dr_dbuf;
	blkptr_t *obp = &dr->dt.dl.dr_overridden_by;

	mutex_enter(&db->db_mtx);
	if (!BP_EQUAL(zio->io_bp, obp)) {
		if (!BP_IS_HOLE(obp))
			dsl_free(spa_get_dsl(zio->io_spa), zio->io_txg, obp);
		arc_release(dr->dt.dl.dr_data, db);
	}
	mutex_exit(&db->db_mtx);
	dbuf_write_done(zio, NULL, db);

	if (zio->io_abd != NULL)
		abd_put(zio->io_abd);
}

typedef struct dbuf_remap_impl_callback_arg {
	objset_t	*drica_os;
	uint64_t	drica_blk_birth;
	dmu_tx_t	*drica_tx;
} dbuf_remap_impl_callback_arg_t;

static void
dbuf_remap_impl_callback(uint64_t vdev, uint64_t offset, uint64_t size,
    void *arg)
{
	dbuf_remap_impl_callback_arg_t *drica = arg;
	objset_t *os = drica->drica_os;
	spa_t *spa = dmu_objset_spa(os);
	dmu_tx_t *tx = drica->drica_tx;

	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)));

	if (os == spa_meta_objset(spa)) {
		spa_vdev_indirect_mark_obsolete(spa, vdev, offset, size, tx);
	} else {
		dsl_dataset_block_remapped(dmu_objset_ds(os), vdev, offset,
		    size, drica->drica_blk_birth, tx);
	}
}

static void
dbuf_remap_impl(dnode_t *dn, blkptr_t *bp, dmu_tx_t *tx)
{
	blkptr_t bp_copy = *bp;
	spa_t *spa = dmu_objset_spa(dn->dn_objset);
	dbuf_remap_impl_callback_arg_t drica;

	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)));

	drica.drica_os = dn->dn_objset;
	drica.drica_blk_birth = bp->blk_birth;
	drica.drica_tx = tx;
	if (spa_remap_blkptr(spa, &bp_copy, dbuf_remap_impl_callback,
	    &drica)) {
		/*
		 * The struct_rwlock prevents dbuf_read_impl() from
		 * dereferencing the BP while we are changing it.  To
		 * avoid lock contention, only grab it when we are actually
		 * changing the BP.
		 */
		rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
		*bp = bp_copy;
		rw_exit(&dn->dn_struct_rwlock);
	}
}

/*
 * Returns true if a dbuf_remap would modify the dbuf. We do this by attempting
 * to remap a copy of every bp in the dbuf.
 */
boolean_t
dbuf_can_remap(const dmu_buf_impl_t *db)
{
	spa_t *spa = dmu_objset_spa(db->db_objset);
	blkptr_t *bp = db->db.db_data;
	boolean_t ret = B_FALSE;

	ASSERT3U(db->db_level, >, 0);
	ASSERT3S(db->db_state, ==, DB_CACHED);

	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_DEVICE_REMOVAL));

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	for (int i = 0; i < db->db.db_size >> SPA_BLKPTRSHIFT; i++) {
		blkptr_t bp_copy = bp[i];
		if (spa_remap_blkptr(spa, &bp_copy, NULL, NULL)) {
			ret = B_TRUE;
			break;
		}
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);

	return (ret);
}

boolean_t
dnode_needs_remap(const dnode_t *dn)
{
	spa_t *spa = dmu_objset_spa(dn->dn_objset);
	boolean_t ret = B_FALSE;

	if (dn->dn_phys->dn_nlevels == 0) {
		return (B_FALSE);
	}

	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_DEVICE_REMOVAL));

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	for (int j = 0; j < dn->dn_phys->dn_nblkptr; j++) {
		blkptr_t bp_copy = dn->dn_phys->dn_blkptr[j];
		if (spa_remap_blkptr(spa, &bp_copy, NULL, NULL)) {
			ret = B_TRUE;
			break;
		}
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);

	return (ret);
}

/*
 * Remap any existing BP's to concrete vdevs, if possible.
 */
static void
dbuf_remap(dnode_t *dn, dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(db->db_objset);
	ASSERT(dsl_pool_sync_context(spa_get_dsl(spa)));

	if (!spa_feature_is_active(spa, SPA_FEATURE_DEVICE_REMOVAL))
		return;

	if (db->db_level > 0) {
		blkptr_t *bp = db->db.db_data;
		for (int i = 0; i < db->db.db_size >> SPA_BLKPTRSHIFT; i++) {
			dbuf_remap_impl(dn, &bp[i], tx);
		}
	} else if (db->db.db_object == DMU_META_DNODE_OBJECT) {
		dnode_phys_t *dnp = db->db.db_data;
		ASSERT3U(db->db_dnode_handle->dnh_dnode->dn_type, ==,
		    DMU_OT_DNODE);
		for (int i = 0; i < db->db.db_size >> DNODE_SHIFT;
		    i += dnp[i].dn_extra_slots + 1) {
			for (int j = 0; j < dnp[i].dn_nblkptr; j++) {
				dbuf_remap_impl(dn, &dnp[i].dn_blkptr[j], tx);
			}
		}
	}
}


/* Issue I/O to commit a dirty buffer to disk. */
static void
dbuf_write(dbuf_dirty_record_t *dr, arc_buf_t *data, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn;
	objset_t *os;
	dmu_buf_impl_t *parent = db->db_parent;
	uint64_t txg = tx->tx_txg;
	zbookmark_phys_t zb;
	zio_prop_t zp;
	zio_t *zio;
	int wp_flag = 0;

	ASSERT(dmu_tx_is_syncing(tx));

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	os = dn->dn_objset;

	if (db->db_state != DB_NOFILL) {
		if (db->db_level > 0 || dn->dn_type == DMU_OT_DNODE) {
			/*
			 * Private object buffers are released here rather
			 * than in dbuf_dirty() since they are only modified
			 * in the syncing context and we don't want the
			 * overhead of making multiple copies of the data.
			 */
			if (BP_IS_HOLE(db->db_blkptr)) {
				arc_buf_thaw(data);
			} else {
				dbuf_release_bp(db);
			}
			dbuf_remap(dn, db, tx);
		}
	}

	if (parent != dn->dn_dbuf) {
		/* Our parent is an indirect block. */
		/* We have a dirty parent that has been scheduled for write. */
		ASSERT(parent && parent->db_data_pending);
		/* Our parent's buffer is one level closer to the dnode. */
		ASSERT(db->db_level == parent->db_level-1);
		/*
		 * We're about to modify our parent's db_data by modifying
		 * our block pointer, so the parent must be released.
		 */
		ASSERT(arc_released(parent->db_buf));
		zio = parent->db_data_pending->dr_zio;
	} else {
		/* Our parent is the dnode itself. */
		ASSERT((db->db_level == dn->dn_phys->dn_nlevels-1 &&
		    db->db_blkid != DMU_SPILL_BLKID) ||
		    (db->db_blkid == DMU_SPILL_BLKID && db->db_level == 0));
		if (db->db_blkid != DMU_SPILL_BLKID)
			ASSERT3P(db->db_blkptr, ==,
			    &dn->dn_phys->dn_blkptr[db->db_blkid]);
		zio = dn->dn_zio;
	}

	ASSERT(db->db_level == 0 || data == db->db_buf);
	ASSERT3U(db->db_blkptr->blk_birth, <=, txg);
	ASSERT(zio);

	SET_BOOKMARK(&zb, os->os_dsl_dataset ?
	    os->os_dsl_dataset->ds_object : DMU_META_OBJSET,
	    db->db.db_object, db->db_level, db->db_blkid);

	if (db->db_blkid == DMU_SPILL_BLKID)
		wp_flag = WP_SPILL;
	wp_flag |= (db->db_state == DB_NOFILL) ? WP_NOFILL : 0;

	dmu_write_policy(os, dn, db->db_level, wp_flag, &zp);
	DB_DNODE_EXIT(db);

	/*
	 * We copy the blkptr now (rather than when we instantiate the dirty
	 * record), because its value can change between open context and
	 * syncing context. We do not need to hold dn_struct_rwlock to read
	 * db_blkptr because we are in syncing context.
	 */
	dr->dr_bp_copy = *db->db_blkptr;

	if (db->db_level == 0 &&
	    dr->dt.dl.dr_override_state == DR_OVERRIDDEN) {
		/*
		 * The BP for this block has been provided by open context
		 * (by dmu_sync() or dmu_buf_write_embedded()).
		 */
		abd_t *contents = (data != NULL) ?
		    abd_get_from_buf(data->b_data, arc_buf_size(data)) : NULL;

		dr->dr_zio = zio_write(zio, os->os_spa, txg, &dr->dr_bp_copy,
		    contents, db->db.db_size, db->db.db_size, &zp,
		    dbuf_write_override_ready, NULL, NULL,
		    dbuf_write_override_done,
		    dr, ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
		mutex_enter(&db->db_mtx);
		dr->dt.dl.dr_override_state = DR_NOT_OVERRIDDEN;
		zio_write_override(dr->dr_zio, &dr->dt.dl.dr_overridden_by,
		    dr->dt.dl.dr_copies, dr->dt.dl.dr_nopwrite);
		mutex_exit(&db->db_mtx);
	} else if (db->db_state == DB_NOFILL) {
		ASSERT(zp.zp_checksum == ZIO_CHECKSUM_OFF ||
		    zp.zp_checksum == ZIO_CHECKSUM_NOPARITY);
		dr->dr_zio = zio_write(zio, os->os_spa, txg,
		    &dr->dr_bp_copy, NULL, db->db.db_size, db->db.db_size, &zp,
		    dbuf_write_nofill_ready, NULL, NULL,
		    dbuf_write_nofill_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE,
		    ZIO_FLAG_MUSTSUCCEED | ZIO_FLAG_NODATA, &zb);
	} else {
		ASSERT(arc_released(data));

		/*
		 * For indirect blocks, we want to setup the children
		 * ready callback so that we can properly handle an indirect
		 * block that only contains holes.
		 */
		arc_write_done_func_t *children_ready_cb = NULL;
		if (db->db_level != 0)
			children_ready_cb = dbuf_write_children_ready;

		dr->dr_zio = arc_write(zio, os->os_spa, txg,
		    &dr->dr_bp_copy, data, DBUF_IS_L2CACHEABLE(db),
		    &zp, dbuf_write_ready, children_ready_cb,
		    dbuf_write_physdone, dbuf_write_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
	}
}
