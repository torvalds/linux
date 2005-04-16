/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * xfs_dir2_block.c
 * XFS V2 directory implementation, single-block form.
 * See xfs_dir2_block.h for the format.
 */

#include "xfs.h"

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_dir_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_trace.h"
#include "xfs_error.h"

/*
 * Local function prototypes.
 */
static void xfs_dir2_block_log_leaf(xfs_trans_t *tp, xfs_dabuf_t *bp, int first,
				    int last);
static void xfs_dir2_block_log_tail(xfs_trans_t *tp, xfs_dabuf_t *bp);
static int xfs_dir2_block_lookup_int(xfs_da_args_t *args, xfs_dabuf_t **bpp,
				     int *entno);
static int xfs_dir2_block_sort(const void *a, const void *b);

/*
 * Add an entry to a block directory.
 */
int						/* error */
xfs_dir2_block_addname(
	xfs_da_args_t		*args)		/* directory op arguments */
{
	xfs_dir2_data_free_t	*bf;		/* bestfree table in block */
	xfs_dir2_block_t	*block;		/* directory block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf entries */
	xfs_dabuf_t		*bp;		/* buffer for block */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	int			compact;	/* need to compact leaf ents */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_inode_t		*dp;		/* directory inode */
	xfs_dir2_data_unused_t	*dup;		/* block unused entry */
	int			error;		/* error return value */
	xfs_dir2_data_unused_t	*enddup=NULL;	/* unused at end of data */
	xfs_dahash_t		hash;		/* hash value of found entry */
	int			high;		/* high index for binary srch */
	int			highstale;	/* high stale index */
	int			lfloghigh=0;	/* last final leaf to log */
	int			lfloglow=0;	/* first final leaf to log */
	int			len;		/* length of the new entry */
	int			low;		/* low index for binary srch */
	int			lowstale;	/* low stale index */
	int			mid=0;		/* midpoint for binary srch */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needlog;	/* need to log header */
	int			needscan;	/* need to rescan freespace */
	xfs_dir2_data_off_t	*tagp;		/* pointer to tag value */
	xfs_trans_t		*tp;		/* transaction structure */

	xfs_dir2_trace_args("block_addname", args);
	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	/*
	 * Read the (one and only) directory block into dabuf bp.
	 */
	if ((error =
	    xfs_da_read_buf(tp, dp, mp->m_dirdatablk, -1, &bp, XFS_DATA_FORK))) {
		return error;
	}
	ASSERT(bp != NULL);
	block = bp->data;
	/*
	 * Check the magic number, corrupted if wrong.
	 */
	if (unlikely(INT_GET(block->hdr.magic, ARCH_CONVERT)
						!= XFS_DIR2_BLOCK_MAGIC)) {
		XFS_CORRUPTION_ERROR("xfs_dir2_block_addname",
				     XFS_ERRLEVEL_LOW, mp, block);
		xfs_da_brelse(tp, bp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	len = XFS_DIR2_DATA_ENTSIZE(args->namelen);
	/*
	 * Set up pointers to parts of the block.
	 */
	bf = block->hdr.bestfree;
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	/*
	 * No stale entries?  Need space for entry and new leaf.
	 */
	if (!btp->stale) {
		/*
		 * Tag just before the first leaf entry.
		 */
		tagp = (xfs_dir2_data_off_t *)blp - 1;
		/*
		 * Data object just before the first leaf entry.
		 */
		enddup = (xfs_dir2_data_unused_t *)((char *)block + INT_GET(*tagp, ARCH_CONVERT));
		/*
		 * If it's not free then can't do this add without cleaning up:
		 * the space before the first leaf entry needs to be free so it
		 * can be expanded to hold the pointer to the new entry.
		 */
		if (INT_GET(enddup->freetag, ARCH_CONVERT) != XFS_DIR2_DATA_FREE_TAG)
			dup = enddup = NULL;
		/*
		 * Check out the biggest freespace and see if it's the same one.
		 */
		else {
			dup = (xfs_dir2_data_unused_t *)
			      ((char *)block + INT_GET(bf[0].offset, ARCH_CONVERT));
			if (dup == enddup) {
				/*
				 * It is the biggest freespace, is it too small
				 * to hold the new leaf too?
				 */
				if (INT_GET(dup->length, ARCH_CONVERT) < len + (uint)sizeof(*blp)) {
					/*
					 * Yes, we use the second-largest
					 * entry instead if it works.
					 */
					if (INT_GET(bf[1].length, ARCH_CONVERT) >= len)
						dup = (xfs_dir2_data_unused_t *)
						      ((char *)block +
						       INT_GET(bf[1].offset, ARCH_CONVERT));
					else
						dup = NULL;
				}
			} else {
				/*
				 * Not the same free entry,
				 * just check its length.
				 */
				if (INT_GET(dup->length, ARCH_CONVERT) < len) {
					dup = NULL;
				}
			}
		}
		compact = 0;
	}
	/*
	 * If there are stale entries we'll use one for the leaf.
	 * Is the biggest entry enough to avoid compaction?
	 */
	else if (INT_GET(bf[0].length, ARCH_CONVERT) >= len) {
		dup = (xfs_dir2_data_unused_t *)
		      ((char *)block + INT_GET(bf[0].offset, ARCH_CONVERT));
		compact = 0;
	}
	/*
	 * Will need to compact to make this work.
	 */
	else {
		/*
		 * Tag just before the first leaf entry.
		 */
		tagp = (xfs_dir2_data_off_t *)blp - 1;
		/*
		 * Data object just before the first leaf entry.
		 */
		dup = (xfs_dir2_data_unused_t *)((char *)block + INT_GET(*tagp, ARCH_CONVERT));
		/*
		 * If it's not free then the data will go where the
		 * leaf data starts now, if it works at all.
		 */
		if (INT_GET(dup->freetag, ARCH_CONVERT) == XFS_DIR2_DATA_FREE_TAG) {
			if (INT_GET(dup->length, ARCH_CONVERT) + (INT_GET(btp->stale, ARCH_CONVERT) - 1) *
			    (uint)sizeof(*blp) < len)
				dup = NULL;
		} else if ((INT_GET(btp->stale, ARCH_CONVERT) - 1) * (uint)sizeof(*blp) < len)
			dup = NULL;
		else
			dup = (xfs_dir2_data_unused_t *)blp;
		compact = 1;
	}
	/*
	 * If this isn't a real add, we're done with the buffer.
	 */
	if (args->justcheck)
		xfs_da_brelse(tp, bp);
	/*
	 * If we don't have space for the new entry & leaf ...
	 */
	if (!dup) {
		/*
		 * Not trying to actually do anything, or don't have
		 * a space reservation: return no-space.
		 */
		if (args->justcheck || args->total == 0)
			return XFS_ERROR(ENOSPC);
		/*
		 * Convert to the next larger format.
		 * Then add the new entry in that format.
		 */
		error = xfs_dir2_block_to_leaf(args, bp);
		xfs_da_buf_done(bp);
		if (error)
			return error;
		return xfs_dir2_leaf_addname(args);
	}
	/*
	 * Just checking, and it would work, so say so.
	 */
	if (args->justcheck)
		return 0;
	needlog = needscan = 0;
	/*
	 * If need to compact the leaf entries, do it now.
	 * Leave the highest-numbered stale entry stale.
	 * XXX should be the one closest to mid but mid is not yet computed.
	 */
	if (compact) {
		int	fromidx;		/* source leaf index */
		int	toidx;			/* target leaf index */

		for (fromidx = toidx = INT_GET(btp->count, ARCH_CONVERT) - 1,
			highstale = lfloghigh = -1;
		     fromidx >= 0;
		     fromidx--) {
			if (INT_GET(blp[fromidx].address, ARCH_CONVERT) == XFS_DIR2_NULL_DATAPTR) {
				if (highstale == -1)
					highstale = toidx;
				else {
					if (lfloghigh == -1)
						lfloghigh = toidx;
					continue;
				}
			}
			if (fromidx < toidx)
				blp[toidx] = blp[fromidx];
			toidx--;
		}
		lfloglow = toidx + 1 - (INT_GET(btp->stale, ARCH_CONVERT) - 1);
		lfloghigh -= INT_GET(btp->stale, ARCH_CONVERT) - 1;
		INT_MOD(btp->count, ARCH_CONVERT, -(INT_GET(btp->stale, ARCH_CONVERT) - 1));
		xfs_dir2_data_make_free(tp, bp,
			(xfs_dir2_data_aoff_t)((char *)blp - (char *)block),
			(xfs_dir2_data_aoff_t)((INT_GET(btp->stale, ARCH_CONVERT) - 1) * sizeof(*blp)),
			&needlog, &needscan);
		blp += INT_GET(btp->stale, ARCH_CONVERT) - 1;
		INT_SET(btp->stale, ARCH_CONVERT, 1);
		/*
		 * If we now need to rebuild the bestfree map, do so.
		 * This needs to happen before the next call to use_free.
		 */
		if (needscan) {
			xfs_dir2_data_freescan(mp, (xfs_dir2_data_t *)block,
				&needlog, NULL);
			needscan = 0;
		}
	}
	/*
	 * Set leaf logging boundaries to impossible state.
	 * For the no-stale case they're set explicitly.
	 */
	else if (INT_GET(btp->stale, ARCH_CONVERT)) {
		lfloglow = INT_GET(btp->count, ARCH_CONVERT);
		lfloghigh = -1;
	}
	/*
	 * Find the slot that's first lower than our hash value, -1 if none.
	 */
	for (low = 0, high = INT_GET(btp->count, ARCH_CONVERT) - 1; low <= high; ) {
		mid = (low + high) >> 1;
		if ((hash = INT_GET(blp[mid].hashval, ARCH_CONVERT)) == args->hashval)
			break;
		if (hash < args->hashval)
			low = mid + 1;
		else
			high = mid - 1;
	}
	while (mid >= 0 && INT_GET(blp[mid].hashval, ARCH_CONVERT) >= args->hashval) {
		mid--;
	}
	/*
	 * No stale entries, will use enddup space to hold new leaf.
	 */
	if (!btp->stale) {
		/*
		 * Mark the space needed for the new leaf entry, now in use.
		 */
		xfs_dir2_data_use_free(tp, bp, enddup,
			(xfs_dir2_data_aoff_t)
			((char *)enddup - (char *)block + INT_GET(enddup->length, ARCH_CONVERT) -
			 sizeof(*blp)),
			(xfs_dir2_data_aoff_t)sizeof(*blp),
			&needlog, &needscan);
		/*
		 * Update the tail (entry count).
		 */
		INT_MOD(btp->count, ARCH_CONVERT, +1);
		/*
		 * If we now need to rebuild the bestfree map, do so.
		 * This needs to happen before the next call to use_free.
		 */
		if (needscan) {
			xfs_dir2_data_freescan(mp, (xfs_dir2_data_t *)block,
				&needlog, NULL);
			needscan = 0;
		}
		/*
		 * Adjust pointer to the first leaf entry, we're about to move
		 * the table up one to open up space for the new leaf entry.
		 * Then adjust our index to match.
		 */
		blp--;
		mid++;
		if (mid)
			memmove(blp, &blp[1], mid * sizeof(*blp));
		lfloglow = 0;
		lfloghigh = mid;
	}
	/*
	 * Use a stale leaf for our new entry.
	 */
	else {
		for (lowstale = mid;
		     lowstale >= 0 &&
			INT_GET(blp[lowstale].address, ARCH_CONVERT) != XFS_DIR2_NULL_DATAPTR;
		     lowstale--)
			continue;
		for (highstale = mid + 1;
		     highstale < INT_GET(btp->count, ARCH_CONVERT) &&
			INT_GET(blp[highstale].address, ARCH_CONVERT) != XFS_DIR2_NULL_DATAPTR &&
			(lowstale < 0 || mid - lowstale > highstale - mid);
		     highstale++)
			continue;
		/*
		 * Move entries toward the low-numbered stale entry.
		 */
		if (lowstale >= 0 &&
		    (highstale == INT_GET(btp->count, ARCH_CONVERT) ||
		     mid - lowstale <= highstale - mid)) {
			if (mid - lowstale)
				memmove(&blp[lowstale], &blp[lowstale + 1],
					(mid - lowstale) * sizeof(*blp));
			lfloglow = MIN(lowstale, lfloglow);
			lfloghigh = MAX(mid, lfloghigh);
		}
		/*
		 * Move entries toward the high-numbered stale entry.
		 */
		else {
			ASSERT(highstale < INT_GET(btp->count, ARCH_CONVERT));
			mid++;
			if (highstale - mid)
				memmove(&blp[mid + 1], &blp[mid],
					(highstale - mid) * sizeof(*blp));
			lfloglow = MIN(mid, lfloglow);
			lfloghigh = MAX(highstale, lfloghigh);
		}
		INT_MOD(btp->stale, ARCH_CONVERT, -1);
	}
	/*
	 * Point to the new data entry.
	 */
	dep = (xfs_dir2_data_entry_t *)dup;
	/*
	 * Fill in the leaf entry.
	 */
	INT_SET(blp[mid].hashval, ARCH_CONVERT, args->hashval);
	INT_SET(blp[mid].address, ARCH_CONVERT, XFS_DIR2_BYTE_TO_DATAPTR(mp, (char *)dep - (char *)block));
	xfs_dir2_block_log_leaf(tp, bp, lfloglow, lfloghigh);
	/*
	 * Mark space for the data entry used.
	 */
	xfs_dir2_data_use_free(tp, bp, dup,
		(xfs_dir2_data_aoff_t)((char *)dup - (char *)block),
		(xfs_dir2_data_aoff_t)len, &needlog, &needscan);
	/*
	 * Create the new data entry.
	 */
	INT_SET(dep->inumber, ARCH_CONVERT, args->inumber);
	dep->namelen = args->namelen;
	memcpy(dep->name, args->name, args->namelen);
	tagp = XFS_DIR2_DATA_ENTRY_TAG_P(dep);
	INT_SET(*tagp, ARCH_CONVERT, (xfs_dir2_data_off_t)((char *)dep - (char *)block));
	/*
	 * Clean up the bestfree array and log the header, tail, and entry.
	 */
	if (needscan)
		xfs_dir2_data_freescan(mp, (xfs_dir2_data_t *)block, &needlog,
			NULL);
	if (needlog)
		xfs_dir2_data_log_header(tp, bp);
	xfs_dir2_block_log_tail(tp, bp);
	xfs_dir2_data_log_entry(tp, bp, dep);
	xfs_dir2_data_check(dp, bp);
	xfs_da_buf_done(bp);
	return 0;
}

/*
 * Readdir for block directories.
 */
int						/* error */
xfs_dir2_block_getdents(
	xfs_trans_t		*tp,		/* transaction (NULL) */
	xfs_inode_t		*dp,		/* incore inode */
	uio_t			*uio,		/* caller's buffer control */
	int			*eofp,		/* eof reached? (out) */
	xfs_dirent_t		*dbp,		/* caller's buffer */
	xfs_dir2_put_t		put)		/* abi's formatting function */
{
	xfs_dir2_block_t	*block;		/* directory block structure */
	xfs_dabuf_t		*bp;		/* buffer for block */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_dir2_data_unused_t	*dup;		/* block unused entry */
	char			*endptr;	/* end of the data entries */
	int			error;		/* error return value */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_dir2_put_args_t	p;		/* arg package for put rtn */
	char			*ptr;		/* current data entry */
	int			wantoff;	/* starting block offset */

	mp = dp->i_mount;
	/*
	 * If the block number in the offset is out of range, we're done.
	 */
	if (XFS_DIR2_DATAPTR_TO_DB(mp, uio->uio_offset) > mp->m_dirdatablk) {
		*eofp = 1;
		return 0;
	}
	/*
	 * Can't read the block, give up, else get dabuf in bp.
	 */
	if ((error =
	    xfs_da_read_buf(tp, dp, mp->m_dirdatablk, -1, &bp, XFS_DATA_FORK))) {
		return error;
	}
	ASSERT(bp != NULL);
	/*
	 * Extract the byte offset we start at from the seek pointer.
	 * We'll skip entries before this.
	 */
	wantoff = XFS_DIR2_DATAPTR_TO_OFF(mp, uio->uio_offset);
	block = bp->data;
	xfs_dir2_data_check(dp, bp);
	/*
	 * Set up values for the loop.
	 */
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	ptr = (char *)block->u;
	endptr = (char *)XFS_DIR2_BLOCK_LEAF_P(btp);
	p.dbp = dbp;
	p.put = put;
	p.uio = uio;
	/*
	 * Loop over the data portion of the block.
	 * Each object is a real entry (dep) or an unused one (dup).
	 */
	while (ptr < endptr) {
		dup = (xfs_dir2_data_unused_t *)ptr;
		/*
		 * Unused, skip it.
		 */
		if (INT_GET(dup->freetag, ARCH_CONVERT) == XFS_DIR2_DATA_FREE_TAG) {
			ptr += INT_GET(dup->length, ARCH_CONVERT);
			continue;
		}

		dep = (xfs_dir2_data_entry_t *)ptr;

		/*
		 * Bump pointer for the next iteration.
		 */
		ptr += XFS_DIR2_DATA_ENTSIZE(dep->namelen);
		/*
		 * The entry is before the desired starting point, skip it.
		 */
		if ((char *)dep - (char *)block < wantoff)
			continue;
		/*
		 * Set up argument structure for put routine.
		 */
		p.namelen = dep->namelen;

		p.cook = XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
						    ptr - (char *)block);
		p.ino = INT_GET(dep->inumber, ARCH_CONVERT);
#if XFS_BIG_INUMS
		p.ino += mp->m_inoadd;
#endif
		p.name = (char *)dep->name;

		/*
		 * Put the entry in the caller's buffer.
		 */
		error = p.put(&p);

		/*
		 * If it didn't fit, set the final offset to here & return.
		 */
		if (!p.done) {
			uio->uio_offset =
				XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
					(char *)dep - (char *)block);
			xfs_da_brelse(tp, bp);
			return error;
		}
	}

	/*
	 * Reached the end of the block.
	 * Set the offset to a nonexistent block 1 and return.
	 */
	*eofp = 1;

	uio->uio_offset =
		XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk + 1, 0);

	xfs_da_brelse(tp, bp);

	return 0;
}

/*
 * Log leaf entries from the block.
 */
static void
xfs_dir2_block_log_leaf(
	xfs_trans_t		*tp,		/* transaction structure */
	xfs_dabuf_t		*bp,		/* block buffer */
	int			first,		/* index of first logged leaf */
	int			last)		/* index of last logged leaf */
{
	xfs_dir2_block_t	*block;		/* directory block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf entries */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_mount_t		*mp;		/* filesystem mount point */

	mp = tp->t_mountp;
	block = bp->data;
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	xfs_da_log_buf(tp, bp, (uint)((char *)&blp[first] - (char *)block),
		(uint)((char *)&blp[last + 1] - (char *)block - 1));
}

/*
 * Log the block tail.
 */
static void
xfs_dir2_block_log_tail(
	xfs_trans_t		*tp,		/* transaction structure */
	xfs_dabuf_t		*bp)		/* block buffer */
{
	xfs_dir2_block_t	*block;		/* directory block structure */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_mount_t		*mp;		/* filesystem mount point */

	mp = tp->t_mountp;
	block = bp->data;
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	xfs_da_log_buf(tp, bp, (uint)((char *)btp - (char *)block),
		(uint)((char *)(btp + 1) - (char *)block - 1));
}

/*
 * Look up an entry in the block.  This is the external routine,
 * xfs_dir2_block_lookup_int does the real work.
 */
int						/* error */
xfs_dir2_block_lookup(
	xfs_da_args_t		*args)		/* dir lookup arguments */
{
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf entries */
	xfs_dabuf_t		*bp;		/* block buffer */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_inode_t		*dp;		/* incore inode */
	int			ent;		/* entry index */
	int			error;		/* error return value */
	xfs_mount_t		*mp;		/* filesystem mount point */

	xfs_dir2_trace_args("block_lookup", args);
	/*
	 * Get the buffer, look up the entry.
	 * If not found (ENOENT) then return, have no buffer.
	 */
	if ((error = xfs_dir2_block_lookup_int(args, &bp, &ent)))
		return error;
	dp = args->dp;
	mp = dp->i_mount;
	block = bp->data;
	xfs_dir2_data_check(dp, bp);
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	/*
	 * Get the offset from the leaf entry, to point to the data.
	 */
	dep = (xfs_dir2_data_entry_t *)
	      ((char *)block + XFS_DIR2_DATAPTR_TO_OFF(mp, INT_GET(blp[ent].address, ARCH_CONVERT)));
	/*
	 * Fill in inode number, release the block.
	 */
	args->inumber = INT_GET(dep->inumber, ARCH_CONVERT);
	xfs_da_brelse(args->trans, bp);
	return XFS_ERROR(EEXIST);
}

/*
 * Internal block lookup routine.
 */
static int					/* error */
xfs_dir2_block_lookup_int(
	xfs_da_args_t		*args,		/* dir lookup arguments */
	xfs_dabuf_t		**bpp,		/* returned block buffer */
	int			*entno)		/* returned entry number */
{
	xfs_dir2_dataptr_t	addr;		/* data entry address */
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf entries */
	xfs_dabuf_t		*bp;		/* block buffer */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_inode_t		*dp;		/* incore inode */
	int			error;		/* error return value */
	xfs_dahash_t		hash;		/* found hash value */
	int			high;		/* binary search high index */
	int			low;		/* binary search low index */
	int			mid;		/* binary search current idx */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_trans_t		*tp;		/* transaction pointer */

	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	/*
	 * Read the buffer, return error if we can't get it.
	 */
	if ((error =
	    xfs_da_read_buf(tp, dp, mp->m_dirdatablk, -1, &bp, XFS_DATA_FORK))) {
		return error;
	}
	ASSERT(bp != NULL);
	block = bp->data;
	xfs_dir2_data_check(dp, bp);
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	/*
	 * Loop doing a binary search for our hash value.
	 * Find our entry, ENOENT if it's not there.
	 */
	for (low = 0, high = INT_GET(btp->count, ARCH_CONVERT) - 1; ; ) {
		ASSERT(low <= high);
		mid = (low + high) >> 1;
		if ((hash = INT_GET(blp[mid].hashval, ARCH_CONVERT)) == args->hashval)
			break;
		if (hash < args->hashval)
			low = mid + 1;
		else
			high = mid - 1;
		if (low > high) {
			ASSERT(args->oknoent);
			xfs_da_brelse(tp, bp);
			return XFS_ERROR(ENOENT);
		}
	}
	/*
	 * Back up to the first one with the right hash value.
	 */
	while (mid > 0 && INT_GET(blp[mid - 1].hashval, ARCH_CONVERT) == args->hashval) {
		mid--;
	}
	/*
	 * Now loop forward through all the entries with the
	 * right hash value looking for our name.
	 */
	do {
		if ((addr = INT_GET(blp[mid].address, ARCH_CONVERT)) == XFS_DIR2_NULL_DATAPTR)
			continue;
		/*
		 * Get pointer to the entry from the leaf.
		 */
		dep = (xfs_dir2_data_entry_t *)
			((char *)block + XFS_DIR2_DATAPTR_TO_OFF(mp, addr));
		/*
		 * Compare, if it's right give back buffer & entry number.
		 */
		if (dep->namelen == args->namelen &&
		    dep->name[0] == args->name[0] &&
		    memcmp(dep->name, args->name, args->namelen) == 0) {
			*bpp = bp;
			*entno = mid;
			return 0;
		}
	} while (++mid < INT_GET(btp->count, ARCH_CONVERT) && INT_GET(blp[mid].hashval, ARCH_CONVERT) == hash);
	/*
	 * No match, release the buffer and return ENOENT.
	 */
	ASSERT(args->oknoent);
	xfs_da_brelse(tp, bp);
	return XFS_ERROR(ENOENT);
}

/*
 * Remove an entry from a block format directory.
 * If that makes the block small enough to fit in shortform, transform it.
 */
int						/* error */
xfs_dir2_block_removename(
	xfs_da_args_t		*args)		/* directory operation args */
{
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf pointer */
	xfs_dabuf_t		*bp;		/* block buffer */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_inode_t		*dp;		/* incore inode */
	int			ent;		/* block leaf entry index */
	int			error;		/* error return value */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needlog;	/* need to log block header */
	int			needscan;	/* need to fixup bestfree */
	xfs_dir2_sf_hdr_t	sfh;		/* shortform header */
	int			size;		/* shortform size */
	xfs_trans_t		*tp;		/* transaction pointer */

	xfs_dir2_trace_args("block_removename", args);
	/*
	 * Look up the entry in the block.  Gets the buffer and entry index.
	 * It will always be there, the vnodeops level does a lookup first.
	 */
	if ((error = xfs_dir2_block_lookup_int(args, &bp, &ent))) {
		return error;
	}
	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	block = bp->data;
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	/*
	 * Point to the data entry using the leaf entry.
	 */
	dep = (xfs_dir2_data_entry_t *)
	      ((char *)block + XFS_DIR2_DATAPTR_TO_OFF(mp, INT_GET(blp[ent].address, ARCH_CONVERT)));
	/*
	 * Mark the data entry's space free.
	 */
	needlog = needscan = 0;
	xfs_dir2_data_make_free(tp, bp,
		(xfs_dir2_data_aoff_t)((char *)dep - (char *)block),
		XFS_DIR2_DATA_ENTSIZE(dep->namelen), &needlog, &needscan);
	/*
	 * Fix up the block tail.
	 */
	INT_MOD(btp->stale, ARCH_CONVERT, +1);
	xfs_dir2_block_log_tail(tp, bp);
	/*
	 * Remove the leaf entry by marking it stale.
	 */
	INT_SET(blp[ent].address, ARCH_CONVERT, XFS_DIR2_NULL_DATAPTR);
	xfs_dir2_block_log_leaf(tp, bp, ent, ent);
	/*
	 * Fix up bestfree, log the header if necessary.
	 */
	if (needscan)
		xfs_dir2_data_freescan(mp, (xfs_dir2_data_t *)block, &needlog,
			NULL);
	if (needlog)
		xfs_dir2_data_log_header(tp, bp);
	xfs_dir2_data_check(dp, bp);
	/*
	 * See if the size as a shortform is good enough.
	 */
	if ((size = xfs_dir2_block_sfsize(dp, block, &sfh)) >
	    XFS_IFORK_DSIZE(dp)) {
		xfs_da_buf_done(bp);
		return 0;
	}
	/*
	 * If it works, do the conversion.
	 */
	return xfs_dir2_block_to_sf(args, bp, size, &sfh);
}

/*
 * Replace an entry in a V2 block directory.
 * Change the inode number to the new value.
 */
int						/* error */
xfs_dir2_block_replace(
	xfs_da_args_t		*args)		/* directory operation args */
{
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf entries */
	xfs_dabuf_t		*bp;		/* block buffer */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* block data entry */
	xfs_inode_t		*dp;		/* incore inode */
	int			ent;		/* leaf entry index */
	int			error;		/* error return value */
	xfs_mount_t		*mp;		/* filesystem mount point */

	xfs_dir2_trace_args("block_replace", args);
	/*
	 * Lookup the entry in the directory.  Get buffer and entry index.
	 * This will always succeed since the caller has already done a lookup.
	 */
	if ((error = xfs_dir2_block_lookup_int(args, &bp, &ent))) {
		return error;
	}
	dp = args->dp;
	mp = dp->i_mount;
	block = bp->data;
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	/*
	 * Point to the data entry we need to change.
	 */
	dep = (xfs_dir2_data_entry_t *)
	      ((char *)block + XFS_DIR2_DATAPTR_TO_OFF(mp, INT_GET(blp[ent].address, ARCH_CONVERT)));
	ASSERT(INT_GET(dep->inumber, ARCH_CONVERT) != args->inumber);
	/*
	 * Change the inode number to the new value.
	 */
	INT_SET(dep->inumber, ARCH_CONVERT, args->inumber);
	xfs_dir2_data_log_entry(args->trans, bp, dep);
	xfs_dir2_data_check(dp, bp);
	xfs_da_buf_done(bp);
	return 0;
}

/*
 * Qsort comparison routine for the block leaf entries.
 */
static int					/* sort order */
xfs_dir2_block_sort(
	const void			*a,	/* first leaf entry */
	const void			*b)	/* second leaf entry */
{
	const xfs_dir2_leaf_entry_t	*la;	/* first leaf entry */
	const xfs_dir2_leaf_entry_t	*lb;	/* second leaf entry */

	la = a;
	lb = b;
	return INT_GET(la->hashval, ARCH_CONVERT) < INT_GET(lb->hashval, ARCH_CONVERT) ? -1 :
		(INT_GET(la->hashval, ARCH_CONVERT) > INT_GET(lb->hashval, ARCH_CONVERT) ? 1 : 0);
}

/*
 * Convert a V2 leaf directory to a V2 block directory if possible.
 */
int						/* error */
xfs_dir2_leaf_to_block(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_dabuf_t		*lbp,		/* leaf buffer */
	xfs_dabuf_t		*dbp)		/* data buffer */
{
	xfs_dir2_data_off_t	*bestsp;	/* leaf bests table */
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_data_unused_t	*dup;		/* unused data entry */
	int			error;		/* error return value */
	int			from;		/* leaf from index */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail structure */
	xfs_mount_t		*mp;		/* file system mount point */
	int			needlog;	/* need to log data header */
	int			needscan;	/* need to scan for bestfree */
	xfs_dir2_sf_hdr_t	sfh;		/* shortform header */
	int			size;		/* bytes used */
	xfs_dir2_data_off_t	*tagp;		/* end of entry (tag) */
	int			to;		/* block/leaf to index */
	xfs_trans_t		*tp;		/* transaction pointer */

	xfs_dir2_trace_args_bb("leaf_to_block", args, lbp, dbp);
	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	leaf = lbp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT) == XFS_DIR2_LEAF1_MAGIC);
	ltp = XFS_DIR2_LEAF_TAIL_P(mp, leaf);
	/*
	 * If there are data blocks other than the first one, take this
	 * opportunity to remove trailing empty data blocks that may have
	 * been left behind during no-space-reservation operations.
	 * These will show up in the leaf bests table.
	 */
	while (dp->i_d.di_size > mp->m_dirblksize) {
		bestsp = XFS_DIR2_LEAF_BESTS_P(ltp);
		if (INT_GET(bestsp[INT_GET(ltp->bestcount, ARCH_CONVERT) - 1], ARCH_CONVERT) ==
		    mp->m_dirblksize - (uint)sizeof(block->hdr)) {
			if ((error =
			    xfs_dir2_leaf_trim_data(args, lbp,
				    (xfs_dir2_db_t)(INT_GET(ltp->bestcount, ARCH_CONVERT) - 1))))
				goto out;
		} else {
			error = 0;
			goto out;
		}
	}
	/*
	 * Read the data block if we don't already have it, give up if it fails.
	 */
	if (dbp == NULL &&
	    (error = xfs_da_read_buf(tp, dp, mp->m_dirdatablk, -1, &dbp,
		    XFS_DATA_FORK))) {
		goto out;
	}
	block = dbp->data;
	ASSERT(INT_GET(block->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC);
	/*
	 * Size of the "leaf" area in the block.
	 */
	size = (uint)sizeof(block->tail) +
	       (uint)sizeof(*lep) * (INT_GET(leaf->hdr.count, ARCH_CONVERT) - INT_GET(leaf->hdr.stale, ARCH_CONVERT));
	/*
	 * Look at the last data entry.
	 */
	tagp = (xfs_dir2_data_off_t *)((char *)block + mp->m_dirblksize) - 1;
	dup = (xfs_dir2_data_unused_t *)((char *)block + INT_GET(*tagp, ARCH_CONVERT));
	/*
	 * If it's not free or is too short we can't do it.
	 */
	if (INT_GET(dup->freetag, ARCH_CONVERT) != XFS_DIR2_DATA_FREE_TAG || INT_GET(dup->length, ARCH_CONVERT) < size) {
		error = 0;
		goto out;
	}
	/*
	 * Start converting it to block form.
	 */
	INT_SET(block->hdr.magic, ARCH_CONVERT, XFS_DIR2_BLOCK_MAGIC);
	needlog = 1;
	needscan = 0;
	/*
	 * Use up the space at the end of the block (blp/btp).
	 */
	xfs_dir2_data_use_free(tp, dbp, dup, mp->m_dirblksize - size, size,
		&needlog, &needscan);
	/*
	 * Initialize the block tail.
	 */
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	INT_SET(btp->count, ARCH_CONVERT, INT_GET(leaf->hdr.count, ARCH_CONVERT) - INT_GET(leaf->hdr.stale, ARCH_CONVERT));
	btp->stale = 0;
	xfs_dir2_block_log_tail(tp, dbp);
	/*
	 * Initialize the block leaf area.  We compact out stale entries.
	 */
	lep = XFS_DIR2_BLOCK_LEAF_P(btp);
	for (from = to = 0; from < INT_GET(leaf->hdr.count, ARCH_CONVERT); from++) {
		if (INT_GET(leaf->ents[from].address, ARCH_CONVERT) == XFS_DIR2_NULL_DATAPTR)
			continue;
		lep[to++] = leaf->ents[from];
	}
	ASSERT(to == INT_GET(btp->count, ARCH_CONVERT));
	xfs_dir2_block_log_leaf(tp, dbp, 0, INT_GET(btp->count, ARCH_CONVERT) - 1);
	/*
	 * Scan the bestfree if we need it and log the data block header.
	 */
	if (needscan)
		xfs_dir2_data_freescan(mp, (xfs_dir2_data_t *)block, &needlog,
			NULL);
	if (needlog)
		xfs_dir2_data_log_header(tp, dbp);
	/*
	 * Pitch the old leaf block.
	 */
	error = xfs_da_shrink_inode(args, mp->m_dirleafblk, lbp);
	lbp = NULL;
	if (error) {
		goto out;
	}
	/*
	 * Now see if the resulting block can be shrunken to shortform.
	 */
	if ((size = xfs_dir2_block_sfsize(dp, block, &sfh)) >
	    XFS_IFORK_DSIZE(dp)) {
		error = 0;
		goto out;
	}
	return xfs_dir2_block_to_sf(args, dbp, size, &sfh);
out:
	if (lbp)
		xfs_da_buf_done(lbp);
	if (dbp)
		xfs_da_buf_done(dbp);
	return error;
}

/*
 * Convert the shortform directory to block form.
 */
int						/* error */
xfs_dir2_sf_to_block(
	xfs_da_args_t		*args)		/* operation arguments */
{
	xfs_dir2_db_t		blkno;		/* dir-relative block # (0) */
	xfs_dir2_block_t	*block;		/* block structure */
	xfs_dir2_leaf_entry_t	*blp;		/* block leaf entries */
	xfs_dabuf_t		*bp;		/* block buffer */
	xfs_dir2_block_tail_t	*btp;		/* block tail pointer */
	char			*buf;		/* sf buffer */
	int			buf_len;
	xfs_dir2_data_entry_t	*dep;		/* data entry pointer */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			dummy;		/* trash */
	xfs_dir2_data_unused_t	*dup;		/* unused entry pointer */
	int			endoffset;	/* end of data objects */
	int			error;		/* error return value */
	int			i;		/* index */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needlog;	/* need to log block header */
	int			needscan;	/* need to scan block freespc */
	int			newoffset;	/* offset from current entry */
	int			offset;		/* target block offset */
	xfs_dir2_sf_entry_t	*sfep;		/* sf entry pointer */
	xfs_dir2_sf_t		*sfp;		/* shortform structure */
	xfs_dir2_data_off_t	*tagp;		/* end of data entry */
	xfs_trans_t		*tp;		/* transaction pointer */

	xfs_dir2_trace_args("sf_to_block", args);
	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Bomb out if the shortform directory is way too short.
	 */
	if (dp->i_d.di_size < offsetof(xfs_dir2_sf_hdr_t, parent)) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sfp = (xfs_dir2_sf_t *)dp->i_df.if_u1.if_data;
	ASSERT(dp->i_d.di_size >= XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count));
	/*
	 * Copy the directory into the stack buffer.
	 * Then pitch the incore inode data so we can make extents.
	 */

	buf_len = dp->i_df.if_bytes;
	buf = kmem_alloc(dp->i_df.if_bytes, KM_SLEEP);

	memcpy(buf, sfp, dp->i_df.if_bytes);
	xfs_idata_realloc(dp, -dp->i_df.if_bytes, XFS_DATA_FORK);
	dp->i_d.di_size = 0;
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	/*
	 * Reset pointer - old sfp is gone.
	 */
	sfp = (xfs_dir2_sf_t *)buf;
	/*
	 * Add block 0 to the inode.
	 */
	error = xfs_dir2_grow_inode(args, XFS_DIR2_DATA_SPACE, &blkno);
	if (error) {
		kmem_free(buf, buf_len);
		return error;
	}
	/*
	 * Initialize the data block.
	 */
	error = xfs_dir2_data_init(args, blkno, &bp);
	if (error) {
		kmem_free(buf, buf_len);
		return error;
	}
	block = bp->data;
	INT_SET(block->hdr.magic, ARCH_CONVERT, XFS_DIR2_BLOCK_MAGIC);
	/*
	 * Compute size of block "tail" area.
	 */
	i = (uint)sizeof(*btp) +
	    (INT_GET(sfp->hdr.count, ARCH_CONVERT) + 2) * (uint)sizeof(xfs_dir2_leaf_entry_t);
	/*
	 * The whole thing is initialized to free by the init routine.
	 * Say we're using the leaf and tail area.
	 */
	dup = (xfs_dir2_data_unused_t *)block->u;
	needlog = needscan = 0;
	xfs_dir2_data_use_free(tp, bp, dup, mp->m_dirblksize - i, i, &needlog,
		&needscan);
	ASSERT(needscan == 0);
	/*
	 * Fill in the tail.
	 */
	btp = XFS_DIR2_BLOCK_TAIL_P(mp, block);
	INT_SET(btp->count, ARCH_CONVERT, INT_GET(sfp->hdr.count, ARCH_CONVERT) + 2);	/* ., .. */
	btp->stale = 0;
	blp = XFS_DIR2_BLOCK_LEAF_P(btp);
	endoffset = (uint)((char *)blp - (char *)block);
	/*
	 * Remove the freespace, we'll manage it.
	 */
	xfs_dir2_data_use_free(tp, bp, dup,
		(xfs_dir2_data_aoff_t)((char *)dup - (char *)block),
		INT_GET(dup->length, ARCH_CONVERT), &needlog, &needscan);
	/*
	 * Create entry for .
	 */
	dep = (xfs_dir2_data_entry_t *)
	      ((char *)block + XFS_DIR2_DATA_DOT_OFFSET);
	INT_SET(dep->inumber, ARCH_CONVERT, dp->i_ino);
	dep->namelen = 1;
	dep->name[0] = '.';
	tagp = XFS_DIR2_DATA_ENTRY_TAG_P(dep);
	INT_SET(*tagp, ARCH_CONVERT, (xfs_dir2_data_off_t)((char *)dep - (char *)block));
	xfs_dir2_data_log_entry(tp, bp, dep);
	INT_SET(blp[0].hashval, ARCH_CONVERT, xfs_dir_hash_dot);
	INT_SET(blp[0].address, ARCH_CONVERT, XFS_DIR2_BYTE_TO_DATAPTR(mp, (char *)dep - (char *)block));
	/*
	 * Create entry for ..
	 */
	dep = (xfs_dir2_data_entry_t *)
		((char *)block + XFS_DIR2_DATA_DOTDOT_OFFSET);
	INT_SET(dep->inumber, ARCH_CONVERT, XFS_DIR2_SF_GET_INUMBER(sfp, &sfp->hdr.parent));
	dep->namelen = 2;
	dep->name[0] = dep->name[1] = '.';
	tagp = XFS_DIR2_DATA_ENTRY_TAG_P(dep);
	INT_SET(*tagp, ARCH_CONVERT, (xfs_dir2_data_off_t)((char *)dep - (char *)block));
	xfs_dir2_data_log_entry(tp, bp, dep);
	INT_SET(blp[1].hashval, ARCH_CONVERT, xfs_dir_hash_dotdot);
	INT_SET(blp[1].address, ARCH_CONVERT, XFS_DIR2_BYTE_TO_DATAPTR(mp, (char *)dep - (char *)block));
	offset = XFS_DIR2_DATA_FIRST_OFFSET;
	/*
	 * Loop over existing entries, stuff them in.
	 */
	if ((i = 0) == INT_GET(sfp->hdr.count, ARCH_CONVERT))
		sfep = NULL;
	else
		sfep = XFS_DIR2_SF_FIRSTENTRY(sfp);
	/*
	 * Need to preserve the existing offset values in the sf directory.
	 * Insert holes (unused entries) where necessary.
	 */
	while (offset < endoffset) {
		/*
		 * sfep is null when we reach the end of the list.
		 */
		if (sfep == NULL)
			newoffset = endoffset;
		else
			newoffset = XFS_DIR2_SF_GET_OFFSET(sfep);
		/*
		 * There should be a hole here, make one.
		 */
		if (offset < newoffset) {
			dup = (xfs_dir2_data_unused_t *)
			      ((char *)block + offset);
			INT_SET(dup->freetag, ARCH_CONVERT, XFS_DIR2_DATA_FREE_TAG);
			INT_SET(dup->length, ARCH_CONVERT, newoffset - offset);
			INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(dup), ARCH_CONVERT,
				(xfs_dir2_data_off_t)
				((char *)dup - (char *)block));
			xfs_dir2_data_log_unused(tp, bp, dup);
			(void)xfs_dir2_data_freeinsert((xfs_dir2_data_t *)block,
				dup, &dummy);
			offset += INT_GET(dup->length, ARCH_CONVERT);
			continue;
		}
		/*
		 * Copy a real entry.
		 */
		dep = (xfs_dir2_data_entry_t *)((char *)block + newoffset);
		INT_SET(dep->inumber, ARCH_CONVERT, XFS_DIR2_SF_GET_INUMBER(sfp,
				XFS_DIR2_SF_INUMBERP(sfep)));
		dep->namelen = sfep->namelen;
		memcpy(dep->name, sfep->name, dep->namelen);
		tagp = XFS_DIR2_DATA_ENTRY_TAG_P(dep);
		INT_SET(*tagp, ARCH_CONVERT, (xfs_dir2_data_off_t)((char *)dep - (char *)block));
		xfs_dir2_data_log_entry(tp, bp, dep);
		INT_SET(blp[2 + i].hashval, ARCH_CONVERT, xfs_da_hashname((char *)sfep->name, sfep->namelen));
		INT_SET(blp[2 + i].address, ARCH_CONVERT, XFS_DIR2_BYTE_TO_DATAPTR(mp,
						 (char *)dep - (char *)block));
		offset = (int)((char *)(tagp + 1) - (char *)block);
		if (++i == INT_GET(sfp->hdr.count, ARCH_CONVERT))
			sfep = NULL;
		else
			sfep = XFS_DIR2_SF_NEXTENTRY(sfp, sfep);
	}
	/* Done with the temporary buffer */
	kmem_free(buf, buf_len);
	/*
	 * Sort the leaf entries by hash value.
	 */
	qsort(blp, INT_GET(btp->count, ARCH_CONVERT), sizeof(*blp), xfs_dir2_block_sort);
	/*
	 * Log the leaf entry area and tail.
	 * Already logged the header in data_init, ignore needlog.
	 */
	ASSERT(needscan == 0);
	xfs_dir2_block_log_leaf(tp, bp, 0, INT_GET(btp->count, ARCH_CONVERT) - 1);
	xfs_dir2_block_log_tail(tp, bp);
	xfs_dir2_data_check(dp, bp);
	xfs_da_buf_done(bp);
	return 0;
}
