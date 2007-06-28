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
#ifndef __XFS_DIR2_NODE_H__
#define	__XFS_DIR2_NODE_H__

/*
 * Directory version 2, btree node format structures
 */

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_da_state;
struct xfs_da_state_blk;
struct xfs_inode;
struct xfs_trans;

/*
 * Offset of the freespace index.
 */
#define	XFS_DIR2_FREE_SPACE	2
#define	XFS_DIR2_FREE_OFFSET	(XFS_DIR2_FREE_SPACE * XFS_DIR2_SPACE_SIZE)
#define	XFS_DIR2_FREE_FIRSTDB(mp)	\
	xfs_dir2_byte_to_db(mp, XFS_DIR2_FREE_OFFSET)

#define	XFS_DIR2_FREE_MAGIC	0x58443246	/* XD2F */

typedef	struct xfs_dir2_free_hdr {
	__be32			magic;		/* XFS_DIR2_FREE_MAGIC */
	__be32			firstdb;	/* db of first entry */
	__be32			nvalid;		/* count of valid entries */
	__be32			nused;		/* count of used entries */
} xfs_dir2_free_hdr_t;

typedef struct xfs_dir2_free {
	xfs_dir2_free_hdr_t	hdr;		/* block header */
	__be16			bests[1];	/* best free counts */
						/* unused entries are -1 */
} xfs_dir2_free_t;

#define	XFS_DIR2_MAX_FREE_BESTS(mp)	\
	(((mp)->m_dirblksize - (uint)sizeof(xfs_dir2_free_hdr_t)) / \
	 (uint)sizeof(xfs_dir2_data_off_t))

/*
 * Convert data space db to the corresponding free db.
 */
static inline xfs_dir2_db_t
xfs_dir2_db_to_fdb(struct xfs_mount *mp, xfs_dir2_db_t db)
{
	return (XFS_DIR2_FREE_FIRSTDB(mp) + (db) / XFS_DIR2_MAX_FREE_BESTS(mp));
}

/*
 * Convert data space db to the corresponding index in a free db.
 */
static inline int
xfs_dir2_db_to_fdindex(struct xfs_mount *mp, xfs_dir2_db_t db)
{
	return ((db) % XFS_DIR2_MAX_FREE_BESTS(mp));
}

extern void xfs_dir2_free_log_bests(struct xfs_trans *tp, struct xfs_dabuf *bp,
				    int first, int last);
extern int xfs_dir2_leaf_to_node(struct xfs_da_args *args,
				 struct xfs_dabuf *lbp);
extern xfs_dahash_t xfs_dir2_leafn_lasthash(struct xfs_dabuf *bp, int *count);
extern int xfs_dir2_leafn_lookup_int(struct xfs_dabuf *bp,
				     struct xfs_da_args *args, int *indexp,
				     struct xfs_da_state *state);
extern int xfs_dir2_leafn_order(struct xfs_dabuf *leaf1_bp,
				struct xfs_dabuf *leaf2_bp);
extern int xfs_dir2_leafn_split(struct xfs_da_state *state,
				struct xfs_da_state_blk *oldblk,
				struct xfs_da_state_blk *newblk);
extern int xfs_dir2_leafn_toosmall(struct xfs_da_state *state, int *action);
extern void xfs_dir2_leafn_unbalance(struct xfs_da_state *state,
				     struct xfs_da_state_blk *drop_blk,
				     struct xfs_da_state_blk *save_blk);
extern int xfs_dir2_node_addname(struct xfs_da_args *args);
extern int xfs_dir2_node_lookup(struct xfs_da_args *args);
extern int xfs_dir2_node_removename(struct xfs_da_args *args);
extern int xfs_dir2_node_replace(struct xfs_da_args *args);
extern int xfs_dir2_node_trim_free(struct xfs_da_args *args, xfs_fileoff_t fo,
				   int *rvalp);

#endif	/* __XFS_DIR2_NODE_H__ */
