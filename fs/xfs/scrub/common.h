// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_COMMON_H__
#define __XFS_SCRUB_COMMON_H__

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
	/*
	 * If preemption is disabled, we need to yield to the scheduler every
	 * few seconds so that we don't run afoul of the soft lockup watchdog
	 * or RCU stall detector.
	 */
	cond_resched();

	if (fatal_signal_pending(current)) {
		if (*error == 0)
			*error = -EINTR;
		return true;
	}
	return false;
}

int xchk_trans_alloc(struct xfs_scrub *sc, uint resblks);
void xchk_trans_cancel(struct xfs_scrub *sc);

bool xchk_process_error(struct xfs_scrub *sc, xfs_agnumber_t agno,
		xfs_agblock_t bno, int *error);
bool xchk_fblock_process_error(struct xfs_scrub *sc, int whichfork,
		xfs_fileoff_t offset, int *error);

bool xchk_xref_process_error(struct xfs_scrub *sc,
		xfs_agnumber_t agno, xfs_agblock_t bno, int *error);
bool xchk_fblock_xref_process_error(struct xfs_scrub *sc,
		int whichfork, xfs_fileoff_t offset, int *error);

void xchk_block_set_preen(struct xfs_scrub *sc,
		struct xfs_buf *bp);
void xchk_ino_set_preen(struct xfs_scrub *sc, xfs_ino_t ino);

void xchk_set_corrupt(struct xfs_scrub *sc);
void xchk_block_set_corrupt(struct xfs_scrub *sc,
		struct xfs_buf *bp);
void xchk_ino_set_corrupt(struct xfs_scrub *sc, xfs_ino_t ino);
void xchk_fblock_set_corrupt(struct xfs_scrub *sc, int whichfork,
		xfs_fileoff_t offset);

void xchk_block_xref_set_corrupt(struct xfs_scrub *sc,
		struct xfs_buf *bp);
void xchk_ino_xref_set_corrupt(struct xfs_scrub *sc,
		xfs_ino_t ino);
void xchk_fblock_xref_set_corrupt(struct xfs_scrub *sc,
		int whichfork, xfs_fileoff_t offset);

void xchk_ino_set_warning(struct xfs_scrub *sc, xfs_ino_t ino);
void xchk_fblock_set_warning(struct xfs_scrub *sc, int whichfork,
		xfs_fileoff_t offset);

void xchk_set_incomplete(struct xfs_scrub *sc);
int xchk_checkpoint_log(struct xfs_mount *mp);

/* Are we set up for a cross-referencing check? */
bool xchk_should_check_xref(struct xfs_scrub *sc, int *error,
			   struct xfs_btree_cur **curpp);

/* Setup functions */
int xchk_setup_agheader(struct xfs_scrub *sc);
int xchk_setup_fs(struct xfs_scrub *sc);
int xchk_setup_ag_allocbt(struct xfs_scrub *sc);
int xchk_setup_ag_iallocbt(struct xfs_scrub *sc);
int xchk_setup_ag_rmapbt(struct xfs_scrub *sc);
int xchk_setup_ag_refcountbt(struct xfs_scrub *sc);
int xchk_setup_inode(struct xfs_scrub *sc);
int xchk_setup_inode_bmap(struct xfs_scrub *sc);
int xchk_setup_inode_bmap_data(struct xfs_scrub *sc);
int xchk_setup_directory(struct xfs_scrub *sc);
int xchk_setup_xattr(struct xfs_scrub *sc);
int xchk_setup_symlink(struct xfs_scrub *sc);
int xchk_setup_parent(struct xfs_scrub *sc);
#ifdef CONFIG_XFS_RT
int xchk_setup_rtbitmap(struct xfs_scrub *sc);
int xchk_setup_rtsummary(struct xfs_scrub *sc);
#else
static inline int
xchk_setup_rtbitmap(struct xfs_scrub *sc)
{
	return -ENOENT;
}
static inline int
xchk_setup_rtsummary(struct xfs_scrub *sc)
{
	return -ENOENT;
}
#endif
#ifdef CONFIG_XFS_QUOTA
int xchk_setup_quota(struct xfs_scrub *sc);
#else
static inline int
xchk_setup_quota(struct xfs_scrub *sc)
{
	return -ENOENT;
}
#endif
int xchk_setup_fscounters(struct xfs_scrub *sc);

void xchk_ag_free(struct xfs_scrub *sc, struct xchk_ag *sa);
int xchk_ag_init(struct xfs_scrub *sc, xfs_agnumber_t agno,
		struct xchk_ag *sa);

/*
 * Grab all AG resources, treating the inability to grab the perag structure as
 * a fs corruption.  This is intended for callers checking an ondisk reference
 * to a given AG, which means that the AG must still exist.
 */
static inline int
xchk_ag_init_existing(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	struct xchk_ag		*sa)
{
	int			error = xchk_ag_init(sc, agno, sa);

	return error == -ENOENT ? -EFSCORRUPTED : error;
}

int xchk_ag_read_headers(struct xfs_scrub *sc, xfs_agnumber_t agno,
		struct xchk_ag *sa);
void xchk_ag_btcur_free(struct xchk_ag *sa);
void xchk_ag_btcur_init(struct xfs_scrub *sc, struct xchk_ag *sa);
int xchk_count_rmap_ownedby_ag(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		const struct xfs_owner_info *oinfo, xfs_filblks_t *blocks);

int xchk_setup_ag_btree(struct xfs_scrub *sc, bool force_log);
int xchk_iget_for_scrubbing(struct xfs_scrub *sc);
int xchk_setup_inode_contents(struct xfs_scrub *sc, unsigned int resblks);
int xchk_install_live_inode(struct xfs_scrub *sc, struct xfs_inode *ip);

void xchk_ilock(struct xfs_scrub *sc, unsigned int ilock_flags);
bool xchk_ilock_nowait(struct xfs_scrub *sc, unsigned int ilock_flags);
void xchk_iunlock(struct xfs_scrub *sc, unsigned int ilock_flags);

void xchk_buffer_recheck(struct xfs_scrub *sc, struct xfs_buf *bp);

/*
 * Grab the inode at @inum.  The caller must have created a scrub transaction
 * so that we can confirm the inumber by walking the inobt and not deadlock on
 * a loop in the inobt.
 */
int xchk_iget(struct xfs_scrub *sc, xfs_ino_t inum, struct xfs_inode **ipp);
int xchk_iget_agi(struct xfs_scrub *sc, xfs_ino_t inum,
		struct xfs_buf **agi_bpp, struct xfs_inode **ipp);
void xchk_irele(struct xfs_scrub *sc, struct xfs_inode *ip);
int xchk_install_handle_inode(struct xfs_scrub *sc, struct xfs_inode *ip);

/*
 * Safe version of (untrusted) xchk_iget that uses an empty transaction to
 * avoid deadlocking on loops in the inobt.  This should only be used in a
 * scrub or repair setup routine, and only prior to grabbing a transaction.
 */
static inline int
xchk_iget_safe(struct xfs_scrub *sc, xfs_ino_t inum, struct xfs_inode **ipp)
{
	int	error;

	ASSERT(sc->tp == NULL);

	error = xchk_trans_alloc(sc, 0);
	if (error)
		return error;
	error = xchk_iget(sc, inum, ipp);
	xchk_trans_cancel(sc);
	return error;
}

/*
 * Don't bother cross-referencing if we already found corruption or cross
 * referencing discrepancies.
 */
static inline bool xchk_skip_xref(struct xfs_scrub_metadata *sm)
{
	return sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
			       XFS_SCRUB_OFLAG_XCORRUPT);
}

#ifdef CONFIG_XFS_ONLINE_REPAIR
/* Decide if a repair is required. */
static inline bool xchk_needs_repair(const struct xfs_scrub_metadata *sm)
{
	return sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
			       XFS_SCRUB_OFLAG_XCORRUPT |
			       XFS_SCRUB_OFLAG_PREEN);
}
#else
# define xchk_needs_repair(sc)		(false)
#endif /* CONFIG_XFS_ONLINE_REPAIR */

int xchk_metadata_inode_forks(struct xfs_scrub *sc);

/*
 * Helper macros to allocate and format xfile description strings.
 * Callers must kfree the pointer returned.
 */
#define xchk_xfile_descr(sc, fmt, ...) \
	kasprintf(XCHK_GFP_FLAGS, "XFS (%s): " fmt, \
			(sc)->mp->m_super->s_id, ##__VA_ARGS__)

/*
 * Setting up a hook to wait for intents to drain is costly -- we have to take
 * the CPU hotplug lock and force an i-cache flush on all CPUs once to set it
 * up, and again to tear it down.  These costs add up quickly, so we only want
 * to enable the drain waiter if the drain actually detected a conflict with
 * running intent chains.
 */
static inline bool xchk_need_intent_drain(struct xfs_scrub *sc)
{
	return sc->flags & XCHK_NEED_DRAIN;
}

void xchk_fsgates_enable(struct xfs_scrub *sc, unsigned int scrub_fshooks);

int xchk_inode_is_allocated(struct xfs_scrub *sc, xfs_agino_t agino,
		bool *inuse);

#endif	/* __XFS_SCRUB_COMMON_H__ */
