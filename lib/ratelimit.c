/*
 * ratelimit.c - Do something with rate limit.
 *
 * Isolated from kernel/printk.c by Dave Young <hidave.darkstar@gmail.com>
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/module.h>

/*
 * __ratelimit - rate limiting
 * @ratelimit_jiffies: minimum time in jiffies between two callbacks
 * @ratelimit_burst: number of callbacks we do before ratelimiting
 *
 * This enforces a rate limit: not more than @ratelimit_burst callbacks
 * in every ratelimit_jiffies
 */
int __ratelimit(int ratelimit_jiffies, int ratelimit_burst)
{
	static DEFINE_SPINLOCK(ratelimit_lock);
	static unsigned toks = 10 * 5 * HZ;
	static unsigned long last_msg;
	static int missed;
	unsigned long flags;
	unsigned long now = jiffies;

	spin_lock_irqsave(&ratelimit_lock, flags);
	toks += now - last_msg;
	last_msg = now;
	if (toks > (ratelimit_burst * ratelimit_jiffies))
		toks = ratelimit_burst * ratelimit_jiffies;
	if (toks >= ratelimit_jiffies) {
		int lost = missed;

		missed = 0;
		toks -= ratelimit_jiffies;
		spin_unlock_irqrestore(&ratelimit_lock, flags);
		if (lost)
			printk(KERN_WARNING "%s: %d messages suppressed\n",
				__func__, lost);
		return 1;
	}
	missed++;
	spin_unlock_irqrestore(&ratelimit_lock, flags);
	return 0;
}
EXPORT_SYMBOL(__ratelimit);
