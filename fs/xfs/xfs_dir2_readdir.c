/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
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
#include "xfs_dinode.h"

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
	__uint8_t		filetype)
{
	if (!xfs_sb_version_hasftype(&mp->m_sb))
		return DT_UNKNOWN;

	if (filetype >= XFS_DIR3_FT_MAX)
		return DT_UNKNOWN;

	return xfs_dir3_filetype_table[filetype];
}
/*
 * @mode, if set, indicates that the type field needs to be set up.
 * This uses the transformation from file mode to DT_* as defined in linux/fs.h
 * for file type specification. This will be propagated into the directory
 * structure if appropriate for the given operation and filesystem config.
 */
const unsigned char xfs_mode_to_ftype[S_IFMT >> S_SHIFT] = {
	[0]			= XFS_DIR3_FT_UNKNOWN,
	[S_IFREG >> S_SHIFT]    = XFS_DIR3_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]    = XFS_DIR3_FT_DIR,
	[S_IFCHR >> S_SHIFT]    = XFS_DIR3_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]    = XFS_DIR3_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]    = XFS_DIR3_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]   = XFS_DIR3_FT_SOCK,
	[S_IFLNK >> S_SHIFT]    = XFS_DIR3_FT_SYMLINK,
};

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
	/*
	 * Give up if the directory is way too short.
	 */
	if (dp->i_d.di_size < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}

	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);

	sfp = (xfs_dir2_sf_hdr_t *)dp->i_df.if_u1.if_data;

	ASSERT(dp->i_d.di_size >= xfs_dir2_sf_hdr_size(sfp->i8count));

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
		__uint8_t filetype;

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
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_dir2_data_unused_t	*dup;		/* block unused entry */
	char			*endptr;	/* end of the data entries */
	int			error;		/* error return value */
	char			*ptr;		/* current data entry */
	int			wantoff;	/* starting block offset */
	xfs_off_t		cook;
	struct xfs_da_geometry	*geo = args->geo;

	/*
	 * If the block number in the offset is out of range, we're done.
	 */
	if (xfs_dir2_dataptr_to_db(geo, ctx->pos) > geo->datablk)
		return 0;

	error = xfs_dir3_block_read(NULL, dp, &bp);
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
	btp = xfs_dir2_block_tail_p(geo, hdr);
	ptr = (char *)dp->d_ops->data_entry_p(hdr);
	endptr = (char *)xfs_dir2_block_leaf_p(btp);

	/*
	 * Loop over the data portion of the block.
	 * Each object is a real entry (dep) or an unused one (dup).
	 */
	while (ptr < endptr) {
		__uint8_t filetype;

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
			xfs_trans_brelse(NULL, bp);
			return 0;
		}
	}

	/*
	 * Reached the end of the block.
	 * Set the offset to a non-existent block 1 and return.
	 */
	ctx->pos = xfs_dir2_db_off_to_dataptr(geo, geo->datablk + 1, 0) &
								0x7fffffff;
	xfs_trans_brelse(NULL, bp);
	return 0;
}

struct xfs_dir2_leaf_map_info {
	xfs_extlen_t	map_blocks;	/* number of fsbs in map */
	xfs_dablk_t	map_off;	/* last mapped file offset */
	int		map_size;	/* total entries in *map */
	int		map_valid;	/* valid entries in *map */
	int		nmap;		/* mappings to ask xfs_bmapi */
	xfs_dir2_db_t	curdb;		/* db for current block */
	int		ra_current;	/* number of read-ahead blks */
	int		ra_index;	/* *map index for read-ahead */
	int		ra_offset;	/* map entry offset for ra */
	int		ra_want;	/* readahead count wanted */
	struct xfs_bmbt_irec map[];	/* map vector for blocks */
};

STATIC int
xfs_dir2_leaf_readbuf(
	struct xfs_da_args	*args,
	size_t			bufsize,
	struct xfs_dir2_leaf_map_info *mip,
	xfs_dir2_off_t		*curoff,
	struct xfs_buf		**bpp)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_buf		*bp = *bpp;
	struct xfs_bmbt_irec	*map = mip->map;
	struct blk_plug		plug;
	int			error = 0;
	int			length;
	int			i;
	int			j;
	struct xfs_da_geometry	*geo = args->geo;

	/*
	 * If we have a buffer, we need to release it and
	 * take it out of the mapping.
	 */

	if (bp) {
		xfs_trans_brelse(NULL, bp);
		bp = NULL;
		mip->map_blocks -= geo->fsbcount;
		/*
		 * Loop to get rid of the extents for the
		 * directory block.
		 */
		for (i = geo->fsbcount; i > 0; ) {
			j = min_t(int, map->br_blockcount, i);
			map->br_blockcount -= j;
			map->br_startblock += j;
			map->br_startoff += j;
			/*
			 * If mapping is done, pitch it from
			 * the table.
			 */
			if (!map->br_blockcount && --mip->map_valid)
				memmove(&map[0], &map[1],
					sizeof(map[0]) * mip->map_valid);
			i -= j;
		}
	}

	/*
	 * Recalculate the readahead blocks wanted.
	 */
	mip->ra_want = howmany(bufsize + geo->blksize, (1 << geo->fsblog)) - 1;
	ASSERT(mip->ra_want >= 0);

	/*
	 * If we don't have as many as we want, and we haven't
	 * run out of data blocks, get some more mappings.
	 */
	if (1 + mip->ra_want > mip->map_blocks &&
	    mip->map_off < xfs_dir2_byte_to_da(geo, XFS_DIR2_LEAF_OFFSET)) {
		/*
		 * Get more bmaps, fill in after the ones
		 * we already have in the table.
		 */
		mip->nmap = mip->map_size - mip->map_valid;
		error = xfs_bmapi_read(dp, mip->map_off,
				xfs_dir2_byte_to_da(geo, XFS_DIR2_LEAF_OFFSET) -
								mip->map_off,
				&map[mip->map_valid], &mip->nmap, 0);

		/*
		 * Don't know if we should ignore this or try to return an
		 * error.  The trouble with returning errors is that readdir
		 * will just stop without actually passing the error through.
		 */
		if (error)
			goto out;	/* XXX */

		/*
		 * If we got all the mappings we asked for, set the final map
		 * offset based on the last bmap value received.  Otherwise,
		 * we've reached the end.
		 */
		if (mip->nmap == mip->map_size - mip->map_valid) {
			i = mip->map_valid + mip->nmap - 1;
			mip->map_off = map[i].br_startoff + map[i].br_blockcount;
		} else
			mip->map_off = xfs_dir2_byte_to_da(geo,
							XFS_DIR2_LEAF_OFFSET);

		/*
		 * Look for holes in the mapping, and eliminate them.  Count up
		 * the valid blocks.
		 */
		for (i = mip->map_valid; i < mip->map_valid + mip->nmap; ) {
			if (map[i].br_startblock == HOLESTARTBLOCK) {
				mip->nmap--;
				length = mip->map_valid + mip->nmap - i;
				if (length)
					memmove(&map[i], &map[i + 1],
						sizeof(map[i]) * length);
			} else {
				mip->map_blocks += map[i].br_blockcount;
				i++;
			}
		}
		mip->map_valid += mip->nmap;
	}

	/*
	 * No valid mappings, so no more data blocks.
	 */
	if (!mip->map_valid) {
		*curoff = xfs_dir2_da_to_byte(geo, mip->map_off);
		goto out;
	}

	/*
	 * Read the directory block starting at the first mapping.
	 */
	mip->curdb = xfs_dir2_da_to_db(geo, map->br_startoff);
	error = xfs_dir3_data_read(NULL, dp, map->br_startoff,
			map->br_blockcount >= geo->fsbcount ?
			    XFS_FSB_TO_DADDR(dp->i_mount, map->br_startblock) :
			    -1, &bp);
	/*
	 * Should just skip over the data block instead of giving up.
	 */
	if (error)
		goto out;	/* XXX */

	/*
	 * Adjust the current amount of read-ahead: we just read a block that
	 * was previously ra.
	 */
	if (mip->ra_current)
		mip->ra_current -= geo->fsbcount;

	/*
	 * Do we need more readahead?
	 */
	blk_start_plug(&plug);
	for (mip->ra_index = mip->ra_offset = i = 0;
	     mip->ra_want > mip->ra_current && i < mip->map_blocks;
	     i += geo->fsbcount) {
		ASSERT(mip->ra_index < mip->map_valid);
		/*
		 * Read-ahead a contiguous directory block.
		 */
		if (i > mip->ra_current &&
		    map[mip->ra_index].br_blockcount >= geo->fsbcount) {
			xfs_dir3_data_readahead(dp,
				map[mip->ra_index].br_startoff + mip->ra_offset,
				XFS_FSB_TO_DADDR(dp->i_mount,
					map[mip->ra_index].br_startblock +
							mip->ra_offset));
			mip->ra_current = i;
		}

		/*
		 * Read-ahead a non-contiguous directory block.  This doesn't
		 * use our mapping, but this is a very rare case.
		 */
		else if (i > mip->ra_current) {
			xfs_dir3_data_readahead(dp,
					map[mip->ra_index].br_startoff +
							mip->ra_offset, -1);
			mip->ra_current = i;
		}

		/*
		 * Advance offset through the mapping table.
		 */
		for (j = 0; j < geo->fsbcount; j += length ) {
			/*
			 * The rest of this extent but not more than a dir
			 * block.
			 */
			length = min_t(int, geo->fsbcount,
					map[mip->ra_index].br_blockcount -
							mip->ra_offset);
			mip->ra_offset += length;

			/*
			 * Advance to the next mapping if this one is used up.
			 */
			if (mip->ra_offset == map[mip->ra_index].br_blockcount) {
				mip->ra_offset = 0;
				mip->ra_index++;
			}
		}
	}
	blk_finish_plug(&plug);

out:
	*bpp = bp;
	return error;
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
	int			error = 0;	/* error return value */
	int			length;		/* temporary length value */
	int			byteoff;	/* offset in current block */
	xfs_dir2_off_t		curoff;		/* current overall offset */
	xfs_dir2_off_t		newoff;		/* new curoff after new blk */
	char			*ptr = NULL;	/* pointer to current data */
	struct xfs_dir2_leaf_map_info *map_info;
	struct xfs_da_geometry	*geo = args->geo;

	/*
	 * If the offset is at or past the largest allowed value,
	 * give up right away.
	 */
	if (ctx->pos >= XFS_DIR2_MAX_DATAPTR)
		return 0;

	/*
	 * Set up to bmap a number of blocks based on the caller's
	 * buffer size, the directory block size, and the filesystem
	 * block size.
	 */
	length = howmany(bufsize + geo->blksize, (1 << geo->fsblog));
	map_info = kmem_zalloc(offsetof(struct xfs_dir2_leaf_map_info, map) +
				(length * sizeof(struct xfs_bmbt_irec)),
			       KM_SLEEP | KM_NOFS);
	map_info->map_size = length;

	/*
	 * Inside the loop we keep the main offset value as a byte offset
	 * in the directory file.
	 */
	curoff = xfs_dir2_dataptr_to_byte(ctx->pos);

	/*
	 * Force this conversion through db so we truncate the offset
	 * down to get the start of the data block.
	 */
	map_info->map_off = xfs_dir2_db_to_da(geo,
					      xfs_dir2_byte_to_db(geo, curoff));

	/*
	 * Loop over directory entries until we reach the end offset.
	 * Get more blocks and readahead as necessary.
	 */
	while (curoff < XFS_DIR2_LEAF_OFFSET) {
		__uint8_t filetype;

		/*
		 * If we have no buffer, or we're off the end of the
		 * current buffer, need to get another one.
		 */
		if (!bp || ptr >= (char *)bp->b_addr + geo->blksize) {

			error = xfs_dir2_leaf_readbuf(args, bufsize, map_info,
						      &curoff, &bp);
			if (error || !map_info->map_valid)
				break;

			/*
			 * Having done a read, we need to set a new offset.
			 */
			newoff = xfs_dir2_db_off_to_byte(geo,
							 map_info->curdb, 0);
			/*
			 * Start of the current block.
			 */
			if (curoff < newoff)
				curoff = newoff;
			/*
			 * Make sure we're in the right block.
			 */
			else if (curoff > newoff)
				ASSERT(xfs_dir2_byte_to_db(geo, curoff) ==
				       map_info->curdb);
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
	kmem_free(map_info);
	if (bp)
		xfs_trans_brelse(NULL, bp);
	return error;
}

/*
 * Read a directory.
 */
int
xfs_readdir(
	struct xfs_inode	*dp,
	struct dir_context	*ctx,
	size_t			bufsize)
{
	struct xfs_da_args	args = { NULL };
	int			rval;
	int			v;
	uint			lock_mode;

	trace_xfs_readdir(dp);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return XFS_ERROR(EIO);

	ASSERT(S_ISDIR(dp->i_d.di_mode));
	XFS_STATS_INC(xs_dir_getdents);

	args.dp = dp;
	args.geo = dp->i_mount->m_dir_geo;

	lock_mode = xfs_ilock_data_map_shared(dp);
	if (dp->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		rval = xfs_dir2_sf_getdents(&args, ctx);
	else if ((rval = xfs_dir2_isblock(&args, &v)))
		;
	else if (v)
		rval = xfs_dir2_block_getdents(&args, ctx);
	else
		rval = xfs_dir2_leaf_getdents(&args, ctx, bufsize);
	xfs_iunlock(dp, lock_mode);

	return rval;
}
