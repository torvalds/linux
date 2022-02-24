/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * journal.h
 *
 * Defines journalling api and structures.
 *
 * Copyright (C) 2003, 2005 Oracle.  All rights reserved.
 */

#ifndef OCFS2_JOURNAL_H
#define OCFS2_JOURNAL_H

#include <linux/fs.h>
#include <linux/jbd2.h>

enum ocfs2_journal_state {
	OCFS2_JOURNAL_FREE = 0,
	OCFS2_JOURNAL_LOADED,
	OCFS2_JOURNAL_IN_SHUTDOWN,
};

struct ocfs2_super;
struct ocfs2_dinode;

/*
 * The recovery_list is a simple linked list of node numbers to recover.
 * It is protected by the recovery_lock.
 */

struct ocfs2_recovery_map {
	unsigned int rm_used;
	unsigned int *rm_entries;
};


struct ocfs2_journal {
	enum ocfs2_journal_state   j_state;    /* Journals current state   */

	journal_t                 *j_journal; /* The kernels journal type */
	struct inode              *j_inode;   /* Kernel inode pointing to
					       * this journal             */
	struct ocfs2_super        *j_osb;     /* pointer to the super
					       * block for the node
					       * we're currently
					       * running on -- not
					       * necessarily the super
					       * block from the node
					       * which we usually run
					       * from (recovery,
					       * etc)                     */
	struct buffer_head        *j_bh;      /* Journal disk inode block */
	atomic_t                  j_num_trans; /* Number of transactions
					        * currently in the system. */
	spinlock_t                j_lock;
	unsigned long             j_trans_id;
	struct rw_semaphore       j_trans_barrier;
	wait_queue_head_t         j_checkpointed;

	/* both fields protected by j_lock*/
	struct list_head          j_la_cleanups;
	struct work_struct        j_recovery_work;
};

extern spinlock_t trans_inc_lock;

/* wrap j_trans_id so we never have it equal to zero. */
static inline unsigned long ocfs2_inc_trans_id(struct ocfs2_journal *j)
{
	unsigned long old_id;
	spin_lock(&trans_inc_lock);
	old_id = j->j_trans_id++;
	if (unlikely(!j->j_trans_id))
		j->j_trans_id = 1;
	spin_unlock(&trans_inc_lock);
	return old_id;
}

static inline void ocfs2_set_ci_lock_trans(struct ocfs2_journal *journal,
					   struct ocfs2_caching_info *ci)
{
	spin_lock(&trans_inc_lock);
	ci->ci_last_trans = journal->j_trans_id;
	spin_unlock(&trans_inc_lock);
}

/* Used to figure out whether it's safe to drop a metadata lock on an
 * cached object. Returns true if all the object's changes have been
 * checkpointed to disk. You should be holding the spinlock on the
 * metadata lock while calling this to be sure that nobody can take
 * the lock and put it on another transaction. */
static inline int ocfs2_ci_fully_checkpointed(struct ocfs2_caching_info *ci)
{
	int ret;
	struct ocfs2_journal *journal =
		OCFS2_SB(ocfs2_metadata_cache_get_super(ci))->journal;

	spin_lock(&trans_inc_lock);
	ret = time_after(journal->j_trans_id, ci->ci_last_trans);
	spin_unlock(&trans_inc_lock);
	return ret;
}

/* convenience function to check if an object backed by struct
 * ocfs2_caching_info  is still new (has never hit disk) Will do you a
 * favor and set created_trans = 0 when you've
 * been checkpointed.  returns '1' if the ci is still new. */
static inline int ocfs2_ci_is_new(struct ocfs2_caching_info *ci)
{
	int ret;
	struct ocfs2_journal *journal =
		OCFS2_SB(ocfs2_metadata_cache_get_super(ci))->journal;

	spin_lock(&trans_inc_lock);
	ret = !(time_after(journal->j_trans_id, ci->ci_created_trans));
	if (!ret)
		ci->ci_created_trans = 0;
	spin_unlock(&trans_inc_lock);
	return ret;
}

/* Wrapper for inodes so we can check system files */
static inline int ocfs2_inode_is_new(struct inode *inode)
{
	/* System files are never "new" as they're written out by
	 * mkfs. This helps us early during mount, before we have the
	 * journal open and j_trans_id could be junk. */
	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_SYSTEM_FILE)
		return 0;

	return ocfs2_ci_is_new(INODE_CACHE(inode));
}

static inline void ocfs2_ci_set_new(struct ocfs2_super *osb,
				    struct ocfs2_caching_info *ci)
{
	spin_lock(&trans_inc_lock);
	ci->ci_created_trans = osb->journal->j_trans_id;
	spin_unlock(&trans_inc_lock);
}

/* Exported only for the journal struct init code in super.c. Do not call. */
void ocfs2_orphan_scan_init(struct ocfs2_super *osb);
void ocfs2_orphan_scan_start(struct ocfs2_super *osb);
void ocfs2_orphan_scan_stop(struct ocfs2_super *osb);

void ocfs2_complete_recovery(struct work_struct *work);
void ocfs2_wait_for_recovery(struct ocfs2_super *osb);

int ocfs2_recovery_init(struct ocfs2_super *osb);
void ocfs2_recovery_exit(struct ocfs2_super *osb);

int ocfs2_compute_replay_slots(struct ocfs2_super *osb);
/*
 *  Journal Control:
 *  Initialize, Load, Shutdown, Wipe a journal.
 *
 *  ocfs2_journal_init     - Initialize journal structures in the OSB.
 *  ocfs2_journal_load     - Load the given journal off disk. Replay it if
 *                          there's transactions still in there.
 *  ocfs2_journal_shutdown - Shutdown a journal, this will flush all
 *                          uncommitted, uncheckpointed transactions.
 *  ocfs2_journal_wipe     - Wipe transactions from a journal. Optionally
 *                          zero out each block.
 *  ocfs2_recovery_thread  - Perform recovery on a node. osb is our own osb.
 *  ocfs2_mark_dead_nodes - Start recovery on nodes we won't get a heartbeat
 *                          event on.
 *  ocfs2_start_checkpoint - Kick the commit thread to do a checkpoint.
 */
void   ocfs2_set_journal_params(struct ocfs2_super *osb);
int    ocfs2_journal_init(struct ocfs2_super *osb, int *dirty);
void   ocfs2_journal_shutdown(struct ocfs2_super *osb);
int    ocfs2_journal_wipe(struct ocfs2_journal *journal,
			  int full);
int    ocfs2_journal_load(struct ocfs2_journal *journal, int local,
			  int replayed);
int    ocfs2_check_journals_nolocks(struct ocfs2_super *osb);
void   ocfs2_recovery_thread(struct ocfs2_super *osb,
			     int node_num);
int    ocfs2_mark_dead_nodes(struct ocfs2_super *osb);
void   ocfs2_complete_mount_recovery(struct ocfs2_super *osb);
void ocfs2_complete_quota_recovery(struct ocfs2_super *osb);

static inline void ocfs2_start_checkpoint(struct ocfs2_super *osb)
{
	wake_up(&osb->checkpoint_event);
}

static inline void ocfs2_checkpoint_inode(struct inode *inode)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (ocfs2_mount_local(osb))
		return;

	if (!ocfs2_ci_fully_checkpointed(INODE_CACHE(inode))) {
		/* WARNING: This only kicks off a single
		 * checkpoint. If someone races you and adds more
		 * metadata to the journal, you won't know, and will
		 * wind up waiting *a lot* longer than necessary. Right
		 * now we only use this in clear_inode so that's
		 * OK. */
		ocfs2_start_checkpoint(osb);

		wait_event(osb->journal->j_checkpointed,
			   ocfs2_ci_fully_checkpointed(INODE_CACHE(inode)));
	}
}

/*
 *  Transaction Handling:
 *  Manage the lifetime of a transaction handle.
 *
 *  ocfs2_start_trans      - Begin a transaction. Give it an upper estimate of
 *                          the number of blocks that will be changed during
 *                          this handle.
 *  ocfs2_commit_trans - Complete a handle. It might return -EIO if
 *                       the journal was aborted. The majority of paths don't
 *                       check the return value as an error there comes too
 *                       late to do anything (and will be picked up in a
 *                       later transaction).
 *  ocfs2_extend_trans     - Extend a handle by nblocks credits. This may
 *                          commit the handle to disk in the process, but will
 *                          not release any locks taken during the transaction.
 *  ocfs2_journal_access* - Notify the handle that we want to journal this
 *                          buffer. Will have to call ocfs2_journal_dirty once
 *                          we've actually dirtied it. Type is one of . or .
 *                          Always call the specific flavor of
 *                          ocfs2_journal_access_*() unless you intend to
 *                          manage the checksum by hand.
 *  ocfs2_journal_dirty    - Mark a journalled buffer as having dirty data.
 *  ocfs2_jbd2_inode_add_write  - Mark an inode with range so that its data goes
 *                                out before the current handle commits.
 */

/* You must always start_trans with a number of buffs > 0, but it's
 * perfectly legal to go through an entire transaction without having
 * dirtied any buffers. */
handle_t		    *ocfs2_start_trans(struct ocfs2_super *osb,
					       int max_buffs);
int			     ocfs2_commit_trans(struct ocfs2_super *osb,
						handle_t *handle);
int			     ocfs2_extend_trans(handle_t *handle, int nblocks);
int			     ocfs2_allocate_extend_trans(handle_t *handle,
						int thresh);

/*
 * Define an arbitrary limit for the amount of data we will anticipate
 * writing to any given transaction.  For unbounded transactions such as
 * fallocate(2) we can write more than this, but we always
 * start off at the maximum transaction size and grow the transaction
 * optimistically as we go.
 */
#define OCFS2_MAX_TRANS_DATA	64U

/*
 * Create access is for when we get a newly created buffer and we're
 * not gonna read it off disk, but rather fill it ourselves.  Right
 * now, we don't do anything special with this (it turns into a write
 * request), but this is a good placeholder in case we do...
 *
 * Write access is for when we read a block off disk and are going to
 * modify it. This way the journalling layer knows it may need to make
 * a copy of that block (if it's part of another, uncommitted
 * transaction) before we do so.
 */
#define OCFS2_JOURNAL_ACCESS_CREATE 0
#define OCFS2_JOURNAL_ACCESS_WRITE  1
#define OCFS2_JOURNAL_ACCESS_UNDO   2


/* ocfs2_inode */
int ocfs2_journal_access_di(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* ocfs2_extent_block */
int ocfs2_journal_access_eb(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* ocfs2_refcount_block */
int ocfs2_journal_access_rb(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* ocfs2_group_desc */
int ocfs2_journal_access_gd(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* ocfs2_xattr_block */
int ocfs2_journal_access_xb(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* quota blocks */
int ocfs2_journal_access_dq(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* dirblock */
int ocfs2_journal_access_db(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* ocfs2_dx_root_block */
int ocfs2_journal_access_dr(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* ocfs2_dx_leaf */
int ocfs2_journal_access_dl(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type);
/* Anything that has no ecc */
int ocfs2_journal_access(handle_t *handle, struct ocfs2_caching_info *ci,
			 struct buffer_head *bh, int type);

/*
 * A word about the journal_access/journal_dirty "dance". It is
 * entirely legal to journal_access a buffer more than once (as long
 * as the access type is the same -- I'm not sure what will happen if
 * access type is different but this should never happen anyway) It is
 * also legal to journal_dirty a buffer more than once. In fact, you
 * can even journal_access a buffer after you've done a
 * journal_access/journal_dirty pair. The only thing you cannot do
 * however, is journal_dirty a buffer which you haven't yet passed to
 * journal_access at least once.
 *
 * That said, 99% of the time this doesn't matter and this is what the
 * path looks like:
 *
 *	<read a bh>
 *	ocfs2_journal_access(handle, bh,	OCFS2_JOURNAL_ACCESS_WRITE);
 *	<modify the bh>
 * 	ocfs2_journal_dirty(handle, bh);
 */
void ocfs2_journal_dirty(handle_t *handle, struct buffer_head *bh);

/*
 *  Credit Macros:
 *  Convenience macros to calculate number of credits needed.
 *
 *  For convenience sake, I have a set of macros here which calculate
 *  the *maximum* number of sectors which will be changed for various
 *  metadata updates.
 */

/* simple file updates like chmod, etc. */
#define OCFS2_INODE_UPDATE_CREDITS 1

/* extended attribute block update */
#define OCFS2_XATTR_BLOCK_UPDATE_CREDITS 1

/* Update of a single quota block */
#define OCFS2_QUOTA_BLOCK_UPDATE_CREDITS 1

/* global quotafile inode update, data block */
#define OCFS2_QINFO_WRITE_CREDITS (OCFS2_INODE_UPDATE_CREDITS + \
				   OCFS2_QUOTA_BLOCK_UPDATE_CREDITS)

#define OCFS2_LOCAL_QINFO_WRITE_CREDITS OCFS2_QUOTA_BLOCK_UPDATE_CREDITS
/*
 * The two writes below can accidentally see global info dirty due
 * to set_info() quotactl so make them prepared for the writes.
 */
/* quota data block, global info */
/* Write to local quota file */
#define OCFS2_QWRITE_CREDITS (OCFS2_QINFO_WRITE_CREDITS + \
			      OCFS2_QUOTA_BLOCK_UPDATE_CREDITS)

/* global quota data block, local quota data block, global quota inode,
 * global quota info */
#define OCFS2_QSYNC_CREDITS (OCFS2_QINFO_WRITE_CREDITS + \
			     2 * OCFS2_QUOTA_BLOCK_UPDATE_CREDITS)

static inline int ocfs2_quota_trans_credits(struct super_block *sb)
{
	int credits = 0;

	if (OCFS2_HAS_RO_COMPAT_FEATURE(sb, OCFS2_FEATURE_RO_COMPAT_USRQUOTA))
		credits += OCFS2_QWRITE_CREDITS;
	if (OCFS2_HAS_RO_COMPAT_FEATURE(sb, OCFS2_FEATURE_RO_COMPAT_GRPQUOTA))
		credits += OCFS2_QWRITE_CREDITS;
	return credits;
}

/* group extend. inode update and last group update. */
#define OCFS2_GROUP_EXTEND_CREDITS	(OCFS2_INODE_UPDATE_CREDITS + 1)

/* group add. inode update and the new group update. */
#define OCFS2_GROUP_ADD_CREDITS	(OCFS2_INODE_UPDATE_CREDITS + 1)

/* get one bit out of a suballocator: dinode + group descriptor +
 * prev. group desc. if we relink. */
#define OCFS2_SUBALLOC_ALLOC (3)

static inline int ocfs2_inline_to_extents_credits(struct super_block *sb)
{
	return OCFS2_SUBALLOC_ALLOC + OCFS2_INODE_UPDATE_CREDITS +
	       ocfs2_quota_trans_credits(sb);
}

/* dinode + group descriptor update. We don't relink on free yet. */
#define OCFS2_SUBALLOC_FREE  (2)

#define OCFS2_TRUNCATE_LOG_UPDATE OCFS2_INODE_UPDATE_CREDITS
#define OCFS2_TRUNCATE_LOG_FLUSH_ONE_REC (OCFS2_SUBALLOC_FREE 		      \
					 + OCFS2_TRUNCATE_LOG_UPDATE)

static inline int ocfs2_remove_extent_credits(struct super_block *sb)
{
	return OCFS2_TRUNCATE_LOG_UPDATE + OCFS2_INODE_UPDATE_CREDITS +
	       ocfs2_quota_trans_credits(sb);
}

/* data block for new dir/symlink, allocation of directory block, dx_root
 * update for free list */
#define OCFS2_DIR_LINK_ADDITIONAL_CREDITS (1 + OCFS2_SUBALLOC_ALLOC + 1)

static inline int ocfs2_add_dir_index_credits(struct super_block *sb)
{
	/* 1 block for index, 2 allocs (data, metadata), 1 clusters
	 * worth of blocks for initial extent. */
	return 1 + 2 * OCFS2_SUBALLOC_ALLOC +
		ocfs2_clusters_to_blocks(sb, 1);
}

/* parent fe, parent block, new file entry, index leaf, inode alloc fe, inode
 * alloc group descriptor + mkdir/symlink blocks + dir blocks + xattr
 * blocks + quota update */
static inline int ocfs2_mknod_credits(struct super_block *sb, int is_dir,
				      int xattr_credits)
{
	int dir_credits = OCFS2_DIR_LINK_ADDITIONAL_CREDITS;

	if (is_dir)
		dir_credits += ocfs2_add_dir_index_credits(sb);

	return 4 + OCFS2_SUBALLOC_ALLOC + dir_credits + xattr_credits +
	       ocfs2_quota_trans_credits(sb);
}

/* local alloc metadata change + main bitmap updates */
#define OCFS2_WINDOW_MOVE_CREDITS (OCFS2_INODE_UPDATE_CREDITS                 \
				  + OCFS2_SUBALLOC_ALLOC + OCFS2_SUBALLOC_FREE)

/* used when we don't need an allocation change for a dir extend. One
 * for the dinode, one for the new block. */
#define OCFS2_SIMPLE_DIR_EXTEND_CREDITS (2)

/* file update (nlink, etc) + directory mtime/ctime + dir entry block + quota
 * update on dir + index leaf + dx root update for free list +
 * previous dirblock update in the free list */
static inline int ocfs2_link_credits(struct super_block *sb)
{
	return 2 * OCFS2_INODE_UPDATE_CREDITS + 4 +
	       ocfs2_quota_trans_credits(sb);
}

/* inode + dir inode (if we unlink a dir), + dir entry block + orphan
 * dir inode link + dir inode index leaf + dir index root */
static inline int ocfs2_unlink_credits(struct super_block *sb)
{
	/* The quota update from ocfs2_link_credits is unused here... */
	return 2 * OCFS2_INODE_UPDATE_CREDITS + 3 + ocfs2_link_credits(sb);
}

/* dinode + orphan dir dinode + inode alloc dinode + orphan dir entry +
 * inode alloc group descriptor + orphan dir index root +
 * orphan dir index leaf */
#define OCFS2_DELETE_INODE_CREDITS (3 * OCFS2_INODE_UPDATE_CREDITS + 4)

/* dinode + orphan dir dinode + extent tree leaf block + orphan dir entry +
 * orphan dir index root + orphan dir index leaf */
#define OCFS2_INODE_ADD_TO_ORPHAN_CREDITS  (2 * OCFS2_INODE_UPDATE_CREDITS + 4)
#define OCFS2_INODE_DEL_FROM_ORPHAN_CREDITS  OCFS2_INODE_ADD_TO_ORPHAN_CREDITS

/* dinode update, old dir dinode update, new dir dinode update, old
 * dir dir entry, new dir dir entry, dir entry update for renaming
 * directory + target unlink + 3 x dir index leaves */
static inline int ocfs2_rename_credits(struct super_block *sb)
{
	return 3 * OCFS2_INODE_UPDATE_CREDITS + 6 + ocfs2_unlink_credits(sb);
}

/* global bitmap dinode, group desc., relinked group,
 * suballocator dinode, group desc., relinked group,
 * dinode, xattr block */
#define OCFS2_XATTR_BLOCK_CREATE_CREDITS (OCFS2_SUBALLOC_ALLOC * 2 + \
					  + OCFS2_INODE_UPDATE_CREDITS \
					  + OCFS2_XATTR_BLOCK_UPDATE_CREDITS)

/* inode update, removal of dx root block from allocator */
#define OCFS2_DX_ROOT_REMOVE_CREDITS (OCFS2_INODE_UPDATE_CREDITS +	\
				      OCFS2_SUBALLOC_FREE)

static inline int ocfs2_calc_dxi_expand_credits(struct super_block *sb)
{
	int credits = 1 + OCFS2_SUBALLOC_ALLOC;

	credits += ocfs2_clusters_to_blocks(sb, 1);
	credits += ocfs2_quota_trans_credits(sb);

	return credits;
}

/* inode update, new refcount block and its allocation credits. */
#define OCFS2_REFCOUNT_TREE_CREATE_CREDITS (OCFS2_INODE_UPDATE_CREDITS + 1 \
					    + OCFS2_SUBALLOC_ALLOC)

/* inode and the refcount block update. */
#define OCFS2_REFCOUNT_TREE_SET_CREDITS (OCFS2_INODE_UPDATE_CREDITS + 1)

/*
 * inode and the refcount block update.
 * It doesn't include the credits for sub alloc change.
 * So if we need to free the bit, OCFS2_SUBALLOC_FREE needs to be added.
 */
#define OCFS2_REFCOUNT_TREE_REMOVE_CREDITS (OCFS2_INODE_UPDATE_CREDITS + 1)

/* 2 metadata alloc, 2 new blocks and root refcount block */
#define OCFS2_EXPAND_REFCOUNT_TREE_CREDITS (OCFS2_SUBALLOC_ALLOC * 2 + 3)

/*
 * Please note that the caller must make sure that root_el is the root
 * of extent tree. So for an inode, it should be &fe->id2.i_list. Otherwise
 * the result may be wrong.
 */
static inline int ocfs2_calc_extend_credits(struct super_block *sb,
					    struct ocfs2_extent_list *root_el)
{
	int bitmap_blocks, sysfile_bitmap_blocks, extent_blocks;

	/* bitmap dinode, group desc. + relinked group. */
	bitmap_blocks = OCFS2_SUBALLOC_ALLOC;

	/* we might need to shift tree depth so lets assume an
	 * absolute worst case of complete fragmentation.  Even with
	 * that, we only need one update for the dinode, and then
	 * however many metadata chunks needed * a remaining suballoc
	 * alloc. */
	sysfile_bitmap_blocks = 1 +
		(OCFS2_SUBALLOC_ALLOC - 1) * ocfs2_extend_meta_needed(root_el);

	/* this does not include *new* metadata blocks, which are
	 * accounted for in sysfile_bitmap_blocks. root_el +
	 * prev. last_eb_blk + blocks along edge of tree.
	 * calc_symlink_credits passes because we just need 1
	 * credit for the dinode there. */
	extent_blocks = 1 + 1 + le16_to_cpu(root_el->l_tree_depth);

	return bitmap_blocks + sysfile_bitmap_blocks + extent_blocks +
	       ocfs2_quota_trans_credits(sb);
}

static inline int ocfs2_calc_symlink_credits(struct super_block *sb)
{
	int blocks = ocfs2_mknod_credits(sb, 0, 0);

	/* links can be longer than one block so we may update many
	 * within our single allocated extent. */
	blocks += ocfs2_clusters_to_blocks(sb, 1);

	return blocks + ocfs2_quota_trans_credits(sb);
}

static inline int ocfs2_calc_group_alloc_credits(struct super_block *sb,
						 unsigned int cpg)
{
	int blocks;
	int bitmap_blocks = OCFS2_SUBALLOC_ALLOC + 1;
	/* parent inode update + new block group header + bitmap inode update
	   + bitmap blocks affected */
	blocks = 1 + 1 + 1 + bitmap_blocks;
	return blocks;
}

/*
 * Allocating a discontiguous block group requires the credits from
 * ocfs2_calc_group_alloc_credits() as well as enough credits to fill
 * the group descriptor's extent list.  The caller already has started
 * the transaction with ocfs2_calc_group_alloc_credits().  They extend
 * it with these credits.
 */
static inline int ocfs2_calc_bg_discontig_credits(struct super_block *sb)
{
	return ocfs2_extent_recs_per_gd(sb);
}

static inline int ocfs2_jbd2_inode_add_write(handle_t *handle, struct inode *inode,
					     loff_t start_byte, loff_t length)
{
	return jbd2_journal_inode_ranged_write(handle,
					       &OCFS2_I(inode)->ip_jinode,
					       start_byte, length);
}

static inline int ocfs2_begin_ordered_truncate(struct inode *inode,
					       loff_t new_size)
{
	return jbd2_journal_begin_ordered_truncate(
				OCFS2_SB(inode->i_sb)->journal->j_journal,
				&OCFS2_I(inode)->ip_jinode,
				new_size);
}

static inline void ocfs2_update_inode_fsync_trans(handle_t *handle,
						  struct inode *inode,
						  int datasync)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	if (!is_handle_aborted(handle)) {
		oi->i_sync_tid = handle->h_transaction->t_tid;
		if (datasync)
			oi->i_datasync_tid = handle->h_transaction->t_tid;
	}
}

#endif /* OCFS2_JOURNAL_H */
