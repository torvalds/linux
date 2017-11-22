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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/dsl_pool.h>
#include <sys/zio.h>
#include <sys/space_map.h>
#include <sys/refcount.h>
#include <sys/zfeature.h>

/*
 * The data for a given space map can be kept on blocks of any size.
 * Larger blocks entail fewer i/o operations, but they also cause the
 * DMU to keep more data in-core, and also to waste more i/o bandwidth
 * when only a few blocks have changed since the last transaction group.
 */
int space_map_blksz = (1 << 12);

/*
 * Load the space map disk into the specified range tree. Segments of maptype
 * are added to the range tree, other segment types are removed.
 *
 * Note: space_map_load() will drop sm_lock across dmu_read() calls.
 * The caller must be OK with this.
 */
int
space_map_load(space_map_t *sm, range_tree_t *rt, maptype_t maptype)
{
	uint64_t *entry, *entry_map, *entry_map_end;
	uint64_t bufsize, size, offset, end, space;
	int error = 0;

	ASSERT(MUTEX_HELD(sm->sm_lock));

	end = space_map_length(sm);
	space = space_map_allocated(sm);

	VERIFY0(range_tree_space(rt));

	if (maptype == SM_FREE) {
		range_tree_add(rt, sm->sm_start, sm->sm_size);
		space = sm->sm_size - space;
	}

	bufsize = MAX(sm->sm_blksz, SPA_MINBLOCKSIZE);
	entry_map = vmem_alloc(bufsize, KM_SLEEP);

	mutex_exit(sm->sm_lock);
	if (end > bufsize) {
		dmu_prefetch(sm->sm_os, space_map_object(sm), 0, bufsize,
		    end - bufsize, ZIO_PRIORITY_SYNC_READ);
	}
	mutex_enter(sm->sm_lock);

	for (offset = 0; offset < end; offset += bufsize) {
		size = MIN(end - offset, bufsize);
		VERIFY(P2PHASE(size, sizeof (uint64_t)) == 0);
		VERIFY(size != 0);
		ASSERT3U(sm->sm_blksz, !=, 0);

		dprintf("object=%llu  offset=%llx  size=%llx\n",
		    space_map_object(sm), offset, size);

		mutex_exit(sm->sm_lock);
		error = dmu_read(sm->sm_os, space_map_object(sm), offset, size,
		    entry_map, DMU_READ_PREFETCH);
		mutex_enter(sm->sm_lock);
		if (error != 0)
			break;

		entry_map_end = entry_map + (size / sizeof (uint64_t));
		for (entry = entry_map; entry < entry_map_end; entry++) {
			uint64_t e = *entry;
			uint64_t offset, size;

			if (SM_DEBUG_DECODE(e))		/* Skip debug entries */
				continue;

			offset = (SM_OFFSET_DECODE(e) << sm->sm_shift) +
			    sm->sm_start;
			size = SM_RUN_DECODE(e) << sm->sm_shift;

			VERIFY0(P2PHASE(offset, 1ULL << sm->sm_shift));
			VERIFY0(P2PHASE(size, 1ULL << sm->sm_shift));
			VERIFY3U(offset, >=, sm->sm_start);
			VERIFY3U(offset + size, <=, sm->sm_start + sm->sm_size);
			if (SM_TYPE_DECODE(e) == maptype) {
				VERIFY3U(range_tree_space(rt) + size, <=,
				    sm->sm_size);
				range_tree_add(rt, offset, size);
			} else {
				range_tree_remove(rt, offset, size);
			}
		}
	}

	if (error == 0)
		VERIFY3U(range_tree_space(rt), ==, space);
	else
		range_tree_vacate(rt, NULL, NULL);

	vmem_free(entry_map, bufsize);
	return (error);
}

void
space_map_histogram_clear(space_map_t *sm)
{
	if (sm->sm_dbuf->db_size != sizeof (space_map_phys_t))
		return;

	bzero(sm->sm_phys->smp_histogram, sizeof (sm->sm_phys->smp_histogram));
}

boolean_t
space_map_histogram_verify(space_map_t *sm, range_tree_t *rt)
{
	int i;

	/*
	 * Verify that the in-core range tree does not have any
	 * ranges smaller than our sm_shift size.
	 */
	for (i = 0; i < sm->sm_shift; i++) {
		if (rt->rt_histogram[i] != 0)
			return (B_FALSE);
	}
	return (B_TRUE);
}

void
space_map_histogram_add(space_map_t *sm, range_tree_t *rt, dmu_tx_t *tx)
{
	int idx = 0;
	int i;

	ASSERT(MUTEX_HELD(rt->rt_lock));
	ASSERT(dmu_tx_is_syncing(tx));
	VERIFY3U(space_map_object(sm), !=, 0);

	if (sm->sm_dbuf->db_size != sizeof (space_map_phys_t))
		return;

	dmu_buf_will_dirty(sm->sm_dbuf, tx);

	ASSERT(space_map_histogram_verify(sm, rt));
	/*
	 * Transfer the content of the range tree histogram to the space
	 * map histogram. The space map histogram contains 32 buckets ranging
	 * between 2^sm_shift to 2^(32+sm_shift-1). The range tree,
	 * however, can represent ranges from 2^0 to 2^63. Since the space
	 * map only cares about allocatable blocks (minimum of sm_shift) we
	 * can safely ignore all ranges in the range tree smaller than sm_shift.
	 */
	for (i = sm->sm_shift; i < RANGE_TREE_HISTOGRAM_SIZE; i++) {

		/*
		 * Since the largest histogram bucket in the space map is
		 * 2^(32+sm_shift-1), we need to normalize the values in
		 * the range tree for any bucket larger than that size. For
		 * example given an sm_shift of 9, ranges larger than 2^40
		 * would get normalized as if they were 1TB ranges. Assume
		 * the range tree had a count of 5 in the 2^44 (16TB) bucket,
		 * the calculation below would normalize this to 5 * 2^4 (16).
		 */
		ASSERT3U(i, >=, idx + sm->sm_shift);
		sm->sm_phys->smp_histogram[idx] +=
		    rt->rt_histogram[i] << (i - idx - sm->sm_shift);

		/*
		 * Increment the space map's index as long as we haven't
		 * reached the maximum bucket size. Accumulate all ranges
		 * larger than the max bucket size into the last bucket.
		 */
		if (idx < SPACE_MAP_HISTOGRAM_SIZE - 1) {
			ASSERT3U(idx + sm->sm_shift, ==, i);
			idx++;
			ASSERT3U(idx, <, SPACE_MAP_HISTOGRAM_SIZE);
		}
	}
}

uint64_t
space_map_entries(space_map_t *sm, range_tree_t *rt)
{
	avl_tree_t *t = &rt->rt_root;
	range_seg_t *rs;
	uint64_t size, entries;

	/*
	 * All space_maps always have a debug entry so account for it here.
	 */
	entries = 1;

	/*
	 * Traverse the range tree and calculate the number of space map
	 * entries that would be required to write out the range tree.
	 */
	for (rs = avl_first(t); rs != NULL; rs = AVL_NEXT(t, rs)) {
		size = (rs->rs_end - rs->rs_start) >> sm->sm_shift;
		entries += howmany(size, SM_RUN_MAX);
	}
	return (entries);
}

/*
 * Note: space_map_write() will drop sm_lock across dmu_write() calls.
 */
void
space_map_write(space_map_t *sm, range_tree_t *rt, maptype_t maptype,
    dmu_tx_t *tx)
{
	objset_t *os = sm->sm_os;
	spa_t *spa = dmu_objset_spa(os);
	avl_tree_t *t = &rt->rt_root;
	range_seg_t *rs;
	uint64_t size, total, rt_space, nodes;
	uint64_t *entry, *entry_map, *entry_map_end;
	uint64_t expected_entries, actual_entries = 1;

	ASSERT(MUTEX_HELD(rt->rt_lock));
	ASSERT(dsl_pool_sync_context(dmu_objset_pool(os)));
	VERIFY3U(space_map_object(sm), !=, 0);
	dmu_buf_will_dirty(sm->sm_dbuf, tx);

	/*
	 * This field is no longer necessary since the in-core space map
	 * now contains the object number but is maintained for backwards
	 * compatibility.
	 */
	sm->sm_phys->smp_object = sm->sm_object;

	if (range_tree_space(rt) == 0) {
		VERIFY3U(sm->sm_object, ==, sm->sm_phys->smp_object);
		return;
	}

	if (maptype == SM_ALLOC)
		sm->sm_phys->smp_alloc += range_tree_space(rt);
	else
		sm->sm_phys->smp_alloc -= range_tree_space(rt);

	expected_entries = space_map_entries(sm, rt);

	entry_map = vmem_alloc(sm->sm_blksz, KM_SLEEP);
	entry_map_end = entry_map + (sm->sm_blksz / sizeof (uint64_t));
	entry = entry_map;

	*entry++ = SM_DEBUG_ENCODE(1) |
	    SM_DEBUG_ACTION_ENCODE(maptype) |
	    SM_DEBUG_SYNCPASS_ENCODE(spa_sync_pass(spa)) |
	    SM_DEBUG_TXG_ENCODE(dmu_tx_get_txg(tx));

	total = 0;
	nodes = avl_numnodes(&rt->rt_root);
	rt_space = range_tree_space(rt);
	for (rs = avl_first(t); rs != NULL; rs = AVL_NEXT(t, rs)) {
		uint64_t start;

		size = (rs->rs_end - rs->rs_start) >> sm->sm_shift;
		start = (rs->rs_start - sm->sm_start) >> sm->sm_shift;

		total += size << sm->sm_shift;

		while (size != 0) {
			uint64_t run_len;

			run_len = MIN(size, SM_RUN_MAX);

			if (entry == entry_map_end) {
				mutex_exit(rt->rt_lock);
				dmu_write(os, space_map_object(sm),
				    sm->sm_phys->smp_objsize, sm->sm_blksz,
				    entry_map, tx);
				mutex_enter(rt->rt_lock);
				sm->sm_phys->smp_objsize += sm->sm_blksz;
				entry = entry_map;
			}

			*entry++ = SM_OFFSET_ENCODE(start) |
			    SM_TYPE_ENCODE(maptype) |
			    SM_RUN_ENCODE(run_len);

			start += run_len;
			size -= run_len;
			actual_entries++;
		}
	}

	if (entry != entry_map) {
		size = (entry - entry_map) * sizeof (uint64_t);
		mutex_exit(rt->rt_lock);
		dmu_write(os, space_map_object(sm), sm->sm_phys->smp_objsize,
		    size, entry_map, tx);
		mutex_enter(rt->rt_lock);
		sm->sm_phys->smp_objsize += size;
	}
	ASSERT3U(expected_entries, ==, actual_entries);

	/*
	 * Ensure that the space_map's accounting wasn't changed
	 * while we were in the middle of writing it out.
	 */
	VERIFY3U(nodes, ==, avl_numnodes(&rt->rt_root));
	VERIFY3U(range_tree_space(rt), ==, rt_space);
	VERIFY3U(range_tree_space(rt), ==, total);

	vmem_free(entry_map, sm->sm_blksz);
}

static int
space_map_open_impl(space_map_t *sm)
{
	int error;
	u_longlong_t blocks;

	error = dmu_bonus_hold(sm->sm_os, sm->sm_object, sm, &sm->sm_dbuf);
	if (error)
		return (error);

	dmu_object_size_from_db(sm->sm_dbuf, &sm->sm_blksz, &blocks);
	sm->sm_phys = sm->sm_dbuf->db_data;
	return (0);
}

int
space_map_open(space_map_t **smp, objset_t *os, uint64_t object,
    uint64_t start, uint64_t size, uint8_t shift, kmutex_t *lp)
{
	space_map_t *sm;
	int error;

	ASSERT(*smp == NULL);
	ASSERT(os != NULL);
	ASSERT(object != 0);

	sm = kmem_alloc(sizeof (space_map_t), KM_SLEEP);

	sm->sm_start = start;
	sm->sm_size = size;
	sm->sm_shift = shift;
	sm->sm_lock = lp;
	sm->sm_os = os;
	sm->sm_object = object;
	sm->sm_length = 0;
	sm->sm_alloc = 0;
	sm->sm_blksz = 0;
	sm->sm_dbuf = NULL;
	sm->sm_phys = NULL;

	error = space_map_open_impl(sm);
	if (error != 0) {
		space_map_close(sm);
		return (error);
	}

	*smp = sm;

	return (0);
}

void
space_map_close(space_map_t *sm)
{
	if (sm == NULL)
		return;

	if (sm->sm_dbuf != NULL)
		dmu_buf_rele(sm->sm_dbuf, sm);
	sm->sm_dbuf = NULL;
	sm->sm_phys = NULL;

	kmem_free(sm, sizeof (*sm));
}

void
space_map_truncate(space_map_t *sm, dmu_tx_t *tx)
{
	objset_t *os = sm->sm_os;
	spa_t *spa = dmu_objset_spa(os);
	dmu_object_info_t doi;

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(os)));
	ASSERT(dmu_tx_is_syncing(tx));
	VERIFY3U(dmu_tx_get_txg(tx), <=, spa_final_dirty_txg(spa));

	dmu_object_info_from_db(sm->sm_dbuf, &doi);

	/*
	 * If the space map has the wrong bonus size (because
	 * SPA_FEATURE_SPACEMAP_HISTOGRAM has recently been enabled), or
	 * the wrong block size (because space_map_blksz has changed),
	 * free and re-allocate its object with the updated sizes.
	 *
	 * Otherwise, just truncate the current object.
	 */
	if ((spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM) &&
	    doi.doi_bonus_size != sizeof (space_map_phys_t)) ||
	    doi.doi_data_block_size != space_map_blksz) {
		zfs_dbgmsg("txg %llu, spa %s, sm %p, reallocating "
		    "object[%llu]: old bonus %u, old blocksz %u",
		    dmu_tx_get_txg(tx), spa_name(spa), sm, sm->sm_object,
		    doi.doi_bonus_size, doi.doi_data_block_size);

		space_map_free(sm, tx);
		dmu_buf_rele(sm->sm_dbuf, sm);

		sm->sm_object = space_map_alloc(sm->sm_os, tx);
		VERIFY0(space_map_open_impl(sm));
	} else {
		VERIFY0(dmu_free_range(os, space_map_object(sm), 0, -1ULL, tx));

		/*
		 * If the spacemap is reallocated, its histogram
		 * will be reset.  Do the same in the common case so that
		 * bugs related to the uncommon case do not go unnoticed.
		 */
		bzero(sm->sm_phys->smp_histogram,
		    sizeof (sm->sm_phys->smp_histogram));
	}

	dmu_buf_will_dirty(sm->sm_dbuf, tx);
	sm->sm_phys->smp_objsize = 0;
	sm->sm_phys->smp_alloc = 0;
}

/*
 * Update the in-core space_map allocation and length values.
 */
void
space_map_update(space_map_t *sm)
{
	if (sm == NULL)
		return;

	ASSERT(MUTEX_HELD(sm->sm_lock));

	sm->sm_alloc = sm->sm_phys->smp_alloc;
	sm->sm_length = sm->sm_phys->smp_objsize;
}

uint64_t
space_map_alloc(objset_t *os, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(os);
	uint64_t object;
	int bonuslen;

	if (spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM)) {
		spa_feature_incr(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM, tx);
		bonuslen = sizeof (space_map_phys_t);
		ASSERT3U(bonuslen, <=, dmu_bonus_max());
	} else {
		bonuslen = SPACE_MAP_SIZE_V0;
	}

	object = dmu_object_alloc(os,
	    DMU_OT_SPACE_MAP, space_map_blksz,
	    DMU_OT_SPACE_MAP_HEADER, bonuslen, tx);

	return (object);
}

void
space_map_free(space_map_t *sm, dmu_tx_t *tx)
{
	spa_t *spa;

	if (sm == NULL)
		return;

	spa = dmu_objset_spa(sm->sm_os);
	if (spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM)) {
		dmu_object_info_t doi;

		dmu_object_info_from_db(sm->sm_dbuf, &doi);
		if (doi.doi_bonus_size != SPACE_MAP_SIZE_V0) {
			VERIFY(spa_feature_is_active(spa,
			    SPA_FEATURE_SPACEMAP_HISTOGRAM));
			spa_feature_decr(spa,
			    SPA_FEATURE_SPACEMAP_HISTOGRAM, tx);
		}
	}

	VERIFY3U(dmu_object_free(sm->sm_os, space_map_object(sm), tx), ==, 0);
	sm->sm_object = 0;
}

uint64_t
space_map_object(space_map_t *sm)
{
	return (sm != NULL ? sm->sm_object : 0);
}

/*
 * Returns the already synced, on-disk allocated space.
 */
uint64_t
space_map_allocated(space_map_t *sm)
{
	return (sm != NULL ? sm->sm_alloc : 0);
}

/*
 * Returns the already synced, on-disk length;
 */
uint64_t
space_map_length(space_map_t *sm)
{
	return (sm != NULL ? sm->sm_length : 0);
}

/*
 * Returns the allocated space that is currently syncing.
 */
int64_t
space_map_alloc_delta(space_map_t *sm)
{
	if (sm == NULL)
		return (0);
	ASSERT(sm->sm_dbuf != NULL);
	return (sm->sm_phys->smp_alloc - space_map_allocated(sm));
}
