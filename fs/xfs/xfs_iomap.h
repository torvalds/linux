// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IOMAP_H__
#define __XFS_IOMAP_H__

#include <linux/iomap.h>

struct xfs_inode;
struct xfs_bmbt_irec;
struct xfs_zone_alloc_ctx;

int xfs_iomap_write_direct(struct xfs_inode *ip, xfs_fileoff_t offset_fsb,
		xfs_fileoff_t count_fsb, unsigned int flags,
		struct xfs_bmbt_irec *imap, u64 *sequence);
int xfs_iomap_write_unwritten(struct xfs_inode *, xfs_off_t, xfs_off_t, bool);
xfs_fileoff_t xfs_iomap_eof_align_last_fsb(struct xfs_inode *ip,
		xfs_fileoff_t end_fsb);

u64 xfs_iomap_inode_sequence(struct xfs_inode *ip, u16 iomap_flags);
int xfs_bmbt_to_iomap(struct xfs_inode *ip, struct iomap *iomap,
		struct xfs_bmbt_irec *imap, unsigned int mapping_flags,
		u16 iomap_flags, u64 sequence_cookie);

int xfs_zero_range(struct xfs_inode *ip, loff_t pos, loff_t len,
		struct xfs_zone_alloc_ctx *ac, bool *did_zero);
int xfs_truncate_page(struct xfs_inode *ip, loff_t pos,
		struct xfs_zone_alloc_ctx *ac, bool *did_zero);

static inline xfs_filblks_t
xfs_aligned_fsb_count(
	xfs_fileoff_t		offset_fsb,
	xfs_filblks_t		count_fsb,
	xfs_extlen_t		extsz)
{
	if (extsz) {
		xfs_extlen_t	align;

		div_u64_rem(offset_fsb, extsz, &align);
		if (align)
			count_fsb += align;
		div_u64_rem(count_fsb, extsz, &align);
		if (align)
			count_fsb += extsz - align;
	}

	return count_fsb;
}

extern const struct iomap_ops xfs_buffered_write_iomap_ops;
extern const struct iomap_ops xfs_direct_write_iomap_ops;
extern const struct iomap_ops xfs_zoned_direct_write_iomap_ops;
extern const struct iomap_ops xfs_read_iomap_ops;
extern const struct iomap_ops xfs_seek_iomap_ops;
extern const struct iomap_ops xfs_xattr_iomap_ops;
extern const struct iomap_ops xfs_dax_write_iomap_ops;
extern const struct iomap_ops xfs_atomic_write_cow_iomap_ops;
extern const struct iomap_write_ops xfs_iomap_write_ops;

#endif /* __XFS_IOMAP_H__*/
