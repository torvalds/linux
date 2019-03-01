/*
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/secure_seq.h>
#include <net/checksum.h>
#include <net/route.h>
#include <net/ip.h>

#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_l3proto.h>
#include <net/netfilter/nf_nat_l4proto.h>

static const struct nf_nat_l3proto nf_nat_l3proto_ipv4;

#ifdef CONFIG_XFRM
static void nf_nat_ipv4_decode_session(struct sk_buff *skb,
				       const struct nf_conn *ct,
				       enum ip_conntrack_dir dir,
				       unsigned long statusbit,
				       struct flowi *fl)
{
	const struct nf_conntrack_tuple *t = &ct->tuplehash[dir].tuple;
	struct flowi4 *fl4 = &fl->u.ip4;

	if (ct->status & statusbit) {
		fl4->daddr = t->dst.u3.ip;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl4->fl4_dport = t->dst.u.all;
	}

	statusbit ^= IPS_NAT_MASK;

	if (ct->status & statusbit) {
		fl4->saddr = t->src.u3.ip;
		if (t->dst.protonum == IPPROTO_TCP ||
		    t->dst.protonum == IPPROTO_UDP ||
		    t->dst.protonum == IPPROTO_UDPLITE ||
		    t->dst.protonum == IPPROTO_DCCP ||
		    t->dst.protonum == IPPROTO_SCTP)
			fl4->fl4_sport = t->src.u.all;
	}
}
#endif /* CONFIG_XFRM */

static bool nf_nat_ipv4_manip_pkt(struct sk_buff *skb,
				  unsigned int iphdroff,
				  const struct nf_conntrack_tuple *target,
				  enum nf_nat_manip_type maniptype)
{
	struct iphdr *iph;
	unsigned int hdroff;

	if (!skb_make_writable(skb, iphdroff + sizeof(*iph)))
		return false;

	iph = (void *)skb->data + iphdroff;
	hdroff = iphdroff + iph->ihl * 4;

	if (!nf_nat_l4proto_manip_pkt(skb, &nf_nat_l3proto_ipv4, iphdroff,
				      hdroff, target, maniptype))
		return false;
	iph = (void *)skb->data + iphdroff;

	if (maniptype == NF_NAT_MANIP_SRC) {
		csum_replace4(&iph->check, iph->saddr, target->src.u3.ip);
		iph->saddr = target->src.u3.ip;
	} else {
		csum_replace4(&iph->check, iph->daddr, target->dst.u3.ip);
		iph->daddr = target->dst.u3.ip;
	}
	return true;
}

static void nf_nat_ipv4_csum_update(struct sk_buff *skb,
				    unsigned int iphdroff, __sum16 *check,
				    const struct nf_conntrack_tuple *t,
				    enum nf_nat_manip_type maniptype)
{
	struct iphdr *iph = (struct iphdr *)(skb->data + iphdroff);
	__be32 oldip, newip;

	if (maniptype == NF_NAT_MANIP_SRC) {
		oldip = iph->saddr;
		newip = t->src.u3.ip;
	} else {
		oldip = iph->daddr;
		newip = t->dst.u3.ip;
	}
	inet_proto_csum_replace4(check, skb, oldip, newip, true);
}

static void nf_nat_ipv4_csum_recalc(struct sk_buff *skb,
				    u8 proto, void *data, __sum16 *check,
				    int datalen, int oldlen)
{
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		const struct iphdr *iph = ip_hdr(skb);

		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum_start = skb_headroom(skb) + skb_network_offset(skb) +
			ip_hdrlen(skb);
		skb->csum_offset = (void *)check - data;
		*check = ~csum_tcpudp_magic(iph->saddr, iph->daddr, datalen,
					    proto, 0);
	} else
		inet_proto_csum_replace2(check, skb,
					 htons(oldlen), htons(datalen), true);
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
static int nf_nat_ipv4_nlattr_to_range(struct nlattr *tb[],
				       struct nf_nat_range2 *range)
{
	if (tb[CTA_NAT_V4_MINIP]) {
		range->min_addr.ip = nla_get_be32(tb[CTA_NAT_V4_MINIP]);
		range->flags |= NF_NAT_RANGE_MAP_IPS;
	}

	if (tb[CTA_NAT_V4_MAXIP])
		range->max_addr.ip = nla_get_be32(tb[CTA_NAT_V4_MAXIP]);
	else
		range->max_addr.ip = range->min_addr.ip;

	return 0;
}
#endif

static const struct nf_nat_l3proto nf_nat_l3proto_ipv4 = {
	.l3proto		= NFPROTO_IPV4,
	.manip_pkt		= nf_nat_ipv4_manip_pkt,
	.csum_update		= nf_nat_ipv4_csum_update,
	.csum_recalc		= nf_nat_ipv4_csum_recalc,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.nlattr_to_range	= nf_nat_ipv4_nlattr_to_range,
#endif
#ifdef CONFIG_XFRM
	.decode_session		= nf_nat_ipv4_decode_session,
#endif
};

int nf_nat_icmp_reply_translation(struct sk_buff *skb,
				  struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  unsigned int hooknum)
{
	struct {
		struct icmphdr	icmp;
		struct iphdr	ip;
	} *inside;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	enum nf_nat_manip_type manip = HOOK2MANIP(hooknum);
	unsigned int hdrlen = ip_hdrlen(skb);
	struct nf_conntrack_tuple target;
	unsigned long statusbit;

	WARN_ON(ctinfo != IP_CT_RELATED && ctinfo != IP_CT_RELATED_REPLY);

	if (!skb_make_writable(skb, hdrlen + sizeof(*inside)))
		return 0;
	if (nf_ip_checksum(skb, hooknum, hdrlen, 0))
		return 0;

	inside = (void *)skb->data + hdrlen;
	if (inside->icmp.type == ICMP_REDIRECT) {
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

	if (!nf_nat_ipv4_manip_pkt(skb, hdrlen + sizeof(inside->icmp),
				   &ct->tuplehash[!dir].tuple, !manip))
		return 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		/* Reloading "inside" here since manip_pkt may reallocate */
		inside = (void *)skb->data + hdrlen;
		inside->icmp.checksum = 0;
		inside->icmp.checksum =
			csum_fold(skb_checksum(skb, hdrlen,
					       skb->len - hdrlen, 0));
	}

	/* Change outer to look like the reply to an incoming packet */
	nf_ct_invert_tuplepr(&target, &ct->tuplehash[!dir].tuple);
	target.dst.protonum = IPPROTO_ICMP;
	if (!nf_nat_ipv4_manip_pkt(skb, 0, &target, manip))
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(nf_nat_icmp_reply_translation);

static unsigned int
nf_nat_ipv4_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	if (ctinfo == IP_CT_RELATED || ctinfo == IP_CT_RELATED_REPLY) {
		if (ip_hdr(skb)->protocol == IPPROTO_ICMP) {
			if (!nf_nat_icmp_reply_translation(skb, ct, ctinfo,
							   state->hook))
				return NF_DROP;
			else
				return NF_ACCEPT;
		}
	}

	return nf_nat_inet_fn(priv, skb, state);
}

static unsigned int
nf_nat_ipv4_in(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state)
{
	unsigned int ret;
	__be32 daddr = ip_hdr(skb)->daddr;

	ret = nf_nat_ipv4_fn(priv, skb, state);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    daddr != ip_hdr(skb)->daddr)
		skb_dst_drop(skb);

	return ret;
}

static unsigned int
nf_nat_ipv4_out(void *priv, struct sk_buff *skb,
		const struct nf_hook_state *state)
{
#ifdef CONFIG_XFRM
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	int err;
#endif
	unsigned int ret;

	ret = nf_nat_ipv4_fn(priv, skb, state);
#ifdef CONFIG_XFRM
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    !(IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED) &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if ((ct->tuplehash[dir].tuple.src.u3.ip !=
		     ct->tuplehash[!dir].tuple.dst.u3.ip) ||
		    (ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMP &&
		     ct->tuplehash[dir].tuple.src.u.all !=
		     ct->tuplehash[!dir].tuple.dst.u.all)) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
	}
#endif
	return ret;
}

static unsigned int
nf_nat_ipv4_local_fn(void *priv, struct sk_buff *skb,
		     const struct nf_hook_state *state)
{
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	unsigned int ret;
	int err;

	ret = nf_nat_ipv4_fn(priv, skb, state);
	if (ret != NF_DROP && ret != NF_STOLEN &&
	    (ct = nf_ct_get(skb, &ctinfo)) != NULL) {
		enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);

		if (ct->tuplehash[dir].tuple.dst.u3.ip !=
		    ct->tuplehash[!dir].tuple.src.u3.ip) {
			err = ip_route_me_harder(state->net, skb, RTN_UNSPEC);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#ifdef CONFIG_XFRM
		else if (!(IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED) &&
			 ct->tuplehash[dir].tuple.dst.protonum != IPPROTO_ICMP &&
			 ct->tuplehash[dir].tuple.dst.u.all !=
			 ct->tuplehash[!dir].tuple.src.u.all) {
			err = nf_xfrm_me_harder(state->net, skb, AF_INET);
			if (err < 0)
				ret = NF_DROP_ERR(err);
		}
#endif
	}
	return ret;
}

static const struct nf_hook_ops nf_nat_ipv4_ops[] = {
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv4_in,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv4_out,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
	/* Before packet filtering, change destination */
	{
		.hook		= nf_nat_ipv4_local_fn,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_OUT,
		.priority	= NF_IP_PRI_NAT_DST,
	},
	/* After packet filtering, change source */
	{
		.hook		= nf_nat_ipv4_fn,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_NAT_SRC,
	},
};

int nf_nat_l3proto_ipv4_register_fn(struct net *net, const struct nf_hook_ops *ops)
{
	return nf_nat_register_fn(net, ops, nf_nat_ipv4_ops, ARRAY_SIZE(nf_nat_ipv4_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_l3proto_ipv4_register_fn);

void nf_nat_l3proto_ipv4_unregister_fn(struct net *net, const struct nf_hook_ops *ops)
{
	nf_nat_unregister_fn(net, ops, ARRAY_SIZE(nf_nat_ipv4_ops));
}
EXPORT_SYMBOL_GPL(nf_nat_l3proto_ipv4_unregister_fn);

static int __init nf_nat_l3proto_ipv4_init(void)
{
	return nf_nat_l3proto_register(&nf_nat_l3proto_ipv4);
}

static void __exit nf_nat_l3proto_ipv4_exit(void)
{
	nf_nat_l3proto_unregister(&nf_nat_l3proto_ipv4);
}

MODULE_LICENSE("GPL");
MODULE_ALIAS("nf-nat-" __stringify(AF_INET));

module_init(nf_nat_l3proto_ipv4_init);
module_exit(nf_nat_l3proto_ipv4_exit);
