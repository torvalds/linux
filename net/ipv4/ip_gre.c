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
#include <linux/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
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
#include <net/ip_tunnels.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/gre.h>
#include <net/dst_metadata.h>
#include <net/erspan.h>

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

   Alexey Kuznetsov.
 */

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

static struct rtnl_link_ops ipgre_link_ops __read_mostly;
static int ipgre_tunnel_init(struct net_device *dev);
static void erspan_build_header(struct sk_buff *skb,
				__be32 id, u32 index, bool truncate);

static unsigned int ipgre_net_id __read_mostly;
static unsigned int gre_tap_net_id __read_mostly;
static unsigned int erspan_net_id __read_mostly;

static void ipgre_err(struct sk_buff *skb, u32 info,
		      const struct tnl_ptk_info *tpi)
{

	/* All the routers (except for Linux) return only
	   8 bytes of packet payload. It means, that precise relaying of
	   ICMP in the real Internet is absolutely infeasible.

	   Moreover, Cisco "wise men" put GRE key to the third word
	   in GRE header. It makes impossible maintaining even soft
	   state for keyed GRE tunnels with enabled checksum. Tell
	   them "thank you".

	   Well, I wonder, rfc1812 was written by Cisco employee,
	   what the hell these idiots break standards established
	   by themselves???
	   */
	struct net *net = dev_net(skb->dev);
	struct ip_tunnel_net *itn;
	const struct iphdr *iph;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	unsigned int data_len = 0;
	struct ip_tunnel *t;

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
		data_len = icmp_hdr(skb)->un.reserved[1] * 4; /* RFC 4884 4.1 */
		break;

	case ICMP_REDIRECT:
		break;
	}

	if (tpi->proto == htons(ETH_P_TEB))
		itn = net_generic(net, gre_tap_net_id);
	else
		itn = net_generic(net, ipgre_net_id);

	iph = (const struct iphdr *)(icmp_hdr(skb) + 1);
	t = ip_tunnel_lookup(itn, skb->dev->ifindex, tpi->flags,
			     iph->daddr, iph->saddr, tpi->key);

	if (!t)
		return;

#if IS_ENABLED(CONFIG_IPV6)
       if (tpi->proto == htons(ETH_P_IPV6) &&
           !ip6_err_gen_icmpv6_unreach(skb, iph->ihl * 4 + tpi->hdr_len,
				       type, data_len))
               return;
#endif

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

static void gre_err(struct sk_buff *skb, u32 info)
{
	/* All the routers (except for Linux) return only
	 * 8 bytes of packet payload. It means, that precise relaying of
	 * ICMP in the real Internet is absolutely infeasible.
	 *
	 * Moreover, Cisco "wise men" put GRE key to the third word
	 * in GRE header. It makes impossible maintaining even soft
	 * state for keyed
	 * GRE tunnels with enabled checksum. Tell them "thank you".
	 *
	 * Well, I wonder, rfc1812 was written by Cisco employee,
	 * what the hell these idiots break standards established
	 * by themselves???
	 */

	const struct iphdr *iph = (struct iphdr *)skb->data;
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	struct tnl_ptk_info tpi;
	bool csum_err = false;

	if (gre_parse_header(skb, &tpi, &csum_err, htons(ETH_P_IP),
			     iph->ihl * 4) < 0) {
		if (!csum_err)		/* ignore csum errors. */
			return;
	}

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		ipv4_update_pmtu(skb, dev_net(skb->dev), info,
				 skb->dev->ifindex, 0, IPPROTO_GRE, 0);
		return;
	}
	if (type == ICMP_REDIRECT) {
		ipv4_redirect(skb, dev_net(skb->dev), skb->dev->ifindex, 0,
			      IPPROTO_GRE, 0);
		return;
	}

	ipgre_err(skb, info, &tpi);
}

static int erspan_rcv(struct sk_buff *skb, struct tnl_ptk_info *tpi,
		      int gre_hdr_len)
{
	struct net *net = dev_net(skb->dev);
	struct metadata_dst *tun_dst = NULL;
	struct ip_tunnel_net *itn;
	struct ip_tunnel *tunnel;
	struct erspanhdr *ershdr;
	const struct iphdr *iph;
	__be32 index;
	int len;

	itn = net_generic(net, erspan_net_id);
	len = gre_hdr_len + sizeof(*ershdr);

	if (unlikely(!pskb_may_pull(skb, len)))
		return -ENOMEM;

	iph = ip_hdr(skb);
	ershdr = (struct erspanhdr *)(skb->data + gre_hdr_len);

	/* The original GRE header does not have key field,
	 * Use ERSPAN 10-bit session ID as key.
	 */
	tpi->key = cpu_to_be32(ntohs(ershdr->session_id) & ID_MASK);
	index = ershdr->md.index;
	tunnel = ip_tunnel_lookup(itn, skb->dev->ifindex,
				  tpi->flags | TUNNEL_KEY,
				  iph->saddr, iph->daddr, tpi->key);

	if (tunnel) {
		if (__iptunnel_pull_header(skb,
					   gre_hdr_len + sizeof(*ershdr),
					   htons(ETH_P_TEB),
					   false, false) < 0)
			goto drop;

		if (tunnel->collect_md) {
			struct ip_tunnel_info *info;
			struct erspan_metadata *md;
			__be64 tun_id;
			__be16 flags;

			tpi->flags |= TUNNEL_KEY;
			flags = tpi->flags;
			tun_id = key32_to_tunnel_id(tpi->key);

			tun_dst = ip_tun_rx_dst(skb, flags,
						tun_id, sizeof(*md));
			if (!tun_dst)
				return PACKET_REJECT;

			md = ip_tunnel_info_opts(&tun_dst->u.tun_info);
			if (!md)
				return PACKET_REJECT;

			md->index = index;
			info = &tun_dst->u.tun_info;
			info->key.tun_flags |= TUNNEL_ERSPAN_OPT;
			info->options_len = sizeof(*md);
		} else {
			tunnel->index = ntohl(index);
		}

		skb_reset_mac_header(skb);
		ip_tunnel_rcv(tunnel, skb, tpi, tun_dst, log_ecn_error);
		return PACKET_RCVD;
	}
drop:
	kfree_skb(skb);
	return PACKET_RCVD;
}

static int __ipgre_rcv(struct sk_buff *skb, const struct tnl_ptk_info *tpi,
		       struct ip_tunnel_net *itn, int hdr_len, bool raw_proto)
{
	struct metadata_dst *tun_dst = NULL;
	const struct iphdr *iph;
	struct ip_tunnel *tunnel;

	iph = ip_hdr(skb);
	tunnel = ip_tunnel_lookup(itn, skb->dev->ifindex, tpi->flags,
				  iph->saddr, iph->daddr, tpi->key);

	if (tunnel) {
		if (__iptunnel_pull_header(skb, hdr_len, tpi->proto,
					   raw_proto, false) < 0)
			goto drop;

		if (tunnel->dev->type != ARPHRD_NONE)
			skb_pop_mac_header(skb);
		else
			skb_reset_mac_header(skb);
		if (tunnel->collect_md) {
			__be16 flags;
			__be64 tun_id;

			flags = tpi->flags & (TUNNEL_CSUM | TUNNEL_KEY);
			tun_id = key32_to_tunnel_id(tpi->key);
			tun_dst = ip_tun_rx_dst(skb, flags, tun_id, 0);
			if (!tun_dst)
				return PACKET_REJECT;
		}

		ip_tunnel_rcv(tunnel, skb, tpi, tun_dst, log_ecn_error);
		return PACKET_RCVD;
	}
	return PACKET_NEXT;

drop:
	kfree_skb(skb);
	return PACKET_RCVD;
}

static int ipgre_rcv(struct sk_buff *skb, const struct tnl_ptk_info *tpi,
		     int hdr_len)
{
	struct net *net = dev_net(skb->dev);
	struct ip_tunnel_net *itn;
	int res;

	if (tpi->proto == htons(ETH_P_TEB))
		itn = net_generic(net, gre_tap_net_id);
	else
		itn = net_generic(net, ipgre_net_id);

	res = __ipgre_rcv(skb, tpi, itn, hdr_len, false);
	if (res == PACKET_NEXT && tpi->proto == htons(ETH_P_TEB)) {
		/* ipgre tunnels in collect metadata mode should receive
		 * also ETH_P_TEB traffic.
		 */
		itn = net_generic(net, ipgre_net_id);
		res = __ipgre_rcv(skb, tpi, itn, hdr_len, true);
	}
	return res;
}

static int gre_rcv(struct sk_buff *skb)
{
	struct tnl_ptk_info tpi;
	bool csum_err = false;
	int hdr_len;

#ifdef CONFIG_NET_IPGRE_BROADCAST
	if (ipv4_is_multicast(ip_hdr(skb)->daddr)) {
		/* Looped back packet, drop it! */
		if (rt_is_output_route(skb_rtable(skb)))
			goto drop;
	}
#endif

	hdr_len = gre_parse_header(skb, &tpi, &csum_err, htons(ETH_P_IP), 0);
	if (hdr_len < 0)
		goto drop;

	if (unlikely(tpi.proto == htons(ETH_P_ERSPAN))) {
		if (erspan_rcv(skb, &tpi, hdr_len) == PACKET_RCVD)
			return 0;
	}

	if (ipgre_rcv(skb, &tpi, hdr_len) == PACKET_RCVD)
		return 0;

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
drop:
	kfree_skb(skb);
	return 0;
}

static void __gre_xmit(struct sk_buff *skb, struct net_device *dev,
		       const struct iphdr *tnl_params,
		       __be16 proto)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	if (tunnel->parms.o_flags & TUNNEL_SEQ)
		tunnel->o_seqno++;

	/* Push GRE header. */
	gre_build_header(skb, tunnel->tun_hlen,
			 tunnel->parms.o_flags, proto, tunnel->parms.o_key,
			 htonl(tunnel->o_seqno));

	ip_tunnel_xmit(skb, dev, tnl_params, tnl_params->protocol);
}

static int gre_handle_offloads(struct sk_buff *skb, bool csum)
{
	return iptunnel_handle_offloads(skb, csum ? SKB_GSO_GRE_CSUM : SKB_GSO_GRE);
}

static struct rtable *gre_get_rt(struct sk_buff *skb,
				 struct net_device *dev,
				 struct flowi4 *fl,
				 const struct ip_tunnel_key *key)
{
	struct net *net = dev_net(dev);

	memset(fl, 0, sizeof(*fl));
	fl->daddr = key->u.ipv4.dst;
	fl->saddr = key->u.ipv4.src;
	fl->flowi4_tos = RT_TOS(key->tos);
	fl->flowi4_mark = skb->mark;
	fl->flowi4_proto = IPPROTO_GRE;

	return ip_route_output_key(net, fl);
}

static struct rtable *prepare_fb_xmit(struct sk_buff *skb,
				      struct net_device *dev,
				      struct flowi4 *fl,
				      int tunnel_hlen)
{
	struct ip_tunnel_info *tun_info;
	const struct ip_tunnel_key *key;
	struct rtable *rt = NULL;
	int min_headroom;
	bool use_cache;
	int err;

	tun_info = skb_tunnel_info(skb);
	key = &tun_info->key;
	use_cache = ip_tunnel_dst_cache_usable(skb, tun_info);

	if (use_cache)
		rt = dst_cache_get_ip4(&tun_info->dst_cache, &fl->saddr);
	if (!rt) {
		rt = gre_get_rt(skb, dev, fl, key);
		if (IS_ERR(rt))
			goto err_free_skb;
		if (use_cache)
			dst_cache_set_ip4(&tun_info->dst_cache, &rt->dst,
					  fl->saddr);
	}

	min_headroom = LL_RESERVED_SPACE(rt->dst.dev) + rt->dst.header_len
			+ tunnel_hlen + sizeof(struct iphdr);
	if (skb_headroom(skb) < min_headroom || skb_header_cloned(skb)) {
		int head_delta = SKB_DATA_ALIGN(min_headroom -
						skb_headroom(skb) +
						16);
		err = pskb_expand_head(skb, max_t(int, head_delta, 0),
				       0, GFP_ATOMIC);
		if (unlikely(err))
			goto err_free_rt;
	}
	return rt;

err_free_rt:
	ip_rt_put(rt);
err_free_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return NULL;
}

static void gre_fb_xmit(struct sk_buff *skb, struct net_device *dev,
			__be16 proto)
{
	struct ip_tunnel_info *tun_info;
	const struct ip_tunnel_key *key;
	struct rtable *rt = NULL;
	struct flowi4 fl;
	int tunnel_hlen;
	__be16 df, flags;

	tun_info = skb_tunnel_info(skb);
	if (unlikely(!tun_info || !(tun_info->mode & IP_TUNNEL_INFO_TX) ||
		     ip_tunnel_info_af(tun_info) != AF_INET))
		goto err_free_skb;

	key = &tun_info->key;
	tunnel_hlen = gre_calc_hlen(key->tun_flags);

	rt = prepare_fb_xmit(skb, dev, &fl, tunnel_hlen);
	if (!rt)
		return;

	/* Push Tunnel header. */
	if (gre_handle_offloads(skb, !!(tun_info->key.tun_flags & TUNNEL_CSUM)))
		goto err_free_rt;

	flags = tun_info->key.tun_flags & (TUNNEL_CSUM | TUNNEL_KEY);
	gre_build_header(skb, tunnel_hlen, flags, proto,
			 tunnel_id_to_key32(tun_info->key.tun_id), 0);

	df = key->tun_flags & TUNNEL_DONT_FRAGMENT ?  htons(IP_DF) : 0;

	iptunnel_xmit(skb->sk, rt, skb, fl.saddr, key->u.ipv4.dst, IPPROTO_GRE,
		      key->tos, key->ttl, df, false);
	return;

err_free_rt:
	ip_rt_put(rt);
err_free_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
}

static void erspan_fb_xmit(struct sk_buff *skb, struct net_device *dev,
			   __be16 proto)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct ip_tunnel_info *tun_info;
	const struct ip_tunnel_key *key;
	struct erspan_metadata *md;
	struct rtable *rt = NULL;
	bool truncate = false;
	struct flowi4 fl;
	int tunnel_hlen;
	__be16 df;

	tun_info = skb_tunnel_info(skb);
	if (unlikely(!tun_info || !(tun_info->mode & IP_TUNNEL_INFO_TX) ||
		     ip_tunnel_info_af(tun_info) != AF_INET))
		goto err_free_skb;

	key = &tun_info->key;

	/* ERSPAN has fixed 8 byte GRE header */
	tunnel_hlen = 8 + sizeof(struct erspanhdr);

	rt = prepare_fb_xmit(skb, dev, &fl, tunnel_hlen);
	if (!rt)
		return;

	if (gre_handle_offloads(skb, false))
		goto err_free_rt;

	if (skb->len > dev->mtu + dev->hard_header_len) {
		pskb_trim(skb, dev->mtu + dev->hard_header_len);
		truncate = true;
	}

	md = ip_tunnel_info_opts(tun_info);
	if (!md)
		goto err_free_rt;

	erspan_build_header(skb, tunnel_id_to_key32(key->tun_id),
			    ntohl(md->index), truncate);

	gre_build_header(skb, 8, TUNNEL_SEQ,
			 htons(ETH_P_ERSPAN), 0, htonl(tunnel->o_seqno++));

	df = key->tun_flags & TUNNEL_DONT_FRAGMENT ?  htons(IP_DF) : 0;

	iptunnel_xmit(skb->sk, rt, skb, fl.saddr, key->u.ipv4.dst, IPPROTO_GRE,
		      key->tos, key->ttl, df, false);
	return;

err_free_rt:
	ip_rt_put(rt);
err_free_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
}

static int gre_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb)
{
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	struct rtable *rt;
	struct flowi4 fl4;

	if (ip_tunnel_info_af(info) != AF_INET)
		return -EINVAL;

	rt = gre_get_rt(skb, dev, &fl4, &info->key);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	ip_rt_put(rt);
	info->key.u.ipv4.src = fl4.saddr;
	return 0;
}

static netdev_tx_t ipgre_xmit(struct sk_buff *skb,
			      struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	const struct iphdr *tnl_params;

	if (tunnel->collect_md) {
		gre_fb_xmit(skb, dev, skb->protocol);
		return NETDEV_TX_OK;
	}

	if (dev->header_ops) {
		/* Need space for new headers */
		if (skb_cow_head(skb, dev->needed_headroom -
				      (tunnel->hlen + sizeof(struct iphdr))))
			goto free_skb;

		tnl_params = (const struct iphdr *)skb->data;

		/* Pull skb since ip_tunnel_xmit() needs skb->data pointing
		 * to gre header.
		 */
		skb_pull(skb, tunnel->hlen + sizeof(struct iphdr));
		skb_reset_mac_header(skb);
	} else {
		if (skb_cow_head(skb, dev->needed_headroom))
			goto free_skb;

		tnl_params = &tunnel->parms.iph;
	}

	if (gre_handle_offloads(skb, !!(tunnel->parms.o_flags & TUNNEL_CSUM)))
		goto free_skb;

	__gre_xmit(skb, dev, tnl_params, skb->protocol);
	return NETDEV_TX_OK;

free_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static inline u8 tos_to_cos(u8 tos)
{
	u8 dscp, cos;

	dscp = tos >> 2;
	cos = dscp >> 3;
	return cos;
}

static void erspan_build_header(struct sk_buff *skb,
				__be32 id, u32 index, bool truncate)
{
	struct iphdr *iphdr = ip_hdr(skb);
	struct ethhdr *eth = eth_hdr(skb);
	enum erspan_encap_type enc_type;
	struct erspanhdr *ershdr;
	struct qtag_prefix {
		__be16 eth_type;
		__be16 tci;
	} *qp;
	u16 vlan_tci = 0;

	enc_type = ERSPAN_ENCAP_NOVLAN;

	/* If mirrored packet has vlan tag, extract tci and
	 *  perserve vlan header in the mirrored frame.
	 */
	if (eth->h_proto == htons(ETH_P_8021Q)) {
		qp = (struct qtag_prefix *)(skb->data + 2 * ETH_ALEN);
		vlan_tci = ntohs(qp->tci);
		enc_type = ERSPAN_ENCAP_INFRAME;
	}

	skb_push(skb, sizeof(*ershdr));
	ershdr = (struct erspanhdr *)skb->data;
	memset(ershdr, 0, sizeof(*ershdr));

	ershdr->ver_vlan = htons((vlan_tci & VLAN_MASK) |
				 (ERSPAN_VERSION << VER_OFFSET));
	ershdr->session_id = htons((u16)(ntohl(id) & ID_MASK) |
			   ((tos_to_cos(iphdr->tos) << COS_OFFSET) & COS_MASK) |
			   (enc_type << EN_OFFSET & EN_MASK) |
			   ((truncate << T_OFFSET) & T_MASK));
	ershdr->md.index = htonl(index & INDEX_MASK);
}

static netdev_tx_t erspan_xmit(struct sk_buff *skb,
			       struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	bool truncate = false;

	if (tunnel->collect_md) {
		erspan_fb_xmit(skb, dev, skb->protocol);
		return NETDEV_TX_OK;
	}

	if (gre_handle_offloads(skb, false))
		goto free_skb;

	if (skb_cow_head(skb, dev->needed_headroom))
		goto free_skb;

	if (skb->len > dev->mtu + dev->hard_header_len) {
		pskb_trim(skb, dev->mtu + dev->hard_header_len);
		truncate = true;
	}

	/* Push ERSPAN header */
	erspan_build_header(skb, tunnel->parms.o_key, tunnel->index, truncate);
	tunnel->parms.o_flags &= ~TUNNEL_KEY;
	__gre_xmit(skb, dev, &tunnel->parms.iph, htons(ETH_P_ERSPAN));
	return NETDEV_TX_OK;

free_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static netdev_tx_t gre_tap_xmit(struct sk_buff *skb,
				struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);

	if (tunnel->collect_md) {
		gre_fb_xmit(skb, dev, htons(ETH_P_TEB));
		return NETDEV_TX_OK;
	}

	if (gre_handle_offloads(skb, !!(tunnel->parms.o_flags & TUNNEL_CSUM)))
		goto free_skb;

	if (skb_cow_head(skb, dev->needed_headroom))
		goto free_skb;

	__gre_xmit(skb, dev, &tunnel->parms.iph, htons(ETH_P_TEB));
	return NETDEV_TX_OK;

free_skb:
	kfree_skb(skb);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static void ipgre_link_update(struct net_device *dev, bool set_mtu)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	int len;

	len = tunnel->tun_hlen;
	tunnel->tun_hlen = gre_calc_hlen(tunnel->parms.o_flags);
	len = tunnel->tun_hlen - len;
	tunnel->hlen = tunnel->hlen + len;

	dev->needed_headroom = dev->needed_headroom + len;
	if (set_mtu)
		dev->mtu = max_t(int, dev->mtu - len, 68);

	if (!(tunnel->parms.o_flags & TUNNEL_SEQ)) {
		if (!(tunnel->parms.o_flags & TUNNEL_CSUM) ||
		    tunnel->encap.type == TUNNEL_ENCAP_NONE) {
			dev->features |= NETIF_F_GSO_SOFTWARE;
			dev->hw_features |= NETIF_F_GSO_SOFTWARE;
		}
		dev->features |= NETIF_F_LLTX;
	}
}

static int ipgre_tunnel_ioctl(struct net_device *dev,
			      struct ifreq *ifr, int cmd)
{
	int err;
	struct ip_tunnel_parm p;

	if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
		return -EFAULT;
	if (cmd == SIOCADDTUNNEL || cmd == SIOCCHGTUNNEL) {
		if (p.iph.version != 4 || p.iph.protocol != IPPROTO_GRE ||
		    p.iph.ihl != 5 || (p.iph.frag_off&htons(~IP_DF)) ||
		    ((p.i_flags|p.o_flags)&(GRE_VERSION|GRE_ROUTING)))
			return -EINVAL;
	}
	p.i_flags = gre_flags_to_tnl_flags(p.i_flags);
	p.o_flags = gre_flags_to_tnl_flags(p.o_flags);

	err = ip_tunnel_ioctl(dev, &p, cmd);
	if (err)
		return err;

	p.i_flags = gre_tnl_flags_to_gre_flags(p.i_flags);
	p.o_flags = gre_tnl_flags_to_gre_flags(p.o_flags);

	if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
		return -EFAULT;
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
	struct iphdr *iph;
	struct gre_base_hdr *greh;

	iph = skb_push(skb, t->hlen + sizeof(*iph));
	greh = (struct gre_base_hdr *)(iph+1);
	greh->flags = gre_tnl_flags_to_gre_flags(t->parms.o_flags);
	greh->protocol = htons(type);

	memcpy(iph, &t->parms.iph, sizeof(struct iphdr));

	/* Set the source hardware address. */
	if (saddr)
		memcpy(&iph->saddr, saddr, 4);
	if (daddr)
		memcpy(&iph->daddr, daddr, 4);
	if (iph->daddr)
		return t->hlen + sizeof(*iph);

	return -(t->hlen + sizeof(*iph));
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

		rt = ip_route_output_gre(t->net, &fl4,
					 t->parms.iph.daddr,
					 t->parms.iph.saddr,
					 t->parms.o_key,
					 RT_TOS(t->parms.iph.tos),
					 t->parms.link);
		if (IS_ERR(rt))
			return -EADDRNOTAVAIL;
		dev = rt->dst.dev;
		ip_rt_put(rt);
		if (!__in_dev_get_rtnl(dev))
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
		in_dev = inetdev_by_index(t->net, t->mlink);
		if (in_dev)
			ip_mc_dec_group(in_dev, t->parms.iph.daddr);
	}
	return 0;
}
#endif

static const struct net_device_ops ipgre_netdev_ops = {
	.ndo_init		= ipgre_tunnel_init,
	.ndo_uninit		= ip_tunnel_uninit,
#ifdef CONFIG_NET_IPGRE_BROADCAST
	.ndo_open		= ipgre_open,
	.ndo_stop		= ipgre_close,
#endif
	.ndo_start_xmit		= ipgre_xmit,
	.ndo_do_ioctl		= ipgre_tunnel_ioctl,
	.ndo_change_mtu		= ip_tunnel_change_mtu,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
	.ndo_get_iflink		= ip_tunnel_get_iflink,
};

#define GRE_FEATURES (NETIF_F_SG |		\
		      NETIF_F_FRAGLIST |	\
		      NETIF_F_HIGHDMA |		\
		      NETIF_F_HW_CSUM)

static void ipgre_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops		= &ipgre_netdev_ops;
	dev->type		= ARPHRD_IPGRE;
	ip_tunnel_setup(dev, ipgre_net_id);
}

static void __gre_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel;
	int t_hlen;

	tunnel = netdev_priv(dev);
	tunnel->tun_hlen = gre_calc_hlen(tunnel->parms.o_flags);
	tunnel->parms.iph.protocol = IPPROTO_GRE;

	tunnel->hlen = tunnel->tun_hlen + tunnel->encap_hlen;

	t_hlen = tunnel->hlen + sizeof(struct iphdr);

	dev->needed_headroom	= LL_MAX_HEADER + t_hlen + 4;
	dev->mtu		= ETH_DATA_LEN - t_hlen - 4;

	dev->features		|= GRE_FEATURES;
	dev->hw_features	|= GRE_FEATURES;

	if (!(tunnel->parms.o_flags & TUNNEL_SEQ)) {
		/* TCP offload with GRE SEQ is not supported, nor
		 * can we support 2 levels of outer headers requiring
		 * an update.
		 */
		if (!(tunnel->parms.o_flags & TUNNEL_CSUM) ||
		    (tunnel->encap.type == TUNNEL_ENCAP_NONE)) {
			dev->features    |= NETIF_F_GSO_SOFTWARE;
			dev->hw_features |= NETIF_F_GSO_SOFTWARE;
		}

		/* Can use a lockless transmit, unless we generate
		 * output sequences
		 */
		dev->features |= NETIF_F_LLTX;
	}
}

static int ipgre_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	struct iphdr *iph = &tunnel->parms.iph;

	__gre_tunnel_init(dev);

	memcpy(dev->dev_addr, &iph->saddr, 4);
	memcpy(dev->broadcast, &iph->daddr, 4);

	dev->flags		= IFF_NOARP;
	netif_keep_dst(dev);
	dev->addr_len		= 4;

	if (iph->daddr && !tunnel->collect_md) {
#ifdef CONFIG_NET_IPGRE_BROADCAST
		if (ipv4_is_multicast(iph->daddr)) {
			if (!iph->saddr)
				return -EINVAL;
			dev->flags = IFF_BROADCAST;
			dev->header_ops = &ipgre_header_ops;
		}
#endif
	} else if (!tunnel->collect_md) {
		dev->header_ops = &ipgre_header_ops;
	}

	return ip_tunnel_init(dev);
}

static const struct gre_protocol ipgre_protocol = {
	.handler     = gre_rcv,
	.err_handler = gre_err,
};

static int __net_init ipgre_init_net(struct net *net)
{
	return ip_tunnel_init_net(net, ipgre_net_id, &ipgre_link_ops, NULL);
}

static void __net_exit ipgre_exit_batch_net(struct list_head *list_net)
{
	ip_tunnel_delete_nets(list_net, ipgre_net_id, &ipgre_link_ops);
}

static struct pernet_operations ipgre_net_ops = {
	.init = ipgre_init_net,
	.exit_batch = ipgre_exit_batch_net,
	.id   = &ipgre_net_id,
	.size = sizeof(struct ip_tunnel_net),
};

static int ipgre_tunnel_validate(struct nlattr *tb[], struct nlattr *data[],
				 struct netlink_ext_ack *extack)
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

	if (data[IFLA_GRE_COLLECT_METADATA] &&
	    data[IFLA_GRE_ENCAP_TYPE] &&
	    nla_get_u16(data[IFLA_GRE_ENCAP_TYPE]) != TUNNEL_ENCAP_NONE)
		return -EINVAL;

	return 0;
}

static int ipgre_tap_validate(struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
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
	return ipgre_tunnel_validate(tb, data, extack);
}

static int erspan_validate(struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	__be16 flags = 0;
	int ret;

	if (!data)
		return 0;

	ret = ipgre_tap_validate(tb, data, extack);
	if (ret)
		return ret;

	/* ERSPAN should only have GRE sequence and key flag */
	if (data[IFLA_GRE_OFLAGS])
		flags |= nla_get_be16(data[IFLA_GRE_OFLAGS]);
	if (data[IFLA_GRE_IFLAGS])
		flags |= nla_get_be16(data[IFLA_GRE_IFLAGS]);
	if (!data[IFLA_GRE_COLLECT_METADATA] &&
	    flags != (GRE_SEQ | GRE_KEY))
		return -EINVAL;

	/* ERSPAN Session ID only has 10-bit. Since we reuse
	 * 32-bit key field as ID, check it's range.
	 */
	if (data[IFLA_GRE_IKEY] &&
	    (ntohl(nla_get_be32(data[IFLA_GRE_IKEY])) & ~ID_MASK))
		return -EINVAL;

	if (data[IFLA_GRE_OKEY] &&
	    (ntohl(nla_get_be32(data[IFLA_GRE_OKEY])) & ~ID_MASK))
		return -EINVAL;

	return 0;
}

static int ipgre_netlink_parms(struct net_device *dev,
				struct nlattr *data[],
				struct nlattr *tb[],
				struct ip_tunnel_parm *parms,
				__u32 *fwmark)
{
	struct ip_tunnel *t = netdev_priv(dev);

	memset(parms, 0, sizeof(*parms));

	parms->iph.protocol = IPPROTO_GRE;

	if (!data)
		return 0;

	if (data[IFLA_GRE_LINK])
		parms->link = nla_get_u32(data[IFLA_GRE_LINK]);

	if (data[IFLA_GRE_IFLAGS])
		parms->i_flags = gre_flags_to_tnl_flags(nla_get_be16(data[IFLA_GRE_IFLAGS]));

	if (data[IFLA_GRE_OFLAGS])
		parms->o_flags = gre_flags_to_tnl_flags(nla_get_be16(data[IFLA_GRE_OFLAGS]));

	if (data[IFLA_GRE_IKEY])
		parms->i_key = nla_get_be32(data[IFLA_GRE_IKEY]);

	if (data[IFLA_GRE_OKEY])
		parms->o_key = nla_get_be32(data[IFLA_GRE_OKEY]);

	if (data[IFLA_GRE_LOCAL])
		parms->iph.saddr = nla_get_in_addr(data[IFLA_GRE_LOCAL]);

	if (data[IFLA_GRE_REMOTE])
		parms->iph.daddr = nla_get_in_addr(data[IFLA_GRE_REMOTE]);

	if (data[IFLA_GRE_TTL])
		parms->iph.ttl = nla_get_u8(data[IFLA_GRE_TTL]);

	if (data[IFLA_GRE_TOS])
		parms->iph.tos = nla_get_u8(data[IFLA_GRE_TOS]);

	if (!data[IFLA_GRE_PMTUDISC] || nla_get_u8(data[IFLA_GRE_PMTUDISC])) {
		if (t->ignore_df)
			return -EINVAL;
		parms->iph.frag_off = htons(IP_DF);
	}

	if (data[IFLA_GRE_COLLECT_METADATA]) {
		t->collect_md = true;
		if (dev->type == ARPHRD_IPGRE)
			dev->type = ARPHRD_NONE;
	}

	if (data[IFLA_GRE_IGNORE_DF]) {
		if (nla_get_u8(data[IFLA_GRE_IGNORE_DF])
		  && (parms->iph.frag_off & htons(IP_DF)))
			return -EINVAL;
		t->ignore_df = !!nla_get_u8(data[IFLA_GRE_IGNORE_DF]);
	}

	if (data[IFLA_GRE_FWMARK])
		*fwmark = nla_get_u32(data[IFLA_GRE_FWMARK]);

	if (data[IFLA_GRE_ERSPAN_INDEX]) {
		t->index = nla_get_u32(data[IFLA_GRE_ERSPAN_INDEX]);

		if (t->index & ~INDEX_MASK)
			return -EINVAL;
	}

	return 0;
}

/* This function returns true when ENCAP attributes are present in the nl msg */
static bool ipgre_netlink_encap_parms(struct nlattr *data[],
				      struct ip_tunnel_encap *ipencap)
{
	bool ret = false;

	memset(ipencap, 0, sizeof(*ipencap));

	if (!data)
		return ret;

	if (data[IFLA_GRE_ENCAP_TYPE]) {
		ret = true;
		ipencap->type = nla_get_u16(data[IFLA_GRE_ENCAP_TYPE]);
	}

	if (data[IFLA_GRE_ENCAP_FLAGS]) {
		ret = true;
		ipencap->flags = nla_get_u16(data[IFLA_GRE_ENCAP_FLAGS]);
	}

	if (data[IFLA_GRE_ENCAP_SPORT]) {
		ret = true;
		ipencap->sport = nla_get_be16(data[IFLA_GRE_ENCAP_SPORT]);
	}

	if (data[IFLA_GRE_ENCAP_DPORT]) {
		ret = true;
		ipencap->dport = nla_get_be16(data[IFLA_GRE_ENCAP_DPORT]);
	}

	return ret;
}

static int gre_tap_init(struct net_device *dev)
{
	__gre_tunnel_init(dev);
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	netif_keep_dst(dev);

	return ip_tunnel_init(dev);
}

static const struct net_device_ops gre_tap_netdev_ops = {
	.ndo_init		= gre_tap_init,
	.ndo_uninit		= ip_tunnel_uninit,
	.ndo_start_xmit		= gre_tap_xmit,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= ip_tunnel_change_mtu,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
	.ndo_get_iflink		= ip_tunnel_get_iflink,
	.ndo_fill_metadata_dst	= gre_fill_metadata_dst,
};

static int erspan_tunnel_init(struct net_device *dev)
{
	struct ip_tunnel *tunnel = netdev_priv(dev);
	int t_hlen;

	tunnel->tun_hlen = 8;
	tunnel->parms.iph.protocol = IPPROTO_GRE;
	tunnel->hlen = tunnel->tun_hlen + tunnel->encap_hlen +
		       sizeof(struct erspanhdr);
	t_hlen = tunnel->hlen + sizeof(struct iphdr);

	dev->needed_headroom = LL_MAX_HEADER + t_hlen + 4;
	dev->mtu = ETH_DATA_LEN - t_hlen - 4;
	dev->features		|= GRE_FEATURES;
	dev->hw_features	|= GRE_FEATURES;
	dev->priv_flags		|= IFF_LIVE_ADDR_CHANGE;
	netif_keep_dst(dev);

	return ip_tunnel_init(dev);
}

static const struct net_device_ops erspan_netdev_ops = {
	.ndo_init		= erspan_tunnel_init,
	.ndo_uninit		= ip_tunnel_uninit,
	.ndo_start_xmit		= erspan_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= ip_tunnel_change_mtu,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
	.ndo_get_iflink		= ip_tunnel_get_iflink,
	.ndo_fill_metadata_dst	= gre_fill_metadata_dst,
};

static void ipgre_tap_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops	= &gre_tap_netdev_ops;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags	|= IFF_LIVE_ADDR_CHANGE;
	ip_tunnel_setup(dev, gre_tap_net_id);
}

static int ipgre_newlink(struct net *src_net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	struct ip_tunnel_parm p;
	struct ip_tunnel_encap ipencap;
	__u32 fwmark = 0;
	int err;

	if (ipgre_netlink_encap_parms(data, &ipencap)) {
		struct ip_tunnel *t = netdev_priv(dev);
		err = ip_tunnel_encap_setup(t, &ipencap);

		if (err < 0)
			return err;
	}

	err = ipgre_netlink_parms(dev, data, tb, &p, &fwmark);
	if (err < 0)
		return err;
	return ip_tunnel_newlink(dev, tb, &p, fwmark);
}

static int ipgre_changelink(struct net_device *dev, struct nlattr *tb[],
			    struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct ip_tunnel *t = netdev_priv(dev);
	struct ip_tunnel_encap ipencap;
	__u32 fwmark = t->fwmark;
	struct ip_tunnel_parm p;
	int err;

	if (ipgre_netlink_encap_parms(data, &ipencap)) {
		err = ip_tunnel_encap_setup(t, &ipencap);

		if (err < 0)
			return err;
	}

	err = ipgre_netlink_parms(dev, data, tb, &p, &fwmark);
	if (err < 0)
		return err;

	err = ip_tunnel_changelink(dev, tb, &p, fwmark);
	if (err < 0)
		return err;

	t->parms.i_flags = p.i_flags;
	t->parms.o_flags = p.o_flags;

	if (strcmp(dev->rtnl_link_ops->kind, "erspan"))
		ipgre_link_update(dev, !tb[IFLA_MTU]);

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
		/* IFLA_GRE_ENCAP_TYPE */
		nla_total_size(2) +
		/* IFLA_GRE_ENCAP_FLAGS */
		nla_total_size(2) +
		/* IFLA_GRE_ENCAP_SPORT */
		nla_total_size(2) +
		/* IFLA_GRE_ENCAP_DPORT */
		nla_total_size(2) +
		/* IFLA_GRE_COLLECT_METADATA */
		nla_total_size(0) +
		/* IFLA_GRE_IGNORE_DF */
		nla_total_size(1) +
		/* IFLA_GRE_FWMARK */
		nla_total_size(4) +
		/* IFLA_GRE_ERSPAN_INDEX */
		nla_total_size(4) +
		0;
}

static int ipgre_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip_tunnel *t = netdev_priv(dev);
	struct ip_tunnel_parm *p = &t->parms;

	if (nla_put_u32(skb, IFLA_GRE_LINK, p->link) ||
	    nla_put_be16(skb, IFLA_GRE_IFLAGS,
			 gre_tnl_flags_to_gre_flags(p->i_flags)) ||
	    nla_put_be16(skb, IFLA_GRE_OFLAGS,
			 gre_tnl_flags_to_gre_flags(p->o_flags)) ||
	    nla_put_be32(skb, IFLA_GRE_IKEY, p->i_key) ||
	    nla_put_be32(skb, IFLA_GRE_OKEY, p->o_key) ||
	    nla_put_in_addr(skb, IFLA_GRE_LOCAL, p->iph.saddr) ||
	    nla_put_in_addr(skb, IFLA_GRE_REMOTE, p->iph.daddr) ||
	    nla_put_u8(skb, IFLA_GRE_TTL, p->iph.ttl) ||
	    nla_put_u8(skb, IFLA_GRE_TOS, p->iph.tos) ||
	    nla_put_u8(skb, IFLA_GRE_PMTUDISC,
		       !!(p->iph.frag_off & htons(IP_DF))) ||
	    nla_put_u32(skb, IFLA_GRE_FWMARK, t->fwmark))
		goto nla_put_failure;

	if (nla_put_u16(skb, IFLA_GRE_ENCAP_TYPE,
			t->encap.type) ||
	    nla_put_be16(skb, IFLA_GRE_ENCAP_SPORT,
			 t->encap.sport) ||
	    nla_put_be16(skb, IFLA_GRE_ENCAP_DPORT,
			 t->encap.dport) ||
	    nla_put_u16(skb, IFLA_GRE_ENCAP_FLAGS,
			t->encap.flags))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_GRE_IGNORE_DF, t->ignore_df))
		goto nla_put_failure;

	if (t->collect_md) {
		if (nla_put_flag(skb, IFLA_GRE_COLLECT_METADATA))
			goto nla_put_failure;
	}

	if (t->index)
		if (nla_put_u32(skb, IFLA_GRE_ERSPAN_INDEX, t->index))
			goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static void erspan_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &erspan_netdev_ops;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	ip_tunnel_setup(dev, erspan_net_id);
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
	[IFLA_GRE_ENCAP_TYPE]	= { .type = NLA_U16 },
	[IFLA_GRE_ENCAP_FLAGS]	= { .type = NLA_U16 },
	[IFLA_GRE_ENCAP_SPORT]	= { .type = NLA_U16 },
	[IFLA_GRE_ENCAP_DPORT]	= { .type = NLA_U16 },
	[IFLA_GRE_COLLECT_METADATA]	= { .type = NLA_FLAG },
	[IFLA_GRE_IGNORE_DF]	= { .type = NLA_U8 },
	[IFLA_GRE_FWMARK]	= { .type = NLA_U32 },
	[IFLA_GRE_ERSPAN_INDEX]	= { .type = NLA_U32 },
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
	.dellink	= ip_tunnel_dellink,
	.get_size	= ipgre_get_size,
	.fill_info	= ipgre_fill_info,
	.get_link_net	= ip_tunnel_get_link_net,
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
	.dellink	= ip_tunnel_dellink,
	.get_size	= ipgre_get_size,
	.fill_info	= ipgre_fill_info,
	.get_link_net	= ip_tunnel_get_link_net,
};

static struct rtnl_link_ops erspan_link_ops __read_mostly = {
	.kind		= "erspan",
	.maxtype	= IFLA_GRE_MAX,
	.policy		= ipgre_policy,
	.priv_size	= sizeof(struct ip_tunnel),
	.setup		= erspan_setup,
	.validate	= erspan_validate,
	.newlink	= ipgre_newlink,
	.changelink	= ipgre_changelink,
	.dellink	= ip_tunnel_dellink,
	.get_size	= ipgre_get_size,
	.fill_info	= ipgre_fill_info,
	.get_link_net	= ip_tunnel_get_link_net,
};

struct net_device *gretap_fb_dev_create(struct net *net, const char *name,
					u8 name_assign_type)
{
	struct nlattr *tb[IFLA_MAX + 1];
	struct net_device *dev;
	LIST_HEAD(list_kill);
	struct ip_tunnel *t;
	int err;

	memset(&tb, 0, sizeof(tb));

	dev = rtnl_create_link(net, name, name_assign_type,
			       &ipgre_tap_ops, tb);
	if (IS_ERR(dev))
		return dev;

	/* Configure flow based GRE device. */
	t = netdev_priv(dev);
	t->collect_md = true;

	err = ipgre_newlink(net, dev, tb, NULL, NULL);
	if (err < 0) {
		free_netdev(dev);
		return ERR_PTR(err);
	}

	/* openvswitch users expect packet sizes to be unrestricted,
	 * so set the largest MTU we can.
	 */
	err = __ip_tunnel_change_mtu(dev, IP_MAX_MTU, false);
	if (err)
		goto out;

	err = rtnl_configure_link(dev, NULL);
	if (err < 0)
		goto out;

	return dev;
out:
	ip_tunnel_dellink(dev, &list_kill);
	unregister_netdevice_many(&list_kill);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(gretap_fb_dev_create);

static int __net_init ipgre_tap_init_net(struct net *net)
{
	return ip_tunnel_init_net(net, gre_tap_net_id, &ipgre_tap_ops, "gretap0");
}

static void __net_exit ipgre_tap_exit_batch_net(struct list_head *list_net)
{
	ip_tunnel_delete_nets(list_net, gre_tap_net_id, &ipgre_tap_ops);
}

static struct pernet_operations ipgre_tap_net_ops = {
	.init = ipgre_tap_init_net,
	.exit_batch = ipgre_tap_exit_batch_net,
	.id   = &gre_tap_net_id,
	.size = sizeof(struct ip_tunnel_net),
};

static int __net_init erspan_init_net(struct net *net)
{
	return ip_tunnel_init_net(net, erspan_net_id,
				  &erspan_link_ops, "erspan0");
}

static void __net_exit erspan_exit_batch_net(struct list_head *net_list)
{
	ip_tunnel_delete_nets(net_list, erspan_net_id, &erspan_link_ops);
}

static struct pernet_operations erspan_net_ops = {
	.init = erspan_init_net,
	.exit_batch = erspan_exit_batch_net,
	.id   = &erspan_net_id,
	.size = sizeof(struct ip_tunnel_net),
};

static int __init ipgre_init(void)
{
	int err;

	pr_info("GRE over IPv4 tunneling driver\n");

	err = register_pernet_device(&ipgre_net_ops);
	if (err < 0)
		return err;

	err = register_pernet_device(&ipgre_tap_net_ops);
	if (err < 0)
		goto pnet_tap_failed;

	err = register_pernet_device(&erspan_net_ops);
	if (err < 0)
		goto pnet_erspan_failed;

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

	err = rtnl_link_register(&erspan_link_ops);
	if (err < 0)
		goto erspan_link_failed;

	return 0;

erspan_link_failed:
	rtnl_link_unregister(&ipgre_tap_ops);
tap_ops_failed:
	rtnl_link_unregister(&ipgre_link_ops);
rtnl_link_failed:
	gre_del_protocol(&ipgre_protocol, GREPROTO_CISCO);
add_proto_failed:
	unregister_pernet_device(&erspan_net_ops);
pnet_erspan_failed:
	unregister_pernet_device(&ipgre_tap_net_ops);
pnet_tap_failed:
	unregister_pernet_device(&ipgre_net_ops);
	return err;
}

static void __exit ipgre_fini(void)
{
	rtnl_link_unregister(&ipgre_tap_ops);
	rtnl_link_unregister(&ipgre_link_ops);
	rtnl_link_unregister(&erspan_link_ops);
	gre_del_protocol(&ipgre_protocol, GREPROTO_CISCO);
	unregister_pernet_device(&ipgre_tap_net_ops);
	unregister_pernet_device(&ipgre_net_ops);
	unregister_pernet_device(&erspan_net_ops);
}

module_init(ipgre_init);
module_exit(ipgre_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("gre");
MODULE_ALIAS_RTNL_LINK("gretap");
MODULE_ALIAS_RTNL_LINK("erspan");
MODULE_ALIAS_NETDEV("gre0");
MODULE_ALIAS_NETDEV("gretap0");
MODULE_ALIAS_NETDEV("erspan0");
