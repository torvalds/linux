/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RATELIMIT_H
#define _LINUX_RATELIMIT_H

#include <linux/ratelimit_types.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

static inline void ratelimit_state_init(struct ratelimit_state *rs,
					int interval, int burst)
{
	memset(rs, 0, sizeof(*rs));

	raw_spin_lock_init(&rs->lock);
	rs->interval	= interval;
	rs->burst	= burst;
}

static inline void ratelimit_default_init(struct ratelimit_state *rs)
{
	return ratelimit_state_init(rs, DEFAULT_RATELIMIT_INTERVAL,
					DEFAULT_RATELIMIT_BURST);
}

static inline void ratelimit_state_inc_miss(struct ratelimit_state *rs)
{
	atomic_inc(&rs->missed);
}

static inline int ratelimit_state_get_miss(struct ratelimit_state *rs)
{
	return atomic_read(&rs->missed);
}

static inline int ratelimit_state_reset_miss(struct ratelimit_state *rs)
{
	return atomic_xchg_relaxed(&rs->missed, 0);
}

static inline void ratelimit_state_reset_interval(struct ratelimit_state *rs, int interval_init)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&rs->lock, flags);
	rs->interval = interval_init;
	rs->flags &= ~RATELIMIT_INITIALIZED;
	rs->printed = 0;
	ratelimit_state_reset_miss(rs);
	raw_spin_unlock_irqrestore(&rs->lock, flags);
}

static inline void ratelimit_state_exit(struct ratelimit_state *rs)
{
	int m;

	if (!(rs->flags & RATELIMIT_MSG_ON_RELEASE))
		return;

	m = ratelimit_state_reset_miss(rs);
	if (m)
		pr_warn("%s: %d output lines suppressed due to ratelimiting\n", current->comm, m);
}

static inline void
ratelimit_set_flags(struct ratelimit_state *rs, unsigned long flags)
{
	rs->flags = flags;
}

extern struct ratelimit_state printk_ratelimit_state;

#ifdef CONFIG_PRINTK

#define WARN_ON_RATELIMIT(condition, state)	({		\
	bool __rtn_cond = !!(condition);			\
	WARN_ON(__rtn_cond && __ratelimit(state));		\
	__rtn_cond;						\
})

#define WARN_RATELIMIT(condition, format, ...)			\
({								\
	static DEFINE_RATELIMIT_STATE(_rs,			\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);	\
	int rtn = !!(condition);				\
								\
	if (unlikely(rtn && __ratelimit(&_rs)))			\
		WARN(rtn, format, ##__VA_ARGS__);		\
								\
	rtn;							\
})

#else

#define WARN_ON_RATELIMIT(condition, state)			\
	WARN_ON(condition)

#define WARN_RATELIMIT(condition, format, ...)			\
({								\
	int rtn = WARN(condition, format, ##__VA_ARGS__);	\
	rtn;							\
})

#endif

#endif /* _LINUX_RATELIMIT_H */
