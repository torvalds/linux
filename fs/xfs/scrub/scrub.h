// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_SCRUB_H__
#define __XFS_SCRUB_SCRUB_H__

struct xfs_scrub;

/*
 * Standard flags for allocating memory within scrub.  NOFS context is
 * configured by the process allocation scope.  Scrub and repair must be able
 * to back out gracefully if there isn't enough memory.  Force-cast to avoid
 * complaints from static checkers.
 */
#define XCHK_GFP_FLAGS	((__force gfp_t)(GFP_KERNEL | __GFP_NOWARN | \
					 __GFP_RETRY_MAYFAIL))

/* Type info and names for the scrub types. */
enum xchk_type {
	ST_NONE = 1,	/* disabled */
	ST_PERAG,	/* per-AG metadata */
	ST_FS,		/* per-FS metadata */
	ST_INODE,	/* per-inode metadata */
};

struct xchk_meta_ops {
	/* Acquire whatever resources are needed for the operation. */
	int		(*setup)(struct xfs_scrub *sc);

	/* Examine metadata for errors. */
	int		(*scrub)(struct xfs_scrub *);

	/* Repair or optimize the metadata. */
	int		(*repair)(struct xfs_scrub *);

	/*
	 * Re-scrub the metadata we repaired, in case there's extra work that
	 * we need to do to check our repair work.  If this is NULL, we'll use
	 * the ->scrub function pointer, assuming that the regular scrub is
	 * sufficient.
	 */
	int		(*repair_eval)(struct xfs_scrub *sc);

	/* Decide if we even have this piece of metadata. */
	bool		(*has)(struct xfs_mount *);

	/* type describing required/allowed inputs */
	enum xchk_type	type;
};

/* Buffer pointers and btree cursors for an entire AG. */
struct xchk_ag {
	struct xfs_perag	*pag;

	/* AG btree roots */
	struct xfs_buf		*agf_bp;
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

	/* File that scrub was called with. */
	struct file			*file;

	/*
	 * File that is undergoing the scrub operation.  This can differ from
	 * the file that scrub was called with if we're checking file-based fs
	 * metadata (e.g. rt bitmaps) or if we're doing a scrub-by-handle for
	 * something that can't be opened directly (e.g. symlinks).
	 */
	struct xfs_inode		*ip;

	/* Kernel memory buffer used by scrubbers; freed at teardown. */
	void				*buf;

	/*
	 * Clean up resources owned by whatever is in the buffer.  Cleanup can
	 * be deferred with this hook as a means for scrub functions to pass
	 * data to repair functions.  This function must not free the buffer
	 * itself.
	 */
	void				(*buf_cleanup)(void *buf);

	/* xfile used by the scrubbers; freed at teardown. */
	struct xfile			*xfile;

	/* Lock flags for @ip. */
	uint				ilock_flags;

	/* See the XCHK/XREP state flags below. */
	unsigned int			flags;

	/*
	 * The XFS_SICK_* flags that correspond to the metadata being scrubbed
	 * or repaired.  We will use this mask to update the in-core fs health
	 * status with whatever we find.
	 */
	unsigned int			sick_mask;

	/* State tracking for single-AG operations. */
	struct xchk_ag			sa;
};

/* XCHK state flags grow up from zero, XREP state flags grown down from 2^31 */
#define XCHK_TRY_HARDER		(1U << 0)  /* can't get resources, try again */
#define XCHK_HAVE_FREEZE_PROT	(1U << 1)  /* do we have freeze protection? */
#define XCHK_FSGATES_DRAIN	(1U << 2)  /* defer ops draining enabled */
#define XCHK_NEED_DRAIN		(1U << 3)  /* scrub needs to drain defer ops */
#define XREP_RESET_PERAG_RESV	(1U << 30) /* must reset AG space reservation */
#define XREP_ALREADY_FIXED	(1U << 31) /* checking our repair work */

/*
 * The XCHK_FSGATES* flags reflect functionality in the main filesystem that
 * are only enabled for this particular online fsck.  When not in use, the
 * features are gated off via dynamic code patching, which is why the state
 * must be enabled during scrub setup and can only be torn down afterwards.
 */
#define XCHK_FSGATES_ALL	(XCHK_FSGATES_DRAIN)

/* Metadata scrubbers */
int xchk_tester(struct xfs_scrub *sc);
int xchk_superblock(struct xfs_scrub *sc);
int xchk_agf(struct xfs_scrub *sc);
int xchk_agfl(struct xfs_scrub *sc);
int xchk_agi(struct xfs_scrub *sc);
int xchk_allocbt(struct xfs_scrub *sc);
int xchk_iallocbt(struct xfs_scrub *sc);
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
int xchk_fscounters(struct xfs_scrub *sc);

/* cross-referencing helpers */
void xchk_xref_is_used_space(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_not_inode_chunk(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_inode_chunk(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_only_owned_by(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len, const struct xfs_owner_info *oinfo);
void xchk_xref_is_not_owned_by(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len, const struct xfs_owner_info *oinfo);
void xchk_xref_has_no_owner(struct xfs_scrub *sc, xfs_agblock_t agbno,
		xfs_extlen_t len);
void xchk_xref_is_cow_staging(struct xfs_scrub *sc, xfs_agblock_t bno,
		xfs_extlen_t len);
void xchk_xref_is_not_shared(struct xfs_scrub *sc, xfs_agblock_t bno,
		xfs_extlen_t len);
void xchk_xref_is_not_cow_staging(struct xfs_scrub *sc, xfs_agblock_t bno,
		xfs_extlen_t len);
#ifdef CONFIG_XFS_RT
void xchk_xref_is_used_rt_space(struct xfs_scrub *sc, xfs_rtblock_t rtbno,
		xfs_extlen_t len);
#else
# define xchk_xref_is_used_rt_space(sc, rtbno, len) do { } while (0)
#endif

#endif	/* __XFS_SCRUB_SCRUB_H__ */
