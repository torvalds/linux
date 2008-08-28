/*
 *	IPv6 output functions
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/net/ipv4/ip_output.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Changes:
 *	A.N.Kuznetsov	:	airthmetics in fragmentation.
 *				extension headers are implemented.
 *				route changes now work.
 *				ip6_forward does not confuse sniffers.
 *				etc.
 *
 *      H. von Brand    :       Added missing #include <linux/string.h>
 *	Imran Patel	: 	frag id should be in NBO
 *      Kazunori MIYAZAWA @USAGI
 *			:       add ip6_append_data and related functions
 *				for datagram xmit
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/route.h>
#include <linux/module.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/rawv6.h>
#include <net/icmp.h>
#include <net/xfrm.h>
#include <net/checksum.h>
#include <linux/mroute6.h>

static int ip6_fragment(struct sk_buff *skb, int (*output)(struct sk_buff *));

static __inline__ void ipv6_select_ident(struct sk_buff *skb, struct frag_hdr *fhdr)
{
	static u32 ipv6_fragmentation_id = 1;
	static DEFINE_SPINLOCK(ip6_id_lock);

	spin_lock_bh(&ip6_id_lock);
	fhdr->identification = htonl(ipv6_fragmentation_id);
	if (++ipv6_fragmentation_id == 0)
		ipv6_fragmentation_id = 1;
	spin_unlock_bh(&ip6_id_lock);
}

int __ip6_local_out(struct sk_buff *skb)
{
	int len;

	len = skb->len - sizeof(struct ipv6hdr);
	if (len > IPV6_MAXPLEN)
		len = 0;
	ipv6_hdr(skb)->payload_len = htons(len);

	return nf_hook(PF_INET6, NF_INET_LOCAL_OUT, skb, NULL, skb->dst->dev,
		       dst_output);
}

int ip6_local_out(struct sk_buff *skb)
{
	int err;

	err = __ip6_local_out(skb);
	if (likely(err == 1))
		err = dst_output(skb);

	return err;
}
EXPORT_SYMBOL_GPL(ip6_local_out);

static int ip6_output_finish(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;

	if (dst->hh)
		return neigh_hh_output(dst->hh, skb);
	else if (dst->neighbour)
		return dst->neighbour->output(skb);

	IP6_INC_STATS_BH(ip6_dst_idev(dst), IPSTATS_MIB_OUTNOROUTES);
	kfree_skb(skb);
	return -EINVAL;

}

/* dev_loopback_xmit for use with netfilter. */
static int ip6_dev_loopback_xmit(struct sk_buff *newskb)
{
	skb_reset_mac_header(newskb);
	__skb_pull(newskb, skb_network_offset(newskb));
	newskb->pkt_type = PACKET_LOOPBACK;
	newskb->ip_summed = CHECKSUM_UNNECESSARY;
	WARN_ON(!newskb->dst);

	netif_rx(newskb);
	return 0;
}


static int ip6_output2(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct net_device *dev = dst->dev;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr)) {
		struct ipv6_pinfo* np = skb->sk ? inet6_sk(skb->sk) : NULL;
		struct inet6_dev *idev = ip6_dst_idev(skb->dst);

		if (!(dev->flags & IFF_LOOPBACK) && (!np || np->mc_loop) &&
		    ((mroute6_socket && !(IP6CB(skb)->flags & IP6SKB_FORWARDED)) ||
		     ipv6_chk_mcast_addr(dev, &ipv6_hdr(skb)->daddr,
					 &ipv6_hdr(skb)->saddr))) {
			struct sk_buff *newskb = skb_clone(skb, GFP_ATOMIC);

			/* Do not check for IFF_ALLMULTI; multicast routing
			   is not supported in any case.
			 */
			if (newskb)
				NF_HOOK(PF_INET6, NF_INET_POST_ROUTING, newskb,
					NULL, newskb->dev,
					ip6_dev_loopback_xmit);

			if (ipv6_hdr(skb)->hop_limit == 0) {
				IP6_INC_STATS(idev, IPSTATS_MIB_OUTDISCARDS);
				kfree_skb(skb);
				return 0;
			}
		}

		IP6_INC_STATS(idev, IPSTATS_MIB_OUTMCASTPKTS);
	}

	return NF_HOOK(PF_INET6, NF_INET_POST_ROUTING, skb, NULL, skb->dev,
		       ip6_output_finish);
}

static inline int ip6_skb_dst_mtu(struct sk_buff *skb)
{
	struct ipv6_pinfo *np = skb->sk ? inet6_sk(skb->sk) : NULL;

	return (np && np->pmtudisc == IPV6_PMTUDISC_PROBE) ?
	       skb->dst->dev->mtu : dst_mtu(skb->dst);
}

int ip6_output(struct sk_buff *skb)
{
	struct inet6_dev *idev = ip6_dst_idev(skb->dst);
	if (unlikely(idev->cnf.disable_ipv6)) {
		IP6_INC_STATS(idev, IPSTATS_MIB_OUTDISCARDS);
		kfree_skb(skb);
		return 0;
	}

	if ((skb->len > ip6_skb_dst_mtu(skb) && !skb_is_gso(skb)) ||
				dst_allfrag(skb->dst))
		return ip6_fragment(skb, ip6_output2);
	else
		return ip6_output2(skb);
}

/*
 *	xmit an sk_buff (used by TCP)
 */

int ip6_xmit(struct sock *sk, struct sk_buff *skb, struct flowi *fl,
	     struct ipv6_txoptions *opt, int ipfragok)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *first_hop = &fl->fl6_dst;
	struct dst_entry *dst = skb->dst;
	struct ipv6hdr *hdr;
	u8  proto = fl->proto;
	int seg_len = skb->len;
	int hlimit, tclass;
	u32 mtu;

	if (opt) {
		unsigned int head_room;

		/* First: exthdrs may take lots of space (~8K for now)
		   MAX_HEADER is not enough.
		 */
		head_room = opt->opt_nflen + opt->opt_flen;
		seg_len += head_room;
		head_room += sizeof(struct ipv6hdr) + LL_RESERVED_SPACE(dst->dev);

		if (skb_headroom(skb) < head_room) {
			struct sk_buff *skb2 = skb_realloc_headroom(skb, head_room);
			if (skb2 == NULL) {
				IP6_INC_STATS(ip6_dst_idev(skb->dst),
					      IPSTATS_MIB_OUTDISCARDS);
				kfree_skb(skb);
				return -ENOBUFS;
			}
			kfree_skb(skb);
			skb = skb2;
			if (sk)
				skb_set_owner_w(skb, sk);
		}
		if (opt->opt_flen)
			ipv6_push_frag_opts(skb, opt, &proto);
		if (opt->opt_nflen)
			ipv6_push_nfrag_opts(skb, opt, &proto, &first_hop);
	}

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	hdr = ipv6_hdr(skb);

	/* Allow local fragmentation. */
	if (ipfragok)
		skb->local_df = 1;

	/*
	 *	Fill in the IPv6 header
	 */

	hlimit = -1;
	if (np)
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = ip6_dst_hoplimit(dst);

	tclass = -1;
	if (np)
		tclass = np->tclass;
	if (tclass < 0)
		tclass = 0;

	*(__be32 *)hdr = htonl(0x60000000 | (tclass << 20)) | fl->fl6_flowlabel;

	hdr->payload_len = htons(seg_len);
	hdr->nexthdr = proto;
	hdr->hop_limit = hlimit;

	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, first_hop);

	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;

	mtu = dst_mtu(dst);
	if ((skb->len <= mtu) || skb->local_df || skb_is_gso(skb)) {
		IP6_INC_STATS(ip6_dst_idev(skb->dst),
			      IPSTATS_MIB_OUTREQUESTS);
		return NF_HOOK(PF_INET6, NF_INET_LOCAL_OUT, skb, NULL, dst->dev,
				dst_output);
	}

	if (net_ratelimit())
		printk(KERN_DEBUG "IPv6: sending pkt_too_big to self\n");
	skb->dev = dst->dev;
	icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, skb->dev);
	IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_FRAGFAILS);
	kfree_skb(skb);
	return -EMSGSIZE;
}

EXPORT_SYMBOL(ip6_xmit);

/*
 *	To avoid extra problems ND packets are send through this
 *	routine. It's code duplication but I really want to avoid
 *	extra checks since ipv6_build_header is used by TCP (which
 *	is for us performance critical)
 */

int ip6_nd_hdr(struct sock *sk, struct sk_buff *skb, struct net_device *dev,
	       const struct in6_addr *saddr, const struct in6_addr *daddr,
	       int proto, int len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6hdr *hdr;
	int totlen;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	totlen = len + sizeof(struct ipv6hdr);

	skb_reset_network_header(skb);
	skb_put(skb, sizeof(struct ipv6hdr));
	hdr = ipv6_hdr(skb);

	*(__be32*)hdr = htonl(0x60000000);

	hdr->payload_len = htons(len);
	hdr->nexthdr = proto;
	hdr->hop_limit = np->hop_limit;

	ipv6_addr_copy(&hdr->saddr, saddr);
	ipv6_addr_copy(&hdr->daddr, daddr);

	return 0;
}

static int ip6_call_ra_chain(struct sk_buff *skb, int sel)
{
	struct ip6_ra_chain *ra;
	struct sock *last = NULL;

	read_lock(&ip6_ra_lock);
	for (ra = ip6_ra_chain; ra; ra = ra->next) {
		struct sock *sk = ra->sk;
		if (sk && ra->sel == sel &&
		    (!sk->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == skb->dev->ifindex)) {
			if (last) {
				struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2)
					rawv6_rcv(last, skb2);
			}
			last = sk;
		}
	}

	if (last) {
		rawv6_rcv(last, skb);
		read_unlock(&ip6_ra_lock);
		return 1;
	}
	read_unlock(&ip6_ra_lock);
	return 0;
}

static int ip6_forward_proxy_check(struct sk_buff *skb)
{
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	u8 nexthdr = hdr->nexthdr;
	int offset;

	if (ipv6_ext_hdr(nexthdr)) {
		offset = ipv6_skip_exthdr(skb, sizeof(*hdr), &nexthdr);
		if (offset < 0)
			return 0;
	} else
		offset = sizeof(struct ipv6hdr);

	if (nexthdr == IPPROTO_ICMPV6) {
		struct icmp6hdr *icmp6;

		if (!pskb_may_pull(skb, (skb_network_header(skb) +
					 offset + 1 - skb->data)))
			return 0;

		icmp6 = (struct icmp6hdr *)(skb_network_header(skb) + offset);

		switch (icmp6->icmp6_type) {
		case NDISC_ROUTER_SOLICITATION:
		case NDISC_ROUTER_ADVERTISEMENT:
		case NDISC_NEIGHBOUR_SOLICITATION:
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
		case NDISC_REDIRECT:
			/* For reaction involving unicast neighbor discovery
			 * message destined to the proxied address, pass it to
			 * input function.
			 */
			return 1;
		default:
			break;
		}
	}

	/*
	 * The proxying router can't forward traffic sent to a link-local
	 * address, so signal the sender and discard the packet. This
	 * behavior is clarified by the MIPv6 specification.
	 */
	if (ipv6_addr_type(&hdr->daddr) & IPV6_ADDR_LINKLOCAL) {
		dst_link_failure(skb);
		return -1;
	}

	return 0;
}

static inline int ip6_forward_finish(struct sk_buff *skb)
{
	return dst_output(skb);
}

int ip6_forward(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct net *net = dev_net(dst->dev);

	if (net->ipv6.devconf_all->forwarding == 0)
		goto error;

	if (skb_warn_if_lro(skb))
		goto drop;

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_FWD, skb)) {
		IP6_INC_STATS(ip6_dst_idev(dst), IPSTATS_MIB_INDISCARDS);
		goto drop;
	}

	skb_forward_csum(skb);

	/*
	 *	We DO NOT make any processing on
	 *	RA packets, pushing them to user level AS IS
	 *	without ane WARRANTY that application will be able
	 *	to interpret them. The reason is that we
	 *	cannot make anything clever here.
	 *
	 *	We are not end-node, so that if packet contains
	 *	AH/ESP, we cannot make anything.
	 *	Defragmentation also would be mistake, RA packets
	 *	cannot be fragmented, because there is no warranty
	 *	that different fragments will go along one path. --ANK
	 */
	if (opt->ra) {
		u8 *ptr = skb_network_header(skb) + opt->ra;
		if (ip6_call_ra_chain(skb, (ptr[2]<<8) + ptr[3]))
			return 0;
	}

	/*
	 *	check and decrement ttl
	 */
	if (hdr->hop_limit <= 1) {
		/* Force OUTPUT device used as source address */
		skb->dev = dst->dev;
		icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
			    0, skb->dev);
		IP6_INC_STATS_BH(ip6_dst_idev(dst), IPSTATS_MIB_INHDRERRORS);

		kfree_skb(skb);
		return -ETIMEDOUT;
	}

	/* XXX: idev->cnf.proxy_ndp? */
	if (net->ipv6.devconf_all->proxy_ndp &&
	    pneigh_lookup(&nd_tbl, net, &hdr->daddr, skb->dev, 0)) {
		int proxied = ip6_forward_proxy_check(skb);
		if (proxied > 0)
			return ip6_input(skb);
		else if (proxied < 0) {
			IP6_INC_STATS(ip6_dst_idev(dst), IPSTATS_MIB_INDISCARDS);
			goto drop;
		}
	}

	if (!xfrm6_route_forward(skb)) {
		IP6_INC_STATS(ip6_dst_idev(dst), IPSTATS_MIB_INDISCARDS);
		goto drop;
	}
	dst = skb->dst;

	/* IPv6 specs say nothing about it, but it is clear that we cannot
	   send redirects to source routed frames.
	   We don't send redirects to frames decapsulated from IPsec.
	 */
	if (skb->dev == dst->dev && dst->neighbour && opt->srcrt == 0 &&
	    !skb->sp) {
		struct in6_addr *target = NULL;
		struct rt6_info *rt;
		struct neighbour *n = dst->neighbour;

		/*
		 *	incoming and outgoing devices are the same
		 *	send a redirect.
		 */

		rt = (struct rt6_info *) dst;
		if ((rt->rt6i_flags & RTF_GATEWAY))
			target = (struct in6_addr*)&n->primary_key;
		else
			target = &hdr->daddr;

		/* Limit redirects both by destination (here)
		   and by source (inside ndisc_send_redirect)
		 */
		if (xrlim_allow(dst, 1*HZ))
			ndisc_send_redirect(skb, n, target);
	} else {
		int addrtype = ipv6_addr_type(&hdr->saddr);

		/* This check is security critical. */
		if (addrtype == IPV6_ADDR_ANY ||
		    addrtype & (IPV6_ADDR_MULTICAST | IPV6_ADDR_LOOPBACK))
			goto error;
		if (addrtype & IPV6_ADDR_LINKLOCAL) {
			icmpv6_send(skb, ICMPV6_DEST_UNREACH,
				ICMPV6_NOT_NEIGHBOUR, 0, skb->dev);
			goto error;
		}
	}

	if (skb->len > dst_mtu(dst)) {
		/* Again, force OUTPUT device used as source address */
		skb->dev = dst->dev;
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, dst_mtu(dst), skb->dev);
		IP6_INC_STATS_BH(ip6_dst_idev(dst), IPSTATS_MIB_INTOOBIGERRORS);
		IP6_INC_STATS_BH(ip6_dst_idev(dst), IPSTATS_MIB_FRAGFAILS);
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (skb_cow(skb, dst->dev->hard_header_len)) {
		IP6_INC_STATS(ip6_dst_idev(dst), IPSTATS_MIB_OUTDISCARDS);
		goto drop;
	}

	hdr = ipv6_hdr(skb);

	/* Mangling hops number delayed to point after skb COW */

	hdr->hop_limit--;

	IP6_INC_STATS_BH(ip6_dst_idev(dst), IPSTATS_MIB_OUTFORWDATAGRAMS);
	return NF_HOOK(PF_INET6, NF_INET_FORWARD, skb, skb->dev, dst->dev,
		       ip6_forward_finish);

error:
	IP6_INC_STATS_BH(ip6_dst_idev(dst), IPSTATS_MIB_INADDRERRORS);
drop:
	kfree_skb(skb);
	return -EINVAL;
}

static void ip6_copy_metadata(struct sk_buff *to, struct sk_buff *from)
{
	to->pkt_type = from->pkt_type;
	to->priority = from->priority;
	to->protocol = from->protocol;
	dst_release(to->dst);
	to->dst = dst_clone(from->dst);
	to->dev = from->dev;
	to->mark = from->mark;

#ifdef CONFIG_NET_SCHED
	to->tc_index = from->tc_index;
#endif
	nf_copy(to, from);
#if defined(CONFIG_NETFILTER_XT_TARGET_TRACE) || \
    defined(CONFIG_NETFILTER_XT_TARGET_TRACE_MODULE)
	to->nf_trace = from->nf_trace;
#endif
	skb_copy_secmark(to, from);
}

int ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr =
				(struct ipv6_opt_hdr *)(ipv6_hdr(skb) + 1);
	unsigned int packet_len = skb->tail - skb->network_header;
	int found_rhdr = 0;
	*nexthdr = &ipv6_hdr(skb)->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {

		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			found_rhdr = 1;
			break;
		case NEXTHDR_DEST:
#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
			if (ipv6_find_tlv(skb, offset, IPV6_TLV_HAO) >= 0)
				break;
#endif
			if (found_rhdr)
				return offset;
			break;
		default :
			return offset;
		}

		offset += ipv6_optlen(exthdr);
		*nexthdr = &exthdr->nexthdr;
		exthdr = (struct ipv6_opt_hdr *)(skb_network_header(skb) +
						 offset);
	}

	return offset;
}

static int ip6_fragment(struct sk_buff *skb, int (*output)(struct sk_buff *))
{
	struct net_device *dev;
	struct sk_buff *frag;
	struct rt6_info *rt = (struct rt6_info*)skb->dst;
	struct ipv6_pinfo *np = skb->sk ? inet6_sk(skb->sk) : NULL;
	struct ipv6hdr *tmp_hdr;
	struct frag_hdr *fh;
	unsigned int mtu, hlen, left, len;
	__be32 frag_id = 0;
	int ptr, offset = 0, err=0;
	u8 *prevhdr, nexthdr = 0;

	dev = rt->u.dst.dev;
	hlen = ip6_find_1stfragopt(skb, &prevhdr);
	nexthdr = *prevhdr;

	mtu = ip6_skb_dst_mtu(skb);

	/* We must not fragment if the socket is set to force MTU discovery
	 * or if the skb it not generated by a local socket.  (This last
	 * check should be redundant, but it's free.)
	 */
	if (!skb->local_df) {
		skb->dev = skb->dst->dev;
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, skb->dev);
		IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_FRAGFAILS);
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (np && np->frag_size < mtu) {
		if (np->frag_size)
			mtu = np->frag_size;
	}
	mtu -= hlen + sizeof(struct frag_hdr);

	if (skb_shinfo(skb)->frag_list) {
		int first_len = skb_pagelen(skb);
		int truesizes = 0;

		if (first_len - hlen > mtu ||
		    ((first_len - hlen) & 7) ||
		    skb_cloned(skb))
			goto slow_path;

		for (frag = skb_shinfo(skb)->frag_list; frag; frag = frag->next) {
			/* Correct geometry. */
			if (frag->len > mtu ||
			    ((frag->len & 7) && frag->next) ||
			    skb_headroom(frag) < hlen)
			    goto slow_path;

			/* Partially cloned skb? */
			if (skb_shared(frag))
				goto slow_path;

			BUG_ON(frag->sk);
			if (skb->sk) {
				sock_hold(skb->sk);
				frag->sk = skb->sk;
				frag->destructor = sock_wfree;
				truesizes += frag->truesize;
			}
		}

		err = 0;
		offset = 0;
		frag = skb_shinfo(skb)->frag_list;
		skb_shinfo(skb)->frag_list = NULL;
		/* BUILD HEADER */

		*prevhdr = NEXTHDR_FRAGMENT;
		tmp_hdr = kmemdup(skb_network_header(skb), hlen, GFP_ATOMIC);
		if (!tmp_hdr) {
			IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_FRAGFAILS);
			return -ENOMEM;
		}

		__skb_pull(skb, hlen);
		fh = (struct frag_hdr*)__skb_push(skb, sizeof(struct frag_hdr));
		__skb_push(skb, hlen);
		skb_reset_network_header(skb);
		memcpy(skb_network_header(skb), tmp_hdr, hlen);

		ipv6_select_ident(skb, fh);
		fh->nexthdr = nexthdr;
		fh->reserved = 0;
		fh->frag_off = htons(IP6_MF);
		frag_id = fh->identification;

		first_len = skb_pagelen(skb);
		skb->data_len = first_len - skb_headlen(skb);
		skb->truesize -= truesizes;
		skb->len = first_len;
		ipv6_hdr(skb)->payload_len = htons(first_len -
						   sizeof(struct ipv6hdr));

		dst_hold(&rt->u.dst);

		for (;;) {
			/* Prepare header of the next frame,
			 * before previous one went down. */
			if (frag) {
				frag->ip_summed = CHECKSUM_NONE;
				skb_reset_transport_header(frag);
				fh = (struct frag_hdr*)__skb_push(frag, sizeof(struct frag_hdr));
				__skb_push(frag, hlen);
				skb_reset_network_header(frag);
				memcpy(skb_network_header(frag), tmp_hdr,
				       hlen);
				offset += skb->len - hlen - sizeof(struct frag_hdr);
				fh->nexthdr = nexthdr;
				fh->reserved = 0;
				fh->frag_off = htons(offset);
				if (frag->next != NULL)
					fh->frag_off |= htons(IP6_MF);
				fh->identification = frag_id;
				ipv6_hdr(frag)->payload_len =
						htons(frag->len -
						      sizeof(struct ipv6hdr));
				ip6_copy_metadata(frag, skb);
			}

			err = output(skb);
			if(!err)
				IP6_INC_STATS(ip6_dst_idev(&rt->u.dst), IPSTATS_MIB_FRAGCREATES);

			if (err || !frag)
				break;

			skb = frag;
			frag = skb->next;
			skb->next = NULL;
		}

		kfree(tmp_hdr);

		if (err == 0) {
			IP6_INC_STATS(ip6_dst_idev(&rt->u.dst), IPSTATS_MIB_FRAGOKS);
			dst_release(&rt->u.dst);
			return 0;
		}

		while (frag) {
			skb = frag->next;
			kfree_skb(frag);
			frag = skb;
		}

		IP6_INC_STATS(ip6_dst_idev(&rt->u.dst), IPSTATS_MIB_FRAGFAILS);
		dst_release(&rt->u.dst);
		return err;
	}

slow_path:
	left = skb->len - hlen;		/* Space per frame */
	ptr = hlen;			/* Where to start from */

	/*
	 *	Fragment the datagram.
	 */

	*prevhdr = NEXTHDR_FRAGMENT;

	/*
	 *	Keep copying data until we run out.
	 */
	while(left > 0)	{
		len = left;
		/* IF: it doesn't fit, use 'mtu' - the data space left */
		if (len > mtu)
			len = mtu;
		/* IF: we are not sending upto and including the packet end
		   then align the next start on an eight byte boundary */
		if (len < left)	{
			len &= ~7;
		}
		/*
		 *	Allocate buffer.
		 */

		if ((frag = alloc_skb(len+hlen+sizeof(struct frag_hdr)+LL_ALLOCATED_SPACE(rt->u.dst.dev), GFP_ATOMIC)) == NULL) {
			NETDEBUG(KERN_INFO "IPv6: frag: no memory for new fragment!\n");
			IP6_INC_STATS(ip6_dst_idev(skb->dst),
				      IPSTATS_MIB_FRAGFAILS);
			err = -ENOMEM;
			goto fail;
		}

		/*
		 *	Set up data on packet
		 */

		ip6_copy_metadata(frag, skb);
		skb_reserve(frag, LL_RESERVED_SPACE(rt->u.dst.dev));
		skb_put(frag, len + hlen + sizeof(struct frag_hdr));
		skb_reset_network_header(frag);
		fh = (struct frag_hdr *)(skb_network_header(frag) + hlen);
		frag->transport_header = (frag->network_header + hlen +
					  sizeof(struct frag_hdr));

		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */
		if (skb->sk)
			skb_set_owner_w(frag, skb->sk);

		/*
		 *	Copy the packet header into the new buffer.
		 */
		skb_copy_from_linear_data(skb, skb_network_header(frag), hlen);

		/*
		 *	Build fragment header.
		 */
		fh->nexthdr = nexthdr;
		fh->reserved = 0;
		if (!frag_id) {
			ipv6_select_ident(skb, fh);
			frag_id = fh->identification;
		} else
			fh->identification = frag_id;

		/*
		 *	Copy a block of the IP datagram.
		 */
		if (skb_copy_bits(skb, ptr, skb_transport_header(frag), len))
			BUG();
		left -= len;

		fh->frag_off = htons(offset);
		if (left > 0)
			fh->frag_off |= htons(IP6_MF);
		ipv6_hdr(frag)->payload_len = htons(frag->len -
						    sizeof(struct ipv6hdr));

		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */
		err = output(frag);
		if (err)
			goto fail;

		IP6_INC_STATS(ip6_dst_idev(skb->dst), IPSTATS_MIB_FRAGCREATES);
	}
	IP6_INC_STATS(ip6_dst_idev(skb->dst),
		      IPSTATS_MIB_FRAGOKS);
	kfree_skb(skb);
	return err;

fail:
	IP6_INC_STATS(ip6_dst_idev(skb->dst),
		      IPSTATS_MIB_FRAGFAILS);
	kfree_skb(skb);
	return err;
}

static inline int ip6_rt_check(struct rt6key *rt_key,
			       struct in6_addr *fl_addr,
			       struct in6_addr *addr_cache)
{
	return ((rt_key->plen != 128 || !ipv6_addr_equal(fl_addr, &rt_key->addr)) &&
		(addr_cache == NULL || !ipv6_addr_equal(fl_addr, addr_cache)));
}

static struct dst_entry *ip6_sk_dst_check(struct sock *sk,
					  struct dst_entry *dst,
					  struct flowi *fl)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct rt6_info *rt = (struct rt6_info *)dst;

	if (!dst)
		goto out;

	/* Yes, checking route validity in not connected
	 * case is not very simple. Take into account,
	 * that we do not support routing by source, TOS,
	 * and MSG_DONTROUTE 		--ANK (980726)
	 *
	 * 1. ip6_rt_check(): If route was host route,
	 *    check that cached destination is current.
	 *    If it is network route, we still may
	 *    check its validity using saved pointer
	 *    to the last used address: daddr_cache.
	 *    We do not want to save whole address now,
	 *    (because main consumer of this service
	 *    is tcp, which has not this problem),
	 *    so that the last trick works only on connected
	 *    sockets.
	 * 2. oif also should be the same.
	 */
	if (ip6_rt_check(&rt->rt6i_dst, &fl->fl6_dst, np->daddr_cache) ||
#ifdef CONFIG_IPV6_SUBTREES
	    ip6_rt_check(&rt->rt6i_src, &fl->fl6_src, np->saddr_cache) ||
#endif
	    (fl->oif && fl->oif != dst->dev->ifindex)) {
		dst_release(dst);
		dst = NULL;
	}

out:
	return dst;
}

static int ip6_dst_lookup_tail(struct sock *sk,
			       struct dst_entry **dst, struct flowi *fl)
{
	int err;
	struct net *net = sock_net(sk);

	if (*dst == NULL)
		*dst = ip6_route_output(net, sk, fl);

	if ((err = (*dst)->error))
		goto out_err_release;

	if (ipv6_addr_any(&fl->fl6_src)) {
		err = ipv6_dev_get_saddr(net, ip6_dst_idev(*dst)->dev,
					 &fl->fl6_dst,
					 sk ? inet6_sk(sk)->srcprefs : 0,
					 &fl->fl6_src);
		if (err)
			goto out_err_release;
	}

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
		/*
		 * Here if the dst entry we've looked up
		 * has a neighbour entry that is in the INCOMPLETE
		 * state and the src address from the flow is
		 * marked as OPTIMISTIC, we release the found
		 * dst entry and replace it instead with the
		 * dst entry of the nexthop router
		 */
		if (!((*dst)->neighbour->nud_state & NUD_VALID)) {
			struct inet6_ifaddr *ifp;
			struct flowi fl_gw;
			int redirect;

			ifp = ipv6_get_ifaddr(net, &fl->fl6_src,
					      (*dst)->dev, 1);

			redirect = (ifp && ifp->flags & IFA_F_OPTIMISTIC);
			if (ifp)
				in6_ifa_put(ifp);

			if (redirect) {
				/*
				 * We need to get the dst entry for the
				 * default router instead
				 */
				dst_release(*dst);
				memcpy(&fl_gw, fl, sizeof(struct flowi));
				memset(&fl_gw.fl6_dst, 0, sizeof(struct in6_addr));
				*dst = ip6_route_output(net, sk, &fl_gw);
				if ((err = (*dst)->error))
					goto out_err_release;
			}
		}
#endif

	return 0;

out_err_release:
	if (err == -ENETUNREACH)
		IP6_INC_STATS_BH(NULL, IPSTATS_MIB_OUTNOROUTES);
	dst_release(*dst);
	*dst = NULL;
	return err;
}

/**
 *	ip6_dst_lookup - perform route lookup on flow
 *	@sk: socket which provides route info
 *	@dst: pointer to dst_entry * for result
 *	@fl: flow to lookup
 *
 *	This function performs a route lookup on the given flow.
 *
 *	It returns zero on success, or a standard errno code on error.
 */
int ip6_dst_lookup(struct sock *sk, struct dst_entry **dst, struct flowi *fl)
{
	*dst = NULL;
	return ip6_dst_lookup_tail(sk, dst, fl);
}
EXPORT_SYMBOL_GPL(ip6_dst_lookup);

/**
 *	ip6_sk_dst_lookup - perform socket cached route lookup on flow
 *	@sk: socket which provides the dst cache and route info
 *	@dst: pointer to dst_entry * for result
 *	@fl: flow to lookup
 *
 *	This function performs a route lookup on the given flow with the
 *	possibility of using the cached route in the socket if it is valid.
 *	It will take the socket dst lock when operating on the dst cache.
 *	As a result, this function can only be used in process context.
 *
 *	It returns zero on success, or a standard errno code on error.
 */
int ip6_sk_dst_lookup(struct sock *sk, struct dst_entry **dst, struct flowi *fl)
{
	*dst = NULL;
	if (sk) {
		*dst = sk_dst_check(sk, inet6_sk(sk)->dst_cookie);
		*dst = ip6_sk_dst_check(sk, *dst, fl);
	}

	return ip6_dst_lookup_tail(sk, dst, fl);
}
EXPORT_SYMBOL_GPL(ip6_sk_dst_lookup);

static inline int ip6_ufo_append_data(struct sock *sk,
			int getfrag(void *from, char *to, int offset, int len,
			int odd, struct sk_buff *skb),
			void *from, int length, int hh_len, int fragheaderlen,
			int transhdrlen, int mtu,unsigned int flags)

{
	struct sk_buff *skb;
	int err;

	/* There is support for UDP large send offload by network
	 * device, so create one single skb packet containing complete
	 * udp datagram
	 */
	if ((skb = skb_peek_tail(&sk->sk_write_queue)) == NULL) {
		skb = sock_alloc_send_skb(sk,
			hh_len + fragheaderlen + transhdrlen + 20,
			(flags & MSG_DONTWAIT), &err);
		if (skb == NULL)
			return -ENOMEM;

		/* reserve space for Hardware header */
		skb_reserve(skb, hh_len);

		/* create space for UDP/IP header */
		skb_put(skb,fragheaderlen + transhdrlen);

		/* initialize network header pointer */
		skb_reset_network_header(skb);

		/* initialize protocol header pointer */
		skb->transport_header = skb->network_header + fragheaderlen;

		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum = 0;
		sk->sk_sndmsg_off = 0;
	}

	err = skb_append_datato_frags(sk,skb, getfrag, from,
				      (length - transhdrlen));
	if (!err) {
		struct frag_hdr fhdr;

		/* specify the length of each IP datagram fragment*/
		skb_shinfo(skb)->gso_size = mtu - fragheaderlen -
					    sizeof(struct frag_hdr);
		skb_shinfo(skb)->gso_type = SKB_GSO_UDP;
		ipv6_select_ident(skb, &fhdr);
		skb_shinfo(skb)->ip6_frag_id = fhdr.identification;
		__skb_queue_tail(&sk->sk_write_queue, skb);

		return 0;
	}
	/* There is not enough support do UPD LSO,
	 * so follow normal path
	 */
	kfree_skb(skb);

	return err;
}

int ip6_append_data(struct sock *sk, int getfrag(void *from, char *to,
	int offset, int len, int odd, struct sk_buff *skb),
	void *from, int length, int transhdrlen,
	int hlimit, int tclass, struct ipv6_txoptions *opt, struct flowi *fl,
	struct rt6_info *rt, unsigned int flags)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;
	unsigned int maxfraglen, fragheaderlen;
	int exthdrlen;
	int hh_len;
	int mtu;
	int copy;
	int err;
	int offset = 0;
	int csummode = CHECKSUM_NONE;

	if (flags&MSG_PROBE)
		return 0;
	if (skb_queue_empty(&sk->sk_write_queue)) {
		/*
		 * setup for corking
		 */
		if (opt) {
			if (np->cork.opt == NULL) {
				np->cork.opt = kmalloc(opt->tot_len,
						       sk->sk_allocation);
				if (unlikely(np->cork.opt == NULL))
					return -ENOBUFS;
			} else if (np->cork.opt->tot_len < opt->tot_len) {
				printk(KERN_DEBUG "ip6_append_data: invalid option length\n");
				return -EINVAL;
			}
			memcpy(np->cork.opt, opt, opt->tot_len);
			inet->cork.flags |= IPCORK_OPT;
			/* need source address above miyazawa*/
		}
		dst_hold(&rt->u.dst);
		inet->cork.dst = &rt->u.dst;
		inet->cork.fl = *fl;
		np->cork.hop_limit = hlimit;
		np->cork.tclass = tclass;
		mtu = np->pmtudisc == IPV6_PMTUDISC_PROBE ?
		      rt->u.dst.dev->mtu : dst_mtu(rt->u.dst.path);
		if (np->frag_size < mtu) {
			if (np->frag_size)
				mtu = np->frag_size;
		}
		inet->cork.fragsize = mtu;
		if (dst_allfrag(rt->u.dst.path))
			inet->cork.flags |= IPCORK_ALLFRAG;
		inet->cork.length = 0;
		sk->sk_sndmsg_page = NULL;
		sk->sk_sndmsg_off = 0;
		exthdrlen = rt->u.dst.header_len + (opt ? opt->opt_flen : 0) -
			    rt->rt6i_nfheader_len;
		length += exthdrlen;
		transhdrlen += exthdrlen;
	} else {
		rt = (struct rt6_info *)inet->cork.dst;
		fl = &inet->cork.fl;
		if (inet->cork.flags & IPCORK_OPT)
			opt = np->cork.opt;
		transhdrlen = 0;
		exthdrlen = 0;
		mtu = inet->cork.fragsize;
	}

	hh_len = LL_RESERVED_SPACE(rt->u.dst.dev);

	fragheaderlen = sizeof(struct ipv6hdr) + rt->rt6i_nfheader_len +
			(opt ? opt->opt_nflen : 0);
	maxfraglen = ((mtu - fragheaderlen) & ~7) + fragheaderlen - sizeof(struct frag_hdr);

	if (mtu <= sizeof(struct ipv6hdr) + IPV6_MAXPLEN) {
		if (inet->cork.length + length > sizeof(struct ipv6hdr) + IPV6_MAXPLEN - fragheaderlen) {
			ipv6_local_error(sk, EMSGSIZE, fl, mtu-exthdrlen);
			return -EMSGSIZE;
		}
	}

	/*
	 * Let's try using as much space as possible.
	 * Use MTU if total length of the message fits into the MTU.
	 * Otherwise, we need to reserve fragment header and
	 * fragment alignment (= 8-15 octects, in total).
	 *
	 * Note that we may need to "move" the data from the tail of
	 * of the buffer to the new fragment when we split
	 * the message.
	 *
	 * FIXME: It may be fragmented into multiple chunks
	 *        at once if non-fragmentable extension headers
	 *        are too large.
	 * --yoshfuji
	 */

	inet->cork.length += length;
	if (((length > mtu) && (sk->sk_protocol == IPPROTO_UDP)) &&
	    (rt->u.dst.dev->features & NETIF_F_UFO)) {

		err = ip6_ufo_append_data(sk, getfrag, from, length, hh_len,
					  fragheaderlen, transhdrlen, mtu,
					  flags);
		if (err)
			goto error;
		return 0;
	}

	if ((skb = skb_peek_tail(&sk->sk_write_queue)) == NULL)
		goto alloc_new_skb;

	while (length > 0) {
		/* Check if the remaining data fits into current packet. */
		copy = (inet->cork.length <= mtu && !(inet->cork.flags & IPCORK_ALLFRAG) ? mtu : maxfraglen) - skb->len;
		if (copy < length)
			copy = maxfraglen - skb->len;

		if (copy <= 0) {
			char *data;
			unsigned int datalen;
			unsigned int fraglen;
			unsigned int fraggap;
			unsigned int alloclen;
			struct sk_buff *skb_prev;
alloc_new_skb:
			skb_prev = skb;

			/* There's no room in the current skb */
			if (skb_prev)
				fraggap = skb_prev->len - maxfraglen;
			else
				fraggap = 0;

			/*
			 * If remaining data exceeds the mtu,
			 * we know we need more fragment(s).
			 */
			datalen = length + fraggap;
			if (datalen > (inet->cork.length <= mtu && !(inet->cork.flags & IPCORK_ALLFRAG) ? mtu : maxfraglen) - fragheaderlen)
				datalen = maxfraglen - fragheaderlen;

			fraglen = datalen + fragheaderlen;
			if ((flags & MSG_MORE) &&
			    !(rt->u.dst.dev->features&NETIF_F_SG))
				alloclen = mtu;
			else
				alloclen = datalen + fragheaderlen;

			/*
			 * The last fragment gets additional space at tail.
			 * Note: we overallocate on fragments with MSG_MODE
			 * because we have no idea if we're the last one.
			 */
			if (datalen == length + fraggap)
				alloclen += rt->u.dst.trailer_len;

			/*
			 * We just reserve space for fragment header.
			 * Note: this may be overallocation if the message
			 * (without MSG_MORE) fits into the MTU.
			 */
			alloclen += sizeof(struct frag_hdr);

			if (transhdrlen) {
				skb = sock_alloc_send_skb(sk,
						alloclen + hh_len,
						(flags & MSG_DONTWAIT), &err);
			} else {
				skb = NULL;
				if (atomic_read(&sk->sk_wmem_alloc) <=
				    2 * sk->sk_sndbuf)
					skb = sock_wmalloc(sk,
							   alloclen + hh_len, 1,
							   sk->sk_allocation);
				if (unlikely(skb == NULL))
					err = -ENOBUFS;
			}
			if (skb == NULL)
				goto error;
			/*
			 *	Fill in the control structures
			 */
			skb->ip_summed = csummode;
			skb->csum = 0;
			/* reserve for fragmentation */
			skb_reserve(skb, hh_len+sizeof(struct frag_hdr));

			/*
			 *	Find where to start putting bytes
			 */
			data = skb_put(skb, fraglen);
			skb_set_network_header(skb, exthdrlen);
			data += fragheaderlen;
			skb->transport_header = (skb->network_header +
						 fragheaderlen);
			if (fraggap) {
				skb->csum = skb_copy_and_csum_bits(
					skb_prev, maxfraglen,
					data + transhdrlen, fraggap, 0);
				skb_prev->csum = csum_sub(skb_prev->csum,
							  skb->csum);
				data += fraggap;
				pskb_trim_unique(skb_prev, maxfraglen);
			}
			copy = datalen - transhdrlen - fraggap;
			if (copy < 0) {
				err = -EINVAL;
				kfree_skb(skb);
				goto error;
			} else if (copy > 0 && getfrag(from, data + transhdrlen, offset, copy, fraggap, skb) < 0) {
				err = -EFAULT;
				kfree_skb(skb);
				goto error;
			}

			offset += copy;
			length -= datalen - fraggap;
			transhdrlen = 0;
			exthdrlen = 0;
			csummode = CHECKSUM_NONE;

			/*
			 * Put the packet on the pending queue
			 */
			__skb_queue_tail(&sk->sk_write_queue, skb);
			continue;
		}

		if (copy > length)
			copy = length;

		if (!(rt->u.dst.dev->features&NETIF_F_SG)) {
			unsigned int off;

			off = skb->len;
			if (getfrag(from, skb_put(skb, copy),
						offset, copy, off, skb) < 0) {
				__skb_trim(skb, off);
				err = -EFAULT;
				goto error;
			}
		} else {
			int i = skb_shinfo(skb)->nr_frags;
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i-1];
			struct page *page = sk->sk_sndmsg_page;
			int off = sk->sk_sndmsg_off;
			unsigned int left;

			if (page && (left = PAGE_SIZE - off) > 0) {
				if (copy >= left)
					copy = left;
				if (page != frag->page) {
					if (i == MAX_SKB_FRAGS) {
						err = -EMSGSIZE;
						goto error;
					}
					get_page(page);
					skb_fill_page_desc(skb, i, page, sk->sk_sndmsg_off, 0);
					frag = &skb_shinfo(skb)->frags[i];
				}
			} else if(i < MAX_SKB_FRAGS) {
				if (copy > PAGE_SIZE)
					copy = PAGE_SIZE;
				page = alloc_pages(sk->sk_allocation, 0);
				if (page == NULL) {
					err = -ENOMEM;
					goto error;
				}
				sk->sk_sndmsg_page = page;
				sk->sk_sndmsg_off = 0;

				skb_fill_page_desc(skb, i, page, 0, 0);
				frag = &skb_shinfo(skb)->frags[i];
			} else {
				err = -EMSGSIZE;
				goto error;
			}
			if (getfrag(from, page_address(frag->page)+frag->page_offset+frag->size, offset, copy, skb->len, skb) < 0) {
				err = -EFAULT;
				goto error;
			}
			sk->sk_sndmsg_off += copy;
			frag->size += copy;
			skb->len += copy;
			skb->data_len += copy;
			skb->truesize += copy;
			atomic_add(copy, &sk->sk_wmem_alloc);
		}
		offset += copy;
		length -= copy;
	}
	return 0;
error:
	inet->cork.length -= length;
	IP6_INC_STATS(rt->rt6i_idev, IPSTATS_MIB_OUTDISCARDS);
	return err;
}

static void ip6_cork_release(struct inet_sock *inet, struct ipv6_pinfo *np)
{
	inet->cork.flags &= ~IPCORK_OPT;
	kfree(np->cork.opt);
	np->cork.opt = NULL;
	if (inet->cork.dst) {
		dst_release(inet->cork.dst);
		inet->cork.dst = NULL;
		inet->cork.flags &= ~IPCORK_ALLFRAG;
	}
	memset(&inet->cork.fl, 0, sizeof(inet->cork.fl));
}

int ip6_push_pending_frames(struct sock *sk)
{
	struct sk_buff *skb, *tmp_skb;
	struct sk_buff **tail_skb;
	struct in6_addr final_dst_buf, *final_dst = &final_dst_buf;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6hdr *hdr;
	struct ipv6_txoptions *opt = np->cork.opt;
	struct rt6_info *rt = (struct rt6_info *)inet->cork.dst;
	struct flowi *fl = &inet->cork.fl;
	unsigned char proto = fl->proto;
	int err = 0;

	if ((skb = __skb_dequeue(&sk->sk_write_queue)) == NULL)
		goto out;
	tail_skb = &(skb_shinfo(skb)->frag_list);

	/* move skb->data to ip header from ext header */
	if (skb->data < skb_network_header(skb))
		__skb_pull(skb, skb_network_offset(skb));
	while ((tmp_skb = __skb_dequeue(&sk->sk_write_queue)) != NULL) {
		__skb_pull(tmp_skb, skb_network_header_len(skb));
		*tail_skb = tmp_skb;
		tail_skb = &(tmp_skb->next);
		skb->len += tmp_skb->len;
		skb->data_len += tmp_skb->len;
		skb->truesize += tmp_skb->truesize;
		__sock_put(tmp_skb->sk);
		tmp_skb->destructor = NULL;
		tmp_skb->sk = NULL;
	}

	/* Allow local fragmentation. */
	if (np->pmtudisc < IPV6_PMTUDISC_DO)
		skb->local_df = 1;

	ipv6_addr_copy(final_dst, &fl->fl6_dst);
	__skb_pull(skb, skb_network_header_len(skb));
	if (opt && opt->opt_flen)
		ipv6_push_frag_opts(skb, opt, &proto);
	if (opt && opt->opt_nflen)
		ipv6_push_nfrag_opts(skb, opt, &proto, &final_dst);

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	hdr = ipv6_hdr(skb);

	*(__be32*)hdr = fl->fl6_flowlabel |
		     htonl(0x60000000 | ((int)np->cork.tclass << 20));

	hdr->hop_limit = np->cork.hop_limit;
	hdr->nexthdr = proto;
	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, final_dst);

	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;

	skb->dst = dst_clone(&rt->u.dst);
	IP6_INC_STATS(rt->rt6i_idev, IPSTATS_MIB_OUTREQUESTS);
	if (proto == IPPROTO_ICMPV6) {
		struct inet6_dev *idev = ip6_dst_idev(skb->dst);

		ICMP6MSGOUT_INC_STATS_BH(idev, icmp6_hdr(skb)->icmp6_type);
		ICMP6_INC_STATS_BH(idev, ICMP6_MIB_OUTMSGS);
	}

	err = ip6_local_out(skb);
	if (err) {
		if (err > 0)
			err = np->recverr ? net_xmit_errno(err) : 0;
		if (err)
			goto error;
	}

out:
	ip6_cork_release(inet, np);
	return err;
error:
	goto out;
}

void ip6_flush_pending_frames(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue_tail(&sk->sk_write_queue)) != NULL) {
		if (skb->dst)
			IP6_INC_STATS(ip6_dst_idev(skb->dst),
				      IPSTATS_MIB_OUTDISCARDS);
		kfree_skb(skb);
	}

	ip6_cork_release(inet_sk(sk), inet6_sk(sk));
}
