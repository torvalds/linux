/*
 * xt_HMARK - Netfilter module to set mark by means of hashing
 *
 * (C) 2012 by Hans Schillstrom <hans.schillstrom@ericsson.com>
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_HMARK.h>

#include <net/ip.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#endif
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
#include <net/ipv6.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hans Schillstrom <hans.schillstrom@ericsson.com>");
MODULE_DESCRIPTION("Xtables: packet marking using hash calculation");
MODULE_ALIAS("ipt_HMARK");
MODULE_ALIAS("ip6t_HMARK");

struct hmark_tuple {
	u32			src;
	u32			dst;
	union hmark_ports	uports;
	uint8_t			proto;
};

static inline u32 hmark_addr6_mask(const __u32 *addr32, const __u32 *mask)
{
	return (addr32[0] & mask[0]) ^
	       (addr32[1] & mask[1]) ^
	       (addr32[2] & mask[2]) ^
	       (addr32[3] & mask[3]);
}

static inline u32
hmark_addr_mask(int l3num, const __u32 *addr32, const __u32 *mask)
{
	switch (l3num) {
	case AF_INET:
		return *addr32 & *mask;
	case AF_INET6:
		return hmark_addr6_mask(addr32, mask);
	}
	return 0;
}

static int
hmark_ct_set_htuple(const struct sk_buff *skb, struct hmark_tuple *t,
		    const struct xt_hmark_info *info)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);
	struct nf_conntrack_tuple *otuple;
	struct nf_conntrack_tuple *rtuple;

	if (ct == NULL || nf_ct_is_untracked(ct))
		return -1;

	otuple = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	rtuple = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	t->src = hmark_addr_mask(otuple->src.l3num, otuple->src.u3.all,
				 info->src_mask.all);
	t->dst = hmark_addr_mask(otuple->src.l3num, rtuple->src.u3.all,
				 info->dst_mask.all);

	if (info->flags & XT_HMARK_FLAG(XT_HMARK_METHOD_L3))
		return 0;

	t->proto = nf_ct_protonum(ct);
	if (t->proto != IPPROTO_ICMP) {
		t->uports.p16.src = otuple->src.u.all;
		t->uports.p16.dst = rtuple->src.u.all;
		t->uports.v32 = (t->uports.v32 & info->port_mask.v32) |
				info->port_set.v32;
		if (t->uports.p16.dst < t->uports.p16.src)
			swap(t->uports.p16.dst, t->uports.p16.src);
	}

	return 0;
#else
	return -1;
#endif
}

static inline u32
hmark_hash(struct hmark_tuple *t, const struct xt_hmark_info *info)
{
	u32 hash;

	if (t->dst < t->src)
		swap(t->src, t->dst);

	hash = jhash_3words(t->src, t->dst, t->uports.v32, info->hashrnd);
	hash = hash ^ (t->proto & info->proto_mask);

	return (hash % info->hmodulus) + info->hoffset;
}

static void
hmark_set_tuple_ports(const struct sk_buff *skb, unsigned int nhoff,
		      struct hmark_tuple *t, const struct xt_hmark_info *info)
{
	int protoff;

	protoff = proto_ports_offset(t->proto);
	if (protoff < 0)
		return;

	nhoff += protoff;
	if (skb_copy_bits(skb, nhoff, &t->uports, sizeof(t->uports)) < 0)
		return;

	t->uports.v32 = (t->uports.v32 & info->port_mask.v32) |
			info->port_set.v32;

	if (t->uports.p16.dst < t->uports.p16.src)
		swap(t->uports.p16.dst, t->uports.p16.src);
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static int get_inner6_hdr(const struct sk_buff *skb, int *offset)
{
	struct icmp6hdr *icmp6h, _ih6;

	icmp6h = skb_header_pointer(skb, *offset, sizeof(_ih6), &_ih6);
	if (icmp6h == NULL)
		return 0;

	if (icmp6h->icmp6_type && icmp6h->icmp6_type < 128) {
		*offset += sizeof(struct icmp6hdr);
		return 1;
	}
	return 0;
}

static int
hmark_pkt_set_htuple_ipv6(const struct sk_buff *skb, struct hmark_tuple *t,
			  const struct xt_hmark_info *info)
{
	struct ipv6hdr *ip6, _ip6;
	int flag = IP6T_FH_F_AUTH;
	unsigned int nhoff = 0;
	u16 fragoff = 0;
	int nexthdr;

	ip6 = (struct ipv6hdr *) (skb->data + skb_network_offset(skb));
	nexthdr = ipv6_find_hdr(skb, &nhoff, -1, &fragoff, &flag);
	if (nexthdr < 0)
		return 0;
	/* No need to check for icmp errors on fragments */
	if ((flag & IP6T_FH_F_FRAG) || (nexthdr != IPPROTO_ICMPV6))
		goto noicmp;
	/* Use inner header in case of ICMP errors */
	if (get_inner6_hdr(skb, &nhoff)) {
		ip6 = skb_header_pointer(skb, nhoff, sizeof(_ip6), &_ip6);
		if (ip6 == NULL)
			return -1;
		/* If AH present, use SPI like in ESP. */
		flag = IP6T_FH_F_AUTH;
		nexthdr = ipv6_find_hdr(skb, &nhoff, -1, &fragoff, &flag);
		if (nexthdr < 0)
			return -1;
	}
noicmp:
	t->src = hmark_addr6_mask(ip6->saddr.s6_addr32, info->src_mask.all);
	t->dst = hmark_addr6_mask(ip6->daddr.s6_addr32, info->dst_mask.all);

	if (info->flags & XT_HMARK_FLAG(XT_HMARK_METHOD_L3))
		return 0;

	t->proto = nexthdr;
	if (t->proto == IPPROTO_ICMPV6)
		return 0;

	if (flag & IP6T_FH_F_FRAG)
		return 0;

	hmark_set_tuple_ports(skb, nhoff, t, info);
	return 0;
}

static unsigned int
hmark_tg_v6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_hmark_info *info = par->targinfo;
	struct hmark_tuple t;

	memset(&t, 0, sizeof(struct hmark_tuple));

	if (info->flags & XT_HMARK_FLAG(XT_HMARK_CT)) {
		if (hmark_ct_set_htuple(skb, &t, info) < 0)
			return XT_CONTINUE;
	} else {
		if (hmark_pkt_set_htuple_ipv6(skb, &t, info) < 0)
			return XT_CONTINUE;
	}

	skb->mark = hmark_hash(&t, info);
	return XT_CONTINUE;
}
#endif

static int get_inner_hdr(const struct sk_buff *skb, int iphsz, int *nhoff)
{
	const struct icmphdr *icmph;
	struct icmphdr _ih;

	/* Not enough header? */
	icmph = skb_header_pointer(skb, *nhoff + iphsz, sizeof(_ih), &_ih);
	if (icmph == NULL || icmph->type > NR_ICMP_TYPES)
		return 0;

	/* Error message? */
	if (icmph->type != ICMP_DEST_UNREACH &&
	    icmph->type != ICMP_SOURCE_QUENCH &&
	    icmph->type != ICMP_TIME_EXCEEDED &&
	    icmph->type != ICMP_PARAMETERPROB &&
	    icmph->type != ICMP_REDIRECT)
		return 0;

	*nhoff += iphsz + sizeof(_ih);
	return 1;
}

static int
hmark_pkt_set_htuple_ipv4(const struct sk_buff *skb, struct hmark_tuple *t,
			  const struct xt_hmark_info *info)
{
	struct iphdr *ip, _ip;
	int nhoff = skb_network_offset(skb);

	ip = (struct iphdr *) (skb->data + nhoff);
	if (ip->protocol == IPPROTO_ICMP) {
		/* Use inner header in case of ICMP errors */
		if (get_inner_hdr(skb, ip->ihl * 4, &nhoff)) {
			ip = skb_header_pointer(skb, nhoff, sizeof(_ip), &_ip);
			if (ip == NULL)
				return -1;
		}
	}

	t->src = (__force u32) ip->saddr;
	t->dst = (__force u32) ip->daddr;

	t->src &= info->src_mask.ip;
	t->dst &= info->dst_mask.ip;

	if (info->flags & XT_HMARK_FLAG(XT_HMARK_METHOD_L3))
		return 0;

	t->proto = ip->protocol;

	/* ICMP has no ports, skip */
	if (t->proto == IPPROTO_ICMP)
		return 0;

	/* follow-up fragments don't contain ports, skip all fragments */
	if (ip->frag_off & htons(IP_MF | IP_OFFSET))
		return 0;

	hmark_set_tuple_ports(skb, (ip->ihl * 4) + nhoff, t, info);

	return 0;
}

static unsigned int
hmark_tg_v4(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_hmark_info *info = par->targinfo;
	struct hmark_tuple t;

	memset(&t, 0, sizeof(struct hmark_tuple));

	if (info->flags & XT_HMARK_FLAG(XT_HMARK_CT)) {
		if (hmark_ct_set_htuple(skb, &t, info) < 0)
			return XT_CONTINUE;
	} else {
		if (hmark_pkt_set_htuple_ipv4(skb, &t, info) < 0)
			return XT_CONTINUE;
	}

	skb->mark = hmark_hash(&t, info);
	return XT_CONTINUE;
}

static int hmark_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_hmark_info *info = par->targinfo;

	if (!info->hmodulus) {
		pr_info("xt_HMARK: hash modulus can't be zero\n");
		return -EINVAL;
	}
	if (info->proto_mask &&
	    (info->flags & XT_HMARK_FLAG(XT_HMARK_METHOD_L3))) {
		pr_info("xt_HMARK: proto mask must be zero with L3 mode\n");
		return -EINVAL;
	}
	if (info->flags & XT_HMARK_FLAG(XT_HMARK_SPI_MASK) &&
	    (info->flags & (XT_HMARK_FLAG(XT_HMARK_SPORT_MASK) |
			     XT_HMARK_FLAG(XT_HMARK_DPORT_MASK)))) {
		pr_info("xt_HMARK: spi-mask and port-mask can't be combined\n");
		return -EINVAL;
	}
	if (info->flags & XT_HMARK_FLAG(XT_HMARK_SPI) &&
	    (info->flags & (XT_HMARK_FLAG(XT_HMARK_SPORT) |
			     XT_HMARK_FLAG(XT_HMARK_DPORT)))) {
		pr_info("xt_HMARK: spi-set and port-set can't be combined\n");
		return -EINVAL;
	}
	return 0;
}

static struct xt_target hmark_tg_reg[] __read_mostly = {
	{
		.name		= "HMARK",
		.family		= NFPROTO_IPV4,
		.target		= hmark_tg_v4,
		.targetsize	= sizeof(struct xt_hmark_info),
		.checkentry	= hmark_tg_check,
		.me		= THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name		= "HMARK",
		.family		= NFPROTO_IPV6,
		.target		= hmark_tg_v6,
		.targetsize	= sizeof(struct xt_hmark_info),
		.checkentry	= hmark_tg_check,
		.me		= THIS_MODULE,
	},
#endif
};

static int __init hmark_tg_init(void)
{
	return xt_register_targets(hmark_tg_reg, ARRAY_SIZE(hmark_tg_reg));
}

static void __exit hmark_tg_exit(void)
{
	xt_unregister_targets(hmark_tg_reg, ARRAY_SIZE(hmark_tg_reg));
}

module_init(hmark_tg_init);
module_exit(hmark_tg_exit);
