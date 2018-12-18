/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_CSUM_H
#define __NET_TC_CSUM_H

#include <linux/types.h>
#include <net/act_api.h>
#include <linux/tc_act/tc_csum.h>

struct tcf_csum_params {
	u32 update_flags;
	struct rcu_head rcu;
};

struct tcf_csum {
	struct tc_action common;

	struct tcf_csum_params __rcu *params;
};
#define to_tcf_csum(a) ((struct tcf_csum *)a)

static inline bool is_tcf_csum(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_CSUM)
		return true;
#endif
	return false;
}

static inline u32 tcf_csum_update_flags(const struct tc_action *a)
{
	u32 update_flags;

	rcu_read_lock();
	update_flags = rcu_dereference(to_tcf_csum(a)->params)->update_flags;
	rcu_read_unlock();

	return update_flags;
}

#endif /* __NET_TC_CSUM_H */
