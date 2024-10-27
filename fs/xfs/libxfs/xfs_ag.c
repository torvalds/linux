/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2018 Red Hat, Inc.
 * All rights reserved.
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_rmap_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_health.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_defer.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_trace.h"
#include "xfs_inode.h"
#include "xfs_icache.h"


/*
 * Passive reference counting access wrappers to the perag structures.  If the
 * per-ag structure is to be freed, the freeing code is responsible for cleaning
 * up objects with passive references before freeing the structure. This is
 * things like cached buffers.
 */
struct xfs_perag *
xfs_perag_get(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_perag	*pag;

	rcu_read_lock();
	pag = xa_load(&mp->m_perags, agno);
	if (pag) {
		trace_xfs_perag_get(pag, _RET_IP_);
		ASSERT(atomic_read(&pag->pag_ref) >= 0);
		atomic_inc(&pag->pag_ref);
	}
	rcu_read_unlock();
	return pag;
}

/* Get a passive reference to the given perag. */
struct xfs_perag *
xfs_perag_hold(
	struct xfs_perag	*pag)
{
	ASSERT(atomic_read(&pag->pag_ref) > 0 ||
	       atomic_read(&pag->pag_active_ref) > 0);

	trace_xfs_perag_hold(pag, _RET_IP_);
	atomic_inc(&pag->pag_ref);
	return pag;
}

void
xfs_perag_put(
	struct xfs_perag	*pag)
{
	trace_xfs_perag_put(pag, _RET_IP_);
	ASSERT(atomic_read(&pag->pag_ref) > 0);
	atomic_dec(&pag->pag_ref);
}

/*
 * Active references for perag structures. This is for short term access to the
 * per ag structures for walking trees or accessing state. If an AG is being
 * shrunk or is offline, then this will fail to find that AG and return NULL
 * instead.
 */
struct xfs_perag *
xfs_perag_grab(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_perag	*pag;

	rcu_read_lock();
	pag = xa_load(&mp->m_perags, agno);
	if (pag) {
		trace_xfs_perag_grab(pag, _RET_IP_);
		if (!atomic_inc_not_zero(&pag->pag_active_ref))
			pag = NULL;
	}
	rcu_read_unlock();
	return pag;
}

void
xfs_perag_rele(
	struct xfs_perag	*pag)
{
	trace_xfs_perag_rele(pag, _RET_IP_);
	if (atomic_dec_and_test(&pag->pag_active_ref))
		wake_up(&pag->pag_active_wq);
}

/*
 * xfs_initialize_perag_data
 *
 * Read in each per-ag structure so we can count up the number of
 * allocated inodes, free inodes and used filesystem blocks as this
 * information is no longer persistent in the superblock. Once we have
 * this information, write it into the in-core superblock structure.
 */
int
xfs_initialize_perag_data(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agcount)
{
	xfs_agnumber_t		index;
	struct xfs_perag	*pag;
	struct xfs_sb		*sbp = &mp->m_sb;
	uint64_t		ifree = 0;
	uint64_t		ialloc = 0;
	uint64_t		bfree = 0;
	uint64_t		bfreelst = 0;
	uint64_t		btree = 0;
	uint64_t		fdblocks;
	int			error = 0;

	for (index = 0; index < agcount; index++) {
		/*
		 * Read the AGF and AGI buffers to populate the per-ag
		 * structures for us.
		 */
		pag = xfs_perag_get(mp, index);
		error = xfs_alloc_read_agf(pag, NULL, 0, NULL);
		if (!error)
			error = xfs_ialloc_read_agi(pag, NULL, 0, NULL);
		if (error) {
			xfs_perag_put(pag);
			return error;
		}

		ifree += pag->pagi_freecount;
		ialloc += pag->pagi_count;
		bfree += pag->pagf_freeblks;
		bfreelst += pag->pagf_flcount;
		btree += pag->pagf_btreeblks;
		xfs_perag_put(pag);
	}
	fdblocks = bfree + bfreelst + btree;

	/*
	 * If the new summary counts are obviously incorrect, fail the
	 * mount operation because that implies the AGFs are also corrupt.
	 * Clear FS_COUNTERS so that we don't unmount with a dirty log, which
	 * will prevent xfs_repair from fixing anything.
	 */
	if (fdblocks > sbp->sb_dblocks || ifree > ialloc) {
		xfs_alert(mp, "AGF corruption. Please run xfs_repair.");
		xfs_fs_mark_sick(mp, XFS_SICK_FS_COUNTERS);
		error = -EFSCORRUPTED;
		goto out;
	}

	/* Overwrite incore superblock counters with just-read data */
	spin_lock(&mp->m_sb_lock);
	sbp->sb_ifree = ifree;
	sbp->sb_icount = ialloc;
	sbp->sb_fdblocks = fdblocks;
	spin_unlock(&mp->m_sb_lock);

	xfs_reinit_percpu_counters(mp);
out:
	xfs_fs_mark_healthy(mp, XFS_SICK_FS_COUNTERS);
	return error;
}

/*
 * Free up the per-ag resources  within the specified AG range.
 */
void
xfs_free_perag_range(
	struct xfs_mount	*mp,
	xfs_agnumber_t		first_agno,
	xfs_agnumber_t		end_agno)

{
	xfs_agnumber_t		agno;

	for (agno = first_agno; agno < end_agno; agno++) {
		struct xfs_perag	*pag = xa_erase(&mp->m_perags, agno);

		ASSERT(pag);
		XFS_IS_CORRUPT(pag->pag_mount, atomic_read(&pag->pag_ref) != 0);
		xfs_defer_drain_free(&pag->pag_intents_drain);

		cancel_delayed_work_sync(&pag->pag_blockgc_work);
		xfs_buf_cache_destroy(&pag->pag_bcache);

		/* drop the mount's active reference */
		xfs_perag_rele(pag);
		XFS_IS_CORRUPT(pag->pag_mount,
				atomic_read(&pag->pag_active_ref) != 0);
		kfree_rcu_mightsleep(pag);
	}
}

/* Find the size of the AG, in blocks. */
static xfs_agblock_t
__xfs_ag_block_count(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agnumber_t		agcount,
	xfs_rfsblock_t		dblocks)
{
	ASSERT(agno < agcount);

	if (agno < agcount - 1)
		return mp->m_sb.sb_agblocks;
	return dblocks - (agno * mp->m_sb.sb_agblocks);
}

xfs_agblock_t
xfs_ag_block_count(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	return __xfs_ag_block_count(mp, agno, mp->m_sb.sb_agcount,
			mp->m_sb.sb_dblocks);
}

/* Calculate the first and last possible inode number in an AG. */
static void
__xfs_agino_range(
	struct xfs_mount	*mp,
	xfs_agblock_t		eoag,
	xfs_agino_t		*first,
	xfs_agino_t		*last)
{
	xfs_agblock_t		bno;

	/*
	 * Calculate the first inode, which will be in the first
	 * cluster-aligned block after the AGFL.
	 */
	bno = round_up(XFS_AGFL_BLOCK(mp) + 1, M_IGEO(mp)->cluster_align);
	*first = XFS_AGB_TO_AGINO(mp, bno);

	/*
	 * Calculate the last inode, which will be at the end of the
	 * last (aligned) cluster that can be allocated in the AG.
	 */
	bno = round_down(eoag, M_IGEO(mp)->cluster_align);
	*last = XFS_AGB_TO_AGINO(mp, bno) - 1;
}

void
xfs_agino_range(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agino_t		*first,
	xfs_agino_t		*last)
{
	return __xfs_agino_range(mp, xfs_ag_block_count(mp, agno), first, last);
}

int
xfs_update_last_ag_size(
	struct xfs_mount	*mp,
	xfs_agnumber_t		prev_agcount)
{
	struct xfs_perag	*pag = xfs_perag_grab(mp, prev_agcount - 1);

	if (!pag)
		return -EFSCORRUPTED;
	pag->block_count = __xfs_ag_block_count(mp, prev_agcount - 1,
			mp->m_sb.sb_agcount, mp->m_sb.sb_dblocks);
	__xfs_agino_range(mp, pag->block_count, &pag->agino_min,
			&pag->agino_max);
	xfs_perag_rele(pag);
	return 0;
}

int
xfs_initialize_perag(
	struct xfs_mount	*mp,
	xfs_agnumber_t		old_agcount,
	xfs_agnumber_t		new_agcount,
	xfs_rfsblock_t		dblocks,
	xfs_agnumber_t		*maxagi)
{
	struct xfs_perag	*pag;
	xfs_agnumber_t		index;
	int			error;

	for (index = old_agcount; index < new_agcount; index++) {
		pag = kzalloc(sizeof(*pag), GFP_KERNEL);
		if (!pag) {
			error = -ENOMEM;
			goto out_unwind_new_pags;
		}
		pag->pag_agno = index;
		pag->pag_mount = mp;

		error = xa_insert(&mp->m_perags, index, pag, GFP_KERNEL);
		if (error) {
			WARN_ON_ONCE(error == -EBUSY);
			goto out_free_pag;
		}

#ifdef __KERNEL__
		/* Place kernel structure only init below this point. */
		spin_lock_init(&pag->pag_ici_lock);
		spin_lock_init(&pag->pagb_lock);
		spin_lock_init(&pag->pag_state_lock);
		INIT_DELAYED_WORK(&pag->pag_blockgc_work, xfs_blockgc_worker);
		INIT_RADIX_TREE(&pag->pag_ici_root, GFP_ATOMIC);
		xfs_defer_drain_init(&pag->pag_intents_drain);
		init_waitqueue_head(&pag->pagb_wait);
		init_waitqueue_head(&pag->pag_active_wq);
		pag->pagb_count = 0;
		pag->pagb_tree = RB_ROOT;
		xfs_hooks_init(&pag->pag_rmap_update_hooks);
#endif /* __KERNEL__ */

		error = xfs_buf_cache_init(&pag->pag_bcache);
		if (error)
			goto out_remove_pag;

		/* Active ref owned by mount indicates AG is online. */
		atomic_set(&pag->pag_active_ref, 1);

		/*
		 * Pre-calculated geometry
		 */
		pag->block_count = __xfs_ag_block_count(mp, index, new_agcount,
				dblocks);
		pag->min_block = XFS_AGFL_BLOCK(mp);
		__xfs_agino_range(mp, pag->block_count, &pag->agino_min,
				&pag->agino_max);
	}

	index = xfs_set_inode_alloc(mp, new_agcount);

	if (maxagi)
		*maxagi = index;

	mp->m_ag_prealloc_blocks = xfs_prealloc_blocks(mp);
	return 0;

out_remove_pag:
	xfs_defer_drain_free(&pag->pag_intents_drain);
	pag = xa_erase(&mp->m_perags, index);
out_free_pag:
	kfree(pag);
out_unwind_new_pags:
	xfs_free_perag_range(mp, old_agcount, index);
	return error;
}

static int
xfs_get_aghdr_buf(
	struct xfs_mount	*mp,
	xfs_daddr_t		blkno,
	size_t			numblks,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;
	int			error;

	error = xfs_buf_get_uncached(mp->m_ddev_targp, numblks, 0, &bp);
	if (error)
		return error;

	bp->b_maps[0].bm_bn = blkno;
	bp->b_ops = ops;

	*bpp = bp;
	return 0;
}

/*
 * Generic btree root block init function
 */
static void
xfs_btroot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	xfs_btree_init_buf(mp, bp, id->bc_ops, 0, 0, id->agno);
}

/* Finish initializing a free space btree. */
static void
xfs_freesp_init_recs(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_alloc_rec	*arec;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);

	arec = XFS_ALLOC_REC_ADDR(mp, XFS_BUF_TO_BLOCK(bp), 1);
	arec->ar_startblock = cpu_to_be32(mp->m_ag_prealloc_blocks);

	if (xfs_ag_contains_log(mp, id->agno)) {
		struct xfs_alloc_rec	*nrec;
		xfs_agblock_t		start = XFS_FSB_TO_AGBNO(mp,
							mp->m_sb.sb_logstart);

		ASSERT(start >= mp->m_ag_prealloc_blocks);
		if (start != mp->m_ag_prealloc_blocks) {
			/*
			 * Modify first record to pad stripe align of log and
			 * bump the record count.
			 */
			arec->ar_blockcount = cpu_to_be32(start -
						mp->m_ag_prealloc_blocks);
			be16_add_cpu(&block->bb_numrecs, 1);
			nrec = arec + 1;

			/*
			 * Insert second record at start of internal log
			 * which then gets trimmed.
			 */
			nrec->ar_startblock = cpu_to_be32(
					be32_to_cpu(arec->ar_startblock) +
					be32_to_cpu(arec->ar_blockcount));
			arec = nrec;
		}
		/*
		 * Change record start to after the internal log
		 */
		be32_add_cpu(&arec->ar_startblock, mp->m_sb.sb_logblocks);
	}

	/*
	 * Calculate the block count of this record; if it is nonzero,
	 * increment the record count.
	 */
	arec->ar_blockcount = cpu_to_be32(id->agsize -
					  be32_to_cpu(arec->ar_startblock));
	if (arec->ar_blockcount)
		be16_add_cpu(&block->bb_numrecs, 1);
}

/*
 * bnobt/cntbt btree root block init functions
 */
static void
xfs_bnoroot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	xfs_btree_init_buf(mp, bp, id->bc_ops, 0, 0, id->agno);
	xfs_freesp_init_recs(mp, bp, id);
}

/*
 * Reverse map root block init
 */
static void
xfs_rmaproot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_rmap_rec	*rrec;

	xfs_btree_init_buf(mp, bp, id->bc_ops, 0, 4, id->agno);

	/*
	 * mark the AG header regions as static metadata The BNO
	 * btree block is the first block after the headers, so
	 * it's location defines the size of region the static
	 * metadata consumes.
	 *
	 * Note: unlike mkfs, we never have to account for log
	 * space when growing the data regions
	 */
	rrec = XFS_RMAP_REC_ADDR(block, 1);
	rrec->rm_startblock = 0;
	rrec->rm_blockcount = cpu_to_be32(XFS_BNO_BLOCK(mp));
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_FS);
	rrec->rm_offset = 0;

	/* account freespace btree root blocks */
	rrec = XFS_RMAP_REC_ADDR(block, 2);
	rrec->rm_startblock = cpu_to_be32(XFS_BNO_BLOCK(mp));
	rrec->rm_blockcount = cpu_to_be32(2);
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_AG);
	rrec->rm_offset = 0;

	/* account inode btree root blocks */
	rrec = XFS_RMAP_REC_ADDR(block, 3);
	rrec->rm_startblock = cpu_to_be32(XFS_IBT_BLOCK(mp));
	rrec->rm_blockcount = cpu_to_be32(XFS_RMAP_BLOCK(mp) -
					  XFS_IBT_BLOCK(mp));
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_INOBT);
	rrec->rm_offset = 0;

	/* account for rmap btree root */
	rrec = XFS_RMAP_REC_ADDR(block, 4);
	rrec->rm_startblock = cpu_to_be32(XFS_RMAP_BLOCK(mp));
	rrec->rm_blockcount = cpu_to_be32(1);
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_AG);
	rrec->rm_offset = 0;

	/* account for refc btree root */
	if (xfs_has_reflink(mp)) {
		rrec = XFS_RMAP_REC_ADDR(block, 5);
		rrec->rm_startblock = cpu_to_be32(xfs_refc_block(mp));
		rrec->rm_blockcount = cpu_to_be32(1);
		rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_REFC);
		rrec->rm_offset = 0;
		be16_add_cpu(&block->bb_numrecs, 1);
	}

	/* account for the log space */
	if (xfs_ag_contains_log(mp, id->agno)) {
		rrec = XFS_RMAP_REC_ADDR(block,
				be16_to_cpu(block->bb_numrecs) + 1);
		rrec->rm_startblock = cpu_to_be32(
				XFS_FSB_TO_AGBNO(mp, mp->m_sb.sb_logstart));
		rrec->rm_blockcount = cpu_to_be32(mp->m_sb.sb_logblocks);
		rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_LOG);
		rrec->rm_offset = 0;
		be16_add_cpu(&block->bb_numrecs, 1);
	}
}

/*
 * Initialise new secondary superblocks with the pre-grow geometry, but mark
 * them as "in progress" so we know they haven't yet been activated. This will
 * get cleared when the update with the new geometry information is done after
 * changes to the primary are committed. This isn't strictly necessary, but we
 * get it for free with the delayed buffer write lists and it means we can tell
 * if a grow operation didn't complete properly after the fact.
 */
static void
xfs_sbblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_dsb		*dsb = bp->b_addr;

	xfs_sb_to_disk(dsb, &mp->m_sb);
	dsb->sb_inprogress = 1;
}

static void
xfs_agfblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_agf		*agf = bp->b_addr;
	xfs_extlen_t		tmpsize;

	agf->agf_magicnum = cpu_to_be32(XFS_AGF_MAGIC);
	agf->agf_versionnum = cpu_to_be32(XFS_AGF_VERSION);
	agf->agf_seqno = cpu_to_be32(id->agno);
	agf->agf_length = cpu_to_be32(id->agsize);
	agf->agf_bno_root = cpu_to_be32(XFS_BNO_BLOCK(mp));
	agf->agf_cnt_root = cpu_to_be32(XFS_CNT_BLOCK(mp));
	agf->agf_bno_level = cpu_to_be32(1);
	agf->agf_cnt_level = cpu_to_be32(1);
	if (xfs_has_rmapbt(mp)) {
		agf->agf_rmap_root = cpu_to_be32(XFS_RMAP_BLOCK(mp));
		agf->agf_rmap_level = cpu_to_be32(1);
		agf->agf_rmap_blocks = cpu_to_be32(1);
	}

	agf->agf_flfirst = cpu_to_be32(1);
	agf->agf_fllast = 0;
	agf->agf_flcount = 0;
	tmpsize = id->agsize - mp->m_ag_prealloc_blocks;
	agf->agf_freeblks = cpu_to_be32(tmpsize);
	agf->agf_longest = cpu_to_be32(tmpsize);
	if (xfs_has_crc(mp))
		uuid_copy(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid);
	if (xfs_has_reflink(mp)) {
		agf->agf_refcount_root = cpu_to_be32(
				xfs_refc_block(mp));
		agf->agf_refcount_level = cpu_to_be32(1);
		agf->agf_refcount_blocks = cpu_to_be32(1);
	}

	if (xfs_ag_contains_log(mp, id->agno)) {
		int64_t	logblocks = mp->m_sb.sb_logblocks;

		be32_add_cpu(&agf->agf_freeblks, -logblocks);
		agf->agf_longest = cpu_to_be32(id->agsize -
			XFS_FSB_TO_AGBNO(mp, mp->m_sb.sb_logstart) - logblocks);
	}
}

static void
xfs_agflblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_agfl		*agfl = XFS_BUF_TO_AGFL(bp);
	__be32			*agfl_bno;
	int			bucket;

	if (xfs_has_crc(mp)) {
		agfl->agfl_magicnum = cpu_to_be32(XFS_AGFL_MAGIC);
		agfl->agfl_seqno = cpu_to_be32(id->agno);
		uuid_copy(&agfl->agfl_uuid, &mp->m_sb.sb_meta_uuid);
	}

	agfl_bno = xfs_buf_to_agfl_bno(bp);
	for (bucket = 0; bucket < xfs_agfl_size(mp); bucket++)
		agfl_bno[bucket] = cpu_to_be32(NULLAGBLOCK);
}

static void
xfs_agiblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_agi		*agi = bp->b_addr;
	int			bucket;

	agi->agi_magicnum = cpu_to_be32(XFS_AGI_MAGIC);
	agi->agi_versionnum = cpu_to_be32(XFS_AGI_VERSION);
	agi->agi_seqno = cpu_to_be32(id->agno);
	agi->agi_length = cpu_to_be32(id->agsize);
	agi->agi_count = 0;
	agi->agi_root = cpu_to_be32(XFS_IBT_BLOCK(mp));
	agi->agi_level = cpu_to_be32(1);
	agi->agi_freecount = 0;
	agi->agi_newino = cpu_to_be32(NULLAGINO);
	agi->agi_dirino = cpu_to_be32(NULLAGINO);
	if (xfs_has_crc(mp))
		uuid_copy(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid);
	if (xfs_has_finobt(mp)) {
		agi->agi_free_root = cpu_to_be32(XFS_FIBT_BLOCK(mp));
		agi->agi_free_level = cpu_to_be32(1);
	}
	for (bucket = 0; bucket < XFS_AGI_UNLINKED_BUCKETS; bucket++)
		agi->agi_unlinked[bucket] = cpu_to_be32(NULLAGINO);
	if (xfs_has_inobtcounts(mp)) {
		agi->agi_iblocks = cpu_to_be32(1);
		if (xfs_has_finobt(mp))
			agi->agi_fblocks = cpu_to_be32(1);
	}
}

typedef void (*aghdr_init_work_f)(struct xfs_mount *mp, struct xfs_buf *bp,
				  struct aghdr_init_data *id);
static int
xfs_ag_init_hdr(
	struct xfs_mount	*mp,
	struct aghdr_init_data	*id,
	aghdr_init_work_f	work,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;
	int			error;

	error = xfs_get_aghdr_buf(mp, id->daddr, id->numblks, &bp, ops);
	if (error)
		return error;

	(*work)(mp, bp, id);

	xfs_buf_delwri_queue(bp, &id->buffer_list);
	xfs_buf_relse(bp);
	return 0;
}

struct xfs_aghdr_grow_data {
	xfs_daddr_t		daddr;
	size_t			numblks;
	const struct xfs_buf_ops *ops;
	aghdr_init_work_f	work;
	const struct xfs_btree_ops *bc_ops;
	bool			need_init;
};

/*
 * Prepare new AG headers to be written to disk. We use uncached buffers here,
 * as it is assumed these new AG headers are currently beyond the currently
 * valid filesystem address space. Using cached buffers would trip over EOFS
 * corruption detection alogrithms in the buffer cache lookup routines.
 *
 * This is a non-transactional function, but the prepared buffers are added to a
 * delayed write buffer list supplied by the caller so they can submit them to
 * disk and wait on them as required.
 */
int
xfs_ag_init_headers(
	struct xfs_mount	*mp,
	struct aghdr_init_data	*id)

{
	struct xfs_aghdr_grow_data aghdr_data[] = {
	{ /* SB */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_SB_DADDR),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_sb_buf_ops,
		.work = &xfs_sbblock_init,
		.need_init = true
	},
	{ /* AGF */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_AGF_DADDR(mp)),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_agf_buf_ops,
		.work = &xfs_agfblock_init,
		.need_init = true
	},
	{ /* AGFL */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_AGFL_DADDR(mp)),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_agfl_buf_ops,
		.work = &xfs_agflblock_init,
		.need_init = true
	},
	{ /* AGI */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_AGI_DADDR(mp)),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_agi_buf_ops,
		.work = &xfs_agiblock_init,
		.need_init = true
	},
	{ /* BNO root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_BNO_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_bnobt_buf_ops,
		.work = &xfs_bnoroot_init,
		.bc_ops = &xfs_bnobt_ops,
		.need_init = true
	},
	{ /* CNT root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_CNT_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_cntbt_buf_ops,
		.work = &xfs_bnoroot_init,
		.bc_ops = &xfs_cntbt_ops,
		.need_init = true
	},
	{ /* INO root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_IBT_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_inobt_buf_ops,
		.work = &xfs_btroot_init,
		.bc_ops = &xfs_inobt_ops,
		.need_init = true
	},
	{ /* FINO root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_FIBT_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_finobt_buf_ops,
		.work = &xfs_btroot_init,
		.bc_ops = &xfs_finobt_ops,
		.need_init =  xfs_has_finobt(mp)
	},
	{ /* RMAP root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_RMAP_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_rmapbt_buf_ops,
		.work = &xfs_rmaproot_init,
		.bc_ops = &xfs_rmapbt_ops,
		.need_init = xfs_has_rmapbt(mp)
	},
	{ /* REFC root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, xfs_refc_block(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_refcountbt_buf_ops,
		.work = &xfs_btroot_init,
		.bc_ops = &xfs_refcountbt_ops,
		.need_init = xfs_has_reflink(mp)
	},
	{ /* NULL terminating block */
		.daddr = XFS_BUF_DADDR_NULL,
	}
	};
	struct  xfs_aghdr_grow_data *dp;
	int			error = 0;

	/* Account for AG free space in new AG */
	id->nfree += id->agsize - mp->m_ag_prealloc_blocks;
	for (dp = &aghdr_data[0]; dp->daddr != XFS_BUF_DADDR_NULL; dp++) {
		if (!dp->need_init)
			continue;

		id->daddr = dp->daddr;
		id->numblks = dp->numblks;
		id->bc_ops = dp->bc_ops;
		error = xfs_ag_init_hdr(mp, id, dp->work, dp->ops);
		if (error)
			break;
	}
	return error;
}

int
xfs_ag_shrink_space(
	struct xfs_perag	*pag,
	struct xfs_trans	**tpp,
	xfs_extlen_t		delta)
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_alloc_arg	args = {
		.tp	= *tpp,
		.mp	= mp,
		.pag	= pag,
		.minlen = delta,
		.maxlen = delta,
		.oinfo	= XFS_RMAP_OINFO_SKIP_UPDATE,
		.resv	= XFS_AG_RESV_NONE,
		.prod	= 1
	};
	struct xfs_buf		*agibp, *agfbp;
	struct xfs_agi		*agi;
	struct xfs_agf		*agf;
	xfs_agblock_t		aglen;
	int			error, err2;

	ASSERT(pag->pag_agno == mp->m_sb.sb_agcount - 1);
	error = xfs_ialloc_read_agi(pag, *tpp, 0, &agibp);
	if (error)
		return error;

	agi = agibp->b_addr;

	error = xfs_alloc_read_agf(pag, *tpp, 0, &agfbp);
	if (error)
		return error;

	agf = agfbp->b_addr;
	aglen = be32_to_cpu(agi->agi_length);
	/* some extra paranoid checks before we shrink the ag */
	if (XFS_IS_CORRUPT(mp, agf->agf_length != agi->agi_length)) {
		xfs_ag_mark_sick(pag, XFS_SICK_AG_AGF);
		return -EFSCORRUPTED;
	}
	if (delta >= aglen)
		return -EINVAL;

	/*
	 * Make sure that the last inode cluster cannot overlap with the new
	 * end of the AG, even if it's sparse.
	 */
	error = xfs_ialloc_check_shrink(pag, *tpp, agibp, aglen - delta);
	if (error)
		return error;

	/*
	 * Disable perag reservations so it doesn't cause the allocation request
	 * to fail. We'll reestablish reservation before we return.
	 */
	xfs_ag_resv_free(pag);

	/* internal log shouldn't also show up in the free space btrees */
	error = xfs_alloc_vextent_exact_bno(&args,
			XFS_AGB_TO_FSB(mp, pag->pag_agno, aglen - delta));
	if (!error && args.agbno == NULLAGBLOCK)
		error = -ENOSPC;

	if (error) {
		/*
		 * If extent allocation fails, need to roll the transaction to
		 * ensure that the AGFL fixup has been committed anyway.
		 *
		 * We need to hold the AGF across the roll to ensure nothing can
		 * access the AG for allocation until the shrink is fully
		 * cleaned up. And due to the resetting of the AG block
		 * reservation space needing to lock the AGI, we also have to
		 * hold that so we don't get AGI/AGF lock order inversions in
		 * the error handling path.
		 */
		xfs_trans_bhold(*tpp, agfbp);
		xfs_trans_bhold(*tpp, agibp);
		err2 = xfs_trans_roll(tpp);
		if (err2)
			return err2;
		xfs_trans_bjoin(*tpp, agfbp);
		xfs_trans_bjoin(*tpp, agibp);
		goto resv_init_out;
	}

	/*
	 * if successfully deleted from freespace btrees, need to confirm
	 * per-AG reservation works as expected.
	 */
	be32_add_cpu(&agi->agi_length, -delta);
	be32_add_cpu(&agf->agf_length, -delta);

	err2 = xfs_ag_resv_init(pag, *tpp);
	if (err2) {
		be32_add_cpu(&agi->agi_length, delta);
		be32_add_cpu(&agf->agf_length, delta);
		if (err2 != -ENOSPC)
			goto resv_err;

		err2 = xfs_free_extent_later(*tpp, args.fsbno, delta, NULL,
				XFS_AG_RESV_NONE, XFS_FREE_EXTENT_SKIP_DISCARD);
		if (err2)
			goto resv_err;

		/*
		 * Roll the transaction before trying to re-init the per-ag
		 * reservation. The new transaction is clean so it will cancel
		 * without any side effects.
		 */
		error = xfs_defer_finish(tpp);
		if (error)
			return error;

		error = -ENOSPC;
		goto resv_init_out;
	}

	/* Update perag geometry */
	pag->block_count -= delta;
	__xfs_agino_range(pag->pag_mount, pag->block_count, &pag->agino_min,
				&pag->agino_max);

	xfs_ialloc_log_agi(*tpp, agibp, XFS_AGI_LENGTH);
	xfs_alloc_log_agf(*tpp, agfbp, XFS_AGF_LENGTH);
	return 0;

resv_init_out:
	err2 = xfs_ag_resv_init(pag, *tpp);
	if (!err2)
		return error;
resv_err:
	xfs_warn(mp, "Error %d reserving per-AG metadata reserve pool.", err2);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
	return err2;
}

/*
 * Extent the AG indicated by the @id by the length passed in
 */
int
xfs_ag_extend_space(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_extlen_t		len)
{
	struct xfs_buf		*bp;
	struct xfs_agi		*agi;
	struct xfs_agf		*agf;
	int			error;

	ASSERT(pag->pag_agno == pag->pag_mount->m_sb.sb_agcount - 1);

	error = xfs_ialloc_read_agi(pag, tp, 0, &bp);
	if (error)
		return error;

	agi = bp->b_addr;
	be32_add_cpu(&agi->agi_length, len);
	xfs_ialloc_log_agi(tp, bp, XFS_AGI_LENGTH);

	/*
	 * Change agf length.
	 */
	error = xfs_alloc_read_agf(pag, tp, 0, &bp);
	if (error)
		return error;

	agf = bp->b_addr;
	be32_add_cpu(&agf->agf_length, len);
	ASSERT(agf->agf_length == agi->agi_length);
	xfs_alloc_log_agf(tp, bp, XFS_AGF_LENGTH);

	/*
	 * Free the new space.
	 *
	 * XFS_RMAP_OINFO_SKIP_UPDATE is used here to tell the rmap btree that
	 * this doesn't actually exist in the rmap btree.
	 */
	error = xfs_rmap_free(tp, bp, pag, be32_to_cpu(agf->agf_length) - len,
				len, &XFS_RMAP_OINFO_SKIP_UPDATE);
	if (error)
		return error;

	error = xfs_free_extent(tp, pag, be32_to_cpu(agf->agf_length) - len,
			len, &XFS_RMAP_OINFO_SKIP_UPDATE, XFS_AG_RESV_NONE);
	if (error)
		return error;

	/* Update perag geometry */
	pag->block_count = be32_to_cpu(agf->agf_length);
	__xfs_agino_range(pag->pag_mount, pag->block_count, &pag->agino_min,
				&pag->agino_max);
	return 0;
}

/* Retrieve AG geometry. */
int
xfs_ag_get_geometry(
	struct xfs_perag	*pag,
	struct xfs_ag_geometry	*ageo)
{
	struct xfs_buf		*agi_bp;
	struct xfs_buf		*agf_bp;
	struct xfs_agi		*agi;
	struct xfs_agf		*agf;
	unsigned int		freeblks;
	int			error;

	/* Lock the AG headers. */
	error = xfs_ialloc_read_agi(pag, NULL, 0, &agi_bp);
	if (error)
		return error;
	error = xfs_alloc_read_agf(pag, NULL, 0, &agf_bp);
	if (error)
		goto out_agi;

	/* Fill out form. */
	memset(ageo, 0, sizeof(*ageo));
	ageo->ag_number = pag->pag_agno;

	agi = agi_bp->b_addr;
	ageo->ag_icount = be32_to_cpu(agi->agi_count);
	ageo->ag_ifree = be32_to_cpu(agi->agi_freecount);

	agf = agf_bp->b_addr;
	ageo->ag_length = be32_to_cpu(agf->agf_length);
	freeblks = pag->pagf_freeblks +
		   pag->pagf_flcount +
		   pag->pagf_btreeblks -
		   xfs_ag_resv_needed(pag, XFS_AG_RESV_NONE);
	ageo->ag_freeblks = freeblks;
	xfs_ag_geom_health(pag, ageo);

	/* Release resources. */
	xfs_buf_relse(agf_bp);
out_agi:
	xfs_buf_relse(agi_bp);
	return error;
}
