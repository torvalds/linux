// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_BTREE_H__
#define	__XFS_BTREE_H__

struct xfs_buf;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct xfs_ifork;

extern kmem_zone_t	*xfs_btree_cur_zone;

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

/*
 * The in-core btree key.  Overlapping btrees actually store two keys
 * per pointer, so we reserve enough memory to hold both.  The __*bigkey
 * items should never be accessed directly.
 */
union xfs_btree_key {
	struct xfs_bmbt_key		bmbt;
	xfs_bmdr_key_t			bmbr;	/* bmbt root block */
	xfs_alloc_key_t			alloc;
	struct xfs_inobt_key		inobt;
	struct xfs_rmap_key		rmap;
	struct xfs_rmap_key		__rmap_bigkey[2];
	struct xfs_refcount_key		refc;
};

union xfs_btree_rec {
	struct xfs_bmbt_rec		bmbt;
	xfs_bmdr_rec_t			bmbr;	/* bmbt root block */
	struct xfs_alloc_rec		alloc;
	struct xfs_inobt_rec		inobt;
	struct xfs_rmap_rec		rmap;
	struct xfs_refcount_rec		refc;
};

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
#define	XFS_BTNUM_FINO	((xfs_btnum_t)XFS_BTNUM_FINOi)
#define	XFS_BTNUM_RMAP	((xfs_btnum_t)XFS_BTNUM_RMAPi)
#define	XFS_BTNUM_REFC	((xfs_btnum_t)XFS_BTNUM_REFCi)

uint32_t xfs_btree_magic(int crc, xfs_btnum_t btnum);

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
#define XFS_BTREE_STATS_INC(cur, stat)	\
	XFS_STATS_INC_OFF((cur)->bc_mp, (cur)->bc_statoff + __XBTS_ ## stat)
#define XFS_BTREE_STATS_ADD(cur, stat, val)	\
	XFS_STATS_ADD_OFF((cur)->bc_mp, (cur)->bc_statoff + __XBTS_ ## stat, val)

#define	XFS_BTREE_MAXLEVELS	9	/* max of all btrees */

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
			       int *stat);
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
	void	(*init_rec_from_cur)(struct xfs_btree_cur *cur,
				     union xfs_btree_rec *rec);
	void	(*init_ptr_from_cur)(struct xfs_btree_cur *cur,
				     union xfs_btree_ptr *ptr);
	void	(*init_high_key_from_rec)(union xfs_btree_key *key,
					  union xfs_btree_rec *rec);

	/* difference between key value and cursor value */
	int64_t (*key_diff)(struct xfs_btree_cur *cur,
			      union xfs_btree_key *key);

	/*
	 * Difference between key2 and key1 -- positive if key1 > key2,
	 * negative if key1 < key2, and zero if equal.
	 */
	int64_t (*diff_two_keys)(struct xfs_btree_cur *cur,
				   union xfs_btree_key *key1,
				   union xfs_btree_key *key2);

	const struct xfs_buf_ops	*buf_ops;

	/* check that k1 is lower than k2 */
	int	(*keys_inorder)(struct xfs_btree_cur *cur,
				union xfs_btree_key *k1,
				union xfs_btree_key *k2);

	/* check that r1 is lower than r2 */
	int	(*recs_inorder)(struct xfs_btree_cur *cur,
				union xfs_btree_rec *r1,
				union xfs_btree_rec *r2);
};

/*
 * Reasons for the update_lastrec method to be called.
 */
#define LASTREC_UPDATE	0
#define LASTREC_INSREC	1
#define LASTREC_DELREC	2


union xfs_btree_irec {
	struct xfs_alloc_rec_incore	a;
	struct xfs_bmbt_irec		b;
	struct xfs_inobt_rec_incore	i;
	struct xfs_rmap_irec		r;
	struct xfs_refcount_irec	rc;
};

/* Per-AG btree information. */
struct xfs_btree_cur_ag {
	union {
		struct xfs_buf		*agbp;
		struct xbtree_afakeroot	*afake;	/* for staging cursor */
	};
	xfs_agnumber_t		agno;
	union {
		struct {
			unsigned long nr_ops;	/* # record updates */
			int	shape_changes;	/* # of extent splits */
		} refc;
		struct {
			bool	active;		/* allocation cursor state */
		} abt;
	};
};

/* Btree-in-inode cursor information */
struct xfs_btree_cur_ino {
	struct xfs_inode		*ip;
	struct xbtree_ifakeroot		*ifake;	/* for staging cursor */
	int				allocated;
	short				forksize;
	char				whichfork;
	char				flags;
/* We are converting a delalloc reservation */
#define	XFS_BTCUR_BMBT_WASDEL		(1 << 0)

/* For extent swap, ignore owner check in verifier */
#define	XFS_BTCUR_BMBT_INVALID_OWNER	(1 << 1)
};

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
	union xfs_btree_irec	bc_rec;	/* current insert/search record value */
	struct xfs_buf	*bc_bufs[XFS_BTREE_MAXLEVELS];	/* buf ptr per level */
	int		bc_ptrs[XFS_BTREE_MAXLEVELS];	/* key/record # */
	uint8_t		bc_ra[XFS_BTREE_MAXLEVELS];	/* readahead bits */
#define	XFS_BTCUR_LEFTRA	1	/* left sibling has been read-ahead */
#define	XFS_BTCUR_RIGHTRA	2	/* right sibling has been read-ahead */
	uint8_t		bc_nlevels;	/* number of levels in the tree */
	uint8_t		bc_blocklog;	/* log2(blocksize) of btree blocks */
	xfs_btnum_t	bc_btnum;	/* identifies which btree type */
	int		bc_statoff;	/* offset of btre stats array */
	union {
		struct xfs_btree_cur_ag	bc_ag;
		struct xfs_btree_cur_ino bc_ino;
	};
} xfs_btree_cur_t;

/* cursor flags */
#define XFS_BTREE_LONG_PTRS		(1<<0)	/* pointers are 64bits long */
#define XFS_BTREE_ROOT_IN_INODE		(1<<1)	/* root may be variable size */
#define XFS_BTREE_LASTREC_UPDATE	(1<<2)	/* track last rec externally */
#define XFS_BTREE_CRC_BLOCKS		(1<<3)	/* uses extended btree blocks */
#define XFS_BTREE_OVERLAPPING		(1<<4)	/* overlapping intervals */
/*
 * The root of this btree is a fakeroot structure so that we can stage a btree
 * rebuild without leaving it accessible via primary metadata.  The ops struct
 * is dynamically allocated and must be freed when the cursor is deleted.
 */
#define XFS_BTREE_STAGING		(1<<5)


#define	XFS_BTREE_NOERROR	0
#define	XFS_BTREE_ERROR		1

/*
 * Convert from buffer to btree block header.
 */
#define	XFS_BUF_TO_BLOCK(bp)	((struct xfs_btree_block *)((bp)->b_addr))

/*
 * Internal long and short btree block checks.  They return NULL if the
 * block is ok or the address of the failed check otherwise.
 */
xfs_failaddr_t __xfs_btree_check_lblock(struct xfs_btree_cur *cur,
		struct xfs_btree_block *block, int level, struct xfs_buf *bp);
xfs_failaddr_t __xfs_btree_check_sblock(struct xfs_btree_cur *cur,
		struct xfs_btree_block *block, int level, struct xfs_buf *bp);

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
bool					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_lptr(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_fsblock_t		fsbno,	/* btree block disk address */
	int			level);	/* btree block level */

/*
 * Check that (short) pointer is ok.
 */
bool					/* error (0 or EFSCORRUPTED) */
xfs_btree_check_sptr(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		agbno,	/* btree block disk address */
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
 * Compute first and last byte offsets for the fields given.
 * Interprets the offsets table, which contains struct field offsets.
 */
void
xfs_btree_offsets(
	int64_t			fields,	/* bitmask of fields */
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
	xfs_btnum_t	btnum,
	__u16		level,
	__u16		numrecs,
	__u64		owner);

void
xfs_btree_init_block_int(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*buf,
	xfs_daddr_t		blkno,
	xfs_btnum_t		btnum,
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
int xfs_btree_change_owner(struct xfs_btree_cur *cur, uint64_t new_owner,
			   struct list_head *buffer_list);

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
		uint16_t numrecs)
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

xfs_failaddr_t xfs_btree_sblock_v5hdr_verify(struct xfs_buf *bp);
xfs_failaddr_t xfs_btree_sblock_verify(struct xfs_buf *bp,
		unsigned int max_recs);
xfs_failaddr_t xfs_btree_lblock_v5hdr_verify(struct xfs_buf *bp,
		uint64_t owner);
xfs_failaddr_t xfs_btree_lblock_verify(struct xfs_buf *bp,
		unsigned int max_recs);

uint xfs_btree_compute_maxlevels(uint *limits, unsigned long len);
unsigned long long xfs_btree_calc_size(uint *limits, unsigned long long len);

/*
 * Return codes for the query range iterator function are 0 to continue
 * iterating, and non-zero to stop iterating.  Any non-zero value will be
 * passed up to the _query_range caller.  The special value -ECANCELED can be
 * used to stop iteration, because _query_range never generates that error
 * code on its own.
 */
typedef int (*xfs_btree_query_range_fn)(struct xfs_btree_cur *cur,
		union xfs_btree_rec *rec, void *priv);

int xfs_btree_query_range(struct xfs_btree_cur *cur,
		union xfs_btree_irec *low_rec, union xfs_btree_irec *high_rec,
		xfs_btree_query_range_fn fn, void *priv);
int xfs_btree_query_all(struct xfs_btree_cur *cur, xfs_btree_query_range_fn fn,
		void *priv);

typedef int (*xfs_btree_visit_blocks_fn)(struct xfs_btree_cur *cur, int level,
		void *data);
/* Visit record blocks. */
#define XFS_BTREE_VISIT_RECORDS		(1 << 0)
/* Visit leaf blocks. */
#define XFS_BTREE_VISIT_LEAVES		(1 << 1)
/* Visit all blocks. */
#define XFS_BTREE_VISIT_ALL		(XFS_BTREE_VISIT_RECORDS | \
					 XFS_BTREE_VISIT_LEAVES)
int xfs_btree_visit_blocks(struct xfs_btree_cur *cur,
		xfs_btree_visit_blocks_fn fn, unsigned int flags, void *data);

int xfs_btree_count_blocks(struct xfs_btree_cur *cur, xfs_extlen_t *blocks);

union xfs_btree_rec *xfs_btree_rec_addr(struct xfs_btree_cur *cur, int n,
		struct xfs_btree_block *block);
union xfs_btree_key *xfs_btree_key_addr(struct xfs_btree_cur *cur, int n,
		struct xfs_btree_block *block);
union xfs_btree_key *xfs_btree_high_key_addr(struct xfs_btree_cur *cur, int n,
		struct xfs_btree_block *block);
union xfs_btree_ptr *xfs_btree_ptr_addr(struct xfs_btree_cur *cur, int n,
		struct xfs_btree_block *block);
int xfs_btree_lookup_get_block(struct xfs_btree_cur *cur, int level,
		union xfs_btree_ptr *pp, struct xfs_btree_block **blkp);
struct xfs_btree_block *xfs_btree_get_block(struct xfs_btree_cur *cur,
		int level, struct xfs_buf **bpp);
bool xfs_btree_ptr_is_null(struct xfs_btree_cur *cur, union xfs_btree_ptr *ptr);
int64_t xfs_btree_diff_two_ptrs(struct xfs_btree_cur *cur,
				const union xfs_btree_ptr *a,
				const union xfs_btree_ptr *b);
void xfs_btree_get_sibling(struct xfs_btree_cur *cur,
			   struct xfs_btree_block *block,
			   union xfs_btree_ptr *ptr, int lr);
void xfs_btree_get_keys(struct xfs_btree_cur *cur,
		struct xfs_btree_block *block, union xfs_btree_key *key);
union xfs_btree_key *xfs_btree_high_key_from_key(struct xfs_btree_cur *cur,
		union xfs_btree_key *key);
int xfs_btree_has_record(struct xfs_btree_cur *cur, union xfs_btree_irec *low,
		union xfs_btree_irec *high, bool *exists);
bool xfs_btree_has_more_records(struct xfs_btree_cur *cur);
struct xfs_ifork *xfs_btree_ifork_ptr(struct xfs_btree_cur *cur);

/* Does this cursor point to the last block in the given level? */
static inline bool
xfs_btree_islastblock(
	xfs_btree_cur_t		*cur,
	int			level)
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;

	block = xfs_btree_get_block(cur, level, &bp);
	ASSERT(block && xfs_btree_check_block(cur, block, level, bp) == 0);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return block->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK);
	return block->bb_u.s.bb_rightsib == cpu_to_be32(NULLAGBLOCK);
}

void xfs_btree_set_ptr_null(struct xfs_btree_cur *cur,
		union xfs_btree_ptr *ptr);
int xfs_btree_get_buf_block(struct xfs_btree_cur *cur, union xfs_btree_ptr *ptr,
		struct xfs_btree_block **block, struct xfs_buf **bpp);
void xfs_btree_set_sibling(struct xfs_btree_cur *cur,
		struct xfs_btree_block *block, union xfs_btree_ptr *ptr,
		int lr);
void xfs_btree_init_block_cur(struct xfs_btree_cur *cur,
		struct xfs_buf *bp, int level, int numrecs);
void xfs_btree_copy_ptrs(struct xfs_btree_cur *cur,
		union xfs_btree_ptr *dst_ptr,
		const union xfs_btree_ptr *src_ptr, int numptrs);
void xfs_btree_copy_keys(struct xfs_btree_cur *cur,
		union xfs_btree_key *dst_key, union xfs_btree_key *src_key,
		int numkeys);

#endif	/* __XFS_BTREE_H__ */
