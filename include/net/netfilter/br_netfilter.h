/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BR_NETFILTER_H_
#define _BR_NETFILTER_H_

#include "../../../net/bridge/br_private.h"

static inline struct nf_bridge_info *nf_bridge_alloc(struct sk_buff *skb)
{
	skb->nf_bridge = kzalloc(sizeof(struct nf_bridge_info), GFP_ATOMIC);

	if (likely(skb->nf_bridge))
		refcount_set(&(skb->nf_bridge->use), 1);

	return skb->nf_bridge;
}

void nf_bridge_update_protocol(struct sk_buff *skb);

int br_nf_hook_thresh(unsigned int hook, struct net *net, struct sock *sk,
		      struct sk_buff *skb, struct net_device *indev,
		      struct net_device *outdev,
		      int (*okfn)(struct net *, struct sock *,
				  struct sk_buff *));

static inline struct nf_bridge_info *
nf_bridge_info_get(const struct sk_buff *skb)
{
	return skb->nf_bridge;
}

unsigned int nf_bridge_encap_header_len(const struct sk_buff *skb);

static inline void nf_bridge_push_encap_header(struct sk_buff *skb)
{
	unsigned int len = nf_bridge_encap_header_len(skb);

	skb_push(skb, len);
	skb->network_header -= len;
}

int br_nf_pre_routing_finish_bridge(struct net *net, struct sock *sk, struct sk_buff *skb);

static inline struct rtable *bridge_parent_rtable(const struct net_device *dev)
{
	struct net_bridge_port *port;

	port = br_port_get_rcu(dev);
	return port ? &port->br->fake_rtable : NULL;
}

struct net_device *setup_pre_routing(struct sk_buff *skb);
void br_netfilter_enable(void);

#if IS_ENABLED(CONFIG_IPV6)
int br_validate_ipv6(struct net *net, struct sk_buff *skb);
unsigned int br_nf_pre_routing_ipv6(void *priv,
				    struct sk_buff *skb,
				    const struct nf_hook_state *state);
#else
static inline int br_validate_ipv6(struct net *net, struct sk_buff *skb)
{
	return -1;
}

static inline unsigned int
br_nf_pre_routing_ipv6(const struct nf_hook_ops *ops, struct sk_buff *skb,
		       const struct nf_hook_state *state)
{
	return NF_ACCEPT;
}
#endif

#endif /* _BR_NETFILTER_H_ */
