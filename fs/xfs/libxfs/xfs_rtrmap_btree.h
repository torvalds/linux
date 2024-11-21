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

#endif /* __XFS_RTRMAP_BTREE_H__ */
