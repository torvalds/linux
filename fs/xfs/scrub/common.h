// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_COMMON_H__
#define __XFS_SCRUB_COMMON_H__

int xchk_trans_alloc(struct xfs_scrub *sc, uint resblks);
void xchk_trans_alloc_empty(struct xfs_scrub *sc);
void xchk_trans_cancel(struct xfs_scrub *sc);

bool xchk_process_error(struct xfs_scrub *sc, xfs_agnumber_t agno,
		xfs_agblock_t bno, int *error);
bool xchk_process_rt_error(struct xfs_scrub *sc, xfs_rgnumber_t rgno,
		xfs_rgblock_t rgbno, int *error);
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
#ifdef CONFIG_XFS_QUOTA
void xchk_qcheck_set_corrupt(struct xfs_scrub *sc, unsigned int dqtype,
		xfs_dqid_t id);
#endif

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

static inline int xchk_setup_nothing(struct xfs_scrub *sc)
{
	return -ENOENT;
}

/* Setup functions */
int xchk_setup_agheader(struct xfs_scrub *sc);
int xchk_setup_fs(struct xfs_scrub *sc);
int xchk_setup_rt(struct xfs_scrub *sc);
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
int xchk_setup_dirtree(struct xfs_scrub *sc);
int xchk_setup_metapath(struct xfs_scrub *sc);
#ifdef CONFIG_XFS_RT
int xchk_setup_rtbitmap(struct xfs_scrub *sc);
int xchk_setup_rtsummary(struct xfs_scrub *sc);
int xchk_setup_rgsuperblock(struct xfs_scrub *sc);
int xchk_setup_rtrmapbt(struct xfs_scrub *sc);
int xchk_setup_rtrefcountbt(struct xfs_scrub *sc);
#else
# define xchk_setup_rtbitmap		xchk_setup_nothing
# define xchk_setup_rtsummary		xchk_setup_nothing
# define xchk_setup_rgsuperblock	xchk_setup_nothing
# define xchk_setup_rtrmapbt		xchk_setup_nothing
# define xchk_setup_rtrefcountbt	xchk_setup_nothing
#endif
#ifdef CONFIG_XFS_QUOTA
int xchk_ino_dqattach(struct xfs_scrub *sc);
int xchk_setup_quota(struct xfs_scrub *sc);
int xchk_setup_quotacheck(struct xfs_scrub *sc);
#else
static inline int
xchk_ino_dqattach(struct xfs_scrub *sc)
{
	return 0;
}
# define xchk_setup_quota		xchk_setup_nothing
# define xchk_setup_quotacheck		xchk_setup_nothing
#endif
int xchk_setup_fscounters(struct xfs_scrub *sc);
int xchk_setup_nlinks(struct xfs_scrub *sc);

void xchk_ag_free(struct xfs_scrub *sc, struct xchk_ag *sa);
int xchk_ag_init(struct xfs_scrub *sc, xfs_agnumber_t agno,
		struct xchk_ag *sa);
int xchk_perag_drain_and_lock(struct xfs_scrub *sc);

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

#ifdef CONFIG_XFS_RT

/* All the locks we need to check an rtgroup. */
#define XCHK_RTGLOCK_ALL	(XFS_RTGLOCK_BITMAP | \
				 XFS_RTGLOCK_RMAP | \
				 XFS_RTGLOCK_REFCOUNT)

int xchk_rtgroup_init(struct xfs_scrub *sc, xfs_rgnumber_t rgno,
		struct xchk_rt *sr);

static inline int
xchk_rtgroup_init_existing(
	struct xfs_scrub	*sc,
	xfs_rgnumber_t		rgno,
	struct xchk_rt		*sr)
{
	int			error = xchk_rtgroup_init(sc, rgno, sr);

	return error == -ENOENT ? -EFSCORRUPTED : error;
}

int xchk_rtgroup_lock(struct xfs_scrub *sc, struct xchk_rt *sr,
		unsigned int rtglock_flags);
void xchk_rtgroup_unlock(struct xchk_rt *sr);
void xchk_rtgroup_btcur_free(struct xchk_rt *sr);
void xchk_rtgroup_free(struct xfs_scrub *sc, struct xchk_rt *sr);
#else
# define xchk_rtgroup_init(sc, rgno, sr)		(-EFSCORRUPTED)
# define xchk_rtgroup_init_existing(sc, rgno, sr)	(-EFSCORRUPTED)
# define xchk_rtgroup_lock(sc, sr, lockflags)		(-EFSCORRUPTED)
# define xchk_rtgroup_unlock(sr)			do { } while (0)
# define xchk_rtgroup_btcur_free(sr)			do { } while (0)
# define xchk_rtgroup_free(sc, sr)			do { } while (0)
#endif /* CONFIG_XFS_RT */

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

bool xchk_dir_looks_zapped(struct xfs_inode *dp);
bool xchk_pptr_looks_zapped(struct xfs_inode *ip);

/* Decide if a repair is required. */
static inline bool xchk_needs_repair(const struct xfs_scrub_metadata *sm)
{
	return sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
			       XFS_SCRUB_OFLAG_XCORRUPT |
			       XFS_SCRUB_OFLAG_PREEN);
}

/*
 * "Should we prepare for a repair?"
 *
 * Return true if the caller permits us to repair metadata and we're not
 * setting up for a post-repair evaluation.
 */
static inline bool xchk_could_repair(const struct xfs_scrub *sc)
{
	return (sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR) &&
		!(sc->flags & XREP_ALREADY_FIXED);
}

int xchk_metadata_inode_forks(struct xfs_scrub *sc);

/*
 * Helper macros to allocate and format xfile description strings.
 * Callers must kfree the pointer returned.
 */
#define xchk_xfile_descr(sc, fmt, ...) \
	kasprintf(XCHK_GFP_FLAGS, "XFS (%s): " fmt, \
			(sc)->mp->m_super->s_id, ##__VA_ARGS__)
#define xchk_xfile_ag_descr(sc, fmt, ...) \
	kasprintf(XCHK_GFP_FLAGS, "XFS (%s): AG 0x%x " fmt, \
			(sc)->mp->m_super->s_id, \
			(sc)->sa.pag ? \
				pag_agno((sc)->sa.pag) : (sc)->sm->sm_agno, \
			##__VA_ARGS__)
#define xchk_xfile_ino_descr(sc, fmt, ...) \
	kasprintf(XCHK_GFP_FLAGS, "XFS (%s): inode 0x%llx " fmt, \
			(sc)->mp->m_super->s_id, \
			(sc)->ip ? (sc)->ip->i_ino : (sc)->sm->sm_ino, \
			##__VA_ARGS__)
#define xchk_xfile_rtgroup_descr(sc, fmt, ...) \
	kasprintf(XCHK_GFP_FLAGS, "XFS (%s): rtgroup 0x%x " fmt, \
			(sc)->mp->m_super->s_id, \
			(sc)->sa.pag ? \
				rtg_rgno((sc)->sr.rtg) : (sc)->sm->sm_agno, \
			##__VA_ARGS__)

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
int xchk_inode_count_blocks(struct xfs_scrub *sc, int whichfork,
		xfs_extnum_t *nextents, xfs_filblks_t *count);

bool xchk_inode_is_dirtree_root(const struct xfs_inode *ip);
bool xchk_inode_is_sb_rooted(const struct xfs_inode *ip);
xfs_ino_t xchk_inode_rootdir_inum(const struct xfs_inode *ip);

#endif	/* __XFS_SCRUB_COMMON_H__ */
