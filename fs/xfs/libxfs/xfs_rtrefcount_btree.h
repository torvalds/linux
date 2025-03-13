/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_RTREFCOUNT_BTREE_H__
#define __XFS_RTREFCOUNT_BTREE_H__

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;
struct xbtree_ifakeroot;
struct xfs_rtgroup;

/* refcounts only exist on crc enabled filesystems */
#define XFS_RTREFCOUNT_BLOCK_LEN	XFS_BTREE_LBLOCK_CRC_LEN

struct xfs_btree_cur *xfs_rtrefcountbt_init_cursor(struct xfs_trans *tp,
		struct xfs_rtgroup *rtg);
struct xfs_btree_cur *xfs_rtrefcountbt_stage_cursor(struct xfs_mount *mp,
		struct xfs_rtgroup *rtg, struct xfs_inode *ip,
		struct xbtree_ifakeroot *ifake);
void xfs_rtrefcountbt_commit_staged_btree(struct xfs_btree_cur *cur,
		struct xfs_trans *tp);
unsigned int xfs_rtrefcountbt_maxrecs(struct xfs_mount *mp,
		unsigned int blocklen, bool leaf);
void xfs_rtrefcountbt_compute_maxlevels(struct xfs_mount *mp);
unsigned int xfs_rtrefcountbt_droot_maxrecs(unsigned int blocklen, bool leaf);

/*
 * Addresses of records, keys, and pointers within an incore rtrefcountbt block.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
static inline struct xfs_refcount_rec *
xfs_rtrefcount_rec_addr(
	struct xfs_btree_block	*block,
	unsigned int		index)
{
	return (struct xfs_refcount_rec *)
		((char *)block + XFS_RTREFCOUNT_BLOCK_LEN +
		 (index - 1) * sizeof(struct xfs_refcount_rec));
}

static inline struct xfs_refcount_key *
xfs_rtrefcount_key_addr(
	struct xfs_btree_block	*block,
	unsigned int		index)
{
	return (struct xfs_refcount_key *)
		((char *)block + XFS_RTREFCOUNT_BLOCK_LEN +
		 (index - 1) * sizeof(struct xfs_refcount_key));
}

static inline xfs_rtrefcount_ptr_t *
xfs_rtrefcount_ptr_addr(
	struct xfs_btree_block	*block,
	unsigned int		index,
	unsigned int		maxrecs)
{
	return (xfs_rtrefcount_ptr_t *)
		((char *)block + XFS_RTREFCOUNT_BLOCK_LEN +
		 maxrecs * sizeof(struct xfs_refcount_key) +
		 (index - 1) * sizeof(xfs_rtrefcount_ptr_t));
}

unsigned int xfs_rtrefcountbt_maxlevels_ondisk(void);
int __init xfs_rtrefcountbt_init_cur_cache(void);
void xfs_rtrefcountbt_destroy_cur_cache(void);

xfs_filblks_t xfs_rtrefcountbt_calc_reserves(struct xfs_mount *mp);
unsigned long long xfs_rtrefcountbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);

/* Addresses of key, pointers, and records within an ondisk rtrefcount block. */

static inline struct xfs_refcount_rec *
xfs_rtrefcount_droot_rec_addr(
	struct xfs_rtrefcount_root	*block,
	unsigned int			index)
{
	return (struct xfs_refcount_rec *)
		((char *)(block + 1) +
		 (index - 1) * sizeof(struct xfs_refcount_rec));
}

static inline struct xfs_refcount_key *
xfs_rtrefcount_droot_key_addr(
	struct xfs_rtrefcount_root	*block,
	unsigned int			index)
{
	return (struct xfs_refcount_key *)
		((char *)(block + 1) +
		 (index - 1) * sizeof(struct xfs_refcount_key));
}

static inline xfs_rtrefcount_ptr_t *
xfs_rtrefcount_droot_ptr_addr(
	struct xfs_rtrefcount_root	*block,
	unsigned int			index,
	unsigned int			maxrecs)
{
	return (xfs_rtrefcount_ptr_t *)
		((char *)(block + 1) +
		 maxrecs * sizeof(struct xfs_refcount_key) +
		 (index - 1) * sizeof(xfs_rtrefcount_ptr_t));
}

/*
 * Address of pointers within the incore btree root.
 *
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
static inline xfs_rtrefcount_ptr_t *
xfs_rtrefcount_broot_ptr_addr(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*bb,
	unsigned int		index,
	unsigned int		block_size)
{
	return xfs_rtrefcount_ptr_addr(bb, index,
			xfs_rtrefcountbt_maxrecs(mp, block_size, false));
}

/*
 * Compute the space required for the incore btree root containing the given
 * number of records.
 */
static inline size_t
xfs_rtrefcount_broot_space_calc(
	struct xfs_mount	*mp,
	unsigned int		level,
	unsigned int		nrecs)
{
	size_t			sz = XFS_RTREFCOUNT_BLOCK_LEN;

	if (level > 0)
		return sz + nrecs * (sizeof(struct xfs_refcount_key) +
				     sizeof(xfs_rtrefcount_ptr_t));
	return sz + nrecs * sizeof(struct xfs_refcount_rec);
}

/*
 * Compute the space required for the incore btree root given the ondisk
 * btree root block.
 */
static inline size_t
xfs_rtrefcount_broot_space(struct xfs_mount *mp, struct xfs_rtrefcount_root *bb)
{
	return xfs_rtrefcount_broot_space_calc(mp, be16_to_cpu(bb->bb_level),
			be16_to_cpu(bb->bb_numrecs));
}

/* Compute the space required for the ondisk root block. */
static inline size_t
xfs_rtrefcount_droot_space_calc(
	unsigned int		level,
	unsigned int		nrecs)
{
	size_t			sz = sizeof(struct xfs_rtrefcount_root);

	if (level > 0)
		return sz + nrecs * (sizeof(struct xfs_refcount_key) +
				     sizeof(xfs_rtrefcount_ptr_t));
	return sz + nrecs * sizeof(struct xfs_refcount_rec);
}

/*
 * Compute the space required for the ondisk root block given an incore root
 * block.
 */
static inline size_t
xfs_rtrefcount_droot_space(struct xfs_btree_block *bb)
{
	return xfs_rtrefcount_droot_space_calc(be16_to_cpu(bb->bb_level),
			be16_to_cpu(bb->bb_numrecs));
}

int xfs_iformat_rtrefcount(struct xfs_inode *ip, struct xfs_dinode *dip);
void xfs_rtrefcountbt_to_disk(struct xfs_mount *mp,
		struct xfs_btree_block *rblock, int rblocklen,
		struct xfs_rtrefcount_root *dblock, int dblocklen);
void xfs_iflush_rtrefcount(struct xfs_inode *ip, struct xfs_dinode *dip);

int xfs_rtrefcountbt_create(struct xfs_rtgroup *rtg, struct xfs_inode *ip,
		struct xfs_trans *tp, bool init);

#endif	/* __XFS_RTREFCOUNT_BTREE_H__ */
