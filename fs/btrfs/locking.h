/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_LOCKING_H
#define BTRFS_LOCKING_H

#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/percpu_counter.h>
#include "extent_io.h"

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

static_assert(BTRFS_NESTING_MAX <= MAX_LOCKDEP_SUBCLASSES,
	      "too many lock subclasses defined");

struct btrfs_path;

void __btrfs_tree_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest);
void btrfs_tree_lock(struct extent_buffer *eb);
void btrfs_tree_unlock(struct extent_buffer *eb);

void __btrfs_tree_read_lock(struct extent_buffer *eb, enum btrfs_lock_nesting nest);
void btrfs_tree_read_lock(struct extent_buffer *eb);
void btrfs_tree_read_unlock(struct extent_buffer *eb);
int btrfs_try_tree_read_lock(struct extent_buffer *eb);
int btrfs_try_tree_write_lock(struct extent_buffer *eb);
struct extent_buffer *btrfs_lock_root_node(struct btrfs_root *root);
struct extent_buffer *btrfs_read_lock_root_node(struct btrfs_root *root);

#ifdef CONFIG_BTRFS_DEBUG
static inline void btrfs_assert_tree_locked(struct extent_buffer *eb) {
	lockdep_assert_held(&eb->lock);
}
#else
static inline void btrfs_assert_tree_locked(struct extent_buffer *eb) { }
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
	struct percpu_counter writers;
	wait_queue_head_t pending_writers;
	wait_queue_head_t pending_readers;
};

int btrfs_drew_lock_init(struct btrfs_drew_lock *lock);
void btrfs_drew_lock_destroy(struct btrfs_drew_lock *lock);
void btrfs_drew_write_lock(struct btrfs_drew_lock *lock);
bool btrfs_drew_try_write_lock(struct btrfs_drew_lock *lock);
void btrfs_drew_write_unlock(struct btrfs_drew_lock *lock);
void btrfs_drew_read_lock(struct btrfs_drew_lock *lock);
void btrfs_drew_read_unlock(struct btrfs_drew_lock *lock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void btrfs_set_buffer_lockdep_class(u64 objectid, struct extent_buffer *eb, int level);
#else
static inline void btrfs_set_buffer_lockdep_class(u64 objectid,
					struct extent_buffer *eb, int level)
{
}
#endif

#endif
