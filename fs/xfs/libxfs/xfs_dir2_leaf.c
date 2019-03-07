// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_cksum.h"
#include "xfs_log.h"

/*
 * Local function declarations.
 */
static int xfs_dir2_leaf_lookup_int(xfs_da_args_t *args, struct xfs_buf **lbpp,
				    int *indexp, struct xfs_buf **dbpp);
static void xfs_dir3_leaf_log_bests(struct xfs_da_args *args,
				    struct xfs_buf *bp, int first, int last);
static void xfs_dir3_leaf_log_tail(struct xfs_da_args *args,
				   struct xfs_buf *bp);

/*
 * Check the internal consistency of a leaf1 block.
 * Pop an assert if something is wrong.
 */
#ifdef DEBUG
static xfs_failaddr_t
xfs_dir3_leaf1_check(
	struct xfs_inode	*dp,
	struct xfs_buf		*bp)
{
	struct xfs_dir2_leaf	*leaf = bp->b_addr;
	struct xfs_dir3_icleaf_hdr leafhdr;

	dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);

	if (leafhdr.magic == XFS_DIR3_LEAF1_MAGIC) {
		struct xfs_dir3_leaf_hdr *leaf3 = bp->b_addr;
		if (be64_to_cpu(leaf3->info.blkno) != bp->b_bn)
			return __this_address;
	} else if (leafhdr.magic != XFS_DIR2_LEAF1_MAGIC)
		return __this_address;

	return xfs_dir3_leaf_check_int(dp->i_mount, dp, &leafhdr, leaf);
}

static inline void
xfs_dir3_leaf_check(
	struct xfs_inode	*dp,
	struct xfs_buf		*bp)
{
	xfs_failaddr_t		fa;

	fa = xfs_dir3_leaf1_check(dp, bp);
	if (!fa)
		return;
	xfs_corruption_error(__func__, XFS_ERRLEVEL_LOW, dp->i_mount,
			bp->b_addr, BBTOB(bp->b_length), __FILE__, __LINE__,
			fa);
	ASSERT(0);
}
#else
#define	xfs_dir3_leaf_check(dp, bp)
#endif

xfs_failaddr_t
xfs_dir3_leaf_check_int(
	struct xfs_mount	*mp,
	struct xfs_inode	*dp,
	struct xfs_dir3_icleaf_hdr *hdr,
	struct xfs_dir2_leaf	*leaf)
{
	struct xfs_dir2_leaf_entry *ents;
	xfs_dir2_leaf_tail_t	*ltp;
	int			stale;
	int			i;
	const struct xfs_dir_ops *ops;
	struct xfs_dir3_icleaf_hdr leafhdr;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;

	/*
	 * we can be passed a null dp here from a verifier, so we need to go the
	 * hard way to get them.
	 */
	ops = xfs_dir_get_ops(mp, dp);

	if (!hdr) {
		ops->leaf_hdr_from_disk(&leafhdr, leaf);
		hdr = &leafhdr;
	}

	ents = ops->leaf_ents_p(leaf);
	ltp = xfs_dir2_leaf_tail_p(geo, leaf);

	/*
	 * XXX (dgc): This value is not restrictive enough.
	 * Should factor in the size of the bests table as well.
	 * We can deduce a value for that from di_size.
	 */
	if (hdr->count > ops->leaf_max_ents(geo))
		return __this_address;

	/* Leaves and bests don't overlap in leaf format. */
	if ((hdr->magic == XFS_DIR2_LEAF1_MAGIC ||
	     hdr->magic == XFS_DIR3_LEAF1_MAGIC) &&
	    (char *)&ents[hdr->count] > (char *)xfs_dir2_leaf_bests_p(ltp))
		return __this_address;

	/* Check hash value order, count stale entries.  */
	for (i = stale = 0; i < hdr->count; i++) {
		if (i + 1 < hdr->count) {
			if (be32_to_cpu(ents[i].hashval) >
					be32_to_cpu(ents[i + 1].hashval))
				return __this_address;
		}
		if (ents[i].address == cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
			stale++;
	}
	if (hdr->stale != stale)
		return __this_address;
	return NULL;
}

/*
 * We verify the magic numbers before decoding the leaf header so that on debug
 * kernels we don't get assertion failures in xfs_dir3_leaf_hdr_from_disk() due
 * to incorrect magic numbers.
 */
static xfs_failaddr_t
xfs_dir3_leaf_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_dir2_leaf	*leaf = bp->b_addr;
	xfs_failaddr_t		fa;

	fa = xfs_da3_blkinfo_verify(bp, bp->b_addr);
	if (fa)
		return fa;

	return xfs_dir3_leaf_check_int(mp, NULL, NULL, leaf);
}

static void
xfs_dir3_leaf_read_verify(
	struct xfs_buf  *bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	xfs_failaddr_t		fa;

	if (xfs_sb_version_hascrc(&mp->m_sb) &&
	     !xfs_buf_verify_cksum(bp, XFS_DIR3_LEAF_CRC_OFF))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_dir3_leaf_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}
}

static void
xfs_dir3_leaf_write_verify(
	struct xfs_buf  *bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	struct xfs_dir3_leaf_hdr *hdr3 = bp->b_addr;
	xfs_failaddr_t		fa;

	fa = xfs_dir3_leaf_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (bip)
		hdr3->info.lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_DIR3_LEAF_CRC_OFF);
}

const struct xfs_buf_ops xfs_dir3_leaf1_buf_ops = {
	.name = "xfs_dir3_leaf1",
	.magic16 = { cpu_to_be16(XFS_DIR2_LEAF1_MAGIC),
		     cpu_to_be16(XFS_DIR3_LEAF1_MAGIC) },
	.verify_read = xfs_dir3_leaf_read_verify,
	.verify_write = xfs_dir3_leaf_write_verify,
	.verify_struct = xfs_dir3_leaf_verify,
};

const struct xfs_buf_ops xfs_dir3_leafn_buf_ops = {
	.name = "xfs_dir3_leafn",
	.magic16 = { cpu_to_be16(XFS_DIR2_LEAFN_MAGIC),
		     cpu_to_be16(XFS_DIR3_LEAFN_MAGIC) },
	.verify_read = xfs_dir3_leaf_read_verify,
	.verify_write = xfs_dir3_leaf_write_verify,
	.verify_struct = xfs_dir3_leaf_verify,
};

int
xfs_dir3_leaf_read(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	xfs_dablk_t		fbno,
	xfs_daddr_t		mappedbno,
	struct xfs_buf		**bpp)
{
	int			err;

	err = xfs_da_read_buf(tp, dp, fbno, mappedbno, bpp,
				XFS_DATA_FORK, &xfs_dir3_leaf1_buf_ops);
	if (!err && tp && *bpp)
		xfs_trans_buf_set_type(tp, *bpp, XFS_BLFT_DIR_LEAF1_BUF);
	return err;
}

int
xfs_dir3_leafn_read(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	xfs_dablk_t		fbno,
	xfs_daddr_t		mappedbno,
	struct xfs_buf		**bpp)
{
	int			err;

	err = xfs_da_read_buf(tp, dp, fbno, mappedbno, bpp,
				XFS_DATA_FORK, &xfs_dir3_leafn_buf_ops);
	if (!err && tp && *bpp)
		xfs_trans_buf_set_type(tp, *bpp, XFS_BLFT_DIR_LEAFN_BUF);
	return err;
}

/*
 * Initialize a new leaf block, leaf1 or leafn magic accepted.
 */
static void
xfs_dir3_leaf_init(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	xfs_ino_t		owner,
	uint16_t		type)
{
	struct xfs_dir2_leaf	*leaf = bp->b_addr;

	ASSERT(type == XFS_DIR2_LEAF1_MAGIC || type == XFS_DIR2_LEAFN_MAGIC);

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		struct xfs_dir3_leaf_hdr *leaf3 = bp->b_addr;

		memset(leaf3, 0, sizeof(*leaf3));

		leaf3->info.hdr.magic = (type == XFS_DIR2_LEAF1_MAGIC)
					 ? cpu_to_be16(XFS_DIR3_LEAF1_MAGIC)
					 : cpu_to_be16(XFS_DIR3_LEAFN_MAGIC);
		leaf3->info.blkno = cpu_to_be64(bp->b_bn);
		leaf3->info.owner = cpu_to_be64(owner);
		uuid_copy(&leaf3->info.uuid, &mp->m_sb.sb_meta_uuid);
	} else {
		memset(leaf, 0, sizeof(*leaf));
		leaf->hdr.info.magic = cpu_to_be16(type);
	}

	/*
	 * If it's a leaf-format directory initialize the tail.
	 * Caller is responsible for initialising the bests table.
	 */
	if (type == XFS_DIR2_LEAF1_MAGIC) {
		struct xfs_dir2_leaf_tail *ltp;

		ltp = xfs_dir2_leaf_tail_p(mp->m_dir_geo, leaf);
		ltp->bestcount = 0;
		bp->b_ops = &xfs_dir3_leaf1_buf_ops;
		xfs_trans_buf_set_type(tp, bp, XFS_BLFT_DIR_LEAF1_BUF);
	} else {
		bp->b_ops = &xfs_dir3_leafn_buf_ops;
		xfs_trans_buf_set_type(tp, bp, XFS_BLFT_DIR_LEAFN_BUF);
	}
}

int
xfs_dir3_leaf_get_buf(
	xfs_da_args_t		*args,
	xfs_dir2_db_t		bno,
	struct xfs_buf		**bpp,
	uint16_t		magic)
{
	struct xfs_inode	*dp = args->dp;
	struct xfs_trans	*tp = args->trans;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_buf		*bp;
	int			error;

	ASSERT(magic == XFS_DIR2_LEAF1_MAGIC || magic == XFS_DIR2_LEAFN_MAGIC);
	ASSERT(bno >= xfs_dir2_byte_to_db(args->geo, XFS_DIR2_LEAF_OFFSET) &&
	       bno < xfs_dir2_byte_to_db(args->geo, XFS_DIR2_FREE_OFFSET));

	error = xfs_da_get_buf(tp, dp, xfs_dir2_db_to_da(args->geo, bno),
			       -1, &bp, XFS_DATA_FORK);
	if (error)
		return error;

	xfs_dir3_leaf_init(mp, tp, bp, dp->i_ino, magic);
	xfs_dir3_leaf_log_header(args, bp);
	if (magic == XFS_DIR2_LEAF1_MAGIC)
		xfs_dir3_leaf_log_tail(args, bp);
	*bpp = bp;
	return 0;
}

/*
 * Convert a block form directory to a leaf form directory.
 */
int						/* error */
xfs_dir2_block_to_leaf(
	xfs_da_args_t		*args,		/* operation arguments */
	struct xfs_buf		*dbp)		/* input block's buffer */
{
	__be16			*bestsp;	/* leaf's bestsp entries */
	xfs_dablk_t		blkno;		/* leaf block's bno */
	xfs_dir2_data_hdr_t	*hdr;		/* block header */
	xfs_dir2_leaf_entry_t	*blp;		/* block's leaf entries */
	xfs_dir2_block_tail_t	*btp;		/* block's tail */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	struct xfs_buf		*lbp;		/* leaf block's buffer */
	xfs_dir2_db_t		ldb;		/* leaf block's bno */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf's tail */
	int			needlog;	/* need to log block header */
	int			needscan;	/* need to rescan bestfree */
	xfs_trans_t		*tp;		/* transaction pointer */
	struct xfs_dir2_data_free *bf;
	struct xfs_dir2_leaf_entry *ents;
	struct xfs_dir3_icleaf_hdr leafhdr;

	trace_xfs_dir2_block_to_leaf(args);

	dp = args->dp;
	tp = args->trans;
	/*
	 * Add the leaf block to the inode.
	 * This interface will only put blocks in the leaf/node range.
	 * Since that's empty now, we'll get the root (block 0 in range).
	 */
	if ((error = xfs_da_grow_inode(args, &blkno))) {
		return error;
	}
	ldb = xfs_dir2_da_to_db(args->geo, blkno);
	ASSERT(ldb == xfs_dir2_byte_to_db(args->geo, XFS_DIR2_LEAF_OFFSET));
	/*
	 * Initialize the leaf block, get a buffer for it.
	 */
	error = xfs_dir3_leaf_get_buf(args, ldb, &lbp, XFS_DIR2_LEAF1_MAGIC);
	if (error)
		return error;

	leaf = lbp->b_addr;
	hdr = dbp->b_addr;
	xfs_dir3_data_check(dp, dbp);
	btp = xfs_dir2_block_tail_p(args->geo, hdr);
	blp = xfs_dir2_block_leaf_p(btp);
	bf = dp->d_ops->data_bestfree_p(hdr);
	ents = dp->d_ops->leaf_ents_p(leaf);

	/*
	 * Set the counts in the leaf header.
	 */
	dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);
	leafhdr.count = be32_to_cpu(btp->count);
	leafhdr.stale = be32_to_cpu(btp->stale);
	dp->d_ops->leaf_hdr_to_disk(leaf, &leafhdr);
	xfs_dir3_leaf_log_header(args, lbp);

	/*
	 * Could compact these but I think we always do the conversion
	 * after squeezing out stale entries.
	 */
	memcpy(ents, blp, be32_to_cpu(btp->count) * sizeof(xfs_dir2_leaf_entry_t));
	xfs_dir3_leaf_log_ents(args, lbp, 0, leafhdr.count - 1);
	needscan = 0;
	needlog = 1;
	/*
	 * Make the space formerly occupied by the leaf entries and block
	 * tail be free.
	 */
	xfs_dir2_data_make_free(args, dbp,
		(xfs_dir2_data_aoff_t)((char *)blp - (char *)hdr),
		(xfs_dir2_data_aoff_t)((char *)hdr + args->geo->blksize -
				       (char *)blp),
		&needlog, &needscan);
	/*
	 * Fix up the block header, make it a data block.
	 */
	dbp->b_ops = &xfs_dir3_data_buf_ops;
	xfs_trans_buf_set_type(tp, dbp, XFS_BLFT_DIR_DATA_BUF);
	if (hdr->magic == cpu_to_be32(XFS_DIR2_BLOCK_MAGIC))
		hdr->magic = cpu_to_be32(XFS_DIR2_DATA_MAGIC);
	else
		hdr->magic = cpu_to_be32(XFS_DIR3_DATA_MAGIC);

	if (needscan)
		xfs_dir2_data_freescan(dp, hdr, &needlog);
	/*
	 * Set up leaf tail and bests table.
	 */
	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);
	ltp->bestcount = cpu_to_be32(1);
	bestsp = xfs_dir2_leaf_bests_p(ltp);
	bestsp[0] =  bf[0].length;
	/*
	 * Log the data header and leaf bests table.
	 */
	if (needlog)
		xfs_dir2_data_log_header(args, dbp);
	xfs_dir3_leaf_check(dp, lbp);
	xfs_dir3_data_check(dp, dbp);
	xfs_dir3_leaf_log_bests(args, lbp, 0, 0);
	return 0;
}

STATIC void
xfs_dir3_leaf_find_stale(
	struct xfs_dir3_icleaf_hdr *leafhdr,
	struct xfs_dir2_leaf_entry *ents,
	int			index,
	int			*lowstale,
	int			*highstale)
{
	/*
	 * Find the first stale entry before our index, if any.
	 */
	for (*lowstale = index - 1; *lowstale >= 0; --*lowstale) {
		if (ents[*lowstale].address ==
		    cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
			break;
	}

	/*
	 * Find the first stale entry at or after our index, if any.
	 * Stop if the result would require moving more entries than using
	 * lowstale.
	 */
	for (*highstale = index; *highstale < leafhdr->count; ++*highstale) {
		if (ents[*highstale].address ==
		    cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
			break;
		if (*lowstale >= 0 && index - *lowstale <= *highstale - index)
			break;
	}
}

struct xfs_dir2_leaf_entry *
xfs_dir3_leaf_find_entry(
	struct xfs_dir3_icleaf_hdr *leafhdr,
	struct xfs_dir2_leaf_entry *ents,
	int			index,		/* leaf table position */
	int			compact,	/* need to compact leaves */
	int			lowstale,	/* index of prev stale leaf */
	int			highstale,	/* index of next stale leaf */
	int			*lfloglow,	/* low leaf logging index */
	int			*lfloghigh)	/* high leaf logging index */
{
	if (!leafhdr->stale) {
		xfs_dir2_leaf_entry_t	*lep;	/* leaf entry table pointer */

		/*
		 * Now we need to make room to insert the leaf entry.
		 *
		 * If there are no stale entries, just insert a hole at index.
		 */
		lep = &ents[index];
		if (index < leafhdr->count)
			memmove(lep + 1, lep,
				(leafhdr->count - index) * sizeof(*lep));

		/*
		 * Record low and high logging indices for the leaf.
		 */
		*lfloglow = index;
		*lfloghigh = leafhdr->count++;
		return lep;
	}

	/*
	 * There are stale entries.
	 *
	 * We will use one of them for the new entry.  It's probably not at
	 * the right location, so we'll have to shift some up or down first.
	 *
	 * If we didn't compact before, we need to find the nearest stale
	 * entries before and after our insertion point.
	 */
	if (compact == 0)
		xfs_dir3_leaf_find_stale(leafhdr, ents, index,
					 &lowstale, &highstale);

	/*
	 * If the low one is better, use it.
	 */
	if (lowstale >= 0 &&
	    (highstale == leafhdr->count ||
	     index - lowstale - 1 < highstale - index)) {
		ASSERT(index - lowstale - 1 >= 0);
		ASSERT(ents[lowstale].address ==
		       cpu_to_be32(XFS_DIR2_NULL_DATAPTR));

		/*
		 * Copy entries up to cover the stale entry and make room
		 * for the new entry.
		 */
		if (index - lowstale - 1 > 0) {
			memmove(&ents[lowstale], &ents[lowstale + 1],
				(index - lowstale - 1) *
					sizeof(xfs_dir2_leaf_entry_t));
		}
		*lfloglow = min(lowstale, *lfloglow);
		*lfloghigh = max(index - 1, *lfloghigh);
		leafhdr->stale--;
		return &ents[index - 1];
	}

	/*
	 * The high one is better, so use that one.
	 */
	ASSERT(highstale - index >= 0);
	ASSERT(ents[highstale].address == cpu_to_be32(XFS_DIR2_NULL_DATAPTR));

	/*
	 * Copy entries down to cover the stale entry and make room for the
	 * new entry.
	 */
	if (highstale - index > 0) {
		memmove(&ents[index + 1], &ents[index],
			(highstale - index) * sizeof(xfs_dir2_leaf_entry_t));
	}
	*lfloglow = min(index, *lfloglow);
	*lfloghigh = max(highstale, *lfloghigh);
	leafhdr->stale--;
	return &ents[index];
}

/*
 * Add an entry to a leaf form directory.
 */
int						/* error */
xfs_dir2_leaf_addname(
	xfs_da_args_t		*args)		/* operation arguments */
{
	__be16			*bestsp;	/* freespace table in leaf */
	int			compact;	/* need to compact leaves */
	xfs_dir2_data_hdr_t	*hdr;		/* data block header */
	struct xfs_buf		*dbp;		/* data block buffer */
	xfs_dir2_data_entry_t	*dep;		/* data block entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_data_unused_t	*dup;		/* data unused entry */
	int			error;		/* error return value */
	int			grown;		/* allocated new data block */
	int			highstale;	/* index of next stale leaf */
	int			i;		/* temporary, index */
	int			index;		/* leaf table position */
	struct xfs_buf		*lbp;		/* leaf's buffer */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	int			length;		/* length of new entry */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry table pointer */
	int			lfloglow;	/* low leaf logging index */
	int			lfloghigh;	/* high leaf logging index */
	int			lowstale;	/* index of prev stale leaf */
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail pointer */
	int			needbytes;	/* leaf block bytes needed */
	int			needlog;	/* need to log data header */
	int			needscan;	/* need to rescan data free */
	__be16			*tagp;		/* end of data entry */
	xfs_trans_t		*tp;		/* transaction pointer */
	xfs_dir2_db_t		use_block;	/* data block number */
	struct xfs_dir2_data_free *bf;		/* bestfree table */
	struct xfs_dir2_leaf_entry *ents;
	struct xfs_dir3_icleaf_hdr leafhdr;

	trace_xfs_dir2_leaf_addname(args);

	dp = args->dp;
	tp = args->trans;

	error = xfs_dir3_leaf_read(tp, dp, args->geo->leafblk, -1, &lbp);
	if (error)
		return error;

	/*
	 * Look up the entry by hash value and name.
	 * We know it's not there, our caller has already done a lookup.
	 * So the index is of the entry to insert in front of.
	 * But if there are dup hash values the index is of the first of those.
	 */
	index = xfs_dir2_leaf_search_hash(args, lbp);
	leaf = lbp->b_addr;
	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);
	ents = dp->d_ops->leaf_ents_p(leaf);
	dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);
	bestsp = xfs_dir2_leaf_bests_p(ltp);
	length = dp->d_ops->data_entsize(args->namelen);

	/*
	 * See if there are any entries with the same hash value
	 * and space in their block for the new entry.
	 * This is good because it puts multiple same-hash value entries
	 * in a data block, improving the lookup of those entries.
	 */
	for (use_block = -1, lep = &ents[index];
	     index < leafhdr.count && be32_to_cpu(lep->hashval) == args->hashval;
	     index++, lep++) {
		if (be32_to_cpu(lep->address) == XFS_DIR2_NULL_DATAPTR)
			continue;
		i = xfs_dir2_dataptr_to_db(args->geo, be32_to_cpu(lep->address));
		ASSERT(i < be32_to_cpu(ltp->bestcount));
		ASSERT(bestsp[i] != cpu_to_be16(NULLDATAOFF));
		if (be16_to_cpu(bestsp[i]) >= length) {
			use_block = i;
			break;
		}
	}
	/*
	 * Didn't find a block yet, linear search all the data blocks.
	 */
	if (use_block == -1) {
		for (i = 0; i < be32_to_cpu(ltp->bestcount); i++) {
			/*
			 * Remember a block we see that's missing.
			 */
			if (bestsp[i] == cpu_to_be16(NULLDATAOFF) &&
			    use_block == -1)
				use_block = i;
			else if (be16_to_cpu(bestsp[i]) >= length) {
				use_block = i;
				break;
			}
		}
	}
	/*
	 * How many bytes do we need in the leaf block?
	 */
	needbytes = 0;
	if (!leafhdr.stale)
		needbytes += sizeof(xfs_dir2_leaf_entry_t);
	if (use_block == -1)
		needbytes += sizeof(xfs_dir2_data_off_t);

	/*
	 * Now kill use_block if it refers to a missing block, so we
	 * can use it as an indication of allocation needed.
	 */
	if (use_block != -1 && bestsp[use_block] == cpu_to_be16(NULLDATAOFF))
		use_block = -1;
	/*
	 * If we don't have enough free bytes but we can make enough
	 * by compacting out stale entries, we'll do that.
	 */
	if ((char *)bestsp - (char *)&ents[leafhdr.count] < needbytes &&
	    leafhdr.stale > 1)
		compact = 1;

	/*
	 * Otherwise if we don't have enough free bytes we need to
	 * convert to node form.
	 */
	else if ((char *)bestsp - (char *)&ents[leafhdr.count] < needbytes) {
		/*
		 * Just checking or no space reservation, give up.
		 */
		if ((args->op_flags & XFS_DA_OP_JUSTCHECK) ||
							args->total == 0) {
			xfs_trans_brelse(tp, lbp);
			return -ENOSPC;
		}
		/*
		 * Convert to node form.
		 */
		error = xfs_dir2_leaf_to_node(args, lbp);
		if (error)
			return error;
		/*
		 * Then add the new entry.
		 */
		return xfs_dir2_node_addname(args);
	}
	/*
	 * Otherwise it will fit without compaction.
	 */
	else
		compact = 0;
	/*
	 * If just checking, then it will fit unless we needed to allocate
	 * a new data block.
	 */
	if (args->op_flags & XFS_DA_OP_JUSTCHECK) {
		xfs_trans_brelse(tp, lbp);
		return use_block == -1 ? -ENOSPC : 0;
	}
	/*
	 * If no allocations are allowed, return now before we've
	 * changed anything.
	 */
	if (args->total == 0 && use_block == -1) {
		xfs_trans_brelse(tp, lbp);
		return -ENOSPC;
	}
	/*
	 * Need to compact the leaf entries, removing stale ones.
	 * Leave one stale entry behind - the one closest to our
	 * insertion index - and we'll shift that one to our insertion
	 * point later.
	 */
	if (compact) {
		xfs_dir3_leaf_compact_x1(&leafhdr, ents, &index, &lowstale,
			&highstale, &lfloglow, &lfloghigh);
	}
	/*
	 * There are stale entries, so we'll need log-low and log-high
	 * impossibly bad values later.
	 */
	else if (leafhdr.stale) {
		lfloglow = leafhdr.count;
		lfloghigh = -1;
	}
	/*
	 * If there was no data block space found, we need to allocate
	 * a new one.
	 */
	if (use_block == -1) {
		/*
		 * Add the new data block.
		 */
		if ((error = xfs_dir2_grow_inode(args, XFS_DIR2_DATA_SPACE,
				&use_block))) {
			xfs_trans_brelse(tp, lbp);
			return error;
		}
		/*
		 * Initialize the block.
		 */
		if ((error = xfs_dir3_data_init(args, use_block, &dbp))) {
			xfs_trans_brelse(tp, lbp);
			return error;
		}
		/*
		 * If we're adding a new data block on the end we need to
		 * extend the bests table.  Copy it up one entry.
		 */
		if (use_block >= be32_to_cpu(ltp->bestcount)) {
			bestsp--;
			memmove(&bestsp[0], &bestsp[1],
				be32_to_cpu(ltp->bestcount) * sizeof(bestsp[0]));
			be32_add_cpu(&ltp->bestcount, 1);
			xfs_dir3_leaf_log_tail(args, lbp);
			xfs_dir3_leaf_log_bests(args, lbp, 0,
						be32_to_cpu(ltp->bestcount) - 1);
		}
		/*
		 * If we're filling in a previously empty block just log it.
		 */
		else
			xfs_dir3_leaf_log_bests(args, lbp, use_block, use_block);
		hdr = dbp->b_addr;
		bf = dp->d_ops->data_bestfree_p(hdr);
		bestsp[use_block] = bf[0].length;
		grown = 1;
	} else {
		/*
		 * Already had space in some data block.
		 * Just read that one in.
		 */
		error = xfs_dir3_data_read(tp, dp,
				   xfs_dir2_db_to_da(args->geo, use_block),
				   -1, &dbp);
		if (error) {
			xfs_trans_brelse(tp, lbp);
			return error;
		}
		hdr = dbp->b_addr;
		bf = dp->d_ops->data_bestfree_p(hdr);
		grown = 0;
	}
	/*
	 * Point to the biggest freespace in our data block.
	 */
	dup = (xfs_dir2_data_unused_t *)
	      ((char *)hdr + be16_to_cpu(bf[0].offset));
	needscan = needlog = 0;
	/*
	 * Mark the initial part of our freespace in use for the new entry.
	 */
	error = xfs_dir2_data_use_free(args, dbp, dup,
			(xfs_dir2_data_aoff_t)((char *)dup - (char *)hdr),
			length, &needlog, &needscan);
	if (error) {
		xfs_trans_brelse(tp, lbp);
		return error;
	}
	/*
	 * Initialize our new entry (at last).
	 */
	dep = (xfs_dir2_data_entry_t *)dup;
	dep->inumber = cpu_to_be64(args->inumber);
	dep->namelen = args->namelen;
	memcpy(dep->name, args->name, dep->namelen);
	dp->d_ops->data_put_ftype(dep, args->filetype);
	tagp = dp->d_ops->data_entry_tag_p(dep);
	*tagp = cpu_to_be16((char *)dep - (char *)hdr);
	/*
	 * Need to scan fix up the bestfree table.
	 */
	if (needscan)
		xfs_dir2_data_freescan(dp, hdr, &needlog);
	/*
	 * Need to log the data block's header.
	 */
	if (needlog)
		xfs_dir2_data_log_header(args, dbp);
	xfs_dir2_data_log_entry(args, dbp, dep);
	/*
	 * If the bests table needs to be changed, do it.
	 * Log the change unless we've already done that.
	 */
	if (be16_to_cpu(bestsp[use_block]) != be16_to_cpu(bf[0].length)) {
		bestsp[use_block] = bf[0].length;
		if (!grown)
			xfs_dir3_leaf_log_bests(args, lbp, use_block, use_block);
	}

	lep = xfs_dir3_leaf_find_entry(&leafhdr, ents, index, compact, lowstale,
				       highstale, &lfloglow, &lfloghigh);

	/*
	 * Fill in the new leaf entry.
	 */
	lep->hashval = cpu_to_be32(args->hashval);
	lep->address = cpu_to_be32(
				xfs_dir2_db_off_to_dataptr(args->geo, use_block,
				be16_to_cpu(*tagp)));
	/*
	 * Log the leaf fields and give up the buffers.
	 */
	dp->d_ops->leaf_hdr_to_disk(leaf, &leafhdr);
	xfs_dir3_leaf_log_header(args, lbp);
	xfs_dir3_leaf_log_ents(args, lbp, lfloglow, lfloghigh);
	xfs_dir3_leaf_check(dp, lbp);
	xfs_dir3_data_check(dp, dbp);
	return 0;
}

/*
 * Compact out any stale entries in the leaf.
 * Log the header and changed leaf entries, if any.
 */
void
xfs_dir3_leaf_compact(
	xfs_da_args_t	*args,		/* operation arguments */
	struct xfs_dir3_icleaf_hdr *leafhdr,
	struct xfs_buf	*bp)		/* leaf buffer */
{
	int		from;		/* source leaf index */
	xfs_dir2_leaf_t	*leaf;		/* leaf structure */
	int		loglow;		/* first leaf entry to log */
	int		to;		/* target leaf index */
	struct xfs_dir2_leaf_entry *ents;
	struct xfs_inode *dp = args->dp;

	leaf = bp->b_addr;
	if (!leafhdr->stale)
		return;

	/*
	 * Compress out the stale entries in place.
	 */
	ents = dp->d_ops->leaf_ents_p(leaf);
	for (from = to = 0, loglow = -1; from < leafhdr->count; from++) {
		if (ents[from].address == cpu_to_be32(XFS_DIR2_NULL_DATAPTR))
			continue;
		/*
		 * Only actually copy the entries that are different.
		 */
		if (from > to) {
			if (loglow == -1)
				loglow = to;
			ents[to] = ents[from];
		}
		to++;
	}
	/*
	 * Update and log the header, log the leaf entries.
	 */
	ASSERT(leafhdr->stale == from - to);
	leafhdr->count -= leafhdr->stale;
	leafhdr->stale = 0;

	dp->d_ops->leaf_hdr_to_disk(leaf, leafhdr);
	xfs_dir3_leaf_log_header(args, bp);
	if (loglow != -1)
		xfs_dir3_leaf_log_ents(args, bp, loglow, to - 1);
}

/*
 * Compact the leaf entries, removing stale ones.
 * Leave one stale entry behind - the one closest to our
 * insertion index - and the caller will shift that one to our insertion
 * point later.
 * Return new insertion index, where the remaining stale entry is,
 * and leaf logging indices.
 */
void
xfs_dir3_leaf_compact_x1(
	struct xfs_dir3_icleaf_hdr *leafhdr,
	struct xfs_dir2_leaf_entry *ents,
	int		*indexp,	/* insertion index */
	int		*lowstalep,	/* out: stale entry before us */
	int		*highstalep,	/* out: stale entry after us */
	int		*lowlogp,	/* out: low log index */
	int		*highlogp)	/* out: high log index */
{
	int		from;		/* source copy index */
	int		highstale;	/* stale entry at/after index */
	int		index;		/* insertion index */
	int		keepstale;	/* source index of kept stale */
	int		lowstale;	/* stale entry before index */
	int		newindex=0;	/* new insertion index */
	int		to;		/* destination copy index */

	ASSERT(leafhdr->stale > 1);
	index = *indexp;

	xfs_dir3_leaf_find_stale(leafhdr, ents, index, &lowstale, &highstale);

	/*
	 * Pick the better of lowstale and highstale.
	 */
	if (lowstale >= 0 &&
	    (highstale == leafhdr->count ||
	     index - lowstale <= highstale - index))
		keepstale = lowstale;
	else
		keepstale = highstale;
	/*
	 * Copy the entries in place, removing all the stale entries
	 * except keepstale.
	 */
	for (from = to = 0; from < leafhdr->count; from++) {
		/*
		 * Notice the new value of index.
		 */
		if (index == from)
			newindex = to;
		if (from != keepstale &&
		    ents[from].address == cpu_to_be32(XFS_DIR2_NULL_DATAPTR)) {
			if (from == to)
				*lowlogp = to;
			continue;
		}
		/*
		 * Record the new keepstale value for the insertion.
		 */
		if (from == keepstale)
			lowstale = highstale = to;
		/*
		 * Copy only the entries that have moved.
		 */
		if (from > to)
			ents[to] = ents[from];
		to++;
	}
	ASSERT(from > to);
	/*
	 * If the insertion point was past the last entry,
	 * set the new insertion point accordingly.
	 */
	if (index == from)
		newindex = to;
	*indexp = newindex;
	/*
	 * Adjust the leaf header values.
	 */
	leafhdr->count -= from - to;
	leafhdr->stale = 1;
	/*
	 * Remember the low/high stale value only in the "right"
	 * direction.
	 */
	if (lowstale >= newindex)
		lowstale = -1;
	else
		highstale = leafhdr->count;
	*highlogp = leafhdr->count - 1;
	*lowstalep = lowstale;
	*highstalep = highstale;
}

/*
 * Log the bests entries indicated from a leaf1 block.
 */
static void
xfs_dir3_leaf_log_bests(
	struct xfs_da_args	*args,
	struct xfs_buf		*bp,		/* leaf buffer */
	int			first,		/* first entry to log */
	int			last)		/* last entry to log */
{
	__be16			*firstb;	/* pointer to first entry */
	__be16			*lastb;		/* pointer to last entry */
	struct xfs_dir2_leaf	*leaf = bp->b_addr;
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail structure */

	ASSERT(leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAF1_MAGIC));

	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);
	firstb = xfs_dir2_leaf_bests_p(ltp) + first;
	lastb = xfs_dir2_leaf_bests_p(ltp) + last;
	xfs_trans_log_buf(args->trans, bp,
		(uint)((char *)firstb - (char *)leaf),
		(uint)((char *)lastb - (char *)leaf + sizeof(*lastb) - 1));
}

/*
 * Log the leaf entries indicated from a leaf1 or leafn block.
 */
void
xfs_dir3_leaf_log_ents(
	struct xfs_da_args	*args,
	struct xfs_buf		*bp,
	int			first,
	int			last)
{
	xfs_dir2_leaf_entry_t	*firstlep;	/* pointer to first entry */
	xfs_dir2_leaf_entry_t	*lastlep;	/* pointer to last entry */
	struct xfs_dir2_leaf	*leaf = bp->b_addr;
	struct xfs_dir2_leaf_entry *ents;

	ASSERT(leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAFN_MAGIC));

	ents = args->dp->d_ops->leaf_ents_p(leaf);
	firstlep = &ents[first];
	lastlep = &ents[last];
	xfs_trans_log_buf(args->trans, bp,
		(uint)((char *)firstlep - (char *)leaf),
		(uint)((char *)lastlep - (char *)leaf + sizeof(*lastlep) - 1));
}

/*
 * Log the header of the leaf1 or leafn block.
 */
void
xfs_dir3_leaf_log_header(
	struct xfs_da_args	*args,
	struct xfs_buf		*bp)
{
	struct xfs_dir2_leaf	*leaf = bp->b_addr;

	ASSERT(leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAFN_MAGIC));

	xfs_trans_log_buf(args->trans, bp,
			  (uint)((char *)&leaf->hdr - (char *)leaf),
			  args->dp->d_ops->leaf_hdr_size - 1);
}

/*
 * Log the tail of the leaf1 block.
 */
STATIC void
xfs_dir3_leaf_log_tail(
	struct xfs_da_args	*args,
	struct xfs_buf		*bp)
{
	struct xfs_dir2_leaf	*leaf = bp->b_addr;
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail structure */

	ASSERT(leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAF1_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC) ||
	       leaf->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAFN_MAGIC));

	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);
	xfs_trans_log_buf(args->trans, bp, (uint)((char *)ltp - (char *)leaf),
		(uint)(args->geo->blksize - 1));
}

/*
 * Look up the entry referred to by args in the leaf format directory.
 * Most of the work is done by the xfs_dir2_leaf_lookup_int routine which
 * is also used by the node-format code.
 */
int
xfs_dir2_leaf_lookup(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_buf		*dbp;		/* data block buffer */
	xfs_dir2_data_entry_t	*dep;		/* data block entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	int			index;		/* found entry index */
	struct xfs_buf		*lbp;		/* leaf buffer */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_trans_t		*tp;		/* transaction pointer */
	struct xfs_dir2_leaf_entry *ents;

	trace_xfs_dir2_leaf_lookup(args);

	/*
	 * Look up name in the leaf block, returning both buffers and index.
	 */
	if ((error = xfs_dir2_leaf_lookup_int(args, &lbp, &index, &dbp))) {
		return error;
	}
	tp = args->trans;
	dp = args->dp;
	xfs_dir3_leaf_check(dp, lbp);
	leaf = lbp->b_addr;
	ents = dp->d_ops->leaf_ents_p(leaf);
	/*
	 * Get to the leaf entry and contained data entry address.
	 */
	lep = &ents[index];

	/*
	 * Point to the data entry.
	 */
	dep = (xfs_dir2_data_entry_t *)
	      ((char *)dbp->b_addr +
	       xfs_dir2_dataptr_to_off(args->geo, be32_to_cpu(lep->address)));
	/*
	 * Return the found inode number & CI name if appropriate
	 */
	args->inumber = be64_to_cpu(dep->inumber);
	args->filetype = dp->d_ops->data_get_ftype(dep);
	error = xfs_dir_cilookup_result(args, dep->name, dep->namelen);
	xfs_trans_brelse(tp, dbp);
	xfs_trans_brelse(tp, lbp);
	return error;
}

/*
 * Look up name/hash in the leaf block.
 * Fill in indexp with the found index, and dbpp with the data buffer.
 * If not found dbpp will be NULL, and ENOENT comes back.
 * lbpp will always be filled in with the leaf buffer unless there's an error.
 */
static int					/* error */
xfs_dir2_leaf_lookup_int(
	xfs_da_args_t		*args,		/* operation arguments */
	struct xfs_buf		**lbpp,		/* out: leaf buffer */
	int			*indexp,	/* out: index in leaf block */
	struct xfs_buf		**dbpp)		/* out: data buffer */
{
	xfs_dir2_db_t		curdb = -1;	/* current data block number */
	struct xfs_buf		*dbp = NULL;	/* data buffer */
	xfs_dir2_data_entry_t	*dep;		/* data entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	int			index;		/* index in leaf block */
	struct xfs_buf		*lbp;		/* leaf buffer */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_dir2_db_t		newdb;		/* new data block number */
	xfs_trans_t		*tp;		/* transaction pointer */
	xfs_dir2_db_t		cidb = -1;	/* case match data block no. */
	enum xfs_dacmp		cmp;		/* name compare result */
	struct xfs_dir2_leaf_entry *ents;
	struct xfs_dir3_icleaf_hdr leafhdr;

	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;

	error = xfs_dir3_leaf_read(tp, dp, args->geo->leafblk, -1, &lbp);
	if (error)
		return error;

	*lbpp = lbp;
	leaf = lbp->b_addr;
	xfs_dir3_leaf_check(dp, lbp);
	ents = dp->d_ops->leaf_ents_p(leaf);
	dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);

	/*
	 * Look for the first leaf entry with our hash value.
	 */
	index = xfs_dir2_leaf_search_hash(args, lbp);
	/*
	 * Loop over all the entries with the right hash value
	 * looking to match the name.
	 */
	for (lep = &ents[index];
	     index < leafhdr.count && be32_to_cpu(lep->hashval) == args->hashval;
	     lep++, index++) {
		/*
		 * Skip over stale leaf entries.
		 */
		if (be32_to_cpu(lep->address) == XFS_DIR2_NULL_DATAPTR)
			continue;
		/*
		 * Get the new data block number.
		 */
		newdb = xfs_dir2_dataptr_to_db(args->geo,
					       be32_to_cpu(lep->address));
		/*
		 * If it's not the same as the old data block number,
		 * need to pitch the old one and read the new one.
		 */
		if (newdb != curdb) {
			if (dbp)
				xfs_trans_brelse(tp, dbp);
			error = xfs_dir3_data_read(tp, dp,
					   xfs_dir2_db_to_da(args->geo, newdb),
					   -1, &dbp);
			if (error) {
				xfs_trans_brelse(tp, lbp);
				return error;
			}
			curdb = newdb;
		}
		/*
		 * Point to the data entry.
		 */
		dep = (xfs_dir2_data_entry_t *)((char *)dbp->b_addr +
			xfs_dir2_dataptr_to_off(args->geo,
						be32_to_cpu(lep->address)));
		/*
		 * Compare name and if it's an exact match, return the index
		 * and buffer. If it's the first case-insensitive match, store
		 * the index and buffer and continue looking for an exact match.
		 */
		cmp = mp->m_dirnameops->compname(args, dep->name, dep->namelen);
		if (cmp != XFS_CMP_DIFFERENT && cmp != args->cmpresult) {
			args->cmpresult = cmp;
			*indexp = index;
			/* case exact match: return the current buffer. */
			if (cmp == XFS_CMP_EXACT) {
				*dbpp = dbp;
				return 0;
			}
			cidb = curdb;
		}
	}
	ASSERT(args->op_flags & XFS_DA_OP_OKNOENT);
	/*
	 * Here, we can only be doing a lookup (not a rename or remove).
	 * If a case-insensitive match was found earlier, re-read the
	 * appropriate data block if required and return it.
	 */
	if (args->cmpresult == XFS_CMP_CASE) {
		ASSERT(cidb != -1);
		if (cidb != curdb) {
			xfs_trans_brelse(tp, dbp);
			error = xfs_dir3_data_read(tp, dp,
					   xfs_dir2_db_to_da(args->geo, cidb),
					   -1, &dbp);
			if (error) {
				xfs_trans_brelse(tp, lbp);
				return error;
			}
		}
		*dbpp = dbp;
		return 0;
	}
	/*
	 * No match found, return -ENOENT.
	 */
	ASSERT(cidb == -1);
	if (dbp)
		xfs_trans_brelse(tp, dbp);
	xfs_trans_brelse(tp, lbp);
	return -ENOENT;
}

/*
 * Remove an entry from a leaf format directory.
 */
int						/* error */
xfs_dir2_leaf_removename(
	xfs_da_args_t		*args)		/* operation arguments */
{
	__be16			*bestsp;	/* leaf block best freespace */
	xfs_dir2_data_hdr_t	*hdr;		/* data block header */
	xfs_dir2_db_t		db;		/* data block number */
	struct xfs_buf		*dbp;		/* data block buffer */
	xfs_dir2_data_entry_t	*dep;		/* data entry structure */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	xfs_dir2_db_t		i;		/* temporary data block # */
	int			index;		/* index into leaf entries */
	struct xfs_buf		*lbp;		/* leaf buffer */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail structure */
	int			needlog;	/* need to log data header */
	int			needscan;	/* need to rescan data frees */
	xfs_dir2_data_off_t	oldbest;	/* old value of best free */
	struct xfs_dir2_data_free *bf;		/* bestfree table */
	struct xfs_dir2_leaf_entry *ents;
	struct xfs_dir3_icleaf_hdr leafhdr;

	trace_xfs_dir2_leaf_removename(args);

	/*
	 * Lookup the leaf entry, get the leaf and data blocks read in.
	 */
	if ((error = xfs_dir2_leaf_lookup_int(args, &lbp, &index, &dbp))) {
		return error;
	}
	dp = args->dp;
	leaf = lbp->b_addr;
	hdr = dbp->b_addr;
	xfs_dir3_data_check(dp, dbp);
	bf = dp->d_ops->data_bestfree_p(hdr);
	dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);
	ents = dp->d_ops->leaf_ents_p(leaf);
	/*
	 * Point to the leaf entry, use that to point to the data entry.
	 */
	lep = &ents[index];
	db = xfs_dir2_dataptr_to_db(args->geo, be32_to_cpu(lep->address));
	dep = (xfs_dir2_data_entry_t *)((char *)hdr +
		xfs_dir2_dataptr_to_off(args->geo, be32_to_cpu(lep->address)));
	needscan = needlog = 0;
	oldbest = be16_to_cpu(bf[0].length);
	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);
	bestsp = xfs_dir2_leaf_bests_p(ltp);
	if (be16_to_cpu(bestsp[db]) != oldbest)
		return -EFSCORRUPTED;
	/*
	 * Mark the former data entry unused.
	 */
	xfs_dir2_data_make_free(args, dbp,
		(xfs_dir2_data_aoff_t)((char *)dep - (char *)hdr),
		dp->d_ops->data_entsize(dep->namelen), &needlog, &needscan);
	/*
	 * We just mark the leaf entry stale by putting a null in it.
	 */
	leafhdr.stale++;
	dp->d_ops->leaf_hdr_to_disk(leaf, &leafhdr);
	xfs_dir3_leaf_log_header(args, lbp);

	lep->address = cpu_to_be32(XFS_DIR2_NULL_DATAPTR);
	xfs_dir3_leaf_log_ents(args, lbp, index, index);

	/*
	 * Scan the freespace in the data block again if necessary,
	 * log the data block header if necessary.
	 */
	if (needscan)
		xfs_dir2_data_freescan(dp, hdr, &needlog);
	if (needlog)
		xfs_dir2_data_log_header(args, dbp);
	/*
	 * If the longest freespace in the data block has changed,
	 * put the new value in the bests table and log that.
	 */
	if (be16_to_cpu(bf[0].length) != oldbest) {
		bestsp[db] = bf[0].length;
		xfs_dir3_leaf_log_bests(args, lbp, db, db);
	}
	xfs_dir3_data_check(dp, dbp);
	/*
	 * If the data block is now empty then get rid of the data block.
	 */
	if (be16_to_cpu(bf[0].length) ==
			args->geo->blksize - dp->d_ops->data_entry_offset) {
		ASSERT(db != args->geo->datablk);
		if ((error = xfs_dir2_shrink_inode(args, db, dbp))) {
			/*
			 * Nope, can't get rid of it because it caused
			 * allocation of a bmap btree block to do so.
			 * Just go on, returning success, leaving the
			 * empty block in place.
			 */
			if (error == -ENOSPC && args->total == 0)
				error = 0;
			xfs_dir3_leaf_check(dp, lbp);
			return error;
		}
		dbp = NULL;
		/*
		 * If this is the last data block then compact the
		 * bests table by getting rid of entries.
		 */
		if (db == be32_to_cpu(ltp->bestcount) - 1) {
			/*
			 * Look for the last active entry (i).
			 */
			for (i = db - 1; i > 0; i--) {
				if (bestsp[i] != cpu_to_be16(NULLDATAOFF))
					break;
			}
			/*
			 * Copy the table down so inactive entries at the
			 * end are removed.
			 */
			memmove(&bestsp[db - i], bestsp,
				(be32_to_cpu(ltp->bestcount) - (db - i)) * sizeof(*bestsp));
			be32_add_cpu(&ltp->bestcount, -(db - i));
			xfs_dir3_leaf_log_tail(args, lbp);
			xfs_dir3_leaf_log_bests(args, lbp, 0,
						be32_to_cpu(ltp->bestcount) - 1);
		} else
			bestsp[db] = cpu_to_be16(NULLDATAOFF);
	}
	/*
	 * If the data block was not the first one, drop it.
	 */
	else if (db != args->geo->datablk)
		dbp = NULL;

	xfs_dir3_leaf_check(dp, lbp);
	/*
	 * See if we can convert to block form.
	 */
	return xfs_dir2_leaf_to_block(args, lbp, dbp);
}

/*
 * Replace the inode number in a leaf format directory entry.
 */
int						/* error */
xfs_dir2_leaf_replace(
	xfs_da_args_t		*args)		/* operation arguments */
{
	struct xfs_buf		*dbp;		/* data block buffer */
	xfs_dir2_data_entry_t	*dep;		/* data block entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	int			index;		/* index of leaf entry */
	struct xfs_buf		*lbp;		/* leaf buffer */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_trans_t		*tp;		/* transaction pointer */
	struct xfs_dir2_leaf_entry *ents;

	trace_xfs_dir2_leaf_replace(args);

	/*
	 * Look up the entry.
	 */
	if ((error = xfs_dir2_leaf_lookup_int(args, &lbp, &index, &dbp))) {
		return error;
	}
	dp = args->dp;
	leaf = lbp->b_addr;
	ents = dp->d_ops->leaf_ents_p(leaf);
	/*
	 * Point to the leaf entry, get data address from it.
	 */
	lep = &ents[index];
	/*
	 * Point to the data entry.
	 */
	dep = (xfs_dir2_data_entry_t *)
	      ((char *)dbp->b_addr +
	       xfs_dir2_dataptr_to_off(args->geo, be32_to_cpu(lep->address)));
	ASSERT(args->inumber != be64_to_cpu(dep->inumber));
	/*
	 * Put the new inode number in, log it.
	 */
	dep->inumber = cpu_to_be64(args->inumber);
	dp->d_ops->data_put_ftype(dep, args->filetype);
	tp = args->trans;
	xfs_dir2_data_log_entry(args, dbp, dep);
	xfs_dir3_leaf_check(dp, lbp);
	xfs_trans_brelse(tp, lbp);
	return 0;
}

/*
 * Return index in the leaf block (lbp) which is either the first
 * one with this hash value, or if there are none, the insert point
 * for that hash value.
 */
int						/* index value */
xfs_dir2_leaf_search_hash(
	xfs_da_args_t		*args,		/* operation arguments */
	struct xfs_buf		*lbp)		/* leaf buffer */
{
	xfs_dahash_t		hash=0;		/* hash from this entry */
	xfs_dahash_t		hashwant;	/* hash value looking for */
	int			high;		/* high leaf index */
	int			low;		/* low leaf index */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	int			mid=0;		/* current leaf index */
	struct xfs_dir2_leaf_entry *ents;
	struct xfs_dir3_icleaf_hdr leafhdr;

	leaf = lbp->b_addr;
	ents = args->dp->d_ops->leaf_ents_p(leaf);
	args->dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);

	/*
	 * Note, the table cannot be empty, so we have to go through the loop.
	 * Binary search the leaf entries looking for our hash value.
	 */
	for (lep = ents, low = 0, high = leafhdr.count - 1,
		hashwant = args->hashval;
	     low <= high; ) {
		mid = (low + high) >> 1;
		if ((hash = be32_to_cpu(lep[mid].hashval)) == hashwant)
			break;
		if (hash < hashwant)
			low = mid + 1;
		else
			high = mid - 1;
	}
	/*
	 * Found one, back up through all the equal hash values.
	 */
	if (hash == hashwant) {
		while (mid > 0 && be32_to_cpu(lep[mid - 1].hashval) == hashwant) {
			mid--;
		}
	}
	/*
	 * Need to point to an entry higher than ours.
	 */
	else if (hash < hashwant)
		mid++;
	return mid;
}

/*
 * Trim off a trailing data block.  We know it's empty since the leaf
 * freespace table says so.
 */
int						/* error */
xfs_dir2_leaf_trim_data(
	xfs_da_args_t		*args,		/* operation arguments */
	struct xfs_buf		*lbp,		/* leaf buffer */
	xfs_dir2_db_t		db)		/* data block number */
{
	__be16			*bestsp;	/* leaf bests table */
	struct xfs_buf		*dbp;		/* data block buffer */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return value */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail structure */
	xfs_trans_t		*tp;		/* transaction pointer */

	dp = args->dp;
	tp = args->trans;
	/*
	 * Read the offending data block.  We need its buffer.
	 */
	error = xfs_dir3_data_read(tp, dp, xfs_dir2_db_to_da(args->geo, db),
				   -1, &dbp);
	if (error)
		return error;

	leaf = lbp->b_addr;
	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);

#ifdef DEBUG
{
	struct xfs_dir2_data_hdr *hdr = dbp->b_addr;
	struct xfs_dir2_data_free *bf = dp->d_ops->data_bestfree_p(hdr);

	ASSERT(hdr->magic == cpu_to_be32(XFS_DIR2_DATA_MAGIC) ||
	       hdr->magic == cpu_to_be32(XFS_DIR3_DATA_MAGIC));
	ASSERT(be16_to_cpu(bf[0].length) ==
	       args->geo->blksize - dp->d_ops->data_entry_offset);
	ASSERT(db == be32_to_cpu(ltp->bestcount) - 1);
}
#endif

	/*
	 * Get rid of the data block.
	 */
	if ((error = xfs_dir2_shrink_inode(args, db, dbp))) {
		ASSERT(error != -ENOSPC);
		xfs_trans_brelse(tp, dbp);
		return error;
	}
	/*
	 * Eliminate the last bests entry from the table.
	 */
	bestsp = xfs_dir2_leaf_bests_p(ltp);
	be32_add_cpu(&ltp->bestcount, -1);
	memmove(&bestsp[1], &bestsp[0], be32_to_cpu(ltp->bestcount) * sizeof(*bestsp));
	xfs_dir3_leaf_log_tail(args, lbp);
	xfs_dir3_leaf_log_bests(args, lbp, 0, be32_to_cpu(ltp->bestcount) - 1);
	return 0;
}

static inline size_t
xfs_dir3_leaf_size(
	struct xfs_dir3_icleaf_hdr	*hdr,
	int				counts)
{
	int	entries;
	int	hdrsize;

	entries = hdr->count - hdr->stale;
	if (hdr->magic == XFS_DIR2_LEAF1_MAGIC ||
	    hdr->magic == XFS_DIR2_LEAFN_MAGIC)
		hdrsize = sizeof(struct xfs_dir2_leaf_hdr);
	else
		hdrsize = sizeof(struct xfs_dir3_leaf_hdr);

	return hdrsize + entries * sizeof(xfs_dir2_leaf_entry_t)
	               + counts * sizeof(xfs_dir2_data_off_t)
		       + sizeof(xfs_dir2_leaf_tail_t);
}

/*
 * Convert node form directory to leaf form directory.
 * The root of the node form dir needs to already be a LEAFN block.
 * Just return if we can't do anything.
 */
int						/* error */
xfs_dir2_node_to_leaf(
	xfs_da_state_t		*state)		/* directory operation state */
{
	xfs_da_args_t		*args;		/* operation arguments */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	struct xfs_buf		*fbp;		/* buffer for freespace block */
	xfs_fileoff_t		fo;		/* freespace file offset */
	xfs_dir2_free_t		*free;		/* freespace structure */
	struct xfs_buf		*lbp;		/* buffer for leaf block */
	xfs_dir2_leaf_tail_t	*ltp;		/* tail of leaf structure */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			rval;		/* successful free trim? */
	xfs_trans_t		*tp;		/* transaction pointer */
	struct xfs_dir3_icleaf_hdr leafhdr;
	struct xfs_dir3_icfree_hdr freehdr;

	/*
	 * There's more than a leaf level in the btree, so there must
	 * be multiple leafn blocks.  Give up.
	 */
	if (state->path.active > 1)
		return 0;
	args = state->args;

	trace_xfs_dir2_node_to_leaf(args);

	mp = state->mp;
	dp = args->dp;
	tp = args->trans;
	/*
	 * Get the last offset in the file.
	 */
	if ((error = xfs_bmap_last_offset(dp, &fo, XFS_DATA_FORK))) {
		return error;
	}
	fo -= args->geo->fsbcount;
	/*
	 * If there are freespace blocks other than the first one,
	 * take this opportunity to remove trailing empty freespace blocks
	 * that may have been left behind during no-space-reservation
	 * operations.
	 */
	while (fo > args->geo->freeblk) {
		if ((error = xfs_dir2_node_trim_free(args, fo, &rval))) {
			return error;
		}
		if (rval)
			fo -= args->geo->fsbcount;
		else
			return 0;
	}
	/*
	 * Now find the block just before the freespace block.
	 */
	if ((error = xfs_bmap_last_before(tp, dp, &fo, XFS_DATA_FORK))) {
		return error;
	}
	/*
	 * If it's not the single leaf block, give up.
	 */
	if (XFS_FSB_TO_B(mp, fo) > XFS_DIR2_LEAF_OFFSET + args->geo->blksize)
		return 0;
	lbp = state->path.blk[0].bp;
	leaf = lbp->b_addr;
	dp->d_ops->leaf_hdr_from_disk(&leafhdr, leaf);

	ASSERT(leafhdr.magic == XFS_DIR2_LEAFN_MAGIC ||
	       leafhdr.magic == XFS_DIR3_LEAFN_MAGIC);

	/*
	 * Read the freespace block.
	 */
	error = xfs_dir2_free_read(tp, dp,  args->geo->freeblk, &fbp);
	if (error)
		return error;
	free = fbp->b_addr;
	dp->d_ops->free_hdr_from_disk(&freehdr, free);

	ASSERT(!freehdr.firstdb);

	/*
	 * Now see if the leafn and free data will fit in a leaf1.
	 * If not, release the buffer and give up.
	 */
	if (xfs_dir3_leaf_size(&leafhdr, freehdr.nvalid) > args->geo->blksize) {
		xfs_trans_brelse(tp, fbp);
		return 0;
	}

	/*
	 * If the leaf has any stale entries in it, compress them out.
	 */
	if (leafhdr.stale)
		xfs_dir3_leaf_compact(args, &leafhdr, lbp);

	lbp->b_ops = &xfs_dir3_leaf1_buf_ops;
	xfs_trans_buf_set_type(tp, lbp, XFS_BLFT_DIR_LEAF1_BUF);
	leafhdr.magic = (leafhdr.magic == XFS_DIR2_LEAFN_MAGIC)
					? XFS_DIR2_LEAF1_MAGIC
					: XFS_DIR3_LEAF1_MAGIC;

	/*
	 * Set up the leaf tail from the freespace block.
	 */
	ltp = xfs_dir2_leaf_tail_p(args->geo, leaf);
	ltp->bestcount = cpu_to_be32(freehdr.nvalid);

	/*
	 * Set up the leaf bests table.
	 */
	memcpy(xfs_dir2_leaf_bests_p(ltp), dp->d_ops->free_bests_p(free),
		freehdr.nvalid * sizeof(xfs_dir2_data_off_t));

	dp->d_ops->leaf_hdr_to_disk(leaf, &leafhdr);
	xfs_dir3_leaf_log_header(args, lbp);
	xfs_dir3_leaf_log_bests(args, lbp, 0, be32_to_cpu(ltp->bestcount) - 1);
	xfs_dir3_leaf_log_tail(args, lbp);
	xfs_dir3_leaf_check(dp, lbp);

	/*
	 * Get rid of the freespace block.
	 */
	error = xfs_dir2_shrink_inode(args,
			xfs_dir2_byte_to_db(args->geo, XFS_DIR2_FREE_OFFSET),
			fbp);
	if (error) {
		/*
		 * This can't fail here because it can only happen when
		 * punching out the middle of an extent, and this is an
		 * isolated block.
		 */
		ASSERT(error != -ENOSPC);
		return error;
	}
	fbp = NULL;
	/*
	 * Now see if we can convert the single-leaf directory
	 * down to a block form directory.
	 * This routine always kills the dabuf for the leaf, so
	 * eliminate it from the path.
	 */
	error = xfs_dir2_leaf_to_block(args, lbp, NULL);
	state->path.blk[0].bp = NULL;
	return error;
}
