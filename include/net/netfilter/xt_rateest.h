#ifndef _XT_RATEEST_H
#define _XT_RATEEST_H

struct xt_rateest {
	struct hlist_node		list;
	char				name[IFNAMSIZ];
	unsigned int			refcnt;
	spinlock_t			lock;
	struct gnet_estimator		params;
	struct gnet_stats_rate_est	rstats;
	struct gnet_stats_basic_packed	bstats;
};

extern struct xt_rateest *xt_rateest_lookup(const char *name);
extern void xt_rateest_put(struct xt_rateest *est);

#endif /* _XT_RATEEST_H */
