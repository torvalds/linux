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

extern kmem_zone_t	*xfs_btree_cur_zone;

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
 * Generic btree header.
 *
 * This is a combination of the actual format used on disk for short and long
 * format btrees.  The first three fields are shared by both format, but the
 * pointers are different and should be used with care.
 *
 * To get the size of the actual short or long form headers please use the size
 * macros below.  Never use sizeof(xfs_btree_block).
 *
 * The blkno, crc, lsn, owner and uuid fields are only available in filesystems
 * with the crc feature bit, and all accesses to them must be conditional on
 * that flag.
 */
struct xfs_btree_block {
	__be32		bb_magic;	/* magic number for block type */
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
	union {
		struct {
			__be32		bb_leftsib;
			__be32		bb_rightsib;

			__be64		bb_blkno;
			__be64		bb_lsn;
			uuid_t		bb_uuid;
			__be32		bb_owner;
			__le32		bb_crc;
		} s;			/* short form pointers */
		struct	{
			__be64		bb_leftsib;
			__be64		bb_rightsib;

			__be64		bb_blkno;
			__be64		bb_lsn;
			uuid_t		bb_uuid;
			__be64		bb_owner;
			__le32		bb_crc;
			__be32		bb_pad; /* padding for alignment */
		} l;			/* long form pointers */
	} bb_u;				/* rest */
};

#define XFS_BTREE_SBLOCK_LEN	16	/* size of a short form block */
#define XFS_BTREE_LBLOCK_LEN	24	/* size of a long form block */

/* sizes of CRC enabled btree blocks */
#define XFS_BTREE_SBLOCK_CRC_LEN	(XFS_BTREE_SBLOCK_LEN + 40)
#define XFS_BTREE_LBLOCK_CRC_LEN	(XFS_BTREE_LBLOCK_LEN + 48)

#define XFS_BTREE_SBLOCK_CRC_OFF \
	offsetof(struct xfs_btree_block, bb_u.s.bb_crc)
#define XFS_BTREE_LBLOCK_CRC_OFF \
	offsetof(struct xfs_btree_block, bb_u.l.bb_crc)

/*
 * Generic key, ptr and record wrapper structures.
 *
 * These are disk format structures, and are converted where necessary
 * by the btree specific code that needs to interpret them.
 */
union xfs_btree_ptr {
	__be32			s;	/* short form ptr */
	__be64			l;	/* long form ptr */
};

union xfs_btree_key {
	xfs_bmbt_key_t		bmbt;
	xfs_bmdr_key_t		bmbr;	/* bmbt root block */
	xfs_alloc_key_t		alloc;
	xfs_inobt_key_t		inobt;
};

union xfs_btree_rec {
	xfs_bmbt_rec_t		bmbt;
	xfs_bmdr_rec_t		bmbr;	/* bmbt root block */
	xfs_alloc_rec_t		alloc;
	xfs_inobt_rec_t		inobt;
};

/*
 * For logging record fields.
 */
#define	XFS_BB_MAGIC		(1 << 0)
#define	XFS_BB_LEVEL		(1 << 1)
#define	XFS_BB_NUMRECS		(1 << 2)
#define	XFS_BB_LEFTSIB		(1 << 3)
#define	XFS_BB_RIGHTSIB		(1 << 4)
#define	XFS_BB_BLKNO		(1 << 5)
#define	XFS_BB_LSN		(1 << 6)
#define	XFS_BB_UUID		(1 << 7)
#define	XFS_BB_OWNER		(1 << 8)
#define	XFS_BB_NUM_BITS		5
#define	XFS_BB_ALL_BITS		((1 << XFS_BB_NUM_BITS) - 1)
#define	XFS_BB_NUM_BITS_CRC	9
#define	XFS_BB_ALL_BITS_CRC	((1 << XFS_BB_NUM_BITS_CRC) - 1)

/*
 * Generic stats interface
 */
#define __XFS_BTREE_STATS_INC(type, stat) \
	XFS_STATS_INC(xs_ ## type ## _2_ ## stat)
#define XFS_BTREE_STATS_INC(cur, stat)  \
do {    \
	switch (cur->bc_btnum) {  \
	case XFS_BTNUM_BNO: __XFS_BTREE_STATS_INC(abtb, stat); break;	\
	case XFS_BTNUM_CNT: __XFS_BTREE_STATS_INC(abtc, stat); break;	\
	case XFS_BTNUM_BMAP: __XFS_BTREE_STATS_INC(bmbt, stat); break;	\
	case XFS_BTNUM_INO: __XFS_BTREE_STATS_INC(ibt, stat); break;	\
	case XFS_BTNUM_MAX: ASSERT(0); /* fucking gcc */ ; break;	\
	}       \
} while (0)

#define __XFS_BTREE_STATS_ADD(type, stat, val) \
	XFS_STATS_ADD(xs_ ## type ## _2_ ## stat, val)
#define XFS_BTREE_STATS_ADD(cur, stat, val)  \
do {    \
	switch (cur->bc_btnum) {  \
	case XFS_BTNUM_BNO: __XFS_BTREE_STATS_ADD(abtb, stat, val); break; \
	case XFS_BTNUM_CNT: __XFS_BTREE_STATS_ADD(abtc, stat, val); break; \
	case XFS_BTNUM_BMAP: __XFS_BTREE_STATS_ADD(bmbt, stat, val); break; \
	case XFS_BTNUM_INO: __XFS_BTREE_STATS_ADD(ibt, stat, val); break; \
	case XFS_BTNUM_MAX: ASSERT(0); /* fucking gcc */ ; break;	\
	}       \
} while (0)

#define	XFS_BTREE_MAXLEVELS	8	/* max of all btrees */

struct xfs_btree_ops {
	/* size of the key and record structures */
	size_t	key_len;
	size_t	rec_len;

	/* cursor operations */
	struct xfs_btree_cur *(*dup_cursor)(struct xfs_btree_cur *);
	void	(*update_cursor)(struct xfs_btree_cur *src,
				 struct xfs_btree_cur *dst);

	/* update btree root pointer */
	void	(*set_root)(struct xfs_btree_cur *cur,
			    union xfs_btree_ptr *nptr, int level_change);

	/* block allocation / freeing */
	int	(*alloc_block)(struct xfs_btree_cur *cur,
			       union xfs_btree_ptr *start_bno,
			       union xfs_btree_ptr *new_bno,
			       int length, int *stat);
	int	(*free_block)(struct xfs_btree_cur *cur, struct xfs_buf *bp);

	/* update last record information */
	void	(*update_lastrec)(struct xfs_btree_cur *cur,
				  struct xfs_btree_block *block,
				  union xfs_btree_rec *rec,
				  int ptr, int reason);

	/* records in block/level */
	int	(*get_minrecs)(struct xfs_btree_cur *cur, int level);
	int	(*get_maxrecs)(struct xfs_btree_cur *cur, int level);

	/* records on disk.  Matter for the root in inode case. */
	int	(*get_dmaxrecs)(struct xfs_btree_cur *cur, int level);

	/* init values of btree structures */
	void	(*init_key_from_rec)(union xfs_btree_key *key,
				     union xfs_btree_rec *rec);
	void	(*init_rec_from_key)(union xfs_btree_key *key,
				     union xfs_btree_rec *rec);
	void	(*init_rec_from_cur)(struct xfs_btree_cur *cur,
				     union xfs_btree_rec *rec);
	void	(*init_ptr_from_cur)(struct xfs_btree_cur *cur,
				     union xfs_btree_ptr *ptr);

	/* difference between key value and cursor value */
	__int64_t (*key_diff)(struct xfs_btree_cur *cur,
			      union xfs_btree_key *key);

	const struct xfs_buf_ops	*buf_ops;

#if defined(DEBUG) || defined(XFS_WARN)
	/* check that k1 is lower than k2 */
	int	(*keys_inorder)(struct xfs_btree_cur *cur,
				union xfs_btree_key *k1,
				union xfs_btree_key *k2);

	/* check that r1 is lower than r2 */
	int	(*recs_inorder)(struct xfs_btree_cur *cur,
				union xfs_btree_rec *r1,
				union xfs_btree_rec *r2);
#endif
};

/*
 * Reasons for the update_lastrec method to be called.
 */
#define LASTREC_UPDATE	0
#define LASTREC_INSREC	1
#define LASTREC_DELREC	2


/*
 * Btree cursor structure.
 * This collects all information needed by the btree code in one place.
 */
typedef struct xfs_btree_cur
{
	struct xfs_trans	*bc_tp;	/* transaction we're in, if any */
	struct xfs_mount	*bc_mp;	/* file system mount struct */
	const struct xfs_btree_ops *bc_ops;
	uint			bc_flags; /* btree features - below */
	union {
		xfs_alloc_rec_incore_t	a;
		xfs_bmbt_irec_t		b;
		xfs_inobt_rec_incore_t	i;
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
		struct {			/* needed for BNO, CNT, INO */
			struct xfs_buf	*agbp;	/* agf/agi buffer pointer */
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
	}		bc_private;	/* per-btree type data */
} xfs_btree_cur_t;

/* cursor flags */
#define XFS_BTREE_LONG_PTRS		(1<<0)	/* pointers are 64bits long */
#define XFS_BTREE_ROOT_IN_INODE		(1<<1)	/* root may be variable size */
#define XFS_BTREE_LASTREC_UPDATE	(1<<2)	/* track last rec externally */
#define XFS_BTREE_CRC_BLOCKS		(1<<3)	/* uses extended btree blocks */


#define	XFS_BTREE_NOERROR	0
#define	XFS_BTREE_ERROR		1

/*
 * Convert from buffer to btree block header.
 */
#define	XFS_BUF_TO_BLOCK(bp)	((struct xfs_btree_block *)((bp)->b_addr))


/*
 * Check that block header is ok.
 */
int
xfs_btree_check_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_btree_block	*block,	/* generic btree block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp);	/* buffer containing block, if any */

/*
 * Check that (long) pointer is ok.
 */
int					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_lptr(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_dfsbno_t		ptr,	/* btree block disk address */
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
 * Check for the cursor referring to the last block at the given level.
 */
int					/* 1=is last block, 0=not last block */
xfs_btree_islastblock(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level);	/* level to check */

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
	int			refval,	/* ref count value for buffer */
	const struct xfs_buf_ops *ops);

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Long-form addressing.
 */
void					/* error */
xfs_btree_reada_bufl(
	struct xfs_mount	*mp,	/* file system mount point */
	xfs_fsblock_t		fsbno,	/* file system block number */
	xfs_extlen_t		count,	/* count of filesystem blocks */
	const struct xfs_buf_ops *ops);

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Short-form addressing.
 */
void					/* error */
xfs_btree_reada_bufs(
	struct xfs_mount	*mp,	/* file system mount point */
	xfs_agnumber_t		agno,	/* allocation group number */
	xfs_agblock_t		agbno,	/* allocation group block number */
	xfs_extlen_t		count,	/* count of filesystem blocks */
	const struct xfs_buf_ops *ops);

/*
 * Initialise a new btree block header
 */
void
xfs_btree_init_block(
	struct xfs_mount *mp,
	struct xfs_buf	*bp,
	__u32		magic,
	__u16		level,
	__u16		numrecs,
	__u64		owner,
	unsigned int	flags);

void
xfs_btree_init_block_int(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*buf,
	xfs_daddr_t		blkno,
	__u32			magic,
	__u16			level,
	__u16			numrecs,
	__u64			owner,
	unsigned int		flags);

/*
 * Common btree core entry points.
 */
int xfs_btree_increment(struct xfs_btree_cur *, int, int *);
int xfs_btree_decrement(struct xfs_btree_cur *, int, int *);
int xfs_btree_lookup(struct xfs_btree_cur *, xfs_lookup_t, int *);
int xfs_btree_update(struct xfs_btree_cur *, union xfs_btree_rec *);
int xfs_btree_new_iroot(struct xfs_btree_cur *, int *, int *);
int xfs_btree_insert(struct xfs_btree_cur *, int *);
int xfs_btree_delete(struct xfs_btree_cur *, int *);
int xfs_btree_get_rec(struct xfs_btree_cur *, union xfs_btree_rec **, int *);
int xfs_btree_change_owner(struct xfs_btree_cur *cur, __uint64_t new_owner);

/*
 * btree block CRC helpers
 */
void xfs_btree_lblock_calc_crc(struct xfs_buf *);
bool xfs_btree_lblock_verify_crc(struct xfs_buf *);
void xfs_btree_sblock_calc_crc(struct xfs_buf *);
bool xfs_btree_sblock_verify_crc(struct xfs_buf *);

/*
 * Internal btree helpers also used by xfs_bmap.c.
 */
void xfs_btree_log_block(struct xfs_btree_cur *, struct xfs_buf *, int);
void xfs_btree_log_recs(struct xfs_btree_cur *, struct xfs_buf *, int, int);

/*
 * Helpers.
 */
static inline int xfs_btree_get_numrecs(struct xfs_btree_block *block)
{
	return be16_to_cpu(block->bb_numrecs);
}

static inline void xfs_btree_set_numrecs(struct xfs_btree_block *block,
		__uint16_t numrecs)
{
	block->bb_numrecs = cpu_to_be16(numrecs);
}

static inline int xfs_btree_get_level(struct xfs_btree_block *block)
{
	return be16_to_cpu(block->bb_level);
}


/*
 * Min and max functions for extlen, agblock, fileoff, and filblks types.
 */
#define	XFS_EXTLEN_MIN(a,b)	min_t(xfs_extlen_t, (a), (b))
#define	XFS_EXTLEN_MAX(a,b)	max_t(xfs_extlen_t, (a), (b))
#define	XFS_AGBLOCK_MIN(a,b)	min_t(xfs_agblock_t, (a), (b))
#define	XFS_AGBLOCK_MAX(a,b)	max_t(xfs_agblock_t, (a), (b))
#define	XFS_FILEOFF_MIN(a,b)	min_t(xfs_fileoff_t, (a), (b))
#define	XFS_FILEOFF_MAX(a,b)	max_t(xfs_fileoff_t, (a), (b))
#define	XFS_FILBLKS_MIN(a,b)	min_t(xfs_filblks_t, (a), (b))
#define	XFS_FILBLKS_MAX(a,b)	max_t(xfs_filblks_t, (a), (b))

#define	XFS_FSB_SANITY_CHECK(mp,fsb)	\
	(XFS_FSB_TO_AGNO(mp, fsb) < mp->m_sb.sb_agcount && \
		XFS_FSB_TO_AGBNO(mp, fsb) < mp->m_sb.sb_agblocks)

/*
 * Trace hooks.  Currently not implemented as they need to be ported
 * over to the generic tracing functionality, which is some effort.
 *
 * i,j = integer (32 bit)
 * b = btree block buffer (xfs_buf_t)
 * p = btree ptr
 * r = btree record
 * k = btree key
 */
#define	XFS_BTREE_TRACE_ARGBI(c, b, i)
#define	XFS_BTREE_TRACE_ARGBII(c, b, i, j)
#define	XFS_BTREE_TRACE_ARGI(c, i)
#define	XFS_BTREE_TRACE_ARGIPK(c, i, p, s)
#define	XFS_BTREE_TRACE_ARGIPR(c, i, p, r)
#define	XFS_BTREE_TRACE_ARGIK(c, i, k)
#define XFS_BTREE_TRACE_ARGR(c, r)
#define	XFS_BTREE_TRACE_CURSOR(c, t)

#endif	/* __XFS_BTREE_H__ */
