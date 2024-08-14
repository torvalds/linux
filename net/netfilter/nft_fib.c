// SPDX-License-Identifier: GPL-2.0-only
/*
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
			NFTA_FIB_F_MARK | NFTA_FIB_F_IIF | NFTA_FIB_F_OIF | \
			NFTA_FIB_F_PRESENT)

int nft_fib_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
		     const struct nft_data **data)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	unsigned int hooks;

	switch (priv->result) {
	case NFT_FIB_RESULT_OIF:
	case NFT_FIB_RESULT_OIFNAME:
		hooks = (1 << NF_INET_PRE_ROUTING) |
			(1 << NF_INET_LOCAL_IN) |
			(1 << NF_INET_FORWARD);
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

	priv->result = ntohl(nla_get_be32(tb[NFTA_FIB_RESULT]));

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

	err = nft_parse_register_store(ctx, tb[NFTA_FIB_DREG], &priv->dreg,
				       NULL, NFT_DATA_VALUE, len);
	if (err < 0)
		return err;

	return 0;
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

void nft_fib_store_result(void *reg, const struct nft_fib *priv,
			  const struct net_device *dev)
{
	u32 *dreg = reg;
	int index;

	switch (priv->result) {
	case NFT_FIB_RESULT_OIF:
		index = dev ? dev->ifindex : 0;
		if (priv->flags & NFTA_FIB_F_PRESENT)
			nft_reg_store8(dreg, !!index);
		else
			*dreg = index;

		break;
	case NFT_FIB_RESULT_OIFNAME:
		if (priv->flags & NFTA_FIB_F_PRESENT)
			nft_reg_store8(dreg, !!dev);
		else
			strncpy(reg, dev ? dev->name : "", IFNAMSIZ);
		break;
	default:
		WARN_ON_ONCE(1);
		*dreg = 0;
		break;
	}
}
EXPORT_SYMBOL_GPL(nft_fib_store_result);

bool nft_fib_reduce(struct nft_regs_track *track,
		    const struct nft_expr *expr)
{
	const struct nft_fib *priv = nft_expr_priv(expr);
	unsigned int len = NFT_REG32_SIZE;
	const struct nft_fib *fib;

	switch (priv->result) {
	case NFT_FIB_RESULT_OIF:
		break;
	case NFT_FIB_RESULT_OIFNAME:
		if (priv->flags & NFTA_FIB_F_PRESENT)
			len = NFT_REG32_SIZE;
		else
			len = IFNAMSIZ;
		break;
	case NFT_FIB_RESULT_ADDRTYPE:
	     break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (!nft_reg_track_cmp(track, expr, priv->dreg)) {
		nft_reg_track_update(track, expr, priv->dreg, len);
		return false;
	}

	fib = nft_expr_priv(track->regs[priv->dreg].selector);
	if (priv->result != fib->result ||
	    priv->flags != fib->flags) {
		nft_reg_track_update(track, expr, priv->dreg, len);
		return false;
	}

	if (!track->regs[priv->dreg].bitwise)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(nft_fib_reduce);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
