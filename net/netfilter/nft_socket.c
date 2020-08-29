/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_socket.h>
#include <net/inet_sock.h>
#include <net/tcp.h>

struct nft_socket {
	enum nft_socket_keys		key:8;
	union {
		enum nft_registers	dreg:8;
	};
};

static void nft_socket_wildcard(const struct nft_pktinfo *pkt,
				struct nft_regs *regs, struct sock *sk,
				u32 *dest)
{
	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		nft_reg_store8(dest, inet_sk(sk)->inet_rcv_saddr == 0);
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		nft_reg_store8(dest, ipv6_addr_any(&sk->sk_v6_rcv_saddr));
		break;
#endif
	default:
		regs->verdict.code = NFT_BREAK;
		return;
	}
}

static void nft_socket_eval(const struct nft_expr *expr,
			    struct nft_regs *regs,
			    const struct nft_pktinfo *pkt)
{
	const struct nft_socket *priv = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	struct sock *sk = skb->sk;
	u32 *dest = &regs->data[priv->dreg];

	if (sk && !net_eq(nft_net(pkt), sock_net(sk)))
		sk = NULL;

	if (!sk)
		switch(nft_pf(pkt)) {
		case NFPROTO_IPV4:
			sk = nf_sk_lookup_slow_v4(nft_net(pkt), skb, nft_in(pkt));
			break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
		case NFPROTO_IPV6:
			sk = nf_sk_lookup_slow_v6(nft_net(pkt), skb, nft_in(pkt));
			break;
#endif
		default:
			WARN_ON_ONCE(1);
			regs->verdict.code = NFT_BREAK;
			return;
		}

	if (!sk) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	switch(priv->key) {
	case NFT_SOCKET_TRANSPARENT:
		nft_reg_store8(dest, inet_sk_transparent(sk));
		break;
	case NFT_SOCKET_MARK:
		if (sk_fullsock(sk)) {
			*dest = sk->sk_mark;
		} else {
			regs->verdict.code = NFT_BREAK;
			return;
		}
		break;
	case NFT_SOCKET_WILDCARD:
		if (!sk_fullsock(sk)) {
			regs->verdict.code = NFT_BREAK;
			return;
		}
		nft_socket_wildcard(pkt, regs, sk, dest);
		break;
	default:
		WARN_ON(1);
		regs->verdict.code = NFT_BREAK;
	}

	if (sk != skb->sk)
		sock_gen_put(sk);
}

static const struct nla_policy nft_socket_policy[NFTA_SOCKET_MAX + 1] = {
	[NFTA_SOCKET_KEY]		= { .type = NLA_U32 },
	[NFTA_SOCKET_DREG]		= { .type = NLA_U32 },
};

static int nft_socket_init(const struct nft_ctx *ctx,
			   const struct nft_expr *expr,
			   const struct nlattr * const tb[])
{
	struct nft_socket *priv = nft_expr_priv(expr);
	unsigned int len;

	if (!tb[NFTA_SOCKET_DREG] || !tb[NFTA_SOCKET_KEY])
		return -EINVAL;

	switch(ctx->family) {
	case NFPROTO_IPV4:
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
#endif
	case NFPROTO_INET:
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->key = ntohl(nla_get_u32(tb[NFTA_SOCKET_KEY]));
	switch(priv->key) {
	case NFT_SOCKET_TRANSPARENT:
	case NFT_SOCKET_WILDCARD:
		len = sizeof(u8);
		break;
	case NFT_SOCKET_MARK:
		len = sizeof(u32);
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->dreg = nft_parse_register(tb[NFTA_SOCKET_DREG]);
	return nft_validate_register_store(ctx, priv->dreg, NULL,
					   NFT_DATA_VALUE, len);
}

static int nft_socket_dump(struct sk_buff *skb,
			   const struct nft_expr *expr)
{
	const struct nft_socket *priv = nft_expr_priv(expr);

	if (nla_put_u32(skb, NFTA_SOCKET_KEY, htonl(priv->key)))
		return -1;
	if (nft_dump_register(skb, NFTA_SOCKET_DREG, priv->dreg))
		return -1;
	return 0;
}

static struct nft_expr_type nft_socket_type;
static const struct nft_expr_ops nft_socket_ops = {
	.type		= &nft_socket_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_socket)),
	.eval		= nft_socket_eval,
	.init		= nft_socket_init,
	.dump		= nft_socket_dump,
};

static struct nft_expr_type nft_socket_type __read_mostly = {
	.name		= "socket",
	.ops		= &nft_socket_ops,
	.policy		= nft_socket_policy,
	.maxattr	= NFTA_SOCKET_MAX,
	.owner		= THIS_MODULE,
};

static int __init nft_socket_module_init(void)
{
	return nft_register_expr(&nft_socket_type);
}

static void __exit nft_socket_module_exit(void)
{
	nft_unregister_expr(&nft_socket_type);
}

module_init(nft_socket_module_init);
module_exit(nft_socket_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Máté Eckl");
MODULE_DESCRIPTION("nf_tables socket match module");
MODULE_ALIAS_NFT_EXPR("socket");
