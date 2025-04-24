/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_RTRMAP_BTREE_H__
#define __XFS_RTRMAP_BTREE_H__

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;
struct xbtree_ifakeroot;
struct xfs_rtgroup;
struct xfbtree;

/* rmaps only exist on crc enabled filesystems */
#define XFS_RTRMAP_BLOCK_LEN	XFS_BTREE_LBLOCK_CRC_LEN

struct xfs_btree_cur *xfs_rtrmapbt_init_cursor(struct xfs_trans *tp,
		struct xfs_rtgroup *rtg);
struct xfs_btree_cur *xfs_rtrmapbt_stage_cursor(struct xfs_mount *mp,
		struct xfs_rtgroup *rtg, struct xfs_inode *ip,
		struct xbtree_ifakeroot *ifake);
void xfs_rtrmapbt_commit_staged_btree(struct xfs_btree_cur *cur,
		struct xfs_trans *tp);
unsigned int xfs_rtrmapbt_maxrecs(struct xfs_mount *mp, unsigned int blocklen,
		bool leaf);
void xfs_rtrmapbt_compute_maxlevels(struct xfs_mount *mp);
unsigned int xfs_rtrmapbt_droot_maxrecs(unsigned int blocklen, bool leaf);

/*
 * Addresses of records, keys, and pointers within an incore rtrmapbt block.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
static inline struct xfs_rmap_rec *
xfs_rtrmap_rec_addr(
	struct xfs_btree_block	*block,
	unsigned int		index)
{
	return (struct xfs_rmap_rec *)
		((char *)block + XFS_RTRMAP_BLOCK_LEN +
		 (index - 1) * sizeof(struct xfs_rmap_rec));
}

static inline struct xfs_rmap_key *
xfs_rtrmap_key_addr(
	struct xfs_btree_block	*block,
	unsigned int		index)
{
	return (struct xfs_rmap_key *)
		((char *)block + XFS_RTRMAP_BLOCK_LEN +
		 (index - 1) * 2 * sizeof(struct xfs_rmap_key));
}

static inline struct xfs_rmap_key *
xfs_rtrmap_high_key_addr(
	struct xfs_btree_block	*block,
	unsigned int		index)
{
	return (struct xfs_rmap_key *)
		((char *)block + XFS_RTRMAP_BLOCK_LEN +
		 sizeof(struct xfs_rmap_key) +
		 (index - 1) * 2 * sizeof(struct xfs_rmap_key));
}

static inline xfs_rtrmap_ptr_t *
xfs_rtrmap_ptr_addr(
	struct xfs_btree_block	*block,
	unsigned int		index,
	unsigned int		maxrecs)
{
	return (xfs_rtrmap_ptr_t *)
		((char *)block + XFS_RTRMAP_BLOCK_LEN +
		 maxrecs * 2 * sizeof(struct xfs_rmap_key) +
		 (index - 1) * sizeof(xfs_rtrmap_ptr_t));
}

unsigned int xfs_rtrmapbt_maxlevels_ondisk(void);

int __init xfs_rtrmapbt_init_cur_cache(void);
void xfs_rtrmapbt_destroy_cur_cache(void);

xfs_filblks_t xfs_rtrmapbt_calc_reserves(struct xfs_mount *mp);

/* Addresses of key, pointers, and records within an ondisk rtrmapbt block. */

static inline struct xfs_rmap_rec *
xfs_rtrmap_droot_rec_addr(
	struct xfs_rtrmap_root	*block,
	unsigned int		index)
{
	return (struct xfs_rmap_rec *)
		((char *)(block + 1) +
		 (index - 1) * sizeof(struct xfs_rmap_rec));
}

static inline struct xfs_rmap_key *
xfs_rtrmap_droot_key_addr(
	struct xfs_rtrmap_root	*block,
	unsigned int		index)
{
	return (struct xfs_rmap_key *)
		((char *)(block + 1) +
		 (index - 1) * 2 * sizeof(struct xfs_rmap_key));
}

static inline xfs_rtrmap_ptr_t *
xfs_rtrmap_droot_ptr_addr(
	struct xfs_rtrmap_root	*block,
	unsigned int		index,
	unsigned int		maxrecs)
{
	return (xfs_rtrmap_ptr_t *)
		((char *)(block + 1) +
		 maxrecs * 2 * sizeof(struct xfs_rmap_key) +
		 (index - 1) * sizeof(xfs_rtrmap_ptr_t));
}

/*
 * Address of pointers within the incore btree root.
 *
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
static inline xfs_rtrmap_ptr_t *
xfs_rtrmap_broot_ptr_addr(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*bb,
	unsigned int		index,
	unsigned int		block_size)
{
	return xfs_rtrmap_ptr_addr(bb, index,
			xfs_rtrmapbt_maxrecs(mp, block_size, false));
}

/*
 * Compute the space required for the incore btree root containing the given
 * number of records.
 */
static inline size_t
xfs_rtrmap_broot_space_calc(
	struct xfs_mount	*mp,
	unsigned int		level,
	unsigned int		nrecs)
{
	size_t			sz = XFS_RTRMAP_BLOCK_LEN;

	if (level > 0)
		return sz + nrecs * (2 * sizeof(struct xfs_rmap_key) +
					 sizeof(xfs_rtrmap_ptr_t));
	return sz + nrecs * sizeof(struct xfs_rmap_rec);
}

/*
 * Compute the space required for the incore btree root given the ondisk
 * btree root block.
 */
static inline size_t
xfs_rtrmap_broot_space(struct xfs_mount *mp, struct xfs_rtrmap_root *bb)
{
	return xfs_rtrmap_broot_space_calc(mp, be16_to_cpu(bb->bb_level),
			be16_to_cpu(bb->bb_numrecs));
}

/* Compute the space required for the ondisk root block. */
static inline size_t
xfs_rtrmap_droot_space_calc(
	unsigned int		level,
	unsigned int		nrecs)
{
	size_t			sz = sizeof(struct xfs_rtrmap_root);

	if (level > 0)
		return sz + nrecs * (2 * sizeof(struct xfs_rmap_key) +
					 sizeof(xfs_rtrmap_ptr_t));
	return sz + nrecs * sizeof(struct xfs_rmap_rec);
}

/*
 * Compute the space required for the ondisk root block given an incore root
 * block.
 */
static inline size_t
xfs_rtrmap_droot_space(struct xfs_btree_block *bb)
{
	return xfs_rtrmap_droot_space_calc(be16_to_cpu(bb->bb_level),
			be16_to_cpu(bb->bb_numrecs));
}

int xfs_iformat_rtrmap(struct xfs_inode *ip, struct xfs_dinode *dip);
void xfs_rtrmapbt_to_disk(struct xfs_mount *mp, struct xfs_btree_block *rblock,
		unsigned int rblocklen, struct xfs_rtrmap_root *dblock,
		unsigned int dblocklen);
void xfs_iflush_rtrmap(struct xfs_inode *ip, struct xfs_dinode *dip);

int xfs_rtrmapbt_create(struct xfs_rtgroup *rtg, struct xfs_inode *ip,
		struct xfs_trans *tp, bool init);
int xfs_rtrmapbt_init_rtsb(struct xfs_mount *mp, struct xfs_rtgroup *rtg,
		struct xfs_trans *tp);

unsigned long long xfs_rtrmapbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);

struct xfs_btree_cur *xfs_rtrmapbt_mem_cursor(struct xfs_rtgroup *rtg,
		struct xfs_trans *tp, struct xfbtree *xfbtree);
int xfs_rtrmapbt_mem_init(struct xfs_mount *mp, struct xfbtree *xfbtree,
		struct xfs_buftarg *btp, xfs_rgnumber_t rgno);

xfs_rgblock_t xfs_rtrmap_highest_rgbno(struct xfs_rtgroup *rtg);

#endif /* __XFS_RTRMAP_BTREE_H__ */
