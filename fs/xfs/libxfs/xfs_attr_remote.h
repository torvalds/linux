/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_ATTR_REMOTE_H__
#define	__XFS_ATTR_REMOTE_H__

int xfs_attr3_rmt_blocks(struct xfs_mount *mp, int attrlen);

int xfs_attr_rmtval_get(struct xfs_da_args *args);
int xfs_attr_rmtval_stale(struct xfs_inode *ip, struct xfs_bmbt_irec *map,
		xfs_buf_flags_t incore_flags);
int xfs_attr_rmtval_invalidate(struct xfs_da_args *args);
int xfs_attr_rmtval_remove(struct xfs_attr_intent *attr);
int xfs_attr_rmt_find_hole(struct xfs_da_args *args);
int xfs_attr_rmtval_set_value(struct xfs_da_args *args);
int xfs_attr_rmtval_set_blk(struct xfs_attr_intent *attr);
int xfs_attr_rmtval_find_space(struct xfs_attr_intent *attr);
#endif /* __XFS_ATTR_REMOTE_H__ */
