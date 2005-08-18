/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
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

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_MASKN)
xfs_inofree_t xfs_inobt_maskn(int i, int n);
#define	XFS_INOBT_MASKN(i,n)		xfs_inobt_maskn(i,n)
#else
#define	XFS_INOBT_MASKN(i,n)	\
	((((n) >= XFS_INODES_PER_CHUNK ? \
		(xfs_inofree_t)0 : ((xfs_inofree_t)1 << (n))) - 1) << (i))
#endif

/*
 * Data record structure
 */
typedef struct xfs_inobt_rec
{
	xfs_agino_t	ir_startino;	/* starting inode number */
	__int32_t	ir_freecount;	/* count of free inodes (set bits) */
	xfs_inofree_t	ir_free;	/* free inode mask */
} xfs_inobt_rec_t;

/*
 * Key structure
 */
typedef struct xfs_inobt_key
{
	xfs_agino_t	ir_startino;	/* starting inode number */
} xfs_inobt_key_t;

typedef xfs_agblock_t xfs_inobt_ptr_t;	/* btree pointer type */
					/* btree block header type */
typedef	struct xfs_btree_sblock xfs_inobt_block_t;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BUF_TO_INOBT_BLOCK)
xfs_inobt_block_t *xfs_buf_to_inobt_block(struct xfs_buf *bp);
#define	XFS_BUF_TO_INOBT_BLOCK(bp)	xfs_buf_to_inobt_block(bp)
#else
#define	XFS_BUF_TO_INOBT_BLOCK(bp) ((xfs_inobt_block_t *)(XFS_BUF_PTR(bp)))
#endif

/*
 * Bit manipulations for ir_free.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_MASK)
xfs_inofree_t xfs_inobt_mask(int i);
#define	XFS_INOBT_MASK(i)		xfs_inobt_mask(i)
#else
#define	XFS_INOBT_MASK(i)		((xfs_inofree_t)1 << (i))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_IS_FREE)
int xfs_inobt_is_free(xfs_inobt_rec_t *rp, int i);
#define	XFS_INOBT_IS_FREE(rp,i)		xfs_inobt_is_free(rp,i)
#define	XFS_INOBT_IS_FREE_DISK(rp,i)	xfs_inobt_is_free_disk(rp,i)
#else
#define	XFS_INOBT_IS_FREE(rp,i)	\
	(((rp)->ir_free & XFS_INOBT_MASK(i)) != 0)
#define XFS_INOBT_IS_FREE_DISK(rp,i) \
	((INT_GET((rp)->ir_free, ARCH_CONVERT) & XFS_INOBT_MASK(i)) != 0)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_SET_FREE)
void xfs_inobt_set_free(xfs_inobt_rec_t *rp, int i);
#define	XFS_INOBT_SET_FREE(rp,i)	xfs_inobt_set_free(rp,i)
#else
#define	XFS_INOBT_SET_FREE(rp,i)	((rp)->ir_free |= XFS_INOBT_MASK(i))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_CLR_FREE)
void xfs_inobt_clr_free(xfs_inobt_rec_t *rp, int i);
#define	XFS_INOBT_CLR_FREE(rp,i)	xfs_inobt_clr_free(rp,i)
#else
#define	XFS_INOBT_CLR_FREE(rp,i)	((rp)->ir_free &= ~XFS_INOBT_MASK(i))
#endif

/*
 * Real block structures have a size equal to the disk block size.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_BLOCK_SIZE)
int xfs_inobt_block_size(int lev, struct xfs_btree_cur *cur);
#define	XFS_INOBT_BLOCK_SIZE(lev,cur)	xfs_inobt_block_size(lev,cur)
#else
#define	XFS_INOBT_BLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_BLOCK_MAXRECS)
int xfs_inobt_block_maxrecs(int lev, struct xfs_btree_cur *cur);
#define	XFS_INOBT_BLOCK_MAXRECS(lev,cur)	xfs_inobt_block_maxrecs(lev,cur)
#else
#define	XFS_INOBT_BLOCK_MAXRECS(lev,cur)	\
	((cur)->bc_mp->m_inobt_mxr[lev != 0])
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_BLOCK_MINRECS)
int xfs_inobt_block_minrecs(int lev, struct xfs_btree_cur *cur);
#define	XFS_INOBT_BLOCK_MINRECS(lev,cur)	xfs_inobt_block_minrecs(lev,cur)
#else
#define	XFS_INOBT_BLOCK_MINRECS(lev,cur)	\
	((cur)->bc_mp->m_inobt_mnr[lev != 0])
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_IS_LAST_REC)
int xfs_inobt_is_last_rec(struct xfs_btree_cur *cur);
#define	XFS_INOBT_IS_LAST_REC(cur)	xfs_inobt_is_last_rec(cur)
#else
#define	XFS_INOBT_IS_LAST_REC(cur)	\
	((cur)->bc_ptrs[0] == \
		INT_GET(XFS_BUF_TO_INOBT_BLOCK((cur)->bc_bufs[0])->bb_numrecs, ARCH_CONVERT))
#endif

/*
 * Maximum number of inode btree levels.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IN_MAXLEVELS)
int xfs_in_maxlevels(struct xfs_mount *mp);
#define	XFS_IN_MAXLEVELS(mp)		xfs_in_maxlevels(mp)
#else
#define	XFS_IN_MAXLEVELS(mp)		((mp)->m_in_maxlevels)
#endif

/*
 * block numbers in the AG.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IBT_BLOCK)
xfs_agblock_t xfs_ibt_block(struct xfs_mount *mp);
#define	XFS_IBT_BLOCK(mp)		xfs_ibt_block(mp)
#else
#define	XFS_IBT_BLOCK(mp)	((xfs_agblock_t)(XFS_CNT_BLOCK(mp) + 1))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_PREALLOC_BLOCKS)
xfs_agblock_t xfs_prealloc_blocks(struct xfs_mount *mp);
#define	XFS_PREALLOC_BLOCKS(mp)		xfs_prealloc_blocks(mp)
#else
#define	XFS_PREALLOC_BLOCKS(mp)	((xfs_agblock_t)(XFS_IBT_BLOCK(mp) + 1))
#endif

/*
 * Record, key, and pointer address macros for btree blocks.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_REC_ADDR)
xfs_inobt_rec_t *
xfs_inobt_rec_addr(xfs_inobt_block_t *bb, int i, struct xfs_btree_cur *cur);
#define	XFS_INOBT_REC_ADDR(bb,i,cur)	xfs_inobt_rec_addr(bb,i,cur)
#else
#define	XFS_INOBT_REC_ADDR(bb,i,cur)	\
	XFS_BTREE_REC_ADDR(XFS_INOBT_BLOCK_SIZE(0,cur), xfs_inobt, bb, i, \
		XFS_INOBT_BLOCK_MAXRECS(0, cur))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_KEY_ADDR)
xfs_inobt_key_t *
xfs_inobt_key_addr(xfs_inobt_block_t *bb, int i, struct xfs_btree_cur *cur);
#define	XFS_INOBT_KEY_ADDR(bb,i,cur)	xfs_inobt_key_addr(bb,i,cur)
#else
#define	XFS_INOBT_KEY_ADDR(bb,i,cur)	\
	XFS_BTREE_KEY_ADDR(XFS_INOBT_BLOCK_SIZE(1,cur), xfs_inobt, bb, i, \
		XFS_INOBT_BLOCK_MAXRECS(1, cur))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_INOBT_PTR_ADDR)
xfs_inobt_ptr_t *
xfs_inobt_ptr_addr(xfs_inobt_block_t *bb, int i, struct xfs_btree_cur *cur);
#define	XFS_INOBT_PTR_ADDR(bb,i,cur)	xfs_inobt_ptr_addr(bb,i,cur)
#else
#define	XFS_INOBT_PTR_ADDR(bb,i,cur)	\
	XFS_BTREE_PTR_ADDR(XFS_INOBT_BLOCK_SIZE(1,cur), xfs_inobt, bb, i, \
		XFS_INOBT_BLOCK_MAXRECS(1, cur))
#endif

/*
 * Prototypes for externally visible routines.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* error */
xfs_inobt_decrement(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level,	/* level in btree, 0 is leaf */
	int			*stat); /* success/failure */

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* error */
xfs_inobt_delete(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			*stat);	/* success/failure */

/*
 * Get the data from the pointed-to record.
 */
int					/* error */
xfs_inobt_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		*ino,	/* output: starting inode of chunk */
	__int32_t		*fcnt,	/* output: number of free inodes */
	xfs_inofree_t		*free,	/* output: free inode mask */
	int			*stat);	/* output: success/failure */

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* error */
xfs_inobt_increment(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level,	/* level in btree, 0 is leaf */
	int			*stat);	/* success/failure */

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
int					/* error */
xfs_inobt_insert(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			*stat);	/* success/failure */

/*
 * Lookup the record equal to ino in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup_eq(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free,	/* free inode mask */
	int			*stat);	/* success/failure */

/*
 * Lookup the first record greater than or equal to ino
 * in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup_ge(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free,	/* free inode mask */
	int			*stat);	/* success/failure */

/*
 * Lookup the first record less than or equal to ino
 * in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup_le(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free,	/* free inode mask */
	int			*stat);	/* success/failure */

/*
 * Update the record referred to by cur, to the value given
 * by [ino, fcnt, free].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
int					/* error */
xfs_inobt_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free);	/* free inode mask */

#endif	/* __XFS_IALLOC_BTREE_H__ */
