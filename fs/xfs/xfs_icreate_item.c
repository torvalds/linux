// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008-2010, 2013 Dave Chinner
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_icreate_item.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_ialloc.h"
#include "xfs_trace.h"

struct kmem_cache	*xfs_icreate_cache;		/* inode create item */

static inline struct xfs_icreate_item *ICR_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_icreate_item, ic_item);
}

/*
 * This returns the number of iovecs needed to log the given inode item.
 *
 * We only need one iovec for the icreate log structure.
 */
STATIC void
xfs_icreate_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_icreate_log);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given inode create log item.
 */
STATIC void
xfs_icreate_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_icreate_item	*icp = ICR_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ICREATE,
			&icp->ic_format,
			sizeof(struct xfs_icreate_log));
}

STATIC void
xfs_icreate_item_release(
	struct xfs_log_item	*lip)
{
	kmem_free(ICR_ITEM(lip)->ic_item.li_lv_shadow);
	kmem_cache_free(xfs_icreate_cache, ICR_ITEM(lip));
}

static const struct xfs_item_ops xfs_icreate_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED,
	.iop_size	= xfs_icreate_item_size,
	.iop_format	= xfs_icreate_item_format,
	.iop_release	= xfs_icreate_item_release,
};


/*
 * Initialize the inode log item for a newly allocated (in-core) inode.
 *
 * Inode extents can only reside within an AG. Hence specify the starting
 * block for the inode chunk by offset within an AG as well as the
 * length of the allocated extent.
 *
 * This joins the item to the transaction and marks it dirty so
 * that we don't need a separate call to do this, nor does the
 * caller need to know anything about the icreate item.
 */
void
xfs_icreate_log(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	unsigned int		count,
	unsigned int		inode_size,
	xfs_agblock_t		length,
	unsigned int		generation)
{
	struct xfs_icreate_item	*icp;

	icp = kmem_cache_zalloc(xfs_icreate_cache, GFP_KERNEL | __GFP_NOFAIL);

	xfs_log_item_init(tp->t_mountp, &icp->ic_item, XFS_LI_ICREATE,
			  &xfs_icreate_item_ops);

	icp->ic_format.icl_type = XFS_LI_ICREATE;
	icp->ic_format.icl_size = 1;	/* single vector */
	icp->ic_format.icl_ag = cpu_to_be32(agno);
	icp->ic_format.icl_agbno = cpu_to_be32(agbno);
	icp->ic_format.icl_count = cpu_to_be32(count);
	icp->ic_format.icl_isize = cpu_to_be32(inode_size);
	icp->ic_format.icl_length = cpu_to_be32(length);
	icp->ic_format.icl_gen = cpu_to_be32(generation);

	xfs_trans_add_item(tp, &icp->ic_item);
	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &icp->ic_item.li_flags);
}

static enum xlog_recover_reorder
xlog_recover_icreate_reorder(
		struct xlog_recover_item *item)
{
	/*
	 * Inode allocation buffers must be replayed before subsequent inode
	 * items try to modify those buffers.  ICREATE items are the logical
	 * equivalent of logging a newly initialized inode buffer, so recover
	 * these at the same time that we recover logged buffers.
	 */
	return XLOG_REORDER_BUFFER_LIST;
}

/*
 * This routine is called when an inode create format structure is found in a
 * committed transaction in the log.  It's purpose is to initialise the inodes
 * being allocated on disk. This requires us to get inode cluster buffers that
 * match the range to be initialised, stamped with inode templates and written
 * by delayed write so that subsequent modifications will hit the cached buffer
 * and only need writing out at the end of recovery.
 */
STATIC int
xlog_recover_icreate_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_icreate_log		*icl;
	struct xfs_ino_geometry		*igeo = M_IGEO(mp);
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;
	unsigned int			count;
	unsigned int			isize;
	xfs_agblock_t			length;
	int				bb_per_cluster;
	int				cancel_count;
	int				nbufs;
	int				i;

	icl = (struct xfs_icreate_log *)item->ri_buf[0].i_addr;
	if (icl->icl_type != XFS_LI_ICREATE) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad type");
		return -EINVAL;
	}

	if (icl->icl_size != 1) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad icl size");
		return -EINVAL;
	}

	agno = be32_to_cpu(icl->icl_ag);
	if (agno >= mp->m_sb.sb_agcount) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad agno");
		return -EINVAL;
	}
	agbno = be32_to_cpu(icl->icl_agbno);
	if (!agbno || agbno == NULLAGBLOCK || agbno >= mp->m_sb.sb_agblocks) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad agbno");
		return -EINVAL;
	}
	isize = be32_to_cpu(icl->icl_isize);
	if (isize != mp->m_sb.sb_inodesize) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad isize");
		return -EINVAL;
	}
	count = be32_to_cpu(icl->icl_count);
	if (!count) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad count");
		return -EINVAL;
	}
	length = be32_to_cpu(icl->icl_length);
	if (!length || length >= mp->m_sb.sb_agblocks) {
		xfs_warn(log->l_mp, "xlog_recover_do_icreate_trans: bad length");
		return -EINVAL;
	}

	/*
	 * The inode chunk is either full or sparse and we only support
	 * m_ino_geo.ialloc_min_blks sized sparse allocations at this time.
	 */
	if (length != igeo->ialloc_blks &&
	    length != igeo->ialloc_min_blks) {
		xfs_warn(log->l_mp,
			 "%s: unsupported chunk length", __func__);
		return -EINVAL;
	}

	/* verify inode count is consistent with extent length */
	if ((count >> mp->m_sb.sb_inopblog) != length) {
		xfs_warn(log->l_mp,
			 "%s: inconsistent inode count and chunk length",
			 __func__);
		return -EINVAL;
	}

	/*
	 * The icreate transaction can cover multiple cluster buffers and these
	 * buffers could have been freed and reused. Check the individual
	 * buffers for cancellation so we don't overwrite anything written after
	 * a cancellation.
	 */
	bb_per_cluster = XFS_FSB_TO_BB(mp, igeo->blocks_per_cluster);
	nbufs = length / igeo->blocks_per_cluster;
	for (i = 0, cancel_count = 0; i < nbufs; i++) {
		xfs_daddr_t	daddr;

		daddr = XFS_AGB_TO_DADDR(mp, agno,
				agbno + i * igeo->blocks_per_cluster);
		if (xlog_is_buffer_cancelled(log, daddr, bb_per_cluster))
			cancel_count++;
	}

	/*
	 * We currently only use icreate for a single allocation at a time. This
	 * means we should expect either all or none of the buffers to be
	 * cancelled. Be conservative and skip replay if at least one buffer is
	 * cancelled, but warn the user that something is awry if the buffers
	 * are not consistent.
	 *
	 * XXX: This must be refined to only skip cancelled clusters once we use
	 * icreate for multiple chunk allocations.
	 */
	ASSERT(!cancel_count || cancel_count == nbufs);
	if (cancel_count) {
		if (cancel_count != nbufs)
			xfs_warn(mp,
	"WARNING: partial inode chunk cancellation, skipped icreate.");
		trace_xfs_log_recover_icreate_cancel(log, icl);
		return 0;
	}

	trace_xfs_log_recover_icreate_recover(log, icl);
	return xfs_ialloc_inode_init(mp, NULL, buffer_list, count, agno, agbno,
				     length, be32_to_cpu(icl->icl_gen));
}

const struct xlog_recover_item_ops xlog_icreate_item_ops = {
	.item_type		= XFS_LI_ICREATE,
	.reorder		= xlog_recover_icreate_reorder,
	.commit_pass2		= xlog_recover_icreate_commit_pass2,
};
