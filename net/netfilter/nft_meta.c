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
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/smp.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/tcp_states.h> /* for TCP_TIME_WAIT */
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nft_meta.h>

void nft_meta_get_eval(const struct nft_expr *expr,
		       struct nft_data data[NFT_REG_MAX + 1],
		       const struct nft_pktinfo *pkt)
{
	const struct nft_meta *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	const struct net_device *in = pkt->in, *out = pkt->out;
	struct nft_data *dest = &data[priv->dreg];

	switch (priv->key) {
	case NFT_META_LEN:
		dest->data[0] = skb->len;
		break;
	case NFT_META_PROTOCOL:
		*(__be16 *)dest->data = skb->protocol;
		break;
	case NFT_META_NFPROTO:
		dest->data[0] = pkt->ops->pf;
		break;
	case NFT_META_L4PROTO:
		dest->data[0] = pkt->tprot;
		break;
	case NFT_META_PRIORITY:
		dest->data[0] = skb->priority;
		break;
	case NFT_META_MARK:
		dest->data[0] = skb->mark;
		break;
	case NFT_META_IIF:
		if (in == NULL)
			goto err;
		dest->data[0] = in->ifindex;
		break;
	case NFT_META_OIF:
		if (out == NULL)
			goto err;
		dest->data[0] = out->ifindex;
		break;
	case NFT_META_IIFNAME:
		if (in == NULL)
			goto err;
		strncpy((char *)dest->data, in->name, sizeof(dest->data));
		break;
	case NFT_META_OIFNAME:
		if (out == NULL)
			goto err;
		strncpy((char *)dest->data, out->name, sizeof(dest->data));
		break;
	case NFT_META_IIFTYPE:
		if (in == NULL)
			goto err;
		*(u16 *)dest->data = in->type;
		break;
	case NFT_META_OIFTYPE:
		if (out == NULL)
			goto err;
		*(u16 *)dest->data = out->type;
		break;
	case NFT_META_SKUID:
		if (skb->sk == NULL || !sk_fullsock(skb->sk))
			goto err;

		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket == NULL ||
		    skb->sk->sk_socket->file == NULL) {
			read_unlock_bh(&skb->sk->sk_callback_lock);
			goto err;
		}

		dest->data[0] =
			from_kuid_munged(&init_user_ns,
				skb->sk->sk_socket->file->f_cred->fsuid);
		read_unlock_bh(&skb->sk->sk_callback_lock);
		break;
	case NFT_META_SKGID:
		if (skb->sk == NULL || !sk_fullsock(skb->sk))
			goto err;

		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket == NULL ||
		    skb->sk->sk_socket->file == NULL) {
			read_unlock_bh(&skb->sk->sk_callback_lock);
			goto err;
		}
		dest->data[0] =
			from_kgid_munged(&init_user_ns,
				 skb->sk->sk_socket->file->f_cred->fsgid);
		read_unlock_bh(&skb->sk->sk_callback_lock);
		break;
#ifdef CONFIG_IP_ROUTE_CLASSID
	case NFT_META_RTCLASSID: {
		const struct dst_entry *dst = skb_dst(skb);

		if (dst == NULL)
			goto err;
		dest->data[0] = dst->tclassid;
		break;
	}
#endif
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
		dest->data[0] = skb->secmark;
		break;
#endif
	case NFT_META_PKTTYPE:
		if (skb->pkt_type != PACKET_LOOPBACK) {
			dest->data[0] = skb->pkt_type;
			break;
		}

		switch (pkt->ops->pf) {
		case NFPROTO_IPV4:
			if (ipv4_is_multicast(ip_hdr(skb)->daddr))
				dest->data[0] = PACKET_MULTICAST;
			else
				dest->data[0] = PACKET_BROADCAST;
			break;
		case NFPROTO_IPV6:
			if (ipv6_hdr(skb)->daddr.s6_addr[0] == 0xFF)
				dest->data[0] = PACKET_MULTICAST;
			else
				dest->data[0] = PACKET_BROADCAST;
			break;
		default:
			WARN_ON(1);
			goto err;
		}
		break;
	case NFT_META_CPU:
		dest->data[0] = smp_processor_id();
		break;
	case NFT_META_IIFGROUP:
		if (in == NULL)
			goto err;
		dest->data[0] = in->group;
		break;
	case NFT_META_OIFGROUP:
		if (out == NULL)
			goto err;
		dest->data[0] = out->group;
		break;
	case NFT_META_CGROUP:
		if (skb->sk == NULL)
			break;

		dest->data[0] = skb->sk->sk_classid;
		break;
	default:
		WARN_ON(1);
		goto err;
	}
	return;

err:
	data[NFT_REG_VERDICT].verdict = NFT_BREAK;
}
EXPORT_SYMBOL_GPL(nft_meta_get_eval);

void nft_meta_set_eval(const struct nft_expr *expr,
		       struct nft_data data[NFT_REG_MAX + 1],
		       const struct nft_pktinfo *pkt)
{
	const struct nft_meta *meta = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	u32 value = data[meta->sreg].data[0];

	switch (meta->key) {
	case NFT_META_MARK:
		skb->mark = value;
		break;
	case NFT_META_PRIORITY:
		skb->priority = value;
		break;
	case NFT_META_NFTRACE:
		skb->nf_trace = 1;
		break;
	default:
		WARN_ON(1);
	}
}
EXPORT_SYMBOL_GPL(nft_meta_set_eval);

const struct nla_policy nft_meta_policy[NFTA_META_MAX + 1] = {
	[NFTA_META_DREG]	= { .type = NLA_U32 },
	[NFTA_META_KEY]		= { .type = NLA_U32 },
	[NFTA_META_SREG]	= { .type = NLA_U32 },
};
EXPORT_SYMBOL_GPL(nft_meta_policy);

int nft_meta_get_init(const struct nft_ctx *ctx,
		      const struct nft_expr *expr,
		      const struct nlattr * const tb[])
{
	struct nft_meta *priv = nft_expr_priv(expr);
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (priv->key) {
	case NFT_META_LEN:
	case NFT_META_PROTOCOL:
	case NFT_META_NFPROTO:
	case NFT_META_L4PROTO:
	case NFT_META_PRIORITY:
	case NFT_META_MARK:
	case NFT_META_IIF:
	case NFT_META_OIF:
	case NFT_META_IIFNAME:
	case NFT_META_OIFNAME:
	case NFT_META_IIFTYPE:
	case NFT_META_OIFTYPE:
	case NFT_META_SKUID:
	case NFT_META_SKGID:
#ifdef CONFIG_IP_ROUTE_CLASSID
	case NFT_META_RTCLASSID:
#endif
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
#endif
	case NFT_META_PKTTYPE:
	case NFT_META_CPU:
	case NFT_META_IIFGROUP:
	case NFT_META_OIFGROUP:
	case NFT_META_CGROUP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->dreg = ntohl(nla_get_be32(tb[NFTA_META_DREG]));
	err = nft_validate_output_register(priv->dreg);
	if (err < 0)
		return err;

	err = nft_validate_data_load(ctx, priv->dreg, NULL, NFT_DATA_VALUE);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(nft_meta_get_init);

int nft_meta_set_init(const struct nft_ctx *ctx,
		      const struct nft_expr *expr,
		      const struct nlattr * const tb[])
{
	struct nft_meta *priv = nft_expr_priv(expr);
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (priv->key) {
	case NFT_META_MARK:
	case NFT_META_PRIORITY:
	case NFT_META_NFTRACE:
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->sreg = ntohl(nla_get_be32(tb[NFTA_META_SREG]));
	err = nft_validate_input_register(priv->sreg);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(nft_meta_set_init);

int nft_meta_get_dump(struct sk_buff *skb,
		      const struct nft_expr *expr)
{
	const struct nft_meta *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_META_KEY, htonl(priv->key)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_META_DREG, htonl(priv->dreg)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_meta_get_dump);

int nft_meta_set_dump(struct sk_buff *skb,
		      const struct nft_expr *expr)
{
	const struct nft_meta *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_META_KEY, htonl(priv->key)))
		goto nla_put_failure;
	if (nla_put_be32(skb, NFTA_META_SREG, htonl(priv->sreg)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_meta_set_dump);

static struct nft_expr_type nft_meta_type;
static const struct nft_expr_ops nft_meta_get_ops = {
	.type		= &nft_meta_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.eval		= nft_meta_get_eval,
	.init		= nft_meta_get_init,
	.dump		= nft_meta_get_dump,
};

static const struct nft_expr_ops nft_meta_set_ops = {
	.type		= &nft_meta_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.eval		= nft_meta_set_eval,
	.init		= nft_meta_set_init,
	.dump		= nft_meta_set_dump,
};

static const struct nft_expr_ops *
nft_meta_select_ops(const struct nft_ctx *ctx,
		    const struct nlattr * const tb[])
{
	if (tb[NFTA_META_KEY] == NULL)
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_META_DREG] && tb[NFTA_META_SREG])
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_META_DREG])
		return &nft_meta_get_ops;

	if (tb[NFTA_META_SREG])
		return &nft_meta_set_ops;

	return ERR_PTR(-EINVAL);
}

static struct nft_expr_type nft_meta_type __read_mostly = {
	.name		= "meta",
	.select_ops	= &nft_meta_select_ops,
	.policy		= nft_meta_policy,
	.maxattr	= NFTA_META_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_meta_module_init(void)
{
	return nft_register_expr(&nft_meta_type);
}

static void __exit nft_meta_module_exit(void)
{
	nft_unregister_expr(&nft_meta_type);
}

module_init(nft_meta_module_init);
module_exit(nft_meta_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS_NFT_EXPR("meta");
