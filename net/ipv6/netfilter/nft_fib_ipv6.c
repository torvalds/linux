// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_fib.h>

#include <net/ip6_fib.h>
#include <net/ip6_route.h>

static int get_ifindex(const struct net_device *dev)
{
	return dev ? dev->ifindex : 0;
}

static int nft_fib6_flowi_init(struct flowi6 *fl6, const struct nft_fib *priv,
			       const struct nft_pktinfo *pkt,
			       const struct net_device *dev,
			       struct ipv6hdr *iph)
{
	int lookup_flags = 0;

	if (priv->flags & NFTA_FIB_F_DADDR) {
		fl6->daddr = iph->daddr;
		fl6->saddr = iph->saddr;
	} else {
		fl6->daddr = iph->saddr;
		fl6->saddr = iph->daddr;
	}

	if (ipv6_addr_type(&fl6->daddr) & IPV6_ADDR_LINKLOCAL) {
		lookup_flags |= RT6_LOOKUP_F_IFACE;
		fl6->flowi6_oif = get_ifindex(dev ? dev : pkt->skb->dev);
	} else if ((priv->flags & NFTA_FIB_F_IIF) &&
		   (netif_is_l3_master(dev) || netif_is_l3_slave(dev))) {
		fl6->flowi6_oif = dev->ifindex;
	}

	if (ipv6_addr_type(&fl6->saddr) & IPV6_ADDR_UNICAST)
		lookup_flags |= RT6_LOOKUP_F_HAS_SADDR;

	if (priv->flags & NFTA_FIB_F_MARK)
		fl6->flowi6_mark = pkt->skb->mark;

	fl6->flowlabel = (*(__be32 *)iph) & IPV6_FLOWINFO_MASK;

	return lookup_flags;
}

static u32 __nft_fib6_eval_type(const struct nft_fib *priv,
				const struct nft_pktinfo *pkt,
				struct ipv6hdr *iph)
{
	const struct net_device *dev = NULL;
	int route_err, addrtype;
	struct rt6_info *rt;
	struct flowi6 fl6 = {
		.flowi6_iif = LOOPBACK_IFINDEX,
		.flowi6_proto = pkt->tprot,
	};
	u32 ret = 0;

	if (priv->flags & NFTA_FIB_F_IIF)
		dev = nft_in(pkt);
	else if (priv->flags & NFTA_FIB_F_OIF)
		dev = nft_out(pkt);

	nft_fib6_flowi_init(&fl6, priv, pkt, dev, iph);

	if (dev && nf_ipv6_chk_addr(nft_net(pkt), &fl6.daddr, dev, true))
		ret = RTN_LOCAL;

	route_err = nf_ip6_route(nft_net(pkt), (struct dst_entry **)&rt,
				 flowi6_to_flowi(&fl6), false);
	if (route_err)
		goto err;

	if (rt->rt6i_flags & RTF_REJECT) {
		route_err = rt->dst.error;
		dst_release(&rt->dst);
		goto err;
	}

	if (ipv6_anycast_destination((struct dst_entry *)rt, &fl6.daddr))
		ret = RTN_ANYCAST;
	else if (!dev && rt->rt6i_flags & RTF_LOCAL)
		ret = RTN_LOCAL;

	dst_release(&rt->dst);

	if (ret)
		return ret;

	addrtype = ipv6_addr_type(&fl6.daddr);

	if (addrtype & IPV6_ADDR_MULTICAST)
		return RTN_MULTICAST;
	if (addrtype & IPV6_ADDR_UNICAST)
		return RTN_UNICAST;

	return RTN_UNSPEC;
 err:
	switch (route_err) {
	case -EINVAL:
		return RTN_BLACKHOLE;
	case -EACCES:
		return RTN_PROHIBIT;
	case -EAGAIN:
		return RTN_THROW;
	default:
		break;
	}

	return RTN_UNREACHABLE;
}

void nft_fib6_eval_type(const struct nft_expr *expr, struct nft_regs *regs,
			const struct nft_pktinfo *pkt)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	int noff = skb_network_offset(pkt->skb);
	u32 *dest = &regs->data[priv->dreg];
	struct ipv6hdr *iph, _iph;

	iph = skb_header_pointer(pkt->skb, noff, sizeof(_iph), &_iph);
	if (!iph) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	*dest = __nft_fib6_eval_type(priv, pkt, iph);
}
EXPORT_SYMBOL_GPL(nft_fib6_eval_type);

static bool nft_fib_v6_skip_icmpv6(const struct sk_buff *skb, u8 next, const struct ipv6hdr *iph)
{
	if (likely(next != IPPROTO_ICMPV6))
		return false;

	if (ipv6_addr_type(&iph->saddr) != IPV6_ADDR_ANY)
		return false;

	return ipv6_addr_type(&iph->daddr) & IPV6_ADDR_LINKLOCAL;
}

void nft_fib6_eval(const struct nft_expr *expr, struct nft_regs *regs,
		   const struct nft_pktinfo *pkt)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	int noff = skb_network_offset(pkt->skb);
	const struct net_device *oif = NULL;
	u32 *dest = &regs->data[priv->dreg];
	struct ipv6hdr *iph, _iph;
	struct flowi6 fl6 = {
		.flowi6_iif = LOOPBACK_IFINDEX,
		.flowi6_proto = pkt->tprot,
	};
	struct rt6_info *rt;
	int lookup_flags;

	if (priv->flags & NFTA_FIB_F_IIF)
		oif = nft_in(pkt);
	else if (priv->flags & NFTA_FIB_F_OIF)
		oif = nft_out(pkt);

	iph = skb_header_pointer(pkt->skb, noff, sizeof(_iph), &_iph);
	if (!iph) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	lookup_flags = nft_fib6_flowi_init(&fl6, priv, pkt, oif, iph);

	if (nft_hook(pkt) == NF_INET_PRE_ROUTING ||
	    nft_hook(pkt) == NF_INET_INGRESS) {
		if (nft_fib_is_loopback(pkt->skb, nft_in(pkt)) ||
		    nft_fib_v6_skip_icmpv6(pkt->skb, pkt->tprot, iph)) {
			nft_fib_store_result(dest, priv, nft_in(pkt));
			return;
		}
	}

	*dest = 0;
	rt = (void *)ip6_route_lookup(nft_net(pkt), &fl6, pkt->skb,
				      lookup_flags);
	if (rt->dst.error)
		goto put_rt_err;

	/* Should not see RTF_LOCAL here */
	if (rt->rt6i_flags & (RTF_REJECT | RTF_ANYCAST | RTF_LOCAL))
		goto put_rt_err;

	if (oif && oif != rt->rt6i_idev->dev &&
	    l3mdev_master_ifindex_rcu(rt->rt6i_idev->dev) != oif->ifindex)
		goto put_rt_err;

	nft_fib_store_result(dest, priv, rt->rt6i_idev->dev);
 put_rt_err:
	ip6_rt_put(rt);
}
EXPORT_SYMBOL_GPL(nft_fib6_eval);

static struct nft_expr_type nft_fib6_type;

static const struct nft_expr_ops nft_fib6_type_ops = {
	.type		= &nft_fib6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fib)),
	.eval		= nft_fib6_eval_type,
	.init		= nft_fib_init,
	.dump		= nft_fib_dump,
	.validate	= nft_fib_validate,
};

static const struct nft_expr_ops nft_fib6_ops = {
	.type		= &nft_fib6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fib)),
	.eval		= nft_fib6_eval,
	.init		= nft_fib_init,
	.dump		= nft_fib_dump,
	.validate	= nft_fib_validate,
};

static const struct nft_expr_ops *
nft_fib6_select_ops(const struct nft_ctx *ctx,
		    const struct nlattr * const tb[])
{
	enum nft_fib_result result;

	if (!tb[NFTA_FIB_RESULT])
		return ERR_PTR(-EINVAL);

	result = ntohl(nla_get_be32(tb[NFTA_FIB_RESULT]));

	switch (result) {
	case NFT_FIB_RESULT_OIF:
		return &nft_fib6_ops;
	case NFT_FIB_RESULT_OIFNAME:
		return &nft_fib6_ops;
	case NFT_FIB_RESULT_ADDRTYPE:
		return &nft_fib6_type_ops;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static struct nft_expr_type nft_fib6_type __read_mostly = {
	.name		= "fib",
	.select_ops	= nft_fib6_select_ops,
	.policy		= nft_fib_policy,
	.maxattr	= NFTA_FIB_MAX,
	.family		= NFPROTO_IPV6,
	.owner		= THIS_MODULE,
};

static int __init nft_fib6_module_init(void)
{
	return nft_register_expr(&nft_fib6_type);
}

static void __exit nft_fib6_module_exit(void)
{
	nft_unregister_expr(&nft_fib6_type);
}
module_init(nft_fib6_module_init);
module_exit(nft_fib6_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_ALIAS_NFT_AF_EXPR(10, "fib");
MODULE_DESCRIPTION("nftables fib / ipv6 route lookup support");
