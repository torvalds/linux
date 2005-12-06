/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_dir_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_error.h"

#ifdef DEBUG
/*
 * Check the consistency of the data block.
 * The input can also be a block-format directory.
 * Pop an assert if we find anything bad.
 */
void
xfs_dir2_data_check(
	xfs_inode_t		*dp,		/* incore inode pointer */
	xfs_dabuf_t		*bp)		/* data block's buffer */
{
	xfs_dir2_dataptr_t	addr;		/* addr for leaf lookup */
	xfs_dir2_data_free_t	*bf;		/* bestfree table */
	xfs_dir2_block_tail_t	*btp=NULL;	/* block tail */
	int			count;		/* count of entries found */
	xfs_dir2_data_t		*d;		/* data block pointer */
	xfs_dir2_data_entry_t	*dep;		/* data entry */
	xfs_dir2_data_free_t	*dfp;		/* bestfree entry */
	xfs_dir2_data_unused_t	*dup;		/* unused entry */
	char			*endp;		/* end of useful data */
	int			freeseen;	/* mask of bestfrees seen */
	xfs_dahash_t		hash;		/* hash of current name */
	int			i;		/* leaf index */
	int			lastfree;	/* last entry was unused */
	xfs_dir2_leaf_entry_t	*lep=NULL;	/* block leaf entries */
	xfs_mount_t		*mp;		/* filesystem mount point */
	char			*p;		/* current data position */
	int			stale;		/* count of stale leaves */

	mp = dp->i_mount;
	d = bp->data;
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
	bf = d->hdr.bestfree;
	p = (char *)d->u;
	if (INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC) {
		btp = XFS_DIR2_BLOCK_TAIL_P(mp, (xfs_dir2_block_t *)d);
		lep = XFS_DIR2_BLOCK_LEAF_P(btp);
		endp = (char *)lep;
	} else
		endp = (char *)d + mp->m_dirblksize;
	count = lastfree = freeseen = 0;
	/*
	 * Account for zero bestfree entries.
	 */
	if (!bf[0].length) {
		ASSERT(!bf[0].offset);
		freeseen |= 1 << 0;
	}
	if (!bf[1].length) {
		ASSERT(!bf[1].offset);
		freeseen |= 1 << 1;
	}
	if (!bf[2].length) {
		ASSERT(!bf[2].offset);
		freeseen |= 1 << 2;
	}
	ASSERT(INT_GET(bf[0].length, ARCH_CONVERT) >= INT_GET(bf[1].length, ARCH_CONVERT));
	ASSERT(INT_GET(bf[1].length, ARCH_CONVERT) >= INT_GET(bf[2].length, ARCH_CONVERT));
	/*
	 * Loop over the data/unused entries.
	 */
	while (p < endp) {
		dup = (xfs_dir2_data_unused_t *)p;
		/*
		 * If it's unused, look for the space in the bestfree table.
		 * If we find it, account for that, else make sure it
		 * doesn't need to be there.
		 */
		if (INT_GET(dup->freetag, ARCH_CONVERT) == XFS_DIR2_DATA_FREE_TAG) {
			ASSERT(lastfree == 0);
			ASSERT(INT_GET(*XFS_DIR2_DATA_UNUSED_TAG_P(dup), ARCH_CONVERT) ==
			       (char *)dup - (char *)d);
			dfp = xfs_dir2_data_freefind(d, dup);
			if (dfp) {
				i = (int)(dfp - bf);
				ASSERT((freeseen & (1 << i)) == 0);
				freeseen |= 1 << i;
			} else
				ASSERT(INT_GET(dup->length, ARCH_CONVERT) <= INT_GET(bf[2].length, ARCH_CONVERT));
			p += INT_GET(dup->length, ARCH_CONVERT);
			lastfree = 1;
			continue;
		}
		/*
		 * It's a real entry.  Validate the fields.
		 * If this is a block directory then make sure it's
		 * in the leaf section of the block.
		 * The linear search is crude but this is DEBUG code.
		 */
		dep = (xfs_dir2_data_entry_t *)p;
		ASSERT(dep->namelen != 0);
		ASSERT(xfs_dir_ino_validate(mp, INT_GET(dep->inumber, ARCH_CONVERT)) == 0);
		ASSERT(INT_GET(*XFS_DIR2_DATA_ENTRY_TAG_P(dep), ARCH_CONVERT) ==
		       (char *)dep - (char *)d);
		count++;
		lastfree = 0;
		if (INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC) {
			addr = XFS_DIR2_DB_OFF_TO_DATAPTR(mp, mp->m_dirdatablk,
				(xfs_dir2_data_aoff_t)
				((char *)dep - (char *)d));
			hash = xfs_da_hashname((char *)dep->name, dep->namelen);
			for (i = 0; i < INT_GET(btp->count, ARCH_CONVERT); i++) {
				if (INT_GET(lep[i].address, ARCH_CONVERT) == addr &&
				    INT_GET(lep[i].hashval, ARCH_CONVERT) == hash)
					break;
			}
			ASSERT(i < INT_GET(btp->count, ARCH_CONVERT));
		}
		p += XFS_DIR2_DATA_ENTSIZE(dep->namelen);
	}
	/*
	 * Need to have seen all the entries and all the bestfree slots.
	 */
	ASSERT(freeseen == 7);
	if (INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC) {
		for (i = stale = 0; i < INT_GET(btp->count, ARCH_CONVERT); i++) {
			if (INT_GET(lep[i].address, ARCH_CONVERT) == XFS_DIR2_NULL_DATAPTR)
				stale++;
			if (i > 0)
				ASSERT(INT_GET(lep[i].hashval, ARCH_CONVERT) >= INT_GET(lep[i - 1].hashval, ARCH_CONVERT));
		}
		ASSERT(count == INT_GET(btp->count, ARCH_CONVERT) - INT_GET(btp->stale, ARCH_CONVERT));
		ASSERT(stale == INT_GET(btp->stale, ARCH_CONVERT));
	}
}
#endif

/*
 * Given a data block and an unused entry from that block,
 * return the bestfree entry if any that corresponds to it.
 */
xfs_dir2_data_free_t *
xfs_dir2_data_freefind(
	xfs_dir2_data_t		*d,		/* data block */
	xfs_dir2_data_unused_t	*dup)		/* data unused entry */
{
	xfs_dir2_data_free_t	*dfp;		/* bestfree entry */
	xfs_dir2_data_aoff_t	off;		/* offset value needed */
#if defined(DEBUG) && defined(__KERNEL__)
	int			matched;	/* matched the value */
	int			seenzero;	/* saw a 0 bestfree entry */
#endif

	off = (xfs_dir2_data_aoff_t)((char *)dup - (char *)d);
#if defined(DEBUG) && defined(__KERNEL__)
	/*
	 * Validate some consistency in the bestfree table.
	 * Check order, non-overlapping entries, and if we find the
	 * one we're looking for it has to be exact.
	 */
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
	for (dfp = &d->hdr.bestfree[0], seenzero = matched = 0;
	     dfp < &d->hdr.bestfree[XFS_DIR2_DATA_FD_COUNT];
	     dfp++) {
		if (!dfp->offset) {
			ASSERT(!dfp->length);
			seenzero = 1;
			continue;
		}
		ASSERT(seenzero == 0);
		if (INT_GET(dfp->offset, ARCH_CONVERT) == off) {
			matched = 1;
			ASSERT(INT_GET(dfp->length, ARCH_CONVERT) == INT_GET(dup->length, ARCH_CONVERT));
		} else if (off < INT_GET(dfp->offset, ARCH_CONVERT))
			ASSERT(off + INT_GET(dup->length, ARCH_CONVERT) <= INT_GET(dfp->offset, ARCH_CONVERT));
		else
			ASSERT(INT_GET(dfp->offset, ARCH_CONVERT) + INT_GET(dfp->length, ARCH_CONVERT) <= off);
		ASSERT(matched || INT_GET(dfp->length, ARCH_CONVERT) >= INT_GET(dup->length, ARCH_CONVERT));
		if (dfp > &d->hdr.bestfree[0])
			ASSERT(INT_GET(dfp[-1].length, ARCH_CONVERT) >= INT_GET(dfp[0].length, ARCH_CONVERT));
	}
#endif
	/*
	 * If this is smaller than the smallest bestfree entry,
	 * it can't be there since they're sorted.
	 */
	if (INT_GET(dup->length, ARCH_CONVERT) < INT_GET(d->hdr.bestfree[XFS_DIR2_DATA_FD_COUNT - 1].length, ARCH_CONVERT))
		return NULL;
	/*
	 * Look at the three bestfree entries for our guy.
	 */
	for (dfp = &d->hdr.bestfree[0];
	     dfp < &d->hdr.bestfree[XFS_DIR2_DATA_FD_COUNT];
	     dfp++) {
		if (!dfp->offset)
			return NULL;
		if (INT_GET(dfp->offset, ARCH_CONVERT) == off)
			return dfp;
	}
	/*
	 * Didn't find it.  This only happens if there are duplicate lengths.
	 */
	return NULL;
}

/*
 * Insert an unused-space entry into the bestfree table.
 */
xfs_dir2_data_free_t *				/* entry inserted */
xfs_dir2_data_freeinsert(
	xfs_dir2_data_t		*d,		/* data block pointer */
	xfs_dir2_data_unused_t	*dup,		/* unused space */
	int			*loghead)	/* log the data header (out) */
{
	xfs_dir2_data_free_t	*dfp;		/* bestfree table pointer */
	xfs_dir2_data_free_t	new;		/* new bestfree entry */

#ifdef __KERNEL__
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
#endif
	dfp = d->hdr.bestfree;
	INT_COPY(new.length, dup->length, ARCH_CONVERT);
	INT_SET(new.offset, ARCH_CONVERT, (xfs_dir2_data_off_t)((char *)dup - (char *)d));
	/*
	 * Insert at position 0, 1, or 2; or not at all.
	 */
	if (INT_GET(new.length, ARCH_CONVERT) > INT_GET(dfp[0].length, ARCH_CONVERT)) {
		dfp[2] = dfp[1];
		dfp[1] = dfp[0];
		dfp[0] = new;
		*loghead = 1;
		return &dfp[0];
	}
	if (INT_GET(new.length, ARCH_CONVERT) > INT_GET(dfp[1].length, ARCH_CONVERT)) {
		dfp[2] = dfp[1];
		dfp[1] = new;
		*loghead = 1;
		return &dfp[1];
	}
	if (INT_GET(new.length, ARCH_CONVERT) > INT_GET(dfp[2].length, ARCH_CONVERT)) {
		dfp[2] = new;
		*loghead = 1;
		return &dfp[2];
	}
	return NULL;
}

/*
 * Remove a bestfree entry from the table.
 */
STATIC void
xfs_dir2_data_freeremove(
	xfs_dir2_data_t		*d,		/* data block pointer */
	xfs_dir2_data_free_t	*dfp,		/* bestfree entry pointer */
	int			*loghead)	/* out: log data header */
{
#ifdef __KERNEL__
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
#endif
	/*
	 * It's the first entry, slide the next 2 up.
	 */
	if (dfp == &d->hdr.bestfree[0]) {
		d->hdr.bestfree[0] = d->hdr.bestfree[1];
		d->hdr.bestfree[1] = d->hdr.bestfree[2];
	}
	/*
	 * It's the second entry, slide the 3rd entry up.
	 */
	else if (dfp == &d->hdr.bestfree[1])
		d->hdr.bestfree[1] = d->hdr.bestfree[2];
	/*
	 * Must be the last entry.
	 */
	else
		ASSERT(dfp == &d->hdr.bestfree[2]);
	/*
	 * Clear the 3rd entry, must be zero now.
	 */
	d->hdr.bestfree[2].length = 0;
	d->hdr.bestfree[2].offset = 0;
	*loghead = 1;
}

/*
 * Given a data block, reconstruct its bestfree map.
 */
void
xfs_dir2_data_freescan(
	xfs_mount_t		*mp,		/* filesystem mount point */
	xfs_dir2_data_t		*d,		/* data block pointer */
	int			*loghead,	/* out: log data header */
	char			*aendp)		/* in: caller's endp */
{
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* active data entry */
	xfs_dir2_data_unused_t	*dup;		/* unused data entry */
	char			*endp;		/* end of block's data */
	char			*p;		/* current entry pointer */

#ifdef __KERNEL__
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
#endif
	/*
	 * Start by clearing the table.
	 */
	memset(d->hdr.bestfree, 0, sizeof(d->hdr.bestfree));
	*loghead = 1;
	/*
	 * Set up pointers.
	 */
	p = (char *)d->u;
	if (aendp)
		endp = aendp;
	else if (INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC) {
		btp = XFS_DIR2_BLOCK_TAIL_P(mp, (xfs_dir2_block_t *)d);
		endp = (char *)XFS_DIR2_BLOCK_LEAF_P(btp);
	} else
		endp = (char *)d + mp->m_dirblksize;
	/*
	 * Loop over the block's entries.
	 */
	while (p < endp) {
		dup = (xfs_dir2_data_unused_t *)p;
		/*
		 * If it's a free entry, insert it.
		 */
		if (INT_GET(dup->freetag, ARCH_CONVERT) == XFS_DIR2_DATA_FREE_TAG) {
			ASSERT((char *)dup - (char *)d ==
			       INT_GET(*XFS_DIR2_DATA_UNUSED_TAG_P(dup), ARCH_CONVERT));
			xfs_dir2_data_freeinsert(d, dup, loghead);
			p += INT_GET(dup->length, ARCH_CONVERT);
		}
		/*
		 * For active entries, check their tags and skip them.
		 */
		else {
			dep = (xfs_dir2_data_entry_t *)p;
			ASSERT((char *)dep - (char *)d ==
			       INT_GET(*XFS_DIR2_DATA_ENTRY_TAG_P(dep), ARCH_CONVERT));
			p += XFS_DIR2_DATA_ENTSIZE(dep->namelen);
		}
	}
}

/*
 * Initialize a data block at the given block number in the directory.
 * Give back the buffer for the created block.
 */
int						/* error */
xfs_dir2_data_init(
	xfs_da_args_t		*args,		/* directory operation args */
	xfs_dir2_db_t		blkno,		/* logical dir block number */
	xfs_dabuf_t		**bpp)		/* output block buffer */
{
	xfs_dabuf_t		*bp;		/* block buffer */
	xfs_dir2_data_t		*d;		/* pointer to block */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_data_unused_t	*dup;		/* unused entry pointer */
	int			error;		/* error return value */
	int			i;		/* bestfree index */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_trans_t		*tp;		/* transaction pointer */
	int                     t;              /* temp */

	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	/*
	 * Get the buffer set up for the block.
	 */
	error = xfs_da_get_buf(tp, dp, XFS_DIR2_DB_TO_DA(mp, blkno), -1, &bp,
		XFS_DATA_FORK);
	if (error) {
		return error;
	}
	ASSERT(bp != NULL);
	/*
	 * Initialize the header.
	 */
	d = bp->data;
	INT_SET(d->hdr.magic, ARCH_CONVERT, XFS_DIR2_DATA_MAGIC);
	INT_SET(d->hdr.bestfree[0].offset, ARCH_CONVERT, (xfs_dir2_data_off_t)sizeof(d->hdr));
	for (i = 1; i < XFS_DIR2_DATA_FD_COUNT; i++) {
		d->hdr.bestfree[i].length = 0;
		d->hdr.bestfree[i].offset = 0;
	}
	/*
	 * Set up an unused entry for the block's body.
	 */
	dup = &d->u[0].unused;
	INT_SET(dup->freetag, ARCH_CONVERT, XFS_DIR2_DATA_FREE_TAG);

	t=mp->m_dirblksize - (uint)sizeof(d->hdr);
	INT_SET(d->hdr.bestfree[0].length, ARCH_CONVERT, t);
	INT_SET(dup->length, ARCH_CONVERT, t);
	INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(dup), ARCH_CONVERT,
		(xfs_dir2_data_off_t)((char *)dup - (char *)d));
	/*
	 * Log it and return it.
	 */
	xfs_dir2_data_log_header(tp, bp);
	xfs_dir2_data_log_unused(tp, bp, dup);
	*bpp = bp;
	return 0;
}

/*
 * Log an active data entry from the block.
 */
void
xfs_dir2_data_log_entry(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp,		/* block buffer */
	xfs_dir2_data_entry_t	*dep)		/* data entry pointer */
{
	xfs_dir2_data_t		*d;		/* data block pointer */

	d = bp->data;
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
	xfs_da_log_buf(tp, bp, (uint)((char *)dep - (char *)d),
		(uint)((char *)(XFS_DIR2_DATA_ENTRY_TAG_P(dep) + 1) -
		       (char *)d - 1));
}

/*
 * Log a data block header.
 */
void
xfs_dir2_data_log_header(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp)		/* block buffer */
{
	xfs_dir2_data_t		*d;		/* data block pointer */

	d = bp->data;
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
	xfs_da_log_buf(tp, bp, (uint)((char *)&d->hdr - (char *)d),
		(uint)(sizeof(d->hdr) - 1));
}

/*
 * Log a data unused entry.
 */
void
xfs_dir2_data_log_unused(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp,		/* block buffer */
	xfs_dir2_data_unused_t	*dup)		/* data unused pointer */
{
	xfs_dir2_data_t		*d;		/* data block pointer */

	d = bp->data;
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
	/*
	 * Log the first part of the unused entry.
	 */
	xfs_da_log_buf(tp, bp, (uint)((char *)dup - (char *)d),
		(uint)((char *)&dup->length + sizeof(dup->length) -
		       1 - (char *)d));
	/*
	 * Log the end (tag) of the unused entry.
	 */
	xfs_da_log_buf(tp, bp,
		(uint)((char *)XFS_DIR2_DATA_UNUSED_TAG_P(dup) - (char *)d),
		(uint)((char *)XFS_DIR2_DATA_UNUSED_TAG_P(dup) - (char *)d +
		       sizeof(xfs_dir2_data_off_t) - 1));
}

/*
 * Make a byte range in the data block unused.
 * Its current contents are unimportant.
 */
void
xfs_dir2_data_make_free(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp,		/* block buffer */
	xfs_dir2_data_aoff_t	offset,		/* starting byte offset */
	xfs_dir2_data_aoff_t	len,		/* length in bytes */
	int			*needlogp,	/* out: log header */
	int			*needscanp)	/* out: regen bestfree */
{
	xfs_dir2_data_t		*d;		/* data block pointer */
	xfs_dir2_data_free_t	*dfp;		/* bestfree pointer */
	char			*endptr;	/* end of data area */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needscan;	/* need to regen bestfree */
	xfs_dir2_data_unused_t	*newdup;	/* new unused entry */
	xfs_dir2_data_unused_t	*postdup;	/* unused entry after us */
	xfs_dir2_data_unused_t	*prevdup;	/* unused entry before us */

	mp = tp->t_mountp;
	d = bp->data;
	/*
	 * Figure out where the end of the data area is.
	 */
	if (INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC)
		endptr = (char *)d + mp->m_dirblksize;
	else {
		xfs_dir2_block_tail_t	*btp;	/* block tail */

		ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
		btp = XFS_DIR2_BLOCK_TAIL_P(mp, (xfs_dir2_block_t *)d);
		endptr = (char *)XFS_DIR2_BLOCK_LEAF_P(btp);
	}
	/*
	 * If this isn't the start of the block, then back up to
	 * the previous entry and see if it's free.
	 */
	if (offset > sizeof(d->hdr)) {
		xfs_dir2_data_off_t	*tagp;	/* tag just before us */

		tagp = (xfs_dir2_data_off_t *)((char *)d + offset) - 1;
		prevdup = (xfs_dir2_data_unused_t *)((char *)d + INT_GET(*tagp, ARCH_CONVERT));
		if (INT_GET(prevdup->freetag, ARCH_CONVERT) != XFS_DIR2_DATA_FREE_TAG)
			prevdup = NULL;
	} else
		prevdup = NULL;
	/*
	 * If this isn't the end of the block, see if the entry after
	 * us is free.
	 */
	if ((char *)d + offset + len < endptr) {
		postdup =
			(xfs_dir2_data_unused_t *)((char *)d + offset + len);
		if (INT_GET(postdup->freetag, ARCH_CONVERT) != XFS_DIR2_DATA_FREE_TAG)
			postdup = NULL;
	} else
		postdup = NULL;
	ASSERT(*needscanp == 0);
	needscan = 0;
	/*
	 * Previous and following entries are both free,
	 * merge everything into a single free entry.
	 */
	if (prevdup && postdup) {
		xfs_dir2_data_free_t	*dfp2;	/* another bestfree pointer */

		/*
		 * See if prevdup and/or postdup are in bestfree table.
		 */
		dfp = xfs_dir2_data_freefind(d, prevdup);
		dfp2 = xfs_dir2_data_freefind(d, postdup);
		/*
		 * We need a rescan unless there are exactly 2 free entries
		 * namely our two.  Then we know what's happening, otherwise
		 * since the third bestfree is there, there might be more
		 * entries.
		 */
		needscan = d->hdr.bestfree[2].length;
		/*
		 * Fix up the new big freespace.
		 */
		INT_MOD(prevdup->length, ARCH_CONVERT, len + INT_GET(postdup->length, ARCH_CONVERT));
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(prevdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)prevdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, prevdup);
		if (!needscan) {
			/*
			 * Has to be the case that entries 0 and 1 are
			 * dfp and dfp2 (don't know which is which), and
			 * entry 2 is empty.
			 * Remove entry 1 first then entry 0.
			 */
			ASSERT(dfp && dfp2);
			if (dfp == &d->hdr.bestfree[1]) {
				dfp = &d->hdr.bestfree[0];
				ASSERT(dfp2 == dfp);
				dfp2 = &d->hdr.bestfree[1];
			}
			xfs_dir2_data_freeremove(d, dfp2, needlogp);
			xfs_dir2_data_freeremove(d, dfp, needlogp);
			/*
			 * Now insert the new entry.
			 */
			dfp = xfs_dir2_data_freeinsert(d, prevdup, needlogp);
			ASSERT(dfp == &d->hdr.bestfree[0]);
			ASSERT(INT_GET(dfp->length, ARCH_CONVERT) == INT_GET(prevdup->length, ARCH_CONVERT));
			ASSERT(!dfp[1].length);
			ASSERT(!dfp[2].length);
		}
	}
	/*
	 * The entry before us is free, merge with it.
	 */
	else if (prevdup) {
		dfp = xfs_dir2_data_freefind(d, prevdup);
		INT_MOD(prevdup->length, ARCH_CONVERT, len);
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(prevdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)prevdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, prevdup);
		/*
		 * If the previous entry was in the table, the new entry
		 * is longer, so it will be in the table too.  Remove
		 * the old one and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(d, dfp, needlogp);
			(void)xfs_dir2_data_freeinsert(d, prevdup, needlogp);
		}
		/*
		 * Otherwise we need a scan if the new entry is big enough.
		 */
		else
			needscan = INT_GET(prevdup->length, ARCH_CONVERT) > INT_GET(d->hdr.bestfree[2].length, ARCH_CONVERT);
	}
	/*
	 * The following entry is free, merge with it.
	 */
	else if (postdup) {
		dfp = xfs_dir2_data_freefind(d, postdup);
		newdup = (xfs_dir2_data_unused_t *)((char *)d + offset);
		INT_SET(newdup->freetag, ARCH_CONVERT, XFS_DIR2_DATA_FREE_TAG);
		INT_SET(newdup->length, ARCH_CONVERT, len + INT_GET(postdup->length, ARCH_CONVERT));
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(newdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)newdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, newdup);
		/*
		 * If the following entry was in the table, the new entry
		 * is longer, so it will be in the table too.  Remove
		 * the old one and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(d, dfp, needlogp);
			(void)xfs_dir2_data_freeinsert(d, newdup, needlogp);
		}
		/*
		 * Otherwise we need a scan if the new entry is big enough.
		 */
		else
			needscan = INT_GET(newdup->length, ARCH_CONVERT) > INT_GET(d->hdr.bestfree[2].length, ARCH_CONVERT);
	}
	/*
	 * Neither neighbor is free.  Make a new entry.
	 */
	else {
		newdup = (xfs_dir2_data_unused_t *)((char *)d + offset);
		INT_SET(newdup->freetag, ARCH_CONVERT, XFS_DIR2_DATA_FREE_TAG);
		INT_SET(newdup->length, ARCH_CONVERT, len);
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(newdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)newdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, newdup);
		(void)xfs_dir2_data_freeinsert(d, newdup, needlogp);
	}
	*needscanp = needscan;
}

/*
 * Take a byte range out of an existing unused space and make it un-free.
 */
void
xfs_dir2_data_use_free(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp,		/* data block buffer */
	xfs_dir2_data_unused_t	*dup,		/* unused entry */
	xfs_dir2_data_aoff_t	offset,		/* starting offset to use */
	xfs_dir2_data_aoff_t	len,		/* length to use */
	int			*needlogp,	/* out: need to log header */
	int			*needscanp)	/* out: need regen bestfree */
{
	xfs_dir2_data_t		*d;		/* data block */
	xfs_dir2_data_free_t	*dfp;		/* bestfree pointer */
	int			matchback;	/* matches end of freespace */
	int			matchfront;	/* matches start of freespace */
	int			needscan;	/* need to regen bestfree */
	xfs_dir2_data_unused_t	*newdup;	/* new unused entry */
	xfs_dir2_data_unused_t	*newdup2;	/* another new unused entry */
	int			oldlen;		/* old unused entry's length */

	d = bp->data;
	ASSERT(INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_DATA_MAGIC ||
	       INT_GET(d->hdr.magic, ARCH_CONVERT) == XFS_DIR2_BLOCK_MAGIC);
	ASSERT(INT_GET(dup->freetag, ARCH_CONVERT) == XFS_DIR2_DATA_FREE_TAG);
	ASSERT(offset >= (char *)dup - (char *)d);
	ASSERT(offset + len <= (char *)dup + INT_GET(dup->length, ARCH_CONVERT) - (char *)d);
	ASSERT((char *)dup - (char *)d == INT_GET(*XFS_DIR2_DATA_UNUSED_TAG_P(dup), ARCH_CONVERT));
	/*
	 * Look up the entry in the bestfree table.
	 */
	dfp = xfs_dir2_data_freefind(d, dup);
	oldlen = INT_GET(dup->length, ARCH_CONVERT);
	ASSERT(dfp || oldlen <= INT_GET(d->hdr.bestfree[2].length, ARCH_CONVERT));
	/*
	 * Check for alignment with front and back of the entry.
	 */
	matchfront = (char *)dup - (char *)d == offset;
	matchback = (char *)dup + oldlen - (char *)d == offset + len;
	ASSERT(*needscanp == 0);
	needscan = 0;
	/*
	 * If we matched it exactly we just need to get rid of it from
	 * the bestfree table.
	 */
	if (matchfront && matchback) {
		if (dfp) {
			needscan = d->hdr.bestfree[2].offset;
			if (!needscan)
				xfs_dir2_data_freeremove(d, dfp, needlogp);
		}
	}
	/*
	 * We match the first part of the entry.
	 * Make a new entry with the remaining freespace.
	 */
	else if (matchfront) {
		newdup = (xfs_dir2_data_unused_t *)((char *)d + offset + len);
		INT_SET(newdup->freetag, ARCH_CONVERT, XFS_DIR2_DATA_FREE_TAG);
		INT_SET(newdup->length, ARCH_CONVERT, oldlen - len);
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(newdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)newdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, newdup);
		/*
		 * If it was in the table, remove it and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(d, dfp, needlogp);
			dfp = xfs_dir2_data_freeinsert(d, newdup, needlogp);
			ASSERT(dfp != NULL);
			ASSERT(INT_GET(dfp->length, ARCH_CONVERT) == INT_GET(newdup->length, ARCH_CONVERT));
			ASSERT(INT_GET(dfp->offset, ARCH_CONVERT) == (char *)newdup - (char *)d);
			/*
			 * If we got inserted at the last slot,
			 * that means we don't know if there was a better
			 * choice for the last slot, or not.  Rescan.
			 */
			needscan = dfp == &d->hdr.bestfree[2];
		}
	}
	/*
	 * We match the last part of the entry.
	 * Trim the allocated space off the tail of the entry.
	 */
	else if (matchback) {
		newdup = dup;
		INT_SET(newdup->length, ARCH_CONVERT, (xfs_dir2_data_off_t)
			(((char *)d + offset) - (char *)newdup));
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(newdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)newdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, newdup);
		/*
		 * If it was in the table, remove it and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(d, dfp, needlogp);
			dfp = xfs_dir2_data_freeinsert(d, newdup, needlogp);
			ASSERT(dfp != NULL);
			ASSERT(INT_GET(dfp->length, ARCH_CONVERT) == INT_GET(newdup->length, ARCH_CONVERT));
			ASSERT(INT_GET(dfp->offset, ARCH_CONVERT) == (char *)newdup - (char *)d);
			/*
			 * If we got inserted at the last slot,
			 * that means we don't know if there was a better
			 * choice for the last slot, or not.  Rescan.
			 */
			needscan = dfp == &d->hdr.bestfree[2];
		}
	}
	/*
	 * Poking out the middle of an entry.
	 * Make two new entries.
	 */
	else {
		newdup = dup;
		INT_SET(newdup->length, ARCH_CONVERT, (xfs_dir2_data_off_t)
			(((char *)d + offset) - (char *)newdup));
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(newdup), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)newdup - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, newdup);
		newdup2 = (xfs_dir2_data_unused_t *)((char *)d + offset + len);
		INT_SET(newdup2->freetag, ARCH_CONVERT, XFS_DIR2_DATA_FREE_TAG);
		INT_SET(newdup2->length, ARCH_CONVERT, oldlen - len - INT_GET(newdup->length, ARCH_CONVERT));
		INT_SET(*XFS_DIR2_DATA_UNUSED_TAG_P(newdup2), ARCH_CONVERT,
			(xfs_dir2_data_off_t)((char *)newdup2 - (char *)d));
		xfs_dir2_data_log_unused(tp, bp, newdup2);
		/*
		 * If the old entry was in the table, we need to scan
		 * if the 3rd entry was valid, since these entries
		 * are smaller than the old one.
		 * If we don't need to scan that means there were 1 or 2
		 * entries in the table, and removing the old and adding
		 * the 2 new will work.
		 */
		if (dfp) {
			needscan = d->hdr.bestfree[2].length;
			if (!needscan) {
				xfs_dir2_data_freeremove(d, dfp, needlogp);
				(void)xfs_dir2_data_freeinsert(d, newdup,
					needlogp);
				(void)xfs_dir2_data_freeinsert(d, newdup2,
					needlogp);
			}
		}
	}
	*needscanp = needscan;
}
