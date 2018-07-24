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
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_icreate_item.h"
#include "xfs_log.h"

kmem_zone_t	*xfs_icreate_zone;		/* inode create item zone */

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


/* Pinning has no meaning for the create item, so just return. */
STATIC void
xfs_icreate_item_pin(
	struct xfs_log_item	*lip)
{
}


/* pinning has no meaning for the create item, so just return. */
STATIC void
xfs_icreate_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
}

STATIC void
xfs_icreate_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_icreate_item	*icp = ICR_ITEM(lip);

	if (test_bit(XFS_LI_ABORTED, &lip->li_flags))
		kmem_zone_free(xfs_icreate_zone, icp);
	return;
}

/*
 * Because we have ordered buffers being tracked in the AIL for the inode
 * creation, we don't need the create item after this. Hence we can free
 * the log item and return -1 to tell the caller we're done with the item.
 */
STATIC xfs_lsn_t
xfs_icreate_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_icreate_item	*icp = ICR_ITEM(lip);

	kmem_zone_free(xfs_icreate_zone, icp);
	return (xfs_lsn_t)-1;
}

/* item can never get into the AIL */
STATIC uint
xfs_icreate_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	ASSERT(0);
	return XFS_ITEM_SUCCESS;
}

/* Ordered buffers do the dependency tracking here, so this does nothing. */
STATIC void
xfs_icreate_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all buf log items.
 */
static const struct xfs_item_ops xfs_icreate_item_ops = {
	.iop_size	= xfs_icreate_item_size,
	.iop_format	= xfs_icreate_item_format,
	.iop_pin	= xfs_icreate_item_pin,
	.iop_unpin	= xfs_icreate_item_unpin,
	.iop_push	= xfs_icreate_item_push,
	.iop_unlock	= xfs_icreate_item_unlock,
	.iop_committed	= xfs_icreate_item_committed,
	.iop_committing = xfs_icreate_item_committing,
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

	icp = kmem_zone_zalloc(xfs_icreate_zone, KM_SLEEP);

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
