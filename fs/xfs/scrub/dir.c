// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_itable.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_ialloc.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/dabtree.h"

/* Set us up to scrub directories. */
int
xchk_setup_directory(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	return xchk_setup_inode_contents(sc, ip, 0);
}

/* Directories */

/* Scrub a directory entry. */

struct xchk_dir_ctx {
	/* VFS fill-directory iterator */
	struct dir_context		dir_iter;

	struct xfs_scrub_context	*sc;
};

/* Check that an inode's mode matches a given DT_ type. */
STATIC int
xchk_dir_check_ftype(
	struct xchk_dir_ctx		*sdc,
	xfs_fileoff_t			offset,
	xfs_ino_t			inum,
	int				dtype)
{
	struct xfs_mount		*mp = sdc->sc->mp;
	struct xfs_inode		*ip;
	int				ino_dtype;
	int				error = 0;

	if (!xfs_sb_version_hasftype(&mp->m_sb)) {
		if (dtype != DT_UNKNOWN && dtype != DT_DIR)
			xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
		goto out;
	}

	/*
	 * Grab the inode pointed to by the dirent.  We release the
	 * inode before we cancel the scrub transaction.  Since we're
	 * don't know a priori that releasing the inode won't trigger
	 * eofblocks cleanup (which allocates what would be a nested
	 * transaction), we can't use DONTCACHE here because DONTCACHE
	 * inodes can trigger immediate inactive cleanup of the inode.
	 */
	error = xfs_iget(mp, sdc->sc->tp, inum, 0, 0, &ip);
	if (!xchk_fblock_xref_process_error(sdc->sc, XFS_DATA_FORK, offset,
			&error))
		goto out;

	/* Convert mode to the DT_* values that dir_emit uses. */
	ino_dtype = xfs_dir3_get_dtype(mp,
			xfs_mode_to_ftype(VFS_I(ip)->i_mode));
	if (ino_dtype != dtype)
		xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK, offset);
	iput(VFS_I(ip));
out:
	return error;
}

/*
 * Scrub a single directory entry.
 *
 * We use the VFS directory iterator (i.e. readdir) to call this
 * function for every directory entry in a directory.  Once we're here,
 * we check the inode number to make sure it's sane, then we check that
 * we can look up this filename.  Finally, we check the ftype.
 */
STATIC int
xchk_dir_actor(
	struct dir_context		*dir_iter,
	const char			*name,
	int				namelen,
	loff_t				pos,
	u64				ino,
	unsigned			type)
{
	struct xfs_mount		*mp;
	struct xfs_inode		*ip;
	struct xchk_dir_ctx		*sdc;
	struct xfs_name			xname;
	xfs_ino_t			lookup_ino;
	xfs_dablk_t			offset;
	int				error = 0;

	sdc = container_of(dir_iter, struct xchk_dir_ctx, dir_iter);
	ip = sdc->sc->ip;
	mp = ip->i_mount;
	offset = xfs_dir2_db_to_da(mp->m_dir_geo,
			xfs_dir2_dataptr_to_db(mp->m_dir_geo, pos));

	/* Does this inode number make sense? */
	if (!xfs_verify_dir_ino(mp, ino)) {
		xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK, offset);
		goto out;
	}

	if (!strncmp(".", name, namelen)) {
		/* If this is "." then check that the inum matches the dir. */
		if (xfs_sb_version_hasftype(&mp->m_sb) && type != DT_DIR)
			xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
		if (ino != ip->i_ino)
			xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
	} else if (!strncmp("..", name, namelen)) {
		/*
		 * If this is ".." in the root inode, check that the inum
		 * matches this dir.
		 */
		if (xfs_sb_version_hasftype(&mp->m_sb) && type != DT_DIR)
			xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
		if (ip->i_ino == mp->m_sb.sb_rootino && ino != ip->i_ino)
			xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK,
					offset);
	}

	/* Verify that we can look up this name by hash. */
	xname.name = name;
	xname.len = namelen;
	xname.type = XFS_DIR3_FT_UNKNOWN;

	error = xfs_dir_lookup(sdc->sc->tp, ip, &xname, &lookup_ino, NULL);
	if (!xchk_fblock_process_error(sdc->sc, XFS_DATA_FORK, offset,
			&error))
		goto out;
	if (lookup_ino != ino) {
		xchk_fblock_set_corrupt(sdc->sc, XFS_DATA_FORK, offset);
		goto out;
	}

	/* Verify the file type.  This function absorbs error codes. */
	error = xchk_dir_check_ftype(sdc, offset, lookup_ino, type);
	if (error)
		goto out;
out:
	/*
	 * A negative error code returned here is supposed to cause the
	 * dir_emit caller (xfs_readdir) to abort the directory iteration
	 * and return zero to xchk_directory.
	 */
	if (error == 0 && sdc->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return -EFSCORRUPTED;
	return error;
}

/* Scrub a directory btree record. */
STATIC int
xchk_dir_rec(
	struct xchk_da_btree		*ds,
	int				level,
	void				*rec)
{
	struct xfs_mount		*mp = ds->state->mp;
	struct xfs_dir2_leaf_entry	*ent = rec;
	struct xfs_inode		*dp = ds->dargs.dp;
	struct xfs_dir2_data_entry	*dent;
	struct xfs_buf			*bp;
	char				*p, *endp;
	xfs_ino_t			ino;
	xfs_dablk_t			rec_bno;
	xfs_dir2_db_t			db;
	xfs_dir2_data_aoff_t		off;
	xfs_dir2_dataptr_t		ptr;
	xfs_dahash_t			calc_hash;
	xfs_dahash_t			hash;
	unsigned int			tag;
	int				error;

	/* Check the hash of the entry. */
	error = xchk_da_btree_hash(ds, level, &ent->hashval);
	if (error)
		goto out;

	/* Valid hash pointer? */
	ptr = be32_to_cpu(ent->address);
	if (ptr == 0)
		return 0;

	/* Find the directory entry's location. */
	db = xfs_dir2_dataptr_to_db(mp->m_dir_geo, ptr);
	off = xfs_dir2_dataptr_to_off(mp->m_dir_geo, ptr);
	rec_bno = xfs_dir2_db_to_da(mp->m_dir_geo, db);

	if (rec_bno >= mp->m_dir_geo->leafblk) {
		xchk_da_set_corrupt(ds, level);
		goto out;
	}
	error = xfs_dir3_data_read(ds->dargs.trans, dp, rec_bno, -2, &bp);
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

	dent = (struct xfs_dir2_data_entry *)(((char *)bp->b_addr) + off);

	/* Make sure we got a real directory entry. */
	p = (char *)mp->m_dir_inode_ops->data_entry_p(bp->b_addr);
	endp = xfs_dir3_data_endp(mp->m_dir_geo, bp->b_addr);
	if (!endp) {
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out_relse;
	}
	while (p < endp) {
		struct xfs_dir2_data_entry	*dep;
		struct xfs_dir2_data_unused	*dup;

		dup = (struct xfs_dir2_data_unused *)p;
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			p += be16_to_cpu(dup->length);
			continue;
		}
		dep = (struct xfs_dir2_data_entry *)p;
		if (dep == dent)
			break;
		p += mp->m_dir_inode_ops->data_entsize(dep->namelen);
	}
	if (p >= endp) {
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out_relse;
	}

	/* Retrieve the entry, sanity check it, and compare hashes. */
	ino = be64_to_cpu(dent->inumber);
	hash = be32_to_cpu(ent->hashval);
	tag = be16_to_cpup(dp->d_ops->data_entry_tag_p(dent));
	if (!xfs_verify_dir_ino(mp, ino) || tag != off)
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
	if (dent->namelen == 0) {
		xchk_fblock_set_corrupt(ds->sc, XFS_DATA_FORK, rec_bno);
		goto out_relse;
	}
	calc_hash = xfs_da_hashname(dent->name, dent->namelen);
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
	struct xfs_scrub_context	*sc,
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
	struct xfs_scrub_context	*sc,
	xfs_dablk_t			lblk,
	bool				is_block)
{
	struct xfs_dir2_data_unused	*dup;
	struct xfs_dir2_data_free	*dfp;
	struct xfs_buf			*bp;
	struct xfs_dir2_data_free	*bf;
	struct xfs_mount		*mp = sc->mp;
	const struct xfs_dir_ops	*d_ops;
	char				*ptr;
	char				*endptr;
	u16				tag;
	unsigned int			nr_bestfrees = 0;
	unsigned int			nr_frees = 0;
	unsigned int			smallest_bestfree;
	int				newlen;
	int				offset;
	int				error;

	d_ops = sc->ip->d_ops;

	if (is_block) {
		/* dir block format */
		if (lblk != XFS_B_TO_FSBT(mp, XFS_DIR2_DATA_OFFSET))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		error = xfs_dir3_block_read(sc->tp, sc->ip, &bp);
	} else {
		/* dir data format */
		error = xfs_dir3_data_read(sc->tp, sc->ip, lblk, -1, &bp);
	}
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		goto out;
	xchk_buffer_recheck(sc, bp);

	/* XXX: Check xfs_dir3_data_hdr.pad is zero once we start setting it. */

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out_buf;

	/* Do the bestfrees correspond to actual free space? */
	bf = d_ops->data_bestfree_p(bp->b_addr);
	smallest_bestfree = UINT_MAX;
	for (dfp = &bf[0]; dfp < &bf[XFS_DIR2_DATA_FD_COUNT]; dfp++) {
		offset = be16_to_cpu(dfp->offset);
		if (offset == 0)
			continue;
		if (offset >= mp->m_dir_geo->blksize) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
			goto out_buf;
		}
		dup = (struct xfs_dir2_data_unused *)(bp->b_addr + offset);
		tag = be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup));

		/* bestfree doesn't match the entry it points at? */
		if (dup->freetag != cpu_to_be16(XFS_DIR2_DATA_FREE_TAG) ||
		    be16_to_cpu(dup->length) != be16_to_cpu(dfp->length) ||
		    tag != ((char *)dup - (char *)bp->b_addr)) {
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
	ptr = (char *)d_ops->data_entry_p(bp->b_addr);
	endptr = xfs_dir3_data_endp(mp->m_dir_geo, bp->b_addr);

	/* Iterate the entries, stopping when we hit or go past the end. */
	while (ptr < endptr) {
		dup = (struct xfs_dir2_data_unused *)ptr;
		/* Skip real entries */
		if (dup->freetag != cpu_to_be16(XFS_DIR2_DATA_FREE_TAG)) {
			struct xfs_dir2_data_entry	*dep;

			dep = (struct xfs_dir2_data_entry *)ptr;
			newlen = d_ops->data_entsize(dep->namelen);
			if (newlen <= 0) {
				xchk_fblock_set_corrupt(sc, XFS_DATA_FORK,
						lblk);
				goto out_buf;
			}
			ptr += newlen;
			continue;
		}

		/* Spot check this free entry */
		tag = be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup));
		if (tag != ((char *)dup - (char *)bp->b_addr)) {
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
		ptr += newlen;
		if (ptr <= endptr)
			nr_frees++;
	}

	/* We're required to fill all the space. */
	if (ptr != endptr)
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
	struct xfs_scrub_context	*sc,
	xfs_dablk_t			lblk,
	struct xfs_buf			*dbp,
	unsigned int			len)
{
	struct xfs_dir2_data_free	*dfp;

	dfp = sc->ip->d_ops->data_bestfree_p(dbp->b_addr);

	if (len != be16_to_cpu(dfp->length))
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);

	if (len > 0 && be16_to_cpu(dfp->offset) == 0)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
}

/* Check free space info in a directory leaf1 block. */
STATIC int
xchk_directory_leaf1_bestfree(
	struct xfs_scrub_context	*sc,
	struct xfs_da_args		*args,
	xfs_dablk_t			lblk)
{
	struct xfs_dir3_icleaf_hdr	leafhdr;
	struct xfs_dir2_leaf_entry	*ents;
	struct xfs_dir2_leaf_tail	*ltp;
	struct xfs_dir2_leaf		*leaf;
	struct xfs_buf			*dbp;
	struct xfs_buf			*bp;
	const struct xfs_dir_ops	*d_ops = sc->ip->d_ops;
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
	error = xfs_dir3_leaf_read(sc->tp, sc->ip, lblk, -1, &bp);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		goto out;
	xchk_buffer_recheck(sc, bp);

	leaf = bp->b_addr;
	d_ops->leaf_hdr_from_disk(&leafhdr, leaf);
	ents = d_ops->leaf_ents_p(leaf);
	ltp = xfs_dir2_leaf_tail_p(geo, leaf);
	bestcount = be32_to_cpu(ltp->bestcount);
	bestp = xfs_dir2_leaf_bests_p(ltp);

	if (xfs_sb_version_hascrc(&sc->mp->m_sb)) {
		struct xfs_dir3_leaf_hdr	*hdr3 = bp->b_addr;

		if (hdr3->pad != cpu_to_be32(0))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
	}

	/*
	 * There should be as many bestfree slots as there are dir data
	 * blocks that can fit under i_size.
	 */
	if (bestcount != xfs_dir2_byte_to_db(geo, sc->ip->i_d.di_size)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		goto out;
	}

	/* Is the leaf count even remotely sane? */
	if (leafhdr.count > d_ops->leaf_max_ents(geo)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		goto out;
	}

	/* Leaves and bests don't overlap in leaf format. */
	if ((char *)&ents[leafhdr.count] > (char *)bestp) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		goto out;
	}

	/* Check hash value order, count stale entries.  */
	for (i = 0; i < leafhdr.count; i++) {
		hash = be32_to_cpu(ents[i].hashval);
		if (i > 0 && lasthash > hash)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
		lasthash = hash;
		if (ents[i].address == cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
			stale++;
	}
	if (leafhdr.stale != stale)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Check all the bestfree entries. */
	for (i = 0; i < bestcount; i++, bestp++) {
		best = be16_to_cpu(*bestp);
		if (best == NULLDATAOFF)
			continue;
		error = xfs_dir3_data_read(sc->tp, sc->ip,
				i * args->geo->fsbcount, -1, &dbp);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk,
				&error))
			break;
		xchk_directory_check_freesp(sc, lblk, dbp, best);
		xfs_trans_brelse(sc->tp, dbp);
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			goto out;
	}
out:
	return error;
}

/* Check free space info in a directory freespace block. */
STATIC int
xchk_directory_free_bestfree(
	struct xfs_scrub_context	*sc,
	struct xfs_da_args		*args,
	xfs_dablk_t			lblk)
{
	struct xfs_dir3_icfree_hdr	freehdr;
	struct xfs_buf			*dbp;
	struct xfs_buf			*bp;
	__be16				*bestp;
	__u16				best;
	unsigned int			stale = 0;
	int				i;
	int				error;

	/* Read the free space block */
	error = xfs_dir2_free_read(sc->tp, sc->ip, lblk, &bp);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		goto out;
	xchk_buffer_recheck(sc, bp);

	if (xfs_sb_version_hascrc(&sc->mp->m_sb)) {
		struct xfs_dir3_free_hdr	*hdr3 = bp->b_addr;

		if (hdr3->pad != cpu_to_be32(0))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
	}

	/* Check all the entries. */
	sc->ip->d_ops->free_hdr_from_disk(&freehdr, bp->b_addr);
	bestp = sc->ip->d_ops->free_bests_p(bp->b_addr);
	for (i = 0; i < freehdr.nvalid; i++, bestp++) {
		best = be16_to_cpu(*bestp);
		if (best == NULLDATAOFF) {
			stale++;
			continue;
		}
		error = xfs_dir3_data_read(sc->tp, sc->ip,
				(freehdr.firstdb + i) * args->geo->fsbcount,
				-1, &dbp);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk,
				&error))
			break;
		xchk_directory_check_freesp(sc, lblk, dbp, best);
		xfs_trans_brelse(sc->tp, dbp);
	}

	if (freehdr.nused + stale != freehdr.nvalid)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, lblk);
out:
	return error;
}

/* Check free space information in directories. */
STATIC int
xchk_directory_blocks(
	struct xfs_scrub_context	*sc)
{
	struct xfs_bmbt_irec		got;
	struct xfs_da_args		args;
	struct xfs_ifork		*ifp;
	struct xfs_mount		*mp = sc->mp;
	xfs_fileoff_t			leaf_lblk;
	xfs_fileoff_t			free_lblk;
	xfs_fileoff_t			lblk;
	struct xfs_iext_cursor		icur;
	xfs_dablk_t			dabno;
	bool				found;
	int				is_block = 0;
	int				error;

	/* Ignore local format directories. */
	if (sc->ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS &&
	    sc->ip->i_d.di_format != XFS_DINODE_FMT_BTREE)
		return 0;

	ifp = XFS_IFORK_PTR(sc->ip, XFS_DATA_FORK);
	lblk = XFS_B_TO_FSB(mp, XFS_DIR2_DATA_OFFSET);
	leaf_lblk = XFS_B_TO_FSB(mp, XFS_DIR2_LEAF_OFFSET);
	free_lblk = XFS_B_TO_FSB(mp, XFS_DIR2_FREE_OFFSET);

	/* Is this a block dir? */
	args.dp = sc->ip;
	args.geo = mp->m_dir_geo;
	args.trans = sc->tp;
	error = xfs_dir2_isblock(&args, &is_block);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, lblk, &error))
		goto out;

	/* Iterate all the data extents in the directory... */
	found = xfs_iext_lookup_extent(sc->ip, ifp, lblk, &icur, &got);
	while (found && !(sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)) {
		/* Block directories only have a single block at offset 0. */
		if (is_block &&
		    (got.br_startoff > 0 ||
		     got.br_blockcount != args.geo->fsbcount)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK,
					got.br_startoff);
			break;
		}

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
		error = xchk_directory_leaf1_bestfree(sc, &args,
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
	struct xfs_scrub_context	*sc)
{
	struct xchk_dir_ctx		sdc = {
		.dir_iter.actor = xchk_dir_actor,
		.dir_iter.pos = 0,
		.sc = sc,
	};
	size_t				bufsize;
	loff_t				oldpos;
	int				error = 0;

	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -ENOENT;

	/* Plausible size? */
	if (sc->ip->i_d.di_size < xfs_dir2_sf_hdr_size(0)) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		goto out;
	}

	/* Check directory tree structure */
	error = xchk_da_btree(sc, XFS_DATA_FORK, xchk_dir_rec, NULL);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return error;

	/* Check the freespace. */
	error = xchk_directory_blocks(sc);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return error;

	/*
	 * Check that every dirent we see can also be looked up by hash.
	 * Userspace usually asks for a 32k buffer, so we will too.
	 */
	bufsize = (size_t)min_t(loff_t, XFS_READDIR_BUFSIZE,
			sc->ip->i_d.di_size);

	/*
	 * Look up every name in this directory by hash.
	 *
	 * Use the xfs_readdir function to call xchk_dir_actor on
	 * every directory entry in this directory.  In _actor, we check
	 * the name, inode number, and ftype (if applicable) of the
	 * entry.  xfs_readdir uses the VFS filldir functions to provide
	 * iteration context.
	 *
	 * The VFS grabs a read or write lock via i_rwsem before it reads
	 * or writes to a directory.  If we've gotten this far we've
	 * already obtained IOLOCK_EXCL, which (since 4.10) is the same as
	 * getting a write lock on i_rwsem.  Therefore, it is safe for us
	 * to drop the ILOCK here in order to reuse the _readdir and
	 * _dir_lookup routines, which do their own ILOCK locking.
	 */
	oldpos = 0;
	sc->ilock_flags &= ~XFS_ILOCK_EXCL;
	xfs_iunlock(sc->ip, XFS_ILOCK_EXCL);
	while (true) {
		error = xfs_readdir(sc->tp, sc->ip, &sdc.dir_iter, bufsize);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0,
				&error))
			goto out;
		if (oldpos == sdc.dir_iter.pos)
			break;
		oldpos = sdc.dir_iter.pos;
	}

out:
	return error;
}
