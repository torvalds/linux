/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <asm/bug.h>
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

void btrfs_assert_tree_read_locked(struct extent_buffer *eb);

/*
 * if we currently have a spinning reader or writer lock
 * (indicated by the rw flag) this will bump the count
 * of blocking holders and drop the spinlock.
 */
void btrfs_set_lock_blocking_rw(struct extent_buffer *eb, int rw)
{
	if (rw == BTRFS_WRITE_LOCK) {
		if (atomic_read(&eb->blocking_writers) == 0) {
			WARN_ON(atomic_read(&eb->spinning_writers) != 1);
			atomic_dec(&eb->spinning_writers);
			btrfs_assert_tree_locked(eb);
			atomic_inc(&eb->blocking_writers);
			write_unlock(&eb->lock);
		}
	} else if (rw == BTRFS_READ_LOCK) {
		btrfs_assert_tree_read_locked(eb);
		atomic_inc(&eb->blocking_readers);
		WARN_ON(atomic_read(&eb->spinning_readers) == 0);
		atomic_dec(&eb->spinning_readers);
		read_unlock(&eb->lock);
	}
	return;
}

/*
 * if we currently have a blocking lock, take the spinlock
 * and drop our blocking count
 */
void btrfs_clear_lock_blocking_rw(struct extent_buffer *eb, int rw)
{
	if (rw == BTRFS_WRITE_LOCK_BLOCKING) {
		BUG_ON(atomic_read(&eb->blocking_writers) != 1);
		write_lock(&eb->lock);
		WARN_ON(atomic_read(&eb->spinning_writers));
		atomic_inc(&eb->spinning_writers);
		if (atomic_dec_and_test(&eb->blocking_writers))
			wake_up(&eb->write_lock_wq);
	} else if (rw == BTRFS_READ_LOCK_BLOCKING) {
		BUG_ON(atomic_read(&eb->blocking_readers) == 0);
		read_lock(&eb->lock);
		atomic_inc(&eb->spinning_readers);
		if (atomic_dec_and_test(&eb->blocking_readers))
			wake_up(&eb->read_lock_wq);
	}
	return;
}

/*
 * take a spinning read lock.  This will wait for any blocking
 * writers
 */
void btrfs_tree_read_lock(struct extent_buffer *eb)
{
again:
	wait_event(eb->write_lock_wq, atomic_read(&eb->blocking_writers) == 0);
	read_lock(&eb->lock);
	if (atomic_read(&eb->blocking_writers)) {
		read_unlock(&eb->lock);
		wait_event(eb->write_lock_wq,
			   atomic_read(&eb->blocking_writers) == 0);
		goto again;
	}
	atomic_inc(&eb->read_locks);
	atomic_inc(&eb->spinning_readers);
}

/*
 * returns 1 if we get the read lock and 0 if we don't
 * this won't wait for blocking writers
 */
int btrfs_try_tree_read_lock(struct extent_buffer *eb)
{
	if (atomic_read(&eb->blocking_writers))
		return 0;

	read_lock(&eb->lock);
	if (atomic_read(&eb->blocking_writers)) {
		read_unlock(&eb->lock);
		return 0;
	}
	atomic_inc(&eb->read_locks);
	atomic_inc(&eb->spinning_readers);
	return 1;
}

/*
 * returns 1 if we get the read lock and 0 if we don't
 * this won't wait for blocking writers or readers
 */
int btrfs_try_tree_write_lock(struct extent_buffer *eb)
{
	if (atomic_read(&eb->blocking_writers) ||
	    atomic_read(&eb->blocking_readers))
		return 0;
	write_lock(&eb->lock);
	if (atomic_read(&eb->blocking_writers) ||
	    atomic_read(&eb->blocking_readers)) {
		write_unlock(&eb->lock);
		return 0;
	}
	atomic_inc(&eb->write_locks);
	atomic_inc(&eb->spinning_writers);
	return 1;
}

/*
 * drop a spinning read lock
 */
void btrfs_tree_read_unlock(struct extent_buffer *eb)
{
	btrfs_assert_tree_read_locked(eb);
	WARN_ON(atomic_read(&eb->spinning_readers) == 0);
	atomic_dec(&eb->spinning_readers);
	atomic_dec(&eb->read_locks);
	read_unlock(&eb->lock);
}

/*
 * drop a blocking read lock
 */
void btrfs_tree_read_unlock_blocking(struct extent_buffer *eb)
{
	btrfs_assert_tree_read_locked(eb);
	WARN_ON(atomic_read(&eb->blocking_readers) == 0);
	if (atomic_dec_and_test(&eb->blocking_readers))
		wake_up(&eb->read_lock_wq);
	atomic_dec(&eb->read_locks);
}

/*
 * take a spinning write lock.  This will wait for both
 * blocking readers or writers
 */
int btrfs_tree_lock(struct extent_buffer *eb)
{
again:
	wait_event(eb->read_lock_wq, atomic_read(&eb->blocking_readers) == 0);
	wait_event(eb->write_lock_wq, atomic_read(&eb->blocking_writers) == 0);
	write_lock(&eb->lock);
	if (atomic_read(&eb->blocking_readers)) {
		write_unlock(&eb->lock);
		wait_event(eb->read_lock_wq,
			   atomic_read(&eb->blocking_readers) == 0);
		goto again;
	}
	if (atomic_read(&eb->blocking_writers)) {
		write_unlock(&eb->lock);
		wait_event(eb->write_lock_wq,
			   atomic_read(&eb->blocking_writers) == 0);
		goto again;
	}
	WARN_ON(atomic_read(&eb->spinning_writers));
	atomic_inc(&eb->spinning_writers);
	atomic_inc(&eb->write_locks);
	return 0;
}

/*
 * drop a spinning or a blocking write lock.
 */
int btrfs_tree_unlock(struct extent_buffer *eb)
{
	int blockers = atomic_read(&eb->blocking_writers);

	BUG_ON(blockers > 1);

	btrfs_assert_tree_locked(eb);
	atomic_dec(&eb->write_locks);

	if (blockers) {
		WARN_ON(atomic_read(&eb->spinning_writers));
		atomic_dec(&eb->blocking_writers);
		smp_wmb();
		wake_up(&eb->write_lock_wq);
	} else {
		WARN_ON(atomic_read(&eb->spinning_writers) != 1);
		atomic_dec(&eb->spinning_writers);
		write_unlock(&eb->lock);
	}
	return 0;
}

void btrfs_assert_tree_locked(struct extent_buffer *eb)
{
	BUG_ON(!atomic_read(&eb->write_locks));
}

void btrfs_assert_tree_read_locked(struct extent_buffer *eb)
{
	BUG_ON(!atomic_read(&eb->read_locks));
}
