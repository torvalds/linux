/*
 *	Extension Header handling for IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Andi Kleen		<ak@muc.de>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	$Id: exthdrs.c,v 1.13 2001/06/19 15:58:56 davem Exp $
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
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <asm/uaccess.h>

/*
 *	Parsing tlv encoded headers.
 *
 *	Parsing function "func" returns 1, if parsing succeed
 *	and 0, if it failed.
 *	It MUST NOT touch skb->h.
 */

struct tlvtype_proc {
	int	type;
	int	(*func)(struct sk_buff *skb, int offset);
};

/*********************
  Generic functions
 *********************/

/* An unknown option is detected, decide what to do */

static int ip6_tlvopt_unknown(struct sk_buff *skb, int optoff)
{
	switch ((skb->nh.raw[optoff] & 0xC0) >> 6) {
	case 0: /* ignore */
		return 1;

	case 1: /* drop packet */
		break;

	case 3: /* Send ICMP if not a multicast address and drop packet */
		/* Actually, it is redundant check. icmp_send
		   will recheck in any case.
		 */
		if (ipv6_addr_is_multicast(&skb->nh.ipv6h->daddr))
			break;
	case 2: /* send ICMP PARM PROB regardless and drop packet */
		icmpv6_param_prob(skb, ICMPV6_UNK_OPTION, optoff);
		return 0;
	};

	kfree_skb(skb);
	return 0;
}

/* Parse tlv encoded option header (hop-by-hop or destination) */

static int ip6_parse_tlv(struct tlvtype_proc *procs, struct sk_buff *skb)
{
	struct tlvtype_proc *curr;
	int off = skb->h.raw - skb->nh.raw;
	int len = ((skb->h.raw[1]+1)<<3);

	if ((skb->h.raw + len) - skb->data > skb_headlen(skb))
		goto bad;

	off += 2;
	len -= 2;

	while (len > 0) {
		int optlen = skb->nh.raw[off+1]+2;

		switch (skb->nh.raw[off]) {
		case IPV6_TLV_PAD0:
			optlen = 1;
			break;

		case IPV6_TLV_PADN:
			break;

		default: /* Other TLV code so scan list */
			if (optlen > len)
				goto bad;
			for (curr=procs; curr->type >= 0; curr++) {
				if (curr->type == skb->nh.raw[off]) {
					/* type specific length/alignment 
					   checks will be performed in the 
					   func(). */
					if (curr->func(skb, off) == 0)
						return 0;
					break;
				}
			}
			if (curr->type < 0) {
				if (ip6_tlvopt_unknown(skb, off) == 0)
					return 0;
			}
			break;
		}
		off += optlen;
		len -= optlen;
	}
	if (len == 0)
		return 1;
bad:
	kfree_skb(skb);
	return 0;
}

/*****************************
  Destination options header.
 *****************************/

static struct tlvtype_proc tlvprocdestopt_lst[] = {
	/* No destination options are defined now */
	{-1,			NULL}
};

static int ipv6_destopt_rcv(struct sk_buff **skbp)
{
	struct sk_buff *skb = *skbp;
	struct inet6_skb_parm *opt = IP6CB(skb);

	if (!pskb_may_pull(skb, (skb->h.raw-skb->data)+8) ||
	    !pskb_may_pull(skb, (skb->h.raw-skb->data)+((skb->h.raw[1]+1)<<3))) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	opt->lastopt = skb->h.raw - skb->nh.raw;
	opt->dst1 = skb->h.raw - skb->nh.raw;

	if (ip6_parse_tlv(tlvprocdestopt_lst, skb)) {
		skb->h.raw += ((skb->h.raw[1]+1)<<3);
		opt->nhoff = opt->dst1;
		return 1;
	}

	IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
	return -1;
}

static struct inet6_protocol destopt_protocol = {
	.handler	=	ipv6_destopt_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

void __init ipv6_destopt_init(void)
{
	if (inet6_add_protocol(&destopt_protocol, IPPROTO_DSTOPTS) < 0)
		printk(KERN_ERR "ipv6_destopt_init: Could not register protocol\n");
}

/********************************
  NONE header. No data in packet.
 ********************************/

static int ipv6_nodata_rcv(struct sk_buff **skbp)
{
	struct sk_buff *skb = *skbp;

	kfree_skb(skb);
	return 0;
}

static struct inet6_protocol nodata_protocol = {
	.handler	=	ipv6_nodata_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

void __init ipv6_nodata_init(void)
{
	if (inet6_add_protocol(&nodata_protocol, IPPROTO_NONE) < 0)
		printk(KERN_ERR "ipv6_nodata_init: Could not register protocol\n");
}

/********************************
  Routing header.
 ********************************/

static int ipv6_rthdr_rcv(struct sk_buff **skbp)
{
	struct sk_buff *skb = *skbp;
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct in6_addr *addr;
	struct in6_addr daddr;
	int n, i;

	struct ipv6_rt_hdr *hdr;
	struct rt0_hdr *rthdr;

	if (!pskb_may_pull(skb, (skb->h.raw-skb->data)+8) ||
	    !pskb_may_pull(skb, (skb->h.raw-skb->data)+((skb->h.raw[1]+1)<<3))) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	hdr = (struct ipv6_rt_hdr *) skb->h.raw;

	if (ipv6_addr_is_multicast(&skb->nh.ipv6h->daddr) ||
	    skb->pkt_type != PACKET_HOST) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INADDRERRORS);
		kfree_skb(skb);
		return -1;
	}

looped_back:
	if (hdr->segments_left == 0) {
		opt->lastopt = skb->h.raw - skb->nh.raw;
		opt->srcrt = skb->h.raw - skb->nh.raw;
		skb->h.raw += (hdr->hdrlen + 1) << 3;
		opt->dst0 = opt->dst1;
		opt->dst1 = 0;
		opt->nhoff = (&hdr->nexthdr) - skb->nh.raw;
		return 1;
	}

	if (hdr->type != IPV6_SRCRT_TYPE_0) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, (&hdr->type) - skb->nh.raw);
		return -1;
	}
	
	if (hdr->hdrlen & 0x01) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, (&hdr->hdrlen) - skb->nh.raw);
		return -1;
	}

	/*
	 *	This is the routing header forwarding algorithm from
	 *	RFC 2460, page 16.
	 */

	n = hdr->hdrlen >> 1;

	if (hdr->segments_left > n) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, (&hdr->segments_left) - skb->nh.raw);
		return -1;
	}

	/* We are about to mangle packet header. Be careful!
	   Do not damage packets queued somewhere.
	 */
	if (skb_cloned(skb)) {
		struct sk_buff *skb2 = skb_copy(skb, GFP_ATOMIC);
		kfree_skb(skb);
		/* the copy is a forwarded packet */
		if (skb2 == NULL) {
			IP6_INC_STATS_BH(IPSTATS_MIB_OUTDISCARDS);	
			return -1;
		}
		*skbp = skb = skb2;
		opt = IP6CB(skb2);
		hdr = (struct ipv6_rt_hdr *) skb2->h.raw;
	}

	if (skb->ip_summed == CHECKSUM_HW)
		skb->ip_summed = CHECKSUM_NONE;

	i = n - --hdr->segments_left;

	rthdr = (struct rt0_hdr *) hdr;
	addr = rthdr->addr;
	addr += i - 1;

	if (ipv6_addr_is_multicast(addr)) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INADDRERRORS);
		kfree_skb(skb);
		return -1;
	}

	ipv6_addr_copy(&daddr, addr);
	ipv6_addr_copy(addr, &skb->nh.ipv6h->daddr);
	ipv6_addr_copy(&skb->nh.ipv6h->daddr, &daddr);

	dst_release(xchg(&skb->dst, NULL));
	ip6_route_input(skb);
	if (skb->dst->error) {
		skb_push(skb, skb->data - skb->nh.raw);
		dst_input(skb);
		return -1;
	}

	if (skb->dst->dev->flags&IFF_LOOPBACK) {
		if (skb->nh.ipv6h->hop_limit <= 1) {
			IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
			icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
				    0, skb->dev);
			kfree_skb(skb);
			return -1;
		}
		skb->nh.ipv6h->hop_limit--;
		goto looped_back;
	}

	skb_push(skb, skb->data - skb->nh.raw);
	dst_input(skb);
	return -1;
}

static struct inet6_protocol rthdr_protocol = {
	.handler	=	ipv6_rthdr_rcv,
	.flags		=	INET6_PROTO_NOPOLICY,
};

void __init ipv6_rthdr_init(void)
{
	if (inet6_add_protocol(&rthdr_protocol, IPPROTO_ROUTING) < 0)
		printk(KERN_ERR "ipv6_rthdr_init: Could not register protocol\n");
};

/*
   This function inverts received rthdr.
   NOTE: specs allow to make it automatically only if
   packet authenticated.

   I will not discuss it here (though, I am really pissed off at
   this stupid requirement making rthdr idea useless)

   Actually, it creates severe problems  for us.
   Embryonic requests has no associated sockets,
   so that user have no control over it and
   cannot not only to set reply options, but
   even to know, that someone wants to connect
   without success. :-(

   For now we need to test the engine, so that I created
   temporary (or permanent) backdoor.
   If listening socket set IPV6_RTHDR to 2, then we invert header.
                                                   --ANK (980729)
 */

struct ipv6_txoptions *
ipv6_invert_rthdr(struct sock *sk, struct ipv6_rt_hdr *hdr)
{
	/* Received rthdr:

	   [ H1 -> H2 -> ... H_prev ]  daddr=ME

	   Inverted result:
	   [ H_prev -> ... -> H1 ] daddr =sender

	   Note, that IP output engine will rewrite this rthdr
	   by rotating it left by one addr.
	 */

	int n, i;
	struct rt0_hdr *rthdr = (struct rt0_hdr*)hdr;
	struct rt0_hdr *irthdr;
	struct ipv6_txoptions *opt;
	int hdrlen = ipv6_optlen(hdr);

	if (hdr->segments_left ||
	    hdr->type != IPV6_SRCRT_TYPE_0 ||
	    hdr->hdrlen & 0x01)
		return NULL;

	n = hdr->hdrlen >> 1;
	opt = sock_kmalloc(sk, sizeof(*opt) + hdrlen, GFP_ATOMIC);
	if (opt == NULL)
		return NULL;
	memset(opt, 0, sizeof(*opt));
	opt->tot_len = sizeof(*opt) + hdrlen;
	opt->srcrt = (void*)(opt+1);
	opt->opt_nflen = hdrlen;

	memcpy(opt->srcrt, hdr, sizeof(*hdr));
	irthdr = (struct rt0_hdr*)opt->srcrt;
	irthdr->reserved = 0;
	opt->srcrt->segments_left = n;
	for (i=0; i<n; i++)
		memcpy(irthdr->addr+i, rthdr->addr+(n-1-i), 16);
	return opt;
}

EXPORT_SYMBOL_GPL(ipv6_invert_rthdr);

/**********************************
  Hop-by-hop options.
 **********************************/

/* Router Alert as of RFC 2711 */

static int ipv6_hop_ra(struct sk_buff *skb, int optoff)
{
	if (skb->nh.raw[optoff+1] == 2) {
		IP6CB(skb)->ra = optoff;
		return 1;
	}
	LIMIT_NETDEBUG(KERN_DEBUG "ipv6_hop_ra: wrong RA length %d\n",
	               skb->nh.raw[optoff+1]);
	kfree_skb(skb);
	return 0;
}

/* Jumbo payload */

static int ipv6_hop_jumbo(struct sk_buff *skb, int optoff)
{
	u32 pkt_len;

	if (skb->nh.raw[optoff+1] != 4 || (optoff&3) != 2) {
		LIMIT_NETDEBUG(KERN_DEBUG "ipv6_hop_jumbo: wrong jumbo opt length/alignment %d\n",
		               skb->nh.raw[optoff+1]);
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		goto drop;
	}

	pkt_len = ntohl(*(u32*)(skb->nh.raw+optoff+2));
	if (pkt_len <= IPV6_MAXPLEN) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, optoff+2);
		return 0;
	}
	if (skb->nh.ipv6h->payload_len) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INHDRERRORS);
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, optoff);
		return 0;
	}

	if (pkt_len > skb->len - sizeof(struct ipv6hdr)) {
		IP6_INC_STATS_BH(IPSTATS_MIB_INTRUNCATEDPKTS);
		goto drop;
	}

	if (pskb_trim_rcsum(skb, pkt_len + sizeof(struct ipv6hdr)))
		goto drop;

	return 1;

drop:
	kfree_skb(skb);
	return 0;
}

static struct tlvtype_proc tlvprochopopt_lst[] = {
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

int ipv6_parse_hopopts(struct sk_buff *skb, int nhoff)
{
	struct inet6_skb_parm *opt = IP6CB(skb);

	/*
	 * skb->nh.raw is equal to skb->data, and
	 * skb->h.raw - skb->nh.raw is always equal to
	 * sizeof(struct ipv6hdr) by definition of
	 * hop-by-hop options.
	 */
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr) + 8) ||
	    !pskb_may_pull(skb, sizeof(struct ipv6hdr) + ((skb->h.raw[1] + 1) << 3))) {
		kfree_skb(skb);
		return -1;
	}

	opt->hop = sizeof(struct ipv6hdr);
	if (ip6_parse_tlv(tlvprochopopt_lst, skb)) {
		skb->h.raw += (skb->h.raw[1]+1)<<3;
		opt->nhoff = sizeof(struct ipv6hdr);
		return sizeof(struct ipv6hdr);
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

	ipv6_addr_copy(phdr->addr + (hops - 1), *addr_p);
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
		long dif = (char*)opt2 - (char*)opt;
		memcpy(opt2, opt, opt->tot_len);
		if (opt2->hopopt)
			*((char**)&opt2->hopopt) += dif;
		if (opt2->dst0opt)
			*((char**)&opt2->dst0opt) += dif;
		if (opt2->dst1opt)
			*((char**)&opt2->dst1opt) += dif;
		if (opt2->srcrt)
			*((char**)&opt2->srcrt) += dif;
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
			*p += CMSG_ALIGN(ipv6_optlen(*(struct ipv6_opt_hdr **)hdr));
		}
	} else {
		if (newopt) {
			if (copy_from_user(*p, newopt, newoptlen))
				return -EFAULT;
			*hdr = (struct ipv6_opt_hdr *)*p;
			if (ipv6_optlen(*(struct ipv6_opt_hdr **)hdr) > newoptlen)
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

	if (newtype != IPV6_HOPOPTS && opt->hopopt)
		tot_len += CMSG_ALIGN(ipv6_optlen(opt->hopopt));
	if (newtype != IPV6_RTHDRDSTOPTS && opt->dst0opt)
		tot_len += CMSG_ALIGN(ipv6_optlen(opt->dst0opt));
	if (newtype != IPV6_RTHDR && opt->srcrt)
		tot_len += CMSG_ALIGN(ipv6_optlen(opt->srcrt));
	if (newtype != IPV6_DSTOPTS && opt->dst1opt)
		tot_len += CMSG_ALIGN(ipv6_optlen(opt->dst1opt));
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

	err = ipv6_renew_option(opt->hopopt, newopt, newoptlen,
				newtype != IPV6_HOPOPTS,
				&opt2->hopopt, &p);
	if (err)
		goto out;

	err = ipv6_renew_option(opt->dst0opt, newopt, newoptlen,
				newtype != IPV6_RTHDRDSTOPTS,
				&opt2->dst0opt, &p);
	if (err)
		goto out;

	err = ipv6_renew_option(opt->srcrt, newopt, newoptlen,
				newtype != IPV6_RTHDR,
				(struct ipv6_opt_hdr **)opt2->srcrt, &p);
	if (err)
		goto out;

	err = ipv6_renew_option(opt->dst1opt, newopt, newoptlen,
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

