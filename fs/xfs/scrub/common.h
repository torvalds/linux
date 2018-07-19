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
xfs_scrub_should_terminate(
	struct xfs_scrub_context	*sc,
	int				*error)
{
	if (fatal_signal_pending(current)) {
		if (*error == 0)
			*error = -EAGAIN;
		return true;
	}
	return false;
}

int xfs_scrub_trans_alloc(struct xfs_scrub_context *sc, uint resblks);
bool xfs_scrub_process_error(struct xfs_scrub_context *sc, xfs_agnumber_t agno,
		xfs_agblock_t bno, int *error);
bool xfs_scrub_fblock_process_error(struct xfs_scrub_context *sc, int whichfork,
		xfs_fileoff_t offset, int *error);

bool xfs_scrub_xref_process_error(struct xfs_scrub_context *sc,
		xfs_agnumber_t agno, xfs_agblock_t bno, int *error);
bool xfs_scrub_fblock_xref_process_error(struct xfs_scrub_context *sc,
		int whichfork, xfs_fileoff_t offset, int *error);

void xfs_scrub_block_set_preen(struct xfs_scrub_context *sc,
		struct xfs_buf *bp);
void xfs_scrub_ino_set_preen(struct xfs_scrub_context *sc, xfs_ino_t ino);

void xfs_scrub_block_set_corrupt(struct xfs_scrub_context *sc,
		struct xfs_buf *bp);
void xfs_scrub_ino_set_corrupt(struct xfs_scrub_context *sc, xfs_ino_t ino);
void xfs_scrub_fblock_set_corrupt(struct xfs_scrub_context *sc, int whichfork,
		xfs_fileoff_t offset);

void xfs_scrub_block_xref_set_corrupt(struct xfs_scrub_context *sc,
		struct xfs_buf *bp);
void xfs_scrub_ino_xref_set_corrupt(struct xfs_scrub_context *sc,
		xfs_ino_t ino);
void xfs_scrub_fblock_xref_set_corrupt(struct xfs_scrub_context *sc,
		int whichfork, xfs_fileoff_t offset);

void xfs_scrub_ino_set_warning(struct xfs_scrub_context *sc, xfs_ino_t ino);
void xfs_scrub_fblock_set_warning(struct xfs_scrub_context *sc, int whichfork,
		xfs_fileoff_t offset);

void xfs_scrub_set_incomplete(struct xfs_scrub_context *sc);
int xfs_scrub_checkpoint_log(struct xfs_mount *mp);

/* Are we set up for a cross-referencing check? */
bool xfs_scrub_should_check_xref(struct xfs_scrub_context *sc, int *error,
			   struct xfs_btree_cur **curpp);

/* Setup functions */
int xfs_scrub_setup_fs(struct xfs_scrub_context *sc, struct xfs_inode *ip);
int xfs_scrub_setup_ag_allocbt(struct xfs_scrub_context *sc,
			       struct xfs_inode *ip);
int xfs_scrub_setup_ag_iallocbt(struct xfs_scrub_context *sc,
				struct xfs_inode *ip);
int xfs_scrub_setup_ag_rmapbt(struct xfs_scrub_context *sc,
			      struct xfs_inode *ip);
int xfs_scrub_setup_ag_refcountbt(struct xfs_scrub_context *sc,
				  struct xfs_inode *ip);
int xfs_scrub_setup_inode(struct xfs_scrub_context *sc,
			  struct xfs_inode *ip);
int xfs_scrub_setup_inode_bmap(struct xfs_scrub_context *sc,
			       struct xfs_inode *ip);
int xfs_scrub_setup_inode_bmap_data(struct xfs_scrub_context *sc,
				    struct xfs_inode *ip);
int xfs_scrub_setup_directory(struct xfs_scrub_context *sc,
			      struct xfs_inode *ip);
int xfs_scrub_setup_xattr(struct xfs_scrub_context *sc,
			  struct xfs_inode *ip);
int xfs_scrub_setup_symlink(struct xfs_scrub_context *sc,
			    struct xfs_inode *ip);
int xfs_scrub_setup_parent(struct xfs_scrub_context *sc,
			   struct xfs_inode *ip);
#ifdef CONFIG_XFS_RT
int xfs_scrub_setup_rt(struct xfs_scrub_context *sc, struct xfs_inode *ip);
#else
static inline int
xfs_scrub_setup_rt(struct xfs_scrub_context *sc, struct xfs_inode *ip)
{
	return -ENOENT;
}
#endif
#ifdef CONFIG_XFS_QUOTA
int xfs_scrub_setup_quota(struct xfs_scrub_context *sc, struct xfs_inode *ip);
#else
static inline int
xfs_scrub_setup_quota(struct xfs_scrub_context *sc, struct xfs_inode *ip)
{
	return -ENOENT;
}
#endif

void xfs_scrub_ag_free(struct xfs_scrub_context *sc, struct xfs_scrub_ag *sa);
int xfs_scrub_ag_init(struct xfs_scrub_context *sc, xfs_agnumber_t agno,
		      struct xfs_scrub_ag *sa);
void xfs_scrub_perag_get(struct xfs_mount *mp, struct xfs_scrub_ag *sa);
int xfs_scrub_ag_read_headers(struct xfs_scrub_context *sc, xfs_agnumber_t agno,
			      struct xfs_buf **agi, struct xfs_buf **agf,
			      struct xfs_buf **agfl);
void xfs_scrub_ag_btcur_free(struct xfs_scrub_ag *sa);
int xfs_scrub_ag_btcur_init(struct xfs_scrub_context *sc,
			    struct xfs_scrub_ag *sa);
int xfs_scrub_count_rmap_ownedby_ag(struct xfs_scrub_context *sc,
				    struct xfs_btree_cur *cur,
				    struct xfs_owner_info *oinfo,
				    xfs_filblks_t *blocks);

int xfs_scrub_setup_ag_btree(struct xfs_scrub_context *sc,
			     struct xfs_inode *ip, bool force_log);
int xfs_scrub_get_inode(struct xfs_scrub_context *sc, struct xfs_inode *ip_in);
int xfs_scrub_setup_inode_contents(struct xfs_scrub_context *sc,
				   struct xfs_inode *ip, unsigned int resblks);
void xfs_scrub_buffer_recheck(struct xfs_scrub_context *sc, struct xfs_buf *bp);

/*
 * Don't bother cross-referencing if we already found corruption or cross
 * referencing discrepancies.
 */
static inline bool xfs_scrub_skip_xref(struct xfs_scrub_metadata *sm)
{
	return sm->sm_flags & (XFS_SCRUB_OFLAG_CORRUPT |
			       XFS_SCRUB_OFLAG_XCORRUPT);
}

int xfs_scrub_metadata_inode_forks(struct xfs_scrub_context *sc);
int xfs_scrub_ilock_inverted(struct xfs_inode *ip, uint lock_mode);

#endif	/* __XFS_SCRUB_COMMON_H__ */
