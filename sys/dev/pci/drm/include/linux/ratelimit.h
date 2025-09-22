/* Public domain. */

#ifndef _LINUX_RATELIMIT_H
#define _LINUX_RATELIMIT_H

struct ratelimit_state {
};

#define DEFINE_RATELIMIT_STATE(name, interval, burst) \
	struct ratelimit_state name

#define RATELIMIT_MSG_ON_RELEASE	(1 << 0)

static inline int
__ratelimit(struct ratelimit_state *rs)
{
	return 1;
}

static inline void
ratelimit_state_init(struct ratelimit_state *rs, int interval, int burst)
{
}

static inline void
ratelimit_set_flags(struct ratelimit_state *rs, unsigned long flags)
{
}

#endif
