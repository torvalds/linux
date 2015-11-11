/*
 *	Extension Header handling for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Andi Kleen		<ak@muc.de>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/* Changes:
 *	yoshfuji		: ensure not to overrun while parsing
 *				  tlv options.
 *	Mitsuru KANDA @USAGI and: Remove ipv6_parse_exthdrs().
 *	YOSHIFUJI Hideaki @USAGI  Register inbound extension header
 *				  handlers as inet6_protocol{}.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <net/dst.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#if IS_ENABLED(CONFIG_IPV6_MIP6)
#include <net/xfrm.h>
#endif

#include <asm/uaccess.h>

/*
 *	Parsing tlv encoded headers.
 *
 *	Parsing function "func" returns true, if parsing succeed
 *	and false, if it failed.
 *	It MUST NOT touch skb->h.
 */

struct tlvtype_proc {
	int	type;
	bool	(*func)(struct sk_buff *skb, int offset);
};

/*********************
  Generic functions
 *********************/

/* An unknown option is detected, decide what to do */

static bool ip6_tlvopt_unknown(struct sk_buff *skb, int optoff)
{
	switch ((skb_network_header(skb)[optoff] & 0xC0) >> 6) {
	case 0: /* ignore */
		return true;

	case 1: /* drop packet */
		break;

	case 3: /* Send ICMP if not a multicast address and drop packet */
		/* Actually, it is redundant check. icmp_send
		   will recheck in any case.
		 */
		if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr))
			break;
	case 2: /* send ICMP PARM PROB regardless and drop packet */
		icmpv6_param_prob(skb, ICMPV6_UNK_OPTION, optoff);
		return false;
	}

	kfree_skb(skb);
	return false;
}

/* Parse tlv encoded option header (hop-by-hop or destination) */

static bool ip6_parse_tlv(const struct tlvtype_proc *procs, struct sk_buff *skb)
{
	const struct tlvtype_proc *curr;
	const unsigned char *nh = skb_network_header(skb);
	int off = skb_network_header_len(skb);
	int len = (skb_transport_header(skb)[1] + 1) << 3;
	int padlen = 0;

	if (skb_transport_offset(skb) + len > skb_headlen(skb))
		goto bad;

	off += 2;
	len -= 2;

	while (len > 0) {
		int optlen = nh[off + 1] + 2;
		int i;

		switch (nh[off]) {
		case IPV6_TLV_PAD1:
			optlen = 1;
			padlen++;
			if (padlen > 7)
				goto bad;
			break;

		case IPV6_TLV_PADN:
			/* RFC 2460 states that the purpose of PadN is
			 * to align the containing header to multiples
			 * of 8. 7 is therefore the highest valid value.
			 * See also RFC 4942, Section 2.1.9.5.
			 */
			padlen += optlen;
			if (padlen > 7)
				goto bad;
			/* RFC 4942 recommends receiving hosts to
			 * actively check PadN payload to contain
			 * only zeroes.
			 */
			for (i = 2; i < optlen; i++) {
				if (nh[off + i] != 0)
					goto bad;
			}
			break;

		default: /* Other TLV code so scan list */
			if (optlen > len)
				goto bad;
			for (curr=procs; curr->type >= 0; curr++) {
				if (curr->type == nh[off]) {
					/* type specific length/alignment
					   checks will be performed in the
					   func(). */
					if (curr->func(skb, off) == false)
						return false;
					break;
				}
			}
			if (curr->type < 0) {
				if (ip6_tlvopt_unknown(skb, off) == 0)
					return false;
			}
			padlen = 0;
			break;
		}
		off += optlen;
		len -= optlen;
	}
	/* This case will not be caught by above check since its padding
	 * length is smaller than 7:
	 * 1 byte NH + 1 byte Length + 6 bytes Padding
	 */
	if ((padlen == 6) && ((off - skb_network_header_len(skb)) == 8))
		goto bad;

	if (len == 0)
		return true;
bad:
	kfree_skb(skb);
	return false;
}

/*****************************
  Destination options header.
 *****************************/

#if IS_ENABLED(CONFIG_IPV6_MIP6)
static bool ipv6_dest_hao(struct sk_buff *skb, int optoff)
{
	struct ipv6_destopt_hao *hao;
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct in6_addr tmp_addr;
	int ret;

	if (opt->dsthao) {
		LIMIT_NETDEBUG(KERN_DEBUG "hao duplicated\n");
		goto discard;
	}
	opt->dsthao = opt->dst1;
	opt->dst1 = 0;

	hao = (struct ipv6_destopt_hao *)(skb_network_header(skb) + optoff);

	if (hao->length != 16) {
		LIMIT_NETDEBUG(
			KERN_DEBUG "hao invalid option length = %d\n", hao->length);
		goto discard;
	}

	if (!(ipv6_addr_type(&hao->addr) & IPV6_ADDR_UNICAST)) {
		LIMIT_NETDEBUG(
			KERN_DEBUG "hao is not an unicast addr: %pI6\n", &hao->addr);
		goto discard;
	}

	ret = xfrm6_input_addr(skb, (xfrm_address_t *)&ipv6h->daddr,
			       (xfrm_address_t *)&hao->addr, IPPROTO_DSTOPTS);
	if (unlikely(ret < 0))
		goto discard;

	if (skb_cloned(skb)) {
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			goto discard;

		/* update all variable using below by copied skbuff */
		hao = (struct ipv6_destopt_hao *)(skb_network_header(skb) +
						  optoff);
		ipv6h = ipv6_hdr(skb);
	}

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;

	tmp_addr = ipv6h->saddr;
	ipv6h->saddr = hao->addr;
	hao->addr = tmp_addr;

	if (skb->tstamp.tv64 == 0)
		__net_timestamp(skb);

	return true;

 discard:
	kfree_skb(skb);
	return false;
}
#endif

static const struct tlvtype_proc tlvprocdestopt_lst[] = {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	{
		.type	= IPV6_TLV_HAO,
		.func	= ipv6_dest_hao,
	},
#endif
	{-1,			NULL}
};

static int ipv6_destopt_rcv(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = IP6CB(skb);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	__u16 dstbuf;
#endif
	struct dst_entry *dst = skb_dst(skb);

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + 8) ||
	    !pskb_may_pull(skb, (skb_transport_offset(skb) +
				 ((skb_transport_header(skb)[1] + 1) << 3)))) {
		IP6_INC_STATS_BH(dev_net(dst->dev), ip6_dst_idev(dst),
				 IPSTATS_MIB_INHDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	opt->lastopt = opt->dst1 = skb_network_header_len(skb);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	dstbuf = opt->dst1;
#endif

	if (ip6_parse_tlv(tlvprocdestopt_lst, skb)) {
		skb->transport_header += (skb_transport_header(skb)[1] + 1) << 3;
		opt = IP6CB(skb);
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		opt->nhoff = dstbuf;
#else
		opt->nhoff = opt->dst1;
#endif
		return 1;
	}

	IP6_INC_STATS_BH(dev_net(dst->dev),
			 ip6_dst_idev(dst), IPSTATS_MIB_INHDRERRORS);
	return -1;
}

/********************************
  Routing header.
 ********************************/

/* called with rcu_read_lock() */
static int ipv6_rthdr_rcv(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct in6_addr *addr = NULL;
	struct in6_addr daddr;
	struct inet6_dev *idev;
	int n, i;
	struct ipv6_rt_hdr *hdr;
	struct rt0_hdr *rthdr;
	struct net *net = dev_net(skb->dev);
	int accept_source_route = net->ipv6.devconf_all->accept_source_route;

	idev = __in6_dev_get(skb->dev);
	if (idev && accept_source_route > idev->cnf.accept_source_route)
		accept_source_route = idev->cnf.accept_source_route;

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + 8) ||
	    !pskb_may_pull(skb, (skb_transport_offset(skb) +
				 ((skb_transport_header(skb)[1] + 1) << 3)))) {
		IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
				 IPSTATS_MIB_INHDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	hdr = (struct ipv6_rt_hdr *)skb_transport_header(skb);

	if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) ||
	    skb->pkt_type != PACKET_HOST) {
		IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
				 IPSTATS_MIB_INADDRERRORS);
		kfree_skb(skb);
		return -1;
	}

looped_back:
	if (hdr->segments_left == 0) {
		switch (hdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
		case IPV6_SRCRT_TYPE_2:
			/* Silently discard type 2 header unless it was
			 * processed by own
			 */
			if (!addr) {
				IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
						 IPSTATS_MIB_INADDRERRORS);
				kfree_skb(skb);
				return -1;
			}
			break;
#endif
		default:
			break;
		}

		opt->lastopt = opt->srcrt = skb_network_header_len(skb);
		skb->transport_header += (hdr->hdrlen + 1) << 3;
		opt->dst0 = opt->dst1;
		opt->dst1 = 0;
		opt->nhoff = (&hdr->nexthdr) - skb_network_header(skb);
		return 1;
	}

	switch (hdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	case IPV6_SRCRT_TYPE_2:
		if (accept_source_route < 0)
			goto unknown_rh;
		/* Silently discard invalid RTH type 2 */
		if (hdr->hdrlen != 2 || hdr->segments_left != 1) {
			IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
					 IPSTATS_MIB_INHDRERRORS);
			kfree_skb(skb);
			return -1;
		}
		break;
#endif
	default:
		goto unknown_rh;
	}

	/*
	 *	This is the routing header forwarding algorithm from
	 *	RFC 2460, page 16.
	 */

	n = hdr->hdrlen >> 1;

	if (hdr->segments_left > n) {
		IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
				 IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
				  ((&hdr->segments_left) -
				   skb_network_header(skb)));
		return -1;
	}

	/* We are about to mangle packet header. Be careful!
	   Do not damage packets queued somewhere.
	 */
	if (skb_cloned(skb)) {
		/* the copy is a forwarded packet */
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC)) {
			IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
					 IPSTATS_MIB_OUTDISCARDS);
			kfree_skb(skb);
			return -1;
		}
		hdr = (struct ipv6_rt_hdr *)skb_transport_header(skb);
	}

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;

	i = n - --hdr->segments_left;

	rthdr = (struct rt0_hdr *) hdr;
	addr = rthdr->addr;
	addr += i - 1;

	switch (hdr->type) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	case IPV6_SRCRT_TYPE_2:
		if (xfrm6_input_addr(skb, (xfrm_address_t *)addr,
				     (xfrm_address_t *)&ipv6_hdr(skb)->saddr,
				     IPPROTO_ROUTING) < 0) {
			IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
					 IPSTATS_MIB_INADDRERRORS);
			kfree_skb(skb);
			return -1;
		}
		if (!ipv6_chk_home_addr(dev_net(skb_dst(skb)->dev), addr)) {
			IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
					 IPSTATS_MIB_INADDRERRORS);
			kfree_skb(skb);
			return -1;
		}
		break;
#endif
	default:
		break;
	}

	if (ipv6_addr_is_multicast(addr)) {
		IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
				 IPSTATS_MIB_INADDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	daddr = *addr;
	*addr = ipv6_hdr(skb)->daddr;
	ipv6_hdr(skb)->daddr = daddr;

	skb_dst_drop(skb);
	ip6_route_input(skb);
	if (skb_dst(skb)->error) {
		skb_push(skb, skb->data - skb_network_header(skb));
		dst_input(skb);
		return -1;
	}

	if (skb_dst(skb)->dev->flags&IFF_LOOPBACK) {
		if (ipv6_hdr(skb)->hop_limit <= 1) {
			IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)),
					 IPSTATS_MIB_INHDRERRORS);
			icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
				    0);
			kfree_skb(skb);
			return -1;
		}
		ipv6_hdr(skb)->hop_limit--;
		goto looped_back;
	}

	skb_push(skb, skb->data - skb_network_header(skb));
	dst_input(skb);
	return -1;

unknown_rh:
	IP6_INC_STATS_BH(net, ip6_dst_idev(skb_dst(skb)), IPSTATS_MIB_INHDRERRORS);
	icmpv6_param_prob(skb, ICMPV6_HDR_FIELD,
			  (&hdr->type) - skb_network_header(skb));
	return -1;
}

static const struct inet6_protocol rthdr_protocol = {
	.handler	=	ipv6_rthdr_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct inet6_protocol destopt_protocol = {
	.handler	=	ipv6_destopt_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

static const struct inet6_protocol nodata_protocol = {
	.handler	=	dst_discard,
	.flags		=	INET6_PROTO_NOPOLICY,
};

int __init ipv6_exthdrs_init(void)
{
	int ret;

	ret = inet6_add_protocol(&rthdr_protocol, IPPROTO_ROUTING);
	if (ret)
		goto out;

	ret = inet6_add_protocol(&destopt_protocol, IPPROTO_DSTOPTS);
	if (ret)
		goto out_rthdr;

	ret = inet6_add_protocol(&nodata_protocol, IPPROTO_NONE);
	if (ret)
		goto out_destopt;

out:
	return ret;
out_destopt:
	inet6_del_protocol(&destopt_protocol, IPPROTO_DSTOPTS);
out_rthdr:
	inet6_del_protocol(&rthdr_protocol, IPPROTO_ROUTING);
	goto out;
};

void ipv6_exthdrs_exit(void)
{
	inet6_del_protocol(&nodata_protocol, IPPROTO_NONE);
	inet6_del_protocol(&destopt_protocol, IPPROTO_DSTOPTS);
	inet6_del_protocol(&rthdr_protocol, IPPROTO_ROUTING);
}

/**********************************
  Hop-by-hop options.
 **********************************/

/*
 * Note: we cannot rely on skb_dst(skb) before we assign it in ip6_route_input().
 */
static inline struct inet6_dev *ipv6_skb_idev(struct sk_buff *skb)
{
	return skb_dst(skb) ? ip6_dst_idev(skb_dst(skb)) : __in6_dev_get(skb->dev);
}

static inline struct net *ipv6_skb_net(struct sk_buff *skb)
{
	return skb_dst(skb) ? dev_net(skb_dst(skb)->dev) : dev_net(skb->dev);
}

/* Router Alert as of RFC 2711 */

static bool ipv6_hop_ra(struct sk_buff *skb, int optoff)
{
	const unsigned char *nh = skb_network_header(skb);

	if (nh[optoff + 1] == 2) {
		IP6CB(skb)->flags |= IP6SKB_ROUTERALERT;
		memcpy(&IP6CB(skb)->ra, nh + optoff + 2, sizeof(IP6CB(skb)->ra));
		return true;
	}
	LIMIT_NETDEBUG(KERN_DEBUG "ipv6_hop_ra: wrong RA length %d\n",
		       nh[optoff + 1]);
	kfree_skb(skb);
	return false;
}

/* Jumbo payload */

static bool ipv6_hop_jumbo(struct sk_buff *skb, int optoff)
{
	const unsigned char *nh = skb_network_header(skb);
	struct net *net = ipv6_skb_net(skb);
	u32 pkt_len;

	if (nh[optoff + 1] != 4 || (optoff & 3) != 2) {
		LIMIT_NETDEBUG(KERN_DEBUG "ipv6_hop_jumbo: wrong jumbo opt length/alignment %d\n",
			       nh[optoff+1]);
		IP6_INC_STATS_BH(net, ipv6_skb_idev(skb),
				 IPSTATS_MIB_INHDRERRORS);
		goto drop;
	}

	pkt_len = ntohl(*(__be32 *)(nh + optoff + 2));
	if (pkt_len <= IPV6_MAXPLEN) {
		IP6_INC_STATS_BH(net, ipv6_skb_idev(skb),
				 IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, optoff+2);
		return false;
	}
	if (ipv6_hdr(skb)->payload_len) {
		IP6_INC_STATS_BH(net, ipv6_skb_idev(skb),
				 IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, optoff);
		return false;
	}

	if (pkt_len > skb->len - sizeof(struct ipv6hdr)) {
		IP6_INC_STATS_BH(net, ipv6_skb_idev(skb),
				 IPSTATS_MIB_INTRUNCATEDPKTS);
		goto drop;
	}

	if (pskb_trim_rcsum(skb, pkt_len + sizeof(struct ipv6hdr)))
		goto drop;

	return true;

drop:
	kfree_skb(skb);
	return false;
}

static const struct tlvtype_proc tlvprochopopt_lst[] = {
	{
		.type	= IPV6_TLV_ROUTERALERT,
		.func	= ipv6_hop_ra,
	},
	{
		.type	= IPV6_TLV_JUMBO,
		.func	= ipv6_hop_jumbo,
	},
	{ -1, }
};

int ipv6_parse_hopopts(struct sk_buff *skb)
{
	struct inet6_skb_parm *opt = IP6CB(skb);

	/*
	 * skb_network_header(skb) is equal to skb->data, and
	 * skb_network_header_len(skb) is always equal to
	 * sizeof(struct ipv6hdr) by definition of
	 * hop-by-hop options.
	 */
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr) + 8) ||
	    !pskb_may_pull(skb, (sizeof(struct ipv6hdr) +
				 ((skb_transport_header(skb)[1] + 1) << 3)))) {
		kfree_skb(skb);
		return -1;
	}

	opt->hop = sizeof(struct ipv6hdr);
	if (ip6_parse_tlv(tlvprochopopt_lst, skb)) {
		skb->transport_header += (skb_transport_header(skb)[1] + 1) << 3;
		opt = IP6CB(skb);
		opt->nhoff = sizeof(struct ipv6hdr);
		return 1;
	}
	return -1;
}

/*
 *	Creating outbound headers.
 *
 *	"build" functions work when skb is filled from head to tail (datagram)
 *	"push"	functions work when headers are added from tail to head (tcp)
 *
 *	In both cases we assume, that caller reserved enough room
 *	for headers.
 */

static void ipv6_push_rthdr(struct sk_buff *skb, u8 *proto,
			    struct ipv6_rt_hdr *opt,
			    struct in6_addr **addr_p)
{
	struct rt0_hdr *phdr, *ihdr;
	int hops;

	ihdr = (struct rt0_hdr *) opt;

	phdr = (struct rt0_hdr *) skb_push(skb, (ihdr->rt_hdr.hdrlen + 1) << 3);
	memcpy(phdr, ihdr, sizeof(struct rt0_hdr));

	hops = ihdr->rt_hdr.hdrlen >> 1;

	if (hops > 1)
		memcpy(phdr->addr, ihdr->addr + 1,
		       (hops - 1) * sizeof(struct in6_addr));

	phdr->addr[hops - 1] = **addr_p;
	*addr_p = ihdr->addr;

	phdr->rt_hdr.nexthdr = *proto;
	*proto = NEXTHDR_ROUTING;
}

static void ipv6_push_exthdr(struct sk_buff *skb, u8 *proto, u8 type, struct ipv6_opt_hdr *opt)
{
	struct ipv6_opt_hdr *h = (struct ipv6_opt_hdr *)skb_push(skb, ipv6_optlen(opt));

	memcpy(h, opt, ipv6_optlen(opt));
	h->nexthdr = *proto;
	*proto = type;
}

void ipv6_push_nfrag_opts(struct sk_buff *skb, struct ipv6_txoptions *opt,
			  u8 *proto,
			  struct in6_addr **daddr)
{
	if (opt->srcrt) {
		ipv6_push_rthdr(skb, proto, opt->srcrt, daddr);
		/*
		 * IPV6_RTHDRDSTOPTS is ignored
		 * unless IPV6_RTHDR is set (RFC3542).
		 */
		if (opt->dst0opt)
			ipv6_push_exthdr(skb, proto, NEXTHDR_DEST, opt->dst0opt);
	}
	if (opt->hopopt)
		ipv6_push_exthdr(skb, proto, NEXTHDR_HOP, opt->hopopt);
}
EXPORT_SYMBOL(ipv6_push_nfrag_opts);

void ipv6_push_frag_opts(struct sk_buff *skb, struct ipv6_txoptions *opt, u8 *proto)
{
	if (opt->dst1opt)
		ipv6_push_exthdr(skb, proto, NEXTHDR_DEST, opt->dst1opt);
}

struct ipv6_txoptions *
ipv6_dup_options(struct sock *sk, struct ipv6_txoptions *opt)
{
	struct ipv6_txoptions *opt2;

	opt2 = sock_kmalloc(sk, opt->tot_len, GFP_ATOMIC);
	if (opt2) {
		long dif = (char *)opt2 - (char *)opt;
		memcpy(opt2, opt, opt->tot_len);
		if (opt2->hopopt)
			*((char **)&opt2->hopopt) += dif;
		if (opt2->dst0opt)
			*((char **)&opt2->dst0opt) += dif;
		if (opt2->dst1opt)
			*((char **)&opt2->dst1opt) += dif;
		if (opt2->srcrt)
			*((char **)&opt2->srcrt) += dif;
	}
	return opt2;
}
EXPORT_SYMBOL_GPL(ipv6_dup_options);

static int ipv6_renew_option(void *ohdr,
			     struct ipv6_opt_hdr __user *newopt, int newoptlen,
			     int inherit,
			     struct ipv6_opt_hdr **hdr,
			     char **p)
{
	if (inherit) {
		if (ohdr) {
			memcpy(*p, ohdr, ipv6_optlen((struct ipv6_opt_hdr *)ohdr));
			*hdr = (struct ipv6_opt_hdr *)*p;
			*p += CMSG_ALIGN(ipv6_optlen(*hdr));
		}
	} else {
		if (newopt) {
			if (copy_from_user(*p, newopt, newoptlen))
				return -EFAULT;
			*hdr = (struct ipv6_opt_hdr *)*p;
			if (ipv6_optlen(*hdr) > newoptlen)
				return -EINVAL;
			*p += CMSG_ALIGN(newoptlen);
		}
	}
	return 0;
}

struct ipv6_txoptions *
ipv6_renew_options(struct sock *sk, struct ipv6_txoptions *opt,
		   int newtype,
		   struct ipv6_opt_hdr __user *newopt, int newoptlen)
{
	int tot_len = 0;
	char *p;
	struct ipv6_txoptions *opt2;
	int err;

	if (opt) {
		if (newtype != IPV6_HOPOPTS && opt->hopopt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->hopopt));
		if (newtype != IPV6_RTHDRDSTOPTS && opt->dst0opt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->dst0opt));
		if (newtype != IPV6_RTHDR && opt->srcrt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->srcrt));
		if (newtype != IPV6_DSTOPTS && opt->dst1opt)
			tot_len += CMSG_ALIGN(ipv6_optlen(opt->dst1opt));
	}

	if (newopt && newoptlen)
		tot_len += CMSG_ALIGN(newoptlen);

	if (!tot_len)
		return NULL;

	tot_len += sizeof(*opt2);
	opt2 = sock_kmalloc(sk, tot_len, GFP_ATOMIC);
	if (!opt2)
		return ERR_PTR(-ENOBUFS);

	memset(opt2, 0, tot_len);

	opt2->tot_len = tot_len;
	p = (char *)(opt2 + 1);

	err = ipv6_renew_option(opt ? opt->hopopt : NULL, newopt, newoptlen,
				newtype != IPV6_HOPOPTS,
				&opt2->hopopt, &p);
	if (err)
		goto out;

	err = ipv6_renew_option(opt ? opt->dst0opt : NULL, newopt, newoptlen,
				newtype != IPV6_RTHDRDSTOPTS,
				&opt2->dst0opt, &p);
	if (err)
		goto out;

	err = ipv6_renew_option(opt ? opt->srcrt : NULL, newopt, newoptlen,
				newtype != IPV6_RTHDR,
				(struct ipv6_opt_hdr **)&opt2->srcrt, &p);
	if (err)
		goto out;

	err = ipv6_renew_option(opt ? opt->dst1opt : NULL, newopt, newoptlen,
				newtype != IPV6_DSTOPTS,
				&opt2->dst1opt, &p);
	if (err)
		goto out;

	opt2->opt_nflen = (opt2->hopopt ? ipv6_optlen(opt2->hopopt) : 0) +
			  (opt2->dst0opt ? ipv6_optlen(opt2->dst0opt) : 0) +
			  (opt2->srcrt ? ipv6_optlen(opt2->srcrt) : 0);
	opt2->opt_flen = (opt2->dst1opt ? ipv6_optlen(opt2->dst1opt) : 0);

	return opt2;
out:
	sock_kfree_s(sk, opt2, opt2->tot_len);
	return ERR_PTR(err);
}

struct ipv6_txoptions *ipv6_fixup_options(struct ipv6_txoptions *opt_space,
					  struct ipv6_txoptions *opt)
{
	/*
	 * ignore the dest before srcrt unless srcrt is being included.
	 * --yoshfuji
	 */
	if (opt && opt->dst0opt && !opt->srcrt) {
		if (opt_space != opt) {
			memcpy(opt_space, opt, sizeof(*opt_space));
			opt = opt_space;
		}
		opt->opt_nflen -= ipv6_optlen(opt->dst0opt);
		opt->dst0opt = NULL;
	}

	return opt;
}
EXPORT_SYMBOL_GPL(ipv6_fixup_options);

/**
 * fl6_update_dst - update flowi destination address with info given
 *                  by srcrt option, if any.
 *
 * @fl6: flowi6 for which daddr is to be updated
 * @opt: struct ipv6_txoptions in which to look for srcrt opt
 * @orig: copy of original daddr address if modified
 *
 * Returns NULL if no txoptions or no srcrt, otherwise returns orig
 * and initial value of fl6->daddr set in orig
 */
struct in6_addr *fl6_update_dst(struct flowi6 *fl6,
				const struct ipv6_txoptions *opt,
				struct in6_addr *orig)
{
	if (!opt || !opt->srcrt)
		return NULL;

	*orig = fl6->daddr;
	fl6->daddr = *((struct rt0_hdr *)opt->srcrt)->addr;
	return orig;
}
EXPORT_SYMBOL_GPL(fl6_update_dst);
