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
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_dir2_format.h"
#include "xfs_dir2_priv.h"
#include "xfs_error.h"

STATIC xfs_dir2_data_free_t *
xfs_dir2_data_freefind(xfs_dir2_data_hdr_t *hdr, xfs_dir2_data_unused_t *dup);

/*
 * Check the consistency of the data block.
 * The input can also be a block-format directory.
 * Return 0 is the buffer is good, otherwise an error.
 */
int
__xfs_dir2_data_check(
	struct xfs_inode	*dp,		/* incore inode pointer */
	struct xfs_buf		*bp)		/* data block's buffer */
{
	xfs_dir2_dataptr_t	addr;		/* addr for leaf lookup */
	xfs_dir2_data_free_t	*bf;		/* bestfree table */
	xfs_dir2_block_tail_t	*btp=NULL;	/* block tail */
	int			count;		/* count of entries found */
	xfs_dir2_data_hdr_t	*hdr;		/* data block header */
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
	struct xfs_name		name;

	mp = bp->b_target->bt_mount;
	hdr = bp->b_addr;
	bf = hdr->bestfree;
	p = (char *)(hdr + 1);

	switch (hdr->magic) {
	case cpu_to_be32(XFS_DIR2_BLOCK_MAGIC):
		btp = xfs_dir2_block_tail_p(mp, hdr);
		lep = xfs_dir2_block_leaf_p(btp);
		endp = (char *)lep;
		break;
	case cpu_to_be32(XFS_DIR2_DATA_MAGIC):
		endp = (char *)hdr + mp->m_dirblksize;
		break;
	default:
		XFS_ERROR_REPORT("Bad Magic", XFS_ERRLEVEL_LOW, mp);
		return EFSCORRUPTED;
	}

	count = lastfree = freeseen = 0;
	/*
	 * Account for zero bestfree entries.
	 */
	if (!bf[0].length) {
		XFS_WANT_CORRUPTED_RETURN(!bf[0].offset);
		freeseen |= 1 << 0;
	}
	if (!bf[1].length) {
		XFS_WANT_CORRUPTED_RETURN(!bf[1].offset);
		freeseen |= 1 << 1;
	}
	if (!bf[2].length) {
		XFS_WANT_CORRUPTED_RETURN(!bf[2].offset);
		freeseen |= 1 << 2;
	}

	XFS_WANT_CORRUPTED_RETURN(be16_to_cpu(bf[0].length) >=
						be16_to_cpu(bf[1].length));
	XFS_WANT_CORRUPTED_RETURN(be16_to_cpu(bf[1].length) >=
						be16_to_cpu(bf[2].length));
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
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			XFS_WANT_CORRUPTED_RETURN(lastfree == 0);
			XFS_WANT_CORRUPTED_RETURN(
				be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup)) ==
					       (char *)dup - (char *)hdr);
			dfp = xfs_dir2_data_freefind(hdr, dup);
			if (dfp) {
				i = (int)(dfp - bf);
				XFS_WANT_CORRUPTED_RETURN(
					(freeseen & (1 << i)) == 0);
				freeseen |= 1 << i;
			} else {
				XFS_WANT_CORRUPTED_RETURN(
					be16_to_cpu(dup->length) <=
						be16_to_cpu(bf[2].length));
			}
			p += be16_to_cpu(dup->length);
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
		XFS_WANT_CORRUPTED_RETURN(dep->namelen != 0);
		XFS_WANT_CORRUPTED_RETURN(
			!xfs_dir_ino_validate(mp, be64_to_cpu(dep->inumber)));
		XFS_WANT_CORRUPTED_RETURN(
			be16_to_cpu(*xfs_dir2_data_entry_tag_p(dep)) ==
					       (char *)dep - (char *)hdr);
		count++;
		lastfree = 0;
		if (hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC)) {
			addr = xfs_dir2_db_off_to_dataptr(mp, mp->m_dirdatablk,
				(xfs_dir2_data_aoff_t)
				((char *)dep - (char *)hdr));
			name.name = dep->name;
			name.len = dep->namelen;
			hash = mp->m_dirnameops->hashname(&name);
			for (i = 0; i < be32_to_cpu(btp->count); i++) {
				if (be32_to_cpu(lep[i].address) == addr &&
				    be32_to_cpu(lep[i].hashval) == hash)
					break;
			}
			XFS_WANT_CORRUPTED_RETURN(i < be32_to_cpu(btp->count));
		}
		p += xfs_dir2_data_entsize(dep->namelen);
	}
	/*
	 * Need to have seen all the entries and all the bestfree slots.
	 */
	XFS_WANT_CORRUPTED_RETURN(freeseen == 7);
	if (hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC)) {
		for (i = stale = 0; i < be32_to_cpu(btp->count); i++) {
			if (lep[i].address ==
			    cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
				stale++;
			if (i > 0)
				XFS_WANT_CORRUPTED_RETURN(
					be32_to_cpu(lep[i].hashval) >=
						be32_to_cpu(lep[i - 1].hashval));
		}
		XFS_WANT_CORRUPTED_RETURN(count ==
			be32_to_cpu(btp->count) - be32_to_cpu(btp->stale));
		XFS_WANT_CORRUPTED_RETURN(stale == be32_to_cpu(btp->stale));
	}
	return 0;
}

void
xfs_dir2_data_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_dir2_data_hdr *hdr = bp->b_addr;
	int			block_ok = 0;

	block_ok = hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC);
	block_ok = block_ok && __xfs_dir2_data_check(NULL, bp) == 0;

	if (!block_ok) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, hdr);
		xfs_buf_ioerror(bp, EFSCORRUPTED);
	}

	bp->b_iodone = NULL;
	xfs_buf_ioend(bp, 0);
}

int
xfs_dir2_data_read(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	xfs_dablk_t		bno,
	xfs_daddr_t		mapped_bno,
	struct xfs_buf		**bpp)
{
	return xfs_da_read_buf(tp, dp, bno, mapped_bno, bpp,
					XFS_DATA_FORK, xfs_dir2_data_verify);
}

int
xfs_dir2_data_readahead(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	xfs_dablk_t		bno,
	xfs_daddr_t		mapped_bno)
{
	return xfs_da_reada_buf(tp, dp, bno, mapped_bno,
					XFS_DATA_FORK, xfs_dir2_data_verify);
}

/*
 * Given a data block and an unused entry from that block,
 * return the bestfree entry if any that corresponds to it.
 */
STATIC xfs_dir2_data_free_t *
xfs_dir2_data_freefind(
	xfs_dir2_data_hdr_t	*hdr,		/* data block */
	xfs_dir2_data_unused_t	*dup)		/* data unused entry */
{
	xfs_dir2_data_free_t	*dfp;		/* bestfree entry */
	xfs_dir2_data_aoff_t	off;		/* offset value needed */
#if defined(DEBUG) && defined(__KERNEL__)
	int			matched;	/* matched the value */
	int			seenzero;	/* saw a 0 bestfree entry */
#endif

	off = (xfs_dir2_data_aoff_t)((char *)dup - (char *)hdr);
#if defined(DEBUG) && defined(__KERNEL__)
	/*
	 * Validate some consistency in the bestfree table.
	 * Check order, non-overlapping entries, and if we find the
	 * one we're looking for it has to be exact.
	 */
	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));
	for (dfp = &hdr->bestfree[0], seenzero = matched = 0;
	     dfp < &hdr->bestfree[XFS_DIR2_DATA_FD_COUNT];
	     dfp++) {
		if (!dfp->offset) {
			ASSERT(!dfp->length);
			seenzero = 1;
			continue;
		}
		ASSERT(seenzero == 0);
		if (be16_to_cpu(dfp->offset) == off) {
			matched = 1;
			ASSERT(dfp->length == dup->length);
		} else if (off < be16_to_cpu(dfp->offset))
			ASSERT(off + be16_to_cpu(dup->length) <= be16_to_cpu(dfp->offset));
		else
			ASSERT(be16_to_cpu(dfp->offset) + be16_to_cpu(dfp->length) <= off);
		ASSERT(matched || be16_to_cpu(dfp->length) >= be16_to_cpu(dup->length));
		if (dfp > &hdr->bestfree[0])
			ASSERT(be16_to_cpu(dfp[-1].length) >= be16_to_cpu(dfp[0].length));
	}
#endif
	/*
	 * If this is smaller than the smallest bestfree entry,
	 * it can't be there since they're sorted.
	 */
	if (be16_to_cpu(dup->length) <
	    be16_to_cpu(hdr->bestfree[XFS_DIR2_DATA_FD_COUNT - 1].length))
		return NULL;
	/*
	 * Look at the three bestfree entries for our guy.
	 */
	for (dfp = &hdr->bestfree[0];
	     dfp < &hdr->bestfree[XFS_DIR2_DATA_FD_COUNT];
	     dfp++) {
		if (!dfp->offset)
			return NULL;
		if (be16_to_cpu(dfp->offset) == off)
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
	xfs_dir2_data_hdr_t	*hdr,		/* data block pointer */
	xfs_dir2_data_unused_t	*dup,		/* unused space */
	int			*loghead)	/* log the data header (out) */
{
	xfs_dir2_data_free_t	*dfp;		/* bestfree table pointer */
	xfs_dir2_data_free_t	new;		/* new bestfree entry */

#ifdef __KERNEL__
	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));
#endif
	dfp = hdr->bestfree;
	new.length = dup->length;
	new.offset = cpu_to_be16((char *)dup - (char *)hdr);

	/*
	 * Insert at position 0, 1, or 2; or not at all.
	 */
	if (be16_to_cpu(new.length) > be16_to_cpu(dfp[0].length)) {
		dfp[2] = dfp[1];
		dfp[1] = dfp[0];
		dfp[0] = new;
		*loghead = 1;
		return &dfp[0];
	}
	if (be16_to_cpu(new.length) > be16_to_cpu(dfp[1].length)) {
		dfp[2] = dfp[1];
		dfp[1] = new;
		*loghead = 1;
		return &dfp[1];
	}
	if (be16_to_cpu(new.length) > be16_to_cpu(dfp[2].length)) {
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
	xfs_dir2_data_hdr_t	*hdr,		/* data block header */
	xfs_dir2_data_free_t	*dfp,		/* bestfree entry pointer */
	int			*loghead)	/* out: log data header */
{
#ifdef __KERNEL__
	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));
#endif
	/*
	 * It's the first entry, slide the next 2 up.
	 */
	if (dfp == &hdr->bestfree[0]) {
		hdr->bestfree[0] = hdr->bestfree[1];
		hdr->bestfree[1] = hdr->bestfree[2];
	}
	/*
	 * It's the second entry, slide the 3rd entry up.
	 */
	else if (dfp == &hdr->bestfree[1])
		hdr->bestfree[1] = hdr->bestfree[2];
	/*
	 * Must be the last entry.
	 */
	else
		ASSERT(dfp == &hdr->bestfree[2]);
	/*
	 * Clear the 3rd entry, must be zero now.
	 */
	hdr->bestfree[2].length = 0;
	hdr->bestfree[2].offset = 0;
	*loghead = 1;
}

/*
 * Given a data block, reconstruct its bestfree map.
 */
void
xfs_dir2_data_freescan(
	xfs_mount_t		*mp,		/* filesystem mount point */
	xfs_dir2_data_hdr_t	*hdr,		/* data block header */
	int			*loghead)	/* out: log data header */
{
	xfs_dir2_block_tail_t	*btp;		/* block tail */
	xfs_dir2_data_entry_t	*dep;		/* active data entry */
	xfs_dir2_data_unused_t	*dup;		/* unused data entry */
	char			*endp;		/* end of block's data */
	char			*p;		/* current entry pointer */

#ifdef __KERNEL__
	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));
#endif
	/*
	 * Start by clearing the table.
	 */
	memset(hdr->bestfree, 0, sizeof(hdr->bestfree));
	*loghead = 1;
	/*
	 * Set up pointers.
	 */
	p = (char *)(hdr + 1);
	if (hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC)) {
		btp = xfs_dir2_block_tail_p(mp, hdr);
		endp = (char *)xfs_dir2_block_leaf_p(btp);
	} else
		endp = (char *)hdr + mp->m_dirblksize;
	/*
	 * Loop over the block's entries.
	 */
	while (p < endp) {
		dup = (xfs_dir2_data_unused_t *)p;
		/*
		 * If it's a free entry, insert it.
		 */
		if (be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG) {
			ASSERT((char *)dup - (char *)hdr ==
			       be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup)));
			xfs_dir2_data_freeinsert(hdr, dup, loghead);
			p += be16_to_cpu(dup->length);
		}
		/*
		 * For active entries, check their tags and skip them.
		 */
		else {
			dep = (xfs_dir2_data_entry_t *)p;
			ASSERT((char *)dep - (char *)hdr ==
			       be16_to_cpu(*xfs_dir2_data_entry_tag_p(dep)));
			p += xfs_dir2_data_entsize(dep->namelen);
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
	struct xfs_buf		**bpp)		/* output block buffer */
{
	struct xfs_buf		*bp;		/* block buffer */
	xfs_dir2_data_hdr_t	*hdr;		/* data block header */
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
	error = xfs_da_get_buf(tp, dp, xfs_dir2_db_to_da(mp, blkno), -1, &bp,
		XFS_DATA_FORK);
	if (error) {
		return error;
	}
	ASSERT(bp != NULL);

	/*
	 * Initialize the header.
	 */
	hdr = bp->b_addr;
	hdr->magic = cpu_to_be32(XFS_DIR2_DATA_MAGIC);
	hdr->bestfree[0].offset = cpu_to_be16(sizeof(*hdr));
	for (i = 1; i < XFS_DIR2_DATA_FD_COUNT; i++) {
		hdr->bestfree[i].length = 0;
		hdr->bestfree[i].offset = 0;
	}

	/*
	 * Set up an unused entry for the block's body.
	 */
	dup = (xfs_dir2_data_unused_t *)(hdr + 1);
	dup->freetag = cpu_to_be16(XFS_DIR2_DATA_FREE_TAG);

	t = mp->m_dirblksize - (uint)sizeof(*hdr);
	hdr->bestfree[0].length = cpu_to_be16(t);
	dup->length = cpu_to_be16(t);
	*xfs_dir2_data_unused_tag_p(dup) = cpu_to_be16((char *)dup - (char *)hdr);
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
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	xfs_dir2_data_entry_t	*dep)		/* data entry pointer */
{
	xfs_dir2_data_hdr_t	*hdr = bp->b_addr;

	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));

	xfs_trans_log_buf(tp, bp, (uint)((char *)dep - (char *)hdr),
		(uint)((char *)(xfs_dir2_data_entry_tag_p(dep) + 1) -
		       (char *)hdr - 1));
}

/*
 * Log a data block header.
 */
void
xfs_dir2_data_log_header(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp)
{
	xfs_dir2_data_hdr_t	*hdr = bp->b_addr;

	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));

	xfs_trans_log_buf(tp, bp, 0, sizeof(*hdr) - 1);
}

/*
 * Log a data unused entry.
 */
void
xfs_dir2_data_log_unused(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	xfs_dir2_data_unused_t	*dup)		/* data unused pointer */
{
	xfs_dir2_data_hdr_t	*hdr = bp->b_addr;

	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));

	/*
	 * Log the first part of the unused entry.
	 */
	xfs_trans_log_buf(tp, bp, (uint)((char *)dup - (char *)hdr),
		(uint)((char *)&dup->length + sizeof(dup->length) -
		       1 - (char *)hdr));
	/*
	 * Log the end (tag) of the unused entry.
	 */
	xfs_trans_log_buf(tp, bp,
		(uint)((char *)xfs_dir2_data_unused_tag_p(dup) - (char *)hdr),
		(uint)((char *)xfs_dir2_data_unused_tag_p(dup) - (char *)hdr +
		       sizeof(xfs_dir2_data_off_t) - 1));
}

/*
 * Make a byte range in the data block unused.
 * Its current contents are unimportant.
 */
void
xfs_dir2_data_make_free(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	xfs_dir2_data_aoff_t	offset,		/* starting byte offset */
	xfs_dir2_data_aoff_t	len,		/* length in bytes */
	int			*needlogp,	/* out: log header */
	int			*needscanp)	/* out: regen bestfree */
{
	xfs_dir2_data_hdr_t	*hdr;		/* data block pointer */
	xfs_dir2_data_free_t	*dfp;		/* bestfree pointer */
	char			*endptr;	/* end of data area */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needscan;	/* need to regen bestfree */
	xfs_dir2_data_unused_t	*newdup;	/* new unused entry */
	xfs_dir2_data_unused_t	*postdup;	/* unused entry after us */
	xfs_dir2_data_unused_t	*prevdup;	/* unused entry before us */

	mp = tp->t_mountp;
	hdr = bp->b_addr;

	/*
	 * Figure out where the end of the data area is.
	 */
	if (hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC))
		endptr = (char *)hdr + mp->m_dirblksize;
	else {
		xfs_dir2_block_tail_t	*btp;	/* block tail */

		ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));
		btp = xfs_dir2_block_tail_p(mp, hdr);
		endptr = (char *)xfs_dir2_block_leaf_p(btp);
	}
	/*
	 * If this isn't the start of the block, then back up to
	 * the previous entry and see if it's free.
	 */
	if (offset > sizeof(*hdr)) {
		__be16			*tagp;	/* tag just before us */

		tagp = (__be16 *)((char *)hdr + offset) - 1;
		prevdup = (xfs_dir2_data_unused_t *)((char *)hdr + be16_to_cpu(*tagp));
		if (be16_to_cpu(prevdup->freetag) != XFS_DIR2_DATA_FREE_TAG)
			prevdup = NULL;
	} else
		prevdup = NULL;
	/*
	 * If this isn't the end of the block, see if the entry after
	 * us is free.
	 */
	if ((char *)hdr + offset + len < endptr) {
		postdup =
			(xfs_dir2_data_unused_t *)((char *)hdr + offset + len);
		if (be16_to_cpu(postdup->freetag) != XFS_DIR2_DATA_FREE_TAG)
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
		dfp = xfs_dir2_data_freefind(hdr, prevdup);
		dfp2 = xfs_dir2_data_freefind(hdr, postdup);
		/*
		 * We need a rescan unless there are exactly 2 free entries
		 * namely our two.  Then we know what's happening, otherwise
		 * since the third bestfree is there, there might be more
		 * entries.
		 */
		needscan = (hdr->bestfree[2].length != 0);
		/*
		 * Fix up the new big freespace.
		 */
		be16_add_cpu(&prevdup->length, len + be16_to_cpu(postdup->length));
		*xfs_dir2_data_unused_tag_p(prevdup) =
			cpu_to_be16((char *)prevdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, prevdup);
		if (!needscan) {
			/*
			 * Has to be the case that entries 0 and 1 are
			 * dfp and dfp2 (don't know which is which), and
			 * entry 2 is empty.
			 * Remove entry 1 first then entry 0.
			 */
			ASSERT(dfp && dfp2);
			if (dfp == &hdr->bestfree[1]) {
				dfp = &hdr->bestfree[0];
				ASSERT(dfp2 == dfp);
				dfp2 = &hdr->bestfree[1];
			}
			xfs_dir2_data_freeremove(hdr, dfp2, needlogp);
			xfs_dir2_data_freeremove(hdr, dfp, needlogp);
			/*
			 * Now insert the new entry.
			 */
			dfp = xfs_dir2_data_freeinsert(hdr, prevdup, needlogp);
			ASSERT(dfp == &hdr->bestfree[0]);
			ASSERT(dfp->length == prevdup->length);
			ASSERT(!dfp[1].length);
			ASSERT(!dfp[2].length);
		}
	}
	/*
	 * The entry before us is free, merge with it.
	 */
	else if (prevdup) {
		dfp = xfs_dir2_data_freefind(hdr, prevdup);
		be16_add_cpu(&prevdup->length, len);
		*xfs_dir2_data_unused_tag_p(prevdup) =
			cpu_to_be16((char *)prevdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, prevdup);
		/*
		 * If the previous entry was in the table, the new entry
		 * is longer, so it will be in the table too.  Remove
		 * the old one and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(hdr, dfp, needlogp);
			xfs_dir2_data_freeinsert(hdr, prevdup, needlogp);
		}
		/*
		 * Otherwise we need a scan if the new entry is big enough.
		 */
		else {
			needscan = be16_to_cpu(prevdup->length) >
				   be16_to_cpu(hdr->bestfree[2].length);
		}
	}
	/*
	 * The following entry is free, merge with it.
	 */
	else if (postdup) {
		dfp = xfs_dir2_data_freefind(hdr, postdup);
		newdup = (xfs_dir2_data_unused_t *)((char *)hdr + offset);
		newdup->freetag = cpu_to_be16(XFS_DIR2_DATA_FREE_TAG);
		newdup->length = cpu_to_be16(len + be16_to_cpu(postdup->length));
		*xfs_dir2_data_unused_tag_p(newdup) =
			cpu_to_be16((char *)newdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, newdup);
		/*
		 * If the following entry was in the table, the new entry
		 * is longer, so it will be in the table too.  Remove
		 * the old one and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(hdr, dfp, needlogp);
			xfs_dir2_data_freeinsert(hdr, newdup, needlogp);
		}
		/*
		 * Otherwise we need a scan if the new entry is big enough.
		 */
		else {
			needscan = be16_to_cpu(newdup->length) >
				   be16_to_cpu(hdr->bestfree[2].length);
		}
	}
	/*
	 * Neither neighbor is free.  Make a new entry.
	 */
	else {
		newdup = (xfs_dir2_data_unused_t *)((char *)hdr + offset);
		newdup->freetag = cpu_to_be16(XFS_DIR2_DATA_FREE_TAG);
		newdup->length = cpu_to_be16(len);
		*xfs_dir2_data_unused_tag_p(newdup) =
			cpu_to_be16((char *)newdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, newdup);
		xfs_dir2_data_freeinsert(hdr, newdup, needlogp);
	}
	*needscanp = needscan;
}

/*
 * Take a byte range out of an existing unused space and make it un-free.
 */
void
xfs_dir2_data_use_free(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	xfs_dir2_data_unused_t	*dup,		/* unused entry */
	xfs_dir2_data_aoff_t	offset,		/* starting offset to use */
	xfs_dir2_data_aoff_t	len,		/* length to use */
	int			*needlogp,	/* out: need to log header */
	int			*needscanp)	/* out: need regen bestfree */
{
	xfs_dir2_data_hdr_t	*hdr;		/* data block header */
	xfs_dir2_data_free_t	*dfp;		/* bestfree pointer */
	int			matchback;	/* matches end of freespace */
	int			matchfront;	/* matches start of freespace */
	int			needscan;	/* need to regen bestfree */
	xfs_dir2_data_unused_t	*newdup;	/* new unused entry */
	xfs_dir2_data_unused_t	*newdup2;	/* another new unused entry */
	int			oldlen;		/* old unused entry's length */

	hdr = bp->b_addr;
	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC));
	ASSERT(be16_to_cpu(dup->freetag) == XFS_DIR2_DATA_FREE_TAG);
	ASSERT(offset >= (char *)dup - (char *)hdr);
	ASSERT(offset + len <= (char *)dup + be16_to_cpu(dup->length) - (char *)hdr);
	ASSERT((char *)dup - (char *)hdr == be16_to_cpu(*xfs_dir2_data_unused_tag_p(dup)));
	/*
	 * Look up the entry in the bestfree table.
	 */
	dfp = xfs_dir2_data_freefind(hdr, dup);
	oldlen = be16_to_cpu(dup->length);
	ASSERT(dfp || oldlen <= be16_to_cpu(hdr->bestfree[2].length));
	/*
	 * Check for alignment with front and back of the entry.
	 */
	matchfront = (char *)dup - (char *)hdr == offset;
	matchback = (char *)dup + oldlen - (char *)hdr == offset + len;
	ASSERT(*needscanp == 0);
	needscan = 0;
	/*
	 * If we matched it exactly we just need to get rid of it from
	 * the bestfree table.
	 */
	if (matchfront && matchback) {
		if (dfp) {
			needscan = (hdr->bestfree[2].offset != 0);
			if (!needscan)
				xfs_dir2_data_freeremove(hdr, dfp, needlogp);
		}
	}
	/*
	 * We match the first part of the entry.
	 * Make a new entry with the remaining freespace.
	 */
	else if (matchfront) {
		newdup = (xfs_dir2_data_unused_t *)((char *)hdr + offset + len);
		newdup->freetag = cpu_to_be16(XFS_DIR2_DATA_FREE_TAG);
		newdup->length = cpu_to_be16(oldlen - len);
		*xfs_dir2_data_unused_tag_p(newdup) =
			cpu_to_be16((char *)newdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, newdup);
		/*
		 * If it was in the table, remove it and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(hdr, dfp, needlogp);
			dfp = xfs_dir2_data_freeinsert(hdr, newdup, needlogp);
			ASSERT(dfp != NULL);
			ASSERT(dfp->length == newdup->length);
			ASSERT(be16_to_cpu(dfp->offset) == (char *)newdup - (char *)hdr);
			/*
			 * If we got inserted at the last slot,
			 * that means we don't know if there was a better
			 * choice for the last slot, or not.  Rescan.
			 */
			needscan = dfp == &hdr->bestfree[2];
		}
	}
	/*
	 * We match the last part of the entry.
	 * Trim the allocated space off the tail of the entry.
	 */
	else if (matchback) {
		newdup = dup;
		newdup->length = cpu_to_be16(((char *)hdr + offset) - (char *)newdup);
		*xfs_dir2_data_unused_tag_p(newdup) =
			cpu_to_be16((char *)newdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, newdup);
		/*
		 * If it was in the table, remove it and add the new one.
		 */
		if (dfp) {
			xfs_dir2_data_freeremove(hdr, dfp, needlogp);
			dfp = xfs_dir2_data_freeinsert(hdr, newdup, needlogp);
			ASSERT(dfp != NULL);
			ASSERT(dfp->length == newdup->length);
			ASSERT(be16_to_cpu(dfp->offset) == (char *)newdup - (char *)hdr);
			/*
			 * If we got inserted at the last slot,
			 * that means we don't know if there was a better
			 * choice for the last slot, or not.  Rescan.
			 */
			needscan = dfp == &hdr->bestfree[2];
		}
	}
	/*
	 * Poking out the middle of an entry.
	 * Make two new entries.
	 */
	else {
		newdup = dup;
		newdup->length = cpu_to_be16(((char *)hdr + offset) - (char *)newdup);
		*xfs_dir2_data_unused_tag_p(newdup) =
			cpu_to_be16((char *)newdup - (char *)hdr);
		xfs_dir2_data_log_unused(tp, bp, newdup);
		newdup2 = (xfs_dir2_data_unused_t *)((char *)hdr + offset + len);
		newdup2->freetag = cpu_to_be16(XFS_DIR2_DATA_FREE_TAG);
		newdup2->length = cpu_to_be16(oldlen - len - be16_to_cpu(newdup->length));
		*xfs_dir2_data_unused_tag_p(newdup2) =
			cpu_to_be16((char *)newdup2 - (char *)hdr);
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
			needscan = (hdr->bestfree[2].length != 0);
			if (!needscan) {
				xfs_dir2_data_freeremove(hdr, dfp, needlogp);
				xfs_dir2_data_freeinsert(hdr, newdup, needlogp);
				xfs_dir2_data_freeinsert(hdr, newdup2,
							 needlogp);
			}
		}
	}
	*needscanp = needscan;
}
