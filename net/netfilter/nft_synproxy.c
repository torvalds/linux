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
				     struct nft_synproxy *priv)
{
	this_cpu_inc(snet->stats->syn_received);
	if (tcp->ece && tcp->cwr)
		opts->options |= NF_SYNPROXY_OPT_ECN;

	opts->options &= priv->info.options;
	opts->mss_encode = opts->mss;
	opts->mss = info->mss;
	if (opts->options & NF_SYNPROXY_OPT_TIMESTAMP)
		synproxy_init_timestamp_cookie(info, opts);
	else
		opts->options &= ~(NF_SYNPROXY_OPT_WSCALE |
				   NF_SYNPROXY_OPT_SACK_PERM |
				   NF_SYNPROXY_OPT_ECN);
}

static void nft_synproxy_eval_v4(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt,
				 const struct tcphdr *tcp,
				 struct tcphdr *_tcph,
				 struct synproxy_options *opts)
{
	struct nft_synproxy *priv = nft_expr_priv(expr);
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
static void nft_synproxy_eval_v6(const struct nft_expr *expr,
				 struct nft_regs *regs,
				 const struct nft_pktinfo *pkt,
				 const struct tcphdr *tcp,
				 struct tcphdr *_tcph,
				 struct synproxy_options *opts)
{
	struct nft_synproxy *priv = nft_expr_priv(expr);
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

static void nft_synproxy_eval(const struct nft_expr *expr,
			      struct nft_regs *regs,
			      const struct nft_pktinfo *pkt)
{
	struct synproxy_options opts = {};
	struct sk_buff *skb = pkt->skb;
	int thoff = pkt->xt.thoff;
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

	tcp = skb_header_pointer(skb, pkt->xt.thoff,
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
		nft_synproxy_eval_v4(expr, regs, pkt, tcp, &_tcph, &opts);
		return;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case htons(ETH_P_IPV6):
		nft_synproxy_eval_v6(expr, regs, pkt, tcp, &_tcph, &opts);
		return;
#endif
	}
	regs->verdict.code = NFT_BREAK;
}

static int nft_synproxy_init(const struct nft_ctx *ctx,
			     const struct nft_expr *expr,
			     const struct nlattr * const tb[])
{
	struct synproxy_net *snet = synproxy_pernet(ctx->net);
	struct nft_synproxy *priv = nft_expr_priv(expr);
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
	case NFPROTO_BRIDGE:
		err = nf_synproxy_ipv4_init(snet, ctx->net);
		if (err)
			goto nf_ct_failure;
		err = nf_synproxy_ipv6_init(snet, ctx->net);
		if (err)
			goto nf_ct_failure;
		break;
	}

	return 0;

nf_ct_failure:
	nf_ct_netns_put(ctx->net, ctx->family);
	return err;
}

static void nft_synproxy_destroy(const struct nft_ctx *ctx,
				 const struct nft_expr *expr)
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
	case NFPROTO_BRIDGE:
		nf_synproxy_ipv4_fini(snet, ctx->net);
		nf_synproxy_ipv6_fini(snet, ctx->net);
		break;
	}
	nf_ct_netns_put(ctx->net, ctx->family);
}

static int nft_synproxy_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_synproxy *priv = nft_expr_priv(expr);

	if (nla_put_be16(skb, NFTA_SYNPROXY_MSS, htons(priv->info.mss)) ||
	    nla_put_u8(skb, NFTA_SYNPROXY_WSCALE, priv->info.wscale) ||
	    nla_put_be32(skb, NFTA_SYNPROXY_FLAGS, htonl(priv->info.options)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int nft_synproxy_validate(const struct nft_ctx *ctx,
				 const struct nft_expr *expr,
				 const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain, (1 << NF_INET_LOCAL_IN) |
						    (1 << NF_INET_FORWARD));
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
};

static struct nft_expr_type nft_synproxy_type __read_mostly = {
	.ops		= &nft_synproxy_ops,
	.name		= "synproxy",
	.owner		= THIS_MODULE,
	.policy		= nft_synproxy_policy,
	.maxattr	= NFTA_SYNPROXY_MAX,
};

static int __init nft_synproxy_module_init(void)
{
	return nft_register_expr(&nft_synproxy_type);
}

static void __exit nft_synproxy_module_exit(void)
{
	return nft_unregister_expr(&nft_synproxy_type);
}

module_init(nft_synproxy_module_init);
module_exit(nft_synproxy_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fernando Fernandez <ffmancera@riseup.net>");
MODULE_ALIAS_NFT_EXPR("synproxy");
