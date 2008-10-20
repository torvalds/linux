/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_jbd_compat.h
 *
 * Compatibility defines for JBD.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef OCFS2_JBD_COMPAT_H
#define OCFS2_JBD_COMPAT_H

#ifndef CONFIG_OCFS2_COMPAT_JBD
# error Should not have been included
#endif

struct jbd2_inode {
	unsigned int dummy;
};

#define JBD2_BARRIER			JFS_BARRIER
#define JBD2_DEFAULT_MAX_COMMIT_AGE	JBD_DEFAULT_MAX_COMMIT_AGE

#define jbd2_journal_ack_err			journal_ack_err
#define jbd2_journal_clear_err			journal_clear_err
#define jbd2_journal_destroy			journal_destroy
#define jbd2_journal_dirty_metadata		journal_dirty_metadata
#define jbd2_journal_errno			journal_errno
#define jbd2_journal_extend			journal_extend
#define jbd2_journal_flush			journal_flush
#define jbd2_journal_force_commit		journal_force_commit
#define jbd2_journal_get_write_access		journal_get_write_access
#define jbd2_journal_get_undo_access		journal_get_undo_access
#define jbd2_journal_init_inode			journal_init_inode
#define jbd2_journal_invalidatepage		journal_invalidatepage
#define jbd2_journal_load			journal_load
#define jbd2_journal_lock_updates		journal_lock_updates
#define jbd2_journal_restart			journal_restart
#define jbd2_journal_start			journal_start
#define jbd2_journal_start_commit		journal_start_commit
#define jbd2_journal_stop			journal_stop
#define jbd2_journal_try_to_free_buffers	journal_try_to_free_buffers
#define jbd2_journal_unlock_updates		journal_unlock_updates
#define jbd2_journal_wipe			journal_wipe
#define jbd2_log_wait_commit			log_wait_commit

static inline int jbd2_journal_file_inode(handle_t *handle,
					  struct jbd2_inode *inode)
{
	return 0;
}

static inline int jbd2_journal_begin_ordered_truncate(struct jbd2_inode *inode,
						      loff_t new_size)
{
	return 0;
}

static inline void jbd2_journal_init_jbd_inode(struct jbd2_inode *jinode,
					       struct inode *inode)
{
	return;
}

static inline void jbd2_journal_release_jbd_inode(journal_t *journal,
						  struct jbd2_inode *jinode)
{
	return;
}


#endif  /* OCFS2_JBD_COMPAT_H */
