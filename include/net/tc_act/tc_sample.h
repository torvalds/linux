/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_SAMPLE_H
#define __NET_TC_SAMPLE_H

#include <net/act_api.h>
#include <linux/tc_act/tc_sample.h>
#include <net/psample.h>

struct tcf_sample {
	struct tc_action common;
	u32 rate;
	bool truncate;
	u32 trunc_size;
	struct psample_group __rcu *psample_group;
	u32 psample_group_num;
	struct list_head tcfm_list;
};
#define to_sample(a) ((struct tcf_sample *)a)

static inline bool is_tcf_sample(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	return a->ops && a->ops->type == TCA_ACT_SAMPLE;
#else
	return false;
#endif
}

static inline __u32 tcf_sample_rate(const struct tc_action *a)
{
	return to_sample(a)->rate;
}

static inline bool tcf_sample_truncate(const struct tc_action *a)
{
	return to_sample(a)->truncate;
}

static inline int tcf_sample_trunc_size(const struct tc_action *a)
{
	return to_sample(a)->trunc_size;
}

static inline struct psample_group *
tcf_sample_psample_group(const struct tc_action *a)
{
	return rcu_dereference(to_sample(a)->psample_group);
}

#endif /* __NET_TC_SAMPLE_H */
