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
#ifndef __XFS_IALLOC_BTREE_H__
#define	__XFS_IALLOC_BTREE_H__

/*
 * Inode map on-disk structures
 */

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_btree_sblock;
struct xfs_mount;

/*
 * There is a btree for the inode map per allocation group.
 */
#define	XFS_IBT_MAGIC	0x49414254	/* 'IABT' */

typedef	__uint64_t	xfs_inofree_t;
#define	XFS_INODES_PER_CHUNK	(NBBY * sizeof(xfs_inofree_t))
#define	XFS_INODES_PER_CHUNK_LOG	(XFS_NBBYLOG + 3)
#define	XFS_INOBT_ALL_FREE	((xfs_inofree_t)-1)

#define	XFS_INOBT_MASKN(i,n)		xfs_inobt_maskn(i,n)
static inline xfs_inofree_t xfs_inobt_maskn(int i, int n)
{
	return (((n) >= XFS_INODES_PER_CHUNK ? \
		(xfs_inofree_t)0 : ((xfs_inofree_t)1 << (n))) - 1) << (i);
}

/*
 * Data record structure
 */
typedef struct xfs_inobt_rec {
	__be32		ir_startino;	/* starting inode number */
	__be32		ir_freecount;	/* count of free inodes (set bits) */
	__be64		ir_free;	/* free inode mask */
} xfs_inobt_rec_t;

typedef struct xfs_inobt_rec_incore {
	xfs_agino_t	ir_startino;	/* starting inode number */
	__int32_t	ir_freecount;	/* count of free inodes (set bits) */
	xfs_inofree_t	ir_free;	/* free inode mask */
} xfs_inobt_rec_incore_t;


/*
 * Key structure
 */
typedef struct xfs_inobt_key {
	__be32		ir_startino;	/* starting inode number */
} xfs_inobt_key_t;

/* btree pointer type */
typedef __be32 xfs_inobt_ptr_t;

/* btree block header type */
typedef	struct xfs_btree_sblock xfs_inobt_block_t;

#define	XFS_BUF_TO_INOBT_BLOCK(bp)	((xfs_inobt_block_t *)XFS_BUF_PTR(bp))

/*
 * Bit manipulations for ir_free.
 */
#define	XFS_INOBT_MASK(i)		((xfs_inofree_t)1 << (i))
#define	XFS_INOBT_IS_FREE(rp,i)		\
		(((rp)->ir_free & XFS_INOBT_MASK(i)) != 0)
#define	XFS_INOBT_IS_FREE_DISK(rp,i)	\
		((be64_to_cpu((rp)->ir_free) & XFS_INOBT_MASK(i)) != 0)
#define	XFS_INOBT_SET_FREE(rp,i)	((rp)->ir_free |= XFS_INOBT_MASK(i))
#define	XFS_INOBT_CLR_FREE(rp,i)	((rp)->ir_free &= ~XFS_INOBT_MASK(i))

/*
 * Real block structures have a size equal to the disk block size.
 */
#define	XFS_INOBT_BLOCK_MAXRECS(lev,cur) ((cur)->bc_mp->m_inobt_mxr[lev != 0])
#define	XFS_INOBT_BLOCK_MINRECS(lev,cur) ((cur)->bc_mp->m_inobt_mnr[lev != 0])
#define	XFS_INOBT_IS_LAST_REC(cur)	\
	((cur)->bc_ptrs[0] == be16_to_cpu(XFS_BUF_TO_INOBT_BLOCK((cur)->bc_bufs[0])->bb_numrecs))

/*
 * Maximum number of inode btree levels.
 */
#define	XFS_IN_MAXLEVELS(mp)		((mp)->m_in_maxlevels)

/*
 * block numbers in the AG.
 */
#define	XFS_IBT_BLOCK(mp)		((xfs_agblock_t)(XFS_CNT_BLOCK(mp) + 1))
#define	XFS_PREALLOC_BLOCKS(mp)		((xfs_agblock_t)(XFS_IBT_BLOCK(mp) + 1))

/*
 * Record, key, and pointer address macros for btree blocks.
 */
#define XFS_INOBT_REC_ADDR(bb,i,cur) \
	(XFS_BTREE_REC_ADDR(xfs_inobt, bb, i))

#define	XFS_INOBT_KEY_ADDR(bb,i,cur) \
	(XFS_BTREE_KEY_ADDR(xfs_inobt, bb, i))

#define	XFS_INOBT_PTR_ADDR(bb,i,cur) \
	(XFS_BTREE_PTR_ADDR(xfs_inobt, bb, \
				i, XFS_INOBT_BLOCK_MAXRECS(1, cur)))

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
extern int xfs_inobt_decrement(struct xfs_btree_cur *cur, int level, int *stat);

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
extern int xfs_inobt_delete(struct xfs_btree_cur *cur, int *stat);

/*
 * Get the data from the pointed-to record.
 */
extern int xfs_inobt_get_rec(struct xfs_btree_cur *cur, xfs_agino_t *ino,
			     __int32_t *fcnt, xfs_inofree_t *free, int *stat);

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
extern int xfs_inobt_increment(struct xfs_btree_cur *cur, int level, int *stat);

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
extern int xfs_inobt_insert(struct xfs_btree_cur *cur, int *stat);

/*
 * Lookup the record equal to ino in the btree given by cur.
 */
extern int xfs_inobt_lookup_eq(struct xfs_btree_cur *cur, xfs_agino_t ino,
				__int32_t fcnt, xfs_inofree_t free, int *stat);

/*
 * Lookup the first record greater than or equal to ino
 * in the btree given by cur.
 */
extern int xfs_inobt_lookup_ge(struct xfs_btree_cur *cur, xfs_agino_t ino,
				__int32_t fcnt,	xfs_inofree_t free, int *stat);

/*
 * Lookup the first record less than or equal to ino
 * in the btree given by cur.
 */
extern int xfs_inobt_lookup_le(struct xfs_btree_cur *cur, xfs_agino_t ino,
				__int32_t fcnt,	xfs_inofree_t free, int *stat);

/*
 * Update the record referred to by cur, to the value given
 * by [ino, fcnt, free].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
extern int xfs_inobt_update(struct xfs_btree_cur *cur, xfs_agino_t ino,
				__int32_t fcnt, xfs_inofree_t free);

#endif	/* __XFS_IALLOC_BTREE_H__ */
