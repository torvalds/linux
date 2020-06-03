// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2012 Pablo Neira Ayuso <pablo@netfilter.org>
 * Copyright (c) 2012 Intel Corporation
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/string.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_tables.h>
#include <net/ip.h>

struct nft_nat {
	enum nft_registers      sreg_addr_min:8;
	enum nft_registers      sreg_addr_max:8;
	enum nft_registers      sreg_proto_min:8;
	enum nft_registers      sreg_proto_max:8;
	enum nf_nat_manip_type  type:8;
	u8			family;
	u16			flags;
};

static void nft_nat_setup_addr(struct nf_nat_range2 *range,
			       const struct nft_regs *regs,
			       const struct nft_nat *priv)
{
	switch (priv->family) {
	case AF_INET:
		range->min_addr.ip = (__force __be32)
				regs->data[priv->sreg_addr_min];
		range->max_addr.ip = (__force __be32)
				regs->data[priv->sreg_addr_max];
		break;
	case AF_INET6:
		memcpy(range->min_addr.ip6, &regs->data[priv->sreg_addr_min],
		       sizeof(range->min_addr.ip6));
		memcpy(range->max_addr.ip6, &regs->data[priv->sreg_addr_max],
		       sizeof(range->max_addr.ip6));
		break;
	}
}

static void nft_nat_setup_proto(struct nf_nat_range2 *range,
				const struct nft_regs *regs,
				const struct nft_nat *priv)
{
	range->min_proto.all = (__force __be16)
		nft_reg_load16(&regs->data[priv->sreg_proto_min]);
	range->max_proto.all = (__force __be16)
		nft_reg_load16(&regs->data[priv->sreg_proto_max]);
}

static void nft_nat_setup_netmap(struct nf_nat_range2 *range,
				 const struct nft_pktinfo *pkt,
				 const struct nft_nat *priv)
{
	struct sk_buff *skb = pkt->skb;
	union nf_inet_addr new_addr;
	__be32 netmask;
	int i, len = 0;

	switch (priv->type) {
	case NFT_NAT_SNAT:
		if (nft_pf(pkt) == NFPROTO_IPV4) {
			new_addr.ip = ip_hdr(skb)->saddr;
			len = sizeof(struct in_addr);
		} else {
			new_addr.in6 = ipv6_hdr(skb)->saddr;
			len = sizeof(struct in6_addr);
		}
		break;
	case NFT_NAT_DNAT:
		if (nft_pf(pkt) == NFPROTO_IPV4) {
			new_addr.ip = ip_hdr(skb)->daddr;
			len = sizeof(struct in_addr);
		} else {
			new_addr.in6 = ipv6_hdr(skb)->daddr;
			len = sizeof(struct in6_addr);
		}
		break;
	}

	for (i = 0; i < len / sizeof(__be32); i++) {
		netmask = ~(range->min_addr.ip6[i] ^ range->max_addr.ip6[i]);
		new_addr.ip6[i] &= ~netmask;
		new_addr.ip6[i] |= range->min_addr.ip6[i] & netmask;
	}

	range->min_addr = new_addr;
	range->max_addr = new_addr;
}

static void nft_nat_eval(const struct nft_expr *expr,
			 struct nft_regs *regs,
			 const struct nft_pktinfo *pkt)
{
	const struct nft_nat *priv = nft_expr_priv(expr);
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(pkt->skb, &ctinfo);
	struct nf_nat_range2 range;

	memset(&range, 0, sizeof(range));

	if (priv->sreg_addr_min) {
		nft_nat_setup_addr(&range, regs, priv);
		if (priv->flags & NF_NAT_RANGE_NETMAP)
			nft_nat_setup_netmap(&range, pkt, priv);
	}

	if (priv->sreg_proto_min)
		nft_nat_setup_proto(&range, regs, priv);

	range.flags = priv->flags;

	regs->verdict.code = nf_nat_setup_info(ct, &range, priv->type);
}

static const struct nla_policy nft_nat_policy[NFTA_NAT_MAX + 1] = {
	[NFTA_NAT_TYPE]		 = { .type = NLA_U32 },
	[NFTA_NAT_FAMILY]	 = { .type = NLA_U32 },
	[NFTA_NAT_REG_ADDR_MIN]	 = { .type = NLA_U32 },
	[NFTA_NAT_REG_ADDR_MAX]	 = { .type = NLA_U32 },
	[NFTA_NAT_REG_PROTO_MIN] = { .type = NLA_U32 },
	[NFTA_NAT_REG_PROTO_MAX] = { .type = NLA_U32 },
	[NFTA_NAT_FLAGS]	 = { .type = NLA_U32 },
};

static int nft_nat_validate(const struct nft_ctx *ctx,
			    const struct nft_expr *expr,
			    const struct nft_data **data)
{
	struct nft_nat *priv = nft_expr_priv(expr);
	int err;

	err = nft_chain_validate_dependency(ctx->chain, NFT_CHAIN_T_NAT);
	if (err < 0)
		return err;

	switch (priv->type) {
	case NFT_NAT_SNAT:
		err = nft_chain_validate_hooks(ctx->chain,
					       (1 << NF_INET_POST_ROUTING) |
					       (1 << NF_INET_LOCAL_IN));
		break;
	case NFT_NAT_DNAT:
		err = nft_chain_validate_hooks(ctx->chain,
					       (1 << NF_INET_PRE_ROUTING) |
					       (1 << NF_INET_LOCAL_OUT));
		break;
	}

	return err;
}

static int nft_nat_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_nat *priv = nft_expr_priv(expr);
	unsigned int alen, plen;
	u32 family;
	int err;

	if (tb[NFTA_NAT_TYPE] == NULL ||
	    (tb[NFTA_NAT_REG_ADDR_MIN] == NULL &&
	     tb[NFTA_NAT_REG_PROTO_MIN] == NULL))
		return -EINVAL;

	switch (ntohl(nla_get_be32(tb[NFTA_NAT_TYPE]))) {
	case NFT_NAT_SNAT:
		priv->type = NF_NAT_MANIP_SRC;
		break;
	case NFT_NAT_DNAT:
		priv->type = NF_NAT_MANIP_DST;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (tb[NFTA_NAT_FAMILY] == NULL)
		return -EINVAL;

	family = ntohl(nla_get_be32(tb[NFTA_NAT_FAMILY]));
	if (ctx->family != NFPROTO_INET && ctx->family != family)
		return -EOPNOTSUPP;

	switch (family) {
	case NFPROTO_IPV4:
		alen = sizeof_field(struct nf_nat_range, min_addr.ip);
		break;
	case NFPROTO_IPV6:
		alen = sizeof_field(struct nf_nat_range, min_addr.ip6);
		break;
	default:
		return -EAFNOSUPPORT;
	}
	priv->family = family;

	if (tb[NFTA_NAT_REG_ADDR_MIN]) {
		priv->sreg_addr_min =
			nft_parse_register(tb[NFTA_NAT_REG_ADDR_MIN]);
		err = nft_validate_register_load(priv->sreg_addr_min, alen);
		if (err < 0)
			return err;

		if (tb[NFTA_NAT_REG_ADDR_MAX]) {
			priv->sreg_addr_max =
				nft_parse_register(tb[NFTA_NAT_REG_ADDR_MAX]);

			err = nft_validate_register_load(priv->sreg_addr_max,
							 alen);
			if (err < 0)
				return err;
		} else {
			priv->sreg_addr_max = priv->sreg_addr_min;
		}

		priv->flags |= NF_NAT_RANGE_MAP_IPS;
	}

	plen = sizeof_field(struct nf_nat_range, min_addr.all);
	if (tb[NFTA_NAT_REG_PROTO_MIN]) {
		priv->sreg_proto_min =
			nft_parse_register(tb[NFTA_NAT_REG_PROTO_MIN]);

		err = nft_validate_register_load(priv->sreg_proto_min, plen);
		if (err < 0)
			return err;

		if (tb[NFTA_NAT_REG_PROTO_MAX]) {
			priv->sreg_proto_max =
				nft_parse_register(tb[NFTA_NAT_REG_PROTO_MAX]);

			err = nft_validate_register_load(priv->sreg_proto_max,
							 plen);
			if (err < 0)
				return err;
		} else {
			priv->sreg_proto_max = priv->sreg_proto_min;
		}

		priv->flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
	}

	if (tb[NFTA_NAT_FLAGS]) {
		priv->flags |= ntohl(nla_get_be32(tb[NFTA_NAT_FLAGS]));
		if (priv->flags & ~NF_NAT_RANGE_MASK)
			return -EOPNOTSUPP;
	}

	return nf_ct_netns_get(ctx->net, family);
}

static int nft_nat_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_nat *priv = nft_expr_priv(expr);

	switch (priv->type) {
	case NF_NAT_MANIP_SRC:
		if (nla_put_be32(skb, NFTA_NAT_TYPE, htonl(NFT_NAT_SNAT)))
			goto nla_put_failure;
		break;
	case NF_NAT_MANIP_DST:
		if (nla_put_be32(skb, NFTA_NAT_TYPE, htonl(NFT_NAT_DNAT)))
			goto nla_put_failure;
		break;
	}

	if (nla_put_be32(skb, NFTA_NAT_FAMILY, htonl(priv->family)))
		goto nla_put_failure;

	if (priv->sreg_addr_min) {
		if (nft_dump_register(skb, NFTA_NAT_REG_ADDR_MIN,
				      priv->sreg_addr_min) ||
		    nft_dump_register(skb, NFTA_NAT_REG_ADDR_MAX,
				      priv->sreg_addr_max))
			goto nla_put_failure;
	}

	if (priv->sreg_proto_min) {
		if (nft_dump_register(skb, NFTA_NAT_REG_PROTO_MIN,
				      priv->sreg_proto_min) ||
		    nft_dump_register(skb, NFTA_NAT_REG_PROTO_MAX,
				      priv->sreg_proto_max))
			goto nla_put_failure;
	}

	if (priv->flags != 0) {
		if (nla_put_be32(skb, NFTA_NAT_FLAGS, htonl(priv->flags)))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -1;
}

static void
nft_nat_destroy(const struct nft_ctx *ctx, const struct nft_expr *expr)
{
	const struct nft_nat *priv = nft_expr_priv(expr);

	nf_ct_netns_put(ctx->net, priv->family);
}

static struct nft_expr_type nft_nat_type;
static const struct nft_expr_ops nft_nat_ops = {
	.type           = &nft_nat_type,
	.size           = NFT_EXPR_SIZE(sizeof(struct nft_nat)),
	.eval           = nft_nat_eval,
	.init           = nft_nat_init,
	.destroy        = nft_nat_destroy,
	.dump           = nft_nat_dump,
	.validate	= nft_nat_validate,
};

static struct nft_expr_type nft_nat_type __read_mostly = {
	.name           = "nat",
	.ops            = &nft_nat_ops,
	.policy         = nft_nat_policy,
	.maxattr        = NFTA_NAT_MAX,
	.owner          = THIS_MODULE,
};

#ifdef CONFIG_NF_TABLES_INET
static void nft_nat_inet_eval(const struct nft_expr *expr,
			      struct nft_regs *regs,
			      const struct nft_pktinfo *pkt)
{
	const struct nft_nat *priv = nft_expr_priv(expr);

	if (priv->family == nft_pf(pkt))
		nft_nat_eval(expr, regs, pkt);
}

static const struct nft_expr_ops nft_nat_inet_ops = {
	.type           = &nft_nat_type,
	.size           = NFT_EXPR_SIZE(sizeof(struct nft_nat)),
	.eval           = nft_nat_inet_eval,
	.init           = nft_nat_init,
	.destroy        = nft_nat_destroy,
	.dump           = nft_nat_dump,
	.validate	= nft_nat_validate,
};

static struct nft_expr_type nft_inet_nat_type __read_mostly = {
	.name           = "nat",
	.family		= NFPROTO_INET,
	.ops            = &nft_nat_inet_ops,
	.policy         = nft_nat_policy,
	.maxattr        = NFTA_NAT_MAX,
	.owner          = THIS_MODULE,
};

static int nft_nat_inet_module_init(void)
{
	return nft_register_expr(&nft_inet_nat_type);
}

static void nft_nat_inet_module_exit(void)
{
	nft_unregister_expr(&nft_inet_nat_type);
}
#else
static int nft_nat_inet_module_init(void) { return 0; }
static void nft_nat_inet_module_exit(void) { }
#endif

static int __init nft_nat_module_init(void)
{
	int ret = nft_nat_inet_module_init();

	if (ret)
		return ret;

	ret = nft_register_expr(&nft_nat_type);
	if (ret)
		nft_nat_inet_module_exit();

	return ret;
}

static void __exit nft_nat_module_exit(void)
{
	nft_nat_inet_module_exit();
	nft_unregister_expr(&nft_nat_type);
}

module_init(nft_nat_module_init);
module_exit(nft_nat_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomasz Bursztyka <tomasz.bursztyka@linux.intel.com>");
MODULE_ALIAS_NFT_EXPR("nat");
