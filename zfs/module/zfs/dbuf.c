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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/arc.h>
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
#include <sys/trace_dbuf.h>

struct dbuf_hold_impl_data {
	/* Function arguments */
	dnode_t *dh_dn;
	uint8_t dh_level;
	uint64_t dh_blkid;
	int dh_fail_sparse;
	void *dh_tag;
	dmu_buf_impl_t **dh_dbp;
	/* Local variables */
	dmu_buf_impl_t *dh_db;
	dmu_buf_impl_t *dh_parent;
	blkptr_t *dh_bp;
	int dh_err;
	dbuf_dirty_record_t *dh_dr;
	arc_buf_contents_t dh_type;
	int dh_depth;
};

static void __dbuf_hold_impl_init(struct dbuf_hold_impl_data *dh,
    dnode_t *dn, uint8_t level, uint64_t blkid, int fail_sparse,
    void *tag, dmu_buf_impl_t **dbp, int depth);
static int __dbuf_hold_impl(struct dbuf_hold_impl_data *dh);

/*
 * Number of times that zfs_free_range() took the slow path while doing
 * a zfs receive.  A nonzero value indicates a potential performance problem.
 */
uint64_t zfs_free_range_recv_miss;

static void dbuf_destroy(dmu_buf_impl_t *db);
static boolean_t dbuf_undirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
static void dbuf_write(dbuf_dirty_record_t *dr, arc_buf_t *data, dmu_tx_t *tx);

#ifndef __lint
extern inline void dmu_buf_init_user(dmu_buf_user_t *dbu,
    dmu_buf_evict_func_t *evict_func, dmu_buf_t **clear_on_evict_dbufp);
#endif /* ! __lint */

/*
 * Global data structures and functions for the dbuf cache.
 */
static kmem_cache_t *dbuf_cache;
static taskq_t *dbu_evict_taskq;

/* ARGSUSED */
static int
dbuf_cons(void *vdb, void *unused, int kmflag)
{
	dmu_buf_impl_t *db = vdb;
	bzero(db, sizeof (dmu_buf_impl_t));

	mutex_init(&db->db_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&db->db_changed, NULL, CV_DEFAULT, NULL);
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
	refcount_destroy(&db->db_holds);
}

/*
 * dbuf hash table routines
 */
static dbuf_hash_table_t dbuf_hash_table;

static uint64_t dbuf_hash_count;

static uint64_t
dbuf_hash(void *os, uint64_t obj, uint8_t lvl, uint64_t blkid)
{
	uintptr_t osv = (uintptr_t)os;
	uint64_t crc = -1ULL;

	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (lvl)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (osv >> 6)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (obj >> 0)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (obj >> 8)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (blkid >> 0)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (blkid >> 8)) & 0xFF];

	crc ^= (osv>>14) ^ (obj>>16) ^ (blkid>>16);

	return (crc);
}

#define	DBUF_HASH(os, obj, level, blkid) dbuf_hash(os, obj, level, blkid);

#define	DBUF_EQUAL(dbuf, os, obj, level, blkid)		\
	((dbuf)->db.db_object == (obj) &&		\
	(dbuf)->db_objset == (os) &&			\
	(dbuf)->db_level == (level) &&			\
	(dbuf)->db_blkid == (blkid))

dmu_buf_impl_t *
dbuf_find(objset_t *os, uint64_t obj, uint8_t level, uint64_t blkid)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	uint64_t hv;
	uint64_t idx;
	dmu_buf_impl_t *db;

	hv = DBUF_HASH(os, obj, level, blkid);
	idx = hv & h->hash_table_mask;

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

	blkid = db->db_blkid;
	hv = DBUF_HASH(os, obj, level, blkid);
	idx = hv & h->hash_table_mask;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (dbf = h->hash_table[idx]; dbf != NULL; dbf = dbf->db_hash_next) {
		if (DBUF_EQUAL(dbf, os, obj, level, blkid)) {
			mutex_enter(&dbf->db_mtx);
			if (dbf->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (dbf);
			}
			mutex_exit(&dbf->db_mtx);
		}
	}

	mutex_enter(&db->db_mtx);
	db->db_hash_next = h->hash_table[idx];
	h->hash_table[idx] = db;
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_add_64(&dbuf_hash_count, 1);

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

	hv = DBUF_HASH(db->db_objset, db->db.db_object,
	    db->db_level, db->db_blkid);
	idx = hv & h->hash_table_mask;

	/*
	 * We musn't hold db_mtx to maintain lock ordering:
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
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_add_64(&dbuf_hash_count, -1);
}

static arc_evict_func_t dbuf_do_evict;

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
	 * Invoke the callback from a taskq to avoid lock order reversals
	 * and limit stack depth.
	 */
	taskq_dispatch_ent(dbu_evict_taskq, dbu->dbu_evict_func, dbu, 0,
	    &dbu->dbu_tqent);
}

boolean_t
dbuf_is_metadata(dmu_buf_impl_t *db)
{
	/*
	 * Consider indirect blocks and spill blocks to be meta data.
	 */
	if (db->db_level > 0 || db->db_blkid == DMU_SPILL_BLKID) {
		return (B_TRUE);
	} else {
		boolean_t is_metadata;

		DB_DNODE_ENTER(db);
		is_metadata = DMU_OT_IS_METADATA(DB_DNODE(db)->dn_type);
		DB_DNODE_EXIT(db);

		return (is_metadata);
	}
}

void
dbuf_evict(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_buf == NULL);
	ASSERT(db->db_data_pending == NULL);

	dbuf_clear(db);
	dbuf_destroy(db);
}

void
dbuf_init(void)
{
	uint64_t hsize = 1ULL << 16;
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	/*
	 * The hash table is big enough to fill all of physical memory
	 * with an average block size of zfs_arc_average_blocksize (default 8K).
	 * By default, the table will take up
	 * totalmem * sizeof(void*) / 8K (1MB per GB with 8-byte pointers).
	 */
	while (hsize * zfs_arc_average_blocksize < physmem * PAGESIZE)
		hsize <<= 1;

retry:
	h->hash_table_mask = hsize - 1;
#if defined(_KERNEL) && defined(HAVE_SPL)
	/*
	 * Large allocations which do not require contiguous pages
	 * should be using vmem_alloc() in the linux kernel
	 */
	h->hash_table = vmem_zalloc(hsize * sizeof (void *), KM_SLEEP);
#else
	h->hash_table = kmem_zalloc(hsize * sizeof (void *), KM_NOSLEEP);
#endif
	if (h->hash_table == NULL) {
		/* XXX - we should really return an error instead of assert */
		ASSERT(hsize > (1ULL << 10));
		hsize >>= 1;
		goto retry;
	}

	dbuf_cache = kmem_cache_create("dmu_buf_impl_t",
	    sizeof (dmu_buf_impl_t),
	    0, dbuf_cons, dbuf_dest, NULL, NULL, NULL, 0);

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_init(&h->hash_mutexes[i], NULL, MUTEX_DEFAULT, NULL);

	dbuf_stats_init(h);

	/*
	 * All entries are queued via taskq_dispatch_ent(), so min/maxalloc
	 * configuration is not required.
	 */
	dbu_evict_taskq = taskq_create("dbu_evict", 1, defclsyspri, 0, 0, 0);
}

void
dbuf_fini(void)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	dbuf_stats_destroy();

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_destroy(&h->hash_mutexes[i]);
#if defined(_KERNEL) && defined(HAVE_SPL)
	/*
	 * Large allocations which do not require contiguous pages
	 * should be using vmem_free() in the linux kernel
	 */
	vmem_free(h->hash_table, (h->hash_table_mask + 1) * sizeof (void *));
#else
	kmem_free(h->hash_table, (h->hash_table_mask + 1) * sizeof (void *));
#endif
	kmem_cache_destroy(dbuf_cache);
	taskq_destroy(dbu_evict_taskq);
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
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
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
			ASSERTV(int epb = db->db_parent->db.db_size >>
				SPA_BLKPTRSHIFT);
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
		 */
		if (db->db_dirtycnt == 0) {
			ASSERTV(uint64_t *buf = db->db.db_data);
			int i;

			for (i = 0; i < db->db.db_size >> 3; i++) {
				ASSERT(buf[i] == 0);
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
	db->db_buf = NULL;
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
	if (!arc_released(buf))
		arc_set_callback(buf, dbuf_do_evict, db);
}

/*
 * Loan out an arc_buf for read.  Return the loaned arc_buf.
 */
arc_buf_t *
dbuf_loan_arcbuf(dmu_buf_impl_t *db)
{
	arc_buf_t *abuf;

	mutex_enter(&db->db_mtx);
	if (arc_released(db->db_buf) || refcount_count(&db->db_holds) > 1) {
		int blksz = db->db.db_size;
		spa_t *spa = db->db_objset->os_spa;

		mutex_exit(&db->db_mtx);
		abuf = arc_loan_buf(spa, blksz);
		bcopy(db->db.db_data, abuf->b_data, blksz);
	} else {
		abuf = db->db_buf;
		arc_loan_inuse_buf(abuf, db);
		dbuf_clear_data(db);
		mutex_exit(&db->db_mtx);
	}
	return (abuf);
}

uint64_t
dbuf_whichblock(dnode_t *dn, uint64_t offset)
{
	if (dn->dn_datablkshift) {
		return (offset >> dn->dn_datablkshift);
	} else {
		ASSERT3U(offset, <, dn->dn_datablksz);
		return (0);
	}
}

static void
dbuf_read_done(zio_t *zio, arc_buf_t *buf, void *vdb)
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
	if (db->db_level == 0 && db->db_freed_in_flight) {
		/* we were freed in flight; disregard any error */
		arc_release(buf, db);
		bzero(buf->b_data, db->db.db_size);
		arc_buf_freeze(buf);
		db->db_freed_in_flight = FALSE;
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	} else if (zio == NULL || zio->io_error == 0) {
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	} else {
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);
		ASSERT3P(db->db_buf, ==, NULL);
		VERIFY(arc_buf_remove_ref(buf, db));
		db->db_state = DB_UNCACHED;
	}
	cv_broadcast(&db->db_changed);
	dbuf_rele_and_unlock(db, NULL);
}

static int
dbuf_read_impl(dmu_buf_impl_t *db, zio_t *zio, uint32_t *flags)
{
	dnode_t *dn;
	zbookmark_phys_t zb;
	uint32_t aflags = ARC_FLAG_NOWAIT;
	int err;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	ASSERT(!refcount_is_zero(&db->db_holds));
	/* We need the struct_rwlock to prevent db_blkptr from changing. */
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_state == DB_UNCACHED);
	ASSERT(db->db_buf == NULL);

	if (db->db_blkid == DMU_BONUS_BLKID) {
		int bonuslen = MIN(dn->dn_bonuslen, dn->dn_phys->dn_bonuslen);

		ASSERT3U(bonuslen, <=, db->db.db_size);
		db->db.db_data = zio_buf_alloc(DN_MAX_BONUSLEN);
		arc_space_consume(DN_MAX_BONUSLEN, ARC_SPACE_OTHER);
		if (bonuslen < DN_MAX_BONUSLEN)
			bzero(db->db.db_data, DN_MAX_BONUSLEN);
		if (bonuslen)
			bcopy(DN_BONUS(dn->dn_phys), db->db.db_data, bonuslen);
		DB_DNODE_EXIT(db);
		db->db_state = DB_CACHED;
		mutex_exit(&db->db_mtx);
		return (0);
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

		DB_DNODE_EXIT(db);
		dbuf_set_data(db, arc_buf_alloc(db->db_objset->os_spa,
		    db->db.db_size, db, type));
		bzero(db->db.db_data, db->db.db_size);
		db->db_state = DB_CACHED;
		*flags |= DB_RF_CACHED;
		mutex_exit(&db->db_mtx);
		return (0);
	}

	DB_DNODE_EXIT(db);

	db->db_state = DB_READ;
	mutex_exit(&db->db_mtx);

	if (DBUF_IS_L2CACHEABLE(db))
		aflags |= ARC_FLAG_L2CACHE;
	if (DBUF_IS_L2COMPRESSIBLE(db))
		aflags |= ARC_FLAG_L2COMPRESS;

	SET_BOOKMARK(&zb, db->db_objset->os_dsl_dataset ?
	    db->db_objset->os_dsl_dataset->ds_object : DMU_META_OBJSET,
	    db->db.db_object, db->db_level, db->db_blkid);

	dbuf_add_ref(db, NULL);

	err = arc_read(zio, db->db_objset->os_spa, db->db_blkptr,
	    dbuf_read_done, db, ZIO_PRIORITY_SYNC_READ,
	    (*flags & DB_RF_CANFAIL) ? ZIO_FLAG_CANFAIL : ZIO_FLAG_MUSTSUCCEED,
	    &aflags, &zb);
	if (aflags & ARC_FLAG_CACHED)
		*flags |= DB_RF_CACHED;

	return (SET_ERROR(err));
}

int
dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags)
{
	int err = 0;
	boolean_t havepzio = (zio != NULL);
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
		mutex_exit(&db->db_mtx);
		if (prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db.db_offset,
			    db->db.db_size, TRUE);
		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&dn->dn_struct_rwlock);
		DB_DNODE_EXIT(db);
	} else if (db->db_state == DB_UNCACHED) {
		spa_t *spa = dn->dn_objset->os_spa;

		if (zio == NULL)
			zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

		err = dbuf_read_impl(db, zio, &flags);

		/* dbuf_read_impl has dropped db_mtx for us */

		if (!err && prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db.db_offset,
			    db->db.db_size, flags & DB_RF_CACHED);

		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&dn->dn_struct_rwlock);
		DB_DNODE_EXIT(db);

		if (!err && !havepzio)
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
			dmu_zfetch(&dn->dn_zfetch, db->db.db_offset,
			    db->db.db_size, TRUE);
		if ((flags & DB_RF_HAVESTRUCT) == 0)
			rw_exit(&dn->dn_struct_rwlock);
		DB_DNODE_EXIT(db);

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

	ASSERT(err || havepzio || db->db_state == DB_CACHED);
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
		dbuf_set_data(db, arc_buf_alloc(spa, db->db.db_size, db, type));
		db->db_state = DB_FILL;
	} else if (db->db_state == DB_NOFILL) {
		dbuf_clear_data(db);
	} else {
		ASSERT3U(db->db_state, ==, DB_CACHED);
	}
	mutex_exit(&db->db_mtx);
}

/*
 * This is our just-in-time copy function.  It makes a copy of
 * buffers, that have been modified in a previous transaction
 * group, before we modify them in the current active group.
 *
 * This function is used in two places: when we are dirtying a
 * buffer for the first time in a txg, and when we are freeing
 * a range in a dnode that includes this buffer.
 *
 * Note that when we are called from dbuf_free_range() we do
 * not put a hold on the buffer, we just traverse the active
 * dbuf list for the dnode.
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
		dr->dt.dl.dr_data = zio_buf_alloc(DN_MAX_BONUSLEN);
		arc_space_consume(DN_MAX_BONUSLEN, ARC_SPACE_OTHER);
		bcopy(db->db.db_data, dr->dt.dl.dr_data, DN_MAX_BONUSLEN);
	} else if (refcount_count(&db->db_holds) > db->db_dirtycnt) {
		int size = db->db.db_size;
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
		spa_t *spa = db->db_objset->os_spa;

		dr->dt.dl.dr_data = arc_buf_alloc(spa, size, db, type);
		bcopy(db->db.db_data, dr->dt.dl.dr_data->b_data, size);
	} else {
		dbuf_clear_data(db);
	}
}

void
dbuf_unoverride(dbuf_dirty_record_t *dr)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	blkptr_t *bp = &dr->dt.dl.dr_overridden_by;
	uint64_t txg = dr->dr_txg;

	ASSERT(MUTEX_HELD(&db->db_mtx));
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
 *
 * This is a no-op if the dataset is in the middle of an incremental
 * receive; see comment below for details.
 */
void
dbuf_free_range(dnode_t *dn, uint64_t start_blkid, uint64_t end_blkid,
    dmu_tx_t *tx)
{
	dmu_buf_impl_t *db_search;
	dmu_buf_impl_t *db, *db_next;
	uint64_t txg = tx->tx_txg;
	avl_index_t where;
	boolean_t freespill =
	    (start_blkid == DMU_SPILL_BLKID || end_blkid == DMU_SPILL_BLKID);

	if (end_blkid > dn->dn_maxblkid && !freespill)
		end_blkid = dn->dn_maxblkid;
	dprintf_dnode(dn, "start=%llu end=%llu\n", start_blkid, end_blkid);

	db_search = kmem_alloc(sizeof (dmu_buf_impl_t), KM_SLEEP);
	db_search->db_level = 0;
	db_search->db_blkid = start_blkid;
	db_search->db_state = DB_SEARCH;

	mutex_enter(&dn->dn_dbufs_mtx);
	if (start_blkid >= dn->dn_unlisted_l0_blkid && !freespill) {
		/* There can't be any dbufs in this range; no need to search. */
#ifdef DEBUG
		db = avl_find(&dn->dn_dbufs, db_search, &where);
		ASSERT3P(db, ==, NULL);
		db = avl_nearest(&dn->dn_dbufs, where, AVL_AFTER);
		ASSERT(db == NULL || db->db_level > 0);
#endif
		goto out;
	} else if (dmu_objset_is_receiving(dn->dn_objset)) {
		/*
		 * If we are receiving, we expect there to be no dbufs in
		 * the range to be freed, because receive modifies each
		 * block at most once, and in offset order.  If this is
		 * not the case, it can lead to performance problems,
		 * so note that we unexpectedly took the slow path.
		 */
		atomic_inc_64(&zfs_free_range_recv_miss);
	}

	db = avl_find(&dn->dn_dbufs, db_search, &where);
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
			dbuf_clear(db);
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

out:
	kmem_free(db_search, sizeof (dmu_buf_impl_t));
	mutex_exit(&dn->dn_dbufs_mtx);
}

static int
dbuf_block_freeable(dmu_buf_impl_t *db)
{
	dsl_dataset_t *ds = db->db_objset->os_dsl_dataset;
	uint64_t birth_txg = 0;

	/*
	 * We don't need any locking to protect db_blkptr:
	 * If it's syncing, then db_last_dirty will be set
	 * so we'll ignore db_blkptr.
	 *
	 * This logic ensures that only block births for
	 * filled blocks are considered.
	 */
	ASSERT(MUTEX_HELD(&db->db_mtx));
	if (db->db_last_dirty && (db->db_blkptr == NULL ||
	    !BP_IS_HOLE(db->db_blkptr))) {
		birth_txg = db->db_last_dirty->dr_txg;
	} else if (db->db_blkptr != NULL && !BP_IS_HOLE(db->db_blkptr)) {
		birth_txg = db->db_blkptr->blk_birth;
	}

	/*
	 * If this block don't exist or is in a snapshot, it can't be freed.
	 * Don't pass the bp to dsl_dataset_block_freeable() since we
	 * are holding the db_mtx lock and might deadlock if we are
	 * prefetching a dedup-ed block.
	 */
	if (birth_txg != 0)
		return (ds == NULL ||
		    dsl_dataset_block_freeable(ds, NULL, birth_txg));
	else
		return (B_FALSE);
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
	buf = arc_buf_alloc(dn->dn_objset->os_spa, size, db, type);

	/* copy old block data to the new block */
	obuf = db->db_buf;
	bcopy(obuf->b_data, buf->b_data, MIN(osize, size));
	/* zero the remainder */
	if (size > osize)
		bzero((uint8_t *)buf->b_data + osize, size - osize);

	mutex_enter(&db->db_mtx);
	dbuf_set_data(db, buf);
	VERIFY(arc_buf_remove_ref(obuf, db));
	db->db.db_size = size;

	if (db->db_level == 0) {
		ASSERT3U(db->db_last_dirty->dr_txg, ==, tx->tx_txg);
		db->db_last_dirty->dt.dl.dr_data = buf;
	}
	mutex_exit(&db->db_mtx);

	dnode_willuse_space(dn, size-osize, tx);
	DB_DNODE_EXIT(db);
}

void
dbuf_release_bp(dmu_buf_impl_t *db)
{
	ASSERTV(objset_t *os = db->db_objset);

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(os)));
	ASSERT(arc_released(os->os_phys_buf) ||
	    list_link_active(&os->os_dsl_dataset->ds_synced_link));
	ASSERT(db->db_parent == NULL || arc_released(db->db_parent->db_buf));

	(void) arc_release(db->db_buf, db);
}

dbuf_dirty_record_t *
dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	dnode_t *dn;
	objset_t *os;
	dbuf_dirty_record_t **drp, *dr;
	int drop_struct_lock = FALSE;
	boolean_t do_free_accounting = B_FALSE;
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
	ASSERT(!dmu_tx_is_syncing(tx) ||
	    BP_IS_HOLE(dn->dn_objset->os_rootbp) ||
	    DMU_OBJECT_IS_SPECIAL(dn->dn_object) ||
	    dn->dn_objset->os_dsl_dataset == NULL);
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
	if (dn->dn_dirtyctx == DN_UNDIRTIED &&
	    !BP_IS_HOLE(dn->dn_objset->os_rootbp)) {
		dn->dn_dirtyctx =
		    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN);
		ASSERT(dn->dn_dirtyctx_firstset == NULL);
		dn->dn_dirtyctx_firstset = kmem_alloc(1, KM_SLEEP);
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

		if (db->db_level == 0 && db->db_blkid != DMU_BONUS_BLKID) {
			/*
			 * If this buffer has already been written out,
			 * we now need to reset its state.
			 */
			dbuf_unoverride(dr);
			if (db->db.db_object != DMU_META_DNODE_OBJECT &&
			    db->db_state != DB_NOFILL)
				arc_buf_thaw(db->db_buf);
		}
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
	ASSERT((dn->dn_phys->dn_nlevels == 0 && db->db_level == 0) ||
	    dn->dn_phys->dn_nlevels > db->db_level ||
	    dn->dn_next_nlevels[txgoff] > db->db_level ||
	    dn->dn_next_nlevels[(tx->tx_txg-1) & TXG_MASK] > db->db_level ||
	    dn->dn_next_nlevels[(tx->tx_txg-2) & TXG_MASK] > db->db_level);

	/*
	 * We should only be dirtying in syncing context if it's the
	 * mos or we're initializing the os or it's a special object.
	 * However, we are allowed to dirty in syncing context provided
	 * we already dirtied it in open context.  Hence we must make
	 * this assertion only if we're not already dirty.
	 */
	os = dn->dn_objset;
	ASSERT(!dmu_tx_is_syncing(tx) || DMU_OBJECT_IS_SPECIAL(dn->dn_object) ||
	    os->os_dsl_dataset == NULL || BP_IS_HOLE(os->os_rootbp));
	ASSERT(db->db.db_size != 0);

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	if (db->db_blkid != DMU_BONUS_BLKID) {
		/*
		 * Update the accounting.
		 * Note: we delay "free accounting" until after we drop
		 * the db_mtx.  This keeps us from grabbing other locks
		 * (and possibly deadlocking) in bp_get_dsize() while
		 * also holding the db_mtx.
		 */
		dnode_willuse_space(dn, db->db.db_size, tx);
		do_free_accounting = dbuf_block_freeable(db);
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
	} else if (do_free_accounting) {
		blkptr_t *bp = db->db_blkptr;
		int64_t willfree = (bp && !BP_IS_HOLE(bp)) ?
		    bp_get_dsize(os->os_spa, bp) : db->db.db_size;
		/*
		 * This is only a guess -- if the dbuf is dirty
		 * in a previous txg, we don't know how much
		 * space it will use on disk yet.  We should
		 * really have the struct_rwlock to access
		 * db_blkptr, but since this is just a guess,
		 * it's OK if we get an odd answer.
		 */
		ddt_prefetch(os->os_spa, bp);
		dnode_willuse_space(dn, -willfree, tx);
	}

	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}

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
			VERIFY(arc_buf_remove_ref(dr->dt.dl.dr_data, db));
	}

	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	ASSERT(db->db_dirtycnt > 0);
	db->db_dirtycnt -= 1;

	if (refcount_remove(&db->db_holds, (void *)(uintptr_t)txg) == 0) {
		arc_buf_t *buf = db->db_buf;

		ASSERT(db->db_state == DB_NOFILL || arc_released(buf));
		dbuf_clear_data(db);
		VERIFY(arc_buf_remove_ref(buf, db));
		dbuf_evict(db);
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
	ASSERT(DBUF_GET_BUFC_TYPE(db) == ARC_BUFC_DATA);
	ASSERT(buf != NULL);
	ASSERT(arc_buf_size(buf) == db->db.db_size);
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
		VERIFY(arc_buf_remove_ref(buf, db));
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
			VERIFY(arc_buf_remove_ref(db->db_buf, db));
		} else if (dr == NULL || dr->dt.dl.dr_data != db->db_buf) {
			arc_release(db->db_buf, db);
			VERIFY(arc_buf_remove_ref(db->db_buf, db));
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

/*
 * "Clear" the contents of this dbuf.  This will mark the dbuf
 * EVICTING and clear *most* of its references.  Unfortunately,
 * when we are not holding the dn_dbufs_mtx, we can't clear the
 * entry in the dn_dbufs list.  We have to wait until dbuf_destroy()
 * in this case.  For callers from the DMU we will usually see:
 *	dbuf_clear()->arc_clear_callback()->dbuf_do_evict()->dbuf_destroy()
 * For the arc callback, we will usually see:
 *	dbuf_do_evict()->dbuf_clear();dbuf_destroy()
 * Sometimes, though, we will get a mix of these two:
 *	DMU: dbuf_clear()->arc_clear_callback()
 *	ARC: dbuf_do_evict()->dbuf_destroy()
 *
 * This routine will dissociate the dbuf from the arc, by calling
 * arc_clear_callback(), but will not evict the data from the ARC.
 */
void
dbuf_clear(dmu_buf_impl_t *db)
{
	dnode_t *dn;
	dmu_buf_impl_t *parent = db->db_parent;
	dmu_buf_impl_t *dndb;
	boolean_t dbuf_gone = B_FALSE;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(refcount_is_zero(&db->db_holds));

	dbuf_evict_user(db);

	if (db->db_state == DB_CACHED) {
		ASSERT(db->db.db_data != NULL);
		if (db->db_blkid == DMU_BONUS_BLKID) {
			zio_buf_free(db->db.db_data, DN_MAX_BONUSLEN);
			arc_space_return(DN_MAX_BONUSLEN, ARC_SPACE_OTHER);
		}
		db->db.db_data = NULL;
		db->db_state = DB_UNCACHED;
	}

	ASSERT(db->db_state == DB_UNCACHED || db->db_state == DB_NOFILL);
	ASSERT(db->db_data_pending == NULL);

	db->db_state = DB_EVICTING;
	db->db_blkptr = NULL;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	dndb = dn->dn_dbuf;
	if (db->db_blkid != DMU_BONUS_BLKID && MUTEX_HELD(&dn->dn_dbufs_mtx)) {
		avl_remove(&dn->dn_dbufs, db);
		atomic_dec_32(&dn->dn_dbufs_count);
		membar_producer();
		DB_DNODE_EXIT(db);
		/*
		 * Decrementing the dbuf count means that the hold corresponding
		 * to the removed dbuf is no longer discounted in dnode_move(),
		 * so the dnode cannot be moved until after we release the hold.
		 * The membar_producer() ensures visibility of the decremented
		 * value in dnode_move(), since DB_DNODE_EXIT doesn't actually
		 * release any lock.
		 */
		dnode_rele(dn, db);
		db->db_dnode_handle = NULL;
	} else {
		DB_DNODE_EXIT(db);
	}

	if (db->db_buf)
		dbuf_gone = arc_clear_callback(db->db_buf);

	if (!dbuf_gone)
		mutex_exit(&db->db_mtx);

	/*
	 * If this dbuf is referenced from an indirect dbuf,
	 * decrement the ref count on the indirect dbuf.
	 */
	if (parent && parent != dndb)
		dbuf_rele(parent, db);
}

__attribute__((always_inline))
static inline int
dbuf_findbp(dnode_t *dn, int level, uint64_t blkid, int fail_sparse,
    dmu_buf_impl_t **parentp, blkptr_t **bpp, struct dbuf_hold_impl_data *dh)
{
	int nlevels, epbs;

	*parentp = NULL;
	*bpp = NULL;

	ASSERT(blkid != DMU_BONUS_BLKID);

	if (blkid == DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		if (dn->dn_have_spill &&
		    (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR))
			*bpp = &dn->dn_phys->dn_spill;
		else
			*bpp = NULL;
		dbuf_add_ref(dn->dn_dbuf, NULL);
		*parentp = dn->dn_dbuf;
		mutex_exit(&dn->dn_mtx);
		return (0);
	}

	if (dn->dn_phys->dn_nlevels == 0)
		nlevels = 1;
	else
		nlevels = dn->dn_phys->dn_nlevels;

	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

	ASSERT3U(level * epbs, <, 64);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	if (level >= nlevels ||
	    (blkid > (dn->dn_phys->dn_maxblkid >> (level * epbs)))) {
		/* the buffer has no parent yet */
		return (SET_ERROR(ENOENT));
	} else if (level < nlevels-1) {
		/* this block is referenced from an indirect block */
		int err;
		if (dh == NULL) {
			err = dbuf_hold_impl(dn, level+1, blkid >> epbs,
					fail_sparse, NULL, parentp);
		} else {
			__dbuf_hold_impl_init(dh + 1, dn, dh->dh_level + 1,
					blkid >> epbs, fail_sparse, NULL,
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

	db = kmem_cache_alloc(dbuf_cache, KM_SLEEP);

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
		db->db.db_size = DN_MAX_BONUSLEN -
		    (dn->dn_nblkptr-1) * sizeof (blkptr_t);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		db->db.db_offset = DMU_BONUS_BLKID;
		db->db_state = DB_UNCACHED;
		/* the bonus dbuf is not placed in the hash table */
		arc_space_consume(sizeof (dmu_buf_impl_t), ARC_SPACE_OTHER);
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
		kmem_cache_free(dbuf_cache, db);
		mutex_exit(&dn->dn_dbufs_mtx);
		return (odb);
	}
	avl_add(&dn->dn_dbufs, db);
	if (db->db_level == 0 && db->db_blkid >=
	    dn->dn_unlisted_l0_blkid)
		dn->dn_unlisted_l0_blkid = db->db_blkid + 1;
	db->db_state = DB_UNCACHED;
	mutex_exit(&dn->dn_dbufs_mtx);
	arc_space_consume(sizeof (dmu_buf_impl_t), ARC_SPACE_OTHER);

	if (parent && parent != dn->dn_dbuf)
		dbuf_add_ref(parent, db);

	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    refcount_count(&dn->dn_holds) > 0);
	(void) refcount_add(&dn->dn_holds, db);
	atomic_inc_32(&dn->dn_dbufs_count);

	dprintf_dbuf(db, "db=%p\n", db);

	return (db);
}

static int
dbuf_do_evict(void *private)
{
	dmu_buf_impl_t *db = private;

	if (!MUTEX_HELD(&db->db_mtx))
		mutex_enter(&db->db_mtx);

	ASSERT(refcount_is_zero(&db->db_holds));

	if (db->db_state != DB_EVICTING) {
		ASSERT(db->db_state == DB_CACHED);
		DBUF_VERIFY(db);
		db->db_buf = NULL;
		dbuf_evict(db);
	} else {
		mutex_exit(&db->db_mtx);
		dbuf_destroy(db);
	}
	return (0);
}

static void
dbuf_destroy(dmu_buf_impl_t *db)
{
	ASSERT(refcount_is_zero(&db->db_holds));

	if (db->db_blkid != DMU_BONUS_BLKID) {
		/*
		 * If this dbuf is still on the dn_dbufs list,
		 * remove it from that list.
		 */
		if (db->db_dnode_handle != NULL) {
			dnode_t *dn;

			DB_DNODE_ENTER(db);
			dn = DB_DNODE(db);
			mutex_enter(&dn->dn_dbufs_mtx);
			avl_remove(&dn->dn_dbufs, db);
			atomic_dec_32(&dn->dn_dbufs_count);
			mutex_exit(&dn->dn_dbufs_mtx);
			DB_DNODE_EXIT(db);
			/*
			 * Decrementing the dbuf count means that the hold
			 * corresponding to the removed dbuf is no longer
			 * discounted in dnode_move(), so the dnode cannot be
			 * moved until after we release the hold.
			 */
			dnode_rele(dn, db);
			db->db_dnode_handle = NULL;
		}
		dbuf_hash_remove(db);
	}
	db->db_parent = NULL;
	db->db_buf = NULL;

	ASSERT(db->db.db_data == NULL);
	ASSERT(db->db_hash_next == NULL);
	ASSERT(db->db_blkptr == NULL);
	ASSERT(db->db_data_pending == NULL);

	kmem_cache_free(dbuf_cache, db);
	arc_space_return(sizeof (dmu_buf_impl_t), ARC_SPACE_OTHER);
}

void
dbuf_prefetch(dnode_t *dn, uint64_t blkid, zio_priority_t prio)
{
	dmu_buf_impl_t *db = NULL;
	blkptr_t *bp = NULL;

	ASSERT(blkid != DMU_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));

	if (dnode_block_freed(dn, blkid))
		return;

	/* dbuf_find() returns with db_mtx held */
	if ((db = dbuf_find(dn->dn_objset, dn->dn_object, 0, blkid))) {
		/*
		 * This dbuf is already in the cache.  We assume that
		 * it is already CACHED, or else about to be either
		 * read or filled.
		 */
		mutex_exit(&db->db_mtx);
		return;
	}

	if (dbuf_findbp(dn, 0, blkid, TRUE, &db, &bp, NULL) == 0) {
		if (bp && !BP_IS_HOLE(bp) && !BP_IS_EMBEDDED(bp)) {
			dsl_dataset_t *ds = dn->dn_objset->os_dsl_dataset;
			arc_flags_t aflags =
			    ARC_FLAG_NOWAIT | ARC_FLAG_PREFETCH;
			zbookmark_phys_t zb;

			SET_BOOKMARK(&zb, ds ? ds->ds_object : DMU_META_OBJSET,
			    dn->dn_object, 0, blkid);

			(void) arc_read(NULL, dn->dn_objset->os_spa,
			    bp, NULL, NULL, prio,
			    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE,
			    &aflags, &zb);
		}
		if (db)
			dbuf_rele(db, NULL);
	}
}

#define	DBUF_HOLD_IMPL_MAX_DEPTH	20

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
top:
	/* dbuf_find() returns with db_mtx held */
	dh->dh_db = dbuf_find(dh->dh_dn->dn_objset, dh->dh_dn->dn_object,
	    dh->dh_level, dh->dh_blkid);

	if (dh->dh_db == NULL) {
		dh->dh_bp = NULL;

		ASSERT3P(dh->dh_parent, ==, NULL);
		dh->dh_err = dbuf_findbp(dh->dh_dn, dh->dh_level, dh->dh_blkid,
					dh->dh_fail_sparse, &dh->dh_parent,
					&dh->dh_bp, dh);
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

	if (dh->dh_db->db_buf && refcount_is_zero(&dh->dh_db->db_holds)) {
		arc_buf_add_ref(dh->dh_db->db_buf, dh->dh_db);
		if (dh->dh_db->db_buf->b_data == NULL) {
			dbuf_clear(dh->dh_db);
			if (dh->dh_parent) {
				dbuf_rele(dh->dh_parent, NULL);
				dh->dh_parent = NULL;
			}
			goto top;
		}
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

		if (dh->dh_dr->dt.dl.dr_data == dh->dh_db->db_buf) {
			dh->dh_type = DBUF_GET_BUFC_TYPE(dh->dh_db);

			dbuf_set_data(dh->dh_db,
			    arc_buf_alloc(dh->dh_dn->dn_objset->os_spa,
			    dh->dh_db->db.db_size, dh->dh_db, dh->dh_type));
			bcopy(dh->dh_dr->dt.dl.dr_data->b_data,
			    dh->dh_db->db.db_data, dh->dh_db->db.db_size);
		}
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
dbuf_hold_impl(dnode_t *dn, uint8_t level, uint64_t blkid, int fail_sparse,
    void *tag, dmu_buf_impl_t **dbp)
{
	struct dbuf_hold_impl_data *dh;
	int error;

	dh = kmem_zalloc(sizeof (struct dbuf_hold_impl_data) *
	    DBUF_HOLD_IMPL_MAX_DEPTH, KM_SLEEP);
	__dbuf_hold_impl_init(dh, dn, level, blkid, fail_sparse, tag, dbp, 0);

	error = __dbuf_hold_impl(dh);

	kmem_free(dh, sizeof (struct dbuf_hold_impl_data) *
	    DBUF_HOLD_IMPL_MAX_DEPTH);

	return (error);
}

static void
__dbuf_hold_impl_init(struct dbuf_hold_impl_data *dh,
    dnode_t *dn, uint8_t level, uint64_t blkid, int fail_sparse,
    void *tag, dmu_buf_impl_t **dbp, int depth)
{
	dh->dh_dn = dn;
	dh->dh_level = level;
	dh->dh_blkid = blkid;
	dh->dh_fail_sparse = fail_sparse;
	dh->dh_tag = tag;
	dh->dh_dbp = dbp;
	dh->dh_depth = depth;
}

dmu_buf_impl_t *
dbuf_hold(dnode_t *dn, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, 0, blkid, FALSE, tag, &db);
	return (err ? NULL : db);
}

dmu_buf_impl_t *
dbuf_hold_level(dnode_t *dn, int level, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, level, blkid, FALSE, tag, &db);
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
	VERIFY(refcount_add(&db->db_holds, tag) > 1);
}

#pragma weak dmu_buf_try_add_ref = dbuf_try_add_ref
boolean_t
dbuf_try_add_ref(dmu_buf_t *db_fake, objset_t *os, uint64_t obj, uint64_t blkid,
    void *tag)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dmu_buf_impl_t *found_db;
	boolean_t result = B_FALSE;

	if (blkid == DMU_BONUS_BLKID)
		found_db = dbuf_find_bonus(os, obj);
	else
		found_db = dbuf_find(os, obj, 0, blkid);

	if (found_db != NULL) {
		if (db == found_db && dbuf_refcount(db) > db->db_dirtycnt) {
			(void) refcount_add(&db->db_holds, tag);
			result = B_TRUE;
		}
		mutex_exit(&found_db->db_mtx);
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
	dbuf_rele_and_unlock(db, tag);
}

void
dmu_buf_rele(dmu_buf_t *db, void *tag)
{
	dbuf_rele((dmu_buf_impl_t *)db, tag);
}

/*
 * dbuf_rele() for an already-locked dbuf.  This is necessary to allow
 * db_dirtycnt and db_holds to be updated atomically.
 */
void
dbuf_rele_and_unlock(dmu_buf_impl_t *db, void *tag)
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
	if (db->db_buf && holds == (db->db_level == 0 ? db->db_dirtycnt : 0))
		arc_buf_freeze(db->db_buf);

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
			dbuf_evict(db);
		} else if (arc_released(db->db_buf)) {
			arc_buf_t *buf = db->db_buf;
			/*
			 * This dbuf has anonymous data associated with it.
			 */
			dbuf_clear_data(db);
			VERIFY(arc_buf_remove_ref(buf, db));
			dbuf_evict(db);
		} else {
			VERIFY(!arc_buf_remove_ref(db->db_buf, db));

			/*
			 * A dbuf will be eligible for eviction if either the
			 * 'primarycache' property is set or a duplicate
			 * copy of this buffer is already cached in the arc.
			 *
			 * In the case of the 'primarycache' a buffer
			 * is considered for eviction if it matches the
			 * criteria set in the property.
			 *
			 * To decide if our buffer is considered a
			 * duplicate, we must call into the arc to determine
			 * if multiple buffers are referencing the same
			 * block on-disk. If so, then we simply evict
			 * ourselves.
			 */
			if (!DBUF_IS_CACHEABLE(db)) {
				if (db->db_blkptr != NULL &&
				    !BP_IS_HOLE(db->db_blkptr) &&
				    !BP_IS_EMBEDDED(db->db_blkptr)) {
					spa_t *spa =
					    dmu_objset_spa(db->db_objset);
					blkptr_t bp = *db->db_blkptr;
					dbuf_clear(db);
					arc_freed(spa, &bp);
				} else {
					dbuf_clear(db);
				}
			} else if (db->db_pending_evict ||
			    arc_buf_eviction_needed(db->db_buf)) {
				dbuf_clear(db);
			} else {
				mutex_exit(&db->db_mtx);
			}
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

boolean_t
dmu_buf_freeable(dmu_buf_t *dbuf)
{
	boolean_t res = B_FALSE;
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbuf;

	if (db->db_blkptr)
		res = dsl_dataset_block_freeable(db->db_objset->os_dsl_dataset,
		    db->db_blkptr, db->db_blkptr->blk_birth);

	return (res);
}

blkptr_t *
dmu_buf_get_blkptr(dmu_buf_t *db)
{
	dmu_buf_impl_t *dbi = (dmu_buf_impl_t *)db;
	return (dbi->db_blkptr);
}

static void
dbuf_check_blkptr(dnode_t *dn, dmu_buf_impl_t *db)
{
	/* ASSERT(dmu_tx_is_syncing(tx) */
	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (db->db_blkptr != NULL)
		return;

	if (db->db_blkid == DMU_SPILL_BLKID) {
		db->db_blkptr = &dn->dn_phys->dn_spill;
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
			(void) dbuf_hold_impl(dn, db->db_level+1,
			    db->db_blkid >> epbs, FALSE, db, &parent);
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
		ASSERT3U(dn->dn_phys->dn_bonuslen, <=, DN_MAX_BONUSLEN);
		bcopy(*datap, DN_BONUS(dn->dn_phys), dn->dn_phys->dn_bonuslen);
		DB_DNODE_EXIT(db);

		if (*datap != db->db.db_data) {
			zio_buf_free(*datap, DN_MAX_BONUSLEN);
			arc_space_return(DN_MAX_BONUSLEN, ARC_SPACE_OTHER);
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
		dbuf_rele_and_unlock(db, (void *)(uintptr_t)txg);
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
		int blksz = arc_buf_size(*datap);
		arc_buf_contents_t type = DBUF_GET_BUFC_TYPE(db);
		*datap = arc_buf_alloc(os->os_spa, blksz, db, type);
		bcopy(db->db.db_data, (*datap)->b_data, blksz);
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

	while ((dr = list_head(list))) {
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

	ASSERT3P(db->db_blkptr, ==, bp);

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
		ASSERT(!(BP_IS_HOLE(db->db_blkptr)) &&
		    db->db_blkptr == &dn->dn_phys->dn_spill);
	}
#endif

	if (db->db_level == 0) {
		mutex_enter(&dn->dn_mtx);
		if (db->db_blkid > dn->dn_phys->dn_maxblkid &&
		    db->db_blkid != DMU_SPILL_BLKID)
			dn->dn_phys->dn_maxblkid = db->db_blkid;
		mutex_exit(&dn->dn_mtx);

		if (dn->dn_type == DMU_OT_DNODE) {
			dnode_phys_t *dnp = db->db.db_data;
			for (i = db->db.db_size >> DNODE_SHIFT; i > 0;
			    i--, dnp++) {
				if (dnp->dn_type != DMU_OT_NONE)
					fill++;
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
		    db->db_blkptr == &dn->dn_phys->dn_spill);
		DB_DNODE_EXIT(db);
	}
#endif

	if (db->db_level == 0) {
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);
		ASSERT(dr->dt.dl.dr_override_state == DR_NOT_OVERRIDDEN);
		if (db->db_state != DB_NOFILL) {
			if (dr->dt.dl.dr_data != db->db_buf)
				VERIFY(arc_buf_remove_ref(dr->dt.dl.dr_data,
				    db));
			else if (!arc_released(db->db_buf))
				arc_set_callback(db->db_buf, dbuf_do_evict, db);
		}
	} else {
		dnode_t *dn;

		DB_DNODE_ENTER(db);
		dn = DB_DNODE(db);
		ASSERT(list_head(&dr->dt.di.dr_children) == NULL);
		ASSERT3U(db->db.db_size, ==, 1 << dn->dn_phys->dn_indblkshift);
		if (!BP_IS_HOLE(db->db_blkptr)) {
			ASSERTV(int epbs = dn->dn_phys->dn_indblkshift -
			    SPA_BLKPTRSHIFT);
			ASSERT3U(db->db_blkid, <=,
			    dn->dn_phys->dn_maxblkid >> (db->db_level * epbs));
			ASSERT3U(BP_GET_LSIZE(db->db_blkptr), ==,
			    db->db.db_size);
			if (!arc_released(db->db_buf))
				arc_set_callback(db->db_buf, dbuf_do_evict, db);
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
	dbuf_rele_and_unlock(db, (void *)(uintptr_t)tx->tx_txg);
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

	if (db->db_level == 0 &&
	    dr->dt.dl.dr_override_state == DR_OVERRIDDEN) {
		/*
		 * The BP for this block has been provided by open context
		 * (by dmu_sync() or dmu_buf_write_embedded()).
		 */
		void *contents = (data != NULL) ? data->b_data : NULL;

		dr->dr_zio = zio_write(zio, os->os_spa, txg,
		    db->db_blkptr, contents, db->db.db_size, &zp,
		    dbuf_write_override_ready, NULL, dbuf_write_override_done,
		    dr, ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
		mutex_enter(&db->db_mtx);
		dr->dt.dl.dr_override_state = DR_NOT_OVERRIDDEN;
		zio_write_override(dr->dr_zio, &dr->dt.dl.dr_overridden_by,
		    dr->dt.dl.dr_copies, dr->dt.dl.dr_nopwrite);
		mutex_exit(&db->db_mtx);
	} else if (db->db_state == DB_NOFILL) {
		ASSERT(zp.zp_checksum == ZIO_CHECKSUM_OFF);
		dr->dr_zio = zio_write(zio, os->os_spa, txg,
		    db->db_blkptr, NULL, db->db.db_size, &zp,
		    dbuf_write_nofill_ready, NULL, dbuf_write_nofill_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE,
		    ZIO_FLAG_MUSTSUCCEED | ZIO_FLAG_NODATA, &zb);
	} else {
		ASSERT(arc_released(data));
		dr->dr_zio = arc_write(zio, os->os_spa, txg,
		    db->db_blkptr, data, DBUF_IS_L2CACHEABLE(db),
		    DBUF_IS_L2COMPRESSIBLE(db), &zp, dbuf_write_ready,
		    dbuf_write_physdone, dbuf_write_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
	}
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dbuf_find);
EXPORT_SYMBOL(dbuf_is_metadata);
EXPORT_SYMBOL(dbuf_evict);
EXPORT_SYMBOL(dbuf_loan_arcbuf);
EXPORT_SYMBOL(dbuf_whichblock);
EXPORT_SYMBOL(dbuf_read);
EXPORT_SYMBOL(dbuf_unoverride);
EXPORT_SYMBOL(dbuf_free_range);
EXPORT_SYMBOL(dbuf_new_size);
EXPORT_SYMBOL(dbuf_release_bp);
EXPORT_SYMBOL(dbuf_dirty);
EXPORT_SYMBOL(dmu_buf_will_dirty);
EXPORT_SYMBOL(dmu_buf_will_not_fill);
EXPORT_SYMBOL(dmu_buf_will_fill);
EXPORT_SYMBOL(dmu_buf_fill_done);
EXPORT_SYMBOL(dmu_buf_rele);
EXPORT_SYMBOL(dbuf_assign_arcbuf);
EXPORT_SYMBOL(dbuf_clear);
EXPORT_SYMBOL(dbuf_prefetch);
EXPORT_SYMBOL(dbuf_hold_impl);
EXPORT_SYMBOL(dbuf_hold);
EXPORT_SYMBOL(dbuf_hold_level);
EXPORT_SYMBOL(dbuf_create_bonus);
EXPORT_SYMBOL(dbuf_spill_set_blksz);
EXPORT_SYMBOL(dbuf_rm_spill);
EXPORT_SYMBOL(dbuf_add_ref);
EXPORT_SYMBOL(dbuf_rele);
EXPORT_SYMBOL(dbuf_rele_and_unlock);
EXPORT_SYMBOL(dbuf_refcount);
EXPORT_SYMBOL(dbuf_sync_list);
EXPORT_SYMBOL(dmu_buf_set_user);
EXPORT_SYMBOL(dmu_buf_set_user_ie);
EXPORT_SYMBOL(dmu_buf_get_user);
EXPORT_SYMBOL(dmu_buf_freeable);
EXPORT_SYMBOL(dmu_buf_get_blkptr);
#endif
