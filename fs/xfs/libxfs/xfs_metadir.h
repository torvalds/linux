/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_METADIR_H__
#define __XFS_METADIR_H__

/* Cleanup widget for metadata inode creation and deletion. */
struct xfs_metadir_update {
	/* Parent directory */
	struct xfs_inode	*dp;

	/* Path to metadata file */
	const char		*path;

	/* Parent pointer update context */
	struct xfs_parent_args	*ppargs;

	/* Child metadata file */
	struct xfs_inode	*ip;

	struct xfs_trans	*tp;

	enum xfs_metafile_type	metafile_type;

	unsigned int		dp_locked:1;
	unsigned int		ip_locked:1;
};

int xfs_metadir_load(struct xfs_trans *tp, struct xfs_inode *dp,
		const char *path, enum xfs_metafile_type metafile_type,
		struct xfs_inode **ipp);

int xfs_metadir_start_create(struct xfs_metadir_update *upd);
int xfs_metadir_create(struct xfs_metadir_update *upd, umode_t mode);

int xfs_metadir_start_link(struct xfs_metadir_update *upd);
int xfs_metadir_link(struct xfs_metadir_update *upd);

int xfs_metadir_commit(struct xfs_metadir_update *upd);
void xfs_metadir_cancel(struct xfs_metadir_update *upd, int error);

int xfs_metadir_mkdir(struct xfs_inode *dp, const char *path,
		struct xfs_inode **ipp);

#endif /* __XFS_METADIR_H__ */
