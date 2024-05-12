/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_READDIR_H__
#define __XFS_SCRUB_READDIR_H__

typedef int (*xchk_dirent_fn)(struct xfs_scrub *sc, struct xfs_inode *dp,
		xfs_dir2_dataptr_t dapos, const struct xfs_name *name,
		xfs_ino_t ino, void *priv);

int xchk_dir_walk(struct xfs_scrub *sc, struct xfs_inode *dp,
		xchk_dirent_fn dirent_fn, void *priv);

int xchk_dir_lookup(struct xfs_scrub *sc, struct xfs_inode *dp,
		const struct xfs_name *name, xfs_ino_t *ino);

#endif /* __XFS_SCRUB_READDIR_H__ */
