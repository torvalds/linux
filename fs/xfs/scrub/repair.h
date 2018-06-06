// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_SCRUB_REPAIR_H__
#define __XFS_SCRUB_REPAIR_H__

static inline int xfs_repair_notsupported(struct xfs_scrub_context *sc)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR

/* Repair helpers */

int xfs_repair_attempt(struct xfs_inode *ip, struct xfs_scrub_context *sc,
		bool *fixed);
void xfs_repair_failure(struct xfs_mount *mp);
int xfs_repair_roll_ag_trans(struct xfs_scrub_context *sc);
bool xfs_repair_ag_has_space(struct xfs_perag *pag, xfs_extlen_t nr_blocks,
		enum xfs_ag_resv_type type);
xfs_extlen_t xfs_repair_calc_ag_resblks(struct xfs_scrub_context *sc);
int xfs_repair_alloc_ag_block(struct xfs_scrub_context *sc,
		struct xfs_owner_info *oinfo, xfs_fsblock_t *fsbno,
		enum xfs_ag_resv_type resv);
int xfs_repair_init_btblock(struct xfs_scrub_context *sc, xfs_fsblock_t fsb,
		struct xfs_buf **bpp, xfs_btnum_t btnum,
		const struct xfs_buf_ops *ops);

struct xfs_repair_extent {
	struct list_head		list;
	xfs_fsblock_t			fsbno;
	xfs_extlen_t			len;
};

struct xfs_repair_extent_list {
	struct list_head		list;
};

static inline void
xfs_repair_init_extent_list(
	struct xfs_repair_extent_list	*exlist)
{
	INIT_LIST_HEAD(&exlist->list);
}

#define for_each_xfs_repair_extent_safe(rbe, n, exlist) \
	list_for_each_entry_safe((rbe), (n), &(exlist)->list, list)
int xfs_repair_collect_btree_extent(struct xfs_scrub_context *sc,
		struct xfs_repair_extent_list *btlist, xfs_fsblock_t fsbno,
		xfs_extlen_t len);
void xfs_repair_cancel_btree_extents(struct xfs_scrub_context *sc,
		struct xfs_repair_extent_list *btlist);
int xfs_repair_subtract_extents(struct xfs_scrub_context *sc,
		struct xfs_repair_extent_list *exlist,
		struct xfs_repair_extent_list *sublist);
int xfs_repair_fix_freelist(struct xfs_scrub_context *sc, bool can_shrink);
int xfs_repair_invalidate_blocks(struct xfs_scrub_context *sc,
		struct xfs_repair_extent_list *btlist);
int xfs_repair_reap_btree_extents(struct xfs_scrub_context *sc,
		struct xfs_repair_extent_list *exlist,
		struct xfs_owner_info *oinfo, enum xfs_ag_resv_type type);

struct xfs_repair_find_ag_btree {
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

int xfs_repair_find_ag_btree_roots(struct xfs_scrub_context *sc,
		struct xfs_buf *agf_bp,
		struct xfs_repair_find_ag_btree *btree_info,
		struct xfs_buf *agfl_bp);
void xfs_repair_force_quotacheck(struct xfs_scrub_context *sc, uint dqtype);
int xfs_repair_ino_dqattach(struct xfs_scrub_context *sc);

/* Metadata repairers */

int xfs_repair_probe(struct xfs_scrub_context *sc);
int xfs_repair_superblock(struct xfs_scrub_context *sc);

#else

static inline int xfs_repair_attempt(
	struct xfs_inode		*ip,
	struct xfs_scrub_context	*sc,
	bool				*fixed)
{
	return -EOPNOTSUPP;
}

static inline void xfs_repair_failure(struct xfs_mount *mp) {}

static inline xfs_extlen_t
xfs_repair_calc_ag_resblks(
	struct xfs_scrub_context	*sc)
{
	ASSERT(!(sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR));
	return 0;
}

#define xfs_repair_probe		xfs_repair_notsupported
#define xfs_repair_superblock		xfs_repair_notsupported

#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif	/* __XFS_SCRUB_REPAIR_H__ */
