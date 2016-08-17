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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#ifndef	_SYS_DBUF_H
#define	_SYS_DBUF_H

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/zfs_context.h>
#include <sys/refcount.h>
#include <sys/zrlock.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	IN_DMU_SYNC 2

/*
 * define flags for dbuf_read
 */

#define	DB_RF_MUST_SUCCEED	(1 << 0)
#define	DB_RF_CANFAIL		(1 << 1)
#define	DB_RF_HAVESTRUCT	(1 << 2)
#define	DB_RF_NOPREFETCH	(1 << 3)
#define	DB_RF_NEVERWAIT		(1 << 4)
#define	DB_RF_CACHED		(1 << 5)

/*
 * The simplified state transition diagram for dbufs looks like:
 *
 *		+----> READ ----+
 *		|		|
 *		|		V
 *  (alloc)-->UNCACHED	     CACHED-->EVICTING-->(free)
 *		|		^	 ^
 *		|		|	 |
 *		+----> FILL ----+	 |
 *		|			 |
 *		|			 |
 *		+--------> NOFILL -------+
 *
 * DB_SEARCH is an invalid state for a dbuf. It is used by dbuf_free_range
 * to find all dbufs in a range of a dnode and must be less than any other
 * dbuf_states_t (see comment on dn_dbufs in dnode.h).
 */
typedef enum dbuf_states {
	DB_SEARCH = -1,
	DB_UNCACHED,
	DB_FILL,
	DB_NOFILL,
	DB_READ,
	DB_CACHED,
	DB_EVICTING
} dbuf_states_t;

struct dnode;
struct dmu_tx;

/*
 * level = 0 means the user data
 * level = 1 means the single indirect block
 * etc.
 */

struct dmu_buf_impl;

typedef enum override_states {
	DR_NOT_OVERRIDDEN,
	DR_IN_DMU_SYNC,
	DR_OVERRIDDEN
} override_states_t;

typedef struct dbuf_dirty_record {
	/* link on our parents dirty list */
	list_node_t dr_dirty_node;

	/* transaction group this data will sync in */
	uint64_t dr_txg;

	/* zio of outstanding write IO */
	zio_t *dr_zio;

	/* pointer back to our dbuf */
	struct dmu_buf_impl *dr_dbuf;

	/* pointer to next dirty record */
	struct dbuf_dirty_record *dr_next;

	/* pointer to parent dirty record */
	struct dbuf_dirty_record *dr_parent;

	/* How much space was changed to dsl_pool_dirty_space() for this? */
	unsigned int dr_accounted;

	union dirty_types {
		struct dirty_indirect {

			/* protect access to list */
			kmutex_t dr_mtx;

			/* Our list of dirty children */
			list_t dr_children;
		} di;
		struct dirty_leaf {

			/*
			 * dr_data is set when we dirty the buffer
			 * so that we can retain the pointer even if it
			 * gets COW'd in a subsequent transaction group.
			 */
			arc_buf_t *dr_data;
			blkptr_t dr_overridden_by;
			override_states_t dr_override_state;
			uint8_t dr_copies;
			boolean_t dr_nopwrite;
		} dl;
	} dt;
} dbuf_dirty_record_t;

typedef struct dmu_buf_impl {
	/*
	 * The following members are immutable, with the exception of
	 * db.db_data, which is protected by db_mtx.
	 */

	/* the publicly visible structure */
	dmu_buf_t db;

	/* the objset we belong to */
	struct objset *db_objset;

	/*
	 * handle to safely access the dnode we belong to (NULL when evicted)
	 */
	struct dnode_handle *db_dnode_handle;

	/*
	 * our parent buffer; if the dnode points to us directly,
	 * db_parent == db_dnode_handle->dnh_dnode->dn_dbuf
	 * only accessed by sync thread ???
	 * (NULL when evicted)
	 * May change from NULL to non-NULL under the protection of db_mtx
	 * (see dbuf_check_blkptr())
	 */
	struct dmu_buf_impl *db_parent;

	/*
	 * link for hash table of all dmu_buf_impl_t's
	 */
	struct dmu_buf_impl *db_hash_next;

	/* our block number */
	uint64_t db_blkid;

	/*
	 * Pointer to the blkptr_t which points to us. May be NULL if we
	 * don't have one yet. (NULL when evicted)
	 */
	blkptr_t *db_blkptr;

	/*
	 * Our indirection level.  Data buffers have db_level==0.
	 * Indirect buffers which point to data buffers have
	 * db_level==1. etc.  Buffers which contain dnodes have
	 * db_level==0, since the dnodes are stored in a file.
	 */
	uint8_t db_level;

	/* db_mtx protects the members below */
	kmutex_t db_mtx;

	/*
	 * Current state of the buffer
	 */
	dbuf_states_t db_state;

	/*
	 * Refcount accessed by dmu_buf_{hold,rele}.
	 * If nonzero, the buffer can't be destroyed.
	 * Protected by db_mtx.
	 */
	refcount_t db_holds;

	/* buffer holding our data */
	arc_buf_t *db_buf;

	kcondvar_t db_changed;
	dbuf_dirty_record_t *db_data_pending;

	/* pointer to most recent dirty record for this buffer */
	dbuf_dirty_record_t *db_last_dirty;

	/*
	 * Our link on the owner dnodes's dn_dbufs list.
	 * Protected by its dn_dbufs_mtx.
	 */
	avl_node_t db_link;

	/* Data which is unique to data (leaf) blocks: */

	/* User callback information. */
	dmu_buf_user_t *db_user;

	/*
	 * Evict user data as soon as the dirty and reference
	 * counts are equal.
	 */
	uint8_t db_user_immediate_evict;

	/*
	 * This block was freed while a read or write was
	 * active.
	 */
	uint8_t db_freed_in_flight;

	/*
	 * dnode_evict_dbufs() or dnode_evict_bonus() tried to
	 * evict this dbuf, but couldn't due to outstanding
	 * references.  Evict once the refcount drops to 0.
	 */
	uint8_t db_pending_evict;

	uint8_t db_dirtycnt;
} dmu_buf_impl_t;

/* Note: the dbuf hash table is exposed only for the mdb module */
#define	DBUF_MUTEXES 8192
#define	DBUF_HASH_MUTEX(h, idx) (&(h)->hash_mutexes[(idx) & (DBUF_MUTEXES-1)])
typedef struct dbuf_hash_table {
	uint64_t hash_table_mask;
	dmu_buf_impl_t **hash_table;
	kmutex_t hash_mutexes[DBUF_MUTEXES];
} dbuf_hash_table_t;


uint64_t dbuf_whichblock(struct dnode *di, uint64_t offset);

void dbuf_create_bonus(struct dnode *dn);
int dbuf_spill_set_blksz(dmu_buf_t *db, uint64_t blksz, dmu_tx_t *tx);

void dbuf_rm_spill(struct dnode *dn, dmu_tx_t *tx);

dmu_buf_impl_t *dbuf_hold(struct dnode *dn, uint64_t blkid, void *tag);
dmu_buf_impl_t *dbuf_hold_level(struct dnode *dn, int level, uint64_t blkid,
    void *tag);
int dbuf_hold_impl(struct dnode *dn, uint8_t level, uint64_t blkid, int create,
    void *tag, dmu_buf_impl_t **dbp);

void dbuf_prefetch(struct dnode *dn, uint64_t blkid, zio_priority_t prio);

void dbuf_add_ref(dmu_buf_impl_t *db, void *tag);
boolean_t dbuf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t obj,
    uint64_t blkid, void *tag);
uint64_t dbuf_refcount(dmu_buf_impl_t *db);

void dbuf_rele(dmu_buf_impl_t *db, void *tag);
void dbuf_rele_and_unlock(dmu_buf_impl_t *db, void *tag);

dmu_buf_impl_t *dbuf_find(struct objset *os, uint64_t object, uint8_t level,
    uint64_t blkid);

int dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags);
void dmu_buf_will_not_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_will_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_fill_done(dmu_buf_t *db, dmu_tx_t *tx);
void dbuf_assign_arcbuf(dmu_buf_impl_t *db, arc_buf_t *buf, dmu_tx_t *tx);
dbuf_dirty_record_t *dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
arc_buf_t *dbuf_loan_arcbuf(dmu_buf_impl_t *db);
void dmu_buf_write_embedded(dmu_buf_t *dbuf, void *data,
    bp_embedded_type_t etype, enum zio_compress comp,
    int uncompressed_size, int compressed_size, int byteorder, dmu_tx_t *tx);

void dbuf_clear(dmu_buf_impl_t *db);
void dbuf_evict(dmu_buf_impl_t *db);

void dbuf_unoverride(dbuf_dirty_record_t *dr);
void dbuf_sync_list(list_t *list, int level, dmu_tx_t *tx);
void dbuf_release_bp(dmu_buf_impl_t *db);

void dbuf_free_range(struct dnode *dn, uint64_t start, uint64_t end,
    struct dmu_tx *);

void dbuf_new_size(dmu_buf_impl_t *db, int size, dmu_tx_t *tx);

void dbuf_stats_init(dbuf_hash_table_t *hash);
void dbuf_stats_destroy(void);

#define	DB_DNODE(_db)		((_db)->db_dnode_handle->dnh_dnode)
#define	DB_DNODE_LOCK(_db)	((_db)->db_dnode_handle->dnh_zrlock)
#define	DB_DNODE_ENTER(_db)	(zrl_add(&DB_DNODE_LOCK(_db)))
#define	DB_DNODE_EXIT(_db)	(zrl_remove(&DB_DNODE_LOCK(_db)))
#define	DB_DNODE_HELD(_db)	(!zrl_is_zero(&DB_DNODE_LOCK(_db)))

void dbuf_init(void);
void dbuf_fini(void);

boolean_t dbuf_is_metadata(dmu_buf_impl_t *db);

#define	DBUF_GET_BUFC_TYPE(_db)	\
	(dbuf_is_metadata(_db) ? ARC_BUFC_METADATA : ARC_BUFC_DATA)

#define	DBUF_IS_CACHEABLE(_db)						\
	((_db)->db_objset->os_primary_cache == ZFS_CACHE_ALL ||		\
	(dbuf_is_metadata(_db) &&					\
	((_db)->db_objset->os_primary_cache == ZFS_CACHE_METADATA)))

#define	DBUF_IS_L2CACHEABLE(_db)					\
	((_db)->db_objset->os_secondary_cache == ZFS_CACHE_ALL ||	\
	(dbuf_is_metadata(_db) &&					\
	((_db)->db_objset->os_secondary_cache == ZFS_CACHE_METADATA)))

#define	DBUF_IS_L2COMPRESSIBLE(_db)					\
	((_db)->db_objset->os_compress != ZIO_COMPRESS_OFF ||		\
	(dbuf_is_metadata(_db) && zfs_mdcomp_disable == B_FALSE))

#ifdef ZFS_DEBUG

/*
 * There should be a ## between the string literal and fmt, to make it
 * clear that we're joining two strings together, but gcc does not
 * support that preprocessor token.
 */
#define	dprintf_dbuf(dbuf, fmt, ...) do { \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { \
	char __db_buf[32]; \
	uint64_t __db_obj = (dbuf)->db.db_object; \
	if (__db_obj == DMU_META_DNODE_OBJECT) \
		(void) strcpy(__db_buf, "mdn"); \
	else \
		(void) snprintf(__db_buf, sizeof (__db_buf), "%lld", \
		    (u_longlong_t)__db_obj); \
	dprintf_ds((dbuf)->db_objset->os_dsl_dataset, \
	    "obj=%s lvl=%u blkid=%lld " fmt, \
	    __db_buf, (dbuf)->db_level, \
	    (u_longlong_t)(dbuf)->db_blkid, __VA_ARGS__); \
	} \
_NOTE(CONSTCOND) } while (0)

#define	dprintf_dbuf_bp(db, bp, fmt, ...) do {			\
	if (zfs_flags & ZFS_DEBUG_DPRINTF) {			\
	char *__blkbuf = kmem_alloc(BP_SPRINTF_LEN, KM_SLEEP);	\
	snprintf_blkptr(__blkbuf, BP_SPRINTF_LEN, bp);		\
	dprintf_dbuf(db, fmt " %s\n", __VA_ARGS__, __blkbuf);	\
	kmem_free(__blkbuf, BP_SPRINTF_LEN);			\
	}							\
_NOTE(CONSTCOND) } while (0)

#define	DBUF_VERIFY(db)	dbuf_verify(db)

#else

#define	dprintf_dbuf(db, fmt, ...)
#define	dprintf_dbuf_bp(db, bp, fmt, ...)
#define	DBUF_VERIFY(db)

#endif


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DBUF_H */
