/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_labels.h>

struct nft_ct {
	enum nft_ct_keys	key:8;
	enum ip_conntrack_dir	dir:8;
	union {
		enum nft_registers	dreg:8;
		enum nft_registers	sreg:8;
	};
};

static void nft_ct_get_eval(const struct nft_expr *expr,
			    struct nft_data data[NFT_REG_MAX + 1],
			    const struct nft_pktinfo *pkt)
{
	const struct nft_ct *priv = nft_expr_priv(expr);
	struct nft_data *dest = &data[priv->dreg];
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	const struct nf_conn_help *help;
	const struct nf_conntrack_tuple *tuple;
	const struct nf_conntrack_helper *helper;
	long diff;
	unsigned int state;

	ct = nf_ct_get(pkt->skb, &ctinfo);

	switch (priv->key) {
	case NFT_CT_STATE:
		if (ct == NULL)
			state = NF_CT_STATE_INVALID_BIT;
		else if (nf_ct_is_untracked(ct))
			state = NF_CT_STATE_UNTRACKED_BIT;
		else
			state = NF_CT_STATE_BIT(ctinfo);
		dest->data[0] = state;
		return;
	default:
		break;
	}

	if (ct == NULL)
		goto err;

	switch (priv->key) {
	case NFT_CT_DIRECTION:
		dest->data[0] = CTINFO2DIR(ctinfo);
		return;
	case NFT_CT_STATUS:
		dest->data[0] = ct->status;
		return;
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
		dest->data[0] = ct->mark;
		return;
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	case NFT_CT_SECMARK:
		dest->data[0] = ct->secmark;
		return;
#endif
	case NFT_CT_EXPIRATION:
		diff = (long)jiffies - (long)ct->timeout.expires;
		if (diff < 0)
			diff = 0;
		dest->data[0] = jiffies_to_msecs(diff);
		return;
	case NFT_CT_HELPER:
		if (ct->master == NULL)
			goto err;
		help = nfct_help(ct->master);
		if (help == NULL)
			goto err;
		helper = rcu_dereference(help->helper);
		if (helper == NULL)
			goto err;
		if (strlen(helper->name) >= sizeof(dest->data))
			goto err;
		strncpy((char *)dest->data, helper->name, sizeof(dest->data));
		return;
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS: {
		struct nf_conn_labels *labels = nf_ct_labels_find(ct);
		unsigned int size;

		if (!labels) {
			memset(dest->data, 0, sizeof(dest->data));
			return;
		}

		BUILD_BUG_ON(NF_CT_LABELS_MAX_SIZE > sizeof(dest->data));
		size = labels->words * sizeof(long);

		memcpy(dest->data, labels->bits, size);
		if (size < sizeof(dest->data))
			memset(((char *) dest->data) + size, 0,
			       sizeof(dest->data) - size);
		return;
	}
#endif
	default:
		break;
	}

	tuple = &ct->tuplehash[priv->dir].tuple;
	switch (priv->key) {
	case NFT_CT_L3PROTOCOL:
		dest->data[0] = nf_ct_l3num(ct);
		return;
	case NFT_CT_SRC:
		memcpy(dest->data, tuple->src.u3.all,
		       nf_ct_l3num(ct) == NFPROTO_IPV4 ? 4 : 16);
		return;
	case NFT_CT_DST:
		memcpy(dest->data, tuple->dst.u3.all,
		       nf_ct_l3num(ct) == NFPROTO_IPV4 ? 4 : 16);
		return;
	case NFT_CT_PROTOCOL:
		dest->data[0] = nf_ct_protonum(ct);
		return;
	case NFT_CT_PROTO_SRC:
		dest->data[0] = (__force __u16)tuple->src.u.all;
		return;
	case NFT_CT_PROTO_DST:
		dest->data[0] = (__force __u16)tuple->dst.u.all;
		return;
	default:
		break;
	}
	return;
err:
	data[NFT_REG_VERDICT].verdict = NFT_BREAK;
}

static void nft_ct_set_eval(const struct nft_expr *expr,
			    struct nft_data data[NFT_REG_MAX + 1],
			    const struct nft_pktinfo *pkt)
{
	const struct nft_ct *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
#ifdef CONFIG_NF_CONNTRACK_MARK
	u32 value = data[priv->sreg].data[0];
#endif
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct == NULL)
		return;

	switch (priv->key) {
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
		if (ct->mark != value) {
			ct->mark = value;
			nf_conntrack_event_cache(IPCT_MARK, ct);
		}
		break;
#endif
	default:
		break;
	}
}

static const struct nla_policy nft_ct_policy[NFTA_CT_MAX + 1] = {
	[NFTA_CT_DREG]		= { .type = NLA_U32 },
	[NFTA_CT_KEY]		= { .type = NLA_U32 },
	[NFTA_CT_DIRECTION]	= { .type = NLA_U8 },
	[NFTA_CT_SREG]		= { .type = NLA_U32 },
};

static int nft_ct_l3proto_try_module_get(uint8_t family)
{
	int err;

	if (family == NFPROTO_INET) {
		err = nf_ct_l3proto_try_module_get(NFPROTO_IPV4);
		if (err < 0)
			goto err1;
		err = nf_ct_l3proto_try_module_get(NFPROTO_IPV6);
		if (err < 0)
			goto err2;
	} else {
		err = nf_ct_l3proto_try_module_get(family);
		if (err < 0)
			goto err1;
	}
	return 0;

err2:
	nf_ct_l3proto_module_put(NFPROTO_IPV4);
err1:
	return err;
}

static void nft_ct_l3proto_module_put(uint8_t family)
{
	if (family == NFPROTO_INET) {
		nf_ct_l3proto_module_put(NFPROTO_IPV4);
		nf_ct_l3proto_module_put(NFPROTO_IPV6);
	} else
		nf_ct_l3proto_module_put(family);
}

static int nft_ct_get_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_ct *priv = nft_expr_priv(expr);
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_CT_KEY]));
	switch (priv->key) {
	case NFT_CT_STATE:
	case NFT_CT_DIRECTION:
	case NFT_CT_STATUS:
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	case NFT_CT_SECMARK:
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS:
#endif
	case NFT_CT_EXPIRATION:
	case NFT_CT_HELPER:
		if (tb[NFTA_CT_DIRECTION] != NULL)
			return -EINVAL;
		break;
	case NFT_CT_L3PROTOCOL:
	case NFT_CT_PROTOCOL:
	case NFT_CT_SRC:
	case NFT_CT_DST:
	case NFT_CT_PROTO_SRC:
	case NFT_CT_PROTO_DST:
		if (tb[NFTA_CT_DIRECTION] == NULL)
			return -EINVAL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (tb[NFTA_CT_DIRECTION] != NULL) {
		priv->dir = nla_get_u8(tb[NFTA_CT_DIRECTION]);
		switch (priv->dir) {
		case IP_CT_DIR_ORIGINAL:
		case IP_CT_DIR_REPLY:
			break;
		default:
			return -EINVAL;
		}
	}

	priv->dreg = ntohl(nla_get_be32(tb[NFTA_CT_DREG]));
	err = nft_validate_output_register(priv->dreg);
	if (err < 0)
		return err;

	err = nft_validate_data_load(ctx, priv->dreg, NULL, NFT_DATA_VALUE);
	if (err < 0)
		return err;

	err = nft_ct_l3proto_try_module_get(ctx->afi->family);
	if (err < 0)
		return err;

	return 0;
}

static int nft_ct_set_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_ct *priv = nft_expr_priv(expr);
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_CT_KEY]));
	switch (priv->key) {
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
		break;
#endif
	default:
		return -EOPNOTSUPP;
	}

	priv->sreg = ntohl(nla_get_be32(tb[NFTA_CT_SREG]));
	err = nft_validate_input_register(priv->sreg);
	if (err < 0)
		return err;

	err = nft_ct_l3proto_try_module_get(ctx->afi->family);
	if (err < 0)
		return err;

	return 0;
}

static void nft_ct_destroy(const struct nft_ctx *ctx,
			   const struct nft_expr *expr)
{
	nft_ct_l3proto_module_put(ctx->afi->family);
}

static int nft_ct_get_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_ct *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_CT_DREG, htonl(priv->dreg)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_CT_KEY, htonl(priv->key)))
		goto nla_put_failure;

	switch (priv->key) {
	case NFT_CT_PROTOCOL:
	case NFT_CT_SRC:
	case NFT_CT_DST:
	case NFT_CT_PROTO_SRC:
	case NFT_CT_PROTO_DST:
		if (nla_put_u8(skb, NFTA_CT_DIRECTION, priv->dir))
			goto nla_put_failure;
	default:
		break;
	}

	return 0;

nla_put_failure:
	return -1;
}

static int nft_ct_set_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_ct *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_CT_SREG, htonl(priv->sreg)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_CT_KEY, htonl(priv->key)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_ct_type;
static const struct nft_expr_ops nft_ct_get_ops = {
	.type		= &nft_ct_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_ct)),
	.eval		= nft_ct_get_eval,
	.init		= nft_ct_get_init,
	.destroy	= nft_ct_destroy,
	.dump		= nft_ct_get_dump,
};

static const struct nft_expr_ops nft_ct_set_ops = {
	.type		= &nft_ct_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_ct)),
	.eval		= nft_ct_set_eval,
	.init		= nft_ct_set_init,
	.destroy	= nft_ct_destroy,
	.dump		= nft_ct_set_dump,
};

static const struct nft_expr_ops *
nft_ct_select_ops(const struct nft_ctx *ctx,
		    const struct nlattr * const tb[])
{
	if (tb[NFTA_CT_KEY] == NULL)
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_CT_DREG] && tb[NFTA_CT_SREG])
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_CT_DREG])
		return &nft_ct_get_ops;

	if (tb[NFTA_CT_SREG])
		return &nft_ct_set_ops;

	return ERR_PTR(-EINVAL);
}

static struct nft_expr_type nft_ct_type __read_mostly = {
	.name		= "ct",
	.select_ops	= &nft_ct_select_ops,
	.policy		= nft_ct_policy,
	.maxattr	= NFTA_CT_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_ct_module_init(void)
{
	return nft_register_expr(&nft_ct_type);
}

static void __exit nft_ct_module_exit(void)
{
	nft_unregister_expr(&nft_ct_type);
}

module_init(nft_ct_module_init);
module_exit(nft_ct_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("ct");
