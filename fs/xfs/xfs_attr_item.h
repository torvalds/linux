/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2022 Oracle.  All Rights Reserved.
 * Author: Allison Henderson <allison.henderson@oracle.com>
 */
#ifndef	__XFS_ATTR_ITEM_H__
#define	__XFS_ATTR_ITEM_H__

/* kernel only ATTRI/ATTRD definitions */

struct xfs_mount;
struct kmem_zone;

struct xfs_attri_log_nameval {
	struct xfs_log_iovec	name;
	struct xfs_log_iovec	value;
	refcount_t		refcount;

	/* name and value follow the end of this struct */
};

/*
 * This is the "attr intention" log item.  It is used to log the fact that some
 * extended attribute operations need to be processed.  An operation is
 * currently either a set or remove.  Set or remove operations are described by
 * the xfs_attr_intent which may be logged to this intent.
 *
 * During a normal attr operation, name and value point to the name and value
 * fields of the caller's xfs_da_args structure.  During a recovery, the name
 * and value buffers are copied from the log, and stored in a trailing buffer
 * attached to the xfs_attr_intent until they are committed.  They are freed
 * when the xfs_attr_intent itself is freed when the work is done.
 */
struct xfs_attri_log_item {
	struct xfs_log_item		attri_item;
	atomic_t			attri_refcount;
	struct xfs_attri_log_nameval	*attri_nameval;
	struct xfs_attri_log_format	attri_format;
};

/*
 * This is the "attr done" log item.  It is used to log the fact that some attrs
 * earlier mentioned in an attri item have been freed.
 */
struct xfs_attrd_log_item {
	struct xfs_log_item		attrd_item;
	struct xfs_attri_log_item	*attrd_attrip;
	struct xfs_attrd_log_format	attrd_format;
};

extern struct kmem_cache	*xfs_attri_cache;
extern struct kmem_cache	*xfs_attrd_cache;

#endif	/* __XFS_ATTR_ITEM_H__ */
