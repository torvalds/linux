// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_btree_staging.h"
#include "xfs_buf_mem.h"
#include "xfs_btree_mem.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_quota.h"
#include "xfs_rtalloc.h"
#include "xfs_ag.h"
#include "xfs_rtgroup.h"
#include "xfs_refcount.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/fsb_bitmap.h"
#include "scrub/rgb_bitmap.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/iscan.h"
#include "scrub/newbt.h"
#include "scrub/reap.h"

/*
 * Realtime Reverse Mapping Btree Repair
 * =====================================
 *
 * This isn't quite as difficult as repairing the rmap btree on the data
 * device, since we only store the data fork extents of realtime files on the
 * realtime device.  We still have to freeze the filesystem and stop the
 * background threads like we do for the rmap repair, but we only have to scan
 * realtime inodes.
 *
 * Collecting entries for the new realtime rmap btree is easy -- all we have
 * to do is generate rtrmap entries from the data fork mappings of all realtime
 * files in the filesystem.  We then scan the rmap btrees of the data device
 * looking for extents belonging to the old btree and note them in a bitmap.
 *
 * To rebuild the realtime rmap btree, we bulk-load the collected mappings into
 * a new btree cursor and atomically swap that into the realtime inode.  Then
 * we can free the blocks from the old btree.
 *
 * We use the 'xrep_rtrmap' prefix for all the rmap functions.
 */

/* Context for collecting rmaps */
struct xrep_rtrmap {
	/* new rtrmapbt information */
	struct xrep_newbt	new_btree;

	/* lock for the xfbtree and xfile */
	struct mutex		lock;

	/* rmap records generated from primary metadata */
	struct xfbtree		rtrmap_btree;

	struct xfs_scrub	*sc;

	/* bitmap of old rtrmapbt blocks */
	struct xfsb_bitmap	old_rtrmapbt_blocks;

	/* Hooks into rtrmap update code. */
	struct xfs_rmap_hook	rhook;

	/* inode scan cursor */
	struct xchk_iscan	iscan;

	/* in-memory btree cursor for the ->get_blocks walk */
	struct xfs_btree_cur	*mcur;

	/* Number of records we're staging in the new btree. */
	uint64_t		nr_records;
};

/* Set us up to repair rt reverse mapping btrees. */
int
xrep_setup_rtrmapbt(
	struct xfs_scrub	*sc)
{
	struct xrep_rtrmap	*rr;
	char			*descr;
	int			error;

	xchk_fsgates_enable(sc, XCHK_FSGATES_RMAP);

	descr = xchk_xfile_rtgroup_descr(sc, "reverse mapping records");
	error = xrep_setup_xfbtree(sc, descr);
	kfree(descr);
	if (error)
		return error;

	rr = kzalloc(sizeof(struct xrep_rtrmap), XCHK_GFP_FLAGS);
	if (!rr)
		return -ENOMEM;

	rr->sc = sc;
	sc->buf = rr;
	return 0;
}

/* Make sure there's nothing funny about this mapping. */
STATIC int
xrep_rtrmap_check_mapping(
	struct xfs_scrub	*sc,
	const struct xfs_rmap_irec *rec)
{
	if (xfs_rtrmap_check_irec(sc->sr.rtg, rec) != NULL)
		return -EFSCORRUPTED;

	/* Make sure this isn't free space. */
	return xrep_require_rtext_inuse(sc, rec->rm_startblock,
			rec->rm_blockcount);
}

/* Store a reverse-mapping record. */
static inline int
xrep_rtrmap_stash(
	struct xrep_rtrmap	*rr,
	xfs_rgblock_t		startblock,
	xfs_extlen_t		blockcount,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags)
{
	struct xfs_rmap_irec	rmap = {
		.rm_startblock	= startblock,
		.rm_blockcount	= blockcount,
		.rm_owner	= owner,
		.rm_offset	= offset,
		.rm_flags	= flags,
	};
	struct xfs_scrub	*sc = rr->sc;
	struct xfs_btree_cur	*mcur;
	int			error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	if (xchk_iscan_aborted(&rr->iscan))
		return -EFSCORRUPTED;

	trace_xrep_rtrmap_found(sc->mp, &rmap);

	/* Add entry to in-memory btree. */
	mutex_lock(&rr->lock);
	mcur = xfs_rtrmapbt_mem_cursor(sc->sr.rtg, sc->tp, &rr->rtrmap_btree);
	error = xfs_rmap_map_raw(mcur, &rmap);
	xfs_btree_del_cursor(mcur, error);
	if (error)
		goto out_cancel;

	error = xfbtree_trans_commit(&rr->rtrmap_btree, sc->tp);
	if (error)
		goto out_abort;

	mutex_unlock(&rr->lock);
	return 0;

out_cancel:
	xfbtree_trans_cancel(&rr->rtrmap_btree, sc->tp);
out_abort:
	xchk_iscan_abort(&rr->iscan);
	mutex_unlock(&rr->lock);
	return error;
}

/* Finding all file and bmbt extents. */

/* Context for accumulating rmaps for an inode fork. */
struct xrep_rtrmap_ifork {
	/*
	 * Accumulate rmap data here to turn multiple adjacent bmaps into a
	 * single rmap.
	 */
	struct xfs_rmap_irec	accum;

	struct xrep_rtrmap	*rr;
};

/* Stash an rmap that we accumulated while walking an inode fork. */
STATIC int
xrep_rtrmap_stash_accumulated(
	struct xrep_rtrmap_ifork	*rf)
{
	if (rf->accum.rm_blockcount == 0)
		return 0;

	return xrep_rtrmap_stash(rf->rr, rf->accum.rm_startblock,
			rf->accum.rm_blockcount, rf->accum.rm_owner,
			rf->accum.rm_offset, rf->accum.rm_flags);
}

/* Accumulate a bmbt record. */
STATIC int
xrep_rtrmap_visit_bmbt(
	struct xfs_btree_cur	*cur,
	struct xfs_bmbt_irec	*rec,
	void			*priv)
{
	struct xrep_rtrmap_ifork *rf = priv;
	struct xfs_rmap_irec	*accum = &rf->accum;
	struct xfs_mount	*mp = rf->rr->sc->mp;
	xfs_rgblock_t		rgbno;
	unsigned int		rmap_flags = 0;
	int			error;

	if (xfs_rtb_to_rgno(mp, rec->br_startblock) !=
	    rtg_rgno(rf->rr->sc->sr.rtg))
		return 0;

	if (rec->br_state == XFS_EXT_UNWRITTEN)
		rmap_flags |= XFS_RMAP_UNWRITTEN;

	/* If this bmap is adjacent to the previous one, just add it. */
	rgbno = xfs_rtb_to_rgbno(mp, rec->br_startblock);
	if (accum->rm_blockcount > 0 &&
	    rec->br_startoff == accum->rm_offset + accum->rm_blockcount &&
	    rgbno == accum->rm_startblock + accum->rm_blockcount &&
	    rmap_flags == accum->rm_flags) {
		accum->rm_blockcount += rec->br_blockcount;
		return 0;
	}

	/* Otherwise stash the old rmap and start accumulating a new one. */
	error = xrep_rtrmap_stash_accumulated(rf);
	if (error)
		return error;

	accum->rm_startblock = rgbno;
	accum->rm_blockcount = rec->br_blockcount;
	accum->rm_offset = rec->br_startoff;
	accum->rm_flags = rmap_flags;
	return 0;
}

/*
 * Iterate the block mapping btree to collect rmap records for anything in this
 * fork that maps to the rt volume.  Sets @mappings_done to true if we've
 * scanned the block mappings in this fork.
 */
STATIC int
xrep_rtrmap_scan_bmbt(
	struct xrep_rtrmap_ifork *rf,
	struct xfs_inode	*ip,
	bool			*mappings_done)
{
	struct xrep_rtrmap	*rr = rf->rr;
	struct xfs_btree_cur	*cur;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	int			error = 0;

	*mappings_done = false;

	/*
	 * If the incore extent cache is already loaded, we'll just use the
	 * incore extent scanner to record mappings.  Don't bother walking the
	 * ondisk extent tree.
	 */
	if (!xfs_need_iread_extents(ifp))
		return 0;

	/* Accumulate all the mappings in the bmap btree. */
	cur = xfs_bmbt_init_cursor(rr->sc->mp, rr->sc->tp, ip, XFS_DATA_FORK);
	error = xfs_bmap_query_all(cur, xrep_rtrmap_visit_bmbt, rf);
	xfs_btree_del_cursor(cur, error);
	if (error)
		return error;

	/* Stash any remaining accumulated rmaps and exit. */
	*mappings_done = true;
	return xrep_rtrmap_stash_accumulated(rf);
}

/*
 * Iterate the in-core extent cache to collect rmap records for anything in
 * this fork that matches the AG.
 */
STATIC int
xrep_rtrmap_scan_iext(
	struct xrep_rtrmap_ifork *rf,
	struct xfs_ifork	*ifp)
{
	struct xfs_bmbt_irec	rec;
	struct xfs_iext_cursor	icur;
	int			error;

	for_each_xfs_iext(ifp, &icur, &rec) {
		if (isnullstartblock(rec.br_startblock))
			continue;
		error = xrep_rtrmap_visit_bmbt(NULL, &rec, rf);
		if (error)
			return error;
	}

	return xrep_rtrmap_stash_accumulated(rf);
}

/* Find all the extents on the realtime device mapped by an inode fork. */
STATIC int
xrep_rtrmap_scan_dfork(
	struct xrep_rtrmap	*rr,
	struct xfs_inode	*ip)
{
	struct xrep_rtrmap_ifork rf = {
		.accum		= { .rm_owner = ip->i_ino, },
		.rr		= rr,
	};
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	int			error = 0;

	if (ifp->if_format == XFS_DINODE_FMT_BTREE) {
		bool		mappings_done;

		/*
		 * Scan the bmbt for mappings.  If the incore extent tree is
		 * loaded, we want to scan the cached mappings since that's
		 * faster when the extent counts are very high.
		 */
		error = xrep_rtrmap_scan_bmbt(&rf, ip, &mappings_done);
		if (error || mappings_done)
			return error;
	} else if (ifp->if_format != XFS_DINODE_FMT_EXTENTS) {
		/* realtime data forks should only be extents or btree */
		return -EFSCORRUPTED;
	}

	/* Scan incore extent cache. */
	return xrep_rtrmap_scan_iext(&rf, ifp);
}

/* Record reverse mappings for a file. */
STATIC int
xrep_rtrmap_scan_inode(
	struct xrep_rtrmap	*rr,
	struct xfs_inode	*ip)
{
	unsigned int		lock_mode;
	int			error = 0;

	/* Skip the rt rmap btree inode. */
	if (rr->sc->ip == ip)
		return 0;

	lock_mode = xfs_ilock_data_map_shared(ip);

	/* Check the data fork if it's on the realtime device. */
	if (XFS_IS_REALTIME_INODE(ip)) {
		error = xrep_rtrmap_scan_dfork(rr, ip);
		if (error)
			goto out_unlock;
	}

	xchk_iscan_mark_visited(&rr->iscan, ip);
out_unlock:
	xfs_iunlock(ip, lock_mode);
	return error;
}

/* Record extents that belong to the realtime rmap inode. */
STATIC int
xrep_rtrmap_walk_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_rtrmap		*rr = priv;
	int				error = 0;

	if (xchk_should_terminate(rr->sc, &error))
		return error;

	/* Skip extents which are not owned by this inode and fork. */
	if (rec->rm_owner != rr->sc->ip->i_ino)
		return 0;

	error = xrep_check_ino_btree_mapping(rr->sc, rec);
	if (error)
		return error;

	return xfsb_bitmap_set(&rr->old_rtrmapbt_blocks,
			xfs_gbno_to_fsb(cur->bc_group, rec->rm_startblock),
			rec->rm_blockcount);
}

/* Scan one AG for reverse mappings for the realtime rmap btree. */
STATIC int
xrep_rtrmap_scan_ag(
	struct xrep_rtrmap	*rr,
	struct xfs_perag	*pag)
{
	struct xfs_scrub	*sc = rr->sc;
	int			error;

	error = xrep_ag_init(sc, pag, &sc->sa);
	if (error)
		return error;

	error = xfs_rmap_query_all(sc->sa.rmap_cur, xrep_rtrmap_walk_rmap, rr);
	xchk_ag_free(sc, &sc->sa);
	return error;
}

struct xrep_rtrmap_stash_run {
	struct xrep_rtrmap	*rr;
	uint64_t		owner;
};

static int
xrep_rtrmap_stash_run(
	uint32_t			start,
	uint32_t			len,
	void				*priv)
{
	struct xrep_rtrmap_stash_run	*rsr = priv;
	struct xrep_rtrmap		*rr = rsr->rr;
	xfs_rgblock_t			rgbno = start;

	return xrep_rtrmap_stash(rr, rgbno, len, rsr->owner, 0, 0);
}

/*
 * Emit rmaps for every extent of bits set in the bitmap.  Caller must ensure
 * that the ranges are in units of FS blocks.
 */
STATIC int
xrep_rtrmap_stash_bitmap(
	struct xrep_rtrmap		*rr,
	struct xrgb_bitmap		*bitmap,
	const struct xfs_owner_info	*oinfo)
{
	struct xrep_rtrmap_stash_run	rsr = {
		.rr			= rr,
		.owner			= oinfo->oi_owner,
	};

	return xrgb_bitmap_walk(bitmap, xrep_rtrmap_stash_run, &rsr);
}

/* Record a CoW staging extent. */
STATIC int
xrep_rtrmap_walk_cowblocks(
	struct xfs_btree_cur		*cur,
	const struct xfs_refcount_irec	*irec,
	void				*priv)
{
	struct xrgb_bitmap		*bitmap = priv;

	if (!xfs_refcount_check_domain(irec) ||
	    irec->rc_domain != XFS_REFC_DOMAIN_COW)
		return -EFSCORRUPTED;

	return xrgb_bitmap_set(bitmap, irec->rc_startblock,
			irec->rc_blockcount);
}

/*
 * Collect rmaps for the blocks containing the refcount btree, and all CoW
 * staging extents.
 */
STATIC int
xrep_rtrmap_find_refcount_rmaps(
	struct xrep_rtrmap	*rr)
{
	struct xrgb_bitmap	cow_blocks;		/* COWBIT */
	struct xfs_refcount_irec low = {
		.rc_startblock	= 0,
		.rc_domain	= XFS_REFC_DOMAIN_COW,
	};
	struct xfs_refcount_irec high = {
		.rc_startblock	= -1U,
		.rc_domain	= XFS_REFC_DOMAIN_COW,
	};
	struct xfs_scrub	*sc = rr->sc;
	int			error;

	if (!xfs_has_rtreflink(sc->mp))
		return 0;

	xrgb_bitmap_init(&cow_blocks);

	/* Collect rmaps for CoW staging extents. */
	error = xfs_refcount_query_range(sc->sr.refc_cur, &low, &high,
			xrep_rtrmap_walk_cowblocks, &cow_blocks);
	if (error)
		goto out_bitmap;

	/* Generate rmaps for everything. */
	error = xrep_rtrmap_stash_bitmap(rr, &cow_blocks, &XFS_RMAP_OINFO_COW);
	if (error)
		goto out_bitmap;

out_bitmap:
	xrgb_bitmap_destroy(&cow_blocks);
	return error;
}

/* Count and check all collected records. */
STATIC int
xrep_rtrmap_check_record(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_rtrmap		*rr = priv;
	int				error;

	error = xrep_rtrmap_check_mapping(rr->sc, rec);
	if (error)
		return error;

	rr->nr_records++;
	return 0;
}

/* Generate all the reverse-mappings for the realtime device. */
STATIC int
xrep_rtrmap_find_rmaps(
	struct xrep_rtrmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	struct xfs_perag	*pag = NULL;
	struct xfs_inode	*ip;
	struct xfs_btree_cur	*mcur;
	int			error;

	/* Generate rmaps for the realtime superblock */
	if (xfs_has_rtsb(sc->mp) && rtg_rgno(rr->sc->sr.rtg) == 0) {
		error = xrep_rtrmap_stash(rr, 0, sc->mp->m_sb.sb_rextsize,
				XFS_RMAP_OWN_FS, 0, 0);
		if (error)
			return error;
	}

	/* Find CoW staging extents. */
	xrep_rtgroup_btcur_init(sc, &sc->sr);
	error = xrep_rtrmap_find_refcount_rmaps(rr);
	xchk_rtgroup_btcur_free(&sc->sr);
	if (error)
		return error;

	/*
	 * Set up for a potentially lengthy filesystem scan by reducing our
	 * transaction resource usage for the duration.  Specifically:
	 *
	 * Unlock the realtime metadata inodes and cancel the transaction to
	 * release the log grant space while we scan the filesystem.
	 *
	 * Create a new empty transaction to eliminate the possibility of the
	 * inode scan deadlocking on cyclical metadata.
	 *
	 * We pass the empty transaction to the file scanning function to avoid
	 * repeatedly cycling empty transactions.  This can be done even though
	 * we take the IOLOCK to quiesce the file because empty transactions
	 * do not take sb_internal.
	 */
	xchk_trans_cancel(sc);
	xchk_rtgroup_unlock(&sc->sr);
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	while ((error = xchk_iscan_iter(&rr->iscan, &ip)) == 1) {
		error = xrep_rtrmap_scan_inode(rr, ip);
		xchk_irele(sc, ip);
		if (error)
			break;

		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&rr->iscan);
	if (error)
		return error;

	/*
	 * Switch out for a real transaction and lock the RT metadata in
	 * preparation for building a new tree.
	 */
	xchk_trans_cancel(sc);
	error = xchk_setup_rt(sc);
	if (error)
		return error;
	error = xchk_rtgroup_lock(sc, &sc->sr, XCHK_RTGLOCK_ALL);
	if (error)
		return error;

	/*
	 * If a hook failed to update the in-memory btree, we lack the data to
	 * continue the repair.
	 */
	if (xchk_iscan_aborted(&rr->iscan))
		return -EFSCORRUPTED;

	/* Scan for old rtrmap blocks. */
	while ((pag = xfs_perag_next(sc->mp, pag))) {
		error = xrep_rtrmap_scan_ag(rr, pag);
		if (error) {
			xfs_perag_rele(pag);
			return error;
		}
	}

	/*
	 * Now that we have everything locked again, we need to count the
	 * number of rmap records stashed in the btree.  This should reflect
	 * all actively-owned rt files in the filesystem.  At the same time,
	 * check all our records before we start building a new btree, which
	 * requires the rtbitmap lock.
	 */
	mcur = xfs_rtrmapbt_mem_cursor(rr->sc->sr.rtg, NULL, &rr->rtrmap_btree);
	rr->nr_records = 0;
	error = xfs_rmap_query_all(mcur, xrep_rtrmap_check_record, rr);
	xfs_btree_del_cursor(mcur, error);

	return error;
}

/* Building the new rtrmap btree. */

/* Retrieve rtrmapbt data for bulk load. */
STATIC int
xrep_rtrmap_get_records(
	struct xfs_btree_cur		*cur,
	unsigned int			idx,
	struct xfs_btree_block		*block,
	unsigned int			nr_wanted,
	void				*priv)
{
	struct xrep_rtrmap		*rr = priv;
	union xfs_btree_rec		*block_rec;
	unsigned int			loaded;
	int				error;

	for (loaded = 0; loaded < nr_wanted; loaded++, idx++) {
		int			stat = 0;

		error = xfs_btree_increment(rr->mcur, 0, &stat);
		if (error)
			return error;
		if (!stat)
			return -EFSCORRUPTED;

		error = xfs_rmap_get_rec(rr->mcur, &cur->bc_rec.r, &stat);
		if (error)
			return error;
		if (!stat)
			return -EFSCORRUPTED;

		block_rec = xfs_btree_rec_addr(cur, idx, block);
		cur->bc_ops->init_rec_from_cur(cur, block_rec);
	}

	return loaded;
}

/* Feed one of the new btree blocks to the bulk loader. */
STATIC int
xrep_rtrmap_claim_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	void			*priv)
{
	struct xrep_rtrmap	*rr = priv;

	return xrep_newbt_claim_block(cur, &rr->new_btree, ptr);
}

/* Figure out how much space we need to create the incore btree root block. */
STATIC size_t
xrep_rtrmap_iroot_size(
	struct xfs_btree_cur	*cur,
	unsigned int		level,
	unsigned int		nr_this_level,
	void			*priv)
{
	return xfs_rtrmap_broot_space_calc(cur->bc_mp, level, nr_this_level);
}

/*
 * Use the collected rmap information to stage a new rmap btree.  If this is
 * successful we'll return with the new btree root information logged to the
 * repair transaction but not yet committed.  This implements section (III)
 * above.
 */
STATIC int
xrep_rtrmap_build_new_tree(
	struct xrep_rtrmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	struct xfs_rtgroup	*rtg = sc->sr.rtg;
	struct xfs_btree_cur	*rmap_cur;
	int			error;

	/*
	 * Prepare to construct the new btree by reserving disk space for the
	 * new btree and setting up all the accounting information we'll need
	 * to root the new btree while it's under construction and before we
	 * attach it to the realtime rmapbt inode.
	 */
	error = xrep_newbt_init_metadir_inode(&rr->new_btree, sc);
	if (error)
		return error;

	rr->new_btree.bload.get_records = xrep_rtrmap_get_records;
	rr->new_btree.bload.claim_block = xrep_rtrmap_claim_block;
	rr->new_btree.bload.iroot_size = xrep_rtrmap_iroot_size;

	rmap_cur = xfs_rtrmapbt_init_cursor(NULL, rtg);
	xfs_btree_stage_ifakeroot(rmap_cur, &rr->new_btree.ifake);

	/* Compute how many blocks we'll need for the rmaps collected. */
	error = xfs_btree_bload_compute_geometry(rmap_cur,
			&rr->new_btree.bload, rr->nr_records);
	if (error)
		goto err_cur;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto err_cur;

	/*
	 * Guess how many blocks we're going to need to rebuild an entire
	 * rtrmapbt from the number of extents we found, and pump up our
	 * transaction to have sufficient block reservation.  We're allowed
	 * to exceed quota to repair inconsistent metadata, though this is
	 * unlikely.
	 */
	error = xfs_trans_reserve_more_inode(sc->tp, rtg_rmap(rtg),
			rr->new_btree.bload.nr_blocks, 0, true);
	if (error)
		goto err_cur;

	/* Reserve the space we'll need for the new btree. */
	error = xrep_newbt_alloc_blocks(&rr->new_btree,
			rr->new_btree.bload.nr_blocks);
	if (error)
		goto err_cur;

	/*
	 * Create a cursor to the in-memory btree so that we can bulk load the
	 * new btree.
	 */
	rr->mcur = xfs_rtrmapbt_mem_cursor(sc->sr.rtg, NULL, &rr->rtrmap_btree);
	error = xfs_btree_goto_left_edge(rr->mcur);
	if (error)
		goto err_mcur;

	/* Add all observed rmap records. */
	rr->new_btree.ifake.if_fork->if_format = XFS_DINODE_FMT_META_BTREE;
	error = xfs_btree_bload(rmap_cur, &rr->new_btree.bload, rr);
	if (error)
		goto err_mcur;

	/*
	 * Install the new rtrmap btree in the inode.  After this point the old
	 * btree is no longer accessible, the new tree is live, and we can
	 * delete the cursor.
	 */
	xfs_rtrmapbt_commit_staged_btree(rmap_cur, sc->tp);
	xrep_inode_set_nblocks(rr->sc, rr->new_btree.ifake.if_blocks);
	xfs_btree_del_cursor(rmap_cur, 0);
	xfs_btree_del_cursor(rr->mcur, 0);
	rr->mcur = NULL;

	/*
	 * Now that we've written the new btree to disk, we don't need to keep
	 * updating the in-memory btree.  Abort the scan to stop live updates.
	 */
	xchk_iscan_abort(&rr->iscan);

	/* Dispose of any unused blocks and the accounting information. */
	error = xrep_newbt_commit(&rr->new_btree);
	if (error)
		return error;

	return xrep_roll_trans(sc);

err_mcur:
	xfs_btree_del_cursor(rr->mcur, error);
err_cur:
	xfs_btree_del_cursor(rmap_cur, error);
	xrep_newbt_cancel(&rr->new_btree);
	return error;
}

/* Reaping the old btree. */

/* Reap the old rtrmapbt blocks. */
STATIC int
xrep_rtrmap_remove_old_tree(
	struct xrep_rtrmap	*rr)
{
	int			error;

	/*
	 * Free all the extents that were allocated to the former rtrmapbt and
	 * aren't cross-linked with something else.
	 */
	error = xrep_reap_metadir_fsblocks(rr->sc, &rr->old_rtrmapbt_blocks);
	if (error)
		return error;

	/*
	 * Ensure the proper reservation for the rtrmap inode so that we don't
	 * fail to expand the new btree.
	 */
	return xrep_reset_metafile_resv(rr->sc);
}

static inline bool
xrep_rtrmapbt_want_live_update(
	struct xchk_iscan		*iscan,
	const struct xfs_owner_info	*oi)
{
	if (xchk_iscan_aborted(iscan))
		return false;

	/*
	 * We scanned the CoW staging extents before we started the iscan, so
	 * we need all the updates.
	 */
	if (XFS_RMAP_NON_INODE_OWNER(oi->oi_owner))
		return true;

	/* Ignore updates to files that the scanner hasn't visited yet. */
	return xchk_iscan_want_live_update(iscan, oi->oi_owner);
}

/*
 * Apply a rtrmapbt update from the regular filesystem into our shadow btree.
 * We're running from the thread that owns the rtrmap ILOCK and is generating
 * the update, so we must be careful about which parts of the struct
 * xrep_rtrmap that we change.
 */
static int
xrep_rtrmapbt_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_rmap_update_params	*p = data;
	struct xrep_rtrmap		*rr;
	struct xfs_mount		*mp;
	struct xfs_btree_cur		*mcur;
	struct xfs_trans		*tp;
	void				*txcookie;
	int				error;

	rr = container_of(nb, struct xrep_rtrmap, rhook.rmap_hook.nb);
	mp = rr->sc->mp;

	if (!xrep_rtrmapbt_want_live_update(&rr->iscan, &p->oinfo))
		goto out_unlock;

	trace_xrep_rmap_live_update(rtg_group(rr->sc->sr.rtg), action, p);

	error = xrep_trans_alloc_hook_dummy(mp, &txcookie, &tp);
	if (error)
		goto out_abort;

	mutex_lock(&rr->lock);
	mcur = xfs_rtrmapbt_mem_cursor(rr->sc->sr.rtg, tp, &rr->rtrmap_btree);
	error = __xfs_rmap_finish_intent(mcur, action, p->startblock,
			p->blockcount, &p->oinfo, p->unwritten);
	xfs_btree_del_cursor(mcur, error);
	if (error)
		goto out_cancel;

	error = xfbtree_trans_commit(&rr->rtrmap_btree, tp);
	if (error)
		goto out_cancel;

	xrep_trans_cancel_hook_dummy(&txcookie, tp);
	mutex_unlock(&rr->lock);
	return NOTIFY_DONE;

out_cancel:
	xfbtree_trans_cancel(&rr->rtrmap_btree, tp);
	xrep_trans_cancel_hook_dummy(&txcookie, tp);
out_abort:
	xchk_iscan_abort(&rr->iscan);
	mutex_unlock(&rr->lock);
out_unlock:
	return NOTIFY_DONE;
}

/* Set up the filesystem scan components. */
STATIC int
xrep_rtrmap_setup_scan(
	struct xrep_rtrmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	int			error;

	mutex_init(&rr->lock);
	xfsb_bitmap_init(&rr->old_rtrmapbt_blocks);

	/* Set up some storage */
	error = xfs_rtrmapbt_mem_init(sc->mp, &rr->rtrmap_btree, sc->xmbtp,
			rtg_rgno(sc->sr.rtg));
	if (error)
		goto out_bitmap;

	/* Retry iget every tenth of a second for up to 30 seconds. */
	xchk_iscan_start(sc, 30000, 100, &rr->iscan);

	/*
	 * Hook into live rtrmap operations so that we can update our in-memory
	 * btree to reflect live changes on the filesystem.  Since we drop the
	 * rtrmap ILOCK to scan all the inodes, we need this piece to avoid
	 * installing a stale btree.
	 */
	ASSERT(sc->flags & XCHK_FSGATES_RMAP);
	xfs_rmap_hook_setup(&rr->rhook, xrep_rtrmapbt_live_update);
	error = xfs_rmap_hook_add(rtg_group(sc->sr.rtg), &rr->rhook);
	if (error)
		goto out_iscan;
	return 0;

out_iscan:
	xchk_iscan_teardown(&rr->iscan);
	xfbtree_destroy(&rr->rtrmap_btree);
out_bitmap:
	xfsb_bitmap_destroy(&rr->old_rtrmapbt_blocks);
	mutex_destroy(&rr->lock);
	return error;
}

/* Tear down scan components. */
STATIC void
xrep_rtrmap_teardown(
	struct xrep_rtrmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;

	xchk_iscan_abort(&rr->iscan);
	xfs_rmap_hook_del(rtg_group(sc->sr.rtg), &rr->rhook);
	xchk_iscan_teardown(&rr->iscan);
	xfbtree_destroy(&rr->rtrmap_btree);
	xfsb_bitmap_destroy(&rr->old_rtrmapbt_blocks);
	mutex_destroy(&rr->lock);
}

/* Repair the realtime rmap btree. */
int
xrep_rtrmapbt(
	struct xfs_scrub	*sc)
{
	struct xrep_rtrmap	*rr = sc->buf;
	int			error;

	/* Make sure any problems with the fork are fixed. */
	error = xrep_metadata_inode_forks(sc);
	if (error)
		return error;

	error = xrep_rtrmap_setup_scan(rr);
	if (error)
		return error;

	/* Collect rmaps for realtime files. */
	error = xrep_rtrmap_find_rmaps(rr);
	if (error)
		goto out_records;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	/* Rebuild the rtrmap information. */
	error = xrep_rtrmap_build_new_tree(rr);
	if (error)
		goto out_records;

	/* Kill the old tree. */
	error = xrep_rtrmap_remove_old_tree(rr);
	if (error)
		goto out_records;

out_records:
	xrep_rtrmap_teardown(rr);
	return error;
}
