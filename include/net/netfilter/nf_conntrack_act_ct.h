/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NF_CONNTRACK_ACT_CT_H
#define _NF_CONNTRACK_ACT_CT_H

#include <net/netfilter/nf_conntrack.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <net/netfilter/nf_conntrack_extend.h>

struct nf_conn_act_ct_ext {
	int ifindex[IP_CT_DIR_MAX];
};

static inline struct nf_conn_act_ct_ext *nf_conn_act_ct_ext_find(const struct nf_conn *ct)
{
#if IS_ENABLED(CONFIG_NET_ACT_CT)
	return nf_ct_ext_find(ct, NF_CT_EXT_ACT_CT);
#else
	return NULL;
#endif
}

static inline void nf_conn_act_ct_ext_fill(struct sk_buff *skb, struct nf_conn *ct,
					   enum ip_conntrack_info ctinfo)
{
#if IS_ENABLED(CONFIG_NET_ACT_CT)
	struct nf_conn_act_ct_ext *act_ct_ext;

	act_ct_ext = nf_conn_act_ct_ext_find(ct);
	if (dev_net(skb->dev) == &init_net && act_ct_ext)
		act_ct_ext->ifindex[CTINFO2DIR(ctinfo)] = skb->dev->ifindex;
#endif
}

static inline struct
nf_conn_act_ct_ext *nf_conn_act_ct_ext_add(struct sk_buff *skb,
					   struct nf_conn *ct,
					   enum ip_conntrack_info ctinfo)
{
#if IS_ENABLED(CONFIG_NET_ACT_CT)
	struct nf_conn_act_ct_ext *act_ct = nf_ct_ext_find(ct, NF_CT_EXT_ACT_CT);

	if (act_ct)
		return act_ct;

	act_ct = nf_ct_ext_add(ct, NF_CT_EXT_ACT_CT, GFP_ATOMIC);
	nf_conn_act_ct_ext_fill(skb, ct, ctinfo);
	return act_ct;
#else
	return NULL;
#endif
}

#endif /* _NF_CONNTRACK_ACT_CT_H */
