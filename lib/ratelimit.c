/*
 * ratelimit.c - Do something with rate limit.
 *
 * Isolated from kernel/printk.c by Dave Young <hidave.darkstar@gmail.com>
 *
 * 2008-05-01 rewrite the function and use a ratelimit_state data struct as
 * parameter. Now every user can use their own standalone ratelimit_state.
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/module.h>

static DEFINE_SPINLOCK(ratelimit_lock);

/*
 * __ratelimit - rate limiting
 * @rs: ratelimit_state data
 *
 * This enforces a rate limit: not more than @rs->ratelimit_burst callbacks
 * in every @rs->ratelimit_jiffies
 */
int __ratelimit(struct ratelimit_state *rs)
{
	unsigned long flags;

	if (!rs->interval)
		return 1;

	spin_lock_irqsave(&ratelimit_lock, flags);
	if (!rs->begin)
		rs->begin = jiffies;

	if (time_is_before_jiffies(rs->begin + rs->interval)) {
		if (rs->missed)
			printk(KERN_WARNING "%s: %d callbacks suppressed\n",
				__func__, rs->missed);
		rs->begin = 0;
		rs->printed = 0;
		rs->missed = 0;
	}
	if (rs->burst && rs->burst > rs->printed)
		goto print;

	rs->missed++;
	spin_unlock_irqrestore(&ratelimit_lock, flags);
	return 0;

print:
	rs->printed++;
	spin_unlock_irqrestore(&ratelimit_lock, flags);
	return 1;
}
EXPORT_SYMBOL(__ratelimit);
