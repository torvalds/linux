/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_CTINFO_H
#define __NET_TC_CTINFO_H

#include <net/act_api.h>

struct tcf_ctinfo_params {
	struct rcu_head rcu;
	struct net *net;
	u32 dscpmask;
	u32 dscpstatemask;
	u32 cpmarkmask;
	u16 zone;
	u8 mode;
	u8 dscpmaskshift;
};

struct tcf_ctinfo {
	struct tc_action common;
	struct tcf_ctinfo_params __rcu *params;
	atomic64_t stats_dscp_set;
	atomic64_t stats_dscp_error;
	atomic64_t stats_cpmark_set;
};

enum {
	CTINFO_MODE_DSCP	= BIT(0),
	CTINFO_MODE_CPMARK	= BIT(1)
};

#define to_ctinfo(a) ((struct tcf_ctinfo *)a)

#endif /* __NET_TC_CTINFO_H */
