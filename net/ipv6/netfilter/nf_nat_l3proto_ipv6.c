/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Development of IPv6 NAT funded by Astaro.
 */
#include <linux/types.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <net/secure_seq.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <net/ip6_route.h>
#include <net/ipv6.h>

#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>

static const struct nf_nat_l3proto nf_nat_l3proto_ipv6;

#ifdef CONFIG_XFRM
static void nf_nat_ipv6_decode_session(struct sk_buff *skb,
				       const struct nf_conn *ct,
				       enum ip_conntrack_dir dir,
				       unsigned long statusbit,
				       struct flowi *fl)
{
	const struct nf_conntrack_tuple *t = &ct->tuplehash[dir].tuple;
	struct flowi6 *fl6 = &fl->u.ip6;

	if (ct->status & statusbit) {
		fl6->daddr = t->dst.u3.in6;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl6->fl6_dport = t->dst.u.all;
	}

	statusbit ^= IPS_NAT_MASK;

	if (ct->status & statusbit) {
		fl6->saddr = t->src.u3.in6;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl6->fl6_sport = t->src.u.all;
	}
}
#endif

static bool nf_nat_ipv6_in_range(const struct nf_conntrack_tuple *t,
				 const struct nf_nat_range *range)
{
	return ipv6_addr_cmp(&t->src.u3.in6, &range->min_addr.in6) >= 0 &&
	       ipv6_addr_cmp(&t->src.u3.in6, &range->max_addr.in6) <= 0;
}

static u32 nf_nat_ipv6_secure_port(const struct nf_conntrack_tuple *t,
				   __be16 dport)
{
	return secure_ipv6_port_ephemeral(t->src.u3.ip6, t->dst.u3.ip6, dport);
}

static bool nf_nat_ipv6_manip_pkt(struct sk_buff *skb,
				  unsigned int iphdroff,
				  const struct nf_nat_l4proto *l4proto,
				  const struct nf_conntrack_tuple *target,
				  enum nf_nat_manip_type maniptype)
{
	struct ipv6hdr *ipv6h;
	__be16 frag_off;
	int hdroff;
	u8 nexthdr;

	if (!skb_make_writable(skb, iphdroff + sizeof(*ipv6h)))
		return false;

	ipv6h = (void *)skb->data + iphdroff;
	nexthdr = ipv6h->nexthdr;
	hdroff = ipv6_skip_exthdr(skb, iphdroff + sizeof(*ipv6h),
				  &nexthdr, &frag_off);
	if (hdroff < 0)
		goto manip_addr;

	if ((frag_off & htons(~0x7)) == 0 &&
	    !l4proto->manip_pkt(skb, &nf_nat_l3proto_ipv6, iphdroff, hdroff,
				target, maniptype))
		return false;

	/* must reload, offset might have changed */
	ipv6h = (void *)skb->data + iphdroff;

manip_addr:
	if (maniptype == NF_NAT_MANIP_SRC)
		ipv6h->saddr = target->src.u3.in6;
	else
		ipv6h->daddr = target->dst.u3.in6;

	return true;
}

static void nf_nat_ipv6_csum_update(struct sk_buff *skb,
				    unsigned int iphdroff, __sum16 *check,
				    const struct nf_conntrack_tuple *t,
				    enum nf_nat_manip_type maniptype)
{
	const struct ipv6hdr *ipv6h = (struct ipv6hdr *)(skb->data + iphdroff);
	const struct in6_addr *oldip, *newip;

	if (maniptype == NF_NAT_MANIP_SRC) {
		oldip = &ipv6h->saddr;
		newip = &t->src.u3.in6;
	} else {
		oldip = &ipv6h->daddr;
		newip = &t->dst.u3.in6;
	}
	inet_proto_csum_replace16(check, skb, oldip->s6_addr32,
				  newip->s6_addr32, true);
}

static void nf_nat_ipv6_csum_recalc(struct sk_buff *skb,
				    u8 proto, void *data, __sum16 *check,
				    int datalen, int oldlen)
{
	const struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct rt6_info *rt = (struct rt6_info *)skb_dst(skb);

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		if (!(rt->rt6i_flags & RTF_LOCAL) &&
		    (!skb->dev || skb->dev->features & NETIF_F_V6_CSUM)) {
			skb->ip_summed = CHECKSUM_PARTIAL;
			skb->csum_start = skb_headroom(skb) +
					  skb_network_offset(skb) +
					  (data - (void *)skb->data);
			skb->csum_offset = (void *)check - data;
			*check = ~csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
						  datalen, proto, 0);
		} else {
			*check = 0;
			*check = csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
						 datalen, proto,
						 csum_partial(data, datalen,
							      0));
			if (proto == IPPROTO_UDP && !*check)
				*check = CSUM_MANGLED_0;
		}
	} else
		inet_proto_csum_replace2(check, skb,
					 htons(oldlen), htons(datalen), true);
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
static int nf_nat_ipv6_nlattr_to_range(struct nlattr *tb[],
				       struct nf_nat_range *range)
{
	if (tb[CTA_NAT_V6_MINIP]) {
		nla_memcpy(&range->min_addr.ip6, tb[CTA_NAT_V6_MINIP],
			   sizeof(struct in6_addr));
		range->flags |= NF_NAT_RANGE_MAP_IPS;
	}

	if (tb[CTA_NAT_V6_MAXIP])
		nla_memcpy(&range->max_addr.ip6, tb[CTA_NAT_V6_MAXIP],
			   sizeof(struct in6_addr));
	else
		range->max_addr = range->min_addr;

	return 0;
}
#endif

static const struct nf_nat_l3proto nf_nat_l3proto_ipv6 = {
	.l3proto		= NFPROTO_IPV6,
	.secure_port		= nf_nat_ipv6_secure_port,
	.in_range		= nf_nat_ipv6_in_range,
	.manip_pkt		= nf_nat_ipv6_manip_pkt,
	.csum_update		= nf_nat_ipv6_csum_update,
	.csum_recalc		= nf_nat_ipv6_csum_recalc,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.nlattr_to_range	= nf_nat_ipv6_nlattr_to_range,
#endif
#ifdef CONFIG_XFRM
	.decode_session	= nf_nat_ipv6_decode_session,
#endif
};

int nf_nat_icmpv6_reply_translation(struct sk_buff *skb,
				    struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int hooknum,
				    unsigned int hdrlen)
{
	struct {
		struct icmp6hdr	icmp6;
		struct ipv6hdr	ip6;
	} *inside;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	enum nf_nat_manip_type manip = HOOK2MANIP(hooknum);
	const struct nf_nat_l4proto *l4proto;
	struct nf_conntrack_tuple target;
	unsigned long statusbit;

	NF_CT_ASSERT(ctinfo == IP_CT_RELATED || ctinfo == IP_CT_RELATED_REPLY);

	if (!skb_make_writable(skb, hdrlen + sizeof(*inside)))
		return 0;
	if (nf_ip6_checksum(skb, hooknum, hdrlen, IPPROTO_ICMPV6))
		return 0;

	inside = (void *)skb->data + hdrlen;
	if (inside->icmp6.icmp6_type == NDISC_REDIRECT) {
		if ((ct->status & IPS_NAT_DONE_MASK) != IPS_NAT_DONE_MASK)
			return 0;
		if (ct->status & IPS_NAT_MASK)
			return 0;
	}

	if (manip == NF_NAT_MANIP_SRC)
		statusbit = IPS_SRC_NAT;
	else
		statusbit = IPS_DST_NAT;

	/* Invert if this is reply direction */
	if (dir == IP_CT_DIR_REPLY)
		statusbit ^= IPS_NAT_MASK;

	if (!(ct->status & statusbit))
		return 1;

	l4proto = __nf_nat_l4proto_find(NFPROTO_IPV6, inside->ip6.nexthdr);
	if (!nf_nat_ipv6_manip_pkt(skb, hdrlen + sizeof(inside->icmp6),
				   l4proto, &ct->tuplehash[!dir].tuple, !manip))
		return 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		struct ipv6hdr *ipv6h = ipv6_hdr(skb);
		inside = (void *)skb->data + hdrlen;
		inside->icmp6.icmp6_cksum = 0;
		inside->icmp6.icmp6_cksum =
			csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
					skb->len - hdrlen, IPPROTO_ICMPV6,
					csum_partial(&inside->icmp6,
						     skb->len - hdrlen, 0));
	}

	nf_ct_invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);
	l4proto = __nf_nat_l4proto_find(NFPROTO_IPV6, IPPROTO_ICMPV6);
	if (!nf_nat_ipv6_manip_pkt(skb, 0, l4proto, &target, manip))
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(nf_nat_icmpv6_reply_translation);

unsigned int
nf_nat_ipv6_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state,
	       unsigned int (*do_chain)(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state,
					struct nf_conn *ct))
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	struct nf_conn_nat *nat;
	enum nf_nat_manip_type maniptype = HOOK2MANIP(state->hook);
	__be16 frag_off;
	int hdrlen;
	u8 nexthdr;

	ct = nf_ct_get(skb, &ctinfo);
	/* Can't track?  It's not due to stress, or conntrack would
	 * have dropped it.  Hence it's the user's responsibilty to
	 * packet filter it out, or implement conntrack/NAT for that
	 * protocol. 8) --RR
	 */
	if (!ct)
		return NF_ACCEPT;

	/* Don't try to NAT if this packet is not conntracked */
	if (nf_ct_is_untracked(ct))
		return NF_ACCEPT;

	nat = nf_ct_nat_ext_add(ct);
	if (nat == NULL)
		return NF_ACCEPT;

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED_REPLY:
		nexthdr = ipv6_hdr(skb)->nexthdr;
		hdrlen = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
					  &nexthdr, &frag_off);

		if (hdrlen >= 0 && nexthdr == IPPROTO_ICMPV6) {
			if (!nf_nat_icmpv6_reply_translation(skb, ct, ctinfo,
							     state->hook,
							     hdrlen))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
		/* Fall thru... (Only ICMPs can be IP_CT_IS_REPLY) */
	case IP_CT_NEW:
		/* Seen it before?  This can happen for loopback, retrans,
		 * or local packets.
		 */
		if (!nf_nat_initialized(ct, maniptype)) {
			unsigned int ret;

			ret = do_chain(priv, skb, state, ct);
			if (ret != NF_ACCEPT)
				return ret;

			if (nf_nat_initialized(ct, HOOK2MANIP(state->hook)))
				break;

			ret = nf_nat_alloc_null_binding(ct, state->hook);
			if (ret != NF_ACCEPT)
				return ret;
		} else {
			pr_debug("Already setup manip %s for ct %p\n",
				 maniptype == NF_NAT_MANIP_SRC ? "SRC" : "DST",
				 ct);
			if (nf_nat_oif_changed(state->hook, ctinfo, nat, state->out))
				goto oif_changed;
		}
		break;

	default:
		/* ESTABLISHED */
		NF_CT_ASSERT(ctinfo == IP_CT_ESTABLISHED ||
			     ctinfo == IP_CT_ESTABLISHED_REPLY);
		if (nf_nat_oif_changed(state->hook, ctinfo, nat, state->out))
			goto oif_changed;
	}

	return nf_nat_packet(ct, ctinfo, state->hook, skb);

oif_changed:
	nf_ct_kill_acct(ct, ctinfo, skb);
	return NF_DROP;
}
EXPORT_SYMBOL_GPL(nf_nat_ipv6_fn);

unsigned int
nf_nat_ipv6_in(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state,
	       unsigned int (*do_chain)(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state,
					struct nf_conn *ct))
{
	unsigned int ret;
	struct in6_addr daddr = ipv6_hdr(skb)->daddr;

	ret = nf_nat_ipv6_fn(priv, skb, state, do_chain);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    ipv6_addr_cmp(&daddr, &ipv6_hdr(skb)->daddr))
		skb_dst_drop(skb);

	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_ipv6_in);

unsigned int
nf_nat_ipv6_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state,
		unsigned int (*do_chain)(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state,
					 struct nf_conn *ct))
{
#ifdef CONFIG_XFRM
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	int err;
#endif
	unsigned int ret;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct ipv6hdr))
		return NF_ACCEPT;

	ret = nf_nat_ipv6_fn(priv, skb, state, do_chain);
#ifdef CONFIG_XFRM
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    !(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.src.u3,
				      &ct->tuplehash[!dir].tuple.dst.u3) ||
		    (ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMPV6 &&
		     ct->tuplehash[dir].tuple.src.u.all !=
		     ct->tuplehash[!dir].tuple.dst.u.all)) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET6);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
	}
#endif
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_ipv6_out);

unsigned int
nf_nat_ipv6_local_fn(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state,
		     unsigned int (*do_chain)(void *priv,
					      struct sk_buff *skb,
					      const struct nf_hook_state *state,
					      struct nf_conn *ct))
{
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int ret;
	int err;

	/* root is playing with raw sockets. */
	if (skb->len < sizeof(struct ipv6hdr))
		return NF_ACCEPT;

	ret = nf_nat_ipv6_fn(priv, skb, state, do_chain);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (!nf_inet_addr_cmp(&ct->tuplehash[dir].tuple.dst.u3,
				      &ct->tuplehash[!dir].tuple.src.u3)) {
			err = ip6_route_me_harder(state->net, skb);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#ifdef CONFIG_XFRM
		else if (!(IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED) &&
			 ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMPV6 &&
			 ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET6);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#endif
	}
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_ipv6_local_fn);

static int __init nf_nat_l3proto_ipv6_init(void)
{
	int err;

	err = nf_nat_l4proto_register(NFPROTO_IPV6, &nf_nat_l4proto_icmpv6);
	if (err < 0)
		goto err1;
	err = nf_nat_l3proto_register(&nf_nat_l3proto_ipv6);
	if (err < 0)
		goto err2;
	return err;

err2:
	nf_nat_l4proto_unregister(NFPROTO_IPV6, &nf_nat_l4proto_icmpv6);
err1:
	return err;
}

static void __exit nf_nat_l3proto_ipv6_exit(void)
{
	nf_nat_l3proto_unregister(&nf_nat_l3proto_ipv6);
	nf_nat_l4proto_unregister(NFPROTO_IPV6, &nf_nat_l4proto_icmpv6);
}

MODULE_LICENSE("GPL");
MODULE_ALIAS("nf-nat-" __stringify(AF_INET6));

module_init(nf_nat_l3proto_ipv6_init);
module_exit(nf_nat_l3proto_ipv6_exit);
