// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2009 Patrick McHardy <kaber@trash.net>
 * Copyright (c) 2014 Intel Corporation
 * Author: Tomasz Bursztyka <tomasz.bursztyka@linux.intel.com>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 */

#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/smp.h>
#include <linux/static_key.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/sock.h>
#include <net/tcp_states.h> /* for TCP_TIME_WAIT */
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nft_meta.h>
#include <net/netfilter/nf_tables_offload.h>

#include <uapi/linux/netfilter_bridge.h> /* NF_BR_PRE_ROUTING */

#define NFT_META_SECS_PER_MINUTE	60
#define NFT_META_SECS_PER_HOUR		3600
#define NFT_META_SECS_PER_DAY		86400
#define NFT_META_DAYS_PER_WEEK		7

static DEFINE_PER_CPU(struct rnd_state, nft_prandom_state);

static u8 nft_meta_weekday(void)
{
	time64_t secs = ktime_get_real_seconds();
	unsigned int dse;
	u8 wday;

	secs -= NFT_META_SECS_PER_MINUTE * sys_tz.tz_minuteswest;
	dse = div_u64(secs, NFT_META_SECS_PER_DAY);
	wday = (4 + dse) % NFT_META_DAYS_PER_WEEK;

	return wday;
}

static u32 nft_meta_hour(time64_t secs)
{
	struct tm tm;

	time64_to_tm(secs, 0, &tm);

	return tm.tm_hour * NFT_META_SECS_PER_HOUR
		+ tm.tm_min * NFT_META_SECS_PER_MINUTE
		+ tm.tm_sec;
}

static noinline_for_stack void
nft_meta_get_eval_time(enum nft_meta_keys key,
		       u32 *dest)
{
	switch (key) {
	case NFT_META_TIME_NS:
		nft_reg_store64(dest, ktime_get_real_ns());
		break;
	case NFT_META_TIME_DAY:
		nft_reg_store8(dest, nft_meta_weekday());
		break;
	case NFT_META_TIME_HOUR:
		*dest = nft_meta_hour(ktime_get_real_seconds());
		break;
	default:
		break;
	}
}

static noinline bool
nft_meta_get_eval_pkttype_lo(const struct nft_pktinfo *pkt,
			     u32 *dest)
{
	const struct sk_buff *skb = pkt->skb;

	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		if (ipv4_is_multicast(ip_hdr(skb)->daddr))
			nft_reg_store8(dest, PACKET_MULTICAST);
		else
			nft_reg_store8(dest, PACKET_BROADCAST);
		break;
	case NFPROTO_IPV6:
		nft_reg_store8(dest, PACKET_MULTICAST);
		break;
	case NFPROTO_NETDEV:
		switch (skb->protocol) {
		case htons(ETH_P_IP): {
			int noff = skb_network_offset(skb);
			struct iphdr *iph, _iph;

			iph = skb_header_pointer(skb, noff,
						 sizeof(_iph), &_iph);
			if (!iph)
				return false;

			if (ipv4_is_multicast(iph->daddr))
				nft_reg_store8(dest, PACKET_MULTICAST);
			else
				nft_reg_store8(dest, PACKET_BROADCAST);

			break;
		}
		case htons(ETH_P_IPV6):
			nft_reg_store8(dest, PACKET_MULTICAST);
			break;
		default:
			WARN_ON_ONCE(1);
			return false;
		}
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}

	return true;
}

static noinline bool
nft_meta_get_eval_skugid(enum nft_meta_keys key,
			 u32 *dest,
			 const struct nft_pktinfo *pkt)
{
	struct sock *sk = skb_to_full_sk(pkt->skb);
	struct socket *sock;

	if (!sk || !sk_fullsock(sk) || !net_eq(nft_net(pkt), sock_net(sk)))
		return false;

	read_lock_bh(&sk->sk_callback_lock);
	sock = sk->sk_socket;
	if (!sock || !sock->file) {
		read_unlock_bh(&sk->sk_callback_lock);
		return false;
	}

	switch (key) {
	case NFT_META_SKUID:
		*dest = from_kuid_munged(sock_net(sk)->user_ns,
					 sock->file->f_cred->fsuid);
		break;
	case NFT_META_SKGID:
		*dest =	from_kgid_munged(sock_net(sk)->user_ns,
					 sock->file->f_cred->fsgid);
		break;
	default:
		break;
	}

	read_unlock_bh(&sk->sk_callback_lock);
	return true;
}

#ifdef CONFIG_CGROUP_NET_CLASSID
static noinline bool
nft_meta_get_eval_cgroup(u32 *dest, const struct nft_pktinfo *pkt)
{
	struct sock *sk = skb_to_full_sk(pkt->skb);

	if (!sk || !sk_fullsock(sk) || !net_eq(nft_net(pkt), sock_net(sk)))
		return false;

	*dest = sock_cgroup_classid(&sk->sk_cgrp_data);
	return true;
}
#endif

static noinline bool nft_meta_get_eval_kind(enum nft_meta_keys key,
					    u32 *dest,
					    const struct nft_pktinfo *pkt)
{
	const struct net_device *in = nft_in(pkt), *out = nft_out(pkt);

	switch (key) {
	case NFT_META_IIFKIND:
		if (!in || !in->rtnl_link_ops)
			return false;
		strncpy((char *)dest, in->rtnl_link_ops->kind, IFNAMSIZ);
		break;
	case NFT_META_OIFKIND:
		if (!out || !out->rtnl_link_ops)
			return false;
		strncpy((char *)dest, out->rtnl_link_ops->kind, IFNAMSIZ);
		break;
	default:
		return false;
	}

	return true;
}

static void nft_meta_store_ifindex(u32 *dest, const struct net_device *dev)
{
	*dest = dev ? dev->ifindex : 0;
}

static void nft_meta_store_ifname(u32 *dest, const struct net_device *dev)
{
	strncpy((char *)dest, dev ? dev->name : "", IFNAMSIZ);
}

static bool nft_meta_store_iftype(u32 *dest, const struct net_device *dev)
{
	if (!dev)
		return false;

	nft_reg_store16(dest, dev->type);
	return true;
}

static bool nft_meta_store_ifgroup(u32 *dest, const struct net_device *dev)
{
	if (!dev)
		return false;

	*dest = dev->group;
	return true;
}

static bool nft_meta_get_eval_ifname(enum nft_meta_keys key, u32 *dest,
				     const struct nft_pktinfo *pkt)
{
	switch (key) {
	case NFT_META_IIFNAME:
		nft_meta_store_ifname(dest, nft_in(pkt));
		break;
	case NFT_META_OIFNAME:
		nft_meta_store_ifname(dest, nft_out(pkt));
		break;
	case NFT_META_IIF:
		nft_meta_store_ifindex(dest, nft_in(pkt));
		break;
	case NFT_META_OIF:
		nft_meta_store_ifindex(dest, nft_out(pkt));
		break;
	case NFT_META_IIFTYPE:
		if (!nft_meta_store_iftype(dest, nft_in(pkt)))
			return false;
		break;
	case NFT_META_OIFTYPE:
		if (!nft_meta_store_iftype(dest, nft_out(pkt)))
			return false;
		break;
	case NFT_META_IIFGROUP:
		if (!nft_meta_store_ifgroup(dest, nft_in(pkt)))
			return false;
		break;
	case NFT_META_OIFGROUP:
		if (!nft_meta_store_ifgroup(dest, nft_out(pkt)))
			return false;
		break;
	default:
		return false;
	}

	return true;
}

static noinline u32 nft_prandom_u32(void)
{
	struct rnd_state *state = this_cpu_ptr(&nft_prandom_state);

	return prandom_u32_state(state);
}

#ifdef CONFIG_IP_ROUTE_CLASSID
static noinline bool
nft_meta_get_eval_rtclassid(const struct sk_buff *skb, u32 *dest)
{
	const struct dst_entry *dst = skb_dst(skb);

	if (!dst)
		return false;

	*dest = dst->tclassid;
	return true;
}
#endif

static noinline u32 nft_meta_get_eval_sdif(const struct nft_pktinfo *pkt)
{
	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		return inet_sdif(pkt->skb);
	case NFPROTO_IPV6:
		return inet6_sdif(pkt->skb);
	}

	return 0;
}

static noinline void
nft_meta_get_eval_sdifname(u32 *dest, const struct nft_pktinfo *pkt)
{
	u32 sdif = nft_meta_get_eval_sdif(pkt);
	const struct net_device *dev;

	dev = sdif ? dev_get_by_index_rcu(nft_net(pkt), sdif) : NULL;
	nft_meta_store_ifname(dest, dev);
}

void nft_meta_get_eval(const struct nft_expr *expr,
		       struct nft_regs *regs,
		       const struct nft_pktinfo *pkt)
{
	const struct nft_meta *priv = nft_expr_priv(expr);
	const struct sk_buff *skb = pkt->skb;
	u32 *dest = &regs->data[priv->dreg];

	switch (priv->key) {
	case NFT_META_LEN:
		*dest = skb->len;
		break;
	case NFT_META_PROTOCOL:
		nft_reg_store16(dest, (__force u16)skb->protocol);
		break;
	case NFT_META_NFPROTO:
		nft_reg_store8(dest, nft_pf(pkt));
		break;
	case NFT_META_L4PROTO:
		if (!pkt->tprot_set)
			goto err;
		nft_reg_store8(dest, pkt->tprot);
		break;
	case NFT_META_PRIORITY:
		*dest = skb->priority;
		break;
	case NFT_META_MARK:
		*dest = skb->mark;
		break;
	case NFT_META_IIF:
	case NFT_META_OIF:
	case NFT_META_IIFNAME:
	case NFT_META_OIFNAME:
	case NFT_META_IIFTYPE:
	case NFT_META_OIFTYPE:
	case NFT_META_IIFGROUP:
	case NFT_META_OIFGROUP:
		if (!nft_meta_get_eval_ifname(priv->key, dest, pkt))
			goto err;
		break;
	case NFT_META_SKUID:
	case NFT_META_SKGID:
		if (!nft_meta_get_eval_skugid(priv->key, dest, pkt))
			goto err;
		break;
#ifdef CONFIG_IP_ROUTE_CLASSID
	case NFT_META_RTCLASSID:
		if (!nft_meta_get_eval_rtclassid(skb, dest))
			goto err;
		break;
#endif
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
		*dest = skb->secmark;
		break;
#endif
	case NFT_META_PKTTYPE:
		if (skb->pkt_type != PACKET_LOOPBACK) {
			nft_reg_store8(dest, skb->pkt_type);
			break;
		}

		if (!nft_meta_get_eval_pkttype_lo(pkt, dest))
			goto err;
		break;
	case NFT_META_CPU:
		*dest = raw_smp_processor_id();
		break;
#ifdef CONFIG_CGROUP_NET_CLASSID
	case NFT_META_CGROUP:
		if (!nft_meta_get_eval_cgroup(dest, pkt))
			goto err;
		break;
#endif
	case NFT_META_PRANDOM:
		*dest = nft_prandom_u32();
		break;
#ifdef CONFIG_XFRM
	case NFT_META_SECPATH:
		nft_reg_store8(dest, secpath_exists(skb));
		break;
#endif
	case NFT_META_IIFKIND:
	case NFT_META_OIFKIND:
		if (!nft_meta_get_eval_kind(priv->key, dest, pkt))
			goto err;
		break;
	case NFT_META_TIME_NS:
	case NFT_META_TIME_DAY:
	case NFT_META_TIME_HOUR:
		nft_meta_get_eval_time(priv->key, dest);
		break;
	case NFT_META_SDIF:
		*dest = nft_meta_get_eval_sdif(pkt);
		break;
	case NFT_META_SDIFNAME:
		nft_meta_get_eval_sdifname(dest, pkt);
		break;
	default:
		WARN_ON(1);
		goto err;
	}
	return;

err:
	regs->verdict.code = NFT_BREAK;
}
EXPORT_SYMBOL_GPL(nft_meta_get_eval);

void nft_meta_set_eval(const struct nft_expr *expr,
		       struct nft_regs *regs,
		       const struct nft_pktinfo *pkt)
{
	const struct nft_meta *meta = nft_expr_priv(expr);
	struct sk_buff *skb = pkt->skb;
	u32 *sreg = &regs->data[meta->sreg];
	u32 value = *sreg;
	u8 value8;

	switch (meta->key) {
	case NFT_META_MARK:
		skb->mark = value;
		break;
	case NFT_META_PRIORITY:
		skb->priority = value;
		break;
	case NFT_META_PKTTYPE:
		value8 = nft_reg_load8(sreg);

		if (skb->pkt_type != value8 &&
		    skb_pkt_type_ok(value8) &&
		    skb_pkt_type_ok(skb->pkt_type))
			skb->pkt_type = value8;
		break;
	case NFT_META_NFTRACE:
		value8 = nft_reg_load8(sreg);

		skb->nf_trace = !!value8;
		break;
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
		skb->secmark = value;
		break;
#endif
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
	unsigned int len;

	priv->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (priv->key) {
	case NFT_META_PROTOCOL:
	case NFT_META_IIFTYPE:
	case NFT_META_OIFTYPE:
		len = sizeof(u16);
		break;
	case NFT_META_NFPROTO:
	case NFT_META_L4PROTO:
	case NFT_META_LEN:
	case NFT_META_PRIORITY:
	case NFT_META_MARK:
	case NFT_META_IIF:
	case NFT_META_OIF:
	case NFT_META_SDIF:
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
#ifdef CONFIG_CGROUP_NET_CLASSID
	case NFT_META_CGROUP:
#endif
		len = sizeof(u32);
		break;
	case NFT_META_IIFNAME:
	case NFT_META_OIFNAME:
	case NFT_META_IIFKIND:
	case NFT_META_OIFKIND:
	case NFT_META_SDIFNAME:
		len = IFNAMSIZ;
		break;
	case NFT_META_PRANDOM:
		prandom_init_once(&nft_prandom_state);
		len = sizeof(u32);
		break;
#ifdef CONFIG_XFRM
	case NFT_META_SECPATH:
		len = sizeof(u8);
		break;
#endif
	case NFT_META_TIME_NS:
		len = sizeof(u64);
		break;
	case NFT_META_TIME_DAY:
		len = sizeof(u8);
		break;
	case NFT_META_TIME_HOUR:
		len = sizeof(u32);
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->dreg = nft_parse_register(tb[NFTA_META_DREG]);
	return nft_validate_register_store(ctx, priv->dreg, NULL,
					   NFT_DATA_VALUE, len);
}
EXPORT_SYMBOL_GPL(nft_meta_get_init);

static int nft_meta_get_validate_sdif(const struct nft_ctx *ctx)
{
	unsigned int hooks;

	switch (ctx->family) {
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
	case NFPROTO_INET:
		hooks = (1 << NF_INET_LOCAL_IN) |
			(1 << NF_INET_FORWARD);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
}

static int nft_meta_get_validate_xfrm(const struct nft_ctx *ctx)
{
#ifdef CONFIG_XFRM
	unsigned int hooks;

	switch (ctx->family) {
	case NFPROTO_NETDEV:
		hooks = 1 << NF_NETDEV_INGRESS;
		break;
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
	case NFPROTO_INET:
		hooks = (1 << NF_INET_PRE_ROUTING) |
			(1 << NF_INET_LOCAL_IN) |
			(1 << NF_INET_FORWARD);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
#else
	return 0;
#endif
}

static int nft_meta_get_validate(const struct nft_ctx *ctx,
				 const struct nft_expr *expr,
				 const struct nft_data **data)
{
	const struct nft_meta *priv = nft_expr_priv(expr);

	switch (priv->key) {
	case NFT_META_SECPATH:
		return nft_meta_get_validate_xfrm(ctx);
	case NFT_META_SDIF:
	case NFT_META_SDIFNAME:
		return nft_meta_get_validate_sdif(ctx);
	default:
		break;
	}

	return 0;
}

int nft_meta_set_validate(const struct nft_ctx *ctx,
			  const struct nft_expr *expr,
			  const struct nft_data **data)
{
	struct nft_meta *priv = nft_expr_priv(expr);
	unsigned int hooks;

	if (priv->key != NFT_META_PKTTYPE)
		return 0;

	switch (ctx->family) {
	case NFPROTO_BRIDGE:
		hooks = 1 << NF_BR_PRE_ROUTING;
		break;
	case NFPROTO_NETDEV:
		hooks = 1 << NF_NETDEV_INGRESS;
		break;
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
	case NFPROTO_INET:
		hooks = 1 << NF_INET_PRE_ROUTING;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return nft_chain_validate_hooks(ctx->chain, hooks);
}
EXPORT_SYMBOL_GPL(nft_meta_set_validate);

int nft_meta_set_init(const struct nft_ctx *ctx,
		      const struct nft_expr *expr,
		      const struct nlattr * const tb[])
{
	struct nft_meta *priv = nft_expr_priv(expr);
	unsigned int len;
	int err;

	priv->key = ntohl(nla_get_be32(tb[NFTA_META_KEY]));
	switch (priv->key) {
	case NFT_META_MARK:
	case NFT_META_PRIORITY:
#ifdef CONFIG_NETWORK_SECMARK
	case NFT_META_SECMARK:
#endif
		len = sizeof(u32);
		break;
	case NFT_META_NFTRACE:
		len = sizeof(u8);
		break;
	case NFT_META_PKTTYPE:
		len = sizeof(u8);
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = nft_parse_register_load(tb[NFTA_META_SREG], &priv->sreg, len);
	if (err < 0)
		return err;

	if (priv->key == NFT_META_NFTRACE)
		static_branch_inc(&nft_trace_enabled);

	return 0;
}
EXPORT_SYMBOL_GPL(nft_meta_set_init);

int nft_meta_get_dump(struct sk_buff *skb,
		      const struct nft_expr *expr)
{
	const struct nft_meta *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_META_KEY, htonl(priv->key)))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_META_DREG, priv->dreg))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_meta_get_dump);

int nft_meta_set_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_meta *priv = nft_expr_priv(expr);

	if (nla_put_be32(skb, NFTA_META_KEY, htonl(priv->key)))
		goto nla_put_failure;
	if (nft_dump_register(skb, NFTA_META_SREG, priv->sreg))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}
EXPORT_SYMBOL_GPL(nft_meta_set_dump);

void nft_meta_set_destroy(const struct nft_ctx *ctx,
			  const struct nft_expr *expr)
{
	const struct nft_meta *priv = nft_expr_priv(expr);

	if (priv->key == NFT_META_NFTRACE)
		static_branch_dec(&nft_trace_enabled);
}
EXPORT_SYMBOL_GPL(nft_meta_set_destroy);

static int nft_meta_get_offload(struct nft_offload_ctx *ctx,
				struct nft_flow_rule *flow,
				const struct nft_expr *expr)
{
	const struct nft_meta *priv = nft_expr_priv(expr);
	struct nft_offload_reg *reg = &ctx->regs[priv->dreg];

	switch (priv->key) {
	case NFT_META_PROTOCOL:
		NFT_OFFLOAD_MATCH_EXACT(FLOW_DISSECTOR_KEY_BASIC, basic, n_proto,
					sizeof(__u16), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_NETWORK);
		break;
	case NFT_META_L4PROTO:
		NFT_OFFLOAD_MATCH_EXACT(FLOW_DISSECTOR_KEY_BASIC, basic, ip_proto,
					sizeof(__u8), reg);
		nft_offload_set_dependency(ctx, NFT_OFFLOAD_DEP_TRANSPORT);
		break;
	case NFT_META_IIF:
		NFT_OFFLOAD_MATCH_EXACT(FLOW_DISSECTOR_KEY_META, meta,
					ingress_ifindex, sizeof(__u32), reg);
		break;
	case NFT_META_IIFTYPE:
		NFT_OFFLOAD_MATCH_EXACT(FLOW_DISSECTOR_KEY_META, meta,
					ingress_iftype, sizeof(__u16), reg);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct nft_expr_ops nft_meta_get_ops = {
	.type		= &nft_meta_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.eval		= nft_meta_get_eval,
	.init		= nft_meta_get_init,
	.dump		= nft_meta_get_dump,
	.validate	= nft_meta_get_validate,
	.offload	= nft_meta_get_offload,
};

static const struct nft_expr_ops nft_meta_set_ops = {
	.type		= &nft_meta_type,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_meta)),
	.eval		= nft_meta_set_eval,
	.init		= nft_meta_set_init,
	.destroy	= nft_meta_set_destroy,
	.dump		= nft_meta_set_dump,
	.validate	= nft_meta_set_validate,
};

static const struct nft_expr_ops *
nft_meta_select_ops(const struct nft_ctx *ctx,
		    const struct nlattr * const tb[])
{
	if (tb[NFTA_META_KEY] == NULL)
		return ERR_PTR(-EINVAL);

	if (tb[NFTA_META_DREG] && tb[NFTA_META_SREG])
		return ERR_PTR(-EINVAL);

#if IS_ENABLED(CONFIG_NF_TABLES_BRIDGE) && IS_MODULE(CONFIG_NFT_BRIDGE_META)
	if (ctx->family == NFPROTO_BRIDGE)
		return ERR_PTR(-EAGAIN);
#endif
	if (tb[NFTA_META_DREG])
		return &nft_meta_get_ops;

	if (tb[NFTA_META_SREG])
		return &nft_meta_set_ops;

	return ERR_PTR(-EINVAL);
}

struct nft_expr_type nft_meta_type __read_mostly = {
	.name		= "meta",
	.select_ops	= nft_meta_select_ops,
	.policy		= nft_meta_policy,
	.maxattr	= NFTA_META_MAX,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_NETWORK_SECMARK
struct nft_secmark {
	u32 secid;
	char *ctx;
};

static const struct nla_policy nft_secmark_policy[NFTA_SECMARK_MAX + 1] = {
	[NFTA_SECMARK_CTX]     = { .type = NLA_STRING, .len = NFT_SECMARK_CTX_MAXLEN },
};

static int nft_secmark_compute_secid(struct nft_secmark *priv)
{
	u32 tmp_secid = 0;
	int err;

	err = security_secctx_to_secid(priv->ctx, strlen(priv->ctx), &tmp_secid);
	if (err)
		return err;

	if (!tmp_secid)
		return -ENOENT;

	err = security_secmark_relabel_packet(tmp_secid);
	if (err)
		return err;

	priv->secid = tmp_secid;
	return 0;
}

static void nft_secmark_obj_eval(struct nft_object *obj, struct nft_regs *regs,
				 const struct nft_pktinfo *pkt)
{
	const struct nft_secmark *priv = nft_obj_data(obj);
	struct sk_buff *skb = pkt->skb;

	skb->secmark = priv->secid;
}

static int nft_secmark_obj_init(const struct nft_ctx *ctx,
				const struct nlattr * const tb[],
				struct nft_object *obj)
{
	struct nft_secmark *priv = nft_obj_data(obj);
	int err;

	if (tb[NFTA_SECMARK_CTX] == NULL)
		return -EINVAL;

	priv->ctx = nla_strdup(tb[NFTA_SECMARK_CTX], GFP_KERNEL);
	if (!priv->ctx)
		return -ENOMEM;

	err = nft_secmark_compute_secid(priv);
	if (err) {
		kfree(priv->ctx);
		return err;
	}

	security_secmark_refcount_inc();

	return 0;
}

static int nft_secmark_obj_dump(struct sk_buff *skb, struct nft_object *obj,
				bool reset)
{
	struct nft_secmark *priv = nft_obj_data(obj);
	int err;

	if (nla_put_string(skb, NFTA_SECMARK_CTX, priv->ctx))
		return -1;

	if (reset) {
		err = nft_secmark_compute_secid(priv);
		if (err)
			return err;
	}

	return 0;
}

static void nft_secmark_obj_destroy(const struct nft_ctx *ctx, struct nft_object *obj)
{
	struct nft_secmark *priv = nft_obj_data(obj);

	security_secmark_refcount_dec();

	kfree(priv->ctx);
}

static const struct nft_object_ops nft_secmark_obj_ops = {
	.type		= &nft_secmark_obj_type,
	.size		= sizeof(struct nft_secmark),
	.init		= nft_secmark_obj_init,
	.eval		= nft_secmark_obj_eval,
	.dump		= nft_secmark_obj_dump,
	.destroy	= nft_secmark_obj_destroy,
};
struct nft_object_type nft_secmark_obj_type __read_mostly = {
	.type		= NFT_OBJECT_SECMARK,
	.ops		= &nft_secmark_obj_ops,
	.maxattr	= NFTA_SECMARK_MAX,
	.policy		= nft_secmark_policy,
	.owner		= THIS_MODULE,
};
#endif /* CONFIG_NETWORK_SECMARK */
