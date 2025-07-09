/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_CONNMARK_H
#define __NET_TC_CONNMARK_H

#include <net/act_api.h>

struct tcf_connmark_parms {
	struct net *net;
	u16 zone;
	int action;
	struct rcu_head rcu;
};

struct tcf_connmark_info {
	struct tc_action common;
	struct tcf_connmark_parms __rcu *parms;
};

#define to_connmark(a) ((struct tcf_connmark_info *)a)

#endif /* __NET_TC_CONNMARK_H */
