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
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/arc.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/dsl_dataset.h>
#include <sys/vdev_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/metaslab.h>
#include <sys/trace_zil.h>

/*
 * The zfs intent log (ZIL) saves transaction records of system calls
 * that change the file system in memory with enough information
 * to be able to replay them. These are stored in memory until
 * either the DMU transaction group (txg) commits them to the stable pool
 * and they can be discarded, or they are flushed to the stable log
 * (also in the pool) due to a fsync, O_DSYNC or other synchronous
 * requirement. In the event of a panic or power fail then those log
 * records (transactions) are replayed.
 *
 * There is one ZIL per file system. Its on-disk (pool) format consists
 * of 3 parts:
 *
 * 	- ZIL header
 * 	- ZIL blocks
 * 	- ZIL records
 *
 * A log record holds a system call transaction. Log blocks can
 * hold many log records and the blocks are chained together.
 * Each ZIL block contains a block pointer (blkptr_t) to the next
 * ZIL block in the chain. The ZIL header points to the first
 * block in the chain. Note there is not a fixed place in the pool
 * to hold blocks. They are dynamically allocated and freed as
 * needed from the blocks available. Figure X shows the ZIL structure:
 */

/*
 * See zil.h for more information about these fields.
 */
zil_stats_t zil_stats = {
	{ "zil_commit_count",			KSTAT_DATA_UINT64 },
	{ "zil_commit_writer_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_count",			KSTAT_DATA_UINT64 },
	{ "zil_itx_indirect_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_indirect_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_copied_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_copied_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_needcopy_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_needcopy_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_normal_count",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_normal_bytes",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_slog_count",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_slog_bytes",	KSTAT_DATA_UINT64 },
};

static kstat_t *zil_ksp;

/*
 * Disable intent logging replay.  This global ZIL switch affects all pools.
 */
int zil_replay_disable = 0;

/*
 * Tunable parameter for debugging or performance analysis.  Setting
 * zfs_nocacheflush will cause corruption on power loss if a volatile
 * out-of-order write cache is enabled.
 */
int zfs_nocacheflush = 0;

static kmem_cache_t *zil_lwb_cache;

static void zil_async_to_sync(zilog_t *zilog, uint64_t foid);

#define	LWB_EMPTY(lwb) ((BP_GET_LSIZE(&lwb->lwb_blk) - \
    sizeof (zil_chain_t)) == (lwb->lwb_sz - lwb->lwb_nused))


/*
 * ziltest is by and large an ugly hack, but very useful in
 * checking replay without tedious work.
 * When running ziltest we want to keep all itx's and so maintain
 * a single list in the zl_itxg[] that uses a high txg: ZILTEST_TXG
 * We subtract TXG_CONCURRENT_STATES to allow for common code.
 */
#define	ZILTEST_TXG (UINT64_MAX - TXG_CONCURRENT_STATES)

static int
zil_bp_compare(const void *x1, const void *x2)
{
	const dva_t *dva1 = &((zil_bp_node_t *)x1)->zn_dva;
	const dva_t *dva2 = &((zil_bp_node_t *)x2)->zn_dva;

	if (DVA_GET_VDEV(dva1) < DVA_GET_VDEV(dva2))
		return (-1);
	if (DVA_GET_VDEV(dva1) > DVA_GET_VDEV(dva2))
		return (1);

	if (DVA_GET_OFFSET(dva1) < DVA_GET_OFFSET(dva2))
		return (-1);
	if (DVA_GET_OFFSET(dva1) > DVA_GET_OFFSET(dva2))
		return (1);

	return (0);
}

static void
zil_bp_tree_init(zilog_t *zilog)
{
	avl_create(&zilog->zl_bp_tree, zil_bp_compare,
	    sizeof (zil_bp_node_t), offsetof(zil_bp_node_t, zn_node));
}

static void
zil_bp_tree_fini(zilog_t *zilog)
{
	avl_tree_t *t = &zilog->zl_bp_tree;
	zil_bp_node_t *zn;
	void *cookie = NULL;

	while ((zn = avl_destroy_nodes(t, &cookie)) != NULL)
		kmem_free(zn, sizeof (zil_bp_node_t));

	avl_destroy(t);
}

int
zil_bp_tree_add(zilog_t *zilog, const blkptr_t *bp)
{
	avl_tree_t *t = &zilog->zl_bp_tree;
	const dva_t *dva;
	zil_bp_node_t *zn;
	avl_index_t where;

	if (BP_IS_EMBEDDED(bp))
		return (0);

	dva = BP_IDENTITY(bp);

	if (avl_find(t, dva, &where) != NULL)
		return (SET_ERROR(EEXIST));

	zn = kmem_alloc(sizeof (zil_bp_node_t), KM_SLEEP);
	zn->zn_dva = *dva;
	avl_insert(t, zn, where);

	return (0);
}

static zil_header_t *
zil_header_in_syncing_context(zilog_t *zilog)
{
	return ((zil_header_t *)zilog->zl_header);
}

static void
zil_init_log_chain(zilog_t *zilog, blkptr_t *bp)
{
	zio_cksum_t *zc = &bp->blk_cksum;

	zc->zc_word[ZIL_ZC_GUID_0] = spa_get_random(-1ULL);
	zc->zc_word[ZIL_ZC_GUID_1] = spa_get_random(-1ULL);
	zc->zc_word[ZIL_ZC_OBJSET] = dmu_objset_id(zilog->zl_os);
	zc->zc_word[ZIL_ZC_SEQ] = 1ULL;
}

/*
 * Read a log block and make sure it's valid.
 */
static int
zil_read_log_block(zilog_t *zilog, const blkptr_t *bp, blkptr_t *nbp, void *dst,
    char **end)
{
	enum zio_flag zio_flags = ZIO_FLAG_CANFAIL;
	arc_flags_t aflags = ARC_FLAG_WAIT;
	arc_buf_t *abuf = NULL;
	zbookmark_phys_t zb;
	int error;

	if (zilog->zl_header->zh_claim_txg == 0)
		zio_flags |= ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB;

	if (!(zilog->zl_header->zh_flags & ZIL_CLAIM_LR_SEQ_VALID))
		zio_flags |= ZIO_FLAG_SPECULATIVE;

	SET_BOOKMARK(&zb, bp->blk_cksum.zc_word[ZIL_ZC_OBJSET],
	    ZB_ZIL_OBJECT, ZB_ZIL_LEVEL, bp->blk_cksum.zc_word[ZIL_ZC_SEQ]);

	error = arc_read(NULL, zilog->zl_spa, bp, arc_getbuf_func, &abuf,
	    ZIO_PRIORITY_SYNC_READ, zio_flags, &aflags, &zb);

	if (error == 0) {
		zio_cksum_t cksum = bp->blk_cksum;

		/*
		 * Validate the checksummed log block.
		 *
		 * Sequence numbers should be... sequential.  The checksum
		 * verifier for the next block should be bp's checksum plus 1.
		 *
		 * Also check the log chain linkage and size used.
		 */
		cksum.zc_word[ZIL_ZC_SEQ]++;

		if (BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_ZILOG2) {
			zil_chain_t *zilc = abuf->b_data;
			char *lr = (char *)(zilc + 1);
			uint64_t len = zilc->zc_nused - sizeof (zil_chain_t);

			if (bcmp(&cksum, &zilc->zc_next_blk.blk_cksum,
			    sizeof (cksum)) || BP_IS_HOLE(&zilc->zc_next_blk)) {
				error = SET_ERROR(ECKSUM);
			} else {
				ASSERT3U(len, <=, SPA_OLD_MAXBLOCKSIZE);
				bcopy(lr, dst, len);
				*end = (char *)dst + len;
				*nbp = zilc->zc_next_blk;
			}
		} else {
			char *lr = abuf->b_data;
			uint64_t size = BP_GET_LSIZE(bp);
			zil_chain_t *zilc = (zil_chain_t *)(lr + size) - 1;

			if (bcmp(&cksum, &zilc->zc_next_blk.blk_cksum,
			    sizeof (cksum)) || BP_IS_HOLE(&zilc->zc_next_blk) ||
			    (zilc->zc_nused > (size - sizeof (*zilc)))) {
				error = SET_ERROR(ECKSUM);
			} else {
				ASSERT3U(zilc->zc_nused, <=,
				    SPA_OLD_MAXBLOCKSIZE);
				bcopy(lr, dst, zilc->zc_nused);
				*end = (char *)dst + zilc->zc_nused;
				*nbp = zilc->zc_next_blk;
			}
		}

		VERIFY(arc_buf_remove_ref(abuf, &abuf));
	}

	return (error);
}

/*
 * Read a TX_WRITE log data block.
 */
static int
zil_read_log_data(zilog_t *zilog, const lr_write_t *lr, void *wbuf)
{
	enum zio_flag zio_flags = ZIO_FLAG_CANFAIL;
	const blkptr_t *bp = &lr->lr_blkptr;
	arc_flags_t aflags = ARC_FLAG_WAIT;
	arc_buf_t *abuf = NULL;
	zbookmark_phys_t zb;
	int error;

	if (BP_IS_HOLE(bp)) {
		if (wbuf != NULL)
			bzero(wbuf, MAX(BP_GET_LSIZE(bp), lr->lr_length));
		return (0);
	}

	if (zilog->zl_header->zh_claim_txg == 0)
		zio_flags |= ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB;

	SET_BOOKMARK(&zb, dmu_objset_id(zilog->zl_os), lr->lr_foid,
	    ZB_ZIL_LEVEL, lr->lr_offset / BP_GET_LSIZE(bp));

	error = arc_read(NULL, zilog->zl_spa, bp, arc_getbuf_func, &abuf,
	    ZIO_PRIORITY_SYNC_READ, zio_flags, &aflags, &zb);

	if (error == 0) {
		if (wbuf != NULL)
			bcopy(abuf->b_data, wbuf, arc_buf_size(abuf));
		(void) arc_buf_remove_ref(abuf, &abuf);
	}

	return (error);
}

/*
 * Parse the intent log, and call parse_func for each valid record within.
 */
int
zil_parse(zilog_t *zilog, zil_parse_blk_func_t *parse_blk_func,
    zil_parse_lr_func_t *parse_lr_func, void *arg, uint64_t txg)
{
	const zil_header_t *zh = zilog->zl_header;
	boolean_t claimed = !!zh->zh_claim_txg;
	uint64_t claim_blk_seq = claimed ? zh->zh_claim_blk_seq : UINT64_MAX;
	uint64_t claim_lr_seq = claimed ? zh->zh_claim_lr_seq : UINT64_MAX;
	uint64_t max_blk_seq = 0;
	uint64_t max_lr_seq = 0;
	uint64_t blk_count = 0;
	uint64_t lr_count = 0;
	blkptr_t blk, next_blk;
	char *lrbuf, *lrp;
	int error = 0;

	bzero(&next_blk, sizeof (blkptr_t));

	/*
	 * Old logs didn't record the maximum zh_claim_lr_seq.
	 */
	if (!(zh->zh_flags & ZIL_CLAIM_LR_SEQ_VALID))
		claim_lr_seq = UINT64_MAX;

	/*
	 * Starting at the block pointed to by zh_log we read the log chain.
	 * For each block in the chain we strongly check that block to
	 * ensure its validity.  We stop when an invalid block is found.
	 * For each block pointer in the chain we call parse_blk_func().
	 * For each record in each valid block we call parse_lr_func().
	 * If the log has been claimed, stop if we encounter a sequence
	 * number greater than the highest claimed sequence number.
	 */
	lrbuf = zio_buf_alloc(SPA_OLD_MAXBLOCKSIZE);
	zil_bp_tree_init(zilog);

	for (blk = zh->zh_log; !BP_IS_HOLE(&blk); blk = next_blk) {
		uint64_t blk_seq = blk.blk_cksum.zc_word[ZIL_ZC_SEQ];
		int reclen;
		char *end = NULL;

		if (blk_seq > claim_blk_seq)
			break;
		if ((error = parse_blk_func(zilog, &blk, arg, txg)) != 0)
			break;
		ASSERT3U(max_blk_seq, <, blk_seq);
		max_blk_seq = blk_seq;
		blk_count++;

		if (max_lr_seq == claim_lr_seq && max_blk_seq == claim_blk_seq)
			break;

		error = zil_read_log_block(zilog, &blk, &next_blk, lrbuf, &end);
		if (error != 0)
			break;

		for (lrp = lrbuf; lrp < end; lrp += reclen) {
			lr_t *lr = (lr_t *)lrp;
			reclen = lr->lrc_reclen;
			ASSERT3U(reclen, >=, sizeof (lr_t));
			if (lr->lrc_seq > claim_lr_seq)
				goto done;
			if ((error = parse_lr_func(zilog, lr, arg, txg)) != 0)
				goto done;
			ASSERT3U(max_lr_seq, <, lr->lrc_seq);
			max_lr_seq = lr->lrc_seq;
			lr_count++;
		}
	}
done:
	zilog->zl_parse_error = error;
	zilog->zl_parse_blk_seq = max_blk_seq;
	zilog->zl_parse_lr_seq = max_lr_seq;
	zilog->zl_parse_blk_count = blk_count;
	zilog->zl_parse_lr_count = lr_count;

	ASSERT(!claimed || !(zh->zh_flags & ZIL_CLAIM_LR_SEQ_VALID) ||
	    (max_blk_seq == claim_blk_seq && max_lr_seq == claim_lr_seq));

	zil_bp_tree_fini(zilog);
	zio_buf_free(lrbuf, SPA_OLD_MAXBLOCKSIZE);

	return (error);
}

static int
zil_claim_log_block(zilog_t *zilog, blkptr_t *bp, void *tx, uint64_t first_txg)
{
	/*
	 * Claim log block if not already committed and not already claimed.
	 * If tx == NULL, just verify that the block is claimable.
	 */
	if (BP_IS_HOLE(bp) || bp->blk_birth < first_txg ||
	    zil_bp_tree_add(zilog, bp) != 0)
		return (0);

	return (zio_wait(zio_claim(NULL, zilog->zl_spa,
	    tx == NULL ? 0 : first_txg, bp, spa_claim_notify, NULL,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB)));
}

static int
zil_claim_log_record(zilog_t *zilog, lr_t *lrc, void *tx, uint64_t first_txg)
{
	lr_write_t *lr = (lr_write_t *)lrc;
	int error;

	if (lrc->lrc_txtype != TX_WRITE)
		return (0);

	/*
	 * If the block is not readable, don't claim it.  This can happen
	 * in normal operation when a log block is written to disk before
	 * some of the dmu_sync() blocks it points to.  In this case, the
	 * transaction cannot have been committed to anyone (we would have
	 * waited for all writes to be stable first), so it is semantically
	 * correct to declare this the end of the log.
	 */
	if (lr->lr_blkptr.blk_birth >= first_txg &&
	    (error = zil_read_log_data(zilog, lr, NULL)) != 0)
		return (error);
	return (zil_claim_log_block(zilog, &lr->lr_blkptr, tx, first_txg));
}

/* ARGSUSED */
static int
zil_free_log_block(zilog_t *zilog, blkptr_t *bp, void *tx, uint64_t claim_txg)
{
	zio_free_zil(zilog->zl_spa, dmu_tx_get_txg(tx), bp);

	return (0);
}

static int
zil_free_log_record(zilog_t *zilog, lr_t *lrc, void *tx, uint64_t claim_txg)
{
	lr_write_t *lr = (lr_write_t *)lrc;
	blkptr_t *bp = &lr->lr_blkptr;

	/*
	 * If we previously claimed it, we need to free it.
	 */
	if (claim_txg != 0 && lrc->lrc_txtype == TX_WRITE &&
	    bp->blk_birth >= claim_txg && zil_bp_tree_add(zilog, bp) == 0 &&
	    !BP_IS_HOLE(bp))
		zio_free(zilog->zl_spa, dmu_tx_get_txg(tx), bp);

	return (0);
}

static lwb_t *
zil_alloc_lwb(zilog_t *zilog, blkptr_t *bp, uint64_t txg, boolean_t fastwrite)
{
	lwb_t *lwb;

	lwb = kmem_cache_alloc(zil_lwb_cache, KM_SLEEP);
	lwb->lwb_zilog = zilog;
	lwb->lwb_blk = *bp;
	lwb->lwb_fastwrite = fastwrite;
	lwb->lwb_buf = zio_buf_alloc(BP_GET_LSIZE(bp));
	lwb->lwb_max_txg = txg;
	lwb->lwb_zio = NULL;
	lwb->lwb_tx = NULL;
	if (BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_ZILOG2) {
		lwb->lwb_nused = sizeof (zil_chain_t);
		lwb->lwb_sz = BP_GET_LSIZE(bp);
	} else {
		lwb->lwb_nused = 0;
		lwb->lwb_sz = BP_GET_LSIZE(bp) - sizeof (zil_chain_t);
	}

	mutex_enter(&zilog->zl_lock);
	list_insert_tail(&zilog->zl_lwb_list, lwb);
	mutex_exit(&zilog->zl_lock);

	return (lwb);
}

/*
 * Called when we create in-memory log transactions so that we know
 * to cleanup the itxs at the end of spa_sync().
 */
void
zilog_dirty(zilog_t *zilog, uint64_t txg)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;
	dsl_dataset_t *ds = dmu_objset_ds(zilog->zl_os);

	if (ds->ds_is_snapshot)
		panic("dirtying snapshot!");

	if (txg_list_add(&dp->dp_dirty_zilogs, zilog, txg)) {
		/* up the hold count until we can be written out */
		dmu_buf_add_ref(ds->ds_dbuf, zilog);
	}
}

boolean_t
zilog_is_dirty(zilog_t *zilog)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;
	int t;

	for (t = 0; t < TXG_SIZE; t++) {
		if (txg_list_member(&dp->dp_dirty_zilogs, zilog, t))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Create an on-disk intent log.
 */
static lwb_t *
zil_create(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;
	lwb_t *lwb = NULL;
	uint64_t txg = 0;
	dmu_tx_t *tx = NULL;
	blkptr_t blk;
	int error = 0;
	boolean_t fastwrite = FALSE;

	/*
	 * Wait for any previous destroy to complete.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);

	ASSERT(zh->zh_claim_txg == 0);
	ASSERT(zh->zh_replay_seq == 0);

	blk = zh->zh_log;

	/*
	 * Allocate an initial log block if:
	 *    - there isn't one already
	 *    - the existing block is the wrong endianess
	 */
	if (BP_IS_HOLE(&blk) || BP_SHOULD_BYTESWAP(&blk)) {
		tx = dmu_tx_create(zilog->zl_os);
		VERIFY(dmu_tx_assign(tx, TXG_WAIT) == 0);
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		txg = dmu_tx_get_txg(tx);

		if (!BP_IS_HOLE(&blk)) {
			zio_free_zil(zilog->zl_spa, txg, &blk);
			BP_ZERO(&blk);
		}

		error = zio_alloc_zil(zilog->zl_spa, txg, &blk,
		    ZIL_MIN_BLKSZ, B_TRUE);
		fastwrite = TRUE;

		if (error == 0)
			zil_init_log_chain(zilog, &blk);
	}

	/*
	 * Allocate a log write buffer (lwb) for the first log block.
	 */
	if (error == 0)
		lwb = zil_alloc_lwb(zilog, &blk, txg, fastwrite);

	/*
	 * If we just allocated the first log block, commit our transaction
	 * and wait for zil_sync() to stuff the block poiner into zh_log.
	 * (zh is part of the MOS, so we cannot modify it in open context.)
	 */
	if (tx != NULL) {
		dmu_tx_commit(tx);
		txg_wait_synced(zilog->zl_dmu_pool, txg);
	}

	ASSERT(bcmp(&blk, &zh->zh_log, sizeof (blk)) == 0);

	return (lwb);
}

/*
 * In one tx, free all log blocks and clear the log header.
 * If keep_first is set, then we're replaying a log with no content.
 * We want to keep the first block, however, so that the first
 * synchronous transaction doesn't require a txg_wait_synced()
 * in zil_create().  We don't need to txg_wait_synced() here either
 * when keep_first is set, because both zil_create() and zil_destroy()
 * will wait for any in-progress destroys to complete.
 */
void
zil_destroy(zilog_t *zilog, boolean_t keep_first)
{
	const zil_header_t *zh = zilog->zl_header;
	lwb_t *lwb;
	dmu_tx_t *tx;
	uint64_t txg;

	/*
	 * Wait for any previous destroy to complete.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);

	zilog->zl_old_header = *zh;		/* debugging aid */

	if (BP_IS_HOLE(&zh->zh_log))
		return;

	tx = dmu_tx_create(zilog->zl_os);
	VERIFY(dmu_tx_assign(tx, TXG_WAIT) == 0);
	dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
	txg = dmu_tx_get_txg(tx);

	mutex_enter(&zilog->zl_lock);

	ASSERT3U(zilog->zl_destroy_txg, <, txg);
	zilog->zl_destroy_txg = txg;
	zilog->zl_keep_first = keep_first;

	if (!list_is_empty(&zilog->zl_lwb_list)) {
		ASSERT(zh->zh_claim_txg == 0);
		VERIFY(!keep_first);
		while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL) {
			ASSERT(lwb->lwb_zio == NULL);
			if (lwb->lwb_fastwrite)
				metaslab_fastwrite_unmark(zilog->zl_spa,
				    &lwb->lwb_blk);
			list_remove(&zilog->zl_lwb_list, lwb);
			if (lwb->lwb_buf != NULL)
				zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
			zio_free_zil(zilog->zl_spa, txg, &lwb->lwb_blk);
			kmem_cache_free(zil_lwb_cache, lwb);
		}
	} else if (!keep_first) {
		zil_destroy_sync(zilog, tx);
	}
	mutex_exit(&zilog->zl_lock);

	dmu_tx_commit(tx);
}

void
zil_destroy_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	ASSERT(list_is_empty(&zilog->zl_lwb_list));
	(void) zil_parse(zilog, zil_free_log_block,
	    zil_free_log_record, tx, zilog->zl_header->zh_claim_txg);
}

int
zil_claim(dsl_pool_t *dp, dsl_dataset_t *ds, void *txarg)
{
	dmu_tx_t *tx = txarg;
	uint64_t first_txg = dmu_tx_get_txg(tx);
	zilog_t *zilog;
	zil_header_t *zh;
	objset_t *os;
	int error;

	error = dmu_objset_own_obj(dp, ds->ds_object,
	    DMU_OST_ANY, B_FALSE, FTAG, &os);
	if (error != 0) {
		/*
		 * EBUSY indicates that the objset is inconsistent, in which
		 * case it can not have a ZIL.
		 */
		if (error != EBUSY) {
			cmn_err(CE_WARN, "can't open objset for %llu, error %u",
			    (unsigned long long)ds->ds_object, error);
		}

		return (0);
	}

	zilog = dmu_objset_zil(os);
	zh = zil_header_in_syncing_context(zilog);

	if (spa_get_log_state(zilog->zl_spa) == SPA_LOG_CLEAR) {
		if (!BP_IS_HOLE(&zh->zh_log))
			zio_free_zil(zilog->zl_spa, first_txg, &zh->zh_log);
		BP_ZERO(&zh->zh_log);
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
		dmu_objset_disown(os, FTAG);
		return (0);
	}

	/*
	 * Claim all log blocks if we haven't already done so, and remember
	 * the highest claimed sequence number.  This ensures that if we can
	 * read only part of the log now (e.g. due to a missing device),
	 * but we can read the entire log later, we will not try to replay
	 * or destroy beyond the last block we successfully claimed.
	 */
	ASSERT3U(zh->zh_claim_txg, <=, first_txg);
	if (zh->zh_claim_txg == 0 && !BP_IS_HOLE(&zh->zh_log)) {
		(void) zil_parse(zilog, zil_claim_log_block,
		    zil_claim_log_record, tx, first_txg);
		zh->zh_claim_txg = first_txg;
		zh->zh_claim_blk_seq = zilog->zl_parse_blk_seq;
		zh->zh_claim_lr_seq = zilog->zl_parse_lr_seq;
		if (zilog->zl_parse_lr_count || zilog->zl_parse_blk_count > 1)
			zh->zh_flags |= ZIL_REPLAY_NEEDED;
		zh->zh_flags |= ZIL_CLAIM_LR_SEQ_VALID;
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
	}

	ASSERT3U(first_txg, ==, (spa_last_synced_txg(zilog->zl_spa) + 1));
	dmu_objset_disown(os, FTAG);
	return (0);
}

/*
 * Check the log by walking the log chain.
 * Checksum errors are ok as they indicate the end of the chain.
 * Any other error (no device or read failure) returns an error.
 */
/* ARGSUSED */
int
zil_check_log_chain(dsl_pool_t *dp, dsl_dataset_t *ds, void *tx)
{
	zilog_t *zilog;
	objset_t *os;
	blkptr_t *bp;
	int error;

	ASSERT(tx == NULL);

	error = dmu_objset_from_ds(ds, &os);
	if (error != 0) {
		cmn_err(CE_WARN, "can't open objset %llu, error %d",
		    (unsigned long long)ds->ds_object, error);
		return (0);
	}

	zilog = dmu_objset_zil(os);
	bp = (blkptr_t *)&zilog->zl_header->zh_log;

	/*
	 * Check the first block and determine if it's on a log device
	 * which may have been removed or faulted prior to loading this
	 * pool.  If so, there's no point in checking the rest of the log
	 * as its content should have already been synced to the pool.
	 */
	if (!BP_IS_HOLE(bp)) {
		vdev_t *vd;
		boolean_t valid = B_TRUE;

		spa_config_enter(os->os_spa, SCL_STATE, FTAG, RW_READER);
		vd = vdev_lookup_top(os->os_spa, DVA_GET_VDEV(&bp->blk_dva[0]));
		if (vd->vdev_islog && vdev_is_dead(vd))
			valid = vdev_log_state_valid(vd);
		spa_config_exit(os->os_spa, SCL_STATE, FTAG);

		if (!valid)
			return (0);
	}

	/*
	 * Because tx == NULL, zil_claim_log_block() will not actually claim
	 * any blocks, but just determine whether it is possible to do so.
	 * In addition to checking the log chain, zil_claim_log_block()
	 * will invoke zio_claim() with a done func of spa_claim_notify(),
	 * which will update spa_max_claim_txg.  See spa_load() for details.
	 */
	error = zil_parse(zilog, zil_claim_log_block, zil_claim_log_record, tx,
	    zilog->zl_header->zh_claim_txg ? -1ULL : spa_first_txg(os->os_spa));

	return ((error == ECKSUM || error == ENOENT) ? 0 : error);
}

static int
zil_vdev_compare(const void *x1, const void *x2)
{
	const uint64_t v1 = ((zil_vdev_node_t *)x1)->zv_vdev;
	const uint64_t v2 = ((zil_vdev_node_t *)x2)->zv_vdev;

	if (v1 < v2)
		return (-1);
	if (v1 > v2)
		return (1);

	return (0);
}

void
zil_add_block(zilog_t *zilog, const blkptr_t *bp)
{
	avl_tree_t *t = &zilog->zl_vdev_tree;
	avl_index_t where;
	zil_vdev_node_t *zv, zvsearch;
	int ndvas = BP_GET_NDVAS(bp);
	int i;

	if (zfs_nocacheflush)
		return;

	ASSERT(zilog->zl_writer);

	/*
	 * Even though we're zl_writer, we still need a lock because the
	 * zl_get_data() callbacks may have dmu_sync() done callbacks
	 * that will run concurrently.
	 */
	mutex_enter(&zilog->zl_vdev_lock);
	for (i = 0; i < ndvas; i++) {
		zvsearch.zv_vdev = DVA_GET_VDEV(&bp->blk_dva[i]);
		if (avl_find(t, &zvsearch, &where) == NULL) {
			zv = kmem_alloc(sizeof (*zv), KM_SLEEP);
			zv->zv_vdev = zvsearch.zv_vdev;
			avl_insert(t, zv, where);
		}
	}
	mutex_exit(&zilog->zl_vdev_lock);
}

static void
zil_flush_vdevs(zilog_t *zilog)
{
	spa_t *spa = zilog->zl_spa;
	avl_tree_t *t = &zilog->zl_vdev_tree;
	void *cookie = NULL;
	zil_vdev_node_t *zv;
	zio_t *zio;

	ASSERT(zilog->zl_writer);

	/*
	 * We don't need zl_vdev_lock here because we're the zl_writer,
	 * and all zl_get_data() callbacks are done.
	 */
	if (avl_numnodes(t) == 0)
		return;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

	while ((zv = avl_destroy_nodes(t, &cookie)) != NULL) {
		vdev_t *vd = vdev_lookup_top(spa, zv->zv_vdev);
		if (vd != NULL)
			zio_flush(zio, vd);
		kmem_free(zv, sizeof (*zv));
	}

	/*
	 * Wait for all the flushes to complete.  Not all devices actually
	 * support the DKIOCFLUSHWRITECACHE ioctl, so it's OK if it fails.
	 */
	(void) zio_wait(zio);

	spa_config_exit(spa, SCL_STATE, FTAG);
}

/*
 * Function called when a log block write completes
 */
static void
zil_lwb_write_done(zio_t *zio)
{
	lwb_t *lwb = zio->io_private;
	zilog_t *zilog = lwb->lwb_zilog;
	dmu_tx_t *tx = lwb->lwb_tx;

	ASSERT(BP_GET_COMPRESS(zio->io_bp) == ZIO_COMPRESS_OFF);
	ASSERT(BP_GET_TYPE(zio->io_bp) == DMU_OT_INTENT_LOG);
	ASSERT(BP_GET_LEVEL(zio->io_bp) == 0);
	ASSERT(BP_GET_BYTEORDER(zio->io_bp) == ZFS_HOST_BYTEORDER);
	ASSERT(!BP_IS_GANG(zio->io_bp));
	ASSERT(!BP_IS_HOLE(zio->io_bp));
	ASSERT(BP_GET_FILL(zio->io_bp) == 0);

	/*
	 * Ensure the lwb buffer pointer is cleared before releasing
	 * the txg. If we have had an allocation failure and
	 * the txg is waiting to sync then we want want zil_sync()
	 * to remove the lwb so that it's not picked up as the next new
	 * one in zil_commit_writer(). zil_sync() will only remove
	 * the lwb if lwb_buf is null.
	 */
	zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
	mutex_enter(&zilog->zl_lock);
	lwb->lwb_zio = NULL;
	lwb->lwb_fastwrite = FALSE;
	lwb->lwb_buf = NULL;
	lwb->lwb_tx = NULL;
	mutex_exit(&zilog->zl_lock);

	/*
	 * Now that we've written this log block, we have a stable pointer
	 * to the next block in the chain, so it's OK to let the txg in
	 * which we allocated the next block sync.
	 */
	dmu_tx_commit(tx);
}

/*
 * Initialize the io for a log block.
 */
static void
zil_lwb_write_init(zilog_t *zilog, lwb_t *lwb)
{
	zbookmark_phys_t zb;

	SET_BOOKMARK(&zb, lwb->lwb_blk.blk_cksum.zc_word[ZIL_ZC_OBJSET],
	    ZB_ZIL_OBJECT, ZB_ZIL_LEVEL,
	    lwb->lwb_blk.blk_cksum.zc_word[ZIL_ZC_SEQ]);

	if (zilog->zl_root_zio == NULL) {
		zilog->zl_root_zio = zio_root(zilog->zl_spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL);
	}

	/* Lock so zil_sync() doesn't fastwrite_unmark after zio is created */
	mutex_enter(&zilog->zl_lock);
	if (lwb->lwb_zio == NULL) {
		if (!lwb->lwb_fastwrite) {
			metaslab_fastwrite_mark(zilog->zl_spa, &lwb->lwb_blk);
			lwb->lwb_fastwrite = 1;
		}
		lwb->lwb_zio = zio_rewrite(zilog->zl_root_zio, zilog->zl_spa,
		    0, &lwb->lwb_blk, lwb->lwb_buf, BP_GET_LSIZE(&lwb->lwb_blk),
		    zil_lwb_write_done, lwb, ZIO_PRIORITY_SYNC_WRITE,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_DONT_PROPAGATE |
		    ZIO_FLAG_FASTWRITE, &zb);
	}
	mutex_exit(&zilog->zl_lock);
}

/*
 * Define a limited set of intent log block sizes.
 *
 * These must be a multiple of 4KB. Note only the amount used (again
 * aligned to 4KB) actually gets written. However, we can't always just
 * allocate SPA_OLD_MAXBLOCKSIZE as the slog space could be exhausted.
 */
uint64_t zil_block_buckets[] = {
    4096,		/* non TX_WRITE */
    8192+4096,		/* data base */
    32*1024 + 4096, 	/* NFS writes */
    UINT64_MAX
};

/*
 * Use the slog as long as the current commit size is less than the
 * limit or the total list size is less than 2X the limit.  Limit
 * checking is disabled by setting zil_slog_limit to UINT64_MAX.
 */
unsigned long zil_slog_limit = 1024 * 1024;
#define	USE_SLOG(zilog) (((zilog)->zl_cur_used < zil_slog_limit) || \
	((zilog)->zl_itx_list_sz < (zil_slog_limit << 1)))

/*
 * Start a log block write and advance to the next log block.
 * Calls are serialized.
 */
static lwb_t *
zil_lwb_write_start(zilog_t *zilog, lwb_t *lwb)
{
	lwb_t *nlwb = NULL;
	zil_chain_t *zilc;
	spa_t *spa = zilog->zl_spa;
	blkptr_t *bp;
	dmu_tx_t *tx;
	uint64_t txg;
	uint64_t zil_blksz, wsz;
	int i, error;
	boolean_t use_slog;

	if (BP_GET_CHECKSUM(&lwb->lwb_blk) == ZIO_CHECKSUM_ZILOG2) {
		zilc = (zil_chain_t *)lwb->lwb_buf;
		bp = &zilc->zc_next_blk;
	} else {
		zilc = (zil_chain_t *)(lwb->lwb_buf + lwb->lwb_sz);
		bp = &zilc->zc_next_blk;
	}

	ASSERT(lwb->lwb_nused <= lwb->lwb_sz);

	/*
	 * Allocate the next block and save its address in this block
	 * before writing it in order to establish the log chain.
	 * Note that if the allocation of nlwb synced before we wrote
	 * the block that points at it (lwb), we'd leak it if we crashed.
	 * Therefore, we don't do dmu_tx_commit() until zil_lwb_write_done().
	 * We dirty the dataset to ensure that zil_sync() will be called
	 * to clean up in the event of allocation failure or I/O failure.
	 */
	tx = dmu_tx_create(zilog->zl_os);
	VERIFY(dmu_tx_assign(tx, TXG_WAIT) == 0);
	dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
	txg = dmu_tx_get_txg(tx);

	lwb->lwb_tx = tx;

	/*
	 * Log blocks are pre-allocated. Here we select the size of the next
	 * block, based on size used in the last block.
	 * - first find the smallest bucket that will fit the block from a
	 *   limited set of block sizes. This is because it's faster to write
	 *   blocks allocated from the same metaslab as they are adjacent or
	 *   close.
	 * - next find the maximum from the new suggested size and an array of
	 *   previous sizes. This lessens a picket fence effect of wrongly
	 *   guesssing the size if we have a stream of say 2k, 64k, 2k, 64k
	 *   requests.
	 *
	 * Note we only write what is used, but we can't just allocate
	 * the maximum block size because we can exhaust the available
	 * pool log space.
	 */
	zil_blksz = zilog->zl_cur_used + sizeof (zil_chain_t);
	for (i = 0; zil_blksz > zil_block_buckets[i]; i++)
		continue;
	zil_blksz = zil_block_buckets[i];
	if (zil_blksz == UINT64_MAX)
		zil_blksz = SPA_OLD_MAXBLOCKSIZE;
	zilog->zl_prev_blks[zilog->zl_prev_rotor] = zil_blksz;
	for (i = 0; i < ZIL_PREV_BLKS; i++)
		zil_blksz = MAX(zil_blksz, zilog->zl_prev_blks[i]);
	zilog->zl_prev_rotor = (zilog->zl_prev_rotor + 1) & (ZIL_PREV_BLKS - 1);

	BP_ZERO(bp);
	use_slog = USE_SLOG(zilog);
	error = zio_alloc_zil(spa, txg, bp, zil_blksz,
	    USE_SLOG(zilog));
	if (use_slog) {
		ZIL_STAT_BUMP(zil_itx_metaslab_slog_count);
		ZIL_STAT_INCR(zil_itx_metaslab_slog_bytes, lwb->lwb_nused);
	} else {
		ZIL_STAT_BUMP(zil_itx_metaslab_normal_count);
		ZIL_STAT_INCR(zil_itx_metaslab_normal_bytes, lwb->lwb_nused);
	}
	if (error == 0) {
		ASSERT3U(bp->blk_birth, ==, txg);
		bp->blk_cksum = lwb->lwb_blk.blk_cksum;
		bp->blk_cksum.zc_word[ZIL_ZC_SEQ]++;

		/*
		 * Allocate a new log write buffer (lwb).
		 */
		nlwb = zil_alloc_lwb(zilog, bp, txg, TRUE);

		/* Record the block for later vdev flushing */
		zil_add_block(zilog, &lwb->lwb_blk);
	}

	if (BP_GET_CHECKSUM(&lwb->lwb_blk) == ZIO_CHECKSUM_ZILOG2) {
		/* For Slim ZIL only write what is used. */
		wsz = P2ROUNDUP_TYPED(lwb->lwb_nused, ZIL_MIN_BLKSZ, uint64_t);
		ASSERT3U(wsz, <=, lwb->lwb_sz);
		zio_shrink(lwb->lwb_zio, wsz);

	} else {
		wsz = lwb->lwb_sz;
	}

	zilc->zc_pad = 0;
	zilc->zc_nused = lwb->lwb_nused;
	zilc->zc_eck.zec_cksum = lwb->lwb_blk.blk_cksum;

	/*
	 * clear unused data for security
	 */
	bzero(lwb->lwb_buf + lwb->lwb_nused, wsz - lwb->lwb_nused);

	zio_nowait(lwb->lwb_zio); /* Kick off the write for the old log block */

	/*
	 * If there was an allocation failure then nlwb will be null which
	 * forces a txg_wait_synced().
	 */
	return (nlwb);
}

static lwb_t *
zil_lwb_commit(zilog_t *zilog, itx_t *itx, lwb_t *lwb)
{
	lr_t *lrc = &itx->itx_lr; /* common log record */
	lr_write_t *lrw = (lr_write_t *)lrc;
	char *lr_buf;
	uint64_t txg = lrc->lrc_txg;
	uint64_t reclen = lrc->lrc_reclen;
	uint64_t dlen = 0;

	if (lwb == NULL)
		return (NULL);

	ASSERT(lwb->lwb_buf != NULL);
	ASSERT(zilog_is_dirty(zilog) ||
	    spa_freeze_txg(zilog->zl_spa) != UINT64_MAX);

	if (lrc->lrc_txtype == TX_WRITE && itx->itx_wr_state == WR_NEED_COPY)
		dlen = P2ROUNDUP_TYPED(
		    lrw->lr_length, sizeof (uint64_t), uint64_t);

	zilog->zl_cur_used += (reclen + dlen);

	zil_lwb_write_init(zilog, lwb);

	/*
	 * If this record won't fit in the current log block, start a new one.
	 */
	if (lwb->lwb_nused + reclen + dlen > lwb->lwb_sz) {
		lwb = zil_lwb_write_start(zilog, lwb);
		if (lwb == NULL)
			return (NULL);
		zil_lwb_write_init(zilog, lwb);
		ASSERT(LWB_EMPTY(lwb));
		if (lwb->lwb_nused + reclen + dlen > lwb->lwb_sz) {
			txg_wait_synced(zilog->zl_dmu_pool, txg);
			return (lwb);
		}
	}

	lr_buf = lwb->lwb_buf + lwb->lwb_nused;
	bcopy(lrc, lr_buf, reclen);
	lrc = (lr_t *)lr_buf;
	lrw = (lr_write_t *)lrc;

	ZIL_STAT_BUMP(zil_itx_count);

	/*
	 * If it's a write, fetch the data or get its blkptr as appropriate.
	 */
	if (lrc->lrc_txtype == TX_WRITE) {
		if (txg > spa_freeze_txg(zilog->zl_spa))
			txg_wait_synced(zilog->zl_dmu_pool, txg);
		if (itx->itx_wr_state == WR_COPIED) {
			ZIL_STAT_BUMP(zil_itx_copied_count);
			ZIL_STAT_INCR(zil_itx_copied_bytes, lrw->lr_length);
		} else {
			char *dbuf;
			int error;

			if (dlen) {
				ASSERT(itx->itx_wr_state == WR_NEED_COPY);
				dbuf = lr_buf + reclen;
				lrw->lr_common.lrc_reclen += dlen;
				ZIL_STAT_BUMP(zil_itx_needcopy_count);
				ZIL_STAT_INCR(zil_itx_needcopy_bytes,
				    lrw->lr_length);
			} else {
				ASSERT(itx->itx_wr_state == WR_INDIRECT);
				dbuf = NULL;
				ZIL_STAT_BUMP(zil_itx_indirect_count);
				ZIL_STAT_INCR(zil_itx_indirect_bytes,
				    lrw->lr_length);
			}
			error = zilog->zl_get_data(
			    itx->itx_private, lrw, dbuf, lwb->lwb_zio);
			if (error == EIO) {
				txg_wait_synced(zilog->zl_dmu_pool, txg);
				return (lwb);
			}
			if (error != 0) {
				ASSERT(error == ENOENT || error == EEXIST ||
				    error == EALREADY);
				return (lwb);
			}
		}
	}

	/*
	 * We're actually making an entry, so update lrc_seq to be the
	 * log record sequence number.  Note that this is generally not
	 * equal to the itx sequence number because not all transactions
	 * are synchronous, and sometimes spa_sync() gets there first.
	 */
	lrc->lrc_seq = ++zilog->zl_lr_seq; /* we are single threaded */
	lwb->lwb_nused += reclen + dlen;
	lwb->lwb_max_txg = MAX(lwb->lwb_max_txg, txg);
	ASSERT3U(lwb->lwb_nused, <=, lwb->lwb_sz);
	ASSERT0(P2PHASE(lwb->lwb_nused, sizeof (uint64_t)));

	return (lwb);
}

itx_t *
zil_itx_create(uint64_t txtype, size_t lrsize)
{
	itx_t *itx;

	lrsize = P2ROUNDUP_TYPED(lrsize, sizeof (uint64_t), size_t);

	itx = zio_data_buf_alloc(offsetof(itx_t, itx_lr) + lrsize);
	itx->itx_lr.lrc_txtype = txtype;
	itx->itx_lr.lrc_reclen = lrsize;
	itx->itx_sod = lrsize; /* if write & WR_NEED_COPY will be increased */
	itx->itx_lr.lrc_seq = 0;	/* defensive */
	itx->itx_sync = B_TRUE;		/* default is synchronous */
	itx->itx_callback = NULL;
	itx->itx_callback_data = NULL;

	return (itx);
}

void
zil_itx_destroy(itx_t *itx)
{
	zio_data_buf_free(itx, offsetof(itx_t, itx_lr)+itx->itx_lr.lrc_reclen);
}

/*
 * Free up the sync and async itxs. The itxs_t has already been detached
 * so no locks are needed.
 */
static void
zil_itxg_clean(itxs_t *itxs)
{
	itx_t *itx;
	list_t *list;
	avl_tree_t *t;
	void *cookie;
	itx_async_node_t *ian;

	list = &itxs->i_sync_list;
	while ((itx = list_head(list)) != NULL) {
		if (itx->itx_callback != NULL)
			itx->itx_callback(itx->itx_callback_data);
		list_remove(list, itx);
		zil_itx_destroy(itx);
	}

	cookie = NULL;
	t = &itxs->i_async_tree;
	while ((ian = avl_destroy_nodes(t, &cookie)) != NULL) {
		list = &ian->ia_list;
		while ((itx = list_head(list)) != NULL) {
			if (itx->itx_callback != NULL)
				itx->itx_callback(itx->itx_callback_data);
			list_remove(list, itx);
			zil_itx_destroy(itx);
		}
		list_destroy(list);
		kmem_free(ian, sizeof (itx_async_node_t));
	}
	avl_destroy(t);

	kmem_free(itxs, sizeof (itxs_t));
}

static int
zil_aitx_compare(const void *x1, const void *x2)
{
	const uint64_t o1 = ((itx_async_node_t *)x1)->ia_foid;
	const uint64_t o2 = ((itx_async_node_t *)x2)->ia_foid;

	if (o1 < o2)
		return (-1);
	if (o1 > o2)
		return (1);

	return (0);
}

/*
 * Remove all async itx with the given oid.
 */
static void
zil_remove_async(zilog_t *zilog, uint64_t oid)
{
	uint64_t otxg, txg;
	itx_async_node_t *ian;
	avl_tree_t *t;
	avl_index_t where;
	list_t clean_list;
	itx_t *itx;

	ASSERT(oid != 0);
	list_create(&clean_list, sizeof (itx_t), offsetof(itx_t, itx_node));

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * Locate the object node and append its list.
		 */
		t = &itxg->itxg_itxs->i_async_tree;
		ian = avl_find(t, &oid, &where);
		if (ian != NULL)
			list_move_tail(&clean_list, &ian->ia_list);
		mutex_exit(&itxg->itxg_lock);
	}
	while ((itx = list_head(&clean_list)) != NULL) {
		if (itx->itx_callback != NULL)
			itx->itx_callback(itx->itx_callback_data);
		list_remove(&clean_list, itx);
		zil_itx_destroy(itx);
	}
	list_destroy(&clean_list);
}

void
zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx)
{
	uint64_t txg;
	itxg_t *itxg;
	itxs_t *itxs, *clean = NULL;

	/*
	 * Object ids can be re-instantiated in the next txg so
	 * remove any async transactions to avoid future leaks.
	 * This can happen if a fsync occurs on the re-instantiated
	 * object for a WR_INDIRECT or WR_NEED_COPY write, which gets
	 * the new file data and flushes a write record for the old object.
	 */
	if ((itx->itx_lr.lrc_txtype & ~TX_CI) == TX_REMOVE)
		zil_remove_async(zilog, itx->itx_oid);

	/*
	 * Ensure the data of a renamed file is committed before the rename.
	 */
	if ((itx->itx_lr.lrc_txtype & ~TX_CI) == TX_RENAME)
		zil_async_to_sync(zilog, itx->itx_oid);

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX)
		txg = ZILTEST_TXG;
	else
		txg = dmu_tx_get_txg(tx);

	itxg = &zilog->zl_itxg[txg & TXG_MASK];
	mutex_enter(&itxg->itxg_lock);
	itxs = itxg->itxg_itxs;
	if (itxg->itxg_txg != txg) {
		if (itxs != NULL) {
			/*
			 * The zil_clean callback hasn't got around to cleaning
			 * this itxg. Save the itxs for release below.
			 * This should be rare.
			 */
			atomic_add_64(&zilog->zl_itx_list_sz, -itxg->itxg_sod);
			itxg->itxg_sod = 0;
			clean = itxg->itxg_itxs;
		}
		ASSERT(itxg->itxg_sod == 0);
		itxg->itxg_txg = txg;
		itxs = itxg->itxg_itxs = kmem_zalloc(sizeof (itxs_t),
		    KM_SLEEP);

		list_create(&itxs->i_sync_list, sizeof (itx_t),
		    offsetof(itx_t, itx_node));
		avl_create(&itxs->i_async_tree, zil_aitx_compare,
		    sizeof (itx_async_node_t),
		    offsetof(itx_async_node_t, ia_node));
	}
	if (itx->itx_sync) {
		list_insert_tail(&itxs->i_sync_list, itx);
		atomic_add_64(&zilog->zl_itx_list_sz, itx->itx_sod);
		itxg->itxg_sod += itx->itx_sod;
	} else {
		avl_tree_t *t = &itxs->i_async_tree;
		uint64_t foid = ((lr_ooo_t *)&itx->itx_lr)->lr_foid;
		itx_async_node_t *ian;
		avl_index_t where;

		ian = avl_find(t, &foid, &where);
		if (ian == NULL) {
			ian = kmem_alloc(sizeof (itx_async_node_t),
			    KM_SLEEP);
			list_create(&ian->ia_list, sizeof (itx_t),
			    offsetof(itx_t, itx_node));
			ian->ia_foid = foid;
			avl_insert(t, ian, where);
		}
		list_insert_tail(&ian->ia_list, itx);
	}

	itx->itx_lr.lrc_txg = dmu_tx_get_txg(tx);
	zilog_dirty(zilog, txg);
	mutex_exit(&itxg->itxg_lock);

	/* Release the old itxs now we've dropped the lock */
	if (clean != NULL)
		zil_itxg_clean(clean);
}

/*
 * If there are any in-memory intent log transactions which have now been
 * synced then start up a taskq to free them. We should only do this after we
 * have written out the uberblocks (i.e. txg has been comitted) so that
 * don't inadvertently clean out in-memory log records that would be required
 * by zil_commit().
 */
void
zil_clean(zilog_t *zilog, uint64_t synced_txg)
{
	itxg_t *itxg = &zilog->zl_itxg[synced_txg & TXG_MASK];
	itxs_t *clean_me;

	mutex_enter(&itxg->itxg_lock);
	if (itxg->itxg_itxs == NULL || itxg->itxg_txg == ZILTEST_TXG) {
		mutex_exit(&itxg->itxg_lock);
		return;
	}
	ASSERT3U(itxg->itxg_txg, <=, synced_txg);
	ASSERT(itxg->itxg_txg != 0);
	ASSERT(zilog->zl_clean_taskq != NULL);
	atomic_add_64(&zilog->zl_itx_list_sz, -itxg->itxg_sod);
	itxg->itxg_sod = 0;
	clean_me = itxg->itxg_itxs;
	itxg->itxg_itxs = NULL;
	itxg->itxg_txg = 0;
	mutex_exit(&itxg->itxg_lock);
	/*
	 * Preferably start a task queue to free up the old itxs but
	 * if taskq_dispatch can't allocate resources to do that then
	 * free it in-line. This should be rare. Note, using TQ_SLEEP
	 * created a bad performance problem.
	 */
	if (taskq_dispatch(zilog->zl_clean_taskq,
	    (void (*)(void *))zil_itxg_clean, clean_me, TQ_NOSLEEP) == 0)
		zil_itxg_clean(clean_me);
}

/*
 * Get the list of itxs to commit into zl_itx_commit_list.
 */
static void
zil_get_commit_list(zilog_t *zilog)
{
	uint64_t otxg, txg;
	list_t *commit_list = &zilog->zl_itx_commit_list;
	uint64_t push_sod = 0;

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		list_move_tail(commit_list, &itxg->itxg_itxs->i_sync_list);
		push_sod += itxg->itxg_sod;
		itxg->itxg_sod = 0;

		mutex_exit(&itxg->itxg_lock);
	}
	atomic_add_64(&zilog->zl_itx_list_sz, -push_sod);
}

/*
 * Move the async itxs for a specified object to commit into sync lists.
 */
static void
zil_async_to_sync(zilog_t *zilog, uint64_t foid)
{
	uint64_t otxg, txg;
	itx_async_node_t *ian;
	avl_tree_t *t;
	avl_index_t where;

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * If a foid is specified then find that node and append its
		 * list. Otherwise walk the tree appending all the lists
		 * to the sync list. We add to the end rather than the
		 * beginning to ensure the create has happened.
		 */
		t = &itxg->itxg_itxs->i_async_tree;
		if (foid != 0) {
			ian = avl_find(t, &foid, &where);
			if (ian != NULL) {
				list_move_tail(&itxg->itxg_itxs->i_sync_list,
				    &ian->ia_list);
			}
		} else {
			void *cookie = NULL;

			while ((ian = avl_destroy_nodes(t, &cookie)) != NULL) {
				list_move_tail(&itxg->itxg_itxs->i_sync_list,
				    &ian->ia_list);
				list_destroy(&ian->ia_list);
				kmem_free(ian, sizeof (itx_async_node_t));
			}
		}
		mutex_exit(&itxg->itxg_lock);
	}
}

static void
zil_commit_writer(zilog_t *zilog)
{
	uint64_t txg;
	itx_t *itx;
	lwb_t *lwb;
	spa_t *spa = zilog->zl_spa;
	int error = 0;

	ASSERT(zilog->zl_root_zio == NULL);

	mutex_exit(&zilog->zl_lock);

	zil_get_commit_list(zilog);

	/*
	 * Return if there's nothing to commit before we dirty the fs by
	 * calling zil_create().
	 */
	if (list_head(&zilog->zl_itx_commit_list) == NULL) {
		mutex_enter(&zilog->zl_lock);
		return;
	}

	if (zilog->zl_suspend) {
		lwb = NULL;
	} else {
		lwb = list_tail(&zilog->zl_lwb_list);
		if (lwb == NULL)
			lwb = zil_create(zilog);
	}

	DTRACE_PROBE1(zil__cw1, zilog_t *, zilog);
	for (itx = list_head(&zilog->zl_itx_commit_list); itx != NULL;
	    itx = list_next(&zilog->zl_itx_commit_list, itx)) {
		txg = itx->itx_lr.lrc_txg;
		ASSERT(txg);

		if (txg > spa_last_synced_txg(spa) || txg > spa_freeze_txg(spa))
			lwb = zil_lwb_commit(zilog, itx, lwb);
	}
	DTRACE_PROBE1(zil__cw2, zilog_t *, zilog);

	/* write the last block out */
	if (lwb != NULL && lwb->lwb_zio != NULL)
		lwb = zil_lwb_write_start(zilog, lwb);

	zilog->zl_cur_used = 0;

	/*
	 * Wait if necessary for the log blocks to be on stable storage.
	 */
	if (zilog->zl_root_zio) {
		error = zio_wait(zilog->zl_root_zio);
		zilog->zl_root_zio = NULL;
		zil_flush_vdevs(zilog);
	}

	if (error || lwb == NULL)
		txg_wait_synced(zilog->zl_dmu_pool, 0);

	while ((itx = list_head(&zilog->zl_itx_commit_list))) {
		txg = itx->itx_lr.lrc_txg;
		ASSERT(txg);

		if (itx->itx_callback != NULL)
			itx->itx_callback(itx->itx_callback_data);
		list_remove(&zilog->zl_itx_commit_list, itx);
		zil_itx_destroy(itx);
	}

	mutex_enter(&zilog->zl_lock);

	/*
	 * Remember the highest committed log sequence number for ztest.
	 * We only update this value when all the log writes succeeded,
	 * because ztest wants to ASSERT that it got the whole log chain.
	 */
	if (error == 0 && lwb != NULL)
		zilog->zl_commit_lr_seq = zilog->zl_lr_seq;
}

/*
 * Commit zfs transactions to stable storage.
 * If foid is 0 push out all transactions, otherwise push only those
 * for that object or might reference that object.
 *
 * itxs are committed in batches. In a heavily stressed zil there will be
 * a commit writer thread who is writing out a bunch of itxs to the log
 * for a set of committing threads (cthreads) in the same batch as the writer.
 * Those cthreads are all waiting on the same cv for that batch.
 *
 * There will also be a different and growing batch of threads that are
 * waiting to commit (qthreads). When the committing batch completes
 * a transition occurs such that the cthreads exit and the qthreads become
 * cthreads. One of the new cthreads becomes the writer thread for the
 * batch. Any new threads arriving become new qthreads.
 *
 * Only 2 condition variables are needed and there's no transition
 * between the two cvs needed. They just flip-flop between qthreads
 * and cthreads.
 *
 * Using this scheme we can efficiently wakeup up only those threads
 * that have been committed.
 */
void
zil_commit(zilog_t *zilog, uint64_t foid)
{
	uint64_t mybatch;

	if (zilog->zl_sync == ZFS_SYNC_DISABLED)
		return;

	ZIL_STAT_BUMP(zil_commit_count);

	/* move the async itxs for the foid to the sync queues */
	zil_async_to_sync(zilog, foid);

	mutex_enter(&zilog->zl_lock);
	mybatch = zilog->zl_next_batch;
	while (zilog->zl_writer) {
		cv_wait(&zilog->zl_cv_batch[mybatch & 1], &zilog->zl_lock);
		if (mybatch <= zilog->zl_com_batch) {
			mutex_exit(&zilog->zl_lock);
			return;
		}
	}

	zilog->zl_next_batch++;
	zilog->zl_writer = B_TRUE;
	ZIL_STAT_BUMP(zil_commit_writer_count);
	zil_commit_writer(zilog);
	zilog->zl_com_batch = mybatch;
	zilog->zl_writer = B_FALSE;

	/* wake up one thread to become the next writer */
	cv_signal(&zilog->zl_cv_batch[(mybatch+1) & 1]);

	/* wake up all threads waiting for this batch to be committed */
	cv_broadcast(&zilog->zl_cv_batch[mybatch & 1]);

	mutex_exit(&zilog->zl_lock);
}

/*
 * Called in syncing context to free committed log blocks and update log header.
 */
void
zil_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	zil_header_t *zh = zil_header_in_syncing_context(zilog);
	uint64_t txg = dmu_tx_get_txg(tx);
	spa_t *spa = zilog->zl_spa;
	uint64_t *replayed_seq = &zilog->zl_replayed_seq[txg & TXG_MASK];
	lwb_t *lwb;

	/*
	 * We don't zero out zl_destroy_txg, so make sure we don't try
	 * to destroy it twice.
	 */
	if (spa_sync_pass(spa) != 1)
		return;

	mutex_enter(&zilog->zl_lock);

	ASSERT(zilog->zl_stop_sync == 0);

	if (*replayed_seq != 0) {
		ASSERT(zh->zh_replay_seq < *replayed_seq);
		zh->zh_replay_seq = *replayed_seq;
		*replayed_seq = 0;
	}

	if (zilog->zl_destroy_txg == txg) {
		blkptr_t blk = zh->zh_log;

		ASSERT(list_head(&zilog->zl_lwb_list) == NULL);

		bzero(zh, sizeof (zil_header_t));
		bzero(zilog->zl_replayed_seq, sizeof (zilog->zl_replayed_seq));

		if (zilog->zl_keep_first) {
			/*
			 * If this block was part of log chain that couldn't
			 * be claimed because a device was missing during
			 * zil_claim(), but that device later returns,
			 * then this block could erroneously appear valid.
			 * To guard against this, assign a new GUID to the new
			 * log chain so it doesn't matter what blk points to.
			 */
			zil_init_log_chain(zilog, &blk);
			zh->zh_log = blk;
		}
	}

	while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL) {
		zh->zh_log = lwb->lwb_blk;
		if (lwb->lwb_buf != NULL || lwb->lwb_max_txg > txg)
			break;

		ASSERT(lwb->lwb_zio == NULL);

		list_remove(&zilog->zl_lwb_list, lwb);
		zio_free_zil(spa, txg, &lwb->lwb_blk);
		kmem_cache_free(zil_lwb_cache, lwb);

		/*
		 * If we don't have anything left in the lwb list then
		 * we've had an allocation failure and we need to zero
		 * out the zil_header blkptr so that we don't end
		 * up freeing the same block twice.
		 */
		if (list_head(&zilog->zl_lwb_list) == NULL)
			BP_ZERO(&zh->zh_log);
	}

	/*
	 * Remove fastwrite on any blocks that have been pre-allocated for
	 * the next commit. This prevents fastwrite counter pollution by
	 * unused, long-lived LWBs.
	 */
	for (; lwb != NULL; lwb = list_next(&zilog->zl_lwb_list, lwb)) {
		if (lwb->lwb_fastwrite && !lwb->lwb_zio) {
			metaslab_fastwrite_unmark(zilog->zl_spa, &lwb->lwb_blk);
			lwb->lwb_fastwrite = 0;
		}
	}

	mutex_exit(&zilog->zl_lock);
}

void
zil_init(void)
{
	zil_lwb_cache = kmem_cache_create("zil_lwb_cache",
	    sizeof (struct lwb), 0, NULL, NULL, NULL, NULL, NULL, 0);

	zil_ksp = kstat_create("zfs", 0, "zil", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zil_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zil_ksp != NULL) {
		zil_ksp->ks_data = &zil_stats;
		kstat_install(zil_ksp);
	}
}

void
zil_fini(void)
{
	kmem_cache_destroy(zil_lwb_cache);

	if (zil_ksp != NULL) {
		kstat_delete(zil_ksp);
		zil_ksp = NULL;
	}
}

void
zil_set_sync(zilog_t *zilog, uint64_t sync)
{
	zilog->zl_sync = sync;
}

void
zil_set_logbias(zilog_t *zilog, uint64_t logbias)
{
	zilog->zl_logbias = logbias;
}

zilog_t *
zil_alloc(objset_t *os, zil_header_t *zh_phys)
{
	zilog_t *zilog;
	int i;

	zilog = kmem_zalloc(sizeof (zilog_t), KM_SLEEP);

	zilog->zl_header = zh_phys;
	zilog->zl_os = os;
	zilog->zl_spa = dmu_objset_spa(os);
	zilog->zl_dmu_pool = dmu_objset_pool(os);
	zilog->zl_destroy_txg = TXG_INITIAL - 1;
	zilog->zl_logbias = dmu_objset_logbias(os);
	zilog->zl_sync = dmu_objset_syncprop(os);
	zilog->zl_next_batch = 1;

	mutex_init(&zilog->zl_lock, NULL, MUTEX_DEFAULT, NULL);

	for (i = 0; i < TXG_SIZE; i++) {
		mutex_init(&zilog->zl_itxg[i].itxg_lock, NULL,
		    MUTEX_DEFAULT, NULL);
	}

	list_create(&zilog->zl_lwb_list, sizeof (lwb_t),
	    offsetof(lwb_t, lwb_node));

	list_create(&zilog->zl_itx_commit_list, sizeof (itx_t),
	    offsetof(itx_t, itx_node));

	mutex_init(&zilog->zl_vdev_lock, NULL, MUTEX_DEFAULT, NULL);

	avl_create(&zilog->zl_vdev_tree, zil_vdev_compare,
	    sizeof (zil_vdev_node_t), offsetof(zil_vdev_node_t, zv_node));

	cv_init(&zilog->zl_cv_writer, NULL, CV_DEFAULT, NULL);
	cv_init(&zilog->zl_cv_suspend, NULL, CV_DEFAULT, NULL);
	cv_init(&zilog->zl_cv_batch[0], NULL, CV_DEFAULT, NULL);
	cv_init(&zilog->zl_cv_batch[1], NULL, CV_DEFAULT, NULL);

	return (zilog);
}

void
zil_free(zilog_t *zilog)
{
	int i;

	zilog->zl_stop_sync = 1;

	ASSERT0(zilog->zl_suspend);
	ASSERT0(zilog->zl_suspending);

	ASSERT(list_is_empty(&zilog->zl_lwb_list));
	list_destroy(&zilog->zl_lwb_list);

	avl_destroy(&zilog->zl_vdev_tree);
	mutex_destroy(&zilog->zl_vdev_lock);

	ASSERT(list_is_empty(&zilog->zl_itx_commit_list));
	list_destroy(&zilog->zl_itx_commit_list);

	for (i = 0; i < TXG_SIZE; i++) {
		/*
		 * It's possible for an itx to be generated that doesn't dirty
		 * a txg (e.g. ztest TX_TRUNCATE). So there's no zil_clean()
		 * callback to remove the entry. We remove those here.
		 *
		 * Also free up the ziltest itxs.
		 */
		if (zilog->zl_itxg[i].itxg_itxs)
			zil_itxg_clean(zilog->zl_itxg[i].itxg_itxs);
		mutex_destroy(&zilog->zl_itxg[i].itxg_lock);
	}

	mutex_destroy(&zilog->zl_lock);

	cv_destroy(&zilog->zl_cv_writer);
	cv_destroy(&zilog->zl_cv_suspend);
	cv_destroy(&zilog->zl_cv_batch[0]);
	cv_destroy(&zilog->zl_cv_batch[1]);

	kmem_free(zilog, sizeof (zilog_t));
}

/*
 * Open an intent log.
 */
zilog_t *
zil_open(objset_t *os, zil_get_data_t *get_data)
{
	zilog_t *zilog = dmu_objset_zil(os);

	ASSERT(zilog->zl_clean_taskq == NULL);
	ASSERT(zilog->zl_get_data == NULL);
	ASSERT(list_is_empty(&zilog->zl_lwb_list));

	zilog->zl_get_data = get_data;
	zilog->zl_clean_taskq = taskq_create("zil_clean", 1, defclsyspri,
	    2, 2, TASKQ_PREPOPULATE);

	return (zilog);
}

/*
 * Close an intent log.
 */
void
zil_close(zilog_t *zilog)
{
	lwb_t *lwb;
	uint64_t txg = 0;

	zil_commit(zilog, 0); /* commit all itx */

	/*
	 * The lwb_max_txg for the stubby lwb will reflect the last activity
	 * for the zil.  After a txg_wait_synced() on the txg we know all the
	 * callbacks have occurred that may clean the zil.  Only then can we
	 * destroy the zl_clean_taskq.
	 */
	mutex_enter(&zilog->zl_lock);
	lwb = list_tail(&zilog->zl_lwb_list);
	if (lwb != NULL)
		txg = lwb->lwb_max_txg;
	mutex_exit(&zilog->zl_lock);
	if (txg)
		txg_wait_synced(zilog->zl_dmu_pool, txg);
	ASSERT(!zilog_is_dirty(zilog));

	taskq_destroy(zilog->zl_clean_taskq);
	zilog->zl_clean_taskq = NULL;
	zilog->zl_get_data = NULL;

	/*
	 * We should have only one LWB left on the list; remove it now.
	 */
	mutex_enter(&zilog->zl_lock);
	lwb = list_head(&zilog->zl_lwb_list);
	if (lwb != NULL) {
		ASSERT(lwb == list_tail(&zilog->zl_lwb_list));
		ASSERT(lwb->lwb_zio == NULL);
		if (lwb->lwb_fastwrite)
			metaslab_fastwrite_unmark(zilog->zl_spa, &lwb->lwb_blk);
		list_remove(&zilog->zl_lwb_list, lwb);
		zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
		kmem_cache_free(zil_lwb_cache, lwb);
	}
	mutex_exit(&zilog->zl_lock);
}

static char *suspend_tag = "zil suspending";

/*
 * Suspend an intent log.  While in suspended mode, we still honor
 * synchronous semantics, but we rely on txg_wait_synced() to do it.
 * On old version pools, we suspend the log briefly when taking a
 * snapshot so that it will have an empty intent log.
 *
 * Long holds are not really intended to be used the way we do here --
 * held for such a short time.  A concurrent caller of dsl_dataset_long_held()
 * could fail.  Therefore we take pains to only put a long hold if it is
 * actually necessary.  Fortunately, it will only be necessary if the
 * objset is currently mounted (or the ZVOL equivalent).  In that case it
 * will already have a long hold, so we are not really making things any worse.
 *
 * Ideally, we would locate the existing long-holder (i.e. the zfsvfs_t or
 * zvol_state_t), and use their mechanism to prevent their hold from being
 * dropped (e.g. VFS_HOLD()).  However, that would be even more pain for
 * very little gain.
 *
 * if cookiep == NULL, this does both the suspend & resume.
 * Otherwise, it returns with the dataset "long held", and the cookie
 * should be passed into zil_resume().
 */
int
zil_suspend(const char *osname, void **cookiep)
{
	objset_t *os;
	zilog_t *zilog;
	const zil_header_t *zh;
	int error;

	error = dmu_objset_hold(osname, suspend_tag, &os);
	if (error != 0)
		return (error);
	zilog = dmu_objset_zil(os);

	mutex_enter(&zilog->zl_lock);
	zh = zilog->zl_header;

	if (zh->zh_flags & ZIL_REPLAY_NEEDED) {		/* unplayed log */
		mutex_exit(&zilog->zl_lock);
		dmu_objset_rele(os, suspend_tag);
		return (SET_ERROR(EBUSY));
	}

	/*
	 * Don't put a long hold in the cases where we can avoid it.  This
	 * is when there is no cookie so we are doing a suspend & resume
	 * (i.e. called from zil_vdev_offline()), and there's nothing to do
	 * for the suspend because it's already suspended, or there's no ZIL.
	 */
	if (cookiep == NULL && !zilog->zl_suspending &&
	    (zilog->zl_suspend > 0 || BP_IS_HOLE(&zh->zh_log))) {
		mutex_exit(&zilog->zl_lock);
		dmu_objset_rele(os, suspend_tag);
		return (0);
	}

	dsl_dataset_long_hold(dmu_objset_ds(os), suspend_tag);
	dsl_pool_rele(dmu_objset_pool(os), suspend_tag);

	zilog->zl_suspend++;

	if (zilog->zl_suspend > 1) {
		/*
		 * Someone else is already suspending it.
		 * Just wait for them to finish.
		 */

		while (zilog->zl_suspending)
			cv_wait(&zilog->zl_cv_suspend, &zilog->zl_lock);
		mutex_exit(&zilog->zl_lock);

		if (cookiep == NULL)
			zil_resume(os);
		else
			*cookiep = os;
		return (0);
	}

	/*
	 * If there is no pointer to an on-disk block, this ZIL must not
	 * be active (e.g. filesystem not mounted), so there's nothing
	 * to clean up.
	 */
	if (BP_IS_HOLE(&zh->zh_log)) {
		ASSERT(cookiep != NULL); /* fast path already handled */

		*cookiep = os;
		mutex_exit(&zilog->zl_lock);
		return (0);
	}

	zilog->zl_suspending = B_TRUE;
	mutex_exit(&zilog->zl_lock);

	zil_commit(zilog, 0);

	zil_destroy(zilog, B_FALSE);

	mutex_enter(&zilog->zl_lock);
	zilog->zl_suspending = B_FALSE;
	cv_broadcast(&zilog->zl_cv_suspend);
	mutex_exit(&zilog->zl_lock);

	if (cookiep == NULL)
		zil_resume(os);
	else
		*cookiep = os;
	return (0);
}

void
zil_resume(void *cookie)
{
	objset_t *os = cookie;
	zilog_t *zilog = dmu_objset_zil(os);

	mutex_enter(&zilog->zl_lock);
	ASSERT(zilog->zl_suspend != 0);
	zilog->zl_suspend--;
	mutex_exit(&zilog->zl_lock);
	dsl_dataset_long_rele(dmu_objset_ds(os), suspend_tag);
	dsl_dataset_rele(dmu_objset_ds(os), suspend_tag);
}

typedef struct zil_replay_arg {
	zil_replay_func_t *zr_replay;
	void		*zr_arg;
	boolean_t	zr_byteswap;
	char		*zr_lr;
} zil_replay_arg_t;

static int
zil_replay_error(zilog_t *zilog, lr_t *lr, int error)
{
	char name[MAXNAMELEN];

	zilog->zl_replaying_seq--;	/* didn't actually replay this one */

	dmu_objset_name(zilog->zl_os, name);

	cmn_err(CE_WARN, "ZFS replay transaction error %d, "
	    "dataset %s, seq 0x%llx, txtype %llu %s\n", error, name,
	    (u_longlong_t)lr->lrc_seq,
	    (u_longlong_t)(lr->lrc_txtype & ~TX_CI),
	    (lr->lrc_txtype & TX_CI) ? "CI" : "");

	return (error);
}

static int
zil_replay_log_record(zilog_t *zilog, lr_t *lr, void *zra, uint64_t claim_txg)
{
	zil_replay_arg_t *zr = zra;
	const zil_header_t *zh = zilog->zl_header;
	uint64_t reclen = lr->lrc_reclen;
	uint64_t txtype = lr->lrc_txtype;
	int error = 0;

	zilog->zl_replaying_seq = lr->lrc_seq;

	if (lr->lrc_seq <= zh->zh_replay_seq)	/* already replayed */
		return (0);

	if (lr->lrc_txg < claim_txg)		/* already committed */
		return (0);

	/* Strip case-insensitive bit, still present in log record */
	txtype &= ~TX_CI;

	if (txtype == 0 || txtype >= TX_MAX_TYPE)
		return (zil_replay_error(zilog, lr, EINVAL));

	/*
	 * If this record type can be logged out of order, the object
	 * (lr_foid) may no longer exist.  That's legitimate, not an error.
	 */
	if (TX_OOO(txtype)) {
		error = dmu_object_info(zilog->zl_os,
		    ((lr_ooo_t *)lr)->lr_foid, NULL);
		if (error == ENOENT || error == EEXIST)
			return (0);
	}

	/*
	 * Make a copy of the data so we can revise and extend it.
	 */
	bcopy(lr, zr->zr_lr, reclen);

	/*
	 * If this is a TX_WRITE with a blkptr, suck in the data.
	 */
	if (txtype == TX_WRITE && reclen == sizeof (lr_write_t)) {
		error = zil_read_log_data(zilog, (lr_write_t *)lr,
		    zr->zr_lr + reclen);
		if (error != 0)
			return (zil_replay_error(zilog, lr, error));
	}

	/*
	 * The log block containing this lr may have been byteswapped
	 * so that we can easily examine common fields like lrc_txtype.
	 * However, the log is a mix of different record types, and only the
	 * replay vectors know how to byteswap their records.  Therefore, if
	 * the lr was byteswapped, undo it before invoking the replay vector.
	 */
	if (zr->zr_byteswap)
		byteswap_uint64_array(zr->zr_lr, reclen);

	/*
	 * We must now do two things atomically: replay this log record,
	 * and update the log header sequence number to reflect the fact that
	 * we did so. At the end of each replay function the sequence number
	 * is updated if we are in replay mode.
	 */
	error = zr->zr_replay[txtype](zr->zr_arg, zr->zr_lr, zr->zr_byteswap);
	if (error != 0) {
		/*
		 * The DMU's dnode layer doesn't see removes until the txg
		 * commits, so a subsequent claim can spuriously fail with
		 * EEXIST. So if we receive any error we try syncing out
		 * any removes then retry the transaction.  Note that we
		 * specify B_FALSE for byteswap now, so we don't do it twice.
		 */
		txg_wait_synced(spa_get_dsl(zilog->zl_spa), 0);
		error = zr->zr_replay[txtype](zr->zr_arg, zr->zr_lr, B_FALSE);
		if (error != 0)
			return (zil_replay_error(zilog, lr, error));
	}
	return (0);
}

/* ARGSUSED */
static int
zil_incr_blks(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	zilog->zl_replay_blks++;

	return (0);
}

/*
 * If this dataset has a non-empty intent log, replay it and destroy it.
 */
void
zil_replay(objset_t *os, void *arg, zil_replay_func_t replay_func[TX_MAX_TYPE])
{
	zilog_t *zilog = dmu_objset_zil(os);
	const zil_header_t *zh = zilog->zl_header;
	zil_replay_arg_t zr;

	if ((zh->zh_flags & ZIL_REPLAY_NEEDED) == 0) {
		zil_destroy(zilog, B_TRUE);
		return;
	}

	zr.zr_replay = replay_func;
	zr.zr_arg = arg;
	zr.zr_byteswap = BP_SHOULD_BYTESWAP(&zh->zh_log);
	zr.zr_lr = vmem_alloc(2 * SPA_MAXBLOCKSIZE, KM_SLEEP);

	/*
	 * Wait for in-progress removes to sync before starting replay.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, 0);

	zilog->zl_replay = B_TRUE;
	zilog->zl_replay_time = ddi_get_lbolt();
	ASSERT(zilog->zl_replay_blks == 0);
	(void) zil_parse(zilog, zil_incr_blks, zil_replay_log_record, &zr,
	    zh->zh_claim_txg);
	vmem_free(zr.zr_lr, 2 * SPA_MAXBLOCKSIZE);

	zil_destroy(zilog, B_FALSE);
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);
	zilog->zl_replay = B_FALSE;
}

boolean_t
zil_replaying(zilog_t *zilog, dmu_tx_t *tx)
{
	if (zilog->zl_sync == ZFS_SYNC_DISABLED)
		return (B_TRUE);

	if (zilog->zl_replay) {
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		zilog->zl_replayed_seq[dmu_tx_get_txg(tx) & TXG_MASK] =
		    zilog->zl_replaying_seq;
		return (B_TRUE);
	}

	return (B_FALSE);
}

/* ARGSUSED */
int
zil_vdev_offline(const char *osname, void *arg)
{
	int error;

	error = zil_suspend(osname, NULL);
	if (error != 0)
		return (SET_ERROR(EEXIST));
	return (0);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zil_alloc);
EXPORT_SYMBOL(zil_free);
EXPORT_SYMBOL(zil_open);
EXPORT_SYMBOL(zil_close);
EXPORT_SYMBOL(zil_replay);
EXPORT_SYMBOL(zil_replaying);
EXPORT_SYMBOL(zil_destroy);
EXPORT_SYMBOL(zil_destroy_sync);
EXPORT_SYMBOL(zil_itx_create);
EXPORT_SYMBOL(zil_itx_destroy);
EXPORT_SYMBOL(zil_itx_assign);
EXPORT_SYMBOL(zil_commit);
EXPORT_SYMBOL(zil_vdev_offline);
EXPORT_SYMBOL(zil_claim);
EXPORT_SYMBOL(zil_check_log_chain);
EXPORT_SYMBOL(zil_sync);
EXPORT_SYMBOL(zil_clean);
EXPORT_SYMBOL(zil_suspend);
EXPORT_SYMBOL(zil_resume);
EXPORT_SYMBOL(zil_add_block);
EXPORT_SYMBOL(zil_bp_tree_add);
EXPORT_SYMBOL(zil_set_sync);
EXPORT_SYMBOL(zil_set_logbias);

module_param(zil_replay_disable, int, 0644);
MODULE_PARM_DESC(zil_replay_disable, "Disable intent logging replay");

module_param(zfs_nocacheflush, int, 0644);
MODULE_PARM_DESC(zfs_nocacheflush, "Disable cache flushes");

module_param(zil_slog_limit, ulong, 0644);
MODULE_PARM_DESC(zil_slog_limit, "Max commit bytes to separate log device");
#endif
