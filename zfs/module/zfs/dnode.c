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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_zfetch.h>
#include <sys/range_tree.h>
#include <sys/trace_dnode.h>

dnode_stats_t dnode_stats = {
	{ "dnode_hold_dbuf_hold",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_dbuf_read",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_alloc_hits",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_alloc_misses",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_alloc_interior",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_alloc_lock_retry",	KSTAT_DATA_UINT64 },
	{ "dnode_hold_alloc_lock_misses",	KSTAT_DATA_UINT64 },
	{ "dnode_hold_alloc_type_none",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_hits",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_misses",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_lock_misses",	KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_lock_retry",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_overflow",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_refcount",		KSTAT_DATA_UINT64 },
	{ "dnode_hold_free_txg",		KSTAT_DATA_UINT64 },
	{ "dnode_allocate",			KSTAT_DATA_UINT64 },
	{ "dnode_reallocate",			KSTAT_DATA_UINT64 },
	{ "dnode_buf_evict",			KSTAT_DATA_UINT64 },
	{ "dnode_alloc_next_chunk",		KSTAT_DATA_UINT64 },
	{ "dnode_alloc_race",			KSTAT_DATA_UINT64 },
	{ "dnode_alloc_next_block",		KSTAT_DATA_UINT64 },
	{ "dnode_move_invalid",			KSTAT_DATA_UINT64 },
	{ "dnode_move_recheck1",		KSTAT_DATA_UINT64 },
	{ "dnode_move_recheck2",		KSTAT_DATA_UINT64 },
	{ "dnode_move_special",			KSTAT_DATA_UINT64 },
	{ "dnode_move_handle",			KSTAT_DATA_UINT64 },
	{ "dnode_move_rwlock",			KSTAT_DATA_UINT64 },
	{ "dnode_move_active",			KSTAT_DATA_UINT64 },
};

static kstat_t *dnode_ksp;
static kmem_cache_t *dnode_cache;

ASSERTV(static dnode_phys_t dnode_phys_zero);

int zfs_default_bs = SPA_MINBLOCKSHIFT;
int zfs_default_ibs = DN_MAX_INDBLKSHIFT;

#ifdef	_KERNEL
static kmem_cbrc_t dnode_move(void *, void *, size_t, void *);
#endif /* _KERNEL */

static int
dbuf_compare(const void *x1, const void *x2)
{
	const dmu_buf_impl_t *d1 = x1;
	const dmu_buf_impl_t *d2 = x2;

	int cmp = AVL_CMP(d1->db_level, d2->db_level);
	if (likely(cmp))
		return (cmp);

	cmp = AVL_CMP(d1->db_blkid, d2->db_blkid);
	if (likely(cmp))
		return (cmp);

	if (d1->db_state == DB_SEARCH) {
		ASSERT3S(d2->db_state, !=, DB_SEARCH);
		return (-1);
	} else if (d2->db_state == DB_SEARCH) {
		ASSERT3S(d1->db_state, !=, DB_SEARCH);
		return (1);
	}

	return (AVL_PCMP(d1, d2));
}

/* ARGSUSED */
static int
dnode_cons(void *arg, void *unused, int kmflag)
{
	dnode_t *dn = arg;
	int i;

	rw_init(&dn->dn_struct_rwlock, NULL, RW_NOLOCKDEP, NULL);
	mutex_init(&dn->dn_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&dn->dn_dbufs_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dn->dn_notxholds, NULL, CV_DEFAULT, NULL);

	/*
	 * Every dbuf has a reference, and dropping a tracked reference is
	 * O(number of references), so don't track dn_holds.
	 */
	refcount_create_untracked(&dn->dn_holds);
	refcount_create(&dn->dn_tx_holds);
	list_link_init(&dn->dn_link);

	bzero(&dn->dn_next_nblkptr[0], sizeof (dn->dn_next_nblkptr));
	bzero(&dn->dn_next_nlevels[0], sizeof (dn->dn_next_nlevels));
	bzero(&dn->dn_next_indblkshift[0], sizeof (dn->dn_next_indblkshift));
	bzero(&dn->dn_next_bonustype[0], sizeof (dn->dn_next_bonustype));
	bzero(&dn->dn_rm_spillblk[0], sizeof (dn->dn_rm_spillblk));
	bzero(&dn->dn_next_bonuslen[0], sizeof (dn->dn_next_bonuslen));
	bzero(&dn->dn_next_blksz[0], sizeof (dn->dn_next_blksz));

	for (i = 0; i < TXG_SIZE; i++) {
		list_link_init(&dn->dn_dirty_link[i]);
		dn->dn_free_ranges[i] = NULL;
		list_create(&dn->dn_dirty_records[i],
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
	}

	dn->dn_allocated_txg = 0;
	dn->dn_free_txg = 0;
	dn->dn_assigned_txg = 0;
	dn->dn_dirtyctx = 0;
	dn->dn_dirtyctx_firstset = NULL;
	dn->dn_bonus = NULL;
	dn->dn_have_spill = B_FALSE;
	dn->dn_zio = NULL;
	dn->dn_oldused = 0;
	dn->dn_oldflags = 0;
	dn->dn_olduid = 0;
	dn->dn_oldgid = 0;
	dn->dn_newuid = 0;
	dn->dn_newgid = 0;
	dn->dn_id_flags = 0;

	dn->dn_dbufs_count = 0;
	avl_create(&dn->dn_dbufs, dbuf_compare, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));

	dn->dn_moved = 0;
	return (0);
}

/* ARGSUSED */
static void
dnode_dest(void *arg, void *unused)
{
	int i;
	dnode_t *dn = arg;

	rw_destroy(&dn->dn_struct_rwlock);
	mutex_destroy(&dn->dn_mtx);
	mutex_destroy(&dn->dn_dbufs_mtx);
	cv_destroy(&dn->dn_notxholds);
	refcount_destroy(&dn->dn_holds);
	refcount_destroy(&dn->dn_tx_holds);
	ASSERT(!list_link_active(&dn->dn_link));

	for (i = 0; i < TXG_SIZE; i++) {
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));
		ASSERT3P(dn->dn_free_ranges[i], ==, NULL);
		list_destroy(&dn->dn_dirty_records[i]);
		ASSERT0(dn->dn_next_nblkptr[i]);
		ASSERT0(dn->dn_next_nlevels[i]);
		ASSERT0(dn->dn_next_indblkshift[i]);
		ASSERT0(dn->dn_next_bonustype[i]);
		ASSERT0(dn->dn_rm_spillblk[i]);
		ASSERT0(dn->dn_next_bonuslen[i]);
		ASSERT0(dn->dn_next_blksz[i]);
	}

	ASSERT0(dn->dn_allocated_txg);
	ASSERT0(dn->dn_free_txg);
	ASSERT0(dn->dn_assigned_txg);
	ASSERT0(dn->dn_dirtyctx);
	ASSERT3P(dn->dn_dirtyctx_firstset, ==, NULL);
	ASSERT3P(dn->dn_bonus, ==, NULL);
	ASSERT(!dn->dn_have_spill);
	ASSERT3P(dn->dn_zio, ==, NULL);
	ASSERT0(dn->dn_oldused);
	ASSERT0(dn->dn_oldflags);
	ASSERT0(dn->dn_olduid);
	ASSERT0(dn->dn_oldgid);
	ASSERT0(dn->dn_newuid);
	ASSERT0(dn->dn_newgid);
	ASSERT0(dn->dn_id_flags);

	ASSERT0(dn->dn_dbufs_count);
	avl_destroy(&dn->dn_dbufs);
}

void
dnode_init(void)
{
	ASSERT(dnode_cache == NULL);
	dnode_cache = kmem_cache_create("dnode_t", sizeof (dnode_t),
	    0, dnode_cons, dnode_dest, NULL, NULL, NULL, 0);
	kmem_cache_set_move(dnode_cache, dnode_move);

	dnode_ksp = kstat_create("zfs", 0, "dnodestats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (dnode_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (dnode_ksp != NULL) {
		dnode_ksp->ks_data = &dnode_stats;
		kstat_install(dnode_ksp);
	}
}

void
dnode_fini(void)
{
	if (dnode_ksp != NULL) {
		kstat_delete(dnode_ksp);
		dnode_ksp = NULL;
	}

	kmem_cache_destroy(dnode_cache);
	dnode_cache = NULL;
}


#ifdef ZFS_DEBUG
void
dnode_verify(dnode_t *dn)
{
	int drop_struct_lock = FALSE;

	ASSERT(dn->dn_phys);
	ASSERT(dn->dn_objset);
	ASSERT(dn->dn_handle->dnh_dnode == dn);

	ASSERT(DMU_OT_IS_VALID(dn->dn_phys->dn_type));

	if (!(zfs_flags & ZFS_DEBUG_DNODE_VERIFY))
		return;

	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}
	if (dn->dn_phys->dn_type != DMU_OT_NONE || dn->dn_allocated_txg != 0) {
		int i;
		int max_bonuslen = DN_SLOTS_TO_BONUSLEN(dn->dn_num_slots);
		ASSERT3U(dn->dn_indblkshift, <=, SPA_MAXBLOCKSHIFT);
		if (dn->dn_datablkshift) {
			ASSERT3U(dn->dn_datablkshift, >=, SPA_MINBLOCKSHIFT);
			ASSERT3U(dn->dn_datablkshift, <=, SPA_MAXBLOCKSHIFT);
			ASSERT3U(1<<dn->dn_datablkshift, ==, dn->dn_datablksz);
		}
		ASSERT3U(dn->dn_nlevels, <=, 30);
		ASSERT(DMU_OT_IS_VALID(dn->dn_type));
		ASSERT3U(dn->dn_nblkptr, >=, 1);
		ASSERT3U(dn->dn_nblkptr, <=, DN_MAX_NBLKPTR);
		ASSERT3U(dn->dn_bonuslen, <=, max_bonuslen);
		ASSERT3U(dn->dn_datablksz, ==,
		    dn->dn_datablkszsec << SPA_MINBLOCKSHIFT);
		ASSERT3U(ISP2(dn->dn_datablksz), ==, dn->dn_datablkshift != 0);
		ASSERT3U((dn->dn_nblkptr - 1) * sizeof (blkptr_t) +
		    dn->dn_bonuslen, <=, max_bonuslen);
		for (i = 0; i < TXG_SIZE; i++) {
			ASSERT3U(dn->dn_next_nlevels[i], <=, dn->dn_nlevels);
		}
	}
	if (dn->dn_phys->dn_type != DMU_OT_NONE)
		ASSERT3U(dn->dn_phys->dn_nlevels, <=, dn->dn_nlevels);
	ASSERT(DMU_OBJECT_IS_SPECIAL(dn->dn_object) || dn->dn_dbuf != NULL);
	if (dn->dn_dbuf != NULL) {
		ASSERT3P(dn->dn_phys, ==,
		    (dnode_phys_t *)dn->dn_dbuf->db.db_data +
		    (dn->dn_object % (dn->dn_dbuf->db.db_size >> DNODE_SHIFT)));
	}
	if (drop_struct_lock)
		rw_exit(&dn->dn_struct_rwlock);
}
#endif

void
dnode_byteswap(dnode_phys_t *dnp)
{
	uint64_t *buf64 = (void*)&dnp->dn_blkptr;
	int i;

	if (dnp->dn_type == DMU_OT_NONE) {
		bzero(dnp, sizeof (dnode_phys_t));
		return;
	}

	dnp->dn_datablkszsec = BSWAP_16(dnp->dn_datablkszsec);
	dnp->dn_bonuslen = BSWAP_16(dnp->dn_bonuslen);
	dnp->dn_extra_slots = BSWAP_8(dnp->dn_extra_slots);
	dnp->dn_maxblkid = BSWAP_64(dnp->dn_maxblkid);
	dnp->dn_used = BSWAP_64(dnp->dn_used);

	/*
	 * dn_nblkptr is only one byte, so it's OK to read it in either
	 * byte order.  We can't read dn_bouslen.
	 */
	ASSERT(dnp->dn_indblkshift <= SPA_MAXBLOCKSHIFT);
	ASSERT(dnp->dn_nblkptr <= DN_MAX_NBLKPTR);
	for (i = 0; i < dnp->dn_nblkptr * sizeof (blkptr_t)/8; i++)
		buf64[i] = BSWAP_64(buf64[i]);

	/*
	 * OK to check dn_bonuslen for zero, because it won't matter if
	 * we have the wrong byte order.  This is necessary because the
	 * dnode dnode is smaller than a regular dnode.
	 */
	if (dnp->dn_bonuslen != 0) {
		/*
		 * Note that the bonus length calculated here may be
		 * longer than the actual bonus buffer.  This is because
		 * we always put the bonus buffer after the last block
		 * pointer (instead of packing it against the end of the
		 * dnode buffer).
		 */
		int off = (dnp->dn_nblkptr-1) * sizeof (blkptr_t);
		int slots = dnp->dn_extra_slots + 1;
		size_t len = DN_SLOTS_TO_BONUSLEN(slots) - off;
		dmu_object_byteswap_t byteswap;
		ASSERT(DMU_OT_IS_VALID(dnp->dn_bonustype));
		byteswap = DMU_OT_BYTESWAP(dnp->dn_bonustype);
		dmu_ot_byteswap[byteswap].ob_func(dnp->dn_bonus + off, len);
	}

	/* Swap SPILL block if we have one */
	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR)
		byteswap_uint64_array(DN_SPILL_BLKPTR(dnp), sizeof (blkptr_t));
}

void
dnode_buf_byteswap(void *vbuf, size_t size)
{
	int i = 0;

	ASSERT3U(sizeof (dnode_phys_t), ==, (1<<DNODE_SHIFT));
	ASSERT((size & (sizeof (dnode_phys_t)-1)) == 0);

	while (i < size) {
		dnode_phys_t *dnp = (void *)(((char *)vbuf) + i);
		dnode_byteswap(dnp);

		i += DNODE_MIN_SIZE;
		if (dnp->dn_type != DMU_OT_NONE)
			i += dnp->dn_extra_slots * DNODE_MIN_SIZE;
	}
}

void
dnode_setbonuslen(dnode_t *dn, int newsize, dmu_tx_t *tx)
{
	ASSERT3U(refcount_count(&dn->dn_holds), >=, 1);

	dnode_setdirty(dn, tx);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	ASSERT3U(newsize, <=, DN_SLOTS_TO_BONUSLEN(dn->dn_num_slots) -
	    (dn->dn_nblkptr-1) * sizeof (blkptr_t));
	dn->dn_bonuslen = newsize;
	if (newsize == 0)
		dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = DN_ZERO_BONUSLEN;
	else
		dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = dn->dn_bonuslen;
	rw_exit(&dn->dn_struct_rwlock);
}

void
dnode_setbonus_type(dnode_t *dn, dmu_object_type_t newtype, dmu_tx_t *tx)
{
	ASSERT3U(refcount_count(&dn->dn_holds), >=, 1);
	dnode_setdirty(dn, tx);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dn->dn_bonustype = newtype;
	dn->dn_next_bonustype[tx->tx_txg & TXG_MASK] = dn->dn_bonustype;
	rw_exit(&dn->dn_struct_rwlock);
}

void
dnode_rm_spill(dnode_t *dn, dmu_tx_t *tx)
{
	ASSERT3U(refcount_count(&dn->dn_holds), >=, 1);
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));
	dnode_setdirty(dn, tx);
	dn->dn_rm_spillblk[tx->tx_txg&TXG_MASK] = DN_KILL_SPILLBLK;
	dn->dn_have_spill = B_FALSE;
}

static void
dnode_setdblksz(dnode_t *dn, int size)
{
	ASSERT0(P2PHASE(size, SPA_MINBLOCKSIZE));
	ASSERT3U(size, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(size, >=, SPA_MINBLOCKSIZE);
	ASSERT3U(size >> SPA_MINBLOCKSHIFT, <,
	    1<<(sizeof (dn->dn_phys->dn_datablkszsec) * 8));
	dn->dn_datablksz = size;
	dn->dn_datablkszsec = size >> SPA_MINBLOCKSHIFT;
	dn->dn_datablkshift = ISP2(size) ? highbit64(size - 1) : 0;
}

static dnode_t *
dnode_create(objset_t *os, dnode_phys_t *dnp, dmu_buf_impl_t *db,
    uint64_t object, dnode_handle_t *dnh)
{
	dnode_t *dn;

	dn = kmem_cache_alloc(dnode_cache, KM_SLEEP);
	ASSERT(!POINTER_IS_VALID(dn->dn_objset));
	dn->dn_moved = 0;

	/*
	 * Defer setting dn_objset until the dnode is ready to be a candidate
	 * for the dnode_move() callback.
	 */
	dn->dn_object = object;
	dn->dn_dbuf = db;
	dn->dn_handle = dnh;
	dn->dn_phys = dnp;

	if (dnp->dn_datablkszsec) {
		dnode_setdblksz(dn, dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
	} else {
		dn->dn_datablksz = 0;
		dn->dn_datablkszsec = 0;
		dn->dn_datablkshift = 0;
	}
	dn->dn_indblkshift = dnp->dn_indblkshift;
	dn->dn_nlevels = dnp->dn_nlevels;
	dn->dn_type = dnp->dn_type;
	dn->dn_nblkptr = dnp->dn_nblkptr;
	dn->dn_checksum = dnp->dn_checksum;
	dn->dn_compress = dnp->dn_compress;
	dn->dn_bonustype = dnp->dn_bonustype;
	dn->dn_bonuslen = dnp->dn_bonuslen;
	dn->dn_num_slots = dnp->dn_extra_slots + 1;
	dn->dn_maxblkid = dnp->dn_maxblkid;
	dn->dn_have_spill = ((dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) != 0);
	dn->dn_id_flags = 0;

	dmu_zfetch_init(&dn->dn_zfetch, dn);

	ASSERT(DMU_OT_IS_VALID(dn->dn_phys->dn_type));
	ASSERT(zrl_is_locked(&dnh->dnh_zrlock));
	ASSERT(!DN_SLOT_IS_PTR(dnh->dnh_dnode));

	mutex_enter(&os->os_lock);

	/*
	 * Exclude special dnodes from os_dnodes so an empty os_dnodes
	 * signifies that the special dnodes have no references from
	 * their children (the entries in os_dnodes).  This allows
	 * dnode_destroy() to easily determine if the last child has
	 * been removed and then complete eviction of the objset.
	 */
	if (!DMU_OBJECT_IS_SPECIAL(object))
		list_insert_head(&os->os_dnodes, dn);
	membar_producer();

	/*
	 * Everything else must be valid before assigning dn_objset
	 * makes the dnode eligible for dnode_move().
	 */
	dn->dn_objset = os;

	dnh->dnh_dnode = dn;
	mutex_exit(&os->os_lock);

	arc_space_consume(sizeof (dnode_t), ARC_SPACE_DNODE);

	return (dn);
}

/*
 * Caller must be holding the dnode handle, which is released upon return.
 */
static void
dnode_destroy(dnode_t *dn)
{
	objset_t *os = dn->dn_objset;
	boolean_t complete_os_eviction = B_FALSE;

	ASSERT((dn->dn_id_flags & DN_ID_NEW_EXIST) == 0);

	mutex_enter(&os->os_lock);
	POINTER_INVALIDATE(&dn->dn_objset);
	if (!DMU_OBJECT_IS_SPECIAL(dn->dn_object)) {
		list_remove(&os->os_dnodes, dn);
		complete_os_eviction =
		    list_is_empty(&os->os_dnodes) &&
		    list_link_active(&os->os_evicting_node);
	}
	mutex_exit(&os->os_lock);

	/* the dnode can no longer move, so we can release the handle */
	zrl_remove(&dn->dn_handle->dnh_zrlock);

	dn->dn_allocated_txg = 0;
	dn->dn_free_txg = 0;
	dn->dn_assigned_txg = 0;

	dn->dn_dirtyctx = 0;
	if (dn->dn_dirtyctx_firstset != NULL) {
		kmem_free(dn->dn_dirtyctx_firstset, 1);
		dn->dn_dirtyctx_firstset = NULL;
	}
	if (dn->dn_bonus != NULL) {
		mutex_enter(&dn->dn_bonus->db_mtx);
		dbuf_destroy(dn->dn_bonus);
		dn->dn_bonus = NULL;
	}
	dn->dn_zio = NULL;

	dn->dn_have_spill = B_FALSE;
	dn->dn_oldused = 0;
	dn->dn_oldflags = 0;
	dn->dn_olduid = 0;
	dn->dn_oldgid = 0;
	dn->dn_newuid = 0;
	dn->dn_newgid = 0;
	dn->dn_id_flags = 0;

	dmu_zfetch_fini(&dn->dn_zfetch);
	kmem_cache_free(dnode_cache, dn);
	arc_space_return(sizeof (dnode_t), ARC_SPACE_DNODE);

	if (complete_os_eviction)
		dmu_objset_evict_done(os);
}

void
dnode_allocate(dnode_t *dn, dmu_object_type_t ot, int blocksize, int ibs,
    dmu_object_type_t bonustype, int bonuslen, int dn_slots, dmu_tx_t *tx)
{
	int i;

	ASSERT3U(dn_slots, >, 0);
	ASSERT3U(dn_slots << DNODE_SHIFT, <=,
	    spa_maxdnodesize(dmu_objset_spa(dn->dn_objset)));
	ASSERT3U(blocksize, <=,
	    spa_maxblocksize(dmu_objset_spa(dn->dn_objset)));
	if (blocksize == 0)
		blocksize = 1 << zfs_default_bs;
	else
		blocksize = P2ROUNDUP(blocksize, SPA_MINBLOCKSIZE);

	if (ibs == 0)
		ibs = zfs_default_ibs;

	ibs = MIN(MAX(ibs, DN_MIN_INDBLKSHIFT), DN_MAX_INDBLKSHIFT);

	dprintf("os=%p obj=%llu txg=%llu blocksize=%d ibs=%d dn_slots=%d\n",
	    dn->dn_objset, dn->dn_object, tx->tx_txg, blocksize, ibs, dn_slots);
	DNODE_STAT_BUMP(dnode_allocate);

	ASSERT(dn->dn_type == DMU_OT_NONE);
	ASSERT(bcmp(dn->dn_phys, &dnode_phys_zero, sizeof (dnode_phys_t)) == 0);
	ASSERT(dn->dn_phys->dn_type == DMU_OT_NONE);
	ASSERT(ot != DMU_OT_NONE);
	ASSERT(DMU_OT_IS_VALID(ot));
	ASSERT((bonustype == DMU_OT_NONE && bonuslen == 0) ||
	    (bonustype == DMU_OT_SA && bonuslen == 0) ||
	    (bonustype != DMU_OT_NONE && bonuslen != 0));
	ASSERT(DMU_OT_IS_VALID(bonustype));
	ASSERT3U(bonuslen, <=, DN_SLOTS_TO_BONUSLEN(dn_slots));
	ASSERT(dn->dn_type == DMU_OT_NONE);
	ASSERT0(dn->dn_maxblkid);
	ASSERT0(dn->dn_allocated_txg);
	ASSERT0(dn->dn_assigned_txg);
	ASSERT(refcount_is_zero(&dn->dn_tx_holds));
	ASSERT3U(refcount_count(&dn->dn_holds), <=, 1);
	ASSERT(avl_is_empty(&dn->dn_dbufs));

	for (i = 0; i < TXG_SIZE; i++) {
		ASSERT0(dn->dn_next_nblkptr[i]);
		ASSERT0(dn->dn_next_nlevels[i]);
		ASSERT0(dn->dn_next_indblkshift[i]);
		ASSERT0(dn->dn_next_bonuslen[i]);
		ASSERT0(dn->dn_next_bonustype[i]);
		ASSERT0(dn->dn_rm_spillblk[i]);
		ASSERT0(dn->dn_next_blksz[i]);
		ASSERT(!list_link_active(&dn->dn_dirty_link[i]));
		ASSERT3P(list_head(&dn->dn_dirty_records[i]), ==, NULL);
		ASSERT3P(dn->dn_free_ranges[i], ==, NULL);
	}

	dn->dn_type = ot;
	dnode_setdblksz(dn, blocksize);
	dn->dn_indblkshift = ibs;
	dn->dn_nlevels = 1;
	dn->dn_num_slots = dn_slots;
	if (bonustype == DMU_OT_SA) /* Maximize bonus space for SA */
		dn->dn_nblkptr = 1;
	else {
		dn->dn_nblkptr = MIN(DN_MAX_NBLKPTR,
		    1 + ((DN_SLOTS_TO_BONUSLEN(dn_slots) - bonuslen) >>
		    SPA_BLKPTRSHIFT));
	}

	dn->dn_bonustype = bonustype;
	dn->dn_bonuslen = bonuslen;
	dn->dn_checksum = ZIO_CHECKSUM_INHERIT;
	dn->dn_compress = ZIO_COMPRESS_INHERIT;
	dn->dn_dirtyctx = 0;

	dn->dn_free_txg = 0;
	if (dn->dn_dirtyctx_firstset) {
		kmem_free(dn->dn_dirtyctx_firstset, 1);
		dn->dn_dirtyctx_firstset = NULL;
	}

	dn->dn_allocated_txg = tx->tx_txg;
	dn->dn_id_flags = 0;

	dnode_setdirty(dn, tx);
	dn->dn_next_indblkshift[tx->tx_txg & TXG_MASK] = ibs;
	dn->dn_next_bonuslen[tx->tx_txg & TXG_MASK] = dn->dn_bonuslen;
	dn->dn_next_bonustype[tx->tx_txg & TXG_MASK] = dn->dn_bonustype;
	dn->dn_next_blksz[tx->tx_txg & TXG_MASK] = dn->dn_datablksz;
}

void
dnode_reallocate(dnode_t *dn, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, int dn_slots, dmu_tx_t *tx)
{
	int nblkptr;

	ASSERT3U(blocksize, >=, SPA_MINBLOCKSIZE);
	ASSERT3U(blocksize, <=,
	    spa_maxblocksize(dmu_objset_spa(dn->dn_objset)));
	ASSERT0(blocksize % SPA_MINBLOCKSIZE);
	ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT || dmu_tx_private_ok(tx));
	ASSERT(tx->tx_txg != 0);
	ASSERT((bonustype == DMU_OT_NONE && bonuslen == 0) ||
	    (bonustype != DMU_OT_NONE && bonuslen != 0) ||
	    (bonustype == DMU_OT_SA && bonuslen == 0));
	ASSERT(DMU_OT_IS_VALID(bonustype));
	ASSERT3U(bonuslen, <=,
	    DN_BONUS_SIZE(spa_maxdnodesize(dmu_objset_spa(dn->dn_objset))));

	dn_slots = dn_slots > 0 ? dn_slots : DNODE_MIN_SLOTS;
	DNODE_STAT_BUMP(dnode_reallocate);

	/* clean up any unreferenced dbufs */
	dnode_evict_dbufs(dn);

	dn->dn_id_flags = 0;

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dnode_setdirty(dn, tx);
	if (dn->dn_datablksz != blocksize) {
		/* change blocksize */
		ASSERT(dn->dn_maxblkid == 0 &&
		    (BP_IS_HOLE(&dn->dn_phys->dn_blkptr[0]) ||
		    dnode_block_freed(dn, 0)));
		dnode_setdblksz(dn, blocksize);
		dn->dn_next_blksz[tx->tx_txg&TXG_MASK] = blocksize;
	}
	if (dn->dn_bonuslen != bonuslen)
		dn->dn_next_bonuslen[tx->tx_txg&TXG_MASK] = bonuslen;

	if (bonustype == DMU_OT_SA) /* Maximize bonus space for SA */
		nblkptr = 1;
	else
		nblkptr = MIN(DN_MAX_NBLKPTR,
		    1 + ((DN_SLOTS_TO_BONUSLEN(dn_slots) - bonuslen) >>
		    SPA_BLKPTRSHIFT));
	if (dn->dn_bonustype != bonustype)
		dn->dn_next_bonustype[tx->tx_txg&TXG_MASK] = bonustype;
	if (dn->dn_nblkptr != nblkptr)
		dn->dn_next_nblkptr[tx->tx_txg&TXG_MASK] = nblkptr;
	if (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		dbuf_rm_spill(dn, tx);
		dnode_rm_spill(dn, tx);
	}
	rw_exit(&dn->dn_struct_rwlock);

	/* change type */
	dn->dn_type = ot;

	/* change bonus size and type */
	mutex_enter(&dn->dn_mtx);
	dn->dn_bonustype = bonustype;
	dn->dn_bonuslen = bonuslen;
	dn->dn_num_slots = dn_slots;
	dn->dn_nblkptr = nblkptr;
	dn->dn_checksum = ZIO_CHECKSUM_INHERIT;
	dn->dn_compress = ZIO_COMPRESS_INHERIT;
	ASSERT3U(dn->dn_nblkptr, <=, DN_MAX_NBLKPTR);

	/* fix up the bonus db_size */
	if (dn->dn_bonus) {
		dn->dn_bonus->db.db_size =
		    DN_SLOTS_TO_BONUSLEN(dn->dn_num_slots) -
		    (dn->dn_nblkptr-1) * sizeof (blkptr_t);
		ASSERT(dn->dn_bonuslen <= dn->dn_bonus->db.db_size);
	}

	dn->dn_allocated_txg = tx->tx_txg;
	mutex_exit(&dn->dn_mtx);
}

#ifdef	_KERNEL
static void
dnode_move_impl(dnode_t *odn, dnode_t *ndn)
{
	int i;

	ASSERT(!RW_LOCK_HELD(&odn->dn_struct_rwlock));
	ASSERT(MUTEX_NOT_HELD(&odn->dn_mtx));
	ASSERT(MUTEX_NOT_HELD(&odn->dn_dbufs_mtx));
	ASSERT(!RW_LOCK_HELD(&odn->dn_zfetch.zf_rwlock));

	/* Copy fields. */
	ndn->dn_objset = odn->dn_objset;
	ndn->dn_object = odn->dn_object;
	ndn->dn_dbuf = odn->dn_dbuf;
	ndn->dn_handle = odn->dn_handle;
	ndn->dn_phys = odn->dn_phys;
	ndn->dn_type = odn->dn_type;
	ndn->dn_bonuslen = odn->dn_bonuslen;
	ndn->dn_bonustype = odn->dn_bonustype;
	ndn->dn_nblkptr = odn->dn_nblkptr;
	ndn->dn_checksum = odn->dn_checksum;
	ndn->dn_compress = odn->dn_compress;
	ndn->dn_nlevels = odn->dn_nlevels;
	ndn->dn_indblkshift = odn->dn_indblkshift;
	ndn->dn_datablkshift = odn->dn_datablkshift;
	ndn->dn_datablkszsec = odn->dn_datablkszsec;
	ndn->dn_datablksz = odn->dn_datablksz;
	ndn->dn_maxblkid = odn->dn_maxblkid;
	ndn->dn_num_slots = odn->dn_num_slots;
	bcopy(&odn->dn_next_nblkptr[0], &ndn->dn_next_nblkptr[0],
	    sizeof (odn->dn_next_nblkptr));
	bcopy(&odn->dn_next_nlevels[0], &ndn->dn_next_nlevels[0],
	    sizeof (odn->dn_next_nlevels));
	bcopy(&odn->dn_next_indblkshift[0], &ndn->dn_next_indblkshift[0],
	    sizeof (odn->dn_next_indblkshift));
	bcopy(&odn->dn_next_bonustype[0], &ndn->dn_next_bonustype[0],
	    sizeof (odn->dn_next_bonustype));
	bcopy(&odn->dn_rm_spillblk[0], &ndn->dn_rm_spillblk[0],
	    sizeof (odn->dn_rm_spillblk));
	bcopy(&odn->dn_next_bonuslen[0], &ndn->dn_next_bonuslen[0],
	    sizeof (odn->dn_next_bonuslen));
	bcopy(&odn->dn_next_blksz[0], &ndn->dn_next_blksz[0],
	    sizeof (odn->dn_next_blksz));
	for (i = 0; i < TXG_SIZE; i++) {
		list_move_tail(&ndn->dn_dirty_records[i],
		    &odn->dn_dirty_records[i]);
	}
	bcopy(&odn->dn_free_ranges[0], &ndn->dn_free_ranges[0],
	    sizeof (odn->dn_free_ranges));
	ndn->dn_allocated_txg = odn->dn_allocated_txg;
	ndn->dn_free_txg = odn->dn_free_txg;
	ndn->dn_assigned_txg = odn->dn_assigned_txg;
	ndn->dn_dirtyctx = odn->dn_dirtyctx;
	ndn->dn_dirtyctx_firstset = odn->dn_dirtyctx_firstset;
	ASSERT(refcount_count(&odn->dn_tx_holds) == 0);
	refcount_transfer(&ndn->dn_holds, &odn->dn_holds);
	ASSERT(avl_is_empty(&ndn->dn_dbufs));
	avl_swap(&ndn->dn_dbufs, &odn->dn_dbufs);
	ndn->dn_dbufs_count = odn->dn_dbufs_count;
	ndn->dn_bonus = odn->dn_bonus;
	ndn->dn_have_spill = odn->dn_have_spill;
	ndn->dn_zio = odn->dn_zio;
	ndn->dn_oldused = odn->dn_oldused;
	ndn->dn_oldflags = odn->dn_oldflags;
	ndn->dn_olduid = odn->dn_olduid;
	ndn->dn_oldgid = odn->dn_oldgid;
	ndn->dn_newuid = odn->dn_newuid;
	ndn->dn_newgid = odn->dn_newgid;
	ndn->dn_id_flags = odn->dn_id_flags;
	dmu_zfetch_init(&ndn->dn_zfetch, NULL);
	list_move_tail(&ndn->dn_zfetch.zf_stream, &odn->dn_zfetch.zf_stream);
	ndn->dn_zfetch.zf_dnode = odn->dn_zfetch.zf_dnode;

	/*
	 * Update back pointers. Updating the handle fixes the back pointer of
	 * every descendant dbuf as well as the bonus dbuf.
	 */
	ASSERT(ndn->dn_handle->dnh_dnode == odn);
	ndn->dn_handle->dnh_dnode = ndn;
	if (ndn->dn_zfetch.zf_dnode == odn) {
		ndn->dn_zfetch.zf_dnode = ndn;
	}

	/*
	 * Invalidate the original dnode by clearing all of its back pointers.
	 */
	odn->dn_dbuf = NULL;
	odn->dn_handle = NULL;
	avl_create(&odn->dn_dbufs, dbuf_compare, sizeof (dmu_buf_impl_t),
	    offsetof(dmu_buf_impl_t, db_link));
	odn->dn_dbufs_count = 0;
	odn->dn_bonus = NULL;
	odn->dn_zfetch.zf_dnode = NULL;

	/*
	 * Set the low bit of the objset pointer to ensure that dnode_move()
	 * recognizes the dnode as invalid in any subsequent callback.
	 */
	POINTER_INVALIDATE(&odn->dn_objset);

	/*
	 * Satisfy the destructor.
	 */
	for (i = 0; i < TXG_SIZE; i++) {
		list_create(&odn->dn_dirty_records[i],
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_node));
		odn->dn_free_ranges[i] = NULL;
		odn->dn_next_nlevels[i] = 0;
		odn->dn_next_indblkshift[i] = 0;
		odn->dn_next_bonustype[i] = 0;
		odn->dn_rm_spillblk[i] = 0;
		odn->dn_next_bonuslen[i] = 0;
		odn->dn_next_blksz[i] = 0;
	}
	odn->dn_allocated_txg = 0;
	odn->dn_free_txg = 0;
	odn->dn_assigned_txg = 0;
	odn->dn_dirtyctx = 0;
	odn->dn_dirtyctx_firstset = NULL;
	odn->dn_have_spill = B_FALSE;
	odn->dn_zio = NULL;
	odn->dn_oldused = 0;
	odn->dn_oldflags = 0;
	odn->dn_olduid = 0;
	odn->dn_oldgid = 0;
	odn->dn_newuid = 0;
	odn->dn_newgid = 0;
	odn->dn_id_flags = 0;

	/*
	 * Mark the dnode.
	 */
	ndn->dn_moved = 1;
	odn->dn_moved = (uint8_t)-1;
}

/*ARGSUSED*/
static kmem_cbrc_t
dnode_move(void *buf, void *newbuf, size_t size, void *arg)
{
	dnode_t *odn = buf, *ndn = newbuf;
	objset_t *os;
	int64_t refcount;
	uint32_t dbufs;

	/*
	 * The dnode is on the objset's list of known dnodes if the objset
	 * pointer is valid. We set the low bit of the objset pointer when
	 * freeing the dnode to invalidate it, and the memory patterns written
	 * by kmem (baddcafe and deadbeef) set at least one of the two low bits.
	 * A newly created dnode sets the objset pointer last of all to indicate
	 * that the dnode is known and in a valid state to be moved by this
	 * function.
	 */
	os = odn->dn_objset;
	if (!POINTER_IS_VALID(os)) {
		DNODE_STAT_BUMP(dnode_move_invalid);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * Ensure that the objset does not go away during the move.
	 */
	rw_enter(&os_lock, RW_WRITER);
	if (os != odn->dn_objset) {
		rw_exit(&os_lock);
		DNODE_STAT_BUMP(dnode_move_recheck1);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * If the dnode is still valid, then so is the objset. We know that no
	 * valid objset can be freed while we hold os_lock, so we can safely
	 * ensure that the objset remains in use.
	 */
	mutex_enter(&os->os_lock);

	/*
	 * Recheck the objset pointer in case the dnode was removed just before
	 * acquiring the lock.
	 */
	if (os != odn->dn_objset) {
		mutex_exit(&os->os_lock);
		rw_exit(&os_lock);
		DNODE_STAT_BUMP(dnode_move_recheck2);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * At this point we know that as long as we hold os->os_lock, the dnode
	 * cannot be freed and fields within the dnode can be safely accessed.
	 * The objset listing this dnode cannot go away as long as this dnode is
	 * on its list.
	 */
	rw_exit(&os_lock);
	if (DMU_OBJECT_IS_SPECIAL(odn->dn_object)) {
		mutex_exit(&os->os_lock);
		DNODE_STAT_BUMP(dnode_move_special);
		return (KMEM_CBRC_NO);
	}
	ASSERT(odn->dn_dbuf != NULL); /* only "special" dnodes have no parent */

	/*
	 * Lock the dnode handle to prevent the dnode from obtaining any new
	 * holds. This also prevents the descendant dbufs and the bonus dbuf
	 * from accessing the dnode, so that we can discount their holds. The
	 * handle is safe to access because we know that while the dnode cannot
	 * go away, neither can its handle. Once we hold dnh_zrlock, we can
	 * safely move any dnode referenced only by dbufs.
	 */
	if (!zrl_tryenter(&odn->dn_handle->dnh_zrlock)) {
		mutex_exit(&os->os_lock);
		DNODE_STAT_BUMP(dnode_move_handle);
		return (KMEM_CBRC_LATER);
	}

	/*
	 * Ensure a consistent view of the dnode's holds and the dnode's dbufs.
	 * We need to guarantee that there is a hold for every dbuf in order to
	 * determine whether the dnode is actively referenced. Falsely matching
	 * a dbuf to an active hold would lead to an unsafe move. It's possible
	 * that a thread already having an active dnode hold is about to add a
	 * dbuf, and we can't compare hold and dbuf counts while the add is in
	 * progress.
	 */
	if (!rw_tryenter(&odn->dn_struct_rwlock, RW_WRITER)) {
		zrl_exit(&odn->dn_handle->dnh_zrlock);
		mutex_exit(&os->os_lock);
		DNODE_STAT_BUMP(dnode_move_rwlock);
		return (KMEM_CBRC_LATER);
	}

	/*
	 * A dbuf may be removed (evicted) without an active dnode hold. In that
	 * case, the dbuf count is decremented under the handle lock before the
	 * dbuf's hold is released. This order ensures that if we count the hold
	 * after the dbuf is removed but before its hold is released, we will
	 * treat the unmatched hold as active and exit safely. If we count the
	 * hold before the dbuf is removed, the hold is discounted, and the
	 * removal is blocked until the move completes.
	 */
	refcount = refcount_count(&odn->dn_holds);
	ASSERT(refcount >= 0);
	dbufs = odn->dn_dbufs_count;

	/* We can't have more dbufs than dnode holds. */
	ASSERT3U(dbufs, <=, refcount);
	DTRACE_PROBE3(dnode__move, dnode_t *, odn, int64_t, refcount,
	    uint32_t, dbufs);

	if (refcount > dbufs) {
		rw_exit(&odn->dn_struct_rwlock);
		zrl_exit(&odn->dn_handle->dnh_zrlock);
		mutex_exit(&os->os_lock);
		DNODE_STAT_BUMP(dnode_move_active);
		return (KMEM_CBRC_LATER);
	}

	rw_exit(&odn->dn_struct_rwlock);

	/*
	 * At this point we know that anyone with a hold on the dnode is not
	 * actively referencing it. The dnode is known and in a valid state to
	 * move. We're holding the locks needed to execute the critical section.
	 */
	dnode_move_impl(odn, ndn);

	list_link_replace(&odn->dn_link, &ndn->dn_link);
	/* If the dnode was safe to move, the refcount cannot have changed. */
	ASSERT(refcount == refcount_count(&ndn->dn_holds));
	ASSERT(dbufs == ndn->dn_dbufs_count);
	zrl_exit(&ndn->dn_handle->dnh_zrlock); /* handle has moved */
	mutex_exit(&os->os_lock);

	return (KMEM_CBRC_YES);
}
#endif	/* _KERNEL */

static void
dnode_slots_hold(dnode_children_t *children, int idx, int slots)
{
	ASSERT3S(idx + slots, <=, DNODES_PER_BLOCK);

	for (int i = idx; i < idx + slots; i++) {
		dnode_handle_t *dnh = &children->dnc_children[i];
		zrl_add(&dnh->dnh_zrlock);
	}
}

static void
dnode_slots_rele(dnode_children_t *children, int idx, int slots)
{
	ASSERT3S(idx + slots, <=, DNODES_PER_BLOCK);

	for (int i = idx; i < idx + slots; i++) {
		dnode_handle_t *dnh = &children->dnc_children[i];

		if (zrl_is_locked(&dnh->dnh_zrlock))
			zrl_exit(&dnh->dnh_zrlock);
		else
			zrl_remove(&dnh->dnh_zrlock);
	}
}

static int
dnode_slots_tryenter(dnode_children_t *children, int idx, int slots)
{
	ASSERT3S(idx + slots, <=, DNODES_PER_BLOCK);

	for (int i = idx; i < idx + slots; i++) {
		dnode_handle_t *dnh = &children->dnc_children[i];

		if (!zrl_tryenter(&dnh->dnh_zrlock)) {
			for (int j = idx; j < i; j++) {
				dnh = &children->dnc_children[j];
				zrl_exit(&dnh->dnh_zrlock);
			}

			return (0);
		}
	}

	return (1);
}

static void
dnode_set_slots(dnode_children_t *children, int idx, int slots, void *ptr)
{
	ASSERT3S(idx + slots, <=, DNODES_PER_BLOCK);

	for (int i = idx; i < idx + slots; i++) {
		dnode_handle_t *dnh = &children->dnc_children[i];
		dnh->dnh_dnode = ptr;
	}
}

static boolean_t
dnode_check_slots(dnode_children_t *children, int idx, int slots, void *ptr)
{
	ASSERT3S(idx + slots, <=, DNODES_PER_BLOCK);

	for (int i = idx; i < idx + slots; i++) {
		dnode_handle_t *dnh = &children->dnc_children[i];
		if (dnh->dnh_dnode != ptr)
			return (B_FALSE);
	}

	return (B_TRUE);
}

void
dnode_special_close(dnode_handle_t *dnh)
{
	dnode_t *dn = dnh->dnh_dnode;

	/*
	 * Wait for final references to the dnode to clear.  This can
	 * only happen if the arc is asynchronously evicting state that
	 * has a hold on this dnode while we are trying to evict this
	 * dnode.
	 */
	while (refcount_count(&dn->dn_holds) > 0)
		delay(1);
	ASSERT(dn->dn_dbuf == NULL ||
	    dmu_buf_get_user(&dn->dn_dbuf->db) == NULL);
	zrl_add(&dnh->dnh_zrlock);
	dnode_destroy(dn); /* implicit zrl_remove() */
	zrl_destroy(&dnh->dnh_zrlock);
	dnh->dnh_dnode = NULL;
}

void
dnode_special_open(objset_t *os, dnode_phys_t *dnp, uint64_t object,
    dnode_handle_t *dnh)
{
	dnode_t *dn;

	zrl_init(&dnh->dnh_zrlock);
	zrl_tryenter(&dnh->dnh_zrlock);

	dn = dnode_create(os, dnp, NULL, object, dnh);
	DNODE_VERIFY(dn);

	zrl_exit(&dnh->dnh_zrlock);
}

static void
dnode_buf_evict_async(void *dbu)
{
	dnode_children_t *dnc = dbu;

	DNODE_STAT_BUMP(dnode_buf_evict);

	for (int i = 0; i < dnc->dnc_count; i++) {
		dnode_handle_t *dnh = &dnc->dnc_children[i];
		dnode_t *dn;

		/*
		 * The dnode handle lock guards against the dnode moving to
		 * another valid address, so there is no need here to guard
		 * against changes to or from NULL.
		 */
		if (!DN_SLOT_IS_PTR(dnh->dnh_dnode)) {
			zrl_destroy(&dnh->dnh_zrlock);
			dnh->dnh_dnode = DN_SLOT_UNINIT;
			continue;
		}

		zrl_add(&dnh->dnh_zrlock);
		dn = dnh->dnh_dnode;
		/*
		 * If there are holds on this dnode, then there should
		 * be holds on the dnode's containing dbuf as well; thus
		 * it wouldn't be eligible for eviction and this function
		 * would not have been called.
		 */
		ASSERT(refcount_is_zero(&dn->dn_holds));
		ASSERT(refcount_is_zero(&dn->dn_tx_holds));

		dnode_destroy(dn); /* implicit zrl_remove() for first slot */
		zrl_destroy(&dnh->dnh_zrlock);
		dnh->dnh_dnode = DN_SLOT_UNINIT;
	}
	kmem_free(dnc, sizeof (dnode_children_t) +
	    dnc->dnc_count * sizeof (dnode_handle_t));
}

/*
 * errors:
 * EINVAL - Invalid object number or flags.
 * ENOSPC - Hole too small to fulfill "slots" request (DNODE_MUST_BE_FREE)
 * EEXIST - Refers to an allocated dnode (DNODE_MUST_BE_FREE)
 *        - Refers to an interior dnode slot (DNODE_MUST_BE_ALLOCATED)
 * ENOENT - The requested dnode is not allocated (DNODE_MUST_BE_ALLOCATED)
 * EIO    - I/O error when reading the meta dnode dbuf.
 *
 * succeeds even for free dnodes.
 */
int
dnode_hold_impl(objset_t *os, uint64_t object, int flag, int slots,
    void *tag, dnode_t **dnp)
{
	int epb, idx, err;
	int drop_struct_lock = FALSE;
	int type;
	uint64_t blk;
	dnode_t *mdn, *dn;
	dmu_buf_impl_t *db;
	dnode_children_t *dnc;
	dnode_phys_t *dn_block;
	dnode_handle_t *dnh;

	ASSERT(!(flag & DNODE_MUST_BE_ALLOCATED) || (slots == 0));
	ASSERT(!(flag & DNODE_MUST_BE_FREE) || (slots > 0));

	/*
	 * If you are holding the spa config lock as writer, you shouldn't
	 * be asking the DMU to do *anything* unless it's the root pool
	 * which may require us to read from the root filesystem while
	 * holding some (not all) of the locks as writer.
	 */
	ASSERT(spa_config_held(os->os_spa, SCL_ALL, RW_WRITER) == 0 ||
	    (spa_is_root(os->os_spa) &&
	    spa_config_held(os->os_spa, SCL_STATE, RW_WRITER)));

	if (object == DMU_USERUSED_OBJECT || object == DMU_GROUPUSED_OBJECT) {
		dn = (object == DMU_USERUSED_OBJECT) ?
		    DMU_USERUSED_DNODE(os) : DMU_GROUPUSED_DNODE(os);
		if (dn == NULL)
			return (SET_ERROR(ENOENT));
		type = dn->dn_type;
		if ((flag & DNODE_MUST_BE_ALLOCATED) && type == DMU_OT_NONE)
			return (SET_ERROR(ENOENT));
		if ((flag & DNODE_MUST_BE_FREE) && type != DMU_OT_NONE)
			return (SET_ERROR(EEXIST));
		DNODE_VERIFY(dn);
		(void) refcount_add(&dn->dn_holds, tag);
		*dnp = dn;
		return (0);
	}

	if (object == 0 || object >= DN_MAX_OBJECT)
		return (SET_ERROR(EINVAL));

	mdn = DMU_META_DNODE(os);
	ASSERT(mdn->dn_object == DMU_META_DNODE_OBJECT);

	DNODE_VERIFY(mdn);

	if (!RW_WRITE_HELD(&mdn->dn_struct_rwlock)) {
		rw_enter(&mdn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}

	blk = dbuf_whichblock(mdn, 0, object * sizeof (dnode_phys_t));

	db = dbuf_hold(mdn, blk, FTAG);
	if (drop_struct_lock)
		rw_exit(&mdn->dn_struct_rwlock);
	if (db == NULL) {
		DNODE_STAT_BUMP(dnode_hold_dbuf_hold);
		return (SET_ERROR(EIO));
	}
	err = dbuf_read(db, NULL, DB_RF_CANFAIL);
	if (err) {
		DNODE_STAT_BUMP(dnode_hold_dbuf_read);
		dbuf_rele(db, FTAG);
		return (err);
	}

	ASSERT3U(db->db.db_size, >=, 1<<DNODE_SHIFT);
	epb = db->db.db_size >> DNODE_SHIFT;

	idx = object & (epb - 1);
	dn_block = (dnode_phys_t *)db->db.db_data;

	ASSERT(DB_DNODE(db)->dn_type == DMU_OT_DNODE);
	dnc = dmu_buf_get_user(&db->db);
	dnh = NULL;
	if (dnc == NULL) {
		dnode_children_t *winner;
		int skip = 0;

		dnc = kmem_zalloc(sizeof (dnode_children_t) +
		    epb * sizeof (dnode_handle_t), KM_SLEEP);
		dnc->dnc_count = epb;
		dnh = &dnc->dnc_children[0];

		/* Initialize dnode slot status from dnode_phys_t */
		for (int i = 0; i < epb; i++) {
			zrl_init(&dnh[i].dnh_zrlock);

			if (skip) {
				skip--;
				continue;
			}

			if (dn_block[i].dn_type != DMU_OT_NONE) {
				int interior = dn_block[i].dn_extra_slots;

				dnode_set_slots(dnc, i, 1, DN_SLOT_ALLOCATED);
				dnode_set_slots(dnc, i + 1, interior,
				    DN_SLOT_INTERIOR);
				skip = interior;
			} else {
				dnh[i].dnh_dnode = DN_SLOT_FREE;
				skip = 0;
			}
		}

		dmu_buf_init_user(&dnc->dnc_dbu, NULL,
		    dnode_buf_evict_async, NULL);
		winner = dmu_buf_set_user(&db->db, &dnc->dnc_dbu);
		if (winner != NULL) {

			for (int i = 0; i < epb; i++)
				zrl_destroy(&dnh[i].dnh_zrlock);

			kmem_free(dnc, sizeof (dnode_children_t) +
			    epb * sizeof (dnode_handle_t));
			dnc = winner;
		}
	}

	ASSERT(dnc->dnc_count == epb);
	dn = DN_SLOT_UNINIT;

	if (flag & DNODE_MUST_BE_ALLOCATED) {
		slots = 1;

		while (dn == DN_SLOT_UNINIT) {
			dnode_slots_hold(dnc, idx, slots);
			dnh = &dnc->dnc_children[idx];

			if (DN_SLOT_IS_PTR(dnh->dnh_dnode)) {
				dn = dnh->dnh_dnode;
				break;
			} else if (dnh->dnh_dnode == DN_SLOT_INTERIOR) {
				DNODE_STAT_BUMP(dnode_hold_alloc_interior);
				dnode_slots_rele(dnc, idx, slots);
				dbuf_rele(db, FTAG);
				return (SET_ERROR(EEXIST));
			} else if (dnh->dnh_dnode != DN_SLOT_ALLOCATED) {
				DNODE_STAT_BUMP(dnode_hold_alloc_misses);
				dnode_slots_rele(dnc, idx, slots);
				dbuf_rele(db, FTAG);
				return (SET_ERROR(ENOENT));
			}

			dnode_slots_rele(dnc, idx, slots);
			if (!dnode_slots_tryenter(dnc, idx, slots)) {
				DNODE_STAT_BUMP(dnode_hold_alloc_lock_retry);
				continue;
			}

			/*
			 * Someone else won the race and called dnode_create()
			 * after we checked DN_SLOT_IS_PTR() above but before
			 * we acquired the lock.
			 */
			if (DN_SLOT_IS_PTR(dnh->dnh_dnode)) {
				DNODE_STAT_BUMP(dnode_hold_alloc_lock_misses);
				dn = dnh->dnh_dnode;
			} else {
				dn = dnode_create(os, dn_block + idx, db,
				    object, dnh);
			}
		}

		mutex_enter(&dn->dn_mtx);
		if (dn->dn_type == DMU_OT_NONE) {
			DNODE_STAT_BUMP(dnode_hold_alloc_type_none);
			mutex_exit(&dn->dn_mtx);
			dnode_slots_rele(dnc, idx, slots);
			dbuf_rele(db, FTAG);
			return (SET_ERROR(ENOENT));
		}

		DNODE_STAT_BUMP(dnode_hold_alloc_hits);
	} else if (flag & DNODE_MUST_BE_FREE) {

		if (idx + slots - 1 >= DNODES_PER_BLOCK) {
			DNODE_STAT_BUMP(dnode_hold_free_overflow);
			dbuf_rele(db, FTAG);
			return (SET_ERROR(ENOSPC));
		}

		while (dn == DN_SLOT_UNINIT) {
			dnode_slots_hold(dnc, idx, slots);

			if (!dnode_check_slots(dnc, idx, slots, DN_SLOT_FREE)) {
				DNODE_STAT_BUMP(dnode_hold_free_misses);
				dnode_slots_rele(dnc, idx, slots);
				dbuf_rele(db, FTAG);
				return (SET_ERROR(ENOSPC));
			}

			dnode_slots_rele(dnc, idx, slots);
			if (!dnode_slots_tryenter(dnc, idx, slots)) {
				DNODE_STAT_BUMP(dnode_hold_free_lock_retry);
				continue;
			}

			if (!dnode_check_slots(dnc, idx, slots, DN_SLOT_FREE)) {
				DNODE_STAT_BUMP(dnode_hold_free_lock_misses);
				dnode_slots_rele(dnc, idx, slots);
				dbuf_rele(db, FTAG);
				return (SET_ERROR(ENOSPC));
			}

			dnh = &dnc->dnc_children[idx];
			dn = dnode_create(os, dn_block + idx, db, object, dnh);
		}

		mutex_enter(&dn->dn_mtx);
		if (!refcount_is_zero(&dn->dn_holds)) {
			DNODE_STAT_BUMP(dnode_hold_free_refcount);
			mutex_exit(&dn->dn_mtx);
			dnode_slots_rele(dnc, idx, slots);
			dbuf_rele(db, FTAG);
			return (SET_ERROR(EEXIST));
		}

		dnode_set_slots(dnc, idx + 1, slots - 1, DN_SLOT_INTERIOR);
		DNODE_STAT_BUMP(dnode_hold_free_hits);
	} else {
		dbuf_rele(db, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (dn->dn_free_txg) {
		DNODE_STAT_BUMP(dnode_hold_free_txg);
		type = dn->dn_type;
		mutex_exit(&dn->dn_mtx);
		dnode_slots_rele(dnc, idx, slots);
		dbuf_rele(db, FTAG);
		return (type == DMU_OT_NONE ? ENOENT : EEXIST);
	}

	if (refcount_add(&dn->dn_holds, tag) == 1)
		dbuf_add_ref(db, dnh);

	mutex_exit(&dn->dn_mtx);

	/* Now we can rely on the hold to prevent the dnode from moving. */
	dnode_slots_rele(dnc, idx, slots);

	DNODE_VERIFY(dn);
	ASSERT3P(dn->dn_dbuf, ==, db);
	ASSERT3U(dn->dn_object, ==, object);
	dbuf_rele(db, FTAG);

	*dnp = dn;
	return (0);
}

/*
 * Return held dnode if the object is allocated, NULL if not.
 */
int
dnode_hold(objset_t *os, uint64_t object, void *tag, dnode_t **dnp)
{
	return (dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED, 0, tag,
	    dnp));
}

/*
 * Can only add a reference if there is already at least one
 * reference on the dnode.  Returns FALSE if unable to add a
 * new reference.
 */
boolean_t
dnode_add_ref(dnode_t *dn, void *tag)
{
	mutex_enter(&dn->dn_mtx);
	if (refcount_is_zero(&dn->dn_holds)) {
		mutex_exit(&dn->dn_mtx);
		return (FALSE);
	}
	VERIFY(1 < refcount_add(&dn->dn_holds, tag));
	mutex_exit(&dn->dn_mtx);
	return (TRUE);
}

void
dnode_rele(dnode_t *dn, void *tag)
{
	mutex_enter(&dn->dn_mtx);
	dnode_rele_and_unlock(dn, tag);
}

void
dnode_rele_and_unlock(dnode_t *dn, void *tag)
{
	uint64_t refs;
	/* Get while the hold prevents the dnode from moving. */
	dmu_buf_impl_t *db = dn->dn_dbuf;
	dnode_handle_t *dnh = dn->dn_handle;

	refs = refcount_remove(&dn->dn_holds, tag);
	mutex_exit(&dn->dn_mtx);

	/*
	 * It's unsafe to release the last hold on a dnode by dnode_rele() or
	 * indirectly by dbuf_rele() while relying on the dnode handle to
	 * prevent the dnode from moving, since releasing the last hold could
	 * result in the dnode's parent dbuf evicting its dnode handles. For
	 * that reason anyone calling dnode_rele() or dbuf_rele() without some
	 * other direct or indirect hold on the dnode must first drop the dnode
	 * handle.
	 */
	ASSERT(refs > 0 || dnh->dnh_zrlock.zr_owner != curthread);

	/* NOTE: the DNODE_DNODE does not have a dn_dbuf */
	if (refs == 0 && db != NULL) {
		/*
		 * Another thread could add a hold to the dnode handle in
		 * dnode_hold_impl() while holding the parent dbuf. Since the
		 * hold on the parent dbuf prevents the handle from being
		 * destroyed, the hold on the handle is OK. We can't yet assert
		 * that the handle has zero references, but that will be
		 * asserted anyway when the handle gets destroyed.
		 */
		dbuf_rele(db, dnh);
	}
}

void
dnode_setdirty(dnode_t *dn, dmu_tx_t *tx)
{
	objset_t *os = dn->dn_objset;
	uint64_t txg = tx->tx_txg;

	if (DMU_OBJECT_IS_SPECIAL(dn->dn_object)) {
		dsl_dataset_dirty(os->os_dsl_dataset, tx);
		return;
	}

	DNODE_VERIFY(dn);

#ifdef ZFS_DEBUG
	mutex_enter(&dn->dn_mtx);
	ASSERT(dn->dn_phys->dn_type || dn->dn_allocated_txg);
	ASSERT(dn->dn_free_txg == 0 || dn->dn_free_txg >= txg);
	mutex_exit(&dn->dn_mtx);
#endif

	/*
	 * Determine old uid/gid when necessary
	 */
	dmu_objset_userquota_get_ids(dn, B_TRUE, tx);

	multilist_t *dirtylist = os->os_dirty_dnodes[txg & TXG_MASK];
	multilist_sublist_t *mls = multilist_sublist_lock_obj(dirtylist, dn);

	/*
	 * If we are already marked dirty, we're done.
	 */
	if (list_link_active(&dn->dn_dirty_link[txg & TXG_MASK])) {
		multilist_sublist_unlock(mls);
		return;
	}

	ASSERT(!refcount_is_zero(&dn->dn_holds) ||
	    !avl_is_empty(&dn->dn_dbufs));
	ASSERT(dn->dn_datablksz != 0);
	ASSERT0(dn->dn_next_bonuslen[txg&TXG_MASK]);
	ASSERT0(dn->dn_next_blksz[txg&TXG_MASK]);
	ASSERT0(dn->dn_next_bonustype[txg&TXG_MASK]);

	dprintf_ds(os->os_dsl_dataset, "obj=%llu txg=%llu\n",
	    dn->dn_object, txg);

	multilist_sublist_insert_head(mls, dn);

	multilist_sublist_unlock(mls);

	/*
	 * The dnode maintains a hold on its containing dbuf as
	 * long as there are holds on it.  Each instantiated child
	 * dbuf maintains a hold on the dnode.  When the last child
	 * drops its hold, the dnode will drop its hold on the
	 * containing dbuf. We add a "dirty hold" here so that the
	 * dnode will hang around after we finish processing its
	 * children.
	 */
	VERIFY(dnode_add_ref(dn, (void *)(uintptr_t)tx->tx_txg));

	(void) dbuf_dirty(dn->dn_dbuf, tx);

	dsl_dataset_dirty(os->os_dsl_dataset, tx);
}

void
dnode_free(dnode_t *dn, dmu_tx_t *tx)
{
	mutex_enter(&dn->dn_mtx);
	if (dn->dn_type == DMU_OT_NONE || dn->dn_free_txg) {
		mutex_exit(&dn->dn_mtx);
		return;
	}
	dn->dn_free_txg = tx->tx_txg;
	mutex_exit(&dn->dn_mtx);

	dnode_setdirty(dn, tx);
}

/*
 * Try to change the block size for the indicated dnode.  This can only
 * succeed if there are no blocks allocated or dirty beyond first block
 */
int
dnode_set_blksz(dnode_t *dn, uint64_t size, int ibs, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db;
	int err;

	ASSERT3U(size, <=, spa_maxblocksize(dmu_objset_spa(dn->dn_objset)));
	if (size == 0)
		size = SPA_MINBLOCKSIZE;
	else
		size = P2ROUNDUP(size, SPA_MINBLOCKSIZE);

	if (ibs == dn->dn_indblkshift)
		ibs = 0;

	if (size >> SPA_MINBLOCKSHIFT == dn->dn_datablkszsec && ibs == 0)
		return (0);

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);

	/* Check for any allocated blocks beyond the first */
	if (dn->dn_maxblkid != 0)
		goto fail;

	mutex_enter(&dn->dn_dbufs_mtx);
	for (db = avl_first(&dn->dn_dbufs); db != NULL;
	    db = AVL_NEXT(&dn->dn_dbufs, db)) {
		if (db->db_blkid != 0 && db->db_blkid != DMU_BONUS_BLKID &&
		    db->db_blkid != DMU_SPILL_BLKID) {
			mutex_exit(&dn->dn_dbufs_mtx);
			goto fail;
		}
	}
	mutex_exit(&dn->dn_dbufs_mtx);

	if (ibs && dn->dn_nlevels != 1)
		goto fail;

	/* resize the old block */
	err = dbuf_hold_impl(dn, 0, 0, TRUE, FALSE, FTAG, &db);
	if (err == 0)
		dbuf_new_size(db, size, tx);
	else if (err != ENOENT)
		goto fail;

	dnode_setdblksz(dn, size);
	dnode_setdirty(dn, tx);
	dn->dn_next_blksz[tx->tx_txg&TXG_MASK] = size;
	if (ibs) {
		dn->dn_indblkshift = ibs;
		dn->dn_next_indblkshift[tx->tx_txg&TXG_MASK] = ibs;
	}
	/* rele after we have fixed the blocksize in the dnode */
	if (db)
		dbuf_rele(db, FTAG);

	rw_exit(&dn->dn_struct_rwlock);
	return (0);

fail:
	rw_exit(&dn->dn_struct_rwlock);
	return (SET_ERROR(ENOTSUP));
}

/* read-holding callers must not rely on the lock being continuously held */
void
dnode_new_blkid(dnode_t *dn, uint64_t blkid, dmu_tx_t *tx, boolean_t have_read)
{
	uint64_t txgoff = tx->tx_txg & TXG_MASK;
	int epbs, new_nlevels;
	uint64_t sz;

	ASSERT(blkid != DMU_BONUS_BLKID);

	ASSERT(have_read ?
	    RW_READ_HELD(&dn->dn_struct_rwlock) :
	    RW_WRITE_HELD(&dn->dn_struct_rwlock));

	/*
	 * if we have a read-lock, check to see if we need to do any work
	 * before upgrading to a write-lock.
	 */
	if (have_read) {
		if (blkid <= dn->dn_maxblkid)
			return;

		if (!rw_tryupgrade(&dn->dn_struct_rwlock)) {
			rw_exit(&dn->dn_struct_rwlock);
			rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
		}
	}

	if (blkid <= dn->dn_maxblkid)
		goto out;

	dn->dn_maxblkid = blkid;

	/*
	 * Compute the number of levels necessary to support the new maxblkid.
	 */
	new_nlevels = 1;
	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
	for (sz = dn->dn_nblkptr;
	    sz <= blkid && sz >= dn->dn_nblkptr; sz <<= epbs)
		new_nlevels++;

	ASSERT3U(new_nlevels, <=, DN_MAX_LEVELS);

	if (new_nlevels > dn->dn_nlevels) {
		int old_nlevels = dn->dn_nlevels;
		dmu_buf_impl_t *db;
		list_t *list;
		dbuf_dirty_record_t *new, *dr, *dr_next;

		dn->dn_nlevels = new_nlevels;

		ASSERT3U(new_nlevels, >, dn->dn_next_nlevels[txgoff]);
		dn->dn_next_nlevels[txgoff] = new_nlevels;

		/* dirty the left indirects */
		db = dbuf_hold_level(dn, old_nlevels, 0, FTAG);
		ASSERT(db != NULL);
		new = dbuf_dirty(db, tx);
		dbuf_rele(db, FTAG);

		/* transfer the dirty records to the new indirect */
		mutex_enter(&dn->dn_mtx);
		mutex_enter(&new->dt.di.dr_mtx);
		list = &dn->dn_dirty_records[txgoff];
		for (dr = list_head(list); dr; dr = dr_next) {
			dr_next = list_next(&dn->dn_dirty_records[txgoff], dr);
			if (dr->dr_dbuf->db_level != new_nlevels-1 &&
			    dr->dr_dbuf->db_blkid != DMU_BONUS_BLKID &&
			    dr->dr_dbuf->db_blkid != DMU_SPILL_BLKID) {
				ASSERT(dr->dr_dbuf->db_level == old_nlevels-1);
				list_remove(&dn->dn_dirty_records[txgoff], dr);
				list_insert_tail(&new->dt.di.dr_children, dr);
				dr->dr_parent = new;
			}
		}
		mutex_exit(&new->dt.di.dr_mtx);
		mutex_exit(&dn->dn_mtx);
	}

out:
	if (have_read)
		rw_downgrade(&dn->dn_struct_rwlock);
}

static void
dnode_dirty_l1(dnode_t *dn, uint64_t l1blkid, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dbuf_hold_level(dn, 1, l1blkid, FTAG);
	if (db != NULL) {
		dmu_buf_will_dirty(&db->db, tx);
		dbuf_rele(db, FTAG);
	}
}

void
dnode_free_range(dnode_t *dn, uint64_t off, uint64_t len, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db;
	uint64_t blkoff, blkid, nblks;
	int blksz, blkshift, head, tail;
	int trunc = FALSE;
	int epbs;

	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	blksz = dn->dn_datablksz;
	blkshift = dn->dn_datablkshift;
	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

	if (len == DMU_OBJECT_END) {
		len = UINT64_MAX - off;
		trunc = TRUE;
	}

	/*
	 * First, block align the region to free:
	 */
	if (ISP2(blksz)) {
		head = P2NPHASE(off, blksz);
		blkoff = P2PHASE(off, blksz);
		if ((off >> blkshift) > dn->dn_maxblkid)
			goto out;
	} else {
		ASSERT(dn->dn_maxblkid == 0);
		if (off == 0 && len >= blksz) {
			/*
			 * Freeing the whole block; fast-track this request.
			 * Note that we won't dirty any indirect blocks,
			 * which is fine because we will be freeing the entire
			 * file and thus all indirect blocks will be freed
			 * by free_children().
			 */
			blkid = 0;
			nblks = 1;
			goto done;
		} else if (off >= blksz) {
			/* Freeing past end-of-data */
			goto out;
		} else {
			/* Freeing part of the block. */
			head = blksz - off;
			ASSERT3U(head, >, 0);
		}
		blkoff = off;
	}
	/* zero out any partial block data at the start of the range */
	if (head) {
		ASSERT3U(blkoff + head, ==, blksz);
		if (len < head)
			head = len;
		if (dbuf_hold_impl(dn, 0, dbuf_whichblock(dn, 0, off),
		    TRUE, FALSE, FTAG, &db) == 0) {
			caddr_t data;

			/* don't dirty if it isn't on disk and isn't dirty */
			if (db->db_last_dirty ||
			    (db->db_blkptr && !BP_IS_HOLE(db->db_blkptr))) {
				rw_exit(&dn->dn_struct_rwlock);
				dmu_buf_will_dirty(&db->db, tx);
				rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
				data = db->db.db_data;
				bzero(data + blkoff, head);
			}
			dbuf_rele(db, FTAG);
		}
		off += head;
		len -= head;
	}

	/* If the range was less than one block, we're done */
	if (len == 0)
		goto out;

	/* If the remaining range is past end of file, we're done */
	if ((off >> blkshift) > dn->dn_maxblkid)
		goto out;

	ASSERT(ISP2(blksz));
	if (trunc)
		tail = 0;
	else
		tail = P2PHASE(len, blksz);

	ASSERT0(P2PHASE(off, blksz));
	/* zero out any partial block data at the end of the range */
	if (tail) {
		if (len < tail)
			tail = len;
		if (dbuf_hold_impl(dn, 0, dbuf_whichblock(dn, 0, off+len),
		    TRUE, FALSE, FTAG, &db) == 0) {
			/* don't dirty if not on disk and not dirty */
			if (db->db_last_dirty ||
			    (db->db_blkptr && !BP_IS_HOLE(db->db_blkptr))) {
				rw_exit(&dn->dn_struct_rwlock);
				dmu_buf_will_dirty(&db->db, tx);
				rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
				bzero(db->db.db_data, tail);
			}
			dbuf_rele(db, FTAG);
		}
		len -= tail;
	}

	/* If the range did not include a full block, we are done */
	if (len == 0)
		goto out;

	ASSERT(IS_P2ALIGNED(off, blksz));
	ASSERT(trunc || IS_P2ALIGNED(len, blksz));
	blkid = off >> blkshift;
	nblks = len >> blkshift;
	if (trunc)
		nblks += 1;

	/*
	 * Dirty all the indirect blocks in this range.  Note that only
	 * the first and last indirect blocks can actually be written
	 * (if they were partially freed) -- they must be dirtied, even if
	 * they do not exist on disk yet.  The interior blocks will
	 * be freed by free_children(), so they will not actually be written.
	 * Even though these interior blocks will not be written, we
	 * dirty them for two reasons:
	 *
	 *  - It ensures that the indirect blocks remain in memory until
	 *    syncing context.  (They have already been prefetched by
	 *    dmu_tx_hold_free(), so we don't have to worry about reading
	 *    them serially here.)
	 *
	 *  - The dirty space accounting will put pressure on the txg sync
	 *    mechanism to begin syncing, and to delay transactions if there
	 *    is a large amount of freeing.  Even though these indirect
	 *    blocks will not be written, we could need to write the same
	 *    amount of space if we copy the freed BPs into deadlists.
	 */
	if (dn->dn_nlevels > 1) {
		uint64_t first, last, i, ibyte;
		int shift, err;

		first = blkid >> epbs;
		dnode_dirty_l1(dn, first, tx);
		if (trunc)
			last = dn->dn_maxblkid >> epbs;
		else
			last = (blkid + nblks - 1) >> epbs;
		if (last != first)
			dnode_dirty_l1(dn, last, tx);

		shift = dn->dn_datablkshift + dn->dn_indblkshift -
		    SPA_BLKPTRSHIFT;
		for (i = first + 1; i < last; i++) {
			/*
			 * Set i to the blockid of the next non-hole
			 * level-1 indirect block at or after i.  Note
			 * that dnode_next_offset() operates in terms of
			 * level-0-equivalent bytes.
			 */
			ibyte = i << shift;
			err = dnode_next_offset(dn, DNODE_FIND_HAVELOCK,
			    &ibyte, 2, 1, 0);
			i = ibyte >> shift;
			if (i >= last)
				break;

			/*
			 * Normally we should not see an error, either
			 * from dnode_next_offset() or dbuf_hold_level()
			 * (except for ESRCH from dnode_next_offset).
			 * If there is an i/o error, then when we read
			 * this block in syncing context, it will use
			 * ZIO_FLAG_MUSTSUCCEED, and thus hang/panic according
			 * to the "failmode" property.  dnode_next_offset()
			 * doesn't have a flag to indicate MUSTSUCCEED.
			 */
			if (err != 0)
				break;

			dnode_dirty_l1(dn, i, tx);
		}
	}

done:
	/*
	 * Add this range to the dnode range list.
	 * We will finish up this free operation in the syncing phase.
	 */
	mutex_enter(&dn->dn_mtx);
	{
	int txgoff = tx->tx_txg & TXG_MASK;
	if (dn->dn_free_ranges[txgoff] == NULL) {
		dn->dn_free_ranges[txgoff] =
		    range_tree_create(NULL, NULL, &dn->dn_mtx);
	}
	range_tree_clear(dn->dn_free_ranges[txgoff], blkid, nblks);
	range_tree_add(dn->dn_free_ranges[txgoff], blkid, nblks);
	}
	dprintf_dnode(dn, "blkid=%llu nblks=%llu txg=%llu\n",
	    blkid, nblks, tx->tx_txg);
	mutex_exit(&dn->dn_mtx);

	dbuf_free_range(dn, blkid, blkid + nblks - 1, tx);
	dnode_setdirty(dn, tx);
out:

	rw_exit(&dn->dn_struct_rwlock);
}

static boolean_t
dnode_spill_freed(dnode_t *dn)
{
	int i;

	mutex_enter(&dn->dn_mtx);
	for (i = 0; i < TXG_SIZE; i++) {
		if (dn->dn_rm_spillblk[i] == DN_KILL_SPILLBLK)
			break;
	}
	mutex_exit(&dn->dn_mtx);
	return (i < TXG_SIZE);
}

/* return TRUE if this blkid was freed in a recent txg, or FALSE if it wasn't */
uint64_t
dnode_block_freed(dnode_t *dn, uint64_t blkid)
{
	void *dp = spa_get_dsl(dn->dn_objset->os_spa);
	int i;

	if (blkid == DMU_BONUS_BLKID)
		return (FALSE);

	/*
	 * If we're in the process of opening the pool, dp will not be
	 * set yet, but there shouldn't be anything dirty.
	 */
	if (dp == NULL)
		return (FALSE);

	if (dn->dn_free_txg)
		return (TRUE);

	if (blkid == DMU_SPILL_BLKID)
		return (dnode_spill_freed(dn));

	mutex_enter(&dn->dn_mtx);
	for (i = 0; i < TXG_SIZE; i++) {
		if (dn->dn_free_ranges[i] != NULL &&
		    range_tree_contains(dn->dn_free_ranges[i], blkid, 1))
			break;
	}
	mutex_exit(&dn->dn_mtx);
	return (i < TXG_SIZE);
}

/* call from syncing context when we actually write/free space for this dnode */
void
dnode_diduse_space(dnode_t *dn, int64_t delta)
{
	uint64_t space;
	dprintf_dnode(dn, "dn=%p dnp=%p used=%llu delta=%lld\n",
	    dn, dn->dn_phys,
	    (u_longlong_t)dn->dn_phys->dn_used,
	    (longlong_t)delta);

	mutex_enter(&dn->dn_mtx);
	space = DN_USED_BYTES(dn->dn_phys);
	if (delta > 0) {
		ASSERT3U(space + delta, >=, space); /* no overflow */
	} else {
		ASSERT3U(space, >=, -delta); /* no underflow */
	}
	space += delta;
	if (spa_version(dn->dn_objset->os_spa) < SPA_VERSION_DNODE_BYTES) {
		ASSERT((dn->dn_phys->dn_flags & DNODE_FLAG_USED_BYTES) == 0);
		ASSERT0(P2PHASE(space, 1<<DEV_BSHIFT));
		dn->dn_phys->dn_used = space >> DEV_BSHIFT;
	} else {
		dn->dn_phys->dn_used = space;
		dn->dn_phys->dn_flags |= DNODE_FLAG_USED_BYTES;
	}
	mutex_exit(&dn->dn_mtx);
}

/*
 * Scans a block at the indicated "level" looking for a hole or data,
 * depending on 'flags'.
 *
 * If level > 0, then we are scanning an indirect block looking at its
 * pointers.  If level == 0, then we are looking at a block of dnodes.
 *
 * If we don't find what we are looking for in the block, we return ESRCH.
 * Otherwise, return with *offset pointing to the beginning (if searching
 * forwards) or end (if searching backwards) of the range covered by the
 * block pointer we matched on (or dnode).
 *
 * The basic search algorithm used below by dnode_next_offset() is to
 * use this function to search up the block tree (widen the search) until
 * we find something (i.e., we don't return ESRCH) and then search back
 * down the tree (narrow the search) until we reach our original search
 * level.
 */
static int
dnode_next_offset_level(dnode_t *dn, int flags, uint64_t *offset,
    int lvl, uint64_t blkfill, uint64_t txg)
{
	dmu_buf_impl_t *db = NULL;
	void *data = NULL;
	uint64_t epbs = dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;
	uint64_t epb = 1ULL << epbs;
	uint64_t minfill, maxfill;
	boolean_t hole;
	int i, inc, error, span;

	hole = ((flags & DNODE_FIND_HOLE) != 0);
	inc = (flags & DNODE_FIND_BACKWARDS) ? -1 : 1;
	ASSERT(txg == 0 || !hole);

	if (lvl == dn->dn_phys->dn_nlevels) {
		error = 0;
		epb = dn->dn_phys->dn_nblkptr;
		data = dn->dn_phys->dn_blkptr;
	} else {
		uint64_t blkid = dbuf_whichblock(dn, lvl, *offset);
		error = dbuf_hold_impl(dn, lvl, blkid, TRUE, FALSE, FTAG, &db);
		if (error) {
			if (error != ENOENT)
				return (error);
			if (hole)
				return (0);
			/*
			 * This can only happen when we are searching up
			 * the block tree for data.  We don't really need to
			 * adjust the offset, as we will just end up looking
			 * at the pointer to this block in its parent, and its
			 * going to be unallocated, so we will skip over it.
			 */
			return (SET_ERROR(ESRCH));
		}
		error = dbuf_read(db, NULL, DB_RF_CANFAIL | DB_RF_HAVESTRUCT);
		if (error) {
			dbuf_rele(db, FTAG);
			return (error);
		}
		data = db->db.db_data;
	}


	if (db != NULL && txg != 0 && (db->db_blkptr == NULL ||
	    db->db_blkptr->blk_birth <= txg ||
	    BP_IS_HOLE(db->db_blkptr))) {
		/*
		 * This can only happen when we are searching up the tree
		 * and these conditions mean that we need to keep climbing.
		 */
		error = SET_ERROR(ESRCH);
	} else if (lvl == 0) {
		dnode_phys_t *dnp = data;

		ASSERT(dn->dn_type == DMU_OT_DNODE);
		ASSERT(!(flags & DNODE_FIND_BACKWARDS));

		for (i = (*offset >> DNODE_SHIFT) & (blkfill - 1);
		    i < blkfill; i += dnp[i].dn_extra_slots + 1) {
			if ((dnp[i].dn_type == DMU_OT_NONE) == hole)
				break;
		}

		if (i == blkfill)
			error = SET_ERROR(ESRCH);

		*offset = (*offset & ~(DNODE_BLOCK_SIZE - 1)) +
		    (i << DNODE_SHIFT);
	} else {
		blkptr_t *bp = data;
		uint64_t start = *offset;
		span = (lvl - 1) * epbs + dn->dn_datablkshift;
		minfill = 0;
		maxfill = blkfill << ((lvl - 1) * epbs);

		if (hole)
			maxfill--;
		else
			minfill++;

		if (span >= 8 * sizeof (*offset)) {
			/* This only happens on the highest indirection level */
			ASSERT3U((lvl - 1), ==, dn->dn_phys->dn_nlevels - 1);
			*offset = 0;
		} else {
			*offset = *offset >> span;
		}

		for (i = BF64_GET(*offset, 0, epbs);
		    i >= 0 && i < epb; i += inc) {
			if (BP_GET_FILL(&bp[i]) >= minfill &&
			    BP_GET_FILL(&bp[i]) <= maxfill &&
			    (hole || bp[i].blk_birth > txg))
				break;
			if (inc > 0 || *offset > 0)
				*offset += inc;
		}

		if (span >= 8 * sizeof (*offset)) {
			*offset = start;
		} else {
			*offset = *offset << span;
		}

		if (inc < 0) {
			/* traversing backwards; position offset at the end */
			ASSERT3U(*offset, <=, start);
			*offset = MIN(*offset + (1ULL << span) - 1, start);
		} else if (*offset < start) {
			*offset = start;
		}
		if (i < 0 || i >= epb)
			error = SET_ERROR(ESRCH);
	}

	if (db)
		dbuf_rele(db, FTAG);

	return (error);
}

/*
 * Find the next hole, data, or sparse region at or after *offset.
 * The value 'blkfill' tells us how many items we expect to find
 * in an L0 data block; this value is 1 for normal objects,
 * DNODES_PER_BLOCK for the meta dnode, and some fraction of
 * DNODES_PER_BLOCK when searching for sparse regions thereof.
 *
 * Examples:
 *
 * dnode_next_offset(dn, flags, offset, 1, 1, 0);
 *	Finds the next/previous hole/data in a file.
 *	Used in dmu_offset_next().
 *
 * dnode_next_offset(mdn, flags, offset, 0, DNODES_PER_BLOCK, txg);
 *	Finds the next free/allocated dnode an objset's meta-dnode.
 *	Only finds objects that have new contents since txg (ie.
 *	bonus buffer changes and content removal are ignored).
 *	Used in dmu_object_next().
 *
 * dnode_next_offset(mdn, DNODE_FIND_HOLE, offset, 2, DNODES_PER_BLOCK >> 2, 0);
 *	Finds the next L2 meta-dnode bp that's at most 1/4 full.
 *	Used in dmu_object_alloc().
 */
int
dnode_next_offset(dnode_t *dn, int flags, uint64_t *offset,
    int minlvl, uint64_t blkfill, uint64_t txg)
{
	uint64_t initial_offset = *offset;
	int lvl, maxlvl;
	int error = 0;

	if (!(flags & DNODE_FIND_HAVELOCK))
		rw_enter(&dn->dn_struct_rwlock, RW_READER);

	if (dn->dn_phys->dn_nlevels == 0) {
		error = SET_ERROR(ESRCH);
		goto out;
	}

	if (dn->dn_datablkshift == 0) {
		if (*offset < dn->dn_datablksz) {
			if (flags & DNODE_FIND_HOLE)
				*offset = dn->dn_datablksz;
		} else {
			error = SET_ERROR(ESRCH);
		}
		goto out;
	}

	maxlvl = dn->dn_phys->dn_nlevels;

	for (lvl = minlvl; lvl <= maxlvl; lvl++) {
		error = dnode_next_offset_level(dn,
		    flags, offset, lvl, blkfill, txg);
		if (error != ESRCH)
			break;
	}

	while (error == 0 && --lvl >= minlvl) {
		error = dnode_next_offset_level(dn,
		    flags, offset, lvl, blkfill, txg);
	}

	/*
	 * There's always a "virtual hole" at the end of the object, even
	 * if all BP's which physically exist are non-holes.
	 */
	if ((flags & DNODE_FIND_HOLE) && error == ESRCH && txg == 0 &&
	    minlvl == 1 && blkfill == 1 && !(flags & DNODE_FIND_BACKWARDS)) {
		error = 0;
	}

	if (error == 0 && (flags & DNODE_FIND_BACKWARDS ?
	    initial_offset < *offset : initial_offset > *offset))
		error = SET_ERROR(ESRCH);
out:
	if (!(flags & DNODE_FIND_HAVELOCK))
		rw_exit(&dn->dn_struct_rwlock);

	return (error);
}
