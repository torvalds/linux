// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
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
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_ag.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/agb_bitmap.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/iscan.h"
#include "scrub/newbt.h"
#include "scrub/reap.h"

/*
 * Reverse Mapping Btree Repair
 * ============================
 *
 * This is the most involved of all the AG space btree rebuilds.  Everywhere
 * else in XFS we lock inodes and then AG data structures, but generating the
 * list of rmap records requires that we be able to scan both block mapping
 * btrees of every inode in the filesystem to see if it owns any extents in
 * this AG.  We can't tolerate any inode updates while we do this, so we
 * freeze the filesystem to lock everyone else out, and grant ourselves
 * special privileges to run transactions with regular background reclamation
 * turned off.
 *
 * We also have to be very careful not to allow inode reclaim to start a
 * transaction because all transactions (other than our own) will block.
 * Deferred inode inactivation helps us out there.
 *
 * I) Reverse mappings for all non-space metadata and file data are collected
 * according to the following algorithm:
 *
 * 1. For each fork of each inode:
 * 1.1. Create a bitmap BMBIT to track bmbt blocks if necessary.
 * 1.2. If the incore extent map isn't loaded, walk the bmbt to accumulate
 *      bmaps into rmap records (see 1.1.4).  Set bits in BMBIT for each btree
 *      block.
 * 1.3. If the incore extent map is loaded but the fork is in btree format,
 *      just visit the bmbt blocks to set the corresponding BMBIT areas.
 * 1.4. From the incore extent map, accumulate each bmap that falls into our
 *      target AG.  Remember, multiple bmap records can map to a single rmap
 *      record, so we cannot simply emit rmap records 1:1.
 * 1.5. Emit rmap records for each extent in BMBIT and free it.
 * 2. Create bitmaps INOBIT and ICHUNKBIT.
 * 3. For each record in the inobt, set the corresponding areas in ICHUNKBIT,
 *    and set bits in INOBIT for each btree block.  If the inobt has no records
 *    at all, we must be careful to record its root in INOBIT.
 * 4. For each block in the finobt, set the corresponding INOBIT area.
 * 5. Emit rmap records for each extent in INOBIT and ICHUNKBIT and free them.
 * 6. Create bitmaps REFCBIT and COWBIT.
 * 7. For each CoW staging extent in the refcountbt, set the corresponding
 *    areas in COWBIT.
 * 8. For each block in the refcountbt, set the corresponding REFCBIT area.
 * 9. Emit rmap records for each extent in REFCBIT and COWBIT and free them.
 * A. Emit rmap for the AG headers.
 * B. Emit rmap for the log, if there is one.
 *
 * II) The rmapbt shape and space metadata rmaps are computed as follows:
 *
 * 1. Count the rmaps collected in the previous step. (= NR)
 * 2. Estimate the number of rmapbt blocks needed to store NR records. (= RMB)
 * 3. Reserve RMB blocks through the newbt using the allocator in normap mode.
 * 4. Create bitmap AGBIT.
 * 5. For each reservation in the newbt, set the corresponding areas in AGBIT.
 * 6. For each block in the AGFL, bnobt, and cntbt, set the bits in AGBIT.
 * 7. Count the extents in AGBIT. (= AGNR)
 * 8. Estimate the number of rmapbt blocks needed for NR + AGNR rmaps. (= RMB')
 * 9. If RMB' >= RMB, reserve RMB' - RMB more newbt blocks, set RMB = RMB',
 *    and clear AGBIT.  Go to step 5.
 * A. Emit rmaps for each extent in AGBIT.
 *
 * III) The rmapbt is constructed and set in place as follows:
 *
 * 1. Sort the rmap records.
 * 2. Bulk load the rmaps.
 *
 * IV) Reap the old btree blocks.
 *
 * 1. Create a bitmap OLDRMBIT.
 * 2. For each gap in the new rmapbt, set the corresponding areas of OLDRMBIT.
 * 3. For each extent in the bnobt, clear the corresponding parts of OLDRMBIT.
 * 4. Reap the extents corresponding to the set areas in OLDRMBIT.  These are
 *    the parts of the AG that the rmap didn't find during its scan of the
 *    primary metadata and aren't known to be in the free space, which implies
 *    that they were the old rmapbt blocks.
 * 5. Commit.
 *
 * We use the 'xrep_rmap' prefix for all the rmap functions.
 */

/* Context for collecting rmaps */
struct xrep_rmap {
	/* new rmapbt information */
	struct xrep_newbt	new_btree;

	/* lock for the xfbtree and xfile */
	struct mutex		lock;

	/* rmap records generated from primary metadata */
	struct xfbtree		rmap_btree;

	struct xfs_scrub	*sc;

	/* in-memory btree cursor for the xfs_btree_bload iteration */
	struct xfs_btree_cur	*mcur;

	/* Hooks into rmap update code. */
	struct xfs_rmap_hook	rhook;

	/* inode scan cursor */
	struct xchk_iscan	iscan;

	/* Number of non-freespace records found. */
	unsigned long long	nr_records;

	/* bnobt/cntbt contribution to btreeblks */
	xfs_agblock_t		freesp_btblocks;

	/* old agf_rmap_blocks counter */
	unsigned int		old_rmapbt_fsbcount;
};

/* Set us up to repair reverse mapping btrees. */
int
xrep_setup_ag_rmapbt(
	struct xfs_scrub	*sc)
{
	struct xrep_rmap	*rr;
	char			*descr;
	int			error;

	xchk_fsgates_enable(sc, XCHK_FSGATES_RMAP);

	descr = xchk_xfile_ag_descr(sc, "reverse mapping records");
	error = xrep_setup_xfbtree(sc, descr);
	kfree(descr);
	if (error)
		return error;

	rr = kzalloc(sizeof(struct xrep_rmap), XCHK_GFP_FLAGS);
	if (!rr)
		return -ENOMEM;

	rr->sc = sc;
	sc->buf = rr;
	return 0;
}

/* Make sure there's nothing funny about this mapping. */
STATIC int
xrep_rmap_check_mapping(
	struct xfs_scrub	*sc,
	const struct xfs_rmap_irec *rec)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (xfs_rmap_check_irec(sc->sa.pag, rec) != NULL)
		return -EFSCORRUPTED;

	/* Make sure this isn't free space. */
	error = xfs_alloc_has_records(sc->sa.bno_cur, rec->rm_startblock,
			rec->rm_blockcount, &outcome);
	if (error)
		return error;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		return -EFSCORRUPTED;

	return 0;
}

/* Store a reverse-mapping record. */
static inline int
xrep_rmap_stash(
	struct xrep_rmap	*rr,
	xfs_agblock_t		startblock,
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

	trace_xrep_rmap_found(sc->mp, sc->sa.pag->pag_agno, &rmap);

	mutex_lock(&rr->lock);
	mcur = xfs_rmapbt_mem_cursor(sc->sa.pag, sc->tp, &rr->rmap_btree);
	error = xfs_rmap_map_raw(mcur, &rmap);
	xfs_btree_del_cursor(mcur, error);
	if (error)
		goto out_cancel;

	error = xfbtree_trans_commit(&rr->rmap_btree, sc->tp);
	if (error)
		goto out_abort;

	mutex_unlock(&rr->lock);
	return 0;

out_cancel:
	xfbtree_trans_cancel(&rr->rmap_btree, sc->tp);
out_abort:
	xchk_iscan_abort(&rr->iscan);
	mutex_unlock(&rr->lock);
	return error;
}

struct xrep_rmap_stash_run {
	struct xrep_rmap	*rr;
	uint64_t		owner;
	unsigned int		rmap_flags;
};

static int
xrep_rmap_stash_run(
	uint32_t			start,
	uint32_t			len,
	void				*priv)
{
	struct xrep_rmap_stash_run	*rsr = priv;
	struct xrep_rmap		*rr = rsr->rr;

	return xrep_rmap_stash(rr, start, len, rsr->owner, 0, rsr->rmap_flags);
}

/*
 * Emit rmaps for every extent of bits set in the bitmap.  Caller must ensure
 * that the ranges are in units of FS blocks.
 */
STATIC int
xrep_rmap_stash_bitmap(
	struct xrep_rmap		*rr,
	struct xagb_bitmap		*bitmap,
	const struct xfs_owner_info	*oinfo)
{
	struct xrep_rmap_stash_run	rsr = {
		.rr			= rr,
		.owner			= oinfo->oi_owner,
		.rmap_flags		= 0,
	};

	if (oinfo->oi_flags & XFS_OWNER_INFO_ATTR_FORK)
		rsr.rmap_flags |= XFS_RMAP_ATTR_FORK;
	if (oinfo->oi_flags & XFS_OWNER_INFO_BMBT_BLOCK)
		rsr.rmap_flags |= XFS_RMAP_BMBT_BLOCK;

	return xagb_bitmap_walk(bitmap, xrep_rmap_stash_run, &rsr);
}

/* Section (I): Finding all file and bmbt extents. */

/* Context for accumulating rmaps for an inode fork. */
struct xrep_rmap_ifork {
	/*
	 * Accumulate rmap data here to turn multiple adjacent bmaps into a
	 * single rmap.
	 */
	struct xfs_rmap_irec	accum;

	/* Bitmap of bmbt blocks in this AG. */
	struct xagb_bitmap	bmbt_blocks;

	struct xrep_rmap	*rr;

	/* Which inode fork? */
	int			whichfork;
};

/* Stash an rmap that we accumulated while walking an inode fork. */
STATIC int
xrep_rmap_stash_accumulated(
	struct xrep_rmap_ifork	*rf)
{
	if (rf->accum.rm_blockcount == 0)
		return 0;

	return xrep_rmap_stash(rf->rr, rf->accum.rm_startblock,
			rf->accum.rm_blockcount, rf->accum.rm_owner,
			rf->accum.rm_offset, rf->accum.rm_flags);
}

/* Accumulate a bmbt record. */
STATIC int
xrep_rmap_visit_bmbt(
	struct xfs_btree_cur	*cur,
	struct xfs_bmbt_irec	*rec,
	void			*priv)
{
	struct xrep_rmap_ifork	*rf = priv;
	struct xfs_mount	*mp = rf->rr->sc->mp;
	struct xfs_rmap_irec	*accum = &rf->accum;
	xfs_agblock_t		agbno;
	unsigned int		rmap_flags = 0;
	int			error;

	if (XFS_FSB_TO_AGNO(mp, rec->br_startblock) !=
			rf->rr->sc->sa.pag->pag_agno)
		return 0;

	agbno = XFS_FSB_TO_AGBNO(mp, rec->br_startblock);
	if (rf->whichfork == XFS_ATTR_FORK)
		rmap_flags |= XFS_RMAP_ATTR_FORK;
	if (rec->br_state == XFS_EXT_UNWRITTEN)
		rmap_flags |= XFS_RMAP_UNWRITTEN;

	/* If this bmap is adjacent to the previous one, just add it. */
	if (accum->rm_blockcount > 0 &&
	    rec->br_startoff == accum->rm_offset + accum->rm_blockcount &&
	    agbno == accum->rm_startblock + accum->rm_blockcount &&
	    rmap_flags == accum->rm_flags) {
		accum->rm_blockcount += rec->br_blockcount;
		return 0;
	}

	/* Otherwise stash the old rmap and start accumulating a new one. */
	error = xrep_rmap_stash_accumulated(rf);
	if (error)
		return error;

	accum->rm_startblock = agbno;
	accum->rm_blockcount = rec->br_blockcount;
	accum->rm_offset = rec->br_startoff;
	accum->rm_flags = rmap_flags;
	return 0;
}

/* Add a btree block to the bitmap. */
STATIC int
xrep_rmap_visit_iroot_btree_block(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*priv)
{
	struct xrep_rmap_ifork	*rf = priv;
	struct xfs_buf		*bp;
	xfs_fsblock_t		fsbno;
	xfs_agblock_t		agbno;

	xfs_btree_get_block(cur, level, &bp);
	if (!bp)
		return 0;

	fsbno = XFS_DADDR_TO_FSB(cur->bc_mp, xfs_buf_daddr(bp));
	if (XFS_FSB_TO_AGNO(cur->bc_mp, fsbno) != rf->rr->sc->sa.pag->pag_agno)
		return 0;

	agbno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);
	return xagb_bitmap_set(&rf->bmbt_blocks, agbno, 1);
}

/*
 * Iterate a metadata btree rooted in an inode to collect rmap records for
 * anything in this fork that matches the AG.
 */
STATIC int
xrep_rmap_scan_iroot_btree(
	struct xrep_rmap_ifork	*rf,
	struct xfs_btree_cur	*cur)
{
	struct xfs_owner_info	oinfo;
	struct xrep_rmap	*rr = rf->rr;
	int			error;

	xagb_bitmap_init(&rf->bmbt_blocks);

	/* Record all the blocks in the btree itself. */
	error = xfs_btree_visit_blocks(cur, xrep_rmap_visit_iroot_btree_block,
			XFS_BTREE_VISIT_ALL, rf);
	if (error)
		goto out;

	/* Emit rmaps for the btree blocks. */
	xfs_rmap_ino_bmbt_owner(&oinfo, rf->accum.rm_owner, rf->whichfork);
	error = xrep_rmap_stash_bitmap(rr, &rf->bmbt_blocks, &oinfo);
	if (error)
		goto out;

	/* Stash any remaining accumulated rmaps. */
	error = xrep_rmap_stash_accumulated(rf);
out:
	xagb_bitmap_destroy(&rf->bmbt_blocks);
	return error;
}

static inline bool
is_rt_data_fork(
	struct xfs_inode	*ip,
	int			whichfork)
{
	return XFS_IS_REALTIME_INODE(ip) && whichfork == XFS_DATA_FORK;
}

/*
 * Iterate the block mapping btree to collect rmap records for anything in this
 * fork that matches the AG.  Sets @mappings_done to true if we've scanned the
 * block mappings in this fork.
 */
STATIC int
xrep_rmap_scan_bmbt(
	struct xrep_rmap_ifork	*rf,
	struct xfs_inode	*ip,
	bool			*mappings_done)
{
	struct xrep_rmap	*rr = rf->rr;
	struct xfs_btree_cur	*cur;
	struct xfs_ifork	*ifp;
	int			error;

	*mappings_done = false;
	ifp = xfs_ifork_ptr(ip, rf->whichfork);
	cur = xfs_bmbt_init_cursor(rr->sc->mp, rr->sc->tp, ip, rf->whichfork);

	if (!xfs_ifork_is_realtime(ip, rf->whichfork) &&
	    xfs_need_iread_extents(ifp)) {
		/*
		 * If the incore extent cache isn't loaded, scan the bmbt for
		 * mapping records.  This avoids loading the incore extent
		 * tree, which will increase memory pressure at a time when
		 * we're trying to run as quickly as we possibly can.  Ignore
		 * realtime extents.
		 */
		error = xfs_bmap_query_all(cur, xrep_rmap_visit_bmbt, rf);
		if (error)
			goto out_cur;

		*mappings_done = true;
	}

	/* Scan for the bmbt blocks, which always live on the data device. */
	error = xrep_rmap_scan_iroot_btree(rf, cur);
out_cur:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * Iterate the in-core extent cache to collect rmap records for anything in
 * this fork that matches the AG.
 */
STATIC int
xrep_rmap_scan_iext(
	struct xrep_rmap_ifork	*rf,
	struct xfs_ifork	*ifp)
{
	struct xfs_bmbt_irec	rec;
	struct xfs_iext_cursor	icur;
	int			error;

	for_each_xfs_iext(ifp, &icur, &rec) {
		if (isnullstartblock(rec.br_startblock))
			continue;
		error = xrep_rmap_visit_bmbt(NULL, &rec, rf);
		if (error)
			return error;
	}

	return xrep_rmap_stash_accumulated(rf);
}

/* Find all the extents from a given AG in an inode fork. */
STATIC int
xrep_rmap_scan_ifork(
	struct xrep_rmap	*rr,
	struct xfs_inode	*ip,
	int			whichfork)
{
	struct xrep_rmap_ifork	rf = {
		.accum		= { .rm_owner = ip->i_ino, },
		.rr		= rr,
		.whichfork	= whichfork,
	};
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	int			error = 0;

	if (!ifp)
		return 0;

	if (ifp->if_format == XFS_DINODE_FMT_BTREE) {
		bool		mappings_done;

		/*
		 * Scan the bmap btree for data device mappings.  This includes
		 * the btree blocks themselves, even if this is a realtime
		 * file.
		 */
		error = xrep_rmap_scan_bmbt(&rf, ip, &mappings_done);
		if (error || mappings_done)
			return error;
	} else if (ifp->if_format != XFS_DINODE_FMT_EXTENTS) {
		return 0;
	}

	/* Scan incore extent cache if this isn't a realtime file. */
	if (xfs_ifork_is_realtime(ip, whichfork))
		return 0;

	return xrep_rmap_scan_iext(&rf, ifp);
}

/*
 * Take ILOCK on a file that we want to scan.
 *
 * Select ILOCK_EXCL if the file has an unloaded data bmbt or has an unloaded
 * attr bmbt.  Otherwise, take ILOCK_SHARED.
 */
static inline unsigned int
xrep_rmap_scan_ilock(
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (xfs_need_iread_extents(&ip->i_df)) {
		lock_mode = XFS_ILOCK_EXCL;
		goto lock;
	}

	if (xfs_inode_has_attr_fork(ip) && xfs_need_iread_extents(&ip->i_af))
		lock_mode = XFS_ILOCK_EXCL;

lock:
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

/* Record reverse mappings for a file. */
STATIC int
xrep_rmap_scan_inode(
	struct xrep_rmap	*rr,
	struct xfs_inode	*ip)
{
	unsigned int		lock_mode = 0;
	int			error;

	/*
	 * Directory updates (create/link/unlink/rename) drop the directory's
	 * ILOCK before finishing any rmapbt updates associated with directory
	 * shape changes.  For this scan to coordinate correctly with the live
	 * update hook, we must take the only lock (i_rwsem) that is held all
	 * the way to dir op completion.  This will get fixed by the parent
	 * pointer patchset.
	 */
	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		lock_mode = XFS_IOLOCK_SHARED;
		xfs_ilock(ip, lock_mode);
	}
	lock_mode |= xrep_rmap_scan_ilock(ip);

	/* Check the data fork. */
	error = xrep_rmap_scan_ifork(rr, ip, XFS_DATA_FORK);
	if (error)
		goto out_unlock;

	/* Check the attr fork. */
	error = xrep_rmap_scan_ifork(rr, ip, XFS_ATTR_FORK);
	if (error)
		goto out_unlock;

	/* COW fork extents are "owned" by the refcount btree. */

	xchk_iscan_mark_visited(&rr->iscan, ip);
out_unlock:
	xfs_iunlock(ip, lock_mode);
	return error;
}

/* Section (I): Find all AG metadata extents except for free space metadata. */

struct xrep_rmap_inodes {
	struct xrep_rmap	*rr;
	struct xagb_bitmap	inobt_blocks;	/* INOBIT */
	struct xagb_bitmap	ichunk_blocks;	/* ICHUNKBIT */
};

/* Record inode btree rmaps. */
STATIC int
xrep_rmap_walk_inobt(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_inobt_rec_incore	irec;
	struct xrep_rmap_inodes		*ri = priv;
	struct xfs_mount		*mp = cur->bc_mp;
	xfs_agblock_t			agbno;
	xfs_extlen_t			aglen;
	xfs_agino_t			agino;
	xfs_agino_t			iperhole;
	unsigned int			i;
	int				error;

	/* Record the inobt blocks. */
	error = xagb_bitmap_set_btcur_path(&ri->inobt_blocks, cur);
	if (error)
		return error;

	xfs_inobt_btrec_to_irec(mp, rec, &irec);
	if (xfs_inobt_check_irec(cur->bc_ag.pag, &irec) != NULL)
		return -EFSCORRUPTED;

	agino = irec.ir_startino;

	/* Record a non-sparse inode chunk. */
	if (!xfs_inobt_issparse(irec.ir_holemask)) {
		agbno = XFS_AGINO_TO_AGBNO(mp, agino);
		aglen = max_t(xfs_extlen_t, 1,
				XFS_INODES_PER_CHUNK / mp->m_sb.sb_inopblock);

		return xagb_bitmap_set(&ri->ichunk_blocks, agbno, aglen);
	}

	/* Iterate each chunk. */
	iperhole = max_t(xfs_agino_t, mp->m_sb.sb_inopblock,
			XFS_INODES_PER_HOLEMASK_BIT);
	aglen = iperhole / mp->m_sb.sb_inopblock;
	for (i = 0, agino = irec.ir_startino;
	     i < XFS_INOBT_HOLEMASK_BITS;
	     i += iperhole / XFS_INODES_PER_HOLEMASK_BIT, agino += iperhole) {
		/* Skip holes. */
		if (irec.ir_holemask & (1 << i))
			continue;

		/* Record the inode chunk otherwise. */
		agbno = XFS_AGINO_TO_AGBNO(mp, agino);
		error = xagb_bitmap_set(&ri->ichunk_blocks, agbno, aglen);
		if (error)
			return error;
	}

	return 0;
}

/* Collect rmaps for the blocks containing inode btrees and the inode chunks. */
STATIC int
xrep_rmap_find_inode_rmaps(
	struct xrep_rmap	*rr)
{
	struct xrep_rmap_inodes	ri = {
		.rr		= rr,
	};
	struct xfs_scrub	*sc = rr->sc;
	int			error;

	xagb_bitmap_init(&ri.inobt_blocks);
	xagb_bitmap_init(&ri.ichunk_blocks);

	/*
	 * Iterate every record in the inobt so we can capture all the inode
	 * chunks and the blocks in the inobt itself.
	 */
	error = xfs_btree_query_all(sc->sa.ino_cur, xrep_rmap_walk_inobt, &ri);
	if (error)
		goto out_bitmap;

	/*
	 * Note that if there are zero records in the inobt then query_all does
	 * nothing and we have to account the empty inobt root manually.
	 */
	if (xagb_bitmap_empty(&ri.ichunk_blocks)) {
		struct xfs_agi	*agi = sc->sa.agi_bp->b_addr;

		error = xagb_bitmap_set(&ri.inobt_blocks,
				be32_to_cpu(agi->agi_root), 1);
		if (error)
			goto out_bitmap;
	}

	/* Scan the finobt too. */
	if (xfs_has_finobt(sc->mp)) {
		error = xagb_bitmap_set_btblocks(&ri.inobt_blocks,
				sc->sa.fino_cur);
		if (error)
			goto out_bitmap;
	}

	/* Generate rmaps for everything. */
	error = xrep_rmap_stash_bitmap(rr, &ri.inobt_blocks,
			&XFS_RMAP_OINFO_INOBT);
	if (error)
		goto out_bitmap;
	error = xrep_rmap_stash_bitmap(rr, &ri.ichunk_blocks,
			&XFS_RMAP_OINFO_INODES);

out_bitmap:
	xagb_bitmap_destroy(&ri.inobt_blocks);
	xagb_bitmap_destroy(&ri.ichunk_blocks);
	return error;
}

/* Record a CoW staging extent. */
STATIC int
xrep_rmap_walk_cowblocks(
	struct xfs_btree_cur		*cur,
	const struct xfs_refcount_irec	*irec,
	void				*priv)
{
	struct xagb_bitmap		*bitmap = priv;

	if (!xfs_refcount_check_domain(irec) ||
	    irec->rc_domain != XFS_REFC_DOMAIN_COW)
		return -EFSCORRUPTED;

	return xagb_bitmap_set(bitmap, irec->rc_startblock, irec->rc_blockcount);
}

/*
 * Collect rmaps for the blocks containing the refcount btree, and all CoW
 * staging extents.
 */
STATIC int
xrep_rmap_find_refcount_rmaps(
	struct xrep_rmap	*rr)
{
	struct xagb_bitmap	refcountbt_blocks;	/* REFCBIT */
	struct xagb_bitmap	cow_blocks;		/* COWBIT */
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

	if (!xfs_has_reflink(sc->mp))
		return 0;

	xagb_bitmap_init(&refcountbt_blocks);
	xagb_bitmap_init(&cow_blocks);

	/* refcountbt */
	error = xagb_bitmap_set_btblocks(&refcountbt_blocks, sc->sa.refc_cur);
	if (error)
		goto out_bitmap;

	/* Collect rmaps for CoW staging extents. */
	error = xfs_refcount_query_range(sc->sa.refc_cur, &low, &high,
			xrep_rmap_walk_cowblocks, &cow_blocks);
	if (error)
		goto out_bitmap;

	/* Generate rmaps for everything. */
	error = xrep_rmap_stash_bitmap(rr, &cow_blocks, &XFS_RMAP_OINFO_COW);
	if (error)
		goto out_bitmap;
	error = xrep_rmap_stash_bitmap(rr, &refcountbt_blocks,
			&XFS_RMAP_OINFO_REFC);

out_bitmap:
	xagb_bitmap_destroy(&cow_blocks);
	xagb_bitmap_destroy(&refcountbt_blocks);
	return error;
}

/* Generate rmaps for the AG headers (AGI/AGF/AGFL) */
STATIC int
xrep_rmap_find_agheader_rmaps(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;

	/* Create a record for the AG sb->agfl. */
	return xrep_rmap_stash(rr, XFS_SB_BLOCK(sc->mp),
			XFS_AGFL_BLOCK(sc->mp) - XFS_SB_BLOCK(sc->mp) + 1,
			XFS_RMAP_OWN_FS, 0, 0);
}

/* Generate rmaps for the log, if it's in this AG. */
STATIC int
xrep_rmap_find_log_rmaps(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;

	if (!xfs_ag_contains_log(sc->mp, sc->sa.pag->pag_agno))
		return 0;

	return xrep_rmap_stash(rr,
			XFS_FSB_TO_AGBNO(sc->mp, sc->mp->m_sb.sb_logstart),
			sc->mp->m_sb.sb_logblocks, XFS_RMAP_OWN_LOG, 0, 0);
}

/* Check and count all the records that we gathered. */
STATIC int
xrep_rmap_check_record(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_rmap		*rr = priv;
	int				error;

	error = xrep_rmap_check_mapping(rr->sc, rec);
	if (error)
		return error;

	rr->nr_records++;
	return 0;
}

/*
 * Generate all the reverse-mappings for this AG, a list of the old rmapbt
 * blocks, and the new btreeblks count.  Figure out if we have enough free
 * space to reconstruct the inode btrees.  The caller must clean up the lists
 * if anything goes wrong.  This implements section (I) above.
 */
STATIC int
xrep_rmap_find_rmaps(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	struct xchk_ag		*sa = &sc->sa;
	struct xfs_inode	*ip;
	struct xfs_btree_cur	*mcur;
	int			error;

	/* Find all the per-AG metadata. */
	xrep_ag_btcur_init(sc, &sc->sa);

	error = xrep_rmap_find_inode_rmaps(rr);
	if (error)
		goto end_agscan;

	error = xrep_rmap_find_refcount_rmaps(rr);
	if (error)
		goto end_agscan;

	error = xrep_rmap_find_agheader_rmaps(rr);
	if (error)
		goto end_agscan;

	error = xrep_rmap_find_log_rmaps(rr);
end_agscan:
	xchk_ag_btcur_free(&sc->sa);
	if (error)
		return error;

	/*
	 * Set up for a potentially lengthy filesystem scan by reducing our
	 * transaction resource usage for the duration.  Specifically:
	 *
	 * Unlock the AG header buffers and cancel the transaction to release
	 * the log grant space while we scan the filesystem.
	 *
	 * Create a new empty transaction to eliminate the possibility of the
	 * inode scan deadlocking on cyclical metadata.
	 *
	 * We pass the empty transaction to the file scanning function to avoid
	 * repeatedly cycling empty transactions.  This can be done even though
	 * we take the IOLOCK to quiesce the file because empty transactions
	 * do not take sb_internal.
	 */
	sa->agf_bp = NULL;
	sa->agi_bp = NULL;
	xchk_trans_cancel(sc);
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	/* Iterate all AGs for inodes rmaps. */
	while ((error = xchk_iscan_iter(&rr->iscan, &ip)) == 1) {
		error = xrep_rmap_scan_inode(rr, ip);
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
	 * Switch out for a real transaction and lock the AG headers in
	 * preparation for building a new tree.
	 */
	xchk_trans_cancel(sc);
	error = xchk_setup_fs(sc);
	if (error)
		return error;
	error = xchk_perag_drain_and_lock(sc);
	if (error)
		return error;

	/*
	 * If a hook failed to update the in-memory btree, we lack the data to
	 * continue the repair.
	 */
	if (xchk_iscan_aborted(&rr->iscan))
		return -EFSCORRUPTED;

	/*
	 * Now that we have everything locked again, we need to count the
	 * number of rmap records stashed in the btree.  This should reflect
	 * all actively-owned space in the filesystem.  At the same time, check
	 * all our records before we start building a new btree, which requires
	 * a bnobt cursor.
	 */
	mcur = xfs_rmapbt_mem_cursor(rr->sc->sa.pag, NULL, &rr->rmap_btree);
	sc->sa.bno_cur = xfs_bnobt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);

	rr->nr_records = 0;
	error = xfs_rmap_query_all(mcur, xrep_rmap_check_record, rr);

	xfs_btree_del_cursor(sc->sa.bno_cur, error);
	sc->sa.bno_cur = NULL;
	xfs_btree_del_cursor(mcur, error);

	return error;
}

/* Section (II): Reserving space for new rmapbt and setting free space bitmap */

struct xrep_rmap_agfl {
	struct xagb_bitmap	*bitmap;
	xfs_agnumber_t		agno;
};

/* Add an AGFL block to the rmap list. */
STATIC int
xrep_rmap_walk_agfl(
	struct xfs_mount	*mp,
	xfs_agblock_t		agbno,
	void			*priv)
{
	struct xrep_rmap_agfl	*ra = priv;

	return xagb_bitmap_set(ra->bitmap, agbno, 1);
}

/*
 * Run one round of reserving space for the new rmapbt and recomputing the
 * number of blocks needed to store the previously observed rmapbt records and
 * the ones we'll create for the free space metadata.  When we don't need more
 * blocks, return a bitmap of OWN_AG extents in @freesp_blocks and set @done to
 * true.
 */
STATIC int
xrep_rmap_try_reserve(
	struct xrep_rmap	*rr,
	struct xfs_btree_cur	*rmap_cur,
	struct xagb_bitmap	*freesp_blocks,
	uint64_t		*blocks_reserved,
	bool			*done)
{
	struct xrep_rmap_agfl	ra = {
		.bitmap		= freesp_blocks,
		.agno		= rr->sc->sa.pag->pag_agno,
	};
	struct xfs_scrub	*sc = rr->sc;
	struct xrep_newbt_resv	*resv, *n;
	struct xfs_agf		*agf = sc->sa.agf_bp->b_addr;
	struct xfs_buf		*agfl_bp;
	uint64_t		nr_blocks;	/* RMB */
	uint64_t		freesp_records;
	int			error;

	/*
	 * We're going to recompute new_btree.bload.nr_blocks at the end of
	 * this function to reflect however many btree blocks we need to store
	 * all the rmap records (including the ones that reflect the changes we
	 * made to support the new rmapbt blocks), so we save the old value
	 * here so we can decide if we've reserved enough blocks.
	 */
	nr_blocks = rr->new_btree.bload.nr_blocks;

	/*
	 * Make sure we've reserved enough space for the new btree.  This can
	 * change the shape of the free space btrees, which can cause secondary
	 * interactions with the rmap records because all three space btrees
	 * have the same rmap owner.  We'll account for all that below.
	 */
	error = xrep_newbt_alloc_blocks(&rr->new_btree,
			nr_blocks - *blocks_reserved);
	if (error)
		return error;

	*blocks_reserved = rr->new_btree.bload.nr_blocks;

	/* Clear everything in the bitmap. */
	xagb_bitmap_destroy(freesp_blocks);

	/* Set all the bnobt blocks in the bitmap. */
	sc->sa.bno_cur = xfs_bnobt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);
	error = xagb_bitmap_set_btblocks(freesp_blocks, sc->sa.bno_cur);
	xfs_btree_del_cursor(sc->sa.bno_cur, error);
	sc->sa.bno_cur = NULL;
	if (error)
		return error;

	/* Set all the cntbt blocks in the bitmap. */
	sc->sa.cnt_cur = xfs_cntbt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);
	error = xagb_bitmap_set_btblocks(freesp_blocks, sc->sa.cnt_cur);
	xfs_btree_del_cursor(sc->sa.cnt_cur, error);
	sc->sa.cnt_cur = NULL;
	if (error)
		return error;

	/* Record our new btreeblks value. */
	rr->freesp_btblocks = xagb_bitmap_hweight(freesp_blocks) - 2;

	/* Set all the new rmapbt blocks in the bitmap. */
	list_for_each_entry_safe(resv, n, &rr->new_btree.resv_list, list) {
		error = xagb_bitmap_set(freesp_blocks, resv->agbno, resv->len);
		if (error)
			return error;
	}

	/* Set all the AGFL blocks in the bitmap. */
	error = xfs_alloc_read_agfl(sc->sa.pag, sc->tp, &agfl_bp);
	if (error)
		return error;

	error = xfs_agfl_walk(sc->mp, agf, agfl_bp, xrep_rmap_walk_agfl, &ra);
	if (error)
		return error;

	/* Count the extents in the bitmap. */
	freesp_records = xagb_bitmap_count_set_regions(freesp_blocks);

	/* Compute how many blocks we'll need for all the rmaps. */
	error = xfs_btree_bload_compute_geometry(rmap_cur,
			&rr->new_btree.bload, rr->nr_records + freesp_records);
	if (error)
		return error;

	/* We're done when we don't need more blocks. */
	*done = nr_blocks >= rr->new_btree.bload.nr_blocks;
	return 0;
}

/*
 * Iteratively reserve space for rmap btree while recording OWN_AG rmaps for
 * the free space metadata.  This implements section (II) above.
 */
STATIC int
xrep_rmap_reserve_space(
	struct xrep_rmap	*rr,
	struct xfs_btree_cur	*rmap_cur)
{
	struct xagb_bitmap	freesp_blocks;	/* AGBIT */
	uint64_t		blocks_reserved = 0;
	bool			done = false;
	int			error;

	/* Compute how many blocks we'll need for the rmaps collected so far. */
	error = xfs_btree_bload_compute_geometry(rmap_cur,
			&rr->new_btree.bload, rr->nr_records);
	if (error)
		return error;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(rr->sc, &error))
		return error;

	xagb_bitmap_init(&freesp_blocks);

	/*
	 * Iteratively reserve space for the new rmapbt and recompute the
	 * number of blocks needed to store the previously observed rmapbt
	 * records and the ones we'll create for the free space metadata.
	 * Finish when we don't need more blocks.
	 */
	do {
		error = xrep_rmap_try_reserve(rr, rmap_cur, &freesp_blocks,
				&blocks_reserved, &done);
		if (error)
			goto out_bitmap;
	} while (!done);

	/* Emit rmaps for everything in the free space bitmap. */
	xrep_ag_btcur_init(rr->sc, &rr->sc->sa);
	error = xrep_rmap_stash_bitmap(rr, &freesp_blocks, &XFS_RMAP_OINFO_AG);
	xchk_ag_btcur_free(&rr->sc->sa);

out_bitmap:
	xagb_bitmap_destroy(&freesp_blocks);
	return error;
}

/* Section (III): Building the new rmap btree. */

/* Update the AGF counters. */
STATIC int
xrep_rmap_reset_counters(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_agf		*agf = sc->sa.agf_bp->b_addr;
	xfs_agblock_t		rmap_btblocks;

	/*
	 * The AGF header contains extra information related to the reverse
	 * mapping btree, so we must update those fields here.
	 */
	rmap_btblocks = rr->new_btree.afake.af_blocks - 1;
	agf->agf_btreeblks = cpu_to_be32(rr->freesp_btblocks + rmap_btblocks);
	xfs_alloc_log_agf(sc->tp, sc->sa.agf_bp, XFS_AGF_BTREEBLKS);

	/*
	 * After we commit the new btree to disk, it is possible that the
	 * process to reap the old btree blocks will race with the AIL trying
	 * to checkpoint the old btree blocks into the filesystem.  If the new
	 * tree is shorter than the old one, the rmapbt write verifier will
	 * fail and the AIL will shut down the filesystem.
	 *
	 * To avoid this, save the old incore btree height values as the alt
	 * height values before re-initializing the perag info from the updated
	 * AGF to capture all the new values.
	 */
	pag->pagf_repair_rmap_level = pag->pagf_rmap_level;

	/* Reinitialize with the values we just logged. */
	return xrep_reinit_pagf(sc);
}

/* Retrieve rmapbt data for bulk load. */
STATIC int
xrep_rmap_get_records(
	struct xfs_btree_cur	*cur,
	unsigned int		idx,
	struct xfs_btree_block	*block,
	unsigned int		nr_wanted,
	void			*priv)
{
	struct xrep_rmap	*rr = priv;
	union xfs_btree_rec	*block_rec;
	unsigned int		loaded;
	int			error;

	for (loaded = 0; loaded < nr_wanted; loaded++, idx++) {
		int		stat = 0;

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
xrep_rmap_claim_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	void			*priv)
{
	struct xrep_rmap        *rr = priv;

	return xrep_newbt_claim_block(cur, &rr->new_btree, ptr);
}

/* Custom allocation function for new rmap btrees. */
STATIC int
xrep_rmap_alloc_vextent(
	struct xfs_scrub	*sc,
	struct xfs_alloc_arg	*args,
	xfs_fsblock_t		alloc_hint)
{
	int			error;

	/*
	 * We don't want an rmap update on the allocation, since we iteratively
	 * compute the OWN_AG records /after/ allocating blocks for the records
	 * that we already know we need to store.  Therefore, fix the freelist
	 * with the NORMAP flag set so that we don't also try to create an rmap
	 * for new AGFL blocks.
	 */
	error = xrep_fix_freelist(sc, XFS_ALLOC_FLAG_NORMAP);
	if (error)
		return error;

	/*
	 * If xrep_fix_freelist fixed the freelist by moving blocks from the
	 * free space btrees or by removing blocks from the AGFL and queueing
	 * an EFI to free the block, the transaction will be dirty.  This
	 * second case is of interest to us.
	 *
	 * Later on, we will need to compare gaps in the new recordset against
	 * the block usage of all OWN_AG owners in order to free the old
	 * btree's blocks, which means that we can't have EFIs for former AGFL
	 * blocks attached to the repair transaction when we commit the new
	 * btree.
	 *
	 * xrep_newbt_alloc_blocks guarantees this for us by calling
	 * xrep_defer_finish to commit anything that fix_freelist may have
	 * added to the transaction.
	 */
	return xfs_alloc_vextent_near_bno(args, alloc_hint);
}


/* Count the records in this btree. */
STATIC int
xrep_rmap_count_records(
	struct xfs_btree_cur	*cur,
	unsigned long long	*nr)
{
	int			running = 1;
	int			error;

	*nr = 0;

	error = xfs_btree_goto_left_edge(cur);
	if (error)
		return error;

	while (running && !(error = xfs_btree_increment(cur, 0, &running))) {
		if (running)
			(*nr)++;
	}

	return error;
}
/*
 * Use the collected rmap information to stage a new rmap btree.  If this is
 * successful we'll return with the new btree root information logged to the
 * repair transaction but not yet committed.  This implements section (III)
 * above.
 */
STATIC int
xrep_rmap_build_new_tree(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_agf		*agf = sc->sa.agf_bp->b_addr;
	struct xfs_btree_cur	*rmap_cur;
	xfs_fsblock_t		fsbno;
	int			error;

	/*
	 * Preserve the old rmapbt block count so that we can adjust the
	 * per-AG rmapbt reservation after we commit the new btree root and
	 * want to dispose of the old btree blocks.
	 */
	rr->old_rmapbt_fsbcount = be32_to_cpu(agf->agf_rmap_blocks);

	/*
	 * Prepare to construct the new btree by reserving disk space for the
	 * new btree and setting up all the accounting information we'll need
	 * to root the new btree while it's under construction and before we
	 * attach it to the AG header.  The new blocks are accounted to the
	 * rmapbt per-AG reservation, which we will adjust further after
	 * committing the new btree.
	 */
	fsbno = XFS_AGB_TO_FSB(sc->mp, pag->pag_agno, XFS_RMAP_BLOCK(sc->mp));
	xrep_newbt_init_ag(&rr->new_btree, sc, &XFS_RMAP_OINFO_SKIP_UPDATE,
			fsbno, XFS_AG_RESV_RMAPBT);
	rr->new_btree.bload.get_records = xrep_rmap_get_records;
	rr->new_btree.bload.claim_block = xrep_rmap_claim_block;
	rr->new_btree.alloc_vextent = xrep_rmap_alloc_vextent;
	rmap_cur = xfs_rmapbt_init_cursor(sc->mp, NULL, NULL, pag);
	xfs_btree_stage_afakeroot(rmap_cur, &rr->new_btree.afake);

	/*
	 * Initialize @rr->new_btree, reserve space for the new rmapbt,
	 * and compute OWN_AG rmaps.
	 */
	error = xrep_rmap_reserve_space(rr, rmap_cur);
	if (error)
		goto err_cur;

	/*
	 * Count the rmapbt records again, because the space reservation
	 * for the rmapbt itself probably added more records to the btree.
	 */
	rr->mcur = xfs_rmapbt_mem_cursor(rr->sc->sa.pag, NULL,
			&rr->rmap_btree);

	error = xrep_rmap_count_records(rr->mcur, &rr->nr_records);
	if (error)
		goto err_mcur;

	/*
	 * Due to btree slack factors, it's possible for a new btree to be one
	 * level taller than the old btree.  Update the incore btree height so
	 * that we don't trip the verifiers when writing the new btree blocks
	 * to disk.
	 */
	pag->pagf_repair_rmap_level = rr->new_btree.bload.btree_height;

	/*
	 * Move the cursor to the left edge of the tree so that the first
	 * increment in ->get_records positions us at the first record.
	 */
	error = xfs_btree_goto_left_edge(rr->mcur);
	if (error)
		goto err_level;

	/* Add all observed rmap records. */
	error = xfs_btree_bload(rmap_cur, &rr->new_btree.bload, rr);
	if (error)
		goto err_level;

	/*
	 * Install the new btree in the AG header.  After this point the old
	 * btree is no longer accessible and the new tree is live.
	 */
	xfs_rmapbt_commit_staged_btree(rmap_cur, sc->tp, sc->sa.agf_bp);
	xfs_btree_del_cursor(rmap_cur, 0);
	xfs_btree_del_cursor(rr->mcur, 0);
	rr->mcur = NULL;

	/*
	 * Now that we've written the new btree to disk, we don't need to keep
	 * updating the in-memory btree.  Abort the scan to stop live updates.
	 */
	xchk_iscan_abort(&rr->iscan);

	/*
	 * The newly committed rmap recordset includes mappings for the blocks
	 * that we reserved to build the new btree.  If there is excess space
	 * reservation to be freed, the corresponding rmap records must also be
	 * removed.
	 */
	rr->new_btree.oinfo = XFS_RMAP_OINFO_AG;

	/* Reset the AGF counters now that we've changed the btree shape. */
	error = xrep_rmap_reset_counters(rr);
	if (error)
		goto err_newbt;

	/* Dispose of any unused blocks and the accounting information. */
	error = xrep_newbt_commit(&rr->new_btree);
	if (error)
		return error;

	return xrep_roll_ag_trans(sc);

err_level:
	pag->pagf_repair_rmap_level = 0;
err_mcur:
	xfs_btree_del_cursor(rr->mcur, error);
err_cur:
	xfs_btree_del_cursor(rmap_cur, error);
err_newbt:
	xrep_newbt_cancel(&rr->new_btree);
	return error;
}

/* Section (IV): Reaping the old btree. */

struct xrep_rmap_find_gaps {
	struct xagb_bitmap	rmap_gaps;
	xfs_agblock_t		next_agbno;
};

/* Subtract each free extent in the bnobt from the rmap gaps. */
STATIC int
xrep_rmap_find_freesp(
	struct xfs_btree_cur		*cur,
	const struct xfs_alloc_rec_incore *rec,
	void				*priv)
{
	struct xrep_rmap_find_gaps	*rfg = priv;

	return xagb_bitmap_clear(&rfg->rmap_gaps, rec->ar_startblock,
			rec->ar_blockcount);
}

/* Record the free space we find, as part of cleaning out the btree. */
STATIC int
xrep_rmap_find_gaps(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_rmap_find_gaps	*rfg = priv;
	int				error;

	if (rec->rm_startblock > rfg->next_agbno) {
		error = xagb_bitmap_set(&rfg->rmap_gaps, rfg->next_agbno,
				rec->rm_startblock - rfg->next_agbno);
		if (error)
			return error;
	}

	rfg->next_agbno = max_t(xfs_agblock_t, rfg->next_agbno,
				rec->rm_startblock + rec->rm_blockcount);
	return 0;
}

/*
 * Reap the old rmapbt blocks.  Now that the rmapbt is fully rebuilt, we make
 * a list of gaps in the rmap records and a list of the extents mentioned in
 * the bnobt.  Any block that's in the new rmapbt gap list but not mentioned
 * in the bnobt is a block from the old rmapbt and can be removed.
 */
STATIC int
xrep_rmap_remove_old_tree(
	struct xrep_rmap	*rr)
{
	struct xrep_rmap_find_gaps rfg = {
		.next_agbno	= 0,
	};
	struct xfs_scrub	*sc = rr->sc;
	struct xfs_agf		*agf = sc->sa.agf_bp->b_addr;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_btree_cur	*mcur;
	xfs_agblock_t		agend;
	int			error;

	xagb_bitmap_init(&rfg.rmap_gaps);

	/* Compute free space from the new rmapbt. */
	mcur = xfs_rmapbt_mem_cursor(rr->sc->sa.pag, NULL, &rr->rmap_btree);

	error = xfs_rmap_query_all(mcur, xrep_rmap_find_gaps, &rfg);
	xfs_btree_del_cursor(mcur, error);
	if (error)
		goto out_bitmap;

	/* Insert a record for space between the last rmap and EOAG. */
	agend = be32_to_cpu(agf->agf_length);
	if (rfg.next_agbno < agend) {
		error = xagb_bitmap_set(&rfg.rmap_gaps, rfg.next_agbno,
				agend - rfg.next_agbno);
		if (error)
			goto out_bitmap;
	}

	/* Compute free space from the existing bnobt. */
	sc->sa.bno_cur = xfs_bnobt_init_cursor(sc->mp, sc->tp, sc->sa.agf_bp,
			sc->sa.pag);
	error = xfs_alloc_query_all(sc->sa.bno_cur, xrep_rmap_find_freesp,
			&rfg);
	xfs_btree_del_cursor(sc->sa.bno_cur, error);
	sc->sa.bno_cur = NULL;
	if (error)
		goto out_bitmap;

	/*
	 * Free the "free" blocks that the new rmapbt knows about but the bnobt
	 * doesn't--these are the old rmapbt blocks.  Credit the old rmapbt
	 * block usage count back to the per-AG rmapbt reservation (and not
	 * fdblocks, since the rmap btree lives in free space) to keep the
	 * reservation and free space accounting correct.
	 */
	error = xrep_reap_agblocks(sc, &rfg.rmap_gaps,
			&XFS_RMAP_OINFO_ANY_OWNER, XFS_AG_RESV_RMAPBT);
	if (error)
		goto out_bitmap;

	/*
	 * Now that we've zapped all the old rmapbt blocks we can turn off
	 * the alternate height mechanism and reset the per-AG space
	 * reservation.
	 */
	pag->pagf_repair_rmap_level = 0;
	sc->flags |= XREP_RESET_PERAG_RESV;
out_bitmap:
	xagb_bitmap_destroy(&rfg.rmap_gaps);
	return error;
}

static inline bool
xrep_rmapbt_want_live_update(
	struct xchk_iscan		*iscan,
	const struct xfs_owner_info	*oi)
{
	if (xchk_iscan_aborted(iscan))
		return false;

	/*
	 * Before unlocking the AG header to perform the inode scan, we
	 * recorded reverse mappings for all AG metadata except for the OWN_AG
	 * metadata.  IOWs, the in-memory btree knows about the AG headers, the
	 * two inode btrees, the CoW staging extents, and the refcount btrees.
	 * For these types of metadata, we need to record the live updates in
	 * the in-memory rmap btree.
	 *
	 * However, we do not scan the free space btrees or the AGFL until we
	 * have re-locked the AGF and are ready to reserve space for the new
	 * rmap btree, so we do not want live updates for OWN_AG metadata.
	 */
	if (XFS_RMAP_NON_INODE_OWNER(oi->oi_owner))
		return oi->oi_owner != XFS_RMAP_OWN_AG;

	/* Ignore updates to files that the scanner hasn't visited yet. */
	return xchk_iscan_want_live_update(iscan, oi->oi_owner);
}

/*
 * Apply a rmapbt update from the regular filesystem into our shadow btree.
 * We're running from the thread that owns the AGF buffer and is generating
 * the update, so we must be careful about which parts of the struct xrep_rmap
 * that we change.
 */
static int
xrep_rmapbt_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_rmap_update_params	*p = data;
	struct xrep_rmap		*rr;
	struct xfs_mount		*mp;
	struct xfs_btree_cur		*mcur;
	struct xfs_trans		*tp;
	void				*txcookie;
	int				error;

	rr = container_of(nb, struct xrep_rmap, rhook.rmap_hook.nb);
	mp = rr->sc->mp;

	if (!xrep_rmapbt_want_live_update(&rr->iscan, &p->oinfo))
		goto out_unlock;

	trace_xrep_rmap_live_update(mp, rr->sc->sa.pag->pag_agno, action, p);

	error = xrep_trans_alloc_hook_dummy(mp, &txcookie, &tp);
	if (error)
		goto out_abort;

	mutex_lock(&rr->lock);
	mcur = xfs_rmapbt_mem_cursor(rr->sc->sa.pag, tp, &rr->rmap_btree);
	error = __xfs_rmap_finish_intent(mcur, action, p->startblock,
			p->blockcount, &p->oinfo, p->unwritten);
	xfs_btree_del_cursor(mcur, error);
	if (error)
		goto out_cancel;

	error = xfbtree_trans_commit(&rr->rmap_btree, tp);
	if (error)
		goto out_cancel;

	xrep_trans_cancel_hook_dummy(&txcookie, tp);
	mutex_unlock(&rr->lock);
	return NOTIFY_DONE;

out_cancel:
	xfbtree_trans_cancel(&rr->rmap_btree, tp);
	xrep_trans_cancel_hook_dummy(&txcookie, tp);
out_abort:
	mutex_unlock(&rr->lock);
	xchk_iscan_abort(&rr->iscan);
out_unlock:
	return NOTIFY_DONE;
}

/* Set up the filesystem scan components. */
STATIC int
xrep_rmap_setup_scan(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;
	int			error;

	mutex_init(&rr->lock);

	/* Set up in-memory rmap btree */
	error = xfs_rmapbt_mem_init(sc->mp, &rr->rmap_btree, sc->xmbtp,
			sc->sa.pag->pag_agno);
	if (error)
		goto out_mutex;

	/* Retry iget every tenth of a second for up to 30 seconds. */
	xchk_iscan_start(sc, 30000, 100, &rr->iscan);

	/*
	 * Hook into live rmap operations so that we can update our in-memory
	 * btree to reflect live changes on the filesystem.  Since we drop the
	 * AGF buffer to scan all the inodes, we need this piece to avoid
	 * installing a stale btree.
	 */
	ASSERT(sc->flags & XCHK_FSGATES_RMAP);
	xfs_rmap_hook_setup(&rr->rhook, xrep_rmapbt_live_update);
	error = xfs_rmap_hook_add(sc->sa.pag, &rr->rhook);
	if (error)
		goto out_iscan;
	return 0;

out_iscan:
	xchk_iscan_teardown(&rr->iscan);
	xfbtree_destroy(&rr->rmap_btree);
out_mutex:
	mutex_destroy(&rr->lock);
	return error;
}

/* Tear down scan components. */
STATIC void
xrep_rmap_teardown(
	struct xrep_rmap	*rr)
{
	struct xfs_scrub	*sc = rr->sc;

	xchk_iscan_abort(&rr->iscan);
	xfs_rmap_hook_del(sc->sa.pag, &rr->rhook);
	xchk_iscan_teardown(&rr->iscan);
	xfbtree_destroy(&rr->rmap_btree);
	mutex_destroy(&rr->lock);
}

/* Repair the rmap btree for some AG. */
int
xrep_rmapbt(
	struct xfs_scrub	*sc)
{
	struct xrep_rmap	*rr = sc->buf;
	int			error;

	error = xrep_rmap_setup_scan(rr);
	if (error)
		return error;

	/*
	 * Collect rmaps for everything in this AG that isn't space metadata.
	 * These rmaps won't change even as we try to allocate blocks.
	 */
	error = xrep_rmap_find_rmaps(rr);
	if (error)
		goto out_records;

	/* Rebuild the rmap information. */
	error = xrep_rmap_build_new_tree(rr);
	if (error)
		goto out_records;

	/* Kill the old tree. */
	error = xrep_rmap_remove_old_tree(rr);
	if (error)
		goto out_records;

out_records:
	xrep_rmap_teardown(rr);
	return error;
}
