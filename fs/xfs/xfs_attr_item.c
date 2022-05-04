// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Oracle.  All Rights Reserved.
 * Author: Allison Henderson <allison.henderson@oracle.com>
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_shared.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_item.h"
#include "xfs_trace.h"
#include "xfs_inode.h"
#include "xfs_trans_space.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"

static const struct xfs_item_ops xfs_attri_item_ops;
static const struct xfs_item_ops xfs_attrd_item_ops;

static inline struct xfs_attri_log_item *ATTRI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_attri_log_item, attri_item);
}

STATIC void
xfs_attri_item_free(
	struct xfs_attri_log_item	*attrip)
{
	kmem_free(attrip->attri_item.li_lv_shadow);
	kmem_free(attrip);
}

/*
 * Freeing the attrip requires that we remove it from the AIL if it has already
 * been placed there. However, the ATTRI may not yet have been placed in the
 * AIL when called by xfs_attri_release() from ATTRD processing due to the
 * ordering of committed vs unpin operations in bulk insert operations. Hence
 * the reference count to ensure only the last caller frees the ATTRI.
 */
STATIC void
xfs_attri_release(
	struct xfs_attri_log_item	*attrip)
{
	ASSERT(atomic_read(&attrip->attri_refcount) > 0);
	if (!atomic_dec_and_test(&attrip->attri_refcount))
		return;

	xfs_trans_ail_delete(&attrip->attri_item, 0);
	xfs_attri_item_free(attrip);
}

STATIC void
xfs_attri_item_size(
	struct xfs_log_item		*lip,
	int				*nvecs,
	int				*nbytes)
{
	struct xfs_attri_log_item       *attrip = ATTRI_ITEM(lip);

	*nvecs += 2;
	*nbytes += sizeof(struct xfs_attri_log_format) +
			xlog_calc_iovec_len(attrip->attri_name_len);

	if (!attrip->attri_value_len)
		return;

	*nvecs += 1;
	*nbytes += xlog_calc_iovec_len(attrip->attri_value_len);
}

/*
 * This is called to fill in the log iovecs for the given attri log
 * item. We use  1 iovec for the attri_format_item, 1 for the name, and
 * another for the value if it is present
 */
STATIC void
xfs_attri_item_format(
	struct xfs_log_item		*lip,
	struct xfs_log_vec		*lv)
{
	struct xfs_attri_log_item	*attrip = ATTRI_ITEM(lip);
	struct xfs_log_iovec		*vecp = NULL;

	attrip->attri_format.alfi_type = XFS_LI_ATTRI;
	attrip->attri_format.alfi_size = 1;

	/*
	 * This size accounting must be done before copying the attrip into the
	 * iovec.  If we do it after, the wrong size will be recorded to the log
	 * and we trip across assertion checks for bad region sizes later during
	 * the log recovery.
	 */

	ASSERT(attrip->attri_name_len > 0);
	attrip->attri_format.alfi_size++;

	if (attrip->attri_value_len > 0)
		attrip->attri_format.alfi_size++;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTRI_FORMAT,
			&attrip->attri_format,
			sizeof(struct xfs_attri_log_format));
	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTR_NAME,
			attrip->attri_name,
			xlog_calc_iovec_len(attrip->attri_name_len));
	if (attrip->attri_value_len > 0)
		xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTR_VALUE,
				attrip->attri_value,
				xlog_calc_iovec_len(attrip->attri_value_len));
}

/*
 * The unpin operation is the last place an ATTRI is manipulated in the log. It
 * is either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the ATTRI transaction has been successfully committed to make
 * it this far. Therefore, we expect whoever committed the ATTRI to either
 * construct and commit the ATTRD or drop the ATTRD's reference in the event of
 * error. Simply drop the log's ATTRI reference now that the log is done with
 * it.
 */
STATIC void
xfs_attri_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	xfs_attri_release(ATTRI_ITEM(lip));
}


STATIC void
xfs_attri_item_release(
	struct xfs_log_item	*lip)
{
	xfs_attri_release(ATTRI_ITEM(lip));
}

/*
 * Allocate and initialize an attri item.  Caller may allocate an additional
 * trailing buffer for name and value
 */
STATIC struct xfs_attri_log_item *
xfs_attri_init(
	struct xfs_mount		*mp,
	uint32_t			name_len,
	uint32_t			value_len)

{
	struct xfs_attri_log_item	*attrip;
	uint32_t			name_vec_len = 0;
	uint32_t			value_vec_len = 0;
	uint32_t			buffer_size;

	if (name_len)
		name_vec_len = xlog_calc_iovec_len(name_len);
	if (value_len)
		value_vec_len = xlog_calc_iovec_len(value_len);

	buffer_size = name_vec_len + value_vec_len;

	if (buffer_size) {
		attrip = kmem_zalloc(sizeof(struct xfs_attri_log_item) +
				    buffer_size, KM_NOFS);
		if (attrip == NULL)
			return NULL;
	} else {
		attrip = kmem_cache_zalloc(xfs_attri_cache,
					  GFP_NOFS | __GFP_NOFAIL);
	}

	attrip->attri_name_len = name_len;
	if (name_len)
		attrip->attri_name = ((char *)attrip) +
				sizeof(struct xfs_attri_log_item);
	else
		attrip->attri_name = NULL;

	attrip->attri_value_len = value_len;
	if (value_len)
		attrip->attri_value = ((char *)attrip) +
				sizeof(struct xfs_attri_log_item) +
				name_vec_len;
	else
		attrip->attri_value = NULL;

	xfs_log_item_init(mp, &attrip->attri_item, XFS_LI_ATTRI,
			  &xfs_attri_item_ops);
	attrip->attri_format.alfi_id = (uintptr_t)(void *)attrip;
	atomic_set(&attrip->attri_refcount, 2);

	return attrip;
}

/*
 * Copy an attr format buffer from the given buf, and into the destination attr
 * format structure.
 */
STATIC int
xfs_attri_copy_format(
	struct xfs_log_iovec		*buf,
	struct xfs_attri_log_format	*dst_attr_fmt)
{
	struct xfs_attri_log_format	*src_attr_fmt = buf->i_addr;
	size_t				len;

	len = sizeof(struct xfs_attri_log_format);
	if (buf->i_len != len) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, NULL);
		return -EFSCORRUPTED;
	}

	memcpy((char *)dst_attr_fmt, (char *)src_attr_fmt, len);
	return 0;
}

static inline struct xfs_attrd_log_item *ATTRD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_attrd_log_item, attrd_item);
}

STATIC void
xfs_attrd_item_free(struct xfs_attrd_log_item *attrdp)
{
	kmem_free(attrdp->attrd_item.li_lv_shadow);
	kmem_free(attrdp);
}

STATIC void
xfs_attrd_item_size(
	struct xfs_log_item		*lip,
	int				*nvecs,
	int				*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_attrd_log_format);
}

/*
 * This is called to fill in the log iovecs for the given attrd log item. We use
 * only 1 iovec for the attrd_format, and we point that at the attr_log_format
 * structure embedded in the attrd item.
 */
STATIC void
xfs_attrd_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_attrd_log_item	*attrdp = ATTRD_ITEM(lip);
	struct xfs_log_iovec		*vecp = NULL;

	attrdp->attrd_format.alfd_type = XFS_LI_ATTRD;
	attrdp->attrd_format.alfd_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTRD_FORMAT,
			&attrdp->attrd_format,
			sizeof(struct xfs_attrd_log_format));
}

/*
 * The ATTRD is either committed or aborted if the transaction is canceled. If
 * the transaction is canceled, drop our reference to the ATTRI and free the
 * ATTRD.
 */
STATIC void
xfs_attrd_item_release(
	struct xfs_log_item		*lip)
{
	struct xfs_attrd_log_item	*attrdp = ATTRD_ITEM(lip);

	xfs_attri_release(attrdp->attrd_attrip);
	xfs_attrd_item_free(attrdp);
}

STATIC xfs_lsn_t
xfs_attri_item_committed(
	struct xfs_log_item		*lip,
	xfs_lsn_t			lsn)
{
	struct xfs_attri_log_item	*attrip = ATTRI_ITEM(lip);

	/*
	 * The attrip refers to xfs_attr_item memory to log the name and value
	 * with the intent item. This already occurred when the intent was
	 * committed so these fields are no longer accessed. Clear them out of
	 * caution since we're about to free the xfs_attr_item.
	 */
	attrip->attri_name = NULL;
	attrip->attri_value = NULL;

	/*
	 * The ATTRI is logged only once and cannot be moved in the log, so
	 * simply return the lsn at which it's been logged.
	 */
	return lsn;
}

STATIC bool
xfs_attri_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return ATTRI_ITEM(lip)->attri_format.alfi_id == intent_id;
}

/* Is this recovered ATTRI format ok? */
static inline bool
xfs_attri_validate(
	struct xfs_mount		*mp,
	struct xfs_attri_log_format	*attrp)
{
	unsigned int			op = attrp->alfi_op_flags &
					     XFS_ATTR_OP_FLAGS_TYPE_MASK;

	if (attrp->__pad != 0)
		return false;

	/* alfi_op_flags should be either a set or remove */
	if (op != XFS_ATTR_OP_FLAGS_SET && op != XFS_ATTR_OP_FLAGS_REMOVE)
		return false;

	if (attrp->alfi_value_len > XATTR_SIZE_MAX)
		return false;

	if ((attrp->alfi_name_len > XATTR_NAME_MAX) ||
	    (attrp->alfi_name_len == 0))
		return false;

	return xfs_verify_ino(mp, attrp->alfi_ino);
}

STATIC int
xlog_recover_attri_commit_pass2(
	struct xlog                     *log,
	struct list_head		*buffer_list,
	struct xlog_recover_item        *item,
	xfs_lsn_t                       lsn)
{
	int                             error;
	struct xfs_mount                *mp = log->l_mp;
	struct xfs_attri_log_item       *attrip;
	struct xfs_attri_log_format     *attri_formatp;
	int				region = 0;

	attri_formatp = item->ri_buf[region].i_addr;

	/* Validate xfs_attri_log_format */
	if (!xfs_attri_validate(mp, attri_formatp)) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
		return -EFSCORRUPTED;
	}

	/* memory alloc failure will cause replay to abort */
	attrip = xfs_attri_init(mp, attri_formatp->alfi_name_len,
				attri_formatp->alfi_value_len);
	if (attrip == NULL)
		return -ENOMEM;

	error = xfs_attri_copy_format(&item->ri_buf[region],
				      &attrip->attri_format);
	if (error)
		goto out;

	region++;
	memcpy(attrip->attri_name, item->ri_buf[region].i_addr,
	       attrip->attri_name_len);

	if (!xfs_attr_namecheck(attrip->attri_name, attrip->attri_name_len)) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
		error = -EFSCORRUPTED;
		goto out;
	}

	if (attrip->attri_value_len > 0) {
		region++;
		memcpy(attrip->attri_value, item->ri_buf[region].i_addr,
		       attrip->attri_value_len);
	}

	/*
	 * The ATTRI has two references. One for the ATTRD and one for ATTRI to
	 * ensure it makes it into the AIL. Insert the ATTRI into the AIL
	 * directly and drop the ATTRI reference. Note that
	 * xfs_trans_ail_update() drops the AIL lock.
	 */
	xfs_trans_ail_insert(log->l_ailp, &attrip->attri_item, lsn);
	xfs_attri_release(attrip);
	return 0;
out:
	xfs_attri_item_free(attrip);
	return error;
}

/*
 * This routine is called when an ATTRD format structure is found in a committed
 * transaction in the log. Its purpose is to cancel the corresponding ATTRI if
 * it was still in the log. To do this it searches the AIL for the ATTRI with
 * an id equal to that in the ATTRD format structure. If we find it we drop
 * the ATTRD reference, which removes the ATTRI from the AIL and frees it.
 */
STATIC int
xlog_recover_attrd_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_attrd_log_format	*attrd_formatp;

	attrd_formatp = item->ri_buf[0].i_addr;
	if (item->ri_buf[0].i_len != sizeof(struct xfs_attrd_log_format)) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, NULL);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_ATTRI,
				    attrd_formatp->alfd_alf_id);
	return 0;
}

static const struct xfs_item_ops xfs_attri_item_ops = {
	.flags		= XFS_ITEM_INTENT,
	.iop_size	= xfs_attri_item_size,
	.iop_format	= xfs_attri_item_format,
	.iop_unpin	= xfs_attri_item_unpin,
	.iop_committed	= xfs_attri_item_committed,
	.iop_release    = xfs_attri_item_release,
	.iop_match	= xfs_attri_item_match,
};

const struct xlog_recover_item_ops xlog_attri_item_ops = {
	.item_type	= XFS_LI_ATTRI,
	.commit_pass2	= xlog_recover_attri_commit_pass2,
};

static const struct xfs_item_ops xfs_attrd_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED |
			  XFS_ITEM_INTENT_DONE,
	.iop_size	= xfs_attrd_item_size,
	.iop_format	= xfs_attrd_item_format,
	.iop_release    = xfs_attrd_item_release,
};

const struct xlog_recover_item_ops xlog_attrd_item_ops = {
	.item_type	= XFS_LI_ATTRD,
	.commit_pass2	= xlog_recover_attrd_commit_pass2,
};
