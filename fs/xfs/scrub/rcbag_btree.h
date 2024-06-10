// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_RCBAG_BTREE_H__
#define __XFS_SCRUB_RCBAG_BTREE_H__

#ifdef CONFIG_XFS_BTREE_IN_MEM

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;

#define RCBAG_MAGIC	0x74826671	/* 'JRBG' */

struct rcbag_key {
	uint32_t	rbg_startblock;
	uint32_t	rbg_blockcount;
};

struct rcbag_rec {
	uint32_t	rbg_startblock;
	uint32_t	rbg_blockcount;
	uint64_t	rbg_refcount;
};

typedef __be64 rcbag_ptr_t;

/* reflinks only exist on crc enabled filesystems */
#define RCBAG_BLOCK_LEN	XFS_BTREE_LBLOCK_CRC_LEN

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
#define RCBAG_REC_ADDR(block, index) \
	((struct rcbag_rec *) \
		((char *)(block) + RCBAG_BLOCK_LEN + \
		 (((index) - 1) * sizeof(struct rcbag_rec))))

#define RCBAG_KEY_ADDR(block, index) \
	((struct rcbag_key *) \
		((char *)(block) + RCBAG_BLOCK_LEN + \
		 ((index) - 1) * sizeof(struct rcbag_key)))

#define RCBAG_PTR_ADDR(block, index, maxrecs) \
	((rcbag_ptr_t *) \
		((char *)(block) + RCBAG_BLOCK_LEN + \
		 (maxrecs) * sizeof(struct rcbag_key) + \
		 ((index) - 1) * sizeof(rcbag_ptr_t)))

unsigned int rcbagbt_maxrecs(struct xfs_mount *mp, unsigned int blocklen,
		bool leaf);

unsigned long long rcbagbt_calc_size(unsigned long long nr_records);

unsigned int rcbagbt_maxlevels_possible(void);

int __init rcbagbt_init_cur_cache(void);
void rcbagbt_destroy_cur_cache(void);

struct xfs_btree_cur *rcbagbt_mem_cursor(struct xfs_mount *mp,
		struct xfs_trans *tp, struct xfbtree *xfbtree);
int rcbagbt_mem_init(struct xfs_mount *mp, struct xfbtree *xfbtree,
		struct xfs_buftarg *btp);

int rcbagbt_lookup_eq(struct xfs_btree_cur *cur,
		const struct xfs_rmap_irec *rmap, int *success);
int rcbagbt_get_rec(struct xfs_btree_cur *cur, struct rcbag_rec *rec, int *has);
int rcbagbt_update(struct xfs_btree_cur *cur, const struct rcbag_rec *rec);
int rcbagbt_insert(struct xfs_btree_cur *cur, const struct rcbag_rec *rec,
		int *success);

#else
# define rcbagbt_init_cur_cache()		0
# define rcbagbt_destroy_cur_cache()		((void)0)
#endif /* CONFIG_XFS_BTREE_IN_MEM */

#endif /* __XFS_SCRUB_RCBAG_BTREE_H__ */
