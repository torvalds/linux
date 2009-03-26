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
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/page-flags.h>
#include <asm/bug.h>
#include "ctree.h"
#include "extent_io.h"
#include "locking.h"

static inline void spin_nested(struct extent_buffer *eb)
{
	spin_lock(&eb->lock);
}

/*
 * Setting a lock to blocking will drop the spinlock and set the
 * flag that forces other procs who want the lock to wait.  After
 * this you can safely schedule with the lock held.
 */
void btrfs_set_lock_blocking(struct extent_buffer *eb)
{
	if (!test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags)) {
		set_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags);
		spin_unlock(&eb->lock);
	}
	/* exit with the spin lock released and the bit set */
}

/*
 * clearing the blocking flag will take the spinlock again.
 * After this you can't safely schedule
 */
void btrfs_clear_lock_blocking(struct extent_buffer *eb)
{
	if (test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags)) {
		spin_nested(eb);
		clear_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags);
		smp_mb__after_clear_bit();
	}
	/* exit with the spin lock held */
}

/*
 * unfortunately, many of the places that currently set a lock to blocking
 * don't end up blocking for every long, and often they don't block
 * at all.  For a dbench 50 run, if we don't spin one the blocking bit
 * at all, the context switch rate can jump up to 400,000/sec or more.
 *
 * So, we're still stuck with this crummy spin on the blocking bit,
 * at least until the most common causes of the short blocks
 * can be dealt with.
 */
static int btrfs_spin_on_block(struct extent_buffer *eb)
{
	int i;
	for (i = 0; i < 512; i++) {
		cpu_relax();
		if (!test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
			return 1;
		if (need_resched())
			break;
	}
	return 0;
}

/*
 * This is somewhat different from trylock.  It will take the
 * spinlock but if it finds the lock is set to blocking, it will
 * return without the lock held.
 *
 * returns 1 if it was able to take the lock and zero otherwise
 *
 * After this call, scheduling is not safe without first calling
 * btrfs_set_lock_blocking()
 */
int btrfs_try_spin_lock(struct extent_buffer *eb)
{
	int i;

	spin_nested(eb);
	if (!test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
		return 1;
	spin_unlock(&eb->lock);

	/* spin for a bit on the BLOCKING flag */
	for (i = 0; i < 2; i++) {
		if (!btrfs_spin_on_block(eb))
			break;

		spin_nested(eb);
		if (!test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
			return 1;
		spin_unlock(&eb->lock);
	}
	return 0;
}

/*
 * the autoremove wake function will return 0 if it tried to wake up
 * a process that was already awake, which means that process won't
 * count as an exclusive wakeup.  The waitq code will continue waking
 * procs until it finds one that was actually sleeping.
 *
 * For btrfs, this isn't quite what we want.  We want a single proc
 * to be notified that the lock is ready for taking.  If that proc
 * already happen to be awake, great, it will loop around and try for
 * the lock.
 *
 * So, btrfs_wake_function always returns 1, even when the proc that we
 * tried to wake up was already awake.
 */
static int btrfs_wake_function(wait_queue_t *wait, unsigned mode,
			       int sync, void *key)
{
	autoremove_wake_function(wait, mode, sync, key);
	return 1;
}

/*
 * returns with the extent buffer spinlocked.
 *
 * This will spin and/or wait as required to take the lock, and then
 * return with the spinlock held.
 *
 * After this call, scheduling is not safe without first calling
 * btrfs_set_lock_blocking()
 */
int btrfs_tree_lock(struct extent_buffer *eb)
{
	DEFINE_WAIT(wait);
	wait.func = btrfs_wake_function;

	while(1) {
		spin_nested(eb);

		/* nobody is blocking, exit with the spinlock held */
		if (!test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
			return 0;

		/*
		 * we have the spinlock, but the real owner is blocking.
		 * wait for them
		 */
		spin_unlock(&eb->lock);

		/*
		 * spin for a bit, and if the blocking flag goes away,
		 * loop around
		 */
		if (btrfs_spin_on_block(eb))
			continue;

		prepare_to_wait_exclusive(&eb->lock_wq, &wait,
					  TASK_UNINTERRUPTIBLE);

		if (test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
			schedule();

		finish_wait(&eb->lock_wq, &wait);
	}
	return 0;
}

/*
 * Very quick trylock, this does not spin or schedule.  It returns
 * 1 with the spinlock held if it was able to take the lock, or it
 * returns zero if it was unable to take the lock.
 *
 * After this call, scheduling is not safe without first calling
 * btrfs_set_lock_blocking()
 */
int btrfs_try_tree_lock(struct extent_buffer *eb)
{
	if (spin_trylock(&eb->lock)) {
		if (test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags)) {
			/*
			 * we've got the spinlock, but the real owner is
			 * blocking.  Drop the spinlock and return failure
			 */
			spin_unlock(&eb->lock);
			return 0;
		}
		return 1;
	}
	/* someone else has the spinlock giveup */
	return 0;
}

int btrfs_tree_unlock(struct extent_buffer *eb)
{
	/*
	 * if we were a blocking owner, we don't have the spinlock held
	 * just clear the bit and look for waiters
	 */
	if (test_and_clear_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
		smp_mb__after_clear_bit();
	else
		spin_unlock(&eb->lock);

	if (waitqueue_active(&eb->lock_wq))
		wake_up(&eb->lock_wq);
	return 0;
}

void btrfs_assert_tree_locked(struct extent_buffer *eb)
{
	if (!test_bit(EXTENT_BUFFER_BLOCKING, &eb->bflags))
		assert_spin_locked(&eb->lock);
}
