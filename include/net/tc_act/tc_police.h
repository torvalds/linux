/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_POLICE_H
#define __NET_TC_POLICE_H

#include <net/act_api.h>

struct tcf_police_params {
	int			tcfp_result;
	u32			tcfp_ewma_rate;
	s64			tcfp_burst;
	u32			tcfp_mtu;
	s64			tcfp_mtu_ptoks;
	struct psched_ratecfg	rate;
	bool			rate_present;
	struct psched_ratecfg	peak;
	bool			peak_present;
	struct rcu_head rcu;
};

struct tcf_police {
	struct tc_action	common;
	struct tcf_police_params __rcu *params;

	spinlock_t		tcfp_lock ____cacheline_aligned_in_smp;
	s64			tcfp_toks;
	s64			tcfp_ptoks;
	s64			tcfp_t_c;
};

#define to_police(pc) ((struct tcf_police *)pc)

/* old policer structure from before tc actions */
struct tc_police_compat {
	u32			index;
	int			action;
	u32			limit;
	u32			burst;
	u32			mtu;
	struct tc_ratespec	rate;
	struct tc_ratespec	peakrate;
};

static inline bool is_tcf_police(const struct tc_action *act)
{
#ifdef CONFIG_NET_CLS_ACT
	if (act->ops && act->ops->id == TCA_ID_POLICE)
		return true;
#endif
	return false;
}

static inline u64 tcf_police_rate_bytes_ps(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_bh_rtnl(police->params);
	return params->rate.rate_bytes_ps;
}

static inline s64 tcf_police_tcfp_burst(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_bh_rtnl(police->params);
	return params->tcfp_burst;
}

#endif /* __NET_TC_POLICE_H */
