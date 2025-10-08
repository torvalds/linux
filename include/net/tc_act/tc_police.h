/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_POLICE_H
#define __NET_TC_POLICE_H

#include <net/act_api.h>

struct tcf_police_params {
	int			action;
	int			tcfp_result;
	u32			tcfp_ewma_rate;
	u32			tcfp_mtu;
	s64			tcfp_burst;
	s64			tcfp_mtu_ptoks;
	s64			tcfp_pkt_burst;
	struct psched_ratecfg	rate;
	bool			rate_present;
	struct psched_ratecfg	peak;
	bool			peak_present;
	struct psched_pktrate	ppsrate;
	bool			pps_present;
	struct rcu_head rcu;
};

struct tcf_police {
	struct tc_action	common;
	struct tcf_police_params __rcu *params;

	spinlock_t		tcfp_lock ____cacheline_aligned_in_smp;
	s64			tcfp_toks;
	s64			tcfp_ptoks;
	s64			tcfp_pkttoks;
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

static inline u64 tcf_police_rate_bytes_ps(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));
	return params->rate.rate_bytes_ps;
}

static inline u32 tcf_police_burst(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;
	u32 burst;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));

	/*
	 *  "rate" bytes   "burst" nanoseconds
	 *  ------------ * -------------------
	 *    1 second          2^6 ticks
	 *
	 * ------------------------------------
	 *        NSEC_PER_SEC nanoseconds
	 *        ------------------------
	 *              2^6 ticks
	 *
	 *    "rate" bytes   "burst" nanoseconds            2^6 ticks
	 *  = ------------ * ------------------- * ------------------------
	 *      1 second          2^6 ticks        NSEC_PER_SEC nanoseconds
	 *
	 *   "rate" * "burst"
	 * = ---------------- bytes/nanosecond
	 *    NSEC_PER_SEC^2
	 *
	 *
	 *   "rate" * "burst"
	 * = ---------------- bytes/second
	 *     NSEC_PER_SEC
	 */
	burst = div_u64(params->tcfp_burst * params->rate.rate_bytes_ps,
			NSEC_PER_SEC);

	return burst;
}

static inline u64 tcf_police_rate_pkt_ps(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));
	return params->ppsrate.rate_pkts_ps;
}

static inline u32 tcf_police_burst_pkt(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;
	u32 burst;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));

	/*
	 *  "rate" pkts     "burst" nanoseconds
	 *  ------------ *  -------------------
	 *    1 second          2^6 ticks
	 *
	 * ------------------------------------
	 *        NSEC_PER_SEC nanoseconds
	 *        ------------------------
	 *              2^6 ticks
	 *
	 *    "rate" pkts    "burst" nanoseconds            2^6 ticks
	 *  = ------------ * ------------------- * ------------------------
	 *      1 second          2^6 ticks        NSEC_PER_SEC nanoseconds
	 *
	 *   "rate" * "burst"
	 * = ---------------- pkts/nanosecond
	 *    NSEC_PER_SEC^2
	 *
	 *
	 *   "rate" * "burst"
	 * = ---------------- pkts/second
	 *     NSEC_PER_SEC
	 */
	burst = div_u64(params->tcfp_pkt_burst * params->ppsrate.rate_pkts_ps,
			NSEC_PER_SEC);

	return burst;
}

static inline u32 tcf_police_tcfp_mtu(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));
	return params->tcfp_mtu;
}

static inline u64 tcf_police_peakrate_bytes_ps(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));
	return params->peak.rate_bytes_ps;
}

static inline u32 tcf_police_tcfp_ewma_rate(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));
	return params->tcfp_ewma_rate;
}

static inline u16 tcf_police_rate_overhead(const struct tc_action *act)
{
	struct tcf_police *police = to_police(act);
	struct tcf_police_params *params;

	params = rcu_dereference_protected(police->params,
					   lockdep_is_held(&police->tcf_lock));
	return params->rate.overhead;
}

#endif /* __NET_TC_POLICE_H */
