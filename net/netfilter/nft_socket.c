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
	u8				level;
	u8				len;
	union {
		u8			dreg;
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

#ifdef CONFIG_SOCK_CGROUP_DATA
static noinline bool
nft_sock_get_eval_cgroupv2(u32 *dest, struct sock *sk, const struct nft_pktinfo *pkt, u32 level)
{
	struct cgroup *cgrp;

	if (!sk_fullsock(sk))
		return false;

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	if (level > cgrp->level)
		return false;

	memcpy(dest, &cgrp->ancestor_ids[level], sizeof(u64));

	return true;
}
#endif

static struct sock *nft_socket_do_lookup(const struct nft_pktinfo *pkt)
{
	const struct net_device *indev = nft_in(pkt);
	const struct sk_buff *skb = pkt->skb;
	struct sock *sk = NULL;

	if (!indev)
		return NULL;

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		sk = nf_sk_lookup_slow_v4(nft_net(pkt), skb, indev);
		break;
#if IS_ENABLED(CONFIG_NF_TABLES_IPV6)
	case NFPROTO_IPV6:
		sk = nf_sk_lookup_slow_v6(nft_net(pkt), skb, indev);
		break;
#endif
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return sk;
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
		sk = nft_socket_do_lookup(pkt);

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
#ifdef CONFIG_SOCK_CGROUP_DATA
	case NFT_SOCKET_CGROUPV2:
		if (!nft_sock_get_eval_cgroupv2(dest, sk, pkt, priv->level)) {
			regs->verdict.code = NFT_BREAK;
			return;
		}
		break;
#endif
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
	[NFTA_SOCKET_LEVEL]		= { .type = NLA_U32 },
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
#ifdef CONFIG_CGROUPS
	case NFT_SOCKET_CGROUPV2: {
		unsigned int level;

		if (!tb[NFTA_SOCKET_LEVEL])
			return -EINVAL;

		level = ntohl(nla_get_u32(tb[NFTA_SOCKET_LEVEL]));
		if (level > 255)
			return -EOPNOTSUPP;

		priv->level = level;
		len = sizeof(u64);
		break;
	}
#endif
	default:
		return -EOPNOTSUPP;
	}

	priv->len = len;
	return nft_parse_register_store(ctx, tb[NFTA_SOCKET_DREG], &priv->dreg,
					NULL, NFT_DATA_VALUE, len);
}

static int nft_socket_dump(struct sk_buff *skb,
			   const struct nft_expr *expr)
{
	const struct nft_socket *priv = nft_expr_priv(expr);

	if (nla_put_u32(skb, NFTA_SOCKET_KEY, htonl(priv->key)))
		return -1;
	if (nft_dump_register(skb, NFTA_SOCKET_DREG, priv->dreg))
		return -1;
	if (priv->key == NFT_SOCKET_CGROUPV2 &&
	    nla_put_u32(skb, NFTA_SOCKET_LEVEL, htonl(priv->level)))
		return -1;
	return 0;
}

static bool nft_socket_reduce(struct nft_regs_track *track,
			      const struct nft_expr *expr)
{
	const struct nft_socket *priv = nft_expr_priv(expr);
	const struct nft_socket *socket;

	if (!nft_reg_track_cmp(track, expr, priv->dreg)) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	socket = nft_expr_priv(track->regs[priv->dreg].selector);
	if (priv->key != socket->key ||
	    priv->dreg != socket->dreg ||
	    priv->level != socket->level) {
		nft_reg_track_update(track, expr, priv->dreg, priv->len);
		return false;
	}

	if (!track->regs[priv->dreg].bitwise)
		return true;

	return nft_expr_reduce_bitwise(track, expr);
}

static int nft_socket_validate(const struct nft_ctx *ctx,
			       const struct nft_expr *expr,
			       const struct nft_data **data)
{
	return nft_chain_validate_hooks(ctx->chain,
					(1 << NF_INET_PRE_ROUTING) |
					(1 << NF_INET_LOCAL_IN) |
					(1 << NF_INET_LOCAL_OUT));
}

static struct nft_expr_type nft_socket_type;
static const struct nft_expr_ops nft_socket_ops = {
	.type		= &nft_socket_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_socket)),
	.eval		= nft_socket_eval,
	.init		= nft_socket_init,
	.dump		= nft_socket_dump,
	.validate	= nft_socket_validate,
	.reduce		= nft_socket_reduce,
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
