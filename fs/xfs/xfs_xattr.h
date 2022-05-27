// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_XATTR_H__
#define __XFS_XATTR_H__

int xfs_attr_grab_log_assist(struct xfs_mount *mp);
void xfs_attr_rele_log_assist(struct xfs_mount *mp);

extern const struct xattr_handler *xfs_xattr_handlers[];

#endif /* __XFS_XATTR_H__ */
