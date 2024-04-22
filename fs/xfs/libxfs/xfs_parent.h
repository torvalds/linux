// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2024 Oracle.
 * All Rights Reserved.
 */
#ifndef	__XFS_PARENT_H__
#define	__XFS_PARENT_H__

/* Metadata validators */
bool xfs_parent_namecheck(unsigned int attr_flags, const void *name,
		size_t length);
bool xfs_parent_valuecheck(struct xfs_mount *mp, const void *value,
		size_t valuelen);

xfs_dahash_t xfs_parent_hashval(struct xfs_mount *mp, const uint8_t *name,
		int namelen, xfs_ino_t parent_ino);
xfs_dahash_t xfs_parent_hashattr(struct xfs_mount *mp, const uint8_t *name,
		int namelen, const void *value, int valuelen);

#endif /* __XFS_PARENT_H__ */
