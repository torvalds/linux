// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_REFLINK_H
#define __XFS_REFLINK_H 1

static inline bool xfs_is_always_cow_iyesde(struct xfs_iyesde *ip)
{
	return ip->i_mount->m_always_cow &&
		xfs_sb_version_hasreflink(&ip->i_mount->m_sb);
}

static inline bool xfs_is_cow_iyesde(struct xfs_iyesde *ip)
{
	return xfs_is_reflink_iyesde(ip) || xfs_is_always_cow_iyesde(ip);
}

extern int xfs_reflink_find_shared(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agyes, xfs_agblock_t agbyes, xfs_extlen_t aglen,
		xfs_agblock_t *fbyes, xfs_extlen_t *flen, bool find_maximal);
extern int xfs_reflink_trim_around_shared(struct xfs_iyesde *ip,
		struct xfs_bmbt_irec *irec, bool *shared);
bool xfs_iyesde_need_cow(struct xfs_iyesde *ip, struct xfs_bmbt_irec *imap,
		bool *shared);

int xfs_reflink_allocate_cow(struct xfs_iyesde *ip, struct xfs_bmbt_irec *imap,
		struct xfs_bmbt_irec *cmap, bool *shared, uint *lockmode,
		bool convert_yesw);
extern int xfs_reflink_convert_cow(struct xfs_iyesde *ip, xfs_off_t offset,
		xfs_off_t count);

extern int xfs_reflink_cancel_cow_blocks(struct xfs_iyesde *ip,
		struct xfs_trans **tpp, xfs_fileoff_t offset_fsb,
		xfs_fileoff_t end_fsb, bool cancel_real);
extern int xfs_reflink_cancel_cow_range(struct xfs_iyesde *ip, xfs_off_t offset,
		xfs_off_t count, bool cancel_real);
extern int xfs_reflink_end_cow(struct xfs_iyesde *ip, xfs_off_t offset,
		xfs_off_t count);
extern int xfs_reflink_recover_cow(struct xfs_mount *mp);
extern loff_t xfs_reflink_remap_range(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, loff_t len,
		unsigned int remap_flags);
extern int xfs_reflink_iyesde_has_shared_extents(struct xfs_trans *tp,
		struct xfs_iyesde *ip, bool *has_shared);
extern int xfs_reflink_clear_iyesde_flag(struct xfs_iyesde *ip,
		struct xfs_trans **tpp);
extern int xfs_reflink_unshare(struct xfs_iyesde *ip, xfs_off_t offset,
		xfs_off_t len);
extern int xfs_reflink_remap_prep(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, loff_t *len,
		unsigned int remap_flags);
extern int xfs_reflink_remap_blocks(struct xfs_iyesde *src, loff_t pos_in,
		struct xfs_iyesde *dest, loff_t pos_out, loff_t remap_len,
		loff_t *remapped);
extern int xfs_reflink_update_dest(struct xfs_iyesde *dest, xfs_off_t newlen,
		xfs_extlen_t cowextsize, unsigned int remap_flags);
extern void xfs_reflink_remap_unlock(struct file *file_in,
		struct file *file_out);

#endif /* __XFS_REFLINK_H */
