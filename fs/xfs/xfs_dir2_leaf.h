/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_DIR2_LEAF_H__
#define	__XFS_DIR2_LEAF_H__

/*
 * Directory version 2, leaf block structures.
 */

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Constants.
 */

/*
 * Offset of the leaf/node space.  First block in this space
 * is the btree root.
 */
#define	XFS_DIR2_LEAF_SPACE	1
#define	XFS_DIR2_LEAF_OFFSET	(XFS_DIR2_LEAF_SPACE * XFS_DIR2_SPACE_SIZE)
#define	XFS_DIR2_LEAF_FIRSTDB(mp)	\
	XFS_DIR2_BYTE_TO_DB(mp, XFS_DIR2_LEAF_OFFSET)

/*
 * Types.
 */

/*
 * Offset in data space of a data entry.
 */
typedef	__uint32_t	xfs_dir2_dataptr_t;
#define	XFS_DIR2_MAX_DATAPTR	((xfs_dir2_dataptr_t)0xffffffff)
#define	XFS_DIR2_NULL_DATAPTR	((xfs_dir2_dataptr_t)0)

/*
 * Structures.
 */

/*
 * Leaf block header.
 */
typedef struct xfs_dir2_leaf_hdr {
	xfs_da_blkinfo_t	info;		/* header for da routines */
	__uint16_t		count;		/* count of entries */
	__uint16_t		stale;		/* count of stale entries */
} xfs_dir2_leaf_hdr_t;

/*
 * Leaf block entry.
 */
typedef struct xfs_dir2_leaf_entry {
	xfs_dahash_t		hashval;	/* hash value of name */
	xfs_dir2_dataptr_t	address;	/* address of data entry */
} xfs_dir2_leaf_entry_t;

/*
 * Leaf block tail.
 */
typedef struct xfs_dir2_leaf_tail {
	__uint32_t		bestcount;
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
 * Macros.
 * The DB blocks are logical directory block numbers, not filesystem blocks.
 */

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_MAX_LEAF_ENTS)
int
xfs_dir2_max_leaf_ents(struct xfs_mount *mp);
#define	XFS_DIR2_MAX_LEAF_ENTS(mp)	\
	xfs_dir2_max_leaf_ents(mp)
#else
#define	XFS_DIR2_MAX_LEAF_ENTS(mp)	\
	((int)(((mp)->m_dirblksize - (uint)sizeof(xfs_dir2_leaf_hdr_t)) / \
	       (uint)sizeof(xfs_dir2_leaf_entry_t)))
#endif

/*
 * Get address of the bestcount field in the single-leaf block.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_LEAF_TAIL_P)
xfs_dir2_leaf_tail_t *
xfs_dir2_leaf_tail_p(struct xfs_mount *mp, xfs_dir2_leaf_t *lp);
#define	XFS_DIR2_LEAF_TAIL_P(mp,lp)	\
	xfs_dir2_leaf_tail_p(mp, lp)
#else
#define	XFS_DIR2_LEAF_TAIL_P(mp,lp)	\
	((xfs_dir2_leaf_tail_t *)\
	 ((char *)(lp) + (mp)->m_dirblksize - \
	  (uint)sizeof(xfs_dir2_leaf_tail_t)))
#endif

/*
 * Get address of the bests array in the single-leaf block.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_LEAF_BESTS_P)
xfs_dir2_data_off_t *
xfs_dir2_leaf_bests_p(xfs_dir2_leaf_tail_t *ltp);
#define	XFS_DIR2_LEAF_BESTS_P(ltp)	xfs_dir2_leaf_bests_p(ltp)
#else
#define	XFS_DIR2_LEAF_BESTS_P(ltp)	\
	((xfs_dir2_data_off_t *)(ltp) - INT_GET((ltp)->bestcount, ARCH_CONVERT))
#endif

/*
 * Convert dataptr to byte in file space
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DATAPTR_TO_BYTE)
xfs_dir2_off_t
xfs_dir2_dataptr_to_byte(struct xfs_mount *mp, xfs_dir2_dataptr_t dp);
#define	XFS_DIR2_DATAPTR_TO_BYTE(mp,dp)	xfs_dir2_dataptr_to_byte(mp, dp)
#else
#define	XFS_DIR2_DATAPTR_TO_BYTE(mp,dp)	\
	((xfs_dir2_off_t)(dp) << XFS_DIR2_DATA_ALIGN_LOG)
#endif

/*
 * Convert byte in file space to dataptr.  It had better be aligned.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_BYTE_TO_DATAPTR)
xfs_dir2_dataptr_t
xfs_dir2_byte_to_dataptr(struct xfs_mount *mp, xfs_dir2_off_t by);
#define	XFS_DIR2_BYTE_TO_DATAPTR(mp,by)	xfs_dir2_byte_to_dataptr(mp,by)
#else
#define	XFS_DIR2_BYTE_TO_DATAPTR(mp,by)	\
	((xfs_dir2_dataptr_t)((by) >> XFS_DIR2_DATA_ALIGN_LOG))
#endif

/*
 * Convert dataptr to a block number
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DATAPTR_TO_DB)
xfs_dir2_db_t
xfs_dir2_dataptr_to_db(struct xfs_mount *mp, xfs_dir2_dataptr_t dp);
#define	XFS_DIR2_DATAPTR_TO_DB(mp,dp)	xfs_dir2_dataptr_to_db(mp, dp)
#else
#define	XFS_DIR2_DATAPTR_TO_DB(mp,dp)	\
	XFS_DIR2_BYTE_TO_DB(mp, XFS_DIR2_DATAPTR_TO_BYTE(mp, dp))
#endif

/*
 * Convert dataptr to a byte offset in a block
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DATAPTR_TO_OFF)
xfs_dir2_data_aoff_t
xfs_dir2_dataptr_to_off(struct xfs_mount *mp, xfs_dir2_dataptr_t dp);
#define	XFS_DIR2_DATAPTR_TO_OFF(mp,dp)	xfs_dir2_dataptr_to_off(mp, dp)
#else
#define	XFS_DIR2_DATAPTR_TO_OFF(mp,dp)	\
	XFS_DIR2_BYTE_TO_OFF(mp, XFS_DIR2_DATAPTR_TO_BYTE(mp, dp))
#endif

/*
 * Convert block and offset to byte in space
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DB_OFF_TO_BYTE)
xfs_dir2_off_t
xfs_dir2_db_off_to_byte(struct xfs_mount *mp, xfs_dir2_db_t db,
			xfs_dir2_data_aoff_t o);
#define	XFS_DIR2_DB_OFF_TO_BYTE(mp,db,o)	\
	xfs_dir2_db_off_to_byte(mp, db, o)
#else
#define	XFS_DIR2_DB_OFF_TO_BYTE(mp,db,o)	\
	(((xfs_dir2_off_t)(db) << \
	 ((mp)->m_sb.sb_blocklog + (mp)->m_sb.sb_dirblklog)) + (o))
#endif

/*
 * Convert byte in space to (DB) block
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_BYTE_TO_DB)
xfs_dir2_db_t xfs_dir2_byte_to_db(struct xfs_mount *mp, xfs_dir2_off_t by);
#define	XFS_DIR2_BYTE_TO_DB(mp,by)	xfs_dir2_byte_to_db(mp, by)
#else
#define	XFS_DIR2_BYTE_TO_DB(mp,by)	\
	((xfs_dir2_db_t)((by) >> \
			 ((mp)->m_sb.sb_blocklog + (mp)->m_sb.sb_dirblklog)))
#endif

/*
 * Convert byte in space to (DA) block
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_BYTE_TO_DA)
xfs_dablk_t xfs_dir2_byte_to_da(struct xfs_mount *mp, xfs_dir2_off_t by);
#define	XFS_DIR2_BYTE_TO_DA(mp,by)	xfs_dir2_byte_to_da(mp, by)
#else
#define	XFS_DIR2_BYTE_TO_DA(mp,by)	\
	XFS_DIR2_DB_TO_DA(mp, XFS_DIR2_BYTE_TO_DB(mp, by))
#endif

/*
 * Convert byte in space to offset in a block
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_BYTE_TO_OFF)
xfs_dir2_data_aoff_t
xfs_dir2_byte_to_off(struct xfs_mount *mp, xfs_dir2_off_t by);
#define	XFS_DIR2_BYTE_TO_OFF(mp,by)	xfs_dir2_byte_to_off(mp, by)
#else
#define	XFS_DIR2_BYTE_TO_OFF(mp,by)	\
	((xfs_dir2_data_aoff_t)((by) & \
				((1 << ((mp)->m_sb.sb_blocklog + \
					(mp)->m_sb.sb_dirblklog)) - 1)))
#endif

/*
 * Convert block and offset to dataptr
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DB_OFF_TO_DATAPTR)
xfs_dir2_dataptr_t
xfs_dir2_db_off_to_dataptr(struct xfs_mount *mp, xfs_dir2_db_t db,
			   xfs_dir2_data_aoff_t o);
#define	XFS_DIR2_DB_OFF_TO_DATAPTR(mp,db,o)	\
	xfs_dir2_db_off_to_dataptr(mp, db, o)
#else
#define	XFS_DIR2_DB_OFF_TO_DATAPTR(mp,db,o)	\
	XFS_DIR2_BYTE_TO_DATAPTR(mp, XFS_DIR2_DB_OFF_TO_BYTE(mp, db, o))
#endif

/*
 * Convert block (DB) to block (dablk)
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DB_TO_DA)
xfs_dablk_t xfs_dir2_db_to_da(struct xfs_mount *mp, xfs_dir2_db_t db);
#define	XFS_DIR2_DB_TO_DA(mp,db)	xfs_dir2_db_to_da(mp, db)
#else
#define	XFS_DIR2_DB_TO_DA(mp,db)	\
	((xfs_dablk_t)((db) << (mp)->m_sb.sb_dirblklog))
#endif

/*
 * Convert block (dablk) to block (DB)
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DA_TO_DB)
xfs_dir2_db_t xfs_dir2_da_to_db(struct xfs_mount *mp, xfs_dablk_t da);
#define	XFS_DIR2_DA_TO_DB(mp,da)	xfs_dir2_da_to_db(mp, da)
#else
#define	XFS_DIR2_DA_TO_DB(mp,da)	\
	((xfs_dir2_db_t)((da) >> (mp)->m_sb.sb_dirblklog))
#endif

/*
 * Convert block (dablk) to byte offset in space
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_DA_TO_BYTE)
xfs_dir2_off_t xfs_dir2_da_to_byte(struct xfs_mount *mp, xfs_dablk_t da);
#define XFS_DIR2_DA_TO_BYTE(mp,da)	xfs_dir2_da_to_byte(mp, da)
#else
#define	XFS_DIR2_DA_TO_BYTE(mp,da)	\
	XFS_DIR2_DB_OFF_TO_BYTE(mp, XFS_DIR2_DA_TO_DB(mp, da), 0)
#endif

/*
 * Function declarations.
 */

extern int
	xfs_dir2_block_to_leaf(struct xfs_da_args *args, struct xfs_dabuf *dbp);

extern int
	xfs_dir2_leaf_addname(struct xfs_da_args *args);

extern void
	xfs_dir2_leaf_compact(struct xfs_da_args *args, struct xfs_dabuf *bp);

extern void
	xfs_dir2_leaf_compact_x1(struct xfs_dabuf *bp, int *indexp,
				 int *lowstalep, int *highstalep, int *lowlogp,
				 int *highlogp);

extern int
	xfs_dir2_leaf_getdents(struct xfs_trans *tp, struct xfs_inode *dp,
			       struct uio *uio, int *eofp, struct xfs_dirent *dbp,
			       xfs_dir2_put_t put);

extern int
	xfs_dir2_leaf_init(struct xfs_da_args *args, xfs_dir2_db_t bno,
			   struct xfs_dabuf **bpp, int magic);

extern void
	xfs_dir2_leaf_log_ents(struct xfs_trans *tp, struct xfs_dabuf *bp,
			       int first, int last);

extern void
	xfs_dir2_leaf_log_header(struct xfs_trans *tp, struct xfs_dabuf *bp);

extern int
	xfs_dir2_leaf_lookup(struct xfs_da_args *args);

extern int
	xfs_dir2_leaf_removename(struct xfs_da_args *args);

extern int
	xfs_dir2_leaf_replace(struct xfs_da_args *args);

extern int
	xfs_dir2_leaf_search_hash(struct xfs_da_args *args,
				  struct xfs_dabuf *lbp);
extern int
	xfs_dir2_leaf_trim_data(struct xfs_da_args *args, struct xfs_dabuf *lbp,				xfs_dir2_db_t db);

extern int
	xfs_dir2_node_to_leaf(struct xfs_da_state *state);

#endif	/* __XFS_DIR2_LEAF_H__ */
