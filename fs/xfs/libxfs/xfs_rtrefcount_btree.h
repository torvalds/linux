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

#endif	/* __XFS_RTREFCOUNT_BTREE_H__ */
