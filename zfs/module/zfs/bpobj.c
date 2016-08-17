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
 * Copyright (c) 2011, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 */

#include <sys/bpobj.h>
#include <sys/zfs_context.h>
#include <sys/refcount.h>
#include <sys/dsl_pool.h>
#include <sys/zfeature.h>
#include <sys/zap.h>

/*
 * Return an empty bpobj, preferably the empty dummy one (dp_empty_bpobj).
 */
uint64_t
bpobj_alloc_empty(objset_t *os, int blocksize, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(os);
	dsl_pool_t *dp = dmu_objset_pool(os);

	if (spa_feature_is_enabled(spa, SPA_FEATURE_EMPTY_BPOBJ)) {
		if (!spa_feature_is_active(spa, SPA_FEATURE_EMPTY_BPOBJ)) {
			ASSERT0(dp->dp_empty_bpobj);
			dp->dp_empty_bpobj =
			    bpobj_alloc(os, SPA_OLD_MAXBLOCKSIZE, tx);
			VERIFY(zap_add(os,
			    DMU_POOL_DIRECTORY_OBJECT,
			    DMU_POOL_EMPTY_BPOBJ, sizeof (uint64_t), 1,
			    &dp->dp_empty_bpobj, tx) == 0);
		}
		spa_feature_incr(spa, SPA_FEATURE_EMPTY_BPOBJ, tx);
		ASSERT(dp->dp_empty_bpobj != 0);
		return (dp->dp_empty_bpobj);
	} else {
		return (bpobj_alloc(os, blocksize, tx));
	}
}

void
bpobj_decr_empty(objset_t *os, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_objset_pool(os);

	spa_feature_decr(dmu_objset_spa(os), SPA_FEATURE_EMPTY_BPOBJ, tx);
	if (!spa_feature_is_active(dmu_objset_spa(os),
	    SPA_FEATURE_EMPTY_BPOBJ)) {
		VERIFY3U(0, ==, zap_remove(dp->dp_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_EMPTY_BPOBJ, tx));
		VERIFY3U(0, ==, dmu_object_free(os, dp->dp_empty_bpobj, tx));
		dp->dp_empty_bpobj = 0;
	}
}

uint64_t
bpobj_alloc(objset_t *os, int blocksize, dmu_tx_t *tx)
{
	int size;

	if (spa_version(dmu_objset_spa(os)) < SPA_VERSION_BPOBJ_ACCOUNT)
		size = BPOBJ_SIZE_V0;
	else if (spa_version(dmu_objset_spa(os)) < SPA_VERSION_DEADLISTS)
		size = BPOBJ_SIZE_V1;
	else
		size = sizeof (bpobj_phys_t);

	return (dmu_object_alloc(os, DMU_OT_BPOBJ, blocksize,
	    DMU_OT_BPOBJ_HDR, size, tx));
}

void
bpobj_free(objset_t *os, uint64_t obj, dmu_tx_t *tx)
{
	int64_t i;
	bpobj_t bpo;
	dmu_object_info_t doi;
	int epb;
	dmu_buf_t *dbuf = NULL;

	ASSERT(obj != dmu_objset_pool(os)->dp_empty_bpobj);
	VERIFY3U(0, ==, bpobj_open(&bpo, os, obj));

	mutex_enter(&bpo.bpo_lock);

	if (!bpo.bpo_havesubobj || bpo.bpo_phys->bpo_subobjs == 0)
		goto out;

	VERIFY3U(0, ==, dmu_object_info(os, bpo.bpo_phys->bpo_subobjs, &doi));
	epb = doi.doi_data_block_size / sizeof (uint64_t);

	for (i = bpo.bpo_phys->bpo_num_subobjs - 1; i >= 0; i--) {
		uint64_t *objarray;
		uint64_t offset, blkoff;

		offset = i * sizeof (uint64_t);
		blkoff = P2PHASE(i, epb);

		if (dbuf == NULL || dbuf->db_offset > offset) {
			if (dbuf)
				dmu_buf_rele(dbuf, FTAG);
			VERIFY3U(0, ==, dmu_buf_hold(os,
			    bpo.bpo_phys->bpo_subobjs, offset, FTAG, &dbuf, 0));
		}

		ASSERT3U(offset, >=, dbuf->db_offset);
		ASSERT3U(offset, <, dbuf->db_offset + dbuf->db_size);

		objarray = dbuf->db_data;
		bpobj_free(os, objarray[blkoff], tx);
	}
	if (dbuf) {
		dmu_buf_rele(dbuf, FTAG);
		dbuf = NULL;
	}
	VERIFY3U(0, ==, dmu_object_free(os, bpo.bpo_phys->bpo_subobjs, tx));

out:
	mutex_exit(&bpo.bpo_lock);
	bpobj_close(&bpo);

	VERIFY3U(0, ==, dmu_object_free(os, obj, tx));
}

int
bpobj_open(bpobj_t *bpo, objset_t *os, uint64_t object)
{
	dmu_object_info_t doi;
	int err;

	err = dmu_object_info(os, object, &doi);
	if (err)
		return (err);

	bzero(bpo, sizeof (*bpo));
	mutex_init(&bpo->bpo_lock, NULL, MUTEX_DEFAULT, NULL);

	ASSERT(bpo->bpo_dbuf == NULL);
	ASSERT(bpo->bpo_phys == NULL);
	ASSERT(object != 0);
	ASSERT3U(doi.doi_type, ==, DMU_OT_BPOBJ);
	ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_BPOBJ_HDR);

	err = dmu_bonus_hold(os, object, bpo, &bpo->bpo_dbuf);
	if (err)
		return (err);

	bpo->bpo_os = os;
	bpo->bpo_object = object;
	bpo->bpo_epb = doi.doi_data_block_size >> SPA_BLKPTRSHIFT;
	bpo->bpo_havecomp = (doi.doi_bonus_size > BPOBJ_SIZE_V0);
	bpo->bpo_havesubobj = (doi.doi_bonus_size > BPOBJ_SIZE_V1);
	bpo->bpo_phys = bpo->bpo_dbuf->db_data;
	return (0);
}

void
bpobj_close(bpobj_t *bpo)
{
	/* Lame workaround for closing a bpobj that was never opened. */
	if (bpo->bpo_object == 0)
		return;

	dmu_buf_rele(bpo->bpo_dbuf, bpo);
	if (bpo->bpo_cached_dbuf != NULL)
		dmu_buf_rele(bpo->bpo_cached_dbuf, bpo);
	bpo->bpo_dbuf = NULL;
	bpo->bpo_phys = NULL;
	bpo->bpo_cached_dbuf = NULL;
	bpo->bpo_object = 0;

	mutex_destroy(&bpo->bpo_lock);
}

static boolean_t
bpobj_hasentries(bpobj_t *bpo)
{
	return (bpo->bpo_phys->bpo_num_blkptrs != 0 ||
	    (bpo->bpo_havesubobj && bpo->bpo_phys->bpo_num_subobjs != 0));
}

static int
bpobj_iterate_impl(bpobj_t *bpo, bpobj_itor_t func, void *arg, dmu_tx_t *tx,
    boolean_t free)
{
	dmu_object_info_t doi;
	int epb;
	int64_t i;
	int err = 0;
	dmu_buf_t *dbuf = NULL;

	mutex_enter(&bpo->bpo_lock);

	if (!bpobj_hasentries(bpo))
		goto out;

	if (free)
		dmu_buf_will_dirty(bpo->bpo_dbuf, tx);

	for (i = bpo->bpo_phys->bpo_num_blkptrs - 1; i >= 0; i--) {
		blkptr_t *bparray;
		blkptr_t *bp;
		uint64_t offset, blkoff;

		offset = i * sizeof (blkptr_t);
		blkoff = P2PHASE(i, bpo->bpo_epb);

		if (dbuf == NULL || dbuf->db_offset > offset) {
			if (dbuf)
				dmu_buf_rele(dbuf, FTAG);
			err = dmu_buf_hold(bpo->bpo_os, bpo->bpo_object, offset,
			    FTAG, &dbuf, 0);
			if (err)
				break;
		}

		ASSERT3U(offset, >=, dbuf->db_offset);
		ASSERT3U(offset, <, dbuf->db_offset + dbuf->db_size);

		bparray = dbuf->db_data;
		bp = &bparray[blkoff];
		err = func(arg, bp, tx);
		if (err)
			break;
		if (free) {
			bpo->bpo_phys->bpo_bytes -=
			    bp_get_dsize_sync(dmu_objset_spa(bpo->bpo_os), bp);
			ASSERT3S(bpo->bpo_phys->bpo_bytes, >=, 0);
			if (bpo->bpo_havecomp) {
				bpo->bpo_phys->bpo_comp -= BP_GET_PSIZE(bp);
				bpo->bpo_phys->bpo_uncomp -= BP_GET_UCSIZE(bp);
			}
			bpo->bpo_phys->bpo_num_blkptrs--;
			ASSERT3S(bpo->bpo_phys->bpo_num_blkptrs, >=, 0);
		}
	}
	if (dbuf) {
		dmu_buf_rele(dbuf, FTAG);
		dbuf = NULL;
	}
	if (free) {
		VERIFY3U(0, ==, dmu_free_range(bpo->bpo_os, bpo->bpo_object,
		    (i + 1) * sizeof (blkptr_t), DMU_OBJECT_END, tx));
	}
	if (err || !bpo->bpo_havesubobj || bpo->bpo_phys->bpo_subobjs == 0)
		goto out;

	ASSERT(bpo->bpo_havecomp);
	err = dmu_object_info(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs, &doi);
	if (err) {
		mutex_exit(&bpo->bpo_lock);
		return (err);
	}
	ASSERT3U(doi.doi_type, ==, DMU_OT_BPOBJ_SUBOBJ);
	epb = doi.doi_data_block_size / sizeof (uint64_t);

	for (i = bpo->bpo_phys->bpo_num_subobjs - 1; i >= 0; i--) {
		uint64_t *objarray;
		uint64_t offset, blkoff;
		bpobj_t sublist;
		uint64_t used_before, comp_before, uncomp_before;
		uint64_t used_after, comp_after, uncomp_after;

		offset = i * sizeof (uint64_t);
		blkoff = P2PHASE(i, epb);

		if (dbuf == NULL || dbuf->db_offset > offset) {
			if (dbuf)
				dmu_buf_rele(dbuf, FTAG);
			err = dmu_buf_hold(bpo->bpo_os,
			    bpo->bpo_phys->bpo_subobjs, offset, FTAG, &dbuf, 0);
			if (err)
				break;
		}

		ASSERT3U(offset, >=, dbuf->db_offset);
		ASSERT3U(offset, <, dbuf->db_offset + dbuf->db_size);

		objarray = dbuf->db_data;
		err = bpobj_open(&sublist, bpo->bpo_os, objarray[blkoff]);
		if (err)
			break;
		if (free) {
			err = bpobj_space(&sublist,
			    &used_before, &comp_before, &uncomp_before);
			if (err != 0) {
				bpobj_close(&sublist);
				break;
			}
		}
		err = bpobj_iterate_impl(&sublist, func, arg, tx, free);
		if (free) {
			VERIFY3U(0, ==, bpobj_space(&sublist,
			    &used_after, &comp_after, &uncomp_after));
			bpo->bpo_phys->bpo_bytes -= used_before - used_after;
			ASSERT3S(bpo->bpo_phys->bpo_bytes, >=, 0);
			bpo->bpo_phys->bpo_comp -= comp_before - comp_after;
			bpo->bpo_phys->bpo_uncomp -=
			    uncomp_before - uncomp_after;
		}

		bpobj_close(&sublist);
		if (err)
			break;
		if (free) {
			err = dmu_object_free(bpo->bpo_os,
			    objarray[blkoff], tx);
			if (err)
				break;
			bpo->bpo_phys->bpo_num_subobjs--;
			ASSERT3S(bpo->bpo_phys->bpo_num_subobjs, >=, 0);
		}
	}
	if (dbuf) {
		dmu_buf_rele(dbuf, FTAG);
		dbuf = NULL;
	}
	if (free) {
		VERIFY3U(0, ==, dmu_free_range(bpo->bpo_os,
		    bpo->bpo_phys->bpo_subobjs,
		    (i + 1) * sizeof (uint64_t), DMU_OBJECT_END, tx));
	}

out:
	/* If there are no entries, there should be no bytes. */
	if (!bpobj_hasentries(bpo)) {
		ASSERT0(bpo->bpo_phys->bpo_bytes);
		ASSERT0(bpo->bpo_phys->bpo_comp);
		ASSERT0(bpo->bpo_phys->bpo_uncomp);
	}

	mutex_exit(&bpo->bpo_lock);
	return (err);
}

/*
 * Iterate and remove the entries.  If func returns nonzero, iteration
 * will stop and that entry will not be removed.
 */
int
bpobj_iterate(bpobj_t *bpo, bpobj_itor_t func, void *arg, dmu_tx_t *tx)
{
	return (bpobj_iterate_impl(bpo, func, arg, tx, B_TRUE));
}

/*
 * Iterate the entries.  If func returns nonzero, iteration will stop.
 */
int
bpobj_iterate_nofree(bpobj_t *bpo, bpobj_itor_t func, void *arg, dmu_tx_t *tx)
{
	return (bpobj_iterate_impl(bpo, func, arg, tx, B_FALSE));
}

void
bpobj_enqueue_subobj(bpobj_t *bpo, uint64_t subobj, dmu_tx_t *tx)
{
	bpobj_t subbpo;
	uint64_t used, comp, uncomp, subsubobjs;
	ASSERTV(dmu_object_info_t doi);

	ASSERT(bpo->bpo_havesubobj);
	ASSERT(bpo->bpo_havecomp);
	ASSERT(bpo->bpo_object != dmu_objset_pool(bpo->bpo_os)->dp_empty_bpobj);

	if (subobj == dmu_objset_pool(bpo->bpo_os)->dp_empty_bpobj) {
		bpobj_decr_empty(bpo->bpo_os, tx);
		return;
	}

	VERIFY3U(0, ==, bpobj_open(&subbpo, bpo->bpo_os, subobj));
	VERIFY3U(0, ==, bpobj_space(&subbpo, &used, &comp, &uncomp));

	if (!bpobj_hasentries(&subbpo)) {
		/* No point in having an empty subobj. */
		bpobj_close(&subbpo);
		bpobj_free(bpo->bpo_os, subobj, tx);
		return;
	}

	mutex_enter(&bpo->bpo_lock);
	dmu_buf_will_dirty(bpo->bpo_dbuf, tx);
	if (bpo->bpo_phys->bpo_subobjs == 0) {
		bpo->bpo_phys->bpo_subobjs = dmu_object_alloc(bpo->bpo_os,
		    DMU_OT_BPOBJ_SUBOBJ, SPA_OLD_MAXBLOCKSIZE,
		    DMU_OT_NONE, 0, tx);
	}

	ASSERT0(dmu_object_info(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs, &doi));
	ASSERT3U(doi.doi_type, ==, DMU_OT_BPOBJ_SUBOBJ);

	dmu_write(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs,
	    bpo->bpo_phys->bpo_num_subobjs * sizeof (subobj),
	    sizeof (subobj), &subobj, tx);
	bpo->bpo_phys->bpo_num_subobjs++;

	/*
	 * If subobj has only one block of subobjs, then move subobj's
	 * subobjs to bpo's subobj list directly.  This reduces
	 * recursion in bpobj_iterate due to nested subobjs.
	 */
	subsubobjs = subbpo.bpo_phys->bpo_subobjs;
	if (subsubobjs != 0) {
		dmu_object_info_t doi;

		VERIFY3U(0, ==, dmu_object_info(bpo->bpo_os, subsubobjs, &doi));
		if (doi.doi_max_offset == doi.doi_data_block_size) {
			dmu_buf_t *subdb;
			uint64_t numsubsub = subbpo.bpo_phys->bpo_num_subobjs;

			VERIFY3U(0, ==, dmu_buf_hold(bpo->bpo_os, subsubobjs,
			    0, FTAG, &subdb, 0));
			/*
			 * Make sure that we are not asking dmu_write()
			 * to write more data than we have in our buffer.
			 */
			VERIFY3U(subdb->db_size, >=,
			    numsubsub * sizeof (subobj));
			dmu_write(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs,
			    bpo->bpo_phys->bpo_num_subobjs * sizeof (subobj),
			    numsubsub * sizeof (subobj), subdb->db_data, tx);
			dmu_buf_rele(subdb, FTAG);
			bpo->bpo_phys->bpo_num_subobjs += numsubsub;

			dmu_buf_will_dirty(subbpo.bpo_dbuf, tx);
			subbpo.bpo_phys->bpo_subobjs = 0;
			VERIFY3U(0, ==, dmu_object_free(bpo->bpo_os,
			    subsubobjs, tx));
		}
	}
	bpo->bpo_phys->bpo_bytes += used;
	bpo->bpo_phys->bpo_comp += comp;
	bpo->bpo_phys->bpo_uncomp += uncomp;
	mutex_exit(&bpo->bpo_lock);

	bpobj_close(&subbpo);
}

void
bpobj_enqueue(bpobj_t *bpo, const blkptr_t *bp, dmu_tx_t *tx)
{
	blkptr_t stored_bp = *bp;
	uint64_t offset;
	int blkoff;
	blkptr_t *bparray;

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(bpo->bpo_object != dmu_objset_pool(bpo->bpo_os)->dp_empty_bpobj);

	if (BP_IS_EMBEDDED(bp)) {
		/*
		 * The bpobj will compress better without the payload.
		 *
		 * Note that we store EMBEDDED bp's because they have an
		 * uncompressed size, which must be accounted for.  An
		 * alternative would be to add their size to bpo_uncomp
		 * without storing the bp, but that would create additional
		 * complications: bpo_uncomp would be inconsistent with the
		 * set of BP's stored, and bpobj_iterate() wouldn't visit
		 * all the space accounted for in the bpobj.
		 */
		bzero(&stored_bp, sizeof (stored_bp));
		stored_bp.blk_prop = bp->blk_prop;
		stored_bp.blk_birth = bp->blk_birth;
	} else if (!BP_GET_DEDUP(bp)) {
		/* The bpobj will compress better without the checksum */
		bzero(&stored_bp.blk_cksum, sizeof (stored_bp.blk_cksum));
	}

	/* We never need the fill count. */
	stored_bp.blk_fill = 0;

	mutex_enter(&bpo->bpo_lock);

	offset = bpo->bpo_phys->bpo_num_blkptrs * sizeof (stored_bp);
	blkoff = P2PHASE(bpo->bpo_phys->bpo_num_blkptrs, bpo->bpo_epb);

	if (bpo->bpo_cached_dbuf == NULL ||
	    offset < bpo->bpo_cached_dbuf->db_offset ||
	    offset >= bpo->bpo_cached_dbuf->db_offset +
	    bpo->bpo_cached_dbuf->db_size) {
		if (bpo->bpo_cached_dbuf)
			dmu_buf_rele(bpo->bpo_cached_dbuf, bpo);
		VERIFY3U(0, ==, dmu_buf_hold(bpo->bpo_os, bpo->bpo_object,
		    offset, bpo, &bpo->bpo_cached_dbuf, 0));
	}

	dmu_buf_will_dirty(bpo->bpo_cached_dbuf, tx);
	bparray = bpo->bpo_cached_dbuf->db_data;
	bparray[blkoff] = stored_bp;

	dmu_buf_will_dirty(bpo->bpo_dbuf, tx);
	bpo->bpo_phys->bpo_num_blkptrs++;
	bpo->bpo_phys->bpo_bytes +=
	    bp_get_dsize_sync(dmu_objset_spa(bpo->bpo_os), bp);
	if (bpo->bpo_havecomp) {
		bpo->bpo_phys->bpo_comp += BP_GET_PSIZE(bp);
		bpo->bpo_phys->bpo_uncomp += BP_GET_UCSIZE(bp);
	}
	mutex_exit(&bpo->bpo_lock);
}

struct space_range_arg {
	spa_t *spa;
	uint64_t mintxg;
	uint64_t maxtxg;
	uint64_t used;
	uint64_t comp;
	uint64_t uncomp;
};

/* ARGSUSED */
static int
space_range_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	struct space_range_arg *sra = arg;

	if (bp->blk_birth > sra->mintxg && bp->blk_birth <= sra->maxtxg) {
		if (dsl_pool_sync_context(spa_get_dsl(sra->spa)))
			sra->used += bp_get_dsize_sync(sra->spa, bp);
		else
			sra->used += bp_get_dsize(sra->spa, bp);
		sra->comp += BP_GET_PSIZE(bp);
		sra->uncomp += BP_GET_UCSIZE(bp);
	}
	return (0);
}

int
bpobj_space(bpobj_t *bpo, uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	mutex_enter(&bpo->bpo_lock);

	*usedp = bpo->bpo_phys->bpo_bytes;
	if (bpo->bpo_havecomp) {
		*compp = bpo->bpo_phys->bpo_comp;
		*uncompp = bpo->bpo_phys->bpo_uncomp;
		mutex_exit(&bpo->bpo_lock);
		return (0);
	} else {
		mutex_exit(&bpo->bpo_lock);
		return (bpobj_space_range(bpo, 0, UINT64_MAX,
		    usedp, compp, uncompp));
	}
}

/*
 * Return the amount of space in the bpobj which is:
 * mintxg < blk_birth <= maxtxg
 */
int
bpobj_space_range(bpobj_t *bpo, uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	struct space_range_arg sra = { 0 };
	int err;

	/*
	 * As an optimization, if they want the whole txg range, just
	 * get bpo_bytes rather than iterating over the bps.
	 */
	if (mintxg < TXG_INITIAL && maxtxg == UINT64_MAX && bpo->bpo_havecomp)
		return (bpobj_space(bpo, usedp, compp, uncompp));

	sra.spa = dmu_objset_spa(bpo->bpo_os);
	sra.mintxg = mintxg;
	sra.maxtxg = maxtxg;

	err = bpobj_iterate_nofree(bpo, space_range_cb, &sra, NULL);
	*usedp = sra.used;
	*compp = sra.comp;
	*uncompp = sra.uncomp;
	return (err);
}
