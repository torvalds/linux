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
struct xfs_btree_sblock;
struct xfs_mount;

/*
 * There are two on-disk btrees, one sorted by blockno and one sorted
 * by blockcount and blockno.  All blocks look the same to make the code
 * simpler; if we have time later, we'll make the optimizations.
 */
#define	XFS_ABTB_MAGIC	0x41425442	/* 'ABTB' for bno tree */
#define	XFS_ABTC_MAGIC	0x41425443	/* 'ABTC' for cnt tree */

/*
 * Data record/key structure
 */
typedef struct xfs_alloc_rec {
	__be32		ar_startblock;	/* starting block number */
	__be32		ar_blockcount;	/* count of free blocks */
} xfs_alloc_rec_t, xfs_alloc_key_t;

typedef struct xfs_alloc_rec_incore {
	xfs_agblock_t	ar_startblock;	/* starting block number */
	xfs_extlen_t	ar_blockcount;	/* count of free blocks */
} xfs_alloc_rec_incore_t;

/* btree pointer type */
typedef __be32 xfs_alloc_ptr_t;
/* btree block header type */
typedef	struct xfs_btree_sblock xfs_alloc_block_t;

#define	XFS_BUF_TO_ALLOC_BLOCK(bp)	((xfs_alloc_block_t *)XFS_BUF_PTR(bp))

/*
 * Real block structures have a size equal to the disk block size.
 */
#define	XFS_ALLOC_BLOCK_MAXRECS(lev,cur) ((cur)->bc_mp->m_alloc_mxr[lev != 0])
#define	XFS_ALLOC_BLOCK_MINRECS(lev,cur) ((cur)->bc_mp->m_alloc_mnr[lev != 0])

/*
 * Minimum and maximum blocksize and sectorsize.
 * The blocksize upper limit is pretty much arbitrary.
 * The sectorsize upper limit is due to sizeof(sb_sectsize).
 */
#define XFS_MIN_BLOCKSIZE_LOG	9	/* i.e. 512 bytes */
#define XFS_MAX_BLOCKSIZE_LOG	16	/* i.e. 65536 bytes */
#define XFS_MIN_BLOCKSIZE	(1 << XFS_MIN_BLOCKSIZE_LOG)
#define XFS_MAX_BLOCKSIZE	(1 << XFS_MAX_BLOCKSIZE_LOG)
#define XFS_MIN_SECTORSIZE_LOG	9	/* i.e. 512 bytes */
#define XFS_MAX_SECTORSIZE_LOG	15	/* i.e. 32768 bytes */
#define XFS_MIN_SECTORSIZE	(1 << XFS_MIN_SECTORSIZE_LOG)
#define XFS_MAX_SECTORSIZE	(1 << XFS_MAX_SECTORSIZE_LOG)

/*
 * Block numbers in the AG:
 * SB is sector 0, AGF is sector 1, AGI is sector 2, AGFL is sector 3.
 */
#define	XFS_BNO_BLOCK(mp)	((xfs_agblock_t)(XFS_AGFL_BLOCK(mp) + 1))
#define	XFS_CNT_BLOCK(mp)	((xfs_agblock_t)(XFS_BNO_BLOCK(mp) + 1))

/*
 * Record, key, and pointer address macros for btree blocks.
 */
#define	XFS_ALLOC_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(xfs_alloc, bb, i)

#define	XFS_ALLOC_KEY_ADDR(bb,i,cur)	\
	XFS_BTREE_KEY_ADDR(xfs_alloc, bb, i)

#define	XFS_ALLOC_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(xfs_alloc, bb, i, XFS_ALLOC_BLOCK_MAXRECS(1, cur))

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
extern int xfs_alloc_decrement(struct xfs_btree_cur *cur, int level, int *stat);

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
extern int xfs_alloc_delete(struct xfs_btree_cur *cur, int *stat);

/*
 * Get the data from the pointed-to record.
 */
extern int xfs_alloc_get_rec(struct xfs_btree_cur *cur,	xfs_agblock_t *bno,
				xfs_extlen_t *len, int *stat);

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
extern int xfs_alloc_increment(struct xfs_btree_cur *cur, int level, int *stat);

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
extern int xfs_alloc_insert(struct xfs_btree_cur *cur, int *stat);

/*
 * Lookup the record equal to [bno, len] in the btree given by cur.
 */
extern int xfs_alloc_lookup_eq(struct xfs_btree_cur *cur, xfs_agblock_t bno,
				xfs_extlen_t len, int *stat);

/*
 * Lookup the first record greater than or equal to [bno, len]
 * in the btree given by cur.
 */
extern int xfs_alloc_lookup_ge(struct xfs_btree_cur *cur, xfs_agblock_t bno,
				xfs_extlen_t len, int *stat);

/*
 * Lookup the first record less than or equal to [bno, len]
 * in the btree given by cur.
 */
extern int xfs_alloc_lookup_le(struct xfs_btree_cur *cur, xfs_agblock_t bno,
				xfs_extlen_t len, int *stat);

/*
 * Update the record referred to by cur, to the value given by [bno, len].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
extern int xfs_alloc_update(struct xfs_btree_cur *cur, xfs_agblock_t bno,
				xfs_extlen_t len);

#endif	/* __XFS_ALLOC_BTREE_H__ */
