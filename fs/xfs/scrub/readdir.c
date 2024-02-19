// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_trace.h"
#include "xfs_bmap.h"
#include "xfs_trans.h"
#include "xfs_error.h"
#include "scrub/scrub.h"
#include "scrub/readdir.h"

/* Call a function for every entry in a shortform directory. */
STATIC int
xchk_dir_walk_sf(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xchk_dirent_fn		dirent_fn,
	void			*priv)
{
	struct xfs_name		name = {
		.name		= ".",
		.len		= 1,
		.type		= XFS_DIR3_FT_DIR,
	};
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;
	struct xfs_dir2_sf_entry *sfep;
	struct xfs_dir2_sf_hdr	*sfp = dp->i_df.if_data;
	xfs_ino_t		ino;
	xfs_dir2_dataptr_t	dapos;
	unsigned int		i;
	int			error;

	ASSERT(dp->i_df.if_bytes == dp->i_disk_size);
	ASSERT(sfp != NULL);

	/* dot entry */
	dapos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
			geo->data_entry_offset);

	error = dirent_fn(sc, dp, dapos, &name, dp->i_ino, priv);
	if (error)
		return error;

	/* dotdot entry */
	dapos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
			geo->data_entry_offset +
			xfs_dir2_data_entsize(mp, sizeof(".") - 1));
	ino = xfs_dir2_sf_get_parent_ino(sfp);
	name.name = "..";
	name.len = 2;

	error = dirent_fn(sc, dp, dapos, &name, ino, priv);
	if (error)
		return error;

	/* iterate everything else */
	sfep = xfs_dir2_sf_firstentry(sfp);
	for (i = 0; i < sfp->count; i++) {
		dapos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
				xfs_dir2_sf_get_offset(sfep));
		ino = xfs_dir2_sf_get_ino(mp, sfp, sfep);
		name.name = sfep->name;
		name.len = sfep->namelen;
		name.type = xfs_dir2_sf_get_ftype(mp, sfep);

		error = dirent_fn(sc, dp, dapos, &name, ino, priv);
		if (error)
			return error;

		sfep = xfs_dir2_sf_nextentry(mp, sfp, sfep);
	}

	return 0;
}

/* Call a function for every entry in a block directory. */
STATIC int
xchk_dir_walk_block(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xchk_dirent_fn		dirent_fn,
	void			*priv)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;
	struct xfs_buf		*bp;
	unsigned int		off, next_off, end;
	int			error;

	error = xfs_dir3_block_read(sc->tp, dp, &bp);
	if (error)
		return error;

	/* Walk each directory entry. */
	end = xfs_dir3_data_end_offset(geo, bp->b_addr);
	for (off = geo->data_entry_offset; off < end; off = next_off) {
		struct xfs_name			name = { };
		struct xfs_dir2_data_unused	*dup = bp->b_addr + off;
		struct xfs_dir2_data_entry	*dep = bp->b_addr + off;
		xfs_ino_t			ino;
		xfs_dir2_dataptr_t		dapos;

		/* Skip an empty entry. */
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			next_off = off + be16_to_cpu(dup->length);
			continue;
		}

		/* Otherwise, find the next entry and report it. */
		next_off = off + xfs_dir2_data_entsize(mp, dep->namelen);
		if (next_off > end)
			break;

		dapos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk, off);
		ino = be64_to_cpu(dep->inumber);
		name.name = dep->name;
		name.len = dep->namelen;
		name.type = xfs_dir2_data_get_ftype(mp, dep);

		error = dirent_fn(sc, dp, dapos, &name, ino, priv);
		if (error)
			break;
	}

	xfs_trans_brelse(sc->tp, bp);
	return error;
}

/* Read a leaf-format directory buffer. */
STATIC int
xchk_read_leaf_dir_buf(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	struct xfs_da_geometry	*geo,
	xfs_dir2_off_t		*curoff,
	struct xfs_buf		**bpp)
{
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	map;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(dp, XFS_DATA_FORK);
	xfs_dablk_t		last_da;
	xfs_dablk_t		map_off;
	xfs_dir2_off_t		new_off;

	*bpp = NULL;

	/*
	 * Look for mapped directory blocks at or above the current offset.
	 * Truncate down to the nearest directory block to start the scanning
	 * operation.
	 */
	last_da = xfs_dir2_byte_to_da(geo, XFS_DIR2_LEAF_OFFSET);
	map_off = xfs_dir2_db_to_da(geo, xfs_dir2_byte_to_db(geo, *curoff));

	if (!xfs_iext_lookup_extent(dp, ifp, map_off, &icur, &map))
		return 0;
	if (map.br_startoff >= last_da)
		return 0;
	xfs_trim_extent(&map, map_off, last_da - map_off);

	/* Read the directory block of that first mapping. */
	new_off = xfs_dir2_da_to_byte(geo, map.br_startoff);
	if (new_off > *curoff)
		*curoff = new_off;

	return xfs_dir3_data_read(tp, dp, map.br_startoff, 0, bpp);
}

/* Call a function for every entry in a leaf directory. */
STATIC int
xchk_dir_walk_leaf(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xchk_dirent_fn		dirent_fn,
	void			*priv)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;
	struct xfs_buf		*bp = NULL;
	xfs_dir2_off_t		curoff = 0;
	unsigned int		offset = 0;
	int			error;

	/* Iterate every directory offset in this directory. */
	while (curoff < XFS_DIR2_LEAF_OFFSET) {
		struct xfs_name			name = { };
		struct xfs_dir2_data_unused	*dup;
		struct xfs_dir2_data_entry	*dep;
		xfs_ino_t			ino;
		unsigned int			length;
		xfs_dir2_dataptr_t		dapos;

		/*
		 * If we have no buffer, or we're off the end of the
		 * current buffer, need to get another one.
		 */
		if (!bp || offset >= geo->blksize) {
			if (bp) {
				xfs_trans_brelse(sc->tp, bp);
				bp = NULL;
			}

			error = xchk_read_leaf_dir_buf(sc->tp, dp, geo, &curoff,
					&bp);
			if (error || !bp)
				break;

			/*
			 * Find our position in the block.
			 */
			offset = geo->data_entry_offset;
			curoff += geo->data_entry_offset;
		}

		/* Skip an empty entry. */
		dup = bp->b_addr + offset;
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			length = be16_to_cpu(dup->length);
			offset += length;
			curoff += length;
			continue;
		}

		/* Otherwise, find the next entry and report it. */
		dep = bp->b_addr + offset;
		length = xfs_dir2_data_entsize(mp, dep->namelen);

		dapos = xfs_dir2_byte_to_dataptr(curoff) & 0x7fffffff;
		ino = be64_to_cpu(dep->inumber);
		name.name = dep->name;
		name.len = dep->namelen;
		name.type = xfs_dir2_data_get_ftype(mp, dep);

		error = dirent_fn(sc, dp, dapos, &name, ino, priv);
		if (error)
			break;

		/* Advance to the next entry. */
		offset += length;
		curoff += length;
	}

	if (bp)
		xfs_trans_brelse(sc->tp, bp);
	return error;
}

/*
 * Call a function for every entry in a directory.
 *
 * Callers must hold the ILOCK.  File types are XFS_DIR3_FT_*.
 */
int
xchk_dir_walk(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xchk_dirent_fn		dirent_fn,
	void			*priv)
{
	struct xfs_da_args	args = {
		.dp		= dp,
		.geo		= dp->i_mount->m_dir_geo,
		.trans		= sc->tp,
	};
	bool			isblock;
	int			error;

	if (xfs_is_shutdown(dp->i_mount))
		return -EIO;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	xfs_assert_ilocked(dp, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	if (dp->i_df.if_format == XFS_DINODE_FMT_LOCAL)
		return xchk_dir_walk_sf(sc, dp, dirent_fn, priv);

	/* dir2 functions require that the data fork is loaded */
	error = xfs_iread_extents(sc->tp, dp, XFS_DATA_FORK);
	if (error)
		return error;

	error = xfs_dir2_isblock(&args, &isblock);
	if (error)
		return error;

	if (isblock)
		return xchk_dir_walk_block(sc, dp, dirent_fn, priv);

	return xchk_dir_walk_leaf(sc, dp, dirent_fn, priv);
}

/*
 * Look up the inode number for an exact name in a directory.
 *
 * Callers must hold the ILOCK.  File types are XFS_DIR3_FT_*.  Names are not
 * checked for correctness.
 */
int
xchk_dir_lookup(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	const struct xfs_name	*name,
	xfs_ino_t		*ino)
{
	struct xfs_da_args	args = {
		.dp		= dp,
		.geo		= dp->i_mount->m_dir_geo,
		.trans		= sc->tp,
		.name		= name->name,
		.namelen	= name->len,
		.filetype	= name->type,
		.hashval	= xfs_dir2_hashname(dp->i_mount, name),
		.whichfork	= XFS_DATA_FORK,
		.op_flags	= XFS_DA_OP_OKNOENT,
	};
	bool			isblock, isleaf;
	int			error;

	if (xfs_is_shutdown(dp->i_mount))
		return -EIO;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	xfs_assert_ilocked(dp, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	if (dp->i_df.if_format == XFS_DINODE_FMT_LOCAL) {
		error = xfs_dir2_sf_lookup(&args);
		goto out_check_rval;
	}

	/* dir2 functions require that the data fork is loaded */
	error = xfs_iread_extents(sc->tp, dp, XFS_DATA_FORK);
	if (error)
		return error;

	error = xfs_dir2_isblock(&args, &isblock);
	if (error)
		return error;

	if (isblock) {
		error = xfs_dir2_block_lookup(&args);
		goto out_check_rval;
	}

	error = xfs_dir2_isleaf(&args, &isleaf);
	if (error)
		return error;

	if (isleaf) {
		error = xfs_dir2_leaf_lookup(&args);
		goto out_check_rval;
	}

	error = xfs_dir2_node_lookup(&args);

out_check_rval:
	if (error == -EEXIST)
		error = 0;
	if (!error)
		*ino = args.inumber;
	return error;
}
