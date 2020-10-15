// SPDX-License-Identifier: GPL-2.0

/*
 * fs/ext4/fast_commit.c
 *
 * Written by Harshad Shirwadkar <harshadshirwadkar@gmail.com>
 *
 * Ext4 fast commits routines.
 */
#include "ext4.h"
#include "ext4_jbd2.h"
#include "ext4_extents.h"
#include "mballoc.h"

/*
 * Ext4 Fast Commits
 * -----------------
 *
 * Ext4 fast commits implement fine grained journalling for Ext4.
 *
 * Fast commits are organized as a log of tag-length-value (TLV) structs. (See
 * struct ext4_fc_tl). Each TLV contains some delta that is replayed TLV by
 * TLV during the recovery phase. For the scenarios for which we currently
 * don't have replay code, fast commit falls back to full commits.
 * Fast commits record delta in one of the following three categories.
 *
 * (A) Directory entry updates:
 *
 * - EXT4_FC_TAG_UNLINK		- records directory entry unlink
 * - EXT4_FC_TAG_LINK		- records directory entry link
 * - EXT4_FC_TAG_CREAT		- records inode and directory entry creation
 *
 * (B) File specific data range updates:
 *
 * - EXT4_FC_TAG_ADD_RANGE	- records addition of new blocks to an inode
 * - EXT4_FC_TAG_DEL_RANGE	- records deletion of blocks from an inode
 *
 * (C) Inode metadata (mtime / ctime etc):
 *
 * - EXT4_FC_TAG_INODE		- record the inode that should be replayed
 *				  during recovery. Note that iblocks field is
 *				  not replayed and instead derived during
 *				  replay.
 * Commit Operation
 * ----------------
 * With fast commits, we maintain all the directory entry operations in the
 * order in which they are issued in an in-memory queue. This queue is flushed
 * to disk during the commit operation. We also maintain a list of inodes
 * that need to be committed during a fast commit in another in memory queue of
 * inodes. During the commit operation, we commit in the following order:
 *
 * [1] Lock inodes for any further data updates by setting COMMITTING state
 * [2] Submit data buffers of all the inodes
 * [3] Wait for [2] to complete
 * [4] Commit all the directory entry updates in the fast commit space
 * [5] Commit all the changed inode structures
 * [6] Write tail tag (this tag ensures the atomicity, please read the following
 *     section for more details).
 * [7] Wait for [4], [5] and [6] to complete.
 *
 * All the inode updates must call ext4_fc_start_update() before starting an
 * update. If such an ongoing update is present, fast commit waits for it to
 * complete. The completion of such an update is marked by
 * ext4_fc_stop_update().
 *
 * Fast Commit Ineligibility
 * -------------------------
 * Not all operations are supported by fast commits today (e.g extended
 * attributes). Fast commit ineligiblity is marked by calling one of the
 * two following functions:
 *
 * - ext4_fc_mark_ineligible(): This makes next fast commit operation to fall
 *   back to full commit. This is useful in case of transient errors.
 *
 * - ext4_fc_start_ineligible() and ext4_fc_stop_ineligible() - This makes all
 *   the fast commits happening between ext4_fc_start_ineligible() and
 *   ext4_fc_stop_ineligible() and one fast commit after the call to
 *   ext4_fc_stop_ineligible() to fall back to full commits. It is important to
 *   make one more fast commit to fall back to full commit after stop call so
 *   that it guaranteed that the fast commit ineligible operation contained
 *   within ext4_fc_start_ineligible() and ext4_fc_stop_ineligible() is
 *   followed by at least 1 full commit.
 *
 * Atomicity of commits
 * --------------------
 * In order to gaurantee atomicity during the commit operation, fast commit
 * uses "EXT4_FC_TAG_TAIL" tag that marks a fast commit as complete. Tail
 * tag contains CRC of the contents and TID of the transaction after which
 * this fast commit should be applied. Recovery code replays fast commit
 * logs only if there's at least 1 valid tail present. For every fast commit
 * operation, there is 1 tail. This means, we may end up with multiple tails
 * in the fast commit space. Here's an example:
 *
 * - Create a new file A and remove existing file B
 * - fsync()
 * - Append contents to file A
 * - Truncate file A
 * - fsync()
 *
 * The fast commit space at the end of above operations would look like this:
 *      [HEAD] [CREAT A] [UNLINK B] [TAIL] [ADD_RANGE A] [DEL_RANGE A] [TAIL]
 *             |<---  Fast Commit 1   --->|<---      Fast Commit 2     ---->|
 *
 * Replay code should thus check for all the valid tails in the FC area.
 *
 * TODOs
 * -----
 * 1) Make fast commit atomic updates more fine grained. Today, a fast commit
 *    eligible update must be protected within ext4_fc_start_update() and
 *    ext4_fc_stop_update(). These routines are called at much higher
 *    routines. This can be made more fine grained by combining with
 *    ext4_journal_start().
 *
 * 2) Same above for ext4_fc_start_ineligible() and ext4_fc_stop_ineligible()
 *
 * 3) Handle more ineligible cases.
 */

#include <trace/events/ext4.h>
static struct kmem_cache *ext4_fc_dentry_cachep;

static void ext4_end_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	BUFFER_TRACE(bh, "");
	if (uptodate) {
		ext4_debug("%s: Block %lld up-to-date",
			   __func__, bh->b_blocknr);
		set_buffer_uptodate(bh);
	} else {
		ext4_debug("%s: Block %lld not up-to-date",
			   __func__, bh->b_blocknr);
		clear_buffer_uptodate(bh);
	}

	unlock_buffer(bh);
}

static inline void ext4_fc_reset_inode(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	ei->i_fc_lblk_start = 0;
	ei->i_fc_lblk_len = 0;
}

void ext4_fc_init_inode(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	ext4_fc_reset_inode(inode);
	ext4_clear_inode_state(inode, EXT4_STATE_FC_COMMITTING);
	INIT_LIST_HEAD(&ei->i_fc_list);
	init_waitqueue_head(&ei->i_fc_wait);
	atomic_set(&ei->i_fc_updates, 0);
	ei->i_fc_committed_subtid = 0;
}

/*
 * Inform Ext4's fast about start of an inode update
 *
 * This function is called by the high level call VFS callbacks before
 * performing any inode update. This function blocks if there's an ongoing
 * fast commit on the inode in question.
 */
void ext4_fc_start_update(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	if (!test_opt2(inode->i_sb, JOURNAL_FAST_COMMIT))
		return;

restart:
	spin_lock(&EXT4_SB(inode->i_sb)->s_fc_lock);
	if (list_empty(&ei->i_fc_list))
		goto out;

	if (ext4_test_inode_state(inode, EXT4_STATE_FC_COMMITTING)) {
		wait_queue_head_t *wq;
#if (BITS_PER_LONG < 64)
		DEFINE_WAIT_BIT(wait, &ei->i_state_flags,
				EXT4_STATE_FC_COMMITTING);
		wq = bit_waitqueue(&ei->i_state_flags,
				   EXT4_STATE_FC_COMMITTING);
#else
		DEFINE_WAIT_BIT(wait, &ei->i_flags,
				EXT4_STATE_FC_COMMITTING);
		wq = bit_waitqueue(&ei->i_flags,
				   EXT4_STATE_FC_COMMITTING);
#endif
		prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
		spin_unlock(&EXT4_SB(inode->i_sb)->s_fc_lock);
		schedule();
		finish_wait(wq, &wait.wq_entry);
		goto restart;
	}
out:
	atomic_inc(&ei->i_fc_updates);
	spin_unlock(&EXT4_SB(inode->i_sb)->s_fc_lock);
}

/*
 * Stop inode update and wake up waiting fast commits if any.
 */
void ext4_fc_stop_update(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	if (!test_opt2(inode->i_sb, JOURNAL_FAST_COMMIT))
		return;

	if (atomic_dec_and_test(&ei->i_fc_updates))
		wake_up_all(&ei->i_fc_wait);
}

/*
 * Remove inode from fast commit list. If the inode is being committed
 * we wait until inode commit is done.
 */
void ext4_fc_del(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);

	if (!test_opt2(inode->i_sb, JOURNAL_FAST_COMMIT))
		return;


	if (!test_opt2(inode->i_sb, JOURNAL_FAST_COMMIT))
		return;

restart:
	spin_lock(&EXT4_SB(inode->i_sb)->s_fc_lock);
	if (list_empty(&ei->i_fc_list)) {
		spin_unlock(&EXT4_SB(inode->i_sb)->s_fc_lock);
		return;
	}

	if (ext4_test_inode_state(inode, EXT4_STATE_FC_COMMITTING)) {
		wait_queue_head_t *wq;
#if (BITS_PER_LONG < 64)
		DEFINE_WAIT_BIT(wait, &ei->i_state_flags,
				EXT4_STATE_FC_COMMITTING);
		wq = bit_waitqueue(&ei->i_state_flags,
				   EXT4_STATE_FC_COMMITTING);
#else
		DEFINE_WAIT_BIT(wait, &ei->i_flags,
				EXT4_STATE_FC_COMMITTING);
		wq = bit_waitqueue(&ei->i_flags,
				   EXT4_STATE_FC_COMMITTING);
#endif
		prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
		spin_unlock(&EXT4_SB(inode->i_sb)->s_fc_lock);
		schedule();
		finish_wait(wq, &wait.wq_entry);
		goto restart;
	}
	if (!list_empty(&ei->i_fc_list))
		list_del_init(&ei->i_fc_list);
	spin_unlock(&EXT4_SB(inode->i_sb)->s_fc_lock);
}

/*
 * Mark file system as fast commit ineligible. This means that next commit
 * operation would result in a full jbd2 commit.
 */
void ext4_fc_mark_ineligible(struct super_block *sb, int reason)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	sbi->s_mount_state |= EXT4_FC_INELIGIBLE;
	WARN_ON(reason >= EXT4_FC_REASON_MAX);
	sbi->s_fc_stats.fc_ineligible_reason_count[reason]++;
}

/*
 * Start a fast commit ineligible update. Any commits that happen while
 * such an operation is in progress fall back to full commits.
 */
void ext4_fc_start_ineligible(struct super_block *sb, int reason)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	WARN_ON(reason >= EXT4_FC_REASON_MAX);
	sbi->s_fc_stats.fc_ineligible_reason_count[reason]++;
	atomic_inc(&sbi->s_fc_ineligible_updates);
}

/*
 * Stop a fast commit ineligible update. We set EXT4_FC_INELIGIBLE flag here
 * to ensure that after stopping the ineligible update, at least one full
 * commit takes place.
 */
void ext4_fc_stop_ineligible(struct super_block *sb)
{
	EXT4_SB(sb)->s_mount_state |= EXT4_FC_INELIGIBLE;
	atomic_dec(&EXT4_SB(sb)->s_fc_ineligible_updates);
}

static inline int ext4_fc_is_ineligible(struct super_block *sb)
{
	return (EXT4_SB(sb)->s_mount_state & EXT4_FC_INELIGIBLE) ||
		atomic_read(&EXT4_SB(sb)->s_fc_ineligible_updates);
}

/*
 * Generic fast commit tracking function. If this is the first time this we are
 * called after a full commit, we initialize fast commit fields and then call
 * __fc_track_fn() with update = 0. If we have already been called after a full
 * commit, we pass update = 1. Based on that, the track function can determine
 * if it needs to track a field for the first time or if it needs to just
 * update the previously tracked value.
 *
 * If enqueue is set, this function enqueues the inode in fast commit list.
 */
static int ext4_fc_track_template(
	struct inode *inode, int (*__fc_track_fn)(struct inode *, void *, bool),
	void *args, int enqueue)
{
	tid_t running_txn_tid;
	bool update = false;
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	int ret;

	if (!test_opt2(inode->i_sb, JOURNAL_FAST_COMMIT))
		return -EOPNOTSUPP;

	if (ext4_fc_is_ineligible(inode->i_sb))
		return -EINVAL;

	running_txn_tid = sbi->s_journal ?
		sbi->s_journal->j_commit_sequence + 1 : 0;

	mutex_lock(&ei->i_fc_lock);
	if (running_txn_tid == ei->i_sync_tid) {
		update = true;
	} else {
		ext4_fc_reset_inode(inode);
		ei->i_sync_tid = running_txn_tid;
	}
	ret = __fc_track_fn(inode, args, update);
	mutex_unlock(&ei->i_fc_lock);

	if (!enqueue)
		return ret;

	spin_lock(&sbi->s_fc_lock);
	if (list_empty(&EXT4_I(inode)->i_fc_list))
		list_add_tail(&EXT4_I(inode)->i_fc_list,
				(sbi->s_mount_state & EXT4_FC_COMMITTING) ?
				&sbi->s_fc_q[FC_Q_STAGING] :
				&sbi->s_fc_q[FC_Q_MAIN]);
	spin_unlock(&sbi->s_fc_lock);

	return ret;
}

struct __track_dentry_update_args {
	struct dentry *dentry;
	int op;
};

/* __track_fn for directory entry updates. Called with ei->i_fc_lock. */
static int __track_dentry_update(struct inode *inode, void *arg, bool update)
{
	struct ext4_fc_dentry_update *node;
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct __track_dentry_update_args *dentry_update =
		(struct __track_dentry_update_args *)arg;
	struct dentry *dentry = dentry_update->dentry;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

	mutex_unlock(&ei->i_fc_lock);
	node = kmem_cache_alloc(ext4_fc_dentry_cachep, GFP_NOFS);
	if (!node) {
		ext4_fc_mark_ineligible(inode->i_sb, EXT4_FC_REASON_MEM);
		mutex_lock(&ei->i_fc_lock);
		return -ENOMEM;
	}

	node->fcd_op = dentry_update->op;
	node->fcd_parent = dentry->d_parent->d_inode->i_ino;
	node->fcd_ino = inode->i_ino;
	if (dentry->d_name.len > DNAME_INLINE_LEN) {
		node->fcd_name.name = kmalloc(dentry->d_name.len, GFP_NOFS);
		if (!node->fcd_name.name) {
			kmem_cache_free(ext4_fc_dentry_cachep, node);
			ext4_fc_mark_ineligible(inode->i_sb,
				EXT4_FC_REASON_MEM);
			mutex_lock(&ei->i_fc_lock);
			return -ENOMEM;
		}
		memcpy((u8 *)node->fcd_name.name, dentry->d_name.name,
			dentry->d_name.len);
	} else {
		memcpy(node->fcd_iname, dentry->d_name.name,
			dentry->d_name.len);
		node->fcd_name.name = node->fcd_iname;
	}
	node->fcd_name.len = dentry->d_name.len;

	spin_lock(&sbi->s_fc_lock);
	if (sbi->s_mount_state & EXT4_FC_COMMITTING)
		list_add_tail(&node->fcd_list,
				&sbi->s_fc_dentry_q[FC_Q_STAGING]);
	else
		list_add_tail(&node->fcd_list, &sbi->s_fc_dentry_q[FC_Q_MAIN]);
	spin_unlock(&sbi->s_fc_lock);
	mutex_lock(&ei->i_fc_lock);

	return 0;
}

void ext4_fc_track_unlink(struct inode *inode, struct dentry *dentry)
{
	struct __track_dentry_update_args args;
	int ret;

	args.dentry = dentry;
	args.op = EXT4_FC_TAG_UNLINK;

	ret = ext4_fc_track_template(inode, __track_dentry_update,
					(void *)&args, 0);
	trace_ext4_fc_track_unlink(inode, dentry, ret);
}

void ext4_fc_track_link(struct inode *inode, struct dentry *dentry)
{
	struct __track_dentry_update_args args;
	int ret;

	args.dentry = dentry;
	args.op = EXT4_FC_TAG_LINK;

	ret = ext4_fc_track_template(inode, __track_dentry_update,
					(void *)&args, 0);
	trace_ext4_fc_track_link(inode, dentry, ret);
}

void ext4_fc_track_create(struct inode *inode, struct dentry *dentry)
{
	struct __track_dentry_update_args args;
	int ret;

	args.dentry = dentry;
	args.op = EXT4_FC_TAG_CREAT;

	ret = ext4_fc_track_template(inode, __track_dentry_update,
					(void *)&args, 0);
	trace_ext4_fc_track_create(inode, dentry, ret);
}

/* __track_fn for inode tracking */
static int __track_inode(struct inode *inode, void *arg, bool update)
{
	if (update)
		return -EEXIST;

	EXT4_I(inode)->i_fc_lblk_len = 0;

	return 0;
}

void ext4_fc_track_inode(struct inode *inode)
{
	int ret;

	if (S_ISDIR(inode->i_mode))
		return;

	ret = ext4_fc_track_template(inode, __track_inode, NULL, 1);
	trace_ext4_fc_track_inode(inode, ret);
}

struct __track_range_args {
	ext4_lblk_t start, end;
};

/* __track_fn for tracking data updates */
static int __track_range(struct inode *inode, void *arg, bool update)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	ext4_lblk_t oldstart;
	struct __track_range_args *__arg =
		(struct __track_range_args *)arg;

	if (inode->i_ino < EXT4_FIRST_INO(inode->i_sb)) {
		ext4_debug("Special inode %ld being modified\n", inode->i_ino);
		return -ECANCELED;
	}

	oldstart = ei->i_fc_lblk_start;

	if (update && ei->i_fc_lblk_len > 0) {
		ei->i_fc_lblk_start = min(ei->i_fc_lblk_start, __arg->start);
		ei->i_fc_lblk_len =
			max(oldstart + ei->i_fc_lblk_len - 1, __arg->end) -
				ei->i_fc_lblk_start + 1;
	} else {
		ei->i_fc_lblk_start = __arg->start;
		ei->i_fc_lblk_len = __arg->end - __arg->start + 1;
	}

	return 0;
}

void ext4_fc_track_range(struct inode *inode, ext4_lblk_t start,
			 ext4_lblk_t end)
{
	struct __track_range_args args;
	int ret;

	if (S_ISDIR(inode->i_mode))
		return;

	args.start = start;
	args.end = end;

	ret = ext4_fc_track_template(inode,  __track_range, &args, 1);

	trace_ext4_fc_track_range(inode, start, end, ret);
}

static void ext4_fc_submit_bh(struct super_block *sb)
{
	int write_flags = REQ_SYNC;
	struct buffer_head *bh = EXT4_SB(sb)->s_fc_bh;

	if (test_opt(sb, BARRIER))
		write_flags |= REQ_FUA | REQ_PREFLUSH;
	lock_buffer(bh);
	clear_buffer_dirty(bh);
	set_buffer_uptodate(bh);
	bh->b_end_io = ext4_end_buffer_io_sync;
	submit_bh(REQ_OP_WRITE, write_flags, bh);
	EXT4_SB(sb)->s_fc_bh = NULL;
}

/* Ext4 commit path routines */

/* memzero and update CRC */
static void *ext4_fc_memzero(struct super_block *sb, void *dst, int len,
				u32 *crc)
{
	void *ret;

	ret = memset(dst, 0, len);
	if (crc)
		*crc = ext4_chksum(EXT4_SB(sb), *crc, dst, len);
	return ret;
}

/*
 * Allocate len bytes on a fast commit buffer.
 *
 * During the commit time this function is used to manage fast commit
 * block space. We don't split a fast commit log onto different
 * blocks. So this function makes sure that if there's not enough space
 * on the current block, the remaining space in the current block is
 * marked as unused by adding EXT4_FC_TAG_PAD tag. In that case,
 * new block is from jbd2 and CRC is updated to reflect the padding
 * we added.
 */
static u8 *ext4_fc_reserve_space(struct super_block *sb, int len, u32 *crc)
{
	struct ext4_fc_tl *tl;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct buffer_head *bh;
	int bsize = sbi->s_journal->j_blocksize;
	int ret, off = sbi->s_fc_bytes % bsize;
	int pad_len;

	/*
	 * After allocating len, we should have space at least for a 0 byte
	 * padding.
	 */
	if (len + sizeof(struct ext4_fc_tl) > bsize)
		return NULL;

	if (bsize - off - 1 > len + sizeof(struct ext4_fc_tl)) {
		/*
		 * Only allocate from current buffer if we have enough space for
		 * this request AND we have space to add a zero byte padding.
		 */
		if (!sbi->s_fc_bh) {
			ret = jbd2_fc_get_buf(EXT4_SB(sb)->s_journal, &bh);
			if (ret)
				return NULL;
			sbi->s_fc_bh = bh;
		}
		sbi->s_fc_bytes += len;
		return sbi->s_fc_bh->b_data + off;
	}
	/* Need to add PAD tag */
	tl = (struct ext4_fc_tl *)(sbi->s_fc_bh->b_data + off);
	tl->fc_tag = cpu_to_le16(EXT4_FC_TAG_PAD);
	pad_len = bsize - off - 1 - sizeof(struct ext4_fc_tl);
	tl->fc_len = cpu_to_le16(pad_len);
	if (crc)
		*crc = ext4_chksum(sbi, *crc, tl, sizeof(*tl));
	if (pad_len > 0)
		ext4_fc_memzero(sb, tl + 1, pad_len, crc);
	ext4_fc_submit_bh(sb);

	ret = jbd2_fc_get_buf(EXT4_SB(sb)->s_journal, &bh);
	if (ret)
		return NULL;
	sbi->s_fc_bh = bh;
	sbi->s_fc_bytes = (sbi->s_fc_bytes / bsize + 1) * bsize + len;
	return sbi->s_fc_bh->b_data;
}

/* memcpy to fc reserved space and update CRC */
static void *ext4_fc_memcpy(struct super_block *sb, void *dst, const void *src,
				int len, u32 *crc)
{
	if (crc)
		*crc = ext4_chksum(EXT4_SB(sb), *crc, src, len);
	return memcpy(dst, src, len);
}

/*
 * Complete a fast commit by writing tail tag.
 *
 * Writing tail tag marks the end of a fast commit. In order to guarantee
 * atomicity, after writing tail tag, even if there's space remaining
 * in the block, next commit shouldn't use it. That's why tail tag
 * has the length as that of the remaining space on the block.
 */
static int ext4_fc_write_tail(struct super_block *sb, u32 crc)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_fc_tl tl;
	struct ext4_fc_tail tail;
	int off, bsize = sbi->s_journal->j_blocksize;
	u8 *dst;

	/*
	 * ext4_fc_reserve_space takes care of allocating an extra block if
	 * there's no enough space on this block for accommodating this tail.
	 */
	dst = ext4_fc_reserve_space(sb, sizeof(tl) + sizeof(tail), &crc);
	if (!dst)
		return -ENOSPC;

	off = sbi->s_fc_bytes % bsize;

	tl.fc_tag = cpu_to_le16(EXT4_FC_TAG_TAIL);
	tl.fc_len = cpu_to_le16(bsize - off - 1 + sizeof(struct ext4_fc_tail));
	sbi->s_fc_bytes = round_up(sbi->s_fc_bytes, bsize);

	ext4_fc_memcpy(sb, dst, &tl, sizeof(tl), &crc);
	dst += sizeof(tl);
	tail.fc_tid = cpu_to_le32(sbi->s_journal->j_running_transaction->t_tid);
	ext4_fc_memcpy(sb, dst, &tail.fc_tid, sizeof(tail.fc_tid), &crc);
	dst += sizeof(tail.fc_tid);
	tail.fc_crc = cpu_to_le32(crc);
	ext4_fc_memcpy(sb, dst, &tail.fc_crc, sizeof(tail.fc_crc), NULL);

	ext4_fc_submit_bh(sb);

	return 0;
}

/*
 * Adds tag, length, value and updates CRC. Returns true if tlv was added.
 * Returns false if there's not enough space.
 */
static bool ext4_fc_add_tlv(struct super_block *sb, u16 tag, u16 len, u8 *val,
			   u32 *crc)
{
	struct ext4_fc_tl tl;
	u8 *dst;

	dst = ext4_fc_reserve_space(sb, sizeof(tl) + len, crc);
	if (!dst)
		return false;

	tl.fc_tag = cpu_to_le16(tag);
	tl.fc_len = cpu_to_le16(len);

	ext4_fc_memcpy(sb, dst, &tl, sizeof(tl), crc);
	ext4_fc_memcpy(sb, dst + sizeof(tl), val, len, crc);

	return true;
}

/* Same as above, but adds dentry tlv. */
static  bool ext4_fc_add_dentry_tlv(struct super_block *sb, u16 tag,
					int parent_ino, int ino, int dlen,
					const unsigned char *dname,
					u32 *crc)
{
	struct ext4_fc_dentry_info fcd;
	struct ext4_fc_tl tl;
	u8 *dst = ext4_fc_reserve_space(sb, sizeof(tl) + sizeof(fcd) + dlen,
					crc);

	if (!dst)
		return false;

	fcd.fc_parent_ino = cpu_to_le32(parent_ino);
	fcd.fc_ino = cpu_to_le32(ino);
	tl.fc_tag = cpu_to_le16(tag);
	tl.fc_len = cpu_to_le16(sizeof(fcd) + dlen);
	ext4_fc_memcpy(sb, dst, &tl, sizeof(tl), crc);
	dst += sizeof(tl);
	ext4_fc_memcpy(sb, dst, &fcd, sizeof(fcd), crc);
	dst += sizeof(fcd);
	ext4_fc_memcpy(sb, dst, dname, dlen, crc);
	dst += dlen;

	return true;
}

/*
 * Writes inode in the fast commit space under TLV with tag @tag.
 * Returns 0 on success, error on failure.
 */
static int ext4_fc_write_inode(struct inode *inode, u32 *crc)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	int inode_len = EXT4_GOOD_OLD_INODE_SIZE;
	int ret;
	struct ext4_iloc iloc;
	struct ext4_fc_inode fc_inode;
	struct ext4_fc_tl tl;
	u8 *dst;

	ret = ext4_get_inode_loc(inode, &iloc);
	if (ret)
		return ret;

	if (EXT4_INODE_SIZE(inode->i_sb) > EXT4_GOOD_OLD_INODE_SIZE)
		inode_len += ei->i_extra_isize;

	fc_inode.fc_ino = cpu_to_le32(inode->i_ino);
	tl.fc_tag = cpu_to_le16(EXT4_FC_TAG_INODE);
	tl.fc_len = cpu_to_le16(inode_len + sizeof(fc_inode.fc_ino));

	dst = ext4_fc_reserve_space(inode->i_sb,
			sizeof(tl) + inode_len + sizeof(fc_inode.fc_ino), crc);
	if (!dst)
		return -ECANCELED;

	if (!ext4_fc_memcpy(inode->i_sb, dst, &tl, sizeof(tl), crc))
		return -ECANCELED;
	dst += sizeof(tl);
	if (!ext4_fc_memcpy(inode->i_sb, dst, &fc_inode, sizeof(fc_inode), crc))
		return -ECANCELED;
	dst += sizeof(fc_inode);
	if (!ext4_fc_memcpy(inode->i_sb, dst, (u8 *)ext4_raw_inode(&iloc),
					inode_len, crc))
		return -ECANCELED;

	return 0;
}

/*
 * Writes updated data ranges for the inode in question. Updates CRC.
 * Returns 0 on success, error otherwise.
 */
static int ext4_fc_write_inode_data(struct inode *inode, u32 *crc)
{
	ext4_lblk_t old_blk_size, cur_lblk_off, new_blk_size;
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_map_blocks map;
	struct ext4_fc_add_range fc_ext;
	struct ext4_fc_del_range lrange;
	struct ext4_extent *ex;
	int ret;

	mutex_lock(&ei->i_fc_lock);
	if (ei->i_fc_lblk_len == 0) {
		mutex_unlock(&ei->i_fc_lock);
		return 0;
	}
	old_blk_size = ei->i_fc_lblk_start;
	new_blk_size = ei->i_fc_lblk_start + ei->i_fc_lblk_len - 1;
	ei->i_fc_lblk_len = 0;
	mutex_unlock(&ei->i_fc_lock);

	cur_lblk_off = old_blk_size;
	jbd_debug(1, "%s: will try writing %d to %d for inode %ld\n",
		  __func__, cur_lblk_off, new_blk_size, inode->i_ino);

	while (cur_lblk_off <= new_blk_size) {
		map.m_lblk = cur_lblk_off;
		map.m_len = new_blk_size - cur_lblk_off + 1;
		ret = ext4_map_blocks(NULL, inode, &map, 0);
		if (ret < 0)
			return -ECANCELED;

		if (map.m_len == 0) {
			cur_lblk_off++;
			continue;
		}

		if (ret == 0) {
			lrange.fc_ino = cpu_to_le32(inode->i_ino);
			lrange.fc_lblk = cpu_to_le32(map.m_lblk);
			lrange.fc_len = cpu_to_le32(map.m_len);
			if (!ext4_fc_add_tlv(inode->i_sb, EXT4_FC_TAG_DEL_RANGE,
					    sizeof(lrange), (u8 *)&lrange, crc))
				return -ENOSPC;
		} else {
			fc_ext.fc_ino = cpu_to_le32(inode->i_ino);
			ex = (struct ext4_extent *)&fc_ext.fc_ex;
			ex->ee_block = cpu_to_le32(map.m_lblk);
			ex->ee_len = cpu_to_le16(map.m_len);
			ext4_ext_store_pblock(ex, map.m_pblk);
			if (map.m_flags & EXT4_MAP_UNWRITTEN)
				ext4_ext_mark_unwritten(ex);
			else
				ext4_ext_mark_initialized(ex);
			if (!ext4_fc_add_tlv(inode->i_sb, EXT4_FC_TAG_ADD_RANGE,
					    sizeof(fc_ext), (u8 *)&fc_ext, crc))
				return -ENOSPC;
		}

		cur_lblk_off += map.m_len;
	}

	return 0;
}


/* Submit data for all the fast commit inodes */
static int ext4_fc_submit_inode_data_all(journal_t *journal)
{
	struct super_block *sb = (struct super_block *)(journal->j_private);
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_inode_info *ei;
	struct list_head *pos;
	int ret = 0;

	spin_lock(&sbi->s_fc_lock);
	sbi->s_mount_state |= EXT4_FC_COMMITTING;
	list_for_each(pos, &sbi->s_fc_q[FC_Q_MAIN]) {
		ei = list_entry(pos, struct ext4_inode_info, i_fc_list);
		ext4_set_inode_state(&ei->vfs_inode, EXT4_STATE_FC_COMMITTING);
		while (atomic_read(&ei->i_fc_updates)) {
			DEFINE_WAIT(wait);

			prepare_to_wait(&ei->i_fc_wait, &wait,
						TASK_UNINTERRUPTIBLE);
			if (atomic_read(&ei->i_fc_updates)) {
				spin_unlock(&sbi->s_fc_lock);
				schedule();
				spin_lock(&sbi->s_fc_lock);
			}
			finish_wait(&ei->i_fc_wait, &wait);
		}
		spin_unlock(&sbi->s_fc_lock);
		ret = jbd2_submit_inode_data(ei->jinode);
		if (ret)
			return ret;
		spin_lock(&sbi->s_fc_lock);
	}
	spin_unlock(&sbi->s_fc_lock);

	return ret;
}

/* Wait for completion of data for all the fast commit inodes */
static int ext4_fc_wait_inode_data_all(journal_t *journal)
{
	struct super_block *sb = (struct super_block *)(journal->j_private);
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_inode_info *pos, *n;
	int ret = 0;

	spin_lock(&sbi->s_fc_lock);
	list_for_each_entry_safe(pos, n, &sbi->s_fc_q[FC_Q_MAIN], i_fc_list) {
		if (!ext4_test_inode_state(&pos->vfs_inode,
					   EXT4_STATE_FC_COMMITTING))
			continue;
		spin_unlock(&sbi->s_fc_lock);

		ret = jbd2_wait_inode_data(journal, pos->jinode);
		if (ret)
			return ret;
		spin_lock(&sbi->s_fc_lock);
	}
	spin_unlock(&sbi->s_fc_lock);

	return 0;
}

/* Commit all the directory entry updates */
static int ext4_fc_commit_dentry_updates(journal_t *journal, u32 *crc)
{
	struct super_block *sb = (struct super_block *)(journal->j_private);
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_fc_dentry_update *fc_dentry;
	struct inode *inode;
	struct list_head *pos, *n, *fcd_pos, *fcd_n;
	struct ext4_inode_info *ei;
	int ret;

	if (list_empty(&sbi->s_fc_dentry_q[FC_Q_MAIN]))
		return 0;
	list_for_each_safe(fcd_pos, fcd_n, &sbi->s_fc_dentry_q[FC_Q_MAIN]) {
		fc_dentry = list_entry(fcd_pos, struct ext4_fc_dentry_update,
					fcd_list);
		if (fc_dentry->fcd_op != EXT4_FC_TAG_CREAT) {
			spin_unlock(&sbi->s_fc_lock);
			if (!ext4_fc_add_dentry_tlv(
				sb, fc_dentry->fcd_op,
				fc_dentry->fcd_parent, fc_dentry->fcd_ino,
				fc_dentry->fcd_name.len,
				fc_dentry->fcd_name.name, crc)) {
				ret = -ENOSPC;
				goto lock_and_exit;
			}
			spin_lock(&sbi->s_fc_lock);
			continue;
		}

		inode = NULL;
		list_for_each_safe(pos, n, &sbi->s_fc_q[FC_Q_MAIN]) {
			ei = list_entry(pos, struct ext4_inode_info, i_fc_list);
			if (ei->vfs_inode.i_ino == fc_dentry->fcd_ino) {
				inode = &ei->vfs_inode;
				break;
			}
		}
		/*
		 * If we don't find inode in our list, then it was deleted,
		 * in which case, we don't need to record it's create tag.
		 */
		if (!inode)
			continue;
		spin_unlock(&sbi->s_fc_lock);

		/*
		 * We first write the inode and then the create dirent. This
		 * allows the recovery code to create an unnamed inode first
		 * and then link it to a directory entry. This allows us
		 * to use namei.c routines almost as is and simplifies
		 * the recovery code.
		 */
		ret = ext4_fc_write_inode(inode, crc);
		if (ret)
			goto lock_and_exit;

		ret = ext4_fc_write_inode_data(inode, crc);
		if (ret)
			goto lock_and_exit;

		if (!ext4_fc_add_dentry_tlv(
			sb, fc_dentry->fcd_op,
			fc_dentry->fcd_parent, fc_dentry->fcd_ino,
			fc_dentry->fcd_name.len,
			fc_dentry->fcd_name.name, crc)) {
			spin_lock(&sbi->s_fc_lock);
			ret = -ENOSPC;
			goto lock_and_exit;
		}

		spin_lock(&sbi->s_fc_lock);
	}
	return 0;
lock_and_exit:
	spin_lock(&sbi->s_fc_lock);
	return ret;
}

static int ext4_fc_perform_commit(journal_t *journal)
{
	struct super_block *sb = (struct super_block *)(journal->j_private);
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_inode_info *iter;
	struct ext4_fc_head head;
	struct list_head *pos;
	struct inode *inode;
	struct blk_plug plug;
	int ret = 0;
	u32 crc = 0;

	ret = ext4_fc_submit_inode_data_all(journal);
	if (ret)
		return ret;

	ret = ext4_fc_wait_inode_data_all(journal);
	if (ret)
		return ret;

	blk_start_plug(&plug);
	if (sbi->s_fc_bytes == 0) {
		/*
		 * Add a head tag only if this is the first fast commit
		 * in this TID.
		 */
		head.fc_features = cpu_to_le32(EXT4_FC_SUPPORTED_FEATURES);
		head.fc_tid = cpu_to_le32(
			sbi->s_journal->j_running_transaction->t_tid);
		if (!ext4_fc_add_tlv(sb, EXT4_FC_TAG_HEAD, sizeof(head),
			(u8 *)&head, &crc))
			goto out;
	}

	spin_lock(&sbi->s_fc_lock);
	ret = ext4_fc_commit_dentry_updates(journal, &crc);
	if (ret) {
		spin_unlock(&sbi->s_fc_lock);
		goto out;
	}

	list_for_each(pos, &sbi->s_fc_q[FC_Q_MAIN]) {
		iter = list_entry(pos, struct ext4_inode_info, i_fc_list);
		inode = &iter->vfs_inode;
		if (!ext4_test_inode_state(inode, EXT4_STATE_FC_COMMITTING))
			continue;

		spin_unlock(&sbi->s_fc_lock);
		ret = ext4_fc_write_inode_data(inode, &crc);
		if (ret)
			goto out;
		ret = ext4_fc_write_inode(inode, &crc);
		if (ret)
			goto out;
		spin_lock(&sbi->s_fc_lock);
		EXT4_I(inode)->i_fc_committed_subtid =
			atomic_read(&sbi->s_fc_subtid);
	}
	spin_unlock(&sbi->s_fc_lock);

	ret = ext4_fc_write_tail(sb, crc);

out:
	blk_finish_plug(&plug);
	return ret;
}

/*
 * The main commit entry point. Performs a fast commit for transaction
 * commit_tid if needed. If it's not possible to perform a fast commit
 * due to various reasons, we fall back to full commit. Returns 0
 * on success, error otherwise.
 */
int ext4_fc_commit(journal_t *journal, tid_t commit_tid)
{
	struct super_block *sb = (struct super_block *)(journal->j_private);
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int nblks = 0, ret, bsize = journal->j_blocksize;
	int subtid = atomic_read(&sbi->s_fc_subtid);
	int reason = EXT4_FC_REASON_OK, fc_bufs_before = 0;
	ktime_t start_time, commit_time;

	trace_ext4_fc_commit_start(sb);

	start_time = ktime_get();

	if (!test_opt2(sb, JOURNAL_FAST_COMMIT) ||
		(ext4_fc_is_ineligible(sb))) {
		reason = EXT4_FC_REASON_INELIGIBLE;
		goto out;
	}

restart_fc:
	ret = jbd2_fc_begin_commit(journal, commit_tid);
	if (ret == -EALREADY) {
		/* There was an ongoing commit, check if we need to restart */
		if (atomic_read(&sbi->s_fc_subtid) <= subtid &&
			commit_tid > journal->j_commit_sequence)
			goto restart_fc;
		reason = EXT4_FC_REASON_ALREADY_COMMITTED;
		goto out;
	} else if (ret) {
		sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_COMMIT_FAILED]++;
		reason = EXT4_FC_REASON_FC_START_FAILED;
		goto out;
	}

	fc_bufs_before = (sbi->s_fc_bytes + bsize - 1) / bsize;
	ret = ext4_fc_perform_commit(journal);
	if (ret < 0) {
		sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_COMMIT_FAILED]++;
		reason = EXT4_FC_REASON_FC_FAILED;
		goto out;
	}
	nblks = (sbi->s_fc_bytes + bsize - 1) / bsize - fc_bufs_before;
	ret = jbd2_fc_wait_bufs(journal, nblks);
	if (ret < 0) {
		sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_COMMIT_FAILED]++;
		reason = EXT4_FC_REASON_FC_FAILED;
		goto out;
	}
	atomic_inc(&sbi->s_fc_subtid);
	jbd2_fc_end_commit(journal);
out:
	/* Has any ineligible update happened since we started? */
	if (reason == EXT4_FC_REASON_OK && ext4_fc_is_ineligible(sb)) {
		sbi->s_fc_stats.fc_ineligible_reason_count[EXT4_FC_COMMIT_FAILED]++;
		reason = EXT4_FC_REASON_INELIGIBLE;
	}

	spin_lock(&sbi->s_fc_lock);
	if (reason != EXT4_FC_REASON_OK &&
		reason != EXT4_FC_REASON_ALREADY_COMMITTED) {
		sbi->s_fc_stats.fc_ineligible_commits++;
	} else {
		sbi->s_fc_stats.fc_num_commits++;
		sbi->s_fc_stats.fc_numblks += nblks;
	}
	spin_unlock(&sbi->s_fc_lock);
	nblks = (reason == EXT4_FC_REASON_OK) ? nblks : 0;
	trace_ext4_fc_commit_stop(sb, nblks, reason);
	commit_time = ktime_to_ns(ktime_sub(ktime_get(), start_time));
	/*
	 * weight the commit time higher than the average time so we don't
	 * react too strongly to vast changes in the commit time
	 */
	if (likely(sbi->s_fc_avg_commit_time))
		sbi->s_fc_avg_commit_time = (commit_time +
				sbi->s_fc_avg_commit_time * 3) / 4;
	else
		sbi->s_fc_avg_commit_time = commit_time;
	jbd_debug(1,
		"Fast commit ended with blks = %d, reason = %d, subtid - %d",
		nblks, reason, subtid);
	if (reason == EXT4_FC_REASON_FC_FAILED)
		return jbd2_fc_end_commit_fallback(journal, commit_tid);
	if (reason == EXT4_FC_REASON_FC_START_FAILED ||
		reason == EXT4_FC_REASON_INELIGIBLE)
		return jbd2_complete_transaction(journal, commit_tid);
	return 0;
}

/*
 * Fast commit cleanup routine. This is called after every fast commit and
 * full commit. full is true if we are called after a full commit.
 */
static void ext4_fc_cleanup(journal_t *journal, int full)
{
	struct super_block *sb = journal->j_private;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_inode_info *iter;
	struct ext4_fc_dentry_update *fc_dentry;
	struct list_head *pos, *n;

	if (full && sbi->s_fc_bh)
		sbi->s_fc_bh = NULL;

	jbd2_fc_release_bufs(journal);

	spin_lock(&sbi->s_fc_lock);
	list_for_each_safe(pos, n, &sbi->s_fc_q[FC_Q_MAIN]) {
		iter = list_entry(pos, struct ext4_inode_info, i_fc_list);
		list_del_init(&iter->i_fc_list);
		ext4_clear_inode_state(&iter->vfs_inode,
				       EXT4_STATE_FC_COMMITTING);
		ext4_fc_reset_inode(&iter->vfs_inode);
		/* Make sure EXT4_STATE_FC_COMMITTING bit is clear */
		smp_mb();
#if (BITS_PER_LONG < 64)
		wake_up_bit(&iter->i_state_flags, EXT4_STATE_FC_COMMITTING);
#else
		wake_up_bit(&iter->i_flags, EXT4_STATE_FC_COMMITTING);
#endif
	}

	while (!list_empty(&sbi->s_fc_dentry_q[FC_Q_MAIN])) {
		fc_dentry = list_first_entry(&sbi->s_fc_dentry_q[FC_Q_MAIN],
					     struct ext4_fc_dentry_update,
					     fcd_list);
		list_del_init(&fc_dentry->fcd_list);
		spin_unlock(&sbi->s_fc_lock);

		if (fc_dentry->fcd_name.name &&
			fc_dentry->fcd_name.len > DNAME_INLINE_LEN)
			kfree(fc_dentry->fcd_name.name);
		kmem_cache_free(ext4_fc_dentry_cachep, fc_dentry);
		spin_lock(&sbi->s_fc_lock);
	}

	list_splice_init(&sbi->s_fc_dentry_q[FC_Q_STAGING],
				&sbi->s_fc_dentry_q[FC_Q_MAIN]);
	list_splice_init(&sbi->s_fc_q[FC_Q_STAGING],
				&sbi->s_fc_q[FC_Q_STAGING]);

	sbi->s_mount_state &= ~EXT4_FC_COMMITTING;
	sbi->s_mount_state &= ~EXT4_FC_INELIGIBLE;

	if (full)
		sbi->s_fc_bytes = 0;
	spin_unlock(&sbi->s_fc_lock);
	trace_ext4_fc_stats(sb);
}

void ext4_fc_init(struct super_block *sb, journal_t *journal)
{
	if (!test_opt2(sb, JOURNAL_FAST_COMMIT))
		return;
	journal->j_fc_cleanup_callback = ext4_fc_cleanup;
	if (jbd2_fc_init(journal, EXT4_NUM_FC_BLKS)) {
		pr_warn("Error while enabling fast commits, turning off.");
		ext4_clear_feature_fast_commit(sb);
	}
}

int __init ext4_fc_init_dentry_cache(void)
{
	ext4_fc_dentry_cachep = KMEM_CACHE(ext4_fc_dentry_update,
					   SLAB_RECLAIM_ACCOUNT);

	if (ext4_fc_dentry_cachep == NULL)
		return -ENOMEM;

	return 0;
}
