/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_LISTXATTR_H__
#define __XFS_SCRUB_LISTXATTR_H__

typedef int (*xchk_xattr_fn)(struct xfs_scrub *sc, struct xfs_inode *ip,
		unsigned int attr_flags, const unsigned char *name,
		unsigned int namelen, const void *value, unsigned int valuelen,
		void *priv);

int xchk_xattr_walk(struct xfs_scrub *sc, struct xfs_inode *ip,
		xchk_xattr_fn attr_fn, void *priv);

#endif /* __XFS_SCRUB_LISTXATTR_H__ */
