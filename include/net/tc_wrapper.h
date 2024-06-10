/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_WRAPPER_H
#define __NET_TC_WRAPPER_H

#include <net/pkt_cls.h>

#if IS_ENABLED(CONFIG_MITIGATION_RETPOLINE)

#include <linux/cpufeature.h>
#include <linux/static_key.h>
#include <linux/indirect_call_wrapper.h>

#define TC_INDIRECT_SCOPE

extern struct static_key_false tc_skip_wrapper;

/* TC Actions */
#ifdef CONFIG_NET_CLS_ACT

#define TC_INDIRECT_ACTION_DECLARE(fname)                              \
	INDIRECT_CALLABLE_DECLARE(int fname(struct sk_buff *skb,       \
					    const struct tc_action *a, \
					    struct tcf_result *res))

TC_INDIRECT_ACTION_DECLARE(tcf_bpf_act);
TC_INDIRECT_ACTION_DECLARE(tcf_connmark_act);
TC_INDIRECT_ACTION_DECLARE(tcf_csum_act);
TC_INDIRECT_ACTION_DECLARE(tcf_ct_act);
TC_INDIRECT_ACTION_DECLARE(tcf_ctinfo_act);
TC_INDIRECT_ACTION_DECLARE(tcf_gact_act);
TC_INDIRECT_ACTION_DECLARE(tcf_gate_act);
TC_INDIRECT_ACTION_DECLARE(tcf_ife_act);
TC_INDIRECT_ACTION_DECLARE(tcf_ipt_act);
TC_INDIRECT_ACTION_DECLARE(tcf_mirred_act);
TC_INDIRECT_ACTION_DECLARE(tcf_mpls_act);
TC_INDIRECT_ACTION_DECLARE(tcf_nat_act);
TC_INDIRECT_ACTION_DECLARE(tcf_pedit_act);
TC_INDIRECT_ACTION_DECLARE(tcf_police_act);
TC_INDIRECT_ACTION_DECLARE(tcf_sample_act);
TC_INDIRECT_ACTION_DECLARE(tcf_simp_act);
TC_INDIRECT_ACTION_DECLARE(tcf_skbedit_act);
TC_INDIRECT_ACTION_DECLARE(tcf_skbmod_act);
TC_INDIRECT_ACTION_DECLARE(tcf_vlan_act);
TC_INDIRECT_ACTION_DECLARE(tunnel_key_act);

static inline int tc_act(struct sk_buff *skb, const struct tc_action *a,
			   struct tcf_result *res)
{
	if (static_branch_likely(&tc_skip_wrapper))
		goto skip;

#if IS_BUILTIN(CONFIG_NET_ACT_GACT)
	if (a->ops->act == tcf_gact_act)
		return tcf_gact_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_MIRRED)
	if (a->ops->act == tcf_mirred_act)
		return tcf_mirred_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_PEDIT)
	if (a->ops->act == tcf_pedit_act)
		return tcf_pedit_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_SKBEDIT)
	if (a->ops->act == tcf_skbedit_act)
		return tcf_skbedit_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_SKBMOD)
	if (a->ops->act == tcf_skbmod_act)
		return tcf_skbmod_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_POLICE)
	if (a->ops->act == tcf_police_act)
		return tcf_police_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_BPF)
	if (a->ops->act == tcf_bpf_act)
		return tcf_bpf_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_CONNMARK)
	if (a->ops->act == tcf_connmark_act)
		return tcf_connmark_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_CSUM)
	if (a->ops->act == tcf_csum_act)
		return tcf_csum_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_CT)
	if (a->ops->act == tcf_ct_act)
		return tcf_ct_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_CTINFO)
	if (a->ops->act == tcf_ctinfo_act)
		return tcf_ctinfo_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_GATE)
	if (a->ops->act == tcf_gate_act)
		return tcf_gate_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_MPLS)
	if (a->ops->act == tcf_mpls_act)
		return tcf_mpls_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_NAT)
	if (a->ops->act == tcf_nat_act)
		return tcf_nat_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_TUNNEL_KEY)
	if (a->ops->act == tunnel_key_act)
		return tunnel_key_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_VLAN)
	if (a->ops->act == tcf_vlan_act)
		return tcf_vlan_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_IFE)
	if (a->ops->act == tcf_ife_act)
		return tcf_ife_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_SIMP)
	if (a->ops->act == tcf_simp_act)
		return tcf_simp_act(skb, a, res);
#endif
#if IS_BUILTIN(CONFIG_NET_ACT_SAMPLE)
	if (a->ops->act == tcf_sample_act)
		return tcf_sample_act(skb, a, res);
#endif

skip:
	return a->ops->act(skb, a, res);
}

#endif /* CONFIG_NET_CLS_ACT */

/* TC Filters */
#ifdef CONFIG_NET_CLS

#define TC_INDIRECT_FILTER_DECLARE(fname)                               \
	INDIRECT_CALLABLE_DECLARE(int fname(struct sk_buff *skb,        \
					    const struct tcf_proto *tp, \
					    struct tcf_result *res))

TC_INDIRECT_FILTER_DECLARE(basic_classify);
TC_INDIRECT_FILTER_DECLARE(cls_bpf_classify);
TC_INDIRECT_FILTER_DECLARE(cls_cgroup_classify);
TC_INDIRECT_FILTER_DECLARE(fl_classify);
TC_INDIRECT_FILTER_DECLARE(flow_classify);
TC_INDIRECT_FILTER_DECLARE(fw_classify);
TC_INDIRECT_FILTER_DECLARE(mall_classify);
TC_INDIRECT_FILTER_DECLARE(route4_classify);
TC_INDIRECT_FILTER_DECLARE(u32_classify);

static inline int tc_classify(struct sk_buff *skb, const struct tcf_proto *tp,
				struct tcf_result *res)
{
	if (static_branch_likely(&tc_skip_wrapper))
		goto skip;

#if IS_BUILTIN(CONFIG_NET_CLS_BPF)
	if (tp->classify == cls_bpf_classify)
		return cls_bpf_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_U32)
	if (tp->classify == u32_classify)
		return u32_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_FLOWER)
	if (tp->classify == fl_classify)
		return fl_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_FW)
	if (tp->classify == fw_classify)
		return fw_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_MATCHALL)
	if (tp->classify == mall_classify)
		return mall_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_BASIC)
	if (tp->classify == basic_classify)
		return basic_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_CGROUP)
	if (tp->classify == cls_cgroup_classify)
		return cls_cgroup_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_FLOW)
	if (tp->classify == flow_classify)
		return flow_classify(skb, tp, res);
#endif
#if IS_BUILTIN(CONFIG_NET_CLS_ROUTE4)
	if (tp->classify == route4_classify)
		return route4_classify(skb, tp, res);
#endif

skip:
	return tp->classify(skb, tp, res);
}

#endif /* CONFIG_NET_CLS */

static inline void tc_wrapper_init(void)
{
#ifdef CONFIG_X86
	if (!cpu_feature_enabled(X86_FEATURE_RETPOLINE))
		static_branch_enable(&tc_skip_wrapper);
#endif
}

#else

#define TC_INDIRECT_SCOPE static

static inline int tc_act(struct sk_buff *skb, const struct tc_action *a,
			   struct tcf_result *res)
{
	return a->ops->act(skb, a, res);
}

static inline int tc_classify(struct sk_buff *skb, const struct tcf_proto *tp,
				struct tcf_result *res)
{
	return tp->classify(skb, tp, res);
}

static inline void tc_wrapper_init(void)
{
}

#endif

#endif /* __NET_TC_WRAPPER_H */
