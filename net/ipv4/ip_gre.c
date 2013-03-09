/*
 *	Linux NET3:	GRE over IP protocol decoder.
 *
 *	Authors: Alexey Kuznetsov (kuznet@ms2.inr.ac.ru)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <linux/in6.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/netfilter_ipv4.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>

#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/ipip.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/gre.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#endif

/*
   Problems & solutions
   --------------------

   1. The most important issue is detecting local dead loops.
   They would cause complete host lockup in transmit, which
   would be "resolved" by stack overflow or, if queueing is enabled,
   with infinite looping in net_bh.

   We cannot track such dead loops during route installation,
   it is infeasible task. The most general solutions would be
   to keep skb->encapsulation counter (sort of local ttl),
   and silently drop packet when it expires. It is a good
   solution, but it supposes maintaining new variable in ALL
   skb, even if no tunneling is used.

   Current solution: xmit_recursion breaks dead loops. This is a percpu
   counter, since when we enter the first ndo_xmit(), cpu migration is
   forbidden. We force an exit if this counter reaches RECURSION_LIMIT

   2. Networking dead loops would not kill routers, but would really
   kill network. IP hop limit plays role of "t->recursion" in this case,
   if we copy it from packet being encapsulated to upper header.
   It is very good solution, but it introduces two problems:

   - Routing protocols, using packets with ttl=1 (OSPF, RIP2),
     do not work over tunnels.
   - traceroute does not work. I planned to relay ICMP from tunnel,
     so that this problem would be solved and traceroute output
     would even more informative. This idea appeared to be wrong:
     only Linux complies to rfc1812 now (yes, guys, Linux is the only
     true router now :-)), all routers (at least, in neighbourhood of mine)
     return only 8 bytes of payload. It is the end.

   Hence, if we want that OSPF worked or traceroute said something reasonable,
   we should search for another solution.

   One of them is to parse packet trying to detect inner encapsulation
   made by our node. It is difficult or even impossible, especially,
   taking into account fragmentation. TO be short, ttl is not solution at all.

   Current solution: The solution was UNEXPECTEDLY SIMPLE.
   We force DF flag on tunnels with preconfigured hop limit,
   that is ALL. :-) Well, it does not remove the problem completely,
   but exponential growth of network traffic is changed to linear
   (branches, that exceed pmtu are pruned) and tunnel mtu
   rapidly degrades to value <68, where looping stops.
   Yes, it is not good if there exists a router in the loop,
   which does not force DF, even when encapsulating packets have DF set.
   But it is not our problem! Nobody could accuse us, we made
   all that we could make. Even if it is your gated who injected
   fatal route to network, even if it were you who configured
   fatal static route: you are innocent. :-)



   3. Really, ipv4/ipip.c, ipv4/ip_gre.c and ipv6/sit.c contain
   practically identical code. It would be good to glue them
   together, but it is not very evident, how to make them modular.
   sit is integral part of IPv6, ipip and gre are naturally modular.
   We could extract common parts (hash table, ioctl etc)
   to a separate module (ip_tunnel.c).

   Alexey Kuznetsov.
 */

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

static struct rtnl_link_ops ipgre_link_ops __read_mostly;
static int ipgre_tunnel_init(struct net_device *dev);
static void ipgre_tunnel_setup(struct net_device *dev);
static int ipgre_tunnel_bind_dev(struct net_device *dev);

/* Fallback tunnel: no source, no destination, no key, no options */

#define HASH_SIZE  16

static int ipgre_net_id __read_mostly;
struct ipgre_net {
	struct ip_tunnel __rcu *tunnels[4][HASH_SIZE];

	struct net_device *fb_tunnel_dev;
};

/* Tunnel hash table */

/*
   4 hash tables:

   3: (remote,local)
   2: (remote,*)
   1: (*,local)
   0: (*,*)

   We require exact key match i.e. if a key is present in packet
   it will match only tunnel with the same key; if it is not present,
   it will match only keyless tunnel.

   All keysless packets, if not matched configured keyless tunnels
   will match fallback tunnel.
 */

#define HASH(addr) (((__force u32)addr^((__force u32)addr>>4))&0xF)

#define tunnels_r_l	tunnels[3]
#define tunnels_r	tunnels[2]
#define tunnels_l	tunnels[1]
#define tunnels_wc	tunnels[0]

static struct rtnl_link_stats64 *ipgre_get_stats64(struct net_device *dev,
						   struct rtnl_link_stats64 *tot)
{
	int i;

	for_each_possible_cpu(i) {
		const struct pcpu_tstats *tstats = per_cpu_ptr(dev->tstats, i);
		u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_bh(&tstats->syncp);
			rx_packets = tstats->rx_packets;
			tx_packets = tstats->tx_packets;
			rx_bytes = tstats->rx_bytes;
			tx_bytes = tstats->tx_bytes;
		} while (u64_stats_fetch_retry_bh(&tstats->syncp, start));

		tot->rx_packets += rx_packets;
		tot->tx_packets += tx_packets;
		tot->rx_bytes   += rx_bytes;
		tot->tx_bytes   += tx_bytes;
	}

	tot->multicast = dev->stats.multicast;
	tot->rx_crc_errors = dev->stats.rx_crc_errors;
	tot->rx_fifo_errors = dev->stats.rx_fifo_errors;
	tot->rx_length_errors = dev->stats.rx_length_errors;
	tot->rx_frame_errors = dev->stats.rx_frame_errors;
	tot->rx_errors = dev->stats.rx_errors;

	tot->tx_fifo_errors = dev->stats.tx_fifo_errors;
	tot->tx_carrier_errors = dev->stats.tx_carrier_errors;
	tot->tx_dropped = dev->stats.tx_dropped;
	tot->tx_aborted_errors = dev->stats.tx_aborted_errors;
	tot->tx_errors = dev->stats.tx_errors;

	return tot;
}

/* Does key in tunnel parameters match packet */
static bool ipgre_key_match(const struct ip_tunnel_parm *p,
			    __be16 flags, __be32 key)
{
	if (p->i_flags & GRE_KEY) {
		if (flags & GRE_KEY)
			return key == p->i_key;
		else
			return false;	/* key expected, none present */
	} else
		return !(flags & GRE_KEY);
}

/* Given src, dst and key, find appropriate for input tunnel. */

static struct ip_tunnel *ipgre_tunnel_lookup(struct net_device *dev,
					     __be32 remote, __be32 local,
					     __be16 flags, __be32 key,
					     __be16 gre_proto)
{
	struct net *net = dev_net(dev);
	int link = dev->ifindex;
	unsigned int h0 = HASH(remote);
	unsigned int h1 = HASH(key);
	struct ip_tunnel *t, *cand = NULL;
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);
	int dev_type = (gre_proto == htons(ETH_P_TEB)) ?
		       ARPHRD_ETHER : ARPHRD_IPGRE;
	int score, cand_score = 4;

	for_each_ip_tunnel_rcu(t, ign->tunnels_r_l[h0 ^ h1]) {
		if (local != t->parms.iph.saddr ||
		    remote != t->parms.iph.daddr ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (!ipgre_key_match(&t->parms, flags, key))
			continue;

		if (t->dev->type != ARPHRD_IPGRE &&
		    t->dev->type != dev_type)
			continue;

		score = 0;
		if (t->parms.link != link)
			score |= 1;
		if (t->dev->type != dev_type)
			score |= 2;
		if (score == 0)
			return t;

		if (score < cand_score) {
			cand = t;
			cand_score = score;
		}
	}

	for_each_ip_tunnel_rcu(t, ign->tunnels_r[h0 ^ h1]) {
		if (remote != t->parms.iph.daddr ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (!ipgre_key_match(&t->parms, flags, key))
			continue;

		if (t->dev->type != ARPHRD_IPGRE &&
		    t->dev->type != dev_type)
			continue;

		score = 0;
		if (t->parms.link != link)
			score |= 1;
		if (t->dev->type != dev_type)
			score |= 2;
		if (score == 0)
			return t;

		if (score < cand_score) {
			cand = t;
			cand_score = score;
		}
	}

	for_each_ip_tunnel_rcu(t, ign->tunnels_l[h1]) {
		if ((local != t->parms.iph.saddr &&
		     (local != t->parms.iph.daddr ||
		      !ipv4_is_multicast(local))) ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (!ipgre_key_match(&t->parms, flags, key))
			continue;

		if (t->dev->type != ARPHRD_IPGRE &&
		    t->dev->type != dev_type)
			continue;

		score = 0;
		if (t->parms.link != link)
			score |= 1;
		if (t->dev->type != dev_type)
			score |= 2;
		if (score == 0)
			return t;

		if (score < cand_score) {
			cand = t;
			cand_score = score;
		}
	}

	for_each_ip_tunnel_rcu(t, ign->tunnels_wc[h1]) {
		if (t->parms.i_key != key ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (t->dev->type != ARPHRD_IPGRE &&
		    t->dev->type != dev_type)
			continue;

		score = 0;
		if (t->parms.link != link)
			score |= 1;
		if (t->dev->type != dev_type)
			score |= 2;
		if (score == 0)
			return t;

		if (score < cand_score) {
			cand = t;
			cand_score = score;
		}
	}

	if (cand != NULL)
		return cand;

	dev = ign->fb_tunnel_dev;
	if (dev->flags & IFF_UP)
		return netdev_priv(dev);

	return NULL;
}

static struct ip_tunnel __rcu **__ipgre_bucket(struct ipgre_net *ign,
		struct ip_tunnel_parm *parms)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	__be32 key = parms->i_key;
	unsigned int h = HASH(key);
	int prio = 0;

	if (local)
		prio |= 1;
	if (remote && !ipv4_is_multicast(remote)) {
		prio |= 2;
		h ^= HASH(remote);
	}

	return &ign->tunnels[prio][h];
}

static inline struct ip_tunnel __rcu **ipgre_bucket(struct ipgre_net *ign,
		struct ip_tunnel *t)
{
	return __ipgre_bucket(ign, &t->parms);
}

static void ipgre_tunnel_link(struct ipgre_net *ign, struct ip_tunnel *t)
{
	struct ip_tunnel __rcu **tp = ipgre_bucket(ign, t);

	rcu_assign_pointer(t->next, rtnl_dereference(*tp));
	rcu_assign_pointer(*tp, t);
}

static void ipgre_tunnel_unlink(struct ipgre_net *ign, struct ip_tunnel *t)
{
	struct ip_tunnel __rcu **tp;
	struct ip_tunnel *iter;

	for (tp = ipgre_bucket(ign, t);
	     (iter = rtnl_dereference(*tp)) != NULL;
	     tp = &iter->next) {
		if (t == iter) {
			rcu_assign_pointer(*tp, t->next);
			break;
		}
	}
}

static struct ip_tunnel *ipgre_tunnel_find(struct net *net,
					   struct ip_tunnel_parm *parms,
					   int type)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	__be32 key = parms->i_key;
	int link = parms->link;
	struct ip_tunnel *t;
	struct ip_tunnel __rcu **tp;
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);

	for (tp = __ipgre_bucket(ign, parms);
	     (t = rtnl_dereference(*tp)) != NULL;
	     tp = &t->next)
		if (local == t->parms.iph.saddr &&
		    remote == t->parms.iph.daddr &&
		    key == t->parms.i_key &&
		    link == t->parms.link &&
		    type == t->dev->type)
			break;

	return t;
}

static struct ip_tunnel *ipgre_tunnel_locate(struct net *net,
		struct ip_tunnel_parm *parms, int create)
{
	struct ip_tunnel *t, *nt;
	struct net_device *dev;
	char name[IFNAMSIZ];
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);

	t = ipgre_tunnel_find(net, parms, ARPHRD_IPGRE);
	if (t || !create)
		return t;

	if (parms->name[0])
		strlcpy(name, parms->name, IFNAMSIZ);
	else
		strcpy(name, "gre%d");

	dev = alloc_netdev(sizeof(*t), name, ipgre_tunnel_setup);
	if (!dev)
		return NULL;

	dev_net_set(dev, net);

	nt = netdev_priv(dev);
	nt->parms = *parms;
	dev->rtnl_link_ops = &ipgre_link_ops;

	dev->mtu = ipgre_tunnel_bind_dev(dev);

	if (register_netdevice(dev) < 0)
		goto failed_free;

	/* Can use a lockless transmit, unless we generate output sequences */
	if (!(nt->parms.o_flags & GRE_SEQ))
		dev->features |= NETIF_F_LLTX;

	dev_hold(dev);
	ipgre_tunnel_link(ign, nt);
	return nt;

failed_free:
	free_netdev(dev);
	return NULL;
}

static void ipgre_tunnel_uninit(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);

	ipgre_tunnel_unlink(ign, netdev_priv(dev));
	dev_put(dev);
}


static void ipgre_err(struct sk_buff *skb, u32 info)
{

/* All the routers (except for Linux) return only
   8 bytes of packet payload. It means, that precise relaying of
   ICMP in the real Internet is absolutely infeasible.

   Moreover, Cisco "wise men" put GRE key to the third word
   in GRE header. It makes impossible maintaining even soft state for keyed
   GRE tunnels with enabled checksum. Tell them "thank you".

   Well, I wonder, rfc1812 was written by Cisco employee,
   what the hell these idiots break standards established
   by themselves???
 */

	const struct iphdr *iph = (const struct iphdr *)skb->data;
	__be16	     *p = (__be16 *)(skb->data+(iph->ihl<<2));
	int grehlen = (iph->ihl<<2) + 4;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	struct ip_tunnel *t;
	__be16 flags;
	__be32 key = 0;

	flags = p[0];
	if (flags&(GRE_CSUM|GRE_KEY|GRE_SEQ|GRE_ROUTING|GRE_VERSION)) {
		if (flags&(GRE_VERSION|GRE_ROUTING))
			return;
		if (flags&GRE_KEY) {
			grehlen += 4;
			if (flags&GRE_CSUM)
				grehlen += 4;
		}
	}

	/* If only 8 bytes returned, keyed message will be dropped here */
	if (skb_headlen(skb) < grehlen)
		return;

	if (flags & GRE_KEY)
		key = *(((__be32 *)p) + (grehlen / 4) - 1);

	switch (type) {
	default:
	case ICMP_PARAMETERPROB:
		return;

	case ICMP_DEST_UNREACH:
		switch (code) {
		case ICMP_SR_FAILED:
		case ICMP_PORT_UNREACH:
			/* Impossible event. */
			return;
		default:
			/* All others are translated to HOST_UNREACH.
			   rfc2003 contains "deep thoughts" about NET_UNREACH,
			   I believe they are just ether pollution. --ANK
			 */
			break;
		}
		break;
	case ICMP_TIME_EXCEEDED:
		if (code != ICMP_EXC_TTL)
			return;
		break;

	case ICMP_REDIRECT:
		break;
	}

	t = ipgre_tunnel_lookup(skb->dev, iph->daddr, iph->saddr,
				flags, key, p[1]);

	if (t == NULL)
		return;

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		ipv4_update_pmtu(skb, dev_net(skb->dev), info,
				 t->parms.link, 0, IPPROTO_GRE, 0);
		return;
	}
	if (type == ICMP_REDIRECT) {
		ipv4_redirect(skb, dev_net(skb->dev), t->parms.link, 0,
			      IPPROTO_GRE, 0);
		return;
	}
	if (t->parms.iph.daddr == 0 ||
	    ipv4_is_multicast(t->parms.iph.daddr))
		return;

	if (t->parms.iph.ttl == 0 && type == ICMP_TIME_EXCEEDED)
		return;

	if (time_before(jiffies, t->err_time + IPTUNNEL_ERR_TIMEO))
		t->err_count++;
	else
		t->err_count = 1;
	t->err_time = jiffies;
}

static inline u8
ipgre_ecn_encapsulate(u8 tos, const struct iphdr *old_iph, struct sk_buff *skb)
{
	u8 inner = 0;
	if (skb->protocol == htons(ETH_P_IP))
		inner = old_iph->tos;
	else if (skb->protocol == htons(ETH_P_IPV6))
		inner = ipv6_get_dsfield((const struct ipv6hdr *)old_iph);
	return INET_ECN_encapsulate(tos, inner);
}

static int ipgre_rcv(struct sk_buff *skb)
{
	const struct iphdr *iph;
	u8     *h;
	__be16    flags;
	__sum16   csum = 0;
	__be32 key = 0;
	u32    seqno = 0;
	struct ip_tunnel *tunnel;
	int    offset = 4;
	__be16 gre_proto;
	int    err;

	if (!pskb_may_pull(skb, 16))
		goto drop;

	iph = ip_hdr(skb);
	h = skb->data;
	flags = *(__be16 *)h;

	if (flags&(GRE_CSUM|GRE_KEY|GRE_ROUTING|GRE_SEQ|GRE_VERSION)) {
		/* - Version must be 0.
		   - We do not support routing headers.
		 */
		if (flags&(GRE_VERSION|GRE_ROUTING))
			goto drop;

		if (flags&GRE_CSUM) {
			switch (skb->ip_summed) {
			case CHECKSUM_COMPLETE:
				csum = csum_fold(skb->csum);
				if (!csum)
					break;
				/* fall through */
			case CHECKSUM_NONE:
				skb->csum = 0;
				csum = __skb_checksum_complete(skb);
				skb->ip_summed = CHECKSUM_COMPLETE;
			}
			offset += 4;
		}
		if (flags&GRE_KEY) {
			key = *(__be32 *)(h + offset);
			offset += 4;
		}
		if (flags&GRE_SEQ) {
			seqno = ntohl(*(__be32 *)(h + offset));
			offset += 4;
		}
	}

	gre_proto = *(__be16 *)(h + 2);

	tunnel = ipgre_tunnel_lookup(skb->dev,
				     iph->saddr, iph->daddr, flags, key,
				     gre_proto);
	if (tunnel) {
		struct pcpu_tstats *tstats;

		secpath_reset(skb);

		skb->protocol = gre_proto;
		/* WCCP version 1 and 2 protocol decoding.
		 * - Change protocol to IP
		 * - When dealing with WCCPv2, Skip extra 4 bytes in GRE header
		 */
		if (flags == 0 && gre_proto == htons(ETH_P_WCCP)) {
			skb->protocol = htons(ETH_P_IP);
			if ((*(h + offset) & 0xF0) != 0x40)
				offset += 4;
		}

		skb->mac_header = skb->network_header;
		__pskb_pull(skb, offset);
		skb_postpull_rcsum(skb, skb_transport_header(skb), offset);
		skb->pkt_type = PACKET_HOST;
#ifdef CONFIG_NET_IPGRE_BROADCAST
		if (ipv4_is_multicast(iph->daddr)) {
			/* Looped back packet, drop it! */
			if (rt_is_output_route(skb_rtable(skb)))
				goto drop;
			tunnel->dev->stats.multicast++;
			skb->pkt_type = PACKET_BROADCAST;
		}
#endif

		if (((flags&GRE_CSUM) && csum) ||
		    (!(flags&GRE_CSUM) && tunnel->parms.i_flags&GRE_CSUM)) {
			tunnel->dev->stats.rx_crc_errors++;
			tunnel->dev->stats.rx_errors++;
			goto drop;
		}
		if (tunnel->parms.i_flags&GRE_SEQ) {
			if (!(flags&GRE_SEQ) ||
			    (tunnel->i_seqno && (s32)(seqno - tunnel->i_seqno) < 0)) {
				tunnel->dev->stats.rx_fifo_errors++;
				tunnel->dev->stats.rx_errors++;
				goto drop;
			}
			tunnel->i_seqno = seqno + 1;
		}

		/* Warning: All skb pointers will be invalidated! */
		if (tunnel->dev->type == ARPHRD_ETHER) {
			if (!pskb_may_pull(skb, ETH_HLEN)) {
				tunnel->dev->stats.rx_length_errors++;
				tunnel->dev->stats.rx_errors++;
				goto drop;
			}

			iph = ip_hdr(skb);
			skb->protocol = eth_type_trans(skb, tunnel->dev);
			skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);
		}

		__skb_tunnel_rx(skb, tunnel->dev);

		skb_reset_network_header(skb);
		err = IP_ECN_decapsulate(iph, skb);
		if (unlikely(err)) {
			if (log_ecn_error)
				net_info_ratelimited("non-ECT from %pI4 with TOS=%#x\n",
						     &iph->saddr, iph->tos);
			if (err > 1) {
				++tunnel->dev->stats.rx_frame_errors;
				++tunnel->dev->stats.rx_errors;
				goto drop;
			}
		}

		tstats = this_cpu_ptr(tunnel->dev->tstats);
		u64_stats_update_begin(&tstats->syncp);
		tstats->rx_packets++;
		tstats->rx_bytes += skb->len;
		u64_stats_update_end(&tstats->syncp);

		gro_cells_receive(&tunnel->gro_cells, skb);
		return 0;
	}
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

drop:
	kfree_skb(skb);
	return 0;
}

static struct sk_buff *handle_offloads(struct ip_tunnel *tunnel, struct sk_buff *skb)
{
	int err;

	if (skb_is_gso(skb)) {
		err = skb_unclone(skb, GFP_ATOMIC);
		if (unlikely(err))
			goto error;
		skb_shinfo(skb)->gso_type |= SKB_GSO_GRE;
		return skb;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL &&
		   tunnel->parms.o_flags&GRE_CSUM) {
		err = skb_checksum_help(skb);
		if (unlikely(err))
			goto error;
	} else if (skb->ip_summed != CHECKSUM_PARTIAL)
		skb->ip_summed = CHECKSUM_NONE;

	return skb;

error:
	kfree_skb(skb);
	return ERR_PTR(err);
}

static netdev_tx_t ipgre_tunnel_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	const struct iphdr  *old_iph;
	const struct iphdr  *tiph;
	struct flowi4 fl4;
	u8     tos;
	__be16 df;
	struct rtable *rt;     			/* Route to the other host */
	struct net_device *tdev;		/* Device to other host */
	struct iphdr  *iph;			/* Our new IP header */
	unsigned int max_headroom;		/* The extra header space needed */
	int    gre_hlen;
	__be32 dst;
	int    mtu;
	u8     ttl;
	int    err;

	skb = handle_offloads(tunnel, skb);
	if (IS_ERR(skb)) {
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	if (!skb->encapsulation) {
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
	}

	old_iph = ip_hdr(skb);

	if (dev->type == ARPHRD_ETHER)
		IPCB(skb)->flags = 0;

	if (dev->header_ops && dev->type == ARPHRD_IPGRE) {
		gre_hlen = 0;
		if (skb->protocol == htons(ETH_P_IP))
			tiph = (const struct iphdr *)skb->data;
		else
			tiph = &tunnel->parms.iph;
	} else {
		gre_hlen = tunnel->hlen;
		tiph = &tunnel->parms.iph;
	}

	if ((dst = tiph->daddr) == 0) {
		/* NBMA tunnel */

		if (skb_dst(skb) == NULL) {
			dev->stats.tx_fifo_errors++;
			goto tx_error;
		}

		if (skb->protocol == htons(ETH_P_IP)) {
			rt = skb_rtable(skb);
			dst = rt_nexthop(rt, old_iph->daddr);
		}
#if IS_ENABLED(CONFIG_IPV6)
		else if (skb->protocol == htons(ETH_P_IPV6)) {
			const struct in6_addr *addr6;
			struct neighbour *neigh;
			bool do_tx_error_icmp;
			int addr_type;

			neigh = dst_neigh_lookup(skb_dst(skb), &ipv6_hdr(skb)->daddr);
			if (neigh == NULL)
				goto tx_error;

			addr6 = (const struct in6_addr *)&neigh->primary_key;
			addr_type = ipv6_addr_type(addr6);

			if (addr_type == IPV6_ADDR_ANY) {
				addr6 = &ipv6_hdr(skb)->daddr;
				addr_type = ipv6_addr_type(addr6);
			}

			if ((addr_type & IPV6_ADDR_COMPATv4) == 0)
				do_tx_error_icmp = true;
			else {
				do_tx_error_icmp = false;
				dst = addr6->s6_addr32[3];
			}
			neigh_release(neigh);
			if (do_tx_error_icmp)
				goto tx_error_icmp;
		}
#endif
		else
			goto tx_error;
	}

	ttl = tiph->ttl;
	tos = tiph->tos;
	if (tos & 0x1) {
		tos &= ~0x1;
		if (skb->protocol == htons(ETH_P_IP))
			tos = old_iph->tos;
		else if (skb->protocol == htons(ETH_P_IPV6))
			tos = ipv6_get_dsfield((const struct ipv6hdr *)old_iph);
	}

	rt = ip_route_output_gre(dev_net(dev), &fl4, dst, tiph->saddr,
				 tunnel->parms.o_key, RT_TOS(tos),
				 tunnel->parms.link);
	if (IS_ERR(rt)) {
		dev->stats.tx_carrier_errors++;
		goto tx_error;
	}
	tdev = rt->dst.dev;

	if (tdev == dev) {
		ip_rt_put(rt);
		dev->stats.collisions++;
		goto tx_error;
	}

	df = tiph->frag_off;
	if (df)
		mtu = dst_mtu(&rt->dst) - dev->hard_header_len - tunnel->hlen;
	else
		mtu = skb_dst(skb) ? dst_mtu(skb_dst(skb)) : dev->mtu;

	if (skb_dst(skb))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), NULL, skb, mtu);

	if (skb->protocol == htons(ETH_P_IP)) {
		df |= (old_iph->frag_off&htons(IP_DF));

		if (!skb_is_gso(skb) &&
		    (old_iph->frag_off&htons(IP_DF)) &&
		    mtu < ntohs(old_iph->tot_len)) {
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
			ip_rt_put(rt);
			goto tx_error;
		}
	}
#if IS_ENABLED(CONFIG_IPV6)
	else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct rt6_info *rt6 = (struct rt6_info *)skb_dst(skb);

		if (rt6 && mtu < dst_mtu(skb_dst(skb)) && mtu >= IPV6_MIN_MTU) {
			if ((tunnel->parms.iph.daddr &&
			     !ipv4_is_multicast(tunnel->parms.iph.daddr)) ||
			    rt6->rt6i_dst.plen == 128) {
				rt6->rt6i_flags |= RTF_MODIFIED;
				dst_metric_set(skb_dst(skb), RTAX_MTU, mtu);
			}
		}

		if (!skb_is_gso(skb) &&
		    mtu >= IPV6_MIN_MTU &&
		    mtu < skb->len - tunnel->hlen + gre_hlen) {
			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
			ip_rt_put(rt);
			goto tx_error;
		}
	}
#endif

	if (tunnel->err_count > 0) {
		if (time_before(jiffies,
				tunnel->err_time + IPTUNNEL_ERR_TIMEO)) {
			tunnel->err_count--;

			dst_link_failure(skb);
		} else
			tunnel->err_count = 0;
	}

	max_headroom = LL_RESERVED_SPACE(tdev) + gre_hlen + rt->dst.header_len;

	if (skb_headroom(skb) < max_headroom || skb_shared(skb)||
	    (skb_cloned(skb) && !skb_clone_writable(skb, 0))) {
		struct sk_buff *new_skb = skb_realloc_headroom(skb, max_headroom);
		if (max_headroom > dev->needed_headroom)
			dev->needed_headroom = max_headroom;
		if (!new_skb) {
			ip_rt_put(rt);
			dev->stats.tx_dropped++;
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}
		if (skb->sk)
			skb_set_owner_w(new_skb, skb->sk);
		dev_kfree_skb(skb);
		skb = new_skb;
		old_iph = ip_hdr(skb);
		/* Warning : tiph value might point to freed memory */
	}

	skb_push(skb, gre_hlen);
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, sizeof(*iph));
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED |
			      IPSKB_REROUTED);
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/*
	 *	Push down and install the IPIP header.
	 */

	iph 			=	ip_hdr(skb);
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr) >> 2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_GRE;
	iph->tos		=	ipgre_ecn_encapsulate(tos, old_iph, skb);
	iph->daddr		=	fl4.daddr;
	iph->saddr		=	fl4.saddr;
	iph->ttl		=	ttl;

	tunnel_ip_select_ident(skb, old_iph, &rt->dst);

	if (ttl == 0) {
		if (skb->protocol == htons(ETH_P_IP))
			iph->ttl = old_iph->ttl;
#if IS_ENABLED(CONFIG_IPV6)
		else if (skb->protocol == htons(ETH_P_IPV6))
			iph->ttl = ((const struct ipv6hdr *)old_iph)->hop_limit;
#endif
		else
			iph->ttl = ip4_dst_hoplimit(&rt->dst);
	}

	((__be16 *)(iph + 1))[0] = tunnel->parms.o_flags;
	((__be16 *)(iph + 1))[1] = (dev->type == ARPHRD_ETHER) ?
				   htons(ETH_P_TEB) : skb->protocol;

	if (tunnel->parms.o_flags&(GRE_KEY|GRE_CSUM|GRE_SEQ)) {
		__be32 *ptr = (__be32 *)(((u8 *)iph) + tunnel->hlen - 4);

		if (tunnel->parms.o_flags&GRE_SEQ) {
			++tunnel->o_seqno;
			*ptr = htonl(tunnel->o_seqno);
			ptr--;
		}
		if (tunnel->parms.o_flags&GRE_KEY) {
			*ptr = tunnel->parms.o_key;
			ptr--;
		}
		/* Skip GRE checksum if skb is getting offloaded. */
		if (!(skb_shinfo(skb)->gso_type & SKB_GSO_GRE) &&
		    (tunnel->parms.o_flags&GRE_CSUM)) {
			int offset = skb_transport_offset(skb);

			if (skb_has_shared_frag(skb)) {
				err = __skb_linearize(skb);
				if (err)
					goto tx_error;
			}

			*ptr = 0;
			*(__sum16 *)ptr = csum_fold(skb_checksum(skb, offset,
								 skb->len - offset,
								 0));
		}
	}

	iptunnel_xmit(skb, dev);
	return NETDEV_TX_OK;

#if IS_ENABLED(CONFIG_IPV6)
tx_error_icmp:
	dst_link_failure(skb);
#endif
tx_error:
	dev->stats.tx_errors++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int ipgre_tunnel_bind_dev(struct net_device *dev)
{
	struct net_device *tdev = NULL;
	struct ip_tunnel *tunnel;
	const struct iphdr *iph;
	int hlen = LL_MAX_HEADER;
	int mtu = ETH_DATA_LEN;
	int addend = sizeof(struct iphdr) + 4;

	tunnel = netdev_priv(dev);
	iph = &tunnel->parms.iph;

	/* Guess output device to choose reasonable mtu and needed_headroom */

	if (iph->daddr) {
		struct flowi4 fl4;
		struct rtable *rt;

		rt = ip_route_output_gre(dev_net(dev), &fl4,
					 iph->daddr, iph->saddr,
					 tunnel->parms.o_key,
					 RT_TOS(iph->tos),
					 tunnel->parms.link);
		if (!IS_ERR(rt)) {
			tdev = rt->dst.dev;
			ip_rt_put(rt);
		}

		if (dev->type != ARPHRD_ETHER)
			dev->flags |= IFF_POINTOPOINT;
	}

	if (!tdev && tunnel->parms.link)
		tdev = __dev_get_by_index(dev_net(dev), tunnel->parms.link);

	if (tdev) {
		hlen = tdev->hard_header_len + tdev->needed_headroom;
		mtu = tdev->mtu;
	}
	dev->iflink = tunnel->parms.link;

	/* Precalculate GRE options length */
	if (tunnel->parms.o_flags&(GRE_CSUM|GRE_KEY|GRE_SEQ)) {
		if (tunnel->parms.o_flags&GRE_CSUM)
			addend += 4;
		if (tunnel->parms.o_flags&GRE_KEY)
			addend += 4;
		if (tunnel->parms.o_flags&GRE_SEQ)
			addend += 4;
	}
	dev->needed_headroom = addend + hlen;
	mtu -= dev->hard_header_len + addend;

	if (mtu < 68)
		mtu = 68;

	tunnel->hlen = addend;
	/* TCP offload with GRE SEQ is not supported. */
	if (!(tunnel->parms.o_flags & GRE_SEQ)) {
		dev->features		|= NETIF_F_GSO_SOFTWARE;
		dev->hw_features	|= NETIF_F_GSO_SOFTWARE;
	}

	return mtu;
}

static int
ipgre_tunnel_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip_tunnel_parm p;
	struct ip_tunnel *t;
	struct net *net = dev_net(dev);
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);

	switch (cmd) {
	case SIOCGETTUNNEL:
		t = NULL;
		if (dev == ign->fb_tunnel_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p))) {
				err = -EFAULT;
				break;
			}
			t = ipgre_tunnel_locate(net, &p, 0);
		}
		if (t == NULL)
			t = netdev_priv(dev);
		memcpy(&p, &t->parms, sizeof(p));
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
			err = -EFAULT;
		break;

	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			goto done;

		err = -EFAULT;
		if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
			goto done;

		err = -EINVAL;
		if (p.iph.version != 4 || p.iph.protocol != IPPROTO_GRE ||
		    p.iph.ihl != 5 || (p.iph.frag_off&htons(~IP_DF)) ||
		    ((p.i_flags|p.o_flags)&(GRE_VERSION|GRE_ROUTING)))
			goto done;
		if (p.iph.ttl)
			p.iph.frag_off |= htons(IP_DF);

		if (!(p.i_flags&GRE_KEY))
			p.i_key = 0;
		if (!(p.o_flags&GRE_KEY))
			p.o_key = 0;

		t = ipgre_tunnel_locate(net, &p, cmd == SIOCADDTUNNEL);

		if (dev != ign->fb_tunnel_dev && cmd == SIOCCHGTUNNEL) {
			if (t != NULL) {
				if (t->dev != dev) {
					err = -EEXIST;
					break;
				}
			} else {
				unsigned int nflags = 0;

				t = netdev_priv(dev);

				if (ipv4_is_multicast(p.iph.daddr))
					nflags = IFF_BROADCAST;
				else if (p.iph.daddr)
					nflags = IFF_POINTOPOINT;

				if ((dev->flags^nflags)&(IFF_POINTOPOINT|IFF_BROADCAST)) {
					err = -EINVAL;
					break;
				}
				ipgre_tunnel_unlink(ign, t);
				synchronize_net();
				t->parms.iph.saddr = p.iph.saddr;
				t->parms.iph.daddr = p.iph.daddr;
				t->parms.i_key = p.i_key;
				t->parms.o_key = p.o_key;
				memcpy(dev->dev_addr, &p.iph.saddr, 4);
				memcpy(dev->broadcast, &p.iph.daddr, 4);
				ipgre_tunnel_link(ign, t);
				netdev_state_change(dev);
			}
		}

		if (t) {
			err = 0;
			if (cmd == SIOCCHGTUNNEL) {
				t->parms.iph.ttl = p.iph.ttl;
				t->parms.iph.tos = p.iph.tos;
				t->parms.iph.frag_off = p.iph.frag_off;
				if (t->parms.link != p.link) {
					t->parms.link = p.link;
					dev->mtu = ipgre_tunnel_bind_dev(dev);
					netdev_state_change(dev);
				}
			}
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &t->parms, sizeof(p)))
				err = -EFAULT;
		} else
			err = (cmd == SIOCADDTUNNEL ? -ENOBUFS : -ENOENT);
		break;

	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			goto done;

		if (dev == ign->fb_tunnel_dev) {
			err = -EFAULT;
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
				goto done;
			err = -ENOENT;
			if ((t = ipgre_tunnel_locate(net, &p, 0)) == NULL)
				goto done;
			err = -EPERM;
			if (t == netdev_priv(ign->fb_tunnel_dev))
				goto done;
			dev = t->dev;
		}
		unregister_netdevice(dev);
		err = 0;
		break;

	default:
		err = -EINVAL;
	}

done:
	return err;
}

static int ipgre_tunnel_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	if (new_mtu < 68 ||
	    new_mtu > 0xFFF8 - dev->hard_header_len - tunnel->hlen)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

/* Nice toy. Unfortunately, useless in real life :-)
   It allows to construct virtual multiprotocol broadcast "LAN"
   over the Internet, provided multicast routing is tuned.


   I have no idea was this bicycle invented before me,
   so that I had to set ARPHRD_IPGRE to a random value.
   I have an impression, that Cisco could make something similar,
   but this feature is apparently missing in IOS<=11.2(8).

   I set up 10.66.66/24 and fec0:6666:6666::0/96 as virtual networks
   with broadcast 224.66.66.66. If you have access to mbone, play with me :-)

   ping -t 255 224.66.66.66

   If nobody answers, mbone does not work.

   ip tunnel add Universe mode gre remote 224.66.66.66 local <Your_real_addr> ttl 255
   ip addr add 10.66.66.<somewhat>/24 dev Universe
   ifconfig Universe up
   ifconfig Universe add fe80::<Your_real_addr>/10
   ifconfig Universe add fec0:6666:6666::<Your_real_addr>/96
   ftp 10.66.66.66
   ...
   ftp fec0:6666:6666::193.233.7.65
   ...

 */

static int ipgre_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type,
			const void *daddr, const void *saddr, unsigned int len)
{
	struct ip_tunnel *t = netdev_priv(dev);
	struct iphdr *iph = (struct iphdr *)skb_push(skb, t->hlen);
	__be16 *p = (__be16 *)(iph+1);

	memcpy(iph, &t->parms.iph, sizeof(struct iphdr));
	p[0]		= t->parms.o_flags;
	p[1]		= htons(type);

	/*
	 *	Set the source hardware address.
	 */

	if (saddr)
		memcpy(&iph->saddr, saddr, 4);
	if (daddr)
		memcpy(&iph->daddr, daddr, 4);
	if (iph->daddr)
		return t->hlen;

	return -t->hlen;
}

static int ipgre_header_parse(const struct sk_buff *skb, unsigned char *haddr)
{
	const struct iphdr *iph = (const struct iphdr *) skb_mac_header(skb);
	memcpy(haddr, &iph->saddr, 4);
	return 4;
}

static const struct header_ops ipgre_header_ops = {
	.create	= ipgre_header,
	.parse	= ipgre_header_parse,
};

#ifdef CONFIG_NET_IPGRE_BROADCAST
static int ipgre_open(struct net_device *dev)
{
	struct ip_tunnel *t = netdev_priv(dev);

	if (ipv4_is_multicast(t->parms.iph.daddr)) {
		struct flowi4 fl4;
		struct rtable *rt;

		rt = ip_route_output_gre(dev_net(dev), &fl4,
					 t->parms.iph.daddr,
					 t->parms.iph.saddr,
					 t->parms.o_key,
					 RT_TOS(t->parms.iph.tos),
					 t->parms.link);
		if (IS_ERR(rt))
			return -EADDRNOTAVAIL;
		dev = rt->dst.dev;
		ip_rt_put(rt);
		if (__in_dev_get_rtnl(dev) == NULL)
			return -EADDRNOTAVAIL;
		t->mlink = dev->ifindex;
		ip_mc_inc_group(__in_dev_get_rtnl(dev), t->parms.iph.daddr);
	}
	return 0;
}

static int ipgre_close(struct net_device *dev)
{
	struct ip_tunnel *t = netdev_priv(dev);

	if (ipv4_is_multicast(t->parms.iph.daddr) && t->mlink) {
		struct in_device *in_dev;
		in_dev = inetdev_by_index(dev_net(dev), t->mlink);
		if (in_dev)
			ip_mc_dec_group(in_dev, t->parms.iph.daddr);
	}
	return 0;
}

#endif

static const struct net_device_ops ipgre_netdev_ops = {
	.ndo_init		= ipgre_tunnel_init,
	.ndo_uninit		= ipgre_tunnel_uninit,
#ifdef CONFIG_NET_IPGRE_BROADCAST
	.ndo_open		= ipgre_open,
	.ndo_stop		= ipgre_close,
#endif
	.ndo_start_xmit		= ipgre_tunnel_xmit,
	.ndo_do_ioctl		= ipgre_tunnel_ioctl,
	.ndo_change_mtu		= ipgre_tunnel_change_mtu,
	.ndo_get_stats64	= ipgre_get_stats64,
};

static void ipgre_dev_free(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	gro_cells_destroy(&tunnel->gro_cells);
	free_percpu(dev->tstats);
	free_netdev(dev);
}

#define GRE_FEATURES (NETIF_F_SG |		\
		      NETIF_F_FRAGLIST |	\
		      NETIF_F_HIGHDMA |		\
		      NETIF_F_HW_CSUM)

static void ipgre_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &ipgre_netdev_ops;
	dev->destructor 	= ipgre_dev_free;

	dev->type		= ARPHRD_IPGRE;
	dev->needed_headroom 	= LL_MAX_HEADER + sizeof(struct iphdr) + 4;
	dev->mtu		= ETH_DATA_LEN - sizeof(struct iphdr) - 4;
	dev->flags		= IFF_NOARP;
	dev->iflink		= 0;
	dev->addr_len		= 4;
	dev->features		|= NETIF_F_NETNS_LOCAL;
	dev->priv_flags		&= ~IFF_XMIT_DST_RELEASE;

	dev->features		|= GRE_FEATURES;
	dev->hw_features	|= GRE_FEATURES;
}

static int ipgre_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel;
	struct iphdr *iph;
	int err;

	tunnel = netdev_priv(dev);
	iph = &tunnel->parms.iph;

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	memcpy(dev->dev_addr, &tunnel->parms.iph.saddr, 4);
	memcpy(dev->broadcast, &tunnel->parms.iph.daddr, 4);

	if (iph->daddr) {
#ifdef CONFIG_NET_IPGRE_BROADCAST
		if (ipv4_is_multicast(iph->daddr)) {
			if (!iph->saddr)
				return -EINVAL;
			dev->flags = IFF_BROADCAST;
			dev->header_ops = &ipgre_header_ops;
		}
#endif
	} else
		dev->header_ops = &ipgre_header_ops;

	dev->tstats = alloc_percpu(struct pcpu_tstats);
	if (!dev->tstats)
		return -ENOMEM;

	err = gro_cells_init(&tunnel->gro_cells, dev);
	if (err) {
		free_percpu(dev->tstats);
		return err;
	}

	return 0;
}

static void ipgre_fb_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	iph->version		= 4;
	iph->protocol		= IPPROTO_GRE;
	iph->ihl		= 5;
	tunnel->hlen		= sizeof(struct iphdr) + 4;

	dev_hold(dev);
}


static const struct gre_protocol ipgre_protocol = {
	.handler     = ipgre_rcv,
	.err_handler = ipgre_err,
};

static void ipgre_destroy_tunnels(struct ipgre_net *ign, struct list_head *head)
{
	int prio;

	for (prio = 0; prio < 4; prio++) {
		int h;
		for (h = 0; h < HASH_SIZE; h++) {
			struct ip_tunnel *t;

			t = rtnl_dereference(ign->tunnels[prio][h]);

			while (t != NULL) {
				unregister_netdevice_queue(t->dev, head);
				t = rtnl_dereference(t->next);
			}
		}
	}
}

static int __net_init ipgre_init_net(struct net *net)
{
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);
	int err;

	ign->fb_tunnel_dev = alloc_netdev(sizeof(struct ip_tunnel), "gre0",
					   ipgre_tunnel_setup);
	if (!ign->fb_tunnel_dev) {
		err = -ENOMEM;
		goto err_alloc_dev;
	}
	dev_net_set(ign->fb_tunnel_dev, net);

	ipgre_fb_tunnel_init(ign->fb_tunnel_dev);
	ign->fb_tunnel_dev->rtnl_link_ops = &ipgre_link_ops;

	if ((err = register_netdev(ign->fb_tunnel_dev)))
		goto err_reg_dev;

	rcu_assign_pointer(ign->tunnels_wc[0],
			   netdev_priv(ign->fb_tunnel_dev));
	return 0;

err_reg_dev:
	ipgre_dev_free(ign->fb_tunnel_dev);
err_alloc_dev:
	return err;
}

static void __net_exit ipgre_exit_net(struct net *net)
{
	struct ipgre_net *ign;
	LIST_HEAD(list);

	ign = net_generic(net, ipgre_net_id);
	rtnl_lock();
	ipgre_destroy_tunnels(ign, &list);
	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations ipgre_net_ops = {
	.init = ipgre_init_net,
	.exit = ipgre_exit_net,
	.id   = &ipgre_net_id,
	.size = sizeof(struct ipgre_net),
};

static int ipgre_tunnel_validate(struct nlattr *tb[], struct nlattr *data[])
{
	__be16 flags;

	if (!data)
		return 0;

	flags = 0;
	if (data[IFLA_GRE_IFLAGS])
		flags |= nla_get_be16(data[IFLA_GRE_IFLAGS]);
	if (data[IFLA_GRE_OFLAGS])
		flags |= nla_get_be16(data[IFLA_GRE_OFLAGS]);
	if (flags & (GRE_VERSION|GRE_ROUTING))
		return -EINVAL;

	return 0;
}

static int ipgre_tap_validate(struct nlattr *tb[], struct nlattr *data[])
{
	__be32 daddr;

	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (!data)
		goto out;

	if (data[IFLA_GRE_REMOTE]) {
		memcpy(&daddr, nla_data(data[IFLA_GRE_REMOTE]), 4);
		if (!daddr)
			return -EINVAL;
	}

out:
	return ipgre_tunnel_validate(tb, data);
}

static void ipgre_netlink_parms(struct nlattr *data[],
				struct ip_tunnel_parm *parms)
{
	memset(parms, 0, sizeof(*parms));

	parms->iph.protocol = IPPROTO_GRE;

	if (!data)
		return;

	if (data[IFLA_GRE_LINK])
		parms->link = nla_get_u32(data[IFLA_GRE_LINK]);

	if (data[IFLA_GRE_IFLAGS])
		parms->i_flags = nla_get_be16(data[IFLA_GRE_IFLAGS]);

	if (data[IFLA_GRE_OFLAGS])
		parms->o_flags = nla_get_be16(data[IFLA_GRE_OFLAGS]);

	if (data[IFLA_GRE_IKEY])
		parms->i_key = nla_get_be32(data[IFLA_GRE_IKEY]);

	if (data[IFLA_GRE_OKEY])
		parms->o_key = nla_get_be32(data[IFLA_GRE_OKEY]);

	if (data[IFLA_GRE_LOCAL])
		parms->iph.saddr = nla_get_be32(data[IFLA_GRE_LOCAL]);

	if (data[IFLA_GRE_REMOTE])
		parms->iph.daddr = nla_get_be32(data[IFLA_GRE_REMOTE]);

	if (data[IFLA_GRE_TTL])
		parms->iph.ttl = nla_get_u8(data[IFLA_GRE_TTL]);

	if (data[IFLA_GRE_TOS])
		parms->iph.tos = nla_get_u8(data[IFLA_GRE_TOS]);

	if (!data[IFLA_GRE_PMTUDISC] || nla_get_u8(data[IFLA_GRE_PMTUDISC]))
		parms->iph.frag_off = htons(IP_DF);
}

static int ipgre_tap_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel;

	tunnel = netdev_priv(dev);

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);

	ipgre_tunnel_bind_dev(dev);

	dev->tstats = alloc_percpu(struct pcpu_tstats);
	if (!dev->tstats)
		return -ENOMEM;

	return 0;
}

static const struct net_device_ops ipgre_tap_netdev_ops = {
	.ndo_init		= ipgre_tap_init,
	.ndo_uninit		= ipgre_tunnel_uninit,
	.ndo_start_xmit		= ipgre_tunnel_xmit,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= ipgre_tunnel_change_mtu,
	.ndo_get_stats64	= ipgre_get_stats64,
};

static void ipgre_tap_setup(struct net_device *dev)
{

	ether_setup(dev);

	dev->netdev_ops		= &ipgre_tap_netdev_ops;
	dev->destructor 	= ipgre_dev_free;

	dev->iflink		= 0;
	dev->features		|= NETIF_F_NETNS_LOCAL;

	dev->features		|= GRE_FEATURES;
	dev->hw_features	|= GRE_FEATURES;
}

static int ipgre_newlink(struct net *src_net, struct net_device *dev, struct nlattr *tb[],
			 struct nlattr *data[])
{
	struct ip_tunnel *nt;
	struct net *net = dev_net(dev);
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);
	int mtu;
	int err;

	nt = netdev_priv(dev);
	ipgre_netlink_parms(data, &nt->parms);

	if (ipgre_tunnel_find(net, &nt->parms, dev->type))
		return -EEXIST;

	if (dev->type == ARPHRD_ETHER && !tb[IFLA_ADDRESS])
		eth_hw_addr_random(dev);

	mtu = ipgre_tunnel_bind_dev(dev);
	if (!tb[IFLA_MTU])
		dev->mtu = mtu;

	/* Can use a lockless transmit, unless we generate output sequences */
	if (!(nt->parms.o_flags & GRE_SEQ))
		dev->features |= NETIF_F_LLTX;

	err = register_netdevice(dev);
	if (err)
		goto out;

	dev_hold(dev);
	ipgre_tunnel_link(ign, nt);

out:
	return err;
}

static int ipgre_changelink(struct net_device *dev, struct nlattr *tb[],
			    struct nlattr *data[])
{
	struct ip_tunnel *t, *nt;
	struct net *net = dev_net(dev);
	struct ipgre_net *ign = net_generic(net, ipgre_net_id);
	struct ip_tunnel_parm p;
	int mtu;

	if (dev == ign->fb_tunnel_dev)
		return -EINVAL;

	nt = netdev_priv(dev);
	ipgre_netlink_parms(data, &p);

	t = ipgre_tunnel_locate(net, &p, 0);

	if (t) {
		if (t->dev != dev)
			return -EEXIST;
	} else {
		t = nt;

		if (dev->type != ARPHRD_ETHER) {
			unsigned int nflags = 0;

			if (ipv4_is_multicast(p.iph.daddr))
				nflags = IFF_BROADCAST;
			else if (p.iph.daddr)
				nflags = IFF_POINTOPOINT;

			if ((dev->flags ^ nflags) &
			    (IFF_POINTOPOINT | IFF_BROADCAST))
				return -EINVAL;
		}

		ipgre_tunnel_unlink(ign, t);
		t->parms.iph.saddr = p.iph.saddr;
		t->parms.iph.daddr = p.iph.daddr;
		t->parms.i_key = p.i_key;
		if (dev->type != ARPHRD_ETHER) {
			memcpy(dev->dev_addr, &p.iph.saddr, 4);
			memcpy(dev->broadcast, &p.iph.daddr, 4);
		}
		ipgre_tunnel_link(ign, t);
		netdev_state_change(dev);
	}

	t->parms.o_key = p.o_key;
	t->parms.iph.ttl = p.iph.ttl;
	t->parms.iph.tos = p.iph.tos;
	t->parms.iph.frag_off = p.iph.frag_off;

	if (t->parms.link != p.link) {
		t->parms.link = p.link;
		mtu = ipgre_tunnel_bind_dev(dev);
		if (!tb[IFLA_MTU])
			dev->mtu = mtu;
		netdev_state_change(dev);
	}

	return 0;
}

static size_t ipgre_get_size(const struct net_device *dev)
{
	return
		/* IFLA_GRE_LINK */
		nla_total_size(4) +
		/* IFLA_GRE_IFLAGS */
		nla_total_size(2) +
		/* IFLA_GRE_OFLAGS */
		nla_total_size(2) +
		/* IFLA_GRE_IKEY */
		nla_total_size(4) +
		/* IFLA_GRE_OKEY */
		nla_total_size(4) +
		/* IFLA_GRE_LOCAL */
		nla_total_size(4) +
		/* IFLA_GRE_REMOTE */
		nla_total_size(4) +
		/* IFLA_GRE_TTL */
		nla_total_size(1) +
		/* IFLA_GRE_TOS */
		nla_total_size(1) +
		/* IFLA_GRE_PMTUDISC */
		nla_total_size(1) +
		0;
}

static int ipgre_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip_tunnel *t = netdev_priv(dev);
	struct ip_tunnel_parm *p = &t->parms;

	if (nla_put_u32(skb, IFLA_GRE_LINK, p->link) ||
	    nla_put_be16(skb, IFLA_GRE_IFLAGS, p->i_flags) ||
	    nla_put_be16(skb, IFLA_GRE_OFLAGS, p->o_flags) ||
	    nla_put_be32(skb, IFLA_GRE_IKEY, p->i_key) ||
	    nla_put_be32(skb, IFLA_GRE_OKEY, p->o_key) ||
	    nla_put_be32(skb, IFLA_GRE_LOCAL, p->iph.saddr) ||
	    nla_put_be32(skb, IFLA_GRE_REMOTE, p->iph.daddr) ||
	    nla_put_u8(skb, IFLA_GRE_TTL, p->iph.ttl) ||
	    nla_put_u8(skb, IFLA_GRE_TOS, p->iph.tos) ||
	    nla_put_u8(skb, IFLA_GRE_PMTUDISC,
		       !!(p->iph.frag_off & htons(IP_DF))))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy ipgre_policy[IFLA_GRE_MAX + 1] = {
	[IFLA_GRE_LINK]		= { .type = NLA_U32 },
	[IFLA_GRE_IFLAGS]	= { .type = NLA_U16 },
	[IFLA_GRE_OFLAGS]	= { .type = NLA_U16 },
	[IFLA_GRE_IKEY]		= { .type = NLA_U32 },
	[IFLA_GRE_OKEY]		= { .type = NLA_U32 },
	[IFLA_GRE_LOCAL]	= { .len = FIELD_SIZEOF(struct iphdr, saddr) },
	[IFLA_GRE_REMOTE]	= { .len = FIELD_SIZEOF(struct iphdr, daddr) },
	[IFLA_GRE_TTL]		= { .type = NLA_U8 },
	[IFLA_GRE_TOS]		= { .type = NLA_U8 },
	[IFLA_GRE_PMTUDISC]	= { .type = NLA_U8 },
};

static struct rtnl_link_ops ipgre_link_ops __read_mostly = {
	.kind		= "gre",
	.maxtype	= IFLA_GRE_MAX,
	.policy		= ipgre_policy,
	.priv_size	= sizeof(struct ip_tunnel),
	.setup		= ipgre_tunnel_setup,
	.validate	= ipgre_tunnel_validate,
	.newlink	= ipgre_newlink,
	.changelink	= ipgre_changelink,
	.get_size	= ipgre_get_size,
	.fill_info	= ipgre_fill_info,
};

static struct rtnl_link_ops ipgre_tap_ops __read_mostly = {
	.kind		= "gretap",
	.maxtype	= IFLA_GRE_MAX,
	.policy		= ipgre_policy,
	.priv_size	= sizeof(struct ip_tunnel),
	.setup		= ipgre_tap_setup,
	.validate	= ipgre_tap_validate,
	.newlink	= ipgre_newlink,
	.changelink	= ipgre_changelink,
	.get_size	= ipgre_get_size,
	.fill_info	= ipgre_fill_info,
};

/*
 *	And now the modules code and kernel interface.
 */

static int __init ipgre_init(void)
{
	int err;

	pr_info("GRE over IPv4 tunneling driver\n");

	err = register_pernet_device(&ipgre_net_ops);
	if (err < 0)
		return err;

	err = gre_add_protocol(&ipgre_protocol, GREPROTO_CISCO);
	if (err < 0) {
		pr_info("%s: can't add protocol\n", __func__);
		goto add_proto_failed;
	}

	err = rtnl_link_register(&ipgre_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	err = rtnl_link_register(&ipgre_tap_ops);
	if (err < 0)
		goto tap_ops_failed;

out:
	return err;

tap_ops_failed:
	rtnl_link_unregister(&ipgre_link_ops);
rtnl_link_failed:
	gre_del_protocol(&ipgre_protocol, GREPROTO_CISCO);
add_proto_failed:
	unregister_pernet_device(&ipgre_net_ops);
	goto out;
}

static void __exit ipgre_fini(void)
{
	rtnl_link_unregister(&ipgre_tap_ops);
	rtnl_link_unregister(&ipgre_link_ops);
	if (gre_del_protocol(&ipgre_protocol, GREPROTO_CISCO) < 0)
		pr_info("%s: can't remove protocol\n", __func__);
	unregister_pernet_device(&ipgre_net_ops);
}

module_init(ipgre_init);
module_exit(ipgre_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("gre");
MODULE_ALIAS_RTNL_LINK("gretap");
MODULE_ALIAS_NETDEV("gre0");
