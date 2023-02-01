// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_REFCOUNT_H__
#define __XFS_REFCOUNT_H__

struct xfs_trans;
struct xfs_mount;
struct xfs_perag;
struct xfs_btree_cur;
struct xfs_bmbt_irec;
struct xfs_refcount_irec;

extern int xfs_refcount_lookup_le(struct xfs_btree_cur *cur,
		enum xfs_refc_domain domain, xfs_agblock_t bno, int *stat);
extern int xfs_refcount_lookup_ge(struct xfs_btree_cur *cur,
		enum xfs_refc_domain domain, xfs_agblock_t bno, int *stat);
extern int xfs_refcount_lookup_eq(struct xfs_btree_cur *cur,
		enum xfs_refc_domain domain, xfs_agblock_t bno, int *stat);
extern int xfs_refcount_get_rec(struct xfs_btree_cur *cur,
		struct xfs_refcount_irec *irec, int *stat);

static inline uint32_t
xfs_refcount_encode_startblock(
	xfs_agblock_t		startblock,
	enum xfs_refc_domain	domain)
{
	uint32_t		start;

	/*
	 * low level btree operations need to handle the generic btree range
	 * query functions (which set rc_domain == -1U), so we check that the
	 * domain is /not/ shared.
	 */
	start = startblock & ~XFS_REFC_COWFLAG;
	if (domain != XFS_REFC_DOMAIN_SHARED)
		start |= XFS_REFC_COWFLAG;

	return start;
}

enum xfs_refcount_intent_type {
	XFS_REFCOUNT_INCREASE = 1,
	XFS_REFCOUNT_DECREASE,
	XFS_REFCOUNT_ALLOC_COW,
	XFS_REFCOUNT_FREE_COW,
};

struct xfs_refcount_intent {
	struct list_head			ri_list;
	enum xfs_refcount_intent_type		ri_type;
	xfs_extlen_t				ri_blockcount;
	xfs_fsblock_t				ri_startblock;
};

/* Check that the refcount is appropriate for the record domain. */
static inline bool
xfs_refcount_check_domain(
	const struct xfs_refcount_irec	*irec)
{
	if (irec->rc_domain == XFS_REFC_DOMAIN_COW && irec->rc_refcount != 1)
		return false;
	if (irec->rc_domain == XFS_REFC_DOMAIN_SHARED && irec->rc_refcount < 2)
		return false;
	return true;
}

void xfs_refcount_increase_extent(struct xfs_trans *tp,
		struct xfs_bmbt_irec *irec);
void xfs_refcount_decrease_extent(struct xfs_trans *tp,
		struct xfs_bmbt_irec *irec);

extern void xfs_refcount_finish_one_cleanup(struct xfs_trans *tp,
		struct xfs_btree_cur *rcur, int error);
extern int xfs_refcount_finish_one(struct xfs_trans *tp,
		struct xfs_refcount_intent *ri, struct xfs_btree_cur **pcur);

extern int xfs_refcount_find_shared(struct xfs_btree_cur *cur,
		xfs_agblock_t agbno, xfs_extlen_t aglen, xfs_agblock_t *fbno,
		xfs_extlen_t *flen, bool find_end_of_shared);

void xfs_refcount_alloc_cow_extent(struct xfs_trans *tp, xfs_fsblock_t fsb,
		xfs_extlen_t len);
void xfs_refcount_free_cow_extent(struct xfs_trans *tp, xfs_fsblock_t fsb,
		xfs_extlen_t len);
extern int xfs_refcount_recover_cow_leftovers(struct xfs_mount *mp,
		struct xfs_perag *pag);

/*
 * While we're adjusting the refcounts records of an extent, we have
 * to keep an eye on the number of extents we're dirtying -- run too
 * many in a single transaction and we'll exceed the transaction's
 * reservation and crash the fs.  Each record adds 12 bytes to the
 * log (plus any key updates) so we'll conservatively assume 32 bytes
 * per record.  We must also leave space for btree splits on both ends
 * of the range and space for the CUD and a new CUI.
 *
 * Each EFI that we attach to the transaction is assumed to consume ~32 bytes.
 * This is a low estimate for an EFI tracking a single extent (16 bytes for the
 * EFI header, 16 for the extent, and 12 for the xlog op header), but the
 * estimate is acceptable if there's more than one extent being freed.
 * In the worst case of freeing every other block during a refcount decrease
 * operation, we amortize the space used for one EFI log item across 16
 * extents.
 */
#define XFS_REFCOUNT_ITEM_OVERHEAD	32

extern int xfs_refcount_has_record(struct xfs_btree_cur *cur,
		enum xfs_refc_domain domain, xfs_agblock_t bno,
		xfs_extlen_t len, bool *exists);
union xfs_btree_rec;
extern void xfs_refcount_btrec_to_irec(const union xfs_btree_rec *rec,
		struct xfs_refcount_irec *irec);
extern int xfs_refcount_insert(struct xfs_btree_cur *cur,
		struct xfs_refcount_irec *irec, int *stat);

extern struct kmem_cache	*xfs_refcount_intent_cache;

int __init xfs_refcount_intent_init_cache(void);
void xfs_refcount_intent_destroy_cache(void);

#endif	/* __XFS_REFCOUNT_H__ */
