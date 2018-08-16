// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_SCRUB_SCRUB_H__
#define __XFS_SCRUB_SCRUB_H__

struct xfs_scrub;

/* Type info and names for the scrub types. */
enum xchk_type {
	ST_NONE = 1,	/* disabled */
	ST_PERAG,	/* per-AG metadata */
	ST_FS,		/* per-FS metadata */
	ST_INODE,	/* per-inode metadata */
};

struct xchk_meta_ops {
	/* Acquire whatever resources are needed for the operation. */
	int		(*setup)(struct xfs_scrub *,
				 struct xfs_inode *);

	/* Examine metadata for errors. */
	int		(*scrub)(struct xfs_scrub *);

	/* Repair or optimize the metadata. */
	int		(*repair)(struct xfs_scrub *);

	/* Decide if we even have this piece of metadata. */
	bool		(*has)(struct xfs_sb *);

	/* type describing required/allowed inputs */
	enum xchk_type	type;
};

/* Buffer pointers and btree cursors for an entire AG. */
struct xchk_ag {
	xfs_agnumber_t		agno;
	struct xfs_perag	*pag;

	/* AG btree roots */
	struct xfs_buf		*agf_bp;
	struct xfs_buf		*agfl_bp;
	struct xfs_buf		*agi_bp;

	/* AG btrees */
	struct xfs_btree_cur	*bno_cur;
	struct xfs_btree_cur	*cnt_cur;
	struct xfs_btree_cur	*ino_cur;
	struct xfs_btree_cur	*fino_cur;
	struct xfs_btree_cur	*rmap_cur;
	struct xfs_btree_cur	*refc_cur;
};

struct xfs_scrub {
	/* General scrub state. */
	struct xfs_mount		*mp;
	struct xfs_scrub_metadata	*sm;
	const struct xchk_meta_ops	*ops;
	struct xfs_trans		*tp;
	struct xfs_inode		*ip;
	void				*buf;
	uint				ilock_flags;
	bool				try_harder;
	bool				has_quotaofflock;

	/* State tracking for single-AG operations. */
	struct xchk_ag			sa;
};

/* Metadata scrubbers */
int xchk_tester(struct xfs_scrub *sc);
int xchk_superblock(struct xfs_scrub *sc);
int xchk_agf(struct xfs_scrub *sc);
int xchk_agfl(struct xfs_scrub *sc);
int xchk_agi(struct xfs_scrub *sc);
int xchk_bnobt(struct xfs_scrub *sc);
int xchk_cntbt(struct xfs_scrub *sc);
int xchk_inobt(struct xfs_scrub *sc);
int xchk_finobt(struct xfs_scrub *sc);
int xchk_rmapbt(struct xfs_scrub *sc);
int xchk_refcountbt(struct xfs_scrub *sc);
int xchk_inode(struct xfs_scrub *sc);
int xchk_bmap_data(struct xfs_scrub *sc);
int xchk_bmap_attr(struct xfs_scrub *sc);
int xchk_bmap_cow(struct xfs_scrub *sc);
int xchk_directory(struct xfs_scrub *sc);
int xchk_xattr(struct xfs_scrub *sc);
int xchk_symlink(struct xfs_scrub *sc);
int xchk_parent(struct xfs_scrub *sc);
#ifdef CONFIG_XFS_RT
int xchk_rtbitmap(struct xfs_scrub *sc);
int xchk_rtsummary(struct xfs_scrub *sc);
#else
static inline int
xchk_rtbitmap(struct xfs_scrub *sc)
{
	return -ENOENT;
}
static inline int
xchk_rtsummary(struct xfs_scrub *sc)
{
	return -ENOENT;
}
#endif
#ifdef CONFIG_XFS_QUOTA
int xchk_quota(struct xfs_scrub *sc);
#else
static inline int
xchk_quota(struct xfs_scrub *sc)
{
	return -ENOENT;
}
#endif

/* cross-referencing helpers */
void xchk_xref_is_used_space(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_not_inode_chunk(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_inode_chunk(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_owned_by(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len, struct xfs_owner_info *oinfo);
void xchk_xref_is_not_owned_by(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len, struct xfs_owner_info *oinfo);
void xchk_xref_has_no_owner(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_cow_staging(struct xfs_scrub *sc, xfs_agblock_t bno,
		xfs_extlen_t len);
void xchk_xref_is_not_shared(struct xfs_scrub *sc, xfs_agblock_t bno,
		xfs_extlen_t len);
#ifdef CONFIG_XFS_RT
void xchk_xref_is_used_rt_space(struct xfs_scrub *sc, xfs_rtblock_t rtbno,
		xfs_extlen_t len);
#else
# define xchk_xref_is_used_rt_space(sc, rtbno, len) do { } while (0)
#endif

#endif	/* __XFS_SCRUB_SCRUB_H__ */
