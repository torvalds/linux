// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_ATTR_REMOTE_H__
#define	__XFS_ATTR_REMOTE_H__

int xfs_attr3_rmt_blocks(struct xfs_mount *mp, int attrlen);

int xfs_attr_rmtval_get(struct xfs_da_args *args);
int xfs_attr_rmtval_set(struct xfs_da_args *args);
int xfs_attr_rmtval_remove(struct xfs_da_args *args);

#endif /* __XFS_ATTR_REMOTE_H__ */
