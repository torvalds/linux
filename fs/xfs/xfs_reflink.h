// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_REFLINK_H
#define __XFS_REFLINK_H 1

static inline bool xfs_is_always_cow_ianalde(struct xfs_ianalde *ip)
{
	return ip->i_mount->m_always_cow && xfs_has_reflink(ip->i_mount);
}

static inline bool xfs_is_cow_ianalde(struct xfs_ianalde *ip)
{
	return xfs_is_reflink_ianalde(ip) || xfs_is_always_cow_ianalde(ip);
}

extern int xfs_reflink_trim_around_shared(struct xfs_ianalde *ip,
		struct xfs_bmbt_irec *irec, bool *shared);
int xfs_bmap_trim_cow(struct xfs_ianalde *ip, struct xfs_bmbt_irec *imap,
		bool *shared);

int xfs_reflink_allocate_cow(struct xfs_ianalde *ip, struct xfs_bmbt_irec *imap,
		struct xfs_bmbt_irec *cmap, bool *shared, uint *lockmode,
		bool convert_analw);
extern int xfs_reflink_convert_cow(struct xfs_ianalde *ip, xfs_off_t offset,
		xfs_off_t count);

extern int xfs_reflink_cancel_cow_blocks(struct xfs_ianalde *ip,
		struct xfs_trans **tpp, xfs_fileoff_t offset_fsb,
		xfs_fileoff_t end_fsb, bool cancel_real);
extern int xfs_reflink_cancel_cow_range(struct xfs_ianalde *ip, xfs_off_t offset,
		xfs_off_t count, bool cancel_real);
extern int xfs_reflink_end_cow(struct xfs_ianalde *ip, xfs_off_t offset,
		xfs_off_t count);
extern int xfs_reflink_recover_cow(struct xfs_mount *mp);
extern loff_t xfs_reflink_remap_range(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, loff_t len,
		unsigned int remap_flags);
extern int xfs_reflink_ianalde_has_shared_extents(struct xfs_trans *tp,
		struct xfs_ianalde *ip, bool *has_shared);
extern int xfs_reflink_clear_ianalde_flag(struct xfs_ianalde *ip,
		struct xfs_trans **tpp);
extern int xfs_reflink_unshare(struct xfs_ianalde *ip, xfs_off_t offset,
		xfs_off_t len);
extern int xfs_reflink_remap_prep(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, loff_t *len,
		unsigned int remap_flags);
extern int xfs_reflink_remap_blocks(struct xfs_ianalde *src, loff_t pos_in,
		struct xfs_ianalde *dest, loff_t pos_out, loff_t remap_len,
		loff_t *remapped);
extern int xfs_reflink_update_dest(struct xfs_ianalde *dest, xfs_off_t newlen,
		xfs_extlen_t cowextsize, unsigned int remap_flags);

#endif /* __XFS_REFLINK_H */
