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
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"
#include "xfs_log.h"


kmem_zone_t	*xfs_efi_zone;
kmem_zone_t	*xfs_efd_zone;

static inline struct xfs_efi_log_item *EFI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_efi_log_item, efi_item);
}

void
xfs_efi_item_free(
	struct xfs_efi_log_item	*efip)
{
	if (efip->efi_format.efi_nextents > XFS_EFI_MAX_FAST_EXTENTS)
		kmem_free(efip);
	else
		kmem_zone_free(xfs_efi_zone, efip);
}

/*
 * Freeing the efi requires that we remove it from the AIL if it has already
 * been placed there. However, the EFI may not yet have been placed in the AIL
 * when called by xfs_efi_release() from EFD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the EFI.
 */
STATIC void
__xfs_efi_release(
	struct xfs_efi_log_item	*efip)
{
	struct xfs_ail		*ailp = efip->efi_item.li_ailp;

	if (atomic_dec_and_test(&efip->efi_refcount)) {
		spin_lock(&ailp->xa_lock);
		/* xfs_trans_ail_delete() drops the AIL lock. */
		xfs_trans_ail_delete(ailp, &efip->efi_item,
				     SHUTDOWN_LOG_IO_ERROR);
		xfs_efi_item_free(efip);
	}
}

/*
 * This returns the number of iovecs needed to log the given efi item.
 * We only need 1 iovec for an efi item.  It just logs the efi_log_format
 * structure.
 */
static inline int
xfs_efi_item_sizeof(
	struct xfs_efi_log_item *efip)
{
	return sizeof(struct xfs_efi_log_format) +
	       (efip->efi_format.efi_nextents - 1) * sizeof(xfs_extent_t);
}

STATIC void
xfs_efi_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += xfs_efi_item_sizeof(EFI_ITEM(lip));
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efi log item. We use only 1 iovec, and we point that
 * at the efi_log_format structure embedded in the efi item.
 * It is at this point that we assert that all of the extent
 * slots in the efi item have been filled.
 */
STATIC void
xfs_efi_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_efi_log_item	*efip = EFI_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(atomic_read(&efip->efi_next_extent) ==
				efip->efi_format.efi_nextents);

	efip->efi_format.efi_type = XFS_LI_EFI;
	efip->efi_format.efi_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_EFI_FORMAT,
			&efip->efi_format,
			xfs_efi_item_sizeof(efip));
}


/*
 * Pinning has no meaning for an efi item, so just return.
 */
STATIC void
xfs_efi_item_pin(
	struct xfs_log_item	*lip)
{
}

/*
 * While EFIs cannot really be pinned, the unpin operation is the last place at
 * which the EFI is manipulated during a transaction.  If we are being asked to
 * remove the EFI it's because the transaction has been cancelled and by
 * definition that means the EFI cannot be in the AIL so remove it from the
 * transaction and free it.  Otherwise coordinate with xfs_efi_release()
 * to determine who gets to free the EFI.
 */
STATIC void
xfs_efi_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_efi_log_item	*efip = EFI_ITEM(lip);

	if (remove) {
		ASSERT(!(lip->li_flags & XFS_LI_IN_AIL));
		if (lip->li_desc)
			xfs_trans_del_item(lip);
		xfs_efi_item_free(efip);
		return;
	}
	__xfs_efi_release(efip);
}

/*
 * Efi items have no locking or pushing.  However, since EFIs are pulled from
 * the AIL when their corresponding EFDs are committed to disk, their situation
 * is very similar to being pinned.  Return XFS_ITEM_PINNED so that the caller
 * will eventually flush the log.  This should help in getting the EFI out of
 * the AIL.
 */
STATIC uint
xfs_efi_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_PINNED;
}

STATIC void
xfs_efi_item_unlock(
	struct xfs_log_item	*lip)
{
	if (lip->li_flags & XFS_LI_ABORTED)
		xfs_efi_item_free(EFI_ITEM(lip));
}

/*
 * The EFI is logged only once and cannot be moved in the log, so simply return
 * the lsn at which it's been logged.
 */
STATIC xfs_lsn_t
xfs_efi_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	return lsn;
}

/*
 * The EFI dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
STATIC void
xfs_efi_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all efi log items.
 */
static const struct xfs_item_ops xfs_efi_item_ops = {
	.iop_size	= xfs_efi_item_size,
	.iop_format	= xfs_efi_item_format,
	.iop_pin	= xfs_efi_item_pin,
	.iop_unpin	= xfs_efi_item_unpin,
	.iop_unlock	= xfs_efi_item_unlock,
	.iop_committed	= xfs_efi_item_committed,
	.iop_push	= xfs_efi_item_push,
	.iop_committing = xfs_efi_item_committing
};


/*
 * Allocate and initialize an efi item with the given number of extents.
 */
struct xfs_efi_log_item *
xfs_efi_init(
	struct xfs_mount	*mp,
	uint			nextents)

{
	struct xfs_efi_log_item	*efip;
	uint			size;

	ASSERT(nextents > 0);
	if (nextents > XFS_EFI_MAX_FAST_EXTENTS) {
		size = (uint)(sizeof(xfs_efi_log_item_t) +
			((nextents - 1) * sizeof(xfs_extent_t)));
		efip = kmem_zalloc(size, KM_SLEEP);
	} else {
		efip = kmem_zone_zalloc(xfs_efi_zone, KM_SLEEP);
	}

	xfs_log_item_init(mp, &efip->efi_item, XFS_LI_EFI, &xfs_efi_item_ops);
	efip->efi_format.efi_nextents = nextents;
	efip->efi_format.efi_id = (__psint_t)(void*)efip;
	atomic_set(&efip->efi_next_extent, 0);
	atomic_set(&efip->efi_refcount, 2);

	return efip;
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
	xfs_efi_log_format_t *src_efi_fmt = buf->i_addr;
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
		xfs_efi_log_format_32_t *src_efi_fmt_32 = buf->i_addr;

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
		xfs_efi_log_format_64_t *src_efi_fmt_64 = buf->i_addr;

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
 * This is called by the efd item code below to release references to the given
 * efi item.  Each efd calls this with the number of extents that it has
 * logged, and when the sum of these reaches the total number of extents logged
 * by this efi item we can free the efi item.
 */
void
xfs_efi_release(xfs_efi_log_item_t	*efip,
		uint			nextents)
{
	ASSERT(atomic_read(&efip->efi_next_extent) >= nextents);
	if (atomic_sub_and_test(nextents, &efip->efi_next_extent)) {
		/* recovery needs us to drop the EFI reference, too */
		if (test_bit(XFS_EFI_RECOVERED, &efip->efi_flags))
			__xfs_efi_release(efip);

		__xfs_efi_release(efip);
		/* efip may now have been freed, do not reference it again. */
	}
}

static inline struct xfs_efd_log_item *EFD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_efd_log_item, efd_item);
}

STATIC void
xfs_efd_item_free(struct xfs_efd_log_item *efdp)
{
	if (efdp->efd_format.efd_nextents > XFS_EFD_MAX_FAST_EXTENTS)
		kmem_free(efdp);
	else
		kmem_zone_free(xfs_efd_zone, efdp);
}

/*
 * This returns the number of iovecs needed to log the given efd item.
 * We only need 1 iovec for an efd item.  It just logs the efd_log_format
 * structure.
 */
static inline int
xfs_efd_item_sizeof(
	struct xfs_efd_log_item *efdp)
{
	return sizeof(xfs_efd_log_format_t) +
	       (efdp->efd_format.efd_nextents - 1) * sizeof(xfs_extent_t);
}

STATIC void
xfs_efd_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += xfs_efd_item_sizeof(EFD_ITEM(lip));
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efd log item. We use only 1 iovec, and we point that
 * at the efd_log_format structure embedded in the efd item.
 * It is at this point that we assert that all of the extent
 * slots in the efd item have been filled.
 */
STATIC void
xfs_efd_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(efdp->efd_next_extent == efdp->efd_format.efd_nextents);

	efdp->efd_format.efd_type = XFS_LI_EFD;
	efdp->efd_format.efd_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_EFD_FORMAT,
			&efdp->efd_format,
			xfs_efd_item_sizeof(efdp));
}

/*
 * Pinning has no meaning for an efd item, so just return.
 */
STATIC void
xfs_efd_item_pin(
	struct xfs_log_item	*lip)
{
}

/*
 * Since pinning has no meaning for an efd item, unpinning does
 * not either.
 */
STATIC void
xfs_efd_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
}

/*
 * There isn't much you can do to push on an efd item.  It is simply stuck
 * waiting for the log to be flushed to disk.
 */
STATIC uint
xfs_efd_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_PINNED;
}

STATIC void
xfs_efd_item_unlock(
	struct xfs_log_item	*lip)
{
	if (lip->li_flags & XFS_LI_ABORTED)
		xfs_efd_item_free(EFD_ITEM(lip));
}

/*
 * When the efd item is committed to disk, all we need to do
 * is delete our reference to our partner efi item and then
 * free ourselves.  Since we're freeing ourselves we must
 * return -1 to keep the transaction code from further referencing
 * this item.
 */
STATIC xfs_lsn_t
xfs_efd_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);

	/*
	 * If we got a log I/O error, it's always the case that the LR with the
	 * EFI got unpinned and freed before the EFD got aborted.
	 */
	if (!(lip->li_flags & XFS_LI_ABORTED))
		xfs_efi_release(efdp->efd_efip, efdp->efd_format.efd_nextents);

	xfs_efd_item_free(efdp);
	return (xfs_lsn_t)-1;
}

/*
 * The EFD dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
STATIC void
xfs_efd_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all efd log items.
 */
static const struct xfs_item_ops xfs_efd_item_ops = {
	.iop_size	= xfs_efd_item_size,
	.iop_format	= xfs_efd_item_format,
	.iop_pin	= xfs_efd_item_pin,
	.iop_unpin	= xfs_efd_item_unpin,
	.iop_unlock	= xfs_efd_item_unlock,
	.iop_committed	= xfs_efd_item_committed,
	.iop_push	= xfs_efd_item_push,
	.iop_committing = xfs_efd_item_committing
};

/*
 * Allocate and initialize an efd item with the given number of extents.
 */
struct xfs_efd_log_item *
xfs_efd_init(
	struct xfs_mount	*mp,
	struct xfs_efi_log_item	*efip,
	uint			nextents)

{
	struct xfs_efd_log_item	*efdp;
	uint			size;

	ASSERT(nextents > 0);
	if (nextents > XFS_EFD_MAX_FAST_EXTENTS) {
		size = (uint)(sizeof(xfs_efd_log_item_t) +
			((nextents - 1) * sizeof(xfs_extent_t)));
		efdp = kmem_zalloc(size, KM_SLEEP);
	} else {
		efdp = kmem_zone_zalloc(xfs_efd_zone, KM_SLEEP);
	}

	xfs_log_item_init(mp, &efdp->efd_item, XFS_LI_EFD, &xfs_efd_item_ops);
	efdp->efd_efip = efip;
	efdp->efd_format.efd_nextents = nextents;
	efdp->efd_format.efd_efi_id = efip->efi_format.efi_id;

	return efdp;
}
