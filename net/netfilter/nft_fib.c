/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Generic part shared by ipv4 and ipv6 backends.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_fib.h>

const struct nla_policy nft_fib_policy[NFTA_FIB_MAX + 1] = {
	[NFTA_FIB_DREG]		= { .type = NLA_U32 },
	[NFTA_FIB_RESULT]	= { .type = NLA_U32 },
	[NFTA_FIB_FLAGS]	= { .type = NLA_U32 },
};
EXPORT_SYMBOL(nft_fib_policy);

#define NFTA_FIB_F_ALL (NFTA_FIB_F_SADDR | NFTA_FIB_F_DADDR | \
			NFTA_FIB_F_MARK | NFTA_FIB_F_IIF | NFTA_FIB_F_OIF)

int nft_fib_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
		     const struct nft_data **data)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	unsigned int hooks;

	switch (priv->result) {
	case NFT_FIB_RESULT_OIF: /* fallthrough */
	case NFT_FIB_RESULT_OIFNAME:
		hooks = (1 << NF_INET_PRE_ROUTING);
		break;
	case NFT_FIB_RESULT_ADDRTYPE:
		if (priv->flags & NFTA_FIB_F_IIF)
			hooks = (1 << NF_INET_PRE_ROUTING) |
				(1 << NF_INET_LOCAL_IN) |
				(1 << NF_INET_FORWARD);
		else if (priv->flags & NFTA_FIB_F_OIF)
			hooks = (1 << NF_INET_LOCAL_OUT) |
				(1 << NF_INET_POST_ROUTING) |
				(1 << NF_INET_FORWARD);
		else
			hooks = (1 << NF_INET_LOCAL_IN) |
				(1 << NF_INET_LOCAL_OUT) |
				(1 << NF_INET_FORWARD) |
				(1 << NF_INET_PRE_ROUTING) |
				(1 << NF_INET_POST_ROUTING);

		break;
	default:
		return -EINVAL;
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
}
EXPORT_SYMBOL_GPL(nft_fib_validate);

int nft_fib_init(const struct nft_ctx *ctx, const struct nft_expr *expr,
		 const struct nlattr * const tb[])
{
	struct nft_fib *priv = nft_expr_priv(expr);
	unsigned int len;
	int err;

	if (!tb[NFTA_FIB_DREG] || !tb[NFTA_FIB_RESULT] || !tb[NFTA_FIB_FLAGS])
		return -EINVAL;

	priv->flags = ntohl(nla_get_be32(tb[NFTA_FIB_FLAGS]));

	if (priv->flags == 0 || (priv->flags & ~NFTA_FIB_F_ALL))
		return -EINVAL;

	if ((priv->flags & (NFTA_FIB_F_SADDR | NFTA_FIB_F_DADDR)) ==
			   (NFTA_FIB_F_SADDR | NFTA_FIB_F_DADDR))
		return -EINVAL;
	if ((priv->flags & (NFTA_FIB_F_IIF | NFTA_FIB_F_OIF)) ==
			   (NFTA_FIB_F_IIF | NFTA_FIB_F_OIF))
		return -EINVAL;
	if ((priv->flags & (NFTA_FIB_F_SADDR | NFTA_FIB_F_DADDR)) == 0)
		return -EINVAL;

	priv->result = htonl(nla_get_be32(tb[NFTA_FIB_RESULT]));
	priv->dreg = nft_parse_register(tb[NFTA_FIB_DREG]);

	switch (priv->result) {
	case NFT_FIB_RESULT_OIF:
		if (priv->flags & NFTA_FIB_F_OIF)
			return -EINVAL;
		len = sizeof(int);
		break;
	case NFT_FIB_RESULT_OIFNAME:
		if (priv->flags & NFTA_FIB_F_OIF)
			return -EINVAL;
		len = IFNAMSIZ;
		break;
	case NFT_FIB_RESULT_ADDRTYPE:
		len = sizeof(u32);
		break;
	default:
		return -EINVAL;
	}

	err = nft_validate_register_store(ctx, priv->dreg, NULL,
					  NFT_DATA_VALUE, len);
	if (err < 0)
		return err;

	return nft_fib_validate(ctx, expr, NULL);
}
EXPORT_SYMBOL_GPL(nft_fib_init);

int nft_fib_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_fib *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_FIB_DREG, priv->dreg))
		return -1;

	if (nla_put_be32(skb, NFTA_FIB_RESULT, htonl(priv->result)))
		return -1;

	if (nla_put_be32(skb, NFTA_FIB_FLAGS, htonl(priv->flags)))
		return -1;

	return 0;
}
EXPORT_SYMBOL_GPL(nft_fib_dump);

void nft_fib_store_result(void *reg, enum nft_fib_result r,
			  const struct nft_pktinfo *pkt, int index)
{
	struct net_device *dev;
	u32 *dreg = reg;

	switch (r) {
	case NFT_FIB_RESULT_OIF:
		*dreg = index;
		break;
	case NFT_FIB_RESULT_OIFNAME:
		dev = dev_get_by_index_rcu(nft_net(pkt), index);
		strncpy(reg, dev ? dev->name : "", IFNAMSIZ);
		break;
	default:
		WARN_ON_ONCE(1);
		*dreg = 0;
		break;
	}
}
EXPORT_SYMBOL_GPL(nft_fib_store_result);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
