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
#ifndef __XFS_DIR2_PRIV_H__
#define __XFS_DIR2_PRIV_H__

/* xfs_dir2.c */
extern int xfs_dir_ino_validate(struct xfs_mount *mp, xfs_ino_t ino);
extern int xfs_dir2_isblock(struct xfs_trans *tp, struct xfs_inode *dp, int *r);
extern int xfs_dir2_isleaf(struct xfs_trans *tp, struct xfs_inode *dp, int *r);
extern int xfs_dir2_grow_inode(struct xfs_da_args *args, int space,
				xfs_dir2_db_t *dbp);
extern int xfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_buf *bp);
extern int xfs_dir_cilookup_result(struct xfs_da_args *args,
				const unsigned char *name, int len);

/* xfs_dir2_block.c */
extern const struct xfs_buf_ops xfs_dir3_block_buf_ops;

extern int xfs_dir2_block_addname(struct xfs_da_args *args);
extern int xfs_dir2_block_getdents(struct xfs_inode *dp,
		struct dir_context *ctx);
extern int xfs_dir2_block_lookup(struct xfs_da_args *args);
extern int xfs_dir2_block_removename(struct xfs_da_args *args);
extern int xfs_dir2_block_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_to_block(struct xfs_da_args *args,
		struct xfs_buf *lbp, struct xfs_buf *dbp);

/* xfs_dir2_data.c */
#ifdef DEBUG
#define	xfs_dir3_data_check(dp,bp) __xfs_dir3_data_check(dp, bp);
#else
#define	xfs_dir3_data_check(dp,bp)
#endif

extern const struct xfs_buf_ops xfs_dir3_data_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_free_buf_ops;

extern int __xfs_dir3_data_check(struct xfs_inode *dp, struct xfs_buf *bp);
extern int xfs_dir3_data_read(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t bno, xfs_daddr_t mapped_bno, struct xfs_buf **bpp);
extern int xfs_dir3_data_readahead(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t bno, xfs_daddr_t mapped_bno);

extern struct xfs_dir2_data_free *
xfs_dir2_data_freeinsert(struct xfs_dir2_data_hdr *hdr,
		struct xfs_dir2_data_unused *dup, int *loghead);
extern void xfs_dir2_data_freescan(struct xfs_mount *mp,
		struct xfs_dir2_data_hdr *hdr, int *loghead);
extern int xfs_dir3_data_init(struct xfs_da_args *args, xfs_dir2_db_t blkno,
		struct xfs_buf **bpp);
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

/* xfs_dir2_leaf.c */
extern const struct xfs_buf_ops xfs_dir3_leaf1_buf_ops;
extern const struct xfs_buf_ops xfs_dir3_leafn_buf_ops;

extern int xfs_dir3_leafn_read(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t fbno, xfs_daddr_t mappedbno, struct xfs_buf **bpp);
extern int xfs_dir2_block_to_leaf(struct xfs_da_args *args,
		struct xfs_buf *dbp);
extern int xfs_dir2_leaf_addname(struct xfs_da_args *args);
extern void xfs_dir3_leaf_compact(struct xfs_da_args *args,
		struct xfs_dir3_icleaf_hdr *leafhdr, struct xfs_buf *bp);
extern void xfs_dir3_leaf_compact_x1(struct xfs_dir3_icleaf_hdr *leafhdr,
		struct xfs_dir2_leaf_entry *ents, int *indexp,
		int *lowstalep, int *highstalep, int *lowlogp, int *highlogp);
extern int xfs_dir2_leaf_getdents(struct xfs_inode *dp, struct dir_context *ctx,
		size_t bufsize);
extern int xfs_dir3_leaf_get_buf(struct xfs_da_args *args, xfs_dir2_db_t bno,
		struct xfs_buf **bpp, __uint16_t magic);
extern void xfs_dir3_leaf_log_ents(struct xfs_trans *tp, struct xfs_buf *bp,
		int first, int last);
extern void xfs_dir3_leaf_log_header(struct xfs_trans *tp,
		struct xfs_buf *bp);
extern int xfs_dir2_leaf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_leaf_removename(struct xfs_da_args *args);
extern int xfs_dir2_leaf_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_search_hash(struct xfs_da_args *args,
		struct xfs_buf *lbp);
extern int xfs_dir2_leaf_trim_data(struct xfs_da_args *args,
		struct xfs_buf *lbp, xfs_dir2_db_t db);
extern struct xfs_dir2_leaf_entry *
xfs_dir3_leaf_find_entry(struct xfs_dir3_icleaf_hdr *leafhdr,
		struct xfs_dir2_leaf_entry *ents, int index, int compact,
		int lowstale, int highstale, int *lfloglow, int *lfloghigh);
extern int xfs_dir2_node_to_leaf(struct xfs_da_state *state);

extern void xfs_dir3_leaf_hdr_from_disk(struct xfs_dir3_icleaf_hdr *to,
		struct xfs_dir2_leaf *from);
extern void xfs_dir3_leaf_hdr_to_disk(struct xfs_dir2_leaf *to,
		struct xfs_dir3_icleaf_hdr *from);
extern bool xfs_dir3_leaf_check_int(struct xfs_mount *mp,
		struct xfs_dir3_icleaf_hdr *hdr, struct xfs_dir2_leaf *leaf);

/* xfs_dir2_node.c */
extern int xfs_dir2_leaf_to_node(struct xfs_da_args *args,
		struct xfs_buf *lbp);
extern xfs_dahash_t xfs_dir2_leafn_lasthash(struct xfs_buf *bp, int *count);
extern int xfs_dir2_leafn_lookup_int(struct xfs_buf *bp,
		struct xfs_da_args *args, int *indexp,
		struct xfs_da_state *state);
extern int xfs_dir2_leafn_order(struct xfs_buf *leaf1_bp,
		struct xfs_buf *leaf2_bp);
extern int xfs_dir2_leafn_split(struct xfs_da_state *state,
	struct xfs_da_state_blk *oldblk, struct xfs_da_state_blk *newblk);
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
extern int xfs_dir2_free_read(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t fbno, struct xfs_buf **bpp);

/* xfs_dir2_sf.c */
extern xfs_ino_t xfs_dir2_sf_get_parent_ino(struct xfs_dir2_sf_hdr *sfp);
extern xfs_ino_t xfs_dir2_sfe_get_ino(struct xfs_dir2_sf_hdr *sfp,
		struct xfs_dir2_sf_entry *sfep);
extern int xfs_dir2_block_sfsize(struct xfs_inode *dp,
		struct xfs_dir2_data_hdr *block, struct xfs_dir2_sf_hdr *sfhp);
extern int xfs_dir2_block_to_sf(struct xfs_da_args *args, struct xfs_buf *bp,
		int size, xfs_dir2_sf_hdr_t *sfhp);
extern int xfs_dir2_sf_addname(struct xfs_da_args *args);
extern int xfs_dir2_sf_create(struct xfs_da_args *args, xfs_ino_t pino);
extern int xfs_dir2_sf_getdents(struct xfs_inode *dp, struct dir_context *ctx);
extern int xfs_dir2_sf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_sf_removename(struct xfs_da_args *args);
extern int xfs_dir2_sf_replace(struct xfs_da_args *args);

#endif /* __XFS_DIR2_PRIV_H__ */
