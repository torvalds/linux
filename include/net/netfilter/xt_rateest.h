/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XT_RATEEST_H
#define _XT_RATEEST_H

#include <net/gen_stats.h>

struct xt_rateest {
	/* keep lock and bstats on same cache line to speedup xt_rateest_tg() */
	struct gnet_stats_basic_sync	bstats;
	spinlock_t			lock;


	/* following fields not accessed in hot path */
	unsigned int			refcnt;
	struct hlist_node		list;
	char				name[IFNAMSIZ];
	struct gnet_estimator		params;
	struct rcu_head			rcu;

	/* keep this field far away to speedup xt_rateest_mt() */
	struct net_rate_estimator __rcu *rate_est;
};

struct xt_rateest *xt_rateest_lookup(struct net *net, const char *name);
void xt_rateest_put(struct net *net, struct xt_rateest *est);

#endif /* _XT_RATEEST_H */
