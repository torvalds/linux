/*
 *	IPv6 output functions
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: ip6_output.c,v 1.34 2002/02/01 22:01:04 davem Exp $
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

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/route.h>

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

static inline int ip6_output_finish(struct sk_buff *skb)
{

	struct dst_entry *dst = skb->dst;
	struct hh_cache *hh = dst->hh;

	if (hh) {
		int hh_alen;

		read_lock_bh(&hh->hh_lock);
		hh_alen = HH_DATA_ALIGN(hh->hh_len);
		memcpy(skb->data - hh_alen, hh->hh_data, hh_alen);
		read_unlock_bh(&hh->hh_lock);
	        skb_push(skb, hh->hh_len);
		return hh->hh_output(skb);
	} else if (dst->neighbour)
		return dst->neighbour->output(skb);

	IP6_INC_STATS_BH(IPSTATS_MIB_OUTNOROUTES);
	kfree_skb(skb);
	return -EINVAL;

}

/* dev_loopback_xmit for use with netfilter. */
static int ip6_dev_loopback_xmit(struct sk_buff *newskb)
{
	newskb->mac.raw = newskb->data;
	__skb_pull(newskb, newskb->nh.raw - newskb->data);
	newskb->pkt_type = PACKET_LOOPBACK;
	newskb->ip_summed = CHECKSUM_UNNECESSARY;
	BUG_TRAP(newskb->dst);

	netif_rx(newskb);
	return 0;
}


static int ip6_output2(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct net_device *dev = dst->dev;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	if (ipv6_addr_is_multicast(&skb->nh.ipv6h->daddr)) {
		struct ipv6_pinfo* np = skb->sk ? inet6_sk(skb->sk) : NULL;

		if (!(dev->flags & IFF_LOOPBACK) && (!np || np->mc_loop) &&
		    ipv6_chk_mcast_addr(dev, &skb->nh.ipv6h->daddr,
				&skb->nh.ipv6h->saddr)) {
			struct sk_buff *newskb = skb_clone(skb, GFP_ATOMIC);

			/* Do not check for IFF_ALLMULTI; multicast routing
			   is not supported in any case.
			 */
			if (newskb)
				NF_HOOK(PF_INET6, NF_IP6_POST_ROUTING, newskb, NULL,
					newskb->dev,
					ip6_dev_loopback_xmit);

			if (skb->nh.ipv6h->hop_limit == 0) {
				IP6_INC_STATS(IPSTATS_MIB_OUTDISCARDS);
				kfree_skb(skb);
				return 0;
			}
		}

		IP6_INC_STATS(IPSTATS_MIB_OUTMCASTPKTS);
	}

	return NF_HOOK(PF_INET6, NF_IP6_POST_ROUTING, skb,NULL, skb->dev,ip6_output_finish);
}

int ip6_output(struct sk_buff *skb)
{
	if ((skb->len > dst_mtu(skb->dst) && !skb_shinfo(skb)->ufo_size) ||
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
		int head_room;

		/* First: exthdrs may take lots of space (~8K for now)
		   MAX_HEADER is not enough.
		 */
		head_room = opt->opt_nflen + opt->opt_flen;
		seg_len += head_room;
		head_room += sizeof(struct ipv6hdr) + LL_RESERVED_SPACE(dst->dev);

		if (skb_headroom(skb) < head_room) {
			struct sk_buff *skb2 = skb_realloc_headroom(skb, head_room);
			kfree_skb(skb);
			skb = skb2;
			if (skb == NULL) {	
				IP6_INC_STATS(IPSTATS_MIB_OUTDISCARDS);
				return -ENOBUFS;
			}
			if (sk)
				skb_set_owner_w(skb, sk);
		}
		if (opt->opt_flen)
			ipv6_push_frag_opts(skb, opt, &proto);
		if (opt->opt_nflen)
			ipv6_push_nfrag_opts(skb, opt, &proto, &first_hop);
	}

	hdr = skb->nh.ipv6h = (struct ipv6hdr*)skb_push(skb, sizeof(struct ipv6hdr));

	/*
	 *	Fill in the IPv6 header
	 */

	hlimit = -1;
	if (np)
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	if (hlimit < 0)
		hlimit = ipv6_get_hoplimit(dst->dev);

	tclass = -1;
	if (np)
		tclass = np->tclass;
	if (tclass < 0)
		tclass = 0;

	*(u32 *)hdr = htonl(0x60000000 | (tclass << 20)) | fl->fl6_flowlabel;

	hdr->payload_len = htons(seg_len);
	hdr->nexthdr = proto;
	hdr->hop_limit = hlimit;

	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, first_hop);

	skb->priority = sk->sk_priority;

	mtu = dst_mtu(dst);
	if ((skb->len <= mtu) || ipfragok) {
		IP6_INC_STATS(IPSTATS_MIB_OUTREQUESTS);
		return NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, skb, NULL, dst->dev,
				dst_output);
	}

	if (net_ratelimit())
		printk(KERN_DEBUG "IPv6: sending pkt_too_big to self\n");
	skb->dev = dst->dev;
	icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, skb->dev);
	IP6_INC_STATS(IPSTATS_MIB_FRAGFAILS);
	kfree_skb(skb);
	return -EMSGSIZE;
}

/*
 *	To avoid extra problems ND packets are send through this
 *	routine. It's code duplication but I really want to avoid
 *	extra checks since ipv6_build_header is used by TCP (which
 *	is for us performance critical)
 */

int ip6_nd_hdr(struct sock *sk, struct sk_buff *skb, struct net_device *dev,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       int proto, int len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ipv6hdr *hdr;
	int totlen;

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	totlen = len + sizeof(struct ipv6hdr);

	hdr = (struct ipv6hdr *) skb_put(skb, sizeof(struct ipv6hdr));
	skb->nh.ipv6h = hdr;

	*(u32*)hdr = htonl(0x60000000);

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

static inline int ip6_forward_finish(struct sk_buff *skb)
{
	return dst_output(skb);
}

int ip6_forward(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	struct inet6_skb_parm *opt = IP6CB(skb);
	
	if (ipv6_devconf.forwarding == 0)
		goto error;

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_FWD, skb)) {
		IP6_INC_STATS(IPSTATS_MIB_INDISCARDS);
		goto drop;
	}

	skb->ip_summed = CHECKSUM_NONE;

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
		u8 *ptr = skb->nh.raw + opt->ra;
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

		kfree_skb(skb);
		return -ETIMEDOUT;
	}

	if (!xfrm6_route_forward(skb)) {
		IP6_INC_STATS(IPSTATS_MIB_INDISCARDS);
		goto drop;
	}
	dst = skb->dst;

	/* IPv6 specs say nothing about it, but it is clear that we cannot
	   send redirects to source routed frames.
	 */
	if (skb->dev == dst->dev && dst->neighbour && opt->srcrt == 0) {
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
	} else if (ipv6_addr_type(&hdr->saddr)&(IPV6_ADDR_MULTICAST|IPV6_ADDR_LOOPBACK
						|IPV6_ADDR_LINKLOCAL)) {
		/* This check is security critical. */
		goto error;
	}

	if (skb->len > dst_mtu(dst)) {
		/* Again, force OUTPUT device used as source address */
		skb->dev = dst->dev;
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, dst_mtu(dst), skb->dev);
		IP6_INC_STATS_BH(IPSTATS_MIB_INTOOBIGERRORS);
		IP6_INC_STATS_BH(IPSTATS_MIB_FRAGFAILS);
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (skb_cow(skb, dst->dev->hard_header_len)) {
		IP6_INC_STATS(IPSTATS_MIB_OUTDISCARDS);
		goto drop;
	}

	hdr = skb->nh.ipv6h;

	/* Mangling hops number delayed to point after skb COW */
 
	hdr->hop_limit--;

	IP6_INC_STATS_BH(IPSTATS_MIB_OUTFORWDATAGRAMS);
	return NF_HOOK(PF_INET6,NF_IP6_FORWARD, skb, skb->dev, dst->dev, ip6_forward_finish);

error:
	IP6_INC_STATS_BH(IPSTATS_MIB_INADDRERRORS);
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

#ifdef CONFIG_NET_SCHED
	to->tc_index = from->tc_index;
#endif
#ifdef CONFIG_NETFILTER
	to->nfmark = from->nfmark;
	/* Connection association is same as pre-frag packet */
	nf_conntrack_put(to->nfct);
	to->nfct = from->nfct;
	nf_conntrack_get(to->nfct);
	to->nfctinfo = from->nfctinfo;
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_conntrack_put_reasm(to->nfct_reasm);
	to->nfct_reasm = from->nfct_reasm;
	nf_conntrack_get_reasm(to->nfct_reasm);
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	nf_bridge_put(to->nf_bridge);
	to->nf_bridge = from->nf_bridge;
	nf_bridge_get(to->nf_bridge);
#endif
#endif
}

int ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr)
{
	u16 offset = sizeof(struct ipv6hdr);
	struct ipv6_opt_hdr *exthdr = (struct ipv6_opt_hdr*)(skb->nh.ipv6h + 1);
	unsigned int packet_len = skb->tail - skb->nh.raw;
	int found_rhdr = 0;
	*nexthdr = &skb->nh.ipv6h->nexthdr;

	while (offset + 1 <= packet_len) {

		switch (**nexthdr) {

		case NEXTHDR_HOP:
		case NEXTHDR_ROUTING:
		case NEXTHDR_DEST:
			if (**nexthdr == NEXTHDR_ROUTING) found_rhdr = 1;
			if (**nexthdr == NEXTHDR_DEST && found_rhdr) return offset;
			offset += ipv6_optlen(exthdr);
			*nexthdr = &exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr*)(skb->nh.raw + offset);
			break;
		default :
			return offset;
		}
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
	u32 frag_id = 0;
	int ptr, offset = 0, err=0;
	u8 *prevhdr, nexthdr = 0;

	dev = rt->u.dst.dev;
	hlen = ip6_find_1stfragopt(skb, &prevhdr);
	nexthdr = *prevhdr;

	mtu = dst_mtu(&rt->u.dst);
	if (np && np->frag_size < mtu) {
		if (np->frag_size)
			mtu = np->frag_size;
	}
	mtu -= hlen + sizeof(struct frag_hdr);

	if (skb_shinfo(skb)->frag_list) {
		int first_len = skb_pagelen(skb);

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
				skb->truesize -= frag->truesize;
			}
		}

		err = 0;
		offset = 0;
		frag = skb_shinfo(skb)->frag_list;
		skb_shinfo(skb)->frag_list = NULL;
		/* BUILD HEADER */

		tmp_hdr = kmalloc(hlen, GFP_ATOMIC);
		if (!tmp_hdr) {
			IP6_INC_STATS(IPSTATS_MIB_FRAGFAILS);
			return -ENOMEM;
		}

		*prevhdr = NEXTHDR_FRAGMENT;
		memcpy(tmp_hdr, skb->nh.raw, hlen);
		__skb_pull(skb, hlen);
		fh = (struct frag_hdr*)__skb_push(skb, sizeof(struct frag_hdr));
		skb->nh.raw = __skb_push(skb, hlen);
		memcpy(skb->nh.raw, tmp_hdr, hlen);

		ipv6_select_ident(skb, fh);
		fh->nexthdr = nexthdr;
		fh->reserved = 0;
		fh->frag_off = htons(IP6_MF);
		frag_id = fh->identification;

		first_len = skb_pagelen(skb);
		skb->data_len = first_len - skb_headlen(skb);
		skb->len = first_len;
		skb->nh.ipv6h->payload_len = htons(first_len - sizeof(struct ipv6hdr));
 

		for (;;) {
			/* Prepare header of the next frame,
			 * before previous one went down. */
			if (frag) {
				frag->ip_summed = CHECKSUM_NONE;
				frag->h.raw = frag->data;
				fh = (struct frag_hdr*)__skb_push(frag, sizeof(struct frag_hdr));
				frag->nh.raw = __skb_push(frag, hlen);
				memcpy(frag->nh.raw, tmp_hdr, hlen);
				offset += skb->len - hlen - sizeof(struct frag_hdr);
				fh->nexthdr = nexthdr;
				fh->reserved = 0;
				fh->frag_off = htons(offset);
				if (frag->next != NULL)
					fh->frag_off |= htons(IP6_MF);
				fh->identification = frag_id;
				frag->nh.ipv6h->payload_len = htons(frag->len - sizeof(struct ipv6hdr));
				ip6_copy_metadata(frag, skb);
			}
			
			err = output(skb);
			if (err || !frag)
				break;

			skb = frag;
			frag = skb->next;
			skb->next = NULL;
		}

		kfree(tmp_hdr);

		if (err == 0) {
			IP6_INC_STATS(IPSTATS_MIB_FRAGOKS);
			return 0;
		}

		while (frag) {
			skb = frag->next;
			kfree_skb(frag);
			frag = skb;
		}

		IP6_INC_STATS(IPSTATS_MIB_FRAGFAILS);
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

		if ((frag = alloc_skb(len+hlen+sizeof(struct frag_hdr)+LL_RESERVED_SPACE(rt->u.dst.dev), GFP_ATOMIC)) == NULL) {
			NETDEBUG(KERN_INFO "IPv6: frag: no memory for new fragment!\n");
			IP6_INC_STATS(IPSTATS_MIB_FRAGFAILS);
			err = -ENOMEM;
			goto fail;
		}

		/*
		 *	Set up data on packet
		 */

		ip6_copy_metadata(frag, skb);
		skb_reserve(frag, LL_RESERVED_SPACE(rt->u.dst.dev));
		skb_put(frag, len + hlen + sizeof(struct frag_hdr));
		frag->nh.raw = frag->data;
		fh = (struct frag_hdr*)(frag->data + hlen);
		frag->h.raw = frag->data + hlen + sizeof(struct frag_hdr);

		/*
		 *	Charge the memory for the fragment to any owner
		 *	it might possess
		 */
		if (skb->sk)
			skb_set_owner_w(frag, skb->sk);

		/*
		 *	Copy the packet header into the new buffer.
		 */
		memcpy(frag->nh.raw, skb->data, hlen);

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
		if (skb_copy_bits(skb, ptr, frag->h.raw, len))
			BUG();
		left -= len;

		fh->frag_off = htons(offset);
		if (left > 0)
			fh->frag_off |= htons(IP6_MF);
		frag->nh.ipv6h->payload_len = htons(frag->len - sizeof(struct ipv6hdr));

		ptr += len;
		offset += len;

		/*
		 *	Put this fragment into the sending queue.
		 */

		IP6_INC_STATS(IPSTATS_MIB_FRAGCREATES);

		err = output(frag);
		if (err)
			goto fail;
	}
	kfree_skb(skb);
	IP6_INC_STATS(IPSTATS_MIB_FRAGOKS);
	return err;

fail:
	kfree_skb(skb); 
	IP6_INC_STATS(IPSTATS_MIB_FRAGFAILS);
	return err;
}

int ip6_dst_lookup(struct sock *sk, struct dst_entry **dst, struct flowi *fl)
{
	int err = 0;

	*dst = NULL;
	if (sk) {
		struct ipv6_pinfo *np = inet6_sk(sk);
	
		*dst = sk_dst_check(sk, np->dst_cookie);
		if (*dst) {
			struct rt6_info *rt = (struct rt6_info*)*dst;
	
			/* Yes, checking route validity in not connected
			 * case is not very simple. Take into account,
			 * that we do not support routing by source, TOS,
			 * and MSG_DONTROUTE 		--ANK (980726)
			 *
			 * 1. If route was host route, check that
			 *    cached destination is current.
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
			if (((rt->rt6i_dst.plen != 128 ||
			      !ipv6_addr_equal(&fl->fl6_dst,
					       &rt->rt6i_dst.addr))
			     && (np->daddr_cache == NULL ||
				 !ipv6_addr_equal(&fl->fl6_dst,
						  np->daddr_cache)))
			    || (fl->oif && fl->oif != (*dst)->dev->ifindex)) {
				dst_release(*dst);
				*dst = NULL;
			}
		}
	}

	if (*dst == NULL)
		*dst = ip6_route_output(sk, fl);

	if ((err = (*dst)->error))
		goto out_err_release;

	if (ipv6_addr_any(&fl->fl6_src)) {
		err = ipv6_get_saddr(*dst, &fl->fl6_dst, &fl->fl6_src);

		if (err)
			goto out_err_release;
	}

	return 0;

out_err_release:
	dst_release(*dst);
	*dst = NULL;
	return err;
}

EXPORT_SYMBOL_GPL(ip6_dst_lookup);

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
		skb->nh.raw = skb->data;

		/* initialize protocol header pointer */
		skb->h.raw = skb->data + fragheaderlen;

		skb->ip_summed = CHECKSUM_HW;
		skb->csum = 0;
		sk->sk_sndmsg_off = 0;
	}

	err = skb_append_datato_frags(sk,skb, getfrag, from,
				      (length - transhdrlen));
	if (!err) {
		struct frag_hdr fhdr;

		/* specify the length of each IP datagram fragment*/
		skb_shinfo(skb)->ufo_size = (mtu - fragheaderlen) - 
						sizeof(struct frag_hdr);
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
		np->cork.rt = rt;
		inet->cork.fl = *fl;
		np->cork.hop_limit = hlimit;
		np->cork.tclass = tclass;
		mtu = dst_mtu(rt->u.dst.path);
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
		exthdrlen = rt->u.dst.header_len + (opt ? opt->opt_flen : 0);
		length += exthdrlen;
		transhdrlen += exthdrlen;
	} else {
		rt = np->cork.rt;
		fl = &inet->cork.fl;
		if (inet->cork.flags & IPCORK_OPT)
			opt = np->cork.opt;
		transhdrlen = 0;
		exthdrlen = 0;
		mtu = inet->cork.fragsize;
	}

	hh_len = LL_RESERVED_SPACE(rt->u.dst.dev);

	fragheaderlen = sizeof(struct ipv6hdr) + (opt ? opt->opt_nflen : 0);
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
			skb->nh.raw = data + exthdrlen;
			data += fragheaderlen;
			skb->h.raw = data + exthdrlen;

			if (fraggap) {
				skb->csum = skb_copy_and_csum_bits(
					skb_prev, maxfraglen,
					data + transhdrlen, fraggap, 0);
				skb_prev->csum = csum_sub(skb_prev->csum,
							  skb->csum);
				data += fraggap;
				skb_trim(skb_prev, maxfraglen);
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
				skb->truesize += PAGE_SIZE;
				atomic_add(PAGE_SIZE, &sk->sk_wmem_alloc);
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
		}
		offset += copy;
		length -= copy;
	}
	return 0;
error:
	inet->cork.length -= length;
	IP6_INC_STATS(IPSTATS_MIB_OUTDISCARDS);
	return err;
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
	struct rt6_info *rt = np->cork.rt;
	struct flowi *fl = &inet->cork.fl;
	unsigned char proto = fl->proto;
	int err = 0;

	if ((skb = __skb_dequeue(&sk->sk_write_queue)) == NULL)
		goto out;
	tail_skb = &(skb_shinfo(skb)->frag_list);

	/* move skb->data to ip header from ext header */
	if (skb->data < skb->nh.raw)
		__skb_pull(skb, skb->nh.raw - skb->data);
	while ((tmp_skb = __skb_dequeue(&sk->sk_write_queue)) != NULL) {
		__skb_pull(tmp_skb, skb->h.raw - skb->nh.raw);
		*tail_skb = tmp_skb;
		tail_skb = &(tmp_skb->next);
		skb->len += tmp_skb->len;
		skb->data_len += tmp_skb->len;
		skb->truesize += tmp_skb->truesize;
		__sock_put(tmp_skb->sk);
		tmp_skb->destructor = NULL;
		tmp_skb->sk = NULL;
	}

	ipv6_addr_copy(final_dst, &fl->fl6_dst);
	__skb_pull(skb, skb->h.raw - skb->nh.raw);
	if (opt && opt->opt_flen)
		ipv6_push_frag_opts(skb, opt, &proto);
	if (opt && opt->opt_nflen)
		ipv6_push_nfrag_opts(skb, opt, &proto, &final_dst);

	skb->nh.ipv6h = hdr = (struct ipv6hdr*) skb_push(skb, sizeof(struct ipv6hdr));
	
	*(u32*)hdr = fl->fl6_flowlabel |
		     htonl(0x60000000 | ((int)np->cork.tclass << 20));

	if (skb->len <= sizeof(struct ipv6hdr) + IPV6_MAXPLEN)
		hdr->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	else
		hdr->payload_len = 0;
	hdr->hop_limit = np->cork.hop_limit;
	hdr->nexthdr = proto;
	ipv6_addr_copy(&hdr->saddr, &fl->fl6_src);
	ipv6_addr_copy(&hdr->daddr, final_dst);

	skb->priority = sk->sk_priority;

	skb->dst = dst_clone(&rt->u.dst);
	IP6_INC_STATS(IPSTATS_MIB_OUTREQUESTS);	
	err = NF_HOOK(PF_INET6, NF_IP6_LOCAL_OUT, skb, NULL, skb->dst->dev, dst_output);
	if (err) {
		if (err > 0)
			err = np->recverr ? net_xmit_errno(err) : 0;
		if (err)
			goto error;
	}

out:
	inet->cork.flags &= ~IPCORK_OPT;
	kfree(np->cork.opt);
	np->cork.opt = NULL;
	if (np->cork.rt) {
		dst_release(&np->cork.rt->u.dst);
		np->cork.rt = NULL;
		inet->cork.flags &= ~IPCORK_ALLFRAG;
	}
	memset(&inet->cork.fl, 0, sizeof(inet->cork.fl));
	return err;
error:
	goto out;
}

void ip6_flush_pending_frames(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;

	while ((skb = __skb_dequeue_tail(&sk->sk_write_queue)) != NULL) {
		IP6_INC_STATS(IPSTATS_MIB_OUTDISCARDS);
		kfree_skb(skb);
	}

	inet->cork.flags &= ~IPCORK_OPT;

	kfree(np->cork.opt);
	np->cork.opt = NULL;
	if (np->cork.rt) {
		dst_release(&np->cork.rt->u.dst);
		np->cork.rt = NULL;
		inet->cork.flags &= ~IPCORK_ALLFRAG;
	}
	memset(&inet->cork.fl, 0, sizeof(inet->cork.fl));
}
