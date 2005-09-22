/*
 *	Handle firewalling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek               <buytenh@gnu.org>
 *	Bart De Schuymer (maintainer)	<bdschuym@pandora.be>
 *
 *	Changes:
 *	Apr 29 2003: physdev module support (bdschuym)
 *	Jun 19 2003: let arptables see bridged ARP traffic (bdschuym)
 *	Oct 06 2003: filter encapsulated IP/ARP VLAN traffic on untagged bridge
 *	             (bdschuym)
 *	Sep 01 2004: add IPv6 filtering (bdschuym)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Lennert dedicates this file to Kerstin Wurdinger.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_arp.h>
#include <linux/in_route.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include "br_private.h"
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#define skb_origaddr(skb)	 (((struct bridge_skb_cb *) \
				 (skb->nf_bridge->data))->daddr.ipv4)
#define store_orig_dstaddr(skb)	 (skb_origaddr(skb) = (skb)->nh.iph->daddr)
#define dnat_took_place(skb)	 (skb_origaddr(skb) != (skb)->nh.iph->daddr)

#define has_bridge_parent(device)	((device)->br_port != NULL)
#define bridge_parent(device)		((device)->br_port->br->dev)

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *brnf_sysctl_header;
static int brnf_call_iptables = 1;
static int brnf_call_ip6tables = 1;
static int brnf_call_arptables = 1;
static int brnf_filter_vlan_tagged = 1;
#else
#define brnf_filter_vlan_tagged 1
#endif

#define IS_VLAN_IP (skb->protocol == __constant_htons(ETH_P_8021Q) &&    \
	hdr->h_vlan_encapsulated_proto == __constant_htons(ETH_P_IP) &&  \
	brnf_filter_vlan_tagged)
#define IS_VLAN_IPV6 (skb->protocol == __constant_htons(ETH_P_8021Q) &&    \
	hdr->h_vlan_encapsulated_proto == __constant_htons(ETH_P_IPV6) &&  \
	brnf_filter_vlan_tagged)
#define IS_VLAN_ARP (skb->protocol == __constant_htons(ETH_P_8021Q) &&   \
	hdr->h_vlan_encapsulated_proto == __constant_htons(ETH_P_ARP) && \
	brnf_filter_vlan_tagged)

/* We need these fake structures to make netfilter happy --
 * lots of places assume that skb->dst != NULL, which isn't
 * all that unreasonable.
 *
 * Currently, we fill in the PMTU entry because netfilter
 * refragmentation needs it, and the rt_flags entry because
 * ipt_REJECT needs it.  Future netfilter modules might
 * require us to fill additional fields. */
static struct net_device __fake_net_device = {
	.hard_header_len	= ETH_HLEN
};

static struct rtable __fake_rtable = {
	.u = {
		.dst = {
			.__refcnt		= ATOMIC_INIT(1),
			.dev			= &__fake_net_device,
			.path			= &__fake_rtable.u.dst,
			.metrics		= {[RTAX_MTU - 1] = 1500},
		}
	},
	.rt_flags	= 0,
};


/* PF_BRIDGE/PRE_ROUTING *********************************************/
/* Undo the changes made for ip6tables PREROUTING and continue the
 * bridge PRE_ROUTING hook. */
static int br_nf_pre_routing_finish_ipv6(struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;

	if (nf_bridge->mask & BRNF_PKT_TYPE) {
		skb->pkt_type = PACKET_OTHERHOST;
		nf_bridge->mask ^= BRNF_PKT_TYPE;
	}
	nf_bridge->mask ^= BRNF_NF_BRIDGE_PREROUTING;

	skb->dst = (struct dst_entry *)&__fake_rtable;
	dst_hold(skb->dst);

	skb->dev = nf_bridge->physindev;
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}
	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_PRE_ROUTING, skb, skb->dev, NULL,
		       br_handle_frame_finish, 1);

	return 0;
}

static void __br_dnat_complain(void)
{
	static unsigned long last_complaint;

	if (jiffies - last_complaint >= 5 * HZ) {
		printk(KERN_WARNING "Performing cross-bridge DNAT requires IP "
			"forwarding to be enabled\n");
		last_complaint = jiffies;
	}
}

/* This requires some explaining. If DNAT has taken place,
 * we will need to fix up the destination Ethernet address,
 * and this is a tricky process.
 *
 * There are two cases to consider:
 * 1. The packet was DNAT'ed to a device in the same bridge
 *    port group as it was received on. We can still bridge
 *    the packet.
 * 2. The packet was DNAT'ed to a different device, either
 *    a non-bridged device or another bridge port group.
 *    The packet will need to be routed.
 *
 * The correct way of distinguishing between these two cases is to
 * call ip_route_input() and to look at skb->dst->dev, which is
 * changed to the destination device if ip_route_input() succeeds.
 *
 * Let us first consider the case that ip_route_input() succeeds:
 *
 * If skb->dst->dev equals the logical bridge device the packet
 * came in on, we can consider this bridging. We then call
 * skb->dst->output() which will make the packet enter br_nf_local_out()
 * not much later. In that function it is assured that the iptables
 * FORWARD chain is traversed for the packet.
 *
 * Otherwise, the packet is considered to be routed and we just
 * change the destination MAC address so that the packet will
 * later be passed up to the IP stack to be routed.
 *
 * Let us now consider the case that ip_route_input() fails:
 *
 * After a "echo '0' > /proc/sys/net/ipv4/ip_forward" ip_route_input()
 * will fail, while __ip_route_output_key() will return success. The source
 * address for __ip_route_output_key() is set to zero, so __ip_route_output_key
 * thinks we're handling a locally generated packet and won't care
 * if IP forwarding is allowed. We send a warning message to the users's
 * log telling her to put IP forwarding on.
 *
 * ip_route_input() will also fail if there is no route available.
 * In that case we just drop the packet.
 *
 * --Lennert, 20020411
 * --Bart, 20020416 (updated)
 * --Bart, 20021007 (updated) */
static int br_nf_pre_routing_finish_bridge(struct sk_buff *skb)
{
	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		skb->nf_bridge->mask |= BRNF_PKT_TYPE;
	}
	skb->nf_bridge->mask ^= BRNF_NF_BRIDGE_PREROUTING;

	skb->dev = bridge_parent(skb->dev);
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(skb, VLAN_HLEN);
		skb->nh.raw += VLAN_HLEN;
	}
	skb->dst->output(skb);
	return 0;
}

static int br_nf_pre_routing_finish(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct iphdr *iph = skb->nh.iph;
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;

	if (nf_bridge->mask & BRNF_PKT_TYPE) {
		skb->pkt_type = PACKET_OTHERHOST;
		nf_bridge->mask ^= BRNF_PKT_TYPE;
	}
	nf_bridge->mask ^= BRNF_NF_BRIDGE_PREROUTING;

	if (dnat_took_place(skb)) {
		if (ip_route_input(skb, iph->daddr, iph->saddr, iph->tos,
		    dev)) {
			struct rtable *rt;
			struct flowi fl = { .nl_u = 
			{ .ip4_u = { .daddr = iph->daddr, .saddr = 0 ,
				     .tos = RT_TOS(iph->tos)} }, .proto = 0};

			if (!ip_route_output_key(&rt, &fl)) {
				/* - Bridged-and-DNAT'ed traffic doesn't
				 *   require ip_forwarding.
				 * - Deal with redirected traffic. */
				if (((struct dst_entry *)rt)->dev == dev ||
				    rt->rt_type == RTN_LOCAL) {
					skb->dst = (struct dst_entry *)rt;
					goto bridged_dnat;
				}
				__br_dnat_complain();
				dst_release((struct dst_entry *)rt);
			}
			kfree_skb(skb);
			return 0;
		} else {
			if (skb->dst->dev == dev) {
bridged_dnat:
				/* Tell br_nf_local_out this is a
				 * bridged frame */
				nf_bridge->mask |= BRNF_BRIDGED_DNAT;
				skb->dev = nf_bridge->physindev;
				if (skb->protocol ==
				    __constant_htons(ETH_P_8021Q)) {
					skb_push(skb, VLAN_HLEN);
					skb->nh.raw -= VLAN_HLEN;
				}
				NF_HOOK_THRESH(PF_BRIDGE, NF_BR_PRE_ROUTING,
					       skb, skb->dev, NULL,
					       br_nf_pre_routing_finish_bridge,
					       1);
				return 0;
			}
			memcpy(eth_hdr(skb)->h_dest, dev->dev_addr,
			       ETH_ALEN);
			skb->pkt_type = PACKET_HOST;
		}
	} else {
		skb->dst = (struct dst_entry *)&__fake_rtable;
		dst_hold(skb->dst);
	}

	skb->dev = nf_bridge->physindev;
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}
	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_PRE_ROUTING, skb, skb->dev, NULL,
		       br_handle_frame_finish, 1);

	return 0;
}

/* Some common code for IPv4/IPv6 */
static void setup_pre_routing(struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;

	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->mask |= BRNF_PKT_TYPE;
	}

	nf_bridge->mask |= BRNF_NF_BRIDGE_PREROUTING;
	nf_bridge->physindev = skb->dev;
	skb->dev = bridge_parent(skb->dev);
}

/* We only check the length. A bridge shouldn't do any hop-by-hop stuff anyway */
static int check_hbh_len(struct sk_buff *skb)
{
	unsigned char *raw = (u8*)(skb->nh.ipv6h+1);
	u32 pkt_len;
	int off = raw - skb->nh.raw;
	int len = (raw[1]+1)<<3;

	if ((raw + len) - skb->data > skb_headlen(skb))
		goto bad;

	off += 2;
	len -= 2;

	while (len > 0) {
		int optlen = raw[off+1]+2;

		switch (skb->nh.raw[off]) {
		case IPV6_TLV_PAD0:
			optlen = 1;
			break;

		case IPV6_TLV_PADN:
			break;

		case IPV6_TLV_JUMBO:
			if (skb->nh.raw[off+1] != 4 || (off&3) != 2)
				goto bad;

			pkt_len = ntohl(*(u32*)(skb->nh.raw+off+2));

			if (pkt_len > skb->len - sizeof(struct ipv6hdr))
				goto bad;
			if (pkt_len + sizeof(struct ipv6hdr) < skb->len) {
				if (__pskb_trim(skb,
				    pkt_len + sizeof(struct ipv6hdr)))
					goto bad;
				if (skb->ip_summed == CHECKSUM_HW)
					skb->ip_summed = CHECKSUM_NONE;
			}
			break;
		default:
			if (optlen > len)
				goto bad;
			break;
		}
		off += optlen;
		len -= optlen;
	}
	if (len == 0)
		return 0;
bad:
	return -1;

}

/* Replicate the checks that IPv6 does on packet reception and pass the packet
 * to ip6tables, which doesn't support NAT, so things are fairly simple. */
static unsigned int br_nf_pre_routing_ipv6(unsigned int hook,
   struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, int (*okfn)(struct sk_buff *))
{
	struct ipv6hdr *hdr;
	u32 pkt_len;
	struct nf_bridge_info *nf_bridge;

	if (skb->len < sizeof(struct ipv6hdr))
		goto inhdr_error;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto inhdr_error;

	hdr = skb->nh.ipv6h;

	if (hdr->version != 6)
		goto inhdr_error;

	pkt_len = ntohs(hdr->payload_len);

	if (pkt_len || hdr->nexthdr != NEXTHDR_HOP) {
		if (pkt_len + sizeof(struct ipv6hdr) > skb->len)
			goto inhdr_error;
		if (pkt_len + sizeof(struct ipv6hdr) < skb->len) {
			if (__pskb_trim(skb, pkt_len + sizeof(struct ipv6hdr)))
				goto inhdr_error;
			if (skb->ip_summed == CHECKSUM_HW)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}
	if (hdr->nexthdr == NEXTHDR_HOP && check_hbh_len(skb))
			goto inhdr_error;

	if ((nf_bridge = nf_bridge_alloc(skb)) == NULL)
		return NF_DROP;
	setup_pre_routing(skb);

	NF_HOOK(PF_INET6, NF_IP6_PRE_ROUTING, skb, skb->dev, NULL,
		br_nf_pre_routing_finish_ipv6);

	return NF_STOLEN;

inhdr_error:
	return NF_DROP;
}

/* Direct IPv6 traffic to br_nf_pre_routing_ipv6.
 * Replicate the checks that IPv4 does on packet reception.
 * Set skb->dev to the bridge device (i.e. parent of the
 * receiving device) to make netfilter happy, the REDIRECT
 * target in particular.  Save the original destination IP
 * address to be able to detect DNAT afterwards. */
static unsigned int br_nf_pre_routing(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct iphdr *iph;
	__u32 len;
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge;
	struct vlan_ethhdr *hdr = vlan_eth_hdr(*pskb);

	if (skb->protocol == __constant_htons(ETH_P_IPV6) || IS_VLAN_IPV6) {
#ifdef CONFIG_SYSCTL
		if (!brnf_call_ip6tables)
			return NF_ACCEPT;
#endif
		if ((skb = skb_share_check(*pskb, GFP_ATOMIC)) == NULL)
			goto out;

		if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
			skb_pull(skb, VLAN_HLEN);
			(skb)->nh.raw += VLAN_HLEN;
		}
		return br_nf_pre_routing_ipv6(hook, skb, in, out, okfn);
	}
#ifdef CONFIG_SYSCTL
	if (!brnf_call_iptables)
		return NF_ACCEPT;
#endif

	if (skb->protocol != __constant_htons(ETH_P_IP) && !IS_VLAN_IP)
		return NF_ACCEPT;

	if ((skb = skb_share_check(*pskb, GFP_ATOMIC)) == NULL)
		goto out;

	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(skb, VLAN_HLEN);
		(skb)->nh.raw += VLAN_HLEN;
	}

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto inhdr_error;

	iph = skb->nh.iph;
	if (iph->ihl < 5 || iph->version != 4)
		goto inhdr_error;

	if (!pskb_may_pull(skb, 4*iph->ihl))
		goto inhdr_error;

	iph = skb->nh.iph;
	if (ip_fast_csum((__u8 *)iph, iph->ihl) != 0)
		goto inhdr_error;

	len = ntohs(iph->tot_len);
	if (skb->len < len || len < 4*iph->ihl)
		goto inhdr_error;

	if (skb->len > len) {
		__pskb_trim(skb, len);
		if (skb->ip_summed == CHECKSUM_HW)
			skb->ip_summed = CHECKSUM_NONE;
	}

	if ((nf_bridge = nf_bridge_alloc(skb)) == NULL)
		return NF_DROP;
	setup_pre_routing(skb);
	store_orig_dstaddr(skb);

	NF_HOOK(PF_INET, NF_IP_PRE_ROUTING, skb, skb->dev, NULL,
		br_nf_pre_routing_finish);

	return NF_STOLEN;

inhdr_error:
//	IP_INC_STATS_BH(IpInHdrErrors);
out:
	return NF_DROP;
}


/* PF_BRIDGE/LOCAL_IN ************************************************/
/* The packet is locally destined, which requires a real
 * dst_entry, so detach the fake one.  On the way up, the
 * packet would pass through PRE_ROUTING again (which already
 * took place when the packet entered the bridge), but we
 * register an IPv4 PRE_ROUTING 'sabotage' hook that will
 * prevent this from happening. */
static unsigned int br_nf_local_in(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;

	if (skb->dst == (struct dst_entry *)&__fake_rtable) {
		dst_release(skb->dst);
		skb->dst = NULL;
	}

	return NF_ACCEPT;
}


/* PF_BRIDGE/FORWARD *************************************************/
static int br_nf_forward_finish(struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = skb->nf_bridge;
	struct net_device *in;
	struct vlan_ethhdr *hdr = vlan_eth_hdr(skb);

	if (skb->protocol != __constant_htons(ETH_P_ARP) && !IS_VLAN_ARP) {
		in = nf_bridge->physindev;
		if (nf_bridge->mask & BRNF_PKT_TYPE) {
			skb->pkt_type = PACKET_OTHERHOST;
			nf_bridge->mask ^= BRNF_PKT_TYPE;
		}
	} else {
		in = *((struct net_device **)(skb->cb));
	}
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}
	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_FORWARD, skb, in,
			skb->dev, br_forward_finish, 1);
	return 0;
}

/* This is the 'purely bridged' case.  For IP, we pass the packet to
 * netfilter with indev and outdev set to the bridge device,
 * but we are still able to filter on the 'real' indev/outdev
 * because of the physdev module. For ARP, indev and outdev are the
 * bridge ports. */
static unsigned int br_nf_forward_ip(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge;
	struct vlan_ethhdr *hdr = vlan_eth_hdr(skb);
	int pf;

	if (!skb->nf_bridge)
		return NF_ACCEPT;

	if (skb->protocol == __constant_htons(ETH_P_IP) || IS_VLAN_IP)
		pf = PF_INET;
	else
		pf = PF_INET6;

	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(*pskb, VLAN_HLEN);
		(*pskb)->nh.raw += VLAN_HLEN;
	}

	nf_bridge = skb->nf_bridge;
	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->mask |= BRNF_PKT_TYPE;
	}

	/* The physdev module checks on this */
	nf_bridge->mask |= BRNF_BRIDGED;
	nf_bridge->physoutdev = skb->dev;

	NF_HOOK(pf, NF_IP_FORWARD, skb, bridge_parent(in),
		bridge_parent(out), br_nf_forward_finish);

	return NF_STOLEN;
}

static unsigned int br_nf_forward_arp(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;
	struct vlan_ethhdr *hdr = vlan_eth_hdr(skb);
	struct net_device **d = (struct net_device **)(skb->cb);

#ifdef CONFIG_SYSCTL
	if (!brnf_call_arptables)
		return NF_ACCEPT;
#endif

	if (skb->protocol != __constant_htons(ETH_P_ARP)) {
		if (!IS_VLAN_ARP)
			return NF_ACCEPT;
		skb_pull(*pskb, VLAN_HLEN);
		(*pskb)->nh.raw += VLAN_HLEN;
	}

	if (skb->nh.arph->ar_pln != 4) {
		if (IS_VLAN_ARP) {
			skb_push(*pskb, VLAN_HLEN);
			(*pskb)->nh.raw -= VLAN_HLEN;
		}
		return NF_ACCEPT;
	}
	*d = (struct net_device *)in;
	NF_HOOK(NF_ARP, NF_ARP_FORWARD, skb, (struct net_device *)in,
		(struct net_device *)out, br_nf_forward_finish);

	return NF_STOLEN;
}


/* PF_BRIDGE/LOCAL_OUT ***********************************************/
static int br_nf_local_out_finish(struct sk_buff *skb)
{
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_push(skb, VLAN_HLEN);
		skb->nh.raw -= VLAN_HLEN;
	}

	NF_HOOK_THRESH(PF_BRIDGE, NF_BR_LOCAL_OUT, skb, NULL, skb->dev,
			br_forward_finish, NF_BR_PRI_FIRST + 1);

	return 0;
}

/* This function sees both locally originated IP packets and forwarded
 * IP packets (in both cases the destination device is a bridge
 * device). It also sees bridged-and-DNAT'ed packets.
 * To be able to filter on the physical bridge devices (with the physdev
 * module), we steal packets destined to a bridge device away from the
 * PF_INET/FORWARD and PF_INET/OUTPUT hook functions, and give them back later,
 * when we have determined the real output device. This is done in here.
 *
 * If (nf_bridge->mask & BRNF_BRIDGED_DNAT) then the packet is bridged
 * and we fake the PF_BRIDGE/FORWARD hook. The function br_nf_forward()
 * will then fake the PF_INET/FORWARD hook. br_nf_local_out() has priority
 * NF_BR_PRI_FIRST, so no relevant PF_BRIDGE/INPUT functions have been nor
 * will be executed.
 * Otherwise, if nf_bridge->physindev is NULL, the bridge-nf code never touched
 * this packet before, and so the packet was locally originated. We fake
 * the PF_INET/LOCAL_OUT hook.
 * Finally, if nf_bridge->physindev isn't NULL, then the packet was IP routed,
 * so we fake the PF_INET/FORWARD hook. ip_sabotage_out() makes sure
 * even routed packets that didn't arrive on a bridge interface have their
 * nf_bridge->physindev set. */
static unsigned int br_nf_local_out(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct net_device *realindev, *realoutdev;
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge;
	struct vlan_ethhdr *hdr = vlan_eth_hdr(skb);
	int pf;

	if (!skb->nf_bridge)
		return NF_ACCEPT;

	if (skb->protocol == __constant_htons(ETH_P_IP) || IS_VLAN_IP)
		pf = PF_INET;
	else
		pf = PF_INET6;

#ifdef CONFIG_NETFILTER_DEBUG
	/* Sometimes we get packets with NULL ->dst here (for example,
	 * running a dhcp client daemon triggers this). This should now
	 * be fixed, but let's keep the check around. */
	if (skb->dst == NULL) {
		printk(KERN_CRIT "br_netfilter: skb->dst == NULL.");
		return NF_ACCEPT;
	}
#endif

	nf_bridge = skb->nf_bridge;
	nf_bridge->physoutdev = skb->dev;
	realindev = nf_bridge->physindev;

	/* Bridged, take PF_BRIDGE/FORWARD.
	 * (see big note in front of br_nf_pre_routing_finish) */
	if (nf_bridge->mask & BRNF_BRIDGED_DNAT) {
		if (nf_bridge->mask & BRNF_PKT_TYPE) {
			skb->pkt_type = PACKET_OTHERHOST;
			nf_bridge->mask ^= BRNF_PKT_TYPE;
		}
		if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
			skb_push(skb, VLAN_HLEN);
			skb->nh.raw -= VLAN_HLEN;
		}

		NF_HOOK(PF_BRIDGE, NF_BR_FORWARD, skb, realindev,
			skb->dev, br_forward_finish);
		goto out;
	}
	realoutdev = bridge_parent(skb->dev);

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	/* iptables should match -o br0.x */
	if (nf_bridge->netoutdev)
		realoutdev = nf_bridge->netoutdev;
#endif
	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(skb, VLAN_HLEN);
		(*pskb)->nh.raw += VLAN_HLEN;
	}
	/* IP forwarded traffic has a physindev, locally
	 * generated traffic hasn't. */
	if (realindev != NULL) {
		if (!(nf_bridge->mask & BRNF_DONT_TAKE_PARENT) &&
		    has_bridge_parent(realindev))
			realindev = bridge_parent(realindev);

		NF_HOOK_THRESH(pf, NF_IP_FORWARD, skb, realindev,
			       realoutdev, br_nf_local_out_finish,
			       NF_IP_PRI_BRIDGE_SABOTAGE_FORWARD + 1);
	} else {
		NF_HOOK_THRESH(pf, NF_IP_LOCAL_OUT, skb, realindev,
			       realoutdev, br_nf_local_out_finish,
			       NF_IP_PRI_BRIDGE_SABOTAGE_LOCAL_OUT + 1);
	}

out:
	return NF_STOLEN;
}


/* PF_BRIDGE/POST_ROUTING ********************************************/
static unsigned int br_nf_post_routing(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;
	struct nf_bridge_info *nf_bridge = (*pskb)->nf_bridge;
	struct vlan_ethhdr *hdr = vlan_eth_hdr(skb);
	struct net_device *realoutdev = bridge_parent(skb->dev);
	int pf;

#ifdef CONFIG_NETFILTER_DEBUG
	/* Be very paranoid. This probably won't happen anymore, but let's
	 * keep the check just to be sure... */
	if (skb->mac.raw < skb->head || skb->mac.raw + ETH_HLEN > skb->data) {
		printk(KERN_CRIT "br_netfilter: Argh!! br_nf_post_routing: "
				 "bad mac.raw pointer.");
		goto print_error;
	}
#endif

	if (!nf_bridge)
		return NF_ACCEPT;

	if (skb->protocol == __constant_htons(ETH_P_IP) || IS_VLAN_IP)
		pf = PF_INET;
	else
		pf = PF_INET6;

#ifdef CONFIG_NETFILTER_DEBUG
	if (skb->dst == NULL) {
		printk(KERN_CRIT "br_netfilter: skb->dst == NULL.");
		goto print_error;
	}
#endif

	/* We assume any code from br_dev_queue_push_xmit onwards doesn't care
	 * about the value of skb->pkt_type. */
	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->mask |= BRNF_PKT_TYPE;
	}

	if (skb->protocol == __constant_htons(ETH_P_8021Q)) {
		skb_pull(skb, VLAN_HLEN);
		skb->nh.raw += VLAN_HLEN;
	}

	nf_bridge_save_header(skb);

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	if (nf_bridge->netoutdev)
		realoutdev = nf_bridge->netoutdev;
#endif
	NF_HOOK(pf, NF_IP_POST_ROUTING, skb, NULL, realoutdev,
	        br_dev_queue_push_xmit);

	return NF_STOLEN;

#ifdef CONFIG_NETFILTER_DEBUG
print_error:
	if (skb->dev != NULL) {
		printk("[%s]", skb->dev->name);
		if (has_bridge_parent(skb->dev))
			printk("[%s]", bridge_parent(skb->dev)->name);
	}
	printk(" head:%p, raw:%p, data:%p\n", skb->head, skb->mac.raw,
					      skb->data);
	return NF_ACCEPT;
#endif
}


/* IP/SABOTAGE *****************************************************/
/* Don't hand locally destined packets to PF_INET(6)/PRE_ROUTING
 * for the second time. */
static unsigned int ip_sabotage_in(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	if ((*pskb)->nf_bridge &&
	    !((*pskb)->nf_bridge->mask & BRNF_NF_BRIDGE_PREROUTING)) {
		return NF_STOP;
	}

	return NF_ACCEPT;
}

/* Postpone execution of PF_INET(6)/FORWARD, PF_INET(6)/LOCAL_OUT
 * and PF_INET(6)/POST_ROUTING until we have done the forwarding
 * decision in the bridge code and have determined nf_bridge->physoutdev. */
static unsigned int ip_sabotage_out(unsigned int hook, struct sk_buff **pskb,
   const struct net_device *in, const struct net_device *out,
   int (*okfn)(struct sk_buff *))
{
	struct sk_buff *skb = *pskb;

	if ((out->hard_start_xmit == br_dev_xmit &&
	    okfn != br_nf_forward_finish &&
	    okfn != br_nf_local_out_finish &&
	    okfn != br_dev_queue_push_xmit)
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
	    || ((out->priv_flags & IFF_802_1Q_VLAN) &&
	    VLAN_DEV_INFO(out)->real_dev->hard_start_xmit == br_dev_xmit)
#endif
	    ) {
		struct nf_bridge_info *nf_bridge;

		if (!skb->nf_bridge) {
#ifdef CONFIG_SYSCTL
			/* This code is executed while in the IP(v6) stack,
			   the version should be 4 or 6. We can't use
			   skb->protocol because that isn't set on
			   PF_INET(6)/LOCAL_OUT. */
			struct iphdr *ip = skb->nh.iph;

			if (ip->version == 4 && !brnf_call_iptables)
				return NF_ACCEPT;
			else if (ip->version == 6 && !brnf_call_ip6tables)
				return NF_ACCEPT;
#endif
			if (hook == NF_IP_POST_ROUTING)
				return NF_ACCEPT;
			if (!nf_bridge_alloc(skb))
				return NF_DROP;
		}

		nf_bridge = skb->nf_bridge;

		/* This frame will arrive on PF_BRIDGE/LOCAL_OUT and we
		 * will need the indev then. For a brouter, the real indev
		 * can be a bridge port, so we make sure br_nf_local_out()
		 * doesn't use the bridge parent of the indev by using
		 * the BRNF_DONT_TAKE_PARENT mask. */
		if (hook == NF_IP_FORWARD && nf_bridge->physindev == NULL) {
			nf_bridge->mask |= BRNF_DONT_TAKE_PARENT;
			nf_bridge->physindev = (struct net_device *)in;
		}
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
		/* the iptables outdev is br0.x, not br0 */
		if (out->priv_flags & IFF_802_1Q_VLAN)
			nf_bridge->netoutdev = (struct net_device *)out;
#endif
		return NF_STOP;
	}

	return NF_ACCEPT;
}

/* For br_nf_local_out we need (prio = NF_BR_PRI_FIRST), to insure that innocent
 * PF_BRIDGE/NF_BR_LOCAL_OUT functions don't get bridged traffic as input.
 * For br_nf_post_routing, we need (prio = NF_BR_PRI_LAST), because
 * ip_refrag() can return NF_STOLEN. */
static struct nf_hook_ops br_nf_ops[] = {
	{ .hook = br_nf_pre_routing, 
	  .owner = THIS_MODULE, 
	  .pf = PF_BRIDGE, 
	  .hooknum = NF_BR_PRE_ROUTING, 
	  .priority = NF_BR_PRI_BRNF, },
	{ .hook = br_nf_local_in,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_LOCAL_IN,
	  .priority = NF_BR_PRI_BRNF, },
	{ .hook = br_nf_forward_ip,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_FORWARD,
	  .priority = NF_BR_PRI_BRNF - 1, },
	{ .hook = br_nf_forward_arp,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_FORWARD,
	  .priority = NF_BR_PRI_BRNF, },
	{ .hook = br_nf_local_out,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_LOCAL_OUT,
	  .priority = NF_BR_PRI_FIRST, },
	{ .hook = br_nf_post_routing,
	  .owner = THIS_MODULE,
	  .pf = PF_BRIDGE,
	  .hooknum = NF_BR_POST_ROUTING,
	  .priority = NF_BR_PRI_LAST, },
	{ .hook = ip_sabotage_in,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_PRE_ROUTING,
	  .priority = NF_IP_PRI_FIRST, },
	{ .hook = ip_sabotage_in,
	  .owner = THIS_MODULE,
	  .pf = PF_INET6,
	  .hooknum = NF_IP6_PRE_ROUTING,
	  .priority = NF_IP6_PRI_FIRST, },
	{ .hook = ip_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_FORWARD,
	  .priority = NF_IP_PRI_BRIDGE_SABOTAGE_FORWARD, },
	{ .hook = ip_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET6,
	  .hooknum = NF_IP6_FORWARD,
	  .priority = NF_IP6_PRI_BRIDGE_SABOTAGE_FORWARD, },
	{ .hook = ip_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_LOCAL_OUT,
	  .priority = NF_IP_PRI_BRIDGE_SABOTAGE_LOCAL_OUT, },
	{ .hook = ip_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET6,
	  .hooknum = NF_IP6_LOCAL_OUT,
	  .priority = NF_IP6_PRI_BRIDGE_SABOTAGE_LOCAL_OUT, },
	{ .hook = ip_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET,
	  .hooknum = NF_IP_POST_ROUTING,
	  .priority = NF_IP_PRI_FIRST, },
	{ .hook = ip_sabotage_out,
	  .owner = THIS_MODULE,
	  .pf = PF_INET6,
	  .hooknum = NF_IP6_POST_ROUTING,
	  .priority = NF_IP6_PRI_FIRST, },
};

#ifdef CONFIG_SYSCTL
static
int brnf_sysctl_call_tables(ctl_table *ctl, int write, struct file * filp,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write && *(int *)(ctl->data))
		*(int *)(ctl->data) = 1;
	return ret;
}

static ctl_table brnf_table[] = {
	{
		.ctl_name	= NET_BRIDGE_NF_CALL_ARPTABLES,
		.procname	= "bridge-nf-call-arptables",
		.data		= &brnf_call_arptables,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &brnf_sysctl_call_tables,
	},
	{
		.ctl_name	= NET_BRIDGE_NF_CALL_IPTABLES,
		.procname	= "bridge-nf-call-iptables",
		.data		= &brnf_call_iptables,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &brnf_sysctl_call_tables,
	},
	{
		.ctl_name	= NET_BRIDGE_NF_CALL_IP6TABLES,
		.procname	= "bridge-nf-call-ip6tables",
		.data		= &brnf_call_ip6tables,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &brnf_sysctl_call_tables,
	},
	{
		.ctl_name	= NET_BRIDGE_NF_FILTER_VLAN_TAGGED,
		.procname	= "bridge-nf-filter-vlan-tagged",
		.data		= &brnf_filter_vlan_tagged,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &brnf_sysctl_call_tables,
	},
	{ .ctl_name = 0 }
};

static ctl_table brnf_bridge_table[] = {
	{
		.ctl_name	= NET_BRIDGE,
		.procname	= "bridge",
		.mode		= 0555,
		.child		= brnf_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table brnf_net_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= brnf_bridge_table,
	},
	{ .ctl_name = 0 }
};
#endif

int br_netfilter_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(br_nf_ops); i++) {
		int ret;

		if ((ret = nf_register_hook(&br_nf_ops[i])) >= 0)
			continue;

		while (i--)
			nf_unregister_hook(&br_nf_ops[i]);

		return ret;
	}

#ifdef CONFIG_SYSCTL
	brnf_sysctl_header = register_sysctl_table(brnf_net_table, 0);
	if (brnf_sysctl_header == NULL) {
		printk(KERN_WARNING "br_netfilter: can't register to sysctl.\n");
		for (i = 0; i < ARRAY_SIZE(br_nf_ops); i++)
			nf_unregister_hook(&br_nf_ops[i]);
		return -EFAULT;
	}
#endif

	printk(KERN_NOTICE "Bridge firewalling registered\n");

	return 0;
}

void br_netfilter_fini(void)
{
	int i;

	for (i = ARRAY_SIZE(br_nf_ops) - 1; i >= 0; i--)
		nf_unregister_hook(&br_nf_ops[i]);
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(brnf_sysctl_header);
#endif
}
