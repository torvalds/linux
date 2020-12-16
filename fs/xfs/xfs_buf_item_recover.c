// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_error.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_quota.h"

/*
 * This structure is used during recovery to record the buf log items which
 * have been canceled and should not be replayed.
 */
struct xfs_buf_cancel {
	xfs_daddr_t		bc_blkno;
	uint			bc_len;
	int			bc_refcount;
	struct list_head	bc_list;
};

static struct xfs_buf_cancel *
xlog_find_buffer_cancelled(
	struct xlog		*log,
	xfs_daddr_t		blkno,
	uint			len)
{
	struct list_head	*bucket;
	struct xfs_buf_cancel	*bcp;

	if (!log->l_buf_cancel_table)
		return NULL;

	bucket = XLOG_BUF_CANCEL_BUCKET(log, blkno);
	list_for_each_entry(bcp, bucket, bc_list) {
		if (bcp->bc_blkno == blkno && bcp->bc_len == len)
			return bcp;
	}

	return NULL;
}

static bool
xlog_add_buffer_cancelled(
	struct xlog		*log,
	xfs_daddr_t		blkno,
	uint			len)
{
	struct xfs_buf_cancel	*bcp;

	/*
	 * If we find an existing cancel record, this indicates that the buffer
	 * was cancelled multiple times.  To ensure that during pass 2 we keep
	 * the record in the table until we reach its last occurrence in the
	 * log, a reference count is kept to tell how many times we expect to
	 * see this record during the second pass.
	 */
	bcp = xlog_find_buffer_cancelled(log, blkno, len);
	if (bcp) {
		bcp->bc_refcount++;
		return false;
	}

	bcp = kmem_alloc(sizeof(struct xfs_buf_cancel), 0);
	bcp->bc_blkno = blkno;
	bcp->bc_len = len;
	bcp->bc_refcount = 1;
	list_add_tail(&bcp->bc_list, XLOG_BUF_CANCEL_BUCKET(log, blkno));
	return true;
}

/*
 * Check if there is and entry for blkno, len in the buffer cancel record table.
 */
bool
xlog_is_buffer_cancelled(
	struct xlog		*log,
	xfs_daddr_t		blkno,
	uint			len)
{
	return xlog_find_buffer_cancelled(log, blkno, len) != NULL;
}

/*
 * Check if there is and entry for blkno, len in the buffer cancel record table,
 * and decremented the reference count on it if there is one.
 *
 * Remove the cancel record once the refcount hits zero, so that if the same
 * buffer is re-used again after its last cancellation we actually replay the
 * changes made at that point.
 */
static bool
xlog_put_buffer_cancelled(
	struct xlog		*log,
	xfs_daddr_t		blkno,
	uint			len)
{
	struct xfs_buf_cancel	*bcp;

	bcp = xlog_find_buffer_cancelled(log, blkno, len);
	if (!bcp) {
		ASSERT(0);
		return false;
	}

	if (--bcp->bc_refcount == 0) {
		list_del(&bcp->bc_list);
		kmem_free(bcp);
	}
	return true;
}

/* log buffer item recovery */

/*
 * Sort buffer items for log recovery.  Most buffer items should end up on the
 * buffer list and are recovered first, with the following exceptions:
 *
 * 1. XFS_BLF_CANCEL buffers must be processed last because some log items
 *    might depend on the incor ecancellation record, and replaying a cancelled
 *    buffer item can remove the incore record.
 *
 * 2. XFS_BLF_INODE_BUF buffers are handled after most regular items so that
 *    we replay di_next_unlinked only after flushing the inode 'free' state
 *    to the inode buffer.
 *
 * See xlog_recover_reorder_trans for more details.
 */
STATIC enum xlog_recover_reorder
xlog_recover_buf_reorder(
	struct xlog_recover_item	*item)
{
	struct xfs_buf_log_format	*buf_f = item->ri_buf[0].i_addr;

	if (buf_f->blf_flags & XFS_BLF_CANCEL)
		return XLOG_REORDER_CANCEL_LIST;
	if (buf_f->blf_flags & XFS_BLF_INODE_BUF)
		return XLOG_REORDER_INODE_BUFFER_LIST;
	return XLOG_REORDER_BUFFER_LIST;
}

STATIC void
xlog_recover_buf_ra_pass2(
	struct xlog                     *log,
	struct xlog_recover_item        *item)
{
	struct xfs_buf_log_format	*buf_f = item->ri_buf[0].i_addr;

	xlog_buf_readahead(log, buf_f->blf_blkno, buf_f->blf_len, NULL);
}

/*
 * Build up the table of buf cancel records so that we don't replay cancelled
 * data in the second pass.
 */
static int
xlog_recover_buf_commit_pass1(
	struct xlog			*log,
	struct xlog_recover_item	*item)
{
	struct xfs_buf_log_format	*bf = item->ri_buf[0].i_addr;

	if (!xfs_buf_log_check_iovec(&item->ri_buf[0])) {
		xfs_err(log->l_mp, "bad buffer log item size (%d)",
				item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	if (!(bf->blf_flags & XFS_BLF_CANCEL))
		trace_xfs_log_recover_buf_not_cancel(log, bf);
	else if (xlog_add_buffer_cancelled(log, bf->blf_blkno, bf->blf_len))
		trace_xfs_log_recover_buf_cancel_add(log, bf);
	else
		trace_xfs_log_recover_buf_cancel_ref_inc(log, bf);
	return 0;
}

/*
 * Validate the recovered buffer is of the correct type and attach the
 * appropriate buffer operations to them for writeback. Magic numbers are in a
 * few places:
 *	the first 16 bits of the buffer (inode buffer, dquot buffer),
 *	the first 32 bits of the buffer (most blocks),
 *	inside a struct xfs_da_blkinfo at the start of the buffer.
 */
static void
xlog_recover_validate_buf_type(
	struct xfs_mount		*mp,
	struct xfs_buf			*bp,
	struct xfs_buf_log_format	*buf_f,
	xfs_lsn_t			current_lsn)
{
	struct xfs_da_blkinfo		*info = bp->b_addr;
	uint32_t			magic32;
	uint16_t			magic16;
	uint16_t			magicda;
	char				*warnmsg = NULL;

	/*
	 * We can only do post recovery validation on items on CRC enabled
	 * fielsystems as we need to know when the buffer was written to be able
	 * to determine if we should have replayed the item. If we replay old
	 * metadata over a newer buffer, then it will enter a temporarily
	 * inconsistent state resulting in verification failures. Hence for now
	 * just avoid the verification stage for non-crc filesystems
	 */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	magic32 = be32_to_cpu(*(__be32 *)bp->b_addr);
	magic16 = be16_to_cpu(*(__be16*)bp->b_addr);
	magicda = be16_to_cpu(info->magic);
	switch (xfs_blft_from_flags(buf_f)) {
	case XFS_BLFT_BTREE_BUF:
		switch (magic32) {
		case XFS_ABTB_CRC_MAGIC:
		case XFS_ABTB_MAGIC:
			bp->b_ops = &xfs_bnobt_buf_ops;
			break;
		case XFS_ABTC_CRC_MAGIC:
		case XFS_ABTC_MAGIC:
			bp->b_ops = &xfs_cntbt_buf_ops;
			break;
		case XFS_IBT_CRC_MAGIC:
		case XFS_IBT_MAGIC:
			bp->b_ops = &xfs_inobt_buf_ops;
			break;
		case XFS_FIBT_CRC_MAGIC:
		case XFS_FIBT_MAGIC:
			bp->b_ops = &xfs_finobt_buf_ops;
			break;
		case XFS_BMAP_CRC_MAGIC:
		case XFS_BMAP_MAGIC:
			bp->b_ops = &xfs_bmbt_buf_ops;
			break;
		case XFS_RMAP_CRC_MAGIC:
			bp->b_ops = &xfs_rmapbt_buf_ops;
			break;
		case XFS_REFC_CRC_MAGIC:
			bp->b_ops = &xfs_refcountbt_buf_ops;
			break;
		default:
			warnmsg = "Bad btree block magic!";
			break;
		}
		break;
	case XFS_BLFT_AGF_BUF:
		if (magic32 != XFS_AGF_MAGIC) {
			warnmsg = "Bad AGF block magic!";
			break;
		}
		bp->b_ops = &xfs_agf_buf_ops;
		break;
	case XFS_BLFT_AGFL_BUF:
		if (magic32 != XFS_AGFL_MAGIC) {
			warnmsg = "Bad AGFL block magic!";
			break;
		}
		bp->b_ops = &xfs_agfl_buf_ops;
		break;
	case XFS_BLFT_AGI_BUF:
		if (magic32 != XFS_AGI_MAGIC) {
			warnmsg = "Bad AGI block magic!";
			break;
		}
		bp->b_ops = &xfs_agi_buf_ops;
		break;
	case XFS_BLFT_UDQUOT_BUF:
	case XFS_BLFT_PDQUOT_BUF:
	case XFS_BLFT_GDQUOT_BUF:
#ifdef CONFIG_XFS_QUOTA
		if (magic16 != XFS_DQUOT_MAGIC) {
			warnmsg = "Bad DQUOT block magic!";
			break;
		}
		bp->b_ops = &xfs_dquot_buf_ops;
#else
		xfs_alert(mp,
	"Trying to recover dquots without QUOTA support built in!");
		ASSERT(0);
#endif
		break;
	case XFS_BLFT_DINO_BUF:
		if (magic16 != XFS_DINODE_MAGIC) {
			warnmsg = "Bad INODE block magic!";
			break;
		}
		bp->b_ops = &xfs_inode_buf_ops;
		break;
	case XFS_BLFT_SYMLINK_BUF:
		if (magic32 != XFS_SYMLINK_MAGIC) {
			warnmsg = "Bad symlink block magic!";
			break;
		}
		bp->b_ops = &xfs_symlink_buf_ops;
		break;
	case XFS_BLFT_DIR_BLOCK_BUF:
		if (magic32 != XFS_DIR2_BLOCK_MAGIC &&
		    magic32 != XFS_DIR3_BLOCK_MAGIC) {
			warnmsg = "Bad dir block magic!";
			break;
		}
		bp->b_ops = &xfs_dir3_block_buf_ops;
		break;
	case XFS_BLFT_DIR_DATA_BUF:
		if (magic32 != XFS_DIR2_DATA_MAGIC &&
		    magic32 != XFS_DIR3_DATA_MAGIC) {
			warnmsg = "Bad dir data magic!";
			break;
		}
		bp->b_ops = &xfs_dir3_data_buf_ops;
		break;
	case XFS_BLFT_DIR_FREE_BUF:
		if (magic32 != XFS_DIR2_FREE_MAGIC &&
		    magic32 != XFS_DIR3_FREE_MAGIC) {
			warnmsg = "Bad dir3 free magic!";
			break;
		}
		bp->b_ops = &xfs_dir3_free_buf_ops;
		break;
	case XFS_BLFT_DIR_LEAF1_BUF:
		if (magicda != XFS_DIR2_LEAF1_MAGIC &&
		    magicda != XFS_DIR3_LEAF1_MAGIC) {
			warnmsg = "Bad dir leaf1 magic!";
			break;
		}
		bp->b_ops = &xfs_dir3_leaf1_buf_ops;
		break;
	case XFS_BLFT_DIR_LEAFN_BUF:
		if (magicda != XFS_DIR2_LEAFN_MAGIC &&
		    magicda != XFS_DIR3_LEAFN_MAGIC) {
			warnmsg = "Bad dir leafn magic!";
			break;
		}
		bp->b_ops = &xfs_dir3_leafn_buf_ops;
		break;
	case XFS_BLFT_DA_NODE_BUF:
		if (magicda != XFS_DA_NODE_MAGIC &&
		    magicda != XFS_DA3_NODE_MAGIC) {
			warnmsg = "Bad da node magic!";
			break;
		}
		bp->b_ops = &xfs_da3_node_buf_ops;
		break;
	case XFS_BLFT_ATTR_LEAF_BUF:
		if (magicda != XFS_ATTR_LEAF_MAGIC &&
		    magicda != XFS_ATTR3_LEAF_MAGIC) {
			warnmsg = "Bad attr leaf magic!";
			break;
		}
		bp->b_ops = &xfs_attr3_leaf_buf_ops;
		break;
	case XFS_BLFT_ATTR_RMT_BUF:
		if (magic32 != XFS_ATTR3_RMT_MAGIC) {
			warnmsg = "Bad attr remote magic!";
			break;
		}
		bp->b_ops = &xfs_attr3_rmt_buf_ops;
		break;
	case XFS_BLFT_SB_BUF:
		if (magic32 != XFS_SB_MAGIC) {
			warnmsg = "Bad SB block magic!";
			break;
		}
		bp->b_ops = &xfs_sb_buf_ops;
		break;
#ifdef CONFIG_XFS_RT
	case XFS_BLFT_RTBITMAP_BUF:
	case XFS_BLFT_RTSUMMARY_BUF:
		/* no magic numbers for verification of RT buffers */
		bp->b_ops = &xfs_rtbuf_ops;
		break;
#endif /* CONFIG_XFS_RT */
	default:
		xfs_warn(mp, "Unknown buffer type %d!",
			 xfs_blft_from_flags(buf_f));
		break;
	}

	/*
	 * Nothing else to do in the case of a NULL current LSN as this means
	 * the buffer is more recent than the change in the log and will be
	 * skipped.
	 */
	if (current_lsn == NULLCOMMITLSN)
		return;

	if (warnmsg) {
		xfs_warn(mp, warnmsg);
		ASSERT(0);
	}

	/*
	 * We must update the metadata LSN of the buffer as it is written out to
	 * ensure that older transactions never replay over this one and corrupt
	 * the buffer. This can occur if log recovery is interrupted at some
	 * point after the current transaction completes, at which point a
	 * subsequent mount starts recovery from the beginning.
	 *
	 * Write verifiers update the metadata LSN from log items attached to
	 * the buffer. Therefore, initialize a bli purely to carry the LSN to
	 * the verifier.
	 */
	if (bp->b_ops) {
		struct xfs_buf_log_item	*bip;

		bp->b_flags |= _XBF_LOGRECOVERY;
		xfs_buf_item_init(bp, mp);
		bip = bp->b_log_item;
		bip->bli_item.li_lsn = current_lsn;
	}
}

/*
 * Perform a 'normal' buffer recovery.  Each logged region of the
 * buffer should be copied over the corresponding region in the
 * given buffer.  The bitmap in the buf log format structure indicates
 * where to place the logged data.
 */
STATIC void
xlog_recover_do_reg_buffer(
	struct xfs_mount		*mp,
	struct xlog_recover_item	*item,
	struct xfs_buf			*bp,
	struct xfs_buf_log_format	*buf_f,
	xfs_lsn_t			current_lsn)
{
	int			i;
	int			bit;
	int			nbits;
	xfs_failaddr_t		fa;
	const size_t		size_disk_dquot = sizeof(struct xfs_disk_dquot);

	trace_xfs_log_recover_buf_reg_buf(mp->m_log, buf_f);

	bit = 0;
	i = 1;  /* 0 is the buf format structure */
	while (1) {
		bit = xfs_next_bit(buf_f->blf_data_map,
				   buf_f->blf_map_size, bit);
		if (bit == -1)
			break;
		nbits = xfs_contig_bits(buf_f->blf_data_map,
					buf_f->blf_map_size, bit);
		ASSERT(nbits > 0);
		ASSERT(item->ri_buf[i].i_addr != NULL);
		ASSERT(item->ri_buf[i].i_len % XFS_BLF_CHUNK == 0);
		ASSERT(BBTOB(bp->b_length) >=
		       ((uint)bit << XFS_BLF_SHIFT) + (nbits << XFS_BLF_SHIFT));

		/*
		 * The dirty regions logged in the buffer, even though
		 * contiguous, may span multiple chunks. This is because the
		 * dirty region may span a physical page boundary in a buffer
		 * and hence be split into two separate vectors for writing into
		 * the log. Hence we need to trim nbits back to the length of
		 * the current region being copied out of the log.
		 */
		if (item->ri_buf[i].i_len < (nbits << XFS_BLF_SHIFT))
			nbits = item->ri_buf[i].i_len >> XFS_BLF_SHIFT;

		/*
		 * Do a sanity check if this is a dquot buffer. Just checking
		 * the first dquot in the buffer should do. XXXThis is
		 * probably a good thing to do for other buf types also.
		 */
		fa = NULL;
		if (buf_f->blf_flags &
		   (XFS_BLF_UDQUOT_BUF|XFS_BLF_PDQUOT_BUF|XFS_BLF_GDQUOT_BUF)) {
			if (item->ri_buf[i].i_addr == NULL) {
				xfs_alert(mp,
					"XFS: NULL dquot in %s.", __func__);
				goto next;
			}
			if (item->ri_buf[i].i_len < size_disk_dquot) {
				xfs_alert(mp,
					"XFS: dquot too small (%d) in %s.",
					item->ri_buf[i].i_len, __func__);
				goto next;
			}
			fa = xfs_dquot_verify(mp, item->ri_buf[i].i_addr, -1);
			if (fa) {
				xfs_alert(mp,
	"dquot corrupt at %pS trying to replay into block 0x%llx",
					fa, bp->b_bn);
				goto next;
			}
		}

		memcpy(xfs_buf_offset(bp,
			(uint)bit << XFS_BLF_SHIFT),	/* dest */
			item->ri_buf[i].i_addr,		/* source */
			nbits<<XFS_BLF_SHIFT);		/* length */
 next:
		i++;
		bit += nbits;
	}

	/* Shouldn't be any more regions */
	ASSERT(i == item->ri_total);

	xlog_recover_validate_buf_type(mp, bp, buf_f, current_lsn);
}

/*
 * Perform a dquot buffer recovery.
 * Simple algorithm: if we have found a QUOTAOFF log item of the same type
 * (ie. USR or GRP), then just toss this buffer away; don't recover it.
 * Else, treat it as a regular buffer and do recovery.
 *
 * Return false if the buffer was tossed and true if we recovered the buffer to
 * indicate to the caller if the buffer needs writing.
 */
STATIC bool
xlog_recover_do_dquot_buffer(
	struct xfs_mount		*mp,
	struct xlog			*log,
	struct xlog_recover_item	*item,
	struct xfs_buf			*bp,
	struct xfs_buf_log_format	*buf_f)
{
	uint			type;

	trace_xfs_log_recover_buf_dquot_buf(log, buf_f);

	/*
	 * Filesystems are required to send in quota flags at mount time.
	 */
	if (!mp->m_qflags)
		return false;

	type = 0;
	if (buf_f->blf_flags & XFS_BLF_UDQUOT_BUF)
		type |= XFS_DQTYPE_USER;
	if (buf_f->blf_flags & XFS_BLF_PDQUOT_BUF)
		type |= XFS_DQTYPE_PROJ;
	if (buf_f->blf_flags & XFS_BLF_GDQUOT_BUF)
		type |= XFS_DQTYPE_GROUP;
	/*
	 * This type of quotas was turned off, so ignore this buffer
	 */
	if (log->l_quotaoffs_flag & type)
		return false;

	xlog_recover_do_reg_buffer(mp, item, bp, buf_f, NULLCOMMITLSN);
	return true;
}

/*
 * Perform recovery for a buffer full of inodes.  In these buffers, the only
 * data which should be recovered is that which corresponds to the
 * di_next_unlinked pointers in the on disk inode structures.  The rest of the
 * data for the inodes is always logged through the inodes themselves rather
 * than the inode buffer and is recovered in xlog_recover_inode_pass2().
 *
 * The only time when buffers full of inodes are fully recovered is when the
 * buffer is full of newly allocated inodes.  In this case the buffer will
 * not be marked as an inode buffer and so will be sent to
 * xlog_recover_do_reg_buffer() below during recovery.
 */
STATIC int
xlog_recover_do_inode_buffer(
	struct xfs_mount		*mp,
	struct xlog_recover_item	*item,
	struct xfs_buf			*bp,
	struct xfs_buf_log_format	*buf_f)
{
	int				i;
	int				item_index = 0;
	int				bit = 0;
	int				nbits = 0;
	int				reg_buf_offset = 0;
	int				reg_buf_bytes = 0;
	int				next_unlinked_offset;
	int				inodes_per_buf;
	xfs_agino_t			*logged_nextp;
	xfs_agino_t			*buffer_nextp;

	trace_xfs_log_recover_buf_inode_buf(mp->m_log, buf_f);

	/*
	 * Post recovery validation only works properly on CRC enabled
	 * filesystems.
	 */
	if (xfs_sb_version_hascrc(&mp->m_sb))
		bp->b_ops = &xfs_inode_buf_ops;

	inodes_per_buf = BBTOB(bp->b_length) >> mp->m_sb.sb_inodelog;
	for (i = 0; i < inodes_per_buf; i++) {
		next_unlinked_offset = (i * mp->m_sb.sb_inodesize) +
			offsetof(xfs_dinode_t, di_next_unlinked);

		while (next_unlinked_offset >=
		       (reg_buf_offset + reg_buf_bytes)) {
			/*
			 * The next di_next_unlinked field is beyond
			 * the current logged region.  Find the next
			 * logged region that contains or is beyond
			 * the current di_next_unlinked field.
			 */
			bit += nbits;
			bit = xfs_next_bit(buf_f->blf_data_map,
					   buf_f->blf_map_size, bit);

			/*
			 * If there are no more logged regions in the
			 * buffer, then we're done.
			 */
			if (bit == -1)
				return 0;

			nbits = xfs_contig_bits(buf_f->blf_data_map,
						buf_f->blf_map_size, bit);
			ASSERT(nbits > 0);
			reg_buf_offset = bit << XFS_BLF_SHIFT;
			reg_buf_bytes = nbits << XFS_BLF_SHIFT;
			item_index++;
		}

		/*
		 * If the current logged region starts after the current
		 * di_next_unlinked field, then move on to the next
		 * di_next_unlinked field.
		 */
		if (next_unlinked_offset < reg_buf_offset)
			continue;

		ASSERT(item->ri_buf[item_index].i_addr != NULL);
		ASSERT((item->ri_buf[item_index].i_len % XFS_BLF_CHUNK) == 0);
		ASSERT((reg_buf_offset + reg_buf_bytes) <= BBTOB(bp->b_length));

		/*
		 * The current logged region contains a copy of the
		 * current di_next_unlinked field.  Extract its value
		 * and copy it to the buffer copy.
		 */
		logged_nextp = item->ri_buf[item_index].i_addr +
				next_unlinked_offset - reg_buf_offset;
		if (XFS_IS_CORRUPT(mp, *logged_nextp == 0)) {
			xfs_alert(mp,
		"Bad inode buffer log record (ptr = "PTR_FMT", bp = "PTR_FMT"). "
		"Trying to replay bad (0) inode di_next_unlinked field.",
				item, bp);
			return -EFSCORRUPTED;
		}

		buffer_nextp = xfs_buf_offset(bp, next_unlinked_offset);
		*buffer_nextp = *logged_nextp;

		/*
		 * If necessary, recalculate the CRC in the on-disk inode. We
		 * have to leave the inode in a consistent state for whoever
		 * reads it next....
		 */
		xfs_dinode_calc_crc(mp,
				xfs_buf_offset(bp, i * mp->m_sb.sb_inodesize));

	}

	return 0;
}

/*
 * V5 filesystems know the age of the buffer on disk being recovered. We can
 * have newer objects on disk than we are replaying, and so for these cases we
 * don't want to replay the current change as that will make the buffer contents
 * temporarily invalid on disk.
 *
 * The magic number might not match the buffer type we are going to recover
 * (e.g. reallocated blocks), so we ignore the xfs_buf_log_format flags.  Hence
 * extract the LSN of the existing object in the buffer based on it's current
 * magic number.  If we don't recognise the magic number in the buffer, then
 * return a LSN of -1 so that the caller knows it was an unrecognised block and
 * so can recover the buffer.
 *
 * Note: we cannot rely solely on magic number matches to determine that the
 * buffer has a valid LSN - we also need to verify that it belongs to this
 * filesystem, so we need to extract the object's LSN and compare it to that
 * which we read from the superblock. If the UUIDs don't match, then we've got a
 * stale metadata block from an old filesystem instance that we need to recover
 * over the top of.
 */
static xfs_lsn_t
xlog_recover_get_buf_lsn(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	uint32_t		magic32;
	uint16_t		magic16;
	uint16_t		magicda;
	void			*blk = bp->b_addr;
	uuid_t			*uuid;
	xfs_lsn_t		lsn = -1;

	/* v4 filesystems always recover immediately */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		goto recover_immediately;

	magic32 = be32_to_cpu(*(__be32 *)blk);
	switch (magic32) {
	case XFS_ABTB_CRC_MAGIC:
	case XFS_ABTC_CRC_MAGIC:
	case XFS_ABTB_MAGIC:
	case XFS_ABTC_MAGIC:
	case XFS_RMAP_CRC_MAGIC:
	case XFS_REFC_CRC_MAGIC:
	case XFS_FIBT_CRC_MAGIC:
	case XFS_FIBT_MAGIC:
	case XFS_IBT_CRC_MAGIC:
	case XFS_IBT_MAGIC: {
		struct xfs_btree_block *btb = blk;

		lsn = be64_to_cpu(btb->bb_u.s.bb_lsn);
		uuid = &btb->bb_u.s.bb_uuid;
		break;
	}
	case XFS_BMAP_CRC_MAGIC:
	case XFS_BMAP_MAGIC: {
		struct xfs_btree_block *btb = blk;

		lsn = be64_to_cpu(btb->bb_u.l.bb_lsn);
		uuid = &btb->bb_u.l.bb_uuid;
		break;
	}
	case XFS_AGF_MAGIC:
		lsn = be64_to_cpu(((struct xfs_agf *)blk)->agf_lsn);
		uuid = &((struct xfs_agf *)blk)->agf_uuid;
		break;
	case XFS_AGFL_MAGIC:
		lsn = be64_to_cpu(((struct xfs_agfl *)blk)->agfl_lsn);
		uuid = &((struct xfs_agfl *)blk)->agfl_uuid;
		break;
	case XFS_AGI_MAGIC:
		lsn = be64_to_cpu(((struct xfs_agi *)blk)->agi_lsn);
		uuid = &((struct xfs_agi *)blk)->agi_uuid;
		break;
	case XFS_SYMLINK_MAGIC:
		lsn = be64_to_cpu(((struct xfs_dsymlink_hdr *)blk)->sl_lsn);
		uuid = &((struct xfs_dsymlink_hdr *)blk)->sl_uuid;
		break;
	case XFS_DIR3_BLOCK_MAGIC:
	case XFS_DIR3_DATA_MAGIC:
	case XFS_DIR3_FREE_MAGIC:
		lsn = be64_to_cpu(((struct xfs_dir3_blk_hdr *)blk)->lsn);
		uuid = &((struct xfs_dir3_blk_hdr *)blk)->uuid;
		break;
	case XFS_ATTR3_RMT_MAGIC:
		/*
		 * Remote attr blocks are written synchronously, rather than
		 * being logged. That means they do not contain a valid LSN
		 * (i.e. transactionally ordered) in them, and hence any time we
		 * see a buffer to replay over the top of a remote attribute
		 * block we should simply do so.
		 */
		goto recover_immediately;
	case XFS_SB_MAGIC:
		/*
		 * superblock uuids are magic. We may or may not have a
		 * sb_meta_uuid on disk, but it will be set in the in-core
		 * superblock. We set the uuid pointer for verification
		 * according to the superblock feature mask to ensure we check
		 * the relevant UUID in the superblock.
		 */
		lsn = be64_to_cpu(((struct xfs_dsb *)blk)->sb_lsn);
		if (xfs_sb_version_hasmetauuid(&mp->m_sb))
			uuid = &((struct xfs_dsb *)blk)->sb_meta_uuid;
		else
			uuid = &((struct xfs_dsb *)blk)->sb_uuid;
		break;
	default:
		break;
	}

	if (lsn != (xfs_lsn_t)-1) {
		if (!uuid_equal(&mp->m_sb.sb_meta_uuid, uuid))
			goto recover_immediately;
		return lsn;
	}

	magicda = be16_to_cpu(((struct xfs_da_blkinfo *)blk)->magic);
	switch (magicda) {
	case XFS_DIR3_LEAF1_MAGIC:
	case XFS_DIR3_LEAFN_MAGIC:
	case XFS_DA3_NODE_MAGIC:
		lsn = be64_to_cpu(((struct xfs_da3_blkinfo *)blk)->lsn);
		uuid = &((struct xfs_da3_blkinfo *)blk)->uuid;
		break;
	default:
		break;
	}

	if (lsn != (xfs_lsn_t)-1) {
		if (!uuid_equal(&mp->m_sb.sb_uuid, uuid))
			goto recover_immediately;
		return lsn;
	}

	/*
	 * We do individual object checks on dquot and inode buffers as they
	 * have their own individual LSN records. Also, we could have a stale
	 * buffer here, so we have to at least recognise these buffer types.
	 *
	 * A notd complexity here is inode unlinked list processing - it logs
	 * the inode directly in the buffer, but we don't know which inodes have
	 * been modified, and there is no global buffer LSN. Hence we need to
	 * recover all inode buffer types immediately. This problem will be
	 * fixed by logical logging of the unlinked list modifications.
	 */
	magic16 = be16_to_cpu(*(__be16 *)blk);
	switch (magic16) {
	case XFS_DQUOT_MAGIC:
	case XFS_DINODE_MAGIC:
		goto recover_immediately;
	default:
		break;
	}

	/* unknown buffer contents, recover immediately */

recover_immediately:
	return (xfs_lsn_t)-1;

}

/*
 * This routine replays a modification made to a buffer at runtime.
 * There are actually two types of buffer, regular and inode, which
 * are handled differently.  Inode buffers are handled differently
 * in that we only recover a specific set of data from them, namely
 * the inode di_next_unlinked fields.  This is because all other inode
 * data is actually logged via inode records and any data we replay
 * here which overlaps that may be stale.
 *
 * When meta-data buffers are freed at run time we log a buffer item
 * with the XFS_BLF_CANCEL bit set to indicate that previous copies
 * of the buffer in the log should not be replayed at recovery time.
 * This is so that if the blocks covered by the buffer are reused for
 * file data before we crash we don't end up replaying old, freed
 * meta-data into a user's file.
 *
 * To handle the cancellation of buffer log items, we make two passes
 * over the log during recovery.  During the first we build a table of
 * those buffers which have been cancelled, and during the second we
 * only replay those buffers which do not have corresponding cancel
 * records in the table.  See xlog_recover_buf_pass[1,2] above
 * for more details on the implementation of the table of cancel records.
 */
STATIC int
xlog_recover_buf_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			current_lsn)
{
	struct xfs_buf_log_format	*buf_f = item->ri_buf[0].i_addr;
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_buf			*bp;
	int				error;
	uint				buf_flags;
	xfs_lsn_t			lsn;

	/*
	 * In this pass we only want to recover all the buffers which have
	 * not been cancelled and are not cancellation buffers themselves.
	 */
	if (buf_f->blf_flags & XFS_BLF_CANCEL) {
		if (xlog_put_buffer_cancelled(log, buf_f->blf_blkno,
				buf_f->blf_len))
			goto cancelled;
	} else {

		if (xlog_is_buffer_cancelled(log, buf_f->blf_blkno,
				buf_f->blf_len))
			goto cancelled;
	}

	trace_xfs_log_recover_buf_recover(log, buf_f);

	buf_flags = 0;
	if (buf_f->blf_flags & XFS_BLF_INODE_BUF)
		buf_flags |= XBF_UNMAPPED;

	error = xfs_buf_read(mp->m_ddev_targp, buf_f->blf_blkno, buf_f->blf_len,
			  buf_flags, &bp, NULL);
	if (error)
		return error;

	/*
	 * Recover the buffer only if we get an LSN from it and it's less than
	 * the lsn of the transaction we are replaying.
	 *
	 * Note that we have to be extremely careful of readahead here.
	 * Readahead does not attach verfiers to the buffers so if we don't
	 * actually do any replay after readahead because of the LSN we found
	 * in the buffer if more recent than that current transaction then we
	 * need to attach the verifier directly. Failure to do so can lead to
	 * future recovery actions (e.g. EFI and unlinked list recovery) can
	 * operate on the buffers and they won't get the verifier attached. This
	 * can lead to blocks on disk having the correct content but a stale
	 * CRC.
	 *
	 * It is safe to assume these clean buffers are currently up to date.
	 * If the buffer is dirtied by a later transaction being replayed, then
	 * the verifier will be reset to match whatever recover turns that
	 * buffer into.
	 */
	lsn = xlog_recover_get_buf_lsn(mp, bp);
	if (lsn && lsn != -1 && XFS_LSN_CMP(lsn, current_lsn) >= 0) {
		trace_xfs_log_recover_buf_skip(log, buf_f);
		xlog_recover_validate_buf_type(mp, bp, buf_f, NULLCOMMITLSN);
		goto out_release;
	}

	if (buf_f->blf_flags & XFS_BLF_INODE_BUF) {
		error = xlog_recover_do_inode_buffer(mp, item, bp, buf_f);
		if (error)
			goto out_release;
	} else if (buf_f->blf_flags &
		  (XFS_BLF_UDQUOT_BUF|XFS_BLF_PDQUOT_BUF|XFS_BLF_GDQUOT_BUF)) {
		bool	dirty;

		dirty = xlog_recover_do_dquot_buffer(mp, log, item, bp, buf_f);
		if (!dirty)
			goto out_release;
	} else {
		xlog_recover_do_reg_buffer(mp, item, bp, buf_f, current_lsn);
	}

	/*
	 * Perform delayed write on the buffer.  Asynchronous writes will be
	 * slower when taking into account all the buffers to be flushed.
	 *
	 * Also make sure that only inode buffers with good sizes stay in
	 * the buffer cache.  The kernel moves inodes in buffers of 1 block
	 * or inode_cluster_size bytes, whichever is bigger.  The inode
	 * buffers in the log can be a different size if the log was generated
	 * by an older kernel using unclustered inode buffers or a newer kernel
	 * running with a different inode cluster size.  Regardless, if
	 * the inode buffer size isn't max(blocksize, inode_cluster_size)
	 * for *our* value of inode_cluster_size, then we need to keep
	 * the buffer out of the buffer cache so that the buffer won't
	 * overlap with future reads of those inodes.
	 */
	if (XFS_DINODE_MAGIC ==
	    be16_to_cpu(*((__be16 *)xfs_buf_offset(bp, 0))) &&
	    (BBTOB(bp->b_length) != M_IGEO(log->l_mp)->inode_cluster_size)) {
		xfs_buf_stale(bp);
		error = xfs_bwrite(bp);
	} else {
		ASSERT(bp->b_mount == mp);
		bp->b_flags |= _XBF_LOGRECOVERY;
		xfs_buf_delwri_queue(bp, buffer_list);
	}

out_release:
	xfs_buf_relse(bp);
	return error;
cancelled:
	trace_xfs_log_recover_buf_cancel(log, buf_f);
	return 0;
}

const struct xlog_recover_item_ops xlog_buf_item_ops = {
	.item_type		= XFS_LI_BUF,
	.reorder		= xlog_recover_buf_reorder,
	.ra_pass2		= xlog_recover_buf_ra_pass2,
	.commit_pass1		= xlog_recover_buf_commit_pass1,
	.commit_pass2		= xlog_recover_buf_commit_pass2,
};
