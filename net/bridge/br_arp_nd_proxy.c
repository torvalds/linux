// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Handle bridge arp/nd proxy/suppress
 *
 *  Copyright (C) 2017 Cumulus Networks
 *  Copyright (c) 2017 Roopa Prabhu <roopa@cumulusnetworks.com>
 *
 *  Authors:
 *	Roopa Prabhu <roopa@cumulusnetworks.com>
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/neighbour.h>
#include <net/arp.h>
#include <linux/if_vlan.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <net/ipv6_stubs.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ip6_checksum.h>
#endif

#include "br_private.h"

void br_recalculate_neigh_suppress_enabled(struct net_bridge *br)
{
	struct net_bridge_port *p;
	bool neigh_suppress = false;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->flags & (BR_NEIGH_SUPPRESS | BR_NEIGH_VLAN_SUPPRESS)) {
			neigh_suppress = true;
			break;
		}
	}

	br_opt_toggle(br, BROPT_NEIGH_SUPPRESS_ENABLED, neigh_suppress);
}

#if IS_ENABLED(CONFIG_INET)
static void br_arp_send(struct net_bridge *br, struct net_bridge_port *p,
			struct net_device *dev, __be32 dest_ip, __be32 src_ip,
			const unsigned char *dest_hw,
			const unsigned char *src_hw,
			const unsigned char *target_hw,
			__be16 vlan_proto, u16 vlan_tci)
{
	struct net_bridge_vlan_group *vg;
	struct sk_buff *skb;
	u16 pvid;

	netdev_dbg(dev, "arp send dev %s dst %pI4 dst_hw %pM src %pI4 src_hw %pM\n",
		   dev->name, &dest_ip, dest_hw, &src_ip, src_hw);

	if (!vlan_tci) {
		arp_send(ARPOP_REPLY, ETH_P_ARP, dest_ip, dev, src_ip,
			 dest_hw, src_hw, target_hw);
		return;
	}

	skb = arp_create(ARPOP_REPLY, ETH_P_ARP, dest_ip, dev, src_ip,
			 dest_hw, src_hw, target_hw);
	if (!skb)
		return;

	if (p)
		vg = nbp_vlan_group_rcu(p);
	else
		vg = br_vlan_group_rcu(br);
	pvid = br_get_pvid(vg);
	if (pvid == (vlan_tci & VLAN_VID_MASK))
		vlan_tci = 0;

	if (vlan_tci)
		__vlan_hwaccel_put_tag(skb, vlan_proto, vlan_tci);

	if (p) {
		arp_xmit(skb);
	} else {
		skb_reset_mac_header(skb);
		__skb_pull(skb, skb_network_offset(skb));
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->pkt_type = PACKET_HOST;

		netif_rx(skb);
	}
}

static int br_chk_addr_ip(struct net_device *dev,
			  struct netdev_nested_priv *priv)
{
	__be32 ip = *(__be32 *)priv->data;
	struct in_device *in_dev;
	__be32 addr = 0;

	in_dev = __in_dev_get_rcu(dev);
	if (in_dev)
		addr = inet_confirm_addr(dev_net(dev), in_dev, 0, ip,
					 RT_SCOPE_HOST);

	if (addr == ip)
		return 1;

	return 0;
}

static bool br_is_local_ip(struct net_device *dev, __be32 ip)
{
	struct netdev_nested_priv priv = {
		.data = (void *)&ip,
	};

	if (br_chk_addr_ip(dev, &priv))
		return true;

	/* check if ip is configured on upper dev */
	if (netdev_walk_all_upper_dev_rcu(dev, br_chk_addr_ip, &priv))
		return true;

	return false;
}

void br_do_proxy_suppress_arp(struct sk_buff *skb, struct net_bridge *br,
			      u16 vid, struct net_bridge_port *p)
{
	struct net_device *dev = br->dev;
	struct net_device *vlandev = dev;
	struct neighbour *n;
	struct arphdr *parp;
	u8 *arpptr, *sha;
	__be32 sip, tip;

	BR_INPUT_SKB_CB(skb)->proxyarp_replied = 0;

	if ((dev->flags & IFF_NOARP) ||
	    !pskb_may_pull(skb, arp_hdr_len(dev)))
		return;

	parp = arp_hdr(skb);

	if (parp->ar_pro != htons(ETH_P_IP) ||
	    parp->ar_hln != dev->addr_len ||
	    parp->ar_pln != 4)
		return;

	arpptr = (u8 *)parp + sizeof(struct arphdr);
	sha = arpptr;
	arpptr += dev->addr_len;	/* sha */
	memcpy(&sip, arpptr, sizeof(sip));
	arpptr += sizeof(sip);
	arpptr += dev->addr_len;	/* tha */
	memcpy(&tip, arpptr, sizeof(tip));

	if (ipv4_is_loopback(tip) ||
	    ipv4_is_multicast(tip))
		return;

	if (br_opt_get(br, BROPT_NEIGH_SUPPRESS_ENABLED)) {
		if (br_is_neigh_suppress_enabled(p, vid))
			return;
		if (parp->ar_op != htons(ARPOP_RREQUEST) &&
		    parp->ar_op != htons(ARPOP_RREPLY) &&
		    (ipv4_is_zeronet(sip) || sip == tip)) {
			/* prevent flooding to neigh suppress ports */
			BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
			return;
		}
	}

	if (parp->ar_op != htons(ARPOP_REQUEST))
		return;

	if (vid != 0) {
		vlandev = __vlan_find_dev_deep_rcu(br->dev, skb->vlan_proto,
						   vid);
		if (!vlandev)
			return;
	}

	if (br_opt_get(br, BROPT_NEIGH_SUPPRESS_ENABLED) &&
	    br_is_local_ip(vlandev, tip)) {
		/* its our local ip, so don't proxy reply
		 * and don't forward to neigh suppress ports
		 */
		BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
		return;
	}

	n = neigh_lookup(&arp_tbl, &tip, vlandev);
	if (n) {
		struct net_bridge_fdb_entry *f;

		if (!(READ_ONCE(n->nud_state) & NUD_VALID)) {
			neigh_release(n);
			return;
		}

		f = br_fdb_find_rcu(br, n->ha, vid);
		if (f) {
			bool replied = false;

			if ((p && (p->flags & BR_PROXYARP)) ||
			    (f->dst && (f->dst->flags & BR_PROXYARP_WIFI)) ||
			    br_is_neigh_suppress_enabled(f->dst, vid)) {
				if (!vid)
					br_arp_send(br, p, skb->dev, sip, tip,
						    sha, n->ha, sha, 0, 0);
				else
					br_arp_send(br, p, skb->dev, sip, tip,
						    sha, n->ha, sha,
						    skb->vlan_proto,
						    skb_vlan_tag_get(skb));
				replied = true;
			}

			/* If we have replied or as long as we know the
			 * mac, indicate to arp replied
			 */
			if (replied ||
			    br_opt_get(br, BROPT_NEIGH_SUPPRESS_ENABLED))
				BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
		}

		neigh_release(n);
	}
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
struct nd_msg *br_is_nd_neigh_msg(struct sk_buff *skb, struct nd_msg *msg)
{
	struct nd_msg *m;

	m = skb_header_pointer(skb, skb_network_offset(skb) +
			       sizeof(struct ipv6hdr), sizeof(*msg), msg);
	if (!m)
		return NULL;

	if (m->icmph.icmp6_code != 0 ||
	    (m->icmph.icmp6_type != NDISC_NEIGHBOUR_SOLICITATION &&
	     m->icmph.icmp6_type != NDISC_NEIGHBOUR_ADVERTISEMENT))
		return NULL;

	return m;
}

static void br_nd_send(struct net_bridge *br, struct net_bridge_port *p,
		       struct sk_buff *request, struct neighbour *n,
		       __be16 vlan_proto, u16 vlan_tci, struct nd_msg *ns)
{
	struct net_device *dev = request->dev;
	struct net_bridge_vlan_group *vg;
	struct sk_buff *reply;
	struct nd_msg *na;
	struct ipv6hdr *pip6;
	int na_olen = 8; /* opt hdr + ETH_ALEN for target */
	int ns_olen;
	int i, len;
	u8 *daddr;
	u16 pvid;

	if (!dev)
		return;

	len = LL_RESERVED_SPACE(dev) + sizeof(struct ipv6hdr) +
		sizeof(*na) + na_olen + dev->needed_tailroom;

	reply = alloc_skb(len, GFP_ATOMIC);
	if (!reply)
		return;

	reply->protocol = htons(ETH_P_IPV6);
	reply->dev = dev;
	skb_reserve(reply, LL_RESERVED_SPACE(dev));
	skb_push(reply, sizeof(struct ethhdr));
	skb_set_mac_header(reply, 0);

	daddr = eth_hdr(request)->h_source;

	/* Do we need option processing ? */
	ns_olen = request->len - (skb_network_offset(request) +
				  sizeof(struct ipv6hdr)) - sizeof(*ns);
	for (i = 0; i < ns_olen - 1; i += (ns->opt[i + 1] << 3)) {
		if (!ns->opt[i + 1]) {
			kfree_skb(reply);
			return;
		}
		if (ns->opt[i] == ND_OPT_SOURCE_LL_ADDR) {
			daddr = ns->opt + i + sizeof(struct nd_opt_hdr);
			break;
		}
	}

	/* Ethernet header */
	ether_addr_copy(eth_hdr(reply)->h_dest, daddr);
	ether_addr_copy(eth_hdr(reply)->h_source, n->ha);
	eth_hdr(reply)->h_proto = htons(ETH_P_IPV6);
	reply->protocol = htons(ETH_P_IPV6);

	skb_pull(reply, sizeof(struct ethhdr));
	skb_set_network_header(reply, 0);
	skb_put(reply, sizeof(struct ipv6hdr));

	/* IPv6 header */
	pip6 = ipv6_hdr(reply);
	memset(pip6, 0, sizeof(struct ipv6hdr));
	pip6->version = 6;
	pip6->priority = ipv6_hdr(request)->priority;
	pip6->nexthdr = IPPROTO_ICMPV6;
	pip6->hop_limit = 255;
	pip6->daddr = ipv6_hdr(request)->saddr;
	pip6->saddr = *(struct in6_addr *)n->primary_key;

	skb_pull(reply, sizeof(struct ipv6hdr));
	skb_set_transport_header(reply, 0);

	na = (struct nd_msg *)skb_put(reply, sizeof(*na) + na_olen);

	/* Neighbor Advertisement */
	memset(na, 0, sizeof(*na) + na_olen);
	na->icmph.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
	na->icmph.icmp6_router = (n->flags & NTF_ROUTER) ? 1 : 0;
	na->icmph.icmp6_override = 1;
	na->icmph.icmp6_solicited = 1;
	na->target = ns->target;
	ether_addr_copy(&na->opt[2], n->ha);
	na->opt[0] = ND_OPT_TARGET_LL_ADDR;
	na->opt[1] = na_olen >> 3;

	na->icmph.icmp6_cksum = csum_ipv6_magic(&pip6->saddr,
						&pip6->daddr,
						sizeof(*na) + na_olen,
						IPPROTO_ICMPV6,
						csum_partial(na, sizeof(*na) + na_olen, 0));

	pip6->payload_len = htons(sizeof(*na) + na_olen);

	skb_push(reply, sizeof(struct ipv6hdr));
	skb_push(reply, sizeof(struct ethhdr));

	reply->ip_summed = CHECKSUM_UNNECESSARY;

	if (p)
		vg = nbp_vlan_group_rcu(p);
	else
		vg = br_vlan_group_rcu(br);
	pvid = br_get_pvid(vg);
	if (pvid == (vlan_tci & VLAN_VID_MASK))
		vlan_tci = 0;

	if (vlan_tci)
		__vlan_hwaccel_put_tag(reply, vlan_proto, vlan_tci);

	netdev_dbg(dev, "nd send dev %s dst %pI6 dst_hw %pM src %pI6 src_hw %pM\n",
		   dev->name, &pip6->daddr, daddr, &pip6->saddr, n->ha);

	if (p) {
		dev_queue_xmit(reply);
	} else {
		skb_reset_mac_header(reply);
		__skb_pull(reply, skb_network_offset(reply));
		reply->ip_summed = CHECKSUM_UNNECESSARY;
		reply->pkt_type = PACKET_HOST;

		netif_rx(reply);
	}
}

static int br_chk_addr_ip6(struct net_device *dev,
			   struct netdev_nested_priv *priv)
{
	struct in6_addr *addr = (struct in6_addr *)priv->data;

	if (ipv6_chk_addr(dev_net(dev), addr, dev, 0))
		return 1;

	return 0;
}

static bool br_is_local_ip6(struct net_device *dev, struct in6_addr *addr)

{
	struct netdev_nested_priv priv = {
		.data = (void *)addr,
	};

	if (br_chk_addr_ip6(dev, &priv))
		return true;

	/* check if ip is configured on upper dev */
	if (netdev_walk_all_upper_dev_rcu(dev, br_chk_addr_ip6, &priv))
		return true;

	return false;
}

void br_do_suppress_nd(struct sk_buff *skb, struct net_bridge *br,
		       u16 vid, struct net_bridge_port *p, struct nd_msg *msg)
{
	struct net_device *dev = br->dev;
	struct net_device *vlandev = NULL;
	struct in6_addr *saddr, *daddr;
	struct ipv6hdr *iphdr;
	struct neighbour *n;

	BR_INPUT_SKB_CB(skb)->proxyarp_replied = 0;

	if (br_is_neigh_suppress_enabled(p, vid))
		return;

	if (msg->icmph.icmp6_type == NDISC_NEIGHBOUR_ADVERTISEMENT &&
	    !msg->icmph.icmp6_solicited) {
		/* prevent flooding to neigh suppress ports */
		BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
		return;
	}

	if (msg->icmph.icmp6_type != NDISC_NEIGHBOUR_SOLICITATION)
		return;

	iphdr = ipv6_hdr(skb);
	saddr = &iphdr->saddr;
	daddr = &iphdr->daddr;

	if (ipv6_addr_any(saddr) || !ipv6_addr_cmp(saddr, daddr)) {
		/* prevent flooding to neigh suppress ports */
		BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
		return;
	}

	if (vid != 0) {
		/* build neigh table lookup on the vlan device */
		vlandev = __vlan_find_dev_deep_rcu(br->dev, skb->vlan_proto,
						   vid);
		if (!vlandev)
			return;
	} else {
		vlandev = dev;
	}

	if (br_is_local_ip6(vlandev, &msg->target)) {
		/* its our own ip, so don't proxy reply
		 * and don't forward to arp suppress ports
		 */
		BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
		return;
	}

	n = neigh_lookup(ipv6_stub->nd_tbl, &msg->target, vlandev);
	if (n) {
		struct net_bridge_fdb_entry *f;

		if (!(READ_ONCE(n->nud_state) & NUD_VALID)) {
			neigh_release(n);
			return;
		}

		f = br_fdb_find_rcu(br, n->ha, vid);
		if (f) {
			bool replied = false;

			if (br_is_neigh_suppress_enabled(f->dst, vid)) {
				if (vid != 0)
					br_nd_send(br, p, skb, n,
						   skb->vlan_proto,
						   skb_vlan_tag_get(skb), msg);
				else
					br_nd_send(br, p, skb, n, 0, 0, msg);
				replied = true;
			}

			/* If we have replied or as long as we know the
			 * mac, indicate to NEIGH_SUPPRESS ports that we
			 * have replied
			 */
			if (replied ||
			    br_opt_get(br, BROPT_NEIGH_SUPPRESS_ENABLED))
				BR_INPUT_SKB_CB(skb)->proxyarp_replied = 1;
		}
		neigh_release(n);
	}
}
#endif

bool br_is_neigh_suppress_enabled(const struct net_bridge_port *p, u16 vid)
{
	if (!p)
		return false;

	if (!vid)
		return !!(p->flags & BR_NEIGH_SUPPRESS);

	if (p->flags & BR_NEIGH_VLAN_SUPPRESS) {
		struct net_bridge_vlan_group *vg = nbp_vlan_group_rcu(p);
		struct net_bridge_vlan *v;

		v = br_vlan_find(vg, vid);
		if (!v)
			return false;
		return !!(v->priv_flags & BR_VLFLAG_NEIGH_SUPPRESS_ENABLED);
	} else {
		return !!(p->flags & BR_NEIGH_SUPPRESS);
	}
}
