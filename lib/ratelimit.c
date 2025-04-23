// SPDX-License-Identifier: GPL-2.0-only
/*
 * ratelimit.c - Do something with rate limit.
 *
 * Isolated from kernel/printk.c by Dave Young <hidave.darkstar@gmail.com>
 *
 * 2008-05-01 rewrite the function and use a ratelimit_state data struct as
 * parameter. Now every user can use their own standalone ratelimit_state.
 */

#include <linux/ratelimit.h>
#include <linux/jiffies.h>
#include <linux/export.h>

/*
 * __ratelimit - rate limiting
 * @rs: ratelimit_state data
 * @func: name of calling function
 *
 * This enforces a rate limit: not more than @rs->burst callbacks
 * in every @rs->interval
 *
 * RETURNS:
 * 0 means callbacks will be suppressed.
 * 1 means go ahead and do it.
 */
int ___ratelimit(struct ratelimit_state *rs, const char *func)
{
	/* Paired with WRITE_ONCE() in .proc_handler().
	 * Changing two values seperately could be inconsistent
	 * and some message could be lost.  (See: net_ratelimit_state).
	 */
	int interval = READ_ONCE(rs->interval);
	int burst = READ_ONCE(rs->burst);
	unsigned long flags;
	int ret;

	/*
	 * Zero interval says never limit, otherwise, non-positive burst
	 * says always limit.
	 */
	if (interval <= 0 || burst <= 0) {
		ret = interval == 0 || burst > 0;
		if (!(READ_ONCE(rs->flags) & RATELIMIT_INITIALIZED) || (!interval && !burst) ||
		    !raw_spin_trylock_irqsave(&rs->lock, flags)) {
			if (!ret)
				ratelimit_state_inc_miss(rs);
			return ret;
		}

		/* Force re-initialization once re-enabled. */
		rs->flags &= ~RATELIMIT_INITIALIZED;
		if (!ret)
			ratelimit_state_inc_miss(rs);
		goto unlock_ret;
	}

	/*
	 * If we contend on this state's lock then just check if
	 * the current burst is used or not. It might cause
	 * false positive when we are past the interval and
	 * the current lock owner is just about to reset it.
	 */
	if (!raw_spin_trylock_irqsave(&rs->lock, flags)) {
		unsigned int rs_flags = READ_ONCE(rs->flags);

		if (rs_flags & RATELIMIT_INITIALIZED && burst) {
			int n_left;

			n_left = atomic_dec_return(&rs->rs_n_left);
			if (n_left >= 0)
				return 1;
		}

		ratelimit_state_inc_miss(rs);
		return 0;
	}

	if (!(rs->flags & RATELIMIT_INITIALIZED)) {
		rs->begin = jiffies;
		rs->flags |= RATELIMIT_INITIALIZED;
		atomic_set(&rs->rs_n_left, rs->burst);
	}

	if (time_is_before_jiffies(rs->begin + interval)) {
		int m;

		/*
		 * Reset rs_n_left ASAP to reduce false positives
		 * in parallel calls, see above.
		 */
		atomic_set(&rs->rs_n_left, rs->burst);
		rs->begin = jiffies;

		m = ratelimit_state_reset_miss(rs);
		if (m) {
			if (!(rs->flags & RATELIMIT_MSG_ON_RELEASE)) {
				printk_deferred(KERN_WARNING
						"%s: %d callbacks suppressed\n", func, m);
			}
		}
	}
	if (burst) {
		int n_left;

		/* The burst might have been taken by a parallel call. */
		n_left = atomic_dec_return(&rs->rs_n_left);
		if (n_left >= 0) {
			ret = 1;
			goto unlock_ret;
		}
	}

	ratelimit_state_inc_miss(rs);
	ret = 0;

unlock_ret:
	raw_spin_unlock_irqrestore(&rs->lock, flags);

	return ret;
}
EXPORT_SYMBOL(___ratelimit);
