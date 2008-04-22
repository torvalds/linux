/*
 *	IPv6 input
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Ian P. Morris		<I.P.Morris@soton.ac.uk>
 *
 *	$Id: ip6_input.c,v 1.19 2000/12/13 18:31:50 davem Exp $
 *
 *	Based in linux/net/ipv4/ip_input.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
/* Changes
 *
 * 	Mitsuru KANDA @USAGI and
 * 	YOSHIFUJI Hideaki @USAGI: Remove ipv6_parse_exthdrs().
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <linux/mroute6.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/xfrm.h>



inline int ip6_rcv_finish( struct sk_buff *skb)
{
	if (skb->dst == NULL)
		ip6_route_input(skb);

	return dst_input(skb);
}

int ipv6_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
	struct ipv6hdr *hdr;
	u32 		pkt_len;
	struct inet6_dev *idev;

	if (skb->pkt_type == PACKET_OTHERHOST) {
		kfree_skb(skb);
		return 0;
	}

	rcu_read_lock();

	idev = __in6_dev_get(skb->dev);

	IP6_INC_STATS_BH(idev, IPSTATS_MIB_INRECEIVES);

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL) {
		IP6_INC_STATS_BH(idev, IPSTATS_MIB_INDISCARDS);
		rcu_read_unlock();
		goto out;
	}

	memset(IP6CB(skb), 0, sizeof(struct inet6_skb_parm));

	/*
	 * Store incoming device index. When the packet will
	 * be queued, we cannot refer to skb->dev anymore.
	 *
	 * BTW, when we send a packet for our own local address on a
	 * non-loopback interface (e.g. ethX), it is being delivered
	 * via the loopback interface (lo) here; skb->dev = loopback_dev.
	 * It, however, should be considered as if it is being
	 * arrived via the sending interface (ethX), because of the
	 * nature of scoping architecture. --yoshfuji
	 */
	IP6CB(skb)->iif = skb->dst ? ip6_dst_idev(skb->dst)->dev->ifindex : dev->ifindex;

	if (unlikely(!pskb_may_pull(skb, sizeof(*hdr))))
		goto err;

	hdr = ipv6_hdr(skb);

	if (hdr->version != 6)
		goto err;

	skb->transport_header = skb->network_header + sizeof(*hdr);
	IP6CB(skb)->nhoff = offsetof(struct ipv6hdr, nexthdr);

	pkt_len = ntohs(hdr->payload_len);

	/* pkt_len may be zero if Jumbo payload option is present */
	if (pkt_len || hdr->nexthdr != NEXTHDR_HOP) {
		if (pkt_len + sizeof(struct ipv6hdr) > skb->len) {
			IP6_INC_STATS_BH(idev, IPSTATS_MIB_INTRUNCATEDPKTS);
			goto drop;
		}
		if (pskb_trim_rcsum(skb, pkt_len + sizeof(struct ipv6hdr))) {
			IP6_INC_STATS_BH(idev, IPSTATS_MIB_INHDRERRORS);
			goto drop;
		}
		hdr = ipv6_hdr(skb);
	}

	if (hdr->nexthdr == NEXTHDR_HOP) {
		if (ipv6_parse_hopopts(skb) < 0) {
			IP6_INC_STATS_BH(idev, IPSTATS_MIB_INHDRERRORS);
			rcu_read_unlock();
			return 0;
		}
	}

	rcu_read_unlock();

	return NF_HOOK(PF_INET6, NF_INET_PRE_ROUTING, skb, dev, NULL,
		       ip6_rcv_finish);
err:
	IP6_INC_STATS_BH(idev, IPSTATS_MIB_INHDRERRORS);
drop:
	rcu_read_unlock();
	kfree_skb(skb);
out:
	return 0;
}

/*
 *	Deliver the packet to the host
 */


static int ip6_input_finish(struct sk_buff *skb)
{
	struct inet6_protocol *ipprot;
	unsigned int nhoff;
	int nexthdr, raw;
	u8 hash;
	struct inet6_dev *idev;

	/*
	 *	Parse extension headers
	 */

	rcu_read_lock();
resubmit:
	idev = ip6_dst_idev(skb->dst);
	if (!pskb_pull(skb, skb_transport_offset(skb)))
		goto discard;
	nhoff = IP6CB(skb)->nhoff;
	nexthdr = skb_network_header(skb)[nhoff];

	raw = raw6_local_deliver(skb, nexthdr);

	hash = nexthdr & (MAX_INET_PROTOS - 1);
	if ((ipprot = rcu_dereference(inet6_protos[hash])) != NULL) {
		int ret;

		if (ipprot->flags & INET6_PROTO_FINAL) {
			struct ipv6hdr *hdr;

			/* Free reference early: we don't need it any more,
			   and it may hold ip_conntrack module loaded
			   indefinitely. */
			nf_reset(skb);

			skb_postpull_rcsum(skb, skb_network_header(skb),
					   skb_network_header_len(skb));
			hdr = ipv6_hdr(skb);
			if (ipv6_addr_is_multicast(&hdr->daddr) &&
			    !ipv6_chk_mcast_addr(skb->dev, &hdr->daddr,
			    &hdr->saddr) &&
			    !ipv6_is_mld(skb, nexthdr))
				goto discard;
		}
		if (!(ipprot->flags & INET6_PROTO_NOPOLICY) &&
		    !xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
			goto discard;

		ret = ipprot->handler(skb);
		if (ret > 0)
			goto resubmit;
		else if (ret == 0)
			IP6_INC_STATS_BH(idev, IPSTATS_MIB_INDELIVERS);
	} else {
		if (!raw) {
			if (xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
				IP6_INC_STATS_BH(idev, IPSTATS_MIB_INUNKNOWNPROTOS);
				icmpv6_send(skb, ICMPV6_PARAMPROB,
					    ICMPV6_UNK_NEXTHDR, nhoff,
					    skb->dev);
			}
		} else
			IP6_INC_STATS_BH(idev, IPSTATS_MIB_INDELIVERS);
		kfree_skb(skb);
	}
	rcu_read_unlock();
	return 0;

discard:
	IP6_INC_STATS_BH(idev, IPSTATS_MIB_INDISCARDS);
	rcu_read_unlock();
	kfree_skb(skb);
	return 0;
}


int ip6_input(struct sk_buff *skb)
{
	return NF_HOOK(PF_INET6, NF_INET_LOCAL_IN, skb, skb->dev, NULL,
		       ip6_input_finish);
}

int ip6_mc_input(struct sk_buff *skb)
{
	struct ipv6hdr *hdr;
	int deliver;

	IP6_INC_STATS_BH(ip6_dst_idev(skb->dst), IPSTATS_MIB_INMCASTPKTS);

	hdr = ipv6_hdr(skb);
	deliver = ipv6_chk_mcast_addr(skb->dev, &hdr->daddr, NULL);

#ifdef CONFIG_IPV6_MROUTE
	/*
	 *      IPv6 multicast router mode is now supported ;)
	 */
	if (ipv6_devconf.mc_forwarding &&
	    likely(!(IP6CB(skb)->flags & IP6SKB_FORWARDED))) {
		/*
		 * Okay, we try to forward - split and duplicate
		 * packets.
		 */
		struct sk_buff *skb2;
		struct inet6_skb_parm *opt = IP6CB(skb);

		/* Check for MLD */
		if (unlikely(opt->ra)) {
			/* Check if this is a mld message */
			u8 *ptr = skb_network_header(skb) + opt->ra;
			struct icmp6hdr *icmp6;
			u8 nexthdr = hdr->nexthdr;
			int offset;

			/* Check if the value of Router Alert
			 * is for MLD (0x0000).
			 */
			if ((ptr[2] | ptr[3]) == 0) {
				deliver = 0;

				if (!ipv6_ext_hdr(nexthdr)) {
					/* BUG */
					goto out;
				}
				offset = ipv6_skip_exthdr(skb, sizeof(*hdr),
							  &nexthdr);
				if (offset < 0)
					goto out;

				if (nexthdr != IPPROTO_ICMPV6)
					goto out;

				if (!pskb_may_pull(skb, (skb_network_header(skb) +
						   offset + 1 - skb->data)))
					goto out;

				icmp6 = (struct icmp6hdr *)(skb_network_header(skb) + offset);

				switch (icmp6->icmp6_type) {
				case ICMPV6_MGM_QUERY:
				case ICMPV6_MGM_REPORT:
				case ICMPV6_MGM_REDUCTION:
				case ICMPV6_MLD2_REPORT:
					deliver = 1;
					break;
				}
				goto out;
			}
			/* unknown RA - process it normally */
		}

		if (deliver)
			skb2 = skb_clone(skb, GFP_ATOMIC);
		else {
			skb2 = skb;
			skb = NULL;
		}

		if (skb2) {
			skb2->dev = skb2->dst->dev;
			ip6_mr_input(skb2);
		}
	}
out:
#endif
	if (likely(deliver))
		ip6_input(skb);
	else {
		/* discard */
		kfree_skb(skb);
	}

	return 0;
}
