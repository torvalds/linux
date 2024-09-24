// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * Copyright (C) 2010 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_bmap_btree.h"
#include "xfs_quota.h"
#include "xfs_trans.h"
#include "xfs_qm.h"
#include "xfs_trans_space.h"
#include "xfs_rtbitmap.h"
#include "xfs_attr_item.h"
#include "xfs_log.h"

#define _ALLOC	true
#define _FREE	false

/*
 * A buffer has a format structure overhead in the log in addition
 * to the data, so we need to take this into account when reserving
 * space in a transaction for a buffer.  Round the space required up
 * to a multiple of 128 bytes so that we don't change the historical
 * reservation that has been used for this overhead.
 */
STATIC uint
xfs_buf_log_overhead(void)
{
	return round_up(sizeof(struct xlog_op_header) +
			sizeof(struct xfs_buf_log_format), 128);
}

/*
 * Calculate out transaction log reservation per item in bytes.
 *
 * The nbufs argument is used to indicate the number of items that
 * will be changed in a transaction.  size is used to tell how many
 * bytes should be reserved per item.
 */
STATIC uint
xfs_calc_buf_res(
	uint		nbufs,
	uint		size)
{
	return nbufs * (size + xfs_buf_log_overhead());
}

/*
 * Per-extent log reservation for the btree changes involved in freeing or
 * allocating an extent.  In classic XFS there were two trees that will be
 * modified (bnobt + cntbt).  With rmap enabled, there are three trees
 * (rmapbt).  The number of blocks reserved is based on the formula:
 *
 * num trees * ((2 blocks/level * max depth) - 1)
 *
 * Keep in mind that max depth is calculated separately for each type of tree.
 */
uint
xfs_allocfree_block_count(
	struct xfs_mount *mp,
	uint		num_ops)
{
	uint		blocks;

	blocks = num_ops * 2 * (2 * mp->m_alloc_maxlevels - 1);
	if (xfs_has_rmapbt(mp))
		blocks += num_ops * (2 * mp->m_rmap_maxlevels - 1);

	return blocks;
}

/*
 * Per-extent log reservation for refcount btree changes.  These are never done
 * in the same transaction as an allocation or a free, so we compute them
 * separately.
 */
static unsigned int
xfs_refcountbt_block_count(
	struct xfs_mount	*mp,
	unsigned int		num_ops)
{
	return num_ops * (2 * mp->m_refc_maxlevels - 1);
}

/*
 * Logging inodes is really tricksy. They are logged in memory format,
 * which means that what we write into the log doesn't directly translate into
 * the amount of space they use on disk.
 *
 * Case in point - btree format forks in memory format use more space than the
 * on-disk format. In memory, the buffer contains a normal btree block header so
 * the btree code can treat it as though it is just another generic buffer.
 * However, when we write it to the inode fork, we don't write all of this
 * header as it isn't needed. e.g. the root is only ever in the inode, so
 * there's no need for sibling pointers which would waste 16 bytes of space.
 *
 * Hence when we have an inode with a maximally sized btree format fork, then
 * amount of information we actually log is greater than the size of the inode
 * on disk. Hence we need an inode reservation function that calculates all this
 * correctly. So, we log:
 *
 * - 4 log op headers for object
 *	- for the ilf, the inode core and 2 forks
 * - inode log format object
 * - the inode core
 * - two inode forks containing bmap btree root blocks.
 *	- the btree data contained by both forks will fit into the inode size,
 *	  hence when combined with the inode core above, we have a total of the
 *	  actual inode size.
 *	- the BMBT headers need to be accounted separately, as they are
 *	  additional to the records and pointers that fit inside the inode
 *	  forks.
 */
STATIC uint
xfs_calc_inode_res(
	struct xfs_mount	*mp,
	uint			ninodes)
{
	return ninodes *
		(4 * sizeof(struct xlog_op_header) +
		 sizeof(struct xfs_inode_log_format) +
		 mp->m_sb.sb_inodesize +
		 2 * xfs_bmbt_block_len(mp));
}

/*
 * Inode btree record insertion/removal modifies the inode btree and free space
 * btrees (since the inobt does not use the agfl). This requires the following
 * reservation:
 *
 * the inode btree: max depth * blocksize
 * the allocation btrees: 2 trees * (max depth - 1) * block size
 *
 * The caller must account for SB and AG header modifications, etc.
 */
STATIC uint
xfs_calc_inobt_res(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(M_IGEO(mp)->inobt_maxlevels,
			XFS_FSB_TO_B(mp, 1)) +
				xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1),
			XFS_FSB_TO_B(mp, 1));
}

/*
 * The free inode btree is a conditional feature. The behavior differs slightly
 * from that of the traditional inode btree in that the finobt tracks records
 * for inode chunks with at least one free inode. A record can be removed from
 * the tree during individual inode allocation. Therefore the finobt
 * reservation is unconditional for both the inode chunk allocation and
 * individual inode allocation (modify) cases.
 *
 * Behavior aside, the reservation for finobt modification is equivalent to the
 * traditional inobt: cover a full finobt shape change plus block allocation.
 */
STATIC uint
xfs_calc_finobt_res(
	struct xfs_mount	*mp)
{
	if (!xfs_has_finobt(mp))
		return 0;

	return xfs_calc_inobt_res(mp);
}

/*
 * Calculate the reservation required to allocate or free an inode chunk. This
 * includes:
 *
 * the allocation btrees: 2 trees * (max depth - 1) * block size
 * the inode chunk: m_ino_geo.ialloc_blks * N
 *
 * The size N of the inode chunk reservation depends on whether it is for
 * allocation or free and which type of create transaction is in use. An inode
 * chunk free always invalidates the buffers and only requires reservation for
 * headers (N == 0). An inode chunk allocation requires a chunk sized
 * reservation on v4 and older superblocks to initialize the chunk. No chunk
 * reservation is required for allocation on v5 supers, which use ordered
 * buffers to initialize.
 */
STATIC uint
xfs_calc_inode_chunk_res(
	struct xfs_mount	*mp,
	bool			alloc)
{
	uint			res, size = 0;

	res = xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1),
			       XFS_FSB_TO_B(mp, 1));
	if (alloc) {
		/* icreate tx uses ordered buffers */
		if (xfs_has_v3inodes(mp))
			return res;
		size = XFS_FSB_TO_B(mp, 1);
	}

	res += xfs_calc_buf_res(M_IGEO(mp)->ialloc_blks, size);
	return res;
}

/*
 * Per-extent log reservation for the btree changes involved in freeing or
 * allocating a realtime extent.  We have to be able to log as many rtbitmap
 * blocks as needed to mark inuse XFS_BMBT_MAX_EXTLEN blocks' worth of realtime
 * extents, as well as the realtime summary block.
 */
static unsigned int
xfs_rtalloc_block_count(
	struct xfs_mount	*mp,
	unsigned int		num_ops)
{
	unsigned int		rtbmp_blocks;
	xfs_rtxlen_t		rtxlen;

	rtxlen = xfs_extlen_to_rtxlen(mp, XFS_MAX_BMBT_EXTLEN);
	rtbmp_blocks = xfs_rtbitmap_blockcount(mp, rtxlen);
	return (rtbmp_blocks + 1) * num_ops;
}

/*
 * Various log reservation values.
 *
 * These are based on the size of the file system block because that is what
 * most transactions manipulate.  Each adds in an additional 128 bytes per
 * item logged to try to account for the overhead of the transaction mechanism.
 *
 * Note:  Most of the reservations underestimate the number of allocation
 * groups into which they could free extents in the xfs_defer_finish() call.
 * This is because the number in the worst case is quite high and quite
 * unusual.  In order to fix this we need to change xfs_defer_finish() to free
 * extents in only a single AG at a time.  This will require changes to the
 * EFI code as well, however, so that the EFI for the extents not freed is
 * logged again in each transaction.  See SGI PV #261917.
 *
 * Reservation functions here avoid a huge stack in xfs_trans_init due to
 * register overflow from temporaries in the calculations.
 */

/*
 * Compute the log reservation required to handle the refcount update
 * transaction.  Refcount updates are always done via deferred log items.
 *
 * This is calculated as:
 * Data device refcount updates (t1):
 *    the agfs of the ags containing the blocks: nr_ops * sector size
 *    the refcount btrees: nr_ops * 1 trees * (2 * max depth - 1) * block size
 */
static unsigned int
xfs_calc_refcountbt_reservation(
	struct xfs_mount	*mp,
	unsigned int		nr_ops)
{
	unsigned int		blksz = XFS_FSB_TO_B(mp, 1);

	if (!xfs_has_reflink(mp))
		return 0;

	return xfs_calc_buf_res(nr_ops, mp->m_sb.sb_sectsize) +
	       xfs_calc_buf_res(xfs_refcountbt_block_count(mp, nr_ops), blksz);
}

/*
 * In a write transaction we can allocate a maximum of 2
 * extents.  This gives (t1):
 *    the inode getting the new extents: inode size
 *    the inode's bmap btree: max depth * block size
 *    the agfs of the ags from which the extents are allocated: 2 * sector
 *    the superblock free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 * Or, if we're writing to a realtime file (t2):
 *    the inode getting the new extents: inode size
 *    the inode's bmap btree: max depth * block size
 *    the agfs of the ags from which the extents are allocated: 2 * sector
 *    the superblock free block counter: sector size
 *    the realtime bitmap: ((XFS_BMBT_MAX_EXTLEN / rtextsize) / NBBY) bytes
 *    the realtime summary: 1 block
 *    the allocation btrees: 2 trees * (2 * max depth - 1) * block size
 * And the bmap_finish transaction can free bmap blocks in a join (t3):
 *    the agfs of the ags containing the blocks: 2 * sector size
 *    the agfls of the ags containing the blocks: 2 * sector size
 *    the super block free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 * And any refcount updates that happen in a separate transaction (t4).
 */
STATIC uint
xfs_calc_write_reservation(
	struct xfs_mount	*mp,
	bool			for_minlogsize)
{
	unsigned int		t1, t2, t3, t4;
	unsigned int		blksz = XFS_FSB_TO_B(mp, 1);

	t1 = xfs_calc_inode_res(mp, 1) +
	     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK), blksz) +
	     xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
	     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 2), blksz);

	if (xfs_has_realtime(mp)) {
		t2 = xfs_calc_inode_res(mp, 1) +
		     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK),
				     blksz) +
		     xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(xfs_rtalloc_block_count(mp, 1), blksz) +
		     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1), blksz);
	} else {
		t2 = 0;
	}

	t3 = xfs_calc_buf_res(5, mp->m_sb.sb_sectsize) +
	     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 2), blksz);

	/*
	 * In the early days of reflink, we included enough reservation to log
	 * two refcountbt splits for each transaction.  The codebase runs
	 * refcountbt updates in separate transactions now, so to compute the
	 * minimum log size, add the refcountbtree splits back to t1 and t3 and
	 * do not account them separately as t4.  Reflink did not support
	 * realtime when the reservations were established, so no adjustment to
	 * t2 is needed.
	 */
	if (for_minlogsize) {
		unsigned int	adj = 0;

		if (xfs_has_reflink(mp))
			adj = xfs_calc_buf_res(
					xfs_refcountbt_block_count(mp, 2),
					blksz);
		t1 += adj;
		t3 += adj;
		return XFS_DQUOT_LOGRES + max3(t1, t2, t3);
	}

	t4 = xfs_calc_refcountbt_reservation(mp, 1);
	return XFS_DQUOT_LOGRES + max(t4, max3(t1, t2, t3));
}

unsigned int
xfs_calc_write_reservation_minlogsize(
	struct xfs_mount	*mp)
{
	return xfs_calc_write_reservation(mp, true);
}

/*
 * In truncating a file we free up to two extents at once.  We can modify (t1):
 *    the inode being truncated: inode size
 *    the inode's bmap btree: (max depth + 1) * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks (t2):
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 * Or, if it's a realtime file (t3):
 *    the agf for each of the ags: 2 * sector size
 *    the agfl for each of the ags: 2 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    the realtime bitmap:
 *		2 exts * ((XFS_BMBT_MAX_EXTLEN / rtextsize) / NBBY) bytes
 *    the realtime summary: 2 exts * 1 block
 *    worst case split in allocation btrees per extent assuming 2 extents:
 *		2 exts * 2 trees * (2 * max depth - 1) * block size
 * And any refcount updates that happen in a separate transaction (t4).
 */
STATIC uint
xfs_calc_itruncate_reservation(
	struct xfs_mount	*mp,
	bool			for_minlogsize)
{
	unsigned int		t1, t2, t3, t4;
	unsigned int		blksz = XFS_FSB_TO_B(mp, 1);

	t1 = xfs_calc_inode_res(mp, 1) +
	     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + 1, blksz);

	t2 = xfs_calc_buf_res(9, mp->m_sb.sb_sectsize) +
	     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 4), blksz);

	if (xfs_has_realtime(mp)) {
		t3 = xfs_calc_buf_res(5, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(xfs_rtalloc_block_count(mp, 2), blksz) +
		     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 2), blksz);
	} else {
		t3 = 0;
	}

	/*
	 * In the early days of reflink, we included enough reservation to log
	 * four refcountbt splits in the same transaction as bnobt/cntbt
	 * updates.  The codebase runs refcountbt updates in separate
	 * transactions now, so to compute the minimum log size, add the
	 * refcount btree splits back here and do not compute them separately
	 * as t4.  Reflink did not support realtime when the reservations were
	 * established, so do not adjust t3.
	 */
	if (for_minlogsize) {
		if (xfs_has_reflink(mp))
			t2 += xfs_calc_buf_res(
					xfs_refcountbt_block_count(mp, 4),
					blksz);

		return XFS_DQUOT_LOGRES + max3(t1, t2, t3);
	}

	t4 = xfs_calc_refcountbt_reservation(mp, 2);
	return XFS_DQUOT_LOGRES + max(t4, max3(t1, t2, t3));
}

unsigned int
xfs_calc_itruncate_reservation_minlogsize(
	struct xfs_mount	*mp)
{
	return xfs_calc_itruncate_reservation(mp, true);
}

static inline unsigned int xfs_calc_pptr_link_overhead(void)
{
	return sizeof(struct xfs_attri_log_format) +
			xlog_calc_iovec_len(sizeof(struct xfs_parent_rec)) +
			xlog_calc_iovec_len(MAXNAMELEN - 1);
}
static inline unsigned int xfs_calc_pptr_unlink_overhead(void)
{
	return sizeof(struct xfs_attri_log_format) +
			xlog_calc_iovec_len(sizeof(struct xfs_parent_rec)) +
			xlog_calc_iovec_len(MAXNAMELEN - 1);
}
static inline unsigned int xfs_calc_pptr_replace_overhead(void)
{
	return sizeof(struct xfs_attri_log_format) +
			xlog_calc_iovec_len(sizeof(struct xfs_parent_rec)) +
			xlog_calc_iovec_len(MAXNAMELEN - 1) +
			xlog_calc_iovec_len(sizeof(struct xfs_parent_rec)) +
			xlog_calc_iovec_len(MAXNAMELEN - 1);
}

/*
 * In renaming a files we can modify:
 *    the five inodes involved: 5 * inode size
 *    the two directory btrees: 2 * (max depth + v2) * dir block size
 *    the two directory bmap btrees: 2 * max depth * block size
 * And the bmap_finish transaction can free dir and bmap blocks (two sets
 *	of bmap blocks) giving (t2):
 *    the agf for the ags in which the blocks live: 3 * sector size
 *    the agfl for the ags in which the blocks live: 3 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 3 exts * 2 trees * (2 * max depth - 1) * block size
 * If parent pointers are enabled (t3), then each transaction in the chain
 *    must be capable of setting or removing the extended attribute
 *    containing the parent information.  It must also be able to handle
 *    the three xattr intent items that track the progress of the parent
 *    pointer update.
 */
STATIC uint
xfs_calc_rename_reservation(
	struct xfs_mount	*mp)
{
	unsigned int		overhead = XFS_DQUOT_LOGRES;
	struct xfs_trans_resv	*resp = M_RES(mp);
	unsigned int		t1, t2, t3 = 0;

	t1 = xfs_calc_inode_res(mp, 5) +
	     xfs_calc_buf_res(2 * XFS_DIROP_LOG_COUNT(mp),
			XFS_FSB_TO_B(mp, 1));

	t2 = xfs_calc_buf_res(7, mp->m_sb.sb_sectsize) +
	     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 3),
			XFS_FSB_TO_B(mp, 1));

	if (xfs_has_parent(mp)) {
		unsigned int	rename_overhead, exchange_overhead;

		t3 = max(resp->tr_attrsetm.tr_logres,
			 resp->tr_attrrm.tr_logres);

		/*
		 * For a standard rename, the three xattr intent log items
		 * are (1) replacing the pptr for the source file; (2)
		 * removing the pptr on the dest file; and (3) adding a
		 * pptr for the whiteout file in the src dir.
		 *
		 * For an RENAME_EXCHANGE, there are two xattr intent
		 * items to replace the pptr for both src and dest
		 * files.  Link counts don't change and there is no
		 * whiteout.
		 *
		 * In the worst case we can end up relogging all log
		 * intent items to allow the log tail to move ahead, so
		 * they become overhead added to each transaction in a
		 * processing chain.
		 */
		rename_overhead = xfs_calc_pptr_replace_overhead() +
				  xfs_calc_pptr_unlink_overhead() +
				  xfs_calc_pptr_link_overhead();
		exchange_overhead = 2 * xfs_calc_pptr_replace_overhead();

		overhead += max(rename_overhead, exchange_overhead);
	}

	return overhead + max3(t1, t2, t3);
}

static inline unsigned int
xfs_rename_log_count(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	/* One for the rename, one more for freeing blocks */
	unsigned int		ret = XFS_RENAME_LOG_COUNT;

	/*
	 * Pre-reserve enough log reservation to handle the transaction
	 * rolling needed to remove or add one parent pointer.
	 */
	if (xfs_has_parent(mp))
		ret += max(resp->tr_attrsetm.tr_logcount,
			   resp->tr_attrrm.tr_logcount);

	return ret;
}

/*
 * For removing an inode from unlinked list at first, we can modify:
 *    the agi hash list and counters: sector size
 *    the on disk inode before ours in the agi hash list: inode cluster size
 *    the on disk inode in the agi hash list: inode cluster size
 */
STATIC uint
xfs_calc_iunlink_remove_reservation(
	struct xfs_mount        *mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
	       2 * M_IGEO(mp)->inode_cluster_size;
}

static inline unsigned int
xfs_link_log_count(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	unsigned int		ret = XFS_LINK_LOG_COUNT;

	/*
	 * Pre-reserve enough log reservation to handle the transaction
	 * rolling needed to add one parent pointer.
	 */
	if (xfs_has_parent(mp))
		ret += resp->tr_attrsetm.tr_logcount;

	return ret;
}

/*
 * For creating a link to an inode:
 *    the parent directory inode: inode size
 *    the linked inode: inode size
 *    the directory btree could split: (max depth + v2) * dir block size
 *    the directory bmap btree could join or split: (max depth + v2) * blocksize
 * And the bmap_finish transaction can free some bmap blocks giving:
 *    the agf for the ag in which the blocks live: sector size
 *    the agfl for the ag in which the blocks live: sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_link_reservation(
	struct xfs_mount	*mp)
{
	unsigned int		overhead = XFS_DQUOT_LOGRES;
	struct xfs_trans_resv	*resp = M_RES(mp);
	unsigned int		t1, t2, t3 = 0;

	overhead += xfs_calc_iunlink_remove_reservation(mp);
	t1 = xfs_calc_inode_res(mp, 2) +
	     xfs_calc_buf_res(XFS_DIROP_LOG_COUNT(mp), XFS_FSB_TO_B(mp, 1));
	t2 = xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
	     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1),
			      XFS_FSB_TO_B(mp, 1));

	if (xfs_has_parent(mp)) {
		t3 = resp->tr_attrsetm.tr_logres;
		overhead += xfs_calc_pptr_link_overhead();
	}

	return overhead + max3(t1, t2, t3);
}

/*
 * For adding an inode to unlinked list we can modify:
 *    the agi hash list: sector size
 *    the on disk inode: inode cluster size
 */
STATIC uint
xfs_calc_iunlink_add_reservation(xfs_mount_t *mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
			M_IGEO(mp)->inode_cluster_size;
}

static inline unsigned int
xfs_remove_log_count(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	unsigned int		ret = XFS_REMOVE_LOG_COUNT;

	/*
	 * Pre-reserve enough log reservation to handle the transaction
	 * rolling needed to add one parent pointer.
	 */
	if (xfs_has_parent(mp))
		ret += resp->tr_attrrm.tr_logcount;

	return ret;
}

/*
 * For removing a directory entry we can modify:
 *    the parent directory inode: inode size
 *    the removed inode: inode size
 *    the directory btree could join: (max depth + v2) * dir block size
 *    the directory bmap btree could join or split: (max depth + v2) * blocksize
 * And the bmap_finish transaction can free the dir and bmap blocks giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the agfl for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_remove_reservation(
	struct xfs_mount	*mp)
{
	unsigned int            overhead = XFS_DQUOT_LOGRES;
	struct xfs_trans_resv   *resp = M_RES(mp);
	unsigned int            t1, t2, t3 = 0;

	overhead += xfs_calc_iunlink_add_reservation(mp);

	t1 = xfs_calc_inode_res(mp, 2) +
	     xfs_calc_buf_res(XFS_DIROP_LOG_COUNT(mp), XFS_FSB_TO_B(mp, 1));
	t2 = xfs_calc_buf_res(4, mp->m_sb.sb_sectsize) +
	     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 2),
			      XFS_FSB_TO_B(mp, 1));

	if (xfs_has_parent(mp)) {
		t3 = resp->tr_attrrm.tr_logres;
		overhead += xfs_calc_pptr_unlink_overhead();
	}

	return overhead + max3(t1, t2, t3);
}

/*
 * For create, break it in to the two cases that the transaction
 * covers. We start with the modify case - allocation done by modification
 * of the state of existing inodes - and the allocation case.
 */

/*
 * For create we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: block size
 *    the superblock for the nlink flag: sector size
 *    the directory btree: (max depth + v2) * dir block size
 *    the directory inode's bmap btree: (max depth + v2) * block size
 *    the finobt (record modification and allocation btrees)
 */
STATIC uint
xfs_calc_create_resv_modify(
	struct xfs_mount	*mp)
{
	return xfs_calc_inode_res(mp, 2) +
		xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		(uint)XFS_FSB_TO_B(mp, 1) +
		xfs_calc_buf_res(XFS_DIROP_LOG_COUNT(mp), XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_finobt_res(mp);
}

/*
 * For icreate we can allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the superblock for the nlink flag: sector size
 *    the inode chunk (allocation, optional init)
 *    the inobt (record insertion)
 *    the finobt (optional, record insertion)
 */
STATIC uint
xfs_calc_icreate_resv_alloc(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		mp->m_sb.sb_sectsize +
		xfs_calc_inode_chunk_res(mp, _ALLOC) +
		xfs_calc_inobt_res(mp) +
		xfs_calc_finobt_res(mp);
}

static inline unsigned int
xfs_icreate_log_count(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	unsigned int		ret = XFS_CREATE_LOG_COUNT;

	/*
	 * Pre-reserve enough log reservation to handle the transaction
	 * rolling needed to add one parent pointer.
	 */
	if (xfs_has_parent(mp))
		ret += resp->tr_attrsetm.tr_logcount;

	return ret;
}

STATIC uint
xfs_calc_icreate_reservation(
	struct xfs_mount	*mp)
{
	struct xfs_trans_resv	*resp = M_RES(mp);
	unsigned int		overhead = XFS_DQUOT_LOGRES;
	unsigned int		t1, t2, t3 = 0;

	t1 = xfs_calc_icreate_resv_alloc(mp);
	t2 = xfs_calc_create_resv_modify(mp);

	if (xfs_has_parent(mp)) {
		t3 = resp->tr_attrsetm.tr_logres;
		overhead += xfs_calc_pptr_link_overhead();
	}

	return overhead + max3(t1, t2, t3);
}

STATIC uint
xfs_calc_create_tmpfile_reservation(
	struct xfs_mount        *mp)
{
	uint	res = XFS_DQUOT_LOGRES;

	res += xfs_calc_icreate_resv_alloc(mp);
	return res + xfs_calc_iunlink_add_reservation(mp);
}

static inline unsigned int
xfs_mkdir_log_count(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	unsigned int		ret = XFS_MKDIR_LOG_COUNT;

	/*
	 * Pre-reserve enough log reservation to handle the transaction
	 * rolling needed to add one parent pointer.
	 */
	if (xfs_has_parent(mp))
		ret += resp->tr_attrsetm.tr_logcount;

	return ret;
}

/*
 * Making a new directory is the same as creating a new file.
 */
STATIC uint
xfs_calc_mkdir_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_icreate_reservation(mp);
}

static inline unsigned int
xfs_symlink_log_count(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	unsigned int		ret = XFS_SYMLINK_LOG_COUNT;

	/*
	 * Pre-reserve enough log reservation to handle the transaction
	 * rolling needed to add one parent pointer.
	 */
	if (xfs_has_parent(mp))
		ret += resp->tr_attrsetm.tr_logcount;

	return ret;
}

/*
 * Making a new symplink is the same as creating a new file, but
 * with the added blocks for remote symlink data which can be up to 1kB in
 * length (XFS_SYMLINK_MAXLEN).
 */
STATIC uint
xfs_calc_symlink_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_icreate_reservation(mp) +
	       xfs_calc_buf_res(1, XFS_SYMLINK_MAXLEN);
}

/*
 * In freeing an inode we can modify:
 *    the inode being freed: inode size
 *    the super block free inode counter, AGF and AGFL: sector size
 *    the on disk inode (agi unlinked list removal)
 *    the inode chunk (invalidated, headers only)
 *    the inode btree
 *    the finobt (record insertion, removal or modification)
 *
 * Note that the inode chunk res. includes an allocfree res. for freeing of the
 * inode chunk. This is technically extraneous because the inode chunk free is
 * deferred (it occurs after a transaction roll). Include the extra reservation
 * anyways since we've had reports of ifree transaction overruns due to too many
 * agfl fixups during inode chunk frees.
 */
STATIC uint
xfs_calc_ifree_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES +
		xfs_calc_inode_res(mp, 1) +
		xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
		xfs_calc_iunlink_remove_reservation(mp) +
		xfs_calc_inode_chunk_res(mp, _FREE) +
		xfs_calc_inobt_res(mp) +
		xfs_calc_finobt_res(mp);
}

/*
 * When only changing the inode we log the inode and possibly the superblock
 * We also add a bit of slop for the transaction stuff.
 */
STATIC uint
xfs_calc_ichange_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES +
		xfs_calc_inode_res(mp, 1) +
		xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);

}

/*
 * Growing the data section of the filesystem.
 *	superblock
 *	agi and agf
 *	allocation btrees
 */
STATIC uint
xfs_calc_growdata_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(3, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Growing the rt section of the filesystem.
 * In the first set of transactions (ALLOC) we allocate space to the
 * bitmap or summary files.
 *	superblock: sector size
 *	agf of the ag from which the extent is allocated: sector size
 *	bmap btree for bitmap/summary inode: max depth * blocksize
 *	bitmap/summary inode: inode size
 *	allocation btrees for 1 block alloc: 2 * (2 * maxdepth - 1) * blocksize
 */
STATIC uint
xfs_calc_growrtalloc_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK),
				 XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_inode_res(mp, 1) +
		xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Growing the rt section of the filesystem.
 * In the second set of transactions (ZERO) we zero the new metadata blocks.
 *	one bitmap/summary block: blocksize
 */
STATIC uint
xfs_calc_growrtzero_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_blocksize);
}

/*
 * Growing the rt section of the filesystem.
 * In the third set of transactions (FREE) we update metadata without
 * allocating any new blocks.
 *	superblock: sector size
 *	bitmap inode: inode size
 *	summary inode: inode size
 *	one bitmap block: blocksize
 *	summary blocks: new summary size
 */
STATIC uint
xfs_calc_growrtfree_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		xfs_calc_inode_res(mp, 2) +
		xfs_calc_buf_res(1, mp->m_sb.sb_blocksize) +
		xfs_calc_buf_res(1, XFS_FSB_TO_B(mp, mp->m_rsumblocks));
}

/*
 * Logging the inode modification timestamp on a synchronous write.
 *	inode
 */
STATIC uint
xfs_calc_swrite_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_inode_res(mp, 1);
}

/*
 * Logging the inode mode bits when writing a setuid/setgid file
 *	inode
 */
STATIC uint
xfs_calc_writeid_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_inode_res(mp, 1);
}

/*
 * Converting the inode from non-attributed to attributed.
 *	the inode being converted: inode size
 *	agf block and superblock (for block allocation)
 *	the new block (directory sized)
 *	bmap blocks for the new directory block
 *	allocation btrees
 */
STATIC uint
xfs_calc_addafork_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES +
		xfs_calc_inode_res(mp, 1) +
		xfs_calc_buf_res(2, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(1, mp->m_dir_geo->blksize) +
		xfs_calc_buf_res(XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK) + 1,
				 XFS_FSB_TO_B(mp, 1)) +
		xfs_calc_buf_res(xfs_allocfree_block_count(mp, 1),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Removing the attribute fork of a file
 *    the inode being truncated: inode size
 *    the inode's bmap btree: max depth * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_attrinval_reservation(
	struct xfs_mount	*mp)
{
	return max((xfs_calc_inode_res(mp, 1) +
		    xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK),
				     XFS_FSB_TO_B(mp, 1))),
		   (xfs_calc_buf_res(9, mp->m_sb.sb_sectsize) +
		    xfs_calc_buf_res(xfs_allocfree_block_count(mp, 4),
				     XFS_FSB_TO_B(mp, 1))));
}

/*
 * Setting an attribute at mount time.
 *	the inode getting the attribute
 *	the superblock for allocations
 *	the agfs extents are allocated from
 *	the attribute btree * max depth
 *	the inode allocation btree
 * Since attribute transaction space is dependent on the size of the attribute,
 * the calculation is done partially at mount time and partially at runtime(see
 * below).
 */
STATIC uint
xfs_calc_attrsetm_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES +
		xfs_calc_inode_res(mp, 1) +
		xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_DA_NODE_MAXDEPTH, XFS_FSB_TO_B(mp, 1));
}

/*
 * Setting an attribute at runtime, transaction space unit per block.
 * 	the superblock for allocations: sector size
 *	the inode bmap btree could join or split: max depth * block size
 * Since the runtime attribute transaction space is dependent on the total
 * blocks needed for the 1st bmap, here we calculate out the space unit for
 * one block so that the caller could figure out the total space according
 * to the attibute extent length in blocks by:
 *	ext * M_RES(mp)->tr_attrsetrt.tr_logres
 */
STATIC uint
xfs_calc_attrsetrt_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize) +
		xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK),
				 XFS_FSB_TO_B(mp, 1));
}

/*
 * Removing an attribute.
 *    the inode: inode size
 *    the attribute btree could join: max depth * block size
 *    the inode bmap btree could join or split: max depth * block size
 * And the bmap_finish transaction can free the attr blocks freed giving:
 *    the agf for the ag in which the blocks live: 2 * sector size
 *    the agfl for the ag in which the blocks live: 2 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_attrrm_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES +
		max((xfs_calc_inode_res(mp, 1) +
		     xfs_calc_buf_res(XFS_DA_NODE_MAXDEPTH,
				      XFS_FSB_TO_B(mp, 1)) +
		     (uint)XFS_FSB_TO_B(mp,
					XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)) +
		     xfs_calc_buf_res(XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK), 0)),
		    (xfs_calc_buf_res(5, mp->m_sb.sb_sectsize) +
		     xfs_calc_buf_res(xfs_allocfree_block_count(mp, 2),
				      XFS_FSB_TO_B(mp, 1))));
}

/*
 * Clearing a bad agino number in an agi hash bucket.
 */
STATIC uint
xfs_calc_clear_agi_bucket_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);
}

/*
 * Adjusting quota limits.
 *    the disk quota buffer: sizeof(struct xfs_disk_dquot)
 */
STATIC uint
xfs_calc_qm_setqlim_reservation(void)
{
	return xfs_calc_buf_res(1, sizeof(struct xfs_disk_dquot));
}

/*
 * Allocating quota on disk if needed.
 *	the write transaction log space for quota file extent allocation
 *	the unit of quota allocation: one system block size
 */
STATIC uint
xfs_calc_qm_dqalloc_reservation(
	struct xfs_mount	*mp,
	bool			for_minlogsize)
{
	return xfs_calc_write_reservation(mp, for_minlogsize) +
		xfs_calc_buf_res(1,
			XFS_FSB_TO_B(mp, XFS_DQUOT_CLUSTER_SIZE_FSB) - 1);
}

unsigned int
xfs_calc_qm_dqalloc_reservation_minlogsize(
	struct xfs_mount	*mp)
{
	return xfs_calc_qm_dqalloc_reservation(mp, true);
}

/*
 * Syncing the incore super block changes to disk.
 *     the super block to reflect the changes: sector size
 */
STATIC uint
xfs_calc_sb_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_buf_res(1, mp->m_sb.sb_sectsize);
}

/*
 * Namespace reservations.
 *
 * These get tricky when parent pointers are enabled as we have attribute
 * modifications occurring from within these transactions. Rather than confuse
 * each of these reservation calculations with the conditional attribute
 * reservations, add them here in a clear and concise manner. This requires that
 * the attribute reservations have already been calculated.
 *
 * Note that we only include the static attribute reservation here; the runtime
 * reservation will have to be modified by the size of the attributes being
 * added/removed/modified. See the comments on the attribute reservation
 * calculations for more details.
 */
STATIC void
xfs_calc_namespace_reservations(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	ASSERT(resp->tr_attrsetm.tr_logres > 0);

	resp->tr_rename.tr_logres = xfs_calc_rename_reservation(mp);
	resp->tr_rename.tr_logcount = xfs_rename_log_count(mp, resp);
	resp->tr_rename.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_link.tr_logres = xfs_calc_link_reservation(mp);
	resp->tr_link.tr_logcount = xfs_link_log_count(mp, resp);
	resp->tr_link.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_remove.tr_logres = xfs_calc_remove_reservation(mp);
	resp->tr_remove.tr_logcount = xfs_remove_log_count(mp, resp);
	resp->tr_remove.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_symlink.tr_logres = xfs_calc_symlink_reservation(mp);
	resp->tr_symlink.tr_logcount = xfs_symlink_log_count(mp, resp);
	resp->tr_symlink.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_create.tr_logres = xfs_calc_icreate_reservation(mp);
	resp->tr_create.tr_logcount = xfs_icreate_log_count(mp, resp);
	resp->tr_create.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_mkdir.tr_logres = xfs_calc_mkdir_reservation(mp);
	resp->tr_mkdir.tr_logcount = xfs_mkdir_log_count(mp, resp);
	resp->tr_mkdir.tr_logflags |= XFS_TRANS_PERM_LOG_RES;
}

void
xfs_trans_resv_calc(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resp)
{
	int			logcount_adj = 0;

	/*
	 * The following transactions are logged in physical format and
	 * require a permanent reservation on space.
	 */
	resp->tr_write.tr_logres = xfs_calc_write_reservation(mp, false);
	resp->tr_write.tr_logcount = XFS_WRITE_LOG_COUNT;
	resp->tr_write.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_itruncate.tr_logres = xfs_calc_itruncate_reservation(mp, false);
	resp->tr_itruncate.tr_logcount = XFS_ITRUNCATE_LOG_COUNT;
	resp->tr_itruncate.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_create_tmpfile.tr_logres =
			xfs_calc_create_tmpfile_reservation(mp);
	resp->tr_create_tmpfile.tr_logcount = XFS_CREATE_TMPFILE_LOG_COUNT;
	resp->tr_create_tmpfile.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_ifree.tr_logres = xfs_calc_ifree_reservation(mp);
	resp->tr_ifree.tr_logcount = XFS_INACTIVE_LOG_COUNT;
	resp->tr_ifree.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_addafork.tr_logres = xfs_calc_addafork_reservation(mp);
	resp->tr_addafork.tr_logcount = XFS_ADDAFORK_LOG_COUNT;
	resp->tr_addafork.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_attrinval.tr_logres = xfs_calc_attrinval_reservation(mp);
	resp->tr_attrinval.tr_logcount = XFS_ATTRINVAL_LOG_COUNT;
	resp->tr_attrinval.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_attrsetm.tr_logres = xfs_calc_attrsetm_reservation(mp);
	resp->tr_attrsetm.tr_logcount = XFS_ATTRSET_LOG_COUNT;
	resp->tr_attrsetm.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_attrrm.tr_logres = xfs_calc_attrrm_reservation(mp);
	resp->tr_attrrm.tr_logcount = XFS_ATTRRM_LOG_COUNT;
	resp->tr_attrrm.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_growrtalloc.tr_logres = xfs_calc_growrtalloc_reservation(mp);
	resp->tr_growrtalloc.tr_logcount = XFS_DEFAULT_PERM_LOG_COUNT;
	resp->tr_growrtalloc.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	resp->tr_qm_dqalloc.tr_logres = xfs_calc_qm_dqalloc_reservation(mp,
			false);
	resp->tr_qm_dqalloc.tr_logcount = XFS_WRITE_LOG_COUNT;
	resp->tr_qm_dqalloc.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	xfs_calc_namespace_reservations(mp, resp);

	/*
	 * The following transactions are logged in logical format with
	 * a default log count.
	 */
	resp->tr_qm_setqlim.tr_logres = xfs_calc_qm_setqlim_reservation();
	resp->tr_qm_setqlim.tr_logcount = XFS_DEFAULT_LOG_COUNT;

	resp->tr_sb.tr_logres = xfs_calc_sb_reservation(mp);
	resp->tr_sb.tr_logcount = XFS_DEFAULT_LOG_COUNT;

	/* growdata requires permanent res; it can free space to the last AG */
	resp->tr_growdata.tr_logres = xfs_calc_growdata_reservation(mp);
	resp->tr_growdata.tr_logcount = XFS_DEFAULT_PERM_LOG_COUNT;
	resp->tr_growdata.tr_logflags |= XFS_TRANS_PERM_LOG_RES;

	/* The following transaction are logged in logical format */
	resp->tr_ichange.tr_logres = xfs_calc_ichange_reservation(mp);
	resp->tr_fsyncts.tr_logres = xfs_calc_swrite_reservation(mp);
	resp->tr_writeid.tr_logres = xfs_calc_writeid_reservation(mp);
	resp->tr_attrsetrt.tr_logres = xfs_calc_attrsetrt_reservation(mp);
	resp->tr_clearagi.tr_logres = xfs_calc_clear_agi_bucket_reservation(mp);
	resp->tr_growrtzero.tr_logres = xfs_calc_growrtzero_reservation(mp);
	resp->tr_growrtfree.tr_logres = xfs_calc_growrtfree_reservation(mp);

	/*
	 * Add one logcount for BUI items that appear with rmap or reflink,
	 * one logcount for refcount intent items, and one logcount for rmap
	 * intent items.
	 */
	if (xfs_has_reflink(mp) || xfs_has_rmapbt(mp))
		logcount_adj++;
	if (xfs_has_reflink(mp))
		logcount_adj++;
	if (xfs_has_rmapbt(mp))
		logcount_adj++;

	resp->tr_itruncate.tr_logcount += logcount_adj;
	resp->tr_write.tr_logcount += logcount_adj;
	resp->tr_qm_dqalloc.tr_logcount += logcount_adj;
}
