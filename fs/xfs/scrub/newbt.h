// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_NEWBT_H__
#define __XFS_SCRUB_NEWBT_H__

struct xrep_newbt_resv {
	/* Link to list of extents that we've reserved. */
	struct list_head	list;

	struct xfs_perag	*pag;

	/* Auto-freeing this reservation if we don't commit. */
	struct xfs_alloc_autoreap autoreap;

	/* AG block of the extent we reserved. */
	xfs_agblock_t		agbno;

	/* Length of the reservation. */
	xfs_extlen_t		len;

	/* How much of this reservation has been used. */
	xfs_extlen_t		used;
};

struct xrep_newbt {
	struct xfs_scrub	*sc;

	/* List of extents that we've reserved. */
	struct list_head	resv_list;

	/* Fake root for new btree. */
	union {
		struct xbtree_afakeroot	afake;
		struct xbtree_ifakeroot	ifake;
	};

	/* rmap owner of these blocks */
	struct xfs_owner_info	oinfo;

	/* btree geometry for the bulk loader */
	struct xfs_btree_bload	bload;

	/* Allocation hint */
	xfs_fsblock_t		alloc_hint;

	/* per-ag reservation type */
	enum xfs_ag_resv_type	resv;
};

void xrep_newbt_init_bare(struct xrep_newbt *xnr, struct xfs_scrub *sc);
void xrep_newbt_init_ag(struct xrep_newbt *xnr, struct xfs_scrub *sc,
		const struct xfs_owner_info *oinfo, xfs_fsblock_t alloc_hint,
		enum xfs_ag_resv_type resv);
int xrep_newbt_init_inode(struct xrep_newbt *xnr, struct xfs_scrub *sc,
		int whichfork, const struct xfs_owner_info *oinfo);
int xrep_newbt_alloc_blocks(struct xrep_newbt *xnr, uint64_t nr_blocks);
int xrep_newbt_add_extent(struct xrep_newbt *xnr, struct xfs_perag *pag,
		xfs_agblock_t agbno, xfs_extlen_t len);
void xrep_newbt_cancel(struct xrep_newbt *xnr);
int xrep_newbt_commit(struct xrep_newbt *xnr);
int xrep_newbt_claim_block(struct xfs_btree_cur *cur, struct xrep_newbt *xnr,
		union xfs_btree_ptr *ptr);
unsigned int xrep_newbt_unused_blocks(struct xrep_newbt *xnr);

#endif /* __XFS_SCRUB_NEWBT_H__ */
