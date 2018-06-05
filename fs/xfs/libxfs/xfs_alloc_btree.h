/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_ALLOC_BTREE_H__
#define	__XFS_ALLOC_BTREE_H__

/*
 * Freespace on-disk structures
 */

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;

/*
 * Btree block header size depends on a superblock flag.
 */
#define XFS_ALLOC_BLOCK_LEN(mp) \
	(xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		XFS_BTREE_SBLOCK_CRC_LEN : XFS_BTREE_SBLOCK_LEN)

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
#define XFS_ALLOC_REC_ADDR(mp, block, index) \
	((xfs_alloc_rec_t *) \
		((char *)(block) + \
		 XFS_ALLOC_BLOCK_LEN(mp) + \
		 (((index) - 1) * sizeof(xfs_alloc_rec_t))))

#define XFS_ALLOC_KEY_ADDR(mp, block, index) \
	((xfs_alloc_key_t *) \
		((char *)(block) + \
		 XFS_ALLOC_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_alloc_key_t)))

#define XFS_ALLOC_PTR_ADDR(mp, block, index, maxrecs) \
	((xfs_alloc_ptr_t *) \
		((char *)(block) + \
		 XFS_ALLOC_BLOCK_LEN(mp) + \
		 (maxrecs) * sizeof(xfs_alloc_key_t) + \
		 ((index) - 1) * sizeof(xfs_alloc_ptr_t)))

extern struct xfs_btree_cur *xfs_allocbt_init_cursor(struct xfs_mount *,
		struct xfs_trans *, struct xfs_buf *,
		xfs_agnumber_t, xfs_btnum_t);
extern int xfs_allocbt_maxrecs(struct xfs_mount *, int, int);
extern xfs_extlen_t xfs_allocbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);

#endif	/* __XFS_ALLOC_BTREE_H__ */
