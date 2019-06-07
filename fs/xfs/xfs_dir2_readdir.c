// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_bmap.h"
#include "xfs_trans.h"

/*
 * Directory file type support functions
 */
static unsigned char xfs_dir3_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK,
	DT_FIFO, DT_SOCK, DT_LNK, DT_WHT,
};

unsigned char
xfs_dir3_get_dtype(
	struct xfs_mount	*mp,
	uint8_t			filetype)
{
	if (!xfs_sb_version_hasftype(&mp->m_sb))
		return DT_UNKNOWN;

	if (filetype >= XFS_DIR3_FT_MAX)
		return DT_UNKNOWN;

	return xfs_dir3_filetype_table[filetype];
}

STATIC int
xfs_dir2_sf_getdents(
	struct xfs_da_args	*args,
	struct dir_context	*ctx)
{
	int			i;		/* shortform entry number */
	struct xfs_inode	*dp = args->dp;	/* incore directory inode */
	xfs_dir2_dataptr_t	off;		/* current entry's offset */
	xfs_dir2_sf_entry_t	*sfep;		/* shortform directory entry */
	xfs_dir2_sf_hdr_t	*sfp;		/* shortform structure */
	xfs_dir2_dataptr_t	dot_offset;
	xfs_dir2_dataptr_t	dotdot_offset;
	xfs_ino_t		ino;
	struct xfs_da_geometry	*geo = args->geo;

	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);

	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;

	/*
	 * If the block number in the offset is out of range, we're done.
	 */
	if (xfs_dir2_dataptr_to_db(geo, ctx->pos) > geo->datablk)
		return 0;

	/*
	 * Precalculate offsets for . and .. as we will always need them.
	 *
	 * XXX(hch): the second argument is sometimes 0 and sometimes
	 * geo->datablk
	 */
	dot_offset = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
						dp->d_ops->data_dot_offset);
	dotdot_offset = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
						dp->d_ops->data_dotdot_offset);

	/*
	 * Put . entry unless we're starting past it.
	 */
	if (ctx->pos <= dot_offset) {
		ctx->pos = dot_offset & 0x7fffffff;
		if (!dir_emit(ctx, ".", 1, dp->i_ino, DT_DIR))
			return 0;
	}

	/*
	 * Put .. entry unless we're starting past it.
	 */
	if (ctx->pos <= dotdot_offset) {
		ino = dp->d_ops->sf_get_parent_ino(sfp);
		ctx->pos = dotdot_offset & 0x7fffffff;
		if (!dir_emit(ctx, "..", 2, ino, DT_DIR))
			return 0;
	}

	/*
	 * Loop while there are more entries and put'ing works.
	 */
	sfep = xfs_dir2_sf_firstentry(sfp);
	for (i = 0; i < sfp->count; i++) {
		uint8_t filetype;

		off = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
				xfs_dir2_sf_get_offset(sfep));

		if (ctx->pos > off) {
			sfep = dp->d_ops->sf_nextentry(sfp, sfep);
			continue;
		}

		ino = dp->d_ops->sf_get_ino(sfp, sfep);
		filetype = dp->d_ops->sf_get_ftype(sfep);
		ctx->pos = off & 0x7fffffff;
		if (!dir_emit(ctx, (char *)sfep->name, sfep->namelen, ino,
			    xfs_dir3_get_dtype(dp->i_mount, filetype)))
			return 0;
		sfep = dp->d_ops->sf_nextentry(sfp, sfep);
	}

	ctx->pos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk + 1, 0) &
								0x7fffffff;
	return 0;
}

/*
 * Readdir for block directories.
 */
STATIC int
xfs_dir2_block_getdents(
	struct xfs_da_args	*args,
	struct dir_context	*ctx)
{
	struct xfs_inode	*dp = args->dp;	/* incore directory inode */
	xfs_dir2_data_hdr_t	*hdr;		/* block header */
	struct xfs_buf		*bp;		/* buffer for block */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_dir2_data_unused_t	*dup;		/* block unused entry */
	char			*endptr;	/* end of the data entries */
	int			error;		/* error return value */
	char			*ptr;		/* current data entry */
	int			wantoff;	/* starting block offset */
	xfs_off_t		cook;
	struct xfs_da_geometry	*geo = args->geo;
	int			lock_mode;

	/*
	 * If the block number in the offset is out of range, we're done.
	 */
	if (xfs_dir2_dataptr_to_db(geo, ctx->pos) > geo->datablk)
		return 0;

	lock_mode = xfs_ilock_data_map_shared(dp);
	error = xfs_dir3_block_read(args->trans, dp, &bp);
	xfs_iunlock(dp, lock_mode);
	if (error)
		return error;

	/*
	 * Extract the byte offset we start at from the seek pointer.
	 * We'll skip entries before this.
	 */
	wantoff = xfs_dir2_dataptr_to_off(geo, ctx->pos);
	hdr = bp->b_addr;
	xfs_dir3_data_check(dp, bp);
	/*
	 * Set up values for the loop.
	 */
	ptr = (char *)dp->d_ops->data_entry_p(hdr);
	endptr = xfs_dir3_data_endp(geo, hdr);

	/*
	 * Loop over the data portion of the block.
	 * Each object is a real entry (dep) or an unused one (dup).
	 */
	while (ptr < endptr) {
		uint8_t filetype;

		dup = (xfs_dir2_data_unused_t *)ptr;
		/*
		 * Unused, skip it.
		 */
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			ptr += be16_to_cpu(dup->length);
			continue;
		}

		dep = (xfs_dir2_data_entry_t *)ptr;

		/*
		 * Bump pointer for the next iteration.
		 */
		ptr += dp->d_ops->data_entsize(dep->namelen);
		/*
		 * The entry is before the desired starting point, skip it.
		 */
		if ((char *)dep - (char *)hdr < wantoff)
			continue;

		cook = xfs_dir2_db_off_to_dataptr(geo, geo->datablk,
					    (char *)dep - (char *)hdr);

		ctx->pos = cook & 0x7fffffff;
		filetype = dp->d_ops->data_get_ftype(dep);
		/*
		 * If it didn't fit, set the final offset to here & return.
		 */
		if (!dir_emit(ctx, (char *)dep->name, dep->namelen,
			    be64_to_cpu(dep->inumber),
			    xfs_dir3_get_dtype(dp->i_mount, filetype))) {
			xfs_trans_brelse(args->trans, bp);
			return 0;
		}
	}

	/*
	 * Reached the end of the block.
	 * Set the offset to a non-existent block 1 and return.
	 */
	ctx->pos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk + 1, 0) &
								0x7fffffff;
	xfs_trans_brelse(args->trans, bp);
	return 0;
}

/*
 * Read a directory block and initiate readahead for blocks beyond that.
 * We maintain a sliding readahead window of the remaining space in the
 * buffer rounded up to the nearest block.
 */
STATIC int
xfs_dir2_leaf_readbuf(
	struct xfs_da_args	*args,
	size_t			bufsize,
	xfs_dir2_off_t		*cur_off,
	xfs_dablk_t		*ra_blk,
	struct xfs_buf		**bpp)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_buf		*bp = NULL;
	struct xfs_da_geometry	*geo = args->geo;
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(dp, XFS_DATA_FORK);
	struct xfs_bmbt_irec	map;
	struct blk_plug		plug;
	xfs_dir2_off_t		new_off;
	xfs_dablk_t		next_ra;
	xfs_dablk_t		map_off;
	xfs_dablk_t		last_da;
	struct xfs_iext_cursor	icur;
	int			ra_want;
	int			error = 0;

	if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(args->trans, dp, XFS_DATA_FORK);
		if (error)
			goto out;
	}

	/*
	 * Look for mapped directory blocks at or above the current offset.
	 * Truncate down to the nearest directory block to start the scanning
	 * operation.
	 */
	last_da = xfs_dir2_byte_to_da(geo, XFS_DIR2_LEAF_OFFSET);
	map_off = xfs_dir2_db_to_da(geo, xfs_dir2_byte_to_db(geo, *cur_off));
	if (!xfs_iext_lookup_extent(dp, ifp, map_off, &icur, &map))
		goto out;
	if (map.br_startoff >= last_da)
		goto out;
	xfs_trim_extent(&map, map_off, last_da - map_off);

	/* Read the directory block of that first mapping. */
	new_off = xfs_dir2_da_to_byte(geo, map.br_startoff);
	if (new_off > *cur_off)
		*cur_off = new_off;
	error = xfs_dir3_data_read(args->trans, dp, map.br_startoff, -1, &bp);
	if (error)
		goto out;

	/*
	 * Start readahead for the next bufsize's worth of dir data blocks.
	 * We may have already issued readahead for some of that range;
	 * ra_blk tracks the last block we tried to read(ahead).
	 */
	ra_want = howmany(bufsize + geo->blksize, (1 << geo->fsblog));
	if (*ra_blk >= last_da)
		goto out;
	else if (*ra_blk == 0)
		*ra_blk = map.br_startoff;
	next_ra = map.br_startoff + geo->fsbcount;
	if (next_ra >= last_da)
		goto out_no_ra;
	if (map.br_blockcount < geo->fsbcount &&
	    !xfs_iext_next_extent(ifp, &icur, &map))
		goto out_no_ra;
	if (map.br_startoff >= last_da)
		goto out_no_ra;
	xfs_trim_extent(&map, next_ra, last_da - next_ra);

	/* Start ra for each dir (not fs) block that has a mapping. */
	blk_start_plug(&plug);
	while (ra_want > 0) {
		next_ra = roundup((xfs_dablk_t)map.br_startoff, geo->fsbcount);
		while (ra_want > 0 &&
		       next_ra < map.br_startoff + map.br_blockcount) {
			if (next_ra >= last_da) {
				*ra_blk = last_da;
				break;
			}
			if (next_ra > *ra_blk) {
				xfs_dir3_data_readahead(dp, next_ra, -2);
				*ra_blk = next_ra;
			}
			ra_want -= geo->fsbcount;
			next_ra += geo->fsbcount;
		}
		if (!xfs_iext_next_extent(ifp, &icur, &map)) {
			*ra_blk = last_da;
			break;
		}
	}
	blk_finish_plug(&plug);

out:
	*bpp = bp;
	return error;
out_no_ra:
	*ra_blk = last_da;
	goto out;
}

/*
 * Getdents (readdir) for leaf and node directories.
 * This reads the data blocks only, so is the same for both forms.
 */
STATIC int
xfs_dir2_leaf_getdents(
	struct xfs_da_args	*args,
	struct dir_context	*ctx,
	size_t			bufsize)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_buf		*bp = NULL;	/* data block buffer */
	xfs_dir2_data_hdr_t	*hdr;		/* data block header */
	xfs_dir2_data_entry_t	*dep;		/* data entry */
	xfs_dir2_data_unused_t	*dup;		/* unused entry */
	char			*ptr = NULL;	/* pointer to current data */
	struct xfs_da_geometry	*geo = args->geo;
	xfs_dablk_t		rablk = 0;	/* current readahead block */
	xfs_dir2_off_t		curoff;		/* current overall offset */
	int			length;		/* temporary length value */
	int			byteoff;	/* offset in current block */
	int			lock_mode;
	int			error = 0;	/* error return value */

	/*
	 * If the offset is at or past the largest allowed value,
	 * give up right away.
	 */
	if (ctx->pos >= XFS_DIR2_MAX_DATAPTR)
		return 0;

	/*
	 * Inside the loop we keep the main offset value as a byte offset
	 * in the directory file.
	 */
	curoff = xfs_dir2_dataptr_to_byte(ctx->pos);

	/*
	 * Loop over directory entries until we reach the end offset.
	 * Get more blocks and readahead as necessary.
	 */
	while (curoff < XFS_DIR2_LEAF_OFFSET) {
		uint8_t filetype;

		/*
		 * If we have no buffer, or we're off the end of the
		 * current buffer, need to get another one.
		 */
		if (!bp || ptr >= (char *)bp->b_addr + geo->blksize) {
			if (bp) {
				xfs_trans_brelse(args->trans, bp);
				bp = NULL;
			}

			lock_mode = xfs_ilock_data_map_shared(dp);
			error = xfs_dir2_leaf_readbuf(args, bufsize, &curoff,
					&rablk, &bp);
			xfs_iunlock(dp, lock_mode);
			if (error || !bp)
				break;

			hdr = bp->b_addr;
			xfs_dir3_data_check(dp, bp);
			/*
			 * Find our position in the block.
			 */
			ptr = (char *)dp->d_ops->data_entry_p(hdr);
			byteoff = xfs_dir2_byte_to_off(geo, curoff);
			/*
			 * Skip past the header.
			 */
			if (byteoff == 0)
				curoff += dp->d_ops->data_entry_offset;
			/*
			 * Skip past entries until we reach our offset.
			 */
			else {
				while ((char *)ptr - (char *)hdr < byteoff) {
					dup = (xfs_dir2_data_unused_t *)ptr;

					if (be16_to_cpu(dup->freetag)
						  == XFS_DIR2_DATA_FREE_TAG) {

						length = be16_to_cpu(dup->length);
						ptr += length;
						continue;
					}
					dep = (xfs_dir2_data_entry_t *)ptr;
					length =
					   dp->d_ops->data_entsize(dep->namelen);
					ptr += length;
				}
				/*
				 * Now set our real offset.
				 */
				curoff =
					xfs_dir2_db_off_to_byte(geo,
					    xfs_dir2_byte_to_db(geo, curoff),
					    (char *)ptr - (char *)hdr);
				if (ptr >= (char *)hdr + geo->blksize) {
					continue;
				}
			}
		}
		/*
		 * We have a pointer to an entry.
		 * Is it a live one?
		 */
		dup = (xfs_dir2_data_unused_t *)ptr;
		/*
		 * No, it's unused, skip over it.
		 */
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			length = be16_to_cpu(dup->length);
			ptr += length;
			curoff += length;
			continue;
		}

		dep = (xfs_dir2_data_entry_t *)ptr;
		length = dp->d_ops->data_entsize(dep->namelen);
		filetype = dp->d_ops->data_get_ftype(dep);

		ctx->pos = xfs_dir2_byte_to_dataptr(curoff) & 0x7fffffff;
		if (!dir_emit(ctx, (char *)dep->name, dep->namelen,
			    be64_to_cpu(dep->inumber),
			    xfs_dir3_get_dtype(dp->i_mount, filetype)))
			break;

		/*
		 * Advance to next entry in the block.
		 */
		ptr += length;
		curoff += length;
		/* bufsize may have just been a guess; don't go negative */
		bufsize = bufsize > length ? bufsize - length : 0;
	}

	/*
	 * All done.  Set output offset value to current offset.
	 */
	if (curoff > xfs_dir2_dataptr_to_byte(XFS_DIR2_MAX_DATAPTR))
		ctx->pos = XFS_DIR2_MAX_DATAPTR & 0x7fffffff;
	else
		ctx->pos = xfs_dir2_byte_to_dataptr(curoff) & 0x7fffffff;
	if (bp)
		xfs_trans_brelse(args->trans, bp);
	return error;
}

/*
 * Read a directory.
 *
 * If supplied, the transaction collects locked dir buffers to avoid
 * nested buffer deadlocks.  This function does not dirty the
 * transaction.  The caller should ensure that the inode is locked
 * before calling this function.
 */
int
xfs_readdir(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	struct dir_context	*ctx,
	size_t			bufsize)
{
	struct xfs_da_args	args = { NULL };
	int			rval;
	int			v;

	trace_xfs_readdir(dp);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return -EIO;

	ASSERT(S_ISDIR(VFS_I(dp)->i_mode));
	XFS_STATS_INC(dp->i_mount, xs_dir_getdents);

	args.dp = dp;
	args.geo = dp->i_mount->m_dir_geo;
	args.trans = tp;

	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_getdents(&args, ctx);
	else if ((rval = xfs_dir2_isblock(&args, &v)))
		;
	else if (v)
		rval = xfs_dir2_block_getdents(&args, ctx);
	else
		rval = xfs_dir2_leaf_getdents(&args, ctx, bufsize);

	return rval;
}
