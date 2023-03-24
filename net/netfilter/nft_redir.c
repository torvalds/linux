// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Arturo Borrero Gonzalez <arturo@debian.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_redirect.h>
#include <net/netfilter/nf_tables.h>

struct nft_redir {
	u8			sreg_proto_min;
	u8			sreg_proto_max;
	u16			flags;
};

static const struct nla_policy nft_redir_policy[NFTA_REDIR_MAX + 1] = {
	[NFTA_REDIR_REG_PROTO_MIN]	= { .type = NLA_U32 },
	[NFTA_REDIR_REG_PROTO_MAX]	= { .type = NLA_U32 },
	[NFTA_REDIR_FLAGS]		= { .type = NLA_U32 },
};

static int nft_redir_validate(const struct nft_ctx *ctx,
			      const struct nft_expr *expr,
			      const struct nft_data **data)
{
	int err;

	err = nft_chain_validate_dependency(ctx->chain, NFT_CHAIN_T_NAT);
	if (err < 0)
		return err;

	return nft_chain_validate_hooks(ctx->chain,
					(1 << NF_INET_PRE_ROUTING) |
					(1 << NF_INET_LOCAL_OUT));
}

static int nft_redir_init(const struct nft_ctx *ctx,
			  const struct nft_expr *expr,
			  const struct nlattr * const tb[])
{
	struct nft_redir *priv = nft_expr_priv(expr);
	unsigned int plen;
	int err;

	plen = sizeof_field(struct nf_nat_range, min_proto.all);
	if (tb[NFTA_REDIR_REG_PROTO_MIN]) {
		err = nft_parse_register_load(tb[NFTA_REDIR_REG_PROTO_MIN],
					      &priv->sreg_proto_min, plen);
		if (err < 0)
			return err;

		if (tb[NFTA_REDIR_REG_PROTO_MAX]) {
			err = nft_parse_register_load(tb[NFTA_REDIR_REG_PROTO_MAX],
						      &priv->sreg_proto_max,
						      plen);
			if (err < 0)
				return err;
		} else {
			priv->sreg_proto_max = priv->sreg_proto_min;
		}
	}

	if (tb[NFTA_REDIR_FLAGS]) {
		priv->flags = ntohl(nla_get_be32(tb[NFTA_REDIR_FLAGS]));
		if (priv->flags & ~NF_NAT_RANGE_MASK)
			return -EINVAL;
	}

	return nf_ct_netns_get(ctx->net, ctx->family);
}

static int nft_redir_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_redir *priv = nft_expr_priv(expr);

	if (priv->sreg_proto_min) {
		if (nft_dump_register(skb, NFTA_REDIR_REG_PROTO_MIN,
				      priv->sreg_proto_min))
			goto nla_put_failure;
		if (nft_dump_register(skb, NFTA_REDIR_REG_PROTO_MAX,
				      priv->sreg_proto_max))
			goto nla_put_failure;
	}

	if (priv->flags != 0 &&
	    nla_put_be32(skb, NFTA_REDIR_FLAGS, htonl(priv->flags)))
			goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static void nft_redir_ipv4_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_redir *priv = nft_expr_priv(expr);
	struct nf_nat_ipv4_multi_range_compat mr;

	memset(&mr, 0, sizeof(mr));
	if (priv->sreg_proto_min) {
		mr.range[0].min.all = (__force __be16)nft_reg_load16(
			&regs->data[priv->sreg_proto_min]);
		mr.range[0].max.all = (__force __be16)nft_reg_load16(
			&regs->data[priv->sreg_proto_max]);
		mr.range[0].flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}

	mr.range[0].flags |= priv->flags;

	regs->verdict.code = nf_nat_redirect_ipv4(pkt->skb, &mr, nft_hook(pkt));
}

static void
nft_redir_ipv4_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, NFPROTO_IPV4);
}

static struct nft_expr_type nft_redir_ipv4_type;
static const struct nft_expr_ops nft_redir_ipv4_ops = {
	.type		= &nft_redir_ipv4_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_redir)),
	.eval		= nft_redir_ipv4_eval,
	.init		= nft_redir_init,
	.destroy	= nft_redir_ipv4_destroy,
	.dump		= nft_redir_dump,
	.validate	= nft_redir_validate,
};

static struct nft_expr_type nft_redir_ipv4_type __read_mostly = {
	.family		= NFPROTO_IPV4,
	.name		= "redir",
	.ops		= &nft_redir_ipv4_ops,
	.policy		= nft_redir_policy,
	.maxattr	= NFTA_REDIR_MAX,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_NF_TABLES_IPV6
static void nft_redir_ipv6_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	struct nft_redir *priv = nft_expr_priv(expr);
	struct nf_nat_range2 range;

	memset(&range, 0, sizeof(range));
	if (priv->sreg_proto_min) {
		range.min_proto.all = (__force __be16)nft_reg_load16(
			&regs->data[priv->sreg_proto_min]);
		range.max_proto.all = (__force __be16)nft_reg_load16(
			&regs->data[priv->sreg_proto_max]);
		range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}

	range.flags |= priv->flags;

	regs->verdict.code =
		nf_nat_redirect_ipv6(pkt->skb, &range, nft_hook(pkt));
}

static void
nft_redir_ipv6_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, NFPROTO_IPV6);
}

static struct nft_expr_type nft_redir_ipv6_type;
static const struct nft_expr_ops nft_redir_ipv6_ops = {
	.type		= &nft_redir_ipv6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_redir)),
	.eval		= nft_redir_ipv6_eval,
	.init		= nft_redir_init,
	.destroy	= nft_redir_ipv6_destroy,
	.dump		= nft_redir_dump,
	.validate	= nft_redir_validate,
};

static struct nft_expr_type nft_redir_ipv6_type __read_mostly = {
	.family		= NFPROTO_IPV6,
	.name		= "redir",
	.ops		= &nft_redir_ipv6_ops,
	.policy		= nft_redir_policy,
	.maxattr	= NFTA_REDIR_MAX,
	.owner		= THIS_MODULE,
};
#endif

#ifdef CONFIG_NF_TABLES_INET
static void nft_redir_inet_eval(const struct nft_expr *expr,
				struct nft_regs *regs,
				const struct nft_pktinfo *pkt)
{
	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		return nft_redir_ipv4_eval(expr, regs, pkt);
	case NFPROTO_IPV6:
		return nft_redir_ipv6_eval(expr, regs, pkt);
	}

	WARN_ON_ONCE(1);
}

static void
nft_redir_inet_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, NFPROTO_INET);
}

static struct nft_expr_type nft_redir_inet_type;
static const struct nft_expr_ops nft_redir_inet_ops = {
	.type		= &nft_redir_inet_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_redir)),
	.eval		= nft_redir_inet_eval,
	.init		= nft_redir_init,
	.destroy	= nft_redir_inet_destroy,
	.dump		= nft_redir_dump,
	.validate	= nft_redir_validate,
};

static struct nft_expr_type nft_redir_inet_type __read_mostly = {
	.family		= NFPROTO_INET,
	.name		= "redir",
	.ops		= &nft_redir_inet_ops,
	.policy		= nft_redir_policy,
	.maxattr	= NFTA_REDIR_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_redir_module_init_inet(void)
{
	return nft_register_expr(&nft_redir_inet_type);
}
#else
static inline int nft_redir_module_init_inet(void) { return 0; }
#endif

static int __init nft_redir_module_init(void)
{
	int ret = nft_register_expr(&nft_redir_ipv4_type);

	if (ret)
		return ret;

#ifdef CONFIG_NF_TABLES_IPV6
	ret = nft_register_expr(&nft_redir_ipv6_type);
	if (ret) {
		nft_unregister_expr(&nft_redir_ipv4_type);
		return ret;
	}
#endif

	ret = nft_redir_module_init_inet();
	if (ret < 0) {
		nft_unregister_expr(&nft_redir_ipv4_type);
#ifdef CONFIG_NF_TABLES_IPV6
		nft_unregister_expr(&nft_redir_ipv6_type);
#endif
		return ret;
	}

	return ret;
}

static void __exit nft_redir_module_exit(void)
{
	nft_unregister_expr(&nft_redir_ipv4_type);
#ifdef CONFIG_NF_TABLES_IPV6
	nft_unregister_expr(&nft_redir_ipv6_type);
#endif
#ifdef CONFIG_NF_TABLES_INET
	nft_unregister_expr(&nft_redir_inet_type);
#endif
}

module_init(nft_redir_module_init);
module_exit(nft_redir_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arturo Borrero Gonzalez <arturo@debian.org>");
MODULE_ALIAS_NFT_EXPR("redir");
MODULE_DESCRIPTION("Netfilter nftables redirect support");
