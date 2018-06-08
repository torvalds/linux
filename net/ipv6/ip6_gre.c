/*
 *	GRE over IPv6 protocol decoder.
 *
 *	Authors: Dmitry Kozlov (xeb@mail.ru)
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
#include <linux/init.h>
#include <linux/in6.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/netfilter_ipv4.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/hash.h>
#include <linux/if_tunnel.h>
#include <linux/ip6_tunnel.h>

#include <net/sock.h>
#include <net/ip.h>
#include <net/ip_tunnels.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/addrconf.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>

#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/ip6_tunnel.h>
#include <net/gre.h>
#include <net/erspan.h>
#include <net/dst_metadata.h>


static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

#define IP6_GRE_HASH_SIZE_SHIFT  5
#define IP6_GRE_HASH_SIZE (1 << IP6_GRE_HASH_SIZE_SHIFT)

static unsigned int ip6gre_net_id __read_mostly;
struct ip6gre_net {
	struct ip6_tnl __rcu *tunnels[4][IP6_GRE_HASH_SIZE];

	struct ip6_tnl __rcu *collect_md_tun;
	struct net_device *fb_tunnel_dev;
};

static struct rtnl_link_ops ip6gre_link_ops __read_mostly;
static struct rtnl_link_ops ip6gre_tap_ops __read_mostly;
static struct rtnl_link_ops ip6erspan_tap_ops __read_mostly;
static int ip6gre_tunnel_init(struct net_device *dev);
static void ip6gre_tunnel_setup(struct net_device *dev);
static void ip6gre_tunnel_link(struct ip6gre_net *ign, struct ip6_tnl *t);
static void ip6gre_tnl_link_config(struct ip6_tnl *t, int set_mtu);

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

#define HASH_KEY(key) (((__force u32)key^((__force u32)key>>4))&(IP6_GRE_HASH_SIZE - 1))
static u32 HASH_ADDR(const struct in6_addr *addr)
{
	u32 hash = ipv6_addr_hash(addr);

	return hash_32(hash, IP6_GRE_HASH_SIZE_SHIFT);
}

#define tunnels_r_l	tunnels[3]
#define tunnels_r	tunnels[2]
#define tunnels_l	tunnels[1]
#define tunnels_wc	tunnels[0]

/* Given src, dst and key, find appropriate for input tunnel. */

static struct ip6_tnl *ip6gre_tunnel_lookup(struct net_device *dev,
		const struct in6_addr *remote, const struct in6_addr *local,
		__be32 key, __be16 gre_proto)
{
	struct net *net = dev_net(dev);
	int link = dev->ifindex;
	unsigned int h0 = HASH_ADDR(remote);
	unsigned int h1 = HASH_KEY(key);
	struct ip6_tnl *t, *cand = NULL;
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);
	int dev_type = (gre_proto == htons(ETH_P_TEB) ||
			gre_proto == htons(ETH_P_ERSPAN) ||
			gre_proto == htons(ETH_P_ERSPAN2)) ?
		       ARPHRD_ETHER : ARPHRD_IP6GRE;
	int score, cand_score = 4;

	for_each_ip_tunnel_rcu(t, ign->tunnels_r_l[h0 ^ h1]) {
		if (!ipv6_addr_equal(local, &t->parms.laddr) ||
		    !ipv6_addr_equal(remote, &t->parms.raddr) ||
		    key != t->parms.i_key ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (t->dev->type != ARPHRD_IP6GRE &&
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
		if (!ipv6_addr_equal(remote, &t->parms.raddr) ||
		    key != t->parms.i_key ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (t->dev->type != ARPHRD_IP6GRE &&
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
		if ((!ipv6_addr_equal(local, &t->parms.laddr) &&
			  (!ipv6_addr_equal(local, &t->parms.raddr) ||
				 !ipv6_addr_is_multicast(local))) ||
		    key != t->parms.i_key ||
		    !(t->dev->flags & IFF_UP))
			continue;

		if (t->dev->type != ARPHRD_IP6GRE &&
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

		if (t->dev->type != ARPHRD_IP6GRE &&
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

	if (cand)
		return cand;

	t = rcu_dereference(ign->collect_md_tun);
	if (t && t->dev->flags & IFF_UP)
		return t;

	dev = ign->fb_tunnel_dev;
	if (dev && dev->flags & IFF_UP)
		return netdev_priv(dev);

	return NULL;
}

static struct ip6_tnl __rcu **__ip6gre_bucket(struct ip6gre_net *ign,
		const struct __ip6_tnl_parm *p)
{
	const struct in6_addr *remote = &p->raddr;
	const struct in6_addr *local = &p->laddr;
	unsigned int h = HASH_KEY(p->i_key);
	int prio = 0;

	if (!ipv6_addr_any(local))
		prio |= 1;
	if (!ipv6_addr_any(remote) && !ipv6_addr_is_multicast(remote)) {
		prio |= 2;
		h ^= HASH_ADDR(remote);
	}

	return &ign->tunnels[prio][h];
}

static inline struct ip6_tnl __rcu **ip6gre_bucket(struct ip6gre_net *ign,
		const struct ip6_tnl *t)
{
	return __ip6gre_bucket(ign, &t->parms);
}

static void ip6gre_tunnel_link(struct ip6gre_net *ign, struct ip6_tnl *t)
{
	struct ip6_tnl __rcu **tp = ip6gre_bucket(ign, t);

	if (t->parms.collect_md)
		rcu_assign_pointer(ign->collect_md_tun, t);

	rcu_assign_pointer(t->next, rtnl_dereference(*tp));
	rcu_assign_pointer(*tp, t);
}

static void ip6gre_tunnel_unlink(struct ip6gre_net *ign, struct ip6_tnl *t)
{
	struct ip6_tnl __rcu **tp;
	struct ip6_tnl *iter;

	if (t->parms.collect_md)
		rcu_assign_pointer(ign->collect_md_tun, NULL);

	for (tp = ip6gre_bucket(ign, t);
	     (iter = rtnl_dereference(*tp)) != NULL;
	     tp = &iter->next) {
		if (t == iter) {
			rcu_assign_pointer(*tp, t->next);
			break;
		}
	}
}

static struct ip6_tnl *ip6gre_tunnel_find(struct net *net,
					   const struct __ip6_tnl_parm *parms,
					   int type)
{
	const struct in6_addr *remote = &parms->raddr;
	const struct in6_addr *local = &parms->laddr;
	__be32 key = parms->i_key;
	int link = parms->link;
	struct ip6_tnl *t;
	struct ip6_tnl __rcu **tp;
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);

	for (tp = __ip6gre_bucket(ign, parms);
	     (t = rtnl_dereference(*tp)) != NULL;
	     tp = &t->next)
		if (ipv6_addr_equal(local, &t->parms.laddr) &&
		    ipv6_addr_equal(remote, &t->parms.raddr) &&
		    key == t->parms.i_key &&
		    link == t->parms.link &&
		    type == t->dev->type)
			break;

	return t;
}

static struct ip6_tnl *ip6gre_tunnel_locate(struct net *net,
		const struct __ip6_tnl_parm *parms, int create)
{
	struct ip6_tnl *t, *nt;
	struct net_device *dev;
	char name[IFNAMSIZ];
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);

	t = ip6gre_tunnel_find(net, parms, ARPHRD_IP6GRE);
	if (t && create)
		return NULL;
	if (t || !create)
		return t;

	if (parms->name[0]) {
		if (!dev_valid_name(parms->name))
			return NULL;
		strlcpy(name, parms->name, IFNAMSIZ);
	} else {
		strcpy(name, "ip6gre%d");
	}
	dev = alloc_netdev(sizeof(*t), name, NET_NAME_UNKNOWN,
			   ip6gre_tunnel_setup);
	if (!dev)
		return NULL;

	dev_net_set(dev, net);

	nt = netdev_priv(dev);
	nt->parms = *parms;
	dev->rtnl_link_ops = &ip6gre_link_ops;

	nt->dev = dev;
	nt->net = dev_net(dev);

	if (register_netdevice(dev) < 0)
		goto failed_free;

	ip6gre_tnl_link_config(nt, 1);

	/* Can use a lockless transmit, unless we generate output sequences */
	if (!(nt->parms.o_flags & TUNNEL_SEQ))
		dev->features |= NETIF_F_LLTX;

	dev_hold(dev);
	ip6gre_tunnel_link(ign, nt);
	return nt;

failed_free:
	free_netdev(dev);
	return NULL;
}

static void ip6gre_tunnel_uninit(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct ip6gre_net *ign = net_generic(t->net, ip6gre_net_id);

	ip6gre_tunnel_unlink(ign, t);
	dst_cache_reset(&t->dst_cache);
	dev_put(dev);
}


static void ip6gre_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		       u8 type, u8 code, int offset, __be32 info)
{
	struct net *net = dev_net(skb->dev);
	const struct gre_base_hdr *greh;
	const struct ipv6hdr *ipv6h;
	int grehlen = sizeof(*greh);
	struct ip6_tnl *t;
	int key_off = 0;
	__be16 flags;
	__be32 key;

	if (!pskb_may_pull(skb, offset + grehlen))
		return;
	greh = (const struct gre_base_hdr *)(skb->data + offset);
	flags = greh->flags;
	if (flags & (GRE_VERSION | GRE_ROUTING))
		return;
	if (flags & GRE_CSUM)
		grehlen += 4;
	if (flags & GRE_KEY) {
		key_off = grehlen + offset;
		grehlen += 4;
	}

	if (!pskb_may_pull(skb, offset + grehlen))
		return;
	ipv6h = (const struct ipv6hdr *)skb->data;
	greh = (const struct gre_base_hdr *)(skb->data + offset);
	key = key_off ? *(__be32 *)(skb->data + key_off) : 0;

	t = ip6gre_tunnel_lookup(skb->dev, &ipv6h->daddr, &ipv6h->saddr,
				 key, greh->protocol);
	if (!t)
		return;

	switch (type) {
		struct ipv6_tlv_tnl_enc_lim *tel;
		__u32 teli;
	case ICMPV6_DEST_UNREACH:
		net_dbg_ratelimited("%s: Path to destination invalid or inactive!\n",
				    t->parms.name);
		if (code != ICMPV6_PORT_UNREACH)
			break;
		return;
	case ICMPV6_TIME_EXCEED:
		if (code == ICMPV6_EXC_HOPLIMIT) {
			net_dbg_ratelimited("%s: Too small hop limit or routing loop in tunnel!\n",
					    t->parms.name);
			break;
		}
		return;
	case ICMPV6_PARAMPROB:
		teli = 0;
		if (code == ICMPV6_HDR_FIELD)
			teli = ip6_tnl_parse_tlv_enc_lim(skb, skb->data);

		if (teli && teli == be32_to_cpu(info) - 2) {
			tel = (struct ipv6_tlv_tnl_enc_lim *) &skb->data[teli];
			if (tel->encap_limit == 0) {
				net_dbg_ratelimited("%s: Too small encapsulation limit or routing loop in tunnel!\n",
						    t->parms.name);
			}
		} else {
			net_dbg_ratelimited("%s: Recipient unable to parse tunneled packet!\n",
					    t->parms.name);
		}
		return;
	case ICMPV6_PKT_TOOBIG:
		ip6_update_pmtu(skb, net, info, 0, 0, sock_net_uid(net, NULL));
		return;
	case NDISC_REDIRECT:
		ip6_redirect(skb, net, skb->dev->ifindex, 0,
			     sock_net_uid(net, NULL));
		return;
	}

	if (time_before(jiffies, t->err_time + IP6TUNNEL_ERR_TIMEO))
		t->err_count++;
	else
		t->err_count = 1;
	t->err_time = jiffies;
}

static int ip6gre_rcv(struct sk_buff *skb, const struct tnl_ptk_info *tpi)
{
	const struct ipv6hdr *ipv6h;
	struct ip6_tnl *tunnel;

	ipv6h = ipv6_hdr(skb);
	tunnel = ip6gre_tunnel_lookup(skb->dev,
				      &ipv6h->saddr, &ipv6h->daddr, tpi->key,
				      tpi->proto);
	if (tunnel) {
		if (tunnel->parms.collect_md) {
			struct metadata_dst *tun_dst;
			__be64 tun_id;
			__be16 flags;

			flags = tpi->flags;
			tun_id = key32_to_tunnel_id(tpi->key);

			tun_dst = ipv6_tun_rx_dst(skb, flags, tun_id, 0);
			if (!tun_dst)
				return PACKET_REJECT;

			ip6_tnl_rcv(tunnel, skb, tpi, tun_dst, log_ecn_error);
		} else {
			ip6_tnl_rcv(tunnel, skb, tpi, NULL, log_ecn_error);
		}

		return PACKET_RCVD;
	}

	return PACKET_REJECT;
}

static int ip6erspan_rcv(struct sk_buff *skb, int gre_hdr_len,
			 struct tnl_ptk_info *tpi)
{
	struct erspan_base_hdr *ershdr;
	struct erspan_metadata *pkt_md;
	const struct ipv6hdr *ipv6h;
	struct erspan_md2 *md2;
	struct ip6_tnl *tunnel;
	u8 ver;

	if (unlikely(!pskb_may_pull(skb, sizeof(*ershdr))))
		return PACKET_REJECT;

	ipv6h = ipv6_hdr(skb);
	ershdr = (struct erspan_base_hdr *)skb->data;
	ver = ershdr->ver;
	tpi->key = cpu_to_be32(get_session_id(ershdr));

	tunnel = ip6gre_tunnel_lookup(skb->dev,
				      &ipv6h->saddr, &ipv6h->daddr, tpi->key,
				      tpi->proto);
	if (tunnel) {
		int len = erspan_hdr_len(ver);

		if (unlikely(!pskb_may_pull(skb, len)))
			return PACKET_REJECT;

		ershdr = (struct erspan_base_hdr *)skb->data;
		pkt_md = (struct erspan_metadata *)(ershdr + 1);

		if (__iptunnel_pull_header(skb, len,
					   htons(ETH_P_TEB),
					   false, false) < 0)
			return PACKET_REJECT;

		if (tunnel->parms.collect_md) {
			struct metadata_dst *tun_dst;
			struct ip_tunnel_info *info;
			struct erspan_metadata *md;
			__be64 tun_id;
			__be16 flags;

			tpi->flags |= TUNNEL_KEY;
			flags = tpi->flags;
			tun_id = key32_to_tunnel_id(tpi->key);

			tun_dst = ipv6_tun_rx_dst(skb, flags, tun_id,
						  sizeof(*md));
			if (!tun_dst)
				return PACKET_REJECT;

			info = &tun_dst->u.tun_info;
			md = ip_tunnel_info_opts(info);
			md->version = ver;
			md2 = &md->u.md2;
			memcpy(md2, pkt_md, ver == 1 ? ERSPAN_V1_MDSIZE :
						       ERSPAN_V2_MDSIZE);
			info->key.tun_flags |= TUNNEL_ERSPAN_OPT;
			info->options_len = sizeof(*md);

			ip6_tnl_rcv(tunnel, skb, tpi, tun_dst, log_ecn_error);

		} else {
			ip6_tnl_rcv(tunnel, skb, tpi, NULL, log_ecn_error);
		}

		return PACKET_RCVD;
	}

	return PACKET_REJECT;
}

static int gre_rcv(struct sk_buff *skb)
{
	struct tnl_ptk_info tpi;
	bool csum_err = false;
	int hdr_len;

	hdr_len = gre_parse_header(skb, &tpi, &csum_err, htons(ETH_P_IPV6), 0);
	if (hdr_len < 0)
		goto drop;

	if (iptunnel_pull_header(skb, hdr_len, tpi.proto, false))
		goto drop;

	if (unlikely(tpi.proto == htons(ETH_P_ERSPAN) ||
		     tpi.proto == htons(ETH_P_ERSPAN2))) {
		if (ip6erspan_rcv(skb, hdr_len, &tpi) == PACKET_RCVD)
			return 0;
		goto out;
	}

	if (ip6gre_rcv(skb, &tpi) == PACKET_RCVD)
		return 0;

out:
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);
drop:
	kfree_skb(skb);
	return 0;
}

static int gre_handle_offloads(struct sk_buff *skb, bool csum)
{
	return iptunnel_handle_offloads(skb,
					csum ? SKB_GSO_GRE_CSUM : SKB_GSO_GRE);
}

static void prepare_ip6gre_xmit_ipv4(struct sk_buff *skb,
				     struct net_device *dev,
				     struct flowi6 *fl6, __u8 *dsfield,
				     int *encap_limit)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct ip6_tnl *t = netdev_priv(dev);

	if (!(t->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT))
		*encap_limit = t->parms.encap_limit;

	memcpy(fl6, &t->fl.u.ip6, sizeof(*fl6));

	if (t->parms.flags & IP6_TNL_F_USE_ORIG_TCLASS)
		*dsfield = ipv4_get_dsfield(iph);
	else
		*dsfield = ip6_tclass(t->parms.flowinfo);

	if (t->parms.flags & IP6_TNL_F_USE_ORIG_FWMARK)
		fl6->flowi6_mark = skb->mark;
	else
		fl6->flowi6_mark = t->parms.fwmark;

	fl6->flowi6_uid = sock_net_uid(dev_net(dev), NULL);
}

static int prepare_ip6gre_xmit_ipv6(struct sk_buff *skb,
				    struct net_device *dev,
				    struct flowi6 *fl6, __u8 *dsfield,
				    int *encap_limit)
{
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct ip6_tnl *t = netdev_priv(dev);
	__u16 offset;

	offset = ip6_tnl_parse_tlv_enc_lim(skb, skb_network_header(skb));
	/* ip6_tnl_parse_tlv_enc_lim() might have reallocated skb->head */

	if (offset > 0) {
		struct ipv6_tlv_tnl_enc_lim *tel;

		tel = (struct ipv6_tlv_tnl_enc_lim *)&skb_network_header(skb)[offset];
		if (tel->encap_limit == 0) {
			icmpv6_send(skb, ICMPV6_PARAMPROB,
				    ICMPV6_HDR_FIELD, offset + 2);
			return -1;
		}
		*encap_limit = tel->encap_limit - 1;
	} else if (!(t->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT)) {
		*encap_limit = t->parms.encap_limit;
	}

	memcpy(fl6, &t->fl.u.ip6, sizeof(*fl6));

	if (t->parms.flags & IP6_TNL_F_USE_ORIG_TCLASS)
		*dsfield = ipv6_get_dsfield(ipv6h);
	else
		*dsfield = ip6_tclass(t->parms.flowinfo);

	if (t->parms.flags & IP6_TNL_F_USE_ORIG_FLOWLABEL)
		fl6->flowlabel |= ip6_flowlabel(ipv6h);

	if (t->parms.flags & IP6_TNL_F_USE_ORIG_FWMARK)
		fl6->flowi6_mark = skb->mark;
	else
		fl6->flowi6_mark = t->parms.fwmark;

	fl6->flowi6_uid = sock_net_uid(dev_net(dev), NULL);

	return 0;
}

static netdev_tx_t __gre6_xmit(struct sk_buff *skb,
			       struct net_device *dev, __u8 dsfield,
			       struct flowi6 *fl6, int encap_limit,
			       __u32 *pmtu, __be16 proto)
{
	struct ip6_tnl *tunnel = netdev_priv(dev);
	__be16 protocol;

	if (dev->type == ARPHRD_ETHER)
		IPCB(skb)->flags = 0;

	if (dev->header_ops && dev->type == ARPHRD_IP6GRE)
		fl6->daddr = ((struct ipv6hdr *)skb->data)->daddr;
	else
		fl6->daddr = tunnel->parms.raddr;

	/* Push GRE header. */
	protocol = (dev->type == ARPHRD_ETHER) ? htons(ETH_P_TEB) : proto;

	if (tunnel->parms.collect_md) {
		struct ip_tunnel_info *tun_info;
		const struct ip_tunnel_key *key;
		__be16 flags;

		tun_info = skb_tunnel_info(skb);
		if (unlikely(!tun_info ||
			     !(tun_info->mode & IP_TUNNEL_INFO_TX) ||
			     ip_tunnel_info_af(tun_info) != AF_INET6))
			return -EINVAL;

		key = &tun_info->key;
		memset(fl6, 0, sizeof(*fl6));
		fl6->flowi6_proto = IPPROTO_GRE;
		fl6->daddr = key->u.ipv6.dst;
		fl6->flowlabel = key->label;
		fl6->flowi6_uid = sock_net_uid(dev_net(dev), NULL);

		dsfield = key->tos;
		flags = key->tun_flags &
			(TUNNEL_CSUM | TUNNEL_KEY | TUNNEL_SEQ);
		tunnel->tun_hlen = gre_calc_hlen(flags);

		gre_build_header(skb, tunnel->tun_hlen,
				 flags, protocol,
				 tunnel_id_to_key32(tun_info->key.tun_id),
				 (flags & TUNNEL_SEQ) ? htonl(tunnel->o_seqno++)
						      : 0);

	} else {
		if (tunnel->parms.o_flags & TUNNEL_SEQ)
			tunnel->o_seqno++;

		gre_build_header(skb, tunnel->tun_hlen, tunnel->parms.o_flags,
				 protocol, tunnel->parms.o_key,
				 htonl(tunnel->o_seqno));
	}

	return ip6_tnl_xmit(skb, dev, dsfield, fl6, encap_limit, pmtu,
			    NEXTHDR_GRE);
}

static inline int ip6gre_xmit_ipv4(struct sk_buff *skb, struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	int encap_limit = -1;
	struct flowi6 fl6;
	__u8 dsfield = 0;
	__u32 mtu;
	int err;

	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	if (!t->parms.collect_md)
		prepare_ip6gre_xmit_ipv4(skb, dev, &fl6,
					 &dsfield, &encap_limit);

	err = gre_handle_offloads(skb, !!(t->parms.o_flags & TUNNEL_CSUM));
	if (err)
		return -1;

	err = __gre6_xmit(skb, dev, dsfield, &fl6, encap_limit, &mtu,
			  skb->protocol);
	if (err != 0) {
		/* XXX: send ICMP error even if DF is not set. */
		if (err == -EMSGSIZE)
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
				  htonl(mtu));
		return -1;
	}

	return 0;
}

static inline int ip6gre_xmit_ipv6(struct sk_buff *skb, struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	int encap_limit = -1;
	struct flowi6 fl6;
	__u8 dsfield = 0;
	__u32 mtu;
	int err;

	if (ipv6_addr_equal(&t->parms.raddr, &ipv6h->saddr))
		return -1;

	if (!t->parms.collect_md &&
	    prepare_ip6gre_xmit_ipv6(skb, dev, &fl6, &dsfield, &encap_limit))
		return -1;

	if (gre_handle_offloads(skb, !!(t->parms.o_flags & TUNNEL_CSUM)))
		return -1;

	err = __gre6_xmit(skb, dev, dsfield, &fl6, encap_limit,
			  &mtu, skb->protocol);
	if (err != 0) {
		if (err == -EMSGSIZE)
			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		return -1;
	}

	return 0;
}

/**
 * ip6_tnl_addr_conflict - compare packet addresses to tunnel's own
 *   @t: the outgoing tunnel device
 *   @hdr: IPv6 header from the incoming packet
 *
 * Description:
 *   Avoid trivial tunneling loop by checking that tunnel exit-point
 *   doesn't match source of incoming packet.
 *
 * Return:
 *   1 if conflict,
 *   0 else
 **/

static inline bool ip6gre_tnl_addr_conflict(const struct ip6_tnl *t,
	const struct ipv6hdr *hdr)
{
	return ipv6_addr_equal(&t->parms.raddr, &hdr->saddr);
}

static int ip6gre_xmit_other(struct sk_buff *skb, struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	int encap_limit = -1;
	struct flowi6 fl6;
	__u32 mtu;
	int err;

	if (!(t->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT))
		encap_limit = t->parms.encap_limit;

	if (!t->parms.collect_md)
		memcpy(&fl6, &t->fl.u.ip6, sizeof(fl6));

	err = gre_handle_offloads(skb, !!(t->parms.o_flags & TUNNEL_CSUM));
	if (err)
		return err;

	err = __gre6_xmit(skb, dev, 0, &fl6, encap_limit, &mtu, skb->protocol);

	return err;
}

static netdev_tx_t ip6gre_tunnel_xmit(struct sk_buff *skb,
	struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net_device_stats *stats = &t->dev->stats;
	int ret;

	if (!ip6_tnl_xmit_ctl(t, &t->parms.laddr, &t->parms.raddr))
		goto tx_err;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		ret = ip6gre_xmit_ipv4(skb, dev);
		break;
	case htons(ETH_P_IPV6):
		ret = ip6gre_xmit_ipv6(skb, dev);
		break;
	default:
		ret = ip6gre_xmit_other(skb, dev);
		break;
	}

	if (ret < 0)
		goto tx_err;

	return NETDEV_TX_OK;

tx_err:
	stats->tx_errors++;
	stats->tx_dropped++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static netdev_tx_t ip6erspan_tunnel_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct ip6_tnl *t = netdev_priv(dev);
	struct dst_entry *dst = skb_dst(skb);
	struct net_device_stats *stats;
	bool truncate = false;
	int encap_limit = -1;
	__u8 dsfield = false;
	struct flowi6 fl6;
	int err = -EINVAL;
	__u32 mtu;

	if (!ip6_tnl_xmit_ctl(t, &t->parms.laddr, &t->parms.raddr))
		goto tx_err;

	if (gre_handle_offloads(skb, false))
		goto tx_err;

	if (skb->len > dev->mtu + dev->hard_header_len) {
		pskb_trim(skb, dev->mtu + dev->hard_header_len);
		truncate = true;
	}

	if (skb_cow_head(skb, dev->needed_headroom))
		goto tx_err;

	t->parms.o_flags &= ~TUNNEL_KEY;
	IPCB(skb)->flags = 0;

	/* For collect_md mode, derive fl6 from the tunnel key,
	 * for native mode, call prepare_ip6gre_xmit_{ipv4,ipv6}.
	 */
	if (t->parms.collect_md) {
		struct ip_tunnel_info *tun_info;
		const struct ip_tunnel_key *key;
		struct erspan_metadata *md;
		__be32 tun_id;

		tun_info = skb_tunnel_info(skb);
		if (unlikely(!tun_info ||
			     !(tun_info->mode & IP_TUNNEL_INFO_TX) ||
			     ip_tunnel_info_af(tun_info) != AF_INET6))
			return -EINVAL;

		key = &tun_info->key;
		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_proto = IPPROTO_GRE;
		fl6.daddr = key->u.ipv6.dst;
		fl6.flowlabel = key->label;
		fl6.flowi6_uid = sock_net_uid(dev_net(dev), NULL);

		dsfield = key->tos;
		md = ip_tunnel_info_opts(tun_info);
		if (!md)
			goto tx_err;

		tun_id = tunnel_id_to_key32(key->tun_id);
		if (md->version == 1) {
			erspan_build_header(skb,
					    ntohl(tun_id),
					    ntohl(md->u.index), truncate,
					    false);
		} else if (md->version == 2) {
			erspan_build_header_v2(skb,
					       ntohl(tun_id),
					       md->u.md2.dir,
					       get_hwid(&md->u.md2),
					       truncate, false);
		} else {
			goto tx_err;
		}
	} else {
		switch (skb->protocol) {
		case htons(ETH_P_IP):
			memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
			prepare_ip6gre_xmit_ipv4(skb, dev, &fl6,
						 &dsfield, &encap_limit);
			break;
		case htons(ETH_P_IPV6):
			if (ipv6_addr_equal(&t->parms.raddr, &ipv6h->saddr))
				goto tx_err;
			if (prepare_ip6gre_xmit_ipv6(skb, dev, &fl6,
						     &dsfield, &encap_limit))
				goto tx_err;
			break;
		default:
			memcpy(&fl6, &t->fl.u.ip6, sizeof(fl6));
			break;
		}

		if (t->parms.erspan_ver == 1)
			erspan_build_header(skb, ntohl(t->parms.o_key),
					    t->parms.index,
					    truncate, false);
		else
			erspan_build_header_v2(skb, ntohl(t->parms.o_key),
					       t->parms.dir,
					       t->parms.hwid,
					       truncate, false);
		fl6.daddr = t->parms.raddr;
	}

	/* Push GRE header. */
	gre_build_header(skb, 8, TUNNEL_SEQ,
			 htons(ETH_P_ERSPAN), 0, htonl(t->o_seqno++));

	/* TooBig packet may have updated dst->dev's mtu */
	if (!t->parms.collect_md && dst && dst_mtu(dst) > dst->dev->mtu)
		dst->ops->update_pmtu(dst, NULL, skb, dst->dev->mtu);

	err = ip6_tnl_xmit(skb, dev, dsfield, &fl6, encap_limit, &mtu,
			   NEXTHDR_GRE);
	if (err != 0) {
		/* XXX: send ICMP error even if DF is not set. */
		if (err == -EMSGSIZE) {
			if (skb->protocol == htons(ETH_P_IP))
				icmp_send(skb, ICMP_DEST_UNREACH,
					  ICMP_FRAG_NEEDED, htonl(mtu));
			else
				icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		}

		goto tx_err;
	}
	return NETDEV_TX_OK;

tx_err:
	stats = &t->dev->stats;
	stats->tx_errors++;
	stats->tx_dropped++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void ip6gre_tnl_link_config(struct ip6_tnl *t, int set_mtu)
{
	struct net_device *dev = t->dev;
	struct __ip6_tnl_parm *p = &t->parms;
	struct flowi6 *fl6 = &t->fl.u.ip6;
	int t_hlen;

	if (dev->type != ARPHRD_ETHER) {
		memcpy(dev->dev_addr, &p->laddr, sizeof(struct in6_addr));
		memcpy(dev->broadcast, &p->raddr, sizeof(struct in6_addr));
	}

	/* Set up flowi template */
	fl6->saddr = p->laddr;
	fl6->daddr = p->raddr;
	fl6->flowi6_oif = p->link;
	fl6->flowlabel = 0;
	fl6->flowi6_proto = IPPROTO_GRE;

	if (!(p->flags&IP6_TNL_F_USE_ORIG_TCLASS))
		fl6->flowlabel |= IPV6_TCLASS_MASK & p->flowinfo;
	if (!(p->flags&IP6_TNL_F_USE_ORIG_FLOWLABEL))
		fl6->flowlabel |= IPV6_FLOWLABEL_MASK & p->flowinfo;

	p->flags &= ~(IP6_TNL_F_CAP_XMIT|IP6_TNL_F_CAP_RCV|IP6_TNL_F_CAP_PER_PACKET);
	p->flags |= ip6_tnl_get_cap(t, &p->laddr, &p->raddr);

	if (p->flags&IP6_TNL_F_CAP_XMIT &&
			p->flags&IP6_TNL_F_CAP_RCV && dev->type != ARPHRD_ETHER)
		dev->flags |= IFF_POINTOPOINT;
	else
		dev->flags &= ~IFF_POINTOPOINT;

	t->tun_hlen = gre_calc_hlen(t->parms.o_flags);

	t->hlen = t->encap_hlen + t->tun_hlen;

	t_hlen = t->hlen + sizeof(struct ipv6hdr);

	if (p->flags & IP6_TNL_F_CAP_XMIT) {
		int strict = (ipv6_addr_type(&p->raddr) &
			      (IPV6_ADDR_MULTICAST|IPV6_ADDR_LINKLOCAL));

		struct rt6_info *rt = rt6_lookup(t->net,
						 &p->raddr, &p->laddr,
						 p->link, NULL, strict);

		if (!rt)
			return;

		if (rt->dst.dev) {
			dev->hard_header_len = rt->dst.dev->hard_header_len +
					       t_hlen;

			if (set_mtu) {
				dev->mtu = rt->dst.dev->mtu - t_hlen;
				if (!(t->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT))
					dev->mtu -= 8;
				if (dev->type == ARPHRD_ETHER)
					dev->mtu -= ETH_HLEN;

				if (dev->mtu < IPV6_MIN_MTU)
					dev->mtu = IPV6_MIN_MTU;
			}
		}
		ip6_rt_put(rt);
	}
}

static int ip6gre_tnl_change(struct ip6_tnl *t,
	const struct __ip6_tnl_parm *p, int set_mtu)
{
	t->parms.laddr = p->laddr;
	t->parms.raddr = p->raddr;
	t->parms.flags = p->flags;
	t->parms.hop_limit = p->hop_limit;
	t->parms.encap_limit = p->encap_limit;
	t->parms.flowinfo = p->flowinfo;
	t->parms.link = p->link;
	t->parms.proto = p->proto;
	t->parms.i_key = p->i_key;
	t->parms.o_key = p->o_key;
	t->parms.i_flags = p->i_flags;
	t->parms.o_flags = p->o_flags;
	t->parms.fwmark = p->fwmark;
	dst_cache_reset(&t->dst_cache);
	ip6gre_tnl_link_config(t, set_mtu);
	return 0;
}

static void ip6gre_tnl_parm_from_user(struct __ip6_tnl_parm *p,
	const struct ip6_tnl_parm2 *u)
{
	p->laddr = u->laddr;
	p->raddr = u->raddr;
	p->flags = u->flags;
	p->hop_limit = u->hop_limit;
	p->encap_limit = u->encap_limit;
	p->flowinfo = u->flowinfo;
	p->link = u->link;
	p->i_key = u->i_key;
	p->o_key = u->o_key;
	p->i_flags = gre_flags_to_tnl_flags(u->i_flags);
	p->o_flags = gre_flags_to_tnl_flags(u->o_flags);
	memcpy(p->name, u->name, sizeof(u->name));
}

static void ip6gre_tnl_parm_to_user(struct ip6_tnl_parm2 *u,
	const struct __ip6_tnl_parm *p)
{
	u->proto = IPPROTO_GRE;
	u->laddr = p->laddr;
	u->raddr = p->raddr;
	u->flags = p->flags;
	u->hop_limit = p->hop_limit;
	u->encap_limit = p->encap_limit;
	u->flowinfo = p->flowinfo;
	u->link = p->link;
	u->i_key = p->i_key;
	u->o_key = p->o_key;
	u->i_flags = gre_tnl_flags_to_gre_flags(p->i_flags);
	u->o_flags = gre_tnl_flags_to_gre_flags(p->o_flags);
	memcpy(u->name, p->name, sizeof(u->name));
}

static int ip6gre_tunnel_ioctl(struct net_device *dev,
	struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip6_tnl_parm2 p;
	struct __ip6_tnl_parm p1;
	struct ip6_tnl *t = netdev_priv(dev);
	struct net *net = t->net;
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);

	memset(&p1, 0, sizeof(p1));

	switch (cmd) {
	case SIOCGETTUNNEL:
		if (dev == ign->fb_tunnel_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p))) {
				err = -EFAULT;
				break;
			}
			ip6gre_tnl_parm_from_user(&p1, &p);
			t = ip6gre_tunnel_locate(net, &p1, 0);
			if (!t)
				t = netdev_priv(dev);
		}
		memset(&p, 0, sizeof(p));
		ip6gre_tnl_parm_to_user(&p, &t->parms);
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
		if ((p.i_flags|p.o_flags)&(GRE_VERSION|GRE_ROUTING))
			goto done;

		if (!(p.i_flags&GRE_KEY))
			p.i_key = 0;
		if (!(p.o_flags&GRE_KEY))
			p.o_key = 0;

		ip6gre_tnl_parm_from_user(&p1, &p);
		t = ip6gre_tunnel_locate(net, &p1, cmd == SIOCADDTUNNEL);

		if (dev != ign->fb_tunnel_dev && cmd == SIOCCHGTUNNEL) {
			if (t) {
				if (t->dev != dev) {
					err = -EEXIST;
					break;
				}
			} else {
				t = netdev_priv(dev);

				ip6gre_tunnel_unlink(ign, t);
				synchronize_net();
				ip6gre_tnl_change(t, &p1, 1);
				ip6gre_tunnel_link(ign, t);
				netdev_state_change(dev);
			}
		}

		if (t) {
			err = 0;

			memset(&p, 0, sizeof(p));
			ip6gre_tnl_parm_to_user(&p, &t->parms);
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
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
			ip6gre_tnl_parm_from_user(&p1, &p);
			t = ip6gre_tunnel_locate(net, &p1, 0);
			if (!t)
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

static int ip6gre_header(struct sk_buff *skb, struct net_device *dev,
			 unsigned short type, const void *daddr,
			 const void *saddr, unsigned int len)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct ipv6hdr *ipv6h;
	__be16 *p;

	ipv6h = skb_push(skb, t->hlen + sizeof(*ipv6h));
	ip6_flow_hdr(ipv6h, 0, ip6_make_flowlabel(dev_net(dev), skb,
						  t->fl.u.ip6.flowlabel,
						  true, &t->fl.u.ip6));
	ipv6h->hop_limit = t->parms.hop_limit;
	ipv6h->nexthdr = NEXTHDR_GRE;
	ipv6h->saddr = t->parms.laddr;
	ipv6h->daddr = t->parms.raddr;

	p = (__be16 *)(ipv6h + 1);
	p[0] = t->parms.o_flags;
	p[1] = htons(type);

	/*
	 *	Set the source hardware address.
	 */

	if (saddr)
		memcpy(&ipv6h->saddr, saddr, sizeof(struct in6_addr));
	if (daddr)
		memcpy(&ipv6h->daddr, daddr, sizeof(struct in6_addr));
	if (!ipv6_addr_any(&ipv6h->daddr))
		return t->hlen;

	return -t->hlen;
}

static const struct header_ops ip6gre_header_ops = {
	.create	= ip6gre_header,
};

static const struct net_device_ops ip6gre_netdev_ops = {
	.ndo_init		= ip6gre_tunnel_init,
	.ndo_uninit		= ip6gre_tunnel_uninit,
	.ndo_start_xmit		= ip6gre_tunnel_xmit,
	.ndo_do_ioctl		= ip6gre_tunnel_ioctl,
	.ndo_change_mtu		= ip6_tnl_change_mtu,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
	.ndo_get_iflink		= ip6_tnl_get_iflink,
};

static void ip6gre_dev_free(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);

	dst_cache_destroy(&t->dst_cache);
	free_percpu(dev->tstats);
}

static void ip6gre_tunnel_setup(struct net_device *dev)
{
	dev->netdev_ops = &ip6gre_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = ip6gre_dev_free;

	dev->type = ARPHRD_IP6GRE;

	dev->flags |= IFF_NOARP;
	dev->addr_len = sizeof(struct in6_addr);
	netif_keep_dst(dev);
	/* This perm addr will be used as interface identifier by IPv6 */
	dev->addr_assign_type = NET_ADDR_RANDOM;
	eth_random_addr(dev->perm_addr);
}

#define GRE6_FEATURES (NETIF_F_SG |		\
		       NETIF_F_FRAGLIST |	\
		       NETIF_F_HIGHDMA |	\
		       NETIF_F_HW_CSUM)

static void ip6gre_tnl_init_features(struct net_device *dev)
{
	struct ip6_tnl *nt = netdev_priv(dev);

	dev->features		|= GRE6_FEATURES;
	dev->hw_features	|= GRE6_FEATURES;

	if (!(nt->parms.o_flags & TUNNEL_SEQ)) {
		/* TCP offload with GRE SEQ is not supported, nor
		 * can we support 2 levels of outer headers requiring
		 * an update.
		 */
		if (!(nt->parms.o_flags & TUNNEL_CSUM) ||
		    nt->encap.type == TUNNEL_ENCAP_NONE) {
			dev->features    |= NETIF_F_GSO_SOFTWARE;
			dev->hw_features |= NETIF_F_GSO_SOFTWARE;
		}

		/* Can use a lockless transmit, unless we generate
		 * output sequences
		 */
		dev->features |= NETIF_F_LLTX;
	}
}

static int ip6gre_tunnel_init_common(struct net_device *dev)
{
	struct ip6_tnl *tunnel;
	int ret;
	int t_hlen;

	tunnel = netdev_priv(dev);

	tunnel->dev = dev;
	tunnel->net = dev_net(dev);
	strcpy(tunnel->parms.name, dev->name);

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	ret = dst_cache_init(&tunnel->dst_cache, GFP_KERNEL);
	if (ret) {
		free_percpu(dev->tstats);
		dev->tstats = NULL;
		return ret;
	}

	tunnel->tun_hlen = gre_calc_hlen(tunnel->parms.o_flags);
	tunnel->hlen = tunnel->tun_hlen + tunnel->encap_hlen;
	t_hlen = tunnel->hlen + sizeof(struct ipv6hdr);

	dev->hard_header_len = LL_MAX_HEADER + t_hlen;
	dev->mtu = ETH_DATA_LEN - t_hlen;
	if (dev->type == ARPHRD_ETHER)
		dev->mtu -= ETH_HLEN;
	if (!(tunnel->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT))
		dev->mtu -= 8;

	if (tunnel->parms.collect_md) {
		dev->features |= NETIF_F_NETNS_LOCAL;
		netif_keep_dst(dev);
	}
	ip6gre_tnl_init_features(dev);

	return 0;
}

static int ip6gre_tunnel_init(struct net_device *dev)
{
	struct ip6_tnl *tunnel;
	int ret;

	ret = ip6gre_tunnel_init_common(dev);
	if (ret)
		return ret;

	tunnel = netdev_priv(dev);

	if (tunnel->parms.collect_md)
		return 0;

	memcpy(dev->dev_addr, &tunnel->parms.laddr, sizeof(struct in6_addr));
	memcpy(dev->broadcast, &tunnel->parms.raddr, sizeof(struct in6_addr));

	if (ipv6_addr_any(&tunnel->parms.raddr))
		dev->header_ops = &ip6gre_header_ops;

	return 0;
}

static void ip6gre_fb_tunnel_init(struct net_device *dev)
{
	struct ip6_tnl *tunnel = netdev_priv(dev);

	tunnel->dev = dev;
	tunnel->net = dev_net(dev);
	strcpy(tunnel->parms.name, dev->name);

	tunnel->hlen		= sizeof(struct ipv6hdr) + 4;

	dev_hold(dev);
}

static struct inet6_protocol ip6gre_protocol __read_mostly = {
	.handler     = gre_rcv,
	.err_handler = ip6gre_err,
	.flags       = INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static void ip6gre_destroy_tunnels(struct net *net, struct list_head *head)
{
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);
	struct net_device *dev, *aux;
	int prio;

	for_each_netdev_safe(net, dev, aux)
		if (dev->rtnl_link_ops == &ip6gre_link_ops ||
		    dev->rtnl_link_ops == &ip6gre_tap_ops ||
		    dev->rtnl_link_ops == &ip6erspan_tap_ops)
			unregister_netdevice_queue(dev, head);

	for (prio = 0; prio < 4; prio++) {
		int h;
		for (h = 0; h < IP6_GRE_HASH_SIZE; h++) {
			struct ip6_tnl *t;

			t = rtnl_dereference(ign->tunnels[prio][h]);

			while (t) {
				/* If dev is in the same netns, it has already
				 * been added to the list by the previous loop.
				 */
				if (!net_eq(dev_net(t->dev), net))
					unregister_netdevice_queue(t->dev,
								   head);
				t = rtnl_dereference(t->next);
			}
		}
	}
}

static int __net_init ip6gre_init_net(struct net *net)
{
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);
	int err;

	if (!net_has_fallback_tunnels(net))
		return 0;
	ign->fb_tunnel_dev = alloc_netdev(sizeof(struct ip6_tnl), "ip6gre0",
					  NET_NAME_UNKNOWN,
					  ip6gre_tunnel_setup);
	if (!ign->fb_tunnel_dev) {
		err = -ENOMEM;
		goto err_alloc_dev;
	}
	dev_net_set(ign->fb_tunnel_dev, net);
	/* FB netdevice is special: we have one, and only one per netns.
	 * Allowing to move it to another netns is clearly unsafe.
	 */
	ign->fb_tunnel_dev->features |= NETIF_F_NETNS_LOCAL;


	ip6gre_fb_tunnel_init(ign->fb_tunnel_dev);
	ign->fb_tunnel_dev->rtnl_link_ops = &ip6gre_link_ops;

	err = register_netdev(ign->fb_tunnel_dev);
	if (err)
		goto err_reg_dev;

	rcu_assign_pointer(ign->tunnels_wc[0],
			   netdev_priv(ign->fb_tunnel_dev));
	return 0;

err_reg_dev:
	free_netdev(ign->fb_tunnel_dev);
err_alloc_dev:
	return err;
}

static void __net_exit ip6gre_exit_batch_net(struct list_head *net_list)
{
	struct net *net;
	LIST_HEAD(list);

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list)
		ip6gre_destroy_tunnels(net, &list);
	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations ip6gre_net_ops = {
	.init = ip6gre_init_net,
	.exit_batch = ip6gre_exit_batch_net,
	.id   = &ip6gre_net_id,
	.size = sizeof(struct ip6gre_net),
};

static int ip6gre_tunnel_validate(struct nlattr *tb[], struct nlattr *data[],
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

	return 0;
}

static int ip6gre_tap_validate(struct nlattr *tb[], struct nlattr *data[],
			       struct netlink_ext_ack *extack)
{
	struct in6_addr daddr;

	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (!data)
		goto out;

	if (data[IFLA_GRE_REMOTE]) {
		daddr = nla_get_in6_addr(data[IFLA_GRE_REMOTE]);
		if (ipv6_addr_any(&daddr))
			return -EINVAL;
	}

out:
	return ip6gre_tunnel_validate(tb, data, extack);
}

static int ip6erspan_tap_validate(struct nlattr *tb[], struct nlattr *data[],
				  struct netlink_ext_ack *extack)
{
	__be16 flags = 0;
	int ret, ver = 0;

	if (!data)
		return 0;

	ret = ip6gre_tap_validate(tb, data, extack);
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

	if (data[IFLA_GRE_ERSPAN_VER]) {
		ver = nla_get_u8(data[IFLA_GRE_ERSPAN_VER]);
		if (ver != 1 && ver != 2)
			return -EINVAL;
	}

	if (ver == 1) {
		if (data[IFLA_GRE_ERSPAN_INDEX]) {
			u32 index = nla_get_u32(data[IFLA_GRE_ERSPAN_INDEX]);

			if (index & ~INDEX_MASK)
				return -EINVAL;
		}
	} else if (ver == 2) {
		if (data[IFLA_GRE_ERSPAN_DIR]) {
			u16 dir = nla_get_u8(data[IFLA_GRE_ERSPAN_DIR]);

			if (dir & ~(DIR_MASK >> DIR_OFFSET))
				return -EINVAL;
		}

		if (data[IFLA_GRE_ERSPAN_HWID]) {
			u16 hwid = nla_get_u16(data[IFLA_GRE_ERSPAN_HWID]);

			if (hwid & ~(HWID_MASK >> HWID_OFFSET))
				return -EINVAL;
		}
	}

	return 0;
}

static void ip6gre_netlink_parms(struct nlattr *data[],
				struct __ip6_tnl_parm *parms)
{
	memset(parms, 0, sizeof(*parms));

	if (!data)
		return;

	if (data[IFLA_GRE_LINK])
		parms->link = nla_get_u32(data[IFLA_GRE_LINK]);

	if (data[IFLA_GRE_IFLAGS])
		parms->i_flags = gre_flags_to_tnl_flags(
				nla_get_be16(data[IFLA_GRE_IFLAGS]));

	if (data[IFLA_GRE_OFLAGS])
		parms->o_flags = gre_flags_to_tnl_flags(
				nla_get_be16(data[IFLA_GRE_OFLAGS]));

	if (data[IFLA_GRE_IKEY])
		parms->i_key = nla_get_be32(data[IFLA_GRE_IKEY]);

	if (data[IFLA_GRE_OKEY])
		parms->o_key = nla_get_be32(data[IFLA_GRE_OKEY]);

	if (data[IFLA_GRE_LOCAL])
		parms->laddr = nla_get_in6_addr(data[IFLA_GRE_LOCAL]);

	if (data[IFLA_GRE_REMOTE])
		parms->raddr = nla_get_in6_addr(data[IFLA_GRE_REMOTE]);

	if (data[IFLA_GRE_TTL])
		parms->hop_limit = nla_get_u8(data[IFLA_GRE_TTL]);

	if (data[IFLA_GRE_ENCAP_LIMIT])
		parms->encap_limit = nla_get_u8(data[IFLA_GRE_ENCAP_LIMIT]);

	if (data[IFLA_GRE_FLOWINFO])
		parms->flowinfo = nla_get_be32(data[IFLA_GRE_FLOWINFO]);

	if (data[IFLA_GRE_FLAGS])
		parms->flags = nla_get_u32(data[IFLA_GRE_FLAGS]);

	if (data[IFLA_GRE_FWMARK])
		parms->fwmark = nla_get_u32(data[IFLA_GRE_FWMARK]);

	if (data[IFLA_GRE_COLLECT_METADATA])
		parms->collect_md = true;

	if (data[IFLA_GRE_ERSPAN_VER])
		parms->erspan_ver = nla_get_u8(data[IFLA_GRE_ERSPAN_VER]);

	if (parms->erspan_ver == 1) {
		if (data[IFLA_GRE_ERSPAN_INDEX])
			parms->index = nla_get_u32(data[IFLA_GRE_ERSPAN_INDEX]);
	} else if (parms->erspan_ver == 2) {
		if (data[IFLA_GRE_ERSPAN_DIR])
			parms->dir = nla_get_u8(data[IFLA_GRE_ERSPAN_DIR]);
		if (data[IFLA_GRE_ERSPAN_HWID])
			parms->hwid = nla_get_u16(data[IFLA_GRE_ERSPAN_HWID]);
	}
}

static int ip6gre_tap_init(struct net_device *dev)
{
	int ret;

	ret = ip6gre_tunnel_init_common(dev);
	if (ret)
		return ret;

	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	return 0;
}

static const struct net_device_ops ip6gre_tap_netdev_ops = {
	.ndo_init = ip6gre_tap_init,
	.ndo_uninit = ip6gre_tunnel_uninit,
	.ndo_start_xmit = ip6gre_tunnel_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = ip6_tnl_change_mtu,
	.ndo_get_stats64 = ip_tunnel_get_stats64,
	.ndo_get_iflink = ip6_tnl_get_iflink,
};

static int ip6erspan_tap_init(struct net_device *dev)
{
	struct ip6_tnl *tunnel;
	int t_hlen;
	int ret;

	tunnel = netdev_priv(dev);

	tunnel->dev = dev;
	tunnel->net = dev_net(dev);
	strcpy(tunnel->parms.name, dev->name);

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	ret = dst_cache_init(&tunnel->dst_cache, GFP_KERNEL);
	if (ret) {
		free_percpu(dev->tstats);
		dev->tstats = NULL;
		return ret;
	}

	tunnel->tun_hlen = 8;
	tunnel->hlen = tunnel->tun_hlen + tunnel->encap_hlen +
		       erspan_hdr_len(tunnel->parms.erspan_ver);
	t_hlen = tunnel->hlen + sizeof(struct ipv6hdr);

	dev->hard_header_len = LL_MAX_HEADER + t_hlen;
	dev->mtu = ETH_DATA_LEN - t_hlen;
	if (dev->type == ARPHRD_ETHER)
		dev->mtu -= ETH_HLEN;
	if (!(tunnel->parms.flags & IP6_TNL_F_IGN_ENCAP_LIMIT))
		dev->mtu -= 8;

	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	ip6gre_tnl_link_config(tunnel, 1);

	return 0;
}

static const struct net_device_ops ip6erspan_netdev_ops = {
	.ndo_init =		ip6erspan_tap_init,
	.ndo_uninit =		ip6gre_tunnel_uninit,
	.ndo_start_xmit =	ip6erspan_tunnel_xmit,
	.ndo_set_mac_address =	eth_mac_addr,
	.ndo_validate_addr =	eth_validate_addr,
	.ndo_change_mtu =	ip6_tnl_change_mtu,
	.ndo_get_stats64 =	ip_tunnel_get_stats64,
	.ndo_get_iflink =	ip6_tnl_get_iflink,
};

static void ip6gre_tap_setup(struct net_device *dev)
{

	ether_setup(dev);

	dev->max_mtu = 0;
	dev->netdev_ops = &ip6gre_tap_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = ip6gre_dev_free;

	dev->features |= NETIF_F_NETNS_LOCAL;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	netif_keep_dst(dev);
}

bool is_ip6gretap_dev(const struct net_device *dev)
{
	return dev->netdev_ops == &ip6gre_tap_netdev_ops;
}
EXPORT_SYMBOL_GPL(is_ip6gretap_dev);

static bool ip6gre_netlink_encap_parms(struct nlattr *data[],
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

static int ip6gre_newlink(struct net *src_net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	struct ip6_tnl *nt;
	struct net *net = dev_net(dev);
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);
	struct ip_tunnel_encap ipencap;
	int err;

	nt = netdev_priv(dev);

	if (ip6gre_netlink_encap_parms(data, &ipencap)) {
		int err = ip6_tnl_encap_setup(nt, &ipencap);

		if (err < 0)
			return err;
	}

	ip6gre_netlink_parms(data, &nt->parms);

	if (nt->parms.collect_md) {
		if (rtnl_dereference(ign->collect_md_tun))
			return -EEXIST;
	} else {
		if (ip6gre_tunnel_find(net, &nt->parms, dev->type))
			return -EEXIST;
	}

	if (dev->type == ARPHRD_ETHER && !tb[IFLA_ADDRESS])
		eth_hw_addr_random(dev);

	nt->dev = dev;
	nt->net = dev_net(dev);

	err = register_netdevice(dev);
	if (err)
		goto out;

	ip6gre_tnl_link_config(nt, !tb[IFLA_MTU]);

	if (tb[IFLA_MTU])
		ip6_tnl_change_mtu(dev, nla_get_u32(tb[IFLA_MTU]));

	dev_hold(dev);
	ip6gre_tunnel_link(ign, nt);

out:
	return err;
}

static int ip6gre_changelink(struct net_device *dev, struct nlattr *tb[],
			     struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct ip6_tnl *t, *nt = netdev_priv(dev);
	struct net *net = nt->net;
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);
	struct __ip6_tnl_parm p;
	struct ip_tunnel_encap ipencap;

	if (dev == ign->fb_tunnel_dev)
		return -EINVAL;

	if (ip6gre_netlink_encap_parms(data, &ipencap)) {
		int err = ip6_tnl_encap_setup(nt, &ipencap);

		if (err < 0)
			return err;
	}

	ip6gre_netlink_parms(data, &p);

	t = ip6gre_tunnel_locate(net, &p, 0);

	if (t) {
		if (t->dev != dev)
			return -EEXIST;
	} else {
		t = nt;
	}

	ip6gre_tunnel_unlink(ign, t);
	ip6gre_tnl_change(t, &p, !tb[IFLA_MTU]);
	ip6gre_tunnel_link(ign, t);
	return 0;
}

static void ip6gre_dellink(struct net_device *dev, struct list_head *head)
{
	struct net *net = dev_net(dev);
	struct ip6gre_net *ign = net_generic(net, ip6gre_net_id);

	if (dev != ign->fb_tunnel_dev)
		unregister_netdevice_queue(dev, head);
}

static size_t ip6gre_get_size(const struct net_device *dev)
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
		nla_total_size(sizeof(struct in6_addr)) +
		/* IFLA_GRE_REMOTE */
		nla_total_size(sizeof(struct in6_addr)) +
		/* IFLA_GRE_TTL */
		nla_total_size(1) +
		/* IFLA_GRE_ENCAP_LIMIT */
		nla_total_size(1) +
		/* IFLA_GRE_FLOWINFO */
		nla_total_size(4) +
		/* IFLA_GRE_FLAGS */
		nla_total_size(4) +
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
		/* IFLA_GRE_FWMARK */
		nla_total_size(4) +
		/* IFLA_GRE_ERSPAN_INDEX */
		nla_total_size(4) +
		0;
}

static int ip6gre_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct __ip6_tnl_parm *p = &t->parms;

	if (nla_put_u32(skb, IFLA_GRE_LINK, p->link) ||
	    nla_put_be16(skb, IFLA_GRE_IFLAGS,
			 gre_tnl_flags_to_gre_flags(p->i_flags)) ||
	    nla_put_be16(skb, IFLA_GRE_OFLAGS,
			 gre_tnl_flags_to_gre_flags(p->o_flags)) ||
	    nla_put_be32(skb, IFLA_GRE_IKEY, p->i_key) ||
	    nla_put_be32(skb, IFLA_GRE_OKEY, p->o_key) ||
	    nla_put_in6_addr(skb, IFLA_GRE_LOCAL, &p->laddr) ||
	    nla_put_in6_addr(skb, IFLA_GRE_REMOTE, &p->raddr) ||
	    nla_put_u8(skb, IFLA_GRE_TTL, p->hop_limit) ||
	    nla_put_u8(skb, IFLA_GRE_ENCAP_LIMIT, p->encap_limit) ||
	    nla_put_be32(skb, IFLA_GRE_FLOWINFO, p->flowinfo) ||
	    nla_put_u32(skb, IFLA_GRE_FLAGS, p->flags) ||
	    nla_put_u32(skb, IFLA_GRE_FWMARK, p->fwmark) ||
	    nla_put_u32(skb, IFLA_GRE_ERSPAN_INDEX, p->index))
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

	if (p->collect_md) {
		if (nla_put_flag(skb, IFLA_GRE_COLLECT_METADATA))
			goto nla_put_failure;
	}

	if (nla_put_u8(skb, IFLA_GRE_ERSPAN_VER, p->erspan_ver))
		goto nla_put_failure;

	if (p->erspan_ver == 1) {
		if (nla_put_u32(skb, IFLA_GRE_ERSPAN_INDEX, p->index))
			goto nla_put_failure;
	} else if (p->erspan_ver == 2) {
		if (nla_put_u8(skb, IFLA_GRE_ERSPAN_DIR, p->dir))
			goto nla_put_failure;
		if (nla_put_u16(skb, IFLA_GRE_ERSPAN_HWID, p->hwid))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy ip6gre_policy[IFLA_GRE_MAX + 1] = {
	[IFLA_GRE_LINK]        = { .type = NLA_U32 },
	[IFLA_GRE_IFLAGS]      = { .type = NLA_U16 },
	[IFLA_GRE_OFLAGS]      = { .type = NLA_U16 },
	[IFLA_GRE_IKEY]        = { .type = NLA_U32 },
	[IFLA_GRE_OKEY]        = { .type = NLA_U32 },
	[IFLA_GRE_LOCAL]       = { .len = FIELD_SIZEOF(struct ipv6hdr, saddr) },
	[IFLA_GRE_REMOTE]      = { .len = FIELD_SIZEOF(struct ipv6hdr, daddr) },
	[IFLA_GRE_TTL]         = { .type = NLA_U8 },
	[IFLA_GRE_ENCAP_LIMIT] = { .type = NLA_U8 },
	[IFLA_GRE_FLOWINFO]    = { .type = NLA_U32 },
	[IFLA_GRE_FLAGS]       = { .type = NLA_U32 },
	[IFLA_GRE_ENCAP_TYPE]   = { .type = NLA_U16 },
	[IFLA_GRE_ENCAP_FLAGS]  = { .type = NLA_U16 },
	[IFLA_GRE_ENCAP_SPORT]  = { .type = NLA_U16 },
	[IFLA_GRE_ENCAP_DPORT]  = { .type = NLA_U16 },
	[IFLA_GRE_COLLECT_METADATA] = { .type = NLA_FLAG },
	[IFLA_GRE_FWMARK]       = { .type = NLA_U32 },
	[IFLA_GRE_ERSPAN_INDEX] = { .type = NLA_U32 },
	[IFLA_GRE_ERSPAN_VER]	= { .type = NLA_U8 },
	[IFLA_GRE_ERSPAN_DIR]	= { .type = NLA_U8 },
	[IFLA_GRE_ERSPAN_HWID]	= { .type = NLA_U16 },
};

static void ip6erspan_tap_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &ip6erspan_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = ip6gre_dev_free;

	dev->features |= NETIF_F_NETNS_LOCAL;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	netif_keep_dst(dev);
}

static struct rtnl_link_ops ip6gre_link_ops __read_mostly = {
	.kind		= "ip6gre",
	.maxtype	= IFLA_GRE_MAX,
	.policy		= ip6gre_policy,
	.priv_size	= sizeof(struct ip6_tnl),
	.setup		= ip6gre_tunnel_setup,
	.validate	= ip6gre_tunnel_validate,
	.newlink	= ip6gre_newlink,
	.changelink	= ip6gre_changelink,
	.dellink	= ip6gre_dellink,
	.get_size	= ip6gre_get_size,
	.fill_info	= ip6gre_fill_info,
	.get_link_net	= ip6_tnl_get_link_net,
};

static struct rtnl_link_ops ip6gre_tap_ops __read_mostly = {
	.kind		= "ip6gretap",
	.maxtype	= IFLA_GRE_MAX,
	.policy		= ip6gre_policy,
	.priv_size	= sizeof(struct ip6_tnl),
	.setup		= ip6gre_tap_setup,
	.validate	= ip6gre_tap_validate,
	.newlink	= ip6gre_newlink,
	.changelink	= ip6gre_changelink,
	.get_size	= ip6gre_get_size,
	.fill_info	= ip6gre_fill_info,
	.get_link_net	= ip6_tnl_get_link_net,
};

static struct rtnl_link_ops ip6erspan_tap_ops __read_mostly = {
	.kind		= "ip6erspan",
	.maxtype	= IFLA_GRE_MAX,
	.policy		= ip6gre_policy,
	.priv_size	= sizeof(struct ip6_tnl),
	.setup		= ip6erspan_tap_setup,
	.validate	= ip6erspan_tap_validate,
	.newlink	= ip6gre_newlink,
	.changelink	= ip6gre_changelink,
	.get_size	= ip6gre_get_size,
	.fill_info	= ip6gre_fill_info,
	.get_link_net	= ip6_tnl_get_link_net,
};

/*
 *	And now the modules code and kernel interface.
 */

static int __init ip6gre_init(void)
{
	int err;

	pr_info("GRE over IPv6 tunneling driver\n");

	err = register_pernet_device(&ip6gre_net_ops);
	if (err < 0)
		return err;

	err = inet6_add_protocol(&ip6gre_protocol, IPPROTO_GRE);
	if (err < 0) {
		pr_info("%s: can't add protocol\n", __func__);
		goto add_proto_failed;
	}

	err = rtnl_link_register(&ip6gre_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	err = rtnl_link_register(&ip6gre_tap_ops);
	if (err < 0)
		goto tap_ops_failed;

	err = rtnl_link_register(&ip6erspan_tap_ops);
	if (err < 0)
		goto erspan_link_failed;

out:
	return err;

erspan_link_failed:
	rtnl_link_unregister(&ip6gre_tap_ops);
tap_ops_failed:
	rtnl_link_unregister(&ip6gre_link_ops);
rtnl_link_failed:
	inet6_del_protocol(&ip6gre_protocol, IPPROTO_GRE);
add_proto_failed:
	unregister_pernet_device(&ip6gre_net_ops);
	goto out;
}

static void __exit ip6gre_fini(void)
{
	rtnl_link_unregister(&ip6gre_tap_ops);
	rtnl_link_unregister(&ip6gre_link_ops);
	rtnl_link_unregister(&ip6erspan_tap_ops);
	inet6_del_protocol(&ip6gre_protocol, IPPROTO_GRE);
	unregister_pernet_device(&ip6gre_net_ops);
}

module_init(ip6gre_init);
module_exit(ip6gre_fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("D. Kozlov (xeb@mail.ru)");
MODULE_DESCRIPTION("GRE over IPv6 tunneling device");
MODULE_ALIAS_RTNL_LINK("ip6gre");
MODULE_ALIAS_RTNL_LINK("ip6gretap");
MODULE_ALIAS_RTNL_LINK("ip6erspan");
MODULE_ALIAS_NETDEV("ip6gre0");
