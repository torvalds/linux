// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <asm/bug.h>
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

#ifdef CONFIG_BTRFS_DEBUG
static void btrfs_assert_spinning_writers_get(struct extent_buffer *eb)
{
	WARN_ON(atomic_read(&eb->spinning_writers));
	atomic_inc(&eb->spinning_writers);
}

static void btrfs_assert_spinning_writers_put(struct extent_buffer *eb)
{
	WARN_ON(atomic_read(&eb->spinning_writers) != 1);
	atomic_dec(&eb->spinning_writers);
}

static void btrfs_assert_no_spinning_writers(struct extent_buffer *eb)
{
	WARN_ON(atomic_read(&eb->spinning_writers));
}

static void btrfs_assert_spinning_readers_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->spinning_readers);
}

static void btrfs_assert_spinning_readers_put(struct extent_buffer *eb)
{
	WARN_ON(atomic_read(&eb->spinning_readers) == 0);
	atomic_dec(&eb->spinning_readers);
}

static void btrfs_assert_tree_read_locks_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->read_locks);
}

static void btrfs_assert_tree_read_locks_put(struct extent_buffer *eb)
{
	atomic_dec(&eb->read_locks);
}

static void btrfs_assert_tree_read_locked(struct extent_buffer *eb)
{
	BUG_ON(!atomic_read(&eb->read_locks));
}

static void btrfs_assert_tree_write_locks_get(struct extent_buffer *eb)
{
	atomic_inc(&eb->write_locks);
}

static void btrfs_assert_tree_write_locks_put(struct extent_buffer *eb)
{
	atomic_dec(&eb->write_locks);
}

void btrfs_assert_tree_locked(struct extent_buffer *eb)
{
	BUG_ON(!atomic_read(&eb->write_locks));
}

#else
static void btrfs_assert_spinning_writers_get(struct extent_buffer *eb) { }
static void btrfs_assert_spinning_writers_put(struct extent_buffer *eb) { }
static void btrfs_assert_no_spinning_writers(struct extent_buffer *eb) { }
static void btrfs_assert_spinning_readers_put(struct extent_buffer *eb) { }
static void btrfs_assert_spinning_readers_get(struct extent_buffer *eb) { }
static void btrfs_assert_tree_read_locked(struct extent_buffer *eb) { }
static void btrfs_assert_tree_read_locks_get(struct extent_buffer *eb) { }
static void btrfs_assert_tree_read_locks_put(struct extent_buffer *eb) { }
void btrfs_assert_tree_locked(struct extent_buffer *eb) { }
static void btrfs_assert_tree_write_locks_get(struct extent_buffer *eb) { }
static void btrfs_assert_tree_write_locks_put(struct extent_buffer *eb) { }
#endif

void btrfs_set_lock_blocking_read(struct extent_buffer *eb)
{
	trace_btrfs_set_lock_blocking_read(eb);
	/*
	 * No lock is required.  The lock owner may change if we have a read
	 * lock, but it won't change to or away from us.  If we have the write
	 * lock, we are the owner and it'll never change.
	 */
	if (eb->lock_nested && current->pid == eb->lock_owner)
		return;
	btrfs_assert_tree_read_locked(eb);
	atomic_inc(&eb->blocking_readers);
	btrfs_assert_spinning_readers_put(eb);
	read_unlock(&eb->lock);
}

void btrfs_set_lock_blocking_write(struct extent_buffer *eb)
{
	trace_btrfs_set_lock_blocking_write(eb);
	/*
	 * No lock is required.  The lock owner may change if we have a read
	 * lock, but it won't change to or away from us.  If we have the write
	 * lock, we are the owner and it'll never change.
	 */
	if (eb->lock_nested && current->pid == eb->lock_owner)
		return;
	if (atomic_read(&eb->blocking_writers) == 0) {
		btrfs_assert_spinning_writers_put(eb);
		btrfs_assert_tree_locked(eb);
		atomic_inc(&eb->blocking_writers);
		write_unlock(&eb->lock);
	}
}

void btrfs_clear_lock_blocking_read(struct extent_buffer *eb)
{
	trace_btrfs_clear_lock_blocking_read(eb);
	/*
	 * No lock is required.  The lock owner may change if we have a read
	 * lock, but it won't change to or away from us.  If we have the write
	 * lock, we are the owner and it'll never change.
	 */
	if (eb->lock_nested && current->pid == eb->lock_owner)
		return;
	BUG_ON(atomic_read(&eb->blocking_readers) == 0);
	read_lock(&eb->lock);
	btrfs_assert_spinning_readers_get(eb);
	/* atomic_dec_and_test implies a barrier */
	if (atomic_dec_and_test(&eb->blocking_readers))
		cond_wake_up_nomb(&eb->read_lock_wq);
}

void btrfs_clear_lock_blocking_write(struct extent_buffer *eb)
{
	trace_btrfs_clear_lock_blocking_write(eb);
	/*
	 * no lock is required.  The lock owner may change if
	 * we have a read lock, but it won't change to or away
	 * from us.  If we have the write lock, we are the owner
	 * and it'll never change.
	 */
	if (eb->lock_nested && current->pid == eb->lock_owner)
		return;
	BUG_ON(atomic_read(&eb->blocking_writers) != 1);
	write_lock(&eb->lock);
	btrfs_assert_spinning_writers_get(eb);
	/* atomic_dec_and_test implies a barrier */
	if (atomic_dec_and_test(&eb->blocking_writers))
		cond_wake_up_nomb(&eb->write_lock_wq);
}

/*
 * take a spinning read lock.  This will wait for any blocking
 * writers
 */
void btrfs_tree_read_lock(struct extent_buffer *eb)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_read_lock_enabled())
		start_ns = ktime_get_ns();
again:
	BUG_ON(!atomic_read(&eb->blocking_writers) &&
	       current->pid == eb->lock_owner);

	read_lock(&eb->lock);
	if (atomic_read(&eb->blocking_writers) &&
	    current->pid == eb->lock_owner) {
		/*
		 * This extent is already write-locked by our thread. We allow
		 * an additional read lock to be added because it's for the same
		 * thread. btrfs_find_all_roots() depends on this as it may be
		 * called on a partly (write-)locked tree.
		 */
		BUG_ON(eb->lock_nested);
		eb->lock_nested = true;
		read_unlock(&eb->lock);
		trace_btrfs_tree_read_lock(eb, start_ns);
		return;
	}
	if (atomic_read(&eb->blocking_writers)) {
		read_unlock(&eb->lock);
		wait_event(eb->write_lock_wq,
			   atomic_read(&eb->blocking_writers) == 0);
		goto again;
	}
	btrfs_assert_tree_read_locks_get(eb);
	btrfs_assert_spinning_readers_get(eb);
	trace_btrfs_tree_read_lock(eb, start_ns);
}

/*
 * take a spinning read lock.
 * returns 1 if we get the read lock and 0 if we don't
 * this won't wait for blocking writers
 */
int btrfs_tree_read_lock_atomic(struct extent_buffer *eb)
{
	if (atomic_read(&eb->blocking_writers))
		return 0;

	read_lock(&eb->lock);
	if (atomic_read(&eb->blocking_writers)) {
		read_unlock(&eb->lock);
		return 0;
	}
	btrfs_assert_tree_read_locks_get(eb);
	btrfs_assert_spinning_readers_get(eb);
	trace_btrfs_tree_read_lock_atomic(eb);
	return 1;
}

/*
 * returns 1 if we get the read lock and 0 if we don't
 * this won't wait for blocking writers
 */
int btrfs_try_tree_read_lock(struct extent_buffer *eb)
{
	if (atomic_read(&eb->blocking_writers))
		return 0;

	if (!read_trylock(&eb->lock))
		return 0;

	if (atomic_read(&eb->blocking_writers)) {
		read_unlock(&eb->lock);
		return 0;
	}
	btrfs_assert_tree_read_locks_get(eb);
	btrfs_assert_spinning_readers_get(eb);
	trace_btrfs_try_tree_read_lock(eb);
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
	btrfs_assert_tree_write_locks_get(eb);
	btrfs_assert_spinning_writers_get(eb);
	eb->lock_owner = current->pid;
	trace_btrfs_try_tree_write_lock(eb);
	return 1;
}

/*
 * drop a spinning read lock
 */
void btrfs_tree_read_unlock(struct extent_buffer *eb)
{
	trace_btrfs_tree_read_unlock(eb);
	/*
	 * if we're nested, we have the write lock.  No new locking
	 * is needed as long as we are the lock owner.
	 * The write unlock will do a barrier for us, and the lock_nested
	 * field only matters to the lock owner.
	 */
	if (eb->lock_nested && current->pid == eb->lock_owner) {
		eb->lock_nested = false;
		return;
	}
	btrfs_assert_tree_read_locked(eb);
	btrfs_assert_spinning_readers_put(eb);
	btrfs_assert_tree_read_locks_put(eb);
	read_unlock(&eb->lock);
}

/*
 * drop a blocking read lock
 */
void btrfs_tree_read_unlock_blocking(struct extent_buffer *eb)
{
	trace_btrfs_tree_read_unlock_blocking(eb);
	/*
	 * if we're nested, we have the write lock.  No new locking
	 * is needed as long as we are the lock owner.
	 * The write unlock will do a barrier for us, and the lock_nested
	 * field only matters to the lock owner.
	 */
	if (eb->lock_nested && current->pid == eb->lock_owner) {
		eb->lock_nested = false;
		return;
	}
	btrfs_assert_tree_read_locked(eb);
	WARN_ON(atomic_read(&eb->blocking_readers) == 0);
	/* atomic_dec_and_test implies a barrier */
	if (atomic_dec_and_test(&eb->blocking_readers))
		cond_wake_up_nomb(&eb->read_lock_wq);
	btrfs_assert_tree_read_locks_put(eb);
}

/*
 * take a spinning write lock.  This will wait for both
 * blocking readers or writers
 */
void btrfs_tree_lock(struct extent_buffer *eb)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_lock_enabled())
		start_ns = ktime_get_ns();

	WARN_ON(eb->lock_owner == current->pid);
again:
	wait_event(eb->read_lock_wq, atomic_read(&eb->blocking_readers) == 0);
	wait_event(eb->write_lock_wq, atomic_read(&eb->blocking_writers) == 0);
	write_lock(&eb->lock);
	if (atomic_read(&eb->blocking_readers) ||
	    atomic_read(&eb->blocking_writers)) {
		write_unlock(&eb->lock);
		goto again;
	}
	btrfs_assert_spinning_writers_get(eb);
	btrfs_assert_tree_write_locks_get(eb);
	eb->lock_owner = current->pid;
	trace_btrfs_tree_lock(eb, start_ns);
}

/*
 * drop a spinning or a blocking write lock.
 */
void btrfs_tree_unlock(struct extent_buffer *eb)
{
	int blockers = atomic_read(&eb->blocking_writers);

	BUG_ON(blockers > 1);

	btrfs_assert_tree_locked(eb);
	trace_btrfs_tree_unlock(eb);
	eb->lock_owner = 0;
	btrfs_assert_tree_write_locks_put(eb);

	if (blockers) {
		btrfs_assert_no_spinning_writers(eb);
		atomic_dec(&eb->blocking_writers);
		/* Use the lighter barrier after atomic */
		smp_mb__after_atomic();
		cond_wake_up_nomb(&eb->write_lock_wq);
	} else {
		btrfs_assert_spinning_writers_put(eb);
		write_unlock(&eb->lock);
	}
}
