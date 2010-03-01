/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#include "xfs_buf_item.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_extfree_item.h"


kmem_zone_t	*xfs_efi_zone;
kmem_zone_t	*xfs_efd_zone;

STATIC void	xfs_efi_item_unlock(xfs_efi_log_item_t *);

void
xfs_efi_item_free(xfs_efi_log_item_t *efip)
{
	int nexts = efip->efi_format.efi_nextents;

	if (nexts > XFS_EFI_MAX_FAST_EXTENTS) {
		kmem_free(efip);
	} else {
		kmem_zone_free(xfs_efi_zone, efip);
	}
}

/*
 * This returns the number of iovecs needed to log the given efi item.
 * We only need 1 iovec for an efi item.  It just logs the efi_log_format
 * structure.
 */
/*ARGSUSED*/
STATIC uint
xfs_efi_item_size(xfs_efi_log_item_t *efip)
{
	return 1;
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efi log item. We use only 1 iovec, and we point that
 * at the efi_log_format structure embedded in the efi item.
 * It is at this point that we assert that all of the extent
 * slots in the efi item have been filled.
 */
STATIC void
xfs_efi_item_format(xfs_efi_log_item_t	*efip,
		    xfs_log_iovec_t	*log_vector)
{
	uint	size;

	ASSERT(efip->efi_next_extent == efip->efi_format.efi_nextents);

	efip->efi_format.efi_type = XFS_LI_EFI;

	size = sizeof(xfs_efi_log_format_t);
	size += (efip->efi_format.efi_nextents - 1) * sizeof(xfs_extent_t);
	efip->efi_format.efi_size = 1;

	log_vector->i_addr = (xfs_caddr_t)&(efip->efi_format);
	log_vector->i_len = size;
	log_vector->i_type = XLOG_REG_TYPE_EFI_FORMAT;
	ASSERT(size >= sizeof(xfs_efi_log_format_t));
}


/*
 * Pinning has no meaning for an efi item, so just return.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_pin(xfs_efi_log_item_t *efip)
{
	return;
}


/*
 * While EFIs cannot really be pinned, the unpin operation is the
 * last place at which the EFI is manipulated during a transaction.
 * Here we coordinate with xfs_efi_cancel() to determine who gets to
 * free the EFI.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_unpin(xfs_efi_log_item_t *efip, int stale)
{
	struct xfs_ail		*ailp = efip->efi_item.li_ailp;

	spin_lock(&ailp->xa_lock);
	if (efip->efi_flags & XFS_EFI_CANCELED) {
		/* xfs_trans_ail_delete() drops the AIL lock. */
		xfs_trans_ail_delete(ailp, (xfs_log_item_t *)efip);
		xfs_efi_item_free(efip);
	} else {
		efip->efi_flags |= XFS_EFI_COMMITTED;
		spin_unlock(&ailp->xa_lock);
	}
}

/*
 * like unpin only we have to also clear the xaction descriptor
 * pointing the log item if we free the item.  This routine duplicates
 * unpin because efi_flags is protected by the AIL lock.  Freeing
 * the descriptor and then calling unpin would force us to drop the AIL
 * lock which would open up a race condition.
 */
STATIC void
xfs_efi_item_unpin_remove(xfs_efi_log_item_t *efip, xfs_trans_t *tp)
{
	struct xfs_ail		*ailp = efip->efi_item.li_ailp;
	xfs_log_item_desc_t	*lidp;

	spin_lock(&ailp->xa_lock);
	if (efip->efi_flags & XFS_EFI_CANCELED) {
		/*
		 * free the xaction descriptor pointing to this item
		 */
		lidp = xfs_trans_find_item(tp, (xfs_log_item_t *) efip);
		xfs_trans_free_item(tp, lidp);

		/* xfs_trans_ail_delete() drops the AIL lock. */
		xfs_trans_ail_delete(ailp, (xfs_log_item_t *)efip);
		xfs_efi_item_free(efip);
	} else {
		efip->efi_flags |= XFS_EFI_COMMITTED;
		spin_unlock(&ailp->xa_lock);
	}
}

/*
 * Efi items have no locking or pushing.  However, since EFIs are
 * pulled from the AIL when their corresponding EFDs are committed
 * to disk, their situation is very similar to being pinned.  Return
 * XFS_ITEM_PINNED so that the caller will eventually flush the log.
 * This should help in getting the EFI out of the AIL.
 */
/*ARGSUSED*/
STATIC uint
xfs_efi_item_trylock(xfs_efi_log_item_t *efip)
{
	return XFS_ITEM_PINNED;
}

/*
 * Efi items have no locking, so just return.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_unlock(xfs_efi_log_item_t *efip)
{
	if (efip->efi_item.li_flags & XFS_LI_ABORTED)
		xfs_efi_item_free(efip);
	return;
}

/*
 * The EFI is logged only once and cannot be moved in the log, so
 * simply return the lsn at which it's been logged.  The canceled
 * flag is not paid any attention here.  Checking for that is delayed
 * until the EFI is unpinned.
 */
/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_efi_item_committed(xfs_efi_log_item_t *efip, xfs_lsn_t lsn)
{
	return lsn;
}

/*
 * There isn't much you can do to push on an efi item.  It is simply
 * stuck waiting for all of its corresponding efd items to be
 * committed to disk.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_push(xfs_efi_log_item_t *efip)
{
	return;
}

/*
 * The EFI dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
/*ARGSUSED*/
STATIC void
xfs_efi_item_committing(xfs_efi_log_item_t *efip, xfs_lsn_t lsn)
{
	return;
}

/*
 * This is the ops vector shared by all efi log items.
 */
static struct xfs_item_ops xfs_efi_item_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_efi_item_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_efi_item_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_efi_item_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*, int))xfs_efi_item_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*, xfs_trans_t *))
					xfs_efi_item_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))xfs_efi_item_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_efi_item_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_efi_item_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_efi_item_push,
	.iop_pushbuf	= NULL,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_efi_item_committing
};


/*
 * Allocate and initialize an efi item with the given number of extents.
 */
xfs_efi_log_item_t *
xfs_efi_init(xfs_mount_t	*mp,
	     uint		nextents)

{
	xfs_efi_log_item_t	*efip;
	uint			size;

	ASSERT(nextents > 0);
	if (nextents > XFS_EFI_MAX_FAST_EXTENTS) {
		size = (uint)(sizeof(xfs_efi_log_item_t) +
			((nextents - 1) * sizeof(xfs_extent_t)));
		efip = (xfs_efi_log_item_t*)kmem_zalloc(size, KM_SLEEP);
	} else {
		efip = (xfs_efi_log_item_t*)kmem_zone_zalloc(xfs_efi_zone,
							     KM_SLEEP);
	}

	efip->efi_item.li_type = XFS_LI_EFI;
	efip->efi_item.li_ops = &xfs_efi_item_ops;
	efip->efi_item.li_mountp = mp;
	efip->efi_item.li_ailp = mp->m_ail;
	efip->efi_format.efi_nextents = nextents;
	efip->efi_format.efi_id = (__psint_t)(void*)efip;

	return (efip);
}

/*
 * Copy an EFI format buffer from the given buf, and into the destination
 * EFI format structure.
 * The given buffer can be in 32 bit or 64 bit form (which has different padding),
 * one of which will be the native format for this kernel.
 * It will handle the conversion of formats if necessary.
 */
int
xfs_efi_copy_format(xfs_log_iovec_t *buf, xfs_efi_log_format_t *dst_efi_fmt)
{
	xfs_efi_log_format_t *src_efi_fmt = (xfs_efi_log_format_t *)buf->i_addr;
	uint i;
	uint len = sizeof(xfs_efi_log_format_t) + 
		(src_efi_fmt->efi_nextents - 1) * sizeof(xfs_extent_t);  
	uint len32 = sizeof(xfs_efi_log_format_32_t) + 
		(src_efi_fmt->efi_nextents - 1) * sizeof(xfs_extent_32_t);  
	uint len64 = sizeof(xfs_efi_log_format_64_t) + 
		(src_efi_fmt->efi_nextents - 1) * sizeof(xfs_extent_64_t);  

	if (buf->i_len == len) {
		memcpy((char *)dst_efi_fmt, (char*)src_efi_fmt, len);
		return 0;
	} else if (buf->i_len == len32) {
		xfs_efi_log_format_32_t *src_efi_fmt_32 =
			(xfs_efi_log_format_32_t *)buf->i_addr;

		dst_efi_fmt->efi_type     = src_efi_fmt_32->efi_type;
		dst_efi_fmt->efi_size     = src_efi_fmt_32->efi_size;
		dst_efi_fmt->efi_nextents = src_efi_fmt_32->efi_nextents;
		dst_efi_fmt->efi_id       = src_efi_fmt_32->efi_id;
		for (i = 0; i < dst_efi_fmt->efi_nextents; i++) {
			dst_efi_fmt->efi_extents[i].ext_start =
				src_efi_fmt_32->efi_extents[i].ext_start;
			dst_efi_fmt->efi_extents[i].ext_len =
				src_efi_fmt_32->efi_extents[i].ext_len;
		}
		return 0;
	} else if (buf->i_len == len64) {
		xfs_efi_log_format_64_t *src_efi_fmt_64 =
			(xfs_efi_log_format_64_t *)buf->i_addr;

		dst_efi_fmt->efi_type     = src_efi_fmt_64->efi_type;
		dst_efi_fmt->efi_size     = src_efi_fmt_64->efi_size;
		dst_efi_fmt->efi_nextents = src_efi_fmt_64->efi_nextents;
		dst_efi_fmt->efi_id       = src_efi_fmt_64->efi_id;
		for (i = 0; i < dst_efi_fmt->efi_nextents; i++) {
			dst_efi_fmt->efi_extents[i].ext_start =
				src_efi_fmt_64->efi_extents[i].ext_start;
			dst_efi_fmt->efi_extents[i].ext_len =
				src_efi_fmt_64->efi_extents[i].ext_len;
		}
		return 0;
	}
	return EFSCORRUPTED;
}

/*
 * This is called by the efd item code below to release references to
 * the given efi item.  Each efd calls this with the number of
 * extents that it has logged, and when the sum of these reaches
 * the total number of extents logged by this efi item we can free
 * the efi item.
 *
 * Freeing the efi item requires that we remove it from the AIL.
 * We'll use the AIL lock to protect our counters as well as
 * the removal from the AIL.
 */
void
xfs_efi_release(xfs_efi_log_item_t	*efip,
		uint			nextents)
{
	struct xfs_ail		*ailp = efip->efi_item.li_ailp;
	int			extents_left;

	ASSERT(efip->efi_next_extent > 0);
	ASSERT(efip->efi_flags & XFS_EFI_COMMITTED);

	spin_lock(&ailp->xa_lock);
	ASSERT(efip->efi_next_extent >= nextents);
	efip->efi_next_extent -= nextents;
	extents_left = efip->efi_next_extent;
	if (extents_left == 0) {
		/* xfs_trans_ail_delete() drops the AIL lock. */
		xfs_trans_ail_delete(ailp, (xfs_log_item_t *)efip);
		xfs_efi_item_free(efip);
	} else {
		spin_unlock(&ailp->xa_lock);
	}
}

STATIC void
xfs_efd_item_free(xfs_efd_log_item_t *efdp)
{
	int nexts = efdp->efd_format.efd_nextents;

	if (nexts > XFS_EFD_MAX_FAST_EXTENTS) {
		kmem_free(efdp);
	} else {
		kmem_zone_free(xfs_efd_zone, efdp);
	}
}

/*
 * This returns the number of iovecs needed to log the given efd item.
 * We only need 1 iovec for an efd item.  It just logs the efd_log_format
 * structure.
 */
/*ARGSUSED*/
STATIC uint
xfs_efd_item_size(xfs_efd_log_item_t *efdp)
{
	return 1;
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efd log item. We use only 1 iovec, and we point that
 * at the efd_log_format structure embedded in the efd item.
 * It is at this point that we assert that all of the extent
 * slots in the efd item have been filled.
 */
STATIC void
xfs_efd_item_format(xfs_efd_log_item_t	*efdp,
		    xfs_log_iovec_t	*log_vector)
{
	uint	size;

	ASSERT(efdp->efd_next_extent == efdp->efd_format.efd_nextents);

	efdp->efd_format.efd_type = XFS_LI_EFD;

	size = sizeof(xfs_efd_log_format_t);
	size += (efdp->efd_format.efd_nextents - 1) * sizeof(xfs_extent_t);
	efdp->efd_format.efd_size = 1;

	log_vector->i_addr = (xfs_caddr_t)&(efdp->efd_format);
	log_vector->i_len = size;
	log_vector->i_type = XLOG_REG_TYPE_EFD_FORMAT;
	ASSERT(size >= sizeof(xfs_efd_log_format_t));
}


/*
 * Pinning has no meaning for an efd item, so just return.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_pin(xfs_efd_log_item_t *efdp)
{
	return;
}


/*
 * Since pinning has no meaning for an efd item, unpinning does
 * not either.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_unpin(xfs_efd_log_item_t *efdp, int stale)
{
	return;
}

/*ARGSUSED*/
STATIC void
xfs_efd_item_unpin_remove(xfs_efd_log_item_t *efdp, xfs_trans_t *tp)
{
	return;
}

/*
 * Efd items have no locking, so just return success.
 */
/*ARGSUSED*/
STATIC uint
xfs_efd_item_trylock(xfs_efd_log_item_t *efdp)
{
	return XFS_ITEM_LOCKED;
}

/*
 * Efd items have no locking or pushing, so return failure
 * so that the caller doesn't bother with us.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_unlock(xfs_efd_log_item_t *efdp)
{
	if (efdp->efd_item.li_flags & XFS_LI_ABORTED)
		xfs_efd_item_free(efdp);
	return;
}

/*
 * When the efd item is committed to disk, all we need to do
 * is delete our reference to our partner efi item and then
 * free ourselves.  Since we're freeing ourselves we must
 * return -1 to keep the transaction code from further referencing
 * this item.
 */
/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_efd_item_committed(xfs_efd_log_item_t *efdp, xfs_lsn_t lsn)
{
	/*
	 * If we got a log I/O error, it's always the case that the LR with the
	 * EFI got unpinned and freed before the EFD got aborted.
	 */
	if ((efdp->efd_item.li_flags & XFS_LI_ABORTED) == 0)
		xfs_efi_release(efdp->efd_efip, efdp->efd_format.efd_nextents);

	xfs_efd_item_free(efdp);
	return (xfs_lsn_t)-1;
}

/*
 * There isn't much you can do to push on an efd item.  It is simply
 * stuck waiting for the log to be flushed to disk.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_push(xfs_efd_log_item_t *efdp)
{
	return;
}

/*
 * The EFD dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
/*ARGSUSED*/
STATIC void
xfs_efd_item_committing(xfs_efd_log_item_t *efip, xfs_lsn_t lsn)
{
	return;
}

/*
 * This is the ops vector shared by all efd log items.
 */
static struct xfs_item_ops xfs_efd_item_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_efd_item_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_efd_item_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_efd_item_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*, int))xfs_efd_item_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*, xfs_trans_t*))
					xfs_efd_item_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))xfs_efd_item_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_efd_item_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_efd_item_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_efd_item_push,
	.iop_pushbuf	= NULL,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_efd_item_committing
};


/*
 * Allocate and initialize an efd item with the given number of extents.
 */
xfs_efd_log_item_t *
xfs_efd_init(xfs_mount_t	*mp,
	     xfs_efi_log_item_t	*efip,
	     uint		nextents)

{
	xfs_efd_log_item_t	*efdp;
	uint			size;

	ASSERT(nextents > 0);
	if (nextents > XFS_EFD_MAX_FAST_EXTENTS) {
		size = (uint)(sizeof(xfs_efd_log_item_t) +
			((nextents - 1) * sizeof(xfs_extent_t)));
		efdp = (xfs_efd_log_item_t*)kmem_zalloc(size, KM_SLEEP);
	} else {
		efdp = (xfs_efd_log_item_t*)kmem_zone_zalloc(xfs_efd_zone,
							     KM_SLEEP);
	}

	efdp->efd_item.li_type = XFS_LI_EFD;
	efdp->efd_item.li_ops = &xfs_efd_item_ops;
	efdp->efd_item.li_mountp = mp;
	efdp->efd_item.li_ailp = mp->m_ail;
	efdp->efd_efip = efip;
	efdp->efd_format.efd_nextents = nextents;
	efdp->efd_format.efd_efi_id = efip->efi_format.efi_id;

	return (efdp);
}
