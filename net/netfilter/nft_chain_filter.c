#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <net/netfilter/nf_tables.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_arp.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

#ifdef CONFIG_NF_TABLES_IPV4
static unsigned int nft_do_chain_ipv4(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_ipv4(&pkt);

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_ipv4 = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_IPV4,
	.hook_mask	= (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
	.hooks		= {
		[NF_INET_LOCAL_IN]	= nft_do_chain_ipv4,
		[NF_INET_LOCAL_OUT]	= nft_do_chain_ipv4,
		[NF_INET_FORWARD]	= nft_do_chain_ipv4,
		[NF_INET_PRE_ROUTING]	= nft_do_chain_ipv4,
		[NF_INET_POST_ROUTING]	= nft_do_chain_ipv4,
	},
};

static void nft_chain_filter_ipv4_init(void)
{
	nft_register_chain_type(&nft_chain_filter_ipv4);
}
static void nft_chain_filter_ipv4_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_ipv4);
}

#else
static inline void nft_chain_filter_ipv4_init(void) {}
static inline void nft_chain_filter_ipv4_fini(void) {}
#endif /* CONFIG_NF_TABLES_IPV4 */

#ifdef CONFIG_NF_TABLES_ARP
static unsigned int nft_do_chain_arp(void *priv, struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_unspec(&pkt);

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_arp = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_ARP,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_ARP_IN) |
			  (1 << NF_ARP_OUT),
	.hooks		= {
		[NF_ARP_IN]		= nft_do_chain_arp,
		[NF_ARP_OUT]		= nft_do_chain_arp,
	},
};

static void nft_chain_filter_arp_init(void)
{
	nft_register_chain_type(&nft_chain_filter_arp);
}

static void nft_chain_filter_arp_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_arp);
}
#else
static inline void nft_chain_filter_arp_init(void) {}
static inline void nft_chain_filter_arp_fini(void) {}
#endif /* CONFIG_NF_TABLES_ARP */

#ifdef CONFIG_NF_TABLES_IPV6
static unsigned int nft_do_chain_ipv6(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);
	nft_set_pktinfo_ipv6(&pkt);

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_ipv6 = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_IPV6,
	.hook_mask	= (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
	.hooks		= {
		[NF_INET_LOCAL_IN]	= nft_do_chain_ipv6,
		[NF_INET_LOCAL_OUT]	= nft_do_chain_ipv6,
		[NF_INET_FORWARD]	= nft_do_chain_ipv6,
		[NF_INET_PRE_ROUTING]	= nft_do_chain_ipv6,
		[NF_INET_POST_ROUTING]	= nft_do_chain_ipv6,
	},
};

static void nft_chain_filter_ipv6_init(void)
{
	nft_register_chain_type(&nft_chain_filter_ipv6);
}

static void nft_chain_filter_ipv6_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_ipv6);
}
#else
static inline void nft_chain_filter_ipv6_init(void) {}
static inline void nft_chain_filter_ipv6_fini(void) {}
#endif /* CONFIG_NF_TABLES_IPV6 */

#ifdef CONFIG_NF_TABLES_INET
static unsigned int nft_do_chain_inet(void *priv, struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (state->pf) {
	case NFPROTO_IPV4:
		nft_set_pktinfo_ipv4(&pkt);
		break;
	case NFPROTO_IPV6:
		nft_set_pktinfo_ipv6(&pkt);
		break;
	default:
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static unsigned int nft_do_chain_inet_ingress(void *priv, struct sk_buff *skb,
					      const struct nf_hook_state *state)
{
	struct nf_hook_state ingress_state = *state;
	struct nft_pktinfo pkt;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		/* Original hook is NFPROTO_NETDEV and NF_NETDEV_INGRESS. */
		ingress_state.pf = NFPROTO_IPV4;
		ingress_state.hook = NF_INET_INGRESS;
		nft_set_pktinfo(&pkt, skb, &ingress_state);

		if (nft_set_pktinfo_ipv4_ingress(&pkt) < 0)
			return NF_DROP;
		break;
	case htons(ETH_P_IPV6):
		ingress_state.pf = NFPROTO_IPV6;
		ingress_state.hook = NF_INET_INGRESS;
		nft_set_pktinfo(&pkt, skb, &ingress_state);

		if (nft_set_pktinfo_ipv6_ingress(&pkt) < 0)
			return NF_DROP;
		break;
	default:
		return NF_ACCEPT;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_inet = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_INET,
	.hook_mask	= (1 << NF_INET_INGRESS) |
			  (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING),
	.hooks		= {
		[NF_INET_INGRESS]	= nft_do_chain_inet_ingress,
		[NF_INET_LOCAL_IN]	= nft_do_chain_inet,
		[NF_INET_LOCAL_OUT]	= nft_do_chain_inet,
		[NF_INET_FORWARD]	= nft_do_chain_inet,
		[NF_INET_PRE_ROUTING]	= nft_do_chain_inet,
		[NF_INET_POST_ROUTING]	= nft_do_chain_inet,
        },
};

static void nft_chain_filter_inet_init(void)
{
	nft_register_chain_type(&nft_chain_filter_inet);
}

static void nft_chain_filter_inet_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_inet);
}
#else
static inline void nft_chain_filter_inet_init(void) {}
static inline void nft_chain_filter_inet_fini(void) {}
#endif /* CONFIG_NF_TABLES_IPV6 */

#if IS_ENABLED(CONFIG_NF_TABLES_BRIDGE)
static unsigned int
nft_do_chain_bridge(void *priv,
		    struct sk_buff *skb,
		    const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (eth_hdr(skb)->h_proto) {
	case htons(ETH_P_IP):
		nft_set_pktinfo_ipv4_validate(&pkt);
		break;
	case htons(ETH_P_IPV6):
		nft_set_pktinfo_ipv6_validate(&pkt);
		break;
	default:
		nft_set_pktinfo_unspec(&pkt);
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_bridge = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_BRIDGE,
	.hook_mask	= (1 << NF_BR_PRE_ROUTING) |
			  (1 << NF_BR_LOCAL_IN) |
			  (1 << NF_BR_FORWARD) |
			  (1 << NF_BR_LOCAL_OUT) |
			  (1 << NF_BR_POST_ROUTING),
	.hooks		= {
		[NF_BR_PRE_ROUTING]	= nft_do_chain_bridge,
		[NF_BR_LOCAL_IN]	= nft_do_chain_bridge,
		[NF_BR_FORWARD]		= nft_do_chain_bridge,
		[NF_BR_LOCAL_OUT]	= nft_do_chain_bridge,
		[NF_BR_POST_ROUTING]	= nft_do_chain_bridge,
	},
};

static void nft_chain_filter_bridge_init(void)
{
	nft_register_chain_type(&nft_chain_filter_bridge);
}

static void nft_chain_filter_bridge_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_bridge);
}
#else
static inline void nft_chain_filter_bridge_init(void) {}
static inline void nft_chain_filter_bridge_fini(void) {}
#endif /* CONFIG_NF_TABLES_BRIDGE */

#ifdef CONFIG_NF_TABLES_NETDEV
static unsigned int nft_do_chain_netdev(void *priv, struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nft_set_pktinfo_ipv4_validate(&pkt);
		break;
	case htons(ETH_P_IPV6):
		nft_set_pktinfo_ipv6_validate(&pkt);
		break;
	default:
		nft_set_pktinfo_unspec(&pkt);
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_netdev = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_NETDEV,
	.hook_mask	= (1 << NF_NETDEV_INGRESS) |
			  (1 << NF_NETDEV_EGRESS),
	.hooks		= {
		[NF_NETDEV_INGRESS]	= nft_do_chain_netdev,
		[NF_NETDEV_EGRESS]	= nft_do_chain_netdev,
	},
};

static void nft_netdev_event(unsigned long event, struct net_device *dev,
			     struct nft_ctx *ctx)
{
	struct nft_base_chain *basechain = nft_base_chain(ctx->chain);
	struct nft_hook *hook, *found = NULL;
	int n = 0;

	if (event != NETDEV_UNREGISTER)
		return;

	list_for_each_entry(hook, &basechain->hook_list, list) {
		if (hook->ops.dev == dev)
			found = hook;

		n++;
	}
	if (!found)
		return;

	if (n > 1) {
		nf_unregister_net_hook(ctx->net, &found->ops);
		list_del_rcu(&found->list);
		kfree_rcu(found, rcu);
		return;
	}

	__nft_release_basechain(ctx);
}

static int nf_tables_netdev_event(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct nftables_pernet *nft_net;
	struct nft_table *table;
	struct nft_chain *chain, *nr;
	struct nft_ctx ctx = {
		.net	= dev_net(dev),
	};

	if (event != NETDEV_UNREGISTER &&
	    event != NETDEV_CHANGENAME)
		return NOTIFY_DONE;

	if (!check_net(ctx.net))
		return NOTIFY_DONE;

	nft_net = nft_pernet(ctx.net);
	mutex_lock(&nft_net->commit_mutex);
	list_for_each_entry(table, &nft_net->tables, list) {
		if (table->family != NFPROTO_NETDEV)
			continue;

		ctx.family = table->family;
		ctx.table = table;
		list_for_each_entry_safe(chain, nr, &table->chains, list) {
			if (!nft_is_base_chain(chain))
				continue;

			ctx.chain = chain;
			nft_netdev_event(event, dev, &ctx);
		}
	}
	mutex_unlock(&nft_net->commit_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block nf_tables_netdev_notifier = {
	.notifier_call	= nf_tables_netdev_event,
};

static int nft_chain_filter_netdev_init(void)
{
	int err;

	nft_register_chain_type(&nft_chain_filter_netdev);

	err = register_netdevice_notifier(&nf_tables_netdev_notifier);
	if (err)
		goto err_register_netdevice_notifier;

	return 0;

err_register_netdevice_notifier:
	nft_unregister_chain_type(&nft_chain_filter_netdev);

	return err;
}

static void nft_chain_filter_netdev_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_netdev);
	unregister_netdevice_notifier(&nf_tables_netdev_notifier);
}
#else
static inline int nft_chain_filter_netdev_init(void) { return 0; }
static inline void nft_chain_filter_netdev_fini(void) {}
#endif /* CONFIG_NF_TABLES_NETDEV */

int __init nft_chain_filter_init(void)
{
	int err;

	err = nft_chain_filter_netdev_init();
	if (err < 0)
		return err;

	nft_chain_filter_ipv4_init();
	nft_chain_filter_ipv6_init();
	nft_chain_filter_arp_init();
	nft_chain_filter_inet_init();
	nft_chain_filter_bridge_init();

	return 0;
}

void nft_chain_filter_fini(void)
{
	nft_chain_filter_bridge_fini();
	nft_chain_filter_inet_fini();
	nft_chain_filter_arp_fini();
	nft_chain_filter_ipv6_fini();
	nft_chain_filter_ipv4_fini();
	nft_chain_filter_netdev_fini();
}
