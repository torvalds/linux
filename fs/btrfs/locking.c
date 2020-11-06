// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <asm/bug.h>
#include "misc.h"
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

/*
 * Extent buffer locking
 * =====================
 *
 * We use a rw_semaphore for tree locking, and the semantics are exactly the
 * same:
 *
 * - reader/writer exclusion
 * - writer/writer exclusion
 * - reader/reader sharing
 * - try-lock semantics for readers and writers
 *
 * The rwsem implementation does opportunistic spinning which reduces number of
 * times the locking task needs to sleep.
 */

/*
 * __btrfs_tree_read_lock - lock extent buffer for read
 * @eb:		the eb to be locked
 * @nest:	the nesting level to be used for lockdep
 * @recurse:	unused
 *
 * This takes the read lock on the extent buffer, using the specified nesting
 * level for lockdep purposes.
 */
void __btrfs_tree_read_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest,
			    bool recurse)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_read_lock_enabled())
		start_ns = ktime_get_ns();

	down_read_nested(&eb->lock, nest);
	eb->lock_owner = current->pid;
	trace_btrfs_tree_read_lock(eb, start_ns);
}

void btrfs_tree_read_lock(struct extent_buffer *eb)
{
	__btrfs_tree_read_lock(eb, BTRFS_NESTING_NORMAL, false);
}

/*
 * Try-lock for read.
 *
 * Retrun 1 if the rwlock has been taken, 0 otherwise
 */
int btrfs_try_tree_read_lock(struct extent_buffer *eb)
{
	if (down_read_trylock(&eb->lock)) {
		eb->lock_owner = current->pid;
		trace_btrfs_try_tree_read_lock(eb);
		return 1;
	}
	return 0;
}

/*
 * Try-lock for write.
 *
 * Retrun 1 if the rwlock has been taken, 0 otherwise
 */
int btrfs_try_tree_write_lock(struct extent_buffer *eb)
{
	if (down_write_trylock(&eb->lock)) {
		eb->lock_owner = current->pid;
		trace_btrfs_try_tree_write_lock(eb);
		return 1;
	}
	return 0;
}

/*
 * Release read lock.
 */
void btrfs_tree_read_unlock(struct extent_buffer *eb)
{
	trace_btrfs_tree_read_unlock(eb);
	eb->lock_owner = 0;
	up_read(&eb->lock);
}

/*
 * __btrfs_tree_lock - lock eb for write
 * @eb:		the eb to lock
 * @nest:	the nesting to use for the lock
 *
 * Returns with the eb->lock write locked.
 */
void __btrfs_tree_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest)
	__acquires(&eb->lock)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_lock_enabled())
		start_ns = ktime_get_ns();

	down_write_nested(&eb->lock, nest);
	eb->lock_owner = current->pid;
	trace_btrfs_tree_lock(eb, start_ns);
}

void btrfs_tree_lock(struct extent_buffer *eb)
{
	__btrfs_tree_lock(eb, BTRFS_NESTING_NORMAL);
}

/*
 * Release the write lock.
 */
void btrfs_tree_unlock(struct extent_buffer *eb)
{
	trace_btrfs_tree_unlock(eb);
	eb->lock_owner = 0;
	up_write(&eb->lock);
}

/*
 * This releases any locks held in the path starting at level and going all the
 * way up to the root.
 *
 * btrfs_search_slot will keep the lock held on higher nodes in a few corner
 * cases, such as COW of the block at slot zero in the node.  This ignores
 * those rules, and it should only be called when there are no more updates to
 * be done higher up in the tree.
 */
void btrfs_unlock_up_safe(struct btrfs_path *path, int level)
{
	int i;

	if (path->keep_locks)
		return;

	for (i = level; i < BTRFS_MAX_LEVEL; i++) {
		if (!path->nodes[i])
			continue;
		if (!path->locks[i])
			continue;
		btrfs_tree_unlock_rw(path->nodes[i], path->locks[i]);
		path->locks[i] = 0;
	}
}

/*
 * Loop around taking references on and locking the root node of the tree until
 * we end up with a lock on the root node.
 *
 * Return: root extent buffer with write lock held
 */
struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		btrfs_tree_lock(eb);
		if (eb == root->node)
			break;
		btrfs_tree_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/*
 * Loop around taking references on and locking the root node of the tree until
 * we end up with a lock on the root node.
 *
 * Return: root extent buffer with read lock held
 */
struct extent_buffer *__btrfs_read_lock_root_node(struct btrfs_root *root,
						  bool recurse)
{
	struct extent_buffer *eb;

	while (1) {
		eb = btrfs_root_node(root);
		__btrfs_tree_read_lock(eb, BTRFS_NESTING_NORMAL, recurse);
		if (eb == root->node)
			break;
		btrfs_tree_read_unlock(eb);
		free_extent_buffer(eb);
	}
	return eb;
}

/*
 * DREW locks
 * ==========
 *
 * DREW stands for double-reader-writer-exclusion lock. It's used in situation
 * where you want to provide A-B exclusion but not AA or BB.
 *
 * Currently implementation gives more priority to reader. If a reader and a
 * writer both race to acquire their respective sides of the lock the writer
 * would yield its lock as soon as it detects a concurrent reader. Additionally
 * if there are pending readers no new writers would be allowed to come in and
 * acquire the lock.
 */

int btrfs_drew_lock_init(struct btrfs_drew_lock *lock)
{
	int ret;

	ret = percpu_counter_init(&lock->writers, 0, GFP_KERNEL);
	if (ret)
		return ret;

	atomic_set(&lock->readers, 0);
	init_waitqueue_head(&lock->pending_readers);
	init_waitqueue_head(&lock->pending_writers);

	return 0;
}

void btrfs_drew_lock_destroy(struct btrfs_drew_lock *lock)
{
	percpu_counter_destroy(&lock->writers);
}

/* Return true if acquisition is successful, false otherwise */
bool btrfs_drew_try_write_lock(struct btrfs_drew_lock *lock)
{
	if (atomic_read(&lock->readers))
		return false;

	percpu_counter_inc(&lock->writers);

	/* Ensure writers count is updated before we check for pending readers */
	smp_mb();
	if (atomic_read(&lock->readers)) {
		btrfs_drew_write_unlock(lock);
		return false;
	}

	return true;
}

void btrfs_drew_write_lock(struct btrfs_drew_lock *lock)
{
	while (true) {
		if (btrfs_drew_try_write_lock(lock))
			return;
		wait_event(lock->pending_writers, !atomic_read(&lock->readers));
	}
}

void btrfs_drew_write_unlock(struct btrfs_drew_lock *lock)
{
	percpu_counter_dec(&lock->writers);
	cond_wake_up(&lock->pending_readers);
}

void btrfs_drew_read_lock(struct btrfs_drew_lock *lock)
{
	atomic_inc(&lock->readers);

	/*
	 * Ensure the pending reader count is perceieved BEFORE this reader
	 * goes to sleep in case of active writers. This guarantees new writers
	 * won't be allowed and that the current reader will be woken up when
	 * the last active writer finishes its jobs.
	 */
	smp_mb__after_atomic();

	wait_event(lock->pending_readers,
		   percpu_counter_sum(&lock->writers) == 0);
}

void btrfs_drew_read_unlock(struct btrfs_drew_lock *lock)
{
	/*
	 * atomic_dec_and_test implies a full barrier, so woken up writers
	 * are guaranteed to see the decrement
	 */
	if (atomic_dec_and_test(&lock->readers))
		wake_up(&lock->pending_writers);
}
