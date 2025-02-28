// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_REFLINK_H
#define __XFS_REFLINK_H 1

/*
 * Check whether it is safe to free COW fork blocks from an inode. It is unsafe
 * to do so when an inode has dirty cache or I/O in-flight, even if no shared
 * extents exist in the data fork, because outstanding I/O may target blocks
 * that were speculatively allocated to the COW fork.
 */
static inline bool
xfs_can_free_cowblocks(struct xfs_inode *ip)
{
	struct inode *inode = VFS_I(ip);

	if ((inode->i_state & I_DIRTY_PAGES) ||
	    mapping_tagged(inode->i_mapping, PAGECACHE_TAG_DIRTY) ||
	    mapping_tagged(inode->i_mapping, PAGECACHE_TAG_WRITEBACK) ||
	    atomic_read(&inode->i_dio_count))
		return false;
	return true;
}

int xfs_reflink_trim_around_shared(struct xfs_inode *ip,
		struct xfs_bmbt_irec *irec, bool *shared);
int xfs_bmap_trim_cow(struct xfs_inode *ip, struct xfs_bmbt_irec *imap,
		bool *shared);

int xfs_reflink_allocate_cow(struct xfs_inode *ip, struct xfs_bmbt_irec *imap,
		struct xfs_bmbt_irec *cmap, bool *shared, uint *lockmode,
		bool convert_now);
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
extern loff_t xfs_reflink_remap_range(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, loff_t len,
		unsigned int remap_flags);
extern int xfs_reflink_inode_has_shared_extents(struct xfs_trans *tp,
		struct xfs_inode *ip, bool *has_shared);
extern int xfs_reflink_clear_inode_flag(struct xfs_inode *ip,
		struct xfs_trans **tpp);
extern int xfs_reflink_unshare(struct xfs_inode *ip, xfs_off_t offset,
		xfs_off_t len);
extern int xfs_reflink_remap_prep(struct file *file_in, loff_t pos_in,
		struct file *file_out, loff_t pos_out, loff_t *len,
		unsigned int remap_flags);
extern int xfs_reflink_remap_blocks(struct xfs_inode *src, loff_t pos_in,
		struct xfs_inode *dest, loff_t pos_out, loff_t remap_len,
		loff_t *remapped);
extern int xfs_reflink_update_dest(struct xfs_inode *dest, xfs_off_t newlen,
		xfs_extlen_t cowextsize, unsigned int remap_flags);

bool xfs_reflink_supports_rextsize(struct xfs_mount *mp, unsigned int rextsize);

#endif /* __XFS_REFLINK_H */
