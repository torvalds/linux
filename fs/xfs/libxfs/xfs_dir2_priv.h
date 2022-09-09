/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DIR2_PRIV_H__
#define __XFS_DIR2_PRIV_H__

struct dir_context;

/*
 * In-core version of the leaf and free block headers to abstract the
 * differences in the v2 and v3 disk format of the headers.
 */
struct xfs_dir3_icleaf_hdr {
	uint32_t		forw;
	uint32_t		back;
	uint16_t		magic;
	uint16_t		count;
	uint16_t		stale;

	/*
	 * Pointer to the on-disk format entries, which are behind the
	 * variable size (v4 vs v5) header in the on-disk block.
	 */
	struct xfs_dir2_leaf_entry *ents;
};

struct xfs_dir3_icfree_hdr {
	uint32_t		magic;
	uint32_t		firstdb;
	uint32_t		nvalid;
	uint32_t		nused;

	/*
	 * Pointer to the on-disk format entries, which are behind the
	 * variable size (v4 vs v5) header in the on-disk block.
	 */
	__be16			*bests;
};

/* xfs_dir2.c */
xfs_dahash_t xfs_ascii_ci_hashname(const struct xfs_name *name);
enum xfs_dacmp xfs_ascii_ci_compname(struct xfs_da_args *args,
		const unsigned char *name, int len);
extern int xfs_dir2_grow_inode(struct xfs_da_args *args, int space,
				xfs_dir2_db_t *dbp);
extern int xfs_dir_cilookup_result(struct xfs_da_args *args,
				const unsigned char *name, int len);


/* xfs_dir2_block.c */
extern int xfs_dir3_block_read(struct xfs_trans *tp, struct xfs_inode *dp,
			       struct xfs_buf **bpp);
extern int xfs_dir2_block_addname(struct xfs_da_args *args);
extern int xfs_dir2_block_lookup(struct xfs_da_args *args);
extern int xfs_dir2_block_removename(struct xfs_da_args *args);
extern int xfs_dir2_block_replace(struct xfs_da_args *args);
extern int xfs_dir2_leaf_to_block(struct xfs_da_args *args,
		struct xfs_buf *lbp, struct xfs_buf *dbp);

/* xfs_dir2_data.c */
struct xfs_dir2_data_free *xfs_dir2_data_bestfree_p(struct xfs_mount *mp,
		struct xfs_dir2_data_hdr *hdr);
__be16 *xfs_dir2_data_entry_tag_p(struct xfs_mount *mp,
		struct xfs_dir2_data_entry *dep);
uint8_t xfs_dir2_data_get_ftype(struct xfs_mount *mp,
		struct xfs_dir2_data_entry *dep);
void xfs_dir2_data_put_ftype(struct xfs_mount *mp,
		struct xfs_dir2_data_entry *dep, uint8_t ftype);

#ifdef DEBUG
extern void xfs_dir3_data_check(struct xfs_inode *dp, struct xfs_buf *bp);
#else
#define	xfs_dir3_data_check(dp,bp)
#endif

extern xfs_failaddr_t __xfs_dir3_data_check(struct xfs_inode *dp,
		struct xfs_buf *bp);
int xfs_dir3_data_read(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t bno, unsigned int flags, struct xfs_buf **bpp);
int xfs_dir3_data_readahead(struct xfs_inode *dp, xfs_dablk_t bno,
		unsigned int flags);

extern struct xfs_dir2_data_free *
xfs_dir2_data_freeinsert(struct xfs_dir2_data_hdr *hdr,
		struct xfs_dir2_data_free *bf, struct xfs_dir2_data_unused *dup,
		int *loghead);
extern int xfs_dir3_data_init(struct xfs_da_args *args, xfs_dir2_db_t blkno,
		struct xfs_buf **bpp);

/* xfs_dir2_leaf.c */
void xfs_dir2_leaf_hdr_from_disk(struct xfs_mount *mp,
		struct xfs_dir3_icleaf_hdr *to, struct xfs_dir2_leaf *from);
void xfs_dir2_leaf_hdr_to_disk(struct xfs_mount *mp, struct xfs_dir2_leaf *to,
		struct xfs_dir3_icleaf_hdr *from);
int xfs_dir3_leaf_read(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t fbno, struct xfs_buf **bpp);
int xfs_dir3_leafn_read(struct xfs_trans *tp, struct xfs_inode *dp,
		xfs_dablk_t fbno, struct xfs_buf **bpp);
extern int xfs_dir2_block_to_leaf(struct xfs_da_args *args,
		struct xfs_buf *dbp);
extern int xfs_dir2_leaf_addname(struct xfs_da_args *args);
extern void xfs_dir3_leaf_compact(struct xfs_da_args *args,
		struct xfs_dir3_icleaf_hdr *leafhdr, struct xfs_buf *bp);
extern void xfs_dir3_leaf_compact_x1(struct xfs_dir3_icleaf_hdr *leafhdr,
		struct xfs_dir2_leaf_entry *ents, int *indexp,
		int *lowstalep, int *highstalep, int *lowlogp, int *highlogp);
extern int xfs_dir3_leaf_get_buf(struct xfs_da_args *args, xfs_dir2_db_t bno,
		struct xfs_buf **bpp, uint16_t magic);
extern void xfs_dir3_leaf_log_ents(struct xfs_da_args *args,
		struct xfs_dir3_icleaf_hdr *hdr, struct xfs_buf *bp, int first,
		int last);
extern void xfs_dir3_leaf_log_header(struct xfs_da_args *args,
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

extern xfs_failaddr_t xfs_dir3_leaf_check_int(struct xfs_mount *mp,
		struct xfs_dir3_icleaf_hdr *hdr, struct xfs_dir2_leaf *leaf,
		bool expensive_checks);

/* xfs_dir2_node.c */
void xfs_dir2_free_hdr_from_disk(struct xfs_mount *mp,
		struct xfs_dir3_icfree_hdr *to, struct xfs_dir2_free *from);
extern int xfs_dir2_leaf_to_node(struct xfs_da_args *args,
		struct xfs_buf *lbp);
extern xfs_dahash_t xfs_dir2_leaf_lasthash(struct xfs_inode *dp,
		struct xfs_buf *bp, int *count);
extern int xfs_dir2_leafn_lookup_int(struct xfs_buf *bp,
		struct xfs_da_args *args, int *indexp,
		struct xfs_da_state *state);
extern int xfs_dir2_leafn_order(struct xfs_inode *dp, struct xfs_buf *leaf1_bp,
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
xfs_ino_t xfs_dir2_sf_get_ino(struct xfs_mount *mp, struct xfs_dir2_sf_hdr *hdr,
		struct xfs_dir2_sf_entry *sfep);
xfs_ino_t xfs_dir2_sf_get_parent_ino(struct xfs_dir2_sf_hdr *hdr);
void xfs_dir2_sf_put_parent_ino(struct xfs_dir2_sf_hdr *hdr, xfs_ino_t ino);
uint8_t xfs_dir2_sf_get_ftype(struct xfs_mount *mp,
		struct xfs_dir2_sf_entry *sfep);
struct xfs_dir2_sf_entry *xfs_dir2_sf_nextentry(struct xfs_mount *mp,
		struct xfs_dir2_sf_hdr *hdr, struct xfs_dir2_sf_entry *sfep);
extern int xfs_dir2_block_sfsize(struct xfs_inode *dp,
		struct xfs_dir2_data_hdr *block, struct xfs_dir2_sf_hdr *sfhp);
extern int xfs_dir2_block_to_sf(struct xfs_da_args *args, struct xfs_buf *bp,
		int size, xfs_dir2_sf_hdr_t *sfhp);
extern int xfs_dir2_sf_addname(struct xfs_da_args *args);
extern int xfs_dir2_sf_create(struct xfs_da_args *args, xfs_ino_t pino);
extern int xfs_dir2_sf_lookup(struct xfs_da_args *args);
extern int xfs_dir2_sf_removename(struct xfs_da_args *args);
extern int xfs_dir2_sf_replace(struct xfs_da_args *args);
extern xfs_failaddr_t xfs_dir2_sf_verify(struct xfs_inode *ip);
int xfs_dir2_sf_entsize(struct xfs_mount *mp,
		struct xfs_dir2_sf_hdr *hdr, int len);
void xfs_dir2_sf_put_ino(struct xfs_mount *mp, struct xfs_dir2_sf_hdr *hdr,
		struct xfs_dir2_sf_entry *sfep, xfs_ino_t ino);
void xfs_dir2_sf_put_ftype(struct xfs_mount *mp,
		struct xfs_dir2_sf_entry *sfep, uint8_t ftype);

/* xfs_dir2_readdir.c */
extern int xfs_readdir(struct xfs_trans *tp, struct xfs_inode *dp,
		       struct dir_context *ctx, size_t bufsize);

static inline unsigned int
xfs_dir2_data_entsize(
	struct xfs_mount	*mp,
	unsigned int		namelen)
{
	unsigned int		len;

	len = offsetof(struct xfs_dir2_data_entry, name[0]) + namelen +
			sizeof(xfs_dir2_data_off_t) /* tag */;
	if (xfs_has_ftype(mp))
		len += sizeof(uint8_t);
	return round_up(len, XFS_DIR2_DATA_ALIGN);
}

xfs_dahash_t xfs_dir2_hashname(struct xfs_mount *mp,
		const struct xfs_name *name);
enum xfs_dacmp xfs_dir2_compname(struct xfs_da_args *args,
		const unsigned char *name, int len);

#endif /* __XFS_DIR2_PRIV_H__ */
