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
 * Additionally we need one level nesting recursion, see below. The rwsem
 * implementation does opportunistic spinning which reduces number of times the
 * locking task needs to sleep.
 *
 *
 * Lock recursion
 * --------------
 *
 * A write operation on a tree might indirectly start a look up on the same
 * tree.  This can happen when btrfs_cow_block locks the tree and needs to
 * lookup free extents.
 *
 * btrfs_cow_block
 *   ..
 *   alloc_tree_block_no_bg_flush
 *     btrfs_alloc_tree_block
 *       btrfs_reserve_extent
 *         ..
 *         load_free_space_cache
 *           ..
 *           btrfs_lookup_file_extent
 *             btrfs_search_slot
 *
 */

/*
 * Mark already held read lock as blocking. Can be nested in write lock by the
 * same thread.
 *
 * Use when there are potentially long operations ahead so other thread waiting
 * on the lock will not actively spin but sleep instead.
 *
 * The rwlock is released and blocking reader counter is increased.
 */
void btrfs_set_lock_blocking_read(struct extent_buffer *eb)
{
}

/*
 * Mark already held write lock as blocking.
 *
 * Use when there are potentially long operations ahead so other threads
 * waiting on the lock will not actively spin but sleep instead.
 *
 * The rwlock is released and blocking writers is set.
 */
void btrfs_set_lock_blocking_write(struct extent_buffer *eb)
{
}

/*
 * __btrfs_tree_read_lock - lock extent buffer for read
 * @eb:		the eb to be locked
 * @nest:	the nesting level to be used for lockdep
 * @recurse:	if this lock is able to be recursed
 *
 * This takes the read lock on the extent buffer, using the specified nesting
 * level for lockdep purposes.
 *
 * If you specify recurse = true, then we will allow this to be taken if we
 * currently own the lock already.  This should only be used in specific
 * usecases, and the subsequent unlock will not change the state of the lock.
 */
void __btrfs_tree_read_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest,
			    bool recurse)
{
	u64 start_ns = 0;

	if (trace_btrfs_tree_read_lock_enabled())
		start_ns = ktime_get_ns();

	if (unlikely(recurse)) {
		/* First see if we can grab the lock outright */
		if (down_read_trylock(&eb->lock))
			goto out;

		/*
		 * Ok still doesn't necessarily mean we are already holding the
		 * lock, check the owner.
		 */
		if (eb->lock_owner != current->pid) {
			down_read_nested(&eb->lock, nest);
			goto out;
		}

		/*
		 * Ok we have actually recursed, but we should only be recursing
		 * once, so blow up if we're already recursed, otherwise set
		 * ->lock_recursed and carry on.
		 */
		BUG_ON(eb->lock_recursed);
		eb->lock_recursed = true;
		goto out;
	}
	down_read_nested(&eb->lock, nest);
out:
	eb->lock_owner = current->pid;
	trace_btrfs_tree_read_lock(eb, start_ns);
}

void btrfs_tree_read_lock(struct extent_buffer *eb)
{
	__btrfs_tree_read_lock(eb, BTRFS_NESTING_NORMAL, false);
}

/*
 * Lock extent buffer for read, optimistically expecting that there are no
 * contending blocking writers. If there are, don't wait.
 *
 * Return 1 if the rwlock has been taken, 0 otherwise
 */
int btrfs_tree_read_lock_atomic(struct extent_buffer *eb)
{
	return btrfs_try_tree_read_lock(eb);
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
 * Release read lock.  If the read lock was recursed then the lock stays in the
 * original state that it was before it was recursively locked.
 */
void btrfs_tree_read_unlock(struct extent_buffer *eb)
{
	trace_btrfs_tree_read_unlock(eb);
	/*
	 * if we're nested, we have the write lock.  No new locking
	 * is needed as long as we are the lock owner.
	 * The write unlock will do a barrier for us, and the lock_recursed
	 * field only matters to the lock owner.
	 */
	if (eb->lock_recursed && current->pid == eb->lock_owner) {
		eb->lock_recursed = false;
		return;
	}
	eb->lock_owner = 0;
	up_read(&eb->lock);
}

/*
 * Release read lock, previously set to blocking by a pairing call to
 * btrfs_set_lock_blocking_read(). Can be nested in write lock by the same
 * thread.
 *
 * State of rwlock is unchanged, last reader wakes waiting threads.
 */
void btrfs_tree_read_unlock_blocking(struct extent_buffer *eb)
{
	btrfs_tree_read_unlock(eb);
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
 * Set all locked nodes in the path to blocking locks.  This should be done
 * before scheduling
 */
void btrfs_set_path_blocking(struct btrfs_path *p)
{
	int i;

	for (i = 0; i < BTRFS_MAX_LEVEL; i++) {
		if (!p->nodes[i] || !p->locks[i])
			continue;
		/*
		 * If we currently have a spinning reader or writer lock this
		 * will bump the count of blocking holders and drop the
		 * spinlock.
		 */
		if (p->locks[i] == BTRFS_READ_LOCK) {
			btrfs_set_lock_blocking_read(p->nodes[i]);
			p->locks[i] = BTRFS_READ_LOCK_BLOCKING;
		} else if (p->locks[i] == BTRFS_WRITE_LOCK) {
			btrfs_set_lock_blocking_write(p->nodes[i]);
			p->locks[i] = BTRFS_WRITE_LOCK_BLOCKING;
		}
	}
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
