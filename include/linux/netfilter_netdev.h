/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETFILTER_NETDEV_H_
#define _NETFILTER_NETDEV_H_

#include <linux/netfilter.h>
#include <linux/netdevice.h>

#ifdef CONFIG_NETFILTER
static __always_inline bool nf_hook_netdev_active(enum nf_dev_hooks hooknum,
					  struct nf_hook_entries __rcu *hooks)
{
#ifdef CONFIG_JUMP_LABEL
	if (!static_key_false(&nf_hooks_needed[NFPROTO_NETDEV][hooknum]))
		return false;
#endif
	return rcu_access_pointer(hooks);
}

/* caller must hold rcu_read_lock */
static __always_inline int nf_hook_netdev(struct sk_buff *skb,
					  enum nf_dev_hooks hooknum,
					  struct nf_hook_entries __rcu *hooks)
{
	struct nf_hook_entries *e = rcu_dereference(hooks);
	struct nf_hook_state state;
	int ret;

	/* Must recheck the hook head, in the event it became NULL
	 * after the check in nf_hook_netdev_active evaluated to true.
	 */
	if (unlikely(!e))
		return 0;

	nf_hook_state_init(&state, hooknum,
			   NFPROTO_NETDEV, skb->dev, NULL, NULL,
			   dev_net(skb->dev), NULL);
	ret = nf_hook_slow(skb, &state, e, 0);
	if (ret == 0)
		return -1;

	return ret;
}
#endif /* CONFIG_NETFILTER */

static inline void nf_hook_netdev_init(struct net_device *dev)
{
#ifdef CONFIG_NETFILTER_INGRESS
	RCU_INIT_POINTER(dev->nf_hooks_ingress, NULL);
#endif
#ifdef CONFIG_NETFILTER_EGRESS
	RCU_INIT_POINTER(dev->nf_hooks_egress, NULL);
#endif
}

#ifdef CONFIG_NETFILTER_INGRESS
static inline bool nf_hook_ingress_active(const struct sk_buff *skb)
{
	return nf_hook_netdev_active(NF_NETDEV_INGRESS,
				     skb->dev->nf_hooks_ingress);
}

static inline int nf_hook_ingress(struct sk_buff *skb)
{
	return nf_hook_netdev(skb, NF_NETDEV_INGRESS,
			      skb->dev->nf_hooks_ingress);
}
#else /* CONFIG_NETFILTER_INGRESS */
static inline int nf_hook_ingress_active(struct sk_buff *skb)
{
	return 0;
}

static inline int nf_hook_ingress(struct sk_buff *skb)
{
	return 0;
}
#endif /* CONFIG_NETFILTER_INGRESS */

#ifdef CONFIG_NETFILTER_EGRESS
static inline bool nf_hook_egress_active(const struct sk_buff *skb)
{
	return nf_hook_netdev_active(NF_NETDEV_EGRESS,
				     skb->dev->nf_hooks_egress);
}

static inline int nf_hook_egress(struct sk_buff *skb)
{
	return nf_hook_netdev(skb, NF_NETDEV_EGRESS,
			      skb->dev->nf_hooks_egress);
}
#else /* CONFIG_NETFILTER_EGRESS */
static inline int nf_hook_egress_active(struct sk_buff *skb)
{
	return 0;
}

static inline int nf_hook_egress(struct sk_buff *skb)
{
	return 0;
}
#endif /* CONFIG_NETFILTER_EGRESS */
#endif /* _NETFILTER_INGRESS_H_ */
