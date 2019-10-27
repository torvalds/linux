// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008-2010, 2013 Dave Chinner
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
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

STATIC void
xfs_icreate_item_release(
	struct xfs_log_item	*lip)
{
	kmem_zone_free(xfs_icreate_zone, ICR_ITEM(lip));
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

	icp = kmem_zone_zalloc(xfs_icreate_zone, 0);

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
