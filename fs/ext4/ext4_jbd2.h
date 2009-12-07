/*
 * ext4_jbd2.h
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1999
 *
 * Copyright 1998--1999 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Ext4-specific journaling extensions.
 */

#ifndef _EXT4_JBD2_H
#define _EXT4_JBD2_H

#include <linux/fs.h>
#include <linux/jbd2.h>
#include "ext4.h"

#define EXT4_JOURNAL(inode)	(EXT4_SB((inode)->i_sb)->s_journal)

/* Define the number of blocks we need to account to a transaction to
 * modify one block of data.
 *
 * We may have to touch one inode, one bitmap buffer, up to three
 * indirection blocks, the group and superblock summaries, and the data
 * block to complete the transaction.
 *
 * For extents-enabled fs we may have to allocate and modify up to
 * 5 levels of tree + root which are stored in the inode. */

#define EXT4_SINGLEDATA_TRANS_BLOCKS(sb)				\
	(EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_EXTENTS)   \
	 ? 27U : 8U)

/* Extended attribute operations touch at most two data buffers,
 * two bitmap buffers, and two group summaries, in addition to the inode
 * and the superblock, which are already accounted for. */

#define EXT4_XATTR_TRANS_BLOCKS		6U

/* Define the minimum size for a transaction which modifies data.  This
 * needs to take into account the fact that we may end up modifying two
 * quota files too (one for the group, one for the user quota).  The
 * superblock only gets updated once, of course, so don't bother
 * counting that again for the quota updates. */

#define EXT4_DATA_TRANS_BLOCKS(sb)	(EXT4_SINGLEDATA_TRANS_BLOCKS(sb) + \
					 EXT4_XATTR_TRANS_BLOCKS - 2 + \
					 2*EXT4_QUOTA_TRANS_BLOCKS(sb))

/*
 * Define the number of metadata blocks we need to account to modify data.
 *
 * This include super block, inode block, quota blocks and xattr blocks
 */
#define EXT4_META_TRANS_BLOCKS(sb)	(EXT4_XATTR_TRANS_BLOCKS + \
					2*EXT4_QUOTA_TRANS_BLOCKS(sb))

/* Delete operations potentially hit one directory's namespace plus an
 * entire inode, plus arbitrary amounts of bitmap/indirection data.  Be
 * generous.  We can grow the delete transaction later if necessary. */

#define EXT4_DELETE_TRANS_BLOCKS(sb)	(2 * EXT4_DATA_TRANS_BLOCKS(sb) + 64)

/* Define an arbitrary limit for the amount of data we will anticipate
 * writing to any given transaction.  For unbounded transactions such as
 * write(2) and truncate(2) we can write more than this, but we always
 * start off at the maximum transaction size and grow the transaction
 * optimistically as we go. */

#define EXT4_MAX_TRANS_DATA		64U

/* We break up a large truncate or write transaction once the handle's
 * buffer credits gets this low, we need either to extend the
 * transaction or to start a new one.  Reserve enough space here for
 * inode, bitmap, superblock, group and indirection updates for at least
 * one block, plus two quota updates.  Quota allocations are not
 * needed. */

#define EXT4_RESERVE_TRANS_BLOCKS	12U

#define EXT4_INDEX_EXTRA_TRANS_BLOCKS	8

#ifdef CONFIG_QUOTA
/* Amount of blocks needed for quota update - we know that the structure was
 * allocated so we need to update only inode+data */
#define EXT4_QUOTA_TRANS_BLOCKS(sb) (test_opt(sb, QUOTA) ? 2 : 0)
/* Amount of blocks needed for quota insert/delete - we do some block writes
 * but inode, sb and group updates are done only once */
#define EXT4_QUOTA_INIT_BLOCKS(sb) (test_opt(sb, QUOTA) ? (DQUOT_INIT_ALLOC*\
		(EXT4_SINGLEDATA_TRANS_BLOCKS(sb)-3)+3+DQUOT_INIT_REWRITE) : 0)
#define EXT4_QUOTA_DEL_BLOCKS(sb) (test_opt(sb, QUOTA) ? (DQUOT_DEL_ALLOC*\
		(EXT4_SINGLEDATA_TRANS_BLOCKS(sb)-3)+3+DQUOT_DEL_REWRITE) : 0)
#else
#define EXT4_QUOTA_TRANS_BLOCKS(sb) 0
#define EXT4_QUOTA_INIT_BLOCKS(sb) 0
#define EXT4_QUOTA_DEL_BLOCKS(sb) 0
#endif

int
ext4_mark_iloc_dirty(handle_t *handle,
		     struct inode *inode,
		     struct ext4_iloc *iloc);

/*
 * On success, We end up with an outstanding reference count against
 * iloc->bh.  This _must_ be cleaned up later.
 */

int ext4_reserve_inode_write(handle_t *handle, struct inode *inode,
			struct ext4_iloc *iloc);

int ext4_mark_inode_dirty(handle_t *handle, struct inode *inode);

/*
 * Wrapper functions with which ext4 calls into JBD.  The intent here is
 * to allow these to be turned into appropriate stubs so ext4 can control
 * ext2 filesystems, so ext2+ext4 systems only nee one fs.  This work hasn't
 * been done yet.
 */

void ext4_journal_abort_handle(const char *caller, const char *err_fn,
		struct buffer_head *bh, handle_t *handle, int err);

int __ext4_journal_get_undo_access(const char *where, handle_t *handle,
				struct buffer_head *bh);

int __ext4_journal_get_write_access(const char *where, handle_t *handle,
				struct buffer_head *bh);

/* When called with an invalid handle, this will still do a put on the BH */
int __ext4_journal_forget(const char *where, handle_t *handle,
				struct buffer_head *bh);

/* When called with an invalid handle, this will still do a put on the BH */
int __ext4_journal_revoke(const char *where, handle_t *handle,
				ext4_fsblk_t blocknr, struct buffer_head *bh);

int __ext4_journal_get_create_access(const char *where,
				handle_t *handle, struct buffer_head *bh);

int __ext4_handle_dirty_metadata(const char *where, handle_t *handle,
				 struct inode *inode, struct buffer_head *bh);

#define ext4_journal_get_undo_access(handle, bh) \
	__ext4_journal_get_undo_access(__func__, (handle), (bh))
#define ext4_journal_get_write_access(handle, bh) \
	__ext4_journal_get_write_access(__func__, (handle), (bh))
#define ext4_journal_revoke(handle, blocknr, bh) \
	__ext4_journal_revoke(__func__, (handle), (blocknr), (bh))
#define ext4_journal_get_create_access(handle, bh) \
	__ext4_journal_get_create_access(__func__, (handle), (bh))
#define ext4_journal_forget(handle, bh) \
	__ext4_journal_forget(__func__, (handle), (bh))
#define ext4_handle_dirty_metadata(handle, inode, bh) \
	__ext4_handle_dirty_metadata(__func__, (handle), (inode), (bh))

handle_t *ext4_journal_start_sb(struct super_block *sb, int nblocks);
int __ext4_journal_stop(const char *where, handle_t *handle);

#define EXT4_NOJOURNAL_MAX_REF_COUNT ((unsigned long) 4096)

/* Note:  Do not use this for NULL handles.  This is only to determine if
 * a properly allocated handle is using a journal or not. */
static inline int ext4_handle_valid(handle_t *handle)
{
	if ((unsigned long)handle < EXT4_NOJOURNAL_MAX_REF_COUNT)
		return 0;
	return 1;
}

static inline void ext4_handle_sync(handle_t *handle)
{
	if (ext4_handle_valid(handle))
		handle->h_sync = 1;
}

static inline void ext4_handle_release_buffer(handle_t *handle,
						struct buffer_head *bh)
{
	if (ext4_handle_valid(handle))
		jbd2_journal_release_buffer(handle, bh);
}

static inline int ext4_handle_is_aborted(handle_t *handle)
{
	if (ext4_handle_valid(handle))
		return is_handle_aborted(handle);
	return 0;
}

static inline int ext4_handle_has_enough_credits(handle_t *handle, int needed)
{
	if (ext4_handle_valid(handle) && handle->h_buffer_credits < needed)
		return 0;
	return 1;
}

static inline void ext4_journal_release_buffer(handle_t *handle,
						struct buffer_head *bh)
{
	if (ext4_handle_valid(handle))
		jbd2_journal_release_buffer(handle, bh);
}

static inline handle_t *ext4_journal_start(struct inode *inode, int nblocks)
{
	return ext4_journal_start_sb(inode->i_sb, nblocks);
}

#define ext4_journal_stop(handle) \
	__ext4_journal_stop(__func__, (handle))

static inline handle_t *ext4_journal_current_handle(void)
{
	return journal_current_handle();
}

static inline int ext4_journal_extend(handle_t *handle, int nblocks)
{
	if (ext4_handle_valid(handle))
		return jbd2_journal_extend(handle, nblocks);
	return 0;
}

static inline int ext4_journal_restart(handle_t *handle, int nblocks)
{
	if (ext4_handle_valid(handle))
		return jbd2_journal_restart(handle, nblocks);
	return 0;
}

static inline int ext4_journal_blocks_per_page(struct inode *inode)
{
	if (EXT4_JOURNAL(inode) != NULL)
		return jbd2_journal_blocks_per_page(inode);
	return 0;
}

static inline int ext4_journal_force_commit(journal_t *journal)
{
	if (journal)
		return jbd2_journal_force_commit(journal);
	return 0;
}

static inline int ext4_jbd2_file_inode(handle_t *handle, struct inode *inode)
{
	if (ext4_handle_valid(handle))
		return jbd2_journal_file_inode(handle, &EXT4_I(inode)->jinode);
	return 0;
}

/* super.c */
int ext4_force_commit(struct super_block *sb);

static inline int ext4_should_journal_data(struct inode *inode)
{
	if (EXT4_JOURNAL(inode) == NULL)
		return 0;
	if (!S_ISREG(inode->i_mode))
		return 1;
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA)
		return 1;
	if (EXT4_I(inode)->i_flags & EXT4_JOURNAL_DATA_FL)
		return 1;
	return 0;
}

static inline int ext4_should_order_data(struct inode *inode)
{
	if (EXT4_JOURNAL(inode) == NULL)
		return 0;
	if (!S_ISREG(inode->i_mode))
		return 0;
	if (EXT4_I(inode)->i_flags & EXT4_JOURNAL_DATA_FL)
		return 0;
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA)
		return 1;
	return 0;
}

static inline int ext4_should_writeback_data(struct inode *inode)
{
	if (!S_ISREG(inode->i_mode))
		return 0;
	if (EXT4_JOURNAL(inode) == NULL)
		return 1;
	if (EXT4_I(inode)->i_flags & EXT4_JOURNAL_DATA_FL)
		return 0;
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_WRITEBACK_DATA)
		return 1;
	return 0;
}

#endif	/* _EXT4_JBD2_H */
