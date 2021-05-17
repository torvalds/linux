/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tproxy.h>
#include <net/inet_sock.h>
#include <net/tcp.h>
#include <linux/if_ether.h>
#include <net/netfilter/ipv4/nf_defrag_ipv4.h>
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#endif

struct nft_tproxy {
	u8	sreg_addr;
	u8	sreg_port;
	u8	family;
};

static void nft_tproxy_eval_v4(const struct nft_expr *expr,
			       struct nft_regs *regs,
			       const struct nft_pktinfo *pkt)
{
	const struct nft_tproxy *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	const struct iphdr *iph = ip_hdr(skb);
	struct udphdr _hdr, *hp;
	__be32 taddr = 0;
	__be16 tport = 0;
	struct sock *sk;

	hp = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_hdr), &_hdr);
	if (!hp) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* check if there's an ongoing connection on the packet addresses, this
	 * happens if the redirect already happened and the current packet
	 * belongs to an already established connection
	 */
	sk = nf_tproxy_get_sock_v4(nft_net(pkt), skb, iph->protocol,
				   iph->saddr, iph->daddr,
				   hp->source, hp->dest,
				   skb->dev, NF_TPROXY_LOOKUP_ESTABLISHED);

	if (priv->sreg_addr)
		taddr = regs->data[priv->sreg_addr];
	taddr = nf_tproxy_laddr4(skb, taddr, iph->daddr);

	if (priv->sreg_port)
		tport = nft_reg_load16(&regs->data[priv->sreg_port]);
	if (!tport)
		tport = hp->dest;

	/* UDP has no TCP_TIME_WAIT state, so we never enter here */
	if (sk && sk->sk_state == TCP_TIME_WAIT) {
		/* reopening a TIME_WAIT connection needs special handling */
		sk = nf_tproxy_handle_time_wait4(nft_net(pkt), skb, taddr, tport, sk);
	} else if (!sk) {
		/* no, there's no established connection, check if
		 * there's a listener on the redirected addr/port
		 */
		sk = nf_tproxy_get_sock_v4(nft_net(pkt), skb, iph->protocol,
					   iph->saddr, taddr,
					   hp->source, tport,
					   skb->dev, NF_TPROXY_LOOKUP_LISTENER);
	}

	if (sk && nf_tproxy_sk_is_transparent(sk))
		nf_tproxy_assign_sock(skb, sk);
	else
		regs->verdict.code = NFT_BREAK;
}

#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
static void nft_tproxy_eval_v6(const struct nft_expr *expr,
			       struct nft_regs *regs,
			       const struct nft_pktinfo *pkt)
{
	const struct nft_tproxy *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct in6_addr taddr;
	int thoff = pkt->xt.thoff;
	struct udphdr _hdr, *hp;
	__be16 tport = 0;
	struct sock *sk;
	int l4proto;

	memset(&taddr, 0, sizeof(taddr));

	if (!pkt->tprot_set) {
		regs->verdict.code = NFT_BREAK;
		return;
	}
	l4proto = pkt->tprot;

	hp = skb_header_pointer(skb, thoff, sizeof(_hdr), &_hdr);
	if (hp == NULL) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	/* check if there's an ongoing connection on the packet addresses, this
	 * happens if the redirect already happened and the current packet
	 * belongs to an already established connection
	 */
	sk = nf_tproxy_get_sock_v6(nft_net(pkt), skb, thoff, l4proto,
				   &iph->saddr, &iph->daddr,
				   hp->source, hp->dest,
				   nft_in(pkt), NF_TPROXY_LOOKUP_ESTABLISHED);

	if (priv->sreg_addr)
		memcpy(&taddr, &regs->data[priv->sreg_addr], sizeof(taddr));
	taddr = *nf_tproxy_laddr6(skb, &taddr, &iph->daddr);

	if (priv->sreg_port)
		tport = nft_reg_load16(&regs->data[priv->sreg_port]);
	if (!tport)
		tport = hp->dest;

	/* UDP has no TCP_TIME_WAIT state, so we never enter here */
	if (sk && sk->sk_state == TCP_TIME_WAIT) {
		/* reopening a TIME_WAIT connection needs special handling */
		sk = nf_tproxy_handle_time_wait6(skb, l4proto, thoff,
						 nft_net(pkt),
						 &taddr,
						 tport,
						 sk);
	} else if (!sk) {
		/* no there's no established connection, check if
		 * there's a listener on the redirected addr/port
		 */
		sk = nf_tproxy_get_sock_v6(nft_net(pkt), skb, thoff,
					   l4proto, &iph->saddr, &taddr,
					   hp->source, tport,
					   nft_in(pkt), NF_TPROXY_LOOKUP_LISTENER);
	}

	/* NOTE: assign_sock consumes our sk reference */
	if (sk && nf_tproxy_sk_is_transparent(sk))
		nf_tproxy_assign_sock(skb, sk);
	else
		regs->verdict.code = NFT_BREAK;
}
#endif

static void nft_tproxy_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	const struct nft_tproxy *priv = nft_expr_priv(expr);

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		switch (priv->family) {
		case NFPROTO_IPV4:
		case NFPROTO_UNSPEC:
			nft_tproxy_eval_v4(expr, regs, pkt);
			return;
		}
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		switch (priv->family) {
		case NFPROTO_IPV6:
		case NFPROTO_UNSPEC:
			nft_tproxy_eval_v6(expr, regs, pkt);
			return;
		}
#endif
	}
	regs->verdict.code = NFT_BREAK;
}

static const struct nla_policy nft_tproxy_policy[NFTA_TPROXY_MAX + 1] = {
	[NFTA_TPROXY_FAMILY]   = { .type = NLA_U32 },
	[NFTA_TPROXY_REG_ADDR] = { .type = NLA_U32 },
	[NFTA_TPROXY_REG_PORT] = { .type = NLA_U32 },
};

static int nft_tproxy_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_tproxy *priv = nft_expr_priv(expr);
	unsigned int alen = 0;
	int err;

	if (!tb[NFTA_TPROXY_FAMILY] ||
	    (!tb[NFTA_TPROXY_REG_ADDR] && !tb[NFTA_TPROXY_REG_PORT]))
		return -EINVAL;

	priv->family = ntohl(nla_get_be32(tb[NFTA_TPROXY_FAMILY]));

	switch (ctx->family) {
	case NFPROTO_IPV4:
		if (priv->family != NFPROTO_IPV4)
			return -EINVAL;
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		if (priv->family != NFPROTO_IPV6)
			return -EINVAL;
		break;
#endif
	case NFPROTO_INET:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Address is specified but the rule family is not set accordingly */
	if (priv->family == NFPROTO_UNSPEC && tb[NFTA_TPROXY_REG_ADDR])
		return -EINVAL;

	switch (priv->family) {
	case NFPROTO_IPV4:
		alen = sizeof_field(union nf_inet_addr, in);
		err = nf_defrag_ipv4_enable(ctx->net);
		if (err)
			return err;
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		alen = sizeof_field(union nf_inet_addr, in6);
		err = nf_defrag_ipv6_enable(ctx->net);
		if (err)
			return err;
		break;
#endif
	case NFPROTO_UNSPEC:
		/* No address is specified here */
		err = nf_defrag_ipv4_enable(ctx->net);
		if (err)
			return err;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
		err = nf_defrag_ipv6_enable(ctx->net);
		if (err)
			return err;
#endif
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (tb[NFTA_TPROXY_REG_ADDR]) {
		err = nft_parse_register_load(tb[NFTA_TPROXY_REG_ADDR],
					      &priv->sreg_addr, alen);
		if (err < 0)
			return err;
	}

	if (tb[NFTA_TPROXY_REG_PORT]) {
		err = nft_parse_register_load(tb[NFTA_TPROXY_REG_PORT],
					      &priv->sreg_port, sizeof(u16));
		if (err < 0)
			return err;
	}

	return 0;
}

static void nft_tproxy_destroy(const struct nft_ctx *ctx,
			       const struct nft_expr *expr)
{
	const struct nft_tproxy *priv = nft_expr_priv(expr);

	switch (priv->family) {
	case NFPROTO_IPV4:
		nf_defrag_ipv4_disable(ctx->net);
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		nf_defrag_ipv6_disable(ctx->net);
		break;
#endif
	case NFPROTO_UNSPEC:
		nf_defrag_ipv4_disable(ctx->net);
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
		nf_defrag_ipv6_disable(ctx->net);
#endif
		break;
	}
}

static int nft_tproxy_dump(struct sk_buff *skb,
			   const struct nft_expr *expr)
{
	const struct nft_tproxy *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_TPROXY_FAMILY, htonl(priv->family)))
		return -1;

	if (priv->sreg_addr &&
	    nft_dump_register(skb, NFTA_TPROXY_REG_ADDR, priv->sreg_addr))
		return -1;

	if (priv->sreg_port &&
	    nft_dump_register(skb, NFTA_TPROXY_REG_PORT, priv->sreg_port))
			return -1;

	return 0;
}

static struct nft_expr_type nft_tproxy_type;
static const struct nft_expr_ops nft_tproxy_ops = {
	.type		= &nft_tproxy_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_tproxy)),
	.eval		= nft_tproxy_eval,
	.init		= nft_tproxy_init,
	.destroy	= nft_tproxy_destroy,
	.dump		= nft_tproxy_dump,
};

static struct nft_expr_type nft_tproxy_type __read_mostly = {
	.name		= "tproxy",
	.ops		= &nft_tproxy_ops,
	.policy		= nft_tproxy_policy,
	.maxattr	= NFTA_TPROXY_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_tproxy_module_init(void)
{
	return nft_register_expr(&nft_tproxy_type);
}

static void __exit nft_tproxy_module_exit(void)
{
	nft_unregister_expr(&nft_tproxy_type);
}

module_init(nft_tproxy_module_init);
module_exit(nft_tproxy_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Máté Eckl");
MODULE_DESCRIPTION("nf_tables tproxy support module");
MODULE_ALIAS_NFT_EXPR("tproxy");
