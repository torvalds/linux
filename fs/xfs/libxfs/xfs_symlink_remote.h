// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SYMLINK_REMOTE_H
#define __XFS_SYMLINK_REMOTE_H

/*
 * Symlink decoding/encoding functions
 */
int xfs_symlink_blocks(struct xfs_mount *mp, int pathlen);
int xfs_symlink_hdr_set(struct xfs_mount *mp, xfs_ino_t ino, uint32_t offset,
			uint32_t size, struct xfs_buf *bp);
bool xfs_symlink_hdr_ok(xfs_ino_t ino, uint32_t offset,
			uint32_t size, struct xfs_buf *bp);
void xfs_symlink_local_to_remote(struct xfs_trans *tp, struct xfs_buf *bp,
				 struct xfs_inode *ip, struct xfs_ifork *ifp);
xfs_failaddr_t xfs_symlink_shortform_verify(void *sfp, int64_t size);
int xfs_symlink_remote_read(struct xfs_inode *ip, char *link);
int xfs_symlink_write_target(struct xfs_trans *tp, struct xfs_inode *ip,
		const char *target_path, int pathlen, xfs_fsblock_t fs_blocks,
		uint resblks);

#endif /* __XFS_SYMLINK_REMOTE_H */
