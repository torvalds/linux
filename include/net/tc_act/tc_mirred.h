/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_MIR_H
#define __NET_TC_MIR_H

#include <net/act_api.h>
#include <linux/tc_act/tc_mirred.h>

struct tcf_mirred {
	struct tc_action	common;
	int			tcfm_eaction;
	bool			tcfm_mac_header_xmit;
	struct net_device __rcu	*tcfm_dev;
	struct list_head	tcfm_list;
};
#define to_mirred(a) ((struct tcf_mirred *)a)

static inline bool is_tcf_mirred_egress_redirect(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_MIRRED)
		return to_mirred(a)->tcfm_eaction == TCA_EGRESS_REDIR;
#endif
	return false;
}

static inline bool is_tcf_mirred_egress_mirror(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_MIRRED)
		return to_mirred(a)->tcfm_eaction == TCA_EGRESS_MIRROR;
#endif
	return false;
}

static inline bool is_tcf_mirred_ingress_redirect(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_MIRRED)
		return to_mirred(a)->tcfm_eaction == TCA_INGRESS_REDIR;
#endif
	return false;
}

static inline bool is_tcf_mirred_ingress_mirror(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_MIRRED)
		return to_mirred(a)->tcfm_eaction == TCA_INGRESS_MIRROR;
#endif
	return false;
}

static inline struct net_device *tcf_mirred_dev(const struct tc_action *a)
{
	return rtnl_dereference(to_mirred(a)->tcfm_dev);
}

#endif /* __NET_TC_MIR_H */
