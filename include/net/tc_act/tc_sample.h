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

#endif /* __NET_TC_SAMPLE_H */
