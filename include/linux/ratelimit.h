#ifndef _LINUX_RATELIMIT_H
#define _LINUX_RATELIMIT_H

#include <linux/param.h>
#include <linux/spinlock.h>

#define DEFAULT_RATELIMIT_INTERVAL	(5 * HZ)
#define DEFAULT_RATELIMIT_BURST		10

struct ratelimit_state {
	spinlock_t	lock;		/* protect the state */

	int		interval;
	int		burst;
	int		printed;
	int		missed;
	unsigned long	begin;
};

#define DEFINE_RATELIMIT_STATE(name, interval_init, burst_init)		\
									\
	struct ratelimit_state name = {					\
		.lock		= __SPIN_LOCK_UNLOCKED(name.lock),	\
		.interval	= interval_init,			\
		.burst		= burst_init,				\
	}

static inline void ratelimit_state_init(struct ratelimit_state *rs,
					int interval, int burst)
{
	spin_lock_init(&rs->lock);
	rs->interval = interval;
	rs->burst = burst;
	rs->printed = 0;
	rs->missed = 0;
	rs->begin = 0;
}

extern int ___ratelimit(struct ratelimit_state *rs, const char *func);
#define __ratelimit(state) ___ratelimit(state, __func__)

#endif /* _LINUX_RATELIMIT_H */
