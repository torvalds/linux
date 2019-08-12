// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_fib.h>

#include <net/ip_fib.h>
#include <net/route.h>

/* don't try to find route from mcast/bcast/zeronet */
static __be32 get_saddr(__be32 addr)
{
	if (ipv4_is_multicast(addr) || ipv4_is_lbcast(addr) ||
	    ipv4_is_zeronet(addr))
		return 0;
	return addr;
}

#define DSCP_BITS     0xfc

void nft_fib4_eval_type(const struct nft_expr *expr, struct nft_regs *regs,
			const struct nft_pktinfo *pkt)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	int noff = skb_network_offset(pkt->skb);
	u32 *dst = &regs->data[priv->dreg];
	const struct net_device *dev = NULL;
	struct iphdr *iph, _iph;
	__be32 addr;

	if (priv->flags & NFTA_FIB_F_IIF)
		dev = nft_in(pkt);
	else if (priv->flags & NFTA_FIB_F_OIF)
		dev = nft_out(pkt);

	iph = skb_header_pointer(pkt->skb, noff, sizeof(_iph), &_iph);
	if (!iph) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	if (priv->flags & NFTA_FIB_F_DADDR)
		addr = iph->daddr;
	else
		addr = iph->saddr;

	*dst = inet_dev_addr_type(nft_net(pkt), dev, addr);
}
EXPORT_SYMBOL_GPL(nft_fib4_eval_type);

void nft_fib4_eval(const struct nft_expr *expr, struct nft_regs *regs,
		   const struct nft_pktinfo *pkt)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	int noff = skb_network_offset(pkt->skb);
	u32 *dest = &regs->data[priv->dreg];
	struct iphdr *iph, _iph;
	struct fib_result res;
	struct flowi4 fl4 = {
		.flowi4_scope = RT_SCOPE_UNIVERSE,
		.flowi4_iif = LOOPBACK_IFINDEX,
	};
	const struct net_device *oif;
	const struct net_device *found;

	/*
	 * Do not set flowi4_oif, it restricts results (for example, asking
	 * for oif 3 will get RTN_UNICAST result even if the daddr exits
	 * on another interface.
	 *
	 * Search results for the desired outinterface instead.
	 */
	if (priv->flags & NFTA_FIB_F_OIF)
		oif = nft_out(pkt);
	else if (priv->flags & NFTA_FIB_F_IIF)
		oif = nft_in(pkt);
	else
		oif = NULL;

	if (nft_hook(pkt) == NF_INET_PRE_ROUTING &&
	    nft_fib_is_loopback(pkt->skb, nft_in(pkt))) {
		nft_fib_store_result(dest, priv, nft_in(pkt));
		return;
	}

	iph = skb_header_pointer(pkt->skb, noff, sizeof(_iph), &_iph);
	if (!iph) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	if (ipv4_is_zeronet(iph->saddr)) {
		if (ipv4_is_lbcast(iph->daddr) ||
		    ipv4_is_local_multicast(iph->daddr)) {
			nft_fib_store_result(dest, priv, pkt->skb->dev);
			return;
		}
	}

	if (priv->flags & NFTA_FIB_F_MARK)
		fl4.flowi4_mark = pkt->skb->mark;

	fl4.flowi4_tos = iph->tos & DSCP_BITS;

	if (priv->flags & NFTA_FIB_F_DADDR) {
		fl4.daddr = iph->daddr;
		fl4.saddr = get_saddr(iph->saddr);
	} else {
		fl4.daddr = iph->saddr;
		fl4.saddr = get_saddr(iph->daddr);
	}

	*dest = 0;

	if (fib_lookup(nft_net(pkt), &fl4, &res, FIB_LOOKUP_IGNORE_LINKSTATE))
		return;

	switch (res.type) {
	case RTN_UNICAST:
		break;
	case RTN_LOCAL: /* Should not see RTN_LOCAL here */
		return;
	default:
		break;
	}

       if (!oif) {
               found = FIB_RES_DEV(res);
	} else {
		if (!fib_info_nh_uses_dev(res.fi, oif))
			return;

		found = oif;
	}

	nft_fib_store_result(dest, priv, found);
}
EXPORT_SYMBOL_GPL(nft_fib4_eval);

static struct nft_expr_type nft_fib4_type;

static const struct nft_expr_ops nft_fib4_type_ops = {
	.type		= &nft_fib4_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fib)),
	.eval		= nft_fib4_eval_type,
	.init		= nft_fib_init,
	.dump		= nft_fib_dump,
	.validate	= nft_fib_validate,
};

static const struct nft_expr_ops nft_fib4_ops = {
	.type		= &nft_fib4_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_fib)),
	.eval		= nft_fib4_eval,
	.init		= nft_fib_init,
	.dump		= nft_fib_dump,
	.validate	= nft_fib_validate,
};

static const struct nft_expr_ops *
nft_fib4_select_ops(const struct nft_ctx *ctx,
		    const struct nlattr * const tb[])
{
	enum nft_fib_result result;

	if (!tb[NFTA_FIB_RESULT])
		return ERR_PTR(-EINVAL);

	result = ntohl(nla_get_be32(tb[NFTA_FIB_RESULT]));

	switch (result) {
	case NFT_FIB_RESULT_OIF:
		return &nft_fib4_ops;
	case NFT_FIB_RESULT_OIFNAME:
		return &nft_fib4_ops;
	case NFT_FIB_RESULT_ADDRTYPE:
		return &nft_fib4_type_ops;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static struct nft_expr_type nft_fib4_type __read_mostly = {
	.name		= "fib",
	.select_ops	= nft_fib4_select_ops,
	.policy		= nft_fib_policy,
	.maxattr	= NFTA_FIB_MAX,
	.family		= NFPROTO_IPV4,
	.owner		= THIS_MODULE,
};

static int __init nft_fib4_module_init(void)
{
	return nft_register_expr(&nft_fib4_type);
}

static void __exit nft_fib4_module_exit(void)
{
	nft_unregister_expr(&nft_fib4_type);
}

module_init(nft_fib4_module_init);
module_exit(nft_fib4_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_ALIAS_NFT_AF_EXPR(2, "fib");
