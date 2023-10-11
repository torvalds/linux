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
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_masquerade.h>

struct nft_masq {
	u32			flags;
	u8			sreg_proto_min;
	u8			sreg_proto_max;
};

static const struct nla_policy nft_masq_policy[NFTA_MASQ_MAX + 1] = {
	[NFTA_MASQ_FLAGS]		=
		NLA_POLICY_MASK(NLA_BE32, NF_NAT_RANGE_MASK),
	[NFTA_MASQ_REG_PROTO_MIN]	= { .type = NLA_U32 },
	[NFTA_MASQ_REG_PROTO_MAX]	= { .type = NLA_U32 },
};

static int nft_masq_validate(const struct nft_ctx *ctx,
			     const struct nft_expr *expr,
			     const struct nft_data **data)
{
	int err;

	err = nft_chain_validate_dependency(ctx->chain, NFT_CHAIN_T_NAT);
	if (err < 0)
		return err;

	return nft_chain_validate_hooks(ctx->chain,
				        (1 << NF_INET_POST_ROUTING));
}

static int nft_masq_init(const struct nft_ctx *ctx,
			 const struct nft_expr *expr,
			 const struct nlattr * const tb[])
{
	u32 plen = sizeof_field(struct nf_nat_range, min_proto.all);
	struct nft_masq *priv = nft_expr_priv(expr);
	int err;

	if (tb[NFTA_MASQ_FLAGS])
		priv->flags = ntohl(nla_get_be32(tb[NFTA_MASQ_FLAGS]));

	if (tb[NFTA_MASQ_REG_PROTO_MIN]) {
		err = nft_parse_register_load(tb[NFTA_MASQ_REG_PROTO_MIN],
					      &priv->sreg_proto_min, plen);
		if (err < 0)
			return err;

		if (tb[NFTA_MASQ_REG_PROTO_MAX]) {
			err = nft_parse_register_load(tb[NFTA_MASQ_REG_PROTO_MAX],
						      &priv->sreg_proto_max,
						      plen);
			if (err < 0)
				return err;
		} else {
			priv->sreg_proto_max = priv->sreg_proto_min;
		}
	}

	return nf_ct_netns_get(ctx->net, ctx->family);
}

static int nft_masq_dump(struct sk_buff *skb,
			 const struct nft_expr *expr, bool reset)
{
	const struct nft_masq *priv = nft_expr_priv(expr);

	if (priv->flags != 0 &&
	    nla_put_be32(skb, NFTA_MASQ_FLAGS, htonl(priv->flags)))
		goto nla_put_failure;

	if (priv->sreg_proto_min) {
		if (nft_dump_register(skb, NFTA_MASQ_REG_PROTO_MIN,
				      priv->sreg_proto_min) ||
		    nft_dump_register(skb, NFTA_MASQ_REG_PROTO_MAX,
				      priv->sreg_proto_max))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -1;
}

static void nft_masq_eval(const struct nft_expr *expr,
			  struct nft_regs *regs,
			  const struct nft_pktinfo *pkt)
{
	const struct nft_masq *priv = nft_expr_priv(expr);
	struct nf_nat_range2 range;

	memset(&range, 0, sizeof(range));
	range.flags = priv->flags;
	if (priv->sreg_proto_min) {
		range.min_proto.all = (__force __be16)
			nft_reg_load16(&regs->data[priv->sreg_proto_min]);
		range.max_proto.all = (__force __be16)
			nft_reg_load16(&regs->data[priv->sreg_proto_max]);
	}

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		regs->verdict.code = nf_nat_masquerade_ipv4(pkt->skb,
							    nft_hook(pkt),
							    &range,
							    nft_out(pkt));
		break;
#ifdef CONFIG_NF_TABLES_IPV6
	case NFPROTO_IPV6:
		regs->verdict.code = nf_nat_masquerade_ipv6(pkt->skb, &range,
							    nft_out(pkt));
		break;
#endif
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static void
nft_masq_ipv4_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, NFPROTO_IPV4);
}

static struct nft_expr_type nft_masq_ipv4_type;
static const struct nft_expr_ops nft_masq_ipv4_ops = {
	.type		= &nft_masq_ipv4_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_masq)),
	.eval		= nft_masq_eval,
	.init		= nft_masq_init,
	.destroy	= nft_masq_ipv4_destroy,
	.dump		= nft_masq_dump,
	.validate	= nft_masq_validate,
	.reduce		= NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_masq_ipv4_type __read_mostly = {
	.family		= NFPROTO_IPV4,
	.name		= "masq",
	.ops		= &nft_masq_ipv4_ops,
	.policy		= nft_masq_policy,
	.maxattr	= NFTA_MASQ_MAX,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_NF_TABLES_IPV6
static void
nft_masq_ipv6_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, NFPROTO_IPV6);
}

static struct nft_expr_type nft_masq_ipv6_type;
static const struct nft_expr_ops nft_masq_ipv6_ops = {
	.type		= &nft_masq_ipv6_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_masq)),
	.eval		= nft_masq_eval,
	.init		= nft_masq_init,
	.destroy	= nft_masq_ipv6_destroy,
	.dump		= nft_masq_dump,
	.validate	= nft_masq_validate,
	.reduce		= NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_masq_ipv6_type __read_mostly = {
	.family		= NFPROTO_IPV6,
	.name		= "masq",
	.ops		= &nft_masq_ipv6_ops,
	.policy		= nft_masq_policy,
	.maxattr	= NFTA_MASQ_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_masq_module_init_ipv6(void)
{
	return nft_register_expr(&nft_masq_ipv6_type);
}

static void nft_masq_module_exit_ipv6(void)
{
	nft_unregister_expr(&nft_masq_ipv6_type);
}
#else
static inline int nft_masq_module_init_ipv6(void) { return 0; }
static inline void nft_masq_module_exit_ipv6(void) {}
#endif

#ifdef CONFIG_NF_TABLES_INET
static void
nft_masq_inet_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	nf_ct_netns_put(ctx->net, NFPROTO_INET);
}

static struct nft_expr_type nft_masq_inet_type;
static const struct nft_expr_ops nft_masq_inet_ops = {
	.type		= &nft_masq_inet_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_masq)),
	.eval		= nft_masq_eval,
	.init		= nft_masq_init,
	.destroy	= nft_masq_inet_destroy,
	.dump		= nft_masq_dump,
	.validate	= nft_masq_validate,
	.reduce		= NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_masq_inet_type __read_mostly = {
	.family		= NFPROTO_INET,
	.name		= "masq",
	.ops		= &nft_masq_inet_ops,
	.policy		= nft_masq_policy,
	.maxattr	= NFTA_MASQ_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_masq_module_init_inet(void)
{
	return nft_register_expr(&nft_masq_inet_type);
}

static void nft_masq_module_exit_inet(void)
{
	nft_unregister_expr(&nft_masq_inet_type);
}
#else
static inline int nft_masq_module_init_inet(void) { return 0; }
static inline void nft_masq_module_exit_inet(void) {}
#endif

static int __init nft_masq_module_init(void)
{
	int ret;

	ret = nft_masq_module_init_ipv6();
	if (ret < 0)
		return ret;

	ret = nft_masq_module_init_inet();
	if (ret < 0) {
		nft_masq_module_exit_ipv6();
		return ret;
	}

	ret = nft_register_expr(&nft_masq_ipv4_type);
	if (ret < 0) {
		nft_masq_module_exit_inet();
		nft_masq_module_exit_ipv6();
		return ret;
	}

	ret = nf_nat_masquerade_inet_register_notifiers();
	if (ret < 0) {
		nft_masq_module_exit_ipv6();
		nft_masq_module_exit_inet();
		nft_unregister_expr(&nft_masq_ipv4_type);
		return ret;
	}

	return ret;
}

static void __exit nft_masq_module_exit(void)
{
	nft_masq_module_exit_ipv6();
	nft_masq_module_exit_inet();
	nft_unregister_expr(&nft_masq_ipv4_type);
	nf_nat_masquerade_inet_unregister_notifiers();
}

module_init(nft_masq_module_init);
module_exit(nft_masq_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arturo Borrero Gonzalez <arturo@debian.org>");
MODULE_ALIAS_NFT_EXPR("masq");
MODULE_DESCRIPTION("Netfilter nftables masquerade expression support");
