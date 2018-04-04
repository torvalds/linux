/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_LOCKING_H
#define BTRFS_LOCKING_H

#define BTRFS_WRITE_LOCK 1
#define BTRFS_READ_LOCK 2
#define BTRFS_WRITE_LOCK_BLOCKING 3
#define BTRFS_READ_LOCK_BLOCKING 4

void btrfs_tree_lock(struct extent_buffer *eb);
void btrfs_tree_unlock(struct extent_buffer *eb);

void btrfs_tree_read_lock(struct extent_buffer *eb);
void btrfs_tree_read_unlock(struct extent_buffer *eb);
void btrfs_tree_read_unlock_blocking(struct extent_buffer *eb);
void btrfs_set_lock_blocking_read(struct extent_buffer *eb);
void btrfs_set_lock_blocking_write(struct extent_buffer *eb);
void btrfs_clear_lock_blocking_read(struct extent_buffer *eb);
void btrfs_clear_lock_blocking_write(struct extent_buffer *eb);
void btrfs_assert_tree_locked(struct extent_buffer *eb);
int btrfs_try_tree_read_lock(struct extent_buffer *eb);
int btrfs_try_tree_write_lock(struct extent_buffer *eb);
int btrfs_tree_read_lock_atomic(struct extent_buffer *eb);


static inline void btrfs_tree_unlock_rw(struct extent_buffer *eb, int rw)
{
	if (rw == BTRFS_WRITE_LOCK || rw == BTRFS_WRITE_LOCK_BLOCKING)
		btrfs_tree_unlock(eb);
	else if (rw == BTRFS_READ_LOCK_BLOCKING)
		btrfs_tree_read_unlock_blocking(eb);
	else if (rw == BTRFS_READ_LOCK)
		btrfs_tree_read_unlock(eb);
	else
		BUG();
}

/*
 * If we currently have a spinning reader or writer lock (indicated by the rw
 * flag) this will bump the count of blocking holders and drop the spinlock.
 */
static inline void btrfs_set_lock_blocking_rw(struct extent_buffer *eb, int rw)
{
	if (rw == BTRFS_WRITE_LOCK)
		btrfs_set_lock_blocking_write(eb);
	else if (rw == BTRFS_READ_LOCK)
		btrfs_set_lock_blocking_read(eb);
}

static inline void btrfs_set_lock_blocking(struct extent_buffer *eb)
{
	btrfs_set_lock_blocking_write(eb);
}

#endif
