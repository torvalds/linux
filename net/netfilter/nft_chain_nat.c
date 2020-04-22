// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

static unsigned int nft_nat_do_chain(void *priv, struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (state->pf) {
#ifdef CONFIG_NF_TABLES_IPV4
	case NFPROTO_IPV4:
		nft_set_pktinfo_ipv4(&pkt, skb);
		break;
#endif
#ifdef CONFIG_NF_TABLES_IPV6
	case NFPROTO_IPV6:
		nft_set_pktinfo_ipv6(&pkt, skb);
		break;
#endif
	default:
		break;
	}

	return nft_do_chain(&pkt, priv);
}

#ifdef CONFIG_NF_TABLES_IPV4
static const struct nft_chain_type nft_chain_nat_ipv4 = {
	.name		= "nat",
	.type		= NFT_CHAIN_T_NAT,
	.family		= NFPROTO_IPV4,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.hooks		= {
		[NF_INET_PRE_ROUTING]	= nft_nat_do_chain,
		[NF_INET_POST_ROUTING]	= nft_nat_do_chain,
		[NF_INET_LOCAL_OUT]	= nft_nat_do_chain,
		[NF_INET_LOCAL_IN]	= nft_nat_do_chain,
	},
	.ops_register = nf_nat_ipv4_register_fn,
	.ops_unregister = nf_nat_ipv4_unregister_fn,
};
#endif

#ifdef CONFIG_NF_TABLES_IPV6
static const struct nft_chain_type nft_chain_nat_ipv6 = {
	.name		= "nat",
	.type		= NFT_CHAIN_T_NAT,
	.family		= NFPROTO_IPV6,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.hooks		= {
		[NF_INET_PRE_ROUTING]	= nft_nat_do_chain,
		[NF_INET_POST_ROUTING]	= nft_nat_do_chain,
		[NF_INET_LOCAL_OUT]	= nft_nat_do_chain,
		[NF_INET_LOCAL_IN]	= nft_nat_do_chain,
	},
	.ops_register		= nf_nat_ipv6_register_fn,
	.ops_unregister		= nf_nat_ipv6_unregister_fn,
};
#endif

#ifdef CONFIG_NF_TABLES_INET
static int nft_nat_inet_reg(struct net *net, const struct nf_hook_ops *ops)
{
	return nf_nat_inet_register_fn(net, ops);
}

static void nft_nat_inet_unreg(struct net *net, const struct nf_hook_ops *ops)
{
	nf_nat_inet_unregister_fn(net, ops);
}

static const struct nft_chain_type nft_chain_nat_inet = {
	.name		= "nat",
	.type		= NFT_CHAIN_T_NAT,
	.family		= NFPROTO_INET,
	.owner		= THIS_MODULE,
	.hook_mask	= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_LOCAL_IN) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_POST_ROUTING),
	.hooks		= {
		[NF_INET_PRE_ROUTING]	= nft_nat_do_chain,
		[NF_INET_LOCAL_IN]	= nft_nat_do_chain,
		[NF_INET_LOCAL_OUT]	= nft_nat_do_chain,
		[NF_INET_POST_ROUTING]	= nft_nat_do_chain,
	},
	.ops_register		= nft_nat_inet_reg,
	.ops_unregister		= nft_nat_inet_unreg,
};
#endif

static int __init nft_chain_nat_init(void)
{
#ifdef CONFIG_NF_TABLES_IPV6
	nft_register_chain_type(&nft_chain_nat_ipv6);
#endif
#ifdef CONFIG_NF_TABLES_IPV4
	nft_register_chain_type(&nft_chain_nat_ipv4);
#endif
#ifdef CONFIG_NF_TABLES_INET
	nft_register_chain_type(&nft_chain_nat_inet);
#endif

	return 0;
}

static void __exit nft_chain_nat_exit(void)
{
#ifdef CONFIG_NF_TABLES_IPV4
	nft_unregister_chain_type(&nft_chain_nat_ipv4);
#endif
#ifdef CONFIG_NF_TABLES_IPV6
	nft_unregister_chain_type(&nft_chain_nat_ipv6);
#endif
#ifdef CONFIG_NF_TABLES_INET
	nft_unregister_chain_type(&nft_chain_nat_inet);
#endif
}

module_init(nft_chain_nat_init);
module_exit(nft_chain_nat_exit);

MODULE_LICENSE("GPL");
#ifdef CONFIG_NF_TABLES_IPV4
MODULE_ALIAS_NFT_CHAIN(AF_INET, "nat");
#endif
#ifdef CONFIG_NF_TABLES_IPV6
MODULE_ALIAS_NFT_CHAIN(AF_INET6, "nat");
#endif
#ifdef CONFIG_NF_TABLES_INET
MODULE_ALIAS_NFT_CHAIN(1, "nat");	/* NFPROTO_INET */
#endif
