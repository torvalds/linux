/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_MISC_H
#define BTRFS_MISC_H

#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/div64.h>

#define in_range(b, first, len) ((b) >= (first) && (b) < (first) + (len))

static inline void cond_wake_up(struct wait_queue_head *wq)
{
	/*
	 * This implies a full smp_mb barrier, see comments for
	 * waitqueue_active why.
	 */
	if (wq_has_sleeper(wq))
		wake_up(wq);
}

static inline void cond_wake_up_nomb(struct wait_queue_head *wq)
{
	/*
	 * Special case for conditional wakeup where the barrier required for
	 * waitqueue_active is implied by some of the preceding code. Eg. one
	 * of such atomic operations (atomic_dec_and_return, ...), or a
	 * unlock/lock sequence, etc.
	 */
	if (waitqueue_active(wq))
		wake_up(wq);
}

static inline u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	return div_u64(num, 10);
}

static inline u64 div_factor_fine(u64 num, int factor)
{
	if (factor == 100)
		return num;
	num *= factor;
	return div_u64(num, 100);
}

/* Copy of is_power_of_two that is 64bit safe */
static inline bool is_power_of_two_u64(u64 n)
{
	return n != 0 && (n & (n - 1)) == 0;
}

static inline bool has_single_bit_set(u64 n)
{
	return is_power_of_two_u64(n);
}

#endif
