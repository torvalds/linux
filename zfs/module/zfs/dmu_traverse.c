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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dnode.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/dmu_impl.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/callb.h>
#include <sys/zfeature.h>

int32_t zfs_pd_bytes_max = 50 * 1024 * 1024;	/* 50MB */
int32_t send_holes_without_birth_time = 1;

typedef struct prefetch_data {
	kmutex_t pd_mtx;
	kcondvar_t pd_cv;
	int32_t pd_bytes_fetched;
	int pd_flags;
	boolean_t pd_cancel;
	boolean_t pd_exited;
	zbookmark_phys_t pd_resume;
} prefetch_data_t;

typedef struct traverse_data {
	spa_t *td_spa;
	uint64_t td_objset;
	blkptr_t *td_rootbp;
	uint64_t td_min_txg;
	zbookmark_phys_t *td_resume;
	int td_flags;
	prefetch_data_t *td_pfd;
	boolean_t td_paused;
	uint64_t td_hole_birth_enabled_txg;
	blkptr_cb_t *td_func;
	void *td_arg;
	boolean_t td_realloc_possible;
} traverse_data_t;

static int traverse_dnode(traverse_data_t *td, const dnode_phys_t *dnp,
    uint64_t objset, uint64_t object);
static void prefetch_dnode_metadata(traverse_data_t *td, const dnode_phys_t *,
    uint64_t objset, uint64_t object);

static int
traverse_zil_block(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	traverse_data_t *td = arg;
	zbookmark_phys_t zb;

	if (BP_IS_HOLE(bp))
		return (0);

	if (claim_txg == 0 && bp->blk_birth >= spa_first_txg(td->td_spa))
		return (0);

	SET_BOOKMARK(&zb, td->td_objset, ZB_ZIL_OBJECT, ZB_ZIL_LEVEL,
	    bp->blk_cksum.zc_word[ZIL_ZC_SEQ]);

	(void) td->td_func(td->td_spa, zilog, bp, &zb, NULL, td->td_arg);

	return (0);
}

static int
traverse_zil_record(zilog_t *zilog, lr_t *lrc, void *arg, uint64_t claim_txg)
{
	traverse_data_t *td = arg;

	if (lrc->lrc_txtype == TX_WRITE) {
		lr_write_t *lr = (lr_write_t *)lrc;
		blkptr_t *bp = &lr->lr_blkptr;
		zbookmark_phys_t zb;

		if (BP_IS_HOLE(bp))
			return (0);

		if (claim_txg == 0 || bp->blk_birth < claim_txg)
			return (0);

		SET_BOOKMARK(&zb, td->td_objset, lr->lr_foid,
		    ZB_ZIL_LEVEL, lr->lr_offset / BP_GET_LSIZE(bp));

		(void) td->td_func(td->td_spa, zilog, bp, &zb, NULL,
		    td->td_arg);
	}
	return (0);
}

static void
traverse_zil(traverse_data_t *td, zil_header_t *zh)
{
	uint64_t claim_txg = zh->zh_claim_txg;
	zilog_t *zilog;

	/*
	 * We only want to visit blocks that have been claimed but not yet
	 * replayed; plus, in read-only mode, blocks that are already stable.
	 */
	if (claim_txg == 0 && spa_writeable(td->td_spa))
		return;

	zilog = zil_alloc(spa_get_dsl(td->td_spa)->dp_meta_objset, zh);

	(void) zil_parse(zilog, traverse_zil_block, traverse_zil_record, td,
	    claim_txg);

	zil_free(zilog);
}

typedef enum resume_skip {
	RESUME_SKIP_ALL,
	RESUME_SKIP_NONE,
	RESUME_SKIP_CHILDREN
} resume_skip_t;

/*
 * Returns RESUME_SKIP_ALL if td indicates that we are resuming a traversal and
 * the block indicated by zb does not need to be visited at all. Returns
 * RESUME_SKIP_CHILDREN if we are resuming a post traversal and we reach the
 * resume point. This indicates that this block should be visited but not its
 * children (since they must have been visited in a previous traversal).
 * Otherwise returns RESUME_SKIP_NONE.
 */
static resume_skip_t
resume_skip_check(traverse_data_t *td, const dnode_phys_t *dnp,
    const zbookmark_phys_t *zb)
{
	if (td->td_resume != NULL && !ZB_IS_ZERO(td->td_resume)) {
		/*
		 * If we already visited this bp & everything below,
		 * don't bother doing it again.
		 */
		if (zbookmark_subtree_completed(dnp, zb, td->td_resume))
			return (RESUME_SKIP_ALL);

		/*
		 * If we found the block we're trying to resume from, zero
		 * the bookmark out to indicate that we have resumed.
		 */
		if (bcmp(zb, td->td_resume, sizeof (*zb)) == 0) {
			bzero(td->td_resume, sizeof (*zb));
			if (td->td_flags & TRAVERSE_POST)
				return (RESUME_SKIP_CHILDREN);
		}
	}
	return (RESUME_SKIP_NONE);
}

static void
traverse_prefetch_metadata(traverse_data_t *td,
    const blkptr_t *bp, const zbookmark_phys_t *zb)
{
	arc_flags_t flags = ARC_FLAG_NOWAIT | ARC_FLAG_PREFETCH;

	if (!(td->td_flags & TRAVERSE_PREFETCH_METADATA))
		return;
	/*
	 * If we are in the process of resuming, don't prefetch, because
	 * some children will not be needed (and in fact may have already
	 * been freed).
	 */
	if (td->td_resume != NULL && !ZB_IS_ZERO(td->td_resume))
		return;
	if (BP_IS_HOLE(bp) || bp->blk_birth <= td->td_min_txg)
		return;
	if (BP_GET_LEVEL(bp) == 0 && BP_GET_TYPE(bp) != DMU_OT_DNODE)
		return;

	(void) arc_read(NULL, td->td_spa, bp, NULL, NULL,
	    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
}

static boolean_t
prefetch_needed(prefetch_data_t *pfd, const blkptr_t *bp)
{
	ASSERT(pfd->pd_flags & TRAVERSE_PREFETCH_DATA);
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp) ||
	    BP_GET_TYPE(bp) == DMU_OT_INTENT_LOG)
		return (B_FALSE);
	return (B_TRUE);
}

static int
traverse_visitbp(traverse_data_t *td, const dnode_phys_t *dnp,
    const blkptr_t *bp, const zbookmark_phys_t *zb)
{
	int err = 0;
	arc_buf_t *buf = NULL;
	prefetch_data_t *pd = td->td_pfd;

	switch (resume_skip_check(td, dnp, zb)) {
	case RESUME_SKIP_ALL:
		return (0);
	case RESUME_SKIP_CHILDREN:
		goto post;
	case RESUME_SKIP_NONE:
		break;
	default:
		ASSERT(0);
	}

	if (bp->blk_birth == 0) {
		/*
		 * Since this block has a birth time of 0 it must be one of
		 * two things: a hole created before the
		 * SPA_FEATURE_HOLE_BIRTH feature was enabled, or a hole
		 * which has always been a hole in an object.
		 *
		 * If a file is written sparsely, then the unwritten parts of
		 * the file were "always holes" -- that is, they have been
		 * holes since this object was allocated.  However, we (and
		 * our callers) can not necessarily tell when an object was
		 * allocated.  Therefore, if it's possible that this object
		 * was freed and then its object number reused, we need to
		 * visit all the holes with birth==0.
		 *
		 * If it isn't possible that the object number was reused,
		 * then if SPA_FEATURE_HOLE_BIRTH was enabled before we wrote
		 * all the blocks we will visit as part of this traversal,
		 * then this hole must have always existed, so we can skip
		 * it.  We visit blocks born after (exclusive) td_min_txg.
		 *
		 * Note that the meta-dnode cannot be reallocated.
		 */
		if (!send_holes_without_birth_time &&
		    (!td->td_realloc_possible ||
		    zb->zb_object == DMU_META_DNODE_OBJECT) &&
		    td->td_hole_birth_enabled_txg <= td->td_min_txg)
			return (0);
	} else if (bp->blk_birth <= td->td_min_txg) {
		return (0);
	}

	if (pd != NULL && !pd->pd_exited && prefetch_needed(pd, bp)) {
		uint64_t size = BP_GET_LSIZE(bp);
		mutex_enter(&pd->pd_mtx);
		ASSERT(pd->pd_bytes_fetched >= 0);
		while (pd->pd_bytes_fetched < size && !pd->pd_exited)
			cv_wait_sig(&pd->pd_cv, &pd->pd_mtx);
		pd->pd_bytes_fetched -= size;
		cv_broadcast(&pd->pd_cv);
		mutex_exit(&pd->pd_mtx);
	}

	if (BP_IS_HOLE(bp)) {
		err = td->td_func(td->td_spa, NULL, bp, zb, dnp, td->td_arg);
		if (err != 0)
			goto post;
		return (0);
	}

	if (td->td_flags & TRAVERSE_PRE) {
		err = td->td_func(td->td_spa, NULL, bp, zb, dnp,
		    td->td_arg);
		if (err == TRAVERSE_VISIT_NO_CHILDREN)
			return (0);
		if (err != 0)
			goto post;
	}

	if (BP_GET_LEVEL(bp) > 0) {
		uint32_t flags = ARC_FLAG_WAIT;
		int32_t i;
		int32_t epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;
		zbookmark_phys_t *czb;

		err = arc_read(NULL, td->td_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err != 0)
			goto post;

		czb = kmem_alloc(sizeof (zbookmark_phys_t), KM_SLEEP);

		for (i = 0; i < epb; i++) {
			SET_BOOKMARK(czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			traverse_prefetch_metadata(td,
			    &((blkptr_t *)buf->b_data)[i], czb);
		}

		/* recursively visitbp() blocks below this */
		for (i = 0; i < epb; i++) {
			SET_BOOKMARK(czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			err = traverse_visitbp(td, dnp,
			    &((blkptr_t *)buf->b_data)[i], czb);
			if (err != 0)
				break;
		}

		kmem_free(czb, sizeof (zbookmark_phys_t));

	} else if (BP_GET_TYPE(bp) == DMU_OT_DNODE) {
		uint32_t flags = ARC_FLAG_WAIT;
		int32_t i;
		int32_t epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;
		dnode_phys_t *child_dnp;

		err = arc_read(NULL, td->td_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err != 0)
			goto post;
		child_dnp = buf->b_data;

		for (i = 0; i < epb; i += child_dnp[i].dn_extra_slots + 1) {
			prefetch_dnode_metadata(td, &child_dnp[i],
			    zb->zb_objset, zb->zb_blkid * epb + i);
		}

		/* recursively visitbp() blocks below this */
		for (i = 0; i < epb; i += child_dnp[i].dn_extra_slots + 1) {
			err = traverse_dnode(td, &child_dnp[i],
			    zb->zb_objset, zb->zb_blkid * epb + i);
			if (err != 0)
				break;
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_OBJSET) {
		arc_flags_t flags = ARC_FLAG_WAIT;
		objset_phys_t *osp;

		err = arc_read(NULL, td->td_spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err != 0)
			goto post;

		osp = buf->b_data;
		prefetch_dnode_metadata(td, &osp->os_meta_dnode, zb->zb_objset,
		    DMU_META_DNODE_OBJECT);
		/*
		 * See the block comment above for the goal of this variable.
		 * If the maxblkid of the meta-dnode is 0, then we know that
		 * we've never had more than DNODES_PER_BLOCK objects in the
		 * dataset, which means we can't have reused any object ids.
		 */
		if (osp->os_meta_dnode.dn_maxblkid == 0)
			td->td_realloc_possible = B_FALSE;

		if (arc_buf_size(buf) >= sizeof (objset_phys_t)) {
			prefetch_dnode_metadata(td, &osp->os_groupused_dnode,
			    zb->zb_objset, DMU_GROUPUSED_OBJECT);
			prefetch_dnode_metadata(td, &osp->os_userused_dnode,
			    zb->zb_objset, DMU_USERUSED_OBJECT);
		}

		err = traverse_dnode(td, &osp->os_meta_dnode, zb->zb_objset,
		    DMU_META_DNODE_OBJECT);
		if (err == 0 && arc_buf_size(buf) >= sizeof (objset_phys_t)) {
			err = traverse_dnode(td, &osp->os_groupused_dnode,
			    zb->zb_objset, DMU_GROUPUSED_OBJECT);
		}
		if (err == 0 && arc_buf_size(buf) >= sizeof (objset_phys_t)) {
			err = traverse_dnode(td, &osp->os_userused_dnode,
			    zb->zb_objset, DMU_USERUSED_OBJECT);
		}
	}

	if (buf)
		arc_buf_destroy(buf, &buf);

post:
	if (err == 0 && (td->td_flags & TRAVERSE_POST))
		err = td->td_func(td->td_spa, NULL, bp, zb, dnp, td->td_arg);

	if ((td->td_flags & TRAVERSE_HARD) && (err == EIO || err == ECKSUM)) {
		/*
		 * Ignore this disk error as requested by the HARD flag,
		 * and continue traversal.
		 */
		err = 0;
	}

	/*
	 * If we are stopping here, set td_resume.
	 */
	if (td->td_resume != NULL && err != 0 && !td->td_paused) {
		td->td_resume->zb_objset = zb->zb_objset;
		td->td_resume->zb_object = zb->zb_object;
		td->td_resume->zb_level = 0;
		/*
		 * If we have stopped on an indirect block (e.g. due to
		 * i/o error), we have not visited anything below it.
		 * Set the bookmark to the first level-0 block that we need
		 * to visit.  This way, the resuming code does not need to
		 * deal with resuming from indirect blocks.
		 *
		 * Note, if zb_level <= 0, dnp may be NULL, so we don't want
		 * to dereference it.
		 */
		td->td_resume->zb_blkid = zb->zb_blkid;
		if (zb->zb_level > 0) {
			td->td_resume->zb_blkid <<= zb->zb_level *
			    (dnp->dn_indblkshift - SPA_BLKPTRSHIFT);
		}
		td->td_paused = B_TRUE;
	}

	return (err);
}

static void
prefetch_dnode_metadata(traverse_data_t *td, const dnode_phys_t *dnp,
    uint64_t objset, uint64_t object)
{
	int j;
	zbookmark_phys_t czb;

	for (j = 0; j < dnp->dn_nblkptr; j++) {
		SET_BOOKMARK(&czb, objset, object, dnp->dn_nlevels - 1, j);
		traverse_prefetch_metadata(td, &dnp->dn_blkptr[j], &czb);
	}

	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		SET_BOOKMARK(&czb, objset, object, 0, DMU_SPILL_BLKID);
		traverse_prefetch_metadata(td, DN_SPILL_BLKPTR(dnp), &czb);
	}
}

static int
traverse_dnode(traverse_data_t *td, const dnode_phys_t *dnp,
    uint64_t objset, uint64_t object)
{
	int j, err = 0;
	zbookmark_phys_t czb;

	if (object != DMU_META_DNODE_OBJECT && td->td_resume != NULL &&
	    object < td->td_resume->zb_object)
		return (0);

	if (td->td_flags & TRAVERSE_PRE) {
		SET_BOOKMARK(&czb, objset, object, ZB_DNODE_LEVEL,
		    ZB_DNODE_BLKID);
		err = td->td_func(td->td_spa, NULL, NULL, &czb, dnp,
		    td->td_arg);
		if (err == TRAVERSE_VISIT_NO_CHILDREN)
			return (0);
		if (err != 0)
			return (err);
	}

	for (j = 0; j < dnp->dn_nblkptr; j++) {
		SET_BOOKMARK(&czb, objset, object, dnp->dn_nlevels - 1, j);
		err = traverse_visitbp(td, dnp, &dnp->dn_blkptr[j], &czb);
		if (err != 0)
			break;
	}

	if (err == 0 && (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR)) {
		SET_BOOKMARK(&czb, objset, object, 0, DMU_SPILL_BLKID);
		err = traverse_visitbp(td, dnp, DN_SPILL_BLKPTR(dnp), &czb);
	}

	if (err == 0 && (td->td_flags & TRAVERSE_POST)) {
		SET_BOOKMARK(&czb, objset, object, ZB_DNODE_LEVEL,
		    ZB_DNODE_BLKID);
		err = td->td_func(td->td_spa, NULL, NULL, &czb, dnp,
		    td->td_arg);
		if (err == TRAVERSE_VISIT_NO_CHILDREN)
			return (0);
		if (err != 0)
			return (err);
	}
	return (err);
}

/* ARGSUSED */
static int
traverse_prefetcher(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	prefetch_data_t *pfd = arg;
	arc_flags_t aflags = ARC_FLAG_NOWAIT | ARC_FLAG_PREFETCH;

	ASSERT(pfd->pd_bytes_fetched >= 0);
	if (bp == NULL)
		return (0);
	if (pfd->pd_cancel)
		return (SET_ERROR(EINTR));

	if (!prefetch_needed(pfd, bp))
		return (0);

	mutex_enter(&pfd->pd_mtx);
	while (!pfd->pd_cancel && pfd->pd_bytes_fetched >= zfs_pd_bytes_max)
		cv_wait_sig(&pfd->pd_cv, &pfd->pd_mtx);
	pfd->pd_bytes_fetched += BP_GET_LSIZE(bp);
	cv_broadcast(&pfd->pd_cv);
	mutex_exit(&pfd->pd_mtx);

	(void) arc_read(NULL, spa, bp, NULL, NULL, ZIO_PRIORITY_ASYNC_READ,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE, &aflags, zb);

	return (0);
}

static void
traverse_prefetch_thread(void *arg)
{
	traverse_data_t *td_main = arg;
	traverse_data_t td = *td_main;
	zbookmark_phys_t czb;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	td.td_func = traverse_prefetcher;
	td.td_arg = td_main->td_pfd;
	td.td_pfd = NULL;
	td.td_resume = &td_main->td_pfd->pd_resume;

	SET_BOOKMARK(&czb, td.td_objset,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);
	(void) traverse_visitbp(&td, NULL, td.td_rootbp, &czb);

	mutex_enter(&td_main->td_pfd->pd_mtx);
	td_main->td_pfd->pd_exited = B_TRUE;
	cv_broadcast(&td_main->td_pfd->pd_cv);
	mutex_exit(&td_main->td_pfd->pd_mtx);
	spl_fstrans_unmark(cookie);
}

/*
 * NB: dataset must not be changing on-disk (eg, is a snapshot or we are
 * in syncing context).
 */
static int
traverse_impl(spa_t *spa, dsl_dataset_t *ds, uint64_t objset, blkptr_t *rootbp,
    uint64_t txg_start, zbookmark_phys_t *resume, int flags,
    blkptr_cb_t func, void *arg)
{
	traverse_data_t *td;
	prefetch_data_t *pd;
	zbookmark_phys_t *czb;
	int err;

	ASSERT(ds == NULL || objset == ds->ds_object);
	ASSERT(!(flags & TRAVERSE_PRE) || !(flags & TRAVERSE_POST));

	td = kmem_alloc(sizeof (traverse_data_t), KM_SLEEP);
	pd = kmem_zalloc(sizeof (prefetch_data_t), KM_SLEEP);
	czb = kmem_alloc(sizeof (zbookmark_phys_t), KM_SLEEP);

	td->td_spa = spa;
	td->td_objset = objset;
	td->td_rootbp = rootbp;
	td->td_min_txg = txg_start;
	td->td_resume = resume;
	td->td_func = func;
	td->td_arg = arg;
	td->td_pfd = pd;
	td->td_flags = flags;
	td->td_paused = B_FALSE;
	td->td_realloc_possible = (txg_start == 0 ? B_FALSE : B_TRUE);

	if (spa_feature_is_active(spa, SPA_FEATURE_HOLE_BIRTH)) {
		VERIFY(spa_feature_enabled_txg(spa,
		    SPA_FEATURE_HOLE_BIRTH, &td->td_hole_birth_enabled_txg));
	} else {
		td->td_hole_birth_enabled_txg = UINT64_MAX;
	}

	pd->pd_flags = flags;
	if (resume != NULL)
		pd->pd_resume = *resume;
	mutex_init(&pd->pd_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&pd->pd_cv, NULL, CV_DEFAULT, NULL);

	SET_BOOKMARK(czb, td->td_objset,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);

	/* See comment on ZIL traversal in dsl_scan_visitds. */
	if (ds != NULL && !ds->ds_is_snapshot && !BP_IS_HOLE(rootbp)) {
		enum zio_flag zio_flags = ZIO_FLAG_CANFAIL;
		uint32_t flags = ARC_FLAG_WAIT;
		objset_phys_t *osp;
		arc_buf_t *buf;

		err = arc_read(NULL, td->td_spa, rootbp, arc_getbuf_func,
		    &buf, ZIO_PRIORITY_ASYNC_READ, zio_flags, &flags, czb);
		if (err != 0) {
			/*
			 * If both TRAVERSE_HARD and TRAVERSE_PRE are set,
			 * continue to visitbp so that td_func can be called
			 * in pre stage, and err will reset to zero.
			 */
			if (!(td->td_flags & TRAVERSE_HARD) ||
			    !(td->td_flags & TRAVERSE_PRE))
				return (err);
		} else {
			osp = buf->b_data;
			traverse_zil(td, &osp->os_zil_header);
			arc_buf_destroy(buf, &buf);
		}
	}

	if (!(flags & TRAVERSE_PREFETCH_DATA) ||
	    taskq_dispatch(spa->spa_prefetch_taskq, traverse_prefetch_thread,
	    td, TQ_NOQUEUE) == TASKQID_INVALID)
		pd->pd_exited = B_TRUE;

	err = traverse_visitbp(td, NULL, rootbp, czb);

	mutex_enter(&pd->pd_mtx);
	pd->pd_cancel = B_TRUE;
	cv_broadcast(&pd->pd_cv);
	while (!pd->pd_exited)
		cv_wait_sig(&pd->pd_cv, &pd->pd_mtx);
	mutex_exit(&pd->pd_mtx);

	mutex_destroy(&pd->pd_mtx);
	cv_destroy(&pd->pd_cv);

	kmem_free(czb, sizeof (zbookmark_phys_t));
	kmem_free(pd, sizeof (struct prefetch_data));
	kmem_free(td, sizeof (struct traverse_data));

	return (err);
}

/*
 * NB: dataset must not be changing on-disk (eg, is a snapshot or we are
 * in syncing context).
 */
int
traverse_dataset_resume(dsl_dataset_t *ds, uint64_t txg_start,
    zbookmark_phys_t *resume,
    int flags, blkptr_cb_t func, void *arg)
{
	return (traverse_impl(ds->ds_dir->dd_pool->dp_spa, ds, ds->ds_object,
	    &dsl_dataset_phys(ds)->ds_bp, txg_start, resume, flags, func, arg));
}

int
traverse_dataset(dsl_dataset_t *ds, uint64_t txg_start,
    int flags, blkptr_cb_t func, void *arg)
{
	return (traverse_dataset_resume(ds, txg_start, NULL, flags, func, arg));
}

int
traverse_dataset_destroyed(spa_t *spa, blkptr_t *blkptr,
    uint64_t txg_start, zbookmark_phys_t *resume, int flags,
    blkptr_cb_t func, void *arg)
{
	return (traverse_impl(spa, NULL, ZB_DESTROYED_OBJSET,
	    blkptr, txg_start, resume, flags, func, arg));
}

/*
 * NB: pool must not be changing on-disk (eg, from zdb or sync context).
 */
int
traverse_pool(spa_t *spa, uint64_t txg_start, int flags,
    blkptr_cb_t func, void *arg)
{
	int err;
	uint64_t obj;
	dsl_pool_t *dp = spa_get_dsl(spa);
	objset_t *mos = dp->dp_meta_objset;
	boolean_t hard = (flags & TRAVERSE_HARD);

	/* visit the MOS */
	err = traverse_impl(spa, NULL, 0, spa_get_rootblkptr(spa),
	    txg_start, NULL, flags, func, arg);
	if (err != 0)
		return (err);

	/* visit each dataset */
	for (obj = 1; err == 0;
	    err = dmu_object_next(mos, &obj, B_FALSE, txg_start)) {
		dmu_object_info_t doi;

		err = dmu_object_info(mos, obj, &doi);
		if (err != 0) {
			if (hard)
				continue;
			break;
		}

		if (doi.doi_bonus_type == DMU_OT_DSL_DATASET) {
			dsl_dataset_t *ds;
			uint64_t txg = txg_start;

			dsl_pool_config_enter(dp, FTAG);
			err = dsl_dataset_hold_obj(dp, obj, FTAG, &ds);
			dsl_pool_config_exit(dp, FTAG);
			if (err != 0) {
				if (hard)
					continue;
				break;
			}
			if (dsl_dataset_phys(ds)->ds_prev_snap_txg > txg)
				txg = dsl_dataset_phys(ds)->ds_prev_snap_txg;
			err = traverse_dataset(ds, txg, flags, func, arg);
			dsl_dataset_rele(ds, FTAG);
			if (err != 0)
				break;
		}
	}
	if (err == ESRCH)
		err = 0;
	return (err);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(traverse_dataset);
EXPORT_SYMBOL(traverse_pool);

module_param(zfs_pd_bytes_max, int, 0644);
MODULE_PARM_DESC(zfs_pd_bytes_max, "Max number of bytes to prefetch");

module_param_named(ignore_hole_birth, send_holes_without_birth_time, int, 0644);
MODULE_PARM_DESC(ignore_hole_birth, "Alias for send_holes_without_birth_time");

module_param_named(send_holes_without_birth_time,
	send_holes_without_birth_time, int, 0644);
MODULE_PARM_DESC(send_holes_without_birth_time,
	"Ignore hole_birth txg for zfs send");
#endif
