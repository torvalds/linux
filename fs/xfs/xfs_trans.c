/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * Copyright (C) 2010 Red Hat, Inc.
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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_trans_priv.h"
#include "xfs_trans_space.h"
#include "xfs_inode_item.h"
#include "xfs_trace.h"

kmem_zone_t	*xfs_trans_zone;
kmem_zone_t	*xfs_log_item_desc_zone;


/*
 * Various log reservation values.
 *
 * These are based on the size of the file system block because that is what
 * most transactions manipulate.  Each adds in an additional 128 bytes per
 * item logged to try to account for the overhead of the transaction mechanism.
 *
 * Note:  Most of the reservations underestimate the number of allocation
 * groups into which they could free extents in the xfs_bmap_finish() call.
 * This is because the number in the worst case is quite high and quite
 * unusual.  In order to fix this we need to change xfs_bmap_finish() to free
 * extents in only a single AG at a time.  This will require changes to the
 * EFI code as well, however, so that the EFI for the extents not freed is
 * logged again in each transaction.  See SGI PV #261917.
 *
 * Reservation functions here avoid a huge stack in xfs_trans_init due to
 * register overflow from temporaries in the calculations.
 */


/*
 * In a write transaction we can allocate a maximum of 2
 * extents.  This gives:
 *    the inode getting the new extents: inode size
 *    the inode's bmap btree: max depth * block size
 *    the agfs of the ags from which the extents are allocated: 2 * sector
 *    the superblock free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 * And the bmap_finish transaction can free bmap blocks in a join:
 *    the agfs of the ags containing the blocks: 2 * sector size
 *    the agfls of the ags containing the blocks: 2 * sector size
 *    the super block free block counter: sector size
 *    the allocation btrees: 2 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_write_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     XFS_FSB_TO_B(mp, XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK)) +
		     2 * mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 2) +
		     128 * (4 + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) +
			    XFS_ALLOCFREE_LOG_COUNT(mp, 2))),
		    (2 * mp->m_sb.sb_sectsize +
		     2 * mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 2) +
		     128 * (5 + XFS_ALLOCFREE_LOG_COUNT(mp, 2))));
}

/*
 * In truncating a file we free up to two extents at once.  We can modify:
 *    the inode being truncated: inode size
 *    the inode's bmap btree: (max depth + 1) * block size
 * And the bmap_finish transaction can free the blocks and bmap blocks:
 *    the agf for each of the ags: 4 * sector size
 *    the agfl for each of the ags: 4 * sector size
 *    the super block to reflect the freed blocks: sector size
 *    worst case split in allocation btrees per extent assuming 4 extents:
 *		4 exts * 2 trees * (2 * max depth - 1) * block size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_itruncate_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     XFS_FSB_TO_B(mp, XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) + 1) +
		     128 * (2 + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK))),
		    (4 * mp->m_sb.sb_sectsize +
		     4 * mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 4) +
		     128 * (9 + XFS_ALLOCFREE_LOG_COUNT(mp, 4)) +
		     128 * 5 +
		     XFS_ALLOCFREE_LOG_RES(mp, 1) +
		     128 * (2 + XFS_IALLOC_BLOCKS(mp) + mp->m_in_maxlevels +
			    XFS_ALLOCFREE_LOG_COUNT(mp, 1))));
}

/*
 * In renaming a files we can modify:
 *    the four inodes involved: 4 * inode size
 *    the two directory btrees: 2 * (max depth + v2) * dir block size
 *    the two directory bmap btrees: 2 * max depth * block size
 * And the bmap_finish transaction can free dir and bmap blocks (two sets
 *	of bmap blocks) giving:
 *    the agf for the ags in which the blocks live: 3 * sector size
 *    the agfl for the ags in which the blocks live: 3 * sector size
 *    the superblock for the free block count: sector size
 *    the allocation btrees: 3 exts * 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_rename_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((4 * mp->m_sb.sb_inodesize +
		     2 * XFS_DIROP_LOG_RES(mp) +
		     128 * (4 + 2 * XFS_DIROP_LOG_COUNT(mp))),
		    (3 * mp->m_sb.sb_sectsize +
		     3 * mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 3) +
		     128 * (7 + XFS_ALLOCFREE_LOG_COUNT(mp, 3))));
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
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     mp->m_sb.sb_inodesize +
		     XFS_DIROP_LOG_RES(mp) +
		     128 * (2 + XFS_DIROP_LOG_COUNT(mp))),
		    (mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 1) +
		     128 * (3 + XFS_ALLOCFREE_LOG_COUNT(mp, 1))));
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
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     mp->m_sb.sb_inodesize +
		     XFS_DIROP_LOG_RES(mp) +
		     128 * (2 + XFS_DIROP_LOG_COUNT(mp))),
		    (2 * mp->m_sb.sb_sectsize +
		     2 * mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 2) +
		     128 * (5 + XFS_ALLOCFREE_LOG_COUNT(mp, 2))));
}

/*
 * For symlink we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: 1 block
 *    the directory btree: (max depth + v2) * dir block size
 *    the directory inode's bmap btree: (max depth + v2) * block size
 *    the blocks for the symlink: 1 kB
 * Or in the first xact we allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (2 * max depth - 1) * block size
 */
STATIC uint
xfs_calc_symlink_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     mp->m_sb.sb_inodesize +
		     XFS_FSB_TO_B(mp, 1) +
		     XFS_DIROP_LOG_RES(mp) +
		     1024 +
		     128 * (4 + XFS_DIROP_LOG_COUNT(mp))),
		    (2 * mp->m_sb.sb_sectsize +
		     XFS_FSB_TO_B(mp, XFS_IALLOC_BLOCKS(mp)) +
		     XFS_FSB_TO_B(mp, mp->m_in_maxlevels) +
		     XFS_ALLOCFREE_LOG_RES(mp, 1) +
		     128 * (2 + XFS_IALLOC_BLOCKS(mp) + mp->m_in_maxlevels +
			    XFS_ALLOCFREE_LOG_COUNT(mp, 1))));
}

/*
 * For create we can modify:
 *    the parent directory inode: inode size
 *    the new inode: inode size
 *    the inode btree entry: block size
 *    the superblock for the nlink flag: sector size
 *    the directory btree: (max depth + v2) * dir block size
 *    the directory inode's bmap btree: (max depth + v2) * block size
 * Or in the first xact we allocate some inodes giving:
 *    the agi and agf of the ag getting the new inodes: 2 * sectorsize
 *    the superblock for the nlink flag: sector size
 *    the inode blocks allocated: XFS_IALLOC_BLOCKS * blocksize
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_create_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     mp->m_sb.sb_inodesize +
		     mp->m_sb.sb_sectsize +
		     XFS_FSB_TO_B(mp, 1) +
		     XFS_DIROP_LOG_RES(mp) +
		     128 * (3 + XFS_DIROP_LOG_COUNT(mp))),
		    (3 * mp->m_sb.sb_sectsize +
		     XFS_FSB_TO_B(mp, XFS_IALLOC_BLOCKS(mp)) +
		     XFS_FSB_TO_B(mp, mp->m_in_maxlevels) +
		     XFS_ALLOCFREE_LOG_RES(mp, 1) +
		     128 * (2 + XFS_IALLOC_BLOCKS(mp) + mp->m_in_maxlevels +
			    XFS_ALLOCFREE_LOG_COUNT(mp, 1))));
}

/*
 * Making a new directory is the same as creating a new file.
 */
STATIC uint
xfs_calc_mkdir_reservation(
	struct xfs_mount	*mp)
{
	return xfs_calc_create_reservation(mp);
}

/*
 * In freeing an inode we can modify:
 *    the inode being freed: inode size
 *    the super block free inode counter: sector size
 *    the agi hash list and counters: sector size
 *    the inode btree entry: block size
 *    the on disk inode before ours in the agi hash list: inode cluster size
 *    the inode btree: max depth * blocksize
 *    the allocation btrees: 2 trees * (max depth - 1) * block size
 */
STATIC uint
xfs_calc_ifree_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		mp->m_sb.sb_inodesize +
		mp->m_sb.sb_sectsize +
		mp->m_sb.sb_sectsize +
		XFS_FSB_TO_B(mp, 1) +
		MAX((__uint16_t)XFS_FSB_TO_B(mp, 1),
		    XFS_INODE_CLUSTER_SIZE(mp)) +
		128 * 5 +
		XFS_ALLOCFREE_LOG_RES(mp, 1) +
		128 * (2 + XFS_IALLOC_BLOCKS(mp) + mp->m_in_maxlevels +
		       XFS_ALLOCFREE_LOG_COUNT(mp, 1));
}

/*
 * When only changing the inode we log the inode and possibly the superblock
 * We also add a bit of slop for the transaction stuff.
 */
STATIC uint
xfs_calc_ichange_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		mp->m_sb.sb_inodesize +
		mp->m_sb.sb_sectsize +
		512;

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
	return mp->m_sb.sb_sectsize * 3 +
		XFS_ALLOCFREE_LOG_RES(mp, 1) +
		128 * (3 + XFS_ALLOCFREE_LOG_COUNT(mp, 1));
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
	return 2 * mp->m_sb.sb_sectsize +
		XFS_FSB_TO_B(mp, XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK)) +
		mp->m_sb.sb_inodesize +
		XFS_ALLOCFREE_LOG_RES(mp, 1) +
		128 * (3 + XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK) +
		       XFS_ALLOCFREE_LOG_COUNT(mp, 1));
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
	return mp->m_sb.sb_blocksize + 128;
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
	return mp->m_sb.sb_sectsize +
		2 * mp->m_sb.sb_inodesize +
		mp->m_sb.sb_blocksize +
		mp->m_rsumsize +
		128 * 5;
}

/*
 * Logging the inode modification timestamp on a synchronous write.
 *	inode
 */
STATIC uint
xfs_calc_swrite_reservation(
	struct xfs_mount	*mp)
{
	return mp->m_sb.sb_inodesize + 128;
}

/*
 * Logging the inode mode bits when writing a setuid/setgid file
 *	inode
 */
STATIC uint
xfs_calc_writeid_reservation(xfs_mount_t *mp)
{
	return mp->m_sb.sb_inodesize + 128;
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
	return XFS_DQUOT_LOGRES(mp) +
		mp->m_sb.sb_inodesize +
		mp->m_sb.sb_sectsize * 2 +
		mp->m_dirblksize +
		XFS_FSB_TO_B(mp, XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK) + 1) +
		XFS_ALLOCFREE_LOG_RES(mp, 1) +
		128 * (4 + XFS_DAENTER_BMAP1B(mp, XFS_DATA_FORK) + 1 +
		       XFS_ALLOCFREE_LOG_COUNT(mp, 1));
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
	return MAX((mp->m_sb.sb_inodesize +
		    XFS_FSB_TO_B(mp, XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)) +
		    128 * (1 + XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK))),
		   (4 * mp->m_sb.sb_sectsize +
		    4 * mp->m_sb.sb_sectsize +
		    mp->m_sb.sb_sectsize +
		    XFS_ALLOCFREE_LOG_RES(mp, 4) +
		    128 * (9 + XFS_ALLOCFREE_LOG_COUNT(mp, 4))));
}

/*
 * Setting an attribute.
 *	the inode getting the attribute
 *	the superblock for allocations
 *	the agfs extents are allocated from
 *	the attribute btree * max depth
 *	the inode allocation btree
 * Since attribute transaction space is dependent on the size of the attribute,
 * the calculation is done partially at mount time and partially at runtime.
 */
STATIC uint
xfs_calc_attrset_reservation(
	struct xfs_mount	*mp)
{
	return XFS_DQUOT_LOGRES(mp) +
		mp->m_sb.sb_inodesize +
		mp->m_sb.sb_sectsize +
		XFS_FSB_TO_B(mp, XFS_DA_NODE_MAXDEPTH) +
		128 * (2 + XFS_DA_NODE_MAXDEPTH);
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
	return XFS_DQUOT_LOGRES(mp) +
		MAX((mp->m_sb.sb_inodesize +
		     XFS_FSB_TO_B(mp, XFS_DA_NODE_MAXDEPTH) +
		     XFS_FSB_TO_B(mp, XFS_BM_MAXLEVELS(mp, XFS_ATTR_FORK)) +
		     128 * (1 + XFS_DA_NODE_MAXDEPTH +
			    XFS_BM_MAXLEVELS(mp, XFS_DATA_FORK))),
		    (2 * mp->m_sb.sb_sectsize +
		     2 * mp->m_sb.sb_sectsize +
		     mp->m_sb.sb_sectsize +
		     XFS_ALLOCFREE_LOG_RES(mp, 2) +
		     128 * (5 + XFS_ALLOCFREE_LOG_COUNT(mp, 2))));
}

/*
 * Clearing a bad agino number in an agi hash bucket.
 */
STATIC uint
xfs_calc_clear_agi_bucket_reservation(
	struct xfs_mount	*mp)
{
	return mp->m_sb.sb_sectsize + 128;
}

/*
 * Initialize the precomputed transaction reservation values
 * in the mount structure.
 */
void
xfs_trans_init(
	struct xfs_mount	*mp)
{
	struct xfs_trans_reservations *resp = &mp->m_reservations;

	resp->tr_write = xfs_calc_write_reservation(mp);
	resp->tr_itruncate = xfs_calc_itruncate_reservation(mp);
	resp->tr_rename = xfs_calc_rename_reservation(mp);
	resp->tr_link = xfs_calc_link_reservation(mp);
	resp->tr_remove = xfs_calc_remove_reservation(mp);
	resp->tr_symlink = xfs_calc_symlink_reservation(mp);
	resp->tr_create = xfs_calc_create_reservation(mp);
	resp->tr_mkdir = xfs_calc_mkdir_reservation(mp);
	resp->tr_ifree = xfs_calc_ifree_reservation(mp);
	resp->tr_ichange = xfs_calc_ichange_reservation(mp);
	resp->tr_growdata = xfs_calc_growdata_reservation(mp);
	resp->tr_swrite = xfs_calc_swrite_reservation(mp);
	resp->tr_writeid = xfs_calc_writeid_reservation(mp);
	resp->tr_addafork = xfs_calc_addafork_reservation(mp);
	resp->tr_attrinval = xfs_calc_attrinval_reservation(mp);
	resp->tr_attrset = xfs_calc_attrset_reservation(mp);
	resp->tr_attrrm = xfs_calc_attrrm_reservation(mp);
	resp->tr_clearagi = xfs_calc_clear_agi_bucket_reservation(mp);
	resp->tr_growrtalloc = xfs_calc_growrtalloc_reservation(mp);
	resp->tr_growrtzero = xfs_calc_growrtzero_reservation(mp);
	resp->tr_growrtfree = xfs_calc_growrtfree_reservation(mp);
}

/*
 * This routine is called to allocate a transaction structure.
 * The type parameter indicates the type of the transaction.  These
 * are enumerated in xfs_trans.h.
 *
 * Dynamically allocate the transaction structure from the transaction
 * zone, initialize it, and return it to the caller.
 */
xfs_trans_t *
xfs_trans_alloc(
	xfs_mount_t	*mp,
	uint		type)
{
	xfs_wait_for_freeze(mp, SB_FREEZE_TRANS);
	return _xfs_trans_alloc(mp, type, KM_SLEEP);
}

xfs_trans_t *
_xfs_trans_alloc(
	xfs_mount_t	*mp,
	uint		type,
	uint		memflags)
{
	xfs_trans_t	*tp;

	atomic_inc(&mp->m_active_trans);

	tp = kmem_zone_zalloc(xfs_trans_zone, memflags);
	tp->t_magic = XFS_TRANS_MAGIC;
	tp->t_type = type;
	tp->t_mountp = mp;
	INIT_LIST_HEAD(&tp->t_items);
	INIT_LIST_HEAD(&tp->t_busy);
	return tp;
}

/*
 * Free the transaction structure.  If there is more clean up
 * to do when the structure is freed, add it here.
 */
STATIC void
xfs_trans_free(
	struct xfs_trans	*tp)
{
	xfs_alloc_busy_sort(&tp->t_busy);
	xfs_alloc_busy_clear(tp->t_mountp, &tp->t_busy, false);

	atomic_dec(&tp->t_mountp->m_active_trans);
	xfs_trans_free_dqinfo(tp);
	kmem_zone_free(xfs_trans_zone, tp);
}

/*
 * This is called to create a new transaction which will share the
 * permanent log reservation of the given transaction.  The remaining
 * unused block and rt extent reservations are also inherited.  This
 * implies that the original transaction is no longer allowed to allocate
 * blocks.  Locks and log items, however, are no inherited.  They must
 * be added to the new transaction explicitly.
 */
xfs_trans_t *
xfs_trans_dup(
	xfs_trans_t	*tp)
{
	xfs_trans_t	*ntp;

	ntp = kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);

	/*
	 * Initialize the new transaction structure.
	 */
	ntp->t_magic = XFS_TRANS_MAGIC;
	ntp->t_type = tp->t_type;
	ntp->t_mountp = tp->t_mountp;
	INIT_LIST_HEAD(&ntp->t_items);
	INIT_LIST_HEAD(&ntp->t_busy);

	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(tp->t_ticket != NULL);

	ntp->t_flags = XFS_TRANS_PERM_LOG_RES | (tp->t_flags & XFS_TRANS_RESERVE);
	ntp->t_ticket = xfs_log_ticket_get(tp->t_ticket);
	ntp->t_blk_res = tp->t_blk_res - tp->t_blk_res_used;
	tp->t_blk_res = tp->t_blk_res_used;
	ntp->t_rtx_res = tp->t_rtx_res - tp->t_rtx_res_used;
	tp->t_rtx_res = tp->t_rtx_res_used;
	ntp->t_pflags = tp->t_pflags;

	xfs_trans_dup_dqinfo(tp, ntp);

	atomic_inc(&tp->t_mountp->m_active_trans);
	return ntp;
}

/*
 * This is called to reserve free disk blocks and log space for the
 * given transaction.  This must be done before allocating any resources
 * within the transaction.
 *
 * This will return ENOSPC if there are not enough blocks available.
 * It will sleep waiting for available log space.
 * The only valid value for the flags parameter is XFS_RES_LOG_PERM, which
 * is used by long running transactions.  If any one of the reservations
 * fails then they will all be backed out.
 *
 * This does not do quota reservations. That typically is done by the
 * caller afterwards.
 */
int
xfs_trans_reserve(
	xfs_trans_t	*tp,
	uint		blocks,
	uint		logspace,
	uint		rtextents,
	uint		flags,
	uint		logcount)
{
	int		log_flags;
	int		error = 0;
	int		rsvd = (tp->t_flags & XFS_TRANS_RESERVE) != 0;

	/* Mark this thread as being in a transaction */
	current_set_flags_nested(&tp->t_pflags, PF_FSTRANS);

	/*
	 * Attempt to reserve the needed disk blocks by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (blocks > 0) {
		error = xfs_icsb_modify_counters(tp->t_mountp, XFS_SBS_FDBLOCKS,
					  -((int64_t)blocks), rsvd);
		if (error != 0) {
			current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
			return (XFS_ERROR(ENOSPC));
		}
		tp->t_blk_res += blocks;
	}

	/*
	 * Reserve the log space needed for this transaction.
	 */
	if (logspace > 0) {
		ASSERT((tp->t_log_res == 0) || (tp->t_log_res == logspace));
		ASSERT((tp->t_log_count == 0) ||
			(tp->t_log_count == logcount));
		if (flags & XFS_TRANS_PERM_LOG_RES) {
			log_flags = XFS_LOG_PERM_RESERV;
			tp->t_flags |= XFS_TRANS_PERM_LOG_RES;
		} else {
			ASSERT(tp->t_ticket == NULL);
			ASSERT(!(tp->t_flags & XFS_TRANS_PERM_LOG_RES));
			log_flags = 0;
		}

		error = xfs_log_reserve(tp->t_mountp, logspace, logcount,
					&tp->t_ticket,
					XFS_TRANSACTION, log_flags, tp->t_type);
		if (error) {
			goto undo_blocks;
		}
		tp->t_log_res = logspace;
		tp->t_log_count = logcount;
	}

	/*
	 * Attempt to reserve the needed realtime extents by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (rtextents > 0) {
		error = xfs_mod_incore_sb(tp->t_mountp, XFS_SBS_FREXTENTS,
					  -((int64_t)rtextents), rsvd);
		if (error) {
			error = XFS_ERROR(ENOSPC);
			goto undo_log;
		}
		tp->t_rtx_res += rtextents;
	}

	return 0;

	/*
	 * Error cases jump to one of these labels to undo any
	 * reservations which have already been performed.
	 */
undo_log:
	if (logspace > 0) {
		if (flags & XFS_TRANS_PERM_LOG_RES) {
			log_flags = XFS_LOG_REL_PERM_RESERV;
		} else {
			log_flags = 0;
		}
		xfs_log_done(tp->t_mountp, tp->t_ticket, NULL, log_flags);
		tp->t_ticket = NULL;
		tp->t_log_res = 0;
		tp->t_flags &= ~XFS_TRANS_PERM_LOG_RES;
	}

undo_blocks:
	if (blocks > 0) {
		xfs_icsb_modify_counters(tp->t_mountp, XFS_SBS_FDBLOCKS,
					 (int64_t)blocks, rsvd);
		tp->t_blk_res = 0;
	}

	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);

	return error;
}

/*
 * Record the indicated change to the given field for application
 * to the file system's superblock when the transaction commits.
 * For now, just store the change in the transaction structure.
 *
 * Mark the transaction structure to indicate that the superblock
 * needs to be updated before committing.
 *
 * Because we may not be keeping track of allocated/free inodes and
 * used filesystem blocks in the superblock, we do not mark the
 * superblock dirty in this transaction if we modify these fields.
 * We still need to update the transaction deltas so that they get
 * applied to the incore superblock, but we don't want them to
 * cause the superblock to get locked and logged if these are the
 * only fields in the superblock that the transaction modifies.
 */
void
xfs_trans_mod_sb(
	xfs_trans_t	*tp,
	uint		field,
	int64_t		delta)
{
	uint32_t	flags = (XFS_TRANS_DIRTY|XFS_TRANS_SB_DIRTY);
	xfs_mount_t	*mp = tp->t_mountp;

	switch (field) {
	case XFS_TRANS_SB_ICOUNT:
		tp->t_icount_delta += delta;
		if (xfs_sb_version_haslazysbcount(&mp->m_sb))
			flags &= ~XFS_TRANS_SB_DIRTY;
		break;
	case XFS_TRANS_SB_IFREE:
		tp->t_ifree_delta += delta;
		if (xfs_sb_version_haslazysbcount(&mp->m_sb))
			flags &= ~XFS_TRANS_SB_DIRTY;
		break;
	case XFS_TRANS_SB_FDBLOCKS:
		/*
		 * Track the number of blocks allocated in the
		 * transaction.  Make sure it does not exceed the
		 * number reserved.
		 */
		if (delta < 0) {
			tp->t_blk_res_used += (uint)-delta;
			ASSERT(tp->t_blk_res_used <= tp->t_blk_res);
		}
		tp->t_fdblocks_delta += delta;
		if (xfs_sb_version_haslazysbcount(&mp->m_sb))
			flags &= ~XFS_TRANS_SB_DIRTY;
		break;
	case XFS_TRANS_SB_RES_FDBLOCKS:
		/*
		 * The allocation has already been applied to the
		 * in-core superblock's counter.  This should only
		 * be applied to the on-disk superblock.
		 */
		ASSERT(delta < 0);
		tp->t_res_fdblocks_delta += delta;
		if (xfs_sb_version_haslazysbcount(&mp->m_sb))
			flags &= ~XFS_TRANS_SB_DIRTY;
		break;
	case XFS_TRANS_SB_FREXTENTS:
		/*
		 * Track the number of blocks allocated in the
		 * transaction.  Make sure it does not exceed the
		 * number reserved.
		 */
		if (delta < 0) {
			tp->t_rtx_res_used += (uint)-delta;
			ASSERT(tp->t_rtx_res_used <= tp->t_rtx_res);
		}
		tp->t_frextents_delta += delta;
		break;
	case XFS_TRANS_SB_RES_FREXTENTS:
		/*
		 * The allocation has already been applied to the
		 * in-core superblock's counter.  This should only
		 * be applied to the on-disk superblock.
		 */
		ASSERT(delta < 0);
		tp->t_res_frextents_delta += delta;
		break;
	case XFS_TRANS_SB_DBLOCKS:
		ASSERT(delta > 0);
		tp->t_dblocks_delta += delta;
		break;
	case XFS_TRANS_SB_AGCOUNT:
		ASSERT(delta > 0);
		tp->t_agcount_delta += delta;
		break;
	case XFS_TRANS_SB_IMAXPCT:
		tp->t_imaxpct_delta += delta;
		break;
	case XFS_TRANS_SB_REXTSIZE:
		tp->t_rextsize_delta += delta;
		break;
	case XFS_TRANS_SB_RBMBLOCKS:
		tp->t_rbmblocks_delta += delta;
		break;
	case XFS_TRANS_SB_RBLOCKS:
		tp->t_rblocks_delta += delta;
		break;
	case XFS_TRANS_SB_REXTENTS:
		tp->t_rextents_delta += delta;
		break;
	case XFS_TRANS_SB_REXTSLOG:
		tp->t_rextslog_delta += delta;
		break;
	default:
		ASSERT(0);
		return;
	}

	tp->t_flags |= flags;
}

/*
 * xfs_trans_apply_sb_deltas() is called from the commit code
 * to bring the superblock buffer into the current transaction
 * and modify it as requested by earlier calls to xfs_trans_mod_sb().
 *
 * For now we just look at each field allowed to change and change
 * it if necessary.
 */
STATIC void
xfs_trans_apply_sb_deltas(
	xfs_trans_t	*tp)
{
	xfs_dsb_t	*sbp;
	xfs_buf_t	*bp;
	int		whole = 0;

	bp = xfs_trans_getsb(tp, tp->t_mountp, 0);
	sbp = XFS_BUF_TO_SBP(bp);

	/*
	 * Check that superblock mods match the mods made to AGF counters.
	 */
	ASSERT((tp->t_fdblocks_delta + tp->t_res_fdblocks_delta) ==
	       (tp->t_ag_freeblks_delta + tp->t_ag_flist_delta +
		tp->t_ag_btree_delta));

	/*
	 * Only update the superblock counters if we are logging them
	 */
	if (!xfs_sb_version_haslazysbcount(&(tp->t_mountp->m_sb))) {
		if (tp->t_icount_delta)
			be64_add_cpu(&sbp->sb_icount, tp->t_icount_delta);
		if (tp->t_ifree_delta)
			be64_add_cpu(&sbp->sb_ifree, tp->t_ifree_delta);
		if (tp->t_fdblocks_delta)
			be64_add_cpu(&sbp->sb_fdblocks, tp->t_fdblocks_delta);
		if (tp->t_res_fdblocks_delta)
			be64_add_cpu(&sbp->sb_fdblocks, tp->t_res_fdblocks_delta);
	}

	if (tp->t_frextents_delta)
		be64_add_cpu(&sbp->sb_frextents, tp->t_frextents_delta);
	if (tp->t_res_frextents_delta)
		be64_add_cpu(&sbp->sb_frextents, tp->t_res_frextents_delta);

	if (tp->t_dblocks_delta) {
		be64_add_cpu(&sbp->sb_dblocks, tp->t_dblocks_delta);
		whole = 1;
	}
	if (tp->t_agcount_delta) {
		be32_add_cpu(&sbp->sb_agcount, tp->t_agcount_delta);
		whole = 1;
	}
	if (tp->t_imaxpct_delta) {
		sbp->sb_imax_pct += tp->t_imaxpct_delta;
		whole = 1;
	}
	if (tp->t_rextsize_delta) {
		be32_add_cpu(&sbp->sb_rextsize, tp->t_rextsize_delta);
		whole = 1;
	}
	if (tp->t_rbmblocks_delta) {
		be32_add_cpu(&sbp->sb_rbmblocks, tp->t_rbmblocks_delta);
		whole = 1;
	}
	if (tp->t_rblocks_delta) {
		be64_add_cpu(&sbp->sb_rblocks, tp->t_rblocks_delta);
		whole = 1;
	}
	if (tp->t_rextents_delta) {
		be64_add_cpu(&sbp->sb_rextents, tp->t_rextents_delta);
		whole = 1;
	}
	if (tp->t_rextslog_delta) {
		sbp->sb_rextslog += tp->t_rextslog_delta;
		whole = 1;
	}

	if (whole)
		/*
		 * Log the whole thing, the fields are noncontiguous.
		 */
		xfs_trans_log_buf(tp, bp, 0, sizeof(xfs_dsb_t) - 1);
	else
		/*
		 * Since all the modifiable fields are contiguous, we
		 * can get away with this.
		 */
		xfs_trans_log_buf(tp, bp, offsetof(xfs_dsb_t, sb_icount),
				  offsetof(xfs_dsb_t, sb_frextents) +
				  sizeof(sbp->sb_frextents) - 1);
}

/*
 * xfs_trans_unreserve_and_mod_sb() is called to release unused reservations
 * and apply superblock counter changes to the in-core superblock.  The
 * t_res_fdblocks_delta and t_res_frextents_delta fields are explicitly NOT
 * applied to the in-core superblock.  The idea is that that has already been
 * done.
 *
 * This is done efficiently with a single call to xfs_mod_incore_sb_batch().
 * However, we have to ensure that we only modify each superblock field only
 * once because the application of the delta values may not be atomic. That can
 * lead to ENOSPC races occurring if we have two separate modifcations of the
 * free space counter to put back the entire reservation and then take away
 * what we used.
 *
 * If we are not logging superblock counters, then the inode allocated/free and
 * used block counts are not updated in the on disk superblock. In this case,
 * XFS_TRANS_SB_DIRTY will not be set when the transaction is updated but we
 * still need to update the incore superblock with the changes.
 */
void
xfs_trans_unreserve_and_mod_sb(
	xfs_trans_t	*tp)
{
	xfs_mod_sb_t	msb[9];	/* If you add cases, add entries */
	xfs_mod_sb_t	*msbp;
	xfs_mount_t	*mp = tp->t_mountp;
	/* REFERENCED */
	int		error;
	int		rsvd;
	int64_t		blkdelta = 0;
	int64_t		rtxdelta = 0;
	int64_t		idelta = 0;
	int64_t		ifreedelta = 0;

	msbp = msb;
	rsvd = (tp->t_flags & XFS_TRANS_RESERVE) != 0;

	/* calculate deltas */
	if (tp->t_blk_res > 0)
		blkdelta = tp->t_blk_res;
	if ((tp->t_fdblocks_delta != 0) &&
	    (xfs_sb_version_haslazysbcount(&mp->m_sb) ||
	     (tp->t_flags & XFS_TRANS_SB_DIRTY)))
	        blkdelta += tp->t_fdblocks_delta;

	if (tp->t_rtx_res > 0)
		rtxdelta = tp->t_rtx_res;
	if ((tp->t_frextents_delta != 0) &&
	    (tp->t_flags & XFS_TRANS_SB_DIRTY))
		rtxdelta += tp->t_frextents_delta;

	if (xfs_sb_version_haslazysbcount(&mp->m_sb) ||
	     (tp->t_flags & XFS_TRANS_SB_DIRTY)) {
		idelta = tp->t_icount_delta;
		ifreedelta = tp->t_ifree_delta;
	}

	/* apply the per-cpu counters */
	if (blkdelta) {
		error = xfs_icsb_modify_counters(mp, XFS_SBS_FDBLOCKS,
						 blkdelta, rsvd);
		if (error)
			goto out;
	}

	if (idelta) {
		error = xfs_icsb_modify_counters(mp, XFS_SBS_ICOUNT,
						 idelta, rsvd);
		if (error)
			goto out_undo_fdblocks;
	}

	if (ifreedelta) {
		error = xfs_icsb_modify_counters(mp, XFS_SBS_IFREE,
						 ifreedelta, rsvd);
		if (error)
			goto out_undo_icount;
	}

	/* apply remaining deltas */
	if (rtxdelta != 0) {
		msbp->msb_field = XFS_SBS_FREXTENTS;
		msbp->msb_delta = rtxdelta;
		msbp++;
	}

	if (tp->t_flags & XFS_TRANS_SB_DIRTY) {
		if (tp->t_dblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_DBLOCKS;
			msbp->msb_delta = tp->t_dblocks_delta;
			msbp++;
		}
		if (tp->t_agcount_delta != 0) {
			msbp->msb_field = XFS_SBS_AGCOUNT;
			msbp->msb_delta = tp->t_agcount_delta;
			msbp++;
		}
		if (tp->t_imaxpct_delta != 0) {
			msbp->msb_field = XFS_SBS_IMAX_PCT;
			msbp->msb_delta = tp->t_imaxpct_delta;
			msbp++;
		}
		if (tp->t_rextsize_delta != 0) {
			msbp->msb_field = XFS_SBS_REXTSIZE;
			msbp->msb_delta = tp->t_rextsize_delta;
			msbp++;
		}
		if (tp->t_rbmblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_RBMBLOCKS;
			msbp->msb_delta = tp->t_rbmblocks_delta;
			msbp++;
		}
		if (tp->t_rblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_RBLOCKS;
			msbp->msb_delta = tp->t_rblocks_delta;
			msbp++;
		}
		if (tp->t_rextents_delta != 0) {
			msbp->msb_field = XFS_SBS_REXTENTS;
			msbp->msb_delta = tp->t_rextents_delta;
			msbp++;
		}
		if (tp->t_rextslog_delta != 0) {
			msbp->msb_field = XFS_SBS_REXTSLOG;
			msbp->msb_delta = tp->t_rextslog_delta;
			msbp++;
		}
	}

	/*
	 * If we need to change anything, do it.
	 */
	if (msbp > msb) {
		error = xfs_mod_incore_sb_batch(tp->t_mountp, msb,
			(uint)(msbp - msb), rsvd);
		if (error)
			goto out_undo_ifreecount;
	}

	return;

out_undo_ifreecount:
	if (ifreedelta)
		xfs_icsb_modify_counters(mp, XFS_SBS_IFREE, -ifreedelta, rsvd);
out_undo_icount:
	if (idelta)
		xfs_icsb_modify_counters(mp, XFS_SBS_ICOUNT, -idelta, rsvd);
out_undo_fdblocks:
	if (blkdelta)
		xfs_icsb_modify_counters(mp, XFS_SBS_FDBLOCKS, -blkdelta, rsvd);
out:
	ASSERT(error == 0);
	return;
}

/*
 * Add the given log item to the transaction's list of log items.
 *
 * The log item will now point to its new descriptor with its li_desc field.
 */
void
xfs_trans_add_item(
	struct xfs_trans	*tp,
	struct xfs_log_item	*lip)
{
	struct xfs_log_item_desc *lidp;

	ASSERT(lip->li_mountp = tp->t_mountp);
	ASSERT(lip->li_ailp = tp->t_mountp->m_ail);

	lidp = kmem_zone_zalloc(xfs_log_item_desc_zone, KM_SLEEP | KM_NOFS);

	lidp->lid_item = lip;
	lidp->lid_flags = 0;
	lidp->lid_size = 0;
	list_add_tail(&lidp->lid_trans, &tp->t_items);

	lip->li_desc = lidp;
}

STATIC void
xfs_trans_free_item_desc(
	struct xfs_log_item_desc *lidp)
{
	list_del_init(&lidp->lid_trans);
	kmem_zone_free(xfs_log_item_desc_zone, lidp);
}

/*
 * Unlink and free the given descriptor.
 */
void
xfs_trans_del_item(
	struct xfs_log_item	*lip)
{
	xfs_trans_free_item_desc(lip->li_desc);
	lip->li_desc = NULL;
}

/*
 * Unlock all of the items of a transaction and free all the descriptors
 * of that transaction.
 */
void
xfs_trans_free_items(
	struct xfs_trans	*tp,
	xfs_lsn_t		commit_lsn,
	int			flags)
{
	struct xfs_log_item_desc *lidp, *next;

	list_for_each_entry_safe(lidp, next, &tp->t_items, lid_trans) {
		struct xfs_log_item	*lip = lidp->lid_item;

		lip->li_desc = NULL;

		if (commit_lsn != NULLCOMMITLSN)
			IOP_COMMITTING(lip, commit_lsn);
		if (flags & XFS_TRANS_ABORT)
			lip->li_flags |= XFS_LI_ABORTED;
		IOP_UNLOCK(lip);

		xfs_trans_free_item_desc(lidp);
	}
}

/*
 * Unlock the items associated with a transaction.
 *
 * Items which were not logged should be freed.  Those which were logged must
 * still be tracked so they can be unpinned when the transaction commits.
 */
STATIC void
xfs_trans_unlock_items(
	struct xfs_trans	*tp,
	xfs_lsn_t		commit_lsn)
{
	struct xfs_log_item_desc *lidp, *next;

	list_for_each_entry_safe(lidp, next, &tp->t_items, lid_trans) {
		struct xfs_log_item	*lip = lidp->lid_item;

		lip->li_desc = NULL;

		if (commit_lsn != NULLCOMMITLSN)
			IOP_COMMITTING(lip, commit_lsn);
		IOP_UNLOCK(lip);

		/*
		 * Free the descriptor if the item is not dirty
		 * within this transaction.
		 */
		if (!(lidp->lid_flags & XFS_LID_DIRTY))
			xfs_trans_free_item_desc(lidp);
	}
}

/*
 * Total up the number of log iovecs needed to commit this
 * transaction.  The transaction itself needs one for the
 * transaction header.  Ask each dirty item in turn how many
 * it needs to get the total.
 */
static uint
xfs_trans_count_vecs(
	struct xfs_trans	*tp)
{
	int			nvecs;
	struct xfs_log_item_desc *lidp;

	nvecs = 1;

	/* In the non-debug case we need to start bailing out if we
	 * didn't find a log_item here, return zero and let trans_commit
	 * deal with it.
	 */
	if (list_empty(&tp->t_items)) {
		ASSERT(0);
		return 0;
	}

	list_for_each_entry(lidp, &tp->t_items, lid_trans) {
		/*
		 * Skip items which aren't dirty in this transaction.
		 */
		if (!(lidp->lid_flags & XFS_LID_DIRTY))
			continue;
		lidp->lid_size = IOP_SIZE(lidp->lid_item);
		nvecs += lidp->lid_size;
	}

	return nvecs;
}

/*
 * Fill in the vector with pointers to data to be logged
 * by this transaction.  The transaction header takes
 * the first vector, and then each dirty item takes the
 * number of vectors it indicated it needed in xfs_trans_count_vecs().
 *
 * As each item fills in the entries it needs, also pin the item
 * so that it cannot be flushed out until the log write completes.
 */
static void
xfs_trans_fill_vecs(
	struct xfs_trans	*tp,
	struct xfs_log_iovec	*log_vector)
{
	struct xfs_log_item_desc *lidp;
	struct xfs_log_iovec	*vecp;
	uint			nitems;

	/*
	 * Skip over the entry for the transaction header, we'll
	 * fill that in at the end.
	 */
	vecp = log_vector + 1;

	nitems = 0;
	ASSERT(!list_empty(&tp->t_items));
	list_for_each_entry(lidp, &tp->t_items, lid_trans) {
		/* Skip items which aren't dirty in this transaction. */
		if (!(lidp->lid_flags & XFS_LID_DIRTY))
			continue;

		/*
		 * The item may be marked dirty but not log anything.  This can
		 * be used to get called when a transaction is committed.
		 */
		if (lidp->lid_size)
			nitems++;
		IOP_FORMAT(lidp->lid_item, vecp);
		vecp += lidp->lid_size;
		IOP_PIN(lidp->lid_item);
	}

	/*
	 * Now that we've counted the number of items in this transaction, fill
	 * in the transaction header. Note that the transaction header does not
	 * have a log item.
	 */
	tp->t_header.th_magic = XFS_TRANS_HEADER_MAGIC;
	tp->t_header.th_type = tp->t_type;
	tp->t_header.th_num_items = nitems;
	log_vector->i_addr = (xfs_caddr_t)&tp->t_header;
	log_vector->i_len = sizeof(xfs_trans_header_t);
	log_vector->i_type = XLOG_REG_TYPE_TRANSHDR;
}

/*
 * The committed item processing consists of calling the committed routine of
 * each logged item, updating the item's position in the AIL if necessary, and
 * unpinning each item.  If the committed routine returns -1, then do nothing
 * further with the item because it may have been freed.
 *
 * Since items are unlocked when they are copied to the incore log, it is
 * possible for two transactions to be completing and manipulating the same
 * item simultaneously.  The AIL lock will protect the lsn field of each item.
 * The value of this field can never go backwards.
 *
 * We unpin the items after repositioning them in the AIL, because otherwise
 * they could be immediately flushed and we'd have to race with the flusher
 * trying to pull the item from the AIL as we add it.
 */
static void
xfs_trans_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		commit_lsn,
	int			aborted)
{
	xfs_lsn_t		item_lsn;
	struct xfs_ail		*ailp;

	if (aborted)
		lip->li_flags |= XFS_LI_ABORTED;
	item_lsn = IOP_COMMITTED(lip, commit_lsn);

	/* item_lsn of -1 means the item needs no further processing */
	if (XFS_LSN_CMP(item_lsn, (xfs_lsn_t)-1) == 0)
		return;

	/*
	 * If the returned lsn is greater than what it contained before, update
	 * the location of the item in the AIL.  If it is not, then do nothing.
	 * Items can never move backwards in the AIL.
	 *
	 * While the new lsn should usually be greater, it is possible that a
	 * later transaction completing simultaneously with an earlier one
	 * using the same item could complete first with a higher lsn.  This
	 * would cause the earlier transaction to fail the test below.
	 */
	ailp = lip->li_ailp;
	spin_lock(&ailp->xa_lock);
	if (XFS_LSN_CMP(item_lsn, lip->li_lsn) > 0) {
		/*
		 * This will set the item's lsn to item_lsn and update the
		 * position of the item in the AIL.
		 *
		 * xfs_trans_ail_update() drops the AIL lock.
		 */
		xfs_trans_ail_update(ailp, lip, item_lsn);
	} else {
		spin_unlock(&ailp->xa_lock);
	}

	/*
	 * Now that we've repositioned the item in the AIL, unpin it so it can
	 * be flushed. Pass information about buffer stale state down from the
	 * log item flags, if anyone else stales the buffer we do not want to
	 * pay any attention to it.
	 */
	IOP_UNPIN(lip, 0);
}

/*
 * This is typically called by the LM when a transaction has been fully
 * committed to disk.  It needs to unpin the items which have
 * been logged by the transaction and update their positions
 * in the AIL if necessary.
 *
 * This also gets called when the transactions didn't get written out
 * because of an I/O error. Abortflag & XFS_LI_ABORTED is set then.
 */
STATIC void
xfs_trans_committed(
	void			*arg,
	int			abortflag)
{
	struct xfs_trans	*tp = arg;
	struct xfs_log_item_desc *lidp, *next;

	list_for_each_entry_safe(lidp, next, &tp->t_items, lid_trans) {
		xfs_trans_item_committed(lidp->lid_item, tp->t_lsn, abortflag);
		xfs_trans_free_item_desc(lidp);
	}

	xfs_trans_free(tp);
}

static inline void
xfs_log_item_batch_insert(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct xfs_log_item	**log_items,
	int			nr_items,
	xfs_lsn_t		commit_lsn)
{
	int	i;

	spin_lock(&ailp->xa_lock);
	/* xfs_trans_ail_update_bulk drops ailp->xa_lock */
	xfs_trans_ail_update_bulk(ailp, cur, log_items, nr_items, commit_lsn);

	for (i = 0; i < nr_items; i++)
		IOP_UNPIN(log_items[i], 0);
}

/*
 * Bulk operation version of xfs_trans_committed that takes a log vector of
 * items to insert into the AIL. This uses bulk AIL insertion techniques to
 * minimise lock traffic.
 *
 * If we are called with the aborted flag set, it is because a log write during
 * a CIL checkpoint commit has failed. In this case, all the items in the
 * checkpoint have already gone through IOP_COMMITED and IOP_UNLOCK, which
 * means that checkpoint commit abort handling is treated exactly the same
 * as an iclog write error even though we haven't started any IO yet. Hence in
 * this case all we need to do is IOP_COMMITTED processing, followed by an
 * IOP_UNPIN(aborted) call.
 *
 * The AIL cursor is used to optimise the insert process. If commit_lsn is not
 * at the end of the AIL, the insert cursor avoids the need to walk
 * the AIL to find the insertion point on every xfs_log_item_batch_insert()
 * call. This saves a lot of needless list walking and is a net win, even
 * though it slightly increases that amount of AIL lock traffic to set it up
 * and tear it down.
 */
void
xfs_trans_committed_bulk(
	struct xfs_ail		*ailp,
	struct xfs_log_vec	*log_vector,
	xfs_lsn_t		commit_lsn,
	int			aborted)
{
#define LOG_ITEM_BATCH_SIZE	32
	struct xfs_log_item	*log_items[LOG_ITEM_BATCH_SIZE];
	struct xfs_log_vec	*lv;
	struct xfs_ail_cursor	cur;
	int			i = 0;

	spin_lock(&ailp->xa_lock);
	xfs_trans_ail_cursor_last(ailp, &cur, commit_lsn);
	spin_unlock(&ailp->xa_lock);

	/* unpin all the log items */
	for (lv = log_vector; lv; lv = lv->lv_next ) {
		struct xfs_log_item	*lip = lv->lv_item;
		xfs_lsn_t		item_lsn;

		if (aborted)
			lip->li_flags |= XFS_LI_ABORTED;
		item_lsn = IOP_COMMITTED(lip, commit_lsn);

		/* item_lsn of -1 means the item needs no further processing */
		if (XFS_LSN_CMP(item_lsn, (xfs_lsn_t)-1) == 0)
			continue;

		/*
		 * if we are aborting the operation, no point in inserting the
		 * object into the AIL as we are in a shutdown situation.
		 */
		if (aborted) {
			ASSERT(XFS_FORCED_SHUTDOWN(ailp->xa_mount));
			IOP_UNPIN(lip, 1);
			continue;
		}

		if (item_lsn != commit_lsn) {

			/*
			 * Not a bulk update option due to unusual item_lsn.
			 * Push into AIL immediately, rechecking the lsn once
			 * we have the ail lock. Then unpin the item. This does
			 * not affect the AIL cursor the bulk insert path is
			 * using.
			 */
			spin_lock(&ailp->xa_lock);
			if (XFS_LSN_CMP(item_lsn, lip->li_lsn) > 0)
				xfs_trans_ail_update(ailp, lip, item_lsn);
			else
				spin_unlock(&ailp->xa_lock);
			IOP_UNPIN(lip, 0);
			continue;
		}

		/* Item is a candidate for bulk AIL insert.  */
		log_items[i++] = lv->lv_item;
		if (i >= LOG_ITEM_BATCH_SIZE) {
			xfs_log_item_batch_insert(ailp, &cur, log_items,
					LOG_ITEM_BATCH_SIZE, commit_lsn);
			i = 0;
		}
	}

	/* make sure we insert the remainder! */
	if (i)
		xfs_log_item_batch_insert(ailp, &cur, log_items, i, commit_lsn);

	spin_lock(&ailp->xa_lock);
	xfs_trans_ail_cursor_done(ailp, &cur);
	spin_unlock(&ailp->xa_lock);
}

/*
 * Called from the trans_commit code when we notice that the filesystem is in
 * the middle of a forced shutdown.
 *
 * When we are called here, we have already pinned all the items in the
 * transaction. However, neither IOP_COMMITTING or IOP_UNLOCK has been called
 * so we can simply walk the items in the transaction, unpin them with an abort
 * flag and then free the items. Note that unpinning the items can result in
 * them being freed immediately, so we need to use a safe list traversal method
 * here.
 */
STATIC void
xfs_trans_uncommit(
	struct xfs_trans	*tp,
	uint			flags)
{
	struct xfs_log_item_desc *lidp, *n;

	list_for_each_entry_safe(lidp, n, &tp->t_items, lid_trans) {
		if (lidp->lid_flags & XFS_LID_DIRTY)
			IOP_UNPIN(lidp->lid_item, 1);
	}

	xfs_trans_unreserve_and_mod_sb(tp);
	xfs_trans_unreserve_and_mod_dquots(tp);

	xfs_trans_free_items(tp, NULLCOMMITLSN, flags);
	xfs_trans_free(tp);
}

/*
 * Format the transaction direct to the iclog. This isolates the physical
 * transaction commit operation from the logical operation and hence allows
 * other methods to be introduced without affecting the existing commit path.
 */
static int
xfs_trans_commit_iclog(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_lsn_t		*commit_lsn,
	int			flags)
{
	int			shutdown;
	int			error;
	int			log_flags = 0;
	struct xlog_in_core	*commit_iclog;
#define XFS_TRANS_LOGVEC_COUNT  16
	struct xfs_log_iovec	log_vector_fast[XFS_TRANS_LOGVEC_COUNT];
	struct xfs_log_iovec	*log_vector;
	uint			nvec;


	/*
	 * Ask each log item how many log_vector entries it will
	 * need so we can figure out how many to allocate.
	 * Try to avoid the kmem_alloc() call in the common case
	 * by using a vector from the stack when it fits.
	 */
	nvec = xfs_trans_count_vecs(tp);
	if (nvec == 0) {
		return ENOMEM;	/* triggers a shutdown! */
	} else if (nvec <= XFS_TRANS_LOGVEC_COUNT) {
		log_vector = log_vector_fast;
	} else {
		log_vector = (xfs_log_iovec_t *)kmem_alloc(nvec *
						   sizeof(xfs_log_iovec_t),
						   KM_SLEEP);
	}

	/*
	 * Fill in the log_vector and pin the logged items, and
	 * then write the transaction to the log.
	 */
	xfs_trans_fill_vecs(tp, log_vector);

	if (flags & XFS_TRANS_RELEASE_LOG_RES)
		log_flags = XFS_LOG_REL_PERM_RESERV;

	error = xfs_log_write(mp, log_vector, nvec, tp->t_ticket, &(tp->t_lsn));

	/*
	 * The transaction is committed incore here, and can go out to disk
	 * at any time after this call.  However, all the items associated
	 * with the transaction are still locked and pinned in memory.
	 */
	*commit_lsn = xfs_log_done(mp, tp->t_ticket, &commit_iclog, log_flags);

	tp->t_commit_lsn = *commit_lsn;
	trace_xfs_trans_commit_lsn(tp);

	if (nvec > XFS_TRANS_LOGVEC_COUNT)
		kmem_free(log_vector);

	/*
	 * If we got a log write error. Unpin the logitems that we
	 * had pinned, clean up, free trans structure, and return error.
	 */
	if (error || *commit_lsn == -1) {
		current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
		xfs_trans_uncommit(tp, flags|XFS_TRANS_ABORT);
		return XFS_ERROR(EIO);
	}

	/*
	 * Once the transaction has committed, unused
	 * reservations need to be released and changes to
	 * the superblock need to be reflected in the in-core
	 * version.  Do that now.
	 */
	xfs_trans_unreserve_and_mod_sb(tp);

	/*
	 * Tell the LM to call the transaction completion routine
	 * when the log write with LSN commit_lsn completes (e.g.
	 * when the transaction commit really hits the on-disk log).
	 * After this call we cannot reference tp, because the call
	 * can happen at any time and the call will free the transaction
	 * structure pointed to by tp.  The only case where we call
	 * the completion routine (xfs_trans_committed) directly is
	 * if the log is turned off on a debug kernel or we're
	 * running in simulation mode (the log is explicitly turned
	 * off).
	 */
	tp->t_logcb.cb_func = xfs_trans_committed;
	tp->t_logcb.cb_arg = tp;

	/*
	 * We need to pass the iclog buffer which was used for the
	 * transaction commit record into this function, and attach
	 * the callback to it. The callback must be attached before
	 * the items are unlocked to avoid racing with other threads
	 * waiting for an item to unlock.
	 */
	shutdown = xfs_log_notify(mp, commit_iclog, &(tp->t_logcb));

	/*
	 * Mark this thread as no longer being in a transaction
	 */
	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);

	/*
	 * Once all the items of the transaction have been copied
	 * to the in core log and the callback is attached, the
	 * items can be unlocked.
	 *
	 * This will free descriptors pointing to items which were
	 * not logged since there is nothing more to do with them.
	 * For items which were logged, we will keep pointers to them
	 * so they can be unpinned after the transaction commits to disk.
	 * This will also stamp each modified meta-data item with
	 * the commit lsn of this transaction for dependency tracking
	 * purposes.
	 */
	xfs_trans_unlock_items(tp, *commit_lsn);

	/*
	 * If we detected a log error earlier, finish committing
	 * the transaction now (unpin log items, etc).
	 *
	 * Order is critical here, to avoid using the transaction
	 * pointer after its been freed (by xfs_trans_committed
	 * either here now, or as a callback).  We cannot do this
	 * step inside xfs_log_notify as was done earlier because
	 * of this issue.
	 */
	if (shutdown)
		xfs_trans_committed(tp, XFS_LI_ABORTED);

	/*
	 * Now that the xfs_trans_committed callback has been attached,
	 * and the items are released we can finally allow the iclog to
	 * go to disk.
	 */
	return xfs_log_release_iclog(mp, commit_iclog);
}

/*
 * Walk the log items and allocate log vector structures for
 * each item large enough to fit all the vectors they require.
 * Note that this format differs from the old log vector format in
 * that there is no transaction header in these log vectors.
 */
STATIC struct xfs_log_vec *
xfs_trans_alloc_log_vecs(
	xfs_trans_t	*tp)
{
	struct xfs_log_item_desc *lidp;
	struct xfs_log_vec	*lv = NULL;
	struct xfs_log_vec	*ret_lv = NULL;


	/* Bail out if we didn't find a log item.  */
	if (list_empty(&tp->t_items)) {
		ASSERT(0);
		return NULL;
	}

	list_for_each_entry(lidp, &tp->t_items, lid_trans) {
		struct xfs_log_vec *new_lv;

		/* Skip items which aren't dirty in this transaction. */
		if (!(lidp->lid_flags & XFS_LID_DIRTY))
			continue;

		/* Skip items that do not have any vectors for writing */
		lidp->lid_size = IOP_SIZE(lidp->lid_item);
		if (!lidp->lid_size)
			continue;

		new_lv = kmem_zalloc(sizeof(*new_lv) +
				lidp->lid_size * sizeof(struct xfs_log_iovec),
				KM_SLEEP);

		/* The allocated iovec region lies beyond the log vector. */
		new_lv->lv_iovecp = (struct xfs_log_iovec *)&new_lv[1];
		new_lv->lv_niovecs = lidp->lid_size;
		new_lv->lv_item = lidp->lid_item;
		if (!ret_lv)
			ret_lv = new_lv;
		else
			lv->lv_next = new_lv;
		lv = new_lv;
	}

	return ret_lv;
}

static int
xfs_trans_commit_cil(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_lsn_t		*commit_lsn,
	int			flags)
{
	struct xfs_log_vec	*log_vector;

	/*
	 * Get each log item to allocate a vector structure for
	 * the log item to to pass to the log write code. The
	 * CIL commit code will format the vector and save it away.
	 */
	log_vector = xfs_trans_alloc_log_vecs(tp);
	if (!log_vector)
		return ENOMEM;

	xfs_log_commit_cil(mp, tp, log_vector, commit_lsn, flags);

	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
	xfs_trans_free(tp);
	return 0;
}

/*
 * xfs_trans_commit
 *
 * Commit the given transaction to the log a/synchronously.
 *
 * XFS disk error handling mechanism is not based on a typical
 * transaction abort mechanism. Logically after the filesystem
 * gets marked 'SHUTDOWN', we can't let any new transactions
 * be durable - ie. committed to disk - because some metadata might
 * be inconsistent. In such cases, this returns an error, and the
 * caller may assume that all locked objects joined to the transaction
 * have already been unlocked as if the commit had succeeded.
 * Do not reference the transaction structure after this call.
 */
int
_xfs_trans_commit(
	struct xfs_trans	*tp,
	uint			flags,
	int			*log_flushed)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_lsn_t		commit_lsn = -1;
	int			error = 0;
	int			log_flags = 0;
	int			sync = tp->t_flags & XFS_TRANS_SYNC;

	/*
	 * Determine whether this commit is releasing a permanent
	 * log reservation or not.
	 */
	if (flags & XFS_TRANS_RELEASE_LOG_RES) {
		ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
		log_flags = XFS_LOG_REL_PERM_RESERV;
	}

	/*
	 * If there is nothing to be logged by the transaction,
	 * then unlock all of the items associated with the
	 * transaction and free the transaction structure.
	 * Also make sure to return any reserved blocks to
	 * the free pool.
	 */
	if (!(tp->t_flags & XFS_TRANS_DIRTY))
		goto out_unreserve;

	if (XFS_FORCED_SHUTDOWN(mp)) {
		error = XFS_ERROR(EIO);
		goto out_unreserve;
	}

	ASSERT(tp->t_ticket != NULL);

	/*
	 * If we need to update the superblock, then do it now.
	 */
	if (tp->t_flags & XFS_TRANS_SB_DIRTY)
		xfs_trans_apply_sb_deltas(tp);
	xfs_trans_apply_dquot_deltas(tp);

	if (mp->m_flags & XFS_MOUNT_DELAYLOG)
		error = xfs_trans_commit_cil(mp, tp, &commit_lsn, flags);
	else
		error = xfs_trans_commit_iclog(mp, tp, &commit_lsn, flags);

	if (error == ENOMEM) {
		xfs_force_shutdown(mp, SHUTDOWN_LOG_IO_ERROR);
		error = XFS_ERROR(EIO);
		goto out_unreserve;
	}

	/*
	 * If the transaction needs to be synchronous, then force the
	 * log out now and wait for it.
	 */
	if (sync) {
		if (!error) {
			error = _xfs_log_force_lsn(mp, commit_lsn,
				      XFS_LOG_SYNC, log_flushed);
		}
		XFS_STATS_INC(xs_trans_sync);
	} else {
		XFS_STATS_INC(xs_trans_async);
	}

	return error;

out_unreserve:
	xfs_trans_unreserve_and_mod_sb(tp);

	/*
	 * It is indeed possible for the transaction to be not dirty but
	 * the dqinfo portion to be.  All that means is that we have some
	 * (non-persistent) quota reservations that need to be unreserved.
	 */
	xfs_trans_unreserve_and_mod_dquots(tp);
	if (tp->t_ticket) {
		commit_lsn = xfs_log_done(mp, tp->t_ticket, NULL, log_flags);
		if (commit_lsn == -1 && !error)
			error = XFS_ERROR(EIO);
	}
	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
	xfs_trans_free_items(tp, NULLCOMMITLSN, error ? XFS_TRANS_ABORT : 0);
	xfs_trans_free(tp);

	XFS_STATS_INC(xs_trans_empty);
	return error;
}

/*
 * Unlock all of the transaction's items and free the transaction.
 * The transaction must not have modified any of its items, because
 * there is no way to restore them to their previous state.
 *
 * If the transaction has made a log reservation, make sure to release
 * it as well.
 */
void
xfs_trans_cancel(
	xfs_trans_t		*tp,
	int			flags)
{
	int			log_flags;
	xfs_mount_t		*mp = tp->t_mountp;

	/*
	 * See if the caller is being too lazy to figure out if
	 * the transaction really needs an abort.
	 */
	if ((flags & XFS_TRANS_ABORT) && !(tp->t_flags & XFS_TRANS_DIRTY))
		flags &= ~XFS_TRANS_ABORT;
	/*
	 * See if the caller is relying on us to shut down the
	 * filesystem.  This happens in paths where we detect
	 * corruption and decide to give up.
	 */
	if ((tp->t_flags & XFS_TRANS_DIRTY) && !XFS_FORCED_SHUTDOWN(mp)) {
		XFS_ERROR_REPORT("xfs_trans_cancel", XFS_ERRLEVEL_LOW, mp);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
	}
#ifdef DEBUG
	if (!(flags & XFS_TRANS_ABORT) && !XFS_FORCED_SHUTDOWN(mp)) {
		struct xfs_log_item_desc *lidp;

		list_for_each_entry(lidp, &tp->t_items, lid_trans)
			ASSERT(!(lidp->lid_item->li_type == XFS_LI_EFD));
	}
#endif
	xfs_trans_unreserve_and_mod_sb(tp);
	xfs_trans_unreserve_and_mod_dquots(tp);

	if (tp->t_ticket) {
		if (flags & XFS_TRANS_RELEASE_LOG_RES) {
			ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
			log_flags = XFS_LOG_REL_PERM_RESERV;
		} else {
			log_flags = 0;
		}
		xfs_log_done(mp, tp->t_ticket, NULL, log_flags);
	}

	/* mark this thread as no longer being in a transaction */
	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);

	xfs_trans_free_items(tp, NULLCOMMITLSN, flags);
	xfs_trans_free(tp);
}

/*
 * Roll from one trans in the sequence of PERMANENT transactions to
 * the next: permanent transactions are only flushed out when
 * committed with XFS_TRANS_RELEASE_LOG_RES, but we still want as soon
 * as possible to let chunks of it go to the log. So we commit the
 * chunk we've been working on and get a new transaction to continue.
 */
int
xfs_trans_roll(
	struct xfs_trans	**tpp,
	struct xfs_inode	*dp)
{
	struct xfs_trans	*trans;
	unsigned int		logres, count;
	int			error;

	/*
	 * Ensure that the inode is always logged.
	 */
	trans = *tpp;
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);

	/*
	 * Copy the critical parameters from one trans to the next.
	 */
	logres = trans->t_log_res;
	count = trans->t_log_count;
	*tpp = xfs_trans_dup(trans);

	/*
	 * Commit the current transaction.
	 * If this commit failed, then it'd just unlock those items that
	 * are not marked ihold. That also means that a filesystem shutdown
	 * is in progress. The caller takes the responsibility to cancel
	 * the duplicate transaction that gets returned.
	 */
	error = xfs_trans_commit(trans, 0);
	if (error)
		return (error);

	trans = *tpp;

	/*
	 * transaction commit worked ok so we can drop the extra ticket
	 * reference that we gained in xfs_trans_dup()
	 */
	xfs_log_ticket_put(trans->t_ticket);


	/*
	 * Reserve space in the log for th next transaction.
	 * This also pushes items in the "AIL", the list of logged items,
	 * out to disk if they are taking up space at the tail of the log
	 * that we want to use.  This requires that either nothing be locked
	 * across this call, or that anything that is locked be logged in
	 * the prior and the next transactions.
	 */
	error = xfs_trans_reserve(trans, 0, logres, 0,
				  XFS_TRANS_PERM_LOG_RES, count);
	/*
	 *  Ensure that the inode is in the new transaction and locked.
	 */
	if (error)
		return error;

	xfs_trans_ijoin(trans, dp);
	return 0;
}
