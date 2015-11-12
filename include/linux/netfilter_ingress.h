#ifndef _NETFILTER_INGRESS_H_
#define _NETFILTER_INGRESS_H_

#include <linux/netfilter.h>
#include <linux/netdevice.h>

#ifdef CONFIG_NETFILTER_INGRESS
static inline int nf_hook_ingress_active(struct sk_buff *skb)
{
	return nf_hook_list_active(&skb->dev->nf_hooks_ingress,
				   NFPROTO_NETDEV, NF_NETDEV_INGRESS);
}

static inline int nf_hook_ingress(struct sk_buff *skb)
{
	struct nf_hook_state state;

	nf_hook_state_init(&state, &skb->dev->nf_hooks_ingress,
			   NF_NETDEV_INGRESS, INT_MIN, NFPROTO_NETDEV, NULL,
			   skb->dev, NULL, dev_net(skb->dev), NULL);
	return nf_hook_slow(skb, &state);
}

static inline void nf_hook_ingress_init(struct net_device *dev)
{
	INIT_LIST_HEAD(&dev->nf_hooks_ingress);
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

static inline void nf_hook_ingress_init(struct net_device *dev) {}
#endif /* CONFIG_NETFILTER_INGRESS */
#endif /* _NETFILTER_INGRESS_H_ */
