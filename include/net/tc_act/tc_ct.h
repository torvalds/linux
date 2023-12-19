/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_CT_H
#define __NET_TC_CT_H

#include <net/act_api.h>
#include <uapi/linux/tc_act/tc_ct.h>

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_conntrack_labels.h>

struct tcf_ct_params {
	struct nf_conntrack_helper *helper;
	struct nf_conn *tmpl;
	u16 zone;

	u32 mark;
	u32 mark_mask;

	u32 labels[NF_CT_LABELS_MAX_SIZE / sizeof(u32)];
	u32 labels_mask[NF_CT_LABELS_MAX_SIZE / sizeof(u32)];

	struct nf_nat_range2 range;
	bool ipv4_range;
	bool put_labels;

	u16 ct_action;

	struct rcu_head rcu;

	struct tcf_ct_flow_table *ct_ft;
	struct nf_flowtable *nf_ft;
};

struct tcf_ct {
	struct tc_action common;
	struct tcf_ct_params __rcu *params;
};

#define to_ct(a) ((struct tcf_ct *)a)
#define to_ct_params(a)							\
	((struct tcf_ct_params *)					\
	 rcu_dereference_protected(to_ct(a)->params,			\
				   lockdep_is_held(&a->tcfa_lock)))

static inline uint16_t tcf_ct_zone(const struct tc_action *a)
{
	return to_ct_params(a)->zone;
}

static inline int tcf_ct_action(const struct tc_action *a)
{
	return to_ct_params(a)->ct_action;
}

static inline struct nf_flowtable *tcf_ct_ft(const struct tc_action *a)
{
	return to_ct_params(a)->nf_ft;
}

static inline struct nf_conntrack_helper *tcf_ct_helper(const struct tc_action *a)
{
	return to_ct_params(a)->helper;
}

#else
static inline uint16_t tcf_ct_zone(const struct tc_action *a) { return 0; }
static inline int tcf_ct_action(const struct tc_action *a) { return 0; }
static inline struct nf_flowtable *tcf_ct_ft(const struct tc_action *a)
{
	return NULL;
}
static inline struct nf_conntrack_helper *tcf_ct_helper(const struct tc_action *a)
{
	return NULL;
}
#endif /* CONFIG_NF_CONNTRACK */

#if IS_ENABLED(CONFIG_NET_ACT_CT)
static inline void
tcf_ct_flow_table_restore_skb(struct sk_buff *skb, unsigned long cookie)
{
	enum ip_conntrack_info ctinfo = cookie & NFCT_INFOMASK;
	struct nf_conn *ct;

	ct = (struct nf_conn *)(cookie & NFCT_PTRMASK);
	nf_conntrack_get(&ct->ct_general);
	nf_ct_set(skb, ct, ctinfo);
}
#else
static inline void
tcf_ct_flow_table_restore_skb(struct sk_buff *skb, unsigned long cookie) { }
#endif

static inline bool is_tcf_ct(const struct tc_action *a)
{
#if defined(CONFIG_NET_CLS_ACT) && IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (a->ops && a->ops->id == TCA_ID_CT)
		return true;
#endif
	return false;
}

#endif /* __NET_TC_CT_H */
