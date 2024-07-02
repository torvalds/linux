/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_INODE_UTIL_H__
#define	__XFS_INODE_UTIL_H__

uint16_t	xfs_flags2diflags(struct xfs_inode *ip, unsigned int xflags);
uint64_t	xfs_flags2diflags2(struct xfs_inode *ip, unsigned int xflags);
uint32_t	xfs_dic2xflags(struct xfs_inode *ip);
uint32_t	xfs_ip2xflags(struct xfs_inode *ip);

prid_t		xfs_get_initial_prid(struct xfs_inode *dp);

/*
 * File creation context.
 *
 * Due to our only partial reliance on the VFS to propagate uid and gid values
 * according to accepted Unix behaviors, callers must initialize idmap to the
 * correct idmapping structure to get the correct inheritance behaviors when
 * XFS_MOUNT_GRPID is set.
 *
 * To create files detached from the directory tree (e.g. quota inodes), set
 * idmap to NULL.  To create a tree root, set pip to NULL.
 */
struct xfs_icreate_args {
	struct mnt_idmap	*idmap;
	struct xfs_inode	*pip;	/* parent inode or null */
	dev_t			rdev;
	umode_t			mode;

#define XFS_ICREATE_TMPFILE	(1U << 0)  /* create an unlinked file */
#define XFS_ICREATE_INIT_XATTRS	(1U << 1)  /* will set xattrs immediately */
	uint16_t		flags;
};

#endif /* __XFS_INODE_UTIL_H__ */
