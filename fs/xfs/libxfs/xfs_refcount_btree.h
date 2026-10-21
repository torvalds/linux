// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_REFCOUNT_BTREE_H__
#define	__XFS_REFCOUNT_BTREE_H__

/*
 * Reference Count Btree on-disk structures
 */

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;
struct xfs_perag;
struct xbtree_afakeroot;
union xfs_btree_key;
union xfs_btree_rec;

/*
 * Btree block header size
 */
#define XFS_REFCOUNT_BLOCK_LEN	XFS_BTREE_SBLOCK_CRC_LEN

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
#define XFS_REFCOUNT_REC_ADDR(block, index) \
	((struct xfs_refcount_rec *) \
		((char *)(block) + \
		 XFS_REFCOUNT_BLOCK_LEN + \
		 (((index) - 1) * sizeof(struct xfs_refcount_rec))))

#define XFS_REFCOUNT_KEY_ADDR(block, index) \
	((struct xfs_refcount_key *) \
		((char *)(block) + \
		 XFS_REFCOUNT_BLOCK_LEN + \
		 ((index) - 1) * sizeof(struct xfs_refcount_key)))

#define XFS_REFCOUNT_PTR_ADDR(block, index, maxrecs) \
	((xfs_refcount_ptr_t *) \
		((char *)(block) + \
		 XFS_REFCOUNT_BLOCK_LEN + \
		 (maxrecs) * sizeof(struct xfs_refcount_key) + \
		 ((index) - 1) * sizeof(xfs_refcount_ptr_t)))

extern struct xfs_btree_cur *xfs_refcountbt_init_cursor(struct xfs_mount *mp,
		struct xfs_trans *tp, struct xfs_buf *agbp,
		struct xfs_perag *pag);
unsigned int xfs_refcountbt_maxrecs(struct xfs_mount *mp, unsigned int blocklen,
		bool leaf);
extern void xfs_refcountbt_compute_maxlevels(struct xfs_mount *mp);

extern xfs_extlen_t xfs_refcountbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);
extern xfs_extlen_t xfs_refcountbt_max_size(struct xfs_mount *mp,
		xfs_agblock_t agblocks);

extern int xfs_refcountbt_calc_reserves(struct xfs_mount *mp,
		struct xfs_trans *tp, struct xfs_perag *pag, xfs_extlen_t *ask,
		xfs_extlen_t *used);

void xfs_refcountbt_commit_staged_btree(struct xfs_btree_cur *cur,
		struct xfs_trans *tp, struct xfs_buf *agbp);

unsigned int xfs_refcountbt_maxlevels_ondisk(void);

int __init xfs_refcountbt_init_cur_cache(void);
void xfs_refcountbt_destroy_cur_cache(void);

/*
 * Key and record btree ops.  The refcount on-disk key/record format is
 * identical for the AG refcount btree and the realtime refcount btree, so
 * these are shared by both.
 */
void xfs_refcountbt_init_key_from_rec(union xfs_btree_key *key,
		const union xfs_btree_rec *rec);
void xfs_refcountbt_init_high_key_from_rec(union xfs_btree_key *key,
		const union xfs_btree_rec *rec);
void xfs_refcountbt_init_rec_from_cur(struct xfs_btree_cur *cur,
		union xfs_btree_rec *rec);
int xfs_refcountbt_cmp_key_with_cur(struct xfs_btree_cur *cur,
		const union xfs_btree_key *key);
int xfs_refcountbt_cmp_two_keys(struct xfs_btree_cur *cur,
		const union xfs_btree_key *k1, const union xfs_btree_key *k2,
		const union xfs_btree_key *mask);
int xfs_refcountbt_keys_inorder(struct xfs_btree_cur *cur,
		const union xfs_btree_key *k1, const union xfs_btree_key *k2);
int xfs_refcountbt_recs_inorder(struct xfs_btree_cur *cur,
		const union xfs_btree_rec *r1, const union xfs_btree_rec *r2);
enum xbtree_key_contig xfs_refcountbt_keys_contiguous(struct xfs_btree_cur *cur,
		const union xfs_btree_key *key1, const union xfs_btree_key *key2,
		const union xfs_btree_key *mask);

#endif	/* __XFS_REFCOUNT_BTREE_H__ */
