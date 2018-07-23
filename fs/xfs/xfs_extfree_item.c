// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"
#include "xfs_log.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"


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
	kmem_free(efip->efi_item.li_lv_shadow);
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
void
xfs_efi_release(
	struct xfs_efi_log_item	*efip)
{
	ASSERT(atomic_read(&efip->efi_refcount) > 0);
	if (atomic_dec_and_test(&efip->efi_refcount)) {
		xfs_trans_ail_remove(&efip->efi_item, SHUTDOWN_LOG_IO_ERROR);
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
 * The unpin operation is the last place an EFI is manipulated in the log. It is
 * either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the EFI transaction has been successfully committed to make it
 * this far. Therefore, we expect whoever committed the EFI to either construct
 * and commit the EFD or drop the EFD's reference in the event of error. Simply
 * drop the log's EFI reference now that the log is done with it.
 */
STATIC void
xfs_efi_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_efi_log_item	*efip = EFI_ITEM(lip);
	xfs_efi_release(efip);
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

/*
 * The EFI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an EFD isn't going to be
 * constructed and thus we free the EFI here directly.
 */
STATIC void
xfs_efi_item_unlock(
	struct xfs_log_item	*lip)
{
	if (test_bit(XFS_LI_ABORTED, &lip->li_flags))
		xfs_efi_release(EFI_ITEM(lip));
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
	efip->efi_format.efi_id = (uintptr_t)(void *)efip;
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
	return -EFSCORRUPTED;
}

static inline struct xfs_efd_log_item *EFD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_efd_log_item, efd_item);
}

STATIC void
xfs_efd_item_free(struct xfs_efd_log_item *efdp)
{
	kmem_free(efdp->efd_item.li_lv_shadow);
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

/*
 * The EFD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the EFI and free the EFD.
 */
STATIC void
xfs_efd_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);

	if (test_bit(XFS_LI_ABORTED, &lip->li_flags)) {
		xfs_efi_release(efdp->efd_efip);
		xfs_efd_item_free(efdp);
	}
}

/*
 * When the efd item is committed to disk, all we need to do is delete our
 * reference to our partner efi item and then free ourselves. Since we're
 * freeing ourselves we must return -1 to keep the transaction code from further
 * referencing this item.
 */
STATIC xfs_lsn_t
xfs_efd_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);

	/*
	 * Drop the EFI reference regardless of whether the EFD has been
	 * aborted. Once the EFD transaction is constructed, it is the sole
	 * responsibility of the EFD to release the EFI (even if the EFI is
	 * aborted due to log I/O error).
	 */
	xfs_efi_release(efdp->efd_efip);
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

/*
 * Process an extent free intent item that was recovered from
 * the log.  We need to free the extents that it describes.
 */
int
xfs_efi_recover(
	struct xfs_mount	*mp,
	struct xfs_efi_log_item	*efip)
{
	struct xfs_efd_log_item	*efdp;
	struct xfs_trans	*tp;
	int			i;
	int			error = 0;
	xfs_extent_t		*extp;
	xfs_fsblock_t		startblock_fsb;
	struct xfs_owner_info	oinfo;

	ASSERT(!test_bit(XFS_EFI_RECOVERED, &efip->efi_flags));

	/*
	 * First check the validity of the extents described by the
	 * EFI.  If any are bad, then assume that all are bad and
	 * just toss the EFI.
	 */
	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		extp = &efip->efi_format.efi_extents[i];
		startblock_fsb = XFS_BB_TO_FSB(mp,
				   XFS_FSB_TO_DADDR(mp, extp->ext_start));
		if (startblock_fsb == 0 ||
		    extp->ext_len == 0 ||
		    startblock_fsb >= mp->m_sb.sb_dblocks ||
		    extp->ext_len >= mp->m_sb.sb_agblocks) {
			/*
			 * This will pull the EFI from the AIL and
			 * free the memory associated with it.
			 */
			set_bit(XFS_EFI_RECOVERED, &efip->efi_flags);
			xfs_efi_release(efip);
			return -EIO;
		}
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate, 0, 0, 0, &tp);
	if (error)
		return error;
	efdp = xfs_trans_get_efd(tp, efip, efip->efi_format.efi_nextents);

	xfs_rmap_any_owner_update(&oinfo);
	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		extp = &efip->efi_format.efi_extents[i];
		error = xfs_trans_free_extent(tp, efdp, extp->ext_start,
					      extp->ext_len, &oinfo, false);
		if (error)
			goto abort_error;

	}

	set_bit(XFS_EFI_RECOVERED, &efip->efi_flags);
	error = xfs_trans_commit(tp);
	return error;

abort_error:
	xfs_trans_cancel(tp);
	return error;
}
