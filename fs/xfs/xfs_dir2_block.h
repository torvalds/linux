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
#ifndef __XFS_DIR2_BLOCK_H__
#define	__XFS_DIR2_BLOCK_H__

/*
 * xfs_dir2_block.h
 * Directory version 2, single block format structures
 */

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_dir2_data_hdr;
struct xfs_dir2_leaf_entry;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * The single block format is as follows:
 * xfs_dir2_data_hdr_t structure
 * xfs_dir2_data_entry_t and xfs_dir2_data_unused_t structures
 * xfs_dir2_leaf_entry_t structures
 * xfs_dir2_block_tail_t structure
 */

#define	XFS_DIR2_BLOCK_MAGIC	0x58443242	/* XD2B: for one block dirs */

typedef struct xfs_dir2_block_tail {
	__be32		count;			/* count of leaf entries */
	__be32		stale;			/* count of stale lf entries */
} xfs_dir2_block_tail_t;

/*
 * Generic single-block structure, for xfs_db.
 */
typedef struct xfs_dir2_block {
	xfs_dir2_data_hdr_t	hdr;		/* magic XFS_DIR2_BLOCK_MAGIC */
	xfs_dir2_data_union_t	u[1];
	xfs_dir2_leaf_entry_t	leaf[1];
	xfs_dir2_block_tail_t	tail;
} xfs_dir2_block_t;

/*
 * Pointer to the leaf header embedded in a data block (1-block format)
 */
static inline xfs_dir2_block_tail_t *
xfs_dir2_block_tail_p(struct xfs_mount *mp, xfs_dir2_block_t *block)
{
	return (((xfs_dir2_block_tail_t *)
		((char *)(block) + (mp)->m_dirblksize)) - 1);
}

/*
 * Pointer to the leaf entries embedded in a data block (1-block format)
 */
static inline struct xfs_dir2_leaf_entry *
xfs_dir2_block_leaf_p(xfs_dir2_block_tail_t *btp)
{
	return ((struct xfs_dir2_leaf_entry *)btp) - be32_to_cpu(btp->count);
}

/*
 * Function declarations.
 */
extern int xfs_dir2_block_addname(struct xfs_da_args *args);
extern int xfs_dir2_block_getdents(struct xfs_trans *tp, struct xfs_inode *dp,
				   struct uio *uio, int *eofp,
				   struct xfs_dirent *dbp, xfs_dir2_put_t put);
extern int xfs_dir2_block_lookup(struct xfs_da_args *args);
extern int xfs_dir2_block_removename(struct xfs_da_args *args);
extern int xfs_dir2_block_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_to_block(struct xfs_da_args *args,
				  struct xfs_dabuf *lbp, struct xfs_dabuf *dbp);
extern int xfs_dir2_sf_to_block(struct xfs_da_args *args);

#endif	/* __XFS_DIR2_BLOCK_H__ */
