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
#ifndef __XFS_DIR2_H__
#define	__XFS_DIR2_H__

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_dir2_put_args;
struct xfs_bmap_free;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Directory version 2.
 * There are 4 possible formats:
 *	shortform
 *	single block - data with embedded leaf at the end
 *	multiple data blocks, single leaf+freeindex block
 *	data blocks, node&leaf blocks (btree), freeindex blocks
 *
 *	The shortform format is in xfs_dir2_sf.h.
 *	The single block format is in xfs_dir2_block.h.
 *	The data block format is in xfs_dir2_data.h.
 *	The leaf and freeindex block formats are in xfs_dir2_leaf.h.
 *	Node blocks are the same as the other version, in xfs_da_btree.h.
 */

/*
 * Byte offset in data block and shortform entry.
 */
typedef	__uint16_t	xfs_dir2_data_off_t;
#define	NULLDATAOFF	0xffffU
typedef uint		xfs_dir2_data_aoff_t;	/* argument form */

/*
 * Directory block number (logical dirblk in file)
 */
typedef	__uint32_t	xfs_dir2_db_t;

/*
 * Byte offset in a directory.
 */
typedef	xfs_off_t	xfs_dir2_off_t;

/*
 * Generic directory interface routines
 */
extern void xfs_dir_startup(void);
extern void xfs_dir_mount(struct xfs_mount *mp);
extern int xfs_dir_isempty(struct xfs_inode *dp);
extern int xfs_dir_init(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_inode *pdp);
extern int xfs_dir_createname(struct xfs_trans *tp, struct xfs_inode *dp,
				char *name, int namelen, xfs_ino_t inum,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
extern int xfs_dir_lookup(struct xfs_trans *tp, struct xfs_inode *dp,
				char *name, int namelen, xfs_ino_t *inum);
extern int xfs_dir_removename(struct xfs_trans *tp, struct xfs_inode *dp,
				char *name, int namelen, xfs_ino_t ino,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
extern int xfs_dir_replace(struct xfs_trans *tp, struct xfs_inode *dp,
				char *name, int namelen, xfs_ino_t inum,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
extern int xfs_dir_canenter(struct xfs_trans *tp, struct xfs_inode *dp,
				char *name, int namelen);
extern int xfs_dir_ino_validate(struct xfs_mount *mp, xfs_ino_t ino);

/*
 * Utility routines for v2 directories.
 */
extern int xfs_dir2_grow_inode(struct xfs_da_args *args, int space,
				xfs_dir2_db_t *dbp);
extern int xfs_dir2_isblock(struct xfs_trans *tp, struct xfs_inode *dp,
				int *vp);
extern int xfs_dir2_isleaf(struct xfs_trans *tp, struct xfs_inode *dp,
				int *vp);
extern int xfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_dabuf *bp);

#endif	/* __XFS_DIR2_H__ */
