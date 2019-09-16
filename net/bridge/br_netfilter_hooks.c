// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Handle firewalling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *	Bart De Schuymer		<bdschuym@pandora.be>
 *
 *	Lennert dedicates this file to Kerstin Wurdinger.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/if_pppox.h>
#include <linux/ppp_defs.h>
#include <linux/netfilter_bridge.h>
#include <uapi/linux/netfilter_bridge.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_arp.h>
#include <linux/in_route.h>
#include <linux/rculist.h>
#include <linux/inetdevice.h>

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/route.h>
#include <net/netfilter/br_netfilter.h>
#include <net/netns/generic.h>

#include <linux/uaccess.h>
#include "br_private.h"
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

static unsigned int brnf_net_id __read_mostly;

struct brnf_net {
	bool enabled;

#ifdef CONFIG_SYSCTL
	struct ctl_table_header *ctl_hdr;
#endif

	/* default value is 1 */
	int call_iptables;
	int call_ip6tables;
	int call_arptables;

	/* default value is 0 */
	int filter_vlan_tagged;
	int filter_pppoe_tagged;
	int pass_vlan_indev;
};

#define IS_IP(skb) \
	(!skb_vlan_tag_present(skb) && skb->protocol == htons(ETH_P_IP))

#define IS_IPV6(skb) \
	(!skb_vlan_tag_present(skb) && skb->protocol == htons(ETH_P_IPV6))

#define IS_ARP(skb) \
	(!skb_vlan_tag_present(skb) && skb->protocol == htons(ETH_P_ARP))

static inline __be16 vlan_proto(const struct sk_buff *skb)
{
	if (skb_vlan_tag_present(skb))
		return skb->protocol;
	else if (skb->protocol == htons(ETH_P_8021Q))
		return vlan_eth_hdr(skb)->h_vlan_encapsulated_proto;
	else
		return 0;
}

static inline bool is_vlan_ip(const struct sk_buff *skb, const struct net *net)
{
	struct brnf_net *brnet = net_generic(net, brnf_net_id);

	return vlan_proto(skb) == htons(ETH_P_IP) && brnet->filter_vlan_tagged;
}

static inline bool is_vlan_ipv6(const struct sk_buff *skb,
				const struct net *net)
{
	struct brnf_net *brnet = net_generic(net, brnf_net_id);

	return vlan_proto(skb) == htons(ETH_P_IPV6) &&
	       brnet->filter_vlan_tagged;
}

static inline bool is_vlan_arp(const struct sk_buff *skb, const struct net *net)
{
	struct brnf_net *brnet = net_generic(net, brnf_net_id);

	return vlan_proto(skb) == htons(ETH_P_ARP) && brnet->filter_vlan_tagged;
}

static inline __be16 pppoe_proto(const struct sk_buff *skb)
{
	return *((__be16 *)(skb_mac_header(skb) + ETH_HLEN +
			    sizeof(struct pppoe_hdr)));
}

static inline bool is_pppoe_ip(const struct sk_buff *skb, const struct net *net)
{
	struct brnf_net *brnet = net_generic(net, brnf_net_id);

	return skb->protocol == htons(ETH_P_PPP_SES) &&
	       pppoe_proto(skb) == htons(PPP_IP) && brnet->filter_pppoe_tagged;
}

static inline bool is_pppoe_ipv6(const struct sk_buff *skb,
				 const struct net *net)
{
	struct brnf_net *brnet = net_generic(net, brnf_net_id);

	return skb->protocol == htons(ETH_P_PPP_SES) &&
	       pppoe_proto(skb) == htons(PPP_IPV6) &&
	       brnet->filter_pppoe_tagged;
}

/* largest possible L2 header, see br_nf_dev_queue_xmit() */
#define NF_BRIDGE_MAX_MAC_HEADER_LENGTH (PPPOE_SES_HLEN + ETH_HLEN)

struct brnf_frag_data {
	char mac[NF_BRIDGE_MAX_MAC_HEADER_LENGTH];
	u8 encap_size;
	u8 size;
	u16 vlan_tci;
	__be16 vlan_proto;
};

static DEFINE_PER_CPU(struct brnf_frag_data, brnf_frag_data_storage);

static void nf_bridge_info_free(struct sk_buff *skb)
{
	skb_ext_del(skb, SKB_EXT_BRIDGE_NF);
}

static inline struct net_device *bridge_parent(const struct net_device *dev)
{
	struct net_bridge_port *port;

	port = br_port_get_rcu(dev);
	return port ? port->br->dev : NULL;
}

static inline struct nf_bridge_info *nf_bridge_unshare(struct sk_buff *skb)
{
	return skb_ext_add(skb, SKB_EXT_BRIDGE_NF);
}

unsigned int nf_bridge_encap_header_len(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __cpu_to_be16(ETH_P_8021Q):
		return VLAN_HLEN;
	case __cpu_to_be16(ETH_P_PPP_SES):
		return PPPOE_SES_HLEN;
	default:
		return 0;
	}
}

static inline void nf_bridge_pull_encap_header(struct sk_buff *skb)
{
	unsigned int len = nf_bridge_encap_header_len(skb);

	skb_pull(skb, len);
	skb->network_header += len;
}

static inline void nf_bridge_pull_encap_header_rcsum(struct sk_buff *skb)
{
	unsigned int len = nf_bridge_encap_header_len(skb);

	skb_pull_rcsum(skb, len);
	skb->network_header += len;
}

/* When handing a packet over to the IP layer
 * check whether we have a skb that is in the
 * expected format
 */

static int br_validate_ipv4(struct net *net, struct sk_buff *skb)
{
	const struct iphdr *iph;
	u32 len;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto inhdr_error;

	iph = ip_hdr(skb);

	/* Basic sanity checks */
	if (iph->ihl < 5 || iph->version != 4)
		goto inhdr_error;

	if (!pskb_may_pull(skb, iph->ihl*4))
		goto inhdr_error;

	iph = ip_hdr(skb);
	if (unlikely(ip_fast_csum((u8 *)iph, iph->ihl)))
		goto csum_error;

	len = ntohs(iph->tot_len);
	if (skb->len < len) {
		__IP_INC_STATS(net, IPSTATS_MIB_INTRUNCATEDPKTS);
		goto drop;
	} else if (len < (iph->ihl*4))
		goto inhdr_error;

	if (pskb_trim_rcsum(skb, len)) {
		__IP_INC_STATS(net, IPSTATS_MIB_INDISCARDS);
		goto drop;
	}

	memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
	/* We should really parse IP options here but until
	 * somebody who actually uses IP options complains to
	 * us we'll just silently ignore the options because
	 * we're lazy!
	 */
	return 0;

csum_error:
	__IP_INC_STATS(net, IPSTATS_MIB_CSUMERRORS);
inhdr_error:
	__IP_INC_STATS(net, IPSTATS_MIB_INHDRERRORS);
drop:
	return -1;
}

void nf_bridge_update_protocol(struct sk_buff *skb)
{
	const struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	switch (nf_bridge->orig_proto) {
	case BRNF_PROTO_8021Q:
		skb->protocol = htons(ETH_P_8021Q);
		break;
	case BRNF_PROTO_PPPOE:
		skb->protocol = htons(ETH_P_PPP_SES);
		break;
	case BRNF_PROTO_UNCHANGED:
		break;
	}
}

/* Obtain the correct destination MAC address, while preserving the original
 * source MAC address. If we already know this address, we just copy it. If we
 * don't, we use the neighbour framework to find out. In both cases, we make
 * sure that br_handle_frame_finish() is called afterwards.
 */
int br_nf_pre_routing_finish_bridge(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct neighbour *neigh;
	struct dst_entry *dst;

	skb->dev = bridge_parent(skb->dev);
	if (!skb->dev)
		goto free_skb;
	dst = skb_dst(skb);
	neigh = dst_neigh_lookup_skb(dst, skb);
	if (neigh) {
		struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);
		int ret;

		if ((neigh->nud_state & NUD_CONNECTED) && neigh->hh.hh_len) {
			neigh_hh_bridge(&neigh->hh, skb);
			skb->dev = nf_bridge->physindev;
			ret = br_handle_frame_finish(net, sk, skb);
		} else {
			/* the neighbour function below overwrites the complete
			 * MAC header, so we save the Ethernet source address and
			 * protocol number.
			 */
			skb_copy_from_linear_data_offset(skb,
							 -(ETH_HLEN-ETH_ALEN),
							 nf_bridge->neigh_header,
							 ETH_HLEN-ETH_ALEN);
			/* tell br_dev_xmit to continue with forwarding */
			nf_bridge->bridged_dnat = 1;
			/* FIXME Need to refragment */
			ret = neigh->output(neigh, skb);
		}
		neigh_release(neigh);
		return ret;
	}
free_skb:
	kfree_skb(skb);
	return 0;
}

static inline bool
br_nf_ipv4_daddr_was_changed(const struct sk_buff *skb,
			     const struct nf_bridge_info *nf_bridge)
{
	return ip_hdr(skb)->daddr != nf_bridge->ipv4_daddr;
}

/* This requires some explaining. If DNAT has taken place,
 * we will need to fix up the destination Ethernet address.
 * This is also true when SNAT takes place (for the reply direction).
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
 * Let's first consider the case that ip_route_input() succeeds:
 *
 * If the output device equals the logical bridge device the packet
 * came in on, we can consider this bridging. The corresponding MAC
 * address will be obtained in br_nf_pre_routing_finish_bridge.
 * Otherwise, the packet is considered to be routed and we just
 * change the destination MAC address so that the packet will
 * later be passed up to the IP stack to be routed. For a redirected
 * packet, ip_route_input() will give back the localhost as output device,
 * which differs from the bridge device.
 *
 * Let's now consider the case that ip_route_input() fails:
 *
 * This can be because the destination address is martian, in which case
 * the packet will be dropped.
 * If IP forwarding is disabled, ip_route_input() will fail, while
 * ip_route_output_key() can return success. The source
 * address for ip_route_output_key() is set to zero, so ip_route_output_key()
 * thinks we're handling a locally generated packet and won't care
 * if IP forwarding is enabled. If the output device equals the logical bridge
 * device, we proceed as if ip_route_input() succeeded. If it differs from the
 * logical bridge port or if ip_route_output_key() fails we drop the packet.
 */
static int br_nf_pre_routing_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct iphdr *iph = ip_hdr(skb);
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);
	struct rtable *rt;
	int err;

	nf_bridge->frag_max_size = IPCB(skb)->frag_max_size;

	if (nf_bridge->pkt_otherhost) {
		skb->pkt_type = PACKET_OTHERHOST;
		nf_bridge->pkt_otherhost = false;
	}
	nf_bridge->in_prerouting = 0;
	if (br_nf_ipv4_daddr_was_changed(skb, nf_bridge)) {
		if ((err = ip_route_input(skb, iph->daddr, iph->saddr, iph->tos, dev))) {
			struct in_device *in_dev = __in_dev_get_rcu(dev);

			/* If err equals -EHOSTUNREACH the error is due to a
			 * martian destination or due to the fact that
			 * forwarding is disabled. For most martian packets,
			 * ip_route_output_key() will fail. It won't fail for 2 types of
			 * martian destinations: loopback destinations and destination
			 * 0.0.0.0. In both cases the packet will be dropped because the
			 * destination is the loopback device and not the bridge. */
			if (err != -EHOSTUNREACH || !in_dev || IN_DEV_FORWARD(in_dev))
				goto free_skb;

			rt = ip_route_output(net, iph->daddr, 0,
					     RT_TOS(iph->tos), 0);
			if (!IS_ERR(rt)) {
				/* - Bridged-and-DNAT'ed traffic doesn't
				 *   require ip_forwarding. */
				if (rt->dst.dev == dev) {
					skb_dst_set(skb, &rt->dst);
					goto bridged_dnat;
				}
				ip_rt_put(rt);
			}
free_skb:
			kfree_skb(skb);
			return 0;
		} else {
			if (skb_dst(skb)->dev == dev) {
bridged_dnat:
				skb->dev = nf_bridge->physindev;
				nf_bridge_update_protocol(skb);
				nf_bridge_push_encap_header(skb);
				br_nf_hook_thresh(NF_BR_PRE_ROUTING,
						  net, sk, skb, skb->dev,
						  NULL,
						  br_nf_pre_routing_finish_bridge);
				return 0;
			}
			ether_addr_copy(eth_hdr(skb)->h_dest, dev->dev_addr);
			skb->pkt_type = PACKET_HOST;
		}
	} else {
		rt = bridge_parent_rtable(nf_bridge->physindev);
		if (!rt) {
			kfree_skb(skb);
			return 0;
		}
		skb_dst_set_noref(skb, &rt->dst);
	}

	skb->dev = nf_bridge->physindev;
	nf_bridge_update_protocol(skb);
	nf_bridge_push_encap_header(skb);
	br_nf_hook_thresh(NF_BR_PRE_ROUTING, net, sk, skb, skb->dev, NULL,
			  br_handle_frame_finish);
	return 0;
}

static struct net_device *brnf_get_logical_dev(struct sk_buff *skb,
					       const struct net_device *dev,
					       const struct net *net)
{
	struct net_device *vlan, *br;
	struct brnf_net *brnet = net_generic(net, brnf_net_id);

	br = bridge_parent(dev);

	if (brnet->pass_vlan_indev == 0 || !skb_vlan_tag_present(skb))
		return br;

	vlan = __vlan_find_dev_deep_rcu(br, skb->vlan_proto,
				    skb_vlan_tag_get(skb) & VLAN_VID_MASK);

	return vlan ? vlan : br;
}

/* Some common code for IPv4/IPv6 */
struct net_device *setup_pre_routing(struct sk_buff *skb, const struct net *net)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->pkt_otherhost = true;
	}

	nf_bridge->in_prerouting = 1;
	nf_bridge->physindev = skb->dev;
	skb->dev = brnf_get_logical_dev(skb, skb->dev, net);

	if (skb->protocol == htons(ETH_P_8021Q))
		nf_bridge->orig_proto = BRNF_PROTO_8021Q;
	else if (skb->protocol == htons(ETH_P_PPP_SES))
		nf_bridge->orig_proto = BRNF_PROTO_PPPOE;

	/* Must drop socket now because of tproxy. */
	skb_orphan(skb);
	return skb->dev;
}

/* Direct IPv6 traffic to br_nf_pre_routing_ipv6.
 * Replicate the checks that IPv4 does on packet reception.
 * Set skb->dev to the bridge device (i.e. parent of the
 * receiving device) to make netfilter happy, the REDIRECT
 * target in particular.  Save the original destination IP
 * address to be able to detect DNAT afterwards. */
static unsigned int br_nf_pre_routing(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct nf_bridge_info *nf_bridge;
	struct net_bridge_port *p;
	struct net_bridge *br;
	__u32 len = nf_bridge_encap_header_len(skb);
	struct brnf_net *brnet;

	if (unlikely(!pskb_may_pull(skb, len)))
		return NF_DROP;

	p = br_port_get_rcu(state->in);
	if (p == NULL)
		return NF_DROP;
	br = p->br;

	brnet = net_generic(state->net, brnf_net_id);
	if (IS_IPV6(skb) || is_vlan_ipv6(skb, state->net) ||
	    is_pppoe_ipv6(skb, state->net)) {
		if (!brnet->call_ip6tables &&
		    !br_opt_get(br, BROPT_NF_CALL_IP6TABLES))
			return NF_ACCEPT;
		if (!ipv6_mod_enabled()) {
			pr_warn_once("Module ipv6 is disabled, so call_ip6tables is not supported.");
			return NF_DROP;
		}

		nf_bridge_pull_encap_header_rcsum(skb);
		return br_nf_pre_routing_ipv6(priv, skb, state);
	}

	if (!brnet->call_iptables && !br_opt_get(br, BROPT_NF_CALL_IPTABLES))
		return NF_ACCEPT;

	if (!IS_IP(skb) && !is_vlan_ip(skb, state->net) &&
	    !is_pppoe_ip(skb, state->net))
		return NF_ACCEPT;

	nf_bridge_pull_encap_header_rcsum(skb);

	if (br_validate_ipv4(state->net, skb))
		return NF_DROP;

	if (!nf_bridge_alloc(skb))
		return NF_DROP;
	if (!setup_pre_routing(skb, state->net))
		return NF_DROP;

	nf_bridge = nf_bridge_info_get(skb);
	nf_bridge->ipv4_daddr = ip_hdr(skb)->daddr;

	skb->protocol = htons(ETH_P_IP);
	skb->transport_header = skb->network_header + ip_hdr(skb)->ihl * 4;

	NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, state->net, state->sk, skb,
		skb->dev, NULL,
		br_nf_pre_routing_finish);

	return NF_STOLEN;
}


/* PF_BRIDGE/FORWARD *************************************************/
static int br_nf_forward_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);
	struct net_device *in;

	if (!IS_ARP(skb) && !is_vlan_arp(skb, net)) {

		if (skb->protocol == htons(ETH_P_IP))
			nf_bridge->frag_max_size = IPCB(skb)->frag_max_size;

		if (skb->protocol == htons(ETH_P_IPV6))
			nf_bridge->frag_max_size = IP6CB(skb)->frag_max_size;

		in = nf_bridge->physindev;
		if (nf_bridge->pkt_otherhost) {
			skb->pkt_type = PACKET_OTHERHOST;
			nf_bridge->pkt_otherhost = false;
		}
		nf_bridge_update_protocol(skb);
	} else {
		in = *((struct net_device **)(skb->cb));
	}
	nf_bridge_push_encap_header(skb);

	br_nf_hook_thresh(NF_BR_FORWARD, net, sk, skb, in, skb->dev,
			  br_forward_finish);
	return 0;
}


/* This is the 'purely bridged' case.  For IP, we pass the packet to
 * netfilter with indev and outdev set to the bridge device,
 * but we are still able to filter on the 'real' indev/outdev
 * because of the physdev module. For ARP, indev and outdev are the
 * bridge ports. */
static unsigned int br_nf_forward_ip(void *priv,
				     struct sk_buff *skb,
				     const struct nf_hook_state *state)
{
	struct nf_bridge_info *nf_bridge;
	struct net_device *parent;
	u_int8_t pf;

	nf_bridge = nf_bridge_info_get(skb);
	if (!nf_bridge)
		return NF_ACCEPT;

	/* Need exclusive nf_bridge_info since we might have multiple
	 * different physoutdevs. */
	if (!nf_bridge_unshare(skb))
		return NF_DROP;

	nf_bridge = nf_bridge_info_get(skb);
	if (!nf_bridge)
		return NF_DROP;

	parent = bridge_parent(state->out);
	if (!parent)
		return NF_DROP;

	if (IS_IP(skb) || is_vlan_ip(skb, state->net) ||
	    is_pppoe_ip(skb, state->net))
		pf = NFPROTO_IPV4;
	else if (IS_IPV6(skb) || is_vlan_ipv6(skb, state->net) ||
		 is_pppoe_ipv6(skb, state->net))
		pf = NFPROTO_IPV6;
	else
		return NF_ACCEPT;

	nf_bridge_pull_encap_header(skb);

	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->pkt_otherhost = true;
	}

	if (pf == NFPROTO_IPV4) {
		if (br_validate_ipv4(state->net, skb))
			return NF_DROP;
		IPCB(skb)->frag_max_size = nf_bridge->frag_max_size;
	}

	if (pf == NFPROTO_IPV6) {
		if (br_validate_ipv6(state->net, skb))
			return NF_DROP;
		IP6CB(skb)->frag_max_size = nf_bridge->frag_max_size;
	}

	nf_bridge->physoutdev = skb->dev;
	if (pf == NFPROTO_IPV4)
		skb->protocol = htons(ETH_P_IP);
	else
		skb->protocol = htons(ETH_P_IPV6);

	NF_HOOK(pf, NF_INET_FORWARD, state->net, NULL, skb,
		brnf_get_logical_dev(skb, state->in, state->net),
		parent,	br_nf_forward_finish);

	return NF_STOLEN;
}

static unsigned int br_nf_forward_arp(void *priv,
				      struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct net_bridge_port *p;
	struct net_bridge *br;
	struct net_device **d = (struct net_device **)(skb->cb);
	struct brnf_net *brnet;

	p = br_port_get_rcu(state->out);
	if (p == NULL)
		return NF_ACCEPT;
	br = p->br;

	brnet = net_generic(state->net, brnf_net_id);
	if (!brnet->call_arptables && !br_opt_get(br, BROPT_NF_CALL_ARPTABLES))
		return NF_ACCEPT;

	if (!IS_ARP(skb)) {
		if (!is_vlan_arp(skb, state->net))
			return NF_ACCEPT;
		nf_bridge_pull_encap_header(skb);
	}

	if (arp_hdr(skb)->ar_pln != 4) {
		if (is_vlan_arp(skb, state->net))
			nf_bridge_push_encap_header(skb);
		return NF_ACCEPT;
	}
	*d = state->in;
	NF_HOOK(NFPROTO_ARP, NF_ARP_FORWARD, state->net, state->sk, skb,
		state->in, state->out, br_nf_forward_finish);

	return NF_STOLEN;
}

static int br_nf_push_frag_xmit(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct brnf_frag_data *data;
	int err;

	data = this_cpu_ptr(&brnf_frag_data_storage);
	err = skb_cow_head(skb, data->size);

	if (err) {
		kfree_skb(skb);
		return 0;
	}

	if (data->vlan_proto)
		__vlan_hwaccel_put_tag(skb, data->vlan_proto, data->vlan_tci);

	skb_copy_to_linear_data_offset(skb, -data->size, data->mac, data->size);
	__skb_push(skb, data->encap_size);

	nf_bridge_info_free(skb);
	return br_dev_queue_push_xmit(net, sk, skb);
}

static int
br_nf_ip_fragment(struct net *net, struct sock *sk, struct sk_buff *skb,
		  int (*output)(struct net *, struct sock *, struct sk_buff *))
{
	unsigned int mtu = ip_skb_dst_mtu(sk, skb);
	struct iphdr *iph = ip_hdr(skb);

	if (unlikely(((iph->frag_off & htons(IP_DF)) && !skb->ignore_df) ||
		     (IPCB(skb)->frag_max_size &&
		      IPCB(skb)->frag_max_size > mtu))) {
		IP_INC_STATS(net, IPSTATS_MIB_FRAGFAILS);
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	return ip_do_fragment(net, sk, skb, output);
}

static unsigned int nf_bridge_mtu_reduction(const struct sk_buff *skb)
{
	const struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	if (nf_bridge->orig_proto == BRNF_PROTO_PPPOE)
		return PPPOE_SES_HLEN;
	return 0;
}

static int br_nf_dev_queue_xmit(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);
	unsigned int mtu, mtu_reserved;

	mtu_reserved = nf_bridge_mtu_reduction(skb);
	mtu = skb->dev->mtu;

	if (nf_bridge->frag_max_size && nf_bridge->frag_max_size < mtu)
		mtu = nf_bridge->frag_max_size;

	if (skb_is_gso(skb) || skb->len + mtu_reserved <= mtu) {
		nf_bridge_info_free(skb);
		return br_dev_queue_push_xmit(net, sk, skb);
	}

	/* This is wrong! We should preserve the original fragment
	 * boundaries by preserving frag_list rather than refragmenting.
	 */
	if (IS_ENABLED(CONFIG_NF_DEFRAG_IPV4) &&
	    skb->protocol == htons(ETH_P_IP)) {
		struct brnf_frag_data *data;

		if (br_validate_ipv4(net, skb))
			goto drop;

		IPCB(skb)->frag_max_size = nf_bridge->frag_max_size;

		nf_bridge_update_protocol(skb);

		data = this_cpu_ptr(&brnf_frag_data_storage);

		if (skb_vlan_tag_present(skb)) {
			data->vlan_tci = skb->vlan_tci;
			data->vlan_proto = skb->vlan_proto;
		} else {
			data->vlan_proto = 0;
		}

		data->encap_size = nf_bridge_encap_header_len(skb);
		data->size = ETH_HLEN + data->encap_size;

		skb_copy_from_linear_data_offset(skb, -data->size, data->mac,
						 data->size);

		return br_nf_ip_fragment(net, sk, skb, br_nf_push_frag_xmit);
	}
	if (IS_ENABLED(CONFIG_NF_DEFRAG_IPV6) &&
	    skb->protocol == htons(ETH_P_IPV6)) {
		const struct nf_ipv6_ops *v6ops = nf_get_ipv6_ops();
		struct brnf_frag_data *data;

		if (br_validate_ipv6(net, skb))
			goto drop;

		IP6CB(skb)->frag_max_size = nf_bridge->frag_max_size;

		nf_bridge_update_protocol(skb);

		data = this_cpu_ptr(&brnf_frag_data_storage);
		data->encap_size = nf_bridge_encap_header_len(skb);
		data->size = ETH_HLEN + data->encap_size;

		skb_copy_from_linear_data_offset(skb, -data->size, data->mac,
						 data->size);

		if (v6ops)
			return v6ops->fragment(net, sk, skb, br_nf_push_frag_xmit);

		kfree_skb(skb);
		return -EMSGSIZE;
	}
	nf_bridge_info_free(skb);
	return br_dev_queue_push_xmit(net, sk, skb);
 drop:
	kfree_skb(skb);
	return 0;
}

/* PF_BRIDGE/POST_ROUTING ********************************************/
static unsigned int br_nf_post_routing(void *priv,
				       struct sk_buff *skb,
				       const struct nf_hook_state *state)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);
	struct net_device *realoutdev = bridge_parent(skb->dev);
	u_int8_t pf;

	/* if nf_bridge is set, but ->physoutdev is NULL, this packet came in
	 * on a bridge, but was delivered locally and is now being routed:
	 *
	 * POST_ROUTING was already invoked from the ip stack.
	 */
	if (!nf_bridge || !nf_bridge->physoutdev)
		return NF_ACCEPT;

	if (!realoutdev)
		return NF_DROP;

	if (IS_IP(skb) || is_vlan_ip(skb, state->net) ||
	    is_pppoe_ip(skb, state->net))
		pf = NFPROTO_IPV4;
	else if (IS_IPV6(skb) || is_vlan_ipv6(skb, state->net) ||
		 is_pppoe_ipv6(skb, state->net))
		pf = NFPROTO_IPV6;
	else
		return NF_ACCEPT;

	/* We assume any code from br_dev_queue_push_xmit onwards doesn't care
	 * about the value of skb->pkt_type. */
	if (skb->pkt_type == PACKET_OTHERHOST) {
		skb->pkt_type = PACKET_HOST;
		nf_bridge->pkt_otherhost = true;
	}

	nf_bridge_pull_encap_header(skb);
	if (pf == NFPROTO_IPV4)
		skb->protocol = htons(ETH_P_IP);
	else
		skb->protocol = htons(ETH_P_IPV6);

	NF_HOOK(pf, NF_INET_POST_ROUTING, state->net, state->sk, skb,
		NULL, realoutdev,
		br_nf_dev_queue_xmit);

	return NF_STOLEN;
}

/* IP/SABOTAGE *****************************************************/
/* Don't hand locally destined packets to PF_INET(6)/PRE_ROUTING
 * for the second time. */
static unsigned int ip_sabotage_in(void *priv,
				   struct sk_buff *skb,
				   const struct nf_hook_state *state)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	if (nf_bridge && !nf_bridge->in_prerouting &&
	    !netif_is_l3_master(skb->dev) &&
	    !netif_is_l3_slave(skb->dev)) {
		state->okfn(state->net, state->sk, skb);
		return NF_STOLEN;
	}

	return NF_ACCEPT;
}

/* This is called when br_netfilter has called into iptables/netfilter,
 * and DNAT has taken place on a bridge-forwarded packet.
 *
 * neigh->output has created a new MAC header, with local br0 MAC
 * as saddr.
 *
 * This restores the original MAC saddr of the bridged packet
 * before invoking bridge forward logic to transmit the packet.
 */
static void br_nf_pre_routing_finish_bridge_slow(struct sk_buff *skb)
{
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	skb_pull(skb, ETH_HLEN);
	nf_bridge->bridged_dnat = 0;

	BUILD_BUG_ON(sizeof(nf_bridge->neigh_header) != (ETH_HLEN - ETH_ALEN));

	skb_copy_to_linear_data_offset(skb, -(ETH_HLEN - ETH_ALEN),
				       nf_bridge->neigh_header,
				       ETH_HLEN - ETH_ALEN);
	skb->dev = nf_bridge->physindev;

	nf_bridge->physoutdev = NULL;
	br_handle_frame_finish(dev_net(skb->dev), NULL, skb);
}

static int br_nf_dev_xmit(struct sk_buff *skb)
{
	const struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	if (nf_bridge && nf_bridge->bridged_dnat) {
		br_nf_pre_routing_finish_bridge_slow(skb);
		return 1;
	}
	return 0;
}

static const struct nf_br_ops br_ops = {
	.br_dev_xmit_hook =	br_nf_dev_xmit,
};

/* For br_nf_post_routing, we need (prio = NF_BR_PRI_LAST), because
 * br_dev_queue_push_xmit is called afterwards */
static const struct nf_hook_ops br_nf_ops[] = {
	{
		.hook = br_nf_pre_routing,
		.pf = NFPROTO_BRIDGE,
		.hooknum = NF_BR_PRE_ROUTING,
		.priority = NF_BR_PRI_BRNF,
	},
	{
		.hook = br_nf_forward_ip,
		.pf = NFPROTO_BRIDGE,
		.hooknum = NF_BR_FORWARD,
		.priority = NF_BR_PRI_BRNF - 1,
	},
	{
		.hook = br_nf_forward_arp,
		.pf = NFPROTO_BRIDGE,
		.hooknum = NF_BR_FORWARD,
		.priority = NF_BR_PRI_BRNF,
	},
	{
		.hook = br_nf_post_routing,
		.pf = NFPROTO_BRIDGE,
		.hooknum = NF_BR_POST_ROUTING,
		.priority = NF_BR_PRI_LAST,
	},
	{
		.hook = ip_sabotage_in,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_PRE_ROUTING,
		.priority = NF_IP_PRI_FIRST,
	},
	{
		.hook = ip_sabotage_in,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_PRE_ROUTING,
		.priority = NF_IP6_PRI_FIRST,
	},
};

static int brnf_device_event(struct notifier_block *unused, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct brnf_net *brnet;
	struct net *net;
	int ret;

	if (event != NETDEV_REGISTER || !(dev->priv_flags & IFF_EBRIDGE))
		return NOTIFY_DONE;

	ASSERT_RTNL();

	net = dev_net(dev);
	brnet = net_generic(net, brnf_net_id);
	if (brnet->enabled)
		return NOTIFY_OK;

	ret = nf_register_net_hooks(net, br_nf_ops, ARRAY_SIZE(br_nf_ops));
	if (ret)
		return NOTIFY_BAD;

	brnet->enabled = true;
	return NOTIFY_OK;
}

static struct notifier_block brnf_notifier __read_mostly = {
	.notifier_call = brnf_device_event,
};

/* recursively invokes nf_hook_slow (again), skipping already-called
 * hooks (< NF_BR_PRI_BRNF).
 *
 * Called with rcu read lock held.
 */
int br_nf_hook_thresh(unsigned int hook, struct net *net,
		      struct sock *sk, struct sk_buff *skb,
		      struct net_device *indev,
		      struct net_device *outdev,
		      int (*okfn)(struct net *, struct sock *,
				  struct sk_buff *))
{
	const struct nf_hook_entries *e;
	struct nf_hook_state state;
	struct nf_hook_ops **ops;
	unsigned int i;
	int ret;

	e = rcu_dereference(net->nf.hooks_bridge[hook]);
	if (!e)
		return okfn(net, sk, skb);

	ops = nf_hook_entries_get_hook_ops(e);
	for (i = 0; i < e->num_hook_entries &&
	      ops[i]->priority <= NF_BR_PRI_BRNF; i++)
		;

	nf_hook_state_init(&state, hook, NFPROTO_BRIDGE, indev, outdev,
			   sk, net, okfn);

	ret = nf_hook_slow(skb, &state, e, i);
	if (ret == 1)
		ret = okfn(net, sk, skb);

	return ret;
}

#ifdef CONFIG_SYSCTL
static
int brnf_sysctl_call_tables(struct ctl_table *ctl, int write,
			    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (write && *(int *)(ctl->data))
		*(int *)(ctl->data) = 1;
	return ret;
}

static struct ctl_table brnf_table[] = {
	{
		.procname	= "bridge-nf-call-arptables",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= brnf_sysctl_call_tables,
	},
	{
		.procname	= "bridge-nf-call-iptables",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= brnf_sysctl_call_tables,
	},
	{
		.procname	= "bridge-nf-call-ip6tables",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= brnf_sysctl_call_tables,
	},
	{
		.procname	= "bridge-nf-filter-vlan-tagged",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= brnf_sysctl_call_tables,
	},
	{
		.procname	= "bridge-nf-filter-pppoe-tagged",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= brnf_sysctl_call_tables,
	},
	{
		.procname	= "bridge-nf-pass-vlan-input-dev",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= brnf_sysctl_call_tables,
	},
	{ }
};

static inline void br_netfilter_sysctl_default(struct brnf_net *brnf)
{
	brnf->call_iptables = 1;
	brnf->call_ip6tables = 1;
	brnf->call_arptables = 1;
	brnf->filter_vlan_tagged = 0;
	brnf->filter_pppoe_tagged = 0;
	brnf->pass_vlan_indev = 0;
}

static int br_netfilter_sysctl_init_net(struct net *net)
{
	struct ctl_table *table = brnf_table;
	struct brnf_net *brnet;

	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(brnf_table), GFP_KERNEL);
		if (!table)
			return -ENOMEM;
	}

	brnet = net_generic(net, brnf_net_id);
	table[0].data = &brnet->call_arptables;
	table[1].data = &brnet->call_iptables;
	table[2].data = &brnet->call_ip6tables;
	table[3].data = &brnet->filter_vlan_tagged;
	table[4].data = &brnet->filter_pppoe_tagged;
	table[5].data = &brnet->pass_vlan_indev;

	br_netfilter_sysctl_default(brnet);

	brnet->ctl_hdr = register_net_sysctl(net, "net/bridge", table);
	if (!brnet->ctl_hdr) {
		if (!net_eq(net, &init_net))
			kfree(table);

		return -ENOMEM;
	}

	return 0;
}

static void br_netfilter_sysctl_exit_net(struct net *net,
					 struct brnf_net *brnet)
{
	struct ctl_table *table = brnet->ctl_hdr->ctl_table_arg;

	unregister_net_sysctl_table(brnet->ctl_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}

static int __net_init brnf_init_net(struct net *net)
{
	return br_netfilter_sysctl_init_net(net);
}
#endif

static void __net_exit brnf_exit_net(struct net *net)
{
	struct brnf_net *brnet;

	brnet = net_generic(net, brnf_net_id);
	if (brnet->enabled) {
		nf_unregister_net_hooks(net, br_nf_ops, ARRAY_SIZE(br_nf_ops));
		brnet->enabled = false;
	}

#ifdef CONFIG_SYSCTL
	br_netfilter_sysctl_exit_net(net, brnet);
#endif
}

static struct pernet_operations brnf_net_ops __read_mostly = {
#ifdef CONFIG_SYSCTL
	.init = brnf_init_net,
#endif
	.exit = brnf_exit_net,
	.id   = &brnf_net_id,
	.size = sizeof(struct brnf_net),
};

static int __init br_netfilter_init(void)
{
	int ret;

	ret = register_pernet_subsys(&brnf_net_ops);
	if (ret < 0)
		return ret;

	ret = register_netdevice_notifier(&brnf_notifier);
	if (ret < 0) {
		unregister_pernet_subsys(&brnf_net_ops);
		return ret;
	}

	RCU_INIT_POINTER(nf_br_ops, &br_ops);
	printk(KERN_NOTICE "Bridge firewalling registered\n");
	return 0;
}

static void __exit br_netfilter_fini(void)
{
	RCU_INIT_POINTER(nf_br_ops, NULL);
	unregister_netdevice_notifier(&brnf_notifier);
	unregister_pernet_subsys(&brnf_net_ops);
}

module_init(br_netfilter_init);
module_exit(br_netfilter_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lennert Buytenhek <buytenh@gnu.org>");
MODULE_AUTHOR("Bart De Schuymer <bdschuym@pandora.be>");
MODULE_DESCRIPTION("Linux ethernet netfilter firewall bridge");
