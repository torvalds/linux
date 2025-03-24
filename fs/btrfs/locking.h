/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_LOCKING_H
#define BTRFS_LOCKING_H

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/lockdep.h>
#include <linux/percpu_counter.h>
#include "extent_io.h"

struct extent_buffer;
struct btrfs_path;
struct btrfs_root;

#define BTRFS_WRITE_LOCK 1
#define BTRFS_READ_LOCK 2

/*
 * We are limited in number of subclasses by MAX_LOCKDEP_SUBCLASSES, which at
 * the time of this patch is 8, which is how many we use.  Keep this in mind if
 * you decide you want to add another subclass.
 */
enum btrfs_lock_nesting {
	BTRFS_NESTING_NORMAL,

	/*
	 * When we COW a block we are holding the lock on the original block,
	 * and since our lockdep maps are rootid+level, this confuses lockdep
	 * when we lock the newly allocated COW'd block.  Handle this by having
	 * a subclass for COW'ed blocks so that lockdep doesn't complain.
	 */
	BTRFS_NESTING_COW,

	/*
	 * Oftentimes we need to lock adjacent nodes on the same level while
	 * still holding the lock on the original node we searched to, such as
	 * for searching forward or for split/balance.
	 *
	 * Because of this we need to indicate to lockdep that this is
	 * acceptable by having a different subclass for each of these
	 * operations.
	 */
	BTRFS_NESTING_LEFT,
	BTRFS_NESTING_RIGHT,

	/*
	 * When splitting we will be holding a lock on the left/right node when
	 * we need to cow that node, thus we need a new set of subclasses for
	 * these two operations.
	 */
	BTRFS_NESTING_LEFT_COW,
	BTRFS_NESTING_RIGHT_COW,

	/*
	 * When splitting we may push nodes to the left or right, but still use
	 * the subsequent nodes in our path, keeping our locks on those adjacent
	 * blocks.  Thus when we go to allocate a new split block we've already
	 * used up all of our available subclasses, so this subclass exists to
	 * handle this case where we need to allocate a new split block.
	 */
	BTRFS_NESTING_SPLIT,

	/*
	 * When promoting a new block to a root we need to have a special
	 * subclass so we don't confuse lockdep, as it will appear that we are
	 * locking a higher level node before a lower level one.  Copying also
	 * has this problem as it appears we're locking the same block again
	 * when we make a snapshot of an existing root.
	 */
	BTRFS_NESTING_NEW_ROOT,

	/*
	 * We are limited to MAX_LOCKDEP_SUBLCLASSES number of subclasses, so
	 * add this in here and add a static_assert to keep us from going over
	 * the limit.  As of this writing we're limited to 8, and we're
	 * definitely using 8, hence this check to keep us from messing up in
	 * the future.
	 */
	BTRFS_NESTING_MAX,
};

enum btrfs_lockdep_trans_states {
	BTRFS_LOCKDEP_TRANS_COMMIT_PREP,
	BTRFS_LOCKDEP_TRANS_UNBLOCKED,
	BTRFS_LOCKDEP_TRANS_SUPER_COMMITTED,
	BTRFS_LOCKDEP_TRANS_COMPLETED,
};

/*
 * Lockdep annotation for wait events.
 *
 * @owner:  The struct where the lockdep map is defined
 * @lock:   The lockdep map corresponding to a wait event
 *
 * This macro is used to annotate a wait event. In this case a thread acquires
 * the lockdep map as writer (exclusive lock) because it has to block until all
 * the threads that hold the lock as readers signal the condition for the wait
 * event and release their locks.
 */
#define btrfs_might_wait_for_event(owner, lock)					\
	do {									\
		rwsem_acquire(&owner->lock##_map, 0, 0, _THIS_IP_);		\
		rwsem_release(&owner->lock##_map, _THIS_IP_);			\
	} while (0)

/*
 * Protection for the resource/condition of a wait event.
 *
 * @owner:  The struct where the lockdep map is defined
 * @lock:   The lockdep map corresponding to a wait event
 *
 * Many threads can modify the condition for the wait event at the same time
 * and signal the threads that block on the wait event. The threads that modify
 * the condition and do the signaling acquire the lock as readers (shared
 * lock).
 */
#define btrfs_lockdep_acquire(owner, lock)					\
	rwsem_acquire_read(&owner->lock##_map, 0, 0, _THIS_IP_)

/*
 * Used after signaling the condition for a wait event to release the lockdep
 * map held by a reader thread.
 */
#define btrfs_lockdep_release(owner, lock)					\
	rwsem_release(&owner->lock##_map, _THIS_IP_)

/*
 * Used to account for the fact that when doing io_uring encoded I/O, we can
 * return to userspace with the inode lock still held.
 */
#define btrfs_lockdep_inode_acquire(owner, lock)				\
	rwsem_acquire_read(&owner->vfs_inode.lock.dep_map, 0, 0, _THIS_IP_)

#define btrfs_lockdep_inode_release(owner, lock)				\
	rwsem_release(&owner->vfs_inode.lock.dep_map, _THIS_IP_)

/*
 * Macros for the transaction states wait events, similar to the generic wait
 * event macros.
 */
#define btrfs_might_wait_for_state(owner, i)					\
	do {									\
		rwsem_acquire(&owner->btrfs_state_change_map[i], 0, 0, _THIS_IP_); \
		rwsem_release(&owner->btrfs_state_change_map[i], _THIS_IP_);	\
	} while (0)

#define btrfs_trans_state_lockdep_acquire(owner, i)				\
	rwsem_acquire_read(&owner->btrfs_state_change_map[i], 0, 0, _THIS_IP_)

#define btrfs_trans_state_lockdep_release(owner, i)				\
	rwsem_release(&owner->btrfs_state_change_map[i], _THIS_IP_)

/* Initialization of the lockdep map */
#define btrfs_lockdep_init_map(owner, lock)					\
	do {									\
		static struct lock_class_key lock##_key;			\
		lockdep_init_map(&owner->lock##_map, #lock, &lock##_key, 0);	\
	} while (0)

/* Initialization of the transaction states lockdep maps. */
#define btrfs_state_lockdep_init_map(owner, lock, state)			\
	do {									\
		static struct lock_class_key lock##_key;			\
		lockdep_init_map(&owner->btrfs_state_change_map[state], #lock,	\
				 &lock##_key, 0);				\
	} while (0)

static_assert(BTRFS_NESTING_MAX <= MAX_LOCKDEP_SUBCLASSES,
	      "too many lock subclasses defined");

void btrfs_tree_lock_nested(struct extent_buffer *eb, enum btrfs_lock_nesting nest);

static inline void btrfs_tree_lock(struct extent_buffer *eb)
{
	btrfs_tree_lock_nested(eb, BTRFS_NESTING_NORMAL);
}

void btrfs_tree_unlock(struct extent_buffer *eb);

void btrfs_tree_read_lock_nested(struct extent_buffer *eb, enum btrfs_lock_nesting nest);

static inline void btrfs_tree_read_lock(struct extent_buffer *eb)
{
	btrfs_tree_read_lock_nested(eb, BTRFS_NESTING_NORMAL);
}

void btrfs_tree_read_unlock(struct extent_buffer *eb);
int btrfs_try_tree_read_lock(struct extent_buffer *eb);
struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root);
struct extent_buffer *btrfs_read_lock_root_node(struct btrfs_root *root);
struct extent_buffer *btrfs_try_read_lock_root_node(struct btrfs_root *root);

#ifdef CONFIG_BTRFS_DEBUG
static inline void btrfs_assert_tree_write_locked(struct extent_buffer *eb)
{
	lockdep_assert_held_write(&eb->lock);
}
static inline void btrfs_assert_tree_read_locked(struct extent_buffer *eb)
{
	lockdep_assert_held_read(&eb->lock);
}
#else
static inline void btrfs_assert_tree_write_locked(struct extent_buffer *eb) { }
static inline void btrfs_assert_tree_read_locked(struct extent_buffer *eb) { }
#endif

void btrfs_unlock_up_safe(struct btrfs_path *path, int level);

static inline void btrfs_tree_unlock_rw(struct extent_buffer *eb, int rw)
{
	if (rw == BTRFS_WRITE_LOCK)
		btrfs_tree_unlock(eb);
	else if (rw == BTRFS_READ_LOCK)
		btrfs_tree_read_unlock(eb);
	else
		BUG();
}

struct btrfs_drew_lock {
	atomic_t readers;
	atomic_t writers;
	wait_queue_head_t pending_writers;
	wait_queue_head_t pending_readers;
};

void btrfs_drew_lock_init(struct btrfs_drew_lock *lock);
void btrfs_drew_write_lock(struct btrfs_drew_lock *lock);
bool btrfs_drew_try_write_lock(struct btrfs_drew_lock *lock);
void btrfs_drew_write_unlock(struct btrfs_drew_lock *lock);
void btrfs_drew_read_lock(struct btrfs_drew_lock *lock);
void btrfs_drew_read_unlock(struct btrfs_drew_lock *lock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void btrfs_set_buffer_lockdep_class(u64 objectid, struct extent_buffer *eb, int level);
void btrfs_maybe_reset_lockdep_class(struct btrfs_root *root, struct extent_buffer *eb);
#else
static inline void btrfs_set_buffer_lockdep_class(u64 objectid,
					struct extent_buffer *eb, int level)
{
}
static inline void btrfs_maybe_reset_lockdep_class(struct btrfs_root *root,
						   struct extent_buffer *eb)
{
}
#endif

#endif
