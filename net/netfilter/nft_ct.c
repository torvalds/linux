/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2016 Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <net/netfilter/nf_conntrack_acct.h>
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

static u64 nft_ct_get_eval_counter(const struct nf_conn_counter *c,
				   enum nft_ct_keys k,
				   enum ip_conntrack_dir d)
{
	if (d < IP_CT_DIR_MAX)
		return k == NFT_CT_BYTES ? atomic64_read(&c[d].bytes) :
					   atomic64_read(&c[d].packets);

	return nft_ct_get_eval_counter(c, k, IP_CT_DIR_ORIGINAL) +
	       nft_ct_get_eval_counter(c, k, IP_CT_DIR_REPLY);
}

static void nft_ct_get_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	const struct nft_ct *priv = nft_expr_priv(expr);
	u32 *dest = &regs->data[priv->dreg];
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	const struct nf_conn_help *help;
	const struct nf_conntrack_tuple *tuple;
	const struct nf_conntrack_helper *helper;
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
		*dest = state;
		return;
	default:
		break;
	}

	if (ct == NULL)
		goto err;

	switch (priv->key) {
	case NFT_CT_DIRECTION:
		*dest = CTINFO2DIR(ctinfo);
		return;
	case NFT_CT_STATUS:
		*dest = ct->status;
		return;
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
		*dest = ct->mark;
		return;
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	case NFT_CT_SECMARK:
		*dest = ct->secmark;
		return;
#endif
	case NFT_CT_EXPIRATION:
		*dest = jiffies_to_msecs(nf_ct_expires(ct));
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
		strncpy((char *)dest, helper->name, NF_CT_HELPER_NAME_LEN);
		return;
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS: {
		struct nf_conn_labels *labels = nf_ct_labels_find(ct);

		if (labels)
			memcpy(dest, labels->bits, NF_CT_LABELS_MAX_SIZE);
		else
			memset(dest, 0, NF_CT_LABELS_MAX_SIZE);
		return;
	}
#endif
	case NFT_CT_BYTES: /* fallthrough */
	case NFT_CT_PKTS: {
		const struct nf_conn_acct *acct = nf_conn_acct_find(ct);
		u64 count = 0;

		if (acct)
			count = nft_ct_get_eval_counter(acct->counter,
							priv->key, priv->dir);
		memcpy(dest, &count, sizeof(count));
		return;
	}
	case NFT_CT_L3PROTOCOL:
		*dest = nf_ct_l3num(ct);
		return;
	case NFT_CT_PROTOCOL:
		*dest = nf_ct_protonum(ct);
		return;
	default:
		break;
	}

	tuple = &ct->tuplehash[priv->dir].tuple;
	switch (priv->key) {
	case NFT_CT_SRC:
		memcpy(dest, tuple->src.u3.all,
		       nf_ct_l3num(ct) == NFPROTO_IPV4 ? 4 : 16);
		return;
	case NFT_CT_DST:
		memcpy(dest, tuple->dst.u3.all,
		       nf_ct_l3num(ct) == NFPROTO_IPV4 ? 4 : 16);
		return;
	case NFT_CT_PROTO_SRC:
		*dest = (__force __u16)tuple->src.u.all;
		return;
	case NFT_CT_PROTO_DST:
		*dest = (__force __u16)tuple->dst.u.all;
		return;
	default:
		break;
	}
	return;
err:
	regs->verdict.code = NFT_BREAK;
}

static void nft_ct_set_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	const struct nft_ct *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
#ifdef CONFIG_NF_CONNTRACK_MARK
	u32 value = regs->data[priv->sreg];
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
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS:
		nf_connlabels_replace(ct,
				      &regs->data[priv->sreg],
				      &regs->data[priv->sreg],
				      NF_CT_LABELS_MAX_SIZE / sizeof(u32));
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
	unsigned int len;
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_CT_KEY]));
	switch (priv->key) {
	case NFT_CT_DIRECTION:
		if (tb[NFTA_CT_DIRECTION] != NULL)
			return -EINVAL;
		len = sizeof(u8);
		break;
	case NFT_CT_STATE:
	case NFT_CT_STATUS:
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	case NFT_CT_SECMARK:
#endif
	case NFT_CT_EXPIRATION:
		if (tb[NFTA_CT_DIRECTION] != NULL)
			return -EINVAL;
		len = sizeof(u32);
		break;
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS:
		if (tb[NFTA_CT_DIRECTION] != NULL)
			return -EINVAL;
		len = NF_CT_LABELS_MAX_SIZE;
		break;
#endif
	case NFT_CT_HELPER:
		if (tb[NFTA_CT_DIRECTION] != NULL)
			return -EINVAL;
		len = NF_CT_HELPER_NAME_LEN;
		break;

	case NFT_CT_L3PROTOCOL:
	case NFT_CT_PROTOCOL:
		/* For compatibility, do not report error if NFTA_CT_DIRECTION
		 * attribute is specified.
		 */
		len = sizeof(u8);
		break;
	case NFT_CT_SRC:
	case NFT_CT_DST:
		if (tb[NFTA_CT_DIRECTION] == NULL)
			return -EINVAL;

		switch (ctx->afi->family) {
		case NFPROTO_IPV4:
			len = FIELD_SIZEOF(struct nf_conntrack_tuple,
					   src.u3.ip);
			break;
		case NFPROTO_IPV6:
		case NFPROTO_INET:
			len = FIELD_SIZEOF(struct nf_conntrack_tuple,
					   src.u3.ip6);
			break;
		default:
			return -EAFNOSUPPORT;
		}
		break;
	case NFT_CT_PROTO_SRC:
	case NFT_CT_PROTO_DST:
		if (tb[NFTA_CT_DIRECTION] == NULL)
			return -EINVAL;
		len = FIELD_SIZEOF(struct nf_conntrack_tuple, src.u.all);
		break;
	case NFT_CT_BYTES:
	case NFT_CT_PKTS:
		/* no direction? return sum of original + reply */
		if (tb[NFTA_CT_DIRECTION] == NULL)
			priv->dir = IP_CT_DIR_MAX;
		len = sizeof(u64);
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

	priv->dreg = nft_parse_register(tb[NFTA_CT_DREG]);
	err = nft_validate_register_store(ctx, priv->dreg, NULL,
					  NFT_DATA_VALUE, len);
	if (err < 0)
		return err;

	err = nft_ct_l3proto_try_module_get(ctx->afi->family);
	if (err < 0)
		return err;

	if (priv->key == NFT_CT_BYTES || priv->key == NFT_CT_PKTS)
		nf_ct_set_acct(ctx->net, true);

	return 0;
}

static int nft_ct_set_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_ct *priv = nft_expr_priv(expr);
	bool label_got = false;
	unsigned int len;
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_CT_KEY]));
	switch (priv->key) {
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
		if (tb[NFTA_CT_DIRECTION])
			return -EINVAL;
		len = FIELD_SIZEOF(struct nf_conn, mark);
		break;
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS:
		if (tb[NFTA_CT_DIRECTION])
			return -EINVAL;
		len = NF_CT_LABELS_MAX_SIZE;
		err = nf_connlabels_get(ctx->net, (len * BITS_PER_BYTE) - 1);
		if (err)
			return err;
		label_got = true;
		break;
#endif
	default:
		return -EOPNOTSUPP;
	}

	priv->sreg = nft_parse_register(tb[NFTA_CT_SREG]);
	err = nft_validate_register_load(priv->sreg, len);
	if (err < 0)
		goto err1;

	err = nft_ct_l3proto_try_module_get(ctx->afi->family);
	if (err < 0)
		goto err1;

	return 0;

err1:
	if (label_got)
		nf_connlabels_put(ctx->net);
	return err;
}

static void nft_ct_get_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	nft_ct_l3proto_module_put(ctx->afi->family);
}

static void nft_ct_set_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	struct nft_ct *priv = nft_expr_priv(expr);

	switch (priv->key) {
#ifdef CONFIG_NF_CONNTRACK_LABELS
	case NFT_CT_LABELS:
		nf_connlabels_put(ctx->net);
		break;
#endif
	default:
		break;
	}

	nft_ct_l3proto_module_put(ctx->afi->family);
}

static int nft_ct_get_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_ct *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_CT_DREG, priv->dreg))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_CT_KEY, htonl(priv->key)))
		goto nla_put_failure;

	switch (priv->key) {
	case NFT_CT_SRC:
	case NFT_CT_DST:
	case NFT_CT_PROTO_SRC:
	case NFT_CT_PROTO_DST:
		if (nla_put_u8(skb, NFTA_CT_DIRECTION, priv->dir))
			goto nla_put_failure;
		break;
	case NFT_CT_BYTES:
	case NFT_CT_PKTS:
		if (priv->dir < IP_CT_DIR_MAX &&
		    nla_put_u8(skb, NFTA_CT_DIRECTION, priv->dir))
			goto nla_put_failure;
		break;
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

	if (nft_dump_register(skb, NFTA_CT_SREG, priv->sreg))
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
	.destroy	= nft_ct_get_destroy,
	.dump		= nft_ct_get_dump,
};

static const struct nft_expr_ops nft_ct_set_ops = {
	.type		= &nft_ct_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_ct)),
	.eval		= nft_ct_set_eval,
	.init		= nft_ct_set_init,
	.destroy	= nft_ct_set_destroy,
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

static void nft_notrack_eval(const struct nft_expr *expr,
			     struct nft_regs *regs,
			     const struct nft_pktinfo *pkt)
{
	struct sk_buff *skb = pkt->skb;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(pkt->skb, &ctinfo);
	/* Previously seen (loopback or untracked)?  Ignore. */
	if (ct)
		return;

	ct = nf_ct_untracked_get();
	atomic_inc(&ct->ct_general.use);
	skb->nfct = &ct->ct_general;
	skb->nfctinfo = IP_CT_NEW;
}

static struct nft_expr_type nft_notrack_type;
static const struct nft_expr_ops nft_notrack_ops = {
	.type		= &nft_notrack_type,
	.size		= NFT_EXPR_SIZE(0),
	.eval		= nft_notrack_eval,
};

static struct nft_expr_type nft_notrack_type __read_mostly = {
	.name		= "notrack",
	.ops		= &nft_notrack_ops,
	.owner		= THIS_MODULE,
};

static int __init nft_ct_module_init(void)
{
	int err;

	BUILD_BUG_ON(NF_CT_LABELS_MAX_SIZE > NFT_REG_SIZE);

	err = nft_register_expr(&nft_ct_type);
	if (err < 0)
		return err;

	err = nft_register_expr(&nft_notrack_type);
	if (err < 0)
		goto err1;

	return 0;
err1:
	nft_unregister_expr(&nft_ct_type);
	return err;
}

static void __exit nft_ct_module_exit(void)
{
	nft_unregister_expr(&nft_notrack_type);
	nft_unregister_expr(&nft_ct_type);
}

module_init(nft_ct_module_init);
module_exit(nft_ct_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("ct");
MODULE_ALIAS_NFT_EXPR("notrack");
