/*
 * Copyright (c) 2013 Red Hat, Inc.
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
#ifndef __XFS_ATTR_REMOTE_H__
#define	__XFS_ATTR_REMOTE_H__

int xfs_attr3_rmt_blocks(struct xfs_mount *mp, int attrlen);

int xfs_attr_rmtval_get(struct xfs_da_args *args);
int xfs_attr_rmtval_set(struct xfs_da_args *args);
int xfs_attr_rmtval_remove(struct xfs_da_args *args);

#endif /* __XFS_ATTR_REMOTE_H__ */
