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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
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

SYSCTL_DECL(_vfs_zfs);

/*
 * Note on space map block size:
 *
 * The data for a given space map can be kept on blocks of any size.
 * Larger blocks entail fewer I/O operations, but they also cause the
 * DMU to keep more data in-core, and also to waste more I/O bandwidth
 * when only a few blocks have changed since the last transaction group.
 */

/*
 * Enabled whenever we want to stress test the use of double-word
 * space map entries.
 */
boolean_t zfs_force_some_double_word_sm_entries = B_FALSE;

/*
 * Override the default indirect block size of 128K, instead using 16K for
 * spacemaps (2^14 bytes).  This dramatically reduces write inflation since
 * appending to a spacemap typically has to write one data block (4KB) and one
 * or two indirect blocks (16K-32K, rather than 128K).
 */
int space_map_ibs = 14;

SYSCTL_INT(_vfs_zfs, OID_AUTO, space_map_ibs, CTLFLAG_RWTUN,
    &space_map_ibs, 0, "Space map indirect block shift");

boolean_t
sm_entry_is_debug(uint64_t e)
{
	return (SM_PREFIX_DECODE(e) == SM_DEBUG_PREFIX);
}

boolean_t
sm_entry_is_single_word(uint64_t e)
{
	uint8_t prefix = SM_PREFIX_DECODE(e);
	return (prefix != SM_DEBUG_PREFIX && prefix != SM2_PREFIX);
}

boolean_t
sm_entry_is_double_word(uint64_t e)
{
	return (SM_PREFIX_DECODE(e) == SM2_PREFIX);
}

/*
 * Iterate through the space map, invoking the callback on each (non-debug)
 * space map entry.
 */
int
space_map_iterate(space_map_t *sm, sm_cb_t callback, void *arg)
{
	uint64_t sm_len = space_map_length(sm);
	ASSERT3U(sm->sm_blksz, !=, 0);

	dmu_prefetch(sm->sm_os, space_map_object(sm), 0, 0, sm_len,
	    ZIO_PRIORITY_SYNC_READ);

	uint64_t blksz = sm->sm_blksz;
	int error = 0;
	for (uint64_t block_base = 0; block_base < sm_len && error == 0;
	    block_base += blksz) {
		dmu_buf_t *db;
		error = dmu_buf_hold(sm->sm_os, space_map_object(sm),
		    block_base, FTAG, &db, DMU_READ_PREFETCH);
		if (error != 0)
			return (error);

		uint64_t *block_start = db->db_data;
		uint64_t block_length = MIN(sm_len - block_base, blksz);
		uint64_t *block_end = block_start +
		    (block_length / sizeof (uint64_t));

		VERIFY0(P2PHASE(block_length, sizeof (uint64_t)));
		VERIFY3U(block_length, !=, 0);
		ASSERT3U(blksz, ==, db->db_size);

		for (uint64_t *block_cursor = block_start;
		    block_cursor < block_end && error == 0; block_cursor++) {
			uint64_t e = *block_cursor;

			if (sm_entry_is_debug(e)) /* Skip debug entries */
				continue;

			uint64_t raw_offset, raw_run, vdev_id;
			maptype_t type;
			if (sm_entry_is_single_word(e)) {
				type = SM_TYPE_DECODE(e);
				vdev_id = SM_NO_VDEVID;
				raw_offset = SM_OFFSET_DECODE(e);
				raw_run = SM_RUN_DECODE(e);
			} else {
				/* it is a two-word entry */
				ASSERT(sm_entry_is_double_word(e));
				raw_run = SM2_RUN_DECODE(e);
				vdev_id = SM2_VDEV_DECODE(e);

				/* move on to the second word */
				block_cursor++;
				e = *block_cursor;
				VERIFY3P(block_cursor, <=, block_end);

				type = SM2_TYPE_DECODE(e);
				raw_offset = SM2_OFFSET_DECODE(e);
			}

			uint64_t entry_offset = (raw_offset << sm->sm_shift) +
			    sm->sm_start;
			uint64_t entry_run = raw_run << sm->sm_shift;

			VERIFY0(P2PHASE(entry_offset, 1ULL << sm->sm_shift));
			VERIFY0(P2PHASE(entry_run, 1ULL << sm->sm_shift));
			ASSERT3U(entry_offset, >=, sm->sm_start);
			ASSERT3U(entry_offset, <, sm->sm_start + sm->sm_size);
			ASSERT3U(entry_run, <=, sm->sm_size);
			ASSERT3U(entry_offset + entry_run, <=,
			    sm->sm_start + sm->sm_size);

			space_map_entry_t sme = {
			    .sme_type = type,
			    .sme_vdev = vdev_id,
			    .sme_offset = entry_offset,
			    .sme_run = entry_run
			};
			error = callback(&sme, arg);
		}
		dmu_buf_rele(db, FTAG);
	}
	return (error);
}

/*
 * Reads the entries from the last block of the space map into
 * buf in reverse order. Populates nwords with number of words
 * in the last block.
 *
 * Refer to block comment within space_map_incremental_destroy()
 * to understand why this function is needed.
 */
static int
space_map_reversed_last_block_entries(space_map_t *sm, uint64_t *buf,
    uint64_t bufsz, uint64_t *nwords)
{
	int error = 0;
	dmu_buf_t *db;

	/*
	 * Find the offset of the last word in the space map and use
	 * that to read the last block of the space map with
	 * dmu_buf_hold().
	 */
	uint64_t last_word_offset =
	    sm->sm_phys->smp_objsize - sizeof (uint64_t);
	error = dmu_buf_hold(sm->sm_os, space_map_object(sm), last_word_offset,
	    FTAG, &db, DMU_READ_NO_PREFETCH);
	if (error != 0)
		return (error);

	ASSERT3U(sm->sm_object, ==, db->db_object);
	ASSERT3U(sm->sm_blksz, ==, db->db_size);
	ASSERT3U(bufsz, >=, db->db_size);
	ASSERT(nwords != NULL);

	uint64_t *words = db->db_data;
	*nwords =
	    (sm->sm_phys->smp_objsize - db->db_offset) / sizeof (uint64_t);

	ASSERT3U(*nwords, <=, bufsz / sizeof (uint64_t));

	uint64_t n = *nwords;
	uint64_t j = n - 1;
	for (uint64_t i = 0; i < n; i++) {
		uint64_t entry = words[i];
		if (sm_entry_is_double_word(entry)) {
			/*
			 * Since we are populating the buffer backwards
			 * we have to be extra careful and add the two
			 * words of the double-word entry in the right
			 * order.
			 */
			ASSERT3U(j, >, 0);
			buf[j - 1] = entry;

			i++;
			ASSERT3U(i, <, n);
			entry = words[i];
			buf[j] = entry;
			j -= 2;
		} else {
			ASSERT(sm_entry_is_debug(entry) ||
			    sm_entry_is_single_word(entry));
			buf[j] = entry;
			j--;
		}
	}

	/*
	 * Assert that we wrote backwards all the
	 * way to the beginning of the buffer.
	 */
	ASSERT3S(j, ==, -1);

	dmu_buf_rele(db, FTAG);
	return (error);
}

/*
 * Note: This function performs destructive actions - specifically
 * it deletes entries from the end of the space map. Thus, callers
 * should ensure that they are holding the appropriate locks for
 * the space map that they provide.
 */
int
space_map_incremental_destroy(space_map_t *sm, sm_cb_t callback, void *arg,
    dmu_tx_t *tx)
{
	uint64_t bufsz = MAX(sm->sm_blksz, SPA_MINBLOCKSIZE);
	uint64_t *buf = zio_buf_alloc(bufsz);

	dmu_buf_will_dirty(sm->sm_dbuf, tx);

	/*
	 * Ideally we would want to iterate from the beginning of the
	 * space map to the end in incremental steps. The issue with this
	 * approach is that we don't have any field on-disk that points
	 * us where to start between each step. We could try zeroing out
	 * entries that we've destroyed, but this doesn't work either as
	 * an entry that is 0 is a valid one (ALLOC for range [0x0:0x200]).
	 *
	 * As a result, we destroy its entries incrementally starting from
	 * the end after applying the callback to each of them.
	 *
	 * The problem with this approach is that we cannot literally
	 * iterate through the words in the space map backwards as we
	 * can't distinguish two-word space map entries from their second
	 * word. Thus we do the following:
	 *
	 * 1] We get all the entries from the last block of the space map
	 *    and put them into a buffer in reverse order. This way the
	 *    last entry comes first in the buffer, the second to last is
	 *    second, etc.
	 * 2] We iterate through the entries in the buffer and we apply
	 *    the callback to each one. As we move from entry to entry we
	 *    we decrease the size of the space map, deleting effectively
	 *    each entry.
	 * 3] If there are no more entries in the space map or the callback
	 *    returns a value other than 0, we stop iterating over the
	 *    space map. If there are entries remaining and the callback
	 *    returned 0, we go back to step [1].
	 */
	int error = 0;
	while (space_map_length(sm) > 0 && error == 0) {
		uint64_t nwords = 0;
		error = space_map_reversed_last_block_entries(sm, buf, bufsz,
		    &nwords);
		if (error != 0)
			break;

		ASSERT3U(nwords, <=, bufsz / sizeof (uint64_t));

		for (uint64_t i = 0; i < nwords; i++) {
			uint64_t e = buf[i];

			if (sm_entry_is_debug(e)) {
				sm->sm_phys->smp_objsize -= sizeof (uint64_t);
				space_map_update(sm);
				continue;
			}

			int words = 1;
			uint64_t raw_offset, raw_run, vdev_id;
			maptype_t type;
			if (sm_entry_is_single_word(e)) {
				type = SM_TYPE_DECODE(e);
				vdev_id = SM_NO_VDEVID;
				raw_offset = SM_OFFSET_DECODE(e);
				raw_run = SM_RUN_DECODE(e);
			} else {
				ASSERT(sm_entry_is_double_word(e));
				words = 2;

				raw_run = SM2_RUN_DECODE(e);
				vdev_id = SM2_VDEV_DECODE(e);

				/* move to the second word */
				i++;
				e = buf[i];

				ASSERT3P(i, <=, nwords);

				type = SM2_TYPE_DECODE(e);
				raw_offset = SM2_OFFSET_DECODE(e);
			}

			uint64_t entry_offset =
			    (raw_offset << sm->sm_shift) + sm->sm_start;
			uint64_t entry_run = raw_run << sm->sm_shift;

			VERIFY0(P2PHASE(entry_offset, 1ULL << sm->sm_shift));
			VERIFY0(P2PHASE(entry_run, 1ULL << sm->sm_shift));
			VERIFY3U(entry_offset, >=, sm->sm_start);
			VERIFY3U(entry_offset, <, sm->sm_start + sm->sm_size);
			VERIFY3U(entry_run, <=, sm->sm_size);
			VERIFY3U(entry_offset + entry_run, <=,
			    sm->sm_start + sm->sm_size);

			space_map_entry_t sme = {
			    .sme_type = type,
			    .sme_vdev = vdev_id,
			    .sme_offset = entry_offset,
			    .sme_run = entry_run
			};
			error = callback(&sme, arg);
			if (error != 0)
				break;

			if (type == SM_ALLOC)
				sm->sm_phys->smp_alloc -= entry_run;
			else
				sm->sm_phys->smp_alloc += entry_run;
			sm->sm_phys->smp_objsize -= words * sizeof (uint64_t);
			space_map_update(sm);
		}
	}

	if (space_map_length(sm) == 0) {
		ASSERT0(error);
		ASSERT0(sm->sm_phys->smp_objsize);
		ASSERT0(sm->sm_alloc);
	}

	zio_buf_free(buf, bufsz);
	return (error);
}

typedef struct space_map_load_arg {
	space_map_t	*smla_sm;
	range_tree_t	*smla_rt;
	maptype_t	smla_type;
} space_map_load_arg_t;

static int
space_map_load_callback(space_map_entry_t *sme, void *arg)
{
	space_map_load_arg_t *smla = arg;
	if (sme->sme_type == smla->smla_type) {
		VERIFY3U(range_tree_space(smla->smla_rt) + sme->sme_run, <=,
		    smla->smla_sm->sm_size);
		range_tree_add(smla->smla_rt, sme->sme_offset, sme->sme_run);
	} else {
		range_tree_remove(smla->smla_rt, sme->sme_offset, sme->sme_run);
	}

	return (0);
}

/*
 * Load the space map disk into the specified range tree. Segments of maptype
 * are added to the range tree, other segment types are removed.
 */
int
space_map_load(space_map_t *sm, range_tree_t *rt, maptype_t maptype)
{
	uint64_t space;
	int err;
	space_map_load_arg_t smla;

	VERIFY0(range_tree_space(rt));
	space = space_map_allocated(sm);

	if (maptype == SM_FREE) {
		range_tree_add(rt, sm->sm_start, sm->sm_size);
		space = sm->sm_size - space;
	}

	smla.smla_rt = rt;
	smla.smla_sm = sm;
	smla.smla_type = maptype;
	err = space_map_iterate(sm, space_map_load_callback, &smla);

	if (err == 0) {
		VERIFY3U(range_tree_space(rt), ==, space);
	} else {
		range_tree_vacate(rt, NULL, NULL);
	}

	return (err);
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
	/*
	 * Verify that the in-core range tree does not have any
	 * ranges smaller than our sm_shift size.
	 */
	for (int i = 0; i < sm->sm_shift; i++) {
		if (rt->rt_histogram[i] != 0)
			return (B_FALSE);
	}
	return (B_TRUE);
}

void
space_map_histogram_add(space_map_t *sm, range_tree_t *rt, dmu_tx_t *tx)
{
	int idx = 0;

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
	for (int i = sm->sm_shift; i < RANGE_TREE_HISTOGRAM_SIZE; i++) {

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

static void
space_map_write_intro_debug(space_map_t *sm, maptype_t maptype, dmu_tx_t *tx)
{
	dmu_buf_will_dirty(sm->sm_dbuf, tx);

	uint64_t dentry = SM_PREFIX_ENCODE(SM_DEBUG_PREFIX) |
	    SM_DEBUG_ACTION_ENCODE(maptype) |
	    SM_DEBUG_SYNCPASS_ENCODE(spa_sync_pass(tx->tx_pool->dp_spa)) |
	    SM_DEBUG_TXG_ENCODE(dmu_tx_get_txg(tx));

	dmu_write(sm->sm_os, space_map_object(sm), sm->sm_phys->smp_objsize,
	    sizeof (dentry), &dentry, tx);

	sm->sm_phys->smp_objsize += sizeof (dentry);
}

/*
 * Writes one or more entries given a segment.
 *
 * Note: The function may release the dbuf from the pointer initially
 * passed to it, and return a different dbuf. Also, the space map's
 * dbuf must be dirty for the changes in sm_phys to take effect.
 */
static void
space_map_write_seg(space_map_t *sm, range_seg_t *rs, maptype_t maptype,
    uint64_t vdev_id, uint8_t words, dmu_buf_t **dbp, void *tag, dmu_tx_t *tx)
{
	ASSERT3U(words, !=, 0);
	ASSERT3U(words, <=, 2);

	/* ensure the vdev_id can be represented by the space map */
	ASSERT3U(vdev_id, <=, SM_NO_VDEVID);

	/*
	 * if this is a single word entry, ensure that no vdev was
	 * specified.
	 */
	IMPLY(words == 1, vdev_id == SM_NO_VDEVID);

	dmu_buf_t *db = *dbp;
	ASSERT3U(db->db_size, ==, sm->sm_blksz);

	uint64_t *block_base = db->db_data;
	uint64_t *block_end = block_base + (sm->sm_blksz / sizeof (uint64_t));
	uint64_t *block_cursor = block_base +
	    (sm->sm_phys->smp_objsize - db->db_offset) / sizeof (uint64_t);

	ASSERT3P(block_cursor, <=, block_end);

	uint64_t size = (rs->rs_end - rs->rs_start) >> sm->sm_shift;
	uint64_t start = (rs->rs_start - sm->sm_start) >> sm->sm_shift;
	uint64_t run_max = (words == 2) ? SM2_RUN_MAX : SM_RUN_MAX;

	ASSERT3U(rs->rs_start, >=, sm->sm_start);
	ASSERT3U(rs->rs_start, <, sm->sm_start + sm->sm_size);
	ASSERT3U(rs->rs_end - rs->rs_start, <=, sm->sm_size);
	ASSERT3U(rs->rs_end, <=, sm->sm_start + sm->sm_size);

	while (size != 0) {
		ASSERT3P(block_cursor, <=, block_end);

		/*
		 * If we are at the end of this block, flush it and start
		 * writing again from the beginning.
		 */
		if (block_cursor == block_end) {
			dmu_buf_rele(db, tag);

			uint64_t next_word_offset = sm->sm_phys->smp_objsize;
			VERIFY0(dmu_buf_hold(sm->sm_os,
			    space_map_object(sm), next_word_offset,
			    tag, &db, DMU_READ_PREFETCH));
			dmu_buf_will_dirty(db, tx);

			/* update caller's dbuf */
			*dbp = db;

			ASSERT3U(db->db_size, ==, sm->sm_blksz);

			block_base = db->db_data;
			block_cursor = block_base;
			block_end = block_base +
			    (db->db_size / sizeof (uint64_t));
		}

		/*
		 * If we are writing a two-word entry and we only have one
		 * word left on this block, just pad it with an empty debug
		 * entry and write the two-word entry in the next block.
		 */
		uint64_t *next_entry = block_cursor + 1;
		if (next_entry == block_end && words > 1) {
			ASSERT3U(words, ==, 2);
			*block_cursor = SM_PREFIX_ENCODE(SM_DEBUG_PREFIX) |
			    SM_DEBUG_ACTION_ENCODE(0) |
			    SM_DEBUG_SYNCPASS_ENCODE(0) |
			    SM_DEBUG_TXG_ENCODE(0);
			block_cursor++;
			sm->sm_phys->smp_objsize += sizeof (uint64_t);
			ASSERT3P(block_cursor, ==, block_end);
			continue;
		}

		uint64_t run_len = MIN(size, run_max);
		switch (words) {
		case 1:
			*block_cursor = SM_OFFSET_ENCODE(start) |
			    SM_TYPE_ENCODE(maptype) |
			    SM_RUN_ENCODE(run_len);
			block_cursor++;
			break;
		case 2:
			/* write the first word of the entry */
			*block_cursor = SM_PREFIX_ENCODE(SM2_PREFIX) |
			    SM2_RUN_ENCODE(run_len) |
			    SM2_VDEV_ENCODE(vdev_id);
			block_cursor++;

			/* move on to the second word of the entry */
			ASSERT3P(block_cursor, <, block_end);
			*block_cursor = SM2_TYPE_ENCODE(maptype) |
			    SM2_OFFSET_ENCODE(start);
			block_cursor++;
			break;
		default:
			panic("%d-word space map entries are not supported",
			    words);
			break;
		}
		sm->sm_phys->smp_objsize += words * sizeof (uint64_t);

		start += run_len;
		size -= run_len;
	}
	ASSERT0(size);

}

/*
 * Note: The space map's dbuf must be dirty for the changes in sm_phys to
 * take effect.
 */
static void
space_map_write_impl(space_map_t *sm, range_tree_t *rt, maptype_t maptype,
    uint64_t vdev_id, dmu_tx_t *tx)
{
	spa_t *spa = tx->tx_pool->dp_spa;
	dmu_buf_t *db;

	space_map_write_intro_debug(sm, maptype, tx);

#ifdef DEBUG
	/*
	 * We do this right after we write the intro debug entry
	 * because the estimate does not take it into account.
	 */
	uint64_t initial_objsize = sm->sm_phys->smp_objsize;
	uint64_t estimated_growth =
	    space_map_estimate_optimal_size(sm, rt, SM_NO_VDEVID);
	uint64_t estimated_final_objsize = initial_objsize + estimated_growth;
#endif

	/*
	 * Find the offset right after the last word in the space map
	 * and use that to get a hold of the last block, so we can
	 * start appending to it.
	 */
	uint64_t next_word_offset = sm->sm_phys->smp_objsize;
	VERIFY0(dmu_buf_hold(sm->sm_os, space_map_object(sm),
	    next_word_offset, FTAG, &db, DMU_READ_PREFETCH));
	ASSERT3U(db->db_size, ==, sm->sm_blksz);

	dmu_buf_will_dirty(db, tx);

	avl_tree_t *t = &rt->rt_root;
	for (range_seg_t *rs = avl_first(t); rs != NULL; rs = AVL_NEXT(t, rs)) {
		uint64_t offset = (rs->rs_start - sm->sm_start) >> sm->sm_shift;
		uint64_t length = (rs->rs_end - rs->rs_start) >> sm->sm_shift;
		uint8_t words = 1;

		/*
		 * We only write two-word entries when both of the following
		 * are true:
		 *
		 * [1] The feature is enabled.
		 * [2] The offset or run is too big for a single-word entry,
		 *	or the vdev_id is set (meaning not equal to
		 *	SM_NO_VDEVID).
		 *
		 * Note that for purposes of testing we've added the case that
		 * we write two-word entries occasionally when the feature is
		 * enabled and zfs_force_some_double_word_sm_entries has been
		 * set.
		 */
		if (spa_feature_is_active(spa, SPA_FEATURE_SPACEMAP_V2) &&
		    (offset >= (1ULL << SM_OFFSET_BITS) ||
		    length > SM_RUN_MAX ||
		    vdev_id != SM_NO_VDEVID ||
		    (zfs_force_some_double_word_sm_entries &&
		    spa_get_random(100) == 0)))
			words = 2;

		space_map_write_seg(sm, rs, maptype, vdev_id, words,
		    &db, FTAG, tx);
	}

	dmu_buf_rele(db, FTAG);

#ifdef DEBUG
	/*
	 * We expect our estimation to be based on the worst case
	 * scenario [see comment in space_map_estimate_optimal_size()].
	 * Therefore we expect the actual objsize to be equal or less
	 * than whatever we estimated it to be.
	 */
	ASSERT3U(estimated_final_objsize, >=, sm->sm_phys->smp_objsize);
#endif
}

/*
 * Note: This function manipulates the state of the given space map but
 * does not hold any locks implicitly. Thus the caller is responsible
 * for synchronizing writes to the space map.
 */
void
space_map_write(space_map_t *sm, range_tree_t *rt, maptype_t maptype,
    uint64_t vdev_id, dmu_tx_t *tx)
{
	objset_t *os = sm->sm_os;

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(os)));
	VERIFY3U(space_map_object(sm), !=, 0);

	dmu_buf_will_dirty(sm->sm_dbuf, tx);

	/*
	 * This field is no longer necessary since the in-core space map
	 * now contains the object number but is maintained for backwards
	 * compatibility.
	 */
	sm->sm_phys->smp_object = sm->sm_object;

	if (range_tree_is_empty(rt)) {
		VERIFY3U(sm->sm_object, ==, sm->sm_phys->smp_object);
		return;
	}

	if (maptype == SM_ALLOC)
		sm->sm_phys->smp_alloc += range_tree_space(rt);
	else
		sm->sm_phys->smp_alloc -= range_tree_space(rt);

	uint64_t nodes = avl_numnodes(&rt->rt_root);
	uint64_t rt_space = range_tree_space(rt);

	space_map_write_impl(sm, rt, maptype, vdev_id, tx);

	/*
	 * Ensure that the space_map's accounting wasn't changed
	 * while we were in the middle of writing it out.
	 */
	VERIFY3U(nodes, ==, avl_numnodes(&rt->rt_root));
	VERIFY3U(range_tree_space(rt), ==, rt_space);
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
    uint64_t start, uint64_t size, uint8_t shift)
{
	space_map_t *sm;
	int error;

	ASSERT(*smp == NULL);
	ASSERT(os != NULL);
	ASSERT(object != 0);

	sm = kmem_zalloc(sizeof (space_map_t), KM_SLEEP);

	sm->sm_start = start;
	sm->sm_size = size;
	sm->sm_shift = shift;
	sm->sm_os = os;
	sm->sm_object = object;

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
space_map_truncate(space_map_t *sm, int blocksize, dmu_tx_t *tx)
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
	    doi.doi_data_block_size != blocksize ||
	    doi.doi_metadata_block_size != 1 << space_map_ibs) {
		zfs_dbgmsg("txg %llu, spa %s, sm %p, reallocating "
		    "object[%llu]: old bonus %u, old blocksz %u",
		    dmu_tx_get_txg(tx), spa_name(spa), sm, sm->sm_object,
		    doi.doi_bonus_size, doi.doi_data_block_size);

		space_map_free(sm, tx);
		dmu_buf_rele(sm->sm_dbuf, sm);

		sm->sm_object = space_map_alloc(sm->sm_os, blocksize, tx);
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

	sm->sm_alloc = sm->sm_phys->smp_alloc;
	sm->sm_length = sm->sm_phys->smp_objsize;
}

uint64_t
space_map_alloc(objset_t *os, int blocksize, dmu_tx_t *tx)
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

	object = dmu_object_alloc_ibs(os, DMU_OT_SPACE_MAP, blocksize,
	    space_map_ibs, DMU_OT_SPACE_MAP_HEADER, bonuslen, tx);

	return (object);
}

void
space_map_free_obj(objset_t *os, uint64_t smobj, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(os);
	if (spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM)) {
		dmu_object_info_t doi;

		VERIFY0(dmu_object_info(os, smobj, &doi));
		if (doi.doi_bonus_size != SPACE_MAP_SIZE_V0) {
			spa_feature_decr(spa,
			    SPA_FEATURE_SPACEMAP_HISTOGRAM, tx);
		}
	}

	VERIFY0(dmu_object_free(os, smobj, tx));
}

void
space_map_free(space_map_t *sm, dmu_tx_t *tx)
{
	if (sm == NULL)
		return;

	space_map_free_obj(sm->sm_os, space_map_object(sm), tx);
	sm->sm_object = 0;
}

/*
 * Given a range tree, it makes a worst-case estimate of how much
 * space would the tree's segments take if they were written to
 * the given space map.
 */
uint64_t
space_map_estimate_optimal_size(space_map_t *sm, range_tree_t *rt,
    uint64_t vdev_id)
{
	spa_t *spa = dmu_objset_spa(sm->sm_os);
	uint64_t shift = sm->sm_shift;
	uint64_t *histogram = rt->rt_histogram;
	uint64_t entries_for_seg = 0;

	/*
	 * In order to get a quick estimate of the optimal size that this
	 * range tree would have on-disk as a space map, we iterate through
	 * its histogram buckets instead of iterating through its nodes.
	 *
	 * Note that this is a highest-bound/worst-case estimate for the
	 * following reasons:
	 *
	 * 1] We assume that we always add a debug padding for each block
	 *    we write and we also assume that we start at the last word
	 *    of a block attempting to write a two-word entry.
	 * 2] Rounding up errors due to the way segments are distributed
	 *    in the buckets of the range tree's histogram.
	 * 3] The activation of zfs_force_some_double_word_sm_entries
	 *    (tunable) when testing.
	 *
	 * = Math and Rounding Errors =
	 *
	 * rt_histogram[i] bucket of a range tree represents the number
	 * of entries in [2^i, (2^(i+1))-1] of that range_tree. Given
	 * that, we want to divide the buckets into groups: Buckets that
	 * can be represented using a single-word entry, ones that can
	 * be represented with a double-word entry, and ones that can
	 * only be represented with multiple two-word entries.
	 *
	 * [Note that if the new encoding feature is not enabled there
	 * are only two groups: single-word entry buckets and multiple
	 * single-word entry buckets. The information below assumes
	 * two-word entries enabled, but it can easily applied when
	 * the feature is not enabled]
	 *
	 * To find the highest bucket that can be represented with a
	 * single-word entry we look at the maximum run that such entry
	 * can have, which is 2^(SM_RUN_BITS + sm_shift) [remember that
	 * the run of a space map entry is shifted by sm_shift, thus we
	 * add it to the exponent]. This way, excluding the value of the
	 * maximum run that can be represented by a single-word entry,
	 * all runs that are smaller exist in buckets 0 to
	 * SM_RUN_BITS + shift - 1.
	 *
	 * To find the highest bucket that can be represented with a
	 * double-word entry, we follow the same approach. Finally, any
	 * bucket higher than that are represented with multiple two-word
	 * entries. To be more specific, if the highest bucket whose
	 * segments can be represented with a single two-word entry is X,
	 * then bucket X+1 will need 2 two-word entries for each of its
	 * segments, X+2 will need 4, X+3 will need 8, ...etc.
	 *
	 * With all of the above we make our estimation based on bucket
	 * groups. There is a rounding error though. As we mentioned in
	 * the example with the one-word entry, the maximum run that can
	 * be represented in a one-word entry 2^(SM_RUN_BITS + shift) is
	 * not part of bucket SM_RUN_BITS + shift - 1. Thus, segments of
	 * that length fall into the next bucket (and bucket group) where
	 * we start counting two-word entries and this is one more reason
	 * why the estimated size may end up being bigger than the actual
	 * size written.
	 */
	uint64_t size = 0;
	uint64_t idx = 0;

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_V2) ||
	    (vdev_id == SM_NO_VDEVID && sm->sm_size < SM_OFFSET_MAX)) {

		/*
		 * If we are trying to force some double word entries just
		 * assume the worst-case of every single word entry being
		 * written as a double word entry.
		 */
		uint64_t entry_size =
		    (spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_V2) &&
		    zfs_force_some_double_word_sm_entries) ?
		    (2 * sizeof (uint64_t)) : sizeof (uint64_t);

		uint64_t single_entry_max_bucket = SM_RUN_BITS + shift - 1;
		for (; idx <= single_entry_max_bucket; idx++)
			size += histogram[idx] * entry_size;

		if (!spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_V2)) {
			for (; idx < RANGE_TREE_HISTOGRAM_SIZE; idx++) {
				ASSERT3U(idx, >=, single_entry_max_bucket);
				entries_for_seg =
				    1ULL << (idx - single_entry_max_bucket);
				size += histogram[idx] *
				    entries_for_seg * entry_size;
			}
			return (size);
		}
	}

	ASSERT(spa_feature_is_enabled(spa, SPA_FEATURE_SPACEMAP_V2));

	uint64_t double_entry_max_bucket = SM2_RUN_BITS + shift - 1;
	for (; idx <= double_entry_max_bucket; idx++)
		size += histogram[idx] * 2 * sizeof (uint64_t);

	for (; idx < RANGE_TREE_HISTOGRAM_SIZE; idx++) {
		ASSERT3U(idx, >=, double_entry_max_bucket);
		entries_for_seg = 1ULL << (idx - double_entry_max_bucket);
		size += histogram[idx] *
		    entries_for_seg * 2 * sizeof (uint64_t);
	}

	/*
	 * Assume the worst case where we start with the padding at the end
	 * of the current block and we add an extra padding entry at the end
	 * of all subsequent blocks.
	 */
	size += ((size / sm->sm_blksz) + 1) * sizeof (uint64_t);

	return (size);
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
