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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/in6.h>
#include <linux/inetdevice.h>
#include <linux/netfilter_ipv4.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/static_key.h>

#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/ip_tunnels.h>
#include <net/ip6_tunnel.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/dst_metadata.h>

const struct ip_tunnel_encap_ops __rcu *
		iptun_encaps[MAX_IPTUN_ENCAP_OPS] __read_mostly;
EXPORT_SYMBOL(iptun_encaps);

const struct ip6_tnl_encap_ops __rcu *
		ip6tun_encaps[MAX_IPTUN_ENCAP_OPS] __read_mostly;
EXPORT_SYMBOL(ip6tun_encaps);

void iptunnel_xmit(struct sock *sk, struct rtable *rt, struct sk_buff *skb,
		   __be32 src, __be32 dst, __u8 proto,
		   __u8 tos, __u8 ttl, __be16 df, bool xnet)
{
	int pkt_len = skb->len - skb_inner_network_offset(skb);
	struct net *net = dev_net(rt->dst.dev);
	struct net_device *dev = skb->dev;
	struct iphdr *iph;
	int err;

	skb_scrub_packet(skb, xnet);

	skb_clear_hash_if_not_l4(skb);
	skb_dst_set(skb, &rt->dst);
	memset(IPCB(skb), 0, sizeof(*IPCB(skb)));

	/* Push down and install the IP header. */
	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);

	iph = ip_hdr(skb);

	iph->version	=	4;
	iph->ihl	=	sizeof(struct iphdr) >> 2;
	iph->frag_off	=	ip_mtu_locked(&rt->dst) ? 0 : df;
	iph->protocol	=	proto;
	iph->tos	=	tos;
	iph->daddr	=	dst;
	iph->saddr	=	src;
	iph->ttl	=	ttl;
	__ip_select_ident(net, iph, skb_shinfo(skb)->gso_segs ?: 1);

	err = ip_local_out(net, sk, skb);

	if (dev) {
		if (unlikely(net_xmit_eval(err)))
			pkt_len = 0;
		iptunnel_xmit_stats(dev, pkt_len);
	}
}
EXPORT_SYMBOL_GPL(iptunnel_xmit);

int __iptunnel_pull_header(struct sk_buff *skb, int hdr_len,
			   __be16 inner_proto, bool raw_proto, bool xnet)
{
	if (unlikely(!pskb_may_pull(skb, hdr_len)))
		return -ENOMEM;

	skb_pull_rcsum(skb, hdr_len);

	if (!raw_proto && inner_proto == htons(ETH_P_TEB)) {
		struct ethhdr *eh;

		if (unlikely(!pskb_may_pull(skb, ETH_HLEN)))
			return -ENOMEM;

		eh = (struct ethhdr *)skb->data;
		if (likely(eth_proto_is_802_3(eh->h_proto)))
			skb->protocol = eh->h_proto;
		else
			skb->protocol = htons(ETH_P_802_2);

	} else {
		skb->protocol = inner_proto;
	}

	skb_clear_hash_if_not_l4(skb);
	skb->vlan_tci = 0;
	skb_set_queue_mapping(skb, 0);
	skb_scrub_packet(skb, xnet);

	return iptunnel_pull_offloads(skb);
}
EXPORT_SYMBOL_GPL(__iptunnel_pull_header);

struct metadata_dst *iptunnel_metadata_reply(struct metadata_dst *md,
					     gfp_t flags)
{
	struct metadata_dst *res;
	struct ip_tunnel_info *dst, *src;

	if (!md || md->type != METADATA_IP_TUNNEL ||
	    md->u.tun_info.mode & IP_TUNNEL_INFO_TX)

		return NULL;

	res = metadata_dst_alloc(0, METADATA_IP_TUNNEL, flags);
	if (!res)
		return NULL;

	dst = &res->u.tun_info;
	src = &md->u.tun_info;
	dst->key.tun_id = src->key.tun_id;
	if (src->mode & IP_TUNNEL_INFO_IPV6)
		memcpy(&dst->key.u.ipv6.dst, &src->key.u.ipv6.src,
		       sizeof(struct in6_addr));
	else
		dst->key.u.ipv4.dst = src->key.u.ipv4.src;
	dst->mode = src->mode | IP_TUNNEL_INFO_TX;

	return res;
}
EXPORT_SYMBOL_GPL(iptunnel_metadata_reply);

int iptunnel_handle_offloads(struct sk_buff *skb,
			     int gso_type_mask)
{
	int err;

	if (likely(!skb->encapsulation)) {
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
	}

	if (skb_is_gso(skb)) {
		err = skb_header_unclone(skb, GFP_ATOMIC);
		if (unlikely(err))
			return err;
		skb_shinfo(skb)->gso_type |= gso_type_mask;
		return 0;
	}

	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		skb->ip_summed = CHECKSUM_NONE;
		/* We clear encapsulation here to prevent badly-written
		 * drivers potentially deciding to offload an inner checksum
		 * if we set CHECKSUM_PARTIAL on the outer header.
		 * This should go away when the drivers are all fixed.
		 */
		skb->encapsulation = 0;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iptunnel_handle_offloads);

/* Often modified stats are per cpu, other are shared (netdev->stats) */
void ip_tunnel_get_stats64(struct net_device *dev,
			   struct rtnl_link_stats64 *tot)
{
	int i;

	netdev_stats_to_stats64(tot, &dev->stats);

	for_each_possible_cpu(i) {
		const struct pcpu_sw_netstats *tstats =
						   per_cpu_ptr(dev->tstats, i);
		u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&tstats->syncp);
			rx_packets = tstats->rx_packets;
			tx_packets = tstats->tx_packets;
			rx_bytes = tstats->rx_bytes;
			tx_bytes = tstats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&tstats->syncp, start));

		tot->rx_packets += rx_packets;
		tot->tx_packets += tx_packets;
		tot->rx_bytes   += rx_bytes;
		tot->tx_bytes   += tx_bytes;
	}
}
EXPORT_SYMBOL_GPL(ip_tunnel_get_stats64);

static const struct nla_policy ip_tun_policy[LWTUNNEL_IP_MAX + 1] = {
	[LWTUNNEL_IP_ID]	= { .type = NLA_U64 },
	[LWTUNNEL_IP_DST]	= { .type = NLA_U32 },
	[LWTUNNEL_IP_SRC]	= { .type = NLA_U32 },
	[LWTUNNEL_IP_TTL]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_TOS]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_FLAGS]	= { .type = NLA_U16 },
};

static int ip_tun_build_state(struct nlattr *attr,
			      unsigned int family, const void *cfg,
			      struct lwtunnel_state **ts,
			      struct netlink_ext_ack *extack)
{
	struct ip_tunnel_info *tun_info;
	struct lwtunnel_state *new_state;
	struct nlattr *tb[LWTUNNEL_IP_MAX + 1];
	int err;

	err = nla_parse_nested(tb, LWTUNNEL_IP_MAX, attr, ip_tun_policy,
			       extack);
	if (err < 0)
		return err;

	new_state = lwtunnel_state_alloc(sizeof(*tun_info));
	if (!new_state)
		return -ENOMEM;

	new_state->type = LWTUNNEL_ENCAP_IP;

	tun_info = lwt_tun_info(new_state);

	if (tb[LWTUNNEL_IP_ID])
		tun_info->key.tun_id = nla_get_be64(tb[LWTUNNEL_IP_ID]);

	if (tb[LWTUNNEL_IP_DST])
		tun_info->key.u.ipv4.dst = nla_get_in_addr(tb[LWTUNNEL_IP_DST]);

	if (tb[LWTUNNEL_IP_SRC])
		tun_info->key.u.ipv4.src = nla_get_in_addr(tb[LWTUNNEL_IP_SRC]);

	if (tb[LWTUNNEL_IP_TTL])
		tun_info->key.ttl = nla_get_u8(tb[LWTUNNEL_IP_TTL]);

	if (tb[LWTUNNEL_IP_TOS])
		tun_info->key.tos = nla_get_u8(tb[LWTUNNEL_IP_TOS]);

	if (tb[LWTUNNEL_IP_FLAGS])
		tun_info->key.tun_flags = nla_get_be16(tb[LWTUNNEL_IP_FLAGS]);

	tun_info->mode = IP_TUNNEL_INFO_TX;
	tun_info->options_len = 0;

	*ts = new_state;

	return 0;
}

static int ip_tun_fill_encap_info(struct sk_buff *skb,
				  struct lwtunnel_state *lwtstate)
{
	struct ip_tunnel_info *tun_info = lwt_tun_info(lwtstate);

	if (nla_put_be64(skb, LWTUNNEL_IP_ID, tun_info->key.tun_id,
			 LWTUNNEL_IP_PAD) ||
	    nla_put_in_addr(skb, LWTUNNEL_IP_DST, tun_info->key.u.ipv4.dst) ||
	    nla_put_in_addr(skb, LWTUNNEL_IP_SRC, tun_info->key.u.ipv4.src) ||
	    nla_put_u8(skb, LWTUNNEL_IP_TOS, tun_info->key.tos) ||
	    nla_put_u8(skb, LWTUNNEL_IP_TTL, tun_info->key.ttl) ||
	    nla_put_be16(skb, LWTUNNEL_IP_FLAGS, tun_info->key.tun_flags))
		return -ENOMEM;

	return 0;
}

static int ip_tun_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size_64bit(8)	/* LWTUNNEL_IP_ID */
		+ nla_total_size(4)	/* LWTUNNEL_IP_DST */
		+ nla_total_size(4)	/* LWTUNNEL_IP_SRC */
		+ nla_total_size(1)	/* LWTUNNEL_IP_TOS */
		+ nla_total_size(1)	/* LWTUNNEL_IP_TTL */
		+ nla_total_size(2);	/* LWTUNNEL_IP_FLAGS */
}

static int ip_tun_cmp_encap(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	return memcmp(lwt_tun_info(a), lwt_tun_info(b),
		      sizeof(struct ip_tunnel_info));
}

static const struct lwtunnel_encap_ops ip_tun_lwt_ops = {
	.build_state = ip_tun_build_state,
	.fill_encap = ip_tun_fill_encap_info,
	.get_encap_size = ip_tun_encap_nlsize,
	.cmp_encap = ip_tun_cmp_encap,
	.owner = THIS_MODULE,
};

static const struct nla_policy ip6_tun_policy[LWTUNNEL_IP6_MAX + 1] = {
	[LWTUNNEL_IP6_ID]		= { .type = NLA_U64 },
	[LWTUNNEL_IP6_DST]		= { .len = sizeof(struct in6_addr) },
	[LWTUNNEL_IP6_SRC]		= { .len = sizeof(struct in6_addr) },
	[LWTUNNEL_IP6_HOPLIMIT]		= { .type = NLA_U8 },
	[LWTUNNEL_IP6_TC]		= { .type = NLA_U8 },
	[LWTUNNEL_IP6_FLAGS]		= { .type = NLA_U16 },
};

static int ip6_tun_build_state(struct nlattr *attr,
			       unsigned int family, const void *cfg,
			       struct lwtunnel_state **ts,
			       struct netlink_ext_ack *extack)
{
	struct ip_tunnel_info *tun_info;
	struct lwtunnel_state *new_state;
	struct nlattr *tb[LWTUNNEL_IP6_MAX + 1];
	int err;

	err = nla_parse_nested(tb, LWTUNNEL_IP6_MAX, attr, ip6_tun_policy,
			       extack);
	if (err < 0)
		return err;

	new_state = lwtunnel_state_alloc(sizeof(*tun_info));
	if (!new_state)
		return -ENOMEM;

	new_state->type = LWTUNNEL_ENCAP_IP6;

	tun_info = lwt_tun_info(new_state);

	if (tb[LWTUNNEL_IP6_ID])
		tun_info->key.tun_id = nla_get_be64(tb[LWTUNNEL_IP6_ID]);

	if (tb[LWTUNNEL_IP6_DST])
		tun_info->key.u.ipv6.dst = nla_get_in6_addr(tb[LWTUNNEL_IP6_DST]);

	if (tb[LWTUNNEL_IP6_SRC])
		tun_info->key.u.ipv6.src = nla_get_in6_addr(tb[LWTUNNEL_IP6_SRC]);

	if (tb[LWTUNNEL_IP6_HOPLIMIT])
		tun_info->key.ttl = nla_get_u8(tb[LWTUNNEL_IP6_HOPLIMIT]);

	if (tb[LWTUNNEL_IP6_TC])
		tun_info->key.tos = nla_get_u8(tb[LWTUNNEL_IP6_TC]);

	if (tb[LWTUNNEL_IP6_FLAGS])
		tun_info->key.tun_flags = nla_get_be16(tb[LWTUNNEL_IP6_FLAGS]);

	tun_info->mode = IP_TUNNEL_INFO_TX | IP_TUNNEL_INFO_IPV6;
	tun_info->options_len = 0;

	*ts = new_state;

	return 0;
}

static int ip6_tun_fill_encap_info(struct sk_buff *skb,
				   struct lwtunnel_state *lwtstate)
{
	struct ip_tunnel_info *tun_info = lwt_tun_info(lwtstate);

	if (nla_put_be64(skb, LWTUNNEL_IP6_ID, tun_info->key.tun_id,
			 LWTUNNEL_IP6_PAD) ||
	    nla_put_in6_addr(skb, LWTUNNEL_IP6_DST, &tun_info->key.u.ipv6.dst) ||
	    nla_put_in6_addr(skb, LWTUNNEL_IP6_SRC, &tun_info->key.u.ipv6.src) ||
	    nla_put_u8(skb, LWTUNNEL_IP6_TC, tun_info->key.tos) ||
	    nla_put_u8(skb, LWTUNNEL_IP6_HOPLIMIT, tun_info->key.ttl) ||
	    nla_put_be16(skb, LWTUNNEL_IP6_FLAGS, tun_info->key.tun_flags))
		return -ENOMEM;

	return 0;
}

static int ip6_tun_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size_64bit(8)	/* LWTUNNEL_IP6_ID */
		+ nla_total_size(16)	/* LWTUNNEL_IP6_DST */
		+ nla_total_size(16)	/* LWTUNNEL_IP6_SRC */
		+ nla_total_size(1)	/* LWTUNNEL_IP6_HOPLIMIT */
		+ nla_total_size(1)	/* LWTUNNEL_IP6_TC */
		+ nla_total_size(2);	/* LWTUNNEL_IP6_FLAGS */
}

static const struct lwtunnel_encap_ops ip6_tun_lwt_ops = {
	.build_state = ip6_tun_build_state,
	.fill_encap = ip6_tun_fill_encap_info,
	.get_encap_size = ip6_tun_encap_nlsize,
	.cmp_encap = ip_tun_cmp_encap,
	.owner = THIS_MODULE,
};

void __init ip_tunnel_core_init(void)
{
	/* If you land here, make sure whether increasing ip_tunnel_info's
	 * options_len is a reasonable choice with its usage in front ends
	 * (f.e., it's part of flow keys, etc).
	 */
	BUILD_BUG_ON(IP_TUNNEL_OPTS_MAX != 255);

	lwtunnel_encap_add_ops(&ip_tun_lwt_ops, LWTUNNEL_ENCAP_IP);
	lwtunnel_encap_add_ops(&ip6_tun_lwt_ops, LWTUNNEL_ENCAP_IP6);
}

DEFINE_STATIC_KEY_FALSE(ip_tunnel_metadata_cnt);
EXPORT_SYMBOL(ip_tunnel_metadata_cnt);

void ip_tunnel_need_metadata(void)
{
	static_branch_inc(&ip_tunnel_metadata_cnt);
}
EXPORT_SYMBOL_GPL(ip_tunnel_need_metadata);

void ip_tunnel_unneed_metadata(void)
{
	static_branch_dec(&ip_tunnel_metadata_cnt);
}
EXPORT_SYMBOL_GPL(ip_tunnel_unneed_metadata);

/* Returns either the correct skb->protocol value, or 0 if invalid. */
__be16 ip_tunnel_parse_protocol(const struct sk_buff *skb)
{
	if (skb_network_header(skb) >= skb->head &&
	    (skb_network_header(skb) + sizeof(struct iphdr)) <= skb_tail_pointer(skb) &&
	    ip_hdr(skb)->version == 4)
		return htons(ETH_P_IP);
	if (skb_network_header(skb) >= skb->head &&
	    (skb_network_header(skb) + sizeof(struct ipv6hdr)) <= skb_tail_pointer(skb) &&
	    ipv6_hdr(skb)->version == 6)
		return htons(ETH_P_IPV6);
	return 0;
}
EXPORT_SYMBOL(ip_tunnel_parse_protocol);
