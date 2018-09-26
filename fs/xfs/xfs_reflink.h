// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_REFLINK_H
#define __XFS_REFLINK_H 1

extern int xfs_reflink_find_shared(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agno, xfs_agblock_t agbno, xfs_extlen_t aglen,
		xfs_agblock_t *fbno, xfs_extlen_t *flen, bool find_maximal);
extern int xfs_reflink_trim_around_shared(struct xfs_inode *ip,
		struct xfs_bmbt_irec *irec, bool *shared, bool *trimmed);

extern int xfs_reflink_reserve_cow(struct xfs_inode *ip,
		struct xfs_bmbt_irec *imap, bool *shared);
extern int xfs_reflink_allocate_cow(struct xfs_inode *ip,
		struct xfs_bmbt_irec *imap, bool *shared, uint *lockmode);
extern int xfs_reflink_convert_cow(struct xfs_inode *ip, xfs_off_t offset,
		xfs_off_t count);

extern int xfs_reflink_cancel_cow_blocks(struct xfs_inode *ip,
		struct xfs_trans **tpp, xfs_fileoff_t offset_fsb,
		xfs_fileoff_t end_fsb, bool cancel_real);
extern int xfs_reflink_cancel_cow_range(struct xfs_inode *ip, xfs_off_t offset,
		xfs_off_t count, bool cancel_real);
extern int xfs_reflink_end_cow(struct xfs_inode *ip, xfs_off_t offset,
		xfs_off_t count);
extern int xfs_reflink_recover_cow(struct xfs_mount *mp);
extern int xfs_reflink_remap_range(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, u64 len, bool is_dedupe);
extern int xfs_reflink_inode_has_shared_extents(struct xfs_trans *tp,
		struct xfs_inode *ip, bool *has_shared);
extern int xfs_reflink_clear_inode_flag(struct xfs_inode *ip,
		struct xfs_trans **tpp);
extern int xfs_reflink_unshare(struct xfs_inode *ip, xfs_off_t offset,
		xfs_off_t len);

#endif /* __XFS_REFLINK_H */
