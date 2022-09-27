// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 Nicira, Inc.
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
#include <net/ip6_checksum.h>
#include <net/arp.h>
#include <net/checksum.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>
#include <net/dst_metadata.h>
#include <net/geneve.h>
#include <net/vxlan.h>
#include <net/erspan.h>

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
	__vlan_hwaccel_clear_tag(skb);
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

	src = &md->u.tun_info;
	res = metadata_dst_alloc(src->options_len, METADATA_IP_TUNNEL, flags);
	if (!res)
		return NULL;

	dst = &res->u.tun_info;
	dst->key.tun_id = src->key.tun_id;
	if (src->mode & IP_TUNNEL_INFO_IPV6)
		memcpy(&dst->key.u.ipv6.dst, &src->key.u.ipv6.src,
		       sizeof(struct in6_addr));
	else
		dst->key.u.ipv4.dst = src->key.u.ipv4.src;
	dst->key.tun_flags = src->key.tun_flags;
	dst->mode = src->mode | IP_TUNNEL_INFO_TX;
	ip_tunnel_info_opts_set(dst, ip_tunnel_info_opts(src),
				src->options_len, 0);

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

/**
 * iptunnel_pmtud_build_icmp() - Build ICMP error message for PMTUD
 * @skb:	Original packet with L2 header
 * @mtu:	MTU value for ICMP error
 *
 * Return: length on success, negative error code if message couldn't be built.
 */
static int iptunnel_pmtud_build_icmp(struct sk_buff *skb, int mtu)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct icmphdr *icmph;
	struct iphdr *niph;
	struct ethhdr eh;
	int len, err;

	if (!pskb_may_pull(skb, ETH_HLEN + sizeof(struct iphdr)))
		return -EINVAL;

	skb_copy_bits(skb, skb_mac_offset(skb), &eh, ETH_HLEN);
	pskb_pull(skb, ETH_HLEN);
	skb_reset_network_header(skb);

	err = pskb_trim(skb, 576 - sizeof(*niph) - sizeof(*icmph));
	if (err)
		return err;

	len = skb->len + sizeof(*icmph);
	err = skb_cow(skb, sizeof(*niph) + sizeof(*icmph) + ETH_HLEN);
	if (err)
		return err;

	icmph = skb_push(skb, sizeof(*icmph));
	*icmph = (struct icmphdr) {
		.type			= ICMP_DEST_UNREACH,
		.code			= ICMP_FRAG_NEEDED,
		.checksum		= 0,
		.un.frag.__unused	= 0,
		.un.frag.mtu		= ntohs(mtu),
	};
	icmph->checksum = ip_compute_csum(icmph, len);
	skb_reset_transport_header(skb);

	niph = skb_push(skb, sizeof(*niph));
	*niph = (struct iphdr) {
		.ihl			= sizeof(*niph) / 4u,
		.version 		= 4,
		.tos 			= 0,
		.tot_len		= htons(len + sizeof(*niph)),
		.id			= 0,
		.frag_off		= htons(IP_DF),
		.ttl			= iph->ttl,
		.protocol		= IPPROTO_ICMP,
		.saddr			= iph->daddr,
		.daddr			= iph->saddr,
	};
	ip_send_check(niph);
	skb_reset_network_header(skb);

	skb->ip_summed = CHECKSUM_NONE;

	eth_header(skb, skb->dev, htons(eh.h_proto), eh.h_source, eh.h_dest, 0);
	skb_reset_mac_header(skb);

	return skb->len;
}

/**
 * iptunnel_pmtud_check_icmp() - Trigger ICMP reply if needed and allowed
 * @skb:	Buffer being sent by encapsulation, L2 headers expected
 * @mtu:	Network MTU for path
 *
 * Return: 0 for no ICMP reply, length if built, negative value on error.
 */
static int iptunnel_pmtud_check_icmp(struct sk_buff *skb, int mtu)
{
	const struct icmphdr *icmph = icmp_hdr(skb);
	const struct iphdr *iph = ip_hdr(skb);

	if (mtu < 576 || iph->frag_off != htons(IP_DF))
		return 0;

	if (ipv4_is_lbcast(iph->daddr)  || ipv4_is_multicast(iph->daddr) ||
	    ipv4_is_zeronet(iph->saddr) || ipv4_is_loopback(iph->saddr)  ||
	    ipv4_is_lbcast(iph->saddr)  || ipv4_is_multicast(iph->saddr))
		return 0;

	if (iph->protocol == IPPROTO_ICMP && icmp_is_err(icmph->type))
		return 0;

	return iptunnel_pmtud_build_icmp(skb, mtu);
}

#if IS_ENABLED(CONFIG_IPV6)
/**
 * iptunnel_pmtud_build_icmpv6() - Build ICMPv6 error message for PMTUD
 * @skb:	Original packet with L2 header
 * @mtu:	MTU value for ICMPv6 error
 *
 * Return: length on success, negative error code if message couldn't be built.
 */
static int iptunnel_pmtud_build_icmpv6(struct sk_buff *skb, int mtu)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct icmp6hdr *icmp6h;
	struct ipv6hdr *nip6h;
	struct ethhdr eh;
	int len, err;
	__wsum csum;

	if (!pskb_may_pull(skb, ETH_HLEN + sizeof(struct ipv6hdr)))
		return -EINVAL;

	skb_copy_bits(skb, skb_mac_offset(skb), &eh, ETH_HLEN);
	pskb_pull(skb, ETH_HLEN);
	skb_reset_network_header(skb);

	err = pskb_trim(skb, IPV6_MIN_MTU - sizeof(*nip6h) - sizeof(*icmp6h));
	if (err)
		return err;

	len = skb->len + sizeof(*icmp6h);
	err = skb_cow(skb, sizeof(*nip6h) + sizeof(*icmp6h) + ETH_HLEN);
	if (err)
		return err;

	icmp6h = skb_push(skb, sizeof(*icmp6h));
	*icmp6h = (struct icmp6hdr) {
		.icmp6_type		= ICMPV6_PKT_TOOBIG,
		.icmp6_code		= 0,
		.icmp6_cksum		= 0,
		.icmp6_mtu		= htonl(mtu),
	};
	skb_reset_transport_header(skb);

	nip6h = skb_push(skb, sizeof(*nip6h));
	*nip6h = (struct ipv6hdr) {
		.priority		= 0,
		.version		= 6,
		.flow_lbl		= { 0 },
		.payload_len		= htons(len),
		.nexthdr		= IPPROTO_ICMPV6,
		.hop_limit		= ip6h->hop_limit,
		.saddr			= ip6h->daddr,
		.daddr			= ip6h->saddr,
	};
	skb_reset_network_header(skb);

	csum = csum_partial(icmp6h, len, 0);
	icmp6h->icmp6_cksum = csum_ipv6_magic(&nip6h->saddr, &nip6h->daddr, len,
					      IPPROTO_ICMPV6, csum);

	skb->ip_summed = CHECKSUM_NONE;

	eth_header(skb, skb->dev, htons(eh.h_proto), eh.h_source, eh.h_dest, 0);
	skb_reset_mac_header(skb);

	return skb->len;
}

/**
 * iptunnel_pmtud_check_icmpv6() - Trigger ICMPv6 reply if needed and allowed
 * @skb:	Buffer being sent by encapsulation, L2 headers expected
 * @mtu:	Network MTU for path
 *
 * Return: 0 for no ICMPv6 reply, length if built, negative value on error.
 */
static int iptunnel_pmtud_check_icmpv6(struct sk_buff *skb, int mtu)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	int stype = ipv6_addr_type(&ip6h->saddr);
	u8 proto = ip6h->nexthdr;
	__be16 frag_off;
	int offset;

	if (mtu < IPV6_MIN_MTU)
		return 0;

	if (stype == IPV6_ADDR_ANY || stype == IPV6_ADDR_MULTICAST ||
	    stype == IPV6_ADDR_LOOPBACK)
		return 0;

	offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &proto,
				  &frag_off);
	if (offset < 0 || (frag_off & htons(~0x7)))
		return 0;

	if (proto == IPPROTO_ICMPV6) {
		struct icmp6hdr *icmp6h;

		if (!pskb_may_pull(skb, skb_network_header(skb) +
					offset + 1 - skb->data))
			return 0;

		icmp6h = (struct icmp6hdr *)(skb_network_header(skb) + offset);
		if (icmpv6_is_err(icmp6h->icmp6_type) ||
		    icmp6h->icmp6_type == NDISC_REDIRECT)
			return 0;
	}

	return iptunnel_pmtud_build_icmpv6(skb, mtu);
}
#endif /* IS_ENABLED(CONFIG_IPV6) */

/**
 * skb_tunnel_check_pmtu() - Check, update PMTU and trigger ICMP reply as needed
 * @skb:	Buffer being sent by encapsulation, L2 headers expected
 * @encap_dst:	Destination for tunnel encapsulation (outer IP)
 * @headroom:	Encapsulation header size, bytes
 * @reply:	Build matching ICMP or ICMPv6 message as a result
 *
 * L2 tunnel implementations that can carry IP and can be directly bridged
 * (currently UDP tunnels) can't always rely on IP forwarding paths to handle
 * PMTU discovery. In the bridged case, ICMP or ICMPv6 messages need to be built
 * based on payload and sent back by the encapsulation itself.
 *
 * For routable interfaces, we just need to update the PMTU for the destination.
 *
 * Return: 0 if ICMP error not needed, length if built, negative value on error
 */
int skb_tunnel_check_pmtu(struct sk_buff *skb, struct dst_entry *encap_dst,
			  int headroom, bool reply)
{
	u32 mtu = dst_mtu(encap_dst) - headroom;

	if ((skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu)) ||
	    (!skb_is_gso(skb) && (skb->len - skb_network_offset(skb)) <= mtu))
		return 0;

	skb_dst_update_pmtu_no_confirm(skb, mtu);

	if (!reply || skb->pkt_type == PACKET_HOST)
		return 0;

	if (skb->protocol == htons(ETH_P_IP))
		return iptunnel_pmtud_check_icmp(skb, mtu);

#if IS_ENABLED(CONFIG_IPV6)
	if (skb->protocol == htons(ETH_P_IPV6))
		return iptunnel_pmtud_check_icmpv6(skb, mtu);
#endif
	return 0;
}
EXPORT_SYMBOL(skb_tunnel_check_pmtu);

/* Often modified stats are per cpu, other are shared (netdev->stats) */
void ip_tunnel_get_stats64(struct net_device *dev,
			   struct rtnl_link_stats64 *tot)
{
	netdev_stats_to_stats64(tot, &dev->stats);
	dev_fetch_sw_netstats(tot, dev->tstats);
}
EXPORT_SYMBOL_GPL(ip_tunnel_get_stats64);

static const struct nla_policy ip_tun_policy[LWTUNNEL_IP_MAX + 1] = {
	[LWTUNNEL_IP_UNSPEC]	= { .strict_start_type = LWTUNNEL_IP_OPTS },
	[LWTUNNEL_IP_ID]	= { .type = NLA_U64 },
	[LWTUNNEL_IP_DST]	= { .type = NLA_U32 },
	[LWTUNNEL_IP_SRC]	= { .type = NLA_U32 },
	[LWTUNNEL_IP_TTL]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_TOS]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_FLAGS]	= { .type = NLA_U16 },
	[LWTUNNEL_IP_OPTS]	= { .type = NLA_NESTED },
};

static const struct nla_policy ip_opts_policy[LWTUNNEL_IP_OPTS_MAX + 1] = {
	[LWTUNNEL_IP_OPTS_GENEVE]	= { .type = NLA_NESTED },
	[LWTUNNEL_IP_OPTS_VXLAN]	= { .type = NLA_NESTED },
	[LWTUNNEL_IP_OPTS_ERSPAN]	= { .type = NLA_NESTED },
};

static const struct nla_policy
geneve_opt_policy[LWTUNNEL_IP_OPT_GENEVE_MAX + 1] = {
	[LWTUNNEL_IP_OPT_GENEVE_CLASS]	= { .type = NLA_U16 },
	[LWTUNNEL_IP_OPT_GENEVE_TYPE]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_OPT_GENEVE_DATA]	= { .type = NLA_BINARY, .len = 128 },
};

static const struct nla_policy
vxlan_opt_policy[LWTUNNEL_IP_OPT_VXLAN_MAX + 1] = {
	[LWTUNNEL_IP_OPT_VXLAN_GBP]	= { .type = NLA_U32 },
};

static const struct nla_policy
erspan_opt_policy[LWTUNNEL_IP_OPT_ERSPAN_MAX + 1] = {
	[LWTUNNEL_IP_OPT_ERSPAN_VER]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_OPT_ERSPAN_INDEX]	= { .type = NLA_U32 },
	[LWTUNNEL_IP_OPT_ERSPAN_DIR]	= { .type = NLA_U8 },
	[LWTUNNEL_IP_OPT_ERSPAN_HWID]	= { .type = NLA_U8 },
};

static int ip_tun_parse_opts_geneve(struct nlattr *attr,
				    struct ip_tunnel_info *info, int opts_len,
				    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[LWTUNNEL_IP_OPT_GENEVE_MAX + 1];
	int data_len, err;

	err = nla_parse_nested(tb, LWTUNNEL_IP_OPT_GENEVE_MAX, attr,
			       geneve_opt_policy, extack);
	if (err)
		return err;

	if (!tb[LWTUNNEL_IP_OPT_GENEVE_CLASS] ||
	    !tb[LWTUNNEL_IP_OPT_GENEVE_TYPE] ||
	    !tb[LWTUNNEL_IP_OPT_GENEVE_DATA])
		return -EINVAL;

	attr = tb[LWTUNNEL_IP_OPT_GENEVE_DATA];
	data_len = nla_len(attr);
	if (data_len % 4)
		return -EINVAL;

	if (info) {
		struct geneve_opt *opt = ip_tunnel_info_opts(info) + opts_len;

		memcpy(opt->opt_data, nla_data(attr), data_len);
		opt->length = data_len / 4;
		attr = tb[LWTUNNEL_IP_OPT_GENEVE_CLASS];
		opt->opt_class = nla_get_be16(attr);
		attr = tb[LWTUNNEL_IP_OPT_GENEVE_TYPE];
		opt->type = nla_get_u8(attr);
		info->key.tun_flags |= TUNNEL_GENEVE_OPT;
	}

	return sizeof(struct geneve_opt) + data_len;
}

static int ip_tun_parse_opts_vxlan(struct nlattr *attr,
				   struct ip_tunnel_info *info, int opts_len,
				   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[LWTUNNEL_IP_OPT_VXLAN_MAX + 1];
	int err;

	err = nla_parse_nested(tb, LWTUNNEL_IP_OPT_VXLAN_MAX, attr,
			       vxlan_opt_policy, extack);
	if (err)
		return err;

	if (!tb[LWTUNNEL_IP_OPT_VXLAN_GBP])
		return -EINVAL;

	if (info) {
		struct vxlan_metadata *md =
			ip_tunnel_info_opts(info) + opts_len;

		attr = tb[LWTUNNEL_IP_OPT_VXLAN_GBP];
		md->gbp = nla_get_u32(attr);
		md->gbp &= VXLAN_GBP_MASK;
		info->key.tun_flags |= TUNNEL_VXLAN_OPT;
	}

	return sizeof(struct vxlan_metadata);
}

static int ip_tun_parse_opts_erspan(struct nlattr *attr,
				    struct ip_tunnel_info *info, int opts_len,
				    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[LWTUNNEL_IP_OPT_ERSPAN_MAX + 1];
	int err;
	u8 ver;

	err = nla_parse_nested(tb, LWTUNNEL_IP_OPT_ERSPAN_MAX, attr,
			       erspan_opt_policy, extack);
	if (err)
		return err;

	if (!tb[LWTUNNEL_IP_OPT_ERSPAN_VER])
		return -EINVAL;

	ver = nla_get_u8(tb[LWTUNNEL_IP_OPT_ERSPAN_VER]);
	if (ver == 1) {
		if (!tb[LWTUNNEL_IP_OPT_ERSPAN_INDEX])
			return -EINVAL;
	} else if (ver == 2) {
		if (!tb[LWTUNNEL_IP_OPT_ERSPAN_DIR] ||
		    !tb[LWTUNNEL_IP_OPT_ERSPAN_HWID])
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	if (info) {
		struct erspan_metadata *md =
			ip_tunnel_info_opts(info) + opts_len;

		md->version = ver;
		if (ver == 1) {
			attr = tb[LWTUNNEL_IP_OPT_ERSPAN_INDEX];
			md->u.index = nla_get_be32(attr);
		} else {
			attr = tb[LWTUNNEL_IP_OPT_ERSPAN_DIR];
			md->u.md2.dir = nla_get_u8(attr);
			attr = tb[LWTUNNEL_IP_OPT_ERSPAN_HWID];
			set_hwid(&md->u.md2, nla_get_u8(attr));
		}

		info->key.tun_flags |= TUNNEL_ERSPAN_OPT;
	}

	return sizeof(struct erspan_metadata);
}

static int ip_tun_parse_opts(struct nlattr *attr, struct ip_tunnel_info *info,
			     struct netlink_ext_ack *extack)
{
	int err, rem, opt_len, opts_len = 0, type = 0;
	struct nlattr *nla;

	if (!attr)
		return 0;

	err = nla_validate(nla_data(attr), nla_len(attr), LWTUNNEL_IP_OPTS_MAX,
			   ip_opts_policy, extack);
	if (err)
		return err;

	nla_for_each_attr(nla, nla_data(attr), nla_len(attr), rem) {
		switch (nla_type(nla)) {
		case LWTUNNEL_IP_OPTS_GENEVE:
			if (type && type != TUNNEL_GENEVE_OPT)
				return -EINVAL;
			opt_len = ip_tun_parse_opts_geneve(nla, info, opts_len,
							   extack);
			if (opt_len < 0)
				return opt_len;
			opts_len += opt_len;
			if (opts_len > IP_TUNNEL_OPTS_MAX)
				return -EINVAL;
			type = TUNNEL_GENEVE_OPT;
			break;
		case LWTUNNEL_IP_OPTS_VXLAN:
			if (type)
				return -EINVAL;
			opt_len = ip_tun_parse_opts_vxlan(nla, info, opts_len,
							  extack);
			if (opt_len < 0)
				return opt_len;
			opts_len += opt_len;
			type = TUNNEL_VXLAN_OPT;
			break;
		case LWTUNNEL_IP_OPTS_ERSPAN:
			if (type)
				return -EINVAL;
			opt_len = ip_tun_parse_opts_erspan(nla, info, opts_len,
							   extack);
			if (opt_len < 0)
				return opt_len;
			opts_len += opt_len;
			type = TUNNEL_ERSPAN_OPT;
			break;
		default:
			return -EINVAL;
		}
	}

	return opts_len;
}

static int ip_tun_get_optlen(struct nlattr *attr,
			     struct netlink_ext_ack *extack)
{
	return ip_tun_parse_opts(attr, NULL, extack);
}

static int ip_tun_set_opts(struct nlattr *attr, struct ip_tunnel_info *info,
			   struct netlink_ext_ack *extack)
{
	return ip_tun_parse_opts(attr, info, extack);
}

static int ip_tun_build_state(struct net *net, struct nlattr *attr,
			      unsigned int family, const void *cfg,
			      struct lwtunnel_state **ts,
			      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[LWTUNNEL_IP_MAX + 1];
	struct lwtunnel_state *new_state;
	struct ip_tunnel_info *tun_info;
	int err, opt_len;

	err = nla_parse_nested_deprecated(tb, LWTUNNEL_IP_MAX, attr,
					  ip_tun_policy, extack);
	if (err < 0)
		return err;

	opt_len = ip_tun_get_optlen(tb[LWTUNNEL_IP_OPTS], extack);
	if (opt_len < 0)
		return opt_len;

	new_state = lwtunnel_state_alloc(sizeof(*tun_info) + opt_len);
	if (!new_state)
		return -ENOMEM;

	new_state->type = LWTUNNEL_ENCAP_IP;

	tun_info = lwt_tun_info(new_state);

	err = ip_tun_set_opts(tb[LWTUNNEL_IP_OPTS], tun_info, extack);
	if (err < 0) {
		lwtstate_free(new_state);
		return err;
	}

#ifdef CONFIG_DST_CACHE
	err = dst_cache_init(&tun_info->dst_cache, GFP_KERNEL);
	if (err) {
		lwtstate_free(new_state);
		return err;
	}
#endif

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
		tun_info->key.tun_flags |=
				(nla_get_be16(tb[LWTUNNEL_IP_FLAGS]) &
				 ~TUNNEL_OPTIONS_PRESENT);

	tun_info->mode = IP_TUNNEL_INFO_TX;
	tun_info->options_len = opt_len;

	*ts = new_state;

	return 0;
}

static void ip_tun_destroy_state(struct lwtunnel_state *lwtstate)
{
#ifdef CONFIG_DST_CACHE
	struct ip_tunnel_info *tun_info = lwt_tun_info(lwtstate);

	dst_cache_destroy(&tun_info->dst_cache);
#endif
}

static int ip_tun_fill_encap_opts_geneve(struct sk_buff *skb,
					 struct ip_tunnel_info *tun_info)
{
	struct geneve_opt *opt;
	struct nlattr *nest;
	int offset = 0;

	nest = nla_nest_start_noflag(skb, LWTUNNEL_IP_OPTS_GENEVE);
	if (!nest)
		return -ENOMEM;

	while (tun_info->options_len > offset) {
		opt = ip_tunnel_info_opts(tun_info) + offset;
		if (nla_put_be16(skb, LWTUNNEL_IP_OPT_GENEVE_CLASS,
				 opt->opt_class) ||
		    nla_put_u8(skb, LWTUNNEL_IP_OPT_GENEVE_TYPE, opt->type) ||
		    nla_put(skb, LWTUNNEL_IP_OPT_GENEVE_DATA, opt->length * 4,
			    opt->opt_data)) {
			nla_nest_cancel(skb, nest);
			return -ENOMEM;
		}
		offset += sizeof(*opt) + opt->length * 4;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int ip_tun_fill_encap_opts_vxlan(struct sk_buff *skb,
					struct ip_tunnel_info *tun_info)
{
	struct vxlan_metadata *md;
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, LWTUNNEL_IP_OPTS_VXLAN);
	if (!nest)
		return -ENOMEM;

	md = ip_tunnel_info_opts(tun_info);
	if (nla_put_u32(skb, LWTUNNEL_IP_OPT_VXLAN_GBP, md->gbp)) {
		nla_nest_cancel(skb, nest);
		return -ENOMEM;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int ip_tun_fill_encap_opts_erspan(struct sk_buff *skb,
					 struct ip_tunnel_info *tun_info)
{
	struct erspan_metadata *md;
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, LWTUNNEL_IP_OPTS_ERSPAN);
	if (!nest)
		return -ENOMEM;

	md = ip_tunnel_info_opts(tun_info);
	if (nla_put_u8(skb, LWTUNNEL_IP_OPT_ERSPAN_VER, md->version))
		goto err;

	if (md->version == 1 &&
	    nla_put_be32(skb, LWTUNNEL_IP_OPT_ERSPAN_INDEX, md->u.index))
		goto err;

	if (md->version == 2 &&
	    (nla_put_u8(skb, LWTUNNEL_IP_OPT_ERSPAN_DIR, md->u.md2.dir) ||
	     nla_put_u8(skb, LWTUNNEL_IP_OPT_ERSPAN_HWID,
			get_hwid(&md->u.md2))))
		goto err;

	nla_nest_end(skb, nest);
	return 0;
err:
	nla_nest_cancel(skb, nest);
	return -ENOMEM;
}

static int ip_tun_fill_encap_opts(struct sk_buff *skb, int type,
				  struct ip_tunnel_info *tun_info)
{
	struct nlattr *nest;
	int err = 0;

	if (!(tun_info->key.tun_flags & TUNNEL_OPTIONS_PRESENT))
		return 0;

	nest = nla_nest_start_noflag(skb, type);
	if (!nest)
		return -ENOMEM;

	if (tun_info->key.tun_flags & TUNNEL_GENEVE_OPT)
		err = ip_tun_fill_encap_opts_geneve(skb, tun_info);
	else if (tun_info->key.tun_flags & TUNNEL_VXLAN_OPT)
		err = ip_tun_fill_encap_opts_vxlan(skb, tun_info);
	else if (tun_info->key.tun_flags & TUNNEL_ERSPAN_OPT)
		err = ip_tun_fill_encap_opts_erspan(skb, tun_info);

	if (err) {
		nla_nest_cancel(skb, nest);
		return err;
	}

	nla_nest_end(skb, nest);
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
	    nla_put_be16(skb, LWTUNNEL_IP_FLAGS, tun_info->key.tun_flags) ||
	    ip_tun_fill_encap_opts(skb, LWTUNNEL_IP_OPTS, tun_info))
		return -ENOMEM;

	return 0;
}

static int ip_tun_opts_nlsize(struct ip_tunnel_info *info)
{
	int opt_len;

	if (!(info->key.tun_flags & TUNNEL_OPTIONS_PRESENT))
		return 0;

	opt_len = nla_total_size(0);		/* LWTUNNEL_IP_OPTS */
	if (info->key.tun_flags & TUNNEL_GENEVE_OPT) {
		struct geneve_opt *opt;
		int offset = 0;

		opt_len += nla_total_size(0);	/* LWTUNNEL_IP_OPTS_GENEVE */
		while (info->options_len > offset) {
			opt = ip_tunnel_info_opts(info) + offset;
			opt_len += nla_total_size(2)	/* OPT_GENEVE_CLASS */
				   + nla_total_size(1)	/* OPT_GENEVE_TYPE */
				   + nla_total_size(opt->length * 4);
							/* OPT_GENEVE_DATA */
			offset += sizeof(*opt) + opt->length * 4;
		}
	} else if (info->key.tun_flags & TUNNEL_VXLAN_OPT) {
		opt_len += nla_total_size(0)	/* LWTUNNEL_IP_OPTS_VXLAN */
			   + nla_total_size(4);	/* OPT_VXLAN_GBP */
	} else if (info->key.tun_flags & TUNNEL_ERSPAN_OPT) {
		struct erspan_metadata *md = ip_tunnel_info_opts(info);

		opt_len += nla_total_size(0)	/* LWTUNNEL_IP_OPTS_ERSPAN */
			   + nla_total_size(1)	/* OPT_ERSPAN_VER */
			   + (md->version == 1 ? nla_total_size(4)
						/* OPT_ERSPAN_INDEX (v1) */
					       : nla_total_size(1) +
						 nla_total_size(1));
						/* OPT_ERSPAN_DIR + HWID (v2) */
	}

	return opt_len;
}

static int ip_tun_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size_64bit(8)	/* LWTUNNEL_IP_ID */
		+ nla_total_size(4)	/* LWTUNNEL_IP_DST */
		+ nla_total_size(4)	/* LWTUNNEL_IP_SRC */
		+ nla_total_size(1)	/* LWTUNNEL_IP_TOS */
		+ nla_total_size(1)	/* LWTUNNEL_IP_TTL */
		+ nla_total_size(2)	/* LWTUNNEL_IP_FLAGS */
		+ ip_tun_opts_nlsize(lwt_tun_info(lwtstate));
					/* LWTUNNEL_IP_OPTS */
}

static int ip_tun_cmp_encap(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct ip_tunnel_info *info_a = lwt_tun_info(a);
	struct ip_tunnel_info *info_b = lwt_tun_info(b);

	return memcmp(info_a, info_b, sizeof(info_a->key)) ||
	       info_a->mode != info_b->mode ||
	       info_a->options_len != info_b->options_len ||
	       memcmp(ip_tunnel_info_opts(info_a),
		      ip_tunnel_info_opts(info_b), info_a->options_len);
}

static const struct lwtunnel_encap_ops ip_tun_lwt_ops = {
	.build_state = ip_tun_build_state,
	.destroy_state = ip_tun_destroy_state,
	.fill_encap = ip_tun_fill_encap_info,
	.get_encap_size = ip_tun_encap_nlsize,
	.cmp_encap = ip_tun_cmp_encap,
	.owner = THIS_MODULE,
};

static const struct nla_policy ip6_tun_policy[LWTUNNEL_IP6_MAX + 1] = {
	[LWTUNNEL_IP6_UNSPEC]	= { .strict_start_type = LWTUNNEL_IP6_OPTS },
	[LWTUNNEL_IP6_ID]		= { .type = NLA_U64 },
	[LWTUNNEL_IP6_DST]		= { .len = sizeof(struct in6_addr) },
	[LWTUNNEL_IP6_SRC]		= { .len = sizeof(struct in6_addr) },
	[LWTUNNEL_IP6_HOPLIMIT]		= { .type = NLA_U8 },
	[LWTUNNEL_IP6_TC]		= { .type = NLA_U8 },
	[LWTUNNEL_IP6_FLAGS]		= { .type = NLA_U16 },
	[LWTUNNEL_IP6_OPTS]		= { .type = NLA_NESTED },
};

static int ip6_tun_build_state(struct net *net, struct nlattr *attr,
			       unsigned int family, const void *cfg,
			       struct lwtunnel_state **ts,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[LWTUNNEL_IP6_MAX + 1];
	struct lwtunnel_state *new_state;
	struct ip_tunnel_info *tun_info;
	int err, opt_len;

	err = nla_parse_nested_deprecated(tb, LWTUNNEL_IP6_MAX, attr,
					  ip6_tun_policy, extack);
	if (err < 0)
		return err;

	opt_len = ip_tun_get_optlen(tb[LWTUNNEL_IP6_OPTS], extack);
	if (opt_len < 0)
		return opt_len;

	new_state = lwtunnel_state_alloc(sizeof(*tun_info) + opt_len);
	if (!new_state)
		return -ENOMEM;

	new_state->type = LWTUNNEL_ENCAP_IP6;

	tun_info = lwt_tun_info(new_state);

	err = ip_tun_set_opts(tb[LWTUNNEL_IP6_OPTS], tun_info, extack);
	if (err < 0) {
		lwtstate_free(new_state);
		return err;
	}

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
		tun_info->key.tun_flags |=
				(nla_get_be16(tb[LWTUNNEL_IP6_FLAGS]) &
				 ~TUNNEL_OPTIONS_PRESENT);

	tun_info->mode = IP_TUNNEL_INFO_TX | IP_TUNNEL_INFO_IPV6;
	tun_info->options_len = opt_len;

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
	    nla_put_be16(skb, LWTUNNEL_IP6_FLAGS, tun_info->key.tun_flags) ||
	    ip_tun_fill_encap_opts(skb, LWTUNNEL_IP6_OPTS, tun_info))
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
		+ nla_total_size(2)	/* LWTUNNEL_IP6_FLAGS */
		+ ip_tun_opts_nlsize(lwt_tun_info(lwtstate));
					/* LWTUNNEL_IP6_OPTS */
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

const struct header_ops ip_tunnel_header_ops = { .parse_protocol = ip_tunnel_parse_protocol };
EXPORT_SYMBOL(ip_tunnel_header_ops);
