// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
			*error = -EAGAIN;
		return true;
	}
	return false;
}

int xchk_trans_alloc(struct xfs_scrub *sc, uint resblks);
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
int xchk_setup_fs(struct xfs_scrub *sc, struct xfs_inode *ip);
int xchk_setup_ag_allocbt(struct xfs_scrub *sc,
			       struct xfs_inode *ip);
int xchk_setup_ag_iallocbt(struct xfs_scrub *sc,
				struct xfs_inode *ip);
int xchk_setup_ag_rmapbt(struct xfs_scrub *sc,
			      struct xfs_inode *ip);
int xchk_setup_ag_refcountbt(struct xfs_scrub *sc,
				  struct xfs_inode *ip);
int xchk_setup_inode(struct xfs_scrub *sc,
			  struct xfs_inode *ip);
int xchk_setup_inode_bmap(struct xfs_scrub *sc,
			       struct xfs_inode *ip);
int xchk_setup_inode_bmap_data(struct xfs_scrub *sc,
				    struct xfs_inode *ip);
int xchk_setup_directory(struct xfs_scrub *sc,
			      struct xfs_inode *ip);
int xchk_setup_xattr(struct xfs_scrub *sc,
			  struct xfs_inode *ip);
int xchk_setup_symlink(struct xfs_scrub *sc,
			    struct xfs_inode *ip);
int xchk_setup_parent(struct xfs_scrub *sc,
			   struct xfs_inode *ip);
#ifdef CONFIG_XFS_RT
int xchk_setup_rt(struct xfs_scrub *sc, struct xfs_inode *ip);
#else
static inline int
xchk_setup_rt(struct xfs_scrub *sc, struct xfs_inode *ip)
{
	return -ENOENT;
}
#endif
#ifdef CONFIG_XFS_QUOTA
int xchk_setup_quota(struct xfs_scrub *sc, struct xfs_inode *ip);
#else
static inline int
xchk_setup_quota(struct xfs_scrub *sc, struct xfs_inode *ip)
{
	return -ENOENT;
}
#endif

void xchk_ag_free(struct xfs_scrub *sc, struct xchk_ag *sa);
int xchk_ag_init(struct xfs_scrub *sc, xfs_agnumber_t agno,
		struct xchk_ag *sa);
void xchk_perag_get(struct xfs_mount *mp, struct xchk_ag *sa);
int xchk_ag_read_headers(struct xfs_scrub *sc, xfs_agnumber_t agno,
		struct xfs_buf **agi, struct xfs_buf **agf,
		struct xfs_buf **agfl);
void xchk_ag_btcur_free(struct xchk_ag *sa);
int xchk_ag_btcur_init(struct xfs_scrub *sc, struct xchk_ag *sa);
int xchk_count_rmap_ownedby_ag(struct xfs_scrub *sc, struct xfs_btree_cur *cur,
		struct xfs_owner_info *oinfo, xfs_filblks_t *blocks);

int xchk_setup_ag_btree(struct xfs_scrub *sc, struct xfs_inode *ip,
		bool force_log);
int xchk_get_inode(struct xfs_scrub *sc, struct xfs_inode *ip_in);
int xchk_setup_inode_contents(struct xfs_scrub *sc, struct xfs_inode *ip,
		unsigned int resblks);
void xchk_buffer_recheck(struct xfs_scrub *sc, struct xfs_buf *bp);

/*
 * Don't bother cross-referencing if we already found corruption or cross
 * referencing discrepancies.
 */
static inline bool xchk_skip_xref(struct xfs_scrub_metadata *sm)
{
	return sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
			       XFS_SCRUB_OFLAG_XCORRUPT);
}

int xchk_metadata_inode_forks(struct xfs_scrub *sc);
int xchk_ilock_inverted(struct xfs_inode *ip, uint lock_mode);

#endif	/* __XFS_SCRUB_COMMON_H__ */
