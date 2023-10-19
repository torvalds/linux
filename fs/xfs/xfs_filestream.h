// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_FILESTREAM_H__
#define __XFS_FILESTREAM_H__

struct xfs_mount;
struct xfs_inode;
struct xfs_bmalloca;

int xfs_filestream_mount(struct xfs_mount *mp);
void xfs_filestream_unmount(struct xfs_mount *mp);
void xfs_filestream_deassociate(struct xfs_inode *ip);
xfs_agnumber_t xfs_filestream_lookup_ag(struct xfs_inode *ip);
int xfs_filestream_new_ag(struct xfs_bmalloca *ap, xfs_agnumber_t *agp);
int xfs_filestream_peek_ag(struct xfs_mount *mp, xfs_agnumber_t agno);

static inline int
xfs_inode_is_filestream(
	struct xfs_inode	*ip)
{
	return xfs_has_filestreams(ip->i_mount) ||
		(ip->i_diflags & XFS_DIFLAG_FILESTREAM);
}

#endif /* __XFS_FILESTREAM_H__ */
