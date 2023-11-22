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
#include "xfs_bmap_btree.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_item.h"
#include "xfs_trace.h"
#include "xfs_trans_space.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"

struct kmem_cache		*xfs_attri_cache;
struct kmem_cache		*xfs_attrd_cache;

static const struct xfs_item_ops xfs_attri_item_ops;
static const struct xfs_item_ops xfs_attrd_item_ops;
static struct xfs_attrd_log_item *xfs_trans_get_attrd(struct xfs_trans *tp,
					struct xfs_attri_log_item *attrip);

static inline struct xfs_attri_log_item *ATTRI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_attri_log_item, attri_item);
}

/*
 * Shared xattr name/value buffers for logged extended attribute operations
 *
 * When logging updates to extended attributes, we can create quite a few
 * attribute log intent items for a single xattr update.  To avoid cycling the
 * memory allocator and memcpy overhead, the name (and value, for setxattr)
 * are kept in a refcounted object that is shared across all related log items
 * and the upper-level deferred work state structure.  The shared buffer has
 * a control structure, followed by the name, and then the value.
 */

static inline struct xfs_attri_log_nameval *
xfs_attri_log_nameval_get(
	struct xfs_attri_log_nameval	*nv)
{
	if (!refcount_inc_not_zero(&nv->refcount))
		return NULL;
	return nv;
}

static inline void
xfs_attri_log_nameval_put(
	struct xfs_attri_log_nameval	*nv)
{
	if (!nv)
		return;
	if (refcount_dec_and_test(&nv->refcount))
		kvfree(nv);
}

static inline struct xfs_attri_log_nameval *
xfs_attri_log_nameval_alloc(
	const void			*name,
	unsigned int			name_len,
	const void			*value,
	unsigned int			value_len)
{
	struct xfs_attri_log_nameval	*nv;

	/*
	 * This could be over 64kB in length, so we have to use kvmalloc() for
	 * this. But kvmalloc() utterly sucks, so we use our own version.
	 */
	nv = xlog_kvmalloc(sizeof(struct xfs_attri_log_nameval) +
					name_len + value_len);

	nv->name.i_addr = nv + 1;
	nv->name.i_len = name_len;
	nv->name.i_type = XLOG_REG_TYPE_ATTR_NAME;
	memcpy(nv->name.i_addr, name, name_len);

	if (value_len) {
		nv->value.i_addr = nv->name.i_addr + name_len;
		nv->value.i_len = value_len;
		memcpy(nv->value.i_addr, value, value_len);
	} else {
		nv->value.i_addr = NULL;
		nv->value.i_len = 0;
	}
	nv->value.i_type = XLOG_REG_TYPE_ATTR_VALUE;

	refcount_set(&nv->refcount, 1);
	return nv;
}

STATIC void
xfs_attri_item_free(
	struct xfs_attri_log_item	*attrip)
{
	kmem_free(attrip->attri_item.li_lv_shadow);
	xfs_attri_log_nameval_put(attrip->attri_nameval);
	kmem_cache_free(xfs_attri_cache, attrip);
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
	struct xfs_attri_log_nameval	*nv = attrip->attri_nameval;

	*nvecs += 2;
	*nbytes += sizeof(struct xfs_attri_log_format) +
			xlog_calc_iovec_len(nv->name.i_len);

	if (!nv->value.i_len)
		return;

	*nvecs += 1;
	*nbytes += xlog_calc_iovec_len(nv->value.i_len);
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
	struct xfs_attri_log_nameval	*nv = attrip->attri_nameval;

	attrip->attri_format.alfi_type = XFS_LI_ATTRI;
	attrip->attri_format.alfi_size = 1;

	/*
	 * This size accounting must be done before copying the attrip into the
	 * iovec.  If we do it after, the wrong size will be recorded to the log
	 * and we trip across assertion checks for bad region sizes later during
	 * the log recovery.
	 */

	ASSERT(nv->name.i_len > 0);
	attrip->attri_format.alfi_size++;

	if (nv->value.i_len > 0)
		attrip->attri_format.alfi_size++;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTRI_FORMAT,
			&attrip->attri_format,
			sizeof(struct xfs_attri_log_format));
	xlog_copy_from_iovec(lv, &vecp, &nv->name);
	if (nv->value.i_len > 0)
		xlog_copy_from_iovec(lv, &vecp, &nv->value);
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
	struct xfs_attri_log_nameval	*nv)
{
	struct xfs_attri_log_item	*attrip;

	attrip = kmem_cache_zalloc(xfs_attri_cache, GFP_NOFS | __GFP_NOFAIL);

	/*
	 * Grab an extra reference to the name/value buffer for this log item.
	 * The caller retains its own reference!
	 */
	attrip->attri_nameval = xfs_attri_log_nameval_get(nv);
	ASSERT(attrip->attri_nameval);

	xfs_log_item_init(mp, &attrip->attri_item, XFS_LI_ATTRI,
			  &xfs_attri_item_ops);
	attrip->attri_format.alfi_id = (uintptr_t)(void *)attrip;
	atomic_set(&attrip->attri_refcount, 2);

	return attrip;
}

static inline struct xfs_attrd_log_item *ATTRD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_attrd_log_item, attrd_item);
}

STATIC void
xfs_attrd_item_free(struct xfs_attrd_log_item *attrdp)
{
	kmem_free(attrdp->attrd_item.li_lv_shadow);
	kmem_cache_free(xfs_attrd_cache, attrdp);
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

static struct xfs_log_item *
xfs_attrd_item_intent(
	struct xfs_log_item	*lip)
{
	return &ATTRD_ITEM(lip)->attrd_attrip->attri_item;
}

/*
 * Performs one step of an attribute update intent and marks the attrd item
 * dirty..  An attr operation may be a set or a remove.  Note that the
 * transaction is marked dirty regardless of whether the operation succeeds or
 * fails to support the ATTRI/ATTRD lifecycle rules.
 */
STATIC int
xfs_xattri_finish_update(
	struct xfs_attr_intent		*attr,
	struct xfs_attrd_log_item	*attrdp)
{
	struct xfs_da_args		*args = attr->xattri_da_args;
	int				error;

	if (XFS_TEST_ERROR(false, args->dp->i_mount, XFS_ERRTAG_LARP)) {
		error = -EIO;
		goto out;
	}

	/* If an attr removal is trivially complete, we're done. */
	if (attr->xattri_op_flags == XFS_ATTRI_OP_FLAGS_REMOVE &&
	    !xfs_inode_hasattr(args->dp)) {
		error = 0;
		goto out;
	}

	error = xfs_attr_set_iter(attr);
	if (!error && attr->xattri_dela_state != XFS_DAS_DONE)
		error = -EAGAIN;
out:
	/*
	 * Mark the transaction dirty, even on error. This ensures the
	 * transaction is aborted, which:
	 *
	 * 1.) releases the ATTRI and frees the ATTRD
	 * 2.) shuts down the filesystem
	 */
	args->trans->t_flags |= XFS_TRANS_DIRTY | XFS_TRANS_HAS_INTENT_DONE;

	/*
	 * attr intent/done items are null when logged attributes are disabled
	 */
	if (attrdp)
		set_bit(XFS_LI_DIRTY, &attrdp->attrd_item.li_flags);

	return error;
}

/* Log an attr to the intent item. */
STATIC void
xfs_attr_log_item(
	struct xfs_trans		*tp,
	struct xfs_attri_log_item	*attrip,
	const struct xfs_attr_intent	*attr)
{
	struct xfs_attri_log_format	*attrp;

	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &attrip->attri_item.li_flags);

	/*
	 * At this point the xfs_attr_intent has been constructed, and we've
	 * created the log intent. Fill in the attri log item and log format
	 * structure with fields from this xfs_attr_intent
	 */
	attrp = &attrip->attri_format;
	attrp->alfi_ino = attr->xattri_da_args->dp->i_ino;
	ASSERT(!(attr->xattri_op_flags & ~XFS_ATTRI_OP_FLAGS_TYPE_MASK));
	attrp->alfi_op_flags = attr->xattri_op_flags;
	attrp->alfi_value_len = attr->xattri_nameval->value.i_len;
	attrp->alfi_name_len = attr->xattri_nameval->name.i_len;
	ASSERT(!(attr->xattri_da_args->attr_filter & ~XFS_ATTRI_FILTER_MASK));
	attrp->alfi_attr_filter = attr->xattri_da_args->attr_filter;
}

/* Get an ATTRI. */
static struct xfs_log_item *
xfs_attr_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_attri_log_item	*attrip;
	struct xfs_attr_intent		*attr;
	struct xfs_da_args		*args;

	ASSERT(count == 1);

	/*
	 * Each attr item only performs one attribute operation at a time, so
	 * this is a list of one
	 */
	attr = list_first_entry_or_null(items, struct xfs_attr_intent,
			xattri_list);
	args = attr->xattri_da_args;

	if (!(args->op_flags & XFS_DA_OP_LOGGED))
		return NULL;

	/*
	 * Create a buffer to store the attribute name and value.  This buffer
	 * will be shared between the higher level deferred xattr work state
	 * and the lower level xattr log items.
	 */
	if (!attr->xattri_nameval) {
		/*
		 * Transfer our reference to the name/value buffer to the
		 * deferred work state structure.
		 */
		attr->xattri_nameval = xfs_attri_log_nameval_alloc(args->name,
				args->namelen, args->value, args->valuelen);
	}

	attrip = xfs_attri_init(mp, attr->xattri_nameval);
	xfs_trans_add_item(tp, &attrip->attri_item);
	xfs_attr_log_item(tp, attrip, attr);

	return &attrip->attri_item;
}

static inline void
xfs_attr_free_item(
	struct xfs_attr_intent		*attr)
{
	if (attr->xattri_da_state)
		xfs_da_state_free(attr->xattri_da_state);
	xfs_attri_log_nameval_put(attr->xattri_nameval);
	if (attr->xattri_da_args->op_flags & XFS_DA_OP_RECOVERY)
		kmem_free(attr);
	else
		kmem_cache_free(xfs_attr_intent_cache, attr);
}

/* Process an attr. */
STATIC int
xfs_attr_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_attr_intent		*attr;
	struct xfs_attrd_log_item	*done_item = NULL;
	int				error;

	attr = container_of(item, struct xfs_attr_intent, xattri_list);
	if (done)
		done_item = ATTRD_ITEM(done);

	/*
	 * Always reset trans after EAGAIN cycle
	 * since the transaction is new
	 */
	attr->xattri_da_args->trans = tp;

	error = xfs_xattri_finish_update(attr, done_item);
	if (error != -EAGAIN)
		xfs_attr_free_item(attr);

	return error;
}

/* Abort all pending ATTRs. */
STATIC void
xfs_attr_abort_intent(
	struct xfs_log_item		*intent)
{
	xfs_attri_release(ATTRI_ITEM(intent));
}

/* Cancel an attr */
STATIC void
xfs_attr_cancel_item(
	struct list_head		*item)
{
	struct xfs_attr_intent		*attr;

	attr = container_of(item, struct xfs_attr_intent, xattri_list);
	xfs_attr_free_item(attr);
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
					     XFS_ATTRI_OP_FLAGS_TYPE_MASK;

	if (attrp->__pad != 0)
		return false;

	if (attrp->alfi_op_flags & ~XFS_ATTRI_OP_FLAGS_TYPE_MASK)
		return false;

	if (attrp->alfi_attr_filter & ~XFS_ATTRI_FILTER_MASK)
		return false;

	/* alfi_op_flags should be either a set or remove */
	switch (op) {
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		break;
	default:
		return false;
	}

	if (attrp->alfi_value_len > XATTR_SIZE_MAX)
		return false;

	if ((attrp->alfi_name_len > XATTR_NAME_MAX) ||
	    (attrp->alfi_name_len == 0))
		return false;

	return xfs_verify_ino(mp, attrp->alfi_ino);
}

/*
 * Process an attr intent item that was recovered from the log.  We need to
 * delete the attr that it describes.
 */
STATIC int
xfs_attri_item_recover(
	struct xfs_defer_pending	*dfp,
	struct list_head		*capture_list)
{
	struct xfs_log_item		*lip = dfp->dfp_intent;
	struct xfs_attri_log_item	*attrip = ATTRI_ITEM(lip);
	struct xfs_attr_intent		*attr;
	struct xfs_mount		*mp = lip->li_log->l_mp;
	struct xfs_inode		*ip;
	struct xfs_da_args		*args;
	struct xfs_trans		*tp;
	struct xfs_trans_res		resv;
	struct xfs_attri_log_format	*attrp;
	struct xfs_attri_log_nameval	*nv = attrip->attri_nameval;
	int				error;
	int				total;
	int				local;
	struct xfs_attrd_log_item	*done_item = NULL;

	/*
	 * First check the validity of the attr described by the ATTRI.  If any
	 * are bad, then assume that all are bad and just toss the ATTRI.
	 */
	attrp = &attrip->attri_format;
	if (!xfs_attri_validate(mp, attrp) ||
	    !xfs_attr_namecheck(nv->name.i_addr, nv->name.i_len))
		return -EFSCORRUPTED;

	error = xlog_recover_iget(mp,  attrp->alfi_ino, &ip);
	if (error)
		return error;

	attr = kmem_zalloc(sizeof(struct xfs_attr_intent) +
			   sizeof(struct xfs_da_args), KM_NOFS);
	args = (struct xfs_da_args *)(attr + 1);

	attr->xattri_da_args = args;
	attr->xattri_op_flags = attrp->alfi_op_flags &
						XFS_ATTRI_OP_FLAGS_TYPE_MASK;

	/*
	 * We're reconstructing the deferred work state structure from the
	 * recovered log item.  Grab a reference to the name/value buffer and
	 * attach it to the new work state.
	 */
	attr->xattri_nameval = xfs_attri_log_nameval_get(nv);
	ASSERT(attr->xattri_nameval);

	args->dp = ip;
	args->geo = mp->m_attr_geo;
	args->whichfork = XFS_ATTR_FORK;
	args->name = nv->name.i_addr;
	args->namelen = nv->name.i_len;
	args->hashval = xfs_da_hashname(args->name, args->namelen);
	args->attr_filter = attrp->alfi_attr_filter & XFS_ATTRI_FILTER_MASK;
	args->op_flags = XFS_DA_OP_RECOVERY | XFS_DA_OP_OKNOENT |
			 XFS_DA_OP_LOGGED;

	ASSERT(xfs_sb_version_haslogxattrs(&mp->m_sb));

	switch (attr->xattri_op_flags) {
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		args->value = nv->value.i_addr;
		args->valuelen = nv->value.i_len;
		args->total = xfs_attr_calc_size(args, &local);
		if (xfs_inode_hasattr(args->dp))
			attr->xattri_dela_state = xfs_attr_init_replace_state(args);
		else
			attr->xattri_dela_state = xfs_attr_init_add_state(args);
		break;
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		attr->xattri_dela_state = xfs_attr_init_remove_state(args);
		break;
	default:
		ASSERT(0);
		error = -EFSCORRUPTED;
		goto out;
	}

	xfs_init_attr_trans(args, &resv, &total);
	resv = xlog_recover_resv(&resv);
	error = xfs_trans_alloc(mp, &resv, total, 0, XFS_TRANS_RESERVE, &tp);
	if (error)
		goto out;

	args->trans = tp;
	done_item = xfs_trans_get_attrd(tp, attrip);
	xlog_recover_transfer_intent(tp, dfp);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	error = xfs_xattri_finish_update(attr, done_item);
	if (error == -EAGAIN) {
		/*
		 * There's more work to do, so add the intent item to this
		 * transaction so that we can continue it later.
		 */
		xfs_defer_add(tp, XFS_DEFER_OPS_TYPE_ATTR, &attr->xattri_list);
		error = xfs_defer_ops_capture_and_commit(tp, capture_list);
		if (error)
			goto out_unlock;

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_irele(ip);
		return 0;
	}
	if (error) {
		xfs_trans_cancel(tp);
		goto out_unlock;
	}

	error = xfs_defer_ops_capture_and_commit(tp, capture_list);
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_irele(ip);
out:
	xfs_attr_free_item(attr);
	return error;
}

/* Re-log an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_attri_item_relog(
	struct xfs_log_item		*intent,
	struct xfs_trans		*tp)
{
	struct xfs_attrd_log_item	*attrdp;
	struct xfs_attri_log_item	*old_attrip;
	struct xfs_attri_log_item	*new_attrip;
	struct xfs_attri_log_format	*new_attrp;
	struct xfs_attri_log_format	*old_attrp;

	old_attrip = ATTRI_ITEM(intent);
	old_attrp = &old_attrip->attri_format;

	tp->t_flags |= XFS_TRANS_DIRTY;
	attrdp = xfs_trans_get_attrd(tp, old_attrip);
	set_bit(XFS_LI_DIRTY, &attrdp->attrd_item.li_flags);

	/*
	 * Create a new log item that shares the same name/value buffer as the
	 * old log item.
	 */
	new_attrip = xfs_attri_init(tp->t_mountp, old_attrip->attri_nameval);
	new_attrp = &new_attrip->attri_format;

	new_attrp->alfi_ino = old_attrp->alfi_ino;
	new_attrp->alfi_op_flags = old_attrp->alfi_op_flags;
	new_attrp->alfi_value_len = old_attrp->alfi_value_len;
	new_attrp->alfi_name_len = old_attrp->alfi_name_len;
	new_attrp->alfi_attr_filter = old_attrp->alfi_attr_filter;

	xfs_trans_add_item(tp, &new_attrip->attri_item);
	set_bit(XFS_LI_DIRTY, &new_attrip->attri_item.li_flags);

	return &new_attrip->attri_item;
}

STATIC int
xlog_recover_attri_commit_pass2(
	struct xlog                     *log,
	struct list_head		*buffer_list,
	struct xlog_recover_item        *item,
	xfs_lsn_t                       lsn)
{
	struct xfs_mount                *mp = log->l_mp;
	struct xfs_attri_log_item       *attrip;
	struct xfs_attri_log_format     *attri_formatp;
	struct xfs_attri_log_nameval	*nv;
	const void			*attr_value = NULL;
	const void			*attr_name;
	size_t				len;

	attri_formatp = item->ri_buf[0].i_addr;
	attr_name = item->ri_buf[1].i_addr;

	/* Validate xfs_attri_log_format before the large memory allocation */
	len = sizeof(struct xfs_attri_log_format);
	if (item->ri_buf[0].i_len != len) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	if (!xfs_attri_validate(mp, attri_formatp)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	/* Validate the attr name */
	if (item->ri_buf[1].i_len !=
			xlog_calc_iovec_len(attri_formatp->alfi_name_len)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	if (!xfs_attr_namecheck(attr_name, attri_formatp->alfi_name_len)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[1].i_addr, item->ri_buf[1].i_len);
		return -EFSCORRUPTED;
	}

	/* Validate the attr value, if present */
	if (attri_formatp->alfi_value_len != 0) {
		if (item->ri_buf[2].i_len != xlog_calc_iovec_len(attri_formatp->alfi_value_len)) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					item->ri_buf[0].i_addr,
					item->ri_buf[0].i_len);
			return -EFSCORRUPTED;
		}

		attr_value = item->ri_buf[2].i_addr;
	}

	/*
	 * Memory alloc failure will cause replay to abort.  We attach the
	 * name/value buffer to the recovered incore log item and drop our
	 * reference.
	 */
	nv = xfs_attri_log_nameval_alloc(attr_name,
			attri_formatp->alfi_name_len, attr_value,
			attri_formatp->alfi_value_len);

	attrip = xfs_attri_init(mp, nv);
	memcpy(&attrip->attri_format, attri_formatp, len);

	xlog_recover_intent_item(log, &attrip->attri_item, lsn,
			XFS_DEFER_OPS_TYPE_ATTR);
	xfs_attri_log_nameval_put(nv);
	return 0;
}

/*
 * This routine is called to allocate an "attr free done" log item.
 */
static struct xfs_attrd_log_item *
xfs_trans_get_attrd(struct xfs_trans		*tp,
		  struct xfs_attri_log_item	*attrip)
{
	struct xfs_attrd_log_item		*attrdp;

	ASSERT(tp != NULL);

	attrdp = kmem_cache_zalloc(xfs_attrd_cache, GFP_NOFS | __GFP_NOFAIL);

	xfs_log_item_init(tp->t_mountp, &attrdp->attrd_item, XFS_LI_ATTRD,
			  &xfs_attrd_item_ops);
	attrdp->attrd_attrip = attrip;
	attrdp->attrd_format.alfd_alf_id = attrip->attri_format.alfi_id;

	xfs_trans_add_item(tp, &attrdp->attrd_item);
	return attrdp;
}

/* Get an ATTRD so we can process all the attrs. */
static struct xfs_log_item *
xfs_attr_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	if (!intent)
		return NULL;

	return &xfs_trans_get_attrd(tp, ATTRI_ITEM(intent))->attrd_item;
}

const struct xfs_defer_op_type xfs_attr_defer_type = {
	.max_items	= 1,
	.create_intent	= xfs_attr_create_intent,
	.abort_intent	= xfs_attr_abort_intent,
	.create_done	= xfs_attr_create_done,
	.finish_item	= xfs_attr_finish_item,
	.cancel_item	= xfs_attr_cancel_item,
};

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
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
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
	.iop_release    = xfs_attri_item_release,
	.iop_recover	= xfs_attri_item_recover,
	.iop_match	= xfs_attri_item_match,
	.iop_relog	= xfs_attri_item_relog,
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
	.iop_intent	= xfs_attrd_item_intent,
};

const struct xlog_recover_item_ops xlog_attrd_item_ops = {
	.item_type	= XFS_LI_ATTRD,
	.commit_pass2	= xlog_recover_attrd_commit_pass2,
};
