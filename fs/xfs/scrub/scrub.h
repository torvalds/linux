// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_SCRUB_H__
#define __XFS_SCRUB_SCRUB_H__

struct xfs_scrub;

struct xchk_relax {
	unsigned long	next_resched;
	unsigned int	resched_nr;
	bool		interruptible;
};

/* Yield to the scheduler at most 10x per second. */
#define XCHK_RELAX_NEXT		(jiffies + (HZ / 10))

#define INIT_XCHK_RELAX	\
	(struct xchk_relax){ \
		.next_resched	= XCHK_RELAX_NEXT, \
		.resched_nr	= 0, \
		.interruptible	= true, \
	}

/*
 * Relax during a scrub operation and exit if there's a fatal signal pending.
 *
 * If preemption is disabled, we need to yield to the scheduler every now and
 * then so that we don't run afoul of the soft lockup watchdog or RCU stall
 * detector.  cond_resched calls are somewhat expensive (~5ns) so we want to
 * ratelimit this to 10x per second.  Amortize the cost of the other checks by
 * only doing it once every 100 calls.
 */
static inline int xchk_maybe_relax(struct xchk_relax *widget)
{
	/* Amortize the cost of scheduling and checking signals. */
	if (likely(++widget->resched_nr < 100))
		return 0;
	widget->resched_nr = 0;

	if (unlikely(widget->next_resched <= jiffies)) {
		cond_resched();
		widget->next_resched = XCHK_RELAX_NEXT;
	}

	if (widget->interruptible && fatal_signal_pending(current))
		return -EINTR;

	return 0;
}

/*
 * Standard flags for allocating memory within scrub.  NOFS context is
 * configured by the process allocation scope.  Scrub and repair must be able
 * to back out gracefully if there isn't enough memory.  Force-cast to avoid
 * complaints from static checkers.
 */
#define XCHK_GFP_FLAGS	((__force gfp_t)(GFP_KERNEL | __GFP_NOWARN | \
					 __GFP_RETRY_MAYFAIL))

/*
 * For opening files by handle for fsck operations, we don't trust the inumber
 * or the allocation state; therefore, perform an untrusted lookup.  We don't
 * want these inodes to pollute the cache, so mark them for immediate removal.
 */
#define XCHK_IGET_FLAGS	(XFS_IGET_UNTRUSTED | XFS_IGET_DONTCACHE)

/* Type info and names for the scrub types. */
enum xchk_type {
	ST_NONE = 1,	/* disabled */
	ST_PERAG,	/* per-AG metadata */
	ST_FS,		/* per-FS metadata */
	ST_INODE,	/* per-inode metadata */
	ST_GENERIC,	/* determined by the scrubber */
	ST_RTGROUP,	/* rtgroup metadata */
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

/* Inode lock state for the RT volume. */
struct xchk_rt {
	/* incore rtgroup, if applicable */
	struct xfs_rtgroup	*rtg;

	/* XFS_RTGLOCK_* lock state if locked */
	unsigned int		rtlock_flags;
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

	/* buffer target for in-memory btrees; also freed at teardown. */
	struct xfs_buftarg		*xmbtp;

	/* Lock flags for @ip. */
	uint				ilock_flags;

	/* The orphanage, for stashing files that have lost their parent. */
	uint				orphanage_ilock_flags;
	struct xfs_inode		*orphanage;

	/* A temporary file on this filesystem, for staging new metadata. */
	struct xfs_inode		*tempip;
	uint				temp_ilock_flags;

	/* See the XCHK/XREP state flags below. */
	unsigned int			flags;

	/*
	 * The XFS_SICK_* flags that correspond to the metadata being scrubbed
	 * or repaired.  We will use this mask to update the in-core fs health
	 * status with whatever we find.
	 */
	unsigned int			sick_mask;

	/*
	 * Clear these XFS_SICK_* flags but only if the scan is ok.  Useful for
	 * removing ZAPPED flags after a repair.
	 */
	unsigned int			healthy_mask;

	/* next time we want to cond_resched() */
	struct xchk_relax		relax;

	/* State tracking for single-AG operations. */
	struct xchk_ag			sa;

	/* State tracking for realtime operations. */
	struct xchk_rt			sr;
};

/* XCHK state flags grow up from zero, XREP state flags grown down from 2^31 */
#define XCHK_TRY_HARDER		(1U << 0)  /* can't get resources, try again */
#define XCHK_HAVE_FREEZE_PROT	(1U << 1)  /* do we have freeze protection? */
#define XCHK_FSGATES_DRAIN	(1U << 2)  /* defer ops draining enabled */
#define XCHK_NEED_DRAIN		(1U << 3)  /* scrub needs to drain defer ops */
#define XCHK_FSGATES_QUOTA	(1U << 4)  /* quota live update enabled */
#define XCHK_FSGATES_DIRENTS	(1U << 5)  /* directory live update enabled */
#define XCHK_FSGATES_RMAP	(1U << 6)  /* rmapbt live update enabled */
#define XREP_RESET_PERAG_RESV	(1U << 30) /* must reset AG space reservation */
#define XREP_ALREADY_FIXED	(1U << 31) /* checking our repair work */

/*
 * The XCHK_FSGATES* flags reflect functionality in the main filesystem that
 * are only enabled for this particular online fsck.  When not in use, the
 * features are gated off via dynamic code patching, which is why the state
 * must be enabled during scrub setup and can only be torn down afterwards.
 */
#define XCHK_FSGATES_ALL	(XCHK_FSGATES_DRAIN | \
				 XCHK_FSGATES_QUOTA | \
				 XCHK_FSGATES_DIRENTS | \
				 XCHK_FSGATES_RMAP)

struct xfs_scrub_subord {
	struct xfs_scrub	sc;
	struct xfs_scrub	*parent_sc;
	unsigned int		old_smtype;
	unsigned int		old_smflags;
};

struct xfs_scrub_subord *xchk_scrub_create_subord(struct xfs_scrub *sc,
		unsigned int subtype);
void xchk_scrub_free_subord(struct xfs_scrub_subord *sub);

/*
 * We /could/ terminate a scrub/repair operation early.  If we're not
 * in a good place to continue (fatal signal, etc.) then bail out.
 * Note that we're careful not to make any judgements about *error.
 */
static inline bool
xchk_should_terminate(
	struct xfs_scrub	*sc,
	int			*error)
{
	if (xchk_maybe_relax(&sc->relax)) {
		if (*error == 0)
			*error = -EINTR;
		return true;
	}
	return false;
}

static inline int xchk_nothing(struct xfs_scrub *sc)
{
	return -ENOENT;
}

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
int xchk_dirtree(struct xfs_scrub *sc);
int xchk_metapath(struct xfs_scrub *sc);
#ifdef CONFIG_XFS_RT
int xchk_rtbitmap(struct xfs_scrub *sc);
int xchk_rtsummary(struct xfs_scrub *sc);
int xchk_rgsuperblock(struct xfs_scrub *sc);
#else
# define xchk_rtbitmap		xchk_nothing
# define xchk_rtsummary		xchk_nothing
# define xchk_rgsuperblock	xchk_nothing
#endif
#ifdef CONFIG_XFS_QUOTA
int xchk_quota(struct xfs_scrub *sc);
int xchk_quotacheck(struct xfs_scrub *sc);
#else
# define xchk_quota		xchk_nothing
# define xchk_quotacheck	xchk_nothing
#endif
int xchk_fscounters(struct xfs_scrub *sc);
int xchk_nlinks(struct xfs_scrub *sc);

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
