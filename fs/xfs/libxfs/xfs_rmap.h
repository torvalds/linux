/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __XFS_RMAP_H__
#define __XFS_RMAP_H__

static inline void
xfs_rmap_ag_owner(
	struct xfs_owner_info	*oi,
	uint64_t		owner)
{
	oi->oi_owner = owner;
	oi->oi_offset = 0;
	oi->oi_flags = 0;
}

static inline void
xfs_rmap_ino_bmbt_owner(
	struct xfs_owner_info	*oi,
	xfs_ino_t		ino,
	int			whichfork)
{
	oi->oi_owner = ino;
	oi->oi_offset = 0;
	oi->oi_flags = XFS_OWNER_INFO_BMBT_BLOCK;
	if (whichfork == XFS_ATTR_FORK)
		oi->oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
}

static inline void
xfs_rmap_ino_owner(
	struct xfs_owner_info	*oi,
	xfs_ino_t		ino,
	int			whichfork,
	xfs_fileoff_t		offset)
{
	oi->oi_owner = ino;
	oi->oi_offset = offset;
	oi->oi_flags = 0;
	if (whichfork == XFS_ATTR_FORK)
		oi->oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
}

static inline void
xfs_rmap_skip_owner_update(
	struct xfs_owner_info	*oi)
{
	oi->oi_owner = XFS_RMAP_OWN_UNKNOWN;
}

/* Reverse mapping functions. */

struct xfs_buf;

static inline __u64
xfs_rmap_irec_offset_pack(
	const struct xfs_rmap_irec	*irec)
{
	__u64			x;

	x = XFS_RMAP_OFF(irec->rm_offset);
	if (irec->rm_flags & XFS_RMAP_ATTR_FORK)
		x |= XFS_RMAP_OFF_ATTR_FORK;
	if (irec->rm_flags & XFS_RMAP_BMBT_BLOCK)
		x |= XFS_RMAP_OFF_BMBT_BLOCK;
	if (irec->rm_flags & XFS_RMAP_UNWRITTEN)
		x |= XFS_RMAP_OFF_UNWRITTEN;
	return x;
}

static inline int
xfs_rmap_irec_offset_unpack(
	__u64			offset,
	struct xfs_rmap_irec	*irec)
{
	if (offset & ~(XFS_RMAP_OFF_MASK | XFS_RMAP_OFF_FLAGS))
		return -EFSCORRUPTED;
	irec->rm_offset = XFS_RMAP_OFF(offset);
	if (offset & XFS_RMAP_OFF_ATTR_FORK)
		irec->rm_flags |= XFS_RMAP_ATTR_FORK;
	if (offset & XFS_RMAP_OFF_BMBT_BLOCK)
		irec->rm_flags |= XFS_RMAP_BMBT_BLOCK;
	if (offset & XFS_RMAP_OFF_UNWRITTEN)
		irec->rm_flags |= XFS_RMAP_UNWRITTEN;
	return 0;
}

static inline void
xfs_owner_info_unpack(
	struct xfs_owner_info	*oinfo,
	uint64_t		*owner,
	uint64_t		*offset,
	unsigned int		*flags)
{
	unsigned int		r = 0;

	*owner = oinfo->oi_owner;
	*offset = oinfo->oi_offset;
	if (oinfo->oi_flags & XFS_OWNER_INFO_ATTR_FORK)
		r |= XFS_RMAP_ATTR_FORK;
	if (oinfo->oi_flags & XFS_OWNER_INFO_BMBT_BLOCK)
		r |= XFS_RMAP_BMBT_BLOCK;
	*flags = r;
}

static inline void
xfs_owner_info_pack(
	struct xfs_owner_info	*oinfo,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags)
{
	oinfo->oi_owner = owner;
	oinfo->oi_offset = XFS_RMAP_OFF(offset);
	oinfo->oi_flags = 0;
	if (flags & XFS_RMAP_ATTR_FORK)
		oinfo->oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
	if (flags & XFS_RMAP_BMBT_BLOCK)
		oinfo->oi_flags |= XFS_OWNER_INFO_BMBT_BLOCK;
}

int xfs_rmap_alloc(struct xfs_trans *tp, struct xfs_buf *agbp,
		   xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t len,
		   struct xfs_owner_info *oinfo);
int xfs_rmap_free(struct xfs_trans *tp, struct xfs_buf *agbp,
		  xfs_agnumber_t agno, xfs_agblock_t bno, xfs_extlen_t len,
		  struct xfs_owner_info *oinfo);

int xfs_rmap_lookup_le(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, uint64_t owner, uint64_t offset,
		unsigned int flags, int *stat);
int xfs_rmap_lookup_eq(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, uint64_t owner, uint64_t offset,
		unsigned int flags, int *stat);
int xfs_rmap_get_rec(struct xfs_btree_cur *cur, struct xfs_rmap_irec *irec,
		int *stat);

#endif	/* __XFS_RMAP_H__ */
