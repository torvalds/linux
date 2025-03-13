// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_rtgroup.h"
#include "xfs_health.h"
#include "xfs_rtalloc.h"
#include "xfs_rtrmap_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/health.h"
#include "xfs_ag.h"

/* Set us up with an inode's bmap. */
int
xchk_setup_inode_bmap(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	error = xchk_iget_for_scrubbing(sc);
	if (error)
		goto out;

	xchk_ilock(sc, XFS_IOLOCK_EXCL);

	/*
	 * We don't want any ephemeral data/cow fork updates sitting around
	 * while we inspect block mappings, so wait for directio to finish
	 * and flush dirty data if we have delalloc reservations.
	 */
	if (S_ISREG(VFS_I(sc->ip)->i_mode) &&
	    sc->sm->sm_type != XFS_SCRUB_TYPE_BMBTA) {
		struct address_space	*mapping = VFS_I(sc->ip)->i_mapping;
		bool			is_repair = xchk_could_repair(sc);

		xchk_ilock(sc, XFS_MMAPLOCK_EXCL);

		/* Break all our leases, we're going to mess with things. */
		if (is_repair) {
			error = xfs_break_layouts(VFS_I(sc->ip),
					&sc->ilock_flags, BREAK_WRITE);
			if (error)
				goto out;
		}

		inode_dio_wait(VFS_I(sc->ip));

		/*
		 * Try to flush all incore state to disk before we examine the
		 * space mappings for the data fork.  Leave accumulated errors
		 * in the mapping for the writer threads to consume.
		 *
		 * On ENOSPC or EIO writeback errors, we continue into the
		 * extent mapping checks because write failures do not
		 * necessarily imply anything about the correctness of the file
		 * metadata.  The metadata and the file data could be on
		 * completely separate devices; a media failure might only
		 * affect a subset of the disk, etc.  We can handle delalloc
		 * extents in the scrubber, so leaving them in memory is fine.
		 */
		error = filemap_fdatawrite(mapping);
		if (!error)
			error = filemap_fdatawait_keep_errors(mapping);
		if (error && (error != -ENOSPC && error != -EIO))
			goto out;

		/* Drop the page cache if we're repairing block mappings. */
		if (is_repair) {
			error = invalidate_inode_pages2(
					VFS_I(sc->ip)->i_mapping);
			if (error)
				goto out;
		}

	}

	/* Got the inode, lock it and we're ready to go. */
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out;

	error = xchk_ino_dqattach(sc);
	if (error)
		goto out;

	xchk_ilock(sc, XFS_ILOCK_EXCL);
out:
	/* scrub teardown will unlock and release the inode */
	return error;
}

/*
 * Inode fork block mapping (BMBT) scrubber.
 * More complex than the others because we have to scrub
 * all the extents regardless of whether or not the fork
 * is in btree format.
 */

struct xchk_bmap_info {
	struct xfs_scrub	*sc;

	/* Incore extent tree cursor */
	struct xfs_iext_cursor	icur;

	/* Previous fork mapping that we examined */
	struct xfs_bmbt_irec	prev_rec;

	/* Is this a realtime fork? */
	bool			is_rt;

	/* May mappings point to shared space? */
	bool			is_shared;

	/* Was the incore extent tree loaded? */
	bool			was_loaded;

	/* Which inode fork are we checking? */
	int			whichfork;
};

/* Look for a corresponding rmap for this irec. */
static inline bool
xchk_bmap_get_rmap(
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec,
	xfs_agblock_t		bno,
	uint64_t		owner,
	struct xfs_rmap_irec	*rmap)
{
	struct xfs_btree_cur	**curp = &info->sc->sa.rmap_cur;
	xfs_fileoff_t		offset;
	unsigned int		rflags = 0;
	int			has_rmap;
	int			error;

	if (xfs_ifork_is_realtime(info->sc->ip, info->whichfork))
		curp = &info->sc->sr.rmap_cur;

	if (*curp == NULL)
		return false;

	if (info->whichfork == XFS_ATTR_FORK)
		rflags |= XFS_RMAP_ATTR_FORK;
	if (irec->br_state == XFS_EXT_UNWRITTEN)
		rflags |= XFS_RMAP_UNWRITTEN;

	/*
	 * CoW staging extents are owned (on disk) by the refcountbt, so
	 * their rmaps do not have offsets.
	 */
	if (info->whichfork == XFS_COW_FORK)
		offset = 0;
	else
		offset = irec->br_startoff;

	/*
	 * If the caller thinks this could be a shared bmbt extent (IOWs,
	 * any data fork extent of a reflink inode) then we have to use the
	 * range rmap lookup to make sure we get the correct owner/offset.
	 */
	if (info->is_shared) {
		error = xfs_rmap_lookup_le_range(*curp, bno, owner, offset,
				rflags, rmap, &has_rmap);
	} else {
		error = xfs_rmap_lookup_le(*curp, bno, owner, offset,
				rflags, rmap, &has_rmap);
	}
	if (!xchk_should_check_xref(info->sc, &error, curp))
		return false;

	if (!has_rmap)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
			irec->br_startoff);
	return has_rmap;
}

/* Make sure that we have rmapbt records for this data/attr fork extent. */
STATIC void
xchk_bmap_xref_rmap(
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec,
	xfs_agblock_t		bno)
{
	struct xfs_rmap_irec	rmap;
	unsigned long long	rmap_end;
	uint64_t		owner = info->sc->ip->i_ino;

	if (xchk_skip_xref(info->sc->sm))
		return;

	/* Find the rmap record for this irec. */
	if (!xchk_bmap_get_rmap(info, irec, bno, owner, &rmap))
		return;

	/*
	 * The rmap must be an exact match for this incore file mapping record,
	 * which may have arisen from multiple ondisk records.
	 */
	if (rmap.rm_startblock != bno)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	rmap_end = (unsigned long long)rmap.rm_startblock + rmap.rm_blockcount;
	if (rmap_end != bno + irec->br_blockcount)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* Check the logical offsets. */
	if (rmap.rm_offset != irec->br_startoff)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	rmap_end = (unsigned long long)rmap.rm_offset + rmap.rm_blockcount;
	if (rmap_end != irec->br_startoff + irec->br_blockcount)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* Check the owner */
	if (rmap.rm_owner != owner)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/*
	 * Check for discrepancies between the unwritten flag in the irec and
	 * the rmap.  Note that the (in-memory) CoW fork distinguishes between
	 * unwritten and written extents, but we don't track that in the rmap
	 * records because the blocks are owned (on-disk) by the refcountbt,
	 * which doesn't track unwritten state.
	 */
	if (!!(irec->br_state == XFS_EXT_UNWRITTEN) !=
	    !!(rmap.rm_flags & XFS_RMAP_UNWRITTEN))
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (!!(info->whichfork == XFS_ATTR_FORK) !=
	    !!(rmap.rm_flags & XFS_RMAP_ATTR_FORK))
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (rmap.rm_flags & XFS_RMAP_BMBT_BLOCK)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
}

/* Make sure that we have rmapbt records for this COW fork extent. */
STATIC void
xchk_bmap_xref_rmap_cow(
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec,
	xfs_agblock_t		bno)
{
	struct xfs_rmap_irec	rmap;
	unsigned long long	rmap_end;
	uint64_t		owner = XFS_RMAP_OWN_COW;

	if (!info->sc->sa.rmap_cur || xchk_skip_xref(info->sc->sm))
		return;

	/* Find the rmap record for this irec. */
	if (!xchk_bmap_get_rmap(info, irec, bno, owner, &rmap))
		return;

	/*
	 * CoW staging extents are owned by the refcount btree, so the rmap
	 * can start before and end after the physical space allocated to this
	 * mapping.  There are no offsets to check.
	 */
	if (rmap.rm_startblock > bno)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	rmap_end = (unsigned long long)rmap.rm_startblock + rmap.rm_blockcount;
	if (rmap_end < bno + irec->br_blockcount)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* Check the owner */
	if (rmap.rm_owner != owner)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/*
	 * No flags allowed.  Note that the (in-memory) CoW fork distinguishes
	 * between unwritten and written extents, but we don't track that in
	 * the rmap records because the blocks are owned (on-disk) by the
	 * refcountbt, which doesn't track unwritten state.
	 */
	if (rmap.rm_flags & XFS_RMAP_ATTR_FORK)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (rmap.rm_flags & XFS_RMAP_BMBT_BLOCK)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (rmap.rm_flags & XFS_RMAP_UNWRITTEN)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
}

/* Cross-reference a single rtdev extent record. */
STATIC void
xchk_bmap_rt_iextent_xref(
	struct xfs_inode	*ip,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_owner_info	oinfo;
	xfs_rgblock_t		rgbno;
	int			error;

	error = xchk_rtgroup_init_existing(info->sc,
			xfs_rtb_to_rgno(ip->i_mount, irec->br_startblock),
			&info->sc->sr);
	if (!xchk_fblock_process_error(info->sc, info->whichfork,
			irec->br_startoff, &error))
		return;

	error = xchk_rtgroup_lock(info->sc, &info->sc->sr, XCHK_RTGLOCK_ALL);
	if (!xchk_fblock_process_error(info->sc, info->whichfork,
			irec->br_startoff, &error))
		goto out_free;

	xchk_xref_is_used_rt_space(info->sc, irec->br_startblock,
			irec->br_blockcount);

	if (!xfs_has_rtrmapbt(info->sc->mp))
		goto out_cur;

	rgbno = xfs_rtb_to_rgbno(info->sc->mp, irec->br_startblock);

	switch (info->whichfork) {
	case XFS_DATA_FORK:
		xchk_bmap_xref_rmap(info, irec, rgbno);
		if (!xfs_is_reflink_inode(info->sc->ip)) {
			xfs_rmap_ino_owner(&oinfo, info->sc->ip->i_ino,
					info->whichfork, irec->br_startoff);
			xchk_xref_is_only_rt_owned_by(info->sc, rgbno,
					irec->br_blockcount, &oinfo);
			xchk_xref_is_not_rt_shared(info->sc, rgbno,
					irec->br_blockcount);
		}
		xchk_xref_is_not_rt_cow_staging(info->sc, rgbno,
				irec->br_blockcount);
		break;
	case XFS_COW_FORK:
		xchk_bmap_xref_rmap_cow(info, irec, rgbno);
		xchk_xref_is_only_rt_owned_by(info->sc, rgbno,
				irec->br_blockcount, &XFS_RMAP_OINFO_COW);
		xchk_xref_is_rt_cow_staging(info->sc, rgbno,
				irec->br_blockcount);
		xchk_xref_is_not_rt_shared(info->sc, rgbno,
				irec->br_blockcount);
		break;
	}
out_cur:
	xchk_rtgroup_btcur_free(&info->sc->sr);
out_free:
	xchk_rtgroup_free(info->sc, &info->sc->sr);
}

/* Cross-reference a single datadev extent record. */
STATIC void
xchk_bmap_iextent_xref(
	struct xfs_inode	*ip,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_owner_info	oinfo;
	struct xfs_mount	*mp = info->sc->mp;
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	xfs_extlen_t		len;
	int			error;

	agno = XFS_FSB_TO_AGNO(mp, irec->br_startblock);
	agbno = XFS_FSB_TO_AGBNO(mp, irec->br_startblock);
	len = irec->br_blockcount;

	error = xchk_ag_init_existing(info->sc, agno, &info->sc->sa);
	if (!xchk_fblock_process_error(info->sc, info->whichfork,
			irec->br_startoff, &error))
		goto out_free;

	xchk_xref_is_used_space(info->sc, agbno, len);
	xchk_xref_is_not_inode_chunk(info->sc, agbno, len);
	switch (info->whichfork) {
	case XFS_DATA_FORK:
		xchk_bmap_xref_rmap(info, irec, agbno);
		if (!xfs_is_reflink_inode(info->sc->ip)) {
			xfs_rmap_ino_owner(&oinfo, info->sc->ip->i_ino,
					info->whichfork, irec->br_startoff);
			xchk_xref_is_only_owned_by(info->sc, agbno,
					irec->br_blockcount, &oinfo);
			xchk_xref_is_not_shared(info->sc, agbno,
					irec->br_blockcount);
		}
		xchk_xref_is_not_cow_staging(info->sc, agbno,
				irec->br_blockcount);
		break;
	case XFS_ATTR_FORK:
		xchk_bmap_xref_rmap(info, irec, agbno);
		xfs_rmap_ino_owner(&oinfo, info->sc->ip->i_ino,
				info->whichfork, irec->br_startoff);
		xchk_xref_is_only_owned_by(info->sc, agbno, irec->br_blockcount,
				&oinfo);
		xchk_xref_is_not_shared(info->sc, agbno,
				irec->br_blockcount);
		xchk_xref_is_not_cow_staging(info->sc, agbno,
				irec->br_blockcount);
		break;
	case XFS_COW_FORK:
		xchk_bmap_xref_rmap_cow(info, irec, agbno);
		xchk_xref_is_only_owned_by(info->sc, agbno, irec->br_blockcount,
				&XFS_RMAP_OINFO_COW);
		xchk_xref_is_cow_staging(info->sc, agbno,
				irec->br_blockcount);
		xchk_xref_is_not_shared(info->sc, agbno,
				irec->br_blockcount);
		break;
	}

out_free:
	xchk_ag_free(info->sc, &info->sc->sa);
}

/*
 * Directories and attr forks should never have blocks that can't be addressed
 * by a xfs_dablk_t.
 */
STATIC void
xchk_bmap_dirattr_extent(
	struct xfs_inode	*ip,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		off;

	if (!S_ISDIR(VFS_I(ip)->i_mode) && info->whichfork != XFS_ATTR_FORK)
		return;

	if (!xfs_verify_dablk(mp, irec->br_startoff))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	off = irec->br_startoff + irec->br_blockcount - 1;
	if (!xfs_verify_dablk(mp, off))
		xchk_fblock_set_corrupt(info->sc, info->whichfork, off);
}

/* Scrub a single extent record. */
STATIC void
xchk_bmap_iextent(
	struct xfs_inode	*ip,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_mount	*mp = info->sc->mp;

	/*
	 * Check for out-of-order extents.  This record could have come
	 * from the incore list, for which there is no ordering check.
	 */
	if (irec->br_startoff < info->prev_rec.br_startoff +
				info->prev_rec.br_blockcount)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (!xfs_verify_fileext(mp, irec->br_startoff, irec->br_blockcount))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	xchk_bmap_dirattr_extent(ip, info, irec);

	/* Make sure the extent points to a valid place. */
	if (info->is_rt &&
	    !xfs_verify_rtbext(mp, irec->br_startblock, irec->br_blockcount))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (!info->is_rt &&
	    !xfs_verify_fsbext(mp, irec->br_startblock, irec->br_blockcount))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* We don't allow unwritten extents on attr forks. */
	if (irec->br_state == XFS_EXT_UNWRITTEN &&
	    info->whichfork == XFS_ATTR_FORK)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (info->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	if (info->is_rt)
		xchk_bmap_rt_iextent_xref(ip, info, irec);
	else
		xchk_bmap_iextent_xref(ip, info, irec);
}

/* Scrub a bmbt record. */
STATIC int
xchk_bmapbt_rec(
	struct xchk_btree	*bs,
	const union xfs_btree_rec *rec)
{
	struct xfs_bmbt_irec	irec;
	struct xfs_bmbt_irec	iext_irec;
	struct xfs_iext_cursor	icur;
	struct xchk_bmap_info	*info = bs->private;
	struct xfs_inode	*ip = bs->cur->bc_ino.ip;
	struct xfs_buf		*bp = NULL;
	struct xfs_btree_block	*block;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, info->whichfork);
	uint64_t		owner;
	int			i;

	/*
	 * Check the owners of the btree blocks up to the level below
	 * the root since the verifiers don't do that.
	 */
	if (xfs_has_crc(bs->cur->bc_mp) &&
	    bs->cur->bc_levels[0].ptr == 1) {
		for (i = 0; i < bs->cur->bc_nlevels - 1; i++) {
			block = xfs_btree_get_block(bs->cur, i, &bp);
			owner = be64_to_cpu(block->bb_u.l.bb_owner);
			if (owner != ip->i_ino)
				xchk_fblock_set_corrupt(bs->sc,
						info->whichfork, 0);
		}
	}

	/*
	 * Check that the incore extent tree contains an extent that matches
	 * this one exactly.  We validate those cached bmaps later, so we don't
	 * need to check them here.  If the incore extent tree was just loaded
	 * from disk by the scrubber, we assume that its contents match what's
	 * on disk (we still hold the ILOCK) and skip the equivalence check.
	 */
	if (!info->was_loaded)
		return 0;

	xfs_bmbt_disk_get_all(&rec->bmbt, &irec);
	if (xfs_bmap_validate_extent(ip, info->whichfork, &irec) != NULL) {
		xchk_fblock_set_corrupt(bs->sc, info->whichfork,
				irec.br_startoff);
		return 0;
	}

	if (!xfs_iext_lookup_extent(ip, ifp, irec.br_startoff, &icur,
				&iext_irec) ||
	    irec.br_startoff != iext_irec.br_startoff ||
	    irec.br_startblock != iext_irec.br_startblock ||
	    irec.br_blockcount != iext_irec.br_blockcount ||
	    irec.br_state != iext_irec.br_state)
		xchk_fblock_set_corrupt(bs->sc, info->whichfork,
				irec.br_startoff);
	return 0;
}

/* Scan the btree records. */
STATIC int
xchk_bmap_btree(
	struct xfs_scrub	*sc,
	int			whichfork,
	struct xchk_bmap_info	*info)
{
	struct xfs_owner_info	oinfo;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->ip, whichfork);
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	struct xfs_btree_cur	*cur;
	int			error;

	/* Load the incore bmap cache if it's not loaded. */
	info->was_loaded = !xfs_need_iread_extents(ifp);

	error = xfs_iread_extents(sc->tp, ip, whichfork);
	if (!xchk_fblock_process_error(sc, whichfork, 0, &error))
		goto out;

	/* Check the btree structure. */
	cur = xfs_bmbt_init_cursor(mp, sc->tp, ip, whichfork);
	xfs_rmap_ino_bmbt_owner(&oinfo, ip->i_ino, whichfork);
	error = xchk_btree(sc, cur, xchk_bmapbt_rec, &oinfo, info);
	xfs_btree_del_cursor(cur, error);
out:
	return error;
}

struct xchk_bmap_check_rmap_info {
	struct xfs_scrub	*sc;
	int			whichfork;
	struct xfs_iext_cursor	icur;
};

/* Can we find bmaps that fit this rmap? */
STATIC int
xchk_bmap_check_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xfs_bmbt_irec		irec;
	struct xfs_rmap_irec		check_rec;
	struct xchk_bmap_check_rmap_info	*sbcri = priv;
	struct xfs_ifork		*ifp;
	struct xfs_scrub		*sc = sbcri->sc;
	bool				have_map;

	/* Is this even the right fork? */
	if (rec->rm_owner != sc->ip->i_ino)
		return 0;
	if ((sbcri->whichfork == XFS_ATTR_FORK) ^
	    !!(rec->rm_flags & XFS_RMAP_ATTR_FORK))
		return 0;
	if (rec->rm_flags & XFS_RMAP_BMBT_BLOCK)
		return 0;

	/* Now look up the bmbt record. */
	ifp = xfs_ifork_ptr(sc->ip, sbcri->whichfork);
	if (!ifp) {
		xchk_fblock_set_corrupt(sc, sbcri->whichfork,
				rec->rm_offset);
		goto out;
	}
	have_map = xfs_iext_lookup_extent(sc->ip, ifp, rec->rm_offset,
			&sbcri->icur, &irec);
	if (!have_map)
		xchk_fblock_set_corrupt(sc, sbcri->whichfork,
				rec->rm_offset);
	/*
	 * bmap extent record lengths are constrained to 2^21 blocks in length
	 * because of space constraints in the on-disk metadata structure.
	 * However, rmap extent record lengths are constrained only by AG
	 * length, so we have to loop through the bmbt to make sure that the
	 * entire rmap is covered by bmbt records.
	 */
	check_rec = *rec;
	while (have_map) {
		if (irec.br_startoff != check_rec.rm_offset)
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					check_rec.rm_offset);
		if (irec.br_startblock !=
		    xfs_gbno_to_fsb(cur->bc_group, check_rec.rm_startblock))
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					check_rec.rm_offset);
		if (irec.br_blockcount > check_rec.rm_blockcount)
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					check_rec.rm_offset);
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			break;
		check_rec.rm_startblock += irec.br_blockcount;
		check_rec.rm_offset += irec.br_blockcount;
		check_rec.rm_blockcount -= irec.br_blockcount;
		if (check_rec.rm_blockcount == 0)
			break;
		have_map = xfs_iext_next_extent(ifp, &sbcri->icur, &irec);
		if (!have_map)
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					check_rec.rm_offset);
	}

out:
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return -ECANCELED;
	return 0;
}

/* Make sure each rmap has a corresponding bmbt entry. */
STATIC int
xchk_bmap_check_ag_rmaps(
	struct xfs_scrub		*sc,
	int				whichfork,
	struct xfs_perag		*pag)
{
	struct xchk_bmap_check_rmap_info	sbcri;
	struct xfs_btree_cur		*cur;
	struct xfs_buf			*agf;
	int				error;

	error = xfs_alloc_read_agf(pag, sc->tp, 0, &agf);
	if (error)
		return error;

	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, agf, pag);

	sbcri.sc = sc;
	sbcri.whichfork = whichfork;
	error = xfs_rmap_query_all(cur, xchk_bmap_check_rmap, &sbcri);
	if (error == -ECANCELED)
		error = 0;

	xfs_btree_del_cursor(cur, error);
	xfs_trans_brelse(sc->tp, agf);
	return error;
}

/* Make sure each rt rmap has a corresponding bmbt entry. */
STATIC int
xchk_bmap_check_rt_rmaps(
	struct xfs_scrub		*sc,
	struct xfs_rtgroup		*rtg)
{
	struct xchk_bmap_check_rmap_info sbcri;
	struct xfs_btree_cur		*cur;
	int				error;

	xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
	cur = xfs_rtrmapbt_init_cursor(sc->tp, rtg);

	sbcri.sc = sc;
	sbcri.whichfork = XFS_DATA_FORK;
	error = xfs_rmap_query_all(cur, xchk_bmap_check_rmap, &sbcri);
	if (error == -ECANCELED)
		error = 0;

	xfs_btree_del_cursor(cur, error);
	xfs_rtgroup_unlock(rtg, XFS_RTGLOCK_RMAP);
	return error;
}

/*
 * Decide if we want to scan the reverse mappings to determine if the attr
 * fork /really/ has zero space mappings.
 */
STATIC bool
xchk_bmap_check_empty_attrfork(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*ifp = &ip->i_af;

	/*
	 * If the dinode repair found a bad attr fork, it will reset the fork
	 * to extents format with zero records and wait for the this scrubber
	 * to reconstruct the block mappings.  If the fork is not in this
	 * state, then the fork cannot have been zapped.
	 */
	if (ifp->if_format != XFS_DINODE_FMT_EXTENTS || ifp->if_nextents != 0)
		return false;

	/*
	 * Files can have an attr fork in EXTENTS format with zero records for
	 * several reasons:
	 *
	 * a) an attr set created a fork but ran out of space
	 * b) attr replace deleted an old attr but failed during the set step
	 * c) the data fork was in btree format when all attrs were deleted, so
	 *    the fork was left in place
	 * d) the inode repair code zapped the fork
	 *
	 * Only in case (d) do we want to scan the rmapbt to see if we need to
	 * rebuild the attr fork.  The fork zap code clears all DAC permission
	 * bits and zeroes the uid and gid, so avoid the scan if any of those
	 * three conditions are not met.
	 */
	if ((VFS_I(ip)->i_mode & 0777) != 0)
		return false;
	if (!uid_eq(VFS_I(ip)->i_uid, GLOBAL_ROOT_UID))
		return false;
	if (!gid_eq(VFS_I(ip)->i_gid, GLOBAL_ROOT_GID))
		return false;

	return true;
}

/*
 * Decide if we want to scan the reverse mappings to determine if the data
 * fork /really/ has zero space mappings.
 */
STATIC bool
xchk_bmap_check_empty_datafork(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*ifp = &ip->i_df;

	/*
	 * If the dinode repair found a bad data fork, it will reset the fork
	 * to extents format with zero records and wait for the this scrubber
	 * to reconstruct the block mappings.  If the fork is not in this
	 * state, then the fork cannot have been zapped.
	 */
	if (ifp->if_format != XFS_DINODE_FMT_EXTENTS || ifp->if_nextents != 0)
		return false;

	/*
	 * If we encounter an empty data fork along with evidence that the fork
	 * might not really be empty, we need to scan the reverse mappings to
	 * decide if we're going to rebuild the fork.  Data forks with nonzero
	 * file size are scanned.
	 */
	return i_size_read(VFS_I(ip)) != 0;
}

/*
 * Decide if we want to walk every rmap btree in the fs to make sure that each
 * rmap for this file fork has corresponding bmbt entries.
 */
static bool
xchk_bmap_want_check_rmaps(
	struct xchk_bmap_info	*info)
{
	struct xfs_scrub	*sc = info->sc;

	if (!xfs_has_rmapbt(sc->mp))
		return false;
	if (info->whichfork == XFS_COW_FORK)
		return false;
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return false;

	if (info->whichfork == XFS_ATTR_FORK)
		return xchk_bmap_check_empty_attrfork(sc->ip);

	return xchk_bmap_check_empty_datafork(sc->ip);
}

/* Make sure each rmap has a corresponding bmbt entry. */
STATIC int
xchk_bmap_check_rmaps(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	struct xfs_perag	*pag = NULL;
	int			error;

	if (xfs_ifork_is_realtime(sc->ip, whichfork)) {
		struct xfs_rtgroup	*rtg = NULL;

		while ((rtg = xfs_rtgroup_next(sc->mp, rtg))) {
			error = xchk_bmap_check_rt_rmaps(sc, rtg);
			if (error ||
			    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
				xfs_rtgroup_rele(rtg);
				return error;
			}
		}

		return 0;
	}

	while ((pag = xfs_perag_next(sc->mp, pag))) {
		error = xchk_bmap_check_ag_rmaps(sc, whichfork, pag);
		if (error ||
		    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
			xfs_perag_rele(pag);
			return error;
		}
	}

	return 0;
}

/* Scrub a delalloc reservation from the incore extent map tree. */
STATIC void
xchk_bmap_iextent_delalloc(
	struct xfs_inode	*ip,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_mount	*mp = info->sc->mp;

	/*
	 * Check for out-of-order extents.  This record could have come
	 * from the incore list, for which there is no ordering check.
	 */
	if (irec->br_startoff < info->prev_rec.br_startoff +
				info->prev_rec.br_blockcount)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (!xfs_verify_fileext(mp, irec->br_startoff, irec->br_blockcount))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* Make sure the extent points to a valid place. */
	if (irec->br_blockcount > XFS_MAX_BMBT_EXTLEN)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
}

/* Decide if this individual fork mapping is ok. */
static bool
xchk_bmap_iext_mapping(
	struct xchk_bmap_info		*info,
	const struct xfs_bmbt_irec	*irec)
{
	/* There should never be a "hole" extent in either extent list. */
	if (irec->br_startblock == HOLESTARTBLOCK)
		return false;
	if (irec->br_blockcount > XFS_MAX_BMBT_EXTLEN)
		return false;
	return true;
}

/* Are these two mappings contiguous with each other? */
static inline bool
xchk_are_bmaps_contiguous(
	const struct xchk_bmap_info	*info,
	const struct xfs_bmbt_irec	*b1,
	const struct xfs_bmbt_irec	*b2)
{
	struct xfs_mount		*mp = info->sc->mp;

	/* Don't try to combine unallocated mappings. */
	if (!xfs_bmap_is_real_extent(b1))
		return false;
	if (!xfs_bmap_is_real_extent(b2))
		return false;

	/* Does b2 come right after b1 in the logical and physical range? */
	if (b1->br_startoff + b1->br_blockcount != b2->br_startoff)
		return false;
	if (b1->br_startblock + b1->br_blockcount != b2->br_startblock)
		return false;
	if (b1->br_state != b2->br_state)
		return false;

	/*
	 * Don't combine bmaps that would cross rtgroup boundaries.  This is a
	 * valid state, but if combined they will fail rtb extent checks.
	 */
	if (info->is_rt && xfs_has_rtgroups(mp)) {
		if (xfs_rtb_to_rgno(mp, b1->br_startblock) !=
		    xfs_rtb_to_rgno(mp, b2->br_startblock))
			return false;
	}

	return true;
}

/*
 * Walk the incore extent records, accumulating consecutive contiguous records
 * into a single incore mapping.  Returns true if @irec has been set to a
 * mapping or false if there are no more mappings.  Caller must ensure that
 * @info.icur is zeroed before the first call.
 */
static bool
xchk_bmap_iext_iter(
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_bmbt_irec	got;
	struct xfs_ifork	*ifp;
	unsigned int		nr = 0;

	ifp = xfs_ifork_ptr(info->sc->ip, info->whichfork);

	/* Advance to the next iextent record and check the mapping. */
	xfs_iext_next(ifp, &info->icur);
	if (!xfs_iext_get_extent(ifp, &info->icur, irec))
		return false;

	if (!xchk_bmap_iext_mapping(info, irec)) {
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
		return false;
	}
	nr++;

	/*
	 * Iterate subsequent iextent records and merge them with the one
	 * that we just read, if possible.
	 */
	while (xfs_iext_peek_next_extent(ifp, &info->icur, &got)) {
		if (!xchk_are_bmaps_contiguous(info, irec, &got))
			break;

		if (!xchk_bmap_iext_mapping(info, &got)) {
			xchk_fblock_set_corrupt(info->sc, info->whichfork,
					got.br_startoff);
			return false;
		}
		nr++;

		irec->br_blockcount += got.br_blockcount;
		xfs_iext_next(ifp, &info->icur);
	}

	/*
	 * If the merged mapping could be expressed with fewer bmbt records
	 * than we actually found, notify the user that this fork could be
	 * optimized.  CoW forks only exist in memory so we ignore them.
	 */
	if (nr > 1 && info->whichfork != XFS_COW_FORK &&
	    howmany_64(irec->br_blockcount, XFS_MAX_BMBT_EXTLEN) < nr)
		xchk_ino_set_preen(info->sc, info->sc->ip->i_ino);

	return true;
}

/*
 * Scrub an inode fork's block mappings.
 *
 * First we scan every record in every btree block, if applicable.
 * Then we unconditionally scan the incore extent cache.
 */
STATIC int
xchk_bmap(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	struct xfs_bmbt_irec	irec;
	struct xchk_bmap_info	info = { NULL };
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	xfs_fileoff_t		endoff;
	int			error = 0;

	/* Non-existent forks can be ignored. */
	if (!ifp)
		return -ENOENT;

	info.is_rt = xfs_ifork_is_realtime(ip, whichfork);
	info.whichfork = whichfork;
	info.is_shared = whichfork == XFS_DATA_FORK && xfs_is_reflink_inode(ip);
	info.sc = sc;

	switch (whichfork) {
	case XFS_COW_FORK:
		/* No CoW forks on non-reflink filesystems. */
		if (!xfs_has_reflink(mp)) {
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
			return 0;
		}
		break;
	case XFS_ATTR_FORK:
		/*
		 * "attr" means that an attr fork was created at some point in
		 * the life of this filesystem.  "attr2" means that inodes have
		 * variable-sized data/attr fork areas.  Hence we only check
		 * attr here.
		 */
		if (!xfs_has_attr(mp))
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		break;
	default:
		ASSERT(whichfork == XFS_DATA_FORK);
		break;
	}

	/* Check the fork values */
	switch (ifp->if_format) {
	case XFS_DINODE_FMT_UUID:
	case XFS_DINODE_FMT_DEV:
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_META_BTREE:
		/* No mappings to check. */
		if (whichfork == XFS_COW_FORK)
			xchk_fblock_set_corrupt(sc, whichfork, 0);
		return 0;
	case XFS_DINODE_FMT_EXTENTS:
		break;
	case XFS_DINODE_FMT_BTREE:
		if (whichfork == XFS_COW_FORK) {
			xchk_fblock_set_corrupt(sc, whichfork, 0);
			return 0;
		}

		error = xchk_bmap_btree(sc, whichfork, &info);
		if (error)
			return error;
		break;
	default:
		xchk_fblock_set_corrupt(sc, whichfork, 0);
		return 0;
	}

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* Find the offset of the last extent in the mapping. */
	error = xfs_bmap_last_offset(ip, &endoff, whichfork);
	if (!xchk_fblock_process_error(sc, whichfork, 0, &error))
		return error;

	/*
	 * Scrub extent records.  We use a special iterator function here that
	 * combines adjacent mappings if they are logically and physically
	 * contiguous.   For large allocations that require multiple bmbt
	 * records, this reduces the number of cross-referencing calls, which
	 * reduces runtime.  Cross referencing with the rmap is simpler because
	 * the rmap must match the combined mapping exactly.
	 */
	while (xchk_bmap_iext_iter(&info, &irec)) {
		if (xchk_should_terminate(sc, &error) ||
		    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
			return 0;

		if (irec.br_startoff >= endoff) {
			xchk_fblock_set_corrupt(sc, whichfork,
					irec.br_startoff);
			return 0;
		}

		if (isnullstartblock(irec.br_startblock))
			xchk_bmap_iextent_delalloc(ip, &info, &irec);
		else
			xchk_bmap_iextent(ip, &info, &irec);
		memcpy(&info.prev_rec, &irec, sizeof(struct xfs_bmbt_irec));
	}

	if (xchk_bmap_want_check_rmaps(&info)) {
		error = xchk_bmap_check_rmaps(sc, whichfork);
		if (!xchk_fblock_xref_process_error(sc, whichfork, 0, &error))
			return error;
	}

	return 0;
}

/* Scrub an inode's data fork. */
int
xchk_bmap_data(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_file_looks_zapped(sc, XFS_SICK_INO_BMBTD_ZAPPED)) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		return 0;
	}

	error = xchk_bmap(sc, XFS_DATA_FORK);
	if (error)
		return error;

	/* If the data fork is clean, it is clearly not zapped. */
	xchk_mark_healthy_if_clean(sc, XFS_SICK_INO_BMBTD_ZAPPED);
	return 0;
}

/* Scrub an inode's attr fork. */
int
xchk_bmap_attr(
	struct xfs_scrub	*sc)
{
	int			error;

	/*
	 * If the attr fork has been zapped, it's possible that forkoff was
	 * reset to zero and hence sc->ip->i_afp is NULL.  We don't want the
	 * NULL ifp check in xchk_bmap to conclude that the attr fork is ok,
	 * so short circuit that logic by setting the corruption flag and
	 * returning immediately.
	 */
	if (xchk_file_looks_zapped(sc, XFS_SICK_INO_BMBTA_ZAPPED)) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		return 0;
	}

	error = xchk_bmap(sc, XFS_ATTR_FORK);
	if (error)
		return error;

	/* If the attr fork is clean, it is clearly not zapped. */
	xchk_mark_healthy_if_clean(sc, XFS_SICK_INO_BMBTA_ZAPPED);
	return 0;
}

/* Scrub an inode's CoW fork. */
int
xchk_bmap_cow(
	struct xfs_scrub	*sc)
{
	return xchk_bmap(sc, XFS_COW_FORK);
}
