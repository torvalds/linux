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
 * directory operations vector for encode/decode routines
 */
struct xfs_dir_ops {
	int	(*sf_entsize)(struct xfs_dir2_sf_hdr *hdr, int len);
	struct xfs_dir2_sf_entry *
		(*sf_nextentry)(struct xfs_dir2_sf_hdr *hdr,
				struct xfs_dir2_sf_entry *sfep);
	__uint8_t (*sf_get_ftype)(struct xfs_dir2_sf_entry *sfep);
	void	(*sf_put_ftype)(struct xfs_dir2_sf_entry *sfep,
				__uint8_t ftype);
	xfs_ino_t (*sf_get_ino)(struct xfs_dir2_sf_hdr *hdr,
				struct xfs_dir2_sf_entry *sfep);
	void	(*sf_put_ino)(struct xfs_dir2_sf_hdr *hdr,
			      struct xfs_dir2_sf_entry *sfep,
			      xfs_ino_t ino);
	xfs_ino_t (*sf_get_parent_ino)(struct xfs_dir2_sf_hdr *hdr);
	void	(*sf_put_parent_ino)(struct xfs_dir2_sf_hdr *hdr,
				     xfs_ino_t ino);

	int	(*data_entsize)(int len);
	__uint8_t (*data_get_ftype)(struct xfs_dir2_data_entry *dep);
	void	(*data_put_ftype)(struct xfs_dir2_data_entry *dep,
				__uint8_t ftype);
	__be16 * (*data_entry_tag_p)(struct xfs_dir2_data_entry *dep);
	struct xfs_dir2_data_free *
		(*data_bestfree_p)(struct xfs_dir2_data_hdr *hdr);

	xfs_dir2_data_aoff_t data_dot_offset;
	xfs_dir2_data_aoff_t data_dotdot_offset;
	xfs_dir2_data_aoff_t data_first_offset;
	size_t	data_entry_offset;

	struct xfs_dir2_data_entry *
		(*data_dot_entry_p)(struct xfs_dir2_data_hdr *hdr);
	struct xfs_dir2_data_entry *
		(*data_dotdot_entry_p)(struct xfs_dir2_data_hdr *hdr);
	struct xfs_dir2_data_entry *
		(*data_first_entry_p)(struct xfs_dir2_data_hdr *hdr);
	struct xfs_dir2_data_entry *
		(*data_entry_p)(struct xfs_dir2_data_hdr *hdr);
	struct xfs_dir2_data_unused *
		(*data_unused_p)(struct xfs_dir2_data_hdr *hdr);

	int	leaf_hdr_size;
	void	(*leaf_hdr_to_disk)(struct xfs_dir2_leaf *to,
				    struct xfs_dir3_icleaf_hdr *from);
	void	(*leaf_hdr_from_disk)(struct xfs_dir3_icleaf_hdr *to,
				      struct xfs_dir2_leaf *from);
	int	(*leaf_max_ents)(struct xfs_da_geometry *geo);
	struct xfs_dir2_leaf_entry *
		(*leaf_ents_p)(struct xfs_dir2_leaf *lp);

	int	node_hdr_size;
	void	(*node_hdr_to_disk)(struct xfs_da_intnode *to,
				    struct xfs_da3_icnode_hdr *from);
	void	(*node_hdr_from_disk)(struct xfs_da3_icnode_hdr *to,
				      struct xfs_da_intnode *from);
	struct xfs_da_node_entry *
		(*node_tree_p)(struct xfs_da_intnode *dap);

	int	free_hdr_size;
	void	(*free_hdr_to_disk)(struct xfs_dir2_free *to,
				    struct xfs_dir3_icfree_hdr *from);
	void	(*free_hdr_from_disk)(struct xfs_dir3_icfree_hdr *to,
				      struct xfs_dir2_free *from);
	int	(*free_max_bests)(struct xfs_da_geometry *geo);
	__be16 * (*free_bests_p)(struct xfs_dir2_free *free);
	xfs_dir2_db_t (*db_to_fdb)(struct xfs_da_geometry *geo,
				   xfs_dir2_db_t db);
	int	(*db_to_fdindex)(struct xfs_da_geometry *geo,
				 xfs_dir2_db_t db);
};

extern const struct xfs_dir_ops *
	xfs_dir_get_ops(struct xfs_mount *mp, struct xfs_inode *dp);
extern const struct xfs_dir_ops *
	xfs_nondir_get_ops(struct xfs_mount *mp, struct xfs_inode *dp);

/*
 * Generic directory interface routines
 */
extern void xfs_dir_startup(void);
extern int xfs_da_mount(struct xfs_mount *mp);
extern void xfs_da_unmount(struct xfs_mount *mp);

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
				struct xfs_name *name);

/*
 * Direct call from the bmap code, bypassing the generic directory layer.
 */
extern int xfs_dir2_sf_to_block(struct xfs_da_args *args);

/*
 * Interface routines used by userspace utilities
 */
extern int xfs_dir2_isblock(struct xfs_da_args *args, int *r);
extern int xfs_dir2_isleaf(struct xfs_da_args *args, int *r);
extern int xfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_buf *bp);

extern void xfs_dir2_data_freescan(struct xfs_inode *dp,
		struct xfs_dir2_data_hdr *hdr, int *loghead);
extern void xfs_dir2_data_log_entry(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_entry *dep);
extern void xfs_dir2_data_log_header(struct xfs_da_args *args,
		struct xfs_buf *bp);
extern void xfs_dir2_data_log_unused(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_unused *dup);
extern void xfs_dir2_data_make_free(struct xfs_da_args *args,
		struct xfs_buf *bp, xfs_dir2_data_aoff_t offset,
		xfs_dir2_data_aoff_t len, int *needlogp, int *needscanp);
extern void xfs_dir2_data_use_free(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_unused *dup,
		xfs_dir2_data_aoff_t offset, xfs_dir2_data_aoff_t len,
		int *needlogp, int *needscanp);

extern struct xfs_dir2_data_free *xfs_dir2_data_freefind(
		struct xfs_dir2_data_hdr *hdr, struct xfs_dir2_data_free *bf,
		struct xfs_dir2_data_unused *dup);

extern const struct xfs_buf_ops xfs_dir3_block_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leafn_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leaf1_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_free_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_data_buf_ops;

#endif	/* __XFS_DIR2_H__ */
