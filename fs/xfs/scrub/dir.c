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
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_health.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/dabtree.h"
#include "scrub/readdir.h"
#include "scrub/health.h"

/* Set us up to scrub directories. */
int
xchk_setup_directory(
	struct xfs_scrub	*sc)
{
	return xchk_setup_inode_contents(sc, 0);
}

/* Directories */

/* Scrub a directory entry. */

/* Check that an inode's mode matches a given XFS_DIR3_FT_* type. */
STATIC void
xchk_dir_check_ftype(
	struct xfs_scrub	*sc,
	xfs_fileoff_t		offset,
	struct xfs_inode	*ip,
	int			ftype)
{
	struct xfs_mount	*mp = sc->mp;

	if (!xfs_has_ftype(mp)) {
		if (ftype != XFS_DIR3_FT_UNKNOWN && ftype != XFS_DIR3_FT_DIR)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
		return;
	}

	if (xfs_mode_to_ftype(VFS_I(ip)->i_mode) != ftype)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
}

/*
 * Scrub a single directory entry.
 *
 * Check the inode number to make sure it's sane, then we check that we can
 * look up this filename.  Finally, we check the ftype.
 */
STATIC int
xchk_dir_actor(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xfs_dir2_dataptr_t	dapos,
	const struct xfs_name	*name,
	xfs_ino_t		ino,
	void			*priv)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip;
	xfs_ino_t		lookup_ino;
	xfs_dablk_t		offset;
	int			error = 0;

	offset = xfs_dir2_db_to_da(mp->m_dir_geo,
			xfs_dir2_dataptr_to_db(mp->m_dir_geo, dapos));

	if (xchk_should_terminate(sc, &error))
		return error;

	/* Does this inode number make sense? */
	if (!xfs_verify_dir_ino(mp, ino)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
		return -ECANCELED;
	}

	/* Does this name make sense? */
	if (!xfs_dir2_namecheck(name->name, name->len)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
		return -ECANCELED;
	}

	if (xfs_dir2_samename(name, &xfs_name_dot)) {
		/* If this is "." then check that the inum matches the dir. */
		if (ino != dp->i_ino)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
	} else if (xfs_dir2_samename(name, &xfs_name_dotdot)) {
		/*
		 * If this is ".." in the root inode, check that the inum
		 * matches this dir.
		 */
		if (dp->i_ino == mp->m_sb.sb_rootino && ino != dp->i_ino)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
	}

	/* Verify that we can look up this name by hash. */
	error = xchk_dir_lookup(sc, dp, name, &lookup_ino);
	/* ENOENT means the hash lookup failed and the dir is corrupt */
	if (error == -ENOENT)
		error = -EFSCORRUPTED;
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, offset, &error))
		goto out;
	if (lookup_ino != ino) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);
		return -ECANCELED;
	}

	/*
	 * Grab the inode pointed to by the dirent.  We release the inode
	 * before we cancel the scrub transaction.
	 *
	 * If _iget returns -EINVAL or -ENOENT then the child inode number is
	 * garbage and the directory is corrupt.  If the _iget returns
	 * -EFSCORRUPTED or -EFSBADCRC then the child is corrupt which is a
	 *  cross referencing error.  Any other error is an operational error.
	 */
	error = xchk_iget(sc, ino, &ip);
	if (error == -EINVAL || error == -ENOENT) {
		error = -EFSCORRUPTED;
		xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error);
		goto out;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, offset, &error))
		goto out;

	xchk_dir_check_ftype(sc, offset, ip, name->type);
	xchk_irele(sc, ip);
out:
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return -ECANCELED;
	return error;
}

/* Scrub a directory btree record. */
STATIC int
xchk_dir_rec(
	struct xchk_da_btree		*ds,
	int				level)
{
	struct xfs_name			dname = { };
	struct xfs_da_state_blk		*blk = &ds->state->path.blk[level];
	struct xfs_mount		*mp = ds->state->mp;
	struct xfs_inode		*dp = ds->dargs.dp;
	struct xfs_da_geometry		*geo = mp->m_dir_geo;
	struct xfs_dir2_data_entry	*dent;
	struct xfs_buf			*bp;
	struct xfs_dir2_leaf_entry	*ent;
	unsigned int			end;
	unsigned int			iter_off;
	xfs_ino_t			ino;
	xfs_dablk_t			rec_bno;
	xfs_dir2_db_t			db;
	xfs_dir2_data_aoff_t		off;
	xfs_dir2_dataptr_t		ptr;
	xfs_dahash_t			calc_hash;
	xfs_dahash_t			hash;
	struct xfs_dir3_icleaf_hdr	hdr;
	unsigned int			tag;
	int				error;

	ASSERT(blk->magic == XFS_DIR2_LEAF1_MAGIC ||
	       blk->magic == XFS_DIR2_LEAFN_MAGIC);

	xfs_dir2_leaf_hdr_from_disk(mp, &hdr, blk->bp->b_addr);
	ent = hdr.ents + blk->index;

	/* Check the hash of the entry. */
	error = xchk_da_btree_hash(ds, level, &ent->hashval);
	if (error)
		goto out;

	/* Valid hash pointer? */
	ptr = be32_to_cpu(ent->address);
	if (ptr == 0)
		return 0;

	/* Find the directory entry's location. */
	db = xfs_dir2_dataptr_to_db(geo, ptr);
	off = xfs_dir2_dataptr_to_off(geo, ptr);
	rec_bno = xfs_dir2_db_to_da(geo, db);

	if (rec_bno >= geo->leafblk) {
		xchk_da_set_corrupt(ds, level);
		goto out;
	}
	error = xfs_dir3_data_read(ds->dargs.trans, dp, rec_bno,
			XFS_DABUF_MAP_HOLE_OK, &bp);
	if (!xchk_fblock_process_error(ds->sc, XFS_DATA_FORK, rec_bno,
			&error))
		goto out;
	if (!bp) {
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out;
	}
	xchk_buffer_recheck(ds->sc, bp);

	if (ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out_relse;

	dent = bp->b_addr + off;

	/* Make sure we got a real directory entry. */
	iter_off = geo->data_entry_offset;
	end = xfs_dir3_data_end_offset(geo, bp->b_addr);
	if (!end) {
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out_relse;
	}
	for (;;) {
		struct xfs_dir2_data_entry	*dep = bp->b_addr + iter_off;
		struct xfs_dir2_data_unused	*dup = bp->b_addr + iter_off;

		if (iter_off >= end) {
			xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
			goto out_relse;
		}

		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			iter_off += be16_to_cpu(dup->length);
			continue;
		}
		if (dep == dent)
			break;
		iter_off += xfs_dir2_data_entsize(mp, dep->namelen);
	}

	/* Retrieve the entry, sanity check it, and compare hashes. */
	ino = be64_to_cpu(dent->inumber);
	hash = be32_to_cpu(ent->hashval);
	tag = be16_to_cpup(xfs_dir2_data_entry_tag_p(mp, dent));
	if (!xfs_verify_dir_ino(mp, ino) || tag != off)
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
	if (dent->namelen == 0) {
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out_relse;
	}

	/* Does the directory hash match? */
	dname.name = dent->name;
	dname.len = dent->namelen;
	calc_hash = xfs_dir2_hashname(mp, &dname);
	if (calc_hash != hash)
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);

out_relse:
	xfs_trans_brelse(ds->dargs.trans, bp);
out:
	return error;
}

/*
 * Is this unused entry either in the bestfree or smaller than all of
 * them?  We've already checked that the bestfrees are sorted longest to
 * shortest, and that there aren't any bogus entries.
 */
STATIC void
xchk_directory_check_free_entry(
	struct xfs_scrub		*sc,
	xfs_dablk_t			lblk,
	struct xfs_dir2_data_free	*bf,
	struct xfs_dir2_data_unused	*dup)
{
	struct xfs_dir2_data_free	*dfp;
	unsigned int			dup_length;

	dup_length = be16_to_cpu(dup->length);

	/* Unused entry is shorter than any of the bestfrees */
	if (dup_length < be16_to_cpu(bf[XFS_DIR2_DATA_FD_COUNT - 1].length))
		return;

	for (dfp = &bf[XFS_DIR2_DATA_FD_COUNT - 1]; dfp >= bf; dfp--)
		if (dup_length == be16_to_cpu(dfp->length))
			return;

	/* Unused entry should be in the bestfrees but wasn't found. */
	xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
}

/* Check free space info in a directory data block. */
STATIC int
xchk_directory_data_bestfree(
	struct xfs_scrub		*sc,
	xfs_dablk_t			lblk,
	bool				is_block)
{
	struct xfs_dir2_data_unused	*dup;
	struct xfs_dir2_data_free	*dfp;
	struct xfs_buf			*bp;
	struct xfs_dir2_data_free	*bf;
	struct xfs_mount		*mp = sc->mp;
	u16				tag;
	unsigned int			nr_bestfrees = 0;
	unsigned int			nr_frees = 0;
	unsigned int			smallest_bestfree;
	int				newlen;
	unsigned int			offset;
	unsigned int			end;
	int				error;

	if (is_block) {
		/* dir block format */
		if (lblk != XFS_B_TO_FSBT(mp, XFS_DIR2_DATA_OFFSET))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		error = xfs_dir3_block_read(sc->tp, sc->ip, &bp);
	} else {
		/* dir data format */
		error = xfs_dir3_data_read(sc->tp, sc->ip, lblk, 0, &bp);
	}
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		goto out;
	xchk_buffer_recheck(sc, bp);

	/* XXX: Check xfs_dir3_data_hdr.pad is zero once we start setting it. */

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out_buf;

	/* Do the bestfrees correspond to actual free space? */
	bf = xfs_dir2_data_bestfree_p(mp, bp->b_addr);
	smallest_bestfree = UINT_MAX;
	for (dfp = &bf[0]; dfp < &bf[XFS_DIR2_DATA_FD_COUNT]; dfp++) {
		offset = be16_to_cpu(dfp->offset);
		if (offset == 0)
			continue;
		if (offset >= mp->m_dir_geo->blksize) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out_buf;
		}
		dup = bp->b_addr + offset;
		tag = be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup));

		/* bestfree doesn't match the entry it points at? */
		if (dup->freetag != cpu_to_be16(XFS_DIR2_DATA_FREE_TAG) ||
		    be16_to_cpu(dup->length) != be16_to_cpu(dfp->length) ||
		    tag != offset) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out_buf;
		}

		/* bestfree records should be ordered largest to smallest */
		if (smallest_bestfree < be16_to_cpu(dfp->length)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out_buf;
		}

		smallest_bestfree = be16_to_cpu(dfp->length);
		nr_bestfrees++;
	}

	/* Make sure the bestfrees are actually the best free spaces. */
	offset = mp->m_dir_geo->data_entry_offset;
	end = xfs_dir3_data_end_offset(mp->m_dir_geo, bp->b_addr);

	/* Iterate the entries, stopping when we hit or go past the end. */
	while (offset < end) {
		dup = bp->b_addr + offset;

		/* Skip real entries */
		if (dup->freetag != cpu_to_be16(XFS_DIR2_DATA_FREE_TAG)) {
			struct xfs_dir2_data_entry *dep = bp->b_addr + offset;

			newlen = xfs_dir2_data_entsize(mp, dep->namelen);
			if (newlen <= 0) {
				xchk_fblock_set_corrupt(sc, XFS_DATA_FORK,
						lblk);
				goto out_buf;
			}
			offset += newlen;
			continue;
		}

		/* Spot check this free entry */
		tag = be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup));
		if (tag != offset) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out_buf;
		}

		/*
		 * Either this entry is a bestfree or it's smaller than
		 * any of the bestfrees.
		 */
		xchk_directory_check_free_entry(sc, lblk, bf, dup);
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			goto out_buf;

		/* Move on. */
		newlen = be16_to_cpu(dup->length);
		if (newlen <= 0) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out_buf;
		}
		offset += newlen;
		if (offset <= end)
			nr_frees++;
	}

	/* We're required to fill all the space. */
	if (offset != end)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);

	/* Did we see at least as many free slots as there are bestfrees? */
	if (nr_frees < nr_bestfrees)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
out_buf:
	xfs_trans_brelse(sc->tp, bp);
out:
	return error;
}

/*
 * Does the free space length in the free space index block ($len) match
 * the longest length in the directory data block's bestfree array?
 * Assume that we've already checked that the data block's bestfree
 * array is in order.
 */
STATIC void
xchk_directory_check_freesp(
	struct xfs_scrub		*sc,
	xfs_dablk_t			lblk,
	struct xfs_buf			*dbp,
	unsigned int			len)
{
	struct xfs_dir2_data_free	*dfp;

	dfp = xfs_dir2_data_bestfree_p(sc->mp, dbp->b_addr);

	if (len != be16_to_cpu(dfp->length))
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);

	if (len > 0 && be16_to_cpu(dfp->offset) == 0)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
}

/* Check free space info in a directory leaf1 block. */
STATIC int
xchk_directory_leaf1_bestfree(
	struct xfs_scrub		*sc,
	struct xfs_da_args		*args,
	xfs_dir2_db_t			last_data_db,
	xfs_dablk_t			lblk)
{
	struct xfs_dir3_icleaf_hdr	leafhdr;
	struct xfs_dir2_leaf_tail	*ltp;
	struct xfs_dir2_leaf		*leaf;
	struct xfs_buf			*dbp;
	struct xfs_buf			*bp;
	struct xfs_da_geometry		*geo = sc->mp->m_dir_geo;
	__be16				*bestp;
	__u16				best;
	__u32				hash;
	__u32				lasthash = 0;
	__u32				bestcount;
	unsigned int			stale = 0;
	int				i;
	int				error;

	/* Read the free space block. */
	error = xfs_dir3_leaf_read(sc->tp, sc->ip, lblk, &bp);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		return error;
	xchk_buffer_recheck(sc, bp);

	leaf = bp->b_addr;
	xfs_dir2_leaf_hdr_from_disk(sc->ip->i_mount, &leafhdr, leaf);
	ltp = xfs_dir2_leaf_tail_p(geo, leaf);
	bestcount = be32_to_cpu(ltp->bestcount);
	bestp = xfs_dir2_leaf_bests_p(ltp);

	if (xfs_has_crc(sc->mp)) {
		struct xfs_dir3_leaf_hdr	*hdr3 = bp->b_addr;

		if (hdr3->pad != cpu_to_be32(0))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
	}

	/*
	 * There must be enough bestfree slots to cover all the directory data
	 * blocks that we scanned.  It is possible for there to be a hole
	 * between the last data block and i_disk_size.  This seems like an
	 * oversight to the scrub author, but as we have been writing out
	 * directories like this (and xfs_repair doesn't mind them) for years,
	 * that's what we have to check.
	 */
	if (bestcount != last_data_db + 1) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		goto out;
	}

	/* Is the leaf count even remotely sane? */
	if (leafhdr.count > geo->leaf_max_ents) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		goto out;
	}

	/* Leaves and bests don't overlap in leaf format. */
	if ((char *)&leafhdr.ents[leafhdr.count] > (char *)bestp) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		goto out;
	}

	/* Check hash value order, count stale entries.  */
	for (i = 0; i < leafhdr.count; i++) {
		hash = be32_to_cpu(leafhdr.ents[i].hashval);
		if (i > 0 && lasthash > hash)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		lasthash = hash;
		if (leafhdr.ents[i].address ==
		    cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
			stale++;
	}
	if (leafhdr.stale != stale)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Check all the bestfree entries. */
	for (i = 0; i < bestcount; i++, bestp++) {
		best = be16_to_cpu(*bestp);
		error = xfs_dir3_data_read(sc->tp, sc->ip,
				xfs_dir2_db_to_da(args->geo, i),
				XFS_DABUF_MAP_HOLE_OK,
				&dbp);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk,
				&error))
			break;

		if (!dbp) {
			if (best != NULLDATAOFF) {
				xchk_fblock_set_corrupt(sc, XFS_DATA_FORK,
						lblk);
				break;
			}
			continue;
		}

		if (best == NULLDATAOFF)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		else
			xchk_directory_check_freesp(sc, lblk, dbp, best);
		xfs_trans_brelse(sc->tp, dbp);
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			break;
	}
out:
	xfs_trans_brelse(sc->tp, bp);
	return error;
}

/* Check free space info in a directory freespace block. */
STATIC int
xchk_directory_free_bestfree(
	struct xfs_scrub		*sc,
	struct xfs_da_args		*args,
	xfs_dablk_t			lblk)
{
	struct xfs_dir3_icfree_hdr	freehdr;
	struct xfs_buf			*dbp;
	struct xfs_buf			*bp;
	__u16				best;
	unsigned int			stale = 0;
	int				i;
	int				error;

	/* Read the free space block */
	error = xfs_dir2_free_read(sc->tp, sc->ip, lblk, &bp);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		return error;
	xchk_buffer_recheck(sc, bp);

	if (xfs_has_crc(sc->mp)) {
		struct xfs_dir3_free_hdr	*hdr3 = bp->b_addr;

		if (hdr3->pad != cpu_to_be32(0))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
	}

	/* Check all the entries. */
	xfs_dir2_free_hdr_from_disk(sc->ip->i_mount, &freehdr, bp->b_addr);
	for (i = 0; i < freehdr.nvalid; i++) {
		best = be16_to_cpu(freehdr.bests[i]);
		if (best == NULLDATAOFF) {
			stale++;
			continue;
		}
		error = xfs_dir3_data_read(sc->tp, sc->ip,
				(freehdr.firstdb + i) * args->geo->fsbcount,
				0, &dbp);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk,
				&error))
			goto out;
		xchk_directory_check_freesp(sc, lblk, dbp, best);
		xfs_trans_brelse(sc->tp, dbp);
	}

	if (freehdr.nused + stale != freehdr.nvalid)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
out:
	xfs_trans_brelse(sc->tp, bp);
	return error;
}

/* Check free space information in directories. */
STATIC int
xchk_directory_blocks(
	struct xfs_scrub	*sc)
{
	struct xfs_bmbt_irec	got;
	struct xfs_da_args	args = {
		.dp		= sc ->ip,
		.whichfork	= XFS_DATA_FORK,
		.geo		= sc->mp->m_dir_geo,
		.trans		= sc->tp,
	};
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->ip, XFS_DATA_FORK);
	struct xfs_mount	*mp = sc->mp;
	xfs_fileoff_t		leaf_lblk;
	xfs_fileoff_t		free_lblk;
	xfs_fileoff_t		lblk;
	struct xfs_iext_cursor	icur;
	xfs_dablk_t		dabno;
	xfs_dir2_db_t		last_data_db = 0;
	bool			found;
	bool			is_block = false;
	int			error;

	/* Ignore local format directories. */
	if (ifp->if_format != XFS_DINODE_FMT_EXTENTS &&
	    ifp->if_format != XFS_DINODE_FMT_BTREE)
		return 0;

	lblk = XFS_B_TO_FSB(mp, XFS_DIR2_DATA_OFFSET);
	leaf_lblk = XFS_B_TO_FSB(mp, XFS_DIR2_LEAF_OFFSET);
	free_lblk = XFS_B_TO_FSB(mp, XFS_DIR2_FREE_OFFSET);

	/* Is this a block dir? */
	error = xfs_dir2_isblock(&args, &is_block);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		goto out;

	/* Iterate all the data extents in the directory... */
	found = xfs_iext_lookup_extent(sc->ip, ifp, lblk, &icur, &got);
	while (found && !(sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
		/* No more data blocks... */
		if (got.br_startoff >= leaf_lblk)
			break;

		/*
		 * Check each data block's bestfree data.
		 *
		 * Iterate all the fsbcount-aligned block offsets in
		 * this directory.  The directory block reading code is
		 * smart enough to do its own bmap lookups to handle
		 * discontiguous directory blocks.  When we're done
		 * with the extent record, re-query the bmap at the
		 * next fsbcount-aligned offset to avoid redundant
		 * block checks.
		 */
		for (lblk = roundup((xfs_dablk_t)got.br_startoff,
				args.geo->fsbcount);
		     lblk < got.br_startoff + got.br_blockcount;
		     lblk += args.geo->fsbcount) {
			last_data_db = xfs_dir2_da_to_db(args.geo, lblk);
			error = xchk_directory_data_bestfree(sc, lblk,
					is_block);
			if (error)
				goto out;
		}
		dabno = got.br_startoff + got.br_blockcount;
		lblk = roundup(dabno, args.geo->fsbcount);
		found = xfs_iext_lookup_extent(sc->ip, ifp, lblk, &icur, &got);
	}

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Look for a leaf1 block, which has free info. */
	if (xfs_iext_lookup_extent(sc->ip, ifp, leaf_lblk, &icur, &got) &&
	    got.br_startoff == leaf_lblk &&
	    got.br_blockcount == args.geo->fsbcount &&
	    !xfs_iext_next_extent(ifp, &icur, &got)) {
		if (is_block) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out;
		}
		error = xchk_directory_leaf1_bestfree(sc, &args, last_data_db,
				leaf_lblk);
		if (error)
			goto out;
	}

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Scan for free blocks */
	lblk = free_lblk;
	found = xfs_iext_lookup_extent(sc->ip, ifp, lblk, &icur, &got);
	while (found && !(sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
		/*
		 * Dirs can't have blocks mapped above 2^32.
		 * Single-block dirs shouldn't even be here.
		 */
		lblk = got.br_startoff;
		if (lblk & ~0xFFFFFFFFULL) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out;
		}
		if (is_block) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out;
		}

		/*
		 * Check each dir free block's bestfree data.
		 *
		 * Iterate all the fsbcount-aligned block offsets in
		 * this directory.  The directory block reading code is
		 * smart enough to do its own bmap lookups to handle
		 * discontiguous directory blocks.  When we're done
		 * with the extent record, re-query the bmap at the
		 * next fsbcount-aligned offset to avoid redundant
		 * block checks.
		 */
		for (lblk = roundup((xfs_dablk_t)got.br_startoff,
				args.geo->fsbcount);
		     lblk < got.br_startoff + got.br_blockcount;
		     lblk += args.geo->fsbcount) {
			error = xchk_directory_free_bestfree(sc, &args,
					lblk);
			if (error)
				goto out;
		}
		dabno = got.br_startoff + got.br_blockcount;
		lblk = roundup(dabno, args.geo->fsbcount);
		found = xfs_iext_lookup_extent(sc->ip, ifp, lblk, &icur, &got);
	}
out:
	return error;
}

/* Scrub a whole directory. */
int
xchk_directory(
	struct xfs_scrub	*sc)
{
	int			error;

	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -ENOENT;

	if (xchk_file_looks_zapped(sc, XFS_SICK_INO_DIR_ZAPPED)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/* Plausible size? */
	if (sc->ip->i_disk_size < xfs_dir2_sf_hdr_size(0)) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		return 0;
	}

	/* Check directory tree structure */
	error = xchk_da_btree(sc, XFS_DATA_FORK, xchk_dir_rec, NULL);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* Check the freespace. */
	error = xchk_directory_blocks(sc);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* Look up every name in this directory by hash. */
	error = xchk_dir_walk(sc, sc->ip, xchk_dir_actor, NULL);
	if (error && error != -ECANCELED)
		return error;

	/* If the dir is clean, it is clearly not zapped. */
	xchk_mark_healthy_if_clean(sc, XFS_SICK_INO_DIR_ZAPPED);
	return 0;
}

/*
 * Decide if this directory has been zapped to satisfy the inode and ifork
 * verifiers.  Checking and repairing should be postponed until the directory
 * is fixed.
 */
bool
xchk_dir_looks_zapped(
	struct xfs_inode	*dp)
{
	/* Repair zapped this dir's data fork a short time ago */
	if (xfs_ifork_zapped(dp, XFS_DATA_FORK))
		return true;

	/*
	 * If the dinode repair found a bad data fork, it will reset the fork
	 * to extents format with zero records and wait for the bmapbtd
	 * scrubber to reconstruct the block mappings.  Directories always
	 * contain some content, so this is a clear sign of a zapped directory.
	 * The state checked by xfs_ifork_zapped is not persisted, so this is
	 * the secondary strategy if repairs are interrupted by a crash or an
	 * unmount.
	 */
	return dp->i_df.if_format == XFS_DINODE_FMT_EXTENTS &&
	       dp->i_df.if_nextents == 0;
}
