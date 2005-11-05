/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_BTREE_H__
#define	__XFS_BTREE_H__

struct xfs_buf;
struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * This nonsense is to make -wlint happy.
 */
#define	XFS_LOOKUP_EQ	((xfs_lookup_t)XFS_LOOKUP_EQi)
#define	XFS_LOOKUP_LE	((xfs_lookup_t)XFS_LOOKUP_LEi)
#define	XFS_LOOKUP_GE	((xfs_lookup_t)XFS_LOOKUP_GEi)

#define	XFS_BTNUM_BNO	((xfs_btnum_t)XFS_BTNUM_BNOi)
#define	XFS_BTNUM_CNT	((xfs_btnum_t)XFS_BTNUM_CNTi)
#define	XFS_BTNUM_BMAP	((xfs_btnum_t)XFS_BTNUM_BMAPi)
#define	XFS_BTNUM_INO	((xfs_btnum_t)XFS_BTNUM_INOi)

/*
 * Short form header: space allocation btrees.
 */
typedef struct xfs_btree_sblock {
	__be32		bb_magic;	/* magic number for block type */
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
	__be32		bb_leftsib;	/* left sibling block or NULLAGBLOCK */
	__be32		bb_rightsib;	/* right sibling block or NULLAGBLOCK */
} xfs_btree_sblock_t;

/*
 * Long form header: bmap btrees.
 */
typedef struct xfs_btree_lblock {
	__be32		bb_magic;	/* magic number for block type */
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
	__be64		bb_leftsib;	/* left sibling block or NULLDFSBNO */
	__be64		bb_rightsib;	/* right sibling block or NULLDFSBNO */
} xfs_btree_lblock_t;

/*
 * Combined header and structure, used by common code.
 */
typedef struct xfs_btree_hdr
{
	__be32		bb_magic;	/* magic number for block type */
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
} xfs_btree_hdr_t;

typedef struct xfs_btree_block {
	xfs_btree_hdr_t	bb_h;		/* header */
	union {
		struct {
			__be32		bb_leftsib;
			__be32		bb_rightsib;
		} s;			/* short form pointers */
		struct	{
			__be64		bb_leftsib;
			__be64		bb_rightsib;
		} l;			/* long form pointers */
	} bb_u;				/* rest */
} xfs_btree_block_t;

/*
 * For logging record fields.
 */
#define	XFS_BB_MAGIC		0x01
#define	XFS_BB_LEVEL		0x02
#define	XFS_BB_NUMRECS		0x04
#define	XFS_BB_LEFTSIB		0x08
#define	XFS_BB_RIGHTSIB		0x10
#define	XFS_BB_NUM_BITS		5
#define	XFS_BB_ALL_BITS		((1 << XFS_BB_NUM_BITS) - 1)

/*
 * Boolean to select which form of xfs_btree_block_t.bb_u to use.
 */
#define	XFS_BTREE_LONG_PTRS(btnum)	((btnum) == XFS_BTNUM_BMAP)

/*
 * Magic numbers for btree blocks.
 */
extern const __uint32_t	xfs_magics[];

/*
 * Maximum and minimum records in a btree block.
 * Given block size, type prefix, and leaf flag (0 or 1).
 * The divisor below is equivalent to lf ? (e1) : (e2) but that produces
 * compiler warnings.
 */
#define	XFS_BTREE_BLOCK_MAXRECS(bsz,t,lf)	\
	((int)(((bsz) - (uint)sizeof(t ## _block_t)) / \
	 (((lf) * (uint)sizeof(t ## _rec_t)) + \
	  ((1 - (lf)) * \
	   ((uint)sizeof(t ## _key_t) + (uint)sizeof(t ## _ptr_t))))))
#define	XFS_BTREE_BLOCK_MINRECS(bsz,t,lf)	\
	(XFS_BTREE_BLOCK_MAXRECS(bsz,t,lf) / 2)

/*
 * Record, key, and pointer address calculation macros.
 * Given block size, type prefix, block pointer, and index of requested entry
 * (first entry numbered 1).
 */
#define	XFS_BTREE_REC_ADDR(bsz,t,bb,i,mxr)	\
	((t ## _rec_t *)((char *)(bb) + sizeof(t ## _block_t) + \
	 ((i) - 1) * sizeof(t ## _rec_t)))
#define	XFS_BTREE_KEY_ADDR(bsz,t,bb,i,mxr)	\
	((t ## _key_t *)((char *)(bb) + sizeof(t ## _block_t) + \
	 ((i) - 1) * sizeof(t ## _key_t)))
#define	XFS_BTREE_PTR_ADDR(bsz,t,bb,i,mxr)	\
	((t ## _ptr_t *)((char *)(bb) + sizeof(t ## _block_t) + \
	 (mxr) * sizeof(t ## _key_t) + ((i) - 1) * sizeof(t ## _ptr_t)))

#define	XFS_BTREE_MAXLEVELS	8	/* max of all btrees */

/*
 * Btree cursor structure.
 * This collects all information needed by the btree code in one place.
 */
typedef struct xfs_btree_cur
{
	struct xfs_trans	*bc_tp;	/* transaction we're in, if any */
	struct xfs_mount	*bc_mp;	/* file system mount struct */
	union {
		xfs_alloc_rec_incore_t	a;
		xfs_bmbt_irec_t		b;
		xfs_inobt_rec_t		i;
	}		bc_rec;		/* current insert/search record value */
	struct xfs_buf	*bc_bufs[XFS_BTREE_MAXLEVELS];	/* buf ptr per level */
	int		bc_ptrs[XFS_BTREE_MAXLEVELS];	/* key/record # */
	__uint8_t	bc_ra[XFS_BTREE_MAXLEVELS];	/* readahead bits */
#define	XFS_BTCUR_LEFTRA	1	/* left sibling has been read-ahead */
#define	XFS_BTCUR_RIGHTRA	2	/* right sibling has been read-ahead */
	__uint8_t	bc_nlevels;	/* number of levels in the tree */
	__uint8_t	bc_blocklog;	/* log2(blocksize) of btree blocks */
	xfs_btnum_t	bc_btnum;	/* identifies which btree type */
	union {
		struct {			/* needed for BNO, CNT */
			struct xfs_buf	*agbp;	/* agf buffer pointer */
			xfs_agnumber_t	agno;	/* ag number */
		} a;
		struct {			/* needed for BMAP */
			struct xfs_inode *ip;	/* pointer to our inode */
			struct xfs_bmap_free *flist;	/* list to free after */
			xfs_fsblock_t	firstblock;	/* 1st blk allocated */
			int		allocated;	/* count of alloced */
			short		forksize;	/* fork's inode space */
			char		whichfork;	/* data or attr fork */
			char		flags;		/* flags */
#define	XFS_BTCUR_BPRV_WASDEL	1			/* was delayed */
		} b;
		struct {			/* needed for INO */
			struct xfs_buf	*agbp;	/* agi buffer pointer */
			xfs_agnumber_t	agno;	/* ag number */
		} i;
	}		bc_private;	/* per-btree type data */
} xfs_btree_cur_t;

#define	XFS_BTREE_NOERROR	0
#define	XFS_BTREE_ERROR		1

/*
 * Convert from buffer to btree block header.
 */
#define	XFS_BUF_TO_BLOCK(bp)	((xfs_btree_block_t *)XFS_BUF_PTR(bp))
#define	XFS_BUF_TO_LBLOCK(bp)	((xfs_btree_lblock_t *)XFS_BUF_PTR(bp))
#define	XFS_BUF_TO_SBLOCK(bp)	((xfs_btree_sblock_t *)XFS_BUF_PTR(bp))


#ifdef __KERNEL__

#ifdef DEBUG
/*
 * Debug routine: check that block header is ok.
 */
void
xfs_btree_check_block(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_block_t	*block,	/* generic btree block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp);	/* buffer containing block, if any */

/*
 * Debug routine: check that keys are in the right order.
 */
void
xfs_btree_check_key(
	xfs_btnum_t		btnum,	/* btree identifier */
	void			*ak1,	/* pointer to left (lower) key */
	void			*ak2);	/* pointer to right (higher) key */

/*
 * Debug routine: check that records are in the right order.
 */
void
xfs_btree_check_rec(
	xfs_btnum_t		btnum,	/* btree identifier */
	void			*ar1,	/* pointer to left (lower) record */
	void			*ar2);	/* pointer to right (higher) record */
#else
#define	xfs_btree_check_block(a,b,c,d)
#define	xfs_btree_check_key(a,b,c)
#define	xfs_btree_check_rec(a,b,c)
#endif	/* DEBUG */

/*
 * Checking routine: check that long form block header is ok.
 */
int					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_lblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_lblock_t	*block,	/* btree long form block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp);	/* buffer containing block, if any */

/*
 * Checking routine: check that (long) pointer is ok.
 */
int					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_lptr(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_dfsbno_t		ptr,	/* btree block disk address */
	int			level);	/* btree block level */

/*
 * Checking routine: check that short form block header is ok.
 */
int					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_sblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_btree_sblock_t	*block,	/* btree short form block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp);	/* buffer containing block */

/*
 * Checking routine: check that (short) pointer is ok.
 */
int					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_sptr(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agblock_t		ptr,	/* btree block disk address */
	int			level);	/* btree block level */

/*
 * Delete the btree cursor.
 */
void
xfs_btree_del_cursor(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			error);	/* del because of error */

/*
 * Duplicate the btree cursor.
 * Allocate a new one, copy the record, re-get the buffers.
 */
int					/* error */
xfs_btree_dup_cursor(
	xfs_btree_cur_t		*cur,	/* input cursor */
	xfs_btree_cur_t		**ncur);/* output cursor */

/*
 * Change the cursor to point to the first record in the current block
 * at the given level.  Other levels are unaffected.
 */
int					/* success=1, failure=0 */
xfs_btree_firstrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to change */

/*
 * Get a buffer for the block, return it with no data read.
 * Long-form addressing.
 */
struct xfs_buf *				/* buffer for fsbno */
xfs_btree_get_bufl(
	struct xfs_mount	*mp,	/* file system mount point */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_fsblock_t		fsbno,	/* file system block number */
	uint			lock);	/* lock flags for get_buf */

/*
 * Get a buffer for the block, return it with no data read.
 * Short-form addressing.
 */
struct xfs_buf *				/* buffer for agno/agbno */
xfs_btree_get_bufs(
	struct xfs_mount	*mp,	/* file system mount point */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		agbno,	/* allocation group block number */
	uint			lock);	/* lock flags for get_buf */

/*
 * Allocate a new btree cursor.
 * The cursor is either for allocation (A) or bmap (B).
 */
xfs_btree_cur_t *			/* new btree cursor */
xfs_btree_init_cursor(
	struct xfs_mount	*mp,	/* file system mount point */
	struct xfs_trans	*tp,	/* transaction pointer */
	struct xfs_buf		*agbp,	/* (A only) buffer for agf structure */
	xfs_agnumber_t		agno,	/* (A only) allocation group number */
	xfs_btnum_t		btnum,	/* btree identifier */
	struct xfs_inode	*ip,	/* (B only) inode owning the btree */
	int			whichfork); /* (B only) data/attr fork */

/*
 * Check for the cursor referring to the last block at the given level.
 */
int					/* 1=is last block, 0=not last block */
xfs_btree_islastblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to check */

/*
 * Change the cursor to point to the last record in the current block
 * at the given level.  Other levels are unaffected.
 */
int					/* success=1, failure=0 */
xfs_btree_lastrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to change */

/*
 * Compute first and last byte offsets for the fields given.
 * Interprets the offsets table, which contains struct field offsets.
 */
void
xfs_btree_offsets(
	__int64_t		fields,	/* bitmask of fields */
	const short		*offsets,/* table of field offsets */
	int			nbits,	/* number of bits to inspect */
	int			*first,	/* output: first byte offset */
	int			*last);	/* output: last byte offset */

/*
 * Get a buffer for the block, return it read in.
 * Long-form addressing.
 */
int					/* error */
xfs_btree_read_bufl(
	struct xfs_mount	*mp,	/* file system mount point */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_fsblock_t		fsbno,	/* file system block number */
	uint			lock,	/* lock flags for read_buf */
	struct xfs_buf		**bpp,	/* buffer for fsbno */
	int			refval);/* ref count value for buffer */

/*
 * Get a buffer for the block, return it read in.
 * Short-form addressing.
 */
int					/* error */
xfs_btree_read_bufs(
	struct xfs_mount	*mp,	/* file system mount point */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		agbno,	/* allocation group block number */
	uint			lock,	/* lock flags for read_buf */
	struct xfs_buf		**bpp,	/* buffer for agno/agbno */
	int			refval);/* ref count value for buffer */

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Long-form addressing.
 */
void					/* error */
xfs_btree_reada_bufl(
	struct xfs_mount	*mp,	/* file system mount point */
	xfs_fsblock_t		fsbno,	/* file system block number */
	xfs_extlen_t		count);	/* count of filesystem blocks */

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Short-form addressing.
 */
void					/* error */
xfs_btree_reada_bufs(
	struct xfs_mount	*mp,	/* file system mount point */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		agbno,	/* allocation group block number */
	xfs_extlen_t		count);	/* count of filesystem blocks */

/*
 * Read-ahead btree blocks, at the given level.
 * Bits in lr are set from XFS_BTCUR_{LEFT,RIGHT}RA.
 */
int					/* readahead block count */
xfs_btree_readahead_core(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	int			lr);	/* left/right bits */

static inline int			/* readahead block count */
xfs_btree_readahead(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	int			lr)	/* left/right bits */
{
	if ((cur->bc_ra[lev] | lr) == cur->bc_ra[lev])
		return 0;

	return xfs_btree_readahead_core(cur, lev, lr);
}


/*
 * Set the buffer for level "lev" in the cursor to bp, releasing
 * any previous buffer.
 */
void
xfs_btree_setbuf(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	struct xfs_buf		*bp);	/* new buffer to set */

#endif	/* __KERNEL__ */


/*
 * Min and max functions for extlen, agblock, fileoff, and filblks types.
 */
#define	XFS_EXTLEN_MIN(a,b)	\
	((xfs_extlen_t)(a) < (xfs_extlen_t)(b) ? \
		(xfs_extlen_t)(a) : (xfs_extlen_t)(b))
#define	XFS_EXTLEN_MAX(a,b)	\
	((xfs_extlen_t)(a) > (xfs_extlen_t)(b) ? \
		(xfs_extlen_t)(a) : (xfs_extlen_t)(b))
#define	XFS_AGBLOCK_MIN(a,b)	\
	((xfs_agblock_t)(a) < (xfs_agblock_t)(b) ? \
		(xfs_agblock_t)(a) : (xfs_agblock_t)(b))
#define	XFS_AGBLOCK_MAX(a,b)	\
	((xfs_agblock_t)(a) > (xfs_agblock_t)(b) ? \
		(xfs_agblock_t)(a) : (xfs_agblock_t)(b))
#define	XFS_FILEOFF_MIN(a,b)	\
	((xfs_fileoff_t)(a) < (xfs_fileoff_t)(b) ? \
		(xfs_fileoff_t)(a) : (xfs_fileoff_t)(b))
#define	XFS_FILEOFF_MAX(a,b)	\
	((xfs_fileoff_t)(a) > (xfs_fileoff_t)(b) ? \
		(xfs_fileoff_t)(a) : (xfs_fileoff_t)(b))
#define	XFS_FILBLKS_MIN(a,b)	\
	((xfs_filblks_t)(a) < (xfs_filblks_t)(b) ? \
		(xfs_filblks_t)(a) : (xfs_filblks_t)(b))
#define	XFS_FILBLKS_MAX(a,b)	\
	((xfs_filblks_t)(a) > (xfs_filblks_t)(b) ? \
		(xfs_filblks_t)(a) : (xfs_filblks_t)(b))

#define	XFS_FSB_SANITY_CHECK(mp,fsb)	\
	(XFS_FSB_TO_AGNO(mp, fsb) < mp->m_sb.sb_agcount && \
		XFS_FSB_TO_AGBNO(mp, fsb) < mp->m_sb.sb_agblocks)

#endif	/* __XFS_BTREE_H__ */
