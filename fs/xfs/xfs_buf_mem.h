// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_BUF_MEM_H__
#define __XFS_BUF_MEM_H__

#define XMBUF_BLOCKSIZE			(PAGE_SIZE)
#define XMBUF_BLOCKSHIFT		(PAGE_SHIFT)

#ifdef CONFIG_XFS_MEMORY_BUFS
static inline bool xfs_buftarg_is_mem(const struct xfs_buftarg *btp)
{
	return btp->bt_bdev == NULL;
}

int xmbuf_alloc(struct xfs_mount *mp, const char *descr,
		struct xfs_buftarg **btpp);
void xmbuf_free(struct xfs_buftarg *btp);

bool xmbuf_verify_daddr(struct xfs_buftarg *btp, xfs_daddr_t daddr);
void xmbuf_trans_bdetach(struct xfs_trans *tp, struct xfs_buf *bp);
int xmbuf_finalize(struct xfs_buf *bp);
#else
# define xfs_buftarg_is_mem(...)	(false)
# define xmbuf_verify_daddr(...)	(false)
#endif /* CONFIG_XFS_MEMORY_BUFS */

int xmbuf_map_backing_mem(struct xfs_buf *bp);

#endif /* __XFS_BUF_MEM_H__ */
