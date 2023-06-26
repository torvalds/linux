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
#include <linux/in.h>
#include <net/xfrm.h>

static const struct nla_policy nft_xfrm_policy[NFTA_XFRM_MAX + 1] = {
	[NFTA_XFRM_KEY]		= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_XFRM_DIR]		= { .type = NLA_U8 },
	[NFTA_XFRM_SPNUM]	= NLA_POLICY_MAX(NLA_BE32, 255),
	[NFTA_XFRM_DREG]	= { .type = NLA_U32 },
};

struct nft_xfrm {
	enum nft_xfrm_keys	key:8;
	u8			dreg;
	u8			dir;
	u8			spnum;
	u8			len;
};

static int nft_xfrm_get_init(const struct nft_ctx *ctx,
			     const struct nft_expr *expr,
			     const struct nlattr * const tb[])
{
	struct nft_xfrm *priv = nft_expr_priv(expr);
	unsigned int len = 0;
	u32 spnum = 0;
	u8 dir;

	if (!tb[NFTA_XFRM_KEY] || !tb[NFTA_XFRM_DIR] || !tb[NFTA_XFRM_DREG])
		return -EINVAL;

	switch (ctx->family) {
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
	case NFPROTO_INET:
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->key = ntohl(nla_get_be32(tb[NFTA_XFRM_KEY]));
	switch (priv->key) {
	case NFT_XFRM_KEY_REQID:
	case NFT_XFRM_KEY_SPI:
		len = sizeof(u32);
		break;
	case NFT_XFRM_KEY_DADDR_IP4:
	case NFT_XFRM_KEY_SADDR_IP4:
		len = sizeof(struct in_addr);
		break;
	case NFT_XFRM_KEY_DADDR_IP6:
	case NFT_XFRM_KEY_SADDR_IP6:
		len = sizeof(struct in6_addr);
		break;
	default:
		return -EINVAL;
	}

	dir = nla_get_u8(tb[NFTA_XFRM_DIR]);
	switch (dir) {
	case XFRM_POLICY_IN:
	case XFRM_POLICY_OUT:
		priv->dir = dir;
		break;
	default:
		return -EINVAL;
	}

	if (tb[NFTA_XFRM_SPNUM])
		spnum = ntohl(nla_get_be32(tb[NFTA_XFRM_SPNUM]));

	if (spnum >= XFRM_MAX_DEPTH)
		return -ERANGE;

	priv->spnum = spnum;

	priv->len = len;
	return nft_parse_register_store(ctx, tb[NFTA_XFRM_DREG], &priv->dreg,
					NULL, NFT_DATA_VALUE, len);
}

/* Return true if key asks for daddr/saddr and current
 * state does have a valid address (BEET, TUNNEL).
 */
static bool xfrm_state_addr_ok(enum nft_xfrm_keys k, u8 family, u8 mode)
{
	switch (k) {
	case NFT_XFRM_KEY_DADDR_IP4:
	case NFT_XFRM_KEY_SADDR_IP4:
		if (family == NFPROTO_IPV4)
			break;
		return false;
	case NFT_XFRM_KEY_DADDR_IP6:
	case NFT_XFRM_KEY_SADDR_IP6:
		if (family == NFPROTO_IPV6)
			break;
		return false;
	default:
		return true;
	}

	return mode == XFRM_MODE_BEET || mode == XFRM_MODE_TUNNEL;
}

static void nft_xfrm_state_get_key(const struct nft_xfrm *priv,
				   struct nft_regs *regs,
				   const struct xfrm_state *state)
{
	u32 *dest = &regs->data[priv->dreg];

	if (!xfrm_state_addr_ok(priv->key,
				state->props.family,
				state->props.mode)) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	switch (priv->key) {
	case NFT_XFRM_KEY_UNSPEC:
	case __NFT_XFRM_KEY_MAX:
		WARN_ON_ONCE(1);
		break;
	case NFT_XFRM_KEY_DADDR_IP4:
		*dest = (__force __u32)state->id.daddr.a4;
		return;
	case NFT_XFRM_KEY_DADDR_IP6:
		memcpy(dest, &state->id.daddr.in6, sizeof(struct in6_addr));
		return;
	case NFT_XFRM_KEY_SADDR_IP4:
		*dest = (__force __u32)state->props.saddr.a4;
		return;
	case NFT_XFRM_KEY_SADDR_IP6:
		memcpy(dest, &state->props.saddr.in6, sizeof(struct in6_addr));
		return;
	case NFT_XFRM_KEY_REQID:
		*dest = state->props.reqid;
		return;
	case NFT_XFRM_KEY_SPI:
		*dest = (__force __u32)state->id.spi;
		return;
	}

	regs->verdict.code = NFT_BREAK;
}

static void nft_xfrm_get_eval_in(const struct nft_xfrm *priv,
				    struct nft_regs *regs,
				    const struct nft_pktinfo *pkt)
{
	const struct sec_path *sp = skb_sec_path(pkt->skb);
	const struct xfrm_state *state;

	if (sp == NULL || sp->len <= priv->spnum) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	state = sp->xvec[priv->spnum];
	nft_xfrm_state_get_key(priv, regs, state);
}

static void nft_xfrm_get_eval_out(const struct nft_xfrm *priv,
				  struct nft_regs *regs,
				  const struct nft_pktinfo *pkt)
{
	const struct dst_entry *dst = skb_dst(pkt->skb);
	int i;

	for (i = 0; dst && dst->xfrm;
	     dst = ((const struct xfrm_dst *)dst)->child, i++) {
		if (i < priv->spnum)
			continue;

		nft_xfrm_state_get_key(priv, regs, dst->xfrm);
		return;
	}

	regs->verdict.code = NFT_BREAK;
}

static void nft_xfrm_get_eval(const struct nft_expr *expr,
			      struct nft_regs *regs,
			      const struct nft_pktinfo *pkt)
{
	const struct nft_xfrm *priv = nft_expr_priv(expr);

	switch (priv->dir) {
	case XFRM_POLICY_IN:
		nft_xfrm_get_eval_in(priv, regs, pkt);
		break;
	case XFRM_POLICY_OUT:
		nft_xfrm_get_eval_out(priv, regs, pkt);
		break;
	default:
		WARN_ON_ONCE(1);
		regs->verdict.code = NFT_BREAK;
		break;
	}
}

static int nft_xfrm_get_dump(struct sk_buff *skb,
			     const struct nft_expr *expr, bool reset)
{
	const struct nft_xfrm *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_XFRM_DREG, priv->dreg))
		return -1;

	if (nla_put_be32(skb, NFTA_XFRM_KEY, htonl(priv->key)))
		return -1;
	if (nla_put_u8(skb, NFTA_XFRM_DIR, priv->dir))
		return -1;
	if (nla_put_be32(skb, NFTA_XFRM_SPNUM, htonl(priv->spnum)))
		return -1;

	return 0;
}

static int nft_xfrm_validate(const struct nft_ctx *ctx, const struct nft_expr *expr,
			     const struct nft_data **data)
{
	const struct nft_xfrm *priv = nft_expr_priv(expr);
	unsigned int hooks;

	switch (priv->dir) {
	case XFRM_POLICY_IN:
		hooks = (1 << NF_INET_FORWARD) |
			(1 << NF_INET_LOCAL_IN) |
			(1 << NF_INET_PRE_ROUTING);
		break;
	case XFRM_POLICY_OUT:
		hooks = (1 << NF_INET_FORWARD) |
			(1 << NF_INET_LOCAL_OUT) |
			(1 << NF_INET_POST_ROUTING);
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
}

static bool nft_xfrm_reduce(struct nft_regs_track *track,
			    const struct nft_expr *expr)
{
	const struct nft_xfrm *priv = nft_expr_priv(expr);
	const struct nft_xfrm *xfrm;

	if (!nft_reg_track_cmp(track, expr, priv->dreg)) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	xfrm = nft_expr_priv(track->regs[priv->dreg].selector);
	if (priv->key != xfrm->key ||
	    priv->dreg != xfrm->dreg ||
	    priv->dir != xfrm->dir ||
	    priv->spnum != xfrm->spnum) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	if (!track->regs[priv->dreg].bitwise)
		return true;

	return nft_expr_reduce_bitwise(track, expr);
}

static struct nft_expr_type nft_xfrm_type;
static const struct nft_expr_ops nft_xfrm_get_ops = {
	.type		= &nft_xfrm_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_xfrm)),
	.eval		= nft_xfrm_get_eval,
	.init		= nft_xfrm_get_init,
	.dump		= nft_xfrm_get_dump,
	.validate	= nft_xfrm_validate,
	.reduce		= nft_xfrm_reduce,
};

static struct nft_expr_type nft_xfrm_type __read_mostly = {
	.name		= "xfrm",
	.ops		= &nft_xfrm_get_ops,
	.policy		= nft_xfrm_policy,
	.maxattr	= NFTA_XFRM_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_xfrm_module_init(void)
{
	return nft_register_expr(&nft_xfrm_type);
}

static void __exit nft_xfrm_module_exit(void)
{
	nft_unregister_expr(&nft_xfrm_type);
}

module_init(nft_xfrm_module_init);
module_exit(nft_xfrm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("nf_tables: xfrm/IPSec matching");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_AUTHOR("Máté Eckl <ecklm94@gmail.com>");
MODULE_ALIAS_NFT_EXPR("xfrm");
