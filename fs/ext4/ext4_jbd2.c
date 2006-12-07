/*
 * Interface between ext4 and JBD
 */

#include <linux/ext4_jbd2.h>

int __ext4_journal_get_undo_access(const char *where, handle_t *handle,
				struct buffer_head *bh)
{
	int err = jbd2_journal_get_undo_access(handle, bh);
	if (err)
		ext4_journal_abort_handle(where, __FUNCTION__, bh, handle,err);
	return err;
}

int __ext4_journal_get_write_access(const char *where, handle_t *handle,
				struct buffer_head *bh)
{
	int err = jbd2_journal_get_write_access(handle, bh);
	if (err)
		ext4_journal_abort_handle(where, __FUNCTION__, bh, handle,err);
	return err;
}

int __ext4_journal_forget(const char *where, handle_t *handle,
				struct buffer_head *bh)
{
	int err = jbd2_journal_forget(handle, bh);
	if (err)
		ext4_journal_abort_handle(where, __FUNCTION__, bh, handle,err);
	return err;
}

int __ext4_journal_revoke(const char *where, handle_t *handle,
				ext4_fsblk_t blocknr, struct buffer_head *bh)
{
	int err = jbd2_journal_revoke(handle, blocknr, bh);
	if (err)
		ext4_journal_abort_handle(where, __FUNCTION__, bh, handle,err);
	return err;
}

int __ext4_journal_get_create_access(const char *where,
				handle_t *handle, struct buffer_head *bh)
{
	int err = jbd2_journal_get_create_access(handle, bh);
	if (err)
		ext4_journal_abort_handle(where, __FUNCTION__, bh, handle,err);
	return err;
}

int __ext4_journal_dirty_metadata(const char *where,
				handle_t *handle, struct buffer_head *bh)
{
	int err = jbd2_journal_dirty_metadata(handle, bh);
	if (err)
		ext4_journal_abort_handle(where, __FUNCTION__, bh, handle,err);
	return err;
}
