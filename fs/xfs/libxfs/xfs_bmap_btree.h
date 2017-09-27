/*
 * Copyright (c) 2000,2002-2005 Silicon Graphics, Inc.
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
#ifndef __XFS_BMAP_BTREE_H__
#define __XFS_BMAP_BTREE_H__

struct xfs_btree_cur;
struct xfs_btree_block;
struct xfs_mount;
struct xfs_inode;
struct xfs_trans;

/*
 * Btree block header size depends on a superblock flag.
 */
#define XFS_BMBT_BLOCK_LEN(mp) \
	(xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		XFS_BTREE_LBLOCK_CRC_LEN : XFS_BTREE_LBLOCK_LEN)

#define XFS_BMBT_REC_ADDR(mp, block, index) \
	((xfs_bmbt_rec_t *) \
		((char *)(block) + \
		 XFS_BMBT_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_bmbt_rec_t)))

#define XFS_BMBT_KEY_ADDR(mp, block, index) \
	((xfs_bmbt_key_t *) \
		((char *)(block) + \
		 XFS_BMBT_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_bmbt_key_t)))

#define XFS_BMBT_PTR_ADDR(mp, block, index, maxrecs) \
	((xfs_bmbt_ptr_t *) \
		((char *)(block) + \
		 XFS_BMBT_BLOCK_LEN(mp) + \
		 (maxrecs) * sizeof(xfs_bmbt_key_t) + \
		 ((index) - 1) * sizeof(xfs_bmbt_ptr_t)))

#define XFS_BMDR_REC_ADDR(block, index) \
	((xfs_bmdr_rec_t *) \
		((char *)(block) + \
		 sizeof(struct xfs_bmdr_block) + \
	         ((index) - 1) * sizeof(xfs_bmdr_rec_t)))

#define XFS_BMDR_KEY_ADDR(block, index) \
	((xfs_bmdr_key_t *) \
		((char *)(block) + \
		 sizeof(struct xfs_bmdr_block) + \
		 ((index) - 1) * sizeof(xfs_bmdr_key_t)))

#define XFS_BMDR_PTR_ADDR(block, index, maxrecs) \
	((xfs_bmdr_ptr_t *) \
		((char *)(block) + \
		 sizeof(struct xfs_bmdr_block) + \
		 (maxrecs) * sizeof(xfs_bmdr_key_t) + \
		 ((index) - 1) * sizeof(xfs_bmdr_ptr_t)))

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
#define XFS_BMAP_BROOT_PTR_ADDR(mp, bb, i, sz) \
	XFS_BMBT_PTR_ADDR(mp, bb, i, xfs_bmbt_maxrecs(mp, sz, 0))

#define XFS_BMAP_BROOT_SPACE_CALC(mp, nrecs) \
	(int)(XFS_BMBT_BLOCK_LEN(mp) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))

#define XFS_BMAP_BROOT_SPACE(mp, bb) \
	(XFS_BMAP_BROOT_SPACE_CALC(mp, be16_to_cpu((bb)->bb_numrecs)))
#define XFS_BMDR_SPACE_CALC(nrecs) \
	(int)(sizeof(xfs_bmdr_block_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))
#define XFS_BMAP_BMDR_SPACE(bb) \
	(XFS_BMDR_SPACE_CALC(be16_to_cpu((bb)->bb_numrecs)))

/*
 * Maximum number of bmap btree levels.
 */
#define XFS_BM_MAXLEVELS(mp,w)		((mp)->m_bm_maxlevels[(w)])

/*
 * Prototypes for xfs_bmap.c to call.
 */
extern void xfs_bmdr_to_bmbt(struct xfs_inode *, xfs_bmdr_block_t *, int,
			struct xfs_btree_block *, int);
extern void xfs_bmbt_get_all(xfs_bmbt_rec_host_t *r, xfs_bmbt_irec_t *s);
extern xfs_filblks_t xfs_bmbt_get_blockcount(xfs_bmbt_rec_host_t *r);
extern xfs_fsblock_t xfs_bmbt_get_startblock(xfs_bmbt_rec_host_t *r);
extern xfs_fileoff_t xfs_bmbt_get_startoff(xfs_bmbt_rec_host_t *r);
extern xfs_exntst_t xfs_bmbt_get_state(xfs_bmbt_rec_host_t *r);

extern xfs_filblks_t xfs_bmbt_disk_get_blockcount(xfs_bmbt_rec_t *r);
extern xfs_fileoff_t xfs_bmbt_disk_get_startoff(xfs_bmbt_rec_t *r);

extern void xfs_bmbt_set_all(xfs_bmbt_rec_host_t *r, xfs_bmbt_irec_t *s);
extern void xfs_bmbt_set_allf(xfs_bmbt_rec_host_t *r, xfs_fileoff_t o,
			xfs_fsblock_t b, xfs_filblks_t c, xfs_exntst_t v);
extern void xfs_bmbt_set_blockcount(xfs_bmbt_rec_host_t *r, xfs_filblks_t v);
extern void xfs_bmbt_set_startblock(xfs_bmbt_rec_host_t *r, xfs_fsblock_t v);
extern void xfs_bmbt_set_startoff(xfs_bmbt_rec_host_t *r, xfs_fileoff_t v);
extern void xfs_bmbt_set_state(xfs_bmbt_rec_host_t *r, xfs_exntst_t v);

extern void xfs_bmbt_disk_set_allf(xfs_bmbt_rec_t *r, xfs_fileoff_t o,
			xfs_fsblock_t b, xfs_filblks_t c, xfs_exntst_t v);

extern void xfs_bmbt_to_bmdr(struct xfs_mount *, struct xfs_btree_block *, int,
			xfs_bmdr_block_t *, int);

extern int xfs_bmbt_get_maxrecs(struct xfs_btree_cur *, int level);
extern int xfs_bmdr_maxrecs(int blocklen, int leaf);
extern int xfs_bmbt_maxrecs(struct xfs_mount *, int blocklen, int leaf);

extern int xfs_bmbt_change_owner(struct xfs_trans *tp, struct xfs_inode *ip,
				 int whichfork, xfs_ino_t new_owner,
				 struct list_head *buffer_list);

extern struct xfs_btree_cur *xfs_bmbt_init_cursor(struct xfs_mount *,
		struct xfs_trans *, struct xfs_inode *, int);

/*
 * Check that the extent does not contain an invalid unwritten extent flag.
 */
static inline bool xfs_bmbt_validate_extent(struct xfs_mount *mp, int whichfork,
		struct xfs_bmbt_rec_host *ep)
{
	if (ep->l0 >> (64 - BMBT_EXNTFLAG_BITLEN) == 0)
		return true;
	if (whichfork == XFS_DATA_FORK &&
	    xfs_sb_version_hasextflgbit(&mp->m_sb))
		return true;
	return false;
}

#endif	/* __XFS_BMAP_BTREE_H__ */
