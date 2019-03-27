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
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 * Copyright 2014 HybridCluster. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/dsl_dataset.h>


static uint64_t
dmu_object_alloc_impl(objset_t *os, dmu_object_type_t ot, int blocksize,
    int indirect_blockshift, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx)
{
	uint64_t object;
	uint64_t L1_dnode_count = DNODES_PER_BLOCK <<
	    (DMU_META_DNODE(os)->dn_indblkshift - SPA_BLKPTRSHIFT);
	dnode_t *dn = NULL;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	boolean_t restarted = B_FALSE;

	if (dn_slots == 0) {
		dn_slots = DNODE_MIN_SLOTS;
	} else {
		ASSERT3S(dn_slots, >=, DNODE_MIN_SLOTS);
		ASSERT3S(dn_slots, <=, DNODE_MAX_SLOTS);
	}
 
	mutex_enter(&os->os_obj_lock);
	for (;;) {
		object = os->os_obj_next;
		/*
		 * Each time we polish off a L1 bp worth of dnodes (2^12
		 * objects), move to another L1 bp that's still
		 * reasonably sparse (at most 1/4 full). Look from the
		 * beginning at most once per txg. If we still can't
		 * allocate from that L1 block, search for an empty L0
		 * block, which will quickly skip to the end of the
		 * metadnode if the no nearby L0 blocks are empty. This
		 * fallback avoids a pathology where full dnode blocks
		 * containing large dnodes appear sparse because they
		 * have a low blk_fill, leading to many failed
		 * allocation attempts. In the long term a better
		 * mechanism to search for sparse metadnode regions,
		 * such as spacemaps, could be implemented.
		 *
		 * os_scan_dnodes is set during txg sync if enough objects
		 * have been freed since the previous rescan to justify
		 * backfilling again.
		 *
		 * Note that dmu_traverse depends on the behavior that we use
		 * multiple blocks of the dnode object before going back to
		 * reuse objects.  Any change to this algorithm should preserve
		 * that property or find another solution to the issues
		 * described in traverse_visitbp.
		 */
		if (P2PHASE(object, L1_dnode_count) == 0) {
			uint64_t offset;
			uint64_t blkfill;
			int minlvl;
			int error;
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
			    DNODE_FIND_HOLE, &offset, minlvl, blkfill, 0);
			if (error == 0)
				object = offset >> DNODE_SHIFT;
		}
		os->os_obj_next = object + dn_slots;

		/*
		 * XXX We should check for an i/o error here and return
		 * up to our caller.  Actually we should pre-read it in
		 * dmu_tx_assign(), but there is currently no mechanism
		 * to do so.
		 */
		(void) dnode_hold_impl(os, object, DNODE_MUST_BE_FREE, dn_slots,
		    FTAG, &dn);
		if (dn)
			break;

		if (dmu_object_next(os, &object, B_TRUE, 0) == 0)
			os->os_obj_next = object;
		else
			/*
			 * Skip to next known valid starting point for a dnode.
			 */
			os->os_obj_next = P2ROUNDUP(object + 1,
			    DNODES_PER_BLOCK);
	}

	dnode_allocate(dn, ot, blocksize, indirect_blockshift,
		       bonustype, bonuslen, dn_slots, tx);
	mutex_exit(&os->os_obj_lock);

	dmu_tx_add_new_object(tx, dn);
	dnode_rele(dn, FTAG);

	return (object);
}

uint64_t
dmu_object_alloc(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return dmu_object_alloc_impl(os, ot, blocksize, 0, bonustype,
	    bonuslen, 0, tx);
}

uint64_t
dmu_object_alloc_ibs(objset_t *os, dmu_object_type_t ot, int blocksize,
    int indirect_blockshift, dmu_object_type_t bonustype, int bonuslen,
    dmu_tx_t *tx)
{
	return dmu_object_alloc_impl(os, ot, blocksize, indirect_blockshift,
	    bonustype, bonuslen, 0, tx);
}

uint64_t
dmu_object_alloc_dnsize(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	return (dmu_object_alloc_impl(os, ot, blocksize, 0, bonustype,
	    bonuslen, dnodesize, tx));
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
	/*
	 * If we don't create this free range, we'll leak indirect blocks when
	 * we get to freeing the dnode in syncing context.
	 */
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
	dmu_object_info_t doi;
	struct dsl_dataset *ds = os->os_dsl_dataset;
	int dnodesize;
	int error;

	/*
	 * Avoid expensive dnode hold if this dataset doesn't use large dnodes.
	 */
	if (ds && ds->ds_feature_inuse[SPA_FEATURE_LARGE_DNODE]) {
		error = dmu_object_info(os, *objectp, &doi);
		if (error && !(error == EINVAL && *objectp == 0))
			return (SET_ERROR(error));
		else
			dnodesize = doi.doi_dnodesize;
	} else {
		dnodesize = DNODE_MIN_SIZE;
	}

	if (*objectp == 0)
		offset = 1 << DNODE_SHIFT;
	else
		offset = (*objectp << DNODE_SHIFT) + dnodesize;

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

	/*
	 * We must initialize the ZAP data before changing the type,
	 * so that concurrent calls to *_is_zapified() can determine if
	 * the object has been completely zapified by checking the type.
	 */
	mzap_create_impl(mos, object, 0, 0, tx);

	dn->dn_next_type[tx->tx_txg & TXG_MASK] = dn->dn_type =
	    DMU_OTN_ZAP_METADATA;
	dnode_setdirty(dn, tx);
	dnode_rele(dn, FTAG);

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
