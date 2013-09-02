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
#define __XFS_DIR2_H__

struct xfs_bmap_free;
struct xfs_da_args;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct xfs_dir2_sf_hdr;
struct xfs_dir2_sf_entry;
struct xfs_dir2_data_hdr;
struct xfs_dir2_data_entry;
struct xfs_dir2_data_unused;

extern struct xfs_name	xfs_name_dotdot;

/*
 * Generic directory interface routines
 */
extern void xfs_dir_startup(void);
extern void xfs_dir_mount(struct xfs_mount *mp);
extern int xfs_dir_isempty(struct xfs_inode *dp);
extern int xfs_dir_init(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_inode *pdp);
extern int xfs_dir_createname(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t inum,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
extern int xfs_dir_lookup(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t *inum,
				struct xfs_name *ci_name);
extern int xfs_dir_removename(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t ino,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
extern int xfs_dir_replace(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t inum,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
extern int xfs_dir_canenter(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, uint resblks);

/*
 * Direct call from the bmap code, bypassing the generic directory layer.
 */
extern int xfs_dir2_sf_to_block(struct xfs_da_args *args);

/*
 * Interface routines used by userspace utilities
 */
extern xfs_ino_t xfs_dir2_sf_get_parent_ino(struct xfs_dir2_sf_hdr *sfp);
extern void xfs_dir2_sf_put_parent_ino(struct xfs_dir2_sf_hdr *sfp,
		xfs_ino_t ino);
extern xfs_ino_t xfs_dir3_sfe_get_ino(struct xfs_mount *mp,
		struct xfs_dir2_sf_hdr *sfp, struct xfs_dir2_sf_entry *sfep);
extern void xfs_dir3_sfe_put_ino(struct xfs_mount *mp,
		struct xfs_dir2_sf_hdr *hdr, struct xfs_dir2_sf_entry *sfep,
		xfs_ino_t ino);

extern int xfs_dir2_isblock(struct xfs_trans *tp, struct xfs_inode *dp, int *r);
extern int xfs_dir2_isleaf(struct xfs_trans *tp, struct xfs_inode *dp, int *r);
extern int xfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_buf *bp);

extern void xfs_dir2_data_freescan(struct xfs_mount *mp,
		struct xfs_dir2_data_hdr *hdr, int *loghead);
extern void xfs_dir2_data_log_entry(struct xfs_trans *tp, struct xfs_buf *bp,
		struct xfs_dir2_data_entry *dep);
extern void xfs_dir2_data_log_header(struct xfs_trans *tp,
		struct xfs_buf *bp);
extern void xfs_dir2_data_log_unused(struct xfs_trans *tp, struct xfs_buf *bp,
		struct xfs_dir2_data_unused *dup);
extern void xfs_dir2_data_make_free(struct xfs_trans *tp, struct xfs_buf *bp,
		xfs_dir2_data_aoff_t offset, xfs_dir2_data_aoff_t len,
		int *needlogp, int *needscanp);
extern void xfs_dir2_data_use_free(struct xfs_trans *tp, struct xfs_buf *bp,
		struct xfs_dir2_data_unused *dup, xfs_dir2_data_aoff_t offset,
		xfs_dir2_data_aoff_t len, int *needlogp, int *needscanp);

extern struct xfs_dir2_data_free *xfs_dir2_data_freefind(
		struct xfs_dir2_data_hdr *hdr, struct xfs_dir2_data_unused *dup);

extern const struct xfs_buf_ops xfs_dir3_block_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leafn_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leaf1_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_free_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_data_buf_ops;

#endif	/* __XFS_DIR2_H__ */
