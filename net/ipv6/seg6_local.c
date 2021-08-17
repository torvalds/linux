// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SR-IPv6 implementation
 *
 *  Authors:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *  eBPF support: Mathieu Xhonneux <m.xhonneux@gmail.com>
 */

#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/lwtunnel.h>
#include <net/netevent.h>
#include <net/netns/generic.h>
#include <net/ip6_fib.h>
#include <net/route.h>
#include <net/seg6.h>
#include <linux/seg6.h>
#include <linux/seg6_local.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/dst_cache.h>
#include <net/ip_tunnels.h>
#ifdef CONFIG_IPV6_SEG6_HMAC
#include <net/seg6_hmac.h>
#endif
#include <net/seg6_local.h>
#include <linux/etherdevice.h>
#include <linux/bpf.h>
#include <net/lwtunnel.h>
#include <linux/netfilter.h>

#define SEG6_F_ATTR(i)		BIT(i)

struct seg6_local_lwt;

/* callbacks used for customizing the creation and destruction of a behavior */
struct seg6_local_lwtunnel_ops {
	int (*build_state)(struct seg6_local_lwt *slwt, const void *cfg,
			   struct netlink_ext_ack *extack);
	void (*destroy_state)(struct seg6_local_lwt *slwt);
};

struct seg6_action_desc {
	int action;
	unsigned long attrs;

	/* The optattrs field is used for specifying all the optional
	 * attributes supported by a specific behavior.
	 * It means that if one of these attributes is not provided in the
	 * netlink message during the behavior creation, no errors will be
	 * returned to the userspace.
	 *
	 * Each attribute can be only of two types (mutually exclusive):
	 * 1) required or 2) optional.
	 * Every user MUST obey to this rule! If you set an attribute as
	 * required the same attribute CANNOT be set as optional and vice
	 * versa.
	 */
	unsigned long optattrs;

	int (*input)(struct sk_buff *skb, struct seg6_local_lwt *slwt);
	int static_headroom;

	struct seg6_local_lwtunnel_ops slwt_ops;
};

struct bpf_lwt_prog {
	struct bpf_prog *prog;
	char *name;
};

enum seg6_end_dt_mode {
	DT_INVALID_MODE	= -EINVAL,
	DT_LEGACY_MODE	= 0,
	DT_VRF_MODE	= 1,
};

struct seg6_end_dt_info {
	enum seg6_end_dt_mode mode;

	struct net *net;
	/* VRF device associated to the routing table used by the SRv6
	 * End.DT4/DT6 behavior for routing IPv4/IPv6 packets.
	 */
	int vrf_ifindex;
	int vrf_table;

	/* tunneled packet family (IPv4 or IPv6).
	 * Protocol and header length are inferred from family.
	 */
	u16 family;
};

struct pcpu_seg6_local_counters {
	u64_stats_t packets;
	u64_stats_t bytes;
	u64_stats_t errors;

	struct u64_stats_sync syncp;
};

/* This struct groups all the SRv6 Behavior counters supported so far.
 *
 * put_nla_counters() makes use of this data structure to collect all counter
 * values after the per-CPU counter evaluation has been performed.
 * Finally, each counter value (in seg6_local_counters) is stored in the
 * corresponding netlink attribute and sent to user space.
 *
 * NB: we don't want to expose this structure to user space!
 */
struct seg6_local_counters {
	__u64 packets;
	__u64 bytes;
	__u64 errors;
};

#define seg6_local_alloc_pcpu_counters(__gfp)				\
	__netdev_alloc_pcpu_stats(struct pcpu_seg6_local_counters,	\
				  ((__gfp) | __GFP_ZERO))

#define SEG6_F_LOCAL_COUNTERS	SEG6_F_ATTR(SEG6_LOCAL_COUNTERS)

struct seg6_local_lwt {
	int action;
	struct ipv6_sr_hdr *srh;
	int table;
	struct in_addr nh4;
	struct in6_addr nh6;
	int iif;
	int oif;
	struct bpf_lwt_prog bpf;
#ifdef CONFIG_NET_L3_MASTER_DEV
	struct seg6_end_dt_info dt_info;
#endif
	struct pcpu_seg6_local_counters __percpu *pcpu_counters;

	int headroom;
	struct seg6_action_desc *desc;
	/* unlike the required attrs, we have to track the optional attributes
	 * that have been effectively parsed.
	 */
	unsigned long parsed_optattrs;
};

static struct seg6_local_lwt *seg6_local_lwtunnel(struct lwtunnel_state *lwt)
{
	return (struct seg6_local_lwt *)lwt->data;
}

static struct ipv6_sr_hdr *get_srh(struct sk_buff *skb, int flags)
{
	struct ipv6_sr_hdr *srh;
	int len, srhoff = 0;

	if (ipv6_find_hdr(skb, &srhoff, IPPROTO_ROUTING, NULL, &flags) < 0)
		return NULL;

	if (!pskb_may_pull(skb, srhoff + sizeof(*srh)))
		return NULL;

	srh = (struct ipv6_sr_hdr *)(skb->data + srhoff);

	len = (srh->hdrlen + 1) << 3;

	if (!pskb_may_pull(skb, srhoff + len))
		return NULL;

	/* note that pskb_may_pull may change pointers in header;
	 * for this reason it is necessary to reload them when needed.
	 */
	srh = (struct ipv6_sr_hdr *)(skb->data + srhoff);

	if (!seg6_validate_srh(srh, len, true))
		return NULL;

	return srh;
}

static struct ipv6_sr_hdr *get_and_validate_srh(struct sk_buff *skb)
{
	struct ipv6_sr_hdr *srh;

	srh = get_srh(skb, IP6_FH_F_SKIP_RH);
	if (!srh)
		return NULL;

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (!seg6_hmac_validate_skb(skb))
		return NULL;
#endif

	return srh;
}

static bool decap_and_validate(struct sk_buff *skb, int proto)
{
	struct ipv6_sr_hdr *srh;
	unsigned int off = 0;

	srh = get_srh(skb, 0);
	if (srh && srh->segments_left > 0)
		return false;

#ifdef CONFIG_IPV6_SEG6_HMAC
	if (srh && !seg6_hmac_validate_skb(skb))
		return false;
#endif

	if (ipv6_find_hdr(skb, &off, proto, NULL, NULL) < 0)
		return false;

	if (!pskb_pull(skb, off))
		return false;

	skb_postpull_rcsum(skb, skb_network_header(skb), off);

	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	if (iptunnel_pull_offloads(skb))
		return false;

	return true;
}

static void advance_nextseg(struct ipv6_sr_hdr *srh, struct in6_addr *daddr)
{
	struct in6_addr *addr;

	srh->segments_left--;
	addr = srh->segments + srh->segments_left;
	*daddr = *addr;
}

static int
seg6_lookup_any_nexthop(struct sk_buff *skb, struct in6_addr *nhaddr,
			u32 tbl_id, bool local_delivery)
{
	struct net *net = dev_net(skb->dev);
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	int flags = RT6_LOOKUP_F_HAS_SADDR;
	struct dst_entry *dst = NULL;
	struct rt6_info *rt;
	struct flowi6 fl6;
	int dev_flags = 0;

	fl6.flowi6_iif = skb->dev->ifindex;
	fl6.daddr = nhaddr ? *nhaddr : hdr->daddr;
	fl6.saddr = hdr->saddr;
	fl6.flowlabel = ip6_flowinfo(hdr);
	fl6.flowi6_mark = skb->mark;
	fl6.flowi6_proto = hdr->nexthdr;

	if (nhaddr)
		fl6.flowi6_flags = FLOWI_FLAG_KNOWN_NH;

	if (!tbl_id) {
		dst = ip6_route_input_lookup(net, skb->dev, &fl6, skb, flags);
	} else {
		struct fib6_table *table;

		table = fib6_get_table(net, tbl_id);
		if (!table)
			goto out;

		rt = ip6_pol_route(net, table, 0, &fl6, skb, flags);
		dst = &rt->dst;
	}

	/* we want to discard traffic destined for local packet processing,
	 * if @local_delivery is set to false.
	 */
	if (!local_delivery)
		dev_flags |= IFF_LOOPBACK;

	if (dst && (dst->dev->flags & dev_flags) && !dst->error) {
		dst_release(dst);
		dst = NULL;
	}

out:
	if (!dst) {
		rt = net->ipv6.ip6_blk_hole_entry;
		dst = &rt->dst;
		dst_hold(dst);
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);
	return dst->error;
}

int seg6_lookup_nexthop(struct sk_buff *skb,
			struct in6_addr *nhaddr, u32 tbl_id)
{
	return seg6_lookup_any_nexthop(skb, nhaddr, tbl_id, false);
}

/* regular endpoint function */
static int input_action_end(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;

	srh = get_and_validate_srh(skb);
	if (!srh)
		goto drop;

	advance_nextseg(srh, &ipv6_hdr(skb)->daddr);

	seg6_lookup_nexthop(skb, NULL, 0);

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

/* regular endpoint, and forward to specified nexthop */
static int input_action_end_x(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;

	srh = get_and_validate_srh(skb);
	if (!srh)
		goto drop;

	advance_nextseg(srh, &ipv6_hdr(skb)->daddr);

	seg6_lookup_nexthop(skb, &slwt->nh6, 0);

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int input_action_end_t(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;

	srh = get_and_validate_srh(skb);
	if (!srh)
		goto drop;

	advance_nextseg(srh, &ipv6_hdr(skb)->daddr);

	seg6_lookup_nexthop(skb, NULL, slwt->table);

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

/* decapsulate and forward inner L2 frame on specified interface */
static int input_action_end_dx2(struct sk_buff *skb,
				struct seg6_local_lwt *slwt)
{
	struct net *net = dev_net(skb->dev);
	struct net_device *odev;
	struct ethhdr *eth;

	if (!decap_and_validate(skb, IPPROTO_ETHERNET))
		goto drop;

	if (!pskb_may_pull(skb, ETH_HLEN))
		goto drop;

	skb_reset_mac_header(skb);
	eth = (struct ethhdr *)skb->data;

	/* To determine the frame's protocol, we assume it is 802.3. This avoids
	 * a call to eth_type_trans(), which is not really relevant for our
	 * use case.
	 */
	if (!eth_proto_is_802_3(eth->h_proto))
		goto drop;

	odev = dev_get_by_index_rcu(net, slwt->oif);
	if (!odev)
		goto drop;

	/* As we accept Ethernet frames, make sure the egress device is of
	 * the correct type.
	 */
	if (odev->type != ARPHRD_ETHER)
		goto drop;

	if (!(odev->flags & IFF_UP) || !netif_carrier_ok(odev))
		goto drop;

	skb_orphan(skb);

	if (skb_warn_if_lro(skb))
		goto drop;

	skb_forward_csum(skb);

	if (skb->len - ETH_HLEN > odev->mtu)
		goto drop;

	skb->dev = odev;
	skb->protocol = eth->h_proto;

	return dev_queue_xmit(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int input_action_end_dx6_finish(struct net *net, struct sock *sk,
				       struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct in6_addr *nhaddr = NULL;
	struct seg6_local_lwt *slwt;

	slwt = seg6_local_lwtunnel(orig_dst->lwtstate);

	/* The inner packet is not associated to any local interface,
	 * so we do not call netif_rx().
	 *
	 * If slwt->nh6 is set to ::, then lookup the nexthop for the
	 * inner packet's DA. Otherwise, use the specified nexthop.
	 */
	if (!ipv6_addr_any(&slwt->nh6))
		nhaddr = &slwt->nh6;

	seg6_lookup_nexthop(skb, nhaddr, 0);

	return dst_input(skb);
}

/* decapsulate and forward to specified nexthop */
static int input_action_end_dx6(struct sk_buff *skb,
				struct seg6_local_lwt *slwt)
{
	/* this function accepts IPv6 encapsulated packets, with either
	 * an SRH with SL=0, or no SRH.
	 */

	if (!decap_and_validate(skb, IPPROTO_IPV6))
		goto drop;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	skb_set_transport_header(skb, sizeof(struct ipv6hdr));
	nf_reset_ct(skb);

	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return NF_HOOK(NFPROTO_IPV6, NF_INET_PRE_ROUTING,
			       dev_net(skb->dev), NULL, skb, NULL,
			       skb_dst(skb)->dev, input_action_end_dx6_finish);

	return input_action_end_dx6_finish(dev_net(skb->dev), NULL, skb);
drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int input_action_end_dx4_finish(struct net *net, struct sock *sk,
				       struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct seg6_local_lwt *slwt;
	struct iphdr *iph;
	__be32 nhaddr;
	int err;

	slwt = seg6_local_lwtunnel(orig_dst->lwtstate);

	iph = ip_hdr(skb);

	nhaddr = slwt->nh4.s_addr ?: iph->daddr;

	skb_dst_drop(skb);

	err = ip_route_input(skb, nhaddr, iph->saddr, 0, skb->dev);
	if (err) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return dst_input(skb);
}

static int input_action_end_dx4(struct sk_buff *skb,
				struct seg6_local_lwt *slwt)
{
	if (!decap_and_validate(skb, IPPROTO_IPIP))
		goto drop;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto drop;

	skb->protocol = htons(ETH_P_IP);
	skb_set_transport_header(skb, sizeof(struct iphdr));
	nf_reset_ct(skb);

	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING,
			       dev_net(skb->dev), NULL, skb, NULL,
			       skb_dst(skb)->dev, input_action_end_dx4_finish);

	return input_action_end_dx4_finish(dev_net(skb->dev), NULL, skb);
drop:
	kfree_skb(skb);
	return -EINVAL;
}

#ifdef CONFIG_NET_L3_MASTER_DEV
static struct net *fib6_config_get_net(const struct fib6_config *fib6_cfg)
{
	const struct nl_info *nli = &fib6_cfg->fc_nlinfo;

	return nli->nl_net;
}

static int __seg6_end_dt_vrf_build(struct seg6_local_lwt *slwt, const void *cfg,
				   u16 family, struct netlink_ext_ack *extack)
{
	struct seg6_end_dt_info *info = &slwt->dt_info;
	int vrf_ifindex;
	struct net *net;

	net = fib6_config_get_net(cfg);

	/* note that vrf_table was already set by parse_nla_vrftable() */
	vrf_ifindex = l3mdev_ifindex_lookup_by_table_id(L3MDEV_TYPE_VRF, net,
							info->vrf_table);
	if (vrf_ifindex < 0) {
		if (vrf_ifindex == -EPERM) {
			NL_SET_ERR_MSG(extack,
				       "Strict mode for VRF is disabled");
		} else if (vrf_ifindex == -ENODEV) {
			NL_SET_ERR_MSG(extack,
				       "Table has no associated VRF device");
		} else {
			pr_debug("seg6local: SRv6 End.DT* creation error=%d\n",
				 vrf_ifindex);
		}

		return vrf_ifindex;
	}

	info->net = net;
	info->vrf_ifindex = vrf_ifindex;

	info->family = family;
	info->mode = DT_VRF_MODE;

	return 0;
}

/* The SRv6 End.DT4/DT6 behavior extracts the inner (IPv4/IPv6) packet and
 * routes the IPv4/IPv6 packet by looking at the configured routing table.
 *
 * In the SRv6 End.DT4/DT6 use case, we can receive traffic (IPv6+Segment
 * Routing Header packets) from several interfaces and the outer IPv6
 * destination address (DA) is used for retrieving the specific instance of the
 * End.DT4/DT6 behavior that should process the packets.
 *
 * However, the inner IPv4/IPv6 packet is not really bound to any receiving
 * interface and thus the End.DT4/DT6 sets the VRF (associated with the
 * corresponding routing table) as the *receiving* interface.
 * In other words, the End.DT4/DT6 processes a packet as if it has been received
 * directly by the VRF (and not by one of its slave devices, if any).
 * In this way, the VRF interface is used for routing the IPv4/IPv6 packet in
 * according to the routing table configured by the End.DT4/DT6 instance.
 *
 * This design allows you to get some interesting features like:
 *  1) the statistics on rx packets;
 *  2) the possibility to install a packet sniffer on the receiving interface
 *     (the VRF one) for looking at the incoming packets;
 *  3) the possibility to leverage the netfilter prerouting hook for the inner
 *     IPv4 packet.
 *
 * This function returns:
 *  - the sk_buff* when the VRF rcv handler has processed the packet correctly;
 *  - NULL when the skb is consumed by the VRF rcv handler;
 *  - a pointer which encodes a negative error number in case of error.
 *    Note that in this case, the function takes care of freeing the skb.
 */
static struct sk_buff *end_dt_vrf_rcv(struct sk_buff *skb, u16 family,
				      struct net_device *dev)
{
	/* based on l3mdev_ip_rcv; we are only interested in the master */
	if (unlikely(!netif_is_l3_master(dev) && !netif_has_l3_rx_handler(dev)))
		goto drop;

	if (unlikely(!dev->l3mdev_ops->l3mdev_l3_rcv))
		goto drop;

	/* the decap packet IPv4/IPv6 does not come with any mac header info.
	 * We must unset the mac header to allow the VRF device to rebuild it,
	 * just in case there is a sniffer attached on the device.
	 */
	skb_unset_mac_header(skb);

	skb = dev->l3mdev_ops->l3mdev_l3_rcv(dev, skb, family);
	if (!skb)
		/* the skb buffer was consumed by the handler */
		return NULL;

	/* when a packet is received by a VRF or by one of its slaves, the
	 * master device reference is set into the skb.
	 */
	if (unlikely(skb->dev != dev || skb->skb_iif != dev->ifindex))
		goto drop;

	return skb;

drop:
	kfree_skb(skb);
	return ERR_PTR(-EINVAL);
}

static struct net_device *end_dt_get_vrf_rcu(struct sk_buff *skb,
					     struct seg6_end_dt_info *info)
{
	int vrf_ifindex = info->vrf_ifindex;
	struct net *net = info->net;

	if (unlikely(vrf_ifindex < 0))
		goto error;

	if (unlikely(!net_eq(dev_net(skb->dev), net)))
		goto error;

	return dev_get_by_index_rcu(net, vrf_ifindex);

error:
	return NULL;
}

static struct sk_buff *end_dt_vrf_core(struct sk_buff *skb,
				       struct seg6_local_lwt *slwt, u16 family)
{
	struct seg6_end_dt_info *info = &slwt->dt_info;
	struct net_device *vrf;
	__be16 protocol;
	int hdrlen;

	vrf = end_dt_get_vrf_rcu(skb, info);
	if (unlikely(!vrf))
		goto drop;

	switch (family) {
	case AF_INET:
		protocol = htons(ETH_P_IP);
		hdrlen = sizeof(struct iphdr);
		break;
	case AF_INET6:
		protocol = htons(ETH_P_IPV6);
		hdrlen = sizeof(struct ipv6hdr);
		break;
	case AF_UNSPEC:
		fallthrough;
	default:
		goto drop;
	}

	if (unlikely(info->family != AF_UNSPEC && info->family != family)) {
		pr_warn_once("seg6local: SRv6 End.DT* family mismatch");
		goto drop;
	}

	skb->protocol = protocol;

	skb_dst_drop(skb);

	skb_set_transport_header(skb, hdrlen);
	nf_reset_ct(skb);

	return end_dt_vrf_rcv(skb, family, vrf);

drop:
	kfree_skb(skb);
	return ERR_PTR(-EINVAL);
}

static int input_action_end_dt4(struct sk_buff *skb,
				struct seg6_local_lwt *slwt)
{
	struct iphdr *iph;
	int err;

	if (!decap_and_validate(skb, IPPROTO_IPIP))
		goto drop;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto drop;

	skb = end_dt_vrf_core(skb, slwt, AF_INET);
	if (!skb)
		/* packet has been processed and consumed by the VRF */
		return 0;

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	iph = ip_hdr(skb);

	err = ip_route_input(skb, iph->daddr, iph->saddr, 0, skb->dev);
	if (unlikely(err))
		goto drop;

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int seg6_end_dt4_build(struct seg6_local_lwt *slwt, const void *cfg,
			      struct netlink_ext_ack *extack)
{
	return __seg6_end_dt_vrf_build(slwt, cfg, AF_INET, extack);
}

static enum
seg6_end_dt_mode seg6_end_dt6_parse_mode(struct seg6_local_lwt *slwt)
{
	unsigned long parsed_optattrs = slwt->parsed_optattrs;
	bool legacy, vrfmode;

	legacy	= !!(parsed_optattrs & SEG6_F_ATTR(SEG6_LOCAL_TABLE));
	vrfmode	= !!(parsed_optattrs & SEG6_F_ATTR(SEG6_LOCAL_VRFTABLE));

	if (!(legacy ^ vrfmode))
		/* both are absent or present: invalid DT6 mode */
		return DT_INVALID_MODE;

	return legacy ? DT_LEGACY_MODE : DT_VRF_MODE;
}

static enum seg6_end_dt_mode seg6_end_dt6_get_mode(struct seg6_local_lwt *slwt)
{
	struct seg6_end_dt_info *info = &slwt->dt_info;

	return info->mode;
}

static int seg6_end_dt6_build(struct seg6_local_lwt *slwt, const void *cfg,
			      struct netlink_ext_ack *extack)
{
	enum seg6_end_dt_mode mode = seg6_end_dt6_parse_mode(slwt);
	struct seg6_end_dt_info *info = &slwt->dt_info;

	switch (mode) {
	case DT_LEGACY_MODE:
		info->mode = DT_LEGACY_MODE;
		return 0;
	case DT_VRF_MODE:
		return __seg6_end_dt_vrf_build(slwt, cfg, AF_INET6, extack);
	default:
		NL_SET_ERR_MSG(extack, "table or vrftable must be specified");
		return -EINVAL;
	}
}
#endif

static int input_action_end_dt6(struct sk_buff *skb,
				struct seg6_local_lwt *slwt)
{
	if (!decap_and_validate(skb, IPPROTO_IPV6))
		goto drop;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

#ifdef CONFIG_NET_L3_MASTER_DEV
	if (seg6_end_dt6_get_mode(slwt) == DT_LEGACY_MODE)
		goto legacy_mode;

	/* DT6_VRF_MODE */
	skb = end_dt_vrf_core(skb, slwt, AF_INET6);
	if (!skb)
		/* packet has been processed and consumed by the VRF */
		return 0;

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* note: this time we do not need to specify the table because the VRF
	 * takes care of selecting the correct table.
	 */
	seg6_lookup_any_nexthop(skb, NULL, 0, true);

	return dst_input(skb);

legacy_mode:
#endif
	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	seg6_lookup_any_nexthop(skb, NULL, slwt->table, true);

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

#ifdef CONFIG_NET_L3_MASTER_DEV
static int seg6_end_dt46_build(struct seg6_local_lwt *slwt, const void *cfg,
			       struct netlink_ext_ack *extack)
{
	return __seg6_end_dt_vrf_build(slwt, cfg, AF_UNSPEC, extack);
}

static int input_action_end_dt46(struct sk_buff *skb,
				 struct seg6_local_lwt *slwt)
{
	unsigned int off = 0;
	int nexthdr;

	nexthdr = ipv6_find_hdr(skb, &off, -1, NULL, NULL);
	if (unlikely(nexthdr < 0))
		goto drop;

	switch (nexthdr) {
	case IPPROTO_IPIP:
		return input_action_end_dt4(skb, slwt);
	case IPPROTO_IPV6:
		return input_action_end_dt6(skb, slwt);
	}

drop:
	kfree_skb(skb);
	return -EINVAL;
}
#endif

/* push an SRH on top of the current one */
static int input_action_end_b6(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;
	int err = -EINVAL;

	srh = get_and_validate_srh(skb);
	if (!srh)
		goto drop;

	err = seg6_do_srh_inline(skb, slwt->srh);
	if (err)
		goto drop;

	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	seg6_lookup_nexthop(skb, NULL, 0);

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return err;
}

/* encapsulate within an outer IPv6 header and a specified SRH */
static int input_action_end_b6_encap(struct sk_buff *skb,
				     struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;
	int err = -EINVAL;

	srh = get_and_validate_srh(skb);
	if (!srh)
		goto drop;

	advance_nextseg(srh, &ipv6_hdr(skb)->daddr);

	skb_reset_inner_headers(skb);
	skb->encapsulation = 1;

	err = seg6_do_srh_encap(skb, slwt->srh, IPPROTO_IPV6);
	if (err)
		goto drop;

	ipv6_hdr(skb)->payload_len = htons(skb->len - sizeof(struct ipv6hdr));
	skb_set_transport_header(skb, sizeof(struct ipv6hdr));

	seg6_lookup_nexthop(skb, NULL, 0);

	return dst_input(skb);

drop:
	kfree_skb(skb);
	return err;
}

DEFINE_PER_CPU(struct seg6_bpf_srh_state, seg6_bpf_srh_states);

bool seg6_bpf_has_valid_srh(struct sk_buff *skb)
{
	struct seg6_bpf_srh_state *srh_state =
		this_cpu_ptr(&seg6_bpf_srh_states);
	struct ipv6_sr_hdr *srh = srh_state->srh;

	if (unlikely(srh == NULL))
		return false;

	if (unlikely(!srh_state->valid)) {
		if ((srh_state->hdrlen & 7) != 0)
			return false;

		srh->hdrlen = (u8)(srh_state->hdrlen >> 3);
		if (!seg6_validate_srh(srh, (srh->hdrlen + 1) << 3, true))
			return false;

		srh_state->valid = true;
	}

	return true;
}

static int input_action_end_bpf(struct sk_buff *skb,
				struct seg6_local_lwt *slwt)
{
	struct seg6_bpf_srh_state *srh_state =
		this_cpu_ptr(&seg6_bpf_srh_states);
	struct ipv6_sr_hdr *srh;
	int ret;

	srh = get_and_validate_srh(skb);
	if (!srh) {
		kfree_skb(skb);
		return -EINVAL;
	}
	advance_nextseg(srh, &ipv6_hdr(skb)->daddr);

	/* preempt_disable is needed to protect the per-CPU buffer srh_state,
	 * which is also accessed by the bpf_lwt_seg6_* helpers
	 */
	preempt_disable();
	srh_state->srh = srh;
	srh_state->hdrlen = srh->hdrlen << 3;
	srh_state->valid = true;

	rcu_read_lock();
	bpf_compute_data_pointers(skb);
	ret = bpf_prog_run_save_cb(slwt->bpf.prog, skb);
	rcu_read_unlock();

	switch (ret) {
	case BPF_OK:
	case BPF_REDIRECT:
		break;
	case BPF_DROP:
		goto drop;
	default:
		pr_warn_once("bpf-seg6local: Illegal return value %u\n", ret);
		goto drop;
	}

	if (srh_state->srh && !seg6_bpf_has_valid_srh(skb))
		goto drop;

	preempt_enable();
	if (ret != BPF_REDIRECT)
		seg6_lookup_nexthop(skb, NULL, 0);

	return dst_input(skb);

drop:
	preempt_enable();
	kfree_skb(skb);
	return -EINVAL;
}

static struct seg6_action_desc seg6_action_table[] = {
	{
		.action		= SEG6_LOCAL_ACTION_END,
		.attrs		= 0,
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_X,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_NH6),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_x,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_T,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_TABLE),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_t,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_DX2,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_OIF),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_dx2,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_DX6,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_NH6),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_dx6,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_DX4,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_NH4),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_dx4,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_DT4,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_VRFTABLE),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
#ifdef CONFIG_NET_L3_MASTER_DEV
		.input		= input_action_end_dt4,
		.slwt_ops	= {
					.build_state = seg6_end_dt4_build,
				  },
#endif
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_DT6,
#ifdef CONFIG_NET_L3_MASTER_DEV
		.attrs		= 0,
		.optattrs	= SEG6_F_LOCAL_COUNTERS		|
				  SEG6_F_ATTR(SEG6_LOCAL_TABLE) |
				  SEG6_F_ATTR(SEG6_LOCAL_VRFTABLE),
		.slwt_ops	= {
					.build_state = seg6_end_dt6_build,
				  },
#else
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_TABLE),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
#endif
		.input		= input_action_end_dt6,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_DT46,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_VRFTABLE),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
#ifdef CONFIG_NET_L3_MASTER_DEV
		.input		= input_action_end_dt46,
		.slwt_ops	= {
					.build_state = seg6_end_dt46_build,
				  },
#endif
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_B6,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_SRH),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_b6,
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_B6_ENCAP,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_SRH),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_b6_encap,
		.static_headroom	= sizeof(struct ipv6hdr),
	},
	{
		.action		= SEG6_LOCAL_ACTION_END_BPF,
		.attrs		= SEG6_F_ATTR(SEG6_LOCAL_BPF),
		.optattrs	= SEG6_F_LOCAL_COUNTERS,
		.input		= input_action_end_bpf,
	},

};

static struct seg6_action_desc *__get_action_desc(int action)
{
	struct seg6_action_desc *desc;
	int i, count;

	count = ARRAY_SIZE(seg6_action_table);
	for (i = 0; i < count; i++) {
		desc = &seg6_action_table[i];
		if (desc->action == action)
			return desc;
	}

	return NULL;
}

static bool seg6_lwtunnel_counters_enabled(struct seg6_local_lwt *slwt)
{
	return slwt->parsed_optattrs & SEG6_F_LOCAL_COUNTERS;
}

static void seg6_local_update_counters(struct seg6_local_lwt *slwt,
				       unsigned int len, int err)
{
	struct pcpu_seg6_local_counters *pcounters;

	pcounters = this_cpu_ptr(slwt->pcpu_counters);
	u64_stats_update_begin(&pcounters->syncp);

	if (likely(!err)) {
		u64_stats_inc(&pcounters->packets);
		u64_stats_add(&pcounters->bytes, len);
	} else {
		u64_stats_inc(&pcounters->errors);
	}

	u64_stats_update_end(&pcounters->syncp);
}

static int seg6_local_input_core(struct net *net, struct sock *sk,
				 struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct seg6_action_desc *desc;
	struct seg6_local_lwt *slwt;
	unsigned int len = skb->len;
	int rc;

	slwt = seg6_local_lwtunnel(orig_dst->lwtstate);
	desc = slwt->desc;

	rc = desc->input(skb, slwt);

	if (!seg6_lwtunnel_counters_enabled(slwt))
		return rc;

	seg6_local_update_counters(slwt, len, rc);

	return rc;
}

static int seg6_local_input(struct sk_buff *skb)
{
	if (skb->protocol != htons(ETH_P_IPV6)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	if (static_branch_unlikely(&nf_hooks_lwtunnel_enabled))
		return NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_IN,
			       dev_net(skb->dev), NULL, skb, skb->dev, NULL,
			       seg6_local_input_core);

	return seg6_local_input_core(dev_net(skb->dev), NULL, skb);
}

static const struct nla_policy seg6_local_policy[SEG6_LOCAL_MAX + 1] = {
	[SEG6_LOCAL_ACTION]	= { .type = NLA_U32 },
	[SEG6_LOCAL_SRH]	= { .type = NLA_BINARY },
	[SEG6_LOCAL_TABLE]	= { .type = NLA_U32 },
	[SEG6_LOCAL_VRFTABLE]	= { .type = NLA_U32 },
	[SEG6_LOCAL_NH4]	= { .type = NLA_BINARY,
				    .len = sizeof(struct in_addr) },
	[SEG6_LOCAL_NH6]	= { .type = NLA_BINARY,
				    .len = sizeof(struct in6_addr) },
	[SEG6_LOCAL_IIF]	= { .type = NLA_U32 },
	[SEG6_LOCAL_OIF]	= { .type = NLA_U32 },
	[SEG6_LOCAL_BPF]	= { .type = NLA_NESTED },
	[SEG6_LOCAL_COUNTERS]	= { .type = NLA_NESTED },
};

static int parse_nla_srh(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;
	int len;

	srh = nla_data(attrs[SEG6_LOCAL_SRH]);
	len = nla_len(attrs[SEG6_LOCAL_SRH]);

	/* SRH must contain at least one segment */
	if (len < sizeof(*srh) + sizeof(struct in6_addr))
		return -EINVAL;

	if (!seg6_validate_srh(srh, len, false))
		return -EINVAL;

	slwt->srh = kmemdup(srh, len, GFP_KERNEL);
	if (!slwt->srh)
		return -ENOMEM;

	slwt->headroom += len;

	return 0;
}

static int put_nla_srh(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct ipv6_sr_hdr *srh;
	struct nlattr *nla;
	int len;

	srh = slwt->srh;
	len = (srh->hdrlen + 1) << 3;

	nla = nla_reserve(skb, SEG6_LOCAL_SRH, len);
	if (!nla)
		return -EMSGSIZE;

	memcpy(nla_data(nla), srh, len);

	return 0;
}

static int cmp_nla_srh(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	int len = (a->srh->hdrlen + 1) << 3;

	if (len != ((b->srh->hdrlen + 1) << 3))
		return 1;

	return memcmp(a->srh, b->srh, len);
}

static void destroy_attr_srh(struct seg6_local_lwt *slwt)
{
	kfree(slwt->srh);
}

static int parse_nla_table(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	slwt->table = nla_get_u32(attrs[SEG6_LOCAL_TABLE]);

	return 0;
}

static int put_nla_table(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	if (nla_put_u32(skb, SEG6_LOCAL_TABLE, slwt->table))
		return -EMSGSIZE;

	return 0;
}

static int cmp_nla_table(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	if (a->table != b->table)
		return 1;

	return 0;
}

static struct
seg6_end_dt_info *seg6_possible_end_dt_info(struct seg6_local_lwt *slwt)
{
#ifdef CONFIG_NET_L3_MASTER_DEV
	return &slwt->dt_info;
#else
	return ERR_PTR(-EOPNOTSUPP);
#endif
}

static int parse_nla_vrftable(struct nlattr **attrs,
			      struct seg6_local_lwt *slwt)
{
	struct seg6_end_dt_info *info = seg6_possible_end_dt_info(slwt);

	if (IS_ERR(info))
		return PTR_ERR(info);

	info->vrf_table = nla_get_u32(attrs[SEG6_LOCAL_VRFTABLE]);

	return 0;
}

static int put_nla_vrftable(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct seg6_end_dt_info *info = seg6_possible_end_dt_info(slwt);

	if (IS_ERR(info))
		return PTR_ERR(info);

	if (nla_put_u32(skb, SEG6_LOCAL_VRFTABLE, info->vrf_table))
		return -EMSGSIZE;

	return 0;
}

static int cmp_nla_vrftable(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	struct seg6_end_dt_info *info_a = seg6_possible_end_dt_info(a);
	struct seg6_end_dt_info *info_b = seg6_possible_end_dt_info(b);

	if (info_a->vrf_table != info_b->vrf_table)
		return 1;

	return 0;
}

static int parse_nla_nh4(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	memcpy(&slwt->nh4, nla_data(attrs[SEG6_LOCAL_NH4]),
	       sizeof(struct in_addr));

	return 0;
}

static int put_nla_nh4(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct nlattr *nla;

	nla = nla_reserve(skb, SEG6_LOCAL_NH4, sizeof(struct in_addr));
	if (!nla)
		return -EMSGSIZE;

	memcpy(nla_data(nla), &slwt->nh4, sizeof(struct in_addr));

	return 0;
}

static int cmp_nla_nh4(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	return memcmp(&a->nh4, &b->nh4, sizeof(struct in_addr));
}

static int parse_nla_nh6(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	memcpy(&slwt->nh6, nla_data(attrs[SEG6_LOCAL_NH6]),
	       sizeof(struct in6_addr));

	return 0;
}

static int put_nla_nh6(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct nlattr *nla;

	nla = nla_reserve(skb, SEG6_LOCAL_NH6, sizeof(struct in6_addr));
	if (!nla)
		return -EMSGSIZE;

	memcpy(nla_data(nla), &slwt->nh6, sizeof(struct in6_addr));

	return 0;
}

static int cmp_nla_nh6(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	return memcmp(&a->nh6, &b->nh6, sizeof(struct in6_addr));
}

static int parse_nla_iif(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	slwt->iif = nla_get_u32(attrs[SEG6_LOCAL_IIF]);

	return 0;
}

static int put_nla_iif(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	if (nla_put_u32(skb, SEG6_LOCAL_IIF, slwt->iif))
		return -EMSGSIZE;

	return 0;
}

static int cmp_nla_iif(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	if (a->iif != b->iif)
		return 1;

	return 0;
}

static int parse_nla_oif(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	slwt->oif = nla_get_u32(attrs[SEG6_LOCAL_OIF]);

	return 0;
}

static int put_nla_oif(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	if (nla_put_u32(skb, SEG6_LOCAL_OIF, slwt->oif))
		return -EMSGSIZE;

	return 0;
}

static int cmp_nla_oif(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	if (a->oif != b->oif)
		return 1;

	return 0;
}

#define MAX_PROG_NAME 256
static const struct nla_policy bpf_prog_policy[SEG6_LOCAL_BPF_PROG_MAX + 1] = {
	[SEG6_LOCAL_BPF_PROG]	   = { .type = NLA_U32, },
	[SEG6_LOCAL_BPF_PROG_NAME] = { .type = NLA_NUL_STRING,
				       .len = MAX_PROG_NAME },
};

static int parse_nla_bpf(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	struct nlattr *tb[SEG6_LOCAL_BPF_PROG_MAX + 1];
	struct bpf_prog *p;
	int ret;
	u32 fd;

	ret = nla_parse_nested_deprecated(tb, SEG6_LOCAL_BPF_PROG_MAX,
					  attrs[SEG6_LOCAL_BPF],
					  bpf_prog_policy, NULL);
	if (ret < 0)
		return ret;

	if (!tb[SEG6_LOCAL_BPF_PROG] || !tb[SEG6_LOCAL_BPF_PROG_NAME])
		return -EINVAL;

	slwt->bpf.name = nla_memdup(tb[SEG6_LOCAL_BPF_PROG_NAME], GFP_KERNEL);
	if (!slwt->bpf.name)
		return -ENOMEM;

	fd = nla_get_u32(tb[SEG6_LOCAL_BPF_PROG]);
	p = bpf_prog_get_type(fd, BPF_PROG_TYPE_LWT_SEG6LOCAL);
	if (IS_ERR(p)) {
		kfree(slwt->bpf.name);
		return PTR_ERR(p);
	}

	slwt->bpf.prog = p;
	return 0;
}

static int put_nla_bpf(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct nlattr *nest;

	if (!slwt->bpf.prog)
		return 0;

	nest = nla_nest_start_noflag(skb, SEG6_LOCAL_BPF);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(skb, SEG6_LOCAL_BPF_PROG, slwt->bpf.prog->aux->id))
		return -EMSGSIZE;

	if (slwt->bpf.name &&
	    nla_put_string(skb, SEG6_LOCAL_BPF_PROG_NAME, slwt->bpf.name))
		return -EMSGSIZE;

	return nla_nest_end(skb, nest);
}

static int cmp_nla_bpf(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	if (!a->bpf.name && !b->bpf.name)
		return 0;

	if (!a->bpf.name || !b->bpf.name)
		return 1;

	return strcmp(a->bpf.name, b->bpf.name);
}

static void destroy_attr_bpf(struct seg6_local_lwt *slwt)
{
	kfree(slwt->bpf.name);
	if (slwt->bpf.prog)
		bpf_prog_put(slwt->bpf.prog);
}

static const struct
nla_policy seg6_local_counters_policy[SEG6_LOCAL_CNT_MAX + 1] = {
	[SEG6_LOCAL_CNT_PACKETS]	= { .type = NLA_U64 },
	[SEG6_LOCAL_CNT_BYTES]		= { .type = NLA_U64 },
	[SEG6_LOCAL_CNT_ERRORS]		= { .type = NLA_U64 },
};

static int parse_nla_counters(struct nlattr **attrs,
			      struct seg6_local_lwt *slwt)
{
	struct pcpu_seg6_local_counters __percpu *pcounters;
	struct nlattr *tb[SEG6_LOCAL_CNT_MAX + 1];
	int ret;

	ret = nla_parse_nested_deprecated(tb, SEG6_LOCAL_CNT_MAX,
					  attrs[SEG6_LOCAL_COUNTERS],
					  seg6_local_counters_policy, NULL);
	if (ret < 0)
		return ret;

	/* basic support for SRv6 Behavior counters requires at least:
	 * packets, bytes and errors.
	 */
	if (!tb[SEG6_LOCAL_CNT_PACKETS] || !tb[SEG6_LOCAL_CNT_BYTES] ||
	    !tb[SEG6_LOCAL_CNT_ERRORS])
		return -EINVAL;

	/* counters are always zero initialized */
	pcounters = seg6_local_alloc_pcpu_counters(GFP_KERNEL);
	if (!pcounters)
		return -ENOMEM;

	slwt->pcpu_counters = pcounters;

	return 0;
}

static int seg6_local_fill_nla_counters(struct sk_buff *skb,
					struct seg6_local_counters *counters)
{
	if (nla_put_u64_64bit(skb, SEG6_LOCAL_CNT_PACKETS, counters->packets,
			      SEG6_LOCAL_CNT_PAD))
		return -EMSGSIZE;

	if (nla_put_u64_64bit(skb, SEG6_LOCAL_CNT_BYTES, counters->bytes,
			      SEG6_LOCAL_CNT_PAD))
		return -EMSGSIZE;

	if (nla_put_u64_64bit(skb, SEG6_LOCAL_CNT_ERRORS, counters->errors,
			      SEG6_LOCAL_CNT_PAD))
		return -EMSGSIZE;

	return 0;
}

static int put_nla_counters(struct sk_buff *skb, struct seg6_local_lwt *slwt)
{
	struct seg6_local_counters counters = { 0, 0, 0 };
	struct nlattr *nest;
	int rc, i;

	nest = nla_nest_start(skb, SEG6_LOCAL_COUNTERS);
	if (!nest)
		return -EMSGSIZE;

	for_each_possible_cpu(i) {
		struct pcpu_seg6_local_counters *pcounters;
		u64 packets, bytes, errors;
		unsigned int start;

		pcounters = per_cpu_ptr(slwt->pcpu_counters, i);
		do {
			start = u64_stats_fetch_begin_irq(&pcounters->syncp);

			packets = u64_stats_read(&pcounters->packets);
			bytes = u64_stats_read(&pcounters->bytes);
			errors = u64_stats_read(&pcounters->errors);

		} while (u64_stats_fetch_retry_irq(&pcounters->syncp, start));

		counters.packets += packets;
		counters.bytes += bytes;
		counters.errors += errors;
	}

	rc = seg6_local_fill_nla_counters(skb, &counters);
	if (rc < 0) {
		nla_nest_cancel(skb, nest);
		return rc;
	}

	return nla_nest_end(skb, nest);
}

static int cmp_nla_counters(struct seg6_local_lwt *a, struct seg6_local_lwt *b)
{
	/* a and b are equal if both have pcpu_counters set or not */
	return (!!((unsigned long)a->pcpu_counters)) ^
		(!!((unsigned long)b->pcpu_counters));
}

static void destroy_attr_counters(struct seg6_local_lwt *slwt)
{
	free_percpu(slwt->pcpu_counters);
}

struct seg6_action_param {
	int (*parse)(struct nlattr **attrs, struct seg6_local_lwt *slwt);
	int (*put)(struct sk_buff *skb, struct seg6_local_lwt *slwt);
	int (*cmp)(struct seg6_local_lwt *a, struct seg6_local_lwt *b);

	/* optional destroy() callback useful for releasing resources which
	 * have been previously acquired in the corresponding parse()
	 * function.
	 */
	void (*destroy)(struct seg6_local_lwt *slwt);
};

static struct seg6_action_param seg6_action_params[SEG6_LOCAL_MAX + 1] = {
	[SEG6_LOCAL_SRH]	= { .parse = parse_nla_srh,
				    .put = put_nla_srh,
				    .cmp = cmp_nla_srh,
				    .destroy = destroy_attr_srh },

	[SEG6_LOCAL_TABLE]	= { .parse = parse_nla_table,
				    .put = put_nla_table,
				    .cmp = cmp_nla_table },

	[SEG6_LOCAL_NH4]	= { .parse = parse_nla_nh4,
				    .put = put_nla_nh4,
				    .cmp = cmp_nla_nh4 },

	[SEG6_LOCAL_NH6]	= { .parse = parse_nla_nh6,
				    .put = put_nla_nh6,
				    .cmp = cmp_nla_nh6 },

	[SEG6_LOCAL_IIF]	= { .parse = parse_nla_iif,
				    .put = put_nla_iif,
				    .cmp = cmp_nla_iif },

	[SEG6_LOCAL_OIF]	= { .parse = parse_nla_oif,
				    .put = put_nla_oif,
				    .cmp = cmp_nla_oif },

	[SEG6_LOCAL_BPF]	= { .parse = parse_nla_bpf,
				    .put = put_nla_bpf,
				    .cmp = cmp_nla_bpf,
				    .destroy = destroy_attr_bpf },

	[SEG6_LOCAL_VRFTABLE]	= { .parse = parse_nla_vrftable,
				    .put = put_nla_vrftable,
				    .cmp = cmp_nla_vrftable },

	[SEG6_LOCAL_COUNTERS]	= { .parse = parse_nla_counters,
				    .put = put_nla_counters,
				    .cmp = cmp_nla_counters,
				    .destroy = destroy_attr_counters },
};

/* call the destroy() callback (if available) for each set attribute in
 * @parsed_attrs, starting from the first attribute up to the @max_parsed
 * (excluded) attribute.
 */
static void __destroy_attrs(unsigned long parsed_attrs, int max_parsed,
			    struct seg6_local_lwt *slwt)
{
	struct seg6_action_param *param;
	int i;

	/* Every required seg6local attribute is identified by an ID which is
	 * encoded as a flag (i.e: 1 << ID) in the 'attrs' bitmask;
	 *
	 * We scan the 'parsed_attrs' bitmask, starting from the first attribute
	 * up to the @max_parsed (excluded) attribute.
	 * For each set attribute, we retrieve the corresponding destroy()
	 * callback. If the callback is not available, then we skip to the next
	 * attribute; otherwise, we call the destroy() callback.
	 */
	for (i = 0; i < max_parsed; ++i) {
		if (!(parsed_attrs & SEG6_F_ATTR(i)))
			continue;

		param = &seg6_action_params[i];

		if (param->destroy)
			param->destroy(slwt);
	}
}

/* release all the resources that may have been acquired during parsing
 * operations.
 */
static void destroy_attrs(struct seg6_local_lwt *slwt)
{
	unsigned long attrs = slwt->desc->attrs | slwt->parsed_optattrs;

	__destroy_attrs(attrs, SEG6_LOCAL_MAX + 1, slwt);
}

static int parse_nla_optional_attrs(struct nlattr **attrs,
				    struct seg6_local_lwt *slwt)
{
	struct seg6_action_desc *desc = slwt->desc;
	unsigned long parsed_optattrs = 0;
	struct seg6_action_param *param;
	int err, i;

	for (i = 0; i < SEG6_LOCAL_MAX + 1; ++i) {
		if (!(desc->optattrs & SEG6_F_ATTR(i)) || !attrs[i])
			continue;

		/* once here, the i-th attribute is provided by the
		 * userspace AND it is identified optional as well.
		 */
		param = &seg6_action_params[i];

		err = param->parse(attrs, slwt);
		if (err < 0)
			goto parse_optattrs_err;

		/* current attribute has been correctly parsed */
		parsed_optattrs |= SEG6_F_ATTR(i);
	}

	/* store in the tunnel state all the optional attributed successfully
	 * parsed.
	 */
	slwt->parsed_optattrs = parsed_optattrs;

	return 0;

parse_optattrs_err:
	__destroy_attrs(parsed_optattrs, i, slwt);

	return err;
}

/* call the custom constructor of the behavior during its initialization phase
 * and after that all its attributes have been parsed successfully.
 */
static int
seg6_local_lwtunnel_build_state(struct seg6_local_lwt *slwt, const void *cfg,
				struct netlink_ext_ack *extack)
{
	struct seg6_action_desc *desc = slwt->desc;
	struct seg6_local_lwtunnel_ops *ops;

	ops = &desc->slwt_ops;
	if (!ops->build_state)
		return 0;

	return ops->build_state(slwt, cfg, extack);
}

/* call the custom destructor of the behavior which is invoked before the
 * tunnel is going to be destroyed.
 */
static void seg6_local_lwtunnel_destroy_state(struct seg6_local_lwt *slwt)
{
	struct seg6_action_desc *desc = slwt->desc;
	struct seg6_local_lwtunnel_ops *ops;

	ops = &desc->slwt_ops;
	if (!ops->destroy_state)
		return;

	ops->destroy_state(slwt);
}

static int parse_nla_action(struct nlattr **attrs, struct seg6_local_lwt *slwt)
{
	struct seg6_action_param *param;
	struct seg6_action_desc *desc;
	unsigned long invalid_attrs;
	int i, err;

	desc = __get_action_desc(slwt->action);
	if (!desc)
		return -EINVAL;

	if (!desc->input)
		return -EOPNOTSUPP;

	slwt->desc = desc;
	slwt->headroom += desc->static_headroom;

	/* Forcing the desc->optattrs *set* and the desc->attrs *set* to be
	 * disjoined, this allow us to release acquired resources by optional
	 * attributes and by required attributes independently from each other
	 * without any interference.
	 * In other terms, we are sure that we do not release some the acquired
	 * resources twice.
	 *
	 * Note that if an attribute is configured both as required and as
	 * optional, it means that the user has messed something up in the
	 * seg6_action_table. Therefore, this check is required for SRv6
	 * behaviors to work properly.
	 */
	invalid_attrs = desc->attrs & desc->optattrs;
	if (invalid_attrs) {
		WARN_ONCE(1,
			  "An attribute cannot be both required AND optional");
		return -EINVAL;
	}

	/* parse the required attributes */
	for (i = 0; i < SEG6_LOCAL_MAX + 1; i++) {
		if (desc->attrs & SEG6_F_ATTR(i)) {
			if (!attrs[i])
				return -EINVAL;

			param = &seg6_action_params[i];

			err = param->parse(attrs, slwt);
			if (err < 0)
				goto parse_attrs_err;
		}
	}

	/* parse the optional attributes, if any */
	err = parse_nla_optional_attrs(attrs, slwt);
	if (err < 0)
		goto parse_attrs_err;

	return 0;

parse_attrs_err:
	/* release any resource that may have been acquired during the i-1
	 * parse() operations.
	 */
	__destroy_attrs(desc->attrs, i, slwt);

	return err;
}

static int seg6_local_build_state(struct net *net, struct nlattr *nla,
				  unsigned int family, const void *cfg,
				  struct lwtunnel_state **ts,
				  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[SEG6_LOCAL_MAX + 1];
	struct lwtunnel_state *newts;
	struct seg6_local_lwt *slwt;
	int err;

	if (family != AF_INET6)
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, SEG6_LOCAL_MAX, nla,
					  seg6_local_policy, extack);

	if (err < 0)
		return err;

	if (!tb[SEG6_LOCAL_ACTION])
		return -EINVAL;

	newts = lwtunnel_state_alloc(sizeof(*slwt));
	if (!newts)
		return -ENOMEM;

	slwt = seg6_local_lwtunnel(newts);
	slwt->action = nla_get_u32(tb[SEG6_LOCAL_ACTION]);

	err = parse_nla_action(tb, slwt);
	if (err < 0)
		goto out_free;

	err = seg6_local_lwtunnel_build_state(slwt, cfg, extack);
	if (err < 0)
		goto out_destroy_attrs;

	newts->type = LWTUNNEL_ENCAP_SEG6_LOCAL;
	newts->flags = LWTUNNEL_STATE_INPUT_REDIRECT;
	newts->headroom = slwt->headroom;

	*ts = newts;

	return 0;

out_destroy_attrs:
	destroy_attrs(slwt);
out_free:
	kfree(newts);
	return err;
}

static void seg6_local_destroy_state(struct lwtunnel_state *lwt)
{
	struct seg6_local_lwt *slwt = seg6_local_lwtunnel(lwt);

	seg6_local_lwtunnel_destroy_state(slwt);

	destroy_attrs(slwt);

	return;
}

static int seg6_local_fill_encap(struct sk_buff *skb,
				 struct lwtunnel_state *lwt)
{
	struct seg6_local_lwt *slwt = seg6_local_lwtunnel(lwt);
	struct seg6_action_param *param;
	unsigned long attrs;
	int i, err;

	if (nla_put_u32(skb, SEG6_LOCAL_ACTION, slwt->action))
		return -EMSGSIZE;

	attrs = slwt->desc->attrs | slwt->parsed_optattrs;

	for (i = 0; i < SEG6_LOCAL_MAX + 1; i++) {
		if (attrs & SEG6_F_ATTR(i)) {
			param = &seg6_action_params[i];
			err = param->put(skb, slwt);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int seg6_local_get_encap_size(struct lwtunnel_state *lwt)
{
	struct seg6_local_lwt *slwt = seg6_local_lwtunnel(lwt);
	unsigned long attrs;
	int nlsize;

	nlsize = nla_total_size(4); /* action */

	attrs = slwt->desc->attrs | slwt->parsed_optattrs;

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_SRH))
		nlsize += nla_total_size((slwt->srh->hdrlen + 1) << 3);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_TABLE))
		nlsize += nla_total_size(4);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_NH4))
		nlsize += nla_total_size(4);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_NH6))
		nlsize += nla_total_size(16);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_IIF))
		nlsize += nla_total_size(4);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_OIF))
		nlsize += nla_total_size(4);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_BPF))
		nlsize += nla_total_size(sizeof(struct nlattr)) +
		       nla_total_size(MAX_PROG_NAME) +
		       nla_total_size(4);

	if (attrs & SEG6_F_ATTR(SEG6_LOCAL_VRFTABLE))
		nlsize += nla_total_size(4);

	if (attrs & SEG6_F_LOCAL_COUNTERS)
		nlsize += nla_total_size(0) + /* nest SEG6_LOCAL_COUNTERS */
			  /* SEG6_LOCAL_CNT_PACKETS */
			  nla_total_size_64bit(sizeof(__u64)) +
			  /* SEG6_LOCAL_CNT_BYTES */
			  nla_total_size_64bit(sizeof(__u64)) +
			  /* SEG6_LOCAL_CNT_ERRORS */
			  nla_total_size_64bit(sizeof(__u64));

	return nlsize;
}

static int seg6_local_cmp_encap(struct lwtunnel_state *a,
				struct lwtunnel_state *b)
{
	struct seg6_local_lwt *slwt_a, *slwt_b;
	struct seg6_action_param *param;
	unsigned long attrs_a, attrs_b;
	int i;

	slwt_a = seg6_local_lwtunnel(a);
	slwt_b = seg6_local_lwtunnel(b);

	if (slwt_a->action != slwt_b->action)
		return 1;

	attrs_a = slwt_a->desc->attrs | slwt_a->parsed_optattrs;
	attrs_b = slwt_b->desc->attrs | slwt_b->parsed_optattrs;

	if (attrs_a != attrs_b)
		return 1;

	for (i = 0; i < SEG6_LOCAL_MAX + 1; i++) {
		if (attrs_a & SEG6_F_ATTR(i)) {
			param = &seg6_action_params[i];
			if (param->cmp(slwt_a, slwt_b))
				return 1;
		}
	}

	return 0;
}

static const struct lwtunnel_encap_ops seg6_local_ops = {
	.build_state	= seg6_local_build_state,
	.destroy_state	= seg6_local_destroy_state,
	.input		= seg6_local_input,
	.fill_encap	= seg6_local_fill_encap,
	.get_encap_size	= seg6_local_get_encap_size,
	.cmp_encap	= seg6_local_cmp_encap,
	.owner		= THIS_MODULE,
};

int __init seg6_local_init(void)
{
	/* If the max total number of defined attributes is reached, then your
	 * kernel build stops here.
	 *
	 * This check is required to avoid arithmetic overflows when processing
	 * behavior attributes and the maximum number of defined attributes
	 * exceeds the allowed value.
	 */
	BUILD_BUG_ON(SEG6_LOCAL_MAX + 1 > BITS_PER_TYPE(unsigned long));

	return lwtunnel_encap_add_ops(&seg6_local_ops,
				      LWTUNNEL_ENCAP_SEG6_LOCAL);
}

void seg6_local_exit(void)
{
	lwtunnel_encap_del_ops(&seg6_local_ops, LWTUNNEL_ENCAP_SEG6_LOCAL);
}
