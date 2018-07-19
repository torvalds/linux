// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_SCRUB_REPAIR_H__
#define __XFS_SCRUB_REPAIR_H__

static inline int xrep_notsupported(struct xfs_scrub_context *sc)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR

/* Repair helpers */

int xrep_attempt(struct xfs_inode *ip, struct xfs_scrub_context *sc,
		bool *fixed);
void xrep_failure(struct xfs_mount *mp);
int xrep_roll_ag_trans(struct xfs_scrub_context *sc);
bool xrep_ag_has_space(struct xfs_perag *pag, xfs_extlen_t nr_blocks,
		enum xfs_ag_resv_type type);
xfs_extlen_t xrep_calc_ag_resblks(struct xfs_scrub_context *sc);
int xrep_alloc_ag_block(struct xfs_scrub_context *sc,
		struct xfs_owner_info *oinfo, xfs_fsblock_t *fsbno,
		enum xfs_ag_resv_type resv);
int xrep_init_btblock(struct xfs_scrub_context *sc, xfs_fsblock_t fsb,
		struct xfs_buf **bpp, xfs_btnum_t btnum,
		const struct xfs_buf_ops *ops);

struct xrep_extent {
	struct list_head		list;
	xfs_fsblock_t			fsbno;
	xfs_extlen_t			len;
};

struct xrep_extent_list {
	struct list_head		list;
};

static inline void
xrep_init_extent_list(
	struct xrep_extent_list		*exlist)
{
	INIT_LIST_HEAD(&exlist->list);
}

#define for_each_xrep_extent_safe(rbe, n, exlist) \
	list_for_each_entry_safe((rbe), (n), &(exlist)->list, list)
int xrep_collect_btree_extent(struct xfs_scrub_context *sc,
		struct xrep_extent_list *btlist, xfs_fsblock_t fsbno,
		xfs_extlen_t len);
void xrep_cancel_btree_extents(struct xfs_scrub_context *sc,
		struct xrep_extent_list *btlist);
int xrep_subtract_extents(struct xfs_scrub_context *sc,
		struct xrep_extent_list *exlist,
		struct xrep_extent_list *sublist);
int xrep_fix_freelist(struct xfs_scrub_context *sc, bool can_shrink);
int xrep_invalidate_blocks(struct xfs_scrub_context *sc,
		struct xrep_extent_list *btlist);
int xrep_reap_btree_extents(struct xfs_scrub_context *sc,
		struct xrep_extent_list *exlist,
		struct xfs_owner_info *oinfo, enum xfs_ag_resv_type type);

struct xrep_find_ag_btree {
	/* in: rmap owner of the btree we're looking for */
	uint64_t			rmap_owner;

	/* in: buffer ops */
	const struct xfs_buf_ops	*buf_ops;

	/* in: magic number of the btree */
	uint32_t			magic;

	/* out: the highest btree block found and the tree height */
	xfs_agblock_t			root;
	unsigned int			height;
};

int xrep_find_ag_btree_roots(struct xfs_scrub_context *sc,
		struct xfs_buf *agf_bp,
		struct xrep_find_ag_btree *btree_info,
		struct xfs_buf *agfl_bp);
void xrep_force_quotacheck(struct xfs_scrub_context *sc, uint dqtype);
int xrep_ino_dqattach(struct xfs_scrub_context *sc);

/* Metadata repairers */

int xrep_probe(struct xfs_scrub_context *sc);
int xrep_superblock(struct xfs_scrub_context *sc);

#else

static inline int xrep_attempt(
	struct xfs_inode		*ip,
	struct xfs_scrub_context	*sc,
	bool				*fixed)
{
	return -EOPNOTSUPP;
}

static inline void xrep_failure(struct xfs_mount *mp) {}

static inline xfs_extlen_t
xrep_calc_ag_resblks(
	struct xfs_scrub_context	*sc)
{
	ASSERT(!(sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR));
	return 0;
}

#define xrep_probe			xrep_notsupported
#define xrep_superblock			xrep_notsupported

#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif	/* __XFS_SCRUB_REPAIR_H__ */
