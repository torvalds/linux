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
#ifndef __XFS_DIR2_LEAF_H__
#define	__XFS_DIR2_LEAF_H__

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Offset of the leaf/node space.  First block in this space
 * is the btree root.
 */
#define	XFS_DIR2_LEAF_SPACE	1
#define	XFS_DIR2_LEAF_OFFSET	(XFS_DIR2_LEAF_SPACE * XFS_DIR2_SPACE_SIZE)
#define	XFS_DIR2_LEAF_FIRSTDB(mp)	\
	xfs_dir2_byte_to_db(mp, XFS_DIR2_LEAF_OFFSET)

/*
 * Offset in data space of a data entry.
 */
typedef	__uint32_t	xfs_dir2_dataptr_t;
#define	XFS_DIR2_MAX_DATAPTR	((xfs_dir2_dataptr_t)0xffffffff)
#define	XFS_DIR2_NULL_DATAPTR	((xfs_dir2_dataptr_t)0)

/*
 * Leaf block header.
 */
typedef struct xfs_dir2_leaf_hdr {
	xfs_da_blkinfo_t	info;		/* header for da routines */
	__be16			count;		/* count of entries */
	__be16			stale;		/* count of stale entries */
} xfs_dir2_leaf_hdr_t;

/*
 * Leaf block entry.
 */
typedef struct xfs_dir2_leaf_entry {
	__be32			hashval;	/* hash value of name */
	__be32			address;	/* address of data entry */
} xfs_dir2_leaf_entry_t;

/*
 * Leaf block tail.
 */
typedef struct xfs_dir2_leaf_tail {
	__be32			bestcount;
} xfs_dir2_leaf_tail_t;

/*
 * Leaf block.
 * bests and tail are at the end of the block for single-leaf only
 * (magic = XFS_DIR2_LEAF1_MAGIC not XFS_DIR2_LEAFN_MAGIC).
 */
typedef struct xfs_dir2_leaf {
	xfs_dir2_leaf_hdr_t	hdr;		/* leaf header */
	xfs_dir2_leaf_entry_t	ents[1];	/* entries */
						/* ... */
	xfs_dir2_data_off_t	bests[1];	/* best free counts */
	xfs_dir2_leaf_tail_t	tail;		/* leaf tail */
} xfs_dir2_leaf_t;

/*
 * DB blocks here are logical directory block numbers, not filesystem blocks.
 */

static inline int xfs_dir2_max_leaf_ents(struct xfs_mount *mp)
{
	return (int)(((mp)->m_dirblksize - (uint)sizeof(xfs_dir2_leaf_hdr_t)) /
	       (uint)sizeof(xfs_dir2_leaf_entry_t));
}

/*
 * Get address of the bestcount field in the single-leaf block.
 */
static inline xfs_dir2_leaf_tail_t *
xfs_dir2_leaf_tail_p(struct xfs_mount *mp, xfs_dir2_leaf_t *lp)
{
	return (xfs_dir2_leaf_tail_t *)
		((char *)(lp) + (mp)->m_dirblksize - 
		  (uint)sizeof(xfs_dir2_leaf_tail_t));
}

/*
 * Get address of the bests array in the single-leaf block.
 */
static inline __be16 *
xfs_dir2_leaf_bests_p(xfs_dir2_leaf_tail_t *ltp)
{
	return (__be16 *)ltp - be32_to_cpu(ltp->bestcount);
}

/*
 * Convert dataptr to byte in file space
 */
static inline xfs_dir2_off_t
xfs_dir2_dataptr_to_byte(struct xfs_mount *mp, xfs_dir2_dataptr_t dp)
{
	return (xfs_dir2_off_t)(dp) << XFS_DIR2_DATA_ALIGN_LOG;
}

/*
 * Convert byte in file space to dataptr.  It had better be aligned.
 */
static inline xfs_dir2_dataptr_t
xfs_dir2_byte_to_dataptr(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return (xfs_dir2_dataptr_t)((by) >> XFS_DIR2_DATA_ALIGN_LOG);
}

/*
 * Convert byte in space to (DB) block
 */
static inline xfs_dir2_db_t
xfs_dir2_byte_to_db(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return (xfs_dir2_db_t)((by) >> \
		 ((mp)->m_sb.sb_blocklog + (mp)->m_sb.sb_dirblklog));
}

/*
 * Convert dataptr to a block number
 */
static inline xfs_dir2_db_t
xfs_dir2_dataptr_to_db(struct xfs_mount *mp, xfs_dir2_dataptr_t dp)
{
	return xfs_dir2_byte_to_db(mp, xfs_dir2_dataptr_to_byte(mp, dp));
}

/*
 * Convert byte in space to offset in a block
 */
static inline xfs_dir2_data_aoff_t
xfs_dir2_byte_to_off(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return (xfs_dir2_data_aoff_t)((by) & \
		((1 << ((mp)->m_sb.sb_blocklog + (mp)->m_sb.sb_dirblklog)) - 1));
}

/*
 * Convert dataptr to a byte offset in a block
 */
static inline xfs_dir2_data_aoff_t
xfs_dir2_dataptr_to_off(struct xfs_mount *mp, xfs_dir2_dataptr_t dp)
{
	return xfs_dir2_byte_to_off(mp, xfs_dir2_dataptr_to_byte(mp, dp));
}

/*
 * Convert block and offset to byte in space
 */
static inline xfs_dir2_off_t
xfs_dir2_db_off_to_byte(struct xfs_mount *mp, xfs_dir2_db_t db,
			xfs_dir2_data_aoff_t o)
{
	return ((xfs_dir2_off_t)(db) << \
		((mp)->m_sb.sb_blocklog + (mp)->m_sb.sb_dirblklog)) + (o);
}

/*
 * Convert block (DB) to block (dablk)
 */
static inline xfs_dablk_t
xfs_dir2_db_to_da(struct xfs_mount *mp, xfs_dir2_db_t db)
{
	return (xfs_dablk_t)((db) << (mp)->m_sb.sb_dirblklog);
}

/*
 * Convert byte in space to (DA) block
 */
static inline xfs_dablk_t
xfs_dir2_byte_to_da(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return xfs_dir2_db_to_da(mp, xfs_dir2_byte_to_db(mp, by));
}

/*
 * Convert block and offset to dataptr
 */
static inline xfs_dir2_dataptr_t
xfs_dir2_db_off_to_dataptr(struct xfs_mount *mp, xfs_dir2_db_t db,
			   xfs_dir2_data_aoff_t o)
{
	return xfs_dir2_byte_to_dataptr(mp, xfs_dir2_db_off_to_byte(mp, db, o));
}

/*
 * Convert block (dablk) to block (DB)
 */
static inline xfs_dir2_db_t
xfs_dir2_da_to_db(struct xfs_mount *mp, xfs_dablk_t da)
{
	return (xfs_dir2_db_t)((da) >> (mp)->m_sb.sb_dirblklog);
}

/*
 * Convert block (dablk) to byte offset in space
 */
static inline xfs_dir2_off_t
xfs_dir2_da_to_byte(struct xfs_mount *mp, xfs_dablk_t da)
{
	return xfs_dir2_db_off_to_byte(mp, xfs_dir2_da_to_db(mp, da), 0);
}

/*
 * Function declarations.
 */
extern int xfs_dir2_block_to_leaf(struct xfs_da_args *args,
				  struct xfs_dabuf *dbp);
extern int xfs_dir2_leaf_addname(struct xfs_da_args *args);
extern void xfs_dir2_leaf_compact(struct xfs_da_args *args,
				  struct xfs_dabuf *bp);
extern void xfs_dir2_leaf_compact_x1(struct xfs_dabuf *bp, int *indexp,
				     int *lowstalep, int *highstalep,
				     int *lowlogp, int *highlogp);
extern int xfs_dir2_leaf_getdents(struct xfs_trans *tp, struct xfs_inode *dp,
				  struct uio *uio, int *eofp,
				  struct xfs_dirent *dbp, xfs_dir2_put_t put);
extern int xfs_dir2_leaf_init(struct xfs_da_args *args, xfs_dir2_db_t bno,
			      struct xfs_dabuf **bpp, int magic);
extern void xfs_dir2_leaf_log_ents(struct xfs_trans *tp, struct xfs_dabuf *bp,
				   int first, int last);
extern void xfs_dir2_leaf_log_header(struct xfs_trans *tp,
				     struct xfs_dabuf *bp);
extern int xfs_dir2_leaf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_leaf_removename(struct xfs_da_args *args);
extern int xfs_dir2_leaf_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_search_hash(struct xfs_da_args *args,
				     struct xfs_dabuf *lbp);
extern int xfs_dir2_leaf_trim_data(struct xfs_da_args *args,
				   struct xfs_dabuf *lbp, xfs_dir2_db_t db);
extern int xfs_dir2_node_to_leaf(struct xfs_da_state *state);

#endif	/* __XFS_DIR2_LEAF_H__ */
