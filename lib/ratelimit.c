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

	if (!interval)
		return 1;

	/*
	 * If we contend on this state's lock then almost
	 * by definition we are too busy to print a message,
	 * in addition to the one that will be printed by
	 * the entity that is holding the lock already:
	 */
	if (!raw_spin_trylock_irqsave(&rs->lock, flags))
		return 0;

	if (!rs->begin)
		rs->begin = jiffies;

	if (time_is_before_jiffies(rs->begin + interval)) {
		int m = ratelimit_state_reset_miss(rs);

		if (m) {
			if (!(rs->flags & RATELIMIT_MSG_ON_RELEASE)) {
				printk_deferred(KERN_WARNING
						"%s: %d callbacks suppressed\n", func, m);
			}
		}
		rs->begin   = jiffies;
		rs->printed = 0;
	}
	if (burst && burst > rs->printed) {
		rs->printed++;
		ret = 1;
	} else {
		ratelimit_state_inc_miss(rs);
		ret = 0;
	}
	raw_spin_unlock_irqrestore(&rs->lock, flags);

	return ret;
}
EXPORT_SYMBOL(___ratelimit);
