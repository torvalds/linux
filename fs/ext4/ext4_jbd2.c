// SPDX-License-Identifier: GPL-2.0
/*
 * Interface between ext4 and JBD
 */

#include "ext4_jbd2.h"

#include <trace/events/ext4.h>

int ext4_inode_journal_mode(struct inode *inode)
{
	if (EXT4_JOURNAL(inode) == NULL)
		return EXT4_INODE_WRITEBACK_DATA_MODE;	/* writeback */
	/* We do not support data journalling with delayed allocation */
	if (!S_ISREG(inode->i_mode) ||
	    ext4_test_inode_flag(inode, EXT4_INODE_EA_INODE) ||
	    test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA ||
	    (ext4_test_inode_flag(inode, EXT4_INODE_JOURNAL_DATA) &&
	    !test_opt(inode->i_sb, DELALLOC))) {
		/* We do not support data journalling for encrypted data */
		if (S_ISREG(inode->i_mode) && IS_ENCRYPTED(inode))
			return EXT4_INODE_ORDERED_DATA_MODE;  /* ordered */
		return EXT4_INODE_JOURNAL_DATA_MODE;	/* journal data */
	}
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA)
		return EXT4_INODE_ORDERED_DATA_MODE;	/* ordered */
	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_WRITEBACK_DATA)
		return EXT4_INODE_WRITEBACK_DATA_MODE;	/* writeback */
	BUG();
}

/* Just increment the non-pointer handle value */
static handle_t *ext4_get_nojournal(void)
{
	handle_t *handle = current->journal_info;
	unsigned long ref_cnt = (unsigned long)handle;

	BUG_ON(ref_cnt >= EXT4_NOJOURNAL_MAX_REF_COUNT);

	ref_cnt++;
	handle = (handle_t *)ref_cnt;

	current->journal_info = handle;
	return handle;
}


/* Decrement the non-pointer handle value */
static void ext4_put_nojournal(handle_t *handle)
{
	unsigned long ref_cnt = (unsigned long)handle;

	BUG_ON(ref_cnt == 0);

	ref_cnt--;
	handle = (handle_t *)ref_cnt;

	current->journal_info = handle;
}

/*
 * Wrappers for jbd2_journal_start/end.
 */
static int ext4_journal_check_start(struct super_block *sb)
{
	journal_t *journal;

	might_sleep();

	if (unlikely(ext4_forced_shutdown(EXT4_SB(sb))))
		return -EIO;

	if (sb_rdonly(sb))
		return -EROFS;
	WARN_ON(sb->s_writers.frozen == SB_FREEZE_COMPLETE);
	journal = EXT4_SB(sb)->s_journal;
	/*
	 * Special case here: if the journal has aborted behind our
	 * backs (eg. EIO in the commit thread), then we still need to
	 * take the FS itself readonly cleanly.
	 */
	if (journal && is_journal_aborted(journal)) {
		ext4_abort(sb, -journal->j_errno, "Detected aborted journal");
		return -EROFS;
	}
	return 0;
}

handle_t *__ext4_journal_start_sb(struct super_block *sb, unsigned int line,
				  int type, int blocks, int rsv_blocks,
				  int revoke_creds)
{
	journal_t *journal;
	int err;

	trace_ext4_journal_start(sb, blocks, rsv_blocks, revoke_creds,
				 _RET_IP_);
	err = ext4_journal_check_start(sb);
	if (err < 0)
		return ERR_PTR(err);

	journal = EXT4_SB(sb)->s_journal;
	if (!journal)
		return ext4_get_nojournal();
	return jbd2__journal_start(journal, blocks, rsv_blocks, revoke_creds,
				   GFP_NOFS, type, line);
}

int __ext4_journal_stop(const char *where, unsigned int line, handle_t *handle)
{
	struct super_block *sb;
	int err;
	int rc;

	if (!ext4_handle_valid(handle)) {
		ext4_put_nojournal(handle);
		return 0;
	}

	err = handle->h_err;
	if (!handle->h_transaction) {
		rc = jbd2_journal_stop(handle);
		return err ? err : rc;
	}

	sb = handle->h_transaction->t_journal->j_private;
	rc = jbd2_journal_stop(handle);

	if (!err)
		err = rc;
	if (err)
		__ext4_std_error(sb, where, line, err);
	return err;
}

handle_t *__ext4_journal_start_reserved(handle_t *handle, unsigned int line,
					int type)
{
	struct super_block *sb;
	int err;

	if (!ext4_handle_valid(handle))
		return ext4_get_nojournal();

	sb = handle->h_journal->j_private;
	trace_ext4_journal_start_reserved(sb,
				jbd2_handle_buffer_credits(handle), _RET_IP_);
	err = ext4_journal_check_start(sb);
	if (err < 0) {
		jbd2_journal_free_reserved(handle);
		return ERR_PTR(err);
	}

	err = jbd2_journal_start_reserved(handle, type, line);
	if (err < 0)
		return ERR_PTR(err);
	return handle;
}

int __ext4_journal_ensure_credits(handle_t *handle, int check_cred,
				  int extend_cred, int revoke_cred)
{
	if (!ext4_handle_valid(handle))
		return 0;
	if (jbd2_handle_buffer_credits(handle) >= check_cred &&
	    handle->h_revoke_credits >= revoke_cred)
		return 0;
	extend_cred = max(0, extend_cred - jbd2_handle_buffer_credits(handle));
	revoke_cred = max(0, revoke_cred - handle->h_revoke_credits);
	return ext4_journal_extend(handle, extend_cred, revoke_cred);
}

static void ext4_journal_abort_handle(const char *caller, unsigned int line,
				      const char *err_fn,
				      struct buffer_head *bh,
				      handle_t *handle, int err)
{
	char nbuf[16];
	const char *errstr = ext4_decode_error(NULL, err, nbuf);

	BUG_ON(!ext4_handle_valid(handle));

	if (bh)
		BUFFER_TRACE(bh, "abort");

	if (!handle->h_err)
		handle->h_err = err;

	if (is_handle_aborted(handle))
		return;

	printk(KERN_ERR "EXT4-fs: %s:%d: aborting transaction: %s in %s\n",
	       caller, line, errstr, err_fn);

	jbd2_journal_abort_handle(handle);
}

static void ext4_check_bdev_write_error(struct super_block *sb)
{
	struct address_space *mapping = sb->s_bdev->bd_inode->i_mapping;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int err;

	/*
	 * If the block device has write error flag, it may have failed to
	 * async write out metadata buffers in the background. In this case,
	 * we could read old data from disk and write it out again, which
	 * may lead to on-disk filesystem inconsistency.
	 */
	if (errseq_check(&mapping->wb_err, READ_ONCE(sbi->s_bdev_wb_err))) {
		spin_lock(&sbi->s_bdev_wb_lock);
		err = errseq_check_and_advance(&mapping->wb_err, &sbi->s_bdev_wb_err);
		spin_unlock(&sbi->s_bdev_wb_lock);
		if (err)
			ext4_error_err(sb, -err,
				       "Error while async write back metadata");
	}
}

int __ext4_journal_get_write_access(const char *where, unsigned int line,
				    handle_t *handle, struct buffer_head *bh)
{
	int err = 0;

	might_sleep();

	if (bh->b_bdev->bd_super)
		ext4_check_bdev_write_error(bh->b_bdev->bd_super);

	if (ext4_handle_valid(handle)) {
		err = jbd2_journal_get_write_access(handle, bh);
		if (err)
			ext4_journal_abort_handle(where, line, __func__, bh,
						  handle, err);
	}
	return err;
}

/*
 * The ext4 forget function must perform a revoke if we are freeing data
 * which has been journaled.  Metadata (eg. indirect blocks) must be
 * revoked in all cases.
 *
 * "bh" may be NULL: a metadata block may have been freed from memory
 * but there may still be a record of it in the journal, and that record
 * still needs to be revoked.
 *
 * If the handle isn't valid we're not journaling, but we still need to
 * call into ext4_journal_revoke() to put the buffer head.
 */
int __ext4_forget(const char *where, unsigned int line, handle_t *handle,
		  int is_metadata, struct inode *inode,
		  struct buffer_head *bh, ext4_fsblk_t blocknr)
{
	int err;

	might_sleep();

	trace_ext4_forget(inode, is_metadata, blocknr);
	BUFFER_TRACE(bh, "enter");

	jbd_debug(4, "forgetting bh %p: is_metadata = %d, mode %o, "
		  "data mode %x\n",
		  bh, is_metadata, inode->i_mode,
		  test_opt(inode->i_sb, DATA_FLAGS));

	/* In the no journal case, we can just do a bforget and return */
	if (!ext4_handle_valid(handle)) {
		bforget(bh);
		return 0;
	}

	/* Never use the revoke function if we are doing full data
	 * journaling: there is no need to, and a V1 superblock won't
	 * support it.  Otherwise, only skip the revoke on un-journaled
	 * data blocks. */

	if (test_opt(inode->i_sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA ||
	    (!is_metadata && !ext4_should_journal_data(inode))) {
		if (bh) {
			BUFFER_TRACE(bh, "call jbd2_journal_forget");
			err = jbd2_journal_forget(handle, bh);
			if (err)
				ext4_journal_abort_handle(where, line, __func__,
							  bh, handle, err);
			return err;
		}
		return 0;
	}

	/*
	 * data!=journal && (is_metadata || should_journal_data(inode))
	 */
	BUFFER_TRACE(bh, "call jbd2_journal_revoke");
	err = jbd2_journal_revoke(handle, blocknr, bh);
	if (err) {
		ext4_journal_abort_handle(where, line, __func__,
					  bh, handle, err);
		__ext4_abort(inode->i_sb, where, line, -err,
			   "error %d when attempting revoke", err);
	}
	BUFFER_TRACE(bh, "exit");
	return err;
}

int __ext4_journal_get_create_access(const char *where, unsigned int line,
				handle_t *handle, struct buffer_head *bh)
{
	int err = 0;

	if (ext4_handle_valid(handle)) {
		err = jbd2_journal_get_create_access(handle, bh);
		if (err)
			ext4_journal_abort_handle(where, line, __func__,
						  bh, handle, err);
	}
	return err;
}

int __ext4_handle_dirty_metadata(const char *where, unsigned int line,
				 handle_t *handle, struct inode *inode,
				 struct buffer_head *bh)
{
	int err = 0;

	might_sleep();

	set_buffer_meta(bh);
	set_buffer_prio(bh);
	if (ext4_handle_valid(handle)) {
		err = jbd2_journal_dirty_metadata(handle, bh);
		/* Errors can only happen due to aborted journal or a nasty bug */
		if (!is_handle_aborted(handle) && WARN_ON_ONCE(err)) {
			ext4_journal_abort_handle(where, line, __func__, bh,
						  handle, err);
			if (inode == NULL) {
				pr_err("EXT4: jbd2_journal_dirty_metadata "
				       "failed: handle type %u started at "
				       "line %u, credits %u/%u, errcode %d",
				       handle->h_type,
				       handle->h_line_no,
				       handle->h_requested_credits,
				       jbd2_handle_buffer_credits(handle), err);
				return err;
			}
			ext4_error_inode(inode, where, line,
					 bh->b_blocknr,
					 "journal_dirty_metadata failed: "
					 "handle type %u started at line %u, "
					 "credits %u/%u, errcode %d",
					 handle->h_type,
					 handle->h_line_no,
					 handle->h_requested_credits,
					 jbd2_handle_buffer_credits(handle),
					 err);
		}
	} else {
		set_buffer_uptodate(bh);
		if (inode)
			mark_buffer_dirty_inode(bh, inode);
		else
			mark_buffer_dirty(bh);
		if (inode && inode_needs_sync(inode)) {
			sync_dirty_buffer(bh);
			if (buffer_req(bh) && !buffer_uptodate(bh)) {
				ext4_error_inode_err(inode, where, line,
						     bh->b_blocknr, EIO,
					"IO error syncing itable block");
				err = -EIO;
			}
		}
	}
	return err;
}

int __ext4_handle_dirty_super(const char *where, unsigned int line,
			      handle_t *handle, struct super_block *sb)
{
	struct buffer_head *bh = EXT4_SB(sb)->s_sbh;
	int err = 0;

	ext4_superblock_csum_set(sb);
	if (ext4_handle_valid(handle)) {
		err = jbd2_journal_dirty_metadata(handle, bh);
		if (err)
			ext4_journal_abort_handle(where, line, __func__,
						  bh, handle, err);
	} else
		mark_buffer_dirty(bh);
	return err;
}
