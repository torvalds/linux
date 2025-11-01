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
#include "xfs_parent.h"

struct kmem_cache		*xfs_attri_cache;
struct kmem_cache		*xfs_attrd_cache;

static const struct xfs_item_ops xfs_attri_item_ops;
static const struct xfs_item_ops xfs_attrd_item_ops;

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
	const void			*new_name,
	unsigned int			new_name_len,
	const void			*value,
	unsigned int			value_len,
	const void			*new_value,
	unsigned int			new_value_len)
{
	struct xfs_attri_log_nameval	*nv;

	/*
	 * This could be over 64kB in length, so we have to use kvmalloc() for
	 * this. But kvmalloc() utterly sucks, so we use our own version.
	 */
	nv = xlog_kvmalloc(sizeof(struct xfs_attri_log_nameval) +
					name_len + new_name_len + value_len +
					new_value_len);

	nv->name.iov_base = nv + 1;
	nv->name.iov_len = name_len;
	memcpy(nv->name.iov_base, name, name_len);

	if (new_name_len) {
		nv->new_name.iov_base = nv->name.iov_base + name_len;
		nv->new_name.iov_len = new_name_len;
		memcpy(nv->new_name.iov_base, new_name, new_name_len);
	} else {
		nv->new_name.iov_base = NULL;
		nv->new_name.iov_len = 0;
	}

	if (value_len) {
		nv->value.iov_base = nv->name.iov_base + name_len + new_name_len;
		nv->value.iov_len = value_len;
		memcpy(nv->value.iov_base, value, value_len);
	} else {
		nv->value.iov_base = NULL;
		nv->value.iov_len = 0;
	}

	if (new_value_len) {
		nv->new_value.iov_base = nv->name.iov_base + name_len +
						new_name_len + value_len;
		nv->new_value.iov_len = new_value_len;
		memcpy(nv->new_value.iov_base, new_value, new_value_len);
	} else {
		nv->new_value.iov_base = NULL;
		nv->new_value.iov_len = 0;
	}

	refcount_set(&nv->refcount, 1);
	return nv;
}

STATIC void
xfs_attri_item_free(
	struct xfs_attri_log_item	*attrip)
{
	kvfree(attrip->attri_item.li_lv_shadow);
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
			xlog_calc_iovec_len(nv->name.iov_len);

	if (nv->new_name.iov_len) {
		*nvecs += 1;
		*nbytes += xlog_calc_iovec_len(nv->new_name.iov_len);
	}

	if (nv->value.iov_len) {
		*nvecs += 1;
		*nbytes += xlog_calc_iovec_len(nv->value.iov_len);
	}

	if (nv->new_value.iov_len) {
		*nvecs += 1;
		*nbytes += xlog_calc_iovec_len(nv->new_value.iov_len);
	}
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

	ASSERT(nv->name.iov_len > 0);
	attrip->attri_format.alfi_size++;

	if (nv->new_name.iov_len > 0)
		attrip->attri_format.alfi_size++;

	if (nv->value.iov_len > 0)
		attrip->attri_format.alfi_size++;

	if (nv->new_value.iov_len > 0)
		attrip->attri_format.alfi_size++;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTRI_FORMAT,
			&attrip->attri_format,
			sizeof(struct xfs_attri_log_format));

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTR_NAME, nv->name.iov_base,
			nv->name.iov_len);

	if (nv->new_name.iov_len > 0)
		xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTR_NEWNAME,
			nv->new_name.iov_base, nv->new_name.iov_len);

	if (nv->value.iov_len > 0)
		xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTR_VALUE,
			nv->value.iov_base, nv->value.iov_len);

	if (nv->new_value.iov_len > 0)
		xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_ATTR_NEWVALUE,
			nv->new_value.iov_base, nv->new_value.iov_len);
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

	attrip = kmem_cache_zalloc(xfs_attri_cache, GFP_KERNEL | __GFP_NOFAIL);

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
	kvfree(attrdp->attrd_item.li_lv_shadow);
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

static inline unsigned int
xfs_attr_log_item_op(const struct xfs_attri_log_format *attrp)
{
	return attrp->alfi_op_flags & XFS_ATTRI_OP_FLAGS_TYPE_MASK;
}

/* Log an attr to the intent item. */
STATIC void
xfs_attr_log_item(
	struct xfs_trans		*tp,
	struct xfs_attri_log_item	*attrip,
	const struct xfs_attr_intent	*attr)
{
	struct xfs_attri_log_format	*attrp;
	struct xfs_attri_log_nameval	*nv = attr->xattri_nameval;
	struct xfs_da_args		*args = attr->xattri_da_args;

	/*
	 * At this point the xfs_attr_intent has been constructed, and we've
	 * created the log intent. Fill in the attri log item and log format
	 * structure with fields from this xfs_attr_intent
	 */
	attrp = &attrip->attri_format;
	attrp->alfi_ino = args->dp->i_ino;
	ASSERT(!(attr->xattri_op_flags & ~XFS_ATTRI_OP_FLAGS_TYPE_MASK));
	attrp->alfi_op_flags = attr->xattri_op_flags;
	attrp->alfi_value_len = nv->value.iov_len;

	switch (xfs_attr_log_item_op(attrp)) {
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
		ASSERT(nv->value.iov_len == nv->new_value.iov_len);

		attrp->alfi_igen = VFS_I(args->dp)->i_generation;
		attrp->alfi_old_name_len = nv->name.iov_len;
		attrp->alfi_new_name_len = nv->new_name.iov_len;
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
		attrp->alfi_igen = VFS_I(args->dp)->i_generation;
		fallthrough;
	default:
		attrp->alfi_name_len = nv->name.iov_len;
		break;
	}

	ASSERT(!(args->attr_filter & ~XFS_ATTRI_FILTER_MASK));
	attrp->alfi_attr_filter = args->attr_filter;
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
		attr->xattri_nameval = xfs_attri_log_nameval_alloc(
				args->name, args->namelen,
				args->new_name, args->new_namelen,
				args->value, args->valuelen,
				args->new_value, args->new_valuelen);
	}

	attrip = xfs_attri_init(mp, attr->xattri_nameval);
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
		kfree(attr);
	else
		kmem_cache_free(xfs_attr_intent_cache, attr);
}

static inline struct xfs_attr_intent *attri_entry(const struct list_head *e)
{
	return list_entry(e, struct xfs_attr_intent, xattri_list);
}

/* Process an attr. */
STATIC int
xfs_attr_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_attr_intent		*attr = attri_entry(item);
	struct xfs_da_args		*args;
	int				error;

	args = attr->xattri_da_args;

	/* Reset trans after EAGAIN cycle since the transaction is new */
	args->trans = tp;

	if (XFS_TEST_ERROR(args->dp->i_mount, XFS_ERRTAG_LARP)) {
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
		return -EAGAIN;

out:
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
	struct xfs_attr_intent		*attr = attri_entry(item);

	xfs_attr_free_item(attr);
}

STATIC bool
xfs_attri_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return ATTRI_ITEM(lip)->attri_format.alfi_id == intent_id;
}

static inline bool
xfs_attri_validate_namelen(unsigned int namelen)
{
	return namelen > 0 && namelen <= XATTR_NAME_MAX;
}

/* Is this recovered ATTRI format ok? */
static inline bool
xfs_attri_validate(
	struct xfs_mount		*mp,
	struct xfs_attri_log_format	*attrp)
{
	unsigned int			op = xfs_attr_log_item_op(attrp);

	if (attrp->alfi_op_flags & ~XFS_ATTRI_OP_FLAGS_TYPE_MASK)
		return false;

	if (attrp->alfi_attr_filter & ~XFS_ATTRI_FILTER_MASK)
		return false;

	if (!xfs_attr_check_namespace(attrp->alfi_attr_filter &
				      XFS_ATTR_NSP_ONDISK_MASK))
		return false;

	switch (op) {
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
		if (!xfs_has_parent(mp))
			return false;
		if (attrp->alfi_value_len != sizeof(struct xfs_parent_rec))
			return false;
		if (!xfs_attri_validate_namelen(attrp->alfi_name_len))
			return false;
		if (!(attrp->alfi_attr_filter & XFS_ATTR_PARENT))
			return false;
		break;
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		if (!xfs_is_using_logged_xattrs(mp))
			return false;
		if (attrp->alfi_value_len > XATTR_SIZE_MAX)
			return false;
		if (!xfs_attri_validate_namelen(attrp->alfi_name_len))
			return false;
		break;
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		if (!xfs_is_using_logged_xattrs(mp))
			return false;
		if (attrp->alfi_value_len != 0)
			return false;
		if (!xfs_attri_validate_namelen(attrp->alfi_name_len))
			return false;
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
		if (!xfs_has_parent(mp))
			return false;
		if (!xfs_attri_validate_namelen(attrp->alfi_old_name_len))
			return false;
		if (!xfs_attri_validate_namelen(attrp->alfi_new_name_len))
			return false;
		if (attrp->alfi_value_len != sizeof(struct xfs_parent_rec))
			return false;
		if (!(attrp->alfi_attr_filter & XFS_ATTR_PARENT))
			return false;
		break;
	default:
		return false;
	}

	return xfs_verify_ino(mp, attrp->alfi_ino);
}

static int
xfs_attri_iread_extents(
	struct xfs_inode		*ip)
{
	struct xfs_trans		*tp;
	int				error;

	tp = xfs_trans_alloc_empty(ip->i_mount);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	error = xfs_iread_extents(tp, ip, XFS_ATTR_FORK);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_trans_cancel(tp);

	return error;
}

static inline struct xfs_attr_intent *
xfs_attri_recover_work(
	struct xfs_mount		*mp,
	struct xfs_defer_pending	*dfp,
	struct xfs_attri_log_format	*attrp,
	struct xfs_inode		**ipp,
	struct xfs_attri_log_nameval	*nv)
{
	struct xfs_attr_intent		*attr;
	struct xfs_da_args		*args;
	struct xfs_inode		*ip;
	int				local;
	int				error;

	/*
	 * Parent pointer attr items record the generation but regular logged
	 * xattrs do not; select the right iget function.
	 */
	switch (xfs_attr_log_item_op(attrp)) {
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
		error = xlog_recover_iget_handle(mp, attrp->alfi_ino,
				attrp->alfi_igen, &ip);
		break;
	default:
		error = xlog_recover_iget(mp, attrp->alfi_ino, &ip);
		break;
	}
	if (error) {
		xfs_irele(ip);
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, attrp,
				sizeof(*attrp));
		return ERR_PTR(-EFSCORRUPTED);
	}

	if (xfs_inode_has_attr_fork(ip)) {
		error = xfs_attri_iread_extents(ip);
		if (error) {
			xfs_irele(ip);
			return ERR_PTR(error);
		}
	}

	attr = kzalloc(sizeof(struct xfs_attr_intent) +
			sizeof(struct xfs_da_args), GFP_KERNEL | __GFP_NOFAIL);
	args = (struct xfs_da_args *)(attr + 1);

	attr->xattri_da_args = args;
	attr->xattri_op_flags = xfs_attr_log_item_op(attrp);

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
	args->name = nv->name.iov_base;
	args->namelen = nv->name.iov_len;
	args->new_name = nv->new_name.iov_base;
	args->new_namelen = nv->new_name.iov_len;
	args->value = nv->value.iov_base;
	args->valuelen = nv->value.iov_len;
	args->new_value = nv->new_value.iov_base;
	args->new_valuelen = nv->new_value.iov_len;
	args->attr_filter = attrp->alfi_attr_filter & XFS_ATTRI_FILTER_MASK;
	args->op_flags = XFS_DA_OP_RECOVERY | XFS_DA_OP_OKNOENT |
			 XFS_DA_OP_LOGGED;
	args->owner = args->dp->i_ino;
	xfs_attr_sethash(args);

	switch (xfs_attr_intent_op(attr)) {
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		args->total = xfs_attr_calc_size(args, &local);
		if (xfs_inode_hasattr(args->dp))
			attr->xattri_dela_state = xfs_attr_init_replace_state(args);
		else
			attr->xattri_dela_state = xfs_attr_init_add_state(args);
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		attr->xattri_dela_state = xfs_attr_init_remove_state(args);
		break;
	}

	xfs_defer_add_item(dfp, &attr->xattri_list);
	*ipp = ip;
	return attr;
}

/*
 * Process an attr intent item that was recovered from the log.  We need to
 * delete the attr that it describes.
 */
STATIC int
xfs_attr_recover_work(
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
	unsigned int			total = 0;

	/*
	 * First check the validity of the attr described by the ATTRI.  If any
	 * are bad, then assume that all are bad and just toss the ATTRI.
	 */
	attrp = &attrip->attri_format;
	if (!xfs_attri_validate(mp, attrp) ||
	    !xfs_attr_namecheck(attrp->alfi_attr_filter, nv->name.iov_base,
				nv->name.iov_len))
		return -EFSCORRUPTED;

	attr = xfs_attri_recover_work(mp, dfp, attrp, &ip, nv);
	if (IS_ERR(attr))
		return PTR_ERR(attr);
	args = attr->xattri_da_args;

	switch (xfs_attr_intent_op(attr)) {
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		resv = xfs_attr_set_resv(args);
		total = args->total;
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		resv = M_RES(mp)->tr_attrrm;
		total = XFS_ATTRRM_SPACE_RES(mp);
		break;
	}
	resv = xlog_recover_resv(&resv);
	error = xfs_trans_alloc(mp, &resv, total, 0, XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;
	args->trans = tp;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	error = xlog_recover_finish_intent(tp, dfp);
	if (error == -EFSCORRUPTED)
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&attrip->attri_format,
				sizeof(attrip->attri_format));
	if (error)
		goto out_cancel;

	error = xfs_defer_ops_capture_and_commit(tp, capture_list);
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_irele(ip);
	return error;
out_cancel:
	xfs_trans_cancel(tp);
	goto out_unlock;
}

/* Re-log an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_attr_relog_intent(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	struct xfs_log_item		*done_item)
{
	struct xfs_attri_log_item	*old_attrip;
	struct xfs_attri_log_item	*new_attrip;
	struct xfs_attri_log_format	*new_attrp;
	struct xfs_attri_log_format	*old_attrp;

	old_attrip = ATTRI_ITEM(intent);
	old_attrp = &old_attrip->attri_format;

	/*
	 * Create a new log item that shares the same name/value buffer as the
	 * old log item.
	 */
	new_attrip = xfs_attri_init(tp->t_mountp, old_attrip->attri_nameval);
	new_attrp = &new_attrip->attri_format;

	new_attrp->alfi_ino = old_attrp->alfi_ino;
	new_attrp->alfi_igen = old_attrp->alfi_igen;
	new_attrp->alfi_op_flags = old_attrp->alfi_op_flags;
	new_attrp->alfi_value_len = old_attrp->alfi_value_len;

	switch (xfs_attr_log_item_op(old_attrp)) {
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
		new_attrp->alfi_new_name_len = old_attrp->alfi_new_name_len;
		new_attrp->alfi_old_name_len = old_attrp->alfi_old_name_len;
		break;
	default:
		new_attrp->alfi_name_len = old_attrp->alfi_name_len;
		break;
	}

	new_attrp->alfi_attr_filter = old_attrp->alfi_attr_filter;

	return &new_attrip->attri_item;
}

/* Get an ATTRD so we can process all the attrs. */
static struct xfs_log_item *
xfs_attr_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	struct xfs_attri_log_item	*attrip;
	struct xfs_attrd_log_item	*attrdp;

	attrip = ATTRI_ITEM(intent);

	attrdp = kmem_cache_zalloc(xfs_attrd_cache, GFP_KERNEL | __GFP_NOFAIL);

	xfs_log_item_init(tp->t_mountp, &attrdp->attrd_item, XFS_LI_ATTRD,
			  &xfs_attrd_item_ops);
	attrdp->attrd_attrip = attrip;
	attrdp->attrd_format.alfd_alf_id = attrip->attri_format.alfi_id;

	return &attrdp->attrd_item;
}

void
xfs_attr_defer_add(
	struct xfs_da_args	*args,
	enum xfs_attr_defer_op	op)
{
	struct xfs_attr_intent	*new;
	unsigned int		log_op = 0;
	bool			is_pptr = args->attr_filter & XFS_ATTR_PARENT;

	if (is_pptr) {
		ASSERT(xfs_has_parent(args->dp->i_mount));
		ASSERT((args->attr_filter & ~XFS_ATTR_PARENT) == 0);
		ASSERT(args->op_flags & XFS_DA_OP_LOGGED);
		ASSERT(args->valuelen == sizeof(struct xfs_parent_rec));
	}

	new = kmem_cache_zalloc(xfs_attr_intent_cache,
			GFP_NOFS | __GFP_NOFAIL);
	new->xattri_da_args = args;

	/* Compute log operation from the higher level op and namespace. */
	switch (op) {
	case XFS_ATTR_DEFER_SET:
		if (is_pptr)
			log_op = XFS_ATTRI_OP_FLAGS_PPTR_SET;
		else
			log_op = XFS_ATTRI_OP_FLAGS_SET;
		break;
	case XFS_ATTR_DEFER_REPLACE:
		if (is_pptr)
			log_op = XFS_ATTRI_OP_FLAGS_PPTR_REPLACE;
		else
			log_op = XFS_ATTRI_OP_FLAGS_REPLACE;
		break;
	case XFS_ATTR_DEFER_REMOVE:
		if (is_pptr)
			log_op = XFS_ATTRI_OP_FLAGS_PPTR_REMOVE;
		else
			log_op = XFS_ATTRI_OP_FLAGS_REMOVE;
		break;
	default:
		ASSERT(0);
		break;
	}
	new->xattri_op_flags = log_op;

	/* Set up initial attr operation state. */
	switch (log_op) {
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
	case XFS_ATTRI_OP_FLAGS_SET:
		new->xattri_dela_state = xfs_attr_init_add_state(args);
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
		ASSERT(args->new_valuelen == args->valuelen);
		new->xattri_dela_state = xfs_attr_init_replace_state(args);
		break;
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		new->xattri_dela_state = xfs_attr_init_replace_state(args);
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		new->xattri_dela_state = xfs_attr_init_remove_state(args);
		break;
	}

	xfs_defer_add(args->trans, &new->xattri_list, &xfs_attr_defer_type);
	trace_xfs_attr_defer_add(new->xattri_dela_state, args->dp);
}

const struct xfs_defer_op_type xfs_attr_defer_type = {
	.name		= "attr",
	.max_items	= 1,
	.create_intent	= xfs_attr_create_intent,
	.abort_intent	= xfs_attr_abort_intent,
	.create_done	= xfs_attr_create_done,
	.finish_item	= xfs_attr_finish_item,
	.cancel_item	= xfs_attr_cancel_item,
	.recover_work	= xfs_attr_recover_work,
	.relog_intent	= xfs_attr_relog_intent,
};

static inline void *
xfs_attri_validate_name_iovec(
	struct xfs_mount		*mp,
	struct xfs_attri_log_format     *attri_formatp,
	const struct kvec		*iovec,
	unsigned int			name_len)
{
	if (iovec->iov_len != xlog_calc_iovec_len(name_len)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				attri_formatp, sizeof(*attri_formatp));
		return NULL;
	}

	if (!xfs_attr_namecheck(attri_formatp->alfi_attr_filter, iovec->iov_base,
				name_len)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				attri_formatp, sizeof(*attri_formatp));
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				iovec->iov_base, iovec->iov_len);
		return NULL;
	}

	return iovec->iov_base;
}

static inline void *
xfs_attri_validate_value_iovec(
	struct xfs_mount		*mp,
	struct xfs_attri_log_format     *attri_formatp,
	const struct kvec		*iovec,
	unsigned int			value_len)
{
	if (iovec->iov_len != xlog_calc_iovec_len(value_len)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				attri_formatp, sizeof(*attri_formatp));
		return NULL;
	}

	if ((attri_formatp->alfi_attr_filter & XFS_ATTR_PARENT) &&
	    !xfs_parent_valuecheck(mp, iovec->iov_base, value_len)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				attri_formatp, sizeof(*attri_formatp));
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				iovec->iov_base, iovec->iov_len);
		return NULL;
	}

	return iovec->iov_base;
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
	const void			*attr_name;
	const void			*attr_value = NULL;
	const void			*attr_new_name = NULL;
	const void			*attr_new_value = NULL;
	size_t				len;
	unsigned int			name_len = 0;
	unsigned int			value_len = 0;
	unsigned int			new_name_len = 0;
	unsigned int			new_value_len = 0;
	unsigned int			op, i = 0;

	/* Validate xfs_attri_log_format before the large memory allocation */
	len = sizeof(struct xfs_attri_log_format);
	if (item->ri_buf[i].iov_len != len) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].iov_base, item->ri_buf[0].iov_len);
		return -EFSCORRUPTED;
	}

	attri_formatp = item->ri_buf[i].iov_base;
	if (!xfs_attri_validate(mp, attri_formatp)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				attri_formatp, len);
		return -EFSCORRUPTED;
	}

	/* Check the number of log iovecs makes sense for the op code. */
	op = xfs_attr_log_item_op(attri_formatp);
	switch (op) {
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
		/* Log item, attr name, attr value */
		if (item->ri_total != 3) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		name_len = attri_formatp->alfi_name_len;
		value_len = attri_formatp->alfi_value_len;
		break;
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		/* Log item, attr name, attr value */
		if (item->ri_total != 3) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		name_len = attri_formatp->alfi_name_len;
		value_len = attri_formatp->alfi_value_len;
		break;
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		/* Log item, attr name */
		if (item->ri_total != 2) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		name_len = attri_formatp->alfi_name_len;
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
		/*
		 * Log item, attr name, new attr name, attr value, new attr
		 * value
		 */
		if (item->ri_total != 5) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		name_len = attri_formatp->alfi_old_name_len;
		new_name_len = attri_formatp->alfi_new_name_len;
		new_value_len = value_len = attri_formatp->alfi_value_len;
		break;
	default:
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				     attri_formatp, len);
		return -EFSCORRUPTED;
	}
	i++;

	/* Validate the attr name */
	attr_name = xfs_attri_validate_name_iovec(mp, attri_formatp,
			&item->ri_buf[i], name_len);
	if (!attr_name)
		return -EFSCORRUPTED;
	i++;

	/* Validate the new attr name */
	if (new_name_len > 0) {
		attr_new_name = xfs_attri_validate_name_iovec(mp,
					attri_formatp, &item->ri_buf[i],
					new_name_len);
		if (!attr_new_name)
			return -EFSCORRUPTED;
		i++;
	}

	/* Validate the attr value, if present */
	if (value_len != 0) {
		attr_value = xfs_attri_validate_value_iovec(mp, attri_formatp,
				&item->ri_buf[i], value_len);
		if (!attr_value)
			return -EFSCORRUPTED;
		i++;
	}

	/* Validate the new attr value, if present */
	if (new_value_len != 0) {
		attr_new_value = xfs_attri_validate_value_iovec(mp,
					attri_formatp, &item->ri_buf[i],
					new_value_len);
		if (!attr_new_value)
			return -EFSCORRUPTED;
		i++;
	}

	/*
	 * Make sure we got the correct number of buffers for the operation
	 * that we just loaded.
	 */
	if (i != item->ri_total) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				attri_formatp, len);
		return -EFSCORRUPTED;
	}

	switch (op) {
	case XFS_ATTRI_OP_FLAGS_REMOVE:
		/* Regular remove operations operate only on names. */
		if (attr_value != NULL || value_len != 0) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		fallthrough;
	case XFS_ATTRI_OP_FLAGS_PPTR_REMOVE:
	case XFS_ATTRI_OP_FLAGS_PPTR_SET:
	case XFS_ATTRI_OP_FLAGS_SET:
	case XFS_ATTRI_OP_FLAGS_REPLACE:
		/*
		 * Regular xattr set/remove/replace operations require a name
		 * and do not take a newname.  Values are optional for set and
		 * replace.
		 *
		 * Name-value set/remove operations must have a name, do not
		 * take a newname, and can take a value.
		 */
		if (attr_name == NULL || name_len == 0) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		break;
	case XFS_ATTRI_OP_FLAGS_PPTR_REPLACE:
		/*
		 * Name-value replace operations require the caller to
		 * specify the old and new names and values explicitly.
		 * Values are optional.
		 */
		if (attr_name == NULL || name_len == 0) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		if (attr_new_name == NULL || new_name_len == 0) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					     attri_formatp, len);
			return -EFSCORRUPTED;
		}
		break;
	}

	/*
	 * Memory alloc failure will cause replay to abort.  We attach the
	 * name/value buffer to the recovered incore log item and drop our
	 * reference.
	 */
	nv = xfs_attri_log_nameval_alloc(attr_name, name_len,
			attr_new_name, new_name_len,
			attr_value, value_len,
			attr_new_value, new_value_len);

	attrip = xfs_attri_init(mp, nv);
	memcpy(&attrip->attri_format, attri_formatp, len);

	xlog_recover_intent_item(log, &attrip->attri_item, lsn,
			&xfs_attr_defer_type);
	xfs_attri_log_nameval_put(nv);
	return 0;
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

	attrd_formatp = item->ri_buf[0].iov_base;
	if (item->ri_buf[0].iov_len != sizeof(struct xfs_attrd_log_format)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				item->ri_buf[0].iov_base, item->ri_buf[0].iov_len);
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
	.iop_intent	= xfs_attrd_item_intent,
};

const struct xlog_recover_item_ops xlog_attrd_item_ops = {
	.item_type	= XFS_LI_ATTRD,
	.commit_pass2	= xlog_recover_attrd_commit_pass2,
};
