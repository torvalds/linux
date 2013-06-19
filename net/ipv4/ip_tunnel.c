/*
 * Copyright (c) 2013 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
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
#include <linux/if_vlan.h>
#include <linux/rculist.h>

#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/ip_tunnels.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#endif

static unsigned int ip_tunnel_hash(struct ip_tunnel_net *itn,
				   __be32 key, __be32 remote)
{
	return hash_32((__force u32)key ^ (__force u32)remote,
			 IP_TNL_HASH_BITS);
}

/* Often modified stats are per cpu, other are shared (netdev->stats) */
struct rtnl_link_stats64 *ip_tunnel_get_stats64(struct net_device *dev,
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

	tot->collisions  = dev->stats.collisions;

	return tot;
}
EXPORT_SYMBOL_GPL(ip_tunnel_get_stats64);

static bool ip_tunnel_key_match(const struct ip_tunnel_parm *p,
				__be16 flags, __be32 key)
{
	if (p->i_flags & TUNNEL_KEY) {
		if (flags & TUNNEL_KEY)
			return key == p->i_key;
		else
			/* key expected, none present */
			return false;
	} else
		return !(flags & TUNNEL_KEY);
}

/* Fallback tunnel: no source, no destination, no key, no options

   Tunnel hash table:
   We require exact key match i.e. if a key is present in packet
   it will match only tunnel with the same key; if it is not present,
   it will match only keyless tunnel.

   All keysless packets, if not matched configured keyless tunnels
   will match fallback tunnel.
   Given src, dst and key, find appropriate for input tunnel.
*/
struct ip_tunnel *ip_tunnel_lookup(struct ip_tunnel_net *itn,
				   int link, __be16 flags,
				   __be32 remote, __be32 local,
				   __be32 key)
{
	unsigned int hash;
	struct ip_tunnel *t, *cand = NULL;
	struct hlist_head *head;

	hash = ip_tunnel_hash(itn, key, remote);
	head = &itn->tunnels[hash];

	hlist_for_each_entry_rcu(t, head, hash_node) {
		if (local != t->parms.iph.saddr ||
		    remote != t->parms.iph.daddr ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (!ip_tunnel_key_match(&t->parms, flags, key))
			continue;

		if (t->parms.link == link)
			return t;
		else
			cand = t;
	}

	hlist_for_each_entry_rcu(t, head, hash_node) {
		if (remote != t->parms.iph.daddr ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (!ip_tunnel_key_match(&t->parms, flags, key))
			continue;

		if (t->parms.link == link)
			return t;
		else if (!cand)
			cand = t;
	}

	hash = ip_tunnel_hash(itn, key, 0);
	head = &itn->tunnels[hash];

	hlist_for_each_entry_rcu(t, head, hash_node) {
		if ((local != t->parms.iph.saddr &&
		     (local != t->parms.iph.daddr ||
		      !ipv4_is_multicast(local))) ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (!ip_tunnel_key_match(&t->parms, flags, key))
			continue;

		if (t->parms.link == link)
			return t;
		else if (!cand)
			cand = t;
	}

	if (flags & TUNNEL_NO_KEY)
		goto skip_key_lookup;

	hlist_for_each_entry_rcu(t, head, hash_node) {
		if (t->parms.i_key != key ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (t->parms.link == link)
			return t;
		else if (!cand)
			cand = t;
	}

skip_key_lookup:
	if (cand)
		return cand;

	if (itn->fb_tunnel_dev && itn->fb_tunnel_dev->flags & IFF_UP)
		return netdev_priv(itn->fb_tunnel_dev);


	return NULL;
}
EXPORT_SYMBOL_GPL(ip_tunnel_lookup);

static struct hlist_head *ip_bucket(struct ip_tunnel_net *itn,
				    struct ip_tunnel_parm *parms)
{
	unsigned int h;
	__be32 remote;

	if (parms->iph.daddr && !ipv4_is_multicast(parms->iph.daddr))
		remote = parms->iph.daddr;
	else
		remote = 0;

	h = ip_tunnel_hash(itn, parms->i_key, remote);
	return &itn->tunnels[h];
}

static void ip_tunnel_add(struct ip_tunnel_net *itn, struct ip_tunnel *t)
{
	struct hlist_head *head = ip_bucket(itn, &t->parms);

	hlist_add_head_rcu(&t->hash_node, head);
}

static void ip_tunnel_del(struct ip_tunnel *t)
{
	hlist_del_init_rcu(&t->hash_node);
}

static struct ip_tunnel *ip_tunnel_find(struct ip_tunnel_net *itn,
					struct ip_tunnel_parm *parms,
					int type)
{
	__be32 remote = parms->iph.daddr;
	__be32 local = parms->iph.saddr;
	__be32 key = parms->i_key;
	int link = parms->link;
	struct ip_tunnel *t = NULL;
	struct hlist_head *head = ip_bucket(itn, parms);

	hlist_for_each_entry_rcu(t, head, hash_node) {
		if (local == t->parms.iph.saddr &&
		    remote == t->parms.iph.daddr &&
		    key == t->parms.i_key &&
		    link == t->parms.link &&
		    type == t->dev->type)
			break;
	}
	return t;
}

static struct net_device *__ip_tunnel_create(struct net *net,
					     const struct rtnl_link_ops *ops,
					     struct ip_tunnel_parm *parms)
{
	int err;
	struct ip_tunnel *tunnel;
	struct net_device *dev;
	char name[IFNAMSIZ];

	if (parms->name[0])
		strlcpy(name, parms->name, IFNAMSIZ);
	else {
		if (strlen(ops->kind) > (IFNAMSIZ - 3)) {
			err = -E2BIG;
			goto failed;
		}
		strlcpy(name, ops->kind, IFNAMSIZ);
		strncat(name, "%d", 2);
	}

	ASSERT_RTNL();
	dev = alloc_netdev(ops->priv_size, name, ops->setup);
	if (!dev) {
		err = -ENOMEM;
		goto failed;
	}
	dev_net_set(dev, net);

	dev->rtnl_link_ops = ops;

	tunnel = netdev_priv(dev);
	tunnel->parms = *parms;

	err = register_netdevice(dev);
	if (err)
		goto failed_free;

	return dev;

failed_free:
	free_netdev(dev);
failed:
	return ERR_PTR(err);
}

static inline struct rtable *ip_route_output_tunnel(struct net *net,
						    struct flowi4 *fl4,
						    int proto,
						    __be32 daddr, __be32 saddr,
						    __be32 key, __u8 tos, int oif)
{
	memset(fl4, 0, sizeof(*fl4));
	fl4->flowi4_oif = oif;
	fl4->daddr = daddr;
	fl4->saddr = saddr;
	fl4->flowi4_tos = tos;
	fl4->flowi4_proto = proto;
	fl4->fl4_gre_key = key;
	return ip_route_output_key(net, fl4);
}

static int ip_tunnel_bind_dev(struct net_device *dev)
{
	struct net_device *tdev = NULL;
	struct ip_tunnel *tunnel = netdev_priv(dev);
	const struct iphdr *iph;
	int hlen = LL_MAX_HEADER;
	int mtu = ETH_DATA_LEN;
	int t_hlen = tunnel->hlen + sizeof(struct iphdr);

	iph = &tunnel->parms.iph;

	/* Guess output device to choose reasonable mtu and needed_headroom */
	if (iph->daddr) {
		struct flowi4 fl4;
		struct rtable *rt;

		rt = ip_route_output_tunnel(dev_net(dev), &fl4,
					    tunnel->parms.iph.protocol,
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

	dev->needed_headroom = t_hlen + hlen;
	mtu -= (dev->hard_header_len + t_hlen);

	if (mtu < 68)
		mtu = 68;

	return mtu;
}

static struct ip_tunnel *ip_tunnel_create(struct net *net,
					  struct ip_tunnel_net *itn,
					  struct ip_tunnel_parm *parms)
{
	struct ip_tunnel *nt, *fbt;
	struct net_device *dev;

	BUG_ON(!itn->fb_tunnel_dev);
	fbt = netdev_priv(itn->fb_tunnel_dev);
	dev = __ip_tunnel_create(net, itn->fb_tunnel_dev->rtnl_link_ops, parms);
	if (IS_ERR(dev))
		return NULL;

	dev->mtu = ip_tunnel_bind_dev(dev);

	nt = netdev_priv(dev);
	ip_tunnel_add(itn, nt);
	return nt;
}

int ip_tunnel_rcv(struct ip_tunnel *tunnel, struct sk_buff *skb,
		  const struct tnl_ptk_info *tpi, bool log_ecn_error)
{
	struct pcpu_tstats *tstats;
	const struct iphdr *iph = ip_hdr(skb);
	int err;

	secpath_reset(skb);

	skb->protocol = tpi->proto;

	skb->mac_header = skb->network_header;
	__pskb_pull(skb, tunnel->hlen);
	skb_postpull_rcsum(skb, skb_transport_header(skb), tunnel->hlen);
#ifdef CONFIG_NET_IPGRE_BROADCAST
	if (ipv4_is_multicast(iph->daddr)) {
		/* Looped back packet, drop it! */
		if (rt_is_output_route(skb_rtable(skb)))
			goto drop;
		tunnel->dev->stats.multicast++;
		skb->pkt_type = PACKET_BROADCAST;
	}
#endif

	if ((!(tpi->flags&TUNNEL_CSUM) &&  (tunnel->parms.i_flags&TUNNEL_CSUM)) ||
	     ((tpi->flags&TUNNEL_CSUM) && !(tunnel->parms.i_flags&TUNNEL_CSUM))) {
		tunnel->dev->stats.rx_crc_errors++;
		tunnel->dev->stats.rx_errors++;
		goto drop;
	}

	if (tunnel->parms.i_flags&TUNNEL_SEQ) {
		if (!(tpi->flags&TUNNEL_SEQ) ||
		    (tunnel->i_seqno && (s32)(ntohl(tpi->seq) - tunnel->i_seqno) < 0)) {
			tunnel->dev->stats.rx_fifo_errors++;
			tunnel->dev->stats.rx_errors++;
			goto drop;
		}
		tunnel->i_seqno = ntohl(tpi->seq) + 1;
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

	skb->pkt_type = PACKET_HOST;
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

drop:
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL_GPL(ip_tunnel_rcv);

void ip_tunnel_xmit(struct sk_buff *skb, struct net_device *dev,
		    const struct iphdr *tnl_params, const u8 protocol)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	const struct iphdr *inner_iph;
	struct iphdr *iph;
	struct flowi4 fl4;
	u8     tos, ttl;
	__be16 df;
	struct rtable *rt;		/* Route to the other host */
	struct net_device *tdev;	/* Device to other host */
	unsigned int max_headroom;	/* The extra header space needed */
	__be32 dst;
	int mtu;

	inner_iph = (const struct iphdr *)skb_inner_network_header(skb);

	memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
	dst = tnl_params->daddr;
	if (dst == 0) {
		/* NBMA tunnel */

		if (skb_dst(skb) == NULL) {
			dev->stats.tx_fifo_errors++;
			goto tx_error;
		}

		if (skb->protocol == htons(ETH_P_IP)) {
			rt = skb_rtable(skb);
			dst = rt_nexthop(rt, inner_iph->daddr);
		}
#if IS_ENABLED(CONFIG_IPV6)
		else if (skb->protocol == htons(ETH_P_IPV6)) {
			const struct in6_addr *addr6;
			struct neighbour *neigh;
			bool do_tx_error_icmp;
			int addr_type;

			neigh = dst_neigh_lookup(skb_dst(skb),
						 &ipv6_hdr(skb)->daddr);
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

	tos = tnl_params->tos;
	if (tos & 0x1) {
		tos &= ~0x1;
		if (skb->protocol == htons(ETH_P_IP))
			tos = inner_iph->tos;
		else if (skb->protocol == htons(ETH_P_IPV6))
			tos = ipv6_get_dsfield((const struct ipv6hdr *)inner_iph);
	}

	rt = ip_route_output_tunnel(dev_net(dev), &fl4,
				    tunnel->parms.iph.protocol,
				    dst, tnl_params->saddr,
				    tunnel->parms.o_key,
				    RT_TOS(tos),
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

	df = tnl_params->frag_off;

	if (df)
		mtu = dst_mtu(&rt->dst) - dev->hard_header_len
					- sizeof(struct iphdr);
	else
		mtu = skb_dst(skb) ? dst_mtu(skb_dst(skb)) : dev->mtu;

	if (skb_dst(skb))
		skb_dst(skb)->ops->update_pmtu(skb_dst(skb), NULL, skb, mtu);

	if (skb->protocol == htons(ETH_P_IP)) {
		df |= (inner_iph->frag_off&htons(IP_DF));

		if (!skb_is_gso(skb) &&
		    (inner_iph->frag_off&htons(IP_DF)) &&
		     mtu < ntohs(inner_iph->tot_len)) {
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
			ip_rt_put(rt);
			goto tx_error;
		}
	}
#if IS_ENABLED(CONFIG_IPV6)
	else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct rt6_info *rt6 = (struct rt6_info *)skb_dst(skb);

		if (rt6 && mtu < dst_mtu(skb_dst(skb)) &&
		    mtu >= IPV6_MIN_MTU) {
			if ((tunnel->parms.iph.daddr &&
			    !ipv4_is_multicast(tunnel->parms.iph.daddr)) ||
			    rt6->rt6i_dst.plen == 128) {
				rt6->rt6i_flags |= RTF_MODIFIED;
				dst_metric_set(skb_dst(skb), RTAX_MTU, mtu);
			}
		}

		if (!skb_is_gso(skb) && mtu >= IPV6_MIN_MTU &&
		    mtu < skb->len) {
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

	ttl = tnl_params->ttl;
	if (ttl == 0) {
		if (skb->protocol == htons(ETH_P_IP))
			ttl = inner_iph->ttl;
#if IS_ENABLED(CONFIG_IPV6)
		else if (skb->protocol == htons(ETH_P_IPV6))
			ttl = ((const struct ipv6hdr *)inner_iph)->hop_limit;
#endif
		else
			ttl = ip4_dst_hoplimit(&rt->dst);
	}

	max_headroom = LL_RESERVED_SPACE(tdev) + sizeof(struct iphdr)
					       + rt->dst.header_len;
	if (max_headroom > dev->needed_headroom) {
		dev->needed_headroom = max_headroom;
		if (skb_cow_head(skb, dev->needed_headroom)) {
			dev->stats.tx_dropped++;
			dev_kfree_skb(skb);
			return;
		}
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	/* Push down and install the IP header. */
	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);

	iph = ip_hdr(skb);
	inner_iph = (const struct iphdr *)skb_inner_network_header(skb);

	iph->version	=	4;
	iph->ihl	=	sizeof(struct iphdr) >> 2;
	iph->frag_off	=	df;
	iph->protocol	=	protocol;
	iph->tos	=	ip_tunnel_ecn_encap(tos, inner_iph, skb);
	iph->daddr	=	fl4.daddr;
	iph->saddr	=	fl4.saddr;
	iph->ttl	=	ttl;
	tunnel_ip_select_ident(skb, inner_iph, &rt->dst);

	iptunnel_xmit(skb, dev);
	return;

#if IS_ENABLED(CONFIG_IPV6)
tx_error_icmp:
	dst_link_failure(skb);
#endif
tx_error:
	dev->stats.tx_errors++;
	dev_kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(ip_tunnel_xmit);

static void ip_tunnel_update(struct ip_tunnel_net *itn,
			     struct ip_tunnel *t,
			     struct net_device *dev,
			     struct ip_tunnel_parm *p,
			     bool set_mtu)
{
	ip_tunnel_del(t);
	t->parms.iph.saddr = p->iph.saddr;
	t->parms.iph.daddr = p->iph.daddr;
	t->parms.i_key = p->i_key;
	t->parms.o_key = p->o_key;
	if (dev->type != ARPHRD_ETHER) {
		memcpy(dev->dev_addr, &p->iph.saddr, 4);
		memcpy(dev->broadcast, &p->iph.daddr, 4);
	}
	ip_tunnel_add(itn, t);

	t->parms.iph.ttl = p->iph.ttl;
	t->parms.iph.tos = p->iph.tos;
	t->parms.iph.frag_off = p->iph.frag_off;

	if (t->parms.link != p->link) {
		int mtu;

		t->parms.link = p->link;
		mtu = ip_tunnel_bind_dev(dev);
		if (set_mtu)
			dev->mtu = mtu;
	}
	netdev_state_change(dev);
}

int ip_tunnel_ioctl(struct net_device *dev, struct ip_tunnel_parm *p, int cmd)
{
	int err = 0;
	struct ip_tunnel *t;
	struct net *net = dev_net(dev);
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_net *itn = net_generic(net, tunnel->ip_tnl_net_id);

	BUG_ON(!itn->fb_tunnel_dev);
	switch (cmd) {
	case SIOCGETTUNNEL:
		t = NULL;
		if (dev == itn->fb_tunnel_dev)
			t = ip_tunnel_find(itn, p, itn->fb_tunnel_dev->type);
		if (t == NULL)
			t = netdev_priv(dev);
		memcpy(p, &t->parms, sizeof(*p));
		break;

	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			goto done;
		if (p->iph.ttl)
			p->iph.frag_off |= htons(IP_DF);
		if (!(p->i_flags&TUNNEL_KEY))
			p->i_key = 0;
		if (!(p->o_flags&TUNNEL_KEY))
			p->o_key = 0;

		t = ip_tunnel_find(itn, p, itn->fb_tunnel_dev->type);

		if (!t && (cmd == SIOCADDTUNNEL))
			t = ip_tunnel_create(net, itn, p);

		if (dev != itn->fb_tunnel_dev && cmd == SIOCCHGTUNNEL) {
			if (t != NULL) {
				if (t->dev != dev) {
					err = -EEXIST;
					break;
				}
			} else {
				unsigned int nflags = 0;

				if (ipv4_is_multicast(p->iph.daddr))
					nflags = IFF_BROADCAST;
				else if (p->iph.daddr)
					nflags = IFF_POINTOPOINT;

				if ((dev->flags^nflags)&(IFF_POINTOPOINT|IFF_BROADCAST)) {
					err = -EINVAL;
					break;
				}

				t = netdev_priv(dev);
			}
		}

		if (t) {
			err = 0;
			ip_tunnel_update(itn, t, dev, p, true);
		} else
			err = (cmd == SIOCADDTUNNEL ? -ENOBUFS : -ENOENT);
		break;

	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			goto done;

		if (dev == itn->fb_tunnel_dev) {
			err = -ENOENT;
			t = ip_tunnel_find(itn, p, itn->fb_tunnel_dev->type);
			if (t == NULL)
				goto done;
			err = -EPERM;
			if (t == netdev_priv(itn->fb_tunnel_dev))
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
EXPORT_SYMBOL_GPL(ip_tunnel_ioctl);

int ip_tunnel_change_mtu(struct net_device *dev, int new_mtu)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	int t_hlen = tunnel->hlen + sizeof(struct iphdr);

	if (new_mtu < 68 ||
	    new_mtu > 0xFFF8 - dev->hard_header_len - t_hlen)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}
EXPORT_SYMBOL_GPL(ip_tunnel_change_mtu);

static void ip_tunnel_dev_free(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	gro_cells_destroy(&tunnel->gro_cells);
	free_percpu(dev->tstats);
	free_netdev(dev);
}

void ip_tunnel_dellink(struct net_device *dev, struct list_head *head)
{
	struct net *net = dev_net(dev);
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_net *itn;

	itn = net_generic(net, tunnel->ip_tnl_net_id);

	if (itn->fb_tunnel_dev != dev) {
		ip_tunnel_del(netdev_priv(dev));
		unregister_netdevice_queue(dev, head);
	}
}
EXPORT_SYMBOL_GPL(ip_tunnel_dellink);

int ip_tunnel_init_net(struct net *net, int ip_tnl_net_id,
				  struct rtnl_link_ops *ops, char *devname)
{
	struct ip_tunnel_net *itn = net_generic(net, ip_tnl_net_id);
	struct ip_tunnel_parm parms;

	itn->tunnels = kzalloc(IP_TNL_HASH_SIZE * sizeof(struct hlist_head), GFP_KERNEL);
	if (!itn->tunnels)
		return -ENOMEM;

	if (!ops) {
		itn->fb_tunnel_dev = NULL;
		return 0;
	}
	memset(&parms, 0, sizeof(parms));
	if (devname)
		strlcpy(parms.name, devname, IFNAMSIZ);

	rtnl_lock();
	itn->fb_tunnel_dev = __ip_tunnel_create(net, ops, &parms);
	rtnl_unlock();
	if (IS_ERR(itn->fb_tunnel_dev)) {
		kfree(itn->tunnels);
		return PTR_ERR(itn->fb_tunnel_dev);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ip_tunnel_init_net);

static void ip_tunnel_destroy(struct ip_tunnel_net *itn, struct list_head *head)
{
	int h;

	for (h = 0; h < IP_TNL_HASH_SIZE; h++) {
		struct ip_tunnel *t;
		struct hlist_node *n;
		struct hlist_head *thead = &itn->tunnels[h];

		hlist_for_each_entry_safe(t, n, thead, hash_node)
			unregister_netdevice_queue(t->dev, head);
	}
	if (itn->fb_tunnel_dev)
		unregister_netdevice_queue(itn->fb_tunnel_dev, head);
}

void ip_tunnel_delete_net(struct ip_tunnel_net *itn)
{
	LIST_HEAD(list);

	rtnl_lock();
	ip_tunnel_destroy(itn, &list);
	unregister_netdevice_many(&list);
	rtnl_unlock();
	kfree(itn->tunnels);
}
EXPORT_SYMBOL_GPL(ip_tunnel_delete_net);

int ip_tunnel_newlink(struct net_device *dev, struct nlattr *tb[],
		      struct ip_tunnel_parm *p)
{
	struct ip_tunnel *nt;
	struct net *net = dev_net(dev);
	struct ip_tunnel_net *itn;
	int mtu;
	int err;

	nt = netdev_priv(dev);
	itn = net_generic(net, nt->ip_tnl_net_id);

	if (ip_tunnel_find(itn, p, dev->type))
		return -EEXIST;

	nt->parms = *p;
	err = register_netdevice(dev);
	if (err)
		goto out;

	if (dev->type == ARPHRD_ETHER && !tb[IFLA_ADDRESS])
		eth_hw_addr_random(dev);

	mtu = ip_tunnel_bind_dev(dev);
	if (!tb[IFLA_MTU])
		dev->mtu = mtu;

	ip_tunnel_add(itn, nt);

out:
	return err;
}
EXPORT_SYMBOL_GPL(ip_tunnel_newlink);

int ip_tunnel_changelink(struct net_device *dev, struct nlattr *tb[],
			 struct ip_tunnel_parm *p)
{
	struct ip_tunnel *t, *nt;
	struct net *net = dev_net(dev);
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_net *itn = net_generic(net, tunnel->ip_tnl_net_id);

	if (dev == itn->fb_tunnel_dev)
		return -EINVAL;

	nt = netdev_priv(dev);

	t = ip_tunnel_find(itn, p, dev->type);

	if (t) {
		if (t->dev != dev)
			return -EEXIST;
	} else {
		t = nt;

		if (dev->type != ARPHRD_ETHER) {
			unsigned int nflags = 0;

			if (ipv4_is_multicast(p->iph.daddr))
				nflags = IFF_BROADCAST;
			else if (p->iph.daddr)
				nflags = IFF_POINTOPOINT;

			if ((dev->flags ^ nflags) &
			    (IFF_POINTOPOINT | IFF_BROADCAST))
				return -EINVAL;
		}
	}

	ip_tunnel_update(itn, t, dev, p, !tb[IFLA_MTU]);
	return 0;
}
EXPORT_SYMBOL_GPL(ip_tunnel_changelink);

int ip_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;
	int err;

	dev->destructor	= ip_tunnel_dev_free;
	dev->tstats = alloc_percpu(struct pcpu_tstats);
	if (!dev->tstats)
		return -ENOMEM;

	err = gro_cells_init(&tunnel->gro_cells, dev);
	if (err) {
		free_percpu(dev->tstats);
		return err;
	}

	tunnel->dev = dev;
	strcpy(tunnel->parms.name, dev->name);
	iph->version		= 4;
	iph->ihl		= 5;

	return 0;
}
EXPORT_SYMBOL_GPL(ip_tunnel_init);

void ip_tunnel_uninit(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_net *itn;

	itn = net_generic(net, tunnel->ip_tnl_net_id);
	/* fb_tunnel_dev will be unregisted in net-exit call. */
	if (itn->fb_tunnel_dev != dev)
		ip_tunnel_del(netdev_priv(dev));
}
EXPORT_SYMBOL_GPL(ip_tunnel_uninit);

/* Do least required initialization, rest of init is done in tunnel_init call */
void ip_tunnel_setup(struct net_device *dev, int net_id)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	tunnel->ip_tnl_net_id = net_id;
}
EXPORT_SYMBOL_GPL(ip_tunnel_setup);

MODULE_LICENSE("GPL");
