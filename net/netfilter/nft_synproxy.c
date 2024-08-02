// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/netlink.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#include <net/netfilter/nf_synproxy.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nf_synproxy.h>

struct nft_synproxy {
	struct nf_synproxy_info	info;
};

static const struct nla_policy nft_synproxy_policy[NFTA_SYNPROXY_MAX + 1] = {
	[NFTA_SYNPROXY_MSS]		= { .type = NLA_U16 },
	[NFTA_SYNPROXY_WSCALE]		= { .type = NLA_U8 },
	[NFTA_SYNPROXY_FLAGS]		= { .type = NLA_U32 },
};

static void nft_synproxy_tcp_options(struct synproxy_options *opts,
				     const struct tcphdr *tcp,
				     struct synproxy_net *snet,
				     struct nf_synproxy_info *info,
				     const struct nft_synproxy *priv)
{
	this_cpu_inc(snet->stats->syn_received);
	if (tcp->ece && tcp->cwr)
		opts->options |= NF_SYNPROXY_OPT_ECN;

	opts->options &= priv->info.options;
	opts->mss_encode = opts->mss_option;
	opts->mss_option = info->mss;
	if (opts->options & NF_SYNPROXY_OPT_TIMESTAMP)
		synproxy_init_timestamp_cookie(info, opts);
	else
		opts->options &= ~(NF_SYNPROXY_OPT_WSCALE |
				   NF_SYNPROXY_OPT_SACK_PERM |
				   NF_SYNPROXY_OPT_ECN);
}

static void nft_synproxy_eval_v4(const struct nft_synproxy *priv,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt,
				 const struct tcphdr *tcp,
				 struct tcphdr *_tcph,
				 struct synproxy_options *opts)
{
	struct nf_synproxy_info info = priv->info;
	struct net *net = nft_net(pkt);
	struct synproxy_net *snet = synproxy_pernet(net);
	struct sk_buff *skb = pkt->skb;

	if (tcp->syn) {
		/* Initial SYN from client */
		nft_synproxy_tcp_options(opts, tcp, snet, &info, priv);
		synproxy_send_client_synack(net, skb, tcp, opts);
		consume_skb(skb);
		regs->verdict.code = NF_STOLEN;
	} else if (tcp->ack) {
		/* ACK from client */
		if (synproxy_recv_client_ack(net, skb, tcp, opts,
					     ntohl(tcp->seq))) {
			consume_skb(skb);
			regs->verdict.code = NF_STOLEN;
		} else {
			regs->verdict.code = NF_DROP;
		}
	}
}

#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
static void nft_synproxy_eval_v6(const struct nft_synproxy *priv,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt,
				 const struct tcphdr *tcp,
				 struct tcphdr *_tcph,
				 struct synproxy_options *opts)
{
	struct nf_synproxy_info info = priv->info;
	struct net *net = nft_net(pkt);
	struct synproxy_net *snet = synproxy_pernet(net);
	struct sk_buff *skb = pkt->skb;

	if (tcp->syn) {
		/* Initial SYN from client */
		nft_synproxy_tcp_options(opts, tcp, snet, &info, priv);
		synproxy_send_client_synack_ipv6(net, skb, tcp, opts);
		consume_skb(skb);
		regs->verdict.code = NF_STOLEN;
	} else if (tcp->ack) {
		/* ACK from client */
		if (synproxy_recv_client_ack_ipv6(net, skb, tcp, opts,
						  ntohl(tcp->seq))) {
			consume_skb(skb);
			regs->verdict.code = NF_STOLEN;
		} else {
			regs->verdict.code = NF_DROP;
		}
	}
}
#endif /* CONFIG_NF_TABLES_IPV6*/

static void nft_synproxy_do_eval(const struct nft_synproxy *priv,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	struct synproxy_options opts = {};
	struct sk_buff *skb = pkt->skb;
	int thoff = nft_thoff(pkt);
	const struct tcphdr *tcp;
	struct tcphdr _tcph;

	if (pkt->tprot != IPPROTO_TCP) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	if (nf_ip_checksum(skb, nft_hook(pkt), thoff, IPPROTO_TCP)) {
		regs->verdict.code = NF_DROP;
		return;
	}

	tcp = skb_header_pointer(skb, thoff,
				 sizeof(struct tcphdr),
				 &_tcph);
	if (!tcp) {
		regs->verdict.code = NF_DROP;
		return;
	}

	if (!synproxy_parse_options(skb, thoff, tcp, &opts)) {
		regs->verdict.code = NF_DROP;
		return;
	}

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nft_synproxy_eval_v4(priv, regs, pkt, tcp, &_tcph, &opts);
		return;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case htons(ETH_P_IPV6):
		nft_synproxy_eval_v6(priv, regs, pkt, tcp, &_tcph, &opts);
		return;
#endif
	}
	regs->verdict.code = NFT_BREAK;
}

static int nft_synproxy_do_init(const struct nft_ctx *ctx,
				const struct nlattr * const tb[],
				struct nft_synproxy *priv)
{
	struct synproxy_net *snet = synproxy_pernet(ctx->net);
	u32 flags;
	int err;

	if (tb[NFTA_SYNPROXY_MSS])
		priv->info.mss = ntohs(nla_get_be16(tb[NFTA_SYNPROXY_MSS]));
	if (tb[NFTA_SYNPROXY_WSCALE])
		priv->info.wscale = nla_get_u8(tb[NFTA_SYNPROXY_WSCALE]);
	if (tb[NFTA_SYNPROXY_FLAGS]) {
		flags = ntohl(nla_get_be32(tb[NFTA_SYNPROXY_FLAGS]));
		if (flags & ~NF_SYNPROXY_OPT_MASK)
			return -EOPNOTSUPP;
		priv->info.options = flags;
	}

	err = nf_ct_netns_get(ctx->net, ctx->family);
	if (err)
		return err;

	switch (ctx->family) {
	case NFPROTO_IPV4:
		err = nf_synproxy_ipv4_init(snet, ctx->net);
		if (err)
			goto nf_ct_failure;
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		err = nf_synproxy_ipv6_init(snet, ctx->net);
		if (err)
			goto nf_ct_failure;
		break;
#endif
	case NFPROTO_INET:
		err = nf_synproxy_ipv4_init(snet, ctx->net);
		if (err)
			goto nf_ct_failure;
		err = nf_synproxy_ipv6_init(snet, ctx->net);
		if (err) {
			nf_synproxy_ipv4_fini(snet, ctx->net);
			goto nf_ct_failure;
		}
		break;
	}

	return 0;

nf_ct_failure:
	nf_ct_netns_put(ctx->net, ctx->family);
	return err;
}

static void nft_synproxy_do_destroy(const struct nft_ctx *ctx)
{
	struct synproxy_net *snet = synproxy_pernet(ctx->net);

	switch (ctx->family) {
	case NFPROTO_IPV4:
		nf_synproxy_ipv4_fini(snet, ctx->net);
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		nf_synproxy_ipv6_fini(snet, ctx->net);
		break;
#endif
	case NFPROTO_INET:
		nf_synproxy_ipv4_fini(snet, ctx->net);
		nf_synproxy_ipv6_fini(snet, ctx->net);
		break;
	}
	nf_ct_netns_put(ctx->net, ctx->family);
}

static int nft_synproxy_do_dump(struct sk_buff *skb, struct nft_synproxy *priv)
{
	if (nla_put_be16(skb, NFTA_SYNPROXY_MSS, htons(priv->info.mss)) ||
	    nla_put_u8(skb, NFTA_SYNPROXY_WSCALE, priv->info.wscale) ||
	    nla_put_be32(skb, NFTA_SYNPROXY_FLAGS, htonl(priv->info.options)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static void nft_synproxy_eval(const struct nft_expr *expr,
			      struct nft_regs *regs,
			      const struct nft_pktinfo *pkt)
{
	const struct nft_synproxy *priv = nft_expr_priv(expr);

	nft_synproxy_do_eval(priv, regs, pkt);
}

static int nft_synproxy_validate(const struct nft_ctx *ctx,
				 const struct nft_expr *expr,
				 const struct nft_data **data)
{
	if (ctx->family != NFPROTO_IPV4 &&
	    ctx->family != NFPROTO_IPV6 &&
	    ctx->family != NFPROTO_INET)
		return -EOPNOTSUPP;

	return nft_chain_validate_hooks(ctx->chain, (1 << NF_INET_LOCAL_IN) |
						    (1 << NF_INET_FORWARD));
}

static int nft_synproxy_init(const struct nft_ctx *ctx,
			     const struct nft_expr *expr,
			     const struct nlattr * const tb[])
{
	struct nft_synproxy *priv = nft_expr_priv(expr);

	return nft_synproxy_do_init(ctx, tb, priv);
}

static void nft_synproxy_destroy(const struct nft_ctx *ctx,
				 const struct nft_expr *expr)
{
	nft_synproxy_do_destroy(ctx);
}

static int nft_synproxy_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	struct nft_synproxy *priv = nft_expr_priv(expr);

	return nft_synproxy_do_dump(skb, priv);
}

static struct nft_expr_type nft_synproxy_type;
static const struct nft_expr_ops nft_synproxy_ops = {
	.eval		= nft_synproxy_eval,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_synproxy)),
	.init		= nft_synproxy_init,
	.destroy	= nft_synproxy_destroy,
	.dump		= nft_synproxy_dump,
	.type		= &nft_synproxy_type,
	.validate	= nft_synproxy_validate,
	.reduce		= NFT_REDUCE_READONLY,
};

static struct nft_expr_type nft_synproxy_type __read_mostly = {
	.ops		= &nft_synproxy_ops,
	.name		= "synproxy",
	.owner		= THIS_MODULE,
	.policy		= nft_synproxy_policy,
	.maxattr	= NFTA_SYNPROXY_MAX,
};

static int nft_synproxy_obj_init(const struct nft_ctx *ctx,
				 const struct nlattr * const tb[],
				 struct nft_object *obj)
{
	struct nft_synproxy *priv = nft_obj_data(obj);

	return nft_synproxy_do_init(ctx, tb, priv);
}

static void nft_synproxy_obj_destroy(const struct nft_ctx *ctx,
				     struct nft_object *obj)
{
	nft_synproxy_do_destroy(ctx);
}

static int nft_synproxy_obj_dump(struct sk_buff *skb,
				 struct nft_object *obj, bool reset)
{
	struct nft_synproxy *priv = nft_obj_data(obj);

	return nft_synproxy_do_dump(skb, priv);
}

static void nft_synproxy_obj_eval(struct nft_object *obj,
				  struct nft_regs *regs,
				  const struct nft_pktinfo *pkt)
{
	const struct nft_synproxy *priv = nft_obj_data(obj);

	nft_synproxy_do_eval(priv, regs, pkt);
}

static void nft_synproxy_obj_update(struct nft_object *obj,
				    struct nft_object *newobj)
{
	struct nft_synproxy *newpriv = nft_obj_data(newobj);
	struct nft_synproxy *priv = nft_obj_data(obj);

	priv->info = newpriv->info;
}

static struct nft_object_type nft_synproxy_obj_type;
static const struct nft_object_ops nft_synproxy_obj_ops = {
	.type		= &nft_synproxy_obj_type,
	.size		= sizeof(struct nft_synproxy),
	.init		= nft_synproxy_obj_init,
	.destroy	= nft_synproxy_obj_destroy,
	.dump		= nft_synproxy_obj_dump,
	.eval		= nft_synproxy_obj_eval,
	.update		= nft_synproxy_obj_update,
};

static struct nft_object_type nft_synproxy_obj_type __read_mostly = {
	.type		= NFT_OBJECT_SYNPROXY,
	.ops		= &nft_synproxy_obj_ops,
	.maxattr	= NFTA_SYNPROXY_MAX,
	.policy		= nft_synproxy_policy,
	.owner		= THIS_MODULE,
};

static int __init nft_synproxy_module_init(void)
{
	int err;

	err = nft_register_obj(&nft_synproxy_obj_type);
	if (err < 0)
		return err;

	err = nft_register_expr(&nft_synproxy_type);
	if (err < 0)
		goto err;

	return 0;

err:
	nft_unregister_obj(&nft_synproxy_obj_type);
	return err;
}

static void __exit nft_synproxy_module_exit(void)
{
	nft_unregister_expr(&nft_synproxy_type);
	nft_unregister_obj(&nft_synproxy_obj_type);
}

module_init(nft_synproxy_module_init);
module_exit(nft_synproxy_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fernando Fernandez <ffmancera@riseup.net>");
MODULE_ALIAS_NFT_EXPR("synproxy");
MODULE_ALIAS_NFT_OBJ(NFT_OBJECT_SYNPROXY);
MODULE_DESCRIPTION("nftables SYNPROXY expression support");
