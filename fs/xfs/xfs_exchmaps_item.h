/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef	__XFS_EXCHMAPS_ITEM_H__
#define	__XFS_EXCHMAPS_ITEM_H__

/*
 * The file mapping exchange intent item helps us exchange multiple file
 * mappings between two inode forks.  It does this by tracking the range of
 * file block offsets that still need to be exchanged, and relogs as progress
 * happens.
 *
 * *I items should be recorded in the *first* of a series of rolled
 * transactions, and the *D items should be recorded in the same transaction
 * that records the associated bmbt updates.
 *
 * Should the system crash after the commit of the first transaction but
 * before the commit of the final transaction in a series, log recovery will
 * use the redo information recorded by the intent items to replay the
 * rest of the mapping exchanges.
 */

/* kernel only XMI/XMD definitions */

struct xfs_mount;
struct kmem_cache;

/*
 * This is the incore file mapping exchange intent log item.  It is used to log
 * the fact that we are exchanging mappings between two files.  It is used in
 * conjunction with the incore file mapping exchange done log item described
 * below.
 *
 * These log items follow the same rules as struct xfs_efi_log_item; see the
 * comments about that structure (in xfs_extfree_item.h) for more details.
 */
struct xfs_xmi_log_item {
	struct xfs_log_item		xmi_item;
	atomic_t			xmi_refcount;
	struct xfs_xmi_log_format	xmi_format;
};

/*
 * This is the incore file mapping exchange done log item.  It is used to log
 * the fact that an exchange mentioned in an earlier xmi item have been
 * performed.
 */
struct xfs_xmd_log_item {
	struct xfs_log_item		xmd_item;
	struct xfs_xmi_log_item		*xmd_intent_log_item;
	struct xfs_xmd_log_format	xmd_format;
};

extern struct kmem_cache	*xfs_xmi_cache;
extern struct kmem_cache	*xfs_xmd_cache;

struct xfs_exchmaps_intent;

void xfs_exchmaps_defer_add(struct xfs_trans *tp,
		struct xfs_exchmaps_intent *xmi);

#endif	/* __XFS_EXCHMAPS_ITEM_H__ */
