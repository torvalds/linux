/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __XFS_SCRUB_SCRUB_H__
#define __XFS_SCRUB_SCRUB_H__

struct xfs_scrub_context;

/* Type info and names for the scrub types. */
enum xfs_scrub_type {
	ST_NONE = 1,	/* disabled */
	ST_PERAG,	/* per-AG metadata */
	ST_FS,		/* per-FS metadata */
	ST_INODE,	/* per-inode metadata */
};

struct xfs_scrub_meta_ops {
	/* Acquire whatever resources are needed for the operation. */
	int		(*setup)(struct xfs_scrub_context *,
				 struct xfs_inode *);

	/* Examine metadata for errors. */
	int		(*scrub)(struct xfs_scrub_context *);

	/* Repair or optimize the metadata. */
	int		(*repair)(struct xfs_scrub_context *);

	/* Decide if we even have this piece of metadata. */
	bool		(*has)(struct xfs_sb *);

	/* type describing required/allowed inputs */
	enum xfs_scrub_type	type;
};

/* Buffer pointers and btree cursors for an entire AG. */
struct xfs_scrub_ag {
	xfs_agnumber_t			agno;
	struct xfs_perag		*pag;

	/* AG btree roots */
	struct xfs_buf			*agf_bp;
	struct xfs_buf			*agfl_bp;
	struct xfs_buf			*agi_bp;

	/* AG btrees */
	struct xfs_btree_cur		*bno_cur;
	struct xfs_btree_cur		*cnt_cur;
	struct xfs_btree_cur		*ino_cur;
	struct xfs_btree_cur		*fino_cur;
	struct xfs_btree_cur		*rmap_cur;
	struct xfs_btree_cur		*refc_cur;
};

struct xfs_scrub_context {
	/* General scrub state. */
	struct xfs_mount		*mp;
	struct xfs_scrub_metadata	*sm;
	const struct xfs_scrub_meta_ops	*ops;
	struct xfs_trans		*tp;
	struct xfs_inode		*ip;
	void				*buf;
	uint				ilock_flags;
	bool				try_harder;
	bool				has_quotaofflock;

	/* State tracking for single-AG operations. */
	struct xfs_scrub_ag		sa;
};

/* Metadata scrubbers */
int xfs_scrub_tester(struct xfs_scrub_context *sc);
int xfs_scrub_superblock(struct xfs_scrub_context *sc);
int xfs_scrub_agf(struct xfs_scrub_context *sc);
int xfs_scrub_agfl(struct xfs_scrub_context *sc);
int xfs_scrub_agi(struct xfs_scrub_context *sc);
int xfs_scrub_bnobt(struct xfs_scrub_context *sc);
int xfs_scrub_cntbt(struct xfs_scrub_context *sc);
int xfs_scrub_inobt(struct xfs_scrub_context *sc);
int xfs_scrub_finobt(struct xfs_scrub_context *sc);
int xfs_scrub_rmapbt(struct xfs_scrub_context *sc);
int xfs_scrub_refcountbt(struct xfs_scrub_context *sc);
int xfs_scrub_inode(struct xfs_scrub_context *sc);
int xfs_scrub_bmap_data(struct xfs_scrub_context *sc);
int xfs_scrub_bmap_attr(struct xfs_scrub_context *sc);
int xfs_scrub_bmap_cow(struct xfs_scrub_context *sc);
int xfs_scrub_directory(struct xfs_scrub_context *sc);
int xfs_scrub_xattr(struct xfs_scrub_context *sc);
int xfs_scrub_symlink(struct xfs_scrub_context *sc);
int xfs_scrub_parent(struct xfs_scrub_context *sc);
#ifdef CONFIG_XFS_RT
int xfs_scrub_rtbitmap(struct xfs_scrub_context *sc);
int xfs_scrub_rtsummary(struct xfs_scrub_context *sc);
#else
static inline int
xfs_scrub_rtbitmap(struct xfs_scrub_context *sc)
{
	return -ENOENT;
}
static inline int
xfs_scrub_rtsummary(struct xfs_scrub_context *sc)
{
	return -ENOENT;
}
#endif
#ifdef CONFIG_XFS_QUOTA
int xfs_scrub_quota(struct xfs_scrub_context *sc);
#else
static inline int
xfs_scrub_quota(struct xfs_scrub_context *sc)
{
	return -ENOENT;
}
#endif

/* cross-referencing helpers */
void xfs_scrub_xref_is_used_space(struct xfs_scrub_context *sc,
		xfs_agblock_t agbno, xfs_extlen_t len);
void xfs_scrub_xref_is_not_inode_chunk(struct xfs_scrub_context *sc,
		xfs_agblock_t agbno, xfs_extlen_t len);
void xfs_scrub_xref_is_inode_chunk(struct xfs_scrub_context *sc,
		xfs_agblock_t agbno, xfs_extlen_t len);
void xfs_scrub_xref_is_owned_by(struct xfs_scrub_context *sc,
		xfs_agblock_t agbno, xfs_extlen_t len,
		struct xfs_owner_info *oinfo);
void xfs_scrub_xref_is_not_owned_by(struct xfs_scrub_context *sc,
		xfs_agblock_t agbno, xfs_extlen_t len,
		struct xfs_owner_info *oinfo);
void xfs_scrub_xref_has_no_owner(struct xfs_scrub_context *sc,
		xfs_agblock_t agbno, xfs_extlen_t len);
void xfs_scrub_xref_is_cow_staging(struct xfs_scrub_context *sc,
		xfs_agblock_t bno, xfs_extlen_t len);
void xfs_scrub_xref_is_not_shared(struct xfs_scrub_context *sc,
		xfs_agblock_t bno, xfs_extlen_t len);
#ifdef CONFIG_XFS_RT
void xfs_scrub_xref_is_used_rt_space(struct xfs_scrub_context *sc,
		xfs_rtblock_t rtbno, xfs_extlen_t len);
#else
# define xfs_scrub_xref_is_used_rt_space(sc, rtbno, len) do { } while (0)
#endif

#endif	/* __XFS_SCRUB_SCRUB_H__ */
