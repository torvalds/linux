// SPDX-License-Identifier: GPL-2.0

#include <linux/ratelimit.h>

__rust_helper void rust_helper_ratelimit_state_init(struct ratelimit_state *rs,
						    int interval, int burst)
{
	ratelimit_state_init(rs, interval, burst);
}

__rust_helper void rust_helper_ratelimit_state_exit(struct ratelimit_state *rs)
{
	ratelimit_state_exit(rs);
}
