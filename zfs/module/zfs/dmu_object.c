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
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 * Copyright 2014 HybridCluster. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/dsl_dataset.h>

/*
 * Each of the concurrent object allocators will grab
 * 2^dmu_object_alloc_chunk_shift dnode slots at a time.  The default is to
 * grab 128 slots, which is 4 blocks worth.  This was experimentally
 * determined to be the lowest value that eliminates the measurable effect
 * of lock contention from this code path.
 */
int dmu_object_alloc_chunk_shift = 7;

uint64_t
dmu_object_alloc(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return dmu_object_alloc_dnsize(os, ot, blocksize, bonustype, bonuslen,
	    0, tx);
}

uint64_t
dmu_object_alloc_dnsize(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	uint64_t object;
	uint64_t L1_dnode_count = DNODES_PER_BLOCK <<
	    (DMU_META_DNODE(os)->dn_indblkshift - SPA_BLKPTRSHIFT);
	dnode_t *dn = NULL;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	boolean_t restarted = B_FALSE;
	uint64_t *cpuobj = NULL;
	int dnodes_per_chunk = 1 << dmu_object_alloc_chunk_shift;
	int error;

	kpreempt_disable();
	cpuobj = &os->os_obj_next_percpu[CPU_SEQID %
	    os->os_obj_next_percpu_len];
	kpreempt_enable();

	if (dn_slots == 0) {
		dn_slots = DNODE_MIN_SLOTS;
	} else {
		ASSERT3S(dn_slots, >=, DNODE_MIN_SLOTS);
		ASSERT3S(dn_slots, <=, DNODE_MAX_SLOTS);
	}

	/*
	 * The "chunk" of dnodes that is assigned to a CPU-specific
	 * allocator needs to be at least one block's worth, to avoid
	 * lock contention on the dbuf.  It can be at most one L1 block's
	 * worth, so that the "rescan after polishing off a L1's worth"
	 * logic below will be sure to kick in.
	 */
	if (dnodes_per_chunk < DNODES_PER_BLOCK)
		dnodes_per_chunk = DNODES_PER_BLOCK;
	if (dnodes_per_chunk > L1_dnode_count)
		dnodes_per_chunk = L1_dnode_count;

	object = *cpuobj;
	for (;;) {
		/*
		 * If we finished a chunk of dnodes, get a new one from
		 * the global allocator.
		 */
		if ((P2PHASE(object, dnodes_per_chunk) == 0) ||
		    (P2PHASE(object + dn_slots - 1, dnodes_per_chunk) <
		    dn_slots)) {
			DNODE_STAT_BUMP(dnode_alloc_next_chunk);
			mutex_enter(&os->os_obj_lock);
			ASSERT0(P2PHASE(os->os_obj_next_chunk,
			    dnodes_per_chunk));
			object = os->os_obj_next_chunk;

			/*
			 * Each time we polish off a L1 bp worth of dnodes
			 * (2^12 objects), move to another L1 bp that's
			 * still reasonably sparse (at most 1/4 full). Look
			 * from the beginning at most once per txg. If we
			 * still can't allocate from that L1 block, search
			 * for an empty L0 block, which will quickly skip
			 * to the end of the metadnode if no nearby L0
			 * blocks are empty. This fallback avoids a
			 * pathology where full dnode blocks containing
			 * large dnodes appear sparse because they have a
			 * low blk_fill, leading to many failed allocation
			 * attempts. In the long term a better mechanism to
			 * search for sparse metadnode regions, such as
			 * spacemaps, could be implemented.
			 *
			 * os_scan_dnodes is set during txg sync if enough
			 * objects have been freed since the previous
			 * rescan to justify backfilling again.
			 *
			 * Note that dmu_traverse depends on the behavior
			 * that we use multiple blocks of the dnode object
			 * before going back to reuse objects.  Any change
			 * to this algorithm should preserve that property
			 * or find another solution to the issues described
			 * in traverse_visitbp.
			 */
			if (P2PHASE(object, L1_dnode_count) == 0) {
				uint64_t offset;
				uint64_t blkfill;
				int minlvl;
				if (os->os_rescan_dnodes) {
					offset = 0;
					os->os_rescan_dnodes = B_FALSE;
				} else {
					offset = object << DNODE_SHIFT;
				}
				blkfill = restarted ? 1 : DNODES_PER_BLOCK >> 2;
				minlvl = restarted ? 1 : 2;
				restarted = B_TRUE;
				error = dnode_next_offset(DMU_META_DNODE(os),
				    DNODE_FIND_HOLE, &offset, minlvl,
				    blkfill, 0);
				if (error == 0) {
					object = offset >> DNODE_SHIFT;
				}
			}
			/*
			 * Note: if "restarted", we may find a L0 that
			 * is not suitably aligned.
			 */
			os->os_obj_next_chunk =
			    P2ALIGN(object, dnodes_per_chunk) +
			    dnodes_per_chunk;
			(void) atomic_swap_64(cpuobj, object);
			mutex_exit(&os->os_obj_lock);
		}

		/*
		 * The value of (*cpuobj) before adding dn_slots is the object
		 * ID assigned to us.  The value afterwards is the object ID
		 * assigned to whoever wants to do an allocation next.
		 */
		object = atomic_add_64_nv(cpuobj, dn_slots) - dn_slots;

		/*
		 * XXX We should check for an i/o error here and return
		 * up to our caller.  Actually we should pre-read it in
		 * dmu_tx_assign(), but there is currently no mechanism
		 * to do so.
		 */
		error = dnode_hold_impl(os, object, DNODE_MUST_BE_FREE,
		    dn_slots, FTAG, &dn);
		if (error == 0) {
			rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
			/*
			 * Another thread could have allocated it; check
			 * again now that we have the struct lock.
			 */
			if (dn->dn_type == DMU_OT_NONE) {
				dnode_allocate(dn, ot, blocksize, 0,
				    bonustype, bonuslen, dn_slots, tx);
				rw_exit(&dn->dn_struct_rwlock);
				dmu_tx_add_new_object(tx, dn);
				dnode_rele(dn, FTAG);
				return (object);
			}
			rw_exit(&dn->dn_struct_rwlock);
			dnode_rele(dn, FTAG);
			DNODE_STAT_BUMP(dnode_alloc_race);
		}

		/*
		 * Skip to next known valid starting point on error.  This
		 * is the start of the next block of dnodes.
		 */
		if (dmu_object_next(os, &object, B_TRUE, 0) != 0) {
			object = P2ROUNDUP(object + 1, DNODES_PER_BLOCK);
			DNODE_STAT_BUMP(dnode_alloc_next_block);
		}
		(void) atomic_swap_64(cpuobj, object);
	}
}

int
dmu_object_claim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (dmu_object_claim_dnsize(os, object, ot, blocksize, bonustype,
	    bonuslen, 0, tx));
}

int
dmu_object_claim_dnsize(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx)
{
	dnode_t *dn;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	int err;

	if (dn_slots == 0)
		dn_slots = DNODE_MIN_SLOTS;
	ASSERT3S(dn_slots, >=, DNODE_MIN_SLOTS);
	ASSERT3S(dn_slots, <=, DNODE_MAX_SLOTS);

	if (object == DMU_META_DNODE_OBJECT && !dmu_tx_private_ok(tx))
		return (SET_ERROR(EBADF));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_FREE, dn_slots,
	    FTAG, &dn);
	if (err)
		return (err);

	dnode_allocate(dn, ot, blocksize, 0, bonustype, bonuslen, dn_slots, tx);
	dmu_tx_add_new_object(tx, dn);

	dnode_rele(dn, FTAG);

	return (0);
}

int
dmu_object_reclaim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (dmu_object_reclaim_dnsize(os, object, ot, blocksize, bonustype,
	    bonuslen, 0, tx));
}

int
dmu_object_reclaim_dnsize(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, int dnodesize,
    dmu_tx_t *tx)
{
	dnode_t *dn;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	int err;

	if (object == DMU_META_DNODE_OBJECT)
		return (SET_ERROR(EBADF));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED, 0,
	    FTAG, &dn);
	if (err)
		return (err);

	dnode_reallocate(dn, ot, blocksize, bonustype, bonuslen, dn_slots, tx);

	dnode_rele(dn, FTAG);
	return (err);
}


int
dmu_object_free(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err;

	ASSERT(object != DMU_META_DNODE_OBJECT || dmu_tx_private_ok(tx));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED, 0,
	    FTAG, &dn);
	if (err)
		return (err);

	ASSERT(dn->dn_type != DMU_OT_NONE);
	dnode_free_range(dn, 0, DMU_OBJECT_END, tx);
	dnode_free(dn, tx);
	dnode_rele(dn, FTAG);

	return (0);
}

/*
 * Return (in *objectp) the next object which is allocated (or a hole)
 * after *object, taking into account only objects that may have been modified
 * after the specified txg.
 */
int
dmu_object_next(objset_t *os, uint64_t *objectp, boolean_t hole, uint64_t txg)
{
	uint64_t offset;
	uint64_t start_obj;
	struct dsl_dataset *ds = os->os_dsl_dataset;
	int error;

	if (*objectp == 0) {
		start_obj = 1;
	} else if (ds && ds->ds_feature_inuse[SPA_FEATURE_LARGE_DNODE]) {
		uint64_t i = *objectp + 1;
		uint64_t last_obj = *objectp | (DNODES_PER_BLOCK - 1);
		dmu_object_info_t doi;

		/*
		 * Scan through the remaining meta dnode block.  The contents
		 * of each slot in the block are known so it can be quickly
		 * checked.  If the block is exhausted without a match then
		 * hand off to dnode_next_offset() for further scanning.
		 */
		while (i <= last_obj) {
			error = dmu_object_info(os, i, &doi);
			if (error == ENOENT) {
				if (hole) {
					*objectp = i;
					return (0);
				} else {
					i++;
				}
			} else if (error == EEXIST) {
				i++;
			} else if (error == 0) {
				if (hole) {
					i += doi.doi_dnodesize >> DNODE_SHIFT;
				} else {
					*objectp = i;
					return (0);
				}
			} else {
				return (error);
			}
		}

		start_obj = i;
	} else {
		start_obj = *objectp + 1;
	}

	offset = start_obj << DNODE_SHIFT;

	error = dnode_next_offset(DMU_META_DNODE(os),
	    (hole ? DNODE_FIND_HOLE : 0), &offset, 0, DNODES_PER_BLOCK, txg);

	*objectp = offset >> DNODE_SHIFT;

	return (error);
}

/*
 * Turn this object from old_type into DMU_OTN_ZAP_METADATA, and bump the
 * refcount on SPA_FEATURE_EXTENSIBLE_DATASET.
 *
 * Only for use from syncing context, on MOS objects.
 */
void
dmu_object_zapify(objset_t *mos, uint64_t object, dmu_object_type_t old_type,
    dmu_tx_t *tx)
{
	dnode_t *dn;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(dnode_hold(mos, object, FTAG, &dn));
	if (dn->dn_type == DMU_OTN_ZAP_METADATA) {
		dnode_rele(dn, FTAG);
		return;
	}
	ASSERT3U(dn->dn_type, ==, old_type);
	ASSERT0(dn->dn_maxblkid);
	dn->dn_next_type[tx->tx_txg & TXG_MASK] = dn->dn_type =
	    DMU_OTN_ZAP_METADATA;
	dnode_setdirty(dn, tx);
	dnode_rele(dn, FTAG);

	mzap_create_impl(mos, object, 0, 0, tx);

	spa_feature_incr(dmu_objset_spa(mos),
	    SPA_FEATURE_EXTENSIBLE_DATASET, tx);
}

void
dmu_object_free_zapified(objset_t *mos, uint64_t object, dmu_tx_t *tx)
{
	dnode_t *dn;
	dmu_object_type_t t;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(dnode_hold(mos, object, FTAG, &dn));
	t = dn->dn_type;
	dnode_rele(dn, FTAG);

	if (t == DMU_OTN_ZAP_METADATA) {
		spa_feature_decr(dmu_objset_spa(mos),
		    SPA_FEATURE_EXTENSIBLE_DATASET, tx);
	}
	VERIFY0(dmu_object_free(mos, object, tx));
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dmu_object_alloc);
EXPORT_SYMBOL(dmu_object_alloc_dnsize);
EXPORT_SYMBOL(dmu_object_claim);
EXPORT_SYMBOL(dmu_object_claim_dnsize);
EXPORT_SYMBOL(dmu_object_reclaim);
EXPORT_SYMBOL(dmu_object_reclaim_dnsize);
EXPORT_SYMBOL(dmu_object_free);
EXPORT_SYMBOL(dmu_object_next);
EXPORT_SYMBOL(dmu_object_zapify);
EXPORT_SYMBOL(dmu_object_free_zapified);

/* BEGIN CSTYLED */
module_param(dmu_object_alloc_chunk_shift, int, 0644);
MODULE_PARM_DESC(dmu_object_alloc_chunk_shift,
	"CPU-specific allocator grabs 2^N objects at once");
/* END CSTYLED */
#endif
