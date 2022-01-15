// SPDX-License-Identifier: GPL-2.0-only
#include <net/ip.h>
#include <net/tcp.h>

#include <net/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink_osf.h>

struct nft_osf {
	u8			dreg;
	u8			ttl;
	u32			flags;
};

static const struct nla_policy nft_osf_policy[NFTA_OSF_MAX + 1] = {
	[NFTA_OSF_DREG]		= { .type = NLA_U32 },
	[NFTA_OSF_TTL]		= { .type = NLA_U8 },
	[NFTA_OSF_FLAGS]	= { .type = NLA_U32 },
};

static void nft_osf_eval(const struct nft_expr *expr, struct nft_regs *regs,
			 const struct nft_pktinfo *pkt)
{
	struct nft_osf *priv = nft_expr_priv(expr);
	u32 *dest = &regs->data[priv->dreg];
	struct sk_buff *skb = pkt->skb;
	char os_match[NFT_OSF_MAXGENRELEN + 1];
	const struct tcphdr *tcp;
	struct nf_osf_data data;
	struct tcphdr _tcph;

	if (pkt->tprot != IPPROTO_TCP) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	tcp = skb_header_pointer(skb, ip_hdrlen(skb),
				 sizeof(struct tcphdr), &_tcph);
	if (!tcp) {
		regs->verdict.code = NFT_BREAK;
		return;
	}
	if (!tcp->syn) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	if (!nf_osf_find(skb, nf_osf_fingers, priv->ttl, &data)) {
		strncpy((char *)dest, "unknown", NFT_OSF_MAXGENRELEN);
	} else {
		if (priv->flags & NFT_OSF_F_VERSION)
			snprintf(os_match, NFT_OSF_MAXGENRELEN, "%s:%s",
				 data.genre, data.version);
		else
			strlcpy(os_match, data.genre, NFT_OSF_MAXGENRELEN);

		strncpy((char *)dest, os_match, NFT_OSF_MAXGENRELEN);
	}
}

static int nft_osf_init(const struct nft_ctx *ctx,
			const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_osf *priv = nft_expr_priv(expr);
	u32 flags;
	int err;
	u8 ttl;

	if (!tb[NFTA_OSF_DREG])
		return -EINVAL;

	if (tb[NFTA_OSF_TTL]) {
		ttl = nla_get_u8(tb[NFTA_OSF_TTL]);
		if (ttl > 2)
			return -EINVAL;
		priv->ttl = ttl;
	}

	if (tb[NFTA_OSF_FLAGS]) {
		flags = ntohl(nla_get_be32(tb[NFTA_OSF_FLAGS]));
		if (flags != NFT_OSF_F_VERSION)
			return -EINVAL;
		priv->flags = flags;
	}

	err = nft_parse_register_store(ctx, tb[NFTA_OSF_DREG], &priv->dreg,
				       NULL, NFT_DATA_VALUE,
				       NFT_OSF_MAXGENRELEN);
	if (err < 0)
		return err;

	return 0;
}

static int nft_osf_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_osf *priv = nft_expr_priv(expr);

	if (nla_put_u8(skb, NFTA_OSF_TTL, priv->ttl))
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_OSF_FLAGS, ntohl(priv->flags)))
		goto nla_put_failure;

	if (nft_dump_register(skb, NFTA_OSF_DREG, priv->dreg))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int nft_osf_validate(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain, (1 << NF_INET_LOCAL_IN) |
						    (1 << NF_INET_PRE_ROUTING) |
						    (1 << NF_INET_FORWARD));
}

static struct nft_expr_type nft_osf_type;
static const struct nft_expr_ops nft_osf_op = {
	.eval		= nft_osf_eval,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_osf)),
	.init		= nft_osf_init,
	.dump		= nft_osf_dump,
	.type		= &nft_osf_type,
	.validate	= nft_osf_validate,
};

static struct nft_expr_type nft_osf_type __read_mostly = {
	.ops		= &nft_osf_op,
	.name		= "osf",
	.owner		= THIS_MODULE,
	.policy		= nft_osf_policy,
	.maxattr	= NFTA_OSF_MAX,
};

static int __init nft_osf_module_init(void)
{
	return nft_register_expr(&nft_osf_type);
}

static void __exit nft_osf_module_exit(void)
{
	return nft_unregister_expr(&nft_osf_type);
}

module_init(nft_osf_module_init);
module_exit(nft_osf_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fernando Fernandez <ffmancera@riseup.net>");
MODULE_ALIAS_NFT_EXPR("osf");
MODULE_DESCRIPTION("nftables passive OS fingerprint support");
