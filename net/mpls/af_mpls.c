// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/sysctl.h>
#include <linux/net.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/mpls.h>
#include <linux/netconf.h>
#include <linux/nospec.h>
#include <linux/vmalloc.h>
#include <linux/percpu.h>
#include <net/ip.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/ip_fib.h>
#include <net/netevent.h>
#include <net/ip_tunnels.h>
#include <net/netns/generic.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#endif
#include <net/ipv6_stubs.h>
#include <net/rtnh.h>
#include "internal.h"

/* max memory we will use for mpls_route */
#define MAX_MPLS_ROUTE_MEM	4096

/* Maximum number of labels to look ahead at when selecting a path of
 * a multipath route
 */
#define MAX_MP_SELECT_LABELS 4

#define MPLS_NEIGH_TABLE_UNSPEC (NEIGH_LINK_TABLE + 1)

static int label_limit = (1 << 20) - 1;
static int ttl_max = 255;

#if IS_ENABLED(CONFIG_NET_IP_TUNNEL)
static size_t ipgre_mpls_encap_hlen(struct ip_tunnel_encap *e)
{
	return sizeof(struct mpls_shim_hdr);
}

static const struct ip_tunnel_encap_ops mpls_iptun_ops = {
	.encap_hlen	= ipgre_mpls_encap_hlen,
};

static int ipgre_tunnel_encap_add_mpls_ops(void)
{
	return ip_tunnel_encap_add_ops(&mpls_iptun_ops, TUNNEL_ENCAP_MPLS);
}

static void ipgre_tunnel_encap_del_mpls_ops(void)
{
	ip_tunnel_encap_del_ops(&mpls_iptun_ops, TUNNEL_ENCAP_MPLS);
}
#else
static int ipgre_tunnel_encap_add_mpls_ops(void)
{
	return 0;
}

static void ipgre_tunnel_encap_del_mpls_ops(void)
{
}
#endif

static void rtmsg_lfib(int event, u32 label, struct mpls_route *rt,
		       struct nlmsghdr *nlh, struct net *net, u32 portid,
		       unsigned int nlm_flags);

static struct mpls_route *mpls_route_input_rcu(struct net *net, unsigned index)
{
	struct mpls_route *rt = NULL;

	if (index < net->mpls.platform_labels) {
		struct mpls_route __rcu **platform_label =
			rcu_dereference(net->mpls.platform_label);
		rt = rcu_dereference(platform_label[index]);
	}
	return rt;
}

bool mpls_output_possible(const struct net_device *dev)
{
	return dev && (dev->flags & IFF_UP) && netif_carrier_ok(dev);
}
EXPORT_SYMBOL_GPL(mpls_output_possible);

static u8 *__mpls_nh_via(struct mpls_route *rt, struct mpls_nh *nh)
{
	return (u8 *)nh + rt->rt_via_offset;
}

static const u8 *mpls_nh_via(const struct mpls_route *rt,
			     const struct mpls_nh *nh)
{
	return __mpls_nh_via((struct mpls_route *)rt, (struct mpls_nh *)nh);
}

static unsigned int mpls_nh_header_size(const struct mpls_nh *nh)
{
	/* The size of the layer 2.5 labels to be added for this route */
	return nh->nh_labels * sizeof(struct mpls_shim_hdr);
}

unsigned int mpls_dev_mtu(const struct net_device *dev)
{
	/* The amount of data the layer 2 frame can hold */
	return dev->mtu;
}
EXPORT_SYMBOL_GPL(mpls_dev_mtu);

bool mpls_pkt_too_big(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	if (skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu))
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(mpls_pkt_too_big);

void mpls_stats_inc_outucastpkts(struct net_device *dev,
				 const struct sk_buff *skb)
{
	struct mpls_dev *mdev;

	if (skb->protocol == htons(ETH_P_MPLS_UC)) {
		mdev = mpls_dev_get(dev);
		if (mdev)
			MPLS_INC_STATS_LEN(mdev, skb->len,
					   tx_packets,
					   tx_bytes);
	} else if (skb->protocol == htons(ETH_P_IP)) {
		IP_UPD_PO_STATS(dev_net(dev), IPSTATS_MIB_OUT, skb->len);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct inet6_dev *in6dev = __in6_dev_get(dev);

		if (in6dev)
			IP6_UPD_PO_STATS(dev_net(dev), in6dev,
					 IPSTATS_MIB_OUT, skb->len);
#endif
	}
}
EXPORT_SYMBOL_GPL(mpls_stats_inc_outucastpkts);

static u32 mpls_multipath_hash(struct mpls_route *rt, struct sk_buff *skb)
{
	struct mpls_entry_decoded dec;
	unsigned int mpls_hdr_len = 0;
	struct mpls_shim_hdr *hdr;
	bool eli_seen = false;
	int label_index;
	u32 hash = 0;

	for (label_index = 0; label_index < MAX_MP_SELECT_LABELS;
	     label_index++) {
		mpls_hdr_len += sizeof(*hdr);
		if (!pskb_may_pull(skb, mpls_hdr_len))
			break;

		/* Read and decode the current label */
		hdr = mpls_hdr(skb) + label_index;
		dec = mpls_entry_decode(hdr);

		/* RFC6790 - reserved labels MUST NOT be used as keys
		 * for the load-balancing function
		 */
		if (likely(dec.label >= MPLS_LABEL_FIRST_UNRESERVED)) {
			hash = jhash_1word(dec.label, hash);

			/* The entropy label follows the entropy label
			 * indicator, so this means that the entropy
			 * label was just added to the hash - no need to
			 * go any deeper either in the label stack or in the
			 * payload
			 */
			if (eli_seen)
				break;
		} else if (dec.label == MPLS_LABEL_ENTROPY) {
			eli_seen = true;
		}

		if (!dec.bos)
			continue;

		/* found bottom label; does skb have room for a header? */
		if (pskb_may_pull(skb, mpls_hdr_len + sizeof(struct iphdr))) {
			const struct iphdr *v4hdr;

			v4hdr = (const struct iphdr *)(hdr + 1);
			if (v4hdr->version == 4) {
				hash = jhash_3words(ntohl(v4hdr->saddr),
						    ntohl(v4hdr->daddr),
						    v4hdr->protocol, hash);
			} else if (v4hdr->version == 6 &&
				   pskb_may_pull(skb, mpls_hdr_len +
						 sizeof(struct ipv6hdr))) {
				const struct ipv6hdr *v6hdr;

				v6hdr = (const struct ipv6hdr *)(hdr + 1);
				hash = __ipv6_addr_jhash(&v6hdr->saddr, hash);
				hash = __ipv6_addr_jhash(&v6hdr->daddr, hash);
				hash = jhash_1word(v6hdr->nexthdr, hash);
			}
		}

		break;
	}

	return hash;
}

static struct mpls_nh *mpls_get_nexthop(struct mpls_route *rt, u8 index)
{
	return (struct mpls_nh *)((u8 *)rt->rt_nh + index * rt->rt_nh_size);
}

/* number of alive nexthops (rt->rt_nhn_alive) and the flags for
 * a next hop (nh->nh_flags) are modified by netdev event handlers.
 * Since those fields can change at any moment, use READ_ONCE to
 * access both.
 */
static struct mpls_nh *mpls_select_multipath(struct mpls_route *rt,
					     struct sk_buff *skb)
{
	u32 hash = 0;
	int nh_index = 0;
	int n = 0;
	u8 alive;

	/* No need to look further into packet if there's only
	 * one path
	 */
	if (rt->rt_nhn == 1)
		return rt->rt_nh;

	alive = READ_ONCE(rt->rt_nhn_alive);
	if (alive == 0)
		return NULL;

	hash = mpls_multipath_hash(rt, skb);
	nh_index = hash % alive;
	if (alive == rt->rt_nhn)
		goto out;
	for_nexthops(rt) {
		unsigned int nh_flags = READ_ONCE(nh->nh_flags);

		if (nh_flags & (RTNH_F_DEAD | RTNH_F_LINKDOWN))
			continue;
		if (n == nh_index)
			return nh;
		n++;
	} endfor_nexthops(rt);

out:
	return mpls_get_nexthop(rt, nh_index);
}

static bool mpls_egress(struct net *net, struct mpls_route *rt,
			struct sk_buff *skb, struct mpls_entry_decoded dec)
{
	enum mpls_payload_type payload_type;
	bool success = false;

	/* The IPv4 code below accesses through the IPv4 header
	 * checksum, which is 12 bytes into the packet.
	 * The IPv6 code below accesses through the IPv6 hop limit
	 * which is 8 bytes into the packet.
	 *
	 * For all supported cases there should always be at least 12
	 * bytes of packet data present.  The IPv4 header is 20 bytes
	 * without options and the IPv6 header is always 40 bytes
	 * long.
	 */
	if (!pskb_may_pull(skb, 12))
		return false;

	payload_type = rt->rt_payload_type;
	if (payload_type == MPT_UNSPEC)
		payload_type = ip_hdr(skb)->version;

	switch (payload_type) {
	case MPT_IPV4: {
		struct iphdr *hdr4 = ip_hdr(skb);
		u8 new_ttl;
		skb->protocol = htons(ETH_P_IP);

		/* If propagating TTL, take the decremented TTL from
		 * the incoming MPLS header, otherwise decrement the
		 * TTL, but only if not 0 to avoid underflow.
		 */
		if (rt->rt_ttl_propagate == MPLS_TTL_PROP_ENABLED ||
		    (rt->rt_ttl_propagate == MPLS_TTL_PROP_DEFAULT &&
		     net->mpls.ip_ttl_propagate))
			new_ttl = dec.ttl;
		else
			new_ttl = hdr4->ttl ? hdr4->ttl - 1 : 0;

		csum_replace2(&hdr4->check,
			      htons(hdr4->ttl << 8),
			      htons(new_ttl << 8));
		hdr4->ttl = new_ttl;
		success = true;
		break;
	}
	case MPT_IPV6: {
		struct ipv6hdr *hdr6 = ipv6_hdr(skb);
		skb->protocol = htons(ETH_P_IPV6);

		/* If propagating TTL, take the decremented TTL from
		 * the incoming MPLS header, otherwise decrement the
		 * hop limit, but only if not 0 to avoid underflow.
		 */
		if (rt->rt_ttl_propagate == MPLS_TTL_PROP_ENABLED ||
		    (rt->rt_ttl_propagate == MPLS_TTL_PROP_DEFAULT &&
		     net->mpls.ip_ttl_propagate))
			hdr6->hop_limit = dec.ttl;
		else if (hdr6->hop_limit)
			hdr6->hop_limit = hdr6->hop_limit - 1;
		success = true;
		break;
	}
	case MPT_UNSPEC:
		/* Should have decided which protocol it is by now */
		break;
	}

	return success;
}

static int mpls_forward(struct sk_buff *skb, struct net_device *dev,
			struct packet_type *pt, struct net_device *orig_dev)
{
	struct net *net = dev_net(dev);
	struct mpls_shim_hdr *hdr;
	struct mpls_route *rt;
	struct mpls_nh *nh;
	struct mpls_entry_decoded dec;
	struct net_device *out_dev;
	struct mpls_dev *out_mdev;
	struct mpls_dev *mdev;
	unsigned int hh_len;
	unsigned int new_header_size;
	unsigned int mtu;
	int err;

	/* Careful this entire function runs inside of an rcu critical section */

	mdev = mpls_dev_get(dev);
	if (!mdev)
		goto drop;

	MPLS_INC_STATS_LEN(mdev, skb->len, rx_packets,
			   rx_bytes);

	if (!mdev->input_enabled) {
		MPLS_INC_STATS(mdev, rx_dropped);
		goto drop;
	}

	if (skb->pkt_type != PACKET_HOST)
		goto err;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		goto err;

	if (!pskb_may_pull(skb, sizeof(*hdr)))
		goto err;

	skb_dst_drop(skb);

	/* Read and decode the label */
	hdr = mpls_hdr(skb);
	dec = mpls_entry_decode(hdr);

	rt = mpls_route_input_rcu(net, dec.label);
	if (!rt) {
		MPLS_INC_STATS(mdev, rx_noroute);
		goto drop;
	}

	nh = mpls_select_multipath(rt, skb);
	if (!nh)
		goto err;

	/* Pop the label */
	skb_pull(skb, sizeof(*hdr));
	skb_reset_network_header(skb);

	skb_orphan(skb);

	if (skb_warn_if_lro(skb))
		goto err;

	skb_forward_csum(skb);

	/* Verify ttl is valid */
	if (dec.ttl <= 1)
		goto err;

	/* Find the output device */
	out_dev = rcu_dereference(nh->nh_dev);
	if (!mpls_output_possible(out_dev))
		goto tx_err;

	/* Verify the destination can hold the packet */
	new_header_size = mpls_nh_header_size(nh);
	mtu = mpls_dev_mtu(out_dev);
	if (mpls_pkt_too_big(skb, mtu - new_header_size))
		goto tx_err;

	hh_len = LL_RESERVED_SPACE(out_dev);
	if (!out_dev->header_ops)
		hh_len = 0;

	/* Ensure there is enough space for the headers in the skb */
	if (skb_cow(skb, hh_len + new_header_size))
		goto tx_err;

	skb->dev = out_dev;
	skb->protocol = htons(ETH_P_MPLS_UC);

	dec.ttl -= 1;
	if (unlikely(!new_header_size && dec.bos)) {
		/* Penultimate hop popping */
		if (!mpls_egress(dev_net(out_dev), rt, skb, dec))
			goto err;
	} else {
		bool bos;
		int i;
		skb_push(skb, new_header_size);
		skb_reset_network_header(skb);
		/* Push the new labels */
		hdr = mpls_hdr(skb);
		bos = dec.bos;
		for (i = nh->nh_labels - 1; i >= 0; i--) {
			hdr[i] = mpls_entry_encode(nh->nh_label[i],
						   dec.ttl, 0, bos);
			bos = false;
		}
	}

	mpls_stats_inc_outucastpkts(out_dev, skb);

	/* If via wasn't specified then send out using device address */
	if (nh->nh_via_table == MPLS_NEIGH_TABLE_UNSPEC)
		err = neigh_xmit(NEIGH_LINK_TABLE, out_dev,
				 out_dev->dev_addr, skb);
	else
		err = neigh_xmit(nh->nh_via_table, out_dev,
				 mpls_nh_via(rt, nh), skb);
	if (err)
		net_dbg_ratelimited("%s: packet transmission failed: %d\n",
				    __func__, err);
	return 0;

tx_err:
	out_mdev = out_dev ? mpls_dev_get(out_dev) : NULL;
	if (out_mdev)
		MPLS_INC_STATS(out_mdev, tx_errors);
	goto drop;
err:
	MPLS_INC_STATS(mdev, rx_errors);
drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct packet_type mpls_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_MPLS_UC),
	.func = mpls_forward,
};

static const struct nla_policy rtm_mpls_policy[RTA_MAX+1] = {
	[RTA_DST]		= { .type = NLA_U32 },
	[RTA_OIF]		= { .type = NLA_U32 },
	[RTA_TTL_PROPAGATE]	= { .type = NLA_U8 },
};

struct mpls_route_config {
	u32			rc_protocol;
	u32			rc_ifindex;
	u8			rc_via_table;
	u8			rc_via_alen;
	u8			rc_via[MAX_VIA_ALEN];
	u32			rc_label;
	u8			rc_ttl_propagate;
	u8			rc_output_labels;
	u32			rc_output_label[MAX_NEW_LABELS];
	u32			rc_nlflags;
	enum mpls_payload_type	rc_payload_type;
	struct nl_info		rc_nlinfo;
	struct rtnexthop	*rc_mp;
	int			rc_mp_len;
};

/* all nexthops within a route have the same size based on max
 * number of labels and max via length for a hop
 */
static struct mpls_route *mpls_rt_alloc(u8 num_nh, u8 max_alen, u8 max_labels)
{
	u8 nh_size = MPLS_NH_SIZE(max_labels, max_alen);
	struct mpls_route *rt;
	size_t size;

	size = sizeof(*rt) + num_nh * nh_size;
	if (size > MAX_MPLS_ROUTE_MEM)
		return ERR_PTR(-EINVAL);

	rt = kzalloc(size, GFP_KERNEL);
	if (!rt)
		return ERR_PTR(-ENOMEM);

	rt->rt_nhn = num_nh;
	rt->rt_nhn_alive = num_nh;
	rt->rt_nh_size = nh_size;
	rt->rt_via_offset = MPLS_NH_VIA_OFF(max_labels);

	return rt;
}

static void mpls_rt_free(struct mpls_route *rt)
{
	if (rt)
		kfree_rcu(rt, rt_rcu);
}

static void mpls_notify_route(struct net *net, unsigned index,
			      struct mpls_route *old, struct mpls_route *new,
			      const struct nl_info *info)
{
	struct nlmsghdr *nlh = info ? info->nlh : NULL;
	unsigned portid = info ? info->portid : 0;
	int event = new ? RTM_NEWROUTE : RTM_DELROUTE;
	struct mpls_route *rt = new ? new : old;
	unsigned nlm_flags = (old && new) ? NLM_F_REPLACE : 0;
	/* Ignore reserved labels for now */
	if (rt && (index >= MPLS_LABEL_FIRST_UNRESERVED))
		rtmsg_lfib(event, index, rt, nlh, net, portid, nlm_flags);
}

static void mpls_route_update(struct net *net, unsigned index,
			      struct mpls_route *new,
			      const struct nl_info *info)
{
	struct mpls_route __rcu **platform_label;
	struct mpls_route *rt;

	ASSERT_RTNL();

	platform_label = rtnl_dereference(net->mpls.platform_label);
	rt = rtnl_dereference(platform_label[index]);
	rcu_assign_pointer(platform_label[index], new);

	mpls_notify_route(net, index, rt, new, info);

	/* If we removed a route free it now */
	mpls_rt_free(rt);
}

static unsigned find_free_label(struct net *net)
{
	struct mpls_route __rcu **platform_label;
	size_t platform_labels;
	unsigned index;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	platform_labels = net->mpls.platform_labels;
	for (index = MPLS_LABEL_FIRST_UNRESERVED; index < platform_labels;
	     index++) {
		if (!rtnl_dereference(platform_label[index]))
			return index;
	}
	return LABEL_NOT_SPECIFIED;
}

#if IS_ENABLED(CONFIG_INET)
static struct net_device *inet_fib_lookup_dev(struct net *net,
					      const void *addr)
{
	struct net_device *dev;
	struct rtable *rt;
	struct in_addr daddr;

	memcpy(&daddr, addr, sizeof(struct in_addr));
	rt = ip_route_output(net, daddr.s_addr, 0, 0, 0);
	if (IS_ERR(rt))
		return ERR_CAST(rt);

	dev = rt->dst.dev;
	dev_hold(dev);

	ip_rt_put(rt);

	return dev;
}
#else
static struct net_device *inet_fib_lookup_dev(struct net *net,
					      const void *addr)
{
	return ERR_PTR(-EAFNOSUPPORT);
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static struct net_device *inet6_fib_lookup_dev(struct net *net,
					       const void *addr)
{
	struct net_device *dev;
	struct dst_entry *dst;
	struct flowi6 fl6;

	if (!ipv6_stub)
		return ERR_PTR(-EAFNOSUPPORT);

	memset(&fl6, 0, sizeof(fl6));
	memcpy(&fl6.daddr, addr, sizeof(struct in6_addr));
	dst = ipv6_stub->ipv6_dst_lookup_flow(net, NULL, &fl6, NULL);
	if (IS_ERR(dst))
		return ERR_CAST(dst);

	dev = dst->dev;
	dev_hold(dev);
	dst_release(dst);

	return dev;
}
#else
static struct net_device *inet6_fib_lookup_dev(struct net *net,
					       const void *addr)
{
	return ERR_PTR(-EAFNOSUPPORT);
}
#endif

static struct net_device *find_outdev(struct net *net,
				      struct mpls_route *rt,
				      struct mpls_nh *nh, int oif)
{
	struct net_device *dev = NULL;

	if (!oif) {
		switch (nh->nh_via_table) {
		case NEIGH_ARP_TABLE:
			dev = inet_fib_lookup_dev(net, mpls_nh_via(rt, nh));
			break;
		case NEIGH_ND_TABLE:
			dev = inet6_fib_lookup_dev(net, mpls_nh_via(rt, nh));
			break;
		case NEIGH_LINK_TABLE:
			break;
		}
	} else {
		dev = dev_get_by_index(net, oif);
	}

	if (!dev)
		return ERR_PTR(-ENODEV);

	if (IS_ERR(dev))
		return dev;

	/* The caller is holding rtnl anyways, so release the dev reference */
	dev_put(dev);

	return dev;
}

static int mpls_nh_assign_dev(struct net *net, struct mpls_route *rt,
			      struct mpls_nh *nh, int oif)
{
	struct net_device *dev = NULL;
	int err = -ENODEV;

	dev = find_outdev(net, rt, nh, oif);
	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		dev = NULL;
		goto errout;
	}

	/* Ensure this is a supported device */
	err = -EINVAL;
	if (!mpls_dev_get(dev))
		goto errout;

	if ((nh->nh_via_table == NEIGH_LINK_TABLE) &&
	    (dev->addr_len != nh->nh_via_alen))
		goto errout;

	RCU_INIT_POINTER(nh->nh_dev, dev);

	if (!(dev->flags & IFF_UP)) {
		nh->nh_flags |= RTNH_F_DEAD;
	} else {
		unsigned int flags;

		flags = dev_get_flags(dev);
		if (!(flags & (IFF_RUNNING | IFF_LOWER_UP)))
			nh->nh_flags |= RTNH_F_LINKDOWN;
	}

	return 0;

errout:
	return err;
}

static int nla_get_via(const struct nlattr *nla, u8 *via_alen, u8 *via_table,
		       u8 via_addr[], struct netlink_ext_ack *extack)
{
	struct rtvia *via = nla_data(nla);
	int err = -EINVAL;
	int alen;

	if (nla_len(nla) < offsetof(struct rtvia, rtvia_addr)) {
		NL_SET_ERR_MSG_ATTR(extack, nla,
				    "Invalid attribute length for RTA_VIA");
		goto errout;
	}
	alen = nla_len(nla) -
			offsetof(struct rtvia, rtvia_addr);
	if (alen > MAX_VIA_ALEN) {
		NL_SET_ERR_MSG_ATTR(extack, nla,
				    "Invalid address length for RTA_VIA");
		goto errout;
	}

	/* Validate the address family */
	switch (via->rtvia_family) {
	case AF_PACKET:
		*via_table = NEIGH_LINK_TABLE;
		break;
	case AF_INET:
		*via_table = NEIGH_ARP_TABLE;
		if (alen != 4)
			goto errout;
		break;
	case AF_INET6:
		*via_table = NEIGH_ND_TABLE;
		if (alen != 16)
			goto errout;
		break;
	default:
		/* Unsupported address family */
		goto errout;
	}

	memcpy(via_addr, via->rtvia_addr, alen);
	*via_alen = alen;
	err = 0;

errout:
	return err;
}

static int mpls_nh_build_from_cfg(struct mpls_route_config *cfg,
				  struct mpls_route *rt)
{
	struct net *net = cfg->rc_nlinfo.nl_net;
	struct mpls_nh *nh = rt->rt_nh;
	int err;
	int i;

	if (!nh)
		return -ENOMEM;

	nh->nh_labels = cfg->rc_output_labels;
	for (i = 0; i < nh->nh_labels; i++)
		nh->nh_label[i] = cfg->rc_output_label[i];

	nh->nh_via_table = cfg->rc_via_table;
	memcpy(__mpls_nh_via(rt, nh), cfg->rc_via, cfg->rc_via_alen);
	nh->nh_via_alen = cfg->rc_via_alen;

	err = mpls_nh_assign_dev(net, rt, nh, cfg->rc_ifindex);
	if (err)
		goto errout;

	if (nh->nh_flags & (RTNH_F_DEAD | RTNH_F_LINKDOWN))
		rt->rt_nhn_alive--;

	return 0;

errout:
	return err;
}

static int mpls_nh_build(struct net *net, struct mpls_route *rt,
			 struct mpls_nh *nh, int oif, struct nlattr *via,
			 struct nlattr *newdst, u8 max_labels,
			 struct netlink_ext_ack *extack)
{
	int err = -ENOMEM;

	if (!nh)
		goto errout;

	if (newdst) {
		err = nla_get_labels(newdst, max_labels, &nh->nh_labels,
				     nh->nh_label, extack);
		if (err)
			goto errout;
	}

	if (via) {
		err = nla_get_via(via, &nh->nh_via_alen, &nh->nh_via_table,
				  __mpls_nh_via(rt, nh), extack);
		if (err)
			goto errout;
	} else {
		nh->nh_via_table = MPLS_NEIGH_TABLE_UNSPEC;
	}

	err = mpls_nh_assign_dev(net, rt, nh, oif);
	if (err)
		goto errout;

	return 0;

errout:
	return err;
}

static u8 mpls_count_nexthops(struct rtnexthop *rtnh, int len,
			      u8 cfg_via_alen, u8 *max_via_alen,
			      u8 *max_labels)
{
	int remaining = len;
	u8 nhs = 0;

	*max_via_alen = 0;
	*max_labels = 0;

	while (rtnh_ok(rtnh, remaining)) {
		struct nlattr *nla, *attrs = rtnh_attrs(rtnh);
		int attrlen;
		u8 n_labels = 0;

		attrlen = rtnh_attrlen(rtnh);
		nla = nla_find(attrs, attrlen, RTA_VIA);
		if (nla && nla_len(nla) >=
		    offsetof(struct rtvia, rtvia_addr)) {
			int via_alen = nla_len(nla) -
				offsetof(struct rtvia, rtvia_addr);

			if (via_alen <= MAX_VIA_ALEN)
				*max_via_alen = max_t(u16, *max_via_alen,
						      via_alen);
		}

		nla = nla_find(attrs, attrlen, RTA_NEWDST);
		if (nla &&
		    nla_get_labels(nla, MAX_NEW_LABELS, &n_labels,
				   NULL, NULL) != 0)
			return 0;

		*max_labels = max_t(u8, *max_labels, n_labels);

		/* number of nexthops is tracked by a u8.
		 * Check for overflow.
		 */
		if (nhs == 255)
			return 0;
		nhs++;

		rtnh = rtnh_next(rtnh, &remaining);
	}

	/* leftover implies invalid nexthop configuration, discard it */
	return remaining > 0 ? 0 : nhs;
}

static int mpls_nh_build_multi(struct mpls_route_config *cfg,
			       struct mpls_route *rt, u8 max_labels,
			       struct netlink_ext_ack *extack)
{
	struct rtnexthop *rtnh = cfg->rc_mp;
	struct nlattr *nla_via, *nla_newdst;
	int remaining = cfg->rc_mp_len;
	int err = 0;
	u8 nhs = 0;

	change_nexthops(rt) {
		int attrlen;

		nla_via = NULL;
		nla_newdst = NULL;

		err = -EINVAL;
		if (!rtnh_ok(rtnh, remaining))
			goto errout;

		/* neither weighted multipath nor any flags
		 * are supported
		 */
		if (rtnh->rtnh_hops || rtnh->rtnh_flags)
			goto errout;

		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			struct nlattr *attrs = rtnh_attrs(rtnh);

			nla_via = nla_find(attrs, attrlen, RTA_VIA);
			nla_newdst = nla_find(attrs, attrlen, RTA_NEWDST);
		}

		err = mpls_nh_build(cfg->rc_nlinfo.nl_net, rt, nh,
				    rtnh->rtnh_ifindex, nla_via, nla_newdst,
				    max_labels, extack);
		if (err)
			goto errout;

		if (nh->nh_flags & (RTNH_F_DEAD | RTNH_F_LINKDOWN))
			rt->rt_nhn_alive--;

		rtnh = rtnh_next(rtnh, &remaining);
		nhs++;
	} endfor_nexthops(rt);

	rt->rt_nhn = nhs;

	return 0;

errout:
	return err;
}

static bool mpls_label_ok(struct net *net, unsigned int *index,
			  struct netlink_ext_ack *extack)
{
	bool is_ok = true;

	/* Reserved labels may not be set */
	if (*index < MPLS_LABEL_FIRST_UNRESERVED) {
		NL_SET_ERR_MSG(extack,
			       "Invalid label - must be MPLS_LABEL_FIRST_UNRESERVED or higher");
		is_ok = false;
	}

	/* The full 20 bit range may not be supported. */
	if (is_ok && *index >= net->mpls.platform_labels) {
		NL_SET_ERR_MSG(extack,
			       "Label >= configured maximum in platform_labels");
		is_ok = false;
	}

	*index = array_index_nospec(*index, net->mpls.platform_labels);
	return is_ok;
}

static int mpls_route_add(struct mpls_route_config *cfg,
			  struct netlink_ext_ack *extack)
{
	struct mpls_route __rcu **platform_label;
	struct net *net = cfg->rc_nlinfo.nl_net;
	struct mpls_route *rt, *old;
	int err = -EINVAL;
	u8 max_via_alen;
	unsigned index;
	u8 max_labels;
	u8 nhs;

	index = cfg->rc_label;

	/* If a label was not specified during insert pick one */
	if ((index == LABEL_NOT_SPECIFIED) &&
	    (cfg->rc_nlflags & NLM_F_CREATE)) {
		index = find_free_label(net);
	}

	if (!mpls_label_ok(net, &index, extack))
		goto errout;

	/* Append makes no sense with mpls */
	err = -EOPNOTSUPP;
	if (cfg->rc_nlflags & NLM_F_APPEND) {
		NL_SET_ERR_MSG(extack, "MPLS does not support route append");
		goto errout;
	}

	err = -EEXIST;
	platform_label = rtnl_dereference(net->mpls.platform_label);
	old = rtnl_dereference(platform_label[index]);
	if ((cfg->rc_nlflags & NLM_F_EXCL) && old)
		goto errout;

	err = -EEXIST;
	if (!(cfg->rc_nlflags & NLM_F_REPLACE) && old)
		goto errout;

	err = -ENOENT;
	if (!(cfg->rc_nlflags & NLM_F_CREATE) && !old)
		goto errout;

	err = -EINVAL;
	if (cfg->rc_mp) {
		nhs = mpls_count_nexthops(cfg->rc_mp, cfg->rc_mp_len,
					  cfg->rc_via_alen, &max_via_alen,
					  &max_labels);
	} else {
		max_via_alen = cfg->rc_via_alen;
		max_labels = cfg->rc_output_labels;
		nhs = 1;
	}

	if (nhs == 0) {
		NL_SET_ERR_MSG(extack, "Route does not contain a nexthop");
		goto errout;
	}

	rt = mpls_rt_alloc(nhs, max_via_alen, max_labels);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		goto errout;
	}

	rt->rt_protocol = cfg->rc_protocol;
	rt->rt_payload_type = cfg->rc_payload_type;
	rt->rt_ttl_propagate = cfg->rc_ttl_propagate;

	if (cfg->rc_mp)
		err = mpls_nh_build_multi(cfg, rt, max_labels, extack);
	else
		err = mpls_nh_build_from_cfg(cfg, rt);
	if (err)
		goto freert;

	mpls_route_update(net, index, rt, &cfg->rc_nlinfo);

	return 0;

freert:
	mpls_rt_free(rt);
errout:
	return err;
}

static int mpls_route_del(struct mpls_route_config *cfg,
			  struct netlink_ext_ack *extack)
{
	struct net *net = cfg->rc_nlinfo.nl_net;
	unsigned index;
	int err = -EINVAL;

	index = cfg->rc_label;

	if (!mpls_label_ok(net, &index, extack))
		goto errout;

	mpls_route_update(net, index, NULL, &cfg->rc_nlinfo);

	err = 0;
errout:
	return err;
}

static void mpls_get_stats(struct mpls_dev *mdev,
			   struct mpls_link_stats *stats)
{
	struct mpls_pcpu_stats *p;
	int i;

	memset(stats, 0, sizeof(*stats));

	for_each_possible_cpu(i) {
		struct mpls_link_stats local;
		unsigned int start;

		p = per_cpu_ptr(mdev->stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			local = p->stats;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));

		stats->rx_packets	+= local.rx_packets;
		stats->rx_bytes		+= local.rx_bytes;
		stats->tx_packets	+= local.tx_packets;
		stats->tx_bytes		+= local.tx_bytes;
		stats->rx_errors	+= local.rx_errors;
		stats->tx_errors	+= local.tx_errors;
		stats->rx_dropped	+= local.rx_dropped;
		stats->tx_dropped	+= local.tx_dropped;
		stats->rx_noroute	+= local.rx_noroute;
	}
}

static int mpls_fill_stats_af(struct sk_buff *skb,
			      const struct net_device *dev)
{
	struct mpls_link_stats *stats;
	struct mpls_dev *mdev;
	struct nlattr *nla;

	mdev = mpls_dev_get(dev);
	if (!mdev)
		return -ENODATA;

	nla = nla_reserve_64bit(skb, MPLS_STATS_LINK,
				sizeof(struct mpls_link_stats),
				MPLS_STATS_UNSPEC);
	if (!nla)
		return -EMSGSIZE;

	stats = nla_data(nla);
	mpls_get_stats(mdev, stats);

	return 0;
}

static size_t mpls_get_stats_af_size(const struct net_device *dev)
{
	struct mpls_dev *mdev;

	mdev = mpls_dev_get(dev);
	if (!mdev)
		return 0;

	return nla_total_size_64bit(sizeof(struct mpls_link_stats));
}

static int mpls_netconf_fill_devconf(struct sk_buff *skb, struct mpls_dev *mdev,
				     u32 portid, u32 seq, int event,
				     unsigned int flags, int type)
{
	struct nlmsghdr  *nlh;
	struct netconfmsg *ncm;
	bool all = false;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(struct netconfmsg),
			flags);
	if (!nlh)
		return -EMSGSIZE;

	if (type == NETCONFA_ALL)
		all = true;

	ncm = nlmsg_data(nlh);
	ncm->ncm_family = AF_MPLS;

	if (nla_put_s32(skb, NETCONFA_IFINDEX, mdev->dev->ifindex) < 0)
		goto nla_put_failure;

	if ((all || type == NETCONFA_INPUT) &&
	    nla_put_s32(skb, NETCONFA_INPUT,
			mdev->input_enabled) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mpls_netconf_msgsize_devconf(int type)
{
	int size = NLMSG_ALIGN(sizeof(struct netconfmsg))
			+ nla_total_size(4); /* NETCONFA_IFINDEX */
	bool all = false;

	if (type == NETCONFA_ALL)
		all = true;

	if (all || type == NETCONFA_INPUT)
		size += nla_total_size(4);

	return size;
}

static void mpls_netconf_notify_devconf(struct net *net, int event,
					int type, struct mpls_dev *mdev)
{
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(mpls_netconf_msgsize_devconf(type), GFP_KERNEL);
	if (!skb)
		goto errout;

	err = mpls_netconf_fill_devconf(skb, mdev, 0, 0, event, 0, type);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in mpls_netconf_msgsize_devconf() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_MPLS_NETCONF, NULL, GFP_KERNEL);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_MPLS_NETCONF, err);
}

static const struct nla_policy devconf_mpls_policy[NETCONFA_MAX + 1] = {
	[NETCONFA_IFINDEX]	= { .len = sizeof(int) },
};

static int mpls_netconf_valid_get_req(struct sk_buff *skb,
				      const struct nlmsghdr *nlh,
				      struct nlattr **tb,
				      struct netlink_ext_ack *extack)
{
	int i, err;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(struct netconfmsg))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid header for netconf get request");
		return -EINVAL;
	}

	if (!netlink_strict_get_check(skb))
		return nlmsg_parse_deprecated(nlh, sizeof(struct netconfmsg),
					      tb, NETCONFA_MAX,
					      devconf_mpls_policy, extack);

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(struct netconfmsg),
					    tb, NETCONFA_MAX,
					    devconf_mpls_policy, extack);
	if (err)
		return err;

	for (i = 0; i <= NETCONFA_MAX; i++) {
		if (!tb[i])
			continue;

		switch (i) {
		case NETCONFA_IFINDEX:
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Unsupported attribute in netconf get request");
			return -EINVAL;
		}
	}

	return 0;
}

static int mpls_netconf_get_devconf(struct sk_buff *in_skb,
				    struct nlmsghdr *nlh,
				    struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct nlattr *tb[NETCONFA_MAX + 1];
	struct net_device *dev;
	struct mpls_dev *mdev;
	struct sk_buff *skb;
	int ifindex;
	int err;

	err = mpls_netconf_valid_get_req(in_skb, nlh, tb, extack);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	if (!tb[NETCONFA_IFINDEX])
		goto errout;

	ifindex = nla_get_s32(tb[NETCONFA_IFINDEX]);
	dev = __dev_get_by_index(net, ifindex);
	if (!dev)
		goto errout;

	mdev = mpls_dev_get(dev);
	if (!mdev)
		goto errout;

	err = -ENOBUFS;
	skb = nlmsg_new(mpls_netconf_msgsize_devconf(NETCONFA_ALL), GFP_KERNEL);
	if (!skb)
		goto errout;

	err = mpls_netconf_fill_devconf(skb, mdev,
					NETLINK_CB(in_skb).portid,
					nlh->nlmsg_seq, RTM_NEWNETCONF, 0,
					NETCONFA_ALL);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in mpls_netconf_msgsize_devconf() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);
errout:
	return err;
}

static int mpls_netconf_dump_devconf(struct sk_buff *skb,
				     struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct net *net = sock_net(skb->sk);
	struct hlist_head *head;
	struct net_device *dev;
	struct mpls_dev *mdev;
	int idx, s_idx;
	int h, s_h;

	if (cb->strict_check) {
		struct netlink_ext_ack *extack = cb->extack;
		struct netconfmsg *ncm;

		if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*ncm))) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid header for netconf dump request");
			return -EINVAL;
		}

		if (nlmsg_attrlen(nlh, sizeof(*ncm))) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid data after header in netconf dump request");
			return -EINVAL;
		}
	}

	s_h = cb->args[0];
	s_idx = idx = cb->args[1];

	for (h = s_h; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		idx = 0;
		head = &net->dev_index_head[h];
		rcu_read_lock();
		cb->seq = net->dev_base_seq;
		hlist_for_each_entry_rcu(dev, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			mdev = mpls_dev_get(dev);
			if (!mdev)
				goto cont;
			if (mpls_netconf_fill_devconf(skb, mdev,
						      NETLINK_CB(cb->skb).portid,
						      nlh->nlmsg_seq,
						      RTM_NEWNETCONF,
						      NLM_F_MULTI,
						      NETCONFA_ALL) < 0) {
				rcu_read_unlock();
				goto done;
			}
			nl_dump_check_consistent(cb, nlmsg_hdr(skb));
cont:
			idx++;
		}
		rcu_read_unlock();
	}
done:
	cb->args[0] = h;
	cb->args[1] = idx;

	return skb->len;
}

#define MPLS_PERDEV_SYSCTL_OFFSET(field)	\
	(&((struct mpls_dev *)0)->field)

static int mpls_conf_proc(struct ctl_table *ctl, int write,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	int oval = *(int *)ctl->data;
	int ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (write) {
		struct mpls_dev *mdev = ctl->extra1;
		int i = (int *)ctl->data - (int *)mdev;
		struct net *net = ctl->extra2;
		int val = *(int *)ctl->data;

		if (i == offsetof(struct mpls_dev, input_enabled) &&
		    val != oval) {
			mpls_netconf_notify_devconf(net, RTM_NEWNETCONF,
						    NETCONFA_INPUT, mdev);
		}
	}

	return ret;
}

static const struct ctl_table mpls_dev_table[] = {
	{
		.procname	= "input",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= mpls_conf_proc,
		.data		= MPLS_PERDEV_SYSCTL_OFFSET(input_enabled),
	},
	{ }
};

static int mpls_dev_sysctl_register(struct net_device *dev,
				    struct mpls_dev *mdev)
{
	char path[sizeof("net/mpls/conf/") + IFNAMSIZ];
	struct net *net = dev_net(dev);
	struct ctl_table *table;
	int i;

	table = kmemdup(&mpls_dev_table, sizeof(mpls_dev_table), GFP_KERNEL);
	if (!table)
		goto out;

	/* Table data contains only offsets relative to the base of
	 * the mdev at this point, so make them absolute.
	 */
	for (i = 0; i < ARRAY_SIZE(mpls_dev_table); i++) {
		table[i].data = (char *)mdev + (uintptr_t)table[i].data;
		table[i].extra1 = mdev;
		table[i].extra2 = net;
	}

	snprintf(path, sizeof(path), "net/mpls/conf/%s", dev->name);

	mdev->sysctl = register_net_sysctl(net, path, table);
	if (!mdev->sysctl)
		goto free;

	mpls_netconf_notify_devconf(net, RTM_NEWNETCONF, NETCONFA_ALL, mdev);
	return 0;

free:
	kfree(table);
out:
	return -ENOBUFS;
}

static void mpls_dev_sysctl_unregister(struct net_device *dev,
				       struct mpls_dev *mdev)
{
	struct net *net = dev_net(dev);
	struct ctl_table *table;

	table = mdev->sysctl->ctl_table_arg;
	unregister_net_sysctl_table(mdev->sysctl);
	kfree(table);

	mpls_netconf_notify_devconf(net, RTM_DELNETCONF, 0, mdev);
}

static struct mpls_dev *mpls_add_dev(struct net_device *dev)
{
	struct mpls_dev *mdev;
	int err = -ENOMEM;
	int i;

	ASSERT_RTNL();

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(err);

	mdev->stats = alloc_percpu(struct mpls_pcpu_stats);
	if (!mdev->stats)
		goto free;

	for_each_possible_cpu(i) {
		struct mpls_pcpu_stats *mpls_stats;

		mpls_stats = per_cpu_ptr(mdev->stats, i);
		u64_stats_init(&mpls_stats->syncp);
	}

	mdev->dev = dev;

	err = mpls_dev_sysctl_register(dev, mdev);
	if (err)
		goto free;

	rcu_assign_pointer(dev->mpls_ptr, mdev);

	return mdev;

free:
	free_percpu(mdev->stats);
	kfree(mdev);
	return ERR_PTR(err);
}

static void mpls_dev_destroy_rcu(struct rcu_head *head)
{
	struct mpls_dev *mdev = container_of(head, struct mpls_dev, rcu);

	free_percpu(mdev->stats);
	kfree(mdev);
}

static int mpls_ifdown(struct net_device *dev, int event)
{
	struct mpls_route __rcu **platform_label;
	struct net *net = dev_net(dev);
	unsigned index;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	for (index = 0; index < net->mpls.platform_labels; index++) {
		struct mpls_route *rt = rtnl_dereference(platform_label[index]);
		bool nh_del = false;
		u8 alive = 0;

		if (!rt)
			continue;

		if (event == NETDEV_UNREGISTER) {
			u8 deleted = 0;

			for_nexthops(rt) {
				struct net_device *nh_dev =
					rtnl_dereference(nh->nh_dev);

				if (!nh_dev || nh_dev == dev)
					deleted++;
				if (nh_dev == dev)
					nh_del = true;
			} endfor_nexthops(rt);

			/* if there are no more nexthops, delete the route */
			if (deleted == rt->rt_nhn) {
				mpls_route_update(net, index, NULL, NULL);
				continue;
			}

			if (nh_del) {
				size_t size = sizeof(*rt) + rt->rt_nhn *
					rt->rt_nh_size;
				struct mpls_route *orig = rt;

				rt = kmalloc(size, GFP_KERNEL);
				if (!rt)
					return -ENOMEM;
				memcpy(rt, orig, size);
			}
		}

		change_nexthops(rt) {
			unsigned int nh_flags = nh->nh_flags;

			if (rtnl_dereference(nh->nh_dev) != dev)
				goto next;

			switch (event) {
			case NETDEV_DOWN:
			case NETDEV_UNREGISTER:
				nh_flags |= RTNH_F_DEAD;
				fallthrough;
			case NETDEV_CHANGE:
				nh_flags |= RTNH_F_LINKDOWN;
				break;
			}
			if (event == NETDEV_UNREGISTER)
				RCU_INIT_POINTER(nh->nh_dev, NULL);

			if (nh->nh_flags != nh_flags)
				WRITE_ONCE(nh->nh_flags, nh_flags);
next:
			if (!(nh_flags & (RTNH_F_DEAD | RTNH_F_LINKDOWN)))
				alive++;
		} endfor_nexthops(rt);

		WRITE_ONCE(rt->rt_nhn_alive, alive);

		if (nh_del)
			mpls_route_update(net, index, rt, NULL);
	}

	return 0;
}

static void mpls_ifup(struct net_device *dev, unsigned int flags)
{
	struct mpls_route __rcu **platform_label;
	struct net *net = dev_net(dev);
	unsigned index;
	u8 alive;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	for (index = 0; index < net->mpls.platform_labels; index++) {
		struct mpls_route *rt = rtnl_dereference(platform_label[index]);

		if (!rt)
			continue;

		alive = 0;
		change_nexthops(rt) {
			unsigned int nh_flags = nh->nh_flags;
			struct net_device *nh_dev =
				rtnl_dereference(nh->nh_dev);

			if (!(nh_flags & flags)) {
				alive++;
				continue;
			}
			if (nh_dev != dev)
				continue;
			alive++;
			nh_flags &= ~flags;
			WRITE_ONCE(nh->nh_flags, nh_flags);
		} endfor_nexthops(rt);

		WRITE_ONCE(rt->rt_nhn_alive, alive);
	}
}

static int mpls_dev_notify(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct mpls_dev *mdev;
	unsigned int flags;

	if (event == NETDEV_REGISTER) {
		mdev = mpls_add_dev(dev);
		if (IS_ERR(mdev))
			return notifier_from_errno(PTR_ERR(mdev));

		return NOTIFY_OK;
	}

	mdev = mpls_dev_get(dev);
	if (!mdev)
		return NOTIFY_OK;

	switch (event) {
		int err;

	case NETDEV_DOWN:
		err = mpls_ifdown(dev, event);
		if (err)
			return notifier_from_errno(err);
		break;
	case NETDEV_UP:
		flags = dev_get_flags(dev);
		if (flags & (IFF_RUNNING | IFF_LOWER_UP))
			mpls_ifup(dev, RTNH_F_DEAD | RTNH_F_LINKDOWN);
		else
			mpls_ifup(dev, RTNH_F_DEAD);
		break;
	case NETDEV_CHANGE:
		flags = dev_get_flags(dev);
		if (flags & (IFF_RUNNING | IFF_LOWER_UP)) {
			mpls_ifup(dev, RTNH_F_DEAD | RTNH_F_LINKDOWN);
		} else {
			err = mpls_ifdown(dev, event);
			if (err)
				return notifier_from_errno(err);
		}
		break;
	case NETDEV_UNREGISTER:
		err = mpls_ifdown(dev, event);
		if (err)
			return notifier_from_errno(err);
		mdev = mpls_dev_get(dev);
		if (mdev) {
			mpls_dev_sysctl_unregister(dev, mdev);
			RCU_INIT_POINTER(dev->mpls_ptr, NULL);
			call_rcu(&mdev->rcu, mpls_dev_destroy_rcu);
		}
		break;
	case NETDEV_CHANGENAME:
		mdev = mpls_dev_get(dev);
		if (mdev) {
			mpls_dev_sysctl_unregister(dev, mdev);
			err = mpls_dev_sysctl_register(dev, mdev);
			if (err)
				return notifier_from_errno(err);
		}
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block mpls_dev_notifier = {
	.notifier_call = mpls_dev_notify,
};

static int nla_put_via(struct sk_buff *skb,
		       u8 table, const void *addr, int alen)
{
	static const int table_to_family[NEIGH_NR_TABLES + 1] = {
		AF_INET, AF_INET6, AF_DECnet, AF_PACKET,
	};
	struct nlattr *nla;
	struct rtvia *via;
	int family = AF_UNSPEC;

	nla = nla_reserve(skb, RTA_VIA, alen + 2);
	if (!nla)
		return -EMSGSIZE;

	if (table <= NEIGH_NR_TABLES)
		family = table_to_family[table];

	via = nla_data(nla);
	via->rtvia_family = family;
	memcpy(via->rtvia_addr, addr, alen);
	return 0;
}

int nla_put_labels(struct sk_buff *skb, int attrtype,
		   u8 labels, const u32 label[])
{
	struct nlattr *nla;
	struct mpls_shim_hdr *nla_label;
	bool bos;
	int i;
	nla = nla_reserve(skb, attrtype, labels*4);
	if (!nla)
		return -EMSGSIZE;

	nla_label = nla_data(nla);
	bos = true;
	for (i = labels - 1; i >= 0; i--) {
		nla_label[i] = mpls_entry_encode(label[i], 0, 0, bos);
		bos = false;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nla_put_labels);

int nla_get_labels(const struct nlattr *nla, u8 max_labels, u8 *labels,
		   u32 label[], struct netlink_ext_ack *extack)
{
	unsigned len = nla_len(nla);
	struct mpls_shim_hdr *nla_label;
	u8 nla_labels;
	bool bos;
	int i;

	/* len needs to be an even multiple of 4 (the label size). Number
	 * of labels is a u8 so check for overflow.
	 */
	if (len & 3 || len / 4 > 255) {
		NL_SET_ERR_MSG_ATTR(extack, nla,
				    "Invalid length for labels attribute");
		return -EINVAL;
	}

	/* Limit the number of new labels allowed */
	nla_labels = len/4;
	if (nla_labels > max_labels) {
		NL_SET_ERR_MSG(extack, "Too many labels");
		return -EINVAL;
	}

	/* when label == NULL, caller wants number of labels */
	if (!label)
		goto out;

	nla_label = nla_data(nla);
	bos = true;
	for (i = nla_labels - 1; i >= 0; i--, bos = false) {
		struct mpls_entry_decoded dec;
		dec = mpls_entry_decode(nla_label + i);

		/* Ensure the bottom of stack flag is properly set
		 * and ttl and tc are both clear.
		 */
		if (dec.ttl) {
			NL_SET_ERR_MSG_ATTR(extack, nla,
					    "TTL in label must be 0");
			return -EINVAL;
		}

		if (dec.tc) {
			NL_SET_ERR_MSG_ATTR(extack, nla,
					    "Traffic class in label must be 0");
			return -EINVAL;
		}

		if (dec.bos != bos) {
			NL_SET_BAD_ATTR(extack, nla);
			if (bos) {
				NL_SET_ERR_MSG(extack,
					       "BOS bit must be set in first label");
			} else {
				NL_SET_ERR_MSG(extack,
					       "BOS bit can only be set in first label");
			}
			return -EINVAL;
		}

		switch (dec.label) {
		case MPLS_LABEL_IMPLNULL:
			/* RFC3032: This is a label that an LSR may
			 * assign and distribute, but which never
			 * actually appears in the encapsulation.
			 */
			NL_SET_ERR_MSG_ATTR(extack, nla,
					    "Implicit NULL Label (3) can not be used in encapsulation");
			return -EINVAL;
		}

		label[i] = dec.label;
	}
out:
	*labels = nla_labels;
	return 0;
}
EXPORT_SYMBOL_GPL(nla_get_labels);

static int rtm_to_route_config(struct sk_buff *skb,
			       struct nlmsghdr *nlh,
			       struct mpls_route_config *cfg,
			       struct netlink_ext_ack *extack)
{
	struct rtmsg *rtm;
	struct nlattr *tb[RTA_MAX+1];
	int index;
	int err;

	err = nlmsg_parse_deprecated(nlh, sizeof(*rtm), tb, RTA_MAX,
				     rtm_mpls_policy, extack);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	rtm = nlmsg_data(nlh);

	if (rtm->rtm_family != AF_MPLS) {
		NL_SET_ERR_MSG(extack, "Invalid address family in rtmsg");
		goto errout;
	}
	if (rtm->rtm_dst_len != 20) {
		NL_SET_ERR_MSG(extack, "rtm_dst_len must be 20 for MPLS");
		goto errout;
	}
	if (rtm->rtm_src_len != 0) {
		NL_SET_ERR_MSG(extack, "rtm_src_len must be 0 for MPLS");
		goto errout;
	}
	if (rtm->rtm_tos != 0) {
		NL_SET_ERR_MSG(extack, "rtm_tos must be 0 for MPLS");
		goto errout;
	}
	if (rtm->rtm_table != RT_TABLE_MAIN) {
		NL_SET_ERR_MSG(extack,
			       "MPLS only supports the main route table");
		goto errout;
	}
	/* Any value is acceptable for rtm_protocol */

	/* As mpls uses destination specific addresses
	 * (or source specific address in the case of multicast)
	 * all addresses have universal scope.
	 */
	if (rtm->rtm_scope != RT_SCOPE_UNIVERSE) {
		NL_SET_ERR_MSG(extack,
			       "Invalid route scope  - MPLS only supports UNIVERSE");
		goto errout;
	}
	if (rtm->rtm_type != RTN_UNICAST) {
		NL_SET_ERR_MSG(extack,
			       "Invalid route type - MPLS only supports UNICAST");
		goto errout;
	}
	if (rtm->rtm_flags != 0) {
		NL_SET_ERR_MSG(extack, "rtm_flags must be 0 for MPLS");
		goto errout;
	}

	cfg->rc_label		= LABEL_NOT_SPECIFIED;
	cfg->rc_protocol	= rtm->rtm_protocol;
	cfg->rc_via_table	= MPLS_NEIGH_TABLE_UNSPEC;
	cfg->rc_ttl_propagate	= MPLS_TTL_PROP_DEFAULT;
	cfg->rc_nlflags		= nlh->nlmsg_flags;
	cfg->rc_nlinfo.portid	= NETLINK_CB(skb).portid;
	cfg->rc_nlinfo.nlh	= nlh;
	cfg->rc_nlinfo.nl_net	= sock_net(skb->sk);

	for (index = 0; index <= RTA_MAX; index++) {
		struct nlattr *nla = tb[index];
		if (!nla)
			continue;

		switch (index) {
		case RTA_OIF:
			cfg->rc_ifindex = nla_get_u32(nla);
			break;
		case RTA_NEWDST:
			if (nla_get_labels(nla, MAX_NEW_LABELS,
					   &cfg->rc_output_labels,
					   cfg->rc_output_label, extack))
				goto errout;
			break;
		case RTA_DST:
		{
			u8 label_count;
			if (nla_get_labels(nla, 1, &label_count,
					   &cfg->rc_label, extack))
				goto errout;

			if (!mpls_label_ok(cfg->rc_nlinfo.nl_net,
					   &cfg->rc_label, extack))
				goto errout;
			break;
		}
		case RTA_GATEWAY:
			NL_SET_ERR_MSG(extack, "MPLS does not support RTA_GATEWAY attribute");
			goto errout;
		case RTA_VIA:
		{
			if (nla_get_via(nla, &cfg->rc_via_alen,
					&cfg->rc_via_table, cfg->rc_via,
					extack))
				goto errout;
			break;
		}
		case RTA_MULTIPATH:
		{
			cfg->rc_mp = nla_data(nla);
			cfg->rc_mp_len = nla_len(nla);
			break;
		}
		case RTA_TTL_PROPAGATE:
		{
			u8 ttl_propagate = nla_get_u8(nla);

			if (ttl_propagate > 1) {
				NL_SET_ERR_MSG_ATTR(extack, nla,
						    "RTA_TTL_PROPAGATE can only be 0 or 1");
				goto errout;
			}
			cfg->rc_ttl_propagate = ttl_propagate ?
				MPLS_TTL_PROP_ENABLED :
				MPLS_TTL_PROP_DISABLED;
			break;
		}
		default:
			NL_SET_ERR_MSG_ATTR(extack, nla, "Unknown attribute");
			/* Unsupported attribute */
			goto errout;
		}
	}

	err = 0;
errout:
	return err;
}

static int mpls_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	struct mpls_route_config *cfg;
	int err;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	err = rtm_to_route_config(skb, nlh, cfg, extack);
	if (err < 0)
		goto out;

	err = mpls_route_del(cfg, extack);
out:
	kfree(cfg);

	return err;
}


static int mpls_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	struct mpls_route_config *cfg;
	int err;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	err = rtm_to_route_config(skb, nlh, cfg, extack);
	if (err < 0)
		goto out;

	err = mpls_route_add(cfg, extack);
out:
	kfree(cfg);

	return err;
}

static int mpls_dump_route(struct sk_buff *skb, u32 portid, u32 seq, int event,
			   u32 label, struct mpls_route *rt, int flags)
{
	struct net_device *dev;
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*rtm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	rtm = nlmsg_data(nlh);
	rtm->rtm_family = AF_MPLS;
	rtm->rtm_dst_len = 20;
	rtm->rtm_src_len = 0;
	rtm->rtm_tos = 0;
	rtm->rtm_table = RT_TABLE_MAIN;
	rtm->rtm_protocol = rt->rt_protocol;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_flags = 0;

	if (nla_put_labels(skb, RTA_DST, 1, &label))
		goto nla_put_failure;

	if (rt->rt_ttl_propagate != MPLS_TTL_PROP_DEFAULT) {
		bool ttl_propagate =
			rt->rt_ttl_propagate == MPLS_TTL_PROP_ENABLED;

		if (nla_put_u8(skb, RTA_TTL_PROPAGATE,
			       ttl_propagate))
			goto nla_put_failure;
	}
	if (rt->rt_nhn == 1) {
		const struct mpls_nh *nh = rt->rt_nh;

		if (nh->nh_labels &&
		    nla_put_labels(skb, RTA_NEWDST, nh->nh_labels,
				   nh->nh_label))
			goto nla_put_failure;
		if (nh->nh_via_table != MPLS_NEIGH_TABLE_UNSPEC &&
		    nla_put_via(skb, nh->nh_via_table, mpls_nh_via(rt, nh),
				nh->nh_via_alen))
			goto nla_put_failure;
		dev = rtnl_dereference(nh->nh_dev);
		if (dev && nla_put_u32(skb, RTA_OIF, dev->ifindex))
			goto nla_put_failure;
		if (nh->nh_flags & RTNH_F_LINKDOWN)
			rtm->rtm_flags |= RTNH_F_LINKDOWN;
		if (nh->nh_flags & RTNH_F_DEAD)
			rtm->rtm_flags |= RTNH_F_DEAD;
	} else {
		struct rtnexthop *rtnh;
		struct nlattr *mp;
		u8 linkdown = 0;
		u8 dead = 0;

		mp = nla_nest_start_noflag(skb, RTA_MULTIPATH);
		if (!mp)
			goto nla_put_failure;

		for_nexthops(rt) {
			dev = rtnl_dereference(nh->nh_dev);
			if (!dev)
				continue;

			rtnh = nla_reserve_nohdr(skb, sizeof(*rtnh));
			if (!rtnh)
				goto nla_put_failure;

			rtnh->rtnh_ifindex = dev->ifindex;
			if (nh->nh_flags & RTNH_F_LINKDOWN) {
				rtnh->rtnh_flags |= RTNH_F_LINKDOWN;
				linkdown++;
			}
			if (nh->nh_flags & RTNH_F_DEAD) {
				rtnh->rtnh_flags |= RTNH_F_DEAD;
				dead++;
			}

			if (nh->nh_labels && nla_put_labels(skb, RTA_NEWDST,
							    nh->nh_labels,
							    nh->nh_label))
				goto nla_put_failure;
			if (nh->nh_via_table != MPLS_NEIGH_TABLE_UNSPEC &&
			    nla_put_via(skb, nh->nh_via_table,
					mpls_nh_via(rt, nh),
					nh->nh_via_alen))
				goto nla_put_failure;

			/* length of rtnetlink header + attributes */
			rtnh->rtnh_len = nlmsg_get_pos(skb) - (void *)rtnh;
		} endfor_nexthops(rt);

		if (linkdown == rt->rt_nhn)
			rtm->rtm_flags |= RTNH_F_LINKDOWN;
		if (dead == rt->rt_nhn)
			rtm->rtm_flags |= RTNH_F_DEAD;

		nla_nest_end(skb, mp);
	}

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

#if IS_ENABLED(CONFIG_INET)
static int mpls_valid_fib_dump_req(struct net *net, const struct nlmsghdr *nlh,
				   struct fib_dump_filter *filter,
				   struct netlink_callback *cb)
{
	return ip_valid_fib_dump_req(net, nlh, filter, cb);
}
#else
static int mpls_valid_fib_dump_req(struct net *net, const struct nlmsghdr *nlh,
				   struct fib_dump_filter *filter,
				   struct netlink_callback *cb)
{
	struct netlink_ext_ack *extack = cb->extack;
	struct nlattr *tb[RTA_MAX + 1];
	struct rtmsg *rtm;
	int err, i;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*rtm))) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid header for FIB dump request");
		return -EINVAL;
	}

	rtm = nlmsg_data(nlh);
	if (rtm->rtm_dst_len || rtm->rtm_src_len  || rtm->rtm_tos   ||
	    rtm->rtm_table   || rtm->rtm_scope    || rtm->rtm_type  ||
	    rtm->rtm_flags) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid values in header for FIB dump request");
		return -EINVAL;
	}

	if (rtm->rtm_protocol) {
		filter->protocol = rtm->rtm_protocol;
		filter->filter_set = 1;
		cb->answer_flags = NLM_F_DUMP_FILTERED;
	}

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(*rtm), tb, RTA_MAX,
					    rtm_mpls_policy, extack);
	if (err < 0)
		return err;

	for (i = 0; i <= RTA_MAX; ++i) {
		int ifindex;

		if (i == RTA_OIF) {
			ifindex = nla_get_u32(tb[i]);
			filter->dev = __dev_get_by_index(net, ifindex);
			if (!filter->dev)
				return -ENODEV;
			filter->filter_set = 1;
		} else if (tb[i]) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported attribute in dump request");
			return -EINVAL;
		}
	}

	return 0;
}
#endif

static bool mpls_rt_uses_dev(struct mpls_route *rt,
			     const struct net_device *dev)
{
	struct net_device *nh_dev;

	if (rt->rt_nhn == 1) {
		struct mpls_nh *nh = rt->rt_nh;

		nh_dev = rtnl_dereference(nh->nh_dev);
		if (dev == nh_dev)
			return true;
	} else {
		for_nexthops(rt) {
			nh_dev = rtnl_dereference(nh->nh_dev);
			if (nh_dev == dev)
				return true;
		} endfor_nexthops(rt);
	}

	return false;
}

static int mpls_dump_routes(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct net *net = sock_net(skb->sk);
	struct mpls_route __rcu **platform_label;
	struct fib_dump_filter filter = {};
	unsigned int flags = NLM_F_MULTI;
	size_t platform_labels;
	unsigned int index;

	ASSERT_RTNL();

	if (cb->strict_check) {
		int err;

		err = mpls_valid_fib_dump_req(net, nlh, &filter, cb);
		if (err < 0)
			return err;

		/* for MPLS, there is only 1 table with fixed type and flags.
		 * If either are set in the filter then return nothing.
		 */
		if ((filter.table_id && filter.table_id != RT_TABLE_MAIN) ||
		    (filter.rt_type && filter.rt_type != RTN_UNICAST) ||
		     filter.flags)
			return skb->len;
	}

	index = cb->args[0];
	if (index < MPLS_LABEL_FIRST_UNRESERVED)
		index = MPLS_LABEL_FIRST_UNRESERVED;

	platform_label = rtnl_dereference(net->mpls.platform_label);
	platform_labels = net->mpls.platform_labels;

	if (filter.filter_set)
		flags |= NLM_F_DUMP_FILTERED;

	for (; index < platform_labels; index++) {
		struct mpls_route *rt;

		rt = rtnl_dereference(platform_label[index]);
		if (!rt)
			continue;

		if ((filter.dev && !mpls_rt_uses_dev(rt, filter.dev)) ||
		    (filter.protocol && rt->rt_protocol != filter.protocol))
			continue;

		if (mpls_dump_route(skb, NETLINK_CB(cb->skb).portid,
				    cb->nlh->nlmsg_seq, RTM_NEWROUTE,
				    index, rt, flags) < 0)
			break;
	}
	cb->args[0] = index;

	return skb->len;
}

static inline size_t lfib_nlmsg_size(struct mpls_route *rt)
{
	size_t payload =
		NLMSG_ALIGN(sizeof(struct rtmsg))
		+ nla_total_size(4)			/* RTA_DST */
		+ nla_total_size(1);			/* RTA_TTL_PROPAGATE */

	if (rt->rt_nhn == 1) {
		struct mpls_nh *nh = rt->rt_nh;

		if (nh->nh_dev)
			payload += nla_total_size(4); /* RTA_OIF */
		if (nh->nh_via_table != MPLS_NEIGH_TABLE_UNSPEC) /* RTA_VIA */
			payload += nla_total_size(2 + nh->nh_via_alen);
		if (nh->nh_labels) /* RTA_NEWDST */
			payload += nla_total_size(nh->nh_labels * 4);
	} else {
		/* each nexthop is packed in an attribute */
		size_t nhsize = 0;

		for_nexthops(rt) {
			if (!rtnl_dereference(nh->nh_dev))
				continue;
			nhsize += nla_total_size(sizeof(struct rtnexthop));
			/* RTA_VIA */
			if (nh->nh_via_table != MPLS_NEIGH_TABLE_UNSPEC)
				nhsize += nla_total_size(2 + nh->nh_via_alen);
			if (nh->nh_labels)
				nhsize += nla_total_size(nh->nh_labels * 4);
		} endfor_nexthops(rt);
		/* nested attribute */
		payload += nla_total_size(nhsize);
	}

	return payload;
}

static void rtmsg_lfib(int event, u32 label, struct mpls_route *rt,
		       struct nlmsghdr *nlh, struct net *net, u32 portid,
		       unsigned int nlm_flags)
{
	struct sk_buff *skb;
	u32 seq = nlh ? nlh->nlmsg_seq : 0;
	int err = -ENOBUFS;

	skb = nlmsg_new(lfib_nlmsg_size(rt), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = mpls_dump_route(skb, portid, seq, event, label, rt, nlm_flags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in lfib_nlmsg_size */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, portid, RTNLGRP_MPLS_ROUTE, nlh, GFP_KERNEL);

	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_MPLS_ROUTE, err);
}

static int mpls_valid_getroute_req(struct sk_buff *skb,
				   const struct nlmsghdr *nlh,
				   struct nlattr **tb,
				   struct netlink_ext_ack *extack)
{
	struct rtmsg *rtm;
	int i, err;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*rtm))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid header for get route request");
		return -EINVAL;
	}

	if (!netlink_strict_get_check(skb))
		return nlmsg_parse_deprecated(nlh, sizeof(*rtm), tb, RTA_MAX,
					      rtm_mpls_policy, extack);

	rtm = nlmsg_data(nlh);
	if ((rtm->rtm_dst_len && rtm->rtm_dst_len != 20) ||
	    rtm->rtm_src_len || rtm->rtm_tos || rtm->rtm_table ||
	    rtm->rtm_protocol || rtm->rtm_scope || rtm->rtm_type) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid values in header for get route request");
		return -EINVAL;
	}
	if (rtm->rtm_flags & ~RTM_F_FIB_MATCH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid flags for get route request");
		return -EINVAL;
	}

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(*rtm), tb, RTA_MAX,
					    rtm_mpls_policy, extack);
	if (err)
		return err;

	if ((tb[RTA_DST] || tb[RTA_NEWDST]) && !rtm->rtm_dst_len) {
		NL_SET_ERR_MSG_MOD(extack, "rtm_dst_len must be 20 for MPLS");
		return -EINVAL;
	}

	for (i = 0; i <= RTA_MAX; i++) {
		if (!tb[i])
			continue;

		switch (i) {
		case RTA_DST:
		case RTA_NEWDST:
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Unsupported attribute in get route request");
			return -EINVAL;
		}
	}

	return 0;
}

static int mpls_getroute(struct sk_buff *in_skb, struct nlmsghdr *in_nlh,
			 struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	u32 portid = NETLINK_CB(in_skb).portid;
	u32 in_label = LABEL_NOT_SPECIFIED;
	struct nlattr *tb[RTA_MAX + 1];
	u32 labels[MAX_NEW_LABELS];
	struct mpls_shim_hdr *hdr;
	unsigned int hdr_size = 0;
	struct net_device *dev;
	struct mpls_route *rt;
	struct rtmsg *rtm, *r;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	struct mpls_nh *nh;
	u8 n_labels;
	int err;

	err = mpls_valid_getroute_req(in_skb, in_nlh, tb, extack);
	if (err < 0)
		goto errout;

	rtm = nlmsg_data(in_nlh);

	if (tb[RTA_DST]) {
		u8 label_count;

		if (nla_get_labels(tb[RTA_DST], 1, &label_count,
				   &in_label, extack)) {
			err = -EINVAL;
			goto errout;
		}

		if (!mpls_label_ok(net, &in_label, extack)) {
			err = -EINVAL;
			goto errout;
		}
	}

	rt = mpls_route_input_rcu(net, in_label);
	if (!rt) {
		err = -ENETUNREACH;
		goto errout;
	}

	if (rtm->rtm_flags & RTM_F_FIB_MATCH) {
		skb = nlmsg_new(lfib_nlmsg_size(rt), GFP_KERNEL);
		if (!skb) {
			err = -ENOBUFS;
			goto errout;
		}

		err = mpls_dump_route(skb, portid, in_nlh->nlmsg_seq,
				      RTM_NEWROUTE, in_label, rt, 0);
		if (err < 0) {
			/* -EMSGSIZE implies BUG in lfib_nlmsg_size */
			WARN_ON(err == -EMSGSIZE);
			goto errout_free;
		}

		return rtnl_unicast(skb, net, portid);
	}

	if (tb[RTA_NEWDST]) {
		if (nla_get_labels(tb[RTA_NEWDST], MAX_NEW_LABELS, &n_labels,
				   labels, extack) != 0) {
			err = -EINVAL;
			goto errout;
		}

		hdr_size = n_labels * sizeof(struct mpls_shim_hdr);
	}

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		err = -ENOBUFS;
		goto errout;
	}

	skb->protocol = htons(ETH_P_MPLS_UC);

	if (hdr_size) {
		bool bos;
		int i;

		if (skb_cow(skb, hdr_size)) {
			err = -ENOBUFS;
			goto errout_free;
		}

		skb_reserve(skb, hdr_size);
		skb_push(skb, hdr_size);
		skb_reset_network_header(skb);

		/* Push new labels */
		hdr = mpls_hdr(skb);
		bos = true;
		for (i = n_labels - 1; i >= 0; i--) {
			hdr[i] = mpls_entry_encode(labels[i],
						   1, 0, bos);
			bos = false;
		}
	}

	nh = mpls_select_multipath(rt, skb);
	if (!nh) {
		err = -ENETUNREACH;
		goto errout_free;
	}

	if (hdr_size) {
		skb_pull(skb, hdr_size);
		skb_reset_network_header(skb);
	}

	nlh = nlmsg_put(skb, portid, in_nlh->nlmsg_seq,
			RTM_NEWROUTE, sizeof(*r), 0);
	if (!nlh) {
		err = -EMSGSIZE;
		goto errout_free;
	}

	r = nlmsg_data(nlh);
	r->rtm_family	 = AF_MPLS;
	r->rtm_dst_len	= 20;
	r->rtm_src_len	= 0;
	r->rtm_table	= RT_TABLE_MAIN;
	r->rtm_type	= RTN_UNICAST;
	r->rtm_scope	= RT_SCOPE_UNIVERSE;
	r->rtm_protocol = rt->rt_protocol;
	r->rtm_flags	= 0;

	if (nla_put_labels(skb, RTA_DST, 1, &in_label))
		goto nla_put_failure;

	if (nh->nh_labels &&
	    nla_put_labels(skb, RTA_NEWDST, nh->nh_labels,
			   nh->nh_label))
		goto nla_put_failure;

	if (nh->nh_via_table != MPLS_NEIGH_TABLE_UNSPEC &&
	    nla_put_via(skb, nh->nh_via_table, mpls_nh_via(rt, nh),
			nh->nh_via_alen))
		goto nla_put_failure;
	dev = rtnl_dereference(nh->nh_dev);
	if (dev && nla_put_u32(skb, RTA_OIF, dev->ifindex))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);

	err = rtnl_unicast(skb, net, portid);
errout:
	return err;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	err = -EMSGSIZE;
errout_free:
	kfree_skb(skb);
	return err;
}

static int resize_platform_label_table(struct net *net, size_t limit)
{
	size_t size = sizeof(struct mpls_route *) * limit;
	size_t old_limit;
	size_t cp_size;
	struct mpls_route __rcu **labels = NULL, **old;
	struct mpls_route *rt0 = NULL, *rt2 = NULL;
	unsigned index;

	if (size) {
		labels = kvzalloc(size, GFP_KERNEL);
		if (!labels)
			goto nolabels;
	}

	/* In case the predefined labels need to be populated */
	if (limit > MPLS_LABEL_IPV4NULL) {
		struct net_device *lo = net->loopback_dev;
		rt0 = mpls_rt_alloc(1, lo->addr_len, 0);
		if (IS_ERR(rt0))
			goto nort0;
		RCU_INIT_POINTER(rt0->rt_nh->nh_dev, lo);
		rt0->rt_protocol = RTPROT_KERNEL;
		rt0->rt_payload_type = MPT_IPV4;
		rt0->rt_ttl_propagate = MPLS_TTL_PROP_DEFAULT;
		rt0->rt_nh->nh_via_table = NEIGH_LINK_TABLE;
		rt0->rt_nh->nh_via_alen = lo->addr_len;
		memcpy(__mpls_nh_via(rt0, rt0->rt_nh), lo->dev_addr,
		       lo->addr_len);
	}
	if (limit > MPLS_LABEL_IPV6NULL) {
		struct net_device *lo = net->loopback_dev;
		rt2 = mpls_rt_alloc(1, lo->addr_len, 0);
		if (IS_ERR(rt2))
			goto nort2;
		RCU_INIT_POINTER(rt2->rt_nh->nh_dev, lo);
		rt2->rt_protocol = RTPROT_KERNEL;
		rt2->rt_payload_type = MPT_IPV6;
		rt2->rt_ttl_propagate = MPLS_TTL_PROP_DEFAULT;
		rt2->rt_nh->nh_via_table = NEIGH_LINK_TABLE;
		rt2->rt_nh->nh_via_alen = lo->addr_len;
		memcpy(__mpls_nh_via(rt2, rt2->rt_nh), lo->dev_addr,
		       lo->addr_len);
	}

	rtnl_lock();
	/* Remember the original table */
	old = rtnl_dereference(net->mpls.platform_label);
	old_limit = net->mpls.platform_labels;

	/* Free any labels beyond the new table */
	for (index = limit; index < old_limit; index++)
		mpls_route_update(net, index, NULL, NULL);

	/* Copy over the old labels */
	cp_size = size;
	if (old_limit < limit)
		cp_size = old_limit * sizeof(struct mpls_route *);

	memcpy(labels, old, cp_size);

	/* If needed set the predefined labels */
	if ((old_limit <= MPLS_LABEL_IPV6NULL) &&
	    (limit > MPLS_LABEL_IPV6NULL)) {
		RCU_INIT_POINTER(labels[MPLS_LABEL_IPV6NULL], rt2);
		rt2 = NULL;
	}

	if ((old_limit <= MPLS_LABEL_IPV4NULL) &&
	    (limit > MPLS_LABEL_IPV4NULL)) {
		RCU_INIT_POINTER(labels[MPLS_LABEL_IPV4NULL], rt0);
		rt0 = NULL;
	}

	/* Update the global pointers */
	net->mpls.platform_labels = limit;
	rcu_assign_pointer(net->mpls.platform_label, labels);

	rtnl_unlock();

	mpls_rt_free(rt2);
	mpls_rt_free(rt0);

	if (old) {
		synchronize_rcu();
		kvfree(old);
	}
	return 0;

nort2:
	mpls_rt_free(rt0);
nort0:
	kvfree(labels);
nolabels:
	return -ENOMEM;
}

static int mpls_platform_labels(struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = table->data;
	int platform_labels = net->mpls.platform_labels;
	int ret;
	struct ctl_table tmp = {
		.procname	= table->procname,
		.data		= &platform_labels,
		.maxlen		= sizeof(int),
		.mode		= table->mode,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &label_limit,
	};

	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && ret == 0)
		ret = resize_platform_label_table(net, platform_labels);

	return ret;
}

#define MPLS_NS_SYSCTL_OFFSET(field)		\
	(&((struct net *)0)->field)

static const struct ctl_table mpls_table[] = {
	{
		.procname	= "platform_labels",
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= mpls_platform_labels,
	},
	{
		.procname	= "ip_ttl_propagate",
		.data		= MPLS_NS_SYSCTL_OFFSET(mpls.ip_ttl_propagate),
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "default_ttl",
		.data		= MPLS_NS_SYSCTL_OFFSET(mpls.default_ttl),
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= &ttl_max,
	},
	{ }
};

static int mpls_net_init(struct net *net)
{
	struct ctl_table *table;
	int i;

	net->mpls.platform_labels = 0;
	net->mpls.platform_label = NULL;
	net->mpls.ip_ttl_propagate = 1;
	net->mpls.default_ttl = 255;

	table = kmemdup(mpls_table, sizeof(mpls_table), GFP_KERNEL);
	if (table == NULL)
		return -ENOMEM;

	/* Table data contains only offsets relative to the base of
	 * the mdev at this point, so make them absolute.
	 */
	for (i = 0; i < ARRAY_SIZE(mpls_table) - 1; i++)
		table[i].data = (char *)net + (uintptr_t)table[i].data;

	net->mpls.ctl = register_net_sysctl(net, "net/mpls", table);
	if (net->mpls.ctl == NULL) {
		kfree(table);
		return -ENOMEM;
	}

	return 0;
}

static void mpls_net_exit(struct net *net)
{
	struct mpls_route __rcu **platform_label;
	size_t platform_labels;
	struct ctl_table *table;
	unsigned int index;

	table = net->mpls.ctl->ctl_table_arg;
	unregister_net_sysctl_table(net->mpls.ctl);
	kfree(table);

	/* An rcu grace period has passed since there was a device in
	 * the network namespace (and thus the last in flight packet)
	 * left this network namespace.  This is because
	 * unregister_netdevice_many and netdev_run_todo has completed
	 * for each network device that was in this network namespace.
	 *
	 * As such no additional rcu synchronization is necessary when
	 * freeing the platform_label table.
	 */
	rtnl_lock();
	platform_label = rtnl_dereference(net->mpls.platform_label);
	platform_labels = net->mpls.platform_labels;
	for (index = 0; index < platform_labels; index++) {
		struct mpls_route *rt = rtnl_dereference(platform_label[index]);
		RCU_INIT_POINTER(platform_label[index], NULL);
		mpls_notify_route(net, index, rt, NULL, NULL);
		mpls_rt_free(rt);
	}
	rtnl_unlock();

	kvfree(platform_label);
}

static struct pernet_operations mpls_net_ops = {
	.init = mpls_net_init,
	.exit = mpls_net_exit,
};

static struct rtnl_af_ops mpls_af_ops __read_mostly = {
	.family		   = AF_MPLS,
	.fill_stats_af	   = mpls_fill_stats_af,
	.get_stats_af_size = mpls_get_stats_af_size,
};

static int __init mpls_init(void)
{
	int err;

	BUILD_BUG_ON(sizeof(struct mpls_shim_hdr) != 4);

	err = register_pernet_subsys(&mpls_net_ops);
	if (err)
		goto out;

	err = register_netdevice_notifier(&mpls_dev_notifier);
	if (err)
		goto out_unregister_pernet;

	dev_add_pack(&mpls_packet_type);

	rtnl_af_register(&mpls_af_ops);

	rtnl_register_module(THIS_MODULE, PF_MPLS, RTM_NEWROUTE,
			     mpls_rtm_newroute, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MPLS, RTM_DELROUTE,
			     mpls_rtm_delroute, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MPLS, RTM_GETROUTE,
			     mpls_getroute, mpls_dump_routes, 0);
	rtnl_register_module(THIS_MODULE, PF_MPLS, RTM_GETNETCONF,
			     mpls_netconf_get_devconf,
			     mpls_netconf_dump_devconf, 0);
	err = ipgre_tunnel_encap_add_mpls_ops();
	if (err)
		pr_err("Can't add mpls over gre tunnel ops\n");

	err = 0;
out:
	return err;

out_unregister_pernet:
	unregister_pernet_subsys(&mpls_net_ops);
	goto out;
}
module_init(mpls_init);

static void __exit mpls_exit(void)
{
	rtnl_unregister_all(PF_MPLS);
	rtnl_af_unregister(&mpls_af_ops);
	dev_remove_pack(&mpls_packet_type);
	unregister_netdevice_notifier(&mpls_dev_notifier);
	unregister_pernet_subsys(&mpls_net_ops);
	ipgre_tunnel_encap_del_mpls_ops();
}
module_exit(mpls_exit);

MODULE_DESCRIPTION("MultiProtocol Label Switching");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(PF_MPLS);
